#!/usr/bin/env python3
"""Static checks for the Apple GL 2.1 ARB2 compatibility corridor."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = "docs/dev/archive/2026-10-18/plans/2026-06-30-apple-support-no-macos-access.md"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def source_section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    if start == -1:
        raise AssertionError(f"Missing section marker {start_marker!r}")
    end = source.find(end_marker, start)
    if end == -1:
        raise AssertionError(f"Missing section end marker {end_marker!r}")
    return source[start:end]


def validate_context_aware_apple_gl21_quirk() -> None:
    header = read("src/renderer/RendererCaps.h")
    source = read("src/renderer/RendererCaps.cpp")
    init_source = read("src/renderer/RenderSystem_init.cpp")

    for token in (
        "int\t\t\t\t\t\t\tglMajor;",
        "int\t\t\t\t\t\t\tglMinor;",
        "rendererContextProfile_t\tprofile;",
        "bool\t\t\t\t\t\thasFixedFunctionCompatibility;",
    ):
        require(header, token, "renderer driver info selected context facts")

    parse_body = function_body(source, "static bool RendererDriverQuirk_ParseGLVersionPrefix( const char *version, int &major, int &minor ) {")
    host_stack_body = function_body(source, "static bool RendererDriverQuirk_HostRunsAppleGLStack( void ) {")
    apple_body = function_body(
        source,
        "static bool RendererDriverQuirk_IsAppleGL21CompatibilityFallback(\n"
        "\tconst renderBackendCaps_t &caps,\n"
        "\tconst rendererDriverInfo_t &driverInfo,\n"
        "\tbool *outHasCompatibilityShape = NULL ) {",
    )
    apply_body = function_body(source, "void RendererDriverQuirks_Apply( renderBackendCaps_t &caps, const rendererDriverInfo_t &driverInfo ) {")
    self_test = function_body(source, "bool RendererCompatibilityGates_RunSelfTest( void ) {")

    for token in (
        "while ( *cursor != '\\0' && ( *cursor < '0' || *cursor > '9' ) )",
        "parsedMajor = parsedMajor * 10",
        "parsedMinor = parsedMinor * 10",
    ):
        require(parse_body, token, "Apple GL version prefix parser")

    # Apple's GL reports the GPU vendor on Intel, AMD and NVIDIA Macs, so the
    # corridor cannot be gated on GL_VENDOR alone (issue #90).
    for token in (
        "#if defined( MACOS_X ) || defined( __APPLE__ )",
        "return true;",
        "return r_forceAppleGL21InteractionCorridor.GetBool();",
    ):
        require(host_stack_body, token, "Apple GL stack host predicate")

    for token in (
        'idStr::Icmp( driverInfo.vendor, "Apple" )',
        "vendorSaysApple || RendererDriverQuirk_HostRunsAppleGLStack()",
        "hasCompatibilityShape && appleGLStack",
        "outHasCompatibilityShape",
        "RendererDriverQuirk_ParseGLVersionPrefix",
        "reportsGL21",
        "selectedGL21",
        "selectedCompatibility",
        "driverInfo.profile == RENDERER_CONTEXT_PROFILE_COMPATIBILITY",
        "driverInfo.hasFixedFunctionCompatibility",
        "caps.profile == RENDERER_CONTEXT_PROFILE_COMPATIBILITY",
        "caps.hasFixedFunctionCompatibility",
    ):
        require(apple_body, token, "context-aware Apple GL 2.1 detector")

    for token in (
        "RendererDriverQuirk_IsAppleGL21CompatibilityFallback( caps, driverInfo, &appleGL21CompatibilityShape )",
        "OpenGL 2.1 compatibility shape seen but the Apple GL 2.1 corridor was not applied; GL_VENDOR='%s' GL_RENDERER='%s' GL_VERSION='%s'",
        "RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS",
        "Apple OpenGL 2.1 compatibility path uses a CPU-backed vertex cache with automatic stock GLSL interactions, simple ARB per-surface fallback, and an emergency interaction bypass",
        "selectedContext=%d.%d %s fixedFunction=%d VBO:%d->%d PBO:%d->%d simpleInteraction=%d ARB2:%d->%d",
    ):
        require(apply_body, token, "Apple GL 2.1 quirk application and logging")

    for token in (
        '"Apple M4 Max"',
        '"Apple M5"',
        '"Apple M4 Max macOS 15.x"',
        '"Apple M5 macOS 16 Tahoe"',
        '"unknown"',
        '"2.1 Metal"',
        '"OpenGL 2.1 Metal"',
        "RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS",
        '"Apple Silicon modern context"',
        "RENDERER_DRIVER_QUIRK_NONE",
        "expectedFlags",
        "expectedVBO",
        "caps.hasVBO != quirkCasesTable[i].expectedVBO",
        # the machines in issue #90: Apple GL reporting a non-Apple vendor
        '"Intel Inc."',
        '"ATI Technologies Inc."',
        '"NVIDIA Corporation"',
        "nonAppleVendorMacFlags",
        "nonAppleVendorMacVBO",
        "RendererDriverQuirk_HostRunsAppleGLStack()",
    ):
        require(self_test, token, "Apple GL 2.1 synthetic quirk cases")

    for token in (
        "glConfig.backendCaps.glMajor",
        "glConfig.backendCaps.glMinor",
        "glConfig.backendCaps.profile",
        "glConfig.backendCaps.hasFixedFunctionCompatibility",
    ):
        require(init_source, token, "renderer driver info runtime context facts")


def validate_simple_interaction_fail_closed() -> None:
    source = read("src/renderer/draw_arb2.cpp")
    init_source = read("src/renderer/RenderSystem_init.cpp")
    common_source = read("src/renderer/draw_common.cpp")
    render_source = read("src/renderer/tr_render.cpp")
    arb_init_body = function_body(source, "void R_ARB2_Init( void ) {")
    material_dispatch_body = function_body(source, "static void RB_DrawMaterialInteractions( const drawSurf_t *surf ) {")
    material_draw_body = function_body(source, "static bool RB_GLSLMaterial_CreateDrawInteractions( const drawSurf_t *surf, const bool forceNeutralEnhancements ) {")
    material_interaction_body = function_body(source, "static void RB_GLSLMaterial_DrawInteraction( const drawInteraction_t *din ) {")
    material_fragment_shader = read("content/baseoq4/pak0/glprogs/material_interaction.fs")
    arb_draw_body = source_section(
        source,
        "void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {",
        "static void RB_ARB2_DisableInteractionVertexAttribArrays( void ) {",
    )
    restore_body = function_body(source, "static void RB_ARB2_RestoreInteractionState( const bool recordBypassBreadcrumb ) {")
    reload_body = function_body(source, "void R_ReloadARBPrograms_f( const idCmdArgs &args ) {")
    failure_body = function_body(source, "static void RB_ErrorIfDriverRequiredSimpleInteractionFailed( void ) {")
    draw_view_body = function_body(common_source, "void\tRB_STD_DrawView( void ) {")
    determine_light_scale_body = function_body(render_source, "void RB_DetermineLightScale( void ) {")

    for token in (
        "RB_DriverPrefersSimpleInteraction()",
        "VPROG_SIMPLE_INTERACTION",
        "FPROG_SIMPLE_INTERACTION",
        "vertexRecord->valid",
        "fragmentRecord->valid",
        "Unsupported Apple OpenGL 2.1 compatibility path",
        "required SimpleInteraction.vfp ARB programs failed to load",
        "cannot safely continue on this driver path",
        "common->Error",
    ):
        require(failure_body, token, "Apple GL 2.1 simple interaction fail-closed path")

    for token in (
        "RB_RecordCurrentInteractionSelectionBreadcrumb();",
        "RB_ErrorIfDriverRequiredSimpleInteractionFailed();",
    ):
        require(reload_body, token, "ARB reload simple interaction fail-closed call")

    for token in (
        "0 = automatic stock GLSL interactions with simple ARB per-surface fallback",
        "1 = force simple ARB diagnostic",
        "2 = force full ARB diagnostic",
        "3 = emergency interaction bypass",
        "idCmdSystem::ArgCompletion_Integer<0,3>",
    ):
        require(init_source, token, "Apple GL 2.1 interaction policy cvar")

    for token in (
        "g_appleGL21AutomaticInteractionPath = true;",
        "interactionOverride == 0",
        "interactionOverride == 1",
        "interactionOverride == 2",
        "glConfig.disableARB2Interactions = true;",
        "emergency ARB2 light-interaction bypass",
    ):
        require(arb_init_body, token, "Apple GL 2.1 interaction policy initialization")

    for token in (
        "RB_AppleGL21AutomaticInteractionPath()",
        "RB_SurfaceEligibleForAppleGL21StockGLSLInteraction",
        "singleSurf.nextOnLight = NULL;",
        "RB_GLSLMaterial_CreateDrawInteractions( &singleSurf, true )",
        "RB_ARB2_CreateDrawInteractions( &singleSurf );",
    ):
        require(material_dispatch_body, token, "Apple GL 2.1 per-surface GLSL/simple routing")

    for token in (
        "forceNeutralEnhancements",
        "RB_MaterialInteractionSetEnhancementUniforms",
        "g_materialInteractionProgram.stockInteraction",
        "g_materialInteractionProgram.ambientNormalMap",
        "submittedInteractions = true;",
        "return submittedInteractions;",
    ):
        require(material_draw_body, token, "Apple GL 2.1 neutral stock GLSL interaction path")

    for token in (
        "din->ambientLight ? 1.0f : 0.0f",
        "globalImages->ambientNormalMap->Bind()",
    ):
        require(material_interaction_body, token, "Apple GL 2.1 ambient interaction uniforms")

    for token in (
        "uniform float uStockInteraction;",
        "uniform float uAmbientLight;",
        "uniform samplerCube uAmbientNormalMap;",
        "DecodeStockLocalNormal",
        "StockSpecularTerm",
        "textureCube( uAmbientNormalMap",
    ):
        require(material_fragment_shader, token, "Apple GL 2.1 stock-compatible GLSL interaction shader")

    for token in (
        "!RB_UseSimpleInteractionShader()",
        "RB_GLSLPrepareInteractionVertexCache( surf, ac )",
        "continue;",
    ):
        require(arb_draw_body, token, "Apple GL 2.1 hardened simple ARB fallback")
    reject(
        arb_draw_body,
        "vertexCache.Position( surf->geo->ambientCache )",
        "Apple GL 2.1 ARB fallback must use guarded cache preparation",
    )

    for token in (
        "RB_ARB2_ClearInteractionTextureState();",
        "RB_ARB2_DisableInteractionVertexAttribArrays();",
        "RB_ARB2_UnbindInteractionPrograms();",
        "glUseProgramObjectARB( 0 );",
        "recordBypassBreadcrumb",
        "GL_ClearStateDelta();",
    ):
        require(restore_body, token, "Apple GL 2.1 post-interaction state restore")

    for token in (
        "static bool RB_ShouldSkipFullInteractionUpload( const progDef_t &prog )",
        "RB_DriverPrefersSimpleInteraction()",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SKIP_FULL_UPLOAD",
        "skipped by renderer driver quirk; using SimpleInteraction.vfp",
        "RB_IsSimpleInteractionProgram( prog )",
    ):
        require(source, token, "Apple GL 2.1 skips full interaction upload and uses simple interaction")

    for token in (
        "glConfig.disableARB2Interactions",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_DRIVER_BYPASS",
        "Apple OpenGL 2.1 compatibility path bypassing ARB2 light interactions",
        "RB_ShadowMapStatsReset();",
        "RB_ShadowMapDebugOverlayReset();",
        "RB_ARB2_RestoreBypassedInteractionState();",
        "RB_ARB2_DisableInteractionVertexAttribArrays",
        "static const int attribs[] = { 1, 2, 5, 6, 7, 8, 9, 10, 11 };",
        "RB_ARB2_ClearInteractionTextureState",
        "GL_SelectTexture( unit );",
        "glDisable( GL_TEXTURE_GEN_S );",
        "glDisable( GL_TEXTURE_GEN_Q );",
        "RB_ARB2_UnbindInteractionPrograms",
        "glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );",
        "glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );",
        "GL_ClearStateDelta();",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_STATE_RESTORED",
    ):
        require(source, token, "Apple GL 2.1 bypasses fragile ARB2 light interactions")

    for token in (
        "tr.backEndRenderer == BE_ARB2 && glConfig.disableARB2Interactions",
        "backEnd.pc.maxLightValue = 1.0f;",
        "backEnd.lightScale = 1.0f;",
        "backEnd.overBright = 1.0f;",
    ):
        require(determine_light_scale_body, token, "issue #73 comment 4894876958 neutral light-scale fallback")

    for token in (
        "RB_ARB2InteractionBypassActive()",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE_SKIPPED",
        "ARB2 interaction bypass light scale skipped",
    ):
        require(common_source + read("src/renderer/RendererStartupDiagnostics.cpp"), token, "issue #73 comment 4894876958 light-scale skip breadcrumb")

    bypass_light_scale_section = source_section(
        draw_view_body,
        "if ( RB_ARB2InteractionBypassActive() ) {",
        "} else {",
    )
    for token in (
        "RB_STD_LightScale();",
        "glStencilFunc( GL_ALWAYS, 128, 255 );",
    ):
        reject(bypass_light_scale_section, token, "issue #73 comment 4894876958 light-scale bypass branch")


def validate_arb_entrypoint_and_binding_audit() -> None:
    init_source = read("src/renderer/RenderSystem_init.cpp")
    arb2_source = read("src/renderer/draw_arb2.cpp")
    arb_guard = function_body(init_source, "static bool R_HasARBProgramEntryPoints() {")
    bind_helper = function_body(arb2_source, "bool R_BindARBProgram( GLenum target, GLuint ident, const char *usage, bool required ) {")
    load_body = function_body(arb2_source, "void R_LoadARBProgram( int progIndex ) {")

    for token in (
        "glBindProgramARB != NULL",
        "glProgramStringARB != NULL",
        "glProgramEnvParameter4fvARB != NULL",
        "glProgramLocalParameter4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
    ):
        require(arb_guard, token, "ARB2 entry-point gate")

    for token in (
        "RB_FindARBProgramRecord",
        "prog == NULL || !prog->valid",
        "RB_WarnInvalidARBProgramUse",
        "glBindProgramARB( target, ident )",
    ):
        require(bind_helper, token, "ARB program binding helper")

    for token in (
        "if ( !glConfig.ARBVertexProgramAvailable )",
        "if ( !glConfig.ARBFragmentProgramAvailable )",
        "glProgramStringARB",
        "GL_PROGRAM_ERROR_POSITION_ARB",
        "RB_SetARBProgramFailure",
    ):
        require(load_body, token, "ARB program upload validation")


def validate_upload_and_vertex_cache_static_coverage() -> None:
    upload = read("src/renderer/RendererUpload.cpp")
    vertex_cache = read("src/renderer/VertexCache.cpp")
    vertex_cache_header = read("src/renderer/VertexCache.h")
    arb2 = read("src/renderer/draw_arb2.cpp")

    upload_init = function_body(upload, "void idUploadManager::Init( const renderBackendCaps_t &caps ) {")
    delete_helper = function_body(upload, "static void R_RendererUpload_DeleteBufferName( unsigned int &vbo ) {")
    position_body = function_body(vertex_cache, "void *idVertexCache::Position( vertCache_t *buffer ) {")
    init_body = function_body(vertex_cache, "void idVertexCache::Init() {")
    end_frame_body = function_body(vertex_cache, "void idVertexCache::EndFrame() {")
    attr_helper = function_body(vertex_cache_header, "ID_INLINE void *RB_DrawVertAttributePointer( const void *base, const size_t byteOffset ) {")
    interaction_section = source_section(arb2, "// set the vertex pointers", "// this may cause RB_ARB2_DrawInteraction")

    for token in (
        "if ( requestedPath == UPLOAD_PATH_DISABLED )",
        "frameBufferCount = 0;",
        "activeRingBytes = requestedPath == UPLOAD_PATH_DISABLED ? 0 : ringBytes",
        "hasSync = requestedPath == UPLOAD_PATH_PERSISTENT && syncAvailable",
    ):
        require(upload_init, token, "disabled renderer upload bridge state")

    for token in (
        "if ( glConfig.isInitialized && glDeleteBuffersARB != NULL )",
        "glDeleteBuffersARB( 1, &vbo )",
        "vbo = 0;",
    ):
        require(delete_helper, token, "guarded renderer upload buffer deletion")

    for token in (
        "if ( buffer->vbo )",
        "BindIndexBuffer( buffer->vbo )",
        "BindArrayBuffer( buffer->vbo )",
        "return reinterpret_cast<void *>( static_cast<uintptr_t>( buffer->offset ) )",
        "return (void *)((byte *)buffer->virtMem + buffer->offset)",
        "BindIndexBuffer( 0 )",
        "BindArrayBuffer( 0 )",
    ):
        require(position_body, token, "CPU-backed and VBO-backed vertex-cache position contract")

    cpu_position_section = source_section(
        position_body,
        "// Client-memory array and index pointers",
        "// virtual memory is a real pointer",
    )
    for token in ("BindArrayBuffer( 0 );", "BindIndexBuffer( 0 );"):
        require(cpu_position_section, token, "CPU vertex-cache pointer clears both buffer targets")
    reject(cpu_position_section, "if ( buffer->indexBuffer )", "CPU pointer binding invariant must clear both targets")

    virtual_init_section = source_section(
        init_body,
        "virtualMemory = true;",
        'common->Printf( "WARNING: vertex array range in virtual memory (SLOW)\\n" );',
    )
    for token in ("BindArrayBuffer( 0 );", "BindIndexBuffer( 0 );"):
        require(virtual_init_section, token, "virtual vertex-cache initialization binding invariant")
        require(end_frame_body, token, "vertex-cache end-of-frame binding invariant")
    reject(end_frame_body, "if( !virtualMemory )", "end-of-frame binding invariant must cover virtual caches")

    require(attr_helper, "reinterpret_cast<uintptr_t>( base ) + byteOffset", "VBO-safe draw-vertex attribute pointer helper")
    for token in (
        "RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET )",
    ):
        require(interaction_section, token, "classic ARB2 interaction VBO-safe offsets")

    for token in (
        "ac->color",
        "ac->normal.ToFloatPtr()",
        "ac->tangents[1].ToFloatPtr()",
        "ac->tangents[0].ToFloatPtr()",
        "ac->st.ToFloatPtr()",
        "ac->xyz.ToFloatPtr()",
    ):
        reject(interaction_section, token, "classic ARB2 interaction must not dereference VBO offset tokens")


def validate_phase3_plan_status() -> None:
    plan = read(PLAN_PATH)

    for token in (
        "## Phase 3: Harden The Apple GL 2.1 ARB2 Corridor",
        "- [x] Expand `RendererDriverQuirks` synthetic cases for known report strings:",
        "- [x] Make Apple GL 2.1 fallback detection depend on normalized vendor/version",
        "- [x] Log every driver quirk that changes `hasVBO`, simple-interaction",
        "- [x] Add a static test proving Apple GL 2.1 disables the VBO vertex cache even",
        "- [x] Add a static test proving Apple GL 2.1 prefers `SimpleInteraction.vfp`",
        "- [x] Add a static test proving Apple GL 2.1 bypasses ARB2 light-interaction",
        "- [x] Add a fail-closed path if the simple interaction program cannot load:",
        "- [x] Audit every `glBindProgramARB`, `glProgramStringARB`,",
        "- [x] Move repeated ARB program binding checks behind small helpers where that",
        "- [x] Add static coverage for disabled upload-manager state: no ring buffers, no",
        "- [x] Add static coverage for CPU-backed vertex-cache paths: all client-array",
        "- [x] Add static coverage for VBO-backed paths: all `idDrawVert` member offsets",
        "Phase 3 implementation status",
        "tools/tests/macos_apple_gl21_arb2_corridor.py",
    ):
        require(plan, token, "Phase 3 Apple support plan")


def validate_docs_and_wiring() -> None:
    support_doc = read("docs/user/macos-support-data.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")
    release_completion = read("docs/dev/release-completion.md")
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (support_doc, "macOS support-data guide"),
        (release_notes, "curated release notes"),
        (release_completion, "release completion notes"),
    ):
        require(source, "Unsupported Apple OpenGL 2.1 compatibility path", context)
        require(source, "SimpleInteraction.vfp", context)
        require(source, "ARB2 light interaction", context)
        require(source, "ARB2 interaction bypass light scale skipped", context)

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_apple_gl21_arb2_corridor.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_apple_gl21_arb2_corridor.py", context)


def validate_corridor_lighting_contract() -> None:
    """Pins the four lighting defects behind issue #73's "still not ok" image."""

    source = read("src/renderer/draw_arb2.cpp")
    fragment = read("content/baseoq4/pak0/glprogs/material_interaction.fs")

    # The stock ARB2 interaction normalizes the half-angle through the
    # normalization cube map. The GLSL replacement fed the raw interpolated
    # varying, whose magnitude reaches 2, into clamp( dot * 4 - 3 ).
    require(
        fragment,
        "vec3 halfAngle = SafeNormalize( vHalfAngleVector );",
        "unconditional half-angle normalization",
    )
    reject(
        fragment,
        "vec3 halfAngle = ( uStockInteraction > 0.5 ) ? vHalfAngleVector",
        "stock interaction must not use an unnormalized half-angle",
    )

    # deformedSurface is an ownership flag that every CPU-skinned MD5 surface
    # carries; it must not stand in for "GPU-posed".
    gpu_posed_body = function_body(source, "static bool RB_SurfaceUsesGPUPosedGeometry( const drawSurf_t *surf ) {")
    require(gpu_posed_body, "tri->primBatchMesh != NULL || ambientTri->primBatchMesh != NULL",
            "GPU-posed geometry predicate")
    reject(gpu_posed_body, "deformedSurface", "GPU-posed predicate must not read the ownership flag")
    # A classicized MD5R surface keeps the skin-to-model table for GPU
    # skinning's compute pass, but its vertex stream is already posed.
    reject(gpu_posed_body, "tri->skinToModelTransforms != NULL",
           "GPU-posed predicate must not treat the compute palette as packed batches")

    # The rebuilt self-test has to model the shape the engine really produces.
    receiver_self_test = function_body(source, "bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {")
    for token in (
        "cpuSkinnedGeo.deformedSurface = true;",
        "RB_SurfaceEligibleForShadowMapReceiver( &skinnedReceiver )",
        "gpuPosedGeo.numSkinToModelTransforms = 1;",
        "gpuPosedGeo.primBatchMesh = reinterpret_cast<rvMesh *>( &gpuPosedBatchToken );",
        "RB_SurfaceEligibleForShadowMapReceiver( &classicMD5RReceiver )",
        "expectedGeneratedReason",
    ):
        require(receiver_self_test, token, "CPU-skinned receiver self-test")

    # A failed GLSL interaction load has to be remembered, or it recompiles per
    # surface per light per frame.
    load_body = function_body(source, "static bool RB_MaterialInteractionLoadProgram( void ) {")
    require(
        load_body,
        "if ( g_materialInteractionProgram.programGeneration == tr.videoRestartCount ) {",
        "failed GLSL interaction load caching",
    )
    reject(
        load_body,
        "g_materialInteractionProgram.programObject != 0 && g_materialInteractionProgram.programGeneration",
        "load cache must not key on the resulting program object",
    )
    require(
        source,
        "g_materialInteractionProgram.programGeneration = -1;",
        "never-attempted generation sentinel",
    )

    # shadow.vp failing to bind silently left w=0 shadow volumes drawn through
    # fixed function, which is indistinguishable from "no shadows".
    reject(
        source,
        'VPROG_STENCIL_SHADOW, "stencil shadow vertex program", false',
        "stencil shadow program bind must report failures",
    )
    reject(
        source,
        'VPROG_STENCIL_SHADOW, "stencil shadow fallback vertex program", false',
        "stencil shadow fallback bind must report failures",
    )

    # r_appleARB2Interactions 2 exists to run the full ARB path; an archived
    # r_useSimpleInteraction 1 used to OR that away.
    simple_body = function_body(source, "static bool RB_UseSimpleInteractionShader( void ) {")
    for token in (
        "RB_AppleGL21CorridorActive()",
        "return RB_DriverPrefersSimpleInteraction();",
    ):
        require(simple_body, token, "Apple policy authority over r_useSimpleInteraction")

    # per-surface route counters, so a reporter log says how much of the frame
    # actually took the corridor
    for token in (
        "Apple GL 2.1 interaction routes: glsl=%d simpleARB=%d (customGLSL=%d gpuPosed=%d unprepared=%d)",
        "void RB_ResetAppleGL21RouteCounters( void ) {",
        "void RB_ReportAppleGL21RouteCounters( void ) {",
        "g_appleGL21RouteCounts.glslCorridor++;",
        "g_appleGL21RouteCounts.simpleFallback++;",
    ):
        require(source, token, "Apple GL 2.1 interaction route counters")
    require(
        read("src/renderer/draw_common.cpp"),
        "RB_ReportAppleGL21RouteCounters();",
        "route counter report call",
    )


