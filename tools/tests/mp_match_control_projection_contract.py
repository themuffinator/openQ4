#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and native contracts for Match Control presentation projection."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/mpgame/mp/match/MatchControlProjection.h"
SOURCE = ROOT / "src/mpgame/mp/match/MatchControlProjection.cpp"

SCALAR_STATES = {
    "match_surface_available",
    "match_phase",
    "match_status_lines",
    "match_ready_action",
    "match_team_lock_action",
    "match_action_side_label",
    "match_action_side_0_label",
    "match_action_side_1_label",
    "match_action_side_visible",
    "match_action_side_0_enabled",
    "match_action_side_1_enabled",
    "match_action_side_0_selected",
    "match_action_side_1_selected",
    "match_broadcaster_control_visible",
    "match_broadcaster_action",
    "match_referee_authenticated",
    "match_global_proposal",
    "match_side_proposal",
    "match_proposal_scope_choice",
    "match_rules_summary",
    "match_staged_summary",
    "match_series_summary",
    "match_evidence_summary",
    "match_result_message",
    "match_role_choice",
    "match_rule_value",
    "match_series_profile_choice",
}

LIST_STATES = {
    "match_team_rows",
    "match_replacement_rows",
    "match_proposal_rows",
    "match_profile_rows",
    "match_rule_rows",
    "match_series_map_rows",
    "match_series_history_rows",
    "match_evidence_rows",
}

CONTEXT_STATES = {
    "match_context_visible",
    "match_context_phase",
    "match_context_role",
    "match_context_pause",
    "match_context_readiness",
    "match_context_timeouts",
    "match_context_proposal",
    "match_context_series",
    "match_context_items",
}

AVAILABILITY_PREFIXES = [
    "ready_set",
    "team_ready_set",
    "force_ready",
    "team_join",
    "team_lock_set",
    "queue_join",
    "queue_defer",
    "queue_leave",
    "roster_leave",
    "timeout_request",
    "tech_pause_request",
    "resume_request",
    "ref_authenticate",
    "ref_logout",
    "rules_select_profile",
    "rules_stage_field",
    "rules_commit",
    "rules_discard",
    "proposal_create",
    "proposal_cast",
    "proposal_cancel",
    "roster_invite",
    "roster_accept",
    "roster_remove",
    "roster_substitute",
    "role_assign",
    "broadcaster_set",
    "series_stage_profile",
    "series_start",
    "series_cancel",
    "series_advance",
    "veto_select",
    "forfeit",
    "abort",
    "participant_remove",
    "series_contestant_bind",
]


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for state in sorted(SCALAR_STATES | LIST_STATES | CONTEXT_STATES):
        require(combined, f'"{state}"', "complete projection state set")

    registry = re.findall(
        r'\{\s*"([a-z0-9_]+)"\s*,\s*MP_MATCH_OP_[A-Z0-9_]+\s*\}', source
    )
    if registry != AVAILABILITY_PREFIXES:
        raise AssertionError(
            "operation availability registry drifted:\n"
            f"expected {AVAILABILITY_PREFIXES}\nactual   {registry}"
        )

    for token in (
        "MPMatchViewValidate( view, NULL )",
        "value.fieldId == MP_RULE_MANAGED_MATCH",
        "!ProjectionIsManagedMatch( acceptedView )",
        "model.SessionId() != state.sessionId",
        "model.ViewRevision() != state.viewRevision",
        "recipient.bindingGeneration != state.recipient.bindingGeneration",
        "context.localOperatorVisible ? 1 : 0",
        "context.resolveParticipantText",
        "context.resolveMapText",
        "MPMatchControlSanitizeDisplayText",
        "DeleteFirstUnusedListItem",
        "availability->reason == MP_MATCH_PROTOCOL_REASON_OK",
        "model->OperationContextAccepted( state.opcode )",
        "MPMatchControlLocalizationKey",
        "MPMatchControlProtocolReasonKey",
        "MPMatchControlReadinessBlockerKey",
        "MPMatchControlRuleFieldKey",
        "MPMatchControlErrorReasonKey",
        "MP_MATCH_OP_ROSTER_LEAVE",
        "model->CanChooseActionSide( 0 )",
        "model->CanChooseActionSide( 1 )",
        "model->ActionSideUsesCompetitionLabels()",
        "BuildRecipientText( value, sizeof( value ), acceptedView )",
        "BuildItemTimingText( value, sizeof( value ), acceptedView )",
        "ItemTimingOrdinal( token, \"large_armor\", ordinal )",
        "text.AppendUInt( static_cast<unsigned int>( ordinal ) )",
        "view.publicState.clocks.matchTimeMsec",
        "Adapter tokens are machine identifiers and are never rendered directly",
        "initializeChoices",
        "Cross-session results are rejected",
        "single visibility gate is deliberately written last",
    ):
        require(combined, token, "fail-closed projection boundary")

    if "text.Append( timing.token )" in source:
        raise AssertionError("raw item-timing machine token reaches presentation")

    # Presentation may write typed selection indices, but it must not read or
    # parse GUI/display state, refresh the GUI mid-batch, or expose credentials.
    for forbidden in (
        "StateChanged(",
        "GetStateString(",
        "GetStateInt(",
        "atoi(",
        "sscanf(",
        "strtok(",
        "match_referee_credential",
        ".key )",
        "row.key",
    ):
        if forbidden in source:
            raise AssertionError(
                f"projection contains forbidden presentation dependency {forbidden!r}"
            )

    # Map tokens may cross only the explicit resolver callback and must never
    # be assigned directly to GUI state.
    if re.search(r"SetStateString\([^;]*(?:mapToken|nextMap)", source, re.S):
        raise AssertionError("stable map token is exposed directly as display text")

    # Normal refreshes cannot overwrite caller-owned input/choice states.
    choice_writes = {
        state: source.count(f'SetStateString( "{state}"')
        for state in (
            "match_role_choice",
            "match_proposal_scope_choice",
            "match_series_profile_choice",
            "match_rule_value",
        )
    }
    if any(count != 1 for count in choice_writes.values()):
        raise AssertionError(f"choice states are written outside one-time initialization: {choice_writes}")

    listed = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if listed.returncode != 0:
        raise AssertionError("could not inspect MP source discovery:\n" + listed.stderr)
    if "mpgame/mp/match/MatchControlProjection.cpp" not in listed.stdout.splitlines():
        raise AssertionError("MatchControlProjection.cpp is not compiled into the MP game module")


