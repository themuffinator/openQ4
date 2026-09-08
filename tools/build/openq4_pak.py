#!/usr/bin/env python3
"""Shared helpers for building and validating openQ4 PK4 payloads."""

from __future__ import annotations

import filecmp
import hashlib
import fnmatch
import os
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


GAME_DIR_NAME = "baseoq4"
PAK0_NAME = "pak0.pk4"
PAK1_NAME = "pak1.pk4"
OPENQ4_PACK_NAMES = (PAK0_NAME, PAK1_NAME)
DETERMINISTIC_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
OPENQ4_PAKS_HEADER_TEMPLATE = """\
#ifndef OPENQ4_PAKS_GENERATED_H
#define OPENQ4_PAKS_GENERATED_H

#define OPENQ4_PAK0_MD5 "{pak0_md5_hex}"
#define OPENQ4_PAK0_SIZE_BYTES {pak0_size_bytes}
#define OPENQ4_PAK0_FILE_COUNT {pak0_file_count}

#define OPENQ4_PAK1_MD5 "{pak1_md5_hex}"
#define OPENQ4_PAK1_SIZE_BYTES {pak1_size_bytes}
#define OPENQ4_PAK1_FILE_COUNT {pak1_file_count}

#endif
"""

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
    PAK0_NAME: {
        "glprogs/smaa_blend.fs",
        "glprogs/smaa_blend.vs",
        "glprogs/smaa_edge.fs",
        "glprogs/smaa_edge.vs",
        "glprogs/smaa_weights.fs",
        "glprogs/smaa_weights.vs",
        "materials/postprocess_openq4.mtr",
    },
    PAK1_NAME: {
        "gfx/guis/loadscreens/generic.dds",
        "gfx/guis/loadscreens/generic.tga",
    },
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


def copy_file_if_changed(source: Path, destination: Path) -> bool:
    if source.is_symlink():
        raise RuntimeError(f"refusing to copy symlinked source file: {source}")
    source = source.resolve()
    if not source.is_file():
        raise RuntimeError(f"refusing to copy non-regular source file: {source}")
    if destination.is_symlink():
        destination.unlink()
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
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


def format_openq4_paks_header(pak0_result: Pk4BuildResult, pak1_result: Pk4BuildResult) -> str:
    return OPENQ4_PAKS_HEADER_TEMPLATE.format(
        pak0_md5_hex=pak0_result.md5_hex,
        pak0_size_bytes=pak0_result.size_bytes,
        pak0_file_count=pak0_result.added_files,
        pak1_md5_hex=pak1_result.md5_hex,
        pak1_size_bytes=pak1_result.size_bytes,
        pak1_file_count=pak1_result.added_files,
    )


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

    return entries, skipped_samples


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
