#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static ARM64 ABI guardrails for openQ4 GameLibs."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_TREES = ("game", "mpgame")
CLASS_SOURCES = (
    "src/game/gamesys/Class.cpp",
    "src/mpgame/gamesys/Class.cpp",
)


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> None:
    if re.search(pattern, haystack, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"Missing pattern {pattern!r} in {context}")


def reject_regex(haystack: str, pattern: str, context: str) -> None:
    match = re.search(pattern, haystack, re.MULTILINE | re.DOTALL)
    if match is not None:
        raise AssertionError(f"Unexpected pattern {pattern!r} in {context}: {match.group(0)!r}")


def validate_class_allocator_source(relative_path: str) -> None:
    source = read(relative_path)

    for token in (
        "static const int IDCLASS_ALLOC_HEADER_SIZE = 16;",
        "static byte *idClass_AllocBlock( size_t objectSize )",
        "static byte *idClass_BlockFromObject( void *ptr )",
        "Mem_Alloc16( totalSize, MA_CLASS )",
        "*reinterpret_cast<int *>( block ) = static_cast<int>( totalSize );",
        "block + IDCLASS_ALLOC_HEADER_SIZE",
        "return static_cast<byte *>( ptr ) - IDCLASS_ALLOC_HEADER_SIZE;",
        "objectSize > ID_HEAP_MAX_SIZE - IDCLASS_ALLOC_HEADER_SIZE",
        'gameLocal.Error( "idClass allocation is too large (%zu bytes)", objectSize );',
        'throw idAllocError( "idClass allocation exceeds the 32-bit heap limit" );',
        "if ( block == NULL )",
        'throw idAllocError( "idClass allocation failed" );',
        "memused += static_cast<size_t>( *reinterpret_cast<int *>( p ) );",
        "return p + IDCLASS_ALLOC_HEADER_SIZE;",
        "p = idClass_BlockFromObject( ptr );",
        "Mem_Free16( p );",
    ):
        require(source, token, relative_path)

    for legacy_token in (
        "s += sizeof( int );",
        "return p + 1;",
        "p = ( ( int * )ptr ) - 1;",
        "unsigned long *ptr = ( ( unsigned long * )this ) - 1;",
        "Mem_Alloc( s, MA_CLASS )",
        "Mem_Free( p );",
    ):
        reject(source, legacy_token, relative_path)


def validate_script_pointer_width_fields(tree: str) -> None:
    program = read(f"src/{tree}/script/Script_Program.cpp")
    interpreter = read(f"src/{tree}/script/Script_Interpreter.cpp")
    event_header = read(f"src/{tree}/gamesys/Event.h")
    event_source = read(f"src/{tree}/gamesys/Event.cpp")
    class_source = read(f"src/{tree}/gamesys/Class.cpp")

    for name in ("scriptevent", "namespace", "function", "pointer"):
        require_regex(
            program,
            rf"idTypeDef\s+type_{name}\s*\([^;\n]*sizeof\s*\(\s*intptr_t\s*\)",
            f"{tree} script type_{name} pointer width",
        )
        reject_regex(
            program,
            rf"idTypeDef\s+type_{name}\s*\([^;\n]*sizeof\s*\(\s*int\s*\)",
            f"{tree} script type_{name} pointer width",
        )

    require(event_header, "sizeof(intptr_t)", f"{tree} event vector alignment")
    require_regex(
        event_header,
        r"CopyArgs\s*\([^;\n]*intptr_t\s+data\s*\[\s*D_EVENT_MAXARGS\s*\]",
        f"{tree} event CopyArgs pointer-width data",
    )
    require_regex(
        class_source,
        r"intptr_t\s+data\s*\[\s*D_EVENT_MAXARGS\s*\]",
        f"{tree} idClass event stack data",
    )
    require_regex(
        class_source,
        r"ProcessEventArgPtr\s*\([^;\n]*intptr_t\s*\*\s*data",
        f"{tree} idClass ProcessEventArgPtr pointer-width data",
    )

    interpreter_data_arrays = re.findall(r"intptr_t\s+data\s*\[\s*D_EVENT_MAXARGS\s*\]", interpreter)
    if len(interpreter_data_arrays) < 2:
        raise AssertionError(f"{tree} script interpreter should keep both event data stacks intptr_t-sized")

    for token in (
        "static void idEvent_WriteIntPtr( idSaveGame *savefile, const intptr_t value )",
        "static intptr_t idEvent_ReadIntPtr( idRestoreGame *savefile )",
        "static const size_t EVENT_ARG_ALIGNMENT = alignof( trace_t ) > alignof( intptr_t )",
        'static_assert( EVENT_ARG_ALIGNMENT <= 16, "event data allocator alignment is insufficient" );',
        "static const size_t EVENT_TRACE_DATA_OFFSET =",
        "dataPtr + EVENT_TRACE_DATA_OFFSET",
        "dataPtr + EVENT_TRACE_MATERIAL_OFFSET",
        "static size_t idEvent_GetLegacyArgLayout",
        "static void idEvent_ConvertLegacyRawArgs",
        "memcpy( destPtr + EVENT_TRACE_DATA_OFFSET, sourcePtr + sizeof( bool ), sizeof( trace_t ) );",
        "legacyArgSize = idEvent_GetLegacyArgLayout",
        "if ( static_cast<size_t>( argsize ) != legacyArgSize )",
        "idEvent_ConvertLegacyRawArgs( savefile, event->eventdef, event->data, legacyData.get(), legacyOffsets );",
        'throw idAllocError( "idEvent data exceeds the 32-bit heap limit" );',
        'throw idAllocError( "restored idEvent data exceeds the 32-bit heap limit" );',
        'throw idAllocError( "restored legacy idEvent data exceeds the 32-bit heap limit" );',
        "if ( ev->data == NULL )",
        "if ( legacyData == NULL )",
        "case D_EVENT_INTEGER64bit",
        "idEvent_WriteIntPtr( savefile, *reinterpret_cast<intptr_t *>( dataPtr ) );",
        "*reinterpret_cast<intptr_t *>( dataPtr ) = idEvent_ReadIntPtr( savefile );",
    ):
        require(event_source, token, f"{tree} event intptr save/restore")

    require(event_header, "size_t\t\t\t\t\t\targOffset", f"{tree} event argument offsets")
    if event_source.count("if ( event->data == NULL )") != 2:
        raise AssertionError(f"{tree} typed and legacy restore allocations must both be checked")
    reject(event_source, "dataPtr + sizeof( bool )", f"{tree} unaligned trace payload")
    reject(event_source, "static_cast<size_t>( argsize ) == eventArgSize", f"{tree} ambiguous legacy raw event layout")


