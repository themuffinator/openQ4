//----------------------------------------------------------------
// MatchProposal.cpp
//----------------------------------------------------------------

#if defined( MP_PROPOSAL_STANDALONE_TEST )
	#include "MatchProposal.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchProposal.h"
#endif

#include <limits.h>
#include <string.h>

static_assert( MP_PROPOSAL_MAX_ELECTORATE == 32,
	"proposal electorate ceiling is a stable competitive-service limit" );
static_assert( MP_PROPOSAL_SCOPE_COUNT == 3,
	"proposal service requires exactly global and two team scopes" );
static_assert( MP_PROPOSAL_MAX_ELECTORATE <= 255,
	"proposal electorate counters no longer fit their storage" );
static_assert( sizeof( mpProposalSessionId_t ) == 8,
	"proposal session identity must remain 64-bit" );
static_assert( sizeof( mpProposalId_t ) == 8,
	"proposal identity must remain 64-bit" );
static_assert( sizeof( mpProposalRevision_t ) == 8,
	"proposal service revision must remain 64-bit" );

namespace {

static bool mpProposalScopeIsValid( mpProposalScope_t scope ) {
	return scope >= MP_PROPOSAL_SCOPE_GLOBAL && scope < MP_PROPOSAL_SCOPE_COUNT;
}

static mpProposalScopeMask_t mpProposalScopeBit( mpProposalScope_t scope ) {
	return mpProposalScopeIsValid( scope ) ? ( 1u << static_cast<unsigned int>( scope ) ) : 0u;
}

static bool mpProposalStatusIsTerminal( mpProposalStatus_t status ) {
	return status == MP_PROPOSAL_STATUS_PASSED ||
		status == MP_PROPOSAL_STATUS_FAILED ||
		status == MP_PROPOSAL_STATUS_EXPIRED ||
		status == MP_PROPOSAL_STATUS_CANCELLED ||
		status == MP_PROPOSAL_STATUS_PHASE_INVALIDATED;
}

static mpProposalBallot_t mpProposalBallotForCallerPolicy(
	mpProposalCallerVotePolicy_t policy ) {
	switch ( policy ) {
		case MP_PROPOSAL_CALLER_VOTE_YES:
			return MP_PROPOSAL_BALLOT_YES;
		case MP_PROPOSAL_CALLER_VOTE_NO:
			return MP_PROPOSAL_BALLOT_NO;
		case MP_PROPOSAL_CALLER_VOTE_ABSTAIN:
			return MP_PROPOSAL_BALLOT_ABSTAIN;
		default:
			return MP_PROPOSAL_BALLOT_NONE;
	}
}

static bool mpProposalAddDuration( mpProposalEngineTime base, int durationMsec,
	mpProposalEngineTime &result ) {
	if ( !base.IsValid() || durationMsec < 0 ||
		base.Milliseconds() > INT64_MAX - static_cast<int64_t>( durationMsec ) ) {
		return false;
	}
	result = mpProposalEngineTime::FromMilliseconds(
		base.Milliseconds() + static_cast<int64_t>( durationMsec ) );
	return true;
}

static void mpProposalApplyBallot( mpProposalRecord_t &record, int electorIndex,
	mpProposalBallot_t ballot ) {
	record.electorate[ electorIndex ].ballot = ballot;
	++record.castCount;
	switch ( ballot ) {
		case MP_PROPOSAL_BALLOT_YES:
			++record.yesCount;
			break;
		case MP_PROPOSAL_BALLOT_NO:
			++record.noCount;
			break;
		case MP_PROPOSAL_BALLOT_ABSTAIN:
			++record.abstainCount;
			break;
		default:
			break;
	}
}

} // namespace

mpProposalEngineTime::mpProposalEngineTime( void ) : msec( -1 ) {
}

mpProposalEngineTime::mpProposalEngineTime( int64_t value ) : msec( value ) {
}

mpProposalEngineTime mpProposalEngineTime::FromMilliseconds( int64_t value ) {
	return mpProposalEngineTime( value );
}

bool mpProposalEngineTime::IsValid( void ) const {
	return msec >= 0;
}

int64_t mpProposalEngineTime::Milliseconds( void ) const {
	return msec;
}

bool mpProposalEngineTime::operator==( const mpProposalEngineTime &rhs ) const {
	return msec == rhs.msec;
}

bool mpProposalEngineTime::operator!=( const mpProposalEngineTime &rhs ) const {
	return msec != rhs.msec;
}

bool mpProposalEngineTime::operator<( const mpProposalEngineTime &rhs ) const {
	return msec < rhs.msec;
}

bool mpProposalEngineTime::operator<=( const mpProposalEngineTime &rhs ) const {
	return msec <= rhs.msec;
}

bool mpProposalEngineTime::operator>( const mpProposalEngineTime &rhs ) const {
	return msec > rhs.msec;
}

bool mpProposalEngineTime::operator>=( const mpProposalEngineTime &rhs ) const {
	return msec >= rhs.msec;
}

