//----------------------------------------------------------------
// Presets.cpp
//
// openQ4 multiplayer presets (roadmap MP-14).  See Presets.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "Modifiers.h"
#include "Presets.h"

/*
===============================================================================

	The built-in presets.

	Row 0, "standard", is every composition setting at its stock value; each
	other preset is applied over it.  A preset that names no game type keeps
	the one being played, so "instagib" is Instagib Capture the Flag on a
	Capture the Flag server.

	The Create Server page offers these in this order.  The framework keeps
	its own copy of the table (RETAINED_CREATE_PRESETS in
	src/framework/Session_menu.cpp), because the page is also shown while the
	single-player module is loaded; mp_modifiers_contract.py holds the two
	tables to each other line for line.

===============================================================================
*/

static const mpPresetInfo_t mpPresetTable[] = {
	{ "standard",		"#str_231980",	"si_ruleset 0 si_instagib 0 si_quadHog 0 si_vampiric 0 si_weaponsFrenzy 0 si_weaponArena none si_powerups 1 si_selfDamage 100 si_knockback 100 si_fallDamage 100 si_weaponRespawn 0" },
	{ "instagib",		"#str_231960",	"si_instagib 1" },
	{ "quadhog",		"#str_231962",	"si_gameType DM si_quadHog 1" },
	{ "vampiric",		"#str_231964",	"si_vampiric 1" },
	{ "frenzy",			"#str_231966",	"si_weaponsFrenzy 1" },
	{ "rocketarena",	"#str_231981",	"si_weaponArena rocketlauncher si_powerups 0 si_selfDamage 0 si_fallDamage 0" },
	{ "railarena",		"#str_231982",	"si_weaponArena railgun si_powerups 0" },
	{ "nadefest",		"#str_231983",	"si_weaponArena grenadelauncher si_selfDamage 50" },
	{ "classic",		"#str_231988",	"si_ruleset 1" },
	{ "instactf",		"#str_231984",	"si_gameType CTF si_instagib 1 si_captureLimit 8" },
	{ "classicctf",		"#str_231985",	"si_gameType CTF si_ruleset 1 si_classicGrapple 1 si_captureLimit 8" },
};

static const int mpNumPresets = sizeof( mpPresetTable ) / sizeof( mpPresetTable[ 0 ] );

// What a preset may set beyond the composition: limits and the plain match
// switches.  Nothing here names a file, a password, a player or a command.
static const char *const mpPresetLimitKeys[] = {
	"si_fragLimit", "si_timeLimit", "si_captureLimit", "si_roundLimit", "si_roundTimeLimit", "si_scoreLimit",
	"si_tourneyLimit", "si_controlTime", "si_overtime", "si_mercyLimit", "si_teamDamage", "si_weaponStay",
	"si_warmupWeapons", "si_classicGrapple", "si_isBuyingEnabled", "si_buyingRules",
};

/*
================
MPPresetAddSetting

One checked setting into the dictionary.  The built-in rows go through the
same checks a file does, so a slip in the table is a refusal at the first use
rather than a silent write.
================
*/
static bool MPPresetAddSetting( const char *key, const char *value, idDict &settings, idStr &reason ) {
	if ( !rvMPPresets::SettingAllowed( key ) ) {
		reason = va( "'%s' is not a setting a preset may change", key );
		return false;
	}
	if ( idStr::Length( value ) > 64 || strpbrk( value, ";\"\n\r" ) != NULL ) {
		reason = va( "the value of '%s' is not usable", key );
		return false;
	}
	if ( idStr::Icmp( key, "si_gameType" ) == 0 ) {
		const mpGameTypeInfo_t *info = MPGameTypeByName( value );
		if ( info == NULL || !MPGameTypeIsSelectable( info->type ) ) {
			reason = va( "'%s' is not a game type", value );
			return false;
		}
		value = info->name;
	} else if ( idStr::Icmp( key, "si_weaponArena" ) == 0 ) {
		if ( !rvMPModifiers::ValidateWeaponArena( value, reason ) ) {
			return false;
		}
		const mpWeaponArenaInfo_t *arena = rvMPModifiers::WeaponArena( rvMPModifiers::WeaponArenaByName( value ) );
		value = arena != NULL ? arena->name : "none";
	} else {
		// every other setting is a whole number
		for ( const char *c = value; *c != '\0'; c++ ) {
			if ( *c < '0' || *c > '9' ) {
				reason = va( "the value of '%s' is a whole number", key );
				return false;
			}
		}
		if ( value[ 0 ] == '\0' ) {
			reason = va( "'%s' has no value", key );
			return false;
		}
	}
	if ( settings.GetNumKeyVals() >= MP_PRESET_SETTINGS_MAX && settings.FindKey( key ) == NULL ) {
		reason = "too many settings";
		return false;
	}
	settings.Set( key, value );
	return true;
}

