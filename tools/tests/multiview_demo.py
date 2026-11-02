#!/usr/bin/env python3
"""Guards the versioned multi-view demo contract.

MVD spans engine lifecycle, server message routing, the pseudo-client playback
path, and the companion multiplayer game library.  Most regressions compile
cleanly, so this check pins the compatibility and safety boundaries that must
move together.
"""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


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
        raise AssertionError(f"Missing ordered MVD contract in {context}: {first!r}, {second!r}")
    if first_at > second_at:
        raise AssertionError(f"{first!r} must precede {second!r} in {context}")


def expect_contract_rejection(validator, candidate: str, context: str) -> None:
    try:
        validator(candidate)
    except AssertionError:
        return
    raise AssertionError(f"Validation contract accepted mutation: {context}")


def validate_legacy_game_api_compatibility(source: str) -> None:
    for token, context in (
        ("MVD_LEGACY_GAME_API_1_0_1_1 = 39u", "retail-compatible MVD API"),
        ("MVD_LEGACY_GAME_API_PRE_SHUTDOWN_SPLIT = 42u", "pre-shutdown-split MVD API"),
        ("MVD_LEGACY_GAME_API_PRE_PRESENTATION_STATE = 44u", "pre-presentation-state MVD API"),
    ):
        require(source, token, context)
    legacy_compatibility = between(
        source,
        "static bool MVD_IsLegacyGameAPICompatible",
        "static bool MVD_ReadCString",
        "known-compatible MVD 1.0/1.1 API allowlist",
    )
    for token in (
        "version == MVD_LEGACY_GAME_API_1_0_1_1",
        "version == MVD_LEGACY_GAME_API_PRE_SHUTDOWN_SPLIT",
        "version == MVD_LEGACY_GAME_API_PRE_PRESENTATION_STATE",
        "version == static_cast<unsigned int>( GAME_API_VERSION )",
    ):
        require(legacy_compatibility, token, "known-compatible MVD 1.0/1.1 API allowlist")


