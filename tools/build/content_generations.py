#!/usr/bin/env python3
"""Content generations and overlay packs (roadmap PKG-04).

A bulk content pack (light grids, loadscreens) is published once per content
generation and players keep it across releases. A generation is the git commit
whose sources the published pack was built from, recorded in
tools/build/content_packs.json:

    {"name": "pak2.pk4", ..., "generation": {"number": 1, "commit": "<sha>"}}

While the sources still match that commit the pack builds from the working
tree as usual. When a few files change between generations, the base pack is
rebuilt from the generation commit's blobs, so it keeps its published bytes,
and the changed or added files ship as an overlay pack:

    {"name": "pak3.pk4", "overlay_of": "pak2.pk4"}

Overlays use pak3.pk4 to pak9.pk4: numbered packs load by number and the
later one wins, and single digits keep pak9.pk4 ahead of pak10.pk4, which
would load first. An overlay is small and ships in the update archive with the
binaries. Removing a file, or an overlay over the table's budget, needs a new
generation instead: `content_pack_release.py generation-bump` records the new
commit and folds the pack's overlays into it.
"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence

from openq4_pak import (
    OPENQ4_CONTENT_PACKS,
    OPENQ4_CONTENT_PACKS_BY_NAME,
    ContentPackSpec,
    Pk4BuildResult,
    create_game_pk4,
    format_pack_source_digest,
    pack_source_digest,
    write_pk4_from_entries,
    _iter_pk4_entries,
    git_blob_sha1_hex,
    pack_rule_drops,
    validate_pk4_arcname,
    _should_skip_pk4_entry,
    OPENQ4_PK4_FORBIDDEN_FILES,
)


class GenerationError(RuntimeError):
    pass


@dataclass(frozen=True)
class GenerationDiff:
    base: str
    commit: str
    changed: tuple[str, ...]  # archive paths whose content differs from the generation
    added: tuple[str, ...]  # archive paths the generation does not have
    removed: tuple[str, ...]  # archive paths the generation has and the sources no longer do
    overlay_bytes: int  # raw bytes of changed + added files

    @property
    def overlay_files(self) -> tuple[str, ...]:
        return tuple(sorted(self.changed + self.added, key=str.lower))

    @property
    def unchanged(self) -> bool:
        return not (self.changed or self.added or self.removed)


def generation_available(source_root: Path, commit: str) -> bool:
    """The generation commit is readable, and source_root is that repository's top level.

    Pack paths in the table are relative to the repository root, so a source
    tree that merely sits inside some checkout (a test fixture, an exported
    tree) has no generation to compare against.
    """
    top = subprocess.run(["git", "-C", str(source_root), "rev-parse", "--show-toplevel"],
                         capture_output=True, text=True, check=False)
    if top.returncode != 0 or Path(top.stdout.strip()).resolve() != source_root.resolve():
        return False
    completed = subprocess.run(["git", "-C", str(source_root), "cat-file", "-e", f"{commit}^{{commit}}"],
                               capture_output=True, check=False)
    return completed.returncode == 0


def git(source_root: Path, *args: str, text: bool = True) -> str | bytes:
    completed = subprocess.run(["git", "-C", str(source_root), *args], capture_output=True, text=text, check=False)
    if completed.returncode != 0:
        detail = completed.stderr if text else completed.stderr.decode("utf-8", "replace")
        raise GenerationError(f"git {' '.join(args)} failed: {detail.strip()}")
    return completed.stdout


def generation_listing(source_root: Path, spec: ContentPackSpec, commit: str) -> dict[str, str]:
    """archive path -> blob id of the pack's sources at the generation commit, after skip rules."""
    listing = git(source_root, "ls-tree", "-r", "-z", commit, "--", spec.source + "/")
    prefix = spec.source.rstrip("/") + "/"
    entries: dict[str, str] = {}
    for record in listing.split("\0"):
        if not record:
            continue
        meta, _tab, path = record.partition("\t")
        fields = meta.split()
        if len(fields) != 3 or fields[1] != "blob" or not path.startswith(prefix):
            continue
        relative = Path(path[len(prefix):])
        if relative.name.lower() in OPENQ4_PK4_FORBIDDEN_FILES or _should_skip_pk4_entry(relative):
            continue
        entries[validate_pk4_arcname(relative.as_posix(), spec.name)] = fields[2]
    dropped = pack_rule_drops(spec.name, list(entries))
    return {name: blob for name, blob in entries.items() if name not in dropped}


