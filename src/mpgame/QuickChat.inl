// openQ4: the multiplayer quick-chat wheel (ui-visual-design.md 14.6 "Quick
// chat", 13.5). The weapon wheel's component with eight localized phrases in
// place of the weapons: _quickChat holds it open, the stick or the mouse
// selects as on the weapon wheel, the zoom control switches the channel in a
// team game, and releasing over a phrase sends it as a chat line in the
// sender's language. Included after WeaponWheel.inl. Quake 4 SDK EULA.

static const char *QUICK_CHAT_PHRASES[] = {
	"#str_231261", "#str_231262", "#str_231263", "#str_231264", "#str_231265", "#str_231266", "#str_231267", "#str_231268"
};
static const int QUICK_CHAT_COUNT = sizeof( QUICK_CHAT_PHRASES ) / sizeof( QUICK_CHAT_PHRASES[ 0 ] );

void idPlayer::InitQuickChat( void ) {
	quickChatActive = quickChatTeam = quickChatCanceled = quickChatZoomHeld = quickChatDebug = quickChatDebugHeld = false;
	quickChatSelected = -1;
	quickChatOpenedTime = quickChatLastUpdateTime = quickChatLastMouseX = quickChatLastMouseY = 0;
	quickChatBlend = 0;
	quickChatCursor.Zero();
	quickChatLastCmdAngles.Zero();
	quickChatGui = NULL;
	quickChatGuiActive = quickChatGuiFailed = false;
	quickChatGuiSelected = -2;
	quickChatGuiChannel = -1;
}

// The local player's quick chat loads as the player spawns: inside the level
// load on a listen server, where the engine parsed it on a worker meanwhile.
void idPlayer::PrecacheQuickChatGui( void ) {
	if ( quickChatGui || quickChatGuiFailed || gameLocal.localClientNum < 0 || entityNumber != gameLocal.localClientNum ) return;
	if ( !cvarSystem->GetCVarBool( "ui_retained" ) ) return;
	quickChatGui = uiManager->FindGui( "guis/hud/quick_chat.rui", true, true );
	if ( !quickChatGui ) quickChatGuiFailed = true;
}

void idPlayer::FreeQuickChatGui( void ) {
	if ( quickChatGui ) uiManager->DeAlloc( quickChatGui );
	quickChatGui = NULL;
	quickChatGuiActive = quickChatGuiFailed = false;
	quickChatGuiSelected = -2;
	quickChatGuiChannel = -1;
}

float idPlayer::GetQuickChatBlend( void ) const {
	return entityNumber == gameLocal.localClientNum ? quickChatBlend : 0;
}

// The local player chats alive, dead or spectating, but not from a menu, a
// cinematic or the weapon wheel. The wheel is a retained document; the Stock
// UI has none.
bool idPlayer::CanUseQuickChat( void ) {
	return gameLocal.isMultiplayer && entityNumber == gameLocal.localClientNum && !gameLocal.inCinematic && !privateCameraView &&
		!GuiActive() && !weaponWheelActive && cvarSystem->GetCVarBool( "ui_retained" );
}

// While the wheel is held it takes the attack, zoom and scores controls (zoom
// switches its channel); the view stops in UpdateViewAngles. Both ends of the
// wire apply it from the usercmd, so prediction agrees.
void idPlayer::ApplyQuickChatInputMask( void ) {
	if ( ( usercmd.buttons & BUTTON_QUICKCHAT ) == 0 || spectating || health <= 0 ) {
		return;
	}
	const int maskedButtons = BUTTON_ATTACK | BUTTON_ZOOM | BUTTON_SCORES;
	buttonMask |= maskedButtons;
	usercmd.buttons &= ~maskedButtons;
}

void idPlayer::ResetQuickChat( bool instantRestore ) {
	quickChatActive = false;
	quickChatCanceled = ( usercmd.buttons & BUTTON_QUICKCHAT ) != 0;
	quickChatDebug = quickChatDebugHeld = false;
	quickChatSelected = -1;
	quickChatCursor.Zero();
	if ( instantRestore ) quickChatBlend = 0;
	quickChatLastUpdateTime = 0;
	if ( quickChatGui && quickChatGuiActive ) quickChatGui->HandleNamedEvent( "close" );
	quickChatGuiActive = false;
	if ( entityNumber == gameLocal.localClientNum && cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 2 ) {
		cvarSystem->SetCVarInteger( "ui_radialOwner", 0 );
		cvarSystem->SetCVarBool( "ui_radialCancel", false );
	}
}

