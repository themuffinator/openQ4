/*
 * openQ4 Awakening scene direction. See LICENSES/QUAKE-4-SDK-EULA.rtf.
 * Uses the player's installed map and assets; no recovered content is embedded.
 */

static idCVar g_awakeningM01Cinematic( "g_awakeningM01Cinematic", "1", CVAR_GAME | CVAR_BOOL,
	"Use openQ4's Awakening m01 dropship direction on a fresh map start" );

// Exercise the real skip path without console-compiled script or input events.
// The reporting path also works after restore and after the helper is removed.
void Awakening_TestM01Feedback_f( const idCmdArgs &args ) {
	if ( gameLocal.isMultiplayer ) { return; }
	idEntity *cue = gameLocal.FindEntity( "oq4_m01_feedback" );
	if ( args.Argc() == 2 && !idStr::Icmp( args.Argv( 1 ), "skip" ) ) {
		if ( cue && gameLocal.inCinematic ) { gameLocal.SkipCinematic(); }
		return;
	}
	gameLocal.Printf( "M01_STATE time=%d cinematic=%d helper=%d phase=%s speaking=%s weld=%d glow=%d fill=%d damage=%d exitLight=%d breachLight=%d\n",
		gameLocal.time, gameLocal.inCinematic ? 1 : 0, cue ? 1 : 0,
		cue ? cue->spawnArgs.GetString( "feedback_phase", "unknown" ) : "none",
		cue ? cue->spawnArgs.GetString( "feedback_speaking", "0" ) : "0",
		gameLocal.FindEntity( "oq4_m01_welder" ) ? 1 : 0,
		gameLocal.FindEntity( "oq4_m01_weld_glow" ) ? 1 : 0,
		gameLocal.FindEntity( "oq4_m01_harness_fill" ) ? 1 : 0,
		gameLocal.FindEntity( "oq4_m01_damage" ) ? 1 : 0,
		gameLocal.FindEntity( "oq4_m01_exit_light" ) ? 1 : 0,
		gameLocal.FindEntity( "oq4_m01v3_breach_light" ) ? 1 : 0 );
}

static bool Awakening_PrepareM01Cinematic( idMapFile *map ) {
	if ( gameLocal.isMultiplayer || !g_awakeningM01Cinematic.GetBool() ||
		idStr::Icmp( cvarSystem->GetCVarString( "fs_game" ), "q4xbase" ) != 0 ) {
		return false;
	}

	idStr mapName = map->GetName();
	mapName.BackSlashesToSlashes();
	mapName.StripFileExtension();
	if ( mapName.Icmp( "maps/game/m01_stranarus_trench1" ) != 0 ) {
		return false;
	}

	// Do not redirect a different revision or a mapper's replacement callbacks.
	bool cabinAnchor = false;
	bool evacuationAnchor = false;
	for ( int i = 1; i < map->GetNumEntities(); ++i ) {
		const idDict &args = map->GetEntity( i )->epairs;
		if ( idStr::Icmp( args.GetString( "classname" ), "trigger_relay" ) != 0 ) {
			continue;
		}
		if ( idStr::Icmp( args.GetString( "name" ), "intro_unit1_seq_5_trigger_seq_6" ) == 0 ) {
			cabinAnchor = idStr::Icmp( args.GetString( "call" ), "intro_unit1_seq_6" ) == 0;
		} else if ( idStr::Icmp( args.GetString( "name" ), "m04_trench_unit1_main" ) == 0 ) {
			evacuationAnchor = idStr::Icmp( args.GetString( "call" ), "m04_trench_unit1_seq_1_main" ) == 0;
		}
	}
	// Each revision is immutable: suspended save threads retain their bytecode.
	// The original filename remains available for pre-lighting M01 saves.
	const char *script = "scripts/openq4/awakening/m01_dropship-v3.script";
	if ( !cabinAnchor || !evacuationAnchor || fileSystem->ReadFile( script, NULL, NULL ) <= 0 ) {
		gameLocal.Warning( "Awakening m01 direction unavailable: keeping the map's original callbacks" );
		return false;
	}

	// Compile after worldspawn, as a map script. The ordinary script save/restore
	// file list then owns this addition; the default program and old saves keep
	// their original statement layout. Never mutate the cached map entity string.
	gameLocal.program.CompileFile( script );
	// Parse the helper while level media is being cached, before the first impact.
	gameLocal.FindEntityDef( "oq4_m01v3_cues", false );
	gameLocal.FindEntityDef( "oq4_m01v3_action", false );
	gameLocal.Printf( "Awakening m01: openQ4 dropship direction enabled\n" );
	return true;
}

static void Awakening_DirectM01Relay( idDict &args ) {
	if ( idStr::Icmp( args.GetString( "name" ), "intro_unit1_seq_5_trigger_seq_6" ) == 0 ) {
		args.Set( "call", "oq4_m01::cabin" );
	} else if ( idStr::Icmp( args.GetString( "name" ), "m04_trench_unit1_main" ) == 0 ) {
		args.Set( "call", "oq4_m01::evacuate" );
	}
}
