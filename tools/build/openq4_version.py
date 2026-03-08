#!/usr/bin/env python3
"""Generate shared OpenQ4 version metadata for builds and packaging."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
IDENTIFIER_RE = re.compile(r"^[0-9A-Za-z-]+$")
DOT_IDENTIFIERS_RE = re.compile(r"^[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*$")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute OpenQ4 version metadata for builds and CI."
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="OpenQ4 repository root (default: current directory).",
    )
    parser.add_argument(
        "--base-version",
        default="",
        help="Base semantic version. Defaults to parsing meson.build.",
    )
    parser.add_argument(
        "--track",
        default="dev",
        help=(
            "Release track label. Use 'stable' for release builds; other values "
            "become prerelease labels (default: dev)."
        ),
    )
    parser.add_argument(
        "--iteration",
        default="",
        help=(
            "Optional dot-separated iteration for prerelease tracks, such as "
            "'20260307.1' or '2'."
        ),
    )
    parser.add_argument(
        "--header-out",
        default="",
        help="Optional output path for a generated C/C++ version header.",
    )
    return parser.parse_args(argv[1:])


def read_base_version(source_root: Path, explicit_base_version: str) -> str:
    if explicit_base_version:
        return explicit_base_version.strip()

    meson_build = source_root / "meson.build"
    text = meson_build.read_text(encoding="utf-8")
    match = re.search(r"version:\s*'([^']+)'", text)
    if match is None:
        raise SystemExit(f"failed to parse project version from {meson_build}")
    return match.group(1).strip()


def validate_base_version(base_version: str) -> tuple[int, int, int]:
    match = SEMVER_RE.fullmatch(base_version)
    if match is None:
        raise SystemExit(
            f"base version must be semantic version major.minor.patch, got: {base_version!r}"
        )
    return tuple(int(group) for group in match.groups())


def validate_track(track: str) -> str:
    normalized = track.strip()
    if not normalized:
        raise SystemExit("version track must not be empty")
    if normalized == "stable":
        return normalized
    if IDENTIFIER_RE.fullmatch(normalized) is None:
        raise SystemExit(
            "version track must be 'stable' or a semver-safe identifier "
            f"(letters, digits, hyphen), got: {normalized!r}"
        )
    return normalized


def validate_iteration(iteration: str, track: str) -> str:
    normalized = iteration.strip()
    if not normalized:
        return ""
    if track == "stable":
        raise SystemExit("version iteration is only valid for prerelease tracks")
    if DOT_IDENTIFIERS_RE.fullmatch(normalized) is None:
        raise SystemExit(
            "version iteration must be dot-separated semver identifiers, "
            f"got: {normalized!r}"
        )
    return normalized


def run_git(source_root: Path, *git_args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(source_root), *git_args],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return completed.stdout.strip()


def detect_git_metadata(source_root: Path) -> tuple[str, bool, int]:
    short_sha = run_git(source_root, "rev-parse", "--short=8", "HEAD")
    commit_count_raw = run_git(source_root, "rev-list", "--count", "HEAD")
    dirty_raw = run_git(source_root, "status", "--porcelain", "--untracked-files=no")

    commit_count = 0
    if commit_count_raw:
        try:
            commit_count = int(commit_count_raw)
        except ValueError:
            commit_count = 0

    return short_sha, bool(dirty_raw), commit_count


def compose_prerelease(track: str, iteration: str) -> str:
    if track == "stable":
        return ""
    if not iteration:
        return track
    return track + "." + iteration


def compose_build_metadata(track: str, short_sha: str, dirty: bool) -> str:
    parts: list[str] = []
    include_git_sha = short_sha and (track != "stable" or dirty)
    if include_git_sha:
        parts.append("g" + short_sha)
    if dirty:
        parts.append("dirty")
    return ".".join(parts)


def compose_version_strings(
    base_version: str,
    prerelease: str,
    build_metadata: str,
) -> tuple[str, str, str]:
    version_short = base_version
    if prerelease:
        version_short += "-" + prerelease

    version = version_short
    if build_metadata:
        version += "+" + build_metadata

    version_tag = version.replace("+", "-")
    return version_short, version, version_tag


def compute_resource_build(commit_count: int) -> int:
    if commit_count <= 0:
        return 0
    return min(commit_count, 65535)


def generate_header_text(
    *,
    base_version: str,
    track: str,
    iteration: str,
    version_short: str,
    version: str,
    version_tag: str,
    product_version: str,
    product_version_full: str,
    major: int,
    minor: int,
    patch: int,
    resource_build: int,
    resource_commas: str,
    resource_dotted: str,
    git_sha: str,
    git_dirty: bool,
    commit_count: int,
) -> str:
    git_dirty_value = 1 if git_dirty else 0
    return "\n".join(
        [
            "#ifndef OPENQ4_VERSION_GENERATED_H",
            "#define OPENQ4_VERSION_GENERATED_H",
            "",
            f'#define OPENQ4_VERSION_BASE "{base_version}"',
            f'#define OPENQ4_VERSION_TRACK "{track}"',
            f'#define OPENQ4_VERSION_ITERATION "{iteration}"',
            f'#define OPENQ4_VERSION_SHORT "{version_short}"',
            f'#define OPENQ4_VERSION "{version}"',
            f'#define OPENQ4_VERSION_TAG "{version_tag}"',
            f'#define OPENQ4_PRODUCT_VERSION "{product_version}"',
            f'#define OPENQ4_PRODUCT_VERSION_FULL "{product_version_full}"',
            "",
            f"#define OPENQ4_VERSION_MAJOR {major}",
            f"#define OPENQ4_VERSION_MINOR {minor}",
            f"#define OPENQ4_VERSION_PATCH {patch}",
            f"#define OPENQ4_VERSION_RESOURCE_BUILD {resource_build}",
            f'#define OPENQ4_VERSION_RESOURCE_COMMAS_STRING "{resource_commas}"',
            f'#define OPENQ4_VERSION_RESOURCE_DOTTED "{resource_dotted}"',
            "",
            f'#define OPENQ4_VERSION_GIT_SHA "{git_sha}"',
            f"#define OPENQ4_VERSION_GIT_DIRTY {git_dirty_value}",
            f"#define OPENQ4_VERSION_COMMIT_COUNT {commit_count}",
            "",
            "#endif",
            "",
        ]
    )


def write_if_changed(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == contents:
        return
    path.write_text(contents, encoding="utf-8")


def emit_metadata(metadata: dict[str, str | int]) -> None:
    for key, value in metadata.items():
        print(f"{key}={value}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    source_root = Path(args.source_root).resolve()
    base_version = read_base_version(source_root, args.base_version)
    major, minor, patch = validate_base_version(base_version)
    track = validate_track(args.track)
    iteration = validate_iteration(args.iteration, track)

    git_sha, git_dirty, commit_count = detect_git_metadata(source_root)
    prerelease = compose_prerelease(track, iteration)
    build_metadata = compose_build_metadata(track, git_sha, git_dirty)
    version_short, version, version_tag = compose_version_strings(
        base_version, prerelease, build_metadata
    )
    product_version = f"OpenQ4 {version_short}"
    product_version_full = f"OpenQ4 {version}"
    resource_build = compute_resource_build(commit_count)
    resource_commas = f"{major}, {minor}, {patch}, {resource_build}"
    resource_dotted = f"{major}.{minor}.{patch}.{resource_build}"

    if args.header_out:
        header_path = Path(args.header_out).resolve()
        header_text = generate_header_text(
            base_version=base_version,
            track=track,
            iteration=iteration,
            version_short=version_short,
            version=version,
            version_tag=version_tag,
            product_version=product_version,
            product_version_full=product_version_full,
            major=major,
            minor=minor,
            patch=patch,
            resource_build=resource_build,
            resource_commas=resource_commas,
            resource_dotted=resource_dotted,
            git_sha=git_sha,
            git_dirty=git_dirty,
            commit_count=commit_count,
        )
        write_if_changed(header_path, header_text)

    emit_metadata(
        {
            "base_version": base_version,
            "track": track,
            "iteration": iteration,
            "version_short": version_short,
            "version": version,
            "version_tag": version_tag,
            "product_version": product_version,
            "product_version_full": product_version_full,
            "version_major": major,
            "version_minor": minor,
            "version_patch": patch,
            "resource_build": resource_build,
            "resource_commas": resource_commas,
            "resource_dotted": resource_dotted,
            "git_sha": git_sha,
            "git_dirty": 1 if git_dirty else 0,
            "commit_count": commit_count,
        }
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
