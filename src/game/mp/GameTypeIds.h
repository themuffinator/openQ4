//----------------------------------------------------------------
// GameTypeIds.h
//
// Stable multiplayer game-type and side identifiers.  These values cross the
// game/network/UI boundary, so keep this dependency-free and append-only.
//----------------------------------------------------------------

#ifndef __MP_GAME_TYPE_IDS_H__
#define __MP_GAME_TYPE_IDS_H__

typedef enum {
	GAME_SP,
	GAME_DM,
	GAME_TOURNEY,
	GAME_TDM,
	GAME_CTF,
	GAME_1F_CTF,
	GAME_ARENA_CTF,
	GAME_ARENA_1F_CTF,
	GAME_DEADZONE,
	GAME_DUEL,
	GAME_CA,
	GAME_FREEZETAG,
	GAME_REDROVER,
	GAME_OVERLOAD,
	GAME_HARVESTER,
	GAME_DOMINATION,
	GAME_ATTACK_DEFEND,
	NUM_GAME_TYPES,
} gameType_t;

typedef enum {
	TEAM_NONE = -1,
	TEAM_MARINE,
	TEAM_STROGG,
	TEAM_MAX,
} team_t;

#endif // __MP_GAME_TYPE_IDS_H__
