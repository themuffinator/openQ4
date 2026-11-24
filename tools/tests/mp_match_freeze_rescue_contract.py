#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Exercise the production Freeze Tag visibility check and continuous thaw timer."""
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
#include <cstring>
#include <map>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
template<class T> T Max(T a,T b) { return std::max(a,b); }
enum { MASK_SOLID=1, ENTITYNUM_NONE=4095 };
struct idVec3 {
    float x=0,y=0,z=0;
    idVec3()=default;
    idVec3(float a,float b,float c):x(a),y(b),z(c){}
    idVec3 operator-(const idVec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
    float LengthSqr() const { return x*x+y*y+z*z; }
};
struct Physics { idVec3 origin; const idVec3& GetOrigin() const { return origin; } };
struct idEntity { bool IsType(int) const { return true; } };
struct idPlayer : idEntity {
    int entityNumber=0,team=0,health=100;
    bool spectating=false,wantSpectate=false;
    Physics physics;
    Physics* GetPhysics() { return &physics; }
    idVec3 GetEyePosition() const { auto p=physics.origin; p.z+=64; return p; }
    static int GetClassType() { return 0; }
};
struct trace_t { float fraction; struct {int entityNum;} c; };
struct idMultiplayerGame {
    enum { CPARM_CLIENT };
    template<class... T> void CenterPrint(T...) {}
};
struct Dict {
    std::map<std::string,int> values;
    int GetInt(const char* s) { return values[s]; }
    bool GetBool(const char* s) { return GetInt(s)!=0; }
};
struct Local {
    bool isClient=false;
    int numClients=3,time=0,msec=16;
    idEntity* entities[32]={};
    Dict serverInfo;
    idMultiplayerGame mpGame;
    enum TraceMode { CLEAR, BODY, WALL, MISSING_WORLD } traceMode=CLEAR;
    int traces=0;
    idPlayer* expectedMate=nullptr;
    idPlayer* expectedBody=nullptr;
    void TracePoint(idPlayer* owner,trace_t& trace,const idVec3& start,const idVec3& end,int mask,idPlayer* ignored) {
        ++traces;
        CHECK(mask==MASK_SOLID);
        CHECK(owner==expectedMate && ignored==expectedMate);
        CHECK((start-expectedMate->GetEyePosition()).LengthSqr()==0);
        auto body=expectedBody->GetPhysics()->GetOrigin(); body.z+=16;
        CHECK((end-body).LengthSqr()==0);
        if(traceMode==MISSING_WORLD) return;
        trace.fraction=traceMode==CLEAR?1.0f:0.5f;
        trace.c.entityNum=traceMode==BODY?expectedBody->entityNumber:4094;
    }
} gameLocal;
struct rvRoundGameState { void Run() {} };
struct rvFreezeTagGameState : rvRoundGameState {
    bool live=true,eliminated[32]={};
    int thawProgress[32]={},lastThawAnnounce[32]={},autoThawTime[32]={};
    int thaws=0,credit=-2;
    bool RoundIsLive() const { return live; }
    bool IsEliminated(int i) const { return eliminated[i]; }
    bool PlayerIsAlive(idPlayer* p) const { return p->health>0&&!p->wantSpectate&&!eliminated[p->entityNumber]; }
    bool CanReachToThaw(idPlayer*,idPlayer*,int) const;
    void Run();
    void ThawPlayer(idPlayer* body,idPlayer* mate) {
        ++thaws; credit=mate?mate->entityNumber:-1;
        eliminated[body->entityNumber]=false; body->health=100;
        thawProgress[body->entityNumber]=0;
    }
};
'''
TESTS = r'''
int main() {
    idPlayer body,mate,enemy;
    body.entityNumber=0; body.health=-999;
    mate.entityNumber=1; mate.physics.origin={48,0,0};
    enemy.entityNumber=2; enemy.team=1;
    gameLocal.entities[0]=&body; gameLocal.entities[1]=&mate; gameLocal.entities[2]=&enemy;
    gameLocal.expectedMate=&mate; gameLocal.expectedBody=&body;
    auto& values=gameLocal.serverInfo.values;
    values["si_freezeThawTime"]=2; values["si_freezeThawRadius"]=96;
    rvFreezeTagGameState state;
    CHECK(!state.CanReachToThaw(nullptr,&mate,96*96));
    CHECK(!state.CanReachToThaw(&body,nullptr,96*96));
    CHECK(state.CanReachToThaw(&body,&mate,96*96));
    gameLocal.traceMode=Local::BODY;
    CHECK(state.CanReachToThaw(&body,&mate,96*96));
    gameLocal.traceMode=Local::WALL;
    CHECK(!state.CanReachToThaw(&body,&mate,96*96));
    values["si_freezeThawThroughSurface"]=1;
    CHECK(state.CanReachToThaw(&body,&mate,96*96));
    mate.physics.origin.x=97;
    CHECK(!state.CanReachToThaw(&body,&mate,96*96));
    mate.physics.origin.x=48; values["si_freezeThawThroughSurface"]=0;
    gameLocal.traceMode=Local::MISSING_WORLD;
    CHECK(!state.CanReachToThaw(&body,&mate,96*96));
    gameLocal.traceMode=Local::CLEAR;
    state.eliminated[0]=true;
    auto tick=[&]() { gameLocal.time+=16; state.Run(); };
    for(int i=0;i<100;++i) tick();
    CHECK(state.thaws==0 && state.thawProgress[0]==1600);
    mate.physics.origin.x=200;
    for(int i=0;i<50;++i) tick();
    CHECK(state.thawProgress[0]==800);
    mate.physics.origin.x=48;
    for(int i=0;i<74;++i) tick();
    CHECK(state.thaws==0 && state.thawProgress[0]==1984);
    tick(); CHECK(state.thaws==1 && body.health==100 && state.credit==1);
    for(int invalid=0;invalid<8;++invalid) {
        state=rvFreezeTagGameState(); state.eliminated[0]=true; body.health=-999;
        mate.health=100; mate.wantSpectate=false; mate.spectating=false;
        body.wantSpectate=false; body.spectating=false; gameLocal.isClient=false;
        if(invalid==0) mate.health=0;
        if(invalid==1) mate.wantSpectate=true;
        if(invalid==2) mate.spectating=true;
        if(invalid==3) state.eliminated[1]=true;
        if(invalid==4) body.wantSpectate=true;
        if(invalid==5) body.spectating=true;
        if(invalid==6) gameLocal.isClient=true;
        if(invalid==7) state.live=false;
        for(int i=0;i<140;++i) tick();
        CHECK(state.thaws==0 && state.thawProgress[0]==0);
    }
    gameLocal.isClient=false; body.spectating=false; body.wantSpectate=false;
    state=rvFreezeTagGameState(); state.eliminated[0]=true; body.health=-999;
    mate.physics.origin.x=200; state.autoThawTime[0]=gameLocal.time+32;
    tick(); CHECK(state.thaws==0); tick(); CHECK(state.thaws==1 && state.credit==-1);
}
'''


def main() -> None:
    source = (ROOT / "src/game/mp/RoundModes.cpp").read_text(encoding="utf-8")
    bodies = function(source, "bool rvFreezeTagGameState::CanReachToThaw(")
    bodies += function(source, "void rvFreezeTagGameState::Run(")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for Freeze Tag regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="freeze-rescue-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "freeze.cpp", path / ("freeze.exe" if os.name == "nt" else "freeze")
        cpp.write_text(STUBS + bodies + TESTS, encoding="utf-8")
        command = [compiler, "-std=c++17", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_freeze_rescue_contract: PASS")


if __name__ == "__main__":
    main()
