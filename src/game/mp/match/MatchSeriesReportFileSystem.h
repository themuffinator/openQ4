//----------------------------------------------------------------
// MatchSeriesReportFileSystem.h
//
// Production idFileSystem adapter for atomic competition-series reports.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_REPORT_FILE_SYSTEM_H__
#define __MP_MATCH_SERIES_REPORT_FILE_SYSTEM_H__

#include "MatchSeriesReportStorage.h"

class idFileSystem;

// The storage core owns every output qpath.  This adapter revalidates that
// canonical path at the engine boundary and permits mutations only beneath
// fs_savepath, so it cannot be repurposed as a caller-selected file writer.
class mpMatchSeriesReportFileSystemWriter final :
	public mpMatchSeriesReportStorageWriter {
public:
	explicit mpMatchSeriesReportFileSystemWriter( idFileSystem *backend );

	virtual int WriteTemp( const char *temporaryQPath, const void *data,
		int bytes ) override;
	virtual bool Promote( const char *temporaryQPath,
		const char *finalQPath ) override;
	virtual bool RemoveTemp( const char *temporaryQPath ) override;

private:
	idFileSystem *fileSystemBackend;
};

#endif // __MP_MATCH_SERIES_REPORT_FILE_SYSTEM_H__
