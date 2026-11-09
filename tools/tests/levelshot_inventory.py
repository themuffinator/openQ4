#!/usr/bin/env python3
"""Validate retail and Raven map-pack levelshot tile coverage.

The checked-in index is derived from the proprietary retail declarations so CI can
validate openQ4's redistributable output without requiring those declarations. Pass
``--retail-def-root`` (or set ``OPENQ4_RETAIL_DEF_ROOT``) to independently re-derive
the index from an installed/source asset tree containing ``maps.def`` and
``raven_mappack.def``.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INDEX = ROOT / "tools" / "tests" / "data" / "retail_levelshot_mapdefs.tsv"
CONTENT_PACKS = ROOT / "tools" / "build" / "content_packs.json"
CLASS_SOURCES = {"base": "maps.def", "map-pack": "raven_mappack.def"}
EXPECTED_COUNTS = {"base": 56, "map-pack": 7}
VARIANTS = ("", "_left", "_right", "_top", "_bottom")
TOKEN_RE = re.compile(r'"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+')


@dataclass(frozen=True, order=True)
class Entry:
    classification: str
    source: str
    map_def: str
    loadimage: str


def fail(message: str) -> None:
    raise AssertionError(message)


def unquote(token: str) -> str:
    if token.startswith('"'):
        return bytes(token[1:-1], "utf-8").decode("unicode_escape")
    return token


def strip_comments(source: str) -> str:
    """Remove idDecl comments without treating comment markers in strings as syntax."""
    output: list[str] = []
    index = 0
    quoted = False
    escaped = False
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if quoted:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            index += 1
            continue
        if char == '"':
            quoted = True
            output.append(char)
            index += 1
        elif char == "/" and following == "/":
            index += 2
            while index < len(source) and source[index] not in "\r\n":
                index += 1
        elif char == "/" and following == "*":
            end = source.find("*/", index + 2)
            if end < 0:
                fail("unterminated block comment in retail declaration")
            output.append("\n" * source.count("\n", index, end + 2))
            index = end + 2
        else:
            output.append(char)
            index += 1
    if quoted:
        fail("unterminated string in retail declaration")
    return "".join(output)


def derive_mapdefs(path: Path) -> dict[str, str]:
    tokens = TOKEN_RE.findall(strip_comments(path.read_text(encoding="utf-8-sig")))
    derived: dict[str, str] = {}
    index = 0
    while index < len(tokens):
        if tokens[index].lower() != "mapdef":
            index += 1
            continue
        if index + 2 >= len(tokens) or tokens[index + 2] != "{":
            fail(f"malformed mapDef near token {index} in {path}")
        map_def = unquote(tokens[index + 1])
        index += 3
        depth = 1
        loadimages: list[str] = []
        while index < len(tokens) and depth:
            token = tokens[index]
            if token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
            elif depth == 1 and unquote(token).lower() == "loadimage":
                if index + 1 >= len(tokens) or tokens[index + 1] in ("{", "}"):
                    fail(f"mapDef {map_def!r} has a malformed loadimage in {path}")
                loadimages.append(unquote(tokens[index + 1]))
                index += 1
            index += 1
        if depth:
            fail(f"mapDef {map_def!r} has an unterminated body in {path}")
        if len(loadimages) > 1:
            fail(f"mapDef {map_def!r} has multiple loadimage values in {path}")
        if loadimages:
            if map_def in derived:
                fail(f"duplicate mapDef {map_def!r} in {path}")
            derived[map_def] = loadimages[0]
    return derived


def read_index(path: Path) -> list[Entry]:
    entries: list[Entry] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = raw_line.split("\t")
        if len(fields) != 4 or any(field != field.strip() or not field for field in fields):
            fail(f"{path}:{line_number}: expected four non-empty tab-separated fields")
        entry = Entry(*fields)
        expected_source = CLASS_SOURCES.get(entry.classification)
        if expected_source is None:
            fail(f"{path}:{line_number}: unknown classification {entry.classification!r}")
        if entry.source != expected_source:
            fail(
                f"{path}:{line_number}: {entry.classification!r} entries must come from "
                f"{expected_source!r}, not {entry.source!r}"
            )
        for label, value in (("mapDef", entry.map_def), ("loadimage", entry.loadimage)):
            if value != value.lower() or "\\" in value or PurePosixPath(value).is_absolute():
                fail(f"{path}:{line_number}: unsafe or non-canonical {label} {value!r}")
            if any(part in ("", ".", "..") for part in PurePosixPath(value).parts):
                fail(f"{path}:{line_number}: unsafe {label} path {value!r}")
        if not entry.loadimage.startswith("gfx/guis/loadscreens/"):
            fail(f"{path}:{line_number}: loadimage is outside gfx/guis/loadscreens: {entry.loadimage!r}")
        if PurePosixPath(entry.loadimage).suffix:
            fail(f"{path}:{line_number}: loadimage must not include an extension: {entry.loadimage!r}")
        entries.append(entry)

    counts = Counter(entry.classification for entry in entries)
    if counts != Counter(EXPECTED_COUNTS):
        fail(f"retail index classification counts changed: expected {EXPECTED_COUNTS}, found {dict(counts)}")
    map_defs = [entry.map_def for entry in entries]
    loadimages = [entry.loadimage for entry in entries]
    if len(map_defs) != len(set(map_defs)):
        fail("retail index contains duplicate mapDef names")
    if len(loadimages) != len(set(loadimages)):
        fail("retail index contains duplicate loadimage names")
    return entries


def compare_retail_sources(entries: list[Entry], retail_def_root: Path) -> None:
    indexed_by_source: dict[str, dict[str, str]] = {}
    for classification, source in CLASS_SOURCES.items():
        source_path = retail_def_root / source
        if not source_path.is_file():
            fail(f"retail declaration not found: {source_path}")
        indexed_by_source[source] = {
            entry.map_def: entry.loadimage
            for entry in entries
            if entry.classification == classification
        }
        derived = derive_mapdefs(source_path)
        indexed = indexed_by_source[source]
        if derived != indexed:
            missing = sorted(set(derived) - set(indexed))
            stale = sorted(set(indexed) - set(derived))
            changed = sorted(name for name in set(derived) & set(indexed) if derived[name] != indexed[name])
            fail(
                f"{source} no longer matches the checked-in index; "
                f"unindexed={missing}, stale={stale}, changed={changed}"
            )


def validate_tga(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 18:
        fail(f"truncated TGA header: {path}")
    id_length, color_map_type, image_type = data[0], data[1], data[2]
    width, height, bits_per_pixel = struct.unpack_from("<HHB", data, 12)
    if color_map_type != 0 or image_type != 2:
        fail(f"expected an uncompressed true-color TGA: {path}")
    if width <= 0 or height <= 0 or bits_per_pixel != 32:
        fail(f"expected a non-empty 32-bit TGA, found {width}x{height}x{bits_per_pixel}: {path}")
    expected_size = 18 + id_length + width * height * 4
    if len(data) != expected_size:
        fail(f"unexpected TGA payload size for {width}x{height}: {path} ({len(data)} != {expected_size})")
    return width, height


def validate_dds(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"DDS ":
        fail(f"invalid or truncated DDS header: {path}")
    header_size = struct.unpack_from("<I", data, 4)[0]
    height, width = struct.unpack_from("<II", data, 12)
    pixel_format_size = struct.unpack_from("<I", data, 76)[0]
    four_cc = data[84:88]
    if header_size != 124 or pixel_format_size != 32 or width <= 0 or height <= 0:
        fail(f"invalid DDS dimensions/header: {path}")
    if four_cc != b"DXT1":
        fail(f"expected the levelshot workflow's DXT1 DDS, found {four_cc!r}: {path}")
    expected_size = 128 + ((width + 3) // 4) * ((height + 3) // 4) * 8
    if len(data) != expected_size:
        fail(f"unexpected DDS payload size for {width}x{height}: {path} ({len(data)} != {expected_size})")
    return width, height


def validate_optional_pose(path: Path) -> None:
    if not path.is_file():
        return
    values = path.read_text(encoding="ascii").split()
    if len(values) != 6:
        fail(f"expected six levelshot pose values: {path}")
    try:
        pose = [float(value) for value in values]
    except ValueError as exc:
        fail(f"invalid levelshot pose value in {path}: {exc}")
    if not all(math.isfinite(value) for value in pose):
        fail(f"non-finite levelshot pose value: {path}")


def validate_coverage(entries: list[Entry], content_root: Path) -> list[tuple[Entry, list[str]]]:
    incomplete: list[tuple[Entry, list[str]]] = []
    for entry in entries:
        base = content_root / PurePosixPath(entry.loadimage)
        missing: list[str] = []
        dimensions: set[tuple[int, int]] = set()
        for variant in VARIANTS:
            tga = Path(f"{base}{variant}.tga")
            dds = Path(f"{base}{variant}.dds")
            absent = [path.suffix[1:] for path in (tga, dds) if not path.is_file()]
            if absent:
                missing.append(f"{variant or '<center>'} ({','.join(absent)})")
                continue
            tga_dimensions = validate_tga(tga)
            dds_dimensions = validate_dds(dds)
            if tga_dimensions != dds_dimensions:
                fail(f"TGA/DDS dimensions differ for {entry.loadimage}{variant}: {tga_dimensions} != {dds_dimensions}")
            dimensions.add(tga_dimensions)
        if missing:
            incomplete.append((entry, missing))
        elif len(dimensions) != 1:
            fail(f"tile dimensions differ within {entry.loadimage}: {sorted(dimensions)}")
        elif next(iter(dimensions))[0] != next(iter(dimensions))[1]:
            fail(f"levelshot tiles must be square for {entry.loadimage}: {next(iter(dimensions))}")
        validate_optional_pose(Path(f"{base}.txt"))
    return incomplete


def loadscreen_pack_root() -> Path:
    """Source folder of the content pack that ships the loadscreens (pak2 since PKG-02)."""
    packs = json.loads(CONTENT_PACKS.read_text(encoding="utf-8"))["packs"]
    roots = [ROOT / pack["source"] for pack in packs
             if any(name.startswith("gfx/guis/loadscreens/") for name in pack.get("required_files", ()))]
    if len(roots) != 1:
        raise SystemExit(f"expected exactly one loadscreen content pack in {CONTENT_PACKS.name}, found {len(roots)}")
    return roots[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--content-root", type=Path, default=None,
                        help="loadscreen pack source (default: the pack content_packs.json assigns them to)")
    parser.add_argument(
        "--retail-def-root",
        type=Path,
        default=Path(os.environ["OPENQ4_RETAIL_DEF_ROOT"]) if os.environ.get("OPENQ4_RETAIL_DEF_ROOT") else None,
        help="directory containing maps.def and raven_mappack.def for source re-derivation",
    )
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help="report missing tile sets without failing (format errors still fail)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.content_root is None:
        args.content_root = loadscreen_pack_root()
    entries = read_index(args.index)
    if args.retail_def_root is not None:
        compare_retail_sources(entries, args.retail_def_root)

    incomplete = validate_coverage(entries, args.content_root)
    incomplete_loadimages = {entry.loadimage for entry, _ in incomplete}
    for classification in CLASS_SOURCES:
        classified = [entry for entry in entries if entry.classification == classification]
        missing_count = sum(entry.loadimage in incomplete_loadimages for entry in classified)
        print(
            f"{classification}: {len(classified)} mapDefs/loadimages, "
            f"{len(classified) - missing_count} complete, {missing_count} incomplete"
        )
    if incomplete:
        print("incomplete levelshot sets:", file=sys.stderr)
        for entry, missing in incomplete:
            print(
                f"  [{entry.classification}; {entry.source}] {entry.map_def} -> {entry.loadimage}: "
                f"{', '.join(missing)}",
                file=sys.stderr,
            )
        if not args.allow_incomplete:
            return 1
    print("levelshot_inventory: ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as exc:
        print(f"levelshot_inventory: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
