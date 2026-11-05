#!/usr/bin/env python3
"""Query the openQ4 content pack table.

Subcommands:

  meson-list           one `name|source|stem` line per pack, for Meson's pack targets
  bulk-list            the bulk packs (published on their own), one name per line
  digest               source digest of every pack (or --pack) in a working tree
  digest-at-rev        source digest of every pack at a git revision, without a checkout

The source digest is the key that decides whether a published pack can be
reused: unchanged sources give an unchanged digest, and the release tooling
then ships the already-published bytes instead of rebuilding them.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from openq4_pak import (
    OPENQ4_CONTENT_PACKS,
    OPENQ4_CONTENT_PACKS_BY_NAME,
    pack_source_digest,
    pack_source_digest_from_tree_listing,
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("meson-list", help="List packs for Meson.")
    sub.add_parser("bulk-list", help="List the bulk packs.")

    digest = sub.add_parser("digest", help="Digest pack sources in a working tree.")
    digest.add_argument("--source-root", default=".", help="Repository root.")
    digest.add_argument("--pack", action="append", default=[], help="Limit to this pack; repeatable.")
    digest.add_argument("--json-out", default="", help="Also write the digests as JSON.")

    at_rev = sub.add_parser("digest-at-rev", help="Digest pack sources at a git revision.")
    at_rev.add_argument("--source-root", default=".", help="Repository root (a git checkout).")
    at_rev.add_argument("--rev", required=True, help="Revision, for example a release tag.")
    at_rev.add_argument("--pack", action="append", default=[], help="Limit to this pack; repeatable.")
    at_rev.add_argument("--json-out", default="", help="Also write the digests as JSON.")
    return parser.parse_args(argv[1:])


def selected_specs(names: list[str]):
    if not names:
        return OPENQ4_CONTENT_PACKS
    unknown = [name for name in names if name not in OPENQ4_CONTENT_PACKS_BY_NAME]
    if unknown:
        raise RuntimeError(f"unknown pack(s): {', '.join(unknown)}")
    return tuple(spec for spec in OPENQ4_CONTENT_PACKS if spec.name in names)


def write_json(path: str, document: dict) -> None:
    if path:
        Path(path).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def tree_digests_at_rev(source_root: Path, rev: str, specs) -> dict[str, dict]:
    """Digest each pack's sources as they stood at rev.

    A pack whose source directory does not exist at rev is reported with a
    null digest, which release notes read as "new in this release".
    """

    commit = subprocess.run(
        ["git", "-C", str(source_root), "rev-parse", "--verify", f"{rev}^{{commit}}"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    packs: dict[str, dict] = {}
    for spec in specs:
        listing = subprocess.run(
            ["git", "-C", str(source_root), "ls-tree", "-r", "-z", commit, "--", spec.source + "/"],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        ).stdout
        digest, count = pack_source_digest_from_tree_listing(listing, spec.source, spec.name)
        packs[spec.name] = {"source_digest": digest if count else None, "files": count, "source": spec.source}
    return {"rev": rev, "commit": commit, "packs": packs}


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.command == "meson-list":
            for spec in OPENQ4_CONTENT_PACKS:
                print(f"{spec.name}|{spec.source}|{Path(spec.name).stem}")
            return 0
        if args.command == "bulk-list":
            for spec in OPENQ4_CONTENT_PACKS:
                if spec.bulk:
                    print(spec.name)
            return 0

        specs = selected_specs(args.pack)
        source_root = Path(args.source_root).resolve()
        if args.command == "digest":
            packs = {}
            for spec in specs:
                digest, count = pack_source_digest(source_root / spec.source, spec.name)
                packs[spec.name] = {"source_digest": digest, "files": count, "source": spec.source}
                print(f"{spec.name} {digest} files={count}")
            write_json(args.json_out, {"packs": packs})
            return 0

        document = tree_digests_at_rev(source_root, args.rev, specs)
        for name, record in document["packs"].items():
            print(f"{name} {record['source_digest'] or 'absent'} files={record['files']}")
        write_json(args.json_out, document)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
