#!/usr/bin/env python3
"""Validate Android arm64 shared libraries before staging an installable APK."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


def validate_library(path: Path, api: int) -> int:
    data = path.read_bytes()
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"{path}: expected a little-endian ELF64 library")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if elf_type != 3 or machine != 183:
        raise ValueError(f"{path}: expected an AArch64 shared library")
    ph_offset = struct.unpack_from("<Q", data, 32)[0]
    ph_size, ph_count = struct.unpack_from("<HH", data, 54)
    if ph_size < 56 or not ph_count or ph_offset + ph_size * ph_count > len(data):
        raise ValueError(f"{path}: invalid ELF program headers")
    loads = 0
    android_api = None
    for index in range(ph_count):
        kind, _, offset, address, _, file_size, memory_size, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, ph_offset + index * ph_size)
        if offset + file_size > len(data):
            raise ValueError(f"{path}: truncated ELF segment")
        if kind == 1:  # PT_LOAD
            loads += 1
            if file_size > memory_size or alignment < 16384 or alignment & (alignment - 1) or offset % alignment != address % alignment:
                raise ValueError(f"{path}: load segment does not support 16 KiB pages")
        elif kind == 4:  # PT_NOTE: NDK crtbegin records the compiled target API.
            end = offset + file_size
            while offset < end:
                if end - offset < 12:
                    raise ValueError(f"{path}: truncated ELF note")
                name_size, desc_size, note_type = struct.unpack_from("<III", data, offset)
                name_start = offset + 12
                desc_start = name_start + ((name_size + 3) & ~3)
                next_note = desc_start + ((desc_size + 3) & ~3)
                if next_note > end:
                    raise ValueError(f"{path}: truncated ELF note data")
                if data[name_start:name_start + name_size].rstrip(b"\0") == b"Android" and note_type == 1 and desc_size >= 4:
                    value = struct.unpack_from("<I", data, desc_start)[0]
                    android_api = max(android_api or 0, value)
                offset = next_note
    if not loads or android_api is None or android_api < 1:
        raise ValueError(f"{path}: missing Android load segments or target API note")
    if android_api > api:
        raise ValueError(f"{path}: targets Android API {android_api}, above the configured API {api}")
    return android_api


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--api", type=int, required=True)
    parser.add_argument("libraries", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        for path in args.libraries:
            native_api = validate_library(path, args.api)
            print(f"{path.name}: Android arm64 API {native_api}, 16 KiB load alignment")
    except (OSError, ValueError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
