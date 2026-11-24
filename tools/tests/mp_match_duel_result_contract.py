#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Export actual managed Duel forfeit results with unambiguous participant identity."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body
from mp_match_duel_queue_contract import HARNESS as QUEUE_HARNESS, MATCH, ROOT
from mp_match_view_contract import HARNESS as VIEW_HARNESS
from mp_match_ranking_contract import function


EVIDENCE_ADAPTER = r'''
    idMultiplayerGame() { ClearMatchTerminalResult(); }
    mpCompetitiveRules profileRules;
    uint64_t matchTerminalResultSessionId = 0;
    mpMatchViewTerminalResult_t matchTerminalResult;
    mpEvidenceMapResult matchTerminalEvidenceResult;
    mpMatchEvidence matchEvidence;
    bool matchEvidenceFinalized = false, matchEvidenceFinalizationPending = false;
    uint64_t matchSeriesId = 0;
    int matchSeriesContestantSlot[MP_SERIES_SIDE_COUNT] = {0, 1};
    uint64_t matchSeriesContestantConnection[MP_SERIES_SIDE_COUNT] = {};
    struct Score { int fragCount = 0; } playerState[MAX_CLIENTS];
    int teamScore[TEAM_MAX] = {};
    int TeamLeader() const { return teamScore[0] == teamScore[1] ? -1 : teamScore[0] > teamScore[1] ? 0 : 1; }
    idPlayer leader{0};
    idPlayer *GetRankLeader() { return playerState[0].fragCount == playerState[1].fragCount ? nullptr :
        (leader.entityNumber = playerState[0].fragCount > playerState[1].fragCount ? 0 : 1, &leader); }
    mpEvidenceActorRef MatchEvidenceActor(mpParticipantId actor) const {
        return actor.IsValid() ? MPEvidenceParticipantActor(actor.SequencePart()) : MPEvidenceSystemActor();
    }
    mpEvidenceCommittedStamp BuildMatchEvidenceStamp() const {
        return {matchSession.GetSessionRevision(), 0, 1000};
    }
    void RecordMatchEvidenceResult(mpMatchTransitionReason_t reason,
                                   mpParticipantId authorizer, int forfeitingSide) {
@EVIDENCE@
    }
    mpEvidenceMapResult BuildMatchTerminalEvidenceResult(mpMatchTransitionReason_t reason,
                                                         mpParticipantId authorizer, int forfeitingSide) {
@CALCULATE@
    }
    void ClearMatchTerminalResult() {
@CLEAR_RESULT@
    }
    void FreezeMatchTerminalResult(const mpEvidenceMapResult &result) {
@FREEZE@
    }
'''

