#!/usr/bin/env python3
"""Production SDL placement lease against counted window/CVar stand-ins.

Proves persistence suppression and exact-value conflict reconciliation without
window/input/device operations. Identical external assignments and compositor
private normal restore rectangles are not observable through these interfaces.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body
import sdl3_strict_window as window_test

ROOT = Path(__file__).resolve().parents[2]
CVARS = r'''
static int placementWrites=0,clearedFlags=0;
static std::string refusedPlacement;
struct PlacementCvar {
    const char* name;int value;bool modified=false;
    int GetInteger() const{return value;}
    void SetInteger(int next){++placementWrites;if(refusedPlacement!=name){value=next;modified=true;}}
    void ClearModified(){++clearedFlags;modified=false;}
};
struct Win32 { bool cdsFullscreen=false;PlacementCvar win_xpos{"x",42},win_ypos{"y",43}; } win32;
static PlacementCvar r_windowWidth{"width",1280},r_windowHeight{"height",720};
'''
EXTRA = r'''
static uint64_t s_windowPlacementLease=0;
static sysWindowPlacementSnapshot_t s_windowPlacementBaseline;
static bool SDL_GetWindowBordersSize(SDL_Window*,int* top,int* left,int* bottom,int* right) {
    if(failure=="borders")return false;*top=30;*left=8;*bottom=8;*right=8;return true;
}
static void SDL3_UpdateFullWindowViewport(int width,int height) { SDL3_SetUIViewport(0,0,width,height); }
'''
MAIN = r'''
int main() {
    char error[128];sysWindowPlacementSnapshot_t baseline,expected,final,current,sentinel;
    sentinel.x=999;current=sentinel;
    assert(!Sys_BeginWindowPlacementLease(0,&current,error,sizeof(error)) && current.x==999);
    assert(!Sys_ReadWindowPlacementLease(1,&current,error,sizeof(error)) && current.x==999);
    s_windowedPlacement={true,42,43,900,700};window.flags|=SDL_WINDOW_MAXIMIZED;window.w=1920;window.h=1080;
    assert(Sys_BeginWindowPlacementLease(41,&baseline,error,sizeof(error)) && Sys_WindowPlacementLeaseActive());
    assert(baseline.normalValid && baseline.normalWidth==900 && baseline.normalHeight==700 && baseline.width==1280);
    assert(!Sys_BeginWindowPlacementLease(42,&current,error,sizeof(error)) && current.x==999);
    assert(!Sys_FinishWindowPlacementLease(42,&baseline,&baseline,error,sizeof(error)) && placementWrites==0);
    SDL3_RecordWindowedPlacement(200,300,1800,900);SDL3_RefreshWindowPlacement();
    assert(placementWrites==0 && clearedFlags==0 && s_windowedPlacement.width==900 && engineWindowState.vidWidth==1920);
    renderWindowState_t actual{};actual.maximized=true;actual.logicalWidth=1920;actual.logicalHeight=1080;
    actual.positionValid=true;actual.windowX=8;actual.windowY=30;
    assert(Sys_BuildWindowPlacementCommit(41,&baseline,&actual,&final,error,sizeof(error)) && SDL3_SamePlacement(final,baseline));
    win32.win_xpos.SetInteger(777);const int externalWrites=placementWrites;
    assert(!Sys_ApplyWindowPlacementLease(41,&baseline,&baseline,error,sizeof(error)) && placementWrites==externalWrites && win32.win_xpos.value==777);
    assert(!Sys_FinishWindowPlacementLease(41,&baseline,&baseline,error,sizeof(error)) && placementWrites==externalWrites && Sys_WindowPlacementLeaseActive());
    current=sentinel;assert(!Sys_BuildWindowPlacementCommit(41,&baseline,&actual,&current,error,sizeof(error)) && current.x==999);
    // An explicit external correction permits recovery; the lease never did it.
    win32.win_xpos.SetInteger(baseline.x);
    r_windowWidth.SetInteger(960);r_windowHeight.SetInteger(540);expected=baseline;expected.width=960;expected.height=540;
    actual.maximized=false;
    actual.fullscreen=true;assert(Sys_BuildWindowPlacementCommit(41,&expected,&actual,&final,error,sizeof(error)) && SDL3_SamePlacement(final,expected));
    actual.fullscreen=false;actual.borderless=true;assert(Sys_BuildWindowPlacementCommit(41,&expected,&actual,&final,error,sizeof(error)) && SDL3_SamePlacement(final,expected));
    actual.borderless=false;actual.hidden=true;assert(Sys_BuildWindowPlacementCommit(41,&expected,&actual,&final,error,sizeof(error)) && SDL3_SamePlacement(final,expected));actual.hidden=false;
    actual.maximized=false;actual.logicalWidth=960;actual.logicalHeight=540;actual.windowX=108;actual.windowY=230;
    failure="borders";current=sentinel;
    assert(!Sys_BuildWindowPlacementCommit(41,&expected,&actual,&current,error,sizeof(error)) && current.x==999);failure.clear();
    assert(Sys_BuildWindowPlacementCommit(41,&expected,&actual,&final,error,sizeof(error)));
    assert(final.x==100 && final.y==200 && final.normalX==100 && final.normalY==200 && final.normalWidth==960 && final.normalHeight==540);
    assert(Sys_ApplyWindowPlacementLease(41,&expected,&final,error,sizeof(error)) && Sys_WindowPlacementLeaseActive());
    assert(win32.win_xpos.modified && r_windowWidth.modified && clearedFlags==0 && s_windowedPlacement.width==960);
    const int committedWrites=placementWrites;window.w=1111;window.h=777;
    SDL3_RefreshWindowPlacement();SDL3_RecordWindowedPlacement(1,2,3,4);
    assert(placementWrites==committedWrites && s_windowedPlacement.width==960);
    assert(Sys_FinishWindowPlacementLease(41,&final,&final,error,sizeof(error)) && !Sys_WindowPlacementLeaseActive() && placementWrites==committedWrites);
    current=sentinel;assert(!Sys_ReadWindowPlacementLease(41,&current,error,sizeof(error)) && current.x==999);
    assert(Sys_BeginWindowPlacementLease(42,&baseline,error,sizeof(error)));
    r_windowWidth.SetInteger(800);r_windowHeight.SetInteger(600);expected=baseline;expected.width=800;expected.height=600;
    assert(Sys_ApplyWindowPlacementLease(42,&expected,&baseline,error,sizeof(error)) && Sys_WindowPlacementLeaseActive());
    assert(r_windowWidth.value==960 && r_windowHeight.value==540 && s_windowedPlacement.width==960);
    assert(Sys_FinishWindowPlacementLease(42,&baseline,&baseline,error,sizeof(error)));
    assert(Sys_BeginWindowPlacementLease(43,&baseline,error,sizeof(error)));final=baseline;final.x=1000;final.y=2000;
    refusedPlacement="y";assert(!Sys_ApplyWindowPlacementLease(43,&baseline,&final,error,sizeof(error)) && Sys_WindowPlacementLeaseActive());
    assert(win32.win_xpos.value==1000 && win32.win_ypos.value==baseline.y && s_windowedPlacement.x==baseline.normalX);
    assert(Sys_ReadWindowPlacementLease(43,&current,error,sizeof(error)));refusedPlacement.clear();
    assert(Sys_ApplyWindowPlacementLease(43,&current,&baseline,error,sizeof(error)));
    final=baseline;final.normalValid=true;final.normalHeight=0;const int beforeInvalid=placementWrites;
    assert(!Sys_ApplyWindowPlacementLease(43,&baseline,&final,error,sizeof(error)) && placementWrites==beforeInvalid);
    assert(Sys_FinishWindowPlacementLease(43,&baseline,&baseline,error,sizeof(error)) && clearedFlags==0);
    std::puts("SDL settings placement: whole-attempt CVar/cache suppression, durable-commit hold, conflict refusal, exact normal-cache recovery and partial refusal passed");
}
'''


def main():
    source = (ROOT / 'src/sys/sdl3/sdl3_backend.cpp').read_text(encoding='utf-8')
    support = window_test.SUPPORT.replace('struct Win32 { bool cdsFullscreen=false; } win32;', CVARS)
    support = support.replace('static bool Sys_WindowPlacementLeaseActive() { return false; }', '')
    signatures = ('static bool SDL3_PlacementError(', 'static sysWindowPlacementSnapshot_t SDL3_ReadPlacementSettings(',
                  'static bool SDL3_SamePlacement(', 'bool Sys_WindowPlacementLeaseActive(',
                  'bool Sys_BeginWindowPlacementLease(', 'bool Sys_ReadWindowPlacementLease(',
                  'bool Sys_BuildWindowPlacementCommit(', 'bool Sys_ApplyWindowPlacementLease(', 'bool Sys_FinishWindowPlacementLease(')
    lease = '\n'.join(function_body(source, signature) for signature in signatures)
    assert '.ClearModified(' not in lease
    dependencies = '\n'.join(function_body(source, signature) for signature in (
        'static int SDL3_ClampViewportPixel(', 'static void SDL3_SetUIViewport(', 'static void SDL3_SetVidSize(',
        'static void SDL3_UpdateDisplayViewport(', 'static void SDL3_RecordWindowedPlacement('))
    code = ('#define USE_SDL3\n#include "src/sys/WindowSettings.h"\n' + support + dependencies + EXTRA + lease +
            function_body(source, 'static void SDL3_RefreshWindowPlacement(') + MAIN)
    compiler = next((p for name in ('clang++', 'g++', 'c++') if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sdl3-settings-placement-', dir=ROOT / '.tmp') as directory:
        temp = Path(directory)
        environment = dict(os.environ, TEMP=str(temp), TMP=str(temp))
        cpp = temp / 'placement.cpp'; binary = temp / 'placement.exe'
        cpp.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True, env=environment)
        subprocess.run([str(binary)], check=True, env=environment)
        cpp.write_text('#include "src/sys/WindowSettings.h"\n#include <cassert>\nint main(){sysWindowPlacementSnapshot_t s;s.x=99;assert(!Sys_BeginWindowPlacementLease(1,&s,nullptr,0)&&s.x==99&&!Sys_WindowPlacementLeaseActive());}\n', encoding='utf-8')
        for flags in ([], ['-DUSE_SDL3', '-DID_DEDICATED']):
            subprocess.run([compiler, '-std=c++20', *flags, '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True, env=environment)
            subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    main()
