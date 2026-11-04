/* Independent corrections to user-supplied Awakening scripts. No recovered
 * script is bundled. Keep v1/v2 stable: saves recompile these private namespaces.
 */
struct awakeningMissionCorrection_t { const char *before; const char *after; };
static idCVar g_awakeningM05M06( "g_awakeningM05M06", "1", CVAR_GAME | CVAR_BOOL,
	"Apply Awakening M05/M06 mission corrections on fresh map starts" );

static bool Awakening_CorrectSource( idStr &source, const awakeningMissionCorrection_t *rules, int count ) {
	for ( int i = 0; i < count; ++i ) {
		const int first = source.Find( rules[i].before );
		if ( first < 0 || source.Find( rules[i].before, true, first + 1 ) >= 0 ) {
			return false;
		}
		source.Replace( rules[i].before, rules[i].after );
	}
	return true;
}

static bool Awakening_ReadMissionSource( const char *filename, idStr &source ) {
	char *buffer = NULL;
	const int length = fileSystem->ReadFile( filename, reinterpret_cast<void **>( &buffer ), NULL );
	if ( length > 0 ) {
		source = buffer;
		source.Replace( "\r\n", "\n" );
	}
	if ( buffer ) {
		fileSystem->FreeFile( buffer );
	}
	return length > 0;
}

static const awakeningMissionCorrection_t awakeningPainLordV1[] = {
	{ "float PAINLORD_DEAD = 0;", "float PAINLORD_DEAD = 0;\n\tfloat PROCESSING_INTERRUPTED = 0;" },
	{ "void redLightOn()\n\t{", "void redLightOn()\n\t{\n\t\tif ( PAINLORD_DEAD ) { return; }" },
	// Claim the light before yielding: stun and processing must not toggle the
	// spawner twice. A delayed light-on must not survive a victory.
	{ "$spinnyLight.fadeInLight( 1 );", "RED_LIGHT_ON = 1;\n\t\t\t$spinnyLight.fadeInLight( 1 );" },
	{ "sys.wait( 1 );\n\t\t\tsys.trigger( $slimy_spawner_w );\n\t\t\tRED_LIGHT_ON = 1;",
	  "if ( RED_LIGHT_ON && !PAINLORD_DEAD && isValidEntity( $slimy_spawner_w ) ) { sys.trigger( $slimy_spawner_w ); }" },
	{ "else if ( PAINLORD_PROCESSING_REQUESTED )", "else if ( PAINLORD_PROCESSING_REQUESTED && !PAINLORD_DEAD )" },
	{ "sys.trigger( $slimy_spawner_w );\n\t\t\t\t$spinnyLight.fadeOutLight( 1 );",
	  "if ( isValidEntity( $slimy_spawner_w ) ) { sys.trigger( $slimy_spawner_w ); }\n\t\t\t\t$spinnyLight.fadeOutLight( 1 );" },
	// Strauss deserves the same console-animation interrupt window as the two
	// preceding captives; the alpha kills him as soon as the boss arrives.
	{ "PAINLORD_BUSY_WORKING = 1;", "PAINLORD_BUSY_WORKING = 1;\n\t\tPROCESSING_INTERRUPTED = 0;" },
	{ "void processingBroken()\n\t{", "void processingBroken()\n\t{\n\t\tPROCESSING_INTERRUPTED = 1;" },
	{ "$moverpod4.moveTo( $processingloc_down );",
	  "while ( PAINLORD_BUSY_WORKING ) { sys.waitFrame(); }\n"
	  "\t\twhile ( PROCESSING_INTERRUPTED && !PAINLORD_DEAD ) {\n"
	  "\t\t\tsys.wait( 40 );\n\t\t\tPAINLORD_PROCESSING_REQUESTED = 1;\n\t\t\t$theboss.requestProcessing();\n"
	  "\t\t\twhile ( PAINLORD_PROCESSING_REQUESTED || PAINLORD_BUSY_WORKING ) { sys.waitFrame(); }\n\t\t}\n"
	  "\t\t$moverpod4.moveTo( $processingloc_down );" },
	{ "PAINLORD_DEAD = 1;",
	  "PAINLORD_DEAD = 1;\n\t\tPAINLORD_PROCESSING_REQUESTED = 0;\n\t\tPAINLORD_BUSY_WORKING = 0;\n"
	  "\t\tsys.killthread( \"painlord::moveToProcessingStation\" );" },
};