void mpProposalCooldownPolicy_t::Clear( void ) {
	for ( int i = 0; i < MP_MATCH_COOLDOWN_COUNT; ++i ) {
		durationMsec[ i ] = 0;
	}
}

bool mpProposalCooldownPolicy_t::IsValid( void ) const {
	for ( int i = 0; i < MP_MATCH_COOLDOWN_COUNT; ++i ) {
		if ( durationMsec[ i ] < 0 || durationMsec[ i ] > MP_PROPOSAL_MAX_COOLDOWN_MSEC ) {
			return false;
		}
	}
	return true;
}

void mpProposalCooldownState_t::Clear( void ) {
	hasDeadline = false;
	until = mpProposalEngineTime();
	lastProposalId = 0;
	lastTerminalStatus = MP_PROPOSAL_STATUS_EMPTY;
}

void mpProposalCreateParams_t::Clear( void ) {
	sessionId = 0;
	proposalId = 0;
	scope = MP_PROPOSAL_SCOPE_GLOBAL;
	electorateCount = 0;
	for ( int i = 0; i < MP_PROPOSAL_MAX_ELECTORATE; ++i ) {
		electorate[ i ].participant = MP_MATCH_INVALID_PARTICIPANT_ID;
		electorate[ i ].human = false;
	}
	requiredQuorum = 0;
	requiredYes = 0;
	createdAt = mpProposalEngineTime();
	expiresAt = mpProposalEngineTime();
	caller = MP_MATCH_INVALID_PARTICIPANT_ID;
	callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_NONE;
	memset( &operation, 0, sizeof( operation ) );
	operation.schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	operation.opcode = MP_MATCH_OP_INVALID;
	operation.teamTarget = MP_MATCH_TEAM_NONE;
}

void mpProposalRecord_t::Clear( void ) {
	sessionId = 0;
	proposalId = 0;
	scope = MP_PROPOSAL_SCOPE_GLOBAL;
	status = MP_PROPOSAL_STATUS_EMPTY;
	electorateCount = 0;
	for ( int i = 0; i < MP_PROPOSAL_MAX_ELECTORATE; ++i ) {
		electorate[ i ].participant = MP_MATCH_INVALID_PARTICIPANT_ID;
		electorate[ i ].ballot = MP_PROPOSAL_BALLOT_NONE;
	}
	requiredQuorum = 0;
	requiredYes = 0;
	castCount = 0;
	yesCount = 0;
	noCount = 0;
	abstainCount = 0;
	createdAt = mpProposalEngineTime();
	expiresAt = mpProposalEngineTime();
	terminalAt = mpProposalEngineTime();
	caller = MP_MATCH_INVALID_PARTICIPANT_ID;
	callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_NONE;
	operationLocalizationId = MP_MATCH_LOCALIZATION_NONE;
	legalPhaseMask = 0;
	cooldownClass = MP_MATCH_COOLDOWN_NONE;
	cancellationReason = static_cast<mpProposalCancellationReason_t>( 0 );
	memset( &operation, 0, sizeof( operation ) );
	operation.schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	operation.opcode = MP_MATCH_OP_INVALID;
	operation.teamTarget = MP_MATCH_TEAM_NONE;
}

bool mpProposalRecord_t::IsOccupied( void ) const {
	return status != MP_PROPOSAL_STATUS_EMPTY;
}

bool mpProposalRecord_t::IsActive( void ) const {
	return status == MP_PROPOSAL_STATUS_ACTIVE;
}

bool mpProposalRecord_t::IsTerminal( void ) const {
	return mpProposalStatusIsTerminal( status );
}

bool mpProposalMutationResult_t::WasApplied( void ) const {
	return code == MP_PROPOSAL_MUTATION_APPLIED;
}

bool mpProposalMutationResult_t::WasRejected( void ) const {
	return code == MP_PROPOSAL_MUTATION_REJECTED;
}

mpProposalService::mpProposalService( void ) :
	sessionId( 0 ),
	serviceRevision( 0 ),
	lastEngineTime(),
	cooldownPolicy() {
	cooldownPolicy.Clear();
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		proposals[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		cooldowns[ i ].Clear();
	}
}

bool mpProposalService::Reset( mpProposalSessionId_t newSessionId,
	mpProposalEngineTime initialEngineTime,
	const mpProposalCooldownPolicy_t &newCooldownPolicy ) {
	if ( newSessionId == 0 || !initialEngineTime.IsValid() || !newCooldownPolicy.IsValid() ) {
		return false;
	}
	sessionId = newSessionId;
	serviceRevision = 1;
	lastEngineTime = initialEngineTime;
	cooldownPolicy = newCooldownPolicy;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		proposals[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		cooldowns[ i ].Clear();
	}
	return ValidateInvariants();
}

mpProposalSessionId_t mpProposalService::GetSessionId( void ) const {
	return sessionId;
}

mpProposalRevision_t mpProposalService::GetRevision( void ) const {
	return serviceRevision;
}

