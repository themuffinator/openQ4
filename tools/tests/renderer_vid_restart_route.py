#!/usr/bin/env python3
"""Run the production full-restart and startup routes for both renderer modules.

Only device/resource boundaries are stand-ins. The extracted functions retain
their real calls and preprocessor branches; no window, GPU or input is opened.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include "src/renderer/DisplayPresentation.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
static bool R_ImagePolicyActive(){return false;}
static bool R_ImagePolicyOperationAllowed(){return true;}
static void R_ImagePolicyObserveError(const char*,int=0){}
static void R_ImagePolicyBindRendererThread(){}
static bool R_ImagePolicyBeforeTeardown(char*,int){return true;}
static void R_ImagePolicyBeginDeviceReload(){}
static bool R_ImagePolicyAfterDeviceReload(char*,int){return true;}
static bool R_ImagePolicyFinish(char*,int){return true;}
static std::vector<std::string> events;
static bool deviceAlive=false, fontsAlive=false, configuredFullscreen=true;
static bool createdFullscreen=false;
static int imageReloads=0, imagePurges=0, deviceStarts=0;
static int failure=0, inputStarts=0;
static bool nestRestart=false;
static bool contextCurrent=true;
static int pendingGLError=0;
static bool r_recoverableRendererRestart=false,r_forceWindowRendererRestart=false,r_recoverableRendererRestore=false;
static bool r_initialRendererDevicePending=false;
static bool nestInitial=false;
struct renderWindowParms_t {
    int width=1280,height=720; bool fullScreen=false,borderless=false,hiddenWindow=false,stereo=false;
    int displayHz=0,multiSamples=0;
};
struct renderWindowRequest_t {
    renderWindowParms_t parms; unsigned displayId=0; int displayIndex=-1;
    bool fullscreenDesktop=false,spanDisplays=false; int swapInterval=1;
};
static renderWindowRequest_t* mutableInitialRequest=nullptr;
static const renderWindowRequest_t* r_recoverableWindowRequest=nullptr;
struct idStr:std::string {
    using std::string::string;
    const char* c_str() const { return std::string::c_str(); }
    static void Copynz(char* out,const char* text,int size) { if(size>0){int i=0;for(;i<size-1 && text[i];++i)out[i]=text[i];out[i]=0;} }
};
struct rendererRestartFailure_t {
    idStr reason; explicit rendererRestartFailure_t(const char* text):reason(text){}
};
bool R_IsRecoverableRendererRestart();
bool R_ForceWindowForRendererRestart();
const renderWindowRequest_t* R_GetRecoverableWindowRequest();
void R_RejectRecoverableRendererRestart(const char*);
bool R_TryFullVidRestart(const renderWindowRequest_t*,char*,int);
bool R_TryInitializeDisplay(const renderWindowRequest_t*,char*,int);
static renderDisplayPresentation_t devicePresentation={};
void R_GetDisplayPresentation(renderDisplayPresentation_t* output){*output=devicePresentation;}
void R_DisplayPresentationFailed(renderDisplayOutcome_t outcome,int32_t nativeError){
    ++devicePresentation.failureSequence;devicePresentation.outcome=outcome;devicePresentation.nativeError=nativeError;
}
struct Command { int commandId=0; Command* next=nullptr; } command;
struct Frame { Command* cmdHead=&command; } frameObject,*frameData=nullptr;
static const int RC_NOP=0;
struct { void* viewDef=nullptr; } backEnd;
struct idCmdArgs {};
static void Record(const char* event) { events.emplace_back(event); }
struct renderFeatureSet_t { bool scenePackets=false; };
struct Config {
    bool isInitialized=false;
    int vidWidth=0,vidHeight=0;
    const char *vendor_string=nullptr,*renderer_string=nullptr,*version_string=nullptr,*extensions_string=nullptr;
    int backendCaps=0;
    renderFeatureSet_t renderFeatures;
} glConfig;
struct CVars {
    bool GetCVarBool(const char*) { return configuredFullscreen; }
    void SetCVarBool(const char*,bool value) { configuredFullscreen=value; }
} cvars, *cvarSystem=&cvars;
struct RendererCvar {
    int value=0;
    int GetInteger() const { return value; }
    bool GetBool() const { return value!=0; }
    void SetInteger(int next) { value=next;Record("cvar-write"); }
    void SetModified() { Record("renderer-selection"); }
} r_renderer,r_multiSamples,r_displayRefresh,r_hiddenWindow,r_borderless,r_windowWidth{1280},r_windowHeight{720},r_customWidth{1920},r_customHeight{1080},r_mode;
struct FullscreenCvar {
    bool GetBool() const { return configuredFullscreen; }
    void SetInteger(int next) { configuredFullscreen=next!=0;Record("cvar-write"); }
} r_fullscreen;
struct vidmode_t { int width,height; } mode{1280,720};
static const vidmode_t* R_FindVidModeByMode(int value) { return value==0?&mode:nullptr; }
static bool Sys_GetDesktopResolution(int* width,int* height) { *width=1920;*height=1080;return true; }
static bool R_GetModeInfo(int* width,int* height,int) { *width=mode.width;*height=mode.height;return true; }
static void R_GetWindowedModeInfo(int* width,int* height) { *width=1280;*height=720; }
struct Common {
    void FatalError(const char* message,...) { throw std::runtime_error(message); }
    void Printf(const char*,...) {}
} commonObject, *common=&commonObject;
struct Images {
    void PurgeAllImages() {
        assert(!fontsAlive);
        ++imagePurges; Record("purge");
    }
    void ReloadImages(bool force) {
        assert(force && deviceAlive && glConfig.isInitialized && !fontsAlive);
        ++imageReloads; Record("reload");
        if(failure==6)pendingGLError=1;
    }
} images, *globalImages=&images;
struct VertexCache {
    void Init() { assert(deviceAlive); Record("vertex-cache"); }
    void PurgeAll() { Record("vertex-purge"); }
} vertexCache;
struct idRenderSystemLocal {
    int videoRestartCount=0,viewCount=0;
    int glContextGeneration=0,viewportOffset[2]={0,0};
    void *viewDef=nullptr,*primaryView=nullptr;
    struct Worlds { int count=0; int Num()const{return count;} } worlds;
    void* primaryWorld=nullptr;
    void InitOpenGL();
    void SetBackEndRenderer() { Record("backend-selection"); }
} tr;
struct Models { void FreeModelVertexCaches(){Record("model-purge");} } models,*renderModelManager=&models;
struct Session { void SetPlayingSoundWorld(){Record("sound-world");} } sessionObject,*session=&sessionObject;
static void StartDevice(const char* backend) {
    assert(!deviceAlive && !glConfig.isInitialized && !fontsAlive);
    createdFullscreen=r_recoverableWindowRequest?r_recoverableWindowRequest->parms.fullScreen:
        configuredFullscreen && !r_forceWindowRendererRestart;
    deviceAlive=true; glConfig.isInitialized=true;
    frameData=&frameObject;command={};
    ++deviceStarts; Record(backend);
}
static bool TryStartDevice(const char* backend) {
    if(nestRestart){char error[64];assert(!R_TryFullVidRestart(R_GetRecoverableWindowRequest(),error,sizeof(error)));assert(*error);}
    if(nestInitial){char error[64];assert(!R_TryInitializeDisplay(R_GetRecoverableWindowRequest(),error,sizeof(error)));assert(*error);}
    if(mutableInitialRequest){
        const auto* actual=R_GetRecoverableWindowRequest();assert(actual && actual!=mutableInitialRequest);
        const int width=actual->parms.width;mutableInitialRequest->parms.width=333;
        assert(actual->parms.width==width);mutableInitialRequest=nullptr;
    }
    if(failure==1)return false;
    StartDevice(backend);
    if(failure==2){R_DisplayPresentationFailed(RDP_CONTEXT_FAILED,-123);return false;}
    if(failure==3)R_RejectRecoverableRendererRestart("unsupported backend");
    return true;
}
static void R_InitOpenGL() {
#ifdef OPENQ4_RENDERER_VK_MODULE
    throw std::runtime_error("Vulkan restart entered the OpenGL initializer");
#else
    StartDevice("opengl");
#endif
}
static bool R_InitOpenGLInternal(bool legacy,bool,char* error,int size) {
    assert(!legacy);
    if(!TryStartDevice("opengl")){idStr::Copynz(error,"OpenGL device failed",size);return false;}
    return true;
}
bool VK_InitRenderDevice() { return TryStartDevice("vulkan"); }
static void GLimp_Shutdown() {
    assert(!fontsAlive);
    deviceAlive=false; Record("device-shutdown");
}
static void R_DoneFreeType() {
    fontsAlive=false; Record("fonts-shutdown");
}
static void R_InitFreeType() {
    assert(deviceAlive && !fontsAlive);
    fontsAlive=true; Record("fonts-init");
}
static void R_RefreshConsoleFontAtlas() {
    assert(deviceAlive && fontsAlive); Record("font-atlas");
    if(failure==5)pendingGLError=1;
}
static void R_ShutdownRenderTargetsBeforeImagePurge() {
    Record("targets-shutdown");
}
static void R_ClearActiveRenderTextures() { Record("targets-clear"); }
static void R_MaterialResourceTable_Init(int,renderFeatureSet_t features) {
    assert(deviceAlive && features.scenePackets); Record("materials-init");
}
static void R_GpuSkinning_ContractInit(int) { Record("skinning-init"); }
static int glGetError() { assert(deviceAlive);const int error=pendingGLError;pendingGLError=0;return error; }
static bool GLimp_EnsureActiveContext(const char*) { return contextCurrent; }
static const int GL_NO_ERROR=0;
#define NOOP(name) static void name() { Record(#name); }
NOOP(R_MigrateLegacyShadowMapContactQuality)
NOOP(Sys_ShutdownInput)
NOOP(R_RendererMetrics_ShutdownGpuTimers)
NOOP(R_ClassicGuiDomain_ResetFrame)
NOOP(R_ClassicCinematicPostDomain_ResetFrame)
NOOP(R_ClassicSpecialFrameDomain_ResetFrame)
NOOP(R_ClassicWorldAmbientDomain_ResetFrame)
NOOP(R_ClassicInteractionDomain_ResetFrame)
NOOP(R_ClassicFogBlendDomain_ResetFrame)
NOOP(R_ClassicSubviewDomain_ResetFrame)
NOOP(R_MaterialResourceTable_Shutdown)
NOOP(R_RenderGraphResources_Shutdown)
NOOP(R_ModernGLExecutor_Shutdown)
NOOP(R_GpuSkinning_ContractShutdown)
NOOP(R_RendererUpload_Shutdown)
NOOP(RendererBootstrap_Shutdown)
NOOP(R_PurgeFramebufferCopyFBOs)
NOOP(R_GLDebugOutput_Shutdown)
NOOP(R_SetColorMappings)
static void R_ShutdownFrameData(){frameData=nullptr;Record("R_ShutdownFrameData");}
static void R_InitFrameData(){frameData=&frameObject;command={};Record("R_InitFrameData");}
static void R_RendererMetrics_ResetGpuFrameTiming(const char*){Record("timing-reset");}
static void Sys_GrabMouseCursor(bool value){assert(!value);Record("cursor-release");}
static void R_FreeDerivedData(){Record("derived-free");}
static void R_ToggleSmpFrame(){assert(frameData);command={};Record("frame-drain");}
static void R_RegenerateWorld_f(const idCmdArgs&){assert(fontsAlive && deviceAlive);Record("world-ready");if(failure==4)R_RejectRecoverableRendererRestart("world program refused");}
'''

CONTEXT = r'''
bool R_TryFullVidRestart(const renderWindowRequest_t*,char*,int){assert(false);return false;}
bool R_TryInitializeDisplay(const renderWindowRequest_t*,char*,int){assert(false);return false;}
using GLint=int;using GLenum=unsigned;using GLubyte=unsigned char;
using glimpParms_t=renderWindowParms_t;
static const int GL_VENDOR=1,GL_RENDERER=2,GL_VERSION=3,GL_EXTENSIONS=4,GL_TRUE=1,GLEW_OK=0;
static bool hasVersion=true;
static int contextRefusals=0,contextAttempts=0,glewResult=0,normalizations=0,moduleBoots=0;
static glimpParms_t observed;
static bool glewExperimental=false;
struct { int vidWidth=0,vidHeight=0; } engineWindowState;
static bool GLimp_Init(glimpParms_t parms) {
    ++contextAttempts;observed=parms;
    if(contextRefusals>0){--contextRefusals;return false;}
    deviceAlive=true;return true;
}
static const char* glGetString(int name) { return name==GL_VERSION&&!hasVersion?nullptr:"test"; }
static int glewInit(){return glewResult;}
static const GLubyte* glewGetErrorString(int){return reinterpret_cast<const GLubyte*>("mock loader failure");}
static const char* va(const char* format,const char* detail) {
    static char text[128];std::snprintf(text,sizeof(text),format,detail);return text;
}
static void Sys_InitInput(){++inputStarts;}
static void R_NormalizeDisplayCvars(){++normalizations;}
static void R_RendererModule_Boot(){++moduleBoots;}
static const int RENDERER_STARTUP_PHASE_R_INIT_OPENGL=1;
static void R_RecordRendererStartupPhase(int){}
static void RB_ResetARB2InteractionHandoffBreadcrumb(){}
static void RB_ResetAppleGL21RouteCounters(){}
static void ResetContext() {
    events.clear();glConfig.isInitialized=false;deviceAlive=false;contextCurrent=hasVersion=true;
    contextRefusals=contextAttempts=glewResult=normalizations=moduleBoots=0;inputStarts=0;
}
'''

CONTEXT_MAIN = r'''
int main() {
    char error[128];
    renderWindowRequest_t request;request.parms.width=1600;request.parms.height=900;
    request.parms.fullScreen=true;request.parms.multiSamples=8;request.parms.displayHz=75;
    r_recoverableRendererRestart=true;r_recoverableWindowRequest=&request;
    ResetContext();contextRefusals=2;
    assert(!R_CreateOpenGLContext(false,false,error,sizeof(error)) && *error);
    assert(contextAttempts==1 && contextRefusals==1 && !normalizations && !moduleBoots && events.empty());
    assert(observed.width==1600 && observed.height==900 && observed.multiSamples==8 && observed.displayHz==75 && observed.fullScreen);
    ResetContext();contextCurrent=false;
    assert(!R_CreateOpenGLContext(false,false,error,sizeof(error)) && contextAttempts==1 && deviceAlive);
    ResetContext();hasVersion=false;
    assert(!R_CreateOpenGLContext(false,false,error,sizeof(error)) && contextAttempts==1);
    ResetContext();assert(R_CreateOpenGLContext(false,false,error,sizeof(error)) && !normalizations && !moduleBoots && events.empty());
    contextCurrent=false;assert(!R_InitOpenGLExtensionLoader(error,sizeof(error)));
    contextCurrent=true;glewResult=1;assert(!R_InitOpenGLExtensionLoader(error,sizeof(error)));
    assert(std::string(error).find("mock loader failure")!=std::string::npos);
    glewResult=0;assert(R_InitOpenGLExtensionLoader(error,sizeof(error)));
    r_recoverableRendererRestart=false;r_recoverableWindowRequest=nullptr;
    ResetContext();contextRefusals=1;
    assert(R_CreateOpenGLContext(true,false,error,sizeof(error)) && contextAttempts==2);
    assert(normalizations==1 && moduleBoots==1 && std::count(events.begin(),events.end(),"cvar-write")==5);
    ResetContext();contextRefusals=2;
    assert(!R_CreateOpenGLContext(true,false,error,sizeof(error)) && contextAttempts==2);
    assert(normalizations==1 && moduleBoots==1 && std::count(events.begin(),events.end(),"cvar-write")==5);
    std::puts("OpenGL context/loader: strict single attempt and immutable request, recoverable context/version/GLEW refusal, legacy safe retry preserved");
}
'''

MAIN = r'''
static size_t Position(const char* event) {
    auto found=std::find(events.begin(),events.end(),event);
    assert(found!=events.end());
    return static_cast<size_t>(found-events.begin());
}
static void ColdInitialize() {
    char error[128]="sentinel";renderWindowRequest_t request;request.parms.width=1024;request.parms.height=768;
    assert(!R_TryInitializeDisplay(&request,error,sizeof(error)) && *error && events.empty());
    r_initialRendererDevicePending=true; // renderer Init completion is source-guarded below
    assert(!R_TryInitializeDisplay(nullptr,error,sizeof(error)) && events.empty());
    globalImages=nullptr;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));globalImages=&images;
    renderModelManager=nullptr;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));renderModelManager=&models;
    glConfig.isInitialized=true;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));glConfig.isInitialized=false;
    r_recoverableRendererRestore=true;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));r_recoverableRendererRestore=false;
    r_recoverableRendererRestart=true;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));r_recoverableRendererRestart=false;
    frameData=&frameObject;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));frameData=nullptr;
    tr.worlds.count=1;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));tr.worlds.count=0;
    for(void** pointer:{&tr.primaryWorld,&tr.viewDef,&tr.primaryView,&backEnd.viewDef}){
        *pointer=&images;assert(!R_TryInitializeDisplay(&request,error,sizeof(error)));*pointer=nullptr;
    }
    std::vector<renderWindowRequest_t> malformed;
    for(int value:{-1,0,319,16385}){auto bad=request;bad.parms.width=value;malformed.push_back(bad);}
    for(int value:{-1,0,239,16385}){auto bad=request;bad.parms.height=value;malformed.push_back(bad);}
    for(int value:{-1,1,3,17}){auto bad=request;bad.parms.multiSamples=value;malformed.push_back(bad);}
    for(int value:{-1,1001}){auto bad=request;bad.parms.displayHz=value;malformed.push_back(bad);}
    for(int value:{-2,2}){auto bad=request;bad.swapInterval=value;malformed.push_back(bad);}
    auto bad=request;bad.displayIndex=-2;malformed.push_back(bad);
    bad=request;bad.parms.stereo=true;malformed.push_back(bad);
    bad=request;bad.parms.hiddenWindow=bad.parms.fullScreen=true;malformed.push_back(bad);
    bad=request;bad.parms.hiddenWindow=bad.parms.borderless=true;malformed.push_back(bad);
    bad=request;bad.parms.fullScreen=bad.parms.borderless=true;malformed.push_back(bad);
    for(const auto& invalid:malformed){
        assert(!R_TryInitializeDisplay(&invalid,error,sizeof(error)) && *error);
        assert(events.empty() && !deviceStarts && r_initialRendererDevicePending && !R_GetRecoverableWindowRequest());
    }
    std::vector<int> refused{1,2,3};
#ifndef OPENQ4_RENDERER_VK_MODULE
    refused.push_back(6);
#endif
    for(int code:refused){
        failure=code;const auto failures=devicePresentation.failureSequence;
        assert(!R_TryInitializeDisplay(&request,error,sizeof(error)) && *error);
        assert(!deviceAlive && !glConfig.isInitialized && !frameData && !fontsAlive);
        assert(r_initialRendererDevicePending && !r_recoverableRendererRestore);
        assert(!R_GetRecoverableWindowRequest() && !R_IsRecoverableRendererRestart());
        assert(devicePresentation.failureSequence==failures+1 && !devicePresentation.presentedSequence);
        assert(devicePresentation.outcome==(code==2?RDP_CONTEXT_FAILED:RDP_INIT_FAILED));
        assert(devicePresentation.nativeError==(code==2?-123:0));
        assert(!tr.videoRestartCount && !tr.viewCount);
        for(const char* absent:{"cvar-write","fonts-init","font-atlas","world-ready","sound-world","model-purge","derived-free","cursor-release"})
            assert(std::find(events.begin(),events.end(),absent)==events.end());
        events.clear();
        // A failed cold start does not enable the live restart route.
        assert(!R_TryFullVidRestart(&request,error,sizeof(error)) && events.empty());
    }
    failure=0;nestInitial=nestRestart=true;mutableInitialRequest=&request;
    assert(R_TryInitializeDisplay(&request,error,sizeof(error)) && !*error);
    nestInitial=nestRestart=false;
    assert(request.parms.width==333 && deviceAlive && glConfig.isInitialized && frameData);
    assert(!r_initialRendererDevicePending && !r_recoverableRendererRestore && !fontsAlive);
    assert(!R_GetRecoverableWindowRequest() && !R_IsRecoverableRendererRestart());
    assert(!tr.videoRestartCount && !tr.viewCount && !devicePresentation.presentedSequence);
    for(const char* absent:{"cvar-write","fonts-init","font-atlas","world-ready","sound-world","model-purge","derived-free","cursor-release"})
        assert(std::find(events.begin(),events.end(),absent)==events.end());
    events.clear();assert(!R_TryInitializeDisplay(&request,error,sizeof(error)) && events.empty());
    R_ShutdownDeviceForRestart();events.clear();
    assert(!R_TryInitializeDisplay(&request,error,sizeof(error)) && events.empty());
    // Reset stand-in counters for the legacy/restart cases below, without
    // granting cold-start permission after a successful device shutdown.
    deviceStarts=imageReloads=imagePurges=0;devicePresentation={};
    std::puts("Strict first device: Init-only gate, immutable request, preflight, GL/VK failure cleanup, explicit cold retry, no world/font/action replay or false present passed");
}
int main() {
#ifdef OPENQ4_RENDERER_VK_MODULE
    const char* backend="vulkan";
#else
    const char* backend="opengl";
#endif
    ColdInitialize();
    tr.InitOpenGL();
    assert(deviceStarts==1 && imageReloads==1 && createdFullscreen);
    assert(Position(backend)<Position("reload"));
    events.clear();
    tr.InitOpenGL();
    assert(deviceStarts==1 && imageReloads==1);
    assert(events.size()==1 && events.front()=="R_MigrateLegacyShadowMapContactQuality");
    fontsAlive=true;frameData=&frameObject;

    // Repeat from each fullscreen preference, with and without a forced
    // window. The temporary restart mode must not replace the saved setting.
    for(bool fullscreen : {true,false}) {
        for(bool forceWindow : {false,true}) {
            configuredFullscreen=fullscreen;
            events.clear();
            const int startsBefore=deviceStarts, reloadsBefore=imageReloads;
            R_PerformFullVidRestart(forceWindow);
            assert(configuredFullscreen==fullscreen);
            assert(createdFullscreen==(fullscreen && !forceWindow));
            assert(deviceStarts==startsBefore+1 && imageReloads==reloadsBefore+1);
            assert(imagePurges==imageReloads-1 && fontsAlive && deviceAlive);
            assert(Position("targets-shutdown")<Position("fonts-shutdown"));
            assert(Position("fonts-shutdown")<Position("purge"));
            assert(Position("purge")<Position("device-shutdown"));
            assert(Position("device-shutdown")<Position(backend));
            assert(Position(backend)<Position("reload"));
            assert(Position("reload")<Position("fonts-init"));
            assert(Position("fonts-init")<Position("font-atlas"));
#ifdef OPENQ4_RENDERER_VK_MODULE
            assert(Position("vulkan")<Position("materials-init"));
            assert(Position("materials-init")<Position("vertex-cache"));
            assert(Position("vertex-cache")<Position("R_InitFrameData"));
            assert(Position("R_InitFrameData")<Position("reload"));
#endif
        }
    }
    events.clear();
    char error[128]="sentinel";
    renderWindowRequest_t request;request.parms.fullScreen=true;request.parms.multiSamples=4;
    auto invalid=request;invalid.parms.width=100;
    assert(!R_TryFullVidRestart(&invalid,error,sizeof(error)) && *error && events.empty() && deviceAlive);
    invalid=request;invalid.parms.multiSamples=3;
    assert(!R_TryFullVidRestart(&invalid,error,sizeof(error)) && events.empty());
    invalid=request;invalid.parms.stereo=true;
    assert(!R_TryFullVidRestart(&invalid,error,sizeof(error)) && events.empty());
    command.next=&command;
    assert(!R_TryFullVidRestart(&request,error,sizeof(error)) && events.empty());command.next=nullptr;
    std::vector<int> refusals{1,2,3,4};
#ifndef OPENQ4_RENDERER_VK_MODULE
    refusals.push_back(5);refusals.push_back(6);
#endif
    for(int refused:refusals) {
        failure=refused;const auto generation=tr.videoRestartCount;events.clear();
        const auto failures=devicePresentation.failureSequence;
        assert(!R_TryFullVidRestart(&request,error,sizeof(error)) && *error);
        assert(!deviceAlive && !glConfig.isInitialized && !fontsAlive && !frameData);
        assert(tr.videoRestartCount==generation && r_recoverableRendererRestore);
        assert(devicePresentation.failureSequence==failures+1);
        assert(devicePresentation.outcome==(refused==2?RDP_CONTEXT_FAILED:RDP_INIT_FAILED));
        assert(devicePresentation.nativeError==(refused==2?-123:0));
        assert(!R_IsRecoverableRendererRestart() && !R_GetRecoverableWindowRequest());
        assert(Position("targets-shutdown")<Position("purge"));
        assert(events.back()=="targets-clear");
        // Invalid restore preserves the failed state; a valid explicit restore
        // runs without frameData and advances readiness exactly once.
        events.clear();assert(!R_TryFullVidRestart(&invalid,error,sizeof(error)) && events.empty());
        failure=0;nestRestart=true;
        assert(R_TryFullVidRestart(&request,error,sizeof(error)) && !*error);nestRestart=false;
        assert(deviceAlive && fontsAlive && frameData && tr.videoRestartCount==generation+1);
        assert(devicePresentation.failureSequence==failures+1 && devicePresentation.presentedSequence==0);
        assert(createdFullscreen && !r_recoverableRendererRestore && !R_IsRecoverableRendererRestart());
        assert(Position("world-ready")<Position("sound-world"));
        assert(!R_GetRecoverableWindowRequest());
    }
    assert(!R_TryFullVidRestart(static_cast<const renderWindowRequest_t*>(nullptr),error,sizeof(error)));
    // Before a renderer has ever started (or after normal shutdown), this is
    // not a replacement for the legacy startup API.
    glConfig.isInitialized=false;r_recoverableRendererRestore=false;
    assert(!R_TryFullVidRestart(&request,error,sizeof(error)));
    std::printf("%s full restart: native route, resource order, repeated restart, single reload and window preference passed\n",backend);
    std::printf("%s recoverable restart: preflight, request scope, refusal/cleanup, nested guard, explicit restore and ready generation passed\n",backend);
}
'''

SCREEN = r'''
#include "src/renderer/DisplayPresentation.h"
#include <cassert>
#include <cstring>
#include <cstdio>
struct glimpParms_t { int width=0,height=0,displayHz=0,multiSamples=0; bool fullScreen=false,borderless=false,hiddenWindow=false,stereo=false; };
using renderWindowParms_t = glimpParms_t;
struct renderWindowRequest_t { renderWindowParms_t parms; } strictRequest;
struct renderWindowState_t {};
static bool strict=false,haveRequest=true;
static bool R_IsRecoverableRendererRestart(){return strict;}
static const renderWindowRequest_t* R_GetRecoverableWindowRequest(){return haveRequest?&strictRequest:nullptr;}
void R_DisplayPresentationFailed(renderDisplayOutcome_t,int32_t){}
struct Common { void Warning(const char*,...){} } commonObject,*common=&commonObject;
struct { int vidWidth=640,vidHeight=480; bool isFullscreen=false; } glConfig;
struct { struct { unsigned width=1600,height=900; } swapchainExtent; } vkCtx;
static bool windowOkay=true,swapchainOkay=true;
static int windows=0,swaps=0;
static bool ApplyStrict(const renderWindowRequest_t* request,renderWindowState_t*,char*,int) {
    assert(request==&strictRequest);++windows;return windowOkay;
}
struct Services {
    bool (*ApplyScreenParmsStrict)(const renderWindowRequest_t*,renderWindowState_t*,char*,int)=ApplyStrict;
    bool ApplyScreenParms(const renderWindowParms_t* p) {
        ++windows; assert(p->width==1920 && p->height==1080 && p->fullScreen);
        return windowOkay;
    }
} services,*vkBackendServices=nullptr;
static bool VK_Device_RecreateSwapchain() { ++swaps; return swapchainOkay; }
'''

SCREEN_MAIN = r'''
int main() {
    glimpParms_t p; p.width=1920; p.height=1080; p.fullScreen=true;
    assert(!GLimp_SetScreenParms(p) && !windows && !swaps);
    vkBackendServices=&services; windowOkay=false;
    assert(!GLimp_SetScreenParms(p) && windows==1 && !swaps);
    windowOkay=true; swapchainOkay=false;
    assert(!GLimp_SetScreenParms(p) && windows==2 && swaps==1);
    assert(glConfig.vidWidth==640 && glConfig.vidHeight==480 && !glConfig.isFullscreen);
    swapchainOkay=true;
    assert(GLimp_SetScreenParms(p) && windows==3 && swaps==2);
    assert(glConfig.vidWidth==1600 && glConfig.vidHeight==900 && glConfig.isFullscreen);
    strict=true;strictRequest.parms=p;strictRequest.parms.fullScreen=false;
    assert(GLimp_SetScreenParms(p) && !glConfig.isFullscreen);
    strictRequest.parms.multiSamples=4;assert(!GLimp_SetScreenParms(p));strictRequest.parms.multiSamples=0;
    haveRequest=false;assert(!GLimp_SetScreenParms(p));haveRequest=true;
    services.ApplyScreenParmsStrict=nullptr;assert(!GLimp_SetScreenParms(p));
    std::puts("Vulkan screen apply: window/swapchain failure propagates without publishing stale dimensions");
}
'''

DEVICE_INIT = r'''
#include "src/renderer/DisplayPresentation.h"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> events;
static bool servicesAvailable=true,prepareOkay=true,createOkay=true,deviceOkay=true;
static bool applyOkay=true,recreateOkay=true,preserved=false,deviceAlive=false,windowAlive=false;
static int pixelWidth=1280,pixelHeight=720;
struct glimpParms_t {
    int width,height,displayHz,multiSamples;
    bool fullScreen,borderless,hiddenWindow,stereo;
};
using renderWindowParms_t=glimpParms_t;
struct renderWindowRequest_t { renderWindowParms_t parms; } strictRequest;
struct renderWindowState_t {};
static bool strict=false,haveRequest=true,initialSizeOkay=true;
static renderWindowParms_t observedParms={};
static bool R_IsRecoverableRendererRestart(){return strict;}
static bool R_ForceWindowForRendererRestart(){return false;}
static const renderWindowRequest_t* R_GetRecoverableWindowRequest(){return haveRequest?&strictRequest:nullptr;}
void R_DisplayPresentationFailed(renderDisplayOutcome_t,int32_t){}
struct renderFramebufferDesc_t {
    int surfaceKind,redBits,greenBits,blueBits,alphaBits,depthBits,stencilBits;
    bool doubleBuffer;
};
static const int RENDER_SURFACE_VULKAN=1;
struct renderModuleWindowInfo_t {
    int pixelWidth,pixelHeight,uiViewportX,uiViewportY,uiViewportWidth,uiViewportHeight;
};
struct {
    bool isInitialized=false;
    int uiViewportX=0,uiViewportY=0,uiViewportWidth=0,uiViewportHeight=0;
    const char* renderer_string="test";
    const char* version_string="test";
} glConfig;
struct { struct { uint32_t width=640,height=480; } swapchainExtent; } vkCtx;
struct CVar { bool GetBool() const { return false; } int GetInteger() const { return 0; } };
static CVar r_fullscreen,r_borderless,r_displayRefresh,r_hiddenWindow;
struct Common {
    void Printf(const char*,...) {}
    void Warning(const char*,...) { events.emplace_back("warning"); }
} commonObject,*common=&commonObject;
static bool ApplyStrict(const renderWindowRequest_t* request,renderWindowState_t*,char*,int) {
    assert(request==&strictRequest && deviceAlive && windowAlive);
    events.emplace_back("strict-window-apply");return applyOkay;
}
struct Services {
    bool (*ApplyScreenParmsStrict)(const renderWindowRequest_t*,renderWindowState_t*,char*,int)=ApplyStrict;
    bool PrepareWindowSystem() { events.emplace_back("prepare"); return prepareOkay; }
    bool CreateWindowForFramebuffer(const renderFramebufferDesc_t* desc,const renderWindowParms_t* parms,
                                   renderModuleWindowInfo_t*,bool* reused) {
        assert(desc->surfaceKind==RENDER_SURFACE_VULKAN);
        observedParms=*parms;
        events.emplace_back("window-create"); *reused=preserved;
        if(createOkay)windowAlive=true;
        return createOkay;
    }
    bool ApplyScreenParms(const renderWindowParms_t*) {
        assert(deviceAlive && windowAlive && !glConfig.isInitialized);
        events.emplace_back("window-apply"); return applyOkay;
    }
    void RefreshNativeWindowHandles(renderModuleWindowInfo_t* info) {
        assert(deviceAlive && windowAlive && applyOkay);
        events.emplace_back("refresh"); info->pixelWidth=pixelWidth;info->pixelHeight=pixelHeight;
        info->uiViewportX=11;info->uiViewportY=17;
        info->uiViewportWidth=1000;info->uiViewportHeight=600;
    }
    void DestroyAttemptWindow() {
        assert(!deviceAlive && !glConfig.isInitialized);
        events.emplace_back("window-destroy"); if(!preserved)windowAlive=false;
    }
    void NotifyWindowReady() {
        assert(deviceAlive && windowAlive && glConfig.isInitialized && events.back()=="config");
        events.emplace_back("ready");
    }
} services,*vkBackendServices=nullptr;
static Services* Sys_GetRenderWindowServices() { return servicesAvailable?&services:nullptr; }
static bool R_GetInitialWindowSize(bool,int* width,int* height) { *width=640;*height=480;return initialSizeOkay; }
static bool VK_Device_Init(Services* value) {
    assert(value==&services && windowAlive && !deviceAlive && !glConfig.isInitialized);
    events.emplace_back("device-init"); deviceAlive=deviceOkay;
    vkCtx.swapchainExtent={640,480}; return deviceOkay;
}
static void VK_Device_Shutdown() {
    assert(deviceAlive && windowAlive && !glConfig.isInitialized);
    events.emplace_back("device-shutdown");deviceAlive=false;
}
static bool VK_Device_RecreateSwapchain() {
    assert(deviceAlive && windowAlive && !glConfig.isInitialized);
    events.emplace_back("recreate");
    // A failed replacement can retire the old chain: keeping the device
    // alive is not evidence that it can submit another frame.
    vkCtx.swapchainExtent={};
    if(recreateOkay)vkCtx.swapchainExtent={static_cast<uint32_t>(pixelWidth),static_cast<uint32_t>(pixelHeight)};
    return recreateOkay;
}
static void VK_FillGLConfigFromDevice() {
    assert(deviceAlive && windowAlive && applyOkay && !glConfig.isInitialized);
    assert(vkCtx.swapchainExtent.width && vkCtx.swapchainExtent.height);
    events.emplace_back("config");
}
static void Sys_InitInput() {
    assert(glConfig.isInitialized && events.back()=="ready");events.emplace_back("input");
}
static void Reset() {
    events.clear(); servicesAvailable=prepareOkay=createOkay=deviceOkay=applyOkay=recreateOkay=true;
    preserved=deviceAlive=windowAlive=false;glConfig={};pixelWidth=1280;pixelHeight=720;
    strict=false;haveRequest=initialSizeOkay=true;strictRequest={};services.ApplyScreenParmsStrict=ApplyStrict;
}
static void Expect(std::initializer_list<const char*> expected) {
    std::vector<std::string> names;for(const char* value:expected)names.emplace_back(value);
    assert(events==names);
}
'''

DEVICE_INIT_MAIN = r'''
int main() {
    Reset();servicesAvailable=false;
    assert(!VK_InitRenderDevice());Expect({"warning"});
    Reset();prepareOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","warning"});
    Reset();createOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","window-create","warning"});
    Reset();deviceOkay=false;
    assert(!VK_InitRenderDevice());Expect({"prepare","window-create","device-init","window-destroy"});
    for(bool keepWindow:{false,true}) {
        for(bool failWindow:{false,true}) {
            Reset();preserved=keepWindow;
            applyOkay=!failWindow;recreateOkay=failWindow;
            assert(!VK_InitRenderDevice());
            if(failWindow)Expect({"prepare","window-create","device-init","window-apply","warning","device-shutdown","window-destroy"});
            else Expect({"prepare","window-create","device-init","window-apply","refresh","recreate","warning","device-shutdown","window-destroy"});
            assert(!deviceAlive && !glConfig.isInitialized && windowAlive==keepWindow);
            assert(!glConfig.uiViewportWidth && !glConfig.uiViewportHeight);
            // A clean attempt can follow either refusal without a stale live
            // device, even when the engine preserved its native window.
            events.clear();applyOkay=recreateOkay=true;
            assert(VK_InitRenderDevice());
            Expect({"prepare","window-create","device-init","window-apply","refresh","recreate","config","ready","input"});
            assert(deviceAlive && windowAlive && glConfig.isInitialized);
            assert(glConfig.uiViewportX==11 && glConfig.uiViewportY==17 && glConfig.uiViewportWidth==1000 && glConfig.uiViewportHeight==600);
        }
    }
    Reset();pixelWidth=640;pixelHeight=480;
    assert(VK_InitRenderDevice());
    Expect({"prepare","window-create","device-init","window-apply","refresh","config","ready","input"});
    Reset();initialSizeOkay=false;assert(!VK_InitRenderDevice());Expect({"prepare"});
    Reset();strict=true;haveRequest=false;assert(!VK_InitRenderDevice());Expect({"prepare","warning"});
    Reset();strict=true;strictRequest.parms.multiSamples=4;assert(!VK_InitRenderDevice());Expect({"prepare","warning"});
    Reset();strict=true;strictRequest.parms.stereo=true;assert(!VK_InitRenderDevice());Expect({"prepare","warning"});
    Reset();strict=true;strictRequest.parms.width=800;strictRequest.parms.height=600;strictRequest.parms.hiddenWindow=true;
    assert(VK_InitRenderDevice() && observedParms.width==800 && observedParms.height==600 && observedParms.hiddenWindow);
    Expect({"prepare","window-create","device-init","strict-window-apply","refresh","recreate","config","ready","input"});
    Reset();strict=true;applyOkay=false;
    assert(!VK_InitRenderDevice() && !deviceAlive && !windowAlive && !glConfig.isInitialized);
    Expect({"prepare","window-create","device-init","strict-window-apply","warning","warning","device-shutdown","window-destroy"});
    Reset();strict=true;services.ApplyScreenParmsStrict=nullptr;
    assert(!VK_InitRenderDevice() && !deviceAlive && !windowAlive && !glConfig.isInitialized);
    Expect({"prepare","window-create","device-init","warning","warning","device-shutdown","window-destroy"});
    std::puts("Vulkan startup: screen/swapchain refusal cleans device before attempt window, skips publication/input and permits retry");
}
'''


def main():
    source = (ROOT / 'src/renderer/RenderSystem_init.cpp').read_text(encoding='utf-8')
    init_body = function_body(source, 'void idRenderSystemLocal::Init( void )')
    assert init_body.index('r_initialRendererDevicePending = false;') < init_body.index('globalImages->Init();')
    assert init_body.index('r_initialRendererDevicePending = true;') > init_body.index('renderModelManager->Init();')
    assert 'r_initialRendererDevicePending = false;' in function_body(source, 'void idRenderSystemLocal::Shutdown( void )')
    assert 'r_initialRendererDevicePending = true;' not in function_body(source, 'void idRenderSystemLocal::ShutdownOpenGL( void )')
    glue = (ROOT / 'src/renderer/RendererGLModule.cpp').read_text(encoding='utf-8')
    assert 'rgm_export.TryInitializeDisplay = R_TryInitializeDisplay;' in glue
    code = SUPPORT + '\n'.join(function_body(source, signature) for signature in (
        'bool R_IsRecoverableRendererRestart( void )',
        'bool R_ForceWindowForRendererRestart( void )',
        'const renderWindowRequest_t *R_GetRecoverableWindowRequest( void )',
        'void R_RejectRecoverableRendererRestart( const char *reason )',
        'static bool R_RendererRestartError( char *error, int errorSize, const char *reason )',
        'bool R_GetInitialWindowSize( bool fullScreen, int *width, int *height )',
        'static int R_NormalizeMultiSamplesValue( const int samples )',
        'static void R_ShutdownDeviceForRestart( void )',
        'static bool R_InitRendererDevice( bool legacyPolicy, bool forceWindow, char *error, int errorSize ) {',
        'void idRenderSystemLocal::InitOpenGL( void )',
        'static void R_PerformFullVidRestart( bool forceWindow )',
        'static bool R_TryFullVidRestartInternal( const renderWindowRequest_t *request, bool forceWindow, char *error, int errorSize )',
        'bool R_TryFullVidRestart( const renderWindowRequest_t *request, char *error, int errorSize )',
        'bool R_TryInitializeDisplay( const renderWindowRequest_t *request, char *error, int errorSize )',
    )) + MAIN
    compiler = next((found for name in ('clang++', 'g++', 'c++') if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vid-restart-', dir=ROOT / '.tmp') as temp:
        env = dict(os.environ, TEMP=temp, TMP=temp)
        test_source = Path(temp) / 'restart.cpp'
        test_source.write_text(code, encoding='utf-8')
        for backend in ('opengl', 'vulkan'):
            binary = Path(temp) / f'{backend}.exe'
            define = ['-DOPENQ4_RENDERER_VK_MODULE'] if backend == 'vulkan' else []
            subprocess.run([compiler, '-std=c++17', *define, '-I', str(ROOT), str(test_source), '-o', str(binary)], check=True, env=env)
            subprocess.run([str(binary)], check=True, env=env)
        context_source = Path(temp) / 'context.cpp'
        context_source.write_text(SUPPORT + CONTEXT + '\n'.join(function_body(source, signature) for signature in (
            'bool R_IsRecoverableRendererRestart( void )',
            'bool R_ForceWindowForRendererRestart( void )',
            'const renderWindowRequest_t *R_GetRecoverableWindowRequest( void )',
            'void R_RejectRecoverableRendererRestart( const char *reason )',
            'static bool R_RendererRestartError( char *error, int errorSize, const char *reason )',
            'bool R_GetInitialWindowSize( bool fullScreen, int *width, int *height )',
            'static bool R_InitOpenGLExtensionLoader( char *error, int errorSize )',
            'static bool R_CreateOpenGLContext( bool legacyPolicy, bool forceWindow, char *error, int errorSize )',
        )) + CONTEXT_MAIN, encoding='utf-8')
        binary = Path(temp) / 'context.exe'
        subprocess.run([compiler, '-std=c++17', '-I', str(ROOT), str(context_source), '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
        backend_source = (ROOT / 'src/renderer/Vulkan/vk_Backend.cpp').read_text(encoding='utf-8')
        screen_source = Path(temp) / 'screen.cpp'
        screen_source.write_text(SCREEN + function_body(backend_source,
            'static bool VK_ApplyRequestedScreenParms( const renderWindowParms_t& parms )') + function_body(backend_source,
            'bool GLimp_SetScreenParms( glimpParms_t parms )') + SCREEN_MAIN, encoding='utf-8')
        binary = Path(temp) / 'screen.exe'
        subprocess.run([compiler, '-std=c++17', '-I', str(ROOT), str(screen_source), '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
        init_source = Path(temp) / 'device-init.cpp'
        init_source.write_text(DEVICE_INIT + function_body(backend_source,
            'static bool VK_ApplyRequestedScreenParms( const renderWindowParms_t& parms )') + function_body(backend_source,
            'bool VK_InitRenderDevice( void )') + DEVICE_INIT_MAIN, encoding='utf-8')
        binary = Path(temp) / 'device-init.exe'
        subprocess.run([compiler, '-std=c++17', '-I', str(ROOT), str(init_source), '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)


if __name__ == '__main__':
    main()
