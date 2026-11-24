#!/usr/bin/env python3
"""Generate the source snapshot stamp used by openQ4 savegames."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}

PROJECT_SCAN_DIRS = (
    "src/framework",
    "src/sound",
    "src/ui",
)

PROJECT_ALWAYS_FILES = (
    "src/framework/BuildVersion.h",
    "src/framework/licensee.h",
    "src/idlib/Lib.h",
    "src/sys/AutoVersion.h",
)

GAME_SCAN_DIRS = ("src/game",)

RELEVANCE_TOKENS = (
    "SaveGame",
    "idSaveGame",
    "idRestoreGame",
    "WriteToSaveGame",
    "ReadFromSaveGame",
    "::Save(",
    "::Restore(",
    " Save(",
    " Restore(",
    "SAVEGAME_",
    "MAX_SAVEGAME_",
)


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def iter_source_files(root: Path, rel_dir: str) -> list[Path]:
    base = (root / rel_dir).resolve()
    if not base.is_dir():
        return []
    if not is_relative_to(base, root):
        raise RuntimeError(f"source directory escapes root: {base}")

    files: list[Path] = []
    for path in sorted(base.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"refusing to hash symlinked source path: {path}")
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        files.append(path)
    return files


def normalize_source_bytes(data: bytes) -> bytes:
    """Make the compatibility stamp independent of checkout line endings."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def read_bytes(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"expected regular file: {path}")
    return normalize_source_bytes(path.read_bytes())


def source_is_relevant(data: bytes) -> bool:
    text = data.decode("utf-8", errors="ignore")
    return any(token in text for token in RELEVANCE_TOKENS)


def collect_relevant_files(root: Path, scan_dirs: tuple[str, ...], always_files: tuple[str, ...]) -> list[tuple[str, Path, bytes]]:
    root = root.resolve()
    selected: dict[str, tuple[Path, bytes]] = {}

    for rel in always_files:
        path = (root / rel).resolve()
        if not is_relative_to(path, root):
            raise RuntimeError(f"source file escapes root: {path}")
        if path.is_file():
            selected[rel.replace("\\", "/")] = (path, read_bytes(path))

    for rel_dir in scan_dirs:
        for path in iter_source_files(root, rel_dir):
            rel = path.relative_to(root).as_posix()
            data = read_bytes(path)
            if source_is_relevant(data):
                selected[rel] = (path, data)

    return [(rel, path, data) for rel, (path, data) in sorted(selected.items())]


def build_digest(project_root: Path, game_stage_root: Path) -> tuple[str, int]:
    entries: list[tuple[str, str, bytes]] = []
    for label, root, dirs, always in (
        ("project", project_root, PROJECT_SCAN_DIRS, PROJECT_ALWAYS_FILES),
        ("gamelibs", game_stage_root, GAME_SCAN_DIRS, ()),
    ):
        for rel, _path, data in collect_relevant_files(root, dirs, always):
            entries.append((label, rel, data))

    if not entries:
        raise RuntimeError("no savegame compatibility source files were selected")

    digest = hashlib.sha256()
    for label, rel, data in entries:
        digest.update(label.encode("utf-8"))
        digest.update(b"\0")
        digest.update(rel.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(data).hexdigest().encode("ascii"))
        digest.update(b"\0")

    return digest.hexdigest(), len(entries)


def render_header(source_hash: str, file_count: int) -> str:
    return "\n".join(
        (
            "#ifndef OPENQ4_SAVEGAME_COMPAT_GENERATED_H",
            "#define OPENQ4_SAVEGAME_COMPAT_GENERATED_H",
            "",
            "#define OPENQ4_SAVEGAME_COMPAT_GENERATOR_VERSION 1",
            f'#define OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH "{source_hash}"',
            f"#define OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT {file_count}",
            "",
            "#endif",
            "",
        )
    )


def write_if_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--game-stage-root", required=True)
    parser.add_argument("--header-out", required=True)
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    game_stage_root = Path(args.game_stage_root).resolve()
    header_out = Path(args.header_out)

    try:
        source_hash, file_count = build_digest(project_root, game_stage_root)
        write_if_changed(header_out, render_header(source_hash, file_count))
    except RuntimeError as exc:
        print(f"error: {exc}", flush=True)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
