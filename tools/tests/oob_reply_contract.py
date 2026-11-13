#!/usr/bin/env python3
"""Guards how the server and client use the out-of-band reply budget.

oob_reply_budget_state.py executes src/framework/async/OutOfBandReply.h and the
roster trimming against the real server info. This checks the wiring those
cases cannot see: the server's info reply keeps the browser's keys, cuts names
and fills the rest within the budget; a connect request carries the capability
tag after the field servers never read; a capable client gets a connect reply
with no game-init id, the loading keys and a roster held to the budget,
followed at once by the game-init message, which the server sends again
(without restarting the pure checks) while the client still reports no game;
and the client waits for that message, then loads without the pure prompt and
keeps its connect roster. Single-reversal controls then mutate the sources and
require each check to fail.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    require(haystack, first, context)
    require(haystack, second, context)
    if haystack.index(first) > haystack.index(second):
        raise AssertionError(f"{first!r} must come before {second!r} in {context}")


def body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def validate(server: str, client: str, server_header: str, client_header: str) -> None:
    require(server, '#include "OutOfBandReply.h"', "server includes the budget")
    require(client, '#include "OutOfBandReply.h"', "client includes the budget")

    fitted = body(server, "static int AsyncServer_WriteFittedDict(", "fitted dictionary")
    require(fitted, "idOutOfBandReply::BUDGET_BYTES - msg.GetSize() - tailBytes", "fitted dictionary budget")
    require(fitted, "idOutOfBandReply::ChooseKeys(", "fitted dictionary choice")
    require(fitted, "msg.WriteDeltaDict( fitted, NULL );", "fitted dictionary write")

    info = body(server, "void idAsyncServer::ProcessGetInfoMessage(", "info reply")
    reject(info, "outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL );", "info reply writes the whole dictionary")
    require(info, "idOutOfBandReply::NameLimit(", "info reply cuts names to fit")
    require(info, "idOutOfBandReply::BROWSER_KEYS,", "info reply keeps the browser's keys")
    require(info, "AsyncServer_WriteFittedDict( outMsg, serverInfo,", "info reply fits its dictionary")
    require(info, "nameLimit >= 0 ? nameLimit + 1 : -1", "info reply writes cut names")
    require_before(info, "AsyncServer_WriteFittedDict( outMsg, serverInfo,", "outMsg.WriteLong( fileSystem->GetOSMask() );",
                   "info reply layout")

    roster = body(server, "static void AsyncServer_WriteConnectRoster(", "connect roster")
    require(roster, "idConnectRoster::EncodeWithin(", "connect roster trims to the budget")

    connect = body(server, "void idAsyncServer::ProcessConnectMessage(", "connect")
    require_before(connect, "msg.ReadString( password, sizeof( password ) );",
                   "msg.ReadLong() == idOutOfBandReply::CAPABILITY_TAG", "capability follows the password")
    require(connect, "capabilities = msg.ReadLong() & idOutOfBandReply::CAPABILITIES;", "capability mask")
    require(connect, "outMsg.WriteLong( deferred ? GAME_INIT_ID_INVALID : gameInitId );", "deferred reply names no game")
    require(connect, "idOutOfBandReply::LOADING_KEYS,", "deferred reply carries the loading keys")
    require(connect, "AsyncServer_WriteConnectRoster( outMsg, clients, clientNum, idOutOfBandReply::BUDGET_BYTES );",
            "deferred roster held to the budget")
    require(connect, 'common->Printf( "sending connect response to %s\\n", Sys_NetAdrToString( from ) );',
            "harnesses count the connect response line")
    require_before(connect, "InitClient( clientNum, clientId, clientRate );", "clients[clientNum].deferredServerInfo = true;",
                   "deferred flag survives InitClient")
    require_before(connect, "clients[clientNum].deferredServerInfo = true;", "SendGameInitToClient( clientNum );",
                   "game-init follows a deferred reply")

    unreliable = body(server, "void idAsyncServer::ProcessUnreliableClientMessage(", "unreliable client message")
    require_before(unreliable, "if ( client.deferredServerInfo ) {", 'if ( sessLocal.mapSpawnData.serverInfo.GetBool( "si_pure" ) ) {',
                   "a lost game-init is sent again without new pure checks")
    require(unreliable, "client.deferredServerInfo = false;", "the flag ends in game")
    for name in ("void idAsyncServer::InitClient(", "void idAsyncServer::ClearClient("):
        require(body(server, name, name), "client.deferredServerInfo = false;", name)
    require(server_header, "bool				deferredServerInfo;", "server client field")

    setup = body(client, "void idAsyncClient::SetupConnection(", "connect request")
    require_before(setup, "msg.WriteShort( 0 );", "msg.WriteLong( idOutOfBandReply::CAPABILITY_TAG );",
                   "capability after the unread field")
    require(setup, "msg.WriteLong( idOutOfBandReply::CAPABILITIES );", "capability mask sent")

    response = body(client, "void idAsyncClient::ProcessConnectResponseMessage(", "connect reply")
    require(response, "if ( serverGameInitId == GAME_INIT_ID_INVALID ) {", "deferred reply recognized")
    require_before(response, "deferredServerInfo = true;", "InitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );",
                   "a deferred reply loads nothing")
    require(response, "gameInitId = GAME_INIT_ID_INVALID;", "a waiting client reports no game")

    unreliable_client = body(client, "void idAsyncClient::ProcessUnreliableServerMessage(", "client unreliable message")
    require(unreliable_client, "pureWait = !completesConnect && serverSI.GetBool( \"si_pure\" );", "no pure prompt after a deferred reply")
    require(unreliable_client, "if ( !completesConnect ) {", "the connect roster is kept")
    require(body(client, "void idAsyncClient::Clear(", "client clear"), "deferredServerInfo = false;", "client clear")
    require(client_header, "bool				deferredServerInfo;", "client field")


def expect_failure(sources: tuple[str, str, str, str], label: str) -> None:
    try:
        validate(*sources)
    except AssertionError:
        return
    raise AssertionError(f"control did not fail: {label}")


def main() -> int:
    sources = (read("src/framework/async/AsyncServer.cpp"), read("src/framework/async/AsyncClient.cpp"),
               read("src/framework/async/AsyncServer.h"), read("src/framework/async/AsyncClient.h"))
    controls = (
        ("the info reply writes the whole dictionary", 0,
         "const int omitted = AsyncServer_WriteFittedDict( outMsg, serverInfo,",
         "outMsg.WriteDeltaDict( sessLocal.mapSpawnData.serverInfo, NULL ); const int omitted = AsyncServer_WriteFittedDict( outMsg, serverInfo,"),
        ("the server never reads the capability", 0,
         "capabilities = msg.ReadLong() & idOutOfBandReply::CAPABILITIES;", "msg.ReadLong();"),
        ("a deferred reply still names the game", 0,
         "outMsg.WriteLong( deferred ? GAME_INIT_ID_INVALID : gameInitId );", "outMsg.WriteLong( gameInitId );"),
        ("no game-init follows a deferred reply", 0,
         "\t\tclients[clientNum].deferredServerInfo = true;\n\t\tSendGameInitToClient( clientNum );",
         "\t\tclients[clientNum].deferredServerInfo = true;"),
        ("a lost game-init restarts the pure checks", 0,
         "\t\t\tif ( client.deferredServerInfo ) {\n\t\t\t\treturn;\n\t\t\t}\n", ""),
        ("the roster ignores the budget", 0,
         "AsyncServer_WriteConnectRoster( outMsg, clients, clientNum, idOutOfBandReply::BUDGET_BYTES );",
         "AsyncServer_WriteConnectRoster( outMsg, clients, clientNum );"),
        ("the client never asks", 1, "msg.WriteLong( idOutOfBandReply::CAPABILITIES );", ""),
        ("a deferred reply loads a map without server info", 1,
         "\t\tdeferredServerInfo = true;\n\t\tgameInitId = GAME_INIT_ID_INVALID;",
         "\t\tdeferredServerInfo = true;\n\t\tInitGame( serverGameInitId, serverGameFrame, serverGameTime, serverSI );"),
        ("the pure prompt waits forever after a deferred reply", 1,
         "pureWait = !completesConnect && serverSI.GetBool( \"si_pure\" );", "pureWait = serverSI.GetBool( \"si_pure\" );"),
    )
    try:
        validate(*sources)
        for label, index, original, mutated in controls:
            if sources[index].count(original) != 1:
                raise AssertionError(f"control anchor is not unique: {label}")
            mutated_sources = list(sources)
            mutated_sources[index] = sources[index].replace(original, mutated, 1)
            expect_failure(tuple(mutated_sources), label)
    except AssertionError as error:
        print(f"oob_reply_contract: FAILED - {error}")
        return 1
    print(f"oob_reply_contract: ok ({len(controls)} single-reversal controls fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
