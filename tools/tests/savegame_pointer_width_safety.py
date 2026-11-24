#!/usr/bin/env python3
"""Regression checks for pointer-width safety in the savegame stream.

GitHub issue #78: ``idScriptObject::Restore`` used to declare ``size_t size``
and call ``savefile->ReadInt( (int &)size )``.  ``idFile::ReadInt`` writes
exactly four bytes, so on any LP64 target the upper four bytes of the
``size_t`` kept whatever happened to be in that stack slot.  The subsequent
size comparison and ``Read( data, size )`` then depended on uninitialized
stack, which is why x86-64 usually survived and AArch64 did not - it either
errored out or desynchronized the whole restore stream, leaving the game
spinning on a load screen until the OOM killer took it.

The savegame reader/writer pair must therefore never bind a fixed-width read to
a reference whose width is not that of the reader.
"""

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

SCRIPT_PROGRAM_SOURCES = (
    "src/game/script/Script_Program.cpp",
    "src/game/script/Script_Program.cpp",
)

# Directories whose save/restore code the check sweeps.
SWEPT_GAME_DIRS = ("src/game",)

# Casting a fixed-width read onto a reference is only width-safe when the cast
# type is itself that width.  Anything wider (or pointer-sized) silently leaves
# the high bytes untouched.
WIDE_CAST_TYPES = (
    "size_t",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "ptrdiff_t",
    "intptr_t",
    "uintptr_t",
    "time_t",
    "ID_TIME_T",
)

READ_WRITE_CALL = re.compile(
    r"\b(Read|Write)(Int|Short|Byte|Bool|Float|Signed?Char)\s*\(\s*\(\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*&\s*\)"
)


def read_game_libs(relative_path: str) -> str:
    return (GAME_LIBS_ROOT / relative_path).read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def validate_script_object_size_contract() -> None:
    for relative_path in SCRIPT_PROGRAM_SOURCES:
        source = read_game_libs(relative_path)
        context = f"{relative_path} script-object savegame size"

        # the exact shape that produced issue #78
        reject(source, "(int &)size", context)
        reject(source, "(int&)size", context)

        require(source, "int savedSize;", context)
        require(source, "savefile->ReadInt( savedSize );", context)
        require(
            source,
            "if ( savedSize < 0 ) {",
            f"{context} (a negative saved size must not reach Read())",
        )
        require(
            source,
            "static_cast<size_t>( savedSize ) != type->Size()",
            f"{context} (width-explicit comparison against the type size)",
        )


def validate_no_wide_reference_casts() -> None:
    offenders = []

    for game_dir in SWEPT_GAME_DIRS:
        directory = GAME_LIBS_ROOT / game_dir
        if not directory.is_dir():
            raise AssertionError(f"Missing game-library directory {directory}")

        for path in sorted(directory.rglob("*.cpp")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in READ_WRITE_CALL.finditer(text):
                cast_type = " ".join(match.group(3).split())
                cast_type = cast_type.replace("const ", "").strip()
                if cast_type in WIDE_CAST_TYPES:
                    line = text.count("\n", 0, match.start()) + 1
                    offenders.append(
                        f"{path.relative_to(GAME_LIBS_ROOT).as_posix()}:{line}: "
                        f"{match.group(1)}{match.group(2)} bound to a ({cast_type} &) reference"
                    )

    if offenders:
        raise AssertionError(
            "Fixed-width savegame accessors bound to wider references "
            "(only the low bytes are written; the rest stay uninitialized):\n  "
            + "\n  ".join(offenders)
        )


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    for source, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
        (runner, "validation runner"),
    ):
        require(source, "savegame_pointer_width_safety.py", context)


def main() -> None:
    validate_script_object_size_contract()
    validate_no_wide_reference_casts()
    validate_ci_smoke()
    print("savegame_pointer_width_safety: ok")


if __name__ == "__main__":
    main()
