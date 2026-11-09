#!/usr/bin/env python3
"""Execute the production render-thread handoff with a controlled fake backend.

No renderer, window or device input is opened. Force both completion orders,
including errors that finish before Sync, and reentrant frame retirement.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
enum { CVAR_RENDERER=1, CVAR_BOOL=2, CVAR_INTEGER=8, CMD_FL_RENDERER=4 };
struct idCVar {
    int value=0;
    idCVar(const char*,const char*,int,const char*,int=0,int=0){}
    bool GetBool() const { return value!=0; }
    int GetInteger() const { return value; }
};
idCVar r_skipBackEnd("","",0,"");
idCVar r_lockSurfaces("","",0,"");
struct frameData_t { int slot=0; int payload=42; };
static frameData_t arena[2];
static frameData_t* frameData=&arena[0];
static thread_local frameData_t* selectedFrame=nullptr;
static void R_SetBackEndFrameData(frameData_t* frame){selectedFrame=frame;}
static void R_CountFrameData(){}
bool R_RenderThread_InFlight();
static void R_FreeDeferredFrameSurfaces(){assert(!R_RenderThread_InFlight());}
static void R_TemporalPresentation_SubmitFrame(){}
static void R_RendererMetrics_MarkBackEndFinished(){}
struct glconfig_t { int width=1280; };
static thread_local glconfig_t glConfig;
struct viewDef_t { int identity; };
static viewDef_t primary{12},replacement{34};
struct {int postProcessTexelSize=0,postProcessSourceColorSpace=0,postProcessSMAAQuality=0;} backEnd;

struct idStr : std::string {
    using std::string::string;
    void Clear(){clear();}
    static int Cmp(const char* a,const char* b) { return std::strcmp(a,b); }
    static int Icmp(const char* a,const char* b) { return Cmp(a,b); }
    static void Copynz(char* d,const char* s,int n) { int i=0; for(;i<n-1 && s[i];++i)d[i]=s[i]; d[i]=0; }
};
template<class T> struct idList : std::vector<T> {
    int Num()const { return int(this->size()); }
    void Append(const T& t){this->push_back(t);}
    void Clear(){this->clear();}
};
struct idStrList : idList<idStr> {
    int FindIndex(const char* s)const { for(int i=0;i<Num();++i)if((*this)[i]==s)return i; return -1; }
};
struct idCmdArgs { int Argc()const{return 0;} const char* Argv(int)const{return "";} };
struct idException { char error[1024]; explicit idException(const char* s){idStr::Copynz(error,s,sizeof(error));} };
static std::thread::id mainId=std::this_thread::get_id();
static int drops=0,fatals=0;
bool R_RenderThread_InFlight();
void R_RenderThread_Error(const char*,bool fatal=false);
struct Common {
    template<class... T> void Printf(const char*,T...){}
    void Error(const char*,const char* message){
        assert(std::this_thread::get_id()==mainId && !R_RenderThread_InFlight());
        ++drops;throw idException(message);
    }
    void FatalError(const char*,const char* message){
        assert(std::this_thread::get_id()==mainId && !R_RenderThread_InFlight());
        ++fatals;throw idException(message);
    }
} commonStorage, *common=&commonStorage;
struct Cmd { void AddCommand(const char*,void(*)(const idCmdArgs&),int,const char*){} } cmdStorage,*cmdSystem=&cmdStorage;
struct emptyCommand_t { int serial; int fail; };
static bool qualifies=true;
static bool R_RenderThread_FrameQualifies(){return qualifies;}
static unsigned long long R_RendererMetrics_CpuClock(){
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static int Sys_Milliseconds(){return int(R_RendererMetrics_CpuClock()/1000);}
static int recorded=0,completed=0,prepared=0,recovered=0;
static void R_RendererMetrics_RecordSubmitMsec(int){++recorded;}
void VK_Backend_PrepareThreadedFrame(){++prepared;}
void VK_Backend_CompleteThreadedFrame(){++recovered;}
bool R_RenderThread_InFlight();
bool R_RenderThread_OnRenderThread();
void R_RenderThread_Sync(const char*);
struct RenderSystem {
    viewDef_t* primaryView=&primary;
    bool takingScreenshot=false;
    int postProcessTexelSize=1,postProcessSourceColorSpace=2,postProcessSMAAQuality=3;
    void CompleteBackEndFrame(){
        assert(std::this_thread::get_id()==mainId);
        assert(!R_RenderThread_InFlight());
        ++completed;
        R_RenderThread_Sync("recursive frame memory retirement");
    }
} tr;
static std::mutex backendMutex;
static std::condition_variable backendWake;
static bool backendEntered=false,backendReleased=false;
static std::atomic<int> executed{0};
static bool verifyOverlap=false;
viewDef_t* R_SubmittedPrimaryView();
bool R_SubmittedTakingScreenshot();
void RB_ExecuteBackEndCommands(const emptyCommand_t* cmd){
    assert(R_RenderThread_OnRenderThread());
    R_RenderThread_Sync("worker never waits on itself");
    {
        std::unique_lock<std::mutex> lock(backendMutex);
        backendEntered=true;
        backendWake.notify_all();
        backendWake.wait(lock,[]{return backendReleased;});
    }
    assert(cmd->serial==++executed);
    if(verifyOverlap){
        assert(selectedFrame==&arena[0] && selectedFrame->payload==42);
        assert(glConfig.width==1280);
        assert(R_SubmittedPrimaryView()==&primary && !R_SubmittedTakingScreenshot());
    }
    if(cmd->fail==1)throw idException("backend failed before sync");
    if(cmd->fail>=2)R_RenderThread_Error("recording invariant",cmd->fail==3);
}
'''


WORLD_SUPPORT = r'''
struct idRenderWorldLocal;
struct idRenderEntityLocal { idRenderWorldLocal* world; };
using idRenderLightLocal=idRenderEntityLocal;
using rvRenderEffectLocal=idRenderEntityLocal;
static int defsFreed=0,queuedFrees=0;
static const int MAX_DEBUG_TEXT=4;
struct debugText_t { idStr text; int lifeTime=0; };
static debugText_t rb_debugText[MAX_DEBUG_TEXT];
static int rb_numDebugText=0,rb_debugTextTime=0;
void R_ClearDrawSurfAreaMemo(){assert(!R_RenderThread_InFlight());}
void R_StaticFree(void*){assert(!R_RenderThread_InFlight());}
struct idRenderWorldLocal {
    bool generateAllInteractionsCalled=true;
    void* interactionTable=nullptr;
    idList<idRenderEntityLocal*> entityDefs,lightDefs,effectsDef;
    idList<int> throughWorldOutlineEntities;
    void FreeDefs();
    void Free(idList<idRenderEntityLocal*>& defs,int i){
        if(R_RenderThread_InFlight()){++queuedFrees;return;}
        delete defs[i];defs[i]=nullptr;++defsFreed;
    }
    void FreeEntityDef(int i){Free(entityDefs,i);}
    void FreeLightDef(int i){Free(lightDefs,i);}
    void FreeEffectDef(int i){Free(effectsDef,i);}
    void FreeDeferredLightDefs(){assert(!R_RenderThread_InFlight());}
    void FreeDemoGuiSurfaces(int){assert(!R_RenderThread_InFlight());}
    void ResetDemoJointHistory(int){assert(!R_RenderThread_InFlight());}
};
'''

CASES = r'''
static void WorkerFinished(){
    std::unique_lock<std::mutex> lock(rt_mutex);
    assert(rt_done.wait_for(lock,std::chrono::seconds(5),[]{return !rt_busy;}));
}
static void ResetBackend(bool released){
    std::lock_guard<std::mutex> lock(backendMutex);
    backendEntered=false;backendReleased=released;
}
int main(){
    R_RenderThread_Init();
    emptyCommand_t cmd{1,false};
    assert(!R_RenderThread_Begin(&cmd)); // opt-in stays off
    r_vkRenderThread.value=true;
    qualifies=false;assert(!R_RenderThread_Begin(&cmd));qualifies=true;
    r_skipBackEnd.value=true;assert(!R_RenderThread_Begin(&cmd));r_skipBackEnd.value=false;
    for(int i=1;i<=1000;++i){
        ResetBackend(true);cmd={i,false};
        assert(R_RenderThread_Begin(&cmd));
        WorkerFinished(); // deterministically reproduce the fast-frame race
        assert(R_RenderThread_InFlight()); // updates must still queue in call order
        assert(completed==i-1);
        R_RenderThread_Sync("frame");
        assert(completed==i && recorded==i && recovered==i);
        assert(!R_RenderThread_InFlight());
        R_RenderThread_Sync("duplicate");assert(completed==i);
    }
    ResetBackend(false);cmd={1001,false};assert(R_RenderThread_Begin(&cmd));
    const int waitsBefore=rt_syncWaits;
    std::thread release([waitsBefore]{
        std::unique_lock<std::mutex> lock(backendMutex);
        assert(backendWake.wait_for(lock,std::chrono::seconds(5),[]{return backendEntered;}));
        // Release only after the actual Sync has observed a busy backend.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;){
            { std::lock_guard<std::mutex> syncLock(rt_mutex); if(rt_syncWaits>waitsBefore)break; }
            assert(std::chrono::steady_clock::now()<deadline);
            std::this_thread::yield();
        }
        backendReleased=true;backendWake.notify_all();
    });
    R_RenderThread_Sync("frame");release.join();assert(completed==1001);
    assert(rt_syncWaits==waitsBefore+1);
    ResetBackend(true);cmd={1002,true};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    bool caught=false;
    try { R_RenderThread_Sync("frame"); } catch(const idException& e) {
        caught=std::strcmp(e.error,"backend failed before sync")==0;
    }
    assert(caught && completed==1002 && !R_RenderThread_InFlight());
    // A failed frame must retire once, clear the failure, then allow another.
    R_RenderThread_Sync("duplicate failure");
    cmd={1003,false};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    r_vkRenderThread.value=false;
    R_RenderThread_Shutdown();assert(completed==1003 && !rt_thread.joinable());
    R_RenderThread_Shutdown();assert(completed==1003);
    r_vkRenderThread.value=true;cmd={1004,false};assert(R_RenderThread_Begin(&cmd));
    R_RenderThread_Shutdown();assert(completed==1004 && recovered==1004);
    assert(prepared==1004 && rt_framesThreaded==1004 && rt_framesInline==3);
    assert(rt_framesCompleted==1004 && rt_syncWaits<1004);
    // Map shutdown calls FreeDefs directly, before FreeWorld. It must retire
    // even an already-finished worker before freeing/clearing the handle tables.
    idRenderWorldLocal world;
    world.entityDefs.Append(new idRenderEntityLocal{&world});
    world.lightDefs.Append(new idRenderLightLocal{&world});
    world.effectsDef.Append(new rvRenderEffectLocal{&world});
    cmd={1005,false};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    world.FreeDefs();
    assert(completed==1005 && defsFreed==3 && queuedFrees==0);
    world.FreeDefs();assert(defsFreed==3);
    cmd={1006,false};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    rb_debugText[0].text="retained";
    RB_ClearDebugText(0);
    assert(R_RenderThread_InFlight() && rb_debugText[0].text.empty());
    RB_ClearDebugText(100);
    assert(R_RenderThread_InFlight() && rb_debugTextTime==100);
    rb_numDebugText=1;rb_debugText[0].lifeTime=200;
    RB_ClearDebugText(150);
    assert(completed==1006 && !R_RenderThread_InFlight() && rb_numDebugText==1);
    RB_ClearDebugText(200);assert(rb_numDebugText==0);
    for(int kind=2;kind<=3;++kind){
        cmd={1005+kind,kind};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
        const int beforeDrops=drops,beforeFatals=fatals;
        bool receipt=false;
        try { R_RenderThread_Sync("failed recording"); } catch(const idException& e){
            receipt=std::strcmp(e.error,"recording invariant")==0;
        }
        assert(receipt && completed==cmd.serial && !R_RenderThread_InFlight());
        assert(drops==beforeDrops+(kind==2) && fatals==beforeFatals+(kind==3));
        R_RenderThread_Sync("failure receipt consumed exactly once");
    }
    cmd={1009,0};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    R_RenderThread_Shutdown();assert(completed==1009 && !rt_thread.joinable());
    // Hold the production consumer while the next producer changes its frame,
    // display state and primary view. Front-end entries must return without
    // waiting, while a resource barrier must still collect the old frame.
    r_vkRenderThread.value=2;verifyOverlap=true;ResetBackend(false);
    cmd={1010,0};assert(R_RenderThread_Begin(&cmd));
    {
        std::unique_lock<std::mutex> lock(backendMutex);
        assert(backendWake.wait_for(lock,std::chrono::seconds(5),[]{return backendEntered;}));
    }
    R_RenderThread_SyncFrontEnd("frame");
    frameData=&arena[1];frameData->payload=77;glConfig.width=1920;
    tr.primaryView=&replacement;tr.takingScreenshot=true;
    R_RenderThread_SyncFrontEnd("frame memory");
    R_RenderThread_SyncFrontEnd("dynamic model");
    assert(R_RenderThread_InFlight() && completed==1009 && rt_frontEndsOverlapped==1);
    assert(R_SubmittedPrimaryView()==&replacement && R_SubmittedTakingScreenshot());
    // Cvar changes affect future submissions, never ownership of this one.
    r_vkRenderThread.value=0;
    assert(R_RenderThread_FrontEndMayOverlap());
    {std::lock_guard<std::mutex> lock(backendMutex);backendReleased=true;backendWake.notify_all();}
    R_RenderThread_Sync("image mutation");
    assert(completed==1010 && !R_RenderThread_FrontEndMayOverlap() && selectedFrame==nullptr);
    R_RenderThread_Shutdown();
    r_vkRenderThread.value=1;verifyOverlap=false;ResetBackend(true);
    cmd={1011,0};assert(R_RenderThread_Begin(&cmd));WorkerFinished();
    RGM_SyncConfiguration();
    assert(completed==1011 && !R_RenderThread_InFlight());
    R_RenderThread_Shutdown();
    // Inline rendering uses the same severity without creating a worker receipt.
    for(bool fatal : {false,true}){
        const int before=fatal?fatals:drops;
        try { R_RenderThread_Error("inline invariant",fatal); assert(false); }
        catch(const idException& e){assert(std::strcmp(e.error,"inline invariant")==0);}
        assert((fatal?fatals:drops)==before+1);
    }
}
'''


def main():
    source = (ROOT / "src/renderer/Vulkan/vk_RenderThread.cpp").read_text(encoding="utf-8")
    # Compile the complete production state machine, only replacing engine types
    # and API boundaries. Do not duplicate its synchronization in the test.
    source = source[source.index("#include <atomic>"):source.rindex("#endif")]
    world = (ROOT / "src/renderer/RenderWorld_load.cpp").read_text(encoding="utf-8")
    source += WORLD_SUPPORT + function_body(world, "void idRenderWorldLocal::FreeDefs(")
    debug = (ROOT / "src/renderer/tr_rendertools.cpp").read_text(encoding="utf-8")
    source += function_body(debug, "void RB_ClearDebugText(")
    module = (ROOT / "src/renderer/RendererGLModule.cpp").read_text(encoding="utf-8")
    source += function_body(module, "static void RGM_SyncConfiguration(")
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="render-thread-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        cpp = temp / "handoff.cpp"
        cpp.write_text(SUPPORT + source + CASES, encoding="utf-8")
        exe = temp / ("handoff.exe" if os.name == "nt" else "handoff")
        subprocess.run([compiler, "-std=c++20", "-pthread", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    print("Render-thread handoff: fast/slow frames, exactly-once retirement, recoverable/fatal errors, disable, shutdown, restart and world teardown passed")


if __name__ == "__main__":
    main()
