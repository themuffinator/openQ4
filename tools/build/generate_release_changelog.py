#!/usr/bin/env python3
"""Generate release notes for OpenQ4 manual releases."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


RELEASE_TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")


def run_git(args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(exc.stderr.strip() or exc.stdout.strip()) from exc
    return result.stdout.strip()


def parse_tag_version(tag: str) -> tuple[int, int, int] | None:
    match = RELEASE_TAG_RE.fullmatch(tag)
    if match is None:
        return None
    parts = match.group(1).split(".")
    return tuple(int(part) for part in parts)


def find_previous_release_tag(current_release_tag: str) -> str | None:
    tags = run_git(["tag", "--list", "v*"])
    release_tags: list[tuple[tuple[int, int, int], str]] = []
    for raw_tag in tags.splitlines():
        tag = raw_tag.strip()
        if not tag or tag == current_release_tag:
            continue
        version = parse_tag_version(tag)
        if version is None:
            continue
        release_tags.append((version, tag))

    if not release_tags:
        return None
    release_tags.sort()
    return release_tags[-1][1]


def collect_commits(range_spec: str | None, max_count: int) -> list[tuple[str, str, str, str]]:
    pretty = "%H%x1f%h%x1f%ad%x1f%s"
    args = ["log", "--no-merges", "--date=short", f"--pretty=format:{pretty}"]
    if range_spec:
        args.append(range_spec)
    else:
        args.extend(["-n", str(max_count)])

    output = run_git(args)
    commits: list[tuple[str, str, str, str]] = []
    if not output:
        return commits

    for line in output.splitlines():
        full, short, date, subject = line.split("\x1f", 3)
        commits.append((full, short, date, subject.strip()))
    return commits


def select_highlights(commits: list[tuple[str, str, str, str]], max_items: int) -> list[str]:
    seen: set[str] = set()
    highlights: list[str] = []
    for _, _, _, subject in commits:
        if not subject or subject in seen:
            continue
        seen.add(subject)
        highlights.append(subject)
        if len(highlights) >= max_items:
            break
    return highlights


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate OpenQ4 release notes.")
    parser.add_argument("--version", required=True, help="Human-readable release version.")
    parser.add_argument("--version-tag", required=True, help="File-safe release version tag.")
    parser.add_argument("--release-tag", required=True, help="Release tag (for example v0.1.010).")
    parser.add_argument("--release-scale", default="", help="Release scale emitted by the version helper.")
    parser.add_argument("--release-reason", default="", help="Release-version rationale emitted by the version helper.")
    parser.add_argument("--repo", required=True, help="GitHub repository slug (owner/name).")
    parser.add_argument("--output", required=True, help="Output markdown path.")
    parser.add_argument("--run-id", default="", help="GitHub workflow run ID.")
    parser.add_argument("--run-url", default="", help="GitHub workflow run URL.")
    parser.add_argument(
        "--max-commits",
        type=int,
        default=40,
        help="Maximum commits to include in the change log section (default: 40).",
    )
    parser.add_argument(
        "--max-highlights",
        type=int,
        default=6,
        help="Maximum highlighted commits (default: 6).",
    )
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    repo_url = f"https://github.com/{args.repo}"
    head_sha = run_git(["rev-parse", "HEAD"])
    short_sha = head_sha[:8]
    previous_tag = find_previous_release_tag(args.release_tag)

    commit_range = f"{previous_tag}..HEAD" if previous_tag else None
    commits = collect_commits(commit_range, args.max_commits)
    if not commits:
        commits = collect_commits(None, args.max_commits)

    highlights = select_highlights(commits, args.max_highlights)

    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    previous_tag_link = ""
    compare_link = ""
    if previous_tag:
        previous_tag_link = f"[`{previous_tag}`]({repo_url}/releases/tag/{previous_tag})"
        compare_link = f"[compare]({repo_url}/compare/{previous_tag}...{head_sha})"

    lines: list[str] = []
    lines.append(f"## OpenQ4 {args.version_tag}")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    lines.append(f"| Version | `{args.version}` |")
    lines.append(f"| Release tag | `{args.release_tag}` |")
    lines.append(f"| Commit | [`{short_sha}`]({repo_url}/commit/{head_sha}) |")
    lines.append(f"| Generated | `{generated_at}` |")
    if args.release_scale:
        lines.append(f"| Release scale | `{args.release_scale}` |")
    if args.release_reason:
        lines.append(f"| Version decision | {args.release_reason} |")
    if args.run_url:
        run_label = args.run_id if args.run_id else "Workflow run"
        lines.append(f"| Workflow | [{run_label}]({args.run_url}) |")
    if previous_tag:
        lines.append(f"| Since | {previous_tag_link} ({compare_link}) |")
    lines.append("")

    lines.append("### Highlights")
    lines.append("")
    if highlights:
        for subject in highlights:
            lines.append(f"- {subject}")
    else:
        lines.append("- Maintenance and release integration updates.")
    lines.append("")

    lines.append("### Change Log")
    lines.append("")
    if commits:
        for full_sha, short, date, subject in commits[: args.max_commits]:
            lines.append(
                f"- {subject} ([`{short}`]({repo_url}/commit/{full_sha}), {date})"
            )
    else:
        lines.append("- No commit metadata was available for this release.")
    lines.append("")

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote release changelog to {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
