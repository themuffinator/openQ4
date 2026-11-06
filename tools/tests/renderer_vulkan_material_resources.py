#!/usr/bin/env python3
"""Execute authored Vulkan binding and frame budgets against counted driver doubles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from filesystem_case_segments import function_body

SUPPORT = r'''
#define VK_NO_PROTOTYPES
#define VK_USE_64_BIT_PTR_DEFINES 0
#include <vulkan/vulkan.h>
#include "GLSLCompiler.h"
#include "ARBTranslator.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
using VmaAllocation = void *;
template<class T> T Max(T a,T b) { return std::max(a,b); }
struct Common {
    int warnings=0;
    std::vector<std::string> messages;
    void Warning(const char *format,...) {
        char text[2048];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
        ++warnings;messages.emplace_back(text);
    }
    void Printf(const char *,...) {}
} commonStore, *common=&commonStore;
struct { VkDevice device=VK_NULL_HANDLE; VkPhysicalDeviceProperties deviceProperties={}; bool depthClipNegativeOneToOne=false; } vkCtx;
struct { bool GetBool() const { return true; } } r_vkResourceProfile;
struct { int value=0; int GetInteger() const { return value; } } r_vkMaterialDescriptorPoolTest;
struct { uint64_t ringBytes=0, ringAllocations=0; } vkResourceProfile;
struct idImage { unsigned handle=1; unsigned GetDeviceHandle() const { return handle; } } white, sampled{2};
struct { idImage *whiteImage=&white; } imageManager;
auto *globalImages=&imageManager;
struct vkImageEntry_t {
    VkSampler sampler=1;VkImageView view=2;VkImageLayout layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool isCube=false, materialSampleFlipY=false;VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
} imageEntry;
struct drawInteraction_t {};
bool failPool=false, failSets=false, failReset=false, failPipeline=false, failShader=false;
bool resident=true, scopeOpen=true;
uint64_t nextHandle=10;
int creates=0, resets=0, allocations=0, updates=0, binds=0, pipelineRequests=0, destroyedPools=0, refusedByDriver=0;
struct Pool { unsigned capacity=0,used=0; };
std::map<VkDescriptorPool,Pool> driverPools;
VkResult vkCreateDescriptorSetLayout(VkDevice,const VkDescriptorSetLayoutCreateInfo *info,const VkAllocationCallbacks *,VkDescriptorSetLayout *out) {
    assert(info->bindingCount==1 || info->bindingCount==oq4material::MaxTextures);
    if(failShader)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out=++nextHandle;return VK_SUCCESS;
}
VkResult vkCreatePipelineLayout(VkDevice,const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *,VkPipelineLayout *out) {
    assert(info->setLayoutCount==2);*out=++nextHandle;return VK_SUCCESS;
}
VkResult vkCreateShaderModule(VkDevice,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *,VkShaderModule *out) {
    assert(info->codeSize>0 && info->pCode);
    if(failShader)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out=++nextHandle;return VK_SUCCESS;
}
VkResult vkCreateDescriptorPool(VkDevice,const VkDescriptorPoolCreateInfo *info,const VkAllocationCallbacks *,VkDescriptorPool *out) {
    ++creates;if(failPool)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(info->poolSizeCount==2 && info->pPoolSizes[0].descriptorCount*2==info->maxSets);
    assert(info->pPoolSizes[1].descriptorCount==info->pPoolSizes[0].descriptorCount*oq4material::MaxTextures);
    *out=++nextHandle;driverPools[*out]={info->maxSets,0};return VK_SUCCESS;
}
void vkDestroyDescriptorPool(VkDevice,VkDescriptorPool pool,const VkAllocationCallbacks *) {
    assert(driverPools.erase(pool)==1);++destroyedPools;
}
VkResult vkResetDescriptorPool(VkDevice,VkDescriptorPool pool,VkDescriptorPoolResetFlags) {
    ++resets;assert(driverPools.count(pool));if(failReset)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    driverPools.at(pool).used=0;return VK_SUCCESS;
}
VkResult vkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo *info,VkDescriptorSet *out) {
    ++allocations;assert(info->descriptorSetCount==2 && driverPools.count(info->descriptorPool));
    if(failSets)return VK_ERROR_OUT_OF_POOL_MEMORY;
    auto &pool=driverPools.at(info->descriptorPool);
    // A full pool refuses as a driver does, without writing the sets.
    if(pool.used+2>pool.capacity) { ++refusedByDriver;return VK_ERROR_OUT_OF_POOL_MEMORY; }
    pool.used+=2;
    out[0]=++nextHandle;out[1]=++nextHandle;return VK_SUCCESS;
}
void vkUpdateDescriptorSets(VkDevice,unsigned count,const VkWriteDescriptorSet *writes,unsigned,const VkCopyDescriptorSet *) {
    ++updates;assert(count==1+oq4material::MaxTextures && writes[0].pBufferInfo);
    assert(writes[0].pBufferInfo->range==sizeof(oq4material::UniformBlock));
}
void vkCmdBindPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline pipeline) { assert(pipeline); }
void vkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned count,const VkDescriptorSet *sets,unsigned,const unsigned *) {
    assert(count==2 && sets[0] && sets[1]);++binds;
}
VkDescriptorSet VK_Exec_ImageDescriptor(unsigned,bool) { return resident?1:VK_NULL_HANDLE; }
const vkImageEntry_t *VK_Image_GetEntry(unsigned) { return resident?&imageEntry:nullptr; }
bool VK_Exec_MainRenderingScopeOpen() { return scopeOpen; }
VkCommandBuffer VK_Exec_ActiveCmd() { return reinterpret_cast<VkCommandBuffer>(uintptr_t(1)); }
'''

GLOBALS = r'''
struct {
    bool frameOpen=true;int frameSlot=0;vkRing_t uniformRings[VK_FRAMES_IN_FLIGHT]={};
    int numProgramPipelines=0;vkProgramPipeline_t programPipelines[VK_MAX_PROGRAM_PIPELINES]={};
} vkExec;
int VK_Exec_ActiveFrameSlot() { return vkExec.frameSlot; }
std::mutex diagnosticMutex;
std::set<std::string> diagnostics;
int suppressedDiagnostics=0,peakDraws=0;
uint64_t frameSerial=1;
// Retirement selection is independently executed by renderer_vulkan_material_cache.
// The binding test selects one released identity to verify the actual retry.
int reclaimIdentity=-1;
bool ReclaimPipelineCapacity();
VkDescriptorSetLayout uniformLayout=VK_NULL_HANDLE,textureLayout=VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
FramePools framePools[VK_FRAMES_IN_FLIGHT];
'''

PIPELINES = r'''
vkPipelineTarget_t currentTarget={};
std::set<VkPipeline> livePipelines;
int destroyedPipelines=0;
vkPipelineTarget_t VK_Exec_CurrentPipelineTarget() { return currentTarget; }
void VK_Exec_InteractionVertexInput(VkVertexInputBindingDescription &binding,
        VkVertexInputAttributeDescription *attributes,VkPipelineVertexInputStateCreateInfo &input) {
    binding={};binding.stride=64;input={};input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;
    for(unsigned i=0;i<6;++i) { attributes[i]={};attributes[i].location=i;attributes[i].format=VK_FORMAT_R32_SFLOAT;attributes[i].offset=4*i; }
    input.vertexAttributeDescriptionCount=6;input.pVertexAttributeDescriptions=attributes;
}
VkPipeline VK_Exec_CreatePipeline(VkShaderModule vertex,VkShaderModule fragment,
        const VkPipelineVertexInputStateCreateInfo *input,int,VkPipelineLayout layout,bool,bool,const vkPipelineTarget_t &) {
    ++pipelineRequests;assert(vertex && fragment && layout && input->vertexBindingDescriptionCount>0);
    if(failPipeline)return VK_NULL_HANDLE;
    const VkPipeline pipeline=++nextHandle;assert(livePipelines.insert(pipeline).second);return pipeline;
}
void vkDestroyPipeline(VkDevice,VkPipeline pipeline,const VkAllocationCallbacks *) {
    assert(livePipelines.erase(pipeline)==1);++destroyedPipelines;
}
'''

MAIN = r'''
bool ReclaimPipelineCapacity() {
    if(reclaimIdentity<0 || !VK_Exec_ProgramPipelineCapacityExhausted())return false;
    VK_Exec_ReleaseAuthoredMaterialPipelines(reclaimIdentity);reclaimIdentity=-1;
    return true;
}
std::vector<unsigned char> backing[VK_FRAMES_IN_FLIGHT];
oq4material::UniformBlock block={};
idImage *images[oq4material::MaxTextures]={};
Program program;
void Reset(int capacity=VK_UNIFORM_RING_BYTES) {
    failPool=failSets=failReset=failPipeline=failShader=false;resident=scopeOpen=true;
    reclaimIdentity=-1;r_vkMaterialDescriptorPoolTest.value=0;
    creates=resets=allocations=updates=binds=pipelineRequests=destroyedPools=refusedByDriver=0;
    driverPools.clear();diagnostics.clear();commonStore={};imageEntry={};livePipelines.clear();destroyedPipelines=0;
    currentTarget={};currentTarget.colorCount=1;currentTarget.colorFormats[0]=VK_FORMAT_R8G8B8A8_UNORM;
    currentTarget.depthFormat=VK_FORMAT_D24_UNORM_S8_UINT;currentTarget.samples=VK_SAMPLE_COUNT_1_BIT;
    vkCtx.deviceProperties.limits.minUniformBufferOffsetAlignment=256;
    vkCtx.deviceProperties.limits.maxUniformBufferRange=65536;
    vkExec={};vkExec.frameOpen=true;
    uniformLayout=textureLayout=VK_NULL_HANDLE;pipelineLayout=VK_NULL_HANDLE;
    for(int slot=0;slot<VK_FRAMES_IN_FLIGHT;++slot) {
        framePools[slot]={};backing[slot].assign(capacity+64,0xcd);
        auto &ring=vkExec.uniformRings[slot];ring={};ring.capacity=capacity;
        ring.mapped=backing[slot].data()+32;ring.buffer=100+slot;ring.label="uniform";
        VK_MaterialPrograms_BeginFrame(slot);
    }
    program={};program.valid=true;program.name="native-pressure";program.identity=1;
    program.compiled.vertex={0x07230203};program.compiled.fragment={0x07230203};program.compiled.textureMask=1;
    program.compiled.vertexInputMask=3;
    std::fill(std::begin(images),std::end(images),&sampled);block={};
}
void NextFrame(int slot) {
    vkExec.frameSlot=slot;vkExec.uniformRings[slot].cursor=0;vkExec.uniformRings[slot].overflowWarned=false;
    VK_MaterialPrograms_BeginFrame(slot);
}
bool Bind() { return BindProgram(&program,images,block,0,false,nullptr); }
void Guards() {
    for(auto &bytes:backing) {
        for(int i=0;i<32;++i)assert(bytes[i]==0xcd && bytes[bytes.size()-1-i]==0xcd);
    }
}
int main() {
    // No descriptor was recorded: repeated driver refusal must preserve the
    // earlier draw's bytes and leave its unused uniform budget available.
    Reset();auto &ring=vkExec.uniformRings[0];ring.cursor=256;
    std::memset(ring.mapped,0x42,256);failSets=true;
    for(int i=0;i<3000;++i)assert(!Bind());
    std::printf("Descriptor refusals: attempts=%d cursor=%d expected=256 recorded=%d\n",allocations,ring.cursor,binds);
    std::fflush(stdout);
    assert(ring.cursor==256 && binds==0 && updates==0 && framePools[0].draws==0);
    for(int i=0;i<256;++i)assert(ring.mapped[i]==0x42);
    assert(commonStore.warnings==1 && program.lastSkip=="material descriptor allocation failed");
    failSets=false;assert(Bind() && framePools[0].draws==1);Guards();

    // Exercise the production four-MiB ring and real uniform-block ABI.
    Reset();int accepted=0;while(Bind()) { ++accepted;assert(accepted<10000); }
    assert(accepted>2048 && accepted<4096 && program.lastSkip=="material uniform ring exhausted");
    assert(framePools[0].created==2 && ring.cursor<=ring.capacity);
    const int stoppedCursor=ring.cursor,stoppedAllocations=allocations;
    for(int i=0;i<1000;++i)assert(!Bind());
    assert(ring.cursor==stoppedCursor && allocations==stoppedAllocations);Guards();
    NextFrame(0);assert(Bind() && framePools[0].draws==1 && ring.cursor==sizeof(block));

    // Isolate the descriptor quota with a larger host-side test ring. This
    // deliberately does not claim that production's ring is this large.
    Reset(32*1024*1024);
    const int quota=MaxDrawsPerPool*MaxPoolsPerSlot;
    for(int i=0;i<quota;++i)assert(Bind());
    assert(creates==MaxPoolsPerSlot && framePools[0].draws==quota && driverPools.size()==MaxPoolsPerSlot);
    const int fullCursor=ring.cursor,fullAllocations=allocations;
    for(int i=0;i<1000;++i)assert(!Bind());
    assert(program.lastSkip=="frame descriptor capacity exhausted" && commonStore.warnings==1);
    assert(ring.cursor==fullCursor && allocations==fullAllocations);Guards();
    NextFrame(1);assert(Bind() && framePools[0].draws==quota && framePools[1].draws==1);
    NextFrame(0);assert(Bind() && framePools[0].created==MaxPoolsPerSlot && framePools[0].draws==1);
    failReset=true;NextFrame(0);const int resetAllocations=allocations;
    assert(!Bind() && program.lastSkip=="frame descriptor pool reset failed" && allocations==resetAllocations);
    failReset=false;NextFrame(0);assert(Bind());

    // r_vkMaterialDescriptorPoolTest: one unchained pool per slot, refused
    // by the driver once full, then the production pools on the next frame.
    Reset();r_vkMaterialDescriptorPoolTest.value=4;NextFrame(0);
    assert(framePools[0].poolDraws==4 && framePools[0].created==0);
    for(int i=0;i<4;++i)assert(Bind());
    const int drillCursor=ring.cursor;
    for(int i=0;i<500;++i)assert(!Bind());
    assert(refusedByDriver==500 && creates==1 && framePools[0].created==1 && framePools[0].draws==4);
    assert(ring.cursor==drillCursor && program.lastSkip=="material descriptor allocation failed" && commonStore.warnings==1);
    NextFrame(0);for(int i=0;i<4;++i)assert(Bind());assert(!Bind() && creates==1);
    r_vkMaterialDescriptorPoolTest.value=0;NextFrame(0);
    assert(destroyedPools==1 && framePools[0].poolDraws==MaxDrawsPerPool && framePools[0].created==0);
    for(int i=0;i<MaxDrawsPerPool+8;++i)assert(Bind());
    assert(framePools[0].created==2 && refusedByDriver==501);Guards();
    std::printf("Descriptor drill: driver refusals=%d, recovered draws=%d\n",refusedByDriver,framePools[0].draws);

    Reset();failPool=true;assert(!Bind() && creates==1 && framePools[0].created==0 && ring.cursor==0);
    failPool=false;assert(Bind());
    Reset();failPipeline=true;assert(!Bind() && pipelineRequests==1 && allocations==0 && ring.cursor==0);
    failPipeline=false;assert(Bind());
    Reset();failShader=true;assert(!Bind() && creates==0 && ring.cursor==0);
    failShader=false;assert(Bind());
    Reset();resident=false;assert(!Bind() && pipelineRequests==0 && ring.cursor==0);
    resident=true;assert(Bind());
    Reset();scopeOpen=false;assert(!Bind() && creates==0 && ring.cursor==0);
    scopeOpen=true;imageEntry.materialSampleFlipY=true;assert(Bind());
    assert(block.textureOrientation[0]==0);Guards();

    // Fill the actual shared cache with two vertex-stream variants for each
    // of 256 identities. Existing keys survive a full cache; releasing one
    // identity frees exactly its entries without replacing another pipeline.
    Reset();std::vector<VkPipeline> cached;
    for(int i=0;i<VK_MAX_PROGRAM_PIPELINES;++i) {
        const VkPipeline pipeline=VK_Exec_AuthoredMaterialPipeline(i/2+1,11,12,13,0,(i&1)!=0,3);
        assert(pipeline);cached.push_back(pipeline);
    }
    assert(vkExec.numProgramPipelines==VK_MAX_PROGRAM_PIPELINES && livePipelines.size()==VK_MAX_PROGRAM_PIPELINES);
    assert(VK_Exec_AuthoredMaterialPipeline(257,11,12,13,0,false,3)==VK_NULL_HANDLE);
    for(int i=0;i<VK_MAX_PROGRAM_PIPELINES;++i)
        assert(VK_Exec_AuthoredMaterialPipeline(i/2+1,11,12,13,0,(i&1)!=0,3)==cached[i]);
    assert(pipelineRequests==VK_MAX_PROGRAM_PIPELINES);
    VK_Exec_ReleaseAuthoredMaterialPipelines(128);
    assert(destroyedPipelines==2 && vkExec.numProgramPipelines==VK_MAX_PROGRAM_PIPELINES-2);
    for(int i=0;i<VK_MAX_PROGRAM_PIPELINES;++i)if(i/2+1!=128)
        assert(VK_Exec_AuthoredMaterialPipeline(i/2+1,11,12,13,0,(i&1)!=0,3)==cached[i]);
    assert(VK_Exec_AuthoredMaterialPipeline(257,11,12,13,0,false,3));
    assert(VK_Exec_AuthoredMaterialPipeline(257,11,12,13,0,true,3));
    assert(vkExec.numProgramPipelines==VK_MAX_PROGRAM_PIPELINES);
    Reset();assert(Bind());const VkPipeline original=vkExec.programPipelines[0].pipeline;
    assert(VK_Exec_AuthoredMaterialPipeline(1,program.vertex,program.fragment,pipelineLayout,GLS_DEPTHFUNC_ALWAYS,false,3)==original);
    currentTarget.samples=VK_SAMPLE_COUNT_4_BIT;assert(Bind());
    currentTarget.colorFormats[0]=VK_FORMAT_R16G16B16A16_SFLOAT;assert(Bind());
    currentTarget.lowerOrigin=true;assert(Bind());
    assert(VK_Exec_AuthoredMaterialPipeline(1,program.vertex,program.fragment,pipelineLayout,GLS_ALPHA_COVERAGE,false,3));
    assert(vkExec.numProgramPipelines==5);
    Reset();
    for(int i=0;i<VK_MAX_PROGRAM_PIPELINES;++i)
        assert(VK_Exec_AuthoredMaterialPipeline(1000+i,11,12,13,0,false,3));
    assert(VK_Exec_ProgramPipelineCapacityExhausted());
    assert(!Bind() && allocations==0 && destroyedPipelines==0);
    reclaimIdentity=1000;
    assert(Bind() && allocations==1 && destroyedPipelines==1);
    assert(VK_Exec_ProgramPipelineCapacityExhausted());
    std::printf("Limits exercised: uniform draws=%d, descriptor draws=%d, cached pipelines=%d\n",accepted,quota,VK_MAX_PROGRAM_PIPELINES);
    std::puts("Authored resources: descriptor refusal, uniform/pool/pipeline limits, frame isolation and recovery passed");
}
'''


def run(output):
    output=output.resolve()
    if not output.is_relative_to(ROOT / '.tmp'):
        raise RuntimeError('Proof outputs must stay under project .tmp')
    output.mkdir(parents=True,exist_ok=True)
    paths=[ROOT/'src/renderer/Vulkan'/name for name in ('vk_MaterialPrograms.cpp','vk_GuiExecutor.cpp','VulkanDevice.h','vk_Image.h')]
    paths.append(ROOT/'src/renderer/tr_local.h')
    material,executor,device,images,state=[path.read_text() for path in paths]
    paths.extend(ROOT/'src/renderer/materialprogram'/name for name in ('GLSLCompiler.h','ARBTranslator.h'))
    constants='\n'.join(re.search(r'(?:constexpr|static const|const) (?:int|size_t) '+name+r'\s*=.*?;',source).group()
        for source,name in ((material,'MaxDrawsPerPool'),(material,'MaxPoolsPerSlot'),(material,'MaxDiagnostics'),
                            (executor,'VK_UNIFORM_RING_BYTES'),(device,'VK_FRAMES_IN_FLIGHT'),
                            (executor,'VK_MAX_PROGRAM_PIPELINES'),(images,'VK_MAX_COLOR_ATTACHMENTS'),
                            *((state,'GLS_'+name) for name in ('SRCBLEND_BITS','DSTBLEND_BITS','ALPHA_COVERAGE',
                              'REDMASK','GREENMASK','BLUEMASK','ALPHAMASK','COLORMASK','DEPTHFUNC_ALWAYS'))))
    structs='struct Program {'+material.split('struct Program {',1)[1].split('\n};',1)[0]+'\n};\n'
    structs+='struct FramePools {'+material.split('struct FramePools {',1)[1].split('\n};',1)[0]+'\n};\n'
    structs+='typedef struct vkRing_s {'+executor.split('typedef struct vkRing_s {',1)[1].split('} vkRing_t;',1)[0]+'} vkRing_t;\n'
    for name in ('vkPipelineTarget','vkProgramPipeline'):
        structs+='typedef struct '+name+'_s {'+executor.split('typedef struct '+name+'_s {',1)[1].split('} '+name+'_t;',1)[0]+'} '+name+'_t;\n'
    functions='\n'.join(function_body(source,signature) for source,signature in (
        (executor,'static bool VK_Ring_CountOverflowFrame('),(executor,'static const char *VK_Ring_Label('),
        (executor,'static int VK_Ring_Alloc('),(executor,'bool VK_Exec_AuthoredUniformAlloc('),
        (executor,'int VK_Exec_InteractionUniformCheckpoint('),(executor,'void VK_Exec_InteractionUniformRestore('),
        (executor,'static bool VK_Exec_PipelineTargetsMatch('),(executor,'VkPipeline VK_Exec_AuthoredMaterialPipeline('),
        (executor,'bool VK_Exec_ProgramPipelineCapacityExhausted('),
        (executor,'void VK_Exec_ReleaseAuthoredMaterialPipelines('),
        (material,'const char *Kind('),(material,'bool Report('),(material,'bool Fail('),
        (material,'bool CreateLayouts('),(material,'bool CreateModule('),(material,'VkDescriptorPool DrawPool('),
        (material,'void VK_MaterialPrograms_BeginFrame('),(material,'bool BindProgram(')))
    source=output/'resources.cpp';source.write_text(SUPPORT+constants+'\n'+structs+GLOBALS+PIPELINES+functions+MAIN)
    binary=output/('resources.exe' if os.name=='nt' else 'resources')
    compiler=next((path for name in ('clang++','g++','c++') if (path:=shutil.which(name))),None)
    if not compiler:raise RuntimeError('C++ compiler required')
    command=[compiler,'-std=c++17','-I'+str(ROOT/'src/external/vulkan/include'),
             '-I'+str(ROOT/'src/renderer/materialprogram'),str(source),'-o',str(binary)]
    environment=dict(os.environ,TMP=str(output),TEMP=str(output),TMPDIR=str(output))
    results=[]
    for name,argv in (('compile',command),('run',[str(binary)])):
        result=subprocess.run(argv,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=environment,cwd=output)
        (output/(name+'.log')).write_text(result.stdout)
        results.append(dict(phase=name,command=argv,returncode=result.returncode))
        print(result.stdout,flush=True)
        if result.returncode:break
    proof=dict(status='pass' if all(row['returncode']==0 for row in results) else 'fail',results=results,
               inputs={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
               harnessSHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               scope='Production binding, ring, descriptor-pool (including the r_vkMaterialDescriptorPoolTest drill) and pipeline-cache functions with counted driver doubles; no GPU qualification. Descriptor quota uses a larger host-side test ring to isolate that limit.')
    shutil.copy2(__file__,output/'harness.py')
    (output/'report.json').write_text(json.dumps(proof,indent=2)+'\n')
    return int(proof['status']!='pass')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,help='retain generated source, logs and proof under project .tmp')
    args=parser.parse_args()
    if args.output_dir is not None:
        return run(args.output_dir)
    scratch=ROOT/'.tmp/renderer-vulkan-material-resources'
    scratch.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        return run(Path(temporary))


if __name__=='__main__':raise SystemExit(main())