void idPlayer::UpdateQuickChat( void ) {
	if ( entityNumber != gameLocal.localClientNum ) return;
	if ( quickChatActive && cvarSystem->GetCVarBool( "ui_radialCancel" ) ) {
		ResetQuickChat( false );
		return;
	}
	const bool requested = quickChatDebug ? quickChatDebugHeld : ( usercmd.buttons & BUTTON_QUICKCHAT ) != 0;
	if ( !requested ) quickChatCanceled = false;
	const bool usable = CanUseQuickChat();
	const bool held = usable && requested && !quickChatCanceled;
	const int now = Sys_Milliseconds();
	if ( quickChatLastUpdateTime == 0 ) quickChatLastUpdateTime = now;
	const float dt = MS2SEC( idMath::ClampInt( 0, 100, now - quickChatLastUpdateTime ) );
	quickChatLastUpdateTime = now;
	const bool reducedMotion = cvarSystem->GetCVarBool( "ui_retainedReducedMotion" );
	// The zoom control as pressed, before the wheel masks it.
	const bool zoomDown = ( gameLocal.usercmds[ entityNumber ].buttons & BUTTON_ZOOM ) != 0;

	if ( held && !quickChatActive ) {
		quickChatActive = true;
		quickChatOpenedTime = now;
		quickChatSelected = -1;
		quickChatTeam = gameLocal.IsTeamGame();
		quickChatCursor.Zero();
		quickChatLastMouseX = usercmd.mx;
		quickChatLastMouseY = usercmd.my;
		for ( int axis = 0; axis < 3; ++axis ) {
			quickChatLastCmdAngles[ axis ] = SHORT2ANGLE( usercmd.angles[ axis ] );
		}
		// A zoom already held when the wheel opens does not switch the channel.
		quickChatZoomHeld = zoomDown;
	}

	if ( quickChatActive ) {
		if ( zoomDown && !quickChatZoomHeld && gameLocal.IsTeamGame() ) {
			quickChatTeam = !quickChatTeam;
		}
		if ( !quickChatDebug ) {
			WeaponWheelAdvanceCursor( usercmd, quickChatCursor, quickChatLastMouseX, quickChatLastMouseY, quickChatLastCmdAngles );
		}
		// Past the dead zone the phrase under the cursor's angle, clockwise from
		// the top, with 4 degrees of hysteresis at the boundaries; inside it the
		// selection holds.
		if ( !quickChatDebug && quickChatCursor.Length() > WeaponWheelDeadzoneRadius() ) {
			float angle = atan2f( quickChatCursor.x, -quickChatCursor.y );
			if ( angle < 0.0f ) angle += idMath::TWO_PI;
			const float slice = idMath::TWO_PI / QUICK_CHAT_COUNT;
			int slot = static_cast<int>( angle / slice + 0.5f ) % QUICK_CHAT_COUNT;
			if ( quickChatSelected >= 0 && slot != quickChatSelected &&
				idMath::Fabs( WeaponWheelNormalizeAngle( angle - quickChatSelected * slice ) ) <= slice * 0.5f + DEG2RAD( 4.0f ) ) {
				slot = quickChatSelected;
			}
			quickChatSelected = slot;
		}
	}
	quickChatZoomHeld = zoomDown;

	if ( !held && quickChatActive ) {
		quickChatActive = false;
		// A menu or the weapon wheel taking over closes the wheel unsent.
		if ( usable && quickChatSelected >= 0 && quickChatSelected < QUICK_CHAT_COUNT &&
			( quickChatDebug || quickChatCursor.Length() > WeaponWheelDeadzoneRadius() ) ) {
			// Sent at once and escaped as the chat input sends a line (say
			// reverses the escapes).
			idStr text = common->GetLocalizedString( QUICK_CHAT_PHRASES[ quickChatSelected ] );
			text.Replace( "&", "&amp;" );
			text.Replace( "\\", "&bsl;" );
			idCmdArgs args;
			args.AppendArg( quickChatTeam ? "sayTeam" : "say" );
			args.AppendArg( text.c_str() );
			cmdSystem->BufferCommandArgs( CMD_EXEC_NOW, args );
		} else if ( !usable && requested ) {
			quickChatCanceled = true;
		}
		quickChatCursor.Zero();
	}

	const float blendIn = reducedMotion ? 12.5f : WEAPON_WHEEL_BLEND_IN_SPEED;
	const float blendOut = reducedMotion ? 12.5f : 6.25f;
	quickChatBlend = quickChatActive ? Min( 1.0f, quickChatBlend + dt * blendIn ) : Max( 0.0f, quickChatBlend - dt * blendOut );
	if ( quickChatActive ) cvarSystem->SetCVarInteger( "ui_radialOwner", 2 );
	else if ( cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 2 ) cvarSystem->SetCVarInteger( "ui_radialOwner", 0 );
}

