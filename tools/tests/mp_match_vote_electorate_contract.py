#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute the inherited vote paths with real eligibility and resolution code."""
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
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


STUBS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
const int LEGACY_VOTE_MSEC=20000, PACKED_VOTE_MSEC=30000;
const int MAX_CLIENTS = 32, MAX_GAME_MESSAGE_SIZE = 4096;
using byte = unsigned char;
enum vote_flags_t { VOTE_NONE, VOTE_MULTIFIELD, VOTE_TIMELIMIT, VOTE_KICK };
enum { PLAYER_VOTE_NONE, PLAYER_VOTE_WAIT, PLAYER_VOTE_YES, PLAYER_VOTE_NO };
enum { VOTE_RESET, VOTE_ABORTED, VOTE_PASSED, VOTE_FAILED, VOTE_UPDATE };
enum { GAME_RELIABLE_MESSAGE_STARTPACKEDVOTE, GAME_RELIABLE_MESSAGE_CASTVOTE, MP_RULE_MANAGED_MATCH };
enum { VOTEFLAG_KICK=1, VOTEFLAG_MAP=2, VOTEFLAG_GAMETYPE=4, VOTEFLAG_TIMELIMIT=8,
       VOTEFLAG_FRAGLIMIT=16, VOTEFLAG_TOURNEYLIMIT=32, VOTEFLAG_CAPTURELIMIT=64,
       VOTEFLAG_BUYING=128, VOTEFLAG_TEAMBALANCE=256, VOTEFLAG_CONTROLTIME=512 };
