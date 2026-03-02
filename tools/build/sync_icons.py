#!/usr/bin/env python3
"""Sync OpenQ4 platform icon artifacts from assets/icons."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

PNG_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sync OpenQ4 icon outputs for all platforms.")
    parser.add_argument(
        "--source-root",
        default=".",
        help="Repository root (defaults to current directory).",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify that icon outputs are in sync without writing files.",
    )
    return parser.parse_args(argv[1:])


def ensure_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"required icon file not found: {path}")


def inspect_ico_sizes(path: Path) -> set[tuple[int, int]]:
    data = path.read_bytes()
    if len(data) < 6:
        raise ValueError(f"invalid ico file: {path}")

    reserved, icon_type, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or icon_type != 1:
        raise ValueError(f"invalid ico header: {path}")

    sizes: set[tuple[int, int]] = set()
    for idx in range(count):
        offset = 6 + (idx * 16)
        if offset + 16 > len(data):
            break
        width, height = data[offset], data[offset + 1]
        width = 256 if width == 0 else width
        height = 256 if height == 0 else height
        sizes.add((width, height))

    return sizes


def highest_png_source(icon_dir: Path) -> Path:
    highest: tuple[int, Path] | None = None
    for size in PNG_SIZES:
        candidate = icon_dir / f"quake4_{size}.png"
        if candidate.is_file():
            if highest is None or size > highest[0]:
                highest = (size, candidate)

    if highest is None:
        raise FileNotFoundError("no source PNG found in assets/icons")
    return highest[1]


def resize_png(src_png: Path, dst_png: Path, size: int) -> bool:
    with Image.open(src_png) as image:
        if image.mode not in ("RGBA", "LA"):
            image = image.convert("RGBA")
        resized = image.resize((size, size), Image.Resampling.LANCZOS)
        dst_png.parent.mkdir(parents=True, exist_ok=True)
        resized.save(dst_png, format="PNG")
    return True


def sync_icons(source_root: Path, check_only: bool) -> int:
    icon_dir = source_root / "assets" / "icons"
    ico_path = icon_dir / "quake4.ico"
    icns_path = icon_dir / "quake4.icns"

    ensure_file(ico_path)
    ensure_file(icns_path)

    ico_sizes = inspect_ico_sizes(ico_path)
    expected_ico_sizes = {(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)}
    missing_ico = sorted(expected_ico_sizes.difference(ico_sizes))
    if missing_ico:
        raise RuntimeError(f"assets/icons/quake4.ico is missing required sizes: {missing_ico}")

    source_png = highest_png_source(icon_dir)

    generated = 0
    pending = 0
    for size in PNG_SIZES:
        path = icon_dir / f"quake4_{size}.png"
        if path.is_file():
            continue

        if check_only:
            pending += 1
            continue

        resize_png(source_png, path, size)
        generated += 1

    if check_only and pending > 0:
        print(f"icons out of sync: missing_png={pending}")
        return 1

    print(
        "icon sync complete: "
        f"generated_png={generated}, "
        f"png_source={source_png.relative_to(source_root).as_posix()}"
    )
    return 0


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_root = Path(args.source_root).resolve()

    try:
        return sync_icons(source_root, args.check)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