static const awakeningMissionCorrection_t awakeningBioV1[] = {
	// The alpha comments out obj1 and has deleted both entities. Recreate the
	// intended rescue objective with localized text and the existing UI.
	{ "void main()\n\t{", "void main()\n\t{\n"
	  "\t\tsys.setSpawnArg( \"name\", \"oq4_m05_rescue_complete\" );\n"
	  "\t\tsys.setSpawnArg( \"objectivetitle\", \"#str_231680\" );\n"
	  "\t\tsys.setSpawnArg( \"inv_objective\", \"1\" );\n"
	  "\t\tentity completion = sys.spawn( \"item_objectivecomplete\" );\n"
	  "\t\tsys.setSpawnArg( \"name\", \"oq4_m05_rescue\" );\n"
	  "\t\tsys.setSpawnArg( \"objectivetitle\", \"#str_231680\" );\n"
	  "\t\tsys.setSpawnArg( \"objectivetext\", \"#str_231681\" );\n"
	  "\t\tsys.setSpawnArg( \"inv_objective\", \"1\" );\n"
	  "\t\tsys.setSpawnArg( \"no_touch\", \"1\" );\n"
	  "\t\tsys.setSpawnArg( \"nodrop\", \"1\" );\n"
	  "\t\tsys.setSpawnArg( \"screenshot\", \"gfx/objectives/bio_findstrauss\" );\n"
	  "\t\tentity rescue = sys.spawn( \"item_objective\" );\n" },
	{ "//sys.trigger( $obj1 );", "sys.trigger( $oq4_m05_rescue );" },
	{ "//sys.trigger( $obj1_complete );", "sys.trigger( $oq4_m05_rescue_complete );" },
};

static const awakeningMissionCorrection_t awakeningMccV1[] = {
	// Mitchell dies explicitly in M05. Remove the stale landing double.
	{ "$mitchell_intro.setMoveSpeed( AIMOVESPEED_WALK );", "$mitchell_intro.remove();" },
	{ "aiScriptedMove( $mitchell_intro, $introwalk_1, 32, 0 );", "" },
	{ "$mitchell_intro.remove();\n\t\t$navajas_intro.remove();", "$navajas_intro.remove();" },
};

