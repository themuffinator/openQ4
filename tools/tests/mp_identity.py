#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Exercise and pin player identity, returning players and the ranking table (MP-17).

Compiles tools/tests/native/Ed25519SignTest.cpp (key derivation and signing
against RFC 8032's vectors) and tools/tests/native/MpIdentityTest.cpp (the
engine-free core in src/game/mp/Identity.h: per-server keys, the signed
proof and what it binds, fingerprints, ghost codes, the reconnect store and
the Elo ratings). Then pins, without game assets, where the game uses it: who
is challenged, how a proof is checked, identity bans, a leaver's place, the
ranking table's file, the console commands and the player-facing text. The
running server is qualified by tools/tests/mp_identity_smoke.py. See
docs/user/server-security.md and docs/user/multiplayer.md.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tmp" / "mp-identity-test"
LANGUAGES = ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")
STRINGS = range(232830, 232837)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def function_body(source: str, signature: str) -> str:
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


def run_native() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for stem, source_name in (("ed25519_sign", "Ed25519SignTest.cpp"), ("mp_identity", "MpIdentityTest.cpp")):
        source = ROOT / "tools/tests/native" / source_name
        exe = OUT / (stem + (".exe" if os.name == "nt" else ""))
        if os.name == "nt":
            command = OUT / f"build_{stem}.cmd"
            command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                               "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                                   "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS", str(source),
                                   "/Fo:" + str(OUT / (stem + ".obj")), "/Fe:" + str(exe)]) + "\n",
                               encoding="utf-8")
            subprocess.run(["cmd", "/d", "/c", str(command)], cwd=OUT, check=True)
        else:
            subprocess.run([os.environ.get("CXX", shutil.which("c++") or "c++"), "-std=c++17", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], cwd=OUT, check=True)


def check_cvars() -> None:
    cvars = read("src/game/gamesys/SysCvar.cpp")
    header = read("src/game/gamesys/SysCvar.h")

    def declaration(name: str) -> str:
        matches = re.findall(r'^(?:idCVar|idModeCVar) ' + name + r'\(.*$', cvars, re.M)
        require(len(matches) == 1 and matches[0].startswith("idCVar "),
                f"{name} is declared once in the unified game module, with one default for both modes")
        require(f"extern idCVar\t{name};" in header, f"{name} is exported from SysCvar.h")
        return matches[0]

    identity = declaration("ui_identity")
    # the user's decision of 23 November 2026: on unless the player turns it off
    require('"1"' in identity and "CVAR_USERINFO" in identity and "CVAR_ARCHIVE" in identity and "CVAR_BOOL" in identity,
            "ui_identity is on by default, archived, and offered to the server in userinfo")
    require('"1"' in declaration("g_identity") and "CVAR_SERVERINFO" not in declaration("g_identity"), "servers ask for identities by default")
    require('"300"' in declaration("g_reconnectRestore") and ", 0, 3600 )" in declaration("g_reconnectRestore"),
            "a leaver's place is kept five minutes by default")
    require('"0"' in declaration("g_ranking") and "CVAR_BOOL" in declaration("g_ranking"), "the ranking table is opt-in for the server")
    # one game module serves both modes: each identity cvar is registered once, and no second tree mirrors it
    require(len(re.findall(r'^idCVar ui_identity\(', cvars, re.M)) == 1
            and not (ROOT / "src/mpgame/gamesys/SysCvar.cpp").exists(), "the game module registers each identity cvar once")


