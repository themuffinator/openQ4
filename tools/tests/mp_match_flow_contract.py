#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute the production score-limit loops and session/countdown adapter.

Engine services are small deterministic stubs; the functions under test are
extracted from their canonical source, not rewritten in the harness.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include "game/mp/match/MatchSession.h"
#include "game/mp/GameTypeIds.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template<class T> T Max(T a, T b) { return std::max(a,b); }
enum { MSG_FRAGLIMIT, MSG_CAPTURELIMIT, MSG_TIMELIMIT, MSG_HOLYSHIT };
const int FRAGLIMIT_DELAY = 2000, CAPTURELIMIT_DELAY = 2000;
const int ARENA_CAMPAIGN_ENTRANCE_MIN_MSEC = 4000;
struct mpMatchRulesSnapshot {
    uint64_t Revision() const { return 1; }
    uint64_t Digest() const { return 0x1234; }
};
struct Rules {
    mpMatchRulesSnapshot snapshot;
    const mpMatchRulesSnapshot &Committed() const { return snapshot; }
};
struct idEntity { virtual ~idEntity() {} bool IsType(int) const { return true; } };
struct idPlayer : idEntity {
    static int GetClassType() { return 0; }
    int entityNumber = 0, team = 0;
    bool spectating = false, active = true, leader = false;
    bool IsFakeClient() const { return entityNumber == 999; }
    void SetLeader(bool value) { leader = value; }
};
struct Rank { idPlayer *player; idPlayer *First() const { return player; } };
struct Ranks : std::vector<Rank> { int Num() const { return int(size()); } };
class rvGameState;
struct mpMatchViewParticipantSummary_t { int slot = 0; bool connected = false, human = false; };
struct mpSessionView {
    struct PublicState {
        int participantSummaryCount = 0;
        mpMatchViewParticipantSummary_t participantSummaries[32];
    } publicState;
};
struct idMultiplayerGame {
    enum { CPARM_TEAM };
    struct Score { int fragCount = 0; } playerState[32];
    int teamScores[2] = {}, winner = -1, messages = 0;
    bool clockExpired = false;
    bool matchSessionOperational = true, competitiveRulesValidForSession = true;
    Rules matchRules;
    Ranks rankedPlayers;
    mpMatchSession matchSession;
    rvGameState *gameState = nullptr;
    const mpSessionView *clientView = nullptr;
    const mpSessionView *GetClientMatchView() const { return clientView; }
    bool CanPlay(idPlayer *p) { return p->active; }
    bool IsRankedParticipant(idPlayer *p);
    int GetScore(idPlayer *p) { return GetScore(p->entityNumber); }
    int GetScore(int slot) { return playerState[slot].fragCount; }
    int GetScoreForTeam(int team) { return teamScores[team]; }
    int MercyLimitHit() { return -1; }
    bool TimeLimitHit() { return clockExpired; }
    void PrintMessageEvent(int, int kind, int value = -1) {
        ++messages;
        if (kind == MSG_FRAGLIMIT || kind == MSG_CAPTURELIMIT) winner = value;
    }
    void CenterPrint(int, const char *, int, int) {}
    idPlayer *FragLeader();
    idPlayer *FragLimitHit();
    bool ScoreIsTied(int *leadingScore = nullptr);
    void ReconcileGameplayPhaseAfterMatchMutation();
    bool IsArenaCampaignMatch() const { return false; }
    bool ArenaCampaignIntroBlocksCountdown() const { return false; }
    bool AllPlayersReady() const { return matchSession.EvaluateReadiness().IsReady(); }
    bool CanEnterMatchCountdown() const { return true; }
    mpMatchTransitionReason_t InferMatchTransitionReason(mpGameState_t from, mpGameState_t to) const;
    bool CanCommitMatchPhaseTransition(mpGameState_t newState) const;
};
struct Dict {
    std::map<std::string, int> values;
    int GetInt(const char *key) { return values[key]; }
    bool GetBool(const char *key) { return GetInt(key) != 0; }
};
struct GameLocal {
    idMultiplayerGame mpGame;
    Dict serverInfo;
    int time = 100, numClients = 3, gameType = GAME_DM;
    bool isServer = true, isClient = false, teamGame = false;
    idEntity *entities[32] = {};
    bool IsTeamGame() { return teamGame; }
} gameLocal;
bool idMultiplayerGame::IsRankedParticipant(idPlayer *p) {
    return CanPlay(p) && (gameLocal.gameType != GAME_DUEL || !p->spectating);
}
struct BotManager {
    bool bots[32] = {};
    bool IsBot(int slot) { return slot >= 0 && slot < 32 && bots[slot]; }
} botManager;
struct Common { template<class... T> void DPrintf(const char *, T...) {} } commonValue, *common = &commonValue;
class rvGameState {
public:
    mpGameState_t currentState = GAMEON, nextState = INACTIVE;
    int fragLimitTimeout = 0, nextStateTime = 0, reviews = 0, overtimeCalls = 0;
    bool allowOvertime = false;
    bool validateCountdown = false;
    int countdownAttempts = 0, rejectedCountdowns = 0;
    void Run() {}
    void RunWarmup() { switch(currentState) { /* PRODUCTION_WARMUP_LOOP */ default: break; } }
    bool NewState(mpGameState_t value) {
        if (value == COUNTDOWN && validateCountdown) {
            ++countdownAttempts;
            auto &session = gameLocal.mpGame.matchSession;
            if (session.BeginCountdown(1, 0x1234, MP_MATCH_TRANSITION_READY_GATE,
                mpParticipantId::Invalid(), session.GetSessionRevision()).WasRejected()) {
                ++rejectedCountdowns;
                return false;
            }
        }
        currentState = value;
        if (value == GAMEREVIEW) ++reviews;
        switch(value) { /* PRODUCTION_COUNTDOWN_ENTRY */ default: break; }
        return true;
    }
    bool StartOvertime() {
        ++overtimeCalls;
        if (!allowOvertime) return false;
        gameLocal.mpGame.clockExpired = false;
        return true;
    }
    mpGameState_t GetMPGameState() { return currentState; }
    void SetNextMPGameState(mpGameState_t value) { nextState = value; }
    void SetNextMPGameStateTime(int value) { nextStateTime = value; }
};
class rvDMGameState : public rvGameState { public: void Run(); };
class rvTeamDMGameState : public rvGameState { public: void Run(); };
class rvCTFGameState : public rvGameState { public: void Run(); };
'''

TESTS = r'''
static mpParticipantId Bind(mpMatchSession &session, int slot, bool human, int side = -1) {
    mpParticipantId id;
    CHECK(session.BindParticipant(slot, human, MPMatchRoleBit(MP_MATCH_ROLE_PLAYER), session.GetSessionRevision(), id).WasApplied());
    CHECK(session.SetParticipantActive(id, true, session.GetSessionRevision()).WasApplied());
    if (side >= 0) CHECK(session.SetParticipantSide(id, side, session.GetSessionRevision()).WasApplied());
    return id;
}
static void Warmup(mpMatchSession &session, const mpMatchReadinessPolicy &policy) {
    CHECK(session.Reset(17, mpMatchEngineTime::FromMilliseconds(0)));
    CHECK(session.FreezeRules(1, 0x1234, session.GetSessionRevision()).WasApplied());
    CHECK(session.TransitionPhase(WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(), session.GetSessionRevision()).WasApplied());
    CHECK(session.ConfigureReadiness(policy, session.GetSessionRevision()).WasApplied());
}
static void Readiness() {
    auto &mp = gameLocal.mpGame;
    auto &session = mp.matchSession;
    mpMatchReadinessPolicy policy;
    policy.policy = MP_MATCH_READY_INDIVIDUAL;
    policy.minimumActiveHumans = 1;
    policy.minimumActiveParticipants = 3;
    policy.readyThresholdBasisPoints = 5100;
    Warmup(session, policy);
    auto human = Bind(session, 0, true);
    Bind(session, 1, false);
    auto ready = session.EvaluateReadiness();
    CHECK(ready.activeParticipants == 2 && !ready.IsReady());
    CHECK(ready.blockers & MPMatchReadinessBlockerBit(MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_PARTICIPANTS));
    auto bot = Bind(session, 2, false);
    ready = session.EvaluateReadiness();
    CHECK(ready.activeHumans == 1 && ready.readyEligibleParticipants == 1);
    CHECK(ready.readyParticipants == 0 && !ready.IsReady());
    CHECK(session.SetParticipantReady(human, true, session.GetSessionRevision()).WasApplied());
    CHECK(session.EvaluateReadiness().IsReady());
    CHECK(session.TransitionPhase(COUNTDOWN, MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(), session.GetSessionRevision()).WasApplied());
    rvGameState state;
    mp.gameState = &state;
    state.currentState = COUNTDOWN;
    state.nextState = GAMEON;
    state.nextStateTime = gameLocal.time; // Readiness changes on the last frame.
    CHECK(session.SetParticipantReady(human, false, session.GetSessionRevision()).WasApplied());
    CHECK(session.GetPhase() == WARMUP);
    mp.ReconcileGameplayPhaseAfterMatchMutation();
    CHECK(state.currentState == WARMUP && state.nextState == INACTIVE && state.nextStateTime == 0);
    CHECK(session.SetParticipantReady(human, true, session.GetSessionRevision()).WasApplied());
    CHECK(session.TransitionPhase(COUNTDOWN, MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(), session.GetSessionRevision()).WasApplied());
    CHECK(session.SetParticipantActive(bot, false, session.GetSessionRevision()).WasApplied());
    CHECK(session.GetPhase() == WARMUP);
    CHECK(session.ValidateInvariants());

    policy.teamMode = true;
    policy.minimumActiveParticipants = 2;
    policy.minimumActivePerRequiredSide = 1;
    policy.minimumActiveOnAnySide = 2;
    policy.requiredSideMask = 3;
    Warmup(session, policy);
    human = Bind(session, 0, true, 0);
    bot = Bind(session, 1, false, 0);
    CHECK(session.SetParticipantReady(human, true, session.GetSessionRevision()).WasApplied());
    CHECK(!session.EvaluateReadiness().IsReady()); // No opposing team.
    CHECK(session.SetParticipantSide(bot, 1, session.GetSessionRevision()).WasApplied());
    CHECK(session.SetParticipantReady(human, true, session.GetSessionRevision()).WasApplied() || session.EvaluateReadiness().readyParticipants == 1);
    CHECK(!session.EvaluateReadiness().IsReady()); // Neither team meets the larger minimum.
    Bind(session, 2, false, 0);
    CHECK(session.EvaluateReadiness().IsReady()); // 2v1 is legal with forcePresent=0.
    CHECK(session.ValidateInvariants());
    auto invalid = policy;
    invalid.minimumActiveParticipants = -1;
    CHECK(session.ConfigureReadiness(invalid, session.GetSessionRevision()).WasRejected());
    invalid = policy;
    invalid.maximumActivePerSide = 1;
    CHECK(session.ConfigureReadiness(invalid, session.GetSessionRevision()).WasRejected());
}

