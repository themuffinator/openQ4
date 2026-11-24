#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Compile server and snapshot-driven gib gates: frozen bodies must survive."""
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
#define BIT(x) (1 << (x))
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
class idDict;
#include "game/mp/GameTypes.h"
struct idVec3 {};
enum { POWERUP_QUADDAMAGE };
struct idPlayer {
    int health=-100,instance=0,gibCalls=0;
    bool quad=false,gibDeath=false,gibsLaunched=false;
    idVec3 gibDir;
    struct Args { bool enabled=true; bool GetBool(const char*) const { return enabled; } } spawnArgs;
    int GetInstance() const { return instance; }
    bool PowerUpActive(int) const { return quad; }
    void ClientGib(const idVec3&);
    void KillVisuals(idPlayer* killer,const idVec3& dir);
};
struct Local {
    bool isMultiplayer=true,isListenServer=true;
    int gameType=GAME_FREEZETAG;
    idPlayer viewer;
    idPlayer* GetLocalPlayer() { return &viewer; }
} gameLocal;
'''

TESTS = r'''
int main() {
    for (int mode=GAME_DM;mode<NUM_GAME_TYPES;++mode) {
        if (!MPGameTypeIsSelectable(mode)) continue;
        gameLocal.gameType=mode;
        for (int health : {-999,-21,-20,0}) {
            for (bool quad : {false,true}) {
                for (bool enabled : {false,true}) {
                    idPlayer victim,killer;
                    victim.health=health; victim.spawnArgs.enabled=enabled; killer.quad=quad;
                    victim.KillVisuals(&killer,idVec3());
                    const bool shouldGib=mode!=GAME_FREEZETAG && (health < -20 || quad);
                    CHECK(victim.gibDeath==shouldGib);
                    CHECK(victim.gibCalls==int(shouldGib && enabled));
                    // Snapshot reception independently asks ClientGib on overkill.
                    victim.gibCalls=0; victim.ClientGib(idVec3());
                    CHECK(victim.gibCalls==int(mode!=GAME_FREEZETAG && enabled));
                }
            }
        }
    }
    gameLocal.isMultiplayer=false;
    idPlayer single; single.ClientGib(idVec3()); CHECK(single.gibCalls==1);
    std::puts("Freeze Tag preserves its body on server and snapshot gib paths; other modes retain gibs: PASS");
}
'''


def main() -> None:
    player = (ROOT / "src/game/Player.cpp").read_text(encoding="utf-8")
    types = (ROOT / "src/game/mp/GameTypes.cpp").read_text(encoding="utf-8")
    begin = types.index("static const mpGameTypeInfo_t mpGameTypeInfoTable[]")
    end = types.index("\n/*", types.index("const int mpNumGameTypeInfo", begin))
    definitions = types[begin:end] + "\n".join(function(types, signature) for signature in (
        "const mpGameTypeInfo_t *MPGameType(", "bool MPGameTypeIsSelectable(", "bool MPGameTypeHasAny(",
    ))
    killed = function(player, "void idPlayer::Killed(")
    server_gate = function(killed, "if ( !MPGameTypeHasAny( gameLocal.gameType, GTF_FREEZE ) &&")
    client = function(player, "void idPlayer::ClientGib(")
    # Execute every real early gate; substitute only the rendering work after it.
    client = client[:client.index("\n\tint i;")] + "\n++gibCalls;\n}\n"
    program = ("#include <initializer_list>\n" + STUBS + definitions + client +
               "void idPlayer::KillVisuals(idPlayer* killer,const idVec3& dir) {\n" + server_gate + "\n}\n" + TESTS)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for frozen-body regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="freeze-body-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "freeze.cpp", path / ("freeze.exe" if os.name == "nt" else "freeze")
        cpp.write_text(program, encoding="utf-8")
        command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_freeze_body_contract: PASS")


if __name__ == "__main__":
    main()
