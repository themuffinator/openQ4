#!/usr/bin/env python3
"""Pin MP-22's autoaction and run its native test.

ui_autoAction (a player) and si_autoAction (a server) name what is done for
every match: demo, ss and stats. The player's side runs on the local game's
live and result edges, the server's on its own; both name the match with
MPBuildMatchBasename. mp_autoaction_smoke.py plays it on a dedicated server.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tmp" / "mp_autoaction"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unbalanced body for {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_native() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tools/tests/native/MpAutoActionTest.cpp"
    exe = OUT / ("autoaction" + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        command = OUT / "build.cmd"
        command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                           "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                               "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", str(source),
                               "/Fo:" + str(OUT / "autoaction.obj"), "/Fe:" + str(exe)]) + "\n", encoding="utf-8")
        subprocess.run(["cmd", "/d", "/c", str(command)], cwd=OUT, check=True)
    else:
        subprocess.run([os.environ.get("CXX", shutil.which("c++") or "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=OUT, check=True)


def check_wiring() -> None:
    cvars = read("src/game/gamesys/SysCvar.cpp")
    require(re.search(r'idCVar ui_autoAction\(\s*"ui_autoAction",\s*"",\s*CVAR_GAME \| CVAR_ARCHIVE \| CVAR_NOCHEAT,', cvars)
            is not None, "ui_autoAction is the player's archived setting and empty by default")
    require(re.search(r'idCVar si_autoAction\(\s*"si_autoAction",\s*"",\s*CVAR_GAME \| CVAR_SERVERINFO \| CVAR_ARCHIVE', cvars)
            is not None, "si_autoAction is in the server info, so players see that their matches are recorded")
    header = read("src/game/mp/AutoAction.h")
    require(re.search(r"\b(?:std::)?(?:v?snprintf|strn?cmp|stricmp|strcasecmp)\s*\(", header) is None,
            "the header uses nothing idlib replaces in the game build")
    host = read("src/game/mp/AutoActionHost.cpp")
    actions = body(host, "int idMultiplayerGame::AutoActionActions( bool local ) const {")
    require("gameLocal.GetDemoState() == DEMO_PLAYING" in actions, "a demo being watched runs no autoaction")
    require("actions &= ~MP_AUTOACTION_DEMO;" in actions, "a host whose server records the match records it once")
    started = body(host, "void idMultiplayerGame::AutoActionMatchStarted( bool local ) {")
    require('va( "record \\"%s\\"\\n", autoActionName[ slot ].c_str() )' in started,
            "the player's demo uses the record command with the name quoted, which the tokenizer would split at its dash")
    require("networkSystem->ServerIsMVDRecording()" in started and "!IsManagedMatch()" in started,
            "a running recording is left alone and a managed match records itself")
    frame = body(host, "void idMultiplayerGame::AutoActionFrame( void ) {")
    require('screenshot \\"screenshots/%s.tga\\"' in frame and "WriteAutoActionStats( name );" in frame and
            '"stopRecord\\n"' in frame and 'ServerStopMVDRecording( "match ended" )' in frame,
            "the result's screenshot, stats and the end of the demo")
    state = read("src/game/mp/GameState.cpp")
    require(state.count("gameLocal.mpGame.AutoActionMatchStarted( true );") == 1 and
            state.count("gameLocal.mpGame.AutoActionMatchEnded( true );") == 1, "the player's edges are the local game's")
    require(state.count("gameLocal.mpGame.AutoActionMatchStarted( false );") == 1 and
            state.count("gameLocal.mpGame.AutoActionMatchEnded( false );") == 1, "the server's edges are its own")
    require("AutoActionFrame();" in body(read("src/game/MultiplayerGame.cpp"), "void idMultiplayerGame::CommonRun( void ) {"),
            "servers and clients both run what the result owes")


def main() -> int:
    run_native()
    check_wiring()
    print("mp_autoaction: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
