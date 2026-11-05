#!/usr/bin/env python3
"""Record thin macOS builds and assemble a fail-closed universal2 staging tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

CONTENT_PACK_NAMES = tuple(
    pack["name"]
    for pack in json.loads((Path(__file__).resolve().with_name("content_packs.json")).read_text(encoding="utf-8"))["packs"]
)

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from game_layer import PACKAGED_LAYER_GAME_DIRS, RETIRED_LAYER_GAME_DIRS  # noqa: E402


THIN_MANIFEST_NAME = "OPENQ4-MACOS-THIN.json"
THIN_MANIFEST_FORMAT = 1
ASSEMBLY_MANIFEST_FORMAT = 1
UNIVERSAL_ARCH = "universal2"
UNIVERSAL_MACHO_ARCHES = frozenset(("arm64", "x86_64"))
THIN_MACHO_ARCHES = {
    "arm64": frozenset(("arm64",)),
    "x64": frozenset(("x86_64",)),
}
BUILD_TYPES = ("debug", "debugoptimized", "release", "minsize")
GRAPHICS_BRIDGES = ("opengl", "metal")
OPENAL_PROVIDERS = ("apple_framework", "system")
SOURCE_PROVENANCE_FIELDS = (
    "projectGitCommit",
    "projectGitDirty",
    "gameLibsGitCommit",
    "gameLibsGitDirty",
)
COMMON_BUILD_FIELDS = (
    *SOURCE_PROVENANCE_FIELDS,
    "stagedSourceSha256",
    "graphicsBridge",
    "openALProvider",
    "deploymentTarget",
    "buildType",
)
# libMoltenVK.dylib is a third-party Vulkan-on-Metal translation layer that
# openQ4 does not build. It ships as a genuinely universal (arm64 + x86_64)
# binary and flows through as shared payload. It is deliberately NOT a
# CODE_KEY: lipo must not touch it, its install name must not be rewritten, and
# it has no openQ4 entry point to check. Apple's ad-hoc signer can emit different
# signature envelopes on Intel and Apple Silicon hosts, so assembly validates
# both signed inputs and compares temporary signature-stripped copies. All
# unsigned MoltenVK bytes must still match exactly, and copy_shared_payload()
# reproduces the validated ARM64-host copy before final package signing.
MOLTENVK_DYLIB_NAME = "libMoltenVK.dylib"
OPENAL_SOFT_DYLIB_NAME = "libopenal.1.dylib"
OPENAL_SOFT_INSTALL_NAME = f"@rpath/{OPENAL_SOFT_DYLIB_NAME}"
OPENAL_SOFT_RELATIVE_PATH = Path("Frameworks") / OPENAL_SOFT_DYLIB_NAME
REQUIRED_SHARED_PATHS = (
    "openQ4.icns",
    "collect_macos_support_info.sh",
    "assets/splash/quake4_rt_bitmap_4001.bmp",
    "baseoq4/mod.json",
    *(f"baseoq4/{name}" for name in CONTENT_PACK_NAMES),
    MOLTENVK_DYLIB_NAME,
    "licenses/openal-soft/COPYING",
    "licenses/openal-soft/LICENSE-pffft",
    "licenses/openal-soft/LICENSE-fmt",
    "licenses/openal-soft/LICENSE-gsl",
    "licenses/openal-soft/SOURCE.md",
    "licenses/openal-soft/openal-soft-1.25.2.tar.gz",
)
# Code files that lipo merges into one universal2 artifact. renderer-vk is the
# macOS Vulkan renderer module; renderer-gl is not built on darwin, so it has no
# entry here (the stale-code sweep below still rejects one if it ever appears).
CODE_KEYS = ("client", "dedicated", "game-sp", "game-mp", "renderer-vk", "openal-soft")
# Loadable modules carry a @loader_path install name and one required export.
MODULE_ENTRY_POINT_EXPORTS = {
    "game-sp": "GetGameAPI",
    "game-mp": "GetGameAPI",
    "renderer-vk": "GetRenderAPI",
}
# A game-library layer (game_layer.PACKAGED_LAYER_GAME_DIRS, q4xbase) stages its own game
# modules beside baseoq4's. They are optional code keys, "q4xbase/game-sp" and
# "q4xbase/game-mp": a layer is staged whole (both modules and its mod.json, which is shared
# payload like baseoq4's) in both thin slices, or in neither.
LAYER_MODULE_STEMS = ("game-sp", "game-mp")


def module_stem(code_key: str) -> str:
    """The module kind a code key names: "q4xbase/game-sp" is a game-sp module."""
    return code_key.rsplit("/", 1)[-1]


def layer_code_keys(layer: str) -> tuple[str, ...]:
    return tuple(f"{layer}/{stem}" for stem in LAYER_MODULE_STEMS)


def optional_thin_code_paths(arch: str) -> dict[str, Path]:
    return {
        f"{layer}/{stem}": Path(layer) / f"{stem}_{arch}.dylib"
        for layer in PACKAGED_LAYER_GAME_DIRS
        for stem in LAYER_MODULE_STEMS
    }


def optional_universal_code_paths() -> dict[str, Path]:
    return {
        f"{layer}/{stem}": Path(layer) / f"{stem}_{UNIVERSAL_ARCH}.dylib"
        for layer in PACKAGED_LAYER_GAME_DIRS
        for stem in LAYER_MODULE_STEMS
    }


class Universal2Error(RuntimeError):
    """Raised when thin provenance or universal2 assembly is unsafe."""


def thin_code_paths(arch: str) -> dict[str, Path]:
    if arch not in THIN_MACHO_ARCHES:
        raise Universal2Error(f"unsupported thin macOS architecture: {arch}")
    return {
        "client": Path(f"openQ4-client_{arch}"),
        "dedicated": Path(f"openQ4-ded_{arch}"),
        "game-sp": Path("baseoq4") / f"game-sp_{arch}.dylib",
        "game-mp": Path("baseoq4") / f"game-mp_{arch}.dylib",
        # meson installs the renderer module beside the engine binaries in the
        # install root; package_nightly.py is what relocates it into
        # openQ4.app/Contents/Frameworks.
        "renderer-vk": Path(f"renderer-vk_{arch}.dylib"),
        "openal-soft": OPENAL_SOFT_RELATIVE_PATH,
    }


def universal_code_paths() -> dict[str, Path]:
    # A fused package carries ONE file per code key, so every per-arch name
    # collapses onto the same "universal2" arch token the game modules already
    # use (openQ4-client_universal2, game-sp_universal2.dylib, ...).
    return {
        "client": Path("openQ4-client_universal2"),
        "dedicated": Path("openQ4-ded_universal2"),
        "game-sp": Path("baseoq4") / "game-sp_universal2.dylib",
        "game-mp": Path("baseoq4") / "game-mp_universal2.dylib",
        "renderer-vk": Path("renderer-vk_universal2.dylib"),
        "openal-soft": OPENAL_SOFT_RELATIVE_PATH,
    }


def expected_install_name(code_key: str, arch: str) -> str:
    if code_key == "openal-soft":
        return OPENAL_SOFT_INSTALL_NAME
    # a layer's modules keep their own file name as ID, as meson links them
    stem = module_stem(code_key)
    if stem not in MODULE_ENTRY_POINT_EXPORTS:
        return ""
    return f"@loader_path/{stem}_{arch}.dylib"


def staged_code_paths(root: Path, arch: str) -> dict[str, Path]:
    """The code files a thin tree holds: the required set plus each staged layer's modules."""
    paths = thin_code_paths(arch)
    optional = optional_thin_code_paths(arch)
    for layer in PACKAGED_LAYER_GAME_DIRS:
        keys = layer_code_keys(layer)
        present = [key for key in keys if os.path.lexists(root / optional[key])]
        has_manifest = os.path.lexists(root / layer / "mod.json")
        if present or has_manifest:
            if len(present) != len(keys) or not has_manifest:
                needed = ", ".join([*(optional[key].as_posix() for key in keys), f"{layer}/mod.json"])
                raise Universal2Error(f"thin macOS payload has an incomplete {layer}/ layer; it needs {needed}")
            paths.update({key: optional[key] for key in keys})
    return paths


