//----------------------------------------------------------------
// MatchEvidence.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_EVIDENCE_STANDALONE_TEST )
	#include "MatchEvidence.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchEvidence.h"
#endif

#include <string.h>

namespace {

static bool IsEvidenceSide( int side ) {
	return side >= 0 && side < MP_MATCH_EVIDENCE_MAX_TEAMS;
}

static bool IsEvidenceSideOrNone( int side ) {
	return side == -1 || IsEvidenceSide( side );
}

static bool ValidateActor( const mpEvidenceActorRef &actor ) {
	if ( actor.kind < MP_EVIDENCE_ACTOR_SYSTEM ||
		actor.kind >= MP_EVIDENCE_ACTOR_KIND_COUNT ) {
		return false;
	}
	return actor.kind == MP_EVIDENCE_ACTOR_PARTICIPANT ?
		actor.participantSequence != 0 : actor.participantSequence == 0;
}

static bool ValidateUtf8( const char *text, int length ) {
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>( text );
	int index = 0;
	while ( index < length ) {
		const unsigned char first = bytes[ index++ ];
		if ( first < 0x80 ) {
			continue;
		}

		int continuationCount = 0;
		uint32_t value = 0;
		uint32_t minimum = 0;
		if ( first >= 0xC2 && first <= 0xDF ) {
			continuationCount = 1;
			value = first & 0x1F;
			minimum = 0x80;
		} else if ( first >= 0xE0 && first <= 0xEF ) {
			continuationCount = 2;
			value = first & 0x0F;
			minimum = 0x800;
		} else if ( first >= 0xF0 && first <= 0xF4 ) {
			continuationCount = 3;
			value = first & 0x07;
			minimum = 0x10000;
		} else {
			return false;
		}
		if ( index + continuationCount > length ) {
			return false;
		}
		for ( int continuation = 0; continuation < continuationCount; ++continuation ) {
			const unsigned char byte = bytes[ index++ ];
			if ( ( byte & 0xC0 ) != 0x80 ) {
				return false;
			}
			value = ( value << 6 ) | ( byte & 0x3F );
		}
		if ( value < minimum || value > 0x10FFFF ||
			( value >= 0xD800 && value <= 0xDFFF ) ) {
			return false;
		}
	}
	return true;
}

static bool BoundedTextLength( const char *text, int maximumBytes,
		bool allowEmpty, int &length ) {
	length = 0;
	if ( text == NULL || maximumBytes < 0 ) {
		return false;
	}
	while ( length <= maximumBytes && text[ length ] != '\0' ) {
		++length;
	}
	if ( length > maximumBytes || ( !allowEmpty && length == 0 ) ) {
		return false;
	}
	return ValidateUtf8( text, length );
}

static bool CopyEvidenceText( const char *source, char *destination,
		int maximumBytes, bool allowEmpty ) {
	int length = 0;
	if ( !BoundedTextLength( source, maximumBytes, allowEmpty, length ) ) {
		return false;
	}
	if ( length > 0 ) {
		memcpy( destination, source, static_cast<size_t>( length ) );
	}
	destination[ length ] = '\0';
	return true;
}

static bool IsArtifactQPathCharacter( unsigned char value ) {
	return ( value >= 'a' && value <= 'z' ) ||
		( value >= 'A' && value <= 'Z' ) ||
		( value >= '0' && value <= '9' ) || value == '-' || value == '_' ||
		value == '.' || value == '/';
}

static bool IsDotComponent( const char *qpath, int start, int length ) {
	return ( length == 1 && qpath[ start ] == '.' ) ||
		( length == 2 && qpath[ start ] == '.' && qpath[ start + 1 ] == '.' );
}

static bool ValidateArtifactQPathInternal( mpEvidenceArtifactKind_t kind,
		const char *qpath ) {
	if ( kind != MP_EVIDENCE_ARTIFACT_MVD ) {
		return false;
	}
	int length = 0;
	if ( !BoundedTextLength( qpath,
			MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES, false, length ) ) {
		return false;
	}
	static const char root[] = "demos/";
	static const char extension[] = ".mvd";
	const int rootBytes = static_cast<int>( sizeof( root ) ) - 1;
	const int extensionBytes = static_cast<int>( sizeof( extension ) ) - 1;
	if ( length <= rootBytes + extensionBytes ||
		memcmp( qpath, root, static_cast<size_t>( rootBytes ) ) != 0 ||
		memcmp( qpath + length - extensionBytes, extension,
			static_cast<size_t>( extensionBytes ) ) != 0 ) {
		return false;
	}

	int componentStart = rootBytes;
	for ( int index = componentStart; index < length; ++index ) {
		const unsigned char value = static_cast<unsigned char>( qpath[ index ] );
		if ( !IsArtifactQPathCharacter( value ) ) {
			return false;
		}
		if ( value != '/' ) {
			continue;
		}
		const int componentLength = index - componentStart;
		if ( componentLength == 0 ||
			IsDotComponent( qpath, componentStart, componentLength ) ) {
			return false;
		}
		componentStart = index + 1;
	}
	const int fileBytes = length - componentStart;
	return fileBytes > extensionBytes &&
		!IsDotComponent( qpath, componentStart, fileBytes );
}

static bool ValidatePhaseEvent( const mpEvidencePhaseTransition &event ) {
	return event.from >= INACTIVE && event.from < STATE_COUNT &&
		event.to >= INACTIVE && event.to < STATE_COUNT &&
		event.from != event.to && event.reason != 0 && ValidateActor( event.actor );
}

static bool ValidateRoundEvent( const mpEvidenceRoundTransition &event ) {
	return event.from >= RS_INACTIVE && event.from < RS_STATE_COUNT &&
		event.to >= RS_INACTIVE && event.to < RS_STATE_COUNT &&
		event.from != event.to && event.reason != 0;
}

static bool ValidatePauseEvent( const mpEvidencePauseTransition &event ) {
	if ( event.from < MP_EVIDENCE_PAUSE_RUNNING ||
		event.from >= MP_EVIDENCE_PAUSE_STATE_COUNT ||
		event.to < MP_EVIDENCE_PAUSE_RUNNING ||
		event.to >= MP_EVIDENCE_PAUSE_STATE_COUNT ||
		event.from == event.to || event.kind <= MP_EVIDENCE_PAUSE_NONE ||
		event.kind >= MP_EVIDENCE_PAUSE_KIND_COUNT || event.reason == 0 ||
		!ValidateActor( event.actor ) ) {
		return false;
	}
	return event.kind == MP_EVIDENCE_PAUSE_TEAM_TIMEOUT ?
		IsEvidenceSide( event.ownerSide ) : event.ownerSide == -1;
}

static bool ValidateRoleEvent( const mpEvidenceRoleChange &event ) {
	return event.targetParticipant != 0 &&
		event.previousRoles != event.currentRoles && ValidateActor( event.authorizer );
}

static bool ValidateProposalEvent( const mpEvidenceProposalEvent &event ) {
	return event.proposalId != 0 &&
		event.action >= MP_EVIDENCE_PROPOSAL_CREATED &&
		event.action < MP_EVIDENCE_PROPOSAL_ACTION_COUNT && event.opcode != 0 &&
		IsEvidenceSideOrNone( event.scopeSide ) && ValidateActor( event.actor );
}

static bool ValidateRosterEvent( const mpEvidenceRosterEvent &event ) {
	if ( event.action < MP_EVIDENCE_ROSTER_SEAT_DECLARED ||
		event.action >= MP_EVIDENCE_ROSTER_ACTION_COUNT ||
		!IsEvidenceSideOrNone( event.side ) ||
		event.role < MP_EVIDENCE_ROSTER_PLAYER ||
		event.role >= MP_EVIDENCE_ROSTER_ROLE_COUNT ||
		!ValidateActor( event.authorizer ) ) {
		return false;
	}
	if ( event.action == MP_EVIDENCE_ROSTER_LOCK_CHANGED ) {
		return event.seat == MP_MATCH_EVIDENCE_NO_ROSTER_SEAT &&
			IsEvidenceSide( event.side ) && event.participant == 0 &&
			event.replacementParticipant == 0;
	}
	if ( event.seat >= MP_MATCH_EVIDENCE_MAX_PARTICIPANTS ) {
		return false;
	}
	if ( event.action == MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED ||
		event.action == MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED ) {
		return event.participant != 0 && event.replacementParticipant == 0;
	}
	if ( event.action == MP_EVIDENCE_ROSTER_SUBSTITUTED ) {
		return event.participant != 0 && event.replacementParticipant != 0 &&
			event.participant != event.replacementParticipant;
	}
	return event.participant == 0 && event.replacementParticipant == 0;
}

static bool ValidateResultEvent( const mpEvidenceMapResult &event ) {
	if ( event.outcome < MP_EVIDENCE_RESULT_DECIDED ||
		event.outcome >= MP_EVIDENCE_RESULT_OUTCOME_COUNT || event.reason == 0 ||
		!IsEvidenceSideOrNone( event.winnerSide ) || !ValidateActor( event.authorizer ) ) {
		return false;
	}
	if ( event.outcome == MP_EVIDENCE_RESULT_ABORTED ||
		event.outcome == MP_EVIDENCE_RESULT_DRAW ) {
		return event.winnerSide == -1 && event.winnerParticipant == 0;
	}
	return IsEvidenceSide( event.winnerSide ) || event.winnerParticipant != 0;
}

static bool ValidateOutputFailure( const mpEvidenceOutputFailure &event ) {
	return event.output >= MP_EVIDENCE_OUTPUT_MVD_START &&
		event.output < MP_EVIDENCE_OUTPUT_KIND_COUNT && event.reason != 0;
}

static bool ValidateEventData( mpEvidenceEventKind_t kind,
		const mpEvidenceEventData &data ) {
	switch ( kind ) {
		case MP_EVIDENCE_EVENT_PHASE_TRANSITION:
			return ValidatePhaseEvent( data.phase );
		case MP_EVIDENCE_EVENT_ROUND_TRANSITION:
			return ValidateRoundEvent( data.round );
		case MP_EVIDENCE_EVENT_PAUSE_TRANSITION:
			return ValidatePauseEvent( data.pause );
		case MP_EVIDENCE_EVENT_ROLE_CHANGE:
			return ValidateRoleEvent( data.role );
		case MP_EVIDENCE_EVENT_PROPOSAL:
			return ValidateProposalEvent( data.proposal );
		case MP_EVIDENCE_EVENT_ROSTER_CHANGE:
			return ValidateRosterEvent( data.roster );
		case MP_EVIDENCE_EVENT_MAP_RESULT:
			return ValidateResultEvent( data.result );
		case MP_EVIDENCE_EVENT_OUTPUT_FAILURE:
			return ValidateOutputFailure( data.outputFailure );
		default:
			return false;
	}
}

static const char *ActorKindToken( mpEvidenceActorKind_t kind ) {
	switch ( kind ) {
		case MP_EVIDENCE_ACTOR_SYSTEM: return "system";
		case MP_EVIDENCE_ACTOR_PARTICIPANT: return "participant";
		case MP_EVIDENCE_ACTOR_SERVER_OPERATOR: return "serverOperator";
		default: return "invalid";
	}
}

static const char *EventKindToken( mpEvidenceEventKind_t kind ) {
	switch ( kind ) {
		case MP_EVIDENCE_EVENT_PHASE_TRANSITION: return "phase";
		case MP_EVIDENCE_EVENT_ROUND_TRANSITION: return "round";
		case MP_EVIDENCE_EVENT_PAUSE_TRANSITION: return "pause";
		case MP_EVIDENCE_EVENT_ROLE_CHANGE: return "role";
		case MP_EVIDENCE_EVENT_PROPOSAL: return "proposal";
		case MP_EVIDENCE_EVENT_ROSTER_CHANGE: return "roster";
		case MP_EVIDENCE_EVENT_MAP_RESULT: return "result";
		case MP_EVIDENCE_EVENT_OUTPUT_FAILURE: return "outputFailure";
		default: return "invalid";
	}
}

static const char *ProposalActionToken( mpEvidenceProposalAction_t action ) {
	switch ( action ) {
		case MP_EVIDENCE_PROPOSAL_CREATED: return "created";
		case MP_EVIDENCE_PROPOSAL_BALLOT_CAST: return "ballotCast";
		case MP_EVIDENCE_PROPOSAL_PASSED: return "passed";
		case MP_EVIDENCE_PROPOSAL_FAILED: return "failed";
		case MP_EVIDENCE_PROPOSAL_CANCELLED: return "cancelled";
		case MP_EVIDENCE_PROPOSAL_EXPIRED: return "expired";
		default: return "invalid";
	}
}

static const char *RosterActionToken( mpEvidenceRosterAction_t action ) {
	switch ( action ) {
		case MP_EVIDENCE_ROSTER_SEAT_DECLARED: return "seatDeclared";
		case MP_EVIDENCE_ROSTER_SEAT_CLEARED: return "seatCleared";
		case MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED: return "assigned";
		case MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED: return "vacated";
		case MP_EVIDENCE_ROSTER_SUBSTITUTED: return "substituted";
		case MP_EVIDENCE_ROSTER_LOCK_CHANGED: return "lockChanged";
		default: return "invalid";
	}
}

static const char *ResultToken( mpEvidenceResultOutcome_t outcome ) {
	switch ( outcome ) {
		case MP_EVIDENCE_RESULT_DECIDED: return "decided";
		case MP_EVIDENCE_RESULT_FORFEIT: return "forfeit";
		case MP_EVIDENCE_RESULT_ABORTED: return "aborted";
		case MP_EVIDENCE_RESULT_DRAW: return "draw";
		default: return "invalid";
	}
}

static const char *OutputToken( mpEvidenceOutputKind_t output ) {
	switch ( output ) {
		case MP_EVIDENCE_OUTPUT_MVD_START: return "mvdStart";
		case MP_EVIDENCE_OUTPUT_MVD_STOP: return "mvdStop";
		case MP_EVIDENCE_OUTPUT_MAP_ARTIFACT: return "mapArtifact";
		case MP_EVIDENCE_OUTPUT_SERIES_RECOVERY: return "seriesRecovery";
		case MP_EVIDENCE_OUTPUT_SERIES_REPORT: return "seriesReport";
		default: return "invalid";
	}
}

static const char *ArtifactKindToken( mpEvidenceArtifactKind_t kind ) {
	switch ( kind ) {
		case MP_EVIDENCE_ARTIFACT_MVD: return "mvd";
		default: return "invalid";
	}
}

class mpEvidenceJsonWriter {
public:
				mpEvidenceJsonWriter( char *destination, uint64_t destinationCapacity ) :
					buffer( destination ), capacity( destinationCapacity ), length( 0 ), valid( true ) {
				}

