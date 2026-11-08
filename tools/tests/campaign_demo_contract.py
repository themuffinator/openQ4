#!/usr/bin/env python3
"""Structural contracts for campaign render demos (render demo v11 and v12).

A campaign recording spans every single-player level it reaches as
self-contained segments, records only gameplay frames, and ends with a raw
summary trailer. Playback loads each segment's level, keeps one timeline and
offers the level starts as markers. v12 predicts animated entities' joints
from the joints last recorded for the same handle. These checks pin the
ordering and gating that keep a long recording decodable and its library
entry honest.
"""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
LANGUAGES = (
    "english", "german", "french", "spanish", "italian", "russian", "polish",
    "czech", "hungarian", "turkish", "ukrainian", "brazilian",
)
CAMPAIGN_STRINGS = range(41576, 41586)


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


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    position = -1
    for needle in needles:
        found = text.find(needle, position + 1)
        if found < 0:
            raise AssertionError(f"Missing {needle!r} after position {position} in {context}")
        position = found


def validate_format() -> None:
    session_h = read("src/framework/Session.h")
    require(session_h, "OPENQ4_RENDERDEMO_CURRENT_VERSION = 12;", "current render-demo version")
    require(session_h, "OPENQ4_RENDERDEMO_SESSION_VERSION = 11;", "session-record version")
    require(session_h, "OPENQ4_RENDERDEMO_JOINT_PREDICTION_VERSION = 12;", "joint-prediction version")

    demo_h = read("src/framework/DemoFile.h")
    require_order(demo_h, ("DS_FINISHED,", "DS_RENDER,", "DS_SOUND,", "DS_VERSION,", "DS_SESSION"),
                  "stable top-level demo tokens")
    require_order(demo_h, ("DSC_HEADER = 1,", "DSC_SEGMENT,", "DSC_SEGMENT_READY,", "DSC_END"),
                  "session record kinds")
    require(demo_h, '#define DEMO_TRAILER_MAGIC\t\t\t"OQ4DSUM1"', "summary trailer magic")
    for needle in ("void\t\t\tResetHashStrings();", "bool\t\t\tCloseWithTrailer( const idDict &summary );",
                   "static bool\t\tReadTrailer( const char *fileName, idDict &summary );",
                   "idHashIndex\t\tdemoStringHash;"):
        require(demo_h, needle, "demo file campaign API")

    demo_cpp = read("src/framework/DemoFile.cpp")
    trailer = between(demo_cpp, "bool idDemoFile::CloseWithTrailer", "bool idDemoFile::ReadTrailer", "trailer writer")
    require_order(trailer, ("compressor->FinishCompress();", "CRC32_BlockChecksum", "f->Write( payload.GetDataPtr(), length )",
                            "f->WriteInt( length )", "f->WriteUnsignedInt( crc )", "DEMO_TRAILER_MAGIC"),
                  "raw trailer layout after the compressed stream")
    reader = between(demo_cpp, "bool idDemoFile::ReadTrailer", "const char *idDemoFile::ReadHashString", "trailer reader")
    for needle in ("memcmp( magic, DEMO_TRAILER_MAGIC, DEMO_TRAILER_MAGIC_LENGTH ) == 0",
                   "length <= DEMO_TRAILER_MAX_PAYLOAD",
                   "CRC32_BlockChecksum( payload.Ptr(), length ) == crc",
                   "count > MAX_DEMO_DICT_ENTRIES"):
        require(reader, needle, "bounded, checksummed trailer reader")
    writer = between(demo_cpp, "void idDemoFile::WriteHashString", "void idDemoFile::ReadDict", "hash-string writer")
    require(writer, "demoStringHash.First( key )", "hashed string lookup")
    require(writer, "demoStringHash.Add( key, demoStrings.Num() );", "hash index kept in step")
    reject(writer, "for ( int i = 0 ; i < demoStrings.Num() ; i++ )", "linear hash-string search")
    reset = between(demo_cpp, "void idDemoFile::ResetHashStrings", "bool idDemoFile::CloseWithTrailer", "hash reset")
    require(reset, "demoStrings.DeleteContents( true );", "string table reset")
    require(reset, "demoStringHash.Clear();", "string index reset")