template<class State> static void TeamEndings(const char *limitKey) {
    gameLocal.teamGame = true;
    auto &mp = gameLocal.mpGame;
    gameLocal.serverInfo.values[limitKey] = 10;
    for (int winningTeam = 0; winningTeam < 2; ++winningTeam) {
        State state;
        gameLocal.time = 100;
        mp.clockExpired = false;
        mp.winner = -1;
        mp.teamScores[0] = mp.teamScores[1] = 10;
        mp.teamScores[winningTeam] = 11;
        state.Run();
        CHECK(state.currentState == GAMEON && state.fragLimitTimeout > gameLocal.time);
        gameLocal.time = state.fragLimitTimeout + 1;
        state.Run();
        CHECK(state.currentState == GAMEREVIEW && mp.winner == winningTeam);
        state.Run();
        CHECK(state.reviews == 1);
    }
    State tied;
    gameLocal.time = 100;
    mp.teamScores[0] = mp.teamScores[1] = 10;
    tied.allowOvertime = true;
    tied.Run();
    CHECK(tied.fragLimitTimeout > gameLocal.time);
    ++gameLocal.time;
    tied.Run();
    CHECK(tied.currentState == GAMEON && tied.overtimeCalls == 0);
    gameLocal.time = tied.fragLimitTimeout + 1;
    tied.Run();
    CHECK(tied.currentState == SUDDENDEATH && tied.overtimeCalls == 0 && tied.fragLimitTimeout == 0);
    ++mp.teamScores[1];
    tied.Run();
    gameLocal.time = tied.fragLimitTimeout + 1;
    tied.Run();
    CHECK(tied.currentState == GAMEREVIEW && mp.winner == 1);

    State lostPoint;
    gameLocal.time = 100;
    mp.teamScores[0] = 10; mp.teamScores[1] = 8;
    lostPoint.Run();
    mp.teamScores[0] = 9;
    gameLocal.time += 3000;
    lostPoint.Run();
    CHECK(lostPoint.currentState == GAMEON && lostPoint.fragLimitTimeout == 0);

    State clock;
    mp.teamScores[0] = mp.teamScores[1] = 1;
    mp.clockExpired = true;
    clock.allowOvertime = true;
    clock.Run();
    CHECK(clock.currentState == GAMEON && clock.overtimeCalls == 1);
    clock.Run();
    CHECK(clock.overtimeCalls == 1);
    mp.clockExpired = true;
    clock.allowOvertime = false;
    clock.Run();
    CHECK(clock.currentState == SUDDENDEATH);
}

