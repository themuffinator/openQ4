#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for bounded match-evidence persistence."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/mpgame/mp/match"
HEADER = MATCH_DIR / "MatchEvidenceStorage.h"
SOURCE = MATCH_DIR / "MatchEvidenceStorage.cpp"
EVIDENCE_SOURCE = MATCH_DIR / "MatchEvidence.cpp"
FILESYSTEM_HEADER = MATCH_DIR / "MatchEvidenceFileSystem.h"
FILESYSTEM_SOURCE = MATCH_DIR / "MatchEvidenceFileSystem.cpp"
def _resolve_engine_root() -> Path:
    """Locate a sibling openQ4 checkout, if the caller has one.

    The directory is named "openQ4"; hardcoding "OpenQ4" only ever resolved on
    case-insensitive filesystems, so on Linux the engine-side assertions were
    skipped even when the engine was checked out beside this repository.
    """

    override = os.environ.get("OPENQ4_ENGINE_REPO", "").strip()
    if override:
        return Path(override)
    # The game sources live in the engine repository now, so this checkout is
    # the engine. Looking for a sibling named openQ4 first tested a different
    # checkout from a worktree, or none at all.
    if (ROOT / "src/framework").is_dir():
        return ROOT
    for name in ("openQ4", "OpenQ4"):
        candidate = ROOT.parent / name
        if candidate.is_dir():
            return candidate
    return ROOT.parent / "openQ4"


ENGINE_ROOT = _resolve_engine_root()
ENGINE_FILESYSTEM_HEADER = ENGINE_ROOT / "src/framework/FileSystem.h"
ENGINE_FILESYSTEM_SOURCE = ENGINE_ROOT / "src/framework/FileSystem.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"expected {first!r} before {second!r} in {context}")


