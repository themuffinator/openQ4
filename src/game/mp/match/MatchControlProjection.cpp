//----------------------------------------------------------------
// MatchControlProjection.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_CONTROL_PROJECTION_SANITIZER_STANDALONE_TEST )
#include <stddef.h>
#else
#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "MatchControlProjection.h"
#include "MatchControlLocalization.h"
#endif

namespace {

static bool IsUTF8Continuation( unsigned char value ) {
	return value >= 0x80u && value <= 0xbfu;
}

// Returns a complete valid sequence length, zero for an invalid lead/sequence.
// Callers pass a NUL-terminated string, so every look-ahead first checks NUL.
static int ValidUTF8SequenceBytes( const char *text ) {
	if ( text == NULL || text[ 0 ] == '\0' ) {
		return 0;
	}
	const unsigned char first = static_cast<unsigned char>( text[ 0 ] );
	if ( first < 0x80u ) {
		return 1;
	}
	if ( first >= 0xc2u && first <= 0xdfu ) {
		return text[ 1 ] != '\0' &&
			IsUTF8Continuation( static_cast<unsigned char>( text[ 1 ] ) ) ? 2 : 0;
	}
	if ( first >= 0xe0u && first <= 0xefu ) {
		if ( text[ 1 ] == '\0' || text[ 2 ] == '\0' ) {
			return 0;
		}
		const unsigned char second = static_cast<unsigned char>( text[ 1 ] );
		const unsigned char third = static_cast<unsigned char>( text[ 2 ] );
		if ( !IsUTF8Continuation( third ) ) {
			return 0;
		}
		if ( first == 0xe0u ) {
			return second >= 0xa0u && second <= 0xbfu ? 3 : 0;
		}
		if ( first == 0xedu ) {
			return second >= 0x80u && second <= 0x9fu ? 3 : 0;
		}
		return IsUTF8Continuation( second ) ? 3 : 0;
	}
	if ( first >= 0xf0u && first <= 0xf4u ) {
		if ( text[ 1 ] == '\0' || text[ 2 ] == '\0' || text[ 3 ] == '\0' ) {
			return 0;
		}
		const unsigned char second = static_cast<unsigned char>( text[ 1 ] );
		const unsigned char third = static_cast<unsigned char>( text[ 2 ] );
		const unsigned char fourth = static_cast<unsigned char>( text[ 3 ] );
		if ( !IsUTF8Continuation( third ) || !IsUTF8Continuation( fourth ) ) {
			return 0;
		}
		if ( first == 0xf0u ) {
			return second >= 0x90u && second <= 0xbfu ? 4 : 0;
		}
		if ( first == 0xf4u ) {
			return second >= 0x80u && second <= 0x8fu ? 4 : 0;
		}
		return IsUTF8Continuation( second ) ? 4 : 0;
	}
	return 0;
}

// idTech display strings interpret caret escapes after ordinary text has been
// assigned to a GUI state.  Player names are data, not formatting: remove the
// complete bounded escape here so a name cannot recolor adjacent labels, issue
// a renderer command, or inject an icon.  A doubled caret is the engine's
// literal-caret form and is preserved for the normal character path.
static int DisplayEscapeBytes( const char *text ) {
	if ( text == NULL || text[ 0 ] != '^' || text[ 1 ] == '\0' ||
		text[ 1 ] == '^' ) {
		return 0;
	}
	switch ( text[ 1 ] ) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case ':': case '-': case '+': case 'r': case 'R':
			return 2;
		case 'n': case 'N':
			return text[ 2 ] != '\0' ? 3 : 0;
		case 'c': case 'C': case 'i': case 'I':
			return text[ 2 ] != '\0' && text[ 3 ] != '\0' &&
				text[ 4 ] != '\0' ? 5 : 0;
		default:
			return 0;
	}
}

}

int MPMatchControlSanitizeDisplayText( const char *source,
	char *destination, int destinationBytes ) {
	if ( destination == NULL || destinationBytes <= 0 ) {
		return 0;
	}
	destination[ 0 ] = '\0';
	if ( source == NULL ) {
		return 0;
	}

	int readIndex = 0;
	int writeIndex = 0;
	bool pendingSpace = false;
	while ( source[ readIndex ] != '\0' ) {
		const int escapeBytes = DisplayEscapeBytes( source + readIndex );
		if ( escapeBytes > 0 ) {
			readIndex += escapeBytes;
			continue;
		}
		const unsigned char first =
			static_cast<unsigned char>( source[ readIndex ] );
		if ( first < 0x80u ) {
			++readIndex;
			if ( first <= 0x20u || first == 0x7fu ) {
				pendingSpace = writeIndex > 0;
				continue;
			}
			if ( pendingSpace ) {
				if ( writeIndex + 1 >= destinationBytes ) {
					break;
				}
				destination[ writeIndex++ ] = ' ';
				pendingSpace = false;
			}
			if ( writeIndex + 1 >= destinationBytes ) {
				break;
			}
			destination[ writeIndex++ ] = static_cast<char>( first );
			continue;
		}

		int sequenceBytes = ValidUTF8SequenceBytes( source + readIndex );
		if ( sequenceBytes == 0 ) {
			sequenceBytes = 1;
			if ( pendingSpace ) {
				if ( writeIndex + 1 >= destinationBytes ) {
					break;
				}
				destination[ writeIndex++ ] = ' ';
				pendingSpace = false;
			}
			if ( writeIndex + 1 >= destinationBytes ) {
				break;
			}
			destination[ writeIndex++ ] = '?';
			readIndex += sequenceBytes;
			continue;
		}
		if ( pendingSpace ) {
			if ( writeIndex + 1 >= destinationBytes ) {
				break;
			}
			destination[ writeIndex++ ] = ' ';
			pendingSpace = false;
		}
		if ( writeIndex + sequenceBytes >= destinationBytes ) {
			break;
		}
		for ( int offset = 0; offset < sequenceBytes; ++offset ) {
			destination[ writeIndex++ ] = source[ readIndex + offset ];
		}
		readIndex += sequenceBytes;
	}
	destination[ writeIndex ] = '\0';
	return writeIndex;
}

#if !defined( MP_MATCH_CONTROL_PROJECTION_SANITIZER_STANDALONE_TEST )

static_assert( MP_MATCH_VIEW_SCHEMA_VERSION == 4,
	"Match Control projection requires an explicit view-schema review" );
static_assert( MP_MATCH_OP_COUNT == 37,
	"Match Control availability projection requires an opcode review" );
static_assert( MP_MATCH_VIEW_ROLE_COUNT == 6,
	"Match Control role presentation requires an explicit mapping review" );
static_assert( MP_MATCH_BLOCKER_COUNT == 12,
	"Match Control readiness presentation requires a blocker review" );

namespace {

static const int PROJECTION_TEXT_BYTES = 2048;

class mpProjectionText {
public:
	mpProjectionText( char *storage, int storageBytes ) :
		buffer( storage ), capacity( storageBytes ), length( 0 ) {
		if ( buffer != NULL && capacity > 0 ) {
			buffer[ 0 ] = '\0';
		}
	}

	void Append( const char *text ) {
		if ( text == NULL || buffer == NULL || capacity <= 0 ) {
			return;
		}
		int readIndex = 0;
		while ( text[ readIndex ] != '\0' ) {
			int bytes = ValidUTF8SequenceBytes( text + readIndex );
			if ( bytes == 0 ) {
				bytes = 1;
			}
			if ( length + bytes >= capacity ) {
				break;
			}
			if ( bytes == 1 &&
				static_cast<unsigned char>( text[ readIndex ] ) >= 0x80u ) {
				buffer[ length++ ] = '?';
			} else {
				for ( int offset = 0; offset < bytes; ++offset ) {
					buffer[ length++ ] = text[ readIndex + offset ];
				}
			}
			readIndex += bytes;
		}
		buffer[ length ] = '\0';
	}

	void AppendInt( int value ) {
		char number[ 32 ];
		idStr::snPrintf( number, sizeof( number ), "%d", value );
		Append( number );
	}

