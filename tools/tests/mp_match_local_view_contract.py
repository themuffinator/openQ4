#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute production listen-client view publication without menu/HUD callbacks."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using byte = unsigned char;
constexpr int MAX_CLIENTS = 4, MAX_GAME_MESSAGE_SIZE = 256;
struct idEntity {
    bool player = true;
    bool IsType(int) const { return player; }
};
struct idPlayer : idEntity {
    bool fake = false;
    static int GetClassType() { return 1; }
    bool IsFakeClient() const { return fake; }
};
struct mpSessionView {
    struct State {
        uint64_t sessionId = 0, viewRevision = 0;
        struct Recipient { int slot = -1; } recipient;
        int phase = 0;
    } publicState;
    void Clear() { *this = mpSessionView(); }
};
struct idBitMsg {
    mpSessionView payload;
    void Init(byte *, int) {}
    void BeginWriting() {}
};
struct Session {
    uint64_t id = 12;
    uint64_t GetSessionId() const { return id; }
};
struct GameLocal {
    bool isServer = true, isListenServer = true;
    int localClientNum = 0, numClients = 3;
    idEntity *entities[MAX_CLIENTS] = {};
} gameLocal;
struct Network {
    int sends[MAX_CLIENTS] = {};
    mpSessionView delivered[MAX_CLIENTS];
    void ServerSendReliableMessage(int slot, const idBitMsg &message) {
        ++sends[slot];
        // Real async-server behavior: no loopback to its local listen client.
        if (gameLocal.isListenServer && slot == gameLocal.localClientNum) return;
        delivered[slot] = message.payload;
    }
} network, *networkSystem = &network;
struct Model {
    bool ready = false;
    bool IsReady() const { return ready; }
};
struct idMultiplayerGame {
    Session matchSession;
    struct PlayerState { bool ingame = true; } playerState[MAX_CLIENTS];
    uint64_t matchViewRevision = 1, matchViewSentRevision[MAX_CLIENTS] = {};
    mpSessionView clientMatchView;
    bool clientMatchViewValid = false;
    Model clientMatchControlModel;
    int currentMenu = 0, gui = 1, *mainGui = &gui, phase = 1;
    int builds[MAX_CLIENTS] = {}, accepts = 0, projections = 0;
    bool buildAllowed[MAX_CLIENTS] = {true, true, true, true}, acceptAllowed = true;
    bool BuildMatchView(int slot, mpSessionView &view) {
        ++builds[slot];
        if (!buildAllowed[slot]) return false;
        view.publicState.sessionId = matchSession.GetSessionId();
        view.publicState.viewRevision = matchViewRevision;
        view.publicState.recipient.slot = slot;
        view.publicState.phase = phase;
        return true;
    }
    bool AcceptClientMatchView(const mpSessionView &incoming) {
        ++accepts;
        if (!acceptAllowed) return false;
        assert(incoming.publicState.recipient.slot == gameLocal.localClientNum);
        clientMatchView = incoming;
        clientMatchViewValid = clientMatchControlModel.ready = true;
        return true;
    }
    bool WriteMatchViewMessage(int slot, idBitMsg &message) {
        return BuildMatchView(slot, message.payload);
    }
    void ProjectClientMatchControlMenu(bool notify) {
        assert(notify && mainGui && currentMenu == 1 && clientMatchViewValid);
        ++projections;
    }
    bool RefreshLocalClientMatchView() {
@REFRESH@
    }
    void SendChangedMatchViews(bool force = false) {
@SEND@
    }
};
struct Fixture {
    idPlayer players[MAX_CLIENTS];
    idMultiplayerGame mp;
    Fixture() {
        gameLocal = GameLocal(); network = Network();
        for (int slot = 0; slot < gameLocal.numClients; ++slot) gameLocal.entities[slot] = &players[slot];
    }
};
static void ClosedPresentationTurnover() {
    Fixture f;
    // Review can close the menu and suppress HUD draws after a suicide. None
    // of the presentation refresh hooks are called anywhere in this test.
    for (int phase : {1, 2, 3, 5, 6, 1}) {
        f.mp.phase = phase;
        f.mp.SendChangedMatchViews();
        assert(f.mp.clientMatchViewValid && f.mp.clientMatchControlModel.IsReady());
        assert(f.mp.clientMatchView.publicState.phase == phase);
        assert(f.mp.clientMatchView.publicState.viewRevision == f.mp.matchViewRevision);
        assert(network.sends[0] == 0);
        for (int slot = 1; slot < 3; ++slot) {
            assert(network.delivered[slot].publicState.phase == phase);
            assert(network.delivered[slot].publicState.viewRevision == f.mp.matchViewRevision);
            assert(network.delivered[slot].publicState.recipient.slot == slot);
        }
        const int builds = f.mp.builds[0], accepts = f.mp.accepts;
        f.mp.SendChangedMatchViews();
        assert(f.mp.builds[0] == builds && f.mp.accepts == accepts);
        assert(f.mp.projections == 0);
        ++f.mp.matchViewRevision;
    }
    f.mp.currentMenu = 1;
    f.mp.SendChangedMatchViews();
    assert(f.mp.projections == 1);
    const int localBuilds = f.mp.builds[0], accepted = f.mp.accepts;
    f.mp.SendChangedMatchViews(true);
    assert(f.mp.builds[0] == localBuilds && f.mp.accepts == accepted);
    ++f.mp.matchSession.id; ++f.mp.matchViewRevision;
    f.mp.SendChangedMatchViews();
    assert(f.mp.clientMatchView.publicState.sessionId == f.mp.matchSession.id);
}
static void FailureRetryAndPeerIsolation() {
    Fixture f;
    f.mp.SendChangedMatchViews();
    ++f.mp.matchViewRevision; f.mp.phase = 5;
    f.mp.buildAllowed[0] = false;
    f.mp.SendChangedMatchViews();
    assert(f.mp.matchViewSentRevision[0] == 1 && f.mp.clientMatchView.publicState.phase == 1);
    assert(network.delivered[1].publicState.phase == 5 && network.delivered[2].publicState.phase == 5);
    f.mp.buildAllowed[0] = true; f.mp.acceptAllowed = false;
    f.mp.SendChangedMatchViews();
    assert(f.mp.matchViewSentRevision[0] == 1 && f.mp.clientMatchView.publicState.phase == 1);
    f.mp.acceptAllowed = true;
    f.mp.SendChangedMatchViews();
    assert(f.mp.matchViewSentRevision[0] == 2 && f.mp.clientMatchView.publicState.phase == 5);
    f.mp.mainGui = nullptr; f.mp.currentMenu = 1;
    ++f.mp.matchViewRevision; f.mp.phase = 1;
    f.mp.SendChangedMatchViews();
    assert(f.mp.clientMatchView.publicState.phase == 1 && f.mp.projections == 0);
}
static void ServerAndEntityGuards() {
    for (int scenario = 0; scenario < 5; ++scenario) {
        Fixture f;
        if (scenario == 0) { gameLocal.isListenServer = false; gameLocal.localClientNum = -1; }
        if (scenario == 1) gameLocal.isServer = false;
        if (scenario == 2) f.mp.matchViewRevision = 0;
        if (scenario == 3) gameLocal.entities[0] = nullptr;
        if (scenario == 4) f.players[0].fake = true;
        f.mp.SendChangedMatchViews();
        assert(f.mp.accepts == 0 && !f.mp.clientMatchViewValid);
        if (scenario == 0) {
            for (int slot = 0; slot < 3; ++slot) assert(network.sends[slot] == 1);
        } else if (scenario == 1 || scenario == 2) {
            for (int slot = 0; slot < 3; ++slot) assert(network.sends[slot] == 0);
        } else {
            assert(network.sends[0] == 0 && network.sends[1] == 1 && network.sends[2] == 1);
        }
    }
    Fixture f;
    gameLocal.localClientNum = 2; // Listen-client slot is not assumed to be zero.
    f.mp.SendChangedMatchViews();
    assert(f.mp.clientMatchView.publicState.recipient.slot == 2);
    assert(network.sends[0] == 1 && network.sends[1] == 1 && network.sends[2] == 0);
}
static void SlowMapAdmission() {
    Fixture f;
    f.mp.playerState[1].ingame = false;
    // Model a delayed remote initialization while the host and another peer
    // continue through a minute of changing authoritative state.
    for (int sample = 0; sample < 60; ++sample) {
        f.mp.SendChangedMatchViews(sample % 3 == 0);
        assert(network.sends[1] == 0 && f.mp.matchViewSentRevision[1] == 0);
        assert(f.mp.clientMatchView.publicState.viewRevision == f.mp.matchViewRevision);
        assert(network.delivered[2].publicState.viewRevision == f.mp.matchViewRevision);
        ++f.mp.matchViewRevision;
    }
    f.mp.playerState[1].ingame = true;
    f.mp.SendChangedMatchViews();
    assert(network.sends[1] == 1);
    assert(network.delivered[1].publicState.viewRevision == f.mp.matchViewRevision);
    f.mp.SendChangedMatchViews();
    assert(network.sends[1] == 1);
}
int main() {
    ClosedPresentationTurnover(); FailureRetryAndPeerIsolation(); ServerAndEntityGuards();
    SlowMapAdmission();
    puts("listen-client view publication, closed presentation, retries and remote delivery: PASS");
}
'''


def main() -> None:
    production = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    source = HARNESS.replace("@REFRESH@", function_body(production, "bool idMultiplayerGame::RefreshLocalClientMatchView"))
    source = source.replace("@SEND@", function_body(production, "void idMultiplayerGame::SendChangedMatchViews"))
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required for listen-client view regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-local-view-", dir=ROOT / ".tmp") as directory:
        folder = Path(directory)
        cpp = folder / "local_view.cpp"
        executable = folder / ("local_view.exe" if os.name == "nt" else "local_view")
        cpp.write_text(source, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(executable)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    print("mp_match_local_view_contract: PASS")


if __name__ == "__main__":
    main()
