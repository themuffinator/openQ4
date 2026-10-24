#!/usr/bin/env python3
"""Run production console input/layout with counted SDL APIs, without real input."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import sys
import tempfile

from filesystem_case_segments import function_body
from pinned_archive_headers import pinned_include

ROOT=Path(__file__).resolve().parents[2]
SUPPORT=r'''
// Use host SDL and standard library declarations for a host executable, then
// select the engine branch. Apple's libc++ reads Android headers under __ANDROID__.
#if defined(__ANDROID__)
#undef __ANDROID__
#define TEST_ANDROID 1
#endif
#define SDL_MAIN_HANDLED 1
#define SDL_STATIC_LIB 1
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#if defined(TEST_ANDROID)
#define __ANDROID__ 1
#endif
#include "sys/sys_console_theme.h"
#define USE_SDL3 1
#undef INT_MAX
template<class T> T Max(T a,T b){return a>b?a:b;}
namespace idMath {
static constexpr int INT_MAX=2147483647;
static int ClampInt(int low,int high,int value){return value<low?low:(value>high?high:value);}
static float ClampFloat(float low,float high,float value){return value<low?low:(value>high?high:value);}
}
// ENGINE_KEY_ENUM
static int s_rightAltKey=K_ALT;
struct idEditField {void AutoComplete(){} void CharEvent(int){} void KeyDownEvent(int){} void SetCursor(int){} };
struct Cvar {void SetInteger(int){} void ClearModified(){}} sys_viewlog,sys_winViewlogAlias;
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static int windowWidth=2340,windowHeight=1080,logicalWidth=0,logicalHeight=0;
static float displayScale=3,pixelDensity=1;
static int copies=0,clears=0,commands=0,keyboards=0,keyboardStops=0;
static void Sys_Printf(const char*,...){}
extern "C" {
bool SDL_GetWindowSize(SDL_Window*,int* w,int* h){*w=windowWidth;*h=windowHeight;return true;}
float SDL_GetWindowDisplayScale(SDL_Window*){return displayScale;}
float SDL_GetWindowPixelDensity(SDL_Window*){return pixelDensity;}
bool SDL_SetRenderLogicalPresentation(SDL_Renderer*,int w,int h,SDL_RendererLogicalPresentation){logicalWidth=w;logicalHeight=h;return true;}
bool SDL_RenderCoordinatesFromWindow(SDL_Renderer*,float x,float y,float* outX,float* outY){*outX=x*logicalWidth/windowWidth;*outY=y*logicalHeight/windowHeight;return true;}
const char* SDL_GetError(){return "fixture error";}
bool SDL_StartTextInput(SDL_Window*){++keyboards;return true;}
bool SDL_StopTextInput(SDL_Window*){++keyboardStops;return true;}
bool SDL_SetTextInputArea(SDL_Window*,const SDL_Rect*,int){return true;}
bool SDL_HideWindow(SDL_Window*){return true;}
void SDL_DestroyRenderer(SDL_Renderer*){}
void SDL_DestroyWindow(SDL_Window*){}
void SDL_QuitSubSystem(SDL_InitFlags){}
}
static void Posix_ConsoleCopyAll(){++copies;}
static void Posix_ConsoleClearBuffer(){++clears;}
static void Posix_ConsoleQueueCommand(const char*){++commands;}
static void Posix_ConsolePasteClipboard(){}
static void Posix_ConsoleSubmitInput(){}
static void Posix_ConsoleHistory(int){}
static void Posix_ConsoleHandleText(const char*){}
static const char* Posix_ConsoleInputState(int& length,int& cursor){length=cursor=0;return "";}
static void Posix_SplashDestroy(){}
'''
MAIN=r'''
static void reset(int width=2340,int height=1080,float scale=3){
    s_consoleWindow=posixConsoleWindow_t{};windowWidth=width;windowHeight=height;displayScale=scale;pixelDensity=1;
    s_consoleWindow.window=reinterpret_cast<SDL_Window*>(1);
    s_consoleWindow.renderer=reinterpret_cast<SDL_Renderer*>(2);
    s_consoleWindow.windowID=7;s_consoleWindow.visible=true;
    s_consoleWindow.forceFatalWindow=s_consoleWindow.quitOnClose=true;
    copies=clears=commands=keyboards=keyboardStops=0;
    Posix_ConsoleUpdateLayout();
}
static bool finger(SDL_EventType type,float x,float y,SDL_FingerID id=11,SDL_TouchID device=23,SDL_WindowID window=7){
    SDL_Event event{};event.tfinger.type=type;event.tfinger.windowID=window;
    event.tfinger.fingerID=id;event.tfinger.touchID=device;
    event.tfinger.x=x/logicalWidth;event.tfinger.y=y/logicalHeight;
    return Posix_ConsoleProcessEvent(&event);
}
static void pressButton(int button,SDL_EventType type=SDL_EVENT_FINGER_DOWN,SDL_FingerID id=11){
    auto r=s_consoleWindow.layout.buttonRects[button];finger(type,r.x+r.w*.5f,r.y+r.h*.5f,id);
}
static void lifecycle(SDL_EventType type){SDL_Event event{};event.window.type=type;event.window.windowID=7;Posix_ConsoleProcessEvent(&event);}
static void back(bool down=true,bool repeat=false,SDL_WindowID window=7){
    SDL_Event event{};event.key.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;event.key.key=SDLK_AC_BACK;
    event.key.down=down;event.key.repeat=repeat;event.key.windowID=window;Posix_ConsoleProcessEvent(&event);
}
int main(){try{
    require(SDL3_MapPhysicalScancode(SDL_SCANCODE_ESCAPE)==K_ESCAPE && SDL3_MapKeycode(SDLK_ESCAPE)==K_ESCAPE,"ordinary Escape mapping changed");
    reset();Posix_ConsoleStartTextInput();
#if defined(__ANDROID__)
    require(SDL3_MapPhysicalScancode(SDL_SCANCODE_AC_BACK)==K_ESCAPE && SDL3_MapKeycode(SDLK_AC_BACK)==K_ESCAPE,"Android Back did not map to normal menu/pause navigation");
    require(!keyboards && keyboardStops==1,"fatal console opened the on-screen keyboard");
    for(int width:{960,1080,2340}){
        reset(width,width==2340?1080:2340);
        require(logicalWidth==width/3,"Android console ignored display density");
        const auto& rects=s_consoleWindow.layout.buttonRects;
        for(const auto& r:rects)require(r.w>=48 && r.h>=48 && r.x>=0 && r.x+r.w<=logicalWidth && r.y+r.h<=logicalHeight,"mobile button is too small or outside the view");
        require(rects[1].x-rects[0].x-rects[0].w>=8 && rects[2].x-rects[1].x-rects[1].w>=8,"touch targets overlap");
        require(s_consoleWindow.layout.outputRect.h>0,"density layout lost its log pane");
        pressButton(POSIX_CONSOLE_BUTTON_COPY);require(!copies,"touch activated on press");
        pressButton(POSIX_CONSOLE_BUTTON_COPY,SDL_EVENT_FINGER_UP);require(copies==1,"touch did not activate on release");
        require(!s_consoleWindow.touch.active,"release kept its touch owner");
    }
    reset();pressButton(POSIX_CONSOLE_BUTTON_QUIT);pressButton(POSIX_CONSOLE_BUTTON_COPY,SDL_EVENT_FINGER_DOWN,12);
    pressButton(POSIX_CONSOLE_BUTTON_QUIT,SDL_EVENT_FINGER_UP,12);require(!s_consoleWindow.exitRequested,"second finger activated the first finger's button");
    auto quit=s_consoleWindow.layout.buttonRects[POSIX_CONSOLE_BUTTON_QUIT];
    finger(SDL_EVENT_FINGER_UP,quit.x+quit.w/2,quit.y+quit.h/2,11,99);
    require(!s_consoleWindow.exitRequested,"another touch device released the owner");
    pressButton(POSIX_CONSOLE_BUTTON_QUIT,SDL_EVENT_FINGER_UP);require(s_consoleWindow.exitRequested && !commands,"fatal Quit failed to end its wait");
    for(SDL_EventType type:{SDL_EVENT_FINGER_MOTION,SDL_EVENT_FINGER_UP}){
        reset();pressButton(POSIX_CONSOLE_BUTTON_COPY);auto r=s_consoleWindow.layout.buttonRects[0];
        finger(type,r.x+r.w/2+9,r.y+r.h/2);pressButton(0,SDL_EVENT_FINGER_UP);
        require(!copies,"drag beyond eight dp still clicked");
    }
    reset();auto r=s_consoleWindow.layout.buttonRects[0];
    finger(SDL_EVENT_FINGER_DOWN,r.x+1,r.y+1);finger(SDL_EVENT_FINGER_UP,r.x-1,r.y+1);require(!copies,"outside release activated a button");
    reset();pressButton(0);finger(SDL_EVENT_FINGER_CANCELED,0,0);pressButton(0,SDL_EVENT_FINGER_UP);
    require(!copies && !s_consoleWindow.touch.active,"cancelled finger retained activation");
    for(float invalid:{-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        reset();pressButton(0);finger(SDL_EVENT_FINGER_MOTION,invalid,10);pressButton(0,SDL_EVENT_FINGER_UP);
        require(!copies,"malformed touch motion restored activation");
    }
    for(SDL_EventType type:{SDL_EVENT_WINDOW_HIDDEN,SDL_EVENT_WINDOW_FOCUS_LOST,SDL_EVENT_WINDOW_RESIZED,SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
            SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED,SDL_EVENT_WILL_ENTER_BACKGROUND,SDL_EVENT_DID_ENTER_BACKGROUND}){
        reset();pressButton(0);lifecycle(type);pressButton(0,SDL_EVENT_FINGER_UP);
        require(!copies && !s_consoleWindow.touch.active,"lifecycle change kept a touch press");
    }
    reset();pressButton(0);Posix_ConsoleHide();pressButton(0,SDL_EVENT_FINGER_UP);require(!copies,"hide kept a touch press");
    reset();pressButton(0);Posix_ShutdownConsole();require(!s_consoleWindow.touch.active,"teardown kept a touch owner");
    reset();require(!finger(SDL_EVENT_FINGER_DOWN,10,10,11,23,8),"foreign window touch was consumed");
    require(!s_consoleWindow.touch.active,"foreign window claimed touch ownership");
    reset();r=s_consoleWindow.layout.buttonRects[0];
    finger(SDL_EVENT_FINGER_DOWN,r.x+r.w/2,r.y+r.h/2,11,SDL_MOUSE_TOUCHID);
    require(!s_consoleWindow.touch.active,"synthetic mouse touch claimed ownership");
    // SDL can deliver a synthetic mouse event before the corresponding finger
    // event. It must neither activate twice nor override a cancelled gesture.
    for(bool drag:{false,true}){
        reset();r=s_consoleWindow.layout.buttonRects[0];SDL_Event mouse{};
        mouse.button.type=SDL_EVENT_MOUSE_BUTTON_DOWN;mouse.button.windowID=7;
        mouse.button.which=SDL_TOUCH_MOUSEID;mouse.button.button=SDL_BUTTON_LEFT;
        mouse.button.x=(r.x+r.w/2)*windowWidth/logicalWidth;
        mouse.button.y=(r.y+r.h/2)*windowHeight/logicalHeight;
        Posix_ConsoleProcessEvent(&mouse);pressButton(0);
        if(drag)finger(SDL_EVENT_FINGER_MOTION,r.x+r.w/2+9,r.y+r.h/2);
        mouse.button.type=SDL_EVENT_MOUSE_BUTTON_UP;Posix_ConsoleProcessEvent(&mouse);
        pressButton(0,SDL_EVENT_FINGER_UP);
        require(copies==(drag?0:1),"synthetic mouse bypassed cancellation or changed activation count");
    }
    reset();
    back(false);back(true,true);back(true,false,8);require(!s_consoleWindow.exitRequested,"Back up/repeat/foreign window closed the console");
    back();require(s_consoleWindow.exitRequested,"Back did not dismiss the fatal console");
    reset();s_consoleWindow.forceFatalWindow=s_consoleWindow.quitOnClose=false;back();
    require(!s_consoleWindow.visible && !commands && !s_consoleWindow.exitRequested,"ordinary console Back quit gameplay");
    reset();s_consoleWindow.forceFatalWindow=false;Posix_ConsoleStartTextInput();require(keyboards==1,"interactive console lost text entry");
    reset();displayScale=std::numeric_limits<float>::quiet_NaN();Posix_ConsoleUpdateLayout();require(logicalWidth==windowWidth,"invalid density was not rejected");
#else
    require(SDL3_MapPhysicalScancode(SDL_SCANCODE_AC_BACK)==0 && SDL3_MapKeycode(SDLK_AC_BACK)==0,"Android Back mapping leaked into desktop");
    require(keyboards==1,"desktop console text input changed");
    require(logicalWidth==windowWidth && logicalHeight==windowHeight,"mobile density leaked into desktop layout");
    require(s_consoleWindow.layout.buttonRects[0].h==POSIX_CONSOLE_BUTTON_HEIGHT,"desktop button height changed");
    require(!finger(SDL_EVENT_FINGER_DOWN,10,10),"desktop unexpectedly consumed touch");back();require(!s_consoleWindow.exitRequested,"Android Back behavior leaked into desktop");
#endif
    // Physical mouse release still works, including on the Android branch.
    reset();auto q=s_consoleWindow.layout.buttonRects[POSIX_CONSOLE_BUTTON_QUIT];SDL_Event mouse{};
    mouse.button.type=SDL_EVENT_MOUSE_BUTTON_DOWN;mouse.button.windowID=7;mouse.button.button=SDL_BUTTON_LEFT;
    mouse.button.x=(q.x+q.w/2)*windowWidth/logicalWidth;mouse.button.y=(q.y+q.h/2)*windowHeight/logicalHeight;
    Posix_ConsoleProcessEvent(&mouse);require(!s_consoleWindow.exitRequested,"mouse activated on press");
    mouse.button.type=SDL_EVENT_MOUSE_BUTTON_UP;Posix_ConsoleProcessEvent(&mouse);require(s_consoleWindow.exitRequested,"mouse release regressed");
    std::puts("Console: touch ownership/cancellation, density targets, Back mapping, fatal IME suppression and desktop/mouse controls passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
'''


def source():
    cpp=(ROOT/'src/sys/posix/posix_syscon.cpp').read_text()
    definitions='\n'.join(re.findall(r'^static const int POSIX_CONSOLE_\w+\s*=[^;]+;',cpp,re.M))
    definitions+='\n'+function_body(cpp,'enum posixConsoleButton_t')+';\n'
    definitions+='\n'+function_body(cpp,'struct posixConsoleLayout_t')+';\n'
    definitions+='\n'+function_body(cpp,'struct posixConsoleWindow_t')+';\n'
    definitions+='\nstatic posixConsoleWindow_t s_consoleWindow;\nstatic SDL_WindowID Posix_ConsoleWindowID(){return s_consoleWindow.windowID;}\n'
    names=[
        'static void Posix_ConsoleCancelTouch(', 'static void Posix_ConsoleApplyLogicalPresentation(',
        'static void Posix_ConsoleUpdateLayout(', 'static void Posix_ConsoleStartTextInput(',
        'static void Posix_ConsoleStopTextInput(', 'static void Posix_ConsoleHide(',
        'static bool Posix_ConsolePointInRect(', 'static void Posix_ConsoleWindowToRenderCoordinates(',
        'static int Posix_ConsoleButtonAtPoint(', 'static void Posix_ConsoleTrackPointer(',
        'static void Posix_ConsoleBeginPress(', 'static void Posix_ConsoleRequestQuit(',
        'static void Posix_ConsoleClickButton(', 'static void Posix_ConsoleHandleTouch(',
        'static void Posix_ConsoleHandleClose(', 'static bool Posix_ConsoleHandleKey(',
        'bool Posix_ConsoleProcessEvent(', 'void Posix_ShutdownConsole(']
    for name in names:
        method=function_body(cpp,name)
        if 'CancelTouch(' in name or 'HandleTouch(' in name:
            method='\n#if defined(__ANDROID__)\n'+method+'\n#endif\n'
        definitions+='\n'+method+'\n'
    backend=(ROOT/'src/sys/sdl3/sdl3_backend.cpp').read_text()
    for name in ('static int SDL3_MapPhysicalScancode(', 'static int SDL3_MapKeycode('):
        definitions+='\n'+function_body(backend,name)+'\n'
    keys=(ROOT/'src/framework/KeyInput.h').read_text()
    key_enum=re.search(r'typedef enum \{\s*K_TAB.*?\}\s*keyNum_t;',keys,re.S)[0]
    return SUPPORT.replace('// ENGINE_KEY_ENUM',key_enum)+definitions+MAIN


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate-native',type=Path)
    parser.add_argument('--sdl-include',type=Path,help='SDL3 include directory; defaults to the pinned Meson wrap, read from its archive when not provisioned')
    args=parser.parse_args()
    if args.generate_native:
        args.generate_native.write_text(source(),encoding='utf-8');return 0
    compiler=next((p for name in ('clang++','g++','c++') if (p:=shutil.which(name))),None)
    if not compiler:raise RuntimeError('C++17 compiler required')
    (ROOT/'.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='android-console-',dir=ROOT/'.tmp') as directory:
        sdl_include=args.sdl_include or pinned_include('sdl3','SDL3',Path(directory))
        if not (sdl_include/'SDL3/SDL.h').is_file():
            raise RuntimeError(f'SDL3 headers are missing from {sdl_include}')
        cpp=Path(directory)/'console.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(source(),encoding='utf-8')
        for platform in ('android','desktop'):
            defines=['-D__ANDROID__=1'] if platform=='android' else []
            subprocess.run([compiler,'-std=c++17',*defines,'-I'+str(sdl_include),
                            '-I'+str(ROOT/'src'),str(cpp),'-o',str(exe)],check=True)
            print(platform,flush=True);subprocess.run([str(exe)],check=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