	bool		IsValid( void ) const { return valid; }
	uint64_t	Length( void ) const { return length; }

	void PutChar( char value ) {
		if ( !valid || length == UINT64_MAX ) {
			valid = false;
			return;
		}
		if ( buffer != NULL ) {
			if ( length >= capacity ) {
				valid = false;
				return;
			}
			buffer[ length ] = value;
		}
		++length;
	}

	void PutLiteral( const char *value ) {
		if ( value == NULL ) {
			valid = false;
			return;
		}
		for ( int index = 0; value[ index ] != '\0'; ++index ) {
			PutChar( value[ index ] );
		}
	}

	void PutBoolean( bool value ) {
		PutLiteral( value ? "true" : "false" );
	}

	void PutUInt64( uint64_t value ) {
		char digits[ 32 ];
		int count = 0;
		do {
			digits[ count++ ] = static_cast<char>( '0' + value % 10 );
			value /= 10;
		} while ( value != 0 );
		while ( count > 0 ) {
			PutChar( digits[ --count ] );
		}
	}

	void PutInt64( int64_t value ) {
		if ( value < 0 ) {
			PutChar( '-' );
			const uint64_t magnitude = static_cast<uint64_t>( -( value + 1 ) ) + 1;
			PutUInt64( magnitude );
		} else {
			PutUInt64( static_cast<uint64_t>( value ) );
		}
	}

