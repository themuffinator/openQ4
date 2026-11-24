//----------------------------------------------------------------
// MatchTerminationPolicy.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_TERMINATION_STANDALONE )
	#include "MatchTerminationPolicy.h"
#elif defined( ID_MAPFILE_STANDALONE )
	#include "MatchTerminationPolicy.h"
#else
	#include "../../Game_local.h"
	#include "MatchTerminationPolicy.h"
#endif

mpMatchTerminationDecision::mpMatchTerminationDecision( void ) {
	kind = MP_MATCH_TERMINATION_NONE;
	targetPhase = INACTIVE;
	reason = MP_MATCH_TRANSITION_NONE;
	forfeitingSide = MP_MATCH_SIDE_NONE;
}

bool mpMatchTerminationDecision::ShouldTransition( void ) const {
	return kind > MP_MATCH_TERMINATION_NONE &&
		kind < MP_MATCH_TERMINATION_COUNT &&
		reason > MP_MATCH_TRANSITION_NONE &&
		reason < MP_MATCH_TRANSITION_REASON_COUNT;
}

mpMatchTerminationDecision MPEvaluatePopulationTermination(
		mpGameState_t phase, bool enoughClients, int forfeitingSide ) {
	mpMatchTerminationDecision decision;
	if ( enoughClients || ( phase != COUNTDOWN && phase != GAMEON &&
			phase != SUDDENDEATH ) ) {
		return decision;
	}

	if ( phase == COUNTDOWN ) {
		decision.kind = MP_MATCH_TERMINATION_COUNTDOWN_CANCELLED;
		decision.targetPhase = WARMUP;
		decision.reason = MP_MATCH_TRANSITION_COUNTDOWN_ABORTED;
		return decision;
	}

	decision.targetPhase = GAMEREVIEW;
	if ( forfeitingSide >= 0 && forfeitingSide < MP_MATCH_SIDE_COUNT ) {
		decision.kind = MP_MATCH_TERMINATION_FORFEIT;
		decision.reason = MP_MATCH_TRANSITION_FORFEIT;
		decision.forfeitingSide = forfeitingSide;
	} else {
		decision.kind = MP_MATCH_TERMINATION_ABORTED;
		decision.reason = MP_MATCH_TRANSITION_MATCH_ABORTED;
	}
	return decision;
}
