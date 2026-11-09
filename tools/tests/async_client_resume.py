#!/usr/bin/env python3
"""Run the production async client's return-from-background policy (AND-01).

ui-visual-design.md 13.6: after the app leaves the background, multiplayer
reconnects or shows a connection-lost state. This compiles the production
idAsyncClient::ApplicationResumed, ResumeReconnect and CheckResume against an
in-memory client and checks: a stay beyond the time-out reconnects at once; a
shorter one drops the queued backlog and waits for a fresh packet, which
keeps the connection, or reconnects after net_resumeProbe without one; and an
inactive client, a listen server, a multi-view demo, a client still
connecting or net_resumeReconnect 0 never reconnects. No sockets are opened.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
enum {CVAR_SYSTEM=1,CVAR_BOOL=2,CVAR_ARCHIVE=4,CVAR_NOCHEAT=8,CVAR_INTEGER=16,CMD_EXEC_APPEND=1};
enum clientState_t {CS_DISCONNECTED,CS_PURERESTART,CS_CHALLENGING,CS_CONNECTING,CS_CONNECTED,CS_INGAME};
struct idCVar {
    std::string value;
    idCVar(const char*,const char* initial,int,const char*,int=0,int=0):value(initial){}
    bool GetBool()const{return std::stoi(value)!=0;}
    int GetInteger()const{return std::stoi(value);}
};
static std::vector<std::string> commands;
struct Commands {void BufferCommandText(int when,const char* text){check(when==CMD_EXEC_APPEND,"reconnect must not run inside the frame");commands.emplace_back(text);}} commandValue,*cmdSystem=&commandValue;
struct Common {void Printf(const char*,...){}} commonValue,*common=&commonValue;
static int nowMs=50000;
static int Sys_Milliseconds(){return nowMs;}
static const char* va(const char* format,...){static char buffer[256];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof buffer,format,args);va_end(args);return buffer;}
struct Server {bool active=false;bool IsActive()const{return active;}};
struct Demo {bool playing=false;bool IsPlaying()const{return playing;}};
struct idAsyncNetwork {static idCVar clientServerTimeout;static Server server;static Demo multiViewDemo;};
idCVar idAsyncNetwork::clientServerTimeout("net_clientServerTimeout","40",0,"");
Server idAsyncNetwork::server;Demo idAsyncNetwork::multiViewDemo;
struct idAsyncClient {
    bool active=true;clientState_t clientState=CS_INGAME;
    int clientTime=1000,lastPacketTime=990,resumeProbeTime=-1,resumeReconnectTime=0,cleared=0;
    void ClearPendingPackets(){++cleared;}
    void ApplicationResumed(int suspendedMs);
    void ResumeReconnect(const char* reason);
    bool CheckResume();
    bool StaleDisconnectAfterResume() const;
};
'''

MAIN = r'''
static idAsyncClient fresh(){commands.clear();idAsyncNetwork::server.active=false;idAsyncNetwork::multiViewDemo.playing=false;
    net_resumeReconnect.value="1";net_resumeProbe.value="5000";return idAsyncClient{};}
static bool reconnecting(){return commands==std::vector<std::string>({"reconnect\n"});}
int main(){try{
    // Away longer than the time-out: the server has dropped the client.
    idAsyncClient c=fresh();c.ApplicationResumed(40000);
    check(reconnecting() && c.resumeProbeTime<0 && c.cleared==1,"a stay beyond the time-out did not drop the backlog and reconnect at once");
    // The dropped channel's disconnect must not end the reconnect: only while
    // the new connection is being set up, and only briefly.
    c.clientState=CS_CHALLENGING;check(c.StaleDisconnectAfterResume(),"the old channel's disconnect ended the reconnect");
    nowMs+=9999;check(c.StaleDisconnectAfterResume(),"the reconnect window closed early");
    nowMs+=1;check(!c.StaleDisconnectAfterResume(),"a disconnect long after the reconnect was ignored");
    nowMs-=5000;c.clientState=CS_CONNECTED;check(!c.StaleDisconnectAfterResume(),"the new connection's own disconnect was ignored");
    idAsyncClient never=fresh();never.clientState=CS_CHALLENGING;
    check(!never.StaleDisconnectAfterResume(),"a disconnect was ignored without a resume reconnect");
    // A short stay: the backlog goes, a fresh packet keeps the connection.
    c=fresh();c.ApplicationResumed(12000);
    check(commands.empty() && c.cleared==1 && c.resumeProbeTime==1000,"a short stay did not start the probe");
    c.clientTime+=2000;check(!c.CheckResume() && commands.empty(),"the probe gave up early");
    c.lastPacketTime=c.clientTime;check(!c.CheckResume() && c.resumeProbeTime<0 && commands.empty(),"a fresh packet did not keep the connection");
    c.clientTime+=60000;check(!c.CheckResume() && commands.empty(),"a confirmed connection reconnected later");
    // A short stay with no packet since: reconnect after net_resumeProbe, once.
    c=fresh();c.ApplicationResumed(3000);c.clientTime+=4999;
    check(!c.CheckResume() && commands.empty(),"reconnected before net_resumeProbe");
    c.clientTime+=1;check(c.CheckResume() && reconnecting() && c.resumeProbeTime<0,"a silent connection did not reconnect");
    check(!c.CheckResume() && reconnecting(),"the probe reconnected twice");
    // A packet received just before the return does not count.
    c=fresh();c.lastPacketTime=c.clientTime;c.ApplicationResumed(3000);c.clientTime+=6000;
    check(c.CheckResume() && reconnecting(),"a packet from before the return proved the connection");
    // Never: inactive, still connecting, a listen server, a demo, or turned off.
    for(int no=0;no<5;++no){
        c=fresh();
        if(no==0)c.active=false;
        if(no==1)c.clientState=CS_CONNECTING;
        if(no==2)idAsyncNetwork::server.active=true;
        if(no==3)idAsyncNetwork::multiViewDemo.playing=true;
        if(no==4)net_resumeReconnect.value="0";
        c.ApplicationResumed(90000);c.clientTime+=90000;
        check(!c.CheckResume() && commands.empty() && !c.cleared,"a client that should stay reconnected or probed");
    }
    // A later resume replaces an unconfirmed probe.
    c=fresh();c.ApplicationResumed(3000);c.clientTime+=1000;c.ApplicationResumed(2000);
    check(c.resumeProbeTime==2000 && c.cleared==2,"a second resume kept the first probe");
    std::puts("PASS: async client resume: reconnect beyond the time-out, fresh-packet probe, probe time-out, stale packets and every exclusion, and the dropped channel's disconnect during the reconnect");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''


def source() -> str:
    client = (ROOT / 'src/framework/async/AsyncClient.cpp').read_text(encoding='utf-8')
    start = client.index('static idCVar net_resumeReconnect(')
    cvars = client[start:client.index('/*', start)]
    bodies = '\n'.join(function_body(client, signature) for signature in (
        'void idAsyncClient::ApplicationResumed(', 'void idAsyncClient::ResumeReconnect(', 'bool idAsyncClient::CheckResume(',
        'bool idAsyncClient::StaleDisconnectAfterResume('))
    disconnect = function_body(client, 'void idAsyncClient::ProcessDisconnectMessage(')
    assert disconnect.index('StaleDisconnectAfterResume()') < disconnect.index('session->MessageBox('), 'a stale disconnect still ends the reconnect'
    frame = function_body(client, 'void idAsyncClient::RunFrame(')
    assert 'if ( CheckResume() || CheckTimeout() ) {' in frame, 'RunFrame no longer probes a resumed connection'
    return SUPPORT + cvars + bodies + MAIN


def main() -> int:
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='async-client-resume-', dir=ROOT / '.tmp') as directory:
        cpp = Path(directory) / 'resume.cpp'
        exe = cpp.with_suffix('.exe')
        cpp.write_text(source(), encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', str(cpp), '-o', str(exe)],
                       check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
