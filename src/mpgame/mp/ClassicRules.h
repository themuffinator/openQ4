//----------------------------------------------------------------
// ClassicRules.h
//
// openQ4 Classic multiplayer ruleset (si_ruleset 1): a Quake 2 inspired
// way to play the stock Quake 4 maps and weapons.  It is not a replica of
// Quake 2; it borrows the feel - tiered armour, a blaster start, Quake 2
// weapon and ammo numbers, timed mega health, stair double jumps, Threewave
// CTF scoring and the grappling hook - and keeps Quake 4's assets.
//
// Stock behaviour is untouched while the ruleset is off.  The weapon, ammo
// and item numbers are an overlay on the shipped entity definitions, applied
// when a Classic map loads and taken back off when it unloads, so no
// replacement content ships and the single-player module never sees it.
//----------------------------------------------------------------

#ifndef __GAME_MP_CLASSICRULES_H__
#define __GAME_MP_CLASSICRULES_H__

class idDict;
class idPlayer;

// si_ruleset
typedef enum {
	MP_RULESET_QUAKE4		= 0,
	MP_RULESET_CLASSIC		= 1,
	MP_RULESET_COUNT
} mpRuleset_t;

// si_classicGrapple
typedef enum {
	CLASSIC_GRAPPLE_OFF		= 0,
	CLASSIC_GRAPPLE_CTF		= 1,	// flag modes only, as Threewave CTF had it
	CLASSIC_GRAPPLE_ALL		= 2
} classicGrappleMode_t;

// Armour tiers.  Only meaningful while the player holds armour.
typedef enum {
	CLASSIC_ARMOR_NONE		= 0,
	CLASSIC_ARMOR_JACKET,			// green: 25, holds 50, stops 30%
	CLASSIC_ARMOR_COMBAT,			// yellow: 50, holds 100, stops 60%
	CLASSIC_ARMOR_BODY,				// red: 100, holds 200, stops 80%
	CLASSIC_ARMOR_TIERS
} classicArmorTier_t;

const int	CLASSIC_ARMOR_TIER_BITS		= 2;

// The grappling hook, as the server simulates it and the snapshot carries it.
typedef enum {
	CLASSIC_HOOK_NONE		= 0,
	CLASSIC_HOOK_FLYING,
	CLASSIC_HOOK_ATTACHED
} classicHookState_t;

const int	CLASSIC_HOOK_STATE_BITS		= 2;

// Health and armour both stop at 250: Threewave's mega health limit, and
// armour travels in the player snapshot as a byte.
const int	CLASSIC_HEALTH_CAP			= 250;
const int	CLASSIC_ARMOR_CAP			= 250;

// Mega health: the extra hundred starts draining after five seconds, a point a
// second, and the item comes back a while after the holder is back at 100.
const int	CLASSIC_MEGA_DECAY_DELAY	= 5000;
const int	CLASSIC_MEGA_DECAY_PULSE	= 1000;

// Stair double jump: a second jump this soon after the last one gets a boost,
// which is what made Quake 2 stairs and ledges a launch pad.
const int	CLASSIC_DOUBLE_JUMP_WINDOW	= 400;
const float	CLASSIC_DOUBLE_JUMP_BOOST	= 100.0f;

// Grappling hook speeds and limits.
const float	CLASSIC_HOOK_FLY_SPEED		= 1200.0f;	// Threewave 650; quicker, for Quake 4's bigger maps
const float	CLASSIC_HOOK_PULL_SPEED		= 750.0f;	// Threewave 650, against Quake 4's 320 run speed
const float	CLASSIC_HOOK_HANG_DISTANCE	= 64.0f;
const int	CLASSIC_HOOK_MAX_FLY_TIME	= 2000;
const int	CLASSIC_HOOK_REFIRE_DELAY	= 250;

class rvClassicRules {
public:
	// Ruleset selection.  The game latches it at map load from the server info.
	static int				RulesetFromServerInfo( const idDict &serverInfo );
	static const char *		RulesetName( int ruleset );

	// Entity definition overlay.
	static void				ApplyDefinitions( void );
	static void				RevertDefinitions( void );
	static bool				DefinitionsApplied( void );

	// Map lifetime of the CTF bookkeeping below.
	static void				ResetMatchState( void );
	static void				ResetClient( int clientNum );

	// Grappling hook availability for the current map and game type.
	static bool				GrappleAllowed( void );

	// Armour.
	static bool				GiveArmor( idPlayer *player, const char *tierName );
	static int				AbsorbDamage( int tier, int armor, int damage, bool energy );
	static const char *		ArmorTierName( int tier );

	// Threewave CTF scoring.  Each returns true when it handled the award, so
	// the caller skips the stock Quake 4 points.
	static void				NoteDamage( idPlayer *victim, idPlayer *attacker );
	static bool				AwardFragBonuses( idPlayer *victim, idPlayer *killer );
	static bool				AwardFlagPickup( idPlayer *player );
	static bool				AwardFlagReturn( idPlayer *player );
	static bool				AwardCapture( idPlayer *capturer );
};

#endif	// __GAME_MP_CLASSICRULES_H__