def working_listing(source_root: Path, spec: ContentPackSpec) -> dict[str, tuple[Path, str]]:
    """archive path -> (file, blob id) of the pack's sources in the working tree, after skip rules."""
    entries, _skipped = _iter_pk4_entries(source_root / spec.source, spec.name)
    return {arcname: (path, git_blob_sha1_hex(path)) for path, arcname in entries}


def diff_against_generation(source_root: Path, spec: ContentPackSpec) -> GenerationDiff:
    if not spec.generation_commit:
        raise GenerationError(f"{spec.name} has no content generation")
    old = generation_listing(source_root, spec, spec.generation_commit)
    new = working_listing(source_root, spec)
    old_lower = {name.lower(): blob for name, blob in old.items()}
    new_lower = {name.lower() for name in new}
    changed = tuple(sorted((name for name, (_p, blob) in new.items()
                            if name.lower() in old_lower and old_lower[name.lower()] != blob), key=str.lower))
    added = tuple(sorted((name for name in new if name.lower() not in old_lower), key=str.lower))
    removed = tuple(sorted((name for name in old if name.lower() not in new_lower), key=str.lower))
    overlay_bytes = sum(new[name][0].stat().st_size for name in changed + added)
    return GenerationDiff(spec.name, spec.generation_commit, changed, added, removed, overlay_bytes)