def validate_framebuffer_and_profile_diagnostics() -> None:
    """Stencil, quality-tier and ambient facts a reporter log never carried."""

    context_source = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    for token in (
        "RENDER_GLATTR_DEPTH_SIZE",
        "RENDER_GLATTR_STENCIL_SIZE",
        "SDL3: reported OpenGL framebuffer attributes: depthBits=%s%d stencilBits=%s%d hasStencilBuffer=%d",
        "the OpenGL context has no stencil buffer; stencil shadow volumes cannot be drawn on this visual",
    ):
        require(context_source, token, "achieved framebuffer attribute reporting")

    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    for token in (
        "case RENDER_GLATTR_DEPTH_SIZE:",
        "case RENDER_GLATTR_STENCIL_SIZE:",
    ):
        require(backend, token, "SDL3 framebuffer attribute plumbing")

    common = read("src/framework/Common.cpp")
    require(
        common,
        "Quality profile: com_machineSpec=%d com_videoRam=%d com_performancePreset='%s' r_forceAmbient=%.3f",
        "unconditional quality profile line",
    )


def validate_multisample_gates_ask_the_context() -> None:
    """glewExperimental sets GLEW_VERSION_3_2 and GLEW_ARB_texture_multisample
    whenever their entry points resolve, and macOS resolves them for its 2.1
    legacy context. A limit query gated on them raised GL_INVALID_ENUM at every
    Apple startup and failed the renderer matrix on both macOS legs, and
    gfxInfo reported MSAA the context could not deliver."""

    graph_source = read("src/renderer/RenderGraphResources.cpp")
    graph_init = function_body(
        graph_source,
        "void R_RenderGraphResources_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {",
    )
    require(
        graph_init,
        '( caps.glVersion >= 3.2f || GLCapabilityProbe_HasExtension( "GL_ARB_texture_multisample" ) )',
        "render graph multisample limit gate",
    )

    image_source = read("src/renderer/OpenGL/gl_Image.cpp")
    alloc_image = function_body(image_source, "void idImage::AllocImage() {")
    require(alloc_image, "R_GLMultisampleTexturesAvailable()", "multisample texture allocation gate")
    texture_gate = function_body(image_source, "bool R_GLMultisampleTexturesAvailable( void ) {")
    for token in (
        "glTexImage2DMultisample != NULL",
        "glConfig.backendCaps.glVersion >= 3.2f",
        'GLCapabilityProbe_HasExtension( "GL_ARB_texture_multisample" )',
    ):
        require(texture_gate, token, "multisample texture capability")
    # The HDR scene target asks the same question before it requests samples:
    # on that context AllocImage dropped them, the image never equalled the
    # request, and ScratchImage reallocated it every frame (#257).
    scene_target = function_body(read("src/renderer/draw_common.cpp"), "static bool RB_EnsureSceneRenderTexture(")
    require(scene_target, "R_GLMultisampleTexturesAvailable()", "HDR scene target multisample gate")

    # gfxInfo printed "MSAA ... effective=8 reason=active" on that context while
    # the same log said every multisample texture had been refused.
    init_source = read("src/renderer/RenderSystem_init.cpp")
    gfxinfo_aa = function_body(init_source, "static void R_GfxInfoPrintAAState( void ) {")
    gfxinfo_max_samples = function_body(init_source, "static int R_GfxInfoGLMaxSamples( bool &available ) {")
    for token in (
        "const bool textureMSAAAvailable = glTexImage2DMultisample != NULL",
        "glConfig.backendCaps.glVersion >= 3.2f",
        'GLCapabilityProbe_HasExtension( "GL_ARB_texture_multisample" )',
    ):
        require(gfxinfo_aa, token, "gfxInfo texture MSAA gate")
    # GL_MAX_SAMPLES is a framebuffer-multisample limit; Apple's 2.1 context
    # has it through EXT_framebuffer_multisample and ARB_framebuffer_object.
    for token in (
        "glConfig.backendCaps.glVersion >= 3.0f",
        'GLCapabilityProbe_HasExtension( "GL_ARB_framebuffer_object" )',
        'GLCapabilityProbe_HasExtension( "GL_EXT_framebuffer_multisample" )',
    ):
        require(gfxinfo_max_samples, token, "gfxInfo GL_MAX_SAMPLES gate")

    for body, context in (
        (graph_init, "render graph multisample limit gate"),
        (alloc_image, "multisample texture allocation gate"),
        (texture_gate, "multisample texture capability"),
        (gfxinfo_aa, "gfxInfo texture MSAA gate"),
        (gfxinfo_max_samples, "gfxInfo GL_MAX_SAMPLES gate"),
    ):
        reject(body, "GLEW_", context)


