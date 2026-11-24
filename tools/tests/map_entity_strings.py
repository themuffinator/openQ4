#!/usr/bin/env python3
"""Guard the runtime map entity-string replacement/extender contract."""

from __future__ import annotations

import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"{context} is missing {needle!r}")


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        position = text.find(needle, cursor + 1)
        if position < 0:
            raise AssertionError(f"{context} is missing {needle!r}")
        if position <= cursor:
            raise AssertionError(f"{context} has {needle!r} out of order")
        cursor = position


def between(text: str, start: str, end: str, context: str) -> str:
    start_at = text.find(start)
    if start_at < 0:
        raise AssertionError(f"{context} is missing start marker {start!r}")
    end_at = text.find(end, start_at + len(start))
    if end_at < 0:
        raise AssertionError(f"{context} is missing end marker {end!r}")
    return text[start_at:end_at]


def validate_mapfile(root: Path, label: str) -> None:
    header = read(root / "src" / "idlib" / "MapFile.h")
    source = read(root / "src" / "idlib" / "mapfile.cpp")

    require(header, "ApplyEntityStringFiles( void )", f"{label} MapFile API")
    require(header, "fromEntityStringFile", f"{label} sidecar ownership marker")

    parser = between(
        source,
        "idMapFile::ParseEntityStringFile",
        "idMapFile::ApplyEntityStringFiles",
        f"{label} entity-string parser",
    )
    for needle, contract in (
        ("MAX_ENTITY_STRING_FILE_BYTES = 16 * 1024 * 1024", "bounded file size"),
        ("MAX_ENTITY_STRING_FILE_ENTITIES = 4096", "bounded entity count"),
        ("MAX_ENTITY_STRING_FILE_KEYS = 4096", "bounded key count"),
        ("bytesRead != extensionLength", "complete-read validation"),
        ("contains an embedded NUL byte", "embedded-NUL rejection"),
        ("0xEF", "UTF-8 BOM handling"),
        ("duplicate key", "duplicate-key rejection"),
        ("duplicate entity name", "duplicate-name rejection"),
        ("must begin with worldspawn", ".ent worldspawn requirement"),
        ("worldspawn cannot be added by an .entx extender", ".entx worldspawn rejection"),
        ("brushes and patches are not supported", "point-entity-only parsing"),
        ("'spawn_entnum' is engine-owned", "reserved-key rejection"),
        ("parsedEntities.DeleteContents( true )", "atomic parse cleanup"),
    ):
        require(parser, needle, f"{label} {contract}")

    apply = between(
        source,
        "idMapFile::ApplyEntityStringFiles",
        "idMapFile::Parse\n",
        f"{label} entity-string application",
    )
    require_order(
        apply,
        (
            'ParseEntityStringFile( "ent", true',
            'ParseEntityStringFile( "entx", false',
            "entities.DeleteContents( true )",
            "entities.Append( replacementEntities[ i ] )",
            "entities.Append( extenderEntities[ i ] )",
            "entityStringFilesEnabled = true",
        ),
        f"{label} replacement-before-extension order",
    )
    for needle, contract in (
        ("const idList<idMapEntity *> &activeEntities", "active-string collision checking"),
        ("Invalid entity-string extender", "cross-file duplicate rejection"),
        ("if ( entityStringFilesEnabled )", "idempotent application"),
        ("replacementEntityFileLoaded", "replacement reload state"),
        ("extenderEntityFileLoaded", "extender reload state"),
    ):
        require(apply, needle, f"{label} {contract}")

    parse = between(
        source,
        "idMapFile::Parse\n",
        "void idMapFile::Resolve",
        f"{label} base map parser",
    )
    if "ApplyEntityStringFiles" in parse:
        raise AssertionError(
            f"{label} base parser must not apply runtime entity strings before collision loading"
        )

    writer = between(
        source,
        "bool idMapFile::Write(",
        "idMapFile::SetGeometryCRC",
        f"{label} map writer",
    )
    require(writer, "replacementEntityFileLoaded", f"{label} replacement write guard")
    require(writer, "IsFromEntityStringFile()", f"{label} extender write filtering")

    reload = between(
        source,
        "bool idMapFile::NeedsReload()",
        "bool idMapFile::WriteExport",
        f"{label} reload tracking",
    )
    require(reload, 'SetFileExtension( "ent" )', f"{label} .ent reload tracking")
    require(reload, 'SetFileExtension( "entx" )', f"{label} .entx reload tracking")


def validate_game_mode(path: Path, label: str) -> None:
    source = read(path)
    apply_at = source.find("mapFile->ApplyEntityStringFiles()")
    if apply_at < 0:
        raise AssertionError(f"{label} does not apply runtime entity-string files")
    load_at = source.rfind("collisionModelManager->LoadMap", 0, apply_at)
    if load_at < 0 or load_at > apply_at:
        raise AssertionError(f"{label} must build collision before replacing entities")
    require(source, "entityStrings=%d", f"{label} load-time profiling")
    require(source, "Map entity string contains %d entities; the runtime limit is %d", f"{label} runtime entity cap")


def validate_docs() -> None:
    docs = read(ROOT / "docs" / "user" / "map-entity-strings.md")
    for needle in (
        "`.ent` is a complete replacement",
        "`.entx` appends",
        "does not rewrite the original `.map`",
        "worldspawn",
        "16 MiB",
        "4,096 entities",
    ):
        require(docs, needle, "map entity-string guide")


def main() -> int:
    try:
        validate_mapfile(ROOT, "engine")
        validate_mapfile(GAME_ROOT, "game-library")
        validate_game_mode(GAME_ROOT / "src" / "game" / "Game_local.cpp", "single-player")
        validate_game_mode(GAME_ROOT / "src" / "game" / "Game_local.cpp", "multiplayer")
        validate_docs()
    except AssertionError as error:
        print(f"map_entity_strings: FAILED - {error}")
        return 1

    print("map_entity_strings: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
