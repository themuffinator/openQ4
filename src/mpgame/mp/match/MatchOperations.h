//----------------------------------------------------------------
// MatchOperations.h
//
// Authoritative, dependency-neutral dispatch for typed match operations.
// Transport, credentials, cvars, game entities, persistence and presentation
// remain adapter responsibilities.
//----------------------------------------------------------------

#ifndef __MP_MATCH_OPERATIONS_H__
#define __MP_MATCH_OPERATIONS_H__

#include "MatchProposal.h"
#include "MatchRules.h"
#include "MatchSeries.h"
#include "MatchSession.h"

#include <stdint.h>

/*
===============================================================================

	Execution policy and typed adapter boundary

	NEEDS_ADAPTER is a pre-commit result: none of the four supplied cores has
	changed.  APPLIED and NO_CHANGE never require a hidden command-string
	dispatch.  Adapters consume the original typed request together with the
	continuation kind below.

===============================================================================
*/

typedef enum {
	MP_OPERATION_REJECTED = 0,
	MP_OPERATION_NO_CHANGE,
	MP_OPERATION_APPLIED,
	MP_OPERATION_NEEDS_ADAPTER,
	MP_OPERATION_OUTCOME_COUNT
} mpOperationOutcome_t;

typedef enum {
	MP_OPERATION_REASON_NONE = 0,
	MP_OPERATION_REASON_PROTOCOL,
	MP_OPERATION_REASON_SESSION_MISMATCH,
	MP_OPERATION_REASON_TRANSPORT_MISMATCH,
	MP_OPERATION_REASON_BINDING_STALE,
	MP_OPERATION_REASON_PARTICIPANT_UNKNOWN,
	MP_OPERATION_REASON_PARTICIPANT_INACTIVE,
	MP_OPERATION_REASON_TARGET_UNKNOWN,
	MP_OPERATION_REASON_TARGET_ALIGNMENT,
	MP_OPERATION_REASON_NOT_AUTHORIZED,
	MP_OPERATION_REASON_WRONG_PHASE,
	MP_OPERATION_REASON_STALE_SESSION_REVISION,
	MP_OPERATION_REASON_STALE_RULES_REVISION,
	MP_OPERATION_REASON_STALE_PROPOSAL_REVISION,
	MP_OPERATION_REASON_STALE_SERIES_REVISION,
	MP_OPERATION_REASON_RULE_VALUE_TYPE,
	MP_OPERATION_REASON_RULE_STATE,
	MP_OPERATION_REASON_PROPOSAL_UNKNOWN,
	MP_OPERATION_REASON_PROPOSAL_NOT_PASSED,
	MP_OPERATION_REASON_SERIES_STATE,
	MP_OPERATION_REASON_UNREPRESENTABLE,
	MP_OPERATION_REASON_CORE_REJECTED,
	MP_OPERATION_REASON_INVARIANT,
	MP_OPERATION_REASON_COUNT
} mpOperationReason_t;

typedef enum {
	MP_OPERATION_CONTINUATION_NONE = 0,
	MP_OPERATION_CONTINUATION_POLICY_RATE_LIMIT,
	MP_OPERATION_CONTINUATION_REFEREE_AUTHENTICATE,
	MP_OPERATION_CONTINUATION_REFEREE_LOGOUT,
	MP_OPERATION_CONTINUATION_TEAM_CHANGE,
	MP_OPERATION_CONTINUATION_TEAM_LOCK,
	MP_OPERATION_CONTINUATION_QUEUE_JOIN,
	MP_OPERATION_CONTINUATION_QUEUE_DEFER,
	MP_OPERATION_CONTINUATION_QUEUE_LEAVE,
	MP_OPERATION_CONTINUATION_RULES_APPLY_PROFILE,
	MP_OPERATION_CONTINUATION_RULES_APPLY_FIELD,
	MP_OPERATION_CONTINUATION_RULES_COMMIT,
	MP_OPERATION_CONTINUATION_PROPOSAL_CREATE,
	MP_OPERATION_CONTINUATION_ROSTER_INVITE,
	MP_OPERATION_CONTINUATION_ROSTER_ACCEPT,
	MP_OPERATION_CONTINUATION_ROSTER_REMOVE,
	MP_OPERATION_CONTINUATION_ROSTER_SUBSTITUTE,
	MP_OPERATION_CONTINUATION_ROLE_ASSIGN,
	MP_OPERATION_CONTINUATION_SERIES_CONFIGURE_PROFILE,
	MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP,
	MP_OPERATION_CONTINUATION_SERIES_PERSIST,
	MP_OPERATION_CONTINUATION_SERIES_MATCH_RESULT,
	MP_OPERATION_CONTINUATION_PROPOSAL_ACKNOWLEDGE,
	MP_OPERATION_CONTINUATION_ROSTER_LEAVE,
	// Adapter removes the stable participant in `participant`; it must resolve
	// the participant's current transport binding only at commit time.
	MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE,
	// Adapter binds stable `participant` to abstract competition `side` A/B.
	MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND,
	MP_OPERATION_CONTINUATION_COUNT
} mpOperationContinuationKind_t;

typedef struct mpOperationAdapterContinuation_s {
	mpOperationContinuationKind_t kind;
	mpMatchOperationOpcode_t opcode;
	mpParticipantId actor;
	mpParticipantId participant;
	mpParticipantId replacementParticipant;
	int side;
	mpMatchRosterRole_t rosterRole;
	mpProposalScope_t proposalScope;
	mpProposalId_t proposalId;
	char mapToken[ MP_SERIES_MAP_TOKEN_BYTES ];
	bool sensitiveValueRemainsInRequest;

	void Clear( void );
} mpOperationAdapterContinuation_t;

