#!/usr/bin/env python3
"""Static contracts for mode-aware multiplayer bot objective discovery."""

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


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(
            f"Expected {first!r} before {second!r} in {context}"
        )


def main() -> None:
    header = read("src/game/bots/BotObjective.h")
    source = read("src/game/bots/BotObjective.cpp")

    # Stable integration interface: the bot brain consumes a single snapshot
    # without taking a dependency on concrete game-state classes.
    for token in (
        "BOTOBJ_NONE",
        "BOTOBJ_CAPTURE",
        "BOTOBJ_INTERCEPT",
        "BOTOBJ_RETURN",
        "BOTOBJ_ESCORT",
        "BOTOBJ_FETCH",
        "BOTOBJ_RESCUE",
        "BOTOBJ_CONTROL",
        "BOTOBJ_DEFEND",
        "idEntityPtr<idEntity>\tentity;",
        "float\t\t\t\t\tpriority;",
        "bool\t\t\t\t\tholdPosition;",
        "bool BotFindObjective( idPlayer *self, botObjective_t &out );",
    ):
        require(header, token, "BotObjective public contract")

    # No mode objective may cross a Tourney/world-instance boundary, and stale
    # or non-playing clients cannot become carriers, escorts, or rescue goals.
    require(
        source,
        "other->GetInstance() == self->GetInstance()",
        "same-instance objective filter",
    )
    require(source, "gameLocal.mpGame.CanPlay( other )", "live player filter")
    require(source, "gameLocal.mpGame.CanPlay( candidate )", "frozen player filter")
    require(source, "other->wantSpectate", "departing-player filter")
    require(source, "matchState == GAMEON || matchState == SUDDENDEATH", "live match gate")

    # Role selection is deterministic, distance-aware and bot-only.  Separate
    # masks let return, intercept, escort and attack roles consume different
    # players rather than sending the whole team at the first candidate.
    for token in (
        "BotObjectiveRolePlayer",
        "botManager.IsBot( candidate->entityNumber )",
        "BotObjectiveSelectClosestRolePlayers",
        "candidate->entityNumber < best->entityNumber",
        "BotObjectiveMergeRoles",
        "BotObjectiveMajoritySlots",
        "BotObjectiveSupportSlots",
        "BotObjectiveAssignUniqueTarget",
        "const bool *blocked",
        "bool assigned[MAX_CLIENTS]",
    ):
        require(source, token, "team-role allocation")

    # CTF uses authoritative state where it is valid, while One Flag discovers
    # TEAM_MAX through entities/powerups and never indexes the two-team arrays.
    # Assault points retain their authored index order but are selected from the
    # querying player's instance instead of the manager's global NextAP result.
    for token in (
        "BotObjectiveNextAssaultPoint( self, ownTeam )",
        "gameLocal.mpGame.assaultPoints[i].GetEntity()",
        "BotObjectiveSameInstance( self, point )",
        "point->GetOwner() == team",
        "team == TEAM_MARINE && index < bestIndex",
        "team == TEAM_STROGG && index > bestIndex",
        "state->GetFlagState( ownTeam ) == FS_DROPPED",
        'candidate->spawnArgs.GetBool( "dropped", "0" )',
        "gameLocal.mpGame.GetFlagEntity( flagTeam )",
        "POWERUP_CTF_ONEFLAG",
        "const int fetchTeam = oneFlag ? TEAM_MAX : enemyTeam;",
    ):
        require(source, token, "CTF objective contract")
    reject(source, "GetFlagEntity( TEAM_MAX )", "One Flag objective safety")
    require_before(
        source,
        "if ( self->PowerUpActive( carriedPowerup ) )",
        "idPlayer *enemyCarrier = BotObjectiveTeamCarrier",
        "carrier capture before team roles",
    )
    # The carrier every role query is measured against has to be the same one for
    # every bot on the team, or BotObjectiveSelectClosestRolePlayers cannot keep
    # its "same mask for every bot" promise.  A nearest-to-me carrier is fine
    # while only one flag of each colour exists, but DeadZone runs several
    # artifacts at once and each bot would then solve a different allocation.
    reject(source, "BotObjectiveNearestCarrier", "team-global carrier selection")
    reject(source, "gameLocal.mpGame.NextAP( ownTeam )", "instance-local assault routing")
    require_before(
        source,
        "BOTOBJ_PRIORITY_INTERCEPT",
        "BOTOBJ_PRIORITY_RETURN",
        "CTF priority ordering",
    )
    for token in (
        "BOTOBJ_PRIORITY_CAPTURE\t= 100.0f",
        "BOTOBJ_PRIORITY_CARRIER\t= 100.0f",
        "BOTOBJ_PRIORITY_INTERCEPT\t= 90.0f",
        "BOTOBJ_PRIORITY_RESCUE\t= 88.0f",
        "BOTOBJ_PRIORITY_RETURN\t= 85.0f",
        "BOTOBJ_PRIORITY_ESCORT\t= 72.0f",
        "BOTOBJ_PRIORITY_CONTROL\t= 68.0f",
        "BOTOBJ_PRIORITY_FETCH\t= 60.0f",
        "BOTOBJ_PRIORITY_DEFEND\t= 54.0f",
    ):
        require(source, token, "shared 0-100 utility scale")

    # Urgent returners and interceptors are selected first, support consumes
    # only the remaining mask, and deliberately unassigned players defend the
    # authored capture end rather than joining a flag dogpile.
    for token in (
        "available >= 5 ? 2 : 1",
        "BOTOBJ_PRIORITY_RETURN + 9.0f",
        "friendlyCarrier ? BotObjectiveSupportSlots( available )",
        "BOTOBJ_PRIORITY_INTERCEPT + urgency",
        "carrierHurt",
        "escorts = Min( available, escorts + 1 )",
        "available <= 2 ? available : BotObjectiveMajoritySlots( available )",
        "if ( !assigned[selfNum] && base",
        "BOTOBJ_PRIORITY_DEFEND + ( enemyCarrier ? 10.0f : 0.0f )",
        "BOTOBJ_DEFEND, base, baseOrigin",
    ):
        require(source, token, "CTF role coverage")

    # Freeze Tag bodies deliberately remain non-spectating dead players. The
    # round-live gate prevents rescue routes during setup/review. Global unique
    # matching spreads the nearest bots across different frozen bodies, and
    # urgency rises when most of the team is frozen or enemies guard the thaw.
    # Roles go to whoever has the shortest run, not the shortest line: the
    # landmark route bound sees floors and walls in constant time.
    require(source, "navMesh.RouteLowerBound( from, to )", "route-aware role distance")
    require(source, "botManager.ClientNavNode( candidate->entityNumber )", "cached player nodes for roles")
    require(source, "state->RoundIsLive()", "Freeze Tag live-round gate")
    require(source, "candidate->health <= 0", "Freeze Tag frozen-body test")
    require(source, "BotObjectiveAssignUniqueTarget( self, frozenBodies, frozenCount, blocked, BOTOBJ_RESCUE )", "Freeze Tag role spread around ordered bots")
    require(source, "const float frozenRatio", "Freeze Tag team urgency")
    require(source, "BotObjectiveNearbyEnemies( self, frozenOrigin, 640.0f )", "contested thaw urgency")
    require(source, "BOTOBJ_RESCUE", "Freeze Tag rescue result")
    require(source, "BOTOBJ_PRIORITY_RESCUE + urgency, true", "Freeze Tag hold behavior")

    # DeadZone honors the map-authored zone encoding and token requirement,
    # ignores disabled triggers, reads authoritative ownership/deadlock state,
    # splits carrier response/support, and assigns distinct spawned artifacts.
    for token in (
        'GetInt( "controlZone", "0" )',
        "zoneTeam != self->team + 1 && zoneTeam != 3",
        'GetBool( "requiresDeadZonePowerup", "1" )',
        "GetPhysics()->GetContents() & CONTENTS_TRIGGER",
        "riDeadZonePowerup::GetClassType()",
        "artifact->powerup != POWERUP_DEADZONE",
        "self->PowerUpActive( POWERUP_DEADZONE )",
        "BOTOBJ_PRIORITY_CARRIER, true",
        "state->GetDZState( self->team )",
        "state->GetDZState( enemyTeam )",
        "enemyControls || deadlocked",
        "ownControls || deadlocked",
        "ownControls && !deadlocked ? Min( 1, available )",
        "BotObjectiveAssignUniqueTarget( self, artifacts, artifactCount, assigned, BOTOBJ_FETCH )",
        "BOTOBJ_CONTROL",
    ):
        require(source, token, "DeadZone objective contract")

    # The menu descriptors for these modes currently have no complete runtime
    # entity/state rules. Objective code must fail closed rather than treating
    # borrowed CTF entities as scoreable goals.
    for token in (
        "gameLocal.gameType == GAME_ATTACK_DEFEND",
        "gameLocal.gameType == GAME_OVERLOAD",
        "gameLocal.gameType == GAME_HARVESTER",
        "gameLocal.gameType == GAME_DOMINATION",
        "Do not mistake borrowed CTF entities for a working",
    ):
        require(source, token, "unimplemented objective-mode safety")

    print("mp bot objective contracts: ok")


if __name__ == "__main__":
    main()
