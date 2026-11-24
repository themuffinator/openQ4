//----------------------------------------------------------------
// MatchSeriesReportStorage.h
//
// Deterministic, bounded persistence boundary for finalized competition-
// series report JSON.  The core remains filesystem-neutral and accepts no
// caller-selected output path.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_REPORT_STORAGE_H__
#define __MP_MATCH_SERIES_REPORT_STORAGE_H__

#include "MatchSeriesReport.h"

static const int MP_SERIES_REPORT_STORAGE_JSON_BYTES =
	MP_SERIES_REPORT_MAX_JSON_BYTES;
static const int MP_SERIES_REPORT_STORAGE_QPATH_BYTES = 128;

typedef enum {
	MP_SERIES_REPORT_STORAGE_STORED = 0,
	MP_SERIES_REPORT_STORAGE_REJECTED,
	MP_SERIES_REPORT_STORAGE_FAILED,
	MP_SERIES_REPORT_STORAGE_CODE_COUNT
} mpSeriesReportStorageCode_t;

typedef enum {
	MP_SERIES_REPORT_STORAGE_REASON_NONE = 0,
	MP_SERIES_REPORT_STORAGE_REASON_NOT_INITIALIZED,
	MP_SERIES_REPORT_STORAGE_REASON_NOT_FINALIZED,
	MP_SERIES_REPORT_STORAGE_REASON_INVALID_REPORT,
	MP_SERIES_REPORT_STORAGE_REASON_INVALID_IDENTITY,
	MP_SERIES_REPORT_STORAGE_REASON_PATH_TOO_LONG,
	MP_SERIES_REPORT_STORAGE_REASON_SERIALIZE_FAILED,
	MP_SERIES_REPORT_STORAGE_REASON_JSON_TOO_LARGE,
	MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_FAILED,
	MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_PARTIAL,
	MP_SERIES_REPORT_STORAGE_REASON_PROMOTION_FAILED,
	MP_SERIES_REPORT_STORAGE_REASON_TEMP_CLEANUP_FAILED,
	MP_SERIES_REPORT_STORAGE_REASON_COUNT
} mpSeriesReportStorageReason_t;

struct mpSeriesReportStoragePaths {
	char finalQPath[ MP_SERIES_REPORT_STORAGE_QPATH_BYTES + 1 ];
	char temporaryQPath[ MP_SERIES_REPORT_STORAGE_QPATH_BYTES + 1 ];

	void Clear( void );
};

struct mpSeriesReportStorageResult {
	mpSeriesReportStorageCode_t code;
	mpSeriesReportStorageReason_t reason;
	mpSeriesReportStorageReason_t cleanupReason;
	int serializedBytes;
	int backendBytes;
	uint64_t seriesId;
	uint64_t reportRevision;
	mpSeriesReportStoragePaths paths;

	void Clear( void );
	bool Succeeded( void ) const;
};

// The caller retains this fixed workspace.  Persistence performs no heap
// allocation and never places the bounded JSON artifact on a transient stack.
struct mpSeriesReportStorageWorkspace {
	char json[ MP_SERIES_REPORT_STORAGE_JSON_BYTES ];
};

// Filesystem-neutral atomic replacement boundary.  WriteTemp must replace or
// truncate only the supplied temporary qpath.  Promote returning false must
// leave the previous final artifact unchanged.  Success consumes the
// temporary file; failed write/promotion paths are offered to RemoveTemp.
class mpMatchSeriesReportStorageWriter {
public:
	virtual ~mpMatchSeriesReportStorageWriter() {}

	virtual int WriteTemp( const char *temporaryQPath, const void *data,
		int bytes ) = 0;
	virtual bool Promote( const char *temporaryQPath,
		const char *finalQPath ) = 0;
	virtual bool RemoveTemp( const char *temporaryQPath ) = 0;
};

// Paths are derived only from validated, finalized report identity and
// revision.  On failure paths remains byte-for-byte unchanged.
bool MPMatchSeriesReportStorageBuildPaths(
	const mpCompetitionSeriesReport &report,
	mpSeriesReportStoragePaths &paths,
	mpSeriesReportStorageReason_t *reason = 0 );

// Strict validators accept only the exact canonical values emitted above.
// Decimal components have no leading zeroes and must fit uint64_t.
bool MPMatchSeriesReportStorageIsFinalQPath( const char *finalQPath );
bool MPMatchSeriesReportStorageIsTemporaryQPath( const char *temporaryQPath );
bool MPMatchSeriesReportStorageIsPromotionPair( const char *temporaryQPath,
	const char *finalQPath );

// Serializes the complete immutable report into the fixed workspace before
// any backend call, then performs WriteTemp -> Promote.  Every failed backend
// mutation attempts exact temporary-path cleanup and reports both the primary
// and cleanup outcomes.  The report is const and unchanged on every path.
mpSeriesReportStorageResult MPMatchSeriesReportStoragePersist(
	const mpCompetitionSeriesReport &report,
	mpMatchSeriesReportStorageWriter &writer,
	mpSeriesReportStorageWorkspace &workspace );

#endif // __MP_MATCH_SERIES_REPORT_STORAGE_H__