static void IndividualEndings() {
    gameLocal.teamGame = false;
    gameLocal.gameType = GAME_DM;
    auto &mp = gameLocal.mpGame;
    mp.clockExpired = false;
    idPlayer players[3];
    for (int i = 0; i < 3; ++i) {
        players[i].entityNumber = i;
        gameLocal.entities[i] = &players[i];
        mp.playerState[i].fragCount = -3 + i;
    }
    CHECK(MPHasActiveHumanMatchParticipant());
    gameLocal.isClient = true;
    CHECK(!MPIsHumanMatchParticipant(&players[0])); // Wait for identity replication.
    mpSessionView view;
    view.publicState.participantSummaryCount = 1;
    view.publicState.participantSummaries[0].connected = true;
    mp.clientView = &view;
    CHECK(!MPIsHumanMatchParticipant(&players[0])); // Explicit bot bit beats an empty local bot manager.
    view.publicState.participantSummaries[0].human = true;
    CHECK(MPIsHumanMatchParticipant(&players[0]));
    gameLocal.isClient = false;
    mp.clientView = nullptr;
    for (int i = 0; i < 3; ++i) botManager.bots[i] = true;
    CHECK(!MPIsHumanMatchParticipant(&players[1]));
    CHECK(!MPHasActiveHumanMatchParticipant());
    botManager.bots[0] = false;
    CHECK(MPIsHumanMatchParticipant(&players[0]));
    players[0].active = false;
    CHECK(!MPHasActiveHumanMatchParticipant());
    players[0].active = true;
    CHECK(MPHasActiveHumanMatchParticipant());
    gameLocal.gameType = GAME_DUEL;
    players[0].spectating = true;
    CHECK(!MPHasActiveHumanMatchParticipant());
    players[0].spectating = false;
    gameLocal.gameType = GAME_DM;
    mp.rankedPlayers = {}; // Deliberately no fresh ranking cache.
    CHECK(mp.FragLeader() == &players[2]);
    mp.playerState[1].fragCount = -1;
    CHECK(mp.FragLeader() == nullptr);
    CHECK(players[1].leader && players[2].leader && !players[0].leader);
    players[2].active = false;
    CHECK(mp.FragLeader() == &players[1] && !players[2].leader);
    gameLocal.gameType = GAME_DUEL;
    players[2].active = true;
    players[2].spectating = true;
    mp.playerState[2].fragCount = 99;
    CHECK(mp.FragLeader() == &players[1]); // Queue scores never supply the winner.
    gameLocal.gameType = GAME_DM;
    players[2].active = false;
    mp.playerState[0].fragCount = mp.playerState[1].fragCount = 10;
    gameLocal.serverInfo.values["si_fragLimit"] = 10;
    rvDMGameState state;
    state.allowOvertime = true;
    gameLocal.time = 100;
    state.Run();
    ++gameLocal.time;
    state.Run();
    CHECK(state.currentState == GAMEON);
    gameLocal.time = state.fragLimitTimeout + 1;
    state.Run();
    CHECK(state.currentState == SUDDENDEATH && state.overtimeCalls == 0);
    ++mp.playerState[1].fragCount;
    state.Run();
    gameLocal.time = state.fragLimitTimeout + 1;
    state.Run();
    CHECK(state.currentState == GAMEREVIEW && mp.winner == 1);
}
static void QuietEmptyPracticeServer() {
    auto &mp = gameLocal.mpGame;
    mpMatchReadinessPolicy policy;
    policy.policy = MP_MATCH_READY_DISABLED;
    policy.minimumActiveHumans = 1;
    policy.minimumActiveParticipants = 0;
    Warmup(mp.matchSession, policy);
    gameLocal.gameType = GAME_DM;
    gameLocal.serverInfo.values["si_warmup"] = 0;
    rvGameState state; state.currentState = WARMUP; state.validateCountdown = true;
    auto revision = mp.matchSession.GetSessionRevision();
    for (int frame = 0; frame < 10000; ++frame) {
        ++gameLocal.time; state.RunWarmup();
    }
    CHECK(state.currentState == WARMUP && state.nextState == INACTIVE);
    CHECK(state.countdownAttempts == 0 && state.rejectedCountdowns == 0);
    CHECK(mp.matchSession.GetSessionRevision() == revision);
    Bind(mp.matchSession, 0, false); Bind(mp.matchSession, 1, false);
    revision = mp.matchSession.GetSessionRevision();
    for (int frame = 0; frame < 10000; ++frame) {
        ++gameLocal.time; state.RunWarmup();
    }
    CHECK(state.currentState == WARMUP && state.countdownAttempts == 0);
    CHECK(mp.matchSession.GetSessionRevision() == revision);
    Bind(mp.matchSession, 2, true);
    state.RunWarmup();
    CHECK(state.currentState == COUNTDOWN && state.nextState == GAMEON);
    CHECK(state.nextStateTime == gameLocal.time);
    CHECK(state.countdownAttempts == 1 && state.rejectedCountdowns == 0);
    CHECK(mp.matchSession.GetPhase() == COUNTDOWN);
}
int main() {
    // Direct typed/operator entry must schedule live play without going through
    // the automatic warmup branch. A subsequent entry replaces the old timer.
    for(int seconds: {4,5,30}) {
        rvGameState state;
        gameLocal.time=2000;
        gameLocal.serverInfo.values["si_countDown"]=seconds;
        CHECK(state.NewState(COUNTDOWN));
        CHECK(state.nextState==GAMEON && state.nextStateTime==2000+seconds*1000);
        gameLocal.time=9000;
        CHECK(state.NewState(COUNTDOWN));
        CHECK(state.nextState==GAMEON && state.nextStateTime==9000+seconds*1000);
    }
    Readiness();
    TeamEndings<rvTeamDMGameState>("si_fragLimit");
    TeamEndings<rvCTFGameState>("si_captureLimit");
    IndividualEndings();
    QuietEmptyPracticeServer();
    std::puts("readiness, countdown cancellation, fresh leaders, score/time limits: PASS");
}
'''


def main() -> None:
    mp = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    states = (ROOT / "src/game/mp/GameState.cpp").read_text(encoding="utf-8")
    run = function(mp, "void idMultiplayerGame::Run( void )")
    assert run.index("ReconcileGameplayPhaseAfterMatchMutation();") < run.index("gameState->Run();")
    sync = function(mp, "void idMultiplayerGame::SynchronizeMatchParticipant(")
    assert "BindParticipant( clientNum, MPIsHumanMatchParticipant( player )" in sync
    assert "switchThrottle[ 1 ] = 0;" in function(mp, "void idMultiplayerGame::SendReady(")
    new_state = function(states, "bool rvGameState::NewState(")
    warmup = new_state[new_state.index("case WARMUP:"):new_state.index("case GAMEON:")]
    assert all(token in warmup for token in ("nextState = INACTIVE;", "nextStateTime = 0;", "fragLimitTimeout = 0;"))
    countdown = new_state[new_state.index("case COUNTDOWN:"):new_state.index("case GAMEON:")]
    base_run = function(states, "void rvGameState::Run(")
    warmup_loop = base_run[base_run.index("case WARMUP:"):base_run.index("case COUNTDOWN:")]
    functions = [function(mp, signature) for signature in (
        "static bool MPIsHumanMatchParticipant(",
        "static bool MPHasActiveHumanMatchParticipant(",
        "idPlayer* idMultiplayerGame::FragLeader(",
        "idPlayer *idMultiplayerGame::FragLimitHit(",
        "bool idMultiplayerGame::ScoreIsTied(",
        "void idMultiplayerGame::ReconcileGameplayPhaseAfterMatchMutation(",
        "mpMatchTransitionReason_t idMultiplayerGame::InferMatchTransitionReason(",
        "bool idMultiplayerGame::CanCommitMatchPhaseTransition(",
    )]
    functions += [function(states, f"void {name}::Run(") for name in (
        "rvDMGameState", "rvTeamDMGameState", "rvCTFGameState")]
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for match-flow regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-flow-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        source = path / "flow.cpp"
        exe = path / ("flow.exe" if os.name == "nt" else "flow")
        source.write_text(STUBS.replace("/* PRODUCTION_COUNTDOWN_ENTRY */", countdown)
                          .replace("/* PRODUCTION_WARMUP_LOOP */", warmup_loop)
                          + "\n".join(functions) + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wno-switch", "-DMP_MATCH_SESSION_STANDALONE_TEST",
                   f"-I{ROOT / 'src'}", str(source),
                   str(ROOT / "src/game/mp/match/MatchSession.cpp"), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_flow_contract: PASS")


if __name__ == "__main__":
    main()
