//----------------------------------------------------------------
// Presets.h
//
// openQ4 multiplayer presets (roadmap MP-14).
//
// A preset names a way to play: a game type, a ruleset, modifiers and limits
// set together, so "rocketarena" or "instactf" is one word in a server
// config, one line in a vote and one choice in Create Server.
//
// A preset is typed data, never executable configuration.  The built-in ones
// are the table in Presets.cpp; a server adds its own as
// presets/<name>.cfg, a list of "key value" lines that may only name the
// match settings in the allow list.  Applying one first puts every
// composition setting back to the standard preset's value and then applies
// the preset's own, so two presets never mix.
//
// si_preset names the preset the server last applied.  It is a statement
// about the settings, not a request: when a composition setting no longer
// matches the named preset, the next map load or restart clears it.
//----------------------------------------------------------------

#ifndef __GAME_MP_PRESETS_H__
#define __GAME_MP_PRESETS_H__

class idCmdArgs;
class idDict;
class idStr;

const int	MP_PRESET_NAME_MAX		= 32;
const int	MP_PRESET_FILE_MAX		= 8192;		// bytes of presets/<name>.cfg read
const int	MP_PRESET_SETTINGS_MAX	= 48;		// settings one preset may carry

typedef struct mpPresetInfo_s {
	const char *	name;		// token: the preset command, votes and si_preset
	const char *	label;		// #str_ name for menus
	const char *	settings;	// "key value key value ..."
} mpPresetInfo_t;

class rvMPPresets {
public:
	// The built-in table.  Row 0 is the standard preset every other one starts from.
	static int						BuiltInCount( void );
	static const mpPresetInfo_t *	BuiltIn( int index );
	static const mpPresetInfo_t *	BuiltInByName( const char *name );

	// Built-in names first, then presets/*.cfg.
	static void				Names( idStrList &names );
	static bool				ValidName( const char *name );

	// The settings a preset resolves to, standard values included.  False
	// with a reason when the preset does not exist or its file is malformed.
	static bool				Settings( const char *name, idDict &settings, idStr &reason );

	// Validate changes nothing.  Apply writes every setting and si_preset;
	// the caller rescans the server info and restarts the match.
	static bool				Validate( const char *name, idStr &reason );
	static bool				Apply( const char *name, idStr &reason );

	// True when the composition settings in serverInfo are the named preset's.
	static bool				Matches( const char *name, const idDict &serverInfo );

	// A setting a preset may carry, and whether it is part of the composition
	// (game type, ruleset, modifiers, scales, switches) that si_preset vouches for.
	static bool				SettingAllowed( const char *key );
	static bool				SettingIsComposition( const char *key );
};

// Server console commands and their argument completion (gamesys/SysCmds.cpp).
void	MP_Modifier_f( const idCmdArgs &args );
void	MP_WeaponArena_f( const idCmdArgs &args );
void	MP_Preset_f( const idCmdArgs &args );
void	MP_ListPresets_f( const idCmdArgs &args );
void	MP_ArgCompletion_Modifier( const idCmdArgs &args, void(*callback)( const char *s ) );
void	MP_ArgCompletion_WeaponArena( const idCmdArgs &args, void(*callback)( const char *s ) );
void	MP_ArgCompletion_Preset( const idCmdArgs &args, void(*callback)( const char *s ) );

#endif	// __GAME_MP_PRESETS_H__