static const awakeningMissionCorrection_t awakeningInvasionV1[] = {
	// Spawner callbacks return through shared globals. Serialize that short
	// handshake so two flight lanes cannot steal each other's vehicle/driver.
	{ "entity spawnedFighter;", "float fighterSpawnBusy = 0;\n\tentity spawnedFighter;" },
	{ "sys.trigger( currVehicleSpawner );",
	  "while ( fighterSpawnBusy ) { sys.waitFrame(); }\n\t\t\tif ( !spawnTurret1Fighters ) { return; }\n"
	  "\t\t\tfighterSpawnBusy = 1;\n\t\t\tspawnedFighter = $null_entity;\n\t\t\tsys.trigger( currVehicleSpawner );" },
	{ "currVehicle.bind( currMoverPitch );",
	  "if ( !isValidEntity( currVehicle ) ) { fighterSpawnBusy = 0; return; }\n\t\t\tcurrVehicle.bind( currMoverPitch );" },
	{ "sys.trigger( currVehicleDriverSpawner );", "spawnedFighterDriver = $null_entity;\n\t\t\tsys.trigger( currVehicleDriverSpawner );" },
	{ "currDriver.enterVehicle( currVehicle );",
	  "fighterSpawnBusy = 0;\n\t\t\tif ( !isValidEntity( currDriver ) || !isValidEntity( currVehicle ) ) {\n"
	  "\t\t\t\tif ( isValidEntity( currDriver ) ) { currDriver.remove(); }\n"
	  "\t\t\t\tif ( isValidEntity( currVehicle ) ) { currVehicle.remove(); }\n\t\t\t\treturn;\n\t\t\t}\n"
	  "\t\t\tcurrDriver.enterVehicle( currVehicle );" },
	// Vehicle removal ejects drivers; it does not delete them. Leaving one
	// alive blocks its max_active=1 spawner on every subsequent pass.
	{ "currMover.stopSpline();", "if ( isValidEntity( currDriver ) ) { currDriver.remove(); }\n\t\t\tcurrMover.stopSpline();" },
	{ "float TURRET_DEAD = 0;", "float defenseStarted = 0;\n\tfloat defenseActive = 0;" },
	// The installed model's frame callback retains the original namespace.
	{ "while ( !TURRET_DEAD )", "while ( !m06b::TURRET_DEAD )" },
	{ "TURRET_DEAD = 1;", "m06b::TURRET_DEAD = 1;" },
	{ "void shipDamaged()\n\t{",
	  "void shipDamaged()\n\t{\n\t\tif ( !defenseActive ) { return; }\n\t\tdefenseActive = 0;\n"
	  "\t\tspawnTurret1Fighters = 0;\n\t\tsys.killthread( \"m06b::turretSequence\" );\n"
	  "\t\tsys.killthread( \"m06b::startTurretSpawners\" );\n\t\tsys.killthread( \"m06b::skyboxDroppods\" );\n"
	  "\t\tsys.killthread( \"m06b::fighterGroup1\" );" },
	{ "$player1.enterVehicle( $turret );  //test", "" },
	{ "thread shipDamaged();", "shipDamaged();" },
	{ "while( 1 ) \n\t\t{\n\t\t\th = $mcc_2.getHealth();",
	  "while( defenseActive )\n\t\t{\n\t\t\th = $mcc_2.getHealth();" },
	{ "void turretSequence()\n\t{",
	  "void turretSequence()\n\t{\n\t\tif ( defenseStarted ) { return; }\n\t\tdefenseStarted = 1;\n\t\tdefenseActive = 1;" },
	{ "//$turret.lock( 1 ); // uncomment for release", "$turret.lock( 1 );" },
	// Allow all three authored flyer spawners to enter before escalation.
	{ "sys.println( \"NOT YET IMPLEMENTED\" );\n\t\tsys.wait( 2 );", "sys.wait( 20 );" },
	{ "// stop spawning plz", "defenseActive = 0;\n\t\tsys.killthread( \"m06b::startTurretSpawners\" );\n"
	  "\t\tsys.killthread( \"m06b::skyboxDroppods\" );\n\t\tsys.killthread( \"m06b::fighterGroup1\" );\n\t\t// stop spawning" },
	{ "void exitTurretSequence()\n\t{", "void exitTurretSequence()\n\t{\n\t\tdefenseActive = 0;" },
	// Save after the player has left the seat and arrived inside the MCC.
	{ "thread setupRouteToHangar();\n\n\t\t$turret.lock( 0 );", "$turret.lock( 0 );" },
	{ "sys.trigger( $player_post_turret ); // info_player_teleport",
	  "sys.trigger( $player_post_turret );\n\t\tsys.waitFrame();\n\t\tthread setupRouteToHangar();" },
};

// Apply additions after the frozen v1 rules. Old saves must still compile the
// exact v1 program, including its suspended statement offsets.
static const awakeningMissionCorrection_t awakeningPainLordV2[] = {
	{ "void moveToProcessingStation()\n\t{",
	  "void moveToProcessingStation()\n\t{\n\t\tif ( PAINLORD_DEAD || PAINLORD_BUSY_WORKING ) { return; }\n"
	  "\t\tsys.threadname( \"painlord::moveToProcessingStation\" );" },
	{ "aiScriptedFaceWait( $theboss, $painlord_working_face, 1 );",
	  "if ( PAINLORD_DEAD ) { return; }\n\t\taiScriptedFaceWait( $theboss, $painlord_working_face, 1 );\n"
	  "\t\tif ( PAINLORD_DEAD ) { return; }" },
};

static const awakeningMissionCorrection_t awakeningInvasionV2[] = {
	// Entity callbacks use rvScriptFuncUtility's anonymous thread constructor.
	// Give the live vehicle callback the same cancellable name as script calls.
	{ "defenseStarted = 1;\n\t\tdefenseActive = 1;",
	  "defenseStarted = 1;\n\t\tdefenseActive = 1;\n\t\tsys.threadname( \"m06b::turretSequence\" );" },
};

