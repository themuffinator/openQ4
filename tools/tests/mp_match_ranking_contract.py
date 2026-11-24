#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute production MP ranking, containers and Duel state/queue code."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_flow_contract import function

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define ID_INLINE inline
#include "idlib/containers/List.h"
#include "idlib/containers/Pair.h"
#include "game/mp/MatchPhase.h"
#include "game/mp/GameTypeIds.h"
static unsigned arrayAllocations = 0;
void *operator new[](std::size_t size) {
    ++arrayAllocations;
    if (void *p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
const int MAX_CLIENTS = 32, MP_PLAYER_MINFRAGS = -100, MP_PLAYER_MAXFRAGS = 999;
const int MAX_GAME_MESSAGE_SIZE = 1024, GAME_RELIABLE_MESSAGE_GAMESTATE = 77;
typedef unsigned char byte;
typedef int gameStateType_t;
const gameStateType_t GS_DUEL = 6;
struct idMath { static int ClampInt(int lo, int hi, int n) { return std::max(lo, std::min(hi,n)); } };
struct idEntity {
    virtual ~idEntity() = default;
    virtual bool IsType(int) const { return false; }
};
struct { bool IsBot(int) const { return false; } } botManager;
struct idPlayer : idEntity {
    int entityNumber = 0, team = 0, rank = -1, rankWrites = 0;
    bool spectating = false, wantSpectate = false, forceRespawn = false;
    bool IsType(int) const override { return true; }
    static int GetClassType() { return 1; }
    void SetRank(int n) { rank = n; ++rankWrites; }
    int GetRank() const { return rank; }
    void ServerSpectate(bool n) { spectating = n; }
};
struct idBitMsg {
    std::vector<byte> bytes;
    mutable unsigned pos = 0;
    void Init(byte *, int) { bytes.clear(); pos = 0; }
    void WriteByte(int n) { bytes.push_back(byte(n)); }
    void WriteLong(int n) { for (int i = 0; i < 4; ++i) WriteByte(unsigned(n) >> (i*8)); }
    int ReadByte() const { CHECK(pos < bytes.size()); return bytes[pos++]; }
    int ReadLong() const { unsigned n = 0; for (int i = 0; i < 4; ++i) n |= unsigned(ReadByte()) << (i*8); return int(n); }
    int GetRemainingReadBits() const { return int(bytes.size() - pos) * 8; }
};
struct idMessageSender {
    mutable std::vector<idBitMsg> messages;
    void Send(const idBitMsg &msg) const { messages.push_back(msg); }
};
struct Common {
    template<class... T> void Warning(const char *, T...) {}
    const char *GetLocalizedString(const char *n) { return n; }
} commonValue, *common = &commonValue;
#define S_COLOR_BLUE ""
#define S_COLOR_RED ""
#define S_COLOR_YELLOW ""
char *va(const char *format, ...) {
    static char buffer[512]; va_list args; va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
}
class rvGameState {
public:
    mpGameState_t currentState = INACTIVE, nextState = INACTIVE;
    int nextStateTime = 0, overtimeCount = 0, overtimeAccumulatedMsec = 0, overtimeStartTime = 0;
    rvGameState *previousGameState;
    bool trackPrevious, acceptTransition = true;
    int notifications = 0;
    rvGameState(bool previous = true) : previousGameState(previous ? new rvGameState(false) : nullptr), trackPrevious(previous) {}
    virtual ~rvGameState() { delete previousGameState; }
    virtual void Clear() { currentState = nextState = INACTIVE; nextStateTime = overtimeCount = overtimeAccumulatedMsec = overtimeStartTime = 0; }
    virtual bool IsType(gameStateType_t) const { return false; }
    virtual bool NewState(mpGameState_t s);
    virtual void Run() {}
    virtual void ClientDisconnect(idPlayer *) {}
    virtual bool AllowRespawn(idPlayer *) { return true; }
    virtual void SendState(const idMessageSender &, int = -1) {}
    virtual void ReceiveState(const idBitMsg &) {}
    virtual void PackState(idBitMsg &);
    virtual void WriteState(idBitMsg &msg) { PackState(msg); }
    void SendInitialState(const idMessageSender &, int);
    bool BaseUnpackState(const idBitMsg &);
    void GameStateChanged() { ++notifications; }
    mpGameState_t GetMPGameState() { return currentState; }
    mpGameState_t GetNextMPGameState() { return nextState; }
    int GetNextMPGameStateTime() { return nextStateTime; }
    int GetOvertimeMsec() { return overtimeAccumulatedMsec; }
    int GetOvertimeStartTime() { return overtimeStartTime; }
    bool operator==(const rvGameState &) const;
    rvGameState &operator=(const rvGameState &);
};
class rvDMGameState : public rvGameState { public: using rvGameState::rvGameState; };
'''

GAME = r'''
struct idMultiplayerGame {
    struct Score { int fragCount = 0, teamFragCount = 0, wins = 0; bool ingame = true; } playerState[MAX_CLIENTS];
    idList<rvPair<idPlayer*, int>> rankedPlayers;
    idList<idPlayer*> unrankedPlayers;
    rvPair<int, int> rankedTeams[TEAM_MAX];
    int teamScore[TEAM_MAX] = {};
    rvGameState *gameState = nullptr;
    bool CanPlay(idPlayer *);
    bool IsRankedParticipant(idPlayer *);
    int GetPlayerRank(idPlayer *, bool &);
    char *GetPlayerRankText(int, bool, int);
    void UpdatePlayerRanks(playerRankMode_t = PRM_AUTO);
    void UpdateTeamRanks();
    idPlayer *GetRankLeader();
    int GetScore(idPlayer *p) { return playerState[p->entityNumber].fragCount; }
    int GetTeamScore(idPlayer *p) { return playerState[p->entityNumber].teamFragCount; }
    int GetWins(idPlayer *p) { return playerState[p->entityNumber].wins; }
    int GetScoreForTeam(int);
    bool managed = false, rotationAllowed = false;
    int rotationCalls = 0, retiredSlot = -2;
    bool IsManagedMatch() const { return managed; }
    bool RotateManagedDuelQueue(int first, int second, int loser) {
        CHECK(managed && first == 0 && second == 1);
        ++rotationCalls; retiredSlot = loser; return rotationAllowed;
    }
};
struct GameLocal {
    idMultiplayerGame mpGame;
    int numClients = 0, localClientNum = -1;
    gameType_t gameType = GAME_DM;
    bool teamGame = false, isServer = true, isClient = false, isRepeater = false;
    idEntity *entities[MAX_CLIENTS] = {};
    bool IsTeamGame() { return teamGame; }
    bool RetailSnapshotFormat() const { return false; }  // MP-22: no retail network demo plays here
    template<class... T> void Error(const char *, T...) { throw std::runtime_error("game error"); }
    template<class... T> void Warning(const char *, T...) {}
} gameLocal;
bool rvGameState::NewState(mpGameState_t state) {
    if (!acceptTransition) return false;
    currentState = state;
    if (state == GAMEREVIEW) for (auto *e : gameLocal.entities)
        if (e && e->IsType(idPlayer::GetClassType())) static_cast<idPlayer *>(e)->ServerSpectate(true);
    return true;
}
'''

TESTS = r'''
static idPlayer players[MAX_CLIENTS];
static void Reset(int count) {
    gameLocal.numClients = count; gameLocal.gameType = GAME_DM; gameLocal.teamGame = false;
    gameLocal.localClientNum = -1; gameLocal.isServer = true; gameLocal.isClient = false;
    gameLocal.mpGame.gameState = nullptr;
    gameLocal.mpGame.managed = false; gameLocal.mpGame.rotationAllowed = false;
    gameLocal.mpGame.rotationCalls = 0; gameLocal.mpGame.retiredSlot = -2;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        players[i] = idPlayer(); players[i].entityNumber = i;
        gameLocal.entities[i] = i < count ? &players[i] : nullptr;
        gameLocal.mpGame.playerState[i] = {};
    }
    gameLocal.mpGame.UpdatePlayerRanks();
}
static void CheckPlaces() {
    auto &mp = gameLocal.mpGame;
    for (int i = 0; i < mp.rankedPlayers.Num(); ++i) {
        const auto &entry = mp.rankedPlayers[i];
        int ahead = 0, equal = 0;
        for (int j = 0; j < mp.rankedPlayers.Num(); ++j) {
            ahead += mp.rankedPlayers[j].Second() > entry.Second();
            equal += mp.rankedPlayers[j].Second() == entry.Second();
        }
        bool tied = false;
        CHECK(entry.First()->GetRank() == ahead);
        CHECK(mp.GetPlayerRank(entry.First(), tied) == ahead && tied == (equal > 1));
        if (i > 0) CHECK(ComparePlayersByScore(&mp.rankedPlayers[i-1], &entry) < 0);
    }
}
static void Ranking() {
    auto &mp = gameLocal.mpGame;
    Reset(6);
    const int scores[] = {10, 10, 9, 8, 8, -3};
    for (int i = 0; i < 6; ++i) mp.playerState[i].fragCount = scores[i];
    mp.UpdatePlayerRanks(); CheckPlaces();
    CHECK(players[2].GetRank() == 2 && players[3].GetRank() == 3 && players[5].GetRank() == 5);
    // Queries must not depend on a previous player's mutable rank field.
    players[0].SetRank(999);
    bool tied = false;
    CHECK(mp.GetPlayerRank(&players[1], tied) == 0 && tied);
    tied = true; CHECK(mp.GetPlayerRank(nullptr, tied) == -1 && !tied);
    CHECK(std::strcmp(mp.GetPlayerRankText(-1, true, 8), "") == 0);
    players[0].wantSpectate = true; mp.playerState[1].ingame = false;
    mp.UpdatePlayerRanks(); CheckPlaces();
    CHECK(players[0].GetRank() == -1 && players[1].GetRank() == -1);
    CHECK(mp.unrankedPlayers.Num() == 2);
    const auto allocations = arrayAllocations;
    for (int i = 0; i < 1000; ++i) mp.UpdatePlayerRanks();
    CHECK(arrayAllocations == allocations); // Actual idList storage, not a vector stub.
    Reset(32); std::mt19937 random(7319);
    for (int pass = 0; pass < 1000; ++pass) {
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            mp.playerState[i].fragCount = int(random() % 17) - 8;
            players[i].wantSpectate = random() % 7 == 0;
            players[i].rankWrites = 0;
        }
        mp.UpdatePlayerRanks(); CheckPlaces();
        for (int i = 0; i < MAX_CLIENTS; ++i) CHECK(players[i].rankWrites <= 2);
    }
    Reset(3);
    mp.playerState[0].fragCount = INT_MIN; mp.playerState[1].fragCount = INT_MAX;
    mp.playerState[2].fragCount = -1;
    mp.UpdatePlayerRanks(); CheckPlaces(); CHECK(mp.rankedPlayers[0].First() == &players[1]);
    for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) {
        const auto &x = mp.rankedPlayers[a], &y = mp.rankedPlayers[b];
        CHECK(ComparePlayersByScore(&x,&y) == -ComparePlayersByScore(&y,&x));
    }
    mp.teamScore[0] = INT_MIN; mp.teamScore[1] = INT_MAX;
    mp.UpdateTeamRanks(); CHECK(mp.rankedTeams[0].First() == 1);
    mp.teamScore[0] = mp.teamScore[1]; mp.UpdateTeamRanks(); CHECK(mp.rankedTeams[0].First() == 0);
    CHECK(mp.GetScoreForTeam(-1) == 0 && mp.GetScoreForTeam(TEAM_MAX) == 0);
    // A disconnected/replaced slot must never inherit the old object's rank.
    idPlayer replacement; replacement.entityNumber = 1;
    gameLocal.entities[1] = &replacement;
    CHECK(!mp.CanPlay(&players[1]));
    mp.UpdatePlayerRanks(); CHECK(mp.GetRankLeader() == &replacement);
    gameLocal.entities[1] = nullptr; mp.UpdatePlayerRanks();
    CHECK(mp.GetPlayerRank(&replacement, tied) == -1);
    gameLocal.entities[1] = &players[1]; players[1].entityNumber = MAX_CLIENTS;
    CHECK(!mp.CanPlay(&players[1]) && !mp.CanPlay(nullptr));
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers.Num() == 2 && players[1].GetRank() == -1);
    idEntity nonPlayer; gameLocal.entities[2] = &nonPlayer;
    gameLocal.numClients = MAX_CLIENTS + 8; mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers.Num() == 1);
    Reset(0); CHECK(mp.rankedPlayers.Num() == 0 && mp.GetRankLeader() == nullptr);
}
static void ModesAndWinners() {
    Reset(4); auto &mp = gameLocal.mpGame;
    gameLocal.teamGame = true;
    for (int i = 0; i < 4; ++i) {
        mp.playerState[i].fragCount = 9-i;
        mp.playerState[i].teamFragCount = i*3;
        mp.playerState[i].wins = i*2;
    }
    players[3].spectating = true; // Elimination is still ranked.
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers[0].First() == &players[3]); CheckPlaces();
    mp.UpdatePlayerRanks(PRM_SCORE); CHECK(mp.rankedPlayers[0].First() == &players[0]);
    mp.UpdatePlayerRanks(PRM_TEAM_SCORE); CHECK(mp.rankedPlayers[0].Second() == 9);
    mp.playerState[0].fragCount = mp.playerState[0].teamFragCount = INT_MAX;
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers[0].Second() == 2*MP_PLAYER_MAXFRAGS);
    players[0].team = TEAM_MAX; mp.UpdatePlayerRanks(); CHECK(players[0].GetRank() == -1);
    gameLocal.gameType = GAME_TOURNEY; gameLocal.teamGame = false;
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers[0].First() == &players[3]); // Wins, including byes.
    Reset(3);
    mp.playerState[0].fragCount = 1; mp.playerState[1].fragCount = 2;
    mp.UpdatePlayerRanks(); mp.playerState[0].fragCount = 3;
    CHECK(mp.GetRankLeader() == &players[0]); // No intervening HUD update.
    mp.playerState[1].fragCount = 3; CHECK(mp.GetRankLeader() == nullptr);
    players[1].wantSpectate = true; CHECK(mp.GetRankLeader() == &players[0]);
    players[2].wantSpectate = true; CHECK(mp.GetRankLeader() == nullptr);
}
static void Duel() {
    Reset(3); auto &mp = gameLocal.mpGame; gameLocal.gameType = GAME_DUEL;
    rvDuelGameState state; mp.gameState = &state;
    state.UpdateQueue(); state.PromoteFromQueue();
    CHECK(state.IsContender(0) && state.IsContender(1) && !state.IsContender(2));
    mp.playerState[0].fragCount = -2; mp.playerState[1].fragCount = -3; mp.playerState[2].fragCount = 999;
    // A queue player briefly out of spectator still cannot rank or win.
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers.Num() == 2 && players[2].GetRank() == -1);
    CHECK(mp.GetRankLeader() == &players[0]);
    state.NewState(GAMEREVIEW);
    CHECK(players[0].spectating && players[1].spectating);
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers.Num() == 2 && mp.GetRankLeader() == &players[0]);
    CHECK(state.IsContender(1)); // Do not rotate the loser off the final board.
    idMessageSender sender;
    state.SendState(sender); CHECK(sender.messages.size() == 1);
    state.SendState(sender); CHECK(sender.messages.size() == 1);
    state.SendInitialState(sender, 5); CHECK(sender.messages.size() == 2);
    rvDuelGameState client;
    idBitMsg wire = sender.messages.back(); wire.ReadByte(); client.ReceiveState(wire);
    CHECK(client.IsContender(0) && client.IsContender(1) && !client.IsContender(2));
    mp.gameState = &client; gameLocal.isServer = false; gameLocal.isClient = true;
    mp.UpdatePlayerRanks(); CHECK(mp.rankedPlayers.Num() == 2 && players[2].GetRank() == -1);
    // Every truncation and an extra byte leave both phase and seats unchanged.
    idBitMsg valid; state.PackState(valid); CHECK(valid.bytes.size() == 18);
    for (int length = 0; length < 18; ++length) {
        idBitMsg broken = valid; broken.bytes.resize(length); client.ReceiveState(broken);
        CHECK(client.IsContender(1) && client.GetMPGameState() == GAMEREVIEW);
    }
    idBitMsg broken = valid; broken.WriteByte(0); client.ReceiveState(broken);
    CHECK(client.IsContender(1));
    for (int slot = MAX_CLIENTS; slot < 255; ++slot) {
        broken = valid; broken.bytes[1] = GAMEON; broken.bytes[16] = byte(slot); client.ReceiveState(broken);
        CHECK(client.IsContender(0) && client.GetMPGameState() == GAMEREVIEW);
    }
    broken = valid; broken.bytes[17] = broken.bytes[16]; client.ReceiveState(broken);
    CHECK(client.IsContender(1));
    gameLocal.isServer = true; gameLocal.isClient = false; mp.gameState = &state;
    state.acceptTransition = false; CHECK(!state.NewState(NEXTGAME) && state.IsContender(1));
    state.acceptTransition = true; state.NewState(NEXTGAME); state.PromoteFromQueue();
    CHECK(state.IsContender(0) && state.IsContender(2) && state.GetQueuePosition(1) == 1);
    state.SendState(sender); CHECK(sender.messages.size() == 3);
    state.contenders[1] = 1; // Seat change alone must trigger replication.
    state.SendInitialState(sender, 6); CHECK(sender.messages.size() == 4);
    state.SendState(sender); CHECK(sender.messages.size() == 5);
    mp.gameState = nullptr;
}
static void ManagedDuelAbort() {
    for (int tied = 0; tied < 2; ++tied) {
        Reset(3); gameLocal.gameType = GAME_DUEL;
        auto &mp = gameLocal.mpGame; mp.managed = mp.rotationAllowed = true;
        rvDuelGameState state; mp.gameState = &state;
        players[2].wantSpectate = true;
        state.UpdateQueue(); state.PromoteFromQueue();
        mp.playerState[0].fragCount = 7;
        mp.playerState[1].fragCount = tied ? 7 : -3;
        state.NewState(GAMEREVIEW); state.CancelTurnover();
        CHECK(state.IsContender(0) && state.IsContender(1));
        state.NewState(NEXTGAME);
        CHECK(mp.rotationCalls == 0 && state.IsContender(0) && state.IsContender(1));
        CHECK(state.queue.Num() == 0);
        // The same two contestants can finish a later game normally.
        state.NewState(WARMUP); CHECK(!state.turnoverCancelled);
        mp.playerState[0].fragCount = 2; mp.playerState[1].fragCount = 1;
        state.NewState(GAMEON); state.NewState(GAMEREVIEW); state.NewState(NEXTGAME);
        CHECK(mp.rotationCalls == 1 && mp.retiredSlot == 1);
        CHECK(state.IsContender(0) && !state.IsContender(1));
        state.CancelTurnover(); state.Clear(); CHECK(!state.turnoverCancelled);
        mp.gameState = nullptr;
    }
}
static void ManagedDuelDispatch() {
    for (int loser = -1; loser < 2; ++loser) {
        for (int allow = 0; allow < 2; ++allow) {
            Reset(3); gameLocal.gameType = GAME_DUEL;
            auto &mp = gameLocal.mpGame; mp.managed = true; mp.rotationAllowed = allow != 0;
            rvDuelGameState state; mp.gameState = &state;
            players[2].wantSpectate = true; // The managed FIFO has no legacy Play entry.
            state.UpdateQueue(); state.PromoteFromQueue();
            CHECK(state.IsContender(0) && state.IsContender(1) && state.queue.Num() == 0);
            mp.playerState[0].fragCount = loser == 0 ? 4 : 5;
            mp.playerState[1].fragCount = loser == 1 ? 4 : 5;
            state.NewState(GAMEREVIEW);
            CHECK(mp.rotationCalls == 0 && state.IsContender(0) && state.IsContender(1));
            state.NewState(NEXTGAME);
            CHECK(mp.rotationCalls == 1 && mp.retiredSlot == loser);
            CHECK(state.IsContender(0) == (!allow || loser == 1));
            CHECK(state.IsContender(1) == (!allow || loser == 0));
            CHECK(state.queue.Num() == 0); // Managed transactions never feed the legacy queue.
            mp.gameState = nullptr;
        }
    }
    for (int forfeiter = 0; forfeiter < 2; ++forfeiter) {
        Reset(3); gameLocal.gameType = GAME_DUEL;
        auto &mp = gameLocal.mpGame; mp.managed = mp.rotationAllowed = true;
        rvDuelGameState state; mp.gameState = &state;
        players[2].wantSpectate = true;
        state.UpdateQueue(); state.PromoteFromQueue();
        mp.playerState[forfeiter].fragCount = 10;
        mp.playerState[1-forfeiter].fragCount = 0;
        state.NewState(GAMEREVIEW); state.SetForfeitingContender(forfeiter);
        state.NewState(NEXTGAME);
        CHECK(mp.retiredSlot == forfeiter && !state.IsContender(forfeiter));
        CHECK(state.IsContender(1-forfeiter));
        state.NewState(WARMUP); CHECK(state.forfeitingContender == -1);
        mp.gameState = nullptr;
    }
    Reset(3); gameLocal.gameType = GAME_DUEL;
    auto &mp = gameLocal.mpGame; mp.managed = mp.rotationAllowed = true;
    rvDuelGameState state; mp.gameState = &state;
    state.UpdateQueue(); state.PromoteFromQueue();
    state.NewState(GAMEREVIEW); state.SetForfeitingContender(31);
    state.NewState(NEXTGAME);
    CHECK(mp.rotationCalls == 0 && state.IsContender(0) && state.IsContender(1));
    mp.gameState = nullptr;
}
int main() {
    Ranking(); ModesAndWinners(); Duel(); ManagedDuelDispatch(); ManagedDuelAbort();
    std::puts("production ranks, storage reuse, eligibility, winners and Duel review/replication: PASS");
}
'''


def main() -> None:
    mp = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/game/MultiplayerGame.h").read_text(encoding="utf-8")
    states = (ROOT / "src/game/mp/GameState.cpp").read_text(encoding="utf-8")
    duel_header = (ROOT / "src/game/mp/Duel.h").read_text(encoding="utf-8")
    duel = (ROOT / "src/game/mp/Duel.cpp").read_text(encoding="utf-8")
    enum_end = header.index("} playerRankMode_t;") + len("} playerRankMode_t;")
    enum_start = header.rindex("typedef enum", 0, enum_end)
    evidence = function(mp, "void idMultiplayerGame::RecordMatchEvidenceResult(")
    terminal = function(mp, "mpEvidenceMapResult idMultiplayerGame::BuildMatchTerminalEvidenceResult(")
    assert "GetRankLeader();" in terminal and "rankedPlayers[ 0 ]" not in terminal
    assert "BuildMatchTerminalEvidenceResult( reason, authorizer, forfeitingSide )" in evidence
    assert "frozen ? matchTerminalEvidenceResult" in evidence
    assert "IsRankedParticipant( player )" in function(mp, "void idMultiplayerGame::SynchronizeMatchParticipant(")
    assert '"gametype", gameLocal.gameType == GAME_DUEL ? GAME_DM : gameLocal.gameType' in function(mp, "void idMultiplayerGame::UpdateScoreboard(")
    bodies = [function(mp, signature) for signature in (
        "int ComparePlayersByScore(", "int CompareTeamsByScore(",
        "bool idMultiplayerGame::CanPlay(", "bool idMultiplayerGame::IsRankedParticipant(",
        "int idMultiplayerGame::GetPlayerRank(", "void idMultiplayerGame::UpdatePlayerRanks(",
        "void idMultiplayerGame::UpdateTeamRanks(", "idPlayer *idMultiplayerGame::GetRankLeader(",
        "char* idMultiplayerGame::GetPlayerRankText( int rank,",
    )]
    bodies += [function(header, "ID_INLINE int idMultiplayerGame::GetScoreForTeam(")]
    bodies += [function(states, signature) for signature in (
        "void rvGameState::PackState(", "bool rvGameState::BaseUnpackState(",
        "bool rvGameState::operator==(", "rvGameState& rvGameState::operator=(",
        "void rvGameState::SendInitialState(",
    )]
    duel_header = duel_header.replace('#include "GameState.h"', '').replace('\nprivate:', '\npublic:')
    duel = '\n'.join(line for line in duel.splitlines() if not line.startswith(('#include', '#pragma')))
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for ranking regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-ranking-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        source = path / "ranking.cpp"
        exe = path / ("ranking.exe" if os.name == "nt" else "ranking")
        source.write_text(STUBS + duel_header + header[enum_start:enum_end] + GAME + '\n'.join(bodies) + duel + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wno-writable-strings", f"-I{ROOT / 'src'}", str(source), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_ranking_contract: PASS")


if __name__ == "__main__":
    main()