def validate_recorder() -> None:
    session = read("src/framework/Session.cpp")
    demo = read("src/framework/Session_demo.cpp")
    render_system = read("src/renderer/RenderSystem.cpp")

    unload = between(session, "void idSessionLocal::UnloadMap() {", "static void Session_BuildLevelLoadCacheSettings", "UnloadMap")
    require_order(unload, ("StopPlayingRenderDemo();", "renderDemoRecordState == RENDER_DEMO_RECORD_RECORDING",
                           "idAsyncNetwork::IsActive()", "StopRecordingRenderDemo();", "SuspendRenderDemoRecording();",
                           "game->MapShutdown();"),
                  "SP recordings suspend before teardown; MP recordings keep one map")
    reject(unload, "if ( writeDemo ) {\n\t\tStopRecordingRenderDemo();", "historical stop at every unload")

    stop = between(session, "void idSessionLocal::StopInternal( bool preserveWipe ) {", "idSessionLocal::Shutdown", "StopInternal")
    require_order(stop, ("UnloadMap();", "renderDemoRecordState == RENDER_DEMO_RECORD_SUSPENDED", "StopRecordingRenderDemo();"),
                  "leaving the game ends a campaign recording")
    shutdown = between(session, "void idSessionLocal::Shutdown() {", "SetPlayingSoundWorld( NULL );", "Shutdown")
    require_order(shutdown, ("Stop();", "IsRenderDemoRecording()", "StopRecordingRenderDemo();"),
                  "quitting finalizes an armed recording")

    execute = between(session, "void idSessionLocal::ExecuteMapChange( bool noFadeWipe ) {", "LoadGame_f", "ExecuteMapChange")
    require_order(execute, ('renderDemoNextSegmentReason = "start";', 'renderDemoNextSegmentReason = "load";',
                            'renderDemoNextSegmentReason = "level";', "UnloadMap();", "mapSpawned = true;",
                            "ContinueRenderDemoAfterMapChange();"),
                  "each spawned level resumes the recording as a segment")
    require(session, 'renderDemoNextSegmentReason = "restart";', "difficulty restart segment reason")

    draw = between(session, "} else if ( mapSpawned ) {", "guiRetainedReleasing->Redraw( presentationTime );", "gameplay draw")
    require_order(draw, ("gameDraw = game->Draw( GetLocalClientNum() );", "if ( writeDemo ) {",
                         "renderSystem->WriteDemoPics();", "renderSystem->WriteDemoEndFrame();", "renderDemoRecordFrames++;"),
                  "only gameplay draws end recorded frames")
    end_frame = between(render_system, "void idRenderSystemLocal::EndFrame", "void idRenderSystemLocal::", "EndFrame")
    reject(end_frame, "DC_END_FRAME", "EndFrame frame terminator")
    require(render_system, "void idRenderSystemLocal::WriteDemoEndFrame() {", "session-driven frame terminator")
    require(session, "if ( timeHitch && writeDemo ) {", "recording never skips tics under its fixed-tic check")

    start = between(session, "void idSessionLocal::StartRecordingRenderDemo", "idSessionLocal::OpenRenderDemoRecordFile", "start recording")
    require_order(start, ('BeginRenderDemoSegment( "start" );', "RENDER_DEMO_RECORD_ARMED"),
                  "the current level starts the recording, or it arms for the next one")
    reject(start, "OpenForWriting", "an armed recording opens no file")
    open_file = between(session, "bool idSessionLocal::OpenRenderDemoRecordFile", "idSessionLocal::StopRecordingRenderDemo", "open file")
    require_order(open_file, ("OpenForWriting", "DS_VERSION", "OPENQ4_RENDERDEMO_CURRENT_VERSION",
                              'header.Set( "game", fileSystem->GetActiveGameDir() );',
                              'header.SetInt( "userCmdHz", common->GetUserCmdHz() );', "DSC_HEADER", "WriteDict( header );"),
                  "the stream starts with its version and header")
    stop_rec = between(session, "void idSessionLocal::StopRecordingRenderDemo", "idSessionLocal::StopPlayingRenderDemo", "stop recording")
    require_order(stop_rec, ("renderDemoRecordFile == NULL", "before any level loaded", "fileSystem->RemoveFile( path.c_str() );",
                             "DSC_END", 'summary.SetInt( "frames"', 'va( "segment%d", i )', "CloseWithTrailer( summary )"),
                  "an armed recording leaves no file; clean ones end with DSC_END and the trailer")

    segment = between(demo, "void idSessionLocal::BeginRenderDemoSegment", "void idSessionLocal::SuspendRenderDemoRecording", "segment writer")
    require_order(segment, ("renderDemoRecordFile == NULL && !OpenRenderDemoRecordFile()",
                            "DSC_SEGMENT );", "WriteDict( record );", "ResetHashStrings();", "writeDemo = renderDemoRecordFile;",
                            "sw->StartWritingDemo( writeDemo );", "rw->StartWritingDemo( writeDemo );", "DSC_SEGMENT_READY"),
                  "a segment stands alone: fresh strings, full sound and world state")
    cont = between(demo, "void idSessionLocal::ContinueRenderDemoAfterMapChange", "void idSessionLocal::PrintRenderDemoRecordStatus", "continue")
    require_order(cont, ("IsMultiplayer()", "multiViewDemo.IsPlaying()", "StopRecordingRenderDemo();", "BeginRenderDemoSegment("),
                  "only single-player levels continue a campaign recording")
    require(demo, 'AddCommand( "demoRecordStatus"', "recording status command")


