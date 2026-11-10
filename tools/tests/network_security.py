#!/usr/bin/env python3
"""Focused source contracts for connection, rcon2, and private-CVar security."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def read_game(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required openQ4-game file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    require(text, first, context)
    require(text, second, context)
    if text.index(first) >= text.index(second):
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def validate_crypto_and_native_vectors() -> None:
    crypto = read("src/idlib/CryptoHash.cpp")
    protocol = read("src/framework/async/Rcon2Protocol.cpp")
    protocol_header = read("src/framework/async/Rcon2Protocol.h")
    native = read("tools/tests/native/CoreSafetyTest.cpp")
    meson = read("meson.build")

    for token in (
        "original implementation of the published FIPS 180-4 SHA-256",
        "void SHA256(",
        "void HMACSHA256(",
        "bool PBKDF2HMACSHA256(",
        "bool ConstantTimeEquals(",
        "void SecureZero(",
    ):
        require(crypto, token, "engine-owned cryptographic primitives")
    require(protocol, 'PROOF_DOMAIN[] = "openQ4-rcon2-proof-v1"', "rcon2 domain separation")
    require(protocol_header, "PBKDF2_ITERATIONS = 200000", "fixed rcon2 KDF work factor")
    require(protocol_header, "MIN_PASSWORD_BYTES = 12", "minimum rcon2 password length")

    for token in (
        "RFC 4231 HMAC-SHA-256",
        "PBKDF2-HMAC-SHA-256 c=1",
        "PBKDF2-HMAC-SHA-256 c=2",
        "PBKDF2-HMAC-SHA-256 c=4096",
        "rcon2 proof domain",
        "rcon2 request digest",
        "SecureZero left data behind",
        '"command ends in private-name prefix"',
        '"private name is a longer token prefix"',
        '"private name at command end"',
        '"parent path escape"',
        '"forward-slash path"',
        '"backslash path"',
        '"Windows device directory"',
        '"oversized game-directory segment"',
    ):
        require(native, token, "native crypto/private-token coverage")
    for token in (
        "src/idlib/CryptoHash.cpp",
        "src/framework/async/Rcon2Protocol.cpp",
    ):
        require(meson, token, "native safety target sources")


def validate_secure_random_and_connection_challenges() -> None:
    client = read("src/framework/async/AsyncClient.cpp")
    server = read("src/framework/async/AsyncServer.cpp")

    for source, label in ((client, "async client"), (server, "async server")):
        reject(source, "rand()", label)
        reject(source, "idRandom random", label)
        reject(source, "Sys_Milliseconds() & CONNECTIONLESS_MESSAGE_ID_MASK", label)

    client_id = function_body(client, "static bool AsyncClient_SecureConnectionId(", "client ID generator")
    server_id = function_body(server, "static bool AsyncServer_SecureConnectionId(", "server ID generator")
    issue = function_body(server, "void idAsyncServer::ProcessChallengeMessage(", "connection challenge issuance")
    validate = function_body(server, "int idAsyncServer::ValidateChallenge(", "connection challenge validation")
    download = function_body(client, "int idAsyncClient::GetDownloadRequest(", "download request ID")

    for body, label in (
        (client_id, "client ID generator"),
        (server_id, "server ID generator"),
        (issue, "connection challenge issuance"),
        (download, "download request ID"),
    ):
        require(body, "Sys_GetSecureRandomBytes", label)
    require(issue, "msg.GetRemainingData() != 4", "bounded challenge payload")
    require(issue, "challenges[i].valid = true", "explicit challenge validity")
    require(issue, "challenges[i].address = from", "challenge endpoint binding")
    require(issue, "challenges[i].clientId = clientId", "challenge client-ID binding")
    require(issue, "AsyncServer_Elapsed( serverTime, challenges[ i ].time )", "wrap-safe challenge age")
    reject(issue, "challenges[i].time < oldestTime", "raw signed challenge eviction")

    for token in (
        "challenges[ i ].valid",
        "!challenges[ i ].connected",
        "AsyncServer_Elapsed( serverTime, challenges[ i ].time ) <= CONNECTION_CHALLENGE_TIMEOUT_MSEC",
        "AsyncServer_SameEndpoint( from, challenges[i].address )",
        "clientId == challenges[ i ].clientId",
        "challenge == challenges[i].challenge",
    ):
        require(validate, token, "bound, expiring challenge validation")
    require(validate, "AllowConnectionlessResponse( from, false )", "bounded bad-challenge reply")
    require(download, "dlRequest = -1", "download request fail-closed path")
    require(server, "AsyncServer_ClearChallenge( challenges[ ichallenge ] );", "one-use successful connection challenge")


def validate_oob_budgets() -> None:
    server = read("src/framework/async/AsyncServer.cpp")
    limiter = function_body(server, "bool idAsyncServer::AllowConnectionlessResponse(", "OOB response limiter")
    info = function_body(server, "void idAsyncServer::ProcessGetInfoMessage(", "getInfo handler")
    challenge = function_body(server, "void idAsyncServer::ProcessChallengeMessage(", "challenge handler")
    rcon_challenge = function_body(
        server,
        "void idAsyncServer::ProcessRemoteConsole2ChallengeMessage(",
        "rcon2 challenge handler",
    )
    connect = function_body(server, "void idAsyncServer::ProcessConnectMessage(", "connect handler")
    pure = function_body(server, "void idAsyncServer::ProcessPureMessage(", "pure handler")
    download = function_body(server, "void idAsyncServer::ProcessDownloadRequestMessage(", "download handler")
    connectionless = function_body(server, "bool idAsyncServer::ConnectionlessMessage(", "server OOB dispatcher")
    process_message = function_body(server, "bool idAsyncServer::ProcessMessage(", "server packet dispatcher")

    for token in (
        "OOB_INFO_MAX_PER_SOURCE",
        "OOB_CHALLENGE_MAX_PER_SOURCE",
        "OOB_INFO_MAX_GLOBAL",
        "OOB_CHALLENGE_MAX_GLOBAL",
        "Sys_IsLANAddress( from )",
        "oobInfoResponses >= OOB_INFO_MAX_GLOBAL",
        "oobChallengeResponses >= OOB_CHALLENGE_MAX_GLOBAL",
    ):
        require(limiter, token, "per-source/global OOB response budget")
    require(info, "msg.GetRemainingData() != 4", "bounded getInfo request")
    require(info, "AllowConnectionlessResponse( from, true )", "bounded infoResponse")
    require_before(challenge, "AllowConnectionlessResponse( from, false )", "serverPort.SendPacket", "challenge response budget")
    require_before(rcon_challenge, "AllowConnectionlessResponse( from, false )", "serverPort.SendPacket", "rcon2 response budget")
    require_before(connect, "ValidateChallenge( from, challenge, clientId )", "protocol != ASYNC_PROTOCOL_VERSION", "challenge before protocol reply")
    require_before(connect, "OS < 0 || OS >= MAX_GAME_OS", "ValidateChallenge( from, challenge, clientId )", "client OS bounds before challenge state")
    require_before(connect, "AllowConnectionlessResponse( from, false )", "protocol != ASYNC_PROTOCOL_VERSION", "connect reply budget")
    require_before(connect, "AllowConnectionlessResponse( from, false )", "clientDataChecksum != serverDataChecksum", "checksum reply budget")
    require(pure, "AllowConnectionlessResponse( from, false )", "pure failure reply budget")
    require(download, "AllowConnectionlessResponse( from, false )", "download response budget")
    inactive = connectionless[connectionless.index("if ( !active )") :]
    require_before(inactive, "AllowConnectionlessResponse( from, false )", "PrintOOB", "inactive-server reply budget")
    unknown_client = process_message[process_message.index("// if we received a sequenced packet") :]
    require_before(
        unknown_client,
        "AllowConnectionlessResponse( from, false )",
        "serverPort.SendPacket",
        "unknown sequenced-client disconnect budget",
    )


def _validate_retail_protocol_refusal(server: str, client: str) -> None:
    challenge = function_body(server, "void idAsyncServer::ProcessChallengeMessage(", "challenge handler")
    require(server, "const int RETAIL_CHALLENGE_REQUEST_BYTES = 5;", "Quake 4 1.4.x challenge layout")
    retail = challenge[challenge.index("msg.GetRemainingData() == RETAIL_CHALLENGE_REQUEST_BYTES"):] \
        if "msg.GetRemainingData() == RETAIL_CHALLENGE_REQUEST_BYTES" in challenge else ""
    require(retail, "return;", "retail challenge refusal")
    retail = retail[:retail.index("return;")]
    require_before(retail, "AllowConnectionlessResponse( from, false )", "PrintOOB( from, SERVER_PRINT_BADPROTOCOL,",
                   "budgeted retail challenge refusal")
    require_before(challenge, "msg.GetRemainingData() == RETAIL_CHALLENGE_REQUEST_BYTES", "msg.GetRemainingData() != 4",
                   "retail challenge refused before the openQ4 layout is enforced")

    setup = function_body(client, "void idAsyncClient::SetupConnection( void )", "connection setup")
    require_before(setup, 'msg.WriteString( "getInfo" );', 'msg.WriteString( "challenge" );', "protocol query before challenge")
    query = setup[setup.index("if ( !connectProtocolKnown ) {"):] if "if ( !connectProtocolKnown ) {" in setup else ""
    require_before(query, 'msg.WriteString( "getInfo" );', "connectInfoRequests++;", "counted protocol query")
    require(query, "msg.WriteLong( clientId );", "protocol query marked by the connection id")

    info = function_body(client, "void idAsyncClient::ProcessInfoResponseMessage(", "info response handler")
    require_before(info, "CheckConnectServerProtocol( from, serverInfo.challenge, protocol )",
                   "if ( protocol != ASYNC_PROTOCOL_VERSION ) {", "connection protocol check before browser filtering")
    check = function_body(client, "bool idAsyncClient::CheckConnectServerProtocol(", "connection protocol check")
    for token in ("challenge != clientId", "AsyncClient_SameEndpoint( from, serverAddress )",
                  "clientState != CS_CHALLENGING && clientState != CS_CONNECTING", '"#str_231570"', '"#str_41561"'):
        require(check, token, "connection protocol check")
    require_before(check, "protocol == ASYNC_PROTOCOL_VERSION", 'BufferCommandText( CMD_EXEC_NOW, "disconnect" )',
                   "only a different protocol is refused")

    response = function_body(client, "void idAsyncClient::ProcessChallengeResponseMessage(", "challenge response handler")
    require_before(response,
                   "otherGameDirectory && !connectProtocolKnown && connectInfoRequests < CONNECT_PROTOCOL_INFO_REQUESTS",
                   "GetModInfo( serverGame, modInfo, &modReason )",
                   "protocol known before a server's game directory is followed")


def validate_retail_protocol_refusal() -> None:
    """Quake 4 1.4.x peers are refused for their protocol, in both directions."""
    server = read("src/framework/async/AsyncServer.cpp")
    client = read("src/framework/async/AsyncClient.cpp")
    _validate_retail_protocol_refusal(server, client)

    for path in sorted((ROOT / "content/baseoq4/pak0/strings").glob("*_openq4.lang")):
        text = path.read_text(encoding="utf-8")
        line = next((entry for entry in text.splitlines() if '"#str_231570"' in entry), "")
        value = line.split('"#str_231570"', 1)[1] if line else ""
        if value.count("%s") != 1 or value.count("%d") != 4:
            raise AssertionError(f"{path.name}: #str_231570 must take the address and both protocol versions")

    for mutation, context in (
        (server.replace("msg.GetRemainingData() == RETAIL_CHALLENGE_REQUEST_BYTES", "false", 1),
         "a retail challenge is dropped without an answer"),
        (server.replace("PrintOOB( from, SERVER_PRINT_BADPROTOCOL, va( \"server uses protocol %d.%d\\n\"", "( ( void )0, va( \"\"", 1),
         "a retail challenge is answered without the protocol print"),
    ):
        try:
            _validate_retail_protocol_refusal(mutation, client)
        except AssertionError:
            continue
        raise AssertionError(f"retail refusal contract accepted mutation: {context}")
    for mutation, context in (
        (client.replace("CheckConnectServerProtocol( from, serverInfo.challenge, protocol )", "false", 1),
         "the connection never checks the server's protocol"),
        (client.replace("otherGameDirectory && !connectProtocolKnown && connectInfoRequests < CONNECT_PROTOCOL_INFO_REQUESTS",
                        "false", 1),
         "a server's game directory is followed before its protocol is known"),
        (client.replace("if ( !connectProtocolKnown ) {", "if ( false ) {", 1),
         "no protocol query goes with the challenge"),
    ):
        try:
            _validate_retail_protocol_refusal(server, mutation)
        except AssertionError:
            continue
        raise AssertionError(f"retail refusal contract accepted mutation: {context}")


def validate_usercmd_packet_bounds() -> None:
    header = read("src/framework/async/AsyncNetwork.h")
    network = read("src/framework/async/AsyncNetwork.cpp")
    client = read("src/framework/async/AsyncClient.cpp")
    server = read("src/framework/async/AsyncServer.cpp")
    decoder = function_body(
        network,
        "bool idAsyncNetwork::ReadUserCmdDelta(",
        "bounded user-command decoder",
    )
    handler = function_body(
        server,
        "void idAsyncServer::ProcessUnreliableClientMessage(",
        "server unreliable-client handler",
    )
    sender = function_body(
        client,
        "void idAsyncClient::SendUsercmdsToServer(",
        "client user-command sender",
    )
    connect_response = function_body(
        client,
        "void idAsyncClient::ProcessConnectResponseMessage(",
        "client connect-response handler",
    )
    connect_request = function_body(
        server,
        "void idAsyncServer::ProcessConnectMessage(",
        "server connect-request handler",
    )
    snapshot_sender = function_body(
        server,
        "bool idAsyncServer::SendSnapshotToClient(",
        "server snapshot sender",
    )
    client_handler = function_body(
        client,
        "void idAsyncClient::ProcessUnreliableServerMessage(",
        "client unreliable-server handler",
    )
    game_init = client_handler[client_handler.index("case SERVER_UNRELIABLE_MESSAGE_GAMEINIT") :]
    snapshot = client_handler[client_handler.index("case SERVER_UNRELIABLE_MESSAGE_SNAPSHOT") :]

    require(header, "MAX_USERCMD_PACKET_COMMANDS = 11", "wire user-command work bound")
    require(header, "static bool\t\t\t\tReadUserCmdDelta", "fallible user-command decoder API")
    require(client, "MAX_USERCMD_PACKET_COMMANDS - 1", "client packet-count bound")
    require(sender, "gameFrame >= requestedUsercmds - 1 ? requestedUsercmds : gameFrame + 1", "startup user-command history clamp")
    require(sender, "gameFrame > AsyncClient_MaxNetworkGameFrame()", "client frame-domain guard")
    reject(sender, "Min( requestedUsercmds, gameFrame + 1 )", "overflow-prone startup history clamp")
    require(network, "AsyncNetwork_CanReadUserCmdDelta", "user-command bit preflight")
    require(network, "probe.GetRemainingReadBits() >= 152", "full-command bit bound")
    require(decoder, "if ( !AsyncNetwork_CanReadUserCmdDelta", "decode before mutation")
    require(decoder, "usercmd_t decoded;", "transactional user-command decode")
    require(decoder, "cmd = decoded;", "commit only complete user-command")
    require(handler, "msg.GetRemainingReadBits() < 64", "fixed unreliable-message prefix bound")
    require(handler, "msg.GetRemainingReadBits() < 32 + 8", "snapshot/id header bound")
    ping_handler = handler[handler.index("case CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE") :]
    require_before(ping_handler, "msg.GetRemainingReadBits() < 32", "msg.ReadLong()", "ping-response payload bound")
    require(handler, "msg.GetRemainingReadBits() < 16 + 32 + 8", "fixed user-command header bound")
    require(handler, "numUsercmds > MAX_USERCMD_PACKET_COMMANDS", "server packet-count bound")
    require(handler, "clientGameFrame < numUsercmds - 1", "negative user-command history rejection")
    require(
        handler,
        "static_cast<int64>( clientGameFrame ) > static_cast<int64>( gameFrame ) + MAX_USERCMD_BACKUP",
        "implausible future client-frame rejection",
    )
    require(handler, "commandIndex < numUsercmds", "counted user-command loop")
    require(handler, "if ( !idAsyncNetwork::ReadUserCmdDelta", "truncated client packet rejection")
    require(handler, "echoedPingTime != client.lastPingTime", "ping response challenge binding")
    require(handler, "AsyncServer_Elapsed( realTime, echoedPingTime )", "overflow-safe ping calculation")
    reject(handler, "i <= clientGameFrame", "overflow-prone frame-sentinel loop")
    require(client, "if ( !idAsyncNetwork::ReadUserCmdDelta", "truncated server snapshot rejection")
    require(client, "i > MAX_ASYNC_CLIENTS", "snapshot relay terminator bound")
    require(client, "i == MAX_ASYNC_CLIENTS", "snapshot relay exact terminator")
    require(client, "numUsercmds < 1 || numUsercmds > MAX_USERCMD_RELAY", "snapshot relay count bound")
    require(client, "server sent a truncated snapshot header", "snapshot fixed-header bound")
    require(client, "server sent invalid snapshot timing", "snapshot timing-domain validation")
    require(client, "server sent invalid game-init timing", "game-init timing-domain validation")

    teardown = function_body(
        client,
        "static void AsyncClient_StopAfterMalformedSnapshot(",
        "malformed snapshot session teardown",
    )
    require(teardown, "arenaCampaign.AbortMatch();", "malformed snapshot Arena rollback")
    require(teardown, "session->Stop();", "malformed snapshot map teardown")
    post_game_decode = snapshot[snapshot.index("if ( !game->ClientReadSnapshot(") :]
    if post_game_decode.count("AsyncClient_StopAfterMalformedSnapshot();") != 4:
        raise AssertionError(
            "all post-game snapshot failures must tear down the session before the outer frame continues"
        )
    reject(post_game_decode, "DisconnectFromServer();", "post-game snapshot channel-only teardown")

    require(client, "AsyncClient_MaxPredictionMsec", "bounded prediction arithmetic")
    require(client, "const std::int64_t adjustedPredictTime", "widened prediction adjustment")
    require(client, "adjustedPredictTime > maximumPredictionMsec", "prediction adjustment clamp")
    require(client, "static ID_INLINE bool AsyncClient_ValidNetworkTiming", "tick-derived network timing helper")
    require(client, "common->GetUserCmdMsecNumerator()", "exact user-command tick frame bound")
    require(client, "common->GetUserCmdMSec()", "legacy user-command tick frame bound")
    require(connect_response, "msg.GetRemainingReadBits() < 32 + 32 + 32 + 32", "connect-response fixed-header bound")
    require(connect_response, "AsyncClient_ValidNetworkTiming( serverGameFrame, serverGameTime )", "connect-response timing bound")
    require_before(connect_response, "msg.IsReadOverflowed()", "channel.Init", "connect response validation before state mutation")
    require(connect_response, "AsyncClient_Elapsed( clientTime, lastConnectTime )", "overflow-safe initial prediction interval")
    require(connect_response, "AsyncClient_MaxPredictionMsec()", "bounded initial prediction clamp")
    reject(connect_response, "clientTime - lastConnectTime", "overflow-prone initial prediction interval")
    require_before(game_init, "!AsyncClient_ValidNetworkTiming", "InitGame", "game-init timing validation before state mutation")
    require_before(snapshot, "!AsyncClient_ValidNetworkTiming", "snapshotGameFrame = receivedSnapshotGameFrame", "snapshot timing validation before state mutation")
    require(connect_request, "connect from %s rejected: truncated GUID", "truncated connect GUID rejection")
    require(connect_request, "connect from %s rejected: truncated password", "truncated connect password rejection")
    require_before(connect_request, "truncated password", "game->ServerAllowClient", "password validation before game callback")
    require(connect_request, "AsyncServer_Elapsed( serverTime, challenges[ ichallenge ].pingTime )", "overflow-safe connect ping")
    require(snapshot_sender, "const std::int64_t clientAheadTime", "widened client-ahead arithmetic")
    require(snapshot_sender, "clientAheadTime < idMath::INT_MIN", "client-ahead lower clamp")
    require(snapshot_sender, "clientAheadTime > idMath::INT_MAX", "client-ahead upper clamp")


def validate_snapshot_decode_bounds() -> None:
    engine_bitmsg = read("src/idlib/BitMsg.cpp")
    engine_bitmsg_header = read("src/idlib/BitMsg.h")
    game_bitmsg = read_game("src/idlib/BitMsg.cpp")
    game_bitmsg_header = read_game("src/idlib/BitMsg.h")
    sp_network = read_game("src/game/Game_network.cpp")
    mp_network = read_game("src/mpgame/Game_network.cpp")
    sp_player = read_game("src/game/Player.cpp")
    mp_player = read_game("src/mpgame/Player.cpp")
    sp_multiplayer = read_game("src/game/MultiplayerGame.cpp")
    mp_multiplayer = read_game("src/mpgame/MultiplayerGame.cpp")
    sp_projectile = read_game("src/game/Projectile.cpp")
    mp_projectile = read_game("src/mpgame/Projectile.cpp")
    sp_particle = read_game("src/game/physics/Physics_Particle.cpp")
    mp_particle = read_game("src/mpgame/physics/Physics_Particle.cpp")
    sp_player_physics = read_game("src/game/physics/Physics_Player.cpp")
    mp_player_physics = read_game("src/mpgame/physics/Physics_Player.cpp")
    sp_entity = read_game("src/game/Entity.cpp")
    mp_entity = read_game("src/mpgame/Entity.cpp")
    sp_weapon = read_game("src/game/Weapon.cpp")
    mp_weapon = read_game("src/mpgame/Weapon.cpp")
    sp_af = read_game("src/game/physics/Physics_AF.cpp")
    mp_af = read_game("src/mpgame/physics/Physics_AF.cpp")

    for header, label in (
        (engine_bitmsg_header, "engine bit-message API"),
        (game_bitmsg_header, "game bit-message API"),
    ):
        require(header, "IsReadOverflowed", label)
        require(header, "MarkReadOverflowed", label)
        require(header, "return readBit == 8;", label)
        require(header, "if ( !IsReadOverflowed() )", label)

        delta_overflow = function_body(
            header,
            "ID_INLINE bool idBitMsgDelta::IsReadOverflowed(",
            f"{label} delta overflow policy",
        )
        require(
            delta_overflow,
            "return readDelta != NULL ? readDelta->IsReadOverflowed()",
            f"{label} conditional-tail base exhaustion policy",
        )
        reject(
            delta_overflow,
            "( base != NULL && base->IsReadOverflowed() ) ||",
            f"{label} false aggregate base-overflow rejection",
        )

    for source, label in (
        (engine_bitmsg, "engine bit-message implementation"),
        (game_bitmsg, "game bit-message implementation"),
    ):
        read_bits = function_body(source, "int idBitMsg::ReadBits(", label)
        read_data = function_body(source, "int idBitMsg::ReadData(", label)
        long_counter = function_body(source, "int idBitMsg::ReadDeltaLongCounter(", label)
        require_before(read_bits, "numBits > GetRemainingReadBits()", "return -1;", label)
        require(read_bits, "MarkReadOverflowed();", label)
        require(read_bits, "uint32_t\tvalue;", label)
        require(read_bits, "1u << ( numBits - 1 )", label)
        require(read_data, "length < 0", label)
        require(read_data, "memset( static_cast<byte *>( data ) + remaining, 0, length - remaining );", label)
        require(read_data, "MarkReadOverflowed();", label)
        require(long_counter, "i > 31", label)
        require(long_counter, "1u << i", label)
        reject(long_counter, "1 << i", label)

        delta_write_bits = function_body(source, "void idBitMsgDelta::WriteBits(", label)
        delta_read_bits = function_body(source, "int idBitMsgDelta::ReadBits(", label)
        delta_read_delta = function_body(source, "int idBitMsgDelta::ReadDelta(", label)
        require(
            delta_write_bits,
            "!base->IsReadOverflowed() && baseValue == value",
            f"{label} exhausted base forces explicit replacement",
        )
        for reader, reader_label in (
            (delta_read_bits, "plain delta field"),
            (delta_read_delta, "old-value delta field"),
        ):
            require(reader, "const bool baseOverflowed = base->IsReadOverflowed();", f"{label} {reader_label}")
            require_before(
                reader,
                "readDelta->ReadBits( 1 ) == 0",
                "readDelta->MarkReadOverflowed();",
                f"{label} {reader_label} unavailable-base reuse rejection",
            )

    queue = function_body(game_bitmsg, "void idMsgQueue::ReadFrom(", "game unreliable-message queue")
    require(queue, "encodedSize >= MAX_MSG_QUEUE_SIZE", "game unreliable-message queue bound")
    require(queue, "encodedSize > remaining", "game unreliable-message queue payload bound")
    require(queue, "invalid nested record size", "game unreliable-message nested record bound")
    require(queue, "msg.MarkReadOverflowed();", "game unreliable-message semantic failure propagation")
    require_before(queue, "encodedSize >= MAX_MSG_QUEUE_SIZE", "msg.ReadData( buffer", "queue bound before copy")

    for source, label, client_bound in (
        (sp_network, "SP snapshot", "clientNum < 0 || clientNum >= MAX_CLIENTS"),
        (mp_network, "MP snapshot", "clientNum < 0 || clientNum > MAX_CLIENTS"),
    ):
        snapshot = function_body(source, "bool idGameLocal::ClientReadSnapshot(", label)
        require_before(snapshot, client_bound, "entities[ clientNum ]", f"{label} client bound")
        require(snapshot, "truncated unreliable-message queue", label)
        require(snapshot, "deltaMsg.IsReadOverflowed()", label)
        require(snapshot, "truncated entity state", label)
        require(snapshot, "truncated PVS state", label)
        require(snapshot, "truncated baseline state", label)

    sp_snapshot = function_body(sp_network, "bool idGameLocal::ClientReadSnapshot(", "SP snapshot")
    require_before(sp_snapshot, "targetPlayer < 0 || targetPlayer >= MAX_CLIENTS", "entities[ targetPlayer ]", "SP target-player bound")
    require(sp_snapshot, "non-player entity %d has player state", "SP player-state type check")
    require(sp_player, "msg.ReadBits( -idMath::BitsForInteger( MAX_WEAPONS ) )", "SP ideal-weapon signed wire decode")

    mp_snapshot = function_body(mp_network, "bool idGameLocal::ClientReadSnapshot(", "MP snapshot")
    require_before(mp_snapshot, "clientEntity->IsType( idPlayer::GetClassType() )", "static_cast<idPlayer *>( clientEntity )", "MP local-player type check before cast")
    repeater = function_body(mp_network, "void idGameLocal::ClientReadRepeaterSnapshot(", "MP repeater snapshot")
    require(repeater, "if ( !ClientReadSnapshot(", "MP repeater malformed-snapshot propagation")
    require(repeater, "common->Error( \"ClientReadRepeaterSnapshot: malformed snapshot %d\"", "MP repeater session abort")
    require(repeater, "\n\t\treturn;", "MP repeater decl-validation return")
    reject(repeater, "(void)ClientReadSnapshot", "MP repeater ignored decode result")

    for source, label in ((sp_player, "SP player snapshot"), (mp_player, "MP player snapshot")):
        player = function_body(source, "void idPlayer::ReadFromSnapshot(", label)
        require_before(player, "decodedSpectator < 0 || decodedSpectator >= MAX_CLIENTS", "gameLocal.entities[ spectator ]", label)
        require(player, "newIdealWeapon >= MAX_WEAPONS", label)
        require(player, "msg.MarkReadOverflowed();", label)
        require(player, "static_cast<unsigned int>( snapshotSequence )", f"{label} wrap-safe sequence delta")
        require(player, "physicsObj.DecodeSnapshotState", label)
        require(player, "DecodeBindSnapshotInfo", label)
        require(player, "rvWeapon::DecodeSnapshotAmmo", label)
        require(player, "if ( msg.IsReadOverflowed() ) {\n\t\treturn;\n\t}\n\n\tlastSnapshotSequence = snapshotSequence;", label)
        require_before(player, "if ( msg.IsReadOverflowed() )", "lastSnapshotSequence = snapshotSequence;", label)
        require_before(player, "if ( msg.IsReadOverflowed() )", "physicsObj.ApplySnapshotState", label)
        require_before(player, "if ( msg.IsReadOverflowed() )", "ApplyBindSnapshotInfo", label)
        require_before(player, "if ( msg.IsReadOverflowed() )", "weapon->ApplySnapshotAmmo", label)
        reject(player, "physicsObj.ReadFromSnapshot( msg )", label)
        reject(player, "ReadBindFromSnapshot( msg )", label)
        reject(player, "health = msg.ReadShort()", label)

    for source, label in (
        (sp_multiplayer, "SP multiplayer state"),
        (mp_multiplayer, "MP multiplayer state"),
    ):
        multiplayer = function_body(source, "void idMultiplayerGame::ReadFromSnapshot(", label)
        require(multiplayer, "ingame[ MAX_CLIENTS / 8 ] = { 0 }", label)
        require(multiplayer, "msg.IsReadOverflowed()", label)
        # The decode half must depend on the in-game bitmap alone.  Gating it on
        # gameLocal.entities[ i ] rejected well formed snapshots and disconnected
        # the client: a slot is in-game on the server well before its entity has
        # ever been inside this client's PVS, and the entity is destroyed again on
        # a reliable DELETE_ENT that is not ordered against the snapshot clearing
        # the bit.  The entity is checked in the apply half instead, which is the
        # only place it is dereferenced.
        require(multiplayer, "haveLocalPlayerEntity", label)
        require(multiplayer, "hasTourneyState[ i ] && haveLocalPlayerEntity", label)
        require_before(
            multiplayer,
            "decodedPlayerState[ i ].fragCount = msg.ReadBits(",
            "haveLocalPlayerEntity",
            label,
        )
        require(multiplayer, "newInstance >= MAX_INSTANCES", label)
        require(multiplayer, "mpPlayerState_t decodedPlayerState[ MAX_CLIENTS ]", label)
        require(multiplayer, "hasTourneyState[ MAX_CLIENTS ] = { false }", label)
        require(multiplayer, "if ( msg.IsReadOverflowed() ) {\n\t\treturn;\n\t}\n\n\tisBuyingAllowedRightNow = decodedBuyingAllowed;", label)
        require_before(multiplayer, "if ( msg.IsReadOverflowed() )", "playerState[ i ].ingame = decodedPlayerState", label)
        require_before(multiplayer, "if ( msg.IsReadOverflowed() )", "ent->SetInstance( decodedInstance", label)

    for source, label in (
        (sp_player_physics, "SP player-physics snapshot"),
        (mp_player_physics, "MP player-physics snapshot"),
    ):
        decode = function_body(source, "bool idPhysics_Player::DecodeSnapshotState(", label)
        apply = function_body(source, "void idPhysics_Player::ApplySnapshotState(", label)
        require(decode, "decoded = current;", label)
        require(decode, "return !msg.IsReadOverflowed();", label)
        require_before(apply, "current = decoded;", "clipModel->Link", label)

    for source, label in (
        (sp_entity, "SP bind snapshot"),
        (mp_entity, "MP bind snapshot"),
    ):
        bind_reader = function_body(source, "void idEntity::ReadBindFromSnapshot(", label)
        require_before(bind_reader, "msg.IsReadOverflowed()", "ApplyBindSnapshotInfo", label)

    for source, label in (
        (sp_weapon, "SP weapon snapshot"),
        (mp_weapon, "MP weapon snapshot"),
    ):
        weapon_reader = function_body(source, "void rvWeapon::ReadFromSnapshot(", label)
        require_before(weapon_reader, "msg.IsReadOverflowed()", "ApplySnapshotAmmo", label)

    for source, label in (
        (sp_particle, "SP particle-physics snapshot"),
        (mp_particle, "MP particle-physics snapshot"),
    ):
        decode = function_body(source, "bool rvPhysics_Particle::DecodeSnapshotState(", label)
        apply = function_body(source, "void rvPhysics_Particle::ApplySnapshotState(", label)
        reader = function_body(source, "void rvPhysics_Particle::ReadFromSnapshot(", label)
        require(decode, "decoded = current;", label)
        require(decode, "return !msg.IsReadOverflowed();", label)
        require_before(apply, "current = decoded;", "clipModel->Link", label)
        require_before(reader, "DecodeSnapshotState", "ApplySnapshotState", label)

    for source, label in (
        (sp_projectile, "SP projectile snapshot"),
        (mp_projectile, "MP projectile snapshot"),
    ):
        projectile = function_body(source, "void idProjectile::ReadFromSnapshot(", label)
        require(projectile, "physicsObj.DecodeSnapshotState", label)
        require(projectile, "newLaunchOrig", label)
        require_before(projectile, "if ( msg.IsReadOverflowed() )", "physicsObj.ApplySnapshotState", label)
        require_before(projectile, "if ( msg.IsReadOverflowed() )", "launchOrig = newLaunchOrig", label)
        require_before(projectile, "if ( msg.IsReadOverflowed() )", "Create(", label)

    mp_projectile_reader = function_body(
        mp_projectile,
        "void idProjectile::ReadFromSnapshot(",
        "MP projectile snapshot",
    )
    require(mp_projectile, "msg.WriteBits( ownerNum, idMath::BitsForInteger(MAX_CLIENTS) );", "MP projectile owner wire slot")
    require(mp_projectile_reader, "int ownerNum = MAX_CLIENTS;", "MP projectile owner sentinel")
    require_before(mp_projectile_reader, "ownerNum >= 0", "gameLocal.entities[ ownerNum ]", "projectile owner bound")

    for source, label in (
        (sp_af, "SP articulated-figure snapshot"),
        (mp_af, "MP articulated-figure snapshot"),
    ):
        af_reader = function_body(source, "void idPhysics_AF::ReadFromSnapshot(", label)
        require(af_reader, "AFPState_t decodedCurrent = current;", label)
        require(af_reader, "idList< AFBodyPState_t > decodedBodies;", label)
        require(af_reader, "num != bodies.Num()", label)
        require_before(af_reader, "if ( msg.IsReadOverflowed() )", "current = decodedCurrent;", label)
        require_before(af_reader, "current = decodedCurrent;", "UpdateClipModels();", label)


def validate_rcon2_flow() -> None:
    network = read("src/framework/async/AsyncNetwork.cpp")
    server = read("src/framework/async/AsyncServer.cpp")
    client = read("src/framework/async/AsyncClient.cpp")

    require(network, 'serverAllowLegacyRcon( "net_serverAllowLegacyRcon", "0"', "legacy server rcon default-off")
    require(network, 'clientUseLegacyRcon( "net_clientUseLegacyRcon", "0"', "legacy client rcon default-off")
    for name in ("serverRemoteConsolePassword", "clientRemoteConsolePassword"):
        line = next((line for line in network.splitlines() if name in line and "idCVar" in line), "")
        require(line, "CVAR_PRIVATE", f"{name} private flag")
        require(line, "CVAR_CASE_SENSITIVE", f"{name} case-sensitive flag")

    refresh = function_body(server, "bool idAsyncServer::RefreshRcon2Verifier(", "cached rcon2 verifier")
    challenge = function_body(server, "void idAsyncServer::ProcessRemoteConsole2ChallengeMessage(", "rcon2 challenge")
    proof = function_body(server, "void idAsyncServer::ProcessRemoteConsole2Message(", "rcon2 proof")
    legacy = function_body(server, "void idAsyncServer::ProcessRemoteConsoleMessage(", "legacy rcon server")
    remote = function_body(client, "void idAsyncClient::RemoteConsole(", "rcon client")
    client_reply = function_body(client, "void idAsyncClient::ConnectionlessMessage(", "client OOB source gate")
    disconnect = function_body(client, "void idAsyncClient::ProcessDisconnectMessage(", "client disconnect handler")
    clear = function_body(client, "void idAsyncClient::ClearRemoteConsoleRequest(", "rcon client cleanup")

    require_before(refresh, "rcon2VerifierInitialized", "DeriveVerifier", "cached verifier before expensive KDF")
    require(refresh, "serverRemoteConsolePassword.IsModified()", "password-change verifier invalidation")
    for token in (
        "AsyncServer_SameEndpoint( from, candidate.address )",
        "RCON2_CHALLENGE_TIMEOUT_MSEC",
        "Sys_GetSecureRandomBytes( randomValues",
        "issued.clientNonce",
        "issued.serverNonce",
        "issued.endpointBinding",
        "issued.requestDigest",
    ):
        require(challenge, token, "bound rcon2 challenge")
    require_before(proof, "idCrypto::SecureZero( &rcon2Challenges[ slot ]", "msg.ReadString( command", "one-shot proof consumption")
    require(proof, "AsyncServer_SameEndpoint( from, candidate.address )", "exact rcon2 proof endpoint")
    require(proof, "idRcon2::HashRequest( command, requestDigest )", "rcon2 command binding")
    require(proof, "idCrypto::ConstantTimeEquals( suppliedProof", "constant-time proof comparison")
    require(proof, "RecordRconFailure( from )", "failed-proof throttling")
    require(proof, "SendRemoteConsole2Complete", "transaction completion marker")

    require(legacy, "serverAllowLegacyRcon.GetBool()", "explicit server legacy opt-in")
    require(legacy, "ConstantTimeEquals", "legacy password comparison")
    require(remote, "clientUseLegacyRcon.GetBool()", "explicit client legacy opt-in")
    require(remote, "Sys_GetSecureRandomBytes( rcon2Request.clientNonce", "client rcon2 nonce")
    require(clear, "SecureZero( &rcon2Request", "rcon2 state wipe")
    require(clear, "memset( &lastRconAddress", "rcon reply-window cleanup")
    require(client_reply, "AsyncClient_SameEndpoint( from, rcon2Request.address )", "exact rcon reply source")
    require(client_reply, "rcon2Request.state == RCON_REPLY_OUTPUT", "post-proof output window")
    require(client_reply, "!fromCurrentServer && !fromPendingRconOutput", "pre-proof print rejection")
    require_before(
        client_reply,
        'if ( idStr::Icmp( string, "rcon2ChallengeResponse" ) == 0 )',
        "if ( !fromCurrentServer )",
        "rcon opcode dispatch before game endpoint gate",
    )
    require_before(
        client_reply,
        "if ( !fromCurrentServer )",
        'if ( idStr::Icmp( string, "challengeResponse" ) == 0 )',
        "exact game endpoint gate before game control dispatch",
    )
    rcon_challenge_dispatch = client_reply[
        client_reply.index('if ( idStr::Icmp( string, "rcon2ChallengeResponse" ) == 0 )') :
        client_reply.index('if ( idStr::Icmp( string, "rcon2Complete" ) == 0 )')
    ]
    require(rcon_challenge_dispatch, "if ( !fromPendingRcon )", "pending-rcon-only challenge reply")
    rcon_complete_dispatch = client_reply[
        client_reply.index('if ( idStr::Icmp( string, "rcon2Complete" ) == 0 )') :
        client_reply.index('if ( idStr::Icmp( string, "print" ) == 0 )')
    ]
    require(rcon_complete_dispatch, "if ( !fromPendingRcon )", "pending-rcon-only completion reply")
    reject(
        client_reply,
        "if ( !fromCurrentServer && !fromPendingRcon )",
        "union endpoint capability gate",
    )
    require(disconnect, "AsyncClient_SameEndpoint( from, serverAddress )", "exact disconnect endpoint")
    reject(disconnect, "Sys_CompareNetAdrBase( from, serverAddress )", "base-address-only disconnect gate")
    reject(server, '"rcon from %s: %s', "remote command logging")
    reject(server, '"bad rcon from %s: %s', "remote password logging")


def validate_pure_admission_fail_closed() -> None:
    server = read("src/framework/async/AsyncServer.cpp")
    connect = function_body(server, "void idAsyncServer::ProcessConnectMessage(", "connect admission")
    map_change = function_body(server, "void idAsyncServer::ExecuteMapChange(", "map-change pure admission")
    unreliable = function_body(
        server,
        "void idAsyncServer::ProcessUnreliableClientMessage(",
        "wrong-gameinit pure admission",
    )

    resend = connect[
        connect.index("case CDK_PUREWAIT:") : connect.index("case CDK_ONLYLAN:")
    ]
    for token in (
        "if ( !SendPureServerMessage( from, OS ) )",
        "AsyncServer_ClearChallenge( challenges[ ichallenge ] );",
        "return;",
    ):
        require(resend, token, "fail-closed pure challenge resend")

    initial_start = connect.rindex(
        'if ( sessLocal.mapSpawnData.serverInfo.GetInt( "si_pure" ) && challenges[ ichallenge ].authState != CDK_PUREOK )'
    )
    initial = connect[initial_start : connect.index("// push back decl checksum", initial_start)]
    for token in (
        "if ( !SendPureServerMessage( from, OS ) )",
        "AsyncServer_ClearChallenge( challenges[ ichallenge ] );",
        "challenges[ ichallenge ].authState = CDK_PUREWAIT;",
        "return;",
    ):
        require(initial, token, "fail-closed initial pure challenge")
    require_before(
        initial,
        "if ( !SendPureServerMessage( from, OS ) )",
        "challenges[ ichallenge ].authState = CDK_PUREWAIT;",
        "pure send before wait-state admission",
    )
    reject(initial, "if ( SendPureServerMessage( from, OS ) )", "fallthrough-capable initial pure send")

    for body, context in (
        (map_change, "map-change reliable pure send"),
        (unreliable, "wrong-gameinit reliable pure send"),
    ):
        require(body, "if ( !SendReliablePureToClient(", context)
        require(body, "DropClient(", context)
        reject(body, "clientState = SCS_CONNECTED;\n\t\t\t\t}", f"{context} failure promotion")


def validate_private_cvar_handling() -> None:
    header = read("src/framework/CVarSystem.h")
    cvars = read("src/framework/CVarSystem.cpp")
    matcher = read("src/idlib/PrivateCommand.h")
    args = read("src/idlib/CmdArgs.cpp")
    strings = read("src/idlib/Str.h")
    commands = read("src/framework/CmdSystem.cpp")
    console = read("src/framework/Console.cpp")

    require(header, "CVAR_PRIVATE", "private CVar flag")
    require(cvars, '( internal->GetFlags() & CVAR_PRIVATE ) ? "<redacted>"', "direct CVar query redaction")
    require(cvars, "!( cvar->GetFlags() & CVAR_PRIVATE )", "private CVar serialization omission")
    require(cvars, "ContainsBoundedCaseInsensitiveToken", "private command matcher integration")
    require(cvars, "expandedArgs.TokenizeString( commandText, false )", "expanded private-target classification")
    require(cvars, "expandedArgs.Argv( argIndex )", "expanded private-target token scan")
    require(cvars, "expandedArgs.ClearSensitive()", "expanded command scratch wipe")
    require(cvars, "( flags & CVAR_CASE_SENSITIVE ) ?", "case-sensitive CVar update selection")
    require(cvars, "valueString.Cmp( newValue ) == 0", "case-only private password update")
    require(cvars, "CVar_AssignString( valueString, newValue", "private CVar replacement wipe")
    require(cvars, "valueString.SecureClear()", "private CVar destructor wipe")
    require(cvars, "resetString.SecureClear()", "private CVar reset-value wipe")
    require(cvars, "toggle is unavailable for private CVar", "private toggle rejection")
    require(matcher, "nameBytes > commandBytes", "short-command matcher bound")
    require(matcher, "offset <= commandBytes - nameBytes", "bounded private-token scan")
    require(matcher, "rightOffset == commandBytes", "safe right-boundary check")
    require(args, 'token = "<redacted>"', "$ private-CVar expansion redaction")
    require(args, "cmd_args.SecureClear()", "Args shared-scratch wipe")
    args_body = function_body(args, "const char *idCmdArgs::Args(", "Args shared scratch")
    require_before(args_body, "cmd_args.SecureClear()", "cmd_args +=", "scratch wipe before Args reuse")
    require(strings, "ID_INLINE void idStr::SecureClear", "full idStr allocation wipe")
    require(strings, "data != baseBuffer ? alloced", "dynamic idStr allocation wipe")
    require(commands, "vstr is unavailable for private CVar", "private vstr rejection")
    require(commands, "idCmdArgs::ClearArgsScratch()", "private command scratch cleanup")
    completion_info = function_body(
        console,
        "bool idConsoleLocal::GetCompletionCvarInfo(",
        "console completion CVar information",
    )
    require(
        completion_info,
        '( cvar->GetFlags() & CVAR_PRIVATE ) ?\n\t\t\t"<redacted>" : cvar->GetString()',
        "private completion-popup value redaction",
    )

    evidence = {
        "src/framework/Console.cpp": (
            "CommandContainsPrivateCVar",
            "]<private command redacted>",
            "removedPrivateCommand",
        ),
        "src/sys/win32/win_syscon.cpp": (
            "CommandContainsPrivateCVar",
            "]<private command redacted>",
            "if ( !privateCommand )",
        ),
        "src/sys/posix/posix_syscon.cpp": (
            "CommandContainsPrivateCVar",
            "]<private command redacted>",
            "if ( !privateCommand )",
        ),
        "src/sys/posix/posix_main.cpp": (
            "CommandContainsPrivateCVar",
            "memset( s, 0, len )",
            "if ( !privateCommand )",
        ),
        "src/framework/Common.cpp": (
            "<private command redacted>",
            "com_consoleLines[ i ].ClearSensitive()",
        ),
        "src/framework/EventLoop.cpp": (
            "EventLoop_IsPrivateConsoleEvent",
            "PRIVATE_EVENT_TEXT",
            "memset( ev.evPtr, 0, ev.evPtrLength )",
        ),
        "src/framework/EditField.cpp": (
            "memset( buffer, 0, sizeof( buffer ) )",
            "memset( &autoComplete, 0, sizeof( autoComplete ) )",
            '"<redacted>" : cvarSystem->GetCVarString( s )',
            "autocomplete is unavailable for private CVar commands",
        ),
        "src/framework/CmdSystem.cpp": (
            "CommandContainsPrivateCVar",
            "ClearSensitive()",
            "memset( textBuf + textLength, 0",
        ),
    }
    for path, tokens in evidence.items():
        source = read(path)
        for token in tokens:
            require(source, token, f"private-data lifecycle in {path}")


def validate_remote_dictionary_authority() -> None:
    engine_header = read("src/framework/CVarSystem.h")
    game_header = read_game("src/framework/CVarSystem.h")
    cvars = read("src/framework/CVarSystem.cpp")
    policy = read("src/framework/RemoteCVarPolicy.h")
    native = read("tools/tests/native/CoreSafetyTest.cpp")
    client = read("src/framework/async/AsyncClient.cpp")
    server = read("src/framework/async/AsyncServer.cpp")
    demo = read("src/framework/async/MultiViewDemo.cpp")
    engine_bitmsg = read("src/idlib/BitMsg.cpp")
    game_bitmsg = read_game("src/idlib/BitMsg.cpp")
    sp_network = read_game("src/game/Game_network.cpp")
    mp_network = read_game("src/mpgame/Game_network.cpp")

    for header, label in (
        (engine_header, "engine CVar interface"),
        (game_header, "game CVar interface"),
    ):
        require(header, "SetCVarsFromDictByFlags", label)
        require(header, "ABI rule: append new virtual methods here", label)
        interface = header.split("class idCVarSystem {", 1)[1].split("};", 1)[0]
        legacy_tail = interface.index("SetCVarsFromDict( const idDict &dict )")
        flagged_slot = interface.index("SetCVarsFromDictByFlags")
        private_slot = interface.index("CommandContainsPrivateCVar")
        if not legacy_tail < flagged_slot < private_slot:
            raise AssertionError(f"{label} security methods are not appended after the legacy v43 tail")

    require(policy, "IsSingleAllowedAuthority", "production remote-CVar policy")
    require(policy, "CanApply", "production remote-CVar policy")
    for token in (
        '"matching userinfo authority"',
        '"matching serverinfo authority"',
        '"matching networksync authority"',
        '"cross-class authority"',
        '"local-only CVar"',
        '"private CVar"',
        '"combined authority"',
        '"unknown authority"',
    ):
        require(native, token, "native remote-CVar authority coverage")

    legacy_apply = function_body(cvars, "void idCVarSystemLocal::SetCVarsFromDict(", "legacy dictionary apply")
    flagged_apply = function_body(cvars, "bool idCVarSystemLocal::SetCVarsFromDictByFlags(", "flagged dictionary apply")
    require(legacy_apply, "CVAR_USERINFO | CVAR_SERVERINFO | CVAR_NETWORKSYNC", "legacy remote-class ceiling")
    require(legacy_apply, "CVAR_PRIVATE", "legacy private-CVar exclusion")
    require(flagged_apply, "CVAR_USERINFO | CVAR_SERVERINFO | CVAR_NETWORKSYNC", "remote-class allowlist")
    require(flagged_apply, "idRemoteCVarPolicy::IsSingleAllowedAuthority", "single remote authority")
    require(flagged_apply, "idRemoteCVarPolicy::CanApply", "per-CVar authority check")
    require(flagged_apply, "CVAR_PRIVATE", "private-CVar exclusion")

    require(client, "SetCVarsFromDictByFlags( info, CVAR_USERINFO )", "client userinfo authority")
    require(client, "SetCVarsFromDictByFlags( info, CVAR_NETWORKSYNC )", "client sync authority")
    require(server, "SetCVarsFromDictByFlags( *gameInfo, CVAR_USERINFO )", "listen-server userinfo authority")
    # Playback start, replay reset and a keyframe jump each restore recorded map state.
    if demo.count("SetCVarsFromDictByFlags( sessLocal.mapSpawnData.syncedCVars, CVAR_NETWORKSYNC )") != 3:
        raise AssertionError("MVD start/reset/keyframe must each apply only NETWORKSYNC CVars")
    if demo.count("SetCVarsFromDictByFlags(") != 3:
        raise AssertionError("MVD playback applies recorded CVars outside the NETWORKSYNC paths")

    for source, label in (
        (engine_bitmsg, "engine delta dictionary"),
        (game_bitmsg, "game delta dictionary"),
    ):
        decoder = function_body(source, "bool idBitMsg::ReadDeltaDict(", label)
        require(decoder, "idDict\t\tdecoded;", f"{label} transaction scratch")
        require(decoder, "if ( IsReadOverflowed() )", f"{label} underflow rejection")
        require_before(decoder, "if ( IsReadOverflowed() )", "dict = decoded;", f"{label} commit after validation")
        if decoder.count("return false;") < 3:
            raise AssertionError(f"{label} does not reject every truncated key/value/tail boundary")

    reliable_client = function_body(client, "void idAsyncClient::ProcessReliableServerMessages(", "client reliable dispatcher")
    clientinfo = reliable_client[
        reliable_client.index("case SERVER_RELIABLE_MESSAGE_CLIENTINFO") :
        reliable_client.index("case SERVER_RELIABLE_MESSAGE_SYNCEDCVARS")
    ]
    synced = reliable_client[
        reliable_client.index("case SERVER_RELIABLE_MESSAGE_SYNCEDCVARS") :
        reliable_client.index("case SERVER_RELIABLE_MESSAGE_PRINT")
    ]
    require_before(clientinfo, "msg.IsReadOverflowed()", "SetCVarsFromDictByFlags", "userinfo validation before CVar commit")
    require_before(synced, "msg.IsReadOverflowed()", "SetCVarsFromDictByFlags", "sync validation before CVar commit")

    reliable_server = function_body(server, "void idAsyncServer::ProcessReliableClientMessages(", "server reliable dispatcher")
    server_clientinfo = reliable_server[
        reliable_server.index("case CLIENT_RELIABLE_MESSAGE_CLIENTINFO") :
        reliable_server.index("case CLIENT_RELIABLE_MESSAGE_PRINT")
    ]
    require_before(server_clientinfo, "msg.IsReadOverflowed()", "SendUserInfoBroadcast", "client userinfo validation before broadcast")

    for source, label in ((sp_network, "SP reliable server-info"), (mp_network, "MP reliable server-info")):
        serverinfo = source[source.index("case GAME_RELIABLE_MESSAGE_SERVERINFO") :]
        serverinfo = serverinfo[: serverinfo.index("case GAME_RELIABLE_MESSAGE_RESTART")]
        require_before(serverinfo, "msg.IsReadOverflowed()", "SetServerInfo( info )", label)

    pure = function_body(server, "void idAsyncServer::ProcessReliablePure(", "reliable pure state recovery")
    require(pure, "outMsg.WriteByte( SERVER_RELIABLE_MESSAGE_RELOAD )", "reliable reload opcode")
    require(pure, "SendReliableMessage( clientNum, outMsg )", "reliable reload buffer identity")
    reject(pure, "SendReliableMessage( clientNum, msg )", "client-controlled reliable echo")


def validate_channel_and_message_abuse_bounds() -> None:
    """What one peer can make the other spend, store or index (audit of 9 November 2026)."""
    server = read("src/framework/async/AsyncServer.cpp")
    channel = read("src/framework/async/MsgChannel.cpp")
    compressor = (ROOT / "src/framework/Compressor.cpp").read_text(encoding="latin-1")
    meson = read("meson.build")
    native = read("tools/tests/native/CompressorTest.cpp")

    # A dropped client's remaining reliable messages are not processed.
    reliable_server = function_body(server, "void idAsyncServer::ProcessReliableClientMessages(", "server reliable dispatcher")
    require(
        reliable_server,
        "while ( client.clientState > SCS_ZOMBIE && client.channel.GetReliableMessage( msg ) )",
        "reliable dispatch stops at a dropped client",
    )

    # User info is bounded before it is stored and rebroadcast.
    server_clientinfo = reliable_server[
        reliable_server.index("case CLIENT_RELIABLE_MESSAGE_CLIENTINFO") :
        reliable_server.index("case CLIENT_RELIABLE_MESSAGE_PRINT")
    ]
    require_before(
        server_clientinfo,
        "!AsyncServer_UserInfoWithinLimits( info )",
        "SendUserInfoBroadcast",
        "client userinfo size bound before broadcast",
    )
    limits = function_body(server, "static bool AsyncServer_UserInfoWithinLimits(", "userinfo limits")
    require(limits, "numKeyVals > CLIENT_USERINFO_MAX_KEYS", "userinfo key bound")
    require(limits, "bytes <= CLIENT_USERINFO_MAX_BYTES", "userinfo byte bound")
    limit_bytes = int(re.search(r"const int CLIENT_USERINFO_MAX_BYTES\s*=\s*(\d+);", server).group(1))
    assert 1024 <= limit_bytes <= 8192, "a whole userinfo must fit one reliable message with room to spare"

    # The zero-run coder can grow a message, so it packs into its own buffer and
    # an oversized result fails the send instead of the process.
    write = function_body(channel, "bool idMsgChannel::WriteMessageData(", "channel message packing")
    require(write, "byte packedBuf[MAX_MESSAGE_SIZE * 2 + 64];", "packing buffer covers worst-case growth")
    require(write, "idFile_BitMsg file( packed );", "compressor writes the private buffer")
    reject(write, "idFile_BitMsg file( out );", "compressor writing the caller's packet")
    require_before(write, "> out.GetMaxSize() - out.GetSize()", "out.WriteData( packed.GetData(), packed.GetSize() )", "fit check before copy")
    send = function_body(channel, "int idMsgChannel::SendMessage(", "channel send")
    require_before(send, "if ( !WriteMessageData( unsentMsg, msg ) )", "unsentFragments = true;", "failed packing leaves no fragments pending")

    # Reopening a channel keeps its compressor.
    init = function_body(channel, "void idMsgChannel::Init(", "channel init")
    require(init, "if ( this->compressor == NULL )", "channel init reuses its compressor")
    require(function_body(channel, "idMsgChannel::idMsgChannel()", "channel constructor"), "compressor = NULL;", "channel constructor")

    # A source that has run dry is not asked for a whole buffer per byte, and the
    # LZW decoder refuses codes its dictionary cannot hold.
    read_bits = function_body(compressor, "int idCompressor_BitStream::ReadBits(", "bit-stream refill")
    require(read_bits, "file->Read( buffer, readDry ? 1 : (int)sizeof( buffer ) )", "dry refill size")
    require(read_bits, "readDry = ( readLength <= 0 );", "dry refill state")
    block = function_body(compressor, "void idCompressor_LZW::DecompressBlock(", "LZW block decode")
    require_before(
        block,
        "code > nextCode || ( oldCode == -1 && code >= LZW_FIRST_CODE )",
        "WriteChain(",
        "LZW code validation before the chain walk",
    )
    require(meson, "test('openq4-compressor', openq4_compressor_test, timeout: 30)", "native compressor test registration")
    for token in ("TestZeroRunDrySource", "TestLZWCorrupt", "zero-run output stays within twice its input"):
        require(native, token, "native compressor coverage")

    # A reliable message that ends the connection ends the packet: nothing
    # queued behind it, and not the packet's unreliable part, is processed.
    client = read("src/framework/async/AsyncClient.cpp")
    require(
        function_body(client, "void idAsyncClient::ProcessReliableServerMessages(", "client reliable dispatcher"),
        "while ( clientState >= CS_CONNECTED && channel.GetReliableMessage( msg ) )",
        "client reliable dispatch stops at a disconnect",
    )
    process = function_body(client, "void idAsyncClient::ProcessMessage(", "client packet entry")
    tail = process[process.index("ProcessReliableServerMessages();") :]
    require_before(tail, "if ( clientState < CS_CONNECTED ) {", "ProcessUnreliableServerMessage( msg );",
                   "client drops the unreliable part after a disconnect")

    # A pure list naming one pak twice is refused, not carried into a restart
    # that cannot satisfy it; and the lock taken while connecting is released
    # when the attempt ends before a full connection.
    pure_list = function_body(client, "bool idAsyncClient::ValidatePureServerChecksums(", "client pure list")
    require_before(pure_list, "if ( inChecksums[ j ] == i ) {", "fileSystem->SetPureServerChecksums(",
                   "duplicate pure checksum refused before the list is applied")
    disconnect = function_body(client, "void idAsyncClient::DisconnectFromServer(", "client disconnect")
    require(
        disconnect,
        "if ( clientState == CS_CHALLENGING || clientState == CS_CONNECTING ) {\n\t\tfileSystem->ClearPureChecksums();",
        "pure lock released when a connection attempt ends",
    )

    # Client-side handlers bound what a server or a demo indexes with.
    for tree in ("src/game", "src/mpgame"):
        network = (ROOT / tree / "Game_network.cpp").read_text(encoding="latin-1")
        death = network[network.index("case GAME_RELIABLE_MESSAGE_DEATH") :]
        death = death[: death.index("mpGame.ReceiveDeathMessage(")]
        reject(death, "static_cast<idPlayer*>(gameLocal.entities[ attackerEntityNumber ])", f"{tree} unchecked death attacker cast")
        require(death, "attackerEntityNumber < MAX_CLIENTS", f"{tree} death attacker bound")
        require(death, "victimEntityNumber < MAX_CLIENTS", f"{tree} death victim bound")
        assert death.count("->IsType( idPlayer::GetClassType() )") == 2, f"{tree} death message type checks"

        stats = (ROOT / tree / "mp/stats/StatManager.cpp").read_text(encoding="latin-1")
        award = function_body(stats, "void rvStatManager::ReceiveInGameAward(", f"{tree} in-game award")
        require_before(
            award,
            "award >= IGA_NUM_AWARDS || client < 0 || client >= MAX_CLIENTS",
            "localInGameAwards[ award ]",
            f"{tree} award bounds before use",
        )

        state = (ROOT / tree / "mp/GameState.cpp").read_text(encoding="latin-1")
        history = state[state.rindex("case MSG_TOURNEY_HISTORY") :]
        history = history[: history.index("case MSG_TOURNEY_TOURNEYCOUNT")]
        require_before(history, "maxr > MAX_ROUNDS", "tourneyHistory[ i ][ j ]", f"{tree} tourney history rounds bound")
        reject(history, "> MAX_ARENAS", f"{tree} tourney history bounded by the arena count")

    # Emotes are validated, throttled and rebuilt before they are forwarded.
    player = (ROOT / "src/mpgame/Player.cpp").read_text(encoding="latin-1")
    receive = function_body(player, "bool idPlayer::ServerReceiveEvent(", "player server events")
    emote = receive[receive.index("case EVENT_EMOTE") :]
    emote = emote[: emote.index("default:")]
    require_before(emote, "newEmote > PE_TAUNT", "ServerSendInstanceEvent(", "emote validation before forwarding")
    require_before(emote, "RELIABLE_FLOOD_EMOTE", "ServerSendInstanceEvent(", "emote throttle before forwarding")
    require(emote, "ServerSendInstanceEvent( EVENT_EMOTE, &forwardMsg, false, entityNumber )", "emote forwards a rebuilt message")
    reject(emote, "ServerSendInstanceEvent( EVENT_EMOTE, &msg,", "emote forwarding the sender's bytes")


def validate_documentation_and_registration() -> None:
    guide = read("docs/user/server-security.md")
    setup = read("docs/user/server-setup.md")
    for token in (
        "not an encrypted transport",
        "PBKDF2-throttled offline password guessing",
        "24 or more characters",
        "never launch with",
        "net_serverAllowLegacyRcon 1",
        "net_clientUseLegacyRcon 1",
        "FIPS 180-4",
        "RFC 2104",
        "RFC 8018",
    ):
        require(guide, token, "rcon2 administrator guidance")
    require(setup, "server-security.md", "server setup security-guide link")

    for path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(path)
        require(workflow, "tools/tests/network_security.py \\", f"{path} syntax-check registration")
        require(workflow, "python tools/tests/network_security.py", f"{path} execution registration")
    require(read("tools/validation/openq4_validate.py"), '"network_security.py"', "local validation registration")


def validate_network_rate_budget() -> None:
    """The rate cap must clear a full server's snapshot, and the one-time
    migration off the legacy value must name the same figure as the default.

    The channel's token bucket refuses whole snapshots until the previous packet
    has been paid for at this rate, so a cap below snapshotSize * snapshotHz does
    not shed bytes - it sheds updates, and with them every replicated projectile,
    effect and event.  Measured at the legacy 25600 B/s: 2-3 snapshots a second
    against the 20 asked for, on ~2.9KB snapshots.
    """
    label = "network rate budget"
    async_network = read("src/framework/async/AsyncNetwork.cpp")
    common = read("src/framework/Common.cpp")

    require(async_network, 'serverMaxClientRate( "net_serverMaxClientRate", "128000"', label)
    require(async_network, 'clientMaxRate( "net_clientMaxRate", "128000"', label)
    require(async_network, 'serverSnapshotDelay( "net_serverSnapshotDelay", "33"', label)

    require(common, "static const int legacyNetworkRate = 25600;", label)
    require(common, "static const int modernNetworkRate = 128000;", label)
    require(common, "Common_MigrateLegacyNetworkRateCaps();", label)

    # The snapshot rate the server asks for has to be affordable inside the cap
    # for a snapshot big enough to matter, or the limiter silently wins.
    snapshot_hz = 1000 // 33
    assert 128000 // snapshot_hz >= 4096, (
        f"{label}: cap leaves only {128000 // snapshot_hz} bytes per snapshot"
    )


def main() -> None:
    validate_crypto_and_native_vectors()
    validate_secure_random_and_connection_challenges()
    validate_oob_budgets()
    validate_retail_protocol_refusal()
    validate_usercmd_packet_bounds()
    validate_snapshot_decode_bounds()
    validate_rcon2_flow()
    validate_pure_admission_fail_closed()
    validate_private_cvar_handling()
    validate_remote_dictionary_authority()
    validate_network_rate_budget()
    validate_channel_and_message_abuse_bounds()
    validate_documentation_and_registration()
    print("network_security: ok")


if __name__ == "__main__":
    main()