static const awakeningMissionCorrection_t awakeningBioV2[] = {
	// Script remove is deferred. Retire the cinematic double before the player
	// teleports into his position, while the existing fade is still black.
	{ "$dekard_boss_cinematic.remove();\n\t\tsys.trigger( $teleport_postboss );",
	  "$dekard_boss_cinematic.remove();\n\t\tsys.waitFrame();\n\t\tsys.trigger( $teleport_postboss );" },
};

static bool Awakening_CorrectTeleporterV2( idStr &source, const char *flag, const char *monitor,
	const char *spin, const char *core, const char *broken, const char *spawner ) {
	const idStr entry = va( "void %s()\n\t{", monitor );
	const idStr guarded = va( "float %sStarted = 0;\n\t%s\n"
		"\t\tif ( %sStarted || %s || !isValidEntity( $%s ) ) { return; }\n\t\t%sStarted = 1;",
		monitor, entry.c_str(), monitor, flag, core, monitor );
	const idStr reset = va( "%s = false;", flag );
	const idStr oldEnd = va( "%s = true;\n\t\t\t\t$%s.remove();", flag, spawner );
	const idStr show = va( "$%s.show();", broken );
	const idStr shutdown = va( "%s = true;\n\t\t\t\t$%s.stopRotating();\n\t\t\t\tsys.killthread( \"m06b::%s\" );\n"
		"\t\t\t\t$%s.remove();\n\t\t\t\t%s", flag, core, spin, spawner, show.c_str() );
	const awakeningMissionCorrection_t rules[] = {
		{ entry.c_str(), guarded.c_str() }, { reset.c_str(), "" },
		{ oldEnd.c_str(), "" }, { show.c_str(), shutdown.c_str() },
	};
	// stopRotating wakes waitFor so the cancelled thread can retire. Killing a
	// mover waiter alone leaves it suspended forever after the mover is removed.
	// Stop spawning and the mover waiter before deleting the core or yielding
	// for the authored break effects. Re-entry must not reset a destroyed core.
	return Awakening_CorrectSource( source, rules, sizeof( rules ) / sizeof( rules[0] ) );
}

static const awakeningMissionCorrection_t awakeningInvasionV3[] = {
	// The core can be destroyed during the crash sequence's three-second wait.
	// Its monitor removes the spawner immediately; never activate it afterward.
	{ "sys.trigger( $func_spawner_command_dropin );",
	  "if ( !commTeleporterDestroyed && isValidEntity( $comm_tele_ball ) && isValidEntity( $func_spawner_command_dropin ) ) {\n"
	  "\t\t\tif ( $comm_tele_ball.getHealth() > 1 ) { sys.trigger( $func_spawner_command_dropin ); }\n\t\t}" },
};

