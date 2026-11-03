#!/usr/bin/env python3
"""The Vulkan render thread's stage-1 ownership contract (REN-07, r_vkRenderThread).

EndFrame hands a frame to the render thread, which records, submits and presents
it while the main thread runs the next game tic. Until R_RenderThread_Sync
completes the frame, the render thread owns its frame memory, command chain and
vertex-cache temps and the Vulkan queue, command pool and upload batch, and the
backend may still read the entity and light defs that frame drew. Every
main-thread entry that would touch that state must sync first, or queue its
change until the sync. A new entry that skips the sync races the backend; these
pins keep the existing ones from being dropped.
"""
from __future__ import annotations

import sys
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
    require(function_body(source, signature), SYNC, f"{context} ({signature})")


def check_thread() -> None:
    thread = read(RENDERER / "Vulkan" / "vk_RenderThread.cpp")
    require(thread, 'idCVar r_vkRenderThread( "r_vkRenderThread", "0"', "the prototype stays opt-in")
    begin = function_body(thread, "bool R_RenderThread_Begin(")
    require(begin, "R_RenderThread_FrameQualifies()", "hand-off gate")
    require_order(begin, "VK_Backend_PrepareThreadedFrame();", "rt_wake.notify_one();",
                  "the main thread polls the window before the render thread presents")
    sync = function_body(thread, "void R_RenderThread_Sync(")
    require(sync, "R_RenderThread_OnRenderThread()", "the render thread never waits on itself")
    require_order(sync, "tr.CompleteBackEndFrame();", 'common->Error( "render thread: %s", failure );',
                  "a backend error is re-raised on the main thread after the frame completes")
    require(function_body(thread, "static void R_RenderThread_Main("), "catch ( idException &ex )",
            "a backend error unwinds only the render thread")
    require(function_body(thread, "void R_RenderThread_Shutdown("), "R_RenderThread_OnRenderThread()",
            "a fatal error raised on the render thread cannot join it")


def check_frame() -> None:
    system = read(RENDERER / "RenderSystem.cpp")
    begin = function_body(system, "void idRenderSystemLocal::BeginFrame(")
    require_order(begin, 'R_RenderThread_Sync( "frame" );', "frameCount++;",
                  "the next front end starts after the frame in flight completes")
    end = function_body(system, "void idRenderSystemLocal::EndFrame(")
    require(end, "R_IssueRenderCommandsOnRenderThread()", "EndFrame hand-off")
    require(end, "CompleteBackEndFrame();", "inline frames complete in EndFrame")
    complete = function_body(system, "void idRenderSystemLocal::CompleteBackEndFrame(")
    require_order(complete, "R_ToggleSmpFrame();", "ApplyPendingDefOps();",
                  "queued render-world changes apply after the frame's memory is released")
    gate = function_body(system, "bool R_RenderThread_FrameQualifies(")
    for needle in ("tr.takingScreenshot", "tr.tiledViewport[0]", "R_LoadingScreenActive()",
                   "SPECIAL_EFFECT_AL", "R_ImagePolicyActive()"):
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
                      "vertCache_t	*idVertexCache::AllocFrameTemp(", "void idVertexCache::PurgeAll("):
        require_sync(cache, signature, "the vertex cache")
    require_sync(read(RENDERER / "tr_light.cpp"), "idRenderModel *R_EntityDefDynamicModel(",
                 "dynamic snapshots rebuilt in place")


def check_world() -> None:
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
    require_sync(backend, "bool GLimp_SetScreenParms(", "screen parameters")
    require_sync(backend, "bool idRenderTexture::Resize(", "render textures")
    image = read(RENDERER / "Vulkan" / "vk_Image.cpp")
    for signature in ("void idImage::PurgeImage(", "void idImage::AllocImage(", "void idImage::SubImageUpload(",
                      "void idImage::SetTexParameters(", "void idImage::Resize("):
        require_sync(image, signature, "images")
    device = read(RENDERER / "Vulkan" / "VulkanDevice.cpp")
    for signature in ("void VK_Device_WaitUploadBatch(", "void VK_Device_FlushUploadBatch(",
                      "bool VK_Device_BatchedUpload(", "void VK_Device_DeferDestroy(",
                      "void VK_Device_WaitIdleForTiming(", "bool VK_Device_RecreateSwapchain("):
        require_sync(device, signature, "the queue, command pool and upload batch")
    executor = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    for signature in ("bool VK_GuiExecutor_BeginFrame( void ) {", "bool VK_Exec_SetRenderTarget( idRenderTexture",
                      "bool VK_GuiExecutor_ReadPixels(", "bool VK_GuiExecutor_EndFrameAndPresent( void ) {"):
        require_sync(executor, signature, "main-thread recording")
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