mpProposalEngineTime mpProposalService::GetLastEngineTime( void ) const {
	return lastEngineTime;
}

mpProposalScopeMask_t mpProposalService::GetActiveScopeMask( void ) const {
	mpProposalScopeMask_t mask = 0;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		if ( proposals[ i ].IsActive() ) {
			mask |= 1u << i;
		}
	}
	return mask;
}

mpProposalScopeMask_t mpProposalService::GetOccupiedScopeMask( void ) const {
	mpProposalScopeMask_t mask = 0;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		if ( proposals[ i ].IsOccupied() ) {
			mask |= 1u << i;
		}
	}
	return mask;
}

const mpProposalRecord_t *mpProposalService::GetProposal( mpProposalScope_t scope ) const {
	return mpProposalScopeIsValid( scope ) ? &proposals[ scope ] : 0;
}

const mpProposalCooldownState_t *mpProposalService::GetCooldown(
	mpMatchOperationOpcode_t opcode ) const {
	return opcode > MP_MATCH_OP_INVALID && opcode < MP_MATCH_OP_COUNT ? &cooldowns[ opcode ] : 0;
}

mpProposalMutationResult_t mpProposalService::Applied( mpProposalRevision_t previousRevision,
	mpProposalScopeMask_t affectedScopes, mpProposalStatus_t status,
	mpProposalId_t proposalId ) {
	++serviceRevision;
	mpProposalMutationResult_t result;
	result.code = MP_PROPOSAL_MUTATION_APPLIED;
	result.reason = MP_PROPOSAL_REASON_NONE;
	result.previousRevision = previousRevision;
	result.currentRevision = serviceRevision;
	result.affectedScopes = affectedScopes;
	result.status = status;
	result.proposalId = proposalId;
	return result;
}

mpProposalMutationResult_t mpProposalService::NoChange( void ) const {
	mpProposalMutationResult_t result;
	result.code = MP_PROPOSAL_MUTATION_NO_CHANGE;
	result.reason = MP_PROPOSAL_REASON_NONE;
	result.previousRevision = serviceRevision;
	result.currentRevision = serviceRevision;
	result.affectedScopes = 0;
	result.status = MP_PROPOSAL_STATUS_EMPTY;
	result.proposalId = 0;
	return result;
}

mpProposalMutationResult_t mpProposalService::Rejected( mpProposalReason_t reason ) const {
	mpProposalMutationResult_t result;
	result.code = MP_PROPOSAL_MUTATION_REJECTED;
	result.reason = reason;
	result.previousRevision = serviceRevision;
	result.currentRevision = serviceRevision;
	result.affectedScopes = 0;
	result.status = MP_PROPOSAL_STATUS_EMPTY;
	result.proposalId = 0;
	return result;
}

bool mpProposalService::CanMutate( mpProposalSessionId_t requestedSessionId,
	mpProposalRevision_t expectedRevision, mpProposalReason_t &reason ) const {
	if ( requestedSessionId == 0 ) {
		reason = MP_PROPOSAL_REASON_INVALID_SESSION_ID;
		return false;
	}
	if ( sessionId == 0 || requestedSessionId != sessionId ) {
		reason = MP_PROPOSAL_REASON_SESSION_MISMATCH;
		return false;
	}
	if ( expectedRevision != serviceRevision ) {
		reason = MP_PROPOSAL_REASON_STALE_REVISION;
		return false;
	}
	if ( serviceRevision == ~static_cast<mpProposalRevision_t>( 0 ) ) {
		reason = MP_PROPOSAL_REASON_REVISION_EXHAUSTED;
		return false;
	}
	reason = MP_PROPOSAL_REASON_NONE;
	return true;
}

bool mpProposalService::ValidateTime( mpProposalEngineTime engineNow,
	mpProposalReason_t &reason ) const {
	if ( !engineNow.IsValid() ) {
		reason = MP_PROPOSAL_REASON_CLOCK_INVALID;
		return false;
	}
	if ( !lastEngineTime.IsValid() || engineNow < lastEngineTime ) {
		reason = MP_PROPOSAL_REASON_CLOCK_REGRESSION;
		return false;
	}
	reason = MP_PROPOSAL_REASON_NONE;
	return true;
}

bool mpProposalService::FinishRecord( mpProposalRecord_t &record,
	mpProposalStatus_t status, mpProposalEngineTime engineNow,
	mpProposalCooldownState_t &cooldown ) const {
	if ( !record.IsActive() || !mpProposalStatusIsTerminal( status ) ||
		record.cooldownClass < MP_MATCH_COOLDOWN_NONE ||
		record.cooldownClass >= MP_MATCH_COOLDOWN_COUNT ) {
		return false;
	}
	cooldown.Clear();
	cooldown.lastProposalId = record.proposalId;
	cooldown.lastTerminalStatus = status;
	const int durationMsec = cooldownPolicy.durationMsec[ record.cooldownClass ];
	if ( durationMsec > 0 ) {
		if ( !mpProposalAddDuration( engineNow, durationMsec, cooldown.until ) ) {
			return false;
		}
		cooldown.hasDeadline = true;
	}
	record.status = status;
	record.terminalAt = engineNow;
	return true;
}