	void AppendUInt( unsigned int value ) {
		char number[ 32 ];
		idStr::snPrintf( number, sizeof( number ), "%u", value );
		Append( number );
	}

	void AppendU64( unsigned long long value ) {
		char number[ 32 ];
		idStr::snPrintf( number, sizeof( number ), "%llu", value );
		Append( number );
	}

	void AppendHex64( unsigned long long value ) {
		char number[ 32 ];
		idStr::snPrintf( number, sizeof( number ), "%08x%08x",
			static_cast<unsigned int>( value >> 32 ),
			static_cast<unsigned int>( value & 0xffffffffull ) );
		Append( number );
	}

	const char *c_str( void ) const {
		return buffer != NULL ? buffer : "";
	}

private:
	char *buffer;
	int capacity;
	int length;
};

static const char *Localized( const char *key ) {
	const char *localized = key != NULL ? common->GetLocalizedString( key ) : NULL;
	if ( localized == NULL || localized[ 0 ] == '\0' ) {
		localized = common->GetLocalizedString( "#str_42301" );
	}
	return localized != NULL ? localized : "";
}

static void AppendField( mpProjectionText &text, const char *labelKey,
	const char *value ) {
	text.Append( Localized( labelKey ) );
	text.Append( ": " );
	text.Append( value );
}

static void AppendSeparator( mpProjectionText &text ) {
	text.Append( " | " );
}

static const char *SideKey( int side, bool spectatorForNone = false ) {
	if ( side == 0 ) {
		return MPMatchControlTeamKey( MP_MATCH_TEAM_MARINE );
	}
	if ( side == 1 ) {
		return MPMatchControlTeamKey( MP_MATCH_TEAM_STROGG );
	}
	return MPMatchControlTeamKey( spectatorForNone ?
		MP_MATCH_TEAM_SPECTATOR : MP_MATCH_TEAM_NONE );
}

static const char *OperationLabel( mpMatchOperationOpcode_t opcode ) {
	const mpMatchOperationDescriptor_t *descriptor =
		MPMatchOperationDescriptor( opcode );
	return Localized( descriptor != NULL ?
		MPMatchControlLocalizationKey( descriptor->labelLocalizationId ) :
		"#str_42301" );
}

static const char *MatchSideKey( const mpSessionView &view, int side ) {
	const mpMatchViewCommittedRules_t &rules = view.publicState.committedRules;
	for ( int index = 0; rules.present && index < rules.valueCount &&
		index < MP_MATCH_VIEW_MAX_RULE_FIELDS; ++index ) {
		if ( rules.values[ index ].fieldId == MP_RULE_GAME_TYPE &&
			rules.values[ index ].value == GAME_DUEL ) {
			return side == 0 ? "#str_42652" :
				( side == 1 ? "#str_42653" : SideKey( side, true ) );
		}
	}
	return SideKey( side, true );
}

static const char *AvailabilityReason(
	const mpMatchViewOperationAvailability_t *availability ) {
	if ( availability == NULL ) {
		return Localized( "#str_41774" );
	}
	if ( availability->localizationId != MP_MATCH_LOCALIZATION_NONE ) {
		return Localized( MPMatchControlLocalizationKey(
			availability->localizationId ) );
	}
	return Localized( MPMatchControlProtocolReasonKey( availability->reason ) );
}

static bool ProjectionIsUsable( const mpSessionView &view,
	const mpMatchControlModel &model ) {
	const mpMatchViewPublicState_t &state = view.publicState;
	if ( !model.IsReady() || state.sessionId == 0 ||
		state.sessionRevision == 0 || state.controlRevision == 0 ||
		state.viewRevision == 0 || state.recipient.participantId == 0 ||
		state.recipient.slot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ||
		state.recipient.bindingGeneration == 0 ||
		model.SessionId() != state.sessionId ||
		model.ViewRevision() != state.viewRevision ) {
		return false;
	}
	const mpMatchViewRecipient_t &recipient = model.Recipient();
	if ( recipient.participantId != state.recipient.participantId ||
		recipient.slot != state.recipient.slot ||
		recipient.bindingGeneration != state.recipient.bindingGeneration ) {
		return false;
	}
	return MPMatchViewValidate( view, NULL );
}

static bool ProjectionIsManagedMatch( const mpSessionView &view ) {
	const mpMatchViewCommittedRules_t &rules = view.publicState.committedRules;
	if ( !rules.present ) {
		return false;
	}
	for ( int index = 0; index < rules.valueCount; ++index ) {
		const mpMatchViewRuleValue_t &value = rules.values[ index ];
		if ( value.fieldId == MP_RULE_MANAGED_MATCH ) {
			return value.type == MP_MATCH_VIEW_RULE_BOOL && value.value != 0;
		}
	}
	return false;
}

static unsigned long long ProjectionNow( const mpSessionView &view,
	const mpMatchControlProjectionContext_t &context ) {
	return context.displayEngineTimeMsec != 0 ?
		context.displayEngineTimeMsec : view.publicState.clocks.engineTimeMsec;
}

static void AppendCountdown( mpProjectionText &text,
	unsigned long long deadlineMsec, unsigned long long nowMsec ) {
	const unsigned long long remainingMsec = deadlineMsec > nowMsec ?
		deadlineMsec - nowMsec : 0;
	const unsigned long long totalSeconds = ( remainingMsec + 999ull ) / 1000ull;
	const unsigned int minutes = static_cast<unsigned int>( totalSeconds / 60ull );
	const unsigned int seconds = static_cast<unsigned int>( totalSeconds % 60ull );
	char clock[ 32 ];
	idStr::snPrintf( clock, sizeof( clock ), "%u:%02u", minutes, seconds );
	text.Append( clock );
}

static void ResolveParticipantText(
	const mpMatchControlProjectionContext_t &context,
	mpMatchProtocolParticipantId_t participantId,
	char destination[ MP_MATCH_CONTROL_PROJECTION_NAME_BYTES ] ) {
	char callbackText[ MP_MATCH_CONTROL_PROJECTION_NAME_BYTES * 2 ];
	callbackText[ 0 ] = '\0';
	callbackText[ sizeof( callbackText ) - 1 ] = '\0';
	const bool resolved = participantId != MP_MATCH_INVALID_PARTICIPANT_ID &&
		context.resolveParticipantText != NULL &&
		context.resolveParticipantText( context.callbackContext, participantId,
			callbackText, sizeof( callbackText ) );
	callbackText[ sizeof( callbackText ) - 1 ] = '\0';
	if ( !resolved || MPMatchControlSanitizeDisplayText( callbackText,
		destination, MP_MATCH_CONTROL_PROJECTION_NAME_BYTES ) == 0 ) {
		MPMatchControlSanitizeDisplayText( Localized( "#str_42301" ),
			destination, MP_MATCH_CONTROL_PROJECTION_NAME_BYTES );
	}
}

static void ResolveMapText( const mpMatchControlProjectionContext_t &context,
	const char *mapToken,
	char destination[ MP_MATCH_CONTROL_PROJECTION_MAP_BYTES ] ) {
	char callbackText[ MP_MATCH_CONTROL_PROJECTION_MAP_BYTES * 2 ];
	callbackText[ 0 ] = '\0';
	callbackText[ sizeof( callbackText ) - 1 ] = '\0';
	const bool resolved = mapToken != NULL && mapToken[ 0 ] != '\0' &&
		context.resolveMapText != NULL &&
		context.resolveMapText( context.callbackContext, mapToken,
			callbackText, sizeof( callbackText ) );
	callbackText[ sizeof( callbackText ) - 1 ] = '\0';
	if ( !resolved || MPMatchControlSanitizeDisplayText( callbackText,
		destination, MP_MATCH_CONTROL_PROJECTION_MAP_BYTES ) == 0 ) {
		MPMatchControlSanitizeDisplayText( Localized( "#str_42301" ),
			destination, MP_MATCH_CONTROL_PROJECTION_MAP_BYTES );
	}
}

static void AppendPublicRoles( mpProjectionText &text,
	mpMatchViewPublicRoleMask_t roles ) {
	bool appended = false;
	for ( int role = MP_MATCH_VIEW_ROLE_PLAYER;
		role < MP_MATCH_VIEW_ROLE_COUNT; ++role ) {
		const mpMatchViewPublicRole_t publicRole =
			static_cast<mpMatchViewPublicRole_t>( role );
		if ( ( roles & MPMatchViewRoleBit( publicRole ) ) == 0 ) {
			continue;
		}
		if ( appended ) {
			text.Append( ", " );
		}
		text.Append( Localized( MPMatchControlPublicRoleKey( publicRole ) ) );
		appended = true;
	}
	if ( !appended ) {
		text.Append( Localized( MPMatchControlPublicRoleKey(
			MP_MATCH_VIEW_ROLE_NONE ) ) );
	}
}

static const mpMatchViewSeriesMap_t *FindSeriesMap(
	const mpMatchViewSeriesSummary_t &series, int poolIndex ) {
	for ( int index = 0; index < series.mapPoolCount; ++index ) {
		if ( series.mapPool[ index ].poolIndex == poolIndex ) {
			return &series.mapPool[ index ];
		}
	}
	return NULL;
}

static const mpMatchViewSeriesMap_t *FindCurrentSeriesMap(
	const mpMatchViewSeriesSummary_t &series ) {
	if ( series.currentMapNumber == 0 ) {
		return NULL;
	}
	for ( int index = 0; index < series.mapPoolCount; ++index ) {
		if ( series.mapPool[ index ].selectionNumber == series.currentMapNumber ) {
			return &series.mapPool[ index ];
		}
	}
	return NULL;
}

static const char *SeriesProfileKeyForBestOf( int bestOf ) {
	switch ( bestOf ) {
		case 1: return MPMatchControlSeriesProfileKey( MP_SERIES_PROFILE_BEST_OF_ONE );
		case 3: return MPMatchControlSeriesProfileKey( MP_SERIES_PROFILE_BEST_OF_THREE );
		case 5: return MPMatchControlSeriesProfileKey( MP_SERIES_PROFILE_BEST_OF_FIVE );
		default: return "#str_42301";
	}
}

static int ChangedFieldCount( unsigned long long mask ) {
	int count = 0;
	while ( mask != 0 ) {
		mask &= mask - 1ull;
		++count;
	}
	return count;
}

static void BuildPhaseText( char *destination, int destinationBytes,
	const mpSessionView &view ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( Localized( MPMatchControlPhaseKey(
		view.publicState.lifecycle.phase ) ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlRoundKey(
		view.publicState.lifecycle.round ) ) );
	if ( view.publicState.clocks.livePeriod > 0 ) {
		AppendSeparator( text );
		text.Append( Localized( "#str_41671" ) );
		text.Append( ": " );
		text.AppendUInt( view.publicState.clocks.livePeriod );
	}
}

static void BuildPauseText( char *destination, int destinationBytes,
	const mpSessionView &view,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	const mpMatchViewLifecycle_t &lifecycle = view.publicState.lifecycle;
	text.Append( Localized( MPMatchControlPauseStateKey( lifecycle.pauseState ) ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlPauseKindKey( lifecycle.pauseKind ) ) );
	if ( lifecycle.pauseReason != MP_MATCH_VIEW_PAUSE_REASON_NONE ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlPauseReasonKey(
			lifecycle.pauseReason ) ) );
	}
	if ( lifecycle.pauseState != MP_MATCH_VIEW_PAUSE_RUNNING ) {
		AppendSeparator( text );
		text.Append( lifecycle.pauseOwnerSide == 0 || lifecycle.pauseOwnerSide == 1 ?
			Localized( MatchSideKey( view, lifecycle.pauseOwnerSide ) ) :
			Localized( "#str_41779" ) );
	}
	const unsigned long long nowMsec = ProjectionNow( view, context );
	if ( lifecycle.hasResumeDeadline ) {
		AppendSeparator( text );
		AppendCountdown( text, lifecycle.resumeDeadlineEngineMsec, nowMsec );
	} else if ( lifecycle.hasPauseExpiry ) {
		AppendSeparator( text );
		AppendCountdown( text, lifecycle.pauseExpiryEngineMsec, nowMsec );
	}
}