	void PutHex64String( uint64_t value ) {
		static const char hexadecimal[] = "0123456789abcdef";
		PutChar( '"' );
		for ( int shift = 60; shift >= 0; shift -= 4 ) {
			PutChar( hexadecimal[ ( value >> shift ) & 0x0F ] );
		}
		PutChar( '"' );
	}

	void PutString( const char *value ) {
		static const char hexadecimal[] = "0123456789abcdef";
		PutChar( '"' );
		const unsigned char *bytes = reinterpret_cast<const unsigned char *>( value );
		for ( int index = 0; value[ index ] != '\0'; ++index ) {
			const unsigned char byte = bytes[ index ];
			switch ( byte ) {
				case '"': PutLiteral( "\\\"" ); break;
				case '\\': PutLiteral( "\\\\" ); break;
				case '\b': PutLiteral( "\\b" ); break;
				case '\f': PutLiteral( "\\f" ); break;
				case '\n': PutLiteral( "\\n" ); break;
				case '\r': PutLiteral( "\\r" ); break;
				case '\t': PutLiteral( "\\t" ); break;
				default:
					if ( byte < 0x20 ) {
						PutLiteral( "\\u00" );
						PutChar( hexadecimal[ byte >> 4 ] );
						PutChar( hexadecimal[ byte & 0x0F ] );
					} else {
						PutChar( static_cast<char>( byte ) );
					}
					break;
			}
		}
		PutChar( '"' );
	}

private:
	char *		buffer;
	uint64_t	capacity;
	uint64_t	length;
	bool		valid;
};

static void WriteActor( mpEvidenceJsonWriter &writer, const mpEvidenceActorRef &actor ) {
	writer.PutLiteral( "{\"kind\":" );
	writer.PutString( ActorKindToken( actor.kind ) );
	writer.PutLiteral( ",\"participant\":" );
	writer.PutUInt64( actor.participantSequence );
	writer.PutChar( '}' );
}

static void WriteEventData( mpEvidenceJsonWriter &writer, const mpEvidenceEvent &event ) {
	writer.PutChar( '{' );
	switch ( event.kind ) {
		case MP_EVIDENCE_EVENT_PHASE_TRANSITION:
			writer.PutLiteral( "\"from\":" );
			writer.PutInt64( event.data.phase.from );
			writer.PutLiteral( ",\"to\":" );
			writer.PutInt64( event.data.phase.to );
			writer.PutLiteral( ",\"reason\":" );
			writer.PutUInt64( event.data.phase.reason );
			writer.PutLiteral( ",\"actor\":" );
			WriteActor( writer, event.data.phase.actor );
			break;

		case MP_EVIDENCE_EVENT_ROUND_TRANSITION:
			writer.PutLiteral( "\"from\":" );
			writer.PutInt64( event.data.round.from );
			writer.PutLiteral( ",\"to\":" );
			writer.PutInt64( event.data.round.to );
			writer.PutLiteral( ",\"reason\":" );
			writer.PutUInt64( event.data.round.reason );
			break;

		case MP_EVIDENCE_EVENT_PAUSE_TRANSITION:
			writer.PutLiteral( "\"from\":" );
			writer.PutInt64( event.data.pause.from );
			writer.PutLiteral( ",\"to\":" );
			writer.PutInt64( event.data.pause.to );
			writer.PutLiteral( ",\"kind\":" );
			writer.PutInt64( event.data.pause.kind );
			writer.PutLiteral( ",\"ownerSide\":" );
			writer.PutInt64( event.data.pause.ownerSide );
			writer.PutLiteral( ",\"reason\":" );
			writer.PutUInt64( event.data.pause.reason );
			writer.PutLiteral( ",\"actor\":" );
			WriteActor( writer, event.data.pause.actor );
			break;

		case MP_EVIDENCE_EVENT_ROLE_CHANGE:
			writer.PutLiteral( "\"targetParticipant\":" );
			writer.PutUInt64( event.data.role.targetParticipant );
			writer.PutLiteral( ",\"previousRoles\":" );
			writer.PutUInt64( event.data.role.previousRoles );
			writer.PutLiteral( ",\"currentRoles\":" );
			writer.PutUInt64( event.data.role.currentRoles );
			writer.PutLiteral( ",\"authorizer\":" );
			WriteActor( writer, event.data.role.authorizer );
			break;

		case MP_EVIDENCE_EVENT_PROPOSAL:
			writer.PutLiteral( "\"proposalId\":" );
			writer.PutUInt64( event.data.proposal.proposalId );
			writer.PutLiteral( ",\"action\":" );
			writer.PutString( ProposalActionToken( event.data.proposal.action ) );
			writer.PutLiteral( ",\"opcode\":" );
			writer.PutUInt64( event.data.proposal.opcode );
			writer.PutLiteral( ",\"scopeSide\":" );
			writer.PutInt64( event.data.proposal.scopeSide );
			writer.PutLiteral( ",\"targetParticipant\":" );
			writer.PutUInt64( event.data.proposal.targetParticipant );
			writer.PutLiteral( ",\"actor\":" );
			WriteActor( writer, event.data.proposal.actor );
			break;

		case MP_EVIDENCE_EVENT_ROSTER_CHANGE:
			writer.PutLiteral( "\"action\":" );
			writer.PutString( RosterActionToken( event.data.roster.action ) );
			writer.PutLiteral( ",\"seat\":" );
			writer.PutUInt64( event.data.roster.seat );
			writer.PutLiteral( ",\"side\":" );
			writer.PutInt64( event.data.roster.side );
			writer.PutLiteral( ",\"role\":" );
			writer.PutInt64( event.data.roster.role );
			writer.PutLiteral( ",\"participant\":" );
			writer.PutUInt64( event.data.roster.participant );
			writer.PutLiteral( ",\"replacementParticipant\":" );
			writer.PutUInt64( event.data.roster.replacementParticipant );
			writer.PutLiteral( ",\"locked\":" );
			writer.PutBoolean( event.data.roster.locked );
			writer.PutLiteral( ",\"authorizer\":" );
			WriteActor( writer, event.data.roster.authorizer );
			break;

		case MP_EVIDENCE_EVENT_MAP_RESULT:
			writer.PutLiteral( "\"outcome\":" );
			writer.PutString( ResultToken( event.data.result.outcome ) );
			writer.PutLiteral( ",\"winnerSide\":" );
			writer.PutInt64( event.data.result.winnerSide );
			writer.PutLiteral( ",\"winnerParticipant\":" );
			writer.PutUInt64( event.data.result.winnerParticipant );
			writer.PutLiteral( ",\"sideScores\":[" );
			writer.PutInt64( event.data.result.sideScore[ 0 ] );
			writer.PutChar( ',' );
			writer.PutInt64( event.data.result.sideScore[ 1 ] );
			writer.PutLiteral( "],\"reason\":" );
			writer.PutUInt64( event.data.result.reason );
			writer.PutLiteral( ",\"authorizer\":" );
			WriteActor( writer, event.data.result.authorizer );
			break;

		case MP_EVIDENCE_EVENT_OUTPUT_FAILURE:
			writer.PutLiteral( "\"output\":" );
			writer.PutString( OutputToken( event.data.outputFailure.output ) );
			writer.PutLiteral( ",\"reason\":" );
			writer.PutUInt64( event.data.outputFailure.reason );
			break;

		default:
			break;
	}
	writer.PutChar( '}' );
}

static void WriteEvent( mpEvidenceJsonWriter &writer, const mpEvidenceEvent &event ) {
	writer.PutLiteral( "{\"sequence\":" );
	writer.PutUInt64( event.sequence );
	writer.PutLiteral( ",\"sessionRevision\":" );
	writer.PutUInt64( event.stamp.sessionRevision );
	writer.PutLiteral( ",\"matchTimeMsec\":" );
	writer.PutUInt64( event.stamp.matchTimeMsec );
	writer.PutLiteral( ",\"hostTimeUtcMsec\":" );
	writer.PutUInt64( event.stamp.hostTimeUtcMsec );
	writer.PutLiteral( ",\"kind\":" );
	writer.PutString( EventKindToken( event.kind ) );
	writer.PutLiteral( ",\"data\":" );
	WriteEventData( writer, event );
	writer.PutChar( '}' );
}

static bool ParticipantStatsEqual( const mpEvidenceParticipantFinalStats &stored,
		const mpEvidenceParticipantStatsInput &input, uint64_t sessionRevision ) {
	return stored.sessionRevision == sessionRevision &&
		stored.participantSequence == input.participantSequence &&
		stored.side == input.side && strcmp( stored.displayName, input.displayName ) == 0 &&
		stored.score == input.score && stored.kills == input.kills &&
		stored.deaths == input.deaths && stored.suicides == input.suicides &&
		stored.damageGiven == input.damageGiven &&
		stored.damageReceived == input.damageReceived && stored.shots == input.shots &&
		stored.hits == input.hits;
}

static bool TeamStatsEqual( const mpEvidenceTeamFinalStats &stored,
		uint64_t sessionRevision, int side, int score, uint32_t objectives,
		uint32_t roundsWon, uint32_t damageGiven ) {
	return stored.sessionRevision == sessionRevision && stored.side == side &&
		stored.score == score && stored.objectives == objectives &&
		stored.roundsWon == roundsWon && stored.damageGiven == damageGiven;
}

} // namespace