bool mpProposalService::EvaluateRecord( mpProposalRecord_t &record,
	mpProposalEngineTime engineNow, mpProposalCooldownState_t &cooldown ) const {
	if ( !record.IsActive() ) {
		return false;
	}
	if ( record.yesCount >= record.requiredYes && record.castCount >= record.requiredQuorum ) {
		return FinishRecord( record, MP_PROPOSAL_STATUS_PASSED, engineNow, cooldown );
	}
	const int remaining = record.electorateCount - record.castCount;
	if ( record.yesCount + remaining < record.requiredYes ||
		record.castCount == record.electorateCount ) {
		return FinishRecord( record, MP_PROPOSAL_STATUS_FAILED, engineNow, cooldown );
	}
	return true;
}

int mpProposalService::FindElector( const mpProposalRecord_t &record,
	mpProposalParticipantId_t participant ) const {
	for ( int i = 0; i < record.electorateCount; ++i ) {
		if ( record.electorate[ i ].participant == participant ) {
			return i;
		}
	}
	return -1;
}

mpProposalMutationResult_t mpProposalService::Create(
	const mpProposalCreateParams_t &params, mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( params.sessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( !mpProposalScopeIsValid( params.scope ) ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_SCOPE );
	}
	if ( !ValidateTime( params.createdAt, reason ) ) {
		return Rejected( reason );
	}
	if ( params.proposalId == 0 ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_PROPOSAL_ID );
	}
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		if ( proposals[ i ].IsOccupied() && proposals[ i ].proposalId == params.proposalId ) {
			return Rejected( MP_PROPOSAL_REASON_INVALID_PROPOSAL_ID );
		}
	}
	if ( proposals[ params.scope ].IsOccupied() ) {
		return Rejected( MP_PROPOSAL_REASON_SLOT_OCCUPIED );
	}
	if ( !params.expiresAt.IsValid() || params.expiresAt <= params.createdAt ||
		params.expiresAt.Milliseconds() - params.createdAt.Milliseconds() >
			MP_PROPOSAL_MAX_LIFETIME_MSEC ) {
		return Rejected( MP_PROPOSAL_REASON_DEADLINE_INVALID );
	}
	if ( params.electorateCount == 0 || params.electorateCount > MP_PROPOSAL_MAX_ELECTORATE ) {
		return Rejected( MP_PROPOSAL_REASON_ELECTORATE_COUNT );
	}
	if ( params.requiredQuorum == 0 || params.requiredQuorum > params.electorateCount ||
		params.requiredYes == 0 || params.requiredYes > params.electorateCount ) {
		return Rejected( MP_PROPOSAL_REASON_THRESHOLD_INVALID );
	}
	if ( params.callerVotePolicy < MP_PROPOSAL_CALLER_VOTE_NONE ||
		params.callerVotePolicy >= MP_PROPOSAL_CALLER_VOTE_POLICY_COUNT ) {
		return Rejected( MP_PROPOSAL_REASON_BALLOT_INVALID );
	}
	for ( int i = 0; i < params.electorateCount; ++i ) {
		if ( params.electorate[ i ].participant == MP_MATCH_INVALID_PARTICIPANT_ID ) {
			return Rejected( MP_PROPOSAL_REASON_ELECTORATE_MEMBER_INVALID );
		}
		if ( !params.electorate[ i ].human ) {
			return Rejected( MP_PROPOSAL_REASON_ELECTORATE_MEMBER_NOT_HUMAN );
		}
		for ( int j = 0; j < i; ++j ) {
			if ( params.electorate[ j ].participant == params.electorate[ i ].participant ) {
				return Rejected( MP_PROPOSAL_REASON_ELECTORATE_DUPLICATE );
			}
		}
	}

	mpMatchProtocolError_t protocolError;
	memset( &protocolError, 0, sizeof( protocolError ) );
	if ( !MPMatchProtocolValidateRequest( params.operation, &protocolError ) ||
		params.operation.sessionId != params.sessionId ) {
		return Rejected( MP_PROPOSAL_REASON_OPERATION_INVALID );
	}
	const mpMatchOperationDescriptor_t *descriptor =
		MPMatchOperationDescriptor( params.operation.opcode );
	if ( descriptor == 0 ||
		( descriptor->flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) == 0 ||
		( descriptor->flags & MP_MATCH_OPERATION_FLAG_SENSITIVE ) != 0 ) {
		return Rejected( MP_PROPOSAL_REASON_OPERATION_NOT_PROPOSABLE );
	}
	if ( descriptor->cooldownClass < MP_MATCH_COOLDOWN_NONE ||
		descriptor->cooldownClass >= MP_MATCH_COOLDOWN_COUNT ) {
		return Rejected( MP_PROPOSAL_REASON_COOLDOWN_POLICY_INVALID );
	}
	const mpProposalCooldownState_t &currentCooldown = cooldowns[ params.operation.opcode ];
	if ( currentCooldown.hasDeadline && params.createdAt < currentCooldown.until ) {
		return Rejected( MP_PROPOSAL_REASON_COOLDOWN_ACTIVE );
	}

	mpProposalRecord_t record;
	record.Clear();
	record.sessionId = params.sessionId;
	record.proposalId = params.proposalId;
	record.scope = params.scope;
	record.status = MP_PROPOSAL_STATUS_ACTIVE;
	record.electorateCount = params.electorateCount;
	for ( int i = 0; i < params.electorateCount; ++i ) {
		record.electorate[ i ].participant = params.electorate[ i ].participant;
		record.electorate[ i ].ballot = MP_PROPOSAL_BALLOT_NONE;
	}
	for ( int i = 1; i < record.electorateCount; ++i ) {
		const mpProposalElectorState_t candidate = record.electorate[ i ];
		int insertion = i;
		while ( insertion > 0 &&
			record.electorate[ insertion - 1 ].participant > candidate.participant ) {
			record.electorate[ insertion ] = record.electorate[ insertion - 1 ];
			--insertion;
		}
		record.electorate[ insertion ] = candidate;
	}
	record.requiredQuorum = params.requiredQuorum;
	record.requiredYes = params.requiredYes;
	record.createdAt = params.createdAt;
	record.expiresAt = params.expiresAt;
	record.caller = params.caller;
	record.callerVotePolicy = params.callerVotePolicy;
	record.operationLocalizationId = descriptor->labelLocalizationId;
	record.legalPhaseMask = descriptor->legalPhaseMask;
	record.cooldownClass = descriptor->cooldownClass;
	record.operation = params.operation;

	const mpProposalBallot_t automaticBallot =
		mpProposalBallotForCallerPolicy( params.callerVotePolicy );
	if ( automaticBallot != MP_PROPOSAL_BALLOT_NONE ) {
		const int callerIndex = FindElector( record, params.caller );
		if ( callerIndex < 0 ) {
			return Rejected( MP_PROPOSAL_REASON_CALLER_NOT_ELECTOR );
		}
		mpProposalApplyBallot( record, callerIndex, automaticBallot );
	}

	mpProposalCooldownState_t cooldown = currentCooldown;
	if ( !EvaluateRecord( record, params.createdAt, cooldown ) ) {
		return Rejected( MP_PROPOSAL_REASON_CLOCK_INVALID );
	}
	const mpProposalRevision_t previousRevision = serviceRevision;
	proposals[ params.scope ] = record;
	if ( record.IsTerminal() ) {
		cooldowns[ params.operation.opcode ] = cooldown;
	}
	lastEngineTime = params.createdAt;
	return Applied( previousRevision, mpProposalScopeBit( params.scope ),
		record.status, record.proposalId );
}

