//----------------------------------------------------------------
// MatchTeams.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_TEAMS_STANDALONE_TEST )
	#include "MatchTeams.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchTeams.h"
#endif

#include <limits.h>
#include <stddef.h>

namespace {

static bool IsTeamsSide( int side ) {
	return side >= 0 && side < MP_MATCH_SIDE_COUNT;
}

static bool IsTeamsRosterRole( mpMatchRosterRole_t role ) {
	return MPMatchRosterRoleIsValid( role );
}

static mpMatchRoleMask_t TeamsPrincipalRoleBits( void ) {
	return MPMatchRosterPrincipalRoleMask();
}

static mpMatchRoleMask_t TeamsRolesForRosterRole( mpMatchRosterRole_t role ) {
	return MPMatchPrincipalRolesForRosterRole( role );
}

static bool IsTeamsLobbyPhase( mpGameState_t phase ) {
	return phase == WARMUP || phase == GAMEREVIEW || phase == NEXTGAME;
}

static bool IsTeamsLivePhase( mpGameState_t phase ) {
	return phase == COUNTDOWN || phase == GAMEON || phase == SUDDENDEATH;
}

static bool TeamsIssuerIsCurrent( const mpMatchSession &session,
		mpParticipantId issuer, int side, bool operatorAuthorized ) {
	const mpMatchParticipantState *state = session.FindParticipant( issuer );
	if ( state == NULL || !state->connected || !state->human ) {
		return false;
	}
	// Invitations delegate current roster authority. Benching or demoting a
	// captain withdraws that delegation while the connection remains present.
	return operatorAuthorized ||
		( state->roles & MPMatchRoleBit( MP_MATCH_ROLE_REFEREE ) ) != 0 ||
		( state->active && state->side == side &&
			( state->roles & MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ) ) != 0 );
}

static bool AddTeamsDuration( mpMatchEngineTime base, int durationMsec,
		mpMatchEngineTime &out ) {
	if ( !base.IsValid() || durationMsec <= 0 ||
		base.Milliseconds() > INT64_MAX - durationMsec ) {
		return false;
	}
	out = mpMatchEngineTime::FromMilliseconds(
		base.Milliseconds() + static_cast<int64_t>( durationMsec ) );
	return true;
}

static mpMatchTeamsJoinDecision_t DeniedJoin( mpMatchTeamsReason_t reason ) {
	mpMatchTeamsJoinDecision_t result;
	result.Clear();
	result.disposition = MP_MATCH_TEAMS_JOIN_DENY;
	result.reason = reason;
	return result;
}

static mpMatchTeamsJoinDecision_t QueuedJoin( mpMatchTeamsReason_t reason ) {
	mpMatchTeamsJoinDecision_t result;
	result.Clear();
	result.disposition = MP_MATCH_TEAMS_JOIN_QUEUE;
	result.reason = reason;
	return result;
}

} // namespace

bool mpMatchTeamsMutationResult_t::WasApplied( void ) const {
	return code == MP_MATCH_TEAMS_MUTATION_APPLIED;
}

bool mpMatchTeamsMutationResult_t::WasRejected( void ) const {
	return code == MP_MATCH_TEAMS_MUTATION_REJECTED;
}

mpMatchRoleMask_t MPMatchTeamsPrincipalRoleMask( void ) {
	return TeamsPrincipalRoleBits();
}

bool MPMatchTeamsAssignRosterRole( mpMatchRoleMask_t existingRoles,
		mpMatchRosterRole_t rosterRole, mpMatchRoleMask_t &outRoles ) {
	outRoles = existingRoles;
	if ( !MPMatchRoleMaskIsValid( existingRoles, true ) ||
		!IsTeamsRosterRole( rosterRole ) ||
		( existingRoles & ( MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ) |
			MPMatchRoleBit( MP_MATCH_ROLE_REFEREE ) ) ) != 0 ) {
		return false;
	}
	const mpMatchRoleMask_t candidate =
		( existingRoles & ~TeamsPrincipalRoleBits() ) |
		TeamsRolesForRosterRole( rosterRole );
	if ( !MPMatchRoleMaskIsValid( candidate, true ) ) {
		return false;
	}
	outRoles = candidate;
	return true;
}

bool MPMatchTeamsClearRosterRole( mpMatchRoleMask_t existingRoles,
		mpMatchRoleMask_t &outRoles ) {
	outRoles = existingRoles;
	if ( !MPMatchRoleMaskIsValid( existingRoles, true ) ) {
		return false;
	}
	const mpMatchRoleMask_t candidate =
		existingRoles & ~TeamsPrincipalRoleBits();
	if ( !MPMatchRoleMaskIsValid( candidate, true ) ) {
		return false;
	}
	outRoles = candidate;
	return true;
}

void mpMatchTeamsPolicy_t::Clear( void ) {
	teamMode = false;
	queueEnabled = false;
	requireRosterMembership = false;
	invitationBypassesLock = true;
	requireInvitationForSubstitution = false;
	allowLiveJoin = false;
	allowLiveSubstitution = false;
	maximumActiveTotal = MP_MATCH_MAX_PARTICIPANTS;
	maximumActivePerSide = 0;
}

bool mpMatchTeamsPolicy_t::IsValid( void ) const {
	if ( maximumActiveTotal < 1 || maximumActiveTotal > MP_MATCH_MAX_PARTICIPANTS ) {
		return false;
	}
	if ( teamMode ) {
		return maximumActivePerSide >= 1 &&
			maximumActivePerSide <= maximumActiveTotal &&
			maximumActivePerSide <= MP_MATCH_MAX_PARTICIPANTS;
	}
	return maximumActivePerSide == 0;
}

void mpMatchQueueEntry_t::Clear( void ) {
	ticketId = 0;
	participant = mpParticipantId::Invalid();
	requestedSide = MP_MATCH_SIDE_NONE;
	enqueuedAt = mpMatchEngineTime::FromMilliseconds( 0 );
	positionedAt = mpMatchEngineTime::FromMilliseconds( 0 );
	deferralCount = 0;
}

bool mpMatchQueueEntry_t::IsOccupied( void ) const {
	return ticketId != 0;
}

void mpMatchRosterInvitation_t::Clear( void ) {
	invitationId = 0;
	sessionId = 0;
	target = mpParticipantId::Invalid();
	side = MP_MATCH_SIDE_NONE;
	role = MP_MATCH_ROSTER_PLAYER;
	issuer = mpParticipantId::Invalid();
	operatorAuthorized = false;
	rosterSeat = -1;
	issuedAt = mpMatchEngineTime::FromMilliseconds( 0 );
	expiresAt = mpMatchEngineTime::FromMilliseconds( 0 );
}

bool mpMatchRosterInvitation_t::IsOccupied( void ) const {
	return invitationId != 0;
}

bool mpMatchRosterInvitation_t::IsActiveAt( mpMatchEngineTime engineNow ) const {
	return IsOccupied() && engineNow.IsValid() && engineNow >= issuedAt &&
		engineNow < expiresAt;
}

void mpMatchTeamsTransactionPlan_t::Clear( void ) {
	kind = MP_MATCH_TEAMS_TRANSACTION_NONE;
	sessionId = 0;
	expectedTeamsRevision = 0;
	expectedSessionRevision = 0;
	incomingParticipant = mpParticipantId::Invalid();
	outgoingParticipant = mpParticipantId::Invalid();
	invitationIssuer = mpParticipantId::Invalid();
	side = MP_MATCH_SIDE_NONE;
	rosterRole = MP_MATCH_ROSTER_PLAYER;
	rosterSeat = -1;
	outgoingRosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
	outgoingRosterSeat = -1;
	queueTicketId = 0;
	invitationId = 0;
	consumeQueueTicket = false;
	consumeInvitation = false;
	setIncomingActive = false;
	incomingActive = false;
	setIncomingSide = false;
	incomingSide = MP_MATCH_SIDE_NONE;
	assignIncomingRosterRole = false;
	setOutgoingActive = false;
	outgoingActive = false;
	setOutgoingSide = false;
	outgoingSide = MP_MATCH_SIDE_NONE;
	clearOutgoingRosterRole = false;
	assignOutgoingRosterRole = false;
	vacateRosterSeat = false;
	vacateOutgoingRosterSeat = false;
	assignRosterSeat = false;
	assignOutgoingRosterSeat = false;
}

