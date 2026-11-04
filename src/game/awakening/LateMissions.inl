/*
 * Independently authored corrections to user-supplied Awakening M08/M09.
 * SDK EULA; see LICENSES/QUAKE-4-SDK-EULA.rtf and docs/dev/awakening-missions.md.
 * These virtual v1 sources are immutable savegame inputs. Never rewrite the
 * original program: old saves must retain their original statement layout.
 */

static const char * const awakeningM08Source = "maps/game/m08_cryofac.script";
static const char * const awakeningM08Version1 = "maps/game/m08_cryofac.openq4-v1.script";
static const char * const awakeningM09Source = "scripts/maps/m09_valkaryne.script";
static const char * const awakeningM09Version1 = "maps/game/m09_valkaryne.openq4-v1.script";
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

static bool Awakening_LoadLateMissionScript( const char *filename, idStr &source ) {
	idStr path = filename;
	path.BackSlashesToSlashes();
	const bool cryo = path.Icmp( awakeningM08Version1 ) == 0;
	if ( !Awakening_LateCampaignActive() || ( !cryo && path.Icmp( awakeningM09Version1 ) != 0 ) ) {
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
	return true;
}

static const char *Awakening_LateMissionScriptName( const char *filename ) {
	idStr path = filename;
	path.BackSlashesToSlashes();
	if ( g_awakeningLateMissions.GetBool() && Awakening_LateCampaignActive() && path.Icmp( awakeningM08Source ) == 0 ) {
		idStr source;
		if ( Awakening_LoadLateMissionScript( awakeningM08Version1, source ) ) {
			gameLocal.Printf( "Awakening M08: mission corrections v1 enabled\n" );
			return awakeningM08Version1;
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
	if ( !Awakening_LoadLateMissionScript( awakeningM09Version1, source ) ) {
		gameLocal.Warning( "Awakening M09: unrecognized script; keeping installed mission behaviour" );
		return;
	}
	gameLocal.program.CompileFile( awakeningM09Version1 );
	worldArgs.Set( "call", "oq4_m09_v1::main" );
	gameLocal.Printf( "Awakening M09: mission corrections v1 enabled\n" );
}

void Awakening_DirectLateMissionEntity( idDict &args ) {
	if ( !Awakening_LateCampaignActive() ) { return; }
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	if ( map.Icmp( "maps/game/m09_valkaryne" ) == 0 &&
		gameLocal.program.FindFunction( "oq4_m09_v1::main" ) != NULL ) {
		for ( int i = 0; i < args.GetNumKeyVals(); ++i ) {
			const idKeyValue *kv = args.GetKeyVal( i );
			if ( kv->GetKey().Icmpn( "call", 4 ) == 0 && kv->GetValue().Icmpn( "map_m09::", 9 ) == 0 ) {
				idStr replacement = "oq4_m09_v1::";
				replacement += kv->GetValue().c_str() + 9;
				if ( gameLocal.program.FindFunction( replacement ) != NULL ) {
					args.Set( kv->GetKey(), replacement );
				}
			}
		}
		if ( !idStr::Icmp( args.GetString( "requestDocking" ), "valkaryne_dock" ) ) {
			args.Set( "requestDocking", "oq4_m09_v1::valkaryne_dock" );
		}
	}
	if ( g_awakeningLateMissions.GetBool() && map.Icmp( "maps/game/m08_cryofac" ) == 0 &&
		!idStr::Icmp( args.GetString( "name" ), "trigger_once_85" ) &&
		!idStr::Icmp( args.GetString( "call" ), "FlashLightsOut" ) &&
		gameLocal.program.FindFunction( "FlashLightsOff" ) != NULL ) {
		args.Set( "call", "FlashLightsOff" );
	}
}
