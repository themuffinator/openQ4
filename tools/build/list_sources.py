#!/usr/bin/env python3
"""List C++ sources for a source subtree, excluding explicit relative paths."""

from __future__ import annotations

import sys
from pathlib import Path


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip()


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(
            f"usage: {argv[0]} <source-root> <subdir> [--exclude-subdirs=sub1,sub2] [exclude-relative-path ...]",
            file=sys.stderr,
        )
        return 2

    source_root = Path(argv[1]).resolve()
    subdir = normalize(argv[2]).strip("/")
    target_root = (source_root / subdir).resolve()

    if not target_root.is_dir():
        print(f"error: source subtree not found: {target_root}", file=sys.stderr)
        return 1

    exclude_subdirs: set[str] = set()
    remaining: list[str] = []
    for arg in argv[3:]:
        if arg.startswith("--exclude-subdirs="):
            for sd in arg[len("--exclude-subdirs="):].split(","):
                sd = sd.strip()
                if sd:
                    exclude_subdirs.add(normalize(subdir + "/" + sd))
        else:
            remaining.append(arg)

    excludes = {normalize(entry) for entry in remaining}
    sources: list[str] = []

    for path in target_root.rglob("*.cpp"):
        rel = path.relative_to(source_root).as_posix()
        if rel in excludes:
            continue
        if any(rel.startswith(excl_dir + "/") for excl_dir in exclude_subdirs):
            continue
        sources.append(rel)

    for rel in sorted(sources):
        print(rel)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
