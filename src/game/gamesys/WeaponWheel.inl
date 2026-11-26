// Shared SP/MP wheel selection and native presentation. Quake 4 SDK EULA.

// Upper-case UTF-8 text for the wheel's weapon name. idStr::ToUpper maps single
// bytes through a Windows-1252 table, which turns the second byte of a
// two-byte letter into another character (Spanish "o" with an acute read as
// "a" with a tilde) and breaks longer sequences. This covers ASCII and the
// Latin-1, Latin Extended-A, Greek and Cyrillic letters whose capital encodes
// to the same two bytes; anything else passes through unchanged.
static void WeaponWheel_UpperUtf8( idStr &text ) {
	const int length = text.Length();
	for ( int i = 0; i < length; ) {
		const unsigned char lead = static_cast<unsigned char>( text[i] );
		if ( lead < 0x80 ) {
			if ( lead >= 'a' && lead <= 'z' ) {
				text[i] = static_cast<char>( lead - 32 );
			}
			i++;
			continue;
		}
		const int size = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
		if ( size == 2 && i + 1 < length ) {
			const int code = ( ( lead & 0x1F ) << 6 ) | ( static_cast<unsigned char>( text[i + 1] ) & 0x3F );
			int upper = code;
			if ( ( code >= 0xE0 && code <= 0xFE && code != 0xF7 ) || ( code >= 0x3B1 && code <= 0x3C9 && code != 0x3C2 ) ||
					( code >= 0x430 && code <= 0x44F ) ) {
				upper = code - 0x20;
			} else if ( code >= 0x450 && code <= 0x45F ) {
				upper = code - 0x50;
			} else if ( ( ( code >= 0x100 && code <= 0x137 ) || ( code >= 0x14A && code <= 0x177 ) ) && code != 0x131 ) {
				upper = code & ~1;
			} else if ( ( code >= 0x139 && code <= 0x148 ) || ( code >= 0x179 && code <= 0x17E ) ) {
				upper = ( code & 1 ) ? code : code - 1;
			}
			if ( upper != code ) {
				text[i] = static_cast<char>( 0xC0 | ( upper >> 6 ) );
				text[i + 1] = static_cast<char>( 0x80 | ( upper & 0x3F ) );
			}
		}
		i += size;
	}
}

// The documents' ring, 104 to 232 dp, holds the selector's whole travel: its
// bound of WEAPON_WHEEL_CURSOR_RADIUS lands on the plates' outer edge.
const float WEAPON_WHEEL_RETAINED_RIM = 232.0f;

// The selector never leaves the wheel. A push past the circumference holds it
// on the rim, where further motion slides it round the ring, so reversing
// the mouse starts back at once instead of first unwinding the overshoot.
static void WeaponWheelBoundCursor( idVec2 &cursor ) {
	const float cursorLength = cursor.Length();
	if ( cursorLength > WEAPON_WHEEL_CURSOR_RADIUS ) {
		cursor *= ( WEAPON_WHEEL_CURSOR_RADIUS / cursorLength );
	}
}

// The selector shows as a dot while a mouse steers it. A stick or a finger
// points the wheel by direction, and a tracked hand by the aim; those keep
// the pointer on the hub alone.
static bool WeaponWheelPointerShown( const usercmd_t &cmd ) {
	return cvarSystem->GetCVarBool( "ui_retained" ) && !cvarSystem->GetCVarBool( "ui_radialStickActive" ) &&
		!cvarSystem->GetCVarBool( "ui_radialTouchActive" ) && ( cmd.buttons & BUTTON_VRAIM ) == 0;
}

static float WeaponWheelDeadzoneRadius() {
	if ( !cvarSystem->GetCVarBool( "ui_retained" ) ) return WEAPON_WHEEL_DEADZONE_RADIUS;
	if ( cvarSystem->GetCVarBool( "ui_radialTouchActive" ) ) return 32.0f;
	return cvarSystem->GetCVarBool( "ui_radialStickActive" ) ? WEAPON_WHEEL_CURSOR_RADIUS * 0.35f : 32.0f;
}

void idPlayer::InitWeaponWheel() {
	weaponWheelActive = weaponWheelGuiActive = weaponWheelCanceled = weaponWheelDebug = weaponWheelDebugHeld = false;
	weaponWheelHoveredSlot = weaponWheelGuiSlot = -1;
	weaponWheelLastMouseX = weaponWheelLastMouseY = weaponWheelLastUpdateTime = weaponWheelOpenedTime = 0;
	weaponWheelBlend = 0;
	weaponWheelCursor.Zero(); weaponWheelLastCmdAngles.Zero();
	weaponWheelGui = NULL;
}

