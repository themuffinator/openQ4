#!/usr/bin/env python3
"""Build the temporary stock-asset fixture used to qualify renderer Milestone D.

The fixture is deliberately not shipping content.  It retrieves the stock
``maps/tools/mv2.map`` shell and a stock ROQ from a user-owned extracted Quake 4
asset tree, adds original test-only mirror and authored-post geometry, compiles
the result with openQ4's in-tree dmap, and stages an isolated runtime below
``.tmp/stock-runtime`` for the gameplay benchmark harness.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SOURCE_MAP_REL = Path("maps/tools/mv2.map")
SOURCE_VIDEO_REL = Path("video/idlogo.roq")
SOURCE_SHA256 = {
    SOURCE_MAP_REL: "412fc796e569660c92dca5d55359e5c4eb3d572754bd1cdff8ce994b0b802fe2",
    SOURCE_VIDEO_REL: "375c9641c96359c2e0d49cdd3905cc2cdc66a7b067c9a8910d80bf36fb06c8f4",
}
STOCK_GAME_DIRECTORIES = ("q4base", "baseoq4")
FIXTURE_MAP_REL = Path("maps/tools/milestone_d_nested_dynamic.map")
FIXTURE_MATERIAL_REL = Path("materials/milestone_d_validation.mtr")
FIXTURE_VIDEO_REL = Path("video/milestone_d_idlogo.roq")

FIXTURE_MATERIAL = """// Temporary Milestone D validation material; never shipped.
milestoneD/nestedCinematicPost
{
    qer_editorimage textures/editor/video.tga
    translucent
    twoSided
    noShadows
    noSelfShadow
    sort postProcess
    {
        blend add
        videoMap loop video/milestone_d_idlogo.roq
        rgb 1
    }
    {
        blend blend
        map _currentRender
        alpha 0.35
    }
}
"""

# The player capture looks north at the mirror.  Its reflected camera looks
# south at the video panel behind the player, so the post-process/video tail is
# present only in the mirror child view.  All other faces are stock caulk.
FIXTURE_ENTITIES = r"""
// entity 7: capture-backed mirror facing the validation camera
{
"classname" "func_static"
"name" "milestone_d_capture_mirror"
"model" "milestone_d_capture_mirror"
// primitive 0
{
 brushDef3
 {
  ( -1 0 0 -160 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 1 0 0 -160 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 -1 0 0 ) ( ( 0.00390625 0 0.5 ) ( 0 -0.005208333333 0.5 ) ) "shaderDemos/transparentMirror"
  ( 0 1 0 -8 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 0 -1 16 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 0 1 -224 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
 }
}
}
// entity 8: post-process video panel visible only through the mirror child view
{
"classname" "func_static"
"name" "milestone_d_nested_post_panel"
"model" "milestone_d_nested_post_panel"
// primitive 0
{
 brushDef3
 {
  ( -1 0 0 -192 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 1 0 0 -192 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 -1 0 -648 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 1 0 640 ) ( ( 0.002604166667 0 0.5 ) ( 0 -0.005681818182 0.5 ) ) "milestoneD/nestedCinematicPost"
  ( 0 0 -1 32 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
  ( 0 0 1 -208 ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "textures/common/caulk"
 }
}
}
"""


def default_asset_root() -> str:
    return r"E:\_SOURCE\_ASSETS\Q4" if os.name == "nt" else ""


def default_basepath() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


def filesystem_path(path: Path) -> Path:
    """Use extended Windows paths so effective loose assets past MAX_PATH hash."""
    if os.name != "nt":
        return path
    absolute = os.path.abspath(path)
    if absolute.startswith("\\\\?\\"):
        return Path(absolute)
    if absolute.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + absolute[2:])
    return Path("\\\\?\\" + absolute)


def is_link_or_junction(path: Path) -> bool:
    native = filesystem_path(path)
    is_junction = getattr(native, "is_junction", None)
    if native.is_symlink() or bool(is_junction and is_junction()):
        return True
    if not native.exists():
        return False
    try:
        attributes = getattr(native.lstat(), "st_file_attributes", 0)
        return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except OSError:
        return False


def require_new_directory(path: Path, parent: Path, description: str) -> Path:
    root_absolute = ROOT.absolute()
    parent_absolute = parent.absolute()
    candidate = path.absolute() if path.is_absolute() else (root_absolute / path).absolute()
    try:
        relative = candidate.relative_to(parent_absolute)
    except ValueError as exc:
        raise ValueError(
            f"{description} must stay below {parent_absolute}: {candidate}"
        ) from exc
    if len(relative.parts) != 1:
        raise ValueError(
            f"{description} must be a named direct child below {parent_absolute}: "
            f"{candidate}"
        )
    try:
        root_relative = candidate.relative_to(root_absolute)
    except ValueError as exc:
        raise ValueError(f"{description} escaped source root {root_absolute}") from exc
    current = root_absolute
    if current.exists() and is_link_or_junction(current):
        raise ValueError(f"{description} may not traverse a link or junction: {current}")
    for part in root_relative.parts:
        current /= part
        if current.exists() and is_link_or_junction(current):
            raise ValueError(f"{description} may not traverse a link or junction: {current}")
    if candidate.exists():
        raise FileExistsError(f"{description} must be new: {candidate}")
    resolved_parent = parent_absolute.resolve()
    resolved = candidate.resolve()
    if resolved.parent != resolved_parent:
        raise ValueError(
            f"resolved {description} must remain below {resolved_parent}: {resolved}"
        )
    return resolved


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with filesystem_path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, root: Path) -> dict[str, Any]:
    return {
        "path": path.relative_to(root).as_posix(),
        "size": filesystem_path(path).stat().st_size,
        "sha256": sha256(path),
    }


def inventory_summary(records: list[dict[str, Any]]) -> dict[str, Any]:
    encoded = json.dumps(
        records,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
        allow_nan=False,
    ).encode("utf-8")
    return {
        "fileCount": len(records),
        "totalBytes": sum(int(record["size"]) for record in records),
        "manifestSha256": hashlib.sha256(encoded).hexdigest(),
        "files": records,
    }


def stock_dependency_inventory(basepath: Path) -> dict[str, Any]:
    """Hash every effective stock PK4/loose file below q4base/baseoq4."""
    basepath = basepath.resolve()
    records: list[dict[str, Any]] = []

    def raise_walk_error(error: OSError) -> None:
        raise error

    for game_directory_name in STOCK_GAME_DIRECTORIES:
        game_directory = basepath / game_directory_name
        if not game_directory.exists():
            continue
        if not game_directory.is_dir() or is_link_or_junction(game_directory):
            raise ValueError(
                f"stock dependency directory must be an ordinary directory: {game_directory}"
            )
        for directory, directory_names, file_names in os.walk(
            game_directory, followlinks=False, onerror=raise_walk_error
        ):
            directory_path = Path(directory)
            directory_names.sort(key=str.casefold)
            file_names.sort(key=str.casefold)
            for name in tuple(directory_names):
                child = directory_path / name
                if is_link_or_junction(child):
                    raise ValueError(
                        f"stock dependency tree contains a link or junction: {child}"
                    )
            for name in file_names:
                path = directory_path / name
                if is_link_or_junction(path):
                    raise ValueError(
                        f"stock dependency tree contains a linked file: {path}"
                    )
                if not filesystem_path(path).is_file():
                    raise ValueError(f"stock dependency entry is not a file: {path}")
                records.append(file_record(path, basepath))
    records.sort(key=lambda record: record["path"].casefold())
    if not records:
        raise FileNotFoundError(
            f"stock base path has no effective q4base/baseoq4 files: {basepath}"
        )
    return inventory_summary(records)


def find_dmap_executable(runtime_dir: Path) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    architecture = host_arch()
    candidates = (
        runtime_dir / f"openQ4-ded_{architecture}{suffix}",
        runtime_dir / f"openQ4-client_{architecture}{suffix}",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "temporary runtime has no host-matching openQ4 dedicated/client executable"
    )


def validate_runtime_components(runtime_dir: Path) -> list[dict[str, Any]]:
    architecture = host_arch()
    executable_suffix = ".exe" if os.name == "nt" else ""
    module_suffix = (
        ".dll" if os.name == "nt" else (".dylib" if sys.platform == "darwin" else ".so")
    )
    required = [
        runtime_dir / f"openQ4-client_{architecture}{executable_suffix}",
        runtime_dir / "baseoq4" / f"game_{architecture}{module_suffix}",
        runtime_dir / f"renderer-vk_{architecture}{module_suffix}",
    ]
    # macOS keeps OpenGL statically linked and intentionally does not build the
    # renderer-gl module; Windows and Linux acceptance require the module.
    if sys.platform != "darwin":
        required.append(runtime_dir / f"renderer-gl_{architecture}{module_suffix}")
    else:
        # Meson intentionally does not build or install MoltenVK. The fixture
        # stages the project's pinned, verified provider beside the loose
        # client so both SDL and the renderer module bind the same image.
        required.append(runtime_dir / "libMoltenVK.dylib")
    missing = [path for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError(
            "temporary runtime is incomplete for GL/Vulkan SP acceptance: "
            + ", ".join(str(path) for path in missing)
        )
    return [file_record(path, runtime_dir) for path in required]


def validation_map_payload(source_map: Path) -> str:
    payload = source_map.read_text(encoding="utf-8")
    for token in (
        "Version 3",
        '"classname" "worldspawn"',
        '"classname" "info_player_start"',
        '"classname" "func_cameraview"',
    ):
        if token not in payload:
            raise ValueError(f"stock fixture source is missing {token!r}: {source_map}")
    return payload.rstrip() + "\n" + FIXTURE_ENTITIES.lstrip()


def timeout_output(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def run_checked(
    command: list[str], cwd: Path, log_path: Path, timeout_seconds: int
) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            errors="replace",
            check=False,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        log_path.write_text(
            "COMMAND\n"
            + subprocess.list2cmdline(command)
            + f"\n\nTIMEOUT\nExceeded {timeout_seconds} seconds; direct child terminated."
            + "\n\nSTDOUT\n"
            + timeout_output(exc.stdout)
            + "\nSTDERR\n"
            + timeout_output(exc.stderr),
            encoding="utf-8",
        )
        raise TimeoutError(
            f"command exceeded {timeout_seconds} seconds; see {log_path}"
        ) from exc
    log_path.write_text(
        "COMMAND\n" + subprocess.list2cmdline(command) + "\n\nSTDOUT\n"
        + completed.stdout + "\nSTDERR\n" + completed.stderr,
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed with exit code {completed.returncode}; see {log_path}"
        )


def stage_temporary_runtime(
    runtime_dir: Path, output_dir: Path, timeout_seconds: int
) -> None:
    run_checked(
        [
            sys.executable,
            "-B",
            str(ROOT / "tools/build/stage_fast_install.py"),
            "--source-root",
            str(ROOT),
            "--build-dir",
            str(ROOT / "builddir"),
            "--install-dir",
            str(runtime_dir),
            "--temporary-runtime",
        ],
        ROOT,
        output_dir / "stage-runtime.log",
        timeout_seconds,
    )
    if sys.platform == "darwin":
        run_checked(
            [
                "/bin/bash",
                str(ROOT / "tools/build/prepare_macos_moltenvk.sh"),
                "--output-dir",
                str(runtime_dir),
            ],
            ROOT,
            output_dir / "stage-moltenvk.log",
            timeout_seconds,
        )


def compile_fixture(
    runtime_dir: Path,
    compile_root: Path,
    basepath: Path | None,
    output_dir: Path,
    timeout_seconds: int,
) -> None:
    executable = find_dmap_executable(runtime_dir)
    command = [
        str(executable),
        "+set", "r_fullscreen", "0",
        "+set", "r_borderless", "0",
        "+set", "in_mouse", "0",
        "+set", "developer", "1",
        "+set", "logFile", "2",
        "+set", "logFileName", "logs/milestone_d_dmap.log",
        "+set", "fs_savepath", str(compile_root),
        "+set", "fs_devpath", str(compile_root),
        "+set", "fs_game", "baseoq4",
    ]
    if basepath is not None:
        command += ["+set", "fs_basepath", str(basepath)]
    command += ["+dmap", FIXTURE_MAP_REL.with_suffix("").as_posix(), "+quit"]
    # Source lookup and collision output use the isolated compile/save tree.
    # The engine deliberately locks fs_cdpath to its executable content root,
    # so dmap's .proc output first lands in this newly created temporary runtime
    # (never .install).  Copy it back to the compile tree before the allowlisted
    # fixture payload is verified and staged as one unit.
    run_checked(
        command, compile_root, output_dir / "dmap-process.log", timeout_seconds
    )

    runtime_proc = runtime_dir / "baseoq4" / FIXTURE_MAP_REL.with_suffix(".proc")
    compile_proc = compile_root / "baseoq4" / FIXTURE_MAP_REL.with_suffix(".proc")
    if not runtime_proc.is_file() or runtime_proc.stat().st_size == 0:
        raise FileNotFoundError(f"dmap did not produce {runtime_proc}")
    compile_proc.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(runtime_proc, compile_proc)

    engine_log = compile_root / "baseoq4/logs/milestone_d_dmap.log"
    if not engine_log.is_file():
        raise FileNotFoundError(f"dmap engine log was not written: {engine_log}")
    log_text = engine_log.read_text(encoding="utf-8", errors="replace")
    for marker in ("ERROR:", "Fatal Error", "leaked"):
        if marker.casefold() in log_text.casefold():
            raise RuntimeError(f"dmap log contains {marker!r}: {engine_log}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--asset-root",
        default=default_asset_root(),
        help="Extracted stock Quake 4 asset root containing maps/ and video/.",
    )
    parser.add_argument(
        "--basepath",
        default=default_basepath(),
        help="Installed Quake 4 root used to resolve stock PK4 dependencies.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Fresh evidence directory below .tmp/renderer-milestone-d/.",
    )
    parser.add_argument(
        "--runtime-dir",
        default="",
        help="Fresh temporary runtime below .tmp/stock-runtime/.",
    )
    parser.add_argument(
        "--stage-timeout",
        type=int,
        default=600,
        help="Maximum seconds allowed for temporary runtime staging.",
    )
    parser.add_argument(
        "--dmap-timeout",
        type=int,
        default=180,
        help="Maximum seconds allowed for the windowed dmap invocation.",
    )
    args = parser.parse_args(argv)
    if args.stage_timeout < 1:
        parser.error("--stage-timeout must be positive")
    if args.dmap_timeout < 1:
        parser.error("--dmap-timeout must be positive")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    output_parent = ROOT / ".tmp/renderer-milestone-d"
    runtime_parent = ROOT / ".tmp/stock-runtime"
    output_dir = require_new_directory(
        Path(args.output_dir) if args.output_dir else output_parent / stamp,
        output_parent,
        "fixture output directory",
    )
    runtime_dir = require_new_directory(
        Path(args.runtime_dir) if args.runtime_dir else runtime_parent / f"milestone-d-{stamp}",
        runtime_parent,
        "temporary runtime directory",
    )
    asset_root = Path(args.asset_root).resolve() if args.asset_root else None
    if asset_root is None or not asset_root.is_dir():
        raise FileNotFoundError("--asset-root must identify an extracted Quake 4 asset tree")
    basepath = Path(args.basepath).resolve() if args.basepath else None
    if basepath is None or not basepath.is_dir():
        raise FileNotFoundError("--basepath must identify an installed Quake 4 root")
    stock_dependencies_before = stock_dependency_inventory(basepath)

    source_map = asset_root / SOURCE_MAP_REL
    source_video = asset_root / SOURCE_VIDEO_REL
    for source in (source_map, source_video):
        if not source.is_file():
            raise FileNotFoundError(f"required stock fixture source is missing: {source}")
    for relative, expected_hash in SOURCE_SHA256.items():
        source = asset_root / relative
        actual_hash = sha256(source)
        if actual_hash != expected_hash:
            raise ValueError(
                f"stock fixture source hash mismatch for {source}: "
                f"expected {expected_hash}, got {actual_hash}"
            )

    output_dir.mkdir(parents=True)
    retrieved_root = output_dir / "retrieved/baseoq4"
    compile_root = output_dir / "compile"
    for source, relative in (
        (source_map, SOURCE_MAP_REL),
        (source_video, SOURCE_VIDEO_REL),
    ):
        target = retrieved_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied_hash = sha256(target)
        if copied_hash != SOURCE_SHA256[relative]:
            raise ValueError(
                f"retrieved stock snapshot hash mismatch for {target}: "
                f"expected {SOURCE_SHA256[relative]}, got {copied_hash}"
            )

    compile_game = compile_root / "baseoq4"
    map_path = compile_game / FIXTURE_MAP_REL
    material_path = compile_game / FIXTURE_MATERIAL_REL
    video_path = compile_game / FIXTURE_VIDEO_REL
    map_path.parent.mkdir(parents=True, exist_ok=True)
    material_path.parent.mkdir(parents=True, exist_ok=True)
    video_path.parent.mkdir(parents=True, exist_ok=True)
    retrieved_map = retrieved_root / SOURCE_MAP_REL
    retrieved_video = retrieved_root / SOURCE_VIDEO_REL
    map_path.write_text(validation_map_payload(retrieved_map), encoding="utf-8")
    material_path.write_text(FIXTURE_MATERIAL, encoding="utf-8")
    shutil.copy2(retrieved_video, video_path)

    stage_temporary_runtime(runtime_dir, output_dir, args.stage_timeout)
    runtime_components = validate_runtime_components(runtime_dir)
    compile_fixture(
        runtime_dir, compile_root, basepath, output_dir, args.dmap_timeout
    )

    generated_files = [map_path, material_path, video_path]
    for suffix in (".proc", ".cm"):
        generated = map_path.with_suffix(suffix)
        if not generated.is_file() or generated.stat().st_size == 0:
            raise FileNotFoundError(f"dmap did not produce {generated}")
        generated_files.append(generated)

    proc_text = map_path.with_suffix(".proc").read_text(
        encoding="utf-8", errors="replace"
    )
    for material in (
        "shaderDemos/transparentMirror",
        "milestoneD/nestedCinematicPost",
    ):
        if material.casefold() not in proc_text.casefold():
            raise ValueError(
                f"compiled fixture does not retain material {material!r}: "
                f"{map_path.with_suffix('.proc')}"
            )

    runtime_game = runtime_dir / "baseoq4"
    for source in generated_files:
        relative = source.relative_to(compile_game)
        target = runtime_game / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    stock_dependencies_after = stock_dependency_inventory(basepath)
    if stock_dependencies_after != stock_dependencies_before:
        raise RuntimeError("stock basepath dependencies changed while building the fixture")

    manifest = {
        "schemaVersion": 1,
        "purpose": "temporary Milestone D nested special-view cinematic/post qualification",
        "shippingContent": False,
        "stockAssetRoot": str(asset_root),
        "basepath": str(basepath),
        "stockDependencies": stock_dependencies_before,
        "runtimeDir": str(runtime_dir),
        "runtimeComponents": runtime_components,
        "map": FIXTURE_MAP_REL.with_suffix("").as_posix(),
        "cameraCommand": "setviewpos 0 -384 96 0 90 0",
        "retrieved": [
            file_record(retrieved_root / SOURCE_MAP_REL, retrieved_root),
            file_record(retrieved_root / SOURCE_VIDEO_REL, retrieved_root),
        ],
        "generated": [file_record(path, compile_game) for path in generated_files],
    }
    manifest_path = output_dir / "fixture_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )
    print(f"fixture runtime: {runtime_dir}")
    print(f"fixture map: {manifest['map']}")
    print(f"fixture manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
