#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contracts and executable invariants for bounded competitive series state."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchSeries.h"
SOURCE = ROOT / "src/game/mp/match/MatchSeries.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_SERIES_MAX_BEST_OF = 15",
        "MP_SERIES_MAX_MAP_POOL = 32",
        "MP_SERIES_MAX_VETO_STEPS = 64",
        "MP_SERIES_MAP_TOKEN_BYTES = 64",
        "MP_SERIES_DISABLED",
        "MP_SERIES_VETO",
        "MP_SERIES_MAP_ACTIVE",
        "MP_SERIES_COMPLETE",
        "deterministicSeed",
        "sourceProfile",
        "expectedRevision",
        "ValidateConfiguration",
        "ReportMapLoadFailure",
        "MP_SERIES_MAP_ABORTED",
        "ValidateInvariants",
        "MP_SERIES_PROFILE_BEST_OF_ONE",
        "MP_SERIES_PROFILE_BEST_OF_THREE",
        "MP_SERIES_PROFILE_BEST_OF_FIVE",
        "MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE",
        "MPSeriesProfileByKey",
        "MPSeriesBuildProfileDraft",
        '"#str_41693"',
        '"#str_41698"',
    ):
        if token not in combined:
            raise AssertionError(f"missing series invariant {token!r}")

    for forbidden in (
        "idUserInterface",
        "idBitMsg",
        "idCVar",
        "idFile",
        "cmdSystem",
        "BufferCommandText",
        "fileSystem",
        "idList<",
    ):
        if forbidden in combined:
            raise AssertionError(f"series core contains forbidden dependency {forbidden!r}")

    if source.count("++revision") != 8:
        raise AssertionError("each of the eight committing operations must own one revision increment")
    if "SeriesTokenEquals" not in source or "IsSafeMapToken" not in source:
        raise AssertionError("series map identity must be bounded and canonical")


