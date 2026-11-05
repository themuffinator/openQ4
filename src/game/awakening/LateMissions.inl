/*
 * Independently authored corrections to user-supplied Awakening M08/M09.
 * SDK EULA; see LICENSES/QUAKE-4-SDK-EULA.rtf and docs/dev/awakening-missions.md.
 * These virtual v1 sources are immutable savegame inputs. Never rewrite the
 * original program: old saves must retain their original statement layout.
 */

static const char * const awakeningM08Source = "maps/game/m08_cryofac.script";
static const char * const awakeningM08Version1 = "maps/game/m08_cryofac.openq4-v1.script";
static const char * const awakeningM08Version2 = "maps/game/m08_cryofac.openq4-v2.script";
static const char * const awakeningM08Version3 = "maps/game/m08_cryofac.openq4-v3.script";
static const char * const awakeningM09Source = "scripts/maps/m09_valkaryne.script";
static const char * const awakeningM09Version1 = "maps/game/m09_valkaryne.openq4-v1.script";
static const char * const awakeningM09Version2 = "maps/game/m09_valkaryne.openq4-v2.script";
static const char * const awakeningM09Version3 = "maps/game/m09_valkaryne.openq4-v3.script";
static const char * const awakeningM09Version4 = "maps/game/m09_valkaryne.openq4-v4.script";
static idCVar g_awakeningLateMissions( "g_awakeningLateMissions", "1", CVAR_GAME | CVAR_BOOL,
	"Use openQ4 M08/M09 mission corrections on fresh starts; saves retain their selected revision" );

static bool Awakening_LateCampaignActive() {
	return !gameLocal.isMultiplayer && !idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" );
}

struct awakeningCorrection_t {
	const char *before;
	const char *after;
	int count;
};

static bool Awakening_ApplyCorrections( idStr &source, const awakeningCorrection_t *edits, int count ) {
	for ( int i = 0; i < count; ++i ) {
		int matches = 0;
		for ( int pos = source.Find( edits[i].before ); pos >= 0;
			pos = source.Find( edits[i].before, true, pos + idStr::Length( edits[i].before ) ) ) {
			++matches;
		}
		if ( matches != edits[i].count ) {
			source.Clear();
			return false;
		}
		source.Replace( edits[i].before, edits[i].after );
	}
	return true;
}

// The recovered cold-breath placements are actor-relative offsets, not joint
// offsets: "legs_channel" names an animation channel, never a model joint.
// Read the user's placements rather than bundling the recovered script/table.
static bool Awakening_CorrectColdBreath( idStr &source ) {
	const char *header = "void coldInHere()\n{";
	const int start = source.Find( header );
	if ( start < 0 || source.Find( header, true, start + 1 ) >= 0 ) { return false; }
	const int end = source.Find( "}", true, start );
	if ( end < 0 ) { return false; }
	idStr body = source.Mid( start + idStr::Length( header ), end - start - idStr::Length( header ) );
	idLexer lexer( body.c_str(), body.Length(), "Awakening cold breath", LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_ALLOWMULTICHARLITERALS );
	idStr replacement = "void openq4_attachBreath( entity effect, entity actor, vector offset ) {\n"
		"\tif ( !effect ) { return; }\n"
		"\tif ( !actor ) { effect.remove(); return; }\n"
		"\teffect.bind( actor );\n\teffect.setOrigin( offset );\n}\n\nvoid coldInHere()\n{\n";
	idToken token, effect, actor, repeated, offset;
	int count = 0;
	while ( lexer.ReadToken( &token ) ) {
		if ( token != "$" || !lexer.ReadToken( &effect ) || effect.type != TT_NAME || effect.Icmpn( "cold_breath_", 12 ) ||
			!lexer.ExpectTokenString( "." ) || !lexer.ExpectTokenString( "bindToJoint" ) || !lexer.ExpectTokenString( "(" ) ||
			!lexer.ExpectTokenString( "$" ) || !lexer.ReadToken( &actor ) || actor.type != TT_NAME ||
			!lexer.ExpectTokenString( "," ) || !lexer.ExpectTokenString( "legs_channel" ) || !lexer.ExpectTokenString( "," ) ||
			!lexer.ExpectTokenString( "1" ) || !lexer.ExpectTokenString( ")" ) || !lexer.ExpectTokenString( ";" ) ||
			!lexer.ExpectTokenString( "$" ) || !lexer.ReadToken( &repeated ) || repeated != effect ||
			!lexer.ExpectTokenString( "." ) || !lexer.ExpectTokenString( "setOrigin" ) || !lexer.ExpectTokenString( "(" ) ||
			!lexer.ReadToken( &offset ) || offset.type != TT_LITERAL ||
			!lexer.ExpectTokenString( ")" ) || !lexer.ExpectTokenString( ";" ) ) { return false; }
		// Only a vector literal is admitted into the generated call.
		idLexer vectorLexer( offset.c_str(), offset.Length(), "breath offset", LEXFL_NOERRORS | LEXFL_NOWARNINGS );
		for ( int component = 0; component < 3; ++component ) {
			if ( !vectorLexer.ReadToken( &token ) ) { return false; }
			if ( token == "-" && !vectorLexer.ReadToken( &token ) ) { return false; }
			if ( token.type != TT_NUMBER ) { return false; }
		}
		if ( vectorLexer.ReadToken( &token ) || vectorLexer.HadError() ) { return false; }
		replacement += va( "\topenq4_attachBreath( sys.getEntity( \"%s\" ), sys.getEntity( \"%s\" ), '%s' );\n", effect.c_str(), actor.c_str(), offset.c_str() );
		++count;
	}
	if ( count != 36 || lexer.HadError() ) { return false; }
	replacement += "}";
	source.Replace( source.Mid( start, end - start + 1 ), replacement );
	return true;
}

