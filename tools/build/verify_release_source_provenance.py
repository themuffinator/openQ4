#!/usr/bin/env python3
"""Verify that a release build used the exact approved source commits."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from stage_gamelibs import validate_stage_manifest
from game_source_inventory import inventory


FULL_GIT_SHA_RE = re.compile(r"[0-9a-f]{40}")


def require_full_git_sha(value: str, label: str) -> str:
    value = value.strip().lower()
    if FULL_GIT_SHA_RE.fullmatch(value) is None:
        raise RuntimeError(f"{label} must be a full 40-character Git SHA: {value!r}")
    return value


def require_repository(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise RuntimeError(f"{label} is unavailable: {path}") from exc
    if not resolved.is_dir():
        raise RuntimeError(f"{label} is not a directory: {resolved}")
    return resolved


def git_value(repository: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repository), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(
            f"Git command failed for {repository}: git {' '.join(args)}"
            + (f": {detail}" if detail else "")
        )
    return completed.stdout.strip()


def verify_repository_commit(repository: Path, expected_commit: str, label: str) -> None:
    git_root = Path(git_value(repository, "rev-parse", "--show-toplevel")).resolve()
    if git_root != repository.resolve():
        raise RuntimeError(f"{label} is not the root of the checked-out repository")
    actual_commit = require_full_git_sha(
        git_value(repository, "rev-parse", "--verify", "HEAD^{commit}"),
        f"{label} checked-out commit",
    )
    if actual_commit != expected_commit:
        raise RuntimeError(
            f"{label} checked-out commit {actual_commit} does not match expected "
            f"release commit {expected_commit}"
        )

    dirty = git_value(repository, "status", "--porcelain", "--untracked-files=all")
    if dirty:
        raise RuntimeError(f"{label} checkout is dirty during release provenance validation")


def read_stage_manifest(path: Path) -> dict[str, object]:
    if path.is_symlink():
        raise RuntimeError(f"GameLibs stage manifest must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise RuntimeError(f"GameLibs stage manifest is unavailable: {path}") from exc
    if not resolved.is_file():
        raise RuntimeError(f"GameLibs stage manifest is not a regular file: {resolved}")
    try:
        manifest = json.loads(resolved.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"GameLibs stage manifest is unreadable: {resolved}") from exc
    if not isinstance(manifest, dict):
        raise RuntimeError("GameLibs stage manifest root must be an object")
    if type(manifest.get("format")) is not int or manifest.get("format") != 1:
        raise RuntimeError("GameLibs stage manifest has an unsupported format")
    return manifest


def verify_manifest_commit(
    manifest: dict[str, object],
    *,
    commit_key: str,
    dirty_key: str,
    expected_commit: str,
    label: str,
) -> None:
    raw_commit = manifest.get(commit_key)
    if not isinstance(raw_commit, str):
        raise RuntimeError(f"GameLibs stage manifest is missing {label} commit metadata")
    staged_commit = require_full_git_sha(raw_commit, f"staged {label} commit")
    if staged_commit != expected_commit:
        raise RuntimeError(
            f"staged {label} commit {staged_commit} does not match expected "
            f"release commit {expected_commit}"
        )
    if manifest.get(dirty_key) is not False:
        raise RuntimeError(f"GameLibs stage manifest does not record a clean {label} checkout")


def verify_layer_provenance(
    layer_root: Path,
    layer_stage_manifest: Path,
    expected_project_commit: str,
    expected_gamelibs_commit: str,
    expected_layer_commit: str,
) -> None:
    """A game-library layer (openQ4-game-awakening) is staged with the base game into its own
    stage, whose manifest records all three commits; each must be the approved one."""
    expected_layer_commit = require_full_git_sha(expected_layer_commit, "expected layer commit")
    layer_root = require_repository(layer_root, "game-library layer repository")
    verify_repository_commit(layer_root, expected_layer_commit, "layer")

    validate_stage_manifest(layer_stage_manifest.parent)
    manifest = read_stage_manifest(layer_stage_manifest)
    verify_manifest_commit(
        manifest,
        commit_key="projectGitCommit",
        dirty_key="projectGitDirty",
        expected_commit=expected_project_commit,
        label="openQ4 (layer stage)",
    )
    verify_manifest_commit(
        manifest,
        commit_key="gameLibsGitCommit",
        dirty_key="gameLibsGitDirty",
        expected_commit=expected_gamelibs_commit,
        label="openQ4-game (layer stage)",
    )
    layer = manifest.get("layer")
    if not isinstance(layer, dict):
        raise RuntimeError("layer stage manifest is missing its layer metadata")
    verify_manifest_commit(
        layer,
        commit_key="gitCommit",
        dirty_key="gitDirty",
        expected_commit=expected_layer_commit,
        label="layer",
    )


def verify_release_source_provenance(
    project_root: Path,
    gamelibs_root: Path | None = None,
    stage_manifest: Path | None = None,
    expected_project_commit: str | None = None,
    expected_gamelibs_commit: str | None = None,
    *,
    build_dir: Path | None = None,
    layer_root: Path | None = None,
    layer_stage_manifest: Path | None = None,
    expected_layer_commit: str | None = None,
) -> None:
    # Canonical releases have one source commit and compile these files in
    # place. Keep the historical staged verifier for old evidence fixtures.
    if gamelibs_root is None and stage_manifest is None and expected_gamelibs_commit is None:
        commit = require_full_git_sha(expected_project_commit or "", "expected openQ4 commit")
        project_root = require_repository(project_root, "openQ4 repository")
        verify_repository_commit(project_root, commit, "openQ4")
        for relative in ("src/game/LICENSE", "src/game/LICENSE", "LICENSES/QUAKE-4-SDK-EULA.rtf", "LICENSING.md"):
            path = project_root / relative
            if path.is_symlink() or not path.is_file():
                raise RuntimeError(f"missing canonical game licensing: {relative}")
        configured = read_stage_manifest((build_dir or project_root / "builddir") / "openq4_game_sources.json")
        if configured != inventory(project_root):
            raise RuntimeError("configured in-tree game source inventory differs from the clean release checkout; reconfigure and rebuild")
        if any(arg is not None for arg in (layer_root, layer_stage_manifest, expected_layer_commit)):
            raise RuntimeError("in-tree releases cannot select a separate Awakening source revision")
        return
    layer_args = (layer_root, layer_stage_manifest, expected_layer_commit)
    if any(arg is not None for arg in layer_args) and not all(arg is not None for arg in layer_args):
        raise RuntimeError("layer provenance needs the layer root, its stage manifest and the expected layer commit")
    expected_project_commit = require_full_git_sha(
        expected_project_commit, "expected openQ4 commit"
    )
    expected_gamelibs_commit = require_full_git_sha(
        expected_gamelibs_commit, "expected openQ4-game commit"
    )
    project_root = require_repository(project_root, "openQ4 repository")
    gamelibs_root = require_repository(gamelibs_root, "openQ4-game repository")

    verify_repository_commit(project_root, expected_project_commit, "openQ4")
    verify_repository_commit(gamelibs_root, expected_gamelibs_commit, "openQ4-game")

    validate_stage_manifest(stage_manifest.parent)
    manifest = read_stage_manifest(stage_manifest)
    verify_manifest_commit(
        manifest,
        commit_key="projectGitCommit",
        dirty_key="projectGitDirty",
        expected_commit=expected_project_commit,
        label="openQ4",
    )
    verify_manifest_commit(
        manifest,
        commit_key="gameLibsGitCommit",
        dirty_key="gameLibsGitDirty",
        expected_commit=expected_gamelibs_commit,
        label="openQ4-game",
    )

    if layer_root is not None:
        verify_layer_provenance(
            layer_root,
            layer_stage_manifest,
            expected_project_commit,
            expected_gamelibs_commit,
            expected_layer_commit,
        )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--gamelibs-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--stage-manifest", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--expected-project-commit", required=True)
    parser.add_argument("--expected-gamelibs-commit", help=argparse.SUPPRESS)
    parser.add_argument("--layer-root", type=Path, help="a game-library layer checkout (openQ4-game-awakening)")
    parser.add_argument("--layer-stage-manifest", type=Path, help="that layer's stage manifest")
    parser.add_argument("--expected-layer-commit", help="the approved layer commit")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        verify_release_source_provenance(
            args.project_root,
            args.gamelibs_root,
            args.stage_manifest,
            args.expected_project_commit,
            args.expected_gamelibs_commit,
            build_dir=args.build_dir,
            layer_root=args.layer_root,
            layer_stage_manifest=args.layer_stage_manifest,
            expected_layer_commit=args.expected_layer_commit,
        )
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(
        "release source provenance verified: "
        f"openQ4={args.expected_project_commit.lower()} "
        f"game sources={(args.expected_gamelibs_commit or args.expected_project_commit).lower()}"
        + (f" layer={args.expected_layer_commit.lower()}" if args.expected_layer_commit else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
