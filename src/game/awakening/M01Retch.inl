/*
 * openQ4 direction for The Awakening's Retch ambush in m01 part 2.
 * See LICENSES/QUAKE-4-SDK-EULA.rtf. The scene is
 * scripts/openq4/awakening/m01_retch.script; it uses the player's installed
 * map, performances and voices. No recovered content is embedded.
 */

static idCVar g_awakeningM01Retch( "g_awakeningM01Retch", "1", CVAR_GAME | CVAR_BOOL,
	"Use openQ4's direction for the Retch ambush in Awakening m01 part 2 on a fresh map start" );

static const char * const awakeningM01RetchScript = "scripts/openq4/awakening/m01_retch.script";

static bool Awakening_PrepareM01Retch( idMapFile *map ) {
	if ( gameLocal.isMultiplayer || !g_awakeningM01Retch.GetBool() || !FS_AwakeningActive() ) {
		return false;
	}
	idStr mapName = map->GetName();
	mapName.BackSlashesToSlashes();
	mapName.StripFileExtension();
	if ( mapName.Icmp( "maps/game/m01_stranarus_trench2" ) != 0 ) {
		return false;
	}
	// The scene must still be the one this direction was written for.
	static const char * const cast[] = {
		"m04_building_unit2_seq_4_retch_kill_marine", "m04_building_unit2_seq_4_retch_1",
		"m04_building_unit2_seq_5_retch_kill_door_anc", "m04_building_unit2_seq_5_retch_kill_door",
		"m04_building_unit2_seq_5_cin_cam_main", "m04_building_unit2_seq_5_lt_tank",
		"m04_building_unit2_seq_3_ai_blocker_top_level", "tak", "navajas"
	};
	bool anchor = false;
	int found = 0;
	for ( int i = 1; i < map->GetNumEntities(); ++i ) {
		const idDict &args = map->GetEntity( i )->epairs;
		const char *name = args.GetString( "name" );
		if ( idStr::Icmp( name, "m04_building_unit2_seq_5" ) == 0 ) {
			anchor = idStr::Icmp( args.GetString( "call" ), "m04_building_unit2_seq_5" ) == 0;
		}
		for ( int c = 0; c < (int)( sizeof( cast ) / sizeof( cast[0] ) ); ++c ) {
			if ( idStr::Icmp( name, cast[c] ) == 0 ) {
				found++;
			}
		}
	}
	if ( !anchor || found != (int)( sizeof( cast ) / sizeof( cast[0] ) ) ||
		fileSystem->ReadFile( awakeningM01RetchScript, NULL, NULL ) <= 0 ) {
		gameLocal.Warning( "Awakening m01 Retch ambush direction unavailable: keeping the map's original scene" );
		return false;
	}
	// Compiled as a map script, after worldspawn: saves own it through their file list.
	gameLocal.program.CompileFile( awakeningM01RetchScript );
	gameLocal.Printf( "Awakening m01: openQ4 Retch ambush direction enabled\n" );
	return true;
}

static void Awakening_DirectM01Retch( idDict &args ) {
	if ( idStr::Icmp( args.GetString( "name" ), "m04_building_unit2_seq_5" ) == 0 &&
		idStr::Icmp( args.GetString( "call" ), "m04_building_unit2_seq_5" ) == 0 &&
		gameLocal.program.FindFunction( "oq4_m01_retch::ambush" ) != NULL ) {
		args.Set( "call", "oq4_m01_retch::ambush" );
	}
}
