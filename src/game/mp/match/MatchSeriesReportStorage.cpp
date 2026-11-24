//----------------------------------------------------------------
// MatchSeriesReportStorage.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SERIES_REPORT_STORAGE_STANDALONE_TEST )
	#include "MatchSeriesReportStorage.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSeriesReportStorage.h"
#endif

#include <string.h>

namespace {

static const char MP_SERIES_REPORT_STORAGE_PATH_PREFIX[] =
	"match-results/series-";

class mpSeriesReportStoragePathBuilder {
public:
	mpSeriesReportStoragePathBuilder( char *destination, int capacity ) :
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

static void SetReason( mpSeriesReportStorageReason_t *reason,
		mpSeriesReportStorageReason_t value ) {
	if ( reason != 0 ) {
		*reason = value;
	}
}

static bool GetBoundedQPathLength( const char *path, int &length ) {
	if ( path == 0 ) {
		return false;
	}
	for ( length = 0; length <= MP_SERIES_REPORT_STORAGE_QPATH_BYTES; ++length ) {
		if ( path[ length ] == '\0' ) {
			return length > 0;
		}
	}
	return false;
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

static bool ReadCanonicalUnsigned64( const char *path, int length, int &cursor ) {
	static const char maximumUnsigned64[] = "18446744073709551615";
	const int start = cursor;
	while ( cursor < length && path[ cursor ] >= '0' && path[ cursor ] <= '9' ) {
		++cursor;
	}
	const int digits = cursor - start;
	if ( digits < 1 || digits > 20 || path[ start ] == '0' ) {
		return false;
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

static bool IsCanonicalSeriesReportQPath( const char *path, bool temporary ) {
	int length = 0;
	if ( !GetBoundedQPathLength( path, length ) ) {
		return false;
	}
	int cursor = 0;
	if ( !ReadLiteral( path, length, cursor,
			MP_SERIES_REPORT_STORAGE_PATH_PREFIX ) ||
		!ReadCanonicalUnsigned64( path, length, cursor ) ||
		!ReadLiteral( path, length, cursor, ".json" ) ) {
		return false;
	}
	if ( temporary &&
		( !ReadLiteral( path, length, cursor, ".pending-" ) ||
			!ReadCanonicalUnsigned64( path, length, cursor ) ) ) {
		return false;
	}
	return cursor == length;
}

static mpSeriesReportStorageReason_t SerializationFailureReason(
		const mpSeriesReportSerializeResult &serialized ) {
	if ( serialized.code == MP_SERIES_REPORT_SERIALIZE_BUFFER_TOO_SMALL &&
		serialized.requiredCapacity > MP_SERIES_REPORT_STORAGE_JSON_BYTES ) {
		return MP_SERIES_REPORT_STORAGE_REASON_JSON_TOO_LARGE;
	}
	if ( serialized.code == MP_SERIES_REPORT_SERIALIZE_OUTPUT_TOO_LARGE ) {
		return MP_SERIES_REPORT_STORAGE_REASON_JSON_TOO_LARGE;
	}
	return MP_SERIES_REPORT_STORAGE_REASON_SERIALIZE_FAILED;
}

static void RecordCleanup( mpMatchSeriesReportStorageWriter &writer,
		mpSeriesReportStorageResult &result ) {
	if ( !writer.RemoveTemp( result.paths.temporaryQPath ) ) {
		result.cleanupReason =
			MP_SERIES_REPORT_STORAGE_REASON_TEMP_CLEANUP_FAILED;
	}
}

} // namespace

void mpSeriesReportStoragePaths::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
}

void mpSeriesReportStorageResult::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	code = MP_SERIES_REPORT_STORAGE_REJECTED;
	reason = MP_SERIES_REPORT_STORAGE_REASON_NONE;
	cleanupReason = MP_SERIES_REPORT_STORAGE_REASON_NONE;
	backendBytes = -1;
	paths.Clear();
}

bool mpSeriesReportStorageResult::Succeeded( void ) const {
	return code == MP_SERIES_REPORT_STORAGE_STORED &&
		reason == MP_SERIES_REPORT_STORAGE_REASON_NONE &&
		cleanupReason == MP_SERIES_REPORT_STORAGE_REASON_NONE &&
		serializedBytes > 0 && backendBytes == serializedBytes &&
		seriesId != 0 && reportRevision != 0;
}

bool MPMatchSeriesReportStorageBuildPaths(
		const mpCompetitionSeriesReport &report,
		mpSeriesReportStoragePaths &paths,
		mpSeriesReportStorageReason_t *reason ) {
	SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_NONE );
	mpSeriesReportStoragePaths built;
	built.Clear();
	if ( !report.IsInitialized() ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_NOT_INITIALIZED );
		return false;
	}
	if ( !report.IsFinalized() ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_NOT_FINALIZED );
		return false;
	}
	if ( !report.ValidateInvariants() ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_INVALID_REPORT );
		return false;
	}
	const uint64_t seriesId = report.GetIdentity().seriesId;
	const uint64_t reportRevision = report.GetReportRevision();
	if ( seriesId == 0 || reportRevision == 0 ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_INVALID_IDENTITY );
		return false;
	}

	mpSeriesReportStoragePathBuilder finalPath( built.finalQPath,
		static_cast<int>( sizeof( built.finalQPath ) ) );
	finalPath.PutLiteral( MP_SERIES_REPORT_STORAGE_PATH_PREFIX );
	finalPath.PutUnsigned64( seriesId );
	finalPath.PutLiteral( ".json" );

	mpSeriesReportStoragePathBuilder temporaryPath( built.temporaryQPath,
		static_cast<int>( sizeof( built.temporaryQPath ) ) );
	temporaryPath.PutLiteral( built.finalQPath );
	temporaryPath.PutLiteral( ".pending-" );
	temporaryPath.PutUnsigned64( reportRevision );
	if ( !finalPath.Succeeded() || !temporaryPath.Succeeded() ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_PATH_TOO_LONG );
		return false;
	}
	if ( !MPMatchSeriesReportStorageIsPromotionPair( built.temporaryQPath,
			built.finalQPath ) ) {
		SetReason( reason, MP_SERIES_REPORT_STORAGE_REASON_PATH_TOO_LONG );
		return false;
	}
	paths = built;
	return true;
}

