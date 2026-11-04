#!/usr/bin/env python3
"""Execute MVD 1.3 navigation: markers, keyframes, marker diff and auto-tracking.

tools/tests/native/MvdNavigationFixture.cpp compiles against the production
headers src/framework/async/MvdNavigation.h, src/mpgame/mp/MvdMarkers.h and
src/mpgame/mp/DemoAutoTrack.h. It checks marker records and their directory,
keyframe records and their directory, the game baseline grammar, marker
stepping, the server's per-frame marker diff and the playback camera choice.
Single-reversal controls then re-run it against mutated copies of the headers
and require each to fail. Source checks tie the headers to their production
callers: the recorder, the reader, the keyframe seek, recovery, the game's
baseline API, the marker publisher and the auto-track adapter.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAVIGATION = "src/framework/async/MvdNavigation.h"
MARKERS = "src/mpgame/mp/MvdMarkers.h"
AUTOTRACK = "src/mpgame/mp/DemoAutoTrack.h"

MUTATIONS = (
    ("marker text keeps control bytes", NAVIGATION,
     "if ( text[i] < 0x20 || text[i] == 0x7f ) { return false; }", "( void )text;"),
    ("a marker directory accepts trailing bytes", NAVIGATION,
     "\tif ( cursor != length ) {\n\t\tmarkers.clear();", "\tif ( false ) {\n\t\tmarkers.clear();"),
    ("keyframe directory entries need not ascend", NAVIGATION,
     "keyframe.fileOffset <= keyframes.back().fileOffset ||", "false ||"),
    ("a keyframe directory may point past the live stream", NAVIGATION,
     "keyframe.fileOffset >= tableOffset ||", "false ||"),
    ("a baseline for another snapshot is accepted", NAVIGATION,
     "ReadI32( data + 4 ) != sequence ||", "false ||"),
    ("a baseline accepts a truncated PVS", NAVIGATION,
     "static_cast<std::int64_t>( pvsWords ) * 4 == length;", "static_cast<std::int64_t>( pvsWords ) * 4 <= length;"),
    ("marker stepping reselects the marker just reached", NAVIGATION,
     "for ( int i = lastIndex + direction; i >= 0 && i < count; i += direction ) {",
     "for ( int i = lastIndex; i >= 0 && i < count; i += direction ) {"),
    ("the keyframe search overshoots the target", NAVIGATION,
     "if ( keyframes[i].gameTime > targetTime ) { break; }", "if ( false ) { break; }"),
    ("a joining player reports powerups they already had", MARKERS,
     "if ( !current.present[ client ] || !previous.present[ client ] ) {", "if ( !current.present[ client ] ) {"),
    ("free-for-all frags are reported as team scores", MARKERS,
     "\tif ( teamGame ) {\n", "\tif ( true ) {\n"),
    ("action tracking ignores the dwell", AUTOTRACK,
     "now - lastSwitchTime >= MP_AUTOTRACK_DWELL_MS &&", ""),
    ("killer tracking follows a dead killer", AUTOTRACK,
     "players[target].alive && target != followed", "target != followed"),
    ("action mode leaves a killer switch pending", AUTOTRACK,
     "\t\tpendingTarget = -1;\n\t\tif ( best != followed", "\t\tif ( best != followed"),
    ("a stale killer switch still fires", AUTOTRACK,
     "if ( fresh && candidate( target )", "if ( candidate( target )"),
    ("a suicide schedules a killer switch", AUTOTRACK,
     "attacker >= MP_AUTOTRACK_CLIENTS || attacker == victim )", "attacker >= MP_AUTOTRACK_CLIENTS )"),
)

SOURCE_CONTRACTS = (
    ("src/framework/async/MultiViewDemo.cpp", (
        "static const unsigned short MVD_FORMAT_MINOR = 3;",
        "MVD_OPTIONAL_USERINFO | MVD_OPTIONAL_MARKERS | MVD_OPTIONAL_KEYFRAMES;",
        "clean = WriteIndexRecord() && WriteNavigationRecords();",
        "game->ServerWriteServerDemoBaseline( snapshotSequence, &baseline )",
        "!game->ClientReadServerDemoBaseline( keyframeHeader.sequence, &baseline ) ||",
        "positioned = JumpToKeyframe( keyframeIndex, worldReset );",
        "ok = WriteIndexRecord() && WriteNavigationRecords();",
        "NoteNavigationRecord( record, outputOffset );",
        "!ParseNavigationTable( record, playbackStreamOffset, indexRecordOffset ) ) {",
        "if ( !ApplyUserInfoRecord( record ) ) {",
        'cmdSystem->AddCommand( "mvdMark", Mark_f',
        'cmdSystem->AddCommand( "mvdMarker", Marker_f',
        'cmdSystem->AddCommand( "mvdAutoTrack", AutoTrack_f',
        "if ( gameFollowClient >= 0 ) {",
        # A cheat-free server's synced cvars must not lock the viewer's controls.
        "\"mvd_scale\", \"1\",\n\tCVAR_SYSTEM | CVAR_FLOAT | CVAR_NOCHEAT,",
        "\"mvd_paused\", \"0\",\n\tCVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT,",
    )),
    ("src/framework/async/AsyncServer.cpp", (
        "idAsyncNetwork::multiViewDemo.CaptureUserInfo( userInfoNum, *gameInfo );",
    )),
    ("src/framework/async/NetworkSystem.cpp", (
        "idAsyncNetwork::multiViewDemo.RecordMarker( marker );",
    )),
    ("src/game/Game.h", (
        "virtual bool				ServerWriteServerDemoBaseline( int sequence, idFile *file ) = 0;",
        "virtual bool				ClientReadServerDemoBaseline( int sequence, idFile *file ) = 0;",
    )),
    ("src/mpgame/Game_network.cpp", (
        "bool idGameLocal::ServerWriteServerDemoBaseline( int sequence, idFile *file ) {",
        "if ( pending == NULL || pending->next != NULL || pending->sequence != sequence ) {",
        "bool idGameLocal::ClientReadServerDemoBaseline( int sequence, idFile *file ) {",
        "UpdateDemoAutoTrack( player, clientCmds[ clientNum ] );",
        "demoAutoTrack.NoteKill( attackerEntityNumber, victimEntityNumber, GetDemoFollowClient(), time );",
    )),
    ("src/mpgame/MultiplayerGame.cpp", (
        "PublishMVDMarkers();",
        "const int count = MPMVDMarkerDiff( mvdMarkerState, current, gameLocal.IsTeamGame(), tracked, events, 64 );",
        "marker.kind = SERVER_MVD_MARKER_KILL;",
    )),
    ("src/framework/Session_demo.cpp", (
        'cmdSystem->AddCommand( "demoMarker", Session_DemoMarker_f',
        'guiDemoMenu->SetStateBool( "demo_canMarker",',
        '"demo_uiSeek", "0",\n\tCVAR_SYSTEM | CVAR_GUI | CVAR_FLOAT | CVAR_NOCHEAT,',
    )),
    ("content/baseoq4/pak0/guis/demo_menu.gui", (
        'set "cmd" "play main_menu_selection ; demoMarkerPrev" ;',
        'set "cmd" "play main_menu_selection ; demoMarkerNext" ;',
        'set "cmd" "play main_menu_selection ; demoAutoTrack" ;',
    )),
)


def compile_and_run(include_root: Path, directory: Path, name: str, compiler: str,
                    strict: bool = True) -> subprocess.CompletedProcess:
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    # Production headers must build warning-free; a mutation may leave a parameter unused.
    command = [compiler, "-std=c++17", f"-I{include_root}", "-Wall", "-Wextra"] + (["-Werror"] if strict else []) + [
               str(ROOT / "tools/tests/native/MvdNavigationFixture.cpp"), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True, capture_output=not strict)
    return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)


def main() -> None:
    for relative, tokens in SOURCE_CONTRACTS:
        text = (ROOT / relative).read_text(encoding="utf-8")
        for token in tokens:
            if token not in text:
                raise SystemExit(f"mvd_navigation_contract: {relative} is missing {token!r}")

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the MVD navigation fixture")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mvd-navigation-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(ROOT, path, "navigation", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mvd_navigation_contract: FAILED\n{result.stdout}{result.stderr}")
        summary = result.stdout.strip()

        for index, (label, header, original, mutated) in enumerate(MUTATIONS):
            # Unix executables have no .exe suffix; keep the include overlay
            # distinct from the control binary's path on every platform.
            mirror = path / f"overlay{index}"
            for relative in (NAVIGATION, MARKERS, AUTOTRACK):
                target = mirror / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                text = (ROOT / relative).read_text(encoding="utf-8")
                if relative == header:
                    if text.count(original) != 1:
                        raise SystemExit(f"mvd_navigation_contract: mutation anchor not unique: {label}")
                    text = text.replace(original, mutated, 1)
                target.write_text(text, encoding="utf-8")
            try:
                control = compile_and_run(mirror, path, f"control{index}", compiler, strict=False)
                failed = control.returncode != 0
            except subprocess.CalledProcessError:
                failed = False  # a control must compile and then fail at run time
            if not failed:
                raise SystemExit(f"mvd_navigation_contract: control did not fail: {label}")

    print(f"mvd_navigation_contract: ok - {summary}; {len(MUTATIONS)} single-reversal controls fail")


if __name__ == "__main__":
    main()