def validate_container() -> None:
    source = read(ROOT / "src/framework/async/MultiViewDemo.cpp")
    header = read(ROOT / "src/framework/async/MultiViewDemo.h")

    for needle in (
        "MVD_MAGIC[8]",
        "MVD_FORMAT_MAJOR = 1",
        "MVD_FORMAT_MINOR = 3",
        "MVD_OPTIONAL_KEYFRAMES",
        "MVD_RECORD_KEYFRAME_TABLE",
        "MVD_FEATURE_RECORD_CRC",
        "MVD_FEATURE_LENGTH_DELIMITED",
        "MVD_FEATURE_SERVER_SNAPSHOTS",
        "MVD_FEATURE_RELIABLE_ROUTING",
        "MVD_FEATURE_FULL_WORLD_INSTANCES",
        "MVD_FEATURE_INSTANCE_ROUTING",
        "MVD_FORMAT_1_2_REQUIRED_FEATURES",
        "MVD_OPTIONAL_TIMELINE_INDEX",
        "MVD_RECORD_INDEX",
        "MVD_MAX_HEADER_BYTES",
        "MVD_MAX_RECORD_BYTES",
        "MVD_MAX_GAME_MESSAGE_BYTES",
        "MVD_MAX_INDEX_ENTRIES",
        "MVD_MAX_INITIALIZATION_RECORDS = 256",
        "MVD_MAX_RECORDS_PER_FRAME = 4096",
        "CRC32_BlockChecksum",
        "MVD_ParseTimelineIndex",
        "MVD_RecordSchemaSupported",
        "MVD_RecordShouldSkip",
        "MVD_PackSchemaVersion",
        "MVD_SchemaMajor",
        "MVD_SchemaMinor",
        "MVD_ValidateSyncedCVars",
        "MoveCVarsToDict( CVAR_NETWORKSYNC )",
        "MVD_ValidateReliablePayload",
        "MVD_ValidateSnapshotPayload",
        "ValidateNetworkStateRecord",
        "WriteIndexRecord",
        "BuildPlaybackIndex",
        "unsupported MVD format",
        "unknown required record type",
        "requires unsupported flag bits",
        "content checksum",
        "header.gameSchemaVersion",
        "header.usercmdHz != static_cast<unsigned int>( common->GetUserCmdHz() )",
    ):
        require(source, needle, "MultiViewDemo.cpp")

    require(header, "gameSchemaVersion", "MVD header compatibility slot")
    require(
        header,
        "Format 1.0/1.1 stored GAME_API_VERSION here",
        "legacy MVD header semantics",
    )

    record_schema = between(
        source,
        "static bool MVD_RecordSchemaSupported",
        "static bool MVD_RecordShouldSkip",
        "MVD record-schema matrix",
    )
    for needle in (
        "case MVD_RECORD_RELIABLE:",
        "case MVD_RECORD_SNAPSHOT:",
        "return version == 1 || version == 2;",
        "case MVD_RECORD_METADATA:",
        "case MVD_RECORD_MAP_STATE:",
        "case MVD_RECORD_NETWORK_STATE:",
        "case MVD_RECORD_INDEX:",
        "case MVD_RECORD_END:",
        "return version == 1;",
    ):
        require(record_schema, needle, "MVD record-schema matrix")

    read_header = between(
        source,
        "bool idMultiViewDemo::ReadHeader",
        "bool idMultiViewDemo::WriteRecord",
        "MVD header reader",
    )
    require(
        read_header,
        "outHeader.formatMinor >= 2",
        "MVD 1.2 required-feature gate",
    )
    require(
        read_header,
        "MVD_FORMAT_1_2_REQUIRED_FEATURES",
        "MVD 1.2 required-feature gate",
    )
    require(
        read_header,
        "missing its full-world instance features",
        "MVD 1.2 partial-world rejection",
    )

    start_recording = between(
        source,
        "bool idMultiViewDemo::StartRecording",
        "bool idMultiViewDemo::CommitRecording",
        "MVD recording header",
    )
    for needle in (
        "game->GetMVDSchemaVersion( schemaMajor, schemaMinor );",
        "header.gameSchemaVersion = MVD_PackSchemaVersion( schemaMajor, schemaMinor );",
        "header.requiredFeatures = MVD_SUPPORTED_REQUIRED_FEATURES;",
    ):
        require(start_recording, needle, "MVD 1.2 recording header")

    start_playback = between(
        source,
        "bool idMultiViewDemo::StartPlayback",
        "bool idMultiViewDemo::ResetPlaybackStream",
        "MVD playback compatibility gates",
    )
    for needle in (
        "game->ValidateDemoProtocol( ASYNC_PROTOCOL_MINOR, header.protocolMinor )",
        "header.formatMinor <= 1",
        "MVD_IsLegacyGameAPICompatible( header.gameSchemaVersion )",
        "game->IsMVDSchemaCompatible(",
        "MVD_SchemaMajor( header.gameSchemaVersion )",
        "MVD_SchemaMinor( header.gameSchemaVersion )",
    ):
        require(start_playback, needle, "MVD playback compatibility gates")
    validate_legacy_game_api_compatibility(source)
    api42_allow = "\t\tversion == MVD_LEGACY_GAME_API_PRE_SHUTDOWN_SPLIT ||\n"
    if source.count(api42_allow) != 1:
        raise AssertionError("MVD API-42 compatibility mutation anchor is not unique")
    expect_contract_rejection(
        validate_legacy_game_api_compatibility,
        source.replace(api42_allow, "", 1),
        "legacy MVDs recorded by game API 42 are rejected after the shutdown-only API bump",
    )
    api44_allow = "\t\tversion == MVD_LEGACY_GAME_API_PRE_PRESENTATION_STATE ||\n"
    if source.count(api44_allow) != 1:
        raise AssertionError("MVD API-44 compatibility mutation anchor is not unique")
    expect_contract_rejection(
        validate_legacy_game_api_compatibility,
        source.replace(api44_allow, "", 1),
        "legacy MVDs recorded by game API 44 are rejected after the presentation-only API bump",
    )

    # Map state from the stream start and from keyframes is parsed into
    # temporaries, validated, and only then applied.
    require_order(
        source,
        "MVD_ReadDict( &payload, syncedCVars, lastError )",
        "MVD_ValidateSyncedCVars( syncedCVars, lastError )",
        "MVD network-cvar allowlist",
    )
    synced_cvars = between(
        source,
        "static bool MVD_ValidateSyncedCVars",
        "static bool MVD_ParseTimelineIndex",
        "MVD network-cvar allowlist",
    )
    require(synced_cvars, "FindKey( kv->GetKey() )", "MVD network-cvar allowlist")
    require(
        synced_cvars,
        "contains non-network cvar",
        "MVD network-cvar rejection",
    )

    # Recording is transactional: an interrupted stream remains visibly
    # partial, and only a clean end record is renamed to its final .mvd name.
    require(source, 'tempFileName += ".part";', "MVD staged recording")
    stop_recording = source[
        source.index("bool idMultiViewDemo::StopRecording( const char *reason, bool finalize,") :
    ]
    stop_recording = stop_recording[: stop_recording.index(
        "void idMultiViewDemo::CaptureReliableMessage"
    )]
    require_order(
        stop_recording,
        "WriteIndexRecord()",
        "WriteRecord( MVD_RECORD_END",
        "MVD timeline finalization",
    )
    require_order(
        stop_recording,
        "WriteRecord( MVD_RECORD_END",
        "CommitRecording()",
        "MVD clean finalization",
    )
    # The atomic rename itself lives in idFileSystemLocal::PromoteFile, which
    # validates both qpaths against the writable root; see
    # filesystem_write_qpath_safety.py and mvd_server_api_contract.py.
    require(
        source,
        'fileSystem->PromoteFile( tempFileName.c_str(), fileName.c_str(),',
        "MVD atomic commit",
    )

    # Bounds and CRCs must be checked before game code receives a payload.
    require_order(
        source,
        "payloadLength > MVD_MAX_RECORD_BYTES",
        "record.payload.SetNum( payloadLength )",
        "MVD record allocation bound",
    )
    require_order(
        source,
        "actualPayloadCRC != payloadCRC",
        "game->ClientReadServerDemoSnapshot",
        "MVD payload validation",
    )

    # The timeline directory remains optional. Format 1.0 and short, clean
    # zero-snapshot recordings leave indexOffset at zero, so the reader must
    # validate the checked stream without requiring an index record.
    require(
        source,
        "bool sawHeaderIndex = header.indexOffset == 0;",
        "MVD no-index fallback",
    )
    require(
        source,
        "if ( record.type == MVD_RECORD_SNAPSHOT )",
        "MVD timeline reconstruction",
    )
    write_index = source[source.index("bool idMultiViewDemo::WriteIndexRecord") :]
    write_index = write_index[: write_index.index("bool idMultiViewDemo::ReadMapStateRecord")]
    require_order(
        write_index,
        "header.indexOffset = file->Tell();",
        "WriteRecord( MVD_RECORD_INDEX",
        "MVD optional index backpatch",
    )
    require_order(
        write_index,
        "WriteRecord( MVD_RECORD_INDEX",
        "WriteHeader()",
        "MVD optional index header backpatch",
    )
    no_snapshot_index = between(
        write_index,
        "if ( recordingIndex.Num() <= 0 )",
        'idFile_Memory payload( "MVD timeline index" );',
        "MVD zero-snapshot index finalization",
    )
    require(no_snapshot_index, "return true;", "MVD zero-snapshot clean finalization")
    require(
        source,
        "reportedRecords != playbackInitializationRecordCount + validatedRecords - 1",
        "MVD no-index global end-count validation",
    )
    require(
        source,
        "playbackInitializationRecordCount",
        "MVD playback initialization count",
    )
    require(
        source,
        "recordCount = playbackInitializationRecordCount;",
        "MVD runtime global record count",
    )
    require(
        source,
        "reportedRecords != recordCount - 1",
        "MVD runtime end-count validation",
    )

    # Delta snapshots are not keyframes. Backward seeks reset the map/network
    # baseline and replay from the stream start under a bounded per-frame
    # budget, which also keeps format 1.0 seekable.
    seek = source[source.index("bool idMultiViewDemo::SeekToMS") :]
    seek = seek[: seek.index("bool idMultiViewDemo::SeekByMS")]
    require_order(
        seek,
        "target < static_cast<int>( playbackGameTime )",
        "ResetPlaybackStream()",
        "MVD backward-seek reset",
    )
    for needle in (
        "mvd_seekBudgetMS",
        "ProcessSeekBudget",
        "processed < 1024",
        "file->Seek( playbackStreamOffset, FS_SEEK_SET )",
        "forcePresentationFrame = true;",
    ):
        require(source, needle, "bounded MVD replay seeking")

    require_order(
        source,
        "MVD_ValidateSnapshotPayload( record.payload, record.version, lastError )",
        "game->ClientReadServerDemoSnapshot",
        "MVD game-payload envelope validation",
    )
    require(
        source,
        "game->IsDemoProtocolCompatible( ASYNC_PROTOCOL_MINOR, inspectedHeader.protocolMinor )",
        "MVD browser protocol compatibility",
    )

    for needle in (
        "WriteRecord( MVD_RECORD_RELIABLE, 2, MVD_RECORD_FLAG_REQUIRED",
        "WriteRecord( MVD_RECORD_SNAPSHOT, 2, MVD_RECORD_FLAG_REQUIRED",
        "const int routeBytes = version == 2 ? 3 : 2;",
        "version == 2 && routeType == MVD_UNRELIABLE_RECORD_AREAS_INSTANCE",
        "game->ServerWriteServerDemoSnapshot( sequence, msg, lastSnapshotGameFrame, true );",
        "record.version >= 2",
    ):
        require(source, needle, "MVD 1.2 record routing")

    playback_records = between(
        source,
        "bool idMultiViewDemo::ProcessPlaybackRecord",
        "void idMultiViewDemo::RunPlaybackFrame",
        "MVD routed-record playback",
    )
    for needle in (
        "record.version == 2 && record.payload[8] != DEMO_RECORD_INSTANCE",
        "normalizedRoute.SetNum( messageBytes - 1 );",
        "record.payload.Ptr() + 11",
        "game->ClientProcessReliableMessage( MAX_ASYNC_CLIENTS, msg );",
        "record.version >= 2",
    ):
        require(playback_records, needle, "MVD v1/v2 route normalization")

    initialization = between(
        start_playback,
        "playbackInitializationRecordCount = 0;",
        "playbackMapState = mapState;",
        "MVD initialization scan",
    )
    require(
        initialization,
        "playbackInitializationRecordCount >= MVD_MAX_INITIALIZATION_RECORDS",
        "bounded MVD initialization scan",
    )

    playback_frame = between(
        source,
        "void idMultiViewDemo::RunPlaybackFrame",
        "bool idMultiViewDemo::QueryFileInfo",
        "MVD playback frame",
    )
    require(
        playback_frame,
        "MVD_MAX_RECORDS_PER_FRAME",
        "MVD per-frame record cap",
    )

    query = between(
        source,
        "bool idMultiViewDemo::QueryFileInfo",
        "bool idMultiViewDemo::Inspect",
        "MVD library validation",
    )
    for needle in (
        "initializationRecords < MVD_MAX_INITIALIZATION_RECORDS",
        "MVD_ValidateSnapshotPayload( record.payload, record.version, lastError )",
        "MVD_ValidateReliablePayload( record.payload, record.version, lastError )",
        "sequence != lastSnapshotSequence + 1",
        "reportedSnapshots != info.snapshotCount",
        "reportedReliables != info.reliableCount",
        "reportedRecords != info.recordCount - 1",
        "MVD contains record %u after its end marker",
    ):
        require(query, needle, "MVD bounded library validation")


