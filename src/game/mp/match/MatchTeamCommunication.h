//----------------------------------------------------------------
// MatchTeamCommunication.h
//
// Allocation-free routing policy for managed team text and voice.
//
// This policy is deliberately narrower than the legacy chat/voice system.  A
// live adapter calls it only after the committed rules say that the match is
// managed; casual routing remains outside this core and therefore unchanged.
// Slots, names and caller-supplied role claims are never identity.  Every
// endpoint carries the session-scoped participant id and slot generation that
// are re-resolved against the current authoritative session before a decision.
//----------------------------------------------------------------

#ifndef __MP_MATCH_TEAM_COMMUNICATION_H__
#define __MP_MATCH_TEAM_COMMUNICATION_H__

#include "MatchSession.h"

#include <stdint.h>

typedef enum {
	MP_MATCH_TEAM_COMMUNICATION_TEXT = 0,
	MP_MATCH_TEAM_COMMUNICATION_VOICE,
	MP_MATCH_TEAM_COMMUNICATION_MEDIUM_COUNT
} mpMatchTeamCommunicationMedium_t;

// Decision reasons are local policy diagnostics, not wire or evidence values.
typedef enum {
	MP_MATCH_TEAM_COMMUNICATION_REASON_NONE = 0,
	MP_MATCH_TEAM_COMMUNICATION_REASON_INVALID_MEDIUM,
	MP_MATCH_TEAM_COMMUNICATION_REASON_NOT_TEAM_MODE,
	MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_BINDING_STALE,
	MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_BINDING_STALE,
	MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_INELIGIBLE,
	MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_INELIGIBLE,
	MP_MATCH_TEAM_COMMUNICATION_REASON_SIDE_MISMATCH,
	MP_MATCH_TEAM_COMMUNICATION_REASON_COUNT
} mpMatchTeamCommunicationReason_t;

typedef struct mpMatchTeamCommunicationBinding_s {
	uint64_t			sessionId;
	mpParticipantId	participant;
	int				slot;
	uint32_t			slotGeneration;

	void Clear( void );
	bool IsStructurallyValid( void ) const;
} mpMatchTeamCommunicationBinding_t;

typedef struct mpMatchTeamCommunicationDecision_s {
	bool						allowed;
	mpMatchTeamCommunicationReason_t reason;
	int						side;

	void Clear( void );
	bool IsAllowed( void ) const;
} mpMatchTeamCommunicationDecision_t;

// Builds one complete endpoint identity from a trusted connection slot and
// generation.  The returned binding remains safe to retain: disconnect,
// reconnect, slot reuse and session replacement all make it fail closed.
bool MPMatchBuildTeamCommunicationBinding( const mpMatchSession &session,
	int slot, uint32_t slotGeneration,
	mpMatchTeamCommunicationBinding_t &outBinding );

// Both media intentionally share one authorization decision.  Transport
// muting, flood control, packet decoding and delivery remain adapter concerns.
mpMatchTeamCommunicationDecision_t MPMatchEvaluateManagedTeamCommunication(
	const mpMatchSession &session,
	const mpMatchTeamCommunicationBinding_t &sender,
	const mpMatchTeamCommunicationBinding_t &recipient,
	mpMatchTeamCommunicationMedium_t medium );

bool MPMatchMayReceiveManagedTeamText( const mpMatchSession &session,
	const mpMatchTeamCommunicationBinding_t &sender,
	const mpMatchTeamCommunicationBinding_t &recipient );
bool MPMatchMayReceiveManagedTeamVoice( const mpMatchSession &session,
	const mpMatchTeamCommunicationBinding_t &sender,
	const mpMatchTeamCommunicationBinding_t &recipient );

#endif // __MP_MATCH_TEAM_COMMUNICATION_H__
