//----------------------------------------------------------------
// GameTypes.cpp
//
// openQ4 multiplayer gametype descriptor table.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "GameTypes.h"

/*
===============================================================================

	The gametype table.

	Indexed by gameType_t.  gameType_t is the first byte of every gamestate
	packet and is compared literally by the .gui files, so it is append-only:
	never insert or renumber, only add before NUM_GAME_TYPES.

	entityFilter selects which map entities spawn.  New modes borrow the
	entity layout of the Quake 4 mode they are shaped like, so they play on
	stock maps without new map content: the round modes borrow Team DM,
	the obelisk and turn modes borrow CTF, and Domination borrows Arena CTF
	so the assault point chain spawns as its control points.

	mapDeclKey is the fallback key checked in the map decl when a map does
	not explicitly advertise the mode.

===============================================================================
*/

static const mpGameTypeInfo_t mpGameTypeInfoTable[] = {
	{	GAME_SP,				"singleplayer",			"sp",		"#str_107679",	"",					"singleplayer",
		GTF_SINGLEPLAYER, MP_GAMESTATE_NONE, false },

	{	GAME_DM,				"DM",					"dm",		"#str_107679",	"DM",				"DM",
		GTF_FRAGLIMIT | GTF_BUYING, MP_GAMESTATE_DM, true },

	{	GAME_TOURNEY,			"Tourney",				"tourney",	"#str_107676",	"Tourney",			"Tourney",
		GTF_FRAGLIMIT | GTF_BRACKET, MP_GAMESTATE_TOURNEY, true },

	{	GAME_TDM,				"Team DM",				"tdm",		"#str_107677",	"Team DM",			"Team DM",
		GTF_TEAM | GTF_FRAGLIMIT | GTF_BUYING, MP_GAMESTATE_TEAMDM, true },

	// The flag modes carry GTF_BUYING because retail Quake 4 allowed the buy
	// menu everywhere except Tourney; the flag now drives that rule rather than
	// a hard-coded exclusion, so it has to say what shipped.
	{	GAME_CTF,				"CTF",					"ctf",		"#str_107678",	"CTF",				"CTF",
		GTF_TEAM | GTF_FLAG | GTF_CAPTURELIMIT | GTF_BUYING, MP_GAMESTATE_CTF, true },

	{	GAME_1F_CTF,			"One Flag CTF",			"1fctf",	"#str_107680",	"CTF",				"CTF",
		GTF_TEAM | GTF_FLAG | GTF_ONEFLAG | GTF_CAPTURELIMIT | GTF_BUYING, MP_GAMESTATE_CTF, true },

	{	GAME_ARENA_CTF,			"Arena CTF",			"actf",		"#str_107681",	"Arena CTF",		"Arena CTF",
		GTF_TEAM | GTF_FLAG | GTF_ARENA | GTF_CAPTURELIMIT | GTF_BUYING, MP_GAMESTATE_CTF, true },

	{	GAME_ARENA_1F_CTF,		"Arena One Flag CTF",	"a1fctf",	"#str_107682",	"Arena CTF",		"Arena CTF",
		GTF_TEAM | GTF_FLAG | GTF_ONEFLAG | GTF_ARENA | GTF_CAPTURELIMIT | GTF_BUYING, MP_GAMESTATE_CTF, true },

	{	GAME_DEADZONE,			"DeadZone",				"dz",		"#str_122001",	"DeadZone",			"DeadZone",
		GTF_TEAM | GTF_DEADZONE | GTF_BUYING, MP_GAMESTATE_DEADZONE, true },

	// openQ4: gametypes carried over from Quake Live
	{	GAME_DUEL,				"Duel",					"duel",		"#str_41300",	"DM",				"DM",
		GTF_DUEL | GTF_FRAGLIMIT, MP_GAMESTATE_DUEL, true },

	{	GAME_CA,				"Clan Arena",			"ca",		"#str_41301",	"Team DM",			"Team DM",
		GTF_TEAM | GTF_ROUND | GTF_ELIMINATION | GTF_ROUNDLIMIT | GTF_DAMAGESCORE | GTF_FULLARSENAL,
		MP_GAMESTATE_CA, true },

	{	GAME_FREEZETAG,			"Freeze Tag",			"ft",		"#str_41302",	"Team DM",			"Team DM",
		GTF_TEAM | GTF_ROUND | GTF_ELIMINATION | GTF_FREEZE | GTF_ROUNDLIMIT, MP_GAMESTATE_FREEZETAG, true },

	{	GAME_REDROVER,			"Red Rover",			"rr",		"#str_41303",	"Team DM",			"Team DM",
		GTF_TEAM | GTF_ROUND | GTF_TEAMSWAP | GTF_ROUNDLIMIT, MP_GAMESTATE_REDROVER, true },

	{	GAME_OVERLOAD,			"Overload",				"ovl",		"#str_41304",	"CTF",				"CTF",
		GTF_TEAM | GTF_OBELISK | GTF_CAPTURELIMIT, MP_GAMESTATE_NONE, false },

	{	GAME_HARVESTER,			"Harvester",			"har",		"#str_41305",	"CTF",				"CTF",
		GTF_TEAM | GTF_OBELISK | GTF_CAPTURELIMIT, MP_GAMESTATE_NONE, false },

	{	GAME_DOMINATION,		"Domination",			"dom",		"#str_41306",	"Arena CTF",		"Arena CTF",
		GTF_TEAM | GTF_CONTROLPOINT | GTF_SCORELIMIT, MP_GAMESTATE_NONE, false },

	{	GAME_ATTACK_DEFEND,		"Attack Defend",		"ad",		"#str_41307",	"CTF",				"CTF",
		GTF_TEAM | GTF_FLAG | GTF_ONEFLAG | GTF_ROUND | GTF_ELIMINATION | GTF_TURNS | GTF_SCORELIMIT | GTF_DAMAGESCORE | GTF_FULLARSENAL,
		MP_GAMESTATE_NONE, false },
};

