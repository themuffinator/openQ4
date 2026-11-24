//----------------------------------------------------------------
// MatchSeriesRecoveryFileSystem.h
//
// Production idFileSystem adapter for competition-series recovery.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_H__
#define __MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_H__

#include "MatchSeriesRecovery.h"

class idFileSystem;

// Revalidates every qpath before reaching the engine filesystem and writes
// only beneath fs_savepath.
class mpMatchSeriesRecoveryFileSystemWriter final :
	public mpMatchSeriesRecoveryWriter {
public:
	explicit mpMatchSeriesRecoveryFileSystemWriter( idFileSystem *backend );

	virtual int WriteTemp( const char *temporaryQPath, const void *data,
		int bytes ) override;
	virtual bool Promote( const char *temporaryQPath,
		const char *finalQPath ) override;
	virtual bool RemoveTemp( const char *temporaryQPath ) override;

private:
	idFileSystem *fileSystemBackend;
};

typedef enum {
	MP_SERIES_RECOVERY_LOAD_LOADED = 0,
	MP_SERIES_RECOVERY_LOAD_REJECTED,
	MP_SERIES_RECOVERY_LOAD_FAILED,
	MP_SERIES_RECOVERY_LOAD_CODE_COUNT
} mpSeriesRecoveryLoadCode_t;

typedef enum {
	MP_SERIES_RECOVERY_LOAD_REASON_NONE = 0,
	MP_SERIES_RECOVERY_LOAD_REASON_INVALID_ARGUMENT,
	MP_SERIES_RECOVERY_LOAD_REASON_INVALID_IDENTITY,
	MP_SERIES_RECOVERY_LOAD_REASON_PATH_FAILED,
	MP_SERIES_RECOVERY_LOAD_REASON_OPEN_FAILED,
	MP_SERIES_RECOVERY_LOAD_REASON_INVALID_LENGTH,
	MP_SERIES_RECOVERY_LOAD_REASON_OVERSIZED_RECORD,
	MP_SERIES_RECOVERY_LOAD_REASON_READ_FAILED,
	MP_SERIES_RECOVERY_LOAD_REASON_READ_PARTIAL,
	MP_SERIES_RECOVERY_LOAD_REASON_DECODE_REJECTED,
	MP_SERIES_RECOVERY_LOAD_REASON_IDENTITY_MISMATCH,
	MP_SERIES_RECOVERY_LOAD_REASON_COUNT
} mpSeriesRecoveryLoadReason_t;

struct mpSeriesRecoveryLoadResult {
	mpSeriesRecoveryLoadCode_t code;
	mpSeriesRecoveryLoadReason_t reason;
	mpSeriesRecoveryReason_t decodeReason;
	int expectedBytes;
	int readBytes;
	char finalQPath[ MP_SERIES_RECOVERY_QPATH_BYTES + 1 ];

	void Clear( void );
	bool Succeeded( void ) const;
};

// Narrow stream seam used by the bounded loader.  The production wrapper
// adapts an idFile; hostile contracts supply deterministic short/oversized
// streams without mocking the complete engine filesystem.
class mpMatchSeriesRecoveryReadStream {
public:
	virtual ~mpMatchSeriesRecoveryReadStream() {}
	virtual int Length( void ) = 0;
	virtual int Read( void *destination, int bytes ) = 0;
};

// Reads and validates an already-open stream.  Output is unchanged unless the
// complete record decodes and its identity equals expectedSeriesId.
mpSeriesRecoveryLoadResult MPMatchSeriesRecoveryLoadStream(
	mpMatchSeriesRecoveryReadStream &stream, uint64_t expectedSeriesId,
	mpSeriesRecoveryWorkspace &workspace, mpSeriesRecoveryRecord &output );

// Constructs only match-series/series-<id>.oq4series, resolves that validated
// qpath beneath fs_savepath, opens the resulting explicit path, and delegates
// to the bounded transactional stream loader.
mpSeriesRecoveryLoadResult MPMatchSeriesRecoveryLoadFileSystem(
	idFileSystem *backend, uint64_t expectedSeriesId,
	mpSeriesRecoveryWorkspace &workspace, mpSeriesRecoveryRecord &output );

#endif // __MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_H__
