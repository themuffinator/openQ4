#!/usr/bin/env python3
"""Generate the openQ4 runtime PK4 integrity header from the content pack table."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openq4_pak import (
    OPENQ4_CONTENT_PACKS,
    OPENQ4_PACK_NAMES,
    format_openq4_paks_header,
    inspect_game_pk4,
    write_text_if_changed,
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate the openQ4 engine pack table header.")
    parser.add_argument(
        "--pack",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="A built pack, for example pak0.pk4=builddir/pak0.pk4; give every pack in tools/build/content_packs.json.",
    )
    parser.add_argument("--header-out", required=True, help="Generated C/C++ header path.")
    return parser.parse_args(argv[1:])


def require_pk4_input(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    resolved = path.resolve()
    if not resolved.is_file():
        raise RuntimeError(f"{label} not found: {path}")
    return resolved


def parse_pack_arguments(values: list[str]) -> dict[str, Path]:
    packs: dict[str, Path] = {}
    for value in values:
        name, separator, raw_path = value.partition("=")
        if not separator or not raw_path:
            raise RuntimeError(f"--pack expects NAME=PATH, got {value!r}")
        if name not in OPENQ4_PACK_NAMES:
            raise RuntimeError(f"--pack names {name}, which is not in the content pack table")
        if name in packs:
            raise RuntimeError(f"--pack repeats {name}")
        packs[name] = Path(raw_path)
    return packs


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        pack_paths = parse_pack_arguments(args.pack)
        results = {
            spec.name: inspect_game_pk4(require_pk4_input(pack_paths[spec.name], spec.name), pak_name=spec.name)
            for spec in OPENQ4_CONTENT_PACKS
            if spec.name in pack_paths
        }
        header = format_openq4_paks_header(results)
    except (KeyError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    for name, result in results.items():
        if result.missing_required:
            print(f"error: {name} is missing required runtime files: {', '.join(result.missing_required)}", file=sys.stderr)
            return 1

    write_text_if_changed(Path(args.header_out).resolve(), header)
    summary = " ".join(f"{Path(name).stem}={result.md5_hex}" for name, result in results.items())
    print(f"generated openq4_paks_generated.h: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
