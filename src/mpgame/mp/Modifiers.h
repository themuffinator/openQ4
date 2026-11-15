//----------------------------------------------------------------
// Modifiers.h
//
// openQ4 multiplayer modifier layer (roadmap MP-14).
//
// A modifier changes how a game type plays without being a game type of its
// own: Instagib, Quad Hog, Vampiric Damage, Weapons Frenzy and the
// single-weapon arenas all run over Deathmatch, Team Deathmatch, Capture the
// Flag and the rest.  Every modifier is one row in the table below, asked for
// by one server-info switch, and the whole set is latched with the game type
// when a map loads or restarts - on the server and on every client - so the
// peers always populate the map for the same rules and a match never changes
// rules half way.
//
// The scales (self damage, knockback, fall damage) and the weapon respawn
// time are read live on the server: nothing a client spawns or predicts
// depends on them.
//----------------------------------------------------------------

#ifndef __GAME_MP_MODIFIERS_H__
#define __GAME_MP_MODIFIERS_H__

class idDict;
class idStr;

// Append-only: the latched mask is published as tokens, but bots, demos and
// the match rules index this enum.
typedef enum {
	MPMOD_INSTAGIB = 0,		// unlimited railgun, any hit from another player kills
	MPMOD_QUADHOG,			// one Quad Damage; only its carrier's frags, and the frag on the carrier, score
	MPMOD_VAMPIRIC,			// damage dealt heals the attacker; no health pickups
	MPMOD_FRENZY,			// weapons and ammo come back fast and carried ammo regenerates
	MPMOD_COUNT
} mpModifier_t;

typedef struct mpModifierInfo_s {
	mpModifier_t	id;
	const char *	name;		// token: the modifier command, votes, presets and si_modifiers
	const char *	cvar;		// the server-info switch that asks for it
	const char *	label;		// #str_ name for menus, the browser and the join card
	const char *	title;		// #str_ format with one %s that wraps the game type's name
} mpModifierInfo_t;

// A single-weapon arena: everyone carries one weapon that never runs out, and
// the gauntlet.  Built from Quake 4's own weapons.
typedef struct mpWeaponArenaInfo_s {
	const char *	name;		// si_weaponArena token
	const char *	weaponDef;	// the weapon's entityDef
	const char *	label;		// #str_ name of the weapon (stock strings)
} mpWeaponArenaInfo_t;

// A modifier's bit in mpModifierState_t::mask. The engine's BIT() takes a
// compile-time constant; this one takes a table index.
inline int MPModifierBit( int modifier ) {
	return 1 << modifier;
}

// What a map is played under, as latched by idMultiplayerGame::SetGameType.
typedef struct mpModifierState_s {
	int				mask;			// MPModifierBit( mpModifier_t )
	int				weaponArena;	// index into the weapon arena table, -1 for none
	bool			powerups;		// the map's powerups spawn

	bool operator==( const mpModifierState_s &other ) const {
		return mask == other.mask && weaponArena == other.weaponArena && powerups == other.powerups;
	}
	bool operator!=( const mpModifierState_s &other ) const {
		return !( *this == other );
	}
} mpModifierState_t;

// Percent scales and seconds, clamped the way the cvars are.
const int	MP_SCALE_PERCENT_MIN		= 0;
const int	MP_SCALE_PERCENT_MAX		= 999;
const int	MP_WEAPON_RESPAWN_MAX		= 999;

// Quad Hog
const int	MP_QUADHOG_FIRST_SPAWN_MS	= 5000;		// after the match goes live
const int	MP_QUADHOG_RESPAWN_MS		= 5000;		// after a loose quad was taken back
const int	MP_QUADHOG_LOOSE_MS			= 20000;	// a dropped quad nobody reaches is moved after this long

// Vampiric Damage: the cap an attacker can drain up to, as a multiple of max health
const int	MP_VAMPIRIC_HEALTH_FACTOR	= 2;

// Weapons Frenzy
const float	MP_FRENZY_RESPAWN_SCALE		= 0.25f;	// weapon and ammo pickups return in a quarter of their time
const float	MP_FRENZY_RESPAWN_FLOOR		= 2.0f;		// but never faster than this, in seconds
const int	MP_FRENZY_REGEN_PULSE_MS	= 1000;		// carried ammo comes back once a second
const int	MP_FRENZY_REGEN_DIVISOR		= 20;		// a twentieth of the ammo type's limit a pulse, at least one

class rvMPModifiers {
public:
	// The tables.
	static int							Count( void ) { return MPMOD_COUNT; }
	static const mpModifierInfo_t *		Info( int modifier );
	static const mpModifierInfo_t *		ByName( const char *name );
	static int							WeaponArenaCount( void );
	static const mpWeaponArenaInfo_t *	WeaponArena( int index );
	static int							WeaponArenaByName( const char *name );	// -1 for none or unknown

	// What the server info asks for, resolved against the game type: a
	// modifier that cannot apply (Quad Hog outside Deathmatch, Weapons Frenzy
	// over a fixed loadout) is left out, so every peer computes the same set.
	static void					Resolve( const idDict &serverInfo, int gameType, bool allowed, mpModifierState_t &state );
	static void					Clear( mpModifierState_t &state );

	// "instagib quadhog arena:rocketlauncher nopowerups": the summary the
	// server publishes as si_modifiers for the browser and the loading card.
	static void					Tokens( const mpModifierState_t &state, idStr &out );
	static void					FromTokens( const char *tokens, mpModifierState_t &state );

	// Localized presentation.
	static void					Title( const mpModifierState_t &state, const char *gameTypeName, idStr &out );
	static void					Labels( const mpModifierState_t &state, idStr &out );	// "Instagib, Quad Hog"; empty for none

	// The switches as an operator, a vote or a preset sets them.  Validate
	// returns false with a reason and changes nothing; Set writes the cvar.
	// Neither restarts the match: the caller rescans the server info, and
	// idMultiplayerGame::CheckModifierChange restarts when the latch differs.
	static bool					ValidateModifier( const char *name, int value, idStr &reason );
	static bool					SetModifier( const char *name, int value, idStr &reason );
	static bool					ValidateWeaponArena( const char *name, idStr &reason );	// "", "none" and "off" turn it off
	static bool					SetWeaponArena( const char *name, idStr &reason );
	static void					ModifierNames( idStrList &names );
	static void					WeaponArenaNames( idStrList &names );

	// Live server-side scales.
	static float				SelfDamageScale( void );
	static float				KnockbackScale( void );
	static float				FallDamageScale( void );
	static float				WeaponRespawnSeconds( void );	// 0: each pickup's own time
};

#endif	// __GAME_MP_MODIFIERS_H__
