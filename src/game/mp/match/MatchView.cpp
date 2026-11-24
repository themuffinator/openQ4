//----------------------------------------------------------------
// MatchView.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_VIEW_STANDALONE_TEST )
#include <stdint.h>
#include <string.h>
#else
#include "../../../idlib/precompiled.h"
#pragma hdrstop
#endif

#include "MatchView.h"

static_assert( sizeof( mpMatchProtocolSessionId_t ) == 8,
	"MatchView requires a full-width session id" );
static_assert( sizeof( mpMatchProtocolRevision_t ) == 8,
	"MatchView requires a full-width session revision" );
static_assert( sizeof( mpMatchViewAllowedOperationMask_t ) == 8,
	"MatchView requires a full-width allowed-operation mask" );
static_assert( MP_MATCH_OP_COUNT <= MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES,
	"MatchView operation decisions no longer cover the operation registry" );
static_assert( MP_MATCH_OP_COUNT <= 64,
	"MatchView operation mask no longer covers the operation registry" );
static_assert( INACTIVE == 0 && WARMUP == 1 && COUNTDOWN == 2 && GAMEON == 3 &&
	SUDDENDEATH == 4 && GAMEREVIEW == 5 && NEXTGAME == 6 && STATE_COUNT == 7,
	"MatchView phase wire values changed" );
static_assert( RS_INACTIVE == 0 && RS_COUNTDOWN == 1 && RS_ACTIVE == 2 &&
	RS_COMPLETE == 3 && RS_STATE_COUNT == 4,
	"MatchView round wire values changed" );

namespace {

static const int MP_MATCH_VIEW_ENVELOPE_HEADER_BYTES = 15;
static const int MP_MATCH_VIEW_MAX_PAYLOAD_BYTES =
	MP_MATCH_VIEW_MAX_MESSAGE_BYTES - MP_MATCH_VIEW_ENVELOPE_HEADER_BYTES;
static const unsigned long long MP_MATCH_VIEW_MAX_SIGNED_TIME = 0x7fffffffffffffffull;
static const unsigned int MP_MATCH_VIEW_MAX_OPERATION_ID = 0x7fffffffu;
static const unsigned char MP_MATCH_VIEW_OPTIONAL_EXTENSION_BIT = 0x80;
static const unsigned char MP_MATCH_VIEW_FIELD_ID_MASK = 0x7f;
static const unsigned char MP_MATCH_VIEW_REQUIRED_FIELD_COUNT = 25;
static const unsigned char MP_MATCH_VIEW_KNOWN_SIDE_MASK =
	( 1u << MP_MATCH_VIEW_SIDE_COUNT ) - 1u;
static const unsigned char MP_MATCH_VIEW_PARTICIPANT_CONNECTED_BIT = 1u << 0;
static const unsigned char MP_MATCH_VIEW_PARTICIPANT_HUMAN_BIT = 1u << 1;
static const unsigned char MP_MATCH_VIEW_PARTICIPANT_ACTIVE_BIT = 1u << 2;
static const unsigned char MP_MATCH_VIEW_PARTICIPANT_FLAG_MASK =
	MP_MATCH_VIEW_PARTICIPANT_CONNECTED_BIT |
	MP_MATCH_VIEW_PARTICIPANT_HUMAN_BIT |
	MP_MATCH_VIEW_PARTICIPANT_ACTIVE_BIT;

typedef enum {
	MP_MATCH_VIEW_FIELD_SCHEMA = 1,
	MP_MATCH_VIEW_FIELD_REVISION = 2,
	MP_MATCH_VIEW_FIELD_LIFECYCLE = 3,
	MP_MATCH_VIEW_FIELD_CLOCKS = 4,
	MP_MATCH_VIEW_FIELD_READINESS = 5,
	MP_MATCH_VIEW_FIELD_TIMEOUTS = 6,
	MP_MATCH_VIEW_FIELD_ROLES = 7,
	MP_MATCH_VIEW_FIELD_TEAMS = 8,
	MP_MATCH_VIEW_FIELD_PROPOSALS = 9,
	MP_MATCH_VIEW_FIELD_SERIES = 10,
	MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY = 11,
	MP_MATCH_VIEW_FIELD_DENIAL = 12,
	MP_MATCH_VIEW_FIELD_TEAM_VITALS = 13,
	MP_MATCH_VIEW_FIELD_ITEM_TIMINGS = 14,
	MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS = 15,
	MP_MATCH_VIEW_FIELD_RECIPIENT = 16,
	MP_MATCH_VIEW_FIELD_VIEW_REVISION = 17,
	MP_MATCH_VIEW_FIELD_PARTICIPANTS = 18,
	MP_MATCH_VIEW_FIELD_RULES = 19,
	MP_MATCH_VIEW_FIELD_ROSTER_SEATS = 20,
	MP_MATCH_VIEW_FIELD_INVITATIONS = 21,
	MP_MATCH_VIEW_FIELD_QUEUES = 22,
	MP_MATCH_VIEW_FIELD_CONTROL_REVISION = 23,
	MP_MATCH_VIEW_FIELD_EVIDENCE = 24,
	MP_MATCH_VIEW_FIELD_TERMINAL_RESULT = 25
} mpMatchViewField_t;

static bool BuildPayload( const mpSessionView &view, byte *encoded,
	int &encodedLength, mpMatchViewError_t *error );

static void ClearError( mpMatchViewError_t *error ) {
	if ( error != 0 ) {
		error->Clear();
	}
}

static void SetError( mpMatchViewError_t *error, mpMatchViewErrorReason_t reason,
	unsigned char fieldId = 0, unsigned int detail = 0 ) {
	if ( error != 0 ) {
		error->reason = reason;
		error->fieldId = fieldId;
		error->detail = detail;
	}
}

static bool IsSide( int side ) {
	return side >= 0 && side < MP_MATCH_VIEW_SIDE_COUNT;
}

static bool IsSpectatorSideAudience( mpMatchViewAudience_t audience ) {
	return audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0 ||
		audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1;
}

static int SpectatorAudienceSide( mpMatchViewAudience_t audience ) {
	if ( audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0 ) {
		return 0;
	}
	if ( audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1 ) {
		return 1;
	}
	return MP_MATCH_VIEW_SIDE_NONE;
}

static unsigned char SideToWire( int side ) {
	return side == MP_MATCH_VIEW_SIDE_NONE ? 0xffu : static_cast<unsigned char>( side );
}

static bool SideFromWire( unsigned char wire, int &side ) {
	if ( wire == 0xffu ) {
		side = MP_MATCH_VIEW_SIDE_NONE;
		return true;
	}
	if ( wire < MP_MATCH_VIEW_SIDE_COUNT ) {
		side = wire;
		return true;
	}
	return false;
}

static bool IsTime( unsigned long long value ) {
	return value <= MP_MATCH_VIEW_MAX_SIGNED_TIME;
}

static int BoundedLength( const char *value, int maximum ) {
	if ( value == 0 || maximum < 0 ) {
		return -1;
	}
	for ( int i = 0; i <= maximum; ++i ) {
		if ( value[ i ] == '\0' ) {
			return i;
		}
	}
	return -1;
}

static bool IsMachineTokenCharacter( unsigned char value ) {
	return ( value >= 'a' && value <= 'z' ) ||
		( value >= 'A' && value <= 'Z' ) ||
		( value >= '0' && value <= '9' ) || value == '_' || value == '-' || value == '.';
}

static bool IsMachineToken( const char *value, int length, int maximum ) {
	if ( value == 0 || length < 1 || length > maximum || value[ length ] != '\0' ) {
		return false;
	}
	for ( int i = 0; i < length; ++i ) {
		if ( !IsMachineTokenCharacter( static_cast<unsigned char>( value[ i ] ) ) ) {
			return false;
		}
	}
	return true;
}

static bool IsMapToken( const char *value, int length ) {
	if ( value == 0 || length < 1 || length > MP_MATCH_VIEW_MAP_TOKEN_BYTES ||
		value[ length ] != '\0' || value[ 0 ] == '/' ) {
		return false;
	}
	for ( int i = 0; i < length; ++i ) {
		const unsigned char character = static_cast<unsigned char>( value[ i ] );
		if ( !IsMachineTokenCharacter( character ) && character != '/' ) {
			return false;
		}
		if ( character == '.' && i + 1 < length && value[ i + 1 ] == '.' ) {
			return false;
		}
	}
	return true;
}

static bool IsValidOpcode( mpMatchOperationOpcode_t opcode ) {
	return opcode > MP_MATCH_OP_INVALID && opcode < MP_MATCH_OP_COUNT;
}

static unsigned int AllPublicRoleBits( void ) {
	return ( 1u << static_cast<unsigned int>( MP_MATCH_VIEW_ROLE_COUNT ) ) - 2u;
}

static bool IsAllZero( const char *value, int bytes ) {
	for ( int i = 0; i < bytes; ++i ) {
		if ( value[ i ] != '\0' ) {
			return false;
		}
	}
	return true;
}

static int ResultNameCodePointBytes( const char *value, int remaining ) {
	if ( remaining < 1 ) return 0;
	const unsigned char first = static_cast<unsigned char>( value[ 0 ] );
	int bytes = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
		first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
	if ( bytes == 0 || bytes > remaining ) return 0;
	unsigned int point = first & ( bytes == 1 ? 0x7f : bytes == 2 ? 0x1f : bytes == 3 ? 0x0f : 7 );
	for ( int i = 1; i < bytes; ++i ) {
		const unsigned char next = static_cast<unsigned char>( value[ i ] );
		if ( ( next & 0xc0 ) != 0x80 ) return 0;
		point = ( point << 6 ) | ( next & 0x3f );
	}
	if ( ( bytes == 2 && point < 0x80 ) || ( bytes == 3 && point < 0x800 ) ||
		( bytes == 4 && point < 0x10000 ) || point > 0x10ffff ||
		( point >= 0xd800 && point <= 0xdfff ) || point < 0x20 ||
		( point >= 0x7f && point <= 0x9f ) || point == 0x2028 || point == 0x2029 ||
		( point >= 0x202a && point <= 0x202e ) || ( point >= 0x2066 && point <= 0x2069 ) ) return 0;
	return bytes;
}

static bool ValidateTerminalResult( const mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	const mpMatchViewTerminalResult_t &result = state.terminalResult;
	bool valid = result.outcome >= MP_MATCH_VIEW_RESULT_NONE &&
		result.outcome < MP_MATCH_VIEW_RESULT_OUTCOME_COUNT &&
		result.reason >= MP_MATCH_VIEW_RESULT_REASON_NONE &&
		result.reason < MP_MATCH_VIEW_RESULT_REASON_COUNT &&
		( result.winnerSide == MP_MATCH_VIEW_SIDE_NONE || IsSide( result.winnerSide ) ) &&
		result.winnerNameLength <= MP_MATCH_VIEW_RESULT_NAME_BYTES;
	if ( !valid ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
		return false;
	}
	valid = IsAllZero( result.winnerName + result.winnerNameLength,
		sizeof( result.winnerName ) - result.winnerNameLength );
	for ( int offset = 0; valid && offset < result.winnerNameLength; ) {
		const int bytes = ResultNameCodePointBytes( result.winnerName + offset,
			result.winnerNameLength - offset );
		valid = bytes != 0;
		offset += bytes;
	}
	const bool individualWinner = result.winnerParticipantId != MP_MATCH_INVALID_PARTICIPANT_ID;
	const bool teamWinner = IsSide( result.winnerSide );
	const bool hasWinner = result.outcome == MP_MATCH_VIEW_RESULT_DECIDED ||
		result.outcome == MP_MATCH_VIEW_RESULT_FORFEIT;
	if ( result.outcome == MP_MATCH_VIEW_RESULT_NONE ) {
		valid = valid && result.reason == MP_MATCH_VIEW_RESULT_REASON_NONE && result.resultRevision == 0;
	} else {
		valid = valid && result.resultRevision != 0 && result.resultRevision <= state.sessionRevision &&
			( state.lifecycle.phase == GAMEREVIEW || state.lifecycle.phase == NEXTGAME || state.lifecycle.phase == WARMUP );
		if ( result.outcome == MP_MATCH_VIEW_RESULT_DECIDED || result.outcome == MP_MATCH_VIEW_RESULT_DRAW ) {
			valid = valid && result.reason == MP_MATCH_VIEW_RESULT_REASON_LIMIT_REACHED;
		} else if ( result.outcome == MP_MATCH_VIEW_RESULT_FORFEIT ) {
			valid = valid && result.reason == MP_MATCH_VIEW_RESULT_REASON_FORFEIT;
		} else {
			// An unresolved forfeit is explicitly an abort, never a guessed winner.
			valid = valid && result.reason >= MP_MATCH_VIEW_RESULT_REASON_FORFEIT;
		}
	}
	valid = valid && ( hasWinner ? individualWinner != teamWinner : !individualWinner && !teamWinner ) &&
		( individualWinner ? result.winnerNameLength != 0 : result.winnerNameLength == 0 );
	if ( !valid ) SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
	return valid;
}

static int CountBits64( unsigned long long value ) {
	int count = 0;
	while ( value != 0 ) {
		value &= value - 1;
		++count;
	}
	return count;
}

static void WriteUInt64Value( idBitMsg &message, unsigned long long value ) {
	message.WriteLong( static_cast<int>( value & 0xffffffffull ) );
	message.WriteLong( static_cast<int>( value >> 32 ) );
}

static bool ReadByteValue( idBitMsg &message, unsigned char &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() < 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId );
		return false;
	}
	value = static_cast<unsigned char>( message.ReadByte() );
	return true;
}

static bool ReadBoolValue( idBitMsg &message, bool &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	unsigned char raw = 0;
	if ( !ReadByteValue( message, raw, fieldId, error ) ) {
		return false;
	}
	if ( raw > 1 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, fieldId, raw );
		return false;
	}
	value = raw != 0;
	return true;
}

static bool ReadUShortValue( idBitMsg &message, unsigned short &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() < 16 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId );
		return false;
	}
	value = static_cast<unsigned short>( message.ReadUShort() );
	return true;
}

static bool ReadUIntValue( idBitMsg &message, unsigned int &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() < 32 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId );
		return false;
	}
	value = static_cast<unsigned int>( message.ReadLong() );
	return true;
}

static bool ReadIntValue( idBitMsg &message, int &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() < 32 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId );
		return false;
	}
	value = message.ReadLong();
	return true;
}

static bool ReadUInt64Value( idBitMsg &message, unsigned long long &value,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() < 64 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId );
		return false;
	}
	const unsigned int low = static_cast<unsigned int>( message.ReadLong() );
	const unsigned int high = static_cast<unsigned int>( message.ReadLong() );
	value = static_cast<unsigned long long>( low ) |
		( static_cast<unsigned long long>( high ) << 32 );
	return true;
}

static bool ReadSideValue( idBitMsg &message, int &side,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	unsigned char wire = 0;
	if ( !ReadByteValue( message, wire, fieldId, error ) ) {
		return false;
	}
	if ( !SideFromWire( wire, side ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, fieldId, wire );
		return false;
	}
	return true;
}

static bool ReadMapToken( idBitMsg &message, unsigned char &length, char *token,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( !ReadByteValue( message, length, fieldId, error ) ) {
		return false;
	}
	if ( length < 1 || length > MP_MATCH_VIEW_MAP_TOKEN_BYTES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING, fieldId, length );
		return false;
	}
	if ( message.GetRemainingReadBits() < static_cast<int>( length ) * 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId, length );
		return false;
	}
	memset( token, 0, MP_MATCH_VIEW_MAP_TOKEN_BYTES + 1 );
	if ( message.ReadData( token, length ) != length ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId, length );
		return false;
	}
	token[ length ] = '\0';
	if ( !IsMapToken( token, length ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING, fieldId, length );
		return false;
	}
	return true;
}

static bool ReadMachineToken( idBitMsg &message, unsigned char &length, char *token,
	int maximum, unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( !ReadByteValue( message, length, fieldId, error ) ) {
		return false;
	}
	if ( length < 1 || length > maximum ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING, fieldId, length );
		return false;
	}
	if ( message.GetRemainingReadBits() < static_cast<int>( length ) * 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId, length );
		return false;
	}
	memset( token, 0, maximum + 1 );
	if ( message.ReadData( token, length ) != length ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId, length );
		return false;
	}
	token[ length ] = '\0';
	if ( !IsMachineToken( token, length, maximum ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING, fieldId, length );
		return false;
	}
	return true;
}

static bool FinishFieldRead( idBitMsg &message, unsigned char fieldId,
	mpMatchViewError_t *error ) {
	if ( message.GetRemainingReadBits() != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRAILING_DATA, fieldId,
			static_cast<unsigned int>( message.GetRemainingReadBits() ) );
		return false;
	}
	return true;
}

static void BeginFieldWrite( idBitMsg &message, byte *storage, int capacity ) {
	message.Init( storage, capacity );
	message.SetAllowOverflow( true );
	message.BeginWriting();
}

static bool AppendField( idBitMsg &payload, unsigned char fieldId,
	const idBitMsg &field, mpMatchViewError_t *error ) {
	if ( field.IsOverflowed() || field.GetSize() > 65535 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE, fieldId,
			static_cast<unsigned int>( field.GetSize() ) );
		return false;
	}
	payload.WriteByte( fieldId );
	payload.WriteUShort( field.GetSize() );
	if ( field.GetSize() > 0 ) {
		payload.WriteData( field.GetData(), field.GetSize() );
	}
	if ( payload.IsOverflowed() ) {
		SetError( error, MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE, fieldId,
			static_cast<unsigned int>( payload.GetSize() ) );
		return false;
	}
	return true;
}

static bool SameToken( const char *left, int leftLength,
	const char *right, int rightLength ) {
	return leftLength == rightLength && leftLength > 0 &&
		memcmp( left, right, leftLength ) == 0;
}

static bool SameProposal( const mpMatchViewProposalSummary_t &left,
	const mpMatchViewProposalSummary_t &right ) {
	return left.present == right.present && left.proposalId == right.proposalId &&
		left.opcode == right.opcode && left.scope == right.scope && left.side == right.side &&
		left.callerParticipantId == right.callerParticipantId &&
		left.yesCount == right.yesCount && left.noCount == right.noCount &&
		left.abstainCount == right.abstainCount && left.castCount == right.castCount &&
		left.eligibleCount == right.eligibleCount &&
		left.requiredQuorumCount == right.requiredQuorumCount &&
		left.requiredYesCount == right.requiredYesCount &&
		left.expiresAtEngineMsec == right.expiresAtEngineMsec &&
		left.recipientEligible == right.recipientEligible &&
		left.recipientBallot == right.recipientBallot;
}

static bool SameStagedRules( const mpMatchViewStagedRules_t &left,
	const mpMatchViewStagedRules_t &right ) {
	if ( left.present != right.present || left.revision != right.revision ||
		left.digest != right.digest || left.profileId != right.profileId ||
		left.customized != right.customized ||
		left.changedFieldMask != right.changedFieldMask ||
		left.valueCount != right.valueCount ) {
		return false;
	}
	for ( int i = 0; i < left.valueCount; ++i ) {
		if ( left.values[ i ].fieldId != right.values[ i ].fieldId ||
			left.values[ i ].type != right.values[ i ].type ||
			left.values[ i ].value != right.values[ i ].value ) {
			return false;
		}
	}
	return true;
}

static bool SameRosterSeat( const mpMatchViewRosterSeat_t &left,
	const mpMatchViewRosterSeat_t &right ) {
	return left.seatIndex == right.seatIndex && left.side == right.side &&
		left.role == right.role && left.required == right.required &&
		left.occupied == right.occupied && left.participantId == right.participantId &&
		left.connected == right.connected && left.ready == right.ready &&
		left.active == right.active;
}

