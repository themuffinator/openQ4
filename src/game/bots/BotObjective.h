//----------------------------------------------------------------
// BotObjective.h
//
// Mode-aware multiplayer objective discovery.  This layer only answers what
// the current rules make important; it does not choose routes, reserve goals,
// or replace the bot's combat target.
//----------------------------------------------------------------

#ifndef __GAME_MP_BOT_OBJECTIVE_H__
#define __GAME_MP_BOT_OBJECTIVE_H__

// idVec3 and idEntityPtr are part of the game-local contract and are complete
// before bot headers are included from Game_local.h.  Keeping the player and
// entity declarations here avoids coupling this small query interface to the
// concrete multiplayer mode classes.
class idEntity;
class idPlayer;

typedef enum {
	BOTOBJ_NONE,
	BOTOBJ_CAPTURE,
	BOTOBJ_INTERCEPT,
	BOTOBJ_RETURN,
	BOTOBJ_ESCORT,
	BOTOBJ_FETCH,
	BOTOBJ_RESCUE,
	BOTOBJ_CONTROL,
	BOTOBJ_DEFEND
} botObjectiveKind_t;

typedef struct botObjective_s {
	botObjectiveKind_t		kind;
	idEntityPtr<idEntity>	entity;
	idVec3					origin;
	float					priority;		// higher values win
	bool					holdPosition;	// remain at the origin after arriving

							botObjective_s( void );
	void					Clear( void );
} botObjective_t;

// Returns false, with out cleared, when the mode has no reliable objective or
// this player was deliberately left free for combat/pickups by the team-role
// allocator.  Role selection is deterministic for the current world snapshot,
// but the result itself is transient: callers must re-query and validate the
// idEntityPtr before using it on a later frame.
bool BotFindObjective( idPlayer *self, botObjective_t &out );

#endif /* !__GAME_MP_BOT_OBJECTIVE_H__ */