bool mpMatchTeamsTransactionPlan_t::IsValid( void ) const {
	if ( kind <= MP_MATCH_TEAMS_TRANSACTION_NONE ||
		kind >= MP_MATCH_TEAMS_TRANSACTION_KIND_COUNT || sessionId == 0 ||
		expectedTeamsRevision == 0 || expectedSessionRevision == 0 ||
		!incomingParticipant.IsValid() ||
		incomingParticipant.SessionPart() != sessionId ||
		( side != MP_MATCH_SIDE_NONE && !IsTeamsSide( side ) ) ||
		!IsTeamsRosterRole( rosterRole ) || incomingSide != side ||
		incomingActive != MPMatchRosterRoleIsActive( rosterRole ) ) {
		return false;
	}
	if ( consumeQueueTicket != ( queueTicketId != 0 ) ||
		consumeInvitation != ( invitationId != 0 ) ) {
		return false;
	}
	if ( consumeInvitation && ( !invitationIssuer.IsValid() ||
		invitationIssuer.SessionPart() != sessionId ) ) {
		return false;
	}
	if ( !consumeInvitation && invitationIssuer.IsValid() ) {
		return false;
	}
	if ( setIncomingSide && incomingSide != MP_MATCH_SIDE_NONE &&
		!IsTeamsSide( incomingSide ) ) {
		return false;
	}
	if ( assignIncomingRosterRole &&
		( rosterSeat < 0 || rosterSeat >= MP_MATCH_MAX_ROSTER_SEATS ) ) {
		return false;
	}
	if ( kind == MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION &&
		!consumeQueueTicket ) {
		return false;
	}
	if ( kind == MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE ) {
		if ( !consumeInvitation || !assignRosterSeat || vacateRosterSeat ||
			rosterSeat < 0 || rosterSeat >= MP_MATCH_MAX_ROSTER_SEATS ||
			!assignIncomingRosterRole || clearOutgoingRosterRole ||
			assignOutgoingRosterRole || vacateOutgoingRosterSeat ||
			assignOutgoingRosterSeat ||
			outgoingRosterRole != MP_MATCH_ROSTER_ROLE_COUNT ||
			outgoingRosterSeat != -1 ) {
			return false;
		}
	}
	if ( kind == MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION ) {
		if ( !outgoingParticipant.IsValid() ||
			outgoingParticipant.SessionPart() != sessionId ||
			outgoingParticipant == incomingParticipant || !vacateRosterSeat ||
			!assignRosterSeat || rosterSeat < 0 ||
			rosterSeat >= MP_MATCH_MAX_ROSTER_SEATS || !setOutgoingActive ||
			outgoingActive || !setIncomingActive || !incomingActive ||
			!assignIncomingRosterRole ||
			!MPMatchRosterRoleIsActive( rosterRole ) ) {
			return false;
		}
		const bool persistentBench = IsPersistentBenchSwap();
		if ( persistentBench ) {
			if ( outgoingRosterSeat >= MP_MATCH_MAX_ROSTER_SEATS ||
				outgoingRosterSeat == rosterSeat ||
				outgoingRosterRole != MP_MATCH_ROSTER_SUBSTITUTE ||
				!vacateOutgoingRosterSeat || !assignOutgoingRosterRole ||
				!assignOutgoingRosterSeat || clearOutgoingRosterRole ||
				setOutgoingSide ) {
				return false;
			}
		} else if ( outgoingRosterRole != MP_MATCH_ROSTER_ROLE_COUNT ||
			vacateOutgoingRosterSeat || assignOutgoingRosterRole ||
			assignOutgoingRosterSeat || !clearOutgoingRosterRole ||
			!setOutgoingSide || outgoingSide != MP_MATCH_SIDE_NONE ) {
			return false;
		}
	} else if ( outgoingParticipant.IsValid() || setOutgoingActive ||
		setOutgoingSide || clearOutgoingRosterRole || assignOutgoingRosterRole ||
		vacateRosterSeat || vacateOutgoingRosterSeat || assignOutgoingRosterSeat ||
		outgoingRosterRole != MP_MATCH_ROSTER_ROLE_COUNT ||
		outgoingRosterSeat != -1 ) {
		return false;
	}
	if ( kind != MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE &&
		kind != MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION && assignRosterSeat ) {
		return false;
	}
	return true;
}

bool mpMatchTeamsTransactionPlan_t::IsPersistentBenchSwap( void ) const {
	return kind == MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION &&
		outgoingRosterSeat >= 0;
}

void mpMatchTeamsJoinDecision_t::Clear( void ) {
	disposition = MP_MATCH_TEAMS_JOIN_DENY;
	reason = MP_MATCH_TEAMS_REASON_INVALID_ARGUMENT;
	plan.Clear();
}

bool mpMatchTeamsJoinDecision_t::IsAllowed( void ) const {
	return disposition == MP_MATCH_TEAMS_JOIN_ALLOW && plan.IsValid();
}

void mpMatchTeamsRecipientSnapshot_t::Clear( void ) {
	sessionId = 0;
	revision = 0;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		sideLocked[ side ] = false;
	}
	queueCount = 0;
	recipientQueued = false;
	recipientQueuePosition = -1;
	recipientQueueTicketId = 0;
	recipientRequestedSide = MP_MATCH_SIDE_NONE;
	invitationCount = 0;
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_INVITATIONS; ++i ) {
		invitations[ i ].Clear();
	}
}

mpMatchTeams::mpMatchTeams( void ) :
	sessionId( 0 ),
	revision( 0 ),
	lastEngineTime( mpMatchEngineTime::FromMilliseconds( 0 ) ),
	nextQueueTicketId( 1 ),
	queueCount( 0 ),
	nextInvitationId( 1 ),
	invitationCount( 0 ) {
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		sideLocked[ side ] = false;
	}
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES; ++i ) {
		queue[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_INVITATIONS; ++i ) {
		invitations[ i ].Clear();
	}
}

bool mpMatchTeams::Reset( uint64_t newSessionId,
		mpMatchEngineTime initialEngineTime ) {
	if ( newSessionId == 0 || !initialEngineTime.IsValid() ) {
		return false;
	}
	sessionId = newSessionId;
	revision = 1;
	lastEngineTime = initialEngineTime;
	nextQueueTicketId = 1;
	queueCount = 0;
	nextInvitationId = 1;
	invitationCount = 0;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		sideLocked[ side ] = false;
	}
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES; ++i ) {
		queue[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_INVITATIONS; ++i ) {
		invitations[ i ].Clear();
	}
	return ValidateInvariants();
}

uint64_t mpMatchTeams::GetSessionId( void ) const {
	return sessionId;
}

mpMatchTeamsRevision_t mpMatchTeams::GetRevision( void ) const {
	return revision;
}

mpMatchEngineTime mpMatchTeams::GetLastEngineTime( void ) const {
	return lastEngineTime;
}