class GitBlobReader:
    """Streams blob contents through one `git cat-file --batch` process."""

    def __init__(self, source_root: Path) -> None:
        self.process = subprocess.Popen(
            ["git", "-C", str(source_root), "cat-file", "--batch"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def read(self, blob: str) -> bytes:
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.write(blob.encode("ascii") + b"\n")
        self.process.stdin.flush()
        header = self.process.stdout.readline().decode("ascii", "replace").split()
        if len(header) != 3 or header[1] != "blob":
            raise GenerationError(f"git cat-file could not read blob {blob}: {' '.join(header)}")
        size = int(header[2])
        data = self.process.stdout.read(size)
        self.process.stdout.read(1)  # the newline after each object
        if len(data) != size:
            raise GenerationError(f"git cat-file returned a short blob {blob}")
        return data

    def close(self) -> None:
        if self.process.stdin is not None:
            self.process.stdin.close()
        self.process.wait()
        for stream in (self.process.stdout, self.process.stderr):
            if stream is not None:
                stream.close()

    def __enter__(self) -> "GitBlobReader":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


def base_entries_at_generation(source_root: Path, spec: ContentPackSpec, reader: GitBlobReader) -> list[tuple[str, Callable[[], bytes]]]:
    listing = generation_listing(source_root, spec, spec.generation_commit)
    return [(name, (lambda blob=blob: reader.read(blob))) for name, blob in sorted(listing.items(), key=lambda item: item[0].lower())]


def overlays_of(base: str, specs: Iterable[ContentPackSpec]) -> list[ContentPackSpec]:
    return [spec for spec in specs if spec.overlay_of == base]


def plan_overlays(source_root: Path, specs: Sequence[ContentPackSpec], budget_bytes: int) -> list[dict]:
    """What each generation-pinned pack needs now: nothing, an overlay, or a new generation."""
    plans = []
    for spec in specs:
        if spec.overlay_of or not spec.generation_commit:
            continue
        diff = diff_against_generation(source_root, spec)
        existing = [overlay.name for overlay in overlays_of(spec.name, specs)]
        if diff.unchanged:
            action = "none"
        elif diff.removed:
            action = "new-generation"
            reason = f"{len(diff.removed)} file(s) were removed, which an overlay cannot express"
        elif diff.overlay_bytes > budget_bytes:
            action = "new-generation"
            reason = f"the overlay would carry {diff.overlay_bytes} bytes, over the {budget_bytes}-byte budget"
        else:
            action = "overlay"
        plans.append({
            "pack": spec.name,
            "generation": spec.generation_number,
            "commit": spec.generation_commit,
            "action": action,
            "reason": reason if action == "new-generation" else "",
            "changed": list(diff.changed),
            "added": list(diff.added),
            "removed": list(diff.removed),
            "overlay_bytes": diff.overlay_bytes,
            "overlays": existing,
        })
    return plans


def next_free_overlay_name(used: Iterable[str]) -> str:
    taken = set(used)
    for number in range(3, 10):
        name = f"pak{number}.pk4"
        if name not in taken:
            return name
    raise GenerationError("pak3.pk4 to pak9.pk4 are all in use; start a new content generation to fold the overlays")


def build_content_pack(
    source_root: Path,
    spec: ContentPackSpec,
    pak_out: Path,
    specs: Sequence[ContentPackSpec] = OPENQ4_CONTENT_PACKS,
) -> Pk4BuildResult:
    """Build any table pack: a plain pack, a generation-pinned bulk pack, or its overlay."""
    if not spec.generation_commit:
        return create_game_pk4(source_root / spec.source, pak_out, pak_name=spec.name)
    by_name = {item.name: item for item in specs}
    base = by_name[spec.overlay_of] if spec.overlay_of else spec
    if not generation_available(source_root, base.generation_commit):
        # A shallow CI checkout lacks the generation commit. Its packs never ship
        # (releases build them in the content_packs job, which has the history,
        # and require it), so pack what is checked out and say so.
        if os.environ.get("OPENQ4_REQUIRE_CONTENT_GENERATIONS", "").strip() == "1":
            raise GenerationError(f"{base.name}'s generation commit {base.generation_commit} is not in this checkout")
        print(f"warning: {base.name} generation commit {base.generation_commit[:12]} is not in this checkout; "
              f"{spec.name} is packed from the working tree and will not match the published pack", file=sys.stderr)
        if spec.overlay_of:
            return write_pk4_from_entries(pak_out, spec.name, [("openq4-overlay-unavailable.txt", lambda: b"")], required_files=set())
        return create_game_pk4(source_root / spec.source, pak_out, pak_name=spec.name)
    diff = diff_against_generation(source_root, base)
    if diff.removed:
        raise GenerationError(
            f"{base.name} sources removed {len(diff.removed)} file(s) since generation {base.generation_number} "
            f"({diff.removed[0]}...); an overlay cannot remove files, so start a new generation "
            "(content_pack_release.py generation-bump)"
        )
    if spec.overlay_of:
        files = diff.overlay_files
        if not files:
            raise GenerationError(
                f"overlay {spec.name} has nothing to carry: {base.name} matches generation {base.generation_number}; "
                "remove it from tools/build/content_packs.json (content_pack_release.py overlay-plan --apply)"
            )
        source_dir = source_root / base.source
        entries = [(name, (lambda path=source_dir / name: path.read_bytes())) for name in files]
        return write_pk4_from_entries(pak_out, spec.name, entries, required_files=set())
    if diff.unchanged:
        return create_game_pk4(source_root / spec.source, pak_out, pak_name=spec.name)
    if not overlays_of(spec.name, specs):
        raise GenerationError(
            f"{spec.name} sources changed since generation {spec.generation_number} but no overlay carries the "
            "change; run content_pack_release.py overlay-plan --apply, or start a new generation"
        )
    # The base keeps its published bytes: pack the generation commit's blobs.
    with GitBlobReader(source_root) as reader:
        return write_pk4_from_entries(pak_out, spec.name, base_entries_at_generation(source_root, spec, reader))


def effective_source_digest(
    source_root: Path, spec: ContentPackSpec, specs: Sequence[ContentPackSpec] = OPENQ4_CONTENT_PACKS
) -> tuple[str, int]:
    """The source digest of exactly the files a pack carries.

    A generation-pinned base carries its generation's sources even while the
    working tree has moved on, so a published base stays reusable; an overlay
    carries the files changed since the generation.
    """
    if not spec.generation_commit or not generation_available(source_root, spec.generation_commit):
        return pack_source_digest(source_root / spec.source, spec.name)
    by_name = {item.name: item for item in specs}
    base = by_name[spec.overlay_of] if spec.overlay_of else spec
    if spec.overlay_of:
        current = working_listing(source_root, base)
        files = diff_against_generation(source_root, base).overlay_files
        pairs = [(name, current[name][1]) for name in files]
    else:
        pairs = list(generation_listing(source_root, spec, spec.generation_commit).items())
    return format_pack_source_digest(spec.name, pairs), len(pairs)
