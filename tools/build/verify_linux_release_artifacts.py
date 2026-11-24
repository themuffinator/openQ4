#!/usr/bin/env python3
"""Verify stripped Linux release ELFs, detached symbols, and runtime archives."""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zlib
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.validation import openq4_validate as staged_validator  # noqa: E402
from tools.build.game_layer import (  # noqa: E402
    GAME_MODULE_STEM,
    PACKAGED_LAYER_GAME_DIRS,
    RETIRED_GAME_MODULE_STEMS,
    LayerError,
    read_layer_mod_json,
)


SUPPORTED_ARCHES = {
    "x64": {"amd64", "x86_64"},
    "arm64": {"aarch64", "arm64"},
}
MAX_ARCHIVE_BYTES = 4 * 1024 * 1024 * 1024
MAX_ARCHIVE_ENTRIES = 20_000
MAX_ARCHIVE_MEMBER_BYTES = 2 * 1024 * 1024 * 1024
MAX_ARCHIVE_TOTAL_BYTES = 8 * 1024 * 1024 * 1024
MAX_ARCHIVE_PATH_BYTES = 1024
GNU_DEBUGLINK_SECTION = ".gnu_debuglink"
NEEDED_RE = re.compile(r"\(NEEDED\).*Shared library: \[([^\]]+)\]")
SAFE_NEEDED_RE = re.compile(r"lib[A-Za-z0-9+_.-]+\.so(?:\.[A-Za-z0-9+_.-]+)*")
FORBIDDEN_DIAGNOSTIC_LIB_RE = re.compile(
    r"(?:^lib(?:asan|ubsan|tsan|lsan)\.so|(?:^|[-_.])(?:asan|ubsan|tsan|lsan)(?:[-_.]|$))",
    re.IGNORECASE,
)


class ReleaseArtifactError(RuntimeError):
    """A Linux release artifact failed its post-mutation contract."""