def staged_layer_keys(binaries: dict[str, object]) -> tuple[str, ...]:
    """The optional layer code keys a thin manifest records, in a stable order."""
    return tuple(key for key in optional_thin_code_paths("arm64") if key in binaries)


def prepare_thin_payload(root: Path, arch: str) -> None:
    """Normalize and re-sign loadable thin modules before provenance recording."""
    if arch not in THIN_MACHO_ARCHES:
        raise Universal2Error(f"unsupported thin macOS architecture: {arch}")
    root = require_directory(root, "thin macOS install root")
    install_name_tool = require_tool("install_name_tool")
    codesign = require_tool("codesign")
    expected_arches = THIN_MACHO_ARCHES[arch]
    macho_arch = next(iter(expected_arches))

    for code_key, relative in staged_code_paths(root, arch).items():
        expected_id = expected_install_name(code_key, arch)
        if not expected_id:
            continue

        path = require_regular_file(root / relative, f"thin macOS {code_key} binary", executable=True)
        validate_exact_arches(path, expected_arches)
        if install_name(path, macho_arch=macho_arch) != expected_id:
            run_command(
                [install_name_tool, "-id", expected_id, str(path)],
                f"normalizing thin {arch} {code_key} install name",
            )
        # Meson install may rewrite the load command, invalidating a linker-
        # produced ad-hoc signature. Sign and verify every loadable module so an
        # already-correct ID cannot conceal an invalid signature. Universal
        # assembly fuses these signed inputs unchanged, then normalizes and
        # re-signs the fused output; packaging applies its final signature later.
        run_command(
            [codesign, "--force", "--sign", "-", str(path)],
            f"ad-hoc signing normalized thin {arch} {code_key}",
        )
        run_command(
            [codesign, "--verify", "--strict", str(path)],
            f"verifying normalized thin {arch} {code_key} signature",
        )
        actual_id = install_name(path, macho_arch=macho_arch)
        if actual_id != expected_id:
            raise Universal2Error(
                f"install name normalization failed for {path}: expected {expected_id!r}, found {actual_id!r}"
            )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def require_no_symlink_components(path: Path, label: str) -> None:
    absolute = Path(os.path.abspath(path))
    for candidate in (absolute, *absolute.parents):
        if candidate.exists() and candidate.is_symlink():
            raise Universal2Error(f"{label} must not contain symlink components: {candidate}")


def require_directory(path: Path, label: str) -> Path:
    require_no_symlink_components(path, label)
    resolved = path.resolve()
    if not resolved.is_dir():
        raise Universal2Error(f"{label} is not a directory: {resolved}")
    return resolved


def require_regular_file(path: Path, label: str, *, executable: bool = False) -> Path:
    require_no_symlink_components(path, label)
    try:
        mode = path.stat().st_mode
    except OSError as exc:
        raise Universal2Error(f"{label} is unreadable: {path}") from exc
    if not stat.S_ISREG(mode):
        raise Universal2Error(f"{label} is not a regular file: {path}")
    if executable and os.name != "nt" and mode & 0o111 == 0:
        raise Universal2Error(f"{label} is not executable: {path}")
    return path.resolve()


