//----------------------------------------------------------------
// MatchProposal.h
//
// Allocation-free authoritative service for typed competitive proposals.
// Electorates, thresholds, deadlines and target operations are frozen at
// creation.  This service never executes the operation it carries; a passed
// operation remains available for the match adapter to authorize and
// revalidate against current session state before one authoritative commit.
//----------------------------------------------------------------

#ifndef __MP_MATCH_PROPOSAL_H__
#define __MP_MATCH_PROPOSAL_H__

#include "MatchProtocol.h"

#include <stdint.h>

static const int MP_PROPOSAL_MAX_ELECTORATE = 32;
static const int MP_PROPOSAL_SCOPE_COUNT = 3;
static const int MP_PROPOSAL_MAX_LIFETIME_MSEC = 60 * 60 * 1000;
static const int MP_PROPOSAL_MAX_COOLDOWN_MSEC = 24 * 60 * 60 * 1000;

typedef mpMatchProtocolSessionId_t mpProposalSessionId_t;
typedef unsigned long long mpProposalId_t;
typedef unsigned long long mpProposalRevision_t;
typedef mpMatchProtocolParticipantId_t mpProposalParticipantId_t;

typedef enum {
	MP_PROPOSAL_SCOPE_GLOBAL = 0,
	MP_PROPOSAL_SCOPE_TEAM_A = 1,
	MP_PROPOSAL_SCOPE_TEAM_B = 2
} mpProposalScope_t;

typedef unsigned int mpProposalScopeMask_t;
static const mpProposalScopeMask_t MP_PROPOSAL_SCOPE_MASK_GLOBAL =
	( 1u << MP_PROPOSAL_SCOPE_GLOBAL );
static const mpProposalScopeMask_t MP_PROPOSAL_SCOPE_MASK_TEAM_A =
	( 1u << MP_PROPOSAL_SCOPE_TEAM_A );
static const mpProposalScopeMask_t MP_PROPOSAL_SCOPE_MASK_TEAM_B =
	( 1u << MP_PROPOSAL_SCOPE_TEAM_B );
static const mpProposalScopeMask_t MP_PROPOSAL_SCOPE_MASK_ALL =
	( 1u << MP_PROPOSAL_SCOPE_COUNT ) - 1u;

typedef enum {
	MP_PROPOSAL_STATUS_EMPTY = 0,
	MP_PROPOSAL_STATUS_ACTIVE,
	MP_PROPOSAL_STATUS_PASSED,
	MP_PROPOSAL_STATUS_FAILED,
	MP_PROPOSAL_STATUS_EXPIRED,
	MP_PROPOSAL_STATUS_CANCELLED,
	MP_PROPOSAL_STATUS_PHASE_INVALIDATED,
	MP_PROPOSAL_STATUS_COUNT
} mpProposalStatus_t;

typedef enum {
	MP_PROPOSAL_BALLOT_NONE = 0,
	MP_PROPOSAL_BALLOT_YES,
	MP_PROPOSAL_BALLOT_NO,
	MP_PROPOSAL_BALLOT_ABSTAIN,
	MP_PROPOSAL_BALLOT_COUNT
} mpProposalBallot_t;

typedef enum {
	MP_PROPOSAL_CALLER_VOTE_NONE = 0,
	MP_PROPOSAL_CALLER_VOTE_YES,
	MP_PROPOSAL_CALLER_VOTE_NO,
	MP_PROPOSAL_CALLER_VOTE_ABSTAIN,
	MP_PROPOSAL_CALLER_VOTE_POLICY_COUNT
} mpProposalCallerVotePolicy_t;

typedef enum {
	MP_PROPOSAL_CANCEL_PROPOSER = 1,
	MP_PROPOSAL_CANCEL_REFEREE,
	MP_PROPOSAL_CANCEL_SERVER_OPERATOR,
	MP_PROPOSAL_CANCEL_SESSION_SHUTDOWN,
	MP_PROPOSAL_CANCEL_REASON_COUNT
} mpProposalCancellationReason_t;

typedef enum {
	MP_PROPOSAL_MUTATION_APPLIED = 0,
	MP_PROPOSAL_MUTATION_NO_CHANGE,
	MP_PROPOSAL_MUTATION_REJECTED,
	MP_PROPOSAL_MUTATION_CODE_COUNT
} mpProposalMutationCode_t;