bool mpMatchTeams::CanMutate( uint64_t requestedSessionId,
		mpMatchTeamsRevision_t expectedRevision,
		mpMatchTeamsReason_t &reason ) const {
	if ( requestedSessionId == 0 || requestedSessionId != sessionId ) {
		reason = MP_MATCH_TEAMS_REASON_SESSION_MISMATCH;
		return false;
	}
	if ( expectedRevision != revision ) {
		reason = MP_MATCH_TEAMS_REASON_STALE_REVISION;
		return false;
	}
	if ( revision == UINT64_MAX ) {
		reason = MP_MATCH_TEAMS_REASON_REVISION_EXHAUSTED;
		return false;
	}
	reason = MP_MATCH_TEAMS_REASON_NONE;
	return true;
}

bool mpMatchTeams::ValidateTime( mpMatchEngineTime engineNow,
		mpMatchTeamsReason_t &reason ) const {
	if ( !engineNow.IsValid() || engineNow < lastEngineTime ) {
		reason = MP_MATCH_TEAMS_REASON_CLOCK_REGRESSION;
		return false;
	}
	reason = MP_MATCH_TEAMS_REASON_NONE;
	return true;
}

bool mpMatchTeams::ValidateSession( const mpMatchSession &session,
		const mpMatchTeamsPolicy_t *policy,
		mpMatchTeamsReason_t &reason ) const {
	if ( sessionId == 0 || session.GetSessionId() != sessionId ) {
		reason = MP_MATCH_TEAMS_REASON_SESSION_MISMATCH;
		return false;
	}
	if ( !session.ValidateInvariants() ) {
		reason = MP_MATCH_TEAMS_REASON_INVARIANT;
		return false;
	}
	if ( policy != NULL && ( !policy->IsValid() ||
		policy->teamMode != session.GetReadinessPolicy().teamMode ) ) {
		reason = MP_MATCH_TEAMS_REASON_POLICY_MISMATCH;
		return false;
	}
	reason = MP_MATCH_TEAMS_REASON_NONE;
	return true;
}

bool mpMatchTeams::IsParticipantForSession( mpParticipantId participant ) const {
	return participant.IsValid() && participant.SessionPart() == sessionId;
}