def require_macos_host() -> None:
    if sys.platform != "darwin":
        raise Universal2Error("macOS thin recording and universal2 assembly require a macOS host")


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise Universal2Error(f"macOS universal2 assembly requires {name}")
    return path


def run_command(command: list[str], label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise Universal2Error(f"{label} failed: {message}")
    return completed


def lipo_arches(path: Path) -> frozenset[str]:
    completed = run_command([require_tool("lipo"), "-archs", str(path)], f"reading Mach-O slices for {path}")
    return frozenset(completed.stdout.strip().split())


def validate_exact_arches(path: Path, expected: frozenset[str]) -> None:
    actual = lipo_arches(path)
    if actual != expected:
        raise Universal2Error(
            f"Mach-O architecture mismatch for {path}: expected {sorted(expected)}, found {sorted(actual)}"
        )


def otool_output(path: Path, flag: str, *, macho_arch: str | None = None) -> str:
    command = [require_tool("otool"), flag]
    if macho_arch:
        command.extend(("-arch", macho_arch))
    command.append(str(path))
    return run_command(command, f"reading Mach-O metadata for {path}").stdout


def minimum_os_version(path: Path, *, macho_arch: str | None = None) -> str:
    output = otool_output(path, "-l", macho_arch=macho_arch)
    build_version_minos = ""
    version_min_macosx = ""
    current_command = ""
    for line in output.splitlines():
        fields = line.strip().split()
        if len(fields) < 2:
            continue
        if fields[0] == "cmd":
            current_command = fields[1]
        elif current_command == "LC_BUILD_VERSION" and fields[0] == "minos":
            build_version_minos = fields[1]
        elif current_command == "LC_VERSION_MIN_MACOSX" and fields[0] == "version":
            version_min_macosx = fields[1]
    result = build_version_minos or version_min_macosx
    if not result:
        raise Universal2Error(f"Mach-O binary has no minimum macOS version: {path}")
    return result


def normalized_version(version: str) -> tuple[int, ...]:
    if re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", version) is None:
        raise Universal2Error(f"invalid dotted-numeric macOS version: {version!r}")
    parts = [int(part) for part in version.split(".")]
    while parts and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def install_name(path: Path, *, macho_arch: str | None = None) -> str:
    lines = [line.strip() for line in otool_output(path, "-D", macho_arch=macho_arch).splitlines() if line.strip()]
    return lines[1] if len(lines) > 1 else ""


def dependencies(path: Path, *, macho_arch: str | None = None, own_install_name: str = "") -> list[str]:
    lines = [line.strip() for line in otool_output(path, "-L", macho_arch=macho_arch).splitlines()[1:] if line.strip()]
    result = []
    for line in lines:
        dependency = line.split(" (compatibility version", 1)[0].strip()
        if dependency and dependency != own_install_name:
            result.append(dependency)
    return sorted(result)


def require_module_entry_export(path: Path, *, macho_arch: str, code_key: str) -> None:
    """Check the one entry point the loader resolves for this module kind.

    game-sp/game-mp export GetGameAPI; the renderer module exports GetRenderAPI
    and nothing else (tools/build/macos_renderer_module.exp). Checking the wrong
    symbol would silently accept a module the engine can never bind.
    """

    symbol = MODULE_ENTRY_POINT_EXPORTS[module_stem(code_key)]
    completed = run_command(
        [require_tool("nm"), "-arch", macho_arch, "-gU", str(path)],
        f"reading exported symbols for {path} ({macho_arch})",
    )
    if re.search(rf"(?:^|\s)_?{re.escape(symbol)}(?:$|\s)", completed.stdout, re.MULTILINE) is None:
        raise Universal2Error(f"macOS {code_key} module does not export {symbol}: {path}")


def load_source_manifest(path: Path) -> dict[str, object]:
    manifest_path = require_regular_file(path, "GameLibs staging provenance manifest")
    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise Universal2Error(f"GameLibs staging provenance manifest is invalid: {manifest_path}") from exc
    if not isinstance(value, dict) or value.get("format") != 1:
        raise Universal2Error("GameLibs staging provenance manifest has an unsupported format")
    files = value.get("files")
    if not isinstance(files, list) or value.get("fileCount") != len(files):
        raise Universal2Error("GameLibs staging provenance manifest has an inconsistent file list")
    seen: set[str] = set()
    normalized_files: list[dict[str, str]] = []
    for entry in files:
        if not isinstance(entry, dict):
            raise Universal2Error("GameLibs staging provenance manifest contains a malformed file entry")
        relative = entry.get("path")
        digest = entry.get("sha256")
        if (
            not isinstance(relative, str)
            or not relative
            or relative.startswith("/")
            or "\\" in relative
            or any(part in ("", ".", "..") for part in relative.split("/"))
            or relative in seen
            or not isinstance(digest, str)
            or re.fullmatch(r"[0-9a-f]{64}", digest) is None
        ):
            raise Universal2Error(f"GameLibs staging provenance manifest contains an unsafe entry: {relative!r}")
        seen.add(relative)
        normalized_files.append({"path": relative, "sha256": digest})
    for field in SOURCE_PROVENANCE_FIELDS:
        if field not in value:
            raise Universal2Error(f"GameLibs staging provenance manifest is missing {field}")
    for field in ("projectGitCommit", "gameLibsGitCommit"):
        if not isinstance(value[field], str) or re.fullmatch(r"[0-9a-fA-F]{40,64}", value[field]) is None:
            raise Universal2Error(f"GameLibs staging provenance manifest has invalid {field}")
    for field in ("projectGitDirty", "gameLibsGitDirty"):
        if value[field] is not False:
            raise Universal2Error(f"universal2 inputs must come from clean source trees ({field} is not false)")
    return {
        **{field: value[field] for field in SOURCE_PROVENANCE_FIELDS},
        "stagedSourceSha256": canonical_json_sha256(normalized_files),
    }


def classify_staged_tree(root: Path, arch: str) -> tuple[dict[str, dict[str, object]], dict[str, dict[str, object]]]:
    code_paths = staged_code_paths(root, arch)
    reverse_code_paths = {path.as_posix(): key for key, path in code_paths.items()}
    expected_code_names = set(reverse_code_paths)
    code_records: dict[str, dict[str, object]] = {}
    shared_records: dict[str, dict[str, object]] = {}

    for required in REQUIRED_SHARED_PATHS:
        require_regular_file(root / required, f"required shared macOS payload {required}")

    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise Universal2Error(f"thin macOS payload contains a symlink: {relative}")
        mode = path.stat().st_mode
        if stat.S_ISDIR(mode):
            continue
        if not stat.S_ISREG(mode):
            raise Universal2Error(f"thin macOS payload contains a special file: {relative}")
        if relative == THIN_MANIFEST_NAME:
            continue
        # A per-arch code file that is not the one this tree is supposed to hold
        # would otherwise be classified as shared payload and copied verbatim
        # into the fused tree, shipping a wrong-architecture binary. The
        # renderer arm covers gl as well as vk so a future renderer-gl build on
        # darwin cannot slip through unnoticed.
        if (
            (path.parent == root and re.fullmatch(r"openQ4-(?:client|ded)_[A-Za-z0-9]+", path.name))
            or (path.parent == root / "baseoq4" and re.fullmatch(r"game-(?:sp|mp)_[A-Za-z0-9]+\.dylib", path.name))
            or (
                path.parent.parent == root
                and path.parent.name in (*PACKAGED_LAYER_GAME_DIRS, *RETIRED_LAYER_GAME_DIRS)
                and re.fullmatch(r"game-(?:sp|mp)_[A-Za-z0-9]+\.dylib", path.name)
            )
            or (path.parent == root and re.fullmatch(r"renderer-(?:gl|vk)_[A-Za-z0-9]+\.dylib", path.name))
        ) and relative not in expected_code_names:
            raise Universal2Error(f"thin macOS payload contains a stale or mismatched code file: {relative}")
        record = {
            "path": relative,
            "sha256": file_sha256(path),
            "size": path.stat().st_size,
            "mode": stat.S_IMODE(mode),
        }
        code_key = reverse_code_paths.get(relative)
        if code_key:
            code_records[code_key] = record
        else:
            shared_records[relative] = record

    missing_code = sorted(set(code_paths) - set(code_records))
    if missing_code:
        raise Universal2Error(f"thin macOS payload is missing required code files: {', '.join(missing_code)}")
    for code_key, relative in code_paths.items():
        require_regular_file(root / relative, f"thin macOS {code_key} binary", executable=True)
    return code_records, shared_records


def collect_binary_metadata(root: Path, arch: str, deployment_target: str) -> dict[str, dict[str, object]]:
    expected_arches = THIN_MACHO_ARCHES[arch]
    macho_arch = next(iter(expected_arches))
    result: dict[str, dict[str, object]] = {}
    for code_key, relative in staged_code_paths(root, arch).items():
        path = root / relative
        validate_exact_arches(path, expected_arches)
        minimum_os = minimum_os_version(path, macho_arch=macho_arch)
        if normalized_version(minimum_os) != normalized_version(deployment_target):
            raise Universal2Error(
                f"minimum macOS version mismatch for {path}: expected {deployment_target}, found {minimum_os}"
            )
        expected_id = expected_install_name(code_key, arch)
        actual_id = install_name(path, macho_arch=macho_arch) if expected_id else ""
        if expected_id and actual_id != expected_id:
            raise Universal2Error(
                f"install name mismatch for {path}: expected {expected_id!r}, found {actual_id!r}"
            )
        if module_stem(code_key) in MODULE_ENTRY_POINT_EXPORTS:
            require_module_entry_export(path, macho_arch=macho_arch, code_key=code_key)
        result[code_key] = {
            "path": relative.as_posix(),
            "sha256": file_sha256(path),
            "size": path.stat().st_size,
            "mode": stat.S_IMODE(path.stat().st_mode),
            "machoArchitectures": sorted(expected_arches),
            "minimumOS": minimum_os,
            "installName": actual_id,
            "dependencies": dependencies(path, macho_arch=macho_arch, own_install_name=actual_id),
        }
    return result


def write_json_atomic(path: Path, value: object, *, replace: bool) -> None:
    require_no_symlink_components(path.parent, "manifest output parent")
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not replace):
        raise Universal2Error(f"manifest output already exists (use --replace to overwrite): {path}")
    temp_path = path.with_name(path.name + ".tmp")
    if temp_path.exists() or temp_path.is_symlink():
        raise Universal2Error(f"temporary manifest output already exists: {temp_path}")
    try:
        temp_path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        os.replace(temp_path, path)
    finally:
        if temp_path.exists():
            temp_path.unlink()


