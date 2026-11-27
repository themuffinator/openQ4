// Shared SP/MP HUD presentation. Quake 4 SDK EULA; see src/game/LICENSE.
//
// The engine presents the stock HUD GUI: with the Remastered layout its
// paired document (src/ui/RetainedHud.cpp) draws in place of the stock
// windows, from the same state and named events; Classic draws the stock GUI.

void idPlayer::InitStatusBar() {}

void idPlayer::FreeStatusBar() {}

void idPlayer::DrawHudWithStatusBar( idUserInterface *legacy ) {
	legacy->Redraw( gameLocal.realClientTime );
}

void idPlayer::DebugStatusBar( const idCmdArgs &args ) {
	if ( args.Argc() == 4 && !idStr::Icmp( args.Argv( 1 ), "stats" ) ) {
		health = idMath::ClampInt( 1, 9999, atoi( args.Argv( 2 ) ) );
		inventory.armor = idMath::ClampInt( 0, 9999, atoi( args.Argv( 3 ) ) );
	}
	if ( args.Argc() == 3 && !idStr::Icmp( args.Argv( 1 ), "consume" ) && weapon ) weapon->UseAmmo( idMath::ClampInt( 0, 100, atoi( args.Argv( 2 ) ) ) );
	// boss [shield percent [warn]]: a boss battle against the player itself,
	// so the boss bar can be exercised without a scripted encounter. It goes
	// to the HUD on screen: a vehicle's while driving.
	idUserInterface *shown = GetHud();
	if ( args.Argc() >= 2 && !idStr::Icmp( args.Argv( 1 ), "boss" ) && shown ) {
		StartBossBattle( this );
		if ( args.Argc() >= 3 ) {
			shown->SetStateFloat( "boss_shield_percent", idMath::ClampFloat( 0.0f, 1.0f, atof( args.Argv( 2 ) ) / 100.0f ) );
			shown->HandleNamedEvent( args.Argc() >= 4 ? "showBossShieldWarn" : "showBossShieldBar" );
		}
	}
	// pickup <entityDef>: the pickup line an item gives, on the HUD and the
	// wrist (VR-09); objective <title> [complete]: an objective notice
	if ( args.Argc() == 3 && !idStr::Icmp( args.Argv( 1 ), "pickup" ) ) {
		const idDict *item = gameLocal.FindEntityDefDict( args.Argv( 2 ), false );
		if ( item != NULL ) {
			const char *name = common->GetLocalizedString( item->GetString( "inv_name" ) );
			if ( hud ) {
				hud->SetStateString( "itemtext", name );
				hud->SetStateString( "itemicon", item->GetString( "inv_icon" ) );
				hud->HandleNamedEvent( "itemPickup" );
			}
			VRWrist_NotePickup( name, item->GetString( "inv_icon" ) );
		}
	}
	if ( args.Argc() >= 3 && !idStr::Icmp( args.Argv( 1 ), "objective" ) ) {
		VRWrist_NoteObjective( common->GetLocalizedString( args.Argv( 2 ) ), args.Argc() >= 4 && !idStr::Icmp( args.Argv( 3 ), "complete" ) );
	}
	idStr remastered;
	const bool active = hud && hud->GetPresentationValue( "__remastered::active", remastered ) && remastered == "1";
	gameLocal.Printf( "STATUS_BAR gui=%s layout=%s health=%d armor=%d ammo=%d clip=%d maximum=%d\n",
		hud ? hud->Name() : "none", active ? "remastered" : "stock", health, inventory.armor,
		weapon ? weapon->AmmoAvailable() : -1, weapon ? weapon->AmmoInClip() : -1, weapon ? weapon->maxAmmo : 0 );
}
