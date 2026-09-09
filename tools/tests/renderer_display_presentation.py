#!/usr/bin/env python3
"""Run actual presentation tracking and backend frame methods against fake APIs.

No window, GPU, input, or engine is started. Vulkan/GL calls are deterministic
boundaries; this qualifies control flow and status, not driver behavior/scanout.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src/renderer"


def method(source, signature):
    # Some production functions also have a forward declaration. Select the
    # definition, leaving the complete body (including branches) unchanged.
    pos = 0
    while True:
        pos = source.index(signature, pos)
        brace = source.index("{", pos)
        semi = source.find(";", pos, brace)
        if semi < 0:
            return function_body(source[pos:], signature) + "\n"
        pos += len(signature)


SUPPORT = r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include "DisplayPresentation.h"
#include <cassert>
#include <cstring>
#include <type_traits>
#include <vector>
#include <string>
using VmaAllocation = void*;
using VmaAllocator = void*;
template<class T> T handle(int n) { return (T)(uintptr_t)n; }
static std::vector<std::string> calls;
static VkResult queueResult=VK_SUCCESS,presentResult=VK_SUCCESS,endResult=VK_SUCCESS,
    flushResult=VK_SUCCESS,waitResult=VK_SUCCESS,resetResult=VK_SUCCESS,beginResult=VK_SUCCESS,
    resetFenceResult=VK_SUCCESS,idleResult=VK_SUCCESS,acquireResult=VK_SUCCESS,uploadResult=VK_SUCCESS;
static bool recreateOkay=true,executorOkay=true;
struct Cvar {
    bool modified=false; int value=1;
    bool IsModified(){return modified;} void ClearModified(){modified=false;}
    int GetInteger()const{return value;}
} r_swapInterval;
struct renderWindowRequest_t {int swapInterval=1;};
static renderWindowRequest_t request;
static bool strict=false,loadingBypass=false;
static const renderWindowRequest_t* R_GetRecoverableWindowRequest(){return &request;}
static bool R_IsRecoverableRendererRestart(){return strict;}
static std::vector<VkPresentModeKHR> supportedModes{VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR,VK_PRESENT_MODE_FIFO_RELAXED_KHR};
static int recreateCalls=0,submitCalls=0,presentCalls=0,acquireCalls=0,waitCalls=0,timingFailures=0;
static uint32_t submittedWaits=0,submittedSignals=0;
struct Common { template<class... T> void Warning(const char*,T...){} template<class... T> void Printf(const char*,T...){} } commonObject,*common=&commonObject;
struct { int frameCount=1; } tr;
constexpr int VK_FRAMES_IN_FLIGHT=2;
struct Context {
    bool initialized=true,presentationBlocked=false,uploadBatchOpen=false,uploadBatchInFlight=false;
    VkDevice device=handle<VkDevice>(1); VkQueue graphicsQueue=handle<VkQueue>(2);
    VkPhysicalDevice physicalDevice=handle<VkPhysicalDevice>(30); VkSurfaceKHR surface=handle<VkSurfaceKHR>(31);
    VmaAllocator allocator=(void*)1;
    VkSwapchainKHR swapchain=handle<VkSwapchainKHR>(3);
    VkFormat swapchainFormat=VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D swapchainExtent{1280,720};
    VkCommandBuffer commandBuffers[2]{handle<VkCommandBuffer>(4),handle<VkCommandBuffer>(5)};
    VkFence frameFences[2]{handle<VkFence>(6),handle<VkFence>(7)};
    VkSemaphore acquireSemaphores[2]{handle<VkSemaphore>(8),handle<VkSemaphore>(9)};
    VkSemaphore renderFinishedSemaphores[2]{handle<VkSemaphore>(10),handle<VkSemaphore>(11)};
    VkImage swapchainImages[2]{handle<VkImage>(12),handle<VkImage>(13)},depthImages[2]{handle<VkImage>(14),handle<VkImage>(15)};
    VkImageView depthViews[2]{};
    VkCommandBuffer uploadCommandBuffer=handle<VkCommandBuffer>(20);
    VkFence uploadFence=handle<VkFence>(21);
    int numUploadBatchPending=0,numUploadBatchInFlight=0;
    VkBuffer uploadBatchPendingBuffers[4]{},uploadBatchInFlightBuffers[4]{};
    VmaAllocation uploadBatchPendingAllocations[4]{},uploadBatchInFlightAllocations[4]{};
    VkDeviceSize uploadBatchPendingBytes=0;
    int frameSlot=0,recordingSlot=0,swapInterval=1;
    bool strictSwapInterval=false; int strictSwapIntervalValue=0,strictSwapIntervalCvar=0;
} vkCtx;
struct vkRing_t { void* mapped=nullptr; int cursor=0; VmaAllocation allocation=nullptr; bool overflowWarned=false; };
struct Pipeline { VkPipeline pipeline{}; };
struct Executor {
    bool frameOpen=false,acquireWaitPending=false,displayColorMapped=false;
    int frameSlot=0; uint32_t swapImageIndex=0; VkCommandBuffer cmd=handle<VkCommandBuffer>(4);
    vkRing_t vertexRings[2],indexRings[2],uniformRings[2];
    int temporalNativeWidth=0,temporalNativeHeight=0,temporalHistoryGenerationSeen=0;
    VkFormat temporalSwapchainFormat=VK_FORMAT_UNDEFINED,pipelineTargetFormat=VK_FORMAT_R8G8B8A8_UNORM;
    VkPipeline casterPipeline{},pointCasterPipeline{},displayColorPipeline{};
    VkDescriptorPool descriptorPool{}; VkDescriptorSet retiredSets[2][4]{}; int numRetiredSets[2]{};
    void *activeRenderTexture=nullptr,*activeColorEntry=nullptr,*activeDepthEntry=nullptr;
    VkImageView activeDepthAttachmentView{}; VkExtent2D activeExtent{}; int activePipelineTarget=0;
    int vertMemo[2]{},idxMemo[2]{},gpuSkinningMemo[2]{};
PIPELINE_FIELDS
} vkExec;
struct { bool active=false; } vkSharedGeometryCheckpoint;
static void R_TemporalPresentation_InvalidateHistory(const char*){}
static int R_TemporalPresentation_HistoryGeneration(){return 1;}
static int R_GetEffectiveSwapInterval(){return loadingBypass?0:r_swapInterval.value;}
static void R_RendererMetrics_ResetGpuFrameTiming(const char*){}
static VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkPresentModeKHR* output){
    if(!output){*count=static_cast<uint32_t>(supportedModes.size());return VK_SUCCESS;}
    const uint32_t copied=*count<supportedModes.size()?*count:static_cast<uint32_t>(supportedModes.size());
    for(uint32_t i=0;i<copied;++i)output[i]=supportedModes[i];*count=copied;return VK_SUCCESS;
}
static int VK_Exec_SwapchainPipelineTarget(){return 0;}
static void VK_Exec_BeginMainRendering(bool){calls.emplace_back("begin-render");}
static void VK_Exec_EndMainRendering(){calls.emplace_back("end-render");}
static void VK_Exec_TransitionActiveTargetToSampled(){}
static void VK_GpuFrameTiming_BeginFrame(VkCommandBuffer,int,int){}
static void VK_GpuFrameTiming_EndFrame(VkCommandBuffer,int){}
static void VK_GpuFrameTiming_SubmitFailed(int){++timingFailures;}
static bool VK_GuiExecutor_Init(){calls.emplace_back("executor-init");return executorOkay;}
static void VK_Device_FlushDeferredDestroys(int){calls.emplace_back("destroy-deferred");}
static VkResult vkDeviceWaitIdle(VkDevice){calls.emplace_back("idle");return idleResult;}
static void vkDestroyPipeline(VkDevice,VkPipeline,const void*){calls.emplace_back("destroy-pipeline");}
static VkResult vkFreeDescriptorSets(VkDevice,VkDescriptorPool,uint32_t,const VkDescriptorSet*){return VK_SUCCESS;}
static VkResult vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++waitCalls;return waitResult;}
static VkResult vkResetFences(VkDevice,uint32_t,const VkFence*){return resetFenceResult;}
static VkResult vkResetCommandBuffer(VkCommandBuffer,VkCommandBufferResetFlags){return resetResult;}
static VkResult vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*){return beginResult;}
static VkResult vkEndCommandBuffer(VkCommandBuffer){calls.emplace_back("end-command");return endResult;}
static void vkCmdPipelineBarrier2(VkCommandBuffer,const VkDependencyInfo*){}
static VkResult vmaFlushAllocation(VmaAllocator,VmaAllocation,VkDeviceSize,VkDeviceSize){return flushResult;}
static void vmaDestroyBuffer(VmaAllocator,VkBuffer,VmaAllocation){calls.emplace_back("destroy-upload");}
static VkResult vkAcquireNextImageKHR(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t* index){++acquireCalls;*index=0;return acquireResult;}
static VkResult vkQueueSubmit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence){calls.emplace_back("upload-submit");return uploadResult;}
static VkResult vkQueueSubmit2(VkQueue,uint32_t,const VkSubmitInfo2* info,VkFence){
    calls.emplace_back("submit"); ++submitCalls;
    submittedWaits=info->waitSemaphoreInfoCount;submittedSignals=info->signalSemaphoreInfoCount;return queueResult;
}
static VkResult vkQueuePresentKHR(VkQueue,const VkPresentInfoKHR*){calls.emplace_back("present");++presentCalls;return presentResult;}
'''

SWAPCHAIN_SUPPORT = r'''
// Only swapchain allocation is doubled: the production interval policy,
// present-mode selection and recreation scope execute around this boundary.
static bool VK_Device_CreateSwapchain(){
    ++recreateCalls;
    if(!recreateOkay)return false;
    const int interval=VK_Device_RequestedSwapInterval();
    VkPresentModeKHR mode=VK_PRESENT_MODE_FIFO_KHR;
    if(!VK_Device_SelectPresentMode(interval,strict||vkCtx.strictSwapInterval,mode))return false;
    VK_Device_RecordSwapInterval(interval);
    R_DisplayPresentationParameters(0,mode==VK_PRESENT_MODE_FIFO_RELAXED_KHR?-1:mode==VK_PRESENT_MODE_FIFO_KHR?1:0,mode,7);
    return true;
}
'''

GL_SUPPORT = r'''
static bool s_strictSwapIntervalActive=false;
static int s_strictSwapInterval=0,s_strictSwapIntervalCvar=0;
struct {int vidWidth=0,vidHeight=0,uiViewportX=0,uiViewportY=0,uiViewportWidth=0,uiViewportHeight=0;} glConfig;
struct renderModuleWindowInfo_t {int pixelWidth=1280,pixelHeight=720,uiViewportX=0,uiViewportY=0,uiViewportWidth=1280,uiViewportHeight=720;};
enum {RENDER_GLATTR_MULTISAMPLE_BUFFERS,RENDER_GLATTR_MULTISAMPLE_SAMPLES};
struct renderWindowParms_t {};
struct renderWindowState_t {};
static bool glSwapOkay=true,glContextOkay=true,glSetOkay=true,glReadOkay=true,glStrictOkay=true;
static int glSwapCalls=0,glActualInterval=1,glRequestedInterval=1,glBuffers=1,glSamples=4;
struct Services {
    void (*RefreshNativeWindowHandles)(renderModuleWindowInfo_t*)=+[](renderModuleWindowInfo_t* output){*output={};};
    bool (*SwapGLWindow)()=+[]{++glSwapCalls;return glSwapOkay;};
    bool (*GetGLAttribute)(int,int*)=+[](int attr,int* output){*output=attr==RENDER_GLATTR_MULTISAMPLE_BUFFERS?glBuffers:glSamples;return true;};
    bool (*GetGLSwapInterval)(int*)=+[](int* output){*output=glActualInterval;return glReadOkay;};
    bool (*SetGLSwapInterval)(int)=+[](int v){glRequestedInterval=v;return glSetOkay;};
    bool (*ApplyScreenParms)(const renderWindowParms_t*)=+[](const renderWindowParms_t*){return true;};
    bool (*ApplyScreenParmsStrict)(const renderWindowRequest_t*,renderWindowState_t*,char*,int)=+[](const renderWindowRequest_t*,renderWindowState_t*,char*,int){return glStrictOkay;};
} services,*s_glWindowServices=&services;
static void* s_glWindow=(void*)1;
static const char* R_GLVideoError(){return "injected";}
static bool SDL3_EnsureGLContextCurrent(const char*){return glContextOkay;}
static bool SDL3_ApplySwapInterval(){return glSetOkay;}
'''

CASES = r'''
static renderDisplayPresentation_t snapshot(){renderDisplayPresentation_t s{};R_GetDisplayPresentation(&s);return s;}
static void reset(){
    queueResult=presentResult=endResult=flushResult=waitResult=resetResult=beginResult=resetFenceResult=idleResult=acquireResult=uploadResult=VK_SUCCESS;
    recreateOkay=executorOkay=true; calls.clear();recreateCalls=submitCalls=presentCalls=acquireCalls=waitCalls=timingFailures=0;
    r_swapInterval={};request={};strict=loadingBypass=false;
    supportedModes={VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR,VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    vkCtx={};vkExec={}; R_DisplayPresentationBeginDevice();R_DisplayPresentationReady();
}
static void open(){assert(VK_GuiExecutor_BeginFrame());assert(vkExec.frameOpen);}
static void stopped(){
    const auto before=snapshot();const int waits=waitCalls,acquires=acquireCalls,submits=submitCalls;
    assert(!VK_GuiExecutor_BeginFrame());assert(!VK_GuiExecutor_SubmitFrame(true));
    VK_Device_WaitUploadBatch();VK_Device_FlushUploadBatch();
    assert(waitCalls==waits && acquireCalls==acquires && submitCalls==submits);
    const auto after=snapshot();assert(after.presentedSequence==before.presentedSequence && !after.available);
}
int main(){
    static_assert(std::is_standard_layout_v<renderDisplayPresentation_t> && std::is_trivially_copyable_v<renderDisplayPresentation_t>);
    auto s=snapshot();assert(!s.available && s.generation==0);
    R_GetDisplayPresentation(nullptr);
    {renderDisplayChangeScope_t scope(RDP_INIT_FAILED);}
    s=snapshot();assert(!s.available && s.outcome==RDP_INIT_FAILED && s.failureSequence==1);
    {renderDisplayChangeScope_t scope(RDP_INIT_FAILED);R_DisplayPresentationFailed(RDP_CONTEXT_FAILED,-4);}
    s=snapshot();assert(s.failureSequence==2 && s.nativeError==-4 && s.outcome==RDP_CONTEXT_FAILED);
    R_DisplayPresentationShutdown();assert(snapshot().outcome==RDP_CONTEXT_FAILED);
    reset();auto baseline=snapshot();open();assert(VK_GuiExecutor_SubmitFrame(true));s=snapshot();
    assert(s.submittedSequence==baseline.submittedSequence+1 && s.presentedSequence==baseline.presentedSequence+1 && s.outcome==RDP_PRESENTED);
    assert(submittedWaits==1 && submittedSignals==1 && presentCalls==1);
    // Capture-only submission consumes acquisition once; a resumed composition
    // submits without a second acquisition wait and presents exactly once.
    reset();baseline=snapshot();open();assert(VK_GuiExecutor_SubmitFrame(false));
    s=snapshot();assert(s.submittedSequence==baseline.submittedSequence+1 && s.presentedSequence==baseline.presentedSequence && presentCalls==0 && submittedSignals==0);
    vkExec.frameOpen=true;assert(VK_GuiExecutor_SubmitFrame(true));
    assert(submittedWaits==0 && submittedSignals==1 && snapshot().presentedSequence==baseline.presentedSequence+1);
    // Neither OOM nor device loss may synthesize success or reuse a slot.
    for(VkResult failure:{VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_DEVICE_LOST}){
        reset();baseline=snapshot();open();queueResult=failure;
        assert(!VK_GuiExecutor_SubmitFrame(true));s=snapshot();
        assert(vkCtx.presentationBlocked && !vkExec.frameOpen && s.nativeError==failure && s.outcome==RDP_SUBMIT_FAILED);
        assert(s.submittedSequence==baseline.submittedSequence && s.presentedSequence==baseline.presentedSequence && presentCalls==0);stopped();
    }
    reset();baseline=snapshot();open();presentResult=VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(!VK_GuiExecutor_SubmitFrame(true));s=snapshot();
    assert(s.submittedSequence==baseline.submittedSequence+1 && s.presentedSequence==baseline.presentedSequence && s.outcome==RDP_PRESENT_FAILED);stopped();
    for(bool recreate:{false,true}){
        reset();baseline=snapshot();open();presentResult=VK_ERROR_OUT_OF_DATE_KHR;recreateOkay=recreate;
        assert(!VK_GuiExecutor_SubmitFrame(true));s=snapshot();
        assert(recreateCalls==1 && s.generation>baseline.generation && s.presentedSequence==baseline.presentedSequence && s.failureSequence>baseline.failureSequence);
        assert(s.available==recreate);
        reset();baseline=snapshot();open();presentResult=VK_SUBOPTIMAL_KHR;recreateOkay=recreate;
        assert(VK_GuiExecutor_SubmitFrame(true)==recreate);s=snapshot();
        assert(s.presentedSequence==baseline.presentedSequence+1 && s.generation>baseline.generation && s.outcome!=RDP_PRESENTED);
    }
    reset();open();endResult=VK_ERROR_DEVICE_LOST;assert(!VK_GuiExecutor_SubmitFrame(true));
    assert(snapshot().outcome==RDP_RECORD_FAILED && submitCalls==0);stopped();
    reset();open();vkExec.vertexRings[0].mapped=(void*)1;vkExec.vertexRings[0].cursor=16;flushResult=VK_ERROR_DEVICE_LOST;
    assert(!VK_GuiExecutor_SubmitFrame(true));assert(submitCalls==0 && snapshot().outcome==RDP_SUBMIT_FAILED);stopped();
    // A failed image-upload dependency cannot be followed by a successful scene.
    reset();open();vkCtx.uploadBatchOpen=true;vkCtx.numUploadBatchPending=1;uploadResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(!VK_GuiExecutor_SubmitFrame(true));assert(submitCalls==0 && vkCtx.numUploadBatchPending==1);
    for(const auto& call:calls)assert(call!="destroy-upload");stopped();
    reset();vkCtx.uploadBatchInFlight=true;waitResult=VK_ERROR_DEVICE_LOST;
    assert(!VK_GuiExecutor_BeginFrame());assert(acquireCalls==0);stopped();
    reset();vkExec.pipelineTargetFormat=VK_FORMAT_UNDEFINED;idleResult=VK_ERROR_DEVICE_LOST;
    assert(!VK_GuiExecutor_BeginFrame());for(const auto& call:calls)assert(call!="destroy-pipeline");stopped();
    for(int failure=0;failure<3;++failure){
        reset();if(failure==0)resetResult=VK_ERROR_DEVICE_LOST;if(failure==1)beginResult=VK_ERROR_DEVICE_LOST;if(failure==2)resetFenceResult=VK_ERROR_DEVICE_LOST;
        assert(!VK_GuiExecutor_BeginFrame());assert(acquireCalls==1);stopped();
    }
    reset();executorOkay=false;assert(!VK_GuiExecutor_BeginFrame());assert(snapshot().outcome==RDP_INIT_FAILED && acquireCalls==0);
    reset();acquireResult=VK_ERROR_DEVICE_LOST;assert(!VK_GuiExecutor_BeginFrame());assert(snapshot().outcome==RDP_ACQUIRE_FAILED);stopped();
    reset();assert(VK_GuiExecutor_BeginFrame()); // a new lifetime clears the latch
    // The typed interval differs from the archived CVar. Normal frames,
    // out-of-date handling and resize keep it without writing that CVar.
    reset();strict=true;request.swapInterval=0;assert(VK_Device_RecreateSwapchain());strict=false;
    assert(vkCtx.strictSwapInterval && vkCtx.swapInterval==0 && r_swapInterval.value==1);
    open();assert(recreateCalls==1);presentResult=VK_ERROR_OUT_OF_DATE_KHR;
    assert(!VK_GuiExecutor_SubmitFrame(true) && recreateCalls==2 && snapshot().swapInterval==0);
    presentResult=VK_SUCCESS;open();assert(recreateCalls==2);assert(VK_GuiExecutor_SubmitFrame(true));
    assert(VK_Device_RecreateSwapchain() && recreateCalls==3 && snapshot().swapInterval==0);
    assert(r_swapInterval.value==1 && vkCtx.strictSwapInterval);
    // A restore owns its saved interval even when the config and loading
    // bypass disagree. Modified-only invalidation cannot erase ownership.
    r_swapInterval.value=0;strict=true;request.swapInterval=1;assert(VK_Device_RecreateSwapchain());strict=false;
    loadingBypass=true;r_swapInterval.modified=true;
    const int restoredCreates=recreateCalls;open();assert(recreateCalls==restoredCreates && snapshot().swapInterval==1);
    assert(VK_GuiExecutor_SubmitFrame(true));
    // An explicit configured-value change relinquishes the latch, including
    // when its effective loading value happens to equal the previous request.
    r_swapInterval.value=1;open();assert(!vkCtx.strictSwapInterval && snapshot().swapInterval==0);
    assert(VK_GuiExecutor_SubmitFrame(true));loadingBypass=false;
    open();assert(snapshot().swapInterval==1);assert(VK_GuiExecutor_SubmitFrame(true));
    // Adaptive strict requests keep exact present-mode validation on later
    // recreations; a missing mode must fail rather than fall back to FIFO.
    strict=true;request.swapInterval=-1;assert(VK_Device_RecreateSwapchain());strict=false;
    assert(VK_Device_RecreateSwapchain() && snapshot().swapInterval==-1);
    supportedModes={VK_PRESENT_MODE_FIFO_KHR};baseline=snapshot();
    assert(!VK_Device_RecreateSwapchain() && !snapshot().available && snapshot().failureSequence>baseline.failureSequence);
    assert(vkCtx.strictSwapInterval && r_swapInterval.value==1);
    supportedModes={VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    assert(VK_Device_RecreateSwapchain() && snapshot().swapInterval==-1);
    // GL must count only a successful actual swap, and expose queried values.
    reset();baseline=snapshot();GLimp_SwapBuffers();s=snapshot();assert(s.presentedSequence==baseline.presentedSequence+1);
    glSwapOkay=false;GLimp_SwapBuffers();assert(snapshot().presentedSequence==s.presentedSequence && snapshot().outcome==RDP_PRESENT_FAILED);
    glContextOkay=false;const int swaps=glSwapCalls;GLimp_SwapBuffers();assert(glSwapCalls==swaps && snapshot().outcome==RDP_CONTEXT_FAILED);
    glContextOkay=glSwapOkay=true;SDL3_RecordDisplayParameters();s=snapshot();assert(s.samples==4 && s.swapInterval==1 && s.parametersValid==3 && s.presentMode==-1);
    glBuffers=0;SDL3_RecordDisplayParameters();assert(snapshot().samples==0);
    request.swapInterval=-1;glActualInterval=1;assert(!SDL3_ApplyStrictSwapInterval());assert(glRequestedInterval==-1);
    glActualInterval=-1;assert(SDL3_ApplyStrictSwapInterval());glReadOkay=false;assert(!SDL3_ApplyStrictSwapInterval());
    strict=true;glStrictOkay=false;assert(!SDL3_ApplyRequestedScreenParms({}));assert(snapshot().outcome==RDP_SCREEN_FAILED);
    R_DisplayPresentationShutdown();baseline=snapshot();R_DisplayPresentationPresented();assert(snapshot().presentedSequence==baseline.presentedSequence);
}
'''


def main():
    gui = (RENDERER / "Vulkan/vk_GuiExecutor.cpp").read_text(encoding="utf-8")
    device = (RENDERER / "Vulkan/VulkanDevice.cpp").read_text(encoding="utf-8")
    gl = (RENDERER / "OpenGL/gl_ContextSDL3.cpp").read_text(encoding="utf-8")
    fields = "\n".join(f"    int num{name}=0; Pipeline {name[0].lower()+name[1:]}[2]{{}};" for name in
                       ("Pipelines", "ScreenPipelines", "CubePipelines", "EnvPipelines", "ProgramPipelines", "SpecialPipelines", "BlendLightPipelines", "TemporalResolvePipelines"))
    source = SUPPORT.replace("PIPELINE_FIELDS", fields)
    source += method(device, "int VK_Device_RequestedSwapInterval( void )")
    source += method(device, "static void VK_Device_RecordSwapInterval( int interval )")
    source += method(device, "static bool VK_Device_SelectPresentMode( int requestedInterval, bool strict, VkPresentModeKHR &selected )")
    source += SWAPCHAIN_SUPPORT
    source += method(device, "void VK_Device_BlockPresentation( renderDisplayOutcome_t outcome, VkResult error, const char *operation )")
    source += method(device, "bool VK_Device_RecreateSwapchain( void )")
    source += method(device, "void VK_Device_WaitUploadBatch( void )")
    source += method(device, "void VK_Device_FlushUploadBatch( void )")
    source += method(gui, "static bool VK_GuiExecutor_BeginFrame( void )")
    source += method(gui, "static bool VK_GuiExecutor_SubmitFrame( bool present )")
    source += GL_SUPPORT
    for signature in ("static int SDL3_RequestedSwapInterval()", "static void SDL3_RecordDisplayParameters()", "static bool SDL3_ApplyRequestedScreenParms(const renderWindowParms_t& parms)", "static bool SDL3_ApplyStrictSwapInterval()", "void GLimp_SwapBuffers(void)"):
        source += method(gl, signature)
    source += CASES
    # The full screenshot method depends on image formats and CPU copy code;
    # retain a narrow lifetime assertion beside the executed submit scenarios.
    readback = function_body(gui, "bool VK_GuiExecutor_ReadPixels(")
    failed_submit = readback.split("if ( !VK_GuiExecutor_SubmitFrame( !resumeAfterReadback ) ) {", 1)[1].split("return false;", 1)[0]
    assert "VK_Device_DeferDestroy" in failed_submit and "vmaDestroyBuffer" not in failed_submit
    recreate = function_body(device, "bool VK_Device_RecreateSwapchain(")
    assert recreate.index("presentationBlocked") < recreate.index("vkDeviceWaitIdle")
    clear = function_body(device, "void VK_Device_PresentClearFrame(")
    assert clear.index("presentationBlocked") < clear.index("vkWaitForFences")
    assert "VK_Device_BlockPresentation( RDP_SUBMIT_FAILED" in clear
    assert "VK_Device_RequestedSwapInterval() != vkCtx.swapInterval" in clear
    create = method(device, "static bool VK_Device_CreateSwapchain( void )")
    assert create.index("VK_Device_RequestedSwapInterval()") < create.index("VK_Device_SelectPresentMode(")
    assert "R_IsRecoverableRendererRestart() || vkCtx.strictSwapInterval" in create
    assert create.index("VK_Device_CreateDepthImages()") < create.index("VK_Device_RecordSwapInterval( requestedInterval )")
    sdk = Path(os.environ.get("VULKAN_SDK", ""))
    includes = [sdk / "Include", sdk / "include", Path("/usr/include"),
                *sorted((ROOT / "subprojects").glob("SDL3-*/src/video/khronos"))]
    include = next((path for path in includes if (path / "vulkan/vulkan_core.h").is_file()), None)
    if include is None:
        raise RuntimeError("Vulkan headers are required (VULKAN_SDK or configured SDL3 subproject)")
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="presentation-", dir=ROOT / ".tmp") as directory:
        temp = Path(directory)
        cpp = temp / "presentation.cpp"
        cpp.write_text(source, encoding="utf-8")
        exe = temp / ("presentation.exe" if os.name == "nt" else "presentation")
        subprocess.run([compiler, "-std=c++20", "-I", str(RENDERER), "-I", str(include), str(cpp), str(RENDERER / "DisplayPresentation.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("Display presentation: real tracker, GL swap/strict readback, Vulkan applied swap policy/restore/legacy handoff, scene/capture submit, present/recreate failures, upload dependencies and no-reuse latch passed")


if __name__ == "__main__":
    main()