const int mpNumGameTypeInfo = sizeof( mpGameTypeInfoTable ) / sizeof( mpGameTypeInfoTable[ 0 ] );

/*
================
si_gameTypeArgs

Mirrors the name column of the table above; kept adjacent so the two cannot
drift unnoticed, and cross-checked at startup by MPValidateGameTypeTable.
Both arrays are constant-initialised, so si_gameType's constructor may read
this safely regardless of translation unit initialisation order.
================
*/
const char *si_gameTypeArgs[] = {
	"singleplayer",
	"DM",
	"Tourney",
	"Team DM",
	"CTF",
	"One Flag CTF",
	"Arena CTF",
	"Arena One Flag CTF",
	"DeadZone",
	"Duel",
	"Clan Arena",
	"Freeze Tag",
	"Red Rover",
	NULL
};

const int si_numGameTypeArgs = sizeof( si_gameTypeArgs ) / sizeof( si_gameTypeArgs[ 0 ] );

/*
================
MPGameType
================
*/
const mpGameTypeInfo_t *MPGameType( int type ) {
	if ( type < 0 || type >= mpNumGameTypeInfo ) {
		return &mpGameTypeInfoTable[ GAME_DM ];
	}

	return &mpGameTypeInfoTable[ type ];
}

/*
================
MPGameTypeByName
================
*/
const mpGameTypeInfo_t *MPGameTypeByName( const char *name ) {
	int i;

	if ( name == NULL || *name == '\0' ) {
		return NULL;
	}

	for ( i = 0; i < mpNumGameTypeInfo; i++ ) {
		if ( idStr::Icmp( mpGameTypeInfoTable[ i ].name, name ) == 0 ) {
			return &mpGameTypeInfoTable[ i ];
		}
	}

	return NULL;
}

/*
================
MPGameTypeFlags
================
*/
int MPGameTypeFlags( int type ) {
	return MPGameType( type )->flags;
}

/*
================
MPGameTypeName
================
*/
const char *MPGameTypeName( int type ) {
	return MPGameType( type )->name;
}

/*
================
MPGameTypeLocalizedName
================
*/
const char *MPGameTypeLocalizedName( int type ) {
	return common->GetLocalizedString( MPGameType( type )->localizedName );
}

/*
================
MPGameTypeIsSelectable

The enum/table retain unavailable modes for wire stability, but only complete
authoritative implementations may enter server configuration, menus or votes.
================
*/
bool MPGameTypeIsSelectable( int type ) {
	if ( type < 0 || type >= mpNumGameTypeInfo ) {
		return false;
	}

	const mpGameTypeInfo_t *info = &mpGameTypeInfoTable[ type ];
	return info->selectable && info->stateFactory != MP_GAMESTATE_NONE;
}

