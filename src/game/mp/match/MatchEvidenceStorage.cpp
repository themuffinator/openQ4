//----------------------------------------------------------------
// MatchEvidenceStorage.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_EVIDENCE_STORAGE_STANDALONE_TEST )
	#include "MatchEvidenceStorage.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchEvidenceStorage.h"
#endif

#include <string.h>

namespace {

static const char MP_MATCH_EVIDENCE_STORAGE_PATH_PREFIX[] =
	"match-results/session-";

class mpEvidenceStoragePathBuilder {
public:
	mpEvidenceStoragePathBuilder( char *destination, int capacity ) :
		buffer( destination ), maximum( capacity ), length( 0 ), valid( true ) {
		if ( buffer == 0 || maximum < 1 ) {
			valid = false;
			return;
		}
		buffer[ 0 ] = '\0';
	}

	void PutCharacter( char value ) {
		if ( !valid || length >= maximum - 1 ) {
			valid = false;
			return;
		}
		buffer[ length++ ] = value;
		buffer[ length ] = '\0';
	}

	void PutLiteral( const char *value ) {
		if ( value == 0 ) {
			valid = false;
			return;
		}
		for ( int index = 0; value[ index ] != '\0'; ++index ) {
			PutCharacter( value[ index ] );
		}
	}

	void PutUnsigned64( uint64_t value ) {
		char reversed[ 20 ];
		int count = 0;
		do {
			reversed[ count++ ] = static_cast<char>( '0' + value % 10u );
			value /= 10u;
		} while ( value != 0 && count < static_cast<int>( sizeof( reversed ) ) );
		for ( int index = count - 1; index >= 0; --index ) {
			PutCharacter( reversed[ index ] );
		}
	}

