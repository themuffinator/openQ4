#!/usr/bin/env python3
"""Execute the actual GL debug readback code against counted framebuffer doubles."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#undef INT_MAX
using GLuint=unsigned; using GLint=int; using GLenum=unsigned; using byte=unsigned char;
enum { GL_DRAW_FRAMEBUFFER_BINDING=1, GL_READ_FRAMEBUFFER_BINDING, GL_SAMPLE_BUFFERS,
       GL_VIEWPORT, GL_PACK_ALIGNMENT, GL_READ_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER,
       GL_RGBA, GL_UNSIGNED_BYTE, GL_DEPTH_COMPONENT, GL_FLOAT,
       GL_PROJECTION, GL_MODELVIEW, GLS_DEPTHFUNC_ALWAYS, GL_STENCIL_INDEX };
constexpr unsigned GL_NO_ERROR=0;
struct idRenderTexture {
    GLuint handle; int width, height;
    GLuint GetDeviceHandle() { return handle; }
    int GetWidth() const { return width; } int GetHeight() const { return height; }
};
struct { int vidWidth=1280, vidHeight=800; } glConfig;
struct { idRenderTexture *renderTexture=nullptr; } backEnd;
idRenderTexture *standIn=nullptr;
idRenderTexture *R_GetDefaultRenderTarget() { return standIn; }
int drawFbo=0, readFbo=0, samples=0, pack=8, viewport[4]={0,0,1280,800};
int reads=0, draws=0, allocations=0, allocatedBytes=0, warnings=0, resolves=0;
int readRect[4], drawExtent[2], resolveExtent[2];
unsigned resolveResult=91, readError=0;
void BindFramebuffer(GLenum binding, GLuint object) {
    if(binding==GL_READ_FRAMEBUFFER) readFbo=object; else { assert(binding==GL_DRAW_FRAMEBUFFER); drawFbo=object; }
}
void (*glBindFramebuffer)(GLenum,GLuint)=BindFramebuffer;
void glGetIntegerv(GLenum name, GLint *value) {
    switch(name) {
    case GL_DRAW_FRAMEBUFFER_BINDING: *value=drawFbo; break;
    case GL_READ_FRAMEBUFFER_BINDING: *value=readFbo; break;
    case GL_SAMPLE_BUFFERS: *value=samples; break;
    case GL_PACK_ALIGNMENT: *value=pack; break;
    case GL_VIEWPORT: std::memcpy(value,viewport,sizeof(viewport)); break;
    default: assert(false);
    }
}
void glPixelStorei(GLenum name, GLint value) { assert(name==GL_PACK_ALIGNMENT); pack=value; }
void GL_CheckErrors() {}
GLenum glGetError() { return readError; }
void RB_WarnReadbackResolve(GLenum,const char *) { ++warnings; }
const char *va(const char *,...) { return "error"; }
GLuint RB_ResolveForReadback(GLuint source, GLenum, int width,int height) {
    assert(source==unsigned(drawFbo)); ++resolves; resolveExtent[0]=width; resolveExtent[1]=height;
    drawFbo=resolveResult; readFbo=source; return resolveResult;
}
void *R_StaticAlloc(int bytes) { assert(bytes>0); ++allocations; allocatedBytes=bytes; return std::malloc(bytes); }
void R_StaticFree(void *data) { assert(allocations>0); --allocations; std::free(data); }
void glReadPixels(int x,int y,int width,int height,GLenum format,GLenum,void *data) {
    ++reads; readRect[0]=x;readRect[1]=y;readRect[2]=width;readRect[3]=height;
    assert(pack==1 && allocatedBytes==width*height*(format==GL_STENCIL_INDEX?1:4));
    if(format==GL_DEPTH_COMPONENT) for(int i=0;i<width*height;++i) static_cast<float *>(data)[i]=0.5f;
    else std::memset(data,format==GL_STENCIL_INDEX?3:64,allocatedBytes);
}
void glDrawPixels(int width,int height,GLenum format,GLenum type,const void *data) {
    ++draws; drawExtent[0]=width;drawExtent[1]=height;
    assert(format==GL_RGBA && type==GL_UNSIGNED_BYTE && allocatedBytes==width*height*4);
    const byte *b=static_cast<const byte *>(data);
    assert((b[0]==128 && b[1]==128 && b[2]==0) || (b[0]==128 && b[1]==128 && b[2]==128 && b[3]==255));
}
struct CVar { bool GetBool() const { return true; } } r_showDepth,r_showIntensity;
struct idMath {
    static constexpr int INT_MAX = 2147483647;
    static byte Ftob(float value) { return byte(value); }
};
void glLoadIdentity() {} void glMatrixMode(GLenum) {} void GL_State(int) {}
void glPushMatrix() {} void glPopMatrix() {} void glOrtho(double,double,double,double,double,double) {}
void glRasterPos2f(float,float) {} void glColor3f(float,float,float) {} void RB_BindNullDebugImage() {}
double overdraw=-1;
struct Common {
    void Printf(const char *format,...) {
        va_list args;va_start(args,format);
        if(std::strcmp(format,"overdraw: %5.1f\n")==0) overdraw=va_arg(args,double);
        va_end(args);
    }
} commonObject;
Common *common=&commonObject;
'''
MAIN = r'''
void reset() {
    assert(allocations==0); reads=draws=resolves=warnings=0;readError=0;resolveResult=91;
    drawFbo=readFbo=0;samples=0;pack=8;backEnd.renderTexture=standIn=nullptr;
    viewport[0]=viewport[1]=0;viewport[2]=1280;viewport[3]=800;glBindFramebuffer=BindFramebuffer;
}
int main() {
    debugReadbackArea_t area;
    for(int scale:{50,75,100,125,150,200}) for(int msaa:{0,4}) {
        reset();idRenderTexture target{7,1280*scale/100,800*scale/100};
        backEnd.renderTexture=&target;drawFbo=7;readFbo=19;samples=msaa;
        viewport[2]=target.width;viewport[3]=target.height;
        assert(RB_GetDebugReadbackArea(true,area) && area.width==target.width && area.height==target.height);
        RB_ShowDepthBuffer();RB_ShowIntensity();
        assert(reads==2 && draws==2 && drawExtent[0]==target.width && drawExtent[1]==target.height);
        assert(pack==8 && readFbo==19 && drawFbo==7 && allocations==0);
        assert(resolves==(msaa?2:0));
        if(msaa) assert(resolveExtent[0]==target.width && resolveExtent[1]==target.height);
        viewport[0]=11;viewport[1]=17;viewport[2]=319;viewport[3]=211;
        RB_ShowDepthBuffer();assert(readRect[0]==11 && readRect[1]==17 && readRect[2]==319 && readRect[3]==211);
        assert(RB_GetDebugReadbackArea(false,area) && area.x==0 && area.y==0 && area.width==target.width);
        RB_CountStencilBuffer();assert(overdraw==3.0 && readRect[0]==0 && readRect[1]==0);
        assert(readRect[2]==target.width && readRect[3]==target.height && allocations==0);
    }
    reset();assert(RB_GetDebugReadbackArea(true,area));RB_ShowIntensity();assert(reads==1 && draws==1);
    reset();glBindFramebuffer=nullptr;RB_ShowDepthBuffer();assert(reads==1 && draws==1);
    reset();idRenderTexture vr{12,640,480};standIn=&vr;drawFbo=12;viewport[2]=640;viewport[3]=480;
    RB_ShowDepthBuffer();assert(drawExtent[0]==640 && drawExtent[1]==480);
    reset();drawFbo=99;assert(!RB_GetDebugReadbackArea(true,area));RB_ShowDepthBuffer();assert(reads==0 && allocations==0);
    reset();idRenderTexture invalid{7,idMath::INT_MAX,2};drawFbo=7;backEnd.renderTexture=&invalid;
    assert(!RB_GetDebugReadbackArea(false,area));invalid.width=0;assert(!RB_GetDebugReadbackArea(false,area));
    for(int coordinate:{-1,idMath::INT_MAX}) {
        reset();viewport[0]=coordinate;RB_ShowIntensity();assert(reads==0 && draws==0 && allocations==0);
    }
    reset();viewport[2]=0;RB_ShowDepthBuffer();assert(reads==0 && draws==0);
    reset();viewport[2]=1281;assert(!RB_GetDebugReadbackArea(true,area));
    reset();assert(RB_GetDebugReadbackArea(true,area));drawFbo=9;
    assert(!RB_ReadFramebufferPixels(area,GL_RGBA,GL_UNSIGNED_BYTE,nullptr) && reads==0);
    reset();idRenderTexture target{7,640,400};backEnd.renderTexture=&target;drawFbo=7;readFbo=19;
    samples=4;viewport[2]=640;viewport[3]=400;resolveResult=0;
    RB_ShowDepthBuffer();assert(reads==0 && draws==0 && allocations==0 && readFbo==19 && drawFbo==7);
    resolveResult=91;readError=5;RB_ShowIntensity();assert(reads==1 && draws==0 && warnings==1 && allocations==0);
    std::puts("GL debug readback: scaled/window/VR targets, offset bounds, MSAA, state restoration and refusal controls passed");
}
'''


def main():
    source = (ROOT / 'src/renderer/tr_rendertools.cpp').read_text()
    area = source.split('struct debugReadbackArea_t {', 1)[1].split('};', 1)[0]
    functions = '\n'.join(function_body(source, signature) for signature in (
        'static bool RB_GetDebugReadbackArea(', 'static bool RB_ReadFramebufferPixels(',
        'void RB_ShowIntensity( void ) {', 'void RB_ShowDepthBuffer( void ) {',
        'void RB_CountStencilBuffer( void ) {'))
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    scratch = ROOT / '.tmp/renderer-debug-readback'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        directory = Path(directory)
        environment = dict(os.environ, TEMP=str(directory), TMP=str(directory))
        test, binary = directory / 'readback.cpp', directory / 'readback.exe'
        test.write_text('#include <initializer_list>\n' + SUPPORT + '\nstruct debugReadbackArea_t {' + area + '};\n' + functions + MAIN)
        subprocess.run([compiler, '-std=c++17', str(test), '-o', str(binary)], check=True, env=environment)
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    main()
