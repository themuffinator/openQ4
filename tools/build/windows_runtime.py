#!/usr/bin/env python3
"""Helpers for validating Windows CRT linkage and staging non-CRT runtime payloads for OpenQ4."""

from __future__ import annotations

import os
import shutil
import struct
from pathlib import Path


PRODUCT_NAME = "OpenQ4"
GAME_DIR_NAME = "baseoq4"
OPENAL_RUNTIME_OVERRIDES = {
    "x64": [Path("src/external/openal-soft/bin/win64/OpenAL32.dll")],
    "x86": [Path("src/external/openal-soft/bin/win32/OpenAL32.dll")],
    "arm64": [Path("src/external/openal-soft/bin/winarm64/OpenAL32.dll")],
}
WINDOWS_ROOT_RUNTIME_PATTERNS = (
    "OpenAL32.dll",
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
    number_of_sections = struct.unpack_from("<H", data, file_header_offset + 2)[0]
    size_of_optional_header = struct.unpack_from("<H", data, file_header_offset + 16)[0]
    optional_header_offset = file_header_offset + 20
    magic = struct.unpack_from("<H", data, optional_header_offset)[0]

    if magic == 0x10B:
        data_directory_offset = optional_header_offset + 96
    elif magic == 0x20B:
        data_directory_offset = optional_header_offset + 112
    else:
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
    patterns = (
        f"{PRODUCT_NAME}-client_*.exe",
        f"{PRODUCT_NAME}-ded_*.exe",
        f"{GAME_DIR_NAME}/game-sp_*.dll",
        f"{GAME_DIR_NAME}/game-mp_*.dll",
    )
    binaries: list[Path] = []
    for pattern in patterns:
        binaries.extend(path for path in root_dir.glob(pattern) if path.is_file())
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
            "OpenQ4 runtime binaries must all use the same CRT flavor."
        )
    if has_debug:
        return RuntimeFlavor.DEBUG
    if has_release:
        return RuntimeFlavor.RELEASE
    return RuntimeFlavor.NONE



def detect_binary_arch(root_dir: Path) -> str:
    patterns = (
        f"{PRODUCT_NAME}-client_*.exe",
        f"{PRODUCT_NAME}-ded_*.exe",
        f"{GAME_DIR_NAME}/game-sp_*.dll",
        f"{GAME_DIR_NAME}/game-mp_*.dll",
    )
    for pattern in patterns:
        for path in sorted(root_dir.glob(pattern)):
            stem = path.stem
            if "_" not in stem:
                continue
            return stem.rsplit("_", 1)[-1]
    raise RuntimeError(f"Could not determine OpenQ4 binary architecture from '{root_dir}'.")



def list_staged_runtime_files(root_dir: Path) -> list[Path]:
    files_by_name: dict[str, Path] = {}
    for pattern in WINDOWS_ROOT_RUNTIME_PATTERNS:
        for path in root_dir.glob(pattern):
            if path.is_file():
                files_by_name[path.name.lower()] = path
    return sorted(files_by_name.values(), key=lambda path: path.name.lower())



def clear_staged_runtime_files(root_dir: Path) -> None:
    for path in list_staged_runtime_files(root_dir):
        path.unlink()



def ensure_no_msvc_runtime_imports(root_dir: Path) -> dict[str, list[str]]:
    violations: dict[str, list[str]] = {}
    for binary_path in collect_runtime_binaries(root_dir):
        imports = sorted(scan_runtime_imports(binary_path))
        if imports:
            violations[str(binary_path)] = imports
    return violations



def _copy_file(source_path: Path, target_dir: Path) -> Path:
    destination = target_dir / source_path.name
    shutil.copy2(source_path, destination)
    return destination



def resolve_openal_runtime_path(source_root: Path, arch: str) -> Path | None:
    override_root_raw = os.environ.get("OPENQ4_OPENAL_ROOT", "").strip()
    if override_root_raw:
        override_path = Path(override_root_raw).resolve() / "bin" / "OpenAL32.dll"
        if override_path.is_file():
            return override_path

    for relative in OPENAL_RUNTIME_OVERRIDES.get(arch, []):
        candidate = source_root / relative
        if candidate.is_file():
            return candidate

    return None


def stage_runtime_payloads(
    source_root: Path,
    build_root: Path,
    targets: list[Path],
) -> dict[str, object]:
    build_root = build_root.resolve()
    source_root = source_root.resolve()

    binaries = collect_runtime_binaries(build_root)
    if not binaries:
        return {
            "arch": None,
            "runtime_flavor": RuntimeFlavor.NONE,
            "targets": [str(target) for target in targets],
            "copied_files": [],
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
            "OpenQ4 Windows binaries still import the MSVC/UCRT runtime. "
            "Expected static CRT linkage for all builds.\n"
            + "\n".join(violation_lines)
        )

    copied_files: list[str] = []
    openal_runtime = resolve_openal_runtime_path(source_root, arch)
    for target in targets:
        target.mkdir(parents=True, exist_ok=True)
        clear_staged_runtime_files(target)

        if openal_runtime is not None:
            copied_files.append(str(_copy_file(openal_runtime, target)))

    return {
        "arch": arch,
        "runtime_flavor": flavor,
        "targets": [str(target) for target in targets],
        "copied_files": sorted(set(copied_files)),
        "validated_binaries": [str(path) for path in binaries],
    }