static bool Awakening_LoadLateMissionScript( const char *filename, idStr &source ) {
	idStr path = filename;
	path.BackSlashesToSlashes();
	const bool cryoVersion3 = path.Icmp( awakeningM08Version3 ) == 0;
	const bool cryo = cryoVersion3 || path.Icmp( awakeningM08Version1 ) == 0 || path.Icmp( awakeningM08Version2 ) == 0;
	const bool version4 = path.Icmp( awakeningM09Version4 ) == 0;
	const bool version3 = version4 || path.Icmp( awakeningM09Version3 ) == 0;
	const bool version2 = cryoVersion3 || version3 || path.Icmp( awakeningM09Version2 ) == 0 || path.Icmp( awakeningM08Version2 ) == 0;
	if ( !Awakening_LateCampaignActive() || ( !cryo && !version2 && path.Icmp( awakeningM09Version1 ) != 0 ) ) {
		return false;
	}
	char *installed = NULL;
	if ( fileSystem->ReadFile( cryo ? awakeningM08Source : awakeningM09Source,
		reinterpret_cast<void **>( &installed ), NULL ) <= 0 ) {
		if ( installed ) { fileSystem->FreeFile( installed ); }
		return false;
	}
	source = installed;
	fileSystem->FreeFile( installed );
	source.Replace( "\r\n", "\n" );
	static const awakeningCorrection_t cryoEdits[] = {
		{ "$navahas,", "$navajas,", 2 },
		{ "frame_unlocked.setShaderParm( 7, 0 );", "frame_unlocked.setShaderParm( 7, 1 );", 1 },
		// The cooling button is reusable; concurrent calls used to toggle the
		// damage volumes back on and lower the fog more than once.
		{ "void TurnOffFreeze()\n{", "void TurnOffFreeze()\n{\n"
		  "\tif ( openq4_cryoShutdown ) { return; }\n\topenq4_cryoShutdown = 1;\n"
		  "\twhile ( !openq4_cryoReady ) { sys.waitFrame(); }", 1 },
		// An early button press must not race the window breach and re-enable
		// its cold damage after the shutdown thread has already toggled it off.
		{ "aiSpeak( $navajas, \"lipsync_nava_weneedloccryo\" );",
		  "openq4_cryoReady = 1;\n\taiSpeak( $navajas, \"lipsync_nava_weneedloccryo\" );", 1 },
		// The 201-frame attack was removed after just two seconds, taking its
		// bound victim with it. Let the authored animation finish first.
		// Keep this short-lived actor awake so its completion state advances
		// even if the player backs out of the scene's visibility area.
		{ "aiScriptedAnim( $valkaryne_1, \"cr_kill_tak\", 0, 0 );",
		  "$valkaryne_1.setNeverDormant( 1 );\n\taiScriptedAnim( $valkaryne_1, \"cr_kill_tak\", 0, 0 );", 1 },
		{ "$valkaryne_1.remove();", "while ( !$valkaryne_1.scriptedDone() ) { sys.waitFrame(); }\n\t$valkaryne_1.remove();", 1 },
	};
	static const awakeningCorrection_t finaleEdits[] = {
		// Default main.script already contains the original namespace. Compile
		// a separate map program and redirect only this fresh map's callbacks.
		{ "namespace map_m09", "namespace oq4_m09_v1", 1 },
		{ "for(t=0;t++;t<5)", "for(t=0;t<5;t++)", 2 },
		{ "aiScriptedMove( $navajas02, $navajas02_spot01, 8, 1 );",
		  "aiScriptedMove( $navajas03, $navajas02_spot01, 8, 1 );", 1 },
		{ "for(r=0;r<=30;r++)", "for(r=1;r<=63;r++)", 1 },
		{ "for(r=31;r>=1;r--)", "for(r=63;r>=1;r--)", 1 },
		{ "for(r=0;r<=14;r++)", "for(r=1;r<=14;r++)", 1 },
		{ "for(r=15;r>=1;r--)", "for(r=14;r>=1;r--)", 1 },
		{ "for(r=0;r<=10;r++)", "for(r=1;r<=10;r++)", 1 },
		{ "for(r=11;r>=1;r--)", "for(r=10;r>=1;r--)", 1 },
		// The shield itself owns this rotation; its old parent was deleted.
		{ "$mvr_makShields.rotate( '0 0 0' );", "$makShield01.rotate( '0 0 0' );", 1 },
		// This obsolete forcefield is absent from the recovered map. Preserve
		// its optional flicker if supplied, without calling a null entity.
		{ "$valk_forcefield.hide();", "if ( $valk_forcefield ) { $valk_forcefield.hide(); }", 2 },
		{ "$valk_forcefield.show();", "if ( $valk_forcefield ) { $valk_forcefield.show(); }", 2 },
		{ "$valk_forcefield.remove();", "if ( $valk_forcefield ) { $valk_forcefield.remove(); }", 1 },
		// The initial escort pickup removes this item. Later objectives must
		// not attempt to collect the consumed escort objective again.
		// Resolve it again: a script's cached entity number stays nonzero after
		// pickup, even though it no longer identifies a live entity.
		{ "sys.trigger( $obj_project_navajas );",
		  "p = sys.getEntity( \"obj_project_navajas\" );\n\tif ( p ) { sys.trigger( p ); }", 1 },
		// Establish phase one during the intro, independently of AI sight or
		// a player destroying the sphere before CheckActions gets to run.
		{ "void valkIntro()\n{", "void valkIntro()\n{\n\tthread valkaryne_dock();", 1 },
		{ "void valkaryne_dock()\n{", "void valkaryne_dock()\n{\n"
		  "\t$valk.setKey( \"openq4_dockingRequested\", \"1\" );", 1 },
		{ "void initValk();", "void initValk();\n\tvoid valkaryne_dock();", 1 },
		// Only the finale uses this exact opening: stop its hex spawners on
		// victory, leaving the unrelated factory encounter loops alone.
		{ "while( 1 )\n\t{\n\t\tsys.trigger( $firstHex_2 );",
		  "while( VALK_ALIVE )\n\t{\n\t\tsys.trigger( $firstHex_2 );", 1 },
		{ "sys.trigger( $secondHex_2 );", "if ( !VALK_ALIVE ) { return; }\n\t\tsys.trigger( $secondHex_2 );", 1 },
		{ "sys.trigger( $thirdHex_2 );", "if ( !VALK_ALIVE ) { return; }\n\t\tsys.trigger( $thirdHex_2 );", 1 },
		// The radio promises thirty seconds; the doors take forty-five. Give
		// the player those thirty, then reserve the departure rather than
		// letting boarding and abort threads fight over the same mover.
		{ "sys.wait( 20 );\n\t\n\tif( !PLAYER_ON_DROPSHIP )",
		  "sys.wait( 30 );\n\t\n\tif( !PLAYER_ON_DROPSHIP )", 1 },
		{ "if( !PLAYER_ON_DROPSHIP )\n\t{", "if( !PLAYER_ON_DROPSHIP )\n\t{\n\t\tGAME_OVER = true;", 1 },
		{ "void youAreTehWinnar()\n{", "void youAreTehWinnar()\n{\n"
		  "\tif ( GAME_OVER || PLAYER_ON_DROPSHIP ) { return; }", 1 },
		// Reuse the map's unused failure item with the active escape title.
		// Capture it before collecting the objective removes its source entity.
		{ "void main()\n{", "void main()\n{\n"
		  "\t$obj_failed01.setKey( \"objectivetitle\", $item_objective_2.getKey( \"objectivetitle\" ) );", 1 },
		// No new display string, content asset or permanent black-screen state.
		{ "sys.fadeOut( '0 0 0', 5);", "sys.fadeOut( '0 0 0', 5);\n\t\tsys.wait( 5 );\n"
		  "\t\tsys.trigger( $obj_failed01 );", 1 },
	};
	if ( cryo ) {
		if ( !Awakening_ApplyCorrections( source, cryoEdits, sizeof( cryoEdits ) / sizeof( cryoEdits[0] ) ) ) {
			return false;
		}
		source.Insert( "float openq4_cryoShutdown = 0;\nfloat openq4_cryoReady = 0;\n", 0 );
	} else if ( !Awakening_ApplyCorrections( source, finaleEdits, sizeof( finaleEdits ) / sizeof( finaleEdits[0] ) ) ) {
		return false;
	}
	if ( version2 ) {
		if ( cryo ) {
			static const awakeningCorrection_t secondCryoEdits[] = {
				// The 32-unit arrival tolerance leaves both marines in the
				// entrance throat; their two hulls can close the player's gap.
				{ "aiScriptedMove( $tak, $tak_intropos_4, 32, 1 );",
				  "aiScriptedMove( $tak, $tak_intropos_4, 8, 1 );", 1 },
				{ "aiScriptedMove( $navajas, $navajas_intropos_4, 32, 1 );",
				  "aiScriptedMove( $navajas, $navajas_intropos_4, 8, 1 );", 1 },
				// CoreMonitor is after the lift descent. Position 3 is back
				// upstairs; position 28 pairs with Tak at this console.
				{ "aiScriptedMove( $navajas, $navajas_pos_3, 8, 0 );",
				  "aiScriptedMove( $navajas, $navajas_pos_28, 8, 0 );", 1 },
			};
			if ( !Awakening_ApplyCorrections( source, secondCryoEdits, sizeof( secondCryoEdits ) / sizeof( secondCryoEdits[0] ) ) ) { return false; }
		} else {
			static const awakeningCorrection_t secondFinaleEdits[] = {
				// These optional effects/sounds were removed from the recovered
				// map. Do not terminate the fog/glass threads or warn during
				// successful extraction, but preserve them if supplied.
				{ "sys.trigger( $steam05_freeze04 );", "if ( $steam05_freeze04 ) { sys.trigger( $steam05_freeze04 ); }", 1 },
				{ "sys.trigger( $broken_glass_steam01 );", "if ( $broken_glass_steam01 ) { sys.trigger( $broken_glass_steam01 ); }", 1 },
				{ "$ent_amb6.remove();", "if ( $ent_amb6 ) { $ent_amb6.remove(); }", 1 },
				{ "$ent_amb7.remove();", "if ( $ent_amb7 ) { $ent_amb7.remove(); }", 1 },
			};
			if ( !Awakening_ApplyCorrections( source, secondFinaleEdits, sizeof( secondFinaleEdits ) / sizeof( secondFinaleEdits[0] ) ) ) { return false; }
			if ( !Awakening_CorrectColdBreath( source ) ) { source.Clear(); return false; }
			source.Replace( "namespace oq4_m09_v1", "namespace oq4_m09_v2" );
		}
	}
	if ( cryoVersion3 ) {
		// The player can reach the freezer trigger while FirstFreeze is still
		// speaking. Do not teleport its other fireteam or steal the marines'
		// move commands until that scene has operated the intervening door.
		static const awakeningCorrection_t thirdCryoEdits[] = {
			{ "void StartFreezerMaidens()\n{", "void StartFreezerMaidens()\n{\n"
			  "\twhile ( !openq4_firstThawComplete ) { sys.wait( 0.1 ); }", 1 },
			{ "sys.wait( 1.0 );\n\taiScriptedMove( $navajas, $cryodoor_6_navajas_pos_1, 16, 0 );",
			  "sys.wait( 1.0 );\n\taiScriptedMove( $navajas, $cryodoor_6_navajas_pos_1, 16, 0 );\n"
			  "\topenq4_firstThawComplete = 1;", 1 },
		};
		if ( !Awakening_ApplyCorrections( source, thirdCryoEdits, sizeof( thirdCryoEdits ) / sizeof( thirdCryoEdits[0] ) ) ) { return false; }
		source.Insert( "float openq4_firstThawComplete = 0;\n", 0 );
	}
	if ( version3 ) {
		static const awakeningCorrection_t thirdFinaleEdits[] = {
			// The bomb is ready at this warning. The ensuing scripted death must
			// not leave "protect Navajas while he plants the bomb" active for
			// the rest of the mission. Capture the authored/localized title
			// before collecting the objective removes its source entity.
			{ "void animateNavajas()\n{",
			  "float openq4_escortComplete = 0;\nstring openq4_escortTitle;\n"
			  "void openq4_completeEscort() {\n"
			  "\tentity completion;\n"
			  "\tif ( openq4_escortComplete || openq4_escortTitle == \"\" ) { return; }\n"
			  "\tsys.setSpawnArg( \"objectivetitle\", openq4_escortTitle );\n"
			  "\tsys.setSpawnArg( \"inv_objective\", \"1\" );\n"
			  "\tcompletion = sys.spawn( \"item_objectivecomplete\" );\n"
			  "\tif ( !completion ) { return; }\n"
			  "\tcompletion.setKey( \"objEnabled\", \"1\" );\n"
			  "\tsys.trigger( completion );\n\topenq4_escortComplete = 1;\n"
			  "}\n\nvoid animateNavajas()\n{", 1 },
			{ "void main()\n{", "void main()\n{\n\topenq4_escortTitle = $obj_project_navajas.getKey( \"objectivetitle\" );", 1 },
			{ "thread navaSpeaks08();", "thread navaSpeaks08();\n\topenq4_completeEscort();", 1 },
			// The approach trigger starts the collapse before the player can
			// cross: deleting this 128-unit stair section leaves a 64-unit rise
			// beyond the 48-unit jump. The existing far-landing trigger is the
			// clearance signal, so collapse behind the player instead.
			{ "void stairsBreak()\n{", "void stairsBreak()\n{\n"
			  "\twhile ( sys.getEntity( \"trigger_once_25\" ) ) { sys.wait( 0.1 ); }", 1 },
		};
		if ( !Awakening_ApplyCorrections( source, thirdFinaleEdits, sizeof( thirdFinaleEdits ) / sizeof( thirdFinaleEdits[0] ) ) ) { return false; }
		source.Replace( "namespace oq4_m09_v2", "namespace oq4_m09_v3" );
	}
	if ( version4 ) {
		// Keep every published revision immutable for save recompilation.
		source.Replace( "namespace oq4_m09_v3", "namespace oq4_m09_v4" );
		static const awakeningCorrection_t fourthFinaleEdits[] = {
			{ "void exitDoorOneDisable()\n{", R"script(void openq4_sealDoor( entity panel, entity first, entity second ) {
	if ( !panel || !first || !second || VALK_ALIVE || panel.getIntKey( "openq4_sealing" ) ) { return; }
	panel.setKey( "openq4_sealing", "1" );
	panel.setGuiParm( "noninteractive", "1" );
	// start_open swaps POS1/POS2: open() moves these doors to the authored
	// closed origins. Confirm BOTH leaves arrived, including after a blockage.
	first.open();
	while ( sys.vecLength( first.getOrigin() - first.getVectorKey( "origin" ) ) > 1 ||
	        sys.vecLength( second.getOrigin() - second.getVectorKey( "origin" ) ) > 1 ) {
		sys.wait( 0.1 );
		first.open();
	}
	panel.setKey( "openq4_sealed", "1" );
}

void exitDoorOneDisable()
{)script", 1 },
			{ "$exitdooronepanel.setGuiParm( \"noninteractive\", \"1\" );\n\tsys.wait( 2 );\n\tEXIT_DOOR_ONE_CLOSED = true;",
			  "if ( !$exitdooronepanel ) { return; }\n\topenq4_sealDoor( $exitdooronepanel, $func_door_144, $func_door_143 );\n\tEXIT_DOOR_ONE_CLOSED = $exitdooronepanel.getIntKey( \"openq4_sealed\" );", 1 },
			{ "$exitdoortwopanel.setGuiParm( \"noninteractive\", \"1\" );\n\tsys.wait( 2 );\n\tEXIT_DOOR_TWO_CLOSED = true;",
			  "if ( !$exitdoortwopanel ) { return; }\n\topenq4_sealDoor( $exitdoortwopanel, $func_door_145, $func_door_146 );\n\tEXIT_DOOR_TWO_CLOSED = $exitdoortwopanel.getIntKey( \"openq4_sealed\" );", 1 },
			{ "$exitdoorthreepanel.setGuiParm( \"noninteractive\", \"1\" );\n\tsys.wait( 2 );\n\tEXIT_DOOR_THREE_CLOSED = true;",
			  "if ( !$exitdoorthreepanel ) { return; }\n\topenq4_sealDoor( $exitdoorthreepanel, $func_door_147, $func_door_148 );\n\tEXIT_DOOR_THREE_CLOSED = $exitdoorthreepanel.getIntKey( \"openq4_sealed\" );", 1 },
		};
		if ( !Awakening_ApplyCorrections( source, fourthFinaleEdits, sizeof( fourthFinaleEdits ) / sizeof( fourthFinaleEdits[0] ) ) ) { return false; }
	}
	return true;
}