def validate_player() -> None:
    session = read("src/framework/Session.cpp")
    demo = read("src/framework/Session_demo.cpp")

    advance = between(session, "void idSessionLocal::AdvanceRenderDemo( bool singleFrameOnly ) {", "idSessionLocal::Draw", "AdvanceRenderDemo")
    require(advance, "ds == DS_SESSION && renderdemoVersion >= OPENQ4_RENDERDEMO_SESSION_VERSION", "version-gated session records")
    require(advance, "malformed session record; playback stopped safely", "nonfatal session records")
    require(advance, "RenderDemoFramesToMS( numDemoFrames - 1 )", "frame-clock duration")

    start = between(session, "void idSessionLocal::StartPlayingRenderDemo", "idSessionLocal::TimeRenderDemo", "start playback")
    require_order(start, ("Session_RenderDemoContentMatches( demoName.c_str(), refusal )", "return;",
                          "StopRecordingRenderDemo();", "Stop();", "ResetRenderDemoPlaybackSession();"),
                  "the campaign gate refuses before anything is torn down")

    records = between(demo, "bool idSessionLocal::ProcessRenderDemoSessionRecord", "bool idSessionLocal::SeekRenderDemoSegment", "session records")
    require(records, "userCmdHz < 10 || userCmdHz > 1000", "bounded tic rate")
    require(records, "renderDemoFrameClock = true;", "frame-clock timeline")
    require_order(records, ("case DSC_SEGMENT:", "readDemo->ResetHashStrings();", 'dict.GetInt( "frame", "-1" ) != numDemoFrames',
                            "CloseRenderDemoLevelLoad();", "sw->StopAllSounds();", "declManager->BeginLevelLoad();",
                            "renderSystem->BeginLevelLoad();", "soundSystem->BeginLevelLoad();", "renderDemoLevelLoadOpen = true;"),
                  "segments reset strings, check frame counts and open the level's media load")
    require_order(records, ("case DSC_SEGMENT_READY:", "CloseRenderDemoLevelLoad();", "case DSC_END:", "finished = true;"),
                  "ready closes the media load; the end stops reading before the trailer")
    close = between(demo, "void idSessionLocal::CloseRenderDemoLevelLoad", "bool idSessionLocal::ProcessRenderDemoSessionRecord", "close load")
    require_order(close, ("renderSystem->EndLevelLoad();", "soundSystem->EndLevelLoad(", "declManager->EndLevelLoad();",
                          "bse->EndLevelLoad();"),
                  "purge order matches ExecuteMapChange")

    sound = read("src/sound/snd_world.cpp")
    resolver = between(sound, "idSoundEmitter* idSoundWorldLocal::DemoEmitterForIndex", "idSoundEmitter* idSoundWorldLocal::EmitterForIndex", "demo emitters")
    require(resolver, "return emitters.Num() > 0 ? emitters[0] : NULL;", "demo slot zero is localSound")
    sound_commands = between(sound, "bool idSoundWorldLocal::ProcessDemoCommand", "idSoundWorldLocal::AVIOpen", "sound demo reader")
    if sound_commands.count("DemoEmitterForIndex( index )") != 5:
        raise AssertionError("update, start, modify, stop and fade must resolve demo emitters, localSound included")
    require(between(sound_commands, "case SCMD_FREE:", "case SCMD_UPDATE:", "free"), "EmitterForIndex( index )",
            "the never-freed localSound stays outside SCMD_FREE")

    time = between(demo, "static int DemoCurrentRenderTimeMS", "struct demoRenderSessionInfo_t", "timeline")
    require(time, "session.RenderDemoFramesToMS( Max( 0, session.numDemoFrames - 1 ) )", "monotonic campaign timeline")
    require(demo, "static_cast<int64>( frames ) * USERCMD_PER_DEMO_FRAME * 1000 / userCmdHz", "frame duration")

    markers = between(demo, "bool idSessionLocal::SeekRenderDemoSegment", "idStr idSessionLocal::RenderDemoSegmentLabel", "markers")
    require(markers, "RESTART_LEVEL_WINDOW_MS", "previous level restarts the current one first")
    require(markers, "return SeekDemoMS( target );", "markers seek through the shared controller")
    require(demo, "renderPlaying && renderDemoFrameClock && renderDemoPlaySegments.Num() > 1", "level marker buttons")
    require(demo, "SeekRenderDemoSegment( !idStr::Icmp( cmd, \"demoMarkerNext\" ) ? 1 : -1 );", "menu level markers")


