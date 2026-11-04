#!/usr/bin/env python3
"""Prove player-hull support and jump/drop landings on synthetic geometry."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_bot_navmesh_search import function

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    nav = (ROOT / "src/mpgame/bots/NavMesh.cpp").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(r"^static const (?:int|float)\s+NAV_\w+\s*=\s*[0-9][^\n]+", nav, re.M))
    bodies = "\n".join(function(nav, signature) for signature in (
        "static float NavLandingImpactDelta(",
        "static bool NavFloorSupported(",
        "static bool NavLandingSupported(",
        "static bool NavDropBeside(",
        "static bool NavRimNode(",
        "bool rvNavMesh::SampleFloor(",
        "static bool NavSweepNearlyClear(",
        "static bool NavWalkFailure(",
        "bool rvNavMesh::WalkableLine(",
        "bool rvNavMesh::TryStep(",
        "bool rvNavMesh::RunningDropClear(",
        "bool rvNavMesh::LandingContinuationClear(",
        "static bool NavRecoveryFootprintContains(",
        "static bool NavRecoverySharedSupport(",
        "bool rvNavMesh::RecoveryBrakeClear(",
        "bool rvNavMesh::RecoveryStepClear(",
        "bool rvNavMesh::GapJumpClear(",
        "bool rvNavMesh::JumpTakeoffClear(",
        "bool rvNavMesh::DodgeJumpClear(",
        "bool rvNavMesh::PadLanding(",
        "bool rvNavMesh::LandingSurvivable(",
        "bool rvNavMesh::AirInputLandingSafe(",
        "bool rvNavMesh::KnockSlideLandingSafe(",
    ))
    physics = (ROOT / "src/mpgame/physics/Physics_Player.cpp").read_text(encoding="utf-8")
    constants += "\n" + "\n".join(re.findall(r"^const int PMF_.*$", physics, re.M))
    bodies = "\n".join(function(physics, signature) for signature in (
        "void idPhysics_Player::SetKnockBack(", "void idPhysics_Player::RestoreKnockBack(",
        "int idPhysics_Player::GetKnockBack(")) + "\n" + bodies
    bodies = function(physics, "idVec3 idPhysics_Player::AdjustVertically(") + "\n" + function(nav, "static bool NavPadLaunchVelocity(") + "\n" + bodies
    for name, value in (("PM_ACCELERATE_MP", 15.0), ("PM_FRICTION", 6.0), ("PM_STOPSPEED", 100.0)):
        assert float(re.search(rf"const float {name}\s*=\s*([\d.]+)f", physics)[1]) == value, name
    header = (ROOT / "src/mpgame/bots/NavMesh.h").read_text(encoding="utf-8")
    bodies = function(header, "ID_INLINE bool NavAirSteer(").replace("ID_INLINE ", "") + "\n" + bodies
    bot = (ROOT / "src/mpgame/bots/Bot.cpp").read_text(encoding="utf-8")
    bodies += "\n" + function(bot, "static bool BotRidesPadArc(")
    item = (ROOT / "src/mpgame/Item.cpp").read_text(encoding="utf-8")
    bodies += "\n" + function(item, "bool idItem::TouchesPickup(")
    bodies += "\n" + function(bot, "static int BotRecoveryHealthAt(")
    bodies += "\n" + function(bot, "static bool BotChooseAirInput(")
    constants += "\n" + re.search(r"^static const float\s+BOT_EDGE_LOOKAHEAD[^\n]+", bot, re.M).group()
    constants += "\n" + re.search(r"^static const float\s+NAV_AIR_STEER_DEADBAND[^\n]+", header, re.M).group()
    hull = function(nav, "static idTraceModel NavCreateAgentTrace(")
    trace_model = (ROOT / "src/idlib/geometry/TraceModel.cpp").read_text(encoding="utf-8")
    cylinder = function(trace_model, "void idTraceModel::SetupCylinder( const idBounds &")
    fixture = (ROOT / "tools/tests/native/MpBotJumpTrajectoriesFixture.cpp").read_text(encoding="utf-8")
    fixture = fixture.replace("// PRODUCTION_CONSTANTS", constants).replace("// PRODUCTION_FUNCTIONS", bodies)
    fixture = fixture.replace("// PRODUCTION_HULL", cylinder + "\n" + hull)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the jump trajectory proofs")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-jumps-", dir=output) as directory:
        path = Path(directory)
        cpp, exe = path / "jumps.cpp", path / ("jumps.exe" if os.name == "nt" else "jumps")
        cpp.write_text(fixture, encoding="utf-8")
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-const-variable",
                   str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)
        origin_anchor = "idVec3 position = origin;\n\tidVec3 velocity = velocityIn;\n\tint unstuck = 0;"
        assert fixture.count(origin_anchor) == 1
        mutated = fixture.replace(origin_anchor,
            "idVec3 position = origin + idVec3( 0.0f, 0.0f, NAV_GROUND_OFFSET );\n\tidVec3 velocity = velocityIn;\n\tint unstuck = 0;")
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "lifted air-recovery origin mutation escaped"
        print("Live air-recovery origin: artificial lift mutation rejected")
        mutated = fixture.replace(
            "if ( navMesh.AirInputLandingSafe( origin, velocity, wish, limit, NULL, NULL, &heldTouch, &heldBrake, knockMsec, self ) || heldBrake )",
            "if ( wish.LengthSqr() > 0.0f && ( navMesh.AirInputLandingSafe( origin, velocity, wish, limit, NULL, NULL, &heldTouch, &heldBrake, knockMsec, self ) || heldBrake ) )")
        assert mutated != fixture
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "discarded safe-coast mutation escaped"
        print("Proven air coast: nonzero-only mutation rejected")
        anchor = "for ( int elapsed = 0; elapsed < 1000; elapsed += 16 )"
        assert fixture.count(anchor) == 1
        cpp.write_text(fixture.replace(anchor,
            "for ( int elapsed = 1000; elapsed < 1000; elapsed += 16 )"), encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "omitted friction-free slide mutation escaped"
        print("Grounded knockback: omitted slide mutation rejected")
        anchor = "if ( idMath::Fabs( trace.c.normal.z ) < 0.01f )"
        assert fixture.count(anchor) == 1
        cpp.write_text(fixture.replace(anchor, "if ( false && idMath::Fabs( trace.c.normal.z ) < 0.01f )"), encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "missing air-step mutation escaped"
        print("Descending recovery: missing air-step mutation rejected")
        mutated = fixture.replace("!RecoveryStepClear( trace.endpos, stop, &supportedOrigin )", "true")
        assert mutated != fixture
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "rim-recovery rejection mutation escaped"
        print("Inward rim landing: old rejection mutation failed")
        for label, anchor, replacement in (
            ("wall-arrested recovery", "if ( !NavAgentTranslation( across, position, next ) ) {\n\t\t\t// A vertical wall",
             "if ( !NavAgentTranslation( across, position, next ) ) { return false;\n\t\t\t// A vertical wall"),
            ("rim braking continuation", "*brakedAffordable = RecoveryBrakeClear( trace, flat, dt * ( 1.0f - trace.fraction ),",
             "*brakedAffordable = false && RecoveryBrakeClear( trace, flat, dt * ( 1.0f - trace.fraction ),"),
            ("landing frame remainder", "float seconds = remainingSeconds;", "float seconds = 0.0f;"),
            ("continuous braking support", "!NavRecoverySharedSupport( position, next ) ||", "false ||"),
            ("braking contact", "!NavAgentHasGroundContacts( next, gameLocal.GetGravity() )", "false"),
            ("exact replay timer", "current.movementFlags &= ~PMF_ALL_TIMES;", "return;"),
            ("zero replay timer", "if ( current.movementTime > 0 ) {", "if ( true ) {"),
            ("knockback expiry before movement", "elapsed + 16 < knockMsec", "elapsed < knockMsec"),
            ("collision without ground contact", "if ( !NavAgentHasGroundContacts( end, gravity ) ) {", "if ( false ) {"),
            ("fatal grounded impact", "if ( !NavAgentHasGroundContacts( end, gravity ) ) {", "if ( true ) {"),
            ("affordable contact without grounding", "if ( !NavAgentHasGroundContacts( end, gravity ) ) {",
             "if ( NavLandingImpactDelta( trace, velocity + gravity * ( dt * trace.fraction ), gravity ) > maxImpactDelta && !NavAgentHasGroundContacts( end, gravity ) ) {"),
            ("coasting pad catch", "if ( transport >= 0 ) {", "if ( false && transport >= 0 ) {"),
            ("disabled recovery pad", "!pad->IsActive( TH_THINK ) ||", "false ||"),
            ("unproven recovery pad", "!NaturalPadLanding( transport, registeredLanding ) ||", "false ||"),
            ("initial pad impulse", "frame == 0 || ( caughtPad", "false || ( caughtPad"),
            ("steered pad approach", "frame == 0 || ( caughtPad", "frame == 0 || steering || ( caughtPad"),
            ("coast after pad catch", "steering = false;", "/* keep the approach input after launch */"),
            ("second recovery pad", "( caughtPad >= 0 && transport != caughtPad ) ||", "false ||"),
            ("descending splash momentum", "idVec3 velocity = velocityIn;\n\tconst float dt = NAV_PAD_STEP_SECONDS;",
             "idVec3 velocity( velocityIn.x, velocityIn.y, 0.0f );\n\tconst float dt = NAV_PAD_STEP_SECONDS;"),
            ("affordable last-resort contact", "*touchedAffordable = true;", "*touchedAffordable = false;"),
            ("grounded steering", "velocity += wish * Min( add, ( knockActive ? NAV_PAD_AIR_ACCELERATE : 15.0f ) * dt * speed );",
             "velocity += wish * 0.0f;"),
        ):
            assert fixture.count(anchor) == 1
            cpp.write_text(fixture.replace(anchor, replacement), encoding="utf-8")
            subprocess.run(command, cwd=path, check=True)
            result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
            assert result.returncode != 0, f"{label} mutation escaped"
            print(f"{label}: reverted-fix mutation rejected")
        mutated = fixture.replace(
            "const float groundSpeed = speed * ( groundWish ? Min( 1.0f, inputLength ) : 1.0f );",
            "const float groundSpeed = speed;")
        assert mutated != fixture
        cpp.write_text(mutated, encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        result = subprocess.run([str(exe)], cwd=path, capture_output=True, text=True)
        assert result.returncode != 0, "full-speed-only drop mutation escaped"
        print("Careful drop: full-speed-only mutation rejected")
        registry = (ROOT / "tools/tests/native/MpBotTransportFixture.cpp").read_text(encoding="utf-8")
        registry_bodies = "\n".join(function(nav, signature) for signature in (
            "static bool NavPadVolumeTouches(",
            "void rvNavMesh::AddOffMeshLinks(",
            "void rvNavMesh::SealTransportVolumes(",
            "int rvNavMesh::TransportAt(",
            "bool rvNavMesh::NaturalPadLanding(",
            "bool rvNavMesh::IsSteeredPadLanding(",
        ))
        # Keep the second native fixture separate: Windows can briefly retain
        # the just-executed image even after its process has exited.
        transport_cpp = path / "transports.cpp"
        transport_exe = path / ("transports.exe" if os.name == "nt" else "transports")
        command[command.index(str(cpp))] = str(transport_cpp)
        command[command.index(str(exe))] = str(transport_exe)
        cpp, exe = transport_cpp, transport_exe
        cpp.write_text(registry.replace("// PRODUCTION_FUNCTIONS", registry_bodies), encoding="utf-8")
        subprocess.run(command, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)


if __name__ == "__main__":
    main()