void idPlayer::DrawQuickChat( void ) {
	if ( entityNumber != gameLocal.localClientNum ) return;
	if ( quickChatBlend <= 0.0f || !cvarSystem->GetCVarBool( "ui_retained" ) ) {
		if ( quickChatGui && quickChatGuiActive ) {
			quickChatGui->HandleNamedEvent( "close" );
			quickChatGuiActive = false;
		}
		return;
	}
	if ( !quickChatGui ) {
		if ( quickChatGuiFailed ) return;
		quickChatGui = uiManager->FindGui( "guis/hud/quick_chat.rui", true, true );
		if ( !quickChatGui ) {
			quickChatGuiFailed = true;
			return;
		}
	}
	float offset, fit;
	WeaponWheelPlacement( offset, fit );
	const int channel = quickChatTeam ? 1 : 0;
	quickChatGui->SetStateFloat( "offset", offset );
	quickChatGui->SetStateFloat( "fit", fit );
	quickChatGui->SetStateFloat( "blend", quickChatBlend );
	quickChatGui->SetStateInt( "selected", quickChatSelected );
	quickChatGui->SetStateInt( "team", gameLocal.IsTeamGame() ? team : -1 );
	quickChatGui->SetStateInt( "channel", channel );
	quickChatGui->SetStateString( "zoomKey", common->KeysFromBinding( "_zoom" ) );
	quickChatGui->StateChanged( gameLocal.time );
	const bool justOpened = quickChatActive && !quickChatGuiActive;
	if ( quickChatGuiActive != quickChatActive ) {
		quickChatGui->HandleNamedEvent( quickChatActive ? "open" : "close" );
		quickChatGuiActive = quickChatActive;
	}
	if ( quickChatGuiSelected != quickChatSelected ) {
		if ( quickChatSelected >= 0 && !justOpened ) quickChatGui->HandleNamedEvent( "selection" );
		quickChatGuiSelected = quickChatSelected;
	}
	if ( quickChatGuiChannel != channel ) {
		if ( quickChatGuiChannel >= 0 && !justOpened ) quickChatGui->HandleNamedEvent( "channel" );
		quickChatGuiChannel = channel;
	}
	quickChatGui->Redraw( gameLocal.time );
}

// Semantic developer commands exercise the wheel without host input. Cheat
// protected by the console registry; nothing survives a map or a save.
void idPlayer::DebugQuickChat( const idCmdArgs &args ) {
	const idStr action = args.Argv( 1 );
	if ( action == "open" ) {
		quickChatDebug = quickChatDebugHeld = true;
		quickChatCanceled = false;
		UpdateQuickChat();
	} else if ( action == "slot" && args.Argc() > 2 && quickChatDebug ) {
		quickChatSelected = idMath::ClampInt( -1, QUICK_CHAT_COUNT - 1, atoi( args.Argv( 2 ) ) );
	} else if ( action == "channel" && quickChatActive && gameLocal.IsTeamGame() ) {
		quickChatTeam = !quickChatTeam;
	} else if ( action == "release" && quickChatDebug ) {
		quickChatDebugHeld = false;
		UpdateQuickChat();
		quickChatDebug = false;
	} else if ( action == "cancel" ) {
		ResetQuickChat();
	}
	gameLocal.Printf( "QUICK_CHAT active=%d slot=%d team=%d blend=%.2f retained=%d gui=%s\n", quickChatActive, quickChatSelected,
		quickChatTeam, quickChatBlend, cvarSystem->GetCVarBool( "ui_retained" ), quickChatGui ? quickChatGui->Name() : "none" );
}