static bool Awakening_LoadM05M06Script( const char *filename, idStr &source ) {
	idStr file = filename;
	file.BackSlashesToSlashes();
	const bool version3 = file.Icmp( "maps/game/m06_mcc_invasion.openq4-v3.script" ) == 0;
	const bool version2 = version3 || file.Find( ".openq4-v2.script", false ) >= 0;
	if ( version3 ) {
		file.Replace( ".openq4-v3.script", ".openq4-v1.script" );
	} else if ( version2 ) {
		file.Replace( ".openq4-v2.script", ".openq4-v1.script" );
	}
	if ( gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) != 0 ) {
		return false;
	}
	if ( file.Icmp( "maps/game/m05_bio.openq4-v1.script" ) == 0 ) {
		idStr boss, level;
		if ( !Awakening_ReadMissionSource( "scripts/maps/m05_painlord.script", boss ) ||
			 !Awakening_ReadMissionSource( "scripts/maps/m05_bio.script", level ) ||
			 !Awakening_CorrectSource( boss, awakeningPainLordV1, sizeof( awakeningPainLordV1 ) / sizeof( awakeningPainLordV1[0] ) ) ||
			 !Awakening_CorrectSource( level, awakeningBioV1, sizeof( awakeningBioV1 ) / sizeof( awakeningBioV1[0] ) ) ) {
			return false;
		}
		if ( version2 && ( !Awakening_CorrectSource( boss, awakeningPainLordV2,
				sizeof( awakeningPainLordV2 ) / sizeof( awakeningPainLordV2[0] ) ) ||
			!Awakening_CorrectSource( level, awakeningBioV2,
				sizeof( awakeningBioV2 ) / sizeof( awakeningBioV2[0] ) ) ) ) {
			return false;
		}
		source = boss + "\n" + level;
		source.Replace( "namespace painlord", "namespace oq4_m05_painlord" );
		source.Replace( "namespace map_m05", "namespace oq4_m05" );
		source.Replace( "painlord::", "oq4_m05_painlord::" );
		source.Replace( "map_m05::", "oq4_m05::" );
		return true;
	}
	if ( file.Icmp( "maps/game/m06_mcc.openq4-v1.script" ) == 0 ) {
		if ( !Awakening_ReadMissionSource( "scripts/maps/m06_mcc.script", source ) ||
			 !Awakening_CorrectSource( source, awakeningMccV1, sizeof( awakeningMccV1 ) / sizeof( awakeningMccV1[0] ) ) ) {
			return false;
		}
		source.Replace( "namespace m06", "namespace oq4_m06" );
		source.Replace( "m06::", "oq4_m06::" );
		return true;
	}
	if ( file.Icmp( "maps/game/m06_mcc_invasion.openq4-v1.script" ) == 0 ) {
		if ( !Awakening_ReadMissionSource( "scripts/maps/m06_mcc_invasion.script", source ) ||
			 !Awakening_CorrectSource( source, awakeningInvasionV1, sizeof( awakeningInvasionV1 ) / sizeof( awakeningInvasionV1[0] ) ) ) {
			return false;
		}
		if ( version2 && ( !Awakening_CorrectSource( source, awakeningInvasionV2,
				sizeof( awakeningInvasionV2 ) / sizeof( awakeningInvasionV2[0] ) ) ||
			!Awakening_CorrectTeleporterV2( source, "hallTeleporterDestroyed", "monitorHallTeleHealth",
				"spinHallTeleBall", "rear_tele_ball_1", "rear_tele_broken_1", "func_spawner_enemy_light_4" ) ||
			!Awakening_CorrectTeleporterV2( source, "commTeleporterDestroyed", "monitorCommTeleHealth",
				"spinCommTeleBall", "comm_tele_ball", "comm_tele_broken", "func_spawner_command_dropin" ) ||
			!Awakening_CorrectTeleporterV2( source, "teleporterDestroyed", "monitorTeleHealth",
				"spinTeleBall", "rear_tele_ball", "rear_tele_broken", "func_spawner_enemy_light_3" ) ) ) {
			return false;
		}
		if ( version3 && !Awakening_CorrectSource( source, awakeningInvasionV3,
			sizeof( awakeningInvasionV3 ) / sizeof( awakeningInvasionV3[0] ) ) ) {
			return false;
		}
		source.Replace( "namespace m06b", "namespace oq4_m06b" );
		source.Replace( "\"m06b::", "\"oq4_m06b::" ); // thread names, not model flag
		return true;
	}
	return false;
}

void Awakening_PrepareM05M06( idDict &worldArgs ) {
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	const char *call = worldArgs.GetString( "call" );
	if ( !g_awakeningM05M06.GetBool() || gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) != 0 ||
		!( ( map.Icmp( "maps/game/m05_bio" ) == 0 && idStr::Cmp( call, "map_m05::main" ) == 0 ) ||
		   ( map.Icmp( "maps/game/m06_mcc" ) == 0 && idStr::Cmp( call, "m06::main" ) == 0 ) ||
		   ( map.Icmp( "maps/game/m06_mcc_invasion" ) == 0 && idStr::Cmp( call, "m06b::main" ) == 0 ) ) ) {
		return;
	}
	// A mapper's sidecar owns its mission.
	if ( fileSystem->ReadFile( map + ".script", NULL, NULL ) > 0 ) {
		return;
	}
	const int versionNumber = map.Icmp( "maps/game/m06_mcc_invasion" ) == 0 ? 3 : 2;
	idStr version = map + va( ".openq4-v%d.script", versionNumber );
	idStr source;
	if ( !Awakening_LoadM05M06Script( version, source ) ) {
		gameLocal.Warning( "Awakening mission corrections unavailable for %s; keeping installed scripts", map.c_str() );
		return;
	}
	gameLocal.program.CompileFile( version );
	Awakening_CorrectM05M06Entity( worldArgs );
	gameLocal.Printf( "Awakening mission corrections v%d: %s\n", versionNumber, map.c_str() );
}

