#!/usr/bin/env python3
"""Release tooling for openQ4 content packs (roadmap PKG-01, PKG-02).

Subcommands:

  plan             which packs a release can reuse from the previous release's
                   content-packs.json, by source digest
  build            build every pack once (or reuse a published file whose
                   source digest is unchanged) and write content-packs.json
  notes            the release-notes section that states each pack's status
  verify-update    prove previous full package + update archive (+ any changed
                   bulk pack published on its own) == new full package
  verify-identity  prove every package carries the manifest's pack bytes
  overlay-plan     what each generation-pinned pack needs: nothing, an overlay
                   (pak3-pak9) carrying the files changed since its generation, or
                   a new generation; --apply updates content_packs.json
  generation-bump  start a new content generation for a pack at a commit, folding
                   its overlay into it

content-packs.json is published with every release. It records, per pack, the
name, what it holds, its audience, size, MD5 (the digest compiled into the
engine), SHA-256 and the source digest from content_packs.py. A later release
whose sources give the same digest ships the same file instead of rebuilding
it, so a player who has it never downloads it again.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json

import sys
import tarfile

import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

from content_generations import (
    GenerationError,
    build_content_pack,
    effective_source_digest,
    git,
    next_free_overlay_name,
    plan_overlays,
)
from openq4_pak import (
    CONTENT_PACKS_TABLE_PATH,
    GAME_DIR_NAME,
    OPENQ4_BULK_PACK_NAMES,
    OPENQ4_CONTENT_PACKS,
    OPENQ4_CONTENT_PACKS_BY_NAME,
    copy_file_if_changed,
    create_game_pk4,
    file_md5_hex,
    inspect_game_pk4,
    load_content_pack_specs,
    load_overlay_budget_bytes,
)

MANIFEST_SCHEMA = 1
MANIFEST_NAME = "content-packs.json"
UPDATE_ARCHIVE_MARKER = "-update"
# Stale files an update archive cannot remove but nothing ever loads.
DEFAULT_ALLOWED_STALE = ("docs/*", "*.html", "*.md", "*.txt")


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def format_size(size: int) -> str:
    return f"{size / 1_000_000:.1f} MB" if size >= 1_000_000 else f"{size / 1_000:.0f} kB"


# --- manifest ---------------------------------------------------------------


def read_manifest(path: Path) -> dict:
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or document.get("schema") != MANIFEST_SCHEMA:
        raise RuntimeError(f"{path} is not a schema {MANIFEST_SCHEMA} content pack manifest")
    packs = document.get("packs")
    if not isinstance(packs, list) or not packs:
        raise RuntimeError(f"{path} lists no packs")
    for pack in packs:
        for key in ("name", "size", "md5", "sha256", "source_digest"):
            if key not in pack:
                raise RuntimeError(f"{path}: pack entry lacks {key}")
    return document


def manifest_packs(document: dict | None) -> dict[str, dict]:
    return {pack["name"]: pack for pack in document["packs"]} if document else {}


def write_manifest(path: Path, document: dict) -> None:
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


# --- plan / build -----------------------------------------------------------


def reusable_packs(source_root: Path, previous: dict | None) -> dict[str, dict]:
    """Packs whose current source digest equals the previous release's."""
    previous_packs = manifest_packs(previous)
    reusable = {}
    for spec in OPENQ4_CONTENT_PACKS:
        record = previous_packs.get(spec.name)
        if record is None:
            continue
        digest, _count = effective_source_digest(source_root, spec)
        if digest == record["source_digest"]:
            reusable[spec.name] = record
    return reusable


def command_plan(args: argparse.Namespace) -> int:
    previous = read_manifest(Path(args.previous_manifest)) if args.previous_manifest else None
    reusable = reusable_packs(Path(args.source_root).resolve(), previous)
    for spec in OPENQ4_CONTENT_PACKS:
        state = "reuse" if spec.name in reusable else "build"
        print(f"{spec.name} {state}")
    if args.reuse_list:
        Path(args.reuse_list).write_text("".join(f"{name}\n" for name in reusable), encoding="utf-8", newline="\n")
    return 0


