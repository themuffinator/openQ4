#!/usr/bin/env python3
"""List open issues that the stale-issue rule in CONTRIBUTING.md allows closing.

An issue qualifies when it is labelled `status: awaiting reporter`, a maintainer
asked its reporter something, the reporter has not replied for at least six
weeks, and at least one release has been published since that request (so the
reporter had a newer version to try).

The script only reads: it never comments on or closes an issue. Review each
candidate before closing it; another user confirming the bug, for example, is
a reason to keep it open. Requires the GitHub CLI (`gh`), signed in.

usage: stale-issues.py [--repo OWNER/NAME] [--weeks 6] [--all] [--json]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys

MAINTAINER_ASSOCIATIONS = {"OWNER", "MEMBER", "COLLABORATOR"}
# Only issues a maintainer has marked as waiting on their reporter can go stale.
AWAITING_LABEL = "status: awaiting reporter"

CLOSING_COMMENT = """\
Closing this for now: it has waited {weeks} weeks for a reply, and openQ4 \
{releases} has been released since. If this still happens in the current \
release, please open a new issue, link this one, and attach a log from that \
version.

<!-- openq4-triage-{today} -->
"""


def gh_json(*args: str):
    result = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        sys.exit(f"gh {' '.join(args)} failed: {result.stderr.strip()}")
    return json.loads(result.stdout)


def parse_time(text: str) -> dt.datetime:
    return dt.datetime.fromisoformat(text.replace("Z", "+00:00"))


def assess(issue: dict, releases: list[dict], now: dt.datetime, weeks: int, maintainers: set[str]) -> dict:
    reporter = issue["author"]["login"] if issue.get("author") else ""
    comments = sorted(issue.get("comments", []), key=lambda c: c["createdAt"])
    reporter_last = parse_time(issue["createdAt"])
    for comment in comments:
        if comment.get("author") and comment["author"]["login"] == reporter:
            reporter_last = max(reporter_last, parse_time(comment["createdAt"]))

    # The request is the first maintainer comment after the reporter last spoke.
    request = None
    for comment in comments:
        when = parse_time(comment["createdAt"])
        login = comment["author"]["login"] if comment.get("author") else ""
        if when > reporter_last and login != reporter and comment.get("authorAssociation") in MAINTAINER_ASSOCIATIONS:
            request = when
            break

    others = sorted({
        c["author"]["login"] for c in comments
        if c.get("author") and request is not None and parse_time(c["createdAt"]) > request
        and c["author"]["login"] != reporter and c.get("authorAssociation") not in MAINTAINER_ASSOCIATIONS
    })
    since = [r for r in releases if request is not None and parse_time(r["publishedAt"]) > request]
    waited_days = (now - request).days if request is not None else None
    awaiting = any(label["name"] == AWAITING_LABEL for label in issue.get("labels", []))
    if reporter in maintainers:
        verdict = "reporter is a maintainer"
    elif not awaiting:
        verdict = "not awaiting the reporter"
    elif request is None:
        verdict = "waiting on a maintainer"
    elif waited_days < weeks * 7:
        verdict = f"waiting {waited_days} days"
    elif not since:
        verdict = "no release since the request"
    elif others:
        verdict = "review: others have commented since"
    else:
        verdict = "close"
    return {
        "number": issue["number"], "title": issue["title"], "reporter": reporter,
        "request": request.date().isoformat() if request else None, "waited_days": waited_days,
        "releases_since": [r["tagName"] for r in since], "others_since": others, "verdict": verdict,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default="themuffinator/openQ4")
    parser.add_argument("--weeks", type=int, default=6)
    parser.add_argument("--all", action="store_true", help="list every open issue, not only candidates")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    now = dt.datetime.now(dt.timezone.utc)
    releases = [r for r in gh_json("release", "list", "-R", args.repo, "--limit", "100", "--json",
                                   "tagName,publishedAt,isDraft") if not r["isDraft"] and r.get("publishedAt")]
    issues = gh_json("issue", "list", "-R", args.repo, "--state", "open", "--limit", "500", "--json",
                     "number,title,author,createdAt,comments,labels")
    # Issues carry no author association in gh's output; comments do.
    maintainers = {args.repo.split("/")[0]} | {
        c["author"]["login"] for i in issues for c in i.get("comments", [])
        if c.get("author") and c.get("authorAssociation") in MAINTAINER_ASSOCIATIONS}
    rows = sorted((assess(i, releases, now, args.weeks, maintainers) for i in issues), key=lambda r: r["number"])
    if not args.all:
        rows = [r for r in rows if r["verdict"] in ("close", "review: others have commented since")]

    if args.json:
        print(json.dumps(rows, indent=2))
        return 0
    for row in rows:
        waited = f"{row['waited_days']}d" if row["waited_days"] is not None else "-"
        print(f"#{row['number']:<5} {row['verdict']:<36} waited {waited:<5} "
              f"releases since: {', '.join(row['releases_since']) or '-':<16} {row['title']}")
        if row["verdict"] == "close":
            newest = row["releases_since"][0]
            print("        suggested comment: " + CLOSING_COMMENT.format(
                weeks=row["waited_days"] // 7, releases=newest.lstrip("v"),
                today=now.date().isoformat()).splitlines()[0])
    if not rows:
        print("No open issue meets the stale-issue rule.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
