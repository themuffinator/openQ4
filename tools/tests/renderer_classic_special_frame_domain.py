#!/usr/bin/env python3
"""Static regression contract for render-demo and Raven special-frame ownership."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_at = haystack.find(first)
    second_at = haystack.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing opening brace for {signature!r} in {context}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated {signature!r} in {context}")


def validate_sealed_domain() -> None:
    header = read("src/renderer/ClassicSpecialFrameDomain.h")
    source = read("src/renderer/ClassicSpecialFrameDomain.cpp")
    packets = read("src/renderer/ScenePackets.cpp")
    packets_header = read("src/renderer/ScenePackets.h")

    for token in (
        "CLASSIC_SPECIAL_FRAME_SCOPE_RENDER_DEMO",
        "CLASSIC_SPECIAL_FRAME_SCOPE_RAVEN_EFFECTS",
        "CLASSIC_SPECIAL_FRAME_FAILURE_SOURCE_PACKET_MISMATCH",
        "CLASSIC_SPECIAL_FRAME_FAILURE_RENDER_DEMO_STATE",
        "CLASSIC_SPECIAL_FRAME_FAILURE_SPECIAL_EFFECT_STATE",
        "CLASSIC_SPECIAL_FRAME_FAILURE_BACKEND_COVERAGE_MISMATCH",
        "classicSpecialFrameDomainView_t",
        "renderDemoVersion",
        "specialEffectsMask",
        "R_ClassicSpecialFrameDomain_PrepareFrame",
        "R_ClassicSpecialFrameDomain_RecordOwned",
        "R_ClassicSpecialFrameDomain_FinalizeBackendFrame",
        "RendererClassicSpecialFrameDomain_RunSelfTest",
    ):
        require(header, token, "special-frame domain interface")

    for token in (
        "scene.renderDemoPlayback",
        "session->renderdemoVersion",
        "session->rw != view->viewDef->renderWorld",
        "RENDER_PASS_DEPTH",
        "RENDER_PASS_ARB2_INTERACTION",
        "RENDER_PASS_AMBIENT",
        "RENDER_PASS_SPECIAL_EFFECTS",
        "SPECIAL_EFFECT_BLUR | SPECIAL_EFFECT_AL",
        "CLASSIC_SPECIAL_FRAME_FAILURE_SOURCE_PACKET_MISMATCH",
        "RecordBackendFallback",
    ):
        require(source, token, "special-frame source validation")
    require_before(
        source,
        "R_ClassicSpecialFrameDomain_ValidateSceneRange( packetFrame,",
        "R_ClassicSpecialFrameDomain_Commit( *view );",
        "source reconciliation before ownership",
    )

    demo_classifier = function_body(
        packets,
        "static bool R_ScenePackets_IsRenderDemoPlaybackView(",
        "render-demo packet classifier",
    )
    for token in (
        "session->readDemo != NULL",
        "session->rw == viewDef->renderWorld",
        "!viewDef->isSubview",
    ):
        require(demo_classifier, token, "exact render-demo provenance")
    require(packets, "SetLastSceneSpecialEffectsMask(", "special-effects source capture")
    require(packets, "reinterpret_cast<const drawSpecialEffectsCommand_t *>( cmd )->effects.enabled", "backend packet reconstruction")
    require(packets_header, "bool\t\t\t\t\trenderDemoPlayback;", "render-demo packet state")
    require(packets_header, "void SetLastSceneSpecialEffectsMask", "packet mutation API")


def validate_backend_handoffs() -> None:
    gl_backend = read("src/renderer/tr_backend.cpp")
    gl = read("src/renderer/draw_common.cpp")
    vk_backend = read("src/renderer/Vulkan/vk_Backend.cpp")
    vk = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")

    for source, backend in ((gl_backend, "OpenGL"), (vk_backend, "Vulkan")):
        require(source, "R_ClassicSpecialFrameDomain_ResetFrame();", f"{backend} reset")
        require(source, "R_ClassicSpecialFrameDomain_PrepareFrame( *scenePackets );", f"{backend} preparation")
        require(source, "R_ClassicSpecialFrameDomain_FinalizeBackendFrame(", f"{backend} final fallback")

    demo = function_body(gl_backend, "static bool RB_DrawSharedRenderDemoView(", "OpenGL demo adapter")
    require(demo, "RB_DrawView( data );", "OpenGL complete demo execution")
    require(demo, "R_ClassicSpecialFrameDomain_RecordOwned", "OpenGL demo ownership")
    for token in (
        "RB_CompositeRVSpecialBlur",
        "RB_DrawRVSpecialALLight",
        "SPECIAL_EFFECT_BLUR",
        "SPECIAL_EFFECT_AL",
        "R_ClassicSpecialFrameDomain_RecordOwned",
    ):
        require(gl, token, "OpenGL special-effect completion")

    vk_view = function_body(vk, "void VK_GuiExecutor_Draw3DView(", "Vulkan demo adapter")
    require(vk_view, "sharedRenderDemoReady", "Vulkan demo readiness")
    require(vk_view, "R_ClassicSpecialFrameDomain_RecordOwned", "Vulkan demo ownership")
    vk_effects = function_body(vk, "static void VK_Exec_DrawRVSpecialEffects(", "Vulkan special adapter")
    require(vk_effects, "drewBlur", "Vulkan blur completion")
    require(vk_effects, "drewAL", "Vulkan AL completion")
    require(vk_effects, "R_ClassicSpecialFrameDomain_RecordOwned", "Vulkan special ownership")


def validate_controls_and_evidence() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    local = read("src/renderer/tr_local.h")
    bootstrap = read("src/renderer/RendererBootstrap.cpp")
    matrix = read("tools/tests/renderer_validation_matrix.py")
    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    baseline = read("tools/validation/stock_asset_baseline.py")
    registry = read("tools/validation/openq4_validate.py")
    roadmap = read("docs/dev/archive/2026-10-18/idtech5-modernization-roadmap.md")
    domain_doc = read("docs/dev/classic-special-frame-domain-modernization.md")

    require(init, 'r_rendererSharedSpecialFrame( "r_rendererSharedSpecialFrame", "0"', "default-off cvar")
    require(init, 'cmdSystem->AddCommand( "rendererClassicSpecialFrameDomainSelfTest"', "native self-test")
    require(init, "Renderer shared special frame:", "gfxInfo diagnostics")
    require(local, "extern idCVar r_rendererSharedSpecialFrame;", "renderer cvar API")
    require(bootstrap, '{ "r_rendererSharedSpecialFrame", &r_rendererSharedSpecialFrame, 0 }', "default safety")
    for source, context in (
        (matrix, "safe renderer matrix"),
        (benchmark, "benchmark isolation"),
        (baseline, "stock-baseline isolation"),
    ):
        require(source, "r_rendererSharedSpecialFrame", context)
    require(matrix, "+rendererClassicSpecialFrameDomainSelfTest", "safe native self-test")
    require(registry, '"renderer_classic_special_frame_domain.py"', "validation registry")
    require(roadmap, "Milestone D scoped implementation is complete.", "advanced roadmap status")
    require(roadmap, "Milestone E temporal", "advanced roadmap status")
    require(roadmap, "Milestone F now has a guarded", "advanced roadmap target")
    for token in (
        "session stream",
        "portal-sky",
        "RC_DRAW_SPECIAL_EFFECTS",
        "r_rendererSharedSpecialFrame 1",
        "classic fallback",
        "engine-written",
    ):
        require(domain_doc, token, "domain documentation")


def main() -> int:
    validate_sealed_domain()
    validate_backend_handoffs()
    validate_controls_and_evidence()
    print("renderer_classic_special_frame_domain: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