def command_build(args: argparse.Namespace) -> int:
    source_root = Path(args.source_root).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    previous = read_manifest(Path(args.previous_manifest)) if args.previous_manifest else None
    previous_packs = manifest_packs(previous)
    previous_dir = Path(args.previous_packs_dir).resolve() if args.previous_packs_dir else None

    packs = []
    for spec in OPENQ4_CONTENT_PACKS:
        source_digest, file_count = effective_source_digest(source_root, spec)
        destination = out_dir / spec.name
        record = previous_packs.get(spec.name)
        reused_from = None
        if record is not None and record["source_digest"] == source_digest and previous_dir is not None:
            candidate = previous_dir / spec.name
            if candidate.is_file() and sha256_hex(candidate) == record["sha256"]:
                copy_file_if_changed(candidate, destination)
                reused_from = record.get("first_published") or previous.get("version_tag")
        if reused_from is None:
            result = build_content_pack(source_root, spec, destination)
            if result.missing_required:
                raise RuntimeError(f"{spec.name} is missing required files: {', '.join(result.missing_required)}")
        inspected = inspect_game_pk4(destination, pak_name=spec.name)
        entry = {
            "name": spec.name,
            "contents": spec.contents,
            "client_only": spec.client_only,
            "bulk": spec.bulk,
            "size": destination.stat().st_size,
            "files": inspected.added_files,
            "source_files": file_count,
            "md5": file_md5_hex(destination),
            "sha256": sha256_hex(destination),
            "source_digest": source_digest,
            "first_published": reused_from or args.version_tag,
        }
        packs.append(entry)
        state = f"reused from {reused_from}" if reused_from else "built"
        print(f"{spec.name}: {state}, {format_size(entry['size'])}, sha256 {entry['sha256']}")

    if args.forbid_bulk_changes and previous is not None:
        changed = [
            pack["name"] for pack in packs
            if pack["bulk"] and pack["name"] in previous_packs and pack["sha256"] != previous_packs[pack["name"]]["sha256"]
        ]
        if changed:
            raise RuntimeError(
                "patch releases do not change bulk content, but "
                + ", ".join(changed)
                + f" changed since {previous.get('version_tag')}; make this a minor release, "
                "or ship the content fix as an overlay pack"
            )

    write_manifest(out_dir / MANIFEST_NAME, {
        "schema": MANIFEST_SCHEMA,
        "version_tag": args.version_tag,
        "packs": packs,
    })
    return 0


# --- notes ------------------------------------------------------------------


def pack_status(name: str, current: dict, previous: dict | None) -> str:
    if previous is None:
        return "new in this release's pack layout"
    record = manifest_packs(previous).get(name)
    if record is None:
        return "new in this release"
    if record["sha256"] == current["sha256"]:
        return f"unchanged since {current.get('first_published') or previous['version_tag']}"
    return f"changed since {previous['version_tag']}"


def format_notes(current: dict, previous: dict | None, update_asset_example: str) -> str:
    rows = []
    changed_bulk = []
    for pack in current["packs"]:
        status = pack_status(pack["name"], pack, previous)
        audience = "clients" if pack["client_only"] else "clients and servers"
        rows.append(f"| `{pack['name']}` | {pack['contents']} | {audience} | {format_size(pack['size'])} | {status} |")
        if pack["bulk"] and not status.startswith("unchanged"):
            changed_bulk.append(pack["name"])

    lines = [
        "## Content Packs",
        "",
        "| Pack | Contents | Needed by | Size | Status |",
        "| --- | --- | --- | ---: | --- |",
        *rows,
        "",
    ]
    if previous is None:
        lines.append(
            "This release changes how openQ4's own content is packed, so it is a full download. "
            "Install a full package, and remove any `baseoq4` folder left by an older openQ4 first."
        )
    elif not changed_bulk:
        lines.append(
            f"The light-grid and loadscreen packs did not change since {previous['version_tag']}. If you have that "
            f"release, download only the update archive for your platform (for example `{update_asset_example}`) and "
            "extract it into your openQ4 folder, replacing files."
        )
    else:
        names = ", ".join(f"`{name}`" for name in changed_bulk)
        lines.append(
            f"{names} changed in this release. To update from {previous['version_tag']}, extract the update archive "
            f"for your platform into your openQ4 folder and download {names} from this release into its `baseoq4` "
            "folder, or install a full package."
        )
    lines.append("")
    lines.append(
        "A dedicated server needs only `pak0.pk4`; the light grids and loadscreens are for clients. If openQ4 reports "
        "a missing or mismatched pack at startup, download that pack from this release under the same name."
    )
    return "\n".join(lines) + "\n"


def command_notes(args: argparse.Namespace) -> int:
    current = read_manifest(Path(args.manifest))
    previous = read_manifest(Path(args.previous_manifest)) if args.previous_manifest else None
    text = format_notes(current, previous, args.update_asset_example)
    if args.out:
        with Path(args.out).open("a" if args.append else "w", encoding="utf-8", newline="\n") as handle:
            handle.write(("\n" if args.append else "") + text)
    else:
        sys.stdout.write(text)
    return 0


