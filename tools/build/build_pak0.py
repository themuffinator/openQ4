#!/usr/bin/env python3
"""Build every openQ4 runtime pack in the content pack table and the generated integrity header.

Meson builds each pack as its own target; this standalone builder does the same
work in one process for scripts and tests that run without a Meson build.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openq4_pak import (
    OPENQ4_CONTENT_PACKS,
    copy_file_if_changed,
    create_game_pk4,
    format_openq4_paks_header,
    write_text_if_changed,
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the openQ4 runtime packs listed in tools/build/content_packs.json and the generated checksum header."
    )
    parser.add_argument("--source-root", required=True, help="Repository root holding each pack's source directory.")
    parser.add_argument("--out-dir", required=True, help="Directory that receives the built packs.")
    parser.add_argument("--header-out", required=True, help="Generated C/C++ header path.")
    parser.add_argument(
        "--stage-dir",
        default="",
        help="Optional baseoq4 directory that also receives a copy of every pack, for direct builddir launches.",
    )
    return parser.parse_args(argv[1:])


def copy_if_changed(source: Path, destination: Path) -> None:
    copy_file_if_changed(source, destination)


def require_pack_source_dir(path: Path, pak_name: str) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{pak_name} source directory must not be a symlink: {path}")
    resolved = path.resolve()
    if not resolved.is_dir():
        raise RuntimeError(f"{pak_name} source directory not found: {resolved}")
    return resolved


def build_pack(pak_name: str, source_dir: Path, pak_out: Path) -> object:
    result = create_game_pk4(source_dir, pak_out, pak_name=pak_name)
    if result.added_files == 0:
        print(f"error: {pak_name} packaging found no eligible files after filtering", file=sys.stderr)
        raise SystemExit(1)
    if result.missing_required:
        print(f"error: {pak_name} packaging is missing required runtime files:", file=sys.stderr)
        for relative_path in result.missing_required:
            print(f"  - {relative_path}", file=sys.stderr)
        raise SystemExit(1)
    return result


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_root = Path(args.source_root)
    out_dir = Path(args.out_dir).resolve()

    try:
        sources = {
            spec.name: require_pack_source_dir(source_root / spec.source, spec.name) for spec in OPENQ4_CONTENT_PACKS
        }
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    results = {}
    for spec in OPENQ4_CONTENT_PACKS:
        results[spec.name] = build_pack(spec.name, sources[spec.name], out_dir / spec.name)

    write_text_if_changed(Path(args.header_out).resolve(), format_openq4_paks_header(results))

    if args.stage_dir:
        stage_dir = Path(args.stage_dir).resolve()
        for spec in OPENQ4_CONTENT_PACKS:
            copy_if_changed(out_dir / spec.name, stage_dir / spec.name)

    for name, result in results.items():
        print(f"built {name}: md5={result.md5_hex} size={result.size_bytes} files={result.added_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
