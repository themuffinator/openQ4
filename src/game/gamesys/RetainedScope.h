// openQ4 additions to the Quake 4 SDK game sources.
// See LICENSES/QUAKE-4-SDK-EULA.rtf and src/game/LICENSE.
#pragma once

// Only the shipped optical overlays have retained counterparts. A mod's
// differently named GUI stays under its author's control. Both backends are
// ordinary GUI instances; restore consumes the serialized instance before a
// subsequent update selects the current preference, preserving save framing.
static void OpenQ4_UpdateScopeGui( idUserInterface *&gui, idStr &requested, const idDict &args ) {
	const char *stock = args.GetString( "gui_zoom" );
	if ( idStr::Icmp( stock, "guis/weapons/machinegun_scope.gui" ) &&
		 idStr::Icmp( stock, "guis/weapons/railgun_scope.gui" ) &&
		 idStr::Icmp( stock, "guis/weapons/nailgun_scope.gui" ) ) {
		return;
	}
	idStr desired = stock;
	if ( cvarSystem->GetCVarBool( "ui_retained" ) ) desired.SetFileExtension( ".rui" );
	if ( !requested.Icmp( desired ) ) return;
	requested = desired; // missing/invalid documents retry only after a gate change
	// A mod may replace a stock-named scope without opting into this UI.
	const char *mods[] = { cvarSystem->GetCVarString( "fs_game" ), cvarSystem->GetCVarString( "fs_game_base" ) };
	for ( int i = 0; i < 2; ++i ) {
		if ( !mods[i][0] || !idStr::Icmp( mods[i], "baseoq4" ) || !idStr::Icmp( mods[i], "q4base" ) || !idStr::Icmp( mods[i], "q4xbase" ) ) continue;
		idFile *custom = fileSystem->OpenFileRead( stock, false, mods[i] );
		if ( custom ) { fileSystem->CloseFile( custom ); return; }
	}
	if ( gui && !idStr::Icmp( gui->Name(), desired ) ) return;
	if ( fileSystem->ReadFile( desired, NULL ) <= 0 ) return;
	idUserInterface *replacement = uiManager->FindGui( desired, true, true );
	if ( !replacement ) return;
	if ( gui ) {
		const idDict &state = gui->State();
		for ( int i = 0; i < state.GetNumKeyVals(); ++i ) {
			const idKeyValue *value = state.GetKeyVal( i );
			if ( value->GetKey().Icmp( "name" ) ) replacement->SetStateString( value->GetKey(), value->GetValue() );
		}
	}
	replacement->StateChanged( gameLocal.time );
	if ( replacement->GetStateFloat( "lockStatus" ) >= 1.0f ) replacement->HandleNamedEvent( "lockAquired" );
	idUserInterface *previous = gui;
	gui = replacement;
	if ( previous && previous->IsUniqued() ) uiManager->DeAlloc( previous );
}
