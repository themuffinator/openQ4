//----------------------------------------------------------------
// Modifiers.cpp
//
// openQ4 multiplayer modifier layer (roadmap MP-14).  See Modifiers.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "Modifiers.h"

/*
===============================================================================

	The modifier table.

	Indexed by mpModifier_t.  name is what an operator types and what the
	server publishes in si_modifiers; the framework's server browser keeps a
	copy of the name and label columns (SERVER_BROWSER_MODIFIERS in
	src/framework/async/ServerScan.cpp), which mp_modifiers_contract.py holds
	to this table.

===============================================================================
*/

static const mpModifierInfo_t mpModifierTable[ MPMOD_COUNT ] = {
	{ MPMOD_INSTAGIB,	"instagib",		"si_instagib",		"#str_231960",	"#str_231961" },
	{ MPMOD_QUADHOG,	"quadhog",		"si_quadHog",		"#str_231962",	"#str_231963" },
	{ MPMOD_VAMPIRIC,	"vampiric",		"si_vampiric",		"#str_231964",	"#str_231965" },
	{ MPMOD_FRENZY,		"frenzy",		"si_weaponsFrenzy",	"#str_231966",	"#str_231967" },
};

/*
===============================================================================

	The weapon arena table.

	si_weaponArena tokens, in the order the Create Server choice offers them
	(RETAINED_CREATE_WEAPON_ARENAS in src/framework/Session_menu.cpp starts
	with "none" and then follows this table).  The labels are Quake 4's own
	weapon names.

===============================================================================
*/

static const mpWeaponArenaInfo_t mpWeaponArenaTable[] = {
	{ "machinegun",			"weapon_machinegun",		"#str_200106" },
	{ "shotgun",			"weapon_shotgun",			"#str_200105" },
	{ "hyperblaster",		"weapon_hyperblaster",		"#str_200107" },
	{ "grenadelauncher",	"weapon_grenadelauncher",	"#str_200108" },
	{ "nailgun",			"weapon_nailgun",			"#str_200109" },
	{ "rocketlauncher",		"weapon_rocketlauncher",	"#str_200110" },
	{ "railgun",			"weapon_railgun",			"#str_200111" },
	{ "lightninggun",		"weapon_lightninggun",		"#str_200112" },
	{ "dmg",				"weapon_dmg",				"#str_200113" },
	{ "napalmgun",			"weapon_napalmgun",			"#str_222001" },
};

static const int mpNumWeaponArenas = sizeof( mpWeaponArenaTable ) / sizeof( mpWeaponArenaTable[ 0 ] );

static const char *MP_MODIFIER_TOKEN_ARENA			= "arena:";
static const char *MP_MODIFIER_TOKEN_NO_POWERUPS	= "nopowerups";

/*
================
rvMPModifiers::Info
================
*/
const mpModifierInfo_t *rvMPModifiers::Info( int modifier ) {
	if ( modifier < 0 || modifier >= MPMOD_COUNT ) {
		return NULL;
	}
	return &mpModifierTable[ modifier ];
}

/*
================
rvMPModifiers::ByName
================
*/
const mpModifierInfo_t *rvMPModifiers::ByName( const char *name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; i < MPMOD_COUNT; i++ ) {
		if ( idStr::Icmp( mpModifierTable[ i ].name, name ) == 0 ) {
			return &mpModifierTable[ i ];
		}
	}
	return NULL;
}

/*
================
rvMPModifiers::WeaponArenaCount
================
*/
int rvMPModifiers::WeaponArenaCount( void ) {
	return mpNumWeaponArenas;
}

/*
================
rvMPModifiers::WeaponArena
================
*/
const mpWeaponArenaInfo_t *rvMPModifiers::WeaponArena( int index ) {
	if ( index < 0 || index >= mpNumWeaponArenas ) {
		return NULL;
	}
	return &mpWeaponArenaTable[ index ];
}

/*
================
rvMPModifiers::WeaponArenaByName

-1 for no arena, which is also what an unknown token plays as: a stale config
or a crafted server info must not leave the peers disagreeing about a loadout.
================
*/
int rvMPModifiers::WeaponArenaByName( const char *name ) {
	if ( name == NULL ) {
		return -1;
	}
	for ( int i = 0; i < mpNumWeaponArenas; i++ ) {
		if ( idStr::Icmp( mpWeaponArenaTable[ i ].name, name ) == 0 ) {
			return i;
		}
	}
	return -1;
}

/*
================
rvMPModifiers::Clear
================
*/
void rvMPModifiers::Clear( mpModifierState_t &state ) {
	state.mask = 0;
	state.weaponArena = -1;
	state.powerups = true;
}

