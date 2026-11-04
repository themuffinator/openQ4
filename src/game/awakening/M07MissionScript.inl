/*
 * M07 race corrections, authored for openQ4 under the Quake 4 SDK EULA.
 * Transform installed scripts; do not redistribute recovered content.
 * Keep each version stable: saved threads recompile its source on restore.
 */
static const char * const awakeningM07Sources[] = {
	"maps/game/m07_race.script", "maps/game/m07_race1.script", "maps/game/m07_race2.script"
};
static const char * const awakeningM07Versions[] = {
	"maps/game/m07_race.openq4-v1.script", "maps/game/m07_race1.openq4-v1.script", "maps/game/m07_race2.openq4-v1.script"
};
static const char * const awakeningM07Versions2[] = {
	"maps/game/m07_race.openq4-v2.script", "maps/game/m07_race1.openq4-v2.script", "maps/game/m07_race2.openq4-v2.script"
};
static const char * const awakeningM07Versions3[] = {
	"maps/game/m07_race.openq4-v3.script", "maps/game/m07_race1.openq4-v3.script", "maps/game/m07_race2.openq4-v3.script"
};

static bool Awakening_M07ReplaceOnce( idStr &source, const char *before, const char *after ) {
	const int first = source.Find( before );
	if ( first < 0 || source.Find( before, true, first + 1 ) >= 0 ) {
		return false;
	}
	source.Replace( before, after );
	return true;
}

