#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Hostile executable and adapter-order contracts for population termination."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchTerminationPolicy.h"
SOURCE = ROOT / "src/game/mp/match/MatchTerminationPolicy.cpp"
MULTIPLAYER = ROOT / "src/game/MultiplayerGame.cpp"
GAME_STATE = ROOT / "src/game/mp/GameState.cpp"


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def bounded(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def static_contracts() -> None:
    header = HEADER.read_text(encoding="utf-8", errors="strict")
    source = SOURCE.read_text(encoding="utf-8", errors="strict")
    multiplayer = MULTIPLAYER.read_text(encoding="utf-8", errors="strict")
    game_state = GAME_STATE.read_text(encoding="utf-8", errors="strict")

    for forbidden in (
        "gameLocal",
        "idPlayer",
        "idCVar",
        "idFile",
        "cmdSystem",
        "networkSystem",
        "teamScore",
    ):
        if forbidden in header + source:
            raise AssertionError(
                f"termination policy contains adapter dependency {forbidden!r}"
            )

    for token in (
        "MPEvaluatePopulationTermination",
        "MP_MATCH_TERMINATION_COUNTDOWN_CANCELLED",
        "MP_MATCH_TRANSITION_COUNTDOWN_ABORTED",
        "MP_MATCH_TRANSITION_MATCH_ABORTED",
        "MP_MATCH_TRANSITION_FORFEIT",
    ):
        require(header + source, token, "pure termination policy")

    abort_adapter = bounded(
        multiplayer,
        "void idMultiplayerGame::CheckAbortGame( mpParticipantId",
        "idMultiplayerGame::WantKilled",
    )
    for token in (
        "MPEvaluatePopulationTermination( phase, enoughClients, forfeitingSide )",
        "CommitMatchPhaseTransition( decision.targetPhase, decision.reason",
        "decision.forfeitingSide",
        "matchSeriesContestantConnection[ opponentSide ] ==",
        "MP_SERIES_MAP_ACTIVE",
    ):
        require(abort_adapter, token, "population-loss adapter")
    for forbidden in (
        "TimeLimitHit()",
        "GetMatchLengthMsec()",
        "gameState->NewState( WARMUP )",
        "gameState->NewState( GAMEREVIEW )",
    ):
        if forbidden in abort_adapter:
            raise AssertionError(
                f"population-loss adapter retains inferred legacy branch {forbidden!r}"
            )
    require(
        abort_adapter,
        'if ( !IsManagedMatch() || gameLocal.gameType != GAME_DUEL ) {\n'
        '\t\t\tAddChatLine( "%s", common->GetLocalizedString( "#str_41315" ) );',
        "committed managed Duel feedback avoids duplicate local population-loss chat",
    )

    forfeit_team = bounded(
        multiplayer,
        "int idMultiplayerGame::ForfeitTeam",
        "idMultiplayerGame::GetOvertimeRespawnDelay",
    )
    require(
        forfeit_team,
        "matchRules.Committed().GetBool( MP_RULE_FORFEIT_ON_EMPTY_TEAM )",
        "managed forfeit rule authority",
    )

    effects = bounded(
        multiplayer,
        "bool idMultiplayerGame::ApplyCommittedMatchPhaseEffects",
        "bool idMultiplayerGame::CommitMatchPhaseTransition( mpGameState_t newState,\n\t\tmpMatchTransitionReason_t reason, mpParticipantId authorizer,",
    )
    for token in (
        "matchPhaseEffectsSessionId == matchSession.GetSessionId()",
        "matchPhaseEffectsRevision == matchSession.GetSessionRevision()",
        "RecordMatchEvidenceResult( transition.reason, transition.authorizer,",
    ):
        require(effects, token, "exact-once committed transition effects")
    for forbidden in (
        "CommitCompetitionSeriesResult(",
        "CommitCompetitionSeriesMapEvidence(",
        ".CommitMapResult(",
        "matchSeries =",
        "matchSeriesReport =",
    ):
        if forbidden in effects:
            raise AssertionError(
                "committed transition effects publish series state before "
                f"evidence sealing via {forbidden!r}"
            )

    finalizer = bounded(
        multiplayer,
        "bool idMultiplayerGame::FinalizeMatchEvidence",
        "idMultiplayerGame::BeginMatchSession",
    )
    require(
        finalizer,
        "CommitCompetitionSeriesMapEvidence( evidenceStorage )",
        "evidence-sealed series result transaction",
    )
    if finalizer.index("CommitCompetitionSeriesMapEvidence( evidenceStorage )") > \
            finalizer.index("matchEvidenceFinalized = true"):
        raise AssertionError(
            "evidence is marked finalized before the series/report transaction commits"
        )

    series_seal = bounded(
        multiplayer,
        "bool idMultiplayerGame::CommitCompetitionSeriesMapEvidence",
        "void idMultiplayerGame::StartMatchMVDIfRequired",
    )
    for token in (
        "seriesCandidate.CommitMapResult(",
        "reportCandidate.AppendMapResult(",
        "PersistCompetitionSeriesCandidate( seriesCandidate, reportCandidate,",
        "matchSeries = seriesCandidate;",
        "matchSeriesReport = reportCandidate;",
    ):
        require(series_seal, token, "sealed series/report publication")
    if not (
        series_seal.index("seriesCandidate.CommitMapResult(")
        < series_seal.index("reportCandidate.AppendMapResult(")
        < series_seal.index(
            "PersistCompetitionSeriesCandidate( seriesCandidate, reportCandidate,"
        )
        < series_seal.index("matchSeries = seriesCandidate;")
        < series_seal.index("matchSeriesReport = reportCandidate;")
    ):
        raise AssertionError(
            "sealed series/report candidates are not checkpointed before publication"
        )

    mirror = bounded(
        multiplayer,
        "void idMultiplayerGame::ApplyMatchOperationLegacyMirror",
        "void idMultiplayerGame::ServerReceiveMatchOperation",
    )
    for token in (
        "MPOperationMapProtocolTeam( request.teamTarget, forfeitingSide )",
        "CommitMatchPhaseTransition( transition.to, transition.reason,",
        "transition.authorizer, forfeitingSide",
    ):
        require(mirror, token, "typed loser/reason propagation")

    tourney = bounded(
        game_state,
        "bool rvTourneyGameState::NewState",
        "rvTourneyGameState::GameStateChanged",
    )
    require(
        tourney,
        "GetMatchSession().GetPhase() != newState",
        "already-committed Tourney transition mirror",
    )


PROBE = r'''
#include "src/game/mp/match/MatchTerminationPolicy.h"

#include <stdio.h>

static int failures = 0;

static void CheckNone(mpGameState_t phase, bool enough, int side) {
    const mpMatchTerminationDecision decision =
        MPEvaluatePopulationTermination(phase, enough, side);
    if (decision.ShouldTransition() || decision.kind != MP_MATCH_TERMINATION_NONE ||
            decision.reason != MP_MATCH_TRANSITION_NONE) {
        ++failures;
    }
}

static void Check(mpGameState_t phase, int side,
        mpMatchTerminationKind_t kind, mpGameState_t target,
        mpMatchTransitionReason_t reason, int expectedSide) {
    const mpMatchTerminationDecision decision =
        MPEvaluatePopulationTermination(phase, false, side);
    if (!decision.ShouldTransition() || decision.kind != kind ||
            decision.targetPhase != target || decision.reason != reason ||
            decision.forfeitingSide != expectedSide) {
        ++failures;
    }
}

int main(void) {
    CheckNone(INACTIVE, false, 0);
    CheckNone(WARMUP, false, 0);
    CheckNone(GAMEREVIEW, false, 0);
    CheckNone(NEXTGAME, false, 0);
    CheckNone(COUNTDOWN, true, 0);
    CheckNone(GAMEON, true, 0);
    CheckNone(SUDDENDEATH, true, 1);

    Check(COUNTDOWN, 0, MP_MATCH_TERMINATION_COUNTDOWN_CANCELLED,
        WARMUP, MP_MATCH_TRANSITION_COUNTDOWN_ABORTED, MP_MATCH_SIDE_NONE);
    Check(COUNTDOWN, 1, MP_MATCH_TERMINATION_COUNTDOWN_CANCELLED,
        WARMUP, MP_MATCH_TRANSITION_COUNTDOWN_ABORTED, MP_MATCH_SIDE_NONE);
    Check(GAMEON, MP_MATCH_SIDE_NONE, MP_MATCH_TERMINATION_ABORTED,
        GAMEREVIEW, MP_MATCH_TRANSITION_MATCH_ABORTED, MP_MATCH_SIDE_NONE);
    Check(SUDDENDEATH, -2, MP_MATCH_TERMINATION_ABORTED,
        GAMEREVIEW, MP_MATCH_TRANSITION_MATCH_ABORTED, MP_MATCH_SIDE_NONE);
    Check(GAMEON, MP_MATCH_SIDE_COUNT, MP_MATCH_TERMINATION_ABORTED,
        GAMEREVIEW, MP_MATCH_TRANSITION_MATCH_ABORTED, MP_MATCH_SIDE_NONE);
    Check(GAMEON, 0, MP_MATCH_TERMINATION_FORFEIT,
        GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT, 0);
    Check(SUDDENDEATH, 1, MP_MATCH_TERMINATION_FORFEIT,
        GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT, 1);

    if (failures != 0) {
        fprintf(stderr, "termination policy failures: %d\n", failures);
        return 1;
    }
    return 0;
}
'''


EFFECTS_PROBE = r'''
#include "src/game/mp/match/MatchSeries.h"
#include "src/game/mp/match/MatchSession.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static const int GAME_DUEL = 9, GAME_DM = 0, CPARM_CLIENT = 0;
static const int MAX_CLIENTS = MP_MATCH_MAX_CONNECTION_SLOTS;
struct GameLocal {
    bool isServer = true, isClient = false;
    int gameType = GAME_DUEL, chatCount = 0;
    bool IsTeamGame() const { return false; }
    void ServerSendChatMessage(int to, const char *name, const char *text) {
        assert(to == -1 && std::strcmp(name, "server") == 0);
        assert(std::strcmp(text, "#str_41315") == 0);
        ++chatCount;
    }
} gameLocal;
struct Series {
    mpSeriesState_t state = MP_SERIES_DISABLED;
    mpSeriesState_t GetState() const { return state; }
};
struct idMultiplayerGame {
    mpMatchSession matchSession;
    Series matchSeries;
    bool managed = true, matchSessionOperational = true;
    bool finalizeSeries = false;
    uint64_t matchPhaseEffectsSessionId = 0, matchPhaseEffectsRevision = 0;
    uint64_t matchConnectionId[MAX_CLIENTS] = {};
    uint64_t matchSeriesCompetitionConnection[MAX_CLIENTS] = {};
    int matchSeriesCompetitionSide[MAX_CLIENTS] = {};
    int matchSeriesGameSideForCompetition[MP_SERIES_SIDE_COUNT] = {0, 1};
    int notices = 0, winnerSlot = -1, observations = 0, results = 0, recordings = 0;
    int synchronizations = 0, duelResults = 0, resultSide = -3, resultClears = 0;
    mpParticipantId resultAuthorizer;
    bool IsManagedMatch() const { return managed; }
    bool IsCompetitionSeriesModeSupported() const { return gameLocal.gameType == GAME_DUEL; }
    bool CollectCompetitionSeriesContestants(int slots[MP_SERIES_SIDE_COUNT],
            uint64_t connections[MP_SERIES_SIDE_COUNT]) const {
@COLLECT@
    }
    int ResolveCompetitionSide(mpParticipantId participant) const {
@SIDE@
    }
    bool ResolveManagedDuelForfeitParticipants(int forfeitingSide, mpParticipantId authorizer,
            mpParticipantId &forfeiter, mpParticipantId &winner) const {
@PARTICIPANTS@
    }
    void CenterPrint(int to, const char *key, int type, int slot) {
        assert(to == -1 && std::strcmp(key, "#str_41313") == 0 && type == CPARM_CLIENT);
        assert(slot >= 0 && slot < MAX_CLIENTS);
        assert(results == 0); // Side mapping must be resolved before series finalization.
        ++notices; winnerSlot = slot;
    }
    void SynchronizeAllMatchParticipants() { ++synchronizations; }
    void ObserveMatchEvidence(mpParticipantId) { ++observations; }
    void StartMatchMVDIfRequired() { ++recordings; }
    void ClearMatchTerminalResult() { ++resultClears; }
    void RecordManagedDuelResult(mpMatchTransitionReason_t, int, mpParticipantId) {
        ++duelResults;
    }
    void RecordMatchEvidenceResult(mpMatchTransitionReason_t, mpParticipantId authorizer, int side) {
        ++results; resultAuthorizer = authorizer; resultSide = side;
        if (finalizeSeries) matchSeries.state = MP_SERIES_COMPLETE;
    }
    bool ApplyCommittedMatchPhaseEffects(int forfeitingSide) {
@EFFECTS@
    }
};
struct Fixture {
    idMultiplayerGame mp;
    mpParticipantId players[3];
    Fixture() {
        gameLocal = GameLocal();
        assert(mp.matchSession.Reset(77, mpMatchEngineTime::FromMilliseconds(0)));
        Transition(WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED);
        mpMatchReadinessPolicy policy;
        policy.policy = MP_MATCH_READY_INDIVIDUAL;
        policy.minimumActiveHumans = 2;
        policy.readyThresholdBasisPoints = 10000;
        assert(!mp.matchSession.ConfigureReadiness(policy,
            mp.matchSession.GetSessionRevision()).WasRejected());
        for (int slot = 0; slot < 3; ++slot) {
            mp.matchConnectionId[slot] = 100 + slot;
            assert(!mp.matchSession.BindParticipant(slot, true, MPMatchRoleBit(
                slot == 2 ? MP_MATCH_ROLE_REFEREE : MP_MATCH_ROLE_PLAYER),
                mp.matchSession.GetSessionRevision(), players[slot]).WasRejected());
            if (slot < 2) {
                assert(!mp.matchSession.SetParticipantActive(players[slot], true,
                    mp.matchSession.GetSessionRevision()).WasRejected());
                assert(!mp.matchSession.SetParticipantReady(players[slot], true,
                    mp.matchSession.GetSessionRevision()).WasRejected());
            }
        }
    }
    void Transition(mpGameState_t phase, mpMatchTransitionReason_t reason,
            mpParticipantId authorizer = mpParticipantId::Invalid()) {
        assert(!mp.matchSession.TransitionPhase(phase, reason, authorizer,
            mp.matchSession.GetSessionRevision()).WasRejected());
    }
    void Live() {
        assert(!mp.matchSession.BeginCountdown(1, 42, MP_MATCH_TRANSITION_READY_GATE,
            mpParticipantId::Invalid(), mp.matchSession.GetSessionRevision()).WasRejected());
        Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
    }
    void Review(mpParticipantId authorizer, mpMatchTransitionReason_t reason = MP_MATCH_TRANSITION_FORFEIT) {
        Live(); Transition(GAMEREVIEW, reason, authorizer);
    }
    void AssertMessages(int chats, int notices, int winner = -1) const {
        assert(gameLocal.chatCount == chats && mp.notices == notices && mp.winnerSlot == winner);
    }
};
static void ForfeitAnnouncements() {
    for (int loser = 0; loser < 2; ++loser) {
        for (int actor = -1; actor < 3; ++actor) {
            Fixture f;
            const auto authorizer = actor < 0 ? mpParticipantId::Invalid() : f.players[actor];
            f.Review(authorizer);
            const auto revision = f.mp.matchSession.GetSessionRevision();
            assert(f.mp.ApplyCommittedMatchPhaseEffects(loser));
            f.AssertMessages(1, 1, 1-loser);
            assert(f.mp.results == 1 && f.mp.observations == 1 && f.mp.duelResults == 1);
            assert(f.mp.resultSide == loser && f.mp.resultAuthorizer == authorizer);
            assert(f.mp.matchSession.GetSessionRevision() == revision);
            assert(f.mp.ApplyCommittedMatchPhaseEffects(1-loser));
            f.AssertMessages(1, 1, 1-loser);
            assert(f.mp.results == 1 && f.mp.observations == 1 && f.mp.duelResults == 1);
        }
    }
    for (int actor = -1; actor < 3; ++actor) {
        Fixture f;
        f.Review(actor < 0 ? mpParticipantId::Invalid() : f.players[actor]);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(MP_MATCH_SIDE_NONE));
        const bool activeActor = actor >= 0 && actor < 2;
        f.AssertMessages(1, activeActor ? 1 : 0, activeActor ? 1-actor : -1);
    }
    for (int loser = 0; loser < 2; ++loser) {
        Fixture f;
        f.mp.matchSeries.state = MP_SERIES_MAP_ACTIVE;
        f.mp.finalizeSeries = true;
        for (int slot = 0; slot < 2; ++slot) {
            f.mp.matchSeriesCompetitionConnection[slot] = f.mp.matchConnectionId[slot];
            f.mp.matchSeriesCompetitionSide[slot] = 1-slot;
        }
        f.Review(f.players[2]);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(loser));
        f.AssertMessages(1, 1, loser); // Reversed series sides outrank slot order.
        assert(f.mp.matchSeries.state == MP_SERIES_COMPLETE);
    }
}
static void RejectedWinnerAndUnrelatedTransitions() {
    for (int invalidSide : {-2, 2, 99}) {
        Fixture f; f.Review(f.players[0]);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(invalidSide));
        f.AssertMessages(1, 0);
    }
    {
        Fixture f; f.Review(f.players[0]); f.mp.matchConnectionId[1] = 0;
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); f.AssertMessages(1, 0);
    }
    {
        Fixture f; f.Review(f.players[0]);
        f.mp.matchSeries.state = MP_SERIES_MAP_ACTIVE; // No valid connection-bound series sides.
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); f.AssertMessages(1, 0);
    }
    for (bool managed : {false, true}) {
        for (int mode : {GAME_DM, GAME_DUEL}) {
            Fixture f; f.mp.managed = managed; gameLocal.gameType = mode;
            f.Review(f.players[0]); assert(f.mp.ApplyCommittedMatchPhaseEffects(0));
            f.AssertMessages(managed && mode == GAME_DUEL ? 1 : 0,
                managed && mode == GAME_DUEL ? 1 : 0, managed && mode == GAME_DUEL ? 1 : -1);
        }
    }
    for (auto reason : {MP_MATCH_TRANSITION_MATCH_ABORTED, MP_MATCH_TRANSITION_LIMIT_REACHED}) {
        Fixture f; f.Review(f.players[0], reason);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); f.AssertMessages(0, 0);
        assert(f.mp.results == 1);
    }
    {
        Fixture f;
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0));
        assert(f.mp.synchronizations == 1);
        assert(f.mp.resultClears == 0); // Previous review remains visible in warmup.
        assert(!f.mp.matchSession.BeginCountdown(1, 42, MP_MATCH_TRANSITION_READY_GATE,
            mpParticipantId::Invalid(), f.mp.matchSession.GetSessionRevision()).WasRejected());
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); assert(f.mp.recordings == 1);
        assert(f.mp.resultClears == 1);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); assert(f.mp.resultClears == 1);
        f.Transition(GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0));
        f.Transition(SUDDENDEATH, MP_MATCH_TRANSITION_REGULATION_TIE);
        assert(f.mp.ApplyCommittedMatchPhaseEffects(0)); f.AssertMessages(0, 0);
    }
    {
        Fixture f; f.Review(f.players[0]); gameLocal.isServer = false;
        assert(!f.mp.ApplyCommittedMatchPhaseEffects(0)); f.AssertMessages(0, 0);
        assert(f.mp.results == 0 && f.mp.observations == 0);
    }
}
int main() {
    ForfeitAnnouncements(); RejectedWinnerAndUnrelatedTransitions();
    std::puts("committed Duel forfeit feedback, side authority and exact-once delivery: PASS");
}
'''


def effects_executable_contract(compiler: str, temporary: Path) -> None:
    source = MULTIPLAYER.read_text(encoding="utf-8")
    harness = EFFECTS_PROBE
    for marker, signature in (
        ("@COLLECT@", "bool idMultiplayerGame::CollectCompetitionSeriesContestants"),
        ("@SIDE@", "int idMultiplayerGame::ResolveCompetitionSide"),
        ("@PARTICIPANTS@", "bool idMultiplayerGame::ResolveManagedDuelForfeitParticipants"),
        ("@EFFECTS@", "bool idMultiplayerGame::ApplyCommittedMatchPhaseEffects"),
    ):
        harness = harness.replace(marker, function_body(source, signature))
    probe = temporary / "effects.cpp"
    probe.write_text(harness, encoding="utf-8")
    executable = temporary / "effects.exe"
    session_source = ROOT / "src/game/mp/match/MatchSession.cpp"
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c++17", "/EHsc",
                   "/DMP_MATCH_SESSION_STANDALONE_TEST", f"/I{ROOT}",
                   str(session_source), str(probe), f"/Fe:{executable}"]
    else:
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-DMP_MATCH_SESSION_STANDALONE_TEST", f"-I{ROOT}",
                   str(session_source), str(probe), "-o", str(executable)]
    subprocess.run(command, cwd=temporary, check=True)
    subprocess.run([str(executable)], cwd=temporary, check=True)


