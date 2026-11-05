#!/usr/bin/env python3
"""Helpers for validating Windows CRT linkage and staging non-CRT runtime payloads for openQ4."""

from __future__ import annotations

import os
import shutil
import stat
import struct
from pathlib import Path

from game_layer import PACKAGED_LAYER_GAME_DIRS
from openq4_pak import OPENQ4_PACK_NAMES, copy_file_if_changed


PRODUCT_NAME = "openQ4"
GAME_DIR_NAME = "baseoq4"
OPENAL_RUNTIME_OVERRIDES = {
    "x64": [Path("src/external/openal-soft/bin/win64/OpenAL32.dll")],
    "x86": [Path("src/external/openal-soft/bin/win32/OpenAL32.dll")],
    "arm64": [Path("src/external/openal-soft/bin/winarm64/OpenAL32.dll")],
}
WINDOWS_ROOT_RUNTIME_PATTERNS = (
    "OpenAL32.dll",
)
# Meson installs overlay their destination and deliberately do not remove files
# from an earlier build.  Keep the Windows cleanup policy as an explicit,
# narrow manifest: these names can only be foreign POSIX engine binaries or
# obsolete renderer-validation backups in a Windows runtime tree.
WINDOWS_STALE_STAGE_FILE_MANIFEST = (
    "openQ4-client_x86",
    "openQ4-client_x64",
    "openQ4-client_arm64",
    "openQ4-ded_x86",
    "openQ4-ded_x64",
    "openQ4-ded_arm64",
    "renderer-gl_x86.dll.mainbak",
    "renderer-gl_x64.dll.mainbak",
    "renderer-gl_arm64.dll.mainbak",
    "renderer-vk_x86.dll.mainbak",
    "renderer-vk_x64.dll.mainbak",
    "renderer-vk_arm64.dll.mainbak",
)
# These directories were emitted by older content staging.  They are removed
# only when empty; populated directories may contain intentional local content
# and are therefore left untouched.
WINDOWS_EMPTY_STAGE_DIRECTORY_MANIFEST = (
    "baseoq4/skins",
)
RUNTIME_BINARY_PATTERNS = (
    f"{PRODUCT_NAME}-client_*.exe",
    f"{PRODUCT_NAME}-ded_*.exe",
    # Renderer modules are shipped package members and are loaded into the
    # client process, so they belong under the same static-CRT enforcement and
    # mixed-architecture guard as everything else. Without them a stale
    # renderer-gl_x64.dll can sit next to an arm64 client and pass every check.
    "renderer-gl_*.dll",
    "renderer-vk_*.dll",
    f"{GAME_DIR_NAME}/game-sp_*.dll",
    f"{GAME_DIR_NAME}/game-mp_*.dll",
    # game-library layers (q4xbase) ship their own game modules beside baseoq4
    *(f"{layer}/game-{kind}_*.dll" for layer in PACKAGED_LAYER_GAME_DIRS for kind in ("sp", "mp")),
)
BUILD_GAME_GENERATED_IGNORE_PATTERNS = (
    "*.dll.p",
    "*.a.p",
    "*.lib.p",
    "*.obj",
    "*.o",
    "*.a",
    "*.lib",
    "*.exp",
)
SOURCE_GAME_RUNTIME_IGNORE_PATTERNS = (
    "meson.build",
    "mod.json.in",
)

RELEASE_IMPORT_TOKENS = (
    b"ucrtbase.dll",
    b"vcruntime140.dll",
    b"vcruntime140_1.dll",
    b"msvcp140.dll",
)
DEBUG_IMPORT_TOKENS = (
    b"ucrtbased.dll",
    b"vcruntime140d.dll",
    b"vcruntime140_1d.dll",
    b"msvcp140d.dll",
)


class RuntimeFlavor:
    NONE = "none"
    RELEASE = "release"
    DEBUG = "debug"



def is_windows_host() -> bool:
    return os.name == "nt"



