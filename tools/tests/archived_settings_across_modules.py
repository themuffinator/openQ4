#!/usr/bin/env python3
"""Pin how saved settings survive the game module that is not loaded (REL-01, 23 November 2026).

openQ4 loads one game module at a time. A config's settings for the other one
(multiplayer HUD options, field of view, bot and server settings while game_sp
runs) exist only as console cvars created by the config's ``seta`` lines until
that module registers them. The 0.16.0 candidate's clean-install and upgrade
runs (release_profile_runtime.py) showed three defects, also in 0.15.0:

* ``seta`` never archived such a cvar (id's "enable this for ship" FIXME), so
  the next config write dropped every one of them.
* The engine declared the multiplayer menu's game settings only after the
  configs ran, found the config's bare cvar, skipped it, and left it without its
  kind; the retained Player Setup could not read it.
* An invalid stored player model (the retail default.cfg's ``marine``) was
  replaced by the first model in the list, Quake 4's Failed Transfer monster,
  rather than the menu def's own default.

These are source contracts; the runtime cases ``rel01.install.clean`` and
``rel01.install.upgrade`` exercise the behaviour on a packaged build.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="latin-1").replace("\r\n", "\n")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unterminated {signature}")


def code(text: str) -> str:
    """The text without // comments, so pins never match prose."""
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def main() -> int:
    checks = 0

    def require(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    cvars = read("src/framework/CVarSystem.cpp")
    seta = code(body(cvars, "void idCVarSystemLocal::SetA_f( const idCmdArgs &args )"))
    require(re.search(r"if \( !\( cvar->GetFlags\(\) & CVAR_STATIC \) \) \{\s*cvar->flags \|= CVAR_ARCHIVE;", seta)
            is not None, "seta archives a cvar that no code has declared")
    require(seta.count("CVAR_ARCHIVE") == 1, "seta never archives a code-declared cvar")
    update = code(body(cvars, "void idInternalCVar::Update( const idCVar *cvar )"))
    require("flags |= cvar->GetFlags();" in update and "flags = cvar->GetFlags()" not in update,
            "a module's static declaration merges its flags into a console cvar, keeping its archive flag")

    common = read("src/framework/Common.cpp")
    init = code(body(common, "void idCommonLocal::InitGame( void )")) if "void idCommonLocal::InitGame( void )" in common \
        else code(common)
    declare = init.find("Session_DeclareMultiplayerMenuGameCVars();")
    exec_default = init.find('"exec default.cfg\\n"')
    exec_config = init.find('"exec " CONFIG_FILE "\\n"')
    require(declare >= 0 and exec_default > declare and exec_config > declare,
            "the multiplayer menu settings are declared before default.cfg and the config run")
    guard = init[max(0, declare - 200):declare]
    require("idAsyncNetwork::serverDedicated.GetInteger() != 1" in guard, "a dedicated server does not declare them")
    require("void Session_DeclareMultiplayerMenuGameCVars( void );" in common, "Common declares the session helper")

    session = read("src/framework/Session.cpp")
    declaration = code(body(session, "void Session_DeclareMultiplayerMenuGameCVars( void )"))
    require("static void Session_DeclareMultiplayerMenuGameCVars" not in session, "the helper is callable from Common")
    require("( existing->GetFlags() & flags ) != flags" in declaration and
            "SetCVarString( menuGameCVars[i].name, value.c_str(), flags )" in declaration,
            "an existing console cvar gains the declaration's kind and archive flag and keeps its value")
    require("Session_DeclareMultiplayerMenuGameCVars();" in code(body(session, "void idSessionLocal::Init()")),
            "Session::Init still declares them")

    menus = (
        ("src/framework/Session_menu.cpp", "static void SetMainMenuMPModelVars( idUserInterface *gui )",
         "MainMenuFirstModelFromList("),
        ("src/game/MultiplayerGame.cpp", "static void ResolveAndApplyMPMenuModelSelection(", "FirstMPMenuModelFromList("),
        ("src/game/MultiplayerGame.cpp", "static void ResolveAndApplyMPMenuModelSelection(", "FirstMPMenuModelFromList("),
    )
    for path, signature, first in menus:
        resolve = code(body(read(path), signature))
        invalid = resolve.find("if ( !selectedValid")
        default = resolve.find('"def_default_model"', invalid)
        fallback = resolve.find(first, invalid)
        require(invalid >= 0 and 0 <= default < fallback,
                f"{path}: an invalid stored model takes the def's default before the first listed model")

    print(f"archived_settings_across_modules: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