static const char *Awakening_LateMissionScriptName( const char *filename ) {
	idStr path = filename;
	path.BackSlashesToSlashes();
	if ( g_awakeningLateMissions.GetBool() && Awakening_LateCampaignActive() && path.Icmp( awakeningM08Source ) == 0 ) {
		idStr source;
		if ( Awakening_LoadLateMissionScript( awakeningM08Version3, source ) ) {
			gameLocal.Printf( "Awakening M08: mission corrections v3 enabled\n" );
			return awakeningM08Version3;
		}
		gameLocal.Warning( "Awakening M08: unrecognized script; keeping installed mission behaviour" );
	}
	return filename;
}

void Awakening_PrepareLateMission( idDict &worldArgs ) {
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();

	if ( !g_awakeningLateMissions.GetBool() || !Awakening_LateCampaignActive() || map.Icmp( "maps/game/m09_valkaryne" ) != 0 ||
		idStr::Icmp( worldArgs.GetString( "call" ), "map_m09::main" ) != 0 ) {
		return;
	}
	// A custom sidecar owns the map; do not layer our revision over it.
	if ( fileSystem->ReadFile( "maps/game/m09_valkaryne.script", NULL, NULL ) > 0 ) { return; }
	idStr source;
	if ( !Awakening_LoadLateMissionScript( awakeningM09Version4, source ) ) {
		gameLocal.Warning( "Awakening M09: unrecognized script; keeping installed mission behaviour" );
		return;
	}
	gameLocal.program.CompileFile( awakeningM09Version4 );
	worldArgs.Set( "call", "oq4_m09_v4::main" );
	gameLocal.Printf( "Awakening M09: mission corrections v4 enabled\n" );
}