/*
================
MPPresetParseSettings

"key value key value" into a dictionary.
================
*/
static bool MPPresetParseSettings( const char *text, idDict &settings, idStr &reason ) {
	idCmdArgs args;
	args.TokenizeString( text, false );
	if ( ( args.Argc() & 1 ) != 0 ) {
		reason = "a setting has no value";
		return false;
	}
	for ( int i = 0; i + 1 < args.Argc(); i += 2 ) {
		if ( !MPPresetAddSetting( args.Argv( i ), args.Argv( i + 1 ), settings, reason ) ) {
			return false;
		}
	}
	return true;
}

/*
================
MPPresetReadFile

presets/<name>.cfg: one "key value" a line, an optional leading "set" or
"seta" so a line copied from a server config still reads, and // comments.
The file is parsed here and never executed.
================
*/
static bool MPPresetReadFile( const char *name, idDict &settings, idStr &reason ) {
	const idStr path = va( "presets/%s.cfg", name );
	idFile *file = fileSystem->OpenFileRead( path.c_str() );
	if ( file == NULL ) {
		reason = va( "no preset named '%s'", name );
		return false;
	}
	const int length = file->Length();
	if ( length <= 0 || length > MP_PRESET_FILE_MAX ) {
		fileSystem->CloseFile( file );
		reason = va( "%s is empty or longer than %d bytes", path.c_str(), MP_PRESET_FILE_MAX );
		return false;
	}
	idStr text;
	text.Fill( ' ', length );
	const int read = file->Read( &text[ 0 ], length );
	fileSystem->CloseFile( file );
	if ( read != length ) {
		reason = va( "%s could not be read", path.c_str() );
		return false;
	}
	// openQ4: a UTF-8 byte order mark, which Windows editors write, read as part
	// of the first key and made it "not a setting a preset may change"
	if ( length >= 3 && static_cast<unsigned char>( text[ 0 ] ) == 0xEF &&
		static_cast<unsigned char>( text[ 1 ] ) == 0xBB && static_cast<unsigned char>( text[ 2 ] ) == 0xBF ) {
		text[ 0 ] = text[ 1 ] = text[ 2 ] = ' ';
	}

	int start = 0;
	int lineNumber = 0;
	while ( start < length ) {
		int end = start;
		while ( end < length && text[ end ] != '\n' && text[ end ] != '\r' ) {
			end++;
		}
		lineNumber++;
		idStr line = text.Mid( start, end - start );
		// a CR LF pair ends one line, not two
		start = ( end + 1 < length && text[ end ] == '\r' && text[ end + 1 ] == '\n' ) ? end + 2 : end + 1;

		const int comment = line.Find( "//" );
		if ( comment >= 0 ) {
			line.CapLength( comment );
		}
		idCmdArgs args;
		args.TokenizeString( line.c_str(), false );
		int first = 0;
		if ( args.Argc() > 0 && ( idStr::Icmp( args.Argv( 0 ), "set" ) == 0 || idStr::Icmp( args.Argv( 0 ), "seta" ) == 0 ) ) {
			first = 1;
		}
		if ( args.Argc() == first ) {
			continue;
		}
		if ( args.Argc() != first + 2 ) {
			reason = va( "%s line %d: expected a setting and its value", path.c_str(), lineNumber );
			return false;
		}
		idStr lineReason;
		if ( !MPPresetAddSetting( args.Argv( first ), args.Argv( first + 1 ), settings, lineReason ) ) {
			reason = va( "%s line %d: %s", path.c_str(), lineNumber, lineReason.c_str() );
			return false;
		}
	}
	return true;
}

/*
================
rvMPPresets::BuiltInCount
================
*/
int rvMPPresets::BuiltInCount( void ) {
	return mpNumPresets;
}

