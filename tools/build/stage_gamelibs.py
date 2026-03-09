#!/usr/bin/env python3
"""Stage OpenQ4-GameLibs game sources into a temporary local tree."""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

OPENQ4_SUPPORT_DIRS = (
    "idlib",
    "renderer",
    "ui",
    "sys",
    "bse_api",
    "MayaImport",
)


def copy_game_sources(source_game_dir: Path, dest_game_dir: Path) -> None:
    if dest_game_dir.exists():
        shutil.rmtree(dest_game_dir)

    for path in source_game_dir.rglob("*"):
        rel = path.relative_to(source_game_dir)
        dest_path = dest_game_dir / rel
        if path.is_dir():
            dest_path.mkdir(parents=True, exist_ok=True)
            continue

        dest_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest_path)


def mirror_support_dir(source_dir: Path, dest_dir: Path) -> None:
    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    if source_dir.is_dir():
        shutil.copytree(source_dir, dest_dir)


def mirror_openq4_support_dirs(openq4_root: Path, stage_root: Path) -> None:
    source_root = openq4_root / "src"
    stage_src_root = stage_root / "src"

    for dir_name in OPENQ4_SUPPORT_DIRS:
        mirror_support_dir(source_root / dir_name, stage_src_root / dir_name)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "usage: stage_gamelibs.py <openq4-root> <gamelibs-root> <stage-root>",
            file=sys.stderr,
        )
        return 2

    openq4_root = Path(argv[1]).resolve()
    gamelibs_root = Path(argv[2]).resolve()
    stage_root = Path(argv[3]).resolve()

    source_game_dir = gamelibs_root / "src" / "game"
    if not source_game_dir.is_dir():
        print(f"error: game source directory not found: {source_game_dir}", file=sys.stderr)
        return 1

    if not openq4_root.is_dir():
        print(f"error: OpenQ4 root not found: {openq4_root}", file=sys.stderr)
        return 1

    dest_game_dir = stage_root / "src" / "game"
    copy_game_sources(source_game_dir, dest_game_dir)
    mirror_openq4_support_dirs(openq4_root, stage_root)

    print(stage_root.as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