def _rva_to_file_offset(rva: int, sections: list[tuple[int, int, int, int]]) -> int | None:
    for virtual_address, virtual_size, raw_size, raw_pointer in sections:
        section_size = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + section_size:
            return raw_pointer + (rva - virtual_address)
    return None



def _read_c_string(data: bytes, offset: int) -> bytes:
    if offset < 0 or offset >= len(data):
        return b""
    end = data.find(b"\0", offset)
    if end == -1:
        end = len(data)
    return data[offset:end]



def _read_pe_import_names(binary_path: Path) -> set[str]:
    data = binary_path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return set()

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 4 > len(data) or data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        return set()

    file_header_offset = e_lfanew + 4
    if file_header_offset + 20 > len(data):
        return set()
    number_of_sections = struct.unpack_from("<H", data, file_header_offset + 2)[0]
    size_of_optional_header = struct.unpack_from("<H", data, file_header_offset + 16)[0]
    optional_header_offset = file_header_offset + 20
    optional_header_end = optional_header_offset + size_of_optional_header
    if optional_header_offset + 2 > len(data) or optional_header_end > len(data):
        return set()
    magic = struct.unpack_from("<H", data, optional_header_offset)[0]

    if magic == 0x10B:
        data_directory_offset = optional_header_offset + 96
    elif magic == 0x20B:
        data_directory_offset = optional_header_offset + 112
    else:
        return set()
    if data_directory_offset + 16 > optional_header_end:
        return set()

    import_rva, _import_size = struct.unpack_from("<II", data, data_directory_offset + 8)
    if import_rva == 0:
        return set()

    section_offset = optional_header_offset + size_of_optional_header
    sections: list[tuple[int, int, int, int]] = []
    for index in range(number_of_sections):
        header_offset = section_offset + (index * 40)
        if header_offset + 40 > len(data):
            return set()
        virtual_size = struct.unpack_from("<I", data, header_offset + 8)[0]
        virtual_address = struct.unpack_from("<I", data, header_offset + 12)[0]
        raw_size = struct.unpack_from("<I", data, header_offset + 16)[0]
        raw_pointer = struct.unpack_from("<I", data, header_offset + 20)[0]
        sections.append((virtual_address, virtual_size, raw_size, raw_pointer))

    import_offset = _rva_to_file_offset(import_rva, sections)
    if import_offset is None:
        return set()

    import_names: set[str] = set()
    descriptor_size = 20
    while import_offset + descriptor_size <= len(data):
        descriptor = struct.unpack_from("<IIIII", data, import_offset)
        if descriptor == (0, 0, 0, 0, 0):
            break

        name_rva = descriptor[3]
        name_offset = _rva_to_file_offset(name_rva, sections)
        if name_offset is not None:
            import_name = _read_c_string(data, name_offset).decode("ascii", errors="ignore").lower()
            if import_name:
                import_names.add(import_name)

        import_offset += descriptor_size

    return import_names



def scan_runtime_imports(binary_path: Path) -> set[str]:
    import_names = _read_pe_import_names(binary_path)
    imports: set[str] = set()
    for token in RELEASE_IMPORT_TOKENS + DEBUG_IMPORT_TOKENS:
        if token.decode("ascii") in import_names:
            imports.add(token.decode("ascii"))
    return imports



def collect_runtime_binaries(root_dir: Path) -> list[Path]:
    if root_dir.is_symlink():
        raise RuntimeError(f"refusing to validate symlinked runtime root: {root_dir}")

    binaries: list[Path] = []
    for pattern in RUNTIME_BINARY_PATTERNS:
        for path in root_dir.glob(pattern):
            if path.is_symlink():
                raise RuntimeError(f"refusing to validate symlinked runtime binary: {path}")
            if path.is_file():
                binaries.append(path)
    return sorted(set(binaries))



