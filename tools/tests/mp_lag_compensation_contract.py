#!/usr/bin/env python3
"""Pins the agreements that make server-side lag compensation honest.

Nothing in either repository asserted anything about lag compensation before this file, which is
how the feature came to ship with a rewind that could not measure what it was for.

Three agreements are pinned here.

  * THE ESTIMATE MUST NOT COME FROM THE COMMAND'S AGE.  `time - shooter->usercmd.gameTime` looks
    like the end-to-end pipeline delay and is in fact a structural constant: idAsyncServer files a
    client's command at THAT client's own frame index and executes the slot for the frame it is
    running, and idGameLocal::RunFrame has already advanced `time` past it, so the difference is
    one GetMSec() at any ping.  A duplicated command is re-stamped with the server's own clock to
    the same effect.  The rewind must be derived from the client's reported prediction lead,
    capped by the round trip the server measured for itself.

  * THE TARGET MUST BE PUT BACK BEFORE ANYTHING ACTS ON THE HIT.  The trace resolves against the
    rewound world, but the damage, the blood decal, the impact effect, the PVS areas that effect
    is broadcast to and the ragdoll a corpse starts from all belong in the present.  The gauntlet
    and the lightning gun already restored before they damaged; the hit-scan path did not.

  * A DISCONTINUITY MUST INVALIDATE THE HISTORY.  Rewinding onto a position a player teleported
    away from, or onto the previous occupant of a recycled client slot, aims a shot at a place
    nobody could have been.

  * THE REWIND AND THE CLIENT'S REMOTE EXTRAPOLATION MUST NOT STACK.  A client in
    net_mpPredictMode 1 draws remote players forward of its last snapshot; it advertises that
    budget (net_mpRemoteExtrapolation) and the server takes it off the rewind, bounded by the lead
    the server already trusts.  tools/tests/mp_lag_compensation_state.py executes the result.

The multiplayer tree is the only one that matters here: src/game is game_sp and never runs
multiplayer.  Its copy of this code is dead, and is checked only for defaults that would read as a
disagreement between the forks.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    require(text, first, context)
    require(text, second, context)
    if text.index(first) >= text.index(second):
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def validate_estimator() -> None:
    game_local = read(GAME_LIBS_ROOT, "src/mpgame/Game_local.cpp")
    estimator = function_body(
        game_local,
        "void idGameLocal::EstimateMPLagCompensation(",
        "MP lag compensation estimator",
    )
    label = "MP lag compensation estimator"

    # The command-age estimate cannot measure anything; see the module docstring.  Rejected as the
    # assignment rather than as the bare symbol, so the comment explaining why it is wrong is
    # allowed to name it.
    reject(estimator, "rewindEstimateMS = time - shooter->usercmd.gameTime", label)
    reject(estimator, "= pingMS / 2", label)

    require(estimator, "ServerGetClientPrediction", label)
    require(estimator, "ServerGetClientPing", label)
    # Both report a 99999 sentinel for a slot that is not fully connected, and the reported lead is
    # a client-authored short, so it needs clamping as well as the sentinel check.
    require(estimator, "99999", label)
    require(estimator, "idMath::ClampInt( 0, 1000, reported )", label)
    require(estimator, "net_mpLagCompSlackMS", label)
    require(estimator, "RemoteExtrapolationMS( clientNum, reported )", label)
    require(estimator, "Min( claim, ceiling ) - estimate.extrapolationMS", label)

    rewinder = function_body(
        game_local,
        "bool idGameLocal::ComputeMPLagCompensationRewind(",
        "MP lag compensation rewind",
    )
    require(rewinder, "EstimateMPLagCompensation( clientNum, used );", "MP lag compensation rewind")
    require(rewinder, "used.poseAgeMS + net_mpLagCompBiasMS.GetInteger()", "MP lag compensation rewind")
    require(rewinder, "net_mpLagCompMaxMS", "MP lag compensation rewind")
    reject(rewinder, "rewindEstimateMS = time - shooter->usercmd.gameTime", "MP lag compensation rewind")

    # Lag compensation and client-side remote extrapolation correct the same error from opposite
    # ends.  Applying both at full strength inverts the shooter's lead rather than removing it, so
    # whatever the shooter advertises it extrapolates comes off the rewind - bounded by the lead the
    # server already trusts, so the advertisement can only ever shorten a rewind.
    coupling = function_body(
        game_local,
        "int idGameLocal::RemoteExtrapolationMS(",
        "remote extrapolation coupling",
    )
    require(coupling, "Min( leadMS, AdvertisedRemoteExtrapolationTics( clientNum ) * GetMSec() )", "remote extrapolation coupling")
    advertised = function_body(
        game_local,
        "int idGameLocal::AdvertisedRemoteExtrapolationTics(",
        "advertised remote extrapolation",
    )
    require(
        advertised,
        'idNumericString::ParseUnsignedBounded( userInfo[ clientNum ].GetString( "net_mpRemoteExtrapolation", "0" ), 64, tics )',
        "advertised remote extrapolation is bounded client-authored text",
    )

    # The client keeps its advertisement equal to what it simulates, from game init and every frame.
    update = function_body(
        game_local,
        "void idGameLocal::UpdateAdvertisedRemoteExtrapolation(",
        "advertised remote extrapolation update",
    )
    require(update, "net_mpPredictMode.GetBool()", "advertised remote extrapolation update")
    require(update, "net_mpPredictMaxFrames.GetInteger()", "advertised remote extrapolation update")
    init = function_body(game_local, "void idGameLocal::Init( void )", "MP game init")
    require(init, "UpdateAdvertisedRemoteExtrapolation();", "MP game init advertises before connecting")
    network = read(GAME_LIBS_ROOT, "src/mpgame/Game_network.cpp")
    client_run = function_body(network, "void idGameLocal::ClientRun( void )", "MP client frame")
    require(client_run, "UpdateAdvertisedRemoteExtrapolation();", "MP client frame keeps the advertisement current")

    mp_cvar = read(GAME_LIBS_ROOT, "src/mpgame/gamesys/SysCvar.cpp")
    advert_line = next(line for line in mp_cvar.splitlines() if "idCVar net_mpRemoteExtrapolation(" in line)
    require(advert_line, "CVAR_USERINFO | CVAR_ROM", "advertised extrapolation cvar")
    # The prediction cvars themselves must not be user info: a server echoes user info back into
    # the client's cvars, and must never be able to force per-frame cost onto a client.
    for name in ("idCVar net_mpPredictMode(", "idCVar net_mpPredictMaxFrames("):
        line = next(line for line in mp_cvar.splitlines() if name in line)
        reject(line, "CVAR_USERINFO", f"{name} stays client-local")

    commands = read(GAME_LIBS_ROOT, "src/mpgame/gamesys/SysCmds.cpp")
    require(commands, 'cmdSystem->AddCommand( "lagCompStatus", Cmd_LagCompStatus_f', "lag compensation status command")
    status = function_body(commands, "static void Cmd_LagCompStatus_f(", "lag compensation status command")
    require(status, "gameLocal.EstimateMPLagCompensation( clientNum, estimate );", "lag compensation status command")
    require(status, "estimate.leadMS > 0 && estimate.measuredMS > 0 ?",
            "lag compensation status must not manufacture a bot or listen-host rewind from bias")

    player = read(GAME_LIBS_ROOT, "src/mpgame/Player.cpp")
    require(player, "gameLocal.lastChainReplayTics - remoteChainResimulatedTics", "client-side displayed pose age")
    # The budget counter is reset by PredictionErrorDecay on every presented frame, so the
    # diagnostic needs its own per-chain count, reset only when the snapshot is read.
    require(player, "remoteChainResimulatedTics++;", "client-side displayed pose age")
    require(network, "lastChainReplayTics = clientReplayTics;", "client replay chain accounting")

    state = ROOT / "tools/tests/mp_lag_compensation_state.py"
    if not state.is_file():
        raise AssertionError("the executable lag compensation state fixture is missing")


def validate_hit_is_resolved_in_the_present() -> None:
    game_local = read(GAME_LIBS_ROOT, "src/mpgame/Game_local.cpp")
    label = "MP lag compensation hit resolution"

    resolve = function_body(
        game_local,
        "bool idGameLocal::ResolveMPLagCompensationHit(",
        label,
    )
    require(resolve, "restore.originalOrigin - restore.rewoundOrigin", label)
    require(resolve, "tr.c.point += shift;", label)
    require(resolve, "tr.endpos += shift;", label)
    require(resolve, "tr.c.dist = tr.c.normal * tr.c.point;", label)
    require(resolve, "restoreState[ i ] = restoreState[ --restoreCount ];", label)

    hitscan = function_body(game_local, "idEntity* idGameLocal::HitScan(", label)
    require(hitscan, "ResolveMPLagCompensationHit(", label)
    # The target must be back in the present before ANY of these run against the hit.
    for consumer in (
        "collisionArea  = pvs.GetPVSArea( tr.c.point )",
        "ent->Damage(",
        "AddDamageEffect(",
    ):
        require_before(hitscan, "ResolveMPLagCompensationHit(", consumer, label)

    require_before(hitscan, "const idVec3 traceCollisionPoint = collisionPoint;",
                   "ResolveMPLagCompensationHit(", "historical ray saved before impact translation")
    penetration = function_body(hitscan,
        "if ( ent->IsType ( idActor::GetClassType() ) && penetrate > 0.0f )", "rail penetration")
    require(penetration, "start = traceCollisionPoint;", "rail penetration stays on the traced ray")

    gauntlet = function_body(read(GAME_LIBS_ROOT, "src/mpgame/weapon/WeaponGauntlet.cpp"),
                            "void rvWeaponGauntlet::Attack", "gauntlet attack")
    require_before(gauntlet, "ResolveMPLagCompensationHit(", "EndMPLagCompensation(", "gauntlet impact translation")
    require_before(gauntlet, "EndMPLagCompensation(", "gameLocal.PlayEffect (", "gauntlet world restored before effects")
    require_before(gauntlet, "EndMPLagCompensation(", "ent->Damage (", "gauntlet world restored before damage")
    lightning = function_body(read(GAME_LIBS_ROOT, "src/mpgame/weapon/WeaponLightningGun.cpp"),
                             "void rvWeaponLightningGun::Think", "lightning think")
    require_before(lightning, "UpdateChainLightning();", "MPLagCompensationOffset(", "chain selection stays historical")
    require_before(lightning, "MPLagCompensationOffset(", "ResolveMPLagCompensationHit(", "translate paths before consuming entries")
    require_before(lightning, "ResolveMPLagCompensationHit(", "EndMPLagCompensation(", "lightning trace translation")
    require_before(lightning, "EndMPLagCompensation(", "UpdateEffects( origin );", "lightning world restored before effects")
    require_before(lightning, "EndMPLagCompensation(", "Attack ( currentPath.target", "lightning world restored before damage")


def validate_invalidation() -> None:
    label = "MP lag compensation history invalidation"
    player = read(GAME_LIBS_ROOT, "src/mpgame/Player.cpp")
    network = read(GAME_LIBS_ROOT, "src/mpgame/Game_network.cpp")

    # A respawn was the only invalidation site the feature shipped with.
    spawn = function_body(player, "void idPlayer::SpawnToPoint(", label)
    require(spawn, "InvalidateMPLagCompensationHistory", label)

    # A teleport does not route through SpawnToPoint.
    teleport = function_body(
        player,
        "void idPlayer::Teleport( const idVec3 &origin, const idAngles &angles, idEntity *destination )",
        label,
    )
    require(teleport, "InvalidateMPLagCompensationHistory", label)

    # A recycled slot must not be rewound onto its previous occupant's positions.
    disconnect = function_body(
        network, "void idGameLocal::ServerClientDisconnect(", label
    )
    require(disconnect, "InvalidateMPLagCompensationHistory", label)


def validate_usercmd_stamp_is_server_authored() -> None:
    label = "usercmd gameTime sanitisation"
    async_server = read(ROOT, "src/framework/async/AsyncServer.cpp")
    process = function_body(
        async_server,
        "void idAsyncServer::ProcessUnreliableClientMessage(",
        label,
    )
    # gameTime arrives client-authored and reaches the game unchecked.  An honest client sends
    # exactly this value, so overwriting it costs nothing and removes a client-controlled dial on
    # anything that measures a command against the server clock.
    require(
        process,
        "userCmds[index][clientNum].gameTime = i * common->GetUserCmdMSec();",
        label,
    )
    require_before(
        process,
        "userCmds[index][clientNum].gameFrame = i;",
        "userCmds[index][clientNum].gameTime = i * common->GetUserCmdMSec();",
        label,
    )


def validate_defaults() -> None:
    label = "MP lag compensation defaults"
    mp_cvar = read(GAME_LIBS_ROOT, "src/mpgame/gamesys/SysCvar.cpp")

    # Off until the rewind has been measured against a real server, and because it must not be on
    # at the same time as full remote extrapolation - the two corrections invert rather than
    # compose.  A deliberate flip changes this line and this assertion together.
    require(mp_cvar, 'net_mpLagCompensation(', label)
    line = next(
        stripped
        for stripped in mp_cvar.splitlines()
        if "net_mpLagCompensation(" in stripped
    )
    if '"0"' not in line:
        raise AssertionError(
            f"{label}: net_mpLagCompensation must ship at 0 until the rewind is measured; got: {line.strip()}"
        )

    require(mp_cvar, 'net_mpLagCompSlackMS(', label)
    require(mp_cvar, 'net_mpLagCompMaxMS(', label)


def main() -> None:
    validate_estimator()
    validate_hit_is_resolved_in_the_present()
    validate_invalidation()
    validate_usercmd_stamp_is_server_authored()
    validate_defaults()
    print("mp_lag_compensation_contract: ok")


if __name__ == "__main__":
    main()
