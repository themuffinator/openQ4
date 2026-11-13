#!/usr/bin/env python3
"""Execute and pin a multiplayer client's predicted hit-scan shots (MP-07).

A client used to draw its own machinegun, shotgun and railgun shots only when the
server's copy arrived: a round trip after the muzzle flash, from where the server had
the gun by then. It now draws each shot on the frame it predicts it, from the gun it
draws, and the server's copy of that shot adds only the impacts on actors.

tools/tests/native/MpPredictedHitscanFixture.cpp executes the production bookkeeping
spliced verbatim from src/mpgame/Game_local.cpp: the spread seed the server and the
shooting client share, and the notes that tell a copy its shot was already drawn
(oldest first, per hit-scan, one second at most, a full ring giving up its oldest).
The source checks pin the wiring around it: the client predicts only its own shot on
the newest frame, outside demo playback, with net_mpPredictHitscan on; multiplayer
spread comes from the shared seed; a predicted shot leaves actor impacts to the
server's copy and the copy leaves everything else to the prediction; damage and the
server's broadcast are unchanged.

tools/tests/native/MpRespawnWeaponRaiseFixture.cpp executes the client's weapon
bookkeeping across its owner's respawn, spliced from src/mpgame/Player.cpp and
src/mpgame/Weapon.cpp: the snapshot reader's hitch test, weapon-change block and respawn
branch, the weapon UpdateWeapon makes, and the raise rvWeapon::PlayAnim plays. The server
raises the new life's weapon on its own frame; the client raises its weapon from that game
time, so its shots fall on the server's frames. Only a respawn across a genuine hitch, or a
weapon swap the client missed, catches the weapon straight up to ready.

Single-reversal controls re-run every part against mutated production source and require
each one to fail.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_lag_compensation_state import block

ROOT = Path(__file__).resolve().parents[2]
FUNCTIONS = (
    "int idGameLocal::HitscanSpreadSeed(",
    "void idGameLocal::NotePredictedHitscan(",
    "bool idGameLocal::ConsumePredictedHitscan(",
)
EXPIRY = "static const int MP_PREDICTED_HITSCAN_EXPIRY_MSEC = 1000;"

# Each reverses one agreement the fixture executes; every one must fail it.
STATE_MUTATIONS = (
    ("copies take the newest note",
     "note.hitscanDefIndex == hitscanDefIndex && ( found < 0 || note.noteMsec < mpPredictedHitscans[ found ].noteMsec )",
     "note.hitscanDefIndex == hitscanDefIndex && ( found < 0 || note.noteMsec > mpPredictedHitscans[ found ].noteMsec )"),
    ("notes never expire", "if ( now - note.noteMsec > MP_PREDICTED_HITSCAN_EXPIRY_MSEC ) {", "if ( false ) {"),
    ("any hit-scan consumes any note", "note.hitscanDefIndex == hitscanDefIndex && ( found < 0", "( found < 0"),
    ("a consumed note stays", "mpPredictedHitscans[ found ].noteMsec = 0;", "(void)found;"),
    ("a full ring overwrites its newest note",
     "if ( mpPredictedHitscans[ i ].noteMsec < mpPredictedHitscans[ slot ].noteMsec ) {",
     "if ( mpPredictedHitscans[ i ].noteMsec > mpPredictedHitscans[ slot ].noteMsec ) {"),
    ("the seed ignores the shooter",
     "h ^= static_cast<unsigned int>( shooterEntityNumber + 1 ) * 0x85EBCA77u;", "h ^= 0u;"),
    ("the seed is the raw shot time", "unsigned int h = static_cast<unsigned int>( shotTime ) * 0x9E3779B1u;",
     "unsigned int h = 0; return shotTime + shooterEntityNumber;"),
)


# Each reverses one rule of the respawn raise; every one must fail its fixture.
RESPAWN_MUTATIONS = (
    ("Init() restarts the snapshot sequence on a respawn", "player",
     "lastSnapshotSequence = latchSnapshotSequence;\n\t\tweaponCatchup = stateHitch;",
     "(void)latchSnapshotSequence;\n\t\tweaponCatchup = stateHitch;"),
    ("Init() restarts the snapshot sequence on spectating", "player",
     "lastSnapshotSequence = latchSnapshotSequence;\n\t\tweaponCatchup = false;",
     "(void)latchSnapshotSequence;\n\t\tweaponCatchup = false;"),
    ("a catch-up outlives the life spectating ends", "player",
     "lastSnapshotSequence = latchSnapshotSequence;\n\t\tweaponCatchup = false;",
     "lastSnapshotSequence = latchSnapshotSequence;"),
    ("a respawn always catches up", "player", "weaponCatchup = stateHitch;", "weaponCatchup = true;"),
    ("a respawn across a hitch raises", "player", "weaponCatchup = stateHitch;", "weaponCatchup = false;"),
    ("the raise ignores whether the server's weapon exists", "player",
     "int serverWeaponRaiseTime = gameLocal.time + frameMsec;", "int serverWeaponRaiseTime = gameLocal.time;"),
    ("the spawn clock is ignored", "player",
     "serverWeaponRaiseTime = spawnClock ? gameLocal.time + frameMsec - spawnElapsed : gameLocal.time;",
     "serverWeaponRaiseTime = gameLocal.time;"),
    ("the spawn clock leaves out the spawn's own frame", "player",
     "serverWeaponRaiseTime = spawnClock ? gameLocal.time + frameMsec - spawnElapsed : gameLocal.time;",
     "serverWeaponRaiseTime = spawnClock ? gameLocal.time - spawnElapsed : gameLocal.time;"),
    ("any knockback reads as the spawn clock", "player", "spawnElapsed % frameMsec == 0;", "true;"),
    ("the spawn starts some other knockback", "player",
     "physicsObj.SetKnockBack( SPAWN_KNOCKBACK_MSEC );", "physicsObj.SetKnockBack( 100 );"),
    ("new weapon entities always catch up", "player",
     "if ( stateHitch ) {\n\t\t\t\tweaponCatchup = true;\n\t\t\t} else {\n\t\t\t\tnetWeaponRaiseTime = serverWeaponRaiseTime;\n\t\t\t}",
     "weaponCatchup = true;"),
    ("the new weapon drops the raise time", "player",
     "weapon->SetNetRaiseStartTime( netWeaponRaiseTime );", "(void)0;"),
    ("the raise time outlives the weapon it was for", "player",
     "netWeaponRaiseTime = -1;\n\t\t\tweaponCatchup = false;", "weaponCatchup = false;"),
    ("a hitch is never seen", "player", "stateHitch = true;", "stateHitch = false;"),
    ("a missed weapon swap raises", "player", "if ( stateHitch ) {\n\t\t\tweaponCatchup = true;\n\t\t}", ""),
    ("the raise plays from now", "weapon",
     "startTime = Min( netRaiseStartTime, gameLocal.time );", "startTime = gameLocal.time;"),
    ("the world model raises from now", "weapon",
     "worldAnimator->PlayAnim( channel, anim, startTime, FRAME2MS( blendFrames ) );",
     "worldAnimator->PlayAnim( channel, anim, gameLocal.time, FRAME2MS( blendFrames ) );"),
    ("a catch-up keeps the raise time", "weapon",
     "netRaiseStartTime = -1;\n\tif ( gameLocal.isMultiplayer", "if ( gameLocal.isMultiplayer"),
    ("the raise time outlives its raise", "weapon", "netRaiseStartTime = -1;\n\t\t}\n\n\t\tviewModel->Show();",
     "}\n\n\t\tviewModel->Show();"),
)
WEAPON_FUNCTIONS = (
    "void rvWeapon::PlayAnim( int channel, const char *animname, int blendFrames ) {",
    "void rvWeapon::SetNetRaiseStartTime( int time ) {",
    "void rvWeapon::NetCatchup( void ) {",
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"{context}: missing `{token}`")


def require_before(text: str, first: str, second: str, context: str) -> None:
    require(text, first, context)
    require(text, second, context)
    if text.index(first) > text.index(second):
        raise AssertionError(f"{context}: `{first}` must come before `{second}`")


def check_sources(game_local: str, network: str, weapon: str, header: str, cvars: str) -> None:
    attack = block(weapon, "void rvWeapon::Attack( bool altAttack")
    require_before(attack, "if ( !gameLocal.isClient ) {",
                   "else if ( ( altAttack ? wfl.attackAltHitscan : wfl.attackHitscan ) && PredictsOwnHitscan() ) {",
                   "the client predicts its shot only where the server path is skipped")

    predicts = block(weapon, "bool rvWeapon::PredictsOwnHitscan(")
    for token in ("gameLocal.isClient", "gameLocal.isNewFrame", "gameLocal.GetDemoState() != DEMO_PLAYING",
                  "owner->entityNumber == gameLocal.localClientNum", "hitscanAttackDef >= 0",
                  "net_mpPredictHitscan.GetBool()"):
        require(predicts, token, "a client predicts only its own shot, once, live")

    hitscan = block(weapon, "void rvWeapon::Hitscan(")
    require(hitscan, "idGameLocal::HitscanSpreadSeed( owner ? owner->entityNumber : 0, gameLocal.time )",
            "the shot is seeded by its shooter and frame")
    require(hitscan, "idRandom &spreadRandom = gameLocal.isMultiplayer ? shotRandom : gameLocal.random;",
            "multiplayer spread uses the shared seed, single player the game generator")
    if "gameLocal.random." in hitscan:
        raise AssertionError("rvWeapon::Hitscan still draws spread from gameLocal.random directly")
    require_before(hitscan, "if ( predicted ) {\n\t\tgameLocal.NotePredictedHitscan( hitscanAttackDef );",
                   "if ( gameLocal.isServer ) {", "a predicted shot is noted once")
    require(hitscan, "hit = gameLocal.HitScan( dict, muzzleOrigin, dir, fxOrigin, owner, false, 1.0f, NULL, NULL, "
                     "idGameLocal::HITSCAN_EFFECTS_PREDICTED );", "a predicted shot draws without actor impacts")
    require(hitscan, "hit = gameLocal.HitScan( dict, muzzleOrigin, dir, fxOrigin, owner, false, 1.0f, NULL, areas );",
            "the server's shot keeps every effect and its routing areas")
    require(hitscan, "gameLocal.SendUnreliableMessagePVS( msg, owner, sendAreas[0], sendAreas[1] );",
            "the server still sends every shot, the shooter's included")

    client = block(network, "void idGameLocal::ClientHitScan( const idBitMsg &msg )")
    require(client, "if ( ownerNumber == localClientNum && ConsumePredictedHitscan( hitscanDefIndex ) ) {",
            "only this client's own predicted shots are confirmed")
    require(client, "effects = HITSCAN_EFFECTS_CONFIRMED;", "a confirmed copy adds only actor impacts")
    require(client, "gameLocal.HitScan( decl->dict, muzzleOrigin, dir, fxOrigin, owner, false, 1.0f, NULL, NULL, effects );",
            "the copy is drawn with the chosen effects")

    trace = block(game_local, "idEntity* idGameLocal::HitScan(")
    context = "idGameLocal::HitScan effect ownership"
    require(trace, "const bool\tdrawShotEffects = ( effects != HITSCAN_EFFECTS_CONFIRMED );", context)
    require(trace, "const bool\tdrawActorImpacts = ( effects != HITSCAN_EFFECTS_PREDICTED );", context)
    require(trace, "if ( ( actorImpact ? drawActorImpacts : drawShotEffects ) && !g_perfTest_weaponNoFX.GetBool() ) {", context)
    for token, count in (("if ( drawShotEffects ) {", 3), ("drawShotEffects && random.RandomFloat( ) < tracerChance", 1),
                         ("drawShotEffects && !ent->fl.takedamage && random.RandomFloat ( ) < tracerChance", 1),
                         ("drawShotEffects && crossedLiquid && PlayLiquidTrailToExit", 2),
                         ("drawShotEffects && !g_perfTest_weaponNoFX.GetBool()", 2),
                         ("} else if ( drawShotEffects ) {", 1), ("if ( !reflect && drawShotEffects ) {", 1)):
        if trace.count(token) < count:
            raise AssertionError(f"{context}: `{token}` guards fewer than {count} effect sites")
    for effect in ('"fx_path"', '"fx_tracer"', '"fx_reflect"'):
        unguarded = [line for line in trace.splitlines() if effect in line and "PlayEffect" in line]
        if not unguarded:
            raise AssertionError(f"{context}: {effect} is no longer played")
    require_before(trace, "if ( !gameLocal.isClient ) {", "ent->Damage( owner, owner, dir, damage, damageScale, hitJoint );",
                   "damage stays on the server")

    require(header, "hitscanEffects_t effects = HITSCAN_EFFECTS_ALL );", "every existing caller keeps all effects")
    require(cvars, 'idCVar net_mpPredictHitscan(				"net_mpPredictHitscan",				"1",',
            "predicted shots are on by default")


def build_fixture(game_local: str, header: str) -> str:
    members = "\n".join((
        "\tenum { MP_PREDICTED_HITSCANS = 16 };" if "enum { MP_PREDICTED_HITSCANS = 16 };" in header else "",
        block(header, "struct mpPredictedHitscan_t {") + ";",
        "\tmpPredictedHitscan_t	mpPredictedHitscans[MP_PREDICTED_HITSCANS];",
    ))
    if EXPIRY not in game_local:
        raise AssertionError("the predicted-shot expiry constant moved")
    bodies = EXPIRY + "\n" + "\n".join(block(game_local, signature) for signature in FUNCTIONS)
    harness = read("tools/tests/native/MpPredictedHitscanFixture.cpp")
    return harness.replace("// PRODUCTION_MEMBERS", members).replace("// PRODUCTION_FUNCTIONS", bodies)


def build_respawn_fixture(player: str, weapon: str) -> str:
    snapshot = block(player, "void idPlayer::ReadFromSnapshot( const idBitMsgDelta &msg ) {")
    start = "if ( static_cast<unsigned int>( snapshotSequence ) - static_cast<unsigned int>( lastSnapshotSequence ) > 1u ) {"
    hitch = snapshot[snapshot.index(start):snapshot.index("oldHealth = health;") + len("oldHealth = health;")]
    # The fixture records the sequence where production does: after the hitch test, before
    # the weapon is touched.
    require_before(snapshot, "oldHealth = health;", "lastSnapshotSequence = snapshotSequence;", "the snapshot sequence")
    require_before(snapshot, "lastSnapshotSequence = snapshotSequence;", "bool weaponChange = ( idealWeapon != newIdealWeapon );",
                   "the snapshot sequence")
    change = snapshot[snapshot.index("bool weaponChange = ( idealWeapon != newIdealWeapon );"):
                      snapshot.index("// rjohnson: instance persistance information")]
    respawn = block(snapshot, "else if ( oldHealth <= 0 && health > 0 ) {")
    update = player.split("void idPlayer::UpdateWeapon( void )", 1)[1].split("{", 1)[1].split("\tif ( hiddenWeapon && tipUp", 1)[0]
    weapons = "\n".join(block(weapon, signature) for signature in WEAPON_FUNCTIONS)
    spectate = block(player, "void idPlayer::Spectate( bool spectate, bool force ) {")
    entering = spectate[spectate.index("const unsigned int latchSnapshotSequence = lastSnapshotSequence;"):
                        spectate.index("StopRagdoll();")]
    # The client reads the spawn's own knockback as its clock.
    constant = "const int SPAWN_KNOCKBACK_MSEC = 100;"
    require(player, constant, "the spawn knockback constant")
    require(block(player, "void idPlayer::SpawnToPoint("), "physicsObj.SetKnockBack( SPAWN_KNOCKBACK_MSEC );",
            "the spawn starts the clock the client reads")
    harness = read("tools/tests/native/MpRespawnWeaponRaiseFixture.cpp")
    return (harness.replace("// PRODUCTION_CONSTANTS", constant).replace("// PRODUCTION_HITCH", hitch).replace("// PRODUCTION_WEAPON_CHANGE", change)
            .replace("// PRODUCTION_RESPAWN", respawn).replace("// PRODUCTION_UPDATE_WEAPON", update)
            .replace("// PRODUCTION_SPECTATE", entering)
            .replace("// PRODUCTION_WEAPON", weapons))


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    try:
        return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired as expired:
        # A control that spins has failed as surely as one that reports it.
        return subprocess.CompletedProcess(expired.cmd, 1, expired.stdout or "", "timed out")


def main() -> None:
    game_local = read("src/mpgame/Game_local.cpp")
    network = read("src/mpgame/Game_network.cpp")
    weapon = read("src/mpgame/Weapon.cpp")
    header = read("src/mpgame/Game_local.h")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    check_sources(game_local, network, weapon, header, cvars)

    # The source checks must reject each wiring reversal.
    source_controls = (
        ("replays draw the shot again", "weapon", "gameLocal.isClient && gameLocal.isMultiplayer && gameLocal.isNewFrame &&",
         "gameLocal.isClient && gameLocal.isMultiplayer &&"),
        ("every player's shots are predicted", "weapon", "owner != NULL && owner->entityNumber == gameLocal.localClientNum &&",
         "owner != NULL &&"),
        ("spread back on the game generator", "weapon",
         "float r = spreadRandom.RandomFloat() * idMath::PI * 2.0f;", "float r = gameLocal.random.RandomFloat() * idMath::PI * 2.0f;"),
        ("a predicted shot draws actor impacts", "weapon", "NULL, NULL, idGameLocal::HITSCAN_EFFECTS_PREDICTED );",
         "NULL, NULL, idGameLocal::HITSCAN_EFFECTS_ALL );"),
        ("other players' copies are confirmed", "network",
         "if ( ownerNumber == localClientNum && ConsumePredictedHitscan( hitscanDefIndex ) ) {",
         "if ( ConsumePredictedHitscan( hitscanDefIndex ) ) {"),
        ("a confirmed copy draws its path again", "game_local",
         "const bool\tdrawShotEffects = ( effects != HITSCAN_EFFECTS_CONFIRMED );", "const bool\tdrawShotEffects = true;"),
        ("predicted shots off by default", "cvars", '"net_mpPredictHitscan",				"1",',
         '"net_mpPredictHitscan",				"0",'),
    )
    sources = {"game_local": game_local, "network": network, "weapon": weapon, "header": header, "cvars": cvars}
    for label, name, original, mutated in source_controls:
        if sources[name].count(original) != 1:
            raise SystemExit(f"mp_predicted_hitscan_state: source control anchor not unique: {label}")
        changed = dict(sources, **{name: sources[name].replace(original, mutated, 1)})
        try:
            check_sources(changed["game_local"], changed["network"], changed["weapon"], changed["header"], changed["cvars"])
        except AssertionError:
            continue
        raise SystemExit(f"mp_predicted_hitscan_state: source control did not fail: {label}")

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the predicted hit-scan fixture")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-predicted-hitscan-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(game_local, header), path, "predicted", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mp_predicted_hitscan_state: FAILED\n{result.stdout}{result.stderr}")
        for index, (label, original, mutated) in enumerate(STATE_MUTATIONS):
            if game_local.count(original) != 1:
                raise SystemExit(f"mp_predicted_hitscan_state: mutation anchor not unique: {label}")
            control = compile_and_run(build_fixture(game_local.replace(original, mutated, 1), header), path,
                                      f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mp_predicted_hitscan_state: control did not fail: {label}")

        respawn_sources = {"player": read("src/mpgame/Player.cpp"), "weapon": weapon}
        result = compile_and_run(build_respawn_fixture(respawn_sources["player"], weapon), path, "respawn", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mp_predicted_hitscan_state: respawn raise FAILED\n{result.stdout}{result.stderr}")
        for index, (label, name, original, mutated) in enumerate(RESPAWN_MUTATIONS):
            if respawn_sources[name].count(original) != 1:
                raise SystemExit(f"mp_predicted_hitscan_state: respawn mutation anchor not unique: {label}")
            changed = dict(respawn_sources, **{name: respawn_sources[name].replace(original, mutated, 1)})
            try:
                source = build_respawn_fixture(changed["player"], changed["weapon"])
            except AssertionError:
                continue
            control = compile_and_run(source, path, f"respawn{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mp_predicted_hitscan_state: respawn control did not fail: {label}")

    controls = len(source_controls) + len(STATE_MUTATIONS) + len(RESPAWN_MUTATIONS)
    print(f"mp_predicted_hitscan_state: ok ({controls} single-reversal controls fail)")


if __name__ == "__main__":
    main()
