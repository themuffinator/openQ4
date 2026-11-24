#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Compile managed Duel turnover against the production session and FIFO cores."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body


ROOT = Path(__file__).resolve().parents[2]
MATCH = ROOT / "src/game/mp/match"

HARNESS = r'''
#include "game/mp/match/MatchTeams.h"
#include "game/mp/match/MatchSeries.h"
#include <cassert>
#include <cstdio>
#include <vector>
static const int GAME_DUEL = 9, GAME_DM = 0;
static const int MAX_CLIENTS = MP_MATCH_MAX_CONNECTION_SLOTS;
template<class T> static T Max(T a, T b) { return a > b ? a : b; }
struct GameLocal {
    bool isServer = true, isClient = false;
    int gameType = GAME_DUEL, numClients = 4, time = 100;
    bool IsTeamGame() const { return false; }
} gameLocal;
struct Series {
    mpSeriesState_t state = MP_SERIES_DISABLED;
    mpSeriesState_t GetState() const { return state; }
};
struct rvDuelGameState {
    int forfeitingSlot = -3;
    bool cancelled = false;
    void SetForfeitingContender(int slot) { forfeitingSlot = slot; }
    void CancelTurnover() { cancelled = true; }
};
struct idMultiplayerGame {
    mpMatchSession matchSession;
    mpMatchTeams matchTeams;
    Series matchSeries;
    bool managed = true, matchSessionOperational = true;
    int mirrors = 0, evidence = 0, revisions = 0;
    std::vector<int> mirroredSlots;
    rvDuelGameState *gameState = nullptr;
    uint64_t matchConnectionId[MAX_CLIENTS] = {};
    uint64_t matchSeriesCompetitionConnection[MAX_CLIENTS] = {};
    int matchSeriesCompetitionSide[MAX_CLIENTS] = {};
    int matchSeriesGameSideForCompetition[MP_SERIES_SIDE_COUNT] = {0, 1};
    bool IsCompetitionSeriesModeSupported() const { return gameLocal.gameType == GAME_DUEL; }
    bool CollectCompetitionSeriesContestants(int slots[MP_SERIES_SIDE_COUNT],
                                            uint64_t connections[MP_SERIES_SIDE_COUNT]) const {
@COLLECT@
    }
    int ResolveCompetitionSide(mpParticipantId participant) const {
@SIDE@
    }
    bool IsManagedMatch() const { return managed; }
    mpMatchTeamsPolicy_t BuildMatchTeamsPolicy() const {
        mpMatchTeamsPolicy_t policy;
        policy.Clear(); policy.queueEnabled = true; policy.maximumActiveTotal = 2;
        return policy;
    }
    void ApplyMatchTeamsPlanToLegacy(const mpMatchTeamsTransactionPlan_t &plan) {
        ++mirrors;
        for (auto id : {plan.incomingParticipant, plan.outgoingParticipant}) {
            int slot = -1; uint32_t generation = 0;
            assert(matchSession.ResolveParticipant(id, slot, generation));
            mirroredSlots.push_back(slot);
        }
    }
    void ObserveMatchEvidence(mpParticipantId participant) {
        assert(!participant.IsValid()); ++evidence;
    }
    void AdvanceMatchViewRevision(bool clock) { assert(clock); ++revisions; }
    bool RotateManagedDuelQueue(int firstSlot, int secondSlot, int losingSlot) {
@ROTATE@
    }
    void RecordManagedDuelResult(mpMatchTransitionReason_t reason, int forfeitingSide, mpParticipantId authorizer) {
@FORFEIT@
    }
    bool ResolveManagedDuelForfeitParticipants(int forfeitingSide, mpParticipantId authorizer,
                                              mpParticipantId &forfeiter, mpParticipantId &winner) const {
@RESULT_PARTICIPANTS@
    }
};
struct Fixture {
    idMultiplayerGame mp;
    mpParticipantId ids[MP_MATCH_MAX_PARTICIPANTS];
    int count;
    explicit Fixture(int clients = 4, bool botSecond = false) : count(clients) {
        gameLocal = GameLocal(); gameLocal.numClients = clients;
        assert(mp.matchSession.Reset(77, mpMatchEngineTime::FromMilliseconds(0)));
        mpMatchReadinessPolicy policy;
        policy.policy = MP_MATCH_READY_INDIVIDUAL;
        policy.minimumActiveHumans = 2;
        policy.readyThresholdBasisPoints = 10000;
        assert(!mp.matchSession.ConfigureReadiness(policy,
            mp.matchSession.GetSessionRevision()).WasRejected());
        Transition(WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED);
        assert(mp.matchTeams.Reset(77, mpMatchEngineTime::FromMilliseconds(0)));
        for (int i = 0; i < count; ++i) {
            mp.matchConnectionId[i] = 100 + i;
            assert(!mp.matchSession.BindParticipant(i, !(botSecond && i == 1),
                MPMatchRoleBit(MP_MATCH_ROLE_PLAYER), mp.matchSession.GetSessionRevision(), ids[i]).WasRejected());
            if (i < 2) {
                assert(!mp.matchSession.SetParticipantActive(ids[i], true,
                    mp.matchSession.GetSessionRevision()).WasRejected());
                if (!botSecond || i != 1) assert(!mp.matchSession.SetParticipantReady(ids[i], true,
                    mp.matchSession.GetSessionRevision()).WasRejected());
            } else {
                assert(!mp.matchTeams.JoinQueue(mp.matchSession, ids[i], MP_MATCH_SIDE_NONE,
                    mp.BuildMatchTeamsPolicy(), mpMatchEngineTime::FromMilliseconds(10 + i),
                    mp.matchTeams.GetRevision()).WasRejected());
            }
        }
    }
    void Transition(mpGameState_t phase, mpMatchTransitionReason_t reason) {
        assert(!mp.matchSession.TransitionPhase(phase, reason, mpParticipantId::Invalid(),
            mp.matchSession.GetSessionRevision()).WasRejected());
    }
    void Countdown() {
        assert(!mp.matchSession.BeginCountdown(1, 42, MP_MATCH_TRANSITION_REFEREE_FORCE_READY,
            mpParticipantId::Invalid(), mp.matchSession.GetSessionRevision()).WasRejected());
    }
    void Finish(bool nextGame = true) {
        Countdown();
        Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
        Transition(GAMEREVIEW, MP_MATCH_TRANSITION_LIMIT_REACHED);
        if (nextGame) Transition(NEXTGAME, MP_MATCH_TRANSITION_REVIEW_COMPLETE);
    }
    bool Active(int slot) const { return mp.matchSession.FindParticipant(ids[slot])->active; }
    int QueueSlot(int position) const {
        const auto *entry = mp.matchTeams.GetQueueEntry(position);
        int slot = -1; uint32_t generation = 0;
        assert(entry && mp.matchSession.ResolveParticipant(entry->participant, slot, generation));
        return slot;
    }
    void AssertUnchanged(uint64_t sessionRevision, uint64_t teamsRevision) const {
        assert(mp.matchSession.GetSessionRevision() == sessionRevision);
        assert(mp.matchTeams.GetRevision() == teamsRevision);
        assert(mp.mirrors == 0 && mp.evidence == 0 && mp.revisions == 0);
    }
    void Promote() {
        // Use the actual join plan, stale-plan checks and session activation.
        // Publishing only after both cores accept matches the live adapter.
        for (int i = 0; i < 2; ++i) {
            auto decision = mp.matchTeams.PlanNextQueueAdmission(mp.matchSession,
                mp.BuildMatchTeamsPolicy(), mpMatchEngineTime::FromMilliseconds(gameLocal.time));
            if (!decision.IsAllowed()) return;
            auto session = mp.matchSession;
            auto teams = mp.matchTeams;
            assert(!teams.CommitTransactionPlan(decision.plan, session,
                mp.BuildMatchTeamsPolicy(), mpMatchEngineTime::FromMilliseconds(gameLocal.time),
                teams.GetRevision()).WasRejected());
            assert(!session.SetParticipantActive(decision.plan.incomingParticipant, true,
                session.GetSessionRevision()).WasRejected());
            assert(session.ValidateInvariants() && teams.ValidateInvariants());
            mp.matchSession = session; mp.matchTeams = teams;
        }
    }
};
static void WinnerStays() {
    for (int count = 3; count <= MP_MATCH_MAX_PARTICIPANTS; ++count) {
        for (int loser = 0; loser < 2; ++loser) {
            Fixture f(count); f.Finish(false);
            auto headTicket = f.mp.matchTeams.GetQueueEntry(0)->ticketId;
            const auto sessionRevision = f.mp.matchSession.GetSessionRevision();
            const auto teamsRevision = f.mp.matchTeams.GetRevision();
            assert(!f.mp.RotateManagedDuelQueue(0, 1, loser));
            f.AssertUnchanged(sessionRevision, teamsRevision); // Finals remain in review.
            f.Transition(NEXTGAME, MP_MATCH_TRANSITION_REVIEW_COMPLETE);
            assert(f.mp.RotateManagedDuelQueue(0, 1, loser));
            assert(f.Active(1-loser) && !f.Active(loser));
            assert(f.mp.matchTeams.GetQueueCount() == count - 1);
            assert(f.QueueSlot(0) == 2 && f.QueueSlot(count - 2) == loser);
            assert(f.mp.matchTeams.GetQueueEntry(0)->ticketId == headTicket);
            assert(f.mp.mirrors == 1 && f.mp.evidence == 1 && f.mp.revisions == 1);
            assert(f.mp.mirroredSlots == std::vector<int>({0, 1}));
            assert(!f.mp.RotateManagedDuelQueue(0, 1, loser)); // No second ticket.
            f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART);
            f.Promote();
            assert(f.Active(1-loser) && f.Active(2) && !f.Active(loser));
            assert(f.mp.matchTeams.GetQueueCount() == count - 2);
            assert(!f.mp.matchSession.FindParticipant(f.ids[2])->ready);
            assert(!f.mp.matchSession.EvaluateReadiness().IsReady());
        }
    }
}
static void DrawAndDefer() {
    Fixture f(5);
    const auto ticket = f.mp.matchTeams.GetQueueEntry(0)->ticketId;
    assert(!f.mp.matchTeams.DeferQueue(f.mp.matchSession, f.ids[2], ticket,
        mpMatchEngineTime::FromMilliseconds(50), f.mp.matchTeams.GetRevision()).WasRejected());
    f.Finish();
    assert(f.mp.RotateManagedDuelQueue(0, 1, -1));
    assert(!f.Active(0) && !f.Active(1));
    assert(f.QueueSlot(0) == 3 && f.QueueSlot(1) == 4 && f.QueueSlot(2) == 2);
    assert(f.QueueSlot(3) == 0 && f.QueueSlot(4) == 1);
    f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART); f.Promote();
    assert(f.Active(3) && f.Active(4));
    assert(f.QueueSlot(0) == 2 && f.QueueSlot(1) == 0 && f.QueueSlot(2) == 1);
}
static void RejectionsAndSeries() {
    for (int state = 0; state < MP_SERIES_STATE_COUNT; ++state) {
        Fixture f; f.Finish(); f.mp.matchSeries.state = static_cast<mpSeriesState_t>(state);
        bool allowed = state == MP_SERIES_DISABLED || state == MP_SERIES_COMPLETE || state == MP_SERIES_CANCELLED;
        const auto s = f.mp.matchSession.GetSessionRevision(), t = f.mp.matchTeams.GetRevision();
        assert(f.mp.RotateManagedDuelQueue(0, 1, 1) == allowed);
        if (!allowed) f.AssertUnchanged(s, t);
    }
    for (int scenario = 0; scenario < 14; ++scenario) {
        Fixture f(scenario == 0 ? 2 : 4, scenario == 13); f.Finish();
        int first = 0, second = 1, loser = 1;
        switch (scenario) {
            case 1: f.mp.managed = false; break;
            case 2: f.mp.matchSessionOperational = false; break;
            case 3: gameLocal.isServer = false; break;
            case 4: gameLocal.isClient = true; break;
            case 5: gameLocal.gameType = GAME_DM; break;
            case 6: first = -1; break;
            case 7: second = 4; break;
            case 8: second = 0; break;
            case 9: loser = 2; break;
            case 10: loser = -2; break;
            case 11: gameLocal.time = 1; break; // JoinQueue rejects after candidate deactivation.
            case 12: {
                uint32_t generation = 0; assert(f.mp.matchSession.GetSlotGeneration(1, generation));
                assert(!f.mp.matchSession.UnbindParticipant(1, generation,
                    f.mp.matchSession.GetSessionRevision()).WasRejected());
                break;
            }
        }
        const auto s = f.mp.matchSession.GetSessionRevision(), t = f.mp.matchTeams.GetRevision();
        assert(!f.mp.RotateManagedDuelQueue(first, second, loser));
        f.AssertUnchanged(s, t);
        assert(f.Active(0));
    }
}
static void DisconnectAndRepeatedTurnover() {
    Fixture f;
    f.Finish(); assert(f.mp.RotateManagedDuelQueue(0, 1, 1));
    const auto oldParticipant = f.ids[1];
    assert(!f.mp.matchTeams.RemoveParticipant(77, oldParticipant,
        mpMatchEngineTime::FromMilliseconds(100), f.mp.matchTeams.GetRevision()).WasRejected());
    uint32_t generation = 0; assert(f.mp.matchSession.GetSlotGeneration(1, generation));
    assert(!f.mp.matchSession.UnbindParticipant(1, generation,
        f.mp.matchSession.GetSessionRevision()).WasRejected());
    assert(!f.mp.matchSession.BindParticipant(1, true, MPMatchRoleBit(MP_MATCH_ROLE_PLAYER),
        f.mp.matchSession.GetSessionRevision(), f.ids[1]).WasRejected());
    assert(!(f.ids[1] == oldParticipant) && f.mp.matchTeams.FindQueuePosition(f.ids[1]) < 0);
    f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART); f.Promote();
    assert(f.Active(0) && f.Active(2) && !f.Active(1));
    f.Finish(); assert(f.mp.RotateManagedDuelQueue(0, 2, 0));
    assert(f.QueueSlot(0) == 3 && f.QueueSlot(1) == 0);
    f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART); f.Promote();
    assert(f.Active(2) && f.Active(3) && !f.Active(0));
}
static void ForfeitIdentity() {
    for (int side = 0; side < 2; ++side) {
        Fixture f; f.Finish(false);
        rvDuelGameState duel; f.mp.gameState = &duel;
        // Operator/referee authorizer can differ from the forfeiting side.
        f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_FORFEIT, side, f.ids[1-side]);
        assert(duel.forfeitingSlot == side);
        f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_FORFEIT, -1, f.ids[side]);
        assert(duel.forfeitingSlot == side);
        f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_FORFEIT, -1, f.ids[2]);
        assert(duel.forfeitingSlot == -1); // A queued spectator cannot be substituted.
        f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_FORFEIT, -1, mpParticipantId::Invalid());
        assert(duel.forfeitingSlot == -1);
    }
    Fixture f; f.Finish(false);
    rvDuelGameState duel; f.mp.gameState = &duel;
    f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_LIMIT_REACHED, -1, mpParticipantId::Invalid());
    assert(!duel.cancelled && duel.forfeitingSlot == -3);
    f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_MATCH_ABORTED, -1, mpParticipantId::Invalid());
    assert(duel.cancelled && duel.forfeitingSlot == -3);
}
static void TerminalSeriesSides() {
    for (int terminal : {MP_SERIES_COMPLETE, MP_SERIES_CANCELLED}) {
        Fixture f;
        f.mp.matchSeries.state = static_cast<mpSeriesState_t>(terminal);
        for (int i = 0; i < 2; ++i) {
            f.mp.matchSeriesCompetitionConnection[i] = f.mp.matchConnectionId[i];
            f.mp.matchSeriesCompetitionSide[i] = i;
        }
        f.Finish(); assert(f.mp.RotateManagedDuelQueue(0, 1, 0));
        f.Transition(WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART); f.Promote();
        assert(f.Active(1) && f.Active(2));
        // The old side-B contestant now occupies current side A. Retaining
        // their old cached side would alias both humans to the same budget.
        assert(f.mp.ResolveCompetitionSide(f.ids[1]) == 0);
        assert(f.mp.ResolveCompetitionSide(f.ids[2]) == 1);
        assert(f.mp.ResolveCompetitionSide(f.ids[0]) == -1);
        assert(!f.mp.matchSession.ConfigureTimeouts(2, 1000, false, 500,
            MP_MATCH_RESUME_OWNER_OR_AUTHORITY, f.mp.matchSession.GetSessionRevision()).WasRejected());
        f.Countdown(); f.Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
        for (int slot : {1, 2}) {
            const int side = f.mp.ResolveCompetitionSide(f.ids[slot]);
            auto session = f.mp.matchSession;
            assert(!session.RequestTeamTimeout(side, MP_MATCH_PAUSE_REASON_TACTICAL,
                session.GetSessionRevision()).WasRejected());
            assert(!session.AdvanceFrame(mpMatchEngineTime::FromMilliseconds(100)).WasRejected());
            assert(session.GetPause().ownerSide == side);
            assert(session.GetTimeoutBudget(side).consumed == 1);
            assert(session.GetTimeoutBudget(1-side).consumed == 0);
        }
        f.Transition(GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT);
        rvDuelGameState duel; f.mp.gameState = &duel;
        for (int slot : {1, 2}) {
            f.mp.RecordManagedDuelResult(MP_MATCH_TRANSITION_FORFEIT,
                f.mp.ResolveCompetitionSide(f.ids[slot]), mpParticipantId::Invalid());
            assert(duel.forfeitingSlot == slot);
        }
    }
    for (int state = MP_SERIES_SETUP; state <= MP_SERIES_MAP_COMPLETE; ++state) {
        Fixture f; f.mp.matchSeries.state = static_cast<mpSeriesState_t>(state);
        for (int i = 0; i < 2; ++i) {
            f.mp.matchSeriesCompetitionConnection[i] = f.mp.matchConnectionId[i];
            f.mp.matchSeriesCompetitionSide[i] = 1-i;
        }
        assert(f.mp.ResolveCompetitionSide(f.ids[0]) == 1);
        assert(f.mp.ResolveCompetitionSide(f.ids[1]) == 0);
        ++f.mp.matchConnectionId[0];
        assert(f.mp.ResolveCompetitionSide(f.ids[0]) == -1);
    }
}
int main() {
    WinnerStays(); DrawAndDefer(); RejectionsAndSeries(); DisconnectAndRepeatedTurnover(); ForfeitIdentity(); TerminalSeriesSides();
    std::puts("managed Duel review, FIFO turnover, readiness, series and atomic rejection: PASS");
}
'''


