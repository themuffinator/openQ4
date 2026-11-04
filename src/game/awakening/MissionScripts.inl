/*
 * Corrections to the player's installed Awakening M02 script. No recovered
 * script is bundled. Keep v1 stable: saves recompile this exact virtual source.
 * Original saves still compile maps/game/m02_trianfac.script unchanged.
 * See docs/dev/awakening-missions.md for intent, provenance and validation.
 */

#include "MissionScripts.h"
#include "MissionM05M06.inl"
#include "M07MissionScript.inl"

static const char * const awakeningM02Source = "maps/game/m02_trianfac.script";
static const char * const awakeningM02Version1 = "maps/game/m02_trianfac.openq4-v1.script";
static const char * const awakeningM02Version2 = "maps/game/m02_trianfac.openq4-v2.script";

#include "LateMissions.inl"

bool Awakening_LoadMissionScript( const char *filename, idStr &source ) {
	if ( Awakening_LoadM05M06Script( filename, source ) ) {
		return true;
	}
	if ( Awakening_LoadM07MissionScript( filename, source ) ) {
		return true;
	}
	if ( Awakening_LoadLateMissionScript( filename, source ) ) {
		return true;
	}
	idStr normalizedName = filename;
	normalizedName.BackSlashesToSlashes();
	const bool version2 = normalizedName.Icmp( awakeningM02Version2 ) == 0;
	if ( ( !version2 && normalizedName.Icmp( awakeningM02Version1 ) != 0 ) || gameLocal.isMultiplayer ||
		idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) != 0 ) {
		return false;
	}
	char *installed = NULL;
	if ( fileSystem->ReadFile( awakeningM02Source, reinterpret_cast<void **>( &installed ), NULL ) <= 0 ) {
		if ( installed != NULL ) {
			fileSystem->FreeFile( installed );
		}
		return false;
	}
	source = installed;
	fileSystem->FreeFile( installed );
	source.Replace( "\r\n", "\n" );

	struct correction_t { const char *before; const char *after; };
	static const correction_t corrections[] = {
		// ScriptedMove already wakes each bot; attach it to its own station.
		{ "$repbot_1.bind( proc_bot_spot );", "proc_bot.bind( proc_bot_spot );" },
		// The destroyed-generator objective belongs before, not after, sabotage.
		{ "void TeamToGeneratorRoom()\n{",
		  "void TeamToGeneratorRoom()\n{\n\tsys.trigger( $item_objective_4 );" },
		{ "sys.trigger( $item_objective_4 );\n\n\taiScriptedMove",
		  "sys.trigger( $item_objectivecomplete_4 );\n\n\taiScriptedMove" },
		// The authored six-bot death counter points at a deleted relay. Retch
		// death starts this callback; wait for both parts of the defence to end.
		{ "void TakToGeneratorConsole()\n{",
		  "void TakToGeneratorConsole()\n{\n"
		  "\twhile ( isLivingEntity( $proc_bot_1 ) || isLivingEntity( $proc_bot_2 ) ||\n"
		  "\t        isLivingEntity( $proc_bot_3 ) || isLivingEntity( $proc_bot_4 ) ||\n"
		  "\t        isLivingEntity( $proc_bot_5 ) || isLivingEntity( $proc_bot_6 ) ) {\n"
		  "\t\tsys.waitFrame();\n\t}\n" },
		// Finish the animation before resetting its speed or starting its cycle.
		{ "aiScriptedAnim( $razor1, \"ad_fights_grunt_2\", 1, 1 );",
		  "aiScriptedAnimWait( $razor1, \"ad_fights_grunt_2\", 1, 1 );" },
		{ "$env_dropship_1.playAnim( ANIMCHANNEL_ALL, \"hangar_land\");",
		  "playAnimWait( $env_dropship_1, ANIMCHANNEL_ALL, \"hangar_land\" );" },
		{ "void BossKilled()\n{",
		  "void BossKilled()\n{\n\tsys.trigger( $item_objectivecomplete_6 );" },
		{ "if ( uplink_room_cleared >= 4 )", "if ( uplink_room_cleared == 4 )" },
		// v2: the extraction scene spawns a separate Darnell beside the cargo.
		// The original squad actor remains on the lower floor.
		{ "$darnell.setMoveSpeed( AIMOVESPEED_RUN );", "$darnell_ending.setMoveSpeed( AIMOVESPEED_RUN );" },
		{ "aiScriptedMove( $darnell, $darnell_ending_pos_2, 5, 0 );",
		  "aiScriptedMove( $darnell_ending, $darnell_ending_pos_2, 5, 0 );" },
	};
	// Refuse partial correction of a mapper's revision. Old source remains a
	// valid fallback on fresh starts; a v1 save must fail rather than reinterpret
	// its suspended threads with a different program.
	const int count = sizeof( corrections ) / sizeof( corrections[0] ) - ( version2 ? 0 : 2 );
	for ( int i = 0; i < count; ++i ) {
		const int first = source.Find( corrections[i].before );
		if ( first < 0 || source.Find( corrections[i].before, true, first + 1 ) >= 0 ) {
			source.Clear();
			return false;
		}
		source.Replace( corrections[i].before, corrections[i].after );
	}
	return true;
}

const char *Awakening_MissionScriptName( const char *filename ) {
	filename = Awakening_M07MissionScriptName( filename );
	const char *lateMission = Awakening_LateMissionScriptName( filename );
	if ( lateMission != filename ) {
		return lateMission;
	}
	idStr normalizedName = filename;
	normalizedName.BackSlashesToSlashes();
	if ( !gameLocal.isMultiplayer && idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) == 0 &&
		normalizedName.Icmp( awakeningM02Source ) == 0 ) {
		idStr source;
		if ( Awakening_LoadMissionScript( awakeningM02Version2, source ) ) {
			gameLocal.Printf( "Awakening M02: mission corrections v2 enabled\n" );
			return awakeningM02Version2;
		}
		gameLocal.Warning( "Awakening M02: unrecognized script; keeping installed mission behaviour" );
	}
	return filename;
}