static bool SameInvitation( const mpMatchViewInvitationSummary_t &left,
	const mpMatchViewInvitationSummary_t &right ) {
	return left.invitationId == right.invitationId && left.side == right.side &&
		left.role == right.role &&
		left.inviterParticipantId == right.inviterParticipantId &&
		left.inviteeParticipantId == right.inviteeParticipantId &&
		left.expiresAtEngineMsec == right.expiresAtEngineMsec;
}

static bool SameQueueEntry( const mpMatchViewQueueEntry_t &left,
	const mpMatchViewQueueEntry_t &right ) {
	return left.participantId == right.participantId && left.side == right.side &&
		left.position == right.position && left.state == right.state;
}

static bool ValidateLifecycle( const mpMatchViewLifecycle_t &lifecycle,
	mpMatchViewError_t *error ) {
	if ( lifecycle.phase < INACTIVE || lifecycle.phase >= STATE_COUNT ||
		lifecycle.round < RS_INACTIVE || lifecycle.round >= RS_STATE_COUNT ||
		lifecycle.pauseState < MP_MATCH_VIEW_PAUSE_RUNNING ||
		lifecycle.pauseState >= MP_MATCH_VIEW_PAUSE_STATE_COUNT ||
		lifecycle.pauseKind < MP_MATCH_VIEW_PAUSE_KIND_NONE ||
		lifecycle.pauseKind >= MP_MATCH_VIEW_PAUSE_KIND_COUNT ||
		lifecycle.pauseReason < MP_MATCH_VIEW_PAUSE_REASON_NONE ||
		lifecycle.pauseReason >= MP_MATCH_VIEW_PAUSE_REASON_COUNT ||
		lifecycle.resumePolicy < MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE ||
		lifecycle.resumePolicy >= MP_MATCH_VIEW_RESUME_POLICY_COUNT ||
		( lifecycle.resumeRequiredSideMask & ~MP_MATCH_VIEW_KNOWN_SIDE_MASK ) != 0 ||
		( lifecycle.resumeConsentingSideMask & ~lifecycle.resumeRequiredSideMask ) != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_ENUM, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	if ( !IsTime( lifecycle.pauseExpiryEngineMsec ) ||
		!IsTime( lifecycle.resumeDeadlineEngineMsec ) ||
		lifecycle.hasPauseExpiry != ( lifecycle.pauseExpiryEngineMsec != 0 ) ||
		lifecycle.hasResumeDeadline != ( lifecycle.resumeDeadlineEngineMsec != 0 ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	if ( lifecycle.pauseState == MP_MATCH_VIEW_PAUSE_RUNNING ) {
		if ( lifecycle.pauseKind != MP_MATCH_VIEW_PAUSE_KIND_NONE ||
			lifecycle.pauseReason != MP_MATCH_VIEW_PAUSE_REASON_NONE ||
			lifecycle.pauseOwnerSide != MP_MATCH_VIEW_SIDE_NONE ||
			lifecycle.hasPauseExpiry || lifecycle.hasResumeDeadline ||
			lifecycle.resumeRequiredSideMask != 0 ||
			lifecycle.resumeConsentingSideMask != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
			return false;
		}
		return true;
	}
	if ( lifecycle.pauseKind == MP_MATCH_VIEW_PAUSE_KIND_NONE ||
		lifecycle.pauseReason == MP_MATCH_VIEW_PAUSE_REASON_NONE ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	if ( lifecycle.pauseKind == MP_MATCH_VIEW_PAUSE_KIND_TEAM_TIMEOUT ) {
		if ( !IsSide( lifecycle.pauseOwnerSide ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
			return false;
		}
	} else if ( lifecycle.pauseOwnerSide != MP_MATCH_VIEW_SIDE_NONE ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	if ( lifecycle.pauseState == MP_MATCH_VIEW_RESUME_COUNTDOWN ?
		!lifecycle.hasResumeDeadline : lifecycle.hasResumeDeadline ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	if ( lifecycle.resumePolicy == MP_MATCH_VIEW_RESUME_REFEREE_ONLY &&
		lifecycle.resumeRequiredSideMask != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	unsigned char expectedRequiredMask = 0;
	if ( lifecycle.pauseState != MP_MATCH_VIEW_PAUSE_PENDING ) {
		if ( lifecycle.resumePolicy == MP_MATCH_VIEW_RESUME_BOTH_SIDES_OR_REFEREE ) {
			expectedRequiredMask = MP_MATCH_VIEW_KNOWN_SIDE_MASK;
		} else if ( lifecycle.resumePolicy == MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE &&
			lifecycle.pauseKind == MP_MATCH_VIEW_PAUSE_KIND_TEAM_TIMEOUT ) {
			expectedRequiredMask =
				static_cast<unsigned char>( 1u << lifecycle.pauseOwnerSide );
		}
	}
	if ( lifecycle.resumeRequiredSideMask != expectedRequiredMask ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_LIFECYCLE );
		return false;
	}
	return true;
}

static bool ValidateProposal( const mpMatchViewProposalSummary_t &proposal,
	unsigned char fieldId, mpMatchViewError_t *error ) {
	if ( !proposal.present ) {
		if ( proposal.proposalId != 0 || proposal.opcode != MP_MATCH_OP_INVALID ||
			proposal.scope != MP_MATCH_VIEW_PROPOSAL_GLOBAL ||
			proposal.side != MP_MATCH_VIEW_SIDE_NONE ||
			proposal.callerParticipantId != MP_MATCH_INVALID_PARTICIPANT_ID ||
			proposal.yesCount != 0 || proposal.noCount != 0 ||
			proposal.abstainCount != 0 || proposal.castCount != 0 ||
			proposal.eligibleCount != 0 || proposal.requiredQuorumCount != 0 ||
			proposal.requiredYesCount != 0 || proposal.expiresAtEngineMsec != 0 ||
			proposal.recipientEligible || proposal.recipientBallot != MP_MATCH_VIEW_BALLOT_NONE ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, fieldId );
			return false;
		}
		return true;
	}
	if ( proposal.proposalId == 0 || proposal.proposalId > MP_MATCH_VIEW_MAX_OPERATION_ID ||
		!IsValidOpcode( proposal.opcode ) ||
		proposal.scope < MP_MATCH_VIEW_PROPOSAL_GLOBAL ||
		proposal.scope >= MP_MATCH_VIEW_PROPOSAL_SCOPE_COUNT ||
		( proposal.scope == MP_MATCH_VIEW_PROPOSAL_GLOBAL ?
			proposal.side != MP_MATCH_VIEW_SIDE_NONE : !IsSide( proposal.side ) ) ||
		proposal.callerParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		proposal.eligibleCount == 0 || proposal.eligibleCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		proposal.yesCount > proposal.eligibleCount || proposal.noCount > proposal.eligibleCount ||
		proposal.abstainCount > proposal.eligibleCount ||
		proposal.castCount != proposal.yesCount + proposal.noCount + proposal.abstainCount ||
		proposal.castCount > proposal.eligibleCount ||
		proposal.requiredQuorumCount == 0 ||
		proposal.requiredQuorumCount > proposal.eligibleCount ||
		proposal.requiredYesCount == 0 ||
		proposal.requiredYesCount > proposal.eligibleCount ||
		proposal.expiresAtEngineMsec == 0 || !IsTime( proposal.expiresAtEngineMsec ) ||
		proposal.recipientBallot < MP_MATCH_VIEW_BALLOT_NONE ||
		proposal.recipientBallot >= MP_MATCH_VIEW_BALLOT_COUNT ||
		( !proposal.recipientEligible && proposal.recipientBallot != MP_MATCH_VIEW_BALLOT_NONE ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, fieldId );
		return false;
	}
	return true;
}

static bool ValidateRuleScalar( mpMatchViewRuleType_t type, int value ) {
	return type >= MP_MATCH_VIEW_RULE_BOOL && type < MP_MATCH_VIEW_RULE_TYPE_COUNT &&
		( type != MP_MATCH_VIEW_RULE_BOOL || value == 0 || value == 1 );
}

static bool ValidateCommittedRules( const mpMatchViewCommittedRules_t &rules,
	mpMatchViewError_t *error ) {
	if ( !rules.present ) {
		if ( rules.rulesSchemaVersion != 0 || rules.revision != 0 || rules.digest != 0 ||
			rules.profileId != 0 || rules.customized ||
			rules.boundary != MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT || rules.valueCount != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RULES );
			return false;
		}
		return true;
	}
	if ( rules.rulesSchemaVersion == 0 || rules.revision == 0 || rules.profileId < -1 ||
		rules.boundary < MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT ||
		rules.boundary >= MP_MATCH_VIEW_RULES_BOUNDARY_COUNT || rules.valueCount == 0 ||
		rules.valueCount > MP_MATCH_VIEW_MAX_RULE_FIELDS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RULES );
		return false;
	}
	for ( int i = 0; i < rules.valueCount; ++i ) {
		const mpMatchViewRuleValue_t &value = rules.values[ i ];
		if ( value.fieldId >= MP_MATCH_VIEW_MAX_RULE_FIELDS ||
			!ValidateRuleScalar( value.type, value.value ) ||
			( i > 0 && rules.values[ i - 1 ].fieldId >= value.fieldId ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_RULES, i );
			return false;
		}
	}
	return true;
}

static int FindCommittedRule( const mpMatchViewCommittedRules_t &committed,
	unsigned char fieldId ) {
	for ( int i = 0; i < committed.valueCount; ++i ) {
		if ( committed.values[ i ].fieldId == fieldId ) {
			return i;
		}
	}
	return -1;
}

static bool ValidateStagedRules( const mpMatchViewStagedRules_t &staged,
	const mpMatchViewCommittedRules_t &committed, mpMatchViewError_t *error ) {
	if ( !staged.present ) {
		if ( staged.revision != 0 || staged.digest != 0 || staged.profileId != 0 ||
			staged.customized || staged.changedFieldMask != 0 || staged.valueCount != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RULES );
			return false;
		}
		return true;
	}
	if ( !committed.present || staged.revision <= committed.revision || staged.profileId < -1 ||
		staged.changedFieldMask == 0 || staged.valueCount == 0 ||
		staged.valueCount > MP_MATCH_VIEW_MAX_RULE_FIELDS ||
		CountBits64( staged.changedFieldMask ) != staged.valueCount ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RULES );
		return false;
	}
	for ( int i = 0; i < staged.valueCount; ++i ) {
		const mpMatchViewStagedRuleValue_t &value = staged.values[ i ];
		const int committedIndex = FindCommittedRule( committed, value.fieldId );
		if ( value.fieldId >= MP_MATCH_VIEW_MAX_RULE_FIELDS ||
			( staged.changedFieldMask & ( 1ull << value.fieldId ) ) == 0 ||
			committedIndex < 0 || committed.values[ committedIndex ].type != value.type ||
			!committed.values[ committedIndex ].editable ||
			!ValidateRuleScalar( value.type, value.value ) ||
			committed.values[ committedIndex ].value == value.value ||
			( i > 0 && staged.values[ i - 1 ].fieldId >= value.fieldId ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_RULES, i );
			return false;
		}
	}
	return true;
}

static bool ValidateOperationAvailability( const mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	if ( state.operationAvailabilityCount != MP_MATCH_OP_COUNT - 1 ||
		state.operationAvailabilityCount > MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES ||
		( state.allowedOperations & ~MPMatchViewAllOperationBits() ) != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, state.operationAvailabilityCount );
		return false;
	}
	mpMatchViewAllowedOperationMask_t reconstructed = 0;
	for ( int i = 0; i < state.operationAvailabilityCount; ++i ) {
		const mpMatchViewOperationAvailability_t &availability =
			state.operationAvailability[ i ];
		if ( availability.opcode != static_cast<mpMatchOperationOpcode_t>( i + 1 ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, i );
			return false;
		}
		if ( availability.available ) {
			if ( availability.reason != MP_MATCH_PROTOCOL_REASON_OK ||
				availability.localizationId !=
					MPMatchProtocolReasonLocalizationId( MP_MATCH_PROTOCOL_REASON_OK ) ||
				availability.fieldId != 0 || availability.detail != 0 ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
					MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, i );
				return false;
			}
			reconstructed |= MPMatchViewOperationBit( availability.opcode );
		} else if ( availability.reason <= MP_MATCH_PROTOCOL_REASON_OK ||
			availability.reason >= MP_MATCH_PROTOCOL_REASON_COUNT ||
			availability.localizationId !=
				MPMatchProtocolReasonLocalizationId( availability.reason ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, i );
			return false;
		}
	}
	if ( reconstructed != state.allowedOperations ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
			MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY );
		return false;
	}
	return true;
}

static bool ValidateSeriesMap( const mpMatchViewSeriesMap_t &map, int index,
	int bestOf, mpMatchViewError_t *error ) {
	if ( map.poolIndex != index || map.disposition < MP_MATCH_VIEW_MAP_AVAILABLE ||
		map.disposition >= MP_MATCH_VIEW_MAP_DISPOSITION_COUNT ||
		!IsMapToken( map.mapToken, map.tokenLength ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES, index );
		return false;
	}
	if ( map.disposition != MP_MATCH_VIEW_MAP_SELECTED ) {
		if ( map.selectedBySide != MP_MATCH_VIEW_SIDE_NONE || map.selectionNumber != 0 ||
			map.decider || map.hasStartingGameSide ||
			map.startingGameSide != MP_MATCH_VIEW_SIDE_NONE ||
			map.gameSideChosenBy != MP_MATCH_VIEW_SIDE_NONE ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_SERIES, index );
			return false;
		}
		return true;
	}
	if ( map.selectionNumber == 0 || map.selectionNumber > bestOf ||
		( map.decider ? map.selectedBySide != MP_MATCH_VIEW_SIDE_NONE :
			!IsSide( map.selectedBySide ) ) ||
		( map.hasStartingGameSide ? !IsSide( map.startingGameSide ) :
			map.startingGameSide != MP_MATCH_VIEW_SIDE_NONE ) ||
		( map.hasStartingGameSide ? !IsSide( map.gameSideChosenBy ) :
			map.gameSideChosenBy != MP_MATCH_VIEW_SIDE_NONE ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES, index );
		return false;
	}
	return true;
}

static bool ValidateSeries( const mpMatchViewSeriesSummary_t &series,
	mpMatchViewError_t *error ) {
	if ( !series.present ) {
		if ( series.seriesId != 0 || series.state != MP_MATCH_VIEW_SERIES_DISABLED ||
			series.revision != 0 ||
			series.gameType != 0 || series.bestOf != 0 || series.currentMapNumber != 0 ||
			series.wins[ 0 ] != 0 || series.wins[ 1 ] != 0 || series.hasNextMap ||
			series.nextMapLength != 0 || !IsAllZero( series.nextMap, sizeof( series.nextMap ) ) ||
			series.currentVetoStep != 0 || series.vetoStepCount != 0 || series.hasVetoTurn ||
			series.vetoTurnAction != MP_MATCH_VIEW_VETO_BAN ||
			series.vetoTurnSide != MP_MATCH_VIEW_SIDE_NONE || series.mapPoolCount != 0 ||
			series.vetoHistoryCount != 0 || series.mapHistoryCount != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES );
			return false;
		}
		return true;
	}
	const int neededWins = series.bestOf / 2 + 1;
	if ( series.seriesId == 0 || series.state <= MP_MATCH_VIEW_SERIES_DISABLED ||
		series.state >= MP_MATCH_VIEW_SERIES_STATE_COUNT || series.revision == 0 ||
		series.gameType < 0 || series.bestOf < 1 || series.bestOf > 15 ||
		( series.bestOf & 1 ) == 0 || series.currentMapNumber > series.bestOf ||
		series.wins[ 0 ] > neededWins || series.wins[ 1 ] > neededWins ||
		( series.wins[ 0 ] >= neededWins && series.wins[ 1 ] >= neededWins ) ||
		series.currentVetoStep > series.vetoStepCount ||
		series.vetoStepCount > MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY ||
		series.mapPoolCount == 0 || series.mapPoolCount > MP_MATCH_VIEW_MAX_SERIES_MAP_POOL ||
		series.vetoHistoryCount > MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY ||
		series.mapHistoryCount > MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY ||
		series.vetoHistoryCount != series.currentVetoStep ||
		( series.hasNextMap ? !IsMapToken( series.nextMap, series.nextMapLength ) :
			series.nextMapLength != 0 || !IsAllZero( series.nextMap, sizeof( series.nextMap ) ) ) ||
		( series.hasVetoTurn ?
			( series.state != MP_MATCH_VIEW_SERIES_VETO ||
				series.currentVetoStep >= series.vetoStepCount ||
				series.vetoTurnAction < MP_MATCH_VIEW_VETO_BAN ||
				series.vetoTurnAction >= MP_MATCH_VIEW_VETO_ACTION_COUNT ||
				!IsSide( series.vetoTurnSide ) ) :
			( series.vetoTurnAction != MP_MATCH_VIEW_VETO_BAN ||
				series.vetoTurnSide != MP_MATCH_VIEW_SIDE_NONE ) ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES );
		return false;
	}
	for ( int i = 0; i < series.mapPoolCount; ++i ) {
		if ( !ValidateSeriesMap( series.mapPool[ i ], i, series.bestOf, error ) ) {
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( SameToken( series.mapPool[ prior ].mapToken, series.mapPool[ prior ].tokenLength,
				series.mapPool[ i ].mapToken, series.mapPool[ i ].tokenLength ) ||
				( series.mapPool[ i ].selectionNumber != 0 &&
					series.mapPool[ prior ].selectionNumber == series.mapPool[ i ].selectionNumber ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_SERIES, i );
				return false;
			}
		}
	}
	if ( series.hasNextMap ) {
		bool found = false;
		for ( int i = 0; i < series.mapPoolCount; ++i ) {
			if ( series.mapPool[ i ].disposition == MP_MATCH_VIEW_MAP_SELECTED &&
				SameToken( series.nextMap, series.nextMapLength,
					series.mapPool[ i ].mapToken, series.mapPool[ i ].tokenLength ) ) {
				found = true;
				break;
			}
		}
		if ( !found ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES );
			return false;
		}
	}
	for ( int i = 0; i < series.vetoHistoryCount; ++i ) {
		const mpMatchViewVetoHistory_t &entry = series.vetoHistory[ i ];
		if ( entry.sequenceNumber != i + 1 ||
			entry.action < MP_MATCH_VIEW_VETO_BAN ||
			entry.action >= MP_MATCH_VIEW_VETO_ACTION_COUNT || !IsSide( entry.actingSide ) ||
			entry.mapPoolIndex >= series.mapPoolCount ||
			( entry.hasSelectedGameSide ?
				( entry.action != MP_MATCH_VIEW_VETO_SIDE || !IsSide( entry.selectedGameSide ) ) :
				entry.selectedGameSide != MP_MATCH_VIEW_SIDE_NONE ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_SERIES, i );
			return false;
		}
	}
	int countedWins[ MP_MATCH_VIEW_SIDE_COUNT ] = { 0, 0 };
	for ( int i = 0; i < series.mapHistoryCount; ++i ) {
		const mpMatchViewSeriesMapHistory_t &entry = series.mapHistory[ i ];
		if ( entry.attemptNumber != i + 1 || entry.mapPoolIndex >= series.mapPoolCount ||
			series.mapPool[ entry.mapPoolIndex ].disposition != MP_MATCH_VIEW_MAP_SELECTED ||
			entry.outcome <= MP_MATCH_VIEW_MAP_UNPLAYED ||
			entry.outcome >= MP_MATCH_VIEW_MAP_OUTCOME_COUNT ||
			( entry.outcome == MP_MATCH_VIEW_MAP_ABORTED ?
				entry.winnerSide != MP_MATCH_VIEW_SIDE_NONE : !IsSide( entry.winnerSide ) ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_SERIES, i );
			return false;
		}
		if ( IsSide( entry.winnerSide ) ) {
			++countedWins[ entry.winnerSide ];
		}
	}
	if ( countedWins[ 0 ] != series.wins[ 0 ] || countedWins[ 1 ] != series.wins[ 1 ] ||
		( series.state == MP_MATCH_VIEW_SERIES_COMPLETE &&
			series.wins[ 0 ] < neededWins && series.wins[ 1 ] < neededWins ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_SERIES );
		return false;
	}
	return true;
}

