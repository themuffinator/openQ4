#!/usr/bin/env python3
"""Actual Vulkan image methods, GL error draining, and image-entry guard preambles.

Native APIs are counted stand-ins. Entry preambles use a counted continuation,
not a simulated file loader; whole touched units are separately compiled with
engine headers. No engine, device, window or input is started.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    # Selected methods have no unmatched braces in strings/comments.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


VK_SUPPORT = r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static int checks=0,errors=0,started=0,succeeded=0,refused=0,native=0,destroyed=0,deferred=0;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
static bool allowed=true,formatOK=true,batchOK=true;
static int fail=0;
bool R_ImagePolicyOperationAllowed(){return allowed;}
bool R_ImagePolicyContentMutation(){return allowed;}
void R_ImagePolicyObserveError(const char*,int=0){++errors;}
struct Common {void Warning(const char*,...) {}} commonObject;
static Common* common=&commonObject;
enum textureFilter_t {TF_DEFAULT,TF_LINEAR,TF_NEAREST};
enum textureRepeat_t {TR_REPEAT,TR_CLAMP,TR_CLAMP_TO_BORDER,TR_CLAMP_TO_ZERO,TR_CLAMP_TO_ZERO_ALPHA,TR_MIRRORED_REPEAT};
enum {TT_2D,TT_CUBIC,FMT_DEPTH,FMT_DEPTH_STENCIL,FMT_RGB565};
using textureUsage_t=int;
struct idImageOpts {int width=8,height=8,textureType=TT_2D,format=100,numLevels=4,numMSAASamples=0;};
struct idImage;
struct renderImageOperation_t {
 bool done=false,permit;
 explicit renderImageOperation_t(const idImage*,bool=false,int=0,int=0,int=0,int=0):permit(R_ImagePolicyOperationAllowed()){++started;}
 ~renderImageOperation_t(){if(permit){if(done)++succeeded;else ++refused;}}
 bool Allowed()const{return permit;}void Succeeded(uint64_t=0){done=true;}
};
struct idImage {idImageOpts opts;int usage=0;textureFilter_t filter=TF_DEFAULT;textureRepeat_t repeat=TR_REPEAT;unsigned texnum=99;uint64_t storageGeneration=0;
 bool IsFileBacked()const{return true;}void PurgeImage(){++native;texnum=99;}void AllocImage();void SubImageUpload(int,int,int,int,int,int,const void*,int)const;};
struct VmaAllocation_T{};using VmaAllocation=VmaAllocation_T*;
struct VmaAllocationCreateInfo{int usage=0,flags=0;};
struct VmaAllocationInfo{void* pMappedData=nullptr;};
enum{VMA_MEMORY_USAGE_AUTO=1,VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT=1,VMA_ALLOCATION_CREATE_MAPPED_BIT=2};
struct {bool initialized=true;VkPhysicalDevice physicalDevice{};VkDevice device{};int allocator=0;struct{struct{float maxSamplerAnisotropy=16;}limits;}deviceProperties;}vkCtx;
struct{int GetInteger()const{return 1;}}image_anisotropy;
struct imageFilterState_t {int mode=0;bool usesMipmaps=true,minLinear=true,magLinear=true,mipLinear=true;};
imageFilterState_t R_GetDefaultImageFilterState(){return {};}
struct vkSamplerKey_t{textureFilter_t filter;textureRepeat_t repeat;bool mips;int anisotropy,defaultFilterMode;};
static constexpr int VK_MAX_SAMPLERS=4;
static vkSamplerKey_t vkSamplerKeys[VK_MAX_SAMPLERS]{};
static VkSampler vkSamplers[VK_MAX_SAMPLERS]{};static int vkNumSamplers=0;
VkResult vkCreateSampler(VkDevice,const VkSamplerCreateInfo*,const VkAllocationCallbacks*,VkSampler* out){++native;*out=(VkSampler)(uintptr_t)1;return fail==1?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
VkResult vmaCreateImage(int,const VkImageCreateInfo*,const VmaAllocationCreateInfo*,VkImage* out,VmaAllocation* a,void*){++native;*out=(VkImage)(uintptr_t)1;*a=(VmaAllocation)(uintptr_t)1;return fail==2?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
static int views=0;
VkResult vkCreateImageView(VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView* out){++native;++views;*out=(VkImageView)(uintptr_t)1;return fail==(views==1?3:4)?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
void vmaDestroyImage(int,VkImage,VmaAllocation){++destroyed;}
void vkDestroyImageView(VkDevice,VkImageView,const VkAllocationCallbacks*){++destroyed;}
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice,VkFormat,VkFormatProperties* p){++native;*p={};p->optimalTilingFeatures=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;}
static unsigned char mapped[1024]{};
VkResult vmaCreateBuffer(int,const VkBufferCreateInfo*,const VmaAllocationCreateInfo*,VkBuffer* b,VmaAllocation* a,VmaAllocationInfo* info){++native;*b=(VkBuffer)(uintptr_t)1;*a=(VmaAllocation)(uintptr_t)1;info->pMappedData=mapped;return fail==5?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;}
VkResult vmaFlushAllocation(int,VmaAllocation,VkDeviceSize,VkDeviceSize){++native;return fail==6?VK_ERROR_MEMORY_MAP_FAILED:VK_SUCCESS;}
void VK_Device_DeferDestroy(VkImage,VkImageView,VkBuffer,VmaAllocation){++deferred;}
struct vkFormatInfo_t{VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;int bytesPerBlock=4,blockDim=1;VkComponentMapping swizzle{};bool expandRgb565=false;};
bool VK_Image_GetFormatInfo(const idImageOpts&,textureUsage_t,vkFormatInfo_t&){++native;return formatOK;}
VkSampleCountFlagBits VK_Image_SelectSampleCount(const idImageOpts&,VkFormat,VkImageAspectFlags,VkImageUsageFlags,VkImageCreateFlags,bool){return VK_SAMPLE_COUNT_1_BIT;}
int VK_Image_SampleCountInteger(VkSampleCountFlagBits){return 0;}
static int slot=0;int VK_Image_AllocSlot(){return slot;}
'''

VK_AFTER_ENTRY = r'''
static vkImageEntry_t vkImages[4]{};static unsigned vkImageGenerationCounter=1;
vkImageEntry_t* VK_Image_GetEntry(unsigned i){++native;return i<4&&vkImages[i].inUse?&vkImages[i]:nullptr;}
void VK_Image_RecordUpload(VkCommandBuffer,void*){}
bool VK_Device_BatchedUpload(void(*)(VkCommandBuffer,void*),void*,VkBuffer,VmaAllocation,VkDeviceSize,uint64_t* batch){++native;if(batchOK&&batch)*batch=1;return batchOK;}
'''

VK_MAIN = r'''
static void Reset(){errors=started=succeeded=refused=native=destroyed=deferred=views=0;allowed=formatOK=batchOK=true;fail=0;slot=0;vkCtx.initialized=true;vkNumSamplers=0;std::memset(vkImages,0,sizeof(vkImages));}
int main(){try{
 Reset();{idImage i;i.AllocImage();TEST(succeeded==1&&refused==0&&errors==0);TEST(i.texnum==0);}
 for(int refusal=0;refusal<7;++refusal){Reset();idImage i;
  if(refusal==0)vkCtx.initialized=false;if(refusal==1)i.opts.width=0;if(refusal==2)formatOK=false;
  if(refusal==3)slot=-1;if(refusal==4)fail=2;if(refusal==5)fail=3;if(refusal==6){fail=4;i.opts.format=FMT_DEPTH_STENCIL;}
  i.AllocImage();TEST(refused==1&&succeeded==0);
 }
 Reset();allowed=false;{idImage i;i.AllocImage();TEST(native==0&&i.storageGeneration==0&&i.texnum==99);}
 Reset();fail=1;{idImage i;i.AllocImage();TEST(errors>=1);}
 Reset();fail=1;vkNumSamplers=1;vkSamplerKeys[0].filter=TF_NEAREST;vkSamplers[0]=(VkSampler)(uintptr_t)9;
 {idImage i;i.AllocImage();TEST(errors>=1);TEST(vkImages[0].sampler==(VkSampler)(uintptr_t)9);}
 Reset();vkNumSamplers=VK_MAX_SAMPLERS;for(auto& k:vkSamplerKeys)k.filter=TF_NEAREST;vkSamplers[0]=(VkSampler)(uintptr_t)9;
 {idImage i;i.AllocImage();TEST(errors>=1);TEST(vkImages[0].sampler==(VkSampler)(uintptr_t)9);}
 // Count every real SubImageUpload early return, including the batch failure
 // which must not fall through to the success marker.
 for(int refusal=0;refusal<9;++refusal){Reset();idImage i;i.AllocImage();started=succeeded=refused=errors=native=0;
  const void* data=mapped;int width=8;
  if(refusal==0)i.texnum=99;if(refusal==1)data=nullptr;if(refusal==2)width=0;
  if(refusal==3)vkImages[0].samples=VK_SAMPLE_COUNT_4_BIT;if(refusal==4)vkImages[0].aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
  if(refusal==5)formatOK=false;if(refusal==6)fail=5;if(refusal==7)fail=6;if(refusal==8)batchOK=false;
  i.SubImageUpload(0,0,0,0,width,8,data,0);TEST(refused==1&&succeeded==0);TEST(!vkImages[0].everUploaded);
  if(refusal>=7)TEST(deferred==1);
 }
 Reset();{idImage i;i.AllocImage();started=succeeded=refused=0;i.SubImageUpload(0,0,0,0,8,8,mapped,0);TEST(succeeded==1&&refused==0&&vkImages[0].everUploaded);}
 Reset();allowed=false;{idImage i;i.SubImageUpload(0,0,0,0,8,8,mapped,0);TEST(native==0);}
 std::printf("actual Vulkan image boundaries: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''

ENTRY_SUPPORT = r'''
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <stdexcept>
static bool allowed=false;static int mutations=0,checks=0,observed=0,logged=0;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
bool R_ImagePolicyOperationAllowed(){return allowed;}
bool R_ImagePolicyContentMutation(){return allowed;}
void R_ImagePolicyObserveError(const char*,int){++observed;}
struct idImage{void ActuallyLoadImage(bool);void Reload(bool);void PurgeGL();void PurgeVK();};
struct idImageManager{void ReloadImages(bool);};
struct Common{void Printf(const char*,...){++logged;}} commonObject;static Common* common=&commonObject;
struct idStr{static void snPrintf(char* o,int n,const char* f,...){va_list a;va_start(a,f);std::vsnprintf(o,n,f,a);va_end(a);}};
enum{GL_NO_ERROR,GL_INVALID_ENUM,GL_INVALID_VALUE,GL_INVALID_OPERATION,GL_STACK_OVERFLOW,GL_STACK_UNDERFLOW,GL_OUT_OF_MEMORY};
static int pending=0;int glGetError(){int n=pending;pending=0;return n;}
void R_GLDebugOutput_FlushMessages(){}
struct{bool ignored=true;bool GetBool()const{return ignored;}}r_ignoreGLErrors;
'''

ENTRY_MAIN = r'''
int main(){try{
 idImage image;idImageManager manager;
 image.ActuallyLoadImage(false);image.Reload(true);manager.ReloadImages(true);image.PurgeGL();image.PurgeVK();TEST(mutations==0);
 allowed=true;image.ActuallyLoadImage(false);image.Reload(true);manager.ReloadImages(true);image.PurgeGL();image.PurgeVK();TEST(mutations==5);
 for(int e=1;e<=GL_OUT_OF_MEMORY;++e){pending=e;int before=observed;GL_CheckErrors();TEST(observed==before+1);TEST(!pending);}
 TEST(logged==0);r_ignoreGLErrors.ignored=false;pending=GL_INVALID_OPERATION;GL_CheckErrors();TEST(logged==1&&observed==GL_OUT_OF_MEMORY+1);
 std::printf("actual image entry preambles / GL error drain: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''

MODULE_SUPPORT = r'''
#include "RendererModule.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
static int checks=0,calls=0,retains=0,releases=0,references=0;
#define TEST(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(0)
struct idStr{static void Copynz(char* out,const char* text,int n){if(n>0)std::snprintf(out,n,"%s",text);}};
class idRenderSystem {public:bool ready=true;bool IsOpenGLRunning()const{return ready;}} renderer;
static idRenderSystem* renderSystem=&renderer;
static uint64_t rm_displayModuleEpoch=1;
static const renderWindowServices_t* rm_displayVideoPin=nullptr;
static renderWindowServices_t services{},otherServices{};
static bool available=true,retainOkay=true,restartOkay=true,throwRestart=false;
static std::function<void(const char*)> callback;
static renderImagePolicyRequest_t request{},submitted{};
static rendererImagePolicyResult_t sentinel{};
static struct {bool interfacesPublished=true,moduleExportValid=true;renderExport_t moduleExport{};rendererModuleStatus_t status{};}rm_state;
const renderWindowServices_t* Sys_GetRenderWindowServices(){return available?&services:nullptr;}
bool Retain(){++retains;if(callback)callback("retain");if(retainOkay)++references;return retainOkay;}
void Release(){++releases;--references;if(callback)callback("release");}
bool Apply(const renderWindowRequest_t*,renderWindowState_t*,char*,int){return true;}
bool Query(renderWindowState_t*){return true;}
bool R_TryImagePolicyRestart(const renderImagePolicyRequest_t* input,renderImagePolicyResult_t* output,char* error,int size){
 ++calls;TEST(references==1);if(callback)callback("restart");submitted=*input;
 if(throwRestart)throw std::runtime_error("counted callback failure");
 *output={};output->attempt=7;output->deviceGeneration=8;
 renderer.ready=restartOkay;if(!restartOkay)idStr::Copynz(error,"refused",size);return restartOkay;
}
'''

MODULE_MAIN = r'''
static void Reset(){
 TEST(!references&&!rm_displayVideoPin);calls=retains=releases=0;callback={};
 renderer.ready=available=retainOkay=restartOkay=true;throwRestart=false;renderSystem=&renderer;rm_displayModuleEpoch=1;
 services={};services.RetainVideoSystem=Retain;services.ReleaseVideoSystem=Release;services.ApplyScreenParmsStrict=Apply;services.QueryWindowState=Query;
 rm_state={};rm_state.interfacesPublished=rm_state.moduleExportValid=true;rm_state.moduleExport.TryImagePolicyRestart=R_TryImagePolicyRestart;
 request={};request.window.parms.width=800;
}
static void Run(bool expected){
 rendererImagePolicyResult_t output=sentinel;char error[128]="unchanged";
 bool result=R_RendererModule_TryImagePolicyRestart(&request,&output,error,sizeof(error));TEST(result==expected);
 if(!result){TEST(!std::memcmp(&output,&sentinel,sizeof(output)));TEST(error[0]);}
 else{TEST(output.moduleEpoch==1&&output.resources.attempt==7&&output.resources.deviceGeneration==8);TEST(!error[0]);}
}
int main(){try{
 std::memset(&sentinel,0x6b,sizeof(sentinel));
 Reset();Run(true);TEST(calls==1&&retains==1&&releases==1&&!references);
 Reset();available=false;Run(false);TEST(!retains&&!calls);
 Reset();services.QueryWindowState=nullptr;Run(false);TEST(!retains&&!calls);
 Reset();rm_state.moduleExport.TryImagePolicyRestart=nullptr;Run(false);TEST(!retains&&!calls);
 Reset();retainOkay=false;Run(false);TEST(retains==1&&!calls&&!references);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"retain"))request.window.parms.width=123;};Run(true);TEST(submitted.window.parms.width==800);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"retain"))++rm_displayModuleEpoch;};Run(false);TEST(!calls&&references==1);callback={};RM_ReleaseDisplayVideoPin();
 Reset();callback=[](const char* at){if(!std::strcmp(at,"restart"))++rm_displayModuleEpoch;};Run(false);TEST(calls==1&&!references);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"release"))++rm_displayModuleEpoch;};Run(false);TEST(calls==1&&!references);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"restart"))available=false;};Run(false);TEST(!references);
 Reset();callback=[](const char* at){if(!std::strcmp(at,"retain")){rendererImagePolicyResult_t output=sentinel;char error[128];TEST(!R_RendererModule_TryImagePolicyRestart(&request,&output,error,sizeof(error)));TEST(!std::memcmp(&output,&sentinel,sizeof(output)));}};Run(true);TEST(retains==1&&calls==1);
 Reset();restartOkay=false;Run(false);TEST(references==1&&!releases);restartOkay=true;Run(true);TEST(retains==1&&releases==1);
 Reset();throwRestart=true;{bool threw=false;rendererImagePolicyResult_t output=sentinel;char error[128];try{R_RendererModule_TryImagePolicyRestart(&request,&output,error,sizeof(error));}catch(...){threw=true;}TEST(threw&&!std::memcmp(&output,&sentinel,sizeof(output)));}throwRestart=false;Run(true);TEST(!references&&retains==1);
 Reset();rm_state.interfacesPublished=false;rm_state.status.disposition=RENDER_MODULE_DISPOSITION_BUILTIN;
#if !defined(OPENQ4_RENDERER_MODULE_ONLY) && !defined(ID_DEDICATED)
 Run(true);TEST(calls==1);
#else
 Run(false);TEST(!calls&&!retains);
#endif
 std::printf("actual image policy module ownership: %d checks passed\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''


def build_units(repository, headers):
    renderer = repository / 'src/renderer'
    vk = (renderer / 'Vulkan/vk_Image.cpp').read_text(encoding='utf-8')
    entry_header = (headers / 'src/renderer/Vulkan/vk_Image.h').read_text(encoding='utf-8')
    entry = entry_header[entry_header.index('typedef struct vkImageEntry_s'):entry_header.index('} vkImageEntry_t;') + len('} vkImageEntry_t;')]
    context = vk[vk.index('typedef struct vkUploadContext_s'):vk.index('} vkUploadContext_t;') + len('} vkUploadContext_t;')]
    vulkan = VK_SUPPORT + entry + VK_AFTER_ENTRY + context + '\n'.join(method(vk, name) for name in (
        'static VkSampler VK_Image_GetSampler(', 'void idImage::AllocImage(', 'void idImage::SubImageUpload(')) + VK_MAIN
    entries = []
    for file, signature in [('Image_load.cpp', 'void idImage::ActuallyLoadImage('), ('Image_load.cpp', 'void idImage::Reload('), ('ImageManager.cpp', 'void idImageManager::ReloadImages(')]:
        body = method((renderer / file).read_text(encoding='utf-8'), signature)
        brace = body.index('{')
        guard = 'if ( !R_ImagePolicyOperationAllowed() ) return;'
        start = body.index(guard, brace)
        if body[brace + 1:start].strip():
            raise AssertionError(f'{signature}: guard must precede CPU work')
        end = start + len(guard)
        content_guard = 'if ( !R_ImagePolicyContentMutation() ) return;'
        if body[end:].lstrip().startswith(content_guard):
            end = body.index(content_guard, end) + len(content_guard)
        entries.append(body[:end] + '\n ++mutations;\n}\n')
    for file, name in [('OpenGL/gl_Image.cpp', 'PurgeGL'), ('Vulkan/vk_Image.cpp', 'PurgeVK')]:
        body = method((renderer / file).read_text(encoding='utf-8'), 'void idImage::PurgeImage(')
        guard = 'if ( !R_ImagePolicyContentMutation() ) return;'
        brace = body.index('{'); start = body.index(guard, brace)
        assert not body[brace + 1:start].strip(), 'purge guard must precede CPU/GPU work'
        entries.append(body[:start + len(guard)].replace('PurgeImage', name) + '\n ++mutations;\n}\n')
    entries.append(method((renderer / 'RenderSystem_init.cpp').read_text(encoding='utf-8'), 'void GL_CheckErrors('))
    # The release upload boundary must preserve errors before program loaders or
    # other consumers can issue a direct glGetError and erase the observation.
    gl_upload = method((renderer / 'OpenGL/gl_Image.cpp').read_text(encoding='utf-8'), 'void idImage::SubImageUpload(')
    if 'if ( R_ImagePolicyActive() || imageConsumedLoad_t::Active(this) ) GL_CheckErrors();\n\timageOperation.Succeeded();' not in gl_upload:
        raise AssertionError('checked GL upload completion must collect errors before success')
    module = (renderer / 'RendererModule.cpp').read_text(encoding='utf-8')
    module = MODULE_SUPPORT + method(module, 'static void RM_ReleaseDisplayVideoPin(') + method(module, 'bool R_RendererModule_TryImagePolicyRestart(') + MODULE_MAIN
    return {'vulkan': vulkan, 'entries': ENTRY_SUPPORT + '\n'.join(entries) + ENTRY_MAIN,
        'module-builtin': module, 'module-only': '#define OPENQ4_RENDERER_MODULE_ONLY\n' + module,
        'module-dedicated': '#define ID_DEDICATED\n' + module}


def mutate(code, old, new):
    if code.count(old) != 1:
        raise AssertionError(f'non-unique mutation anchor: {old}')
    return code.replace(old, new)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--headers', type=Path, default=ROOT)
    parser.add_argument('--mutations', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    repository, headers = args.repository.resolve(), args.headers.resolve()
    compiler = next((p for n in ('clang++', 'g++', 'c++') if (p := shutil.which(n))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    inputs = [repository / name for name in (
        'src/renderer/Image_load.cpp', 'src/renderer/ImageManager.cpp', 'src/renderer/RenderSystem_init.cpp',
        'src/renderer/OpenGL/gl_Image.cpp', 'src/renderer/Vulkan/vk_Image.cpp', 'tools/tests/renderer_image_policy_boundaries.py',
        'src/renderer/RendererModule.cpp', 'src/renderer/RendererModule.h', 'src/renderer/RenderModuleAPI.h',
        'src/renderer/RendererResourceSettings.h', 'src/renderer/DisplayPresentation.h')]
    inputs += [headers / 'src/renderer/Vulkan/vk_Image.h']
    inputs += sorted((headers / 'src/external/vulkan/include').rglob('*.h'))
    def hashes():
        return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    before = hashes()
    out = Path(tempfile.mkdtemp(prefix='image-boundaries-', dir=repository / '.tmp'))
    records = []
    env = dict(os.environ, TEMP=str(out), TMP=str(out), TMPDIR=str(out))
    units = build_units(repository, headers)
    cases = [(name, 'baseline', code) for name, code in units.items()]
    if args.mutations:
        vk = units['vulkan']
        changes = [
            ('sampler-refusal', 'R_ImagePolicyObserveError( "Vulkan sampler creation failed", samplerResult );', '(void)samplerResult;'),
            ('sampler-capacity', 'R_ImagePolicyObserveError( "Vulkan sampler cache exhausted" );', '(void)0;'),
            ('batch-refusal', 'VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, staging, stagingAlloc );\n\t\treturn;\n\t}\n\n\timageOperation.Succeeded(consumedBatch);', 'VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, staging, stagingAlloc );\n\t}\n\n\timageOperation.Succeeded(consumedBatch);'),
        ]
        cases += [('vulkan', name, mutate(vk, old, new)) for name, old, new in changes]
        entries = units['entries']
        cases += [('entries', 'foreign-entry', entries.replace('if ( !R_ImagePolicyOperationAllowed() ) return;', '(void)0;', 1).replace('if ( !R_ImagePolicyContentMutation() ) return;', '(void)0;', 1))]
        cases += [('entries', 'gl-error-drain', mutate(entries, 'R_ImagePolicyObserveError( "OpenGL resource error", err );', '(void)err;'))]
        cases += [('entries', 'purge-entry', entries.replace('void idImage::PurgeGL() {\n\tif ( !R_ImagePolicyContentMutation() ) return;', 'void idImage::PurgeGL() {', 1))]
    status = 'running'
    try:
        for name, case, code in cases:
            tag = name + '-' + case
            source = out / (tag + '.cpp')
            source.write_text(code, encoding='utf-8')
            binary = out / (tag + '.exe')
            command = [compiler, '-std=c++20', '-D_CRT_SECURE_NO_WARNINGS', '-I', str(headers / 'src/external/vulkan/include'), '-I', str(repository / 'src/renderer')]
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
            command += [str(source), '-o', str(binary)]
            for stage, call in (('compile', command), ('run', [str(binary)])):
                completed = subprocess.run(call, cwd=out, env=env, timeout=90, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                log = out / (tag + '-' + stage + '.log')
                log.write_text(completed.stdout, encoding='utf-8')
                record = {'unit': name, 'case': case, 'stage': stage, 'command': call, 'exit_code': completed.returncode, 'log': str(log), 'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
                records.append(record)
                if stage == 'compile' and completed.returncode:
                    raise RuntimeError(f'{tag}: compilation failed')
                if stage == 'run':
                    if case == 'baseline' and completed.returncode:
                        raise RuntimeError(f'{tag}: baseline failed')
                    if case != 'baseline' and not completed.returncode:
                        raise RuntimeError(f'{tag}: compiled mutation survived')
                    record['expected_rejection'] = case != 'baseline'
                    print(f'{tag}: ' + ('rejected compiled mutation' if case != 'baseline' else completed.stdout.strip()))
        status = 'passed'
    finally:
        after = hashes()
        if before != after:
            status = 'source_changed'
        elif status != 'passed':
            status = 'failed'
        (out / 'result.json').write_text(json.dumps({'status': status, 'files_before': before, 'files_after': after, 'records': records, 'limitations': __doc__}, indent=2) + '\n')
        print(out / 'result.json')
    if status != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
