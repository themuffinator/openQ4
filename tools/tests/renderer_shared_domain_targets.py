#!/usr/bin/env python3
"""Static contract for the REN-01 shared classic domain seams.

The nine shared domains were qualified (renderer_shared_domain_qualification.py)
under the default presets on OpenGL and Vulkan. That depends on a few seams
that this contract keeps from silently narrowing again:

- the shared adapters draw where the classic walker draws: the window, the
  renderer's own scene target, or the game's forward scene target bound as its
  own feedback source (both backends);
- Vulkan gates the 3D-view domains on a native-size scene, never on "any scene
  target", and no longer moves a view to the swapchain when a domain is on;
- a frame whose draws fit never overflows its geometry or instance records;
- the qualification fault injection is cheat-protected, default-off, guarded by
  the default-safety audit, and gates subview, special-frame and
  cinematic/post readiness;
- the final scene composite ends its 2D batch on both backends, so the HUD
  that follows is a pure GUI view.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def reject(text: str, needle: str, label: str) -> None:
    if needle in text:
        raise AssertionError(f"{label}: must not contain {needle!r}")


def body(text: str, signature: str, label: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"{label}: missing {signature!r}")
    open_brace = text.find("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:index + 1]
    raise AssertionError(f"{label}: unbalanced body")


def validate_gl_targets() -> None:
    common = read("src/renderer/draw_common.cpp")
    arb2 = read("src/renderer/draw_arb2.cpp")
    local = read("src/renderer/tr_local.h")
    require(local, "bool RB_SharedDomainTargetAllowed( void );", "GL target predicate declaration")
    predicate = body(common, "bool RB_SharedDomainTargetAllowed( void ) {", "GL target predicate")
    for token in ("backEnd.feedbackRenderTexture == NULL",
                  "RB_IsSceneRenderTexture( backEnd.renderTexture )",
                  "backEnd.renderTexture == backEnd.feedbackRenderTexture"):
        require(predicate, token, "GL target predicate")
    # GUI, world ambient, cinematic/post and fog/blend in draw_common.cpp;
    # interaction in draw_arb2.cpp.
    if common.count("!RB_SharedDomainTargetAllowed()") < 4:
        raise AssertionError("GL shared adapters: each of GUI, ambient, cinematic/post and fog/blend must use the target predicate")
    require(arb2, "!RB_SharedDomainTargetAllowed()", "GL shared interaction adapter")


def validate_vulkan_targets() -> None:
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    ready = body(executor, "bool VK_Exec_SharedInteractionTargetReady( void ) {", "Vulkan target query")
    for token in ("vkExec.temporalSceneRenderTexture",
                  "backEnd.renderTexture == backEnd.feedbackRenderTexture",
                  "VK_Exec_SwapchainPipelineTarget()"):
        require(ready, token, "Vulkan target query")
    scene = body(executor, "static bool VK_SharedDomainSceneAllowed( const vkSceneScaleState_t &state ) {",
                 "Vulkan scene gate")
    for token in ("state.windowMultisample", "state.targetWidth == state.nativeWidth",
                  "state.targetHeight == state.nativeHeight"):
        require(scene, token, "Vulkan scene gate")
    walk = body(executor, "void VK_GuiExecutor_Draw3DView( const viewDef_t *viewDef ) {", "Vulkan 3D walk")
    reject(walk, "!sceneScaleState.active && r_rendererShared", "Vulkan 3D walk")
    if walk.count("VK_SharedDomainSceneAllowed( sceneScaleState ) && r_rendererShared") != 5:
        raise AssertionError("Vulkan 3D walk: ambient, interaction, in-world GUI, cinematic/post and fog/blend must use the scene gate")
    # A shared domain no longer moves its view onto the swapchain.
    reject(executor, "VK_TemporalPresentation_SharedAdapterRequested", "Vulkan scene-target policy")
    for preflight in ("bool VK_ClassicInteraction_Preflight( const viewDef_t *viewDef ) {",
                      "bool VK_ClassicFogBlend_Preflight( const viewDef_t *viewDef ) {"):
        require(body(interactions, preflight, preflight), "VK_Exec_SharedInteractionTargetReady()", preflight)


def validate_packet_pools() -> None:
    packets = read("src/renderer/ScenePackets.h")
    require(packets, "const int SCENE_PACKET_MAX_GEOMETRY_RECORDS = SCENE_PACKET_MAX_DRAWS;", "geometry pool")
    require(packets, "const int SCENE_PACKET_MAX_INSTANCE_RECORDS = SCENE_PACKET_MAX_DRAWS;", "instance pool")
    geometry = read("src/renderer/GeometryResources.cpp")
    require(geometry, "SCENE_PACKET_MAX_GEOMETRY_RECORDS >= SCENE_PACKET_MAX_DRAWS", "geometry pool self-test")


def validate_fault_injection() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    match = re.search(r'idCVar r_rendererSharedFaultInject\( "r_rendererSharedFaultInject", "0", ([^,]+),', init)
    if not match or "CVAR_CHEAT" not in match.group(1):
        raise AssertionError("r_rendererSharedFaultInject must be a default-off cheat cvar")
    require(read("src/renderer/RendererBootstrap.cpp"),
            '{ "r_rendererSharedFaultInject", &r_rendererSharedFaultInject, 0 },', "default-safety audit")
    require(read("src/renderer/tr_backend.cpp"), "SHARED_FAULT_INJECT_SUBVIEW", "GL subview preflight")
    require(read("src/renderer/Vulkan/vk_Backend.cpp"), "SHARED_FAULT_INJECT_SUBVIEW", "Vulkan subview preflight")
    require(body(read("src/renderer/ClassicSpecialFrameDomain.cpp"),
                 "bool R_ClassicSpecialFrameDomain_ReadyForBackend(", "special-frame readiness"),
            "SHARED_FAULT_INJECT_SPECIAL_FRAME", "special-frame readiness")
    require(body(read("src/renderer/ClassicCinematicPostDomain.cpp"),
                 "bool R_ClassicCinematicPostDomain_ReadyForBackend(", "cinematic/post readiness"),
            "SHARED_FAULT_INJECT_CINEMATIC_POST", "cinematic/post readiness")


def validate_scope_widening() -> None:
    gui = read("src/renderer/ClassicGuiDomain.cpp")
    require(gui, "packet->instanceRecord->negativeScale && !inWorld", "in-world GUI mirrored transform")
    ambient = body(read("src/renderer/ClassicWorldAmbientDomain.cpp"),
                   "static bool ForbiddenPassCategory( renderPassCategory_t category ) {", "ambient passes")
    allowed = ambient[:ambient.index("return false;")]
    for category in ("RENDER_PASS_ARB2_INTERACTION", "RENDER_PASS_STENCIL_SHADOW", "RENDER_PASS_SHADOW_MAP",
                     "RENDER_PASS_LIGHT_GRID", "RENDER_PASS_FOG_BLEND", "RENDER_PASS_SSAO"):
        require(allowed, category, "ambient phases beside classic passes")
    forbidden = ambient[ambient.index("return false;"):]
    for category in ("RENDER_PASS_FORWARD_PLUS", "RENDER_PASS_DEFERRED_RESOLVE",
                     "RENDER_PASS_SPECIAL_EFFECTS", "RENDER_PASS_GUI"):
        require(forbidden, category, "ambient forbidden passes")
    flush = body(read("src/renderer/RenderSystem.cpp"),
                 "void idRenderSystemLocal::SetUseUIViewportFor2D( bool enable ) {", "2D batch boundary")
    require(flush, "guiModel->HasSceneFeedback()", "2D batch boundary")
    reject(flush, "OPENQ4_RENDERER_VK_MODULE", "2D batch boundary")


def main() -> int:
    validate_gl_targets()
    validate_vulkan_targets()
    validate_packet_pools()
    validate_fault_injection()
    validate_scope_widening()
    print("renderer_shared_domain_targets: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
