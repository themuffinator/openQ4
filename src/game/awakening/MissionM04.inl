/*
 * M04 prison corrections, authored for openQ4 under the Quake 4 SDK EULA.
 * Transform the player's installed script; no recovered script is bundled.
 * Keep each version stable: saved threads recompile its source on restore.
 * Original saves still compile maps/game/m04_prison.script unchanged.
 * The callback repairs of revisions 1-3 live in CampaignRepairs.inl and the
 * appended scripts/openq4/awakening/m04_prison*.script helpers.
 */
static const char * const awakeningM04Source = "maps/game/m04_prison.script";
static const char * const awakeningM04Version4 = "maps/game/m04_prison.openq4-v4.script";

static bool Awakening_LoadM04MissionScript( const char *filename, idStr &source ) {
	idStr normalized = filename;
	normalized.BackSlashesToSlashes();
	if ( normalized.Icmp( awakeningM04Version4 ) != 0 || gameLocal.isMultiplayer || !FS_AwakeningActive() ) {
		return false;
	}
	char *installed = NULL;
	const int length = fileSystem->ReadFile( awakeningM04Source, reinterpret_cast<void **>( &installed ), NULL );
	if ( length > 0 ) {
		source = installed;
	}
	if ( installed != NULL ) {
		fileSystem->FreeFile( installed );
	}
	if ( length <= 0 ) {
		return false;
	}
	source.Replace( "\r\n", "\n" );

	struct correction_t { const char *before; const char *after; };
	static const correction_t corrections[] = {
		// State shared by the corrections below.
		{ "\tvoid straussCinematicOver();",
		  "\tstring openq4TakFailTitle;\n"
		  "\tfloat openq4TakFailed;\n"
		  "\tvoid secRoomNag();\n\n"
		  "\tvoid straussCinematicOver();" },
		// Each block's seventh prisoner shares the bottom cell with the sixth
		// and has the same activation pose; the lists stop one short.
		{ "\t\tsys.trigger( $cellblock2_prisoner_6 );\n",
		  "\t\tsys.trigger( $cellblock2_prisoner_6 );\n\t\tsys.trigger( $cellblock2_prisoner_7 );\n" },
		{ "\t\tsys.trigger( $cellblock3_prisoner_6 );\n",
		  "\t\tsys.trigger( $cellblock3_prisoner_6 );\n\t\tsys.trigger( $cellblock3_prisoner_7 );\n" },
		// Tak's radio reminders while Dekard crosses the heat exchanger alone.
		// The authored loop ends when the security console is used (gotSpecs
		// kills it); nothing ever started it.
		{ "\t\tsys.killthread( \"darnellNagHatch\" );\n",
		  "\t\tsys.killthread( \"darnellNagHatch\" );\n"
		  "\t\tif ( !SECROOMSPECS ) {\n\t\t\tthread secRoomNag();\n\t\t}\n" },
		// Tak's death called a failure entity the map does not contain, so the
		// mission went on without the tech who opens its doors. Fail the
		// mission the player is on, under its authored title.
		{ "\tvoid takDies()\n\t{\n\t\tsys.trigger( $item_objectivefailed_3 );\n\t}",
		  "\tvoid takDies()\n\t{\n"
		  "\t\tentity failure;\n"
		  "\t\tif ( openq4TakFailed || openq4TakFailTitle == \"\" ) {\n\t\t\treturn;\n\t\t}\n"
		  "\t\topenq4TakFailed = 1;\n"
		  "\t\tsys.setSpawnArg( \"objectivetitle\", openq4TakFailTitle );\n"
		  "\t\tsys.setSpawnArg( \"inv_objective\", \"1\" );\n"
		  "\t\tfailure = sys.spawn( \"item_objectivefailed\" );\n"
		  "\t\tif ( isValidEntity( failure ) ) {\n\t\t\tsys.trigger( failure );\n\t\t}\n"
		  "\t}" },
		// "We have to find the central control room": the map's objective for
		// it is completed at the control room but was never given.
		{ "\t\taiSpeakWait( $tak, \"lipsync_tak_wehavefindcent\" );\n",
		  "\t\taiSpeakWait( $tak, \"lipsync_tak_wehavefindcent\" );\n"
		  "\t\tsys.trigger( $objective_controlroom );\n" },
		// Markswell's recorded urging, after his last order. He leaves with
		// the squad when Dekard locks the control room behind him.
		{ "\tvoid controlRoomSeq2()\n\t{",
		  "\tvoid openq4MarkswellUrges()\n\t{\n"
		  "\t\tsys.wait( 20 );\n"
		  "\t\tif ( isValidEntity( $markswell ) ) { aiSpeakWait( $markswell, \"lipsync_marks_dekgetgoing\" ); }\n"
		  "\t\tsys.wait( 20 );\n"
		  "\t\tif ( isValidEntity( $markswell ) ) { aiSpeakWait( $markswell, \"lipsync_marks_areyouhard\" ); }\n"
		  "\t\tsys.wait( 20 );\n"
		  "\t\tif ( isValidEntity( $markswell ) ) { aiSpeakWait( $markswell, \"lipsync_marks_holdposition\" ); }\n"
		  "\t}\n\n"
		  "\tvoid controlRoomSeq2()\n\t{" },
		{ "\t\taiSpeakWait( $markswell, \"lipsync_marks_getnow\" );\n",
		  "\t\taiSpeakWait( $markswell, \"lipsync_marks_getnow\" );\n"
		  "\t\tthread openq4MarkswellUrges();\n" },
		// The lift warning belongs to the Viper squad's Mitchell; the control
		// room's Mitchell was removed when Dekard left it, so it never played.
		{ "\t\taiSpeak( $mitchell, \"lipsync_mitch_theresome\" );",
		  "\t\taiSpeak( $mitchell_RS, \"lipsync_mitch_theresome\" );" },
		// Capture the failure title before collecting the objective removes it.
		{ "\tvoid main()\n\t{\n",
		  "\tvoid main()\n\t{\n"
		  "\t\topenq4TakFailTitle = $item_objective_2.getKey( \"objectivetitle\" );\n" },
	};
	// Refuse partial correction of a mapper's revision: a fresh start then
	// keeps the installed behaviour.
	for ( int i = 0; i < (int)( sizeof( corrections ) / sizeof( corrections[0] ) ); ++i ) {
		const int first = source.Find( corrections[i].before );
		if ( first < 0 || source.Find( corrections[i].before, true, first + 1 ) >= 0 ) {
			source.Clear();
			return false;
		}
		source.Replace( corrections[i].before, corrections[i].after );
	}
	return true;
}

static const char *Awakening_M04MissionScriptName( const char *filename ) {
	idStr normalized = filename;
	normalized.BackSlashesToSlashes();
	if ( gameLocal.isMultiplayer || !FS_AwakeningActive() || normalized.Icmp( awakeningM04Source ) != 0 ) {
		return filename;
	}
	idStr source;
	if ( Awakening_LoadM04MissionScript( awakeningM04Version4, source ) ) {
		gameLocal.Printf( "Awakening M04: mission corrections v4 enabled\n" );
		return awakeningM04Version4;
	}
	gameLocal.Warning( "Awakening M04: unrecognized script; keeping installed mission behaviour" );
	return filename;
}
