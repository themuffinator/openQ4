//----------------------------------------------------------------
// MatchSeriesRecoveryFileSystem.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_STANDALONE_TEST )
	#include "MatchSeriesRecoveryFileSystem.h"
	#include <string.h>
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSeriesRecoveryFileSystem.h"
#endif

namespace {

static void CopyFinalQPath( char destination[ MP_SERIES_RECOVERY_QPATH_BYTES + 1 ],
		const char *source ) {
	int index = 0;
	while ( source != 0 && index < MP_SERIES_RECOVERY_QPATH_BYTES &&
		source[ index ] != '\0' ) {
		destination[ index ] = source[ index ];
		++index;
	}
	destination[ index ] = '\0';
}

#if !defined( MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_STANDALONE_TEST )

static const char *const MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT = "fs_savepath";

class mpMatchSeriesRecoveryIdFileStream final :
		public mpMatchSeriesRecoveryReadStream {
public:
	explicit mpMatchSeriesRecoveryIdFileStream( idFile *source ) : file( source ) {}

	virtual int Length( void ) override {
		return file != 0 ? file->Length() : -1;
	}
	virtual int Read( void *destination, int bytes ) override {
		return file != 0 ? file->Read( destination, bytes ) : -1;
	}

private:
	idFile *file;
};

#endif

} // namespace

void mpSeriesRecoveryLoadResult::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	code = MP_SERIES_RECOVERY_LOAD_REJECTED;
	reason = MP_SERIES_RECOVERY_LOAD_REASON_NONE;
	decodeReason = MP_SERIES_RECOVERY_REASON_NONE;
}

bool mpSeriesRecoveryLoadResult::Succeeded( void ) const {
	return code == MP_SERIES_RECOVERY_LOAD_LOADED &&
		reason == MP_SERIES_RECOVERY_LOAD_REASON_NONE &&
		decodeReason == MP_SERIES_RECOVERY_REASON_NONE && expectedBytes > 0 &&
		readBytes == expectedBytes;
}

mpSeriesRecoveryLoadResult MPMatchSeriesRecoveryLoadStream(
		mpMatchSeriesRecoveryReadStream &stream, uint64_t expectedSeriesId,
		mpSeriesRecoveryWorkspace &workspace, mpSeriesRecoveryRecord &output ) {
	mpSeriesRecoveryLoadResult result;
	result.Clear();
	if ( expectedSeriesId == 0 ) {
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_INVALID_IDENTITY;
		return result;
	}
	result.expectedBytes = stream.Length();
	if ( result.expectedBytes <= 0 ) {
		result.code = MP_SERIES_RECOVERY_LOAD_FAILED;
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_INVALID_LENGTH;
		return result;
	}
	if ( result.expectedBytes > MP_SERIES_RECOVERY_MAX_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_OVERSIZED_RECORD;
		return result;
	}

	while ( result.readBytes < result.expectedBytes ) {
		const int remaining = result.expectedBytes - result.readBytes;
		const int read = stream.Read( workspace.bytes + result.readBytes, remaining );
		if ( read <= 0 || read > remaining ) {
			result.code = MP_SERIES_RECOVERY_LOAD_FAILED;
			result.reason = result.readBytes > 0 ?
				MP_SERIES_RECOVERY_LOAD_REASON_READ_PARTIAL :
				MP_SERIES_RECOVERY_LOAD_REASON_READ_FAILED;
			return result;
		}
		result.readBytes += read;
	}
	if ( result.readBytes != result.expectedBytes ) {
		result.code = MP_SERIES_RECOVERY_LOAD_FAILED;
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_READ_PARTIAL;
		return result;
	}

	mpSeriesRecoveryRecord candidate;
	candidate.Clear();
	const mpSeriesRecoveryCodecResult decoded = MPMatchSeriesRecoveryDecode(
		workspace.bytes, result.readBytes, candidate );
	if ( !decoded.Succeeded() ) {
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_DECODE_REJECTED;
		result.decodeReason = decoded.reason;
		return result;
	}
	if ( candidate.seriesId != expectedSeriesId ) {
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_IDENTITY_MISMATCH;
		return result;
	}
	output = candidate;
	result.code = MP_SERIES_RECOVERY_LOAD_LOADED;
	result.reason = MP_SERIES_RECOVERY_LOAD_REASON_NONE;
	return result;
}