static bool Awakening_CorrectM07Script( idStr &source, int race ) {
	source.Replace( "\r\n", "\n" );
	struct correction_t { const char *before; const char *after; };
	static const correction_t corrections[] = {
		{ "float area1TimeLeft;", "float area1TimeLeft;\n"
		  "boolean oq4_raceArea1Started;\nboolean oq4_raceArea2Started;\nboolean oq4_raceComplete;" },
		// Late or repeated callbacks must not reset an already moving gate.
		{ "void startTimerArea1()\n\t{", "void startTimerArea1()\n\t{\n"
		  "\t\tif ( oq4_raceArea1Started || oq4_raceArea2Started || oq4_raceComplete ) { return; }\n"
		  "\t\toq4_raceArea1Started = true;" },
		{ "void startTimerArea2()\n\t{", "void startTimerArea2()\n\t{\n"
		  "\t\tif ( oq4_raceArea2Started || oq4_raceComplete ) { return; }\n"
		  "\t\toq4_raceArea2Started = true;" },
		// Establish carried time before launching the gate thread. Issue the
		// objective now, so fast completion cannot precede its delayed creation.
		{ "AREA2_TIME_EXPIRED = AREA2_TIME_STARTED + AREA2_TOTAL_TIME + area1TimeLeft;",
		  "AREA2_TIME_EXPIRED = AREA2_TIME_STARTED + AREA2_TOTAL_TIME + area1TimeLeft;\n"
		  "\t\tthread lockdownGate2Down();\n\t\tsys.trigger( $objective_area2 );" },
		{ "\n\t\tthread lockdownGate2Down();\n\t\tsys.wait( 5 );\n\t\tsys.trigger( $objective_area2 );",
		  "\n\t\tsys.wait( 5 );" },
		// Completion must not depend on the adjacent position trigger's order.
		{ "void area1Cleared()\n\t{", "void area1Cleared()\n\t{\n"
		  "\t\tif ( oq4_raceArea2Started || oq4_raceComplete ) { return; }\n\t\tspeederInArea2();" },
		{ "void area2Cleared()\n\t{", "void area2Cleared()\n\t{\n"
		  "\t\tif ( oq4_raceComplete ) { return; }\n\t\toq4_raceComplete = true;\n"
		  "\t\tspeederInArea3();\n"
		  "\t\t$vehicle_speeder_1.guiEvent( \"hidespeedertimer\" );\n"
		  "\t\t$vehicle_speeder_1.guiEvent( \"hidespeedertimer2\" );\n"
		  "\t\t$vehicle_speeder_1.guiEvent( \"showprogress100\" );" },
		// waitFrame can overshoot. Keep zero throughout the final gate closure.
		{ "float timeLeft = AREA1_TIME_EXPIRED - timeNow;",
		  "float timeLeft = AREA1_TIME_EXPIRED - timeNow;\n\t\t\t\tif ( timeLeft < 0 ) { timeLeft = 0; }" },
		{ "float timeLeft = AREA2_TIME_EXPIRED - timeNow;",
		  "float timeLeft = AREA2_TIME_EXPIRED - timeNow;\n\t\t\t\tif ( timeLeft < 0 ) { timeLeft = 0; }" },
		{ "while( timeNow <= AREA1_TIME_EXPIRED )",
		  "while( timeNow <= AREA1_TIME_EXPIRED && !oq4_raceArea2Started && !oq4_raceComplete )" },
		{ "while( timeNow <= AREA2_TIME_EXPIRED )",
		  "while( timeNow <= AREA2_TIME_EXPIRED && !oq4_raceComplete )" },
	};
	for ( size_t i = 0; i < sizeof( corrections ) / sizeof( corrections[0] ); ++i ) {
		if ( !Awakening_M07ReplaceOnce( source, corrections[i].before, corrections[i].after ) ) {
			return false;
		}
	}
	if ( race != 0 ) {
		// setFOV belongs to cameras, not the player. The script CVar bridge
		// applies a neutral view offset while preserving the user's chosen FOV.
		if ( !Awakening_M07ReplaceOnce( source, "$player1.setFOV( 90 );", "sys.setcvar( \"g_fov\", \"90\" );" ) ) {
			return false;
		}
	}
	// Copied bomber runs all cleaned up flyer one, leaving their own assemblies
	// behind and potentially removing a different attack that was still running.
	const int bomberCount = race == 0 ? 3 : race == 1 ? 2 : 4;
	for ( int bomber = 1; bomber <= bomberCount; ++bomber ) {
		if ( bomber > 1 ) {
			idStr before = va( "sys.waitFor( $strafeBase_%d );\n\t\tremoveFlyerBits(\"1\");", bomber );
			idStr after = va( "sys.waitFor( $strafeBase_%d );\n\t\tremoveFlyerBits(\"%d\");", bomber, bomber );
			if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
		}
		// A shot-down flyer exits the attack early. Its detached mover must
		// still be removed, as must any surviving launchers/effects.
		idStr before = va( "if (!isValidEntity($strafingFlyer_%d)) // make sure it hasn't been destroyed before manipulating it\n\t\treturn;", bomber );
		idStr after = va( "if (!isValidEntity($strafingFlyer_%d)) { removeFlyerBits(\"%d\"); return; }", bomber, bomber );
		int count = 0;
		for ( int at = source.Find( before ); at >= 0; at = source.Find( before, true, at + 1 ) ) { ++count; }
		if ( count != ( race == 0 && bomber > 1 ? 7 : 5 ) ) {
			return false;
		}
		source.Replace( before, after );
	}
	if ( race == 1 ) {
		// The extra placed flyers have damage relays and explosion FX, but no
		// relay callbacks. Use the map's own shared destruction sequence.
		if ( source.Find( "void bomberExplode_3(" ) >= 0 || source.Find( "void bomberExplode_4(" ) >= 0 ) {
			return false;
		}
		if ( !Awakening_M07ReplaceOnce( source, "void bomberExplode_1()",
			"void bomberExplode_3() {\n"
			"\tif ( isValidEntity( $strafingFlyer_3 ) ) {\n"
			"\t\tstroggFlyerExplosion( $strafingFlyer_3, $flyerStrafeExplode_3 );\n\t\tremoveFlyerBits(\"3\");\n\t}\n}\n"
			"void bomberExplode_4() {\n"
			"\tif ( isValidEntity( $strafingFlyer_4 ) ) {\n"
			"\t\tstroggFlyerExplosion( $strafingFlyer_4, $flyerStrafeExplode_4 );\n\t\tremoveFlyerBits(\"4\");\n\t}\n}\n"
			"void bomberExplode_1()" ) ) {
			return false;
		}
	}
	const char *area = race == 0 ? "RACE_AREA" : race == 1 ? "RACE1_AREA" : "RACE2_AREA";
	for ( int gate = 1; gate <= 2; ++gate ) {
		idStr before = va( "if( %s <= %d )", area, gate );
		idStr after = va( "if( %s <= %d && !oq4_raceComplete )", area, gate );
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) {
			return false;
		}
	}
	return true;
}

