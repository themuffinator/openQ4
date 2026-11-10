#!/usr/bin/env python3
"""Render-thread ownership barriers for tic-only and full-front-end overlap.

Only explicitly listed producer entries may use the mode-aware barrier. Device,
resource mutation, debug and teardown paths must still join the worker.
"""
from __future__ import annotations

import sys
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))
from filesystem_case_segments import function_body, require_order  # noqa: E402

RENDERER = ROOT / "src" / "renderer"
SYNC = "R_RenderThread_Sync("


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"{context}: missing {needle!r}")


def require_sync(source: str, signature: str, context: str) -> None:
    body = function_body(source, signature)
    producer_entries = {
        "void *R_GetCommandBuffer(", "void *R_FrameAlloc(",
        "void idVertexCache::Alloc(", "void idVertexCache::Free(", "void idVertexCache::Touch(",
        "vertCache_t *idVertexCache::AllocFrameTemp(", "idRenderModel *R_EntityDefDynamicModel(",
        "bool idRenderWorldLocal::ModelTrace(", "bool idRenderWorldLocal::Trace(",
        "guiPoint_t\tidRenderWorldLocal::GuiTrace(", "void idRenderWorldLocal::RenderScene(",
    }
    expected = "R_RenderThread_SyncFrontEnd(" if signature in producer_entries else SYNC
    if expected not in body:
        raise AssertionError(f"{context} ({signature}): missing ownership barrier")


def check_thread() -> None:
    thread = read(RENDERER / "Vulkan" / "vk_RenderThread.cpp")
    require(thread, 'idCVar r_vkRenderThread( "r_vkRenderThread", "0"', "the prototype stays opt-in")
    begin = function_body(thread, "bool R_RenderThread_Begin(")
    require(begin, "R_RenderThread_FrameQualifies()", "hand-off gate")
    require_order(begin, "VK_Backend_PrepareThreadedFrame();", "rt_wake.notify_one();",
                  "the main thread polls the window before the render thread presents")
    require_order(begin, "R_FreeDeferredFrameSurfaces();", "rt_wake.notify_one();",
                  "shared deferred surfaces retire before the worker can append to its list")
    sync = function_body(thread, "void R_RenderThread_Sync(")
    require(sync, "R_RenderThread_OnRenderThread()", "the render thread never waits on itself")
    require_order(sync, "rt_done.wait(", "rt_inFlight.store( false",
                  "ownership ends only after collecting the worker result")
    require_order(sync, "rt_inFlight.store( false", "tr.CompleteBackEndFrame();",
                  "completion can reenter Sync without retiring the frame twice")
    require_order(sync, "tr.CompleteBackEndFrame();", 'common->Error( "render thread: %s", failure );',
                  "a backend error is re-raised on the main thread after the frame completes")
    require(function_body(thread, "static void R_RenderThread_Main("), "catch ( idException &ex )",
            "a backend error unwinds only the render thread")
    require(function_body(thread, "void R_RenderThread_Shutdown("), "R_RenderThread_OnRenderThread()",
            "a fatal error raised on the render thread cannot join it")


def check_frame() -> None:
    system = read(RENDERER / "RenderSystem.cpp")
    begin = function_body(system, "void idRenderSystemLocal::BeginFrame(")
    require_order(begin, 'R_RenderThread_SyncFrontEnd( "frame" );', "frameCount++;",
                  "the next front end checks submitted ownership before using its own arena")
    end = function_body(system, "void idRenderSystemLocal::EndFrame(")
    require(end, "R_IssueRenderCommandsOnRenderThread()", "EndFrame hand-off")
    require(end, "CompleteBackEndFrame();", "inline frames complete in EndFrame")
    require_order(function_body(system, "static bool R_IssueRenderCommandsOnRenderThread("),
                  "if ( !R_RenderThread_Begin(", "R_ScenePackets_SealFrame();",
                  "inline reconstruction keeps previous history until retirement")
    require_order(end, "const int previousBackEndMsec = backEnd.pc.msec;", "R_IssueRenderCommandsOnRenderThread()",
                  "timing readback must not race the worker's counters")
    debug = read(RENDERER / "tr_rendertools.cpp")
    for name in ("ClearDebugText", "AddDebugText", "ClearDebugLines", "AddDebugLine", "ClearDebugPolygons", "AddDebugPolygon"):
        require_sync(debug, "void RB_" + name + "(", "debug primitives shared with the backend")
    complete = function_body(system, "void idRenderSystemLocal::CompleteBackEndFrame(")
    require_order(complete, "R_ToggleSmpFrame();", "ApplyPendingDefOps();",
                  "queued render-world changes apply after the frame's memory is released")
    gate = function_body(system, "bool R_RenderThread_FrameQualifies(")
    for needle in ("tr.takingScreenshot", "!tr.vrFrame.active", "tr.tiledViewport[0]", "R_LoadingScreenActive()",
                   "SPECIAL_EFFECT_AL", "R_ImagePolicyActive()", "!view->frameSnapshot"):
        require(gate, needle, "frames that read back, load or walk live lights stay inline")
    require_sync(system, "void *R_GetCommandBuffer(", "the command chain")
    require_sync(system, "static void R_IssueRenderCommands(", "inline flushes")
    for signature in ("bool idRenderSystemLocal::BeginSavePreview(", "int idRenderSystemLocal::PollSavePreview(",
                      "void idRenderSystemLocal::CancelSavePreview(", "bool idRenderSystemLocal::UploadImage(",
                      "idImage* idRenderSystemLocal::CreateImage(", "bool idRenderSystemLocal::ResizeRenderTexture(",
                      "void idRenderSystemLocal::DestroyRenderTexture(", "void idRenderSystemLocal::FreeRenderWorld("):
        require_sync(system, signature, "render system entry")

    main = read(RENDERER / "tr_main.cpp")
    require_sync(main, "void *R_FrameAlloc(", "frame memory")
    require_sync(main, "void R_ToggleSmpFrame(", "frame memory reset")
    cache = read(RENDERER / "VertexCache.cpp")
    for signature in ("void idVertexCache::Alloc(", "void idVertexCache::Free(", "void idVertexCache::Touch(",
                      "vertCache_t *idVertexCache::AllocFrameTemp(", "void idVertexCache::PurgeAll("):
        require_sync(cache, signature, "the vertex cache")
    require_sync(read(RENDERER / "tr_light.cpp"), "idRenderModel *R_EntityDefDynamicModel(",
                 "dynamic snapshots rebuilt in place")


