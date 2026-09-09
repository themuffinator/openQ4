#!/usr/bin/env python3
"""Static contracts for Milestone E temporal presentation and dynamic resolution."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", str(ROOT.parent / "openQ4-game"))
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
        "#define RENDER_API_VERSION\t\t\t15",
        "renderer ABI v14",
    )
    require(
        read(GAME_ROOT / "src" / "game" / "Game.h"),
        "const int GAME_API_VERSION\t\t= 48;",
        "game ABI v48",
    )

    render_init = read(RENDERER / "RenderSystem_init.cpp")
    require(
        render_init,
        'idCVar r_rendererDynamicResolution( "r_rendererDynamicResolution", "0"',
        "default-off dynamic resolution",
    )
    temporal = read(RENDERER / "TemporalPresentation.cpp")
    for token in (
        'idCVar r_temporalAA( "r_temporalAA", "0"',
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
    begin_frame = function_body(render_system, "idRenderSystemLocal::BeginFrame(")
    for token in (
        "!R_TemporalPresentation_DynamicResolutionRequested()",
        "!R_TemporalPresentation_TemporalAARequested()",
        "screenFraction < 100 && r_resolutionScaleMode.GetInteger() == 0",
    ):
        require(begin_frame, token, "legacy-crop temporal bypass")
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
    gl_resolve = function_body(gl_backend, "bool RB_ResolveTemporalPresentation(")
    for token in (
        "captureFrame",
        "RB_RenderMotionVectorBuffer(",
        "RB_DrawTemporalResolvePass(",
        "RB_PresentTemporalSpatialFallback(",
        "R_ScenePackets_BuildTemporalViewMotionPolicy(",
        "exactMotionDomains",
        "rbSceneScalePresentedFrame = backEnd.frameCount;",
    ):
        require(gl_backend if token.startswith("rbScene") else gl_resolve, token, "OpenGL temporal resolve")
    for token in (
        "neighborhoodMin",
        "historyClamped",
        "depthReactive",
        "historyWeight",
        "ReactiveRegion0",
        "packetReactive",
        "R_ScenePackets_TemporalRigidMotionEligible(",
    ):
        require(gl_backend, token, "OpenGL disocclusion/reactive resolve")

    gl_swap_scale = function_body(
        gl_backend, "void RB_ApplyResolutionScaleToBackBuffer( void )"
    )
    for token in (
        "presentation.dynamicResolutionRequested",
        "presentation.temporalAARequested",
    ):
        require(gl_swap_scale, token, "OpenGL native UI scale ownership")
    temporal_guard_at = gl_swap_scale.find("presentation.dynamicResolutionRequested")
    scene_marker_guard_at = gl_swap_scale.find(
        "if ( rbSceneScalePresentedFrame", temporal_guard_at
    )
    backbuffer_copy_at = gl_swap_scale.find("sceneImage->CopyFramebuffer(")
    if (
        temporal_guard_at < 0
        or scene_marker_guard_at <= temporal_guard_at
        or backbuffer_copy_at <= scene_marker_guard_at
    ):
        raise AssertionError(
            "OpenGL temporal ownership must reject swap-tail scaling before copying UI"
        )
    require(
        gl_swap_scale[temporal_guard_at:scene_marker_guard_at],
        "return;",
        "OpenGL temporal native UI early-out",
    )

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
    vk_resolve_signature = "static bool VK_TemporalPresentation_ResolveTargets("
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
    for token in (
        "neighborhoodMin",
        "historyClamped",
        "depthReactive",
        "historyWeight",
        "InsideReactiveRect(",
        "packetReactive",
    ):
        require(vk_shader, token, "Vulkan disocclusion/reactive resolve")

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