def check_protocol() -> None:
    local = read("src/game/Game_local.h")
    enum = re.search(r"GAME_RELIABLE_MESSAGE_MODERATION,(.*?)GAME_RELIABLE_MESSAGE_COUNT,", local, re.S)
    require(enum is not None and "GAME_RELIABLE_MESSAGE_IDENTITY," in enum.group(1), "the identity message is appended after MP-18's")
    require("RELIABLE_FLOOD_IDENTITY," in local and "RELIABLE_FLOOD_IDENTITY_DELAY" in local, "identity requests have their own flood class")
    network = read("src/game/Game_network.cpp")
    require(network.count("case GAME_RELIABLE_MESSAGE_IDENTITY:") >= 3, "the server, the viewers and the client each handle the message")
    require("CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_IDENTITY, RELIABLE_FLOOD_IDENTITY_DELAY )" in network,
            "the server paces identity requests")
    repeater = re.search(r"case GAME_RELIABLE_MESSAGE_MATCH_AUTH_CHALLENGE:\n\t\t\tcase GAME_RELIABLE_MESSAGE_IDENTITY:\n\t\t\t\t// These payloads are recipient-specific", network)
    require(repeater is not None, "a challenge is never copied to a repeater or a demo")
    require("mpGame.IdentityClientConnect( clientNum );" in function_body(network, "void idGameLocal::ServerClientConnect("),
            "a connection, or a new map, starts with no identity")
    require("mpGame.IdentityClientBegin( clientNum, isBot );" in function_body(network, "void idGameLocal::ServerClientBegin("),
            "the challenge goes out when the player has the map")

    host = read("src/game/mp/IdentityHost.cpp")
    begin = function_body(host, "void idMultiplayerGame::IdentityClientBegin(")
    require("isBot" in begin and "IdentitySendChallenge( clientNum );" in begin, "a player, never a bot, is challenged on begin")
    send = function_body(host, "void idMultiplayerGame::IdentitySendChallenge(")
    require('gameLocal.userInfo[ clientNum ].GetBool( "ui_identity" )' in send and "identityAsked[ clientNum ] = true;" in send,
            "only a game that offers an identity is challenged, once a connection, so an older client never sees the message")
    require("sys->SecureRandomBytes( identityNonce[ clientNum ], MP_ID_NONCE_BYTES )" in send, "each challenge has a fresh nonce")
    write = function_body(host, "void idMultiplayerGame::IdentityWriteChallenge(")
    require("IdentityWriteChallenge( clientNum );" in send and
            "networkSystem->ServerSendReliableMessage( clientNum, outMsg );" in write, "the challenge goes to that player alone")
    frame = function_body(host, "void idMultiplayerGame::IdentityServerFrame(")
    require("MP_ID_RESEND_AT_MS[]		= { 0, 3000, 15000 };" in host and "MP_ID_GHOST_AFTER_MS		= 20000;" in host and
            "IdentityWriteChallenge( i );" in frame and "MP_ID_RESEND_AT_MS[ identityChallengeSends[ i ] ]" in frame,
            "an unanswered challenge is sent again, the same nonce, before the ghost code is told")
    require("mpGame.IdentityUserInfoChanged( clientNum );" in function_body(read("src/game/Game_local.cpp"), "const idDict* idGameLocal::SetUserInfo("),
            "userinfo that arrives after the map is loaded still brings the challenge")
    receive = function_body(host, "void idMultiplayerGame::ServerReceiveIdentity(")
    proof = receive[receive.index("MP_ID_MSG_PROOF"):receive.index("MP_ID_MSG_GHOST")]
    require(proof.index("identityPending[ clientNum ] = false;") < proof.index("MPIdVerifyProof("),
            "one answer to one challenge: the challenge is spent before the proof is checked")
    require("matchConnectionId[ clientNum ]" in proof and "identityNonce[ clientNum ], clientNum, connection" in proof,
            "a proof is bound to the server's nonce, the slot and the connection")
    require("SecureWipe( identityNonce[ clientNum ]" in proof, "the spent nonce is wiped")
    proved = function_body(host, "void idMultiplayerGame::IdentityProved(")
    require(proved.index("FindIdentityBan( fingerprint )") < proved.index("IdentityRestore("),
            "a banned identity is removed before anything is given back")
    client = function_body(host, "void idMultiplayerGame::ClientReceiveIdentityChallenge(")
    require("slot != gameLocal.localClientNum || !ui_identity.GetBool()" in client, "a player who turned identity off sends nothing")
    require("SecureWipe( seed, sizeof( seed ) )" in client, "the seed does not outlive the signature")
    frame = function_body(host, "void idMultiplayerGame::IdentityServerFrame(")
    require('FindKey( "ui_identity" ) != NULL' in frame, "ghost codes go only to games that have the ghost command")


