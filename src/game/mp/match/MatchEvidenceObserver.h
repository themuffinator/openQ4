//----------------------------------------------------------------
// MatchEvidenceObserver.h
//
// Allocation-free adapter that turns committed session/proposal deltas into
// typed evidence events.  It owns no game, filesystem, network or GUI state.
//----------------------------------------------------------------

#ifndef __MP_MATCH_EVIDENCE_OBSERVER_H__
#define __MP_MATCH_EVIDENCE_OBSERVER_H__

#include "MatchEvidence.h"
#include "MatchProposal.h"
#include "MatchSession.h"

class mpMatchEvidenceObserver {
public:
						mpMatchEvidenceObserver( void );

	void				Reset( const mpMatchSession &session,
							const mpProposalService &proposals );
	void				Observe( mpMatchEvidence &evidence,
							const mpEvidenceCommittedStamp &stamp,
							const mpMatchSession &session,
							const mpProposalService &proposals,
							mpEvidenceActorRef actor );

private:
	struct participantState_t {
		bool		occupied;
		uint32_t	sequence;
		mpMatchRoleMask_t roles;
	};

	struct rosterState_t {
		bool		declared;
		int			side;
		mpMatchRosterRole_t role;
		uint32_t	occupant;
	};

	struct proposalState_t {
		bool		occupied;
		mpProposalId_t proposalId;
		mpProposalStatus_t status;
		unsigned char castCount;
	};

	bool				initialized;
	uint64_t			sessionId;
	mpGameState_t		phase;
	roundState_t		round;
	mpMatchPauseView	pause;
	participantState_t	participants[ MP_MATCH_MAX_PARTICIPANTS ];
	rosterState_t		roster[ MP_MATCH_MAX_ROSTER_SEATS ];
	proposalState_t		proposal[ MP_PROPOSAL_SCOPE_COUNT ];

	void				Capture( const mpMatchSession &session,
							const mpProposalService &proposals );
	static mpEvidencePauseState_t EvidencePauseState( mpMatchPauseState_t state );
	static mpEvidencePauseKind_t EvidencePauseKind( mpMatchPauseKind_t kind );
	static mpEvidenceRosterRole_t EvidenceRosterRole( mpMatchRosterRole_t role );
	static int			ProposalSide( mpProposalScope_t scope );
	static mpEvidenceProposalAction_t ProposalAction( mpProposalStatus_t status );
};

#endif // __MP_MATCH_EVIDENCE_OBSERVER_H__
