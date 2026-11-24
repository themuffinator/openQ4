#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contracts for the fs_savepath-only series recovery adapter and loader."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
HEADER = MATCH_DIR / "MatchSeriesRecoveryFileSystem.h"
SOURCE = MATCH_DIR / "MatchSeriesRecoveryFileSystem.cpp"
RECOVERY_SOURCE = MATCH_DIR / "MatchSeriesRecovery.cpp"
SERIES_SOURCE = MATCH_DIR / "MatchSeries.cpp"
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


def static_contracts() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    combined = header + source

    for token in (
        "class mpMatchSeriesRecoveryFileSystemWriter",
        "class mpMatchSeriesRecoveryReadStream",
        "MPMatchSeriesRecoveryLoadStream",
        "MPMatchSeriesRecoveryLoadFileSystem",
        "mpSeriesRecoveryLoadResult",
        "MP_SERIES_RECOVERY_LOAD_REASON_OVERSIZED_RECORD",
        "MP_SERIES_RECOVERY_LOAD_REASON_READ_PARTIAL",
        "MP_SERIES_RECOVERY_LOAD_REASON_DECODE_REJECTED",
        "MP_SERIES_RECOVERY_LOAD_REASON_IDENTITY_MISMATCH",
        "MP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_STANDALONE_TEST",
    ):
        require(combined, token, "bounded filesystem adapter API")

    for token in (
        'MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT = "fs_savepath"',
        "MPMatchSeriesRecoveryIsTemporaryQPath( temporaryQPath )",
        "OpenFileWrite( temporaryQPath",
        "file->Write",
        "file->Sync()",
        "CloseFile( file )",
        "MPMatchSeriesRecoveryIsPromotionPair( temporaryQPath, finalQPath )",
        "PromoteFile( temporaryQPath, finalQPath",
        "RemoveFileChecked( temporaryQPath",
    ):
        require(source, token, "hardened atomic writer")
    require_before(
        source,
        "MPMatchSeriesRecoveryIsTemporaryQPath( temporaryQPath )",
        "OpenFileWrite( temporaryQPath",
        "temporary qpath validation before write",
    )
    require_before(
        source,
        "MPMatchSeriesRecoveryIsPromotionPair( temporaryQPath, finalQPath )",
        "PromoteFile( temporaryQPath, finalQPath",
        "promotion pair validation before mutation",
    )

    loader_at = source.index("MPMatchSeriesRecoveryLoadFileSystem")
    loader = source[loader_at:]
    for token in (
        "MPMatchSeriesRecoveryBuildFinalQPath( expectedSeriesId",
        "MPMatchSeriesRecoveryIsFinalQPath( result.finalQPath )",
        "RelativePathToOSPath( result.finalQPath",
        "MP_MATCH_SERIES_RECOVERY_WRITABLE_ROOT",
        "OpenExplicitFileRead( explicitPath )",
        "MPMatchSeriesRecoveryLoadStream( stream",
        "backend->CloseFile( file )",
    ):
        require(loader, token, "fs_savepath-only production loader")
    require_before(
        loader,
        "MPMatchSeriesRecoveryBuildFinalQPath",
        "RelativePathToOSPath",
        "canonical qpath before OS mapping",
    )
    require_before(
        loader,
        "RelativePathToOSPath",
        "OpenExplicitFileRead",
        "explicit path derived beneath fs_savepath before open",
    )
    require_before(
        loader,
        "MPMatchSeriesRecoveryLoadStream",
        "backend->CloseFile",
        "bounded read before close",
    )

    stream_at = source.index("MPMatchSeriesRecoveryLoadStream")
    stream_end = source.index(
        "mpMatchSeriesRecoveryFileSystemWriter::mpMatchSeriesRecoveryFileSystemWriter"
    )
    stream = source[stream_at:stream_end]
    for token in (
        "expectedSeriesId == 0",
        "stream.Length()",
        "result.expectedBytes > MP_SERIES_RECOVERY_MAX_BYTES",
        "stream.Read( workspace.bytes + result.readBytes, remaining )",
        "read <= 0 || read > remaining",
        "result.readBytes != result.expectedBytes",
        "MPMatchSeriesRecoveryDecode",
        "candidate.seriesId != expectedSeriesId",
        "output = candidate",
    ):
        require(stream, token, "bounded transactional stream loader")
    require_before(
        stream,
        "result.expectedBytes > MP_SERIES_RECOVERY_MAX_BYTES",
        "stream.Read(",
        "oversize rejection before read",
    )
    require_before(
        stream,
        "MPMatchSeriesRecoveryDecode",
        "candidate.seriesId != expectedSeriesId",
        "decode before expected identity check",
    )
    require_before(
        stream,
        "candidate.seriesId != expectedSeriesId",
        "output = candidate",
        "identity check before transactional commit",
    )

    for forbidden in (
        "OpenFileRead(",
        "OpenFileReadFromPak",
        "ReadFile(",
        "WriteFile(",
        "fs_cdpath",
        "fs_basepath",
        "CopyFile",
        "MoveFile",
        "rename(",
        "fopen(",
        "ofstream",
        "std::filesystem",
        "system(",
        "popen(",
    ):
        reject(combined, forbidden, "exact-root adapter")

    # The public load API accepts only an expected numeric identity; callers
    # cannot smuggle an arbitrary qpath or OS path into it.
    load_decl = header[header.index("MPMatchSeriesRecoveryLoadFileSystem"):]
    load_decl = load_decl[: load_decl.index(";")]
    for forbidden in ("char *", "char*", "qpath", "path"):
        reject(load_decl.lower(), forbidden.lower(), "numeric-only load API")

    listing = subprocess.run(
        [
            "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "game/mp/match/MatchSeriesRecoveryFileSystem.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("series recovery filesystem adapter absent from MP sources")


HARNESS = r'''
#include "game/mp/match/MatchSeriesRecoveryFileSystem.h"

#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

class FakeStream : public mpMatchSeriesRecoveryReadStream {
public:
    const unsigned char *source;
    int sourceBytes;
    int reportedLength;
    int maximumChunk;
    int position;
    int lengthCalls;
    int readCalls;
    bool returnOversizedChunk;

    FakeStream(const void *data, int available, int length) :
        source(static_cast<const unsigned char *>(data)), sourceBytes(available),
        reportedLength(length), maximumChunk(2147483647), position(0),
        lengthCalls(0), readCalls(0), returnOversizedChunk(false) {}

    int Length() override {
        ++lengthCalls;
        return reportedLength;
    }
    int Read(void *destination, int bytes) override {
        ++readCalls;
        if (returnOversizedChunk) return bytes + 1;
        if (source == 0 || position >= sourceBytes) return 0;
        int count = sourceBytes - position;
        if (count > bytes) count = bytes;
        if (count > maximumChunk) count = maximumChunk;
        memcpy(destination, source + position, count);
        position += count;
        return count;
    }
};

static bool BuildRecord(mpSeriesRecoveryRecord &record,
                        mpSeriesRecoveryWorkspace &workspace,
                        mpSeriesRecoveryCodecResult &encoded) {
    const char *maps[] = { "mp/q4dm1" };
    mpSeriesConfiguration configuration;
    mpSeriesReason_t reason = MP_SERIES_REASON_NONE;
    if (!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_ONE, 0, 99, 0,
            false, maps, 1, configuration, reason)) return false;
    mpCompetitionSeries series;
    if (!series.Configure(configuration, series.GetRevision()).WasApplied() ||
        !series.Start(series.GetRevision()).WasApplied() ||
        !series.ApplyVeto(configuration.vetoSteps[0].expectedSide,
            MP_SERIES_VETO_DECIDER, maps[0], MP_SERIES_SIDE_NONE,
            series.GetRevision()).WasApplied()) return false;
    record.Clear();
    if (!MPMatchSeriesRecoveryCapture(series, 42, 84, record, 0)) return false;
    encoded = MPMatchSeriesRecoveryEncode(record, workspace.bytes,
        sizeof(workspace.bytes));
    return encoded.Succeeded();
}

static void SetSentinel(mpSeriesRecoveryRecord &record) {
    record.Clear();
    record.seriesId = UINT64_C(9999);
    record.linkedSessionId = UINT64_C(8888);
}

int main() {
    static mpSeriesRecoveryWorkspace encodedWorkspace;
    static mpSeriesRecoveryRecord sourceRecord;
    mpSeriesRecoveryCodecResult encoded;
    CHECK(BuildRecord(sourceRecord, encodedWorkspace, encoded));

    static mpSeriesRecoveryWorkspace loadWorkspace;
    static mpSeriesRecoveryRecord output;
    SetSentinel(output);
    FakeStream success(encodedWorkspace.bytes, encoded.bytes, encoded.bytes);
    success.maximumChunk = 7;
    mpSeriesRecoveryLoadResult loaded = MPMatchSeriesRecoveryLoadStream(
        success, 42, loadWorkspace, output);
    CHECK(loaded.Succeeded() && output.seriesId == 42 &&
        output.linkedSessionId == 84 && loaded.expectedBytes == encoded.bytes &&
        loaded.readBytes == encoded.bytes && success.readCalls > 1);

    SetSentinel(output);
    static mpSeriesRecoveryRecord before;
    before = output;
    FakeStream invalidIdentity(encodedWorkspace.bytes, encoded.bytes, encoded.bytes);
    loaded = MPMatchSeriesRecoveryLoadStream(invalidIdentity, 0, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_INVALID_IDENTITY &&
        invalidIdentity.lengthCalls == 0 && invalidIdentity.readCalls == 0 &&
        memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream wrongIdentity(encodedWorkspace.bytes, encoded.bytes, encoded.bytes);
    loaded = MPMatchSeriesRecoveryLoadStream(wrongIdentity, 43, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_IDENTITY_MISMATCH &&
        wrongIdentity.readCalls > 0 &&
        memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream oversized(encodedWorkspace.bytes, encoded.bytes,
        MP_SERIES_RECOVERY_MAX_BYTES + 1);
    loaded = MPMatchSeriesRecoveryLoadStream(oversized, 42, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_OVERSIZED_RECORD &&
        oversized.readCalls == 0 && memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream empty(encodedWorkspace.bytes, encoded.bytes, 0);
    loaded = MPMatchSeriesRecoveryLoadStream(empty, 42, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_INVALID_LENGTH &&
        empty.readCalls == 0 && memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream shortRead(encodedWorkspace.bytes, encoded.bytes - 1, encoded.bytes);
    shortRead.maximumChunk = 11;
    loaded = MPMatchSeriesRecoveryLoadStream(shortRead, 42, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_READ_PARTIAL &&
        loaded.readBytes == encoded.bytes - 1 &&
        memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream failedRead(0, 0, encoded.bytes);
    loaded = MPMatchSeriesRecoveryLoadStream(failedRead, 42, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_READ_FAILED &&
        loaded.readBytes == 0 && memcmp(&output, &before, sizeof(output)) == 0);

    FakeStream impossibleRead(encodedWorkspace.bytes, encoded.bytes, encoded.bytes);
    impossibleRead.returnOversizedChunk = true;
    loaded = MPMatchSeriesRecoveryLoadStream(impossibleRead, 42, loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_READ_FAILED &&
        memcmp(&output, &before, sizeof(output)) == 0);

    static unsigned char corrupt[MP_SERIES_RECOVERY_MAX_BYTES];
    memcpy(corrupt, encodedWorkspace.bytes, encoded.bytes);
    corrupt[50] ^= 1;
    FakeStream corruptStream(corrupt, encoded.bytes, encoded.bytes);
    loaded = MPMatchSeriesRecoveryLoadStream(corruptStream, 42,
        loadWorkspace, output);
    CHECK(!loaded.Succeeded() &&
        loaded.reason == MP_SERIES_RECOVERY_LOAD_REASON_DECODE_REJECTED &&
        loaded.decodeReason == MP_SERIES_RECOVERY_REASON_CHECKSUM_MISMATCH &&
        memcmp(&output, &before, sizeof(output)) == 0);
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
            "mp_match_series_recovery_filesystem_contract: "
            "executable checks skipped (no C++ compiler)"
        )
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="series-recovery-fs-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "series_recovery_filesystem_contract.cpp"
        executable = temp_dir / (
            "series_recovery_filesystem_contract.exe"
            if compiler.lower().endswith(".exe")
            else "series_recovery_filesystem_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-DMP_MATCH_SERIES_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_RECOVERY_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_RECOVERY_FILE_SYSTEM_STANDALONE_TEST",
                f"-I{ROOT / 'src'}",
                str(harness),
                str(SERIES_SOURCE),
                str(REPORT_SOURCE),
                str(RECOVERY_SOURCE),
                str(SOURCE),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone recovery-filesystem contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"recovery-filesystem invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    static_contracts()
    executable_contract()
    print("mp_match_series_recovery_filesystem_contract: PASS")


if __name__ == "__main__":
    main()