// Apply only after v1. Separate identity preserves existing saved statement
// offsets, while fresh starts treat both success and failure as terminal.
static bool Awakening_CorrectM07ScriptV2( idStr &source, int race ) {
	if ( !Awakening_M07ReplaceOnce( source, "boolean oq4_raceComplete;",
		"boolean oq4_raceComplete;\nboolean oq4_raceFailed;" ) ) { return false; }
	const char *callbacks[] = { "startTimerArea1", "startTimerArea2", "area1Cleared", "area2Cleared" };
	for ( size_t i = 0; i < sizeof( callbacks ) / sizeof( callbacks[0] ); ++i ) {
		idStr before = va( "void %s()\n\t{", callbacks[i] );
		idStr after = before;
		after += "\n\t\tif ( oq4_raceFailed ) { return; }";
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
	}
	// These are repeatable position triggers. Retain backtracking feedback while
	// racing, but never overwrite the settled result after a finish or failure.
	for ( int progress = 0; progress <= 100; progress += 10 ) {
		idStr before = va( "void raceProgress%d()\n\t{", progress );
		idStr after = before;
		after += "\n\t\tif ( oq4_raceComplete || oq4_raceFailed ) { return; }";
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
	}
	const char *area = race == 0 ? "RACE_AREA" : race == 1 ? "RACE1_AREA" : "RACE2_AREA";
	for ( int gate = 1; gate <= 2; ++gate ) {
		idStr before = va( "if( %s <= %d && !oq4_raceComplete )\n\t\t\t{", area, gate );
		idStr after = va( "if( %s <= %d && !oq4_raceComplete && !oq4_raceFailed )\n\t\t\t{\n"
			"\t\t\t\toq4_raceFailed = true;\n"
			"\t\t\t\t$vehicle_speeder_1.disableMovement();\n"
			"\t\t\t\t$vehicle_speeder_1.disableWeapon();", area, gate );
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
		before = gate == 1 ? "while( timeNow <= AREA1_TIME_EXPIRED && !oq4_raceArea2Started && !oq4_raceComplete )"
			: "while( timeNow <= AREA2_TIME_EXPIRED && !oq4_raceComplete )";
		after = before;
		after.Replace( " )", " && !oq4_raceFailed )" );
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
	}
	if ( race == 2 && !Awakening_M07ReplaceOnce( source, "void raceExitCinematic()\n\t{",
		"void raceExitCinematic()\n\t{\n\t\tif ( oq4_raceFailed ) { return; }" ) ) { return false; }
	return true;
}

static bool Awakening_M07ReplaceCount( idStr &source, const char *before, const char *after, int expected ) {
	int count = 0;
	for ( int at = source.Find( before ); at >= 0; at = source.Find( before, true, at + 1 ) ) { ++count; }
	if ( count != expected ) { return false; }
	source.Replace( before, after );
	return true;
}