def infer_runtime_flavor(root_dir: Path) -> str:
    has_release = False
    has_debug = False

    for binary_path in collect_runtime_binaries(root_dir):
        imports = scan_runtime_imports(binary_path)
        if any(token.decode("ascii") in imports for token in RELEASE_IMPORT_TOKENS):
            has_release = True
        if any(token.decode("ascii") in imports for token in DEBUG_IMPORT_TOKENS):
            has_debug = True

    if has_release and has_debug:
        raise RuntimeError(
            f"Mixed MSVC CRT flavors detected under '{root_dir}'. "
            "openQ4 runtime binaries must all use the same CRT flavor."
        )
    if has_debug:
        return RuntimeFlavor.DEBUG
    if has_release:
        return RuntimeFlavor.RELEASE
    return RuntimeFlavor.NONE



def _binary_arch_from_name(path: Path) -> str | None:
    stem = path.stem
    if "_" not in stem:
        return None
    return stem.rsplit("_", 1)[-1]


def detect_binary_arch(root_dir: Path) -> str:
    arches = sorted(
        {
            arch
            for path in collect_runtime_binaries(root_dir)
            for arch in [_binary_arch_from_name(path)]
            if arch
        }
    )
    if len(arches) > 1:
        raise RuntimeError(
            f"Mixed openQ4 binary architectures detected under '{root_dir}': {', '.join(arches)}"
        )
    if arches:
        return arches[0]
    raise RuntimeError(f"Could not determine openQ4 binary architecture from '{root_dir}'.")


def has_engine_binary(paths: list[Path]) -> bool:
    return any(
        path.name.startswith(f"{PRODUCT_NAME}-client_")
        or path.name.startswith(f"{PRODUCT_NAME}-ded_")
        for path in paths
    )



def list_staged_runtime_files(root_dir: Path) -> list[Path]:
    if root_dir.is_symlink():
        raise RuntimeError(f"refusing to inspect symlinked runtime target: {root_dir}")

    files_by_name: dict[str, Path] = {}
    for pattern in WINDOWS_ROOT_RUNTIME_PATTERNS:
        for path in root_dir.glob(pattern):
            if path.is_file():
                files_by_name[path.name.lower()] = path
    return sorted(files_by_name.values(), key=lambda path: path.name.lower())



def clear_staged_runtime_files(root_dir: Path) -> None:
    for path in list_staged_runtime_files(root_dir):
        path.unlink()


def _manifest_path(root_dir: Path, relative_name: str) -> Path:
    relative_path = Path(relative_name)
    if relative_path.is_absolute() or not relative_path.parts or ".." in relative_path.parts:
        raise RuntimeError(f"invalid Windows stage cleanup manifest path: {relative_name!r}")
    parent = root_dir
    for part in relative_path.parts[:-1]:
        parent /= part
        if _is_link_or_junction(parent):
            raise RuntimeError(f"Windows stage cleanup parent is a link or junction: {parent}")
        if parent.exists() and not parent.is_dir():
            raise RuntimeError(f"Windows stage cleanup parent is not a directory: {parent}")
    candidate = root_dir.joinpath(*relative_path.parts)
    if not _is_relative_to(candidate, root_dir):
        raise RuntimeError(f"Windows stage cleanup path escapes its runtime target: {relative_name!r}")
    return candidate