void Awakening_DirectLateMissionEntity( idDict &args ) {
	if ( !Awakening_LateCampaignActive() ) { return; }
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	const function_t *main = gameLocal.program.FindFunction( "main" );
	idStr cryoProgram = main ? gameLocal.program.GetFilename( main->filenum ) : "";
	cryoProgram.BackSlashesToSlashes();
	const bool correctedCryo = map.Icmp( "maps/game/m08_cryofac" ) == 0 &&
		( cryoProgram.Icmp( awakeningM08Version1 ) == 0 || cryoProgram.Icmp( awakeningM08Version2 ) == 0 || cryoProgram.Icmp( awakeningM08Version3 ) == 0 );
	const char *finaleNamespace = gameLocal.program.FindFunction( "oq4_m09_v4::main" ) ? "oq4_m09_v4::" :
		gameLocal.program.FindFunction( "oq4_m09_v3::main" ) ? "oq4_m09_v3::" :
		gameLocal.program.FindFunction( "oq4_m09_v2::main" ) ? "oq4_m09_v2::" : "oq4_m09_v1::";
	const bool correctedFinale = map.Icmp( "maps/game/m09_valkaryne" ) == 0 &&
		gameLocal.program.FindFunction( va( "%smain", finaleNamespace ) ) != NULL;
	// These callbacks belong to encounters removed from the recovered maps.
	// Keep the trigger's surviving target chain (including escape frost and
	// taunt audio), but do not dispatch a nonexistent function. Never strip a
	// mapper's replacement implementation or affect the uncorrected baseline.
	if ( g_awakeningLateMissions.GetBool() && ( correctedCryo || correctedFinale ) ) {
		struct obsoleteCallback_t { const char *map; const char *entity; const char *key; const char *function; };
		static const obsoleteCallback_t obsolete[] = {
			{ "maps/game/m08_cryofac", "trigger_once_46", "call", "ValkaryneEscape" },
			{ "maps/game/m08_cryofac", "trigger_once_64", "call", "VO_9_5_1" },
			{ "maps/game/m08_cryofac", "trigger_relay_76", "call", "RemoveDoorIce3" },
			{ "maps/game/m08_cryofac", "lab1_scientist_1", "script_death", "FirstLabCleared" },
			{ "maps/game/m08_cryofac", "lab1_scientist_2", "script_death", "FirstLabCleared" },
			{ "maps/game/m09_valkaryne", "trigger_once_70", "call", "map_m09::spawnTactical02" },
			{ "maps/game/m09_valkaryne", "trigger_once_96", "call", "map_m09::spawnTrashCan01" },
		};
		for ( int i = 0; i < sizeof( obsolete ) / sizeof( obsolete[0] ); ++i ) {
			const obsoleteCallback_t &entry = obsolete[i];
			idStr correctedFunction = entry.function;
			if ( correctedFinale && correctedFunction.Icmpn( "map_m09::", 9 ) == 0 ) {
				correctedFunction = va( "%s%s", finaleNamespace, entry.function + 9 );
			}
			if ( map.Icmp( entry.map ) == 0 && !idStr::Icmp( args.GetString( "name" ), entry.entity ) &&
				!idStr::Icmp( args.GetString( entry.key ), entry.function ) && !gameLocal.program.FindFunction( entry.function ) &&
				!gameLocal.program.FindFunction( correctedFunction ) ) {
				args.Delete( entry.key );
			}
		}
	}
	if ( correctedFinale ) {
		if ( gameLocal.program.FindFunction( "oq4_m09_v4::main" ) && !idStr::Icmpn( args.GetString( "team" ), "tunneldoors", 11 ) ) {
			args.SetBool( "no_touch", true );
			args.Set( "wait", "-1" );
		}
		if ( ( gameLocal.program.FindFunction( "oq4_m09_v4::main" ) != NULL || gameLocal.program.FindFunction( "oq4_m09_v3::main" ) != NULL ) &&
			!idStr::Icmp( args.GetString( "name" ), "trigger_once_15" ) ) {
			// Both surviving actors lost their old leading underscore in the
			// recovered map; the trigger still points at the obsolete names.
			if ( !idStr::Icmp( args.GetString( "target" ), "_monster_iron_maiden_5" ) ) {
				args.Set( "target", "monster_iron_maiden_5" );
			}
			if ( !idStr::Icmp( args.GetString( "target2" ), "_env_wall_casket_5" ) ) {
				args.Set( "target2", "env_wall_casket_5" );
			}
		}
		for ( int i = 0; i < args.GetNumKeyVals(); ++i ) {
			const idKeyValue *kv = args.GetKeyVal( i );
			if ( kv->GetKey().Icmpn( "call", 4 ) == 0 && kv->GetValue().Icmpn( "map_m09::", 9 ) == 0 ) {
				idStr replacement = finaleNamespace;
				replacement += kv->GetValue().c_str() + 9;
				if ( gameLocal.program.FindFunction( replacement ) != NULL ) {
					args.Set( kv->GetKey(), replacement );
				}
			}
		}
		if ( !idStr::Icmp( args.GetString( "requestDocking" ), "valkaryne_dock" ) ) {
			args.Set( "requestDocking", va( "%svalkaryne_dock", finaleNamespace ) );
		}
	}
	if ( g_awakeningLateMissions.GetBool() && correctedCryo &&
		!idStr::Icmp( args.GetString( "name" ), "trigger_once_85" ) &&
		!idStr::Icmp( args.GetString( "call" ), "FlashLightsOut" ) &&
		gameLocal.program.FindFunction( "FlashLightsOff" ) != NULL ) {
		args.Set( "call", "FlashLightsOff" );
	}
}

#include "LateMissionDetails.inl"