mpMatchTeamsMutationResult_t mpMatchTeams::Applied(
		mpMatchQueueTicketId_t queueTicketId,
		mpMatchRosterInvitationId_t invitationId ) {
	mpMatchTeamsMutationResult_t result;
	result.code = MP_MATCH_TEAMS_MUTATION_APPLIED;
	result.reason = MP_MATCH_TEAMS_REASON_NONE;
	result.previousRevision = revision;
	++revision;
	result.currentRevision = revision;
	result.queueTicketId = queueTicketId;
	result.invitationId = invitationId;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::NoChange(
		mpMatchTeamsReason_t reason, mpMatchQueueTicketId_t queueTicketId,
		mpMatchRosterInvitationId_t invitationId ) const {
	mpMatchTeamsMutationResult_t result;
	result.code = MP_MATCH_TEAMS_MUTATION_NO_CHANGE;
	result.reason = reason;
	result.previousRevision = revision;
	result.currentRevision = revision;
	result.queueTicketId = queueTicketId;
	result.invitationId = invitationId;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::ObservedNoChange(
		mpMatchEngineTime engineNow, mpMatchTeamsReason_t reason,
		mpMatchQueueTicketId_t queueTicketId,
		mpMatchRosterInvitationId_t invitationId ) {
	lastEngineTime = engineNow;
	return NoChange( reason, queueTicketId, invitationId );
}

mpMatchTeamsMutationResult_t mpMatchTeams::Rejected(
		mpMatchTeamsReason_t reason ) const {
	mpMatchTeamsMutationResult_t result;
	result.code = MP_MATCH_TEAMS_MUTATION_REJECTED;
	result.reason = reason;
	result.previousRevision = revision;
	result.currentRevision = revision;
	result.queueTicketId = 0;
	result.invitationId = 0;
	return result;
}

bool mpMatchTeams::IsSideLocked( int side ) const {
	return IsTeamsSide( side ) && sideLocked[ side ];
}

mpMatchTeamsMutationResult_t mpMatchTeams::SetSideLocked(
		const mpMatchSession &session, int side, bool locked,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( session.GetSessionId(), expectedRevision, reason ) ||
		!ValidateSession( session, NULL, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsTeamsLobbyPhase( session.GetPhase() ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	if ( !session.GetReadinessPolicy().teamMode ) {
		return Rejected( MP_MATCH_TEAMS_REASON_POLICY_MISMATCH );
	}
	if ( !IsTeamsSide( side ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	}
	if ( sideLocked[ side ] == locked ) {
		return NoChange( MP_MATCH_TEAMS_REASON_NONE );
	}
	mpMatchTeams candidate = *this;
	candidate.sideLocked[ side ] = locked;
	mpMatchTeamsMutationResult_t result = candidate.Applied( 0, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

int mpMatchTeams::GetQueueCount( void ) const {
	return queueCount;
}

const mpMatchQueueEntry_t *mpMatchTeams::GetQueueEntry( int index ) const {
	return index >= 0 && index < queueCount ? &queue[ index ] : NULL;
}

int mpMatchTeams::FindQueuePosition( mpParticipantId participant ) const {
	if ( !IsParticipantForSession( participant ) ) {
		return -1;
	}
	for ( int i = 0; i < queueCount; ++i ) {
		if ( queue[ i ].participant == participant ) {
			return i;
		}
	}
	return -1;
}

void mpMatchTeams::RemoveQueueAt( int index ) {
	if ( index < 0 || index >= queueCount ) {
		return;
	}
	for ( int i = index; i + 1 < queueCount; ++i ) {
		queue[ i ] = queue[ i + 1 ];
	}
	--queueCount;
	queue[ queueCount ].Clear();
}

mpMatchTeamsMutationResult_t mpMatchTeams::JoinQueue(
		const mpMatchSession &session, mpParticipantId participant,
		int requestedSide, const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( session.GetSessionId(), expectedRevision, reason ) ||
		!ValidateSession( session, &policy, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsTeamsLobbyPhase( session.GetPhase() ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	if ( !policy.queueEnabled ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_DISABLED );
	}
	if ( ( policy.teamMode && !IsTeamsSide( requestedSide ) ) ||
		( !policy.teamMode && requestedSide != MP_MATCH_SIDE_NONE ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	}
	if ( !IsParticipantForSession( participant ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	const mpMatchParticipantState *state = session.FindParticipant( participant );
	if ( state == NULL || !state->connected ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !state->human ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN );
	}
	if ( state->active ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_ACTIVE );
	}
	const int rosterSeat = session.FindRosterSeat( participant );
	const mpMatchRosterSeat *seat = rosterSeat >= 0 ?
		session.GetRosterSeat( rosterSeat ) : NULL;
	if ( seat != NULL && !MPMatchRosterRoleIsActive( seat->role ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_ROLE );
	}
	if ( seat != NULL && seat->side != requestedSide ) {
		return Rejected( MP_MATCH_TEAMS_REASON_ROSTER_ALIGNMENT );
	}
	if ( policy.requireRosterMembership && seat == NULL ) {
		return Rejected( MP_MATCH_TEAMS_REASON_ROSTER_REQUIRED );
	}
	if ( policy.teamMode && sideLocked[ requestedSide ] && seat == NULL ) {
		return Rejected( MP_MATCH_TEAMS_REASON_TEAM_LOCKED );
	}
	const int existing = FindQueuePosition( participant );
	if ( existing >= 0 ) {
		if ( queue[ existing ].requestedSide == requestedSide ) {
			return ObservedNoChange( engineNow, MP_MATCH_TEAMS_REASON_NONE,
				queue[ existing ].ticketId, 0 );
		}
		return Rejected( MP_MATCH_TEAMS_REASON_ALREADY_QUEUED );
	}
	if ( queueCount >= MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_CAPACITY );
	}
	if ( nextQueueTicketId == 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_TICKET_EXHAUSTED );
	}

	mpMatchTeams candidate = *this;
	mpMatchQueueEntry_t &entry = candidate.queue[ candidate.queueCount++ ];
	entry.Clear();
	entry.ticketId = candidate.nextQueueTicketId;
	entry.participant = participant;
	entry.requestedSide = requestedSide;
	entry.enqueuedAt = engineNow;
	entry.positionedAt = engineNow;
	if ( candidate.nextQueueTicketId == UINT64_MAX ) {
		candidate.nextQueueTicketId = 0;
	} else {
		++candidate.nextQueueTicketId;
	}
	candidate.lastEngineTime = engineNow;
	const mpMatchQueueTicketId_t ticketId = entry.ticketId;
	mpMatchTeamsMutationResult_t result = candidate.Applied( ticketId, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::DeferQueue(
		const mpMatchSession &session, mpParticipantId participant,
		mpMatchQueueTicketId_t expectedTicketId, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( session.GetSessionId(), expectedRevision, reason ) ||
		!ValidateSession( session, NULL, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsTeamsLobbyPhase( session.GetPhase() ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	const int position = FindQueuePosition( participant );
	if ( position < 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_NOT_QUEUED );
	}
	const mpMatchParticipantState *state = session.FindParticipant( participant );
	if ( state == NULL || !state->connected ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !state->human ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN );
	}
	if ( expectedTicketId == 0 || queue[ position ].ticketId != expectedTicketId ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_TICKET_MISMATCH );
	}
	if ( position + 1 == queueCount ) {
		return ObservedNoChange( engineNow, MP_MATCH_TEAMS_REASON_NONE,
			expectedTicketId, 0 );
	}
	if ( nextQueueTicketId == 0 || queue[ position ].deferralCount == UINT32_MAX ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_TICKET_EXHAUSTED );
	}

	mpMatchTeams candidate = *this;
	mpMatchQueueEntry_t moved = candidate.queue[ position ];
	for ( int i = position; i + 1 < candidate.queueCount; ++i ) {
		candidate.queue[ i ] = candidate.queue[ i + 1 ];
	}
	moved.ticketId = candidate.nextQueueTicketId;
	moved.positionedAt = engineNow;
	++moved.deferralCount;
	candidate.queue[ candidate.queueCount - 1 ] = moved;
	if ( candidate.nextQueueTicketId == UINT64_MAX ) {
		candidate.nextQueueTicketId = 0;
	} else {
		++candidate.nextQueueTicketId;
	}
	candidate.lastEngineTime = engineNow;
	mpMatchTeamsMutationResult_t result = candidate.Applied( moved.ticketId, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::LeaveQueue(
		uint64_t requestedSessionId, mpParticipantId participant,
		mpMatchQueueTicketId_t expectedTicketId,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ) {
		return Rejected( reason );
	}
	const int position = FindQueuePosition( participant );
	if ( position < 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_NOT_QUEUED );
	}
	if ( expectedTicketId == 0 || queue[ position ].ticketId != expectedTicketId ) {
		return Rejected( MP_MATCH_TEAMS_REASON_QUEUE_TICKET_MISMATCH );
	}
	mpMatchTeams candidate = *this;
	candidate.RemoveQueueAt( position );
	mpMatchTeamsMutationResult_t result = candidate.Applied( expectedTicketId, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

int mpMatchTeams::GetInvitationCount( void ) const {
	return invitationCount;
}

const mpMatchRosterInvitation_t *mpMatchTeams::GetInvitationByIndex(
		int index ) const {
	return index >= 0 && index < invitationCount ? &invitations[ index ] : NULL;
}

int mpMatchTeams::FindInvitationIndex(
		mpMatchRosterInvitationId_t invitationId ) const {
	if ( invitationId == 0 ) {
		return -1;
	}
	for ( int i = 0; i < invitationCount; ++i ) {
		if ( invitations[ i ].invitationId == invitationId ) {
			return i;
		}
	}
	return -1;
}

const mpMatchRosterInvitation_t *mpMatchTeams::FindInvitation(
		mpMatchRosterInvitationId_t invitationId ) const {
	const int index = FindInvitationIndex( invitationId );
	return index >= 0 ? &invitations[ index ] : NULL;
}

void mpMatchTeams::RemoveInvitationAt( int index ) {
	if ( index < 0 || index >= invitationCount ) {
		return;
	}
	for ( int i = index; i + 1 < invitationCount; ++i ) {
		invitations[ i ] = invitations[ i + 1 ];
	}
	--invitationCount;
	invitations[ invitationCount ].Clear();
}

int mpMatchTeams::RemoveExpiredInvitations( const mpMatchSession &session,
		mpMatchEngineTime engineNow ) {
	int removed = 0;
	for ( int i = 0; i < invitationCount; ) {
		if ( engineNow >= invitations[ i ].expiresAt ||
			!TeamsIssuerIsCurrent( session, invitations[ i ].issuer,
				invitations[ i ].side, invitations[ i ].operatorAuthorized ) ) {
			RemoveInvitationAt( i );
			++removed;
		} else {
			++i;
		}
	}
	return removed;
}

int mpMatchTeams::FindReservableRosterSeat( const mpMatchSession &session,
		int side, mpMatchRosterRole_t role ) const {
	for ( int seatIndex = 0; seatIndex < MP_MATCH_MAX_ROSTER_SEATS; ++seatIndex ) {
		const mpMatchRosterSeat *seat = session.GetRosterSeat( seatIndex );
		if ( seat == NULL || seat->side != side || seat->role != role ||
			seat->occupant.IsValid() ) {
			continue;
		}
		bool reserved = false;
		for ( int invitationIndex = 0; invitationIndex < invitationCount;
				++invitationIndex ) {
			if ( invitations[ invitationIndex ].rosterSeat == seatIndex ) {
				reserved = true;
				break;
			}
		}
		if ( !reserved ) {
			return seatIndex;
		}
	}
	return -1;
}

mpMatchTeamsMutationResult_t mpMatchTeams::IssueRosterInvitation(
		const mpMatchSession &session, mpParticipantId target, int side,
		mpMatchRosterRole_t role, mpParticipantId issuer, int lifetimeMsec,
		mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision,
		mpMatchRosterInvitationId_t &outInvitationId,
		bool operatorAuthorized ) {
	outInvitationId = 0;
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( session.GetSessionId(), expectedRevision, reason ) ||
		!ValidateSession( session, NULL, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsTeamsLobbyPhase( session.GetPhase() ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	if ( !IsParticipantForSession( target ) || !IsParticipantForSession( issuer ) ||
		target == issuer ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	const mpMatchParticipantState *targetState = session.FindParticipant( target );
	const mpMatchParticipantState *issuerState = session.FindParticipant( issuer );
	if ( targetState == NULL || issuerState == NULL || !targetState->connected ||
		!issuerState->connected ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !targetState->human || !issuerState->human ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN );
	}
	if ( !IsTeamsRosterRole( role ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_ROLE );
	}
	const bool teamMode = session.GetReadinessPolicy().teamMode;
	if ( ( teamMode && !IsTeamsSide( side ) ) ||
		( !teamMode && side != MP_MATCH_SIDE_NONE ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	}
	if ( !TeamsIssuerIsCurrent( session, issuer, side, operatorAuthorized ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
	}
	if ( session.FindRosterSeat( target ) >= 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_ALREADY_ROSTERED );
	}
	if ( lifetimeMsec <= 0 || lifetimeMsec > MP_MATCH_TEAMS_MAX_INVITATION_MSEC ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVALID_ARGUMENT );
	}
	mpMatchEngineTime expiresAt;
	if ( !AddTeamsDuration( engineNow, lifetimeMsec, expiresAt ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_CLOCK_OVERFLOW );
	}
	for ( int i = 0; i < invitationCount; ++i ) {
		const mpMatchRosterInvitation_t &existing = invitations[ i ];
		if ( existing.IsActiveAt( engineNow ) && existing.target == target &&
			existing.side == side && existing.role == role &&
			existing.issuer == issuer ) {
			outInvitationId = existing.invitationId;
			return ObservedNoChange( engineNow,
				MP_MATCH_TEAMS_REASON_INVITATION_DUPLICATE, 0,
				existing.invitationId );
		}
	}
	if ( nextInvitationId == 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_ID_EXHAUSTED );
	}

	mpMatchTeams candidate = *this;
	candidate.RemoveExpiredInvitations( session, engineNow );
	if ( candidate.invitationCount >= MP_MATCH_TEAMS_MAX_INVITATIONS ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_CAPACITY );
	}
	const int rosterSeat = candidate.FindReservableRosterSeat( session, side, role );
	if ( rosterSeat < 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_ROSTER_SEAT_UNAVAILABLE );
	}
	mpMatchRosterInvitation_t &invitation =
		candidate.invitations[ candidate.invitationCount++ ];
	invitation.Clear();
	invitation.invitationId = candidate.nextInvitationId;
	invitation.sessionId = candidate.sessionId;
	invitation.target = target;
	invitation.side = side;
	invitation.role = role;
	invitation.issuer = issuer;
	invitation.operatorAuthorized = operatorAuthorized;
	invitation.rosterSeat = rosterSeat;
	invitation.issuedAt = engineNow;
	invitation.expiresAt = expiresAt;
	if ( candidate.nextInvitationId == MP_MATCH_TEAMS_MAX_INVITATION_ID ) {
		candidate.nextInvitationId = 0;
	} else {
		++candidate.nextInvitationId;
	}
	candidate.lastEngineTime = engineNow;
	outInvitationId = invitation.invitationId;
	mpMatchTeamsMutationResult_t result = candidate.Applied( 0,
		invitation.invitationId );
	if ( !candidate.ValidateInvariants() ) {
		outInvitationId = 0;
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::RevokeRosterInvitation(
		uint64_t requestedSessionId,
		mpMatchRosterInvitationId_t invitationId, mpParticipantId requester,
		bool authorityOverride, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsParticipantForSession( requester ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	const int index = FindInvitationIndex( invitationId );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_UNKNOWN );
	}
	if ( !invitations[ index ].IsActiveAt( engineNow ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_EXPIRED );
	}
	if ( !authorityOverride && invitations[ index ].issuer != requester ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVITATION_NOT_ISSUER );
	}
	mpMatchTeams candidate = *this;
	candidate.RemoveInvitationAt( index );
	candidate.lastEngineTime = engineNow;
	mpMatchTeamsMutationResult_t result = candidate.Applied( 0, invitationId );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::ExpireRosterInvitations(
		const mpMatchSession &session, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( session.GetSessionId(), expectedRevision, reason ) ||
		!ValidateSession( session, NULL, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	mpMatchTeams candidate = *this;
	if ( candidate.RemoveExpiredInvitations( session, engineNow ) == 0 ) {
		return ObservedNoChange( engineNow, MP_MATCH_TEAMS_REASON_NONE );
	}
	candidate.lastEngineTime = engineNow;
	mpMatchTeamsMutationResult_t result = candidate.Applied( 0, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsMutationResult_t mpMatchTeams::RemoveParticipant(
		uint64_t requestedSessionId, mpParticipantId participant,
		mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( requestedSessionId, expectedRevision, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !IsParticipantForSession( participant ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	mpMatchTeams candidate = *this;
	bool changed = false;
	const int queuePosition = candidate.FindQueuePosition( participant );
	if ( queuePosition >= 0 ) {
		candidate.RemoveQueueAt( queuePosition );
		changed = true;
	}
	for ( int i = 0; i < candidate.invitationCount; ) {
		if ( candidate.invitations[ i ].target == participant ||
			candidate.invitations[ i ].issuer == participant ) {
			candidate.RemoveInvitationAt( i );
			changed = true;
		} else {
			++i;
		}
	}
	if ( !changed ) {
		return ObservedNoChange( engineNow, MP_MATCH_TEAMS_REASON_NONE );
	}
	candidate.lastEngineTime = engineNow;
	mpMatchTeamsMutationResult_t result = candidate.Applied( 0, 0 );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

mpMatchTeamsJoinDecision_t mpMatchTeams::EvaluateJoinInternal(
		const mpMatchSession &session, mpParticipantId participant,
		int requestedSide, mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow,
		bool requireInvitation, bool queueHeadOnly ) const {
	mpMatchTeamsReason_t reason;
	if ( !ValidateSession( session, &policy, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return DeniedJoin( reason );
	}
	const mpGameState_t phase = session.GetPhase();
	if ( phase != WARMUP &&
		!( policy.allowLiveJoin && IsTeamsLivePhase( phase ) ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	if ( !IsParticipantForSession( participant ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	const mpMatchParticipantState *state = session.FindParticipant( participant );
	if ( state == NULL || !state->connected ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !state->human ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN );
	}
	if ( ( policy.teamMode && !IsTeamsSide( requestedSide ) ) ||
		( !policy.teamMode && requestedSide != MP_MATCH_SIDE_NONE ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	}

	const mpMatchRosterInvitation_t *invitation = NULL;
	if ( invitationId != 0 ) {
		invitation = FindInvitation( invitationId );
		if ( invitation == NULL ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_UNKNOWN );
		}
		if ( invitation->target != participant || invitation->side != requestedSide ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_TARGET_MISMATCH );
		}
		if ( !invitation->IsActiveAt( engineNow ) ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_EXPIRED );
		}
		if ( !TeamsIssuerIsCurrent( session, invitation->issuer,
			invitation->side, invitation->operatorAuthorized ) ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
		}
		const mpMatchRosterSeat *reservedSeat =
			session.GetRosterSeat( invitation->rosterSeat );
		if ( reservedSeat == NULL || reservedSeat->side != invitation->side ||
			reservedSeat->role != invitation->role ||
			reservedSeat->occupant.IsValid() ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_ROSTER_SEAT_UNAVAILABLE );
		}
	} else if ( requireInvitation ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_REQUIRED );
	}

	const int currentRosterSeat = session.FindRosterSeat( participant );
	const mpMatchRosterSeat *currentRoster = currentRosterSeat >= 0 ?
		session.GetRosterSeat( currentRosterSeat ) : NULL;
	if ( invitation != NULL && currentRoster != NULL ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_ALREADY_ROSTERED );
	}
	if ( currentRoster != NULL && currentRoster->side != requestedSide ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_ROSTER_ALIGNMENT );
	}
	const bool rosterAuthorized = currentRoster != NULL || invitation != NULL;
	if ( policy.requireRosterMembership && !rosterAuthorized ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_ROSTER_REQUIRED );
	}
	if ( policy.teamMode && sideLocked[ requestedSide ] &&
		currentRoster == NULL &&
		!( invitation != NULL && policy.invitationBypassesLock ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_TEAM_LOCKED );
	}

	const mpMatchRosterRole_t role = invitation != NULL ? invitation->role :
		( currentRoster != NULL ? currentRoster->role : MP_MATCH_ROSTER_PLAYER );
	if ( !IsTeamsRosterRole( role ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVALID_ROLE );
	}
	const bool shouldBeActive = MPMatchRosterRoleIsActive( role );
	const int queuePosition = FindQueuePosition( participant );
	if ( queuePosition >= 0 && state->active && invitation == NULL ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_ACTIVE );
	}
	if ( invitation == NULL && queueCount > 0 ) {
		if ( queuePosition < 0 || queuePosition > 0 ) {
			return QueuedJoin( MP_MATCH_TEAMS_REASON_QUEUE_WAIT );
		}
		if ( queueHeadOnly && queuePosition != 0 ) {
			return QueuedJoin( MP_MATCH_TEAMS_REASON_QUEUE_WAIT );
		}
	}

	int activeTotal = 0;
	int activePerSide[ MP_MATCH_SIDE_COUNT ] = { 0, 0 };
	for ( int i = 0; i < MP_MATCH_MAX_PARTICIPANTS; ++i ) {
		const mpMatchParticipantState *candidate = session.GetParticipantByIndex( i );
		if ( candidate == NULL || !candidate->active ) {
			continue;
		}
		++activeTotal;
		if ( policy.teamMode ) {
			if ( !IsTeamsSide( candidate->side ) ) {
				return DeniedJoin( MP_MATCH_TEAMS_REASON_POLICY_MISMATCH );
			}
			++activePerSide[ candidate->side ];
		}
	}
	int resultingActiveTotal = activeTotal - ( state->active ? 1 : 0 ) +
		( shouldBeActive ? 1 : 0 );
	if ( resultingActiveTotal > policy.maximumActiveTotal ) {
		return invitation == NULL && policy.queueEnabled ?
			QueuedJoin( MP_MATCH_TEAMS_REASON_ACTIVE_CAPACITY ) :
			DeniedJoin( MP_MATCH_TEAMS_REASON_ACTIVE_CAPACITY );
	}
	if ( policy.teamMode && shouldBeActive ) {
		int targetCount = activePerSide[ requestedSide ];
		if ( state->active && state->side == requestedSide ) {
			--targetCount;
		}
		++targetCount;
		if ( targetCount > policy.maximumActivePerSide ) {
			return invitation == NULL && policy.queueEnabled ?
				QueuedJoin( MP_MATCH_TEAMS_REASON_TEAM_CAPACITY ) :
				DeniedJoin( MP_MATCH_TEAMS_REASON_TEAM_CAPACITY );
		}
	}

	mpMatchTeamsJoinDecision_t result;
	result.Clear();
	result.disposition = MP_MATCH_TEAMS_JOIN_ALLOW;
	result.reason = MP_MATCH_TEAMS_REASON_NONE;
	mpMatchTeamsTransactionPlan_t &plan = result.plan;
	plan.Clear();
	plan.kind = invitation != NULL ? MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE :
		( queuePosition == 0 ? MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION :
			MP_MATCH_TEAMS_TRANSACTION_DIRECT_JOIN );
	plan.sessionId = sessionId;
	plan.expectedTeamsRevision = revision;
	plan.expectedSessionRevision = session.GetSessionRevision();
	plan.incomingParticipant = participant;
	plan.side = requestedSide;
	plan.rosterRole = role;
	plan.rosterSeat = invitation != NULL ? invitation->rosterSeat : currentRosterSeat;
	if ( queuePosition >= 0 ) {
		plan.consumeQueueTicket = true;
		plan.queueTicketId = queue[ queuePosition ].ticketId;
	}
	if ( invitation != NULL ) {
		plan.consumeInvitation = true;
		plan.invitationId = invitation->invitationId;
		plan.invitationIssuer = invitation->issuer;
		plan.assignRosterSeat = true;
	}
	plan.incomingActive = shouldBeActive;
	plan.setIncomingActive = state->active != shouldBeActive;
	plan.incomingSide = requestedSide;
	plan.setIncomingSide = state->side != requestedSide;
	plan.assignIncomingRosterRole = invitation != NULL ||
		( currentRoster != NULL &&
			( state->roles & TeamsPrincipalRoleBits() ) !=
			TeamsRolesForRosterRole( role ) );
	if ( !plan.setIncomingActive && !plan.setIncomingSide &&
		!plan.consumeQueueTicket && !plan.consumeInvitation &&
		!plan.assignRosterSeat && !plan.assignIncomingRosterRole ) {
		result.disposition = MP_MATCH_TEAMS_JOIN_NO_CHANGE;
		result.plan.Clear();
		return result;
	}
	if ( !plan.IsValid() ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID );
	}
	return result;
}

mpMatchTeamsJoinDecision_t mpMatchTeams::EvaluateJoin(
		const mpMatchSession &session, mpParticipantId participant,
		int requestedSide, mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow ) const {
	return EvaluateJoinInternal( session, participant, requestedSide,
		invitationId, policy, engineNow, false, false );
}

mpMatchTeamsJoinDecision_t mpMatchTeams::PlanNextQueueAdmission(
		const mpMatchSession &session, const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow ) const {
	if ( queueCount <= 0 ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_NOT_QUEUED );
	}
	mpMatchTeamsJoinDecision_t result = EvaluateJoinInternal( session,
		queue[ 0 ].participant, queue[ 0 ].requestedSide, 0, policy,
		engineNow, false, true );
	if ( result.IsAllowed() &&
		( result.plan.kind != MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION ||
		result.plan.queueTicketId != queue[ 0 ].ticketId ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID );
	}
	return result;
}

mpMatchTeamsJoinDecision_t mpMatchTeams::PlanRosterInvitationAcceptance(
		const mpMatchSession &session, mpParticipantId target,
		mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow ) const {
	const mpMatchRosterInvitation_t *invitation = FindInvitation( invitationId );
	if ( invitation == NULL ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_UNKNOWN );
	}
	return EvaluateJoinInternal( session, target, invitation->side,
		invitationId, policy, engineNow, true, false );
}

mpMatchTeamsJoinDecision_t mpMatchTeams::PlanSubstitution(
		const mpMatchSession &session, mpParticipantId outgoingParticipant,
		mpParticipantId incomingParticipant, int side,
		mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow ) const {
	mpMatchTeamsReason_t reason;
	if ( !ValidateSession( session, &policy, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return DeniedJoin( reason );
	}
	const mpGameState_t phase = session.GetPhase();
	const bool administrativePhase = phase == WARMUP || phase == COUNTDOWN;
	if ( !administrativePhase &&
		!( policy.allowLiveSubstitution &&
			( phase == GAMEON || phase == SUDDENDEATH ) ) ) {
		return DeniedJoin( IsTeamsLivePhase( phase ) ?
			MP_MATCH_TEAMS_REASON_SUBSTITUTION_DISABLED :
			MP_MATCH_TEAMS_REASON_WRONG_PHASE );
	}
	if ( outgoingParticipant == incomingParticipant ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_SUBSTITUTION_SAME_PARTICIPANT );
	}
	if ( !IsParticipantForSession( outgoingParticipant ) ||
		!IsParticipantForSession( incomingParticipant ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID );
	}
	const mpMatchParticipantState *outgoing =
		session.FindParticipant( outgoingParticipant );
	const mpMatchParticipantState *incoming =
		session.FindParticipant( incomingParticipant );
	if ( outgoing == NULL || incoming == NULL || !outgoing->connected ||
		!incoming->connected ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !incoming->human ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN );
	}
	if ( ( policy.teamMode && !IsTeamsSide( side ) ) ||
		( !policy.teamMode && side != MP_MATCH_SIDE_NONE ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	}
	const int rosterSeat = session.FindRosterSeat( outgoingParticipant );
	const mpMatchRosterSeat *seat = rosterSeat >= 0 ?
		session.GetRosterSeat( rosterSeat ) : NULL;
	if ( seat == NULL || seat->side != side ||
		!MPMatchRosterRoleIsActive( seat->role ) || !outgoing->active ||
		outgoing->side != side ||
		( outgoing->roles & TeamsPrincipalRoleBits() ) !=
			TeamsRolesForRosterRole( seat->role ) ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_ROSTER_ALIGNMENT );
	}
	if ( incoming->active ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_ACTIVE );
	}
	const int incomingRosterSeat = session.FindRosterSeat( incomingParticipant );
	const mpMatchRosterSeat *incomingRoster = incomingRosterSeat >= 0 ?
		session.GetRosterSeat( incomingRosterSeat ) : NULL;
	const bool persistentBench = incomingRoster != NULL &&
		incomingRoster->role == MP_MATCH_ROSTER_SUBSTITUTE &&
		incomingRoster->side == side && incoming->side == side &&
		( incoming->roles & TeamsPrincipalRoleBits() ) ==
			TeamsRolesForRosterRole( MP_MATCH_ROSTER_SUBSTITUTE );
	if ( incomingRoster != NULL && !persistentBench ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_PARTICIPANT_ALREADY_ROSTERED );
	}
	if ( incomingRoster == NULL && policy.requireRosterMembership ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_ROSTER_REQUIRED );
	}
	const mpMatchRosterInvitation_t *invitation = NULL;
	if ( invitationId != 0 ) {
		if ( persistentBench ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_TARGET_MISMATCH );
		}
		invitation = FindInvitation( invitationId );
		if ( invitation == NULL ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_UNKNOWN );
		}
		if ( invitation->target != incomingParticipant ||
			invitation->side != side || invitation->role != seat->role ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_TARGET_MISMATCH );
		}
		if ( !invitation->IsActiveAt( engineNow ) ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_EXPIRED );
		}
		if ( !TeamsIssuerIsCurrent( session, invitation->issuer,
			invitation->side, invitation->operatorAuthorized ) ) {
			return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
		}
	} else if ( policy.requireInvitationForSubstitution && !persistentBench ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_INVITATION_REQUIRED );
	}

	mpMatchTeamsJoinDecision_t result;
	result.Clear();
	result.disposition = MP_MATCH_TEAMS_JOIN_ALLOW;
	result.reason = MP_MATCH_TEAMS_REASON_NONE;
	mpMatchTeamsTransactionPlan_t &plan = result.plan;
	plan.Clear();
	plan.kind = MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION;
	plan.sessionId = sessionId;
	plan.expectedTeamsRevision = revision;
	plan.expectedSessionRevision = session.GetSessionRevision();
	plan.incomingParticipant = incomingParticipant;
	plan.outgoingParticipant = outgoingParticipant;
	plan.side = side;
	plan.rosterRole = seat->role;
	plan.rosterSeat = rosterSeat;
	const int queuePosition = FindQueuePosition( incomingParticipant );
	if ( queuePosition >= 0 ) {
		plan.consumeQueueTicket = true;
		plan.queueTicketId = queue[ queuePosition ].ticketId;
	}
	if ( invitation != NULL ) {
		plan.consumeInvitation = true;
		plan.invitationId = invitation->invitationId;
		plan.invitationIssuer = invitation->issuer;
	}
	const bool incomingShouldBeActive = MPMatchRosterRoleIsActive( seat->role );
	plan.incomingActive = incomingShouldBeActive;
	plan.setIncomingActive = incoming->active != incomingShouldBeActive;
	plan.incomingSide = side;
	plan.setIncomingSide = incoming->side != side;
	plan.setOutgoingActive = true;
	plan.outgoingActive = false;
	plan.assignIncomingRosterRole = true;
	plan.vacateRosterSeat = true;
	plan.assignRosterSeat = true;
	if ( persistentBench ) {
		plan.outgoingRosterRole = MP_MATCH_ROSTER_SUBSTITUTE;
		plan.outgoingRosterSeat = incomingRosterSeat;
		plan.assignOutgoingRosterRole = true;
		plan.vacateOutgoingRosterSeat = true;
		plan.assignOutgoingRosterSeat = true;
	} else {
		plan.setOutgoingSide = true;
		plan.outgoingSide = MP_MATCH_SIDE_NONE;
		plan.clearOutgoingRosterRole = true;
	}
	if ( !plan.IsValid() ) {
		return DeniedJoin( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID );
	}
	return result;
}

