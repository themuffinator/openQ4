#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Compile the managed userinfo adapter and verify owner/peer publication order."""
from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    body = function_body(source, "void idMultiplayerGame::ApplyMatchTeamsPlanToLegacy")
    player = (ROOT / "src/game/Player.cpp").read_text(encoding="utf-8")
    initial_join = function_body(player, "if ( gameLocal.isServer && managedMatch )")
    synchronize = function_body(source, "void idMultiplayerGame::SynchronizeMatchParticipant")
    retry_anchor = "if ( IsManagedMatch() && player->initialJoinPending"
    retry_start = synchronize.index(retry_anchor)
    retry = (synchronize[retry_start:synchronize.index("{", retry_start) + 1]
             + function_body(synchronize, retry_anchor) + "}")
    admission = synchronize[synchronize.index("const mpMatchParticipantState *acceptedDuel"):
                            synchronize.index("const int side =")]
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct idDict {
    std::map<std::string, std::string> values;
    void Set(const char *key, const char *value) { values[key] = value; }
    const std::string *FindKey(const char *key) const {
        auto found = values.find(key);
        return found == values.end() ? nullptr : &found->second;
    }
    void SetBool(const char *key, bool value) { Set(key, value ? "1" : "0"); }
    bool GetBool(const char *key, const char *fallback) const {
        auto found = values.find(key);
        return (found == values.end() ? std::string(fallback) : found->second) == "1";
    }
    const char *GetString(const char *key) const {
        auto found = values.find(key);
        return found == values.end() ? "" : found->second.c_str();
    }
};
struct mpParticipantId {
    int value = 0;
    bool IsValid() const { return value > 0 && value < 3; }
    bool operator==(mpParticipantId other) const { return value == other.value; }
};
struct mpMatchParticipantState { bool active = false; int side = -1; };
struct mpMatchTeamsTransactionPlan_t { mpParticipantId incomingParticipant, outgoingParticipant; };
static const int INACTIVE = 0, WARMUP = 1, GAME_DUEL = 9, GAME_DM = 0;
struct Session {
    int phase = INACTIVE;
    int GetPhase() const { return phase; }
    mpMatchParticipantState states[3];
    int slots[3] = {-1, 0, 1};
    bool bound[3] = {false, true, true}, present[3] = {false, true, true};
    bool ResolveParticipant(mpParticipantId id, int &slot, uint32_t &generation) const {
        if (!id.IsValid() || !bound[id.value]) { return false; }
        slot = slots[id.value]; generation = 7; return true;
    }
    const mpMatchParticipantState *FindParticipant(mpParticipantId id) const {
        return id.IsValid() && present[id.value] ? &states[id.value] : nullptr;
    }
};
static const int TEAM_MAX = 2, CMD_EXEC_NOW = 0, CMD_EXEC_INSERT = 1;
static const char *teamNames[TEAM_MAX] = {"Marine", "Strogg"};
static std::vector<int> operations;
struct Game {
    struct MatchGame {
        Session session;
        Session &GetMatchSession() { return session; }
    } mpGame;
    int numClients = 2;
    int gameType = GAME_DM;
    idDict userInfo[2], remoteInfo[2];
    void SetUserInfo(int slot, const idDict &info, bool client) {
        assert(!client && slot >= 0 && slot < numClients);
        userInfo[slot] = info;
        operations.push_back(slot * 2);
    }
} gameLocal;
static const char *va(const char *format, int slot) {
    static char command[32];
    std::snprintf(command, sizeof(command), format, slot);
    return command;
}
struct Commands {
    std::vector<int> deferred;
    bool requireApplied = true;
    int immediateCalls = 0;
    void BufferCommandText(int mode, const char *command) {
        assert(mode == CMD_EXEC_NOW || mode == CMD_EXEC_INSERT);
        assert(std::strncmp(command, "updateUI ", 9) == 0);
        char *end = nullptr;
        const long parsed = std::strtol(command + 9, &end, 10);
        assert(end != command + 9 && std::strcmp(end, "\n") == 0 && parsed >= 0 && parsed < gameLocal.numClients);
        const int slot = static_cast<int>(parsed);
        if (mode == CMD_EXEC_INSERT) { deferred.insert(deferred.begin(), slot); return; }
        ++immediateCalls;
        assert(!requireApplied || (!operations.empty() && operations.back() == slot * 2));
        gameLocal.remoteInfo[slot] = gameLocal.userInfo[slot];
        operations.push_back(slot * 2 + 1);
    }
    void Flush() {
        for (int slot : deferred) {
            gameLocal.remoteInfo[slot] = gameLocal.userInfo[slot];
        }
        deferred.clear();
    }
} commands;
static Commands *cmdSystem = &commands;
struct idMultiplayerGame {
    int matchTeamApplicationSlot=-1;
    Session matchSession;
    void ApplyMatchTeamsPlanToLegacy(const mpMatchTeamsTransactionPlan_t &plan) {
@BODY@
    }
};
struct Cvars {
    idDict values;
    void SetCVarBool(const char *key, bool value) { values.SetBool(key, value); }
    void SetCVarString(const char *key, const char *value) { values.Set(key, value); }
} cvars;
static Cvars *cvarSystem = &cvars;
struct InitialPlayer {
    int entityNumber = 1;
    bool local = false, initialJoinPending = true;
    bool initialJoinSpectateApplied = false, initialJoinMenuPending = true;
    bool IsLocalClient() const { return local; }
    void Reconcile(bool spec, idDict *userInfo, bool &modifiedInfo) {
        if (initialJoinPending) {
@INITIAL_JOIN@
        }
    }
};
struct Bots {
    bool bot = false;
    bool IsBot(int) const { return bot; }
} botManager;
struct AdmissionRetry {
    Session matchSession;
    bool managed = true;
    bool IsManagedMatch() const { return managed; }
    void Run(InitialPlayer *player, int clientNum) {
@RETRY@
    }
};
struct DuelAdmission {
    Session matchSession;
    bool managed = true, ranked = false;
    bool IsManagedMatch() const { return managed; }
    bool IsRankedParticipant(InitialPlayer *) const { return ranked; }
    bool Run(InitialPlayer *player, mpParticipantId participant) {
@ADMISSION@
        return active;
    }
};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "failed line %d\n", __LINE__); return 1; } } while (0)
int main() {
    // An accepted managed Duel seat exists before the legacy contender list
    // catches up. A stale contender must likewise never admit a waiting player.
    for (bool managed : {false, true}) for (bool duel : {false, true})
    for (bool active : {false, true}) for (bool ranked : {false, true}) {
        DuelAdmission adapter;
        adapter.managed = managed; adapter.ranked = ranked;
        adapter.matchSession.states[1].active = active;
        gameLocal.gameType = duel ? GAME_DUEL : GAME_DM;
        CHECK(adapter.Run(nullptr, {1}) == (managed && duel ? active : ranked));
    }
    idMultiplayerGame mp;
    mp.matchSession.states[1] = {true, 0};
    mp.matchSession.states[2] = {false, 1};
    gameLocal.userInfo[0].Set("ui_name", "Player One");
    gameLocal.userInfo[0].Set("ui_spectate", "Spectate");
    gameLocal.userInfo[0].Set("ui_team", "Strogg");
    gameLocal.userInfo[1].Set("ui_spectate", "Play");
    gameLocal.userInfo[1].Set("ui_team", "Strogg");
    mp.ApplyMatchTeamsPlanToLegacy({{1}, {2}});
    CHECK(operations == (std::vector<int>{0, 1, 2, 3}));
    CHECK(gameLocal.remoteInfo[0].values == gameLocal.userInfo[0].values);
    CHECK(gameLocal.remoteInfo[1].values == gameLocal.userInfo[1].values);
    CHECK(std::string(gameLocal.remoteInfo[0].GetString("ui_spectate")) == "Play");
    CHECK(std::string(gameLocal.remoteInfo[0].GetString("ui_team")) == "Marine");
    CHECK(std::string(gameLocal.remoteInfo[0].GetString("ui_name")) == "Player One");
    CHECK(std::string(gameLocal.remoteInfo[1].GetString("ui_spectate")) == "Spectate");
    CHECK(std::string(gameLocal.remoteInfo[1].GetString("ui_team")) == "Strogg");
    operations.clear();
    mp.ApplyMatchTeamsPlanToLegacy({{1}, {1}});
    CHECK(operations == (std::vector<int>{0, 1}));
    operations.clear();
    mp.ApplyMatchTeamsPlanToLegacy({{0}, {-1}});
    CHECK(operations.empty());
    for (int invalidSlot : {-1, 2, 32}) {
        mp.matchSession.slots[1] = invalidSlot;
        mp.ApplyMatchTeamsPlanToLegacy({{1}, {0}});
        CHECK(operations.empty());
    }
    mp.matchSession.slots[1] = 0;
    mp.matchSession.bound[1] = false;
    mp.ApplyMatchTeamsPlanToLegacy({{1}, {0}});
    CHECK(operations.empty());
    mp.matchSession.bound[1] = true;
    mp.matchSession.present[1] = false;
    mp.ApplyMatchTeamsPlanToLegacy({{1}, {0}});
    CHECK(operations.empty());
    // Initial admission can happen inside Spawn or an outer userinfo send.
    // Wait until that caller's stale dictionary has been sent, then publish
    // the accepted active/spectator choice once to its owner and peers.
    for (bool local : {false, true}) {
        for (bool spec : {false, true}) {
            for (bool joined : {false, true}) {
                gameLocal = Game{};
                cvars = Cvars{};
                InitialPlayer player;
                player.local = local;
                auto &info = gameLocal.userInfo[1];
                info.Set("ui_spectate", spec ? "Spectate" : "Play");
                info.SetBool("ui_joined", joined);
                bool modified = false;
                player.Reconcile(spec, &info, modified);
                CHECK(player.initialJoinPending && !modified && commands.deferred.empty());
                gameLocal.mpGame.session.phase = WARMUP;
                player.Reconcile(spec, &info, modified);
                CHECK(player.initialJoinPending && !modified && commands.deferred.empty());
                info.SetBool("ui_autoJoin", !spec);
                player.Reconcile(spec, &info, modified);
                CHECK(!player.initialJoinPending && player.initialJoinSpectateApplied && !player.initialJoinMenuPending);
                CHECK(modified == !joined && info.GetBool("ui_joined", "0"));
                CHECK(commands.deferred == (std::vector<int>{1}));
                CHECK(gameLocal.remoteInfo[1].values.empty());
                if (local) {
                    CHECK(cvars.values.GetBool("ui_joined", "0"));
                    CHECK(std::string(cvars.values.GetString("ui_spectate")) == (spec ? "Spectate" : "Play"));
                } else {
                    CHECK(cvars.values.values.empty());
                }
                player.Reconcile(spec, &info, modified);
                CHECK(commands.deferred == (std::vector<int>{1}));
                gameLocal.remoteInfo[1].Set("ui_spectate", spec ? "Play" : "Spectate");
                commands.Flush();
                CHECK(gameLocal.remoteInfo[1].values == info.values && commands.deferred.empty());
            }
        }
    }
    // The pre-warmup retry must never broadcast a freshly spawned remote
    // client's placeholder dictionary before its actual userinfo arrives.
    commands.requireApplied = false;
    for (int mask = 0; mask < 32; ++mask) {
        gameLocal = Game{};
        InitialPlayer player;
        AdmissionRetry retry;
        retry.managed = (mask & 1) != 0;
        player.initialJoinPending = (mask & 2) != 0;
        retry.matchSession.phase = (mask & 4) != 0 ? WARMUP : INACTIVE;
        botManager.bot = (mask & 8) != 0;
        if (mask & 16) { gameLocal.userInfo[1].SetBool("ui_autoJoin", false); }
        commands.immediateCalls = 0;
        retry.Run(&player, 1);
        CHECK(commands.immediateCalls == (mask == (1 | 2 | 4 | 16) ? 1 : 0));
    }
    return 0;
}
'''.replace("@BODY@", body).replace("@INITIAL_JOIN@", initial_join).replace("@RETRY@", retry).replace("@ADMISSION@", admission)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="userinfo-mirror-", dir=temporary) as temp:
        folder = Path(temp)
        cpp, exe = folder / "test.cpp", folder / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("mp_match_userinfo_mirror_contract: PASS")


if __name__ == "__main__":
    main()