def _is_link_or_junction(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    if path.is_symlink() or bool(is_junction and is_junction()):
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except OSError:
        return False


def cleanup_windows_stage_target(root_dir: Path) -> dict[str, list[str]]:
    """Remove only explicitly known stale entries from a Windows stage root."""

    root_dir = validate_runtime_directory(root_dir, "Windows runtime target", must_exist=False)
    removed_files: list[str] = []
    removed_directories: list[str] = []

    if not root_dir.exists():
        return {
            "removed_stale_files": removed_files,
            "removed_empty_directories": removed_directories,
        }

    for relative_name in WINDOWS_STALE_STAGE_FILE_MANIFEST:
        candidate = _manifest_path(root_dir, relative_name)
        if _is_link_or_junction(candidate):
            # Unlinking a known stale leaf is safe and does not follow its
            # target.  Directory links are rejected rather than recursively
            # traversed or removed.
            if candidate.is_dir():
                raise RuntimeError(f"stale Windows runtime file is a directory link or junction: {candidate}")
            candidate.unlink()
            removed_files.append(relative_name)
            continue
        if not candidate.exists():
            continue
        if not candidate.is_file():
            raise RuntimeError(f"stale Windows runtime file path is not a regular file: {candidate}")
        candidate.unlink()
        removed_files.append(relative_name)

    for relative_name in WINDOWS_EMPTY_STAGE_DIRECTORY_MANIFEST:
        candidate = _manifest_path(root_dir, relative_name)
        if _is_link_or_junction(candidate):
            raise RuntimeError(f"empty Windows runtime directory is a link or junction: {candidate}")
        if not candidate.exists():
            continue
        if not candidate.is_dir():
            raise RuntimeError(f"empty Windows runtime directory path is not a directory: {candidate}")
        try:
            next(candidate.iterdir())
        except StopIteration:
            candidate.rmdir()
            removed_directories.append(relative_name)

    return {
        "removed_stale_files": removed_files,
        "removed_empty_directories": removed_directories,
    }



def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False



def _remove_directory_inside(directory_path: Path, root_dir: Path) -> None:
    directory_path = directory_path.resolve()
    root_dir = root_dir.resolve()
    if directory_path == root_dir or not _is_relative_to(directory_path, root_dir):
        raise RuntimeError(f"Refusing to remove directory outside build root: '{directory_path}'")
    if directory_path.exists():
        shutil.rmtree(directory_path)



def _copy_runtime_tree(source_dir: Path, destination_dir: Path, ignore_patterns: tuple[str, ...]) -> None:
    if not source_dir.is_dir():
        return
    if source_dir.is_symlink():
        raise RuntimeError(f"refusing to stage symlinked runtime directory: {source_dir}")
    for path in source_dir.rglob("*"):
        if path.is_symlink():
            raise RuntimeError(f"refusing to stage symlink from generated runtime tree: {path}")
    shutil.copytree(
        source_dir,
        destination_dir,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns(*ignore_patterns),
    )

def validate_runtime_directory(path: Path, label: str, *, must_exist: bool) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    if _is_link_or_junction(path):
        raise RuntimeError(f"{label} must not be a junction or reparse point: {path}")
    if path.exists() and not path.is_dir():
        raise RuntimeError(f"{label} exists but is not a directory: {path}")
    if must_exist and not path.is_dir():
        raise RuntimeError(f"{label} does not exist: {path}")
    return path.resolve()


def stage_build_game_directory(source_root: Path, build_root: Path) -> dict[str, object]:
    """Prepare builddir/baseoq4 so the client can run directly from builddir."""

    source_root = validate_runtime_directory(source_root, "source root", must_exist=False)
    build_root = validate_runtime_directory(build_root, "build root", must_exist=True)
    build_generated_game_dir = build_root / "content" / GAME_DIR_NAME
    build_runtime_game_dir = build_root / GAME_DIR_NAME
    build_game_pk4s = [build_root / name for name in OPENQ4_PACK_NAMES]
    staged_file_count = 0

    if not build_generated_game_dir.is_dir() and not any(path.is_file() for path in build_game_pk4s):
        return {
            "directory": None,
            "file_count": staged_file_count,
        }

    _remove_directory_inside(build_runtime_game_dir, build_root)
    build_runtime_game_dir.mkdir(parents=True, exist_ok=True)

    _copy_runtime_tree(build_generated_game_dir, build_runtime_game_dir, BUILD_GAME_GENERATED_IGNORE_PATTERNS)
    for build_game_pk4 in build_game_pk4s:
        if build_game_pk4.is_file():
            copy_file_if_changed(build_game_pk4, build_runtime_game_dir / build_game_pk4.name)

    for path in build_runtime_game_dir.rglob("*"):
        if path.is_file():
            staged_file_count += 1

    return {
        "directory": str(build_runtime_game_dir),
        "file_count": staged_file_count,
    }



def ensure_no_msvc_runtime_imports(root_dir: Path) -> dict[str, list[str]]:
    violations: dict[str, list[str]] = {}
    for binary_path in collect_runtime_binaries(root_dir):
        imports = sorted(scan_runtime_imports(binary_path))
        if imports:
            violations[str(binary_path)] = imports
    return violations



def _copy_file_if_changed(source_path: Path, target_dir: Path) -> Path | None:
    destination = target_dir / source_path.name
    return destination if copy_file_if_changed(source_path, destination) else None



def resolve_openal_runtime_path(source_root: Path, arch: str) -> Path | None:
    override_root_raw = os.environ.get("OPENQ4_OPENAL_ROOT", "").strip()
    if override_root_raw:
        override_root = validate_runtime_directory(
            Path(override_root_raw),
            "OpenAL override root",
            must_exist=True,
        )
        override_path = override_root / "bin" / "OpenAL32.dll"
        if override_path.is_symlink():
            raise RuntimeError(f"OpenAL override runtime must not be a symlink: {override_path}")
        if not override_path.is_file():
            raise RuntimeError(f"OpenAL override runtime not found: {override_path}")
        return override_path

    for relative in OPENAL_RUNTIME_OVERRIDES.get(arch, []):
        candidate = source_root / relative
        if candidate.is_symlink():
            raise RuntimeError(f"bundled OpenAL runtime must not be a symlink: {candidate}")
        if candidate.is_file():
            return candidate

    return None


def stage_runtime_payloads(
    source_root: Path,
    build_root: Path,
    targets: list[Path],
) -> dict[str, object]:
    source_root = validate_runtime_directory(source_root, "source root", must_exist=False)
    build_root = validate_runtime_directory(build_root, "build root", must_exist=True)
    targets = [
        validate_runtime_directory(target, "runtime target", must_exist=False)
        for target in targets
    ]

    staged_build_game = stage_build_game_directory(source_root, build_root)
    cleanup_results: dict[str, dict[str, list[str]]] = {}
    for target in targets:
        cleanup_results[str(target)] = cleanup_windows_stage_target(target)

    binaries = collect_runtime_binaries(build_root)
    if not binaries:
        return {
            "arch": None,
            "runtime_flavor": RuntimeFlavor.NONE,
            "targets": [str(target) for target in targets],
            "copied_files": [],
            "stage_cleanup": cleanup_results,
            "staged_build_game": staged_build_game,
            "validated_binaries": [],
        }

    arch = detect_binary_arch(build_root)
    flavor = infer_runtime_flavor(build_root)
    violations = ensure_no_msvc_runtime_imports(build_root)
    if violations:
        violation_lines = []
        for binary_name, imports in sorted(violations.items()):
            violation_lines.append(f"{binary_name}: {', '.join(imports)}")
        raise RuntimeError(
            "openQ4 Windows binaries still import the MSVC/UCRT runtime. "
            "Expected static CRT linkage for all builds.\n"
            + "\n".join(violation_lines)
        )

    copied_files: list[str] = []
    openal_runtime = resolve_openal_runtime_path(source_root, arch)
    if openal_runtime is None and has_engine_binary(binaries):
        raise RuntimeError(
            f"OpenAL32.dll runtime not found for Windows {arch}. "
            "Set OPENQ4_OPENAL_ROOT to a prepared OpenAL Soft package or add the bundled runtime."
        )
    for target in targets:
        target.mkdir(parents=True, exist_ok=True)
        expected_runtime_names = {openal_runtime.name.lower()} if openal_runtime is not None else set()
        for staged_runtime in list_staged_runtime_files(target):
            if staged_runtime.name.lower() not in expected_runtime_names:
                staged_runtime.unlink()

        if openal_runtime is not None:
            copied_file = _copy_file_if_changed(openal_runtime, target)
            if copied_file is not None:
                copied_files.append(str(copied_file))

    return {
        "arch": arch,
        "runtime_flavor": flavor,
        "targets": [str(target) for target in targets],
        "copied_files": sorted(set(copied_files)),
        "stage_cleanup": cleanup_results,
        "staged_build_game": staged_build_game,
        "validated_binaries": [str(path) for path in binaries],
    }
