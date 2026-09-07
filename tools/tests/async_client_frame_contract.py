#!/usr/bin/env python3
"""Compile the production client frame loop and verify its game callback lifetime."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <string>
using byte = unsigned char;
constexpr int MAX_MESSAGE_SIZE=32, MAX_USERCMD_BACKUP=16, CVAR_USERINFO=1;
enum { CS_DISCONNECTED, CS_PURERESTART, CS_CONNECTING, CS_CONNECTED, CS_INGAME };
struct netadr_t {};
struct idBitMsg { void Init(byte*,int){} void SetSize(int){} void BeginReading(){} };
struct gameReturn_t { const char *sessionCommand=""; };
std::string events;
struct Game {
    void ThrottleUserInfo() { events+='T'; }
    void SetUserInfo(int,int,bool) { events+='U'; }
    void ClientRun() { events+='R'; }
    gameReturn_t ClientPrediction(int,int,bool) { events+='P'; return {}; }
    void ClientEndFrame() { events+='E'; }
} gameValue, *game=&gameValue;
struct Cvars { int flags=0; int GetModifiedFlags(){return flags;} void ClearModifiedFlags(int){flags=0;} } cv, *cvarSystem=&cv;
struct Commands { void GetDirectUsercmd() {} } commands, *usercmdGen=&commands;
struct Common { int GetUserCmdMSec(){return 16;} } commonValue, *common=&commonValue;
struct Session { struct { int userInfo[1]{}; } mapSpawnData; } sessLocal;
struct idAsyncNetwork { static void ExecuteSessionCommand(const char*){} };
int AsyncClient_NextGameFrameMsec(int) {return 16;}
struct idAsyncClient {
    struct Port {
        bool opened=true;
        int GetPort(){return opened;}
        bool GetPacketBlocking(netadr_t&,byte*,int&,int,int){return false;}
    } clientPort;
    struct List { void RunFrame(){} } serverList;
    int gameTimeResidual=0, clientPredictTime=0, clientState=CS_INGAME;
    int gameFrame=0, gameTime=0, snapshotGameFrame=0, snapshotGameTime=0;
    int lastFrameDelta=0, clientNum=0, elapsed=0, userCmds[MAX_USERCMD_BACKUP]{};
    bool timeout=false;
    int UpdateTime(int){int value=elapsed; elapsed=0; return value;}
    void UpdateRemoteConsoleRequest(){}
    void HandleDownloads(){}
    void ProcessMessage(netadr_t&,idBitMsg&){}
    void Reconnect(){}
    void SetupConnection(){}
    bool CheckTimeout(){return timeout;}
    void Idle(){}
    void SendUserInfoToServer(){events+='S';}
    void SendUsercmdsToServer(){}
    void DuplicateUsercmds(int,int){}
    void RunFrame(bool);
};
'''
MAIN = r'''
int main() {
    for(int state=CS_DISCONNECTED; state<=CS_INGAME; ++state) {
        for(int ticks=0; ticks<=4; ++ticks) {
            for(int userinfo=0; userinfo<2; ++userinfo) {
                for(int timedOut=0; timedOut<2; ++timedOut) {
                    for(int open=0; open<2; ++open) {
                        idAsyncClient client;
                        client.clientState=state; client.elapsed=ticks*16;
                        client.timeout=timedOut; client.clientPort.opened=open;
                        cv.flags=userinfo; events.clear();
                        client.RunFrame(false);
                        const bool ingame=state==CS_INGAME && !timedOut && open;
                        const std::string expected=ingame ?
                            (userinfo ? "TSU" : "") + std::string("R") + std::string(ticks,'P') + "E" : "";
                        assert(events==expected);
                        if(ingame) {
                            // Consecutive render-only calls must still service
                            // received challenges and finish icons exactly once.
                            events.clear(); client.RunFrame(false);
                            assert(events=="RE");
                        }
                    }
                }
            }
        }
    }
}
'''


def main():
    body = function_body((ROOT / "src/framework/async/AsyncClient.cpp").read_text(encoding="utf-8"),
                         "void idAsyncClient::RunFrame")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    temp = ROOT / ".tmp"
    temp.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="client-frame-", dir=temp) as directory:
        source, executable = Path(directory) / "contract.cpp", Path(directory) / "contract.exe"
        source.write_text(SUPPORT + body + MAIN, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("async client frame callbacks: PASS (200 state/timing cases)")


if __name__ == "__main__":
    main()