static bool ValidateEvidence( const mpMatchViewEvidenceSummary_t &evidence,
	mpMatchViewError_t *error ) {
	if ( evidence.evidenceState < MP_MATCH_VIEW_EVIDENCE_DISABLED ||
		evidence.evidenceState >= MP_MATCH_VIEW_EVIDENCE_STATE_COUNT ||
		evidence.mvdState < MP_MATCH_VIEW_MVD_DISABLED ||
		evidence.mvdState >= MP_MATCH_VIEW_MVD_STATE_COUNT ||
		evidence.reportState < MP_MATCH_VIEW_REPORT_DISABLED ||
		evidence.reportState >= MP_MATCH_VIEW_REPORT_STATE_COUNT ||
		evidence.eventCount > MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS ||
		evidence.participantStatsCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		evidence.teamStatsCount > MP_MATCH_VIEW_SIDE_COUNT ||
		evidence.recentEventCount > MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ||
		evidence.recentEventCount != ( evidence.eventCount <
			MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ? evidence.eventCount :
			MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ) ||
		( evidence.droppedRecordCountSaturated &&
			evidence.droppedRecordCount != 0xffffffffu ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
			MP_MATCH_VIEW_FIELD_EVIDENCE );
		return false;
	}
	for ( int i = 0; i < evidence.recentEventCount; ++i ) {
		if ( evidence.recentEventKinds[ i ] <= MP_MATCH_VIEW_EVIDENCE_EVENT_NONE ||
			evidence.recentEventKinds[ i ] >=
				MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_ENUM,
				MP_MATCH_VIEW_FIELD_EVIDENCE, i );
			return false;
		}
	}
	for ( int i = evidence.recentEventCount;
		i < MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS; ++i ) {
		if ( evidence.recentEventKinds[ i ] != MP_MATCH_VIEW_EVIDENCE_EVENT_NONE ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_EVIDENCE, i );
			return false;
		}
	}
	if ( evidence.evidenceState == MP_MATCH_VIEW_EVIDENCE_DISABLED ) {
		if ( evidence.mvdState != MP_MATCH_VIEW_MVD_DISABLED ||
			evidence.reportState != MP_MATCH_VIEW_REPORT_DISABLED ||
			evidence.evidenceRevision != 0 || evidence.eventCount != 0 ||
			evidence.droppedRecordCount != 0 ||
			evidence.droppedRecordCountSaturated ||
			evidence.participantStatsCount != 0 || evidence.teamStatsCount != 0 ||
			evidence.resultRecorded || evidence.recentEventCount != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_EVIDENCE );
			return false;
		}
		return true;
	}
	if ( evidence.evidenceState == MP_MATCH_VIEW_EVIDENCE_FAILED ) {
		if ( evidence.evidenceRevision != 0 || evidence.eventCount != 0 ||
			evidence.droppedRecordCount != 0 ||
			evidence.droppedRecordCountSaturated ||
			evidence.participantStatsCount != 0 || evidence.teamStatsCount != 0 ||
			evidence.resultRecorded || evidence.recentEventCount != 0 ||
			( evidence.mvdState != MP_MATCH_VIEW_MVD_DISABLED &&
				evidence.mvdState != MP_MATCH_VIEW_MVD_FAILED ) ||
			( evidence.reportState != MP_MATCH_VIEW_REPORT_DISABLED &&
				evidence.reportState != MP_MATCH_VIEW_REPORT_FAILED ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_EVIDENCE );
			return false;
		}
		return true;
	}
	if ( evidence.evidenceRevision == 0 ||
		( evidence.resultRecorded && evidence.eventCount == 0 ) ||
		( evidence.reportState == MP_MATCH_VIEW_REPORT_AVAILABLE &&
			evidence.evidenceState != MP_MATCH_VIEW_EVIDENCE_FINALIZED ) ||
		( evidence.reportState == MP_MATCH_VIEW_REPORT_PENDING &&
			evidence.evidenceState == MP_MATCH_VIEW_EVIDENCE_FINALIZED ) ||
		( evidence.mvdState == MP_MATCH_VIEW_MVD_AVAILABLE &&
			evidence.evidenceState != MP_MATCH_VIEW_EVIDENCE_FINALIZED ) ||
		( evidence.mvdState == MP_MATCH_VIEW_MVD_PENDING &&
			evidence.evidenceState == MP_MATCH_VIEW_EVIDENCE_FINALIZED ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
			MP_MATCH_VIEW_FIELD_EVIDENCE );
		return false;
	}
	return true;
}

static bool ValidateDenial( const mpMatchViewDenial_t &denial,
	mpMatchViewError_t *error ) {
	if ( !denial.present ) {
		if ( denial.opcode != MP_MATCH_OP_INVALID ||
			denial.reason != MP_MATCH_PROTOCOL_REASON_NONE ||
			denial.localizationId != MP_MATCH_LOCALIZATION_NONE ||
			denial.fieldId != 0 || denial.detail != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_DENIAL );
			return false;
		}
		return true;
	}
	if ( !IsValidOpcode( denial.opcode ) || denial.reason <= MP_MATCH_PROTOCOL_REASON_OK ||
		denial.reason >= MP_MATCH_PROTOCOL_REASON_COUNT ||
		denial.localizationId != MPMatchProtocolReasonLocalizationId( denial.reason ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_DENIAL );
		return false;
	}
	return true;
}

static bool ValidateRecipient( const mpMatchViewRecipient_t &recipient,
	mpMatchViewError_t *error ) {
	if ( recipient.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		recipient.slot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ||
		recipient.bindingGeneration == 0 ||
		( recipient.side != MP_MATCH_VIEW_SIDE_NONE && !IsSide( recipient.side ) ) ||
		( recipient.competitionSide != MP_MATCH_VIEW_SIDE_NONE &&
			!IsSide( recipient.competitionSide ) ) ||
		( recipient.publicRoleMask & ~AllPublicRoleBits() ) != 0 ||
		recipient.queueState < MP_MATCH_VIEW_QUEUE_NONE ||
		recipient.queueState >= MP_MATCH_VIEW_QUEUE_STATE_COUNT ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
		return false;
	}
	if ( recipient.queueState == MP_MATCH_VIEW_QUEUE_NONE ) {
		if ( recipient.queueSide != MP_MATCH_VIEW_SIDE_NONE ||
			recipient.hasQueuePosition || recipient.queuePosition != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
			return false;
		}
	} else if ( recipient.queueState == MP_MATCH_VIEW_QUEUE_ADMITTED ) {
		if ( ( recipient.queueSide != MP_MATCH_VIEW_SIDE_NONE &&
			!IsSide( recipient.queueSide ) ) || recipient.hasQueuePosition ||
			recipient.queuePosition != 0 || !recipient.active ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
			return false;
		}
	} else if ( ( recipient.queueSide != MP_MATCH_VIEW_SIDE_NONE &&
		!IsSide( recipient.queueSide ) ) || !recipient.hasQueuePosition ||
		recipient.queuePosition == 0 ||
		recipient.queuePosition > MP_MATCH_VIEW_MAX_QUEUE_ENTRIES || recipient.active ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
		return false;
	}
	if ( recipient.ready && !recipient.readyEligible ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
		return false;
	}
	return true;
}

static bool ValidatePublicState( const mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	if ( state.schemaVersion != MP_MATCH_VIEW_SCHEMA_VERSION ) {
		SetError( error, MP_MATCH_VIEW_ERROR_UNSUPPORTED_SCHEMA, MP_MATCH_VIEW_FIELD_SCHEMA,
			state.schemaVersion );
		return false;
	}
	if ( state.sessionId == 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_SESSION );
		return false;
	}
	if ( state.sessionRevision == 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_REVISION, MP_MATCH_VIEW_FIELD_REVISION );
		return false;
	}
	if ( state.controlRevision == 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_REVISION,
			MP_MATCH_VIEW_FIELD_CONTROL_REVISION );
		return false;
	}
	if ( state.viewRevision == 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_REVISION,
			MP_MATCH_VIEW_FIELD_VIEW_REVISION );
		return false;
	}
	if ( !ValidateLifecycle( state.lifecycle, error ) ) {
		return false;
	}
	if ( !IsTime( state.clocks.engineTimeMsec ) || !IsTime( state.clocks.matchTimeMsec ) ||
		!IsTime( state.clocks.liveDeadlineMatchMsec ) ||
		state.clocks.isOvertime != ( state.clocks.livePeriod != 0 ) ||
		state.clocks.hasLiveDeadline != ( state.clocks.liveDeadlineMatchMsec != 0 ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_CLOCKS );
		return false;
	}
	if ( state.readiness.readyCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		state.readiness.eligibleCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		state.readiness.activeHumans > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		state.readiness.vacantRequiredSeats > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		state.readiness.readyCount > state.readiness.eligibleCount ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT, MP_MATCH_VIEW_FIELD_READINESS );
		return false;
	}
	for ( int side = 0; side < MP_MATCH_VIEW_SIDE_COUNT; ++side ) {
		const mpMatchViewTimeoutBudget_t &budget = state.timeoutBudgets[ side ];
		if ( budget.configured > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
			budget.remaining > budget.configured || budget.consumed > budget.configured ||
			static_cast<int>( budget.remaining ) + budget.consumed != budget.configured ||
			( budget.configured == 0 ) != ( budget.durationSeconds == 0 ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_TIMEOUTS, side );
			return false;
		}
	}
	if ( state.roleSummaryCount > MP_MATCH_VIEW_MAX_ROLE_SUMMARIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT, MP_MATCH_VIEW_FIELD_ROLES,
			state.roleSummaryCount );
		return false;
	}
	for ( int i = 0; i < state.roleSummaryCount; ++i ) {
		const mpMatchViewRoleSummary_t &summary = state.roleSummaries[ i ];
		if ( summary.role <= MP_MATCH_VIEW_ROLE_NONE || summary.role >= MP_MATCH_VIEW_ROLE_COUNT ||
			summary.count == 0 || summary.count > MP_MATCH_VIEW_MAX_PARTICIPANTS ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_ROLES, i );
			return false;
		}
		const bool sideRole = summary.role == MP_MATCH_VIEW_ROLE_PLAYER ||
			summary.role == MP_MATCH_VIEW_ROLE_CAPTAIN ||
			summary.role == MP_MATCH_VIEW_ROLE_COACH;
		if ( sideRole ?
			( summary.side != MP_MATCH_VIEW_SIDE_NONE && !IsSide( summary.side ) ) :
			summary.side != MP_MATCH_VIEW_SIDE_NONE ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_ROLES, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( state.roleSummaries[ prior ].role == summary.role &&
				state.roleSummaries[ prior ].side == summary.side ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_ROLES, i );
				return false;
			}
		}
	}
	if ( state.rosterSummaryCount > MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT, MP_MATCH_VIEW_FIELD_TEAMS,
			state.rosterSummaryCount );
		return false;
	}
	for ( int i = 0; i < state.rosterSummaryCount; ++i ) {
		const mpMatchViewRosterSummary_t &summary = state.rosterSummaries[ i ];
		if ( !IsSide( summary.side ) || summary.declaredSeats > MP_MATCH_VIEW_MAX_ROSTER_SEATS ||
			summary.occupiedSeats > summary.declaredSeats ||
			summary.connectedOccupants > summary.occupiedSeats ||
			summary.readyOccupants > summary.connectedOccupants ||
			summary.activeParticipants > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
			summary.queueDepth > MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_TEAMS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( state.rosterSummaries[ prior ].side == summary.side ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_TEAMS, i );
				return false;
			}
		}
	}
	if ( state.participantSummaryCount == 0 ||
		state.participantSummaryCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_PARTICIPANTS, state.participantSummaryCount );
		return false;
	}
	bool foundRecipient = false;
	for ( int i = 0; i < state.participantSummaryCount; ++i ) {
		const mpMatchViewParticipantSummary_t &participant = state.participantSummaries[ i ];
		if ( participant.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
			( participant.side != MP_MATCH_VIEW_SIDE_NONE && !IsSide( participant.side ) ) ||
			( participant.publicRoleMask & ~AllPublicRoleBits() ) != 0 ||
			( participant.connected ? participant.slot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS :
				participant.slot != 0xffu ) || ( participant.active && !participant.connected ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, i );
			return false;
		}
		const mpMatchViewPublicRoleMask_t sidedAuthorityMask =
			MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_CAPTAIN ) |
			MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_COACH );
		if ( participant.side == MP_MATCH_VIEW_SIDE_NONE &&
			( participant.publicRoleMask & sidedAuthorityMask ) != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( state.participantSummaries[ prior ].participantId == participant.participantId ||
				( participant.connected && state.participantSummaries[ prior ].connected &&
					state.participantSummaries[ prior ].slot == participant.slot ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_PARTICIPANTS, i );
				return false;
			}
		}
		if ( participant.participantId == state.recipient.participantId ) {
			foundRecipient = participant.connected && participant.slot == state.recipient.slot &&
				participant.side == state.recipient.side &&
				participant.publicRoleMask == state.recipient.publicRoleMask &&
				participant.human &&
				participant.active == state.recipient.active;
		}
	}
	if ( !ValidateProposal( state.globalProposal, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		( state.globalProposal.present &&
			( state.globalProposal.scope != MP_MATCH_VIEW_PROPOSAL_GLOBAL ||
				state.globalProposal.expiresAtEngineMsec <= state.clocks.engineTimeMsec ) ) ||
		!ValidateSeries( state.series, error ) ||
		!ValidateEvidence( state.evidence, error ) ||
		!ValidateTerminalResult( state, error ) ||
		!ValidateOperationAvailability( state, error ) ||
		!ValidateDenial( state.denial, error ) ||
		!ValidateRecipient( state.recipient, error ) ||
		!ValidateCommittedRules( state.committedRules, error ) ) {
		return false;
	}
	if ( !foundRecipient ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
		return false;
	}
	const unsigned char ownSideBit = IsSide( state.recipient.side ) ?
		static_cast<unsigned char>( 1u << state.recipient.side ) : 0;
	if ( state.recipient.resumeConsented !=
		( ownSideBit != 0 &&
			( state.lifecycle.resumeConsentingSideMask & ownSideBit ) != 0 ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_RECIPIENT );
		return false;
	}
	return true;
}

static bool ValidateAuthorizationTag( const mpMatchViewAuthorizationTag_t &tag,
	int index, mpMatchViewError_t *error ) {
	if ( tag.audience < MP_MATCH_VIEW_AUDIENCE_PUBLIC ||
		tag.audience >= MP_MATCH_VIEW_AUDIENCE_COUNT ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_ENUM, 0, index );
		return false;
	}
	switch ( tag.audience ) {
		case MP_MATCH_VIEW_AUDIENCE_OWN_SIDE:
			if ( !IsSide( tag.audienceSide ) ||
				tag.audienceParticipantId != MP_MATCH_INVALID_PARTICIPANT_ID ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
				return false;
			}
			break;
		case MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0:
		case MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1:
			if ( tag.audienceSide != SpectatorAudienceSide( tag.audience ) ||
				tag.audienceParticipantId != MP_MATCH_INVALID_PARTICIPANT_ID ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
				return false;
			}
			break;
		case MP_MATCH_VIEW_AUDIENCE_RECIPIENT:
			if ( tag.audienceSide != MP_MATCH_VIEW_SIDE_NONE ||
				tag.audienceParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
				return false;
			}
			break;
		default:
			if ( tag.audienceSide != MP_MATCH_VIEW_SIDE_NONE ||
				tag.audienceParticipantId != MP_MATCH_INVALID_PARTICIPANT_ID ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
				return false;
			}
			break;
	}
	return true;
}

static bool CandidateAuthorized( const mpMatchViewAuthorizationTag_t &tag,
	const mpMatchViewRecipientPolicy_t &policy ) {
	if ( ( policy.audiences & MPMatchViewAudienceBit( tag.audience ) ) == 0 ) {
		return false;
	}
	if ( tag.audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ) {
		return tag.audienceSide == policy.ownSide;
	}
	if ( tag.audience == MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) {
		return tag.audienceParticipantId == policy.recipientId;
	}
	return true;
}

static bool ValidateRosterSeat( const mpMatchViewRosterSeat_t &seat,
	int index, mpMatchViewError_t *error ) {
	if ( seat.seatIndex >= MP_MATCH_VIEW_MAX_ROSTER_SEATS || !IsSide( seat.side ) ||
		seat.role < MP_MATCH_VIEW_ROSTER_PLAYER ||
		seat.role >= MP_MATCH_VIEW_ROSTER_ROLE_COUNT ||
		( seat.occupied ? seat.participantId == MP_MATCH_INVALID_PARTICIPANT_ID :
			seat.participantId != MP_MATCH_INVALID_PARTICIPANT_ID || seat.connected ||
			seat.ready || seat.active ) || ( seat.active && !seat.connected ) ||
		( seat.ready && !seat.connected ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
			MP_MATCH_VIEW_FIELD_ROSTER_SEATS, index );
		return false;
	}
	return true;
}

static bool ValidateInvitation( const mpMatchViewInvitationSummary_t &invitation,
	int index, mpMatchViewError_t *error ) {
	if ( invitation.invitationId == 0 ||
		invitation.invitationId > MP_MATCH_VIEW_MAX_OPERATION_ID ||
		!IsSide( invitation.side ) ||
		invitation.role < MP_MATCH_VIEW_ROSTER_PLAYER ||
		invitation.role >= MP_MATCH_VIEW_ROSTER_ROLE_COUNT ||
		invitation.inviterParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		invitation.inviteeParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		invitation.inviterParticipantId == invitation.inviteeParticipantId ||
		invitation.expiresAtEngineMsec == 0 || !IsTime( invitation.expiresAtEngineMsec ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
			MP_MATCH_VIEW_FIELD_INVITATIONS, index );
		return false;
	}
	return true;
}

static bool ValidateQueueEntry( const mpMatchViewQueueEntry_t &entry,
	int index, mpMatchViewError_t *error ) {
	if ( entry.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		( entry.side != MP_MATCH_VIEW_SIDE_NONE && !IsSide( entry.side ) ) ||
		entry.position == 0 || entry.position > MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ||
		( entry.state != MP_MATCH_VIEW_QUEUE_WAITING &&
			entry.state != MP_MATCH_VIEW_QUEUE_DEFERRED ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_QUEUES, index );
		return false;
	}
	return true;
}