// Append-only result values.  They are evidence-friendly identifiers, not UI
// text.  Presentation maps them to language-table identifiers elsewhere.
typedef enum {
	MP_PROPOSAL_REASON_NONE = 0,
	MP_PROPOSAL_REASON_INVALID_ARGUMENT,
	MP_PROPOSAL_REASON_INVALID_SESSION_ID,
	MP_PROPOSAL_REASON_SESSION_MISMATCH,
	MP_PROPOSAL_REASON_INVALID_SCOPE,
	MP_PROPOSAL_REASON_INVALID_PROPOSAL_ID,
	MP_PROPOSAL_REASON_STALE_REVISION,
	MP_PROPOSAL_REASON_REVISION_EXHAUSTED,
	MP_PROPOSAL_REASON_CLOCK_INVALID,
	MP_PROPOSAL_REASON_CLOCK_REGRESSION,
	MP_PROPOSAL_REASON_DEADLINE_INVALID,
	MP_PROPOSAL_REASON_DEADLINE_REACHED,
	MP_PROPOSAL_REASON_SLOT_OCCUPIED,
	MP_PROPOSAL_REASON_SLOT_EMPTY,
	MP_PROPOSAL_REASON_PROPOSAL_MISMATCH,
	MP_PROPOSAL_REASON_NOT_ACTIVE,
	MP_PROPOSAL_REASON_NOT_TERMINAL,
	MP_PROPOSAL_REASON_ELECTORATE_COUNT,
	MP_PROPOSAL_REASON_ELECTORATE_MEMBER_INVALID,
	MP_PROPOSAL_REASON_ELECTORATE_MEMBER_NOT_HUMAN,
	MP_PROPOSAL_REASON_ELECTORATE_DUPLICATE,
	MP_PROPOSAL_REASON_THRESHOLD_INVALID,
	MP_PROPOSAL_REASON_CALLER_NOT_ELECTOR,
	MP_PROPOSAL_REASON_BALLOT_INVALID,
	MP_PROPOSAL_REASON_ALREADY_VOTED,
	MP_PROPOSAL_REASON_OPERATION_INVALID,
	MP_PROPOSAL_REASON_OPERATION_NOT_PROPOSABLE,
	MP_PROPOSAL_REASON_COOLDOWN_ACTIVE,
	MP_PROPOSAL_REASON_COOLDOWN_POLICY_INVALID,
	MP_PROPOSAL_REASON_PHASE_INVALID,
	MP_PROPOSAL_REASON_INVARIANT,
	MP_PROPOSAL_REASON_COUNT
} mpProposalReason_t;

/*
===============================================================================

	Strong engine/network clock value

	No method in this service reads a host or wall clock.  Every time-bearing
	mutation receives this value explicitly from the authoritative engine loop.

===============================================================================
*/
class mpProposalEngineTime {
public:
					mpProposalEngineTime( void );

	static mpProposalEngineTime FromMilliseconds( int64_t value );
	bool			IsValid( void ) const;
	int64_t		Milliseconds( void ) const;

	bool			operator==( const mpProposalEngineTime &rhs ) const;
	bool			operator!=( const mpProposalEngineTime &rhs ) const;
	bool			operator<( const mpProposalEngineTime &rhs ) const;
	bool			operator<=( const mpProposalEngineTime &rhs ) const;
	bool			operator>( const mpProposalEngineTime &rhs ) const;
	bool			operator>=( const mpProposalEngineTime &rhs ) const;

private:
	explicit		mpProposalEngineTime( int64_t value );
	int64_t			msec;
};

typedef struct mpProposalElectorateInput_s {
	mpProposalParticipantId_t participant;
	bool				human;
} mpProposalElectorateInput_t;

typedef struct mpProposalElectorState_s {
	mpProposalParticipantId_t participant;
	mpProposalBallot_t	ballot;
} mpProposalElectorState_t;

typedef struct mpProposalCooldownPolicy_s {
	int durationMsec[ MP_MATCH_COOLDOWN_COUNT ];

	void Clear( void );
	bool IsValid( void ) const;
} mpProposalCooldownPolicy_t;

typedef struct mpProposalCooldownState_s {
	bool				hasDeadline;
	mpProposalEngineTime until;
	mpProposalId_t		lastProposalId;
	mpProposalStatus_t	lastTerminalStatus;

	void Clear( void );
} mpProposalCooldownState_t;

typedef struct mpProposalCreateParams_s {
	mpProposalSessionId_t	sessionId;
	mpProposalId_t		proposalId;
	mpProposalScope_t		scope;
	unsigned char			electorateCount;
	mpProposalElectorateInput_t electorate[ MP_PROPOSAL_MAX_ELECTORATE ];
	unsigned char			requiredQuorum;
	unsigned char			requiredYes;
	mpProposalEngineTime	createdAt;
	mpProposalEngineTime	expiresAt;
	mpProposalParticipantId_t caller;
	mpProposalCallerVotePolicy_t callerVotePolicy;
	mpMatchOperationRequest_t operation;

	void Clear( void );
} mpProposalCreateParams_t;

typedef struct mpProposalRecord_s {
	mpProposalSessionId_t	sessionId;
	mpProposalId_t		proposalId;
	mpProposalScope_t		scope;
	mpProposalStatus_t		status;
	unsigned char			electorateCount;
	mpProposalElectorState_t electorate[ MP_PROPOSAL_MAX_ELECTORATE ];
	unsigned char			requiredQuorum;
	unsigned char			requiredYes;
	unsigned char			castCount;
	unsigned char			yesCount;
	unsigned char			noCount;
	unsigned char			abstainCount;
	mpProposalEngineTime	createdAt;
	mpProposalEngineTime	expiresAt;
	mpProposalEngineTime	terminalAt;
	mpProposalParticipantId_t caller;
	mpProposalCallerVotePolicy_t callerVotePolicy;
	mpMatchLocalizationId_t operationLocalizationId;
	mpMatchPhaseMask_t		legalPhaseMask;
	mpMatchCooldownClass_t	cooldownClass;
	mpProposalCancellationReason_t cancellationReason;
	mpMatchOperationRequest_t operation;

	void Clear( void );
	bool IsOccupied( void ) const;
	bool IsActive( void ) const;
	bool IsTerminal( void ) const;
} mpProposalRecord_t;