static void BuildReadinessText( char *destination, int destinationBytes,
	const mpSessionView &view ) {
	mpProjectionText text( destination, destinationBytes );
	// Readiness describes admission to the next countdown. The session clears
	// ready flags when play starts; showing those as blockers during the match
	// incorrectly tells players that their running match cannot begin.
	if ( view.publicState.lifecycle.phase != WARMUP &&
		view.publicState.lifecycle.phase != COUNTDOWN ) {
		return;
	}
	const mpMatchViewReadiness_t &readiness = view.publicState.readiness;
	text.Append( Localized( "#str_41708" ) );
	text.Append( ": " );
	text.AppendUInt( readiness.readyCount );
	text.Append( "/" );
	text.AppendUInt( readiness.eligibleCount );

	int readyTeams = 0;
	int knownTeams = 0;
	for ( int index = 0; index < view.publicState.rosterSummaryCount; ++index ) {
		const mpMatchViewRosterSummary_t &summary =
			view.publicState.rosterSummaries[ index ];
		if ( summary.side < 0 || summary.side >= MP_MATCH_VIEW_SIDE_COUNT ) {
			continue;
		}
		++knownTeams;
		if ( summary.teamReady ) {
			++readyTeams;
		}
	}
	if ( knownTeams > 0 ) {
		AppendSeparator( text );
		text.Append( Localized( "#str_41715" ) );
		text.Append( ": " );
		text.AppendInt( readyTeams );
		text.Append( "/" );
		text.AppendInt( knownTeams );
	}

	bool firstBlocker = true;
	for ( int blocker = 0; blocker < MP_MATCH_BLOCKER_COUNT; ++blocker ) {
		const mpMatchReadinessBlocker_t value =
			static_cast<mpMatchReadinessBlocker_t>( blocker );
		if ( ( readiness.blockers & MPMatchReadinessBlockerBit( value ) ) == 0 ) {
			continue;
		}
		text.Append( firstBlocker ? " | " : " " );
		text.Append( Localized( MPMatchControlReadinessBlockerKey( value ) ) );
		firstBlocker = false;
	}
}

static void BuildTimeoutText( char *destination, int destinationBytes,
	const mpSessionView &view ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( Localized( "#str_41778" ) );
	text.Append( ": " );
	for ( int side = 0; side < MP_MATCH_VIEW_SIDE_COUNT; ++side ) {
		if ( side > 0 ) {
			AppendSeparator( text );
		}
		text.Append( Localized( MatchSideKey( view, side ) ) );
		text.Append( " " );
		text.AppendUInt( view.publicState.timeoutBudgets[ side ].remaining );
		text.Append( "/" );
		text.AppendUInt( view.publicState.timeoutBudgets[ side ].configured );
	}
}

static void BuildRecipientText( char *destination, int destinationBytes,
	const mpSessionView &view ) {
	mpProjectionText text( destination, destinationBytes );
	const mpMatchViewRecipient_t &recipient = view.publicState.recipient;
	text.Append( Localized( "#str_41710" ) );
	text.Append( ": " );
	AppendPublicRoles( text, recipient.publicRoleMask );
	AppendSeparator( text );
	text.Append( Localized( MatchSideKey( view,
		recipient.side == MP_MATCH_VIEW_SIDE_NONE && recipient.active ?
			recipient.competitionSide : recipient.side ) ) );
	if ( recipient.active &&
		( view.publicState.lifecycle.phase == WARMUP ||
		  view.publicState.lifecycle.phase == COUNTDOWN ) ) {
		AppendSeparator( text );
		text.Append( Localized( recipient.ready ? "#str_41711" : "#str_41712" ) );
	}
	if ( recipient.queueState != MP_MATCH_VIEW_QUEUE_NONE ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlQueueStateKey(
			recipient.queueState ) ) );
		if ( recipient.hasQueuePosition ) {
			text.Append( " " );
			text.AppendUInt( recipient.queuePosition );
		}
	}
}

