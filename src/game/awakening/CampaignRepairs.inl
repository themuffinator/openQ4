/*
 * openQ4 campaign repairs. See LICENSES/QUAKE-4-SDK-EULA.rtf.
 * Only redirects identified alpha callbacks; installed maps remain untouched.
 */

#include "PrisonAlarms.h"

static int Awakening_PrepareCampaignRepairs( idMapFile *map ) {
	if ( gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ) {
		return 0;
	}
	idStr name = map->GetName();
	name.BackSlashesToSlashes();
	name.StripFileExtension();
	const int chapter = !name.Icmp( "maps/game/m03_airassault" ) ? 3 :
		!name.Icmp( "maps/game/m04_prison" ) ? 4 : 0;
	if ( !chapter ) {
		return 0;
	}
	// Same-name replacement maps own their scripts. Require the recovered
	// entry points and authored destinations before compiling dependent helpers.
	const bool originalFunctions = chapter == 3 ?
		gameLocal.program.FindFunction( "map_m03aa::engineFireOut" ) != NULL &&
		gameLocal.program.FindFunction( "map_m03aa::dropshiphealthcheck" ) != NULL :
		gameLocal.program.FindFunction( "cellblock1UpperSpawnMove" ) != NULL &&
		gameLocal.program.FindFunction( "unlockSecRoomDoors" ) != NULL &&
		gameLocal.program.FindFunction( "squadTopLoop7Pos" ) != NULL &&
		gameLocal.program.FindFunction( "cellblock3_prisoners" ) != NULL;
	bool anchorA = false, anchorB = false;
	for ( int i = 1; i < map->GetNumEntities(); ++i ) {
		const idDict &args = map->GetEntity( i )->epairs;
		if ( chapter == 3 ) {
			anchorA |= !idStr::Icmp( args.GetString( "name" ), "forwardgui" ) &&
				!idStr::Icmp( args.GetString( "gui_Parm3" ), "map_m03aa::engineFireOut" );
			anchorB |= !idStr::Icmp( args.GetString( "name" ), "drop_ship" );
		} else {
			anchorA |= !idStr::Icmp( args.GetString( "name" ), "CB1_upr_cvr_pos_1" );
			anchorB |= !idStr::Icmp( args.GetString( "name" ), "CB1_lwr_cvr_pos_1" );
		}
	}
	if ( !originalFunctions || !anchorA || !anchorB ) {
		return 0;
	}
	const char *script = chapter == 3 ? "scripts/openq4/awakening/m03_airassault.script" :
		"scripts/openq4/awakening/m04_prison_v3.script";
	if ( fileSystem->ReadFile( script, NULL, NULL ) <= 0 ) {
		gameLocal.Warning( "Awakening m%02d repairs unavailable: missing %s", chapter, script );
		return 0;
	}
	// Map script files participate in ordinary save recompilation. Never rewrite
	// the installed alpha program or its statement layout underneath old saves.
	gameLocal.program.CompileFile( script );
	return chapter;
}

