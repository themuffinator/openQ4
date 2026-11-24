//----------------------------------------------------------------
// MatchPhase.h
//
// Stable multiplayer lifecycle wire values shared by the legacy gameplay
// adapters and the authoritative match-session core.  Keep this header free
// of game-local dependencies so value-layer code cannot create include cycles.
//----------------------------------------------------------------

#ifndef __MP_MATCH_PHASE_H__
#define __MP_MATCH_PHASE_H__

// The first byte of every rvGameState packet.  Append-only: never insert,
// remove or renumber a value that has shipped.
typedef enum {
	INACTIVE = 0,
	WARMUP,
	COUNTDOWN,
	GAMEON,
	SUDDENDEATH,
	GAMEREVIEW,
	NEXTGAME,
	STATE_COUNT
} mpGameState_t;

// Round-mode sub-state replicated alongside the parent match phase.
// Append-only for the same wire-compatibility reason.
typedef enum {
	RS_INACTIVE = 0,
	RS_COUNTDOWN,
	RS_ACTIVE,
	RS_COMPLETE,
	RS_STATE_COUNT
} roundState_t;

#endif // __MP_MATCH_PHASE_H__