static bool ItemTimingOrdinal( const char *token, const char *semanticToken,
		int &ordinal ) {
	ordinal = 0;
	if ( token == NULL || semanticToken == NULL ) {
		return false;
	}
	const int prefixLength = static_cast<int>( strlen( semanticToken ) );
	if ( idStr::Cmpn( token, semanticToken, prefixLength ) != 0 ||
		token[ prefixLength ] != '.' || token[ prefixLength + 1 ] < '1' ||
		token[ prefixLength + 1 ] > '9' ) {
		return false;
	}
	int value = 0;
	for ( int index = prefixLength + 1; token[ index ] != '\0'; ++index ) {
		if ( token[ index ] < '0' || token[ index ] > '9' || value > 9999 ) {
			return false;
		}
		value = value * 10 + ( token[ index ] - '0' );
	}
	if ( value <= 0 ) {
		return false;
	}
	ordinal = value;
	return true;
}

static const char *ItemTimingTokenKey( const char *token, int &ordinal ) {
	ordinal = 0;
	if ( token == NULL ) {
		return "#str_42748";
	}
	if ( strcmp( token, "quad" ) == 0 ||
		ItemTimingOrdinal( token, "quad", ordinal ) ) {
		return "#str_42741";
	}
	if ( strcmp( token, "haste" ) == 0 ||
		ItemTimingOrdinal( token, "haste", ordinal ) ) {
		return "#str_42742";
	}
	if ( strcmp( token, "regeneration" ) == 0 ||
		ItemTimingOrdinal( token, "regeneration", ordinal ) ) {
		return "#str_42743";
	}
	if ( strcmp( token, "invisibility" ) == 0 ||
		ItemTimingOrdinal( token, "invisibility", ordinal ) ) {
		return "#str_42744";
	}
	if ( strcmp( token, "mega_health" ) == 0 ||
		ItemTimingOrdinal( token, "mega_health", ordinal ) ) {
		return "#str_42745";
	}
	if ( strcmp( token, "large_armor" ) == 0 ||
		ItemTimingOrdinal( token, "large_armor", ordinal ) ) {
		return "#str_42746";
	}
	if ( strcmp( token, "small_armor" ) == 0 ||
		ItemTimingOrdinal( token, "small_armor", ordinal ) ) {
		return "#str_42747";
	}
	// Adapter tokens are machine identifiers and are never rendered directly.
	return "#str_42748";
}

static void BuildItemTimingText( char *destination, int destinationBytes,
		const mpSessionView &view ) {
	mpProjectionText text( destination, destinationBytes );
	if ( view.itemTimingCount == 0 ) {
		return;
	}
	text.Append( Localized( "#str_42740" ) );
	text.Append( ": " );
	for ( int index = 0; index < view.itemTimingCount; ++index ) {
		const mpMatchViewItemTiming_t &timing = view.itemTimings[ index ];
		if ( index > 0 ) {
			text.Append( ", " );
		}
		int ordinal = 0;
		text.Append( Localized( ItemTimingTokenKey( timing.token, ordinal ) ) );
		if ( ordinal > 0 ) {
			text.Append( " " );
			text.AppendUInt( static_cast<unsigned int>( ordinal ) );
		}
		text.Append( " " );
		if ( timing.available ) {
			text.Append( Localized( "#str_42530" ) );
		} else {
			AppendCountdown( text, timing.matchDeadlineMsec,
				view.publicState.clocks.matchTimeMsec );
		}
	}
}

static void BuildProposalText( char *destination, int destinationBytes,
	const mpMatchViewProposalSummary_t &proposal,
	const mpSessionView &view,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	if ( !proposal.present ) {
		text.Append( Localized( "#str_42302" ) );
		return;
	}
	text.Append( OperationLabel( proposal.opcode ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlProposalScopeKey( proposal.scope ) ) );
	if ( proposal.scope == MP_MATCH_VIEW_PROPOSAL_SIDE ) {
		text.Append( " " );
		text.Append( Localized( SideKey( proposal.side ) ) );
	}
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlBallotKey( MP_MATCH_VIEW_BALLOT_YES ) ) );
	text.Append( " " );
	text.AppendUInt( proposal.yesCount );
	text.Append( "/" );
	text.AppendUInt( proposal.requiredYesCount );
	text.Append( ", " );
	text.Append( Localized( MPMatchControlBallotKey( MP_MATCH_VIEW_BALLOT_NO ) ) );
	text.Append( " " );
	text.AppendUInt( proposal.noCount );
	AppendSeparator( text );
	AppendCountdown( text, proposal.expiresAtEngineMsec,
		ProjectionNow( view, context ) );
	if ( proposal.recipientBallot != MP_MATCH_VIEW_BALLOT_NONE ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlBallotKey(
			proposal.recipientBallot ) ) );
	}
}

static void BuildRulesText( char *destination, int destinationBytes,
	const mpMatchViewCommittedRules_t &rules ) {
	mpProjectionText text( destination, destinationBytes );
	if ( !rules.present ) {
		text.Append( Localized( "#str_42302" ) );
		return;
	}
	text.Append( Localized( MPMatchControlMatchProfileKey(
		static_cast<mpMatchProfileId_t>( rules.profileId ) ) ) );
	if ( rules.customized ) {
		AppendSeparator( text );
		text.Append( Localized( "#str_42700" ) );
	}
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlRulesBoundaryKey( rules.boundary ) ) );
	AppendSeparator( text );
	text.AppendUInt( rules.revision );
	text.Append( "/" );
	text.AppendHex64( rules.digest );
}

static void BuildStagedRulesText( char *destination, int destinationBytes,
	const mpMatchViewStagedRules_t &rules ) {
	mpProjectionText text( destination, destinationBytes );
	if ( !rules.present ) {
		text.Append( Localized( "#str_42302" ) );
		return;
	}
	text.Append( Localized( MPMatchControlMatchProfileKey(
		static_cast<mpMatchProfileId_t>( rules.profileId ) ) ) );
	if ( rules.customized ) {
		AppendSeparator( text );
		text.Append( Localized( "#str_42700" ) );
	}
	AppendSeparator( text );
	text.AppendUInt( rules.revision );
	text.Append( "/" );
	text.AppendHex64( rules.digest );
	AppendSeparator( text );
	text.AppendInt( ChangedFieldCount( rules.changedFieldMask ) );
}

static void BuildSeriesText( char *destination, int destinationBytes,
	const mpMatchViewSeriesSummary_t &series,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	if ( !series.present ) {
		text.Append( Localized( "#str_42302" ) );
		return;
	}
	text.Append( Localized( MPMatchControlSeriesStateKey( series.state ) ) );
	AppendSeparator( text );
	text.Append( Localized( SeriesProfileKeyForBestOf( series.bestOf ) ) );
	AppendSeparator( text );
	text.Append( Localized( "#str_41777" ) );
	text.Append( ": " );
	text.AppendUInt( series.wins[ 0 ] );
	text.Append( "-" );
	text.AppendUInt( series.wins[ 1 ] );

	char mapName[ MP_MATCH_CONTROL_PROJECTION_MAP_BYTES ];
	const mpMatchViewSeriesMap_t *currentMap = FindCurrentSeriesMap( series );
	if ( currentMap != NULL ) {
		ResolveMapText( context, currentMap->mapToken, mapName );
		AppendSeparator( text );
		AppendField( text, "#str_41775", mapName );
	} else if ( series.hasNextMap ) {
		ResolveMapText( context, series.nextMap, mapName );
		AppendSeparator( text );
		AppendField( text, "#str_41776", mapName );
	}
	if ( series.hasVetoTurn ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlVetoActionKey(
			series.vetoTurnAction ) ) );
		text.Append( " " );
		text.Append( Localized( SideKey( series.vetoTurnSide ) ) );
	}
}

static void BuildEvidenceText( char *destination, int destinationBytes,
	const mpMatchViewEvidenceSummary_t &evidence ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( Localized( MPMatchControlEvidenceStateKey(
		evidence.evidenceState ) ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlReportStateKey(
		evidence.reportState ) ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlMVDStateKey( evidence.mvdState ) ) );
	AppendSeparator( text );
	text.AppendUInt( evidence.eventCount );
	if ( evidence.droppedRecordCount > 0 ||
		evidence.droppedRecordCountSaturated ) {
		text.Append( "/" );
		text.AppendUInt( evidence.droppedRecordCount );
		if ( evidence.droppedRecordCountSaturated ) {
			text.Append( "+" );
		}
	}
}

