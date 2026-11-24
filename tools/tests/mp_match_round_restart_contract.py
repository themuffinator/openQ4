#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Execute the production client round-restart dispatch and verify its sender."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#include <cstdio>
#include <cstdlib>
#include "game/mp/MatchPhase.h"
#include "game/mp/GameTypeIds.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
enum { GTF_ROUND };
bool MPGameTypeHasAny(int type, int) { return type == GAME_CA || type == GAME_FREEZETAG || type == GAME_REDROVER; }
struct rvGameState {
    mpGameState_t phase = GAMEON;
    int round = 2;
    mpGameState_t GetMPGameState() const { return phase; }
};
struct Common {
    template<class... A> void DPrintf(const char*, A...) {}
    template<class... A> void Warning(const char*, A...) {}
} commonObject, *common = &commonObject;
struct Msg { int bits = 0; int GetRemainingReadBits() const { return bits; } };
struct Game {
    struct MP { rvGameState* state = nullptr; rvGameState* GetGameState() { return state; } } mpGame;
    int gameType = GAME_CA, worldRestarts = 0, fullRestarts = 0, warnings = 0;
    void LocalMapRestart() { ++worldRestarts; }
    void MapRestart() { ++fullRestarts; mpGame.state = nullptr; }
    void Warning(const char*) { ++warnings; }
    void Receive(int type, const Msg& msg);
};
'''

TESTS = r'''
int main() {
    static_assert(GAME_RELIABLE_MESSAGE_ANNOUNCER == 45, "existing wire id changed");
    static_assert(GAME_RELIABLE_MESSAGE_ROUNDRESTART == 46, "round-reset wire id changed");
    for (int id = -256; id <= 512; ++id) {
        CHECK(AcceptReliable(id) == (id >= GAME_RELIABLE_MESSAGE_SPAWN_PLAYER && id < GAME_RELIABLE_MESSAGE_COUNT));
    }
    for (int type = GAME_DM; type < NUM_GAME_TYPES; ++type) {
        for (int phase = INACTIVE; phase < STATE_COUNT; ++phase) {
            for (int extraBits = 0; extraBits <= 16; ++extraBits) {
                Game game; rvGameState state;
                game.mpGame.state = &state; game.gameType = type; state.phase = mpGameState_t(phase);
                game.Receive(GAME_RELIABLE_MESSAGE_ROUNDRESTART, Msg{extraBits});
                const bool valid = MPGameTypeHasAny(type, GTF_ROUND) && phase == GAMEON && extraBits == 0;
                CHECK(game.worldRestarts == int(valid));
                CHECK(game.warnings == int(!valid) && game.fullRestarts == 0);
                CHECK(game.mpGame.state == &state && state.phase == phase && state.round == 2);
            }
        }
    }
    Game game;
    game.Receive(GAME_RELIABLE_MESSAGE_ROUNDRESTART, Msg{});
    CHECK(game.warnings == 1 && game.worldRestarts == 0);
    // The ordinary restart still owns gametype/session reinitialization.
    game.Receive(GAME_RELIABLE_MESSAGE_RESTART, Msg{});
    CHECK(game.fullRestarts == 1);
    std::puts("round world reset preserves match state; wrong-mode/phase/trailing payload rejected: PASS");
}
'''


def main() -> None:
    network = (ROOT / "src/game/Game_network.cpp").read_text(encoding="utf-8")
    start = network.index("case GAME_RELIABLE_MESSAGE_RESTART: {")
    end = network.index("case GAME_RELIABLE_MESSAGE_STARTVOTE:", start)
    cases = network[start:end]
    rounds = (ROOT / "src/game/mp/RoundGameState.cpp").read_text(encoding="utf-8")
    reset = function(rounds, "void rvRoundGameState::ResetRound(")
    assert "outMsg.WriteByte( GAME_RELIABLE_MESSAGE_ROUNDRESTART );" in reset
    assert "GAME_RELIABLE_MESSAGE_RESTART" not in reset and "outMsg.WriteBits" not in reset
    assert reset.index("gameLocal.LocalMapRestart();") < reset.index("ServerSendReliableMessage")
    local = (ROOT / "src/game/Game_local.cpp").read_text(encoding="utf-8")
    assert "SetGameType();" in function(local, "void idGameLocal::MapRestart(")
    assert "SetGameType(" not in function(local, "void idGameLocal::LocalMapRestart(")
    header = (ROOT / "src/game/Game_local.h").read_text(encoding="utf-8")
    assert header.index("GAME_RELIABLE_MESSAGE_ANNOUNCER,") < header.index("GAME_RELIABLE_MESSAGE_ROUNDRESTART,")
    enum_start = header.rfind("enum {", 0, header.index("GAME_RELIABLE_MESSAGE_SPAWN_PLAYER,"))
    wire_enum = header[enum_start:header.index("};", enum_start) + 2]
    guard = function(network, "if ( id < GAME_RELIABLE_MESSAGE_SPAWN_PLAYER ||")
    guard = "bool AcceptReliable(int id) {\n" + guard.replace("return;", "return false;") + "return true;\n}\n"
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for round-restart regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="round-restart-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "restart.cpp", path / ("restart.exe" if os.name == "nt" else "restart")
        cpp.write_text(wire_enum + STUBS + guard +
                       "void Game::Receive(int type, const Msg& msg) { if (!AcceptReliable(type)) { ++warnings; return; } switch(type) {\n" +
                       cases + "} }\n" + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_round_restart_contract: PASS")


if __name__ == "__main__":
    main()
