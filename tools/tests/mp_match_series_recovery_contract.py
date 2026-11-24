#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Hostile static and executable contract for competition-series recovery."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
HEADER = MATCH_DIR / "MatchSeriesRecovery.h"
SOURCE = MATCH_DIR / "MatchSeriesRecovery.cpp"
SERIES_HEADER = MATCH_DIR / "MatchSeries.h"
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


def static_contracts() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    series_header = read(SERIES_HEADER)
    series_source = read(SERIES_SOURCE)
    combined = header + source

    for token in (
        "MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION = 2",
        "MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION = 3",
        "MP_SERIES_RECOVERY_SCHEMA_VERSION = 4",
        "MP_SERIES_RECOVERY_MAX_BYTES = 65536",
        "mpSeriesRecoveryState",
        "mpSeriesRecoveryRecord",
        "seriesId",
        "linkedSessionId",
        "contentDigest",
        "mpSeriesRecoveryWorkspace",
        "class mpMatchSeriesRecoveryWriter",
        "virtual int WriteTemp",
        "virtual bool Promote",
        "virtual bool RemoveTemp",
        "MPMatchSeriesRecoveryCapture",
        "MPMatchSeriesRecoveryValidate",
        "MPMatchSeriesRecoveryComputeContentDigest",
        "MPMatchSeriesRecoveryEncode",
        "MPMatchSeriesRecoveryDecode",
        "MPMatchSeriesRecoveryPersist",
        "MPMatchSeriesRecoveryBuildPaths",
        "MPMatchSeriesRecoveryBuildFinalQPath",
        "MPMatchSeriesRecoveryIsPromotionPair",
    ):
        require(combined, token, "bounded recovery API")

    for token in (
        "MP_SERIES_RECOVERY_REASON_UNSUPPORTED_SCHEMA",
        "MP_SERIES_RECOVERY_REASON_TRUNCATED_RECORD",
        "MP_SERIES_RECOVERY_REASON_TRAILING_DATA",
        "MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD",
        "MP_SERIES_RECOVERY_REASON_CHECKSUM_MISMATCH",
        "MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH",
        "MP_SERIES_RECOVERY_REASON_TEMP_WRITE_PARTIAL",
        "MP_SERIES_RECOVERY_REASON_PROMOTION_FAILED",
        "MP_SERIES_RECOVERY_REASON_TEMP_CLEANUP_FAILED",
    ):
        require(header, token, "stable recovery failures")

    for token in (
        "mpSeriesAppliedVeto",
        "GetAppliedVetoCount",
        "GetAppliedVeto",
        "ExportRecoveryState",
        "RestoreRecoveryState",
        "appliedVetoes[ MP_SERIES_MAX_VETO_STEPS ]",
    ):
        require(series_header, token, "exact veto and recovery seam")
    for token in (
        "appliedVeto.action = action",
        "appliedVeto.actingSide = actingSide",
        "appliedVeto.poolIndex = poolIndex",
        "appliedVeto.selectedGameSide",
        "historyDisposition",
        "applied.action != step.action",
        "mpCompetitionSeries::ExportRecoveryState",
        "mpCompetitionSeries::RestoreRecoveryState",
        "if ( !candidate.ValidateInvariants() )",
        "*this = candidate",
    ):
        require(series_source, token, "transactional exact series recovery")

    require(
        source,
        "static const uint8_t MP_SERIES_RECOVERY_MAGIC[ 8 ]",
        "versioned binary envelope",
    )
    require(source, "ComputeChecksum", "record checksum")
    require(source, "MP_SERIES_RECOVERY_FNV_OFFSET", "logical content digest")
    require(source, "ValidateRecoveryRevision", "semantic revision validation")
    require(source, "output = candidate", "transactional decode commit")
    if source.index("ValidateLogicalRecord( candidate") > source.index("output = candidate"):
        raise AssertionError("decode commits before validation")
    require(source, '"match-series/series-"', "server-owned recovery root")
    if source.count("match-series/") != 1:
        raise AssertionError("recovery must have exactly one fixed qpath root")

    for forbidden in (
        "std::vector",
        "std::string",
        "new ",
        "malloc(",
        "realloc(",
        "fopen(",
        "ofstream",
        "std::filesystem",
        "idFileSystem",
        "fileSystem",
        "gameLocal",
        "cmdSystem",
        "system(",
        "popen(",
        "MoveFile",
        "CopyFile",
        "rename(",
    ):
        reject(combined, forbidden, "allocation/filesystem-neutral recovery core")

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
    if "game/mp/match/MatchSeriesRecovery.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("MatchSeriesRecovery.cpp is absent from MP sources")


