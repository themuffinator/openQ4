#!/usr/bin/env python3
"""Generate shared openQ4 version metadata for builds and packaging."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


NUMERIC_VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
IDENTIFIER_RE = re.compile(r"^[0-9A-Za-z-]+$")
DOT_IDENTIFIERS_RE = re.compile(r"^[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*$")
DATE_STAMP_RE = re.compile(r"^\d{8}$")
RELEASE_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
ISO_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute openQ4 version metadata for builds and CI."
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root (default: current directory).",
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
        "--auto-iteration",
        action="store_true",
        help=(
            "Automatically derive prerelease iteration as YYYYMMDD.N from "
            "existing published release tags for the current UTC day."
        ),
    )
    parser.add_argument(
        "--auto-iteration-date",
        default="",
        help=(
            "Optional UTC date override for --auto-iteration in YYYYMMDD form. "
            "Defaults to the current UTC date."
        ),
    )
    parser.add_argument(
        "--header-out",
        default="",
        help="Optional output path for a generated C/C++ version header.",
    )
    return parser.parse_args(argv[1:])


def read_meson_base_version(source_root: Path) -> str:
    meson_build = source_root / "meson.build"
    text = meson_build.read_text(encoding="utf-8")
    match = re.search(r"version:\s*'([^']+)'", text)
    if match is None:
        raise SystemExit(f"failed to parse project version from {meson_build}")
    return match.group(1).strip()


def find_latest_release_tag_version(source_root: Path) -> str:
    """Return the highest published v<major>.<minor>.<patch> tag, without the 'v'."""
    highest_key: tuple[int, int, int] | None = None
    highest_version = ""
    for raw_tag in run_git(source_root, "tag", "--list", "v*").splitlines():
        match = RELEASE_TAG_RE.fullmatch(raw_tag.strip())
        if match is None:
            continue
        key = tuple(int(group) for group in match.groups())
        if highest_key is None or key > highest_key:
            highest_key = key
            highest_version = raw_tag.strip()[1:]
    return highest_version


def read_base_version(source_root: Path, explicit_base_version: str) -> str:
    if explicit_base_version:
        return explicit_base_version.strip()

    # The meson.build version is only a release floor; it is not bumped for
    # every published release. Without the tag ceiling below, any build that
    # does not pin -Dversion_base_override (local builds, CI validation builds)
    # reports the stale floor instead of a version players can match against a
    # release, so bug reports arrive tagged with a version that never shipped.
    floor_version = read_meson_base_version(source_root)
    latest_release_version = find_latest_release_tag_version(source_root)
    if not latest_release_version:
        return floor_version
    if validate_base_version(latest_release_version) > validate_base_version(floor_version):
        return latest_release_version
    return floor_version


def validate_base_version(base_version: str) -> tuple[int, int, int]:
    match = NUMERIC_VERSION_RE.fullmatch(base_version)
    if match is None:
        raise SystemExit(
            "base version must be a numeric major.minor.patch string "
            f"(leading zeroes are allowed), got: {base_version!r}"
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


def resolve_auto_iteration_date(auto_iteration_date: str) -> str:
    normalized = auto_iteration_date.strip()
    if not normalized:
        return datetime.now(timezone.utc).strftime("%Y%m%d")
    if DATE_STAMP_RE.fullmatch(normalized) is None:
        raise SystemExit(
            "auto iteration date must be YYYYMMDD, "
            f"got: {normalized!r}"
        )
    try:
        datetime.strptime(normalized, "%Y%m%d")
    except ValueError as exc:
        raise SystemExit(
            "auto iteration date must be a real UTC calendar date, "
            f"got: {normalized!r}"
        ) from exc
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


def compute_auto_iteration(source_root: Path, base_version: str, track: str, auto_iteration_date: str) -> str:
    if track == "stable":
        raise SystemExit("auto iteration is only valid for prerelease tracks")

    date_stamp = resolve_auto_iteration_date(auto_iteration_date)
    tag_glob = f"{track}-{base_version}-{track}.{date_stamp}*"
    tag_pattern = re.compile(
        rf"^{re.escape(track)}-{re.escape(base_version)}-"
        rf"{re.escape(track)}\.{re.escape(date_stamp)}\.(\d+)$"
    )
    matching_tags = run_git(source_root, "tag", "--list", tag_glob)

    highest_published_build = 0
    for raw_tag in matching_tags.splitlines():
        tag = raw_tag.strip()
        match = tag_pattern.fullmatch(tag)
        if match is None:
            continue
        highest_published_build = max(highest_published_build, int(match.group(1)))

    return f"{date_stamp}.{highest_published_build + 1}"


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


def detect_version_date(source_root: Path) -> str:
    """Date this build's source is from, as YYYY-MM-DD."""
    commit_date = run_git(source_root, "log", "-1", "--format=%cd", "--date=short")
    if ISO_DATE_RE.fullmatch(commit_date):
        return commit_date

    # No usable git history (release tarball, exported tree). Prefer the
    # reproducible-build stamp over the wall clock when one is published.
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if source_date_epoch.isdigit():
        return datetime.fromtimestamp(int(source_date_epoch), timezone.utc).strftime("%Y-%m-%d")
    return datetime.now(timezone.utc).strftime("%Y-%m-%d")


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
    version_date: str,
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
            f'#define OPENQ4_VERSION_DATE "{version_date}"',
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
    if path.is_symlink():
        path.unlink()
    if path.is_file() and path.read_text(encoding="utf-8") == contents:
        return
    path.write_text(contents, encoding="utf-8")


def emit_metadata(metadata: dict[str, str | int]) -> None:
    for key, value in metadata.items():
        print(f"{key}={value}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    source_root = Path(args.source_root).resolve()
    explicit_iteration = args.iteration.strip()
    auto_iteration_date = args.auto_iteration_date.strip()
    if args.auto_iteration and explicit_iteration:
        raise SystemExit("cannot specify both --iteration and --auto-iteration")
    if auto_iteration_date and not args.auto_iteration:
        raise SystemExit("--auto-iteration-date requires --auto-iteration")

    base_version = read_base_version(source_root, args.base_version)
    major, minor, patch = validate_base_version(base_version)
    track = validate_track(args.track)
    if args.auto_iteration:
        iteration = compute_auto_iteration(
            source_root,
            base_version,
            track,
            auto_iteration_date,
        )
    else:
        iteration = validate_iteration(explicit_iteration, track)

    git_sha, git_dirty, commit_count = detect_git_metadata(source_root)
    if track == "stable" and (
        not git_sha
        or commit_count <= 0
        or run_git(source_root, "rev-parse", "--is-shallow-repository") != "false"
    ):
        raise SystemExit(
            "stable builds require complete Git history for savegame build numbers; "
            "use git fetch --unshallow or actions/checkout fetch-depth: 0"
        )
    prerelease = compose_prerelease(track, iteration)
    build_metadata = compose_build_metadata(track, git_sha, git_dirty)
    version_short, version, version_tag = compose_version_strings(
        base_version, prerelease, build_metadata
    )
    version_date = detect_version_date(source_root)
    product_version = f"openQ4 {version_short}"
    product_version_full = f"openQ4 {version}"
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
            version_date=version_date,
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
            "version_date": version_date,
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