bool mpMatchTeams::PlansEqual( const mpMatchTeamsTransactionPlan_t &lhs,
		const mpMatchTeamsTransactionPlan_t &rhs ) const {
	return lhs.kind == rhs.kind && lhs.sessionId == rhs.sessionId &&
		lhs.expectedTeamsRevision == rhs.expectedTeamsRevision &&
		lhs.expectedSessionRevision == rhs.expectedSessionRevision &&
		lhs.incomingParticipant == rhs.incomingParticipant &&
		lhs.outgoingParticipant == rhs.outgoingParticipant &&
	lhs.invitationIssuer == rhs.invitationIssuer && lhs.side == rhs.side &&
	lhs.rosterRole == rhs.rosterRole && lhs.rosterSeat == rhs.rosterSeat &&
	lhs.outgoingRosterRole == rhs.outgoingRosterRole &&
	lhs.outgoingRosterSeat == rhs.outgoingRosterSeat &&
		lhs.queueTicketId == rhs.queueTicketId &&
		lhs.invitationId == rhs.invitationId &&
		lhs.consumeQueueTicket == rhs.consumeQueueTicket &&
		lhs.consumeInvitation == rhs.consumeInvitation &&
		lhs.setIncomingActive == rhs.setIncomingActive &&
		lhs.incomingActive == rhs.incomingActive &&
		lhs.setIncomingSide == rhs.setIncomingSide &&
		lhs.incomingSide == rhs.incomingSide &&
		lhs.assignIncomingRosterRole == rhs.assignIncomingRosterRole &&
		lhs.setOutgoingActive == rhs.setOutgoingActive &&
		lhs.outgoingActive == rhs.outgoingActive &&
		lhs.setOutgoingSide == rhs.setOutgoingSide &&
	lhs.outgoingSide == rhs.outgoingSide &&
	lhs.clearOutgoingRosterRole == rhs.clearOutgoingRosterRole &&
	lhs.assignOutgoingRosterRole == rhs.assignOutgoingRosterRole &&
	lhs.vacateRosterSeat == rhs.vacateRosterSeat &&
	lhs.vacateOutgoingRosterSeat == rhs.vacateOutgoingRosterSeat &&
	lhs.assignRosterSeat == rhs.assignRosterSeat &&
	lhs.assignOutgoingRosterSeat == rhs.assignOutgoingRosterSeat;
}

