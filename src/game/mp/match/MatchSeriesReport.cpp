//----------------------------------------------------------------
// MatchSeriesReport.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SERIES_REPORT_STANDALONE_TEST )
	#include "MatchSeriesReport.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSeriesReport.h"
#endif

#include <limits.h>
#include <string.h>

namespace {

static const uint64_t MP_SERIES_REPORT_UINT64_MAX =
	static_cast<uint64_t>( ~( static_cast<uint64_t>( 0 ) ) );

static bool IsContestant( int contestant ) {
	return contestant >= 0 && contestant < MP_SERIES_SIDE_COUNT;
}

static bool AddUInt32( uint32_t lhs, uint32_t rhs, uint32_t &output ) {
	if ( rhs > UINT32_MAX - lhs ) {
		return false;
	}
	output = lhs + rhs;
	return true;
}

static bool AddUInt64( uint64_t lhs, uint64_t rhs, uint64_t &output ) {
	if ( rhs > UINT64_MAX - lhs ) {
		return false;
	}
	output = lhs + rhs;
	return true;
}

static bool AddInt64( int64_t lhs, int64_t rhs, int64_t &output ) {
	if ( ( rhs > 0 && lhs > INT64_MAX - rhs ) ||
		( rhs < 0 && lhs < INT64_MIN - rhs ) ) {
		return false;
	}
	output = lhs + rhs;
	return true;
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
	return length <= maximumBytes && ( allowEmpty || length != 0 ) &&
		ValidateUtf8( text, length );
}

static bool CopyText( const char *source, char *destination, int maximumBytes,
		bool allowEmpty ) {
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

static bool IsIdentityTokenCharacter( unsigned char value ) {
	return ( value >= 'a' && value <= 'z' ) ||
		( value >= 'A' && value <= 'Z' ) ||
		( value >= '0' && value <= '9' ) || value == '_' || value == '-';
}

static bool IsIdentityToken( const char *token, int maximumBytes ) {
	int length = 0;
	if ( !BoundedTextLength( token, maximumBytes, false, length ) ) {
		return false;
	}
	for ( int index = 0; index < length; ++index ) {
		if ( !IsIdentityTokenCharacter(
			static_cast<unsigned char>( token[ index ] ) ) ) {
			return false;
		}
	}
	return true;
}

static bool ValidateProfile( mpSeriesProfileId_t profile, const char *key,
		int bestOf ) {
	if ( bestOf < 1 || bestOf > MP_SERIES_MAX_BEST_OF || ( bestOf & 1 ) == 0 ||
		!IsIdentityToken( key, MP_SERIES_REPORT_PROFILE_KEY_BYTES ) ) {
		return false;
	}
	switch ( profile ) {
		case MP_SERIES_PROFILE_BEST_OF_ONE:
			return bestOf == 1 && strcmp( key, "best_of_one" ) == 0;
		case MP_SERIES_PROFILE_BEST_OF_THREE:
			return bestOf == 3 && strcmp( key, "best_of_three" ) == 0;
		case MP_SERIES_PROFILE_BEST_OF_FIVE:
			return bestOf == 5 && strcmp( key, "best_of_five" ) == 0;
		case MP_SERIES_PROFILE_CUSTOM:
			return true;
		default:
			return false;
	}
}

static bool BuildContestant( const mpSeriesReportContestantInput &input,
		mpSeriesReportContestant &output ) {
	memset( &output, 0, sizeof( output ) );
	if ( input.kind <= MP_SERIES_REPORT_CONTESTANT_INVALID ||
		input.kind >= MP_SERIES_REPORT_CONTESTANT_KIND_COUNT ||
		!CopyText( input.label, output.label,
			MP_SERIES_REPORT_DISPLAY_NAME_BYTES, false ) ) {
		return false;
	}
	if ( input.kind == MP_SERIES_REPORT_CONTESTANT_PARTICIPANT ) {
		if ( input.participantSequence == 0 ) {
			return false;
		}
		output.participantSequence = input.participantSequence;
	} else if ( input.participantSequence != 0 ) {
		return false;
	}
	output.kind = input.kind;
	return true;
}

static bool BuildIdentity( const mpSeriesReportIdentityInput &input,
		mpSeriesReportIdentity &output, mpSeriesReportReason_t &reason ) {
	memset( &output, 0, sizeof( output ) );
	reason = MP_SERIES_REPORT_REASON_INVALID_ARGUMENT;
	if ( input.seriesId == 0 || input.gameType < 0 ) {
		return false;
	}
	if ( !ValidateProfile( input.profile, input.profileKey, input.bestOf ) ) {
		reason = ( input.bestOf < 1 || input.bestOf > MP_SERIES_MAX_BEST_OF ||
			( input.bestOf & 1 ) == 0 ) ? MP_SERIES_REPORT_REASON_INVALID_BEST_OF :
			MP_SERIES_REPORT_REASON_INVALID_PROFILE;
		return false;
	}
	if ( input.rulesSchema == 0 || input.rulesRevision == 0 ||
		input.rulesDigest == 0 ||
		!IsIdentityToken( input.modeToken, MP_SERIES_REPORT_MODE_TOKEN_BYTES ) ) {
		reason = MP_SERIES_REPORT_REASON_INVALID_RULES_IDENTITY;
		return false;
	}
	if ( !BuildContestant( input.contestants[ 0 ], output.contestants[ 0 ] ) ||
		!BuildContestant( input.contestants[ 1 ], output.contestants[ 1 ] ) ||
		output.contestants[ 0 ].kind != output.contestants[ 1 ].kind ||
		strcmp( output.contestants[ 0 ].label,
			output.contestants[ 1 ].label ) == 0 ||
		( output.contestants[ 0 ].kind == MP_SERIES_REPORT_CONTESTANT_PARTICIPANT &&
			output.contestants[ 0 ].participantSequence ==
			output.contestants[ 1 ].participantSequence ) ) {
		reason = MP_SERIES_REPORT_REASON_INVALID_ARGUMENT;
		return false;
	}

	output.seriesId = input.seriesId;
	output.profile = input.profile;
	CopyText( input.profileKey, output.profileKey,
		MP_SERIES_REPORT_PROFILE_KEY_BYTES, false );
	output.bestOf = input.bestOf;
	output.rulesSchema = input.rulesSchema;
	output.rulesRevision = input.rulesRevision;
	output.rulesDigest = input.rulesDigest;
	output.gameType = input.gameType;
	CopyText( input.modeToken, output.modeToken,
		MP_SERIES_REPORT_MODE_TOKEN_BYTES, false );
	reason = MP_SERIES_REPORT_REASON_NONE;
	return true;
}

static bool ContestantEqual( const mpSeriesReportContestant &lhs,
		const mpSeriesReportContestant &rhs ) {
	return lhs.kind == rhs.kind &&
		lhs.participantSequence == rhs.participantSequence &&
		strcmp( lhs.label, rhs.label ) == 0;
}

static bool IdentityEqual( const mpSeriesReportIdentity &lhs,
		const mpSeriesReportIdentity &rhs ) {
	return lhs.seriesId == rhs.seriesId && lhs.profile == rhs.profile &&
		strcmp( lhs.profileKey, rhs.profileKey ) == 0 && lhs.bestOf == rhs.bestOf &&
		lhs.rulesSchema == rhs.rulesSchema &&
		lhs.rulesRevision == rhs.rulesRevision &&
		lhs.rulesDigest == rhs.rulesDigest && lhs.gameType == rhs.gameType &&
		strcmp( lhs.modeToken, rhs.modeToken ) == 0 &&
		ContestantEqual( lhs.contestants[ 0 ], rhs.contestants[ 0 ] ) &&
		ContestantEqual( lhs.contestants[ 1 ], rhs.contestants[ 1 ] );
}

static bool IsPathCharacter( unsigned char value ) {
	return ( value >= 'a' && value <= 'z' ) ||
		( value >= 'A' && value <= 'Z' ) ||
		( value >= '0' && value <= '9' ) || value == '-' || value == '_' ||
		value == '.' || value == '/';
}

static bool IsDotComponent( const char *path, int start, int length ) {
	return ( length == 1 && path[ start ] == '.' ) ||
		( length == 2 && path[ start ] == '.' && path[ start + 1 ] == '.' );
}

static bool ValidateArtifactQPath( mpSeriesReportArtifactKind_t kind,
		const char *qpath, bool partial = false ) {
	const char *root = NULL;
	const char *extension = NULL;
	if ( kind == MP_SERIES_REPORT_ARTIFACT_EVIDENCE ) {
		if ( partial ) {
			return false;
		}
		root = "match-results/";
		extension = ".json";
	} else if ( kind == MP_SERIES_REPORT_ARTIFACT_MVD ) {
		root = "demos/";
		extension = partial ? ".mvd.part" : ".mvd";
	} else {
		return false;
	}
	int length = 0;
	if ( !BoundedTextLength( qpath, MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES,
		false, length ) ) {
		return false;
	}
	const int rootLength = static_cast<int>( strlen( root ) );
	const int extensionLength = static_cast<int>( strlen( extension ) );
	if ( length <= rootLength + extensionLength ||
		memcmp( qpath, root, static_cast<size_t>( rootLength ) ) != 0 ||
		memcmp( qpath + length - extensionLength, extension,
			static_cast<size_t>( extensionLength ) ) != 0 ) {
		return false;
	}

	int componentStart = rootLength;
	for ( int index = componentStart; index < length; ++index ) {
		const unsigned char value = static_cast<unsigned char>( qpath[ index ] );
		if ( !IsPathCharacter( value ) ) {
			return false;
		}
		if ( value == '/' ) {
			const int componentLength = index - componentStart;
			if ( componentLength == 0 ||
				IsDotComponent( qpath, componentStart, componentLength ) ) {
				return false;
			}
			componentStart = index + 1;
		}
	}
	const int fileLength = length - componentStart;
	return fileLength > extensionLength &&
		!IsDotComponent( qpath, componentStart, fileLength );
}

static bool BuildArtifact( mpSeriesReportArtifactKind_t kind,
		const mpSeriesReportArtifactInput &input, mpSeriesReportArtifact &output,
		mpSeriesReportReason_t &reason ) {
	memset( &output, 0, sizeof( output ) );
	if ( input.status < MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED ||
		input.status >= MP_SERIES_REPORT_ARTIFACT_STATUS_COUNT ) {
		reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_STATUS;
		return false;
	}
	const char *qpath = input.qpath != NULL ? input.qpath : "";
	if ( input.status == MP_SERIES_REPORT_ARTIFACT_AVAILABLE ) {
		if ( input.reason != 0 || !ValidateArtifactQPath( kind, qpath ) ||
			!CopyText( qpath, output.qpath,
				MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES, false ) ) {
			reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH;
			return false;
		}
	} else if ( input.status == MP_SERIES_REPORT_ARTIFACT_PENDING ) {
		if ( kind != MP_SERIES_REPORT_ARTIFACT_MVD || input.reason == 0 ||
			!ValidateArtifactQPath( kind, qpath, true ) ||
			!CopyText( qpath, output.qpath,
				MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES, false ) ) {
			reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH;
			return false;
		}
	} else if ( input.status == MP_SERIES_REPORT_ARTIFACT_FAILED ) {
		if ( input.reason == 0 || ( qpath[ 0 ] != '\0' &&
			( kind != MP_SERIES_REPORT_ARTIFACT_MVD ||
				!ValidateArtifactQPath( kind, qpath, true ) ||
				!CopyText( qpath, output.qpath,
					MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES, false ) ) ) ) {
			reason = qpath[ 0 ] != '\0' ?
				MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH :
				MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_STATUS;
			return false;
		}
	} else {
		if ( qpath[ 0 ] != '\0' ) {
			reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH;
			return false;
		}
		if ( input.status == MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED ) {
			if ( input.reason != 0 ) {
				reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_STATUS;
				return false;
			}
		} else if ( input.reason == 0 ) {
			reason = MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_STATUS;
			return false;
		}
	}
	output.status = input.status;
	output.reason = input.reason;
	reason = MP_SERIES_REPORT_REASON_NONE;
	return true;
}

static bool BuildMapResult( const mpSeriesReportIdentity &identity,
		const mpSeriesReportMapResultInput &input,
		mpSeriesReportMapResult &output, mpSeriesReportReason_t &reason ) {
	memset( &output, 0, sizeof( output ) );
	reason = MP_SERIES_REPORT_REASON_INVALID_MAP_RESULT;
	if ( input.attempt == 0 || input.sessionId == 0 ||
		input.rulesDigest != identity.rulesDigest ||
		!MPMatchSeriesReportIsSafeMapToken( input.mapToken ) ||
		input.outcome < MP_SERIES_REPORT_MAP_DECIDED ||
		input.outcome >= MP_SERIES_REPORT_MAP_OUTCOME_COUNT || input.reason == 0 ) {
		return false;
	}
	const bool hasWinner = input.outcome == MP_SERIES_REPORT_MAP_DECIDED ||
		input.outcome == MP_SERIES_REPORT_MAP_FORFEIT;
	if ( ( hasWinner && !IsContestant( input.winnerContestant ) ) ||
		( !hasWinner &&
			input.winnerContestant != MP_SERIES_REPORT_CONTESTANT_NONE ) ||
		( input.outcome == MP_SERIES_REPORT_MAP_DRAW &&
			input.score[ 0 ] != input.score[ 1 ] ) ) {
		return false;
	}
	for ( int kind = 0; kind < MP_SERIES_REPORT_ARTIFACT_KIND_COUNT; ++kind ) {
		if ( !BuildArtifact( static_cast<mpSeriesReportArtifactKind_t>( kind ),
			input.artifacts[ kind ], output.artifacts[ kind ], reason ) ) {
			return false;
		}
	}
	output.attempt = input.attempt;
	output.sessionId = input.sessionId;
	CopyText( input.mapToken, output.mapToken, MP_SERIES_MAP_TOKEN_BYTES - 1,
		false );
	output.rulesDigest = input.rulesDigest;
	output.outcome = input.outcome;
	output.reason = input.reason;
	output.winnerContestant = static_cast<int8_t>( input.winnerContestant );
	output.score[ 0 ] = input.score[ 0 ];
	output.score[ 1 ] = input.score[ 1 ];
	reason = MP_SERIES_REPORT_REASON_NONE;
	return true;
}

static bool ArtifactEqual( const mpSeriesReportArtifact &lhs,
		const mpSeriesReportArtifact &rhs ) {
	return lhs.status == rhs.status && lhs.reason == rhs.reason &&
		strcmp( lhs.qpath, rhs.qpath ) == 0;
}

static bool MapResultEqual( const mpSeriesReportMapResult &lhs,
		const mpSeriesReportMapResult &rhs ) {
	return lhs.attempt == rhs.attempt && lhs.sessionId == rhs.sessionId &&
		strcmp( lhs.mapToken, rhs.mapToken ) == 0 &&
		lhs.rulesDigest == rhs.rulesDigest && lhs.outcome == rhs.outcome &&
		lhs.reason == rhs.reason &&
		lhs.winnerContestant == rhs.winnerContestant &&
		lhs.score[ 0 ] == rhs.score[ 0 ] && lhs.score[ 1 ] == rhs.score[ 1 ] &&
		ArtifactEqual( lhs.artifacts[ 0 ], rhs.artifacts[ 0 ] ) &&
		ArtifactEqual( lhs.artifacts[ 1 ], rhs.artifacts[ 1 ] );
}

static bool BuildParticipantStats( const mpSeriesReportIdentity &identity,
		const mpSeriesReportParticipantStatsInput &input,
		mpSeriesReportParticipantStats &output ) {
	memset( &output, 0, sizeof( output ) );
	if ( input.participantSequence == 0 || !IsContestant( input.contestant ) ||
		input.mapsWon > input.mapsPlayed || input.hits > input.shots ||
		!CopyText( input.displayName, output.displayName,
			MP_SERIES_REPORT_DISPLAY_NAME_BYTES, false ) ) {
		return false;
	}
	for ( int contestant = 0; contestant < MP_SERIES_SIDE_COUNT; ++contestant ) {
		const mpSeriesReportContestant &stable = identity.contestants[ contestant ];
		if ( stable.kind == MP_SERIES_REPORT_CONTESTANT_PARTICIPANT &&
			stable.participantSequence == input.participantSequence &&
			( contestant != input.contestant ||
				strcmp( stable.label, input.displayName ) != 0 ) ) {
			return false;
		}
	}
	output.participantSequence = input.participantSequence;
	output.contestant = static_cast<int8_t>( input.contestant );
	output.mapsPlayed = input.mapsPlayed;
	output.mapsWon = input.mapsWon;
	output.score = input.score;
	output.kills = input.kills;
	output.deaths = input.deaths;
	output.suicides = input.suicides;
	output.damageGiven = input.damageGiven;
	output.damageReceived = input.damageReceived;
	output.shots = input.shots;
	output.hits = input.hits;
	return true;
}

static bool ParticipantStatsEqual( const mpSeriesReportParticipantStats &lhs,
		const mpSeriesReportParticipantStats &rhs ) {
	return lhs.participantSequence == rhs.participantSequence &&
		lhs.contestant == rhs.contestant &&
		strcmp( lhs.displayName, rhs.displayName ) == 0 &&
		lhs.mapsPlayed == rhs.mapsPlayed && lhs.mapsWon == rhs.mapsWon &&
		lhs.score == rhs.score && lhs.kills == rhs.kills &&
		lhs.deaths == rhs.deaths && lhs.suicides == rhs.suicides &&
		lhs.damageGiven == rhs.damageGiven &&
		lhs.damageReceived == rhs.damageReceived && lhs.shots == rhs.shots &&
		lhs.hits == rhs.hits;
}

static bool BuildTeamStats( const mpSeriesReportTeamStatsInput &input,
		mpSeriesReportTeamStats &output ) {
	memset( &output, 0, sizeof( output ) );
	if ( !IsContestant( input.contestant ) || input.mapsWon > input.mapsPlayed ) {
		return false;
	}
	output.contestant = static_cast<int8_t>( input.contestant );
	output.mapsPlayed = input.mapsPlayed;
	output.mapsWon = input.mapsWon;
	output.score = input.score;
	output.objectives = input.objectives;
	output.roundsWon = input.roundsWon;
	output.damageGiven = input.damageGiven;
	return true;
}

static bool TeamStatsEqual( const mpSeriesReportTeamStats &lhs,
		const mpSeriesReportTeamStats &rhs ) {
	return lhs.contestant == rhs.contestant &&
		lhs.mapsPlayed == rhs.mapsPlayed && lhs.mapsWon == rhs.mapsWon &&
		lhs.score == rhs.score && lhs.objectives == rhs.objectives &&
		lhs.roundsWon == rhs.roundsWon && lhs.damageGiven == rhs.damageGiven;
}

static bool ValidateAuthorizer( const mpSeriesReportAuthorizer &authorizer ) {
	if ( authorizer.kind < MP_SERIES_REPORT_AUTHORIZER_SYSTEM ||
		authorizer.kind >= MP_SERIES_REPORT_AUTHORIZER_KIND_COUNT ) {
		return false;
	}
	return authorizer.kind == MP_SERIES_REPORT_AUTHORIZER_PARTICIPANT ?
		authorizer.participantSequence != 0 : authorizer.participantSequence == 0;
}

static bool BuildFinal( const mpSeriesReportFinalInput &input,
		mpSeriesReportFinal &output ) {
	memset( &output, 0, sizeof( output ) );
	if ( input.outcome <= MP_SERIES_REPORT_FINAL_NONE ||
		input.outcome >= MP_SERIES_REPORT_FINAL_OUTCOME_COUNT ||
		input.reason == 0 || !ValidateAuthorizer( input.authorizer ) ) {
		return false;
	}
	if ( input.outcome == MP_SERIES_REPORT_FINAL_COMPLETE ) {
		if ( !IsContestant( input.winnerContestant ) ) {
			return false;
		}
	} else if ( input.winnerContestant != MP_SERIES_REPORT_CONTESTANT_NONE ) {
		return false;
	}
	output.outcome = input.outcome;
	output.reason = input.reason;
	output.winnerContestant = static_cast<int8_t>( input.winnerContestant );
	output.authorizer = input.authorizer;
	return true;
}

static bool FinalEqual( const mpSeriesReportFinal &lhs,
		const mpSeriesReportFinal &rhs ) {
	return lhs.outcome == rhs.outcome && lhs.reason == rhs.reason &&
		lhs.winnerContestant == rhs.winnerContestant &&
		lhs.authorizer.kind == rhs.authorizer.kind &&
		lhs.authorizer.participantSequence == rhs.authorizer.participantSequence;
}

static int CountWins( const mpCompetitionSeriesReport &report, int contestant ) {
	int wins = 0;
	for ( int index = 0; index < report.GetMapResultCount(); ++index ) {
		const mpSeriesReportMapResult &map = *report.GetMapResult( index );
		if ( ( map.outcome == MP_SERIES_REPORT_MAP_DECIDED ||
			map.outcome == MP_SERIES_REPORT_MAP_FORFEIT ) &&
			map.winnerContestant == contestant ) {
			++wins;
		}
	}
	return wins;
}

static int CountCompletedMaps( const mpCompetitionSeriesReport &report ) {
	int completed = 0;
	for ( int index = 0; index < report.GetMapResultCount(); ++index ) {
		if ( report.GetMapResult( index )->outcome != MP_SERIES_REPORT_MAP_ABORTED ) {
			++completed;
		}
	}
	return completed;
}

static const char *ContestantKindToken( mpSeriesReportContestantKind_t kind ) {
	switch ( kind ) {
		case MP_SERIES_REPORT_CONTESTANT_PARTICIPANT: return "participant";
		case MP_SERIES_REPORT_CONTESTANT_SIDE: return "side";
		default: return "invalid";
	}
}

static const char *MapOutcomeToken( mpSeriesReportMapOutcome_t outcome ) {
	switch ( outcome ) {
		case MP_SERIES_REPORT_MAP_DECIDED: return "decided";
		case MP_SERIES_REPORT_MAP_FORFEIT: return "forfeit";
		case MP_SERIES_REPORT_MAP_ABORTED: return "aborted";
		case MP_SERIES_REPORT_MAP_DRAW: return "draw";
		default: return "invalid";
	}
}

static const char *ArtifactStatusToken( mpSeriesReportArtifactStatus_t status ) {
	switch ( status ) {
		case MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED: return "notRequested";
		case MP_SERIES_REPORT_ARTIFACT_AVAILABLE: return "available";
		case MP_SERIES_REPORT_ARTIFACT_FAILED: return "failed";
		case MP_SERIES_REPORT_ARTIFACT_DROPPED: return "dropped";
		case MP_SERIES_REPORT_ARTIFACT_PENDING: return "pending";
		default: return "invalid";
	}
}

static const char *AuthorizerKindToken( mpSeriesReportAuthorizerKind_t kind ) {
	switch ( kind ) {
		case MP_SERIES_REPORT_AUTHORIZER_SYSTEM: return "system";
		case MP_SERIES_REPORT_AUTHORIZER_PARTICIPANT: return "participant";
		case MP_SERIES_REPORT_AUTHORIZER_SERVER_OPERATOR: return "serverOperator";
		default: return "invalid";
	}
}

static const char *FinalOutcomeToken( mpSeriesReportFinalOutcome_t outcome ) {
	switch ( outcome ) {
		case MP_SERIES_REPORT_FINAL_COMPLETE: return "complete";
		case MP_SERIES_REPORT_FINAL_CANCELLED: return "cancelled";
		default: return "invalid";
	}
}

class mpSeriesReportJsonWriter {
public:
	mpSeriesReportJsonWriter( char *destination, uint64_t destinationCapacity ) :
		buffer( destination ), capacity( destinationCapacity ), length( 0 ), valid( true ) {
	}

	bool IsValid( void ) const { return valid; }
	uint64_t Length( void ) const { return length; }

	void PutChar( char value ) {
		if ( !valid || length == MP_SERIES_REPORT_UINT64_MAX ) {
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

	void PutBoolean( bool value ) { PutLiteral( value ? "true" : "false" ); }

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
			PutUInt64( static_cast<uint64_t>( -( value + 1 ) ) + 1 );
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

static void WriteContestant( mpSeriesReportJsonWriter &writer, int slot,
		const mpSeriesReportContestant &contestant ) {
	writer.PutLiteral( "{\"slot\":" );
	writer.PutInt64( slot );
	writer.PutLiteral( ",\"kind\":" );
	writer.PutString( ContestantKindToken( contestant.kind ) );
	writer.PutLiteral( ",\"participant\":" );
	writer.PutUInt64( contestant.participantSequence );
	writer.PutLiteral( ",\"label\":" );
	writer.PutString( contestant.label );
	writer.PutChar( '}' );
}

static void WriteArtifact( mpSeriesReportJsonWriter &writer,
		const mpSeriesReportArtifact &artifact ) {
	writer.PutLiteral( "{\"status\":" );
	writer.PutString( ArtifactStatusToken( artifact.status ) );
	writer.PutLiteral( ",\"reason\":" );
	writer.PutUInt64( artifact.reason );
	writer.PutLiteral( ",\"qpath\":" );
	writer.PutString( artifact.qpath );
	writer.PutChar( '}' );
}

static void WriteMapResult( mpSeriesReportJsonWriter &writer,
		const mpSeriesReportMapResult &map ) {
	writer.PutLiteral( "{\"sequence\":" );
	writer.PutUInt64( map.sequence );
	writer.PutLiteral( ",\"attempt\":" );
	writer.PutUInt64( map.attempt );
	writer.PutLiteral( ",\"sessionId\":" );
	writer.PutUInt64( map.sessionId );
	writer.PutLiteral( ",\"map\":" );
	writer.PutString( map.mapToken );
	writer.PutLiteral( ",\"rulesDigest\":" );
	writer.PutHex64String( map.rulesDigest );
	writer.PutLiteral( ",\"outcome\":" );
	writer.PutString( MapOutcomeToken( map.outcome ) );
	writer.PutLiteral( ",\"reason\":" );
	writer.PutUInt64( map.reason );
	writer.PutLiteral( ",\"winner\":" );
	writer.PutInt64( map.winnerContestant );
	writer.PutLiteral( ",\"scores\":[" );
	writer.PutInt64( map.score[ 0 ] );
	writer.PutChar( ',' );
	writer.PutInt64( map.score[ 1 ] );
	writer.PutLiteral( "],\"artifacts\":{\"evidence\":" );
	WriteArtifact( writer, map.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ] );
	writer.PutLiteral( ",\"mvd\":" );
	WriteArtifact( writer, map.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ] );
	writer.PutLiteral( "}}" );
}

static void WriteParticipantStats( mpSeriesReportJsonWriter &writer,
		const mpSeriesReportParticipantStats &stats ) {
	writer.PutLiteral( "{\"participant\":" );
	writer.PutUInt64( stats.participantSequence );
	writer.PutLiteral( ",\"contestant\":" );
	writer.PutInt64( stats.contestant );
	writer.PutLiteral( ",\"displayName\":" );
	writer.PutString( stats.displayName );
	writer.PutLiteral( ",\"mapsPlayed\":" );
	writer.PutUInt64( stats.mapsPlayed );
	writer.PutLiteral( ",\"mapsWon\":" );
	writer.PutUInt64( stats.mapsWon );
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

static void WriteTeamStats( mpSeriesReportJsonWriter &writer,
		const mpSeriesReportTeamStats &stats ) {
	writer.PutLiteral( "{\"contestant\":" );
	writer.PutInt64( stats.contestant );
	writer.PutLiteral( ",\"mapsPlayed\":" );
	writer.PutUInt64( stats.mapsPlayed );
	writer.PutLiteral( ",\"mapsWon\":" );
	writer.PutUInt64( stats.mapsWon );
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

static uint64_t CountArtifactStatus( const mpCompetitionSeriesReport &report,
		mpSeriesReportArtifactKind_t kind, mpSeriesReportArtifactStatus_t status ) {
	uint64_t count = 0;
	for ( int index = 0; index < report.GetMapResultCount(); ++index ) {
		if ( report.GetMapResult( index )->artifacts[ kind ].status == status ) {
			++count;
		}
	}
	return count;
}

static void WriteArtifactOutputSummary( mpSeriesReportJsonWriter &writer,
		const mpCompetitionSeriesReport &report,
		mpSeriesReportArtifactKind_t kind ) {
	writer.PutLiteral( "{\"notRequested\":" );
	writer.PutUInt64( CountArtifactStatus( report, kind,
		MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED ) );
	writer.PutLiteral( ",\"available\":" );
	writer.PutUInt64( CountArtifactStatus( report, kind,
		MP_SERIES_REPORT_ARTIFACT_AVAILABLE ) );
	writer.PutLiteral( ",\"pending\":" );
	writer.PutUInt64( CountArtifactStatus( report, kind,
		MP_SERIES_REPORT_ARTIFACT_PENDING ) );
	writer.PutLiteral( ",\"failed\":" );
	writer.PutUInt64( CountArtifactStatus( report, kind,
		MP_SERIES_REPORT_ARTIFACT_FAILED ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( CountArtifactStatus( report, kind,
		MP_SERIES_REPORT_ARTIFACT_DROPPED ) );
	writer.PutChar( '}' );
}

static void WriteAuthorizer( mpSeriesReportJsonWriter &writer,
		const mpSeriesReportAuthorizer &authorizer ) {
	writer.PutLiteral( "{\"kind\":" );
	writer.PutString( AuthorizerKindToken( authorizer.kind ) );
	writer.PutLiteral( ",\"participant\":" );
	writer.PutUInt64( authorizer.participantSequence );
	writer.PutChar( '}' );
}

static void WriteCanonicalReport( mpSeriesReportJsonWriter &writer,
		const mpCompetitionSeriesReport &report ) {
	const mpSeriesReportIdentity &identity = report.GetIdentity();
	writer.PutLiteral( "{\"schema\":" );
	writer.PutUInt64( MP_SERIES_REPORT_SCHEMA_VERSION );
	writer.PutLiteral( ",\"seriesId\":" );
	writer.PutUInt64( identity.seriesId );
	writer.PutLiteral( ",\"profile\":{\"id\":" );
	writer.PutInt64( identity.profile );
	writer.PutLiteral( ",\"key\":" );
	writer.PutString( identity.profileKey );
	writer.PutLiteral( ",\"bestOf\":" );
	writer.PutInt64( identity.bestOf );
	writer.PutLiteral( "},\"rules\":{\"schema\":" );
	writer.PutUInt64( identity.rulesSchema );
	writer.PutLiteral( ",\"revision\":" );
	writer.PutUInt64( identity.rulesRevision );
	writer.PutLiteral( ",\"digest\":" );
	writer.PutHex64String( identity.rulesDigest );
	writer.PutLiteral( ",\"gameType\":" );
	writer.PutInt64( identity.gameType );
	writer.PutLiteral( ",\"mode\":" );
	writer.PutString( identity.modeToken );
	writer.PutLiteral( "},\"reportRevision\":" );
	writer.PutUInt64( report.GetReportRevision() );
	writer.PutLiteral( ",\"contestants\":[" );
	WriteContestant( writer, 0, identity.contestants[ 0 ] );
	writer.PutChar( ',' );
	WriteContestant( writer, 1, identity.contestants[ 1 ] );

	writer.PutLiteral( "],\"maps\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( report.GetMapResultCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( report.GetDroppedMapResultCount() );
	writer.PutLiteral( ",\"entries\":[" );
	for ( int index = 0; index < report.GetMapResultCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		WriteMapResult( writer, *report.GetMapResult( index ) );
	}

	writer.PutLiteral( "]},\"seriesScore\":[" );
	writer.PutInt64( CountWins( report, 0 ) );
	writer.PutChar( ',' );
	writer.PutInt64( CountWins( report, 1 ) );
	writer.PutLiteral( "],\"participantStats\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( report.GetParticipantStatsCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( report.GetDroppedParticipantStatsCount() );
	writer.PutLiteral( ",\"entries\":[" );
	for ( int index = 0; index < report.GetParticipantStatsCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		WriteParticipantStats( writer, *report.GetParticipantStats( index ) );
	}

	writer.PutLiteral( "]},\"teamStats\":{\"accepted\":" );
	writer.PutUInt64( static_cast<uint64_t>( report.GetTeamStatsCount() ) );
	writer.PutLiteral( ",\"dropped\":" );
	writer.PutUInt64( report.GetDroppedTeamStatsCount() );
	writer.PutLiteral( ",\"entries\":[" );
	for ( int index = 0; index < report.GetTeamStatsCount(); ++index ) {
		if ( index > 0 ) {
			writer.PutChar( ',' );
		}
		WriteTeamStats( writer, *report.GetTeamStats( index ) );
	}

	writer.PutLiteral( "]},\"output\":{\"evidence\":" );
	WriteArtifactOutputSummary( writer, report,
		MP_SERIES_REPORT_ARTIFACT_EVIDENCE );
	writer.PutLiteral( ",\"mvd\":" );
	WriteArtifactOutputSummary( writer, report, MP_SERIES_REPORT_ARTIFACT_MVD );
	writer.PutLiteral( ",\"dropCounterSaturated\":" );
	writer.PutBoolean( report.IsDropCounterSaturated() );

	const mpSeriesReportFinal &finalResult = report.GetFinal();
	writer.PutLiteral( "},\"final\":{\"outcome\":" );
	writer.PutString( FinalOutcomeToken( finalResult.outcome ) );
	writer.PutLiteral( ",\"reason\":" );
	writer.PutUInt64( finalResult.reason );
	writer.PutLiteral( ",\"winner\":" );
	writer.PutInt64( finalResult.winnerContestant );
	writer.PutLiteral( ",\"authorizer\":" );
	WriteAuthorizer( writer, finalResult.authorizer );
	writer.PutLiteral( "}}" );
}

} // namespace

/*
===============================================================================

	Public helpers

===============================================================================
*/

bool MPMatchSeriesReportIsSafeMapToken( const char *mapToken ) {
	if ( mapToken == NULL || mapToken[ 0 ] == '\0' ) {
		return false;
	}
	int length = 0;
	char previous = '\0';
	for ( const char *cursor = mapToken; *cursor != '\0'; ++cursor ) {
		const unsigned char value = static_cast<unsigned char>( *cursor );
		if ( ++length >= MP_SERIES_MAP_TOKEN_BYTES ||
			!( ( value >= 'a' && value <= 'z' ) ||
				( value >= 'A' && value <= 'Z' ) ||
				( value >= '0' && value <= '9' ) || value == '_' || value == '-' ||
				value == '/' ) ||
			( value == '/' && ( previous == '\0' || previous == '/' ) ) ) {
			return false;
		}
		previous = static_cast<char>( value );
	}
	return previous != '/';
}

bool MPMatchSeriesReportIsSafeArtifactQPath( mpSeriesReportArtifactKind_t kind,
		const char *qpath ) {
	return ValidateArtifactQPath( kind, qpath );
}

mpSeriesReportAuthorizer MPSeriesReportSystemAuthorizer( void ) {
	mpSeriesReportAuthorizer authorizer;
	authorizer.kind = MP_SERIES_REPORT_AUTHORIZER_SYSTEM;
	authorizer.participantSequence = 0;
	return authorizer;
}

mpSeriesReportAuthorizer MPSeriesReportParticipantAuthorizer(
		uint32_t participantSequence ) {
	mpSeriesReportAuthorizer authorizer;
	authorizer.kind = MP_SERIES_REPORT_AUTHORIZER_PARTICIPANT;
	authorizer.participantSequence = participantSequence;
	return authorizer;
}

mpSeriesReportAuthorizer MPSeriesReportServerOperatorAuthorizer( void ) {
	mpSeriesReportAuthorizer authorizer;
	authorizer.kind = MP_SERIES_REPORT_AUTHORIZER_SERVER_OPERATOR;
	authorizer.participantSequence = 0;
	return authorizer;
}

bool mpSeriesReportWriteResult::WasAccepted( void ) const {
	return code == MP_SERIES_REPORT_WRITE_ACCEPTED;
}

bool mpSeriesReportWriteResult::WasDropped( void ) const {
	return code == MP_SERIES_REPORT_WRITE_DROPPED;
}

bool mpSeriesReportSerializeResult::Succeeded( void ) const {
	return code == MP_SERIES_REPORT_SERIALIZE_SUCCESS;
}

void mpSeriesReportCheckpointState::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_SERIES_REPORT_CHECKPOINT_STATE_VERSION;
	finalResult.winnerContestant = MP_SERIES_REPORT_CONTESTANT_NONE;
}

/*
===============================================================================

	Lifecycle and mutation

===============================================================================
*/

mpCompetitionSeriesReport::mpCompetitionSeriesReport( void ) {
	Clear();
}

void mpCompetitionSeriesReport::Clear( void ) {
	initialized = false;
	reportRevision = 0;
	memset( &identity, 0, sizeof( identity ) );
	memset( mapResults, 0, sizeof( mapResults ) );
	mapResultCount = 0;
	memset( participantStats, 0, sizeof( participantStats ) );
	participantStatsCount = 0;
	memset( teamStats, 0, sizeof( teamStats ) );
	teamStatsCount = 0;
	droppedMapResultCount = 0;
	droppedParticipantStatsCount = 0;
	droppedTeamStatsCount = 0;
	dropCounterSaturated = false;
	memset( &finalResult, 0, sizeof( finalResult ) );
	finalResult.winnerContestant = MP_SERIES_REPORT_CONTESTANT_NONE;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::Initialize(
		const mpSeriesReportIdentityInput &input ) {
	mpSeriesReportIdentity candidate;
	mpSeriesReportReason_t reason;
	if ( !BuildIdentity( input, candidate, reason ) ) {
		return Rejected( reason );
	}
	if ( initialized ) {
		return IdentityEqual( identity, candidate ) ? NoChange(
			MP_SERIES_REPORT_REASON_NONE ) : Rejected(
			MP_SERIES_REPORT_REASON_IDENTITY_CONFLICT );
	}
	initialized = true;
	identity = candidate;
	reportRevision = 1;
	return Accepted( 0 );
}

bool mpCompetitionSeriesReport::IsInitialized( void ) const {
	return initialized;
}

bool mpCompetitionSeriesReport::IsFinalized( void ) const {
	return finalResult.outcome != MP_SERIES_REPORT_FINAL_NONE;
}

uint64_t mpCompetitionSeriesReport::GetReportRevision( void ) const {
	return reportRevision;
}

const mpSeriesReportIdentity &mpCompetitionSeriesReport::GetIdentity( void ) const {
	return identity;
}

bool mpCompetitionSeriesReport::CanMutate( void ) const {
	return initialized && !IsFinalized() &&
		reportRevision != MP_SERIES_REPORT_UINT64_MAX;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::CommitAccepted( void ) {
	const uint64_t previousRevision = reportRevision;
	++reportRevision;
	return Accepted( previousRevision );
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::Accepted(
		uint64_t previousRevision ) {
	mpSeriesReportWriteResult result;
	result.code = MP_SERIES_REPORT_WRITE_ACCEPTED;
	result.reason = MP_SERIES_REPORT_REASON_NONE;
	result.previousRevision = previousRevision;
	result.currentRevision = reportRevision;
	return result;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::NoChange(
		mpSeriesReportReason_t reason ) const {
	mpSeriesReportWriteResult result;
	result.code = MP_SERIES_REPORT_WRITE_NO_CHANGE;
	result.reason = reason;
	result.previousRevision = reportRevision;
	result.currentRevision = reportRevision;
	return result;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::Rejected(
		mpSeriesReportReason_t reason ) const {
	mpSeriesReportWriteResult result;
	result.code = MP_SERIES_REPORT_WRITE_REJECTED;
	result.reason = reason;
	result.previousRevision = reportRevision;
	result.currentRevision = reportRevision;
	return result;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::RecordDrop(
		mpSeriesReportReason_t reason, uint64_t &counter ) {
	if ( !CanMutate() ) {
		return Rejected( !initialized ? MP_SERIES_REPORT_REASON_NOT_INITIALIZED :
			( IsFinalized() ? MP_SERIES_REPORT_REASON_FINALIZED :
				MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED ) );
	}
	if ( counter == MP_SERIES_REPORT_UINT64_MAX ) {
		dropCounterSaturated = true;
	} else {
		++counter;
		if ( counter == MP_SERIES_REPORT_UINT64_MAX ) {
			dropCounterSaturated = true;
		}
	}
	mpSeriesReportWriteResult result = CommitAccepted();
	result.code = MP_SERIES_REPORT_WRITE_DROPPED;
	result.reason = reason;
	return result;
}

int mpCompetitionSeriesReport::FindMapAttempt( uint32_t attempt ) const {
	for ( int index = 0; index < mapResultCount; ++index ) {
		if ( mapResults[ index ].attempt == attempt ) {
			return index;
		}
	}
	return -1;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::AppendMapResult(
		const mpSeriesReportMapResultInput &input ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	mpSeriesReportMapResult candidate;
	mpSeriesReportReason_t reason;
	if ( !BuildMapResult( identity, input, candidate, reason ) ) {
		return Rejected( reason );
	}
	const int existing = FindMapAttempt( input.attempt );
	if ( existing >= 0 ) {
		candidate.sequence = mapResults[ existing ].sequence;
		return MapResultEqual( mapResults[ existing ], candidate ) ?
			NoChange( MP_SERIES_REPORT_REASON_NONE ) :
			Rejected( MP_SERIES_REPORT_REASON_MAP_ATTEMPT_CONFLICT );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	if ( mapResultCount > 0 && input.attempt <= mapResults[ mapResultCount - 1 ].attempt ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_MAP_ORDER );
	}
	const int requiredWins = identity.bestOf / 2 + 1;
	if ( CountWins( *this, 0 ) >= requiredWins ||
		CountWins( *this, 1 ) >= requiredWins ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_MAP_RESULT );
	}
	if ( mapResultCount >= MP_SERIES_REPORT_MAX_MAP_RESULTS ) {
		return RecordDrop( MP_SERIES_REPORT_REASON_MAP_RESULT_CAPACITY,
			droppedMapResultCount );
	}
	candidate.sequence = static_cast<uint32_t>( mapResultCount ) + 1;
	mapResults[ mapResultCount++ ] = candidate;
	return CommitAccepted();
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::ReconcileMapArtifact(
		uint32_t attempt, mpSeriesReportArtifactKind_t kind,
		const mpSeriesReportArtifactInput &input ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	if ( kind < MP_SERIES_REPORT_ARTIFACT_EVIDENCE ||
		kind >= MP_SERIES_REPORT_ARTIFACT_KIND_COUNT ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_ARGUMENT );
	}
	mpSeriesReportArtifact candidate;
	mpSeriesReportReason_t reason;
	if ( !BuildArtifact( kind, input, candidate, reason ) ) {
		return Rejected( reason );
	}
	const int mapIndex = FindMapAttempt( attempt );
	if ( mapIndex < 0 ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_ARGUMENT );
	}
	mpSeriesReportArtifact &stored = mapResults[ mapIndex ].artifacts[ kind ];
	if ( ArtifactEqual( stored, candidate ) ) {
		return NoChange( MP_SERIES_REPORT_REASON_NONE );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	if ( stored.status != MP_SERIES_REPORT_ARTIFACT_PENDING ||
		( candidate.status != MP_SERIES_REPORT_ARTIFACT_PENDING &&
			candidate.status != MP_SERIES_REPORT_ARTIFACT_AVAILABLE &&
			candidate.status != MP_SERIES_REPORT_ARTIFACT_FAILED ) ||
		( candidate.status == MP_SERIES_REPORT_ARTIFACT_PENDING &&
			strcmp( stored.qpath, candidate.qpath ) != 0 ) ) {
		return Rejected(
			MP_SERIES_REPORT_REASON_ARTIFACT_RECONCILIATION_CONFLICT );
	}
	stored = candidate;
	return CommitAccepted();
}

int mpCompetitionSeriesReport::GetMapResultCount( void ) const {
	return mapResultCount;
}

const mpSeriesReportMapResult *mpCompetitionSeriesReport::GetMapResult(
		int index ) const {
	return index >= 0 && index < mapResultCount ? &mapResults[ index ] : NULL;
}

int mpCompetitionSeriesReport::FindParticipantStats(
		uint32_t participantSequence ) const {
	for ( int index = 0; index < participantStatsCount; ++index ) {
		if ( participantStats[ index ].participantSequence == participantSequence ) {
			return index;
		}
	}
	return -1;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::RecordParticipantStats(
		const mpSeriesReportParticipantStatsInput &input ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	mpSeriesReportParticipantStats candidate;
	if ( !BuildParticipantStats( identity, input, candidate ) ||
		input.mapsPlayed > static_cast<uint32_t>( CountCompletedMaps( *this ) ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS );
	}
	const int existing = FindParticipantStats( input.participantSequence );
	if ( existing >= 0 ) {
		return ParticipantStatsEqual( participantStats[ existing ], candidate ) ?
			NoChange( MP_SERIES_REPORT_REASON_NONE ) : Rejected(
				MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CONFLICT );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	if ( participantStatsCount >= MP_SERIES_REPORT_MAX_PARTICIPANTS ) {
		return RecordDrop( MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CAPACITY,
			droppedParticipantStatsCount );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	participantStats[ participantStatsCount++ ] = candidate;
	return CommitAccepted();
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::AccumulateParticipantStats(
		const mpSeriesReportParticipantStatsInput &delta ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	mpSeriesReportParticipantStats validatedDelta;
	if ( !BuildParticipantStats( identity, delta, validatedDelta ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS );
	}
	const int existing = FindParticipantStats( delta.participantSequence );
	if ( existing < 0 ) {
		return RecordParticipantStats( delta );
	}

	const mpSeriesReportParticipantStats &stored = participantStats[ existing ];
	if ( stored.contestant != validatedDelta.contestant ||
		strcmp( stored.displayName, validatedDelta.displayName ) != 0 ) {
		return Rejected( MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CONFLICT );
	}
	mpSeriesReportParticipantStatsInput combined;
	combined.participantSequence = stored.participantSequence;
	combined.contestant = stored.contestant;
	combined.displayName = stored.displayName;
	if ( !AddUInt32( stored.mapsPlayed, validatedDelta.mapsPlayed,
			combined.mapsPlayed ) ||
		!AddUInt32( stored.mapsWon, validatedDelta.mapsWon,
			combined.mapsWon ) ||
		!AddInt64( stored.score, validatedDelta.score, combined.score ) ||
		!AddUInt64( stored.kills, validatedDelta.kills, combined.kills ) ||
		!AddUInt64( stored.deaths, validatedDelta.deaths, combined.deaths ) ||
		!AddUInt64( stored.suicides, validatedDelta.suicides,
			combined.suicides ) ||
		!AddUInt64( stored.damageGiven, validatedDelta.damageGiven,
			combined.damageGiven ) ||
		!AddUInt64( stored.damageReceived, validatedDelta.damageReceived,
			combined.damageReceived ) ||
		!AddUInt64( stored.shots, validatedDelta.shots, combined.shots ) ||
		!AddUInt64( stored.hits, validatedDelta.hits, combined.hits ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS );
	}
	mpSeriesReportParticipantStats candidate;
	if ( !BuildParticipantStats( identity, combined, candidate ) ||
		combined.mapsPlayed > static_cast<uint32_t>( CountCompletedMaps( *this ) ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS );
	}
	if ( ParticipantStatsEqual( stored, candidate ) ) {
		return NoChange( MP_SERIES_REPORT_REASON_NONE );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	participantStats[ existing ] = candidate;
	return CommitAccepted();
}

int mpCompetitionSeriesReport::FindTeamStats( int contestant ) const {
	for ( int index = 0; index < teamStatsCount; ++index ) {
		if ( teamStats[ index ].contestant == contestant ) {
			return index;
		}
	}
	return -1;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::RecordTeamStats(
		const mpSeriesReportTeamStatsInput &input ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	mpSeriesReportTeamStats candidate;
	if ( !BuildTeamStats( input, candidate ) ||
		input.mapsPlayed > static_cast<uint32_t>( CountCompletedMaps( *this ) ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_TEAM_STATS );
	}
	const int existing = FindTeamStats( input.contestant );
	if ( existing >= 0 ) {
		return TeamStatsEqual( teamStats[ existing ], candidate ) ?
			NoChange( MP_SERIES_REPORT_REASON_NONE ) :
			Rejected( MP_SERIES_REPORT_REASON_TEAM_STATS_CONFLICT );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	if ( teamStatsCount >= MP_SERIES_REPORT_MAX_TEAMS ) {
		return RecordDrop( MP_SERIES_REPORT_REASON_TEAM_STATS_CAPACITY,
			droppedTeamStatsCount );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	teamStats[ teamStatsCount++ ] = candidate;
	return CommitAccepted();
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::AccumulateTeamStats(
		const mpSeriesReportTeamStatsInput &delta ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	if ( IsFinalized() ) {
		return Rejected( MP_SERIES_REPORT_REASON_FINALIZED );
	}
	mpSeriesReportTeamStats validatedDelta;
	if ( !BuildTeamStats( delta, validatedDelta ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_TEAM_STATS );
	}
	const int existing = FindTeamStats( delta.contestant );
	if ( existing < 0 ) {
		return RecordTeamStats( delta );
	}

	const mpSeriesReportTeamStats &stored = teamStats[ existing ];
	mpSeriesReportTeamStatsInput combined;
	combined.contestant = stored.contestant;
	if ( !AddUInt32( stored.mapsPlayed, validatedDelta.mapsPlayed,
			combined.mapsPlayed ) ||
		!AddUInt32( stored.mapsWon, validatedDelta.mapsWon,
			combined.mapsWon ) ||
		!AddInt64( stored.score, validatedDelta.score, combined.score ) ||
		!AddUInt64( stored.objectives, validatedDelta.objectives,
			combined.objectives ) ||
		!AddUInt64( stored.roundsWon, validatedDelta.roundsWon,
			combined.roundsWon ) ||
		!AddUInt64( stored.damageGiven, validatedDelta.damageGiven,
			combined.damageGiven ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_TEAM_STATS );
	}
	mpSeriesReportTeamStats candidate;
	if ( !BuildTeamStats( combined, candidate ) ||
		combined.mapsPlayed > static_cast<uint32_t>( CountCompletedMaps( *this ) ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_TEAM_STATS );
	}
	if ( TeamStatsEqual( stored, candidate ) ) {
		return NoChange( MP_SERIES_REPORT_REASON_NONE );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	teamStats[ existing ] = candidate;
	return CommitAccepted();
}

int mpCompetitionSeriesReport::GetParticipantStatsCount( void ) const {
	return participantStatsCount;
}

const mpSeriesReportParticipantStats *
	mpCompetitionSeriesReport::GetParticipantStats( int index ) const {
	return index >= 0 && index < participantStatsCount ?
		&participantStats[ index ] : NULL;
}

int mpCompetitionSeriesReport::GetTeamStatsCount( void ) const {
	return teamStatsCount;
}

const mpSeriesReportTeamStats *mpCompetitionSeriesReport::GetTeamStats(
		int index ) const {
	return index >= 0 && index < teamStatsCount ? &teamStats[ index ] : NULL;
}

uint64_t mpCompetitionSeriesReport::GetDroppedMapResultCount( void ) const {
	return droppedMapResultCount;
}

uint64_t mpCompetitionSeriesReport::GetDroppedParticipantStatsCount( void ) const {
	return droppedParticipantStatsCount;
}

uint64_t mpCompetitionSeriesReport::GetDroppedTeamStatsCount( void ) const {
	return droppedTeamStatsCount;
}

bool mpCompetitionSeriesReport::IsDropCounterSaturated( void ) const {
	return dropCounterSaturated;
}

mpSeriesReportWriteResult mpCompetitionSeriesReport::Finalize(
		const mpSeriesReportFinalInput &input ) {
	if ( !initialized ) {
		return Rejected( MP_SERIES_REPORT_REASON_NOT_INITIALIZED );
	}
	mpSeriesReportFinal candidate;
	if ( !BuildFinal( input, candidate ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_FINAL_OUTCOME );
	}
	if ( IsFinalized() ) {
		return FinalEqual( finalResult, candidate ) ?
			NoChange( MP_SERIES_REPORT_REASON_NONE ) : Rejected(
				MP_SERIES_REPORT_REASON_FINALIZATION_CONFLICT );
	}
	if ( reportRevision == MP_SERIES_REPORT_UINT64_MAX ) {
		return Rejected( MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED );
	}
	const int requiredWins = identity.bestOf / 2 + 1;
	const int wins0 = CountWins( *this, 0 );
	const int wins1 = CountWins( *this, 1 );
	const int winner = wins0 >= requiredWins ? 0 :
		( wins1 >= requiredWins ? 1 : MP_SERIES_REPORT_CONTESTANT_NONE );
	if ( ( candidate.outcome == MP_SERIES_REPORT_FINAL_COMPLETE &&
		candidate.winnerContestant != winner ) ||
		( candidate.outcome == MP_SERIES_REPORT_FINAL_CANCELLED &&
			winner != MP_SERIES_REPORT_CONTESTANT_NONE ) ) {
		return Rejected( MP_SERIES_REPORT_REASON_INVALID_FINAL_OUTCOME );
	}
	finalResult = candidate;
	return CommitAccepted();
}

const mpSeriesReportFinal &mpCompetitionSeriesReport::GetFinal( void ) const {
	return finalResult;
}

/*
===============================================================================

	Canonical serialization

===============================================================================
*/

mpSeriesReportSerializeResult mpCompetitionSeriesReport::SerializeCanonicalJson(
		char *buffer, int capacity ) const {
	mpSeriesReportSerializeResult result;
	result.code = MP_SERIES_REPORT_SERIALIZE_INVALID_ARGUMENT;
	result.bytesWritten = 0;
	result.requiredCapacity = 0;
	if ( capacity < 0 ) {
		return result;
	}
	if ( !initialized || !IsFinalized() || !ValidateInvariants() ) {
		result.code = MP_SERIES_REPORT_SERIALIZE_INVALID_STATE;
		return result;
	}

	mpSeriesReportJsonWriter counter( NULL, 0 );
	WriteCanonicalReport( counter, *this );
	if ( !counter.IsValid() ||
		counter.Length() >= static_cast<uint64_t>( MP_SERIES_REPORT_MAX_JSON_BYTES ) ||
		counter.Length() >= 2147483647ULL ) {
		result.code = MP_SERIES_REPORT_SERIALIZE_OUTPUT_TOO_LARGE;
		return result;
	}
	result.requiredCapacity = static_cast<int>( counter.Length() ) + 1;
	if ( buffer == NULL || capacity < result.requiredCapacity ) {
		result.code = MP_SERIES_REPORT_SERIALIZE_BUFFER_TOO_SMALL;
		return result;
	}

	mpSeriesReportJsonWriter writer( buffer, static_cast<uint64_t>( capacity ) );
	WriteCanonicalReport( writer, *this );
	buffer[ counter.Length() ] = '\0';
	result.code = MP_SERIES_REPORT_SERIALIZE_SUCCESS;
	result.bytesWritten = static_cast<int>( counter.Length() );
	return result;
}

bool mpCompetitionSeriesReport::ExportCheckpointState(
		mpSeriesReportCheckpointState &state ) const {
	if ( !initialized || !ValidateInvariants() ) {
		return false;
	}
	mpSeriesReportCheckpointState candidate;
	candidate.Clear();
	candidate.initialized = initialized;
	candidate.reportRevision = reportRevision;
	candidate.identity = identity;
	memcpy( candidate.mapResults, mapResults, sizeof( mapResults ) );
	candidate.mapResultCount = mapResultCount;
	memcpy( candidate.participantStats, participantStats,
		sizeof( participantStats ) );
	candidate.participantStatsCount = participantStatsCount;
	memcpy( candidate.teamStats, teamStats, sizeof( teamStats ) );
	candidate.teamStatsCount = teamStatsCount;
	candidate.droppedMapResultCount = droppedMapResultCount;
	candidate.droppedParticipantStatsCount = droppedParticipantStatsCount;
	candidate.droppedTeamStatsCount = droppedTeamStatsCount;
	candidate.dropCounterSaturated = dropCounterSaturated;
	candidate.finalResult = finalResult;
	state = candidate;
	return true;
}

bool mpCompetitionSeriesReport::RestoreCheckpointState(
		const mpSeriesReportCheckpointState &state ) {
	if ( state.schemaVersion != MP_SERIES_REPORT_CHECKPOINT_STATE_VERSION ||
		!state.initialized ) {
		return false;
	}
	mpCompetitionSeriesReport candidate;
	candidate.initialized = state.initialized;
	candidate.reportRevision = state.reportRevision;
	candidate.identity = state.identity;
	memcpy( candidate.mapResults, state.mapResults,
		sizeof( candidate.mapResults ) );
	candidate.mapResultCount = state.mapResultCount;
	memcpy( candidate.participantStats, state.participantStats,
		sizeof( candidate.participantStats ) );
	candidate.participantStatsCount = state.participantStatsCount;
	memcpy( candidate.teamStats, state.teamStats,
		sizeof( candidate.teamStats ) );
	candidate.teamStatsCount = state.teamStatsCount;
	candidate.droppedMapResultCount = state.droppedMapResultCount;
	candidate.droppedParticipantStatsCount = state.droppedParticipantStatsCount;
	candidate.droppedTeamStatsCount = state.droppedTeamStatsCount;
	candidate.dropCounterSaturated = state.dropCounterSaturated;
	candidate.finalResult = state.finalResult;
	if ( !candidate.ValidateInvariants() ) {
		return false;
	}
	*this = candidate;
	return true;
}

/*
===============================================================================

	Invariants

===============================================================================
*/

bool mpCompetitionSeriesReport::ValidateInvariants( void ) const {
	if ( !initialized ) {
		return reportRevision == 0 && mapResultCount == 0 &&
			participantStatsCount == 0 && teamStatsCount == 0 &&
			droppedMapResultCount == 0 && droppedParticipantStatsCount == 0 &&
			droppedTeamStatsCount == 0 && !dropCounterSaturated &&
			finalResult.outcome == MP_SERIES_REPORT_FINAL_NONE;
	}
	if ( reportRevision == 0 || identity.seriesId == 0 ||
		!ValidateProfile( identity.profile, identity.profileKey, identity.bestOf ) ||
		identity.rulesSchema == 0 || identity.rulesRevision == 0 ||
		identity.rulesDigest == 0 || identity.gameType < 0 ||
		!IsIdentityToken( identity.modeToken, MP_SERIES_REPORT_MODE_TOKEN_BYTES ) ||
		mapResultCount < 0 || mapResultCount > MP_SERIES_REPORT_MAX_MAP_RESULTS ||
		participantStatsCount < 0 ||
		participantStatsCount > MP_SERIES_REPORT_MAX_PARTICIPANTS ||
		teamStatsCount < 0 || teamStatsCount > MP_SERIES_REPORT_MAX_TEAMS ) {
		return false;
	}
	for ( int contestant = 0; contestant < MP_SERIES_SIDE_COUNT; ++contestant ) {
		const mpSeriesReportContestant &stored = identity.contestants[ contestant ];
		mpSeriesReportContestantInput input;
		input.kind = stored.kind;
		input.participantSequence = stored.participantSequence;
		input.label = stored.label;
		mpSeriesReportContestant rebuilt;
		if ( !BuildContestant( input, rebuilt ) || !ContestantEqual( stored, rebuilt ) ) {
			return false;
		}
	}
	if ( identity.contestants[ 0 ].kind != identity.contestants[ 1 ].kind ||
		strcmp( identity.contestants[ 0 ].label,
			identity.contestants[ 1 ].label ) == 0 ||
		( identity.contestants[ 0 ].kind == MP_SERIES_REPORT_CONTESTANT_PARTICIPANT &&
			identity.contestants[ 0 ].participantSequence ==
			identity.contestants[ 1 ].participantSequence ) ) {
		return false;
	}

	uint32_t previousAttempt = 0;
	for ( int index = 0; index < mapResultCount; ++index ) {
		const mpSeriesReportMapResult &stored = mapResults[ index ];
		mpSeriesReportMapResultInput input;
		memset( &input, 0, sizeof( input ) );
		input.attempt = stored.attempt;
		input.sessionId = stored.sessionId;
		input.mapToken = stored.mapToken;
		input.rulesDigest = stored.rulesDigest;
		input.outcome = stored.outcome;
		input.reason = stored.reason;
		input.winnerContestant = stored.winnerContestant;
		input.score[ 0 ] = stored.score[ 0 ];
		input.score[ 1 ] = stored.score[ 1 ];
		for ( int kind = 0; kind < MP_SERIES_REPORT_ARTIFACT_KIND_COUNT; ++kind ) {
			input.artifacts[ kind ].status = stored.artifacts[ kind ].status;
			input.artifacts[ kind ].reason = stored.artifacts[ kind ].reason;
			input.artifacts[ kind ].qpath = stored.artifacts[ kind ].qpath;
		}
		mpSeriesReportMapResult rebuilt;
		mpSeriesReportReason_t reason;
		if ( stored.sequence != static_cast<uint32_t>( index ) + 1 ||
			stored.attempt <= previousAttempt ||
			!BuildMapResult( identity, input, rebuilt, reason ) ) {
			return false;
		}
		rebuilt.sequence = stored.sequence;
		if ( !MapResultEqual( stored, rebuilt ) ) {
			return false;
		}
		previousAttempt = stored.attempt;
	}

	const int completedMaps = CountCompletedMaps( *this );
	for ( int index = 0; index < participantStatsCount; ++index ) {
		const mpSeriesReportParticipantStats &stored = participantStats[ index ];
		mpSeriesReportParticipantStatsInput input;
		input.participantSequence = stored.participantSequence;
		input.contestant = stored.contestant;
		input.displayName = stored.displayName;
		input.mapsPlayed = stored.mapsPlayed;
		input.mapsWon = stored.mapsWon;
		input.score = stored.score;
		input.kills = stored.kills;
		input.deaths = stored.deaths;
		input.suicides = stored.suicides;
		input.damageGiven = stored.damageGiven;
		input.damageReceived = stored.damageReceived;
		input.shots = stored.shots;
		input.hits = stored.hits;
		mpSeriesReportParticipantStats rebuilt;
		if ( stored.mapsPlayed > static_cast<uint32_t>( completedMaps ) ||
			!BuildParticipantStats( identity, input, rebuilt ) ||
			!ParticipantStatsEqual( stored, rebuilt ) ) {
			return false;
		}
		for ( int other = index + 1; other < participantStatsCount; ++other ) {
			if ( stored.participantSequence ==
				participantStats[ other ].participantSequence ) {
				return false;
			}
		}
	}
	for ( int index = 0; index < teamStatsCount; ++index ) {
		const mpSeriesReportTeamStats &stored = teamStats[ index ];
		mpSeriesReportTeamStatsInput input;
		input.contestant = stored.contestant;
		input.mapsPlayed = stored.mapsPlayed;
		input.mapsWon = stored.mapsWon;
		input.score = stored.score;
		input.objectives = stored.objectives;
		input.roundsWon = stored.roundsWon;
		input.damageGiven = stored.damageGiven;
		mpSeriesReportTeamStats rebuilt;
		if ( stored.mapsPlayed > static_cast<uint32_t>( completedMaps ) ||
			!BuildTeamStats( input, rebuilt ) || !TeamStatsEqual( stored, rebuilt ) ) {
			return false;
		}
		for ( int other = index + 1; other < teamStatsCount; ++other ) {
			if ( stored.contestant == teamStats[ other ].contestant ) {
				return false;
			}
		}
	}

	const bool hasSaturatedCounter =
		droppedMapResultCount == MP_SERIES_REPORT_UINT64_MAX ||
		droppedParticipantStatsCount == MP_SERIES_REPORT_UINT64_MAX ||
		droppedTeamStatsCount == MP_SERIES_REPORT_UINT64_MAX;
	if ( hasSaturatedCounter != dropCounterSaturated ) {
		return false;
	}

	const int requiredWins = identity.bestOf / 2 + 1;
	const int wins0 = CountWins( *this, 0 );
	const int wins1 = CountWins( *this, 1 );
	if ( wins0 > requiredWins || wins1 > requiredWins ||
		( wins0 >= requiredWins && wins1 >= requiredWins ) ) {
		return false;
	}
	if ( !IsFinalized() ) {
		return finalResult.outcome == MP_SERIES_REPORT_FINAL_NONE &&
			finalResult.reason == 0 &&
			finalResult.winnerContestant == MP_SERIES_REPORT_CONTESTANT_NONE &&
			finalResult.authorizer.participantSequence == 0;
	}
	mpSeriesReportFinalInput finalInput;
	finalInput.outcome = finalResult.outcome;
	finalInput.reason = finalResult.reason;
	finalInput.winnerContestant = finalResult.winnerContestant;
	finalInput.authorizer = finalResult.authorizer;
	mpSeriesReportFinal rebuiltFinal;
	if ( !BuildFinal( finalInput, rebuiltFinal ) ||
		!FinalEqual( finalResult, rebuiltFinal ) ) {
		return false;
	}
	const int winner = wins0 >= requiredWins ? 0 :
		( wins1 >= requiredWins ? 1 : MP_SERIES_REPORT_CONTESTANT_NONE );
	return ( finalResult.outcome == MP_SERIES_REPORT_FINAL_COMPLETE &&
		finalResult.winnerContestant == winner ) ||
		( finalResult.outcome == MP_SERIES_REPORT_FINAL_CANCELLED &&
			winner == MP_SERIES_REPORT_CONTESTANT_NONE );
}
