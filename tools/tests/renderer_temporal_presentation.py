#!/usr/bin/env python3
"""Static contracts for Milestone E temporal presentation and dynamic resolution."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", str(ROOT))
).resolve()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing required source: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {label}: {token}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body for: {signature}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated body for: {signature}")


def main() -> int:
    public_header = read(RENDERER / "RenderSystem.h")
    companion_header = read(GAME_ROOT / "src" / "renderer" / "RenderSystem.h")
    if public_header != companion_header:
        raise AssertionError("engine and companion RenderSystem.h files are not byte-identical")
    for token in (
        "renderPresentationState_s",
        "sceneWidth;",
        "outputWidth;",
        "historyGeneration;",
        "GetPresentationState( renderPresentationState_t &state ) const",
        "ResolveTemporalPresentation(",
    ):
        require(public_header, token, "shared renderer ABI")

    require(
        read(RENDERER / "RenderModuleAPI.h"),
        "#define RENDER_API_VERSION\t\t\t33",
        "renderer ABI v33",
    )
    require(
        read(GAME_ROOT / "src" / "game" / "Game.h"),
        "const int GAME_API_VERSION\t\t= 58;",
        "game ABI v55",
    )

    render_init = read(RENDERER / "RenderSystem_init.cpp")
    require(
        render_init,
        'idCVar r_rendererDynamicResolution( "r_rendererDynamicResolution", "0"',
        "default-off dynamic resolution",
    )
    temporal = read(RENDERER / "TemporalPresentation.cpp")
    for token in (
        # automatic TAA: upscales only below native resolution
        'idCVar r_temporalAA( "r_temporalAA", "2"',
        "R_TemporalPresentation_AutomaticTemporalAAWanted(",
        "defined( OPENQ4_RENDERER_GLES_MODULE )",
        "glConfig.GLSL130Available",
        "r_screenFraction.GetInteger() < 100 && ( scaleMode == 1 || scaleMode == 2 )",
        "R_TemporalPresentation_CapturePreviousPositions(",
        "tri->positionsFrame == frame - 1",
        "R_TemporalPresentation_PreviousPositionCache(",
        "R_TemporalPresentation_RecordOwnership(",
        'idCVar r_temporalAASharpness( "r_temporalAASharpness", "0.5"',
        'idCVar r_dynamicResolutionCaptureNative( "r_dynamicResolutionCaptureNative", "0"',
        "R_TemporalPresentation_FindSampleScale(",
        "eligibleForFeedback",
        "TemporalResolutionCore_Update(",
        "R_TemporalPresentation_RecordFrameScale(",
        "R_TemporalPresentation_InvalidateHistory(",
        "R_TemporalPresentation_MarkCurrentFrameCapture(",
        "viewDef->isSubview",
        "TemporalHistoryCore_Jitter(",
        "TemporalHistoryCore_ValidateCameraHistory(",
        "viewDef->renderView.forceUpdate",
        "TEMPORAL_HISTORY_RESET_EXPLICIT_CUT",
    ):
        require(temporal, token, "temporal frontend policy")
    capture_body = function_body(
        temporal, "void R_TemporalPresentation_MarkCurrentFrameCapture("
    )
    require(capture_body, "entry.eligibleForFeedback = false;", "capture timing rejection")
    require(capture_body, "R_TemporalPresentation_InvalidateHistory(", "late-capture history reset")

    resolution_core = read(RENDERER / "TemporalPresentationCore.h")
    for token in (
        "maximumSampleAgeFrames",
        "TemporalResolutionCore_SampleIsNewer(",
        "sample.scalePercent - config.dropStepPercent",
        "state.belowBudgetFrames >= config.raiseFrames",
        "TemporalResolutionCore_AlignedDimension(",
    ):
        require(resolution_core, token, "dynamic-resolution controller")

    history_core = read(RENDERER / "TemporalHistoryCore.h")
    for domain in (
        "STATIC_WORLD",
        "RIGID",
        "SKINNED",
        "PARTICLE",
        "DEFORM",
        "SUBVIEW",
        "IN_WORLD_GUI",
        "VIEW_MODEL",
    ):
        require(
            history_core,
            f"TEMPORAL_MOTION_DOMAIN_{domain}",
            f"{domain.lower()} motion ownership",
        )
    for token in (
        "TEMPORAL_MOTION_OWNERSHIP_REACTIVE",
        "TEMPORAL_MOTION_OWNERSHIP_DISOCCLUSION_TEST",
        "TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY",
        "TEMPORAL_MOTION_OWNERSHIP_DEPTH_HACK",
        "current.outputWidth != previous.outputWidth",
        "current.renderTimeMsec < previous.renderTimeMsec",
        "TEMPORAL_MAX_REACTIVE_REGIONS = 2",
        "TemporalHistoryCore_AddMotionOwnership(",
        "backendExactMotionDomainMask",
        "unsupportedObjectMotion",
    ):
        require(history_core, token, "temporal history policy")
    if "current.sceneWidth != previous.sceneWidth" in function_body(
        history_core, "TemporalHistoryCore_ValidateCameraHistory("
    ):
        raise AssertionError("scene-only scale changes must preserve native history")

    scene_packets = read(RENDERER / "ScenePackets.cpp")
    for token in (
        "R_TemporalPresentation_TemporalAARequested()",
        "TemporalHistoryCore_ClassifyMotion( temporalInput )",
        "hasPreviousModelMatrix",
        "TEMPORAL_MOTION_OWNERSHIP_SEPARATE_HISTORY",
        "R_ScenePackets_BuildTemporalViewMotionPolicy(",
        "R_ScenePackets_TemporalRigidMotionEligible(",
        "packet.temporalExactRigidEligible",
        "TemporalHistoryCore_AddMotionOwnership( policy,",
    ):
        require(scene_packets, token, "scene-packet temporal ownership")

    for camera_source in (
        GAME_ROOT / "src" / "game" / "Camera.cpp",
        GAME_ROOT / "src" / "mpgame" / "Camera.cpp",
    ):
        camera = read(camera_source)
        require(camera, "view->forceUpdate = prevCut != cut;", "authored camera-cut signal")
        require(camera, "cameraDef->GetAnim(1)->GetCut( i )", "authored cut traversal")

    metrics = read(RENDERER / "RendererMetrics.cpp")
    gl_timing = function_body(metrics, "void R_RendererMetrics_BeginGpuBackendFrame(")
    require(gl_timing, "r_rendererDynamicResolution.GetBool()", "automatic GL timing")
    require(metrics, "GL_QUERY_RESULT_AVAILABLE", "nonblocking GL timing poll")
    vk_timing = read(RENDERER / "Vulkan" / "VulkanGpuFrameTiming.cpp")
    require(vk_timing, "r_rendererDynamicResolution.GetBool()", "automatic Vulkan timing")
    if "VK_QUERY_RESULT_WAIT_BIT" in vk_timing.replace(
        "Deliberately no VK_QUERY_RESULT_WAIT_BIT", ""
    ):
        raise AssertionError("Vulkan dynamic timing must not use WAIT_BIT")

    render_system = read(RENDERER / "RenderSystem.cpp")
    legacy_crop = function_body(
        render_system, "bool idRenderSystemLocal::PushSceneResolutionScale("
    )
    for token in (
        "R_TemporalPresentation_DynamicResolutionRequested()",
        "R_TemporalPresentation_TemporalAARequested()",
        "const bool legacyCrop = r_resolutionScaleMode.GetInteger() == 0;",
    ):
        require(legacy_crop, token, "legacy-crop temporal bypass")
    enqueue = function_body(
        render_system, "bool idRenderSystemLocal::ResolveTemporalPresentation("
    )
    for token in (
        "tr.takingScreenshot",
        "presentation.captureFrozen",
        "presentation.captureForcedNative",
        "primaryView->temporalCaptureFrame",
        "RC_RESOLVE_TEMPORAL_PRESENTATION",
        "cmd->historyGeneration = historyGeneration;",
    ):
        require(enqueue, token, "validated temporal command enqueue")
    require(
        read(RENDERER / "tr_backend.cpp"),
        "case RC_RESOLVE_TEMPORAL_PRESENTATION:",
        "OpenGL temporal dispatch",
    )

    gl_backend = read(RENDERER / "draw_common.cpp")
    gl_scene_request = function_body(
        gl_backend, "static bool RB_ScaledSceneTargetRequested("
    )
    for token in (
        "r_resolutionScaleMode.GetInteger() == 0",
        "!R_TemporalPresentation_TemporalAARequested()",
    ):
        require(gl_scene_request, token, "OpenGL manual-scale temporal ownership")
    gl_draw_view = function_body(gl_backend, "RB_STD_DrawView( void )")
    for token in (
        "const bool sceneRenderTargetReady =",
        "rootSceneRenderTargetRequested && !sceneRenderTargetReady",
        "RB_RecenterDirectTemporalProjection( sceneScaleState, backEnd.viewDef );",
        "RB_RestoreDirectTemporalProjection( sceneScaleState, backEnd.viewDef );",
    ):
        require(gl_draw_view, token, "OpenGL direct temporal ownership fallback")
    recenter_at = gl_draw_view.find("RB_RecenterDirectTemporalProjection(")
    draw_at = gl_draw_view.find("RB_BeginDrawingView();")
    if recenter_at < 0 or draw_at <= recenter_at:
        raise AssertionError("OpenGL direct fallback must recenter before world drawing")
    restore_at = gl_draw_view.find("RB_RestoreSceneScaling( sceneScaleState );")
    present_at = gl_draw_view.find("RB_PresentBackendTemporalScene()")
    if restore_at < 0 or present_at <= restore_at:
        raise AssertionError(
            "OpenGL reactive policy must see restored native view/scissor coordinates"
        )
    # REN-04: a subview copies its parent view; it must not inherit the
    # parent's temporal preparation (jitter, identity), or Vulkan's direct
    # render of a mirror retires the main view's history every frame.
    subview = read(RENDERER / "tr_subview.cpp")
    reset = function_body(subview, "static void R_ResetSubviewTemporalState( viewDef_t *parms )")
    for token in ("temporalPrepared = false", "temporalJitterEnabled = false"):
        require(reset, token, "subview temporal reset")
    if subview.count("R_ResetSubviewTemporalState( parms );") != 4:
        raise AssertionError("every subview constructor must reset inherited temporal state")
    vk_exec = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    for token in ("temporalDepthStamps[ VK_TEMPORAL_DEPTH_STAMP_COUNT ]",
                  "VK_TemporalPresentation_FindDepthStamp(",
                  "VK_TemporalPresentation_DepthCurrentForCapture(",
                  "vkExec.acquireTimedOut && vkExec.acquireTimeoutFrame == backEnd.frameCount"):
        require(vk_exec, token, "Vulkan per-target depth proof and acquire-timeout latch")
    # The public entry records the outcome (REN-04); the resolve itself is here.
    # REN-04: every resolve, on both backends, records how it ended.
    gl_entry = function_body(gl_backend, "bool RB_ResolveTemporalPresentation( const resolveTemporalPresentationCommand_t &command ) {")
    require(gl_entry, "R_TemporalPresentation_RecordResolve(", "OpenGL resolve outcome record")
    vk_entry_source = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    vk_entry_signature = "static bool VK_TemporalPresentation_ResolveTargets(\r\n" if "\r\n" in vk_entry_source else "static bool VK_TemporalPresentation_ResolveTargets(\n"
    vk_entry = function_body(vk_entry_source[vk_entry_source.rfind(vk_entry_signature):], vk_entry_signature)
    require(vk_entry, "R_TemporalPresentation_RecordResolve(", "Vulkan resolve outcome record")
    status = function_body(temporal, "void R_TemporalPresentation_PrintStatus_f(")
    for token in ("\"reset\"", "Temporal resolve:", "Temporal view history:",
                  "Temporal resolve fallbacks", "Temporal history invalidations"):
        require(status, token, "cumulative temporal status")
    gl_resolve = function_body(gl_backend, "static bool RB_ResolveTemporalPresentationRecorded(")
    for token in (
        "captureFrame",
        "RB_RenderMotionVectorBuffer(",
        "RB_DrawTemporalResolvePass(",
        "RB_PresentTemporalSpatialFallback(",
        "R_ScenePackets_BuildTemporalViewMotionPolicy(",
        "exactMotionDomains",
    ):
        require(gl_resolve, token, "OpenGL temporal resolve")
    for token in (
        "neighborhoodMin",
        "historyClamped",
        "depthReactive",
        "historyWeight",
        "ReactiveRegion0",
        "packetReactive",
        # motion blur shares the exact per-surface classification (REN-08)
        "R_ScenePackets_TemporalSurfaceMotion(",
    ):
        require(gl_backend, token, "OpenGL disocclusion/reactive resolve")

    gl_swap_scale = function_body(
        gl_backend, "void RB_ApplyResolutionScaleToBackBuffer( void )"
    )
    # No completed-frame scaler is safe here, even when a UI-only frame has
    # neither a temporal request nor a world-present marker. The native test
    # renderer_native_ui_output.py executes this hook through RB_SwapBuffers
    # alongside the real scene-size and spatial-present methods.
    executable_scale_body = re.sub(r"//[^\n]*|/\*.*?\*/", "", gl_swap_scale, flags=re.S)
    if executable_scale_body.strip():
        raise AssertionError("OpenGL swap tail must not filter the native UI backbuffer")

    vk_backend = read(RENDERER / "Vulkan" / "vk_Backend.cpp")
    require(vk_backend, "RC_RESOLVE_TEMPORAL_PRESENTATION", "Vulkan temporal dispatch")
    vk_gui = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    vk_scene_request = function_body(
        vk_gui, "static bool VK_TemporalPresentation_BackendSceneRequested("
    )
    for token in (
        "r_resolutionScaleMode.GetInteger() == 0",
        "!R_TemporalPresentation_TemporalAARequested()",
    ):
        require(vk_scene_request, token, "Vulkan manual-scale temporal ownership")
    vk_resolve_signature = "static bool VK_TemporalPresentation_ResolveTargetsRecorded("
    vk_resolve = function_body(
        vk_gui[vk_gui.rfind(vk_resolve_signature) :], vk_resolve_signature
    )
    for token in (
        "command.captureFrame",
        "historyResourceChanged",
        "generationMatches",
        "VK_TemporalPresentation_DrawResolve(",
        "VK_TemporalPresentation_BlitColorToSwap(",
        "VK_TemporalPresentation_DepthStampMatches(",
        "&& depthReady && writeMatches",
        '"Vulkan temporal depth is not current"',
        "VK_PostProcess_TemporalMotionVectors(",
        "VK_PostProcess_CommitTemporalMotion(",
        "velocityComplete",
    ):
        require(vk_resolve, token, "Vulkan temporal resolve")
    vk_direct = function_body(
        vk_gui, "static bool VK_TemporalPresentation_ResolvePendingSceneTemporal("
    )
    for token in (
        "temporalDirectHistoryRenderTextures[readIndex]",
        "temporalDirectHistoryRenderTextures[writeIndex]",
        "temporalDirectHistoryFrame == backEnd.frameCount - 1",
        "VK_TemporalPresentation_ResolveTargets(",
        "if ( historyAdvanced )",
    ):
        require(vk_direct, token, "Vulkan direct-view history ownership")
    for token in (
        "FMT_RGBA16F",
        "VK_TemporalPresentation_EnsureDirectHistoryTargets(",
        "VK_TemporalPresentation_DrawPendingSceneSpatial(",
        "temporalDepthStampFrame == backEnd.frameCount",
        "destinationRenderTexture, depthResolved",
        "R_ScenePackets_BuildTemporalViewMotionPolicy(",
        "block.reactiveRect0",
        "block.reactiveRect1",
    ):
        require(vk_gui, token, "Vulkan backend-owned temporal resources")
    vk_fill_resolve = function_body(
        vk_gui, "static void VK_TemporalPresentation_FillResolveBlock("
    )
    for token in (
        "block.reactiveRect0[2] = 1.0f;",
        "const bool motionPolicyAvailable = viewDef != NULL",
        "if ( motionPolicyAvailable )",
        "block.reactiveRect0[component] = -1.0f;",
        "block.reactiveRect1[component] = -1.0f;",
    ):
        require(vk_fill_resolve, token, "Vulkan reactive-policy fail-safe")
    vk_shader = read(RENDERER / "Vulkan" / "shaders" / "temporal_resolve.frag")
    vk_post = read(RENDERER / "Vulkan" / "vk_PostProcess.cpp")
    vk_motion = function_body(vk_post, "idImage *VK_PostProcess_TemporalMotionVectors(")
    for token in (
        "viewDef->temporalCaptureFrame", "viewDef->temporalPreviousProjectionValid",
        "generation != vkTemporalMotionGeneration", "vkTemporalMotionFrame != backEnd.frameCount - 1",
        "viewDef->temporalViewIdentity != vkTemporalMotionViewIdentity", "VK_PostProcess_ResetTemporalMotion()",
    ):
        require(vk_motion, token, "Vulkan exact transform-history ownership")
    require(vk_shader, "texture(objectMotion, sceneCameraUV)", "Vulkan rigid velocity texture origin")
    require(vk_shader, "objectVelocity.xy * temporal.sceneOutputExtent.xy", "Vulkan rigid scene-pixel velocity units")
    if "bool objectValid = false;" in vk_shader:
        raise AssertionError("Vulkan rigid velocity execution is disabled")
    for token in (
        "neighborhoodMin",
        "historyClamped",
        "depthReactive",
        "historyWeight",
        "InsideReactiveRect(",
        "packetReactive",
    ):
        require(vk_shader, token, "Vulkan disocclusion/reactive resolve")

    # Per-pixel ownership: every moving surface is written as an exact vector
    # (rigid, posed previous positions, the depth-hacked weapon) or as reactive
    # coverage; screen regions are only the incomplete-pass fallback.
    require(scene_packets, "temporalSurfaceMotion_t R_ScenePackets_TemporalSurfaceMotion(",
        "per-surface temporal classification")
    require(scene_packets, "IsStaticWorldModel()", "world geometry stays on depth reprojection")
    require(scene_packets, "if ( perPixelOwnership ) {", "per-pixel policy skips screen regions")
    for token in (
        "R_ScenePackets_TemporalSurfaceMotion( surf )",
        "RB_FindTemporalEntityHistory(",
        "RB_MotionVectorWeaponProjection(",
        "glColorMask( GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE );",
        "const bool perPixelOwnership = velocityValid && velocityComplete;",
        "vec3 SampleHistory( vec2 uv )",
        "vec3 ClipToBox( vec3 value, vec3 boxMin, vec3 boxMax )",
        "R_TemporalPresentation_PresentSharpness()",
    ):
        require(gl_backend, token, "OpenGL per-pixel temporal ownership")
    glprogs = ROOT / "content" / "baseoq4" / "pak0" / "glprogs"
    require(read(glprogs / "motionvectors.vs"), "gl_MultiTexCoord1.xyz", "OpenGL posed previous positions")
    require(read(glprogs / "motionvectors.fs"), "gl_FragCoord.z > sceneDepth + depthTolerance",
        "OpenGL reactive coverage depth test")
    for token in (
        "VK_POST_MOTION_VECTORS_PREVIOUS",
        "VK_POST_MOTION_VECTORS_REACTIVE",
        "VK_Exec_BindPreviousPositions( cmd, slot, surf )",
        "VK_Post_FindTemporalMotionEntityHistory(",
        "GLS_REDMASK | GLS_GREENMASK | GLS_ALPHAMASK",
        "viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;",
    ):
        require(vk_post, token, "Vulkan per-pixel temporal ownership")
    for token in ("vec3 SampleHistory(vec2 textureUV)", "vec3 ClipToBox(vec3 value, vec3 boxMin, vec3 boxMax)",
                  "vec3 SharpenPresent(vec2 textureUV, vec3 center, float amount)",
                  "texture(objectMotion, closestCameraUV)"):
        require(vk_shader, token, "Vulkan temporal resolve")

    game_local_header = read(GAME_ROOT / "src" / "game" / "Game_local.h")
    require(game_local_header, "temporalHistoryRT[2]", "game history ping-pong ownership")
    game_render = read(GAME_ROOT / "src" / "game" / "Game_render.cpp")
    for token in (
        "presentation.sceneWidth",
        "presentation.outputWidth",
        "blurEnabled || presentation.temporalAARequested",
        "!presentation.temporalAARequested",
        "presentation.captureFrozen",
        "presentation.captureForcedNative",
        "openQ4_ResolveTemporalPresentation(",
        "gameRender.temporalHistoryReadIndex = historyWriteIndex;",
    ):
        require(game_render, token, "game temporal scheduling")
    game_resolve = function_body(
        game_render, "static bool openQ4_ResolveTemporalPresentation("
    )
    call_at = game_resolve.find("renderSystem->ResolveTemporalPresentation(")
    swap_at = game_resolve.find(
        "gameRender.temporalHistoryReadIndex = historyWriteIndex;"
    )
    if call_at < 0 or swap_at <= call_at:
        raise AssertionError("game history must swap only after resolve acceptance")

    temporal_content = [
        path
        for path in (ROOT / "content" / "baseoq4").rglob("*")
        if path.is_file() and "temporal" in path.name.lower()
    ]
    if temporal_content:
        raise AssertionError(
            "temporal presentation must not depend on loose content assets: "
            + ", ".join(str(path.relative_to(ROOT)) for path in temporal_content)
        )

    meson = read(ROOT / "meson.build")
    require(meson, "openq4-temporal-presentation-core-test", "controller native test")
    require(meson, "openq4-temporal-history-core-test", "history native test")

    print("renderer temporal presentation contracts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