# --- archive reading --------------------------------------------------------


@dataclass(frozen=True)
class Member:
    path: str  # relative to the package root
    sha256: str
    size: int


def _strip_root(names: list[str]) -> str:
    """The single top-level folder every entry shares, or '' for a rootless archive."""
    tops = {PurePosixPath(name).parts[0] for name in names if PurePosixPath(name).parts}
    if len(tops) == 1:
        top = next(iter(tops))
        if all(len(PurePosixPath(name).parts) > 1 for name in names):
            return top + "/"
    return ""


def _hash_stream(stream) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
        size += len(chunk)
    return digest.hexdigest(), size


def read_archive(path: Path, wanted=None, rootless: bool = False) -> dict[str, Member]:
    """Map package-relative path -> Member for a zip, APK, tar archive or directory.

    Full packages keep everything under one folder, which is stripped; update
    archives and APKs are rootless.
    """
    members: dict[str, Member] = {}
    if path.is_dir():
        for file in sorted(path.rglob("*")):
            if file.is_file():
                relative = file.relative_to(path).as_posix()
                if wanted is None or wanted(relative):
                    digest, size = _hash_stream(file.open("rb"))
                    members[relative] = Member(relative, digest, size)
        return members
    name = path.name.lower()
    if name.endswith((".zip", ".apk")):
        with zipfile.ZipFile(path) as archive:
            infos = [info for info in archive.infolist() if not info.is_dir()]
            prefix = "" if rootless or name.endswith(".apk") else _strip_root([info.filename for info in infos])
            for info in infos:
                relative = info.filename[len(prefix):]
                if wanted is None or wanted(relative):
                    with archive.open(info) as stream:
                        digest, size = _hash_stream(stream)
                    members[relative] = Member(relative, digest, size)
        return members
    if ".tar" in name:
        with tarfile.open(path, "r:*") as archive:
            infos = [info for info in archive.getmembers() if info.isfile()]
            prefix = "" if rootless else _strip_root([info.name for info in infos])
            for info in infos:
                relative = info.name[len(prefix):]
                if wanted is None or wanted(relative):
                    stream = archive.extractfile(info)
                    digest, size = _hash_stream(stream)
                    members[relative] = Member(relative, digest, size)
        return members
    raise RuntimeError(f"cannot read {path}: expected a zip, APK, tar archive or directory")


def pack_member_paths(members: Iterable[str], name: str) -> list[str]:
    """Every baseoq4/<name> in a package, wherever its baseoq4 sits (app bundle, APK assets)."""
    suffix = f"{GAME_DIR_NAME}/{name}"
    return [member for member in members if member == suffix or member.endswith("/" + suffix)]


# --- verify-update ----------------------------------------------------------


def command_verify_update(args: argparse.Namespace) -> int:
    previous = read_archive(Path(args.previous_full))
    update = read_archive(Path(args.update), rootless=True)
    new = read_archive(Path(args.new_full))
    standalone = {}
    for value in args.pack:
        pack_name, _sep, raw = value.partition("=")
        if pack_name not in OPENQ4_CONTENT_PACKS_BY_NAME or not raw:
            raise RuntimeError(f"--pack expects a table pack NAME=PATH, got {value!r}")
        standalone[f"{GAME_DIR_NAME}/{pack_name}"] = Member(f"{GAME_DIR_NAME}/{pack_name}", sha256_hex(Path(raw)), Path(raw).stat().st_size)

    failures: list[str] = []
    for bulk in OPENQ4_BULK_PACK_NAMES:
        for path in pack_member_paths(update, bulk):
            failures.append(f"the update archive carries the bulk pack {path}")

    result = dict(previous)
    result.update(update)
    result.update(standalone)
    for path, member in sorted(new.items()):
        have = result.get(path)
        if have is None:
            failures.append(f"missing after update: {path}")
        elif have.sha256 != member.sha256:
            origin = "update archive" if path in update else ("standalone pack" if path in standalone else "previous package")
            failures.append(f"differs after update: {path} (from the {origin})")
    allowed = tuple(args.allow_stale) or DEFAULT_ALLOWED_STALE
    stale = sorted(set(result) - set(new))
    harmful = [path for path in stale if not any(fnmatch.fnmatch(path, pattern) for pattern in allowed)]
    failures.extend(f"left behind by the update and still loaded: {path}" for path in harmful)

    needed = sorted(
        path for path in new
        if path in previous and path not in update and previous[path].sha256 != new[path].sha256
    )
    report = {
        "status": "fail" if failures else "pass",
        "previous_full": str(args.previous_full),
        "update": str(args.update),
        "new_full": str(args.new_full),
        "standalone_packs": sorted(standalone),
        "files_checked": len(new),
        "harmless_stale": [path for path in stale if path not in harmful],
        "failures": failures,
        "packs_to_download": needed,
    }
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "files_checked", "standalone_packs", "harmless_stale", "failures")}, indent=2))
    return int(bool(failures))