/*
================
rvMPPresets::BuiltIn
================
*/
const mpPresetInfo_t *rvMPPresets::BuiltIn( int index ) {
	if ( index < 0 || index >= mpNumPresets ) {
		return NULL;
	}
	return &mpPresetTable[ index ];
}

/*
================
rvMPPresets::BuiltInByName
================
*/
const mpPresetInfo_t *rvMPPresets::BuiltInByName( const char *name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; i < mpNumPresets; i++ ) {
		if ( idStr::Icmp( mpPresetTable[ i ].name, name ) == 0 ) {
			return &mpPresetTable[ i ];
		}
	}
	return NULL;
}

/*
================
rvMPPresets::ValidName

Lower-case letters, digits, '-' and '_': the name is a file name, a vote
argument and a server-info value.
================
*/
bool rvMPPresets::ValidName( const char *name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return false;
	}
	int length = 0;
	for ( const char *c = name; *c != '\0'; c++, length++ ) {
		const bool ok = ( *c >= 'a' && *c <= 'z' ) || ( *c >= '0' && *c <= '9' ) || *c == '-' || *c == '_';
		if ( !ok || length >= MP_PRESET_NAME_MAX ) {
			return false;
		}
	}
	return true;
}

/*
================
rvMPPresets::Names
================
*/
void rvMPPresets::Names( idStrList &names ) {
	names.Clear();
	for ( int i = 0; i < mpNumPresets; i++ ) {
		names.Append( mpPresetTable[ i ].name );
	}
	idFileList *files = fileSystem->ListFiles( "presets", ".cfg", true );
	if ( files != NULL ) {
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			idStr name = files->GetFile( i );
			name.StripFileExtension();
			name.ToLower();
			if ( ValidName( name.c_str() ) && BuiltInByName( name.c_str() ) == NULL && names.FindIndex( name ) < 0 ) {
				names.Append( name );
			}
		}
		fileSystem->FreeFileList( files );
	}
}

/*
================
rvMPPresets::Settings
================
*/
bool rvMPPresets::Settings( const char *name, idDict &settings, idStr &reason ) {
	settings.Clear();
	idStr lowered = name != NULL ? name : "";
	lowered.ToLower();
	if ( !ValidName( lowered.c_str() ) ) {
		reason = "a preset name is lower-case letters, digits, '-' and '_'";
		return false;
	}
	if ( !MPPresetParseSettings( mpPresetTable[ 0 ].settings, settings, reason ) ) {
		return false;
	}
	const mpPresetInfo_t *builtIn = BuiltInByName( lowered.c_str() );
	if ( builtIn != NULL ) {
		return MPPresetParseSettings( builtIn->settings, settings, reason );
	}
	return MPPresetReadFile( lowered.c_str(), settings, reason );
}

/*
================
rvMPPresets::Validate
================
*/
bool rvMPPresets::Validate( const char *name, idStr &reason ) {
	idDict settings;
	return Settings( name, settings, reason );
}

/*
================
rvMPPresets::Apply
================
*/
bool rvMPPresets::Apply( const char *name, idStr &reason ) {
	idDict settings;
	if ( !Settings( name, settings, reason ) ) {
		return false;
	}
	for ( int i = 0; i < settings.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = settings.GetKeyVal( i );
		cvarSystem->SetCVarString( kv->GetKey().c_str(), kv->GetValue().c_str() );
	}
	idStr lowered = name;
	lowered.ToLower();
	cvarSystem->SetCVarString( "si_preset", lowered.c_str() );
	// A casual session imports its limits from the settings once and mirrors
	// them back at every map start. A preset has just changed them, so the
	// next start must import again or the mirror would put the old ones back.
	if ( gameLocal.isMultiplayer && !gameLocal.isClient ) {
		gameLocal.mpGame.InvalidateCasualRulesImport();
	}
	return true;
}

/*
================
rvMPPresets::Matches
================
*/
bool rvMPPresets::Matches( const char *name, const idDict &serverInfo ) {
	idDict settings;
	idStr reason;
	if ( !Settings( name, settings, reason ) ) {
		return false;
	}
	for ( int i = 0; i < settings.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = settings.GetKeyVal( i );
		const char *key = kv->GetKey().c_str();
		if ( !SettingIsComposition( key ) ) {
			continue;
		}
		const char *wanted = kv->GetValue().c_str();
		const char *actual = serverInfo.GetString( key );
		if ( idStr::Icmp( key, "si_weaponArena" ) == 0 ) {
			if ( rvMPModifiers::WeaponArenaByName( wanted ) != rvMPModifiers::WeaponArenaByName( actual ) ) {
				return false;
			}
		} else if ( idStr::Icmp( key, "si_gameType" ) == 0 ) {
			if ( idStr::Icmp( wanted, actual ) != 0 ) {
				return false;
			}
		} else if ( atoi( wanted ) != atoi( actual ) ) {
			return false;
		}
	}
	return true;
}

