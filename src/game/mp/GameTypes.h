//----------------------------------------------------------------
// GameTypes.h
//
// openQ4 multiplayer gametype descriptor table.
//
// Quake 4 described a gametype with a bare enum plus six independent
// hand-written string switches (SetGameType, si_gameTypeArgs,
// GetLongGametypeName, VoteGameTypeToString, GameTypeToVote,
// LocalizeGametype) and three hard-coded predicate sets that did not
// agree with each other.  Missing one of them degraded silently.
// Everything a gametype declares about itself now lives in one table.
//----------------------------------------------------------------

#ifndef __MP_GAMETYPES_H__
#define __MP_GAMETYPES_H__

#include "GameTypeIds.h"

/*
===============================================================================

	Gametype behaviour flags

	These replace idGameLocal::IsTeamGame/IsTeamGameType/IsFlagGameType,
	which were three overlapping and mutually inconsistent hard-coded lists.

===============================================================================
*/

const int GTF_TEAM			= BIT(  0 );	// two fixed teams (marine/strogg)
const int GTF_FLAG			= BIT(  1 );	// uses the CTF flag entities
const int GTF_ONEFLAG		= BIT(  2 );	// single neutral flag carried to the enemy base
const int GTF_ARENA			= BIT(  3 );	// team runes / arena powerups
const int GTF_ROUND			= BIT(  4 );	// round based match progression
const int GTF_ELIMINATION	= BIT(  5 );	// no respawn until the round ends
const int GTF_OBELISK		= BIT(  6 );	// team obelisk objective
const int GTF_CONTROLPOINT	= BIT(  7 );	// capturable control points
const int GTF_FRAGLIMIT		= BIT(  8 );	// match ends on si_fragLimit
const int GTF_CAPTURELIMIT	= BIT(  9 );	// match ends on si_captureLimit
const int GTF_SCORELIMIT	= BIT( 10 );	// match ends on si_scoreLimit
const int GTF_ROUNDLIMIT	= BIT( 11 );	// match ends on si_roundLimit
const int GTF_BRACKET		= BIT( 12 );	// multi-arena elimination bracket (Quake 4 Tourney)
const int GTF_DUEL			= BIT( 13 );	// strict 1v1 with a spectator queue
const int GTF_BUYING		= BIT( 14 );	// the buy menu is available
const int GTF_DAMAGESCORE	= BIT( 15 );	// personal score accrues from damage dealt
const int GTF_TEAMSWAP		= BIT( 16 );	// a kill moves the victim to the killer's team
const int GTF_FREEZE		= BIT( 17 );	// death freezes rather than kills
const int GTF_TURNS			= BIT( 18 );	// teams alternate attacking each round
const int GTF_DEADZONE		= BIT( 19 );	// the Ritual DeadZone control artifact
const int GTF_SINGLEPLAYER	= BIT( 20 );	// not a multiplayer gametype at all
const int GTF_FULLARSENAL	= BIT( 21 );	// spawn with every weapon, full ammo and full armour

// convenience masks
const int GTF_ANY_OBJECTIVE	= ( GTF_FLAG | GTF_OBELISK | GTF_CONTROLPOINT | GTF_DEADZONE );

/*
===============================================================================

	mpGameTypeInfo_t

===============================================================================
*/

// Construction is declared beside every descriptor so a mode cannot be
// advertised without an authoritative game-state implementation.  This is a
// module-local implementation detail rather than a wire value.
typedef enum {
	MP_GAMESTATE_NONE = 0,
	MP_GAMESTATE_DM,
	MP_GAMESTATE_TOURNEY,
	MP_GAMESTATE_TEAMDM,
	MP_GAMESTATE_CTF,
	MP_GAMESTATE_DEADZONE,
	MP_GAMESTATE_DUEL,
	MP_GAMESTATE_CA,
	MP_GAMESTATE_FREEZETAG,
	MP_GAMESTATE_REDROVER,
	MP_GAMESTATE_FACTORY_COUNT
} mpGameStateFactory_t;

typedef struct mpGameTypeInfo_s {
	gameType_t			type;			// wire value; the table is indexed by it
	const char *		name;			// si_gameType token
	const char *		abbrev;			// short tag used by logs and stat reporting
	const char *		localizedName;	// #str_ id shown in menus, HUD and scoreboard
	const char *		entityFilter;	// si_entityFilter used when spawning map entities
	const char *		mapDeclKey;		// map decl key that advertises support for this mode
	int					flags;
	mpGameStateFactory_t	stateFactory;	// data-driven SetGameType dispatch
	bool				selectable;		// exposed to cvar completion, menus and votes
} mpGameTypeInfo_t;

// NULL-terminated si_gameType token list; kept adjacent to the table it mirrors
extern const char *		si_gameTypeArgs[];
extern const int		si_numGameTypeArgs;

const mpGameTypeInfo_t *	MPGameType( int type );
const mpGameTypeInfo_t *	MPGameTypeByName( const char *name );
int							MPGameTypeFlags( int type );
const char *				MPGameTypeName( int type );
const char *				MPGameTypeLocalizedName( int type );
bool						MPGameTypeIsSelectable( int type );

// true when every flag in mask is set on the gametype
bool						MPGameTypeHasAll( int type, int mask );
// true when any flag in mask is set on the gametype
bool						MPGameTypeHasAny( int type, int mask );

// map decl compatibility: a mode is playable on a map that advertises either
// the mode itself or the base mode it inherits its entity layout from
bool						MPMapSupportsGameType( const idDict *mapDict, int type );
bool						MPMapSupportsGameTypeName( const idDict *mapDict, const char *name );

// vote and menu ordering.  The GUI gametype dropdowns index this list
// directly, so like gameType_t it is append-only.
int							MPVoteGameTypeCount( void );
int							MPVoteGameTypeToGameType( int voteIndex );
int							MPGameTypeToVoteGameType( int type );

// startup consistency check between the table and si_gameTypeArgs
void						MPValidateGameTypeTable( void );

#endif // __MP_GAMETYPES_H__
