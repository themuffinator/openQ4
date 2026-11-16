/*
 * openQ4 placement of The Awakening's unrecorded m01 battle radio.
 * See LICENSES/QUAKE-4-SDK-EULA.rtf and docs/dev/awakening-missing-voice-lines.md.
 * The words come from the player's installed draft script; the placement is
 * scripts/openq4/awakening/m01_radio.script. No recovered content is embedded.
 */

static idCVar g_awakeningM01Radio( "g_awakeningM01Radio", "1", CVAR_GAME | CVAR_INTEGER,
	"The Awakening m01 battle radio drafted but never recorded. 0 off; 1 when the content declares its lines; "
	"2 always, to test the placement without audio (undeclared lines stay silent)", 0, 2 );

typedef struct awakeningM01RadioHook_s {
	const char *map;		// maps/game/<map>, without extension
	const char *entity;		// the trigger whose scene the radio follows
	const char *call;		// the scene function the map calls
	const char *placed;		// the same scene, followed by its radio
} awakeningM01RadioHook_t;

static const awakeningM01RadioHook_t awakeningM01RadioHooks[] = {
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_2",                       "m04_trench_unit1_seq_2",                    "oq4_m01_radio::landingZone" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_3",                       "m04_trench_unit1_seq_3",                    "oq4_m01_radio::harvesterDown" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_4",                       "m04_trench_unit1_seq_4",                    "oq4_m01_radio::squadMovesUp" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_7",                       "m04_trench_unit1_seq_7",                    "oq4_m01_radio::turretBunkers" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_7_player_blocker_1_trig", "m04_trench_unit1_seq_7_disable_turrets",    "oq4_m01_radio::bunkerClear" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_9",                       "m04_trench_unit1_seq_9",                    "oq4_m01_radio::markswellOrders" },
	{ "maps/game/m01_stranarus_trench1", "m04_trench_unit1_seq_10_begin_run_inside",     "m04_trench_unit1_seq_10_begin_run_inside",  "oq4_m01_radio::caveCollapse" },
	{ "maps/game/m01_stranarus_trench2", "m04_building_unit2_seq_1",                     "m04_building_unit2_seq_1",                  "oq4_m01_radio::supplyTunnels" },
	{ "maps/game/m01_stranarus_trench2", "m04_maint_tunnel_unit1_seq_1",                 "m04_maint_tunnel_unit1_seq_1",              "oq4_m01_radio::walkersFound" },
	{ "maps/game/m01_stranarus_trench2", "m04_maint_tunnel_unit1_seq_4_area_clear_and_charges_set",
		"m04_maint_tunnel_unit1_seq_4_area_clear_and_charges_set", "oq4_m01_radio::tunnelCleared" },
	{ "maps/game/m01_stranarus_trench2", "m04_trench_unit2_seq_1",                       "m04_trench_unit2_seq_1",                    "oq4_m01_radio::lastTrench" },
};

static const char * const awakeningM01RadioScript = "scripts/openq4/awakening/m01_radio.script";

static bool Awakening_PrepareM01Radio( idMapFile *map ) {
	if ( gameLocal.isMultiplayer || g_awakeningM01Radio.GetInteger() <= 0 || !FS_AwakeningActive() ) {
		return false;
	}
	idStr mapName = map->GetName();
	mapName.BackSlashesToSlashes();
	mapName.StripFileExtension();
	if ( mapName.Icmp( "maps/game/m01_stranarus_trench1" ) != 0 && mapName.Icmp( "maps/game/m01_stranarus_trench2" ) != 0 ) {
		return false;
	}
	// Nothing to hear until the lines have content: leave the map's program alone.
	if ( g_awakeningM01Radio.GetInteger() < 2 && !Awakening_SoundDeclared( "vo_4_2_2_1" ) ) {
		return false;
	}
	// Every scene must still be the one the placement was written for.
	int anchors = 0, expected = 0;
	for ( int h = 0; h < (int)( sizeof( awakeningM01RadioHooks ) / sizeof( awakeningM01RadioHooks[ 0 ] ) ); h++ ) {
		if ( mapName.Icmp( awakeningM01RadioHooks[ h ].map ) != 0 ) {
			continue;
		}
		expected++;
		for ( int i = 1; i < map->GetNumEntities(); ++i ) {
			const idDict &args = map->GetEntity( i )->epairs;
			if ( idStr::Icmp( args.GetString( "name" ), awakeningM01RadioHooks[ h ].entity ) == 0 &&
				idStr::Icmp( args.GetString( "call" ), awakeningM01RadioHooks[ h ].call ) == 0 ) {
				anchors++;
				break;
			}
		}
	}
	if ( anchors != expected || fileSystem->ReadFile( awakeningM01RadioScript, NULL, NULL ) <= 0 ) {
		gameLocal.Warning( "Awakening m01 radio unavailable: the map's scenes are not the ones it was placed in" );
		return false;
	}
	// Compiled as a map script, after worldspawn: saves own it through their file list.
	gameLocal.program.CompileFile( awakeningM01RadioScript );
	gameLocal.Printf( "Awakening m01: battle radio placed\n" );
	return true;
}

static void Awakening_DirectM01Radio( idDict &args ) {
	idStr map;
	for ( int h = 0; h < (int)( sizeof( awakeningM01RadioHooks ) / sizeof( awakeningM01RadioHooks[ 0 ] ) ); h++ ) {
		const awakeningM01RadioHook_t &hook = awakeningM01RadioHooks[ h ];
		if ( idStr::Icmp( args.GetString( "name" ), hook.entity ) != 0 || idStr::Icmp( args.GetString( "call" ), hook.call ) != 0 ) {
			continue;
		}
		if ( !map.Length() ) {
			map = gameLocal.GetMapName();
			map.BackSlashesToSlashes();
			map.StripFileExtension();
		}
		if ( map.Icmp( hook.map ) == 0 && gameLocal.program.FindFunction( hook.placed ) != NULL ) {
			args.Set( "call", hook.placed );
		}
	}
}