CASES = r'''
static void ForfeitReview(Fixture &f) {
    f.Countdown();
    f.Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
    f.Transition(GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT);
}
static void InitializeEvidence(Fixture &f) {
    mpEvidenceMetadataInput metadata = {77, 0, 42, GAME_DUEL, "duel-contract", "mp/q4dm1", "Duel"};
    assert(f.mp.matchEvidence.Reset(metadata));
}
static const mpEvidenceMapResult &OnlyResult(Fixture &f) {
    assert(f.mp.matchEvidence.GetEventCount() == 1);
    const auto *event = f.mp.matchEvidence.GetEvent(0);
    assert(event && event->kind == MP_EVIDENCE_EVENT_MAP_RESULT);
    assert(!f.mp.matchEvidenceFinalizationPending);
    return event->data.result;
}
static void Export(Fixture &f, int loser) {
    for (int slot = 0; slot < 2; ++slot) {
        mpEvidenceParticipantStatsInput stats = {};
        stats.participantSequence = f.ids[slot].SequencePart();
        stats.side = MP_MATCH_SIDE_NONE;
        stats.displayName = slot == loser ? "Leading forfeiter" : "Winner";
        stats.score = f.mp.playerState[slot].fragCount;
        assert(f.mp.matchEvidence.RecordParticipantFinalStats(f.mp.BuildMatchEvidenceStamp(), stats).code ==
            MP_EVIDENCE_WRITE_ACCEPTED);
    }
    const auto measure = f.mp.matchEvidence.SerializeCanonicalJson(nullptr, 0);
    std::vector<char> buffer(measure.requiredCapacity);
    assert(f.mp.matchEvidence.SerializeCanonicalJson(buffer.data(), static_cast<int>(buffer.size())).Succeeded());
    std::puts(buffer.data());
}
static void ExplicitAndSelfForfeit() {
    for (int loser = 0; loser < 2; ++loser) {
        for (int authority = 0; authority < 4; ++authority) {
            Fixture f; ForfeitReview(f); InitializeEvidence(f);
            // The forfeiting player leads. Neither score ordering nor a different
            // operation authorizer may override the committed side target.
            f.mp.playerState[loser].fragCount = 9;
            f.mp.playerState[1-loser].fragCount = -2;
            mpParticipantId actor = authority == 0 ? mpParticipantId::Invalid() :
                authority == 1 ? f.ids[1-loser] : authority == 2 ? f.ids[2] : f.ids[loser];
            if (authority == 2) {
                assert(!f.mp.matchSession.SetParticipantRoles(actor,
                    MPMatchRoleBit(MP_MATCH_ROLE_REFEREE), f.mp.matchSession.GetSessionRevision()).WasRejected());
            }
            const int target = authority == 3 ? MP_MATCH_SIDE_NONE : loser;
            mpParticipantId forfeiter, winner;
            assert(f.mp.ResolveManagedDuelForfeitParticipants(target, actor, forfeiter, winner));
            assert(forfeiter == f.ids[loser] && winner == f.ids[1-loser]);
            f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, actor, target);
            const auto &result = OnlyResult(f);
            assert(result.outcome == MP_EVIDENCE_RESULT_FORFEIT);
            assert(result.winnerSide == 1-loser && result.winnerParticipant == f.ids[1-loser].SequencePart());
            assert(result.sideScore[loser] == 9 && result.sideScore[1-loser] == -2);
            assert(f.mp.playerState[loser].fragCount == 9 && f.mp.playerState[1-loser].fragCount == -2);
            const auto revision = f.mp.matchEvidence.GetEvidenceRevision();
            f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, f.ids[1-loser], 1-loser);
            assert(f.mp.matchEvidence.GetEvidenceRevision() == revision);
            assert(OnlyResult(f).winnerParticipant == f.ids[1-loser].SequencePart());
            Export(f, loser);
        }
    }
}
static void UnresolvedIdentityFailsClosed() {
    for (int scenario = 0; scenario < 10; ++scenario) {
        Fixture f; ForfeitReview(f); InitializeEvidence(f);
        mpParticipantId actor = f.ids[2];
        int target = MP_MATCH_SIDE_NONE;
        switch (scenario) {
            case 0: break; // A queued player has no forfeitable competition side.
            case 1: assert(!f.mp.matchSession.SetParticipantRoles(actor,
                MPMatchRoleBit(MP_MATCH_ROLE_REFEREE), f.mp.matchSession.GetSessionRevision()).WasRejected()); break;
            case 2: actor = mpParticipantId::Invalid(); break; // Operator needs explicit side.
            case 3: target = 2; break;
            case 4: target = -2; break;
            case 5: target = 0; f.mp.matchConnectionId[1] = 0; break;
            case 6: {
                target = 0; uint32_t generation = 0;
                assert(f.mp.matchSession.GetSlotGeneration(1, generation));
                assert(!f.mp.matchSession.UnbindParticipant(1, generation,
                    f.mp.matchSession.GetSessionRevision()).WasRejected());
                break;
            }
            case 7: target = 0; assert(!f.mp.matchSession.SetParticipantActive(f.ids[1], false,
                f.mp.matchSession.GetSessionRevision()).WasRejected()); break;
            case 8: target = 0; f.mp.matchSeries.state = MP_SERIES_MAP_ACTIVE; break; // Missing series bindings.
            case 9: target = 0; f.Transition(NEXTGAME, MP_MATCH_TRANSITION_REVIEW_COMPLETE); break;
        }
        mpParticipantId forfeiter = f.ids[0], winner = f.ids[1];
        assert(!f.mp.ResolveManagedDuelForfeitParticipants(target, actor, forfeiter, winner));
        assert(!forfeiter.IsValid() && !winner.IsValid());
        f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, actor, target);
        const auto &result = OnlyResult(f);
        assert(result.outcome == MP_EVIDENCE_RESULT_ABORTED);
        assert(result.winnerParticipant == 0 && result.winnerSide == -1);
    }
}
static void SeriesIdentityAndTerminalMappings() {
    for (int seriesState : {MP_SERIES_MAP_ACTIVE, MP_SERIES_COMPLETE, MP_SERIES_CANCELLED}) {
        Fixture f; ForfeitReview(f); InitializeEvidence(f);
        f.mp.matchSeries.state = static_cast<mpSeriesState_t>(seriesState);
        f.mp.matchSeriesId = 123;
        // A live series retains reversed sides. Terminal series state must no
        // longer contaminate the next standalone result's identity mapping.
        for (int slot = 0; slot < 2; ++slot) {
            f.mp.matchSeriesCompetitionConnection[slot] = f.mp.matchConnectionId[slot];
            f.mp.matchSeriesCompetitionSide[slot] = 1-slot;
            f.mp.matchSeriesContestantSlot[1-slot] = slot;
            f.mp.matchSeriesContestantConnection[1-slot] = f.mp.matchConnectionId[slot];
        }
        f.mp.playerState[0].fragCount = 12; f.mp.playerState[1].fragCount = -1;
        f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, f.ids[0], MP_MATCH_SIDE_NONE);
        const auto &result = OnlyResult(f);
        const int winnerSide = seriesState == MP_SERIES_MAP_ACTIVE ? 0 : 1;
        assert(result.outcome == MP_EVIDENCE_RESULT_FORFEIT);
        assert(result.winnerParticipant == f.ids[1].SequencePart() && result.winnerSide == winnerSide);
        assert(result.sideScore[winnerSide] == -1 && result.sideScore[1-winnerSide] == 12);
    }
}
static mpSessionView ProjectRoundTrip(Fixture &f) {
    mpSessionView view; view.Clear();
    auto &publicState = view.publicState;
    publicState.sessionId = f.mp.matchSession.GetSessionId();
    publicState.sessionRevision = f.mp.matchSession.GetSessionRevision();
    publicState.viewRevision = publicState.controlRevision = publicState.sessionRevision;
    publicState.lifecycle.phase = f.mp.matchSession.GetPhase();
    f.mp.ProjectTerminalResult(publicState);
    const auto *recipient = f.mp.matchSession.FindParticipant(f.ids[2]); assert(recipient);
    publicState.recipient.participantId = recipient->id.SequencePart();
    publicState.recipient.slot = recipient->slot;
    publicState.recipient.bindingGeneration = recipient->slotGeneration;
    publicState.participantSummaryCount = 1;
    auto &row = publicState.participantSummaries[0];
    row.participantId = recipient->id.SequencePart(); row.slot = recipient->slot;
    row.connected = row.human = true;
    byte data[MP_MATCH_VIEW_MAX_MESSAGE_BYTES]; idBitMsg write;
    write.Init(data, sizeof(data)); write.BeginWriting();
    mpMatchViewError_t error;
    if (!MPMatchViewEncode(write, view, &error)) {
        fprintf(stderr, "projection error=%d field=%d\n", error.reason, error.fieldId); assert(false);
    }
    idBitMsg read; read.Init(data, write.GetSize()); read.SetSize(write.GetSize()); read.BeginReading();
    mpSessionView accepted; accepted.Clear();
    assert(MPMatchViewDecode(read, accepted, &error));
    return accepted;
}
static void FrozenIndependentPublicResult() {
    Fixture f;
    mpMatchRulesDraft profile; mpRuleValidationFailure_t profileFailure;
    assert(f.mp.profileRules.BeginDraftFromProfile(MP_MATCH_PROFILE_COMPETITIVE_DUEL,
        GAME_DUEL, profile, profileFailure));
    assert(f.mp.profileRules.Commit(profile, {}, MP_RULES_OPEN_FOR_COMMIT).Succeeded());
    f.mp.managed = f.mp.profileRules.Committed().GetBool(MP_RULE_MANAGED_MATCH);
    assert(f.mp.managed);
    ForfeitReview(f);
    gameLocal.userInfo[1].name = "Frozen \xE2\x98\x83\nWinner";
    f.mp.playerState[0].fragCount = 12; f.mp.playerState[1].fragCount = -1;
    f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, f.ids[0], 0);
    assert(!f.mp.matchEvidence.IsInitialized());
    const auto result = ProjectRoundTrip(f).publicState.terminalResult;
    assert(result.outcome == MP_MATCH_VIEW_RESULT_FORFEIT && result.winnerSide == -1);
    assert(result.winnerParticipantId == f.ids[1].SequencePart());
    assert(std::strcmp(result.winnerName, "Frozen \xE2\x98\x83_Winner") == 0);
    uint32_t generation = 0; assert(f.mp.matchSession.GetSlotGeneration(1, generation));
    assert(!f.mp.matchSession.UnbindParticipant(1, generation,
        f.mp.matchSession.GetSessionRevision()).WasRejected());
    f.mp.matchConnectionId[1] = 501;
    gameLocal.userInfo[1].name = "Replacement occupant";
    mpParticipantId replacement;
    assert(!f.mp.matchSession.BindParticipant(1, true, MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),
        f.mp.matchSession.GetSessionRevision(), replacement).WasRejected());
    assert(replacement != f.ids[1]);
    InitializeEvidence(f); // Delayed evidence initialization consumes the same frozen result.
    f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, f.ids[1], 1);
    assert(OnlyResult(f).winnerParticipant == result.winnerParticipantId);
    f.Transition(NEXTGAME, MP_MATCH_TRANSITION_REVIEW_COMPLETE);
    f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART);
    // The accepted casual profile changes the next match in this same warmup.
    // The prior winner remains frozen independently of current managed mode.
    const auto oldSession = f.mp.matchSession.GetSessionId();
    assert(f.mp.matchSession.GetPhase() == WARMUP);
    assert(f.mp.profileRules.BeginDraftFromProfile(MP_MATCH_PROFILE_CASUAL,
        GAME_DUEL, profile, profileFailure));
    assert(f.mp.profileRules.Commit(profile, {}, MP_RULES_OPEN_FOR_COMMIT).Succeeded());
    f.mp.managed = f.mp.profileRules.Committed().GetBool(MP_RULE_MANAGED_MATCH);
    assert(!f.mp.managed && f.mp.matchSession.GetSessionId() == oldSession);
    const auto retained = ProjectRoundTrip(f).publicState.terminalResult;
    assert(retained.resultRevision == result.resultRevision &&
        retained.winnerParticipantId == result.winnerParticipantId &&
        std::strcmp(retained.winnerName, result.winnerName) == 0);
    mpMatchViewPublicState_t discarded; discarded.Clear();
    discarded.sessionId = f.mp.matchSession.GetSessionId() + 1;
    discarded.lifecycle.phase = WARMUP;
    f.mp.ProjectTerminalResult(discarded);
    assert(discarded.terminalResult.outcome == MP_MATCH_VIEW_RESULT_NONE);
    for (auto phase : {INACTIVE, COUNTDOWN, GAMEON, SUDDENDEATH}) {
        discarded.Clear(); discarded.sessionId = f.mp.matchSession.GetSessionId();
        discarded.lifecycle.phase = phase; f.mp.ProjectTerminalResult(discarded);
        assert(discarded.terminalResult.outcome == MP_MATCH_VIEW_RESULT_NONE);
    }
    f.mp.matchTerminalResult.outcome = MP_MATCH_VIEW_RESULT_NONE;
    assert(ProjectRoundTrip(f).publicState.terminalResult.outcome == MP_MATCH_VIEW_RESULT_NONE);
    f.mp.matchTerminalResult = result;
    f.mp.ClearMatchTerminalResult();
    assert(ProjectRoundTrip(f).publicState.terminalResult.outcome == MP_MATCH_VIEW_RESULT_NONE);
    assert(f.mp.matchTerminalResultSessionId == 0);
}
static void TeamAndScoredOutcomes() {
    for (bool team : {false, true}) for (int scenario = 0; scenario < 6; ++scenario) {
        Fixture f; f.Countdown(); f.Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
        const bool abort = scenario < 2, forfeit = scenario >= 4;
        const auto reason = abort ? MP_MATCH_TRANSITION_MATCH_ABORTED :
            forfeit ? MP_MATCH_TRANSITION_FORFEIT : MP_MATCH_TRANSITION_LIMIT_REACHED;
        f.Transition(GAMEREVIEW, reason);
        if (team) { gameLocal.teamGame = true; gameLocal.gameType = GAME_DM; }
        const int leading = scenario % 2;
        f.mp.teamScore[leading] = f.mp.playerState[leading].fragCount = scenario == 0 || scenario == 2 ? 0 : 17;
        f.mp.RecordMatchEvidenceResult(reason, mpParticipantId::Invalid(), forfeit ? leading : -1);
        const auto result = ProjectRoundTrip(f).publicState.terminalResult;
        assert(result.reason == (abort ? MP_MATCH_VIEW_RESULT_REASON_MATCH_ABORTED :
            forfeit ? MP_MATCH_VIEW_RESULT_REASON_FORFEIT : MP_MATCH_VIEW_RESULT_REASON_LIMIT_REACHED));
        if (abort || scenario == 2) {
            assert(result.outcome == (abort ? MP_MATCH_VIEW_RESULT_ABORTED : MP_MATCH_VIEW_RESULT_DRAW));
            assert(result.winnerSide == -1 && result.winnerParticipantId == 0 && result.winnerNameLength == 0);
        } else {
            const int winner = forfeit ? 1-leading : leading;
            assert(result.outcome == (forfeit ? MP_MATCH_VIEW_RESULT_FORFEIT : MP_MATCH_VIEW_RESULT_DECIDED));
            assert(team ? result.winnerSide == winner && result.winnerParticipantId == 0 :
                result.winnerSide == -1 && result.winnerParticipantId == f.ids[winner].SequencePart());
        }
    }
    Fixture f; ForfeitReview(f); gameLocal.teamGame = true; gameLocal.gameType = GAME_DM;
    f.mp.matchSeriesId = 123; f.mp.matchSeries.state = MP_SERIES_MAP_ACTIVE;
    f.mp.matchSeriesGameSideForCompetition[0] = 1; f.mp.matchSeriesGameSideForCompetition[1] = 0;
    f.mp.teamScore[0] = 30; f.mp.teamScore[1] = 2;
    f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, mpParticipantId::Invalid(), 0);
    assert(f.mp.matchTerminalEvidenceResult.winnerSide == 0); // Competition side zero is Strogg.
    assert(ProjectRoundTrip(f).publicState.terminalResult.winnerSide == 1);
    f.mp.matchSeries.state = MP_SERIES_COMPLETE;
    f.mp.matchSeriesGameSideForCompetition[0] = 0;
    assert(ProjectRoundTrip(f).publicState.terminalResult.winnerSide == 1);
}
static void DepartedSeriesForfeit() {
    for (int lost = 0; lost < 2; ++lost) for (int invalid = 0; invalid < 4; ++invalid) {
        Fixture f; f.Countdown(); f.Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
        InitializeEvidence(f);
        f.mp.matchSeriesId = 123; f.mp.matchSeries.state = MP_SERIES_MAP_ACTIVE;
        for (int slot = 0; slot < 2; ++slot) {
            f.mp.matchSeriesContestantConnection[slot] = f.mp.matchConnectionId[slot];
            f.mp.matchSeriesCompetitionConnection[slot] = f.mp.matchConnectionId[slot];
            f.mp.matchSeriesCompetitionSide[slot] = slot;
        }
        uint32_t generation = 0; assert(f.mp.matchSession.GetSlotGeneration(lost, generation));
        assert(!f.mp.matchSession.UnbindParticipant(lost, generation,
            f.mp.matchSession.GetSessionRevision()).WasRejected());
        f.mp.matchConnectionId[lost] = 0;
        f.Transition(GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT);
        if (invalid == 1) f.mp.matchConnectionId[1-lost]++;
        if (invalid == 2) {
            mpParticipantId replacement;
            assert(!f.mp.matchSession.BindParticipant(lost, true, MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),
                f.mp.matchSession.GetSessionRevision(), replacement).WasRejected());
        }
        if (invalid == 3) assert(!f.mp.matchSession.SetParticipantActive(f.ids[1-lost], false,
            f.mp.matchSession.GetSessionRevision()).WasRejected());
        f.mp.RecordMatchEvidenceResult(MP_MATCH_TRANSITION_FORFEIT, mpParticipantId::Invalid(), lost);
        const auto &result = OnlyResult(f);
        assert(result.outcome == (invalid ? MP_EVIDENCE_RESULT_ABORTED : MP_EVIDENCE_RESULT_FORFEIT));
        assert(result.winnerParticipant == (invalid ? 0 : f.ids[1-lost].SequencePart()));
        assert(ProjectRoundTrip(f).publicState.terminalResult.outcome ==
            (invalid ? MP_MATCH_VIEW_RESULT_ABORTED : MP_MATCH_VIEW_RESULT_FORFEIT));
    }
}
int main() {
    ExplicitAndSelfForfeit(); UnresolvedIdentityFailsClosed(); SeriesIdentityAndTerminalMappings();
    FrozenIndependentPublicResult(); TeamAndScoredOutcomes(); DepartedSeriesForfeit();
}
'''