def executable_contract() -> None:
    compiler = next(
        (candidate for candidate in ("clang++", "g++", "cl") if shutil.which(candidate)),
        None,
    )
    if compiler is None:
        print(
            "mp_match_termination_policy_contract: executable checks skipped "
            "(no C++ compiler)"
        )
        return

    scratch_root = ROOT / ".tmp"
    scratch_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="mp-match-termination-", dir=scratch_root
    ) as directory:
        temporary = Path(directory)
        probe = temporary / "probe.cpp"
        probe.write_text(PROBE, encoding="utf-8")
        executable = temporary / (
            "probe.exe" if Path(compiler).name.lower() == "cl.exe" else "probe"
        )
        if Path(compiler).name.lower() in ("cl", "cl.exe"):
            command = [
                compiler,
                "/nologo",
                "/std:c++17",
                "/EHsc",
                "/DMP_MATCH_TERMINATION_STANDALONE",
                f"/I{ROOT}",
                str(SOURCE),
                str(probe),
                f"/Fe:{executable}",
            ]
        else:
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DMP_MATCH_TERMINATION_STANDALONE",
                f"-I{ROOT}",
                str(SOURCE),
                str(probe),
                "-o",
                str(executable),
            ]
        compiled = subprocess.run(
            command, cwd=ROOT, text=True, capture_output=True, check=False
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "termination policy probe did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run(
            [str(executable)], cwd=ROOT, text=True, capture_output=True, check=False
        )
        if ran.returncode != 0:
            raise AssertionError(
                "termination policy probe failed:\n" + ran.stdout + ran.stderr
            )
        effects_executable_contract(compiler, temporary)


def main() -> None:
    static_contracts()
    executable_contract()
    print("mp_match_termination_policy_contract: PASS")


if __name__ == "__main__":
    main()
