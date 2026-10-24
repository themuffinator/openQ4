#!/usr/bin/env python3
"""Exercise production Android timer pacing and bounded scheduler catch-up."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body
from presentation_clock_ownership import definition

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
// Use host standard library declarations, then select the engine branch.
// Apple's libc++ reads Android headers under __ANDROID__.
#if defined(__ANDROID__)
#undef __ANDROID__
#define TEST_ANDROID 1
#endif
#include <atomic>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <vector>
#if defined(TEST_ANDROID)
#define __ANDROID__ 1
#endif
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int now=0,com_ticNumber=0,perCall=0,asyncCalls=0,events=0;
static bool com_shuttingDown=false,lastTicMsecValid=true,smooth=false,stopRequested=false;
static double lastTicMsec=0;
static int Sys_Milliseconds(){perCall=0;++asyncCalls;return now;}
struct Cvar {float value=1;bool GetBool(){return value!=0;}float GetFloat(){return value;}} com_preciseTic,com_timescale;
static std::atomic<float> openQ4_gameTimeScale{1};
static bool openQ4_ShouldUseSmoothSingleplayerSlowTime(float){return smooth;}
struct idCommonLocal {
    void Async();
    void SingleAsyncTic(){if(++perCall>512)throw std::runtime_error("unbounded asynchronous catch-up");++com_ticNumber;}
    int GetUserCmdMsecNumerator()const{return 1000;}
    int GetUserCmdMsecDenominator()const{return 60;}
    /* @USERCMD_INTERVAL@ */
} commonValue,*common=&commonValue;
struct SDL_AtomicInt {std::atomic<int> value{0};};static SDL_AtomicInt s_sdlAppBackgrounded;
static int SDL_GetAtomicInt(SDL_AtomicInt* value){return value->value.load();}
static std::vector<bool> plannedBackground;
static std::vector<unsigned> sleeps;
static int stopOnSleep=0,freezeOnSleep=0;
static bool Sys_IsCurrentThreadStopRequested(){
    if(stopRequested)return true;
    require(sleeps.size()<plannedBackground.size(),"worker failed to stop");
    s_sdlAppBackgrounded.value.store(plannedBackground[sleeps.size()]);
    return false;
}
static int TestSleep(unsigned microseconds){
    const bool background=plannedBackground[sleeps.size()];
    require(microseconds==(background?100000u:1000u),"background worker still wakes every millisecond");
    sleeps.push_back(microseconds);now+=microseconds/1000;
    if(freezeOnSleep==static_cast<int>(sleeps.size()))now+=3600000;
    if(stopOnSleep==static_cast<int>(sleeps.size()) || sleeps.size()==plannedBackground.size())stopRequested=true;
    return 0;
}
#define usleep TestSleep
static constexpr int TRIGGER_EVENT_ONE=1;
static void Sys_TriggerEvent(int event){require(event==TRIGGER_EVENT_ONE,"wrong wake event");++events;}
'''
MAIN = r'''
static void reset(float scale=1,bool smoothTime=false){
    now=com_ticNumber=perCall=asyncCalls=events=0;lastTicMsec=0;lastTicMsecValid=true;
    com_shuttingDown=stopRequested=false;smooth=smoothTime;
    com_preciseTic.value=1;com_timescale.value=scale;openQ4_gameTimeScale.store(1);
    sleeps.clear();plannedBackground.clear();stopOnSleep=freezeOnSleep=0;
}
int main(){try {
    // Frequent foreground wakes retain the requested cadence at every scale.
    for(float scale:{.1f,.5f,1.0f,2.0f,10.0f})for(bool slow:{false,true}){
        if(slow && scale>=1)continue;
        reset(scale,slow);
        for(now=1;now<=1000;++now)common->Async();
        const int expected=static_cast<int>(std::floor(1000/(slow?common->GetUserCmdMsecFloat():std::fmax(1.0,common->GetUserCmdMsecFloat()/scale))));
        require(std::abs(com_ticNumber-expected)<=1,"foreground timescale cadence changed");
    }
#if defined(__ANDROID__)
    // A process can be frozen after foreground was already announced. The
    // clock budget must hold without relying on a currently-backgrounded flag.
    for(float scale:{.1f,.5f,1.0f,2.0f,10.0f})for(float gameScale:{.25f,1.0f,2.0f}){
        reset(scale);openQ4_gameTimeScale.store(gameScale);now=3600000;common->Async();
        require(perCall>0 && perCall<=10,"Android freeze replayed more than ten tics");
        ++now;common->Async();require(perCall<=1,"Android left a catch-up backlog after resume");
    }
    for(bool frozen:{false,true}){
        reset(2);plannedBackground={false,true,true,false};freezeOnSleep=frozen?2:0;
        Sys_AsyncThread();
        require(sleeps==std::vector<unsigned>({1000,100000,100000,1000}),"unexpected foreground/background intervals");
        require(asyncCalls==3 && events==com_ticNumber && com_ticNumber<=30,"worker emitted extra tics or wake events");
    }
    reset();plannedBackground={true,false};stopOnSleep=1;Sys_AsyncThread();
    require(sleeps.size()==1 && sleeps[0]==100000 && !asyncCalls && !events,"shutdown ran async work after its long sleep");
    reset();stopRequested=true;Sys_AsyncThread();require(sleeps.empty() && !asyncCalls,"stopped worker slept or advanced time");
#else
    // Preserve the existing desktop accelerated-time policy; only Android
    // changes its long-suspension budget in this work item.
    reset(2);now=2000;common->Async();require(perCall>=239 && perCall<=240,"desktop accelerated catch-up changed");
#endif
    reset(.25f,true);now=3600000;common->Async();require(perCall>0 && perCall<=10,"smooth slow-time budget changed");
    reset();com_preciseTic.value=0;now=3600000;common->Async();require(com_ticNumber==1,"imprecise timer no longer emits one tic");
    reset();lastTicMsecValid=false;now=100;common->Async();require(com_ticNumber==1,"initial timer phase changed");
    reset();com_shuttingDown=true;common->Async();require(!asyncCalls && !com_ticNumber,"shutdown advanced the timer");
    std::puts("Async lifecycle: foreground cadence, background intervals, frozen/accelerated catch-up, wake counts and bounded shutdown passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''


def source():
    common = (ROOT/'src/framework/Common.cpp').read_text()
    interval = definition((ROOT/'src/framework/Common.h').read_text(), 'GetUserCmdMsecFloat')
    backend = (ROOT/'src/sys/sdl3/sdl3_backend.cpp').read_text()
    thread = (ROOT/'src/sys/android/android_main.cpp').read_text()
    return (SUPPORT.replace('/* @USERCMD_INTERVAL@ */', interval)
            + function_body(backend, 'bool Sys_SDL_IsAppBackgrounded(')
            + function_body(common, 'void idCommonLocal::Async(')
            + function_body(thread, 'void Sys_AsyncThread(') + MAIN)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--generate-native':
        Path(sys.argv[2]).write_text(source(), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='android-async-', dir=ROOT/'.tmp') as directory:
        cpp=Path(directory)/'async.cpp';exe=cpp.with_suffix('.exe')
        cpp.write_text(source(), encoding='utf-8')
        for platform in ('android', 'desktop'):
            defines=['-D__ANDROID__=1'] if platform=='android' else []
            subprocess.run([compiler,'-std=c++17',*defines,str(cpp),'-o',str(exe)],check=True)
            print(platform,flush=True)
            subprocess.run([str(exe)],check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