def check_world() -> None:
    for path, signature in (("RenderWorld_load.cpp", "void idRenderWorldLocal::FreeDefs("),
                            ("tr_lightrun.cpp", "void R_FreeDerivedData("),
                            ("ModelManager.cpp", "void idRenderModelManagerLocal::FreeModelVertexCaches("),
                            ("RenderSystem_init.cpp", "void R_VidRestart_f("),
                            ("RenderSystem_init.cpp", "static bool R_TryFullVidRestartInternal(")):
        body = function_body(read(RENDERER / path), signature)
        require_sync(read(RENDERER / path), signature, "bulk teardown retires before clearing live owners")
        first = body[body.index("{") + 1:]
        first = re.sub(r"//[^\n]*", "", first).strip()
        assert first.startswith("R_RenderThread_Sync("), signature
    world = read(RENDERER / "RenderWorld.cpp")
    for signature, op in (("void idRenderWorldLocal::UpdateEntityDef(", "PENDING_UPDATE_ENTITY"),
                          ("void idRenderWorldLocal::FreeEntityDef(", "PENDING_FREE_ENTITY"),
                          ("void idRenderWorldLocal::UpdateLightDef(", "PENDING_UPDATE_LIGHT"),
                          ("void idRenderWorldLocal::FreeLightDef(", "PENDING_FREE_LIGHT"),
                          ("void idRenderWorldLocal::ProjectDecalOntoWorld(", "PENDING_DECAL_ONTO_WORLD"),
                          ("void idRenderWorldLocal::ProjectDecal(", "PENDING_DECAL"),
                          ("void idRenderWorldLocal::ProjectOverlay(", "PENDING_OVERLAY"),
                          ("void idRenderWorldLocal::RemoveDecals(", "PENDING_REMOVE_DECALS")):
        require(function_body(world, signature), f"QueueDefOp( {op}", f"{signature} queues while a frame is in flight")
    for signature in ("bool idRenderWorldLocal::ModelTrace(", "bool idRenderWorldLocal::Trace(",
                      "guiPoint_t	idRenderWorldLocal::GuiTrace(", "void idRenderWorldLocal::RenderScene("):
        require_sync(world, signature, "traces see what the game last wrote")
    for signature in ("bool idRenderWorldLocal::UpdateEffectDef(", "void idRenderWorldLocal::FreeEffectDef("):
        body = function_body(world, signature)
        require(body, "DeleteEffectModel(", f"{signature} defers effect snapshots")
        if "delete def->dynamicModel" in body or "delete effectsDef[effectHandle]->dynamicModel" in body:
            raise AssertionError(f"{signature} deletes an effect snapshot the frame in flight may draw")
    require(function_body(world, "void idRenderWorldLocal::FreeEntityDef("), "deferredFreeEntityDefs.Append( def );",
            "an entity def the frame in flight drew outlives it")