def build_harness() -> str:
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    for signature in ("idMultiplayerGame::idMultiplayerGame()", "void idMultiplayerGame::Clear()",
                      "bool idMultiplayerGame::BeginMatchSession"):
        assert "ClearMatchTerminalResult();" in function_body(source, signature), signature
    assert "MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 1 <= MAX_GAME_MESSAGE_SIZE" in source
    harness = QUEUE_HARNESS.split("static void WinnerStays()", 1)[0]
    harness = harness.replace("static const int GAME_DUEL = 9, GAME_DM = 0;", "")
    harness = harness.replace("#include <cassert>", '#include "game/mp/match/MatchEvidence.h"\n#include <cstring>\n#include <cassert>')
    harness = harness.replace("struct GameLocal {", "struct idPlayer { int entityNumber; };\nstruct GameLocal {")
    harness = harness.replace("struct GameLocal {", '''struct GameLocal {
    struct UserInfo { const char *name = "Winner"; const char *GetString(const char *) const { return name; } } userInfo[MAX_CLIENTS];
    bool teamGame = false;''')
    harness = harness.replace("bool IsTeamGame() const { return false; }", "bool IsTeamGame() const { return teamGame; }")
    harness = harness.replace("bool IsTeamGame() const", "template<class... T> void Warning(const char *, T...) {}\n    bool IsTeamGame() const")
    harness = harness.replace("    bool RotateManagedDuelQueue(", EVIDENCE_ADAPTER + "    bool RotateManagedDuelQueue(")
    projection = source.index("\tif ( matchTerminalResultSessionId != 0 && matchTerminalResultSessionId == publicState.sessionId")
    projection_end = source.index("\n\t}", projection) + len("\n\t}")
    harness = harness.replace("    bool RotateManagedDuelQueue(",
        "    void ProjectTerminalResult(mpMatchViewPublicState_t &publicState) const {\n" +
        source[projection:projection_end] + "\n    }\n    bool RotateManagedDuelQueue(")
    for token, signature in {
        "@COLLECT@": "bool idMultiplayerGame::CollectCompetitionSeriesContestants",
        "@SIDE@": "int idMultiplayerGame::ResolveCompetitionSide",
        "@ROTATE@": "bool idMultiplayerGame::RotateManagedDuelQueue",
        "@FORFEIT@": "void idMultiplayerGame::RecordManagedDuelResult",
        "@RESULT_PARTICIPANTS@": "bool idMultiplayerGame::ResolveManagedDuelForfeitParticipants",
        "@EVIDENCE@": "void idMultiplayerGame::RecordMatchEvidenceResult",
        "@CALCULATE@": "mpEvidenceMapResult idMultiplayerGame::BuildMatchTerminalEvidenceResult",
        "@CLEAR_RESULT@": "void idMultiplayerGame::ClearMatchTerminalResult",
        "@FREEZE@": "void idMultiplayerGame::FreezeMatchTerminalResult",
    }.items():
        assert token in harness
        harness = harness.replace(token, function_body(source, signature))
    rules = (MATCH / "MatchRules.cpp").read_text(encoding="utf-8")
    rules = re.sub(r'^#(?:include|pragma).*$', '', rules, flags=re.MULTILINE)
    types = (ROOT / "src/game/mp/GameTypes.cpp").read_text(encoding="utf-8")
    begin = types.index("static const mpGameTypeInfo_t mpGameTypeInfoTable[]")
    end = types.index("\n/*", types.index("const int mpNumGameTypeInfo", begin))
    definitions = types[begin:end] + "\n".join(function(types, signature) for signature in (
        "const mpGameTypeInfo_t *MPGameType(", "int MPGameTypeFlags(",
        "const char *MPGameTypeName(", "bool MPGameTypeIsSelectable(", "bool MPGameTypeHasAny(",
    ))
    rules_prefix = r'''
#include <string>
#include <cctype>
template<class T> static T Min(T a, T b) { return a < b ? a : b; }
#include "game/mp/match/MatchRules.h"
#define BIT(x) (1 << (x))
struct idDict;
#include "game/mp/GameTypes.h"
class idStr : public std::string {
public:
    using std::string::string; using std::string::operator=;
    void Clear() { clear(); } void Append(const char *value) { append(value); }
    int Length() const { return static_cast<int>(size()); }
    static int Icmp(const char *a, const char *b) {
        while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
        return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
    }
    template<class... T> static int snPrintf(char *out, int size, const char *format, T... args) {
        return std::snprintf(out, size, format, args...);
    }
};
'''
    return VIEW_HARNESS.split("#define CHECK", 1)[0] + rules_prefix + definitions + rules + harness + CASES


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for managed Duel evidence regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-duel-result-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        harness = path / "duel_result.cpp"
        executable = path / ("duel_result.exe" if os.name == "nt" else "duel_result")
        harness.write_text(build_harness(), encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra",
                   "-DMP_MATCH_SESSION_STANDALONE_TEST", "-DMP_MATCH_TEAMS_STANDALONE_TEST",
                   "-DMP_MATCH_EVIDENCE_STANDALONE_TEST", f"-I{ROOT / 'src'}", str(harness),
                   str(MATCH / "MatchSession.cpp"), str(MATCH / "MatchTeams.cpp"),
                   str(MATCH / "MatchEvidence.cpp"), "-o", str(executable)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        completed = subprocess.run([str(executable)], cwd=ROOT, check=True, text=True, capture_output=True)
        exports = [json.loads(line) for line in completed.stdout.splitlines()]
        assert len(exports) == 8
        for evidence in exports:
            result = evidence["journal"]["events"][0]["data"]
            participants = evidence["participantStats"]["entries"]
            assert all(participant["side"] == -1 for participant in participants)
            winner = next(p for p in participants if p["participant"] == result["winnerParticipant"])
            assert winner["displayName"] == "Winner" and winner["score"] == -2
            assert sorted(result["sideScores"]) == [-2, 9]
    print("mp_match_duel_result_contract: PASS (frozen public result, casual profile retention, evidence-off, leading forfeit, departed series opponent, slot reuse, team mapping, canonical JSON)")


if __name__ == "__main__":
    main()
