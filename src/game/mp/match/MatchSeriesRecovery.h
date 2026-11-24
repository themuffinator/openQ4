//----------------------------------------------------------------
// MatchSeriesRecovery.h
//
// Bounded, allocation-free competition-series recovery records.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_RECOVERY_H__
#define __MP_MATCH_SERIES_RECOVERY_H__

#include "MatchSeries.h"
#include "MatchSeriesReport.h"

#include <stdint.h>

static const uint16_t MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION = 2;
static const uint16_t MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION = 3;
static const uint16_t MP_SERIES_RECOVERY_SCHEMA_VERSION = 4;
static const int MP_SERIES_RECOVERY_MAX_BYTES = 65536;
static const int MP_SERIES_RECOVERY_QPATH_BYTES = 128;

// This is the exact logical state owned by mpCompetitionSeries.  It is not a
// file image: the codec writes each field explicitly in a canonical byte
// order, and ignores no unknown bytes.  Keep additions append-only and gate
// incompatible layouts behind a later recovery schema.
struct mpSeriesRecoveryState {
	uint16_t				schemaVersion;
	mpSeriesState_t		state;
	uint64_t				revision;
	mpSeriesConfiguration	configuration;
	int					currentVetoStep;
	mpSeriesAppliedVeto		appliedVetoes[ MP_SERIES_MAX_VETO_STEPS ];
	int					appliedVetoCount;
	mpSeriesMapDisposition_t mapDisposition[ MP_SERIES_MAX_MAP_POOL ];
	mpSeriesSelectedMap		selectedMaps[ MP_SERIES_MAX_BEST_OF ];
	int					selectedMapCount;
	int					nextSelectionIndex;
	int					currentSelectionIndex;
	mpSeriesMapAttempt		attempts[ MP_SERIES_MAX_MAP_ATTEMPTS ];
	int					attemptCount;
	int					wins[ MP_SERIES_SIDE_COUNT ];
	int					mapLoadFailureCount;

	void					Clear( void );
};

struct mpSeriesRecoveryRecord {
	uint64_t				seriesId;
	uint64_t				linkedSessionId;
	mpSeriesRecoveryState	series;
	bool					hasReport;
	mpSeriesReportCheckpointState report;
	uint64_t				contentDigest;

	void					Clear( void );
};

typedef enum {
	MP_SERIES_RECOVERY_REASON_NONE = 0,
	MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT,
	MP_SERIES_RECOVERY_REASON_INVALID_IDENTITY,
	MP_SERIES_RECOVERY_REASON_INVALID_SERIES,
	MP_SERIES_RECOVERY_REASON_INVALID_REPORT,
	MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH,
	MP_SERIES_RECOVERY_REASON_UNSUPPORTED_SCHEMA,
	MP_SERIES_RECOVERY_REASON_BUFFER_TOO_SMALL,
	MP_SERIES_RECOVERY_REASON_TRUNCATED_RECORD,
	MP_SERIES_RECOVERY_REASON_TRAILING_DATA,
	MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD,
	MP_SERIES_RECOVERY_REASON_CHECKSUM_MISMATCH,
	MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH,
	MP_SERIES_RECOVERY_REASON_PATH_TOO_LONG,
	MP_SERIES_RECOVERY_REASON_TEMP_WRITE_FAILED,
	MP_SERIES_RECOVERY_REASON_TEMP_WRITE_PARTIAL,
	MP_SERIES_RECOVERY_REASON_PROMOTION_FAILED,
	MP_SERIES_RECOVERY_REASON_TEMP_CLEANUP_FAILED,
	MP_SERIES_RECOVERY_REASON_COUNT
} mpSeriesRecoveryReason_t;

struct mpSeriesRecoveryCodecResult {
	mpSeriesRecoveryReason_t	reason;
	int					bytes;
	int					requiredCapacity;
	uint64_t				contentDigest;
	uint32_t				checksum;

	void					Clear( void );
	bool					Succeeded( void ) const;
};

struct mpSeriesRecoveryPaths {
	char					finalQPath[ MP_SERIES_RECOVERY_QPATH_BYTES + 1 ];
	char					temporaryQPath[ MP_SERIES_RECOVERY_QPATH_BYTES + 1 ];

	void					Clear( void );
};

typedef enum {
	MP_SERIES_RECOVERY_STORAGE_STORED = 0,
	MP_SERIES_RECOVERY_STORAGE_REJECTED,
	MP_SERIES_RECOVERY_STORAGE_FAILED,
	MP_SERIES_RECOVERY_STORAGE_CODE_COUNT
} mpSeriesRecoveryStorageCode_t;

