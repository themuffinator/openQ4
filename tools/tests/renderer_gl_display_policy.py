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
struct Common {template<class... T> void Printf(const char*,T...){} template<class... T> void Warning(const char*,T...){} } commonObject,*common=&commonObject;
struct Cvar {
    int value=0,writes=0;bool modified=false;
    int GetInteger()const{return value;}bool IsModified()const{return modified;}
    void ClearModified(){modified=false;}void SetInteger(int v){value=v;modified=true;++writes;}
} r_swapInterval,r_multiSamples;
static bool bypass=false,contextOkay=true,swapOkay=true,setOkay=true,readOkay=true,ignoreSet=false;
static int interval=0,sampleBuffers=0,samples=0,setCalls=0,swaps=0;
static int R_GetEffectiveSwapInterval(){return bypass?0:r_swapInterval.GetInteger();}
struct Request {int swapInterval=0;} request;
static const Request* R_GetRecoverableWindowRequest(){return &request;}
struct renderModuleWindowInfo_t {int pixelWidth=1280,pixelHeight=720,uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720;};
struct {int vidWidth=1280,vidHeight=720,uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720;bool isInitialized=true;} glConfig;
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
GL_CONSTANTS
enum {TT_2D,TT_CUBIC,FMT_DEPTH,FMT_DEPTH_STENCIL,FMT_RGBA16F,TF_LINEAR,TF_NEAREST,TR_CLAMP,TD_DEFAULT,TD_DEPTH};
static bool GLEW_EXT_framebuffer_blit=true,GLEW_ARB_framebuffer_object=false,GLEW_VERSION_3_0=false,GLEW_EXT_framebuffer_object=false;
static GLint drawBinding=0,readBinding=0;
static GLuint r_copyDepthbufferFbo=0;
static int blits=0,copies=0,readPixels=0;
template<class T> struct idList {std::vector<T> values;void SetNum(int n,bool){values.resize(n);}T* Ptr(){return values.data();}};
struct idImageOpts {int textureType=TT_2D,format=FMT_DEPTH,width=32,height=32,numLevels=1,numMSAASamples=0;bool isPersistant=false;};
struct idImage {
    idImageOpts opts;GLenum internalFormat=GL_DEPTH_COMPONENT24,dataFormat=GL_DEPTH_COMPONENT,dataType=GL_FLOAT;GLuint texnum=1;
    const idImageOpts& GetOpts()const{return opts;}
    bool CopyDepthbuffer(int,int,int,int,int=0);
};
struct RenderTexture {
    idImage* depth=nullptr;int width=1280,height=720;
    idImage* GetDepthImage(){return depth;}int GetDeviceHandle(){return 9;}
    int GetWidth(){return width;}int GetHeight(){return height;}
};
struct {RenderTexture* renderTexture=nullptr;} backEnd;
template<class... T> void R_BindTextureForDirectAccess(T...){}
template<class... T> void R_AllocateCopyTextureStorage(T...){}
template<class... T> void glTexParameterf(T...){}
static void glBindFramebuffer(GLenum target,GLuint buffer){if(target==GL_READ_FRAMEBUFFER)readBinding=buffer;else drawBinding=buffer;}
static void glBindFramebufferEXT(GLenum,GLuint buffer){readBinding=drawBinding=buffer;}
template<class... T> void glReadBuffer(T...){}
template<class... T> void glDrawBuffer(T...){}
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
}
static void glGenFramebuffers(int,GLuint* out){*out=7;}
static bool glIsEnabled(GLenum){return false;}
struct viewDef_t {struct{int x2=1279,y2=719;} viewport;};
static bool scaled=false,temporal=false,effects=false,modern=false;
static bool RB_ComputeScaledSceneSize(const viewDef_t*,int& w,int& h){w=640;h=360;return scaled;}
static bool R_TemporalPresentation_TemporalAARequested(){return temporal;}
static bool R_TemporalPresentation_ScreenSpaceEffectsRequested(){return effects;}
static bool R_ModernGLExecutor_ModernVisibleRequestedForPost(){return modern;}
static idImage sceneColor,sceneDepth,*rbSceneColorImage=nullptr,*rbSceneDepthStencilImage=nullptr;
static RenderTexture sceneTexture,*rbSceneRenderTexture=nullptr;
static int rbSceneRenderTextureSamples=0,rbBackendTemporalHistoryFrame=0;
static bool rbBackendTemporalHistoryValid=true;
struct Images {
    idImage* ScratchImage(const char* name,const idImageOpts* opts,int,int,int){
        idImage* image=std::strcmp(name,"_hdrSceneColor")==0?&sceneColor:&sceneDepth;image->opts=*opts;return image;
    }
} images,*globalImages=&images;
struct Renderer {
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
    sampleBuffers=0;samples=8;r_multiSamples.value=8;SDL3_RecordDisplayParameters();
    blits=copies=0;assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(copies==1 && blits==0);
    assert(RB_EnsureSceneRenderTexture(&view));assert(sceneColor.opts.numMSAASamples==0);
    // An explicit render texture keeps its own actual source sample count.
    idImage sourceDepth;sourceDepth.opts.numMSAASamples=4;RenderTexture source;source.depth=&sourceDepth;backEnd.renderTexture=&source;
    GLEW_EXT_framebuffer_blit=false;blits=copies=readPixels=0;
    assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(readPixels==1 && copies==0);
    sourceDepth.opts.numMSAASamples=0;sampleBuffers=1;samples=4;SDL3_RecordDisplayParameters();
    blits=copies=readPixels=0;assert(depthCopy.CopyDepthbuffer(0,0,32,32));assert(copies==1 && readPixels==0);
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
}
'''


def main():
    gl = (ROOT / 'src/renderer/OpenGL/gl_ContextSDL3.cpp').read_text(encoding='utf-8')
    image = (ROOT / 'src/renderer/Image_load.cpp').read_text(encoding='utf-8')
    draw = (ROOT / 'src/renderer/draw_common.cpp').read_text(encoding='utf-8')
    samples = (ROOT / 'src/renderer/OpenGL/FramebufferSamples.h').read_text(encoding='utf-8')
    depth = function_body(image, 'bool idImage::CopyDepthbuffer(')
    scene = function_body(draw, 'static bool RB_EnsureSceneRenderTexture(')
    constants = 'enum : unsigned {' + ','.join(sorted(set(re.findall(r'\bGL_[A-Z0-9_]+', depth + samples)))) + '};'
    source = SUPPORT.replace('GL_CONSTANTS', constants)
    source += gl[gl.index('static bool s_strictSwapIntervalActive'):gl.index('static void SDL3_RecordDisplayParameters')]
    for signature in ('static void SDL3_RecordDisplayParameters()', 'static bool SDL3_ApplyStrictSwapInterval()', 'static bool SDL3_ApplySwapInterval(void)', 'void GLimp_SwapBuffers(void)'):
        source += function_body(gl, signature) + '\n'
    source += '#include "src/renderer/OpenGL/FramebufferSamples.h"\n' + depth + '\n' + scene + '\n' + CASES
    for signature in ('bool GLimp_Init(', 'void GLimp_Shutdown('):
        body = function_body(gl, signature)
        assert 's_strictSwapIntervalActive = false;' in body
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
    print('GL display policy: strict interval survives bypass, legacy value edits release, actual context/texture samples govern depth copy and scene targets; passed')


if __name__ == '__main__':
    main()