/*
================
MPGameTypeHasAll
================
*/
bool MPGameTypeHasAll( int type, int mask ) {
	return ( ( MPGameType( type )->flags & mask ) == mask );
}

/*
================
MPGameTypeHasAny
================
*/
bool MPGameTypeHasAny( int type, int mask ) {
	return ( ( MPGameType( type )->flags & mask ) != 0 );
}

/*
================
MapDeclSupports

A map names each gametype it supports as a boolean key. Some spell the name
without its spaces (the Awakening's maps.def has "TeamDM" for "Team DM"), so
both spellings count.
================
*/
static bool MapDeclSupports( const idDict *mapDict, const char *key ) {
	if ( mapDict->GetBool( key ) ) {
		return true;
	}
	idStr compact;
	for ( const char *c = key; *c; c++ ) {
		if ( *c != ' ' ) {
			compact.Append( *c );
		}
	}
	return compact.Length() != idStr::Length( key ) && mapDict->GetBool( compact );
}

/*
================
MPMapSupportsGameType

A map advertises support with a boolean key named after the gametype.  Stock
Quake 4 maps only know about the stock modes, so a mode that borrows another
mode's entity layout is also playable wherever that base mode is.
================
*/
bool MPMapSupportsGameType( const idDict *mapDict, int type ) {
	const mpGameTypeInfo_t *info;

	if ( mapDict == NULL ) {
		return false;
	}

	info = MPGameType( type );

	if ( MapDeclSupports( mapDict, info->name ) ) {
		return true;
	}

	if ( info->mapDeclKey != NULL && idStr::Icmp( info->mapDeclKey, info->name ) != 0 ) {
		return MapDeclSupports( mapDict, info->mapDeclKey );
	}

	return false;
}

/*
================
MPMapSupportsGameTypeName
================
*/
bool MPMapSupportsGameTypeName( const idDict *mapDict, const char *name ) {
	const mpGameTypeInfo_t *info;

	if ( mapDict == NULL ) {
		return false;
	}

	info = MPGameTypeByName( name );
	if ( info == NULL ) {
		// unknown token; fall back to the literal key so map packs that
		// advertise their own modes keep working
		return mapDict->GetBool( name );
	}

	return MPMapSupportsGameType( mapDict, info->type );
}

/*
================
mpVoteGameTypeOrder

Order the gametype dropdowns and the vote packet use.  The first six entries
must stay where they are: they are the shipped vote_gametype_t values and the
stock .gui dropdowns index them positionally.  A table row may remain present
for wire compatibility while absent here until its authoritative state exists.
================
*/
static const int mpVoteGameTypeOrder[] = {
	GAME_DM,
	GAME_TOURNEY,
	GAME_TDM,
	GAME_CTF,
	GAME_ARENA_CTF,
	GAME_DEADZONE,
	// openQ4 additions
	GAME_DUEL,
	GAME_CA,
	GAME_FREEZETAG,
	GAME_REDROVER,
	GAME_1F_CTF,
	GAME_ARENA_1F_CTF
};

static const int mpNumVoteGameTypes = sizeof( mpVoteGameTypeOrder ) / sizeof( mpVoteGameTypeOrder[ 0 ] );

/*
================
MPVoteGameTypeCount
================
*/
int MPVoteGameTypeCount( void ) {
	return mpNumVoteGameTypes;
}

/*
================
MPVoteGameTypeToGameType
================
*/
int MPVoteGameTypeToGameType( int voteIndex ) {
	if ( voteIndex < 0 || voteIndex >= mpNumVoteGameTypes ) {
		return GAME_DM;
	}

	return mpVoteGameTypeOrder[ voteIndex ];
}

/*
================
MPGameTypeToVoteGameType
================
*/
int MPGameTypeToVoteGameType( int type ) {
	int i;

	for ( i = 0; i < mpNumVoteGameTypes; i++ ) {
		if ( mpVoteGameTypeOrder[ i ] == type ) {
			return i;
		}
	}

	return 0;	// VOTE_GAMETYPE_DM
}