def validate_engine_wiring() -> None:
    network = read(ROOT / "src/framework/async/AsyncNetwork.cpp")
    server = read(ROOT / "src/framework/async/AsyncServer.cpp")
    client = read(ROOT / "src/framework/async/AsyncClient.cpp")
    network_system = read(ROOT / "src/framework/async/NetworkSystem.cpp")
    network_system_header = read(ROOT / "src/framework/async/NetworkSystem.h")
    session = read(ROOT / "src/framework/Session.cpp")

    require(network, "multiViewDemo.RunPlaybackFrame();", "async playback loop")
    require_order(
        network,
        "server.RunFrame( allowBlocking );",
        "multiViewDemo.CaptureServerFrame",
        "server snapshot timing",
    )
    require(server, "CaptureReliableMessage( msg, DEMO_RECORD_CLIENTNUM", "reliable target routing")
    require(server, "CaptureReliableMessage( msg, DEMO_RECORD_EXCLUDE", "reliable exclusion routing")
    require(server, "bool captureDemo", "semantic reliable capture control")
    require(server, "if ( clientNum >= 0 )", "pseudo-client non-broadcast handling")
    for needle in (
        "ServerSendReliableMessageNoDemo",
        "ServerSendReliableMessageExcludingNoDemo",
        "ServerRecordInstanceReliableMessage",
    ):
        require(network_system_header, needle, "instance reliable routing API")
    require(
        network_system,
        "msg, DEMO_RECORD_INSTANCE, excludeClient, instance",
        "single semantic instance reliable capture",
    )
    require(client, "return active || idAsyncNetwork::multiViewDemo.IsPlaying();", "pseudo-client activity")
    require(client, "MAX_ASYNC_CLIENTS : clientNum", "pseudo-client number")
    require_order(
        session,
        "idAsyncNetwork::multiViewDemo.SessionStop();",
        "UnloadMap();",
        "MVD teardown before map teardown",
    )


