#!/usr/bin/env python3
"""Execute the production usercmd handler across stalled-server history bounds."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using int64=std::int64_t;
constexpr int MAX_USERCMD_BACKUP=256, MAX_USERCMD_PACKET_COMMANDS=32;
struct usercmd_t {int gameFrame=0,gameTime=0,duplicateCount=0;};
struct idBitMsg {
    int bits=56,frame=0,count=1,failAt=-1;
    mutable int reads=0;
    int GetRemainingReadBits()const{return bits;}
    int ReadShort()const{return 16;}
    int ReadLong()const{return frame;}
    int ReadByte()const{return count;}
};
struct {int GetUserCmdMSec(){return 16;} void Printf(const char*,...){} } commonValue,*common=&commonValue;
struct idAsyncNetwork {
    static inline struct {int GetInteger(){return 0;}} verbose;
    static bool ReadUserCmdDelta(const idBitMsg &msg,usercmd_t &cmd,usercmd_t*) {
        if(msg.reads++==msg.failAt)return false;
        cmd.gameTime=123456;return true;
    }
    static bool UsercmdInputChanged(const usercmd_t&,const usercmd_t&){return false;}
};
struct Client {int clientPrediction=0,gameFrame=0,gameTime=0,lastInputTime=0;};
struct Server {
    Client client;
    usercmd_t userCmds[MAX_USERCMD_BACKUP][1]{};
    int gameFrame=1837,serverTime=29392,clientGameInitId=7,drops=0;
    void DropClient(int,const char*){++drops;}
    void Handle(const idBitMsg &msg) {
        int i,clientGameFrame,numUsercmds,index,clientNum=0;
        usercmd_t *last;
        switch(1) { @CASE@ }
    }
};
int main() {
    // Reproduce the observed reset: frame 1837 stalls while the peer sends
    // through 2093. Frame 2094 must neither overwrite history nor disconnect.
    Server stalled;
    for(int frame : {1837,2093}) {
        idBitMsg msg;msg.frame=frame;stalled.Handle(msg);
        assert(!stalled.drops && msg.reads==1 && stalled.client.gameFrame==frame);
        assert(stalled.userCmds[frame&255][0].gameTime==frame*16);
    }
    usercmd_t retained[MAX_USERCMD_BACKUP][1];
    memcpy(retained,stalled.userCmds,sizeof(retained));
    for(int frame : {2094,3000,INT32_MAX}) {
        idBitMsg msg;msg.frame=frame;stalled.Handle(msg);
        assert(!stalled.drops && msg.reads==0 && stalled.client.gameFrame==2093);
        assert(memcmp(retained,stalled.userCmds,sizeof(retained))==0);
    }
    // A corrected command after the next snapshot remains usable.
    idBitMsg corrected;corrected.frame=1838;stalled.Handle(corrected);
    assert(!stalled.drops && stalled.client.gameFrame==1838 && corrected.reads==1);
    // Malformed counts, negative histories and truncated payloads stay fatal.
    for(int count : {0,33,255}) {
        Server server;idBitMsg msg;msg.frame=1837;msg.count=count;server.Handle(msg);
        assert(server.drops==1 && msg.reads==0);
    }
    for(int frame : {-1,INT32_MIN,2}) {
        Server server;idBitMsg msg;msg.frame=frame;msg.count=4;server.Handle(msg);
        assert(server.drops==1 && msg.reads==0);
    }
    for(int bits : {0,8,55}) {
        Server server;idBitMsg msg;msg.frame=1837;msg.bits=bits;server.Handle(msg);
        assert(server.drops==1 && msg.reads==0);
    }
    Server truncated;idBitMsg msg;msg.frame=1837;msg.failAt=0;truncated.Handle(msg);
    assert(truncated.drops==1 && truncated.client.gameFrame==0);
    puts("async_usercmd_window_contract: PASS (stalled peer recovery; malformed packets rejected)");
}
'''


def main():
    source = (ROOT / "src/framework/async/AsyncServer.cpp").read_text(encoding="utf-8")
    case = function_body(source, "case CLIENT_UNRELIABLE_MESSAGE_USERCMD:")
    case = case.replace("case CLIENT_UNRELIABLE_MESSAGE_USERCMD:", "case 1:", 1)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise RuntimeError("a native C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="usercmd-window-", dir=temporary) as temp:
        cpp, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
        cpp.write_text(HARNESS.replace("@CASE@", case), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
