#!/usr/bin/env python3
"""Compute the next openQ4 manual release version."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from openq4_release_status import is_withdrawn_release, require_available_release


VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
RELEASE_TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")
BUMP_MODE_ALIASES = {
    "auto": "auto",
    "major": "major",
    "major (x..)": "major",
    "minor": "minor",
    "minor (.x.)": "minor",
    "patch": "patch",
    "patch (..x)": "patch",
    "serial": "patch",
    "serial (..x)": "patch",
}


@dataclass(frozen=True)
class ReleaseVersion:
    major: int
    minor: int
    patch: int
    patch_width: int
    text: str

    @property
    def key(self) -> tuple[int, int, int]:
        return (self.major, self.minor, self.patch)

    def format(
        self,
        *,
        major: int | None = None,
        minor: int | None = None,
        patch: int | None = None,
    ) -> str:
        rendered_major = self.major if major is None else major
        rendered_minor = self.minor if minor is None else minor
        rendered_patch = self.patch if patch is None else patch
        rendered_width = max(self.patch_width, len(str(rendered_patch)))
        return f"{rendered_major}.{rendered_minor}.{rendered_patch:0{rendered_width}d}"


def parse_bump_mode(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in BUMP_MODE_ALIASES:
        raise argparse.ArgumentTypeError(
            "bump mode must be one of: auto, major (x..), minor (.x.), patch (..x)"
        )
    return BUMP_MODE_ALIASES[normalized]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute the next manual openQ4 release version."
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="openQ4 repository root (default: current directory).",
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
        type=parse_bump_mode,
        default="auto",
        help="Release bump mode: auto, major (x..), minor (.x.), or patch (..x).",
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

    major_raw, minor_raw, patch_raw = match.groups()
    return ReleaseVersion(
        major=int(major_raw),
        minor=int(minor_raw),
        patch=int(patch_raw),
        patch_width=max(3, len(patch_raw)),
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
        version = parse_version(match.group(1))
        if not is_withdrawn_release(version.key):
            tags.append((tag, version))
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
    ("content/baseoq4/pak1/env/", "maps", 3, True),
    ("content/baseoq4/pak2/gfx/guis/loadscreens/", "maps", 3, False),
    ("content/baseoq4/pak1/maps/", "maps", 3, True),
    ("content/baseoq4/pak0/maps/", "maps", 3, True),
    ("content/baseoq4/pak0/guis/", "ui-content", 2, False),
    ("content/baseoq4/pak0/def/", "defs", 2, False),
    ("content/baseoq4/pak0/strings/", "localization", 1, False),
    ("content/baseoq4/", "content", 1, False),
    ("assets/docs/", "docs", 0, False),
    ("assets/icons/", "packaging", 2, False),
    ("assets/linux/", "platform", 2, False),
    ("assets/release/", "packaging", 2, False),
    ("assets/splash/", "packaging", 2, False),
    ("assets/img/", "packaging", 1, False),
    ("tools/build/", "build", 2, False),
    (".github/workflows/", "workflow", 2, False),
    ("meson.build", "build", 2, False),
    ("meson_options.txt", "build", 2, False),
    ("subprojects/", "dependencies", 2, False),
)

DOC_PREFIXES = (
    "docs/",
    "docs/user/",
    "docs/dev/",
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
        return "patch", "No changed files were detected since the previous release tag.", 0

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
        return "patch", "Only documentation and packaging metadata changed since the previous release.", 0

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
    return "patch", reason, score


def emit_metadata(metadata: dict[str, str | int]) -> None:
    for key, value in metadata.items():
        print(f"{key}={value}")


def validate_version_override(
    override_version: ReleaseVersion,
    current_version: ReleaseVersion,
    latest_release: tuple[str, ReleaseVersion] | None,
) -> None:
    require_available_release(override_version.key)
    if override_version.key < current_version.key:
        raise SystemExit(
            "version override must not be lower than the configured project version "
            f"{current_version.text}, got: {override_version.text}"
        )

    if latest_release is None:
        return

    latest_tag, latest_version = latest_release
    if override_version.key < latest_version.key:
        raise SystemExit(
            "version override must not be lower than the latest published release "
            f"{latest_tag} ({latest_version.text}), got: {override_version.text}"
        )


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_root = Path(args.source_root).resolve()

    current_version = parse_version(
        read_project_version(source_root, args.current_version)
    )
    require_available_release(current_version.key)
    latest_release = find_latest_release_tag(source_root)

    version_override = args.version_override.strip()
    if version_override:
        override_version = parse_version(version_override)
        validate_version_override(override_version, current_version, latest_release)
        latest_tag = latest_release[0] if latest_release is not None else ""
        latest_version = latest_release[1].text if latest_release is not None else ""
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": latest_tag,
                "latest_release_version": latest_version,
                "version": override_version.text,
                "version_tag": override_version.text,
                "release_tag": f"v{override_version.text}",
                "release_name": f"openQ4 {override_version.text}",
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

    if latest_release is None:
        emit_metadata(
            {
                "current_version": current_version.text,
                "latest_release_tag": "",
                "latest_release_version": "",
                "version": current_version.text,
                "version_tag": current_version.text,
                "release_tag": f"v{current_version.text}",
                "release_name": f"openQ4 {current_version.text}",
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
                "release_name": f"openQ4 {current_version.text}",
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
                "release_name": f"openQ4 {latest_version.text}",
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

    if args.bump_mode == "major":
        release_scale = "major"
        release_reason = "Major bump was selected manually."
        analysis_score = 0
    elif args.bump_mode == "minor":
        release_scale = "minor"
        release_reason = "Minor bump was selected manually."
        analysis_score = 0
    elif args.bump_mode == "patch":
        release_scale = "patch"
        release_reason = "Patch bump was selected manually."
        analysis_score = 0
    else:
        release_scale, release_reason, analysis_score = analyze_change_scale(
            changed_files,
            commit_count=commit_count,
            additions=additions,
            deletions=deletions,
        )

    if release_scale == "major":
        next_version = base_version.format(
            major=base_version.major + 1,
            minor=0,
            patch=0,
        )
    elif release_scale == "minor":
        next_version = base_version.format(
            minor=base_version.minor + 1,
            patch=0,
        )
    else:
        next_version = base_version.format(patch=base_version.patch + 1)

    # A withdrawn version must never be recreated by an automatic minor bump.
    while is_withdrawn_release(parse_version(next_version).key):
        withdrawn = parse_version(next_version)
        next_version = withdrawn.format(patch=withdrawn.patch + 1)
        release_reason += f" Skipped withdrawn version {withdrawn.text}."

    emit_metadata(
        {
            "current_version": current_version.text,
            "latest_release_tag": latest_tag,
            "latest_release_version": latest_version.text,
            "version": next_version,
            "version_tag": next_version,
            "release_tag": f"v{next_version}",
            "release_name": f"openQ4 {next_version}",
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