mpProposalMutationResult_t mpProposalService::CastBallot(
	mpProposalSessionId_t requestedSessionId, mpProposalScope_t scope,
	mpProposalId_t proposalId, mpProposalParticipantId_t participant,
	mpProposalBallot_t ballot, mpProposalEngineTime engineNow,
	mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( !mpProposalScopeIsValid( scope ) ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_SCOPE );
	}
	if ( !ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( proposalId == 0 ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_PROPOSAL_ID );
	}
	if ( participant == MP_MATCH_INVALID_PARTICIPANT_ID ) {
		return Rejected( MP_PROPOSAL_REASON_ELECTORATE_MEMBER_INVALID );
	}
	if ( ballot <= MP_PROPOSAL_BALLOT_NONE || ballot >= MP_PROPOSAL_BALLOT_COUNT ) {
		return Rejected( MP_PROPOSAL_REASON_BALLOT_INVALID );
	}
	const mpProposalRecord_t &current = proposals[ scope ];
	if ( !current.IsOccupied() ) {
		return Rejected( MP_PROPOSAL_REASON_SLOT_EMPTY );
	}
	if ( current.proposalId != proposalId ) {
		return Rejected( MP_PROPOSAL_REASON_PROPOSAL_MISMATCH );
	}
	if ( !current.IsActive() ) {
		return Rejected( MP_PROPOSAL_REASON_NOT_ACTIVE );
	}
	if ( engineNow >= current.expiresAt ) {
		return Rejected( MP_PROPOSAL_REASON_DEADLINE_REACHED );
	}
	const int electorIndex = FindElector( current, participant );
	if ( electorIndex < 0 ) {
		return Rejected( MP_PROPOSAL_REASON_CALLER_NOT_ELECTOR );
	}
	if ( current.electorate[ electorIndex ].ballot != MP_PROPOSAL_BALLOT_NONE ) {
		return Rejected( MP_PROPOSAL_REASON_ALREADY_VOTED );
	}

	mpProposalRecord_t record = current;
	mpProposalCooldownState_t cooldown = cooldowns[ record.operation.opcode ];
	mpProposalApplyBallot( record, electorIndex, ballot );
	if ( !EvaluateRecord( record, engineNow, cooldown ) ) {
		return Rejected( MP_PROPOSAL_REASON_CLOCK_INVALID );
	}
	const mpProposalRevision_t previousRevision = serviceRevision;
	proposals[ scope ] = record;
	if ( record.IsTerminal() ) {
		cooldowns[ record.operation.opcode ] = cooldown;
	}
	lastEngineTime = engineNow;
	return Applied( previousRevision, mpProposalScopeBit( scope ),
		record.status, record.proposalId );
}

