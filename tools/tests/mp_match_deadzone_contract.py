#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Run the production DeadZone controller and per-player scoring adapter."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "game/mp/GameTypeIds.h"
#include "game/mp/MatchPhase.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
enum { POWERUP_DEADZONE };
struct idEntity {};
struct idPlayer : idEntity {
    int entityNumber=0,team=TEAM_MARINE,health=100;
    bool spectating=false,wantSpectate=false,token=true;
    float buyMenuCash=0;
    bool PowerUpActive(int) const { return token; }
};
struct State { mpGameState_t phase=GAMEON; mpGameState_t GetMPGameState() const { return phase; } } liveState;
struct idMultiplayerGame {
    State* gameState=&liveState;
    struct Row { int deadZoneScore=0,teamFragCount=0; } playerState[32];
    struct Buying { int GetIntValueForKey(const char*,int) const { return 0; } } mpBuyingManager;
    int reportedTeam=TEAM_NONE,reportedCount=-1,reportedSituation=-1,calls=0;
    void ReportZoneControllingPlayer(idPlayer*);
    void ReportZoneController(int team,int count,int situation,idEntity*) {
        reportedTeam=team; reportedCount=count; reportedSituation=situation; ++calls;
    }
} ;
struct Local {
    bool isMultiplayer=true,isClient=false;
    int gameType=GAME_DEADZONE;
    idMultiplayerGame mpGame;
    int GetMSec() const { return 16; }
} gameLocal;
struct PlayerList : std::vector<idPlayer*> {
    int Num() const { return int(size()); }
    void Clear() { clear(); }
    void AddUnique(idPlayer* p) { if (std::find(begin(),end(),p)==end()) push_back(p); }
};
struct idTrigger_Multi : idEntity {
    struct Args { bool requireToken=true; bool GetBool(const char*,const char*) const { return requireToken; } } spawnArgs;
    PlayerList playersInTrigger;
    int controlZoneTrigger=3,prevZoneController=TEAM_NONE;
    void HandleControlZoneTrigger();
};
'''

TESTS = r'''
void Reset() {
    gameLocal.isMultiplayer=true; gameLocal.isClient=false; gameLocal.gameType=GAME_DEADZONE;
    gameLocal.mpGame=idMultiplayerGame(); liveState.phase=GAMEON;
}
void CheckZone(idTrigger_Multi& zone,int owner,int count,int situation) {
    zone.HandleControlZoneTrigger();
    CHECK(gameLocal.mpGame.reportedTeam==owner && gameLocal.mpGame.reportedCount==count);
    CHECK(gameLocal.mpGame.reportedSituation==situation && zone.prevZoneController==owner);
    CHECK(zone.playersInTrigger.empty());
}
int main() {
    idPlayer marine,strogg,second; marine.entityNumber=0; strogg.entityNumber=1; strogg.team=TEAM_STROGG; second.entityNumber=2;
    // Loss of the first touch's token must not turn the next eligible player into a deadlock.
    for (bool reverse:{false,true}) {
        Reset(); idTrigger_Multi zone; marine.token=false;
        zone.playersInTrigger=PlayerList();
        zone.playersInTrigger.push_back(reverse?&strogg:&marine);
        zone.playersInTrigger.push_back(reverse?&marine:&strogg);
        CheckZone(zone,TEAM_STROGG,1,DZ_STROGG_TAKEN);
        CHECK(gameLocal.mpGame.playerState[0].deadZoneScore==0);
        CHECK(gameLocal.mpGame.playerState[1].deadZoneScore==16);
    }
    marine.token=true;
    // Token-free zones count controllers even though no player carries a token.
    Reset(); idTrigger_Multi freeZone; freeZone.spawnArgs.requireToken=false; marine.token=false; second.token=false;
    freeZone.playersInTrigger.push_back(&marine); freeZone.playersInTrigger.push_back(&second);
    CheckZone(freeZone,TEAM_MARINE,2,DZ_MARINES_TAKEN);
    CHECK(gameLocal.mpGame.playerState[0].deadZoneScore==16 && gameLocal.mpGame.playerState[2].deadZoneScore==16);
    marine.token=second.token=true;
    // Opposing controllers contest regardless of collection order or later teammates.
    std::vector<idPlayer*> players={&marine,&strogg,&second};
    do {
        Reset(); idTrigger_Multi zone;
        for (auto* p:players) zone.playersInTrigger.push_back(p);
        CheckZone(zone,2,0,DZ_MARINE_DEADLOCK);
        CHECK(gameLocal.mpGame.playerState[0].deadZoneScore==0 && gameLocal.mpGame.playerState[1].deadZoneScore==0);
    } while (std::next_permutation(players.begin(),players.end(),[](idPlayer* a,idPlayer* b){return a->entityNumber<b->entityNumber;}));
    // Dead, spectating, withdrawing, invalid-team and null touches cannot score or contest.
    for (int invalid=0;invalid<5;++invalid) {
        Reset(); idTrigger_Multi zone; idPlayer excluded;
        if (invalid==0) excluded.health=0;
        if (invalid==1) excluded.spectating=true;
        if (invalid==2) excluded.wantSpectate=true;
        if (invalid==3) excluded.team=TEAM_NONE;
        zone.playersInTrigger.push_back(invalid==4?nullptr:&excluded); zone.playersInTrigger.push_back(&strogg);
        CheckZone(zone,TEAM_STROGG,1,DZ_STROGG_TAKEN);
    }
    Reset(); idTrigger_Multi zone; zone.controlZoneTrigger=1; zone.playersInTrigger.push_back(&strogg);
    CheckZone(zone,TEAM_NONE,0,DZ_NONE);
    zone.controlZoneTrigger=3; zone.playersInTrigger.push_back(&marine);
    CheckZone(zone,TEAM_MARINE,1,DZ_MARINES_TAKEN);
    CheckZone(zone,TEAM_NONE,0,DZ_MARINES_LOST);
    for (int phase=INACTIVE;phase<STATE_COUNT;++phase) {
        for (bool client:{false,true}) {
            Reset(); liveState.phase=mpGameState_t(phase); gameLocal.isClient=client;
            gameLocal.mpGame.ReportZoneControllingPlayer(&marine);
            CHECK(gameLocal.mpGame.playerState[0].deadZoneScore==(!client && (phase==GAMEON || phase==SUDDENDEATH)?16:0));
        }
    }
    Reset(); gameLocal.isClient=true; idTrigger_Multi clientZone; clientZone.playersInTrigger.push_back(&marine);
    clientZone.HandleControlZoneTrigger(); CHECK(gameLocal.mpGame.calls==0);
    std::puts("DeadZone eligibility, token-free scoring, contest order, side restrictions and phase authority: PASS");
}
'''


def main() -> None:
    trigger = (ROOT / "src/game/Trigger.cpp").read_text(encoding="utf-8")
    mp = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/game/mp/GameState.h").read_text(encoding="utf-8")
    enum_start = header.index("enum dzState_t {")
    dz_enum = header[enum_start:header.index("};", enum_start) + 2]
    bodies = function(trigger, "static bool IsEligibleDeadZoneController(")
    bodies += function(trigger, "void idTrigger_Multi::HandleControlZoneTrigger(")
    bodies += function(mp, "void idMultiplayerGame::ReportZoneControllingPlayer(")
    touch = function(trigger, "void idTrigger_Multi::Event_Touch(")
    assert "playersInTrigger.AddUnique( p )" in touch and "playersInTrigger.Append" not in touch
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for DeadZone regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="deadzone-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "deadzone.cpp", path / ("deadzone.exe" if os.name == "nt" else "deadzone")
        cpp.write_text(STUBS + dz_enum + bodies + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_deadzone_contract: PASS")


if __name__ == "__main__":
    main()