def record_thin(args: argparse.Namespace) -> None:
    require_macos_host()
    root = require_directory(Path(args.install_root), "thin macOS install root")
    code_records, shared_records = classify_staged_tree(root, args.arch)
    source = load_source_manifest(Path(args.source_manifest))
    binary_records = collect_binary_metadata(root, args.arch, args.deployment_target)
    if set(binary_records) != set(code_records):
        raise Universal2Error("thin code files changed while they were being recorded")
    for key in code_records:
        if binary_records[key]["sha256"] != code_records[key]["sha256"]:
            raise Universal2Error(f"thin binary changed while it was being recorded: {code_records[key]['path']}")
    manifest = {
        "format": THIN_MANIFEST_FORMAT,
        "architecture": args.arch,
        "machoArchitectures": sorted(THIN_MACHO_ARCHES[args.arch]),
        "graphicsBridge": args.graphics_bridge,
        "openALProvider": args.openal_provider,
        "deploymentTarget": args.deployment_target,
        "buildType": args.build_type,
        **source,
        "sharedPayloadSha256": canonical_json_sha256(list(shared_records.values())),
        "sharedFileCount": len(shared_records),
        "binaries": binary_records,
    }
    write_json_atomic(root / THIN_MANIFEST_NAME, manifest, replace=args.replace)