def validate_library() -> None:
    demo = read("src/framework/Session_demo.cpp")
    probe = between(demo, "static void DemoProbeEntry", "static void Session_CycleDemoAutoTrack", "library probe")
    require(probe, "streamVersion >= OPENQ4_RENDERDEMO_SESSION_VERSION", "campaign probe version gate")
    require(probe, 'entry.format = DemoLocalized( "#str_41576" );', "campaign demo kind")
    require_order(probe, ("sessLocal.IsRenderDemoRecording() && !recordingPath.Icmp( entry.path )",
                          'DemoLocalized( "#str_41581" )', "idStr::Icmp( recordedGame, fileSystem->GetActiveGameDir() )",
                          'DemoLocalized( "#str_41577" )', 'DemoLocalized( "#str_41578" )', "!info.summarized",
                          'DemoLocalized( "#str_41579" )'),
                  "recording, other-campaign and interrupted entries are described honestly")
    require_order(probe, ("const bool beingRecorded = sessLocal.IsRenderDemoRecording()", "if ( DemoEntryCanDelete( entry.path ) ) {",
                          "if ( !beingRecorded ) {", "entry.capabilities |= DEMO_CAP_DELETE;"),
                  "the file being recorded is never offered for deletion")
    info = between(demo, "static bool DemoReadRenderSessionInfo", "static idStr DemoCampaignName", "session info probe")
    require(info, "probe.OpenForReading( path, false, true )", "quiet bounded probe")
    require(info, "idDemoFile::ReadTrailer( path, info.summary )", "trailer-backed duration")


