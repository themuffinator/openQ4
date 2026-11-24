#!/usr/bin/env python3
"""Guard non-consuming lexer lookahead in both idlib source copies."""

from __future__ import annotations

import os
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read_legacy_source(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_bytes().decode("windows-1252")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def validate_tree(root: Path, context: str) -> None:
    header = (root / "src/idlib/Lexer.h").read_text(encoding="utf-8")
    source = read_legacy_source(root / "src/idlib/Lexer.cpp")
    declarations = re.findall(
        r"\bint\s+PeekTokenString\s*\(\s*const char\s*\*\s*string\s*\)\s*;",
        header,
    )
    if len(declarations) != 2:
        raise AssertionError(
            f"{context}: idLexer and Lexer must both expose PeekTokenString; "
            f"found {len(declarations)} declarations"
        )

    native = function_body(source, "int idLexer::PeekTokenString", context)
    for token in ("ReadToken", "UnreadToken", "tok == string"):
        if token not in native:
            raise AssertionError(f"{context}: native lookahead lost {token!r}")
    for unsafe in ("script_p = lastScript_p", "line = lastline"):
        if unsafe in native:
            raise AssertionError(f"{context}: native lookahead manually rewinds {unsafe!r}")
    if not native.index("ReadToken") < native.index("UnreadToken") < native.index("tok == string"):
        raise AssertionError(f"{context}: native lookahead must read, unread, then compare")

    wrapper = function_body(source, "int Lexer::PeekTokenString", context)
    for token in ("mDelegate->PeekTokenString", "ReadToken", "UnreadToken", "tok == string"):
        if token not in wrapper:
            raise AssertionError(f"{context}: wrapper lookahead lost {token!r}")


def main() -> None:
    validate_tree(ROOT, "engine idLexer")
    validate_tree(GAME_LIBS_ROOT, "GameLibs idLexer")

    bot_character = (GAME_LIBS_ROOT / "src/game/bots/BotCharacter.cpp").read_text(
        encoding="utf-8"
    )
    if 'lexer.PeekTokenString( "{" )' not in bot_character:
        raise AssertionError("BotCharacter parser no longer exercises lexer lookahead")

    validator = (ROOT / "tools/validation/openq4_validate.py").read_text(encoding="utf-8")
    if "lexer_peek_contract.py" not in validator:
        raise AssertionError("Validation runner does not execute the lexer lookahead contract")

    print("lexer_peek_contract: ok")


if __name__ == "__main__":
    main()