mpProposalMutationResult_t mpProposalService::Cancel(
	mpProposalSessionId_t requestedSessionId, mpProposalScope_t scope,
	mpProposalId_t proposalId, mpProposalCancellationReason_t cancelReason,
	mpProposalEngineTime engineNow, mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( !mpProposalScopeIsValid( scope ) ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_SCOPE );
	}
	if ( !ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( cancelReason < MP_PROPOSAL_CANCEL_PROPOSER ||
		cancelReason >= MP_PROPOSAL_CANCEL_REASON_COUNT ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_ARGUMENT );
	}
	const mpProposalRecord_t &current = proposals[ scope ];
	if ( !current.IsOccupied() ) {
		return Rejected( MP_PROPOSAL_REASON_SLOT_EMPTY );
	}
	if ( current.proposalId != proposalId ) {
		return Rejected( MP_PROPOSAL_REASON_PROPOSAL_MISMATCH );
	}
	if ( !current.IsActive() ) {
		return Rejected( MP_PROPOSAL_REASON_NOT_ACTIVE );
	}
	if ( engineNow >= current.expiresAt ) {
		return Rejected( MP_PROPOSAL_REASON_DEADLINE_REACHED );
	}

	mpProposalRecord_t record = current;
	record.cancellationReason = cancelReason;
	mpProposalCooldownState_t cooldown = cooldowns[ record.operation.opcode ];
	if ( !FinishRecord( record, MP_PROPOSAL_STATUS_CANCELLED, engineNow, cooldown ) ) {
		return Rejected( MP_PROPOSAL_REASON_CLOCK_INVALID );
	}
	const mpProposalRevision_t previousRevision = serviceRevision;
	proposals[ scope ] = record;
	cooldowns[ record.operation.opcode ] = cooldown;
	lastEngineTime = engineNow;
	return Applied( previousRevision, mpProposalScopeBit( scope ),
		record.status, record.proposalId );
}

mpProposalMutationResult_t mpProposalService::Expire(
	mpProposalSessionId_t requestedSessionId, mpProposalEngineTime engineNow,
	mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( !ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}

	mpProposalRecord_t nextProposals[ MP_PROPOSAL_SCOPE_COUNT ];
	mpProposalCooldownState_t nextCooldowns[ MP_MATCH_OP_COUNT ];
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		nextProposals[ i ] = proposals[ i ];
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		nextCooldowns[ i ] = cooldowns[ i ];
	}

	mpProposalScopeMask_t affectedScopes = 0;
	mpProposalId_t affectedProposal = 0;
	int affectedCount = 0;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		mpProposalRecord_t &record = nextProposals[ i ];
		if ( !record.IsActive() || engineNow < record.expiresAt ) {
			continue;
		}
		if ( !FinishRecord( record, MP_PROPOSAL_STATUS_EXPIRED, engineNow,
			nextCooldowns[ record.operation.opcode ] ) ) {
			return Rejected( MP_PROPOSAL_REASON_CLOCK_INVALID );
		}
		affectedScopes |= 1u << i;
		++affectedCount;
		if ( affectedCount == 1 ) {
			affectedProposal = record.proposalId;
		} else {
			affectedProposal = 0;
		}
	}
	if ( affectedScopes == 0 ) {
		return NoChange();
	}

	const mpProposalRevision_t previousRevision = serviceRevision;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		proposals[ i ] = nextProposals[ i ];
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		cooldowns[ i ] = nextCooldowns[ i ];
	}
	lastEngineTime = engineNow;
	return Applied( previousRevision, affectedScopes,
		MP_PROPOSAL_STATUS_EXPIRED, affectedProposal );
}