def validate_renderer() -> None:
    tr_local = read("src/renderer/tr_local.h")
    require_order(tr_local, ("DC_UPDATE_EFFECTDEF,", "DC_STOP_EFFECTDEF,", "DC_DELETE_EFFECTDEF,", "DC_GUI_SURFACE"),
                  "render-demo commands only append")

    gui = read("src/renderer/GuiModel.cpp")
    excluded = between(gui, "static bool R_GuiDemoSurfaceIsExcluded", "static int R_DemoRetainedLayerSlot", "2D exclusion")
    require(excluded, "material->GetSort() >= SS_POST_PROCESS", "scene composition stays out of recorded 2D")
    for prefix in ('"_retainedAtlas/"', '"_retainedLayer/"', '"_retainedMask/"', '"_retainedGenerated/"', '"_retainedBlur/"'):
        require(excluded, prefix, "runtime-only retained images")
    emit = between(gui, "void idGuiModel::EmitFullScreen( void ) {", "viewDef = (viewDef_t *)R_ClearedFrameAlloc", "EmitFullScreen")
    require(emit, "R_DemoGuiTarget()", "every flushed screen batch is recorded")
    append = between(gui, "void idGuiModel::AppendForDemoScaled", "bool idGuiModel::IsEmpty", "layer splice")
    require_order(append, ('"_retainedLayer/"', "AppendForDemoScaled( *content, layerOpacity );", "R_GuiDemoSurfaceIsExcluded"),
                  "retained layer composites record as their content")
    writer = between(gui, "void idGuiModel::WriteToDemo( idDemoFile *demo, const idGuiModel *previous )", "bool idGuiModel::ReadFromDemo", "writer")
    require(writer, "demo->WriteUnsignedChar( 0 );\t\t// as the previous frame drew it", "unchanged surfaces cost one byte")
    reader = between(gui, "bool idGuiModel::ReadSurfacesFromDemo", "void idGuiModel::Swap", "v11 reader")
    for needle in ("repeats a surface the previous frame did not have", "invalid vertex layout",
                   "index < 0 || index >= numVerts", "( 1 << 20 ) - verts.Num()"):
        require(reader, needle, "bounded v11 2D reader")

    render_system = read("src/renderer/RenderSystem.cpp")
    pics = between(render_system, "void idRenderSystemLocal::WriteDemoPics() {", "void idRenderSystemLocal::WriteDemoEndFrame", "WriteDemoPics")
    require_order(pics, ("demoLastGuiModelValid ? demoLastGuiModel : NULL", "demoLastGuiModel->Swap( *demoFrameGuiModel );"),
                  "frames compare against the frame written before")
    clear = between(render_system, "void idRenderSystemLocal::ClearRenderTarget", "idRenderSystemLocal::ResolveMSAA", "clear")
    require(clear, "layerContent->Clear();", "recorded layer content resets with its target")
    world_demo = read("src/renderer/RenderWorld_demo.cpp")
    start = between(world_demo, "void\t\tidRenderWorldLocal::StartWritingDemo", "void idRenderWorldLocal::StopWritingDemo", "segment start")
    require(start, "demoGuiSurfaceChecksums.Clear();", "in-world GUIs rewritten per segment")
    require(start, "tr.demoLastGuiModelValid = false;", "2D written in full per segment")
    require(world_demo, "case DC_GUI_SURFACE:", "in-world GUI playback")
    require(world_demo, "in-world GUI record before render demo v11", "in-world GUI version gate")

    gui_surf = read("src/renderer/tr_guisurf.cpp")
    require_order(gui_surf, ("world->DemoGuiSurface( entityDef->index, entityGui )", "gui->Redraw(",
                             "tr.guiRecursionLevel == 1 && !tr.viewDef->isSubview", "world->WriteGuiSurface(",
                             "emitted->EmitToCurrentView("),
                  "playback draws a recorded in-world GUI; recording writes the main view's")

    retained = read("src/ui/RetainedUI.cpp")
    require(retained, "static bool RetainedRecordingRenderDemo() { return session != NULL && session->writeDemo != NULL; }",
            "retained host sees recordings")
    for needle in ("bool CanCacheLayers() const override { return !RetainedRecordingRenderDemo(); }",
                   "ui_retainedReplay.GetBool() && !RetainedRecordingRenderDemo()",
                   "ui_retainedSourceCache.GetBool() && !RetainedRecordingRenderDemo()"):
        require(retained, needle, "retained HUD draws replayable geometry while recording")
    if retained.count("RetainedAtlasActive()") < 4:
        raise AssertionError("every coverage-atlas use must go through RetainedAtlasActive()")


