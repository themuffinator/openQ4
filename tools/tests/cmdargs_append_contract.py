#!/usr/bin/env python3
"""Guard command-argument slot, token-buffer, and escaping boundaries."""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()
MAX_COMMAND_ARGS = 64
MAX_COMMAND_STRING = 2 * 4096


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def append_model(argc: int, next_offset: int | None) -> tuple[int, bool]:
    if argc < 0 or argc > MAX_COMMAND_ARGS:
        argc = 0
    if argc == 0:
        return 1, True
    if argc >= MAX_COMMAND_ARGS:
        return argc, False
    if next_offset is None or next_offset < 0 or next_offset >= MAX_COMMAND_STRING:
        return argc, False
    return argc + 1, True


def validate_tree(root: Path, context: str) -> None:
    source = (root / "src/idlib/CmdArgs.cpp").read_text(encoding="utf-8")
    args_body = function_body(source, "const char *idCmdArgs::Args", context)
    append_body = function_body(source, "void idCmdArgs::AppendArg", context)

    if "static idStr cmd_args" not in source:
        raise AssertionError(f"{context}: missing dynamic idCmdArgs::Args scratch storage")
    if not any(clear in args_body for clear in ("cmd_args.Clear()", "cmd_args.SecureClear()")):
        raise AssertionError(f"{context}: idCmdArgs::Args does not clear its scratch storage")

    for needle in (
        "if ( start < 0 )",
        'cmd_args += "\\\\\\\\"',
        "cmd_args += *p",
        "return cmd_args.c_str()",
    ):
        if needle not in args_body:
            raise AssertionError(f"{context}: missing {needle!r} in idCmdArgs::Args")
    for unsafe in ("static char cmd_args", "strcat(", "cmd_args[ l+1 ]"):
        if unsafe in args_body:
            raise AssertionError(f"{context}: fixed-buffer assembly remains: {unsafe!r}")

    for needle in (
        "if ( !text )",
        "if ( argc < 0 || argc > MAX_COMMAND_ARGS )",
        "if ( argc >= MAX_COMMAND_ARGS )",
        "next >= tokenized + sizeof( tokenized )",
        "const int remaining = idLib::SizeToInt",
        "idStr::Copynz( argv[ argc ], text, remaining )",
    ):
        if needle not in append_body:
            raise AssertionError(f"{context}: missing {needle!r} in idCmdArgs::AppendArg")

    slot_guard = append_body.index("if ( argc >= MAX_COMMAND_ARGS )")
    buffer_guard = append_body.index("next >= tokenized + sizeof( tokenized )")
    pointer_install = append_body.index("argv[ argc ] = next")
    if not slot_guard < buffer_guard < pointer_install:
        raise AssertionError(f"{context}: guards must precede argv installation")


def validate_auto_exec_capture_boundary() -> None:
    for relative_path in ("src/game/Game_local.cpp", "src/game/Game_local.cpp"):
        source = (GAME_LIBS_ROOT / relative_path).read_text(encoding="utf-8")
        body = function_body(
            source,
            "void idGameLocal::CheckAutoExecAfterMapLoad",
            relative_path,
        )
        safe_dispatch = "BufferCommandArgs( CMD_EXEC_APPEND, execArgs )"
        unsafe_dispatch = "BufferCommandArgs( CMD_EXEC_NOW, execArgs )"
        if safe_dispatch not in body:
            raise AssertionError(
                f"{relative_path}: post-map cfg must be queued outside the active draw"
            )
        if unsafe_dispatch in body:
            raise AssertionError(
                f"{relative_path}: post-map cfg still executes synchronously during Draw"
            )


def generate_native(output: Path) -> None:
    """Compile the real argument ownership methods with the real list container."""
    implementation = ROOT / "src/idlib/CmdArgs.cpp"
    source = implementation.read_text(encoding="utf-8")
    lib = (ROOT / "src/idlib/Lib.h").read_text(encoding="utf-8")
    capacity = re.search(r"#define\s+MAX_STRING_CHARS\s+(\d+)", lib)
    if capacity is None:
        raise AssertionError("production command string capacity is missing")
    result = f'#define MAX_STRING_CHARS {capacity[1]}\n#include "src/idlib/CmdArgs.h"\n'
    for signature in (
        "void idCmdArgs::operator=( const idCmdArgs &args )",
        "void idCmdArgs::AppendArg( const char *text )",
        "void idCmdArgs::ClearSensitive( void )",
    ):
        line = source.count("\n", 0, source.index(signature)) + 1
        result += f'\n#line {line} "{implementation.as_posix()}"\n'
        result += signature + " {\n" + function_body(source, signature, "native command arguments") + "\n}\n"
    output.write_text(result, encoding="utf-8")


def main() -> None:
    validate_tree(ROOT, "engine idCmdArgs")
    validate_tree(GAME_LIBS_ROOT, "GameLibs idCmdArgs")
    validate_auto_exec_capture_boundary()

    argc = 0
    for _ in range(MAX_COMMAND_ARGS):
        argc, installed = append_model(argc, 1)
        if not installed:
            raise AssertionError("A valid argument was unexpectedly rejected")
    argc_after_65th, installed = append_model(argc, 1)
    if argc_after_65th != MAX_COMMAND_ARGS or installed:
        raise AssertionError("The 65th command argument must be ignored")

    argc_after_full, installed = append_model(1, MAX_COMMAND_STRING)
    if argc_after_full != 1 or installed:
        raise AssertionError("A one-past token-buffer pointer must never be installed")

    near_capacity = "\\" * (MAX_COMMAND_STRING - 1)
    escaped = '"' + near_capacity.replace("\\", "\\\\") + '"'
    if len(escaped) != 2 * (MAX_COMMAND_STRING - 1) + 2:
        raise AssertionError("Backslash escaping no longer doubles every slash")
    if len(escaped) <= MAX_COMMAND_STRING:
        raise AssertionError("Near-capacity escaping must grow beyond the old fixed buffer")

    validator = (ROOT / "tools/validation/openq4_validate.py").read_text(encoding="utf-8")
    if "cmdargs_append_contract.py" not in validator:
        raise AssertionError("Validation runner does not execute the command-argument contract")

    print("cmdargs_append_contract: ok")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--generate-native":
        generate_native(Path(sys.argv[2]))
    elif len(sys.argv) == 1:
        main()
    else:
        raise SystemExit("usage: cmdargs_append_contract.py [--generate-native OUTPUT]")
