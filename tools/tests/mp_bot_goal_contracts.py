#!/usr/bin/env python3
"""Static contracts for multiplayer bot goal intelligence."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def main() -> None:
    header = read("src/game/bots/Bot.h")
    source = read("src/game/bots/Bot.cpp")

    # Reaching a hold objective is its own movement state. Control uses the
    # live trigger volume and rescue uses the rules' actual thaw radius.
    require(header, "AtObjectiveHoldPosition( idPlayer *self ) const;", "hold helper contract")
    require(source, "objectiveKind == BOTOBJ_CONTROL", "control hold test")
    require(source, "IntersectsBounds(", "control trigger bounds")
    require(source, 'GetInt( "si_freezeThawRadius" )', "Freeze Tag thaw radius")
    require(source, "if ( AtObjectiveHoldPosition( self ) )", "movement hold gate")
    require(source, "noRouteSince = 0;", "hold is not route failure")

    # A routine same-goal replan must not erase the longer-lived progress
    # deadline. Abandoned movement targets then honor their avoid cooldown.
    require(
        header,
        "Repath( idPlayer *self, const idVec3 &goal, bool preserveProgress = false );",
        "progress-preserving replan API",
    )
    require(source, "if ( !preserveProgress )", "progress history preservation")
    if source.count("Repath( self, goalOrigin, true )") < 3:
        raise AssertionError("Routine moving/fixed goal refreshes must preserve progress history")
    require(source, "const bool objectiveAvoided", "objective avoid cooldown")
    require(
        source,
        "const bool enemyMovementAvailable = currentFoe && !IsAvoided( currentFoe ) &&",
        "enemy movement avoid cooldown",
    )
    require(source, "if ( enemyMovementAvailable && currentFoe == lastAttacker", "attacker avoid cooldown")
    require(source, "if ( currentObjectiveValid )", "live objective hysteresis value")
    require(source, "goalUtility = objectivePriority;", "claim-adjusted objective hysteresis")
    require(source, "else if ( currentEnemyValid )", "live enemy hysteresis value")
    require(source, "goalUtility = enemyPriority;", "current enemy hysteresis")
    require(source, "if ( !gameLocal.IsTeamGame() )", "no FFA goal coordination")
    require(source, "player->team != requesterPlayer->team", "same-team reservations")

    # Owning a weapon is not itself a pickup benefit. Its ammo keys may still
    # make the duplicate useful later in ItemUtility.
    require(source, "if ( !owned )", "unowned weapon value")
    require(source, "utility = Max( utility, 5.0f );", "unowned weapon utility")
    reject(source, "owned ? 1.0f : 5.0f", "full-ammo duplicate weapon rejection")

    print("mp_bot_intelligence: ok")


if __name__ == "__main__":
    main()