HARNESS = r'''
#include <string.h>

#define MP_MATCH_CONTROL_PROJECTION_SANITIZER_STANDALONE_TEST 1
#include "mpgame/mp/match/MatchControlProjection.cpp"

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

int main(void) {
    char output[64];
    CHECK(MPMatchControlSanitizeDisplayText(NULL, output, sizeof(output)) == 0);
    CHECK(output[0] == '\0');
    CHECK(MPMatchControlSanitizeDisplayText("  Alice\t\r\n Bob  ", output,
        sizeof(output)) == 9);
    CHECK(strcmp(output, "Alice Bob") == 0);

    const char validUtf8[] = { 'R', (char)0xc3, (char)0xa9, 'n', (char)0xc3,
        (char)0xa9, 0 };
    CHECK(MPMatchControlSanitizeDisplayText(validUtf8, output, sizeof(output)) == 6);
    CHECK(memcmp(output, validUtf8, sizeof(validUtf8)) == 0);

    const char invalidUtf8[] = { 'A', (char)0xc0, (char)0xaf, 'B', 0 };
    CHECK(MPMatchControlSanitizeDisplayText(invalidUtf8, output, sizeof(output)) == 4);
    CHECK(strcmp(output, "A??B") == 0);

    const char euro[] = { 'a', 'b', (char)0xe2, (char)0x82, (char)0xac, 0 };
    char shortOutput[4];
    CHECK(MPMatchControlSanitizeDisplayText(euro, shortOutput,
        sizeof(shortOutput)) == 2);
    CHECK(strcmp(shortOutput, "ab") == 0);

    const char controls[] = { 'a', 1, 2, '\t', 'b', 0 };
    CHECK(MPMatchControlSanitizeDisplayText(controls, output, sizeof(output)) == 3);
    CHECK(strcmp(output, "a b") == 0);
    CHECK(MPMatchControlSanitizeDisplayText(
        "^1Red^0 ^c683Marine^i123Icon^rName", output,
        sizeof(output)) == 18);
    CHECK(strcmp(output, "Red MarineIconName") == 0);
    CHECK(MPMatchControlSanitizeDisplayText("^^literal ^xkept", output,
        sizeof(output)) == 16);
    CHECK(strcmp(output, "^^literal ^xkept") == 0);
    CHECK(MPMatchControlSanitizeDisplayText("abc", NULL, 0) == 0);
    return 0;
}
'''


