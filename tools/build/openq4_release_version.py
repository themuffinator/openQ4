#!/usr/bin/env python3
"""Compute the next OpenQ4 manual release version."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
RELEASE_TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")


@dataclass(frozen=True)
class ReleaseVersion:
    major: int
    minor: int
    serial: int
    serial_width: int
    text: str

    @property
    def key(self) -> tuple[int, int, int]:
        return (self.major, self.minor, self.serial)

    def format(self, *, minor: int | None = None, serial: int | None = None) -> str:
        rendered_minor = self.minor if minor is None else minor
        rendered_serial = self.serial if serial is None else serial
        rendered_width = max(self.serial_width, len(str(rendered_serial)))
        return f"{self.major}.{rendered_minor}.{rendered_serial:0{rendered_width}d}"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute the next manual OpenQ4 release version."
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="OpenQ4 repository root (default: current directory).",
    )
    parser.add_argument(
        "--current-version",
        default="",
        help="Current configured release version. Defaults to parsing meson.build.",
    )
    parser.add_argument(
        "--version-override",
        default="",
        help="Optional explicit release version that bypasses automatic bump selection.",
    )
    parser.add_argument(
        "--bump-mode",
        choices=("auto", "serial", "minor"),
        default="auto",
        help="Release bump mode (default: auto).",
    )
    return parser.parse_args(argv[1:])


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


def read_project_version(source_root: Path, explicit_current_version: str) -> str:
    if explicit_current_version.strip():
        return explicit_current_version.strip()

    meson_build = source_root / "meson.build"
    text = meson_build.read_text(encoding="utf-8")
    match = re.search(r"version:\s*'([^']+)'", text)
    if match is None:
        raise SystemExit(f"failed to parse project version from {meson_build}")
    return match.group(1).strip()


def parse_version(version_text: str) -> ReleaseVersion:
    normalized = version_text.strip()
    match = VERSION_RE.fullmatch(normalized)
    if match is None:
        raise SystemExit(
            "release version must be a numeric major.minor.patch string "
            f"(for example 0.1.010), got: {version_text!r}"
        )

    major_raw, minor_raw, serial_raw = match.groups()
    return ReleaseVersion(
        major=int(major_raw),
        minor=int(minor_raw),
        serial=int(serial_raw),
        serial_width=max(3, len(serial_raw)),
        text=normalized,
    )


def list_release_tags(source_root: Path) -> list[tuple[str, ReleaseVersion]]:
    output = run_git(source_root, "tag", "--list", "v*")
    tags: list[tuple[str, ReleaseVersion]] = []
    for raw_tag in output.splitlines():
        tag = raw_tag.strip()
        if not tag:
            continue
        match = RELEASE_TAG_RE.fullmatch(tag)
        if match is None:
            continue
        tags.append((tag, parse_version(match.group(1))))
    return tags


def find_latest_release_tag(source_root: Path) -> tuple[str, ReleaseVersion] | None:
    release_tags = list_release_tags(source_root)
    if not release_tags:
        return None
    return max(release_tags, key=lambda item: item[1].key)


def count_commits(source_root: Path, range_spec: str) -> int:
    output = run_git(source_root, "rev-list", "--count", range_spec)
    if not output:
        return 0
    try:
        return int(output)
    except ValueError:
        return 0


def collect_changed_files(source_root: Path, range_spec: str) -> list[str]:
    output = run_git(source_root, "diff", "--name-only", range_spec)
    if not output:
        return []
    return [line.strip() for line in output.splitlines() if line.strip()]


def collect_diff_totals(source_root: Path, range_spec: str) -> tuple[int, int]:
    output = run_git(source_root, "diff", "--numstat", range_spec)
    additions = 0
    deletions = 0
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split("\t", 2)
        if len(parts) < 2:
            continue
        added_raw, deleted_raw = parts[0], parts[1]
        if added_raw.isdigit():
            additions += int(added_raw)
        if deleted_raw.isdigit():
            deletions += int(deleted_raw)
    return additions, deletions


CATEGORY_RULES: tuple[tuple[str, str, int, bool], ...] = (
    ("src/framework/", "engine", 4, True),
    ("src/renderer/", "renderer", 4, True),
    ("src/bse/", "bse", 4, True),
    ("src/game/", "game", 4, True),
    ("src/script/", "scripting", 3, True),
    ("src/ui/", "ui", 3, True),
    ("src/sys/", "platform", 3, True),
    ("content/baseoq4/maps/", "maps", 3, True),
    ("content/baseoq4/guis/", "ui-content", 2, False),
    ("content/baseoq4/def/", "defs", 2, False),
    ("content/baseoq4/strings/", "localization", 1, False),
    ("content/baseoq4/", "content", 1, False),
    ("tools/build/", "build", 2, False),
    (".github/workflows/", "workflow", 2, False),
    ("meson.build", "build", 2, False),
    ("meson_options.txt", "build", 2, False),
    ("subprojects/", "dependencies", 2, False),
)

DOC_PREFIXES = (
    "docs/",
    "docs-dev/",
    "assets/",
)

DOC_FILES = {
    "README.md",
    "BUILDING.md",
    "TECHNICAL.md",
    "AGENTS.md",
    "CHANGELOG.md",
}


def classify_path(path: str) -> tuple[str, int, bool]:
    for prefix, category, weight, major_subsystem in CATEGORY_RULES:
        if prefix.endswith("/"):
            if path.startswith(prefix):
                return category, weight, major_subsystem
        elif path == prefix:
            return category, weight, major_subsystem

    if path in DOC_FILES or path.startswith(DOC_PREFIXES):
        return "docs", 0, False

    return "misc", 1, False


def analyze_change_scale(
    changed_files: list[str],
    *,
    commit_count: int,
    additions: int,
    deletions: int,
) -> tuple[str, str, int]:
    if not changed_files:
        return "serial", "No changed files were detected since the previous release tag.", 0

    unique_categories: dict[str, int] = {}
    major_subsystems: set[str] = set()
    docs_only = True
    for path in changed_files:
        category, weight, is_major = classify_path(path)
        unique_categories[category] = max(unique_categories.get(category, 0), weight)
        if weight > 0:
            docs_only = False
        if is_major:
            major_subsystems.add(category)

    if docs_only:
        return "serial", "Only documentation and packaging metadata changed since the previous release.", 0

    score = sum(unique_categories.values())
    file_count = len(changed_files)
    churn = additions + deletions

    if file_count >= 10:
        score += 1
    if file_count >= 25:
        score += 2
    if file_count >= 60:
        score += 2

    if churn >= 400:
        score += 1
    if churn >= 1500:
        score += 2
    if churn >= 5000:
        score += 3

    if commit_count >= 10:
        score += 1
    if commit_count >= 25:
        score += 2

    if len(major_subsystems) >= 3:
        score += 2

    category_summary = ", ".join(sorted(category for category in unique_categories if category != "docs"))
    reason = (
        f"Changed {file_count} files across {category_summary or 'misc'} with "
        f"{commit_count} commits and {churn} lines of diff."
    )

    if score >= 12 or (len(major_subsystems) >= 3 and churn >= 1200):
        return "minor", reason, score
    return "serial", reason, score


def emit_metadata(metadata: dict[str, str | int]) -> None:
    for key, value in metadata.items():
        print(f"{key}={value}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_root = Path(args.source_root).resolve()

    current_version = parse_version(
        read_project_version(source_root, args.current_version)
    )

    version_override = args.version_override.strip()
    if version_override:
        override_version = parse_version(version_override)
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": "",
                "latest_release_version": "",
                "version": override_version.text,
                "version_tag": override_version.text,
                "release_tag": f"v{override_version.text}",
                "release_name": f"OpenQ4 {override_version.text}",
                "release_scale": "override",
                "release_reason": "Version override was supplied manually.",
                "bump_mode": args.bump_mode,
                "analysis_score": 0,
                "commit_count": 0,
                "changed_files": 0,
                "added_lines": 0,
                "deleted_lines": 0,
            }
        )
        return 0

    latest_release = find_latest_release_tag(source_root)
    if latest_release is None:
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": "",
                "latest_release_version": "",
                "version": current_version.text,
                "version_tag": current_version.text,
                "release_tag": f"v{current_version.text}",
                "release_name": f"OpenQ4 {current_version.text}",
                "release_scale": "floor",
                "release_reason": (
                    f"No published v* release tag was found, so the configured version "
                    f"{current_version.text} becomes the first manual release."
                ),
                "bump_mode": args.bump_mode,
                "analysis_score": 0,
                "commit_count": 0,
                "changed_files": 0,
                "added_lines": 0,
                "deleted_lines": 0,
            }
        )
        return 0

    latest_tag, latest_version = latest_release
    if current_version.key > latest_version.key:
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": latest_tag,
                "latest_release_version": latest_version.text,
                "version": current_version.text,
                "version_tag": current_version.text,
                "release_tag": f"v{current_version.text}",
                "release_name": f"OpenQ4 {current_version.text}",
                "release_scale": "floor",
                "release_reason": (
                    f"The configured repo version {current_version.text} is ahead of "
                    f"the latest published release {latest_tag}, so the repo version is used."
                ),
                "bump_mode": args.bump_mode,
                "analysis_score": 0,
                "commit_count": 0,
                "changed_files": 0,
                "added_lines": 0,
                "deleted_lines": 0,
            }
        )
        return 0

    base_version = latest_version
    range_spec = f"{latest_tag}..HEAD"
    commit_count = count_commits(source_root, range_spec)
    changed_files = collect_changed_files(source_root, range_spec)
    additions, deletions = collect_diff_totals(source_root, range_spec)

    if args.bump_mode == "auto" and commit_count == 0:
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": latest_tag,
                "latest_release_version": latest_version.text,
                "version": latest_version.text,
                "version_tag": latest_version.text,
                "release_tag": latest_tag,
                "release_name": f"OpenQ4 {latest_version.text}",
                "release_scale": "current",
                "release_reason": (
                    f"No commits were found since {latest_tag}, so the existing release version is reused."
                ),
                "bump_mode": args.bump_mode,
                "analysis_score": 0,
                "commit_count": 0,
                "changed_files": 0,
                "added_lines": 0,
                "deleted_lines": 0,
            }
        )
        return 0

    if args.bump_mode == "minor":
        release_scale = "minor"
        release_reason = "Minor bump was selected manually."
        analysis_score = 0
    elif args.bump_mode == "serial":
        release_scale = "serial"
        release_reason = "Serial bump was selected manually."
        analysis_score = 0
    else:
        release_scale, release_reason, analysis_score = analyze_change_scale(
            changed_files,
            commit_count=commit_count,
            additions=additions,
            deletions=deletions,
        )

    if release_scale == "minor":
        next_version = base_version.format(
            minor=base_version.minor + 1,
            serial=0,
        )
    else:
        next_version = base_version.format(serial=base_version.serial + 1)

    emit_metadata(
        {
            "current_version": current_version.text,
            "latest_release_tag": latest_tag,
            "latest_release_version": latest_version.text,
            "version": next_version,
            "version_tag": next_version,
            "release_tag": f"v{next_version}",
            "release_name": f"OpenQ4 {next_version}",
            "release_scale": release_scale,
            "release_reason": release_reason,
            "bump_mode": args.bump_mode,
            "analysis_score": analysis_score,
            "commit_count": commit_count,
            "changed_files": len(changed_files),
            "added_lines": additions,
            "deleted_lines": deletions,
        }
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