// Independent cores intentionally retain independent revisions.  Any adapter
// calling an operation that touches one of them must supply the corresponding
// observed state.  Rule digests close the gap left by staged snapshots, whose
// revision is not represented by the protocol envelope.
typedef struct mpOperationAdapterContext_s {
	int trustedTransportSlot;
	bool localOperator;
	bool preauthenticatedRefereeGrant;
	// Non-team competition sides are adapter-owned because a Duel participant
	// intentionally has no gameplay team.  The binding is connection-scoped;
	// it never confers a general team or roster capability.
	bool actorCompetitionContestant;
	int actorCompetitionSide;
	// The actor's side in the unfinished series, from the resolver the published
	// view also uses.  A team series can bind a seat to its series side or remap
	// gameplay sides between maps, so the gameplay side is not the veto side.
	// MP_MATCH_SIDE_NONE outside a series.
	int actorSeriesSide;
	// openQ4 MP-12: on a casual team server (si_timeouts) an active human on a
	// side may call and end that side's own timeouts without a captain seat.
	// Set by the adapter; it grants nothing else.
	bool casualSideTimeout;
	// A failed adapter bootstrap leaves the previous aggregate resident only so
	// its sealed evidence/checkpoint can be retried.  No typed operation may
	// mutate that aggregate until a complete session is operational again.
	bool sessionOperational;
	// False while the live adapter has not completed a usable authoritative
	// session or a recovered Duel still needs explicit contestant bindings.
	// FORCE_READY must honor this just like the legacy countdown path.
	bool countdownPrerequisitesSatisfied;
	bool cooldownPolicyAccepted;
	mpMatchEngineTime engineTime;

	mpMatchRulesValidationContext_t validatedRuleContext;
	int ruleGameType;
	mpRuleCommitBoundary_t ruleBoundary;
	uint32_t expectedRulesRevision;
	uint64_t expectedRulesDigest;
	bool expectedStagedRules;
	uint64_t expectedStagedRulesDigest;

	mpProposalRevision_t expectedProposalRevision;
	uint64_t expectedSeriesRevision;
	// A validated completed-map checkpoint restored into a new warmup session.
	// This is server-owned context, never a client request argument.
	bool seriesReviewRecovered;

	mpOperationAdapterContext_s( void );
} mpOperationAdapterContext_t;

typedef struct mpOperationExecutionResult_s {
	mpOperationOutcome_t outcome;
	mpOperationReason_t reason;
	mpMatchProtocolReason_t protocolReason;
	mpMatchReason_t sessionReason;
	mpRuleValidationFailure_t ruleFailure;
	mpProposalReason_t proposalReason;
	mpSeriesReason_t seriesReason;
	uint64_t resultingSessionRevision;
	uint32_t resultingRulesRevision;
	uint64_t resultingRulesDigest;
	mpProposalRevision_t resultingProposalRevision;
	uint64_t resultingSeriesRevision;
	mpOperationAdapterContinuation_t continuation;

	void Clear( void );
	bool Succeeded( void ) const;
} mpOperationExecutionResult_t;

/*
===============================================================================

	Explicit policy mappings

	Protocol masks and session masks are separate domains.  These helpers are
	the only supported bridge; callers must never cast between them.  A mapping
	may expose alternative session capabilities because ownership checks (for
	example own-team captain versus referee) are completed by the executor.

===============================================================================
*/

typedef struct mpOperationCapabilityPolicy_s {
	mpMatchProtocolCapabilityMask_t protocolCapability;
	mpMatchCapabilityMask_t anySessionCapability;
	bool localOperatorAllowed;
	bool refereeGrantAllowed;
} mpOperationCapabilityPolicy_t;

const mpOperationCapabilityPolicy_t *MPOperationCapabilityPolicy(
	mpMatchProtocolCapabilityMask_t protocolCapability );

bool MPOperationMapProtocolRosterRole( unsigned short protocolRole,
	mpMatchRosterRole_t &rosterRole, mpMatchRoleMask_t &principalRoles );
bool MPOperationBroadcasterTargetIsValid( const mpMatchSession &session,
	mpParticipantId participant );

bool MPOperationMapProtocolTeam( mpMatchTeam_t protocolTeam, int &side );
bool MPOperationMapProtocolCompetitionSide( unsigned short protocolSide,
	int &competitionSide );

/*
===============================================================================

	Single operation pipeline

	ExecutePassedProposal deliberately calls the same internal dispatcher as a
	direct request.  The proposal proof changes only the capability policy; the
	actor binding, current participant state, phase, revisions and arguments are
	all resolved again.  Successful execution is acknowledged separately so a
	cross-core partial commit is impossible.

===============================================================================
*/

class mpMatchOperationExecutor {
public:
	mpOperationExecutionResult_t Execute(
		const mpMatchOperationRequest_t &request,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series ) const;

	mpOperationExecutionResult_t ExecutePassedProposal(
		mpProposalScope_t scope,
		mpProposalId_t proposalId,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series ) const;

	mpOperationExecutionResult_t AcknowledgePassedProposal(
		mpProposalScope_t scope,
		mpProposalId_t proposalId,
		const mpOperationAdapterContext_t &context,
		const mpMatchSession &session,
		const mpCompetitiveRules &rules,
		mpProposalService &proposals,
		const mpCompetitionSeries &series ) const;

private:
	mpOperationExecutionResult_t ExecuteInternal(
		const mpMatchOperationRequest_t &request,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series,
		bool passedProposalAuthority ) const;
};

#endif // __MP_MATCH_OPERATIONS_H__
