#!/usr/bin/env python3
"""Static contracts for backend-neutral, nonblocking whole-frame GPU timing."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", str(ROOT.parent / "openQ4-game"))
).resolve()


def read(path: Path) -> str:
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


def current_branch(repository: Path) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(repository), "branch", "--show-current"],
        check=False,
        capture_output=True,
        text=True,
    )
    branch = result.stdout.strip()
    return branch if result.returncode == 0 and branch else None


def validate_gl_timestamp_support(metrics: str) -> None:
    support = function_body(metrics, "static bool R_RendererMetrics_GlTimestampQueriesSupported(")
    available = function_body(metrics, "static bool R_RendererMetrics_GlFullFrameTimingAvailable(")
    harness = r'''
#include <cassert>
#include <cstddef>
#include <cstring>
struct { struct { bool hasTimerQuery = true; float glVersion = 2.1f; } backendCaps; } glConfig;
bool arbTimer = false;
bool GLCapabilityProbe_HasExtension(const char *name) {
    return !std::strcmp(name, "GL_EXT_timer_query") ||
        (!std::strcmp(name, "GL_ARB_timer_query") && arbTimer);
}
void stub() {}
auto glGenQueries = &stub, glDeleteQueries = &stub, glQueryCounter = &stub;
auto glGetQueryObjectiv = &stub, glGetQueryObjectui64v = &stub;
'''
    harness += "bool R_RendererMetrics_GlTimestampQueriesSupported() {" + support + "}\n"
    harness += "bool R_RendererMetrics_GlFullFrameTimingAvailable() {" + available + "}\n"
    harness += r'''
int main() {
    // Apple GL 2.1: EXT elapsed queries and exported symbols do not imply timestamps.
    assert(!R_RendererMetrics_GlFullFrameTimingAvailable());
    arbTimer = true;
    assert(R_RendererMetrics_GlFullFrameTimingAvailable());
    arbTimer = false; glConfig.backendCaps.glVersion = 3.3f;
    assert(R_RendererMetrics_GlFullFrameTimingAvailable());
    glConfig.backendCaps.hasTimerQuery = false;
    assert(!R_RendererMetrics_GlFullFrameTimingAvailable());
    glConfig.backendCaps.hasTimerQuery = true; glQueryCounter = nullptr;
    assert(!R_RendererMetrics_GlFullFrameTimingAvailable());
}
'''
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required for the GPU timestamp contract")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gpu-timestamp-", dir=ROOT / ".tmp") as directory:
        source, binary = Path(directory) / "timestamp.cpp", Path(directory) / "timestamp.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


def main() -> int:
    public_header = read(RENDERER / "RenderSystem.h")
    companion_header_path = GAME_ROOT / "src" / "renderer" / "RenderSystem.h"
    if not companion_header_path.is_file():
        raise AssertionError(f"missing companion renderer header: {companion_header_path}")
    companion_header = read(companion_header_path)
    engine_branch = current_branch(ROOT)
    companion_branch = current_branch(GAME_ROOT)
    matching_source_pair = (
        engine_branch is not None
        and companion_branch is not None
        and engine_branch == companion_branch
    )
    companion_has_timing_api = "GetGpuFrameTiming( renderGpuFrameTiming_t &timing ) const" in companion_header
    if matching_source_pair or companion_has_timing_api:
        if public_header != companion_header:
            raise AssertionError("engine and companion RenderSystem.h files are not byte-identical")
    else:
        print(
            "renderer GPU frame timing contracts: companion parity skipped "
            "(workflow source pair is on different revisions)"
        )
    for token in (
        "RENDER_GPU_TIMING_BACKEND_OPENGL",
        "RENDER_GPU_TIMING_BACKEND_VULKAN",
        "bool\t\t\t\tvalid;",
        "unsigned long long\telapsedMicroseconds;",
        "unsigned long long\tunavailableSamples;",
        "unsigned long long\tdroppedSamples;",
        "GetGpuFrameTiming( renderGpuFrameTiming_t &timing ) const",
        "ResetGpuFrameTiming( const char *reason )",
        "renderPresentationState_s",
        "int\t\t\t\tsceneWidth;",
        "int\t\t\t\toutputWidth;",
        "GetPresentationState( renderPresentationState_t &state ) const",
        "ResolveTemporalPresentation(",
    ):
        require(public_header, token, "public renderer module ABI")

    module_api = read(RENDERER / "RenderModuleAPI.h")
    require(module_api, "#define RENDER_API_VERSION\t\t\t12", "renderer module ABI v12")

    core = read(RENDERER / "GpuFrameTimingCore.h")
    require(core, "elapsedMicroseconds == 0", "zero-duration rejection")
    require(core, "static_cast<std::int64_t>( currentFrame )", "wide latency arithmetic")
    require(core, "std::int64_t( 1 ) << 32", "signed frame-counter wrap")

    render_system = read(RENDERER / "RenderSystem.cpp")
    for token in (
        "idRenderSystemLocal::GetPresentationState",
        "R_TemporalPresentation_GetFrameState()",
        "R_TemporalPresentation_HistoryGeneration()",
        "idRenderSystemLocal::ResolveTemporalPresentation",
        "RC_RESOLVE_TEMPORAL_PRESENTATION",
    ):
        require(render_system, token, "frame-latched presentation ABI")
    temporal_resolve = function_body(
        render_system, "bool idRenderSystemLocal::ResolveTemporalPresentation("
    )
    for token in (
        "presentation.captureFrozen",
        "presentation.captureForcedNative",
        "cmd->sceneColorTarget = sceneColorTarget;",
        "cmd->sceneDepthTarget = sceneDepthTarget;",
        "cmd->historyReadTarget = historyReadTarget;",
        "cmd->historyWriteTarget = historyWriteTarget;",
        "cmd->viewDef = primaryView;",
        "cmd->presentation = presentation;",
    ):
        require(temporal_resolve, token, "immutable temporal resolve command")

    tr_local = read(RENDERER / "tr_local.h")
    for token in (
        "RC_RESOLVE_TEMPORAL_PRESENTATION",
        "} resolveTemporalPresentationCommand_t;",
        "renderPresentationState_t presentation;",
        "unsigned int\t\thistoryGeneration;",
    ):
        require(tr_local, token, "temporal resolve render command")

    backend = read(RENDERER / "tr_backend.cpp")
    require(backend, "case RC_RESOLVE_TEMPORAL_PRESENTATION:", "OpenGL temporal dispatch")
    require(backend, "executionCommand.captureFrame = true;", "late capture temporal bypass")

    game_render_path = GAME_ROOT / "src" / "game" / "Game_render.cpp"
    if not game_render_path.is_file():
        raise AssertionError(f"missing companion game render integration: {game_render_path}")
    game_render = read(game_render_path)
    for token in (
        "_temporalHistoryAlbedo%d",
        "presentation.outputWidth",
        "presentation.outputHeight",
        "!presentation.temporalAARequested",
        "blurEnabled || presentation.temporalAARequested",
        "openQ4_ResolveTemporalPresentation(",
        "gameRender.postProcessRT[1]",
        "gameRender.forwardRenderPassResolvedRT",
    ):
        require(game_render, token, "game temporal presentation integration")
    begin_frame = function_body(render_system, "void idRenderSystemLocal::BeginFrame(")
    capture_depth = function_body(
        render_system, "void idRenderSystemLocal::CaptureDepthRenderToImage("
    )
    require(begin_frame, "R_RendererMetrics_MarkCpuFrameBegin();", "CPU frame start")
    if "R_RendererMetrics_MarkCpuFrameBegin();" in capture_depth:
        raise AssertionError("CPU frame start must not be anchored to depth capture")
    if render_system.count("R_RendererMetrics_MarkCpuFrameBegin();") != 1:
        raise AssertionError("CPU frame timing must have exactly one BeginFrame anchor")

    metrics = read(RENDERER / "RendererMetrics.cpp")
    validate_gl_timestamp_support(metrics)
    gl_poll = function_body(metrics, "static bool R_RendererMetrics_PollGlFullFrameTiming(")
    availability = gl_poll.find("GL_QUERY_RESULT_AVAILABLE")
    result_read = gl_poll.find("glGetQueryObjectui64v")
    if availability < 0 or result_read < 0 or availability >= result_read:
        raise AssertionError("OpenGL timestamp results must be availability-checked first")
    gl_self_test = function_body(metrics, "bool RendererGpuTimer_RunSelfTest(")
    if "glFinish" in gl_self_test:
        raise AssertionError("GPU timing self-test must not force GL completion")
    require(metrics, "renderer metrics capture window started", "capture-window reset")

    benchmarks = read(RENDERER / "RendererBenchmarks.cpp")
    marker = (
        "OPENQ4_FRAME_TIMING_V1 map=%s backend=%s profile=%s "
        "cpuSamples=%d cpuP50Us=%llu cpuP95Us=%llu cpuP99Us=%llu "
        "gpuAvailable=%d gpuSamples=%d gpuP50Us=%lld gpuP95Us=%lld gpuP99Us=%lld"
    )
    require(benchmarks, marker, "versioned timing marker")
    require(benchmarks, "mapName.ToLower();", "canonical lowercase map")
    require(benchmarks, "const int gpuCount = timing.supported", "supported GPU aggregation")
    require(benchmarks, "acceptedFrames[existing] == sample.gpuFrameNumber", "GPU dedup")
    require(benchmarks, "delayedCount != 2", "GPU dedup self-test")
    require(benchmarks, 'normalizedMap != "game/storage1"', "map normalization self-test")

    render_init = read(RENDERER / "RenderSystem_init.cpp")
    if render_init.count("RendererBenchmarks_PrintTimingMarker();") != 1:
        raise AssertionError("rendererBenchmarkCapture must emit the V1 marker exactly once")
    require(render_init, 'ResetGpuFrameTiming( "begin level load" )', "map begin reset")
    require(render_init, 'ResetGpuFrameTiming( "end level load" )', "map end reset")
    require(render_init, 'ResetGpuFrameTiming( "renderer init" )', "renderer init reset")
    require(render_init, 'ResetGpuFrameTiming( "renderer shutdown" )', "renderer shutdown reset")
    require(render_init, 'ResetGpuFrameTiming( "vid_restart" )', "video restart reset")

    session = read(ROOT / "src" / "framework" / "Session.cpp")
    unload_map = function_body(session, "void idSessionLocal::UnloadMap(")
    require(unload_map, 'ResetGpuFrameTiming( "session unload" )', "session reset")

    vk_timing = read(RENDERER / "Vulkan" / "VulkanGpuFrameTiming.cpp")
    require(vk_timing, "VK_QUERY_RESULT_64_BIT );", "64-bit non-waiting query read")
    if "VK_QUERY_RESULT_64_BIT |" in vk_timing or "| VK_QUERY_RESULT_WAIT_BIT" in vk_timing:
        raise AssertionError("Vulkan timestamp query reads must not use WAIT_BIT")
    require(vk_timing, "GpuFrameTimingCore_TimestampDelta", "valid-bit timestamp math")
    require(vk_timing, "elapsedMicroseconds", "Vulkan microsecond result")

    vk_gui = read(RENDERER / "Vulkan" / "vk_GuiExecutor.cpp")
    wait_at = vk_gui.find("const VkResult frameFenceResult = vkWaitForFences")
    resolve_at = vk_gui.find("VK_GpuFrameTiming_BeginFrame( cmd, slot, tr.frameCount )")
    if wait_at < 0 or resolve_at < 0 or wait_at >= resolve_at:
        raise AssertionError("Vulkan timestamps must resolve only after the slot fence retires")
    require(vk_gui, "readbackFenceResult != VK_SUCCESS", "screenshot-fence result check")

    vk_device = read(RENDERER / "Vulkan" / "VulkanDevice.cpp")
    require(vk_device, "graphicsTimestampValidBits", "Vulkan queue timestamp width")
    require(vk_device, 'ResetGpuFrameTiming( "Vulkan swapchain recreation" )', "swapchain reset")

    discovery = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "build" / "meson_sources.py"), "--emit", "renderer_vk"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    timing_source = "src/renderer/Vulkan/VulkanGpuFrameTiming.cpp"
    if discovery.count(timing_source) != 1:
        raise AssertionError("Vulkan timing source must occur exactly once in module discovery")
    source_contract = read(ROOT / "tools" / "build" / "meson_sources.py")
    require(source_contract, "RENDERER_VK_REQUIRED_SOURCES", "required Vulkan source contract")

    meson = read(ROOT / "meson.build")
    require(meson, "'openq4-gpu-frame-timing-test'", "native timing test target")
    native_test = read(ROOT / "tools" / "tests" / "native" / "GpuFrameTimingTest.cpp")
    require(native_test, "INT_MAX, INT_MIN", "native frame-wrap test")
    require(native_test, "state.generation, 0, 1", "native zero-duration test")

    print("renderer GPU frame timing contracts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