def validate_post_aa_request_keeps_the_window_route() -> None:
    """Without multisample textures the window is the scene's only multisampled
    target on Apple's 2.1 context. SMAA needs GLSL 1.30, so a Post AA request
    the balanced, quality and ultra presets make sent the scene offscreen for
    nothing and cost it its MSAA. The offscreen route then raised
    GL_INVALID_FRAMEBUFFER_OPERATION every frame: the soft-particle depth copy
    blitted into a depth-only framebuffer whose read buffer still named a
    colour attachment, which GL before 4.1 without ARB_ES2_compatibility
    calls incomplete."""

    game_render = read("src/game/Game_render.cpp")
    render_scene = function_body(
        game_render,
        "void idGameLocal::RenderScene(const renderView_t *view, idRenderWorld *renderWorld, idCamera* portalSky, int renderFlags) {",
    )
    fast_no_post = source_section(render_scene, "const bool canUseFastNoPost =", ";")
    require(fast_no_post, "!useSMAA", "direct-route Post AA gate")
    reject(fast_no_post, "wantsSMAA", "direct-route Post AA gate")
    require(
        render_scene,
        "const bool useSMAA = PostAAModeUsesSMAA( postAASchedule.effectiveMode );",
        "direct-route Post AA gate",
    )

    image_source = read("src/renderer/Image_load.cpp")
    depth_copy = function_body(
        image_source,
        "bool idImage::CopyDepthbuffer( int x, int y, int imageWidth, int imageHeight,",
    )
    depth_fbo_setup = source_section(
        depth_copy,
        "if ( r_copyDepthbufferFbo == 0 ) {",
        "const GLuint copyDepthFbo = r_copyDepthbufferFbo;",
    )
    for token in (
        "glGenFramebuffers( 1, &r_copyDepthbufferFbo );",
        "glBindFramebuffer( GL_READ_FRAMEBUFFER, r_copyDepthbufferFbo );",
        "glReadBuffer( GL_NONE );",
    ):
        require(depth_fbo_setup, token, "depth-copy framebuffer setup")


def main() -> None:
    validate_context_aware_apple_gl21_quirk()
    validate_corridor_lighting_contract()
    validate_framebuffer_and_profile_diagnostics()
    validate_multisample_gates_ask_the_context()
    validate_post_aa_request_keeps_the_window_route()
    validate_simple_interaction_fail_closed()
    validate_arb_entrypoint_and_binding_audit()
    validate_upload_and_vertex_cache_static_coverage()
    validate_phase3_plan_status()
    validate_docs_and_wiring()
    print("macos_apple_gl21_arb2_corridor: ok")


if __name__ == "__main__":
    main()