struct mpSeriesRecoveryStorageResult {
	mpSeriesRecoveryStorageCode_t code;
	mpSeriesRecoveryReason_t	reason;
	mpSeriesRecoveryReason_t	cleanupReason;
	int					serializedBytes;
	int					backendBytes;
	uint64_t				contentDigest;
	uint32_t				checksum;
	mpSeriesRecoveryPaths	paths;

	void					Clear( void );
	bool					Succeeded( void ) const;
};

// Callers retain this fixed workspace; recovery performs no heap allocation.
struct mpSeriesRecoveryWorkspace {
	uint8_t bytes[ MP_SERIES_RECOVERY_MAX_BYTES ];
};

// Filesystem-neutral atomic replacement boundary.  A failed Promote must
// leave the prior final record unchanged and retain or reject the temporary
// file so the caller can remove it explicitly.
class mpMatchSeriesRecoveryWriter {
public:
	virtual ~mpMatchSeriesRecoveryWriter() {}
	virtual int WriteTemp( const char *temporaryQPath, const void *data,
		int bytes ) = 0;
	virtual bool Promote( const char *temporaryQPath,
		const char *finalQPath ) = 0;
	virtual bool RemoveTemp( const char *temporaryQPath ) = 0;
};

bool MPMatchSeriesRecoveryCapture( const mpCompetitionSeries &series,
	uint64_t seriesId, uint64_t linkedSessionId,
	mpSeriesRecoveryRecord &record,
	mpSeriesRecoveryReason_t *reason = 0 );

// Unified v3 capture.  The series core and mutable report draft are validated
// together and encoded into one checksummed file; one successful promotion is
// therefore the sole durable commit point for both aggregates.
bool MPMatchSeriesRecoveryCapture( const mpCompetitionSeries &series,
	const mpCompetitionSeriesReport &report, uint64_t seriesId,
	uint64_t linkedSessionId, mpSeriesRecoveryRecord &record,
	mpSeriesRecoveryReason_t *reason = 0 );

// Transactional paired restore.  Legacy v2 records decode successfully with
// hasReport=false so operators can diagnose/upgrade them, but this function
// rejects them rather than inventing artifact paths or duplicate map entries.
bool MPMatchSeriesRecoveryRestoreCores( const mpSeriesRecoveryRecord &record,
	mpCompetitionSeries &series, mpCompetitionSeriesReport &report,
	mpSeriesRecoveryReason_t *reason = 0 );

bool MPMatchSeriesRecoveryValidate( const mpSeriesRecoveryRecord &record,
	mpSeriesRecoveryReason_t *reason = 0 );

uint64_t MPMatchSeriesRecoveryComputeContentDigest(
	const mpSeriesRecoveryRecord &record );

mpSeriesRecoveryCodecResult MPMatchSeriesRecoveryEncode(
	const mpSeriesRecoveryRecord &record, void *destination, int capacity );

// Transactional: output remains byte-for-byte unchanged on every failure.
mpSeriesRecoveryCodecResult MPMatchSeriesRecoveryDecode( const void *source,
	int bytes, mpSeriesRecoveryRecord &output );

bool MPMatchSeriesRecoveryBuildPaths( const mpSeriesRecoveryRecord &record,
	mpSeriesRecoveryPaths &paths,
	mpSeriesRecoveryReason_t *reason = 0 );
// Constructs the only final qpath accepted by the production loader.  The
// destination is unchanged on failure and seriesId must be nonzero.
bool MPMatchSeriesRecoveryBuildFinalQPath( uint64_t seriesId,
	char *destination, int capacity,
	mpSeriesRecoveryReason_t *reason = 0 );
bool MPMatchSeriesRecoveryIsFinalQPath( const char *finalQPath );
bool MPMatchSeriesRecoveryIsTemporaryQPath( const char *temporaryQPath );
bool MPMatchSeriesRecoveryIsPromotionPair( const char *temporaryQPath,
	const char *finalQPath );

mpSeriesRecoveryStorageResult MPMatchSeriesRecoveryPersist(
	const mpSeriesRecoveryRecord &record,
	mpMatchSeriesRecoveryWriter &writer,
	mpSeriesRecoveryWorkspace &workspace );

#endif // __MP_MATCH_SERIES_RECOVERY_H__