mpMatchTeamsMutationResult_t mpMatchTeams::CommitTransactionPlan(
		const mpMatchTeamsTransactionPlan_t &plan,
		const mpMatchSession &session, const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision ) {
	mpMatchTeamsReason_t reason;
	if ( !CanMutate( plan.sessionId, expectedRevision, reason ) ||
		!ValidateSession( session, &policy, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return Rejected( reason );
	}
	if ( !plan.IsValid() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID );
	}
	if ( plan.expectedTeamsRevision != revision ) {
		return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
	}
	if ( plan.expectedSessionRevision != session.GetSessionRevision() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_STALE_SESSION_REVISION );
	}

	mpMatchTeamsJoinDecision_t current;
	switch ( plan.kind ) {
		case MP_MATCH_TEAMS_TRANSACTION_DIRECT_JOIN:
			current = EvaluateJoinInternal( session, plan.incomingParticipant,
				plan.side, 0, policy, engineNow, false, false );
			break;
		case MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION:
			current = PlanNextQueueAdmission( session, policy, engineNow );
			break;
		case MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE:
			current = PlanRosterInvitationAcceptance( session,
				plan.incomingParticipant, plan.invitationId, policy, engineNow );
			break;
		case MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION:
			current = PlanSubstitution( session, plan.outgoingParticipant,
				plan.incomingParticipant, plan.side, plan.invitationId,
				policy, engineNow );
			break;
		default:
			return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID );
	}
	if ( !current.IsAllowed() || !PlansEqual( plan, current.plan ) ) {
		return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
	}

	mpMatchTeams candidate = *this;
	if ( plan.consumeQueueTicket ) {
		const int queuePosition = candidate.FindQueuePosition(
			plan.incomingParticipant );
		if ( queuePosition < 0 ||
			candidate.queue[ queuePosition ].ticketId != plan.queueTicketId ||
			( plan.kind == MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION &&
				queuePosition != 0 ) ) {
			return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
		}
		candidate.RemoveQueueAt( queuePosition );
	}
	if ( plan.consumeInvitation ) {
		const int invitationIndex = candidate.FindInvitationIndex(
			plan.invitationId );
		if ( invitationIndex < 0 ||
			!candidate.invitations[ invitationIndex ].IsActiveAt( engineNow ) ||
			candidate.invitations[ invitationIndex ].target !=
				plan.incomingParticipant ||
			candidate.invitations[ invitationIndex ].side != plan.side ||
			candidate.invitations[ invitationIndex ].role != plan.rosterRole ||
			candidate.invitations[ invitationIndex ].issuer !=
				plan.invitationIssuer ) {
			return Rejected( MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
		}
		candidate.RemoveInvitationAt( invitationIndex );
	}
	candidate.lastEngineTime = engineNow;
	mpMatchTeamsMutationResult_t result = candidate.Applied(
		plan.queueTicketId, plan.invitationId );
	if ( !candidate.ValidateInvariants() ) {
		return Rejected( MP_MATCH_TEAMS_REASON_INVARIANT );
	}
	*this = candidate;
	return result;
}

