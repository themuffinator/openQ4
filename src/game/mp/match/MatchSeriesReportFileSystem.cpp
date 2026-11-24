//----------------------------------------------------------------
// MatchSeriesReportFileSystem.cpp
//----------------------------------------------------------------

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "MatchSeriesReportFileSystem.h"

namespace {

static const char *const MP_MATCH_SERIES_REPORT_WRITABLE_ROOT = "fs_savepath";

} // namespace

mpMatchSeriesReportFileSystemWriter::mpMatchSeriesReportFileSystemWriter(
		idFileSystem *backend ) :
	fileSystemBackend( backend ) {
}

int mpMatchSeriesReportFileSystemWriter::WriteTemp(
		const char *temporaryQPath, const void *data, int bytes ) {
	if ( fileSystemBackend == 0 || data == 0 || bytes < 1 ||
		bytes >= MP_SERIES_REPORT_STORAGE_JSON_BYTES ||
		!MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath ) ) {
		return -1;
	}

	idFile *file = fileSystemBackend->OpenFileWrite( temporaryQPath,
		MP_MATCH_SERIES_REPORT_WRITABLE_ROOT );
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

bool mpMatchSeriesReportFileSystemWriter::Promote(
		const char *temporaryQPath, const char *finalQPath ) {
	return fileSystemBackend != 0 &&
		MPMatchSeriesReportStorageIsPromotionPair( temporaryQPath, finalQPath ) &&
		fileSystemBackend->PromoteFile( temporaryQPath, finalQPath,
			MP_MATCH_SERIES_REPORT_WRITABLE_ROOT );
}

bool mpMatchSeriesReportFileSystemWriter::RemoveTemp(
		const char *temporaryQPath ) {
	return fileSystemBackend != 0 &&
		MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath ) &&
		fileSystemBackend->RemoveFileChecked( temporaryQPath,
			MP_MATCH_SERIES_REPORT_WRITABLE_ROOT );
}