/*
================
rvMPModifiers::Resolve

Everything here is a pure function of the server info and the game type, both
of which every peer holds before it populates the map.  Where two switches
cannot both mean something, one wins by rule rather than by whoever read the
dictionary first:

  Instagib owns the loadout and the damage rule, so it switches off the weapon
  arena, Vampiric Damage (nobody survives a hit to drain from), Quad Hog (a
  quad adds nothing to a lethal hit) and Weapons Frenzy.
  Quad Hog is a Deathmatch rule: its scoring has no meaning for a team, a duel
  or a round.
  Weapons Frenzy needs pickups and spendable ammo, so it stands aside for any
  fixed loadout: a weapon arena, or a mode that hands out the whole arsenal.
================
*/
void rvMPModifiers::Resolve( const idDict &serverInfo, int gameType, bool allowed, mpModifierState_t &state ) {
	Clear( state );
	if ( !allowed ) {
		return;
	}

	const bool instagib = serverInfo.GetBool( "si_instagib" );
	if ( instagib ) {
		state.mask |= MPModifierBit( MPMOD_INSTAGIB );
	} else {
		state.weaponArena = WeaponArenaByName( serverInfo.GetString( "si_weaponArena" ) );
		state.powerups = serverInfo.GetBool( "si_powerups", "1" );
		if ( serverInfo.GetBool( "si_quadHog" ) && gameType == GAME_DM ) {
			state.mask |= MPModifierBit( MPMOD_QUADHOG );
		}
		if ( serverInfo.GetBool( "si_vampiric" ) ) {
			state.mask |= MPModifierBit( MPMOD_VAMPIRIC );
		}
		if ( serverInfo.GetBool( "si_weaponsFrenzy" ) && state.weaponArena < 0 &&
			 !MPGameTypeHasAny( gameType, GTF_FULLARSENAL ) ) {
			state.mask |= MPModifierBit( MPMOD_FRENZY );
		}
	}
}

/*
================
rvMPModifiers::Tokens
================
*/
void rvMPModifiers::Tokens( const mpModifierState_t &state, idStr &out ) {
	out.Clear();
	for ( int i = 0; i < MPMOD_COUNT; i++ ) {
		if ( state.mask & MPModifierBit( i ) ) {
			if ( out.Length() ) {
				out += " ";
			}
			out += mpModifierTable[ i ].name;
		}
	}
	const mpWeaponArenaInfo_t *arena = WeaponArena( state.weaponArena );
	if ( arena != NULL ) {
		if ( out.Length() ) {
			out += " ";
		}
		out += MP_MODIFIER_TOKEN_ARENA;
		out += arena->name;
	}
	if ( !state.powerups ) {
		if ( out.Length() ) {
			out += " ";
		}
		out += MP_MODIFIER_TOKEN_NO_POWERUPS;
	}
}

/*
================
rvMPModifiers::FromTokens

Reads a published si_modifiers back, for a card about a server this module is
not playing on.  Unknown tokens are skipped: a newer server may know modifiers
this build does not.
================
*/
void rvMPModifiers::FromTokens( const char *tokens, mpModifierState_t &state ) {
	Clear( state );
	if ( tokens == NULL ) {
		return;
	}
	idCmdArgs args;
	args.TokenizeString( tokens, false );
	const int arenaPrefix = idStr::Length( MP_MODIFIER_TOKEN_ARENA );
	for ( int i = 0; i < args.Argc(); i++ ) {
		const char *token = args.Argv( i );
		const mpModifierInfo_t *info = ByName( token );
		if ( info != NULL ) {
			state.mask |= MPModifierBit( info->id );
		} else if ( idStr::Icmpn( token, MP_MODIFIER_TOKEN_ARENA, arenaPrefix ) == 0 ) {
			state.weaponArena = WeaponArenaByName( token + arenaPrefix );
		} else if ( idStr::Icmp( token, MP_MODIFIER_TOKEN_NO_POWERUPS ) == 0 ) {
			state.powerups = false;
		}
	}
}

/*
================
rvMPModifiers::Title

"Instagib Deathmatch", "Quad Hog Deathmatch", "Deathmatch (Rocket Launcher)".
Each modifier wraps the name with its own format, so a language orders the
words its own way; a weapon arena names its weapon after the lot.
================
*/
void rvMPModifiers::Title( const mpModifierState_t &state, const char *gameTypeName, idStr &out ) {
	out = gameTypeName != NULL ? gameTypeName : "";
	for ( int i = 0; i < MPMOD_COUNT; i++ ) {
		if ( state.mask & MPModifierBit( i ) ) {
			const idStr wrapped = va( common->GetLocalizedString( mpModifierTable[ i ].title ), out.c_str() );
			out = wrapped;
		}
	}
	const mpWeaponArenaInfo_t *arena = WeaponArena( state.weaponArena );
	if ( arena != NULL ) {
		const idStr weapon = common->GetLocalizedString( arena->label );
		const idStr wrapped = va( common->GetLocalizedString( "#str_231969" ), out.c_str(), weapon.c_str() );
		out = wrapped;
	}
}

