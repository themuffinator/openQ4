#!/usr/bin/env python3
"""Static checks for issue #73 renderer startup breadcrumbs."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = "docs/dev/plans/2026-06-30-apple-support-no-macos-access.md"


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


def require_ordered(haystack: str, tokens: tuple[str, ...], context: str) -> None:
    position = -1
    for token in tokens:
        next_position = haystack.find(token, position + 1)
        if next_position == -1:
            raise AssertionError(f"Missing ordered token {token!r} in {context}")
        position = next_position


def source_section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    if start == -1:
        raise AssertionError(f"Missing section marker {start_marker!r}")
    end = source.find(end_marker, start)
    if end == -1:
        raise AssertionError(f"Missing section end marker {end_marker!r}")
    return source[start:end]


def validate_diagnostics_module() -> None:
    header = read("src/renderer/RendererStartupDiagnostics.h")
    source = read("src/renderer/RendererStartupDiagnostics.cpp")

    for token in (
        "#include <signal.h>",
        "RENDERER_STARTUP_PHASE_R_INIT_OPENGL",
        "RENDERER_STARTUP_PHASE_R_CHECK_PORTABLE_EXTENSIONS",
        "RENDERER_STARTUP_PHASE_R_ARB2_INIT",
        "RENDERER_STARTUP_PHASE_R_RELOAD_ARB_PROGRAMS",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_FULL_UPLOAD",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SIMPLE_UPLOAD",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SKIP_FULL_UPLOAD",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_COLOR_MODE",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_FULL_SELECTION",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SIMPLE_SELECTION",
        "RENDERER_STARTUP_PHASE_R_RENDERER_UPLOAD_INIT",
        "RENDERER_STARTUP_PHASE_VERTEX_CACHE_INIT",
        "RENDERER_STARTUP_PHASE_SET_BACK_END_RENDERER",
        "RENDERER_STARTUP_PHASE_READY",
        "RENDERER_STARTUP_PHASE_FIRST_ARB2_INTERACTION_HANDOFF",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_DRIVER_BYPASS",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_STATE_RESTORED",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE_SKIPPED",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_AMBIENT_RESCUE",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_FRAME_TAIL",
        "void R_SetRendererStartupPhase",
        "void R_RecordRendererStartupPhase",
        "const char *R_RendererStartupPhaseSignalName",
    ):
        require(header, token, "renderer startup diagnostics header")

    for token in (
        "static volatile sig_atomic_t r_lastRendererStartupPhase",
        "R_NormalizeRendererStartupPhase",
        "renderer startup phase: %s",
        "R_RendererStartupPhaseSignalName",
        "first ARB2 interaction handoff",
        "R_ReloadARBPrograms_f: interaction color mode",
        "R_ReloadARBPrograms_f: skipped full interaction upload",
        "R_ReloadARBPrograms_f: selected simple interaction",
        "ARB2 interaction driver bypass",
        "ARB2 interaction bypass state restored",
        "ARB2 interaction bypass light scale",
        "ARB2 interaction bypass light scale skipped",
        "ARB2 interaction bypass ambient rescue",
        "ARB2 interaction bypass frame tail",
    ):
        require(source, token, "renderer startup diagnostics source")


def validate_posix_signal_bridge() -> None:
    source = read("src/sys/posix/posix_signal.cpp")
    fatal_body = function_body(source, "static void sig_handler( int signum, siginfo_t *info, void *context ) {")

    # module-only clients carry no renderer TUs; the breadcrumb resolves
    # through the Posix_RendererStartupPhaseName wrapper, which falls back to
    # a fixed marker there and to the real phase name on static builds
    for token in (
        '#include "../../renderer/RendererStartupDiagnostics.h"',
        "static const char *Posix_RendererStartupPhaseName( void ) {",
        "return R_RendererStartupPhaseSignalName();",
        'Posix_WriteSignalText( "openQ4: last renderer startup phase: " );',
        "Posix_WriteSignalText( Posix_RendererStartupPhaseName() );",
    ):
        require(source, token, "POSIX fatal signal renderer startup phase bridge")

    require_ordered(
        fatal_body,
        (
            'Posix_WriteSignalText( "openQ4: fatal signal " );',
            'Posix_WriteSignalText( "), exiting without unsafe engine shutdown\\n" );',
            'Posix_WriteSignalText( "openQ4: last renderer startup phase: " );',
            "Posix_RendererStartupPhaseName()",
            'Posix_WriteSignalText( "openQ4: last game module phase: " );',
            "Com_GameModuleLoadPhaseSignalName()",
            "_exit( 128 + signum );",
        ),
        "POSIX fatal signal renderer startup phase output order",
    )
    require(
        source,
        '#include "../../framework/GameModuleDiagnostics.h"',
        "POSIX fatal signal game module phase bridge",
    )
    require(
        source,
        'strcat( breadcrumb, "; last game module phase: " );',
        "fatal breadcrumb file game module phase",
    )


def validate_game_module_phase_breadcrumbs() -> None:
    """Issue #90: a SIGABRT between 'Selected game module:' and 'Initializing
    Game' named no step at all, so nothing distinguished a bad static
    initializer from GetGameAPI from idGameLocal::Init."""

    header = read("src/framework/GameModuleDiagnostics.h")
    for token in (
        "GAME_MODULE_PHASE_IDLE = 0,",
        "GAME_MODULE_PHASE_LOCATE,",
        "GAME_MODULE_PHASE_BINARY_LOAD,",
        "GAME_MODULE_PHASE_RESOLVE_ENTRY_POINT,",
        "GAME_MODULE_PHASE_CALL_GET_GAME_API,",
        "GAME_MODULE_PHASE_VERIFY_API_VERSION,",
        "GAME_MODULE_PHASE_GAME_INIT,",
        "GAME_MODULE_PHASE_READY,",
        "const char *Com_GameModuleLoadPhaseSignalName( void );",
    ):
        require(header, token, "game module diagnostics header")

    common = read("src/framework/Common.cpp")
    load_body = function_body(common, "void idCommonLocal::LoadGameDLL( void ) {")
    require_ordered(
        load_body,
        (
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_LOCATE );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_BINARY_LOAD );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_RESOLVE_ENTRY_POINT );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_CALL_GET_GAME_API );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_VERIFY_API_VERSION );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_GAME_INIT );",
            "Com_SetGameModuleLoadPhase( GAME_MODULE_PHASE_READY );",
        ),
        "game module load phase order",
    )
    # GetGameAPI is dereferenced immediately; a NULL return has to be caught.
    require(load_body, "const gameExport_t *gameExportPtr = GetGameAPI( &gameImport );", "GetGameAPI NULL guard")
    require(load_body, "if ( gameExportPtr == NULL ) {", "GetGameAPI NULL guard")


def validate_renderer_startup_order(init_source: str | None = None) -> None:
    if init_source is None:
        init_source = read("src/renderer/RenderSystem_init.cpp")
    upload_source = read("src/renderer/RendererUpload.cpp")
    common_source = read("src/renderer/draw_common.cpp")
    draw_source = read("src/renderer/draw_arb2.cpp")
    render_source = read("src/renderer/tr_render.cpp")

    wrapper_body = function_body(init_source, "void R_InitOpenGL( void ) {")
    init_body = function_body(init_source, "static bool R_InitOpenGLInternal(")
    context_body = function_body(init_source, "static bool R_CreateOpenGLContext(")
    portable_body = function_body(init_source, "static bool R_CheckPortableExtensions(")
    require(wrapper_body, "R_InitOpenGLInternal( true, false, error, sizeof( error ) )", "legacy startup shared route")
    # Expand the actual shared context call in place; concatenating detached
    # helpers would conceal an incorrectly moved call.
    context_call = "if ( !R_CreateOpenGLContext( legacyPolicy, forceWindow, error, errorSize ) ) return false;"
    if init_body.count(context_call) != 1:
        raise AssertionError("Shared startup must invoke context creation exactly once")
    init_body = init_body.replace(context_call, context_body, 1)
    upload_body = function_body(upload_source, "void R_RendererUpload_Init( const renderBackendCaps_t &caps ) {")
    arb_init_body = function_body(draw_source, "void R_ARB2_Init( void ) {")
    reload_body = function_body(draw_source, "void R_ReloadARBPrograms_f( const idCmdArgs &args ) {")
    load_body = function_body(draw_source, "void R_LoadARBProgram( int progIndex ) {")
    interaction_body = source_section(
        draw_source,
        "void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {",
        "void RB_ARB2_DrawInteractions( void ) {",
    )
    draw_interactions_body = function_body(draw_source, "void RB_ARB2_DrawInteractions( void ) {")
    interaction_restore_body = function_body(draw_source, "static void RB_ARB2_RestoreInteractionState( const bool recordBypassBreadcrumb ) {")
    bypass_restore_body = function_body(draw_source, "static void RB_ARB2_RestoreBypassedInteractionState( void ) {")
    disable_attrib_body = function_body(draw_source, "static void RB_ARB2_DisableInteractionVertexAttribArrays( void ) {")
    draw_view_body = function_body(common_source, "void\tRB_STD_DrawView( void ) {")
    bypass_active_body = function_body(common_source, "static bool RB_ARB2InteractionBypassActive( void ) {")
    bypass_frame_body = function_body(common_source, "static void RB_RecordARB2InteractionBypassFramePhase( rendererStartupPhase_t phase ) {")
    reset_body = function_body(draw_source, "void RB_ResetARB2InteractionHandoffBreadcrumb( void ) {")
    determine_light_scale_body = function_body(render_source, "void RB_DetermineLightScale( void ) {")

    require_ordered(
        init_body,
        (
            "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_INIT_OPENGL );",
            "RB_ResetARB2InteractionHandoffBreadcrumb();",
            "R_CheckPortableExtensions( legacyPolicy, error, errorSize )",
            "R_ARB2_Init();",
            "R_ReloadARBPrograms_f( idCmdArgs() );",
            "R_RendererUpload_Init( glConfig.backendCaps );",
            "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_VERTEX_CACHE_INIT );",
            "vertexCache.Init();",
            "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_SET_BACK_END_RENDERER );",
            "tr.SetBackEndRenderer();",
            "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_READY );",
        ),
        "R_InitOpenGL renderer startup phase order",
    )

    require(portable_body, "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_CHECK_PORTABLE_EXTENSIONS );", "portable extension phase")
    require(arb_init_body, "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_ARB2_INIT );", "ARB2 init phase")
    require(reload_body, "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_RELOAD_ARB_PROGRAMS );", "ARB reload phase")
    require(reload_body, "RB_RecordCurrentInteractionSelectionBreadcrumb();", "ARB interaction selection breadcrumb")
    require(upload_body, "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_RENDERER_UPLOAD_INIT );", "renderer upload init phase")
    require(reset_body, "g_firstARB2InteractionHandoffBreadcrumb = false;", "ARB2 handoff reset")

    for token in (
        "RB_IsFullInteractionProgram( prog )",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_FULL_UPLOAD",
        "RB_IsSimpleInteractionProgram( prog )",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SIMPLE_UPLOAD",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SKIP_FULL_UPLOAD",
        "R_SetRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_COLOR_MODE );",
    ):
        require(load_body, token, "ARB program load interaction breadcrumbs")

    for token in (
        "g_firstARB2InteractionHandoffBreadcrumb",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_FIRST_ARB2_INTERACTION_HANDOFF );",
    ):
        require(interaction_body, token, "first ARB2 interaction handoff breadcrumb")

    for token in (
        "glConfig.disableARB2Interactions",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_DRIVER_BYPASS );",
        "RB_ARB2_RestoreBypassedInteractionState();",
    ):
        require(draw_interactions_body, token, "ARB2 interaction driver bypass breadcrumb")

    for token in (
        "RB_ARB2_ClearInteractionTextureState();",
        "glDisableClientState( GL_COLOR_ARRAY );",
        "glDisableClientState( GL_NORMAL_ARRAY );",
        "RB_ARB2_DisableInteractionVertexAttribArrays();",
        "glDisable( GL_VERTEX_PROGRAM_ARB );",
        "glDisable( GL_FRAGMENT_PROGRAM_ARB );",
        "RB_ARB2_UnbindInteractionPrograms();",
        "glStencilFunc( GL_ALWAYS, 128, 255 );",
        "GL_ClearStateDelta();",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_STATE_RESTORED",
    ):
        require(interaction_restore_body, token, "ARB2 interaction state restore")

    require(bypass_restore_body, "RB_ARB2_RestoreInteractionState( true );", "ARB2 interaction bypass state restore wrapper")
    require(draw_interactions_body, "RB_ARB2_RestoreInteractionState( false );", "Apple interaction draw state restore")

    for token in (
        "glDisableVertexAttribArrayARB == NULL",
        "static const int attribs[] = { 1, 2, 5, 6, 7, 8, 9, 10, 11 };",
        "glDisableVertexAttribArrayARB( attribs[i] );",
    ):
        require(disable_attrib_body, token, "ARB2 bypass interaction attribute cleanup")

    texture_cleanup_body = function_body(draw_source, "static void RB_ARB2_ClearInteractionTextureState( void ) {")
    for token in (
        "maxStateUnits",
        "maxInteractionUnit",
        "GL_SelectTextureNoClient can leave the client active texture behind.",
        "GL_SelectTexture( unit );",
        "globalImages->BindNull();",
        "glDisable( GL_TEXTURE_GEN_S );",
        "glDisable( GL_TEXTURE_GEN_T );",
        "glDisable( GL_TEXTURE_GEN_R );",
        "glDisable( GL_TEXTURE_GEN_Q );",
        "GL_TexEnv( GL_MODULATE );",
        "glDisableClientState( GL_TEXTURE_COORD_ARRAY );",
        "backEnd.glState.currenttmu = -1;",
        "GL_SelectTexture( 0 );",
        "glEnableClientState( GL_TEXTURE_COORD_ARRAY );",
    ):
        require(texture_cleanup_body, token, "ARB2 bypass interaction texture cleanup")
    if texture_cleanup_body.count("backEnd.glState.currenttmu = -1;") < 2:
        raise AssertionError("ARB2 bypass interaction texture cleanup must force texture selection before unit cleanup and unit 0 restore")

    for token in (
        "tr.backEndRenderer == BE_ARB2",
        "glConfig.disableARB2Interactions",
    ):
        require(bypass_active_body, token, "ARB2 interaction bypass active helper")

    for token in (
        "tr.backEndRenderer == BE_ARB2 && glConfig.disableARB2Interactions",
        "backEnd.pc.maxLightValue = 1.0f;",
        "backEnd.lightScale = 1.0f;",
        "backEnd.overBright = 1.0f;",
        "return;",
    ):
        require(determine_light_scale_body, token, "ARB2 bypass neutral light-scale state")

    for token in (
        "RB_ARB2InteractionBypassActive()",
        "rbARB2InteractionBypassFrameBreadcrumbsComplete",
        "R_RecordRendererStartupPhase( phase );",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_FRAME_TAIL",
    ):
        require(bypass_frame_body, token, "ARB2 bypass frame-tail breadcrumb helper")

    for token in (
        "if ( RB_ARB2InteractionBypassActive() ) {",
        "RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE_SKIPPED );",
        "} else {",
        "RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE );",
        "glStencilFunc( GL_ALWAYS, 128, 255 );",
        "RB_STD_LightScale();",
        "RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_AMBIENT_RESCUE );",
        "RB_STD_ForceAmbient();",
        "RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_FRAME_TAIL );",
    ):
        require(draw_view_body, token, "ARB2 bypass draw-view frame-tail breadcrumbs")

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


def validate_startup_mutation_sensitivity() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")
    for anchor in (
        "if ( !R_CreateOpenGLContext( legacyPolicy, forceWindow, error, errorSize ) ) return false;",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_INIT_OPENGL );",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_CHECK_PORTABLE_EXTENSIONS );",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_READY );",
    ):
        if source.count(anchor) != 1:
            raise AssertionError(f"Startup mutation anchor is not unique: {anchor!r}")
        try:
            validate_renderer_startup_order(source.replace(anchor, "", 1))
        except AssertionError:
            continue
        raise AssertionError(f"Startup regression escaped detection: {anchor!r}")
    early_ready = "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_READY );"
    misplaced = source.replace(early_ready, "", 1).replace(
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_INIT_OPENGL );",
        "R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_INIT_OPENGL );\n" + early_ready, 1)
    try:
        validate_renderer_startup_order(misplaced)
    except AssertionError:
        return
    raise AssertionError("Premature renderer-ready phase escaped detection")


def validate_phase2_plan_status() -> None:
    plan = read(PLAN_PATH)

    for token in (
        "## Phase 2: Add Crash-Useful Renderer Breadcrumbs",
        '- [x] Add a tiny crash-safe "last renderer phase" string or enum updated before',
        "- [x] Teach the POSIX signal handler to print the last renderer phase using only",
        "- [x] Print the same phase state in normal logs before and after renderer",
        "- [x] Add static validation that every renderer startup phase marker is present",
        "- [x] Add a specific issue #73 breadcrumb around interaction program selection:",
        "- [x] Add a specific issue #73 breadcrumb when Apple GL 2.1 bypasses",
        "Phase 2 implementation status",
        "src/renderer/RendererStartupDiagnostics.h",
        "src/renderer/RendererStartupDiagnostics.cpp",
        "src/sys/posix/posix_signal.cpp",
        "tools/tests/macos_renderer_phase_breadcrumbs.py",
    ):
        require(plan, token, "Phase 2 Apple support plan")


def validate_docs_and_release_notes() -> None:
    support_doc = read("docs/user/macos-support-data.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")
    release_completion = read("docs/dev/release-completion.md")

    for source, context in (
        (support_doc, "macOS support-data guide"),
        (release_notes, "curated release notes"),
        (release_completion, "release completion notes"),
    ):
        require(source, "last renderer startup phase", context)
        require(source, "first ARB2 interaction handoff", context)
        require(source, "ARB2 interaction driver bypass", context)
        require(source, "ARB2 interaction bypass state", context)
        require(source, "ARB2 interaction bypass light scale", context)
        require(source, "ARB2 interaction bypass light scale skipped", context)
        require(source, "ARB2 interaction bypass ambient", context)
        require(source, "ARB2 interaction bypass frame tail", context)


def validate_ci_and_local_wiring() -> None:
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_renderer_phase_breadcrumbs.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_renderer_phase_breadcrumbs.py", context)


def main() -> None:
    validate_diagnostics_module()
    validate_posix_signal_bridge()
    validate_game_module_phase_breadcrumbs()
    validate_renderer_startup_order()
    validate_startup_mutation_sensitivity()
    validate_phase2_plan_status()
    validate_docs_and_release_notes()
    validate_ci_and_local_wiring()
    print("macos_renderer_phase_breadcrumbs: ok")


if __name__ == "__main__":
    main()
