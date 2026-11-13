#!/usr/bin/env python3
"""Regression contracts for Vulkan world-light interaction parity."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/renderer_vulkan_world_interaction_compatibility.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def compact(value: str) -> str:
    return " ".join(value.split())


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_compact(haystack: str, needle: str, context: str) -> None:
    if compact(needle) not in compact(haystack):
        raise AssertionError(
            f"Missing compact source contract {compact(needle)!r} in {context}"
        )


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    compact_haystack = compact(haystack)
    previous = -1
    for needle in needles:
        position = compact_haystack.find(compact(needle), previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {compact(needle)!r} in {context}")
        if position <= previous:
            raise AssertionError(f"Expected ordered contracts in {context}: {needles!r}")
        previous = position


def braced_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r} in {context}")
    opening_brace = source.find("{", start + len(marker))
    if opening_brace == -1:
        raise AssertionError(f"Missing opening brace after {marker!r} in {context}")

    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def validate_stock_bump_sampling_contract() -> None:
    image_source = read("src/renderer/Vulkan/vk_Image.cpp")
    format_info = braced_body(
        image_source,
        "static bool VK_Image_GetFormatInfo(",
        "Vulkan image-format selection",
    )
    require_order(
        format_info,
        (
            "else if ( usage == TD_BUMP && opts.colorFormat != CFM_NORMAL_DXT5 )",
            "info.swizzle.a = VK_COMPONENT_SWIZZLE_R;",
        ),
        "Quake 4 bump red-to-alpha texture view",
    )

    fragment = read("src/renderer/Vulkan/shaders/interaction.frag")
    require_order(
        fragment,
        (
            "vec4 bumpSample = texture(bumpMap, bumpTexCoord);",
            # r_enhancedMaterials replaces only the decode and the specular
            # term (enhanced_material.glsl); the stock pair stays verbatim.
            "vec3 localNormal = EnhancedMaterialActive() ? EnhancedLocalNormal(bumpSample)",
            ": vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;",
            "float ndotl = max(dot(lightDir, localNormal), 0.0);",
            "vec3 specularSample = texture(specularMap, specularTexCoord).rgb;",
            "specularTerm = EnhancedSpecularTerm(halfAngle, SafeNormalize(vViewVector), localNormal, specularSample);",
            "specularTerm = texture(specularTableMap, vec2(specularDot, 0.5)).r * 2.0;",
            "vec3 specular = specularSample * inter.specularColor.rgb * specularTerm;",
            "outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);",
        ),
        "stock interaction fragment math",
    )


def validate_interaction_decomposition() -> None:
    source = read("src/renderer/Vulkan/vk_Interactions.cpp")
    create = braced_body(
        source,
        "static void VK_CreateSingleDrawInteractions(",
        "Vulkan primitive interaction decomposition",
    )
    require_order(
        create,
        (
            "r_skipInteractions.GetBool()",
            "VK_Exec_BindTriGeometry(",
            "VK_Exec_SetSurfScissor(",
            "surfaceShader->GetCullType()",
            "surfaceShader->TestMaterialFlag( MF_POLYGONOFFSET )",
            "R_GlobalPointToLocal(",
            "R_GlobalPlaneToLocal(",
            "const bool pbrOwnerEligible = VK_PBRHasSingleClassicInteractionTopology( surf );",
            "if ( !lightRegs[ lightStage->conditionRegister ] )",
            "RB_BakeTextureMatrixIntoTexgen(",
            "case SL_BUMP:",
            "VK_SubmitInteraction( &inter, false );",
            "case SL_DIFFUSE:",
            "VK_SubmitInteraction( &inter, false );",
            "case SL_SPECULAR:",
            "VK_SubmitInteraction( &inter, false );",
            "VK_SubmitInteraction( &inter, pbrOwnerEligible );",
            "VK_DrawCustomLightingStage(",
            "vkCmdSetDepthBiasEnable( interPass.cmd, VK_FALSE );",
        ),
        "Vulkan primitive interaction decomposition",
    )
    for token in (
        "surfaceRegs[ surfaceStage->conditionRegister ]",
        "surfaceStage->vertexColor",
        "backEnd.lightScale",
        "lightStage->color.registers",
        "inter.ambientLight = lightShader->IsAmbientLight()",
    ):
        require(create, token, "Vulkan primitive interaction decomposition")

    submit = braced_body(
        source,
        "static void VK_SubmitInteraction(",
        "stock interaction submission",
    )
    for token in (
        "r_skipDiffuse.GetBool()",
        "r_skipSpecular.GetBool()",
        "din->ambientLight",
        "r_skipBump.GetBool()",
        "globalImages->blackImage",
        "globalImages->flatNormalMap",
        "VK_DrawSingleInteraction( din, allowNativePBR )",
    ):
        require(submit, token, "stock interaction debug substitutions")

    custom_submit = braced_body(
        source,
        "static void VK_SubmitCustomLightingInteraction(",
        "customLighting submission",
    )
    for forbidden in ("r_skipDiffuse", "r_skipSpecular", "r_skipBump"):
        if forbidden in custom_submit:
            raise AssertionError(
                f"Raven customLighting stages must retain authored maps under {forbidden}"
            )
    require(
        custom_submit,
        "VK_DrawSingleInteractionMode( din, parallax, scaleBias[ 0 ], scaleBias[ 1 ], false );",
        "customLighting direct interaction submission",
    )

    custom_stage = braced_body(
        source,
        "static void VK_DrawCustomLightingStage(",
        "customLighting stage translation",
    )
    for token in (
        "VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT",
        "VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP",
        '"NormalMap"',
        '"DiffuseMap"',
        '"SpecularMap"',
        '"LightFalloffImage"',
        '"LightImage"',
        "surfaceRegs[ surfaceStage->conditionRegister ]",
        "VK_CustomLightingScaleBias(",
        "VK_SubmitCustomLightingInteraction(",
    ):
        require(custom_stage, token, "customLighting stage translation")


def validate_light_ownership_and_draw_state() -> None:
    source = read("src/renderer/Vulkan/vk_Interactions.cpp")
    draw_lights = braced_body(
        source,
        "void VK_Interactions_DrawLights(",
        "Vulkan world-light loop",
    )
    for token in (
        "vLight->lightShader->IsFogLight()",
        "vLight->lightShader->IsBlendLight()",
        "VK_SHADOW_RECEIVER_LOCAL",
        "VK_SHADOW_RECEIVER_GLOBAL",
        "vLight->globalShadows",
        "vLight->localShadows",
        "vLight->globalShadowMapStencilSupplements",
        "vLight->localShadowMapStencilSupplements",
        "vLight->localInteractions",
        "vLight->globalInteractions",
        "vLight->translucentInteractions",
        "VK_COMPARE_OP_EQUAL",
        "VK_COMPARE_OP_LESS_OR_EQUAL",
        "r_skipTranslucent.GetBool()",
        "r_shadowMapTranslucentReceivers.GetBool()",
        "r_stencilTranslucentShadows.GetBool()",
        "required shadow resource unavailable; affected light receivers fall back unshadowed",
    ):
        require(draw_lights, token, "Vulkan world-light ownership and draw state")
    require_order(
        draw_lights,
        (
            "VK_StencilShadowPass( localGlobalVolumes );",
            "VK_DrawInteractionChain( vLight->localInteractions );",
            "VK_StencilShadowPass( globalGlobalVolumes );",
            "VK_StencilShadowPass( globalLocalVolumes );",
            "VK_DrawInteractionChain( vLight->globalInteractions );",
        ),
        "retail and hybrid stencil ownership order",
    )

    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    interaction_pipeline = braced_body(
        executor,
        "VkPipeline VK_Exec_InteractionPipeline(",
        "Vulkan interaction pipeline",
    )
    require(
        interaction_pipeline,
        "GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE",
        "ONE:ONE additive interaction blend",
    )


def validate_scissor_and_depth_bounds_state() -> None:
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    scissor = braced_body(
        executor,
        "void VK_Exec_SetSurfScissor(",
        "Vulkan interaction scissor",
    )
    require_compact(
        scissor,
        """( r_useScissor.GetBool() && !drawSurf->scissorRect.IsEmpty() )
            ? drawSurf->scissorRect : viewDef->scissor""",
        "surface/view scissor selection",
    )
    # The clamp and the issue live in the shared rect helper, which the
    # view-level entry point (r_shadowMapDebugOverlay's restore) also uses.
    require(scissor, "fbHeight", "bounded Vulkan interaction scissor")
    require(
        scissor,
        "VK_Exec_SetScissorRect( cmd, viewDef,",
        "the surface scissor issues through the shared rect helper",
    )
    scissor_rect = braced_body(
        executor,
        "static void VK_Exec_SetScissorRect( VkCommandBuffer cmd, const viewDef_t *viewDef,",
        "Vulkan scissor rect helper",
    )
    for token in ("viewDef->viewport", "fbHeight", "vkCmdSetScissor"):
        require(scissor_rect, token, "bounded Vulkan interaction scissor")

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    stencil_clear = braced_body(
        interactions,
        "static void VK_Inter_StencilClear(",
        "per-light stencil clear",
    )
    require_compact(
        stencil_clear,
        """const idScreenRect &rect = r_useScissor.GetBool()
            ? vLight->scissorRect : viewDef->scissor;""",
        "light/view stencil-clear selection",
    )

    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    for token in (
        "features2.features.depthBounds = supported.depthBounds;",
        "vkCtx.depthBoundsSupported = supported.depthBounds == VK_TRUE;",
        "Vulkan: optional depth features clamp=%d bounds=%d",
    ):
        require(device, token, "optional depth-bounds feature enablement")

    create_pipeline = braced_body(
        executor,
        "static VkPipeline VK_Exec_CreatePipeline(",
        "Vulkan dynamic pipeline state",
    )
    for token in (
        "VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE",
        "VK_DYNAMIC_STATE_DEPTH_BOUNDS",
    ):
        require(create_pipeline, token, "Vulkan dynamic depth-bounds state")

    stencil_pass = braced_body(
        interactions,
        "static bool VK_StencilShadowPass(",
        "Vulkan stencil shadow pass",
    )
    require_order(
        stencil_pass,
        (
            "vkCmdSetDepthBoundsTestEnable( cmd, VK_TRUE );",
            "vkCmdSetDepthBounds( cmd, minDepth, maxDepth );",
            "vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );",
            "vkCmdSetDepthBounds( cmd, 0.0f, 1.0f );",
        ),
        "stencil shadow depth-bounds lifetime",
    )

    classic_draw = read("src/renderer/draw_common.cpp")
    classic_shadow = braced_body(
        classic_draw,
        "static void RB_T_Shadow(",
        "OpenGL stencil shadow pass",
    )
    require_compact(
        classic_shadow,
        """const float minDepth = idMath::ClampFloat( 0.0f, 1.0f,
            surf->scissorRect.zmin );
        const float maxDepth = idMath::ClampFloat( minDepth, 1.0f,
            surf->scissorRect.zmax );
        glDepthBoundsEXT( minDepth, maxDepth );""",
        "OpenGL ordered depth-bounds clamp",
    )


def validate_shared_interaction_consumer() -> None:
    source = read("src/renderer/Vulkan/vk_Interactions.cpp")
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    require(
        source,
        '#include "../ClassicInteractionDomain.h"',
        "Vulkan shared interaction contract include",
    )

    preflight = braced_body(
        source,
        "bool VK_ClassicInteraction_Preflight(",
        "Vulkan shared interaction preflight",
    )
    for token in (
        "R_ClassicInteractionDomain_FindView(",
        "VK_Exec_SharedInteractionTargetReady()",
        "VK_Exec_InteractionPipeline(",
        "VK_Exec_InteractionPipelineLayout(",
        "VK_Exec_SharedInteractionGeometryCheckpoint()",
        "VK_Exec_PrepareTriGeometry(",
        "VK_ClassicInteraction_ResolveDescriptor(",
        "VK_Exec_InteractionUniformCheckpoint(",
        "VK_Exec_InteractionUniformAlloc(",
        "VK_Exec_ShadowUniformAlloc(",
        "VK_Exec_SharedInteractionGeometryCommit()",
        "VK_ShadowMap_PreflightClassicInteractionView( view )",
        "R_ClassicInteractionDomain_LightShadowCaster(",
        "VK_Exec_PrepareShadowGeometry(",
        "prepared.ready = true;",
    ):
        require(preflight, token, "Vulkan shared interaction preflight")
    for forbidden in ("vkCmdDrawIndexed(", "VK_Interactions_DrawLights("):
        if forbidden in preflight:
            raise AssertionError(
                f"Shared interaction preflight must not submit owned work via {forbidden}"
            )
    require_order(
        preflight,
        (
            "VK_Exec_SharedInteractionTargetReady()",
            "VK_Exec_SharedInteractionGeometryCheckpoint()",
            "VK_Exec_PrepareTriGeometry(",
            "VK_Exec_InteractionUniformCheckpoint(",
            "VK_Exec_SharedInteractionGeometryCommit()",
            "prepared.ready = true;",
        ),
        "Vulkan target and geometry transaction before shared interaction allocation",
    )
    checkpoint_tail = preflight[
        preflight.index("VK_Exec_SharedInteractionGeometryCheckpoint()") :
    ]
    if re.search(r"(?m)^\s*return false;\s*$", checkpoint_tail):
        raise AssertionError(
            "Shared interaction preflight bypasses transactional failure cleanup"
        )

    fail = braced_body(
        source,
        "static bool VK_ClassicInteraction_Fail(",
        "Vulkan shared interaction transactional failure",
    )
    require(
        fail,
        "VK_ShadowMap_AbortClassicInteractionView(",
        "Vulkan shared mapped-shadow rollback",
    )
    require(
        fail,
        "VK_Exec_SharedInteractionGeometryRestore();",
        "Vulkan shared interaction geometry rollback",
    )
    require(
        fail,
        "VK_Exec_InteractionUniformRestore(",
        "Vulkan shared interaction uniform rollback",
    )

    geometry_checkpoint_entry = braced_body(
        executor,
        "bool VK_Exec_SharedInteractionGeometryCheckpoint(",
        "Vulkan shared interaction geometry checkpoint entry",
    )
    require(
        geometry_checkpoint_entry,
        "VK_Exec_SharedGeometryCheckpoint()",
        "Vulkan shared interaction geometry checkpoint delegation",
    )
    geometry_checkpoint = braced_body(
        executor,
        "static bool VK_Exec_SharedGeometryCheckpoint(",
        "Vulkan shared geometry checkpoint",
    )
    for token in (
        "vkExec.vertexRings[ vkExec.frameSlot ]",
        "vkExec.indexRings[ vkExec.frameSlot ]",
        "checkpoint.vertexCursor",
        "checkpoint.indexCursor",
        "checkpoint.boundVertexOffset",
        "memcpy( checkpoint.vertMemo, vkExec.vertMemo",
        "memcpy( checkpoint.idxMemo, vkExec.idxMemo",
        "checkpoint.active = true;",
    ):
        require(geometry_checkpoint, token, "Vulkan bounded geometry checkpoint")

    geometry_restore_entry = braced_body(
        executor,
        "void VK_Exec_SharedInteractionGeometryRestore(",
        "Vulkan shared interaction geometry restore entry",
    )
    require(
        geometry_restore_entry,
        "VK_Exec_SharedGeometryRestore();",
        "Vulkan shared interaction geometry restore delegation",
    )
    geometry_restore = braced_body(
        executor,
        "static void VK_Exec_SharedGeometryRestore(",
        "Vulkan shared geometry restore",
    )
    for token in (
        "vkExec.vertexRings[ checkpoint.frameSlot ].cursor",
        "vkExec.indexRings[ checkpoint.frameSlot ].cursor",
        "vkExec.boundVertexOffset = checkpoint.boundVertexOffset",
        "memcpy( vkExec.vertMemo, checkpoint.vertMemo",
        "memcpy( vkExec.idxMemo, checkpoint.idxMemo",
        "checkpoint.active = false;",
    ):
        require(geometry_restore, token, "Vulkan complete geometry restore")

    target_ready = braced_body(
        executor,
        "bool VK_Exec_SharedInteractionTargetReady(",
        "Vulkan shared interaction main-target query",
    )
    # REN-01: the shared domains draw where the classic walker draws - the
    # swapchain, the backend-owned scene target or the game's forward scene
    # target (bound as its own feedback source) - and nowhere else.
    for token in (
        "vkExec.frameOpen",
        "vkExec.mainScopeOpen",
        "vkExec.pendingSpecialEffectsView != NULL",
        "vkExec.pendingSpecialEffectsSource != NULL",
        "vkExec.pendingSpecialEffectsNeedsResolve",
        "vkExec.activeRenderTexture == NULL",
        "vkExec.activeColorEntries[ 0 ] == NULL",
        "vkExec.activeDepthEntry == NULL",
        "VK_Exec_PipelineTargetsMatch(",
        "VK_Exec_SwapchainPipelineTarget()",
        "vkExec.activeRenderTexture == vkExec.temporalSceneRenderTexture",
        "vkExec.temporalScenePendingComposite",
        "backEnd.renderTexture == backEnd.feedbackRenderTexture",
        "vkExec.activeRenderTexture == backEnd.renderTexture",
    ):
        require(target_ready, token, "Vulkan shared interaction main-target query")
    walk = braced_body(
        executor,
        "void VK_GuiExecutor_Draw3DView(",
        "Vulkan 3D walk",
    )
    if "!sceneScaleState.active && r_rendererShared" in walk:
        raise AssertionError(
            "Shared domains must be gated by VK_SharedDomainSceneAllowed, not by any scene target"
        )
    require(walk, "VK_SharedDomainSceneAllowed( sceneScaleState )",
            "Vulkan 3D walk shared-domain scene gate")

    owned = braced_body(
        source,
        "void VK_ClassicInteraction_DrawOwnedView(",
        "Vulkan sealed shared interaction draw",
    )
    receiver_draw = braced_body(
        source,
        "static void VK_ClassicInteraction_DrawReceiverRange(",
        "Vulkan sealed shared receiver draw",
    )
    shadow_draw = braced_body(
        source,
        "static void VK_ClassicInteraction_DrawShadowRange(",
        "Vulkan sealed shared stencil draw",
    )
    planned_shadow_work = braced_body(
        source,
        "static void VK_ClassicInteraction_CountPlannedShadowWork(",
        "Vulkan receiver-order physical stencil plan",
    )
    for token in (
        "VK_Exec_BindPreparedTriGeometry(",
        "vkCmdBindDescriptorSets(",
        "vkCmdPushConstants(",
        "vkCmdDrawIndexed(",
        "prepared.projectedInteractionPipeline",
        "prepared.pointInteractionPipeline",
        "prepared.mappedInteractionLayout",
        "setCount = plan.mappedShadowMode != 0 ? 8u : 7u",
        "plan.mappedShadowMode != 0 ? 2u : 1u",
    ):
        require(receiver_draw, token, "Vulkan sealed shared receiver draw")
    for token in (
        "prepared.shadows[",
        "VK_Exec_BindPreparedTriGeometry(",
        "vkCmdSetStencilOp(",
        "vkCmdDrawIndexed(",
        "submittedShadowCasters",
        "submittedLogicalVolumeDraws",
        "submittedPreloadVolumeDraws",
    ):
        require(shadow_draw, token, "Vulkan sealed shared stencil draw")
    for token in (
        "CLASSIC_INTERACTION_RECEIVER_LOCAL",
        "CLASSIC_INTERACTION_RECEIVER_GLOBAL",
        "CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT",
        "VK_ClassicInteraction_VolumeMode(",
        "preparedMode != globalMode",
        "preparedMode != globalMode || !preparedIncludesLocal",
        "preparedMode != translucentMode",
        "else if ( !preparedIncludesLocal )",
        "VK_ClassicInteraction_CountShadowRange(",
    ):
        require(
            planned_shadow_work,
            token,
            "Vulkan receiver-order physical stencil plan",
        )
    for token in (
        "VK_ClassicInteraction_CountPlannedShadowWork( prepared, lightPlan",
        "lightLogicalVolumeDraws != light->logicalVolumeDraws",
        "lightPreloadVolumeDraws != light->preloadVolumeDraws",
        "prepared.logicalVolumeDrawCount != view->logicalVolumeDrawCount",
        "prepared.preloadVolumeDrawCount != view->preloadVolumeDrawCount",
    ):
        require(preflight, token, "Vulkan physical stencil-plan reconciliation")
    for forbidden in ("logicalSubmitted", "preloadSubmitted"):
        if forbidden in source:
            raise AssertionError(
                "Vulkan physical replay coverage must not collapse receiver-order "
                f"submissions through {forbidden}"
            )
    require_order(
        shadow_draw,
        (
            "if ( caster.preload )",
            "vkCmdDrawIndexed(",
            "prepared.submittedPreloadVolumeDraws++",
            "prepared.submittedLogicalVolumeDraws++",
        ),
        "Vulkan physical volume counters follow draw submission",
    )
    require(
        owned,
        "modeChanged || !preparedVolumeIncludesLocal",
        "Vulkan GLOBAL receiver rebuild after mode switch or missing LOCAL family",
    )
    require_order(
        owned,
        (
            "preparedVolumeMode != translucentMode",
            "VK_ClassicInteraction_ClearStencil( prepared, lightPlan )",
            "lightPlan.firstShadow[ globalChain ]",
            "lightPlan.firstShadow[ localChain ]",
        ),
        "Vulkan translucent receiver physical replay after shadow-mode switch",
    )
    require(
        owned,
        "R_ClassicInteractionDomain_RecordOwned(",
        "Vulkan sealed ownership reconciliation",
    )
    require_order(
        owned,
        (
            "VK_ShadowMap_CommitClassicInteractionView( prepared.view )",
            "vkCmdSetViewport(",
            "R_ClassicInteractionDomain_RecordOwned(",
        ),
        "Vulkan mapped commit before visible shared interaction writes",
    )
    for token in (
        "CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL",
        "CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL",
        "prepared.view->shadowMapPassCount",
        "prepared.view->hybridShadowPassCount",
    ):
        require(owned, token, "Vulkan mapped/hybrid ownership submission")
    require_order(
        owned,
        (
            "VK_ClassicInteraction_ClearStencil( prepared, lightPlan );",
            "CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL",
            "CLASSIC_INTERACTION_RECEIVER_LOCAL",
            "CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL",
            "CLASSIC_INTERACTION_RECEIVER_GLOBAL",
            "CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT",
            "R_ClassicInteractionDomain_RecordOwned(",
        ),
        "Vulkan sealed stencil/receiver ownership order",
    )
    for forbidden in (
        "GetStage(",
        "shaderRegisters",
        "legacyDrawSurf",
        "legacyViewLight",
        "VK_Exec_ImageDescriptor(",
        "VK_Exec_InteractionUniformAlloc(",
        "R_ClassicInteractionDomain_ResolveTexture(",
    ):
        if forbidden in owned or forbidden in receiver_draw or forbidden in shadow_draw:
            raise AssertionError(
                f"Shared Vulkan draw must consume sealed plans, not {forbidden}"
            )

    map_preflight = braced_body(
        shadow_map,
        "bool VK_ShadowMap_PreflightClassicInteractionView(",
        "Vulkan mapped-shadow transaction preflight",
    )
    for token in (
        "R_ClassicInteractionDomain_LightShadowMapPass(",
        "CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC",
        "CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC",
        "CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC",
        "CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC",
        "sealed->mappedCasterCount",
        "sealed->drawableMappedCasters",
        "sealed->noopMappedCasters",
        "transaction.ready = true",
    ):
        require(map_preflight, token, "Vulkan mapped-shadow transaction preflight")
    for forbidden in ("vkCmdDraw", "vkCmdClear", "VK_ShadowMap_RenderAtlas("):
        if forbidden in map_preflight:
            raise AssertionError(
                f"Mapped-shadow preflight must not mutate attachments via {forbidden}"
            )

    draw_view = braced_body(
        executor,
        "void VK_GuiExecutor_Draw3DView(",
        "Vulkan 3D-view interaction ownership slot",
    )
    require_order(
        draw_view,
        (
            "VK_ClassicInteraction_Preflight( viewDef )",
            "if ( sharedWorldInteractionOwned )",
            "VK_ClassicInteraction_DrawOwnedView( viewDef );",
            "VK_Interactions_DrawLights( viewDef );",
        ),
        "Vulkan transactional shared interaction handoff",
    )


def validate_ci_registration() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    if validator.count("renderer_vulkan_world_interaction_compatibility.py") != 1:
        raise AssertionError(
            "Local validation must register the Vulkan world-interaction test exactly once"
        )
    for workflow, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(f"{context} must compile and directly run {TEST_PATH}")
        require(workflow, f"python {TEST_PATH}", context)


def main() -> None:
    validate_stock_bump_sampling_contract()
    validate_interaction_decomposition()
    validate_light_ownership_and_draw_state()
    validate_scissor_and_depth_bounds_state()
    validate_shared_interaction_consumer()
    validate_ci_registration()
    print("renderer_vulkan_world_interaction_compatibility: ok")


if __name__ == "__main__":
    main()