static void BuildResultText( char *destination, int destinationBytes,
	const mpSessionView &view,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	if ( context.localError != NULL &&
		context.localError->reason != MP_MATCH_CONTROL_ERROR_NONE ) {
		text.Append( Localized( MPMatchControlErrorReasonKey(
			context.localError->reason ) ) );
		if ( context.localError->protocolReason != MP_MATCH_PROTOCOL_REASON_NONE ) {
			AppendSeparator( text );
			text.Append( Localized( MPMatchControlProtocolReasonKey(
				context.localError->protocolReason ) ) );
		}
		return;
	}
	const mpMatchOperationResult_t *result = context.authoritativeResult;
	if ( result == NULL || result->sessionId != view.publicState.sessionId ) {
		return;
	}
	text.Append( OperationLabel( result->opcode ) );
	AppendSeparator( text );
	text.Append( Localized( MPMatchControlOperationResultStatusKey(
		result->status ) ) );
	if ( result->localizationId != MP_MATCH_LOCALIZATION_NONE ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlLocalizationKey(
			result->localizationId ) ) );
	} else if ( result->reason != MP_MATCH_PROTOCOL_REASON_NONE ) {
		AppendSeparator( text );
		text.Append( Localized( MPMatchControlProtocolReasonKey(
			result->reason ) ) );
	}
}

static void BuildLastResultText( char *destination, int destinationBytes,
	const mpMatchViewTerminalResult_t &result ) {
	mpProjectionText text( destination, destinationBytes );
	if ( result.outcome == MP_MATCH_VIEW_RESULT_NONE ) {
		return;
	}
	char outcome[ 512 ];
	mpProjectionText outcomeText( outcome, sizeof( outcome ) );
	if ( result.outcome == MP_MATCH_VIEW_RESULT_ABORTED ) {
		outcomeText.Append( Localized( "#str_42870" ) );
	} else if ( result.outcome == MP_MATCH_VIEW_RESULT_DRAW ) {
		outcomeText.Append( Localized( "#str_42871" ) );
	} else if ( result.winnerSide == 0 || result.winnerSide == 1 ) {
		outcomeText.Append( Localized( result.winnerSide == 0 ? "#str_201012" : "#str_201013" ) );
	} else {
		// The accepted result owns this name. A current roster lookup could
		// silently substitute a renamed winner or a new occupant of their slot.
		char winner[ 384 ];
		idStr::snPrintf( winner, sizeof( winner ), Localized( "#str_41313" ), result.winnerName );
		outcomeText.Append( winner );
	}
	if ( result.outcome == MP_MATCH_VIEW_RESULT_FORFEIT ) {
		AppendSeparator( outcomeText );
		outcomeText.Append( Localized( "#str_41315" ) );
	}
	char line[ 768 ];
	idStr::snPrintf( line, sizeof( line ), Localized( "#str_42873" ), outcome );
	text.Append( line );
}

static void BuildStatusLines( char *destination, int destinationBytes,
	const mpSessionView &view,
	const mpMatchControlProjectionContext_t &context ) {
	char phase[ 384 ];
	char readiness[ 768 ];
	char pause[ 512 ];
	char recipient[ 512 ];
	char timeouts[ 384 ];
	char lastResult[ 768 ];
	BuildPhaseText( phase, sizeof( phase ), view );
	BuildLastResultText( lastResult, sizeof( lastResult ), view.publicState.terminalResult );
	BuildReadinessText( readiness, sizeof( readiness ), view );
	BuildPauseText( pause, sizeof( pause ), view, context );
	BuildRecipientText( recipient, sizeof( recipient ), view );
	BuildTimeoutText( timeouts, sizeof( timeouts ), view );
	mpProjectionText text( destination, destinationBytes );
	text.Append( phase );
	if ( lastResult[ 0 ] != '\0' ) {
		text.Append( "\n" );
		text.Append( lastResult );
	}
	if ( readiness[ 0 ] != '\0' ) {
		text.Append( "\n" );
		text.Append( readiness );
	}
	text.Append( "\n" );
	text.Append( pause );
	text.Append( "\n" );
	text.Append( recipient );
	text.Append( "\n" );
	text.Append( timeouts );
}

static void BuildTeamRowText( char *destination, int destinationBytes,
	const mpMatchControlTeamRow_t &row,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	char participantName[ MP_MATCH_CONTROL_PROJECTION_NAME_BYTES ];
	switch ( row.kind ) {
		case MP_MATCH_CONTROL_TEAM_ROW_SIDE:
			text.Append( Localized( SideKey( row.side ) ) );
			text.Append( "\t" );
			text.Append( Localized( row.teamReady ? "#str_41711" : "#str_41712" ) );
			text.Append( "\t" );
			text.Append( Localized( row.teamLocked ? "#str_41735" : "#str_41734" ) );
			break;
		case MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT:
			ResolveParticipantText( context, row.participantId, participantName );
			text.Append( participantName );
			text.Append( "\t" );
			text.Append( Localized( SideKey( row.side, true ) ) );
			text.Append( "\t" );
			AppendPublicRoles( text, row.publicRoleMask );
			break;
		case MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT:
			if ( row.occupied ) {
				ResolveParticipantText( context, row.participantId, participantName );
				text.Append( participantName );
			} else {
				text.Append( Localized( "#str_42302" ) );
			}
			text.Append( "\t" );
			text.Append( Localized( SideKey( row.side ) ) );
			text.Append( "\t" );
			text.Append( Localized( MPMatchControlRosterRoleKey( row.rosterRole ) ) );
			break;
		case MP_MATCH_CONTROL_TEAM_ROW_INVITATION:
			ResolveParticipantText( context, row.participantId, participantName );
			text.Append( participantName );
			text.Append( "\t" );
			text.Append( Localized( SideKey( row.side ) ) );
			text.Append( "\t" );
			text.Append( Localized( MPMatchControlRosterRoleKey( row.rosterRole ) ) );
			break;
		case MP_MATCH_CONTROL_TEAM_ROW_QUEUE_ENTRY:
			ResolveParticipantText( context, row.participantId, participantName );
			text.Append( participantName );
			text.Append( "\t" );
			text.Append( Localized( MPMatchControlQueueStateKey( row.queueState ) ) );
			text.Append( "\t" );
			text.AppendUInt( row.queuePosition );
			break;
		case MP_MATCH_CONTROL_TEAM_ROW_KIND_COUNT:
		default:
			text.Append( Localized( "#str_42301" ) );
			break;
	}
}

static void BuildReplacementRowText( char *destination, int destinationBytes,
	const mpMatchControlReplacementRow_t &row,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	char participantName[ MP_MATCH_CONTROL_PROJECTION_NAME_BYTES ];
	ResolveParticipantText( context, row.participantId, participantName );
	text.Append( participantName );
	// This narrow selector chooses a participant; side and role are shown in
	// the adjacent roster table and remain in the structured selection model.
}

static void BuildProposalTemplateRowText( char *destination, int destinationBytes,
	const mpMatchControlProposalTemplateRow_t &row ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( OperationLabel( row.opcode ) );
	text.Append( "\t" );
	text.Append( Localized( MPMatchControlProposalScopeKey(
		row.targetKind == MP_MATCH_CONTROL_PROPOSAL_TARGET_OWN_SIDE ?
			MP_MATCH_VIEW_PROPOSAL_SIDE : MP_MATCH_VIEW_PROPOSAL_GLOBAL ) ) );
}

static void BuildProfileRowText( char *destination, int destinationBytes,
	const mpMatchControlProfileRow_t &row ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( Localized( MPMatchControlMatchProfileKey( row.profileId ) ) );
}