idStr idPlayer::RetainedWeaponWheelPath() const {
	const char *family = isStrogg && !gameLocal.isMultiplayer ? "strogg" : "marine";
	return va( "guis/hud/weapon_wheel_%s%s.rui", family, GetWeaponWheelSlotCount() == 13 ? "_awakening" : "" );
}

// The local player's wheel loads as the player spawns: inside the level load
// in single player and on a listen server, where the engine parsed it on a
// worker meanwhile, so its first opening builds nothing.
void idPlayer::PrecacheWeaponWheelGui() {
	if ( weaponWheelGui || gameLocal.localClientNum < 0 || entityNumber != gameLocal.localClientNum ) return;
	if ( !cvarSystem->GetCVarBool( "ui_retained" ) ) return;
	weaponWheelGui = uiManager->FindGui( RetainedWeaponWheelPath(), true, true );
	if ( !weaponWheelGui ) weaponWheelGuiSlot = -2;
}

void idPlayer::FreeWeaponWheelGui() {
	if ( weaponWheelGui ) uiManager->DeAlloc( weaponWheelGui );
	weaponWheelGui = NULL;
	weaponWheelGuiActive = false;
	weaponWheelGuiSlot = -1;
}

int idPlayer::GetWeaponWheelSlotCount() const {
	if ( !cvarSystem->GetCVarBool( "ui_retained" ) ) return WEAPON_WHEEL_SLOT_COUNT;
	// q4xbase adds core, napalm and freeze in slots 10..12. MP keeps its
	// own eleven-slot roster, irrespective of the campaign selected in SP.
	return !gameLocal.isMultiplayer && spawnArgs.GetString( "def_weapon12" )[0] ? 13 : 11;
}

// The wheels' placement in their 720 dp canvas: ui_weaponWheelOffset, kept
// inside a narrow view without changing the archived preference.
static void WeaponWheelPlacement( float &offset, float &fit ) {
	const float width = 720.0f * gameLocal.GetScreenAspectRatio();
	const float inset = idMath::ClampFloat( 0.0f, 10.0f, cvarSystem->GetCVarFloat( "ui_hudSafeInset" ) ) * 0.01f;
	const float marginX = Max( 16.0f, width * inset );
	const float marginY = Max( 16.0f, 720.0f * inset );
	const float scale = idMath::ClampFloat( 0.5f, 2.0f, cvarSystem->GetCVarFloat( "ui_hudScale" ) );
	fit = Min( scale, Max( 0.1f, Min( ( width - 2.0f * marginX ) / 560.0f, ( 720.0f - 2.0f * marginY ) / 560.0f ) ) );
	const float reach = Max( 0.0f, width * 0.5f - 264.0f * fit - marginX );
	const float requested = width * idMath::ClampFloat( -40.0f, 40.0f, cvarSystem->GetCVarFloat( "ui_weaponWheelOffset" ) ) / 100.0f;
	offset = idMath::ClampFloat( -reach, reach, requested );
}

