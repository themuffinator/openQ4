/*
 * Corrections to the player's installed Awakening M02 script. No recovered
 * script is bundled. Keep v1 and v2 stable: saves recompile this exact virtual
 * source. Original saves still compile maps/game/m02_trianfac.script unchanged.
 * See docs/dev/awakening-missions.md for intent, provenance and validation.
 */

#include "MissionScripts.h"
#include "MissionM05M06.inl"
#include "M07MissionScript.inl"

static const char * const awakeningM02Source = "maps/game/m02_trianfac.script";
static const char * const awakeningM02Version1 = "maps/game/m02_trianfac.openq4-v1.script";
static const char * const awakeningM02Version2 = "maps/game/m02_trianfac.openq4-v2.script";
static const char * const awakeningM02Version3 = "maps/game/m02_trianfac.openq4-v3.script";

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
	int version = 0;
	if ( normalizedName.Icmp( awakeningM02Version1 ) == 0 ) {
		version = 1;
	} else if ( normalizedName.Icmp( awakeningM02Version2 ) == 0 ) {
		version = 2;
	} else if ( normalizedName.Icmp( awakeningM02Version3 ) == 0 ) {
		version = 3;
	}
	if ( version == 0 || gameLocal.isMultiplayer || !FS_AwakeningActive() ) {
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

	// Each correction belongs to the first version that applied it. A version
	// applies its own and every earlier correction, in this order.
	struct correction_t { int version; const char *before; const char *after; };
	static const correction_t corrections[] = {
		// ScriptedMove already wakes each bot; attach it to its own station.
		{ 1, "$repbot_1.bind( proc_bot_spot );", "proc_bot.bind( proc_bot_spot );" },
		// The destroyed-generator objective belongs before, not after, sabotage.
		{ 1, "void TeamToGeneratorRoom()\n{",
		  "void TeamToGeneratorRoom()\n{\n\tsys.trigger( $item_objective_4 );" },
		{ 1, "sys.trigger( $item_objective_4 );\n\n\taiScriptedMove",
		  "sys.trigger( $item_objectivecomplete_4 );\n\n\taiScriptedMove" },
		// The authored six-bot death counter points at a deleted relay. Retch
		// death starts this callback; wait for both parts of the defence to end.
		{ 1, "void TakToGeneratorConsole()\n{",
		  "void TakToGeneratorConsole()\n{\n"
		  "\twhile ( isLivingEntity( $proc_bot_1 ) || isLivingEntity( $proc_bot_2 ) ||\n"
		  "\t        isLivingEntity( $proc_bot_3 ) || isLivingEntity( $proc_bot_4 ) ||\n"
		  "\t        isLivingEntity( $proc_bot_5 ) || isLivingEntity( $proc_bot_6 ) ) {\n"
		  "\t\tsys.waitFrame();\n\t}\n" },
		// Finish the animation before resetting its speed or starting its cycle.
		{ 1, "aiScriptedAnim( $razor1, \"ad_fights_grunt_2\", 1, 1 );",
		  "aiScriptedAnimWait( $razor1, \"ad_fights_grunt_2\", 1, 1 );" },
		{ 1, "$env_dropship_1.playAnim( ANIMCHANNEL_ALL, \"hangar_land\");",
		  "playAnimWait( $env_dropship_1, ANIMCHANNEL_ALL, \"hangar_land\" );" },
		{ 1, "void BossKilled()\n{",
		  "void BossKilled()\n{\n\tsys.trigger( $item_objectivecomplete_6 );" },
		{ 1, "if ( uplink_room_cleared >= 4 )", "if ( uplink_room_cleared == 4 )" },
		// v2: the extraction scene spawns a separate Darnell beside the cargo.
		// The original squad actor remains on the lower floor.
		{ 2, "$darnell.setMoveSpeed( AIMOVESPEED_RUN );", "$darnell_ending.setMoveSpeed( AIMOVESPEED_RUN );" },
		{ 2, "aiScriptedMove( $darnell, $darnell_ending_pos_2, 5, 0 );",
		  "aiScriptedMove( $darnell_ending, $darnell_ending_pos_2, 5, 0 );" },
		// v3: state shared by the sequences below.
		{ 3, "void PlayerBeyondMomentaryDoors()\n{",
		  "float openq4GeneratorRoomStarted = 0;\n"
		  "float openq4StuckDoorStage = 0;\n"
		  "float openq4StuckDoorRaised = 0;\n"
		  "float openq4StuckDoorDropped = 0;\n\n"
		  "void PlayerBeyondMomentaryDoors()\n{" },
		// v3: stepping off the lift starts TeamToGeneratorRoom at once. The lift
		// arrival then sent Markswell and Gray back to the lift, one at a time,
		// overriding the generator orders: Gray worked the generator console
		// from the lift and died far from the Retch dropped onto that console.
		{ 3, "\taiScriptedMoveWait( $tak, $tak_pos_27, 32, 1 );\n"
		  "\taiScriptedMoveWait( $markswell, $markswell_pos_27, 32, 1 );\n"
		  "\taiScriptedMoveWait( $razor1, $razor1_pos_27, 32, 0 );\n",
		  "\tif ( !openq4GeneratorRoomStarted ) {\n"
		  "\t\taiScriptedMove( $tak, $tak_pos_27, 32, 1 );\n"
		  "\t\taiScriptedMove( $markswell, $markswell_pos_27, 32, 1 );\n"
		  "\t\taiScriptedMove( $razor1, $razor1_pos_27, 32, 0 );\n"
		  "\t}\n" },
		{ 3, "void TeamToGeneratorRoom()\n{\n\tsys.trigger( $item_objective_4 );",
		  "void TeamToGeneratorRoom()\n{\n\tsys.trigger( $item_objective_4 );\n\topenq4GeneratorRoomStarted = 1;" },
		// v3: Tak picks the spike gun up, examines it and hands it over. The
		// authored scene played the empty-handed gift beside a gun left on the
		// floor. The gun rides the joint that carries his own machinegun.
		{ 3, "\taiScriptedFaceWait( $tak, $weapon_spikegun_1, 0 );\n\n"
		  "\taiSpeakWait( $tak, \"lipsync_tak_hmmletmesee\" );\n\n"
		  "\taiSpeak( $tak, \"lipsync_tak_herekedshoots\" );\n\n"
		  "\taiScriptedAnimWait( $tak, \"player_gift2_start\", 0, 0 );\n"
		  "\taiScriptedAnimWait( $tak, \"player_gift2_loop\", 0, 0 );\n"
		  "\taiScriptedAnimWait( $tak, \"player_gift2_end\", 0, 0 );\n\n"
		  "\t$weapon_spikegun_1.remove();\n"
		  "\tsys.trigger( $target_give_3 );\n",
		  "\taiScriptedFaceWait( $tak, $weapon_spikegun_1, 0 );\n"
		  "\taiScriptedAnimWait( $tak, \"stand_to_crouch\", 4, 0 );\n"
		  "\t$tak.hideSurface( \"models/weapons/mp_guns/w_mg_lo\" );\n"
		  "\t$weapon_spikegun_1.bindToJoint( $tak, \"MG_stock_joint\", 1 );\n"
		  "\t$weapon_spikegun_1.setOrigin( '0 -7 9' );\n"
		  "\t$weapon_spikegun_1.setAngles( '0 0 -90' );\n"
		  "\taiScriptedAnimWait( $tak, \"crouch_to_stand\", 4, 0 );\n"
		  "\taiScriptedFaceWait( $tak, $player1, 0 );\n\n"
		  "\taiSpeak( $tak, \"lipsync_tak_hmmletmesee\" );\n"
		  "\taiScriptedAnimWait( $tak, \"weapon_check_1\", 4, 0 );\n"
		  "\tif ( $tak.isSpeaking() ) {\n"
		  "\t\taiScriptedAnimWait( $tak, \"weapon_check_2\", 4, 0 );\n"
		  "\t}\n"
		  "\taiSpeakDone( $tak );\n\n"
		  "\taiScriptedFaceWait( $tak, $player1, 0 );\n"
		  "\taiSpeak( $tak, \"lipsync_tak_herekedshoots\" );\n"
		  "\taiScriptedAnimWait( $tak, \"player_gift_start\", 4, 0 );\n"
		  "\taiScriptedAnimLoop( $tak, \"player_gift_loop\", 4 );\n"
		  "\tsys.wait( 1.5 );\n"
		  "\taiSpeakDone( $tak );\n\n"
		  "\t$weapon_spikegun_1.remove();\n"
		  "\tsys.trigger( $target_give_3 );\n"
		  "\taiScriptedAnimWait( $tak, \"player_gift_end\", 4, 0 );\n"
		  "\t$tak.showSurface( \"models/weapons/mp_guns/w_mg_lo\" );\n" },
		// v3: the Tank introduction parks its stand-in Dekard at the extraction
		// mark, in sight through the arena's open north gate, for the whole
		// fight. Keep him hidden until the ending walk needs him.
		{ 3, "\t$dekard.setOrigin( $dekard_ending_pos_4.getOrigin() );\n\n\tsys.firstPerson();",
		  "\t$dekard.setOrigin( $dekard_ending_pos_4.getOrigin() );\n\t$dekard.hide();\n\n\tsys.firstPerson();" },
		{ 3, "\t$dekard.setOrigin( $dekard_ending_pos_1.getOrigin() );\n",
		  "\t$dekard.setOrigin( $dekard_ending_pos_1.getOrigin() );\n\t$dekard.show();\n" },
		// v3: the stuck door was not solid, so the player could walk through it
		// before Darnell and Mitchell lifted it and drop it early. Block players
		// and projectiles only: the squad's authored marks stand inside its
		// bounds and Tak ducks through the first, 32-unit opening.
		{ 3, "void InitCharacters()\n{",
		  "void InitCharacters()\n{\n\t$stuck_door.setContents( CONTENTS_PLAYERCLIP | 16777216 );" },
		// v3: the scripted moves stop anywhere within eight units of the marks,
		// which can leave less room between the pair than a crouching player
		// needs now that the door itself blocks. Stand exactly on the marks.
		{ 3, "\taiScriptedMoveWait( $darnell, $darnell_door_pos, 8, 0 );\n"
		  "\taiScriptedMoveWait( $mitchell, $mitchell_door_pos, 8, 0 );\n",
		  "\taiScriptedMoveWait( $darnell, $darnell_door_pos, 8, 0 );\n"
		  "\taiScriptedMoveWait( $mitchell, $mitchell_door_pos, 8, 0 );\n"
		  "\t$darnell.setOrigin( $darnell_door_pos.getWorldOrigin() );\n"
		  "\t$mitchell.setOrigin( $mitchell_door_pos.getWorldOrigin() );\n" },
		// v3: hold each lift pose until the next stage instead of letting go
		// after a single cycle. Repeat it one cycle at a time from where the
		// lift put them: the cycles' root motion would otherwise walk Darnell
		// into the gap the player crouches through. A hold ends with its stage,
		// so it never takes the pose again once the door has been dropped.
		{ 3, "void TeammateLiftDoor( entity teammate, entity look_target )\n{",
		  "void openq4HoldStuckDoor( entity teammate, string anim, float stage )\n{\n"
		  "\tvector spot = teammate.getWorldOrigin();\n"
		  "\twhile ( openq4StuckDoorStage == stage ) {\n"
		  "\t\taiScriptedAnimWait( teammate, anim, 0, 0 );\n"
		  "\t\tif ( openq4StuckDoorStage == stage ) {\n"
		  "\t\t\tteammate.setOrigin( spot );\n"
		  "\t\t}\n"
		  "\t}\n"
		  "}\n\n"
		  "void TeammateLiftDoor( entity teammate, entity look_target )\n{" },
		{ 3, "\taiScriptedAnimWait( teammate, \"tr_doorlift_loop_1\", 0, 0 );",
		  "\topenq4HoldStuckDoor( teammate, \"tr_doorlift_loop_1\", 1 );" },
		{ 3, "\taiScriptedAnimWait( teammate, \"tr_doorlift_loop_2\", 0, 0 );",
		  "\topenq4HoldStuckDoor( teammate, \"tr_doorlift_loop_2\", 2 );" },
		{ 3, "\tthread TeammateLiftDoor( $darnell, $darnell_stuck_door_look_target );",
		  "\topenq4StuckDoorStage = 1;\n\tthread TeammateLiftDoor( $darnell, $darnell_stuck_door_look_target );" },
		{ 3, "\tthread TeammateLiftDoorHigher( $darnell );",
		  "\topenq4StuckDoorStage = 2;\n\tthread TeammateLiftDoorHigher( $darnell );" },
		// v3: the door drops only once it is fully raised, and the urging stops
		// once the player is through.
		{ 3, "void DarnellMitchellDropDoor()\n{",
		  "void DarnellMitchellDropDoor()\n{\n"
		  "\twhile ( !openq4StuckDoorRaised ) {\n\t\tsys.waitFrame();\n\t}\n"
		  "\topenq4StuckDoorStage = 3;\n"
		  "\topenq4StuckDoorDropped = 1;" },
		// v3: Darnell and Mitchell stay behind the closed door; it now blocks
		// them too, and a follow order would only walk them into it.
		{ 3, "\t$stuck_door.move( DOWN, 48 );\n\tsys.waitFor( $stuck_door );\n",
		  "\t$stuck_door.move( DOWN, 48 );\n\tsys.waitFor( $stuck_door );\n"
		  "\t$stuck_door.setContents( CONTENTS_SOLID );\n"
		  "\t$darnell.setLeader( $null_entity );\n"
		  "\t$mitchell.setLeader( $null_entity );\n" },
		{ 3, "\t$stuck_door.move( UP, 16 );\n\tsys.waitFor( $stuck_door );\n\n"
		  "\taiSpeakWait( $mitchell, \"lipsync_mitch_pain\" );\n"
		  "\taiSpeakWait( $darnell, \"lipsync_darn_dekhrry\" );\n"
		  "\taiSpeakWait( $mitchell, \"lipsync_mitch_cntholdlng\" );\n\n"
		  "\taiSpeakWait( $darnell, \"lipsync_darn_comondek\" );\n"
		  "\taiSpeakWait( $darnell, \"lipsync_darn_pain2\" );\n"
		  "\taiSpeakWait( $mitchell, \"lipsync_mitch_pain2\" );\n\n"
		  "\tradioChatterPlay(",
		  "\t$stuck_door.move( UP, 16 );\n\tsys.waitFor( $stuck_door );\n"
		  "\topenq4StuckDoorRaised = 1;\n\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $mitchell, \"lipsync_mitch_pain\" ); }\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $darnell, \"lipsync_darn_dekhrry\" ); }\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $mitchell, \"lipsync_mitch_cntholdlng\" ); }\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $darnell, \"lipsync_darn_comondek\" ); }\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $darnell, \"lipsync_darn_pain2\" ); }\n"
		  "\tif ( !openq4StuckDoorDropped ) { aiSpeakWait( $mitchell, \"lipsync_mitch_pain2\" ); }\n"
		  "\twhile ( !openq4StuckDoorDropped ) {\n\t\tsys.waitFrame();\n\t}\n"
		  "\tsys.wait( 1.0 );\n\n"
		  "\tradioChatterPlay(" },
	};
	// Refuse partial correction of a mapper's revision. Old source remains a
	// valid fallback on fresh starts; a v1 or v2 save must fail rather than
	// reinterpret its suspended threads with a different program.
	const int total = sizeof( corrections ) / sizeof( corrections[0] );
	for ( int i = 0; i < total; ++i ) {
		if ( corrections[i].version > version ) {
			continue;
		}
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
	if ( !gameLocal.isMultiplayer && FS_AwakeningActive() &&
		normalizedName.Icmp( awakeningM02Source ) == 0 ) {
		idStr source;
		if ( Awakening_LoadMissionScript( awakeningM02Version3, source ) ) {
			gameLocal.Printf( "Awakening M02: mission corrections v3 enabled\n" );
			return awakeningM02Version3;
		}
		gameLocal.Warning( "Awakening M02: unrecognized script; keeping installed mission behaviour" );
	}
	return filename;
}