bool mpMatchTeams::BuildRecipientSnapshot( const mpMatchSession &session,
		mpParticipantId recipient,
		mpMatchEngineTime engineNow,
		mpMatchTeamsRecipientSnapshot_t &out ) const {
	out.Clear();
	mpMatchTeamsReason_t reason;
	if ( !ValidateSession( session, NULL, reason ) ||
		!ValidateTime( engineNow, reason ) ) {
		return false;
	}
	if ( recipient.IsValid() && ( !IsParticipantForSession( recipient ) ||
		session.FindParticipant( recipient ) == NULL ) ) {
		return false;
	}
	out.sessionId = sessionId;
	out.revision = revision;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		out.sideLocked[ side ] = sideLocked[ side ];
	}
	out.queueCount = queueCount;
	if ( recipient.IsValid() ) {
		const int queuePosition = FindQueuePosition( recipient );
		if ( queuePosition >= 0 ) {
			out.recipientQueued = true;
			out.recipientQueuePosition = queuePosition;
			out.recipientQueueTicketId = queue[ queuePosition ].ticketId;
			out.recipientRequestedSide = queue[ queuePosition ].requestedSide;
		}
		for ( int i = 0; i < invitationCount; ++i ) {
			if ( invitations[ i ].target == recipient &&
				invitations[ i ].IsActiveAt( engineNow ) &&
				TeamsIssuerIsCurrent( session, invitations[ i ].issuer,
					invitations[ i ].side, invitations[ i ].operatorAuthorized ) ) {
				if ( out.invitationCount >= MP_MATCH_TEAMS_MAX_INVITATIONS ) {
					out.Clear();
					return false;
				}
				out.invitations[ out.invitationCount++ ] = invitations[ i ];
			}
		}
	}
	return true;
}