bool idPlayer::DrawRetainedWeaponWheel() {
	if ( !cvarSystem->GetCVarBool( "ui_retained" ) ) {
		FreeWeaponWheelGui();
		return false;
	}
	const idStr path = RetainedWeaponWheelPath();
	if ( weaponWheelGui && idStr::Icmp( weaponWheelGui->Name(), path ) ) FreeWeaponWheelGui();
	if ( !weaponWheelGui ) {
		if ( weaponWheelGuiSlot == -2 ) return false; // failed load: retry on next open or gate change
		weaponWheelGui = uiManager->FindGui( path, true, true );
		if ( !weaponWheelGui ) { weaponWheelGuiSlot = -2; return false; }
	}
	// Work in the document's isotropic 720 dp canvas.
	float offset, fit;
	WeaponWheelPlacement( offset, fit );
	weaponWheelGui->SetStateFloat( "offset", offset );
	weaponWheelGui->SetStateFloat( "fit", fit );
	weaponWheelGui->SetStateFloat( "blend", weaponWheelBlend );
	weaponWheelGui->SetStateInt( "selected", Max( 0, weaponWheelHoveredSlot ) );
	weaponWheelGui->SetStateInt( "previous", GetWeaponWheelSlotForWeapon( previousWeapon ) );
	weaponWheelGui->SetStateBool( "mp", gameLocal.isMultiplayer );
	weaponWheelGui->SetStateInt( "team", gameLocal.isMultiplayer && gameLocal.IsTeamGame() ? team : -1 );
	// The mouse's selector in the wheel's dp, bounded by its rim.
	const float rim = WEAPON_WHEEL_RETAINED_RIM / WEAPON_WHEEL_CURSOR_RADIUS;
	weaponWheelGui->SetStateFloat( "cursorX", weaponWheelCursor.x * rim );
	weaponWheelGui->SetStateFloat( "cursorY", weaponWheelCursor.y * rim );
	weaponWheelGui->SetStateBool( "cursorShown", weaponWheelActive && WeaponWheelPointerShown( usercmd ) );
	for ( int slot = 0; slot < GetWeaponWheelSlotCount(); ++slot ) {
		weaponWheelGui->SetStateString( va( "slot%d.index", slot ), va( "%d", slot + 1 ) );
		const bool owned = HasWeaponWheelSlotWeapon( slot );
		const int count = GetWeaponWheelAmmoCount( slot );
		const idDeclEntityDef *decl = GetWeaponDef( slot );
		const int ammoType = decl ? inventory.AmmoIndexForWeaponClass( spawnArgs.GetString( va( "def_weapon%d", slot ) ) ) : 0;
		const int maximum = ammoType > 0 ? inventory.MaxAmmoForAmmoClass( this, rvWeapon::GetAmmoNameForIndex( ammoType ) ) : 0;
		const int total = ammoType > 0 ? inventory.ammo[ammoType] : -1;
		const int clip = gameLocal.isMultiplayer ? 0 : Max( 0, currentWeapon == slot && weapon ? weapon->AmmoInClip() : inventory.clip[slot] );
		const int reserve = total < 0 ? -1 : Max( 0, total - clip );
		weaponWheelGui->SetStateBool( va( "slot%d.owned", slot ), owned );
		weaponWheelGui->SetStateInt( va( "slot%d.count", slot ), count );
		weaponWheelGui->SetStateString( va( "slot%d.reserve", slot ), reserve < 0 ? "" : va( "%d", reserve ) );
		weaponWheelGui->SetStateFloat( va( "slot%d.fill", slot ), maximum > 0 ? idMath::ClampFloat( 0, 1, static_cast<float>( reserve ) / maximum ) : 0 );
		if ( slot == weaponWheelHoveredSlot ) {
			idStr name = owned && decl ? common->GetLocalizedString( decl->dict.GetString( "inv_name" ) ) : "";
			WeaponWheel_UpperUtf8( name );
			weaponWheelGui->SetStateString( "weaponName", name );
			idStr readout;
			if ( owned && total < 0 ) {
				readout = "\xE2\x88\x9E"; // infinity; the native document draws its vector glyph
			} else if ( owned ) {
				if ( !gameLocal.isMultiplayer && decl && decl->dict.GetInt( "clipSize" ) > 0 ) readout = va( "%d / %d", clip, reserve );
				else readout = va( "%d", reserve );
			}
			weaponWheelGui->SetStateString( "ammo", readout );
		}
	}
	weaponWheelGui->StateChanged( gameLocal.time );
	const bool justOpened = weaponWheelActive && !weaponWheelGuiActive;
	if ( weaponWheelGuiActive != weaponWheelActive ) {
		weaponWheelGui->HandleNamedEvent( weaponWheelActive ? "open" : "close" );
		weaponWheelGuiActive = weaponWheelActive;
	}
	if ( weaponWheelGuiSlot != weaponWheelHoveredSlot ) {
		// Opening already tunes/translates this name. A selection event in the
		// same frame would replace the family's full opening choreography.
		if ( !justOpened ) weaponWheelGui->HandleNamedEvent( "selection" );
		weaponWheelGuiSlot = weaponWheelHoveredSlot;
	}
	weaponWheelGui->Redraw( gameLocal.time );
	return true;
}

float idPlayer::GetWeaponWheelBlend() const {
	if ( entityNumber != gameLocal.localClientNum ) return 0;
	// The quick-chat wheel softens the scene as the weapon wheel does (it only
	// opens in network play, so it stays 0 in the campaign).
	return Max( weaponWheelBlend, quickChatBlend );
}

void idPlayer::DrawWeaponWheel() {
	if ( weaponWheelBlend <= 0 || entityNumber != gameLocal.localClientNum ) return;
	if ( !DrawRetainedWeaponWheel() ) DrawLegacyWeaponWheel();
}