mpProposalMutationResult_t mpProposalService::InvalidateForPhase(
	mpProposalSessionId_t requestedSessionId, mpGameState_t phase,
	mpProposalEngineTime engineNow, mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( phase < INACTIVE || phase >= STATE_COUNT ) {
		return Rejected( MP_PROPOSAL_REASON_PHASE_INVALID );
	}
	if ( !ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}

	mpProposalRecord_t nextProposals[ MP_PROPOSAL_SCOPE_COUNT ];
	mpProposalCooldownState_t nextCooldowns[ MP_MATCH_OP_COUNT ];
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		nextProposals[ i ] = proposals[ i ];
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		nextCooldowns[ i ] = cooldowns[ i ];
	}

	const mpMatchPhaseMask_t phaseBit = 1u << static_cast<unsigned int>( phase );
	mpProposalScopeMask_t affectedScopes = 0;
	mpProposalId_t affectedProposal = 0;
	int affectedCount = 0;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		mpProposalRecord_t &record = nextProposals[ i ];
		if ( !record.IsActive() || ( record.legalPhaseMask & phaseBit ) != 0 ) {
			continue;
		}
		if ( !FinishRecord( record, MP_PROPOSAL_STATUS_PHASE_INVALIDATED, engineNow,
			nextCooldowns[ record.operation.opcode ] ) ) {
			return Rejected( MP_PROPOSAL_REASON_CLOCK_INVALID );
		}
		affectedScopes |= 1u << i;
		++affectedCount;
		if ( affectedCount == 1 ) {
			affectedProposal = record.proposalId;
		} else {
			affectedProposal = 0;
		}
	}
	if ( affectedScopes == 0 ) {
		return NoChange();
	}

	const mpProposalRevision_t previousRevision = serviceRevision;
	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		proposals[ i ] = nextProposals[ i ];
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
		cooldowns[ i ] = nextCooldowns[ i ];
	}
	lastEngineTime = engineNow;
	return Applied( previousRevision, affectedScopes,
		MP_PROPOSAL_STATUS_PHASE_INVALIDATED, affectedProposal );
}

mpProposalMutationResult_t mpProposalService::Acknowledge(
	mpProposalSessionId_t requestedSessionId, mpProposalScope_t scope,
	mpProposalId_t proposalId, mpProposalRevision_t expectedRevision ) {
	mpProposalReason_t reason = MP_PROPOSAL_REASON_NONE;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	if ( !mpProposalScopeIsValid( scope ) ) {
		return Rejected( MP_PROPOSAL_REASON_INVALID_SCOPE );
	}
	const mpProposalRecord_t &current = proposals[ scope ];
	if ( !current.IsOccupied() ) {
		return Rejected( MP_PROPOSAL_REASON_SLOT_EMPTY );
	}
	if ( current.proposalId != proposalId ) {
		return Rejected( MP_PROPOSAL_REASON_PROPOSAL_MISMATCH );
	}
	if ( !current.IsTerminal() ) {
		return Rejected( MP_PROPOSAL_REASON_NOT_TERMINAL );
	}

	const mpProposalRevision_t previousRevision = serviceRevision;
	proposals[ scope ].Clear();
	return Applied( previousRevision, mpProposalScopeBit( scope ),
		MP_PROPOSAL_STATUS_EMPTY, proposalId );
}