/*
===============================================================================

Public value helpers

===============================================================================
*/

bool MPMatchEvidenceIsSafeArtifactQPath( mpEvidenceArtifactKind_t kind,
		const char *qpath ) {
	return ValidateArtifactQPathInternal( kind, qpath );
}

mpEvidenceActorRef MPEvidenceSystemActor( void ) {
	mpEvidenceActorRef actor;
	actor.kind = MP_EVIDENCE_ACTOR_SYSTEM;
	actor.participantSequence = 0;
	return actor;
}

mpEvidenceActorRef MPEvidenceParticipantActor( uint32_t participantSequence ) {
	mpEvidenceActorRef actor;
	actor.kind = MP_EVIDENCE_ACTOR_PARTICIPANT;
	actor.participantSequence = participantSequence;
	return actor;
}

mpEvidenceActorRef MPEvidenceServerOperatorActor( void ) {
	mpEvidenceActorRef actor;
	actor.kind = MP_EVIDENCE_ACTOR_SERVER_OPERATOR;
	actor.participantSequence = 0;
	return actor;
}

bool mpEvidenceWriteResult::WasAccepted( void ) const {
	return code == MP_EVIDENCE_WRITE_ACCEPTED;
}

bool mpEvidenceWriteResult::WasDropped( void ) const {
	return code == MP_EVIDENCE_WRITE_DROPPED;
}

bool mpEvidenceSerializeResult::Succeeded( void ) const {
	return code == MP_EVIDENCE_SERIALIZE_SUCCESS;
}

/*
===============================================================================

	mpMatchEvidence setup and mutation helpers

===============================================================================
*/

mpMatchEvidence::mpMatchEvidence( void ) {
	Clear();
}

void mpMatchEvidence::Clear( void ) {
	initialized = false;
	memset( &metadata, 0, sizeof( metadata ) );
	evidenceRevision = 0;
	lastSessionRevision = 0;
	nextEventSequence = 1;
	seriesLinkSessionRevision = 0;
	memset( artifacts, 0, sizeof( artifacts ) );
	artifactCount = 0;
	memset( events, 0, sizeof( events ) );
	eventCount = 0;
	droppedEventCount = 0;
	firstDroppedSessionRevision = 0;
	lastDroppedSessionRevision = 0;
	dropCounterSaturated = false;
	memset( participantStats, 0, sizeof( participantStats ) );
	participantStatsCount = 0;
	droppedParticipantStatsCount = 0;
	memset( teamStats, 0, sizeof( teamStats ) );
	teamStatsCount = 0;
	droppedTeamStatsCount = 0;
}

bool mpMatchEvidence::Reset( const mpEvidenceMetadataInput &input ) {
	mpEvidenceMetadata newMetadata;
	memset( &newMetadata, 0, sizeof( newMetadata ) );
	if ( input.sessionId == 0 || input.modeId == 0 ||
		!CopyEvidenceText( input.build, newMetadata.build,
			MP_MATCH_EVIDENCE_MAX_BUILD_BYTES, false ) ||
		!CopyEvidenceText( input.map, newMetadata.map,
			MP_MATCH_EVIDENCE_MAX_MAP_BYTES, false ) ||
		!CopyEvidenceText( input.mode, newMetadata.mode,
			MP_MATCH_EVIDENCE_MAX_MODE_BYTES, false ) ) {
		return false;
	}
	newMetadata.schemaVersion = MP_MATCH_EVIDENCE_SCHEMA_VERSION;
	newMetadata.sessionId = input.sessionId;
	newMetadata.seriesId = input.seriesId;
	newMetadata.rulesDigest = input.rulesDigest;
	newMetadata.modeId = input.modeId;

	initialized = true;
	metadata = newMetadata;
	evidenceRevision = 1;
	lastSessionRevision = 0;
	nextEventSequence = 1;
	seriesLinkSessionRevision = 0;
	memset( artifacts, 0, sizeof( artifacts ) );
	artifactCount = 0;
	memset( events, 0, sizeof( events ) );
	eventCount = 0;
	droppedEventCount = 0;
	firstDroppedSessionRevision = 0;
	lastDroppedSessionRevision = 0;
	dropCounterSaturated = false;
	memset( participantStats, 0, sizeof( participantStats ) );
	participantStatsCount = 0;
	droppedParticipantStatsCount = 0;
	memset( teamStats, 0, sizeof( teamStats ) );
	teamStatsCount = 0;
	droppedTeamStatsCount = 0;
	return ValidateInvariants();
}

