#!/usr/bin/env python3
"""The Awakening's monsters, weapons and vehicles keep the behaviour their
content was built for (the gameplay audit, docs/dev/archive/2026-10-18/plans/q4x-awakening.md
Phase 5).

- The m09 Valkaryne runs her "requestDocking" script, found in any namespace.
- "ignoreplayer" is a target filter, so every way an enemy reaches an AI
  (sight, sound, a teammate, pain, a script) honours it.
- The Pain Lord gets back a damage zone that a later zone swallowed whole.
- A Retch without its def's AAS walks on aas48.
- The Tank's damaged launcher scatters its rockets (accuracy 10), and either
  break ends its rocket attack.
- Elite grunts pull with a gravity well, elite gunners spread their fire,
  tactical elites throw grenades (only their model has the animation).
- Space flyers honour canturn, noFaceEnemy and their master's pitch and roll.
- The goob gun's burst, burn stacking, alternate muzzle flash and the player's
  flamethrower pose; the spike's impact force and fixed rotation.
- A vehicle's script GUI events and parameters reach its HUD, as the
  expansion's rvVehicle passed them on: m07's race timer and m06's MCC health.
- m09's flares are not single quads; R_FlareDeform warns once per material.
- The cockpit cannons' god mode, reset on exit, jam lock (on by default only
  under q4xbase) and the jam text the overheat must not clear early.
- The pulse cannon's barrels kick and fold as the expansion moved them, and
  unselected vehicle weapons get a per-frame call (empty for retail's), so the
  cannon cools and the launcher reloads while the other is selected.
- Retail's cinematic harvester, which has no turn animations, never tries to
  turn in place (m07's races).
- Map scripts' g_fov becomes a view offset, and m07's hurt volumes honour
  velscale.
- The headless test tools the audit relies on stay registered.

None of these keys appears in retail content, so stock behaviour is unchanged,
except that retail's cinematic harvester no longer fails a turn it has no
animation for.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = source.find("{", start + len(signature))
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        index = text.find(needle, cursor + 1)
        if index < 0:
            raise AssertionError(f"Missing {needle!r} after earlier tokens in {context}")
        cursor = index


def check_valkaryne() -> None:
    source = read("src/game/awakening/ai/Monster_Valkaryne.cpp")
    actions = function_body(source, "bool riMonsterValkaryne::CheckActions(", "Monster_Valkaryne.cpp")
    require(actions, "RequestDocking()", "riMonsterValkaryne::CheckActions")
    request = function_body(source, "bool riMonsterValkaryne::RequestDocking(", "Monster_Valkaryne.cpp")
    require_order(request, ('"requestDocking"', '"openq4_dockingRequested"',
                            "FindScriptFunctionAnyNamespace(", "CallFunction(", "DelayedStart("),
                  "riMonsterValkaryne::RequestDocking")
    lookup = function_body(source, "static const function_t *FindScriptFunctionAnyNamespace(",
                           "Monster_Valkaryne.cpp")
    require_order(lookup, ("gameLocal.program.FindFunction( name )", "ev_namespace", '"::"'),
                  "FindScriptFunctionAnyNamespace")


def check_target_filter() -> None:
    source = read("src/game/ai/AI.cpp")
    init = function_body(source, "void idAI::InitNonPersistentSpawnArgs (", "AI.cpp")
    require(init, 'ignorePlayer = spawnArgs.GetBool( "ignoreplayer" );', "idAI::InitNonPersistentSpawnArgs")
    allowed = function_body(source, "bool idAI::IsAllowedTarget(", "AI.cpp")
    require(allowed, "ignorePlayer && ent->IsType( idPlayer::GetClassType() )", "idAI::IsAllowedTarget")
    require(function_body(source, "bool idAI::SetEnemy(", "AI.cpp"), "IsAllowedTarget( newEnemy )",
            "idAI::SetEnemy")
    require(function_body(source, "idEntity *idAI::FindEnemy (", "AI.cpp"), "IsAllowedTarget( actor )",
            "idAI::FindEnemy")
    require(read("src/game/ai/AI.h"), "bool					ignorePlayer;", "AI.h")


def check_pain_lord() -> None:
    source = read("src/game/awakening/ai/Monster_PainLord.cpp")
    require(function_body(source, "void riMonsterPainLord::Spawn(", "Monster_PainLord.cpp"),
            "RestoreShadowedDamageZones();", "riMonsterPainLord::Spawn")
    zones = function_body(source, "void riMonsterPainLord::RestoreShadowedDamageZones(", "Monster_PainLord.cpp")
    require_order(zones, ('MatchPrefix( "damage_zone " )', "damageGroups.FindIndex( zone ) >= 0",
                          '"damage_scale %s"', "damageGroups[ jointList[ i ] ] = zone;",
                          "damageScale[ jointList[ i ] ] = scale;"),
                  "riMonsterPainLord::RestoreShadowedDamageZones")


def check_retch() -> None:
    source = read("src/game/awakening/ai/Monster_Retch.cpp")
    require(source, 'RETCH_FALLBACK_AAS = "aas48"', "Monster_Retch.cpp")
    require(function_body(source, "void riMonsterRetch::Spawn(", "Monster_Retch.cpp"), "UseFallbackAAS();",
            "riMonsterRetch::Spawn")
    require(function_body(source, "void riMonsterRetch::Restore(", "Monster_Retch.cpp"), "UseFallbackAAS();",
            "riMonsterRetch::Restore")
    fallback = function_body(source, "void riMonsterRetch::UseFallbackAAS(", "Monster_Retch.cpp")
    require_order(fallback, ("aas != NULL", "gameLocal.GetAAS( RETCH_FALLBACK_AAS )",
                             "ValidForBounds( settings, physicsObj.GetBounds() )", "aas = fallback;"),
                  "riMonsterRetch::UseFallbackAAS")
    require(function_body(source, "void riMonsterRetch::OnTacticalChange(", "Monster_Retch.cpp"),
            "AITACTICAL_MELEE", "riMonsterRetch::OnTacticalChange")


def check_tank() -> None:
    source = read("src/game/awakening/ai/Monster_Tank.cpp")
    require(source, "TANK_DAMAGED_ROCKET_ACCURACY = 10.0f;", "Monster_Tank.cpp")
    breaks = function_body(source, "void riMonsterTank::BreakPart(", "Monster_Tank.cpp")
    require_order(breaks, ("HideSurface( surface );", '"fx_railgun_explode"', "actionRocketAttack.fl.disabled = true;"),
                  "riMonsterTank::BreakPart")
    require(function_body(source, "void riMonsterTank::DestroyChest(", "Monster_Tank.cpp"), '"fx_chest_explode"',
            "riMonsterTank::DestroyChest")
    require(function_body(source, "void riMonsterTank::DestroyLauncher(", "Monster_Tank.cpp"),
            '"fx_launcher_explode"', "riMonsterTank::DestroyLauncher")


def check_elites() -> None:
    grunt = read("src/game/ai/Monster_Grunt.cpp")
    require(grunt, 'q4xElite = spawnArgs.GetBool ( "q4xElite" );', "Monster_Grunt.cpp")
    require(grunt, 'STATE ( "Torso_GravityWell",', "Monster_Grunt.cpp")
    require(grunt, '"gravity_well"', "Monster_Grunt.cpp")
    gunner = read("src/game/ai/Monster_Gunner.cpp")
    require(gunner, 'q4xElite = spawnArgs.GetBool( "q4xElite" );', "Monster_Gunner.cpp")
    require(gunner, 'PostAnimState ( ANIMCHANNEL_TORSO, "Torso_SpreadFire"', "Monster_Gunner.cpp")
    tactical = read("src/game/ai/AI_Tactical.cpp")
    init = function_body(tactical, "void rvAITactical::InitGrenadeThrow (", "AI_Tactical.cpp")
    require_order(init, ('"action_throwgrenade"', '"grenadedelay"', 'animator.HasAnim ( "throw_grenade" )'),
                  "rvAITactical::InitGrenadeThrow")
    require(function_body(tactical, "void rvAITactical::Spawn (", "AI_Tactical.cpp"), "InitGrenadeThrow ( );",
            "rvAITactical::Spawn")
    require(function_body(tactical, "void rvAITactical::Restore(", "AI_Tactical.cpp"), "InitGrenadeThrow ( );",
            "rvAITactical::Restore")
    require(function_body(tactical, "bool rvAITactical::CheckActions (", "AI_Tactical.cpp"),
            "canThrowGrenades && PerformAction ( &actionThrowGrenade", "rvAITactical::CheckActions")


def check_flyers() -> None:
    flyer = read("src/game/ai/Monster_StroggFlyer.cpp")
    require(flyer, 'canTurn					= spawnArgs.GetBool ( "canturn", "1" );', "Monster_StroggFlyer.cpp")
    require(function_body(flyer, "bool rvMonsterStroggFlyer::CanTurn (", "Monster_StroggFlyer.cpp"),
            "canTurn && idAI::CanTurn ( )", "rvMonsterStroggFlyer::CanTurn")
    require(read("src/game/ai/AI_Move.cpp"), 'faceTargets = !spawnArgs.GetBool( "noFaceEnemy" );', "AI_Move.cpp")
    think = function_body(read("src/game/ai/AI.cpp"), "void idAI::Think(", "AI.cpp")
    require_order(think, ('spawnArgs.GetBool( "useMasterPitch" )', "GetMasterPosition(",
                          'spawnArgs.GetBool( "useMasterRoll" )'), "idAI::Think")
    # the movers they ride bank into turns, in the Awakening only
    parametric = read("src/game/physics/Physics_Parametric.cpp")
    require(function_body(parametric, "static bool Parametric_SplineBanking(", "Physics_Parametric.cpp"),
            "FS_AwakeningActive()", "Parametric_SplineBanking")
    require_order(function_body(parametric, "static float Parametric_SplineRoll(", "Physics_Parametric.cpp"),
                  ("g_splineRollLookahead.GetFloat()", "t + 2.0f * lookahead", "idMath::TWO_PI",
                   "-g_splineRollMultiplier.GetFloat() * RAD2DEG( turn )"), "Parametric_SplineRoll")
    require_order(function_body(parametric, "bool idPhysics_Parametric::Evaluate(", "Physics_Parametric.cpp"),
                  ("current.useSplineAngles", "if ( splineBanking )", "Parametric_SplineRoll("),
                  "idPhysics_Parametric::Evaluate")
    for signature in ("void idPhysics_Parametric::SetSpline(", "void idPhysics_Parametric::Restore("):
        require(function_body(parametric, signature, "Physics_Parametric.cpp"),
                "splineBanking = Parametric_SplineBanking( );", signature)
    cvars = read("src/game/gamesys/SysCvar.cpp")
    require(cvars, '"g_splineRollLookahead",	"500"', "SysCvar.cpp")
    require(cvars, '"g_splineRollMultiplier",	"2.0"', "SysCvar.cpp")


def check_weapons() -> None:
    projectile = read("src/game/Projectile.cpp")
    require(projectile, 'spawnArgs.GetString("def_impactEntity", spawnArgs.GetString("impactEntity",""))',
            "Projectile.cpp")
    require(projectile, 'applyRotation		= spawnArgs.GetBool( "applyRotation", "1" );', "Projectile.cpp")
    force = function_body(projectile, "void idProjectile::ApplyImpactForce(", "Projectile.cpp")
    require_order(force, ('spawnArgs.GetFloat( "impactForce", "0" )', "force == 0.0f"),
                  "idProjectile::ApplyImpactForce")
    require(function_body(projectile, "bool idProjectile::Collide( const trace_t &collision, const idVec3 &velocity, "
                                      "bool &hitTeleporter )", "Projectile.cpp"),
            "ApplyImpactForce(ent, collision, dir);", "idProjectile::Collide")
    require(read("src/game/Game_local.cpp"), '!kv->GetKey().Icmp( "impactEntity" )', "Game_local.cpp")
    require(read("src/game/Weapon.cpp"), '"fx_altmuzzleflash"', "Weapon.cpp")
    torso_fire = function_body(read("src/game/Player_States.cpp"), "stateResult_t idPlayer::State_Torso_Fire (",
                               "Player_States.cpp")
    require_order(torso_fire, ("weapon->wsfl.zoom", 'GetBool( "useAltFireAnim" )', '"fire_alt"'),
                  "idPlayer::State_Torso_Fire")
    dot = read("src/game/awakening/DOTEntity.cpp")
    require(dot, "static const int DOT_MAX_STACKS = 3;", "DOTEntity.cpp")
    require(function_body(dot, "bool DOTEntity::FeedExistingBurn(", "DOTEntity.cpp"), "DOT_MAX_STACKS",
            "DOTEntity::FeedExistingBurn")


def check_vehicles() -> None:
    position = function_body(read("src/game/vehicle/VehiclePosition.cpp"), "bool rvVehiclePosition::SetDriver (",
                             "VehiclePosition.cpp")
    require_order(position, ('"useGodMode"', '"openq4_gaveGodMode", true', '"openq4_gaveGodMode"',
                             "godmode = false;"), "rvVehiclePosition::SetDriver")
    parts = read("src/game/vehicle/VehicleParts.cpp")
    lock = function_body(parts, "void rvVehicleTurret::ReadMovementLock (", "VehicleParts.cpp")
    require_order(lock, ("FS_AwakeningActive()", '"allowDisableMovement", awakening ? "1" : "0"'),
                  "rvVehicleTurret::ReadMovementLock")
    require(function_body(parts, "void rvVehicleTurret::Restore (", "VehicleParts.cpp"), "ReadMovementLock ( );",
            "rvVehicleTurret::Restore")
    require(function_body(parts, "void rvVehicleTurret::RunPostPhysics (", "VehicleParts.cpp"),
            "parent->IsMovementEnabled ( )", "rvVehicleTurret::RunPostPhysics")
    require(function_body(parts, "void rvVehicleTurret::Activate (", "VehicleParts.cpp"), '"resetOnExit"',
            "rvVehicleTurret::Activate")
    cockpit = read("src/game/awakening/vehicle/CockpitWeapons.cpp")
    cool = function_body(cockpit, "void riVCWPulseCannon::Cool(", "CockpitWeapons.cpp")
    require_order(cool, ("vehicle->IsShootingEnabled()", 'SendGuiEvent( "JammedTextOff" )'),
                  "riVCWPulseCannon::Cool")
    # a vehicle's script GUI events and parameters reach its HUD (m07's race timer, m06's MCC health)
    vehicle = read("src/game/vehicle/Vehicle.cpp")
    table = vehicle[vehicle.find("CLASS_DECLARATION( idActor, rvVehicle )"):]
    table = table[:table.find("END_CLASS")]
    require(table, "EVENT( EV_GuiEvent,", "rvVehicle event table")
    require(table, "EVENT( EV_SetGuiParm,", "rvVehicle event table")
    require_order(function_body(vehicle, "void rvVehicle::Event_GuiEvent (", "Vehicle.cpp"),
                  ("hud->HandleNamedEvent ( eventName );", "idEntity::Event_GuiEvent ( eventName );"),
                  "rvVehicle::Event_GuiEvent")
    require_order(function_body(vehicle, "void rvVehicle::Event_SetGuiParm (", "Vehicle.cpp"),
                  ("hud->SetStateString ( key, value );", "hud->StateChanged ( gameLocal.time );",
                   "idEntity::Event_SetGuiParm ( key, value );"), "rvVehicle::Event_SetGuiParm")
    # the HUD's view cone and the bike's speedometer
    require_order(function_body(vehicle, "void rvVehicle::UpdateHUD (", "Vehicle.cpp"),
                  ('gui->SetStateFloat ( "playerYaw",', "GetLinearVelocity().Length() * 0.25f",
                   'renderEntity.gui[ i ]->SetStateInt ( "vehicle_speed", speed );'), "rvVehicle::UpdateHUD")
    player_source = read("src/game/Player.cpp")
    require(function_body(player_source, "void idPlayer::DrawHUD(", "Player.cpp"),
            '_hud->SetStateFloat( "playerYaw",', "idPlayer::DrawHUD")
    # the cursor's "wait" talk crosshair
    require_order(function_body(player_source, "void idPlayer::UpdateFocusCharacter(", "Player.cpp"),
                  ('cursor->SetStateInt( "npc_talkstate",', "GetTalkState()"), "idPlayer::UpdateFocusCharacter")


def check_cockpit_barrels() -> None:
    # every weapon but the selected one gets a per-frame call, as in the expansion's
    # rvVehiclePosition; it is empty by default, so retail vehicles are unchanged
    require(read("src/game/vehicle/VehicleParts.h"), "virtual void				RunInactivePostPhysics	( void ) { }",
            "rvVehiclePart")
    post = function_body(read("src/game/vehicle/VehiclePosition.cpp"), "void rvVehiclePosition::RunPostPhysics (",
                         "VehiclePosition.cpp")
    require_order(post, ("if ( i != mCurrentWeapon ) {", "mWeapons[i]->RunInactivePostPhysics ( );",
                         "mWeapons[mCurrentWeapon]->RunPostPhysics ( );"), "rvVehiclePosition::RunPostPhysics")
    cockpit = read("src/game/awakening/vehicle/CockpitWeapons.cpp")
    require(cockpit, '{ "pulse_cannon_l", "pulse_cannon_r" }', "CockpitWeapons.cpp")
    for constant in ("BARREL_RECOIL			= 32.0f", "BARREL_KICK_SEC			= 0.01f", "BARREL_RETURN_SEC		= 0.1f",
                     "BARREL_FOLD_SEC			= 0.4f", "BARREL_UNFOLD_SEC		= 0.2f", "BARREL_FOLD_OFFSET		= 12.0f",
                     "BARREL_FOLD_ANGLE_X		= -7.5f", "BARREL_FOLD_ANGLE_Z		= -15.0f"):
        require(cockpit, constant, "CockpitWeapons.cpp barrel constants")
    barrels = function_body(cockpit, "void riVCWPulseCannon::UpdateBarrels(", "CockpitWeapons.cpp")
    require_order(barrels, ("position->GetActiveWeapon() != this ) ? 1.0f : 0.0f",
                            "axis.RotateAbsolute( 0, barrel.fold * BARREL_FOLD_ANGLE_X );",
                            "axis.RotateRelative( 2, barrel.fold * BARREL_FOLD_ANGLE_Z );",
                            "SetJointPos( barrel.joint, JOINTMOD_LOCAL,", "SetJointAxis( barrel.joint, JOINTMOD_LOCAL, axis );"),
                  "riVCWPulseCannon::UpdateBarrels")
    fire = function_body(cockpit, "bool riVCWPulseCannon::Fire(", "CockpitWeapons.cpp")
    require_order(fire, ("const int firingBarrel = jointIndex;", "rvVehicleWeapon::Fire()",
                         "barrels[ firingBarrel ].kickState = BARREL_KICKING;"), "riVCWPulseCannon::Fire")
    require_order(function_body(cockpit, "void riVCWPulseCannon::RunInactivePostPhysics(", "CockpitWeapons.cpp"),
                  ("Cool();", "UpdateBarrels();"), "riVCWPulseCannon::RunInactivePostPhysics")
    require(function_body(cockpit, "void riVCWMissileTurret::RunInactivePostPhysics(", "CockpitWeapons.cpp"),
            "UpdateMagazine();", "riVCWMissileTurret::RunInactivePostPhysics")


def check_harvester_turn() -> None:
    # retail's cinematic harvester (m07's background one) has no turn animations
    actions = function_body(read("src/game/ai/Monster_Harvester.cpp"), "bool rvMonsterHarvester::CheckActions (",
                            "Monster_Harvester.cpp")
    require_order(actions, ('HasAnim ( ANIMCHANNEL_TORSO, "turn_90_lt" )', 'PerformAction ( "Torso_TurnLeft90"',
                            'HasAnim ( ANIMCHANNEL_TORSO, "turn_90_rt" )', 'PerformAction ( "Torso_TurnRight90"'),
                  "rvMonsterHarvester::CheckActions")


def check_scripts_and_triggers() -> None:
    thread = read("src/game/script/Script_Thread.cpp")
    require_order(thread, ('!gameLocal.isMultiplayer && !idStr::Icmp( name, "g_fov" )',
                           '"openq4_scriptFovOffset", atof( value ) - 90.0f'), "Script_Thread.cpp")
    require(function_body(read("src/game/Player.cpp"), "float idPlayer::DefaultFov(", "Player.cpp"),
            '"openq4_scriptFovOffset"', "idPlayer::DefaultFov")
    hurt = function_body(read("src/game/Trigger.cpp"), "void idTrigger_Hurt::Event_Touch(", "Trigger.cpp")
    require_order(hurt, ('spawnArgs.GetFloat( "velscale", "1" )', "SetLinearVelocity("), "idTrigger_Hurt::Event_Touch")


def check_flare_warning() -> None:
    # m09's flares are not single quads: warn once per material, not every frame
    deform = read("src/renderer/tr_deform.cpp")
    # the last definition is the live one; an older copy sits in a comment above it
    active = deform[deform.rfind("static void R_FlareDeform( drawSurf_t *surf )"):]
    body = function_body(active, "static void R_FlareDeform( drawSurf_t *surf )", "tr_deform.cpp")
    require_order(body, ("tri->numVerts != 4 || tri->numIndexes != 6",
                         "warnedMaterials.FindIndex( surf->material ) < 0", "warnedMaterials.Append( surf->material );",
                         '"R_FlareDeform: not a single quad (%s)"'), "R_FlareDeform")


def check_test_tools() -> None:
    commands = read("src/game/gamesys/SysCmds.cpp")
    for name in ('"openq4_testInput"', '"openq4_viewReport"'):
        require(commands, name, "SysCmds.cpp")
    damage = function_body(commands, "void Cmd_Damage_f(", "SysCmds.cpp")
    require(damage, "GetAnimator()->GetJointHandle( args.Argv( 4 ) )", "Cmd_Damage_f")
    player = read("src/game/Player.cpp")
    test_input = function_body(player, "void Cmd_OpenQ4TestInput_f(", "Player.cpp")
    for option in ('"yaw"', '"pitch"', '"impulse"', '"aim"'):
        require(test_input, option, "Cmd_OpenQ4TestInput_f")
    apply = function_body(player, "void idPlayer::ApplyTestInput(", "Player.cpp")
    require_order(apply, ("usercmd.flags ^= openq4TestInput.impulseFlip;", "UCF_IMPULSE_SEQUENCE"),
                  "idPlayer::ApplyTestInput")
    require(read("src/game/gamesys/SysCvar.cpp"), 'idCVar ai_debugActions(', "SysCvar.cpp")
    if re.search(r'"vampiric"', player):
        raise AssertionError("The vampiric gauntlet is multiplayer content; the SP module does not read it")


def main() -> int:
    check_valkaryne()
    check_target_filter()
    check_pain_lord()
    check_retch()
    check_tank()
    check_elites()
    check_flyers()
    check_weapons()
    check_vehicles()
    check_cockpit_barrels()
    check_harvester_turn()
    check_scripts_and_triggers()
    check_flare_warning()
    check_test_tools()
    print("awakening_gameplay_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