struct idEntity { bool player = false; int identity = 1; bool IsType(int) const { return player; } };
struct idPlayer : idEntity {
    idPlayer() { player = true; }
    bool spectating = false, wantSpectate = false, fake = false;
    bool IsFakeClient() const { return fake; }
    static int GetClassType() { return 1; }
};
struct Bots { bool slots[MAX_CLIENTS] = {}; bool IsBot(int slot) { return slots[slot]; } } botManager;
struct idBitMsg {
    void Init(byte*, int) {} void WriteByte(int) {} void WriteShort(int) {}
    void WriteString(const char*) {}
};
struct idMath { static int ClampChar(int x) { return x; } static int ClampShort(int x) { return x; } };
struct voteStruct_t {
    int m_fieldFlags = 0, m_kick = 0, m_gameType = 0, m_timeLimit = 0, m_fragLimit = 0,
        m_tourneyLimit = 0, m_captureLimit = 0, m_buying = 0, m_teamBalance = 0, m_controlTime = 0;
    std::string m_map;
};
struct Common {
    template<class... A> void Printf(const char*, A...) {}
    template<class... A> void DPrintf(const char*, A...) {}
    template<class... A> void Warning(const char*, A...) {}
} commonObject, *common = &commonObject;
struct Network {
    void ServerSendReliableMessage(int, const idBitMsg&) {}
    void ClientSendReliableMessage(const idBitMsg&) {}
} networkObject, *networkSystem = &networkObject;
struct Rules {
    bool managed = false;
    const Rules& Committed() const { return *this; }
    bool GetBool(int) const { return managed; }
};
struct idMultiplayerGame {
    struct PlayerState { bool ingame = false; int vote = PLAYER_VOTE_NONE; } playerState[MAX_CLIENTS];
    Rules matchRules;
    vote_flags_t vote = VOTE_NONE;
    int yesVotes = 0, noVotes = 0, voteEligibleCount = 0, voteTimeOut = 0, voteExecTime = 0;
    int voteKickIdentity = 0;
    int notifications = 0, lastNotice = -1, executions = 0, cooldowns = 0;
    bool voted = false;
    std::string voteValue;
    voteStruct_t currentVoteData;
    bool IsInGame(int slot) const { return playerState[slot].ingame; }
    // The managed cancellation path is exercised by the authority contracts.
    bool AbortInheritedVoteForManagedMatch() { return matchRules.managed; }
    void StampVoteRateLimit(int) { ++cooldowns; }
    void ClientUpdateVote(int notice, int, int, const voteStruct_t&) { ++notifications; lastNotice = notice; }
    void ExecuteVote() { ++executions; }
    void ServerStartVote(int clientNum, vote_flags_t index, const char* value);
    void ServerStartPackedVote(int clientNum, const voteStruct_t& data);
    void CastVote(int clientNum, bool castVote);
    void CheckVote();
    bool VoteKickTargetCurrent() const;
};
struct Game {
    bool isClient = false, isServer = true;
    int time = 1000, numClients = MAX_CLIENTS, localClientNum = 0;
    idEntity* entities[MAX_CLIENTS] = {};
    idMultiplayerGame mpGame;
    int GetSpawnId(const idEntity* entity) const { return entity->identity; }
    void ServerSendChatMessage(int, const char*, const char*) {}
} gameLocal;
'''

TESTS = r'''
idPlayer players[MAX_CLIENTS];
void Reset(int humans, int bots) {
    gameLocal = Game(); botManager = Bots();
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        players[i] = idPlayer();
        if (i < humans + bots) {
            gameLocal.entities[i] = &players[i];
            gameLocal.mpGame.playerState[i].ingame = true;
            botManager.slots[i] = i >= humans;
        }
    }
}
void Start(bool packed, int caller = 0) {
    auto& mp = gameLocal.mpGame;
    if (packed) {
        voteStruct_t data; data.m_fieldFlags = VOTEFLAG_TIMELIMIT; data.m_timeLimit = 5;
        mp.ServerStartPackedVote(caller, data);
    } else {
        mp.ServerStartVote(caller, VOTE_TIMELIMIT, "5");
    }
}
void Eligibility() {
    Reset(2, 2);
    CHECK(!IsEligibleVotePlayerSlot(-1) && !IsEligibleVotePlayerSlot(MAX_CLIENTS));
    CHECK(IsEligibleVotePlayerSlot(0) && IsEligibleVotePlayerSlot(1));
    CHECK(!IsEligibleVotePlayerSlot(2) && !IsEligibleVotePlayerSlot(3));
    CHECK(!IsEligibleVotePlayerSlot(4));
    gameLocal.numClients = 1; CHECK(!IsEligibleVotePlayerSlot(1)); gameLocal.numClients = MAX_CLIENTS;
    auto& p = players[1];
    p.spectating = true; CHECK(!IsEligibleVotePlayerSlot(1)); p.spectating = false;
    p.wantSpectate = true; CHECK(!IsEligibleVotePlayerSlot(1)); p.wantSpectate = false;
    p.fake = true; CHECK(!IsEligibleVotePlayerSlot(1)); p.fake = false;
    gameLocal.mpGame.playerState[1].ingame = false; CHECK(!IsEligibleVotePlayerSlot(1));
    idEntity prop; gameLocal.entities[1] = &prop; CHECK(!IsEligibleVotePlayerSlot(1));
}
void Lifecycle(bool packed) {
    auto& mp = gameLocal.mpGame;
    Reset(1, 6); Start(packed);
    CHECK(mp.voteEligibleCount == 1 && mp.yesVotes == 1 && mp.cooldowns == 1);
    for (int i = 1; i < 7; ++i) {
        CHECK(mp.playerState[i].vote == PLAYER_VOTE_NONE);
        mp.CastVote(i, false);
    }
    CHECK(mp.noVotes == 0);
    mp.CheckVote(); CHECK(mp.lastNotice == VOTE_PASSED && mp.voteExecTime == 3000);
    gameLocal.time = 3000; mp.CheckVote(); CHECK(mp.executions == 0);
    gameLocal.time = 3001; mp.CheckVote(); CHECK(mp.executions == 1 && mp.vote == VOTE_NONE);
    mp.CheckVote(); CHECK(mp.executions == 1);

    // Invalid callers must not seed an automatic yes vote or take a cooldown.
    for (int caller : {-1, 2, 4, MAX_CLIENTS}) {
        Reset(2, 2); Start(packed, caller);
        CHECK(mp.vote == VOTE_NONE && mp.cooldowns == 0 && mp.yesVotes == 0);
    }
    Reset(2, 2); gameLocal.isServer = false; Start(packed);
    CHECK(mp.vote == VOTE_NONE && mp.yesVotes == 0);

    // A strict majority of humans is needed; bot count cannot block a pass.
    Reset(3, 20); Start(packed);
    CHECK(mp.voteEligibleCount == 3);
    mp.CheckVote(); CHECK(mp.voteExecTime == 0);
    mp.CastVote(1, true); mp.CastVote(1, true);
    CHECK(mp.yesVotes == 2);
    mp.CheckVote(); CHECK(mp.lastNotice == VOTE_PASSED);

    Reset(4, 20); Start(packed); mp.CastVote(1, true);
    mp.CheckVote(); CHECK(mp.voteExecTime == 0);
    mp.CastVote(2, false); mp.CheckVote(); CHECK(mp.vote != VOTE_NONE);
    mp.CastVote(3, false); mp.CheckVote(); CHECK(mp.lastNotice == VOTE_FAILED && mp.vote == VOTE_NONE);

    // Disconnecting and replacing a slot does not alter the original threshold
    // or create a ballot. The real connect/disconnect adapters clear playerState.
    Reset(3, 0); Start(packed);
    gameLocal.entities[1] = nullptr; mp.playerState[1] = {};
    gameLocal.entities[2] = nullptr; mp.playerState[2] = {};
    CHECK(mp.voteEligibleCount == 3);
    mp.CheckVote(); CHECK(mp.voteExecTime == 0);
    gameLocal.entities[1] = &players[1]; mp.playerState[1].ingame = true;
    mp.CastVote(1, true); CHECK(mp.yesVotes == 1);
    gameLocal.time = mp.voteTimeOut + 1; mp.CheckVote();
    CHECK(mp.lastNotice == VOTE_FAILED && mp.vote == VOTE_NONE);
}
void KickIdentity(bool packed) {
    Reset(2, 0);
    auto& mp = gameLocal.mpGame;
    players[1].identity = 8193;
    if (packed) {
        voteStruct_t data; data.m_fieldFlags = VOTEFLAG_KICK | VOTEFLAG_TIMELIMIT;
        data.m_kick = 1; data.m_timeLimit = 5;
        mp.ServerStartPackedVote(0, data);
    } else {
        mp.ServerStartVote(0, VOTE_KICK, "1");
    }
    CHECK(mp.voteKickIdentity == 8193 && mp.VoteKickTargetCurrent());
    mp.CastVote(1, true); mp.CheckVote();
    CHECK(mp.voteExecTime == 3000);
    // The object can be reused at the same address. Only its identity changed.
    players[1].identity = 12289;
    gameLocal.time = 3001; mp.CheckVote();
    CHECK(mp.executions == 0 && mp.vote == VOTE_NONE && mp.lastNotice == VOTE_ABORTED);
    Start(packed); CHECK(mp.voteKickIdentity == 0 && mp.VoteKickTargetCurrent());
}
int main() {
    Eligibility(); Lifecycle(false); Lifecycle(true);
    KickIdentity(false); KickIdentity(true);
    std::puts("human/bot electorate, both transports, majority, slot reuse and execution delay: PASS");
}
'''


def main() -> None:
    source = (ROOT / "src/mpgame/MultiplayerGame.cpp").read_text(encoding="utf-8")
    functions = [function(source, signature) for signature in (
        "static bool IsValidVotePlayerSlot(", "static bool IsCurrentPlayerTarget(",
        "static bool ParseBoundedVoteInteger(", "static bool ParseVotePlayerSlot(",
        "static bool IsEligibleVotePlayerSlot(", "bool idMultiplayerGame::VoteKickTargetCurrent(",
        "void idMultiplayerGame::ServerStartVote(", "void idMultiplayerGame::ServerStartPackedVote(",
        "void idMultiplayerGame::CastVote(", "void idMultiplayerGame::CheckVote(",
    )]
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for vote regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vote-electorate-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "vote.cpp", path / ("vote.exe" if os.name == "nt" else "vote")
        cpp.write_text(STUBS + "\n".join(functions) + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_vote_electorate_contract: PASS")


if __name__ == "__main__":
    main()
