#!/usr/bin/env python3
"""Guards the agreements that keep combat effects visible to multiplayer clients.

A multiplayer client never runs idGameLocal::RunFrame - the engine drives it
through ClientReadSnapshot / ClientProcessReliableMessage / ClientPrediction.
Anything the effect system needs that is only maintained from the server frame
is therefore dead on a client, and the failure is silent: effects simply stop
appearing, with no warning and no crash.  Two of those went unnoticed for a long
time, so they are pinned here:

  * The BSE effect rate limiter is a leaky bucket per effect category.  It used
    to drain a fixed step per bse->StartFrame(), and the only StartFrame call
    site in the whole tree is idGameLocal::RunFrame.  On a client the bucket
    only ever filled, and nine EC_IMPACT effects after a map load every bullet
    impact effect was filtered for the rest of the map.  The drain must be
    driven by elapsed time and must happen on the way in to CanPlayRateLimited,
    so it cannot depend on any particular frame loop calling it.

  * idGameLocal::InPlayerConnectedArea answers from playerConnectedAreas, which
    only SetupPlayerPVS builds and only RunFrame calls.  With no connected-area
    set to consult it has to fail open, or rvClientEffect::Think marks every
    bound effect on a client as unreachable from the view.

The receive paths matter too: charging the rate limiter through both
CanPlayRateLimited() and Filtered() spends the budget twice per replicated
effect, and a NULL decl off the wire has to be rejected before it reaches
rvClientEffect.  The single-player tree was repaired for these; the multiplayer
tree is a separate fork and has to be checked independently.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def body_of(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing {signature!r} in {context}")
    end = source.find("\n}", start)
    if end == -1:
        raise AssertionError(f"Unterminated {signature!r} in {context}")
    return source[start:end]


def validate_rate_limiter() -> None:
    manager = read(ROOT / "src" / "bse" / "BSE_Manager.cpp")
    header = read(ROOT / "src" / "bse" / "BSE.h")

    # The bucket is drained from a wall-clock stamp, not from a per-call step.
    require(header, "mEffectRateTime", "BSE.h")
    update = body_of(manager, "void rvBSEManagerLocal::UpdateRateTimes(void)", "BSE_Manager.cpp")
    require(update, "Sys_Milliseconds()", "rvBSEManagerLocal::UpdateRateTimes")
    require(update, "mEffectRateTime", "rvBSEManagerLocal::UpdateRateTimes")
    if "elapsed" not in update:
        raise AssertionError(
            "rvBSEManagerLocal::UpdateRateTimes no longer drains by elapsed time; a client "
            "never calls StartFrame, so a per-call drain leaves the bucket latched shut"
        )

    # And it is drained on the way in, so no caller has to remember to pump it.
    can_play = body_of(
        manager,
        "bool rvBSEManagerLocal::CanPlayRateLimited(effectCategory_t category)",
        "BSE_Manager.cpp",
    )
    require(can_play, "UpdateRateTimes();", "rvBSEManagerLocal::CanPlayRateLimited")
    if can_play.index("UpdateRateTimes();") > can_play.index("mEffectRates[category] + cost"):
        raise AssertionError(
            "rvBSEManagerLocal::CanPlayRateLimited must drain before it charges the bucket"
        )


def validate_client_connected_area() -> None:
    mp = GAME_LIBS_ROOT / "src" / "game"
    if not mp.is_dir():
        return

    local = read(mp / "Game_local.cpp")
    connected = body_of(
        local, "bool idGameLocal::InPlayerConnectedArea( idEntity *ent ) const", "game Game_local.cpp"
    )
    # playerConnectedAreas is only built by SetupPlayerPVS, which only RunFrame calls.
    if re.search(r"playerConnectedAreas\.i == -1\s*\)\s*\{\s*return false;", connected):
        raise AssertionError(
            "idGameLocal::InPlayerConnectedArea returns false with no connected-area set; on a "
            "client that flags every bound rvClientEffect as outside the view"
        )
    require(connected, "return isClient;", "idGameLocal::InPlayerConnectedArea")


def validate_receive_paths() -> None:
    mp = GAME_LIBS_ROOT / "src" / "game"
    if not mp.is_dir():
        return

    entity = read(mp / "Entity.cpp")
    network = read(mp / "Game_network.cpp")

    receive = body_of(
        entity,
        "bool idEntity::ClientReceiveEvent( int event, int time, const idBitMsg &msg )",
        "game Entity.cpp",
    )
    unreliable = body_of(
        network, "void idGameLocal::ProcessUnreliableMessage( const idBitMsg &msg )", "game Game_network.cpp"
    )

    for name, block in (
        ("idEntity::ClientReceiveEvent", receive),
        ("idGameLocal::ProcessUnreliableMessage", unreliable),
    ):
        # Filtered() already charges the category; asking CanPlayRateLimited as well
        # spends the budget twice for one effect.
        if "CanPlayRateLimited" in block:
            raise AssertionError(
                f"{name} charges the effect rate limiter directly; Filtered() already does that, "
                "so the replicated effect pays twice"
            )
        require(block, "bse->Filtered(", name)
        require(block, "SetGravity(", name)

    # A decl that failed to resolve off the wire must not reach rvClientEffect.
    for count, name in ((2, "EVENT_PLAYEFFECT/EVENT_PLAYEFFECT_JOINT"),):
        if receive.count("if ( !effect ) {") + receive.count("if ( !effect ||") < count:
            raise AssertionError(f"{name} in game Entity.cpp does not reject a NULL decl")
    if "if ( !decl ||" not in unreliable and "if ( !decl ) {" not in unreliable:
        raise AssertionError(
            "GAME_UNRELIABLE_MESSAGE_EFFECT in game Game_network.cpp does not reject a NULL decl"
        )


def validate_hitscan_attack_def() -> None:
    mp = GAME_LIBS_ROOT / "src" / "game"
    if not mp.is_dir():
        return

    weapon = read(mp / "Weapon.cpp")
    init = body_of(weapon, "void rvWeapon::InitDefs( void )", "game Weapon.cpp")
    # rvWeapon::Hitscan transmits one index for both fire modes, so a weapon whose
    # only hitscan is the alt one still has to publish a resolvable decl index.
    if init.count("hitscanAttackDef = def->Index();") < 2:
        raise AssertionError(
            "rvWeapon::InitDefs leaves hitscanAttackDef unset for def_althitscan; the client then "
            "resolves DeclByIndex( -1 ) for that weapon's impact effects"
        )


def validate_network_clock() -> None:
    """The engine's snapshot clock and idGameLocal's clock must be the same ladder.

    The snapshot header carries gameTime, idGameLocal::ClientReadSnapshot assigns it
    straight to gameLocal.time, and the game then compares that against timestamps the
    SERVER's gameLocal.time produced - idProjectile::launchTime, the time stamped into
    every entity event.  If the two advance at different rates the gap grows without
    bound: client-side projectile dead reckoning walks its projectiles further and
    further past where they really are, and replicated events age out of the 300ms
    window in idEntity::ClientReceiveEvent.  Neither half may move without the other.
    """
    server = read(ROOT / "src" / "framework" / "async" / "AsyncServer.cpp")
    client = read(ROOT / "src" / "framework" / "async" / "AsyncClient.cpp")

    engine_exact = "gameTime = common->GetUserCmdTime( gameFrame );" in server
    engine_tic = "gameTime = gameFrame * common->GetUserCmdMSec();" in server
    if engine_exact == engine_tic:
        raise AssertionError(
            "idAsyncServer::RunFrame no longer stamps gameTime in a recognised way; the "
            "snapshot clock has to stay in step with idGameLocal's clock"
        )
    if "snapshotGameTime = snapshotGameFrame * common->GetUserCmdMSec();" not in client:
        if engine_tic:
            raise AssertionError(
                "idAsyncClient advances snapshotGameTime on a different ladder from "
                "idAsyncServer's gameTime"
            )

    mp = GAME_LIBS_ROOT / "src" / "game"
    if not mp.is_dir():
        return

    local = read(mp / "Game_local.cpp")
    game_tic = "time += GetMSec();" in local
    game_exact = "time = common->GetUserCmdTime( framenum );" in local

    if game_tic and not engine_tic:
        raise AssertionError(
            "idGameLocal advances gameLocal.time by GetUserCmdMSec() while the engine "
            "stamps snapshots from GetUserCmdTime()'s exact ladder; the two clocks drift "
            "apart and every replicated timestamp decays with map time"
        )
    if game_exact and not engine_exact:
        raise AssertionError(
            "idGameLocal moved to the exact user-cmd time ladder; idAsyncServer::RunFrame "
            "has to stamp gameTime with common->GetUserCmdTime( gameFrame ) to match"
        )


def validate_reliable_routing() -> None:
    server = read(ROOT / "src" / "framework" / "async" / "AsyncServer.cpp")
    send = body_of(
        server,
        "void idAsyncServer::SendReliableGameMessage( int clientNum, const idBitMsg &msg, bool captureDemo )",
        "AsyncServer.cpp",
    )
    # The game passes MAX_CLIENTS for its server-demo pseudo client. Only a negative
    # clientNum may mean "everyone", or every instance-scoped event is delivered twice
    # and the exclusion the caller asked for is ignored.
    if "if ( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS )" in send:
        raise AssertionError(
            "idAsyncServer::SendReliableGameMessage treats an out-of-range clientNum as a "
            "broadcast; the game's MAX_CLIENTS demo pseudo client then doubles every event"
        )
    require(send, "if ( clientNum >= 0 ) {", "idAsyncServer::SendReliableGameMessage")
    require(
        send,
        "if ( captureDemo && idAsyncNetwork::multiViewDemo.IsRecording() )",
        "idAsyncServer::SendReliableGameMessage MVD capture guard",
    )


def main() -> int:
    try:
        validate_rate_limiter()
        validate_client_connected_area()
        validate_receive_paths()
        validate_hitscan_attack_def()
        validate_network_clock()
        validate_reliable_routing()
    except AssertionError as error:
        print(f"mp_client_combat_effects: FAILED - {error}")
        return 1

    print("mp_client_combat_effects: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