/*
================
MPValidateGameTypeTable

Catches table, construction, completion and vote-order drift before a server
can advertise a mode that SetGameType cannot construct.
================
*/
void MPValidateGameTypeTable( void ) {
	int i;
	int j;

	if ( mpNumGameTypeInfo != NUM_GAME_TYPES ) {
		gameLocal.Error( "MPValidateGameTypeTable: gametype table has %d rows, gameType_t has %d values", mpNumGameTypeInfo, NUM_GAME_TYPES );
		return;
	}

	if ( si_numGameTypeArgs < 2 || si_gameTypeArgs[ si_numGameTypeArgs - 1 ] != NULL ) {
		gameLocal.Error( "MPValidateGameTypeTable: si_gameTypeArgs is not NULL terminated" );
		return;
	}

	for ( i = 0; i < mpNumGameTypeInfo; i++ ) {
		const mpGameTypeInfo_t &info = mpGameTypeInfoTable[ i ];
		const bool singlePlayer = ( info.flags & GTF_SINGLEPLAYER ) != 0;
		int completionCount = 0;
		int voteCount = 0;

		if ( info.type != i ) {
			gameLocal.Error( "MPValidateGameTypeTable: row %d declares gametype %d; the table must be indexed by gameType_t", i, info.type );
			return;
		}

		if ( info.name == NULL || info.name[ 0 ] == '\0' || info.localizedName == NULL || info.localizedName[ 0 ] == '\0' ) {
			gameLocal.Error( "MPValidateGameTypeTable: row %d has incomplete identity metadata", i );
			return;
		}

		if ( info.stateFactory < MP_GAMESTATE_NONE || info.stateFactory >= MP_GAMESTATE_FACTORY_COUNT ) {
			gameLocal.Error( "MPValidateGameTypeTable: gametype '%s' has invalid state factory %d", info.name, info.stateFactory );
			return;
		}

		if ( info.selectable != ( !singlePlayer && info.stateFactory != MP_GAMESTATE_NONE ) ) {
			gameLocal.Error( "MPValidateGameTypeTable: gametype '%s' selectability and state factory disagree", info.name );
			return;
		}

		for ( j = i + 1; j < mpNumGameTypeInfo; j++ ) {
			if ( idStr::Icmp( info.name, mpGameTypeInfoTable[ j ].name ) == 0 ) {
				gameLocal.Error( "MPValidateGameTypeTable: duplicate gametype token '%s'", info.name );
				return;
			}
		}

		for ( j = 0; j < si_numGameTypeArgs - 1; j++ ) {
			if ( si_gameTypeArgs[ j ] == NULL ) {
				gameLocal.Error( "MPValidateGameTypeTable: si_gameTypeArgs terminates early at %d", j );
				return;
			}
			if ( idStr::Icmp( info.name, si_gameTypeArgs[ j ] ) == 0 ) {
				completionCount++;
			}
		}

		for ( j = 0; j < mpNumVoteGameTypes; j++ ) {
			if ( mpVoteGameTypeOrder[ j ] == i ) {
				voteCount++;
			}
		}

		const int expectedCompletionCount = ( info.selectable || singlePlayer ) ? 1 : 0;
		const int expectedVoteCount = info.selectable ? 1 : 0;
		if ( completionCount != expectedCompletionCount ) {
			gameLocal.Error( "MPValidateGameTypeTable: gametype '%s' appears %d times in si_gameTypeArgs, expected %d", info.name, completionCount, expectedCompletionCount );
			return;
		}
		if ( voteCount != expectedVoteCount ) {
			gameLocal.Error( "MPValidateGameTypeTable: gametype '%s' appears %d times in mpVoteGameTypeOrder, expected %d", info.name, voteCount, expectedVoteCount );
			return;
		}
	}

	for ( i = 0; i < si_numGameTypeArgs - 1; i++ ) {
		if ( MPGameTypeByName( si_gameTypeArgs[ i ] ) == NULL ) {
			gameLocal.Error( "MPValidateGameTypeTable: si_gameTypeArgs contains unknown token '%s'", si_gameTypeArgs[ i ] );
			return;
		}
		for ( j = i + 1; j < si_numGameTypeArgs - 1; j++ ) {
			if ( idStr::Icmp( si_gameTypeArgs[ i ], si_gameTypeArgs[ j ] ) == 0 ) {
				gameLocal.Error( "MPValidateGameTypeTable: duplicate si_gameTypeArgs token '%s'", si_gameTypeArgs[ i ] );
				return;
			}
		}
	}

	for ( i = 0; i < mpNumVoteGameTypes; i++ ) {
		if ( !MPGameTypeIsSelectable( mpVoteGameTypeOrder[ i ] ) ) {
			gameLocal.Error( "MPValidateGameTypeTable: vote row %d names unavailable gametype %d", i, mpVoteGameTypeOrder[ i ] );
			return;
		}
	}
}
