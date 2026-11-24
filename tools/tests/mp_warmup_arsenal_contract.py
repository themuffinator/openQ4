#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contract checks for the si_warmupWeapons arsenal lifetime.

Warmup hands out every weapon the map contains so the pre-match period is
practice.  Nothing else takes those weapons away again, because an MP respawn
never clears inventory.weapons - so the grant is gated on the match not being
live, and the live transition must publish that state BEFORE it respawns the
players it just disarmed.  Get that order wrong and every player starts the real
match holding the whole map, permanently, with default cvars.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / "src/game/Player.cpp"
GAMESTATE = ROOT / "src/game/mp/GameState.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def index_of(text: str, token: str, context: str) -> int:
    position = text.find(token)
    if position < 0:
        raise AssertionError(f"missing {token!r} in {context}")
    return position


def gameon_case(gamestate: str) -> str:
    """The body of rvGameState::NewState's GAMEON case."""
    start = index_of(gamestate, "case GAMEON: {", "rvGameState::NewState")
    end = gamestate.find("case GAMEREVIEW:", start)
    if end < 0:
        raise AssertionError("could not delimit the GAMEON case in rvGameState::NewState")
    return gamestate[start:end]


def validate_grant_gate(player: str) -> None:
    grant = index_of(player, 'gameLocal.serverInfo.GetBool( "si_warmupWeapons" )',
                     "idPlayer::SpawnToPoint")
    window = player[grant:grant + 1200]

    # The grant only ever happens before the match is live.
    require(window, "mpState == WARMUP || mpState == COUNTDOWN", "warmup arsenal gate")
    require(window, "gameLocal.mpGame.GetGameState()->GetMPGameState()", "warmup arsenal gate")
    # And only what the map actually contains.
    require(window, "inventory.weapons |= gameLocal.mpGame.GetMapWeaponMask()", "warmup arsenal grant")
    # The loadout underneath is latched exactly once, so a second warmup respawn
    # cannot overwrite it with the already-granted arsenal.
    require(window, "if ( !warmupArsenalGranted ) {", "warmup arsenal latch")
    require(window, "warmupArsenalRestoreWeapons = inventory.weapons", "warmup arsenal latch")

    revoke = index_of(player, "void idPlayer::RevokeWarmupArsenal( void ) {",
                      "idPlayer::RevokeWarmupArsenal")
    body = player[revoke:revoke + 900]
    require(body, "inventory.weapons = warmupArsenalRestoreWeapons", "warmup arsenal revoke")
    require(body, "warmupArsenalGranted = false", "warmup arsenal revoke")
    # A revoked weapon must not stay in the player's hands.
    require(body, "NextBestWeapon()", "warmup arsenal revoke")


def validate_live_transition_order(gamestate: str) -> None:
    body = gameon_case(gamestate)

    commit = index_of(body, "currentState = newState;", "GAMEON case")
    revoke = index_of(body, "p->RevokeWarmupArsenal();", "GAMEON case")
    respawn = index_of(body, "p->ServerSpectate(", "GAMEON case")

    if not commit < revoke < respawn:
        raise AssertionError(
            "GAMEON must publish the live state, then revoke the warmup arsenal, then "
            "respawn: idPlayer::SpawnToPoint re-grants the whole map arsenal for any "
            "respawn that still observes WARMUP/COUNTDOWN "
            f"(commit={commit}, revoke={revoke}, respawn={respawn})"
        )

    # Scoring is no longer suppressed once the state is published, and every MP
    # spawn runs KillBox - so the scoreboard reset has to follow the spawn wave
    # or a telefragged player starts the live match on -1.
    score_reset = index_of(body, "gameLocal.mpGame.SetPlayerScore( p, 0 );", "GAMEON case")
    if not respawn < score_reset:
        raise AssertionError(
            "the GAMEON score reset must run after the respawn loop, not inside it "
            f"(respawn={respawn}, score_reset={score_reset})"
        )


def validate_no_midmatch_warmup(gamestate: str) -> None:
    """Nothing may re-enter WARMUP while a match is live."""
    for match in re.finditer(r"NewState\(\s*WARMUP\s*\)", gamestate):
        prefix = gamestate[max(0, match.start() - 2000):match.start()]
        if "currentState == INACTIVE" in prefix or "case GAMEREVIEW:" in prefix:
            continue
        line = gamestate[:match.start()].count("\n") + 1
        raise AssertionError(
            f"GameState.cpp:{line} re-enters WARMUP from something other than "
            "INACTIVE or GAMEREVIEW; the warmup arsenal gate would reopen mid-match"
        )


def main() -> None:
    player = read(PLAYER)
    gamestate = read(GAMESTATE)

    validate_grant_gate(player)
    validate_live_transition_order(gamestate)
    validate_no_midmatch_warmup(gamestate)

    print("mp_warmup_arsenal_contract: ok")


if __name__ == "__main__":
    main()
