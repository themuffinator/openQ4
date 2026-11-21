#!/usr/bin/env python3
"""A medic or tech must get its combat stance back however a heal ends.

rvAIMedic::TakePatient makes the healer passive and blind to enemies
(becomePassive( 1 )). Stock Quake 4 gave that back only when DropPatient ran
while the healer was unscripted. A heal broken off by a scripted move
(OnStartMoving -> DropPatient while scripted) or by a scripted face/anim
(SetState -> OnStateThreadClear, which never called DropPatient) left the
healer passive for good. Stock medics are unarmed, so nobody saw it; The
Awakening's Tak is an armed tech whose map scripts move him constantly, and he
then walked through firefights without returning fire.

This contract checks:

* SP and MP carry the same fix;
* TakePatient records that it made the healer passive, without overwriting
  the remembered stance when it takes over a heal already in progress;
* DropPatient and OnStateThreadClear both give the stance back, and
  DropPatient does so outside its unscripted branch;
* a script's own becomePassive/becomeAggressive takes ownership, so a heal
  ending later does not undo it;
* the new flag stays out of the save (Restore derives it), so the payload is
  unchanged.
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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_tree(tree: str) -> dict[str, str]:
    cpp = read(ROOT / "src" / tree / "ai" / "AI_Medic.cpp")
    header = read(ROOT / "src" / tree / "ai" / "AI_Medic.h")
    context = f"src/{tree}/ai/AI_Medic"

    require("bool				patientPassive;" in header, f"{context}.h does not declare patientPassive")
    require("RestorePatientCombatFlags" in header, f"{context}.h does not declare RestorePatientCombatFlags")

    events = strip_comments(cpp[:cpp.index("END_CLASS")])
    for event in ("AI_BecomePassive,", "AI_BecomeAggressive,"):
        require(event in events, f"{context} does not override {event.rstrip(',')}")

    restore_flags = strip_comments(body(cpp, "bool rvAIMedic::RestorePatientCombatFlags ( void )"))
    for token in ("if ( !patientPassive )", "patientPassive = false;",
                  "combat.fl.aware = wasAware;", "combat.fl.ignoreEnemies = wasIgnoreEnemies;"):
        require(token in restore_flags, f"{context} RestorePatientCombatFlags lacks {token!r}")

    for name in ("Event_BecomePassive ( int ignoreEnemies )", "Event_BecomeAggressive ( void )"):
        event_body = strip_comments(body(cpp, f"void rvAIMedic::{name}"))
        require("patientPassive = false;" in event_body,
                f"{context} {name} does not let the script take ownership of the stance")
        require("idAI::Event_Become" in event_body, f"{context} {name} does not chain to idAI")

    take = strip_comments(body(cpp, "void rvAIMedic::TakePatient( idPlayer* pPatient )"))
    require(re.search(r"if \( !patientPassive \) \{\s*wasAware = combat\.fl\.aware;", take) is not None,
            f"{context} TakePatient overwrites the remembered stance while already passive for a heal")
    require(take.index("ProcessEvent( &AI_BecomePassive, true );") < take.index("patientPassive = true;"),
            f"{context} TakePatient must set patientPassive after its own becomePassive")

    drop = strip_comments(body(cpp, "void rvAIMedic::DropPatient( void )"))
    require("RestorePatientCombatFlags();" in drop, f"{context} DropPatient does not restore the stance")
    require(drop.index("RestorePatientCombatFlags();") < drop.index("if ( !aifl.scripted )"),
            f"{context} DropPatient restores the stance only when unscripted")

    clear = strip_comments(body(cpp, "void rvAIMedic::OnStateThreadClear( const char *statename, int flags )"))
    require("RestorePatientCombatFlags();" in clear,
            f"{context} OnStateThreadClear drops the patient without restoring the stance")

    save = strip_comments(body(cpp, "void rvAIMedic::Save( idSaveGame *savefile ) const"))
    require("patientPassive" not in save, f"{context} saves patientPassive; the payload must not change")
    restore = strip_comments(body(cpp, "void rvAIMedic::Restore( idRestoreGame *savefile )"))
    require("patientPassive = ( patient.GetEntity() != NULL );" in restore,
            f"{context} Restore does not derive patientPassive from the patient")

    return {"RestorePatientCombatFlags": restore_flags, "TakePatient": take,
            "DropPatient": drop, "OnStateThreadClear": clear}


def main() -> None:
    sp = validate_tree("game")
    mp = validate_tree("mpgame")
    for name in sp:
        if sp[name] != mp[name]:
            raise AssertionError(f"SP and MP rvAIMedic::{name} differ")
    print("ai_medic_combat_stance_restore: ok")


if __name__ == "__main__":
    main()
