#!/usr/bin/env python3
"""Execute production GL swap policy and MSAA consumers without a device.

The complete CopyDepthbuffer and scene-target creation functions are compiled
against counted GL/image boundaries. This verifies decisions, not GPU pixels.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include "src/renderer/DisplayPresentation.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>
template<class T> T Max(T a,T b){return std::max(a,b);}
enum {RENDERER_PRESENT_WINDOW_STATE,RENDERER_PRESENT_CONTEXT,RENDERER_PRESENT_SWAP};
static unsigned long long R_RendererMetrics_CpuClock(){return 0;}
static void R_RendererMetrics_EndPresentPhase(int,unsigned long long){}
static bool R_ImagePolicyContentMutation(){return true;}
struct Common {template<class... T> void Printf(const char*,T...){} template<class... T> void Warning(const char*,T...){} } commonObject,*common=&commonObject;
struct Cvar {
    int value=0,writes=0;bool modified=false;
    int GetInteger()const{return value;}bool IsModified()const{return modified;}
    void ClearModified(){modified=false;}void SetInteger(int v){value=v;modified=true;++writes;}
} r_swapInterval,r_multiSamples,r_glCopyTargetState;
using uint64=unsigned long long;
static bool bypass=false,contextOkay=true,swapOkay=true,setOkay=true,readOkay=true,ignoreSet=false;
static int interval=0,sampleBuffers=0,samples=0,setCalls=0,swaps=0;
static int R_GetEffectiveSwapInterval(){return bypass?0:r_swapInterval.GetInteger();}
struct Request {int swapInterval=0;} request;
static const Request* R_GetRecoverableWindowRequest(){return &request;}
struct renderModuleWindowInfo_t {int pixelWidth=1280,pixelHeight=720,uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720;};
struct {int vidWidth=1280,vidHeight=720,uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720;bool isInitialized=true;} glConfig;
static void SDL3_SyncGLConfigWindowDimensions(const renderModuleWindowInfo_t&){}
enum {RENDER_GLATTR_MULTISAMPLE_BUFFERS,RENDER_GLATTR_MULTISAMPLE_SAMPLES};
struct Services {
    bool (*GetGLAttribute)(int,int*)=+[](int a,int* out){*out=a==RENDER_GLATTR_MULTISAMPLE_BUFFERS?sampleBuffers:samples;return true;};
    bool (*GetGLSwapInterval)(int*)=+[](int* out){*out=interval;return readOkay;};
    bool (*SetGLSwapInterval)(int)=+[](int v){++setCalls;if(!ignoreSet&&setOkay)interval=v;return setOkay;};
    bool (*SwapGLWindow)()=+[]{++swaps;return swapOkay;};
    void (*RefreshNativeWindowHandles)(renderModuleWindowInfo_t*)=+[](renderModuleWindowInfo_t* out){*out={};};
} services,*s_glWindowServices=&services;
static void *s_glWindow=(void*)1,*s_glContext=(void*)1;
static const char* R_GLVideoError(){return "injected";}
static bool SDL3_EnsureGLContextCurrent(const char*){return contextOkay;}
using GLenum=unsigned;using GLuint=unsigned;using GLint=int;using GLboolean=bool;
// no VR frame: the window is the default framebuffer (RenderTexture.h); 0x0405 is GL_BACK.
// standInSamples >= 0 puts a stand-in with that many samples in the window's place.
static int standInSamples=-1;
static bool windowCarries=true;
static GLuint R_DefaultFramebufferHandle(){return standInSamples>=0?7u:0u;}
int R_DefaultRenderTargetSamples(){return standInSamples>1?standInSamples:0;}
bool R_WindowCarriesMultisampling(){return windowCarries;}
static GLenum R_DefaultColorBuffer(){return 0x0405u;}
GL_CONSTANTS
enum {TT_2D,TT_CUBIC,FMT_DEPTH,FMT_DEPTH_STENCIL,FMT_RGBA16F,TF_LINEAR,TF_NEAREST,TR_CLAMP,TD_DEFAULT,TD_DEPTH};
static bool GLEW_EXT_framebuffer_blit=true,GLEW_ARB_framebuffer_object=false,GLEW_VERSION_3_0=false,GLEW_EXT_framebuffer_object=false;
static GLint drawBinding=0,readBinding=0;
// FPS-08: what a copy asks OpenGL for before it changes it (0x0405 is GL_BACK)
static GLint readBufferState=0x0405,drawBufferState=0x0405;
static int stateQueries=0;
static GLuint r_copyDepthbufferFbo=0;
static int blits=0,copies=0,readPixels=0;
template<class T> struct idList {std::vector<T> values;void SetNum(int n,bool){values.resize(n);}T* Ptr(){return values.data();}};
struct idImageOpts {int textureType=TT_2D,format=FMT_DEPTH,width=32,height=32,numLevels=1,numMSAASamples=0;bool isPersistant=false;};
struct idImage {
    idImageOpts opts;GLenum internalFormat=GL_DEPTH_COMPONENT24,dataFormat=GL_DEPTH_COMPONENT,dataType=GL_FLOAT;GLuint texnum=1;
    const idImageOpts& GetOpts()const{return opts;}
    bool IsFileBacked()const{return false;}
    bool CopyDepthbuffer(int,int,int,int,int=0);
};
// FPS-05: CopyDepthbuffer counts a resolve out of a multisampled target.
static int msaaResolves=0;
void R_CountMSAAResolve(bool){++msaaResolves;}
struct RenderTexture {
    idImage* depth=nullptr;int width=1280,height=720,handle=9,colors=0;
    idImage* GetDepthImage(){return depth;}int GetDeviceHandle(){return handle;}int GetNumColorImages()const{return colors;}
    int GetWidth(){return width;}int GetHeight(){return height;}
};
using idRenderTexture=RenderTexture;
struct {RenderTexture* renderTexture=nullptr;int c_copyFrameBuffer=0;int c_copyDepthBuffer=0;
    bool copyKeepsBoundTarget=false;RenderTexture* copyBoundTarget=nullptr;} backEnd;
template<class... T> void R_BindTextureForDirectAccess(T...){}
template<class... T> void R_AllocateCopyTextureStorage(T...){}
template<class... T> void glTexParameterf(T...){}
static void glBindFramebuffer(GLenum target,GLuint buffer){if(target==GL_READ_FRAMEBUFFER)readBinding=buffer;else drawBinding=buffer;}
static void glBindFramebufferEXT(GLenum,GLuint buffer){readBinding=drawBinding=buffer;}
static void glReadBuffer(GLenum buffer){readBufferState=(GLint)buffer;}
static void glDrawBuffer(GLenum buffer){drawBufferState=(GLint)buffer;}
template<class... T> void glFramebufferTexture2D(T...){}
template<class... T> void glDisable(T...){}
template<class... T> void glEnable(T...){}
template<class... T> void glScissor(T...){}
template<class... T> void glTexImage2D(T...){}
template<class... T> void glTexSubImage2D(T...){}
template<class... T> void glBlitFramebuffer(T...){++blits;}
template<class... T> void glCopyTexImage2D(T...){++copies;}
template<class... T> void glCopyTexSubImage2D(T...){++copies;}
template<class... T> void glReadPixels(T...){++readPixels;}
static void glGetIntegerv(GLenum name,GLint* out){
    *out=0;
    if(name==GL_SAMPLE_BUFFERS)*out=drawBinding?1:sampleBuffers;
    if(name==GL_SAMPLES)*out=drawBinding?16:samples;
    if(name==GL_DRAW_FRAMEBUFFER_BINDING || name==GL_FRAMEBUFFER_BINDING_EXT)*out=drawBinding;
    if(name==GL_READ_FRAMEBUFFER_BINDING)*out=readBinding;
    if(name==GL_READ_BUFFER)*out=readBufferState;
    if(name==GL_DRAW_BUFFER)*out=drawBufferState;
    if(name==GL_DRAW_FRAMEBUFFER_BINDING || name==GL_READ_FRAMEBUFFER_BINDING || name==GL_READ_BUFFER || name==GL_DRAW_BUFFER)++stateQueries;
}
static void glGenFramebuffers(int,GLuint* out){*out=7;}
static bool glIsEnabled(GLenum){return false;}
struct viewDef_t {struct{int x2=1279,y2=719;} viewport;};
static bool scaled=false,temporal=false,effects=false,modern=false;
static bool RB_ComputeScaledSceneSize(const viewDef_t*,int& w,int& h){w=640;h=360;return scaled;}
static bool R_TemporalPresentation_TemporalAARequested(){return temporal;}
static bool R_TemporalPresentation_ScreenSpaceEffectsRequested(){return effects;}
static bool R_ModernGLExecutor_ModernVisibleRequestedForPost(){return modern;}
static bool textureMSAA=true;
bool R_GLMultisampleTexturesAvailable(){return textureMSAA;}
static idImage sceneColor,sceneDepth,*rbSceneColorImage=nullptr,*rbSceneDepthStencilImage=nullptr;
static RenderTexture sceneTexture,*rbSceneRenderTexture=nullptr;
static int rbSceneRenderTextureSamples=0,rbBackendTemporalHistoryFrame=0,rbSceneRenderTargetClearedFrame=0;
static bool rbBackendTemporalHistoryValid=true;
struct Images {
    idImage* ScratchImage(const char* name,const idImageOpts* opts,int,int,int){
        idImage* image=std::strcmp(name,"_hdrSceneColor")==0?&sceneColor:&sceneDepth;image->opts=*opts;return image;
    }
} images,*globalImages=&images;
struct Renderer {
    int glContextGeneration=0;
    void DestroyRenderTexture(RenderTexture*){}
    RenderTexture* CreateRenderTexture(idImage* color,idImage* depth){sceneTexture.depth=depth;sceneTexture.width=color->opts.width;sceneTexture.height=color->opts.height;return &sceneTexture;}
} tr;
'''

CASES = r'''
static renderDisplayPresentation_t snapshot(){renderDisplayPresentation_t s{};R_GetDisplayPresentation(&s);return s;}
static void strict(int desired,int legacy){
    r_swapInterval.value=legacy;request.swapInterval=desired;
    assert(SDL3_ApplyStrictSwapInterval());r_swapInterval.ClearModified();
    assert(s_strictSwapIntervalActive && interval==desired);
}
int main(){
    R_DisplayPresentationBeginDevice();R_DisplayPresentationReady();
    strict(-1,1);auto before=snapshot();GLimp_SwapBuffers();assert(interval==-1 && snapshot().presentedSequence==before.presentedSequence+1);
    // Both loading bypass entry and exit mark the CVar dirty without editing it.
    for(bool loading:{true,false,true,false}){bypass=loading;r_swapInterval.modified=true;GLimp_SwapBuffers();assert(interval==-1 && s_strictSwapIntervalActive && !r_swapInterval.writes);}
    strict(1,0);bypass=true;r_swapInterval.modified=true;GLimp_SwapBuffers();assert(interval==1);
    // A raw legacy preference edit releases strict ownership, including an
    // edit whose dirty flag has already been consumed by another legacy user.
    r_swapInterval.SetInteger(-1);r_swapInterval.ClearModified();GLimp_SwapBuffers();assert(!s_strictSwapIntervalActive && interval==0);
    bypass=false;r_swapInterval.modified=true;GLimp_SwapBuffers();assert(interval==-1);
    // A failed strict set/readback cannot install new ownership.
    s_strictSwapIntervalActive=false;setOkay=false;request.swapInterval=1;
    assert(!SDL3_ApplyStrictSwapInterval() && !s_strictSwapIntervalActive);setOkay=true;
    ignoreSet=true;interval=0;assert(!SDL3_ApplyStrictSwapInterval() && !s_strictSwapIntervalActive);ignoreSet=false;
    readOkay=false;assert(!SDL3_ApplyStrictSwapInterval() && !s_strictSwapIntervalActive);readOkay=true;
    strict(0,1);assert(SDL3_RequestedSwapInterval()==0 && r_swapInterval.value==1);
    // Context sample observation, not the unapplied archived value, controls
    // default framebuffer depth copying and scene target multisampling.
    idImage depthCopy;viewDef_t view;
    sampleBuffers=1;samples=4;r_multiSamples.value=0;SDL3_RecordDisplayParameters();
    blits=copies=readPixels=0;assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(blits==1 && copies==0 && readPixels==0);
    assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==4 && sceneDepth.opts.numMSAASamples==4);
    // A context without multisample textures (macOS's GL 2.1 legacy context,
    // whose window carries MSAA) keeps the target single-sample, so the images
    // equal the request and are not reallocated every frame (#257).
    textureMSAA=false;assert(RB_EnsureSceneRenderTexture(&view));
    assert(sceneColor.opts.numMSAASamples==0 && sceneDepth.opts.numMSAASamples==0);textureMSAA=true;
    sampleBuffers=0;samples=8;r_multiSamples.value=8;SDL3_RecordDisplayParameters();
    blits=copies=0;assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(copies==1 && blits==0);
    assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
    // An explicit render texture keeps its own actual source sample count.
    idImage sourceDepth;sourceDepth.opts.numMSAASamples=4;RenderTexture source;source.depth=&sourceDepth;backEnd.renderTexture=&source;
    GLEW_EXT_framebuffer_blit=false;blits=copies=readPixels=0;
    assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(readPixels==1 && copies==0);
    sourceDepth.opts.numMSAASamples=0;sampleBuffers=1;samples=4;SDL3_RecordDisplayParameters();
    // r_debugRenderToTexture 1 reports the depth copies a frame made: each successful copy counts once.
    const int countedDepthCopies=backEnd.c_copyDepthBuffer;
    blits=copies=readPixels=0;assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(copies==1 && readPixels==0);
    assert(backEnd.c_copyDepthBuffer==countedDepthCopies+1);
    backEnd.renderTexture=nullptr;GLEW_EXT_framebuffer_blit=true;
    for(int suppression=0;suppression<4;++suppression){
        scaled=suppression==0;temporal=suppression==1;effects=suppression==2;modern=suppression==3;
        assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
    }
    scaled=temporal=effects=modern=false;
#if defined(USE_SDL3)
    R_DisplayPresentationParameters(4,1,-1,0);blits=copies=readPixels=0;
    assert(!depthCopy.CopyDepthbuffer(0,0,32,32));assert(!blits&&!copies&&!readPixels);
    assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
    {
        // A window that carries no samples leaves the count to the setting, which the
        // views' render targets follow; the setting itself is never written.
        const int archived=r_multiSamples.value;
        windowCarries=false;r_multiSamples.value=4;R_DisplayPresentationParameters(0,1,-1,RDP_PARAMETER_SAMPLES);
        assert(R_DefaultFramebufferSamples()==0 && R_ViewFramebufferSamples()==4);
        assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==4);
        r_multiSamples.value=1;assert(R_ViewFramebufferSamples()==0);
        // a VR stand-in stays single-sample whatever the setting
        r_multiSamples.value=4;standInSamples=0;assert(R_ViewFramebufferSamples()==0);standInSamples=-1;
        // unknown window samples still authorize nothing
        R_DisplayPresentationParameters(0,1,-1,0);assert(R_ViewFramebufferSamples()==-1);
        assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
        // a window that does carry samples keeps its own count, not the setting's
        windowCarries=true;R_DisplayPresentationParameters(2,1,-1,RDP_PARAMETER_SAMPLES);assert(R_ViewFramebufferSamples()==2);
        r_multiSamples.value=archived;R_DisplayPresentationParameters(4,1,-1,0);
    }
    SDL3_RecordDisplayParameters();R_DisplayPresentationShutdown();
    assert(!depthCopy.CopyDepthbuffer(0,0,32,32));assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
#else
    // Native GL has no presentation publisher: select the actual default
    // framebuffer while querying, then restore core/EXT binding semantics.
    samples=4;sampleBuffers=1;drawBinding=9;readBinding=11;GLEW_ARB_framebuffer_object=true;
    assert(R_DefaultFramebufferSamples()==4 && drawBinding==9 && readBinding==11);
    assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==4 && drawBinding==9 && readBinding==11);
    GLEW_ARB_framebuffer_object=false;GLEW_EXT_framebuffer_object=true;readBinding=drawBinding=9;
    assert(R_DefaultFramebufferSamples()==4 && drawBinding==9 && readBinding==9);
    sampleBuffers=0;assert(R_DefaultFramebufferSamples()==0 && drawBinding==9);
    glConfig.isInitialized=false;assert(R_DefaultFramebufferSamples()==-1);
    assert(!depthCopy.CopyDepthbuffer(0,0,32,32));assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
#endif
    assert(!r_multiSamples.writes);
    // A stand-in for the window reports its own samples, whatever the window has:
    // a VR eye is single-sample, a window view multisampled.
    standInSamples=0;assert(R_DefaultFramebufferSamples()==0 && R_ViewFramebufferSamples()==0);
    standInSamples=8;assert(R_DefaultFramebufferSamples()==8 && R_ViewFramebufferSamples()==8);
    standInSamples=-1;
    assert(!r_multiSamples.writes);
    // FPS-08: a copy takes the state it has to put back from the target the back end has bound instead of
    // asking OpenGL. A target is compared with OpenGL the first time it is seen; after that a copy asks nothing.
    {
        idImage copyDepth,boundDepth;RenderTexture bound;bound.depth=&boundDepth;bound.handle=21;bound.colors=1;
        glConfig.isInitialized=true;GLEW_EXT_framebuffer_blit=true;backEnd.renderTexture=&bound;
        const GLint first=(GLint)GL_COLOR_ATTACHMENT0;	// what a target with a colour image draws to and reads from
        drawBinding=readBinding=21;readBufferState=drawBufferState=first;
        r_glCopyTargetState.value=1;
        stateQueries=0;blits=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(blits==1 && stateQueries==4);
        assert(drawBinding==21 && readBinding==21 && readBufferState==first && drawBufferState==first);
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==0);
        assert(drawBinding==21 && readBinding==21 && readBufferState==first && drawBufferState==first);
        // a target without a colour image has none selected
        RenderTexture depthOnly;depthOnly.depth=&boundDepth;depthOnly.handle=24;backEnd.renderTexture=&depthOnly;
        drawBinding=readBinding=24;readBufferState=drawBufferState=(GLint)GL_NONE;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==4);
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==0 && readBufferState==(GLint)GL_NONE && drawBufferState==(GLint)GL_NONE);
        backEnd.renderTexture=&bound;drawBinding=readBinding=21;readBufferState=drawBufferState=first;
        // A caller that reads a source it has not bound names the target that stays bound.
        RenderTexture other;other.depth=&boundDepth;other.handle=22;
        backEnd.renderTexture=&other;backEnd.copyBoundTarget=&bound;backEnd.copyKeepsBoundTarget=true;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==0 && drawBinding==21 && readBinding==21);
        backEnd.copyKeepsBoundTarget=false;backEnd.copyBoundTarget=nullptr;
        // A target OpenGL disagrees about is asked from then on, and what OpenGL answered is what is put back.
        drawBinding=21;readBinding=23;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==4 && drawBinding==21 && readBinding==23);
        drawBinding=readBinding=22;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==4 && drawBinding==22 && readBinding==22);
        // The trusted target is still not asked about, a new context forgets every target, and 0 always asks.
        backEnd.renderTexture=&bound;drawBinding=readBinding=21;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==0);
        ++tr.glContextGeneration;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==4);
        r_glCopyTargetState.value=0;
        stateQueries=0;assert(copyDepth.CopyDepthbuffer(0,0,32,32));assert(stateQueries==4 && drawBinding==21 && readBinding==21);
        backEnd.renderTexture=nullptr;
    }
}
'''

# FPS-08: the OpenGL state query shadow (GLQueryShadow.h, tr_backend.cpp) against a counted driver.
# The driver below keeps the same state the shadow tracks, with OpenGL's own rules for the
# attribute stack, clamped viewports and rejected scissor sizes, and counts every query it answers.
SHADOW_DRIVER = r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
using GLenum=unsigned;using GLuint=unsigned;using GLint=int;using GLsizei=int;using GLboolean=unsigned char;
using GLbitfield=unsigned;using GLclampf=float;using GLfloat=float;
#define ID_INLINE inline
template<class T> T Max(T a,T b){return std::max(a,b);}
enum : unsigned {GL_FALSE=0,GL_TRUE=1,GL_POLYGON_BIT=0x8,GL_DEPTH_BUFFER_BIT=0x100,GL_STENCIL_BUFFER_BIT=0x400,GL_VIEWPORT_BIT=0x800,
    GL_ENABLE_BIT=0x2000,GL_COLOR_BUFFER_BIT=0x4000,GL_SCISSOR_BIT=0x80000,GL_ALL_ATTRIB_BITS=0xFFFFFFFFu,
    GL_CULL_FACE=0x0B44,GL_DEPTH_TEST=0x0B71,GL_STENCIL_TEST=0x0B90,GL_VIEWPORT=0x0BA2,GL_BLEND=0x0BE2,GL_SCISSOR_BOX=0x0C10,
    GL_SCISSOR_TEST=0x0C11,GL_COLOR_CLEAR_VALUE=0x0C22,GL_MAX_VIEWPORT_DIMS=0x0D3A,GL_TEXTURE_2D=0x0DE1,GL_ARRAY_BUFFER_BINDING_ARB=0x8894};
enum {CVAR_RENDERER=1,CVAR_INTEGER=2};
struct idCmdSystem {template<int,int> static void ArgCompletion_Integer(){}};
struct idCVar {
    int value;
    template<class... T> idCVar(const char*,const char* v,T...):value(std::atoi(v)){}
    int GetInteger()const{return value;}
};
static int warnings=0,prints=0;
struct Common {template<class... T> void Printf(const char*,T...){++prints;} template<class... T> void Warning(const char*,T...){++warnings;}} commonObject,*common=&commonObject;
struct {int glContextGeneration=0;} tr;
static bool cacheKnows=false;static unsigned cacheBuffer=0;static int cacheInvalidations=0;
struct idVertexCache {
    static bool BoundArrayBuffer(unsigned& vbo){if(!cacheKnows)return false;vbo=cacheBuffer;return true;}
    static void InvalidateBufferBindings(){cacheKnows=false;++cacheInvalidations;}
};
// the driver
struct DriverState {bool blend=false,scissorTest=false,stencil=false,cull=false,depth=false,texture=false;int scissor[4]={0,0,640,480},viewport[4]={0,0,640,480};float clear[4]={0,0,0,0};};
static DriverState driver;static DriverState driverStack[64];static GLbitfield driverMask[64];static int driverDepth=0;
static unsigned arrayBuffer=0;static int queries=0;
static bool* driverEnable(GLenum cap){switch(cap){case GL_BLEND:return &driver.blend;case GL_SCISSOR_TEST:return &driver.scissorTest;
    case GL_STENCIL_TEST:return &driver.stencil;case GL_CULL_FACE:return &driver.cull;case GL_DEPTH_TEST:return &driver.depth;default:return &driver.texture;}}
static void glEnable(GLenum cap){*driverEnable(cap)=true;}
static void glDisable(GLenum cap){*driverEnable(cap)=false;}
static GLboolean glIsEnabled(GLenum cap){++queries;return *driverEnable(cap)?GL_TRUE:GL_FALSE;}
static void glScissor(GLint x,GLint y,GLsizei w,GLsizei h){if(w<0||h<0)return;driver.scissor[0]=x;driver.scissor[1]=y;driver.scissor[2]=w;driver.scissor[3]=h;}
static void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){if(w<0||h<0)return;driver.viewport[0]=x;driver.viewport[1]=y;driver.viewport[2]=std::min(w,4096);driver.viewport[3]=std::min(h,2048);}
static void glClearColor(GLclampf r,GLclampf g,GLclampf b,GLclampf a){const float in[4]={r,g,b,a};for(int i=0;i<4;++i)driver.clear[i]=std::min(1.0f,std::max(0.0f,in[i]));}
static void glPushAttrib(GLbitfield mask){assert(driverDepth<64);driverMask[driverDepth]=mask;driverStack[driverDepth++]=driver;}
static void glPopAttrib(){assert(driverDepth>0);const DriverState& s=driverStack[--driverDepth];const GLbitfield m=driverMask[driverDepth];
    if(m&(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT))driver.blend=s.blend;
    if(m&(GL_ENABLE_BIT|GL_SCISSOR_BIT))driver.scissorTest=s.scissorTest;
    if(m&(GL_ENABLE_BIT|GL_STENCIL_BUFFER_BIT))driver.stencil=s.stencil;
    if(m&(GL_ENABLE_BIT|GL_POLYGON_BIT))driver.cull=s.cull;
    if(m&(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT))driver.depth=s.depth;
    if(m&GL_ENABLE_BIT)driver.texture=s.texture;
    if(m&GL_SCISSOR_BIT)std::memcpy(driver.scissor,s.scissor,sizeof(s.scissor));
    if(m&GL_VIEWPORT_BIT)std::memcpy(driver.viewport,s.viewport,sizeof(s.viewport));
    if(m&GL_COLOR_BUFFER_BIT)std::memcpy(driver.clear,s.clear,sizeof(s.clear));}
static void glGetIntegerv(GLenum name,GLint* out){
    if(name==GL_MAX_VIEWPORT_DIMS){out[0]=4096;out[1]=2048;return;}	// asked once a context, not a state query of the frame
    ++queries;
    if(name==GL_SCISSOR_BOX)std::memcpy(out,driver.scissor,sizeof(driver.scissor));
    else if(name==GL_VIEWPORT)std::memcpy(out,driver.viewport,sizeof(driver.viewport));
    else if(name==GL_ARRAY_BUFFER_BINDING_ARB)out[0]=(GLint)arrayBuffer;
    else out[0]=77;
}
static void glGetFloatv(GLenum name,GLfloat* out){++queries;if(name==GL_COLOR_CLEAR_VALUE)std::memcpy(out,driver.clear,sizeof(driver.clear));else out[0]=7.0f;}
static void glGetBooleanv(GLenum,GLboolean* out){++queries;out[0]=GL_TRUE;}
static GLenum glGetError(){++queries;return 0;}
'''

SHADOW_CASES = r'''
static bool box(GLenum name,int a,int b,int c,int d){GLint v[4]={-1,-1,-1,-1};glGetIntegerv(name,v);return v[0]==a&&v[1]==b&&v[2]==c&&v[3]==d;}
static bool clear(float r,float g,float b,float a){GLfloat v[4]={-1,-1,-1,-1};glGetFloatv(GL_COLOR_CLEAR_VALUE,v);return v[0]==r&&v[1]==g&&v[2]==b&&v[3]==a;}
int main(){
    // A new context: nothing is remembered, each value is asked for once and then known.
    assert(r_glStateQueries.GetInteger()==1);
    queries=0;assert(glIsEnabled(GL_BLEND)==GL_FALSE && queries==1);assert(glIsEnabled(GL_BLEND)==GL_FALSE && queries==1);
    assert(box(GL_SCISSOR_BOX,0,0,640,480) && queries==2);assert(box(GL_SCISSOR_BOX,0,0,640,480) && queries==2);
    assert(box(GL_VIEWPORT,0,0,640,480) && queries==3);assert(clear(0,0,0,0) && queries==4);assert(clear(0,0,0,0) && queries==4);
    // What the renderer sets is answered without the driver.
    queries=0;
    glEnable(GL_SCISSOR_TEST);glEnable(GL_STENCIL_TEST);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glDisable(GL_STENCIL_TEST);
    assert(glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE && glIsEnabled(GL_STENCIL_TEST)==GL_FALSE && glIsEnabled(GL_CULL_FACE)==GL_TRUE
        && glIsEnabled(GL_DEPTH_TEST)==GL_TRUE && glIsEnabled(GL_BLEND)==GL_TRUE);
    glScissor(10,20,300,200);glViewport(5,6,1280,720);glClearColor(0.25f,0.5f,0.75f,1.0f);
    assert(box(GL_SCISSOR_BOX,10,20,300,200) && box(GL_VIEWPORT,5,6,1280,720) && clear(0.25f,0.5f,0.75f,1.0f));
    assert(queries==0 && driver.scissorTest && driver.blend && !driver.stencil && driver.viewport[2]==1280);
    // An attribute group that was pushed comes back with its pop; a group that was not pushed stays as set.
    glPushAttrib(GL_ENABLE_BIT|GL_SCISSOR_BIT);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glScissor(0,0,8,8);glViewport(0,0,64,64);glClearColor(1,1,1,1);
    assert(glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE && box(GL_SCISSOR_BOX,0,0,8,8));
    glPopAttrib();
    assert(glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE && glIsEnabled(GL_BLEND)==GL_TRUE && box(GL_SCISSOR_BOX,10,20,300,200));
    assert(box(GL_VIEWPORT,0,0,64,64) && clear(1,1,1,1) && queries==0);
    glPushAttrib(GL_VIEWPORT_BIT|GL_COLOR_BUFFER_BIT);glViewport(1,2,3,4);glClearColor(0,0,0,0);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glScissor(7,7,7,7);glPopAttrib();
    assert(box(GL_VIEWPORT,0,0,64,64) && clear(1,1,1,1) && glIsEnabled(GL_BLEND)==GL_TRUE && glIsEnabled(GL_CULL_FACE)==GL_FALSE && queries==0);
    assert(box(GL_SCISSOR_BOX,7,7,7,7) && driver.scissor[0]==7 && queries==0);glScissor(10,20,300,200);
    glPushAttrib(GL_STENCIL_BUFFER_BIT);glPushAttrib(GL_POLYGON_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);glEnable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
    glPopAttrib();assert(glIsEnabled(GL_CULL_FACE)==GL_FALSE && glIsEnabled(GL_DEPTH_TEST)==GL_TRUE && glIsEnabled(GL_STENCIL_TEST)==GL_TRUE);
    glPopAttrib();assert(glIsEnabled(GL_STENCIL_TEST)==GL_FALSE && queries==0);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_BLEND);glScissor(9,9,9,9);glViewport(9,9,9,9);glClearColor(0,1,0,1);glPopAttrib();
    assert(glIsEnabled(GL_BLEND)==GL_TRUE && box(GL_SCISSOR_BOX,10,20,300,200) && box(GL_VIEWPORT,0,0,64,64) && clear(1,1,1,1) && queries==0);
    // A value the driver may not keep as given is not remembered: a rejected size, a clamped one, a colour out of range.
    glScissor(1,1,-5,5);assert(box(GL_SCISSOR_BOX,10,20,300,200) && queries==1);
    glViewport(0,0,8192,8192);assert(box(GL_VIEWPORT,0,0,4096,2048) && queries==2);assert(box(GL_VIEWPORT,0,0,4096,2048) && queries==2);
    glViewport(0,0,-1,4);assert(box(GL_VIEWPORT,0,0,4096,2048) && queries==3);
    glClearColor(2.0f,0,0,1);assert(clear(1,0,0,1) && queries==4);assert(clear(1,0,0,1) && queries==4);
    // Everything else is passed on, every time.
    queries=0;glEnable(GL_TEXTURE_2D);assert(glIsEnabled(GL_TEXTURE_2D)==GL_TRUE && glIsEnabled(GL_TEXTURE_2D)==GL_TRUE && queries==2);
    GLint other[4];glGetIntegerv(0x1234,other);GLboolean flag;glGetBooleanv(0x1234,&flag);assert(other[0]==77 && glGetError()==0 && queries==5);
    // The array buffer is the vertex cache's own record, while the cache has one.
    queries=0;arrayBuffer=31;glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,other);assert(other[0]==31 && queries==1);
    cacheKnows=true;cacheBuffer=31;glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,other);assert(other[0]==31 && queries==1);
    // More pushes than are kept: the pops that cannot be matched forget, they do not guess.
    glEnable(GL_BLEND);
    for(int i=0;i<20;++i)glPushAttrib(GL_ENABLE_BIT);
    glDisable(GL_BLEND);
    for(int i=0;i<4;++i)glPopAttrib();
    queries=0;assert(glIsEnabled(GL_BLEND)==GL_TRUE && queries==1);
    for(int i=0;i<16;++i)glPopAttrib();
    assert(glIsEnabled(GL_BLEND)==GL_TRUE && driverDepth==0);
    // 2 answers, asks as well and reports a difference: state changed where the wrappers cannot see it.
    r_glStateQueries.value=2;warnings=0;
    glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);
    queries=0;assert(glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE && box(GL_SCISSOR_BOX,1,2,3,4) && queries==2 && warnings==0);
    driver.scissorTest=false;driver.scissor[0]=50;driver.viewport[0]=3;driver.clear[0]=0.5f;arrayBuffer=32;
    assert(glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE && box(GL_SCISSOR_BOX,50,2,3,4) && warnings==2);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING_ARB,other);assert(other[0]==32 && cacheInvalidations==1);
    r_glStateQueries.value=1;queries=0;
    assert(glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE && box(GL_SCISSOR_BOX,50,2,3,4) && queries==0);	// corrected, and known again
    // 0 asks the driver for everything, as before.
    r_glStateQueries.value=0;queries=0;glEnable(GL_BLEND);
    assert(glIsEnabled(GL_BLEND)==GL_TRUE && box(GL_SCISSOR_BOX,50,2,3,4) && queries==2);
    // A new context starts from its own defaults.
    r_glStateQueries.value=1;driver=DriverState();++tr.glContextGeneration;queries=0;
    assert(glIsEnabled(GL_BLEND)==GL_FALSE && box(GL_SCISSOR_BOX,0,0,640,480) && queries==2);
    // The report counts what was answered and what reached the driver.
    r_glStateQueryReport.value=1;prints=0;R_GLShadow_EndFrame();assert(prints>=1);
    r_glStateQueryReport.value=0;
    return 0;
}
'''


def main():
    gl = (ROOT / 'src/renderer/OpenGL/gl_ContextSDL3.cpp').read_text(encoding='utf-8')
    image = (ROOT / 'src/renderer/Image_load.cpp').read_text(encoding='utf-8')
    draw = (ROOT / 'src/renderer/draw_common.cpp').read_text(encoding='utf-8')
    samples = (ROOT / 'src/renderer/OpenGL/FramebufferSamples.h').read_text(encoding='utf-8')
    depth = function_body(image, 'bool idImage::CopyDepthbuffer(')
    # the state a copy puts back (FPS-08): compiled with the copy, from its type to its report
    copy_state = image[image.index('struct glCopySavedState_t {'):image.index('// r_debugRenderToTexture: how the copies')]
    assert 'glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING' not in depth and 'glIsEnabled( GL_SCISSOR_TEST )' not in depth
    for call in ('R_GLCopy_SaveTargetState( saved, true );', 'R_GLCopy_DisableScissor( saved );',
                 'R_GLCopy_RestoreScissor( saved );', 'R_GLCopy_RestoreTargetState( saved, true );'):
        assert call in depth, call
    scene = function_body(draw, 'static bool RB_EnsureSceneRenderTexture(')
    constants = 'enum : unsigned {' + ','.join(sorted(set(re.findall(r'\bGL_[A-Z0-9_]+', depth + samples + copy_state)))) + '};'
    source = SUPPORT.replace('GL_CONSTANTS', constants)
    source += gl[gl.index('static bool s_strictSwapIntervalActive'):gl.index('static void SDL3_RecordDisplayParameters')]
    for signature in ('static void SDL3_RecordDisplayParameters()', 'static bool SDL3_ApplyStrictSwapInterval()', 'static bool SDL3_ApplySwapInterval(void)', 'void GLimp_SwapBuffers(void)'):
        source += function_body(gl, signature) + '\n'
    source += '#include "src/renderer/OpenGL/FramebufferSamples.h"\n' + copy_state + '\n' + depth + '\n' + scene + '\n' + CASES
    for signature in ('bool GLimp_Init(', 'void GLimp_Shutdown('):
        body = function_body(gl, signature)
        assert 's_strictSwapIntervalActive = false;' in body
    # The renderer's scene target starts every frame from the window's r_clear
    # color: a 3D view clears only depth and stencil, and pixels no surface
    # covers otherwise carried the last composite into bloom again (frozen
    # render-demo playback brightened frame by frame). The first view of the
    # frame clears it, before an imported portal-sky backdrop draws into it.
    backend = (ROOT / 'src/renderer/tr_backend.cpp').read_text(encoding='utf-8')
    assert 'RB_FrameClearColor( c )' in function_body(backend, 'static void\tRB_SetBuffer(')
    frame_clear = function_body(draw, 'static void RB_BeginSceneRenderTargetFrame(')
    assert 'rbSceneRenderTargetClearedFrame == backEnd.frameCount' in frame_clear
    assert 'RB_FrameClearColor( color )' in frame_clear and 'glClear( GL_COLOR_BUFFER_BIT );' in frame_clear
    assert 'rbSceneRenderTargetClearedFrame = -1;' in scene
    view = function_body(draw, 'void\tRB_STD_DrawView( void )')
    assert view.index('backEnd.renderTexture->MakeCurrent();') < view.index('RB_BeginSceneRenderTargetFrame();') \
        < view.index('RB_DrawPortalSkyBackdrop( portalSkyBackdrop );')
    # The state query shadow: the production wrappers and macros over the counted driver above. Every
    # source of the OpenGL module has to see the macros, so the header comes with the entry points.
    begin = backend.index('static idCVar r_glStateQueries(')
    shadow = backend[begin:backend.index('#endif /* OPENQ4_RENDERER_GL_MODULE */', begin)]
    assert '(glIsEnabled)( cap )' in shadow and '(glGetIntegerv)( pname, data )' in shadow
    swap = function_body(backend, 'const void\tRB_SwapBuffers(')
    assert 'R_GLShadow_EndFrame();' in swap
    # a headset's runtime works in the context between frames: nothing is remembered across its frame
    assert swap.index('if ( !RB_VR_PresentFrame() ) {') < swap.index('R_GLShadow_Forget();')
    qgl = (ROOT / 'src/renderer/qgl.h').read_text(encoding='utf-8')
    assert qgl.index('glew.h') < qgl.index('#include "GLQueryShadow.h"')
    header = (ROOT / 'src/renderer/GLQueryShadow.h').read_text(encoding='utf-8')
    for name in ('glEnable', 'glDisable', 'glIsEnabled', 'glScissor', 'glViewport', 'glClearColor', 'glPushAttrib',
                 'glPopAttrib', 'glGetIntegerv', 'glGetFloatv', 'glGetBooleanv', 'glGetError'):
        assert re.search(r'#undef %s\n#define %s\(' % (name, name), header), name
    assert header.count('#if defined( OPENQ4_RENDERER_GL_MODULE )') == 1
    # no source of the module reaches these entry points around the header
    for path in sorted((ROOT / 'src/renderer').rglob('*.cpp')) + sorted((ROOT / 'src/renderer').rglob('*.h')):
        text = path.read_text(encoding='utf-8', errors='replace')
        assert 'glew.h' not in text or path.name in ('qgl.h',) or 'GLES' in path.parts or 'external' in path.parts, path
    shadow_source = SHADOW_DRIVER + '#include "src/renderer/GLQueryShadow.h"\n' + shadow + SHADOW_CASES
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='gl-display-policy-', dir=ROOT / '.tmp') as directory:
        temp = Path(directory)
        cpp = temp / 'policy.cpp'
        exe = temp / ('policy.exe' if os.name == 'nt' else 'policy')
        cpp.write_text(source, encoding='utf-8')
        for defines in (['-DUSE_SDL3'], []):
            subprocess.run([compiler, '-std=c++20', *defines, '-I', str(ROOT), str(cpp), str(ROOT / 'src/renderer/DisplayPresentation.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        shadow_cpp = temp / 'shadow.cpp'
        shadow_exe = temp / ('shadow.exe' if os.name == 'nt' else 'shadow')
        shadow_cpp.write_text(shadow_source, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-DOPENQ4_RENDERER_GL_MODULE', '-I', str(ROOT), str(shadow_cpp), '-o', str(shadow_exe)], check=True)
        subprocess.run([str(shadow_exe)], check=True)
    print('GL display policy: strict interval survives bypass, legacy value edits release, actual context/texture samples govern depth copy and scene targets, copies and state queries are answered without the driver; passed')


if __name__ == '__main__':
    main()
