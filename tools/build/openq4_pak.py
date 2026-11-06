#!/usr/bin/env python3
"""Shared helpers for building and validating openQ4 PK4 payloads."""

from __future__ import annotations

import filecmp
import hashlib
import fnmatch
import json
import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


GAME_DIR_NAME = "baseoq4"
CONTENT_PACKS_TABLE_PATH = Path(__file__).resolve().with_name("content_packs.json")
CONTENT_PACK_NAME_PATTERN = re.compile(r"^pak[0-9]\.pk4$")
CONTENT_PACK_STRING_PATTERN = re.compile(r"^#str_[0-9]{6}$")
GIT_COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")
OVERLAY_PACK_NUMBERS = range(3, 10)
DEFAULT_OVERLAY_BUDGET_BYTES = 25 * 1000 * 1000
DETERMINISTIC_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


@dataclass(frozen=True)
class ContentPackSpec:
    """One openQ4 runtime content pack, as declared in content_packs.json."""

    name: str
    source: str
    contents: str
    contents_string: str
    client_only: bool
    bulk: bool
    required_files: frozenset[str]
    rules: tuple[str, ...] = ()
    overlay_of: str = ""  # an overlay pack: the base pack whose generation it amends
    generation_number: int = 0
    generation_commit: str = ""  # the commit whose sources the published pack was built from

    @property
    def macro_stem(self) -> str:
        return "OPENQ4_" + Path(self.name).stem.upper()


def light_grid_trim(arcnames: Sequence[str]) -> set[str]:
    """Light-grid files the runtime never reads from a release pack (roadmap PKG-03).

    A map with a .lightgridpack loads only the pack on every renderer (one
    without S3TC decodes its DXT atlases in software), so its loose atlas images
    under env/<map>/ are inspection copies. A .lightgrid with neither a pack nor
    atlases cannot light anything. Both stay in the source tree.
    """
    packed = {name.lower()[: -len(".lightgridpack")] for name in arcnames if name.lower().endswith(".lightgridpack")}
    atlas_maps: set[str] = set()
    dropped: set[str] = set()
    for name in arcnames:
        lower = name.lower()
        if lower.startswith("env/") and "/" in lower[4:]:
            owner = lower[4:].rsplit("/", 1)[0]
            if owner in packed:
                dropped.add(name)
            else:
                atlas_maps.add(owner)
    for name in arcnames:
        lower = name.lower()
        if lower.endswith(".lightgrid"):
            owner = lower[: -len(".lightgrid")]
            if owner not in packed and owner not in atlas_maps:
                dropped.add(name)
    return dropped


PACK_RULES = {"light-grid-trim": light_grid_trim}


def _parse_overlay_spec(raw: dict, label: str, by_name: Mapping[str, ContentPackSpec]) -> ContentPackSpec:
    """An overlay amends a bulk pack between generations and takes its audience and rules."""
    name = raw.get("name")
    base_name = raw.get("overlay_of")
    if not isinstance(name, str) or not CONTENT_PACK_NAME_PATTERN.match(name) or int(name[3]) not in OVERLAY_PACK_NUMBERS:
        raise RuntimeError(f"{label} overlay must be named pak3.pk4 to pak9.pk4, got {name!r}")
    base = by_name.get(base_name) if isinstance(base_name, str) else None
    if base is None or not base.bulk or base.overlay_of:
        raise RuntimeError(f"{label} ({name}) overlay_of must name a bulk pack listed before it")
    if not base.generation_commit:
        raise RuntimeError(f"{label} ({name}) amends {base.name}, which has no content generation")
    unknown = sorted(set(raw) - {"name", "overlay_of"})
    if unknown:
        raise RuntimeError(f"{label} ({name}) overlay takes only name and overlay_of, not {unknown}")
    return ContentPackSpec(
        name=name,
        source=base.source,
        contents=f"{base.contents} update",
        contents_string=base.contents_string,
        client_only=base.client_only,
        bulk=False,
        required_files=frozenset(),
        rules=base.rules,
        overlay_of=base.name,
        generation_number=base.generation_number,
        generation_commit=base.generation_commit,
    )