def load_thin_manifest(root: Path, expected_arch: str) -> dict[str, object]:
    path = require_regular_file(root / THIN_MANIFEST_NAME, f"{expected_arch} thin build manifest")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise Universal2Error(f"thin build manifest is invalid: {path}") from exc
    if not isinstance(value, dict) or value.get("format") != THIN_MANIFEST_FORMAT:
        raise Universal2Error(f"thin build manifest has an unsupported format: {path}")
    if value.get("architecture") != expected_arch or value.get("machoArchitectures") != sorted(THIN_MACHO_ARCHES[expected_arch]):
        raise Universal2Error(f"thin build manifest architecture mismatch: {path}")
    validate_thin_manifest_metadata(value, expected_arch)
    for field in COMMON_BUILD_FIELDS:
        if field not in value:
            raise Universal2Error(f"thin build manifest is missing {field}: {path}")
    return value


def validate_thin_manifest_metadata(manifest: dict[str, object], expected_arch: str) -> None:
    """Reject malformed thin-build metadata before it influences a merge.

    Artifact manifests cross a runner boundary. Their binary records are later
    compared against fresh Mach-O inspection, but validate their shape here as
    well so a corrupted manifest cannot turn type coercion or omitted fields
    into a weaker check.
    """

    if manifest.get("architecture") != expected_arch:
        raise Universal2Error("thin build manifest architecture metadata is invalid")
    if manifest.get("machoArchitectures") != sorted(THIN_MACHO_ARCHES[expected_arch]):
        raise Universal2Error("thin build manifest Mach-O architecture metadata is invalid")

    for field in ("projectGitCommit", "gameLibsGitCommit"):
        value = manifest.get(field)
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-fA-F]{40,64}", value) is None:
            raise Universal2Error(f"thin build manifest has invalid {field}")
    for field in ("projectGitDirty", "gameLibsGitDirty"):
        if manifest.get(field) is not False:
            raise Universal2Error(f"thin build manifest must record a clean source tree ({field})")
    for field in ("stagedSourceSha256", "sharedPayloadSha256"):
        value = manifest.get(field)
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise Universal2Error(f"thin build manifest has invalid {field}")
    if manifest.get("graphicsBridge") not in GRAPHICS_BRIDGES:
        raise Universal2Error("thin build manifest has invalid graphicsBridge")
    if manifest.get("openALProvider") not in OPENAL_PROVIDERS:
        raise Universal2Error("thin build manifest has invalid openALProvider")
    deployment_target = manifest.get("deploymentTarget")
    if not isinstance(deployment_target, str):
        raise Universal2Error("thin build manifest has invalid deploymentTarget")
    normalized_version(deployment_target)
    if manifest.get("buildType") not in BUILD_TYPES:
        raise Universal2Error("thin build manifest has invalid buildType")
    shared_file_count = manifest.get("sharedFileCount")
    if isinstance(shared_file_count, bool) or not isinstance(shared_file_count, int) or shared_file_count < 0:
        raise Universal2Error("thin build manifest has invalid sharedFileCount")

    binaries = manifest.get("binaries")
    if not isinstance(binaries, dict) or not set(CODE_KEYS) <= set(binaries):
        raise Universal2Error("thin build manifest has an invalid binary set")
    optional = optional_thin_code_paths(expected_arch)
    layer_keys = set(binaries) - set(CODE_KEYS)
    if not layer_keys <= set(optional):
        raise Universal2Error("thin build manifest has an invalid binary set")
    for layer in PACKAGED_LAYER_GAME_DIRS:
        keys = set(layer_code_keys(layer))
        if layer_keys & keys and not keys <= layer_keys:
            raise Universal2Error(f"thin build manifest records an incomplete {layer}/ layer")
    recorded_paths = {**thin_code_paths(expected_arch), **{key: optional[key] for key in sorted(layer_keys)}}
    for key, relative in recorded_paths.items():
        record = binaries.get(key)
        if not isinstance(record, dict):
            raise Universal2Error(f"thin build manifest has invalid {key} binary metadata")
        if record.get("path") != relative.as_posix():
            raise Universal2Error(f"thin build manifest has an invalid {key} path")
        digest = record.get("sha256")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise Universal2Error(f"thin build manifest has an invalid {key} sha256")
        size = record.get("size")
        mode = record.get("mode")
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise Universal2Error(f"thin build manifest has an invalid {key} size")
        if isinstance(mode, bool) or not isinstance(mode, int) or mode < 0 or mode > 0o7777:
            raise Universal2Error(f"thin build manifest has an invalid {key} mode")
        if record.get("machoArchitectures") != sorted(THIN_MACHO_ARCHES[expected_arch]):
            raise Universal2Error(f"thin build manifest has invalid {key} Mach-O architecture metadata")
        minimum_os = record.get("minimumOS")
        if not isinstance(minimum_os, str):
            raise Universal2Error(f"thin build manifest has invalid {key} minimumOS")
        normalized_version(minimum_os)
        if record.get("installName") != expected_install_name(key, expected_arch):
            raise Universal2Error(f"thin build manifest has invalid {key} installName")
        dependencies_value = record.get("dependencies")
        if (
            not isinstance(dependencies_value, list)
            or any(not isinstance(dependency, str) or not dependency for dependency in dependencies_value)
            or dependencies_value != sorted(dependencies_value)
        ):
            raise Universal2Error(f"thin build manifest has invalid {key} dependencies")


