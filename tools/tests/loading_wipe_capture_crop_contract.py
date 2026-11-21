#!/usr/bin/env python3
"""Run the production wipe capture against print-driven loading redraws.

ExecuteMapChange ends with StartWipe. It pushes a render crop, draws, copies the
crop into _scratch and pops it, all outside UpdateScreen, while every print still
offers PacifierUpdate a loading-screen redraw. That redraw's BeginFrame resets
the crop stack, so one print inside the capture (fs_debug's "Can't find
_scratch", a developer DPrintf, a non-precached decl warning) used to drop the
finished map with "UnCrop: currentRenderCrop < 1".

The harness compiles the production StartWipe, PacifierUpdate, UpdateScreen and
sessionCaptureDrawGuard_t against a renderer that keeps a real crop stack, and
proves its teeth by rebuilding StartWipe without the guard, which must
reproduce the drop.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

from async_drop_client_contract import function_body, read, require

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
template<class T> T Max(T a,T b){return std::max(a,b);}
template<class T> T Min(T a,T b){return std::min(a,b);}
struct idMath {
    static float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
    static float Ceil(float v){return std::ceil(v);}
    static int Ftoi(float v){return static_cast<int>(v);}
};
static void Check(bool ok,const char *what){if(!ok){std::printf("FAIL: %s\n",what);std::exit(2);}}
int clockMsec=100000;
int Sys_Milliseconds(){return clockMsec;}
bool retainedEnabled=false,reducedMotion=false,initializationActive=false;
bool openQ4_UpdateInitialization(){return initializationActive;}
bool RetainedUI_CanUpdateInitialization(){return true;}
struct idStr {
    static int Icmp(const char *a,const char *b){
        while(*a && std::tolower(static_cast<unsigned char>(*a))==std::tolower(static_cast<unsigned char>(*b))){++a;++b;}
        return std::tolower(static_cast<unsigned char>(*a))-std::tolower(static_cast<unsigned char>(*b));
    }
};
struct {
    bool GetCVarBool(const char *name)const{
        return idStr::Icmp(name,"ui_retained")==0 ? retainedEnabled : reducedMotion;
    }
} cvarSystemValue,*cvarSystem=&cvarSystemValue;
void Sys_GenerateEvents(){}
@PACER@
double LoadingPresentation_Now(){return clockMsec;}
void LoadingPresentation_Presented(double,double){}
int com_frameRealTime=0;
int Session_FindPresentationCap(){return 60;}
void openQ4_BeginPacedPresentationFrame(){}
void Session_BeginBlockingLoadPresentationFrame(){}
int com_ticNumber=500;
int com_editors=0;
bool Sys_IsWindowVisible(){return true;}
void Sys_GrabMouseCursor(bool){}
void RetainedUI_FrameSubmitted(){}
struct UI_SettingsRenderFrame {void Submitting(){} void Presented(){}};
struct {void BeginFrame(){} void DrawMenuPointer(){} void EndFrame(){}} vrSystemValue,*vrSystem=&vrSystemValue;
struct {bool GetBool()const{return false;}} com_speeds;
struct {float GetFloat()const{return 1.0f;}} com_wipeSeconds;
struct {void Close(){}} consoleValue,*console=&consoleValue;
struct idMaterial {} fadeMaterial;
struct {const idMaterial *FindMaterial(const char*,bool){return &fadeMaterial;}} declManagerValue,*declManager=&declManagerValue;
struct {int GetReadCount(){return 40;}} fileSystemValue,*fileSystem=&fileSystemValue;
struct {
    int GetPresentationTime(){return clockMsec;}
    int GetUserCmdTicsForMsecCeil(int msec){return (msec+15)/16;}
} commonValue,*common=&commonValue;
struct GUI {
    float shown=0.0f;
    GUI &State(){return *this;}
    float GetFloat(const char*){return shown;}
    void SetStateFloat(const char*,float value){shown=value;}
    void StateChanged(int){}
};
struct Network {void PacifierUpdate(){}};
struct idAsyncNetwork {static inline Network client,server;};
// The renderer keeps the engine's crop stack: BeginFrame resets it, and popping
// below the frame's own crop is idRenderSystemLocal::UnCrop's common->Error.
struct CropDrop {};
struct Renderer {
    int crops=0,frames=0,capturedAtCrop=-1;
    int GetScreenWidth()const{return 1280;}
    int GetScreenHeight()const{return 720;}
    void SetLoadingScreenSwapIntervalBypass(bool){}
    void BeginFrame(int,int){crops=0;++frames;}
    void EndFrame(int*,int*){}
    void CropRenderSize(int,int,bool=false,bool=false){++crops;}
    void UnCrop(){if(crops<1)throw CropDrop{};--crops;}
    void CaptureRenderToImage(const char*);
} rendererValue,*renderSystem=&rendererValue;
struct idSessionLocal {
    bool insideExecuteMapChange=true,insidePacifierUpdate=false,insideUpdateScreen=false,startupLoadingHandoff=false;
    int lastPacifierTime=0,bytesNeededForMapLoad=100;
    openq4::LoadingFramePacer loadingFramePacer;
    bool loadingAssetQueueActive=false;
    int loadingAssetQueueTotal=0,loadingAssetQueueLoaded=0;
    float loadingAssetQueueStartPct=0.0f;
    GUI gui,*guiLoading=&gui;
    int time_frontend=0,time_backend=0;
    const idMaterial *wipeMaterial=nullptr;
    int wipeStartTic=0,wipeStopTic=0;
    bool wipeHold=false,drawThrows=false;
    bool wipeToBlack=false;
    int wipeFadeStarted=0,wipeFadeDuration=0;
    int drawsInsideCrop=0;
    void PublishRetainedLoadingCount(){}
    void PublishRetainedLoadingTip(bool){}
    // A client with a screen: a dedicated server skips the capture and the redraws.
    bool IsDedicatedServer() const {return false;}
    void Draw();
    void StartWipe(const char *_wipeMaterial,bool hold=false);
    void PacifierUpdate();
    void UpdateScreen(bool outOfSequence=false);
} session;
// The tail of idCommonLocal::VPrintf: refresh-on-print redraws at once, and
// every print offers the loading screen a pacifier update.
bool refreshOnPrint=false;
int prints=0;
void Print(){
    ++prints;
    if(refreshOnPrint)session.UpdateScreen();
    session.PacifierUpdate();
}
// fs_debug's "Can't find _scratch" from the image lookup inside the capture.
void Renderer::CaptureRenderToImage(const char*){Print();capturedAtCrop=crops;}
// A draw outlasts the pacing interval and prints, like a developer DPrintf or
// a "Loading non pre-cached" warning from the loading GUI.
void idSessionLocal::Draw(){
    if(renderSystem->crops>0)++drawsInsideCrop;
    clockMsec+=40;
    if(drawThrows)throw std::runtime_error("drop thrown by the drawing code");
    Print();
}
@GUARD@
@BODIES@
static void Reset(){
    session=idSessionLocal();session.guiLoading=&session.gui;
    rendererValue=Renderer();refreshOnPrint=false;prints=0;clockMsec=100000;
    retainedEnabled=false;reducedMotion=false;initializationActive=false;
}
static bool Wipe(const char *material,bool hold=false){
    try{session.StartWipe(material,hold);}
    catch(const CropDrop&){std::puts("ERROR: idRenderSystemLocal::UnCrop: currentRenderCrop < 1");return false;}
    return true;
}
int main() {
    // The load-end wipe. The SP continue gate presents through UpdateScreen and
    // never stamps the pacifier, so the first print in the capture is due a redraw.
    Reset();session.lastPacifierTime=clockMsec-1000;
    if(!Wipe("gfx/wipes/fade_blend"))return 1;
    Check(prints==2,"the capture's draw and image lookup both printed");
    Check(rendererValue.frames==0,"no loading redraw began a frame inside the capture");
    Check(rendererValue.capturedAtCrop==1 && rendererValue.crops==0,"the capture copied and popped its own crop");
    Check(session.drawsInsideCrop==1,"the wipe source was drawn inside the crop");
    Check(!session.insideUpdateScreen,"the capture released the screen-update flag");
    Check(session.wipeMaterial==&fadeMaterial && session.wipeStopTic>session.wipeStartTic,"the wipe started");
    // Deferred, not lost: the next print redraws the loading screen.
    clockMsec+=1000;Print();
    Check(rendererValue.frames==1 && !session.insideUpdateScreen,"the next print redraws the loading screen");

    // Refresh-on-print (dmap-style tools) redraws on every print, inside or outside a load.
    Reset();session.insideExecuteMapChange=false;refreshOnPrint=true;
    if(!Wipe("gfx/wipes/fade",true))return 1;
    Check(rendererValue.frames==0 && rendererValue.capturedAtCrop==1 && rendererValue.crops==0,"refresh-on-print waits for the capture");
    Print();
    Check(rendererValue.frames==1,"refresh-on-print redraws once the capture is done");

    // A drop thrown from the drawing code must not leave every later screen update refused.
    Reset();session.drawThrows=true;
    bool threw=false;
    try{session.StartWipe("gfx/wipes/fade_blend");}catch(const std::runtime_error&){threw=true;}
    Check(threw && !session.insideUpdateScreen,"a throwing capture releases the flag");
    session.drawThrows=false;session.UpdateScreen();
    Check(rendererValue.frames==1 && rendererValue.crops==0,"the next frame runs and drops the abandoned crop");

    // A capture inside a screen update leaves the flag to that update.
    Reset();session.insideUpdateScreen=true;
    if(!Wipe("gfx/wipes/fade_blend"))return 1;
    Check(session.insideUpdateScreen && rendererValue.frames==0 && rendererValue.crops==0,"a nested capture keeps the outer update's flag");

    // The optional retained fade avoids a capture entirely, but load-end
    // fade_blend still needs the guarded crop with either interface.
    for(bool reduced : {false,true}){
        Reset();retainedEnabled=true;reducedMotion=reduced;
        if(!Wipe("gfx/wipes/fade",true))return 1;
        Check(session.wipeToBlack && session.wipeHold,"retained transition holds its fade");
        Check(session.wipeFadeStarted==clockMsec && session.wipeFadeDuration==(reduced?80:240),"retained fade respects reduced motion");
        Check(prints==0 && rendererValue.capturedAtCrop==-1 && rendererValue.crops==0,"retained fade never captures a crop");
        if(!Wipe("gfx/wipes/fade_blend"))return 1;
        Check(!session.wipeToBlack && rendererValue.capturedAtCrop==1 && rendererValue.crops==0,"retained load-end wipe retains the guarded capture");
    }

    puts("load-end wipe capture keeps its crop through loading redraws: PASS");
    return 0;
}
'''

GUARD_DECLARATION = re.compile(r"\n[ \t]*sessionCaptureDrawGuard_t\s+\w+\(\s*\*this\s*\);")
SESSION_CROP_GUARD = re.compile(r"sessionRenderCropGuard_t\s+\w+\s*\(")


def check_sources(session: str) -> None:
    """Pin the facts the harness models and the save preview it cannot run."""
    common = read("src/framework/Common.cpp")
    vprintf = function_body(common, "void idCommonLocal::VPrintf(")
    for token in ("if ( com_refreshOnPrint ) {", "session->UpdateScreen();", "session->PacifierUpdate();"):
        require(vprintf, token, "VPrintf's print-driven redraw offers")

    renderer = read("src/renderer/RenderSystem.cpp")
    require(function_body(renderer, "void idRenderSystemLocal::BeginFrame("), "currentRenderCrop = 0;",
            "BeginFrame resetting the crop stack")
    uncrop = function_body(renderer, "void idRenderSystemLocal::UnCrop(")
    require(uncrop, "if ( currentRenderCrop < 1 ) {", "UnCrop's underflow check")
    require(uncrop, 'common->Error( "idRenderSystemLocal::UnCrop: currentRenderCrop < 1" );', "UnCrop's map drop")

    require(function_body(session, "void idSessionLocal::ExecuteMapChange("), 'StartWipe( "gfx/wipes/fade_blend" );',
            "the load-end wipe capture")

    # SaveGame draws the preview into its own crop outside UpdateScreen too. The
    # guard must be in the crop's block and declared first, so it outlives the pop.
    save = function_body(session, "bool idSessionLocal::SaveGame(")
    guard = GUARD_DECLARATION.search(save)
    crop = save.find("sessionRenderCropGuard_t previewCrop(")
    if guard is None or crop < 0:
        raise AssertionError("SaveGame's preview must hold sessionCaptureDrawGuard_t across sessionRenderCropGuard_t")
    between = save[guard.end():crop]
    if not guard.start() < crop or "{" in between or "}" in between:
        raise AssertionError("SaveGame's preview guard must precede its crop guard in the same block")
    if not crop < save.index("game->Draw( 0 );") < save.index("CaptureRenderToFile( tempPreviewFile"):
        raise AssertionError("SaveGame's preview must draw and capture inside its crop")

    # The load's unseen first-frame warm-up draws into a crop the same way.
    warm = function_body(session, "int idSessionLocal::WarmFirstGameFrame(")
    guard = GUARD_DECLARATION.search(warm)
    crop = warm.find("sessionRenderCropGuard_t warmCrop(")
    if guard is None or crop < 0:
        raise AssertionError("WarmFirstGameFrame must hold sessionCaptureDrawGuard_t across sessionRenderCropGuard_t")
    between = warm[guard.end():crop]
    if not guard.start() < crop or "{" in between or "}" in between:
        raise AssertionError("WarmFirstGameFrame's guard must precede its crop guard in the same block")
    if not crop < warm.index("game->Draw( 0 );") < warm.index('CaptureRenderToImage( "_scratch" );'):
        raise AssertionError("WarmFirstGameFrame must draw and capture inside its crop")

    # Every session-side crop is one of the guarded captures. A new one must
    # hold sessionCaptureDrawGuard_t and be added here.
    if session.count("CropRenderSize(") != 2 or len(SESSION_CROP_GUARD.findall(session)) != 2:
        raise AssertionError("Session.cpp gained a render crop: hold sessionCaptureDrawGuard_t across it and pin it here")


def build_and_run(compiler: str, source: str, temp: Path, name: str) -> subprocess.CompletedProcess:
    cpp = temp / f"{name}.cpp"
    exe = temp / f"{name}.exe"
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    return subprocess.run([str(exe)], capture_output=True, text=True)


def main() -> None:
    session = read("src/framework/Session.cpp")
    check_sources(session)

    guard = function_body(session, "class sessionCaptureDrawGuard_t") + ";\n"
    wipe = function_body(session, "void idSessionLocal::StartWipe(")
    if len(GUARD_DECLARATION.findall(wipe)) != 1:
        raise AssertionError("StartWipe must hold one sessionCaptureDrawGuard_t across its capture")
    bodies = "\n".join(function_body(session, signature) for signature in (
        "void idSessionLocal::PacifierUpdate(",
        "void idSessionLocal::UpdateScreen(",
    ))
    pacer = (ROOT / "src/framework/LoadingFramePacer.h").read_text(encoding="utf-8").replace("#pragma once", "")
    harness = HARNESS.replace("@GUARD@", guard).replace("@PACER@", pacer)

    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wipe-capture-crop-", dir=ROOT / ".tmp") as temp:
        production = build_and_run(compiler, harness.replace("@BODIES@", wipe + "\n" + bodies), Path(temp), "production")
        print(production.stdout, end="")
        if production.returncode != 0:
            raise AssertionError(f"production StartWipe failed (exit {production.returncode}):\n{production.stderr}")

        # Without the guard the same harness must reproduce the shipped drop.
        unguarded = GUARD_DECLARATION.sub("\n", wipe)
        mutant = build_and_run(compiler, harness.replace("@BODIES@", unguarded + "\n" + bodies), Path(temp), "unguarded")
        if mutant.returncode != 1 or "UnCrop: currentRenderCrop < 1" not in mutant.stdout:
            raise AssertionError("an unguarded StartWipe no longer reproduces the UnCrop drop; "
                                 f"the harness lost its teeth (exit {mutant.returncode}):\n{mutant.stdout}{mutant.stderr}")
        print("unguarded StartWipe reproduces the UnCrop drop: PASS")


if __name__ == "__main__":
    main()
