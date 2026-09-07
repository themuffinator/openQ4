#!/usr/bin/env python3
"""Static regression contract for sealed classic material-deform ownership."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/renderer_classic_deform_domain.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_count(haystack: str, needle: str, expected: int, context: str) -> None:
    actual = haystack.count(needle)
    if actual != expected:
        raise AssertionError(
            f"Expected {expected} occurrence(s) of {needle!r} in {context}, got {actual}"
        )


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    previous = -1
    for needle in needles:
        position = haystack.find(needle, previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {needle!r} in {context}")
        if position <= previous:
            raise AssertionError(
                f"Expected ordered contracts in {context}: {needles!r}"
            )
        previous = position


def braced_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r} in {context}")
    opening = source.find("{", start + len(marker))
    if opening == -1:
        raise AssertionError(f"Missing opening brace after {marker!r} in {context}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def validate_sealed_record_contract() -> None:
    header = read("src/renderer/ClassicDeformDomain.h")
    source = read("src/renderer/ClassicDeformDomain.cpp")
    for token in (
        "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
        "CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER",
        "CLASSIC_DEFORM_ROLE_FOG_RECEIVER",
        "CLASSIC_DEFORM_ROLE_SHADOW_VOLUME",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "CLASSIC_DEFORM_OUTCOME_SKIPPED",
        "CLASSIC_DEFORM_OUTCOME_COMPLETED",
        "CLASSIC_DEFORM_OUTCOME_EMPTY",
        "CLASSIC_DEFORM_OUTCOME_FAILED",
        "CLASSIC_DEFORM_OUTCOME_UNSUPPORTED",
        "CLASSIC_DEFORM_CACHE_FRAME_TEMP",
        "ambientCacheHasBacking",
        "indexCacheHasBacking",
        "frameToken",
        "sourceMaterialNameHash",
        "resultMaterialNameHash",
        "deformDeclNameHash",
        "inputSemanticHash",
        "semanticHash",
        "R_ClassicDeformDomain_CurrentFrameToken",
        "R_ClassicDeformDomain_SnapshotDrawSurf",
        "R_ClassicDeformDomain_ValidateRecordForFrame",
        "R_ClassicDeformDomain_RecordFreshForFrame",
        "R_ClassicDeformDomain_RecordMatchesDrawSurf",
        "R_ClassicDeformDomain_SameProvenance",
        "R_ClassicDeformDomain_HasCompletedOutput",
        "R_ClassicDeformDomain_HasEmptyOutput",
        "R_ClassicDeformDomain_IsFailClosed",
        "RendererClassicDeformDomain_RunSelfTest",
    ):
        require(header, token, "sealed material-deform API")

    initializer = braced_body(
        source,
        "static void InitializeRecordFromDrawSurf(",
        "role-aware material-deform initialization",
    )
    for token in (
        "record.role = role;",
        "record.frameToken = frameToken;",
        "role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "r_skipDeforms.GetBool()",
        "CLASSIC_DEFORM_OUTCOME_SKIPPED",
        "KindIsUnsupported( record.kind )",
        "CLASSIC_DEFORM_OUTCOME_UNSUPPORTED",
        "CLASSIC_DEFORM_OUTCOME_FAILED",
    ):
        require(initializer, token, "role/outcome initialization")

    snapshot = braced_body(
        source,
        "void R_ClassicDeformDomain_SnapshotDrawSurf(",
        "explicit receiver snapshot",
    )
    require_order(
        snapshot,
        (
            "role != CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
            "InitializeRecordFromDrawSurf( drawSurf, role, frameToken, false, record );",
            "SealRecord( record );",
            "return;",
        ),
        "receiver NOT_APPLICABLE sealing",
    )

    completed = braced_body(
        source, "static bool CompletedOutputProvable(", "completed deform proof"
    )
    for token in (
        "MaterialIdentityMatches( record )",
        "result.geometry != record.sourceGeometry.geometry",
        "result.cacheStateValid",
        "result.ambientCacheHasBacking",
        "result.indexCacheHasBacking",
        "result.ambientTag == TAG_TEMP",
        "result.indexTag == TAG_TEMP",
        "CLASSIC_DEFORM_CACHE_FRAME_TEMP",
    ):
        require(completed, token, "completed output cache proof")

    empty = braced_body(source, "static bool EmptyOutputProvable(", "empty deform proof")
    for token in (
        "MaterialIdentityMatches( record )",
        "result.geometry != record.sourceGeometry.geometry",
        "result.indexCount == 0",
        "record.cpuFinalized",
    ):
        require(empty, token, "empty output proof")

    validator = braced_body(
        source, "static bool ValidateRecordInternal(", "sealed-record validation"
    )
    for token in (
        "record.frameToken == 0",
        "record.role <= CLASSIC_DEFORM_ROLE_UNKNOWN",
        "record.sourceGeometry.cacheStateValid",
        "record.resultGeometry.cacheStateValid",
        "R_ClassicDeformDomain_ComputeSemanticHash( record )",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "CLASSIC_DEFORM_OUTCOME_COMPLETED",
        "CompletedOutputProvable( record )",
        "CLASSIC_DEFORM_OUTCOME_EMPTY",
        "EmptyOutputProvable( record )",
        "CLASSIC_DEFORM_OUTCOME_FAILED",
        "CLASSIC_DEFORM_OUTCOME_UNSUPPORTED",
    ):
        require(validator, token, "role/outcome/cache/hash validation")

    semantic_hash = braced_body(
        source,
        "std::uint64_t R_ClassicDeformDomain_ComputeSemanticHash(",
        "address-independent deform hash",
    )
    for token in (
        "CONTRACT_HASH_VERSION",
        "record.role",
        "record.kind",
        "record.outcome",
        "record.sourceMaterialNameHash",
        "record.resultMaterialNameHash",
        "record.deformDeclNameHash",
        "record.inputSemanticHash",
        "record.sourceGeometry",
        "record.resultGeometry",
        "record.parameterValues",
        "record.cpuFinalized",
    ):
        require(semantic_hash, token, "semantic deform hash")
    for token in ("record.frameToken", "ambientBuffer", "indexBuffer"):
        reject(semantic_hash, token, "address/frame/backend-independent semantic hash")

    freshness = braced_body(
        source,
        "bool R_ClassicDeformDomain_RecordFreshForFrame(",
        "frame freshness proof",
    )
    for token in ("frameToken != 0", "record.initialized", "record.frameToken == frameToken"):
        require(freshness, token, "frame freshness proof")

    provenance = braced_body(
        source,
        "bool R_ClassicDeformDomain_SameProvenance(",
        "material-deform provenance identity",
    )
    for token in (
        "a.frameToken == b.frameToken",
        "a.semanticHash == b.semanticHash",
        "a.role == b.role",
        "a.outcome == b.outcome",
        "a.sourceMaterial == b.sourceMaterial",
        "a.resultMaterial == b.resultMaterial",
        "GeometryStateMatches( a.sourceGeometry, b.sourceGeometry )",
        "GeometryStateMatches( a.resultGeometry, b.resultGeometry )",
    ):
        require(provenance, token, "material-deform provenance identity")

    fail_closed = braced_body(
        source, "bool R_ClassicDeformDomain_IsFailClosed(", "atomic deform fallback"
    )
    for token in (
        "!R_ClassicDeformDomain_ValidateRecord( record )",
        "CLASSIC_DEFORM_OUTCOME_COMPLETED",
        "CLASSIC_DEFORM_OUTCOME_EMPTY",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
    ):
        require(fail_closed, token, "completed/empty acceptance and fail-closed outcomes")

    selftest = braced_body(
        source,
        "bool RendererClassicDeformDomain_RunSelfTest(",
        "material-deform runtime self-test",
    )
    for token in (
        "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
        "CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER",
        "CLASSIC_DEFORM_OUTCOME_COMPLETED",
        "R_ClassicDeformDomain_ValidateRecordForFrame( completed, 1 )",
        "R_ClassicDeformDomain_HasCompletedOutput( completed )",
        "driftedMaterial.resultMaterial =",
        "driftedMaterial.resultMaterialIndex++;",
        "driftedMaterial.resultMaterialNameHash++;",
        "R_ClassicDeformDomain_HasCompletedOutput( driftedMaterial )",
        "ambientCacheHasBacking = true",
        "same.frameToken = 2",
        "same.resultGeometry.ambientBuffer = 99",
        "R_ClassicDeformDomain_SameProvenance( completed, same )",
        "same.parameterValues[ 0 ] = 3.0f",
        "CLASSIC_DEFORM_OUTCOME_EMPTY",
        "R_ClassicDeformDomain_HasEmptyOutput( empty )",
        "R_ClassicDeformDomain_HasEmptyOutput( driftedMaterial )",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "CLASSIC_DEFORM_OUTCOME_SKIPPED",
        "CLASSIC_DEFORM_OUTCOME_FAILED",
        "CLASSIC_DEFORM_KIND_PARTICLE",
        "CLASSIC_DEFORM_OUTCOME_UNSUPPORTED",
        "R_ClassicDeformDomain_IsFailClosed",
    ):
        require(selftest, token, "outcome/freshness/hash/cache runtime coverage")


def validate_frontend_packet_provenance() -> None:
    local = read("src/renderer/tr_local.h")
    light = read("src/renderer/tr_light.cpp")
    packet_header = read("src/renderer/ScenePackets.h")
    packet_source = read("src/renderer/ScenePackets.cpp")
    require(local, '#include "ClassicDeformDomain.h"', "draw-surface deform API")
    require(local, "classicDeformRecord_t\tclassicDeform;", "draw-surface sealed record")

    material_init = braced_body(
        read("src/renderer/Material.cpp"),
        "void idMaterial::CommonInit()",
        "material deform-declaration initialization",
    )
    require(material_init, "deformDecl = NULL;", "safe non-table deform capture")

    finalize = braced_body(light, "void R_FinalizeDrawSurf(", "authoritative CPU deform call")
    require_order(
        finalize,
        (
            "R_ClassicDeformDomain_BeginDrawSurf( drawSurf );",
            "R_DeformDrawSurf( drawSurf );",
            "R_ClassicDeformDomain_EndDrawSurf( drawSurf );",
        ),
        "CPU deform begin/end bracket",
    )

    if packet_header.count("hasClassicDeformRecord;") < 2:
        raise AssertionError("Draw and geometry packets must both seal deform provenance")
    require(
        packet_header,
        "const classicDeformRecord_t *classicDeformRecord;",
        "compact draw-packet deform reference",
    )
    require_count(
        packet_header,
        "classicDeformRecord_t\tclassicDeform;",
        1,
        "single geometry-owned deform record",
    )
    for token in (
        "drawPacketsWithClassicDeformRecord",
        "materialDeformDrawPackets",
        "deformFinalizedDrawPackets",
        "deformInteractionReceiverPackets",
        "deformFogReceiverPackets",
        "deformShadowVolumePackets",
        "deformCompletedPackets",
        "deformEmptyPackets",
        "deformNotApplicablePackets",
        "deformSkippedPackets",
        "deformFailedPackets",
        "deformUnsupportedPackets",
        "deformFallbackPackets",
    ):
        require(packet_header, token, "scene-packet deform diagnostics")

    role_classifier = braced_body(
        packet_source,
        "static classicDeformRole_t R_ScenePackets_ClassicDeformRoleForPass(",
        "explicit packet deform roles",
    )
    for category, role in (
        ("RENDER_PASS_ARB2_INTERACTION", "CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER"),
        ("RENDER_PASS_FOG_BLEND", "CLASSIC_DEFORM_ROLE_FOG_RECEIVER"),
        ("RENDER_PASS_STENCIL_SHADOW", "CLASSIC_DEFORM_ROLE_SHADOW_VOLUME"),
        ("default:", "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW"),
    ):
        require_order(role_classifier, (category, role), "packet deform-role classification")

    lookup_key = braced_body(
        packet_source,
        "static int R_ScenePackets_GeometryLookupKey(",
        "provenance-sensitive geometry lookup key",
    )
    for token in (
        "classicDeform.semanticHash",
        "classicDeform.frameToken",
        "classicDeform.role",
    ):
        require(lookup_key, token, "provenance-sensitive geometry lookup key")

    geometry = braced_body(
        packet_source,
        "int idScenePacketFrame::FindOrAddGeometryRecord(",
        "provenance-sensitive geometry record",
    )
    for token in (
        "R_ScenePackets_GeometryLookupKey( geo, classicDeform )",
        "R_ClassicDeformDomain_SameProvenance(",
        "record.classicDeform = classicDeform;",
        "record.hasClassicDeformRecord =",
        "R_ClassicDeformDomain_ValidateRecordForFrame( classicDeform,",
    ):
        require(geometry, token, "provenance-sensitive geometry record")
    if geometry.count("R_ClassicDeformDomain_SameProvenance(") < 2:
        raise AssertionError("Both hashed and linear geometry lookup paths must verify provenance")

    add_draw = braced_body(
        packet_source, "bool idScenePacketFrame::AddDrawPacket(", "draw-packet deform seal"
    )
    require_order(
        add_draw,
        (
            "R_ScenePackets_ClassicDeformRoleForPass( category )",
            "R_ClassicDeformDomain_SnapshotDrawSurf(",
            "FindOrAddGeometryRecord(",
            "drawSurf, classicDeform )",
            "packet.classicDeformRecord = packet.geometryRecord != NULL",
            "&packet.geometryRecord->classicDeform",
            "packet.hasClassicDeformRecord =",
        ),
        "role snapshot before geometry provenance lookup",
    )
    for token in (
        "packet.classicDeformRecord != NULL",
        "*packet.classicDeformRecord, deformFrameToken",
        "*packet.classicDeformRecord, classicDeform",
    ):
        require(add_draw, token, "geometry-owned draw-packet deform provenance")

    submit_plan = braced_body(
        read("src/renderer/ModernGLSubmitPlan.cpp"),
        "bool RendererModernGLSubmitPlan_RunSelfTest(",
        "large packet-arena self-test storage",
    )
    require_count(
        submit_plan,
        "idAutoPtr<idScenePacketFrame>",
        4,
        "heap-backed simultaneous packet fixtures",
    )
    reject(
        submit_plan,
        "idScenePacketFrame packetFrame;",
        "render-thread stack packet fixture",
    )
    for arena in ("idModernGLDrawPlan", "idModernGLSubmitPlan"):
        require_count(submit_plan, f"idAutoPtr<{arena}>", 4, "heap-backed draw/submit fixtures")

    pbr_test = braced_body(
        read("src/renderer/ModernGLExecutor.cpp"),
        "bool RendererPBRVisible_RunSelfTest(",
        "PBR fixture stack budget",
    )
    for arena in ("idScenePacketFrame", "idModernGLDrawPlan", "idModernGLSubmitPlan"):
        require_count(pbr_test, f"idAutoPtr<{arena}>", 2, "heap-backed simultaneous PBR fixtures")


def validate_shared_domain_admission() -> None:
    domains = (
        (
            "GUI",
            "src/renderer/ClassicGuiDomain.h",
            "src/renderer/ClassicGuiDomain.cpp",
            "CLASSIC_GUI_DOMAIN_FAILURE_DEFORM_CONTRACT",
            ("materialDeformSurfaces", "completedDeformSurfaces", "emptyDeformSurfaces"),
        ),
        (
            "world ambient",
            "src/renderer/ClassicWorldAmbientDomain.h",
            "src/renderer/ClassicWorldAmbientDomain.cpp",
            "CLASSIC_WORLD_AMBIENT_FAILURE_DEFORM_CONTRACT",
            ("materialDeformSurfaces", "completedDeformSurfaces", "emptyDeformSurfaces"),
        ),
        (
            "interaction",
            "src/renderer/ClassicInteractionDomain.h",
            "src/renderer/ClassicInteractionDomain.cpp",
            "CLASSIC_INTERACTION_FAILURE_DEFORM_CONTRACT",
            ("materialDeformReceivers", "completedDeformCasters", "emptyDeformCasters"),
        ),
        (
            "fog/blend",
            "src/renderer/ClassicFogBlendDomain.h",
            "src/renderer/ClassicFogBlendDomain.cpp",
            "CLASSIC_FOG_BLEND_FAILURE_DEFORM_CONTRACT",
            ("materialDeformReceivers",),
        ),
    )
    for name, header_path, source_path, failure, counters in domains:
        header = read(header_path)
        source = read(source_path)
        require(header, failure, f"{name} named deform-contract fallback")
        for token in ("deformRole", "deformOutcome", "deformContractHash", *counters):
            require(header, token, f"{name} deform disposition and accounting")
        for token in (
            "r_rendererSharedDeform.GetBool()",
            "R_ClassicDeformDomain_ValidateRecordForFrame(",
            failure,
        ):
            require(source, token, f"{name} fail-closed deform admission")

    gui = read("src/renderer/ClassicGuiDomain.cpp")
    ambient = read("src/renderer/ClassicWorldAmbientDomain.cpp")
    for source, context in ((gui, "GUI"), (ambient, "world ambient")):
        for token in (
            "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
            "R_ClassicDeformDomain_RecordMatchesDrawSurf(",
            "R_ClassicDeformDomain_SameProvenance(",
            "R_ClassicDeformDomain_HasCompletedOutput( record )",
            "R_ClassicDeformDomain_HasEmptyOutput( record )",
            "CLASSIC_DEFORM_OUTCOME_COMPLETED",
            "CLASSIC_DEFORM_OUTCOME_EMPTY",
        ):
            require(source, token, f"{context} completed/empty deform acceptance")

    interaction = read("src/renderer/ClassicInteractionDomain.cpp")
    for token in (
        "CLASSIC_DEFORM_ROLE_INTERACTION_RECEIVER",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
        "R_ClassicDeformDomain_HasCompletedOutput(",
        "R_ClassicDeformDomain_HasEmptyOutput(",
        "R_ClassicDeformDomain_SameProvenance(",
    ):
        require(interaction, token, "interaction receiver/caster deform split")
    mapped_chain = braced_body(
        interaction,
        "static bool PrepareMappedShadowChain(",
        "mapped deform-caster isolation",
    )
    require_order(
        mapped_chain,
        (
            "if ( MaterialRequestsClassicDeform( drawSurf ) )",
            "if ( !AppendEmptyMappedDeformCaster(",
            "return false;",
        ),
        "disabled deform ownership must block empty mapped casters",
    )
    reject(
        mapped_chain,
        "MaterialRequestsClassicDeform( drawSurf )\n\t\t\t\t\t&& r_rendererSharedDeform.GetBool()",
        "disabled deform ownership mapped-caster bypass",
    )

    interaction_validator = braced_body(
        interaction,
        "static bool ValidateDeformContract(",
        "interaction deform authorization",
    )
    require(
        interaction_validator,
        "!r_rendererSharedDeform.GetBool() || r_skipDeforms.GetBool()",
        "interaction independent switch and skip isolation",
    )

    noop_validator = braced_body(
        interaction,
        "bool R_ClassicInteractionDomain_ShadowCasterNoopValid(",
        "backend-neutral mapped no-op validation",
    )
    for token in (
        "CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY",
        "caster.selectedIndexCount != 0",
        "caster.alphaStageCount != 0",
        "caster.firstAlphaStage != -1",
        "caster.deformContractHash != 0",
        "CLASSIC_DEFORM_ROLE_FINALIZED_DRAW",
        "CLASSIC_DEFORM_OUTCOME_EMPTY",
        "caster.vertexCount == 0",
        "caster.totalIndexCount == 0",
        "caster.deformContractHash == 0",
        "caster.materialCoverage == MC_PERFORATED",
        "caster.vertexCount > 0",
        "caster.totalIndexCount > 0",
    ):
        require(noop_validator, token, "sealed-empty versus inactive-perforated proof")
    interaction_selftest = braced_body(
        interaction,
        "bool RendererClassicInteractionDomain_RunSelfTest(",
        "mapped no-op native fixtures",
    )
    if interaction_selftest.count(
        "R_ClassicInteractionDomain_ShadowCasterNoopValid( noopCaster )"
    ) < 5:
        raise AssertionError("Native interaction fixtures must cover all no-op variants")

    fog = read("src/renderer/ClassicFogBlendDomain.cpp")
    for token in (
        "CLASSIC_DEFORM_ROLE_FOG_RECEIVER",
        "CLASSIC_DEFORM_OUTCOME_NOT_APPLICABLE",
        "packet.classicDeformRecord\n\t\t\t\t!= &packet.geometryRecord->classicDeform",
    ):
        require(fog, token, "fog receiver NOT_APPLICABLE contract")
    prepare_fog = braced_body(
        fog, "static bool PrepareView(", "fog/blend deform isolation"
    )
    reject(
        prepare_fog,
        "MaterialRequestsClassicDeform( drawSurf )\n\t\t\t\t\t\t\t&& r_rendererSharedDeform.GetBool()",
        "disabled deform ownership no-op bypass",
    )
    fog_validator = braced_body(
        fog,
        "static bool ValidateReceiverDeformContract(",
        "fog/blend deform authorization",
    )
    require(
        fog_validator,
        "!r_rendererSharedDeform.GetBool() || r_skipDeforms.GetBool()",
        "fog/blend independent switch and skip isolation",
    )


def validate_backend_preflight_transactions() -> None:
    gl = read("src/renderer/draw_common.cpp")
    vk = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")

    for backend_path in (
        "src/renderer/draw_arb2.cpp",
        "src/renderer/Vulkan/vk_ShadowMap.cpp",
    ):
        require(
            read(backend_path),
            "R_ClassicInteractionDomain_ShadowCasterNoopValid(",
            f"{backend_path} shared no-op predicate",
        )

    gl_preflight = braced_body(
        gl,
        "static bool RB_SharedWorldAmbientGLPreflight(",
        "OpenGL complete-view preflight",
    )
    reject(gl_preflight, "RB_DrawElementsWithCounters(", "draw-free OpenGL preflight")
    gl_handoff = braced_body(gl, "void\tRB_STD_DrawView(", "OpenGL ambient handoff")
    require_order(
        gl_handoff,
        (
            "RB_PrepareSharedWorldAmbientView( backEnd.viewDef )",
            "RB_DrawSharedWorldAmbientPhase(",
        ),
        "OpenGL preflight before deform-bearing commit",
    )

    vk_preflight = braced_body(
        vk,
        "static bool VK_ClassicWorldAmbient_Preflight(",
        "Vulkan complete-view geometry transaction",
    )
    for token in (
        "VK_Exec_SharedGeometryCheckpoint()",
        "VK_Exec_PrepareTriGeometryOffsets(",
        "VK_Exec_SharedGeometryRestore();",
        "VK_Exec_SharedGeometryCommit();",
        "prepared.ready = true;",
    ):
        require(vk_preflight, token, "Vulkan checkpoint/restore/commit transaction")
    require_order(
        vk_preflight,
        (
            "VK_Exec_SharedGeometryCheckpoint()",
            "VK_Exec_PrepareTriGeometryOffsets(",
            "VK_Exec_SharedGeometryCommit();",
            "prepared.ready = true;",
        ),
        "Vulkan geometry preflight before commit",
    )
    for token in ("vkCmdDrawIndexed(", "vkCmdClearAttachments("):
        reject(vk_preflight, token, "framebuffer-write-free Vulkan preflight")

    vk_handoff = braced_body(
        vk, "void VK_GuiExecutor_Draw3DView(", "Vulkan framebuffer handoff"
    )
    require_order(
        vk_handoff,
        (
            "VK_ClassicWorldAmbient_Preflight( viewDef )",
            "vkCmdClearAttachments(",
            "VK_ClassicWorldAmbient_DrawPhase(",
        ),
        "Vulkan preflight before framebuffer commit",
    )


def validate_controls_diagnostics_and_tooling() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    local = read("src/renderer/tr_local.h")
    bootstrap = read("src/renderer/RendererBootstrap.cpp")
    scene_packets = read("src/renderer/ScenePackets.cpp")
    modern = read("src/renderer/ModernGLExecutor.cpp")
    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    baseline = read("tools/validation/stock_asset_baseline.py")
    baseline_test = read("tools/tests/stock_asset_baseline.py")
    matrix = read("tools/tests/renderer_validation_matrix.py")
    registry = read("tools/validation/openq4_validate.py")

    require(
        init,
        'idCVar r_rendererSharedDeform( "r_rendererSharedDeform", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL',
        "default-off archived deform control",
    )
    for token in (
        '"rendererClassicDeformDomainSelfTest"',
        "RendererClassicDeformDomain self-test passed",
        "Renderer shared material deform: requested=%d frameToken=%llu records=%d none=%d notApplicable=%d completed=%d empty=%d skipped=%d failed=%d unsupported=%d invalid=%d hash=%016llx",
    ):
        require(init, token, "deform self-test and gfxInfo diagnostics")
    require(local, "extern idCVar r_rendererSharedDeform;", "deform cvar API")
    require(
        bootstrap,
        '{ "r_rendererSharedDeform", &r_rendererSharedDeform, 0 }',
        "bootstrap default safety",
    )
    require(bootstrap, "sharedDeform=%d", "bootstrap deform diagnostics")
    require(scene_packets, "|| r_rendererSharedDeform.GetBool()", "scene-packet activation")
    require(modern, "&& !r_rendererSharedDeform.GetBool()", "aggregate modern isolation")

    for source, tokens, context in (
        (
            benchmark,
            (
                'append_set(args, "r_rendererSharedDeform", "0")',
                '"r_rendererSharedDeform 0"',
            ),
            "benchmark default isolation",
        ),
        (
            baseline,
            (
                'add_set(args, "r_rendererSharedDeform", 0)',
                '"r_rendererSharedDeform 0"',
            ),
            "stock baseline isolation",
        ),
        (
            baseline_test,
            ('cvar_value(plan.args, "r_rendererSharedDeform") == "0"',),
            "stock baseline unit contract",
        ),
    ):
        for token in tokens:
            require(source, token, context)

    require(benchmark, '"sp-mv2-deform":', "controlled deform gameplay scene")
    deform_profile = braced_body(benchmark, '"deform":', "controlled deform profile")
    for token in (
        '"display": ("windowed",)',
        '("com_fixedTic", "1")',
        '("g_stopTime", "1")',
        '("g_stopTime", "0")',
        '("in_mouse", "0")',
        '"g_stopTime 1"',
        '"setviewpos 0 0 256 80 0 0"',
        '("r_rendererSharedWorldAmbient", "1")',
        '("r_rendererSharedDeform", "1")',
        '("r_materialOverride", "shaderDemos/move")',
        '("r_skipDeforms", "0")',
    ):
        require(deform_profile, token, "fixed stock material-deform capture")
    require(
        benchmark,
        "parsed.extra_cvars = profile_cvars + parse_extra_cvars(parsed.set_cvar)",
        "standard --set-cvar override ordering",
    )
    difference_helper = braced_body(
        benchmark,
        "def compare_screenshot_difference_if_requested(",
        "generic feature-delta comparison",
    )
    for token in (
        "controlled feature-on scene",
        "feature-disabled capture",
        "interaction shadows, fog/blend, and material deforms",
    ):
        require(difference_helper, token, "generic feature-delta comparison")
    reject(
        difference_helper,
        "controlled shadows-on scene",
        "shadow-only feature-delta documentation",
    )

    role_start = benchmark.find("def evaluate_role_result(")
    role_end = benchmark.find("\ndef launch_and_wait(", role_start)
    if role_start == -1 or role_end == -1:
        raise AssertionError("Missing evaluate_role_result function boundaries")
    role_evaluation = benchmark[role_start:role_end]
    for token in (
        "is_deform_scene = spec.case_id in DEFORM_SCENES",
        "difference_reference_dir is not None and difference_feature_case",
        "difference_reference_case_id = (",
        "is_deform_scene",
        "else spec.id_for_shadow_preset(\"unshadowed\")",
        "if difference_required",
        'if image_difference.get("pass") is False:',
        'missing.append(',
        '"image difference "',
    ):
        require(role_evaluation, token, "deform effect-delta gate")
    require_order(
        role_evaluation,
        (
            "difference_reference_case_id = (",
            "spec.id",
            "is_deform_scene",
            "else spec.id_for_shadow_preset(\"unshadowed\")",
            "difference_reference_case_id,",
            "if difference_required",
        ),
        "same-run deform difference-reference routing",
    )
    require(
        benchmark,
        "each eligible interaction-shadow, fog/blend, or material-deform capture must differ from",
        "generic difference-reference CLI help",
    )

    if matrix.count("+rendererClassicDeformDomainSelfTest") < 2:
        raise AssertionError("Foundation and Vulkan safe cases must run the deform self-test")
    if matrix.count("RendererClassicDeformDomain self-test passed") < 2:
        raise AssertionError("Foundation and Vulkan safe cases must check the deform marker")
    require(matrix, '"id": "sp-mv2-deform"', "manual deform validation case")
    require(matrix, '"profile": "deform"', "deform gameplay harness entry")
    require_count(
        registry,
        '"renderer_classic_deform_domain.py"',
        1,
        "validation registry",
    )

    for workflow_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(workflow_path)
        require_count(workflow, TEST_PATH, 2, f"{workflow_path} compile/run registration")

    require(read("meson.build"), "meson_sources.py", "renderer Meson source discovery")
    require(
        read("tools/build/meson_sources.py"),
        '"renderer/*.cpp"',
        "ClassicDeformDomain.cpp source discovery",
    )


def main() -> int:
    validate_sealed_record_contract()
    validate_frontend_packet_provenance()
    validate_shared_domain_admission()
    validate_backend_preflight_transactions()
    validate_controls_diagnostics_and_tooling()
    print("renderer_classic_deform_domain: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