# --- verify-identity --------------------------------------------------------


def command_verify_identity(args: argparse.Namespace) -> int:
    manifest = manifest_packs(read_manifest(Path(args.manifest)))
    failures: list[str] = []
    summary: dict[str, dict[str, str]] = {}
    wanted_names = {f"{GAME_DIR_NAME}/{name}" for name in manifest}

    def wanted(relative: str) -> bool:
        return any(relative == name or relative.endswith("/" + name) for name in wanted_names)

    for raw in args.archive:
        path = Path(raw)
        is_update = UPDATE_ARCHIVE_MARKER in path.name and not path.name.endswith(".apk")
        members = read_archive(path, wanted, rootless=is_update)
        found: dict[str, str] = {}
        for name, record in manifest.items():
            paths = pack_member_paths(members, name)
            if is_update and record["bulk"]:
                if paths:
                    failures.append(f"{path.name}: an update archive must not carry {name}")
                continue
            if len(paths) != 1:
                failures.append(f"{path.name}: expected one {GAME_DIR_NAME}/{name}, found {len(paths)}")
                continue
            member = members[paths[0]]
            found[name] = member.sha256
            if member.sha256 != record["sha256"]:
                failures.append(f"{path.name}: {name} sha256 {member.sha256} differs from the manifest's {record['sha256']}")
        summary[path.name] = found
    for raw in args.pack_file:
        name, _sep, file = raw.partition("=")
        record = manifest.get(name)
        if record is None or not file:
            failures.append(f"--pack-file {raw!r} does not name a manifest pack")
            continue
        digest = sha256_hex(Path(file))
        summary[Path(file).name] = {name: digest}
        if digest != record["sha256"]:
            failures.append(f"{Path(file).name}: sha256 {digest} differs from the manifest's {record['sha256']}")
    report = {"status": "fail" if failures else "pass", "packages": summary, "failures": failures}
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "packages": len(summary), "failures": failures}, indent=2))
    return int(bool(failures))


# --- overlays and generations (PKG-04) ---------------------------------------


def write_table(path: Path, document: dict) -> None:
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")


def command_overlay_plan(args: argparse.Namespace) -> int:
    table_path = Path(args.table).resolve()
    source_root = Path(args.source_root).resolve()
    specs = load_content_pack_specs(table_path)
    budget = load_overlay_budget_bytes(table_path)
    plans = plan_overlays(source_root, specs, budget)
    for plan in plans:
        overlay = ", ".join(plan["overlays"]) or "none"
        print(f"{plan['pack']} generation {plan['generation']} ({plan['commit'][:12]}): {plan['action']}; "
              f"{len(plan['changed'])} changed, {len(plan['added'])} added, {len(plan['removed'])} removed, "
              f"{plan['overlay_bytes']} overlay bytes of {budget}; overlay {overlay}"
              + (f"; {plan['reason']}" if plan["reason"] else ""))
    if args.report:
        Path(args.report).write_text(json.dumps({"budget_bytes": budget, "plans": plans}, indent=2) + "\n", encoding="utf-8")
    if not args.apply:
        return 1 if any(plan["action"] == "new-generation" for plan in plans) else 0

    document = json.loads(table_path.read_text(encoding="utf-8"))
    packs = document["packs"]
    used = {pack["name"] for pack in packs}
    changed_table = False
    for plan in plans:
        if plan["action"] == "new-generation":
            print(f"error: {plan['pack']} needs a new generation ({plan['reason']}); run generation-bump", file=sys.stderr)
            return 1
        existing = [pack for pack in packs if pack.get("overlay_of") == plan["pack"]]
        if plan["action"] == "none" and existing:
            for pack in existing:
                packs.remove(pack)
                print(f"removed {pack['name']}: {plan['pack']} matches its generation again")
            changed_table = True
        elif plan["action"] == "overlay" and not existing:
            name = next_free_overlay_name(used)
            used.add(name)
            packs.append({"name": name, "overlay_of": plan["pack"]})
            print(f"added {name}: overlay of {plan['pack']} carrying {len(plan['changed']) + len(plan['added'])} file(s)")
            changed_table = True
    if changed_table:
        packs.sort(key=lambda pack: pack["name"])
        write_table(table_path, document)
        load_content_pack_specs(table_path)
    return 0