	bool Succeeded( void ) const { return valid; }

private:
	char *buffer;
	int maximum;
	int length;
	bool valid;
};

static bool IsAsciiAlphaNumeric( unsigned char value ) {
	return ( value >= 'a' && value <= 'z' ) ||
		( value >= 'A' && value <= 'Z' ) ||
		( value >= '0' && value <= '9' );
}

static char LowerAscii( unsigned char value ) {
	return value >= 'A' && value <= 'Z' ?
		static_cast<char>( value - 'A' + 'a' ) : static_cast<char>( value );
}

static void BuildSanitizedMapToken( const char *map,
	char token[ MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES + 1 ] ) {
	memset( token, 0, MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES + 1 );
	int outputLength = 0;
	bool separatorPending = false;
	if ( map != 0 ) {
		int inputLength = 0;
		int componentStart = 0;
		while ( inputLength < MP_MATCH_EVIDENCE_MAX_MAP_BYTES && map[ inputLength ] != '\0' ) {
			if ( map[ inputLength ] == '/' || map[ inputLength ] == '\\' ) {
				componentStart = inputLength + 1;
			}
			++inputLength;
		}
		for ( int input = componentStart; input < inputLength; ++input ) {
			const unsigned char value = static_cast<unsigned char>( map[ input ] );
			if ( IsAsciiAlphaNumeric( value ) ) {
				if ( separatorPending && outputLength > 0 &&
					outputLength < MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES ) {
					token[ outputLength++ ] = '-';
				}
				separatorPending = false;
				if ( outputLength >= MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES ) {
					break;
				}
				token[ outputLength++ ] = LowerAscii( value );
			} else {
				separatorPending = outputLength > 0;
			}
		}
	}
	while ( outputLength > 0 && token[ outputLength - 1 ] == '-' ) {
		--outputLength;
	}
	if ( outputLength == 0 ) {
		memcpy( token, "map", 3 );
		outputLength = 3;
	}
	token[ outputLength ] = '\0';
}

static void SetReason( mpEvidenceStorageReason_t *reason,
	mpEvidenceStorageReason_t value ) {
	if ( reason != 0 ) {
		*reason = value;
	}
}

static mpEvidenceStorageReason_t SerializationFailureReason(
	const mpEvidenceSerializeResult &serialized ) {
	if ( serialized.code == MP_EVIDENCE_SERIALIZE_BUFFER_TOO_SMALL &&
		serialized.requiredCapacity > MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES ) {
		return MP_EVIDENCE_STORAGE_REASON_JSON_TOO_LARGE;
	}
	return MP_EVIDENCE_STORAGE_REASON_SERIALIZE_FAILED;
}

static void RecordCleanup( mpMatchEvidenceStorageWriter &writer,
	mpEvidenceStorageResult &result ) {
	if ( !writer.RemoveTemp( result.paths.temporaryQPath ) ) {
		result.cleanupReason = MP_EVIDENCE_STORAGE_REASON_TEMP_CLEANUP_FAILED;
	}
}

static bool ReadLiteral( const char *path, int length, int &cursor,
		const char *literal ) {
	if ( literal == 0 ) {
		return false;
	}
	for ( int index = 0; literal[ index ] != '\0'; ++index ) {
		if ( cursor >= length || path[ cursor ] != literal[ index ] ) {
			return false;
		}
		++cursor;
	}
	return true;
}

static bool ReadCanonicalUnsigned64( const char *path, int length, int &cursor,
		bool allowZero ) {
	static const char maximumUnsigned64[] = "18446744073709551615";
	const int start = cursor;
	while ( cursor < length && path[ cursor ] >= '0' && path[ cursor ] <= '9' ) {
		++cursor;
	}
	const int digits = cursor - start;
	if ( digits < 1 || digits > 20 ) {
		return false;
	}
	if ( path[ start ] == '0' ) {
		return digits == 1 && allowZero;
	}
	if ( digits == 20 ) {
		for ( int index = 0; index < digits; ++index ) {
			if ( path[ start + index ] < maximumUnsigned64[ index ] ) {
				break;
			}
			if ( path[ start + index ] > maximumUnsigned64[ index ] ) {
				return false;
			}
		}
	}
	return true;
}

static bool ReadMapToken( const char *path, int length, int &cursor ) {
	const int start = cursor;
	bool previousHyphen = false;
	while ( cursor < length && path[ cursor ] != '.' ) {
		const char value = path[ cursor ];
		const bool alphaNumeric = ( value >= 'a' && value <= 'z' ) ||
			( value >= '0' && value <= '9' );
		if ( !alphaNumeric && value != '-' ) {
			return false;
		}
		if ( value == '-' && ( cursor == start || previousHyphen ) ) {
			return false;
		}
		previousHyphen = value == '-';
		++cursor;
	}
	const int tokenBytes = cursor - start;
	return tokenBytes >= 1 && tokenBytes <= MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES &&
		!previousHyphen;
}

static bool GetBoundedQPathLength( const char *path, int &length ) {
	if ( path == 0 ) {
		return false;
	}
	for ( length = 0; length <= MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES; ++length ) {
		if ( path[ length ] == '\0' ) {
			return length > 0;
		}
	}
	return false;
}

static bool IsCanonicalEvidenceQPath( const char *path, bool temporary ) {
	int length = 0;
	if ( !GetBoundedQPathLength( path, length ) ) {
		return false;
	}
	int cursor = 0;
	if ( !ReadLiteral( path, length, cursor,
			MP_MATCH_EVIDENCE_STORAGE_PATH_PREFIX ) ||
		 !ReadCanonicalUnsigned64( path, length, cursor, false ) ||
		 !ReadLiteral( path, length, cursor, "_series-" ) ||
		 !ReadCanonicalUnsigned64( path, length, cursor, true ) ||
		 !ReadLiteral( path, length, cursor, "_" ) ||
		 !ReadMapToken( path, length, cursor ) ||
		 !ReadLiteral( path, length, cursor, ".json" ) ) {
		return false;
	}
	if ( temporary ) {
		if ( !ReadLiteral( path, length, cursor, ".pending-" ) ||
			 !ReadCanonicalUnsigned64( path, length, cursor, false ) ) {
			return false;
		}
	}
	return cursor == length;
}

} // namespace

void mpEvidenceStoragePaths::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
}

void mpEvidenceStorageResult::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	code = MP_EVIDENCE_STORAGE_REJECTED;
	reason = MP_EVIDENCE_STORAGE_REASON_NONE;
	cleanupReason = MP_EVIDENCE_STORAGE_REASON_NONE;
	backendBytes = -1;
	paths.Clear();
}

bool mpEvidenceStorageResult::Succeeded( void ) const {
	return code == MP_EVIDENCE_STORAGE_STORED &&
		reason == MP_EVIDENCE_STORAGE_REASON_NONE &&
		cleanupReason == MP_EVIDENCE_STORAGE_REASON_NONE;
}