def validate_recorded_tree(root: Path, arch: str, manifest: dict[str, object]) -> dict[str, dict[str, object]]:
    validate_thin_manifest_metadata(manifest, arch)
    code_records, shared_records = classify_staged_tree(root, arch)
    if manifest.get("sharedFileCount") != len(shared_records):
        raise Universal2Error(f"thin shared payload file count changed after recording: {root}")
    if manifest.get("sharedPayloadSha256") != canonical_json_sha256(list(shared_records.values())):
        raise Universal2Error(f"thin shared payload changed after recording: {root}")
    binary_manifest = manifest["binaries"]
    assert isinstance(binary_manifest, dict)
    if set(code_records) != set(binary_manifest):
        raise Universal2Error(f"thin code files changed after recording: {root}")
    for key, record in code_records.items():
        saved = binary_manifest.get(key)
        if not isinstance(saved, dict) or any(saved.get(field) != record[field] for field in ("path", "sha256", "size", "mode")):
            raise Universal2Error(f"thin binary changed after recording: {record['path']}")
        validate_exact_arches(root / Path(str(record["path"])), THIN_MACHO_ARCHES[arch])

    # Re-inspect the downloaded Mach-O files instead of trusting metadata that
    # was recorded on a different runner. This enforces the same deployment
    # target, IDs, dependency set, and game API export that record_thin()
    # required before any lipo operation is attempted.
    deployment_target = manifest["deploymentTarget"]
    assert isinstance(deployment_target, str)
    inspected_binaries = collect_binary_metadata(root, arch, deployment_target)
    if inspected_binaries != binary_manifest:
        raise Universal2Error(f"thin binary Mach-O metadata changed after recording: {root}")
    return shared_records


def validate_matching_inputs(
    arm_manifest: dict[str, object],
    x64_manifest: dict[str, object],
    arm_shared: dict[str, dict[str, object]],
    x64_shared: dict[str, dict[str, object]],
) -> None:
    mismatched_fields = [field for field in COMMON_BUILD_FIELDS if arm_manifest.get(field) != x64_manifest.get(field)]
    if mismatched_fields:
        raise Universal2Error(f"thin macOS provenance/build settings differ: {', '.join(mismatched_fields)}")
    arm_binaries = arm_manifest["binaries"]
    x64_binaries = x64_manifest["binaries"]
    assert isinstance(arm_binaries, dict) and isinstance(x64_binaries, dict)
    arm_layer_keys = staged_layer_keys(arm_binaries)
    x64_layer_keys = staged_layer_keys(x64_binaries)
    if arm_layer_keys != x64_layer_keys:
        raise Universal2Error(
            "game-layer modules must be staged in both thin slices or in neither: "
            f"arm64={list(arm_layer_keys)} x64={list(x64_layer_keys)}"
        )
    if arm_shared != x64_shared:
        arm_paths = set(arm_shared)
        x64_paths = set(x64_shared)
        details = []
        if arm_paths - x64_paths:
            details.append(f"missing from x64: {sorted(arm_paths - x64_paths)[:5]}")
        if x64_paths - arm_paths:
            details.append(f"missing from arm64: {sorted(x64_paths - arm_paths)[:5]}")
        changed = sorted(path for path in arm_paths & x64_paths if arm_shared[path] != x64_shared[path])
        if changed:
            details.append(f"different content/mode: {changed[:5]}")
        raise Universal2Error("thin shared payloads are not identical; " + "; ".join(details))
    for key in (*CODE_KEYS, *arm_layer_keys):
        arm_record = arm_binaries[key]
        x64_record = x64_binaries[key]
        assert isinstance(arm_record, dict) and isinstance(x64_record, dict)
        if arm_record.get("dependencies") != x64_record.get("dependencies"):
            raise Universal2Error(f"thin {key} dependency sets differ between arm64 and x64")


def signature_normalized_shared_records(
    root: Path,
    shared: dict[str, dict[str, object]],
) -> dict[str, dict[str, object]]:
    """Return matching records with only MoltenVK's host signature removed.

    Both recorded files remain immutable and retain their independently valid
    signatures. The temporary copy makes a strict underlying-byte comparison
    possible without accepting two different MoltenVK builds merely because
    both have valid ad-hoc signatures.
    """

    normalized = {relative: dict(record) for relative, record in shared.items()}
    if MOLTENVK_DYLIB_NAME not in normalized:
        raise Universal2Error(f"thin macOS payload is missing required {MOLTENVK_DYLIB_NAME}")

    source = require_regular_file(root / MOLTENVK_DYLIB_NAME, "host-signed MoltenVK shared payload")
    validate_exact_arches(source, UNIVERSAL_MACHO_ARCHES)
    codesign = require_tool("codesign")
    run_command(
        [codesign, "--verify", "--strict", "--all-architectures", str(source)],
        f"verifying all MoltenVK signature slices for {source}",
    )
    with tempfile.TemporaryDirectory(prefix="openq4-moltenvk-signature-") as temporary:
        unsigned = Path(temporary) / MOLTENVK_DYLIB_NAME
        shutil.copy2(source, unsigned)
        run_command(
            [codesign, "--remove-signature", str(unsigned)],
            f"normalizing host-dependent MoltenVK signature for {source}",
        )
        normalized[MOLTENVK_DYLIB_NAME] = {
            "path": MOLTENVK_DYLIB_NAME,
            "sha256": file_sha256(unsigned),
            "size": unsigned.stat().st_size,
            "mode": stat.S_IMODE(unsigned.stat().st_mode),
        }
    return normalized


