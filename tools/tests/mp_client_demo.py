#!/usr/bin/env python3
"""Pin a player's recording of their own view (MP-22) without a runtime.

A client-view recording is an MVD whose header requires MVD_FEATURE_CLIENT_VIEW,
so a reader without it refuses the file. The client records what it gave its
game (every snapshot message, applied snapshot, game reliable and user info) and
its own commands; playback runs the game as the recorder's client slot and
repeats idAsyncClient's prediction. mp_client_demo_smoke.py plays it for real.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unbalanced body for {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_container() -> None:
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    require("MVD_FEATURE_CLIENT_VIEW			= 1u << 6" in mvd, "the client view is a required feature bit of its own")
    client_required = re.search(r"MVD_CLIENT_VIEW_REQUIRED_FEATURES =\s*([^;]+);", mvd)
    require(client_required is not None and "MVD_FEATURE_CLIENT_VIEW" in client_required[1] and
            "SERVER_SNAPSHOTS" not in client_required[1], "a client-view recording claims no server-view feature")
    require("header.requiredFeatures = asClient ? MVD_CLIENT_VIEW_REQUIRED_FEATURES : MVD_SERVER_VIEW_REQUIRED_FEATURES;" in mvd,
            "a server recording keeps the requirements it always wrote")
    header = body(mvd, "bool idMultiViewDemo::ReadHeader( idFile *source, header_t &outHeader ) {")
    require("( outHeader.requiredFeatures & ~MVD_CLIENT_VIEW_REQUIRED_FEATURES ) != 0" in header,
            "a header that mixes the two views is refused")
    for name, value in (("CLIENT_SNAPSHOT", 30), ("CLIENT_APPLY", 31), ("CLIENT_RELIABLE", 32), ("CLIENT_USERCMD", 33)):
        require(re.search(rf"MVD_RECORD_{name}\s*=\s*{value},", mvd) is not None, f"record type {name} is {value}")
    process = body(mvd, "bool idMultiViewDemo::ProcessPlaybackRecord( const record_t &record, bool &finished ) {")
    require("MVD_IsClientRecord( record.type )" in process and "does not belong to this recording's view" in process,
            "each view refuses the other's snapshot and reliable records")
    index = body(mvd, "bool idMultiViewDemo::BuildPlaybackIndex() {")
    require("MVD_ScanClientRecord(" in index, "the playback index validates client records and counts them for the end record")
    query = body(mvd, "bool idMultiViewDemo::QueryFileInfo( const char *name, mvdFileInfo_t &info ) {")
    require(query.count("ValidateClientNetworkState(") == 2 and "MVD_ScanClientRecord(" in query,
            "the library validates a client-view recording as playback does")


def check_recording() -> None:
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    client = read("src/framework/async/AsyncClient.cpp")
    start = body(mvd, "bool idMultiViewDemo::StartRecording( const idCmdArgs &args ) {")
    require("!idAsyncNetwork::server.IsActive() && idAsyncNetwork::client.IsInGame()" in start,
            "a player on a server they joined records their own view")
    require("asClient ? WriteClientNetworkStateRecord() : WriteNetworkStateRecord()" in start and
            "if ( !asClient ) {\n\t\tgame->SetDemoState( DEMO_RECORDING, true, false );" in start,
            "the client writes its own start state and leaves the game's demo state alone")
    network = body(mvd, "bool idMultiViewDemo::BuildClientNetworkState( idFile_Memory &payload ) {")
    require("game->WriteNetworkInfo( &payload, viewClient );" in network and "ValidateClientNetworkState(" in network,
            "the client's start state is the game's client network info, checked before it is written")
    stamp = body(mvd, "void idMultiViewDemo::ClientStamp( int &frame, int &time ) const {")
    require("Sys_Milliseconds() - recordingStartRealTime" in stamp, "records are stamped with real time, which only moves forward")
    for hook, where in (
        ("idAsyncNetwork::multiViewDemo.CaptureClientSnapshot( msg, messageStart, gameFrame );", "void idAsyncClient::ProcessUnreliableServerMessage( const idBitMsg &msg ) {"),
        ("idAsyncNetwork::multiViewDemo.CaptureClientApplySnapshot( sequence );", "void idAsyncClient::ProcessReliableServerMessages( void ) {"),
        ("idAsyncNetwork::multiViewDemo.CaptureClientReliable( msg );\n\t\t\t\t\tgame->ClientProcessReliableMessage( clientNum, msg );", "void idAsyncClient::ProcessReliableServerMessages( void ) {"),
        ("idAsyncNetwork::multiViewDemo.CaptureClientUserInfo( clientNum, info );", "void idAsyncClient::ProcessReliableServerMessages( void ) {"),
        ("idAsyncNetwork::multiViewDemo.CaptureClientUsercmd( userCmds[index][clientNum] );", "void idAsyncClient::SendUsercmdsToServer( void ) {"),
        ("idAsyncNetwork::multiViewDemo.OnClientGameEnded( \"disconnected\" );", "void idAsyncClient::DisconnectFromServer( void ) {"),
        ("idAsyncNetwork::multiViewDemo.OnClientGameEnded( \"the server changed map\" );", "void idAsyncClient::ProcessUnreliableServerMessage( const idBitMsg &msg ) {"),
    ):
        require(hook in body(client, where), f"the client hook is in place: {hook.split('(')[0]}")
    unreliable = body(client, "void idAsyncClient::ProcessUnreliableServerMessage( const idBitMsg &msg ) {")
    require(unreliable.index("const int messageStart = msg.GetReadCount();") < unreliable.index("serverGameInitId = msg.ReadLong();"),
            "the snapshot is recorded from the start of the message, after the channel's header")
    exists = body(mvd, "static bool MVD_PathExists( const idStr &path ) {")
    require('std::filesystem::exists( std::filesystem::u8path(\n\t\tfileSystem->RelativePathToOSPath( path.c_str(), "fs_savepath" ) ), error )' in exists,
            "a free recording name is checked in the save folder, which a pure server's file system hides from its clients")
    commands = body(mvd, "void idMultiViewDemo::Init() {")
    require('AddCommand( "record", Record_f' in commands and 'AddCommand( "stopRecord", Stop_f' in commands,
            "record and stopRecord are the short commands")


def check_playback() -> None:
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    client = read("src/framework/async/AsyncClient.cpp")
    apply_state = body(mvd, "bool idMultiViewDemo::ApplyPlaybackNetworkState( const record_t &networkState, const char *context ) {")
    require("game->SetDemoState( DEMO_PLAYING, !clientView, false );" in apply_state and
            "clientView ? viewClient : MAX_ASYNC_CLIENTS" in apply_state,
            "a client-view recording plays as its recorder's client, a server recording as the demo spectator")
    start = body(mvd, "bool idMultiViewDemo::StartPlayback( const idCmdArgs &args ) {")
    require(start.index("ValidateClientNetworkState(") < start.index("ApplyPlaybackNetworkState( networkState"),
            "the whole client start state is validated before the game reads any of it")
    prediction = body(mvd, "void idMultiViewDemo::RunClientPrediction() {")
    require("idAsyncNetwork::DuplicateUsercmd(" in prediction and
            "game->ClientPrediction( viewClient, playbackUsercmds[ current ], last );" in prediction,
            "playback repeats the client's prediction with the commands it had")
    records = body(mvd, "bool idMultiViewDemo::ProcessClientPlaybackRecord( const record_t &record, bool &handled ) {")
    require("game->ClientReadSnapshot( viewClient, sequence, gameFrame, gameTime, duplicated, aheadOfServer, msg )" in records and
            "game->ClientApplySnapshot( viewClient, sequence )" in records and
            "game->ClientProcessReliableMessage( viewClient, msg );" in records,
            "the records reach the game as the connected client gave them")
    require("idAsyncNetwork::multiViewDemo.GetPlaybackLocalClient()" in body(client, "int idAsyncClient::GetLocalClientNum( void ) const {"),
            "the engine's local client during playback is the recorder")
    for signature in ("void idMultiViewDemo::FollowNext() {", "void idMultiViewDemo::FollowPrev() {",
                      "bool idMultiViewDemo::FollowClient( int clientNum ) {", "void idMultiViewDemo::FreeRoam() {"):
        require("MVD_ClientViewCamera( clientView )" in body(mvd, signature), f"{signature.split('(')[0]} keeps the recorder's view")
    session = read("src/framework/Session_demo.cpp")
    require(session.count("~( DEMO_CAP_FREE_ROAM | DEMO_CAP_FOLLOW | DEMO_CAP_FULL_WORLD )") == 2,
            "the library and the deck offer a client-view recording no camera controls")


def check_navigation_and_tools() -> None:
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    session = read("src/framework/Session.cpp")
    common = read("src/framework/Common.cpp")
    snapshot = body(mvd, "void idMultiViewDemo::CaptureClientSnapshot( const idBitMsg &msg, int messageStart, int clientGameFrame ) {")
    require("WriteClientKeyframe( frame, time );" in snapshot and "mvd_keyframeInterval.GetInteger()" in snapshot,
            "a client recording writes a keyframe after a snapshot every mvd_keyframeInterval seconds")
    keyframe = body(mvd, "bool idMultiViewDemo::WriteClientKeyframe( int gameFrame, int gameTime ) {")
    require("BuildClientNetworkState( networkState )" in keyframe and "MVD_RECORD_CLIENT_KEYFRAME" in keyframe,
            "a client keyframe holds the client network state in a record of its own type")
    jump = body(mvd, "bool idMultiViewDemo::JumpToClientKeyframe( int index, bool &worldReset ) {")
    require(jump.index("ValidateClientNetworkState(") < jump.index("session->Stop();") and
            'ApplyPlaybackNetworkState( networkState, "keyframe" )' in jump,
            "a client keyframe is validated before the world is reset and restored as the start state is")
    require("return JumpToClientKeyframe( index, worldReset );" in body(mvd, "bool idMultiViewDemo::JumpToKeyframe( int index, bool &worldReset ) {"),
            "seeking a client recording uses its keyframes")
    recover = body(mvd, "bool idMultiViewDemo::Recover( const idCmdArgs &args ) {")
    require("record.type == MVD_RECORD_CLIENT_SNAPSHOT" in recover, "mvdRecover counts a client recording's snapshots")
    frame = body(mvd, "void idMultiViewDemo::RunPlaybackFrame() {")
    require("elapsed = Max( 1, common->GetUserCmdMSec() );" in frame, "a timedemo frame is one simulation tic")
    require("StartTimeDemo( args.Argv( 1 ), false )" in session and "StartTimeDemo( args.Argv( 1 ), true )" in session,
            "timeDemo and timeDemoQuit take a .mvd")
    require("idAsyncNetwork::multiViewDemo.IsTimeDemo()" in body(common, "static int Common_GetRequestedPresentationCap( void ) {"),
            "a timedemo is not frame-capped")
    require('metadata.Set( "packs", packList );' in mvd and "MVD_PackDifference(" in body(mvd, "bool idMultiViewDemo::StartPlayback( const idCmdArgs &args ) {"),
            "a recording keeps its packs and a refusal for other game data names the pack that differs")


def check_deck_and_library() -> None:
    mvd = read("src/framework/async/MultiViewDemo.cpp")
    session = read("src/framework/Session_demo.cpp")
    menu = read("src/framework/Session_menu.cpp")
    gui = read("content/baseoq4/pak0/guis/demo_menu.gui")
    english = read("content/baseoq4/pak0/strings/english_openq4.lang")
    require('set "cmd" "play main_menu_selection ; demoPlayers" ;' in gui and gui.count("demoFollowRow ") == 16 and
            'text		"gui::demo_player15_name"' in gui, "the deck lists up to 16 recorded players to follow by name")
    require('set "cmd" "play main_menu_selection ; demoStep -1" ;' in gui, "the deck steps back a frame")
    for key in ("#str_232840", "#str_232841", "#str_232842", "#str_232843", "#str_232844"):
        require(f'"{key}"' in english, f"{key} is in the language table")
    require("demoPlayerRowSlots[ row ]" in session and "FollowClient(" in session, "a row follows its recorded slot")
    require("const int step = ( frames == 0 ? 1 : frames ) * Max( 1, common->GetUserCmdMSec() );" in
            body(mvd, "void idMultiViewDemo::StepFrames( int frames ) {"), "a step is whole tics, back as well as on")
    export = body(mvd, "void idMultiViewDemo::RunPlaybackFrame() {")
    require('screenshot \\"%s/frame%05d.png\\"' in export and "elapsed = exportStepMS;" in export,
            "an export writes a PNG frame each 1000/fps of the match")
    require('AddCommand( "demoExport", Export_f' in mvd, "demoExport is a command")
    require("ClientStamp( markerFrame, marker.gameTime );" in body(mvd, "bool idMultiViewDemo::RecordMarker( const serverMVDMarker_t &input ) {"),
            "a client recording stamps markers on its own clock")
    require("idAsyncNetwork::multiViewDemo.IsClientRecording()" in read("src/framework/async/NetworkSystem.cpp"),
            "the game's markers reach a client recording")
    require("PublishMVDMarkers();" in body(read("src/mpgame/MultiplayerGame.cpp"), "void idMultiplayerGame::ClientRun( void ) {"),
            "a client's game finds the match's changes for its recording")
    require("SERVER_MVD_MARKER_KILL" in read("src/mpgame/Game_network.cpp").split("case GAME_RELIABLE_MESSAGE_DEATH")[1][:3000],
            "a client recording marks each kill it hears of")
    require("WriteSummaryRecord()" in body(mvd, "bool idMultiViewDemo::StopRecording( const char *reason, bool finalize,") and "MVD_RECORD_SUMMARY" in body(mvd, "bool idMultiViewDemo::WriteSummaryRecord() {"),
            "a finished recording names everyone seen in its summary")
    require('AddCommand( "demoFind", Session_DemoFind_f' in session and "#str_232844" in session and "#str_232843" in session,
            "demoFind searches the library, which names the players or whose view a demo is")
    require('"screenshot \\"%s\\" 320 240\\n"' in body(mvd, "void idMultiViewDemo::ServiceThumbnail() {"),
            "a recording made where there is a view takes a 320x240 thumbnail")
    require('thumbnail += "_thumb";' in menu, "the library shows a demo's own thumbnail")
    require("!idStr::IsNumeric( args.Argv( 1 ) )" in read("src/renderer/RenderSystem_init.cpp"),
            "screenshot takes a file name with a size")


def main() -> int:
    try:
        check_deck_and_library()
        check_container()
        check_recording()
        check_playback()
        check_navigation_and_tools()
    except (AssertionError, ValueError) as error:
        print(f"mp_client_demo: FAIL: {error}")
        return 1
    print("mp_client_demo: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
