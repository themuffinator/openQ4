#!/usr/bin/env python3
"""Guards how the engine picks a game module from si_gameType.

The engine has to choose between game_sp and game_mp before any game module is
loaded, so it cannot ask the game for its own gametype table. It keeps a mirror
of mpGameTypeInfoTable names and selectability instead. This test pins that
mirror against the GameLibs table, and pins the shipped default.cfg value that
decides which module a plain client boots (issue #73: booting game_mp meant
every New Game tore the renderer down for a module swap).
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def expect_contract_rejection(validator, candidate: str, context: str) -> None:
    try:
        validator(candidate)
    except AssertionError:
        return
    raise AssertionError(f"Validation contract accepted mutation: {context}")


def validate_shutdown_lifecycle_contract(common: str) -> None:
    unload = function_body(common, "void idCommonLocal::UnloadGameDLL( void )", "game DLL unload")
    shutdown = function_body(common, "void idCommonLocal::ShutdownGame( bool reloading )", "game shutdown")
    load = function_body(common, "void idCommonLocal::LoadGameDLL( void )", "game DLL load")

    reject(unload, "game->Shutdown();", "binary-only game module unload")
    reject(unload, "game->ShutdownAfterDecls();", "binary-only game module unload")
    if common.count("game->Shutdown();") != 1:
        raise AssertionError("game module must receive exactly one engine-driven early shutdown")
    if common.count("game->ShutdownAfterDecls();") != 1:
        raise AssertionError("game module must receive exactly one engine-driven late finalization")

    for token in (
        "gameShutdownCalled = false;",
        "gameShutdownAfterDeclsCalled = false;",
    ):
        require(load, token, "game module lifecycle reset")

    false_store = shutdown.index(
        "openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );"
    )
    session_shutdown = shutdown.index("session->Shutdown();")
    early_shutdown = shutdown.index("game->Shutdown();")
    decl_shutdown = shutdown.index("declManager->Shutdown();")
    late_shutdown = shutdown.index("game->ShutdownAfterDecls();")
    unload_dll = shutdown.index("UnloadGameDLL();")
    if false_store > early_shutdown:
        raise AssertionError("single-player module state is cleared after game shutdown begins")
    if session_shutdown > early_shutdown:
        raise AssertionError("game shutdown runs before session map teardown")
    for service_shutdown in (
        "uiManager->Shutdown();",
        "soundSystem->Shutdown();",
        "idAsyncNetwork::Shutdown();",
        "usercmdGen->Shutdown();",
        "eventLoop->Shutdown();",
        "renderSystem->Shutdown();",
    ):
        service = shutdown.index(service_shutdown)
        if early_shutdown > service:
            raise AssertionError(f"game shutdown runs after {service_shutdown}")
        if service > decl_shutdown:
            raise AssertionError(f"declaration shutdown runs before {service_shutdown}")
    if not decl_shutdown < late_shutdown < unload_dll:
        raise AssertionError("late game finalization must run after declarations and before DLL unload")

    early_guard = function_body(
        shutdown,
        "if ( game != NULL && !gameShutdownCalled )",
        "one-shot early game shutdown",
    )
    require(early_guard, "gameShutdownCalled = true;", "one-shot early game shutdown")
    require(early_guard, "game->Shutdown();", "one-shot early game shutdown")
    if early_guard.index("gameShutdownCalled = true;") > early_guard.index("game->Shutdown();"):
        raise AssertionError("early game shutdown publishes its one-shot state after crossing into module code")

    late_guard = function_body(
        shutdown,
        "if ( game != NULL && !gameShutdownAfterDeclsCalled )",
        "one-shot late game finalization",
    )
    require(late_guard, "gameShutdownAfterDeclsCalled = true;", "one-shot late game finalization")
    require(late_guard, "game->ShutdownAfterDecls();", "one-shot late game finalization")
    if late_guard.index("gameShutdownAfterDeclsCalled = true;") > late_guard.index("game->ShutdownAfterDecls();"):
        raise AssertionError("late game finalization publishes its one-shot state after crossing into module code")


def validate_font_resource_reset_contract(font_source: str, font_header: str) -> None:
    register = function_body(
        font_source,
        "bool idRenderSystemLocal::RegisterFont",
        "renderer font registration",
    )
    done = function_body(font_source, "void R_DoneFreeType( void )", "renderer font shutdown")
    refresh = function_body(
        font_source,
        "void R_RefreshConsoleFontAtlas( void )",
        "console font refresh",
    )

    require(
        font_source[: font_source.index("bool idRenderSystemLocal::RegisterFont")],
        "static bool consoleFontChecked = false;",
        "renderer-lifetime console font guard",
    )
    if font_source.count("static bool consoleFontChecked = false;") != 1:
        raise AssertionError("console font lifecycle guard must have one file-scope definition")
    reject(register, "static bool consoleFontChecked", "function-static console font guard")
    # The console sheet is byte-indexed, so besides the once-per-renderer guard it
    # also has to rebuild when sys_lang moves the string tables between the
    # Western and Central European codepages.
    for token in (
        "if ( !consoleFontChecked || consoleFontCodePageGeneration != codePageGeneration )",
        "consoleFontChecked = true;",
        "R_BuildConsoleFontAtlas();",
    ):
        require(register, token, "once-per-renderer console font rebuild")
    require(register, "LangDict_GetCodePageGeneration()", "codepage-aware console font rebuild")
    for token in ("consoleFontChecked = true;", "R_BuildConsoleFontAtlas();"):
        require(refresh, token, "console font refresh")
    if not (
        register.index("if ( !consoleFontChecked ||")
        < register.index("consoleFontChecked = true;")
        < register.index("R_BuildConsoleFontAtlas();")
        and refresh.index("consoleFontChecked = true;") < refresh.index("R_BuildConsoleFontAtlas();")
    ):
        raise AssertionError("RegisterFont must arm and rebuild the console atlas exactly once per font lifecycle")

    require(done, "R_ShutdownTrueTypeFonts();", "TrueType renderer shutdown")
    require(done, "consoleFontChecked = false;", "console font lifecycle reset")
    if done.index("R_ShutdownTrueTypeFonts();") > done.index("consoleFontChecked = false;"):
        raise AssertionError("console atlas guard resets before cached TrueType fonts shut down")
    require(font_header, "void R_RefreshConsoleFontAtlas( void );", "console font refresh declaration")


def validate_full_vid_restart_font_contract(renderer_source: str) -> None:
    restart = function_body(
        renderer_source,
        "static void R_PerformFullVidRestart( bool forceWindow )",
        "full video restart",
    )
    teardown = function_body(renderer_source, "static void R_ShutdownDeviceForRestart( void )", "restart teardown")
    require(restart, "R_ShutdownDeviceForRestart();", "shared restart teardown")
    restart = restart.replace("R_ShutdownDeviceForRestart();", teardown)
    for token in (
        "R_DoneFreeType();",
        "globalImages->PurgeAllImages();",
        "tr.InitOpenGL();",
        "R_InitFreeType();",
        "R_RefreshConsoleFontAtlas();",
    ):
        require(restart, token, "full video restart font lifecycle")
    if not (
        restart.index("R_DoneFreeType();")
        < restart.index("globalImages->PurgeAllImages();")
        < restart.index("tr.InitOpenGL();")
        < restart.index("R_InitFreeType();")
        < restart.index("R_RefreshConsoleFontAtlas();")
    ):
        raise AssertionError("Full vid_restart must release fonts before purge and rebuild them after image reload")
    reject(restart, "R_InitOpenGL();", "backend-aware device restart")
    reject(restart, "globalImages->ReloadImages", "single image reload owned by device startup")

    renderer_shutdown = function_body(
        renderer_source,
        "void idRenderSystemLocal::Shutdown( void )",
        "full renderer shutdown",
    )
    require(renderer_shutdown, "R_DoneFreeType( );", "full renderer font shutdown")
    if renderer_shutdown.index("R_DoneFreeType( );") > renderer_shutdown.index("globalImages->PurgeAllImages();"):
        raise AssertionError("Full renderer shutdown purges images before releasing cached TrueType fonts")

    vid_restart = function_body(renderer_source, "void R_VidRestart_f", "video restart command")
    full_branch = function_body(vid_restart, "if ( full )", "full video restart selection")
    require(full_branch, "R_PerformFullVidRestart( forceWindow );", "requested full video restart")
    reject(full_branch, "GLimp_SetScreenParms", "requested full video restart")
    if vid_restart.count("R_PerformFullVidRestart( forceWindow );") != 2:
        raise AssertionError("Full restart must cover the requested path and failed partial fallback")
    reject(vid_restart, "R_DoneFreeType", "successful partial video restart font preservation")

    partial_failure = function_body(
        vid_restart,
        "if ( !GLimp_SetScreenParms( parms ) )",
        "failed partial video restart",
    )
    require(partial_failure, "R_PerformFullVidRestart( forceWindow );", "failed partial full-restart fallback")
    failure_end = vid_restart.index(partial_failure) + len(partial_failure)
    partial_tail = vid_restart[failure_end:].lstrip()
    if not partial_tail.startswith("else {"):
        raise AssertionError("Successful partial video restart must be the failed-mode-change else path")
    partial_success = function_body(partial_tail, "else", "successful partial video restart")
    require(partial_success, "R_RefreshConsoleFontAtlas();", "partial console atlas refresh")
    for token in (
        "R_PerformFullVidRestart",
        "R_DoneFreeType",
        "R_InitFreeType",
        "globalImages->PurgeAllImages",
        "globalImages->ReloadImages",
    ):
        reject(partial_success, token, "partial font face/image preservation")

    generation = "if ( tr.videoRestartCount < 0x7fffffff )"
    require(vid_restart, generation, "video-restart font generation")
    require(vid_restart, "tr.videoRestartCount++;", "video-restart font generation")
    if vid_restart.index(generation) < failure_end + partial_tail.index(partial_success) + len(partial_success):
        raise AssertionError("Renderer restart generation advances before the full/partial font work finishes")


def validate_ui_font_reload_contract(
    device_source: str,
    device_header: str,
    public_header: str,
) -> None:
    reload_fonts = function_body(device_source, "bool idDeviceContext::ReloadFonts()", "UI font reload")
    for token in (
        "if ( fonts.Num() == 0 )",
        "int activeFontIndex = 0;",
        "int useFontSlot = -1;",
        "if ( activeFont != &fonts[i] )",
        "fontLang = cvarSystem->GetCVarString( \"sys_lang\" );",
        "openQ4_NormalizeFontLanguage( fontLang );",
        "for ( int i = 0; i < fonts.Num(); ++i )",
        "idStr::Copynz( logicalName, fonts[i].name, sizeof( logicalName ) );",
        # registration goes through the helper so a language without its own font
        # artwork falls back to English instead of failing every font
        "openQ4_RegisterFontForLanguage( logicalName, fontLang, replacement, fileName )",
        "fontInfoEx_t replacement;",
        "idStr::Copynz( replacement.name, logicalName, sizeof( replacement.name ) );",
        "fonts[i] = replacement;",
        "SetFont( activeFontIndex );",
        "useFont = &activeFont->fontInfoSmall;",
        "useFont = &activeFont->fontInfoMedium;",
        "useFont = &activeFont->fontInfoLarge;",
        "return allFontsReloaded;",
    ):
        require(reload_fonts, token, "UI font reload")
    reject(reload_fonts, "fonts.Append", "stable UI font indices across vid_restart")
    if not (
        reload_fonts.index("idStr::Copynz( logicalName, fonts[i].name")
        < reload_fonts.index("openQ4_RegisterFontForLanguage( logicalName, fontLang, replacement, fileName )")
        < reload_fonts.index("fonts[i] = replacement;")
        < reload_fonts.index("SetFont( activeFontIndex );")
    ):
        raise AssertionError("UI font reload must replace cached entries in place before restoring selection")

    register_for_language = function_body(
        device_source,
        "static bool openQ4_RegisterFontForLanguage(",
        "language font fallback",
    )
    for token in (
        "openQ4_ResolveFontFileName( logicalName, language, resolvedFileName );",
        "renderSystem->RegisterFont( resolvedFileName, font )",
        "idStr::Icmp( language.c_str(), \"english\" ) == 0",
        "openQ4_ResolveFontFileName( logicalName, idStr( \"english\" ), englishFileName );",
    ):
        require(register_for_language, token, "language font fallback")
    if not (
        register_for_language.index("renderSystem->RegisterFont( resolvedFileName, font )")
        < register_for_language.index("openQ4_ResolveFontFileName( logicalName, idStr( \"english\" ), englishFileName );")
    ):
        raise AssertionError(
            "the language-specific font must be tried before the English fallback, so a "
            "localised release keeps its own atlases"
        )

    ensure = function_body(device_source, "void idDeviceContext::EnsureFontsCurrent()", "lazy UI font refresh")
    for token in (
        "!initialized || renderSystem == NULL || !renderSystem->IsOpenGLRunning()",
        "const int currentRestartCount = renderSystem->GetVideoRestartCount();",
        # a sys_lang change that swaps the 8-bit codepage invalidates every glyph
        # atlas even though the renderer never restarted
        "const int currentCodePageGeneration = LangDict_GetCodePageGeneration();",
        "if ( fontsVideoRestartCount == currentRestartCount &&",
        "fontsCodePageGeneration == currentCodePageGeneration )",
        "fontsVideoRestartCount = currentRestartCount;",
        "fontsCodePageGeneration = currentCodePageGeneration;",
        "if ( !ReloadFonts() )",
    ):
        require(ensure, token, "lazy UI font refresh")
    if not (
        ensure.index("if ( fontsVideoRestartCount == currentRestartCount &&")
        < ensure.index("fontsVideoRestartCount = currentRestartCount;")
        < ensure.index("if ( !ReloadFonts() )")
    ):
        raise AssertionError("UI font refresh must record the generation before its recursive registration paths")

    for signature, context in (
        ("int idDeviceContext::FindFont", "font lookup refresh"),
        ("void idDeviceContext::SetFont( int num )", "font selection refresh"),
        ("void idDeviceContext::SetFontByScale", "font scale refresh"),
    ):
        caller = function_body(device_source, signature, context)
        require(caller, "EnsureFontsCurrent();", context)

    require(device_source, "int idDeviceContext::fontsVideoRestartCount = -1;", "UI font restart generation")
    init = function_body(device_source, "void idDeviceContext::Init()", "UI device-context initialization")
    require(init, "fontsVideoRestartCount = renderSystem->GetVideoRestartCount();", "initial UI font generation")
    shutdown = function_body(device_source, "void idDeviceContext::Shutdown()", "UI device-context shutdown")
    require(shutdown, "fontsVideoRestartCount = -1;", "UI font generation reset")

    # activeFont and useFont point into the fonts list, which reallocates on
    # every append because its granularity is 1. Registering a face a later map
    # is the first to name would otherwise leave the current selection reading a
    # freed allocation, which is a font that lays text out from garbage metrics.
    find_font = function_body(device_source, "int idDeviceContext::FindFont", "device-context font lookup")
    require(find_font, "fonts.Append( fontInfo )", "device-context font lookup")
    for token in (
        "activeFont = ( selectedFont >= 0 ) ? &fonts[selectedFont] : NULL;",
        "useFont = ( selectedSize == 0 ) ? &sized.fontInfoSmall",
    ):
        require(find_font, token, "font selection reseat across list growth")
    if find_font.index("fonts.Append( fontInfo )") > find_font.index("activeFont = ( selectedFont >= 0 )"):
        raise AssertionError("font selection must be reseated after the fonts list grows, not before")

    require(device_header, "bool\t\t\t\tReloadFonts();", "device-context font reload declaration")
    require(device_header, "void\t\t\t\tEnsureFontsCurrent();", "lazy UI font refresh declaration")
    require(device_header, "static int\t\t\tfontsVideoRestartCount;", "UI font restart generation declaration")
    reject(public_header, "ReloadFonts", "cross-module UI interface font reload")


def validate_ttf_persistent_atlas_contract(ttf_source: str) -> None:
    # The GUI upload lives in R_TTFPackAtlas rather than R_TTFBuildSlot: the base
    # Latin-1 slots and the extended code-point pages are two atlases now, and
    # both go through the one packer so they cannot diverge.
    slot = function_body(ttf_source, "static bool R_TTFPackAtlas", "GUI TrueType atlas")
    console = function_body(ttf_source, "bool R_BuildConsoleFontAtlas", "console TrueType atlas")
    for body, context in ((slot, "GUI TrueType atlas"), (console, "console TrueType atlas")):
        require(body, "opts.isPersistant = true;", context)
        require(body, "globalImages->ScratchImage(", context)
        if body.index("opts.isPersistant = true;") > body.index("globalImages->ScratchImage("):
            raise AssertionError(f"{context} marks persistence after allocating the scratch image")

    for token in (
        "static idMaterial *ttfConsoleMaterial = NULL;",
        "static idImage *ttfConsoleOriginalImage = NULL;",
    ):
        require(ttf_source, token, "authored console image preservation")
    shutdown = function_body(ttf_source, "void R_ShutdownTrueTypeFonts( void )", "TrueType font shutdown")
    for token in (
        "ttfConsoleMaterial->OverrideStageImageForRuntime( 0, ttfConsoleOriginalImage )",
        "ttfConsoleMaterial = NULL;",
        "ttfConsoleOriginalImage = NULL;",
        "ttfFonts.Shutdown();",
    ):
        require(shutdown, token, "authored console image restoration")
    if not (
        shutdown.index("OverrideStageImageForRuntime")
        < shutdown.index("ttfConsoleMaterial = NULL;")
        < shutdown.index("ttfConsoleOriginalImage = NULL;")
        < shutdown.index("ttfFonts.Shutdown();")
    ):
        raise AssertionError("TrueType shutdown must restore the authored console image before releasing state")

    # The generated atlas materials have no file behind them. If a declaration
    # purge is ever allowed to reset one, the implicit text the manager
    # regenerates maps an image that does not exist, so every glyph samples the
    # transparent default and GUI text silently disappears for the rest of the
    # session. Two things keep that from happening: the declaration carries the
    # generated source itself, and the level-load purge is followed by a restore.
    install = function_body(
        ttf_source, "static void R_TTFInstallAtlasStage", "TrueType atlas stage install"
    )
    for token in ("SetText(", "FreeData();", "Parse(", "SetSort( SS_GUI );"):
        require(install, token, "TrueType atlas stage install")
    if install.index("SetText(") > install.index("Parse("):
        raise AssertionError("TrueType atlas material must carry its source text before it is parsed")

    create = function_body(
        ttf_source, "static const idMaterial *R_TTFCreateAtlasMaterial", "TrueType atlas material creation"
    )
    require(create, "R_TTFInstallAtlasStage(", "TrueType atlas material creation")
    require(create, "ttfAtlasMaterials.Append(", "TrueType atlas material registry")
    reject(
        create,
        "if ( !R_TTFMaterialSamplesAtlas(",
        "unconditional TrueType atlas material source install",
    )

    # FindMaterial parses a newly created implicit material before this module
    # can replace its source. Its default stage maps an image named after the
    # material, so the generated material must share the already-uploaded
    # intrinsic atlas identity. A separate openq4/ttffont name probes a file
    # which can never exist and emits one warning/default image per atlas.
    for signature, context in (
        ("static void R_TTFBuildExtendedPages", "extended TrueType atlas identity"),
        ("static bool R_TTFBuildSlot", "base TrueType atlas identity"),
    ):
        atlas = function_body(ttf_source, signature, context)
        require(atlas, "const idStr materialName = imageName;", context)
        reject(atlas, "openq4/ttffont", context)

    restore = function_body(
        ttf_source, "void R_TTFRestoreAtlasMaterials( void )", "TrueType atlas material restore"
    )
    require(restore, "FindDeclWithoutParsing(", "TrueType atlas material restore")
    require(restore, "R_TTFInstallAtlasStage(", "TrueType atlas material restore")
    require(
        function_body(ttf_source, "void R_ShutdownTrueTypeFonts( void )", "TrueType font shutdown"),
        "ttfAtlasMaterials.DeleteContents( true );",
        "TrueType atlas material registry release",
    )


def validate_ttf_level_load_restore_contract() -> None:
    render_init = read(ROOT / "src" / "renderer" / "RenderSystem_init.cpp")
    begin = function_body(
        render_init, "void idRenderSystemLocal::BeginLevelLoad( void )", "renderer level-load begin"
    )
    require(begin, "R_TTFRestoreAtlasMaterials();", "renderer level-load begin")
    if begin.index("R_TTFRestoreAtlasMaterials();") > begin.index("globalImages->BeginLevelLoad();"):
        raise AssertionError(
            "purged glyph atlas materials must be restored before the level-load image purge runs"
        )
    require(
        read(ROOT / "src" / "renderer" / "tr_local.h"),
        "void R_TTFRestoreAtlasMaterials( void );",
        "TrueType atlas restore declaration",
    )


def validate_font_restart_documentation() -> None:
    documentation = read(ROOT / "docs" / "dev" / "ttf-font-system.md")
    for token in (
        "A full `vid_restart`\nreleases cached TrueType faces before image purge",
        "A successful partial restart keeps the context,\nfaces, and images alive",
        "Cached GUI fonts notice the renderer restart generation on their next\nlookup or selection and re-register in place",
        "preserving the integer indices\nstored by parsed GUIs as well as the active font and size selection",
    ):
        require(documentation, token, "TrueType renderer-restart documentation")


def validate_game_module_two_phase_lifecycle(source: str, header: str, label: str) -> None:
    for token in (
        "virtual void\t\t\tShutdown( void );",
        "virtual void\t\t\tShutdownAfterDecls( void );",
        "bool\t\t\t\t\tmoduleIdLibInitialized;",
        "bool\t\t\t\t\tmoduleShutdownStarted;",
        "bool\t\t\t\t\tmoduleShutdownFinalized;",
        "bool\t\t\t\t\tmoduleEventInitStarted;",
        "bool\t\t\t\t\tmoduleClassInitStarted;",
        "bool\t\t\t\t\tmoduleProgramInitStarted;",
    ):
        require(header, token, f"{label} two-phase shutdown declaration")

    constructor = function_body(source, "idGameLocal::idGameLocal()", f"{label} game constructor")
    for token in (
        "moduleIdLibInitialized( false )",
        "moduleShutdownStarted( false )",
        "moduleShutdownFinalized( false )",
        "moduleEventInitStarted( false )",
        "moduleClassInitStarted( false )",
        "moduleProgramInitStarted( false )",
    ):
        require(constructor, token, f"{label} game lifecycle initialization")

    init_start = source.index("void idGameLocal::Init(")
    shutdown_start = source.index("void idGameLocal::Shutdown( void )", init_start)
    init = source[init_start:shutdown_start]
    for token in (
        "moduleShutdownStarted = false;",
        "moduleShutdownFinalized = false;",
        "moduleEventInitStarted = false;",
        "moduleClassInitStarted = false;",
        "moduleProgramInitStarted = false;",
        "assert( !moduleIdLibInitialized );",
        "idLib::Init();",
        "moduleIdLibInitialized = true;",
    ):
        require(init, token, f"{label} game lifecycle reset")
    if not (
        init.index("moduleShutdownStarted = false;")
        < init.index("moduleShutdownFinalized = false;")
        < init.index("moduleEventInitStarted = false;")
        < init.index("moduleClassInitStarted = false;")
        < init.index("moduleProgramInitStarted = false;")
        < init.index("idLib::Init();")
        < init.index("moduleIdLibInitialized = true;")
    ):
        raise AssertionError(f"{label} lifecycle guards are not reset before idLib initialization")

    init_pairs = (
        ("moduleEventInitStarted = true;", "idEvent::Init();", "event system"),
        ("moduleClassInitStarted = true;", "idClass::Init();", "class system"),
        ("moduleProgramInitStarted = true;", "program.Startup( SCRIPT_DEFAULT );", "script program"),
    )
    for flag, call, subsystem in init_pairs:
        require(init, f"\t{flag}\n\t{call}", f"{label} {subsystem} partial-init tracking")
    if not (
        init.index("moduleEventInitStarted = true;")
        < init.index("idEvent::Init();")
        < init.index("moduleClassInitStarted = true;")
        < init.index("idClass::Init();")
        < init.index("moduleProgramInitStarted = true;")
        < init.index("program.Startup( SCRIPT_DEFAULT );")
    ):
        raise AssertionError(f"{label} subsystem init progress is not recorded in dependency order")

    early = function_body(source, "void idGameLocal::Shutdown( void )", f"{label} early game shutdown")
    for token in (
        "if ( moduleShutdownStarted )",
        "moduleShutdownStarted = true;",
        "if ( !common )",
        "if ( gamestate == GAMESTATE_UNINITIALIZED )",
    ):
        require(early, token, f"{label} early game shutdown")
    if not (
        early.index("if ( moduleShutdownStarted )")
        < early.index("moduleShutdownStarted = true;")
        < early.index("if ( !common )")
        < early.index("if ( gamestate == GAMESTATE_UNINITIALIZED )")
    ):
        raise AssertionError(f"{label} early shutdown does not fail safely before service-dependent cleanup")

    uninitialized = function_body(
        early,
        "if ( gamestate == GAMESTATE_UNINITIALIZED )",
        f"{label} partial-init shutdown",
    )
    for token in ("aasList.DeleteContents( true );", "aasNames.Clear();"):
        require(uninitialized, token, f"{label} partial AAS cleanup")

    shutdown_pairs = (
        ("moduleEventInitStarted", "idEvent::Shutdown();", "event system"),
        ("moduleProgramInitStarted", "program.Shutdown();", "script program"),
        ("moduleClassInitStarted", "idClass::Shutdown();", "class system"),
    )
    for flag, call, subsystem in shutdown_pairs:
        partial_guard = function_body(
            uninitialized,
            f"if ( {flag} )",
            f"{label} partial {subsystem} shutdown",
        )
        require(partial_guard, call, f"{label} partial {subsystem} shutdown")
        require(partial_guard, f"{flag} = false;", f"{label} partial {subsystem} reset")
        if partial_guard.index(call) > partial_guard.index(f"{flag} = false;"):
            raise AssertionError(f"{label} clears {subsystem} progress before shutting it down")
    if not (
        uninitialized.index("aasList.DeleteContents( true );")
        < uninitialized.index("aasNames.Clear();")
        < uninitialized.index("if ( moduleEventInitStarted )")
        < uninitialized.index("if ( moduleProgramInitStarted )")
        < uninitialized.index("if ( moduleClassInitStarted )")
        < uninitialized.rindex("return;")
    ):
        raise AssertionError(f"{label} partial-init resources are not released in dependency order")

    normal = early[early.index(uninitialized) + len(uninitialized) :]
    for flag, call, subsystem in shutdown_pairs:
        normal_guard = function_body(
            normal,
            f"if ( {flag} )",
            f"{label} normal {subsystem} shutdown",
        )
        require(normal_guard, call, f"{label} normal {subsystem} shutdown")
        require(normal_guard, f"{flag} = false;", f"{label} normal {subsystem} reset")
    if not (
        normal.index("if ( moduleEventInitStarted )")
        < normal.index("if ( moduleProgramInitStarted )")
        < normal.index("if ( moduleClassInitStarted )")
    ):
        raise AssertionError(f"{label} normal subsystem shutdown does not match partial-init ordering")
    for token in (
        "ShutdownConsoleCommands();",
        "RemoveFlaggedAutoCompletion",
        "animationLib->Shutdown();",
        "delete animationLib;",
        "delete visemeTable100;",
        "delete visemeTable66;",
        "delete visemeTable33;",
        "idLib::ShutDown();",
    ):
        reject(early, token, f"{label} early game shutdown")

    late = function_body(
        source,
        "void idGameLocal::ShutdownAfterDecls( void )",
        f"{label} late game finalization",
    )
    for token in (
        "if ( moduleShutdownFinalized )",
        "moduleShutdownFinalized = true;",
        "ShutdownConsoleCommands();",
        "cvarSystem->RemoveFlaggedAutoCompletion( CVAR_GAME );",
        "animationLib->Shutdown();",
        "delete animationLib;",
        "animationLib = NULL;",
        "delete visemeTable100;",
        "visemeTable100 = NULL;",
        "delete visemeTable66;",
        "visemeTable66 = NULL;",
        "delete visemeTable33;",
        "visemeTable33 = NULL;",
        "if ( moduleIdLibInitialized )",
        "idLib::ShutDown();",
        "moduleIdLibInitialized = false;",
    ):
        require(late, token, f"{label} late game finalization")
    for token in ("idEvent::Shutdown();", "program.Shutdown();", "idClass::Shutdown();"):
        reject(late, token, f"{label} late game finalization")
    if not (
        late.index("if ( moduleShutdownFinalized )")
        < late.index("moduleShutdownFinalized = true;")
        < late.index("ShutdownConsoleCommands();")
        < late.index("animationLib->Shutdown();")
        < late.index("idLib::ShutDown();")
        < late.index("moduleIdLibInitialized = false;")
    ):
        raise AssertionError(f"{label} late finalization releases module resources in an unsafe order")


def validate_two_phase_game_api_contract() -> None:
    game_api_path = GAME_LIBS_ROOT / "src" / "game" / "Game.h"
    sp_source_path = GAME_LIBS_ROOT / "src" / "game" / "Game_local.cpp"
    sp_header_path = GAME_LIBS_ROOT / "src" / "game" / "Game_local.h"
    mp_source_path = GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.cpp"
    mp_header_path = GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.h"
    required = (game_api_path, sp_source_path, sp_header_path, mp_source_path, mp_header_path)
    if not all(path.is_file() for path in required):
        print(
            "game_type_module_selection: skipped two-phase lifecycle cross-check "
            f"(incomplete GameLibs checkout at {GAME_LIBS_ROOT})"
        )
        return

    game_api = read(game_api_path)
    require(game_api, "const int GAME_API_VERSION\t\t= 48;", "current game API version")
    require(
        game_api,
        "virtual void\t\t\t\tShutdownAfterDecls( void ) = 0;",
        "two-phase game API",
    )
    validate_game_module_two_phase_lifecycle(read(sp_source_path), read(sp_header_path), "SP")
    validate_game_module_two_phase_lifecycle(read(mp_source_path), read(mp_header_path), "MP")


def parse_string_array(source: str, declaration: str) -> list[str]:
    start = source.find(declaration)
    if start == -1:
        raise AssertionError(f"Missing array declaration {declaration!r}")
    open_brace = source.index("{", start)
    close_brace = source.index("}", open_brace)
    body = source[open_brace + 1 : close_brace]
    return re.findall(r'"([^"]*)"', body)


def parse_gametype_table(source: str) -> list[tuple[str, bool]]:
    declaration = "static const mpGameTypeInfo_t mpGameTypeInfoTable[] = {"
    start = source.find(declaration)
    if start == -1:
        raise AssertionError(f"Missing table declaration {declaration!r}")
    body = source[start + len(declaration) :]
    end = body.find("\n};")
    if end == -1:
        raise AssertionError("Unterminated mpGameTypeInfoTable")
    body = body[:end]

    rows = re.findall(
        r"\{\s*GAME_[A-Z0-9_]+,\s*\"([^\"]*)\".*?MP_GAMESTATE_[A-Z0-9_]+,\s*(true|false)\s*\}",
        body,
        re.DOTALL,
    )
    if not rows:
        raise AssertionError("Could not parse any mpGameTypeInfoTable rows")
    return [(name, flag == "true") for name, flag in rows]


def parse_engine_route_table(source: str) -> list[tuple[str, bool]]:
    declaration = "static const openQ4GameTypeRoute_t openQ4_multiplayerGameTypes[] = {"
    start = source.find(declaration)
    if start == -1:
        raise AssertionError(f"Missing route table declaration {declaration!r}")
    body = source[start + len(declaration) :]
    end = body.find("\n};")
    if end == -1:
        raise AssertionError("Unterminated engine multiplayer route table")
    rows = re.findall(r'\{\s*"([^"]+)",\s*(true|false)\s*\}', body[:end])
    if not rows:
        raise AssertionError("Could not parse engine multiplayer route table")
    return [(name, flag == "true") for name, flag in rows]


def validate_engine_mirror() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    engine_routes = parse_engine_route_table(common)
    if not engine_routes:
        raise AssertionError("engine multiplayer gametype allowlist is empty")
    engine_types = [name for name, _selectable in engine_routes]
    reserved_routes = [name for name, selectable in engine_routes if not selectable]
    if not reserved_routes:
        raise AssertionError("engine route table no longer distinguishes reserved multiplayer wire tokens")

    game_types_source = GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "GameTypes.cpp"
    if not game_types_source.is_file():
        print(
            f"game_type_module_selection: skipped table cross-check "
            f"(no GameLibs checkout at {GAME_LIBS_ROOT})"
        )
    else:
        game_types_text = read(game_types_source)
        game_types = parse_string_array(game_types_text, "const char *si_gameTypeArgs[] = {")
        if not game_types:
            raise AssertionError("GameLibs si_gameTypeArgs is empty")
        if game_types[0] != "singleplayer":
            raise AssertionError(
                f"si_gameTypeArgs[0] is {game_types[0]!r}, expected 'singleplayer'"
            )

        # All descriptor rows route to game_mp, including append-only reserved
        # wire tokens, but only implemented/selectable rows may survive module
        # normalization. Otherwise game_mp registration would silently coerce
        # a reserved token to si_gameTypeArgs[0] (singleplayer).
        table_types = parse_gametype_table(game_types_text)
        expected_routes = [row for row in table_types if row[0] != "singleplayer"]
        if engine_routes != expected_routes:
            raise AssertionError(
                "openQ4 multiplayer routes have drifted from mpGameTypeInfoTable:\n"
                f"  engine:   {engine_routes}\n"
                f"  gamelibs: {expected_routes}"
            )

        selectable = [
            name for name, is_selectable in table_types
            if is_selectable and name != "singleplayer"
        ]
        missing_selectable = [name for name in selectable if name not in engine_types]
        if missing_selectable:
            raise AssertionError(
                "selectable multiplayer gametypes missing from the engine allowlist "
                f"(they would boot game_sp): {missing_selectable}"
            )
        unlisted = [name for name in game_types[1:] if name not in engine_types]
        if unlisted:
            raise AssertionError(
                f"si_gameTypeArgs entries missing from the engine allowlist: {unlisted}"
            )

    # An allowlist, not "anything that is not singleplayer".
    for token in (
        "struct openQ4GameTypeRoute_t {",
        "bool selectable;",
        "static const openQ4GameTypeRoute_t openQ4_multiplayerGameTypes[] = {",
        "static const char *openQ4_CanonicalMultiplayerGameType( const char *gameType, const bool selectableOnly ) {",
        "static bool openQ4_IsMultiplayerGameType( const char *gameType ) {",
        "for ( int i = 0; openQ4_multiplayerGameTypes[i].name != NULL; i++ ) {",
        "( !selectableOnly || route.selectable )",
        "openQ4_CanonicalMultiplayerGameType( gameType, false )",
        "openQ4_CanonicalMultiplayerGameType( currentGameType, true )",
        "static void openQ4_NormalizeGameTypeForModule( const char *moduleName ) {",
        'cvarSystem->SetCVarString( "si_gameType", "singleplayer" );',
        'cvarSystem->SetCVarString( "si_gameType", "DM" );',
    ):
        require(common, token, "engine multiplayer gametype allowlist")

    # A dedicated server has no single-player mode; it must not be routed to
    # game_sp by an unrecognised gametype.
    require(common, "#ifdef ID_DEDICATED", "dedicated game module selection")
    require(
        common,
        'return ( gameType != NULL && idStr::Icmp( gameType, "singleplayer" ) == 0 ) ? "game_sp" : "game_mp";',
        "dedicated game module selection",
    )


def validate_default_cfg() -> None:
    cfg = read(ROOT / "content" / "baseoq4" / "pak0" / "default.cfg")
    require(cfg, "sets\tsi_gameType\t\tsingleplayer", "shipped default gametype")
    if re.search(r"^sets\s+si_gameType\s+dm\s*$", cfg, re.MULTILINE | re.IGNORECASE):
        raise AssertionError("default.cfg still selects a multiplayer gametype")


def validate_swap_guard() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    start = common.index("void Com_ReloadGameModule_f( const idCmdArgs &args ) {")
    end = common.index("idCommonLocal::GetLanguageDict", start)
    body = common[start:end]
    for token in (
        "try {",
        "commonLocal.ShutdownGame( true );",
        "commonLocal.InitGame();",
        "catch( idException &ex ) {",
        "swapFailed = true;",
        "failedPhase = Com_GetGameModuleLoadPhase();",
        "Com_GameModuleLoadPhaseName( failedPhase )",
        "============= ReloadGameModule failed ============",
        "session->StartMenu();",
    ):
        require(body, token, "game module swap exception guard")


def validate_async_module_state() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    validate_shutdown_lifecycle_contract(common)
    helper_start = common.index(
        "static bool openQ4_ShouldUseSmoothSingleplayerSlowTime( float effectiveTimeScale ) {"
    )
    helper_end = common.index("\n}\n", helper_start)
    helper = common[helper_start:helper_end]

    # The async thread runs while game DLL initialization registers static
    # CVars. A name-based lookup here races the registry's backing array, and a
    # direct idStr read still has a phase-check/read timing gap during unload.
    if "cvarSystem->" in helper or "GetCVarString" in helper or "com_activeGameModule" in helper:
        raise AssertionError("async slow-time helper still reads mutable CVar/module strings")
    require(
        common,
        "static std::atomic<bool> openQ4_singleplayerGameModuleReady( false );",
        "async-safe game module state",
    )
    require(
        helper,
        "openQ4_singleplayerGameModuleReady.load( std::memory_order_acquire )",
        "async-safe game module state",
    )
    require(
        helper,
        "return effectiveTimeScale < 0.999f;",
        "composed single-player slow-time detection",
    )

    load_start = common.index("void idCommonLocal::LoadGameDLL( void ) {")
    unload_start = common.index("void idCommonLocal::UnloadGameDLL( void ) {", load_start)
    load_body = common[load_start:unload_start]
    require(
        load_body,
        "openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );",
        "game module load transition",
    )
    require(
        load_body,
        'game != NULL && idStr::Icmp( gameModuleBaseName, "game_sp" ) == 0,',
        "game module ready transition",
    )
    require(
        load_body,
        "openQ4_NormalizeGameTypeForModule( gameModuleBaseName );",
        "multiplayer game type normalization before module load",
    )
    if load_body.index("openQ4_NormalizeGameTypeForModule") > load_body.index("sys->DLL_Load"):
        raise AssertionError("multiplayer game type is normalized after the module starts loading")

    init_start = common.index("void idCommonLocal::InitGame( void ) {")
    init_end = common.index("void idCommonLocal::ShutdownGame( bool reloading ) {", init_start)
    init_body = common[init_start:init_end]
    for token in (
        "const idStr pendingGameModule = openQ4_SelectGameModuleBaseName();",
        "openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );",
    ):
        require(init_body, token, "multiplayer game type normalization before declaration initialization")
    if not (
        init_body.index("const idStr pendingGameModule")
        < init_body.index("openQ4_NormalizeGameTypeForModule( pendingGameModule.c_str() );")
        < init_body.index("declManager->Init")
    ):
        raise AssertionError("multiplayer game type is normalized after declarations initialize")

    reject(common, "openQ4_StartupRequiresMultiplayerModule", "startup command preselection")
    common_init_start = common.index("void idCommonLocal::Init( int argc")
    common_init_end = common.index("void idCommonLocal::Shutdown( void ) {", common_init_start)
    common_init = common[common_init_start:common_init_end]
    for token in (
        'SetCVarString( "com_nextGameModule", "game_mp" )',
        'openQ4_NormalizeGameTypeForModule( "game_mp" );',
    ):
        reject(common_init, token, "startup command preselection")

    validate_font_resource_reset_contract(
        read(ROOT / "src" / "renderer" / "tr_font.cpp"),
        read(ROOT / "src" / "renderer" / "tr_local.h"),
    )
    validate_full_vid_restart_font_contract(read(ROOT / "src" / "renderer" / "RenderSystem_init.cpp"))
    validate_ui_font_reload_contract(
        read(ROOT / "src" / "ui" / "DeviceContext.cpp"),
        read(ROOT / "src" / "ui" / "DeviceContext.h"),
        read(ROOT / "src" / "ui" / "UserInterface.h"),
    )
    validate_ttf_persistent_atlas_contract(read(ROOT / "src" / "renderer" / "tr_fontTTF.cpp"))
    validate_ttf_level_load_restore_contract()


def validate_lifecycle_mutation_sensitivity() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")

    shutdown_call = "\t\tgame->Shutdown();\n"
    ui_shutdown = "\tuiManager->Shutdown();"
    if common.count(shutdown_call) != 1 or common.count(ui_shutdown) != 1:
        raise AssertionError("Game shutdown lifecycle mutation anchors are not unique")
    game_after_ui = common.replace(shutdown_call, "", 1).replace(
        ui_shutdown,
        ui_shutdown + "\n\tgame->Shutdown();",
        1,
    )
    expect_contract_rejection(
        validate_shutdown_lifecycle_contract,
        game_after_ui,
        "game object shuts down after UI destruction",
    )

    late_call = "\t\tgame->ShutdownAfterDecls();\n"
    decl_shutdown = "\tdeclManager->Shutdown();"
    if common.count(late_call) != 1 or common.count(decl_shutdown) != 1:
        raise AssertionError("Late game/decl shutdown mutation anchors are not unique")
    late_before_decls = common.replace(late_call, "", 1).replace(
        decl_shutdown,
        "\tgame->ShutdownAfterDecls();\n" + decl_shutdown,
        1,
    )
    expect_contract_rejection(
        validate_shutdown_lifecycle_contract,
        late_before_decls,
        "late module finalization destroys animation/idLib before module-owned declarations",
    )

    unload_phase = "\tCom_SetGameModuleLoadPhase( GAME_MODULE_PHASE_BINARY_UNLOAD );"
    if common.count(unload_phase) != 1:
        raise AssertionError("Game DLL unload phase mutation anchor is not unique")
    duplicate_shutdown = common.replace(
        unload_phase,
        unload_phase + "\n\tgame->Shutdown();",
        1,
    )
    expect_contract_rejection(
        validate_shutdown_lifecycle_contract,
        duplicate_shutdown,
        "binary unload invokes game shutdown a second time",
    )

    font_source = read(ROOT / "src" / "renderer" / "tr_font.cpp")
    font_header = read(ROOT / "src" / "renderer" / "tr_local.h")
    if font_source.count("R_ShutdownTrueTypeFonts();") != 1:
        raise AssertionError("TrueType shutdown mutation anchor is not unique")
    expect_contract_rejection(
        lambda candidate: validate_font_resource_reset_contract(candidate, font_header),
        font_source.replace("R_ShutdownTrueTypeFonts();", "", 1),
        "renderer shutdown keeps stale TrueType faces and console guard state",
    )

    renderer = read(ROOT / "src" / "renderer" / "RenderSystem_init.cpp")
    if renderer.count("\tR_DoneFreeType();") != 1:
        raise AssertionError("Full vid_restart font-release mutation anchor is not unique")
    expect_contract_rejection(
        validate_full_vid_restart_font_contract,
        renderer.replace("\tR_DoneFreeType();", "", 1),
        "full vid_restart purges images without releasing font state",
    )
    restart_device = "\ttr.InitOpenGL();\n\tcvarSystem->SetCVarBool( \"r_fullscreen\", latchedFullscreen );"
    if renderer.count(restart_device) != 1:
        raise AssertionError("Full vid_restart device-startup mutation anchor is not unique")
    expect_contract_rejection(
        validate_full_vid_restart_font_contract,
        renderer.replace(
            restart_device,
            "\tR_RefreshConsoleFontAtlas();\n" + restart_device,
            1,
        ),
        "console atlas refresh runs before persistent image allocation",
    )
    expect_contract_rejection(
        validate_full_vid_restart_font_contract,
        renderer.replace(restart_device, restart_device.replace("tr.InitOpenGL();", "R_InitOpenGL();"), 1),
        "full vid_restart bypasses native Vulkan device startup",
    )
    expect_contract_rejection(
        validate_full_vid_restart_font_contract,
        renderer.replace(restart_device, "\tglobalImages->ReloadImages( true );\n" + restart_device, 1),
        "full vid_restart reloads images outside backend device startup",
    )

    partial_refresh = "\t\t\tR_RefreshConsoleFontAtlas();"
    legacy_restart = function_body(renderer, "void R_VidRestart_f", "legacy restart mutation")
    if legacy_restart.count(partial_refresh) != 1:
        raise AssertionError("Partial vid_restart console-refresh mutation anchor is not unique")
    expect_contract_rejection(
        validate_full_vid_restart_font_contract,
        renderer.replace(legacy_restart, legacy_restart.replace(partial_refresh, "", 1), 1),
        "successful partial vid_restart leaves resolution-dependent console glyphs stale",
    )

    device_source = read(ROOT / "src" / "ui" / "DeviceContext.cpp")
    device_header = read(ROOT / "src" / "ui" / "DeviceContext.h")
    public_header = read(ROOT / "src" / "ui" / "UserInterface.h")
    generation_before_reload = (
        "\tfontsVideoRestartCount = currentRestartCount;\n"
        "\tfontsCodePageGeneration = currentCodePageGeneration;\n"
        "\tif ( !ReloadFonts() )"
    )
    generation_after_reload = (
        "\tif ( !ReloadFonts() )"
        "\n\tfontsVideoRestartCount = currentRestartCount;"
        "\n\tfontsCodePageGeneration = currentCodePageGeneration;"
    )
    if device_source.count(generation_before_reload) != 1:
        raise AssertionError("Lazy font-generation mutation anchor is not unique")
    expect_contract_rejection(
        lambda candidate: validate_ui_font_reload_contract(
            candidate,
            device_header,
            public_header,
        ),
        device_source.replace(generation_before_reload, generation_after_reload, 1),
        "lazy UI font refresh records generation after recursive registration",
    )

    ttf_source = read(ROOT / "src" / "renderer" / "tr_fontTTF.cpp")
    if ttf_source.count("opts.isPersistant = true;") != 2:
        raise AssertionError("TrueType persistent-atlas mutation anchors are not exact")
    expect_contract_rejection(
        validate_ttf_persistent_atlas_contract,
        ttf_source.replace("\topts.isPersistant = true;\n", "", 1),
        "GUI TrueType atlas is not recreated across full vid_restart",
    )

    atlas_identity = "\tconst idStr materialName = imageName;"
    if ttf_source.count(atlas_identity) != 2:
        raise AssertionError("TrueType atlas identity mutation anchors are not exact")
    expect_contract_rejection(
        validate_ttf_persistent_atlas_contract,
        ttf_source.replace(atlas_identity, '\tconst idStr materialName = "detached-atlas-material";', 1),
        "generated TrueType material probes a nonexistent image identity on first parse",
    )

    create_install = "\tR_TTFInstallAtlasStage( material, source );"
    if ttf_source.count(create_install) != 1:
        raise AssertionError("TrueType atlas source-install mutation anchor is not exact")
    conditional_install = (
        "\tif ( !R_TTFMaterialSamplesAtlas( material, imageName ) ) {\n"
        "\t\tR_TTFInstallAtlasStage( material, source );\n"
        "\t}"
    )
    expect_contract_rejection(
        validate_ttf_persistent_atlas_contract,
        ttf_source.replace(create_install, conditional_install, 1),
        "generated TrueType material keeps an implicit or restart-stale stage",
    )

    for label, source_path, header_path in (
        (
            "SP",
            GAME_LIBS_ROOT / "src" / "game" / "Game_local.cpp",
            GAME_LIBS_ROOT / "src" / "game" / "Game_local.h",
        ),
        (
            "MP",
            GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.cpp",
            GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.h",
        ),
    ):
        if not source_path.is_file() or not header_path.is_file():
            continue
        module_source = read(source_path)
        module_header = read(header_path)
        early_guard = "\tmoduleShutdownStarted = true;"
        if module_source.count(early_guard) != 1:
            raise AssertionError(f"{label} early-shutdown mutation anchor is not unique")
        expect_contract_rejection(
            lambda candidate, header=module_header, name=label: validate_game_module_two_phase_lifecycle(
                candidate,
                header,
                name,
            ),
            module_source.replace(
                early_guard,
                early_guard + "\n\tanimationLib->Shutdown();",
                1,
            ),
            f"{label} early shutdown destroys animation state before declarations",
        )
        if module_source.count("\t\tidLib::ShutDown();") != 1:
            raise AssertionError(f"{label} idLib finalization mutation anchor is not unique")
        expect_contract_rejection(
            lambda candidate, header=module_header, name=label: validate_game_module_two_phase_lifecycle(
                candidate,
                header,
                name,
            ),
            module_source.replace("\t\tidLib::ShutDown();\n", "", 1),
            f"{label} late shutdown leaves module idLib initialized",
        )

        for flag, call, subsystem in (
            ("moduleEventInitStarted", "idEvent::Init();", "event system"),
            ("moduleClassInitStarted", "idClass::Init();", "class system"),
            ("moduleProgramInitStarted", "program.Startup( SCRIPT_DEFAULT );", "script program"),
        ):
            tracked_init = f"\t{flag} = true;\n\t{call}"
            late_tracking = f"\t{call}\n\t{flag} = true;"
            if module_source.count(tracked_init) != 1:
                raise AssertionError(f"{label} {subsystem} init-tracking mutation anchor is not unique")
            expect_contract_rejection(
                lambda candidate, header=module_header, name=label: validate_game_module_two_phase_lifecycle(
                    candidate,
                    header,
                    name,
                ),
                module_source.replace(tracked_init, late_tracking, 1),
                f"{label} records {subsystem} initialization only after the fallible call",
            )

        partial_aas = "\t\taasList.DeleteContents( true );\n"
        if module_source.count(partial_aas) != 1:
            raise AssertionError(f"{label} partial AAS cleanup mutation anchor is not unique")
        expect_contract_rejection(
            lambda candidate, header=module_header, name=label: validate_game_module_two_phase_lifecycle(
                candidate,
                header,
                name,
            ),
            module_source.replace(partial_aas, "", 1),
            f"{label} partial initialization leaks allocated AAS instances",
        )


def main() -> int:
    validate_engine_mirror()
    validate_two_phase_game_api_contract()
    validate_font_restart_documentation()
    validate_default_cfg()
    validate_swap_guard()
    validate_async_module_state()
    validate_lifecycle_mutation_sensitivity()
    print("game_type_module_selection: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