def merge_binary(arm_path: Path, x64_path: Path, output_path: Path, code_key: str) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    run_command(
        [require_tool("lipo"), "-create", str(arm_path), str(x64_path), "-output", str(output_path)],
        f"merging universal2 {code_key}",
    )
    output_path.chmod(stat.S_IMODE(arm_path.stat().st_mode) | 0o111)
    expected_id = expected_install_name(code_key, UNIVERSAL_ARCH)
    if expected_id:
        run_command(
            [require_tool("install_name_tool"), "-id", expected_id, str(output_path)],
            f"normalizing universal2 {code_key} install name",
        )
    codesign = require_tool("codesign")
    run_command(
        [codesign, "--force", "--sign", "-", str(output_path)],
        f"ad-hoc signing universal2 {code_key}",
    )
    run_command(
        [codesign, "--verify", "--strict", "--all-architectures", str(output_path)],
        f"verifying universal2 {code_key} signature",
    )
    validate_exact_arches(output_path, UNIVERSAL_MACHO_ARCHES)
    if expected_id:
        for macho_arch in sorted(UNIVERSAL_MACHO_ARCHES):
            actual_id = install_name(output_path, macho_arch=macho_arch)
            if actual_id != expected_id:
                raise Universal2Error(
                    f"universal2 {code_key} install name mismatch for {macho_arch}: {actual_id!r}"
                )
        if module_stem(code_key) in MODULE_ENTRY_POINT_EXPORTS:
            for macho_arch in sorted(UNIVERSAL_MACHO_ARCHES):
                require_module_entry_export(output_path, macho_arch=macho_arch, code_key=code_key)


def copy_shared_payload(source_root: Path, output_root: Path, shared: dict[str, dict[str, object]]) -> None:
    for relative, record in shared.items():
        source = source_root / relative
        destination = output_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        if file_sha256(destination) != record["sha256"] or stat.S_IMODE(destination.stat().st_mode) != record["mode"]:
            raise Universal2Error(f"shared payload copy verification failed: {relative}")


def validate_universal_output(
    output_root: Path,
    arm_manifest: dict[str, object],
    x64_manifest: dict[str, object],
) -> dict[str, dict[str, object]]:
    output_records: dict[str, dict[str, object]] = {}
    arm_binaries = arm_manifest["binaries"]
    x64_binaries = x64_manifest["binaries"]
    assert isinstance(arm_binaries, dict) and isinstance(x64_binaries, dict)
    optional = optional_universal_code_paths()
    output_paths = {**universal_code_paths(), **{key: optional[key] for key in staged_layer_keys(arm_binaries)}}
    for key, relative in output_paths.items():
        path = require_regular_file(output_root / relative, f"universal2 {key}", executable=True)
        validate_exact_arches(path, UNIVERSAL_MACHO_ARCHES)
        expected_id = expected_install_name(key, UNIVERSAL_ARCH)
        per_slice = {}
        for macho_arch in sorted(UNIVERSAL_MACHO_ARCHES):
            minimum_os = minimum_os_version(path, macho_arch=macho_arch)
            source_record = arm_binaries[key] if macho_arch == "arm64" else x64_binaries[key]
            assert isinstance(source_record, dict)
            if normalized_version(minimum_os) != normalized_version(str(source_record["minimumOS"])):
                raise Universal2Error(f"universal2 {key} minimum-OS metadata changed for {macho_arch}")
            actual_id = install_name(path, macho_arch=macho_arch) if expected_id else ""
            if expected_id and actual_id != expected_id:
                raise Universal2Error(f"universal2 {key} install name changed for {macho_arch}")
            actual_dependencies = dependencies(path, macho_arch=macho_arch, own_install_name=actual_id)
            if actual_dependencies != source_record["dependencies"]:
                raise Universal2Error(f"universal2 {key} dependencies changed for {macho_arch}")
            per_slice[macho_arch] = {
                "minimumOS": minimum_os,
                "installName": actual_id,
                "dependencies": actual_dependencies,
            }
        output_records[key] = {
            "path": relative.as_posix(),
            "sha256": file_sha256(path),
            "size": path.stat().st_size,
            "mode": stat.S_IMODE(path.stat().st_mode),
            "machoArchitectures": sorted(UNIVERSAL_MACHO_ARCHES),
            "slices": per_slice,
        }
    return output_records


def paths_overlap(first: Path, second: Path) -> bool:
    try:
        first.relative_to(second)
        return True
    except ValueError:
        pass
    try:
        second.relative_to(first)
        return True
    except ValueError:
        return False