def check_vulkan() -> None:
    backend = read(RENDERER / "Vulkan" / "vk_Backend.cpp")
    swap = function_body(backend, "void GLimp_SwapBuffers(")
    require(swap, "R_RenderThread_OnRenderThread()", "SDL and recovery stay on the main thread")
    require(swap, "vkThreadedWindowInfo", "a threaded frame uses the hand-off's window poll")
    require(function_body(backend, "void VK_Backend_CompleteThreadedFrame("), "VK_Device_CompleteThreadedRecovery();",
            "window/surface recovery runs after collecting the worker")
    require_sync(backend, "bool GLimp_SetScreenParms(", "screen parameters")
    require_sync(backend, "bool idRenderTexture::Resize(", "render textures")
    image = read(RENDERER / "Vulkan" / "vk_Image.cpp")
    for signature in ("void idImage::PurgeImage(", "void idImage::AllocImage(", "void idImage::SubImageUpload(",
                      "void idImage::SetTexParameters(", "void idImage::Resize("):
        require_sync(image, signature, "images")
    device = read(RENDERER / "Vulkan" / "VulkanDevice.cpp")
    for signature in ("bool VK_Device_RecreateSwapchain(", "bool VK_Device_RecoverSurface("):
        recovery = function_body(device, signature)
        require_order(recovery, "R_RenderThread_OnRenderThread()", "vkDeviceWaitIdle(",
                      "the worker defers recovery without window calls or device teardown")
        require(recovery, "vkThreadedRecreatePending = true;", "deferred recovery receipt")
    for signature in ("void VK_Device_WaitUploadBatch(", "void VK_Device_FlushUploadBatch(",
                      "bool VK_Device_BatchedUpload(", "void VK_Device_DeferDestroy(",
                      "void VK_Device_WaitIdleForTiming(", "bool VK_Device_RecreateSwapchain("):
        require_sync(device, signature, "the queue, command pool and upload batch")
    executor = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    for signature in ("bool VK_GuiExecutor_BeginFrame( void ) {", "bool VK_Exec_SetRenderTarget( idRenderTexture",
                      "bool VK_GuiExecutor_ReadPixels(", "bool VK_GuiExecutor_EndFrameAndPresent( void ) {"):
        require_sync(executor, signature, "main-thread recording")
    manager = read(RENDERER / "ImageManager.cpp")
    for signature in ("idImage *idImageManager::ImageFromFunction(",
                      "idImage\t*idImageManager::GetImageWithParameters(",
                      "idImage\t*idImageManager::ImageFromFile(",
                      "idImage *idImageManager::ImageHandleDeferred(",
                      "idImage * idImageManager::ScratchImage("):
        require_sync(manager, signature, "registry access joins backend scratch creation")
    require_order(function_body(manager, "idImage *idImageManager::GetImage("),
                  "registryLock( imageRegistryMutex )", "imageHash.First(", "name-only registry lookup is locked")
    require_order(function_body(manager, "idImage *idImageManager::AllocImage("),
                  'R_RenderThread_Sync( "image" )', "registryLock( imageRegistryMutex )", "never join while holding the registry lock")
    image_load = read(RENDERER / "Image_load.cpp")
    for signature in ("void idImage::ActuallyLoadImage(", "void idImage::SetSamplerState("):
        require_sync(image_load, signature, "content barriers precede shared-image mutation")
    require_sync(read(RENDERER / "Material.cpp"), "void idMaterial::FreeData(", "material lifetime")
    cvars = read(ROOT / "src/framework/CVarSystem.cpp")
    for signature in ("void idInternalCVar::Set(", "void idInternalCVar::Reset("):
        require_order(function_body(cvars, signature), "R_RendererModule_SyncConfiguration();",
                      "CVar_AssignString(", "renderer settings retire their reader before mutation")
    require(function_body(read(RENDERER / "RendererGLModule.cpp"), "static void RGM_SyncConfiguration("),
            'R_RenderThread_Sync( "renderer configuration" );', "module configuration barrier")
    require(read(RENDERER / "RenderModuleAPI.h"), "(*SyncConfiguration)(void)", "configuration export")
    local = read(RENDERER / "tr_local.h")
    require(local, "ID_INLINE void	R_RenderThread_Sync( const char * ) {}", "other modules run the backend inline")


def check_console() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    vprintf = function_body(common, "void idCommonLocal::VPrintf(")
    require_order(vprintf, "Common_QueueOffMainThread( false, msg )", "console->Print( msg );",
                  "prints from the render thread queue for the main thread")
    require(function_body(common, "void idCommonLocal::Warning("), "Common_QueueOffMainThread( true, msg )",
            "warnings from the render thread queue for the main thread")
    error = function_body(common, "void idCommonLocal::Error(")
    require_order(error, "throw idException( msg );", "com_refreshOnPrint = false;",
                  "an error off the main thread unwinds before touching main-thread state")


def main() -> int:
    check_thread()
    check_frame()
    check_world()
    check_vulkan()
    check_console()
    print("renderer_render_thread_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