bool mpMatchEvidence::IsInitialized( void ) const {
	return initialized;
}

const mpEvidenceMetadata &mpMatchEvidence::GetMetadata( void ) const {
	return metadata;
}

uint64_t mpMatchEvidence::GetEvidenceRevision( void ) const {
	return evidenceRevision;
}

uint64_t mpMatchEvidence::GetLastSessionRevision( void ) const {
	return lastSessionRevision;
}

uint64_t mpMatchEvidence::GetSeriesLinkSessionRevision( void ) const {
	return seriesLinkSessionRevision;
}

bool mpMatchEvidence::ValidateStamp( const mpEvidenceCommittedStamp &stamp ) const {
	return initialized && stamp.sessionRevision != 0 &&
		stamp.sessionRevision >= lastSessionRevision;
}

bool mpMatchEvidence::CanMutate( void ) const {
	return initialized && evidenceRevision < UINT64_MAX;
}

mpEvidenceWriteResult mpMatchEvidence::Accepted( uint64_t previousRevision ) {
	mpEvidenceWriteResult result;
	result.code = MP_EVIDENCE_WRITE_ACCEPTED;
	result.reason = MP_EVIDENCE_REASON_NONE;
	result.previousEvidenceRevision = previousRevision;
	++evidenceRevision;
	result.currentEvidenceRevision = evidenceRevision;
	return result;
}

mpEvidenceWriteResult mpMatchEvidence::NoChange( mpEvidenceReason_t reason ) const {
	mpEvidenceWriteResult result;
	result.code = MP_EVIDENCE_WRITE_NO_CHANGE;
	result.reason = reason;
	result.previousEvidenceRevision = evidenceRevision;
	result.currentEvidenceRevision = evidenceRevision;
	return result;
}

mpEvidenceWriteResult mpMatchEvidence::Rejected( mpEvidenceReason_t reason ) const {
	mpEvidenceWriteResult result;
	result.code = MP_EVIDENCE_WRITE_REJECTED;
	result.reason = reason;
	result.previousEvidenceRevision = evidenceRevision;
	result.currentEvidenceRevision = evidenceRevision;
	return result;
}

mpEvidenceWriteResult mpMatchEvidence::LinkSeriesId(
		const mpEvidenceCommittedStamp &stamp, uint64_t seriesId ) {
	if ( seriesId == 0 ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	}
	if ( !ValidateStamp( stamp ) ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	if ( !CanMutate() ) {
		return Rejected( MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED );
	}
	if ( metadata.seriesId != 0 ) {
		return metadata.seriesId == seriesId ?
			NoChange( MP_EVIDENCE_REASON_NONE ) :
			Rejected( MP_EVIDENCE_REASON_SERIES_ID_CONFLICT );
	}

	const uint64_t previousRevision = evidenceRevision;
	metadata.seriesId = seriesId;
	seriesLinkSessionRevision = stamp.sessionRevision;
	lastSessionRevision = stamp.sessionRevision;
	return Accepted( previousRevision );
}

int mpMatchEvidence::FindArtifact( mpEvidenceArtifactKind_t kind ) const {
	for ( int index = 0; index < artifactCount; ++index ) {
		if ( artifacts[ index ].kind == kind ) {
			return index;
		}
	}
	return -1;
}

mpEvidenceWriteResult mpMatchEvidence::LinkArtifact(
		const mpEvidenceCommittedStamp &stamp,
		const mpEvidenceArtifactLinkInput &artifact ) {
	if ( artifact.kind <= MP_EVIDENCE_ARTIFACT_INVALID ||
		artifact.kind >= MP_EVIDENCE_ARTIFACT_KIND_COUNT ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	}
	if ( !MPMatchEvidenceIsSafeArtifactQPath( artifact.kind, artifact.qpath ) ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARTIFACT_QPATH );
	}
	if ( !ValidateStamp( stamp ) ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	if ( !CanMutate() ) {
		return Rejected( MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED );
	}
	const int existing = FindArtifact( artifact.kind );
	if ( existing >= 0 ) {
		return strcmp( artifacts[ existing ].qpath, artifact.qpath ) == 0 ?
			NoChange( MP_EVIDENCE_REASON_NONE ) :
			Rejected( MP_EVIDENCE_REASON_ARTIFACT_CONFLICT );
	}
	if ( artifactCount >= MP_MATCH_EVIDENCE_MAX_ARTIFACTS ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	}

	mpEvidenceArtifactLink candidate;
	memset( &candidate, 0, sizeof( candidate ) );
	candidate.sessionRevision = stamp.sessionRevision;
	candidate.kind = artifact.kind;
	if ( !CopyEvidenceText( artifact.qpath, candidate.qpath,
			MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES, false ) ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARTIFACT_QPATH );
	}

	int insertion = artifactCount;
	while ( insertion > 0 && artifacts[ insertion - 1 ].kind > candidate.kind ) {
		artifacts[ insertion ] = artifacts[ insertion - 1 ];
		--insertion;
	}
	artifacts[ insertion ] = candidate;
	++artifactCount;
	const uint64_t previousRevision = evidenceRevision;
	lastSessionRevision = stamp.sessionRevision;
	return Accepted( previousRevision );
}

int mpMatchEvidence::GetArtifactCount( void ) const {
	return artifactCount;
}

const mpEvidenceArtifactLink *mpMatchEvidence::GetArtifact( int index ) const {
	return index >= 0 && index < artifactCount ? &artifacts[ index ] : NULL;
}

