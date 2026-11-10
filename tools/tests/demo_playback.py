#!/usr/bin/env python3
"""Structural contracts for the unified demo browser and playback controls.

The demo UI spans session dispatch, several playback backends, a large GUI,
and every shipped language table. These checks keep format capabilities honest
and prevent a harmless-looking menu edit from exposing an unsupported action.
"""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(text: str, needle: str, context: str) -> None:
    if needle in text:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def between(text: str, start: str, end: str, context: str) -> str:
    start_at = text.find(start)
    if start_at < 0:
        raise AssertionError(f"Missing start marker {start!r} in {context}")
    end_at = text.find(end, start_at + len(start))
    if end_at < 0:
        raise AssertionError(f"Missing end marker {end!r} in {context}")
    return text[start_at:end_at]


def require_order(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0:
        raise AssertionError(
            f"Missing ordered contract in {context}: {first!r}, {second!r}"
        )
    if first_at >= second_at:
        raise AssertionError(f"{first!r} must precede {second!r} in {context}")


def validate_library_contract() -> None:
    source = read("src/framework/Session_demo.cpp")
    header = read("src/framework/Session_local.h")

    for needle in (
        "DEMO_CAP_PLAY",
        "DEMO_CAP_PAUSE",
        "DEMO_CAP_RATE",
        "DEMO_CAP_SEEK",
        "DEMO_CAP_STEP",
        "DEMO_CAP_FREE_ROAM",
        "DEMO_CAP_FOLLOW",
        "DEMO_CAP_DELETE",
        "DEMO_CAP_FULL_WORLD",
    ):
        require(header, needle, "demo capability model")

    for extension in (".mvd", ".demo", ".cdemo", ".netdemo", ".part", ".ucmd"):
        require(
            source,
            f'DemoCollectExtension( paths, "{extension}" );',
            "demo library discovery",
        )

    for demo_filter in ("all", "mvd", "render", "legacy", "incomplete"):
        require(source, f'"{demo_filter}"', "demo library filters")

    for needle in (
        "multiViewDemo.QueryFileInfo",
        "OQ4MVD %u.%u / %u.%u",
        'AMBIGUOUS_MAGIC[] = "Quake4 RDEMO"',
        "probe.OpenForReading( entry.path.c_str(), false, true )",
        "OPENQ4_RENDERDEMO_CURRENT_VERSION",
        "ValidateCmdDemoFile",
        'memcmp( prefix, "NDMO", 4 )',
        "DEMO_LIBRARY_LEGACY_RENDER",
        "DEMO_LIBRARY_LEGACY_NET",
        "DEMO_LIBRARY_INCOMPLETE",
        "DemoPathIsSafe",
        "RemoveExplicitFile",
    ):
        require(source, needle, "demo probing and safety")

    incomplete = between(
        source,
        'if ( DemoPathEndsWith( entry.path, ".mvd.part" )',
        '} else if ( DemoPathEndsWith( entry.path, ".mvd" ) ) {',
        "incomplete demo branch",
    )
    reject(incomplete, "DEMO_CAP_PLAY", "incomplete demo capabilities")

    filter_match = between(
        source,
        "static bool DemoEntryMatchesFilter",
        "static bool DemoEntryCanDelete",
        "demo filter matching",
    )
    incomplete_filter = between(
        filter_match,
        "if ( !filter.Icmp( DEMO_FILTER_INCOMPLETE ) ) {",
        "return true;",
        "incomplete filter",
    )
    require(
        incomplete_filter,
        "return entry.type == DEMO_LIBRARY_INCOMPLETE;",
        "incomplete-only filter",
    )
    reject(incomplete_filter, "!entry.compatible", "incomplete-only filter")

    mvd_probe = between(
        source,
        '} else if ( DemoPathEndsWith( entry.path, ".mvd" ) ) {',
        '} else if ( DemoPathEndsWith( entry.path, ".demo" ) ) {',
        "MVD browser probe",
    )
    require(mvd_probe, "entry.playable = info.compatible;", "MVD compatibility gate")
    require(mvd_probe, "info.error", "specific MVD incompatibility reason")

    command_demo = between(
        source,
        '} else if ( DemoPathEndsWith( entry.path, ".cdemo" ) ) {',
        '} else if ( DemoPathEndsWith( entry.path, ".netdemo" ) ) {',
        "command demo branch",
    )
    require(command_demo, "ValidateCmdDemoFile", "command demo structural preflight")
    require(command_demo, "entry.capabilities = DEMO_CAP_PLAY;", "command demo capability")
    for unsupported in (
        "DEMO_CAP_PAUSE",
        "DEMO_CAP_RATE",
        "DEMO_CAP_SEEK",
        "DEMO_CAP_STEP",
        "DEMO_CAP_FREE_ROAM",
        "DEMO_CAP_FOLLOW",
    ):
        reject(command_demo, unsupported, "command demo capability")

    legacy_net = between(
        source,
        '} else if ( DemoPathEndsWith( entry.path, ".netdemo" ) ) {',
        "if ( DemoEntryCanDelete( entry.path ) ) {",
        "legacy netdemo branch",
    )
    reject(legacy_net, "DEMO_CAP_PLAY", "unsupported retail netdemo capability")
    require(legacy_net, '"#str_41547"', "legacy netdemo status")

    # The browser's Play and the retained Demos page share one typed player.
    browser_play = between(
        source,
        'if ( !idStr::Icmp( cmd, "demoPlay" ) ) {',
        'if ( !idStr::Icmp( cmd, "demoDelete" ) ) {',
        "browser demo playback",
    )
    require(browser_play, "PlayDemoLibraryEntry( selection )", "browser demo playback")
    dispatch = between(
        source,
        "bool idSessionLocal::PlayDemoLibraryEntry( int index ) {",
        "bool idSessionLocal::DeleteDemoLibraryFile(",
        "typed demo playback dispatch",
    )
    for needle in (
        "idMultiViewDemo::Play_f( play );",
        "StartPlayingRenderDemo( entry.path );",
        "StartPlayingCmdDemo( commandName.c_str() );",
    ):
        require(dispatch, needle, "typed demo playback dispatch")
    reject(dispatch, "commandName.StripFileExtension();", "dotted command demo dispatch")
    # Deletion stays inside the save path's demo directory for both.
    deletion = between(
        source,
        "bool idSessionLocal::DeleteDemoLibraryFile(",
        "bool idSessionLocal::HandleDemoMenuCommand",
        "demo deletion",
    )
    for needle in ("DemoPathIsSafe( entry.path )", "saveDemoRoot", "DemoEntryCanDelete( entry.path )", "RemoveExplicitFile"):
        require(deletion, needle, "demo deletion containment")

    path_check = between(
        source,
        "static bool DemoPathIsSafe",
        "static idStr DemoSanitizeText",
        "demo path containment",
    )
    for needle in ('"demos/"', 'path.Find( ".." ) < 0', 'path.Find( ":" ) < 0'):
        require(path_check, needle, "demo path containment")


def validate_playback_controls() -> None:
    source = read("src/framework/Session_demo.cpp")
    session = read("src/framework/Session.cpp")
    session_menu = read("src/framework/Session_menu.cpp")
    demo_file = read("src/framework/DemoFile.cpp")
    render_demo = read("src/renderer/RenderWorld_demo.cpp")
    sound_demo = read("src/sound/snd_world.cpp")
    sound_api = read("src/sound/sound.h")

    for command in (
        "demoMenu",
        "demoPause",
        "demoSpeed",
        "demoSeek",
        "demoSkip",
        "demoStep",
        "demoFollow",
        "demoFreeRoam",
        "demoStop",
    ):
        require(source, f'AddCommand( "{command}"', "unified demo commands")

    for needle in (
        "multiViewDemo.SetPaused",
        "renderDemoPaused = paused",
        "multiViewDemo.SetPlaybackScale",
        "renderDemoPlaybackRate = clamped",
        "multiViewDemo.SeekToMS",
        "multiViewDemo.SeekByMS",
        "multiViewDemo.StepFrames",
        "multiViewDemo.FollowNext",
        "multiViewDemo.FreeRoam",
    ):
        require(source, needle, "backend-gated playback controls")

    render_seek = between(
        source,
        "bool idSessionLocal::SeekDemoMS",
        "bool idSessionLocal::SkipDemoMS",
        "render-demo seeking",
    )
    require(
        render_seek,
        "if ( target < DemoCurrentRenderTimeMS( *this ) )",
        "render-demo rewind detection",
    )
    require(
        render_seek,
        "renderDemoSeekRestartPending = true;",
        "deferred render-demo rewind reset",
    )
    require(render_seek, "renderDemoSeekTargetMS = target;", "render-demo seek target")
    require(render_seek, "renderDemoSeekMuteOwned = true;", "render-demo mute ownership")
    reject(render_seek, "StartPlayingRenderDemo", "synchronous render-demo restart")
    reject(render_seek, "while ( readDemo != NULL", "synchronous render-demo seeking")

    seek_processor = between(
        source,
        "void idSessionLocal::ProcessRenderDemoSeekBudget",
        "bool idSessionLocal::SeekDemoMS",
        "render-demo seek processor",
    )
    for needle in (
        "demo_renderSeekBudgetMS",
        "ProcessRenderDemoSeekBudget",
        "frames < 512",
        "AdvanceRenderDemo( true );",
        "FinishRenderDemoSeek",
        "renderDemoSeekRestoreMute",
        "if ( renderDemoSeeking )",
    ):
        require(source, needle, "cooperative render-demo seeking")
    for needle in (
        "if ( renderDemoSeekRestartPending )",
        "renderDemoSeekRestartPresentationPending",
        "StartPlayingRenderDemo( demoName );",
        "renderDemoSeekDeadlineMS = deadline;",
        "renderDemoSeekWorkDeferred",
    ):
        require(seek_processor, needle, "deferred/cooperative render-demo seeking")
    require(session, "if ( renderDemoSeeking )", "render-demo seek pacing bypass")
    require(session, "renderDemoDurationName = activeRenderDemoName;", "render-demo duration cache")
    require(session, "elapsedDemoFrames *", "elapsed-time render-demo rate")
    require(session, "if ( sw->IsPaused() )", "render-demo sound-world resume")
    require(session, "soundSystem->SetMute( restoreMuteOnFailure );", "render-demo failure mute restore")
    require(
        session,
        "const bool restoreMuteOnFailure = soundSystem->IsMuted();",
        "timedemo failure mute restore",
    )
    require(session, "renderDemoSeekDeadlineMS > 0", "command-granular seek deadline")
    require(session, "numDemoFrames = 0;", "clean render-demo initial frame state")
    require(session, "numDemoFrames != 1", "complete render-demo initial frame")
    require(session, "!= sizeof( logCmd )", "exact command-demo frame reads")
    require(session, "Session_ParseCmdDemoHeader", "nonfatal command-demo parser")
    require(session, "SESSION_MAX_CMD_DEMO_HEADER_BYTES", "bounded command-demo header")
    require(
        session,
        'idStr::Icmp( fullDemoName.c_str() + fullDemoName.Length() - 6, ".cdemo" )',
        "dotted command-demo extension handling",
    )
    require(
        session,
        "if ( guiActive == guiDemoMenu && readDemo != NULL )",
        "live render-demo frame behind playback controls",
    )
    require(source, "if ( !timeDemo && !aviCaptureMode )", "capture-safe render capabilities")

    require(session, "if ( IsDemoPlaybackActive() )", "Escape demo overlay")
    require(session, "OpenDemoMenu( false );", "Escape demo overlay")
    require(session_menu, 'if ( gui == guiDemoMenu )', "demo GUI command dispatch")
    require(session_menu, 'if ( !idStr::Icmp( cmd, "demoOpen" ) )', "main-menu demo dispatch")

    for needle in (
        '#define DEMO_MAGIC_HISTORICAL "OpenQ4 RDEMO"',
        '#define DEMO_MAGIC_QUAKE4 "Quake4 RDEMO"',
        "ambiguousQuake4Wrapper",
        "firstToken != DS_VERSION",
        "unsupported retail Quake 4 render-demo stream",
        "allowPreload && com_preloadDemos.GetBool()",
    ):
        require(demo_file, needle, "historical render-demo discrimination")

    hash_reader = between(
        demo_file,
        "const char *idDemoFile::ReadHashString",
        "void idDemoFile::WriteHashString",
        "render-demo hash-string reader",
    )
    for needle in (
        "ReadInt( index ) != sizeof( index )",
        "MAX_DEMO_HASH_STRINGS",
        "length < 0 || length >= MAX_STRING_CHARS",
        "Read( &data[0], length ) != length",
        "index < -1 || index >= demoStrings.Num()",
        "Close();",
        "playback stopped safely",
    ):
        require(hash_reader, needle, "bounded render-demo hash strings")

    render_dispatch = between(
        render_demo,
        "idRenderWorldLocal::ProcessDemoCommand",
        "idRenderWorldLocal::WriteVisibleDefs",
        "render-demo command decoder",
    )
    for needle in (
        "bool R_RejectRenderDemo",
        "demo->Close();",
        "R_DemoReadInt",
        "R_DemoReadFloat",
        "R_DemoReadVec3",
        "R_DemoReadMat3",
        "invalid render command",
        "playback stopped safely",
    ):
        require(render_demo, needle, "nonfatal render-demo decoding")
    reject(render_dispatch, "common->Error(", "render-demo command decoder")

    sound_dispatch = between(
        sound_demo,
        "bool idSoundWorldLocal::ProcessDemoCommand",
        "idSoundWorldLocal::AVIOpen",
        "sound-demo command decoder",
    )
    for needle in (
        "virtual bool",
        "ProcessDemoCommand",
        "returns false when malformed or truncated input",
    ):
        require(sound_api, needle, "recoverable sound-demo API")
    for needle in (
        "StopMalformedSoundDemo",
        "ReadSoundState( readDemo, readDemo )",
        "ReadDemoShaderParms",
        "truncated command identifier",
    ):
        require(sound_dispatch, needle, "nonfatal sound-demo decoding")
    reject(sound_dispatch, "common->Error(", "sound-demo command decoder")
    require(
        sound_demo,
        "ReadSoundState( savefile, NULL );",
        "shared save/demo sound-state decoder",
    )
    require(
        session,
        "if ( !sw->ProcessDemoCommand( readDemo ) )",
        "session propagation of malformed sound demos",
    )
    for needle in (
        "tokenBytes != 0 && tokenBytes != sizeof( ds )",
        "ended before DC_END_FRAME; playback stopped safely",
        "contains invalid token %d; playback stopped safely",
        "streamVersion > OPENQ4_RENDERDEMO_CURRENT_VERSION",
    ):
        require(session, needle, "nonfatal top-level render-demo decoding")


def validate_gui_and_localization() -> None:
    gui = read("content/baseoq4/pak0/guis/demo_menu.gui")
    main_menu = read("content/baseoq4/pak0/guis/mainmenu.gui")
    source = read("src/framework/Session_demo.cpp")

    for needle in (
        "listDef demoList",
        "demo_browser_reveal_timeline",
        "demo_browser_exit_timeline",
        # The retained pages' backdrop: the stock screen's light band and a grid.
        "gfx/guis/mainmenu/screen",
        "gfx/guis/loading/bg_grid_tiled",
        # The filters' rails follow a number: GUI expressions cannot compare strings.
        '"gui::demo_filterIndex" == 0',
        '"gui::demo_filterIndex" == 4',
        "gfx/guis/mainmenu/topbar",
        "gfx/guis/mainmenu/btmbar",
        "demo_browser_topbar_tile_left",
        "demo_browser_topbar_tile_right",
        "demo_browser_btmbar_tile_left",
        "demo_browser_btmbar_tile_right",
        "demo_filter_all_active",
        "demo_filter_mvd_active",
        "demo_filter_render_active",
        "demo_filter_legacy_active",
        "demo_filter_incomplete_active",
        "demo_selected_name",
        "demo_playback_reveal_timeline",
        "demo_playback_transition_blocker",
        "demo_playback_transition_input_blocker",
        "demo_playback_reveal_curtain",
        "demo_playback_deck_surface",
        "demo_playback_deck_line",
        "screenaligny\ttop",
        "screenaligny\tbottom",
        "gfx/guis/mainmenu/b2_dark",
        "gfx/guis/mainmenu/b4_light",
        '"play main_menu_selection ; demoFilter all"',
        '"play main_menu_selection ; demoFilter mvd"',
        '"play main_menu_selection ; demoFilter render"',
        '"play main_menu_selection ; demoFilter legacy"',
        '"play main_menu_selection ; demoFilter incomplete"',
        '"play main_menu_selection ; demoPlay"',
        '"play main_menu_selection ; demoDelete"',
        '"play main_menu_selection ; demoPause"',
        '"play main_menu_selection ; demoSkip -30"',
        '"play main_menu_selection ; demoSkip -10"',
        '"play main_menu_selection ; demoStep"',
        '"play main_menu_selection ; demoSkip 10"',
        '"play main_menu_selection ; demoSkip 30"',
        '"play main_menu_selection ; demoFreeRoam"',
        '"play main_menu_selection ; demoFollow"',
        '"play main_menu_selection ; demoSpeed 0.25"',
        '"play main_menu_selection ; demoSpeed 0.5"',
        '"play main_menu_selection ; demoSpeed 1"',
        '"play main_menu_selection ; demoSpeed 2"',
        '"play main_menu_selection ; demoSpeed 4"',
        '"play main_menu_selection ; demoStop"',
        '"play main_menu_mouseover"',
    ):
        require(gui, needle, "demo menu actions")

    for state in (
        "demo_canPlay",
        "demo_canDelete",
        "demo_canPause",
        "demo_canRate",
        "demo_canSeek",
        "demo_canStep",
        "demo_canFreeRoam",
        "demo_canFollow",
        "demo_paused",
        "demo_seeking",
        "demo_browserMode",
        "demo_interactive",
        "demo_filter",
        "demo_filterIndex",
    ):
        require(gui, f"gui::{state}", "capability-gated demo menu")
        require(source, f'"{state}"', "demo menu state producer")

    require(source, "DemoEllipsizeText", "bounded demo-browser text")
    command_handler = between(
        source,
        "bool idSessionLocal::HandleDemoMenuCommand",
        "return handled;",
        "demo menu compound command handler",
    )
    for needle in (
        "for ( int icmd = 0; icmd < args.Argc(); )",
        '"play"',
        "PlayShaderDirectly",
    ):
        require(command_handler, needle, "demo menu sound/action sequencing")

    require(
        main_menu,
        'set "cmd" "play main_menu_selection ; demoOpen" ;',
        "main-menu Demos entry",
    )
    require(main_menu, 'set "main_t_b9::text" "#str_41500" ;', "localized Demos label")

    open_menu = between(
        source,
        "void idSessionLocal::OpenDemoMenu",
        "void idSessionLocal::CloseDemoMenu",
        "demo browser activation",
    )
    state_update = open_menu.find("UpdateDemoMenuGui();")
    activation = open_menu.find("SetGUI( guiDemoMenu, NULL );")
    if not 0 <= state_update < activation:
        raise AssertionError(
            "Demo browser state must be published before the synchronous GUI activation"
        )

    # GUI display text must be localized or dynamic. A slash is the only
    # intentional literal, used between current and total time.
    for value in re.findall(r'^\s*text\s+"([^"]*)"', gui, re.MULTILINE):
        if value == "/" or value.startswith("#str_") or value.startswith("gui::"):
            continue
        raise AssertionError(f"Unlocalized demo GUI text: {value!r}")

    used_ids = set(re.findall(r"#str_415\d{2}", "\n".join((gui, main_menu, source))))
    expected_ids = {f"#str_{value}" for value in range(41500, 41571)}
    # The long-form seek caption remains in the language tables for existing
    # consumers; the tight capability chip deliberately uses its compact sibling.
    expected_ids.remove("#str_41538")
    # The browser stands in the retained Demos page's layout and takes that
    # page's upper-case title, column and plate labels; its earlier mixed-case
    # ones stay in the language tables for a mod's copy of the stock browser.
    for retired in (41501, 41507, 41509, 41510, 41512, 41513, 41514, 41515, 41530, 41557):
        expected_ids.remove(f"#str_{retired}")
    for label in ("#str_231523", "#str_231521", "#str_231536", "#str_231530", "#str_231531"):
        require(gui, label, "demo browser labels shared with the retained page")
    missing_usage = sorted(expected_ids - used_ids)
    if missing_usage:
        raise AssertionError(f"Demo string contract is not referenced: {missing_usage}")

    lang_paths = sorted(
        (ROOT / "content/baseoq4/pak0/strings").glob("*_openq4.lang")
    )
    if not lang_paths:
        raise AssertionError("No openQ4 localization tables found")
    for path in lang_paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        values = {
            key: value
            for key, value in re.findall(
                r'^\s*"(#str_\d+)"\s+"((?:\\.|[^"])*)"', text, re.MULTILINE
            )
        }
        for string_id in sorted(expected_ids):
            if string_id not in values:
                raise AssertionError(f"Missing {string_id} in {path.name}")
            if not values[string_id].strip():
                raise AssertionError(f"Empty {string_id} in {path.name}")


def validate_documentation_entry_points() -> None:
    readme = read("README.md")
    release = read("docs/dev/release-completion.md")
    require(
        readme,
        "[Demo Library and Multi-View Demos](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/multiview-demos.md)",
        "README demo guide",
    )
    require(release, "MVD 1.2", "release completion demo compatibility note")
    require(release, "no-index/zero-snapshot", "release empty-MVD compatibility note")
    require(release, "network-synchronized cvar allowlist", "release MVD cvar safety note")
    require(
        release,
        "malformed legacy renderer or sound commands",
        "release legacy corruption behavior",
    )
    require(release, "Retail Quake 4 `.demo`/`.netdemo`", "release legacy limitation")


def main() -> int:
    validate_library_contract()
    validate_playback_controls()
    validate_gui_and_localization()
    validate_documentation_entry_points()
    print("demo_playback: browser, controls, capability, and localization checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