/*
================
rvMPModifiers::Labels
================
*/
void rvMPModifiers::Labels( const mpModifierState_t &state, idStr &out ) {
	out.Clear();
	for ( int i = 0; i < MPMOD_COUNT; i++ ) {
		if ( state.mask & MPModifierBit( i ) ) {
			if ( out.Length() ) {
				out += ", ";
			}
			out += common->GetLocalizedString( mpModifierTable[ i ].label );
		}
	}
	const mpWeaponArenaInfo_t *arena = WeaponArena( state.weaponArena );
	if ( arena != NULL ) {
		const idStr weapon = common->GetLocalizedString( arena->label );
		if ( out.Length() ) {
			out += ", ";
		}
		out += va( common->GetLocalizedString( "#str_231970" ), weapon.c_str() );
	}
	if ( !state.powerups ) {
		if ( out.Length() ) {
			out += ", ";
		}
		out += common->GetLocalizedString( "#str_231987" );
	}
}

/*
================
rvMPModifiers::ValidateModifier

A reason instead of a print, so a console command, a vote and a preset can
each say it their own way.  The game type test uses the game type being
played: asking for Quad Hog in Capture the Flag would pass a vote and then
change nothing.
================
*/
bool rvMPModifiers::ValidateModifier( const char *name, int value, idStr &reason ) {
	const mpModifierInfo_t *info = ByName( name );
	if ( info == NULL ) {
		reason = va( "unknown modifier '%s'", name != NULL ? name : "" );
		return false;
	}
	if ( value != 0 && value != 1 ) {
		reason = va( "modifier '%s' is 0 or 1", info->name );
		return false;
	}
	if ( value == 1 && info->id == MPMOD_QUADHOG && gameLocal.isMultiplayer &&
		 gameLocal.GameState() == GAMESTATE_ACTIVE && gameLocal.gameType != GAME_DM ) {
		reason = "Quad Hog is played in Deathmatch";
		return false;
	}
	reason.Clear();
	return true;
}

/*
================
rvMPModifiers::SetModifier
================
*/
bool rvMPModifiers::SetModifier( const char *name, int value, idStr &reason ) {
	if ( !ValidateModifier( name, value, reason ) ) {
		return false;
	}
	cvarSystem->SetCVarBool( ByName( name )->cvar, value != 0 );
	return true;
}

/*
================
rvMPModifiers::ValidateWeaponArena
================
*/
bool rvMPModifiers::ValidateWeaponArena( const char *name, idStr &reason ) {
	reason.Clear();
	if ( name == NULL || name[ 0 ] == '\0' || idStr::Icmp( name, "none" ) == 0 || idStr::Icmp( name, "off" ) == 0 ) {
		return true;
	}
	if ( WeaponArenaByName( name ) < 0 ) {
		reason = va( "unknown weapon arena '%s'", name );
		return false;
	}
	return true;
}

/*
================
rvMPModifiers::SetWeaponArena
================
*/
bool rvMPModifiers::SetWeaponArena( const char *name, idStr &reason ) {
	if ( !ValidateWeaponArena( name, reason ) ) {
		return false;
	}
	const mpWeaponArenaInfo_t *arena = WeaponArena( WeaponArenaByName( name ) );
	cvarSystem->SetCVarString( "si_weaponArena", arena != NULL ? arena->name : "none" );
	return true;
}

/*
================
rvMPModifiers::ModifierNames
================
*/
void rvMPModifiers::ModifierNames( idStrList &names ) {
	names.Clear();
	for ( int i = 0; i < MPMOD_COUNT; i++ ) {
		names.Append( mpModifierTable[ i ].name );
	}
}

/*
================
rvMPModifiers::WeaponArenaNames
================
*/
void rvMPModifiers::WeaponArenaNames( idStrList &names ) {
	names.Clear();
	names.Append( "none" );
	for ( int i = 0; i < mpNumWeaponArenas; i++ ) {
		names.Append( mpWeaponArenaTable[ i ].name );
	}
}

/*
================
MPScalePercent

A scale travels as a whole percentage, so it reads the same on every platform
and fits the menus and the match rules, which deal in integers.
================
*/
static float MPScalePercent( const char *key ) {
	const int percent = gameLocal.serverInfo.GetInt( key, "100" );
	return idMath::ClampInt( MP_SCALE_PERCENT_MIN, MP_SCALE_PERCENT_MAX, percent ) * 0.01f;
}

/*
================
rvMPModifiers::SelfDamageScale
================
*/
float rvMPModifiers::SelfDamageScale( void ) {
	return gameLocal.isMultiplayer ? MPScalePercent( "si_selfDamage" ) : 1.0f;
}

/*
================
rvMPModifiers::KnockbackScale
================
*/
float rvMPModifiers::KnockbackScale( void ) {
	return gameLocal.isMultiplayer ? MPScalePercent( "si_knockback" ) : 1.0f;
}

/*
================
rvMPModifiers::FallDamageScale
================
*/
float rvMPModifiers::FallDamageScale( void ) {
	return gameLocal.isMultiplayer ? MPScalePercent( "si_fallDamage" ) : 1.0f;
}

/*
================
rvMPModifiers::WeaponRespawnSeconds
================
*/
float rvMPModifiers::WeaponRespawnSeconds( void ) {
	if ( !gameLocal.isMultiplayer ) {
		return 0.0f;
	}
	return static_cast<float>( idMath::ClampInt( 0, MP_WEAPON_RESPAWN_MAX, gameLocal.serverInfo.GetInt( "si_weaponRespawn" ) ) );
}
