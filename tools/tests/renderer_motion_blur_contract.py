#!/usr/bin/env python3
"""Static contracts for REN-08 motion blur on OpenGL and Vulkan.

Motion blur keeps a camera history of its own, independent of temporal AA.
Both backends must route every history, jitter and strength decision through
src/renderer/MotionBlurCore.h, draw exact vectors for rigid, skinned and
view-weapon surfaces, and publish rendererMotionBlurStatus. The runtime
qualification is tools/tests/renderer_motion_blur_qualification.py; the
policy itself is unit-tested by tools/tests/native/MotionBlurCoreTest.cpp.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"missing required source: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {label}: {token}")


def forbid(source: str, token: str, label: str) -> None:
    if token in source:
        raise AssertionError(f"{label}: {token}")


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


def strip_comments(source: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)


def main() -> int:
    core = read(RENDERER / "MotionBlurCore.h")
    for token in (
        "MotionBlurCore_ValidateHistory(",
        "MotionBlurCore_AlignPreviousOffsets(",
        "MotionBlurCore_UpdateShutter(",
        "MotionBlurCore_ShutterScale(",
        "MotionBlurCore_CenterRotationPixels(",
        "MOTION_BLUR_HISTORY_EXPLICIT_CUT",
        "projectionNeutral[4]",
    ):
        require(core, token, "motion blur core")
    validate = strip_comments(function_body(core, "inline motionBlurHistoryResult_t MotionBlurCore_ValidateHistory("))
    require(validate, "deltaMsec < 0", "time-reversal reset")
    forbid(validate, "deltaMsec <= 0", "equal presentation times must not reset motion blur")
    require(validate, "MotionBlurCore_ProjectionChanged(", "projection test")
    projection = function_body(core, "inline bool MotionBlurCore_ProjectionChanged(")
    require(projection, "projectionNeutral", "jitter-free projection comparison")

    tr_main = read(RENDERER / "tr_main.cpp")
    setup = function_body(tr_main, "void R_SetupProjection( void )")
    neutral_at = setup.find("temporalNeutralProjectionOffset.Set(")
    jitter_at = setup.find("xmin += jitterx;")
    if neutral_at < 0 or jitter_at < 0 or neutral_at > jitter_at:
        raise AssertionError("R_SetupProjection must record the neutral offsets before adding jitter")
    require(read(RENDERER / "tr_local.h"), "temporalNeutralProjectionValid", "viewDef neutral flag")

    temporal = read(RENDERER / "TemporalPresentation.cpp")
    capture = function_body(temporal, "void R_TemporalPresentation_CapturePreviousPositions(")
    require(capture, "R_TemporalPresentation_PreviousPositionsRequested()", "skinned capture for motion blur")
    cache = function_body(temporal, "struct vertCache_s *R_TemporalPresentation_PreviousPositionCache(")
    require(cache, "R_TemporalPresentation_PreviousPositionsRequested()", "skinned stream for motion blur")
    requested = function_body(temporal, "bool R_TemporalPresentation_PreviousPositionsRequested(")
    for token in ("R_TemporalPresentation_TemporalAARequested()", "r_motionBlurObjectVectors.GetBool()"):
        require(requested, token, "previous-position consumers")
    status = function_body(temporal, "void R_TemporalPresentation_PrintMotionBlurStatus_f(")
    for token in ("R_RenderThread_Sync(", "\"reset\"", "Motion blur history:", "Motion blur ownership:",
                  "Motion blur effective:", "Motion blur camera:"):
        require(status, token, "rendererMotionBlurStatus")
    init = read(RENDERER / "RenderSystem_init.cpp")
    require(init, '"rendererMotionBlurStatus", R_TemporalPresentation_PrintMotionBlurStatus_f', "status command")
    require(init, 'idCVar r_motionBlurShutterMsec( "r_motionBlurShutterMsec", "16.667"', "shutter cvar")

    gl = read(RENDERER / "draw_common.cpp")
    vk = read(RENDERER / "Vulkan" / "vk_PostProcess.cpp")
    for label, source, blur_sig, build_sig in (
        ("OpenGL", gl, "static void RB_STD_MotionBlur( void )", "static bool RB_BuildMotionBlurViewState("),
        ("Vulkan", vk, "static bool VK_Post_MotionBlur(", "static bool VK_Post_BuildMotionViewState("),
    ):
        require(source, '#include "', f"{label} includes")
        require(source, "MotionBlurCore.h", f"{label} motion blur core include")
        blur = strip_comments(function_body(source, blur_sig))
        for token in ("ValidateHistory(", "AlignPreviousState(", "MotionBlurCore_UpdateShutter(",
                      "MotionBlurCore_ShutterScale(", "stats.subviewSkips++", "stats.drawnViews++",
                      "stats.history[", "alignedPreviousState"):
            require(blur, token, f"{label} motion blur pass")
        forbid(blur, "renderTime <= previous", f"{label} must not reset on equal presentation times")
        build = function_body(source, build_sig)
        require(build, "temporalNeutralProjectionOffset", f"{label} neutral offsets")
        require(build, "!", f"{label} capture-frame exclusion")
        require(build, "temporalCaptureFrame", f"{label} screenshots are not camera cuts")
        require(source, "weaponStill", f"{label} zero-motion view weapon")
        require(source, "blurStats->posed++", f"{label} exact skinned vectors")
        forbid(strip_comments(source), "TemporalRigidMotionEligible", f"{label} motion blur is no longer rigid-only")

    for shader in (
        ROOT / "content" / "baseoq4" / "pak0" / "glprogs" / "motionblur.fs",
        RENDERER / "Vulkan" / "shaders" / "post_motionblur.frag",
    ):
        require(read(shader), "velocityPixels *= ( motionBlurObjectParams.z > 0.0 ) ? motionBlurObjectParams.z : 1.0;",
                f"{shader.name} shutter scale")

    require(read(ROOT / "meson.build"), "tools/tests/native/MotionBlurCoreTest.cpp", "native core test")
    print("renderer_motion_blur_contract: ok")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"renderer_motion_blur_contract: {error}", file=sys.stderr)
        sys.exit(1)
