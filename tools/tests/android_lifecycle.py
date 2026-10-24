#!/usr/bin/env python3
"""Production lifecycle dispatch/resume with in-memory window, input and audio APIs.

No OS input, audio playback or screen capture is performed. Android and desktop
branches run independently; physical suspend/resume remains a device check.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
// Use host standard library declarations, then select the engine branch.
// Apple's libc++ reads Android headers under __ANDROID__.
#if defined(__ANDROID__)
#undef __ANDROID__
#define TEST_ANDROID 1
#endif
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(TEST_ANDROID)
#define __ANDROID__ 1
#endif
#define SDLCALL
#define USE_SDL3 1
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
enum {SDL_EVENT_TERMINATING=1,SDL_EVENT_WILL_ENTER_BACKGROUND,SDL_EVENT_DID_ENTER_BACKGROUND,
      SDL_EVENT_WILL_ENTER_FOREGROUND,SDL_EVENT_DID_ENTER_FOREGROUND,SDL_EVENT_LOW_MEMORY};
struct SDL_Event {int type;};
struct SDL_AtomicInt {int value=0;};
static SDL_AtomicInt s_sdlLifecyclePending;
static SDL_AtomicInt s_sdlAppBackgrounded;
static int SDL_GetAtomicInt(SDL_AtomicInt* value){return value->value;}
static int SDL_SetAtomicInt(SDL_AtomicInt* value,int replacement){int old=value->value;value->value=replacement;return old;}
static bool SDL_CompareAndSwapAtomicInt(SDL_AtomicInt* value,int expected,int replacement){if(value->value!=expected)return false;value->value=replacement;return true;}
static constexpr int SDL3_LIFECYCLE_PENDING_BACKGROUND=1,SDL3_LIFECYCLE_PENDING_FOREGROUND=2,SDL3_LIFECYCLE_PENDING_LOW_MEMORY=4;
static constexpr int SDL_SENSOR_GYRO=1,SDL_WINDOW_MOUSE_FOCUS=1,CVAR_ARCHIVE=1,MAX_KEYS=8;
static constexpr const char* CONFIG_FILE="test.cfg";
static bool engineThread=true,s_sdlAppInBackground=false,s_sdlAppSuspendedSinceQuery=false;
static bool s_menuMouseInsideWindow=true,s_gamepadGyroEnabled=true,s_sdlFocusInputReleased=false;
static int pad=1,window=1;static int* s_sdlGamepad=&pad;static int* s_sdlJoystick=nullptr;static int* s_sdlWindow=&window;
static int s_retainedKeyOwners[4]={};
static int writes=0,inputReleases=0,usercmdClears=0,rumbleStops=0,menus=0,resumes=0;
static int touchReleases=0;
static void AndroidTouchControls_Release(){++touchReleases;}
static void Quake4_ClearTouchInput(){}
static void Quake4_ResetTouchState(){}
static bool devicePaused=false,com_editorActive=false;
static int setupRequests=0,setupWarnings=0;static bool setupMessageSucceeds=true;
static bool SDL_SendAndroidMessage(unsigned command,int param){require(command==0x8000 && param==0,"wrong setup command");++setupRequests;return setupMessageSucceeds;}
static const char* SDL_GetError(){return "injected setup failure";}
static std::vector<std::string> actions;
static bool Posix_IsMainThread(){return engineThread;}
static int Sys_Milliseconds(){return 100;}
static void S_SetDevicePaused(bool paused){devicePaused=paused;if(!paused)++resumes;actions.push_back(paused?"pause":"resume");}
static int trims=0;
static void S_TrimMemory(const char* reason){require(!std::strcmp(reason,"os low-memory"),"wrong trim reason");require(engineThread,"audio trimmed off the main thread");++trims;}
struct {bool activeApp=true,movingWindow=false;} win32;
namespace openq4 {static void NativeInputBeforeWindowChange(){}}
struct Key {bool down=false;};static Key keyStorage[MAX_KEYS];static Key* keys=keyStorage;
struct Usercmd {void Clear(){++usercmdClears;}} usercmdValue,*usercmdGen=&usercmdValue;
struct idKeyInput {
    static void ClearStates();
    static void PreliminaryKeyEvent(int index,bool down){keys[index].down=down;++inputReleases;}
};
static void SDL3_QueueRetainedFocus(bool,int){}
static void SDL3_ClearInputQueues(){}
static void Sys_GrabMouseCursor(bool){}
static void SDL3_StopControllerRumble(){++rumbleStops;}
static void SDL3_ReleaseGamepadState(int){}
static void SDL3_ReleaseJoystickState(int){}
static void SDL3_ClearJoystickState(){}
static void SDL3_InvalidateMenuMouseRouting(){}
static bool SDL_SetGamepadSensorEnabled(int*,int,bool){return true;}
static void SDL3_UpdateCursorVisibility(){}
static bool Sys_SDL_IsGameWindowFocused(){return !s_sdlAppInBackground && s_sdlWindow;}
static int SDL_GetWindowFlags(int*){return SDL_WINDOW_MOUSE_FOCUS;}
static void SDL3_OpenFirstController(){}
static void SDL3_UpdateGamepadSensorState(bool){}
static void SDL3_UpdateGamepadAxes(int){}
static void SDL3_UpdateJoystickAxes(){}
struct Common {bool IsInitialized(){return true;}void Printf(const char*,...){}void Warning(const char*,...){++setupWarnings;}void WriteConfigToFile(const char*){++writes;}} commonValue,*common=&commonValue;
struct Cvars {bool IsInitialized(){return true;}void SetModifiedFlags(int){}} cvarValue,*cvarSystem=&cvarValue;
struct Files {bool IsInitialized(){return true;}} fileValue,*fileSystem=&fileValue;
struct idAsyncNetwork {static bool active;static bool IsActive(){return active;}};bool idAsyncNetwork::active=false;
struct idSessionLocal {
    bool mapSpawned=true,insideExecuteMapChange=false,gui=false,readDemo=false;
    bool IsGUIActive(){return gui;}
    void StartMenu(){gui=true;++menus;actions.push_back("menu");}
    void SetPlayingSoundWorld(){}
    void HandleAppResume(bool);
} sessionValue,*session=&sessionValue;
static void SDL3_HandleAppBackgroundTransition(int,const char*);
static void SDL3_HandleAppForegroundTransition(int,const char*);
'''
MAIN = r'''
static void reset(){
    s_sdlLifecyclePending.value=s_sdlAppBackgrounded.value=0;s_sdlAppInBackground=s_sdlAppSuspendedSinceQuery=false;
    s_sdlFocusInputReleased=false;engineThread=true;devicePaused=false;win32.activeApp=true;
    keys=keyStorage;for(auto& key:keyStorage)key.down=false;keys[2].down=keys[6].down=true;
    writes=inputReleases=usercmdClears=rumbleStops=menus=resumes=touchReleases=0;
    sessionValue=idSessionLocal{};idAsyncNetwork::active=com_editorActive=false;actions.clear();
}
static void send(int type){SDL_Event event{type};SDL3_LifecycleEventWatch(nullptr,&event);}
int main(){try {
    reset();require(SDL3_LifecycleEventWatch(nullptr,nullptr),"null event rejected");
    send(SDL_EVENT_WILL_ENTER_BACKGROUND);send(SDL_EVENT_DID_ENTER_BACKGROUND);
#if defined(__ANDROID__)
    // The application is now parked inside SDL_PollEvent. Its queued-event
    // handler has not run; sound and held input must already be released.
    require(devicePaused && s_sdlAppInBackground && Sys_SDL_IsAppBackgrounded(),"Android watch waited for the blocked event queue");
    require(inputReleases==2 && rumbleStops==1 && writes==1,"background transition did not release once");
    require(touchReleases==1,"background transition left touch actions held");
    require(!Sys_SDL_ConsumeAppSuspension(),"session consumed suspension while still backgrounded");
    SDL3_ProcessPendingLifecycleEvents(100);
    require(writes==1 && inputReleases==2,"queued background replay repeated cleanup");
    require(touchReleases==1,"queued background replay repeated touch release");
    send(SDL_EVENT_WILL_ENTER_FOREGROUND);send(SDL_EVENT_DID_ENTER_FOREGROUND);
    require(devicePaused,"event watch resumed audio before session preparation");
    SDL3_ProcessPendingLifecycleEvents(101);
    require(!s_sdlAppInBackground && !Sys_SDL_IsAppBackgrounded() && devicePaused,"foreground bypassed session audio handoff");
    sessionValue.HandleAppResume(false);
    require(menus==1 && resumes==1 && !devicePaused,"live SP failed to pause behind menu on return");
    require(actions[actions.size()-2]=="menu" && actions.back()=="resume","audio resumed before the pause menu");
    sessionValue.HandleAppResume(false);require(menus==1 && resumes==1,"suspension consumed twice");
    // A foreign-thread watch only publishes pending work; no engine API calls.
    reset();engineThread=false;send(SDL_EVENT_DID_ENTER_BACKGROUND);
    require(!devicePaused && !writes && !inputReleases,"foreign-thread callback touched engine state");
    engineThread=true;SDL3_ProcessPendingLifecycleEvents(102);
    require(devicePaused && writes==1 && inputReleases==2,"deferred main-thread suspension lost");
    // Menu/load/demo/editor/network flows resume audio without inventing a menu.
    for(int flow=0;flow<7;++flow){
        reset();s_sdlAppSuspendedSinceQuery=true;devicePaused=true;
        if(flow==0)sessionValue.mapSpawned=false;
        if(flow==1)sessionValue.gui=true;
        if(flow==2)sessionValue.insideExecuteMapChange=true;
        if(flow==3)sessionValue.readDemo=true;
        if(flow==4)com_editorActive=true;
        if(flow==5)idAsyncNetwork::active=true;
        sessionValue.HandleAppResume(flow==6);
        require(!menus && resumes==1 && !devicePaused,"non-gameplay resume opened a menu or stayed silent");
        require(!Sys_SDL_ConsumeAppSuspension(),"non-gameplay resume left a stale pause request");
    }
    // Two complete suspend cycles release newly held controls independently.
    reset();
    for(int cycle=0;cycle<2;++cycle){
        keys[2].down=keys[6].down=true;
        send(SDL_EVENT_WILL_ENTER_BACKGROUND);SDL3_ProcessPendingLifecycleEvents(103);
        send(SDL_EVENT_DID_ENTER_FOREGROUND);SDL3_ProcessPendingLifecycleEvents(104);
        sessionValue.HandleAppResume(false);sessionValue.gui=false;
    }
    require(writes==2 && inputReleases==4 && menus==2 && resumes==2,"second suspend/resume cycle changed behavior");
    require(touchReleases==2,"second suspend failed to release touch actions");
#else
    require(!s_sdlAppInBackground && !writes && !devicePaused,"desktop watch became immediate");
    SDL3_ProcessPendingLifecycleEvents(100);
    require(s_sdlAppInBackground && writes==1 && inputReleases==2 && !devicePaused,"desktop deferred transition changed");
    send(SDL_EVENT_DID_ENTER_FOREGROUND);SDL3_ProcessPendingLifecycleEvents(101);
    sessionValue.HandleAppResume(false);require(!menus && !resumes && !s_sdlAppInBackground,"desktop acquired Android resume behavior");
    require(touchReleases==0,"desktop invoked Android touch controls");
#endif
    // A low-memory notice from any thread trims audio once, on the main thread.
    reset();trims=0;engineThread=false;send(SDL_EVENT_LOW_MEMORY);
    require(!trims,"low-memory watch trimmed audio before the main thread ran");
    engineThread=true;SDL3_ProcessPendingLifecycleEvents(105);require(trims==1,"pending low-memory notice did not trim audio");
    SDL3_ProcessPendingLifecycleEvents(106);require(trims==1,"one low-memory notice trimmed audio twice");
    reset();keys=nullptr;idKeyInput::ClearStates();require(!usercmdClears,"uninitialized input touched usercmd state");
    keys=keyStorage;idKeyInput::ClearStates();require(inputReleases==2 && usercmdClears==1,"live input clearing changed");
    keys=nullptr;idKeyInput::ClearStates();
    std::puts("Lifecycle: immediate Android suspension, main-thread deferral, single-consumption SP/menu/load/network resume and inactive-key teardown passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
'''


def source():
    backend = (ROOT/'src/sys/sdl3/sdl3_backend.cpp').read_text()
    session = (ROOT/'src/framework/Session.cpp').read_text()
    frame = function_body(session, 'void idSessionLocal::Frame()')
    assert frame.index('HandleAppResume( false );') < frame.index('soundSystem->Render();')
    load = function_body(session, 'void idSessionLocal::ExecuteMapChange(')
    assert 'HandleAppResume( true );' in load
    code = function_body((ROOT/'src/framework/KeyInput.cpp').read_text(), 'void idKeyInput::ClearStates(')
    code += '\n'.join(function_body(backend, signature) for signature in (
        'static void SDL3_ReleaseFocusInputState(', 'static void SDL3_SetLifecyclePendingFlag(',
        'static bool SDLCALL SDL3_LifecycleEventWatch(',
        'static void SDL3_HandleAppBackgroundTransition(int eventTime, const char *reason) {',
        'static void SDL3_HandleAppForegroundTransition(int eventTime, const char *reason) {',
        'static void SDL3_ProcessPendingLifecycleEvents(', 'bool Sys_SDL_IsAppBackgrounded(',
        'bool Sys_SDL_ConsumeAppSuspension('))
    menu = (ROOT/'src/framework/Session_menu.cpp').read_text()
    setup = '\n'.join(function_body(menu, signature) for signature in (
        'static bool Session_AndroidSetupAvailable(', 'static void Session_OpenAndroidSetup('))
    checks = '''
    Session_OpenAndroidSetup();setupMessageSucceeds=false;Session_OpenAndroidSetup();
#if defined(__ANDROID__) && !defined(OPENQ4_SIGMATOUCH)
    require(Session_AndroidSetupAvailable() && setupRequests==2 && setupWarnings==1,"Android setup handoff/failure mismatch");
#else
    require(!Session_AndroidSetupAvailable() && setupRequests==0 && setupWarnings==0,"setup advertised outside standalone Android");
#endif
#if defined(OPENQ4_SIGMATOUCH)
    std::puts("Setup re-entry is unavailable to the separate SigmaTouch host");return 0;
#endif
'''
    entry = 'int main(){try {'
    assert MAIN.count(entry) == 1, 'fixture entry point changed'
    return SUPPORT + setup + code + function_body(session, 'void idSessionLocal::HandleAppResume(') + MAIN.replace(entry, entry + checks)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--generate-native':
        Path(sys.argv[2]).write_text(source(), encoding='utf-8')
        return 0
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='android-lifecycle-', dir=ROOT/'.tmp') as directory:
        cpp = Path(directory)/'lifecycle.cpp';exe = cpp.with_suffix('.exe')
        cpp.write_text(source(), encoding='utf-8')
        for platform in ('android', 'desktop', 'sigmatouch'):
            defines = ['-D__ANDROID__=1'] if platform != 'desktop' else []
            if platform == 'sigmatouch': defines.append('-DOPENQ4_SIGMATOUCH=1')
            subprocess.run([compiler, '-std=c++17', *defines, str(cpp), '-o', str(exe)], check=True)
            print(platform, flush=True)
            subprocess.run([str(exe)], check=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