def validate_savegame_object_serialization(tree: str) -> None:
    source = read(f"src/{tree}/gamesys/SaveGame.cpp")

    for token in (
        "void idSaveGame::WriteObject( const idClass *obj )",
        "index = SaveGame_FindObjectIndex( objects, objectHash, obj );",
        "WriteInt( index );",
        "void idRestoreGame::ReadObject( idClass *&obj )",
        "ReadInt( index );",
        "obj = objects[ index ];",
        "invalid object index",
    ):
        require(source, token, f"{tree} savegame object index serialization")

    for pattern in (
        r"Write(?:Int|UnsignedInt|Long|UnsignedLong)\s*\([^;\n]*(?:reinterpret_cast|static_cast)\s*<\s*(?:u?intptr_t|u?long|u?int)\s*>\s*\(\s*obj\s*\)",
        r"WriteData\s*\([^;\n]*&\s*obj\s*,\s*sizeof\s*\(\s*obj\s*\)",
        r"file->Write\s*\([^;\n]*&\s*obj\s*,\s*sizeof\s*\(\s*obj\s*\)",
    ):
        reject_regex(source, pattern, f"{tree} savegame object serialization must not write raw pointer values")


def validate_alignment_sensitive_allocations(tree: str) -> None:
    required_sources = {
        f"src/{tree}/gamesys/Class.cpp": (
            "IDCLASS_ALLOC_HEADER_SIZE = 16",
            "Mem_Alloc16",
            "Mem_Free16",
        ),
        f"src/{tree}/anim/Anim_Blend.cpp": (
            "Mem_Alloc16",
            "Mem_Free16",
            "_alloca16",
        ),
        f"src/{tree}/AFEntity.cpp": (
            "idJointMat",
            "idMat3",
            "_alloca16",
        ),
        f"src/{tree}/BrittleFracture.cpp": (
            "srfTriangles_t",
            "idDrawVert",
            "_alloca16",
        ),
        f"src/{tree}/Entity.cpp": (
            "idJointMat",
            "_alloca16",
        ),
        f"src/{tree}/IK.cpp": (
            "idJointMat",
            "_alloca16",
        ),
        f"src/{tree}/ai/AAS_pathing.cpp": (
            "wallEdge_t",
            "_alloca16",
        ),
    }

    for relative_path, tokens in required_sources.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"{tree} alignment-sensitive allocation contract in {relative_path}")

    physics_path = ROOT / "src" / tree / "physics" / "Physics_AF.cpp"
    if physics_path.is_file():
        physics_source = physics_path.read_text(encoding="utf-8")
        require(physics_source, "_alloca16", f"{tree} articulated-figure physics scratch allocation")


def main() -> None:
    for relative_path in CLASS_SOURCES:
        validate_class_allocator_source(relative_path)
    for tree in GAME_TREES:
        validate_script_pointer_width_fields(tree)
        validate_savegame_object_serialization(tree)
        validate_alignment_sensitive_allocations(tree)
    # Unified build/CI coverage is owned by game_regression_suite and platform tests.
    print("arm64_abi_contract: ok")


if __name__ == "__main__":
    main()