// Semantic developer commands exercise player selection, not host input. They
// are cheat protected by the console registry and cannot survive a map/save.
void idPlayer::DebugWeaponWheel( const idCmdArgs &args ) {
	const idStr action = args.Argv( 1 );
	if ( action == "open" ) {
		weaponWheelDebug = weaponWheelDebugHeld = true;
		weaponWheelCanceled = false;
		UpdateWeaponWheel();
	} else if ( action == "slot" && args.Argc() > 2 && weaponWheelDebug ) {
		weaponWheelHoveredSlot = idMath::ClampInt( 0, GetWeaponWheelSlotCount() - 1, atoi( args.Argv( 2 ) ) );
	} else if ( action == "cursor" && args.Argc() > 3 && weaponWheelDebug ) {
		// The mouse's selector at a point, in its own units (rim 118), and the
		// slot it selects there, as UpdateWeaponWheel would choose it.
		weaponWheelCursor.Set( static_cast<float>( atof( args.Argv( 2 ) ) ), static_cast<float>( atof( args.Argv( 3 ) ) ) );
		WeaponWheelBoundCursor( weaponWheelCursor );
		if ( weaponWheelCursor.Length() > WeaponWheelDeadzoneRadius() ) {
			float angle = atan2f( weaponWheelCursor.x, -weaponWheelCursor.y );
			if ( angle < 0.0f ) angle += idMath::TWO_PI;
			weaponWheelHoveredSlot = GetClosestAvailableWeaponWheelSlot( angle );
		}
	} else if ( action == "step" && args.Argc() > 2 && weaponWheelDebug ) {
		// A weapon control while the wheel is open, by impulse number.
		HandleWeaponWheelImpulse( atoi( args.Argv( 2 ) ) );
	} else if ( action == "release" && weaponWheelDebug ) {
		weaponWheelDebugHeld = false; UpdateWeaponWheel(); weaponWheelDebug = false;
	} else if ( action == "cancel" ) {
		ResetWeaponWheel( true );
	} else if ( action == "empty" && args.Argc() > 2 ) {
		const int slot = idMath::ClampInt( 0, GetWeaponWheelSlotCount() - 1, atoi( args.Argv( 2 ) ) );
		const int ammo = inventory.AmmoIndexForWeaponClass( spawnArgs.GetString( va( "def_weapon%d", slot ) ) );
		if ( ammo > 0 ) inventory.ammo[ammo] = 0;
		inventory.clip[slot] = 0;
	} else if ( action == "family" && !gameLocal.isMultiplayer && args.Argc() > 2 ) {
		isStrogg = !idStr::Icmp( args.Argv( 2 ), "strogg" );
	}
	gameLocal.Printf( "WEAPON_WHEEL active=%d slot=%d slots=%d current=%d ideal=%d retained=%d offset=%d timescale=%.3f gui=%s\n",
		weaponWheelActive, weaponWheelHoveredSlot, GetWeaponWheelSlotCount(), currentWeapon, idealWeapon,
		cvarSystem->GetCVarBool( "ui_retained" ), cvarSystem->GetCVarInteger( "ui_weaponWheelOffset" ), common->GetGameTimeScale(),
		weaponWheelGui ? weaponWheelGui->Name() : "none" );
}

bool idPlayer::CanUseWeaponWheel( void ) {
	return entityNumber == gameLocal.localClientNum &&
		!spectating &&
		health > 0 &&
		!pfl.dead &&
		weaponEnabled &&
		!gameLocal.inCinematic &&
		!privateCameraView &&
		gameLocal.GetCamera() == NULL &&
		!GuiActive() &&
		!objectiveSystemOpen &&
		!vehicleController.IsDriving();
}

void idPlayer::ApplyWeaponWheelInputMask( void ) {
	if ( !weaponEnabled || spectating || health <= 0 || gameLocal.inCinematic || privateCameraView || gameLocal.GetCamera() || objectiveSystemOpen || vehicleController.IsDriving() ) {
		return;
	}

	if ( ( usercmd.buttons & BUTTON_WEAPONWHEEL ) == 0 ) {
		return;
	}

	const int maskedButtons = BUTTON_ATTACK | BUTTON_ZOOM | BUTTON_SCORES;
	buttonMask |= maskedButtons;
	usercmd.buttons &= ~maskedButtons;
	if ( !gameLocal.isMultiplayer ) {
		usercmd.forwardmove = 0; usercmd.rightmove = 0; usercmd.upmove = 0;
	}
}

void idPlayer::ResetWeaponWheel( bool instantRestore ) {
	weaponWheelActive = false;
	weaponWheelCanceled = ( usercmd.buttons & BUTTON_WEAPONWHEEL ) != 0;
	weaponWheelDebug = false;
	weaponWheelHoveredSlot = GetDefaultWeaponWheelSlot();
	weaponWheelCursor.Zero();
	weaponWheelLastMouseX = usercmd.mx;
	weaponWheelLastMouseY = usercmd.my;
	weaponWheelLastCmdAngles[ PITCH ] = SHORT2ANGLE( usercmd.angles[ PITCH ] );
	weaponWheelLastCmdAngles[ YAW ] = SHORT2ANGLE( usercmd.angles[ YAW ] );
	weaponWheelLastCmdAngles[ ROLL ] = SHORT2ANGLE( usercmd.angles[ ROLL ] );
	weaponWheelLastUpdateTime = Sys_Milliseconds();
	if ( entityNumber == gameLocal.localClientNum && cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 1 ) {
		cvarSystem->SetCVarInteger( "ui_radialOwner", 0 );
		cvarSystem->SetCVarBool( "ui_radialCancel", false );
	}

	if ( instantRestore ) {
		weaponWheelBlend = 0.0f;
		weaponWheelGuiActive = false;
		if ( weaponWheelGui ) weaponWheelGui->HandleNamedEvent( "close" );
		// the multiplayer library had no game-side blur to clear, even outside a map
		if ( !gameLocal.IsMultiplayerMode() ) {
			gameLocal.SetSpecialEffect( SPECIAL_EFFECT_BLUR, false );
		}
		if ( !gameLocal.isMultiplayer ) {
			common->SetGameTimeScale( 1.0f );
		}
	}
}

