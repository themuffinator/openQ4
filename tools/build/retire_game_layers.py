#!/usr/bin/env python3
"""Remove obsolete openQ4 game-library outputs after staging the unified module.

Two kinds of output are retired. The q4xbase layer directory loses its old
layer modules and mod.json, and baseoq4 loses the split game-sp_<arch> and
game-mp_<arch> modules that the single game_<arch> module replaced.

Only known build artifacts are removed. Loose content, PK4s, saves and user
configuration in q4xbase are preserved. Links are never followed or removed.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

from game_layer import RETIRED_GAME_MODULE_STEMS

RETIRED_GAME_MODULE_ARTIFACTS = {
    f"{stem}_{arch}.{extension}"
    for stem in RETIRED_GAME_MODULE_STEMS
    for arch in ("x86", "x64", "arm64", "universal2")
    for extension in ("dll", "so", "dylib", "pdb", "lib", "exp")
}
ARTIFACTS = {"mod.json"} | RETIRED_GAME_MODULE_ARTIFACTS
# Android stages its native libraries, prefixed "lib", in <stage>/lib.
RETIRED_ANDROID_GAME_MODULE_ARTIFACTS = {
    f"lib{stem}_{arch}.so" for stem in RETIRED_GAME_MODULE_STEMS for arch in ("x64", "arm64")
}


def _is_link(path: Path) -> bool:
    return path.is_symlink() or bool(path.exists() and getattr(path.stat(follow_symlinks=False), "st_file_attributes", 0) & 0x400)


def _remove_known_files(target: Path, names: set[str]) -> list[str]:
    removed = []
    for name in sorted(names):
        path = target / name
        if _is_link(path):
            raise ValueError(f"refusing layer cleanup of a link: {path}")
        if path.is_file():
            path.unlink()
            removed.append(str(path))
    return removed


def _checked_target(root: Path, name: str) -> Path | None:
    target = root.absolute() / name
    for path in (target, *target.parents):
        if _is_link(path):
            raise ValueError(f"refusing layer cleanup through a link: {path}")
    if not target.exists():
        return None
    if not target.is_dir():
        raise ValueError(f"obsolete layer path is not a directory: {target}")
    return target


def retire(root: Path) -> list[str]:
    target = _checked_target(root, "q4xbase")
    if target is None:
        return []
    removed = _remove_known_files(target, ARTIFACTS)
    if not any(target.iterdir()):
        target.rmdir()
    return removed


def retire_split_game_modules(root: Path) -> list[str]:
    """Remove the retired game-sp/game-mp modules; the directories themselves stay.

    Desktop stages keep them in baseoq4, an Android stage in lib.
    """
    removed = []
    for name, artifacts in (("baseoq4", RETIRED_GAME_MODULE_ARTIFACTS), ("lib", RETIRED_ANDROID_GAME_MODULE_ARTIFACTS)):
        target = _checked_target(root, name)
        if target is not None:
            removed += _remove_known_files(target, artifacts)
    return removed


def main() -> None:
    # Meson supplies the absolute runtime directory; apply DESTDIR if set. This helper
    # is only an install hook, never a recursive cleanup of an asset source.
    value = sys.argv[1] if len(sys.argv) == 2 else ""
    if not value or not Path(value).is_absolute():
        raise ValueError("Meson must supply the absolute runtime staging directory")
    root = Path(value)
    if os.environ.get("DESTDIR"):
        root = Path(os.environ["DESTDIR"]) / root.relative_to(root.anchor)
    for path in retire(root):
        print(f"Removed obsolete layer output: {path}")
    for path in retire_split_game_modules(root):
        print(f"Removed retired split game module: {path}")


if __name__ == "__main__":
    main()