static void BuildRuleRowText( char *destination, int destinationBytes,
	const mpMatchControlRuleRow_t &row ) {
	mpProjectionText text( destination, destinationBytes );
	text.Append( Localized( MPMatchControlRuleFieldKey( row.fieldId ) ) );
	text.Append( "\t" );
	text.Append( Localized( MPMatchControlRuleTypeKey( row.type ) ) );
	text.Append( "\t" );
	text.AppendInt( row.committedValue );
	if ( row.hasStagedValue ) {
		text.Append( " -> " );
		text.AppendInt( row.stagedValue );
	}
}

static void BuildSeriesMapRowText( char *destination, int destinationBytes,
	const mpMatchControlSeriesMapRow_t &row,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	char mapName[ MP_MATCH_CONTROL_PROJECTION_MAP_BYTES ];
	ResolveMapText( context, row.map.mapToken, mapName );
	text.Append( mapName );
	text.Append( "\t" );
	text.Append( Localized( MPMatchControlMapDispositionKey(
		row.map.disposition ) ) );
	if ( row.map.selectedBySide == 0 || row.map.selectedBySide == 1 ) {
		text.Append( "\t" );
		text.Append( Localized( SideKey( row.map.selectedBySide ) ) );
	}
	if ( row.map.hasStartingGameSide ) {
		text.Append( "\t" );
		text.Append( Localized( SideKey( row.map.startingGameSide ) ) );
	}
}

static void BuildSeriesHistoryRowText( char *destination, int destinationBytes,
	const mpMatchControlSeriesHistoryRow_t &row,
	const mpMatchViewSeriesSummary_t &series,
	const mpMatchControlProjectionContext_t &context ) {
	mpProjectionText text( destination, destinationBytes );
	char mapName[ MP_MATCH_CONTROL_PROJECTION_MAP_BYTES ];
	if ( row.kind == MP_MATCH_CONTROL_HISTORY_VETO ) {
		const mpMatchViewSeriesMap_t *map = FindSeriesMap( series,
			row.veto.mapPoolIndex );
		ResolveMapText( context, map != NULL ? map->mapToken : NULL, mapName );
		text.Append( mapName );
		text.Append( "\t" );
		text.Append( Localized( MPMatchControlVetoActionKey( row.veto.action ) ) );
		text.Append( "\t" );
		text.Append( Localized( SideKey( row.veto.actingSide ) ) );
		if ( row.veto.hasSelectedGameSide ) {
			text.Append( "\t" );
			text.Append( Localized( SideKey( row.veto.selectedGameSide ) ) );
		}
		return;
	}
	if ( row.kind == MP_MATCH_CONTROL_HISTORY_MAP ) {
		const mpMatchViewSeriesMap_t *map = FindSeriesMap( series,
			row.map.mapPoolIndex );
		ResolveMapText( context, map != NULL ? map->mapToken : NULL, mapName );
		text.Append( mapName );
		text.Append( "\t" );
		text.Append( Localized( MPMatchControlMapOutcomeKey( row.map.outcome ) ) );
		text.Append( "\t" );
		text.AppendUInt( row.map.scores[ 0 ] );
		text.Append( "-" );
		text.AppendUInt( row.map.scores[ 1 ] );
		if ( row.map.winnerSide == 0 || row.map.winnerSide == 1 ) {
			text.Append( "\t" );
			text.Append( Localized( SideKey( row.map.winnerSide ) ) );
		}
		return;
	}
	text.Append( Localized( "#str_42301" ) );
}

static void BuildEvidenceRowText( char *destination, int destinationBytes,
	const mpMatchControlEvidenceRow_t &row ) {
	mpProjectionText text( destination, destinationBytes );
	if ( row.kind == MP_MATCH_CONTROL_EVIDENCE_SUMMARY ) {
		BuildEvidenceText( destination, destinationBytes, row.summary );
		return;
	}
	if ( row.kind == MP_MATCH_CONTROL_EVIDENCE_RECENT_EVENT ) {
		text.Append( Localized( MPMatchControlEvidenceEventKindKey(
			row.recentEventKind ) ) );
		return;
	}
	text.Append( Localized( "#str_42301" ) );
}

static void SetListSelection( idUserInterface &gui, const char *listName,
	int selected, int count ) {
	char stateName[ 96 ];
	idStr::snPrintf( stateName, sizeof( stateName ), "%s_sel_0", listName );
	gui.SetStateInt( stateName,
		selected >= 0 && selected < count ? selected : -1 );
}

static void DeleteFirstUnusedListItem( idUserInterface &gui,
	const char *listName, int count ) {
	char stateName[ 96 ];
	idStr::snPrintf( stateName, sizeof( stateName ), "%s_item_%d",
		listName, count );
	gui.DeleteStateVar( stateName );
}

static void SetListItem( idUserInterface &gui, const char *listName,
	int index, const char *value ) {
	char stateName[ 96 ];
	idStr::snPrintf( stateName, sizeof( stateName ), "%s_item_%d",
		listName, index );
	gui.SetStateString( stateName, value );
}

