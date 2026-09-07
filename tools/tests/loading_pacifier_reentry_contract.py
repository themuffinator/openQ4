#!/usr/bin/env python3
"""Run the production loading update with recursive presentation/network offers."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
template<class T> T Max(T a,T b){return std::max(a,b);}
template<class T> T Min(T a,T b){return std::min(a,b);}
struct idMath {
    static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
    static float Ceil(float v){return std::ceil(v);}
};
int clockMsec=1000, redraws=0, callbackStage=-1;
bool throwFromCallback=false;
int Sys_Milliseconds(){return clockMsec;}
void Sys_GenerateEvents(){}
float Session_GetBlockingLoadFrameIntervalMsec(){return 16.0f;}
constexpr float SESSION_PACIFIER_DRAW_BUDGET_RATIO=4.0f;
void Session_BeginBlockingLoadPresentationFrame(){++clockMsec;}
struct {int GetPresentationTime(){return clockMsec;}} commonValue,*common=&commonValue;
struct {int GetReadCount(){return 40;}} filesystemValue,*fileSystem=&filesystemValue;
struct GUI {
    float shown=0.0f;
    GUI &State(){return *this;}
    float GetFloat(const char*){return shown;}
    void SetStateFloat(const char*,float value){shown=value;}
    void StateChanged(int){}
};
void NestedOffer(int stage);
struct Network {
    int stage=0,calls=0;
    void PacifierUpdate(){++calls;NestedOffer(stage);}
};
struct idAsyncNetwork {static inline Network client{1,0},server{2,0};};
struct idSessionLocal {
    bool insideExecuteMapChange=true,insidePacifierUpdate=false,insideUpdateScreen=false;
    int lastPacifierTime=0,lastPacifierDrawMsec=0,bytesNeededForMapLoad=100;
    bool loadingAssetQueueActive=true;
    int loadingAssetQueueTotal=10,loadingAssetQueueLoaded=2;
    float loadingAssetQueueStartPct=0.1f;
    GUI gui,*guiLoading=&gui;
    void UpdateScreen(){++redraws;NestedOffer(0);}
    void PacifierUpdate();
} session;
void NestedOffer(int stage) {
    // Model a print/flush taking longer than the pacing interval. It offers
    // another loading update before the original callback returns.
    clockMsec+=100;
    if(callbackStage!=stage)return;
    callbackStage=-1;
    session.PacifierUpdate();
    if(throwFromCallback)throw std::runtime_error("load callback failed");
}
@BODY@
void Reset() {
    session=idSessionLocal();session.guiLoading=&session.gui;
    idAsyncNetwork::client.calls=idAsyncNetwork::server.calls=redraws=0;
    clockMsec=1000;callbackStage=-1;throwFromCallback=false;
}
int main() {
    for(int stage=0;stage<3;++stage)for(int fail=0;fail<2;++fail) {
        Reset();callbackStage=stage;throwFromCallback=fail!=0;
        try {session.PacifierUpdate();} catch(const std::runtime_error&) {}
        assert(!session.insidePacifierUpdate);
#ifdef ID_DEDICATED
        assert(redraws==0 && idAsyncNetwork::client.calls==0);
        assert(idAsyncNetwork::server.calls==1);
#else
        assert(redraws==1);
        assert(idAsyncNetwork::client.calls==(!fail || stage>0?1:0));
        assert(idAsyncNetwork::server.calls==(!fail || stage>1?1:0));
#endif
        // A later ordinary offer still progresses, including after exceptions.
        const int prior=idAsyncNetwork::server.calls;
        clockMsec+=10000;callbackStage=-1;throwFromCallback=false;
        session.PacifierUpdate();
        assert(idAsyncNetwork::server.calls==prior+1 && !session.insidePacifierUpdate);
    }
    Reset();session.insideExecuteMapChange=false;session.PacifierUpdate();
    assert(redraws==0 && idAsyncNetwork::server.calls==0 && !session.insidePacifierUpdate);
    Reset();session.lastPacifierTime=clockMsec;session.PacifierUpdate();
    assert(redraws==0 && idAsyncNetwork::server.calls==0 && !session.insidePacifierUpdate);
    puts("loading pacifier recursive offers, exception recovery and pacing: PASS");
}
'''


def main():
    production = (ROOT / "src/framework/Session.cpp").read_text(encoding="utf-8")
    source = HARNESS.replace("@BODY@", function_body(production, "void idSessionLocal::PacifierUpdate"))
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="pacifier-reentry-", dir=ROOT / ".tmp") as temp:
        cpp = Path(temp) / "test.cpp"
        cpp.write_text(source, encoding="utf-8")
        for dedicated in (False, True):
            exe = Path(temp) / ("dedicated.exe" if dedicated else "client.exe")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
            if dedicated:
                command += ["-DID_DEDICATED"]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