static bool ValidateObserverCandidate( const mpMatchViewObserverCandidate_t &candidate,
	int index, mpMatchViewError_t *error ) {
	if ( !ValidateAuthorizationTag( candidate.authorization, index, error ) ||
		candidate.kind < MP_MATCH_VIEW_OBSERVER_TEAM_VITAL ||
		candidate.kind >= MP_MATCH_VIEW_OBSERVER_KIND_COUNT ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_ENUM, 0, index );
		return false;
	}
	switch ( candidate.kind ) {
		case MP_MATCH_VIEW_OBSERVER_TEAM_VITAL:
			if ( candidate.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
				!IsSide( candidate.participantSide ) || candidate.primaryValue > 999 ||
				candidate.secondaryValue > 999 || candidate.matchDeadlineMsec != 0 ||
				candidate.tokenLength != 0 ||
				!IsAllZero( candidate.token, sizeof( candidate.token ) ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, 0, index );
				return false;
			}
			break;
		case MP_MATCH_VIEW_OBSERVER_ITEM_TIMING:
			if ( candidate.participantId != MP_MATCH_INVALID_PARTICIPANT_ID ||
				candidate.participantSide != MP_MATCH_VIEW_SIDE_NONE ||
				candidate.primaryValue != 0 || candidate.secondaryValue != 0 ||
				!IsTime( candidate.matchDeadlineMsec ) ||
				!IsMachineToken( candidate.token, candidate.tokenLength,
					MP_MATCH_VIEW_ITEM_TOKEN_BYTES ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING, 0, index );
				return false;
			}
			break;
		case MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET:
			if ( candidate.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
				!IsSide( candidate.participantSide ) || candidate.primaryValue != 0 ||
				candidate.secondaryValue != 0 || candidate.matchDeadlineMsec != 0 ||
				candidate.tokenLength != 0 ||
				!IsAllZero( candidate.token, sizeof( candidate.token ) ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, 0, index );
				return false;
			}
			break;
		default:
			return false;
	}
	if ( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
		candidate.kind != MP_MATCH_VIEW_OBSERVER_ITEM_TIMING &&
		candidate.participantSide != candidate.authorization.audienceSide ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
		return false;
	}
	if ( IsSpectatorSideAudience( candidate.authorization.audience ) &&
		( candidate.kind != MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET ||
			candidate.participantSide != candidate.authorization.audienceSide ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY, 0, index );
		return false;
	}
	return true;
}

static bool AddRosterSeat( mpSessionView &view, const mpMatchViewRosterSeat_t &seat,
	int sourceIndex, mpMatchViewError_t *error ) {
	for ( int i = 0; i < view.rosterSeatCount; ++i ) {
		if ( view.rosterSeats[ i ].side == seat.side &&
			view.rosterSeats[ i ].seatIndex == seat.seatIndex ) {
			if ( SameRosterSeat( view.rosterSeats[ i ], seat ) ) {
				return true;
			}
			SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, sourceIndex );
			return false;
		}
	}
	if ( view.rosterSeatCount >= MP_MATCH_VIEW_MAX_ROSTER_SEATS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
			MP_MATCH_VIEW_FIELD_ROSTER_SEATS, sourceIndex );
		return false;
	}
	view.rosterSeats[ view.rosterSeatCount++ ] = seat;
	return true;
}

static bool AddInvitation( mpSessionView &view,
	const mpMatchViewInvitationSummary_t &invitation, int sourceIndex,
	mpMatchViewError_t *error ) {
	for ( int i = 0; i < view.invitationCount; ++i ) {
		if ( view.invitations[ i ].invitationId == invitation.invitationId ) {
			if ( SameInvitation( view.invitations[ i ], invitation ) ) {
				return true;
			}
			SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
				MP_MATCH_VIEW_FIELD_INVITATIONS, sourceIndex );
			return false;
		}
	}
	if ( view.invitationCount >= MP_MATCH_VIEW_MAX_INVITATIONS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
			MP_MATCH_VIEW_FIELD_INVITATIONS, sourceIndex );
		return false;
	}
	view.invitations[ view.invitationCount++ ] = invitation;
	return true;
}

static bool AddQueueEntry( mpSessionView &view, const mpMatchViewQueueEntry_t &entry,
	int sourceIndex, mpMatchViewError_t *error ) {
	for ( int i = 0; i < view.queueEntryCount; ++i ) {
		if ( view.queueEntries[ i ].participantId == entry.participantId ||
			( view.queueEntries[ i ].side == entry.side &&
				view.queueEntries[ i ].position == entry.position ) ) {
			if ( SameQueueEntry( view.queueEntries[ i ], entry ) ) {
				return true;
			}
			SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
				MP_MATCH_VIEW_FIELD_QUEUES, sourceIndex );
			return false;
		}
	}
	if ( view.queueEntryCount >= MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
			MP_MATCH_VIEW_FIELD_QUEUES, sourceIndex );
		return false;
	}
	view.queueEntries[ view.queueEntryCount++ ] = entry;
	return true;
}

static bool AddAuthorizedObserver( mpSessionView &view,
	const mpMatchViewObserverCandidate_t &candidate, int sourceIndex,
	mpMatchViewError_t *error ) {
	switch ( candidate.kind ) {
		case MP_MATCH_VIEW_OBSERVER_TEAM_VITAL: {
			for ( int i = 0; i < view.teamVitalCount; ++i ) {
				if ( view.teamVitals[ i ].participantId == candidate.participantId ) {
					const mpMatchViewTeamVital_t &existing = view.teamVitals[ i ];
					if ( existing.participantSide == candidate.participantSide &&
						existing.health == candidate.primaryValue &&
						existing.armor == candidate.secondaryValue &&
						existing.alive == candidate.active ) {
						return true;
					}
					SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
						MP_MATCH_VIEW_FIELD_TEAM_VITALS, sourceIndex );
					return false;
				}
			}
			if ( view.teamVitalCount >= MP_MATCH_VIEW_MAX_TEAM_VITALS ) {
				SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
					MP_MATCH_VIEW_FIELD_TEAM_VITALS, sourceIndex );
				return false;
			}
			mpMatchViewTeamVital_t &target = view.teamVitals[ view.teamVitalCount++ ];
			target.participantId = candidate.participantId;
			target.participantSide = candidate.participantSide;
			target.health = candidate.primaryValue;
			target.armor = candidate.secondaryValue;
			target.alive = candidate.active;
			return true;
		}
		case MP_MATCH_VIEW_OBSERVER_ITEM_TIMING: {
			for ( int i = 0; i < view.itemTimingCount; ++i ) {
				if ( SameToken( view.itemTimings[ i ].token, view.itemTimings[ i ].tokenLength,
					candidate.token, candidate.tokenLength ) ) {
					const mpMatchViewItemTiming_t &existing = view.itemTimings[ i ];
					if ( existing.available == candidate.active &&
						existing.matchDeadlineMsec == candidate.matchDeadlineMsec ) {
						return true;
					}
					SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
						MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, sourceIndex );
					return false;
				}
			}
			if ( view.itemTimingCount >= MP_MATCH_VIEW_MAX_ITEM_TIMINGS ) {
				SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
					MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, sourceIndex );
				return false;
			}
			mpMatchViewItemTiming_t &target = view.itemTimings[ view.itemTimingCount++ ];
			target.available = candidate.active;
			target.matchDeadlineMsec = candidate.matchDeadlineMsec;
			target.tokenLength = candidate.tokenLength;
			memcpy( target.token, candidate.token, candidate.tokenLength + 1 );
			return true;
		}
		case MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET: {
			for ( int i = 0; i < view.followTargetCount; ++i ) {
				if ( view.followTargets[ i ].participantId == candidate.participantId ) {
					const mpMatchViewFollowTarget_t &existing = view.followTargets[ i ];
					if ( existing.participantSide == candidate.participantSide &&
						existing.selectable == candidate.active ) {
						return true;
					}
					SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
						MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, sourceIndex );
					return false;
				}
			}
			if ( view.followTargetCount >= MP_MATCH_VIEW_MAX_FOLLOW_TARGETS ) {
				SetError( error, MP_MATCH_VIEW_ERROR_CAPACITY,
					MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, sourceIndex );
				return false;
			}
			mpMatchViewFollowTarget_t &target = view.followTargets[ view.followTargetCount++ ];
			target.participantId = candidate.participantId;
			target.participantSide = candidate.participantSide;
			target.selectable = candidate.active;
			return true;
		}
		default:
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_ENUM, 0, sourceIndex );
			return false;
	}
}

} // namespace