def assemble(args: argparse.Namespace) -> None:
    require_macos_host()
    arm_root = require_directory(Path(args.arm64_root), "arm64 thin install root")
    x64_root = require_directory(Path(args.x64_root), "x64 thin install root")
    raw_output_root = Path(args.output_root)
    if raw_output_root.is_symlink():
        raise Universal2Error(f"universal2 output root must not be a symlink: {raw_output_root}")
    output_parent = require_directory(raw_output_root.parent, "universal2 output parent")
    output_root = raw_output_root.resolve(strict=False)
    raw_manifest_path = Path(args.assembly_manifest)
    if raw_manifest_path.is_symlink():
        raise Universal2Error(f"assembly manifest must not be a symlink: {raw_manifest_path}")
    manifest_path = raw_manifest_path.resolve(strict=False)
    if arm_root == x64_root or paths_overlap(arm_root, x64_root):
        raise Universal2Error("arm64 and x64 thin roots must be distinct, non-overlapping directories")
    if paths_overlap(output_root, arm_root) or paths_overlap(output_root, x64_root):
        raise Universal2Error("universal2 output root must not overlap either thin input root")
    if output_root.exists() or output_root.is_symlink():
        raise Universal2Error(f"universal2 output root must not already exist: {output_root}")
    if paths_overlap(manifest_path, output_root) or paths_overlap(manifest_path, arm_root) or paths_overlap(manifest_path, x64_root):
        raise Universal2Error("assembly manifest must be outside the input and output staging trees")
    require_directory(manifest_path.parent, "assembly manifest parent")
    if manifest_path.is_symlink() or (manifest_path.exists() and not args.replace_manifest):
        raise Universal2Error(
            f"assembly manifest already exists (use --replace-manifest to overwrite): {manifest_path}"
        )

    arm_manifest = load_thin_manifest(arm_root, "arm64")
    x64_manifest = load_thin_manifest(x64_root, "x64")
    arm_shared = validate_recorded_tree(arm_root, "arm64", arm_manifest)
    x64_shared = validate_recorded_tree(x64_root, "x64", x64_manifest)
    arm_shared_for_matching = signature_normalized_shared_records(arm_root, arm_shared)
    x64_shared_for_matching = signature_normalized_shared_records(x64_root, x64_shared)
    validate_matching_inputs(
        arm_manifest,
        x64_manifest,
        arm_shared_for_matching,
        x64_shared_for_matching,
    )

    temporary_root = Path(tempfile.mkdtemp(prefix=f".{output_root.name}-", dir=output_parent))
    try:
        copy_shared_payload(arm_root, temporary_root, arm_shared)
        arm_paths = staged_code_paths(arm_root, "arm64")
        x64_paths = staged_code_paths(x64_root, "x64")
        optional = optional_universal_code_paths()
        output_paths = {**universal_code_paths(), **{key: optional[key] for key in staged_layer_keys(arm_paths)}}
        for key, output_relative in output_paths.items():
            merge_binary(arm_root / arm_paths[key], x64_root / x64_paths[key], temporary_root / output_relative, key)
        output_records = validate_universal_output(temporary_root, arm_manifest, x64_manifest)
        os.replace(temporary_root, output_root)
    finally:
        if temporary_root.exists():
            shutil.rmtree(temporary_root)

    assembly_manifest = {
        "format": ASSEMBLY_MANIFEST_FORMAT,
        "architecture": UNIVERSAL_ARCH,
        "machoArchitectures": sorted(UNIVERSAL_MACHO_ARCHES),
        **{field: arm_manifest[field] for field in COMMON_BUILD_FIELDS},
        "sharedPayloadSha256": arm_manifest["sharedPayloadSha256"],
        "signatureNormalizedSharedPayloadSha256": canonical_json_sha256(
            list(arm_shared_for_matching.values())
        ),
        "sharedFileCount": arm_manifest["sharedFileCount"],
        "thinManifestSha256": {
            "arm64": file_sha256(arm_root / THIN_MANIFEST_NAME),
            "x64": file_sha256(x64_root / THIN_MANIFEST_NAME),
        },
        "binaries": output_records,
    }
    write_json_atomic(manifest_path, assembly_manifest, replace=args.replace_manifest)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    record_parser = subparsers.add_parser("record", help="record and validate one thin macOS staging tree")
    record_parser.add_argument("--install-root", required=True)
    record_parser.add_argument("--source-manifest", required=True)
    record_parser.add_argument("--arch", required=True, choices=sorted(THIN_MACHO_ARCHES))
    record_parser.add_argument("--graphics-bridge", required=True, choices=GRAPHICS_BRIDGES)
    record_parser.add_argument("--openal-provider", required=True, choices=OPENAL_PROVIDERS)
    record_parser.add_argument("--deployment-target", default="11.0")
    record_parser.add_argument("--build-type", default="debug", choices=BUILD_TYPES)
    record_parser.add_argument("--replace", action="store_true")

    prepare_parser = subparsers.add_parser(
        "prepare",
        help="normalize and ad-hoc sign one staged thin macOS payload before recording",
    )
    prepare_parser.add_argument("--install-root", required=True)
    prepare_parser.add_argument("--arch", required=True, choices=sorted(THIN_MACHO_ARCHES))

    assemble_parser = subparsers.add_parser("assemble", help="merge validated arm64 and x64 staging trees")
    assemble_parser.add_argument("--arm64-root", required=True)
    assemble_parser.add_argument("--x64-root", required=True)
    assemble_parser.add_argument("--output-root", required=True)
    assemble_parser.add_argument("--assembly-manifest", required=True)
    assemble_parser.add_argument("--replace-manifest", action="store_true")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        if args.command == "record":
            record_thin(args)
        elif args.command == "prepare":
            prepare_thin_payload(Path(args.install_root), args.arch)
        else:
            assemble(args)
    except (OSError, Universal2Error, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