def static_contracts(header: str, source: str, adapter_header: str,
                     adapter_source: str) -> None:
    combined = header + source
    for token in (
        "MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES = 262144",
        "MP_MATCH_EVIDENCE_STORAGE_MAP_TOKEN_BYTES = 48",
        "MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES = 160",
        "class mpMatchEvidenceStorageWriter",
        "virtual int WriteTemp",
        "virtual bool Promote",
        "virtual bool RemoveTemp",
        "mpEvidenceStorageWorkspace",
        "mpEvidenceStorageResult",
        "MPMatchEvidenceStorageBuildPaths",
        "MPMatchEvidenceStorageIsFinalQPath",
        "MPMatchEvidenceStorageIsTemporaryQPath",
        "MPMatchEvidenceStorageIsPromotionPair",
        "MPMatchEvidenceStoragePersist",
    ):
        require(combined, token, "bounded persistence API")

    for token in (
        "MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_FAILED",
        "MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_PARTIAL",
        "MP_EVIDENCE_STORAGE_REASON_PROMOTION_FAILED",
        "MP_EVIDENCE_STORAGE_REASON_TEMP_CLEANUP_FAILED",
        "MP_EVIDENCE_STORAGE_REASON_JSON_TOO_LARGE",
    ):
        require(header, token, "explicit storage failure reasons")

    require(header, "const mpMatchEvidence &journal", "immutable journal boundary")
    if header.count("const mpMatchEvidence &journal") != 2:
        raise AssertionError("path construction and persistence must both take a const journal")
    for forbidden in (
        "idFileSystem",
        "fileSystem",
        "fopen",
        "ofstream",
        "std::filesystem",
        "std::thread",
        "CreateThread",
        "rename(",
        "MoveFile",
        "CopyFile",
        "system(",
        "popen(",
        "cmdSystem",
        "gameLocal",
        "MatchSession.h",
    ):
        reject(combined, forbidden, "injectable engine-neutral storage core")

    # The only path root is authored in source.  Journal metadata contributes
    # numeric IDs and a sanitized token, never a path argument.
    require(source,
            'MP_MATCH_EVIDENCE_STORAGE_PATH_PREFIX[] =\n\t"match-results/session-"',
            "server-owned artifact root")
    require(source, "finalPath.PutLiteral( MP_MATCH_EVIDENCE_STORAGE_PATH_PREFIX )",
            "server-owned artifact path builder")
    if source.count("match-results/") != 1:
        raise AssertionError("storage must have exactly one fixed qpath root")
    for token in (
        "BuildSanitizedMapToken",
        "componentStart = inputLength + 1",
        "IsAsciiAlphaNumeric",
        "LowerAscii",
        'memcpy( token, "map", 3 )',
        "finalPath.PutUnsigned64( metadata.sessionId )",
        "finalPath.PutUnsigned64( metadata.seriesId )",
        "temporaryPath.PutUnsigned64( journal.GetEvidenceRevision() )",
    ):
        require(source, token, "deterministic server-owned qpath")
    if re.search(r"MPMatchEvidenceStorage(?:BuildPaths|Persist)\([^)]*char\s*\*",
                 header, re.DOTALL):
        raise AssertionError("public persistence entry points accept a caller path")

    persist_at = source.index("mpEvidenceStorageResult MPMatchEvidenceStoragePersist")
    persist = source[persist_at:]
    require_before(persist, "SerializeCanonicalJson", "writer.WriteTemp",
                   "complete serialization before write")
    require_before(persist, "writer.WriteTemp", "writer.Promote",
                   "temporary write before atomic promotion")
    require(persist, "result.backendBytes != result.serializedBytes",
            "short-write rejection")
    require(persist, "RecordCleanup( writer, result )", "failed-temp cleanup")
    require(header, "False must leave the\n\t// previous final artifact unchanged.",
            "atomic promotion backend contract")
    require(source, "MP_MATCH_EVIDENCE_STORAGE_STANDALONE_TEST",
            "standalone compile seam")

    adapter = adapter_header + adapter_source
    for token in (
        "class mpMatchEvidenceFileSystemWriter",
        "idFileSystem *fileSystemBackend",
        "OpenFileWrite",
        "file->Write",
        "file->Sync()",
        "CloseFile( file )",
        "PromoteFile",
        "RemoveFileChecked",
        'MP_MATCH_EVIDENCE_WRITABLE_ROOT = "fs_savepath"',
    ):
        require(adapter, token, "production evidence filesystem adapter")
    require_before(adapter_source, "MPMatchEvidenceStorageIsTemporaryQPath",
                   "OpenFileWrite", "temporary path validation before write")
    require_before(adapter_source, "MPMatchEvidenceStorageIsPromotionPair",
                   "PromoteFile", "promotion-pair validation before rename")
    remove_method = adapter_source[adapter_source.index(
        "bool mpMatchEvidenceFileSystemWriter::RemoveTemp"):]
    require_before(remove_method, "MPMatchEvidenceStorageIsTemporaryQPath",
                   "RemoveFileChecked", "temporary path validation before cleanup")
    for forbidden in (
        "WriteFile(", "CopyFile", "RelativePathToOSPath", "RemoveFile(",
        "rename(", "MoveFile", "SDL_RenamePath", "fopen", "ofstream",
        "fs_cdpath", "fs_basepath",
    ):
        reject(adapter, forbidden, "narrow evidence filesystem adapter")

    companion_filesystem_header = read(ROOT / "src/framework/FileSystem.h")
    filesystem_headers = [companion_filesystem_header]
    if ENGINE_FILESYSTEM_HEADER.is_file():
        filesystem_headers.append(read(ENGINE_FILESYSTEM_HEADER))
    for filesystem_header in filesystem_headers:
        require(filesystem_header, "virtual bool\t\t\tRemoveFileChecked(",
                "checked cleanup API in both filesystem header trees")
        require(filesystem_header,
                "Returns true when the file was removed or was already absent.",
                "checked cleanup missing-file semantics")
    # A standalone openQ4-game checkout can compile and test the adapter.  When
    # the engine sibling is present, also verify the concrete mutation backend;
    # the engine repository carries the same checks in its own filesystem test.
    if ENGINE_FILESYSTEM_SOURCE.is_file():
        engine_source = read(ENGINE_FILESYSTEM_SOURCE)
        checked_at = engine_source.index("bool idFileSystemLocal::RemoveFileChecked")
        checked_end = engine_source.index("idFileSystemLocal::RemoveFile\n", checked_at)
        checked_remove = engine_source[checked_at:checked_end]
        for token in (
            "FS_ValidateRelativeWritePath",
            "BuildOSPath( root, gameFolder, relativePath )",
            "removalError == ENOENT", "return false;",
        ):
            require(checked_remove, token,
                    "checked exact-root cleanup implementation")
        reject(checked_remove, "fs_cdpath",
               "checked exact-root cleanup implementation")

        promote_at = engine_source.index("bool idFileSystemLocal::PromoteFile")
        promote_end = engine_source.index(
            "idFileSystemLocal::SetIsFileLoadingAllowed", promote_at)
        promote = engine_source[promote_at:promote_end]
        for token in ("SDL_RenamePath", "MoveFileExA", "rename("):
            require(promote, token,
                    "cross-platform atomic promotion implementation")
        for forbidden in ("CopyFile", "WriteFile(", "OpenFileWrite"):
            reject(promote, forbidden,
                   "atomic promotion without copy fallback")

    # Storage adds no network identity, credentials, arbitrary backend text, or
    # caller-controlled filename fields.
    for field in (
        "ipAddress", "remoteAddress", "credential", "password", "secret",
        "userinfo", "backendText", "callerPath", "outputPath",
    ):
        if re.search(rf"\b{re.escape(field)}\b", combined, re.IGNORECASE):
            raise AssertionError(f"privacy/path-sensitive field leaked into storage: {field}")

    listing = subprocess.run(
        [
            "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "mpgame/mp/match/MatchEvidenceStorage.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("MatchEvidenceStorage.cpp is absent from the MP source list")
    if "mpgame/mp/match/MatchEvidenceFileSystem.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("MatchEvidenceFileSystem.cpp is absent from the MP source list")


HARNESS = r'''
#include "mpgame/mp/match/MatchEvidenceStorage.h"

#include <string.h>

#define CHECK( value ) do { if ( !( value ) ) return __LINE__; } while ( 0 )

static mpEvidenceStorageWorkspace workspace;
static char baseline[ MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES ];
static char captured[ MP_MATCH_EVIDENCE_STORAGE_JSON_BYTES ];

class FakeWriter : public mpMatchEvidenceStorageWriter {
public:
	enum Mode { SUCCESS, WRITE_FAIL, WRITE_PARTIAL, PROMOTE_FAIL };

	FakeWriter( Mode failureMode, bool cleanupWorks = true ) :
		mode( failureMode ), cleanupSucceeds( cleanupWorks ), writeCalls( 0 ),
		promoteCalls( 0 ), cleanupCalls( 0 ), capturedBytes( 0 ), finalGeneration( 91 ) {
		temporaryPath[ 0 ] = '\0';
		finalPath[ 0 ] = '\0';
	}

	int WriteTemp( const char *qpath, const void *data, int bytes ) override {
		++writeCalls;
		CopyPath( temporaryPath, qpath );
		if ( mode == WRITE_FAIL ) return -1;
		capturedBytes = bytes;
		memcpy( captured, data, static_cast<size_t>( bytes ) );
		return mode == WRITE_PARTIAL ? bytes - 1 : bytes;
	}

	bool Promote( const char *temporary, const char *final ) override {
		++promoteCalls;
		CHECK_PATH( temporaryPath, temporary );
		CopyPath( finalPath, final );
		if ( mode == PROMOTE_FAIL ) return false;
		++finalGeneration;
		return true;
	}

	bool RemoveTemp( const char *temporary ) override {
		++cleanupCalls;
		CHECK_PATH( temporaryPath, temporary );
		return cleanupSucceeds;
	}

	Mode mode;
	bool cleanupSucceeds;
	int writeCalls;
	int promoteCalls;
	int cleanupCalls;
	int capturedBytes;
	int finalGeneration;
	char temporaryPath[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];
	char finalPath[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];

private:
	static void CopyPath( char *destination, const char *source ) {
		int index = 0;
		while ( index < MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES && source[ index ] != '\0' ) {
			destination[ index ] = source[ index ];
			++index;
		}
		destination[ index ] = '\0';
	}
	static void CHECK_PATH( const char *expected, const char *actual ) {
		if ( strcmp( expected, actual ) != 0 ) {
			// Make the enclosing operation observably fail without exceptions.
			const_cast<char *>( expected )[ 0 ] = '\0';
		}
	}
};

static int JournalUnchanged( const mpMatchEvidence &journal, uint64_t revision,
	const char *json, int bytes ) {
	if ( journal.GetEvidenceRevision() != revision ) return 0;
	mpEvidenceSerializeResult result = journal.SerializeCanonicalJson(
		workspace.json, static_cast<int>( sizeof( workspace.json ) ) );
	return result.Succeeded() && result.bytesWritten == bytes &&
		memcmp( workspace.json, json, static_cast<size_t>( bytes ) ) == 0;
}

int main() {
	mpMatchEvidence uninitialized;
	mpEvidenceStoragePaths emptyPaths;
	mpEvidenceStorageReason_t pathReason = MP_EVIDENCE_STORAGE_REASON_NONE;
	memset( &emptyPaths, 'X', sizeof( emptyPaths ) );
	CHECK( !MPMatchEvidenceStorageBuildPaths( uninitialized, emptyPaths, &pathReason ) );
	CHECK( pathReason == MP_EVIDENCE_STORAGE_REASON_NOT_INITIALIZED );
	CHECK( static_cast<unsigned char>( emptyPaths.finalQPath[ 0 ] ) == 'X' );

	mpMatchEvidence journal;
	mpEvidenceMetadataInput metadata;
	metadata.sessionId = 18446744073709551615ULL;
	metadata.seriesId = 9223372036854775808ULL;
	metadata.rulesDigest = 0x1234ULL;
	metadata.modeId = 4;
	metadata.build = "openQ4";
	metadata.map = "../../Maps\\MP:CON?.PK4 / \xc3\x9c Arena";
	metadata.mode = "duel";
	CHECK( journal.Reset( metadata ) );

	mpEvidenceStoragePaths firstPaths;
	mpEvidenceStoragePaths secondPaths;
	CHECK( MPMatchEvidenceStorageBuildPaths( journal, firstPaths, &pathReason ) );
	CHECK( pathReason == MP_EVIDENCE_STORAGE_REASON_NONE );
	CHECK( MPMatchEvidenceStorageBuildPaths( journal, secondPaths, 0 ) );
	CHECK( memcmp( &firstPaths, &secondPaths, sizeof( firstPaths ) ) == 0 );
	CHECK( strcmp( firstPaths.mapToken, "arena" ) == 0 );
	CHECK( strcmp( firstPaths.finalQPath,
		"match-results/session-18446744073709551615_series-9223372036854775808_"
		"arena.json" ) == 0 );
	CHECK( strcmp( firstPaths.temporaryQPath,
		"match-results/session-18446744073709551615_series-9223372036854775808_"
		"arena.json.pending-1" ) == 0 );
	CHECK( strstr( firstPaths.finalQPath, ".." ) == 0 );
	CHECK( strchr( firstPaths.finalQPath, '\\' ) == 0 );
	CHECK( strchr( firstPaths.finalQPath, ':' ) == 0 );
	CHECK( MPMatchEvidenceStorageIsFinalQPath( firstPaths.finalQPath ) );
	CHECK( MPMatchEvidenceStorageIsTemporaryQPath( firstPaths.temporaryQPath ) );
	CHECK( MPMatchEvidenceStorageIsPromotionPair( firstPaths.temporaryQPath,
		firstPaths.finalQPath ) );

	static const char *invalidFinalPaths[] = {
		"", "/match-results/session-1_series-0_map.json",
		"match-results/../session-1_series-0_map.json",
		"match-results\\session-1_series-0_map.json",
		"match-results/session-0_series-0_map.json",
		"match-results/session-01_series-0_map.json",
		"match-results/session-18446744073709551616_series-0_map.json",
		"match-results/session-1_series-00_map.json",
		"match-results/session-1_series-18446744073709551616_map.json",
		"match-results/session-1_series-0_Map.json",
		"match-results/session-1_series-0_-map.json",
		"match-results/session-1_series-0_map-.json",
		"match-results/session-1_series-0_map--one.json",
		"match-results/session-1_series-0_map.json.pending-1",
		"match-results/session-1_series-0_map.json/extra",
		"match-results/session-1_series-0_map.json.extra"
	};
	CHECK( !MPMatchEvidenceStorageIsFinalQPath( 0 ) );
	for ( unsigned int index = 0;
			index < sizeof( invalidFinalPaths ) / sizeof( invalidFinalPaths[ 0 ] );
			++index ) {
		CHECK( !MPMatchEvidenceStorageIsFinalQPath( invalidFinalPaths[ index ] ) );
	}
	static const char *invalidTemporaryPaths[] = {
		"match-results/session-1_series-0_map.json",
		"match-results/session-1_series-0_map.json.pending-0",
		"match-results/session-1_series-0_map.json.pending-01",
		"match-results/session-1_series-0_map.json.pending-18446744073709551616",
		"match-results/session-1_series-0_map.json.pending-1/escape",
		"../match-results/session-1_series-0_map.json.pending-1"
	};
	CHECK( !MPMatchEvidenceStorageIsTemporaryQPath( 0 ) );
	for ( unsigned int index = 0;
			index < sizeof( invalidTemporaryPaths ) / sizeof( invalidTemporaryPaths[ 0 ] );
			++index ) {
		CHECK( !MPMatchEvidenceStorageIsTemporaryQPath( invalidTemporaryPaths[ index ] ) );
	}
	CHECK( !MPMatchEvidenceStorageIsPromotionPair(
		"match-results/session-2_series-0_map.json.pending-1",
		"match-results/session-1_series-0_map.json" ) );
	CHECK( !MPMatchEvidenceStorageIsPromotionPair(
		"match-results/session-1_series-0_a.json.pending-1",
		"match-results/session-18446744073709551615_series-9223372036854775808_"
		"a-very-long-map-token.json" ) );
	char unterminated[ MP_MATCH_EVIDENCE_STORAGE_QPATH_BYTES + 1 ];
	memset( unterminated, 'a', sizeof( unterminated ) );
	CHECK( !MPMatchEvidenceStorageIsFinalQPath( unterminated ) );

	mpEvidenceSerializeResult baselineResult = journal.SerializeCanonicalJson(
		baseline, static_cast<int>( sizeof( baseline ) ) );
	CHECK( baselineResult.Succeeded() );
	const uint64_t journalRevision = journal.GetEvidenceRevision();

	FakeWriter writeFailure( FakeWriter::WRITE_FAIL );
	mpEvidenceStorageResult failedWrite = MPMatchEvidenceStoragePersist(
		journal, writeFailure, workspace );
	CHECK( failedWrite.code == MP_EVIDENCE_STORAGE_FAILED );
	CHECK( failedWrite.reason == MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_FAILED );
	CHECK( failedWrite.backendBytes == -1 );
	CHECK( writeFailure.writeCalls == 1 && writeFailure.promoteCalls == 0 );
	CHECK( writeFailure.cleanupCalls == 1 );
	CHECK( JournalUnchanged( journal, journalRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter partial( FakeWriter::WRITE_PARTIAL );
	mpEvidenceStorageResult partialWrite = MPMatchEvidenceStoragePersist(
		journal, partial, workspace );
	CHECK( partialWrite.reason == MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_PARTIAL );
	CHECK( partialWrite.backendBytes == partialWrite.serializedBytes - 1 );
	CHECK( partial.promoteCalls == 0 && partial.cleanupCalls == 1 );
	CHECK( JournalUnchanged( journal, journalRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter promotionFailure( FakeWriter::PROMOTE_FAIL );
	const int preservedFinalGeneration = promotionFailure.finalGeneration;
	mpEvidenceStorageResult failedPromotion = MPMatchEvidenceStoragePersist(
		journal, promotionFailure, workspace );
	CHECK( failedPromotion.reason == MP_EVIDENCE_STORAGE_REASON_PROMOTION_FAILED );
	CHECK( promotionFailure.writeCalls == 1 && promotionFailure.promoteCalls == 1 );
	CHECK( promotionFailure.cleanupCalls == 1 );
	CHECK( promotionFailure.finalGeneration == preservedFinalGeneration );
	CHECK( JournalUnchanged( journal, journalRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter cleanupFailure( FakeWriter::WRITE_PARTIAL, false );
	mpEvidenceStorageResult failedCleanup = MPMatchEvidenceStoragePersist(
		journal, cleanupFailure, workspace );
	CHECK( failedCleanup.reason == MP_EVIDENCE_STORAGE_REASON_TEMP_WRITE_PARTIAL );
	CHECK( failedCleanup.cleanupReason == MP_EVIDENCE_STORAGE_REASON_TEMP_CLEANUP_FAILED );
	CHECK( !failedCleanup.Succeeded() );

	FakeWriter success( FakeWriter::SUCCESS );
	mpEvidenceStorageResult stored = MPMatchEvidenceStoragePersist(
		journal, success, workspace );
	CHECK( stored.Succeeded() );
	CHECK( stored.serializedBytes == baselineResult.bytesWritten );
	CHECK( stored.backendBytes == stored.serializedBytes );
	CHECK( success.writeCalls == 1 && success.promoteCalls == 1 && success.cleanupCalls == 0 );
	CHECK( strcmp( success.temporaryPath, firstPaths.temporaryQPath ) == 0 );
	CHECK( strcmp( success.finalPath, firstPaths.finalQPath ) == 0 );
	CHECK( memcmp( captured, baseline, static_cast<size_t>( baselineResult.bytesWritten ) ) == 0 );
	CHECK( JournalUnchanged( journal, journalRevision, baseline,
		baselineResult.bytesWritten ) );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_evidence_storage_contract: executable checks skipped (no C++ compiler)")
        return
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-evidence-storage-", dir=ROOT / ".tmp") as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_evidence_storage_contract.cpp"
        executable = temp_dir / "match_evidence_storage_contract.exe"
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_EVIDENCE_STANDALONE_TEST",
            "-DMP_MATCH_EVIDENCE_STORAGE_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(EVIDENCE_SOURCE),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone evidence-storage contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"evidence-storage executable invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    adapter_header = read(FILESYSTEM_HEADER)
    adapter_source = read(FILESYSTEM_SOURCE)
    static_contracts(header, source, adapter_header, adapter_source)
    executable_contract()
    print("mp_match_evidence_storage_contract: PASS")


if __name__ == "__main__":
    main()