HARNESS = r'''
#include "game/mp/match/MatchSeriesRecovery.h"

#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint32_t Checksum(const unsigned char *data, int bytes) {
    uint32_t checksum = UINT32_C(0xffffffff);
    for (int index = 0; index < bytes; ++index) {
        checksum ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (checksum & 1u);
            checksum = (checksum >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~checksum;
}

static void RewriteChecksum(unsigned char *data, int bytes) {
    const uint32_t checksum = Checksum(data, bytes - 4);
    for (int shift = 0; shift < 32; shift += 8) {
        data[bytes - 4 + shift / 8] = (unsigned char)(checksum >> shift);
    }
}

static bool SameCore(const mpCompetitionSeries &a,
                     const mpCompetitionSeries &b) {
    if (a.GetState() != b.GetState() || a.GetRevision() != b.GetRevision() ||
        a.GetCurrentVetoStep() != b.GetCurrentVetoStep() ||
        a.GetAppliedVetoCount() != b.GetAppliedVetoCount() ||
        a.GetSelectedMapCount() != b.GetSelectedMapCount() ||
        a.GetNextSelectionIndex() != b.GetNextSelectionIndex() ||
        a.GetCurrentSelectionIndex() != b.GetCurrentSelectionIndex() ||
        a.GetAttemptCount() != b.GetAttemptCount() ||
        a.GetWins(0) != b.GetWins(0) || a.GetWins(1) != b.GetWins(1) ||
        a.GetMapLoadFailureCount() != b.GetMapLoadFailureCount()) {
        return false;
    }
    const mpSeriesConfiguration &ac = a.GetConfiguration();
    const mpSeriesConfiguration &bc = b.GetConfiguration();
    if (ac.sourceProfile != bc.sourceProfile || ac.gameType != bc.gameType ||
        ac.bestOf != bc.bestOf || ac.deterministicSeed != bc.deterministicSeed ||
        ac.initialSide != bc.initialSide ||
        ac.requireStartingGameSide != bc.requireStartingGameSide ||
        ac.mapPoolCount != bc.mapPoolCount ||
        ac.vetoStepCount != bc.vetoStepCount) {
        return false;
    }
    for (int i = 0; i < ac.mapPoolCount; ++i) {
        if (strcmp(ac.mapPool[i], bc.mapPool[i]) != 0 ||
            a.GetMapDisposition(i) != b.GetMapDisposition(i)) return false;
    }
    for (int i = 0; i < ac.vetoStepCount; ++i) {
        if (ac.vetoSteps[i].action != bc.vetoSteps[i].action ||
            ac.vetoSteps[i].expectedSide != bc.vetoSteps[i].expectedSide) return false;
    }
    for (int i = 0; i < a.GetAppliedVetoCount(); ++i) {
        const mpSeriesAppliedVeto *av = a.GetAppliedVeto(i);
        const mpSeriesAppliedVeto *bv = b.GetAppliedVeto(i);
        if (av == 0 || bv == 0 || av->action != bv->action ||
            av->actingSide != bv->actingSide || av->poolIndex != bv->poolIndex ||
            av->selectedGameSide != bv->selectedGameSide) return false;
    }
    for (int i = 0; i < a.GetSelectedMapCount(); ++i) {
        const mpSeriesSelectedMap *as = a.GetSelectedMap(i);
        const mpSeriesSelectedMap *bs = b.GetSelectedMap(i);
        if (as == 0 || bs == 0 || as->poolIndex != bs->poolIndex ||
            as->selectedBySide != bs->selectedBySide || as->decider != bs->decider ||
            as->hasStartingGameSide != bs->hasStartingGameSide ||
            as->startingGameSide != bs->startingGameSide ||
            as->gameSideChosenBy != bs->gameSideChosenBy) return false;
    }
    for (int i = 0; i < a.GetAttemptCount(); ++i) {
        const mpSeriesMapAttempt *aa = a.GetAttempt(i);
        const mpSeriesMapAttempt *ba = b.GetAttempt(i);
        if (aa == 0 || ba == 0 || aa->selectionIndex != ba->selectionIndex ||
            aa->outcome != ba->outcome || aa->winnerSide != ba->winnerSide ||
            aa->score[0] != ba->score[0] || aa->score[1] != ba->score[1] ||
            aa->matchSessionId != ba->matchSessionId ||
            aa->rulesDigest != ba->rulesDigest) return false;
    }
    return true;
}

static bool BuildCompleteSeries(mpCompetitionSeries &series) {
    const char *maps[] = {
        "mp/q4dm1", "mp/q4dm2", "mp/q4dm3", "mp/q4dm4", "mp/q4dm5"
    };
    mpSeriesConfiguration configuration;
    mpSeriesReason_t reason = MP_SERIES_REASON_NONE;
    if (!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_THREE, 3,
			UINT64_C(0x1020304050607080), 0, true, maps, 5, configuration, reason)) return false;
    if (!series.Configure(configuration, series.GetRevision()).WasApplied() ||
        !series.Start(series.GetRevision()).WasApplied()) return false;

    bool consumed[5] = { false, false, false, false, false };
    int lastSelection = -1;
    while (series.GetState() == MP_SERIES_VETO) {
        const int stepIndex = series.GetCurrentVetoStep();
        const mpSeriesVetoStep &step = configuration.vetoSteps[stepIndex];
        int poolIndex = lastSelection;
        int selectedSide = MP_SERIES_SIDE_NONE;
        if (step.action == MP_SERIES_VETO_SIDE) {
            selectedSide = stepIndex & 1;
        } else {
            poolIndex = -1;
            for (int i = 0; i < 5; ++i) {
                if (!consumed[i]) { poolIndex = i; break; }
            }
            if (poolIndex < 0) return false;
            consumed[poolIndex] = true;
            if (step.action == MP_SERIES_VETO_PICK ||
                step.action == MP_SERIES_VETO_DECIDER) lastSelection = poolIndex;
        }
        if (!series.ApplyVeto(step.expectedSide, step.action, maps[poolIndex],
                selectedSide, series.GetRevision()).WasApplied()) return false;
    }
    if (series.GetState() != MP_SERIES_READY ||
        series.GetAppliedVetoCount() != configuration.vetoStepCount) return false;

    const char *map = series.GetNextMapToken();
    if (map == 0 || !series.ReportMapLoadFailure(map, series.GetRevision()).WasApplied() ||
        !series.BeginMap(map, series.GetRevision()).WasApplied() ||
        !series.CommitMapResult(MP_SERIES_MAP_DECIDED, 0, 15, 9,
            1001, 501, series.GetRevision()).WasApplied() ||
        !series.AdvanceAfterMap(series.GetRevision()).WasApplied()) return false;
    map = series.GetNextMapToken();
    if (map == 0 || !series.BeginMap(map, series.GetRevision()).WasApplied() ||
        !series.CommitMapResult(MP_SERIES_MAP_ABORTED, MP_SERIES_SIDE_NONE, 4, 4,
            1002, 502, series.GetRevision()).WasApplied() ||
        !series.AdvanceAfterMap(series.GetRevision()).WasApplied()) return false;
    map = series.GetNextMapToken();
    if (map == 0 || !series.BeginMap(map, series.GetRevision()).WasApplied() ||
        !series.CommitMapResult(MP_SERIES_MAP_FORFEIT, 1, 0, 1,
            1003, 503, series.GetRevision()).WasApplied() ||
        !series.AdvanceAfterMap(series.GetRevision()).WasApplied()) return false;
    map = series.GetNextMapToken();
    if (map == 0 || !series.BeginMap(map, series.GetRevision()).WasApplied() ||
        !series.CommitMapResult(MP_SERIES_MAP_DECIDED, 0, 12, 10,
            1004, 504, series.GetRevision()).WasApplied() ||
        !series.AdvanceAfterMap(series.GetRevision()).WasApplied()) return false;
    return series.GetState() == MP_SERIES_COMPLETE &&
        series.GetAttemptCount() == 4 && series.GetWins(0) == 2 &&
        series.GetWins(1) == 1 && series.ValidateInvariants();
}

static bool BuildNoSideSeries(mpCompetitionSeries &series) {
    const char *maps[] = { "mp/q4dm1" };
    mpSeriesConfiguration configuration;
    mpSeriesReason_t reason = MP_SERIES_REASON_NONE;
    if (!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_ONE, 0,
            UINT64_C(77), 0, false, maps, 1, configuration, reason) ||
        configuration.requireStartingGameSide || configuration.vetoStepCount != 1 ||
        configuration.vetoSteps[0].action != MP_SERIES_VETO_DECIDER ||
        !series.Configure(configuration, series.GetRevision()).WasApplied() ||
        !series.Start(series.GetRevision()).WasApplied() ||
        !series.ApplyVeto(configuration.vetoSteps[0].expectedSide,
            MP_SERIES_VETO_DECIDER, maps[0], MP_SERIES_SIDE_NONE,
            series.GetRevision()).WasApplied()) return false;
    const mpSeriesSelectedMap *selection = series.GetSelectedMap(0);
    return series.GetState() == MP_SERIES_READY && selection != 0 &&
        !selection->hasStartingGameSide && series.ValidateInvariants();
}

class TestWriter : public mpMatchSeriesRecoveryWriter {
public:
    enum Mode { OK, WRITE_FAIL, SHORT_WRITE, PROMOTE_FAIL };
    Mode mode;
    bool cleanupSucceeds;
    int writeCalls, promoteCalls, removeCalls, storedBytes;
    unsigned char stored[MP_SERIES_RECOVERY_MAX_BYTES];
    char temporary[MP_SERIES_RECOVERY_QPATH_BYTES + 1];
    char finalPath[MP_SERIES_RECOVERY_QPATH_BYTES + 1];

    TestWriter() : mode(OK), cleanupSucceeds(true), writeCalls(0), promoteCalls(0),
        removeCalls(0), storedBytes(0) {
        memset(stored, 0, sizeof(stored));
        memset(temporary, 0, sizeof(temporary));
        memset(finalPath, 0, sizeof(finalPath));
    }
    int WriteTemp(const char *path, const void *data, int bytes) override {
        ++writeCalls;
        strncpy(temporary, path, sizeof(temporary) - 1);
        if (mode == WRITE_FAIL) return -1;
        const int accepted = mode == SHORT_WRITE ? bytes - 1 : bytes;
        if (accepted > 0) memcpy(stored, data, accepted);
        storedBytes = accepted;
        return accepted;
    }
    bool Promote(const char *temporaryPath, const char *destination) override {
        ++promoteCalls;
        if (strcmp(temporary, temporaryPath) != 0) return false;
        strncpy(finalPath, destination, sizeof(finalPath) - 1);
        return mode != PROMOTE_FAIL;
    }
    bool RemoveTemp(const char *path) override {
        ++removeCalls;
        return strcmp(temporary, path) == 0 && cleanupSucceeds;
    }
};

int main() {
    static mpCompetitionSeries original;
    CHECK(BuildCompleteSeries(original));
    CHECK(original.GetRevision() == 23);
    CHECK(original.GetAppliedVeto(0) != 0);
    CHECK(original.GetAppliedVeto(-1) == 0);
    CHECK(original.GetAppliedVeto(original.GetAppliedVetoCount()) == 0);

    mpSeriesRecoveryReason_t reason = MP_SERIES_RECOVERY_REASON_NONE;
    static mpSeriesRecoveryRecord record;
    record.Clear();
    CHECK(MPMatchSeriesRecoveryCapture(original, UINT64_C(72623859790382856),
        1004, record, &reason));
    CHECK(reason == MP_SERIES_RECOVERY_REASON_NONE && record.seriesId != 0 &&
        record.linkedSessionId == 1004 && record.contentDigest != 0);
    CHECK(MPMatchSeriesRecoveryValidate(record, &reason));
    CHECK(record.series.appliedVetoCount == original.GetAppliedVetoCount());

    static mpSeriesRecoveryWorkspace workspace;
    mpSeriesRecoveryCodecResult encoded = MPMatchSeriesRecoveryEncode(
        record, workspace.bytes, sizeof(workspace.bytes));
    CHECK(encoded.Succeeded() && encoded.bytes > 44 &&
        encoded.bytes < MP_SERIES_RECOVERY_MAX_BYTES &&
        encoded.contentDigest == record.contentDigest && encoded.checksum != 0);

    static mpSeriesRecoveryRecord decoded;
    decoded.Clear();
    mpSeriesRecoveryCodecResult decodedResult = MPMatchSeriesRecoveryDecode(
        workspace.bytes, encoded.bytes, decoded);
    CHECK(decodedResult.Succeeded() && decodedResult.bytes == encoded.bytes &&
        decoded.seriesId == record.seriesId &&
        decoded.linkedSessionId == record.linkedSessionId &&
        decoded.contentDigest == record.contentDigest);
    static mpCompetitionSeries restored;
    CHECK(restored.RestoreRecoveryState(decoded.series));
    CHECK(SameCore(original, restored));

    static mpSeriesRecoveryWorkspace deterministic;
    mpSeriesRecoveryCodecResult encodedAgain = MPMatchSeriesRecoveryEncode(
        decoded, deterministic.bytes, sizeof(deterministic.bytes));
    CHECK(encodedAgain.Succeeded() && encodedAgain.bytes == encoded.bytes &&
        memcmp(workspace.bytes, deterministic.bytes, encoded.bytes) == 0);
    mpSeriesRecoveryCodecResult tooSmall = MPMatchSeriesRecoveryEncode(
        record, deterministic.bytes, encoded.bytes - 1);
    CHECK(!tooSmall.Succeeded() &&
        tooSmall.reason == MP_SERIES_RECOVERY_REASON_BUFFER_TOO_SMALL &&
        tooSmall.requiredCapacity == encoded.bytes);

    static mpCompetitionSeries noSideSeries;
    CHECK(BuildNoSideSeries(noSideSeries));
    static mpSeriesRecoveryRecord noSideRecord;
    noSideRecord.Clear();
    CHECK(MPMatchSeriesRecoveryCapture(noSideSeries, 77, 88,
        noSideRecord, &reason));
    static mpSeriesRecoveryWorkspace noSideBytes;
    mpSeriesRecoveryCodecResult noSideEncoded = MPMatchSeriesRecoveryEncode(
        noSideRecord, noSideBytes.bytes, sizeof(noSideBytes.bytes));
    CHECK(noSideEncoded.Succeeded());
    static mpSeriesRecoveryRecord noSideDecoded;
    noSideDecoded.Clear();
    CHECK(MPMatchSeriesRecoveryDecode(noSideBytes.bytes, noSideEncoded.bytes,
        noSideDecoded).Succeeded());
    CHECK(!noSideDecoded.series.configuration.requireStartingGameSide);
    static mpCompetitionSeries noSideRestored;
    CHECK(noSideRestored.RestoreRecoveryState(noSideDecoded.series));
    CHECK(SameCore(noSideSeries, noSideRestored));

    static mpSeriesRecoveryRecord sentinel;
    sentinel.Clear();
    sentinel.seriesId = UINT64_C(999999);
    sentinel.linkedSessionId = UINT64_C(888888);
    static mpSeriesRecoveryRecord sentinelBefore;
    sentinelBefore = sentinel;
    for (int length = 0; length < encoded.bytes; ++length) {
        mpSeriesRecoveryCodecResult truncated = MPMatchSeriesRecoveryDecode(
            workspace.bytes, length, sentinel);
        CHECK(!truncated.Succeeded());
        CHECK(memcmp(&sentinel, &sentinelBefore, sizeof(sentinel)) == 0);
    }
    CHECK(MPMatchSeriesRecoveryDecode(0, encoded.bytes, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT);
    CHECK(MPMatchSeriesRecoveryDecode(workspace.bytes, -1, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT);

    static unsigned char hostile[MP_SERIES_RECOVERY_MAX_BYTES + 1];
    memcpy(hostile, workspace.bytes, encoded.bytes);
    hostile[encoded.bytes] = 0x7f;
    CHECK(MPMatchSeriesRecoveryDecode(hostile, encoded.bytes + 1, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_TRAILING_DATA);
    CHECK(memcmp(&sentinel, &sentinelBefore, sizeof(sentinel)) == 0);

    memcpy(hostile, workspace.bytes, encoded.bytes);
    hostile[8] = 1;
    CHECK(MPMatchSeriesRecoveryDecode(hostile, encoded.bytes, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_UNSUPPORTED_SCHEMA);
    CHECK(memcmp(&sentinel, &sentinelBefore, sizeof(sentinel)) == 0);

    memcpy(hostile, workspace.bytes, encoded.bytes);
    hostile[50] ^= 0x40;
    CHECK(MPMatchSeriesRecoveryDecode(hostile, encoded.bytes, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_CHECKSUM_MISMATCH);
    CHECK(memcmp(&sentinel, &sentinelBefore, sizeof(sentinel)) == 0);

    memcpy(hostile, workspace.bytes, encoded.bytes);
    hostile[24] ^= 1;
    RewriteChecksum(hostile, encoded.bytes);
    CHECK(MPMatchSeriesRecoveryDecode(hostile, encoded.bytes, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH);
    CHECK(memcmp(&sentinel, &sentinelBefore, sizeof(sentinel)) == 0);

    memcpy(hostile, workspace.bytes, encoded.bytes);
    hostile[10] = 1;
    RewriteChecksum(hostile, encoded.bytes);
    CHECK(MPMatchSeriesRecoveryDecode(hostile, encoded.bytes, sentinel).reason ==
        MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD);

    static mpSeriesRecoveryRecord bad;
    bad = record;
    bad.series.appliedVetoes[0].poolIndex = MP_SERIES_MAX_MAP_POOL;
    CHECK(!MPMatchSeriesRecoveryValidate(bad, &reason));
    CHECK(reason == MP_SERIES_RECOVERY_REASON_INVALID_SERIES);
    CHECK(MPMatchSeriesRecoveryComputeContentDigest(bad) == 0);
    const uint64_t unchangedRevision = restored.GetRevision();
    CHECK(!restored.RestoreRecoveryState(bad.series));
    CHECK(restored.GetRevision() == unchangedRevision && SameCore(original, restored));
    bad = record;
    ++bad.series.revision;
    CHECK(MPMatchSeriesRecoveryComputeContentDigest(bad) == 0);

    static mpCompetitionSeries disabled;
    static mpSeriesRecoveryRecord rejectedCapture;
    rejectedCapture = record;
    CHECK(!MPMatchSeriesRecoveryCapture(disabled, 1, 1, rejectedCapture, &reason));
    CHECK(rejectedCapture.seriesId == record.seriesId);
    CHECK(!MPMatchSeriesRecoveryCapture(original, 0, 1004,
        rejectedCapture, &reason));
    CHECK(reason == MP_SERIES_RECOVERY_REASON_INVALID_IDENTITY);

    mpSeriesRecoveryPaths paths;
    paths.Clear();
    CHECK(MPMatchSeriesRecoveryBuildPaths(record, paths, &reason));
    CHECK(strcmp(paths.finalQPath,
        "match-series/series-72623859790382856.oq4series") == 0);
    CHECK(strcmp(paths.temporaryQPath,
        "match-series/series-72623859790382856.oq4series.pending-23") == 0);
    CHECK(MPMatchSeriesRecoveryIsFinalQPath(paths.finalQPath));
    CHECK(MPMatchSeriesRecoveryIsTemporaryQPath(paths.temporaryQPath));
    CHECK(MPMatchSeriesRecoveryIsPromotionPair(paths.temporaryQPath,
        paths.finalQPath));
    char directPath[MP_SERIES_RECOVERY_QPATH_BYTES + 1];
    memset(directPath, 0x5a, sizeof(directPath));
    CHECK(MPMatchSeriesRecoveryBuildFinalQPath(record.seriesId, directPath,
        sizeof(directPath), &reason));
    CHECK(strcmp(directPath, paths.finalQPath) == 0);
    char tinyPath[8] = { 'k', 'e', 'e', 'p', '\0', 0, 0, 0 };
    CHECK(!MPMatchSeriesRecoveryBuildFinalQPath(record.seriesId, tinyPath,
        sizeof(tinyPath), &reason));
    CHECK(strcmp(tinyPath, "keep") == 0 &&
        reason == MP_SERIES_RECOVERY_REASON_PATH_TOO_LONG);
    CHECK(!MPMatchSeriesRecoveryBuildFinalQPath(0, directPath,
        sizeof(directPath), &reason));
    CHECK(reason == MP_SERIES_RECOVERY_REASON_INVALID_IDENTITY);
    CHECK(!MPMatchSeriesRecoveryIsFinalQPath("match-series/series-0.oq4series"));
    CHECK(!MPMatchSeriesRecoveryIsFinalQPath("../series-1.oq4series"));
    CHECK(!MPMatchSeriesRecoveryIsTemporaryQPath(
        "match-series/series-1.oq4series.pending-0"));
    CHECK(!MPMatchSeriesRecoveryIsPromotionPair(
        "match-series/series-2.oq4series.pending-1",
        "match-series/series-1.oq4series"));

    static TestWriter success;
    mpSeriesRecoveryStorageResult stored = MPMatchSeriesRecoveryPersist(
        record, success, deterministic);
    CHECK(stored.Succeeded() && success.writeCalls == 1 &&
        success.promoteCalls == 1 && success.removeCalls == 0 &&
        success.storedBytes == stored.serializedBytes);
    static mpSeriesRecoveryRecord persisted;
    persisted.Clear();
    CHECK(MPMatchSeriesRecoveryDecode(success.stored, success.storedBytes,
        persisted).Succeeded());
    CHECK(persisted.contentDigest == record.contentDigest);

    static TestWriter writeFail;
    writeFail.mode = TestWriter::WRITE_FAIL;
    stored = MPMatchSeriesRecoveryPersist(record, writeFail, deterministic);
    CHECK(!stored.Succeeded() && stored.code == MP_SERIES_RECOVERY_STORAGE_FAILED &&
        stored.reason == MP_SERIES_RECOVERY_REASON_TEMP_WRITE_FAILED &&
        writeFail.writeCalls == 1 && writeFail.promoteCalls == 0 &&
        writeFail.removeCalls == 1);
    static TestWriter shortWrite;
    shortWrite.mode = TestWriter::SHORT_WRITE;
    stored = MPMatchSeriesRecoveryPersist(record, shortWrite, deterministic);
    CHECK(stored.reason == MP_SERIES_RECOVERY_REASON_TEMP_WRITE_PARTIAL &&
        shortWrite.promoteCalls == 0 && shortWrite.removeCalls == 1);
    static TestWriter promoteFail;
    promoteFail.mode = TestWriter::PROMOTE_FAIL;
    stored = MPMatchSeriesRecoveryPersist(record, promoteFail, deterministic);
    CHECK(stored.reason == MP_SERIES_RECOVERY_REASON_PROMOTION_FAILED &&
        promoteFail.promoteCalls == 1 && promoteFail.removeCalls == 1);
    static TestWriter cleanupFail;
    cleanupFail.mode = TestWriter::PROMOTE_FAIL;
    cleanupFail.cleanupSucceeds = false;
    stored = MPMatchSeriesRecoveryPersist(record, cleanupFail, deterministic);
    CHECK(stored.reason == MP_SERIES_RECOVERY_REASON_PROMOTION_FAILED &&
        stored.cleanupReason == MP_SERIES_RECOVERY_REASON_TEMP_CLEANUP_FAILED);
    return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_series_recovery_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="series-recovery-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "series_recovery_contract.cpp"
        executable = temp_dir / (
            "series_recovery_contract.exe"
            if compiler.lower().endswith(".exe")
            else "series_recovery_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-DMP_MATCH_SERIES_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_RECOVERY_STANDALONE_TEST",
                f"-I{ROOT / 'src'}",
                str(harness),
                str(SERIES_SOURCE),
                str(REPORT_SOURCE),
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
                "standalone series-recovery contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"series-recovery invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    static_contracts()
    executable_contract()
    print("mp_match_series_recovery_contract: PASS")


if __name__ == "__main__":
    main()
