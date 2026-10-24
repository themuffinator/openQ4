#!/usr/bin/env python3
"""Turbo mode must leave script-choreographed movers at their authored timing.

Turbo doubles mover speeds, but map scripts time their own waits and actors
against a func_mover's authored move: hangar1's Viper Squad lift arrived about
3.5 s early, aiScriptedStop released Tech Li before his scripted stand-and-turn
finished, and that turn's thread then left him in an endless scripted idle
(issue #184). A plain func_mover (exactly idMover) therefore keeps its timing;
doors and platforms (idMover_Binary), elevators and the decorative movers are
still sped up.

This contract checks:

* the turbo helpers take the mover and exempt exactly idMover, compared by
  type identity, not IsType (subclasses such as func_elevator stay fast);
* every turbo call site passes the mover, so none can bypass the exemption;
* idMover's spawn timing and its script setters still go through the helpers,
  as do the door/platform paths, so only the class changes the outcome.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MOVER = ROOT / "src" / "game" / "Mover.cpp"


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature}")
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"Unbalanced body for {signature}")


def main() -> None:
    source = MOVER.read_text(encoding="utf-8")

    active = body(source, "static bool OpenQ4_TurboMoversActive( const idEntity *mover )")
    if "OpenQ4_TurboModeActive() && mover->GetType() != &idMover::GetClassType()" not in active:
        raise AssertionError("turbo must exempt exactly idMover (func_mover) by type identity")
    if "IsType" in active:
        raise AssertionError("IsType would also exempt idMover subclasses such as func_elevator")

    for helper in ("Speed", "Seconds", "Milliseconds"):
        definition = re.search(rf"static \w+ OpenQ4_TurboMover{helper}\( const idEntity \*mover, ", source)
        if definition is None:
            raise AssertionError(f"OpenQ4_TurboMover{helper} must take the mover")
        if f"OpenQ4_TurboMoversActive( mover )" not in body(source, definition.group(0)):
            raise AssertionError(f"OpenQ4_TurboMover{helper} must ask about its mover")

    calls = re.findall(r"OpenQ4_TurboMover(?:Speed|Seconds|Milliseconds)\( ([^,)]*)", source)
    stray = [arg for arg in calls if arg not in ("this", "const idEntity *mover")]
    if stray:
        raise AssertionError(f"turbo call sites must pass the mover; found {stray}")

    for signature, token in (
        ("void idMover::Spawn( void )", "move_time		= OpenQ4_TurboMoverMilliseconds( this, move_time );"),
        ("void idMover::Event_SetMoveTime( float time )", "OpenQ4_TurboMoverMilliseconds( this, SEC2MS( time ) )"),
        ("void idMover::Event_SetMoveSpeed( float speed )", "OpenQ4_TurboMoverSpeed( this, speed )"),
        ("void idMover_Binary::InitSpeed(", "OpenQ4_TurboMoverSpeed( this, mspeed ? mspeed : 100 )"),
        ("void idMover_Binary::InitTime(", "OpenQ4_TurboMoverSeconds( this, mtime )"),
    ):
        if token not in body(source, signature):
            raise AssertionError(f"{signature} no longer applies turbo through the mover-aware helper")

    print("turbo_scripted_movers: ok")


if __name__ == "__main__":
    main()
