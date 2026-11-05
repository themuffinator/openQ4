/* Independently authored recovered-map repairs. SDK EULA; see LateMissions.inl. */

void Awakening_CorrectLateMissionMedia( idDict &args ) {
	if ( !Awakening_LateCampaignActive() || !g_awakeningLateMissions.GetBool() ) { return; }
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	if ( map.Icmp( "maps/game/m09_valkaryne" ) == 0 ) {
		if ( !idStr::Icmp( args.GetString( "screenShot" ), "gfx/objectives/m09_obj_00.tga" ) &&
			fileSystem->ReadFile( "gfx/objectives/m09_obj_00.tga", NULL, NULL ) < 0 &&
			fileSystem->ReadFile( "gfx/objectives/m09_obj_01.tga", NULL, NULL ) > 0 ) {
			args.Set( "screenShot", "gfx/objectives/m09_obj_01.tga" );
		}
		return;
	}
	if ( map.Icmp( "maps/game/m08_cryofac" ) || idStr::Icmp( args.GetString( "classname" ), "func_static" ) ) { return; }
	const idStr model = args.GetString( "model" );
	if ( model.Icmp( "models/mapobjects/marine/equipment/tables/table1_low.lwo" ) &&
		model.Icmp( "models/mapobjects/strogg/misc_equipment/table_wheels03_lo.lwo" ) &&
		model.Icmp( "models/mapobjects/strogg/misc_equipment/bonesaw01_lo.lwo" ) &&
		model.Icmp( "models/mapobjects/strogg/misc_equipment/pincers01_lo.lwo" ) &&
		model.Icmp( "models/mapobjects/strogg/misc_equipment/cannister01_lo_temp.lwo" ) ) { return; }
	if ( fileSystem->ReadFile( model, NULL, NULL ) >= 0 ) { return; }
	// The surviving retail variants retain the prop's role. Never replace an
	// installed original. Missing small surgical tools have no equivalent:
	// retain their named entities but omit the default error cubes/collision.
	const char *replacement = NULL;
	if ( !model.Icmp( "models/mapobjects/marine/equipment/tables/table1_low.lwo" ) ) {
		replacement = "models/mapobjects/marine/equipment/tables/table2_low.lwo";
	} else if ( !model.Icmp( "models/mapobjects/strogg/misc_equipment/table_wheels03_lo.lwo" ) ) {
		replacement = "models/mapobjects/strogg/misc_equipment/table_wheels02_lo.lwo";
	} else if ( !model.Icmp( "models/mapobjects/strogg/misc_equipment/bonesaw01_lo.lwo" ) ||
		!model.Icmp( "models/mapobjects/strogg/misc_equipment/pincers01_lo.lwo" ) ||
		!model.Icmp( "models/mapobjects/strogg/misc_equipment/cannister01_lo_temp.lwo" ) ) {
		args.Delete( "model" );
		args.SetBool( "solid", false );
		args.SetBool( "hide", true );
	}
	if ( replacement && fileSystem->ReadFile( replacement, NULL, NULL ) > 0 ) {
		args.Set( "model", replacement );
	}
}

void Awakening_StartLateMissionDetails() {
	if ( !Awakening_LateCampaignActive() || !gameLocal.program.FindFunction( "oq4_m09_v4::main" ) ||
		idStr::Icmp( gameLocal.GetMapName(), "maps/game/m09_valkaryne" ) ) { return; }
	struct sealPanel_t { const char *name; const char *door; const char *team; const char *origin; const char *angle; const char *callback; };
	static const sealPanel_t panels[] = {
		{ "exitdooronepanel", "func_door_144", "tunneldoors1", "-3432 -2032 -4960", "270", "exitDoorOneDisable" },
		{ "exitdoortwopanel", "func_door_145", "tunneldoors2", "-4048 -2488 -4960", "180", "exitDoorTwoDisable" },
		{ "exitdoorthreepanel", "func_door_147", "tunneldoors3", "-4328 -2928 -4960", "270", "exitDoorThreeDisable" }
	};
	for ( int i = 0; i < 3; ++i ) {
		const sealPanel_t &panel = panels[i];
		idEntity *door = gameLocal.FindEntity( panel.door );
		// Same-name custom maps and already supplied panels own their content.
		if ( gameLocal.FindEntity( panel.name ) || !door || !door->IsType( idDoor::Type ) ||
			idStr::Icmp( door->spawnArgs.GetString( "team" ), panel.team ) || !door->spawnArgs.GetBool( "start_open" ) ) { continue; }
		const idStr relayName = va( "openq4_%s_relay", panel.name );
		if ( gameLocal.FindEntity( relayName ) ) { continue; }
		idDict args;
		args.Set( "classname", "trigger_relay" );
		args.Set( "name", relayName );
		args.Set( "call", va( "oq4_m09_v4::%s", panel.callback ) );
		args.Set( "origin", panel.origin );
		if ( !gameLocal.SpawnEntityDef( args ) ) { continue; }
		args.Clear();
		args.Set( "classname", "func_static" );
		args.Set( "name", panel.name );
		args.Set( "origin", panel.origin );
		args.Set( "angle", panel.angle );
		args.Set( "model", "models/mapobjects/strogg/monitors/gui_array/bottom_monitor.lwo" );
		args.Set( "gui", "guis/common/strogg/generic_seal.gui" );
		args.Set( "gui_parm_strogg", "1" );
		args.Set( "target", relayName );
		args.SetBool( "solid", false );
		gameLocal.SpawnEntityDef( args );
	}
}