static void Awakening_RepairMapArgs( int chapter, idDict &args ) {
	const char *name = args.GetString( "name" );
	if ( chapter == 3 && !idStr::Icmp( name, "forwardgui" ) &&
		!idStr::Icmp( args.GetString( "gui_Parm3" ), "map_m03aa::engineFireOut" ) ) {
		args.Set( "gui_Parm3", "oq4_m03::restartEngine" );
	} else if ( chapter == 4 ) {
		Awakening_RepairPrisonAlarm( args );
		if ( !idStr::Icmp( name, "trigger_relay_15" ) &&
			!idStr::Icmp( args.GetString( "call" ), "unlockSecRoomDoors" ) ) {
			args.Set( "call", "oq4_m04_v3::unlockSecurity" );
		}
		if ( !idStr::Icmp( name, "trigger_once_155" ) &&
			!idStr::Icmp( args.GetString( "call" ), "gruntAndGunnerAttack" ) ) {
			args.Set( "call", "oq4_m04_v3::turretApproach" );
		}
		if ( !idStr::Icmp( name, "tunnel_bot" ) &&
			!idStr::Icmp( args.GetString( "script_init" ), "tunnelReparBotWorking" ) ) {
			args.Set( "script_init", "oq4_m04::tunnelRepair" );
		}
		// rvMonsterTurret owns its idle/scan animation. playCycle is a mover
		// event and the alpha's initializer only produces unsupported-event warnings.
		if ( !idStr::Icmp( args.GetString( "classname" ), "env_q4x_prison_camera" ) &&
			!idStr::Icmp( args.GetString( "script_init" ), "secCamStart" ) ) {
			args.Delete( "script_init" );
			if ( !args.FindKey( "mins" ) && !args.FindKey( "maxs" ) && !args.FindKey( "size" ) ) {
				// The def's upright actor box reaches into the upper cell doors,
				// keeping camera 8 in Wait_Activated. The hanging camera's idle/scan
				// bounds span z=-35.16..3.80 and a horizontal radius below 31 units.
				// Cover its complete sweep at any map yaw without changing its pose.
				args.Set( "mins", "-32 -32 -36" );
				args.Set( "maxs", "32 32 4" );
			}
		}
		const bool upper = !idStr::Icmp( name, "target_null_41" ) || !idStr::Icmp( name, "target_null_42" );
		const bool lower = !idStr::Icmp( name, "target_null_47" ) || !idStr::Icmp( name, "target_null_48" );
		if ( ( upper || lower ) && !idStr::Icmp( args.GetString( "classname" ), "target_null" ) &&
			!idStr::Icmp( args.GetString( "spawn_script_init" ), "cellblock1UpperSpawnMove" ) ) {
			args.Set( "spawn_script_init", lower ? "oq4_m04_v2::lowerReinforcement" : "oq4_m04_v2::upperReinforcement" );
		}
		// Burst is the base weapon's attack, not a shipped upgrade. Do not invent
		// an upgrade or grant the later flamethrower in its place.
		if ( !idStr::Icmp( name, "target_give_1" ) &&
			!idStr::Icmp( args.GetString( "item13" ), "weaponmod_goobgun_burst" ) &&
			gameLocal.FindEntityDef( "weaponmod_goobgun_burst", false ) == NULL ) {
			args.Delete( "item13" );
		}
	}
}

static void Awakening_StartCampaignRepairs( int chapter ) {
	if ( chapter == 3 ) {
		// The alpha misspells one reference at the start of the first attack wave.
		// Bind its existing script slot without renaming the real actor or changing
		// bytecode. Script globals then preserve the reference across saves.
		if ( !gameLocal.FindEntity( "navjas" ) ) {
			gameLocal.program.SetEntity( "navjas", gameLocal.FindEntity( "navajas" ) );
		}
		const function_t *monitor = gameLocal.program.FindFunction( "oq4_m03::monitorRepairs" );
		if ( monitor ) {
			( new idThread( monitor ) )->DelayedStart( 0 );
		}
	} else if ( chapter == 4 && !gameLocal.FindEntity( "gunner_3_toBridge" ) ) {
		idEntity *ambush = gameLocal.FindEntity( "trigger_once_130" );
		idEntity *bridge = gameLocal.FindEntity( "strogg_ctrlRm_jumper_1_pos_1" );
		if ( ambush && bridge && !idStr::Icmp( ambush->spawnArgs.GetString( "call" ), "bridgeGunnerGo" ) &&
			!idStr::Icmp( bridge->spawnArgs.GetString( "classname" ), "target_null" ) ) {
			// Its missing destination leaves the delayed gunner outside the fight.
			// Use the authored enemy staging point at the upper bridge approach.
			// The entrance trigger lies
			// below the catwalk, outside this gunner's connected walking AAS areas;
			// it cannot follow the marines' scripted drop to the lower platform.
			// Stay before the scripted drop; the farther marine marker is beyond
			// the platform edge. Retain this marker's authored height. Its slot is
			// serialized normally, just like the Navajas reference above.
			gameLocal.program.SetEntity( "gunner_3_toBridge", bridge );
		}
	}
	if ( chapter ) {
		gameLocal.Printf( "Awakening m%02d: campaign repairs enabled\n", chapter );
	}
}
