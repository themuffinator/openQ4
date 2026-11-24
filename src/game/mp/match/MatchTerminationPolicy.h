//----------------------------------------------------------------
// MatchTerminationPolicy.h
//
// Dependency-neutral population-loss termination policy.  Entity counting,
// team/series binding and presentation remain adapter responsibilities.
//----------------------------------------------------------------

#ifndef __MP_MATCH_TERMINATION_POLICY_H__
#define __MP_MATCH_TERMINATION_POLICY_H__

#include "MatchSession.h"

typedef enum {
	MP_MATCH_TERMINATION_NONE = 0,
	MP_MATCH_TERMINATION_COUNTDOWN_CANCELLED,
	MP_MATCH_TERMINATION_ABORTED,
	MP_MATCH_TERMINATION_FORFEIT,
	MP_MATCH_TERMINATION_COUNT
} mpMatchTerminationKind_t;

struct mpMatchTerminationDecision {
	mpMatchTerminationKind_t kind;
	mpGameState_t targetPhase;
	mpMatchTransitionReason_t reason;
	int forfeitingSide;

	mpMatchTerminationDecision( void );
	bool ShouldTransition( void ) const;
};

// Evaluate only after the gameplay adapter has counted its current eligible
// contestants.  A playable forfeiting side is supplied only when the adapter
// can prove who withdrew (for example an emptied team or a connection-scoped
// Duel series contestant); otherwise live population loss is an abort.
mpMatchTerminationDecision MPEvaluatePopulationTermination(
	mpGameState_t phase, bool enoughClients, int forfeitingSide );

#endif // __MP_MATCH_TERMINATION_POLICY_H__