def require_directory(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise ReleaseArtifactError(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise ReleaseArtifactError(f"{label} is unavailable: {path}") from exc
    if not resolved.is_dir():
        raise ReleaseArtifactError(f"{label} is not a directory: {resolved}")
    return resolved


def require_regular_file(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise ReleaseArtifactError(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise ReleaseArtifactError(f"{label} is unavailable: {path}") from exc
    if not resolved.is_file():
        raise ReleaseArtifactError(f"{label} is not a regular file: {resolved}")
    return resolved


def normalize_host_arch() -> str:
    machine = platform.machine().strip().lower()
    for arch, names in SUPPORTED_ARCHES.items():
        if machine in names:
            return arch
    return machine or "unknown"


def require_native_arch(expected_arch: str) -> None:
    if expected_arch not in SUPPORTED_ARCHES:
        raise ReleaseArtifactError(f"Unsupported Linux release architecture: {expected_arch}")
    host_arch = normalize_host_arch()
    if host_arch != expected_arch:
        raise ReleaseArtifactError(
            f"Linux release artifact verification requires a native {expected_arch} runner; "
            f"current host is {host_arch}"
        )


def packaged_layer_dirs(runtime_root: Path) -> list[str]:
    """The game-library layers (q4xbase) this payload carries beside baseoq4."""
    return [
        name
        for name in PACKAGED_LAYER_GAME_DIRS
        if (runtime_root / name).exists() or (runtime_root / name).is_symlink()
    ]


def expected_runtime_binaries(runtime_root: Path, arch: str) -> list[tuple[Path, str, bool]]:
    expected = [
        (runtime_root / f"openQ4-client_{arch}", "openQ4-client", False),
        (runtime_root / f"openQ4-ded_{arch}", "openQ4-ded", False),
        (runtime_root / "baseoq4" / f"{GAME_MODULE_STEM}_{arch}.so", GAME_MODULE_STEM, True),
    ]
    # a layer's module comes after baseoq4's, so expected[2] stays baseoq4's
    for name in packaged_layer_dirs(runtime_root):
        expected.append((runtime_root / name / f"{GAME_MODULE_STEM}_{arch}.so", GAME_MODULE_STEM, True))
    # the in-game updater's file-swapping helper ships hardened like the engine
    expected.append((runtime_root / f"openQ4-updater_{arch}", "openQ4-updater", False))
    return expected


def validate_layer_game_dir(runtime_root: Path, name: str, arch: str) -> None:
    """A layer game directory holds exactly its game module and its layer mod.json."""
    layer_dir = require_directory(runtime_root / name, f"Linux runtime {name} directory")
    expected = {f"{GAME_MODULE_STEM}_{arch}.so", "mod.json"}
    actual = {path.relative_to(layer_dir).as_posix() for path in layer_dir.rglob("*")}
    if actual != expected:
        raise ReleaseArtifactError(
            f"Linux {name}/ must hold exactly {sorted(expected)}; "
            f"missing {sorted(expected - actual)}, unexpected {sorted(actual - expected)}"
        )
    try:
        read_layer_mod_json(layer_dir / "mod.json")
    except LayerError as exc:
        raise ReleaseArtifactError(str(exc)) from exc


def reject_unexpected_binary_variants(
    runtime_root: Path, expected: list[tuple[Path, str, bool]]
) -> None:
    game_dir = runtime_root / "baseoq4"
    candidates = set(runtime_root.glob("openQ4-client_*"))
    candidates.update(runtime_root.glob("openQ4-ded_*"))
    candidates.update(runtime_root.glob("openQ4-updater_*"))
    for module_dir in (game_dir, *(runtime_root / name for name in packaged_layer_dirs(runtime_root))):
        # the retired split game-sp/game-mp modules count as unexpected variants
        for stem in (GAME_MODULE_STEM, *RETIRED_GAME_MODULE_STEMS):
            candidates.update(module_dir.glob(f"{stem}_*"))
    actual = {path.resolve(strict=False) for path in candidates}
    expected_paths = {path.resolve(strict=False) for path, _, _ in expected}
    if actual != expected_paths:
        missing = sorted(str(path) for path in expected_paths - actual)
        unexpected = sorted(str(path) for path in actual - expected_paths)
        raise ReleaseArtifactError(
            "Linux runtime binary set does not match its architecture contract. "
            f"Missing: {missing or '<none>'}; unexpected: {unexpected or '<none>'}"
        )


def sanitized_loader_environment() -> dict[str, str]:
    env = os.environ.copy()
    for variable in (
        "LD_AUDIT",
        "LD_DEBUG",
        "LD_LIBRARY_PATH",
        "LD_PRELOAD",
    ):
        env.pop(variable, None)
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    return env


def validate_linux_dependency_contract(
    root: Path, binary_specs: list[tuple[Path, str, bool]]
) -> None:
    ldd = shutil.which("ldd")
    if ldd is None:
        raise ReleaseArtifactError("Linux dependency validation requires ldd")

    for binary_path, arch, _ in binary_specs:
        dynamic = staged_validator.readelf_output(binary_path, ["-W", "-d"], root)
        forbidden_dynamic_tags = [
            tag for tag in ("RPATH", "RUNPATH", "TEXTREL") if f"({tag})" in dynamic
        ]
        if forbidden_dynamic_tags:
            raise ReleaseArtifactError(
                f"Linux release binary has forbidden dynamic tags {forbidden_dynamic_tags}: "
                f"{binary_path.relative_to(root)}"
            )

        needed = NEEDED_RE.findall(dynamic)
        if not needed:
            raise ReleaseArtifactError(
                f"Linux release binary has no declared runtime dependencies: "
                f"{binary_path.relative_to(root)}"
            )
        architecture_runtime_names = staged_validator.LINUX_DEDICATED_ARCH_ALLOWED_NEEDED.get(
            arch,
            frozenset(),
        )
        unsafe_needed = [
            dependency
            for dependency in needed
            if (
                SAFE_NEEDED_RE.fullmatch(dependency) is None
                and dependency not in architecture_runtime_names
            )
            or "/" in dependency
            or "\\" in dependency
            or FORBIDDEN_DIAGNOSTIC_LIB_RE.search(dependency) is not None
        ]
        if unsafe_needed:
            raise ReleaseArtifactError(
                f"Linux release binary has unsafe or diagnostic DT_NEEDED entries: "
                f"{binary_path.relative_to(root)} -> {unsafe_needed}"
            )

        completed = subprocess.run(
            [ldd, "-r", str(binary_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=sanitized_loader_environment(),
        )
        diagnostics = "\n".join(
            part for part in (completed.stdout.strip(), completed.stderr.strip()) if part
        )
        unresolved = re.search(
            r"(?:\bnot found\b|\bundefined symbol\b|\bsymbol lookup error\b)",
            diagnostics,
            flags=re.IGNORECASE,
        )
        if completed.returncode != 0 or unresolved is not None:
            raise ReleaseArtifactError(
                f"Linux release dependency resolution failed for "
                f"{binary_path.relative_to(root)} (ldd -r exit {completed.returncode}):\n"
                f"{diagnostics or '<no diagnostics>'}"
            )


def validate_linux_runtime_payload(runtime_root: Path, arch: str) -> list[tuple[Path, str, bool]]:
    require_native_arch(arch)
    runtime_root = require_directory(runtime_root, "Linux runtime root")
    # Windows embeds the startup splash as an RC resource and the macOS bundle
    # carries it under Contents/Resources, so Linux is the only package that ships
    # it as a loose file - and the only one that can quietly lose it.
    require_regular_file(
        runtime_root / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp",
        "Linux startup splash bitmap",
    )
    game_dir = require_directory(runtime_root / "baseoq4", "Linux runtime game directory")
    expected = expected_runtime_binaries(runtime_root, arch)
    for binary_path, stem, _ in expected:
        require_regular_file(binary_path, f"Linux {stem} binary")
    for binary_path, _, is_game_module in expected:
        if not is_game_module and not os.access(binary_path, os.X_OK):
            raise ReleaseArtifactError(f"Linux engine binary is not executable: {binary_path}")

    reject_unexpected_binary_variants(runtime_root, expected)
    staged_validator.validate_no_staged_symlinks(runtime_root, runtime_root)
    debug_files = sorted(runtime_root.rglob("*.debug"))
    if debug_files:
        raise ReleaseArtifactError(
            "Linux runtime payload contains detached debug files: "
            + ", ".join(str(path.relative_to(runtime_root)) for path in debug_files)
        )

    clients = [expected[0][0]]
    dedicated = [expected[1][0]]
    game_modules = [expected[2][0]]
    staged_arches = staged_validator.validate_staged_architecture_set(
        runtime_root, game_dir, clients, dedicated
    )
    if staged_arches != {arch}:
        raise ReleaseArtifactError(
            f"Linux runtime architecture set is {sorted(staged_arches)}, expected only {arch}"
        )
    binary_specs = [
        (path, arch, is_game_module) for path, _, is_game_module in expected
    ]
    staged_validator.validate_linux_binary_hardening(runtime_root, binary_specs)
    staged_validator.validate_linux_client_runtime_dependencies(runtime_root, clients)
    staged_validator.validate_linux_dedicated_runtime_dependencies(
        runtime_root,
        [(dedicated[0], arch), (game_modules[0], arch)],
    )
    for name in packaged_layer_dirs(runtime_root):
        validate_layer_game_dir(runtime_root, name, arch)
        layer_dir = runtime_root / name
        layer_module = layer_dir / f"{GAME_MODULE_STEM}_{arch}.so"
        layer_arches = staged_validator.validate_staged_architecture_set(runtime_root, layer_dir, clients, dedicated)
        if layer_arches != {arch}:
            raise ReleaseArtifactError(
                f"Linux {name} architecture set is {sorted(layer_arches)}, expected only {arch}"
            )
        staged_validator.validate_linux_dedicated_runtime_dependencies(runtime_root, [(layer_module, arch)])
    validate_linux_dependency_contract(runtime_root, binary_specs)
    return expected


def elf_byteorder(path: Path) -> str:
    with path.open("rb") as handle:
        identity = handle.read(6)
    if len(identity) != 6 or identity[:4] != b"\x7fELF":
        raise ReleaseArtifactError(f"Not an ELF file: {path}")
    if identity[5] == 1:
        return "little"
    if identity[5] == 2:
        return "big"
    raise ReleaseArtifactError(f"ELF has unknown byte order: {path}")


def dump_elf_section(path: Path, section_name: str) -> bytes:
    objcopy = shutil.which("objcopy")
    if objcopy is None:
        raise ReleaseArtifactError("Linux ELF section validation requires objcopy")
    with tempfile.TemporaryDirectory(prefix="openq4-elf-section-") as temp_dir:
        section_path = Path(temp_dir) / "section.bin"
        copied_elf_path = Path(temp_dir) / "validated-elf-copy"
        completed = subprocess.run(
            [
                objcopy,
                f"--dump-section={section_name}={section_path}",
                str(path),
                str(copied_elf_path),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if completed.returncode != 0 or not section_path.is_file():
            diagnostics = completed.stderr.strip() or completed.stdout.strip()
            raise ReleaseArtifactError(
                f"Unable to read {section_name} from {path}: "
                f"{diagnostics or '<no diagnostics>'}"
            )
        return section_path.read_bytes()


def parse_gnu_build_id_note(data: bytes, byteorder: str) -> str:
    if len(data) < 16:
        raise ReleaseArtifactError(".note.gnu.build-id is truncated")
    name_size = int.from_bytes(data[0:4], byteorder=byteorder)
    description_size = int.from_bytes(data[4:8], byteorder=byteorder)
    note_type = int.from_bytes(data[8:12], byteorder=byteorder)
    if name_size != 4 or note_type != 3:
        raise ReleaseArtifactError(".note.gnu.build-id has an invalid GNU note header")
    name_offset = 12
    description_offset = name_offset + ((name_size + 3) & ~3)
    description_end = description_offset + description_size
    if data[name_offset : name_offset + name_size] != b"GNU\0":
        raise ReleaseArtifactError(".note.gnu.build-id has an invalid owner")
    if description_size < 8 or description_end > len(data):
        raise ReleaseArtifactError(".note.gnu.build-id has an invalid description")
    build_id = data[description_offset:description_end].hex()
    if len(build_id) < 16 or len(build_id) % 2 != 0:
        raise ReleaseArtifactError(f".note.gnu.build-id is malformed: {build_id}")
    return build_id


def read_elf_build_id(path: Path, root: Path) -> str:
    try:
        return parse_gnu_build_id_note(
            dump_elf_section(path, ".note.gnu.build-id"), elf_byteorder(path)
        )
    except ReleaseArtifactError as exc:
        raise ReleaseArtifactError(
            f"Linux ELF build ID validation failed for {path.relative_to(root)}: {exc}"
        ) from exc


def parse_gnu_debuglink_section(data: bytes, byteorder: str) -> tuple[str, int]:
    terminator = data.find(b"\0")
    if terminator <= 0:
        raise ReleaseArtifactError(".gnu_debuglink is missing its filename terminator")
    try:
        filename = data[:terminator].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ReleaseArtifactError(".gnu_debuglink filename is not ASCII") from exc
    if (
        filename in {".", ".."}
        or "/" in filename
        or "\\" in filename
        or any(ord(character) < 32 or ord(character) == 127 for character in filename)
    ):
        raise ReleaseArtifactError(f".gnu_debuglink has an unsafe filename: {filename!r}")
    crc_offset = (terminator + 1 + 3) & ~3
    if len(data) < crc_offset + 4:
        raise ReleaseArtifactError(".gnu_debuglink is missing its CRC32")
    crc = int.from_bytes(data[crc_offset : crc_offset + 4], byteorder=byteorder)
    return filename, crc


def dump_gnu_debuglink(path: Path) -> tuple[str, int]:
    return parse_gnu_debuglink_section(
        dump_elf_section(path, GNU_DEBUGLINK_SECTION), elf_byteorder(path)
    )


def gnu_debuglink_crc32(path: Path) -> int:
    crc = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def validate_debuglink_pairs(
    runtime_root: Path,
    symbols_root: Path,
    binaries: list[tuple[Path, str, bool]],
) -> None:
    symbols_root = require_directory(symbols_root, "Linux detached-symbol root")
    staged_validator.validate_no_staged_symlinks(symbols_root, symbols_root)
    expected_debug_files: set[Path] = set()
    for binary_path, _, _ in binaries:
        relative = binary_path.relative_to(runtime_root)
        debug_file = symbols_root / f"{relative.as_posix()}.debug"
        debug_file = require_regular_file(debug_file, "Linux detached debug file")
        expected_debug_files.add(debug_file)

        runtime_build_id = read_elf_build_id(binary_path, runtime_root)
        debug_build_id = read_elf_build_id(debug_file, symbols_root)
        if runtime_build_id != debug_build_id:
            raise ReleaseArtifactError(
                f"Detached debug build ID does not match runtime ELF for {relative}: "
                f"runtime={runtime_build_id} debug={debug_build_id}"
            )

        debuglink_name, debuglink_crc = dump_gnu_debuglink(binary_path)
        if debuglink_name != debug_file.name:
            raise ReleaseArtifactError(
                f"Runtime ELF debuglink for {relative} is {debuglink_name!r}, "
                f"expected {debug_file.name!r}"
            )
        actual_crc = gnu_debuglink_crc32(debug_file)
        if debuglink_crc != actual_crc:
            raise ReleaseArtifactError(
                f"Runtime ELF debuglink CRC does not match detached debug file for {relative}: "
                f"debuglink={debuglink_crc:08x} actual={actual_crc:08x}"
            )

    actual_debug_files = {
        path.resolve() for path in symbols_root.rglob("*.debug") if path.is_file()
    }
    if actual_debug_files != expected_debug_files:
        missing = sorted(str(path) for path in expected_debug_files - actual_debug_files)
        unexpected = sorted(str(path) for path in actual_debug_files - expected_debug_files)
        raise ReleaseArtifactError(
            "Linux detached-symbol file set does not match runtime ELFs. "
            f"Missing: {missing or '<none>'}; unexpected: {unexpected or '<none>'}"
        )


def validate_archive_member_name(name: str) -> PurePosixPath:
    try:
        encoded_name = name.encode("utf-8")
    except UnicodeError as exc:
        raise ReleaseArtifactError(
            f"Linux runtime archive path is not valid UTF-8: {name!r}"
        ) from exc
    if (
        not name
        or name.startswith("/")
        or "\\" in name
        or len(encoded_name) > MAX_ARCHIVE_PATH_BYTES
        or any(ord(character) < 32 or ord(character) == 127 for character in name)
    ):
        raise ReleaseArtifactError(f"Linux runtime archive has an unsafe path: {name!r}")
    raw_parts = name.split("/")
    if any(part in {"", ".", ".."} for part in raw_parts):
        raise ReleaseArtifactError(f"Linux runtime archive path escapes its root: {name!r}")
    path = PurePosixPath(name)
    if not path.parts or any(part in {"", ".", ".."} for part in path.parts):
        raise ReleaseArtifactError(f"Linux runtime archive path escapes its root: {name!r}")
    return path


# The release ships each Linux package as .tar.xz and, for sites that do not
# take xz (ModDB), the same tar as .tar.gz.
LINUX_ARCHIVE_READ_MODES = {".tar.xz": "r:xz", ".tar.gz": "r:gz"}


def linux_archive_suffix(archive_path: Path) -> str:
    for suffix in LINUX_ARCHIVE_READ_MODES:
        if archive_path.name.endswith(suffix):
            return suffix
    raise ReleaseArtifactError(
        "Linux runtime archive must use the expected .tar.xz or .tar.gz suffix: " + archive_path.name
    )


def expected_archive_root_name(archive_path: Path) -> str:
    suffix = linux_archive_suffix(archive_path)
    name = archive_path.name[: -len(suffix)]
    if re.fullmatch(r"openq4-[A-Za-z0-9._-]+-linux-(?:x64|arm64)(?:-preview)?", name) is None:
        raise ReleaseArtifactError(f"Linux runtime archive has an unsafe package stem: {name!r}")
    return name


def extract_linux_runtime_archive(archive_path: Path, extraction_root: Path) -> Path:
    archive_path = require_regular_file(archive_path, "Linux runtime archive")
    if archive_path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ReleaseArtifactError(
            f"Linux runtime archive exceeds {MAX_ARCHIVE_BYTES} bytes: {archive_path}"
        )
    extraction_root.mkdir(parents=True, exist_ok=False)
    extraction_root = extraction_root.resolve(strict=True)
    expected_root = expected_archive_root_name(archive_path)
    seen_paths: set[PurePosixPath] = set()
    top_levels: set[str] = set()
    total_size = 0
    entry_count = 0

    try:
        archive = tarfile.open(archive_path, mode=LINUX_ARCHIVE_READ_MODES[linux_archive_suffix(archive_path)])
    except (OSError, tarfile.TarError) as exc:
        raise ReleaseArtifactError(f"Linux runtime archive is unreadable: {archive_path}") from exc

    with archive:
        for member in archive:
            entry_count += 1
            if entry_count > MAX_ARCHIVE_ENTRIES:
                raise ReleaseArtifactError(
                    f"Linux runtime archive exceeds {MAX_ARCHIVE_ENTRIES} entries"
                )
            member_path = validate_archive_member_name(member.name)
            if member_path in seen_paths:
                raise ReleaseArtifactError(
                    f"Linux runtime archive contains a duplicate path: {member.name}"
                )
            seen_paths.add(member_path)
            top_levels.add(member_path.parts[0])

            if not member.isfile():
                raise ReleaseArtifactError(
                    f"Linux runtime archive contains a non-regular entry: {member.name}"
                )
            if member.size < 0 or member.size > MAX_ARCHIVE_MEMBER_BYTES:
                raise ReleaseArtifactError(
                    f"Linux runtime archive member has an unsafe size: "
                    f"{member.name} ({member.size})"
                )
            total_size += member.size
            if total_size > MAX_ARCHIVE_TOTAL_BYTES:
                raise ReleaseArtifactError(
                    f"Linux runtime archive expands beyond {MAX_ARCHIVE_TOTAL_BYTES} bytes"
                )
            mode = member.mode & 0o7777
            if mode not in {0o644, 0o755}:
                raise ReleaseArtifactError(
                    f"Linux runtime archive member has an unsafe mode: {member.name} ({mode:o})"
                )

            destination = extraction_root.joinpath(*member_path.parts)
            resolved_destination = destination.resolve(strict=False)
            if not resolved_destination.is_relative_to(extraction_root):
                raise ReleaseArtifactError(
                    f"Linux runtime archive path escapes extraction root: {member.name}"
                )
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ReleaseArtifactError(
                    f"Linux runtime archive member is unreadable: {member.name}"
                )
            copied = 0
            with source, destination.open("xb") as output:
                while copied < member.size:
                    chunk = source.read(min(1024 * 1024, member.size - copied))
                    if not chunk:
                        break
                    output.write(chunk)
                    copied += len(chunk)
            if copied != member.size:
                raise ReleaseArtifactError(
                    f"Linux runtime archive member was truncated: {member.name} "
                    f"({copied}/{member.size} bytes)"
                )
            os.chmod(destination, mode)

    if entry_count == 0:
        raise ReleaseArtifactError("Linux runtime archive is empty")
    if top_levels != {expected_root}:
        raise ReleaseArtifactError(
            f"Linux runtime archive top-level roots are {sorted(top_levels)}, "
            f"expected only {expected_root}"
        )
    runtime_root = extraction_root / expected_root
    return require_directory(runtime_root, "Extracted Linux runtime root")


def verify_split(args: argparse.Namespace) -> None:
    runtime_root = require_directory(args.runtime_root, "Linux staged runtime root")
    binaries = validate_linux_runtime_payload(runtime_root, args.arch)
    validate_debuglink_pairs(runtime_root, args.symbols_root, binaries)
    print(
        f"stripped Linux release ELFs and detached debug links verified: "
        f"arch={args.arch} root={runtime_root}"
    )


def verify_archive(args: argparse.Namespace) -> None:
    require_native_arch(args.arch)
    work_root = require_directory(args.work_root, "Linux archive verification work root")
    with tempfile.TemporaryDirectory(
        prefix=f"openq4-linux-{args.arch}-archive-", dir=work_root
    ) as temp_dir:
        extraction_root = Path(temp_dir) / "extract"
        runtime_root = extract_linux_runtime_archive(args.archive, extraction_root)
        binaries = validate_linux_runtime_payload(runtime_root, args.arch)
        validate_debuglink_pairs(runtime_root, args.symbols_root, binaries)
    print(f"extracted Linux runtime archive verified: arch={args.arch} archive={args.archive}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    split = subparsers.add_parser("split", help="Verify stripped staged ELFs and detached symbols")
    split.add_argument("--runtime-root", required=True, type=Path)
    split.add_argument("--symbols-root", required=True, type=Path)
    split.add_argument("--arch", required=True, choices=sorted(SUPPORTED_ARCHES))
    split.set_defaults(callback=verify_split)

    archive = subparsers.add_parser("archive", help="Extract and verify a Linux runtime archive")
    archive.add_argument("--archive", required=True, type=Path)
    archive.add_argument("--arch", required=True, choices=sorted(SUPPORTED_ARCHES))
    archive.add_argument("--symbols-root", required=True, type=Path)
    archive.add_argument("--work-root", required=True, type=Path)
    archive.set_defaults(callback=verify_archive)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        args.callback(args)
    except (
        EOFError,
        OSError,
        ReleaseArtifactError,
        staged_validator.ValidationError,
        tarfile.TarError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