def check_places_and_bans() -> None:
    mp = read("src/game/MultiplayerGame.cpp")
    disconnect = function_body(mp, "void idMultiplayerGame::DisconnectClient(")
    require(disconnect.index("IdentityClientDisconnect( clientNum );") < disconnect.index("matchConnectionId[ clientNum ] = 0;"),
            "a leaver's place is kept while the slot still knows them")
    # one Reset serves both modes: its network-play half, ResetMatchSession, runs in the multiplayer mode
    reset = function_body(mp, "void idMultiplayerGame::Reset()")
    session = reset.find("const bool matchSession = gameLocal.IsMultiplayerMode();")
    require(0 < session < reset.find("Clear();") < reset.find("if ( matchSession ) {\n\t\tResetMatchSession();\n\t}")
            and "IdentityNewMatch();" in function_body(mp, "void idMultiplayerGame::ResetMatchSession( void )"),
            "a new map forgets kept places")
    require("IdentityMatchEnded();" in function_body(mp, "void idMultiplayerGame::OnMatchEnded("), "the final board rates the match")
    require("gameLocal.mpGame.IdentityNewMatch();" in read("src/game/mp/GameState.cpp"), "going live forgets the warmup's places")
    host = read("src/game/mp/IdentityHost.cpp")
    restore = function_body(host, "bool idMultiplayerGame::IdentityRestore(")
    require("GTF_BRACKET" in restore and "identityRestored[ clientNum ]" in restore and "IdentityCasual()" in restore,
            "places are casual, once a connection, and never in a bracket")
    ended = function_body(host, "void idMultiplayerGame::IdentityMatchEnded(")
    require("GetScoreForTeam( TEAM_MARINE )" in ended and "GetTeamScore(" not in ended,
            "team results come from the teams' scores (GetTeamScore is one player's share of them)")
    ranking = function_body(host, "static void RankingSave(")
    require("MP_RANKING_STAGED" in ranking and "PromoteFile( MP_RANKING_STAGED, MP_RANKING_FILE" in ranking and "rankingBlocked" in ranking,
            "the ranking table is written whole and atomically, and a file this build cannot read is left alone")

    network = read("src/game/Game_network.cpp")
    require("gameLocal.ServerBanClient( clientNum, duration, reason.c_str(), \"console\" );" in function_body(network, "void idGameLocal::Ban_f("),
            "ban <player> bans their range and their identity")
    require("ServerBanClient( targetSlot," in network, "an admin's ban does the same")
    require("gameLocal.ServerBanClient( clientNum," in function_body(mp, "void idMultiplayerGame::HandleServerAdminBanPlayer("),
            "the Admin page's Ban does the same")
    require("MPModParseIdentity( argv[ 1 ], identity )" in function_body(network, "void idGameLocal::Ban_f("), "ban id:<identity> works")
    require("MPModBanFindTarget(" in function_body(network, "void idGameLocal::Unban_f("), "unban id:<identity> works")
    require("ClientIdentity( i )" in function_body(network, "void idGameLocal::Status_f("), "status shows each player's identity")
    # one Game_network.cpp serves both modes (each module carried its own ban-list menu code before the merge)
    require("MPModBanTarget(" in function_body(network, "const mpBanInfo_t* idGameLocal::GetBanListEntry("),
            "the menus list an identity ban as id:<identity>")
    require("MPModBanFindTarget(" in function_body(network, "void idGameLocal::RemoveGuidFromBanList("),
            "the menus lift an identity ban")
    require(network.count("const mpBanInfo_t* idGameLocal::GetBanListEntry(") == 1
            and network.count("void idGameLocal::RemoveGuidFromBanList(") == 1, "the ban-list menu code is defined once")


def check_commands_and_text() -> None:
    commands = read("src/game/gamesys/SysCmds.cpp")
    for name, handler in (("identity", "Identity_f"), ("identityReset", "IdentityReset_f"), ("ghost", "Ghost_f"),
                          ("rankings", "Rankings_f"), ("history", "History_f")):
        require(re.search(rf'AddCommand\(\s*"{name}",\s*idMultiplayerGame::{handler}\b', commands) is not None,
                f"console command {name} reaches {handler}")
    host = read("src/game/mp/IdentityHost.cpp")
    require('idStr::Icmp( args.Argv( 1 ), "confirm" )' in function_body(host, "void idMultiplayerGame::IdentityReset_f("),
            "identityReset asks for confirm before it makes the player new everywhere")
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for number in STRINGS:
            require(f'"#str_{number}"' in table, f"{language} has #str_{number}")


def main() -> int:
    run_native()
    check_cvars()
    check_protocol()
    check_places_and_bans()
    check_commands_and_text()
    print("mp_identity: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