bool mpProposalService::ValidateInvariants( void ) const {
	if ( sessionId == 0 ) {
		if ( serviceRevision != 0 || lastEngineTime.IsValid() ) {
			return false;
		}
		for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
			if ( proposals[ i ].IsOccupied() ) {
				return false;
			}
		}
		for ( int i = 0; i < MP_MATCH_OP_COUNT; ++i ) {
			if ( cooldowns[ i ].hasDeadline || cooldowns[ i ].until.IsValid() ||
				cooldowns[ i ].lastProposalId != 0 ||
				cooldowns[ i ].lastTerminalStatus != MP_PROPOSAL_STATUS_EMPTY ) {
				return false;
			}
		}
		return cooldownPolicy.IsValid();
	}
	if ( serviceRevision == 0 || !lastEngineTime.IsValid() || !cooldownPolicy.IsValid() ) {
		return false;
	}

	for ( int i = 0; i < MP_PROPOSAL_SCOPE_COUNT; ++i ) {
		const mpProposalRecord_t &record = proposals[ i ];
		if ( !record.IsOccupied() ) {
			if ( record.status != MP_PROPOSAL_STATUS_EMPTY ) {
				return false;
			}
			continue;
		}
		if ( record.scope != i || record.sessionId != sessionId || record.proposalId == 0 ||
			record.status <= MP_PROPOSAL_STATUS_EMPTY || record.status >= MP_PROPOSAL_STATUS_COUNT ||
			record.electorateCount == 0 || record.electorateCount > MP_PROPOSAL_MAX_ELECTORATE ||
			record.requiredQuorum == 0 || record.requiredQuorum > record.electorateCount ||
			record.requiredYes == 0 || record.requiredYes > record.electorateCount ||
			!record.createdAt.IsValid() || !record.expiresAt.IsValid() ||
			record.createdAt > lastEngineTime ||
			record.expiresAt <= record.createdAt ||
			record.expiresAt.Milliseconds() - record.createdAt.Milliseconds() >
				MP_PROPOSAL_MAX_LIFETIME_MSEC ||
			record.callerVotePolicy < MP_PROPOSAL_CALLER_VOTE_NONE ||
			record.callerVotePolicy >= MP_PROPOSAL_CALLER_VOTE_POLICY_COUNT ) {
			return false;
		}
		for ( int j = 0; j < i; ++j ) {
			if ( proposals[ j ].IsOccupied() && proposals[ j ].proposalId == record.proposalId ) {
				return false;
			}
		}

		int castCount = 0;
		int yesCount = 0;
		int noCount = 0;
		int abstainCount = 0;
		for ( int j = 0; j < record.electorateCount; ++j ) {
			const mpProposalElectorState_t &elector = record.electorate[ j ];
			if ( elector.participant == MP_MATCH_INVALID_PARTICIPANT_ID ||
				elector.ballot < MP_PROPOSAL_BALLOT_NONE || elector.ballot >= MP_PROPOSAL_BALLOT_COUNT ||
				( j > 0 && record.electorate[ j - 1 ].participant >= elector.participant ) ) {
				return false;
			}
			if ( elector.ballot != MP_PROPOSAL_BALLOT_NONE ) {
				++castCount;
			}
			switch ( elector.ballot ) {
				case MP_PROPOSAL_BALLOT_YES:
					++yesCount;
					break;
				case MP_PROPOSAL_BALLOT_NO:
					++noCount;
					break;
				case MP_PROPOSAL_BALLOT_ABSTAIN:
					++abstainCount;
					break;
				default:
					break;
			}
		}
		if ( record.castCount != castCount || record.yesCount != yesCount ||
			record.noCount != noCount || record.abstainCount != abstainCount ||
			castCount != yesCount + noCount + abstainCount ) {
			return false;
		}

		const int remaining = record.electorateCount - record.castCount;
		const bool passed = record.yesCount >= record.requiredYes &&
			record.castCount >= record.requiredQuorum;
		const bool failed = !passed &&
			( record.yesCount + remaining < record.requiredYes ||
			record.castCount == record.electorateCount );
		if ( ( record.status == MP_PROPOSAL_STATUS_ACTIVE && ( passed || failed ) ) ||
			( record.status == MP_PROPOSAL_STATUS_PASSED && !passed ) ||
			( record.status == MP_PROPOSAL_STATUS_FAILED && !failed ) ) {
			return false;
		}
		if ( record.IsActive() ) {
			if ( record.terminalAt.IsValid() ) {
				return false;
			}
		} else if ( !record.IsTerminal() || !record.terminalAt.IsValid() ||
			record.terminalAt < record.createdAt || record.terminalAt > lastEngineTime ) {
			return false;
		}
		if ( record.status == MP_PROPOSAL_STATUS_EXPIRED && record.terminalAt < record.expiresAt ) {
			return false;
		}
		if ( record.status == MP_PROPOSAL_STATUS_CANCELLED ) {
			if ( record.cancellationReason < MP_PROPOSAL_CANCEL_PROPOSER ||
				record.cancellationReason >= MP_PROPOSAL_CANCEL_REASON_COUNT ) {
				return false;
			}
		} else if ( record.cancellationReason != 0 ) {
			return false;
		}

		const mpProposalBallot_t automaticBallot =
			mpProposalBallotForCallerPolicy( record.callerVotePolicy );
		if ( automaticBallot != MP_PROPOSAL_BALLOT_NONE ) {
			const int callerIndex = FindElector( record, record.caller );
			if ( callerIndex < 0 || record.electorate[ callerIndex ].ballot != automaticBallot ) {
				return false;
			}
		}

		mpMatchProtocolError_t protocolError;
		memset( &protocolError, 0, sizeof( protocolError ) );
		const mpMatchOperationDescriptor_t *descriptor =
			MPMatchOperationDescriptor( record.operation.opcode );
		if ( descriptor == 0 || !MPMatchProtocolValidateRequest( record.operation, &protocolError ) ||
			record.operation.sessionId != sessionId ||
			( descriptor->flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) == 0 ||
			( descriptor->flags & MP_MATCH_OPERATION_FLAG_SENSITIVE ) != 0 ||
			record.operationLocalizationId != descriptor->labelLocalizationId ||
			record.legalPhaseMask != descriptor->legalPhaseMask ||
			record.cooldownClass != descriptor->cooldownClass ) {
			return false;
		}
	}

	for ( int opcode = MP_MATCH_OP_INVALID; opcode < MP_MATCH_OP_COUNT; ++opcode ) {
		const mpProposalCooldownState_t &cooldown = cooldowns[ opcode ];
		if ( cooldown.lastProposalId == 0 ) {
			if ( cooldown.hasDeadline || cooldown.until.IsValid() ||
				cooldown.lastTerminalStatus != MP_PROPOSAL_STATUS_EMPTY ) {
				return false;
			}
			continue;
		}
		if ( opcode == MP_MATCH_OP_INVALID ||
			!mpProposalStatusIsTerminal( cooldown.lastTerminalStatus ) ||
			( cooldown.hasDeadline && !cooldown.until.IsValid() ) ||
			( !cooldown.hasDeadline && cooldown.until.IsValid() ) ) {
			return false;
		}
	}
	return true;
}
