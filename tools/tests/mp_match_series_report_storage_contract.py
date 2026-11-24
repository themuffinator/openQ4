#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and hostile native contracts for atomic series-report persistence."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
HEADER = MATCH_DIR / "MatchSeriesReportStorage.h"
SOURCE = MATCH_DIR / "MatchSeriesReportStorage.cpp"
REPORT_HEADER = MATCH_DIR / "MatchSeriesReport.h"
REPORT_SOURCE = MATCH_DIR / "MatchSeriesReport.cpp"


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


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_SERIES_REPORT_STORAGE_JSON_BYTES =\n\tMP_SERIES_REPORT_MAX_JSON_BYTES",
        "MP_SERIES_REPORT_STORAGE_QPATH_BYTES = 128",
        "mpSeriesReportStorageWorkspace",
        "mpSeriesReportStorageResult",
        "class mpMatchSeriesReportStorageWriter",
        "virtual int WriteTemp",
        "virtual bool Promote",
        "virtual bool RemoveTemp",
        "MPMatchSeriesReportStorageBuildPaths",
        "MPMatchSeriesReportStorageIsFinalQPath",
        "MPMatchSeriesReportStorageIsTemporaryQPath",
        "MPMatchSeriesReportStorageIsPromotionPair",
        "MPMatchSeriesReportStoragePersist",
    ):
        require(combined, token, "bounded series-report persistence API")

    for token in (
        "MP_SERIES_REPORT_STORAGE_REASON_NOT_INITIALIZED",
        "MP_SERIES_REPORT_STORAGE_REASON_NOT_FINALIZED",
        "MP_SERIES_REPORT_STORAGE_REASON_INVALID_REPORT",
        "MP_SERIES_REPORT_STORAGE_REASON_JSON_TOO_LARGE",
        "MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_FAILED",
        "MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_PARTIAL",
        "MP_SERIES_REPORT_STORAGE_REASON_PROMOTION_FAILED",
        "MP_SERIES_REPORT_STORAGE_REASON_TEMP_CLEANUP_FAILED",
    ):
        require(header, token, "explicit persistence outcomes")

    if header.count("const mpCompetitionSeriesReport &report") != 2:
        raise AssertionError("path construction and persistence need const report inputs")
    for forbidden in (
        "idFileSystem",
        "fileSystem",
        "idFile",
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
        "MultiplayerGame.h",
        "MatchSeriesRecovery.h",
    ):
        reject(combined, forbidden, "filesystem-neutral storage seam")
    for pattern in (
        r"\bnew\b",
        r"\bdelete\b",
        r"\bmalloc\s*\(",
        r"\bcalloc\s*\(",
        r"\brealloc\s*\(",
        r"\bfree\s*\(",
    ):
        if re.search(pattern, combined):
            raise AssertionError(f"storage seam allocates via {pattern!r}")

    require(
        source,
        'MP_SERIES_REPORT_STORAGE_PATH_PREFIX[] =\n\t"match-results/series-"',
        "single server-owned report root",
    )
    if source.count("match-results/") != 1:
        raise AssertionError("series-report storage must own exactly one fixed qpath root")
    for token in (
        "finalPath.PutUnsigned64( seriesId )",
        "temporaryPath.PutUnsigned64( reportRevision )",
        'ReadLiteral( path, length, cursor, ".json" )',
        'ReadLiteral( path, length, cursor, ".pending-" )',
        'maximumUnsigned64[] = "18446744073709551615"',
        "path[ start ] == '0'",
    ):
        require(source, token, "strict canonical qpath codec")
    if re.search(
        r"MPMatchSeriesReportStorage(?:BuildPaths|Persist)\([^)]*char\s*\*",
        header,
        re.DOTALL,
    ):
        raise AssertionError("public persistence entry points accept a caller path")

    persist = source[source.index(
        "mpSeriesReportStorageResult MPMatchSeriesReportStoragePersist"
    ) :]
    require_before(
        persist,
        "SerializeCanonicalJson",
        "writer.WriteTemp",
        "complete serialization before backend mutation",
    )
    require_before(
        persist,
        "writer.WriteTemp",
        "writer.Promote",
        "temporary write before promotion",
    )
    require(persist, "result.backendBytes != result.serializedBytes", "short writes")
    if persist.count("RecordCleanup( writer, result )") != 2:
        raise AssertionError("write and promotion failures must both attempt cleanup")
    require(
        header,
        "Promote returning false must\n// leave the previous final artifact unchanged.",
        "atomic promotion backend contract",
    )
    require(
        source,
        "MP_MATCH_SERIES_REPORT_STORAGE_STANDALONE_TEST",
        "standalone compile seam",
    )

    # No arbitrary backend diagnostics, external identity, or caller-owned
    # destination can enter the immutable result value.
    for field in (
        "ipAddress",
        "remoteAddress",
        "credential",
        "password",
        "secret",
        "userinfo",
        "backendText",
        "callerPath",
        "outputPath",
    ):
        if re.search(rf"\b{re.escape(field)}\b", combined, re.IGNORECASE):
            raise AssertionError(f"sensitive storage field leaked: {field}")

    listing = subprocess.run(
        [
            shutil.which("python") or shutil.which("python3") or "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    discovered = {line.strip() for line in listing}
    for source_name in (
        "game/mp/match/MatchSeriesReport.cpp",
        "game/mp/match/MatchSeriesReportStorage.cpp",
    ):
        if source_name not in discovered:
            raise AssertionError(f"recursive MP source discovery omits {source_name}")


HARNESS = r'''
#include "game/mp/match/MatchSeriesReportStorage.h"

#include <string.h>

#define CHECK( value ) do { if ( !( value ) ) return __LINE__; } while ( 0 )

static mpSeriesReportStorageWorkspace workspace;
static char baseline[ MP_SERIES_REPORT_STORAGE_JSON_BYTES ];
static char captured[ MP_SERIES_REPORT_STORAGE_JSON_BYTES ];

static mpSeriesReportIdentityInput Identity( uint64_t seriesId ) {
	mpSeriesReportIdentityInput identity = {};
	identity.seriesId = seriesId;
	identity.profile = MP_SERIES_PROFILE_BEST_OF_ONE;
	identity.profileKey = "best_of_one";
	identity.bestOf = 1;
	identity.rulesSchema = 2;
	identity.rulesRevision = 7;
	identity.rulesDigest = 0x0123456789abcdefULL;
	identity.gameType = 1;
	identity.modeToken = "duel";
	identity.contestants[ 0 ].kind = MP_SERIES_REPORT_CONTESTANT_PARTICIPANT;
	identity.contestants[ 0 ].participantSequence = 101;
	identity.contestants[ 0 ].label = "Alpha";
	identity.contestants[ 1 ].kind = MP_SERIES_REPORT_CONTESTANT_PARTICIPANT;
	identity.contestants[ 1 ].participantSequence = 202;
	identity.contestants[ 1 ].label = "Bravo";
	return identity;
}

static int BuildFinalReport( mpCompetitionSeriesReport &report, uint64_t seriesId ) {
	mpSeriesReportIdentityInput identity = Identity( seriesId );
	if ( !report.Initialize( identity ).WasAccepted() ) return 0;
	mpSeriesReportMapResultInput map = {};
	map.attempt = 1;
	map.sessionId = 9001;
	map.mapToken = "maps/mp/q4dm1";
	map.rulesDigest = identity.rulesDigest;
	map.outcome = MP_SERIES_REPORT_MAP_DECIDED;
	map.reason = 41;
	map.winnerContestant = 0;
	map.score[ 0 ] = 15;
	map.score[ 1 ] = 9;
	if ( !report.AppendMapResult( map ).WasAccepted() ) return 0;
	mpSeriesReportFinalInput finalResult = {};
	finalResult.outcome = MP_SERIES_REPORT_FINAL_COMPLETE;
	finalResult.reason = 91;
	finalResult.winnerContestant = 0;
	finalResult.authorizer = MPSeriesReportSystemAuthorizer();
	return report.Finalize( finalResult ).WasAccepted() ? 1 : 0;
}

class FakeWriter : public mpMatchSeriesReportStorageWriter {
public:
	enum Mode { SUCCESS, WRITE_FAIL, WRITE_PARTIAL, PROMOTE_FAIL };

	FakeWriter( Mode selectedMode, bool cleanupWorks = true ) :
		mode( selectedMode ), cleanupSucceeds( cleanupWorks ), writeCalls( 0 ),
		promoteCalls( 0 ), cleanupCalls( 0 ), capturedBytes( 0 ),
		finalGeneration( 73 ), pathsMatched( true ) {
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
		pathsMatched = pathsMatched && strcmp( temporaryPath, temporary ) == 0;
		CopyPath( finalPath, final );
		if ( mode == PROMOTE_FAIL ) return false;
		++finalGeneration;
		return true;
	}

	bool RemoveTemp( const char *temporary ) override {
		++cleanupCalls;
		pathsMatched = pathsMatched && strcmp( temporaryPath, temporary ) == 0;
		return cleanupSucceeds;
	}

	Mode mode;
	bool cleanupSucceeds;
	int writeCalls;
	int promoteCalls;
	int cleanupCalls;
	int capturedBytes;
	int finalGeneration;
	bool pathsMatched;
	char temporaryPath[ MP_SERIES_REPORT_STORAGE_QPATH_BYTES + 1 ];
	char finalPath[ MP_SERIES_REPORT_STORAGE_QPATH_BYTES + 1 ];

private:
	static void CopyPath( char *destination, const char *source ) {
		int index = 0;
		while ( index < MP_SERIES_REPORT_STORAGE_QPATH_BYTES &&
			source[ index ] != '\0' ) {
			destination[ index ] = source[ index ];
			++index;
		}
		destination[ index ] = '\0';
	}
};

static int ReportUnchanged( const mpCompetitionSeriesReport &report,
	uint64_t revision, const char *json, int bytes ) {
	if ( report.GetReportRevision() != revision || !report.ValidateInvariants() ) {
		return 0;
	}
	mpSeriesReportSerializeResult serialized = report.SerializeCanonicalJson(
		workspace.json, static_cast<int>( sizeof( workspace.json ) ) );
	return serialized.Succeeded() && serialized.bytesWritten == bytes &&
		memcmp( workspace.json, json, static_cast<size_t>( bytes ) ) == 0;
}

int main() {
	static_assert( sizeof( mpSeriesReportStorageWorkspace ) ==
		MP_SERIES_REPORT_MAX_JSON_BYTES,
		"storage workspace must exactly cover the bounded serializer" );

	mpCompetitionSeriesReport uninitialized;
	mpSeriesReportStoragePaths untouched;
	memset( &untouched, 0x58, sizeof( untouched ) );
	mpSeriesReportStorageReason_t pathReason = MP_SERIES_REPORT_STORAGE_REASON_NONE;
	CHECK( !MPMatchSeriesReportStorageBuildPaths(
		uninitialized, untouched, &pathReason ) );
	CHECK( pathReason == MP_SERIES_REPORT_STORAGE_REASON_NOT_INITIALIZED );
	CHECK( static_cast<unsigned char>( untouched.finalQPath[ 0 ] ) == 0x58 );
	FakeWriter neverCalled( FakeWriter::SUCCESS );
	mpSeriesReportStorageResult rejected = MPMatchSeriesReportStoragePersist(
		uninitialized, neverCalled, workspace );
	CHECK( rejected.code == MP_SERIES_REPORT_STORAGE_REJECTED );
	CHECK( rejected.reason == MP_SERIES_REPORT_STORAGE_REASON_NOT_INITIALIZED );
	CHECK( neverCalled.writeCalls == 0 && neverCalled.promoteCalls == 0 &&
		neverCalled.cleanupCalls == 0 );

	mpCompetitionSeriesReport open;
	mpSeriesReportIdentityInput openIdentity = Identity( 7 );
	CHECK( open.Initialize( openIdentity ).WasAccepted() );
	memset( &untouched, 0x59, sizeof( untouched ) );
	CHECK( !MPMatchSeriesReportStorageBuildPaths( open, untouched, &pathReason ) );
	CHECK( pathReason == MP_SERIES_REPORT_STORAGE_REASON_NOT_FINALIZED );
	CHECK( static_cast<unsigned char>( untouched.temporaryQPath[ 0 ] ) == 0x59 );

	mpCompetitionSeriesReport report;
	CHECK( BuildFinalReport( report, 18446744073709551615ULL ) );
	CHECK( report.GetReportRevision() == 3 );
	mpSeriesReportStoragePaths firstPaths;
	mpSeriesReportStoragePaths secondPaths;
	CHECK( MPMatchSeriesReportStorageBuildPaths( report, firstPaths, &pathReason ) );
	CHECK( pathReason == MP_SERIES_REPORT_STORAGE_REASON_NONE );
	CHECK( MPMatchSeriesReportStorageBuildPaths( report, secondPaths, 0 ) );
	CHECK( memcmp( &firstPaths, &secondPaths, sizeof( firstPaths ) ) == 0 );
	CHECK( strcmp( firstPaths.finalQPath,
		"match-results/series-18446744073709551615.json" ) == 0 );
	CHECK( strcmp( firstPaths.temporaryQPath,
		"match-results/series-18446744073709551615.json.pending-3" ) == 0 );
	CHECK( MPMatchSeriesReportStorageIsFinalQPath( firstPaths.finalQPath ) );
	CHECK( MPMatchSeriesReportStorageIsTemporaryQPath(
		firstPaths.temporaryQPath ) );
	CHECK( MPMatchSeriesReportStorageIsPromotionPair(
		firstPaths.temporaryQPath, firstPaths.finalQPath ) );

	static const char *invalidFinalPaths[] = {
		"", "/match-results/series-1.json", "match-results/../series-1.json",
		"match-results\\series-1.json", "match-results/series-0.json",
		"match-results/series-01.json", "match-results/series-+1.json",
		"match-results/series-18446744073709551616.json",
		"match-results/Series-1.json", "match-results/series-1.JSON",
		"match-results/series-1.json.pending-2",
		"match-results/series-1.json/extra", "match-results/series-1.json.extra",
		"match-results/session-1_series-1_map.json"
	};
	CHECK( !MPMatchSeriesReportStorageIsFinalQPath( 0 ) );
	for ( unsigned int index = 0;
		index < sizeof( invalidFinalPaths ) / sizeof( invalidFinalPaths[ 0 ] );
		++index ) {
		CHECK( !MPMatchSeriesReportStorageIsFinalQPath( invalidFinalPaths[ index ] ) );
	}
	CHECK( MPMatchSeriesReportStorageIsFinalQPath(
		"match-results/series-1.json" ) );

	static const char *invalidTemporaryPaths[] = {
		"match-results/series-1.json",
		"match-results/series-1.json.pending-0",
		"match-results/series-1.json.pending-01",
		"match-results/series-1.json.pending-+1",
		"match-results/series-1.json.pending-18446744073709551616",
		"match-results/series-1.json.pending-1/escape",
		"../match-results/series-1.json.pending-1"
	};
	CHECK( !MPMatchSeriesReportStorageIsTemporaryQPath( 0 ) );
	for ( unsigned int index = 0; index < sizeof( invalidTemporaryPaths ) /
			sizeof( invalidTemporaryPaths[ 0 ] ); ++index ) {
		CHECK( !MPMatchSeriesReportStorageIsTemporaryQPath(
			invalidTemporaryPaths[ index ] ) );
	}
	CHECK( MPMatchSeriesReportStorageIsTemporaryQPath(
		"match-results/series-1.json.pending-1" ) );
	CHECK( !MPMatchSeriesReportStorageIsPromotionPair(
		"match-results/series-2.json.pending-3",
		"match-results/series-1.json" ) );
	CHECK( !MPMatchSeriesReportStorageIsPromotionPair(
		"match-results/series-1.json.pending-3",
		"match-results/series-2.json" ) );
	char unterminated[ MP_SERIES_REPORT_STORAGE_QPATH_BYTES + 1 ];
	memset( unterminated, 'a', sizeof( unterminated ) );
	CHECK( !MPMatchSeriesReportStorageIsFinalQPath( unterminated ) );
	CHECK( !MPMatchSeriesReportStorageIsTemporaryQPath( unterminated ) );

	mpSeriesReportSerializeResult baselineResult = report.SerializeCanonicalJson(
		baseline, static_cast<int>( sizeof( baseline ) ) );
	CHECK( baselineResult.Succeeded() );
	const uint64_t reportRevision = report.GetReportRevision();

	FakeWriter writeFailure( FakeWriter::WRITE_FAIL );
	mpSeriesReportStorageResult failedWrite = MPMatchSeriesReportStoragePersist(
		report, writeFailure, workspace );
	CHECK( failedWrite.code == MP_SERIES_REPORT_STORAGE_FAILED );
	CHECK( failedWrite.reason == MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_FAILED );
	CHECK( failedWrite.cleanupReason == MP_SERIES_REPORT_STORAGE_REASON_NONE );
	CHECK( failedWrite.backendBytes == -1 );
	CHECK( failedWrite.seriesId == report.GetIdentity().seriesId );
	CHECK( failedWrite.reportRevision == reportRevision );
	CHECK( writeFailure.writeCalls == 1 && writeFailure.promoteCalls == 0 &&
		writeFailure.cleanupCalls == 1 && writeFailure.pathsMatched );
	CHECK( ReportUnchanged( report, reportRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter partial( FakeWriter::WRITE_PARTIAL );
	mpSeriesReportStorageResult partialWrite = MPMatchSeriesReportStoragePersist(
		report, partial, workspace );
	CHECK( partialWrite.reason ==
		MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_PARTIAL );
	CHECK( partialWrite.backendBytes == partialWrite.serializedBytes - 1 );
	CHECK( partial.promoteCalls == 0 && partial.cleanupCalls == 1 &&
		partial.pathsMatched );
	CHECK( ReportUnchanged( report, reportRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter promotionFailure( FakeWriter::PROMOTE_FAIL );
	const int preservedFinalGeneration = promotionFailure.finalGeneration;
	mpSeriesReportStorageResult failedPromotion = MPMatchSeriesReportStoragePersist(
		report, promotionFailure, workspace );
	CHECK( failedPromotion.reason ==
		MP_SERIES_REPORT_STORAGE_REASON_PROMOTION_FAILED );
	CHECK( promotionFailure.writeCalls == 1 &&
		promotionFailure.promoteCalls == 1 && promotionFailure.cleanupCalls == 1 &&
		promotionFailure.pathsMatched );
	CHECK( promotionFailure.finalGeneration == preservedFinalGeneration );
	CHECK( ReportUnchanged( report, reportRevision, baseline,
		baselineResult.bytesWritten ) );

	FakeWriter cleanupFailure( FakeWriter::WRITE_PARTIAL, false );
	mpSeriesReportStorageResult failedCleanup = MPMatchSeriesReportStoragePersist(
		report, cleanupFailure, workspace );
	CHECK( failedCleanup.reason ==
		MP_SERIES_REPORT_STORAGE_REASON_TEMP_WRITE_PARTIAL );
	CHECK( failedCleanup.cleanupReason ==
		MP_SERIES_REPORT_STORAGE_REASON_TEMP_CLEANUP_FAILED );
	CHECK( !failedCleanup.Succeeded() );

	FakeWriter success( FakeWriter::SUCCESS );
	mpSeriesReportStorageResult stored = MPMatchSeriesReportStoragePersist(
		report, success, workspace );
	CHECK( stored.Succeeded() );
	CHECK( stored.serializedBytes == baselineResult.bytesWritten );
	CHECK( stored.backendBytes == stored.serializedBytes );
	CHECK( success.writeCalls == 1 && success.promoteCalls == 1 &&
		success.cleanupCalls == 0 && success.pathsMatched );
	CHECK( strcmp( success.temporaryPath, firstPaths.temporaryQPath ) == 0 );
	CHECK( strcmp( success.finalPath, firstPaths.finalQPath ) == 0 );
	CHECK( memcmp( captured, baseline,
		static_cast<size_t>( baselineResult.bytesWritten ) ) == 0 );
	CHECK( ReportUnchanged( report, reportRevision, baseline,
		baselineResult.bytesWritten ) );

	// Retrying a sealed report is byte- and path-identical; only a successful
	// backend promotion can advance the fake final generation.
	const int firstGeneration = success.finalGeneration;
	mpSeriesReportStorageResult retried = MPMatchSeriesReportStoragePersist(
		report, success, workspace );
	CHECK( retried.Succeeded() );
	CHECK( success.finalGeneration == firstGeneration + 1 );
	CHECK( strcmp( retried.paths.finalQPath, stored.paths.finalQPath ) == 0 );
	CHECK( strcmp( retried.paths.temporaryQPath,
		stored.paths.temporaryQPath ) == 0 );
	CHECK( retried.serializedBytes == stored.serializedBytes );
	CHECK( memcmp( captured, baseline,
		static_cast<size_t>( baselineResult.bytesWritten ) ) == 0 );
	CHECK( ReportUnchanged( report, reportRevision, baseline,
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
        print(
            "mp_match_series_report_storage_contract: native checks skipped "
            "(no C++ compiler)"
        )
        return

    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="match-series-report-storage-", dir=ROOT / ".tmp"
    ) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_series_report_storage_contract.cpp"
        executable = temp_dir / "match_series_report_storage_contract.exe"
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST",
            "-DMP_MATCH_SERIES_REPORT_STORAGE_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(REPORT_SOURCE),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone series-report storage contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "series-report storage invariant failed at harness line "
                f"{ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    read(REPORT_HEADER)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_series_report_storage_contract: PASS")


if __name__ == "__main__":
    main()