def validate_joints() -> None:
    """v12 joint records: exact integer prediction, one shared history, checksummed."""
    world_demo = read("src/renderer/RenderWorld_demo.cpp")
    local_h = read("src/renderer/RenderWorld_local.h")
    for needle in ("struct demoJointHistory_t {", "bool\t\t\t\t\thasPrevious = false;",
                   "void\t\t\t\t\tWriteDemoJoints( qhandle_t handle, const idJointMat *joints, int numJoints );",
                   "bool\t\t\t\t\tReadDemoJoints( qhandle_t handle, idJointMat *joints, int numJoints );",
                   "void\t\t\t\t\tResetDemoJointHistory( qhandle_t handle );\t// -1: all"):
        require(local_h, needle, "render world joint history")
    for needle in ("const byte DEMO_JOINTS_RAW = 0;", "const byte DEMO_JOINTS_PREDICTED = 1;",
                   "static_assert( sizeof( idJointMat ) == DEMO_JOINT_WORDS * sizeof( unsigned int )"):
        require(world_demo, needle, "joint record layout")

    # prediction runs on the words as integers, so every platform decodes the same bits
    prediction = between(world_demo, "ID_INLINE unsigned int R_DemoJointPrediction", "// Keeps what was just written", "prediction")
    require(prediction, "return last + ( last - history.previous[word] );", "linear integer prediction")
    reject(prediction, "float", "floating-point prediction")
    store = between(world_demo, "void R_DemoStoreJointHistory", "ID_INLINE void R_DemoPutWord", "history update")
    require_order(store, ("history.numJoints == numJoints && history.current.Num() == numWords",
                          "history.previous.Swap( history.current );", "history.hasPrevious = true;",
                          "history.hasPrevious = false;", "memcpy( history.current.Ptr(), words"),
                  "a history keeps the last record and the one before it at the same joint count")

    entity_writer = between(world_demo, "void\tidRenderWorldLocal::WriteRenderEntity", "bool\tidRenderWorldLocal::ReadRenderEntity", "entity writer")
    require(entity_writer, "WriteDemoJoints( handle, ent->joints, ent->numJoints );", "entity joints go through the joint coder")
    reject(entity_writer, "WriteFloat( data[j] )", "raw joint floats in the entity writer")
    entity_reader = between(world_demo, "bool\tidRenderWorldLocal::ReadRenderEntity", "UpdateEntityDef( index, &ent );", "entity reader")
    require_order(entity_reader, ("Mem_Alloc16( ent.numJoints * sizeof( ent.joints[0] ) )",
                                  "ReadDemoJoints( index, ent.joints, ent.numJoints )",
                                  "R_FreeRenderDemoEntityPayload( ent, demoDecals, demoOverlay );"),
                  "a failed joint record frees the entity payload")

    writer = between(world_demo, "void idRenderWorldLocal::WriteDemoJoints", "bool idRenderWorldLocal::ReadDemoJoints", "joint writer")
    require_order(writer, ("history == NULL || history->numJoints != numJoints", "out[0] = DEMO_JOINTS_RAW;",
                           "memcmp( words + first, history->current.Ptr() + first",
                           "( residual << 1 ) ^ ( 0u - ( residual >> 31 ) )", "out[0] = DEMO_JOINTS_PREDICTED;",
                           "R_DemoJointChecksum( words, numWords )", "R_DemoStoreJointHistory( *history, words, numJoints );"),
                  "raw without a matching history, else moved joints' zigzagged residuals and a checksum")
    reader = between(world_demo, "bool idRenderWorldLocal::ReadDemoJoints", "/*\n==============\nProcessDemoCommand", "joint reader")
    require_order(reader, ("session->renderdemoVersion < OPENQ4_RENDERDEMO_JOINT_PREDICTION_VERSION",
                           "R_DemoReadFloat( demo, data[j] )", "return true;"),
                  "older demos keep their raw joint floats")
    for needle in ("predicts joints it never recorded", "joint payload of %d bytes is out of range",
                   "( shift == 28 && *cursor > 0x0Fu )", "malformed joint residual", "joint payload has trailing bytes",
                   "joints do not match their checksum", "unknown joint record %d"):
        require(reader, needle, "bounded, verified joint reader")
    require_order(reader, ("R_DemoJointChecksum( words, numWords )", "memcpy( joints, words",
                           "R_DemoStoreJointHistory( *history, words, numJoints );"),
                  "playback keeps the verified joints as its history")

    # both sides drop the history at the same stream point, DC_LOADMAP
    load_writer = between(world_demo, "void\tidRenderWorldLocal::WriteLoadMap", "void\tidRenderWorldLocal::WriteVisibleDefs", "WriteLoadMap")
    require_order(load_writer, ("this != session->rw", "WriteInt( DC_LOADMAP );", "ResetDemoJointHistory( -1 );"),
                  "a recorded map load or segment start drops the joint history")
    load_reader = between(world_demo, "case DC_LOADMAP: {", "case DC_RENDERVIEW:", "DC_LOADMAP reader")
    require_order(load_reader, ("ResetDemoJointHistory( -1 );", "InitFromMap( header.mapname )"),
                  "playback drops the joint history at the same point")
    delete_reader = between(world_demo, "case DC_DELETE_ENTITYDEF:", "case DC_UPDATE_LIGHTDEF:", "DC_DELETE_ENTITYDEF reader")
    reject(delete_reader, "ResetDemoJointHistory", "playback must keep history across the delete before each rewrite")
    free_defs = between(read("src/renderer/RenderWorld_load.cpp"), "void idRenderWorldLocal::FreeDefs() {", "generateAllInteractionsCalled = false;", "FreeDefs")
    require(free_defs, "ResetDemoJointHistory( -1 );", "the history goes with the world's entities")
    free_entity = between(read("src/renderer/RenderWorld.cpp"), "void idRenderWorldLocal::FreeEntityDef", "GetRenderEntity", "FreeEntityDef")
    require_order(free_entity, ("FreeDemoGuiSurfaces( entityHandle );", "session->writeDemo != NULL && this == session->rw",
                                "ResetDemoJointHistory( entityHandle );"),
                  "only a recording drops a freed handle's history")


def validate_localization() -> None:
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for key in CAMPAIGN_STRINGS:
            match = re.search(rf'"#str_{key}"\t"([^"]*)"', table)
            if match is None:
                raise AssertionError(f"{language}: missing #str_{key}")
            formats = re.findall(r"%[sd]", match.group(1))
            if key == 41578 and formats != ["%s"]:
                raise AssertionError(f"{language}: #str_41578 must keep one %s")
            if key == 41582 and formats != ["%d"]:
                raise AssertionError(f"{language}: #str_41582 must keep one %d")
            if key not in ( 41578, 41582 ) and formats:
                raise AssertionError(f"{language}: #str_{key} must not carry a format specifier")


def main() -> int:
    validate_format()
    validate_recorder()
    validate_player()
    validate_library()
    validate_renderer()
    validate_joints()
    validate_localization()
    print("campaign demo contracts: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
