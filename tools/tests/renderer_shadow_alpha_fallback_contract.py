#!/usr/bin/env python3
"""REN-03: shadow-map casters whose cutout coverage cannot be reproduced.

A perforated caster whose active alpha-test stage has no bindable image or
uses generated (non-explicit) texture coordinates must not be drawn as solid
depth into a light that still claims to be mapped. Both backends fail the
light to the sticky stencil fallback instead (retail stencil volumes ignore
alpha too, so the result matches the original game). A perforated caster with
no active alpha stage is solid and keeps solid depth. No stock Quake 4
material or guide reaches the unreproducible case (audited 6 November 2026);
mods can.
"""

from __future__ import annotations

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
    gl = read("src/renderer/draw_arb2.cpp")
    coverage = function_body(gl, "static bool RB_ShadowMapPerforatedCoverageUnreproducible(")
    for token in ("RB_ShadowMapTextureStageHasBindableImage(", "texgen != TG_EXPLICIT", "return true;"):
        require(coverage, token, "OpenGL unreproducible-coverage test")
    for signature in ("static bool RB_ShadowMapDrawCasterChain(", "static bool RB_PointShadowMapDrawCasterChain("):
        chain = function_body(gl, signature)
        branch = chain.find("RB_ShadowMapPerforatedCoverageUnreproducible( surf )")
        if branch < 0 or "return false;" not in chain[branch:branch + 400]:
            raise AssertionError(f"{signature} must fail the pass for unreproducible cutout coverage")
    if "unsupported-alpha-texgen-solid-depth" in gl:
        raise AssertionError("OpenGL must not draw unreproducible cutout coverage as solid depth")
    # the failed render takes the sticky stencil fallback
    require(gl, "passResult = SHADOWMAP_PASS_RESULT_RENDER_FAIL;", "OpenGL render-failure result")
    require(gl, "RB_ShadowMapMarkStencilFallbackSticky( vLight );", "OpenGL sticky stencil fallback")

    vk = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    supported = function_body(vk, "static bool VK_ShadowMap_PerforatedCasterSupported(")
    for token in ('"cinematic-alpha"', '"unsupported-alpha-texgen"', '"missing-alpha-image"'):
        require(supported, token, "Vulkan unsupported cutout reasons")
    print("renderer_shadow_alpha_fallback_contract: ok")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"renderer_shadow_alpha_fallback_contract: {error}", file=sys.stderr)
        sys.exit(1)