def production_definition(source: str, signature: str, *, declaration: bool = False) -> str:
    """Extract a complete production definition, keeping its body under test."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + (";" if declaration else "") + "\n"


def status_harness(source: str) -> str:
    # Real wire/view/role types and localization mappings keep this regression
    # coupled to the accepted projection format. Only engine string formatting
    # and the localization service are replaced; all presentation bodies run.
    prefix = r'''
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <string>
#define MP_MATCH_CONTROL_PROJECTION_SANITIZER_STANDALONE_TEST 1
#include "mpgame/mp/match/MatchControlProjection.cpp"
#include "mpgame/mp/match/MatchControlProjection.h"
#define MP_MATCH_CONTROL_LOCALIZATION_STANDALONE_TEST 1
#include "mpgame/mp/match/MatchControlLocalization.cpp"
class idStr {
public:
    static int snPrintf(char *out, int capacity, const char *format, ...) {
        va_list args;
        va_start(args, format);
        const int result = vsnprintf(out, capacity, format, args);
        va_end(args);
        return result;
    }
};
struct LocalizationService {
    int language = 0;
    const char *GetLocalizedString(const char *key) {
@RESULT_LOCALIZATION@
        return key;
    }
};
static LocalizationService localizationService;
static LocalizationService *common = &localizationService;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "status projection invariant failed at line %d: %s\n", \
        __LINE__, #condition); return 1; } } while (0)
'''
    definitions = [production_definition(source, "class mpProjectionText", declaration=True)]
    for relative, signature in (
        ("MatchView.cpp", "mpMatchViewPublicRoleMask_t MPMatchViewRoleBit("),
        ("MatchSession.cpp", "mpMatchReadinessBlockerMask_t MPMatchReadinessBlockerBit("),
    ):
        definitions.append(production_definition(read(SOURCE.parent / relative), signature))
    for signature in (
        "static const char *Localized(",
        "static void AppendSeparator(",
        "static const char *SideKey(",
        "static const char *MatchSideKey(",
        "static unsigned long long ProjectionNow(",
        "static void AppendCountdown(",
        "static void AppendPublicRoles(",
        "static void BuildPhaseText(",
        "static void BuildPauseText(",
        "static void BuildReadinessText(",
        "static void BuildTimeoutText(",
        "static void BuildRecipientText(",
        "static void BuildLastResultText(",
        "static void BuildStatusLines(",
    ):
        definitions.append(production_definition(source, signature))
    checks = r'''
int main() {
    static_assert(STATE_COUNT == 7, "review phase coverage for appended states");
    mpSessionView view = {};
    mpMatchControlProjectionContext_t context = {};
    view.publicState.readiness.readyCount = 1;
    view.publicState.readiness.eligibleCount = 2;
    view.publicState.readiness.blockers =
        MPMatchReadinessBlockerBit(MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY) |
        MPMatchReadinessBlockerBit(MP_MATCH_BLOCKER_TEAM_NOT_READY);
    view.publicState.rosterSummaryCount = 2;
    view.publicState.rosterSummaries[0].side = 0;
    view.publicState.rosterSummaries[0].teamReady = true;
    view.publicState.rosterSummaries[1].side = 1;
    char readiness[1024], recipient[1024], status[4096];
    int cases = 0;
    for (int phase = INACTIVE; phase < STATE_COUNT; ++phase) {
        view.publicState.lifecycle.phase = static_cast<mpGameState_t>(phase);
        const bool preparing = phase == WARMUP || phase == COUNTDOWN;
        for (int role = MP_MATCH_VIEW_ROLE_NONE; role < MP_MATCH_VIEW_ROLE_COUNT; ++role) {
            mpMatchViewRecipient_t &who = view.publicState.recipient;
            who.publicRoleMask = MPMatchViewRoleBit(static_cast<mpMatchViewPublicRole_t>(role));
            who.active = role == MP_MATCH_VIEW_ROLE_PLAYER || role == MP_MATCH_VIEW_ROLE_CAPTAIN;
            who.side = who.active || role == MP_MATCH_VIEW_ROLE_COACH ? 0 : MP_MATCH_VIEW_SIDE_NONE;
            for (int ready = 0; ready < 2; ++ready) {
                who.ready = ready != 0;
                for (int queue = MP_MATCH_VIEW_QUEUE_NONE; queue < MP_MATCH_VIEW_QUEUE_STATE_COUNT; ++queue) {
                    who.queueState = static_cast<mpMatchViewQueueState_t>(queue);
                    who.hasQueuePosition = queue == MP_MATCH_VIEW_QUEUE_WAITING;
                    who.queuePosition = 3;
                    // Reuse dirty buffers: leaving preparation must clear an
                    // earlier blocker instead of retaining a previous view.
                    std::snprintf(readiness, sizeof(readiness), "old readiness blockers");
                    BuildReadinessText(readiness, sizeof(readiness), view);
                    BuildRecipientText(recipient, sizeof(recipient), view);
                    BuildStatusLines(status, sizeof(status), view, context);
                    if (preparing) {
                        CHECK(std::strstr(readiness, "#str_41708: 1/2") != NULL);
                        CHECK(std::strstr(readiness, "#str_41715: 1/2") != NULL);
                        CHECK(std::strstr(readiness, MPMatchControlReadinessBlockerKey(
                            MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY)) != NULL);
                        CHECK(std::strstr(readiness, MPMatchControlReadinessBlockerKey(
                            MP_MATCH_BLOCKER_TEAM_NOT_READY)) != NULL);
                        CHECK(std::strstr(status, readiness) != NULL);
                    } else {
                        CHECK(readiness[0] == '\0');
                        CHECK(std::strstr(status, "#str_41708") == NULL);
                        CHECK(std::strstr(status, MPMatchControlReadinessBlockerKey(
                            MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY)) == NULL);
                    }
                    const bool personalReady = preparing && who.active;
                    CHECK((std::strstr(recipient, "#str_41711") != NULL) == (personalReady && who.ready));
                    CHECK((std::strstr(recipient, "#str_41712") != NULL) == (personalReady && !who.ready));
                    CHECK(std::strstr(recipient, MPMatchControlPublicRoleKey(
                        static_cast<mpMatchViewPublicRole_t>(role))) != NULL);
                    if (queue != MP_MATCH_VIEW_QUEUE_NONE) {
                        std::string label = MPMatchControlQueueStateKey(who.queueState);
                        if (who.hasQueuePosition) { label += " 3"; }
                        CHECK(std::strstr(recipient, label.c_str()) != NULL);
                        CHECK(std::strstr(status, label.c_str()) != NULL);
                    }
                    const std::string lines(status);
                    CHECK(std::count(lines.begin(), lines.end(), '\n') == (preparing ? 4 : 3));
                    CHECK(lines.find("\n\n") == std::string::npos);
                    CHECK(std::strstr(status, recipient) != NULL);
                    CHECK(std::strstr(status, "#str_41778") != NULL);
                    ++cases;
                }
            }
        }
    }
    // Empty/tiny destinations remain bounded on both sides of the phase gate.
    for (int phase = WARMUP; phase <= GAMEON; ++phase) {
        view.publicState.lifecycle.phase = static_cast<mpGameState_t>(phase);
        char bounded[] = { 'L', 'x', 'R' };
        BuildReadinessText(bounded + 1, 0, view);
        CHECK(bounded[1] == 'x');
        BuildReadinessText(bounded + 1, 1, view);
        CHECK(bounded[0] == 'L' && bounded[1] == '\0' && bounded[2] == 'R');
    }
@RESULT_CASES@
    std::printf("status projection: %d phase/role/readiness/queue cases passed\n", cases);
    return 0;
}
'''
    # This checkout is the engine since the game sources moved into it; a
    # sibling named openQ4 is only where a companion checkout used to find it.
    engine_root = Path(os.environ.get("OPENQ4_ENGINE_REPO",
        ROOT if (ROOT / "src/framework").is_dir() else ROOT.parent / "openQ4"))
    strings_root = engine_root / "content/baseoq4/pak0/strings"
    languages = ("english", "french", "italian", "spanish", "polish", "russian")
    keys = ("#str_42873", "#str_42870", "#str_42871", "#str_201012",
            "#str_201013", "#str_41313", "#str_41315")
    translations = []
    expected = []
    frozen_name = "Frozen \u2603 %s Winner"
    for language in languages:
        entries = dict(re.findall(r'"(#str_\d+)"\s+"([^"\r\n]*)"',
            read(strings_root / f"{language}_guis.lang") + "\n" +
            read(strings_root / f"{language}_openq4.lang")))
        assert entries[keys[0]].count("%s") == 1
        translations.append([entries[key] for key in keys])
        values = ["", entries[keys[1]], entries[keys[2]], entries[keys[3]], entries[keys[4]],
                  entries[keys[3]] + " | " + entries[keys[6]],
                  entries[keys[4]] + " | " + entries[keys[6]],
                  entries[keys[5]] % frozen_name,
                  (entries[keys[5]] % frozen_name) + " | " + entries[keys[6]]]
        expected.append([entries[keys[0]] % value if value else "" for value in values])
    literal = lambda value: json.dumps(value, ensure_ascii=False)
    localization = "static const char *keys[] = {" + ",".join(map(literal, keys)) + "};\n"
    localization += "static const char *values[6][7] = {" + ",".join(
        "{" + ",".join(map(literal, row)) + "}" for row in translations) + "};\n"
    localization += "for (int i = 0; i < 7; ++i) if (std::strcmp(key, keys[i]) == 0) return values[language][i];"
    result_cases = r'''
    const char *expected[6][9] = @EXPECTED@;
    int resultCases = 0;
    for (int language = 0; language < 6; ++language) {
        localizationService.language = language;
        for (int phase : {GAMEREVIEW, NEXTGAME, WARMUP}) for (int scenario = 0; scenario < 9; ++scenario) {
            auto &result = view.publicState.terminalResult;
            result = {}; result.winnerSide = MP_MATCH_VIEW_SIDE_NONE;
            result.outcome = scenario == 0 ? MP_MATCH_VIEW_RESULT_NONE : scenario == 1 ? MP_MATCH_VIEW_RESULT_ABORTED :
                scenario == 2 ? MP_MATCH_VIEW_RESULT_DRAW : scenario == 5 || scenario == 6 || scenario == 8 ?
                MP_MATCH_VIEW_RESULT_FORFEIT : MP_MATCH_VIEW_RESULT_DECIDED;
            if (scenario >= 3 && scenario <= 6) result.winnerSide = scenario == 3 || scenario == 5 ? 0 : 1;
            if (scenario >= 7) {
                result.winnerParticipantId = 0xffffffffu; // Departed winner, no live roster lookup.
                std::snprintf(result.winnerName, sizeof(result.winnerName), "%s", "Frozen \xE2\x98\x83 %s Winner");
                result.winnerNameLength = static_cast<unsigned char>(std::strlen(result.winnerName));
            }
            view.publicState.lifecycle.phase = static_cast<mpGameState_t>(phase);
            char phaseText[384]; BuildPhaseText(phaseText, sizeof(phaseText), view);
            char resultText[768]; std::snprintf(resultText, sizeof(resultText), "old result");
            BuildLastResultText(resultText, sizeof(resultText), result);
            CHECK(std::strcmp(resultText, expected[language][scenario]) == 0);
            BuildStatusLines(status, sizeof(status), view, context);
            const std::string lines(status);
            if (scenario != 0) {
                CHECK(lines.find(std::string(phaseText) + "\n" + resultText + "\n") == 0);
            } else {
                CHECK(std::strstr(status, "old result") == NULL);
            }
            CHECK(std::count(lines.begin(), lines.end(), '\n') ==
                (phase == WARMUP ? 4 : 3) + (scenario != 0 ? 1 : 0));
            CHECK(lines.find("\n\n") == std::string::npos);
            char bounded[] = {'L','x','R'};
            BuildLastResultText(bounded + 1, 0, result); CHECK(bounded[1] == 'x');
            BuildLastResultText(bounded + 1, 1, result);
            CHECK(bounded[0] == 'L' && bounded[1] == '\0' && bounded[2] == 'R');
            ++resultCases;
        }
    }
    std::printf("last result projection: %d six-language frozen outcome/phase cases passed\n", resultCases);
'''
    result_cases = result_cases.replace("@EXPECTED@", "{" + ",".join(
        "{" + ",".join(map(literal, row)) + "}" for row in expected) + "}")
    # Raw C++ harness text above must contain escapes, not literal backslashes
    # in the final player name; the name itself deliberately includes %s.
    result_cases = result_cases.replace(r"\\x", r"\x").replace(r"\\n", r"\n").replace(r"\\0", r"\0")
    return prefix.replace("@RESULT_LOCALIZATION@", localization) + "\n".join(definitions) + checks.replace("@RESULT_CASES@", result_cases)


def native_contracts(source: str) -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_control_projection_contract: native checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-control-projection-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_control_projection_contract.cpp"
        executable = temp_dir / (
            "match_control_projection_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_control_projection_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1" else []
        compiled = subprocess.run(
            [
                compiler,
                *sanitizers,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'src'}",
                str(harness),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone projection sanitizer did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "projection sanitizer invariant failed at harness line "
                f"{ran.returncode}:\n{ran.stdout}{ran.stderr}"
            )
        harness.write_text(status_harness(source), encoding="utf-8")
        compiled = subprocess.run(
            [compiler, *sanitizers, "-std=c++17", "-Wall", "-Wextra", "-Werror",
             f"-I{ROOT / 'src'}", str(harness), "-o", str(executable)],
            cwd=ROOT, text=True, capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError("production status projection did not compile:\n" +
                                 compiled.stdout + compiled.stderr)
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError("production status projection failed:\n" + ran.stdout + ran.stderr)
        print(ran.stdout.strip())


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    native_contracts(source)
    print("mp_match_control_projection_contract: PASS")


if __name__ == "__main__":
    main()