typedef struct mpProposalMutationResult_s {
	mpProposalMutationCode_t code;
	mpProposalReason_t		reason;
	mpProposalRevision_t	previousRevision;
	mpProposalRevision_t	currentRevision;
	mpProposalScopeMask_t	affectedScopes;
	mpProposalStatus_t		status;
	mpProposalId_t		proposalId;

	bool WasApplied( void ) const;
	bool WasRejected( void ) const;
} mpProposalMutationResult_t;

/*
===============================================================================

	mpProposalService

	One fixed slot exists for each scope.  Terminal records are retained until
	the adapter acknowledges them, guaranteeing that a passed operation cannot
	be overwritten before it is revalidated and consumed.

===============================================================================
*/
class mpProposalService {
public:
					mpProposalService( void );

	bool			Reset( mpProposalSessionId_t sessionId,
						mpProposalEngineTime initialEngineTime,
						const mpProposalCooldownPolicy_t &cooldownPolicy );

	mpProposalSessionId_t GetSessionId( void ) const;
	mpProposalRevision_t GetRevision( void ) const;
	mpProposalEngineTime GetLastEngineTime( void ) const;
	mpProposalScopeMask_t GetActiveScopeMask( void ) const;
	mpProposalScopeMask_t GetOccupiedScopeMask( void ) const;
	const mpProposalRecord_t *GetProposal( mpProposalScope_t scope ) const;
	const mpProposalCooldownState_t *GetCooldown( mpMatchOperationOpcode_t opcode ) const;

	mpProposalMutationResult_t Create( const mpProposalCreateParams_t &params,
						mpProposalRevision_t expectedRevision );
	mpProposalMutationResult_t CastBallot( mpProposalSessionId_t sessionId,
						mpProposalScope_t scope, mpProposalId_t proposalId,
						mpProposalParticipantId_t participant, mpProposalBallot_t ballot,
						mpProposalEngineTime engineNow, mpProposalRevision_t expectedRevision );
	mpProposalMutationResult_t Cancel( mpProposalSessionId_t sessionId,
						mpProposalScope_t scope, mpProposalId_t proposalId,
						mpProposalCancellationReason_t cancellationReason,
						mpProposalEngineTime engineNow, mpProposalRevision_t expectedRevision );
	mpProposalMutationResult_t Expire( mpProposalSessionId_t sessionId,
						mpProposalEngineTime engineNow, mpProposalRevision_t expectedRevision );
	mpProposalMutationResult_t InvalidateForPhase( mpProposalSessionId_t sessionId,
						mpGameState_t phase, mpProposalEngineTime engineNow,
						mpProposalRevision_t expectedRevision );
	mpProposalMutationResult_t Acknowledge( mpProposalSessionId_t sessionId,
						mpProposalScope_t scope, mpProposalId_t proposalId,
						mpProposalRevision_t expectedRevision );

	bool			ValidateInvariants( void ) const;

private:
	mpProposalMutationResult_t Applied( mpProposalRevision_t previousRevision,
						mpProposalScopeMask_t affectedScopes,
						mpProposalStatus_t status, mpProposalId_t proposalId );
	mpProposalMutationResult_t NoChange( void ) const;
	mpProposalMutationResult_t Rejected( mpProposalReason_t reason ) const;
	bool			CanMutate( mpProposalSessionId_t requestedSessionId,
						mpProposalRevision_t expectedRevision,
						mpProposalReason_t &reason ) const;
	bool			ValidateTime( mpProposalEngineTime engineNow,
						mpProposalReason_t &reason ) const;
	bool			FinishRecord( mpProposalRecord_t &record, mpProposalStatus_t status,
						mpProposalEngineTime engineNow, mpProposalCooldownState_t &cooldown ) const;
	bool			EvaluateRecord( mpProposalRecord_t &record,
						mpProposalEngineTime engineNow, mpProposalCooldownState_t &cooldown ) const;
	int			FindElector( const mpProposalRecord_t &record,
						mpProposalParticipantId_t participant ) const;

	mpProposalSessionId_t sessionId;
	mpProposalRevision_t serviceRevision;
	mpProposalEngineTime lastEngineTime;
	mpProposalCooldownPolicy_t cooldownPolicy;
	mpProposalRecord_t proposals[ MP_PROPOSAL_SCOPE_COUNT ];
	mpProposalCooldownState_t cooldowns[ MP_MATCH_OP_COUNT ];
};

#endif // __MP_MATCH_PROPOSAL_H__
