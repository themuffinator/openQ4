#!/usr/bin/env python3
"""A medic or tech must not wait forever for a patient it cannot reach.

rvAIMedic::TakePatient makes the healer passive and blind to enemies, and
State_Medic's approach stage ends only when the healer is beside the patient
and can see it. A patient it can see but never reach -- or a stationary healer
whose patient walked off -- therefore held the healer in that stage for good,
standing in its idle pose while the map script and its tether waited (issue
#184, Tech Li on hangar1's lift).

The watchdog gives up after ai_medicApproachTimeout seconds without progress,
then holds off automatic heals for as long again. This contract checks:

* SP and MP carry the same watchdog;
* it runs first in the approach stage and leaves through the existing
  DropPatient/post-heal-script path;
* only automatic heals are held back, not one the player asks for (TalkTo);
* the watchdog fields stay out of the save, so the payload is unchanged.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TREES = ("game", "mpgame")


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


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


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def validate_tree(tree: str) -> dict[str, str]:
    cpp = read(ROOT / "src" / tree / "ai" / "AI_Medic.cpp")
    header = read(ROOT / "src" / tree / "ai" / "AI_Medic.h")
    cvars = read(ROOT / "src" / tree / "gamesys" / "SysCvar.cpp")
    cvar_header = read(ROOT / "src" / tree / "gamesys" / "SysCvar.h")
    context = f"{tree} rvAIMedic"

    if not re.search(r'idCVar ai_medicApproachTimeout\(\s*"ai_medicApproachTimeout",\s*"15"', cvars):
        raise AssertionError(f"{tree} does not define ai_medicApproachTimeout with its 15 second default")
    if "extern idCVar	ai_medicApproachTimeout;" not in cvar_header:
        raise AssertionError(f"{tree} does not declare ai_medicApproachTimeout")
    for member in ("approachDistance", "approachProgressTime", "patientRetryTime"):
        if member not in header:
            raise AssertionError(f"{context} lacks watchdog member {member}")

    stalled = strip_comments(body(cpp, "bool rvAIMedic::PatientApproachStalled( void )"))
    for token in (
        "ai_medicApproachTimeout.GetFloat()",
        "timeoutSec <= 0.0f",
        "distance < approachDistance - 8.0f",
        "approachProgressTime = gameLocal.GetTime();",
        "SEC2MS( timeoutSec )",
    ):
        if token not in stalled:
            raise AssertionError(f"{context} watchdog lacks {token!r}")
    # Movement alone is not progress: a medic shuffling beneath an unreachable
    # patient moves constantly without ever closing in.
    if "GetOrigin" in stalled:
        raise AssertionError(f"{context} watchdog must measure closing distance, not movement")

    medic = strip_comments(body(cpp, "stateResult_t rvAIMedic::State_Medic ( const stateParms_t& parms )"))
    move_stage = medic[medic.index("case STAGE_MOVE:"):medic.index("case STAGE_PRE_HEAL_ANIM_WAIT:")]
    first_statement = move_stage[len("case STAGE_MOVE:"):].lstrip()
    if not first_statement.startswith("if ( PatientApproachStalled() )"):
        raise AssertionError(f"{context} must check the watchdog before anything else in the approach stage")
    giving_up = move_stage[:move_stage.index("return SRESULT_DONE;")]
    for token in ("StopMove( MOVE_STATUS_DONE );", "DropPatient();", "patientRetryTime =", "ExecScriptFunction( mPostHealScript );"):
        if token not in giving_up:
            raise AssertionError(f"{context} gives up without {token!r}")

    think = strip_comments(body(cpp, "void rvAIMedic::Think ( void )"))
    if "gameLocal.GetTime() >= patientRetryTime && AvailableToTakePatient()" not in think:
        raise AssertionError(f"{context} does not hold back automatic heals after giving up")
    talk = strip_comments(body(cpp, "void rvAIMedic::TalkTo( idActor *actor )"))
    if "patientRetryTime" in talk:
        raise AssertionError(f"{context} must not hold back a heal the player asks for")

    take = strip_comments(body(cpp, "void rvAIMedic::TakePatient( idPlayer* pPatient )"))
    if "approachProgressTime = 0;" not in take:
        raise AssertionError(f"{context} does not restart the watchdog for a new patient")
    restore = strip_comments(body(cpp, "void rvAIMedic::Restore( idRestoreGame *savefile )"))
    if "approachProgressTime = 0;" not in restore or "patientRetryTime = 0;" not in restore:
        raise AssertionError(f"{context} does not reset the watchdog after a restore")

    save = strip_comments(body(cpp, "void rvAIMedic::Save( idSaveGame *savefile ) const"))
    for member in ("approach", "patientRetryTime"):
        if member in save or member in restore.replace("approachProgressTime = 0;", "").replace("patientRetryTime = 0;", ""):
            raise AssertionError(f"{context} serializes watchdog state; it must stay out of the save")

    return {"PatientApproachStalled": stalled, "State_Medic": medic, "Think": think}


def main() -> None:
    sp = validate_tree("game")
    mp = validate_tree("mpgame")
    for name in sp:
        if sp[name] != mp[name]:
            raise AssertionError(f"SP and MP rvAIMedic::{name} differ")
    print("ai_medic_approach_watchdog: ok")


if __name__ == "__main__":
    main()
