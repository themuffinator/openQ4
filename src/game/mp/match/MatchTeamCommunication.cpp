//----------------------------------------------------------------
// MatchTeamCommunication.cpp
//----------------------------------------------------------------

#include "MatchTeamCommunication.h"

namespace {

static bool IsValidMedium( mpMatchTeamCommunicationMedium_t medium ) {
	return medium >= MP_MATCH_TEAM_COMMUNICATION_TEXT &&
		medium < MP_MATCH_TEAM_COMMUNICATION_MEDIUM_COUNT;
}

static bool IsValidSide( int side ) {
	return side >= 0 && side < MP_MATCH_SIDE_COUNT;
}

static mpMatchTeamCommunicationDecision_t Denied(
		mpMatchTeamCommunicationReason_t reason ) {
	mpMatchTeamCommunicationDecision_t decision;
	decision.Clear();
	decision.reason = reason;
	return decision;
}

static bool ResolveCurrentBinding( const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &binding,
		const mpMatchParticipantState *&outState ) {
	outState = NULL;
	if ( !binding.IsStructurallyValid() ||
		binding.sessionId != session.GetSessionId() ) {
		return false;
	}

	mpParticipantId resolvedParticipant;
	if ( !session.ResolveSlotBinding( binding.slot, binding.slotGeneration,
			resolvedParticipant ) || resolvedParticipant != binding.participant ) {
		return false;
	}

	const mpMatchParticipantState *state =
		session.FindParticipant( resolvedParticipant );
	if ( state == NULL || !state->connected || state->id != binding.participant ||
		state->slot != binding.slot ||
		state->slotGeneration != binding.slotGeneration ) {
		return false;
	}
	outState = state;
	return true;
}

static bool IsManagedTeamCommunicator(
		const mpMatchParticipantState &participant ) {
	if ( !participant.connected || !participant.human ||
		!IsValidSide( participant.side ) ||
		!MPMatchRoleMaskIsValid( participant.roles, true ) ) {
		return false;
	}

	const mpMatchRoleMask_t player =
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
	const mpMatchRoleMask_t captain =
		MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN );
	const mpMatchRoleMask_t coach =
		MPMatchRoleBit( MP_MATCH_ROLE_COACH );

	if ( participant.active ) {
		return participant.roles == player ||
			participant.roles == ( player | captain );
	}
	return participant.roles == coach;
}

} // namespace

void mpMatchTeamCommunicationBinding_s::Clear( void ) {
	sessionId = 0;
	participant = mpParticipantId::Invalid();
	slot = -1;
	slotGeneration = 0;
}

bool mpMatchTeamCommunicationBinding_s::IsStructurallyValid( void ) const {
	return sessionId != 0 && participant.IsValid() &&
		participant.SessionPart() == sessionId &&
		slot >= 0 && slot < MP_MATCH_MAX_CONNECTION_SLOTS &&
		slotGeneration != 0;
}

void mpMatchTeamCommunicationDecision_s::Clear( void ) {
	allowed = false;
	reason = MP_MATCH_TEAM_COMMUNICATION_REASON_INVALID_MEDIUM;
	side = MP_MATCH_SIDE_NONE;
}

bool mpMatchTeamCommunicationDecision_s::IsAllowed( void ) const {
	return allowed && reason == MP_MATCH_TEAM_COMMUNICATION_REASON_NONE &&
		IsValidSide( side );
}

bool MPMatchBuildTeamCommunicationBinding( const mpMatchSession &session,
		int slot, uint32_t slotGeneration,
		mpMatchTeamCommunicationBinding_t &outBinding ) {
	outBinding.Clear();
	mpParticipantId participant;
	if ( !session.ResolveSlotBinding( slot, slotGeneration, participant ) ) {
		return false;
	}

	const mpMatchParticipantState *state = session.FindParticipant( participant );
	if ( state == NULL || !state->connected || state->slot != slot ||
		state->slotGeneration != slotGeneration ) {
		return false;
	}

	outBinding.sessionId = session.GetSessionId();
	outBinding.participant = participant;
	outBinding.slot = slot;
	outBinding.slotGeneration = slotGeneration;
	if ( !outBinding.IsStructurallyValid() ) {
		outBinding.Clear();
		return false;
	}
	return true;
}

mpMatchTeamCommunicationDecision_t MPMatchEvaluateManagedTeamCommunication(
		const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &sender,
		const mpMatchTeamCommunicationBinding_t &recipient,
		mpMatchTeamCommunicationMedium_t medium ) {
	if ( !IsValidMedium( medium ) ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_INVALID_MEDIUM );
	}
	if ( !session.GetReadinessPolicy().teamMode ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_NOT_TEAM_MODE );
	}

	const mpMatchParticipantState *senderState = NULL;
	if ( !ResolveCurrentBinding( session, sender, senderState ) ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_BINDING_STALE );
	}
	const mpMatchParticipantState *recipientState = NULL;
	if ( !ResolveCurrentBinding( session, recipient, recipientState ) ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_BINDING_STALE );
	}
	if ( !IsManagedTeamCommunicator( *senderState ) ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_INELIGIBLE );
	}
	if ( !IsManagedTeamCommunicator( *recipientState ) ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_INELIGIBLE );
	}
	if ( senderState->side != recipientState->side ) {
		return Denied( MP_MATCH_TEAM_COMMUNICATION_REASON_SIDE_MISMATCH );
	}

	mpMatchTeamCommunicationDecision_t decision;
	decision.allowed = true;
	decision.reason = MP_MATCH_TEAM_COMMUNICATION_REASON_NONE;
	decision.side = senderState->side;
	return decision;
}

bool MPMatchMayReceiveManagedTeamText( const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &sender,
		const mpMatchTeamCommunicationBinding_t &recipient ) {
	return MPMatchEvaluateManagedTeamCommunication( session, sender, recipient,
		MP_MATCH_TEAM_COMMUNICATION_TEXT ).IsAllowed();
}

bool MPMatchMayReceiveManagedTeamVoice( const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &sender,
		const mpMatchTeamCommunicationBinding_t &recipient ) {
	return MPMatchEvaluateManagedTeamCommunication( session, sender, recipient,
		MP_MATCH_TEAM_COMMUNICATION_VOICE ).IsAllowed();
}
