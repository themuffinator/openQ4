#!/usr/bin/env python3
"""Regression checks for idClass allocation alignment in openQ4-game."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()
CLASS_SOURCES = (
    "src/game/gamesys/Class.cpp",
    "src/game/gamesys/Class.cpp",
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    return (GAME_LIBS_ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_class_allocator_source(relative_path: str) -> None:
    source = read_game_libs(relative_path)

    for token in (
        "static const int IDCLASS_ALLOC_HEADER_SIZE = 16;",
        "static byte *idClass_AllocBlock( size_t objectSize )",
        "static byte *idClass_BlockFromObject( void *ptr )",
        "objectSize > ID_HEAP_MAX_SIZE - IDCLASS_ALLOC_HEADER_SIZE",
        "const size_t totalSize = objectSize + IDCLASS_ALLOC_HEADER_SIZE;",
        "Mem_Alloc16( totalSize, MA_CLASS )",
        "if ( block == NULL )",
        "*reinterpret_cast<int *>( block ) = static_cast<int>( totalSize );",
        "block + IDCLASS_ALLOC_HEADER_SIZE",
        "return static_cast<byte *>( ptr ) - IDCLASS_ALLOC_HEADER_SIZE;",
        "size_t\tidClass::memused",
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
        "const size_t totalSize = s + IDCLASS_ALLOC_HEADER_SIZE;",
    ):
        reject(source, legacy_token, relative_path)


def validate_validation_wiring() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require(validator, "game_class_allocator_alignment.py", "validation runner")
    for workflow, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(workflow, "tools/tests/game_class_allocator_alignment.py", context)
        if workflow.count("tools/tests/game_class_allocator_alignment.py") < 2:
            raise AssertionError(f"{context} should compile and run game_class_allocator_alignment.py")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux ARM64 save-game loading no longer crashes", "release completion notes")
    require(source, "16-byte aligned", "release completion notes")
    require(source, "restoring fresh saves", "release completion notes")


def main() -> None:
    for relative_path in CLASS_SOURCES:
        validate_class_allocator_source(relative_path)
    validate_validation_wiring()
    validate_release_note()
    print("game_class_allocator_alignment: ok")


if __name__ == "__main__":
    main()