bool MPMatchEvidenceStorageBuildPaths( const mpMatchEvidence &journal,
	mpEvidenceStoragePaths &paths, mpEvidenceStorageReason_t *reason ) {
	SetReason( reason, MP_EVIDENCE_STORAGE_REASON_NONE );
	mpEvidenceStoragePaths built;
	built.Clear();
	if ( !journal.IsInitialized() ) {
		SetReason( reason, MP_EVIDENCE_STORAGE_REASON_NOT_INITIALIZED );
		return false;
	}
	if ( !journal.ValidateInvariants() ) {
		SetReason( reason, MP_EVIDENCE_STORAGE_REASON_INVALID_JOURNAL );
		return false;
	}
	const mpEvidenceMetadata &metadata = journal.GetMetadata();
	if ( metadata.sessionId == 0 || journal.GetEvidenceRevision() == 0 ) {
		SetReason( reason, MP_EVIDENCE_STORAGE_REASON_INVALID_IDENTITY );
		return false;
	}
	BuildSanitizedMapToken( metadata.map, built.mapToken );

	mpEvidenceStoragePathBuilder finalPath( built.finalQPath,
		static_cast<int>( sizeof( built.finalQPath ) ) );
	finalPath.PutLiteral( MP_MATCH_EVIDENCE_STORAGE_PATH_PREFIX );
	finalPath.PutUnsigned64( metadata.sessionId );
	finalPath.PutLiteral( "_series-" );
	finalPath.PutUnsigned64( metadata.seriesId );
	finalPath.PutCharacter( '_' );
	finalPath.PutLiteral( built.mapToken );
	finalPath.PutLiteral( ".json" );

	mpEvidenceStoragePathBuilder temporaryPath( built.temporaryQPath,
		static_cast<int>( sizeof( built.temporaryQPath ) ) );
	temporaryPath.PutLiteral( built.finalQPath );
	temporaryPath.PutLiteral( ".pending-" );
	temporaryPath.PutUnsigned64( journal.GetEvidenceRevision() );
	if ( !finalPath.Succeeded() || !temporaryPath.Succeeded() ) {
		SetReason( reason, MP_EVIDENCE_STORAGE_REASON_PATH_TOO_LONG );
		return false;
	}
	if ( !MPMatchEvidenceStorageIsPromotionPair( built.temporaryQPath,
			built.finalQPath ) ) {
		SetReason( reason, MP_EVIDENCE_STORAGE_REASON_PATH_TOO_LONG );
		return false;
	}
	paths = built;
	return true;
}

bool MPMatchEvidenceStorageIsFinalQPath( const char *finalQPath ) {
	return IsCanonicalEvidenceQPath( finalQPath, false );
}

bool MPMatchEvidenceStorageIsTemporaryQPath( const char *temporaryQPath ) {
	return IsCanonicalEvidenceQPath( temporaryQPath, true );
}

bool MPMatchEvidenceStorageIsPromotionPair( const char *temporaryQPath,
		const char *finalQPath ) {
	if ( !MPMatchEvidenceStorageIsTemporaryQPath( temporaryQPath ) ||
		 !MPMatchEvidenceStorageIsFinalQPath( finalQPath ) ) {
		return false;
	}
	int finalLength = 0;
	int temporaryLength = 0;
	if ( !GetBoundedQPathLength( finalQPath, finalLength ) ||
		 !GetBoundedQPathLength( temporaryQPath, temporaryLength ) ||
		 temporaryLength <= finalLength ) {
		return false;
	}
	for ( int index = 0; index < finalLength; ++index ) {
		if ( temporaryQPath[ index ] != finalQPath[ index ] ) {
			return false;
		}
	}
	return temporaryQPath[ finalLength ] == '.';
}

mpEvidenceStorageResult MPMatchEvidenceStoragePersist(
	const mpMatchEvidence &journal,
	mpMatchEvidenceStorageWriter &writer,
	mpEvidenceStorageWorkspace &workspace ) {
	mpEvidenceStorageResult result;
	result.Clear();
	if ( !MPMatchEvidenceStorageBuildPaths( journal, result.paths, &result.reason ) ) {
		return result;
	}
	result.code = MP_EVIDENCE_STORAGE_FAILED;

	const mpEvidenceSerializeResult serialized = journal.SerializeCanonicalJson(
		workspace.json, static_cast<int>( sizeof( workspace.json ) ) );
	if ( !serialized.Succeeded() || serialized.bytesWritten < 1 ||
		serialized.bytesWritten >= static_cast<int>( sizeof( workspace.json ) ) ) {
		result.reason = SerializationFailureReason( serialized );
		return result;
	}
	result.serializedBytes = serialized.bytesWritten;
	result.backendBytes = writer.WriteTemp( result.paths.temporaryQPath,
		workspace.json, result.serializedBytes );
	if ( result.backendBytes != result.serializedBytes ) {
		result.reason = result.backendBytes < 0 ?
			MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_FAILED :
			MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_PARTIAL;
		RecordCleanup( writer, result );
		return result;
	}
	if ( !writer.Promote( result.paths.temporaryQPath, result.paths.finalQPath ) ) {
		result.reason = MP_EVIDENCE_STORAGE_REASON_PROMOTION_FAILED;
		RecordCleanup( writer, result );
		return result;
	}
	result.code = MP_EVIDENCE_STORAGE_STORED;
	result.reason = MP_EVIDENCE_STORAGE_REASON_NONE;
	return result;
}