namespace {

static bool DecodeLifecycleField( idBitMsg &field, mpMatchViewLifecycle_t &value,
	mpMatchViewError_t *error ) {
	unsigned char phase = 0;
	unsigned char round = 0;
	unsigned char pauseState = 0;
	unsigned char pauseKind = 0;
	unsigned char pauseReason = 0;
	unsigned char resumePolicy = 0;
	if ( !ReadByteValue( field, phase, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, round, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, pauseState, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, pauseKind, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, pauseReason, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadSideValue( field, value.pauseOwnerSide, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadBoolValue( field, value.hasPauseExpiry, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadUInt64Value( field, value.pauseExpiryEngineMsec,
			MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadBoolValue( field, value.hasResumeDeadline,
			MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadUInt64Value( field, value.resumeDeadlineEngineMsec,
			MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, resumePolicy, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, value.resumeRequiredSideMask,
			MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!ReadByteValue( field, value.resumeConsentingSideMask,
			MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ||
		!FinishFieldRead( field, MP_MATCH_VIEW_FIELD_LIFECYCLE, error ) ) {
		return false;
	}
	value.phase = static_cast<mpGameState_t>( phase );
	value.round = static_cast<roundState_t>( round );
	value.pauseState = static_cast<mpMatchViewPauseState_t>( pauseState );
	value.pauseKind = static_cast<mpMatchViewPauseKind_t>( pauseKind );
	value.pauseReason = static_cast<mpMatchViewPauseReason_t>( pauseReason );
	value.resumePolicy = static_cast<mpMatchViewResumePolicy_t>( resumePolicy );
	return true;
}

static bool DecodeClocksField( idBitMsg &field, mpMatchViewClocks_t &value,
	mpMatchViewError_t *error ) {
	if ( !ReadUInt64Value( field, value.engineTimeMsec, MP_MATCH_VIEW_FIELD_CLOCKS, error ) ||
		!ReadUInt64Value( field, value.matchTimeMsec, MP_MATCH_VIEW_FIELD_CLOCKS, error ) ||
		!ReadUIntValue( field, value.livePeriod, MP_MATCH_VIEW_FIELD_CLOCKS, error ) ||
		!ReadBoolValue( field, value.isOvertime, MP_MATCH_VIEW_FIELD_CLOCKS, error ) ||
		!ReadBoolValue( field, value.hasLiveDeadline, MP_MATCH_VIEW_FIELD_CLOCKS, error ) ||
		!ReadUInt64Value( field, value.liveDeadlineMatchMsec,
			MP_MATCH_VIEW_FIELD_CLOCKS, error ) ) {
		return false;
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_CLOCKS, error );
}

static bool DecodeReadinessField( idBitMsg &field, mpMatchViewReadiness_t &value,
	mpMatchViewError_t *error ) {
	if ( !ReadUIntValue( field, value.blockers, MP_MATCH_VIEW_FIELD_READINESS, error ) ||
		!ReadUShortValue( field, value.readyCount, MP_MATCH_VIEW_FIELD_READINESS, error ) ||
		!ReadUShortValue( field, value.eligibleCount, MP_MATCH_VIEW_FIELD_READINESS, error ) ||
		!ReadUShortValue( field, value.activeHumans, MP_MATCH_VIEW_FIELD_READINESS, error ) ||
		!ReadUShortValue( field, value.vacantRequiredSeats,
			MP_MATCH_VIEW_FIELD_READINESS, error ) ) {
		return false;
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_READINESS, error );
}

static bool DecodeTimeoutsField( idBitMsg &field,
	mpMatchViewTimeoutBudget_t *budgets, mpMatchViewError_t *error ) {
	for ( int side = 0; side < MP_MATCH_VIEW_SIDE_COUNT; ++side ) {
		if ( !ReadByteValue( field, budgets[ side ].configured,
			MP_MATCH_VIEW_FIELD_TIMEOUTS, error ) ||
			!ReadByteValue( field, budgets[ side ].remaining,
				MP_MATCH_VIEW_FIELD_TIMEOUTS, error ) ||
			!ReadByteValue( field, budgets[ side ].consumed,
				MP_MATCH_VIEW_FIELD_TIMEOUTS, error ) ||
			!ReadUShortValue( field, budgets[ side ].durationSeconds,
				MP_MATCH_VIEW_FIELD_TIMEOUTS, error ) ) {
			return false;
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_TIMEOUTS, error );
}

static bool DecodeRolesField( idBitMsg &field, mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, state.roleSummaryCount,
		MP_MATCH_VIEW_FIELD_ROLES, error ) ||
		state.roleSummaryCount > MP_MATCH_VIEW_MAX_ROLE_SUMMARIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_ROLES, state.roleSummaryCount );
		return false;
	}
	for ( int i = 0; i < state.roleSummaryCount; ++i ) {
		unsigned char role = 0;
		if ( !ReadByteValue( field, role, MP_MATCH_VIEW_FIELD_ROLES, error ) ||
			!ReadSideValue( field, state.roleSummaries[ i ].side,
				MP_MATCH_VIEW_FIELD_ROLES, error ) ||
			!ReadByteValue( field, state.roleSummaries[ i ].count,
				MP_MATCH_VIEW_FIELD_ROLES, error ) ) {
			return false;
		}
		state.roleSummaries[ i ].role = static_cast<mpMatchViewPublicRole_t>( role );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_ROLES, error );
}

static bool DecodeTeamsField( idBitMsg &field, mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, state.rosterSummaryCount,
		MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
		state.rosterSummaryCount > MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_TEAMS, state.rosterSummaryCount );
		return false;
	}
	for ( int i = 0; i < state.rosterSummaryCount; ++i ) {
		mpMatchViewRosterSummary_t &summary = state.rosterSummaries[ i ];
		if ( !ReadSideValue( field, summary.side, MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.declaredSeats,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.occupiedSeats,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.connectedOccupants,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.readyOccupants,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.activeParticipants,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadByteValue( field, summary.queueDepth,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadBoolValue( field, summary.teamReady,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ||
			!ReadBoolValue( field, summary.locked,
				MP_MATCH_VIEW_FIELD_TEAMS, error ) ) {
			return false;
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_TEAMS, error );
}

static bool DecodeProposal( idBitMsg &field, mpMatchViewProposalSummary_t &proposal,
	mpMatchViewError_t *error ) {
	proposal.Clear();
	if ( !ReadBoolValue( field, proposal.present,
		MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ) {
		return false;
	}
	if ( !proposal.present ) {
		return true;
	}
	unsigned char opcode = 0;
	unsigned char scope = 0;
	unsigned char ballot = 0;
	if ( !ReadUIntValue( field, proposal.proposalId,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadByteValue( field, opcode, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadByteValue( field, scope, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadSideValue( field, proposal.side, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUIntValue( field, proposal.callerParticipantId,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.yesCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.noCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.abstainCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.castCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.eligibleCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.requiredQuorumCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUShortValue( field, proposal.requiredYesCount,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadUInt64Value( field, proposal.expiresAtEngineMsec,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadBoolValue( field, proposal.recipientEligible,
			MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ReadByteValue( field, ballot, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ) {
		return false;
	}
	proposal.opcode = static_cast<mpMatchOperationOpcode_t>( opcode );
	proposal.scope = static_cast<mpMatchViewProposalScope_t>( scope );
	proposal.recipientBallot = static_cast<mpMatchViewBallot_t>( ballot );
	return true;
}

static bool DecodeProposalsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !DecodeProposal( field, view.publicState.globalProposal, error ) ||
		!DecodeProposal( field, view.ownSideProposal, error ) ) {
		return false;
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_PROPOSALS, error );
}

static bool DecodeSeriesField( idBitMsg &field, mpMatchViewSeriesSummary_t &series,
	mpMatchViewError_t *error ) {
	series.Clear();
	if ( !ReadBoolValue( field, series.present, MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
		return false;
	}
	if ( !series.present ) {
		return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_SERIES, error );
	}
	unsigned char state = 0;
	if ( !ReadUInt64Value( field, series.seriesId, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, state, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadUInt64Value( field, series.revision, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadIntValue( field, series.gameType, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, series.bestOf, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, series.currentMapNumber, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, series.wins[ 0 ], MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, series.wins[ 1 ], MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadBoolValue( field, series.hasNextMap, MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
		return false;
	}
	series.state = static_cast<mpMatchViewSeriesState_t>( state );
	if ( series.hasNextMap && !ReadMapToken( field, series.nextMapLength,
		series.nextMap, MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
		return false;
	}
	if ( !ReadByteValue( field, series.currentVetoStep,
			MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadByteValue( field, series.vetoStepCount,
			MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		!ReadBoolValue( field, series.hasVetoTurn,
			MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
		return false;
	}
	if ( series.hasVetoTurn ) {
		unsigned char action = 0;
		if ( !ReadByteValue( field, action, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, series.vetoTurnSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
			return false;
		}
		series.vetoTurnAction = static_cast<mpMatchViewVetoAction_t>( action );
	}
	if ( !ReadByteValue( field, series.mapPoolCount, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		series.mapPoolCount > MP_MATCH_VIEW_MAX_SERIES_MAP_POOL ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_SERIES, series.mapPoolCount );
		return false;
	}
	for ( int i = 0; i < series.mapPoolCount; ++i ) {
		mpMatchViewSeriesMap_t &map = series.mapPool[ i ];
		unsigned char disposition = 0;
		if ( !ReadByteValue( field, map.poolIndex, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, disposition, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, map.selectedBySide, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, map.selectionNumber, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadBoolValue( field, map.decider, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadBoolValue( field, map.hasStartingGameSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, map.startingGameSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, map.gameSideChosenBy,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadMapToken( field, map.tokenLength, map.mapToken,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
			return false;
		}
		map.disposition = static_cast<mpMatchViewMapDisposition_t>( disposition );
	}
	if ( !ReadByteValue( field, series.vetoHistoryCount,
			MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		series.vetoHistoryCount > MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_SERIES, series.vetoHistoryCount );
		return false;
	}
	for ( int i = 0; i < series.vetoHistoryCount; ++i ) {
		mpMatchViewVetoHistory_t &entry = series.vetoHistory[ i ];
		unsigned char action = 0;
		if ( !ReadByteValue( field, entry.sequenceNumber,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, action, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, entry.actingSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, entry.mapPoolIndex,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadBoolValue( field, entry.hasSelectedGameSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
			return false;
		}
		entry.action = static_cast<mpMatchViewVetoAction_t>( action );
		if ( entry.hasSelectedGameSide &&
			!ReadSideValue( field, entry.selectedGameSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
			return false;
		}
	}
	if ( !ReadByteValue( field, series.mapHistoryCount,
			MP_MATCH_VIEW_FIELD_SERIES, error ) ||
		series.mapHistoryCount > MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_SERIES, series.mapHistoryCount );
		return false;
	}
	for ( int i = 0; i < series.mapHistoryCount; ++i ) {
		mpMatchViewSeriesMapHistory_t &entry = series.mapHistory[ i ];
		unsigned char outcome = 0;
		if ( !ReadByteValue( field, entry.attemptNumber,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, entry.mapPoolIndex,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadByteValue( field, outcome, MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadSideValue( field, entry.winnerSide,
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadUShortValue( field, entry.scores[ 0 ],
				MP_MATCH_VIEW_FIELD_SERIES, error ) ||
			!ReadUShortValue( field, entry.scores[ 1 ],
				MP_MATCH_VIEW_FIELD_SERIES, error ) ) {
			return false;
		}
		entry.outcome = static_cast<mpMatchViewMapOutcome_t>( outcome );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_SERIES, error );
}

static bool DecodeOperationAvailabilityField( idBitMsg &field,
	mpMatchViewPublicState_t &state, mpMatchViewError_t *error ) {
	if ( !ReadUInt64Value( field, state.allowedOperations,
			MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
		!ReadByteValue( field, state.operationAvailabilityCount,
			MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
		state.operationAvailabilityCount > MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY,
			state.operationAvailabilityCount );
		return false;
	}
	for ( int i = 0; i < state.operationAvailabilityCount; ++i ) {
		mpMatchViewOperationAvailability_t &availability = state.operationAvailability[ i ];
		unsigned char opcode = 0;
		unsigned char reason = 0;
		unsigned short localization = 0;
		if ( !ReadByteValue( field, opcode,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
			!ReadBoolValue( field, availability.available,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
			!ReadByteValue( field, reason,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
			!ReadUShortValue( field, localization,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
			!ReadByteValue( field, availability.fieldId,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ||
			!ReadUIntValue( field, availability.detail,
				MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error ) ) {
			return false;
		}
		availability.opcode = static_cast<mpMatchOperationOpcode_t>( opcode );
		availability.reason = static_cast<mpMatchProtocolReason_t>( reason );
		availability.localizationId = static_cast<mpMatchLocalizationId_t>( localization );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, error );
}

static bool DecodeDenialField( idBitMsg &field, mpMatchViewDenial_t &denial,
	mpMatchViewError_t *error ) {
	denial.Clear();
	if ( !ReadBoolValue( field, denial.present, MP_MATCH_VIEW_FIELD_DENIAL, error ) ) {
		return false;
	}
	if ( !denial.present ) {
		return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_DENIAL, error );
	}
	unsigned char opcode = 0;
	unsigned char reason = 0;
	unsigned short localization = 0;
	if ( !ReadByteValue( field, opcode, MP_MATCH_VIEW_FIELD_DENIAL, error ) ||
		!ReadByteValue( field, reason, MP_MATCH_VIEW_FIELD_DENIAL, error ) ||
		!ReadUShortValue( field, localization, MP_MATCH_VIEW_FIELD_DENIAL, error ) ||
		!ReadByteValue( field, denial.fieldId, MP_MATCH_VIEW_FIELD_DENIAL, error ) ||
		!ReadUIntValue( field, denial.detail, MP_MATCH_VIEW_FIELD_DENIAL, error ) ) {
		return false;
	}
	denial.opcode = static_cast<mpMatchOperationOpcode_t>( opcode );
	denial.reason = static_cast<mpMatchProtocolReason_t>( reason );
	denial.localizationId = static_cast<mpMatchLocalizationId_t>( localization );
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_DENIAL, error );
}

static bool DecodeVitalsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.teamVitalCount,
		MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ||
		view.teamVitalCount > MP_MATCH_VIEW_MAX_TEAM_VITALS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_TEAM_VITALS, view.teamVitalCount );
		return false;
	}
	for ( int i = 0; i < view.teamVitalCount; ++i ) {
		if ( !ReadUIntValue( field, view.teamVitals[ i ].participantId,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ||
			!ReadSideValue( field, view.teamVitals[ i ].participantSide,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ||
			!ReadUShortValue( field, view.teamVitals[ i ].health,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ||
			!ReadUShortValue( field, view.teamVitals[ i ].armor,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ||
			!ReadBoolValue( field, view.teamVitals[ i ].alive,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, error ) ) {
			return false;
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_TEAM_VITALS, error );
}

static bool DecodeItemsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.itemTimingCount,
		MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, error ) ||
		view.itemTimingCount > MP_MATCH_VIEW_MAX_ITEM_TIMINGS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, view.itemTimingCount );
		return false;
	}
	for ( int i = 0; i < view.itemTimingCount; ++i ) {
		if ( !ReadBoolValue( field, view.itemTimings[ i ].available,
				MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, error ) ||
			!ReadUInt64Value( field, view.itemTimings[ i ].matchDeadlineMsec,
				MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, error ) ||
			!ReadMachineToken( field, view.itemTimings[ i ].tokenLength,
				view.itemTimings[ i ].token, MP_MATCH_VIEW_ITEM_TOKEN_BYTES,
				MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, error ) ) {
			return false;
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, error );
}

static bool DecodeFollowTargetsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.followTargetCount,
		MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, error ) ||
		view.followTargetCount > MP_MATCH_VIEW_MAX_FOLLOW_TARGETS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, view.followTargetCount );
		return false;
	}
	for ( int i = 0; i < view.followTargetCount; ++i ) {
		if ( !ReadUIntValue( field, view.followTargets[ i ].participantId,
				MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, error ) ||
			!ReadSideValue( field, view.followTargets[ i ].participantSide,
				MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, error ) ||
			!ReadBoolValue( field, view.followTargets[ i ].selectable,
				MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, error ) ) {
			return false;
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, error );
}

static bool DecodeRecipientField( idBitMsg &field, mpMatchViewRecipient_t &recipient,
	mpMatchViewError_t *error ) {
	unsigned char queueState = 0;
	if ( !ReadUIntValue( field, recipient.participantId,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadByteValue( field, recipient.slot, MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadUIntValue( field, recipient.bindingGeneration,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadSideValue( field, recipient.side, MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadSideValue( field, recipient.competitionSide,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadUIntValue( field, recipient.publicRoleMask,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadBoolValue( field, recipient.ready, MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadBoolValue( field, recipient.active, MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadBoolValue( field, recipient.readyEligible,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadByteValue( field, queueState, MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadSideValue( field, recipient.queueSide,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadBoolValue( field, recipient.hasQueuePosition,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadByteValue( field, recipient.queuePosition,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ||
		!ReadBoolValue( field, recipient.resumeConsented,
			MP_MATCH_VIEW_FIELD_RECIPIENT, error ) ) {
		return false;
	}
	recipient.queueState = static_cast<mpMatchViewQueueState_t>( queueState );
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_RECIPIENT, error );
}

static bool DecodeEvidenceField( idBitMsg &field,
	mpMatchViewEvidenceSummary_t &evidence, mpMatchViewError_t *error ) {
	unsigned char evidenceState = 0;
	unsigned char mvdState = 0;
	unsigned char reportState = 0;
	if ( !ReadByteValue( field, evidenceState, MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadByteValue( field, mvdState, MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadByteValue( field, reportState, MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadUInt64Value( field, evidence.evidenceRevision,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadUShortValue( field, evidence.eventCount,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadUIntValue( field, evidence.droppedRecordCount,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadBoolValue( field, evidence.droppedRecordCountSaturated,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadByteValue( field, evidence.participantStatsCount,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadByteValue( field, evidence.teamStatsCount,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadBoolValue( field, evidence.resultRecorded,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		!ReadByteValue( field, evidence.recentEventCount,
			MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ||
		evidence.recentEventCount > MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ) {
		if ( error == 0 || error->reason == MP_MATCH_VIEW_ERROR_NONE ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
				MP_MATCH_VIEW_FIELD_EVIDENCE, evidence.recentEventCount );
		}
		return false;
	}
	evidence.evidenceState = static_cast<mpMatchViewEvidenceState_t>( evidenceState );
	evidence.mvdState = static_cast<mpMatchViewMVDState_t>( mvdState );
	evidence.reportState = static_cast<mpMatchViewReportState_t>( reportState );
	for ( int i = 0; i < evidence.recentEventCount; ++i ) {
		unsigned char kind = 0;
		if ( !ReadByteValue( field, kind, MP_MATCH_VIEW_FIELD_EVIDENCE, error ) ) {
			return false;
		}
		evidence.recentEventKinds[ i ] =
			static_cast<mpMatchViewEvidenceEventKind_t>( kind );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_EVIDENCE, error );
}

static bool DecodeTerminalResultField( idBitMsg &field,
	mpMatchViewTerminalResult_t &result, mpMatchViewError_t *error ) {
	unsigned char outcome = 0, reason = 0;
	if ( !ReadByteValue( field, outcome, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ||
		!ReadByteValue( field, reason, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ||
		!ReadUInt64Value( field, result.resultRevision, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ||
		!ReadSideValue( field, result.winnerSide, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ||
		!ReadUIntValue( field, result.winnerParticipantId, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ||
		!ReadByteValue( field, result.winnerNameLength, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error ) ) return false;
	if ( outcome >= MP_MATCH_VIEW_RESULT_OUTCOME_COUNT || reason >= MP_MATCH_VIEW_RESULT_REASON_COUNT ||
		result.winnerNameLength > MP_MATCH_VIEW_RESULT_NAME_BYTES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
		return false;
	}
	result.outcome = static_cast<mpMatchViewResultOutcome_t>( outcome );
	result.reason = static_cast<mpMatchViewResultReason_t>( reason );
	if ( field.GetRemainingReadBits() < result.winnerNameLength * 8 ||
		( result.winnerNameLength != 0 && field.ReadData( result.winnerName,
			result.winnerNameLength ) != result.winnerNameLength ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
		return false;
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, error );
}

static bool DecodeParticipantsField( idBitMsg &field, mpMatchViewPublicState_t &state,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, state.participantSummaryCount,
			MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ||
		state.participantSummaryCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_PARTICIPANTS, state.participantSummaryCount );
		return false;
	}
	for ( int i = 0; i < state.participantSummaryCount; ++i ) {
		mpMatchViewParticipantSummary_t &participant = state.participantSummaries[ i ];
		unsigned char flags = 0;
		if ( !ReadUIntValue( field, participant.participantId,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ||
			!ReadByteValue( field, participant.slot,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ||
			!ReadSideValue( field, participant.side,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ||
			!ReadUIntValue( field, participant.publicRoleMask,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ||
			!ReadByteValue( field, flags,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, error ) ) {
			return false;
		}
		if ( ( flags & ~MP_MATCH_VIEW_PARTICIPANT_FLAG_MASK ) != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_PARTICIPANTS, i );
			return false;
		}
		participant.connected =
			( flags & MP_MATCH_VIEW_PARTICIPANT_CONNECTED_BIT ) != 0;
		participant.human = ( flags & MP_MATCH_VIEW_PARTICIPANT_HUMAN_BIT ) != 0;
		participant.active = ( flags & MP_MATCH_VIEW_PARTICIPANT_ACTIVE_BIT ) != 0;
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_PARTICIPANTS, error );
}

static bool DecodeRulesField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	mpMatchViewCommittedRules_t &committed = view.publicState.committedRules;
	committed.Clear();
	if ( !ReadBoolValue( field, committed.present, MP_MATCH_VIEW_FIELD_RULES, error ) ) {
		return false;
	}
	if ( committed.present ) {
		unsigned char boundary = 0;
		if ( !ReadUIntValue( field, committed.rulesSchemaVersion,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadUIntValue( field, committed.revision,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadUInt64Value( field, committed.digest,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadIntValue( field, committed.profileId,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadBoolValue( field, committed.customized,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadByteValue( field, boundary, MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadByteValue( field, committed.valueCount,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			committed.valueCount > MP_MATCH_VIEW_MAX_RULE_FIELDS ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
				MP_MATCH_VIEW_FIELD_RULES, committed.valueCount );
			return false;
		}
		committed.boundary = static_cast<mpMatchViewRulesBoundary_t>( boundary );
		for ( int i = 0; i < committed.valueCount; ++i ) {
			unsigned char type = 0;
			if ( !ReadByteValue( field, committed.values[ i ].fieldId,
					MP_MATCH_VIEW_FIELD_RULES, error ) ||
				!ReadByteValue( field, type, MP_MATCH_VIEW_FIELD_RULES, error ) ||
				!ReadIntValue( field, committed.values[ i ].value,
					MP_MATCH_VIEW_FIELD_RULES, error ) ||
				!ReadBoolValue( field, committed.values[ i ].editable,
					MP_MATCH_VIEW_FIELD_RULES, error ) ) {
				return false;
			}
			committed.values[ i ].type = static_cast<mpMatchViewRuleType_t>( type );
		}
	}
	mpMatchViewStagedRules_t &staged = view.stagedRules;
	staged.Clear();
	if ( !ReadBoolValue( field, staged.present, MP_MATCH_VIEW_FIELD_RULES, error ) ) {
		return false;
	}
	if ( staged.present ) {
		if ( !ReadUIntValue( field, staged.revision, MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadUInt64Value( field, staged.digest, MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadIntValue( field, staged.profileId, MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadBoolValue( field, staged.customized, MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadUInt64Value( field, staged.changedFieldMask,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			!ReadByteValue( field, staged.valueCount,
				MP_MATCH_VIEW_FIELD_RULES, error ) ||
			staged.valueCount > MP_MATCH_VIEW_MAX_RULE_FIELDS ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
				MP_MATCH_VIEW_FIELD_RULES, staged.valueCount );
			return false;
		}
		for ( int i = 0; i < staged.valueCount; ++i ) {
			unsigned char type = 0;
			if ( !ReadByteValue( field, staged.values[ i ].fieldId,
					MP_MATCH_VIEW_FIELD_RULES, error ) ||
				!ReadByteValue( field, type, MP_MATCH_VIEW_FIELD_RULES, error ) ||
				!ReadIntValue( field, staged.values[ i ].value,
					MP_MATCH_VIEW_FIELD_RULES, error ) ) {
				return false;
			}
			staged.values[ i ].type = static_cast<mpMatchViewRuleType_t>( type );
		}
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_RULES, error );
}

static bool DecodeRosterSeatsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.rosterSeatCount,
			MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
		view.rosterSeatCount > MP_MATCH_VIEW_MAX_ROSTER_SEATS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_ROSTER_SEATS, view.rosterSeatCount );
		return false;
	}
	for ( int i = 0; i < view.rosterSeatCount; ++i ) {
		mpMatchViewRosterSeat_t &seat = view.rosterSeats[ i ];
		unsigned char role = 0;
		if ( !ReadByteValue( field, seat.seatIndex,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadSideValue( field, seat.side,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadByteValue( field, role, MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadBoolValue( field, seat.required,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadBoolValue( field, seat.occupied,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadUIntValue( field, seat.participantId,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadBoolValue( field, seat.connected,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadBoolValue( field, seat.ready,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ||
			!ReadBoolValue( field, seat.active,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error ) ) {
			return false;
		}
		seat.role = static_cast<mpMatchViewRosterRole_t>( role );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_ROSTER_SEATS, error );
}

static bool DecodeInvitationsField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.invitationCount,
			MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
		view.invitationCount > MP_MATCH_VIEW_MAX_INVITATIONS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_INVITATIONS, view.invitationCount );
		return false;
	}
	for ( int i = 0; i < view.invitationCount; ++i ) {
		mpMatchViewInvitationSummary_t &invitation = view.invitations[ i ];
		unsigned char role = 0;
		if ( !ReadUIntValue( field, invitation.invitationId,
				MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
			!ReadSideValue( field, invitation.side,
				MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
			!ReadByteValue( field, role, MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
			!ReadUIntValue( field, invitation.inviterParticipantId,
				MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
			!ReadUIntValue( field, invitation.inviteeParticipantId,
				MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ||
			!ReadUInt64Value( field, invitation.expiresAtEngineMsec,
				MP_MATCH_VIEW_FIELD_INVITATIONS, error ) ) {
			return false;
		}
		invitation.role = static_cast<mpMatchViewRosterRole_t>( role );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_INVITATIONS, error );
}

static bool DecodeQueuesField( idBitMsg &field, mpSessionView &view,
	mpMatchViewError_t *error ) {
	if ( !ReadByteValue( field, view.queueEntryCount,
			MP_MATCH_VIEW_FIELD_QUEUES, error ) ||
		view.queueEntryCount > MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT,
			MP_MATCH_VIEW_FIELD_QUEUES, view.queueEntryCount );
		return false;
	}
	for ( int i = 0; i < view.queueEntryCount; ++i ) {
		unsigned char state = 0;
		if ( !ReadUIntValue( field, view.queueEntries[ i ].participantId,
				MP_MATCH_VIEW_FIELD_QUEUES, error ) ||
			!ReadSideValue( field, view.queueEntries[ i ].side,
				MP_MATCH_VIEW_FIELD_QUEUES, error ) ||
			!ReadByteValue( field, view.queueEntries[ i ].position,
				MP_MATCH_VIEW_FIELD_QUEUES, error ) ||
			!ReadByteValue( field, state, MP_MATCH_VIEW_FIELD_QUEUES, error ) ) {
			return false;
		}
		view.queueEntries[ i ].state = static_cast<mpMatchViewQueueState_t>( state );
	}
	return FinishFieldRead( field, MP_MATCH_VIEW_FIELD_QUEUES, error );
}

static bool DecodeKnownField( unsigned char fieldId, const byte *data, int length,
	mpSessionView &view, mpMatchViewError_t *error ) {
	idBitMsg field;
	field.Init( data, length );
	field.SetSize( length );
	field.BeginReading();
	switch ( fieldId ) {
		case MP_MATCH_VIEW_FIELD_SCHEMA:
			return ReadUShortValue( field, view.publicState.schemaVersion, fieldId, error ) &&
				FinishFieldRead( field, fieldId, error );
		case MP_MATCH_VIEW_FIELD_REVISION:
			return ReadUInt64Value( field, view.publicState.sessionRevision, fieldId, error ) &&
				FinishFieldRead( field, fieldId, error );
		case MP_MATCH_VIEW_FIELD_LIFECYCLE:
			return DecodeLifecycleField( field, view.publicState.lifecycle, error );
		case MP_MATCH_VIEW_FIELD_CLOCKS:
			return DecodeClocksField( field, view.publicState.clocks, error );
		case MP_MATCH_VIEW_FIELD_READINESS:
			return DecodeReadinessField( field, view.publicState.readiness, error );
		case MP_MATCH_VIEW_FIELD_TIMEOUTS:
			return DecodeTimeoutsField( field, view.publicState.timeoutBudgets, error );
		case MP_MATCH_VIEW_FIELD_ROLES:
			return DecodeRolesField( field, view.publicState, error );
		case MP_MATCH_VIEW_FIELD_TEAMS:
			return DecodeTeamsField( field, view.publicState, error );
		case MP_MATCH_VIEW_FIELD_PROPOSALS:
			return DecodeProposalsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_SERIES:
			return DecodeSeriesField( field, view.publicState.series, error );
		case MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY:
			return DecodeOperationAvailabilityField( field, view.publicState, error );
		case MP_MATCH_VIEW_FIELD_DENIAL:
			return DecodeDenialField( field, view.publicState.denial, error );
		case MP_MATCH_VIEW_FIELD_TEAM_VITALS:
			return DecodeVitalsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_ITEM_TIMINGS:
			return DecodeItemsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS:
			return DecodeFollowTargetsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_RECIPIENT:
			return DecodeRecipientField( field, view.publicState.recipient, error );
		case MP_MATCH_VIEW_FIELD_VIEW_REVISION:
			return ReadUInt64Value( field, view.publicState.viewRevision, fieldId, error ) &&
				FinishFieldRead( field, fieldId, error );
		case MP_MATCH_VIEW_FIELD_PARTICIPANTS:
			return DecodeParticipantsField( field, view.publicState, error );
		case MP_MATCH_VIEW_FIELD_RULES:
			return DecodeRulesField( field, view, error );
		case MP_MATCH_VIEW_FIELD_ROSTER_SEATS:
			return DecodeRosterSeatsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_INVITATIONS:
			return DecodeInvitationsField( field, view, error );
		case MP_MATCH_VIEW_FIELD_QUEUES:
			return DecodeQueuesField( field, view, error );
		case MP_MATCH_VIEW_FIELD_CONTROL_REVISION:
			return ReadUInt64Value( field, view.publicState.controlRevision,
				fieldId, error ) && FinishFieldRead( field, fieldId, error );
		case MP_MATCH_VIEW_FIELD_EVIDENCE:
			return DecodeEvidenceField( field, view.publicState.evidence, error );
		case MP_MATCH_VIEW_FIELD_TERMINAL_RESULT:
			return DecodeTerminalResultField( field, view.publicState.terminalResult, error );
		default:
			SetError( error, MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD, fieldId );
			return false;
	}
}

static bool DecodePayload( const byte *data, int length,
	mpSessionView &view, mpMatchViewError_t *error ) {
	idBitMsg payload;
	payload.Init( data, length );
	payload.SetSize( length );
	payload.BeginReading();
	if ( payload.GetRemainingReadBits() < 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED );
		return false;
	}
	const int fieldCount = payload.ReadByte();
	if ( fieldCount < MP_MATCH_VIEW_REQUIRED_FIELD_COUNT ||
		fieldCount > MP_MATCH_VIEW_MAX_TOP_LEVEL_FIELDS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT, 0, fieldCount );
		return false;
	}
	bool seen[ 128 ];
	memset( seen, 0, sizeof( seen ) );
	byte fieldData[ MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ];
	for ( int i = 0; i < fieldCount; ++i ) {
		if ( payload.GetRemainingReadBits() < 24 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED );
			return false;
		}
		const unsigned char rawTag = static_cast<unsigned char>( payload.ReadByte() );
		const int fieldLength = payload.ReadUShort();
		const unsigned char fieldId = rawTag & MP_MATCH_VIEW_FIELD_ID_MASK;
		const bool known = fieldId >= MP_MATCH_VIEW_FIELD_SCHEMA &&
			fieldId <= MP_MATCH_VIEW_FIELD_TERMINAL_RESULT;
		if ( fieldId == 0 || seen[ fieldId ] ) {
			SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD, fieldId, rawTag );
			return false;
		}
		seen[ fieldId ] = true;
		if ( known && ( rawTag & MP_MATCH_VIEW_OPTIONAL_EXTENSION_BIT ) != 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD, fieldId, rawTag );
			return false;
		}
		if ( !known && ( rawTag & MP_MATCH_VIEW_OPTIONAL_EXTENSION_BIT ) == 0 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD, fieldId, rawTag );
			return false;
		}
		if ( fieldLength < 0 || fieldLength > MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ||
			payload.GetRemainingReadBits() < fieldLength * 8 ) {
			SetError( error, fieldLength > MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ?
				MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE : MP_MATCH_VIEW_ERROR_TRUNCATED,
				fieldId, fieldLength );
			return false;
		}
		if ( fieldLength > 0 && payload.ReadData( fieldData, fieldLength ) != fieldLength ) {
			SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, fieldId, fieldLength );
			return false;
		}
		if ( known && !DecodeKnownField( fieldId, fieldData, fieldLength, view, error ) ) {
			return false;
		}
	}
	if ( payload.GetRemainingReadBits() != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRAILING_DATA, 0,
			static_cast<unsigned int>( payload.GetRemainingReadBits() ) );
		return false;
	}
	for ( int fieldId = MP_MATCH_VIEW_FIELD_SCHEMA;
		fieldId <= MP_MATCH_VIEW_FIELD_TERMINAL_RESULT; ++fieldId ) {
		if ( !seen[ fieldId ] ) {
			SetError( error, MP_MATCH_VIEW_ERROR_MISSING_REQUIRED_FIELD,
				static_cast<unsigned char>( fieldId ) );
			return false;
		}
	}
	return true;
}

} // namespace

bool MPMatchViewEncode( idBitMsg &message, const mpSessionView &view,
	mpMatchViewError_t *error ) {
	ClearError( error );
	if ( !MPMatchViewValidate( view, error ) ) {
		return false;
	}
	if ( message.GetWriteBit() != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_ALIGNMENT );
		return false;
	}
	byte payload[ MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ];
	int payloadLength = 0;
	if ( !BuildPayload( view, payload, payloadLength, error ) ) {
		return false;
	}
	byte encoded[ MP_MATCH_VIEW_MAX_MESSAGE_BYTES ];
	idBitMsg staging;
	staging.Init( encoded, sizeof( encoded ) );
	staging.SetAllowOverflow( true );
	staging.BeginWriting();
	staging.WriteUShort( MP_MATCH_PROTOCOL_MAGIC );
	staging.WriteUShort( MP_MATCH_PROTOCOL_SCHEMA_VERSION );
	staging.WriteByte( MP_MATCH_ENVELOPE_SESSION_VIEW );
	WriteUInt64Value( staging, view.publicState.sessionId );
	staging.WriteUShort( payloadLength );
	staging.WriteData( payload, payloadLength );
	if ( staging.IsOverflowed() || staging.GetSize() > MP_MATCH_VIEW_MAX_MESSAGE_BYTES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE, 0,
			static_cast<unsigned int>( staging.GetSize() ) );
		return false;
	}
	if ( message.IsOverflowed() || message.GetRemainingWriteBits() < staging.GetSize() * 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_BUFFER_TOO_SMALL, 0,
			static_cast<unsigned int>( staging.GetSize() ) );
		return false;
	}
	int savedSize = 0;
	int savedBit = 0;
	message.SaveWriteState( savedSize, savedBit );
	message.WriteData( encoded, staging.GetSize() );
	if ( message.IsOverflowed() ) {
		message.RestoreWriteState( savedSize, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_BUFFER_TOO_SMALL, 0,
			static_cast<unsigned int>( staging.GetSize() ) );
		return false;
	}
	return true;
}

bool MPMatchViewDecode( const idBitMsg &message, mpSessionView &view,
	mpMatchViewError_t *error ) {
	ClearError( error );
	int savedCount = 0;
	int savedBit = 0;
	message.SaveReadState( savedCount, savedBit );
	if ( message.GetReadBit() != 0 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_ALIGNMENT );
		return false;
	}
	if ( message.GetRemainingReadBits() < MP_MATCH_VIEW_ENVELOPE_HEADER_BYTES * 8 ) {
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED );
		return false;
	}
	const int magic = message.ReadUShort();
	const int protocolSchema = message.ReadUShort();
	const int kind = message.ReadByte();
	const unsigned int sessionLow = static_cast<unsigned int>( message.ReadLong() );
	const unsigned int sessionHigh = static_cast<unsigned int>( message.ReadLong() );
	const mpMatchProtocolSessionId_t sessionId =
		static_cast<mpMatchProtocolSessionId_t>( sessionLow ) |
		( static_cast<mpMatchProtocolSessionId_t>( sessionHigh ) << 32 );
	const int payloadLength = message.ReadUShort();
	if ( magic != MP_MATCH_PROTOCOL_MAGIC || kind != MP_MATCH_ENVELOPE_SESSION_VIEW ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_UNKNOWN_ENVELOPE, 0,
			magic != MP_MATCH_PROTOCOL_MAGIC ? magic : kind );
		return false;
	}
	if ( protocolSchema != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_UNSUPPORTED_SCHEMA, 0, protocolSchema );
		return false;
	}
	if ( sessionId == 0 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_SESSION );
		return false;
	}
	if ( payloadLength < 0 || payloadLength > MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE, 0, payloadLength );
		return false;
	}
	if ( message.GetRemainingReadBits() < payloadLength * 8 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, 0, payloadLength );
		return false;
	}
	byte payload[ MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ];
	if ( payloadLength > 0 && message.ReadData( payload, payloadLength ) != payloadLength ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_TRUNCATED, 0, payloadLength );
		return false;
	}
	if ( message.GetRemainingReadBits() != 0 ) {
		const int trailing = message.GetRemainingReadBits();
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_VIEW_ERROR_TRAILING_DATA, 0, trailing );
		return false;
	}
	mpSessionView decoded;
	decoded.Clear();
	decoded.publicState.sessionId = sessionId;
	if ( !DecodePayload( payload, payloadLength, decoded, error ) ||
		!MPMatchViewValidate( decoded, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	view = decoded;
	return true;
}

mpMatchViewAcceptResult_t MPMatchViewAccept( mpSessionView &current,
	const mpSessionView &incoming, mpMatchViewError_t *error ) {
	ClearError( error );
	if ( !MPMatchViewValidate( incoming, error ) ) {
		return MP_MATCH_VIEW_ACCEPT_REJECTED_INVALID;
	}
	if ( current.publicState.sessionId == 0 ||
		current.publicState.sessionId != incoming.publicState.sessionId ) {
		current = incoming;
		return MP_MATCH_VIEW_ACCEPT_REPLACED_SESSION;
	}
	if ( incoming.publicState.viewRevision < current.publicState.viewRevision ) {
		SetError( error, MP_MATCH_VIEW_ERROR_STALE, MP_MATCH_VIEW_FIELD_VIEW_REVISION );
		return MP_MATCH_VIEW_ACCEPT_REJECTED_STALE;
	}
	const mpMatchViewTerminalResult_t &previous = current.publicState.terminalResult;
	const mpMatchViewTerminalResult_t &next = incoming.publicState.terminalResult;
	if ( previous.resultRevision != 0 && next.resultRevision != 0 ) {
		if ( next.resultRevision < previous.resultRevision ) {
			SetError( error, MP_MATCH_VIEW_ERROR_STALE, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
			return MP_MATCH_VIEW_ACCEPT_REJECTED_STALE;
		}
		if ( next.resultRevision == previous.resultRevision &&
			( next.outcome != previous.outcome || next.reason != previous.reason ||
				next.winnerSide != previous.winnerSide || next.winnerParticipantId != previous.winnerParticipantId ||
				next.winnerNameLength != previous.winnerNameLength ||
				memcmp( next.winnerName, previous.winnerName, sizeof( next.winnerName ) ) != 0 ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT );
			return MP_MATCH_VIEW_ACCEPT_REJECTED_INVALID;
		}
	}
	if ( incoming.publicState.viewRevision == current.publicState.viewRevision ) {
		return MP_MATCH_VIEW_ACCEPT_NO_CHANGE;
	}
	current = incoming;
	return MP_MATCH_VIEW_ACCEPT_ADVANCED;
}

void mpMatchViewLifecycle_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	phase = INACTIVE;
	round = RS_INACTIVE;
	pauseState = MP_MATCH_VIEW_PAUSE_RUNNING;
	pauseKind = MP_MATCH_VIEW_PAUSE_KIND_NONE;
	pauseReason = MP_MATCH_VIEW_PAUSE_REASON_NONE;
	pauseOwnerSide = MP_MATCH_VIEW_SIDE_NONE;
	resumePolicy = MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE;
}

void mpMatchViewClocks_t::Clear( void ) { memset( this, 0, sizeof( *this ) ); }
void mpMatchViewReadiness_t::Clear( void ) { memset( this, 0, sizeof( *this ) ); }
void mpMatchViewTimeoutBudget_t::Clear( void ) { memset( this, 0, sizeof( *this ) ); }

void mpMatchViewRoleSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	role = MP_MATCH_VIEW_ROLE_NONE;
	side = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewRosterSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	side = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewParticipantSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	slot = 0xffu;
	side = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewProposalSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	opcode = MP_MATCH_OP_INVALID;
	scope = MP_MATCH_VIEW_PROPOSAL_GLOBAL;
	side = MP_MATCH_VIEW_SIDE_NONE;
	recipientBallot = MP_MATCH_VIEW_BALLOT_NONE;
}

void mpMatchViewRuleValue_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	type = MP_MATCH_VIEW_RULE_BOOL;
}

void mpMatchViewCommittedRules_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	boundary = MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT;
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_RULE_FIELDS; ++i ) {
		values[ i ].Clear();
	}
}

void mpMatchViewStagedRuleValue_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	type = MP_MATCH_VIEW_RULE_BOOL;
}

void mpMatchViewStagedRules_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_RULE_FIELDS; ++i ) {
		values[ i ].Clear();
	}
}

void mpMatchViewOperationAvailability_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	opcode = MP_MATCH_OP_INVALID;
	reason = MP_MATCH_PROTOCOL_REASON_NONE;
	localizationId = MP_MATCH_LOCALIZATION_NONE;
}

void mpMatchViewSeriesMap_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	disposition = MP_MATCH_VIEW_MAP_AVAILABLE;
	selectedBySide = MP_MATCH_VIEW_SIDE_NONE;
	startingGameSide = MP_MATCH_VIEW_SIDE_NONE;
	gameSideChosenBy = MP_MATCH_VIEW_SIDE_NONE;
}

bool mpMatchViewSeriesMap_t::SetMapToken( const char *mapTokenValue, int length ) {
	if ( length < 0 ) {
		length = BoundedLength( mapTokenValue, MP_MATCH_VIEW_MAP_TOKEN_BYTES );
	}
	if ( !IsMapToken( mapTokenValue, length ) ) {
		return false;
	}
	memset( mapToken, 0, sizeof( mapToken ) );
	memcpy( mapToken, mapTokenValue, length );
	tokenLength = static_cast<unsigned char>( length );
	return true;
}

void mpMatchViewVetoHistory_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	action = MP_MATCH_VIEW_VETO_BAN;
	actingSide = MP_MATCH_VIEW_SIDE_NONE;
	selectedGameSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewSeriesMapHistory_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	outcome = MP_MATCH_VIEW_MAP_UNPLAYED;
	winnerSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewSeriesSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	state = MP_MATCH_VIEW_SERIES_DISABLED;
	vetoTurnAction = MP_MATCH_VIEW_VETO_BAN;
	vetoTurnSide = MP_MATCH_VIEW_SIDE_NONE;
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_SERIES_MAP_POOL; ++i ) {
		mapPool[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY; ++i ) {
		vetoHistory[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY; ++i ) {
		mapHistory[ i ].Clear();
	}
}

void mpMatchViewTerminalResult_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	winnerSide = MP_MATCH_VIEW_SIDE_NONE;
}

void MPMatchViewSetResultWinnerName( mpMatchViewTerminalResult_t &result, const char *name ) {
	result.winnerNameLength = 0;
	memset( result.winnerName, 0, sizeof( result.winnerName ) );
	if ( result.winnerParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ) return;
	if ( name != 0 ) {
		for ( int input = 0; name[ input ] != '\0' &&
			result.winnerNameLength < MP_MATCH_VIEW_RESULT_NAME_BYTES; ) {
			int available = 0;
			while ( available < 4 && name[ input + available ] != '\0' ) ++available;
			const int bytes = ResultNameCodePointBytes( name + input, available );
			if ( bytes == 0 ) {
				result.winnerName[ result.winnerNameLength++ ] = '_';
				++input;
			} else {
				if ( result.winnerNameLength + bytes > MP_MATCH_VIEW_RESULT_NAME_BYTES ) break;
				memcpy( result.winnerName + result.winnerNameLength, name + input, bytes );
				result.winnerNameLength += static_cast<unsigned char>( bytes );
				input += bytes;
			}
		}
	}
	if ( result.winnerNameLength == 0 ) {
		memcpy( result.winnerName, "player-", 7 );
		result.winnerNameLength = 7;
		char digits[ 10 ];
		int count = 0;
		unsigned int value = result.winnerParticipantId;
		do { digits[ count++ ] = static_cast<char>( '0' + value % 10 ); value /= 10; } while ( value != 0 );
		while ( count > 0 ) result.winnerName[ result.winnerNameLength++ ] = digits[ --count ];
	}
}

void mpMatchViewEvidenceSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	evidenceState = MP_MATCH_VIEW_EVIDENCE_DISABLED;
	mvdState = MP_MATCH_VIEW_MVD_DISABLED;
	reportState = MP_MATCH_VIEW_REPORT_DISABLED;
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS; ++i ) {
		recentEventKinds[ i ] = MP_MATCH_VIEW_EVIDENCE_EVENT_NONE;
	}
}

bool mpMatchViewSeriesSummary_t::SetNextMap( const char *mapTokenValue, int length ) {
	if ( length < 0 ) {
		length = BoundedLength( mapTokenValue, MP_MATCH_VIEW_MAP_TOKEN_BYTES );
	}
	if ( !IsMapToken( mapTokenValue, length ) ) {
		return false;
	}
	memset( nextMap, 0, sizeof( nextMap ) );
	memcpy( nextMap, mapTokenValue, length );
	nextMapLength = static_cast<unsigned char>( length );
	hasNextMap = true;
	return true;
}

void mpMatchViewDenial_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	opcode = MP_MATCH_OP_INVALID;
	reason = MP_MATCH_PROTOCOL_REASON_NONE;
	localizationId = MP_MATCH_LOCALIZATION_NONE;
}

void mpMatchViewRecipient_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	slot = 0xffu;
	side = MP_MATCH_VIEW_SIDE_NONE;
	competitionSide = MP_MATCH_VIEW_SIDE_NONE;
	queueState = MP_MATCH_VIEW_QUEUE_NONE;
	queueSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewRecipientPolicy_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	audiences = MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_PUBLIC ) |
		MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_RECIPIENT );
	ownSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewAuthorizationTag_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	audience = MP_MATCH_VIEW_AUDIENCE_PUBLIC;
	audienceSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewRosterSeat_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	side = MP_MATCH_VIEW_SIDE_NONE;
	role = MP_MATCH_VIEW_ROSTER_PLAYER;
}

void mpMatchViewInvitationSummary_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	side = MP_MATCH_VIEW_SIDE_NONE;
	role = MP_MATCH_VIEW_ROSTER_PLAYER;
}

void mpMatchViewQueueEntry_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	side = MP_MATCH_VIEW_SIDE_NONE;
	state = MP_MATCH_VIEW_QUEUE_NONE;
}

void mpMatchViewProposalCandidate_t::Clear( void ) {
	authorization.Clear();
	value.Clear();
}

void mpMatchViewStagedRulesCandidate_t::Clear( void ) {
	authorization.Clear();
	value.Clear();
}

void mpMatchViewRosterSeatCandidate_t::Clear( void ) {
	authorization.Clear();
	value.Clear();
}

void mpMatchViewInvitationCandidate_t::Clear( void ) {
	authorization.Clear();
	value.Clear();
}

void mpMatchViewQueueEntryCandidate_t::Clear( void ) {
	authorization.Clear();
	value.Clear();
}

void mpMatchViewObserverCandidate_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	authorization.Clear();
	kind = MP_MATCH_VIEW_OBSERVER_TEAM_VITAL;
	participantSide = MP_MATCH_VIEW_SIDE_NONE;
}

bool mpMatchViewObserverCandidate_t::SetTeamVital( mpMatchViewAudience_t audienceTag,
	int tagSide, mpMatchProtocolParticipantId_t participant, int side,
	int health, int armor, bool aliveValue ) {
	if ( audienceTag == MP_MATCH_VIEW_AUDIENCE_RECIPIENT ||
		IsSpectatorSideAudience( audienceTag ) ||
		audienceTag < MP_MATCH_VIEW_AUDIENCE_PUBLIC ||
		audienceTag >= MP_MATCH_VIEW_AUDIENCE_COUNT ||
		( audienceTag == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ? !IsSide( tagSide ) :
			tagSide != MP_MATCH_VIEW_SIDE_NONE ) ||
		participant == MP_MATCH_INVALID_PARTICIPANT_ID || !IsSide( side ) ||
		health < 0 || health > 999 || armor < 0 || armor > 999 ||
		( audienceTag == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE && side != tagSide ) ) {
		return false;
	}
	Clear();
	authorization.audience = audienceTag;
	authorization.audienceSide = tagSide;
	kind = MP_MATCH_VIEW_OBSERVER_TEAM_VITAL;
	participantId = participant;
	participantSide = side;
	primaryValue = static_cast<unsigned short>( health );
	secondaryValue = static_cast<unsigned short>( armor );
	active = aliveValue;
	return true;
}

bool mpMatchViewObserverCandidate_t::SetItemTiming( mpMatchViewAudience_t audienceTag,
	int tagSide, const char *itemToken, unsigned long long deadlineMsec, bool available ) {
	const int length = BoundedLength( itemToken, MP_MATCH_VIEW_ITEM_TOKEN_BYTES );
	if ( audienceTag == MP_MATCH_VIEW_AUDIENCE_RECIPIENT ||
		IsSpectatorSideAudience( audienceTag ) ||
		audienceTag < MP_MATCH_VIEW_AUDIENCE_PUBLIC ||
		audienceTag >= MP_MATCH_VIEW_AUDIENCE_COUNT ||
		( audienceTag == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ? !IsSide( tagSide ) :
			tagSide != MP_MATCH_VIEW_SIDE_NONE ) || !IsTime( deadlineMsec ) ||
		!IsMachineToken( itemToken, length, MP_MATCH_VIEW_ITEM_TOKEN_BYTES ) ) {
		return false;
	}
	Clear();
	authorization.audience = audienceTag;
	authorization.audienceSide = tagSide;
	kind = MP_MATCH_VIEW_OBSERVER_ITEM_TIMING;
	active = available;
	matchDeadlineMsec = deadlineMsec;
	tokenLength = static_cast<unsigned char>( length );
	memcpy( token, itemToken, length );
	token[ length ] = '\0';
	return true;
}

bool mpMatchViewObserverCandidate_t::SetFollowTarget( mpMatchViewAudience_t audienceTag,
	int tagSide, mpMatchProtocolParticipantId_t participant, int side, bool selectable ) {
	const bool sideAudience = audienceTag == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ||
		IsSpectatorSideAudience( audienceTag );
	if ( audienceTag == MP_MATCH_VIEW_AUDIENCE_RECIPIENT ||
		audienceTag < MP_MATCH_VIEW_AUDIENCE_PUBLIC ||
		audienceTag >= MP_MATCH_VIEW_AUDIENCE_COUNT ||
		( sideAudience ? !IsSide( tagSide ) :
			tagSide != MP_MATCH_VIEW_SIDE_NONE ) ||
		( IsSpectatorSideAudience( audienceTag ) &&
			tagSide != SpectatorAudienceSide( audienceTag ) ) ||
		participant == MP_MATCH_INVALID_PARTICIPANT_ID || !IsSide( side ) ||
		( sideAudience && side != tagSide ) ) {
		return false;
	}
	Clear();
	authorization.audience = audienceTag;
	authorization.audienceSide = tagSide;
	kind = MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET;
	participantId = participant;
	participantSide = side;
	active = selectable;
	return true;
}

void mpMatchViewTeamVital_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	participantSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewItemTiming_t::Clear( void ) { memset( this, 0, sizeof( *this ) ); }

void mpMatchViewFollowTarget_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	participantSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewPublicState_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_MATCH_VIEW_SCHEMA_VERSION;
	lifecycle.Clear();
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_ROLE_SUMMARIES; ++i ) {
		roleSummaries[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES; ++i ) {
		rosterSummaries[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_PARTICIPANTS; ++i ) {
		participantSummaries[ i ].Clear();
	}
	globalProposal.Clear();
	series.Clear();
	evidence.Clear();
	terminalResult.Clear();
	operationAvailabilityCount = MP_MATCH_OP_COUNT - 1;
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES; ++i ) {
		operationAvailability[ i ].Clear();
		if ( i < operationAvailabilityCount ) {
			operationAvailability[ i ].opcode =
				static_cast<mpMatchOperationOpcode_t>( i + 1 );
			operationAvailability[ i ].reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
			operationAvailability[ i ].localizationId =
				MPMatchProtocolReasonLocalizationId( MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED );
		}
	}
	denial.Clear();
	recipient.Clear();
	committedRules.Clear();
}

void mpMatchViewSource_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	publicState.Clear();
	for ( int i = 0; i < MP_MATCH_VIEW_SIDE_COUNT; ++i ) {
		proposalCandidates[ i ].Clear();
	}
	for ( int i = 0; i < 4; ++i ) {
		stagedRulesCandidates[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES; ++i ) {
		rosterSeatCandidates[ i ].Clear();
		invitationCandidates[ i ].Clear();
		queueEntryCandidates[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES; ++i ) {
		observerCandidates[ i ].Clear();
	}
}

void mpSessionView_s::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	publicState.Clear();
	ownSideProposal.Clear();
	stagedRules.Clear();
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_ROSTER_SEATS; ++i ) {
		rosterSeats[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_INVITATIONS; ++i ) {
		invitations[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_QUEUE_ENTRIES; ++i ) {
		queueEntries[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_TEAM_VITALS; ++i ) {
		teamVitals[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_ITEM_TIMINGS; ++i ) {
		itemTimings[ i ].Clear();
	}
	for ( int i = 0; i < MP_MATCH_VIEW_MAX_FOLLOW_TARGETS; ++i ) {
		followTargets[ i ].Clear();
	}
}

void mpMatchViewError_t::Clear( void ) {
	reason = MP_MATCH_VIEW_ERROR_NONE;
	fieldId = 0;
	detail = 0;
}

mpMatchViewAudienceMask_t MPMatchViewAudienceBit( mpMatchViewAudience_t audience ) {
	return audience >= MP_MATCH_VIEW_AUDIENCE_PUBLIC && audience < MP_MATCH_VIEW_AUDIENCE_COUNT ?
		( 1u << static_cast<unsigned int>( audience ) ) : 0u;
}

mpMatchViewObserverKindMask_t MPMatchViewObserverKindBit( mpMatchViewObserverKind_t kind ) {
	return kind >= MP_MATCH_VIEW_OBSERVER_TEAM_VITAL && kind < MP_MATCH_VIEW_OBSERVER_KIND_COUNT ?
		( 1u << static_cast<unsigned int>( kind ) ) : 0u;
}

mpMatchViewPublicRoleMask_t MPMatchViewRoleBit( mpMatchViewPublicRole_t role ) {
	return role > MP_MATCH_VIEW_ROLE_NONE && role < MP_MATCH_VIEW_ROLE_COUNT ?
		( 1u << static_cast<unsigned int>( role ) ) : 0u;
}

mpMatchViewAllowedOperationMask_t MPMatchViewOperationBit( mpMatchOperationOpcode_t opcode ) {
	return IsValidOpcode( opcode ) ?
		( 1ull << static_cast<unsigned int>( opcode ) ) : 0ull;
}

mpMatchViewAllowedOperationMask_t MPMatchViewAllOperationBits( void ) {
	return ( 1ull << static_cast<unsigned int>( MP_MATCH_OP_COUNT ) ) - 2ull;
}

bool MPMatchViewSetOperationDecision( mpMatchViewPublicState_t &state,
	mpMatchOperationOpcode_t opcode, mpMatchProtocolReason_t reason,
	unsigned char fieldId, unsigned int detail ) {
	if ( !IsValidOpcode( opcode ) ||
		state.operationAvailabilityCount != MP_MATCH_OP_COUNT - 1 ||
		( reason != MP_MATCH_PROTOCOL_REASON_OK &&
			( reason <= MP_MATCH_PROTOCOL_REASON_OK ||
				reason >= MP_MATCH_PROTOCOL_REASON_COUNT ) ) ||
		( reason == MP_MATCH_PROTOCOL_REASON_OK && ( fieldId != 0 || detail != 0 ) ) ) {
		return false;
	}
	mpMatchViewOperationAvailability_t &availability =
		state.operationAvailability[ static_cast<int>( opcode ) - 1 ];
	if ( availability.opcode != opcode ) {
		return false;
	}
	availability.available = reason == MP_MATCH_PROTOCOL_REASON_OK;
	availability.reason = reason;
	availability.localizationId = MPMatchProtocolReasonLocalizationId( reason );
	availability.fieldId = fieldId;
	availability.detail = detail;
	const mpMatchViewAllowedOperationMask_t bit = MPMatchViewOperationBit( opcode );
	if ( availability.available ) {
		state.allowedOperations |= bit;
	} else {
		state.allowedOperations &= ~bit;
	}
	return true;
}

bool MPMatchViewValidate( const mpSessionView &view, mpMatchViewError_t *error ) {
	ClearError( error );
	if ( !ValidatePublicState( view.publicState, error ) ||
		!ValidateProposal( view.ownSideProposal, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ||
		!ValidateStagedRules( view.stagedRules, view.publicState.committedRules, error ) ) {
		return false;
	}
	if ( view.ownSideProposal.present &&
		( view.ownSideProposal.scope != MP_MATCH_VIEW_PROPOSAL_SIDE ||
			view.ownSideProposal.side != view.publicState.recipient.side ||
			view.ownSideProposal.expiresAtEngineMsec <=
				view.publicState.clocks.engineTimeMsec ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_PROPOSALS );
		return false;
	}
	if ( view.rosterSeatCount > MP_MATCH_VIEW_MAX_ROSTER_SEATS ||
		view.invitationCount > MP_MATCH_VIEW_MAX_INVITATIONS ||
		view.queueEntryCount > MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ||
		view.teamVitalCount > MP_MATCH_VIEW_MAX_TEAM_VITALS ||
		view.itemTimingCount > MP_MATCH_VIEW_MAX_ITEM_TIMINGS ||
		view.followTargetCount > MP_MATCH_VIEW_MAX_FOLLOW_TARGETS ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT );
		return false;
	}
	for ( int i = 0; i < view.rosterSeatCount; ++i ) {
		if ( !ValidateRosterSeat( view.rosterSeats[ i ], i, error ) ) {
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( ( view.rosterSeats[ prior ].side == view.rosterSeats[ i ].side &&
				view.rosterSeats[ prior ].seatIndex == view.rosterSeats[ i ].seatIndex ) ||
				( view.rosterSeats[ i ].occupied && view.rosterSeats[ prior ].occupied &&
					view.rosterSeats[ prior ].participantId ==
						view.rosterSeats[ i ].participantId ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_ROSTER_SEATS, i );
				return false;
			}
		}
	}
	for ( int i = 0; i < view.invitationCount; ++i ) {
		if ( !ValidateInvitation( view.invitations[ i ], i, error ) ) {
			return false;
		}
		if ( view.invitations[ i ].expiresAtEngineMsec <=
			view.publicState.clocks.engineTimeMsec ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_INVITATIONS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( view.invitations[ prior ].invitationId == view.invitations[ i ].invitationId ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_INVITATIONS, i );
				return false;
			}
		}
	}
	bool foundRecipientQueueEntry = view.publicState.recipient.queueState ==
		MP_MATCH_VIEW_QUEUE_NONE || view.publicState.recipient.queueState ==
		MP_MATCH_VIEW_QUEUE_ADMITTED;
	for ( int i = 0; i < view.queueEntryCount; ++i ) {
		if ( !ValidateQueueEntry( view.queueEntries[ i ], i, error ) ) {
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( view.queueEntries[ prior ].participantId == view.queueEntries[ i ].participantId ||
				( view.queueEntries[ prior ].side == view.queueEntries[ i ].side &&
					view.queueEntries[ prior ].position == view.queueEntries[ i ].position ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_QUEUES, i );
				return false;
			}
		}
		if ( view.queueEntries[ i ].participantId == view.publicState.recipient.participantId ) {
			foundRecipientQueueEntry =
				view.queueEntries[ i ].side == view.publicState.recipient.queueSide &&
				view.queueEntries[ i ].position == view.publicState.recipient.queuePosition &&
				view.queueEntries[ i ].state == view.publicState.recipient.queueState;
		}
	}
	if ( !foundRecipientQueueEntry ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE, MP_MATCH_VIEW_FIELD_QUEUES );
		return false;
	}
	for ( int i = 0; i < view.teamVitalCount; ++i ) {
		const mpMatchViewTeamVital_t &vital = view.teamVitals[ i ];
		if ( vital.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
			!IsSide( vital.participantSide ) || vital.health > 999 || vital.armor > 999 ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_TEAM_VITALS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( view.teamVitals[ prior ].participantId == vital.participantId ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_TEAM_VITALS, i );
				return false;
			}
		}
	}
	for ( int i = 0; i < view.itemTimingCount; ++i ) {
		const mpMatchViewItemTiming_t &item = view.itemTimings[ i ];
		if ( !IsTime( item.matchDeadlineMsec ) ||
			!IsMachineToken( item.token, item.tokenLength, MP_MATCH_VIEW_ITEM_TOKEN_BYTES ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STRING,
				MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( SameToken( view.itemTimings[ prior ].token,
				view.itemTimings[ prior ].tokenLength, item.token, item.tokenLength ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, i );
				return false;
			}
		}
	}
	for ( int i = 0; i < view.followTargetCount; ++i ) {
		const mpMatchViewFollowTarget_t &target = view.followTargets[ i ];
		if ( target.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
			!IsSide( target.participantSide ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_STATE,
				MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, i );
			return false;
		}
		for ( int prior = 0; prior < i; ++prior ) {
			if ( view.followTargets[ prior ].participantId == target.participantId ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, i );
				return false;
			}
		}
	}
	return true;
}

bool MPMatchViewBuild( const mpMatchViewSource_t &source,
	const mpMatchViewRecipientPolicy_t &policy, mpSessionView &view,
	mpMatchViewError_t *error ) {
	ClearError( error );
	const mpMatchViewAudienceMask_t knownAudiences =
		( 1u << static_cast<unsigned int>( MP_MATCH_VIEW_AUDIENCE_COUNT ) ) - 1u;
	const mpMatchViewObserverKindMask_t knownKinds =
		( 1u << static_cast<unsigned int>( MP_MATCH_VIEW_OBSERVER_KIND_COUNT ) ) - 1u;
	if ( ( policy.audiences & ~knownAudiences ) != 0 ||
		( policy.observerKinds & ~knownKinds ) != 0 ||
		( policy.audiences & MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_PUBLIC ) ) == 0 ||
		( policy.audiences & MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) ) == 0 ||
		policy.recipientId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		policy.recipientId != source.publicState.recipient.participantId ||
		( ( policy.audiences & MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ) ) != 0 ?
			!IsSide( policy.ownSide ) : policy.ownSide != MP_MATCH_VIEW_SIDE_NONE ) ||
		( IsSide( source.publicState.recipient.side ) ?
			policy.ownSide != source.publicState.recipient.side :
			( policy.audiences & MPMatchViewAudienceBit(
				MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ) ) != 0 ) ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY );
		return false;
	}
	if ( source.proposalCandidateCount > MP_MATCH_VIEW_SIDE_COUNT ||
		source.stagedRulesCandidateCount > 4 ||
		source.rosterSeatCandidateCount > MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ||
		source.invitationCandidateCount > MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ||
		source.queueEntryCandidateCount > MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ||
		source.observerCandidateCount > MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_INVALID_COUNT );
		return false;
	}
	mpSessionView built;
	built.Clear();
	built.publicState = source.publicState;
	if ( !ValidatePublicState( built.publicState, error ) ) {
		return false;
	}
	// Validate every candidate before filtering so malformed values cannot hide
	// behind an audience the current recipient does not possess.
	for ( int i = 0; i < source.proposalCandidateCount; ++i ) {
		const mpMatchViewProposalCandidate_t &candidate = source.proposalCandidates[ i ];
		if ( !ValidateAuthorizationTag( candidate.authorization, i, error ) ||
			!ValidateProposal( candidate.value, MP_MATCH_VIEW_FIELD_PROPOSALS, error ) ) {
			return false;
		}
		if ( candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ||
			!candidate.value.present || candidate.value.scope != MP_MATCH_VIEW_PROPOSAL_SIDE ||
			candidate.value.side != candidate.authorization.audienceSide ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY,
				MP_MATCH_VIEW_FIELD_PROPOSALS, i );
			return false;
		}
	}
	for ( int i = 0; i < source.stagedRulesCandidateCount; ++i ) {
		const mpMatchViewStagedRulesCandidate_t &candidate =
			source.stagedRulesCandidates[ i ];
		if ( !ValidateAuthorizationTag( candidate.authorization, i, error ) ||
			!ValidateStagedRules( candidate.value,
				source.publicState.committedRules, error ) ) {
			return false;
		}
		if ( ( candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) ||
			!candidate.value.present ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY,
				MP_MATCH_VIEW_FIELD_RULES, i );
			return false;
		}
	}
	for ( int i = 0; i < source.rosterSeatCandidateCount; ++i ) {
		const mpMatchViewRosterSeatCandidate_t &candidate = source.rosterSeatCandidates[ i ];
		if ( !ValidateAuthorizationTag( candidate.authorization, i, error ) ||
			!ValidateRosterSeat( candidate.value, i, error ) ) {
			return false;
		}
		if ( ( candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) ||
			( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
				candidate.value.side != candidate.authorization.audienceSide ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY,
				MP_MATCH_VIEW_FIELD_ROSTER_SEATS, i );
			return false;
		}
	}
	for ( int i = 0; i < source.invitationCandidateCount; ++i ) {
		const mpMatchViewInvitationCandidate_t &candidate = source.invitationCandidates[ i ];
		if ( !ValidateAuthorizationTag( candidate.authorization, i, error ) ||
			!ValidateInvitation( candidate.value, i, error ) ) {
			return false;
		}
		if ( ( candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) ||
			( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
				candidate.value.side != candidate.authorization.audienceSide ) ||
			( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_RECIPIENT &&
				candidate.value.inviteeParticipantId !=
					candidate.authorization.audienceParticipantId ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY,
				MP_MATCH_VIEW_FIELD_INVITATIONS, i );
			return false;
		}
	}
	for ( int i = 0; i < source.queueEntryCandidateCount; ++i ) {
		const mpMatchViewQueueEntryCandidate_t &candidate = source.queueEntryCandidates[ i ];
		if ( !ValidateAuthorizationTag( candidate.authorization, i, error ) ||
			!ValidateQueueEntry( candidate.value, i, error ) ) {
			return false;
		}
		if ( ( candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			candidate.authorization.audience != MP_MATCH_VIEW_AUDIENCE_RECIPIENT ) ||
			( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
				candidate.value.side != candidate.authorization.audienceSide ) ||
			( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_RECIPIENT &&
				candidate.value.participantId !=
					candidate.authorization.audienceParticipantId ) ) {
			SetError( error, MP_MATCH_VIEW_ERROR_INVALID_POLICY,
				MP_MATCH_VIEW_FIELD_QUEUES, i );
			return false;
		}
	}
	for ( int i = 0; i < source.observerCandidateCount; ++i ) {
		if ( !ValidateObserverCandidate( source.observerCandidates[ i ], i, error ) ) {
			return false;
		}
	}
	for ( int i = 0; i < source.proposalCandidateCount; ++i ) {
		const mpMatchViewProposalCandidate_t &candidate = source.proposalCandidates[ i ];
		if ( CandidateAuthorized( candidate.authorization, policy ) ) {
			if ( built.ownSideProposal.present &&
				!SameProposal( built.ownSideProposal, candidate.value ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_PROPOSALS, i );
				return false;
			}
			built.ownSideProposal = candidate.value;
		}
	}
	for ( int i = 0; i < source.stagedRulesCandidateCount; ++i ) {
		const mpMatchViewStagedRulesCandidate_t &candidate =
			source.stagedRulesCandidates[ i ];
		if ( CandidateAuthorized( candidate.authorization, policy ) ) {
			if ( built.stagedRules.present && !SameStagedRules( built.stagedRules,
				candidate.value ) ) {
				SetError( error, MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
					MP_MATCH_VIEW_FIELD_RULES, i );
				return false;
			}
			built.stagedRules = candidate.value;
		}
	}
	for ( int i = 0; i < source.rosterSeatCandidateCount; ++i ) {
		const mpMatchViewRosterSeatCandidate_t &candidate = source.rosterSeatCandidates[ i ];
		if ( CandidateAuthorized( candidate.authorization, policy ) &&
			!AddRosterSeat( built, candidate.value, i, error ) ) {
			return false;
		}
	}
	for ( int i = 0; i < source.invitationCandidateCount; ++i ) {
		const mpMatchViewInvitationCandidate_t &candidate = source.invitationCandidates[ i ];
		if ( CandidateAuthorized( candidate.authorization, policy ) &&
			!AddInvitation( built, candidate.value, i, error ) ) {
			return false;
		}
	}
	for ( int i = 0; i < source.queueEntryCandidateCount; ++i ) {
		const mpMatchViewQueueEntryCandidate_t &candidate = source.queueEntryCandidates[ i ];
		if ( CandidateAuthorized( candidate.authorization, policy ) &&
			!AddQueueEntry( built, candidate.value, i, error ) ) {
			return false;
		}
	}
	for ( int i = 0; i < source.observerCandidateCount; ++i ) {
		const mpMatchViewObserverCandidate_t &candidate = source.observerCandidates[ i ];
		if ( ( policy.observerKinds & MPMatchViewObserverKindBit( candidate.kind ) ) != 0 &&
			CandidateAuthorized( candidate.authorization, policy ) &&
			!AddAuthorizedObserver( built, candidate, i, error ) ) {
			return false;
		}
	}
	if ( !MPMatchViewValidate( built, error ) ) {
		return false;
	}
	view = built;
	return true;
}

namespace {

static void WriteLifecycleField( idBitMsg &field, const mpMatchViewLifecycle_t &value ) {
	field.WriteByte( value.phase );
	field.WriteByte( value.round );
	field.WriteByte( value.pauseState );
	field.WriteByte( value.pauseKind );
	field.WriteByte( value.pauseReason );
	field.WriteByte( SideToWire( value.pauseOwnerSide ) );
	field.WriteByte( value.hasPauseExpiry ? 1 : 0 );
	WriteUInt64Value( field, value.pauseExpiryEngineMsec );
	field.WriteByte( value.hasResumeDeadline ? 1 : 0 );
	WriteUInt64Value( field, value.resumeDeadlineEngineMsec );
	field.WriteByte( value.resumePolicy );
	field.WriteByte( value.resumeRequiredSideMask );
	field.WriteByte( value.resumeConsentingSideMask );
}

static void WriteClocksField( idBitMsg &field, const mpMatchViewClocks_t &value ) {
	WriteUInt64Value( field, value.engineTimeMsec );
	WriteUInt64Value( field, value.matchTimeMsec );
	field.WriteLong( static_cast<int>( value.livePeriod ) );
	field.WriteByte( value.isOvertime ? 1 : 0 );
	field.WriteByte( value.hasLiveDeadline ? 1 : 0 );
	WriteUInt64Value( field, value.liveDeadlineMatchMsec );
}

static void WriteReadinessField( idBitMsg &field, const mpMatchViewReadiness_t &value ) {
	field.WriteLong( static_cast<int>( value.blockers ) );
	field.WriteUShort( value.readyCount );
	field.WriteUShort( value.eligibleCount );
	field.WriteUShort( value.activeHumans );
	field.WriteUShort( value.vacantRequiredSeats );
}

static void WriteTimeoutsField( idBitMsg &field,
	const mpMatchViewTimeoutBudget_t *budgets ) {
	for ( int side = 0; side < MP_MATCH_VIEW_SIDE_COUNT; ++side ) {
		field.WriteByte( budgets[ side ].configured );
		field.WriteByte( budgets[ side ].remaining );
		field.WriteByte( budgets[ side ].consumed );
		field.WriteUShort( budgets[ side ].durationSeconds );
	}
}

static void WriteRolesField( idBitMsg &field, const mpMatchViewPublicState_t &state ) {
	field.WriteByte( state.roleSummaryCount );
	for ( int i = 0; i < state.roleSummaryCount; ++i ) {
		field.WriteByte( state.roleSummaries[ i ].role );
		field.WriteByte( SideToWire( state.roleSummaries[ i ].side ) );
		field.WriteByte( state.roleSummaries[ i ].count );
	}
}

static void WriteTeamsField( idBitMsg &field, const mpMatchViewPublicState_t &state ) {
	field.WriteByte( state.rosterSummaryCount );
	for ( int i = 0; i < state.rosterSummaryCount; ++i ) {
		const mpMatchViewRosterSummary_t &summary = state.rosterSummaries[ i ];
		field.WriteByte( SideToWire( summary.side ) );
		field.WriteByte( summary.declaredSeats );
		field.WriteByte( summary.occupiedSeats );
		field.WriteByte( summary.connectedOccupants );
		field.WriteByte( summary.readyOccupants );
		field.WriteByte( summary.activeParticipants );
		field.WriteByte( summary.queueDepth );
		field.WriteByte( summary.teamReady ? 1 : 0 );
		field.WriteByte( summary.locked ? 1 : 0 );
	}
}

static void WriteProposal( idBitMsg &field,
	const mpMatchViewProposalSummary_t &proposal ) {
	field.WriteByte( proposal.present ? 1 : 0 );
	if ( !proposal.present ) {
		return;
	}
	field.WriteLong( static_cast<int>( proposal.proposalId ) );
	field.WriteByte( proposal.opcode );
	field.WriteByte( proposal.scope );
	field.WriteByte( SideToWire( proposal.side ) );
	field.WriteLong( static_cast<int>( proposal.callerParticipantId ) );
	field.WriteUShort( proposal.yesCount );
	field.WriteUShort( proposal.noCount );
	field.WriteUShort( proposal.abstainCount );
	field.WriteUShort( proposal.castCount );
	field.WriteUShort( proposal.eligibleCount );
	field.WriteUShort( proposal.requiredQuorumCount );
	field.WriteUShort( proposal.requiredYesCount );
	WriteUInt64Value( field, proposal.expiresAtEngineMsec );
	field.WriteByte( proposal.recipientEligible ? 1 : 0 );
	field.WriteByte( proposal.recipientBallot );
}

static void WriteProposalsField( idBitMsg &field, const mpSessionView &view ) {
	WriteProposal( field, view.publicState.globalProposal );
	WriteProposal( field, view.ownSideProposal );
}

static void WriteSeriesField( idBitMsg &field,
	const mpMatchViewSeriesSummary_t &series ) {
	field.WriteByte( series.present ? 1 : 0 );
	if ( !series.present ) {
		return;
	}
	WriteUInt64Value( field, series.seriesId );
	field.WriteByte( series.state );
	WriteUInt64Value( field, series.revision );
	field.WriteLong( series.gameType );
	field.WriteByte( series.bestOf );
	field.WriteByte( series.currentMapNumber );
	field.WriteByte( series.wins[ 0 ] );
	field.WriteByte( series.wins[ 1 ] );
	field.WriteByte( series.hasNextMap ? 1 : 0 );
	if ( series.hasNextMap ) {
		field.WriteByte( series.nextMapLength );
		field.WriteData( series.nextMap, series.nextMapLength );
	}
	field.WriteByte( series.currentVetoStep );
	field.WriteByte( series.vetoStepCount );
	field.WriteByte( series.hasVetoTurn ? 1 : 0 );
	if ( series.hasVetoTurn ) {
		field.WriteByte( series.vetoTurnAction );
		field.WriteByte( SideToWire( series.vetoTurnSide ) );
	}
	field.WriteByte( series.mapPoolCount );
	for ( int i = 0; i < series.mapPoolCount; ++i ) {
		const mpMatchViewSeriesMap_t &map = series.mapPool[ i ];
		field.WriteByte( map.poolIndex );
		field.WriteByte( map.disposition );
		field.WriteByte( SideToWire( map.selectedBySide ) );
		field.WriteByte( map.selectionNumber );
		field.WriteByte( map.decider ? 1 : 0 );
		field.WriteByte( map.hasStartingGameSide ? 1 : 0 );
		field.WriteByte( SideToWire( map.startingGameSide ) );
		field.WriteByte( SideToWire( map.gameSideChosenBy ) );
		field.WriteByte( map.tokenLength );
		field.WriteData( map.mapToken, map.tokenLength );
	}
	field.WriteByte( series.vetoHistoryCount );
	for ( int i = 0; i < series.vetoHistoryCount; ++i ) {
		const mpMatchViewVetoHistory_t &entry = series.vetoHistory[ i ];
		field.WriteByte( entry.sequenceNumber );
		field.WriteByte( entry.action );
		field.WriteByte( SideToWire( entry.actingSide ) );
		field.WriteByte( entry.mapPoolIndex );
		field.WriteByte( entry.hasSelectedGameSide ? 1 : 0 );
		if ( entry.hasSelectedGameSide ) {
			field.WriteByte( SideToWire( entry.selectedGameSide ) );
		}
	}
	field.WriteByte( series.mapHistoryCount );
	for ( int i = 0; i < series.mapHistoryCount; ++i ) {
		const mpMatchViewSeriesMapHistory_t &entry = series.mapHistory[ i ];
		field.WriteByte( entry.attemptNumber );
		field.WriteByte( entry.mapPoolIndex );
		field.WriteByte( entry.outcome );
		field.WriteByte( SideToWire( entry.winnerSide ) );
		field.WriteUShort( entry.scores[ 0 ] );
		field.WriteUShort( entry.scores[ 1 ] );
	}
}

static void WriteOperationAvailabilityField( idBitMsg &field,
	const mpMatchViewPublicState_t &state ) {
	WriteUInt64Value( field, state.allowedOperations );
	field.WriteByte( state.operationAvailabilityCount );
	for ( int i = 0; i < state.operationAvailabilityCount; ++i ) {
		const mpMatchViewOperationAvailability_t &availability =
			state.operationAvailability[ i ];
		field.WriteByte( availability.opcode );
		field.WriteByte( availability.available ? 1 : 0 );
		field.WriteByte( availability.reason );
		field.WriteUShort( availability.localizationId );
		field.WriteByte( availability.fieldId );
		field.WriteLong( static_cast<int>( availability.detail ) );
	}
}

static void WriteDenialField( idBitMsg &field, const mpMatchViewDenial_t &denial ) {
	field.WriteByte( denial.present ? 1 : 0 );
	if ( !denial.present ) {
		return;
	}
	field.WriteByte( denial.opcode );
	field.WriteByte( denial.reason );
	field.WriteUShort( denial.localizationId );
	field.WriteByte( denial.fieldId );
	field.WriteLong( static_cast<int>( denial.detail ) );
}

static void WriteVitalsField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.teamVitalCount );
	for ( int i = 0; i < view.teamVitalCount; ++i ) {
		field.WriteLong( static_cast<int>( view.teamVitals[ i ].participantId ) );
		field.WriteByte( SideToWire( view.teamVitals[ i ].participantSide ) );
		field.WriteUShort( view.teamVitals[ i ].health );
		field.WriteUShort( view.teamVitals[ i ].armor );
		field.WriteByte( view.teamVitals[ i ].alive ? 1 : 0 );
	}
}

static void WriteItemsField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.itemTimingCount );
	for ( int i = 0; i < view.itemTimingCount; ++i ) {
		field.WriteByte( view.itemTimings[ i ].available ? 1 : 0 );
		WriteUInt64Value( field, view.itemTimings[ i ].matchDeadlineMsec );
		field.WriteByte( view.itemTimings[ i ].tokenLength );
		field.WriteData( view.itemTimings[ i ].token, view.itemTimings[ i ].tokenLength );
	}
}

static void WriteFollowTargetsField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.followTargetCount );
	for ( int i = 0; i < view.followTargetCount; ++i ) {
		field.WriteLong( static_cast<int>( view.followTargets[ i ].participantId ) );
		field.WriteByte( SideToWire( view.followTargets[ i ].participantSide ) );
		field.WriteByte( view.followTargets[ i ].selectable ? 1 : 0 );
	}
}

static void WriteRecipientField( idBitMsg &field,
	const mpMatchViewRecipient_t &recipient ) {
	field.WriteLong( static_cast<int>( recipient.participantId ) );
	field.WriteByte( recipient.slot );
	field.WriteLong( static_cast<int>( recipient.bindingGeneration ) );
	field.WriteByte( SideToWire( recipient.side ) );
	field.WriteByte( SideToWire( recipient.competitionSide ) );
	field.WriteLong( static_cast<int>( recipient.publicRoleMask ) );
	field.WriteByte( recipient.ready ? 1 : 0 );
	field.WriteByte( recipient.active ? 1 : 0 );
	field.WriteByte( recipient.readyEligible ? 1 : 0 );
	field.WriteByte( recipient.queueState );
	field.WriteByte( SideToWire( recipient.queueSide ) );
	field.WriteByte( recipient.hasQueuePosition ? 1 : 0 );
	field.WriteByte( recipient.queuePosition );
	field.WriteByte( recipient.resumeConsented ? 1 : 0 );
}

static void WriteEvidenceField( idBitMsg &field,
	const mpMatchViewEvidenceSummary_t &evidence ) {
	field.WriteByte( evidence.evidenceState );
	field.WriteByte( evidence.mvdState );
	field.WriteByte( evidence.reportState );
	WriteUInt64Value( field, evidence.evidenceRevision );
	field.WriteUShort( evidence.eventCount );
	field.WriteLong( static_cast<int>( evidence.droppedRecordCount ) );
	field.WriteByte( evidence.droppedRecordCountSaturated ? 1 : 0 );
	field.WriteByte( evidence.participantStatsCount );
	field.WriteByte( evidence.teamStatsCount );
	field.WriteByte( evidence.resultRecorded ? 1 : 0 );
	field.WriteByte( evidence.recentEventCount );
	for ( int i = 0; i < evidence.recentEventCount; ++i ) {
		field.WriteByte( evidence.recentEventKinds[ i ] );
	}
}

static void WriteParticipantsField( idBitMsg &field,
	const mpMatchViewPublicState_t &state ) {
	field.WriteByte( state.participantSummaryCount );
	for ( int i = 0; i < state.participantSummaryCount; ++i ) {
		const mpMatchViewParticipantSummary_t &participant = state.participantSummaries[ i ];
		field.WriteLong( static_cast<int>( participant.participantId ) );
		field.WriteByte( participant.slot );
		field.WriteByte( SideToWire( participant.side ) );
		field.WriteLong( static_cast<int>( participant.publicRoleMask ) );
		unsigned char flags = 0;
		if ( participant.connected ) {
			flags |= MP_MATCH_VIEW_PARTICIPANT_CONNECTED_BIT;
		}
		if ( participant.human ) {
			flags |= MP_MATCH_VIEW_PARTICIPANT_HUMAN_BIT;
		}
		if ( participant.active ) {
			flags |= MP_MATCH_VIEW_PARTICIPANT_ACTIVE_BIT;
		}
		field.WriteByte( flags );
	}
}

static void WriteRulesField( idBitMsg &field, const mpSessionView &view ) {
	const mpMatchViewCommittedRules_t &committed = view.publicState.committedRules;
	field.WriteByte( committed.present ? 1 : 0 );
	if ( committed.present ) {
		field.WriteLong( static_cast<int>( committed.rulesSchemaVersion ) );
		field.WriteLong( static_cast<int>( committed.revision ) );
		WriteUInt64Value( field, committed.digest );
		field.WriteLong( committed.profileId );
		field.WriteByte( committed.customized ? 1 : 0 );
		field.WriteByte( committed.boundary );
		field.WriteByte( committed.valueCount );
		for ( int i = 0; i < committed.valueCount; ++i ) {
			field.WriteByte( committed.values[ i ].fieldId );
			field.WriteByte( committed.values[ i ].type );
			field.WriteLong( committed.values[ i ].value );
			field.WriteByte( committed.values[ i ].editable ? 1 : 0 );
		}
	}
	const mpMatchViewStagedRules_t &staged = view.stagedRules;
	field.WriteByte( staged.present ? 1 : 0 );
	if ( staged.present ) {
		field.WriteLong( static_cast<int>( staged.revision ) );
		WriteUInt64Value( field, staged.digest );
		field.WriteLong( staged.profileId );
		field.WriteByte( staged.customized ? 1 : 0 );
		WriteUInt64Value( field, staged.changedFieldMask );
		field.WriteByte( staged.valueCount );
		for ( int i = 0; i < staged.valueCount; ++i ) {
			field.WriteByte( staged.values[ i ].fieldId );
			field.WriteByte( staged.values[ i ].type );
			field.WriteLong( staged.values[ i ].value );
		}
	}
}

static void WriteRosterSeatsField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.rosterSeatCount );
	for ( int i = 0; i < view.rosterSeatCount; ++i ) {
		const mpMatchViewRosterSeat_t &seat = view.rosterSeats[ i ];
		field.WriteByte( seat.seatIndex );
		field.WriteByte( SideToWire( seat.side ) );
		field.WriteByte( seat.role );
		field.WriteByte( seat.required ? 1 : 0 );
		field.WriteByte( seat.occupied ? 1 : 0 );
		field.WriteLong( static_cast<int>( seat.participantId ) );
		field.WriteByte( seat.connected ? 1 : 0 );
		field.WriteByte( seat.ready ? 1 : 0 );
		field.WriteByte( seat.active ? 1 : 0 );
	}
}

static void WriteInvitationsField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.invitationCount );
	for ( int i = 0; i < view.invitationCount; ++i ) {
		const mpMatchViewInvitationSummary_t &invitation = view.invitations[ i ];
		field.WriteLong( static_cast<int>( invitation.invitationId ) );
		field.WriteByte( SideToWire( invitation.side ) );
		field.WriteByte( invitation.role );
		field.WriteLong( static_cast<int>( invitation.inviterParticipantId ) );
		field.WriteLong( static_cast<int>( invitation.inviteeParticipantId ) );
		WriteUInt64Value( field, invitation.expiresAtEngineMsec );
	}
}

static void WriteQueuesField( idBitMsg &field, const mpSessionView &view ) {
	field.WriteByte( view.queueEntryCount );
	for ( int i = 0; i < view.queueEntryCount; ++i ) {
		const mpMatchViewQueueEntry_t &entry = view.queueEntries[ i ];
		field.WriteLong( static_cast<int>( entry.participantId ) );
		field.WriteByte( SideToWire( entry.side ) );
		field.WriteByte( entry.position );
		field.WriteByte( entry.state );
	}
}

static bool BuildPayload( const mpSessionView &view, byte *encoded,
	int &encodedLength, mpMatchViewError_t *error ) {
	idBitMsg payload;
	payload.Init( encoded, MP_MATCH_VIEW_MAX_PAYLOAD_BYTES );
	payload.SetAllowOverflow( true );
	payload.BeginWriting();
	payload.WriteByte( MP_MATCH_VIEW_REQUIRED_FIELD_COUNT );
	byte fieldStorage[ MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ];
	idBitMsg field;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	field.WriteUShort( view.publicState.schemaVersion );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_SCHEMA, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteUInt64Value( field, view.publicState.sessionRevision );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_REVISION, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteLifecycleField( field, view.publicState.lifecycle );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_LIFECYCLE, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteClocksField( field, view.publicState.clocks );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_CLOCKS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteReadinessField( field, view.publicState.readiness );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_READINESS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteTimeoutsField( field, view.publicState.timeoutBudgets );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_TIMEOUTS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteRolesField( field, view.publicState );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_ROLES, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteTeamsField( field, view.publicState );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_TEAMS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteProposalsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_PROPOSALS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteSeriesField( field, view.publicState.series );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_SERIES, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteOperationAvailabilityField( field, view.publicState );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_OPERATION_AVAILABILITY, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteDenialField( field, view.publicState.denial );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_DENIAL, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteVitalsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_TEAM_VITALS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteItemsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_ITEM_TIMINGS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteFollowTargetsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteRecipientField( field, view.publicState.recipient );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_RECIPIENT, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteUInt64Value( field, view.publicState.viewRevision );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_VIEW_REVISION, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteParticipantsField( field, view.publicState );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_PARTICIPANTS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteRulesField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_RULES, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteRosterSeatsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_ROSTER_SEATS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteInvitationsField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_INVITATIONS, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteQueuesField( field, view );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_QUEUES, field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteUInt64Value( field, view.publicState.controlRevision );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_CONTROL_REVISION,
		field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	WriteEvidenceField( field, view.publicState.evidence );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_EVIDENCE,
		field, error ) ) return false;

	BeginFieldWrite( field, fieldStorage, sizeof( fieldStorage ) );
	const mpMatchViewTerminalResult_t &result = view.publicState.terminalResult;
	field.WriteByte( result.outcome );
	field.WriteByte( result.reason );
	WriteUInt64Value( field, result.resultRevision );
	field.WriteByte( SideToWire( result.winnerSide ) );
	field.WriteLong( static_cast<int>( result.winnerParticipantId ) );
	field.WriteByte( result.winnerNameLength );
	field.WriteData( result.winnerName, result.winnerNameLength );
	if ( !AppendField( payload, MP_MATCH_VIEW_FIELD_TERMINAL_RESULT, field, error ) ) return false;

	if ( payload.IsOverflowed() || payload.GetSize() > MP_MATCH_VIEW_MAX_PAYLOAD_BYTES ) {
		SetError( error, MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE, 0,
			static_cast<unsigned int>( payload.GetSize() ) );
		return false;
	}
	encodedLength = payload.GetSize();
	return true;
}

} // namespace