def validate_game_wiring() -> None:
    game_network = GAME_LIBS_ROOT / "src/mpgame/Game_network.cpp"
    if not game_network.is_file():
        print(
            f"multiview_demo: skipped companion game checks "
            f"(no GameLibs checkout at {GAME_LIBS_ROOT})"
        )
        return

    source = read(game_network)
    public_api = read(GAME_LIBS_ROOT / "src/game/Game.h")
    for needle in (
        "DEMO_RECORD_INSTANCE",
        "IsDemoProtocolCompatible( int minor_ref, int minor ) const",
        "GetMVDSchemaVersion( int &major, int &minor ) const",
        "IsMVDSchemaCompatible( int major, int minor ) const",
        "ServerWriteServerDemoSnapshot( int sequence, idBitMsg &msg, int lastSnapshotFrame, bool fullWorldInstances )",
        "bool",
        "ClientReadServerDemoSnapshot( int sequence, const int gameFrame, const int gameTime, const idBitMsg &msg, bool fullWorldInstances )",
    ):
        require(public_api, needle, "game MVD compatibility API")

    for needle in (
        "ServerWriteServerDemoSnapshot",
        "ClientReadServerDemoSnapshot",
        "clientEntityStates[ MAX_CLIENTS ]",
        "unreliableMessages[ MAX_CLIENTS ]",
        "bool fullWorldInstances",
        "bool writeEntityInstances",
        "if ( !writeEntityInstances && ent->GetInstance() != instance )",
        "if ( writeEntityInstances )",
        "ASYNC_PLAYER_INSTANCE_BITS",
        "readEntityInstances ? deltaMsg.ReadBits",
        "return ClientReadSnapshot( MAX_CLIENTS, sequence, gameFrame, gameTime, 0, 0, msg, fullWorldInstances );",
        "toClient = msg.ReadChar();",
        "excludeClient = msg.ReadChar();",
        "case DEMO_RECORD_INSTANCE:",
        "const int routeInstance = msg.ReadChar();",
        "viewEntity->GetInstance() == routeInstance",
        "GAME_UNRELIABLE_RECORD_AREAS_INSTANCE",
        "dest.WriteChar( instanceEnt->GetInstance() );",
        "record_type == GAME_UNRELIABLE_RECORD_AREAS_INSTANCE",
        "networkSystem->ServerSendReliableMessageNoDemo",
        "networkSystem->ServerRecordInstanceReliableMessage",
        "dest.WriteChar( clientNum );",
        "int client = msg.ReadChar();",
        "invalid player count",
        "invalid or truncated entity number",
        "non-player entity %d has player state",
        "invalid server-demo reliable route",
        "missing baseline for sequence",
        "invalid entity header",
        "invalid baseline instance",
    ):
        require(source, needle, "mpgame/Game_network.cpp")

    game_implementation = "\n".join(
        read(path) for path in sorted((GAME_LIBS_ROOT / "src/mpgame").glob("*.cpp"))
    )
    sp_implementation = "\n".join(
        read(path) for path in sorted((GAME_LIBS_ROOT / "src/game").glob("*.cpp"))
    )
    for needle in (
        "idGameLocal::IsDemoProtocolCompatible",
        "idGameLocal::GetMVDSchemaVersion",
        "idGameLocal::IsMVDSchemaCompatible",
    ):
        require(game_implementation, needle, "multiplayer MVD compatibility implementation")
        require(sp_implementation, needle, "single-player MVD compatibility stub")

    multiplayer = read(GAME_LIBS_ROOT / "src/mpgame/MultiplayerGame.cpp")
    game_state = read(GAME_LIBS_ROOT / "src/mpgame/mp/GameState.cpp")
    require(multiplayer, "invalid multiplayer start-state client", "MP start-state bounds")
    require(multiplayer, "invalid start-state size", "MP start-state length bound")
    require(game_state, "invalid state size", "MP game-state bounds")
    require(game_state, "invalid tourney history range", "MP tourney-state bounds")


