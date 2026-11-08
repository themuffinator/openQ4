#!/usr/bin/env python3
"""Static integration contracts for the guarded Milestone F lighting work."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"required source is missing: {relative_path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {token!r} in {context}")


def section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    end = source.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        raise AssertionError(
            f"missing source section {start_marker!r}..{end_marker!r}"
        )
    return source[start:end]


def test_conservative_leaf_defaults_and_master_rollback() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    for name, default in (
        ("r_pbrMaterials", "1"),
        ("r_rendererModernQuality", "1"),
        ("r_rendererReflectionProbes", "0"),
        ("r_rendererClusteredDecals", "0"),
    ):
        require(
            init,
            f'idCVar {name}( "{name}", "{default}"',
            "Milestone F cvar defaults",
        )
    require(init, "0 rolls every domain back to classic ownership", "master rollback help")

    bootstrap = read("src/renderer/RendererBootstrap.cpp")
    for token in (
        '{ "r_rendererModernQuality", &r_rendererModernQuality, 1 }',
        '{ "r_pbrMaterials", &r_pbrMaterials, 1 }',
        '{ "r_rendererReflectionProbes", &r_rendererReflectionProbes, 0 }',
        '{ "r_rendererClusteredDecals", &r_rendererClusteredDecals, 0 }',
    ):
        require(bootstrap, token, "default-safety inventory")

    material_table = read("src/renderer/MaterialResourceTable.cpp")
    require(
        material_table,
        "if ( !r_rendererModernQuality.GetBool() || !r_pbrMaterials.GetBool() )",
        "PBR master rollback",
    )
    require(
        material_table,
        "return r_rendererModernQuality.GetBool()",
        "PBR eligibility master gate",
    )

    light_frontend = read("src/renderer/tr_light.cpp")
    probe_gate = section(
        light_frontend,
        "const bool specularProbeRequested",
        "// When its leaf/master gate",
    )
    require(probe_gate, "r_rendererModernQuality.GetBool()", "probe master gate")
    require(probe_gate, "r_rendererReflectionProbes.GetBool()", "probe leaf gate")

    draw_plan = read("src/renderer/ModernGLDrawPlan.cpp")
    decal_gate = section(
        draw_plan,
        "context.forwardPlusRequested",
        "for ( int kind",
    )
    require(decal_gate, "r_rendererModernQuality.GetBool()", "decal master gate")
    require(decal_gate, "r_rendererClusteredDecals.GetBool()", "decal leaf gate")

    # Either advanced-lighting leaf must request packet construction even when
    # no aggregate modern-visible diagnostic is enabled.
    scene_packets = read("src/renderer/ScenePackets.cpp")
    for token in (
        "advancedLightingLeafRequested",
        "r_rendererReflectionProbes.GetBool() || r_rendererClusteredDecals.GetBool()",
        "|| advancedLightingLeafRequested",
    ):
        require(scene_packets, token, "advanced-lighting leaf packet activation")

    backend = read("src/renderer/tr_backend.cpp")
    require(
        backend,
        "R_ModernClusteredLighting_ResetDecalsForFrame();",
        "empty-command stale decal reset",
    )


def test_probe_decal_bounds_and_shader_abi_markers() -> None:
    core = read("src/renderer/AdvancedLightingCore.h")
    require(core, "const int ADVANCED_LIGHTING_PROBES_PER_CLUSTER = 2;", "top-two probe bound")
    for token in (
        "ADVANCED_LIGHTING_RECORD_PROBE = 0",
        "ADVANCED_LIGHTING_RECORD_DECAL",
        "ADVANCED_LIGHTING_REJECT_CAPACITY",
        "ADVANCED_LIGHTING_REJECT_REFERENCE_CAPACITY",
        "volume.generation == 0 || volume.generation != generation",
        "const int total = state.probeReferences + state.decalReferences;",
        "total >= state.referenceCapacity",
    ):
        require(core, token, "advanced-lighting CPU contract")

    atlas = read("src/renderer/ModernSpecularProbeAtlas.h")
    for token in (
        "MODERN_SPECULAR_PROBE_ATLAS_SIZE = 2048",
        "MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE = 256",
        "MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT = 6",
        "MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES = 8",
        "slot >= MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES",
        "faceSize > MODERN_SPECULAR_PROBE_ATLAS_FACE_SIZE",
        "std::uint64_t atlasGeneration",
        "std::uint64_t residencyGeneration",
        "std::uint64_t sourceStorageGeneration",
        "faceRects[MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT][4]",
    ):
        require(atlas, token, "specular-probe atlas bound/ABI")

    clustered_header = read("src/renderer/ModernClusteredLighting.h")
    for token in (
        "RENDERER_CLUSTER_DECAL_MAX_RECORDS = 1024",
        "RENDERER_CLUSTER_DECAL_MAX_REFERENCES = 65536",
        "unsigned int\t\tmaterialStableId;",
        "unsigned int\t\tgeometryStableId;",
        "unsigned int\t\tinstanceStableId;",
        "unsigned int\t\tgeneration;",
        "RENDERER_CLUSTER_DECAL_REJECT_STALE_GENERATION",
        "RENDERER_CLUSTER_DECAL_REJECT_REFERENCE_CAPACITY",
    ):
        require(clustered_header, token, "clustered-decal bound/identity ABI")

    clustered = read("src/renderer/ModernClusteredLighting.cpp")
    for token in (
        "sourceCount > RENDERER_CLUSTER_DECAL_MAX_RECORDS",
        "referenceCount > RENDERER_CLUSTER_DECAL_MAX_REFERENCES",
        "totalReferences ) + referenceCount > RENDERER_CLUSTER_DECAL_MAX_REFERENCES",
        "!rg_clusteredDecalStats.ownershipReady",
        "candidate.priority = sphericalWeight > 0.0f ? probe.priority : -1;",
        "rg_clusteredDecalStats.generation != R_ModernClusteredLighting_CurrentDecalGeneration()",
    ):
        require(clustered, token, "clustered-decal bounded publication")

    shader = read("src/renderer/ModernGLShaderLibrary.cpp")
    for token in (
        '"#define MODERN_SPECULAR_PROBE_MAX_RECORDS 32\\n"',
        '"#define MODERN_SPECULAR_PROBE_ATLAS_SLOTS 8\\n"',
        '"    vec4 positionRadius;\\n"',
        '"    vec4 tintIntensity;\\n"',
        '"    vec4 axisXPriority;\\n"',
        '"    vec4 axisYBlend;\\n"',
        '"    vec4 axisZSlot;\\n"',
        '"    vec4 identity;\\n"',
        '"layout(std140) uniform ModernSpecularProbeRecords {\\n"',
        "probeIndex = candidate == 0 ? clusterRange.z : clusterRange.w",
        "probe.identity.w != frameGeneration",
    ):
        require(shader, token, "specular-probe shader ABI")


def test_clustered_decal_executor_is_atomic_and_exact() -> None:
    executor = read("src/renderer/ModernGLExecutor.cpp")
    clustered = read("src/renderer/ModernClusteredLighting.cpp")
    exact_surface = section(
        executor,
        "static bool R_ModernGLExecutor_IsExactSingleStageClusteredDecalSurface",
        "static bool R_ModernGLExecutor_IsExactSingleStageClusteredDecal",
    )
    for token in (
        "material->IsPortalSky()",
        "material->SuppressInSubview()",
        "stage->texture.image->GetOpts().textureType != TT_2D",
    ):
        require(exact_surface, token, "classic-suppressed clustered-decal rejection")

    transaction = section(
        executor,
        "static void R_ModernGLExecutor_PrepareClusteredDecalTransaction",
        "static bool R_ModernGLExecutor_ForwardPlusDecalOverlayAllowed",
    )
    for token in (
        "packetFrame.NumScenes()",
        "packetFrame.NumDrawPackets()",
        "packetStats.overflow",
        "R_ModernGLExecutor_IsRootWorldDrawScene",
        "sourceInventoryCount != authoritativeCount",
        "scene.firstDrawPacket",
        "viewDef->numDrawSurfs",
        "R_ModernGLExecutor_IsAuthoritativeClusteredDecalDraw",
        "command.drawPlanEntry->drawPacket == &draw",
        "R_ModernGLExecutor_ClusteredDecalCommandReady",
        "sourceCount != authoritativeCount",
        "R_ModernClusteredLighting_BindGridForView",
        "R_ModernClusteredLighting_PrepareDecals",
        "R_ModernClusteredLighting_SealDecals",
    ):
        require(transaction, token, "authoritative clustered-decal transaction")
    for token in (
        "geometry->ambientCacheBytes - surface->decalColorOffset",
        "R_ModernGLExecutor_BindClusteredDecalColorStream",
        "R_ModernGLExecutor_RestoreDrawVertColorStream",
        "MODERN_GL_DECAL_COLOR_BINDING_INDEX",
        "clusteredDecalSurface != NULL",
        "useLegacyViewportOffset ? RB_TileOffsetX() : 0",
        "useLegacyViewportOffset ? RB_TileOffsetY() : 0",
        "|| command.negativeScale",
        "R_ModernGLExecutor_TextureForCommand( command ) != binding->textureHandle",
        "binding->image->GetOpts().textureType != TT_2D",
    ):
        require(executor, token, "clustered-decal baked color-stream binding")

    texture_binding = section(
        executor,
        "static void R_ModernGLExecutor_BindMaterialTextures",
        "static bool R_ModernGLExecutor_ExerciseMaterialTextureTableForSelfTest",
    )
    for token in (
        "forceClassicTextureObjectSampling",
        "R_ModernGLExecutor_SetTextureTableMode( command, false )",
        "R_GLStateCache().BindTexture(",
        "R_GLStateCache().BindSampler( MODERN_GL_MATERIAL_TEXTURE_MAIN, 0 )",
    ):
        require(texture_binding, token, "clustered-decal exact texture sampling")

    legacy_view_restore = section(
        executor,
        "static void R_ModernGLExecutor_RestoreLegacyViewRect",
        "static void R_ModernGLExecutor_RestoreAfterForwardPlusDecalOverlay",
    )
    for token in (
        # A tiled capture moves only window draws (RB_TileOffsetX).
        "RB_TileOffsetX() + viewDef->viewport.x1",
        "RB_TileOffsetY() + viewDef->viewport.y1",
        "R_GLStateCache().SetViewport",
        "R_GLStateCache().SetScissor",
        "backEnd.currentScissor = viewDef->scissor",
    ):
        require(legacy_view_restore, token, "clustered-decal legacy view restoration")
    if "r_useScissor.GetBool()" in legacy_view_restore:
        raise AssertionError("clustered-decal view restoration must not depend on r_useScissor")

    overlay_gate = section(
        executor,
        "static bool R_ModernGLExecutor_ForwardPlusDecalOverlayAllowed",
        "static void R_ModernGLExecutor_RestoreAfterForwardPlusDecalOverlay",
    )
    for token in (
        "!r_rendererModernQuality.GetBool()",
        "!r_rendererClusteredDecals.GetBool()",
        "r_showOverDraw.GetInteger() != 0",
        "r_rendererSharedWorldAmbient.GetBool()",
        "R_ModernClusteredLighting_DecalViewStats",
    ):
        require(overlay_gate, token, "clustered-decal master/leaf/view gate")

    ownership_gate = section(
        clustered,
        "bool R_ModernClusteredLighting_DecalOwnsSurface",
        "static void R_ModernClusteredLighting_CountClusterReference",
    )
    if ownership_gate.count("r_skipDecals.GetBool()") != 2:
        raise AssertionError(
            "clustered-decal surface and command ownership must both honor r_skipDecals"
        )

    overlay = section(
        executor,
        "bool R_ModernGLExecutor_SubmitForwardPlusDecalSurface",
        "static bool R_ModernGLExecutor_SharedGuiOwnsCommand",
    )
    for token in (
        "R_ModernClusteredLighting_DecalSealedCommandForSurface(",
        "Modern clustered decal surface has duplicate sealed commands",
        "R_ModernClusteredLighting_BindGridForView( viewDef )",
        "GL_DST_COLOR, GL_ZERO",
        "GL_ZERO, GL_SRC_COLOR",
        "Modern clustered decal command failed after complete preflight",
    ):
        require(overlay, token, "exact sealed decal surface submission")
    if "R_ModernGLExecutor_RejectClusteredDecals" in overlay:
        raise AssertionError("sealed clustered-decal submission must not attempt late rollback")
    for forbidden in (
        "CommandMatchesForwardPlusDecalOverlayView",
        "CommandVisibleForModernPath",
        "ForwardPlusMaterialSupported",
    ):
        if forbidden in overlay:
            raise AssertionError(
                f"sealed clustered-decal overlay must not re-filter through {forbidden}"
            )

    polygon_offset = section(
        executor,
        "static bool R_ModernGLExecutor_ClusteredDecalPolygonOffset",
        "static bool R_ModernGLExecutor_ClusteredDecalDepthRange",
    )
    for token in (
        "stage->privatePolygonOffset != 0.0f",
        "polygonOffset = stage->privatePolygonOffset",
        "materialRecord->hasMaterialPolygonOffset",
        "polygonOffset = materialRecord->polygonOffset",
    ):
        require(polygon_offset, token, "classic clustered-decal polygon-offset precedence")
    if polygon_offset.index("stage->privatePolygonOffset != 0.0f") > polygon_offset.index(
        "materialRecord->hasMaterialPolygonOffset"
    ):
        raise AssertionError("stage-private polygon offset must override the material-wide value")
    for token in (
        "R_ModernGLExecutor_ClusteredDecalPolygonOffset(",
        "r_offsetUnits.GetFloat() * polygonOffset",
    ):
        require(overlay, token, "clustered-decal polygon-offset submission")

    executor_selftest = section(
        executor,
        "static bool R_ModernGLExecutor_ClusteredDecalPolygonOffsetSelfTest",
        "static bool R_ModernGLExecutor_InitSelfTestTriangleGeometry",
    )
    for token in (
        "privateOverridesMaterial",
        "privateAcceptedAlone",
        "polygonOffset == -3.0f",
        "polygonOffset == 4.0f",
    ):
        require(executor_selftest, token, "clustered-decal polygon-offset self-test")
    require(
        executor,
        "if ( !R_ModernGLExecutor_ClusteredDecalPolygonOffsetSelfTest() )",
        "clustered-decal polygon-offset self-test wiring",
    )

    clustered_header = read("src/renderer/ModernClusteredLighting.h")
    require(
        clustered_header,
        "R_ModernClusteredLighting_RejectDecalsForFrame",
        "explicit zero-ownership rejection API",
    )

    classic = read("src/renderer/draw_common.cpp")
    for token in (
        "RB_STD_DrawShaderPasses( drawSurfs, processed, RB_DrawSurfIsPreFogMaterialPass )",
        "R_ModernGLExecutor_SubmitForwardPlusDecalSurface",
    ):
        require(classic, token, "ordered one-for-one clustered-decal replacement")


def test_hiz_and_diagnostic_submit_own_complete_framebuffer_targets() -> None:
    executor = read("src/renderer/ModernGLExecutor.cpp")
    hiz_restore = section(
        executor,
        "static void R_ModernGLExecutor_RestoreAfterHiZBuild",
        "static bool R_ModernGLExecutor_PrimeGpuDrivenSelfTestHiZ",
    )
    diagnostic_submit = section(
        executor,
        "static void R_ModernGLExecutor_SubmitPlan",
        "static void R_ModernGLExecutor_SubmitModernGui",
    )
    for source, label in (
        (hiz_restore, "Hi-Z soft handoff"),
        (diagnostic_submit, "diagnostic submit"),
    ):
        require(source, "BindFramebuffer( GL_FRAMEBUFFER, 0 )", f"{label} framebuffer ownership")
        require(source, "glReadBuffer( GL_BACK )", f"{label} read target")
        require(source, "glDrawBuffer( GL_BACK )", f"{label} draw target")


def test_native_cpu_and_atlas_tests_are_wired() -> None:
    meson = read("meson.build")
    for token in (
        "'openq4-advanced-lighting-core-test'",
        "files('tools/tests/native/AdvancedLightingCoreTest.cpp')",
        "'openq4-advanced-lighting-core'",
        "'openq4-advanced-screen-space-core-test'",
        "files('tools/tests/native/AdvancedScreenSpaceCoreTest.cpp')",
        "'openq4-advanced-screen-space-core'",
        "'openq4-specular-probe-atlas-packing-test'",
        "files('tools/tests/native/ModernSpecularProbeAtlasPackingTest.cpp')",
        "'openq4-specular-probe-atlas-packing'",
    ):
        require(meson, token, "native Milestone F Meson wiring")

    validator = read("tools/validation/openq4_validate.py")
    require(validator, 'base.append("-Dbuild_native_tests=true")', "local native-test setup")
    require(validator, "wrapper + test_args(build_dir)", "local native-test execution")

    cpu_test = read("tools/tests/native/AdvancedLightingCoreTest.cpp")
    for token in (
        "ADVANCED_LIGHTING_REJECT_INVALID_GENERATION",
        "ADVANCED_LIGHTING_REJECT_CAPACITY",
        "reference overflow must be bounded",
        "numeric_limits<float>::quiet_NaN()",
        "the default-off master gate must publish no records",
    ):
        require(cpu_test, token, "native advanced-lighting coverage")

    atlas_test = read("tools/tests/native/ModernSpecularProbeAtlasPackingTest.cpp")
    for token in (
        "MODERN_SPECULAR_PROBE_ATLAS_MAX_ENTRIES",
        "MODERN_SPECULAR_PROBE_ATLAS_FACE_COUNT",
        "face rectangles must be disjoint",
        "oversized faces must not produce a placement",
    ):
        require(atlas_test, token, "native atlas-packing coverage")

    screen_space_test = read("tools/tests/native/AdvancedScreenSpaceCoreTest.cpp")
    for token in (
        "ADVANCED_SCREEN_SPACE_FROXEL_MAX_SLICES",
        "ADVANCED_SCREEN_SPACE_SSR_MAX_STEPS",
        "the GL/Vulkan eight-float packet must be stable",
        "the master rollback packet must be exactly zero-initialized",
    ):
        require(screen_space_test, token, "native screen-space lighting coverage")

    source_discovery = read("tools/build/meson_sources.py")
    if source_discovery.count('"src/renderer/ModernSpecularProbeAtlas.cpp"') != 1:
        raise AssertionError("specular-probe atlas implementation must be discovered exactly once")


def test_validation_entry_points_include_this_contract() -> None:
    validator = read("tools/validation/openq4_validate.py")
    if validator.count('root / "tools" / "tests" / "renderer_advanced_lighting.py"') != 1:
        raise AssertionError("local validation must include this contract exactly once")

    for workflow_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(workflow_path)
        if workflow.count("tools/tests/renderer_advanced_lighting.py") != 2:
            raise AssertionError(
                f"{workflow_path} must syntax-check and execute this contract exactly once"
            )


def test_parity_stays_unpromoted_and_follow_ons_have_separate_gates() -> None:
    executor = read("src/renderer/ModernGLExecutor.cpp")
    matches = re.findall(
        r"static const int\s+MODERN_LIGHTING_PARITY_PROVEN_DOMAINS\s*=\s*([^;]+);",
        executor,
    )
    if matches != ["0"]:
        raise AssertionError(
            "MODERN_LIGHTING_PARITY_PROVEN_DOMAINS must have one declaration equal to 0"
        )

    docs = "\n".join(
        (
            read("docs/dev/engine-capability-matrix.md"),
            read("docs/dev/archive/2026-10-18/idtech5-modernization-roadmap.md"),
            read("docs/dev/renderer-validation-matrix.md"),
        )
    )
    for feature in ("Froxel", "SSR", "SSGI"):
        require(docs, feature, "separate advanced-lighting follow-on documentation")
    if not re.search(
        r"Froxel[^\n]*(?:SSR[^\n]*SSGI|volumetrics)[^\n]*(?:separate|independent)",
        docs,
        re.IGNORECASE,
    ):
        raise AssertionError("froxel, SSR, and SSGI must remain independently gated")

    temporal = read("src/renderer/TemporalPresentation.cpp")
    for feature, symbol in (
        ("froxel", "r_rendererFroxelVolumetrics"),
        ("SSR", "r_rendererSSR"),
        ("SSGI", "r_rendererSSGI"),
    ):
        require(
            temporal,
            f'idCVar {symbol}( "{symbol}", "0"',
            f"{feature} explicit default-off gate",
        )
    require(
        temporal,
        "AdvancedScreenSpaceCore_Build(\n\t\tallowed && r_rendererModernQuality.GetBool()",
        "Milestone F master admission",
    )
    # VR eyes share one history per frame, so the history-backed effects stand down
    require(
        temporal,
        "const bool allowed = !R_TemporalPresentation_VRFrame();",
        "advanced screen space stands down while a VR frame presents",
    )

    core = read("src/renderer/AdvancedScreenSpaceCore.h")
    for token in (
        "ADVANCED_SCREEN_SPACE_EFFECT_FROXEL = 1 << 0",
        "ADVANCED_SCREEN_SPACE_EFFECT_SSR = 1 << 1",
        "ADVANCED_SCREEN_SPACE_EFFECT_SSGI = 1 << 2",
        "ADVANCED_SCREEN_SPACE_FROXEL_MAX_SLICES = 16",
        "ADVANCED_SCREEN_SPACE_SSR_MAX_STEPS = 16",
        "ADVANCED_SCREEN_SPACE_SSGI_TAPS = 8",
    ):
        require(core, token, "bounded independent screen-space contract")

    gl = read("src/renderer/draw_common.cpp")
    vk = read("src/renderer/Vulkan/shaders/temporal_resolve.frag")
    for source, label in ((gl, "OpenGL"), (vk, "Vulkan")):
        for token in (
            "ApplyFroxelVolumetrics",
            "ApplyScreenSpaceReflection",
            "ApplyScreenSpaceGI",
        ):
            require(source, token, f"{label} screen-space implementation")


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
    print("renderer_advanced_lighting: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
