#!/usr/bin/env python3
"""REN-06: newly resident reflection probes are filtered off the frame.

A probe-source cache miss used to stall the frame that took it for the whole
prefilter (about 85 ms with parallel tiles, half a second serially). With
r_pbrProbeAsyncPrefilter (default 1) both backends read the source, filter it
on a background job (Vulkan also defers its copy and decode) and upload the
finished RGBA16F tiles over later frames within a fixed per-frame budget. The
probe set keeps its analytic fallback until the last tile lands; screenshots
and `pbrProbeSettle` finish everything at once. The jobs are joined before the
atlas or the renderer module goes away. The gameplay proof is
renderer_vulkan_pbr_probes.py on both backends; the native openq4-pbr-math
test requires the job's tiles to equal the synchronous filter exactly.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function: {signature}")
    brace = source.find("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated body for: {signature}")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {label}: {token}")


def main() -> int:
    environment = read("src/renderer/PBREnvironment.h")
    for token in ("class AsyncPrefilter", "bool StartFrom(", "PackTilesHalf(", "~AsyncPrefilter() { Wait(); }",
                  "bool Failed() const"):
        require(environment, token, "background prefilter job")

    atlas_header = read("src/renderer/ModernSpecularProbeAtlas.h")
    for token in ("MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING", "MODERN_SPECULAR_PROBE_TILE_COUNT",
                  "MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US = 2000", "void R_ModernSpecularProbeAtlas_FlushUploads( bool captureFrame );"):
        require(atlas_header, token, "shared probe publishing contract")
    budget = int(re.search(r"MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US = (\d+)", atlas_header).group(1))
    if budget > 4000:
        raise AssertionError("the per-frame publish budget must stay inside the 4 ms cold-miss budget")

    init = read("src/renderer/RenderSystem_init.cpp")
    require(init, 'idCVar r_pbrProbeAsyncPrefilter( "r_pbrProbeAsyncPrefilter", "1"', "default-on cvar")
    require(init, '"pbrProbeSettle", R_PBRProbeSettle_f', "settle command")
    require(init, "R_PBR_PROBE_SETTLE_FRAMES", "settle request expiry")

    gl = read("src/renderer/ModernSpecularProbeAtlas.cpp")
    flush = function_body(gl, "void R_ModernSpecularProbeAtlas_FlushUploads( bool captureFrame ) {")
    for token in ("job.Start(", "MODERN_SPECULAR_PROBE_PREFILTER_WORKERS, true", "R_ModernSpecularProbeAtlas_UploadHalfTile(",
                  "MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US", "!captureFrame", "R_ModernSpecularProbeAtlas_CancelPrefilter( slot );"):
        require(flush, token, "OpenGL background prefilter flush")
    require(function_body(gl, "static void R_ModernSpecularProbeAtlas_ResetEntries( void ) {"),
            "R_ModernSpecularProbeAtlas_CancelPrefilter( slot );", "OpenGL join on reset/shutdown")
    acquire = function_body(gl, "modernSpecularProbeAtlasReject_t R_ModernSpecularProbeAtlas_Acquire(")
    require(acquire, "R_ModernSpecularProbeAtlas_CancelPrefilter( slot );", "OpenGL cancel on slot reuse")
    require(function_body(gl, "bool R_ModernSpecularProbeAtlas_CaptureFrame( const idScenePacketFrame &frame ) {"),
            "temporalCaptureFrame", "OpenGL capture frames finish pending probes")
    for path in ("src/renderer/draw_pbr.cpp", "src/renderer/ModernGLExecutor.cpp"):
        require(read(path), "R_ModernSpecularProbeAtlas_FlushUploads( R_ModernSpecularProbeAtlas_CaptureFrame(",
                f"{path} capture-aware flush")

    vk = read("src/renderer/Vulkan/vk_PBRProbes.cpp")
    vk_acquire = function_body(vk, "static modernSpecularProbeAtlasReject_t VK_PBRProbes_Acquire(")
    for token in ("VK_ProbeSource_BeginRead(", "VK_ProbeSource_CollectRead(", "StartFrom(", "VK_ProbeSource_Decode(",
                  "VK_PBRProbes_UploadHalfTile(", "MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US",
                  "MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING", "r_pbrProbeAsyncPrefilter.GetBool()"):
        require(vk_acquire, token, "Vulkan staged probe miss")
    prepare = function_body(vk, "void VK_PBRProbes_PrepareFrame( const idScenePacketFrame &frame ) {")
    for token in ("temporalCaptureFrame", "R_PBRProbes_SettleRequested()", "++prefilterPendingFrames;"):
        require(prepare, token, "Vulkan capture/settle handling")
    require(function_body(vk, "void VK_PBRProbes_Shutdown() {"), "VK_PBRProbes_CancelAllPrefilters();",
            "Vulkan join on shutdown")
    require(function_body(vk, "static bool VK_PBRProbes_AtlasReady( idImage *&atlas ) {"),
            "VK_PBRProbes_CancelAllPrefilters();", "Vulkan cancel on atlas reload")
    require(function_body(vk, "static void VK_PBRProbes_CancelPrefilter( int slot ) {"),
            "VK_ProbeSource_CancelRead(", "Vulkan releases an in-flight readback")

    source = read("src/renderer/Vulkan/vk_ProbeSource.cpp")
    begin = function_body(source, "modernSpecularProbeAtlasReject_t VK_ProbeSource_BeginRead(")
    if "VK_Device_FlushUploadBatch" in begin or "vkWaitForFences" in begin:
        raise AssertionError("BeginRead must leave the copy in the frame's batch without submitting or waiting")
    collect = function_body(source, "modernSpecularProbeAtlasReject_t VK_ProbeSource_CollectRead(")
    for token in ("vkGetFenceStatus(", "MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING", "VK_ProbeSource_Matches( image, read.stamp )"):
        require(collect, token, "Vulkan non-blocking collection")
    require(function_body(source, "void VK_ProbeSource_CancelRead( vkProbeSourceRead_t &read ) {"),
            "VK_Device_DeferDestroy(", "Vulkan deferred release of a busy readback")
    print("renderer_pbr_probe_prefilter_contract: ok")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"renderer_pbr_probe_prefilter_contract: {error}", file=sys.stderr)
        sys.exit(1)