/*
================
rvMPPresets::SettingIsComposition
================
*/
bool rvMPPresets::SettingIsComposition( const char *key ) {
	if ( key == NULL ) {
		return false;
	}
	if ( idStr::Icmp( key, "si_gameType" ) == 0 ) {
		return true;
	}
	// the standard preset is the list of composition settings
	idCmdArgs args;
	args.TokenizeString( mpPresetTable[ 0 ].settings, false );
	for ( int i = 0; i + 1 < args.Argc(); i += 2 ) {
		if ( idStr::Icmp( args.Argv( i ), key ) == 0 ) {
			return true;
		}
	}
	return false;
}

/*
================
rvMPPresets::SettingAllowed
================
*/
bool rvMPPresets::SettingAllowed( const char *key ) {
	if ( SettingIsComposition( key ) ) {
		return true;
	}
	for ( int i = 0; i < static_cast<int>( sizeof( mpPresetLimitKeys ) / sizeof( mpPresetLimitKeys[ 0 ] ) ); i++ ) {
		if ( idStr::Icmp( mpPresetLimitKeys[ i ], key ) == 0 ) {
			return true;
		}
	}
	return false;
}

/*
===============================================================================

	Console commands.

	Server side only.  Each validates, writes the settings and lets the match
	restart apply them: the latch in idMultiplayerGame::SetGameType is the
	one place a rule changes, on every peer at once.

===============================================================================
*/

/*
================
MPRulesCommandRefused
================
*/
static bool MPRulesCommandRefused( const char *action ) {
	if ( gameLocal.isClient ) {
		common->Printf( "%s: only the server can change the match rules\n", action );
		return true;
	}
	if ( gameLocal.isMultiplayer && gameLocal.GameState() == GAMESTATE_ACTIVE &&
		 gameLocal.mpGame.RejectManagedLegacyMutation( action ) ) {
		return true;
	}
	return false;
}

/*
================
MPRulesCommandRescan

A map is running: push the new values into the server info now.
idMultiplayerGame::CheckModifierChange restarts the match when the latched
rules differ from what the server info now asks for.
================
*/
static void MPRulesCommandRescan( void ) {
	if ( gameLocal.isServer && gameLocal.isMultiplayer && gameLocal.GameState() == GAMESTATE_ACTIVE ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "rescanSI" " " __FILE__ " " __LINESTR__ );
	}
}

/*
================
MP_Modifier_f
================
*/
void MP_Modifier_f( const idCmdArgs &args ) {
	if ( args.Argc() < 3 ) {
		common->Printf( "usage: modifier <name> <0|1>\n" );
		for ( int i = 0; i < rvMPModifiers::Count(); i++ ) {
			const mpModifierInfo_t *info = rvMPModifiers::Info( i );
			common->Printf( "  %-12s %s %d%s\n", info->name, info->cvar, cvarSystem->GetCVarBool( info->cvar ) ? 1 : 0,
				( gameLocal.isMultiplayer && gameLocal.mpGame.HasModifier( info->id ) ) ? "  (active)" : "" );
		}
		return;
	}
	if ( MPRulesCommandRefused( "modifier" ) ) {
		return;
	}
	idStr reason;
	if ( !rvMPModifiers::SetModifier( args.Argv( 1 ), atoi( args.Argv( 2 ) ), reason ) ) {
		common->Printf( "modifier: %s\n", reason.c_str() );
		return;
	}
	MPRulesCommandRescan();
}

/*
================
MP_WeaponArena_f
================
*/
void MP_WeaponArena_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		idStrList names;
		rvMPModifiers::WeaponArenaNames( names );
		common->Printf( "usage: weaponArena <weapon|none>\n" );
		for ( int i = 0; i < names.Num(); i++ ) {
			common->Printf( "  %s\n", names[ i ].c_str() );
		}
		common->Printf( "si_weaponArena is \"%s\"\n", cvarSystem->GetCVarString( "si_weaponArena" ) );
		return;
	}
	if ( MPRulesCommandRefused( "weaponArena" ) ) {
		return;
	}
	idStr reason;
	if ( !rvMPModifiers::SetWeaponArena( args.Argv( 1 ), reason ) ) {
		common->Printf( "weaponArena: %s\n", reason.c_str() );
		return;
	}
	MPRulesCommandRescan();
}