bool MPMatchSeriesReportStorageIsFinalQPath( const char *finalQPath ) {
	return IsCanonicalSeriesReportQPath( finalQPath, false );
}

bool MPMatchSeriesReportStorageIsTemporaryQPath( const char *temporaryQPath ) {
	return IsCanonicalSeriesReportQPath( temporaryQPath, true );
}

bool MPMatchSeriesReportStorageIsPromotionPair( const char *temporaryQPath,
		const char *finalQPath ) {
	if ( !MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath ) ||
		!MPMatchSeriesReportStorageIsFinalQPath( finalQPath ) ) {
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

mpSeriesReportStorageResult MPMatchSeriesReportStoragePersist(
		const mpCompetitionSeriesReport &report,
		mpMatchSeriesReportStorageWriter &writer,
		mpSeriesReportStorageWorkspace &workspace ) {
	mpSeriesReportStorageResult result;
	result.Clear();
	if ( !MPMatchSeriesReportStorageBuildPaths( report, result.paths,
			&result.reason ) ) {
		return result;
	}
	result.seriesId = report.GetIdentity().seriesId;
	result.reportRevision = report.GetReportRevision();
	result.code = MP_SERIES_REPORT_STORAGE_FAILED;

	const mpSeriesReportSerializeResult serialized = report.SerializeCanonicalJson(
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
			MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_FAILED :
			MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_PARTIAL;
		RecordCleanup( writer, result );
		return result;
	}
	if ( !writer.Promote( result.paths.temporaryQPath,
			result.paths.finalQPath ) ) {
		result.reason = MP_SERIES_REPORT_STORAGE_REASON_PROMOTION_FAILED;
		RecordCleanup( writer, result );
		return result;
	}
	result.code = MP_SERIES_REPORT_STORAGE_STORED;
	result.reason = MP_SERIES_REPORT_STORAGE_REASON_NONE;
	return result;
}
