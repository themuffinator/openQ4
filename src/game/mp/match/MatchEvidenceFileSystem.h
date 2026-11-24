//----------------------------------------------------------------
// MatchEvidenceFileSystem.h
//
// Production idFileSystem adapter for atomic match-evidence persistence.
//----------------------------------------------------------------

#ifndef __MP_MATCH_EVIDENCE_FILE_SYSTEM_H__
#define __MP_MATCH_EVIDENCE_FILE_SYSTEM_H__

#include "MatchEvidenceStorage.h"

class idFileSystem;

// This adapter is intentionally rooted at fs_savepath.  Its public writer
// surface revalidates the canonical evidence qpaths even though the engine
// filesystem also rejects unsafe mutation paths.  That gives both boundaries
// fail-closed behavior and prevents this class from becoming a general file
// writer if it is called outside MPMatchEvidenceStoragePersist.
class mpMatchEvidenceFileSystemWriter final : public mpMatchEvidenceStorageWriter {
public:
	explicit mpMatchEvidenceFileSystemWriter( idFileSystem *backend );

	virtual int WriteTemp( const char *temporaryQPath, const void *data,
		int bytes ) override;
	virtual bool Promote( const char *temporaryQPath,
		const char *finalQPath ) override;
	virtual bool RemoveTemp( const char *temporaryQPath ) override;

private:
	idFileSystem *fileSystemBackend;
};

#endif // __MP_MATCH_EVIDENCE_FILE_SYSTEM_H__