int idPlayer::GetWeaponWheelSlotForWeapon( int weaponIndex ) const {
	for ( int i = 0; i < GetWeaponWheelSlotCount(); ++i ) {
		if ( GetWeaponWheelWeaponForSlot( i ) == weaponIndex ) {
			return i;
		}
	}

	return -1;
}

int idPlayer::GetWeaponWheelWeaponForSlot( int slot ) const {
	if ( slot < 0 || slot >= GetWeaponWheelSlotCount() ) {
		return -1;
	}

	return cvarSystem->GetCVarBool( "ui_retained" ) ? slot : weaponWheelSlotWeapons[ slot ];
}

bool idPlayer::HasWeaponWheelSlotWeapon( int slot ) const {
	const int weaponIndex = GetWeaponWheelWeaponForSlot( slot );
	return weaponIndex >= 0 && weaponIndex < MAX_WEAPONS && ( inventory.weapons & ( 1 << weaponIndex ) ) != 0;
}

int idPlayer::GetDefaultWeaponWheelSlot( void ) const {
	const int currentSlot = GetWeaponWheelSlotForWeapon( currentWeapon );
	if ( HasWeaponWheelSlotWeapon( currentSlot ) ) {
		return currentSlot;
	}

	for ( int slot = 0; slot < GetWeaponWheelSlotCount(); ++slot ) {
		if ( HasWeaponWheelSlotWeapon( slot ) ) {
			return slot;
		}
	}

	return -1;
}

int idPlayer::GetClosestAvailableWeaponWheelSlot( float angle ) const {
	const float slice = idMath::TWO_PI / GetWeaponWheelSlotCount();
	const int slot = static_cast<int>( angle / slice + 0.5f ) % GetWeaponWheelSlotCount();
	// Fixed slots must not redirect the pointer to a neighbouring weapon.
	if ( !HasWeaponWheelSlotWeapon( slot ) ) return weaponWheelHoveredSlot;
	if ( HasWeaponWheelSlotWeapon( weaponWheelHoveredSlot ) ) {
		const float previousDelta = idMath::Fabs( WeaponWheelNormalizeAngle( angle - weaponWheelHoveredSlot * slice ) );
		if ( previousDelta <= slice * 0.5f + DEG2RAD( 4.0f ) ) return weaponWheelHoveredSlot;
	}
	return slot;
}

int idPlayer::GetWeaponWheelAmmoCount( int weaponIndex ) {
	if ( weaponIndex < 0 || weaponIndex >= MAX_WEAPONS ) {
		return -1;
	}

	const char* weaponName = spawnArgs.GetString( va( "def_weapon%d", weaponIndex ) );
	if ( weaponName == NULL || weaponName[ 0 ] == '\0' ) {
		return -1;
	}

	int ammoRequired = 0;
	const int ammoIndex = inventory.AmmoIndexForWeaponClass( weaponName, &ammoRequired );
	if ( ammoIndex <= 0 || ammoRequired <= 0 ) {
		return -1;
	}

	return inventory.HasAmmo( ammoIndex, ammoRequired );
}

bool idPlayer::IsWeaponWheelSlotLowAmmo( int slot ) {
	const int weaponIndex = GetWeaponWheelWeaponForSlot( slot );
	if ( weaponIndex < 0 || weaponIndex >= MAX_WEAPONS ) {
		return false;
	}

	if ( ( inventory.weapons & ( 1 << weaponIndex ) ) == 0 ) {
		return false;
	}

	const idDeclEntityDef* weaponDecl = GetWeaponDef( weaponIndex );
	if ( weaponDecl == NULL ) {
		return false;
	}

	const int ammoCount = GetWeaponWheelAmmoCount( weaponIndex );
	if ( ammoCount <= 0 ) {
		return false;
	}

	int lowAmmo = weaponDecl->dict.GetInt( "lowAmmo", "0" );
	if ( lowAmmo <= 0 ) {
		lowAmmo = 2;
	}

	return ammoCount <= lowAmmo;
}