HARNESS = r'''
#include "game/mp/match/MatchSeries.h"
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

static bool AppliedOnce(const mpSeriesMutationResult &result) {
	return result.WasApplied() && result.currentRevision == result.previousRevision + 1;
}

static void SetToken(char *target, const char *value) {
	strncpy(target, value, MP_SERIES_MAP_TOKEN_BYTES - 1);
	target[MP_SERIES_MAP_TOKEN_BYTES - 1] = '\0';
}

static mpSeriesConfiguration GoodConfiguration() {
	mpSeriesConfiguration config;
	memset(&config, 0, sizeof(config));
	const char *maps[] = {
		"mp/q4dm1", "mp/q4dm2", "mp/q4dm3", "mp/q4dm4", "mp/q4dm5"
	};
	mpSeriesReason_t reason = MP_SERIES_REASON_COUNT;
	if (!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_THREE, 2,
		0x12345678u, 0, true, maps, 5, config, reason)) {
		memset(&config, 0, sizeof(config));
		config.gameType = -1;
	}
	return config;
}

int main() {
	mpCompetitionSeries series;
	CHECK(series.ValidateInvariants());
	CHECK(series.GetState() == MP_SERIES_DISABLED);
	CHECK(MPSeriesProfileDescriptorCount() == 3);
	const mpSeriesProfileDescriptor *bo3 = MPSeriesProfileByKey("BEST_OF_THREE");
	CHECK(bo3 != NULL);
	CHECK(bo3->id == MP_SERIES_PROFILE_BEST_OF_THREE);
	CHECK(bo3->bestOf == 3);
	CHECK(strcmp(bo3->labelLocalizationKey, "#str_41695") == 0);
	CHECK(MPSeriesProfileByKey("standard.cfg") == NULL);
	CHECK(MPSeriesProfileDescriptorForId(MP_SERIES_PROFILE_CUSTOM) == NULL);

	mpSeriesConfiguration preserved;
	memset(&preserved, 0x5a, sizeof(preserved));
	mpSeriesConfiguration failedDraft = preserved;
	const char *unsafeMaps[] = { "mp/q4dm1", "../escape", "mp/q4dm3" };
	mpSeriesReason_t draftReason = MP_SERIES_REASON_NONE;
	CHECK(!MPSeriesBuildProfileDraft(MP_SERIES_PROFILE_BEST_OF_THREE, 2, 7,
		0, true, unsafeMaps, 3, failedDraft, draftReason));
	CHECK(draftReason == MP_SERIES_REASON_INVALID_MAP_TOKEN);
	CHECK(memcmp(&failedDraft, &preserved, sizeof(preserved)) == 0);

	char token63[MP_SERIES_MAP_TOKEN_BYTES];
	memset(token63, 'a', sizeof(token63));
	token63[MP_SERIES_MAP_TOKEN_BYTES - 1] = '\0';
	CHECK(mpCompetitionSeries::IsSafeMapToken(token63));
	char token64[MP_SERIES_MAP_TOKEN_BYTES + 1];
	memset(token64, 'a', sizeof(token64));
	token64[MP_SERIES_MAP_TOKEN_BYTES] = '\0';
	CHECK(!mpCompetitionSeries::IsSafeMapToken(token64));

	mpSeriesConfiguration invalid = GoodConfiguration();
	SetToken(invalid.mapPool[4], "MP/Q4DM1");
	const mpSeriesMutationResult badConfig = series.Configure(invalid, 0);
	CHECK(badConfig.WasRejected());
	CHECK(badConfig.reason == MP_SERIES_REASON_DUPLICATE_MAP);
	CHECK(series.GetRevision() == 0);

	mpSeriesConfiguration config = GoodConfiguration();
	CHECK(config.gameType == 2);
	CHECK(config.sourceProfile == MP_SERIES_PROFILE_BEST_OF_THREE);
	CHECK(config.vetoStepCount == 8);
	CHECK(config.vetoSteps[0].action == MP_SERIES_VETO_BAN &&
		config.vetoSteps[0].expectedSide == 0);
	CHECK(config.vetoSteps[1].action == MP_SERIES_VETO_BAN &&
		config.vetoSteps[1].expectedSide == 1);
	CHECK(config.vetoSteps[2].action == MP_SERIES_VETO_PICK &&
		config.vetoSteps[2].expectedSide == 0);
	CHECK(config.vetoSteps[3].action == MP_SERIES_VETO_SIDE &&
		config.vetoSteps[3].expectedSide == 1);
	CHECK(config.vetoSteps[6].action == MP_SERIES_VETO_DECIDER &&
		config.vetoSteps[6].expectedSide == 0);
	CHECK(config.vetoSteps[7].action == MP_SERIES_VETO_SIDE &&
		config.vetoSteps[7].expectedSide == 1);
	mpSeriesReason_t invalidReason = MP_SERIES_REASON_NONE;
	mpSeriesConfiguration nonAlternating = config;
	nonAlternating.vetoSteps[1].expectedSide = 0;
	CHECK(!mpCompetitionSeries::ValidateConfiguration(nonAlternating, invalidReason));
	CHECK(invalidReason == MP_SERIES_REASON_INVALID_VETO_PATTERN);
	mpSeriesConfiguration missingSide = config;
	missingSide.vetoStepCount -= 1;
	CHECK(!mpCompetitionSeries::ValidateConfiguration(missingSide, invalidReason));
	CHECK(invalidReason == MP_SERIES_REASON_INVALID_VETO_PATTERN);
	CHECK(AppliedOnce(series.Configure(config, 0)));
	CHECK(AppliedOnce(series.Start(series.GetRevision())));
	const uint64_t vetoRevision = series.GetRevision();
	CHECK(series.ApplyVeto(1, MP_SERIES_VETO_BAN, "mp/q4dm1", -1,
		vetoRevision).reason == MP_SERIES_REASON_WRONG_VETO_SIDE);
	CHECK(series.GetRevision() == vetoRevision);

	CHECK(AppliedOnce(series.ApplyVeto(0, MP_SERIES_VETO_BAN,
		"mp/q4dm1", -1, series.GetRevision())));
	CHECK(AppliedOnce(series.ApplyVeto(1, MP_SERIES_VETO_BAN,
		"mp/q4dm2", -1, series.GetRevision())));
	CHECK(AppliedOnce(series.ApplyVeto(0, MP_SERIES_VETO_PICK,
		"mp/q4dm3", -1, series.GetRevision())));
	CHECK(series.ValidateInvariants());
	const uint64_t beforeWrongSideMap = series.GetRevision();
	CHECK(series.ApplyVeto(1, MP_SERIES_VETO_SIDE, "mp/q4dm4", 0,
		beforeWrongSideMap).reason == MP_SERIES_REASON_MAP_NOT_SELECTED);
	CHECK(series.GetRevision() == beforeWrongSideMap);
	CHECK(series.ApplyVeto(1, MP_SERIES_VETO_SIDE, "mp/q4dm3", -1,
		beforeWrongSideMap).reason == MP_SERIES_REASON_INVALID_ARGUMENT);
	CHECK(series.GetRevision() == beforeWrongSideMap);
	CHECK(AppliedOnce(series.ApplyVeto(1, MP_SERIES_VETO_SIDE,
		"mp/q4dm3", 0, series.GetRevision())));
	CHECK(AppliedOnce(series.ApplyVeto(1, MP_SERIES_VETO_PICK,
		"mp/q4dm4", -1, series.GetRevision())));
	CHECK(AppliedOnce(series.ApplyVeto(0, MP_SERIES_VETO_SIDE,
		"mp/q4dm4", 1, series.GetRevision())));
	CHECK(AppliedOnce(series.ApplyVeto(0, MP_SERIES_VETO_DECIDER,
		"mp/q4dm5", -1, series.GetRevision())));
	CHECK(series.GetState() == MP_SERIES_VETO);
	CHECK(AppliedOnce(series.ApplyVeto(1, MP_SERIES_VETO_SIDE,
		"mp/q4dm5", 0, series.GetRevision())));
	CHECK(series.GetState() == MP_SERIES_READY);
	CHECK(series.GetSelectedMapCount() == 3);
	CHECK(strcmp(series.GetNextMapToken(), "mp/q4dm3") == 0);
	CHECK(series.GetSelectedMap(2)->decider);
	CHECK(series.GetSelectedMap(2)->hasStartingGameSide);
	CHECK(series.ValidateInvariants());

	CHECK(AppliedOnce(series.ReportMapLoadFailure("MP/Q4DM3", series.GetRevision())));
	CHECK(series.GetMapLoadFailureCount() == 1);
	CHECK(AppliedOnce(series.BeginMap("mp/q4dm3", series.GetRevision())));
	CHECK(AppliedOnce(series.CommitMapResult(MP_SERIES_MAP_DECIDED, 0, 12, 8,
		1001, 0x1111, series.GetRevision())));
	CHECK(AppliedOnce(series.AdvanceAfterMap(series.GetRevision())));
	CHECK(series.GetWins(0) == 1);
	CHECK(strcmp(series.GetNextMapToken(), "mp/q4dm4") == 0);

	CHECK(AppliedOnce(series.BeginMap("mp/q4dm4", series.GetRevision())));
	CHECK(AppliedOnce(series.CommitMapResult(MP_SERIES_MAP_ABORTED, -1, 0, 0,
		1002, 0x2222, series.GetRevision())));
	CHECK(AppliedOnce(series.AdvanceAfterMap(series.GetRevision())));
	CHECK(strcmp(series.GetNextMapToken(), "mp/q4dm4") == 0);
	CHECK(series.GetWins(0) == 1);

	CHECK(AppliedOnce(series.BeginMap("mp/q4dm4", series.GetRevision())));
	CHECK(AppliedOnce(series.CommitMapResult(MP_SERIES_MAP_FORFEIT, 0, 1, 0,
		1003, 0x3333, series.GetRevision())));
	CHECK(AppliedOnce(series.AdvanceAfterMap(series.GetRevision())));
	CHECK(series.GetState() == MP_SERIES_COMPLETE);
	CHECK(series.GetWins(0) == 2);
	CHECK(series.GetAttemptCount() == 3);
	CHECK(series.ValidateInvariants());
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_series_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-series-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_series_contract.cpp"
        executable = temp_dir / (
            "match_series_contract.exe" if compiler.lower().endswith(".exe") else "match_series_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-DMP_MATCH_SERIES_STANDALONE_TEST",
                f"-I{ROOT / 'src'}",
                str(harness),
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
                "standalone match-series contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"match-series invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_series_contract: PASS")


if __name__ == "__main__":
    main()
