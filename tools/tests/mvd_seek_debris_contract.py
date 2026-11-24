#!/usr/bin/env python3
"""Guards MVD seeks dropping client-only debris instead of freezing it.

A multi-view demo seek replays snapshot records without running game frames. Gibs,
brass and BSE debris are client moveables whose physics and fade-out only advance
on frames, so those spawned during the replay stayed frozen where they spawned:
clusters of gibs inside a body, often at the death camera, which showed as the
distorted foreground debris the Red Rover seek finding recorded.

The checks pin the parts no compiler sees together:

* the engine publishes a read-only mvd_seeking exactly while seekInProgress is
  set, at every place that flag changes;
* the game reads it only during server demo playback;
* every client moveable spawned while it is set removes itself on the first
  event service, which the presentation frame ending the seek runs before
  anything is drawn.

Single-reversal controls then mutate the sources and require each check to fail.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

SOURCES = {
    "mvd": ROOT / "src" / "framework" / "async" / "MultiViewDemo.cpp",
    "game_local": GAME_LIBS_ROOT / "src" / "game" / "Game_local.h",
    "moveable": GAME_LIBS_ROOT / "src" / "game" / "client" / "ClientMoveable.cpp",
    "game_network": GAME_LIBS_ROOT / "src" / "game" / "Game_network.cpp",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r}")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[brace + 1:end - 1]


def validate(sources: dict[str, str]) -> None:
    mvd = sources["mvd"]
    require(mvd, '"mvd_seeking", "0",\n\tCVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT | CVAR_ROM,', "the read-only seek flag")
    # Every change of seekInProgress publishes the same value. ClearState() resets the
    # members alone, because the global instance's constructor runs during static
    # initialization, before mvd_seeking may exist (every Linux start crashed); Clear()
    # publishes right after it.
    clear_state = body(mvd, "void idMultiViewDemo::ClearState()")
    constructor = body(mvd, "idMultiViewDemo::idMultiViewDemo()")
    if "mvd_seeking." in clear_state or "mvd_seeking." in constructor or re.search(r"\bClear\(\);", constructor):
        raise AssertionError("the constructor path touches mvd_seeking during static initialization")
    require(body(mvd, "void idMultiViewDemo::Clear()"), "ClearState();\n\tmvd_seeking.SetBool( false );",
            "Clear publishes the cleared seek flag")
    changes = re.findall(r"seekInProgress = (true|false);\n\s*(mvd_seeking\.SetBool\( (true|false) \);)?",
                         mvd.replace(clear_state, ""))
    if len(changes) < 3:
        raise AssertionError(f"expected at least three seekInProgress changes besides ClearState, found {len(changes)}")
    for value, published, published_value in changes:
        if not published or published_value != value:
            raise AssertionError(f"seekInProgress = {value} is not published to mvd_seeking")

    require(sources["game_local"],
            'bool\t\t\t\t\tIsServerDemoSeeking( void ) const { return IsServerDemoPlaying() && cvarSystem->GetCVarBool( "mvd_seeking" ); }',
            "the game reads the flag only during server demo playback")
    spawn = body(sources["moveable"], "void rvClientMoveable::Spawn ( void )")
    require(spawn, "if ( gameLocal.IsServerDemoSeeking() ) {\n\t\tPostEventMS( &EV_Remove, 0 );\n\t}",
            "a client moveable spawned during a seek removes itself")
    # The presentation frame services events before the frame is drawn.
    prediction = body(sources["game_network"],
                      "gameReturn_t idGameLocal::ClientPrediction( int clientNum, const usercmd_t *clientCmds, bool lastPredictFrame, ClientStats_t *cs )")
    require(prediction, "idEvent::ServiceEvents();", "client prediction services pending events")


def expect_failure(sources: dict[str, str], label: str) -> None:
    try:
        validate(sources)
    except AssertionError:
        return
    raise AssertionError(f"control did not fail: {label}")


CONTROLS = (
    ("the console can set the seek flag", "mvd",
     '"mvd_seeking", "0",\n\tCVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT | CVAR_ROM,', '"mvd_seeking", "0",\n\tCVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,'),
    ("a seek's start is not published", "mvd",
     "\tseekInProgress = true;\n\tmvd_seeking.SetBool( true );", "\tseekInProgress = true;"),
    ("a seek that lands is not cleared", "mvd",
     "\t\tseekInProgress = false;\n\t\tmvd_seeking.SetBool( false );\n\t\tplaybackGameTime = seekTargetGameTime;",
     "\t\tseekInProgress = false;\n\t\tplaybackGameTime = seekTargetGameTime;"),
    ("the constructor publishes during static initialization", "mvd",
     "\tClearState();\n}", "\tClear();\n}"),
    ("the game ignores the flag", "moveable", "if ( gameLocal.IsServerDemoSeeking() ) {", "if ( false ) {"),
    ("live play reads the flag too", "game_local",
     'return IsServerDemoPlaying() && cvarSystem->GetCVarBool( "mvd_seeking" );', 'return cvarSystem->GetCVarBool( "mvd_seeking" );'),
)


def main() -> int:
    try:
        sources = {name: read(path) for name, path in SOURCES.items()}
        validate(sources)
        for label, name, original, mutated in CONTROLS:
            if sources[name].count(original) != 1:
                raise AssertionError(f"control anchor is not unique: {label}")
            mutated_sources = dict(sources)
            mutated_sources[name] = sources[name].replace(original, mutated, 1)
            expect_failure(mutated_sources, label)
    except AssertionError as error:
        print(f"mvd_seek_debris_contract: FAILED - {error}")
        return 1
    print(f"mvd_seek_debris_contract: ok ({len(CONTROLS)} single-reversal controls fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
