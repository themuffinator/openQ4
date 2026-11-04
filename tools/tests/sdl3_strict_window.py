#!/usr/bin/env python3
"""Compile real strict SDL window apply/query against counted SDL stand-ins.

Checks authoritative observations, exact mode selection, partial-failure output
atomicity and explicit restore placement. This does not create a real window,
drive input, qualify a compositor/monitor or prove renderer resource/present
success. Legacy windowing source guards run separately.
Video pin tests verify counted references, not SDL's real display-ID lifetime.
Native Wayland's per-window mode emulation is modelled from SDL 3.4.18; no
compositor is qualified.
The POD observes maximized geometry; it does not expose the compositor's hidden
normal restore rectangle, so complete restore-rectangle fidelity is unqualified.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#define OPENQ4_RENDERER_MODULE_ONLY
#include "src/renderer/RenderModuleAPI.h"
#include "src/sys/DisplayModeRule.h"
using SDL_DisplayID=unsigned;
using SDL_WindowFlags=unsigned long long;
constexpr SDL_WindowFlags SDL_WINDOW_FULLSCREEN=1,SDL_WINDOW_BORDERLESS=2,SDL_WINDOW_HIDDEN=4,
    SDL_WINDOW_MINIMIZED=8,SDL_WINDOW_MAXIMIZED=16,SDL_WINDOW_INPUT_FOCUS=32;
struct SDL_Rect { int x=0,y=0,w=0,h=0; };
struct SDL_DisplayMode { unsigned displayID=0;int w=0,h=0;float pixel_density=1,refresh_rate=60; };
struct SDL_Window {
    SDL_WindowFlags flags=SDL_WINDOW_INPUT_FOCUS;
    unsigned display=101;
    int x=50,y=60,w=1280,h=720;
    int safeLeft=0,safeTop=0,safeRight=0,safeBottom=0; // window-coordinate safe-area insets
    float densityX=1,densityY=1,scale=1;
    bool haveFullscreenMode=false;
    SDL_DisplayMode fullscreenMode;
} window,*s_sdlWindow=&window;
static bool wayland=false,emulatedModes=false,s_screenParmTransitionActive=false,s_windowAspectSnapActive=false;
static float s_windowAspectSnapRatio=0;
static unsigned viewportDisplay=101;
static int allocations=0,mutations=0,queries=0;
static bool noModes=false,noCurrentMode=false,noDesktopMode=false,silentSize=false,silentFullscreen=false,
    silentBorder=false,silentMove=false,failQueryAfterMutation=false,noBorders=false;
static std::string failure,lastError;
static std::vector<unsigned> displayIds={101,202};
static std::map<unsigned,SDL_Rect> displayBounds={{101,{0,0,1920,1080}},{202,{1920,0,1920,1080}}};
static std::map<unsigned,SDL_DisplayMode> desktopModes={{101,{101,1920,1080,1,60}},{202,{202,1920,1080,1,144}}};
static std::map<unsigned,SDL_DisplayMode> currentModes=desktopModes;
static std::vector<SDL_DisplayMode> modes={{101,1280,720,1,60},{101,1920,1080,1,60},
    {101,1920,1080,2,59.94f},{101,1920,1080,2,144},{202,1920,1080,1,144}};
static bool SDL_SetError(const char* format,...) {
    char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);lastError=text;return false;
}
static const char* SDL_GetError() { return lastError.c_str(); }
constexpr unsigned SDL_INIT_VIDEO=0x20;
static int videoReferences=0,videoInitCalls=0,videoQuitCalls=0;
static bool videoInitFails=false;
static unsigned SDL_WasInit(unsigned flags) { assert(flags==SDL_INIT_VIDEO);return videoReferences?SDL_INIT_VIDEO:0; }
static bool SDL_InitSubSystem(unsigned flags) {
    assert(flags==SDL_INIT_VIDEO);++videoInitCalls;
    if(videoInitFails)return SDL_SetError("injected video init failure");
    ++videoReferences;return true;
}
static void SDL_QuitSubSystem(unsigned flags) { assert(flags==SDL_INIT_VIDEO && videoReferences>0);++videoQuitCalls;--videoReferences; }
static bool Mutate(const char* name) {
    ++mutations;if(failure==name)return SDL_SetError("injected %s failure",name);return true;
}
static void* Allocate(size_t bytes) { ++allocations;return std::malloc(bytes); }
static void SDL_free(void* memory) { if(memory) { --allocations;std::free(memory); } }
static SDL_DisplayID* SDL_GetDisplays(int* count) {
    ++queries;*count=static_cast<int>(displayIds.size());
    if(failure=="displays")return nullptr;
    auto* result=static_cast<unsigned*>(Allocate(displayIds.size()*sizeof(unsigned)));
    std::copy(displayIds.begin(),displayIds.end(),result);return result;
}
static unsigned SDL_GetDisplayForWindow(SDL_Window* value) { ++queries;return value->display; }
static unsigned SDL_GetPrimaryDisplay() { ++queries;return 101; }
static SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* value) { ++queries;return value->flags; }
static bool raiseGrantsFocus=false;
static bool SDL_RaiseWindow(SDL_Window* value) {
    if(!Mutate("raise"))return false;
    if(raiseGrantsFocus)value->flags|=SDL_WINDOW_INPUT_FOCUS;
    return true;
}
struct idCmdArgs { int count=1;const char* argument="invalid";int Argc()const{return count;}const char* Argv(int)const{return argument;} };
static std::string diagnostic;
struct FocusConsole {
    void Printf(const char* format,...) {
        char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
        diagnostic+=text;
    }
} focusConsole;
static FocusConsole* common=&focusConsole;
static bool SDL_GetWindowPosition(SDL_Window* value,int* x,int* y) {
    ++queries;if(failure=="position-query")return false;*x=value->x;*y=value->y;return true;
}
static bool SDL_GetWindowSize(SDL_Window* value,int* width,int* height) {
    ++queries;if(failure=="size-query" || (failQueryAfterMutation && mutations))return false;
    *width=value->w;*height=value->h;return true;
}
static bool SDL_GetWindowSizeInPixels(SDL_Window* value,int* width,int* height) {
    ++queries;if(failure=="pixel-query")return false;
    *width=static_cast<int>(value->w*value->densityX+.5f);*height=static_cast<int>(value->h*value->densityY+.5f);return true;
}
static bool SDL_GetWindowSafeArea(SDL_Window* value,SDL_Rect* rect) {
    ++queries;if(failure=="safe-query")return false;
    *rect={value->safeLeft,value->safeTop,value->w-value->safeLeft-value->safeRight,value->h-value->safeTop-value->safeBottom};return true;
}
static float SDL_GetWindowDisplayScale(SDL_Window* value) { ++queries;return value->scale; }
static const SDL_DisplayMode* SDL_GetWindowFullscreenMode(SDL_Window* value) { ++queries;return value->haveFullscreenMode?&value->fullscreenMode:nullptr; }
static const SDL_DisplayMode* SDL_GetCurrentDisplayMode(unsigned display) {
    ++queries;return noCurrentMode || !currentModes.contains(display)?nullptr:&currentModes.at(display);
}
static const SDL_DisplayMode* SDL_GetDesktopDisplayMode(unsigned display) {
    ++queries;return noDesktopMode || !desktopModes.contains(display)?nullptr:&desktopModes.at(display);
}
static bool SDL_GetDisplayBounds(unsigned display,SDL_Rect* bounds) {
    ++queries;if(failure=="bounds" || !displayBounds.contains(display))return false;*bounds=displayBounds.at(display);return true;
}
static SDL_DisplayMode** SDL_GetFullscreenDisplayModes(unsigned display,int* count) {
    ++queries;if(noModes) { *count=0;return nullptr; }
    auto** result=static_cast<SDL_DisplayMode**>(Allocate(modes.size()*sizeof(SDL_DisplayMode*)));
    *count=0;for(auto& mode:modes)if(mode.displayID==display)result[(*count)++]=&mode;return result;
}
static int SDL_WINDOWPOS_CENTERED_DISPLAY(unsigned display) { return static_cast<int>(1000000+display); }
static bool SDL_SetWindowAspectRatio(SDL_Window*,float,float) { return Mutate("aspect"); }
static bool SDL_SetWindowFullscreenMode(SDL_Window* value,const SDL_DisplayMode* mode) {
    if(!Mutate("mode"))return false;value->haveFullscreenMode=mode!=nullptr;if(mode)value->fullscreenMode=*mode;return true;
}
static bool SDL_SetWindowFullscreen(SDL_Window* value,bool enable) {
    if(!Mutate(enable?"enter":"leave"))return false;
    if(silentFullscreen)return true;
    if(enable) {
        value->flags|=SDL_WINDOW_FULLSCREEN;
        auto mode=value->haveFullscreenMode?value->fullscreenMode:desktopModes.at(value->display);
        // Wayland emulates the mode: the output keeps its desktop mode, and the
        // window's buffer takes the mode's size at density 1.
        if(!emulatedModes)currentModes[value->display]=mode;value->w=mode.w;value->h=mode.h;
        value->densityX=value->densityY=emulatedModes && value->haveFullscreenMode?1:mode.pixel_density;
    } else { value->flags&=~SDL_WINDOW_FULLSCREEN;currentModes[value->display]=desktopModes.at(value->display); }
    return true;
}
static bool SDL_RestoreWindow(SDL_Window* value) {
    if(!Mutate("restore"))return false;value->flags&=~(SDL_WINDOW_MINIMIZED|SDL_WINDOW_MAXIMIZED);return true;
}
static bool SDL_MaximizeWindow(SDL_Window* value) {
    if(!Mutate("maximize"))return false;value->flags|=SDL_WINDOW_MAXIMIZED;return true;
}
static bool SDL_SetWindowBordered(SDL_Window* value,bool enabled) {
    if(noBorders)return SDL_SetError("That operation is not supported"); // SDL 3.4.18 drivers without border control
    if(!Mutate("border"))return false;
    if(!silentBorder) { if(enabled)value->flags&=~SDL_WINDOW_BORDERLESS;else value->flags|=SDL_WINDOW_BORDERLESS; }return true;
}
static bool SDL_SetWindowPosition(SDL_Window* value,int x,int y) {
    if(!Mutate("position"))return false;if(silentMove)return true;
    if(x>=1000000) { value->display=x-1000000;value->x=displayBounds.at(value->display).x;value->y=displayBounds.at(value->display).y; }
    else {
        value->x=x;value->y=y;
        for(const auto& [id,b]:displayBounds)if(x>=b.x && x<static_cast<int64_t>(b.x)+b.w && y>=b.y && y<static_cast<int64_t>(b.y)+b.h)value->display=id;
    }return true;
}
static bool SDL_SetWindowSize(SDL_Window* value,int width,int height) {
    if(!Mutate("size"))return false;if(!silentSize) { value->w=width;value->h=height; }return true;
}
static bool SDL_HideWindow(SDL_Window* value) { if(!Mutate("hide"))return false;value->flags|=SDL_WINDOW_HIDDEN;return true; }
static bool SDL_ShowWindow(SDL_Window* value) { if(!Mutate("show"))return false;value->flags&=~SDL_WINDOW_HIDDEN;return true; }
static bool SDL_SyncWindow(SDL_Window*) { return Mutate("sync"); }
struct idStr {
    static int snPrintf(char* output,int size,const char* format,...) {
        va_list args;va_start(args,format);int result=std::vsnprintf(output,size,format,args);va_end(args);return result;
    }
};
#undef INT_MAX // idlib Math.h supplies the engine constant under this name.
struct idMath {
    static constexpr int INT_MAX=2147483647;
    static int ClampInt(int low,int high,int value) { return std::clamp(value,low,high); }
};
static bool SDL3_UseAbsoluteWindowPlacement() { return !wayland; }
static bool SDL3_IsNativeWaylandVideoDriver() { return wayland; }
static bool Sys_WindowPlacementLeaseActive() { return false; }
static unsigned SDL3_ResolveViewportDisplay() { return viewportDisplay; }
static int SDL3_SaturateWindowCoordinate(int64_t value) {
    return static_cast<int>(std::clamp(value,static_cast<int64_t>(-2147483647)-1,static_cast<int64_t>(2147483647)));
}
struct EngineState {
    int vidWidth=7,vidHeight=8,uiViewportX=9,uiViewportY=10,uiViewportWidth=11,uiViewportHeight=12;
    bool isFullscreen=false;float displayScale=3,pixelDensityX=4,pixelDensityY=5;
    int safeAreaLeft=13,safeAreaTop=14,safeAreaRight=15,safeAreaBottom=16;
} engineWindowState;
struct Win32 { bool cdsFullscreen=false;void* hWnd=reinterpret_cast<void*>(0x1234); } win32;
struct Placement { bool valid=false;int x=0,y=0,width=640,height=480; } s_windowedPlacement;
static void SDL3_ClientOriginToFrameOrigin(int x,int y,int& frameX,int& frameY) { frameX=x-8;frameY=y-30; }
'''

MAIN = r'''
static void Reset() {
    assert(allocations==0);window={};s_sdlWindow=&window;engineWindowState={};win32={};s_windowedPlacement={};
    displayIds={101,202};displayBounds={{101,{0,0,1920,1080}},{202,{1920,0,1920,1080}}};
    desktopModes={{101,{101,1920,1080,1,60}},{202,{202,1920,1080,1,144}}};currentModes=desktopModes;
    modes={{101,1280,720,1,60},{101,1920,1080,1,60},{101,1920,1080,2,59.94f},{101,1920,1080,2,144},{202,1920,1080,1,144}};
    mutations=queries=0;failure.clear();lastError.clear();viewportDisplay=101;
    wayland=emulatedModes=noModes=noCurrentMode=noDesktopMode=silentSize=silentFullscreen=silentBorder=silentMove=failQueryAfterMutation=noBorders=false;
    s_screenParmTransitionActive=s_windowAspectSnapActive=false;s_windowAspectSnapRatio=0;
}
static renderWindowRequest_t Request() {
    renderWindowRequest_t request={};request.parms.width=960;request.parms.height=540;request.displayIndex=-1;return request;
}
static void Failed(const renderWindowRequest_t& request,bool preflight=false) {
    renderWindowState_t output;std::memset(&output,0x5a,sizeof(output));auto original=output;
    const auto engine=engineWindowState;char error[64];std::memset(error,'@',sizeof(error));
    assert(!SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,error,31));
    assert(!std::memcmp(&output,&original,sizeof(output)) && error[0]!='@' && std::memchr(error,'\0',31) && error[31]=='@');
    assert(!std::memcmp(&engine,&engineWindowState,sizeof(engine)) && !s_screenParmTransitionActive && allocations==0);
    if(preflight)assert(mutations==0);
}
static renderWindowState_t Applied(const renderWindowRequest_t& request) {
    renderWindowState_t output={};char error[256]="previous error";
    assert(SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,error,sizeof(error)));
    assert(!error[0] && !s_screenParmTransitionActive && allocations==0);
    assert(output.pixelWidth==engineWindowState.vidWidth && output.pixelHeight==engineWindowState.vidHeight);
    assert(output.uiViewportX==engineWindowState.uiViewportX && output.uiViewportWidth==engineWindowState.uiViewportWidth);
    return output;
}
static void Query() {
    Reset();window.x=-100;window.y=0;window.densityX=1.5;window.densityY=2;window.scale=1.25;
    window.flags|=SDL_WINDOW_MAXIMIZED|SDL_WINDOW_HIDDEN|(1ull<<43);currentModes[101]={101,1920,1080,2,59.94f};
    const auto engine=engineWindowState;renderWindowState_t result={};
    assert(SDL3_WindowServices_QueryWindowState(&result) && mutations==0 && allocations==0);
    assert(!std::memcmp(&engine,&engineWindowState,sizeof(engine)));
    assert(result.displayId==101 && result.displayIndex==0 && result.windowFlags==window.flags);
    assert(!result.fullscreen && !result.fullscreenDesktop && result.hidden && result.maximized && result.focused && !result.minimized);
    assert(result.logicalWidth==1280 && result.logicalHeight==720 && result.pixelWidth==1920 && result.pixelHeight==1440);
    assert(result.pixelDensityX==1.5f && result.pixelDensityY==2 && result.displayScale==1.25f);
    assert(result.currentModeValid && result.modeWidth==1920 && result.modePixelWidth==3840 && result.modePixelHeight==2160 && result.refreshRate==59.94f);
    SDL3_UpdateDisplayViewport(viewportDisplay,result.windowX,result.windowY,result.logicalWidth,result.logicalHeight,result.pixelWidth,result.pixelHeight);
    assert(result.uiViewportX==150 && result.uiViewportWidth==1770 && result.uiViewportHeight==1440);
    assert(result.uiViewportX==engineWindowState.uiViewportX && result.uiViewportY==engineWindowState.uiViewportY &&
           result.uiViewportWidth==engineWindowState.uiViewportWidth && result.uiViewportHeight==engineWindowState.uiViewportHeight);
    // A desktop window reports no inset; a phone's insets become framebuffer
    // pixels rounded outward (16.5 -> 17), and a failed query reports none.
    assert(!engineWindowState.safeAreaLeft && !engineWindowState.safeAreaTop && !engineWindowState.safeAreaRight && !engineWindowState.safeAreaBottom);
    window.safeLeft=33;window.safeTop=47;window.safeRight=11;window.safeBottom=21;
    SDL3_UpdateDisplayViewport(viewportDisplay,result.windowX,result.windowY,result.logicalWidth,result.logicalHeight,result.pixelWidth,result.pixelHeight);
    assert(engineWindowState.safeAreaLeft==50 && engineWindowState.safeAreaTop==94 && engineWindowState.safeAreaRight==17 && engineWindowState.safeAreaBottom==42);
    failure="safe-query";SDL3_UpdateDisplayViewport(viewportDisplay,result.windowX,result.windowY,result.logicalWidth,result.logicalHeight,result.pixelWidth,result.pixelHeight);
    assert(!engineWindowState.safeAreaLeft && !engineWindowState.safeAreaTop && !engineWindowState.safeAreaRight && !engineWindowState.safeAreaBottom);
    Reset();window.flags|=SDL_WINDOW_FULLSCREEN;
    assert(SDL3_WindowServices_QueryWindowState(&result) && result.fullscreen && result.fullscreenDesktop);
    window.haveFullscreenMode=true;assert(SDL3_WindowServices_QueryWindowState(&result) && !result.fullscreenDesktop);
    noCurrentMode=true;assert(SDL3_WindowServices_QueryWindowState(&result) && !result.currentModeValid);
    Reset();wayland=true;assert(SDL3_WindowServices_QueryWindowState(&result) && !result.positionValid && result.uiViewportWidth==1280);
    Reset();failure="position-query";assert(SDL3_WindowServices_QueryWindowState(&result) && !result.positionValid);
    for(const char* fail:{"displays","size-query","pixel-query"}) {
        Reset();failure=fail;std::memset(&result,0x5a,sizeof(result));auto old=result;
        assert(!SDL3_WindowServices_QueryWindowState(&result) && !std::memcmp(&result,&old,sizeof(old)) && !mutations && !allocations);
    }
    Reset();window.scale=std::numeric_limits<float>::quiet_NaN();assert(!SDL3_WindowServices_QueryWindowState(&result));
    Reset();window.w=0;assert(!SDL3_WindowServices_QueryWindowState(&result));
    Reset();window.display=999;assert(!SDL3_WindowServices_QueryWindowState(&result));
    Reset();s_sdlWindow=nullptr;assert(!SDL3_WindowServices_QueryWindowState(&result));
    Reset();assert(!SDL3_WindowServices_QueryWindowState(nullptr));
    std::puts("strict SDL query: actual flags/display/mode/density, viewport parity, no publication and atomic failure passed");
}
static void Preflight() {
    for(int which=0;which<15;++which) {
        Reset();auto request=Request();
        switch(which) {
            case 0:request.parms.width=319;break;case 1:request.parms.height=16385;break;
            case 2:request.displayIndex=2;break;case 3:request.displayId=999;break;
            case 4:request.parms.hiddenWindow=request.parms.fullScreen=true;break;
            case 5:wayland=true;request.parms.borderless=request.spanDisplays=true;break;
            case 6:request.parms.fullScreen=true;request.parms.width=1234;break;
            case 7:request.parms.fullScreen=true;request.parms.width=1280;request.parms.height=720;request.parms.displayHz=75;break;
            case 8:request.parms.fullScreen=request.fullscreenDesktop=true;noDesktopMode=true;break;
            case 9:request.parms.borderless=request.spanDisplays=true;displayBounds.erase(202);break;
            case 10:failure="bounds";break;case 11:failure="size-query";break;
            case 12:request.displayIndex=-2;break;
            case 13:request.restorePlacement=true;wayland=true;break;
            // Auto aims at the desktop rate, so an exclusive request needs it.
            case 14:request.parms.fullScreen=true;request.parms.width=1280;request.parms.height=720;noDesktopMode=true;break;
        }
        Failed(request,true);
    }
    Reset();auto request=Request();renderWindowState_t output={};char error[32];
    assert(!SDL3_WindowServices_ApplyScreenParmsStrict(nullptr,&output,error,sizeof(error)) && mutations==0);
    assert(!SDL3_WindowServices_ApplyScreenParmsStrict(&request,nullptr,error,sizeof(error)) && mutations==0);
    s_screenParmTransitionActive=true;
    assert(!SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,error,sizeof(error)) && s_screenParmTransitionActive && mutations==0);
    s_screenParmTransitionActive=false;
    std::puts("strict SDL preflight: invalid identities/modes/refresh/bounds/policy reject before mutation passed");
}
static void Success() {
    Reset();auto request=Request();s_windowedPlacement.valid=true;s_windowedPlacement.width=640;
    auto output=Applied(request);assert(output.logicalWidth==960 && output.logicalHeight==540 && s_windowedPlacement.width==960);
    Reset();displayIds={202,101};request=Request();request.displayId=101;request.displayIndex=0;
    output=Applied(request);assert(output.displayId==101 && output.displayIndex==1);
    request.displayIndex=999;output=Applied(request);assert(output.displayId==101 && output.displayIndex==1);
    Reset();request=Request();request.displayIndex=1;output=Applied(request);assert(output.displayId==202 && output.displayIndex==1);
    Reset();window.display=202;window.x=2000;request=Request();output=Applied(request);assert(output.displayId==202);
    Reset();request=Request();request.parms.fullScreen=true;request.parms.width=3840;request.parms.height=2160;request.parms.displayHz=60;
    output=Applied(request);assert(output.fullscreen && !output.fullscreenDesktop && output.modeWidth==1920 && output.modePixelWidth==3840 && output.refreshRate==59.94f);
    // Auto takes the rate nearest the desktop's (60 Hz here), as SDL's closest
    // mode does for refresh 0; a 144 Hz desktop takes 144.
    Reset();request.parms.displayHz=0;output=Applied(request);assert(output.refreshRate==59.94f);
    Reset();desktopModes[101].refresh_rate=144;currentModes=desktopModes;output=Applied(request);assert(output.refreshRate==144);
    // Of two equally near rates the lower wins, as in SDL.
    Reset();modes.push_back({101,1600,900,1,70});modes.push_back({101,1600,900,1,50});request=Request();
    request.parms.fullScreen=true;request.parms.width=1600;request.parms.height=900;output=Applied(request);assert(output.refreshRate==50);
    Reset();request=Request();request.parms.fullScreen=request.fullscreenDesktop=true;output=Applied(request);
    assert(output.fullscreen && output.fullscreenDesktop && output.pixelWidth==1920 && output.pixelHeight==1080);
    Reset();request=Request();request.parms.borderless=true;output=Applied(request);assert(output.borderless && !output.fullscreen && output.logicalWidth==1920 && output.windowX==0);
    Reset();request=Request();request.parms.fullScreen=request.fullscreenDesktop=request.spanDisplays=true;output=Applied(request);
    assert(!output.fullscreen && output.borderless && output.logicalWidth==3840 && output.uiViewportWidth==1920 && win32.cdsFullscreen);
    Reset();wayland=true;request=Request();request.displayId=202;output=Applied(request);assert(output.displayId==202 && !output.positionValid && output.uiViewportWidth==960);
    Reset();request=Request();request.parms.hiddenWindow=true;output=Applied(request);assert(output.hidden && !output.fullscreen);
    Reset();window.flags|=SDL_WINDOW_MAXIMIZED;request=Request();output=Applied(request);assert(!output.maximized && output.logicalWidth==960);
    Reset();request=Request();request.restorePlacement=true;request.windowX=250;request.windowY=180;
    s_windowedPlacement.valid=true;s_windowedPlacement.x=999;s_windowedPlacement.y=888;
    output=Applied(request);assert(output.windowX==250 && output.windowY==180 && output.positionValid);
    assert(s_windowedPlacement.x==242 && s_windowedPlacement.y==150);
    Reset();request.maximized=true;s_windowedPlacement.valid=true;s_windowedPlacement.width=777;
    output=Applied(request);assert(output.maximized && output.windowX==250 && output.windowY==180 && s_windowedPlacement.width==777);
    Reset();wayland=true;request=Request();request.maximized=true;output=Applied(request);assert(output.maximized && !output.positionValid);
    std::puts("strict SDL apply: requested size, stable ID, exact high-density exclusive, desktop/borderless/span/Wayland and hidden policies passed");
}
static void Failures() {
    for(const char* fail:{"aspect","leave","restore","mode","border","position","size","show","hide","sync","enter","maximize"}) {
        Reset();auto request=Request();failure=fail;
        if(failure=="aspect")s_windowAspectSnapActive=true;
        if(failure=="leave")window.flags|=SDL_WINDOW_FULLSCREEN;
        if(failure=="restore")window.flags|=SDL_WINDOW_MINIMIZED;
        if(failure=="border")window.flags|=SDL_WINDOW_BORDERLESS;
        if(failure=="hide")request.parms.hiddenWindow=true;
        if(failure=="enter") { request.parms.fullScreen=true;request.parms.width=1280;request.parms.height=720; }
        if(failure=="maximize")request.maximized=true;
        Failed(request);assert(mutations>0);
    }
    Reset();silentSize=true;Failed(Request());
    Reset();silentBorder=true;auto request=Request();request.parms.borderless=true;Failed(request);
    Reset();noBorders=true;Applied(Request());
    Reset();noBorders=true;request=Request();request.parms.borderless=true;Failed(request);
    Reset();silentFullscreen=true;request=Request();request.parms.fullScreen=request.fullscreenDesktop=true;Failed(request);
    Reset();silentMove=true;request=Request();request.displayId=202;Failed(request);
    Reset();silentMove=true;request=Request();request.restorePlacement=true;request.windowX=250;request.windowY=180;Failed(request);
    Reset();failQueryAfterMutation=true;Failed(Request());
    Reset();noCurrentMode=true;request=Request();request.parms.fullScreen=request.fullscreenDesktop=true;Failed(request);
    Reset();request=Request();failure="size";renderWindowState_t output={};
    assert(!SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,nullptr,0) && !s_screenParmTransitionActive);
    std::puts("strict SDL failures: mutator/sync refusal, silent normalization and post-apply readback preserve output and release transition guard passed");
}
static void VideoLifetime() {
    assert(!SDL3_WindowServices_RetainVideoSystem() && videoInitCalls==0 && videoQuitCalls==0 && videoReferences==0);
    videoReferences=1;videoInitFails=true;
    assert(!SDL3_WindowServices_RetainVideoSystem() && videoInitCalls==1 && videoQuitCalls==0 && videoReferences==1);
    videoInitFails=false;
    assert(SDL3_WindowServices_RetainVideoSystem() && videoReferences==2);
    SDL_QuitSubSystem(SDL_INIT_VIDEO); // Legacy renderer teardown removes its own reference.
    assert(videoReferences==1 && SDL_WasInit(SDL_INIT_VIDEO));
    assert(SDL3_WindowServices_RetainVideoSystem() && videoReferences==2);
    SDL3_WindowServices_ReleaseVideoSystem();assert(videoReferences==1);
    assert(SDL_InitSubSystem(SDL_INIT_VIDEO)); // A successful renderer recreation owns its normal reference.
    SDL3_WindowServices_ReleaseVideoSystem();assert(videoReferences==1);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);assert(videoReferences==0 && videoInitCalls==4 && videoQuitCalls==4);
    std::puts("strict SDL video pin: cold/failing retain rejected; balanced reference survives legacy teardown and releases after recreation passed");
}
// Native Wayland emulates an exclusive mode in the window: the window's own
// fullscreen mode is the observed one there, and only there.
static void WaylandEmulation() {
    Reset();wayland=emulatedModes=true;auto request=Request();request.parms.fullScreen=true;request.parms.width=1280;request.parms.height=720;request.parms.displayHz=60;
    renderWindowState_t output={};char error[256]={};
    assert(SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,error,sizeof(error)));
    assert(output.fullscreen && !output.fullscreenDesktop && output.currentModeValid && output.modePixelWidth==1280 && output.modePixelHeight==720);
    assert(output.pixelWidth==1280 && output.refreshRate==60 && currentModes.at(101).w==1920);
    renderWindowState_t queried={};assert(SDL3_WindowServices_QueryWindowState(&queried) && queried.modePixelWidth==1280);
    // Desktop fullscreen keeps the desktop mode.
    Reset();wayland=emulatedModes=true;request=Request();request.parms.fullScreen=request.fullscreenDesktop=true;
    output={};assert(SDL3_WindowServices_ApplyScreenParmsStrict(&request,&output,error,sizeof(error)) && output.fullscreenDesktop && output.modePixelWidth==1920);
    // Another driver that leaves the output's mode unchanged is refused.
    Reset();emulatedModes=true;request=Request();request.parms.fullScreen=true;request.parms.width=1280;request.parms.height=720;request.parms.displayHz=60;Failed(request);
    // Only an exclusive window's mode on its own display is adopted.
    Reset();wayland=true;window.flags|=SDL_WINDOW_FULLSCREEN;window.haveFullscreenMode=true;window.fullscreenMode={202,1280,720,1,144};
    renderWindowState_t r={};assert(SDL3_WindowServices_QueryWindowState(&r) && r.modePixelWidth==1920);
    window.fullscreenMode={101,1280,720,1,60};assert(SDL3_WindowServices_QueryWindowState(&r) && r.modePixelWidth==1280);
    window.flags&=~SDL_WINDOW_FULLSCREEN;assert(SDL3_WindowServices_QueryWindowState(&r) && r.modePixelWidth==1920);
    wayland=false;window.flags|=SDL_WINDOW_FULLSCREEN;assert(SDL3_WindowServices_QueryWindowState(&r) && r.modePixelWidth==1920);
    std::puts("strict SDL Wayland emulation: the exclusive window's mode is observed on native Wayland only, desktop and other drivers unchanged passed");
}
static void FocusRequest() {
    idCmdArgs command;
    Reset();s_sdlWindow=nullptr;SDL3_WindowFocusTest_f(command);assert(mutations==0);s_sdlWindow=&window;
    for(auto flag:{SDL_WINDOW_HIDDEN,SDL_WINDOW_MINIMIZED,SDL_WINDOW_FULLSCREEN}) {
        Reset();window.flags=flag;SDL3_WindowFocusTest_f(command);assert(mutations==0);
    }
    Reset();command.count=2;SDL3_WindowFocusTest_f(command);assert(mutations==0);command.count=1;
    Reset();diagnostic.clear();command.count=2;command.argument="report";
    SDL3_WindowFocusTest_f(command);assert(mutations==0 && diagnostic.find("nativeWindow=")!=std::string::npos);
    assert(diagnostic.find("requestAccepted=")==std::string::npos);command.count=1;
    Reset();window.flags=0;diagnostic.clear();raiseGrantsFocus=false;
    SDL3_WindowFocusTest_f(command);assert(mutations==1 && diagnostic.find("requestAccepted=1 focused=0")!=std::string::npos);
    Reset();window.flags=0;diagnostic.clear();failure="raise";
    SDL3_WindowFocusTest_f(command);assert(mutations==1 && diagnostic.find("requestAccepted=0 focused=0")!=std::string::npos);
    Reset();window.flags=0;diagnostic.clear();raiseGrantsFocus=true;
    SDL3_WindowFocusTest_f(command);assert(mutations==1 && diagnostic.find("requestAccepted=1 focused=1")!=std::string::npos);
    raiseGrantsFocus=false;
    std::puts("explicit window focus: unsafe states refused, one-shot request and independently observed focus passed");
}
int main() { Query();Preflight();Success();Failures();WaylandEmulation();VideoLifetime();FocusRequest();assert(allocations==0); }
'''


def main():
    source = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    strict = source.split('// Strict settings operations are separate from legacy startup negotiation.', 1)[1].split(
        'static const renderWindowServices_t s_sdl3WindowServices = {', 1)[0]
    query = function_body(source, 'static bool SDL3_WindowServices_QueryWindowState(')
    apply = function_body(source, 'static bool SDL3_WindowServices_ApplyScreenParmsStrict(')
    assert 'SDL3_RefreshWindowPlacement(' not in strict
    assert 'SDL_GetClosestFullscreenDisplayMode(' not in strict
    assert '.SetInteger(' not in strict and 'SDL3_ApplyScreenParms(' not in apply
    assert 'SDL3_SetUIViewport(' not in query and 'SDL3_SetVidSize(' not in query
    table = source.split('static const renderWindowServices_t s_sdl3WindowServices = {', 1)[1].split('};', 1)[0]
    assert table.rstrip().endswith('SDL3_WindowServices_QueryWindowState,\n\tSDL3_WindowServices_ApplyScreenParmsStrict,\n\tSDL3_WindowServices_RetainVideoSystem,\n\tSDL3_WindowServices_ReleaseVideoSystem,')
    dependencies = '\n'.join(function_body(source, signature) for signature in (
        'static void SDL3_WindowFocusTest_f(',
        'static int SDL3_ClampViewportPixel(', 'static void SDL3_RefreshSafeArea(', 'static void SDL3_SetUIViewport(',
        'static void SDL3_SetVidSize(', 'static void SDL3_SetFullscreenState(',
        'static void SDL3_UpdateDisplayViewport(', 'static void SDL3_RecordWindowedPlacement(',
        'static bool SDL3_SetWindowBorderedCompat(',
    ))
    assert '"windowFocusTest", SDL3_WindowFocusTest_f, CMD_FL_SYSTEM | CMD_FL_CHEAT,' in source
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sdl3-strict-window-', dir=ROOT / '.tmp') as directory:
        environment = dict(os.environ, TEMP=directory, TMP=directory)
        test = Path(directory) / 'window.cpp'
        binary = Path(directory) / 'window.exe'
        test.write_text(SUPPORT + dependencies + strict + MAIN, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(test), '-o', str(binary)], check=True, env=environment)
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    main()