// The native selector uses the actual stick direction. Neutral holds the
// weapon selection but returns the cursor to the dead zone, which quick chat
// also uses to cancel on release. Classic retains its cursor sensitivity.
// openQ4 VR: a tracked aim (BUTTON_VRAIM) steers either selector by turning
// the weapon hand, or, without moving the gun off its target, by pushing the
// weapon-hand stick towards a slot (the engine hands that push over as
// vr_wheelStickX/Y only while a selector is open). A push past the stick's
// own threshold points the selector the way a gamepad's does; let go, and the
// hand steers on from there.
static bool WeaponWheelVRStick( idVec2 &stick ) {
	stick.x = cvarSystem->GetCVarFloat( "vr_wheelStickX" );
	stick.y = cvarSystem->GetCVarFloat( "vr_wheelStickY" );
	return stick.Length() >= 0.35f;
}

static void WeaponWheelAdvanceCursor( const usercmd_t &cmd, idVec2 &cursor, int &lastMouseX, int &lastMouseY, idAngles &lastAngles ) {
	const int mouseDx = cmd.mx - lastMouseX;
	const int mouseDy = cmd.my - lastMouseY;
	idAngles currentCmdAngles;
	currentCmdAngles[ PITCH ] = SHORT2ANGLE( cmd.angles[ PITCH ] );
	currentCmdAngles[ YAW ] = SHORT2ANGLE( cmd.angles[ YAW ] );
	currentCmdAngles[ ROLL ] = SHORT2ANGLE( cmd.angles[ ROLL ] );
	const float lookDx = idMath::AngleNormalize180( currentCmdAngles[ YAW ] - lastAngles[ YAW ] );
	const float lookDy = idMath::AngleNormalize180( currentCmdAngles[ PITCH ] - lastAngles[ PITCH ] );
	const bool trackedAim = ( cmd.buttons & BUTTON_VRAIM ) != 0;

	lastMouseX = cmd.mx;
	lastMouseY = cmd.my;
	lastAngles = currentCmdAngles;

	cursor.x += mouseDx * WEAPON_WHEEL_MOUSE_SENSITIVITY;
	cursor.y += mouseDy * WEAPON_WHEEL_MOUSE_SENSITIVITY;
	idVec2 vrStick;
	if ( trackedAim && WeaponWheelVRStick( vrStick ) ) {
		cursor.x = vrStick.x * WEAPON_WHEEL_CURSOR_RADIUS;
		cursor.y = vrStick.y * WEAPON_WHEEL_CURSOR_RADIUS;
	} else if ( cvarSystem->GetCVarBool( "ui_radialTouchActive" ) ) {
		cursor.x = cvarSystem->GetCVarFloat( "ui_radialTouchX" ) * WEAPON_WHEEL_CURSOR_RADIUS;
		cursor.y = cvarSystem->GetCVarFloat( "ui_radialTouchY" ) * WEAPON_WHEEL_CURSOR_RADIUS;
	} else if ( !trackedAim && mouseDx == 0 && mouseDy == 0 && cvarSystem->GetCVarBool( "ui_retained" ) && cvarSystem->GetCVarBool( "ui_radialStickActive" ) ) {
		cursor.x = cvarSystem->GetCVarFloat( "ui_radialStickX" ) * WEAPON_WHEEL_CURSOR_RADIUS;
		cursor.y = cvarSystem->GetCVarFloat( "ui_radialStickY" ) * WEAPON_WHEEL_CURSOR_RADIUS;
	} else if ( mouseDx == 0 && mouseDy == 0 && ( trackedAim || !cvarSystem->GetCVarBool( "ui_retained" ) ) ) {
		const float sensitivity = idMath::ClampFloat( 0.25f, 16.0f, cvarSystem->GetCVarFloat( "in_weaponWheelSensitivity" ) );
		// Turning right decreases yaw; wheel coordinates increase to the right.
		cursor.x -= lookDx * sensitivity;
		cursor.y += lookDy * sensitivity;
	}

	WeaponWheelBoundCursor( cursor );
}

void idPlayer::UpdateWeaponWheelCursor( void ) {
	WeaponWheelAdvanceCursor( usercmd, weaponWheelCursor, weaponWheelLastMouseX, weaponWheelLastMouseY, weaponWheelLastCmdAngles );
}