// Destruction can interrupt a volley or a mover wait. Claim each flyer before
// yielding, let its existing explosion/ejection finish once, then remove its
// assembly. Preserve the v1/v2 programs for saves that already own those threads.
static bool Awakening_CorrectM07ScriptV3( idStr &source, int race ) {
	const int count = race == 0 ? 3 : 4;
	idStr globals = "boolean oq4_raceFailed;";
	for ( int bomber = 1; bomber <= count; ++bomber ) {
		globals += va( "\nboolean oq4_bomberDestroyed_%d;", bomber );
	}
	if ( !Awakening_M07ReplaceOnce( source, "boolean oq4_raceFailed;", globals ) ) { return false; }
	for ( int bomber = 1; bomber <= count; ++bomber ) {
		idStr before;
		if ( race == 1 && bomber > 2 ) {
			before = va( "void bomberExplode_%d() {\n\tif ( isValidEntity( $strafingFlyer_%d ) ) {\n"
				"\t\tstroggFlyerExplosion( $strafingFlyer_%d, $flyerStrafeExplode_%d );\n"
				"\t\tremoveFlyerBits(\"%d\");\n\t}\n}", bomber, bomber, bomber, bomber, bomber );
		} else {
			before = va( "void bomberExplode_%d()\n\t{\n\t\tstroggFlyerExplosion( $strafingFlyer_%d , $flyerStrafeExplode_%d );\n\t}", bomber, bomber, bomber );
		}
		idStr after = va( "void bomberExplode_%d() {\n"
			"\tif ( oq4_bomberDestroyed_%d ) { return; }\n\toq4_bomberDestroyed_%d = true;\n"
			"\tif ( isValidEntity( $strafingFlyer_%d ) && isValidEntity( $flyerStrafeExplode_%d ) ) {\n"
			"\t\tstroggFlyerExplosion( $strafingFlyer_%d, $flyerStrafeExplode_%d );\n\t}\n"
			"\tremoveFlyerBits(\"%d\");\n}", bomber, bomber, bomber, bomber, bomber, bomber, bomber, bomber );
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
		if ( race == 1 && bomber > 2 ) { continue; }
		before = va( "setNewTimes($strafeBase_%d,", bomber );
		after = va( "if ( oq4_bomberDestroyed_%d ) { return; }\n"
			"\t\tif ( !isValidEntity( $strafingFlyer_%d ) || !isValidEntity( $strafeBase_%d ) ) { removeFlyerBits(\"%d\"); return; }\n"
			"\t\tsetNewTimes($strafeBase_%d,", bomber, bomber, bomber, bomber, bomber );
		if ( !Awakening_M07ReplaceOnce( source, before, after ) ) { return false; }
		before = va( "if (!isValidEntity($strafingFlyer_%d)) { removeFlyerBits(\"%d\"); return; }", bomber, bomber );
		after = va( "if ( oq4_bomberDestroyed_%d ) { return; }\n\t\t", bomber );
		after += before;
		if ( !Awakening_M07ReplaceCount( source, before, after, race == 0 && bomber > 1 ? 7 : 5 ) ) { return false; }
	}
	struct correction_t { const char *before; const char *after; };
	static const correction_t corrections[] = {
		// The fighter is valid at the loop head. Do not fire from a hidden wreck
		// or through launchers removed by its bound-entity teardown.
		{ "\n\t\t\tvector guessLead = $player1.getLinearVelocity();",
		  "\n\t\t\tif ( fighter.isHidden() || !isValidEntity( projectileOne ) || !isValidEntity( projectileTwo ) ) { break; }\n"
		  "\t\t\tvector guessLead = $player1.getLinearVelocity();" },
		{ "sys.trigger( projectileTwo );",
		  "if ( isValidEntity( fighter ) && isValidEntity( projectileTwo ) ) {\n"
		  "\t\t\t\tif ( !fighter.isHidden() ) { sys.trigger( projectileTwo ); }\n\t\t\t}" },
		{ "tempTarget.remove();\n\t\t\n\t\t\tprojectileOne.removeTarget( tempTarget );\n\t\t\tprojectileTwo.removeTarget( tempTarget );",
		  "if ( isValidEntity( tempTarget ) ) {\n"
		  "\t\t\t\tif ( isValidEntity( projectileOne ) ) { projectileOne.removeTarget( tempTarget ); }\n"
		  "\t\t\t\tif ( isValidEntity( projectileTwo ) ) { projectileTwo.removeTarget( tempTarget ); }\n"
		  "\t\t\t\ttempTarget.remove();\n\t\t\t}" },
		{ "tempTarget.remove();\t", "if ( isValidEntity( tempTarget ) ) { tempTarget.remove(); }\t" },
		// Hide before the first yield so the volley stops as soon as the hit is
		// accepted. Keep the authored velocity sample and pilot throw unchanged.
		{ "old = flyer.getWorldOrigin();",
		  "if ( !isValidEntity( flyer ) || !isValidEntity( explosion ) ) { return; }\n"
		  "\t\tflyer.hide();\n\t\told = flyer.getWorldOrigin();" },
		{ "new = flyer.getWorldOrigin();",
		  "if ( !isValidEntity( flyer ) || !isValidEntity( explosion ) ) { return; }\n\t\tnew = flyer.getWorldOrigin();" },
		{ "sys.setSpawnArg( \"classname\", \"monster_strogg_marine\" );",
		  "if ( !isValidEntity( flyer ) || !isValidEntity( explosion ) ) { return; }\n"
		  "\t\tsys.setSpawnArg( \"classname\", \"monster_strogg_marine\" );" },
		// stopMoving notifies waitFor before the mover disappears. A waiting
		// attack can finish its idempotent cleanup instead of remaining suspended.
		{ "flyerBit = sys.getEntity(\"strafeBase_\" + flyerNumber);\n\t\tif (flyerBit != $nullEntity)\n\t\t\tflyerBit.remove();",
		  "flyerBit = sys.getEntity(\"strafeBase_\" + flyerNumber);\n"
		  "\t\tif (flyerBit != $nullEntity) { flyerBit.stopMoving(); flyerBit.remove(); }" },
	};
	for ( size_t i = 0; i < sizeof( corrections ) / sizeof( corrections[0] ); ++i ) {
		if ( !Awakening_M07ReplaceOnce( source, corrections[i].before, corrections[i].after ) ) { return false; }
	}
	if ( race == 2 ) {
		// The fourth bomber first fires at the girders, using the same paired
		// launcher timing. Its separate volley needs the same destruction guards.
		if ( !Awakening_M07ReplaceOnce( source, "$strafeRocketA_4.appendTarget( $girder_target_2 );",
			"if ( fighter.isHidden() || !isValidEntity( $strafeRocketA_4 ) || !isValidEntity( $strafeRocketB_4 ) ) { break; }\n"
			"\t\t\t$strafeRocketA_4.appendTarget( $girder_target_2 );" ) ||
			!Awakening_M07ReplaceOnce( source, "sys.trigger( $strafeRocketB_4 );",
			"if ( isValidEntity( fighter ) && isValidEntity( $strafeRocketB_4 ) ) {\n"
			"\t\t\t\tif ( !fighter.isHidden() ) { sys.trigger( $strafeRocketB_4 ); }\n\t\t\t}" ) ||
			!Awakening_M07ReplaceOnce( source, "$strafeRocketA_4.removeTarget( $girder_target_2 );",
			"if ( isValidEntity( $strafeRocketA_4 ) ) { $strafeRocketA_4.removeTarget( $girder_target_2 ); }" ) ||
			!Awakening_M07ReplaceOnce( source, "$strafeRocketB_4.removeTarget( $girder_target_1 );",
			"if ( isValidEntity( $strafeRocketB_4 ) ) { $strafeRocketB_4.removeTarget( $girder_target_1 ); }" ) ) { return false; }
	}
	return true;
}

