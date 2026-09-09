#!/usr/bin/env python3
"""Execute the Vulkan final-pass controller and GLSL math with counted GPU stubs.

No Vulkan device or game is created. Real driver/validation and screenshot
parity remain integration gates; this checks production control flow, shader
math/orientation, neutral bypass, resource reuse, and reported failure paths.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "src/renderer/Vulkan/shaders/display_color_mapping_spv.h"
struct ivec2;
struct vec2 { float x=0,y=0; vec2()=default; vec2(float x,float y):x(x),y(y){}; explicit vec2(ivec2); };
struct vec3 {
    float v[3];
    explicit vec3(float x=0):v{x,x,x}{}
    vec3(float x,float y,float z):v{x,y,z}{}
};
struct vec4 { vec3 rgb; float a; vec4(vec3 rgb=vec3(),float a=0):rgb(rgb),a(a){} };
struct ivec2 {
    int x,y;
    explicit ivec2(vec2 v):x(int(v.x)),y(int(v.y)){}
    ivec2(int x,int y):x(x),y(y){}
};
vec2::vec2(ivec2 v):x(float(v.x)),y(float(v.y)){}
vec2 operator*(vec2 a,vec2 b){return {a.x*b.x,a.y*b.y};}
vec3 operator*(vec3 a,float b) { for(float& c:a.v)c*=b;return a; }
vec3 clamp(vec3 a,float lo,float hi) { for(float& c:a.v)c=std::clamp(c,lo,hi);return a; }
vec3 pow(vec3 a,vec3 b) { for(int i=0;i<3;++i)a.v[i]=std::pow(a.v[i],b.v[i]);return a; }
float max(float a,float b) { return std::max(a,b); }
struct { struct { vec2 xy; } settings; } pc;
vec2 fragUV;
vec4 outColor;
int scene=0,textureWidth=0,textureHeight=0;
std::vector<vec4> pixels,sourcePixels;
ivec2 textureSize(int,int) { return {textureWidth,textureHeight}; }
vec4 texelFetch(int,ivec2 p,int) {
    assert(p.x>=0 && p.x<textureWidth && p.y>=0 && p.y<textureHeight);
    return sourcePixels[p.y*textureWidth+p.x];
}
void FragmentMain();
using VkPipeline=uintptr_t;
using VkShaderModule=uintptr_t;
using VkDescriptorSet=uintptr_t;
constexpr uintptr_t VK_NULL_HANDLE=0;
constexpr int VK_SUCCESS=0,VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO=1;
constexpr int VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO=2;
constexpr int GLS_SRCBLEND_ONE=1,GLS_DSTBLEND_ZERO=2,VK_FALSE=0;
constexpr int VK_COMPARE_OP_ALWAYS=3,VK_CULL_MODE_NONE=0,VK_FRONT_FACE_COUNTER_CLOCKWISE=1;
constexpr int VK_PIPELINE_BIND_POINT_GRAPHICS=0,VK_SHADER_STAGE_VERTEX_BIT=1,VK_SHADER_STAGE_FRAGMENT_BIT=2;
constexpr int VK_FRAMES_IN_FLIGHT=2,FMT_RGBA8=1,TF_NEAREST=2,TR_CLAMP=3,TD_DEFAULT=4;
struct VkShaderModuleCreateInfo { int sType; size_t codeSize; const uint32_t* pCode; };
struct VkPipelineVertexInputStateCreateInfo { int sType; };
struct VkExtent2D { uint32_t width=4,height=3; };
struct VkViewport { float x,y,width,height,minDepth,maxDepth; };
struct VkRect2D { struct { int x,y; } offset; VkExtent2D extent; };
struct idImageOpts { int width=0,height=0,format=0,numLevels=0,numMSAASamples=0;bool isPersistant=false; };
struct idImage { bool loaded=true; int handle=0; idImageOpts opts;
    bool IsLoaded() const {return loaded;} unsigned GetDeviceHandle() const {return handle;}
};
struct {
    VkShaderModule temporalResolveVertModule=11,displayColorFragModule=0;
    VkPipeline displayColorPipeline=0;
    idImage* displayColorSourceImages[2]={};
    bool displayColorMapped=false,displayColorMappingWarned=false;
    bool frameOpen=true,temporalScenePendingComposite=false,mainScopeOpen=true;
    int frameSlot=0,cmd=1,pipelineLayout=7;
} vkExec;
struct { int device=1; bool swapchainTransferSrc=true,depthBoundsSupported=true,presentationBlocked=false; VkExtent2D swapchainExtent; } vkCtx;
struct CVar {float value=1;float GetFloat()const{return value;}} r_brightness,r_gamma;
namespace idMath {
float ClampFloat(float a,float b,float v){return std::clamp(v,a,b);}
float Fabs(float v){return std::abs(v);}
}
template<class T>T Max(T a,T b){return std::max(a,b);}
std::string failure;
std::vector<std::string> calls;
int warnings=0,shaderCreates=0,pipelineCreates=0,imageCreates=0,draws=0;
struct Common {void Warning(const char*,const char*){++warnings;}} commonObject;
Common* common=&commonObject;
std::map<std::string,idImage> images;
const char* va(const char* format,int slot){static char name[80];std::snprintf(name,sizeof(name),format,slot);return name;}
struct Images {
    idImage* ScratchImage(const char* name,idImageOpts* opts,int filter,int repeat,int usage) {
        calls.push_back("image");++imageCreates;
        assert(opts->format==FMT_RGBA8 && opts->numLevels==1 && opts->numMSAASamples==0 && opts->isPersistant);
        assert(filter==TF_NEAREST && repeat==TR_CLAMP && usage==TD_DEFAULT);
        if(failure=="image")return nullptr;
        auto& image=images[name];image.loaded=true;image.opts=*opts;image.handle=vkExec.frameSlot+20;return &image;
    }
} imageManager;
Images* globalImages=&imageManager;
int vkCreateShaderModule(int,const VkShaderModuleCreateInfo* info,void*,VkShaderModule* out) {
    ++shaderCreates;calls.push_back("shader");
    assert(info->sType==VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO && info->codeSize>20);
    assert(reinterpret_cast<uintptr_t>(info->pCode)%4==0 && info->pCode[0]==0x07230203);
    if(failure=="shader")return -1;*out=12;return VK_SUCCESS;
}
int VK_Exec_SwapchainPipelineTarget(){return 19;}
VkPipeline VK_Exec_CreatePipeline(VkShaderModule vert,VkShaderModule frag,const VkPipelineVertexInputStateCreateInfo*,int bits,int layout,bool depth,bool masked,int target) {
    ++pipelineCreates;calls.push_back("pipeline");
    assert(vert==11 && frag==12 && bits==(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ZERO));
    assert(layout==7 && !depth && !masked && target==19);return failure=="pipeline"?0:13;
}
bool VK_TemporalPresentation_CompositePendingScene(){calls.push_back("compose");if(failure=="compose")return false;vkExec.temporalScenePendingComposite=false;return true;}
bool VK_Exec_SetRenderTarget(void* target){assert(!target);calls.push_back("target");if(failure=="target")return false;vkExec.mainScopeOpen=true;return true;}
bool VK_Exec_CopyRender(idImage* source,int x,int y,int w,int h,int face,bool depth) {
    calls.push_back("copy");assert(x==0 && y==0 && face==0 && !depth);
    if(failure=="copy")return false;
    source->opts.width=w;source->opts.height=h;textureWidth=w;textureHeight=h;
    sourcePixels.resize(pixels.size());
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)sourcePixels[y*w+x]=pixels[(h-1-y)*w+x];
    return true;
}
VkDescriptorSet VK_GuiExecutor_GetImageDescriptor(unsigned handle){calls.push_back("descriptor");assert(handle==unsigned(vkExec.frameSlot+20));return failure=="descriptor"?0:handle;}
void vkCmdSetViewport(int,int,int,const VkViewport* v){assert(v->x==0 && v->y==0 && v->width==textureWidth && v->height==textureHeight && v->maxDepth==1);}
void vkCmdSetScissor(int,int,int,const VkRect2D* r){assert(r->offset.x==0 && r->offset.y==0 && int(r->extent.width)==textureWidth && int(r->extent.height)==textureHeight);}
void vkCmdSetDepthTestEnable(int,int value){assert(value==VK_FALSE);}
void vkCmdSetDepthWriteEnable(int,int value){assert(value==VK_FALSE);}
void vkCmdSetDepthCompareOp(int,int value){assert(value==VK_COMPARE_OP_ALWAYS);}
void vkCmdSetCullMode(int,int value){assert(value==VK_CULL_MODE_NONE);}
void vkCmdSetFrontFace(int,int value){assert(value==VK_FRONT_FACE_COUNTER_CLOCKWISE);}
void vkCmdSetDepthBiasEnable(int,int value){assert(value==VK_FALSE);}
void vkCmdSetStencilTestEnable(int,int value){assert(value==VK_FALSE);}
void vkCmdSetDepthBoundsTestEnable(int,int value){assert(value==VK_FALSE);}
void vkCmdBindPipeline(int,int,VkPipeline pipeline){assert(pipeline==13);}
void vkCmdBindDescriptorSets(int,int,int layout,int first,int count,const VkDescriptorSet* set,int dynamicCount,void*){assert(layout==7 && first==0 && count==1 && *set && dynamicCount==0);}
void vkCmdPushConstants(int,int layout,int stages,int offset,size_t size,const void* value){
    assert(layout==7 && stages==3 && offset==0 && size==16);
    const auto* mapping=static_cast<const float*>(value);pc.settings.xy={mapping[0],mapping[1]};
}
void vkCmdDraw(int,int count,int instances,int first,int base){
    assert(count==3 && instances==1 && first==0 && base==0);++draws;calls.push_back("draw");
    for(int y=0;y<textureHeight;++y)for(int x=0;x<textureWidth;++x){fragUV={(x+.5f)/textureWidth,(y+.5f)/textureHeight};FragmentMain();pixels[y*textureWidth+x]=outColor;}
}
bool VK_GuiExecutor_SubmitFrame(bool present){assert(present);calls.push_back("present");vkExec.frameOpen=false;return true;}
void NewComposition(int slot=0,int width=4,int height=3){
    vkCtx.swapchainExtent.width=width;vkCtx.swapchainExtent.height=height;
    vkExec.frameOpen=true;vkExec.mainScopeOpen=true;vkExec.displayColorMapped=false;vkExec.frameSlot=slot;
    calls={"scene","gui","overlay"};pixels.clear();
    for(int i=0;i<width*height;++i)pixels.push_back({vec3(((i*37+13)%256)/255.f,((i*29+79)%256)/255.f,((i*71+3)%256)/255.f),(i%5)/4.f});
}
bool SamePixels(const std::vector<vec4>& other){return pixels.size()==other.size() && std::memcmp(pixels.data(),other.data(),pixels.size()*sizeof(vec4))==0;}
void CheckMapping(const std::vector<vec4>& original,float brightness,float gamma){
    for(size_t i=0;i<pixels.size();++i){
        for(int c=0;c<3;++c){const double expected=std::pow(std::clamp(double(original[i].rgb.v[c])*brightness,0.,1.),1./gamma);assert(std::abs(pixels[i].rgb.v[c]-expected)<2e-5);}
        assert(pixels[i].a==original[i].a);
    }
}
'''

MAIN = r'''
int main(){
    NewComposition();auto original=pixels;const auto initialCalls=calls;
    assert(VK_DisplayColorMapping_Apply() && SamePixels(original) && calls==initialCalls);
    r_brightness.value=1.00005f;r_gamma.value=.99995f;
    assert(VK_DisplayColorMapping_Apply() && SamePixels(original) && calls==initialCalls);
    assert(shaderCreates==0 && pipelineCreates==0 && imageCreates==0 && draws==0);
    const vec2 settings[]={{.4f,.7f},{1.8f,1.4f},{0,2},{-1,1},{20,.001f},{.8f,0}};
    for(auto mapping:settings){
        NewComposition();original=pixels;r_brightness.value=mapping.x;r_gamma.value=mapping.y;
        assert(VK_DisplayColorMapping_Apply());CheckMapping(original,std::clamp(mapping.x,0.f,16.f),std::max(mapping.y,.001f));
        const auto once=pixels;const int count=draws;assert(VK_DisplayColorMapping_Apply());
        assert(draws==count && SamePixels(once));assert(VK_GuiExecutor_EndFrameAndPresent());
        assert(draws==count && SamePixels(once) && calls.back()=="present");
    }
    assert(shaderCreates==1 && pipelineCreates==1 && imageCreates==1);
    NewComposition(1,7,2);original=pixels;assert(VK_DisplayColorMapping_Apply());
    CheckMapping(original,.8f,.001f);assert(imageCreates==2 && vkExec.displayColorSourceImages[0]!=vkExec.displayColorSourceImages[1]);
    NewComposition(0,9,4);original=pixels;assert(VK_DisplayColorMapping_Apply());
    assert(imageCreates==2 && vkExec.displayColorSourceImages[0]->opts.width==9);
    // A purged scratch is reallocated as scratch, never loaded from a file.
    vkExec.displayColorSourceImages[0]->loaded=false;NewComposition();assert(VK_DisplayColorMapping_Apply());assert(imageCreates==3);
    NewComposition();vkExec.temporalScenePendingComposite=true;calls.clear();
    assert(VK_DisplayColorMapping_Apply() && calls.front()=="compose");
    assert(std::find(calls.begin(),calls.end(),"copy")<std::find(calls.begin(),calls.end(),"draw"));
    // Setup failures retain the complete original frame, report once, and
    // remain retryable. No failed attempt can mark uncorrected output mapped.
    for(const char* point:{"shader","pipeline","image","copy","descriptor","target","compose"}){
        vkExec={};images.clear();warnings=0;failure=point;NewComposition();original=pixels;
        vkExec.temporalScenePendingComposite=failure=="compose";const int count=draws;
        assert(!VK_DisplayColorMapping_Apply() && SamePixels(original) && !vkExec.displayColorMapped && warnings==1 && draws==count);
        assert(!VK_DisplayColorMapping_Apply() && warnings==1 && draws==count);
        failure.clear();assert(VK_DisplayColorMapping_Apply() && !vkExec.displayColorMappingWarned);CheckMapping(original,.8f,.001f);
    }
    NewComposition();original=pixels;vkCtx.swapchainTransferSrc=false;
    assert(!VK_DisplayColorMapping_Apply() && SamePixels(original));
    vkCtx.swapchainTransferSrc=true;assert(VK_DisplayColorMapping_Apply());
    // A capture's resumed clear starts fresh; stale descriptors/push state
    // cannot turn a new composition into a second mapping of the old image.
    NewComposition();original=pixels;r_brightness.value=1.25f;r_gamma.value=1.6f;
    assert(VK_GuiExecutor_EndFrameAndPresent());CheckMapping(original,1.25f,1.6f);
    // A lost device stops before any color-mapping allocation, draw or
    // submission, and does not falsely mark the composition mapped.
    NewComposition();original=pixels;const auto blockedCalls=calls;
    const int blockedDraws=draws,blockedImages=imageCreates;
    vkCtx.presentationBlocked=true;
    assert(!VK_GuiExecutor_EndFrameAndPresent());
    assert(SamePixels(original) && calls==blockedCalls && draws==blockedDraws && imageCreates==blockedImages);
    assert(vkExec.frameOpen && !vkExec.displayColorMapped);
    vkCtx.presentationBlocked=false;
    assert(VK_GuiExecutor_EndFrameAndPresent());CheckMapping(original,1.25f,1.6f);
    std::puts("Vulkan display mapping: production pass/GLSL math, orientation/alpha, neutral bypass, composition, one-time mapping, slot reuse and reported failures passed");
}
'''


def main():
    source = (ROOT / 'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text()
    shader = (ROOT / 'src/renderer/Vulkan/shaders/display_color_mapping.frag').read_text()
    readback = function_body(source, 'bool VK_GuiExecutor_ReadPixels(')
    order = ['VK_TemporalPresentation_CompositePendingScene()',
             'VK_DisplayColorMapping_Apply()', 'vkCmdCopyImageToBuffer(',
             'VK_GuiExecutor_SubmitFrame(', 'vkWaitForFences(',
             'vkExec.displayColorMapped = false;', 'VK_Exec_BeginMainRendering( true )']
    positions = [readback.index(part) for part in order]
    assert positions == sorted(positions), 'capture must map before readback and reset only after retirement'
    begin = function_body(source, 'static bool VK_GuiExecutor_BeginFrame(')
    assert begin.index('vkWaitForFences(') < begin.index('vkExec.displayColorMapped = false;')
    assert 'vkExec.displayColorPipeline = VK_NULL_HANDLE;' in begin, 'swapchain format change must invalidate the display pipeline'
    shutdown = function_body(source, 'void VK_GuiExecutor_Shutdown( void ) {')
    assert 'vkDestroyPipeline( vkCtx.device, vkExec.displayColorPipeline, NULL );' in shutdown
    assert 'vkDestroyShaderModule( vkCtx.device, vkExec.displayColorFragModule, NULL );' in shutdown
    assert 'VK_DisplayColorMapping_Apply' not in function_body(source, 'static bool VK_GuiExecutor_SubmitFrame( bool present ) {'), 'readback submission must not apply twice'
    code = SUPPORT + function_body(shader, 'vec4 ApplyDisplayColorMapping(')
    code += function_body(shader, 'void main()').replace('void main()', 'void FragmentMain()', 1)
    for signature in ('static bool VK_DisplayColorMapping_Failed(', 'static VkPipeline VK_DisplayColorMapping_GetPipeline(',
                      'static bool VK_DisplayColorMapping_Apply(', 'bool VK_GuiExecutor_EndFrameAndPresent( void ) {'):
        code += function_body(source, signature)
    code += MAIN
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vk-display-', dir=ROOT / '.tmp') as temporary:
        fixture = Path(temporary) / 'display.cpp'
        binary = Path(temporary) / 'display.exe'
        fixture.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', '-I', str(ROOT), str(fixture), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