def load_content_pack_specs(path: Path = CONTENT_PACKS_TABLE_PATH) -> tuple[ContentPackSpec, ...]:
    """Read and validate the content pack table.

    The table is the single list of openQ4 runtime packs. Every consumer, from
    the Meson targets to the engine's generated validation table and the
    packagers, reads it, so a pack added or retired here reaches all of them.
    """

    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise RuntimeError(f"cannot read content pack table {path}: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise RuntimeError(f"content pack table {path} must be a schema 1 object")
    raw_packs = document.get("packs")
    if not isinstance(raw_packs, list) or not raw_packs:
        raise RuntimeError(f"content pack table {path} lists no packs")
    by_name: dict[str, ContentPackSpec] = {}

    specs: list[ContentPackSpec] = []
    seen_names: set[str] = set()
    seen_sources: set[str] = set()
    for index, raw in enumerate(raw_packs):
        label = f"content pack table entry {index}"
        if not isinstance(raw, dict):
            raise RuntimeError(f"{label} is not an object")
        if "overlay_of" in raw:
            overlay = _parse_overlay_spec(raw, label, by_name)
            if overlay.name in seen_names:
                raise RuntimeError(f"{label} repeats pack name {overlay.name}")
            seen_names.add(overlay.name)
            specs.append(overlay)
            by_name[overlay.name] = overlay
            continue
        name = raw.get("name")
        source = raw.get("source")
        contents = raw.get("contents")
        contents_string = raw.get("contents_string")
        client_only = raw.get("client_only")
        bulk = raw.get("bulk")
        required = raw.get("required_files", [])
        rules = raw.get("rules", [])
        if not isinstance(name, str) or not CONTENT_PACK_NAME_PATTERN.match(name):
            # Single digits only: numbered packs load by number, but wider
            # numerals load first, so pak10.pk4 would lose to pak9.pk4.
            raise RuntimeError(f"{label} has an invalid pack name {name!r}; expected pak0.pk4 to pak9.pk4")
        if name in seen_names:
            raise RuntimeError(f"{label} repeats pack name {name}")
        if not isinstance(source, str) or not source.startswith(f"content/{GAME_DIR_NAME}/"):
            raise RuntimeError(f"{label} ({name}) must name a source under content/{GAME_DIR_NAME}/")
        if source in seen_sources:
            raise RuntimeError(f"{label} ({name}) repeats source {source}")
        if not isinstance(contents, str) or not contents.strip():
            raise RuntimeError(f"{label} ({name}) has no contents description")
        if not isinstance(contents_string, str) or not CONTENT_PACK_STRING_PATTERN.match(contents_string):
            raise RuntimeError(f"{label} ({name}) needs a #str_NNNNNN contents_string")
        if not isinstance(client_only, bool) or not isinstance(bulk, bool):
            raise RuntimeError(f"{label} ({name}) must set client_only and bulk to true or false")
        if not isinstance(required, list) or not all(isinstance(item, str) and item for item in required):
            raise RuntimeError(f"{label} ({name}) required_files must be a list of archive paths")
        if not isinstance(rules, list) or any(rule not in PACK_RULES for rule in rules):
            raise RuntimeError(f"{label} ({name}) rules must name packing rules from {sorted(PACK_RULES)}")
        generation = raw.get("generation")
        generation_number = 0
        generation_commit = ""
        if generation is not None:
            if not bulk:
                raise RuntimeError(f"{label} ({name}): only bulk packs have content generations")
            if (not isinstance(generation, dict) or not isinstance(generation.get("number"), int)
                    or generation["number"] < 1 or not isinstance(generation.get("commit"), str)
                    or not GIT_COMMIT_PATTERN.match(generation["commit"])):
                raise RuntimeError(f"{label} ({name}) generation must be {{\"number\": N, \"commit\": \"<40-hex sha>\"}}")
            generation_number = generation["number"]
            generation_commit = generation["commit"]
        seen_names.add(name)
        seen_sources.add(source)
        spec = ContentPackSpec(
            name=name,
            source=source,
            contents=contents.strip(),
            contents_string=contents_string,
            client_only=client_only,
            bulk=bulk,
            required_files=frozenset(required),
            rules=tuple(rules),
            generation_number=generation_number,
            generation_commit=generation_commit,
        )
        specs.append(spec)
        by_name[name] = spec

    overlay_bases = [spec.overlay_of for spec in specs if spec.overlay_of]
    if len(overlay_bases) != len(set(overlay_bases)):
        raise RuntimeError("a bulk pack takes at most one overlay; it carries every change since the generation")
    if specs[0].name != "pak0.pk4" or specs[0].client_only or specs[0].bulk:
        raise RuntimeError("the first content pack must be pak0.pk4, the core pack every package and server carries")
    if sorted(spec.name for spec in specs) != [spec.name for spec in specs]:
        raise RuntimeError("content packs must be listed in load order")
    return tuple(specs)


OPENQ4_CONTENT_PACKS = load_content_pack_specs()
OPENQ4_CONTENT_PACKS_BY_NAME: Mapping[str, ContentPackSpec] = {spec.name: spec for spec in OPENQ4_CONTENT_PACKS}
PAK0_NAME = "pak0.pk4"
PAK1_NAME = "pak1.pk4"
PAK2_NAME = "pak2.pk4"
OPENQ4_PACK_NAMES = tuple(spec.name for spec in OPENQ4_CONTENT_PACKS)
OPENQ4_CORE_PACK_NAMES = tuple(spec.name for spec in OPENQ4_CONTENT_PACKS if not spec.bulk)
OPENQ4_BULK_PACK_NAMES = tuple(spec.name for spec in OPENQ4_CONTENT_PACKS if spec.bulk)
OPENQ4_CLIENT_ONLY_PACK_NAMES = tuple(spec.name for spec in OPENQ4_CONTENT_PACKS if spec.client_only)
OPENQ4_OVERLAY_PACK_NAMES = tuple(spec.name for spec in OPENQ4_CONTENT_PACKS if spec.overlay_of)


def load_overlay_budget_bytes(path: Path = CONTENT_PACKS_TABLE_PATH) -> int:
    value = json.loads(path.read_text(encoding="utf-8")).get("overlay_budget_bytes", DEFAULT_OVERLAY_BUDGET_BYTES)
    if not isinstance(value, int) or value <= 0:
        raise RuntimeError(f"{path}: overlay_budget_bytes must be a positive integer")
    return value

OPENQ4_EXCLUDED_DIRS = {"logs", "screenshots"}
OPENQ4_PK4_EXCLUDED_SUFFIXES = {
    ".dll",
    ".so",
    ".dylib",
    ".pdb",
    ".lib",
    ".exp",
    ".ilk",
}
OPENQ4_REQUIRED_PK4_FILES_BY_PACK = {
    spec.name: set(spec.required_files) for spec in OPENQ4_CONTENT_PACKS
}
OPENQ4_REQUIRED_LOOSE_GAME_FILES = {
    "mod.json",
}
OPENQ4_PK4_FORBIDDEN_FILES = {
    "addon.conf",
    "binary.conf",
}
OPENQ4_PK4_EXCLUDED_FILES = {
    "meson.build",
    "mod.json.in",
    *(name.lower() for name in OPENQ4_PACK_NAMES),
    *(relative_path.lower() for relative_path in OPENQ4_REQUIRED_LOOSE_GAME_FILES),
}


@dataclass(frozen=True)
class Pk4BuildResult:
    added_files: int
    skipped_samples: list[str]
    missing_required: list[str]
    md5_hex: str
    size_bytes: int


def file_md5_hex(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_text_if_changed(path: Path, contents: str) -> None:
    if path.is_symlink():
        path.unlink()
    if path.is_file() and path.read_text(encoding="utf-8") == contents:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8", newline="\n")


def _replace_file_if_changed(source: Path, destination: Path) -> None:
    if destination.is_symlink():
        destination.unlink()
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        source.unlink()
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    os.replace(source, destination)


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def require_directory_inside(path: Path, parent: Path, label: str) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{label} directory must not be a symlink: {path}")
    resolved_path = path.resolve()
    resolved_parent = parent.resolve()
    if not resolved_path.is_dir():
        raise RuntimeError(f"{label} directory not found: {resolved_path}")
    if not is_relative_to(resolved_path, resolved_parent):
        raise RuntimeError(f"{label} directory must stay under {resolved_parent}: {resolved_path}")
    return resolved_path


def validate_pk4_arcname(name: str, pak_name: str, seen_names: set[str] | None = None) -> str:
    normalized = name.replace("\\", "/")
    raw_parts = normalized.split("/")
    parts = tuple(part for part in raw_parts if part != "")
    first_part = parts[0] if parts else ""
    if (
        normalized != name
        or normalized.startswith("/")
        or normalized.startswith("../")
        or "/../" in f"/{normalized}/"
        or "" in raw_parts
        or ":" in first_part
        or not parts
        or any(part in (".", "..") for part in parts)
    ):
        raise RuntimeError(f"{pak_name} contains unsafe archive path: {name}")

    canonical = "/".join(parts)
    if seen_names is not None:
        key = canonical.lower()
        if key in seen_names:
            raise RuntimeError(f"{pak_name} contains duplicate archive path: {canonical}")
        seen_names.add(key)
    return canonical


def advance_mtime_ns(path: Path, mtime_ns: int) -> bool:
    """Move path's modification time forward to mtime_ns; never move it back."""
    current = path.stat()
    if current.st_mtime_ns >= mtime_ns:
        return False
    os.utime(path, ns=(current.st_atime_ns, mtime_ns))
    return True


def copy_file_if_changed(source: Path, destination: Path) -> bool:
    if source.is_symlink():
        raise RuntimeError(f"refusing to copy symlinked source file: {source}")
    source = source.resolve()
    if not source.is_file():
        raise RuntimeError(f"refusing to copy non-regular source file: {source}")
    if destination.is_symlink():
        destination.unlink()
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        # Identical bytes still take a newer source timestamp, as a fresh copy2
        # would, so a restamped pack never leaves its staged copies looking
        # older than the build they came from.
        advance_mtime_ns(destination, source.stat().st_mtime_ns)
        return False

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix=destination.name + ".",
        suffix=".tmp",
        dir=destination.parent,
        delete=False,
    ) as tmp_handle:
        tmp_path = Path(tmp_handle.name)

    try:
        shutil.copy2(source, tmp_path)
        os.replace(tmp_path, destination)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()
    return True


def _matches_content_filter(rel_posix_lower: str, filters: Sequence[str]) -> bool:
    """Last matching pattern wins; a leading '!' re-includes.

    Ordering matters, so 'gfx/*' followed by '!gfx/keepme*' drops the directory
    but keeps one subtree. Patterns are fnmatch globs against the lowercased
    archive path, where '*' spans '/' -- 'env/*' therefore covers the whole
    tree rather than only its top level.
    """
    excluded = False
    for pattern in filters:
        negated = pattern.startswith("!")
        candidate = pattern[1:] if negated else pattern
        if fnmatch.fnmatch(rel_posix_lower, candidate.lower()):
            excluded = not negated
    return excluded


def _should_skip_pk4_entry(relative_path: Path, content_filters: Sequence[str] = ()) -> bool:
    rel_parts_lower = {part.lower() for part in relative_path.parts}
    rel_posix_lower = relative_path.as_posix().lower()

    if rel_parts_lower & OPENQ4_EXCLUDED_DIRS:
        return True

    if rel_posix_lower in OPENQ4_PK4_EXCLUDED_FILES:
        return True

    if relative_path.suffix.lower() in OPENQ4_PK4_EXCLUDED_SUFFIXES:
        return True

    if content_filters and _matches_content_filter(rel_posix_lower, content_filters):
        return True

    return False


def required_files_for_pack(pak_name: str) -> set[str]:
    return set(OPENQ4_REQUIRED_PK4_FILES_BY_PACK.get(pak_name.lower(), set()))


def _c_string(value: str) -> str:
    if any(ch in value for ch in '"\\\n\r') or not value.isascii():
        raise RuntimeError(f"content pack table value cannot be written as a C string: {value!r}")
    return f'"{value}"'


def format_openq4_paks_header(
    results: Mapping[str, Pk4BuildResult],
    specs: Sequence[ContentPackSpec] = OPENQ4_CONTENT_PACKS,
) -> str:
    """Format the engine's generated pack table.

    Each pack gets OPENQ4_PAKn_* macros, and OPENQ4_PACK_TABLE_ENTRIES expands
    to one initializer per pack, in load order, for an array of
    { name, md5, size, file count, client-only, contents string, contents }.
    """

    missing = [spec.name for spec in specs if spec.name not in results]
    unknown = sorted(set(results) - {spec.name for spec in specs})
    if missing or unknown:
        raise RuntimeError(
            "pack header needs exactly the packs in the content pack table; "
            f"missing {missing or 'none'}, unexpected {unknown or 'none'}"
        )

    lines = [
        "#ifndef OPENQ4_PAKS_GENERATED_H",
        "#define OPENQ4_PAKS_GENERATED_H",
        "",
        "/* Generated from tools/build/content_packs.json by generate_pak_header.py. */",
        "",
        f"#define OPENQ4_PACK_COUNT {len(specs)}",
        "",
    ]
    for spec in specs:
        result = results[spec.name]
        stem = spec.macro_stem
        lines.extend(
            [
                f"#define {stem}_MD5 \"{result.md5_hex}\"",
                f"#define {stem}_SIZE_BYTES {result.size_bytes}",
                f"#define {stem}_FILE_COUNT {result.added_files}",
                f"#define {stem}_CLIENT_ONLY {1 if spec.client_only else 0}",
                f"#define {stem}_BULK {1 if spec.bulk else 0}",
                "",
            ]
        )
    lines.append("#define OPENQ4_PACK_TABLE_ENTRIES \\")
    for index, spec in enumerate(specs):
        stem = spec.macro_stem
        terminator = " \\" if index + 1 < len(specs) else ""
        lines.append(
            f"\t{{ {_c_string(spec.name)}, {stem}_MD5, {stem}_SIZE_BYTES, {stem}_FILE_COUNT, "
            f"{stem}_CLIENT_ONLY != 0, {stem}_BULK != 0, {_c_string(spec.contents_string)}, "
            f"{_c_string(spec.contents)} }},{terminator}"
        )
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def _iter_pk4_entries(
    source_dir: Path, pak_name: str, content_filters: Sequence[str] = ()
) -> tuple[list[tuple[Path, str]], list[str]]:
    entries: list[tuple[Path, str]] = []
    skipped_samples: list[str] = []

    if source_dir.is_symlink():
        raise RuntimeError(f"{pak_name} source directory must not be a symlink: {source_dir}")
    if not source_dir.is_dir():
        raise RuntimeError(f"{pak_name} source directory not found: {source_dir}")

    for path in sorted(source_dir.rglob("*"), key=lambda item: item.relative_to(source_dir).as_posix().lower()):
        rel = path.relative_to(source_dir)
        if path.is_symlink():
            raise RuntimeError(f"refusing to package symlink into {pak_name}: {rel.as_posix()}")
        if not path.is_file():
            continue

        rel_posix = validate_pk4_arcname(rel.as_posix(), pak_name)
        rel_posix_lower = rel_posix.lower()

        if rel_posix_lower in OPENQ4_PK4_FORBIDDEN_FILES:
            raise RuntimeError(
                f"{pak_name} must remain a pure runtime pack; "
                f"refusing marker file: {rel_posix}"
            )

        if _should_skip_pk4_entry(rel, content_filters):
            if len(skipped_samples) < 5:
                skipped_samples.append(rel_posix)
            continue

        entries.append((path, rel_posix))

    dropped = pack_rule_drops(pak_name, [arcname for _path, arcname in entries])
    if dropped:
        for _path, arcname in entries:
            if arcname in dropped and len(skipped_samples) < 5:
                skipped_samples.append(arcname)
        entries = [(path, arcname) for path, arcname in entries if arcname not in dropped]
    return entries, skipped_samples


def pack_rule_drops(pak_name: str, arcnames: Sequence[str]) -> set[str]:
    spec = OPENQ4_CONTENT_PACKS_BY_NAME.get(pak_name)
    dropped: set[str] = set()
    for rule in spec.rules if spec is not None else ():
        dropped |= PACK_RULES[rule](arcnames)
    return dropped


PACK_SOURCE_DIGEST_FORMAT = "openq4-pack-source 1"


def git_blob_sha1_hex(path: Path) -> str:
    """The git object id of a file's bytes, as `git hash-object` reports it.

    Pack sources are checked out without end-of-line conversion (see
    .gitattributes), so the working-tree bytes are the blob's bytes and a
    tree listing (`git ls-tree -r`) gives the same ids without a checkout.
    """

    digest = hashlib.sha1()
    digest.update(f"blob {path.stat().st_size}\0".encode("ascii"))
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def format_pack_source_digest(pak_name: str, entries: Sequence[tuple[str, str]], content_filters: Sequence[str] = ()) -> str:
    """Digest a pack's sources from (archive path, git blob id) pairs.

    Two source trees with the same digest pack to the same bytes with the same
    packer, so a published pack whose recorded digest matches can be reused
    instead of rebuilt. The digest covers the packer format, the pack name,
    any content filters and every packed path with its content id.
    """

    digest = hashlib.sha256()
    digest.update(f"{PACK_SOURCE_DIGEST_FORMAT}\n{pak_name}\n".encode("utf-8"))
    spec = OPENQ4_CONTENT_PACKS_BY_NAME.get(pak_name)
    for rule in spec.rules if spec is not None else ():
        digest.update(f"rule\t{rule}\n".encode("utf-8"))
    for pattern in content_filters:
        digest.update(f"filter\t{pattern}\n".encode("utf-8"))
    for arcname, blob_id in sorted(entries, key=lambda item: item[0].lower()):
        digest.update(f"{arcname}\t{blob_id}\n".encode("utf-8"))
    return digest.hexdigest()


def pack_source_digest(source_dir: Path, pak_name: str, content_filters: Sequence[str] = ()) -> tuple[str, int]:
    """Return (source digest, packed file count) for a working-tree pack source."""

    entries, _skipped = _iter_pk4_entries(source_dir, pak_name, content_filters)
    pairs = [(arcname, git_blob_sha1_hex(path)) for path, arcname in entries]
    return format_pack_source_digest(pak_name, pairs, content_filters), len(pairs)


def pack_source_digest_from_tree_listing(
    listing: str, source_prefix: str, pak_name: str, content_filters: Sequence[str] = ()
) -> tuple[str, int]:
    """Digest a pack source from `git ls-tree -r -z <rev> -- <source>` output.

    This answers "did this pack change since that tag?" for release notes
    without checking the tag out. The same skip rules as packing apply.
    """

    prefix = source_prefix.rstrip("/") + "/"
    pairs: list[tuple[str, str]] = []
    for record in listing.split("\0"):
        if not record:
            continue
        meta, _tab, path = record.partition("\t")
        fields = meta.split()
        if len(fields) != 3 or fields[1] != "blob" or not path.startswith(prefix):
            continue
        relative = Path(path[len(prefix):])
        if relative.name.lower() in OPENQ4_PK4_FORBIDDEN_FILES or _should_skip_pk4_entry(relative, content_filters):
            continue
        pairs.append((validate_pk4_arcname(relative.as_posix(), pak_name), fields[2]))
    dropped = pack_rule_drops(pak_name, [arcname for arcname, _blob in pairs])
    pairs = [(arcname, blob) for arcname, blob in pairs if arcname not in dropped]
    return format_pack_source_digest(pak_name, pairs, content_filters), len(pairs)


def format_pk4_source_manifest(source_root: Path, source_dir: Path, pak_name: str) -> str:
    source_root = source_root.resolve()
    source_dir = require_directory_inside(source_dir, source_root, "pack source")
    entries, _skipped_samples = _iter_pk4_entries(source_dir, pak_name)
    lines = [
        "# openQ4 PK4 source manifest",
        f"pak={pak_name}",
        f"source={source_dir.relative_to(source_root).as_posix()}",
    ]

    for path, arcname in entries:
        stat = path.stat()
        relative_path = path.relative_to(source_root).as_posix()
        lines.append(f"{arcname}\t{relative_path}\t{stat.st_size}\t{stat.st_mtime_ns}")

    return "\n".join(lines) + "\n"


def _write_deterministic_zip(
    source_dir: Path,
    destination_pk4: Path,
    pak_name: str,
    required_files: set[str] | None = None,
    content_filters: Sequence[str] = (),
) -> tuple[int, list[str], list[str]]:
    entries, skipped_samples = _iter_pk4_entries(source_dir, pak_name, content_filters)
    added_paths: set[str] = set()
    required_files = required_files if required_files is not None else required_files_for_pack(pak_name)

    destination_pk4.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix=destination_pk4.name + ".",
        suffix=".tmp",
        dir=destination_pk4.parent,
        delete=False,
    ) as tmp_handle:
        tmp_path = Path(tmp_handle.name)

    try:
        with ZipFile(tmp_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as pk4:
            for path, arcname in entries:
                info = ZipInfo(arcname, date_time=DETERMINISTIC_ZIP_TIMESTAMP)
                # ZipInfo otherwise records the build host (DOS=0, Unix=3).
                # The engine embeds a whole-archive checksum, so identical
                # authored content must also have identical container metadata.
                # Preserve the existing Windows package identity on every host.
                info.create_system = 0
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                with path.open("rb") as source, pk4.open(info, "w") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
                added_paths.add(arcname.lower())

        missing_required = sorted(
            required_path
            for required_path in required_files
            if required_path.lower() not in added_paths
        )

        _replace_file_if_changed(tmp_path, destination_pk4)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()

    return len(entries), skipped_samples, missing_required


def write_pk4_from_entries(
    destination_pk4: Path,
    pak_name: str,
    entries: Sequence[tuple[str, "Callable[[], bytes]"]],
    required_files: set[str] | None = None,
) -> Pk4BuildResult:
    """Write a deterministic pack from (archive path, data reader) entries.

    The same container metadata as create_game_pk4, for packs whose files come
    from somewhere other than one source directory: a content generation's git
    blobs, or the changed files an overlay carries.
    """
    required_files = required_files if required_files is not None else required_files_for_pack(pak_name)
    seen: set[str] = set()
    ordered = sorted(entries, key=lambda item: item[0].lower())
    destination_pk4.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=destination_pk4.name + ".", suffix=".tmp", dir=destination_pk4.parent, delete=False) as handle:
        tmp_path = Path(handle.name)
    try:
        with ZipFile(tmp_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as pk4:
            for arcname, read in ordered:
                arcname = validate_pk4_arcname(arcname, pak_name, seen)
                info = ZipInfo(arcname, date_time=DETERMINISTIC_ZIP_TIMESTAMP)
                info.create_system = 0
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                with pk4.open(info, "w") as target:
                    target.write(read())
        _replace_file_if_changed(tmp_path, destination_pk4)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()
    missing = sorted(path for path in required_files if path.lower() not in seen)
    return Pk4BuildResult(
        added_files=len(ordered),
        skipped_samples=[],
        missing_required=missing,
        md5_hex=file_md5_hex(destination_pk4),
        size_bytes=destination_pk4.stat().st_size,
    )


def create_game_pk4(
    source_dir: Path,
    destination_pk4: Path,
    *,
    pak_name: str | None = None,
    required_files: set[str] | None = None,
    content_filters: Sequence[str] = (),
) -> Pk4BuildResult:
    pak_name = pak_name or destination_pk4.name
    added_files, skipped_samples, missing_required = _write_deterministic_zip(
        source_dir,
        destination_pk4,
        pak_name,
        required_files,
        content_filters,
    )
    return Pk4BuildResult(
        added_files=added_files,
        skipped_samples=skipped_samples,
        missing_required=missing_required,
        md5_hex=file_md5_hex(destination_pk4),
        size_bytes=destination_pk4.stat().st_size,
    )


def inspect_game_pk4(
    pk4_path: Path,
    *,
    pak_name: str | None = None,
    required_files: set[str] | None = None,
) -> Pk4BuildResult:
    pak_name = pak_name or pk4_path.name
    required_files = required_files if required_files is not None else required_files_for_pack(pak_name)

    with ZipFile(pk4_path, "r") as pk4:
        names = []
        seen_names: set[str] = set()
        for info in pk4.infolist():
            if info.is_dir():
                continue
            mode_type = (info.external_attr >> 16) & 0o170000
            if mode_type not in (0, 0o100000):
                raise RuntimeError(f"{pak_name} contains non-regular archive entry: {info.filename}")
            names.append(validate_pk4_arcname(info.filename, pak_name, seen_names))

    lower_names = {name.lower() for name in names}
    forbidden = sorted(lower_names & OPENQ4_PK4_FORBIDDEN_FILES)
    if forbidden:
        joined = ", ".join(forbidden)
        raise RuntimeError(f"{pak_name} must remain a pure runtime pack; found marker file(s): {joined}")

    missing_required = sorted(
        required_path
        for required_path in required_files
        if required_path.lower() not in lower_names
    )

    return Pk4BuildResult(
        added_files=len(names),
        skipped_samples=[],
        missing_required=missing_required,
        md5_hex=file_md5_hex(pk4_path),
        size_bytes=pk4_path.stat().st_size,
    )


def copy_game_pk4(
    source_pk4: Path,
    destination_pk4: Path,
    *,
    pak_name: str | None = None,
    required_files: set[str] | None = None,
) -> Pk4BuildResult:
    copy_file_if_changed(source_pk4, destination_pk4)
    return inspect_game_pk4(destination_pk4, pak_name=pak_name, required_files=required_files)