static bool Awakening_LoadM07MissionScript( const char *filename, idStr &source ) {
	idStr normalized = filename;
	normalized.BackSlashesToSlashes();
	if ( gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) != 0 ) {
		return false;
	}
	for ( int race = 0; race < 3; ++race ) {
		const bool version3 = normalized.Icmp( awakeningM07Versions3[race] ) == 0;
		const bool version2 = normalized.Icmp( awakeningM07Versions2[race] ) == 0;
		if ( !version3 && !version2 && normalized.Icmp( awakeningM07Versions[race] ) != 0 ) { continue; }
		char *installed = NULL;
		const int length = fileSystem->ReadFile( awakeningM07Sources[race], reinterpret_cast<void **>( &installed ), NULL );
		if ( length > 0 ) { source = installed; }
		if ( installed != NULL ) { fileSystem->FreeFile( installed ); }
		if ( length <= 0 || !Awakening_CorrectM07Script( source, race ) ||
			( ( version3 || version2 ) && !Awakening_CorrectM07ScriptV2( source, race ) ) ||
			( version3 && !Awakening_CorrectM07ScriptV3( source, race ) ) ) {
			source.Clear();
			return false;
		}
		return true;
	}
	return false;
}

static const char *Awakening_M07MissionScriptName( const char *filename ) {
	idStr normalized = filename;
	normalized.BackSlashesToSlashes();
	if ( !gameLocal.isMultiplayer && idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) == 0 ) {
		for ( int race = 0; race < 3; ++race ) {
			if ( normalized.Icmp( awakeningM07Sources[race] ) != 0 ) { continue; }
			idStr source;
			if ( Awakening_LoadM07MissionScript( awakeningM07Versions3[race], source ) ) {
				gameLocal.Printf( "Awakening M07: %s mission corrections v3 enabled\n", filename );
				return awakeningM07Versions3[race];
			}
			gameLocal.Warning( "Awakening M07: unrecognized script; keeping installed mission behaviour" );
		}
	}
	return filename;
}