#if !defined( MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_STANDALONE_TEST )

mpMatchSeriesRecoveryFileSystemWriter::mpMatchSeriesRecoveryFileSystemWriter(
		idFileSystem *backend ) : fileSystemBackend( backend ) {
}

int mpMatchSeriesRecoveryFileSystemWriter::WriteTemp(
		const char *temporaryQPath, const void *data, int bytes ) {
	if ( fileSystemBackend == 0 || data == 0 || bytes < 1 ||
		bytes > MP_SERIES_RECOVERY_MAX_BYTES ||
		!MPMatchSeriesRecoveryIsTemporaryQPath( temporaryQPath ) ) {
		return -1;
	}
	idFile *file = fileSystemBackend->OpenFileWrite( temporaryQPath,
		MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT );
	if ( file == 0 ) {
		return -1;
	}

	int committedBytes = 0;
	bool writeFailed = false;
	const unsigned char *source = static_cast<const unsigned char *>( data );
	while ( committedBytes < bytes ) {
		const int remaining = bytes - committedBytes;
		const int written = file->Write( source + committedBytes, remaining );
		if ( written <= 0 || written > remaining ) {
			writeFailed = true;
			break;
		}
		committedBytes += written;
	}
	const bool synced = !writeFailed && committedBytes == bytes && file->Sync();
	fileSystemBackend->CloseFile( file );
	if ( !synced ) {
		return writeFailed && committedBytes > 0 ? committedBytes : -1;
	}
	return committedBytes;
}

bool mpMatchSeriesRecoveryFileSystemWriter::Promote(
		const char *temporaryQPath, const char *finalQPath ) {
	return fileSystemBackend != 0 &&
		MPMatchSeriesRecoveryIsPromotionPair( temporaryQPath, finalQPath ) &&
		fileSystemBackend->PromoteFile( temporaryQPath, finalQPath,
			MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT );
}

bool mpMatchSeriesRecoveryFileSystemWriter::RemoveTemp(
		const char *temporaryQPath ) {
	return fileSystemBackend != 0 &&
		MPMatchSeriesRecoveryIsTemporaryQPath( temporaryQPath ) &&
		fileSystemBackend->RemoveFileChecked( temporaryQPath,
			MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT );
}

mpSeriesRecoveryLoadResult MPMatchSeriesRecoveryLoadFileSystem(
		idFileSystem *backend, uint64_t expectedSeriesId,
		mpSeriesRecoveryWorkspace &workspace, mpSeriesRecoveryRecord &output ) {
	mpSeriesRecoveryLoadResult result;
	result.Clear();
	if ( backend == 0 ) {
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_INVALID_ARGUMENT;
		return result;
	}
	mpSeriesRecoveryReason_t pathReason = MP_SERIES_RECOVERY_REASON_NONE;
	if ( !MPMatchSeriesRecoveryBuildFinalQPath( expectedSeriesId,
			result.finalQPath, static_cast<int>( sizeof( result.finalQPath ) ),
			&pathReason ) ||
		!MPMatchSeriesRecoveryIsFinalQPath( result.finalQPath ) ) {
		result.reason = expectedSeriesId == 0 ?
			MP_SERIES_RECOVERY_LOAD_REASON_INVALID_IDENTITY :
			MP_SERIES_RECOVERY_LOAD_REASON_PATH_FAILED;
		return result;
	}

	const char *explicitPath = backend->RelativePathToOSPath( result.finalQPath,
		MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT );
	if ( explicitPath == 0 || explicitPath[ 0 ] == '\0' ) {
		result.code = MP_SERIES_RECOVERY_LOAD_FAILED;
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_PATH_FAILED;
		return result;
	}
	idFile *file = backend->OpenExplicitFileRead( explicitPath );
	if ( file == 0 ) {
		result.code = MP_SERIES_RECOVERY_LOAD_FAILED;
		result.reason = MP_SERIES_RECOVERY_LOAD_REASON_OPEN_FAILED;
		return result;
	}
	mpMatchSeriesRecoveryIdFileStream stream( file );
	mpSeriesRecoveryLoadResult loaded = MPMatchSeriesRecoveryLoadStream( stream,
		expectedSeriesId, workspace, output );
	backend->CloseFile( file );
	CopyFinalQPath( loaded.finalQPath, result.finalQPath );
	return loaded;
}

#endif

