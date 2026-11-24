#!/usr/bin/env python3
"""Stage a GameLibs checkout's game sources into a temporary local tree.

usage: stage_gamelibs.py <project-root> <gamelibs-root> <stage-root> [--layer <layer-root>]
       stage_gamelibs.py --check-fresh <stage-root>

One game module, game_<arch>, serves both single-player and multiplayer, so
the stage holds one game tree: src/game of the GameLibs root, beside the
openQ4 support directories its sources include. Nothing else in the GameLibs
root is staged; a leftover src/mpgame from the retired split modules is not
part of the module and is ignored.

With --layer, a game-library layer (see game_layer.py) is staged on top: every
one of its source trees is copied into src/game/<id>/ inside the base game
tree, so layer sources include base headers exactly as base sources do. A
layer never replaces a base game file, and two of its trees may not provide
the same path. --check-fresh exits 0 when a stage still matches its recorded
sources and 3 when it is stale.

Restaging updates an existing stage in place. A staged file whose bytes still
match its source keeps its mtime, so ninja recompiles only what changed; new
and changed files are copied with a fresh mtime, and anything else in the
stage is deleted.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

import game_layer


OPENQ4_SUPPORT_DIRS = (
    "idlib",
    "renderer",
    "ui",
    "sys",
    "bse",
    "mayaimport",
)

# the one game source tree, relative to the GameLibs root and to the stage
GAME_SOURCE_DIR = Path("src") / "game"

MANIFEST_NAME = "openq4_gamelibs_stage_manifest.json"
PYTHON_BYTECODE_SUFFIXES = (".pyc", ".pyo")


def repo_git_value(root: Path, *args: str) -> str:
    if not (root / ".git").exists():
        return ""

    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def repo_git_dirty(root: Path) -> bool:
    return bool(repo_git_value(root, "status", "--porcelain"))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def iter_regular_source_files(source_dir: Path) -> list[Path]:
    files: list[Path] = []
    for path in sorted(source_dir.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"refusing to stage symlink: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise RuntimeError(f"refusing to stage non-regular file: {path}")
        relative = path.relative_to(source_dir)
        if "__pycache__" in relative.parts or path.suffix.lower() in PYTHON_BYTECODE_SUFFIXES:
            continue
        files.append(path)
    return files


def plan_regular_tree(source_dir: Path, dest_dir: Path) -> dict[Path, Path]:
    """Map the staged path of every regular file under source_dir to its source."""
    planned: dict[Path, Path] = {}
    for source_path in iter_regular_source_files(source_dir):
        rel = source_path.relative_to(source_dir)
        if any(part in ("", ".", "..") for part in rel.parts):
            raise RuntimeError(f"refusing to stage unsafe path: {source_path}")
        planned[dest_dir / rel] = source_path
    return planned


def same_file_bytes(first: Path, second: Path) -> bool:
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as first_handle, second.open("rb") as second_handle:
        while True:
            chunk = first_handle.read(1024 * 1024)
            if chunk != second_handle.read(1024 * 1024):
                return False
            if not chunk:
                return True


def is_link(entry: os.DirEntry) -> bool:
    # DirEntry.is_symlink() misses Windows junctions, which otherwise read as
    # plain directories, so check the reparse-point attribute as well
    if entry.is_symlink():
        return True
    attributes = getattr(entry.stat(follow_symlinks=False), "st_file_attributes", 0)
    return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def remove_link(entry: os.DirEntry) -> None:
    # a junction goes with rmdir; neither call touches the link's target
    if entry.is_dir(follow_symlinks=False):
        os.rmdir(entry.path)
    else:
        os.unlink(entry.path)


def sync_stage_tree(stage_root: Path, planned: dict[Path, Path], keep_dirs: list[Path] | None = None) -> list[Path]:
    """Make stage_root hold exactly the planned files and return their paths.

    planned maps each staged path to its source. A staged file that already
    holds its source's bytes is left alone, mtime included, so ninja rebuilds
    nothing that depends on it. Everything else under stage_root is deleted:
    files nothing plans, directories left empty (other than keep_dirs), and
    links, which are removed without being followed so that a restage never
    writes through one into another tree.
    """
    keep_dirs = keep_dirs or []
    wanted = {path.relative_to(stage_root).as_posix(): path for path in planned}
    keep = {path.relative_to(stage_root).as_posix() for path in keep_dirs}
    current: set[str] = set()

    def prune(directory: str, prefix: str) -> bool:
        """Delete everything unplanned in directory; True when it ends up empty."""
        with os.scandir(directory) as iterator:
            entries = list(iterator)
        empty = True
        for entry in entries:
            rel = prefix + entry.name
            if is_link(entry):
                remove_link(entry)
            elif entry.is_dir(follow_symlinks=False):
                if prune(entry.path, rel + "/") and rel not in keep:
                    os.rmdir(entry.path)
                else:
                    empty = False
            elif rel in wanted and entry.is_file(follow_symlinks=False):
                # names compare case-sensitively, so a case-only rename is
                # deleted here and staged again under its new name
                current.add(rel)
                empty = False
            else:
                os.unlink(entry.path)
        return empty

    stage_root.mkdir(parents=True, exist_ok=True)
    prune(str(stage_root), "")
    for rel, dest_path in sorted(wanted.items()):
        source_path = planned[dest_path]
        if rel in current and same_file_bytes(source_path, dest_path):
            # shutil.copy below also carries the permission bits
            if stat.S_IMODE(source_path.stat().st_mode) != stat.S_IMODE(dest_path.stat().st_mode):
                shutil.copymode(source_path, dest_path)
            continue
        dest_path.parent.mkdir(parents=True, exist_ok=True)
        # This is a generated compiler-input tree. A changed source may carry
        # a timestamp older than an existing PCH (checkout or a concurrent
        # companion edit), so the newly staged copy must get a fresh mtime.
        # Preserve permissions, but never backdate a replacement build input.
        shutil.copy(source_path, dest_path)
    for directory in keep_dirs:
        directory.mkdir(parents=True, exist_ok=True)
    return sorted(planned)


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def validate_stage_root(project_root: Path, gamelibs_root: Path, stage_root: Path) -> None:
    if stage_root.is_symlink():
        raise RuntimeError(f"refusing to stage into symlink: {stage_root}")

    project_tmp = project_root / ".tmp"
    if not is_relative_to(stage_root, project_tmp) or stage_root == project_tmp:
        raise RuntimeError(f"stage root must be under openQ4 .tmp: {stage_root}")

    if is_relative_to(project_root, stage_root) or is_relative_to(gamelibs_root, stage_root):
        raise RuntimeError(f"refusing to stage over source repository: {stage_root}")


def plan_game_sources(source_game_dir: Path, dest_game_dir: Path) -> dict[Path, Path]:
    return plan_regular_tree(source_game_dir, dest_game_dir)


def plan_project_support_dirs(project_root: Path, stage_root: Path) -> dict[Path, Path]:
    source_root = project_root / "src"
    stage_src_root = stage_root / "src"
    planned: dict[Path, Path] = {}

    for dir_name in OPENQ4_SUPPORT_DIRS:
        if (source_root / dir_name).is_dir():
            planned.update(plan_regular_tree(source_root / dir_name, stage_src_root / dir_name))
    return planned


def staged_file_manifest(stage_root: Path, staged_files: list[Path]) -> list[dict[str, str]]:
    entries: list[dict[str, str]] = []
    for path in sorted(staged_files):
        entries.append(
            {
                "path": path.relative_to(stage_root).as_posix(),
                "sha256": file_sha256(path),
            }
        )
    return entries


def write_stage_manifest(
    project_root: Path,
    gamelibs_root: Path,
    stage_root: Path,
    staged_files: list[Path],
    source_roots: list[tuple[Path, Path]] | None = None,
    layer: dict | None = None,
) -> None:
    manifest = {
        "format": 1,
        "projectRoot": project_root.as_posix(),
        "projectGitCommit": repo_git_value(project_root, "rev-parse", "--verify", "HEAD"),
        "projectGitDirty": repo_git_dirty(project_root),
        "gameLibsRoot": gamelibs_root.as_posix(),
        "gameLibsGitCommit": repo_git_value(gamelibs_root, "rev-parse", "--verify", "HEAD"),
        "gameLibsGitDirty": repo_git_dirty(gamelibs_root),
        "fileCount": len(staged_files),
        "files": staged_file_manifest(stage_root, staged_files),
    }
    if source_roots is not None:
        # every staged directory and the source it mirrors, for --check-fresh
        manifest["sourceRoots"] = [
            {"source": source.as_posix(), "staged": staged.relative_to(stage_root).as_posix()}
            for source, staged in source_roots
        ]
    if layer is not None:
        manifest["layer"] = {
            "id": layer["id"],
            "gameDir": layer["gameDir"],
            "root": layer["root"].as_posix(),
            "gitCommit": repo_git_value(layer["root"], "rev-parse", "--verify", "HEAD"),
            "gitDirty": repo_git_dirty(layer["root"]),
        }

    manifest_path = stage_root / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def plan_layer_sources(
    layer: dict, stage_root: Path, base_files: dict[Path, Path]
) -> tuple[dict[Path, Path], list[tuple[Path, Path]], list[Path]]:
    """Plan a layer's source trees into src/game/<id>/ of the stage.

    Every layer source tree feeds the one game module, so all of them land in
    that one directory and a path two trees both provide is refused. A layer
    only ever adds files, so no staged base game file may sit in its
    directory either. The stage persists between runs and the layer directory
    exists from the last one, so that check runs against the base files being
    staged (base_files), never against the stage on disk. Returns the layer's
    planned files, its source roots, and its directory, which is kept even
    while the layer's trees hold no files.
    """
    layer_id = layer["id"]
    layer_rel = (GAME_SOURCE_DIR / layer_id).as_posix()
    dest_root = stage_root / GAME_SOURCE_DIR / layer_id
    if any(is_relative_to(path, dest_root) for path in base_files):
        raise RuntimeError(f"the base game sources already have {layer_rel}; layer '{layer_id}' would replace it")

    planned: dict[Path, Path] = {}
    roots: list[tuple[Path, Path]] = []
    claimed: dict[Path, str] = {}
    for tree, source_dir in layer["sources"].items():
        for dest_path, source_path in plan_regular_tree(source_dir, dest_root).items():
            if dest_path in claimed:
                rel = dest_path.relative_to(dest_root).as_posix()
                raise RuntimeError(
                    f"layer '{layer_id}' provides {layer_rel}/{rel} "
                    f"from both '{claimed[dest_path]}' and '{tree}'"
                )
            claimed[dest_path] = tree
            planned[dest_path] = source_path
        roots.append((source_dir, dest_root))
    return planned, roots, [dest_root]


def check_stage_fresh(stage_root: Path) -> bool:
    """True while every recorded source root still matches its staged copy.

    Needs a manifest written with source roots; any stage without them, or any
    difference in the set of files or their contents, reads as stale.
    """
    manifest_path = stage_root / MANIFEST_NAME
    if manifest_path.is_symlink() or not manifest_path.is_file():
        return False
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    roots = manifest.get("sourceRoots")
    files = manifest.get("files")
    if manifest.get("format") != 1 or not isinstance(roots, list) or not isinstance(files, list):
        return False
    recorded = {entry.get("path"): entry.get("sha256") for entry in files if isinstance(entry, dict)}

    expected: dict[str, Path] = {}
    for root in roots:
        if not isinstance(root, dict) or not isinstance(root.get("source"), str) or not isinstance(root.get("staged"), str):
            return False
        source_dir = Path(root["source"])
        if not source_dir.is_dir():
            return False
        try:
            source_files = iter_regular_source_files(source_dir)
        except RuntimeError:
            return False
        for source_path in source_files:
            rel = (Path(root["staged"]) / source_path.relative_to(source_dir)).as_posix()
            if rel in expected:
                # two sources for one staged file (say a layer's shared and game
                # trees): restaging reports the conflict
                return False
            expected[rel] = source_path

    staged_under_roots = {
        rel for rel in recorded
        if any(rel.startswith(root["staged"] + "/") for root in roots)
    }
    if set(expected) != staged_under_roots:
        return False
    for rel, source_path in expected.items():
        staged_path = stage_root / rel
        if not staged_path.is_file() or file_sha256(source_path) != recorded[rel] or file_sha256(staged_path) != recorded[rel]:
            return False
    return True


def validate_stage_manifest(stage_root: Path) -> None:
    stage_root = stage_root.resolve()
    manifest_path = stage_root / MANIFEST_NAME
    if manifest_path.is_symlink():
        raise RuntimeError(f"stage manifest must not be a symlink: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != 1:
        raise RuntimeError("stage manifest has unsupported format")
    files = manifest.get("files")
    file_count = manifest.get("fileCount")
    if not isinstance(files, list) or not isinstance(file_count, int) or isinstance(file_count, bool) or file_count != len(files):
        raise RuntimeError("stage manifest file count is inconsistent")

    for entry in files:
        rel = entry.get("path")
        expected_hash = entry.get("sha256")
        if not isinstance(rel, str) or not isinstance(expected_hash, str):
            raise RuntimeError("stage manifest contains malformed file entry")
        if re.fullmatch(r"[0-9a-f]{64}", expected_hash) is None:
            raise RuntimeError(f"stage manifest contains malformed sha256 for {rel}")

        rel_path = Path(rel)
        if not rel_path.parts or rel_path.is_absolute() or any(part in ("", ".", "..") for part in rel_path.parts):
            raise RuntimeError(f"stage manifest contains unsafe path: {rel}")

        path = (stage_root / rel_path).resolve()
        if not is_relative_to(path, stage_root):
            raise RuntimeError(f"stage manifest path escapes stage root: {rel}")
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"stage manifest references missing file: {rel}")
        actual_hash = file_sha256(path)
        if actual_hash != expected_hash:
            raise RuntimeError(f"stage manifest hash mismatch for {rel}")


def prepare_stage_root(stage_root: Path) -> None:
    if stage_root.exists() and not stage_root.is_dir():
        raise RuntimeError(f"stage root is not a directory: {stage_root}")
    stage_root.mkdir(parents=True, exist_ok=True)
    # The stage is updated in place, so drop the old manifest first: a restage
    # that fails part way must not leave one vouching for a half-updated stage.
    manifest_path = stage_root / MANIFEST_NAME
    if manifest_path.is_symlink() or manifest_path.is_file():
        manifest_path.unlink()


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "--check-fresh":
        return 0 if check_stage_fresh(Path(argv[2]).resolve()) else 3

    layer_arg = None
    if len(argv) == 6 and argv[4] == "--layer":
        layer_arg = argv[5]
        argv = argv[:4]
    if len(argv) != 4:
        print(
            "usage: stage_gamelibs.py <project-root> <gamelibs-root> <stage-root> [--layer <layer-root>]\n"
            "       stage_gamelibs.py --check-fresh <stage-root>",
            file=sys.stderr,
        )
        return 2

    raw_project_root = Path(argv[1])
    raw_gamelibs_root = Path(argv[2])
    if raw_project_root.is_symlink():
        print(f"error: openQ4 root must not be a symlink: {raw_project_root}", file=sys.stderr)
        return 1
    if raw_gamelibs_root.is_symlink():
        print(f"error: GameLibs root must not be a symlink: {raw_gamelibs_root}", file=sys.stderr)
        return 1

    project_root = raw_project_root.resolve()
    gamelibs_root = raw_gamelibs_root.resolve()
    raw_stage_root = Path(argv[3])
    if raw_stage_root.is_symlink():
        print(f"error: refusing to stage into symlink: {raw_stage_root}", file=sys.stderr)
        return 1
    stage_root = raw_stage_root.resolve()

    source_game_dir = gamelibs_root / GAME_SOURCE_DIR
    if not source_game_dir.is_dir():
        print(f"error: game source directory not found: {source_game_dir}", file=sys.stderr)
        return 1

    if not project_root.is_dir():
        print(f"error: openQ4 root not found: {project_root}", file=sys.stderr)
        return 1

    try:
        layer = game_layer.load_layer(Path(layer_arg)) if layer_arg is not None else None
        validate_stage_root(project_root, gamelibs_root, stage_root)
        if layer is not None and is_relative_to(layer["root"], stage_root):
            raise RuntimeError(f"refusing to stage over source repository: {stage_root}")
        # plan the whole stage before touching it, so a refused source leaves
        # the previous stage and its manifest as they were
        stage_game_dir = stage_root / GAME_SOURCE_DIR
        planned: dict[Path, Path] = plan_game_sources(source_game_dir, stage_game_dir)
        source_roots: list[tuple[Path, Path]] = [(source_game_dir, stage_game_dir)]
        planned.update(plan_project_support_dirs(project_root, stage_root))
        for dir_name in OPENQ4_SUPPORT_DIRS:
            if (project_root / "src" / dir_name).is_dir():
                source_roots.append((project_root / "src" / dir_name, stage_root / "src" / dir_name))
        keep_dirs: list[Path] = []
        if layer is not None:
            layer_files, layer_roots, keep_dirs = plan_layer_sources(layer, stage_root, planned)
            planned.update(layer_files)
            source_roots += layer_roots
        prepare_stage_root(stage_root)
        staged_files = sync_stage_tree(stage_root, planned, keep_dirs)
        write_stage_manifest(project_root, gamelibs_root, stage_root, staged_files, source_roots, layer)
        validate_stage_manifest(stage_root)
    except (RuntimeError, game_layer.LayerError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(stage_root.as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
