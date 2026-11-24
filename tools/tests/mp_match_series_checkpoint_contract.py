#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Atomic unified series/report recovery checkpoint contracts."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
RECOVERY_HEADER = MATCH_DIR / "MatchSeriesRecovery.h"
RECOVERY_SOURCE = MATCH_DIR / "MatchSeriesRecovery.cpp"
SERIES_SOURCE = MATCH_DIR / "MatchSeries.cpp"
REPORT_HEADER = MATCH_DIR / "MatchSeriesReport.h"
REPORT_SOURCE = MATCH_DIR / "MatchSeriesReport.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts() -> None:
    header = read(RECOVERY_HEADER)
    source = read(RECOVERY_SOURCE)
    report_header = read(REPORT_HEADER)
    for token in (
        "MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION = 2",
        "MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION = 3",
        "MP_SERIES_RECOVERY_SCHEMA_VERSION = 4",
        "MP_SERIES_RECOVERY_MAX_BYTES = 65536",
        "bool\t\t\t\t\thasReport",
        "mpSeriesReportCheckpointState report",
        "const mpCompetitionSeriesReport &report",
        "MPMatchSeriesRecoveryRestoreCores",
        "MP_SERIES_RECOVERY_REASON_INVALID_REPORT",
        "MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH",
    ):
        require(header, token, "unified checkpoint API")
    for token in (
        "MP_SERIES_RECOVERY_FLAG_REPORT",
        "WriteReportPayload",
        "ReadReportPayload",
        "ValidateReportAgainstSeries",
        "candidate.hasReport",
        "ComputeLegacyV2ContentDigest",
        "series = candidateSeries",
        "report = candidateReport",
    ):
        require(source, token, "one-file report payload and paired restore")
    for token in (
        "mpSeriesReportCheckpointState",
        "ExportCheckpointState",
        "RestoreCheckpointState",
        "AccumulateParticipantStats",
        "AccumulateTeamStats",
    ):
        require(report_header, token, "mutable report checkpoint seam")