static void ProjectLists( idUserInterface &gui, const mpSessionView &view,
	const mpMatchControlModel &model,
	const mpMatchControlProjectionContext_t &context ) {
	char rowText[ MP_MATCH_CONTROL_PROJECTION_ROW_BYTES ];

	for ( int index = 0; index < model.TeamRowCount(); ++index ) {
		const mpMatchControlTeamRow_t *row = model.TeamRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildTeamRowText( rowText, sizeof( rowText ), *row, context );
		SetListItem( gui, "match_team_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_team_rows", model.TeamRowCount() );
	SetListSelection( gui, "match_team_rows", model.SelectedTeamRow(),
		model.TeamRowCount() );

	for ( int index = 0; index < model.ReplacementRowCount(); ++index ) {
		const mpMatchControlReplacementRow_t *row = model.ReplacementRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildReplacementRowText( rowText, sizeof( rowText ), *row, context );
		SetListItem( gui, "match_replacement_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_replacement_rows",
		model.ReplacementRowCount() );
	SetListSelection( gui, "match_replacement_rows",
		model.SelectedReplacementRow(), model.ReplacementRowCount() );

	for ( int index = 0; index < model.ProposalTemplateRowCount(); ++index ) {
		const mpMatchControlProposalTemplateRow_t *row =
			model.ProposalTemplateRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildProposalTemplateRowText( rowText, sizeof( rowText ), *row );
		SetListItem( gui, "match_proposal_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_proposal_rows",
		model.ProposalTemplateRowCount() );
	SetListSelection( gui, "match_proposal_rows",
		model.SelectedProposalTemplateRow(), model.ProposalTemplateRowCount() );

	for ( int index = 0; index < model.ProfileRowCount(); ++index ) {
		const mpMatchControlProfileRow_t *row = model.ProfileRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildProfileRowText( rowText, sizeof( rowText ), *row );
		SetListItem( gui, "match_profile_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_profile_rows", model.ProfileRowCount() );
	SetListSelection( gui, "match_profile_rows", model.SelectedProfileRow(),
		model.ProfileRowCount() );

	for ( int index = 0; index < model.RuleRowCount(); ++index ) {
		const mpMatchControlRuleRow_t *row = model.RuleRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildRuleRowText( rowText, sizeof( rowText ), *row );
		SetListItem( gui, "match_rule_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_rule_rows", model.RuleRowCount() );
	SetListSelection( gui, "match_rule_rows", model.SelectedRuleRow(),
		model.RuleRowCount() );

	for ( int index = 0; index < model.SeriesMapRowCount(); ++index ) {
		const mpMatchControlSeriesMapRow_t *row = model.SeriesMapRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildSeriesMapRowText( rowText, sizeof( rowText ), *row, context );
		SetListItem( gui, "match_series_map_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_series_map_rows",
		model.SeriesMapRowCount() );
	SetListSelection( gui, "match_series_map_rows", model.SelectedSeriesMapRow(),
		model.SeriesMapRowCount() );

	for ( int index = 0; index < model.SeriesHistoryRowCount(); ++index ) {
		const mpMatchControlSeriesHistoryRow_t *row =
			model.SeriesHistoryRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildSeriesHistoryRowText( rowText, sizeof( rowText ), *row,
			view.publicState.series, context );
		SetListItem( gui, "match_series_history_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_series_history_rows",
		model.SeriesHistoryRowCount() );
	SetListSelection( gui, "match_series_history_rows", -1,
		model.SeriesHistoryRowCount() );

	for ( int index = 0; index < model.EvidenceRowCount(); ++index ) {
		const mpMatchControlEvidenceRow_t *row = model.EvidenceRow( index );
		if ( row == NULL ) {
			break;
		}
		BuildEvidenceRowText( rowText, sizeof( rowText ), *row );
		SetListItem( gui, "match_evidence_rows", index, rowText );
	}
	DeleteFirstUnusedListItem( gui, "match_evidence_rows",
		model.EvidenceRowCount() );
	SetListSelection( gui, "match_evidence_rows", -1,
		model.EvidenceRowCount() );
}

typedef struct mpProjectionAvailabilityState_s {
	const char *prefix;
	mpMatchOperationOpcode_t opcode;
} mpProjectionAvailabilityState_t;

static const mpProjectionAvailabilityState_t AVAILABILITY_STATES[] = {
	{ "ready_set", MP_MATCH_OP_READY_SET },
	{ "team_ready_set", MP_MATCH_OP_TEAM_READY_SET },
	{ "force_ready", MP_MATCH_OP_FORCE_READY },
	{ "team_join", MP_MATCH_OP_TEAM_JOIN },
	{ "team_lock_set", MP_MATCH_OP_TEAM_LOCK_SET },
	{ "queue_join", MP_MATCH_OP_QUEUE_JOIN },
	{ "queue_defer", MP_MATCH_OP_QUEUE_DEFER },
	{ "queue_leave", MP_MATCH_OP_QUEUE_LEAVE },
	{ "roster_leave", MP_MATCH_OP_ROSTER_LEAVE },
	{ "timeout_request", MP_MATCH_OP_TIMEOUT_REQUEST },
	{ "tech_pause_request", MP_MATCH_OP_TECH_PAUSE_REQUEST },
	{ "resume_request", MP_MATCH_OP_RESUME_REQUEST },
	{ "ref_authenticate", MP_MATCH_OP_REF_AUTHENTICATE },
	{ "ref_logout", MP_MATCH_OP_REF_LOGOUT },
	{ "rules_select_profile", MP_MATCH_OP_RULES_SELECT_PROFILE },
	{ "rules_stage_field", MP_MATCH_OP_RULES_STAGE_FIELD },
	{ "rules_commit", MP_MATCH_OP_RULES_COMMIT },
	{ "rules_discard", MP_MATCH_OP_RULES_DISCARD },
	{ "proposal_create", MP_MATCH_OP_PROPOSAL_CREATE },
	{ "proposal_cast", MP_MATCH_OP_PROPOSAL_CAST },
	{ "proposal_cancel", MP_MATCH_OP_PROPOSAL_CANCEL },
	{ "roster_invite", MP_MATCH_OP_ROSTER_INVITE },
	{ "roster_accept", MP_MATCH_OP_ROSTER_ACCEPT },
	{ "roster_remove", MP_MATCH_OP_ROSTER_REMOVE },
	{ "roster_substitute", MP_MATCH_OP_ROSTER_SUBSTITUTE },
	{ "role_assign", MP_MATCH_OP_ROLE_ASSIGN },
	{ "broadcaster_set", MP_MATCH_OP_BROADCASTER_SET },
	{ "series_stage_profile", MP_MATCH_OP_SERIES_STAGE_PROFILE },
	{ "series_start", MP_MATCH_OP_SERIES_START },
	{ "series_cancel", MP_MATCH_OP_SERIES_CANCEL },
	{ "series_advance", MP_MATCH_OP_SERIES_ADVANCE },
	{ "veto_select", MP_MATCH_OP_VETO_SELECT },
	{ "forfeit", MP_MATCH_OP_FORFEIT },
	{ "abort", MP_MATCH_OP_ABORT },
	{ "participant_remove", MP_MATCH_OP_PARTICIPANT_REMOVE },
	{ "series_contestant_bind", MP_MATCH_OP_SERIES_CONTESTANT_BIND }
};

static_assert( sizeof( AVAILABILITY_STATES ) /
	sizeof( AVAILABILITY_STATES[ 0 ] ) == MP_MATCH_OP_COUNT - 1,
	"Every append-only match operation needs one Match Control availability state" );

static void ProjectAvailability( idUserInterface &gui,
	const mpMatchControlModel *model ) {
	char availableState[ 96 ];
	char reasonState[ 96 ];
	for ( int index = 0; index < static_cast<int>( sizeof( AVAILABILITY_STATES ) /
		sizeof( AVAILABILITY_STATES[ 0 ] ) ); ++index ) {
		const mpProjectionAvailabilityState_t &state = AVAILABILITY_STATES[ index ];
		const mpMatchViewOperationAvailability_t *availability = model != NULL ?
			model->OperationAvailability( state.opcode ) : NULL;
		idStr::snPrintf( availableState, sizeof( availableState ),
			"match_op_%s_available", state.prefix );
		idStr::snPrintf( reasonState, sizeof( reasonState ),
			"match_op_%s_reason", state.prefix );
		const bool serverAvailable = availability != NULL &&
			availability->available &&
			availability->reason == MP_MATCH_PROTOCOL_REASON_OK;
		const bool contextAccepted = model != NULL &&
			model->OperationContextAccepted( state.opcode );
		const bool available = serverAvailable && contextAccepted;
		gui.SetStateInt( availableState, available ? 1 : 0 );
		gui.SetStateString( reasonState,
			serverAvailable && !contextAccepted ? Localized( "#str_42387" ) :
			AvailabilityReason( availability ) );
	}
}

static void InitializeChoiceStates( idUserInterface &gui,
	const mpMatchViewSeriesSummary_t *series = NULL ) {
	gui.SetStateString( "match_role_choice", "1" );
	gui.SetStateString( "match_proposal_scope_choice", "global" );
	const int bestOf = series != NULL && series->present ? series->bestOf : 3;
	gui.SetStateString( "match_series_profile_choice", bestOf == 1 ?
		"best_of_one" : bestOf == 5 ? "best_of_five" : "best_of_three" );
	gui.SetStateString( "match_rule_value", "" );
}

static void ProjectActionSide( idUserInterface &gui,
		const mpMatchControlModel *model ) {
	const bool side0Enabled = model != NULL && model->CanChooseActionSide( 0 );
	const bool side1Enabled = model != NULL && model->CanChooseActionSide( 1 );
	const bool competitionLabels = model != NULL &&
		model->ActionSideUsesCompetitionLabels();
	gui.SetStateString( "match_action_side_label", Localized( "#str_42651" ) );
	gui.SetStateString( "match_action_side_0_label", Localized(
		competitionLabels ? "#str_42652" : SideKey( 0 ) ) );
	gui.SetStateString( "match_action_side_1_label", Localized(
		competitionLabels ? "#str_42653" : SideKey( 1 ) ) );
	gui.SetStateInt( "match_action_side_visible",
		( side0Enabled || side1Enabled ) ? 1 : 0 );
	gui.SetStateInt( "match_action_side_0_enabled", side0Enabled ? 1 : 0 );
	gui.SetStateInt( "match_action_side_1_enabled", side1Enabled ? 1 : 0 );
	const int selected = model != NULL ?
		static_cast<int>( model->ActionSideChoice() ) :
		static_cast<int>( MP_MATCH_CONTROL_SIDE_CHOICE_NONE );
	gui.SetStateInt( "match_action_side_0_selected", selected == 0 ? 1 : 0 );
	gui.SetStateInt( "match_action_side_1_selected", selected == 1 ? 1 : 0 );
}

static int SelectedSide( const mpMatchControlModel &model ) {
	const mpMatchControlTeamRow_t *selected = model.TeamRow(
		model.SelectedTeamRow() );
	return selected != NULL && selected->side >= 0 &&
		selected->side < MP_MATCH_VIEW_SIDE_COUNT ? selected->side :
		MP_MATCH_VIEW_SIDE_NONE;
}

static const mpMatchControlTeamRow_t *SelectedSideSummaryRow(
	const mpMatchControlModel &model, int side ) {
	for ( int index = 0; index < model.TeamRowCount(); ++index ) {
		const mpMatchControlTeamRow_t *row = model.TeamRow( index );
		if ( row != NULL && row->kind == MP_MATCH_CONTROL_TEAM_ROW_SIDE &&
			row->side == side ) {
			return row;
		}
	}
	return NULL;
}

static void ClearList( idUserInterface &gui, const char *listName ) {
	DeleteFirstUnusedListItem( gui, listName, 0 );
	SetListSelection( gui, listName, -1, 0 );
}

}

void mpMatchControlProjectionContext_s::Clear( void ) {
	callbackContext = NULL;
	resolveParticipantText = NULL;
	resolveMapText = NULL;
	localOperatorVisible = false;
	displayEngineTimeMsec = 0;
	initializeChoices = false;
	authoritativeResult = NULL;
	localError = NULL;
}

void MPMatchControlClearMenu( idUserInterface &gui, bool initializeChoices ) {
	gui.SetStateInt( "match_surface_available", 0 );
	static const char *const scalarStates[] = {
		"match_phase", "match_phase_short", "match_status_lines", "match_ready_action",
		"match_team_lock_action", "match_broadcaster_action",
		"match_action_side_label", "match_action_side_0_label",
		"match_action_side_1_label",
		"match_global_proposal", "match_side_proposal", "match_rules_summary",
		"match_staged_summary", "match_series_summary", "match_evidence_summary",
		"match_result_message"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( scalarStates ) /
		sizeof( scalarStates[ 0 ] ) ); ++index ) {
		gui.SetStateString( scalarStates[ index ], "" );
	}
	gui.SetStateInt( "match_broadcaster_control_visible", 0 );
	gui.SetStateInt( "match_referee_authenticated", 0 );
	ProjectActionSide( gui, NULL );
	ClearList( gui, "match_team_rows" );
	ClearList( gui, "match_replacement_rows" );
	ClearList( gui, "match_proposal_rows" );
	ClearList( gui, "match_profile_rows" );
	ClearList( gui, "match_rule_rows" );
	ClearList( gui, "match_series_map_rows" );
	ClearList( gui, "match_series_history_rows" );
	ClearList( gui, "match_evidence_rows" );
	ProjectAvailability( gui, NULL );
	if ( initializeChoices ) {
		InitializeChoiceStates( gui );
	}
}

void MPMatchControlProjectMenu( idUserInterface &gui,
	const mpSessionView &acceptedView,
	const mpMatchControlModel &model,
	const mpMatchControlProjectionContext_t &context ) {
	if ( !ProjectionIsUsable( acceptedView, model ) ) {
		MPMatchControlClearMenu( gui, context.initializeChoices );
		return;
	}
	if ( context.initializeChoices ) {
		InitializeChoiceStates( gui, &acceptedView.publicState.series );
	}

	char value[ PROJECTION_TEXT_BYTES ];
	BuildPhaseText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_phase", value );
	gui.SetStateString( "match_phase_short", Localized(
		MPMatchControlPhaseKey( acceptedView.publicState.lifecycle.phase ) ) );
	BuildStatusLines( value, sizeof( value ), acceptedView, context );
	gui.SetStateString( "match_status_lines", value );
	gui.SetStateString( "match_ready_action", Localized(
		acceptedView.publicState.recipient.ready ? "#str_41714" : "#str_41713" ) );
	ProjectActionSide( gui, &model );

	const int selectedSide = SelectedSide( model );
	const mpMatchControlTeamRow_t *sideRow =
		SelectedSideSummaryRow( model, selectedSide );
	gui.SetStateString( "match_team_lock_action", sideRow != NULL ?
		Localized( sideRow->teamLocked ? "#str_41735" : "#str_41734" ) : "" );

	gui.SetStateInt( "match_broadcaster_control_visible",
		context.localOperatorVisible ? 1 : 0 );
	const mpMatchControlTeamRow_t *selectedRow = model.TeamRow(
		model.SelectedTeamRow() );
	if ( selectedRow != NULL &&
		selectedRow->kind == MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT ) {
		const bool broadcaster = ( selectedRow->publicRoleMask &
			MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_BROADCASTER ) ) != 0;
		gui.SetStateString( "match_broadcaster_action", Localized(
			broadcaster ? "#str_41796" : "#str_41795" ) );
	} else {
		gui.SetStateString( "match_broadcaster_action", "" );
	}
	gui.SetStateInt( "match_referee_authenticated",
		( acceptedView.publicState.recipient.publicRoleMask &
		MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_REFEREE ) ) != 0 ? 1 : 0 );

	BuildProposalText( value, sizeof( value ),
		acceptedView.publicState.globalProposal, acceptedView, context );
	gui.SetStateString( "match_global_proposal", value );
	BuildProposalText( value, sizeof( value ), acceptedView.ownSideProposal,
		acceptedView, context );
	gui.SetStateString( "match_side_proposal", value );
	BuildRulesText( value, sizeof( value ),
		acceptedView.publicState.committedRules );
	gui.SetStateString( "match_rules_summary", value );
	BuildStagedRulesText( value, sizeof( value ), acceptedView.stagedRules );
	gui.SetStateString( "match_staged_summary", value );
	BuildSeriesText( value, sizeof( value ), acceptedView.publicState.series,
		context );
	gui.SetStateString( "match_series_summary", value );
	BuildEvidenceText( value, sizeof( value ), acceptedView.publicState.evidence );
	gui.SetStateString( "match_evidence_summary", value );
	BuildResultText( value, sizeof( value ), acceptedView, context );
	gui.SetStateString( "match_result_message", value );

	ProjectLists( gui, acceptedView, model, context );
	ProjectAvailability( gui, &model );
	gui.SetStateInt( "match_surface_available", 1 );
}

void MPMatchControlClearManagedContext( idUserInterface &gui ) {
	gui.SetStateString( "match_context_phase", "" );
	gui.SetStateString( "match_context_role", "" );
	gui.SetStateString( "match_context_pause", "" );
	gui.SetStateString( "match_context_readiness", "" );
	gui.SetStateString( "match_context_timeouts", "" );
	gui.SetStateString( "match_context_proposal", "" );
	gui.SetStateString( "match_context_series", "" );
	gui.SetStateString( "match_context_items", "" );
	// The single visibility gate is deliberately written last.
	gui.SetStateInt( "match_context_visible", 0 );
}

void MPMatchControlProjectManagedContext( idUserInterface &gui,
	const mpSessionView &acceptedView,
	const mpMatchControlModel &model,
	const mpMatchControlProjectionContext_t &context ) {
	if ( !ProjectionIsUsable( acceptedView, model ) ||
		!ProjectionIsManagedMatch( acceptedView ) ) {
		MPMatchControlClearManagedContext( gui );
		return;
	}
	char value[ PROJECTION_TEXT_BYTES ];
	BuildPhaseText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_context_phase", value );
	BuildRecipientText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_context_role", value );
	BuildPauseText( value, sizeof( value ), acceptedView, context );
	gui.SetStateString( "match_context_pause", value );
	BuildReadinessText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_context_readiness", value );
	BuildTimeoutText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_context_timeouts", value );
	const mpMatchViewProposalSummary_t &proposal =
		acceptedView.publicState.globalProposal.present ?
		acceptedView.publicState.globalProposal : acceptedView.ownSideProposal;
	BuildProposalText( value, sizeof( value ), proposal, acceptedView, context );
	gui.SetStateString( "match_context_proposal", value );
	BuildSeriesText( value, sizeof( value ), acceptedView.publicState.series,
		context );
	gui.SetStateString( "match_context_series", value );
	BuildItemTimingText( value, sizeof( value ), acceptedView );
	gui.SetStateString( "match_context_items", value );
	// The single visibility gate is deliberately written last.
	gui.SetStateInt( "match_context_visible", 1 );
}

#endif // !MP_MATCH_CONTROL_PROJECTION_SANITIZER_STANDALONE_TEST