/*
================
MP_Preset_f
================
*/
void MP_Preset_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: preset <name>\n" );
		MP_ListPresets_f( args );
		return;
	}
	if ( MPRulesCommandRefused( "preset" ) ) {
		return;
	}
	idStr reason;
	// openQ4: a preset may name a game type. On a running server with no map
	// that plays it the restart cannot happen, so say so before anything is
	// written; where a map does play it, QueueRulesRestart moves there.
	if ( gameLocal.isServer && gameLocal.isMultiplayer && gameLocal.GameState() == GAMESTATE_ACTIVE ) {
		idDict wanted;
		if ( rvMPPresets::Settings( args.Argv( 1 ), wanted, reason ) ) {
			const char *gameTypeName = wanted.GetString( "si_gameType" );
			if ( gameTypeName[ 0 ] != '\0' && !gameLocal.mpGame.AnyMapPlaysGameType( gameTypeName ) ) {
				common->Printf( "preset: no installed map plays %s\n", gameTypeName );
				return;
			}
		}
		reason.Clear();
	}
	if ( !rvMPPresets::Apply( args.Argv( 1 ), reason ) ) {
		common->Printf( "preset: %s\n", reason.c_str() );
		return;
	}
	common->Printf( "preset '%s' applied\n", cvarSystem->GetCVarString( "si_preset" ) );
	// A preset may change the game type and the limits as well as the latched
	// rules, so it always restarts a running match.
	if ( gameLocal.isServer && gameLocal.isMultiplayer && gameLocal.GameState() == GAMESTATE_ACTIVE ) {
		gameLocal.mpGame.QueueRulesRestart();
	}
}

/*
================
MP_ListPresets_f
================
*/
void MP_ListPresets_f( const idCmdArgs &args ) {
	idStrList names;
	rvMPPresets::Names( names );
	const char *current = cvarSystem->GetCVarString( "si_preset" );
	for ( int i = 0; i < names.Num(); i++ ) {
		idDict settings;
		idStr reason;
		const bool builtIn = rvMPPresets::BuiltInByName( names[ i ].c_str() ) != NULL;
		if ( !rvMPPresets::Settings( names[ i ].c_str(), settings, reason ) ) {
			common->Printf( "  %-16s unusable: %s\n", names[ i ].c_str(), reason.c_str() );
			continue;
		}
		idStr line;
		if ( builtIn ) {
			line = rvMPPresets::BuiltInByName( names[ i ].c_str() )->settings;
		} else {
			line = va( "presets/%s.cfg", names[ i ].c_str() );
		}
		common->Printf( "%s %-16s %s\n", idStr::Icmp( current, names[ i ].c_str() ) == 0 ? "*" : " ", names[ i ].c_str(), line.c_str() );
	}
	if ( current[ 0 ] == '\0' ) {
		common->Printf( "si_preset is empty: custom settings\n" );
	}
}

/*
================
MP_ArgCompletion_Modifier
================
*/
void MP_ArgCompletion_Modifier( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	idStrList names;
	rvMPModifiers::ModifierNames( names );
	for ( int i = 0; i < names.Num(); i++ ) {
		callback( va( "%s %s", args.Argv( 0 ), names[ i ].c_str() ) );
	}
}

/*
================
MP_ArgCompletion_WeaponArena
================
*/
void MP_ArgCompletion_WeaponArena( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	idStrList names;
	rvMPModifiers::WeaponArenaNames( names );
	for ( int i = 0; i < names.Num(); i++ ) {
		callback( va( "%s %s", args.Argv( 0 ), names[ i ].c_str() ) );
	}
}

/*
================
MP_ArgCompletion_Preset
================
*/
void MP_ArgCompletion_Preset( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	idStrList names;
	rvMPPresets::Names( names );
	for ( int i = 0; i < names.Num(); i++ ) {
		callback( va( "%s %s", args.Argv( 0 ), names[ i ].c_str() ) );
	}
}