// While the wheel is open the weapon controls choose on it instead of
// switching at once, so keyboard, mouse wheel and D-pad players have the
// wheel too: a weapon key picks its slot, next and previous weapon step to
// the next owned slot clockwise or back, and last weapon picks the previous
// weapon. Releasing the wheel equips the choice as ever. A control for a
// weapon the player does not have is used up and leaves the choice. The
// local player's impulses alone come here, before PerformImpulse sends any.
bool idPlayer::HandleWeaponWheelImpulse( int impulse ) {
	if ( !weaponWheelActive || entityNumber != gameLocal.localClientNum ) return false;
	const int count = GetWeaponWheelSlotCount();
	int slot = -1;
	if ( impulse >= IMPULSE_0 && impulse <= IMPULSE_12 ) {
		slot = GetWeaponWheelSlotForWeapon( impulse - IMPULSE_0 );
	} else if ( impulse == IMPULSE_51 ) {
		slot = GetWeaponWheelSlotForWeapon( previousWeapon );
	} else if ( impulse == IMPULSE_14 || impulse == IMPULSE_15 ) {
		const int step = impulse == IMPULSE_14 ? 1 : count - 1;
		const int from = weaponWheelHoveredSlot >= 0 ? weaponWheelHoveredSlot : 0;
		for ( int i = 1; i <= count; ++i ) {
			const int candidate = ( from + step * i ) % count;
			if ( HasWeaponWheelSlotWeapon( candidate ) ) {
				slot = candidate;
				break;
			}
		}
	} else {
		return false;
	}
	if ( !HasWeaponWheelSlotWeapon( slot ) ) return true;
	weaponWheelHoveredSlot = slot;
	// A selector out on the ring turns to the choice, or its angle would take
	// the selection straight back; one resting in the dead zone stays there.
	const float reach = weaponWheelCursor.Length();
	if ( reach > WeaponWheelDeadzoneRadius() ) {
		const float angle = slot * idMath::TWO_PI / count;
		weaponWheelCursor.x = sinf( angle ) * reach;
		weaponWheelCursor.y = -cosf( angle ) * reach;
	}
	// A choice made with a control is deliberate: the wheel shows at once,
	// and releasing it equips the choice rather than counting as a tap.
	weaponWheelOpenedTime = Min( weaponWheelOpenedTime, Sys_Milliseconds() - 180 );
	return true;
}

void idPlayer::UpdateWeaponWheelEffects( void ) {
	// Network play keeps full speed and draws no wheel blur.
	if ( gameLocal.isMultiplayer ) {
		return;
	}
	const float blend = idMath::ClampFloat( 0.0f, 1.0f, weaponWheelBlend );

	if ( blend <= 0.0f ) {
		common->SetGameTimeScale( 1.0f );
		gameLocal.SetSpecialEffect( SPECIAL_EFFECT_BLUR, false );
		return;
	}

	common->SetGameTimeScale( WeaponWheelLerp( 1.0f, WEAPON_WHEEL_TIMESCALE_SCALE, blend ) );

	if ( cvarSystem->GetCVarBool( "ui_retained" ) ) {
		gameLocal.SetSpecialEffect( SPECIAL_EFFECT_BLUR, false );
		return; // the native pre-HUD pass supplies Gaussian scene soft focus
	}
	gameLocal.SetSpecialEffect( SPECIAL_EFFECT_BLUR, true );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 0, WeaponWheelLerp( 0.694f, 0.820f, blend ) );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 1, WeaponWheelLerp( 0.694f, 0.860f, blend ) );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 2, WeaponWheelLerp( 0.694f, 0.930f, blend ) );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 3, 0.0f );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 4, WeaponWheelLerp( 96.0f, 64.0f, blend ) );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 5, WeaponWheelLerp( 24.0f, 16.0f, blend ) );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 6, blend );
	gameLocal.SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 7, Max( cvarSystem->GetCVarFloat( "r_znear" ), 0.25f ) );
}