void Awakening_CorrectM05M06Entity( idDict &args ) {
	if ( gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) != 0 ) {
		return;
	}
	// The service lift was renamed in the map; its lamp retained the deleted
	// name and stayed downstairs. Preserve mapper overrides of either key.
	if ( gameLocal.program.FindFunction( "oq4_m05::main" ) != NULL &&
		idStr::Cmp( args.GetString( "name" ), "light_8861" ) == 0 &&
		idStr::Cmp( args.GetString( "classname" ), "light" ) == 0 &&
		idStr::Cmp( args.GetString( "bind" ), "func_elevator_31" ) == 0 ) {
		args.Set( "bind", "elevator2" );
	}
	// This one-way freight ride is controlled by the squad script, not a
	// func_elevator. Match its other locked panels and discard the stale target.
	if ( gameLocal.program.FindFunction( "oq4_m05::main" ) != NULL &&
		idStr::Cmp( args.GetString( "name" ), "elevACall_2" ) == 0 &&
		idStr::Cmp( args.GetString( "classname" ), "func_static" ) == 0 &&
		idStr::Cmp( args.GetString( "gui" ), "guis/movers/strogg/call_lift.gui" ) == 0 &&
		idStr::Cmp( args.GetString( "target" ), "func_elevator_1" ) == 0 ) {
		args.Delete( "target" );
		args.SetBool( "gui_noninteractive", true );
	}
	if ( gameLocal.program.FindFunction( "oq4_m05::main" ) != NULL &&
		idStr::Cmp( args.GetString( "classname" ), "light" ) == 0 &&
		idStr::Cmp( args.GetString( "texture" ), "lights/p_light1b" ) == 0 &&
		declManager->FindMaterial( "lights/p_light1b", false ) == NULL &&
		declManager->FindMaterial( "lights/p_light1xb", false ) != NULL ) {
		args.Set( "texture", "lights/p_light1xb" );
	}
	if ( gameLocal.program.FindFunction( "oq4_m06b::main" ) != NULL &&
		idStr::Cmp( args.GetString( "classname" ), "func_static" ) == 0 &&
		idStr::Cmp( args.GetString( "model" ), "models/mapobjects/marine/equipment/monitors/mcc_monitor02_sm.lwo" ) == 0 &&
		idStr::Cmp( args.GetString( "gui" ), "guis/monitors/marine/mcc1.gui" ) == 0 &&
		fileSystem->ReadFile( "guis/monitors/marine/mcc1.gui", NULL, NULL ) < 0 &&
		fileSystem->ReadFile( "guis/monitors/marine/mcc/q4x_ambient_mcc1.gui", NULL, NULL ) > 0 ) {
		args.Set( "gui", "guis/monitors/marine/mcc/q4x_ambient_mcc1.gui" );
	}
	const char *prefixes[] = { "map_m05::", "painlord::", "m06::", "m06b::" };
	const char *corrected[] = { "oq4_m05::", "oq4_m05_painlord::", "oq4_m06::", "oq4_m06b::" };
	for ( int p = 0; p < 4; ++p ) {
		if ( gameLocal.program.FindFunction( va( "%smain", corrected[p] ) ) == NULL ) {
			continue;
		}
		for ( int i = 0; i < args.GetNumKeyVals(); ++i ) {
			const idKeyValue *kv = args.GetKeyVal( i );
			if ( idStr::Cmpn( kv->GetValue(), prefixes[p], (int)strlen( prefixes[p] ) ) == 0 ) {
				idStr value = corrected[p];
				value += kv->GetValue().c_str() + strlen( prefixes[p] );
				args.Set( kv->GetKey(), value );
			}
		}
	}
}