def validate_documented_compatibility() -> None:
    developer = read(ROOT / "docs/dev/multiview-demos.md")
    user = read(ROOT / "docs/user/multiview-demos.md")

    for needle in (
        "format `1.3`",
        "formats `1.0` to `1.2`",
        "### Keyframes, 1.3",
        "raw `GAME_API_VERSION`",
        "packed MVD game-schema",
        "`FULL_WORLD_INSTANCES`",
        "`INSTANCE_ROUTING`",
        "record version 1",
        "record version 2",
        "`CVAR_NETWORKSYNC`",
        "at most 256 records",
        "at most 4096 stream records",
        "zero snapshots",
        "timeline directory, not a random-access checkpoint",
        "replay-from-start",
        "closes the demo and returns to the menu with a",
        "Retail Quake 4 render `.demo`",
        "Retail Quake 4 `.netdemo`",
        "openQ4 `.cdemo`",
    ):
        require(developer, needle, "developer MVD compatibility documentation")

    for needle in (
        "MVD `.mvd` 1.3",
        "MVD 1.3 is the current writer",
        "every active multiplayer instance",
        "version-1 decoder",
        "no index and a zero snapshot count",
        "separately versioned MVD game schema",
        "not network-synchronized",
        "returns to the menu with a warning",
        "replay-based seek",
        "Retail Quake 4 `.netdemo`",
        "Command `.cdemo`",
        "`demoSeek <seconds>`",
        "`demoFreeRoam`",
    ):
        require(user, needle, "user demo playback documentation")


def main() -> int:
    validate_container()
    validate_engine_wiring()
    validate_game_wiring()
    validate_documented_compatibility()
    print("multiview_demo: compatibility and lifecycle checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