mpEvidenceWriteResult mpMatchEvidence::RecordDrop( const mpEvidenceCommittedStamp &stamp,
		mpEvidenceReason_t reason, uint64_t &counter ) {
	if ( !CanMutate() ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	const uint64_t previousRevision = evidenceRevision;
	if ( counter < UINT64_MAX ) {
		++counter;
	} else {
		dropCounterSaturated = true;
	}
	if ( &counter == &droppedEventCount ) {
		if ( firstDroppedSessionRevision == 0 ) {
			firstDroppedSessionRevision = stamp.sessionRevision;
		}
		lastDroppedSessionRevision = stamp.sessionRevision;
	}
	lastSessionRevision = stamp.sessionRevision;
	mpEvidenceWriteResult result = Accepted( previousRevision );
	result.code = MP_EVIDENCE_WRITE_DROPPED;
	result.reason = reason;
	return result;
}

mpEvidenceWriteResult mpMatchEvidence::AppendValidatedEvent(
		const mpEvidenceCommittedStamp &stamp, mpEvidenceEventKind_t kind,
		const mpEvidenceEventData &data ) {
	if ( !ValidateStamp( stamp ) ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	if ( !CanMutate() ) {
		return Rejected( MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED );
	}
	// Keep one slot available until the authoritative terminal result exists.
	// A noisy long-running match may lose optional history, but it must never
	// become impossible to seal the score that advances its series.
	bool hasMapResult = false;
	for ( int index = 0; index < eventCount; ++index ) {
		if ( events[ index ].kind == MP_EVIDENCE_EVENT_MAP_RESULT ) {
			hasMapResult = true;
			break;
		}
	}
	const int eventCapacity = kind == MP_EVIDENCE_EVENT_MAP_RESULT || hasMapResult ?
		MP_MATCH_EVIDENCE_MAX_EVENTS : MP_MATCH_EVIDENCE_MAX_EVENTS - 1;
	if ( eventCount >= eventCapacity ) {
		return RecordDrop( stamp, MP_EVIDENCE_REASON_EVENT_CAPACITY, droppedEventCount );
	}
	if ( nextEventSequence == 0 ) {
		return RecordDrop( stamp, MP_EVIDENCE_REASON_EVENT_SEQUENCE_EXHAUSTED,
			droppedEventCount );
	}

	const uint64_t previousRevision = evidenceRevision;
	mpEvidenceEvent &destination = events[ eventCount++ ];
	memset( &destination, 0, sizeof( destination ) );
	destination.sequence = nextEventSequence;
	destination.stamp = stamp;
	destination.kind = kind;
	destination.data = data;
	if ( nextEventSequence == UINT64_MAX ) {
		nextEventSequence = 0;
	} else {
		++nextEventSequence;
	}
	lastSessionRevision = stamp.sessionRevision;
	return Accepted( previousRevision );
}

/*
===============================================================================

	Typed journal entry points

===============================================================================
*/

#define MP_EVIDENCE_APPEND_IMPLEMENTATION( methodName, eventKind, memberName, payloadType, validator ) \
	mpEvidenceWriteResult mpMatchEvidence::methodName( \
			const mpEvidenceCommittedStamp &stamp, const payloadType &event ) { \
		if ( !validator( event ) ) { \
			return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT ); \
		} \
		mpEvidenceEventData data; \
		memset( &data, 0, sizeof( data ) ); \
		data.memberName = event; \
		return AppendValidatedEvent( stamp, eventKind, data ); \
	}

MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendPhaseTransition,
	MP_EVIDENCE_EVENT_PHASE_TRANSITION, phase, mpEvidencePhaseTransition,
	ValidatePhaseEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendRoundTransition,
	MP_EVIDENCE_EVENT_ROUND_TRANSITION, round, mpEvidenceRoundTransition,
	ValidateRoundEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendPauseTransition,
	MP_EVIDENCE_EVENT_PAUSE_TRANSITION, pause, mpEvidencePauseTransition,
	ValidatePauseEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendRoleChange,
	MP_EVIDENCE_EVENT_ROLE_CHANGE, role, mpEvidenceRoleChange,
	ValidateRoleEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendProposal,
	MP_EVIDENCE_EVENT_PROPOSAL, proposal, mpEvidenceProposalEvent,
	ValidateProposalEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendRosterChange,
	MP_EVIDENCE_EVENT_ROSTER_CHANGE, roster, mpEvidenceRosterEvent,
	ValidateRosterEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendMapResult,
	MP_EVIDENCE_EVENT_MAP_RESULT, result, mpEvidenceMapResult,
	ValidateResultEvent )
MP_EVIDENCE_APPEND_IMPLEMENTATION( AppendOutputFailure,
	MP_EVIDENCE_EVENT_OUTPUT_FAILURE, outputFailure, mpEvidenceOutputFailure,
	ValidateOutputFailure )

#undef MP_EVIDENCE_APPEND_IMPLEMENTATION

int mpMatchEvidence::GetEventCount( void ) const {
	return eventCount;
}

const mpEvidenceEvent *mpMatchEvidence::GetEvent( int index ) const {
	return index >= 0 && index < eventCount ? &events[ index ] : NULL;
}

uint64_t mpMatchEvidence::GetDroppedEventCount( void ) const {
	return droppedEventCount;
}

uint64_t mpMatchEvidence::GetFirstDroppedSessionRevision( void ) const {
	return firstDroppedSessionRevision;
}

uint64_t mpMatchEvidence::GetLastDroppedSessionRevision( void ) const {
	return lastDroppedSessionRevision;
}

bool mpMatchEvidence::IsDropCounterSaturated( void ) const {
	return dropCounterSaturated;
}

/*
===============================================================================

	Final statistics

===============================================================================
*/

int mpMatchEvidence::FindParticipantStats( uint32_t participantSequence ) const {
	for ( int index = 0; index < participantStatsCount; ++index ) {
		if ( participantStats[ index ].participantSequence == participantSequence ) {
			return index;
		}
	}
	return -1;
}

int mpMatchEvidence::FindTeamStats( int side ) const {
	for ( int index = 0; index < teamStatsCount; ++index ) {
		if ( teamStats[ index ].side == side ) {
			return index;
		}
	}
	return -1;
}

mpEvidenceWriteResult mpMatchEvidence::RecordParticipantFinalStats(
		const mpEvidenceCommittedStamp &stamp,
		const mpEvidenceParticipantStatsInput &stats ) {
	int displayNameLength = 0;
	if ( stats.participantSequence == 0 || !IsEvidenceSideOrNone( stats.side ) ||
		stats.hits > stats.shots ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	}
	if ( !BoundedTextLength( stats.displayName,
			MP_MATCH_EVIDENCE_MAX_DISPLAY_NAME_BYTES, false, displayNameLength ) ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_TEXT );
	}
	if ( !ValidateStamp( stamp ) ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	if ( !CanMutate() ) {
		return Rejected( MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED );
	}
	const int existing = FindParticipantStats( stats.participantSequence );
	if ( existing >= 0 ) {
		return ParticipantStatsEqual( participantStats[ existing ], stats,
			stamp.sessionRevision ) ? NoChange( MP_EVIDENCE_REASON_NONE ) :
			Rejected( MP_EVIDENCE_REASON_DUPLICATE_PARTICIPANT_STATS );
	}
	if ( participantStatsCount >= MP_MATCH_EVIDENCE_MAX_PARTICIPANTS ) {
		return RecordDrop( stamp, MP_EVIDENCE_REASON_PARTICIPANT_STATS_CAPACITY,
			droppedParticipantStatsCount );
	}

	const uint64_t previousRevision = evidenceRevision;
	mpEvidenceParticipantFinalStats &destination =
		participantStats[ participantStatsCount++ ];
	memset( &destination, 0, sizeof( destination ) );
	destination.sessionRevision = stamp.sessionRevision;
	destination.participantSequence = stats.participantSequence;
	destination.side = stats.side;
	memcpy( destination.displayName, stats.displayName,
		static_cast<size_t>( displayNameLength ) );
	destination.displayName[ displayNameLength ] = '\0';
	destination.score = stats.score;
	destination.kills = stats.kills;
	destination.deaths = stats.deaths;
	destination.suicides = stats.suicides;
	destination.damageGiven = stats.damageGiven;
	destination.damageReceived = stats.damageReceived;
	destination.shots = stats.shots;
	destination.hits = stats.hits;
	lastSessionRevision = stamp.sessionRevision;
	return Accepted( previousRevision );
}