bool mpMatchTeams::ValidateInvariants( void ) const {
	if ( queueCount < 0 || queueCount > MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES ||
		invitationCount < 0 ||
		invitationCount > MP_MATCH_TEAMS_MAX_INVITATIONS ) {
		return false;
	}
	if ( sessionId == 0 ) {
		if ( revision != 0 || queueCount != 0 || invitationCount != 0 ||
			nextQueueTicketId != 1 || nextInvitationId != 1 ) {
			return false;
		}
		for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
			if ( sideLocked[ side ] ) {
				return false;
			}
		}
	} else if ( revision == 0 || !lastEngineTime.IsValid() ) {
		return false;
	}

	mpMatchQueueTicketId_t previousTicket = 0;
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES; ++i ) {
		const mpMatchQueueEntry_t &entry = queue[ i ];
		if ( i >= queueCount ) {
			if ( entry.IsOccupied() ) {
				return false;
			}
			continue;
		}
		if ( !entry.IsOccupied() || !IsParticipantForSession( entry.participant ) ||
			( entry.requestedSide != MP_MATCH_SIDE_NONE &&
				!IsTeamsSide( entry.requestedSide ) ) ||
			!entry.enqueuedAt.IsValid() || !entry.positionedAt.IsValid() ||
			entry.enqueuedAt > entry.positionedAt ||
			entry.positionedAt > lastEngineTime || entry.ticketId <= previousTicket ) {
			return false;
		}
		for ( int other = 0; other < i; ++other ) {
			if ( queue[ other ].participant == entry.participant ) {
				return false;
			}
		}
		previousTicket = entry.ticketId;
	}
	if ( nextQueueTicketId != 0 && nextQueueTicketId <= previousTicket ) {
		return false;
	}

	mpMatchRosterInvitationId_t previousInvitationId = 0;
	for ( int i = 0; i < MP_MATCH_TEAMS_MAX_INVITATIONS; ++i ) {
		const mpMatchRosterInvitation_t &invitation = invitations[ i ];
		if ( i >= invitationCount ) {
			if ( invitation.IsOccupied() ) {
				return false;
			}
			continue;
		}
		if ( !invitation.IsOccupied() ||
			invitation.invitationId > MP_MATCH_TEAMS_MAX_INVITATION_ID ||
			invitation.invitationId <= previousInvitationId ||
			invitation.sessionId != sessionId ||
			!IsParticipantForSession( invitation.target ) ||
			!IsParticipantForSession( invitation.issuer ) ||
			invitation.target == invitation.issuer ||
			( invitation.side != MP_MATCH_SIDE_NONE &&
				!IsTeamsSide( invitation.side ) ) ||
			!IsTeamsRosterRole( invitation.role ) || invitation.rosterSeat < 0 ||
			invitation.rosterSeat >= MP_MATCH_MAX_ROSTER_SEATS ||
			!invitation.issuedAt.IsValid() || !invitation.expiresAt.IsValid() ||
			invitation.issuedAt > lastEngineTime ||
			invitation.expiresAt <= invitation.issuedAt ||
			invitation.expiresAt.Milliseconds() -
				invitation.issuedAt.Milliseconds() >
				MP_MATCH_TEAMS_MAX_INVITATION_MSEC ) {
			return false;
		}
		for ( int other = 0; other < i; ++other ) {
			if ( invitations[ other ].rosterSeat == invitation.rosterSeat ||
				( invitations[ other ].target == invitation.target &&
				invitations[ other ].side == invitation.side &&
				invitations[ other ].role == invitation.role &&
				invitations[ other ].issuer == invitation.issuer ) ) {
				return false;
			}
		}
		previousInvitationId = invitation.invitationId;
	}
	if ( nextInvitationId != 0 &&
		( nextInvitationId > MP_MATCH_TEAMS_MAX_INVITATION_ID ||
		nextInvitationId <= previousInvitationId ) ) {
		return false;
	}
	return true;
}
