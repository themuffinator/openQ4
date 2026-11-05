//----------------------------------------------------------------
// MatchOperations.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_OPERATIONS_STANDALONE_TEST )
	#include "MatchOperations.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchOperations.h"
#endif

#include <string.h>

namespace {

#define MP_OPERATION_ARRAY_COUNT( value ) \
	( static_cast<int>( sizeof( value ) / sizeof( ( value )[ 0 ] ) ) )

static const unsigned int MP_OPERATION_INT_MAX = 2147483647u;

static const mpOperationCapabilityPolicy_t capabilityPolicies[] = {
	{ MP_MATCH_PROTOCOL_CAP_READY_SELF,
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_READY ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_READY_TEAM,
		MPMatchCapabilityBit( MP_MATCH_CAP_TEAM_READY ), true, false },
	{ MP_MATCH_PROTOCOL_CAP_FORCE_READY,
		MPMatchCapabilityBit( MP_MATCH_CAP_PHASE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_TEAM_SELF,
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_TEAM_AND_QUEUE ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_TEAM_LOCK,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_LOCK ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_ROSTER_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_QUEUE,
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_TEAM_AND_QUEUE ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_TIMEOUT_TEAM,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_TIMEOUT ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_PAUSE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_PAUSE_TECHNICAL,
		MPMatchCapabilityBit( MP_MATCH_CAP_PAUSE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_RESUME,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_TIMEOUT ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_PAUSE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_REFEREE_SESSION,
		MPMatchCapabilityBit( MP_MATCH_CAP_AUTH_CREDENTIALS ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_RULES_STAGE,
		MPMatchCapabilityBit( MP_MATCH_CAP_RULES_BOUNDARY ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_RULES_COMMIT,
		MPMatchCapabilityBit( MP_MATCH_CAP_RULES_BOUNDARY ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_PROPOSAL_CREATE,
		MPMatchCapabilityBit( MP_MATCH_CAP_ELIGIBLE_PROPOSAL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_PROPOSAL_CAST,
		MPMatchCapabilityBit( MP_MATCH_CAP_ELIGIBLE_PROPOSAL ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_PROPOSAL_CANCEL,
		MPMatchCapabilityBit( MP_MATCH_CAP_ELIGIBLE_PROPOSAL ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_PROPOSAL_MODERATION ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_ROSTER_SELF,
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_TEAM_AND_QUEUE ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_ROSTER_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_ROLE_ASSIGN,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_ROSTER_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE,
		MPMatchCapabilityBit( MP_MATCH_CAP_SERVER_POLICY ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_VETO_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_VETO_SELECT,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_VETO_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_FORFEIT,
		MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ) |
		MPMatchCapabilityBit( MP_MATCH_CAP_PHASE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_ABORT,
		MPMatchCapabilityBit( MP_MATCH_CAP_PHASE_CONTROL ), true, true },
	{ MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN, 0, true, false },
	{ MP_MATCH_PROTOCOL_CAP_ROSTER_LEAVE_SELF,
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE ), false, false },
	{ MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE,
		MPMatchCapabilityBit( MP_MATCH_CAP_PROPOSAL_MODERATION ), true, true }
};

static_assert( MP_OPERATION_ARRAY_COUNT( capabilityPolicies ) == 25,
	"Every stable protocol capability requires an explicit session policy" );

struct mpOperationResolvedPrincipal {
	mpParticipantId actor;
	const mpMatchParticipantState *actorState;
	mpParticipantId target;
	const mpMatchParticipantState *targetState;
	int targetSide;
	mpMatchCapabilityMask_t actorCapabilities;

	void Clear( void ) {
		actor = mpParticipantId::Invalid();
		actorState = NULL;
		target = mpParticipantId::Invalid();
		targetState = NULL;
		targetSide = MP_MATCH_SIDE_NONE;
		actorCapabilities = 0;
	}
};

static bool HasCapability( mpMatchCapabilityMask_t mask, mpMatchCapability_t capability ) {
	return ( mask & MPMatchCapabilityBit( capability ) ) != 0;
}

static const mpMatchOperationArgument_t *FindArgument(
		const mpMatchOperationRequest_t &request, unsigned char fieldId ) {
	for ( int index = 0; index < request.argumentCount; ++index ) {
		if ( request.arguments[ index ].fieldId == fieldId ) {
			return &request.arguments[ index ];
		}
	}
	return NULL;
}

static bool IsPlayableSide( int side ) {
	return side == 0 || side == 1;
}

static mpProposalScope_t ProposalScopeForSide( int side ) {
	if ( side == 0 ) {
		return MP_PROPOSAL_SCOPE_TEAM_A;
	}
	if ( side == 1 ) {
		return MP_PROPOSAL_SCOPE_TEAM_B;
	}
	return MP_PROPOSAL_SCOPE_GLOBAL;
}

static const mpProposalRecord_t *FindProposalById( const mpProposalService &proposals,
		mpProposalId_t proposalId, mpProposalScope_t &scope ) {
	static const mpProposalScope_t scopes[] = {
		MP_PROPOSAL_SCOPE_GLOBAL,
		MP_PROPOSAL_SCOPE_TEAM_A,
		MP_PROPOSAL_SCOPE_TEAM_B
	};
	for ( int index = 0; index < MP_OPERATION_ARRAY_COUNT( scopes ); ++index ) {
		const mpProposalRecord_t *record = proposals.GetProposal( scopes[ index ] );
		if ( record != NULL && record->IsOccupied() && record->proposalId == proposalId ) {
			scope = scopes[ index ];
			return record;
		}
	}
	return NULL;
}

static bool MapBallot( unsigned short protocolBallot, mpProposalBallot_t &ballot ) {
	switch ( protocolBallot ) {
		case MP_MATCH_BALLOT_YES:
			ballot = MP_PROPOSAL_BALLOT_YES;
			return true;
		case MP_MATCH_BALLOT_NO:
			ballot = MP_PROPOSAL_BALLOT_NO;
			return true;
		case MP_MATCH_BALLOT_ABSTAIN:
			ballot = MP_PROPOSAL_BALLOT_ABSTAIN;
			return true;
		default:
			return false;
	}
}

static bool MapVetoAction( unsigned short protocolAction, mpSeriesVetoAction_t &action ) {
	switch ( protocolAction ) {
		case MP_MATCH_VETO_BAN:
			action = MP_SERIES_VETO_BAN;
			return true;
		case MP_MATCH_VETO_PICK:
			action = MP_SERIES_VETO_PICK;
			return true;
		case MP_MATCH_VETO_DECIDER:
			action = MP_SERIES_VETO_DECIDER;
			return true;
		case MP_MATCH_VETO_SIDE:
			action = MP_SERIES_VETO_SIDE;
			return true;
		default:
			return false;
	}
}

static bool MapStartingGameSide( unsigned short protocolSide, int &gameSide ) {
	switch ( protocolSide ) {
		case MP_MATCH_STARTING_SIDE_MARINE:
			gameSide = 0;
			return true;
		case MP_MATCH_STARTING_SIDE_STROGG:
			gameSide = 1;
			return true;
		default:
			gameSide = MP_SERIES_SIDE_NONE;
			return false;
	}
}

static void CopyMapToken( char destination[ MP_SERIES_MAP_TOKEN_BYTES ], const char *source ) {
	destination[ 0 ] = '\0';
	if ( source == NULL ) {
		return;
	}
	int index = 0;
	while ( index + 1 < MP_SERIES_MAP_TOKEN_BYTES && source[ index ] != '\0' ) {
		destination[ index ] = source[ index ];
		++index;
	}
	destination[ index ] = '\0';
}

static void RefreshRevisions( mpOperationExecutionResult_t &result,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	result.resultingSessionRevision = session.GetSessionRevision();
	result.resultingRulesRevision = rules.Committed().Revision();
	result.resultingRulesDigest = rules.Committed().Digest();
	result.resultingProposalRevision = proposals.GetRevision();
	result.resultingSeriesRevision = series.GetRevision();
}

static mpOperationExecutionResult_t BaseResult( const mpMatchSession &session,
		const mpCompetitiveRules &rules, const mpProposalService &proposals,
		const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result;
	result.Clear();
	RefreshRevisions( result, session, rules, proposals, series );
	return result;
}

static mpOperationExecutionResult_t Reject( mpOperationReason_t reason,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.outcome = MP_OPERATION_REJECTED;
	result.reason = reason;
	return result;
}

static mpOperationExecutionResult_t NeedsAdapter(
		mpOperationContinuationKind_t continuationKind,
		mpMatchOperationOpcode_t opcode,
		const mpOperationResolvedPrincipal &principal,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.outcome = MP_OPERATION_NEEDS_ADAPTER;
	result.reason = MP_OPERATION_REASON_NONE;
	result.continuation.kind = continuationKind;
	result.continuation.opcode = opcode;
	result.continuation.actor = principal.actor;
	result.continuation.participant = principal.target;
	result.continuation.side = principal.targetSide;
	return result;
}

static mpOperationExecutionResult_t FromSessionMutation(
		const mpMatchMutationResult &mutation,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.sessionReason = mutation.reason;
	if ( mutation.WasRejected() ) {
		result.outcome = MP_OPERATION_REJECTED;
		result.reason = MP_OPERATION_REASON_CORE_REJECTED;
	} else if ( mutation.WasApplied() ) {
		result.outcome = MP_OPERATION_APPLIED;
	} else {
		result.outcome = MP_OPERATION_NO_CHANGE;
	}
	return result;
}

static mpOperationExecutionResult_t FromProposalMutation(
		const mpProposalMutationResult_t &mutation,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.proposalReason = mutation.reason;
	if ( mutation.WasRejected() ) {
		result.outcome = MP_OPERATION_REJECTED;
		result.reason = MP_OPERATION_REASON_CORE_REJECTED;
	} else if ( mutation.WasApplied() ) {
		result.outcome = MP_OPERATION_APPLIED;
	} else {
		result.outcome = MP_OPERATION_NO_CHANGE;
	}
	return result;
}

static mpOperationExecutionResult_t FromSeriesMutation(
		const mpSeriesMutationResult &mutation,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.seriesReason = mutation.reason;
	if ( mutation.WasRejected() ) {
		result.outcome = MP_OPERATION_REJECTED;
		result.reason = MP_OPERATION_REASON_CORE_REJECTED;
	} else if ( mutation.WasApplied() ) {
		result.outcome = MP_OPERATION_APPLIED;
	} else {
		result.outcome = MP_OPERATION_NO_CHANGE;
	}
	return result;
}

static mpOperationExecutionResult_t RejectSeriesInput( mpSeriesReason_t reason,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_CORE_REJECTED,
		session, rules, proposals, series );
	result.seriesReason = reason;
	return result;
}

static bool RulesStateMatches( const mpOperationAdapterContext_t &context,
		const mpCompetitiveRules &rules ) {
	if ( rules.Committed().Revision() != context.expectedRulesRevision ||
			rules.Committed().Digest() != context.expectedRulesDigest ||
			rules.HasStagedSnapshot() != context.expectedStagedRules ) {
		return false;
	}
	if ( !context.expectedStagedRules ) {
		return context.expectedStagedRulesDigest == 0;
	}
	const mpMatchRulesSnapshot *staged = rules.StagedSnapshot();
	return staged != NULL && staged->Digest() == context.expectedStagedRulesDigest;
}

static bool SeriesOwnsCommittedRules( const mpCompetitionSeries &series ) {
	const mpSeriesState_t state = series.GetState();
	return state != MP_SERIES_DISABLED && state != MP_SERIES_COMPLETE &&
		state != MP_SERIES_CANCELLED;
}

static bool SetDraftField( mpMatchRulesDraft &draft,
		const mpRuleFieldDescriptor_t &field,
		const mpMatchOperationValue_t &value,
		mpRuleValidationFailure_t &failure ) {
	switch ( field.type ) {
		case MP_RULE_TYPE_BOOL:
			if ( value.type == MP_MATCH_VALUE_BOOL ) {
				return draft.SetBool( field.id, value.unsignedValue != 0, failure );
			}
			break;

		case MP_RULE_TYPE_INTEGER:
			if ( value.type == MP_MATCH_VALUE_INT32 ) {
				return draft.SetInteger( field.id, value.signedValue, failure );
			}
			if ( value.type == MP_MATCH_VALUE_UINT32 &&
					value.unsignedValue <= MP_OPERATION_INT_MAX ) {
				return draft.SetInteger( field.id, static_cast<int>( value.unsignedValue ), failure );
			}
			break;

		case MP_RULE_TYPE_ENUM:
			if ( value.type == MP_MATCH_VALUE_ENUM ) {
				return draft.SetEnum( field.id, value.enumValue, failure );
			}
			break;

		default:
			break;
	}

	// Printable scalar strings are accepted only by the strict bounded rule
	// parser.  They are never treated as executable console text.
	if ( value.type == MP_MATCH_VALUE_STRING ) {
		return draft.SetParsedValue( field.id, value.stringValue, failure );
	}
	return false;
}

static mpOperationExecutionResult_t FromRuleCommit(
		const mpRuleCommitResult_t &commit,
		const mpMatchSession &session, const mpCompetitiveRules &rules,
		const mpProposalService &proposals, const mpCompetitionSeries &series ) {
	mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
	result.ruleFailure = commit.failure;
	switch ( commit.disposition ) {
		case MP_RULE_COMMIT_UNCHANGED:
			result.outcome = MP_OPERATION_NO_CHANGE;
			break;
		case MP_RULE_COMMIT_APPLIED:
		case MP_RULE_COMMIT_STAGED:
			result.outcome = MP_OPERATION_APPLIED;
			break;
		default:
			result.outcome = MP_OPERATION_REJECTED;
			result.reason = MP_OPERATION_REASON_CORE_REJECTED;
			break;
	}
	return result;
}

static bool IsRulesOpcode( mpMatchOperationOpcode_t opcode ) {
	return opcode == MP_MATCH_OP_RULES_SELECT_PROFILE ||
		opcode == MP_MATCH_OP_RULES_STAGE_FIELD ||
		opcode == MP_MATCH_OP_RULES_COMMIT ||
		opcode == MP_MATCH_OP_RULES_DISCARD;
}

static bool IsProposalMutationOpcode( mpMatchOperationOpcode_t opcode ) {
	return opcode == MP_MATCH_OP_PROPOSAL_CREATE ||
		opcode == MP_MATCH_OP_PROPOSAL_CAST ||
		opcode == MP_MATCH_OP_PROPOSAL_CANCEL;
}

static bool IsSeriesOpcode( mpMatchOperationOpcode_t opcode ) {
	return opcode == MP_MATCH_OP_SERIES_STAGE_PROFILE ||
		opcode == MP_MATCH_OP_SERIES_START ||
		opcode == MP_MATCH_OP_SERIES_CANCEL ||
		opcode == MP_MATCH_OP_SERIES_ADVANCE ||
		opcode == MP_MATCH_OP_VETO_SELECT ||
		opcode == MP_MATCH_OP_SERIES_CONTESTANT_BIND;
}

static bool IsOwnSideOrAuthority( int requestedSide,
		const mpOperationResolvedPrincipal &principal,
		const mpOperationAdapterContext_t &context,
		mpMatchCapability_t authorityCapability ) {
	if ( principal.actorState != NULL && principal.actorState->side == requestedSide ) {
		return true;
	}
	if ( context.actorCompetitionContestant &&
		context.actorCompetitionSide == requestedSide ) {
		return true;
	}
	if ( context.localOperator || context.preauthenticatedRefereeGrant ) {
		return true;
	}
	return HasCapability( principal.actorCapabilities, authorityCapability );
}

static bool IsAuthorized( const mpMatchOperationRequest_t &request,
		const mpMatchOperationDescriptor_t &descriptor,
		const mpOperationResolvedPrincipal &principal,
		const mpOperationAdapterContext_t &context,
		bool passedProposalAuthority ) {
	if ( request.opcode == MP_MATCH_OP_REF_AUTHENTICATE ) {
		// This authorizes only entry into the credential adapter.  It grants no
		// referee capability and performs no credential comparison here.
		return principal.actorState != NULL && principal.actorState->human;
	}

	const mpOperationCapabilityPolicy_t *policy =
		MPOperationCapabilityPolicy( descriptor.requiredCapability );
	if ( policy == NULL ) {
		return false;
	}

	if ( passedProposalAuthority ) {
		return ( descriptor.flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) != 0 &&
			HasCapability( principal.actorCapabilities, MP_MATCH_CAP_ELIGIBLE_PROPOSAL );
	}
	if ( context.actorCompetitionContestant &&
		( request.opcode == MP_MATCH_OP_VETO_SELECT ||
			request.opcode == MP_MATCH_OP_FORFEIT ) ) {
		return true;
	}
	// Duel contestants own their connection-bound competition-side budget;
	// they do not acquire a captain role or authority over gameplay teams.
	if ( context.ruleGameType == GAME_DUEL && context.actorCompetitionContestant &&
		IsPlayableSide( context.actorCompetitionSide ) &&
		principal.actorState != NULL && principal.actorState->active &&
		( request.opcode == MP_MATCH_OP_TIMEOUT_REQUEST ||
			request.opcode == MP_MATCH_OP_RESUME_REQUEST ) ) {
		return true;
	}

	if ( ( principal.actorCapabilities & policy->anySessionCapability ) != 0 ) {
		return true;
	}
	if ( context.localOperator && policy->localOperatorAllowed ) {
		return true;
	}
	if ( context.preauthenticatedRefereeGrant && policy->refereeGrantAllowed ) {
		return true;
	}
	return false;
}

} // namespace

/*
================
mpOperationAdapterContinuation_t::Clear
================
*/
void mpOperationAdapterContinuation_t::Clear( void ) {
	kind = MP_OPERATION_CONTINUATION_NONE;
	opcode = MP_MATCH_OP_INVALID;
	actor = mpParticipantId::Invalid();
	participant = mpParticipantId::Invalid();
	replacementParticipant = mpParticipantId::Invalid();
	side = MP_MATCH_SIDE_NONE;
	rosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
	proposalScope = MP_PROPOSAL_SCOPE_GLOBAL;
	proposalId = 0;
	mapToken[ 0 ] = '\0';
	sensitiveValueRemainsInRequest = false;
}

mpOperationAdapterContext_s::mpOperationAdapterContext_s( void ) {
	trustedTransportSlot = -1;
	localOperator = false;
	preauthenticatedRefereeGrant = false;
	actorCompetitionContestant = false;
	actorCompetitionSide = MP_MATCH_SIDE_NONE;
	actorSeriesSide = MP_MATCH_SIDE_NONE;
	sessionOperational = false;
	countdownPrerequisitesSatisfied = false;
	cooldownPolicyAccepted = false;
	engineTime = mpMatchEngineTime();
	ruleGameType = -1;
	ruleBoundary = MP_RULES_OPEN_FOR_COMMIT;
	expectedRulesRevision = 0;
	expectedRulesDigest = 0;
	expectedStagedRules = false;
	expectedStagedRulesDigest = 0;
	expectedProposalRevision = 0;
	expectedSeriesRevision = 0;
	seriesReviewRecovered = false;
}

void mpOperationExecutionResult_t::Clear( void ) {
	outcome = MP_OPERATION_REJECTED;
	reason = MP_OPERATION_REASON_NONE;
	protocolReason = MP_MATCH_PROTOCOL_REASON_NONE;
	sessionReason = MP_MATCH_REASON_NONE;
	ruleFailure.Clear();
	proposalReason = MP_PROPOSAL_REASON_NONE;
	seriesReason = MP_SERIES_REASON_NONE;
	resultingSessionRevision = 0;
	resultingRulesRevision = 0;
	resultingRulesDigest = 0;
	resultingProposalRevision = 0;
	resultingSeriesRevision = 0;
	continuation.Clear();
}

bool mpOperationExecutionResult_t::Succeeded( void ) const {
	return outcome == MP_OPERATION_NO_CHANGE || outcome == MP_OPERATION_APPLIED;
}

const mpOperationCapabilityPolicy_t *MPOperationCapabilityPolicy(
		mpMatchProtocolCapabilityMask_t protocolCapability ) {
	for ( int index = 0; index < MP_OPERATION_ARRAY_COUNT( capabilityPolicies ); ++index ) {
		if ( capabilityPolicies[ index ].protocolCapability == protocolCapability ) {
			return &capabilityPolicies[ index ];
		}
	}
	return NULL;
}

bool MPOperationMapProtocolRosterRole( unsigned short protocolRole,
		mpMatchRosterRole_t &rosterRole, mpMatchRoleMask_t &principalRoles ) {
	switch ( protocolRole ) {
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER:
			rosterRole = MP_MATCH_ROSTER_PLAYER;
			break;
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN:
			rosterRole = MP_MATCH_ROSTER_CAPTAIN;
			break;
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH:
			rosterRole = MP_MATCH_ROSTER_COACH;
			break;
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE:
			rosterRole = MP_MATCH_ROSTER_SUBSTITUTE;
			break;
		default:
			rosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
			principalRoles = 0;
			return false;
	}
	principalRoles = MPMatchPrincipalRolesForRosterRole( rosterRole );
	return true;
}

bool MPOperationBroadcasterTargetIsValid( const mpMatchSession &session,
		mpParticipantId participant ) {
	const mpMatchParticipantState *state = session.FindParticipant( participant );
	if ( state == NULL || !state->connected || !state->human || state->active ||
		state->side != MP_MATCH_SIDE_NONE || session.FindRosterSeat( participant ) >= 0 ) {
		return false;
	}
	const mpMatchRoleMask_t playerRole = MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
	const mpMatchRoleMask_t broadcasterRole =
		MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER );
	return state->roles == playerRole || state->roles == broadcasterRole;
}

bool MPOperationMapProtocolTeam( mpMatchTeam_t protocolTeam, int &side ) {
	switch ( protocolTeam ) {
		case MP_MATCH_TEAM_MARINE:
			side = 0;
			return true;
		case MP_MATCH_TEAM_STROGG:
			side = 1;
			return true;
		case MP_MATCH_TEAM_SPECTATOR:
			side = MP_MATCH_SIDE_NONE;
			return true;
		case MP_MATCH_TEAM_NONE:
		default:
			side = MP_MATCH_SIDE_NONE;
			return false;
	}
}

bool MPOperationMapProtocolCompetitionSide( unsigned short protocolSide,
		int &competitionSide ) {
	switch ( protocolSide ) {
		case MP_MATCH_COMPETITION_SIDE_A:
			competitionSide = 0;
			return true;
		case MP_MATCH_COMPETITION_SIDE_B:
			competitionSide = 1;
			return true;
		default:
			competitionSide = MP_SERIES_SIDE_NONE;
			return false;
	}
}

mpOperationExecutionResult_t mpMatchOperationExecutor::Execute(
		const mpMatchOperationRequest_t &request,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series ) const {
	return ExecuteInternal( request, context, session, rules, proposals, series, false );
}

mpOperationExecutionResult_t mpMatchOperationExecutor::ExecuteInternal(
		const mpMatchOperationRequest_t &request,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series,
		bool passedProposalAuthority ) const {
	mpMatchProtocolError_t protocolError;
	protocolError.Clear();
	if ( !MPMatchProtocolValidateRequest( request, &protocolError ) ) {
		mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_PROTOCOL,
			session, rules, proposals, series );
		result.protocolReason = protocolError.reason;
		return result;
	}

	const mpMatchOperationDescriptor_t *descriptor = MPMatchOperationDescriptor( request.opcode );
	if ( descriptor == NULL ) {
		mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_PROTOCOL,
			session, rules, proposals, series );
		result.protocolReason = MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE;
		return result;
	}
	if ( !context.sessionOperational ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	if ( request.sessionId != session.GetSessionId() ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	const bool trustedLocalOperator = context.localOperator &&
		context.trustedTransportSlot == -1;
	if ( trustedLocalOperator ) {
		// A dedicated console has no transport participant.  Keep this seam
		// deliberately narrow: it changes principal resolution only, while schema,
		// phase, capability, revision and operation semantics remain shared.
		if ( request.actorSlot != 0 || request.actorBindingGeneration != 1 ||
			( request.opcode != MP_MATCH_OP_FORCE_READY &&
				request.opcode != MP_MATCH_OP_BROADCASTER_SET &&
				request.opcode != MP_MATCH_OP_SERIES_CONTESTANT_BIND &&
				request.opcode != MP_MATCH_OP_PARTICIPANT_REMOVE ) ) {
			return Reject( MP_OPERATION_REASON_TRANSPORT_MISMATCH,
				session, rules, proposals, series );
		}
	} else if ( context.trustedTransportSlot < 0 ||
			context.trustedTransportSlot >= MP_MATCH_MAX_CONNECTION_SLOTS ||
			request.actorSlot != context.trustedTransportSlot ) {
		return Reject( MP_OPERATION_REASON_TRANSPORT_MISMATCH,
			session, rules, proposals, series );
	}

	mpOperationResolvedPrincipal principal;
	principal.Clear();
	if ( !trustedLocalOperator ) {
		if ( !session.ResolveSlotBinding( context.trustedTransportSlot,
				request.actorBindingGeneration, principal.actor ) ) {
			return Reject( MP_OPERATION_REASON_BINDING_STALE,
				session, rules, proposals, series );
		}
		principal.actorState = session.FindParticipant( principal.actor );
		if ( principal.actorState == NULL || !principal.actorState->connected ||
				!principal.actorState->human ||
				principal.actor.SessionPart() != session.GetSessionId() ) {
			return Reject( MP_OPERATION_REASON_PARTICIPANT_UNKNOWN,
				session, rules, proposals, series );
		}
		principal.actorCapabilities = session.GetParticipantCapabilities(
			principal.actor );
	}

	if ( request.hasParticipantTarget ) {
		if ( !session.ResolveParticipantSequence( request.participantTarget, principal.target ) ) {
			return Reject( MP_OPERATION_REASON_TARGET_UNKNOWN,
				session, rules, proposals, series );
		}
		principal.targetState = session.FindParticipant( principal.target );
		if ( principal.targetState == NULL || !principal.targetState->connected ||
				principal.target.SessionPart() != session.GetSessionId() ) {
			return Reject( MP_OPERATION_REASON_TARGET_UNKNOWN,
				session, rules, proposals, series );
		}
	}
	if ( request.hasTeamTarget && !MPOperationMapProtocolTeam( request.teamTarget,
			principal.targetSide ) ) {
		return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
			session, rules, proposals, series );
	}

	if ( request.expectedSessionRevision != session.GetSessionRevision() ) {
		return Reject( MP_OPERATION_REASON_STALE_SESSION_REVISION,
			session, rules, proposals, series );
	}
	if ( session.GetPhase() < INACTIVE || session.GetPhase() >= STATE_COUNT ||
			( descriptor->legalPhaseMask &
				( static_cast<mpMatchPhaseMask_t>( 1u ) << session.GetPhase() ) ) == 0 ) {
		return Reject( MP_OPERATION_REASON_WRONG_PHASE,
			session, rules, proposals, series );
	}
	if ( !IsAuthorized( request, *descriptor, principal, context,
			passedProposalAuthority ) ) {
		return Reject( MP_OPERATION_REASON_NOT_AUTHORIZED,
			session, rules, proposals, series );
	}

	if ( IsRulesOpcode( request.opcode ) ) {
		if ( !RulesStateMatches( context, rules ) ) {
			return Reject( MP_OPERATION_REASON_STALE_RULES_REVISION,
				session, rules, proposals, series );
		}
		const mpRuleCommitBoundary_t requiredBoundary = session.HasFrozenRules() ?
			MP_RULES_FROZEN_FOR_MAP : MP_RULES_OPEN_FOR_COMMIT;
		if ( context.ruleBoundary != requiredBoundary ) {
			return Reject( MP_OPERATION_REASON_RULE_STATE,
				session, rules, proposals, series );
		}
	}
	if ( IsProposalMutationOpcode( request.opcode ) ) {
		if ( proposals.GetSessionId() != session.GetSessionId() ) {
			return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
				session, rules, proposals, series );
		}
		if ( proposals.GetRevision() != context.expectedProposalRevision ) {
			return Reject( MP_OPERATION_REASON_STALE_PROPOSAL_REVISION,
				session, rules, proposals, series );
		}
	}
	if ( IsSeriesOpcode( request.opcode ) &&
			series.GetRevision() != context.expectedSeriesRevision ) {
		return Reject( MP_OPERATION_REASON_STALE_SERIES_REVISION,
			session, rules, proposals, series );
	}

	if ( descriptor->cooldownClass != MP_MATCH_COOLDOWN_NONE &&
			!context.cooldownPolicyAccepted ) {
		return NeedsAdapter( MP_OPERATION_CONTINUATION_POLICY_RATE_LIMIT,
			request.opcode, principal, session, rules, proposals, series );
	}

	switch ( request.opcode ) {
		case MP_MATCH_OP_READY_SET: {
			const mpMatchOperationArgument_t *enabled = FindArgument( request, MP_MATCH_ARG_ENABLED );
			return FromSessionMutation( session.SetParticipantReady( principal.actor,
				enabled->value.unsignedValue != 0, request.expectedSessionRevision ),
				session, rules, proposals, series );
		}

		case MP_MATCH_OP_TEAM_READY_SET: {
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_PHASE_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			const mpMatchOperationArgument_t *enabled = FindArgument( request, MP_MATCH_ARG_ENABLED );
			return FromSessionMutation( session.SetTeamReady( principal.targetSide,
				enabled->value.unsignedValue != 0, request.expectedSessionRevision ),
				session, rules, proposals, series );
		}

		case MP_MATCH_OP_FORCE_READY: {
			if ( request.hasParticipantTarget && request.hasTeamTarget ) {
				return Reject( MP_OPERATION_REASON_UNREPRESENTABLE,
					session, rules, proposals, series );
			}
			const bool enabled = FindArgument( request, MP_MATCH_ARG_ENABLED )->value.unsignedValue != 0;
			if ( request.hasParticipantTarget ) {
				return FromSessionMutation( session.SetParticipantReady( principal.target,
					enabled, request.expectedSessionRevision ), session, rules, proposals, series );
			}
			if ( request.hasTeamTarget ) {
				if ( !IsPlayableSide( principal.targetSide ) ) {
					return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
						session, rules, proposals, series );
				}
				return FromSessionMutation( session.SetTeamReady( principal.targetSide,
					enabled, request.expectedSessionRevision ), session, rules, proposals, series );
			}
			if ( enabled ) {
				if ( !context.countdownPrerequisitesSatisfied ) {
					return Reject( MP_OPERATION_REASON_SERIES_STATE,
						session, rules, proposals, series );
				}
				if ( session.GetPhase() == COUNTDOWN ) {
					mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
					result.outcome = MP_OPERATION_NO_CHANGE;
					return result;
				}
				const mpMatchMutationResult transition = session.BeginCountdown(
					rules.Committed().Revision(), rules.Committed().Digest(),
					MP_MATCH_TRANSITION_REFEREE_FORCE_READY, principal.actor,
					request.expectedSessionRevision );
				return FromSessionMutation( transition, session, rules,
					proposals, series );
			}
			if ( session.GetPhase() == WARMUP ) {
				mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
				result.outcome = MP_OPERATION_NO_CHANGE;
				return result;
			}
			return FromSessionMutation( session.TransitionPhase( WARMUP,
				MP_MATCH_TRANSITION_COUNTDOWN_ABORTED, principal.actor,
				request.expectedSessionRevision ), session, rules, proposals, series );
		}

		case MP_MATCH_OP_TEAM_JOIN: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_TEAM_CHANGE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_ROSTER_LEAVE: {
			// Targetless and actor-bound by construction.  The explicit roster
			// role/activity precondition prevents self-withdrawal from becoming a
			// second generic spectator/team-change operation.  Recheck it here even
			// though capabilities derive from the same predicate: authorization and
			// mutation semantics must both fail closed if they ever drift.
			if ( !session.CanSelfLeaveRoster( principal.actor ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_ROSTER_LEAVE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_TEAM_LOCK_SET:
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_ROSTER_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			return NeedsAdapter( MP_OPERATION_CONTINUATION_TEAM_LOCK,
				request.opcode, principal, session, rules, proposals, series );

		case MP_MATCH_OP_QUEUE_JOIN: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_QUEUE_JOIN, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_QUEUE_DEFER: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_QUEUE_DEFER, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_QUEUE_LEAVE: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_QUEUE_LEAVE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_TIMEOUT_REQUEST:
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_PAUSE_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			return FromSessionMutation( session.RequestTeamTimeout( principal.targetSide,
				MP_MATCH_PAUSE_REASON_TACTICAL, request.expectedSessionRevision ),
				session, rules, proposals, series );

		case MP_MATCH_OP_TECH_PAUSE_REQUEST: {
			if ( request.hasTeamTarget && ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_PAUSE_CONTROL ) ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			mpMatchPauseReason_t pauseReason = MP_MATCH_PAUSE_REASON_TECHNICAL_FAULT;
			if ( context.localOperator ) {
				pauseReason = MP_MATCH_PAUSE_REASON_SERVER_FAULT;
			} else if ( context.preauthenticatedRefereeGrant ||
					HasCapability( principal.actorCapabilities, MP_MATCH_CAP_PAUSE_CONTROL ) ) {
				pauseReason = MP_MATCH_PAUSE_REASON_REFEREE;
			}
			return FromSessionMutation( session.RequestTechnicalPause( pauseReason,
				request.expectedSessionRevision ), session, rules, proposals, series );
		}

		case MP_MATCH_OP_RESUME_REQUEST: {
			if ( context.localOperator || context.preauthenticatedRefereeGrant ||
					HasCapability( principal.actorCapabilities, MP_MATCH_CAP_PAUSE_CONTROL ) ) {
				return FromSessionMutation( session.RequestResumeByAuthority(
					request.expectedSessionRevision ), session, rules, proposals, series );
			}
			const int resumeSide = context.ruleGameType == GAME_DUEL &&
				context.actorCompetitionContestant ? context.actorCompetitionSide :
				principal.actorState->side;
			if ( !IsPlayableSide( resumeSide ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			return FromSessionMutation( session.RequestResumeByTeam(
				resumeSide, request.expectedSessionRevision ),
				session, rules, proposals, series );
		}

		case MP_MATCH_OP_REF_AUTHENTICATE: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_REFEREE_AUTHENTICATE, request.opcode,
				principal, session, rules, proposals, series );
			result.continuation.sensitiveValueRemainsInRequest = true;
			return result;
		}

		case MP_MATCH_OP_REF_LOGOUT:
			return NeedsAdapter( MP_OPERATION_CONTINUATION_REFEREE_LOGOUT,
				request.opcode, principal, session, rules, proposals, series );

		case MP_MATCH_OP_RULES_SELECT_PROFILE: {
			if ( SeriesOwnsCommittedRules( series ) ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			const mpMatchOperationArgument_t *profileArg = FindArgument( request, MP_MATCH_ARG_PROFILE );
			const mpMatchProfileDescriptor_t *profile = MPMatchProfileByKey(
				profileArg->value.stringValue );
			if ( profile == NULL ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			mpCompetitiveRules candidate = rules;
			mpMatchRulesDraft draft;
			mpRuleValidationFailure_t failure;
			failure.Clear();
			if ( !candidate.BeginDraftFromProfile( profile->id, context.ruleGameType,
					draft, failure ) ) {
				mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_CORE_REJECTED,
					session, rules, proposals, series );
				result.ruleFailure = failure;
				return result;
			}
			const mpRuleCommitResult_t commit = candidate.Commit( draft,
				context.validatedRuleContext, context.ruleBoundary );
			if ( commit.disposition == MP_RULE_COMMIT_REJECTED ) {
				mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_CORE_REJECTED,
					session, rules, proposals, series );
				result.ruleFailure = commit.failure;
				return result;
			}
			if ( commit.disposition == MP_RULE_COMMIT_UNCHANGED ) {
				mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
				result.outcome = MP_OPERATION_NO_CHANGE;
				return result;
			}
			if ( commit.disposition == MP_RULE_COMMIT_APPLIED ) {
				return NeedsAdapter( MP_OPERATION_CONTINUATION_RULES_APPLY_PROFILE,
					request.opcode, principal, session, rules, proposals, series );
			}
			rules = candidate;
			return FromRuleCommit( commit, session, rules, proposals, series );
		}

		case MP_MATCH_OP_RULES_STAGE_FIELD: {
			if ( SeriesOwnsCommittedRules( series ) ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			const mpMatchOperationArgument_t *fieldArg = FindArgument( request, MP_MATCH_ARG_SETTING_ID );
			const mpMatchOperationArgument_t *valueArg = FindArgument( request, MP_MATCH_ARG_SETTING_VALUE );
			const mpRuleFieldDescriptor_t *field = MPMatchRuleFieldByKey(
				fieldArg->value.stringValue );
			if ( field == NULL ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			mpCompetitiveRules candidate = rules;
			mpMatchRulesDraft draft = candidate.HasStagedSnapshot() ?
				candidate.BeginDraftForNextWarmup() : candidate.BeginDraftFromCommitted();
			mpRuleValidationFailure_t failure;
			failure.Clear();
			if ( !SetDraftField( draft, *field, valueArg->value, failure ) ) {
				mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_RULE_VALUE_TYPE,
					session, rules, proposals, series );
				result.ruleFailure = failure;
				return result;
			}
			const mpRuleCommitResult_t commit = candidate.Commit( draft,
				context.validatedRuleContext, context.ruleBoundary );
			if ( commit.disposition == MP_RULE_COMMIT_REJECTED ) {
				mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_CORE_REJECTED,
					session, rules, proposals, series );
				result.ruleFailure = commit.failure;
				return result;
			}
			if ( commit.disposition == MP_RULE_COMMIT_UNCHANGED ) {
				mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
				result.outcome = MP_OPERATION_NO_CHANGE;
				return result;
			}
			if ( commit.disposition == MP_RULE_COMMIT_APPLIED ) {
				return NeedsAdapter( MP_OPERATION_CONTINUATION_RULES_APPLY_FIELD,
					request.opcode, principal, session, rules, proposals, series );
			}
			rules = candidate;
			return FromRuleCommit( commit, session, rules, proposals, series );
		}

		case MP_MATCH_OP_RULES_COMMIT: {
			if ( SeriesOwnsCommittedRules( series ) ||
					context.ruleBoundary != MP_RULES_OPEN_FOR_COMMIT ||
					!rules.HasStagedSnapshot() ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			mpCompetitiveRules candidate = rules;
			const mpRuleCommitResult_t commit = candidate.ApplyStagedAtWarmup(
				context.validatedRuleContext );
			if ( commit.disposition == MP_RULE_COMMIT_REJECTED ) {
				mpOperationExecutionResult_t result = Reject( MP_OPERATION_REASON_CORE_REJECTED,
					session, rules, proposals, series );
				result.ruleFailure = commit.failure;
				return result;
			}
			return NeedsAdapter( MP_OPERATION_CONTINUATION_RULES_COMMIT,
				request.opcode, principal, session, rules, proposals, series );
		}

		case MP_MATCH_OP_RULES_DISCARD: {
			mpCompetitiveRules candidate = rules;
			if ( !candidate.DiscardStagedSnapshot() ) {
				mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
				result.outcome = MP_OPERATION_NO_CHANGE;
				return result;
			}
			rules = candidate;
			mpOperationExecutionResult_t result = BaseResult( session, rules, proposals, series );
			result.outcome = MP_OPERATION_APPLIED;
			return result;
		}

		case MP_MATCH_OP_PROPOSAL_CREATE: {
			if ( request.hasTeamTarget && !IsPlayableSide( principal.targetSide ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			if ( request.hasTeamTarget ) {
				// A side ballot is decided by that side's own active players, so
				// only one of them may open it.  Referees, operators and the
				// opposing side keep their direct operations instead; authority
				// over a side never lets anyone call that side's vote.
				if ( principal.actorState == NULL || !principal.actorState->active ||
						principal.actorState->side != principal.targetSide ) {
					return Reject( MP_OPERATION_REASON_NOT_AUTHORIZED,
						session, rules, proposals, series );
				}
				// The vote must be able to execute now.  A side ballot that could
				// only fail at execution would cost the side its scope for the
				// whole window.
				const mpMatchOperationArgument_t *proposedOpcode = FindArgument( request,
					MP_MATCH_ARG_PROPOSED_OPCODE );
				const mpMatchOperationDescriptor_t *proposedDescriptor =
					proposedOpcode != NULL ? MPMatchOperationDescriptor(
						static_cast<mpMatchOperationOpcode_t>(
							proposedOpcode->value.enumValue ) ) : NULL;
				if ( proposedDescriptor == NULL ||
						( proposedDescriptor->legalPhaseMask &
							( static_cast<mpMatchPhaseMask_t>( 1u ) <<
								session.GetPhase() ) ) == 0 ) {
					return Reject( MP_OPERATION_REASON_WRONG_PHASE,
						session, rules, proposals, series );
				}
			}
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_PROPOSAL_CREATE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.proposalScope = request.hasTeamTarget ?
				ProposalScopeForSide( principal.targetSide ) : MP_PROPOSAL_SCOPE_GLOBAL;
			return result;
		}

		case MP_MATCH_OP_PROPOSAL_CAST: {
			const mpProposalId_t proposalId = FindArgument( request,
				MP_MATCH_ARG_PROPOSAL_ID )->value.unsignedValue;
			mpProposalScope_t scope = MP_PROPOSAL_SCOPE_GLOBAL;
			const mpProposalRecord_t *record = FindProposalById( proposals, proposalId, scope );
			if ( record == NULL ) {
				return Reject( MP_OPERATION_REASON_PROPOSAL_UNKNOWN,
					session, rules, proposals, series );
			}
			mpProposalBallot_t ballot = MP_PROPOSAL_BALLOT_NONE;
			if ( !MapBallot( FindArgument( request,
					MP_MATCH_ARG_BALLOT_CHOICE )->value.enumValue, ballot ) ) {
				return Reject( MP_OPERATION_REASON_PROTOCOL,
					session, rules, proposals, series );
			}
			const mpProposalMutationResult_t mutation = proposals.CastBallot(
				session.GetSessionId(), scope, proposalId, principal.actor.SequencePart(),
				ballot, mpProposalEngineTime::FromMilliseconds( context.engineTime.Milliseconds() ),
				context.expectedProposalRevision );
			return FromProposalMutation( mutation, session, rules, proposals, series );
		}

		case MP_MATCH_OP_PROPOSAL_CANCEL: {
			const mpProposalId_t proposalId = FindArgument( request,
				MP_MATCH_ARG_PROPOSAL_ID )->value.unsignedValue;
			mpProposalScope_t scope = MP_PROPOSAL_SCOPE_GLOBAL;
			const mpProposalRecord_t *record = FindProposalById( proposals, proposalId, scope );
			if ( record == NULL ) {
				return Reject( MP_OPERATION_REASON_PROPOSAL_UNKNOWN,
					session, rules, proposals, series );
			}
			const bool proposer = record->caller == principal.actor.SequencePart();
			const bool moderator = context.localOperator ||
				context.preauthenticatedRefereeGrant ||
				HasCapability( principal.actorCapabilities, MP_MATCH_CAP_PROPOSAL_MODERATION );
			if ( !proposer && !moderator ) {
				return Reject( MP_OPERATION_REASON_NOT_AUTHORIZED,
					session, rules, proposals, series );
			}
			const mpProposalCancellationReason_t cancelReason = proposer ?
				MP_PROPOSAL_CANCEL_PROPOSER : ( context.localOperator ?
					MP_PROPOSAL_CANCEL_SERVER_OPERATOR : MP_PROPOSAL_CANCEL_REFEREE );
			const mpProposalMutationResult_t mutation = proposals.Cancel(
				session.GetSessionId(), scope, proposalId, cancelReason,
				mpProposalEngineTime::FromMilliseconds( context.engineTime.Milliseconds() ),
				context.expectedProposalRevision );
			return FromProposalMutation( mutation, session, rules, proposals, series );
		}

		case MP_MATCH_OP_ROSTER_INVITE: {
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_ROSTER_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			unsigned short protocolRole = MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER;
			const mpMatchOperationArgument_t *roleArg = FindArgument( request, MP_MATCH_ARG_ROLE );
			if ( roleArg != NULL ) {
				protocolRole = roleArg->value.enumValue;
			}
			mpMatchRosterRole_t rosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
			mpMatchRoleMask_t ignoredPrincipalRoles = 0;
			if ( !MPOperationMapProtocolRosterRole( protocolRole, rosterRole,
					ignoredPrincipalRoles ) ) {
				return Reject( MP_OPERATION_REASON_PROTOCOL,
					session, rules, proposals, series );
			}
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_ROSTER_INVITE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.rosterRole = rosterRole;
			return result;
		}

		case MP_MATCH_OP_ROSTER_ACCEPT: {
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_ROSTER_ACCEPT, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.participant = principal.actor;
			return result;
		}

		case MP_MATCH_OP_ROSTER_REMOVE: {
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_ROSTER_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			const int seat = session.FindRosterSeat( principal.target );
			const mpMatchRosterSeat *rosterSeat = seat >= 0 ? session.GetRosterSeat( seat ) : NULL;
			if ( rosterSeat == NULL || rosterSeat->side != principal.targetSide ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			return NeedsAdapter( MP_OPERATION_CONTINUATION_ROSTER_REMOVE,
				request.opcode, principal, session, rules, proposals, series );
		}

		case MP_MATCH_OP_PARTICIPANT_REMOVE:
			// ParticipantId is resolved above in the current session.  Never copy
			// targetState->slot into the continuation: a slot can be reused between
			// proposal creation, passage and adapter commit.
			if ( principal.target == principal.actor ||
					principal.targetState == NULL || !principal.targetState->human ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			if ( ( principal.targetState->roles &
					MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) != 0 ) {
				return Reject( MP_OPERATION_REASON_INVARIANT,
					session, rules, proposals, series );
			}
			return NeedsAdapter( MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE,
				request.opcode, principal, session, rules, proposals, series );

		case MP_MATCH_OP_ROSTER_SUBSTITUTE: {
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_ROSTER_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			const int seat = session.FindRosterSeat( principal.target );
			const mpMatchRosterSeat *rosterSeat = seat >= 0 ? session.GetRosterSeat( seat ) : NULL;
			if ( rosterSeat == NULL || rosterSeat->side != principal.targetSide ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			mpParticipantId replacement;
			const unsigned int replacementSequence = FindArgument( request,
				MP_MATCH_ARG_REPLACEMENT_PARTICIPANT )->value.unsignedValue;
			if ( !session.ResolveParticipantSequence( replacementSequence, replacement ) ||
					replacement == principal.target ) {
				return Reject( MP_OPERATION_REASON_TARGET_UNKNOWN,
					session, rules, proposals, series );
			}
			const mpMatchParticipantState *replacementState = session.FindParticipant( replacement );
			if ( replacementState == NULL || !replacementState->connected ||
					!replacementState->human ) {
				return Reject( MP_OPERATION_REASON_TARGET_UNKNOWN,
					session, rules, proposals, series );
			}
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_ROSTER_SUBSTITUTE, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.replacementParticipant = replacement;
			result.continuation.rosterRole = rosterSeat->role;
			return result;
		}

		case MP_MATCH_OP_ROLE_ASSIGN: {
			if ( !IsPlayableSide( principal.targetSide ) ||
					principal.targetState->side != principal.targetSide ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_ROSTER_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			mpMatchRosterRole_t rosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
			mpMatchRoleMask_t assignedRoles = 0;
			if ( !MPOperationMapProtocolRosterRole( FindArgument( request,
					MP_MATCH_ARG_ROLE )->value.enumValue, rosterRole, assignedRoles ) ||
				assignedRoles != MPMatchPrincipalRolesForRosterRole( rosterRole ) ) {
				return Reject( MP_OPERATION_REASON_PROTOCOL,
					session, rules, proposals, series );
			}
			if ( principal.targetState->roles & MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) {
				return Reject( MP_OPERATION_REASON_INVARIANT,
					session, rules, proposals, series );
			}
			if ( ( principal.targetState->roles &
				( MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ) |
					MPMatchRoleBit( MP_MATCH_ROLE_REFEREE ) ) ) != 0 ) {
				return Reject( MP_OPERATION_REASON_INVARIANT,
					session, rules, proposals, series );
			}
			// Every roster-role change also changes seat, activity and disclosure
			// identity.  The adapter commits those facts together on a candidate
			// session instead of publishing a transient label-only mutation.
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_ROLE_ASSIGN, request.opcode, principal,
				session, rules, proposals, series );
			result.continuation.rosterRole = rosterRole;
			return result;
		}

		case MP_MATCH_OP_SERIES_STAGE_PROFILE: {
			if ( rules.HasStagedSnapshot() ) {
				return Reject( MP_OPERATION_REASON_RULE_STATE,
					session, rules, proposals, series );
			}
			const mpMatchOperationArgument_t *profileArgument = FindArgument( request,
				MP_MATCH_ARG_SERIES_PROFILE );
			const mpSeriesProfileDescriptor *profile = profileArgument == NULL ? NULL :
				MPSeriesProfileByKey( profileArgument->value.stringValue );
			if ( profile == NULL ) {
				return RejectSeriesInput( MP_SERIES_REASON_UNKNOWN_PROFILE,
					session, rules, proposals, series );
			}
			const mpMatchOperationArgument_t *bestOfArgument = FindArgument( request,
				MP_MATCH_ARG_BEST_OF );
			if ( bestOfArgument != NULL &&
				static_cast<int>( bestOfArgument->value.unsignedValue ) != profile->bestOf ) {
				return RejectSeriesInput( MP_SERIES_REASON_PROFILE_BEST_OF_MISMATCH,
					session, rules, proposals, series );
			}
			return NeedsAdapter( MP_OPERATION_CONTINUATION_SERIES_CONFIGURE_PROFILE,
				request.opcode, principal, session, rules, proposals, series );
		}

		case MP_MATCH_OP_SERIES_START: {
			const mpSeriesMutationResult mutation = series.Start( context.expectedSeriesRevision );
			mpOperationExecutionResult_t result = FromSeriesMutation( mutation,
				session, rules, proposals, series );
			if ( result.outcome == MP_OPERATION_APPLIED ) {
				result.continuation.kind = MP_OPERATION_CONTINUATION_SERIES_PERSIST;
				result.continuation.opcode = request.opcode;
				result.continuation.actor = principal.actor;
			}
			return result;
		}

		case MP_MATCH_OP_SERIES_CANCEL: {
			if ( series.GetState() == MP_SERIES_MAP_ACTIVE ) {
				return Reject( MP_OPERATION_REASON_SERIES_STATE,
					session, rules, proposals, series );
			}
			const mpSeriesMutationResult mutation = series.Cancel( context.expectedSeriesRevision );
			mpOperationExecutionResult_t result = FromSeriesMutation( mutation,
				session, rules, proposals, series );
			if ( result.outcome == MP_OPERATION_APPLIED ) {
				result.continuation.kind = MP_OPERATION_CONTINUATION_SERIES_PERSIST;
				result.continuation.opcode = request.opcode;
				result.continuation.actor = principal.actor;
			}
			return result;
		}

		case MP_MATCH_OP_SERIES_ADVANCE: {
			const mpGameState_t phase = session.GetPhase();
			const bool recoveredReview = phase == WARMUP && context.seriesReviewRecovered;
			if ( ( phase == WARMUP || phase == NEXTGAME ) && !recoveredReview ) {
				if ( series.GetState() != MP_SERIES_READY ) {
					return Reject( MP_OPERATION_REASON_SERIES_STATE,
						session, rules, proposals, series );
				}
				mpOperationExecutionResult_t result = NeedsAdapter(
					MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP,
					request.opcode, principal, session, rules, proposals, series );
				CopyMapToken( result.continuation.mapToken,
					series.GetNextMapToken() );
				return result;
			}
			// A completed-map checkpoint can resume in a new warmup after a
			// server restart. Only the validated adapter recovery context may
			// stand in for review; ordinary early/stale warmup controls still fail.
			if ( ( phase != GAMEREVIEW && !recoveredReview ) ||
				series.GetState() != MP_SERIES_MAP_COMPLETE ) {
				return Reject( MP_OPERATION_REASON_SERIES_STATE,
					session, rules, proposals, series );
			}
			mpCompetitionSeries candidate = series;
			const mpSeriesMutationResult mutation = candidate.AdvanceAfterMap(
				context.expectedSeriesRevision );
			if ( mutation.WasRejected() ) {
				return FromSeriesMutation( mutation, session, rules, proposals, series );
			}
			if ( !mutation.WasApplied() ) {
				return FromSeriesMutation( mutation, session, rules, proposals, series );
			}
			if ( candidate.GetState() == MP_SERIES_READY ) {
				mpOperationExecutionResult_t result = NeedsAdapter(
					MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP,
					request.opcode, principal, session, rules, proposals, series );
				CopyMapToken( result.continuation.mapToken, candidate.GetNextMapToken() );
				return result;
			}
			if ( candidate.GetState() != MP_SERIES_COMPLETE ) {
				return Reject( MP_OPERATION_REASON_INVARIANT,
					session, rules, proposals, series );
			}
			series = candidate;
			mpOperationExecutionResult_t result = FromSeriesMutation( mutation,
				session, rules, proposals, series );
			if ( result.outcome == MP_OPERATION_APPLIED ) {
				result.continuation.kind = MP_OPERATION_CONTINUATION_SERIES_PERSIST;
				result.continuation.opcode = request.opcode;
				result.continuation.actor = principal.actor;
			}
			return result;
		}

		case MP_MATCH_OP_SERIES_CONTESTANT_BIND: {
			int competitionSide = MP_SERIES_SIDE_NONE;
			const mpMatchOperationArgument_t *sideArgument = FindArgument( request,
				MP_MATCH_ARG_COMPETITION_SIDE );
			if ( sideArgument == NULL || !MPOperationMapProtocolCompetitionSide(
					sideArgument->value.enumValue, competitionSide ) ) {
				return Reject( MP_OPERATION_REASON_UNREPRESENTABLE,
					session, rules, proposals, series );
			}
			const mpSeriesState_t state = series.GetState();
			if ( context.ruleGameType != GAME_DUEL ||
					series.GetConfiguration().gameType != GAME_DUEL ||
					state == MP_SERIES_DISABLED || state == MP_SERIES_COMPLETE ||
					state == MP_SERIES_CANCELLED ) {
				return Reject( MP_OPERATION_REASON_SERIES_STATE,
					session, rules, proposals, series );
			}
			if ( principal.targetState == NULL || !principal.targetState->human ||
					!principal.targetState->active ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			mpOperationExecutionResult_t result = NeedsAdapter(
				MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND,
				request.opcode, principal, session, rules, proposals, series );
			result.continuation.side = competitionSide;
			return result;
		}

		case MP_MATCH_OP_VETO_SELECT: {
			mpSeriesVetoAction_t action = MP_SERIES_VETO_ACTION_COUNT;
			if ( !MapVetoAction( FindArgument( request,
					MP_MATCH_ARG_VETO_ACTION )->value.enumValue, action ) ) {
				return Reject( MP_OPERATION_REASON_UNREPRESENTABLE,
					session, rules, proposals, series );
			}
			// The turn is checked on the same series side the view showed the actor,
			// never on the gameplay side it happens to hold.
			int actingSide = context.actorSeriesSide;
			const bool controlsVeto = context.localOperator ||
				context.preauthenticatedRefereeGrant ||
				HasCapability( principal.actorCapabilities, MP_MATCH_CAP_VETO_CONTROL );
			if ( controlsVeto ) {
				const int step = series.GetCurrentVetoStep();
				if ( step >= 0 && step < series.GetConfiguration().vetoStepCount ) {
					actingSide = series.GetConfiguration().vetoSteps[ step ].expectedSide;
				}
			}
			if ( !IsPlayableSide( actingSide ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			const char *mapToken = FindArgument( request, MP_MATCH_ARG_MAP_TOKEN )->value.stringValue;
			int startingGameSide = MP_SERIES_SIDE_NONE;
			if ( action == MP_SERIES_VETO_SIDE ) {
				const mpMatchOperationArgument_t *startingSide = FindArgument( request,
					MP_MATCH_ARG_STARTING_SIDE );
				if ( startingSide == NULL || !MapStartingGameSide(
						startingSide->value.enumValue, startingGameSide ) ) {
					return Reject( MP_OPERATION_REASON_UNREPRESENTABLE,
						session, rules, proposals, series );
				}
			}
			const mpSeriesMutationResult mutation = series.ApplyVeto( actingSide,
				action, mapToken, startingGameSide, context.expectedSeriesRevision );
			mpOperationExecutionResult_t result = FromSeriesMutation( mutation,
				session, rules, proposals, series );
			if ( result.outcome == MP_OPERATION_APPLIED ) {
				result.continuation.kind = MP_OPERATION_CONTINUATION_SERIES_PERSIST;
				result.continuation.opcode = request.opcode;
				result.continuation.actor = principal.actor;
			}
			return result;
		}

		case MP_MATCH_OP_FORFEIT:
			if ( !IsPlayableSide( principal.targetSide ) ||
					!IsOwnSideOrAuthority( principal.targetSide, principal, context,
						MP_MATCH_CAP_PHASE_CONTROL ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			if ( series.GetState() != MP_SERIES_DISABLED &&
					series.GetState() != MP_SERIES_COMPLETE &&
					series.GetState() != MP_SERIES_CANCELLED ) {
				return NeedsAdapter( MP_OPERATION_CONTINUATION_SERIES_MATCH_RESULT,
					request.opcode, principal, session, rules, proposals, series );
			}
			return FromSessionMutation( session.TransitionPhase( GAMEREVIEW,
				MP_MATCH_TRANSITION_FORFEIT, principal.actor,
				request.expectedSessionRevision ), session, rules, proposals, series );

		case MP_MATCH_OP_ABORT:
			if ( series.GetState() != MP_SERIES_DISABLED &&
					series.GetState() != MP_SERIES_COMPLETE &&
					series.GetState() != MP_SERIES_CANCELLED ) {
				return NeedsAdapter( MP_OPERATION_CONTINUATION_SERIES_MATCH_RESULT,
					request.opcode, principal, session, rules, proposals, series );
			}
			if ( session.GetPhase() == COUNTDOWN ) {
				return FromSessionMutation( session.TransitionPhase( WARMUP,
					MP_MATCH_TRANSITION_COUNTDOWN_ABORTED, principal.actor,
					request.expectedSessionRevision ), session, rules, proposals, series );
			}
			return FromSessionMutation( session.TransitionPhase( GAMEREVIEW,
				MP_MATCH_TRANSITION_MATCH_ABORTED, principal.actor,
				request.expectedSessionRevision ), session, rules, proposals, series );

		case MP_MATCH_OP_BROADCASTER_SET: {
			const bool enabled = FindArgument( request,
				MP_MATCH_ARG_ENABLED )->value.unsignedValue != 0;
			if ( !MPOperationBroadcasterTargetIsValid( session,
				principal.target ) ) {
				return Reject( MP_OPERATION_REASON_TARGET_ALIGNMENT,
					session, rules, proposals, series );
			}
			const mpMatchRoleMask_t playerRole =
				MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
			const mpMatchRoleMask_t broadcasterRole =
				MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER );
			return FromSessionMutation( session.SetParticipantRoles( principal.target,
				enabled ? broadcasterRole : playerRole,
				request.expectedSessionRevision ), session, rules, proposals, series );
		}

		default:
			return Reject( MP_OPERATION_REASON_UNREPRESENTABLE,
				session, rules, proposals, series );
	}
}

mpOperationExecutionResult_t mpMatchOperationExecutor::ExecutePassedProposal(
		mpProposalScope_t scope,
		mpProposalId_t proposalId,
		const mpOperationAdapterContext_t &context,
		mpMatchSession &session,
		mpCompetitiveRules &rules,
		mpProposalService &proposals,
		mpCompetitionSeries &series ) const {
	if ( !context.sessionOperational ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	if ( proposals.GetSessionId() != session.GetSessionId() ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	if ( proposals.GetRevision() != context.expectedProposalRevision ) {
		return Reject( MP_OPERATION_REASON_STALE_PROPOSAL_REVISION,
			session, rules, proposals, series );
	}
	const mpProposalRecord_t *record = proposals.GetProposal( scope );
	if ( record == NULL || !record->IsOccupied() || record->proposalId != proposalId ) {
		return Reject( MP_OPERATION_REASON_PROPOSAL_UNKNOWN,
			session, rules, proposals, series );
	}
	if ( record->status != MP_PROPOSAL_STATUS_PASSED ) {
		return Reject( MP_OPERATION_REASON_PROPOSAL_NOT_PASSED,
			session, rules, proposals, series );
	}

	// Copy before execution because a successful target may mutate a core.  The
	// proposal record remains retained until the explicit acknowledgement call.
	mpMatchOperationRequest_t operation = record->operation;
	// A ballot's authority is its vote, and a vote takes up to its whole window.
	// Ready changes, connections and every other session mutation in that window
	// advance the session revision, so the revision the proposer saw would make
	// almost every passed ballot stale.  Revalidate against the session as it is
	// now instead: phase, the proposer's binding and eligibility, the target's
	// identity and side and the opcode's own semantic checks all run again below.
	// Once-only execution is the adapter's acknowledgement, not this revision.
	operation.expectedSessionRevision = session.GetSessionRevision();
	mpOperationExecutionResult_t result = ExecuteInternal( operation, context,
		session, rules, proposals, series, true );
	result.continuation.proposalScope = scope;
	result.continuation.proposalId = proposalId;
	if ( ( result.outcome == MP_OPERATION_APPLIED ||
			result.outcome == MP_OPERATION_NO_CHANGE ) &&
			result.continuation.kind == MP_OPERATION_CONTINUATION_NONE ) {
		result.continuation.kind = MP_OPERATION_CONTINUATION_PROPOSAL_ACKNOWLEDGE;
		result.continuation.opcode = operation.opcode;
		result.continuation.proposalScope = scope;
		result.continuation.proposalId = proposalId;
	}
	return result;
}

mpOperationExecutionResult_t mpMatchOperationExecutor::AcknowledgePassedProposal(
		mpProposalScope_t scope,
		mpProposalId_t proposalId,
		const mpOperationAdapterContext_t &context,
		const mpMatchSession &session,
		const mpCompetitiveRules &rules,
		mpProposalService &proposals,
		const mpCompetitionSeries &series ) const {
	if ( !context.sessionOperational ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	if ( proposals.GetSessionId() != session.GetSessionId() ) {
		return Reject( MP_OPERATION_REASON_SESSION_MISMATCH,
			session, rules, proposals, series );
	}
	if ( proposals.GetRevision() != context.expectedProposalRevision ) {
		return Reject( MP_OPERATION_REASON_STALE_PROPOSAL_REVISION,
			session, rules, proposals, series );
	}
	const mpProposalRecord_t *record = proposals.GetProposal( scope );
	if ( record == NULL || !record->IsOccupied() || record->proposalId != proposalId ) {
		return Reject( MP_OPERATION_REASON_PROPOSAL_UNKNOWN,
			session, rules, proposals, series );
	}
	if ( record->status != MP_PROPOSAL_STATUS_PASSED ) {
		return Reject( MP_OPERATION_REASON_PROPOSAL_NOT_PASSED,
			session, rules, proposals, series );
	}
	return FromProposalMutation( proposals.Acknowledge( session.GetSessionId(),
		scope, proposalId, context.expectedProposalRevision ),
		session, rules, proposals, series );
}
