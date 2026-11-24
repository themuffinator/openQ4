//----------------------------------------------------------------
// MatchEvidenceStorage.h
//
// Deterministic, bounded persistence boundary for match evidence JSON.
//----------------------------------------------------------------

#ifndef __MP_MATCH_EVIDENCE_STORAGE_H__
#define __MP_MATCH_EVIDENCE_STORAGE_H__

#include "MatchEvidence.h"

static const int MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES = 262144;
static const int MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES = 48;
static const int MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES = 160;

typedef enum {
	MP_EVIDENCE_STORAGE_STORED = 0,
	MP_EVIDENCE_STORAGE_REJECTED,
	MP_EVIDENCE_STORAGE_FAILED,
	MP_EVIDENCE_STORAGE_CODE_COUNT
} mpEvidenceStorageCode_t;

typedef enum {
	MP_EVIDENCE_STORAGE_REASON_NONE = 0,
	MP_EVIDENCE_STORAGE_REASON_NOT_INITIALIZED,
	MP_EVIDENCE_STORAGE_REASON_INVALID_JOURNAL,
	MP_EVIDENCE_STORAGE_REASON_INVALID_IDENTITY,
	MP_EVIDENCE_STORAGE_REASON_PATH_TOO_LONG,
	MP_EVIDENCE_STORAGE_REASON_SERIALIZE_FAILED,
	MP_EVIDENCE_STORAGE_REASON_JSON_TOO_LARGE,
	MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_FAILED,
	MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_PARTIAL,
	MP_EVIDENCE_STORAGE_REASON_PROMOTION_FAILED,
	MP_EVIDENCE_STORAGE_REASON_TEMP_CLEANUP_FAILED,
	MP_EVIDENCE_STORAGE_REASON_COUNT
} mpEvidenceStorageReason_t;

struct mpEvidenceStoragePaths {
	char mapToken[ MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES + 1 ];
	char finalQPath[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];
	char temporaryQPath[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];

	void Clear( void );
};

struct mpEvidenceStorageResult {
	mpEvidenceStorageCode_t	code;
	mpEvidenceStorageReason_t	reason;
	mpEvidenceStorageReason_t	cleanupReason;
	int					serializedBytes;
	int					backendBytes;
	mpEvidenceStoragePaths	paths;

	void Clear( void );
	bool Succeeded( void ) const;
};

// Callers own this fixed workspace so the persistence path never performs an
// unbounded allocation or places a large artifact buffer on a transient stack.
struct mpEvidenceStorageWorkspace {
	char json[ MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES ];
};

// Narrow backend boundary.  Production implements this with the engine file
// system; standalone contracts inject deterministic failure writers.
class mpMatchEvidenceStorageWriter {
public:
	virtual ~mpMatchEvidenceStorageWriter() {}

	// Replaces/truncates the temporary qpath and returns the number of bytes
	// committed, or -1 on failure.  A short non-negative result is a failure.
	virtual int WriteTemp( const char *temporaryQPath, const void *data, int bytes ) = 0;

	// Atomically replaces finalQPath with temporaryQPath.  False must leave the
	// previous final artifact unchanged.  Success consumes the temporary file.
	virtual bool Promote( const char *temporaryQPath, const char *finalQPath ) = 0;

	// Best-effort removal after a failed write or promotion.
	virtual bool RemoveTemp( const char *temporaryQPath ) = 0;
};

// Paths are derived only from validated journal metadata.  No caller path or
// caller-selected filename enters this API.
bool MPMatchEvidenceStorageBuildPaths( const mpMatchEvidence &journal,
	mpEvidenceStoragePaths &paths, mpEvidenceStorageReason_t *reason = 0 );

// These validators deliberately accept only the canonical, server-authored
// evidence filenames emitted by MPMatchEvidenceStorageBuildPaths.  They are
// also used by the production filesystem adapter so its narrow writer methods
// remain safe when called independently of MPMatchEvidenceStoragePersist.
bool MPMatchEvidenceStorageIsFinalQPath( const char *finalQPath );
bool MPMatchEvidenceStorageIsTemporaryQPath( const char *temporaryQPath );
bool MPMatchEvidenceStorageIsPromotionPair( const char *temporaryQPath,
	const char *finalQPath );

// Serializes completely, writes a temporary artifact, then atomically promotes
// it.  The journal is const and no match/game state is reachable from here.
mpEvidenceStorageResult MPMatchEvidenceStoragePersist(
	const mpMatchEvidence &journal,
	mpMatchEvidenceStorageWriter &writer,
	mpEvidenceStorageWorkspace &workspace );

#endif // __MP_MATCH_EVIDENCE_STORAGE_H__