void idPlayer::UpdateWeaponWheel( void ) {
	if ( entityNumber != gameLocal.localClientNum ) return;
	if ( weaponWheelActive && cvarSystem->GetCVarBool( "ui_radialCancel" ) ) {
		ResetWeaponWheel( false );
		return;
	}
	const bool canUseWheel = CanUseWeaponWheel();
	const bool requested = weaponWheelDebug ? weaponWheelDebugHeld : ( usercmd.buttons & BUTTON_WEAPONWHEEL ) != 0;
	if ( !requested ) weaponWheelCanceled = false;
	const bool held = canUseWheel && requested && !weaponWheelCanceled;
	const bool wasBlended = weaponWheelBlend > 0.0f;
	const int now = Sys_Milliseconds();
	const bool reducedMotion = cvarSystem->GetCVarBool( "ui_retained" ) && cvarSystem->GetCVarBool( "ui_retainedReducedMotion" );
	const float blendInSpeed = reducedMotion ? 12.5f : WEAPON_WHEEL_BLEND_IN_SPEED;
	const float blendOutSpeed = reducedMotion ? 12.5f : cvarSystem->GetCVarBool( "ui_retained" ) ? 6.25f : WEAPON_WHEEL_BLEND_OUT_SPEED;

	if ( weaponWheelLastUpdateTime == 0 ) {
		weaponWheelLastUpdateTime = now;
	}

	float dt = MS2SEC( idMath::ClampInt( 0, 100, now - weaponWheelLastUpdateTime ) );
	weaponWheelLastUpdateTime = now;

	if ( !canUseWheel ) {
		// A menu, camera or death taking over dismisses this invocation. Do
		// not reopen from the same held control if it returns during the fade.
		if ( weaponWheelActive && requested ) weaponWheelCanceled = true;
		weaponWheelActive = false;
		if ( cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 1 ) cvarSystem->SetCVarInteger( "ui_radialOwner", 0 );
		weaponWheelBlend = Max( 0.0f, weaponWheelBlend - dt * blendOutSpeed );
		if ( weaponWheelBlend > 0.0f || wasBlended ) {
			UpdateWeaponWheelEffects();
		}
		if ( weaponWheelBlend <= 0.0f && wasBlended ) {
			ResetWeaponWheel( true );
		}
		return;
	}

	if ( held && !weaponWheelActive ) {
		weaponWheelActive = true;
		// Touch already qualified the hold before posting BUTTON_WEAPONWHEEL;
		// do not delay it again or misinterpret its release as a previous-weapon tap.
		weaponWheelOpenedTime = now - ( cvarSystem->GetCVarBool( "ui_radialTouchActive" ) ? 180 : 0 );
		if ( !weaponWheelGui ) weaponWheelGuiSlot = -1;
		weaponWheelCursor.Zero();
		weaponWheelLastMouseX = usercmd.mx;
		weaponWheelLastMouseY = usercmd.my;
		weaponWheelHoveredSlot = GetDefaultWeaponWheelSlot();
		for ( int axis = 0; axis < 3; ++axis ) {
			weaponWheelLastCmdAngles[ axis ] = SHORT2ANGLE( usercmd.angles[ axis ] );
		}
	}

	if ( weaponWheelActive ) {
		if ( !weaponWheelDebug ) UpdateWeaponWheelCursor();

		if ( weaponWheelCursor.Length() > WeaponWheelDeadzoneRadius() ) {
			float angle = atan2f( weaponWheelCursor.x, -weaponWheelCursor.y );
			if ( angle < 0.0f ) {
				angle += idMath::TWO_PI;
			}

			weaponWheelHoveredSlot = GetClosestAvailableWeaponWheelSlot( angle );
		} else if ( weaponWheelHoveredSlot < 0 ) {
			weaponWheelHoveredSlot = GetDefaultWeaponWheelSlot();
		}
	} else {
		weaponWheelLastMouseX = usercmd.mx;
		weaponWheelLastMouseY = usercmd.my;
	}

	if ( !held && weaponWheelActive ) {
		const int selectedWeapon = ( now - weaponWheelOpenedTime < 180 && !weaponWheelDebug )
			? previousWeapon : GetWeaponWheelWeaponForSlot( weaponWheelHoveredSlot );
		weaponWheelActive = false;
		weaponWheelCursor.Zero();
		weaponWheelLastMouseX = usercmd.mx;
		weaponWheelLastMouseY = usercmd.my;

		if ( selectedWeapon >= 0 && selectedWeapon < MAX_WEAPONS && ( inventory.weapons & ( 1 << selectedWeapon ) ) != 0 ) {
			// Use the existing reliable impulse route in MP, including prediction and ACK.
			if ( selectedWeapon != currentWeapon && GetWeaponWheelAmmoCount( selectedWeapon ) != 0 ) PerformImpulse( selectedWeapon );
		}
	}

	// Delay presentation, not selection. A tap swaps back without briefly
	// flashing the wheel or slowing/softening the scene.
	const bool showWheel = held && ( weaponWheelDebug || now - weaponWheelOpenedTime >= 180 );
	const float targetBlend = showWheel ? 1.0f : 0.0f;
	if ( weaponWheelBlend < targetBlend ) {
		weaponWheelBlend = Min( 1.0f, weaponWheelBlend + dt * blendInSpeed );
	} else if ( weaponWheelBlend > targetBlend ) {
		weaponWheelBlend = Max( 0.0f, weaponWheelBlend - dt * blendOutSpeed );
	}

	if ( weaponWheelBlend > 0.0f || wasBlended ) {
		UpdateWeaponWheelEffects();
	}
	if ( weaponWheelActive ) cvarSystem->SetCVarInteger( "ui_radialOwner", 1 );
	else if ( cvarSystem->GetCVarInteger( "ui_radialOwner" ) == 1 ) cvarSystem->SetCVarInteger( "ui_radialOwner", 0 );
}

