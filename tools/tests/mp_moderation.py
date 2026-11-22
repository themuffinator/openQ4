#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Exercise and pin server administration and moderation (MP-18).

Compiles tools/tests/native/MpModerationTest.cpp, which runs the engine-free
core in src/mpgame/mp/Moderation.h (addresses and ranges, the ban list and its
file lines, text mutes, the chat flood window, the name policy, the admin log's
lines). Then pins, without any game assets, the places the game and the engine
use it: where a connection is refused, where chat is checked, each console
command and who may run it, the ban file's reader, and the player-facing text.
The running server is qualified by tools/tests/mp_moderation_smoke.py.
See docs/user/server-security.md.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tmp" / "mp-moderation-test"
LANGUAGES = ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")
COMMANDS = {
    "ban": ("idGameLocal::Ban_f", "void idGameLocal::Ban_f( const idCmdArgs &args )", '"ban"'),
    "unban": ("idGameLocal::Unban_f", "void idGameLocal::Unban_f( const idCmdArgs &args )", '"unban"'),
    "banlist": ("idGameLocal::BanList_f", "void idGameLocal::BanList_f( const idCmdArgs &args )", '"banlist"'),
    "mute": ("idGameLocal::Mute_f", "void idGameLocal::Mute_f( const idCmdArgs &args )", 'mute ? "mute" : "unmute"'),
    "unmute": ("idGameLocal::Mute_f", "void idGameLocal::Mute_f( const idCmdArgs &args )", 'mute ? "mute" : "unmute"'),
    "status": ("idGameLocal::Status_f", "void idGameLocal::Status_f( const idCmdArgs &args )", '"status"'),
    "serverShutdown": ("idGameLocal::Shutdown_f", "void idGameLocal::Shutdown_f( const idCmdArgs &args )", '"serverShutdown"'),
}


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
    source = ROOT / "tools/tests/native/MpModerationTest.cpp"
    exe = OUT / ("mp_moderation.exe" if os.name == "nt" else "mp_moderation")
    if os.name == "nt":
        command = OUT / "build.cmd"
        command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                           "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                               "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", str(source),
                               "/Fo:" + str(OUT / "mp_moderation.obj"), "/Fe:" + str(exe)]) + "\n",
                           encoding="utf-8")
        subprocess.run(["cmd", "/d", "/c", str(command)], cwd=OUT, check=True)
    else:
        subprocess.run([os.environ.get("CXX", shutil.which("c++") or "c++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Werror", "-pedantic", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=OUT, check=True)


def check_cvars() -> None:
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    header = read("src/mpgame/gamesys/SysCvar.h")

    def declaration(name: str) -> str:
        match = re.search(r'^idCVar ' + name + r'\(.*$', cvars, re.M)
        require(match is not None, f"{name} is declared in the multiplayer game module")
        require(f"extern idCVar\t{name};" in header, f"{name} is exported from SysCvar.h")
        require("CVAR_GAME | CVAR_ARCHIVE" in match.group(0) and "CVAR_SERVERINFO" not in match.group(0),
                f"{name} is the server's own archived setting, not sent to clients")
        return match.group(0)

    # Quake II's flood_msgs 4, flood_persecond 4, flood_waitdelay 10
    require('"4"' in declaration("g_chatFloodMessages") and ", 0, 16 )" in declaration("g_chatFloodMessages"),
            "g_chatFloodMessages ships at four lines, and 0 turns the window off")
    require('"4"' in declaration("g_chatFloodSeconds") and ", 1, 60 )" in declaration("g_chatFloodSeconds"),
            "g_chatFloodSeconds ships at four seconds")
    require('"10"' in declaration("g_chatFloodWait") and ", 0, 600 )" in declaration("g_chatFloodWait"),
            "g_chatFloodWait ships at ten seconds")
    require('"1"' in declaration("g_adminLog") and "CVAR_BOOL" in declaration("g_adminLog"), "the admin log is on by default")
    require('"admin;console;referee"' in declaration("g_reservedNames"), "three names are reserved by default")
    for module in ("src/game/gamesys/SysCvar.cpp",):
        require("g_chatFloodMessages" not in read(module), "game-sp registers no moderation cvar")

    network = read("src/framework/async/AsyncNetwork.cpp")
    limit = re.search(r'^idCVar\s+idAsyncNetwork::serverMaxClientsPerAddress\(.*$', network, re.M)
    require(limit is not None and '"net_serverMaxClientsPerAddress", "0"' in limit.group(0)
            and "0, MAX_ASYNC_CLIENTS )" in limit.group(0) and "CVAR_ARCHIVE" in limit.group(0),
            "net_serverMaxClientsPerAddress is off by default: players behind one router share an address")


def check_commands() -> None:
    cmds = read("src/mpgame/gamesys/SysCmds.cpp")
    network = read("src/mpgame/Game_network.cpp")
    guard = function_body(network, "static bool ModerationServerCommand( const char *command )")
    require("!gameLocal.isMultiplayer || !gameLocal.isServer" in guard and "return false;" in guard,
            "the moderation commands refuse to run anywhere but on the server")
    for name, (handler, signature, guarded) in COMMANDS.items():
        registration = re.search(r'cmdSystem->AddCommand\( "' + name + r'",\s*' + re.escape(handler) + r',\s*CMD_FL_GAME,', cmds)
        require(registration is not None, f"{name} is a game command handled by {handler}")
        body = function_body(network, signature)
        call = body.find("ModerationServerCommand( " + guarded + " )")
        require(0 <= call < 200, f"{name} checks that it runs on the server before anything else")
    require('idMultiplayerGame::RemoveClientFromBanList_f' in cmds, "the older unban name is still registered")
    old = function_body(read("src/mpgame/MultiplayerGame.cpp"), "void idMultiplayerGame::RemoveClientFromBanList_f( const idCmdArgs& args )")
    require("idGameLocal::Unban_f( args );" in old and "GetClientGUID" not in old, "the older name does what unban does")

    ban = function_body(network, "void idGameLocal::Ban_f( const idCmdArgs &args )")
    require(ban.index("ModerationFindClient( args.Argv( 1 ) )") < ban.index("MPModJoinAddressArgs( argc, argv, 1, text, sizeof( text ) )"),
            "ban reads a slot or a name before it reads an address, so a slot number is never an address")
    require("address.family == 0" in ban, "ban refuses a bot and the server's own player, which have no address")
    require("next += MPModJoinDurationArgs( argc, argv, next, duration );" in ban, "ban reads an optional time before the reason")
    unban = function_body(network, "void idGameLocal::Unban_f( const idCmdArgs &args )")
    require("1 + MPModJoinAddressArgs( argc, argv, 1, text, sizeof( text ) ) != argc" in unban,
            "unban takes one row or one address, put back together from the words the console cut it into")
    require("2 + MPModJoinDurationArgs( argc, argv, 2, duration ) != argc" in function_body(network, signature := COMMANDS["mute"][1]),
            "mute takes one optional time")
    server_ban = function_body(network, "int idGameLocal::ServerBan(")
    require(server_ban.index("AddBan(") < server_ban.index('ModerationLogBan( "ban", by, ban );')
            < server_ban.index('networkSystem->ServerDropClient( i, "#str_107239" );'),
            "a ban is stored and logged before the players it covers are removed")
    require("i == localClientNum" in server_ban and "MPModAddressInRange( clientModAddress[ i ], ban.address, ban.prefixBits )" in server_ban,
            "a ban removes every connected client in the range, and never the listen server's host")
    require("mpGame.BeginServerDrop( i );" in server_ban and "mpGame.EndServerDrop( i );" in server_ban,
            "a banned player's removal is the server's own, not a lost connection")
    mute = function_body(network, "bool idGameLocal::SetClientTextMute(")
    require("clientNum == localClientNum" in mute and "MPModMuteAdd( modMutes, clientModAddress[ clientNum ], now, durationSeconds );" in mute,
            "a mute is kept by address as well, and the host cannot be muted")
    status = function_body(network, "void idGameLocal::Status_f( const idCmdArgs &args )")
    require("MPModFormatAddress( gameLocal.clientModAddress[ i ], address, sizeof( address ) );" in status
            and "IsClientTextMuted( i )" in status, "status shows each client's address and mute")


def check_enforcement() -> None:
    network = read("src/mpgame/Game_network.cpp")
    for signature in ("allowReply_t idGameLocal::ServerAllowClient(", "allowReply_t idGameLocal::RepeaterAllowClient("):
        body = function_body(network, signature)
        refused = body.find("if ( ModerationRefuseBanned( IP ) ) {")
        require(0 < refused < body.find("si_pure"), f"{signature} refuses a banned address before anything else")
        require('"#str_107239"' in body[refused:refused + 200] and "return ALLOW_NO;" in body[refused:refused + 200],
                "a banned address is told it is banned")
    require("IsGuidBanned" not in network and "IsPlayerBanned" not in network,
            "the multiplayer module holds no GUID ban: no client sends a GUID")
    refuse = function_body(network, "bool idGameLocal::ModerationRefuseBanned( const char *endpoint )")
    require("MP_MOD_REFUSAL_LOG_SECONDS" in refuse and "return true;" in refuse,
            "a refused address is logged at most once a minute and always refused")

    connect = function_body(network, "void idGameLocal::ServerClientConnect( int clientNum, const char *guid )")
    require(connect.index("ModerationClientConnect( clientNum );") < connect.index("mpGame.ServerClientConnect( clientNum );"),
            "a slot learns its new occupant's address and mute as it connects")
    learned = function_body(network, "void idGameLocal::ModerationClientConnect( int clientNum )")
    require("networkSystem->GetClientAddress( clientNum )" in learned and "MPModMuteFind( modMutes" in learned
            and "MPModFloodReset( modChatFlood[ clientNum ] );" in learned,
            "a connect reads the engine's address, restores an address mute and starts a fresh flood window")
    require("ModerationClientDisconnect( clientNum );" in network, "a disconnect clears the slot's moderation state")

    reliable = function_body(network, "void idGameLocal::ServerProcessReliableMessage( int clientNum, const idBitMsg &msg )")
    chat = reliable[reliable.index("case GAME_RELIABLE_MESSAGE_CHAT:"):reliable.index("case GAME_RELIABLE_MESSAGE_VCHAT:")]
    require(chat.index("msg.ReadString( parm, sizeof( parm ) );") < chat.index("if ( !ModerationAllowChat( clientNum, true ) ) {")
            < chat.index("mpGame.ProcessChatMessage("), "a chat line is read whole, then checked, then passed on")
    voice = reliable[reliable.index("case GAME_RELIABLE_MESSAGE_VCHAT:"):reliable.index("case GAME_RELIABLE_MESSAGE_KILL:")]
    require(voice.index("if ( !ModerationAllowChat( clientNum, true ) ) {") < voice.index("mpGame.ProcessVoiceChat("),
            "a voice-menu line answers to the same mute and flood window")
    allow = function_body(network, "bool idGameLocal::ModerationAllowChat( int clientNum, bool fromNetwork )")
    require("g_chatFloodMessages.GetInteger()" in allow and "g_chatFloodSeconds.GetInteger() * 1000" in allow
            and "g_chatFloodWait.GetInteger() * 1000" in allow and "MP_MOD_NOTICE_MSEC" in allow,
            "the flood window reads its three settings, and the sender is told at a bounded rate")
    process = function_body(read("src/mpgame/MultiplayerGame.cpp"), "void idMultiplayerGame::ProcessChatMessage(")
    require("gameLocal.isServer && gameLocal.IsClientTextMuted( clientNum )" in process,
            "a muted player's line stops in ProcessChatMessage whichever path it took")
    require("case GAME_RELIABLE_MESSAGE_SERVER_ADMIN:" in reliable and "Rejected unauthenticated server admin message" in reliable,
            "the unauthenticated admin message is still refused from remote clients")

    mp = read("src/mpgame/MultiplayerGame.cpp")
    require('"kick %i ban"' not in mp, "the Admin page's Ban no longer runs a kick the engine reads as one word")
    page = function_body(mp, "void idMultiplayerGame::HandleServerAdminBanPlayer( int clientNum )")
    require("gameLocal.ServerBan( address, MPModAddressBits( address ), 0," in page, "the Admin page's Ban bans the player's address")
    require(mp.count('gameLocal.ModerationLogClient( "kick", "vote",') == 2
            and 'gameLocal.ModerationLogClient( "kick", "admin", clientNum, "", 0 );' in mp, "vote and Admin page kicks reach the admin log")

    names = function_body(read("src/mpgame/Game_local.cpp"), "const idDict* idGameLocal::SetUserInfo(")
    clean = names.find("MPModCleanName(")
    require(0 < clean < names.find("idStr::IsNumeric(") and "MPModNameIsReserved(" in names
            and "g_reservedNames.GetString()" in names, "the name policy runs before the older name rules")
    require("!idStr::IcmpNoEscape( idGameLocal::userInfo[ clientNum ].GetString( \"ui_name\" ), idGameLocal::userInfo[ i ].GetString( \"ui_name\" ) )" in names,
            "a name already in use is refused whatever its colours")


def check_files() -> None:
    loader = read("src/mpgame/mp/ModerationFile.h")
    load = function_body(loader, "inline void MPModLoadBanFile(")
    require('fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_MOD_BANLIST_FILENAME, "fs_savepath" ) )' in load
            and "fileSystem->ReadFile(" not in load,
            "the ban list is read by OS path from the save folder: a pure server refuses loose files mid-map")
    require("MP_MOD_BANLIST_MAX_BYTES" in load and "MP_MOD_BANLIST_UNREADABLE" in load,
            "an oversized file is not read, and a file in neither form is copied before it is replaced")
    save = function_body(loader, "inline bool MPModSaveBanFile(")
    require('fileSystem->OpenFileWrite( MP_MOD_BANLIST_FILENAME, "fs_savepath" )' in save and "keptLines[ i ].c_str()" in save,
            "the ban list is written to the save folder with the lines this build did not understand")
    for tree in ("src/mpgame", "src/game"):
        network = read(f"{tree}/Game_network.cpp")
        require("ModerationFile.h" in network and "banFile->WriteString" not in network and "BANLIST_FILENAME \"banlist.txt\"" not in network,
                f"{tree} reads and writes the text ban list, never Quake 4's binary one")
        populate = function_body(network, "void idGameLocal::PopulateBanList( idUserInterface* hud )")
        require('va( "%d:  %s\\t%s", i+1, banInfo->name, banInfo->guid )' in populate,
                f"{tree} publishes a ban row as the menus parse it")
    log = function_body(read("src/mpgame/Game_network.cpp"), "void idGameLocal::ModerationWriteLog( mpModLogLine_t &line )")
    require('fileSystem->OpenFileAppend( MP_MOD_ADMIN_LOG_FILENAME, true, "fs_savepath" )' in log and "g_adminLog.GetBool()" in log,
            "the admin log is appended and synced under the save folder")
    require('#define MP_MOD_ADMIN_LOG_FILENAME\t"logs/admin.ndjson"' in read("src/mpgame/Game_network.cpp"), "the admin log's path")


def check_engine() -> None:
    server = read("src/framework/async/AsyncServer.cpp")
    connect = function_body(server, "void idAsyncServer::ProcessConnectMessage(")
    limit = connect.find("idAsyncNetwork::serverMaxClientsPerAddress.GetInteger()")
    require(connect.find("ValidateChallenge( from, challenge, clientId )") < limit < connect.find("game->ServerAllowClient("),
            "the per-address limit is checked on a proven address, before the game is asked")
    block = connect[limit:connect.index("// game may be passworded")]
    require("client.clientState < SCS_PUREWAIT" in block and "Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() )" in block
            and "clientId == client.clientId || from.port == client.channel.GetRemoteAddress().port" in block
            and 'PrintOOB( from, SERVER_PRINT_MISC, "#str_232640" );' in block,
            "the limit counts other connections from the address, not the slot a reconnect takes over, and says why")
    copy = function_body(server, "bool idAsyncServer::CopyClientAddress( int clientNum, char *out, int outSize ) const")
    require("clientNum == localClientNum" in copy and "SCS_FREE" in copy, "the listen server's own player has no network address")
    system = read("src/framework/async/NetworkSystem.cpp")
    require("idAsyncNetwork::server.CopyClientAddress( clientNum, clientAddress, sizeof( clientAddress ) );" in
            function_body(system, "const char *idNetworkSystem::GetClientAddress( int clientNum )"),
            "the game module can read a client's address")
    require("virtual const char* GetClientAddress(int clientNum);" in read("src/framework/async/NetworkSystem.h"),
            "GetClientAddress keeps its slot in the game-module interface")


def check_console_kick() -> None:
    """The engine's own kick reaches the admin log without a change to the game interface."""
    server = read("src/framework/async/AsyncServer.cpp")
    drop = function_body(server, "void idAsyncServer::DropClient( int clientNum, const char *reason )")
    publish = drop.find('idAsyncNetwork::serverLastDrop.SetString( va( "%d %s", clientNum, reason ) );')
    require(0 < publish < drop.find("reason = common->GetLanguageDict()->GetString( reason );") < drop.find("game->ServerClientDisconnect( clientNum );"),
            "the engine publishes the slot and the untranslated reason before the game hears of the disconnect")
    declared = re.search(r'^idCVar\s+idAsyncNetwork::serverLastDrop\(.*$', read("src/framework/async/AsyncNetwork.cpp"), re.M)
    require(declared is not None and '"net_serverLastDrop", ""' in declared.group(0) and "CVAR_ROM" in declared.group(0),
            "net_serverLastDrop is read-only: only the engine writes it")
    network = read("src/mpgame/Game_network.cpp")
    left = function_body(network, "void idGameLocal::ModerationClientDisconnect( int clientNum )")
    require('#define MP_MOD_ENGINE_KICK_REASON\t"#str_07134"' in network, "the engine's kick reason")
    require("!mpGame.ServerDropInFlight( clientNum )" in left and "!botManager.IsBot( clientNum )" in left
            and 'cvarSystem->GetCVarString( "net_serverLastDrop" )' in left
            and left.index('ModerationLogClient( "kick", "console", clientNum, "", 0 );') < left.index("MPModClearAddress( clientModAddress[ clientNum ] );"),
            "a console kick is logged once, with the address, and never for a removal the game logged itself or for a bot")
    require(function_body(read("src/framework/async/AsyncNetwork.cpp"), "void idAsyncNetwork::Kick_f( const idCmdArgs &args )")
            .count('server.DropClient( iclient, "#str_07134" );') == 1, "the engine's kick drops with the reason the game looks for")


def check_mute_vote() -> None:
    header = read("src/mpgame/MultiplayerGame.h")
    mp = read("src/mpgame/MultiplayerGame.cpp")
    require("VOTEFLAG_MUTE\t\t\t= 0x100000," in header and re.search(r"#define\s+NUM_VOTES\s+21\b", header) is not None,
            "the mute vote is flag bit 20 and inside the vote mask")
    call = function_body(mp, "void idMultiplayerGame::CallVote_f( const idCmdArgs &args )")
    word = call.find('idStr::Icmp( szArg1, "mute" )')
    require(0 < word and "voteData.m_fieldFlags |= VOTEFLAG_KICK | VOTEFLAG_MUTE;" in call[word:word + 900]
            and '"#str_232649"' in call[word:word + 900], "callvote mute names its player in the kick field")
    server = function_body(mp, "void idMultiplayerGame::ServerCallPackedVote( int clientNum, const idBitMsg &msg )")
    require("gameLocal.IsClientTextMuted( voteData.m_kick )" in server and '"#str_232650"' in server,
            "a vote to mute a player who is already muted is refused")
    require('gameLocal.SetClientTextMute( currentVoteData.m_kick, true, static_cast<mpModTime_t>( g_voteMuteMinutes.GetInteger() ) * 60, "vote" );' in mp,
            "a passed mute vote silences its target for g_voteMuteMinutes and is logged as a vote")
    passed = mp[mp.index("0 != ( currentVoteData.m_fieldFlags & VOTEFLAG_MUTE ) ) {"):]
    require(passed.index("gameLocal.SetClientTextMute(") < passed.index('va( "kick %d", currentVoteData.m_kick )'),
            "a mute vote never reaches the kick")
    require(mp.count('"#str_232648"') == 2, "the HUD, the stock menu and the retained page say MUTE PLAYER for a mute vote")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    declared = re.search(r'^idCVar g_voteMuteMinutes\(.*$', cvars, re.M)
    require(declared is not None and '"10"' in declared.group(0) and ", 0, 1440 )" in declared.group(0)
            and "CVAR_GAME | CVAR_ARCHIVE" in declared.group(0), "a mute vote lasts ten minutes by default")
    require('"bit 20 (+1048576) mute player"' in cvars, "si_voteFlags documents the mute bit")


def check_shutdown() -> None:
    network = read("src/mpgame/Game_network.cpp")
    run = function_body(network, "bool idGameLocal::RunServerShutdown( bool matchEnded )")
    require('cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ? "quit\\n" : "disconnect\\n"' in run,
            "a dedicated server's process quits; a server hosted from the game returns to its menu")
    stopped = run.index("if ( modShutdownMode == MOD_SHUTDOWN_STOPPED ) {")
    require(stopped < run.index("modShutdownMode = MOD_SHUTDOWN_STOPPED;") and "modShutdownMode = MOD_SHUTDOWN_NONE;" not in run,
            "once the stop command is queued nothing may start another map: the state holds until the server is gone")
    ended = function_body(network, "void idGameLocal::ServerShutdownMapEnded( void )")
    require("modShutdownMode = MOD_SHUTDOWN_NONE;" in ended and "ServerShutdownMapEnded();" in function_body(
        read("src/mpgame/Game_local.cpp"), "void idGameLocal::MapShutdown( void )"),
            "a listen server that stopped itself does not hand its shutdown to the next server the host starts")
    goodbye = run.index('ServerSendChatMessage( -1, "server", "#str_232647", "" );')
    require(goodbye < run.index("modShutdownMode = MOD_SHUTDOWN_STOPPING;") < run.index("modShutdownStopTime = time + MP_MOD_SHUTDOWN_STOP_MSEC;"),
            "the goodbye is sent before the stop is armed")
    require("stop = matchEnded;" in run and "!botManager.IsBot( i )" in run,
            "a shutdown after the match also stops a server with no human on it")
    require("modShutdownNextNotice = ModerationNextShutdownNotice( left );" in run, "a timed shutdown repeats its notice as the time runs down")
    state = function_body(read("src/mpgame/mp/GameState.cpp"), "void rvGameState::Run( void )") if "void rvGameState::Run( void )" in read(
        "src/mpgame/mp/GameState.cpp") else read("src/mpgame/mp/GameState.cpp")
    hook = state.index("if ( gameLocal.RunServerShutdown( true ) ) {")
    require(hook < state.index("if ( gameLocal.NextMap() ) {", hook), "the shutdown is asked before the next map is chosen")
    restart = function_body(read("src/mpgame/Game_local.cpp"), "void idGameLocal::MapRestart( int instance )")
    require(0 < restart.index("if ( RunServerShutdown( true ) ) {") < restart.index("mpGame.PickMap("),
            "a map change a vote or an operator forces also ends the last match")
    frame = function_body(read("src/mpgame/MultiplayerGame.cpp"), "void idMultiplayerGame::Run( void )")
    require("gameLocal.RunServerShutdown( false );" in frame, "the shutdown runs every server frame")
    command = function_body(network, "void idGameLocal::Shutdown_f( const idCmdArgs &args )")
    require('idStr::Icmp( argv[ 1 ], "cancel" ) == 0' in command and 'idStr::Icmp( argv[ 1 ], "match" ) == 0' in command
            and "MPModJoinDurationArgs( argc, argv, 1, seconds )" in command, "serverShutdown takes match, now, a time or cancel")
    for event in ('"shutdown_scheduled"', '"shutdown_cancelled"', '"shutdown"'):
        require("ModerationLogShutdown( " + event in network, f"{event} reaches the admin log")


def check_text() -> None:
    expected = {"232640": None, "232641": None, "232642": None, "232643": None, "232644": None, "232645": None,
                "232646": None, "232647": None, "232648": None, "232649": None, "232650": None}
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for ident in expected:
            match = re.search(r'^\t"#str_' + ident + r'"\t"([^"\n]+)"$', table, re.M)
            require(match is not None and len(match.group(1)) > 8, f"{language}: #str_{ident} is present")
    english = read("content/baseoq4/pak0/strings/english_openq4.lang")
    require('"#str_232640"\t"Too many players are connected from your address."' in english, "the per-address refusal text")
    require('"#str_232641"\t"You are muted on this server. Your chat reaches nobody."' in english, "the mute notice")
    guide = read("docs/user/server-security.md")
    for token in ("ban <", "unban <", "banlist", "mute <", "unmute <", "status", "banlist.txt", "logs/admin.ndjson",
                  "g_chatFloodMessages", "g_chatFloodSeconds", "g_chatFloodWait", "g_reservedNames", "g_adminLog",
                  "net_serverMaxClientsPerAddress", "serverShutdown", "callvote mute", "g_voteMuteMinutes"):
        require(token in guide, f"the server security guide covers {token}")
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        require(re.search(r'^\t"#str_232648"\t"[^"\n]*%s[^"\n]*"$', table, re.M) is not None,
                f"{language}: the mute vote line names its player")


def main() -> None:
    run_native()
    check_cvars()
    check_commands()
    check_enforcement()
    check_files()
    check_engine()
    check_console_kick()
    check_mute_vote()
    check_shutdown()
    check_text()
    print("mp_moderation: ok")


if __name__ == "__main__":
    main()