HARNESS = r'''
#include "game/mp/match/MatchSeriesRecovery.h"

#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint64_t FnvByte(uint64_t digest, unsigned char value) {
    return (digest ^ value) * UINT64_C(1099511628211);
}

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

static void PutLittle64(unsigned char *destination, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        destination[shift / 8] = (unsigned char)(value >> shift);
    }
}

static void RewriteChecksum(unsigned char *data, int bytes) {
    const uint32_t checksum = Checksum(data, bytes - 4);
    for (int shift = 0; shift < 32; shift += 8) {
        data[bytes - 4 + shift / 8] = (unsigned char)(checksum >> shift);
    }
}

static bool BuildMapComplete(mpCompetitionSeries &series,
                             mpCompetitionSeriesReport &report,
                             uint64_t seriesId) {
    const char *maps[] = { "mp/q4dm1" };
    mpSeriesConfiguration configuration;
    mpSeriesReason_t reason = MP_SERIES_REASON_NONE;
    if (!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_ONE, 3,
            UINT64_C(0x123456789abcdef0), 0, false, maps, 1,
            configuration, reason) ||
        !series.Configure(configuration, series.GetRevision()).WasApplied() ||
        !series.Start(series.GetRevision()).WasApplied() ||
        !series.ApplyVeto(configuration.vetoSteps[0].expectedSide,
            MP_SERIES_VETO_DECIDER, maps[0], MP_SERIES_SIDE_NONE,
            series.GetRevision()).WasApplied()) {
        return false;
    }

    mpSeriesReportIdentityInput identity;
    memset(&identity, 0, sizeof(identity));
    identity.seriesId = seriesId;
    identity.profile = MP_SERIES_PROFILE_BEST_OF_ONE;
    identity.profileKey = "best_of_one";
    identity.bestOf = 1;
    identity.rulesSchema = 1;
    identity.rulesRevision = 7;
    identity.rulesDigest = UINT64_C(0x0102030405060708);
    identity.gameType = 3;
    identity.modeToken = "team_dm";
    identity.contestants[0].kind = MP_SERIES_REPORT_CONTESTANT_SIDE;
    identity.contestants[0].label = "Marine";
    identity.contestants[1].kind = MP_SERIES_REPORT_CONTESTANT_SIDE;
    identity.contestants[1].label = "Strogg";
    if (!report.Initialize(identity).WasAccepted()) return false;

    const char *map = series.GetNextMapToken();
    if (map == 0 || !series.BeginMap(map, series.GetRevision()).WasApplied() ||
        !series.CommitMapResult(MP_SERIES_MAP_DECIDED, 0, 15, 9,
            9001, identity.rulesDigest, series.GetRevision()).WasApplied()) {
        return false;
    }

    mpSeriesReportMapResultInput result;
    memset(&result, 0, sizeof(result));
    result.attempt = 1;
    result.sessionId = 9001;
    result.mapToken = map;
    result.rulesDigest = identity.rulesDigest;
    result.outcome = MP_SERIES_REPORT_MAP_DECIDED;
    result.reason = 41;
    result.winnerContestant = 0;
    result.score[0] = 15;
    result.score[1] = 9;
    result.artifacts[MP_SERIES_REPORT_ARTIFACT_EVIDENCE].status =
        MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
    result.artifacts[MP_SERIES_REPORT_ARTIFACT_EVIDENCE].qpath =
        "match-results/session-9001_series-42_q4dm1.json";
    result.artifacts[MP_SERIES_REPORT_ARTIFACT_MVD].status =
        MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
    result.artifacts[MP_SERIES_REPORT_ARTIFACT_MVD].qpath =
        "demos/match_9001_q4dm1.mvd";
    if (!report.AppendMapResult(result).WasAccepted()) return false;

    mpSeriesReportParticipantStatsInput player;
    memset(&player, 0, sizeof(player));
    player.participantSequence = 101;
    player.contestant = 0;
    player.displayName = "Player One";
    player.mapsPlayed = 1;
    player.mapsWon = 1;
    player.score = 15;
    player.kills = 15;
    player.deaths = 9;
    player.damageGiven = 2300;
    player.damageReceived = 1700;
    player.shots = 120;
    player.hits = 60;
    if (!report.AccumulateParticipantStats(player).WasAccepted()) return false;

    mpSeriesReportTeamStatsInput team;
    memset(&team, 0, sizeof(team));
    team.contestant = 0;
    team.mapsPlayed = 1;
    team.mapsWon = 1;
    team.score = 15;
    team.damageGiven = 2300;
    if (!report.AccumulateTeamStats(team).WasAccepted()) return false;
    return series.GetState() == MP_SERIES_MAP_COMPLETE &&
        report.ValidateInvariants();
}

int main() {
    static mpCompetitionSeries series;
    static mpCompetitionSeriesReport report;
    const uint64_t seriesId = 42;
    CHECK(BuildMapComplete(series, report, seriesId));

    static mpSeriesRecoveryRecord checkpoint;
    checkpoint.Clear();
    mpSeriesRecoveryReason_t reason = MP_SERIES_RECOVERY_REASON_NONE;
    CHECK(MPMatchSeriesRecoveryCapture(series, report, seriesId, 9001,
        checkpoint, &reason));
    CHECK(checkpoint.hasReport && checkpoint.report.mapResultCount == 1 &&
        checkpoint.report.participantStatsCount == 1 &&
        checkpoint.report.teamStatsCount == 1 && checkpoint.contentDigest != 0);

    static mpSeriesRecoveryWorkspace workspace;
    const mpSeriesRecoveryCodecResult encoded = MPMatchSeriesRecoveryEncode(
        checkpoint, workspace.bytes, sizeof(workspace.bytes));
    CHECK(encoded.Succeeded() && encoded.bytes < MP_SERIES_RECOVERY_MAX_BYTES &&
        workspace.bytes[8] == MP_SERIES_RECOVERY_SCHEMA_VERSION &&
        workspace.bytes[10] == 1);

    static mpSeriesRecoveryRecord decoded;
    decoded.Clear();
    CHECK(MPMatchSeriesRecoveryDecode(workspace.bytes, encoded.bytes,
        decoded).Succeeded());
    CHECK(decoded.hasReport && decoded.contentDigest == checkpoint.contentDigest);

    // Schema 3 used the same paired payload layout.  Authenticate it under its
    // own schema digest, then normalize it to schema 4 logical state.
    static mpSeriesRecoveryWorkspace previousWorkspace;
    memcpy(previousWorkspace.bytes, workspace.bytes, encoded.bytes);
    previousWorkspace.bytes[8] = MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION;
    previousWorkspace.bytes[9] = 0;
    uint64_t previousDigest = UINT64_C(14695981039346656037);
    previousDigest = FnvByte(previousDigest,
        MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION);
    previousDigest = FnvByte(previousDigest, 0);
    for (int index = 16; index < 32; ++index) {
        previousDigest = FnvByte(previousDigest, previousWorkspace.bytes[index]);
    }
    previousDigest = FnvByte(previousDigest, 1);
    for (int index = 40; index < encoded.bytes - 4; ++index) {
        previousDigest = FnvByte(previousDigest, previousWorkspace.bytes[index]);
    }
    PutLittle64(previousWorkspace.bytes + 32, previousDigest);
    RewriteChecksum(previousWorkspace.bytes, encoded.bytes);
    static mpSeriesRecoveryRecord decodedV3;
    decodedV3.Clear();
    CHECK(MPMatchSeriesRecoveryDecode(previousWorkspace.bytes, encoded.bytes,
        decodedV3).Succeeded());
    CHECK(decodedV3.hasReport && decodedV3.seriesId == seriesId &&
        decodedV3.contentDigest != previousDigest &&
        MPMatchSeriesRecoveryValidate(decodedV3, &reason));

    static mpCompetitionSeries restoredSeries;
    static mpCompetitionSeriesReport restoredReport;
    CHECK(MPMatchSeriesRecoveryRestoreCores(decoded, restoredSeries,
        restoredReport, &reason));
    CHECK(restoredSeries.GetState() == MP_SERIES_MAP_COMPLETE &&
        restoredSeries.GetAttemptCount() == 1 &&
        restoredReport.GetMapResultCount() == 1 &&
        restoredReport.GetParticipantStatsCount() == 1 &&
        restoredReport.GetTeamStatsCount() == 1 &&
        !restoredReport.IsFinalized());

    static mpCompetitionSeries completeSeries;
    static mpCompetitionSeriesReport completeReport;
    completeSeries = series;
    completeReport = report;
    CHECK(completeSeries.AdvanceAfterMap(
        completeSeries.GetRevision()).WasApplied());
    mpSeriesReportFinalInput finalResult;
    memset(&finalResult, 0, sizeof(finalResult));
    finalResult.outcome = MP_SERIES_REPORT_FINAL_COMPLETE;
    finalResult.reason = 51;
    finalResult.winnerContestant = 0;
    finalResult.authorizer = MPSeriesReportSystemAuthorizer();
    CHECK(completeReport.Finalize(finalResult).WasAccepted());
    static mpSeriesRecoveryRecord terminal;
    terminal.Clear();
    CHECK(MPMatchSeriesRecoveryCapture(completeSeries, completeReport,
        seriesId, 9001, terminal, &reason));
    CHECK(terminal.hasReport && terminal.report.finalResult.outcome ==
        MP_SERIES_REPORT_FINAL_COMPLETE);

    static mpSeriesRecoveryRecord mismatch;
    mismatch = checkpoint;
    ++mismatch.report.mapResults[0].sessionId;
    CHECK(!MPMatchSeriesRecoveryValidate(mismatch, &reason) &&
        reason == MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH);

    static mpSeriesRecoveryRecord legacySeriesOnly;
    legacySeriesOnly.Clear();
    CHECK(MPMatchSeriesRecoveryCapture(series, seriesId, 9001,
        legacySeriesOnly, &reason));
    CHECK(!legacySeriesOnly.hasReport);
    const uint64_t beforeSeriesRevision = restoredSeries.GetRevision();
    const uint64_t beforeReportRevision = restoredReport.GetReportRevision();
    CHECK(!MPMatchSeriesRecoveryRestoreCores(legacySeriesOnly, restoredSeries,
        restoredReport, &reason));
    CHECK(reason == MP_SERIES_RECOVERY_REASON_INVALID_REPORT &&
        restoredSeries.GetRevision() == beforeSeriesRevision &&
        restoredReport.GetReportRevision() == beforeReportRevision);

    // A v2 series-only record has the same series payload.  Rewrite only
    // its schema/digest/checksum to exercise the real legacy decoder path.
    static mpSeriesRecoveryWorkspace legacyWorkspace;
    const mpSeriesRecoveryCodecResult legacyEncoded = MPMatchSeriesRecoveryEncode(
        legacySeriesOnly, legacyWorkspace.bytes, sizeof(legacyWorkspace.bytes));
    CHECK(legacyEncoded.Succeeded() && legacyWorkspace.bytes[10] == 0);
    legacyWorkspace.bytes[8] = MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION;
    legacyWorkspace.bytes[9] = 0;
    uint64_t legacyDigest = UINT64_C(14695981039346656037);
    legacyDigest = FnvByte(legacyDigest, 2);
    legacyDigest = FnvByte(legacyDigest, 0);
    for (int index = 16; index < 32; ++index) {
        legacyDigest = FnvByte(legacyDigest, legacyWorkspace.bytes[index]);
    }
    for (int index = 40; index < legacyEncoded.bytes - 4; ++index) {
        legacyDigest = FnvByte(legacyDigest, legacyWorkspace.bytes[index]);
    }
    PutLittle64(legacyWorkspace.bytes + 32, legacyDigest);
    RewriteChecksum(legacyWorkspace.bytes, legacyEncoded.bytes);
    static mpSeriesRecoveryRecord decodedV2;
    decodedV2.Clear();
    CHECK(MPMatchSeriesRecoveryDecode(legacyWorkspace.bytes,
        legacyEncoded.bytes, decodedV2).Succeeded());
    CHECK(!decodedV2.hasReport && decodedV2.seriesId == seriesId &&
        decodedV2.contentDigest != legacyDigest &&
        MPMatchSeriesRecoveryValidate(decodedV2, &reason));
    return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_series_checkpoint_contract: executable checks skipped (no C++ compiler)")
        return
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="series-checkpoint-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "series_checkpoint_contract.cpp"
        executable = temp_dir / (
            "series_checkpoint_contract.exe"
            if compiler.lower().endswith(".exe")
            else "series_checkpoint_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DMP_MATCH_SERIES_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST",
                "-DMP_MATCH_SERIES_RECOVERY_STANDALONE_TEST",
                f"-I{ROOT / 'src'}",
                str(harness),
                str(SERIES_SOURCE),
                str(REPORT_SOURCE),
                str(RECOVERY_SOURCE),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone unified checkpoint contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"unified checkpoint invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    static_contracts()
    executable_contract()
    print("mp_match_series_checkpoint_contract: PASS")


if __name__ == "__main__":
    main()