def command_generation_bump(args: argparse.Namespace) -> int:
    table_path = Path(args.table).resolve()
    source_root = Path(args.source_root).resolve()
    document = json.loads(table_path.read_text(encoding="utf-8"))
    packs = document["packs"]
    record = next((pack for pack in packs if pack.get("name") == args.pack and "overlay_of" not in pack), None)
    if record is None or not record.get("bulk"):
        raise RuntimeError(f"{args.pack} is not a bulk pack in {table_path}")
    commit = str(git(source_root, "rev-parse", "--verify", f"{args.commit}^{{commit}}")).strip()
    dirty = str(git(source_root, "status", "--porcelain", "--", record["source"])).strip()
    if dirty and args.commit == "HEAD":
        raise RuntimeError(f"{record['source']} has uncommitted changes; commit them before starting a generation from HEAD")
    previous = record.get("generation", {}).get("number", 0)
    record["generation"] = {"number": previous + 1, "commit": commit}
    folded = [pack["name"] for pack in packs if pack.get("overlay_of") == args.pack]
    document["packs"] = [pack for pack in packs if pack.get("overlay_of") != args.pack]
    write_table(table_path, document)
    load_content_pack_specs(table_path)
    print(f"{args.pack}: generation {previous + 1} at {commit}" + (f"; folded {', '.join(folded)}" if folded else ""))
    return 0


# --- main -------------------------------------------------------------------


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    plan = sub.add_parser("plan")
    plan.add_argument("--source-root", default=".")
    plan.add_argument("--previous-manifest", default="")
    plan.add_argument("--reuse-list", default="", help="write the reusable pack names, one per line")

    build = sub.add_parser("build")
    build.add_argument("--source-root", default=".")
    build.add_argument("--out-dir", required=True)
    build.add_argument("--version-tag", required=True)
    build.add_argument("--previous-manifest", default="")
    build.add_argument("--previous-packs-dir", default="", help="directory holding previously published packs to reuse")
    build.add_argument("--forbid-bulk-changes", action="store_true",
                       help="fail if a bulk pack differs from the previous release's (patch releases)")

    notes = sub.add_parser("notes")
    notes.add_argument("--manifest", required=True)
    notes.add_argument("--previous-manifest", default="")
    notes.add_argument("--update-asset-example", default="openq4-<version>-windows-x64-update.zip")
    notes.add_argument("--out", default="")
    notes.add_argument("--append", action="store_true")

    verify_update = sub.add_parser("verify-update")
    verify_update.add_argument("--previous-full", required=True)
    verify_update.add_argument("--update", required=True)
    verify_update.add_argument("--new-full", required=True)
    verify_update.add_argument("--pack", action="append", default=[], metavar="NAME=PATH",
                               help="a changed bulk pack published on its own")
    verify_update.add_argument("--allow-stale", action="append", default=[], metavar="GLOB")
    verify_update.add_argument("--report", default="")

    identity = sub.add_parser("verify-identity")
    identity.add_argument("--manifest", required=True)
    identity.add_argument("--archive", action="append", default=[], help="a package archive, APK or directory")
    identity.add_argument("--pack-file", action="append", default=[], metavar="NAME=PATH")
    identity.add_argument("--report", default="")
    overlay = sub.add_parser("overlay-plan")
    overlay.add_argument("--source-root", default=".")
    overlay.add_argument("--table", default=str(CONTENT_PACKS_TABLE_PATH))
    overlay.add_argument("--apply", action="store_true", help="add or remove overlay entries in the table")
    overlay.add_argument("--report", default="")

    bump = sub.add_parser("generation-bump")
    bump.add_argument("--source-root", default=".")
    bump.add_argument("--table", default=str(CONTENT_PACKS_TABLE_PATH))
    bump.add_argument("--pack", required=True)
    bump.add_argument("--commit", default="HEAD")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    handlers = {
        "plan": command_plan,
        "build": command_build,
        "notes": command_notes,
        "verify-update": command_verify_update,
        "verify-identity": command_verify_identity,
        "overlay-plan": command_overlay_plan,
        "generation-bump": command_generation_bump,
    }
    try:
        return handlers[args.command](args)
    except (OSError, RuntimeError, ValueError, GenerationError, zipfile.BadZipFile, tarfile.TarError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