mpEvidenceWriteResult mpMatchEvidence::RecordTeamFinalStats(
		const mpEvidenceCommittedStamp &stamp, int side, int score,
		uint32_t objectives, uint32_t roundsWon, uint32_t damageGiven ) {
	if ( !IsEvidenceSide( side ) ) {
		return Rejected( MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	}
	if ( !ValidateStamp( stamp ) ) {
		return Rejected( initialized ? MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION :
			MP_EVIDENCE_REASON_NOT_INITIALIZED );
	}
	if ( !CanMutate() ) {
		return Rejected( MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED );
	}
	const int existing = FindTeamStats( side );
	if ( existing >= 0 ) {
		return TeamStatsEqual( teamStats[ existing ], stamp.sessionRevision, side,
			score, objectives, roundsWon, damageGiven ) ?
			NoChange( MP_EVIDENCE_REASON_NONE ) :
			Rejected( MP_EVIDENCE_REASON_DUPLICATE_TEAM_STATS );
	}
	if ( teamStatsCount >= MP_MATCH_EVIDENCE_MAX_TEAMS ) {
		return RecordDrop( stamp, MP_EVIDENCE_REASON_TEAM_STATS_CAPACITY,
			droppedTeamStatsCount );
	}

	const uint64_t previousRevision = evidenceRevision;
	mpEvidenceTeamFinalStats &destination = teamStats[ teamStatsCount++ ];
	memset( &destination, 0, sizeof( destination ) );
	destination.sessionRevision = stamp.sessionRevision;
	destination.side = static_cast<int8_t>( side );
	destination.score = score;
	destination.objectives = objectives;
	destination.roundsWon = roundsWon;
	destination.damageGiven = damageGiven;
	lastSessionRevision = stamp.sessionRevision;
	return Accepted( previousRevision );
}

int mpMatchEvidence::GetParticipantStatsCount( void ) const {
	return participantStatsCount;
}

const mpEvidenceParticipantFinalStats *mpMatchEvidence::GetParticipantStats( int index ) const {
	return index >= 0 && index < participantStatsCount ? &participantStats[ index ] : NULL;
}

int mpMatchEvidence::GetTeamStatsCount( void ) const {
	return teamStatsCount;
}

const mpEvidenceTeamFinalStats *mpMatchEvidence::GetTeamStats( int index ) const {
	return index >= 0 && index < teamStatsCount ? &teamStats[ index ] : NULL;
}

uint64_t mpMatchEvidence::GetDroppedParticipantStatsCount( void ) const {
	return droppedParticipantStatsCount;
}

uint64_t mpMatchEvidence::GetDroppedTeamStatsCount( void ) const {
	return droppedTeamStatsCount;
}

/*
===============================================================================

	Canonical JSON

===============================================================================
*/

static void WriteCanonicalArtifact( mpEvidenceJsonWriter &writer,
		const mpMatchEvidence &evidence ) {
	const mpEvidenceMetadata &metadata = evidence.GetMetadata();
	writer.PutLiteral( "{\"schema\":" );
	writer.PutUInt64( metadata.schemaVersion );
	writer.PutLiteral( ",\"sessionId\":" );
	writer.PutUInt64( metadata.sessionId );
	writer.PutLiteral( ",\"seriesId\":" );
	writer.PutUInt64( metadata.seriesId );
	writer.PutLiteral( ",\"seriesLinkSessionRevision\":" );
	writer.PutUInt64( evidence.GetSeriesLinkSessionRevision() );
	writer.PutLiteral( ",\"build\":" );
	writer.PutString( metadata.build );
	writer.PutLiteral( ",\"rulesDigest\":" );
	writer.PutHex64String( metadata.rulesDigest );
	writer.PutLiteral( ",\"map\":" );
	writer.PutString( metadata.map );
	writer.PutLiteral( ",\"mode\":{\"id\":" );
	writer.PutUInt64( metadata.modeId );
	writer.PutLiteral( ",\"token\":" );
	writer.PutString( metadata.mode );
	writer.PutLiteral( "},\"evidenceRevision\":" );
	writer.PutUInt64( evidence.GetEvidenceRevision() );
	writer.PutLiteral( ",\"lastSessionRevision\":" );
	writer.PutUInt64( evidence.GetLastSessionRevision() );
	writer.PutLiteral( ",\"artifacts\":[" );
	for ( int index = 0; index < evidence.GetArtifactCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		const mpEvidenceArtifactLink &artifact = *evidence.GetArtifact( index );
		writer.PutLiteral( "{\"kind\":" );
		writer.PutString( ArtifactKindToken( artifact.kind ) );
		writer.PutLiteral( ",\"sessionRevision\":" );
		writer.PutUInt64( artifact.sessionRevision );
		writer.PutLiteral( ",\"qpath\":" );
		writer.PutString( artifact.qpath );
		writer.PutChar( '}' );
	}
	writer.PutChar( ']' );

	writer.PutLiteral( ",\"journal\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( evidence.GetEventCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( evidence.GetDroppedEventCount() );
	writer.PutLiteral( ",\"firstDroppedSessionRevision\":" );
	writer.PutUInt64( evidence.GetFirstDroppedSessionRevision() );
	writer.PutLiteral( ",\"lastDroppedSessionRevision\":" );
	writer.PutUInt64( evidence.GetLastDroppedSessionRevision() );
	writer.PutLiteral( ",\"dropCounterSaturated\":" );
	writer.PutBoolean( evidence.IsDropCounterSaturated() );
	writer.PutLiteral( ",\"events\":[" );
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		WriteEvent( writer, *evidence.GetEvent( index ) );
	}
	writer.PutLiteral( "]}" );

	writer.PutLiteral( ",\"participantStats\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( evidence.GetParticipantStatsCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( evidence.GetDroppedParticipantStatsCount() );
	writer.PutLiteral( ",\"entries\":[" );
	for ( int index = 0; index < evidence.GetParticipantStatsCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		const mpEvidenceParticipantFinalStats &stats =
			*evidence.GetParticipantStats( index );
		writer.PutLiteral( "{\"sessionRevision\":" );
		writer.PutUInt64( stats.sessionRevision );
		writer.PutLiteral( ",\"participant\":" );
		writer.PutUInt64( stats.participantSequence );
		writer.PutLiteral( ",\"side\":" );
		writer.PutInt64( stats.side );
		writer.PutLiteral( ",\"displayName\":" );
		writer.PutString( stats.displayName );
		writer.PutLiteral( ",\"score\":" );
		writer.PutInt64( stats.score );
		writer.PutLiteral( ",\"kills\":" );
		writer.PutUInt64( stats.kills );
		writer.PutLiteral( ",\"deaths\":" );
		writer.PutUInt64( stats.deaths );
		writer.PutLiteral( ",\"suicides\":" );
		writer.PutUInt64( stats.suicides );
		writer.PutLiteral( ",\"damageGiven\":" );
		writer.PutUInt64( stats.damageGiven );
		writer.PutLiteral( ",\"damageReceived\":" );
		writer.PutUInt64( stats.damageReceived );
		writer.PutLiteral( ",\"shots\":" );
		writer.PutUInt64( stats.shots );
		writer.PutLiteral( ",\"hits\":" );
		writer.PutUInt64( stats.hits );
		writer.PutChar( '}' );
	}
	writer.PutLiteral( "]}" );

	writer.PutLiteral( ",\"teamStats\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( evidence.GetTeamStatsCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( evidence.GetDroppedTeamStatsCount() );
	writer.PutLiteral( ",\"entries\":[" );
	for ( int index = 0; index < evidence.GetTeamStatsCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		const mpEvidenceTeamFinalStats &stats = *evidence.GetTeamStats( index );
		writer.PutLiteral( "{\"sessionRevision\":" );
		writer.PutUInt64( stats.sessionRevision );
		writer.PutLiteral( ",\"side\":" );
		writer.PutInt64( stats.side );
		writer.PutLiteral( ",\"score\":" );
		writer.PutInt64( stats.score );
		writer.PutLiteral( ",\"objectives\":" );
		writer.PutUInt64( stats.objectives );
		writer.PutLiteral( ",\"roundsWon\":" );
		writer.PutUInt64( stats.roundsWon );
		writer.PutLiteral( ",\"damageGiven\":" );
		writer.PutUInt64( stats.damageGiven );
		writer.PutChar( '}' );
	}
	writer.PutLiteral( "]}}" );
}

mpEvidenceSerializeResult mpMatchEvidence::SerializeCanonicalJson( char *buffer,
		int capacity ) const {
	mpEvidenceSerializeResult result;
	result.code = MP_EVIDENCE_SERIALIZE_INVALID_ARGUMENT;
	result.bytesWritten = 0;
	result.requiredCapacity = 0;
	if ( capacity < 0 ) {
		return result;
	}
	if ( !initialized || !ValidateInvariants() ) {
		result.code = MP_EVIDENCE_SERIALIZE_INVALID_STATE;
		return result;
	}

	mpEvidenceJsonWriter counter( NULL, 0 );
	WriteCanonicalArtifact( counter, *this );
	// The public serializer uses the engine's 32-bit int capacity convention on
	// every supported target; keep the bound independent of idMath's INT_MAX.
	if ( !counter.IsValid() || counter.Length() >= 2147483647ULL ) {
		result.code = MP_EVIDENCE_SERIALIZE_OUTPUT_TOO_LARGE;
		return result;
	}
	result.requiredCapacity = static_cast<int>( counter.Length() ) + 1;
	if ( buffer == NULL || capacity < result.requiredCapacity ) {
		result.code = MP_EVIDENCE_SERIALIZE_BUFFER_TOO_SMALL;
		return result;
	}

	mpEvidenceJsonWriter writer( buffer, static_cast<uint64_t>( capacity ) );
	WriteCanonicalArtifact( writer, *this );
	// This pass is the same deterministic walk as the count pass, over immutable
	// state, so it has no caller-controlled failure path after the first write.
	buffer[ counter.Length() ] = '\0';
	result.code = MP_EVIDENCE_SERIALIZE_SUCCESS;
	result.bytesWritten = static_cast<int>( counter.Length() );
	return result;
}

/*
===============================================================================

	Invariants

===============================================================================
*/

bool mpMatchEvidence::ValidateInvariants( void ) const {
	if ( !initialized ) {
		return evidenceRevision == 0 && lastSessionRevision == 0 &&
			seriesLinkSessionRevision == 0 && artifactCount == 0 &&
			eventCount == 0 && participantStatsCount == 0 && teamStatsCount == 0;
	}
	int ignoredLength = 0;
	if ( metadata.schemaVersion != MP_MATCH_EVIDENCE_SCHEMA_VERSION ||
		metadata.sessionId == 0 || metadata.modeId == 0 || evidenceRevision == 0 ||
		!BoundedTextLength( metadata.build, MP_MATCH_EVIDENCE_MAX_BUILD_BYTES,
			false, ignoredLength ) ||
		!BoundedTextLength( metadata.map, MP_MATCH_EVIDENCE_MAX_MAP_BYTES,
			false, ignoredLength ) ||
		!BoundedTextLength( metadata.mode, MP_MATCH_EVIDENCE_MAX_MODE_BYTES,
			false, ignoredLength ) ||
		( seriesLinkSessionRevision != 0 &&
			( metadata.seriesId == 0 ||
				seriesLinkSessionRevision > lastSessionRevision ) ) ||
		artifactCount < 0 || artifactCount > MP_MATCH_EVIDENCE_MAX_ARTIFACTS ||
		eventCount < 0 ||
		eventCount > MP_MATCH_EVIDENCE_MAX_EVENTS || participantStatsCount < 0 ||
		participantStatsCount > MP_MATCH_EVIDENCE_MAX_PARTICIPANTS ||
		teamStatsCount < 0 || teamStatsCount > MP_MATCH_EVIDENCE_MAX_TEAMS ) {
		return false;
	}
	for ( int index = 0; index < artifactCount; ++index ) {
		const mpEvidenceArtifactLink &artifact = artifacts[ index ];
		if ( artifact.sessionRevision == 0 ||
			artifact.sessionRevision > lastSessionRevision ||
			artifact.kind <= MP_EVIDENCE_ARTIFACT_INVALID ||
			artifact.kind >= MP_EVIDENCE_ARTIFACT_KIND_COUNT ||
			!MPMatchEvidenceIsSafeArtifactQPath( artifact.kind, artifact.qpath ) ||
			( index > 0 && artifacts[ index - 1 ].kind >= artifact.kind ) ) {
			return false;
		}
	}

	uint64_t observedRevision = 0;
	for ( int index = 0; index < eventCount; ++index ) {
		const mpEvidenceEvent &event = events[ index ];
		if ( event.sequence != static_cast<uint64_t>( index ) + 1 ||
			event.stamp.sessionRevision == 0 ||
			event.stamp.sessionRevision < observedRevision ||
			event.kind <= MP_EVIDENCE_EVENT_INVALID ||
			event.kind >= MP_EVIDENCE_EVENT_KIND_COUNT ||
			!ValidateEventData( event.kind, event.data ) ) {
			return false;
		}
		observedRevision = event.stamp.sessionRevision;
	}
	if ( nextEventSequence != static_cast<uint64_t>( eventCount ) + 1 ) {
		return false;
	}

	for ( int index = 0; index < participantStatsCount; ++index ) {
		const mpEvidenceParticipantFinalStats &stats = participantStats[ index ];
		if ( stats.sessionRevision == 0 || stats.sessionRevision > lastSessionRevision ||
			stats.participantSequence == 0 || !IsEvidenceSideOrNone( stats.side ) ||
			stats.hits > stats.shots ||
			!BoundedTextLength( stats.displayName,
				MP_MATCH_EVIDENCE_MAX_DISPLAY_NAME_BYTES, false, ignoredLength ) ) {
			return false;
		}
		for ( int other = index + 1; other < participantStatsCount; ++other ) {
			if ( stats.participantSequence == participantStats[ other ].participantSequence ) {
				return false;
			}
		}
	}
	for ( int index = 0; index < teamStatsCount; ++index ) {
		const mpEvidenceTeamFinalStats &stats = teamStats[ index ];
		if ( stats.sessionRevision == 0 || stats.sessionRevision > lastSessionRevision ||
			!IsEvidenceSide( stats.side ) ) {
			return false;
		}
		for ( int other = index + 1; other < teamStatsCount; ++other ) {
			if ( stats.side == teamStats[ other ].side ) {
				return false;
			}
		}
	}

	if ( droppedEventCount == 0 ) {
		if ( firstDroppedSessionRevision != 0 || lastDroppedSessionRevision != 0 ) {
			return false;
		}
	} else if ( firstDroppedSessionRevision == 0 ||
		lastDroppedSessionRevision < firstDroppedSessionRevision ||
		lastDroppedSessionRevision > lastSessionRevision ) {
		return false;
	}
	if ( !dropCounterSaturated ) {
		const uint64_t acceptedMutationCount = static_cast<uint64_t>( eventCount ) +
			static_cast<uint64_t>( participantStatsCount ) +
			static_cast<uint64_t>( teamStatsCount ) +
			static_cast<uint64_t>( artifactCount ) +
			( seriesLinkSessionRevision != 0 ? UINT64_C( 1 ) : UINT64_C( 0 ) );
		if ( acceptedMutationCount > UINT64_MAX - droppedEventCount ||
			acceptedMutationCount + droppedEventCount >
				UINT64_MAX - droppedParticipantStatsCount ||
			acceptedMutationCount + droppedEventCount + droppedParticipantStatsCount >
				UINT64_MAX - droppedTeamStatsCount ) {
			return false;
		}
		const uint64_t totalMutations = acceptedMutationCount + droppedEventCount +
			droppedParticipantStatsCount + droppedTeamStatsCount;
		if ( totalMutations == UINT64_MAX || evidenceRevision != totalMutations + 1 ) {
			return false;
		}
	}
	return observedRevision <= lastSessionRevision;
}