def main() -> None:
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    body = function_body(source, "bool idMultiplayerGame::RotateManagedDuelQueue")
    forfeit = function_body(source, "void idMultiplayerGame::RecordManagedDuelResult")
    participants = function_body(source, "bool idMultiplayerGame::ResolveManagedDuelForfeitParticipants")
    effects = function_body(source, "bool idMultiplayerGame::ApplyCommittedMatchPhaseEffects")
    assert "RecordManagedDuelResult( transition.reason, forfeitingSide, transition.authorizer )" in effects
    collect = function_body(source, "bool idMultiplayerGame::CollectCompetitionSeriesContestants")
    side = function_body(source, "int idMultiplayerGame::ResolveCompetitionSide")
    process = function_body(source, "void idMultiplayerGame::ProcessMatchTeamQueue")
    assert "matchSession.GetPhase() != WARMUP" in process
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for managed Duel queue regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-duel-queue-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        harness = path / "duel_queue.cpp"
        executable = path / ("duel_queue.exe" if os.name == "nt" else "duel_queue")
        harness.write_text(HARNESS.replace("@ROTATE@", body).replace("@FORFEIT@", forfeit)
                           .replace("@RESULT_PARTICIPANTS@", participants)
                           .replace("@COLLECT@", collect).replace("@SIDE@", side), encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra",
                   "-DMP_MATCH_SESSION_STANDALONE_TEST", "-DMP_MATCH_TEAMS_STANDALONE_TEST",
                   f"-I{ROOT / 'src'}", str(harness), str(MATCH / "MatchSession.cpp"),
                   str(MATCH / "MatchTeams.cpp"), "-o", str(executable)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print("mp_match_duel_queue_contract: PASS")


if __name__ == "__main__":
    main()
