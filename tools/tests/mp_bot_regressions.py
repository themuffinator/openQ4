#!/usr/bin/env python3
"""Execute production bot decisions with deterministic navigation/perception fixtures.

The fixture in tools/tests/native/MpBotRegressionsFixture.cpp supplies a small
mock world - players, items, projectiles, a cost-field navmesh, item timers and
noises - and the decision functions are spliced in verbatim from production, so
there is no second implementation of goal policy to drift.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(source: str, signature: str) -> str:
    """Return one function, from its signature to the matching closing brace."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main() -> None:
    bot = (ROOT / "src/mpgame/bots/Bot.cpp").read_text(encoding="utf-8")
    combat = (ROOT / "src/mpgame/bots/BotCombat.cpp").read_text(encoding="utf-8")
    network = (ROOT / "src/mpgame/Game_network.cpp").read_text(encoding="utf-8")
    physics = (ROOT / "src/mpgame/physics/Physics_Player.cpp").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(r"^static const (?:int|float)\s+BOT_\w+\s*=\s*[0-9][^\n]+", bot, re.M))
    constants += "\n" + "\n".join(re.findall(
        r"^static const (?:int|float)\s+BOTCOMBAT_\w+[^\n]+", combat, re.M))
    constants += "\n" + "\n".join(re.findall(
        r"^static (?:const int|int)\s+(?:MAX_UNRELIABLE_BATCH_BYTES|UNRELIABLE_OVERFLOW_WARN_INTERVAL|unreliableOverflowWarnTime)[^\n]+",
        network, re.M))
    bodies = "\n".join(function(bot, signature) for signature in (
        "static float BotGapToPad(",
        "static bool BotPadFlightsShareCorridor(",
        "static bool BotPadCorridorBusy(",
        "bool rvBot::HoldForPad(",
        "void rvBot::ResetDodgeJump(",
        "void rvBot::AdoptPadLaunch(",
        "static float BotMaxSafeFallDelta(",
        "bool rvBot::GuardLandingHealth(",
        "bool rvBot::AlignJumpDropApproach(",
        "bool rvBot::HandleJumpTakeoff(",
        "bool rvBot::AtObjectiveHoldPosition(",
        "bool rvBot::GuardLandingContinuation(",
        "static bool BotPadFlightComplete(",
        "void rvBot::ApplyMove(",
        "static float BotGroundBrakeStrength(",
        "void rvBot::ProbeAirControl(",
        "void rvBot::PressJump(",
        "void rvBot::TryDodgeJump(",
        "bool rvBot::HoldDodgeJump(",
        "static bool BotDeadlyDropAt(",
        "static bool BotBrakeAtEdge( idPlayer *self, idVec3 &moveDir ) {",
        "static int BotPlannedTransport(",
        "static bool BotBrakeForTransport( idPlayer *self, idVec3 &moveDir, int plannedEntityNum ) {",
        "bool rvBot::RecoverInAir(",
        "void rvBot::OnKnockback(",
        "void rvBot::RefreshKnockbackMove(",
        "bool rvBot::RecoverToNavMesh(",
        "void rvBot::UpdateEnemy(",
        "int rvBot::EnemyLastKnownTime(",
        "void rvBot::UpdateHearing(",
        "bool rvBot::Repath(",
        "float rvBot::PathDistanceRemaining(",
        "void rvBot::AbandonGoal(",
        "int rvBot::ItemPendingMsec(",
        "idEntity *rvBot::PickItemGoal(",
        "void rvBot::UpdateGoal(",
    ))
    bodies += function(combat, "bool BotCombatFindIncomingProjectileThreat(")
    bodies += function(combat, "static bool BotCombatSplashThrowsShooter(")
    bodies += function(physics, "float idPhysics_Player::CmdScale(")
    bodies += "\n".join(function(network, signature) for signature in (
        "void idGameLocal::QueueUnreliableMessage(",
        "void idGameLocal::SendUnreliableMessage(",
        "void idGameLocal::SendUnreliableMessagePVS(",
    ))
    harness = (ROOT / "tools/tests/native/MpBotRegressionsFixture.cpp").read_text(encoding="utf-8")
    harness = harness.replace("// PRODUCTION_CONSTANTS", constants).replace("// PRODUCTION_FUNCTIONS", bodies)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for bot regressions")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-regressions-", dir=output) as directory:
        path = Path(directory)
        cpp, exe = path / "bots.cpp", path / ("bots.exe" if os.name == "nt" else "bots")
        cpp.write_text(harness, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-const-variable",
                   "-Wno-unused-parameter", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)
        # Slipping off a WALK rim is recoverable without an enemy impulse.
        # Reverting admission to knock-only must fail the live-state cases.
        mutated = harness.replace("( !knocked && !walkingFall )", "!knocked")
        assert mutated != harness
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe), "landings"], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "knock-only walking-fall mutation escaped"
        print("Unpressed WALK recovery: knock-only mutation rejected")
        for label, anchor, replacement in (
            ("ramp ascent recovery", "!airborneVoluntary;", "!airborneVoluntary && velocity.z <= 0.0f;"),
            ("completed walking route", "const bool walking = path.IsEmpty() || (", "const bool walking = false || ("),
            ("unlaunched pad approach", "path[pathCorner + 1].travelType == NAVTRAVEL_DROP ||\n\t\t  path[pathCorner + 1].travelType == NAVTRAVEL_TELEPORT",
             "path[pathCorner + 1].travelType == NAVTRAVEL_DROP || path[pathCorner + 1].travelType == NAVTRAVEL_JUMPPAD ||\n\t\t  path[pathCorner + 1].travelType == NAVTRAVEL_TELEPORT"),
        ):
            mutated = harness.replace(anchor, replacement)
            assert mutated != harness, label
            cpp.write_text(mutated, encoding="utf-8")
            subprocess.run(command, cwd=path, check=True)
            result = subprocess.run([str(exe), "landings"], cwd=path, capture_output=True, text=True)
            assert result.returncode != 0, f"{label} mutation escaped"
            print(f"{label}: reverted-fix mutation rejected")
        mutated = harness.replace("!protectCoast && !BotAirLandingUnsafe", "!BotAirLandingUnsafe")
        assert mutated != harness
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe), "landings"], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "late knocked-coast ownership mutation escaped"
        print("Knocked coast: late-ownership mutation rejected")
        for label, anchor, replacement in (
            ("probe settled braking", "if ( static_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack() <= 0 ) {\n\t\t\tbrake *= BotGroundBrakeStrength( speed );",
             "if ( false ) {\n\t\t\tbrake *= 1.0f;"),
            ("recovery settled braking", "\t\tif ( knockMsec <= 0 ) {\n\t\t\tbrake *= BotGroundBrakeStrength( speed );\n\t\t}",
             "\t\t// full reverse runout"),
            ("off-route node transport", "const bool transportBraking = BotBrakeForTransport( self, moveDir, -1 );",
             "const bool transportBraking = false;"),
            ("off-route wander transport", "\t\tBotBrakeForTransport( self, moveDir, -1 );",
             "\t\t// omitted wander transport guard"),
            ("airborne pad approach reservation", "if ( botManager.PadApproachOf( i, otherPad, otherSince, otherLaunch, otherLanding ) &&",
             "if ( other->pfl.onGround && botManager.PadApproachOf( i, otherPad, otherSince, otherLaunch, otherLanding ) &&"),
            ("actual floor health", "!navMesh.LandingSurvivable( self->GetPhysics()->GetOrigin(), landing, BotMaxSafeFallDelta( self ) )",
             "false"),
            ("airborne self-blast", "!shooter->pfl.onGround && model.selfPushScale > 0.0f &&",
             "false && !shooter->pfl.onGround && model.selfPushScale > 0.0f &&"),
            ("projected airborne self-blast", "if ( !shooter->pfl.onGround && model.selfPushScale > 0.0f )",
             "if ( false && !shooter->pfl.onGround && model.selfPushScale > 0.0f )"),
            ("weak impulse admission", "if ( kick.LengthSqr() <= 0.0f )", "if ( kick.LengthSqr() < Square( 150.0f ) )"),
            ("same-frame movement refresh", "\tRecoverInAir( self, cmd );", "\t// disabled recovery refresh"),
            ("hit jump coast ownership", "ballistic || ( !traversalStarted", "false || ( !traversalStarted"),
            ("supported rim steering", "magnitude > 0.0f && i < 4", "magnitude > 0.0f && i < 0"),
            ("rim steering support", "navMesh.LandingContinuationClear( origin, next ) &&",
             "true &&"),
            ("rim steering knock interval", "\n\t\tstatic_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack() <= 0",
             "\n\t\tstatic_cast<idPhysics_Player *>( self->GetPlayerPhysics() )->GetKnockBack() <= 1000"),
            ("rim steering speed limit", "flat.LengthSqr() <= Square( BOT_RECOVER_EDGE_SPEED )",
             "flat.LengthSqr() <= Square( 1000.0f )"),
            ("supported rim continuation", "navMesh.RecoveryStepClear( origin, stop, &supported )",
             "false && navMesh.RecoveryStepClear( origin, stop, &supported )"),
            ("grounded knock coast", "if ( self->pfl.onGround && !transported && !traversalStarted &&",
             "if ( false && self->pfl.onGround && !transported && !traversalStarted &&"),
            ("early grounded steering", "!proven && i < 16", "!proven && i < 0"),
            ("low-speed edge brake", "moveDir *= Min( 1.0f, speed / Max( 1.0f, pm_speed.GetFloat() ) );",
             "moveDir *= 1.0f;"),
            ("self splash damage scale", "( 1.0f - distance / model.splashRadius ) * model.attackerScale",
             "( 1.0f - distance / model.splashRadius )"),
            ("self splash push scale", "(float)knockback * model.selfPushScale / 200.0f",
             "(float)knockback / 200.0f"),
            ("self splash knockback timer", "idMath::ClampInt( 50, 200, knockback * 2 ), idMath::INFINITY",
             "200, idMath::INFINITY"),
            ("zero-push friction interval", "model.attackerScale <= 0.0f )",
             "model.attackerScale <= 0.0f || model.selfPushScale <= 0.0f )"),
        ):
            assert harness.count(anchor) == 1
            cpp.write_text(harness.replace(anchor, replacement), encoding="utf-8")
            subprocess.run(command, cwd=path, check=True)
            result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
            assert result.returncode != 0, f"{label} mutation escaped"
            print(f"{label}: reverted-fix mutation rejected")
        # A stale takeoff proof must not own movement after its flight loses
        # the landing. Exercise the failure control against production code.
        mutated = harness.replace("if ( dodgeHopAirborne && gameLocal.time >= dodgeHopCheckTime )",
                                  "if ( false && dodgeHopAirborne && gameLocal.time >= dodgeHopCheckTime )")
        assert mutated != harness
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe), "movement"], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "stale dodge proof mutation escaped"
        print("Dodge flight revalidation: disabled-check mutation rejected")
        mutated = harness.replace(
            "goalBestDistance = PathDistanceRemaining( self->GetPhysics()->GetOrigin() ) + pendingProgress;\n\tif ( !preserveProgress ) {",
            "if ( !preserveProgress ) {\n\t\tgoalBestDistance = PathDistanceRemaining( self->GetPhysics()->GetOrigin() );")
        assert mutated != harness
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe), "goals"], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "historical route-distance mutation escaped"
        print("Goal progress: historical-distance mutation rejected")


if __name__ == "__main__":
    main()
