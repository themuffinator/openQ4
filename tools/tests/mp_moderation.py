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


# the commands a logged-in admin may type on a client, and the request each becomes
ADMIN_VERBS = {
    "ban": "MP_MOD_ADMIN_BAN",
    "mute": "mute ? MP_MOD_ADMIN_MUTE : MP_MOD_ADMIN_UNMUTE",
    "unmute": "mute ? MP_MOD_ADMIN_MUTE : MP_MOD_ADMIN_UNMUTE",
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
    """The engine-free core, and the ban file reader over a stand-in file system."""
    OUT.mkdir(parents=True, exist_ok=True)
    for stem, source_name in (("mp_moderation", "MpModerationTest.cpp"), ("mp_moderation_file", "MpModerationFileTest.cpp")):
        source = ROOT / "tools/tests/native" / source_name
        exe = OUT / (stem + (".exe" if os.name == "nt" else ""))
        if os.name == "nt":
            command = OUT / "build.cmd"
            command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                               "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                                   "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", str(source),
                                   "/Fo:" + str(OUT / (stem + ".obj")), "/Fe:" + str(exe)]) + "\n",
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
        flags = r'CMD_FL_GAME,' if name in ("banlist", "status") else r'CMD_FL_GAME \| CMD_FL_RAWTEXT,'
        registration = re.search(r'cmdSystem->AddCommand\( "' + name + r'",\s*' + re.escape(handler) + r',\s*' + flags, cmds)
        require(registration is not None, f"{name} is a game command handled by {handler}"
                + ("" if name in ("banlist", "status") else ", and reads its line as typed"))
        body = function_body(network, signature)
        call = body.find("ModerationServerCommand( " + guarded + " )")
        if name in ADMIN_VERBS:
            # typed on a client these are a logged-in admin's request; nothing else of the command runs there
            forward = body.find("if ( gameLocal.isMultiplayer && gameLocal.isClient ) {")
            sent = body.find("ModerationClientCommand( " + ADMIN_VERBS[name] + ", args );")
            require(0 <= forward < sent < body.find("return;", sent) < call < 420,
                    f"{name} on a client is sent to the server as an admin's request, and runs only on the server")
        else:
            require(0 <= call < 200, f"{name} checks that it runs on the server before anything else")
    require('idMultiplayerGame::RemoveClientFromBanList_f' in cmds, "the older unban name is still registered")
    old = function_body(read("src/mpgame/MultiplayerGame.cpp"), "void idMultiplayerGame::RemoveClientFromBanList_f( const idCmdArgs& args )")
    require("idGameLocal::Unban_f( args );" in old and "GetClientGUID" not in old, "the older name does what unban does")

    ban = function_body(network, "void idGameLocal::Ban_f( const idCmdArgs &args )")
    slot = ban.index("if ( ModerationIsSlot( argv[ 1 ] ) ) {")
    joined = ban.index("MPModJoinAddressArgs( argc, argv, 1, text, sizeof( text ) )")
    require(slot < joined < ban.index("clientNum = ModerationFindClient( argv[ 1 ] );", joined)
            and 'strpbrk( text, ".:" ) != NULL && MPModParseAddress( text, address, prefixBits )' in ban,
            "ban reads a slot number, then an address, then a name: a player named like an address cannot take a ban meant for one")
    require("args.Argv( 1 )" not in ban and "args.Args(" not in ban and "prefixBits = MPModPlayerPrefixBits( address );" in ban,
            "ban reads the line as typed, and bans a connected player by the range they hold")
    words = function_body(network, "static int ModerationArgs( const idCmdArgs &args, mpModWords_t &words, const char *argv[ MODERATION_MAX_ARGS ] )")
    require('cvarSystem->GetCVarString( "cmd_rawText" )' in words and "MPModSplitWords( raw, words )" in words
            and "strchr( raw, '$' ) == NULL" in words and "argv[ i ] = args.Argv( i );" in words,
            "the commands read their line as typed where the engine publishes it, and the console's tokens otherwise")
    require("address.family == 0" in ban, "ban refuses a bot and the server's own player, which have no address")
    reload = function_body(network, "void idGameLocal::BanList_f( const idCmdArgs &args )")
    require(reload.index("gameLocal.LoadBanList();") < reload.index("gameLocal.FindBan( gameLocal.clientModAddress[ i ] ) == NULL")
            < reload.index('networkSystem->ServerDropClient( i, "#str_107239" );'),
            "a ban the reloaded file gained removes the connected players it covers")
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
    page = function_body(mp, "void idMultiplayerGame::HandleServerAdminBanPlayer( int clientNum, int seconds, int reason )")
    require("gameLocal.ServerBanClient( clientNum, static_cast<mpModTime_t>( Max( 0, seconds ) ), RetainedAdminBanReasonText( reason )," in page,
            "the Admin page's Ban bans the player for its length and reason: by the range they hold (their address, or "
            "their IPv6 /64) and, since MP-17, their identity")
    client_ban = function_body(read("src/mpgame/Game_network.cpp"), "int idGameLocal::ServerBanClient(")
    require("ServerBan( address, MPModPlayerPrefixBits( address ), durationSeconds," in client_ban,
            "a connected player's ban covers the range they hold")
    require(mp.count('gameLocal.ModerationLogClient( "kick", "vote",') == 2
            and 'gameLocal.ModerationLogClient( "kick", "admin", clientNum, "", 0 );' in mp, "vote and Admin page kicks reach the admin log")

    names = function_body(read("src/mpgame/Game_local.cpp"), "const idDict* idGameLocal::SetUserInfo(")
    clean = names.find("MPModCleanName(")
    require(0 < clean < names.find("MPModNameIsNumeric(") and "MPModNameIsReserved(" in names
            and "g_reservedNames.GetString()" in names and "idStr::IsNumeric(" not in names,
            "the name policy runs before the older name rules, and a name that only shows digits is no name")
    clan = names.find('MPModCleanText( idGameLocal::userInfo[ clientNum ].GetString( "ui_clan" ), cleanClan,')
    require(clean < clan and 'MPModNameIsReserved( cleanClan, g_reservedNames.GetString() )' in names,
            "the clan tag answers to the name policy, and a reserved name is no clan tag")
    held = names.find("if ( IsClientTextMuted( clientNum ) ) {")
    require(clan < held and 'idGameLocal::userInfo[ clientNum ].Set( "ui_name", modMutedName[ clientNum ].c_str() );' in names[held:]
            and 'idGameLocal::userInfo[ clientNum ].Set( "ui_clan", modMutedClan[ clientNum ].c_str() );' in names[held:],
            "a muted player is held to the name and the clan tag the mute found them under")
    require("!idStr::IcmpNoEscape( idGameLocal::userInfo[ clientNum ].GetString( \"ui_name\" ), idGameLocal::userInfo[ i ].GetString( \"ui_name\" ) )" in names,
            "a name already in use is refused whatever its colours")


def check_files() -> None:
    loader = read("src/mpgame/mp/ModerationFile.h")
    load = function_body(loader, "inline void MPModLoadBanFile(")
    require('fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_MOD_BANLIST_FILENAME, "fs_savepath" ) )' in load
            and "fileSystem->ReadFile(" not in load,
            "the ban list is read by OS path from the save folder: a pure server refuses loose files mid-map")
    require("MP_MOD_BANLIST_MAX_BYTES" in load and load.count("MPModBanFileBlocked() = true;") >= 2
            and "MPModBanFileBlocked() = result.unreadable;" in load and "WriteFile" not in load and "OpenFileWrite" not in load,
            "a file that is there and cannot be used is left alone, and saving over it is refused")
    require("text[ end ] != '\\n' && text[ end ] != '\\r'" in load and "text[ i ] == '\\0'" in load
            and 'idStr::Cmpn( text.c_str() + first, "ban ", 4 )' not in load,
            "a line ends at either line-end byte, and text is told from Quake 4's list by its zero bytes, not by its first word")
    require("result.overflow++;" in load, "bans past the list's capacity are counted, never passed off as lines kept")
    save = function_body(loader, "inline bool MPModSaveBanFile(")
    promote = save.find('fileSystem->PromoteFile( MP_MOD_BANLIST_STAGED, MP_MOD_BANLIST_FILENAME, "fs_savepath" )')
    require(0 < save.find("fileSystem->CloseFile( file );") < promote and 'OpenFileWrite( MP_MOD_BANLIST_FILENAME' not in save
            and 'fileSystem->RemoveFileChecked( MP_MOD_BANLIST_STAGED, "fs_savepath" );' in save[promote:],
            "the list is written whole to a staged file and takes the list's place in one step, so a crash cannot cut it short")
    require(save.index("if ( MPModBanFileBlocked() ) {") < save.index("fileSystem->OpenFileWrite(")
            and save.index("written = check->Length();") < promote and "written != expected ||" in save,
            "nothing is saved over an unusable file, and a staged file the disk cut short is never put in the list's place")
    game = read("src/mpgame/Game_network.cpp")
    loaded = function_body(game, "void idGameLocal::LoadBanList() {")
    require(loaded.index("if ( reload && ( result.unreadable || !result.found ) ) {") < loaded.index("delete modBans;")
            and "banListChanged = true;" in loaded[:loaded.index("delete modBans;")],
            "a reload that finds no usable file keeps the bans in memory")
    require('fileSystem->OpenFileWrite( MP_MOD_BANLIST_STAGED, "fs_savepath" )' in save and "keptLines[ i ].c_str()" in save,
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
    require("client.clientState < SCS_PUREWAIT" in block and "AsyncServer_SameSubscriber( from, held )" in block
            and "Sys_CompareNetAdrBase( from, held ) && ( clientId == client.clientId || from.port == held.port )" in block
            and 'PrintOOB( from, SERVER_PRINT_MISC, "#str_232640" );' in block,
            "the limit counts other connections from the subscriber, not the slot a reconnect takes over, and says why")
    same = function_body(server, "static bool AsyncServer_SameSubscriber( const netadr_t &a, const netadr_t &b )")
    require("memcmp( a.ip6, b.ip6, 8 ) == 0" in same and "linkLocal" in same and "mapped" in same and "Sys_CompareNetAdrBase( a, b )" in same,
            "over IPv6 one subscriber is a /64; link-local and IPv4-mapped addresses are compared whole")
    rcon = function_body(server, "void idAsyncServer::ExecuteRemoteConsoleCommand( const netadr_t from, const char *command, bool authenticated )")
    require(rcon.index("rconSource( from );") < rcon.index("cmdSystem->BufferCommandText( CMD_EXEC_NOW, command );")
            and '~rconSource_t() { idAsyncNetwork::serverRconFrom.SetString( "" ); }' in rcon,
            "while an rcon command runs the game can learn where it came from, and never afterwards")
    require("idAsyncNetwork::serverRunId.SetInteger( idAsyncNetwork::serverRunId.GetInteger() + 1 );" in server,
            "the engine counts the servers it starts, so the game can tell a new server from a new map")
    commands = read("src/framework/CmdSystem.cpp")
    require("CMD_FL_RAWTEXT\t\t\t= BIT(6)" in read("src/framework/CmdSystem.h")
            and 'static idCVar cmd_rawText( "cmd_rawText", "", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ROM | CVAR_PRIVATE,' in commands,
            "a command can ask for its line as typed, published in a private read-only cvar")
    raw = function_body(commands, "void idCmdSystemLocal::ExecuteWithRawText( const idCmdArgs &args, const char *rawText )")
    require("~rawTextScope_t() { cmd_rawText.SetString( previous.c_str() ); previous.SecureClear(); }" in raw,
            "the line is withdrawn however the command ends")
    private = read("src/idlib/PrivateCommand.h")
    require("static inline bool NamesAdminPassword( const char *commandText )" in private
            and "idPrivateCommand::NamesAdminPassword( commandText )" in read("src/framework/CVarSystem.cpp"),
            "a password typed after 'admin' by mistake makes the line private")
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
    require("VOTEFLAG_MUTE\t\t\t= 0x100000," in header and re.search(r"#define\s+NUM_VOTES\s+(\d+)\b", header) is not None
            and int(re.search(r"#define\s+NUM_VOTES\s+(\d+)\b", header).group(1)) > 20,
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
    require('"bit 20 (+1048576) mute player' in cvars, "si_voteFlags documents the mute bit")


def check_shutdown() -> None:
    network = read("src/mpgame/Game_network.cpp")
    run = function_body(network, "bool idGameLocal::RunServerShutdown( bool matchEnded )")
    require('cvarSystem->GetCVarInteger( "net_serverDedicated" ) != 0 ? "quit\\n" : "disconnect\\n"' in run,
            "a dedicated server's process quits; a server hosted from the game returns to its menu")
    stopped = run.index("if ( modShutdownMode == MOD_SHUTDOWN_STOPPED ) {")
    other = run.index('if ( modShutdownRun != cvarSystem->GetCVarInteger( "net_serverRunId" ) ) {')
    require(other < stopped < run.index("modShutdownMode = MOD_SHUTDOWN_STOPPED;")
            and run.count("modShutdownMode = MOD_SHUTDOWN_NONE;") == 1 and other < run.index("modShutdownMode = MOD_SHUTDOWN_NONE;") < stopped,
            "once the stop command is queued nothing may start another map: the state holds until the server is gone, and only another server lets go of it")
    require('modShutdownRun = cvarSystem->GetCVarInteger( "net_serverRunId" );' in function_body(
        network, "void idGameLocal::ScheduleServerShutdown( bool afterMatch, mpModTime_t seconds, const char *reason, const char *by )"),
            "a shutdown remembers the server it was asked of")
    frame = read("src/mpgame/Game_local.cpp")
    choke = frame.index("if ( sessionCommand.Length() && isMultiplayer && isServer && RunServerShutdown( true ) ) {")
    require(choke < frame.index("idStr::Copynz( ret.sessionCommand, sessionCommand, sizeof( ret.sessionCommand ) );")
            and 'sessionCommand = "";' in frame[choke:choke + 120],
            "every map the game asks the engine for passes the waiting shutdown first, whichever code asked")
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


def check_match_log() -> None:
    """g_matchLog: the live event stream's events, kept for good on a server."""
    stream = read("src/mpgame/mp/LiveEventStream.cpp")
    declared = re.search(r'^idCVar g_matchLog\( "g_matchLog", "0", CVAR_GAME \| CVAR_ARCHIVE \| CVAR_BOOL \| CVAR_NOCHEAT,', stream, re.M)
    require(declared is not None, "the match log is off by default: a busy server writes a line for every kill")
    require('static const char *const MP_MATCH_LOG_FILE = "logs/match.ndjson";' in stream, "the match log's path")
    wanted = function_body(stream, "bool mpLiveEventStream::Wanted( void ) const")
    require("kind == MATCH_LOG ? ( g_matchLog.GetBool() && gameLocal.isServer ) : g_matchEventStream.GetBool()" in wanted
            and "gameLocal.GetDemoState() != DEMO_PLAYING" in wanted,
            "only a server keeps a match log, and never from a demo")
    opened = function_body(stream, "bool mpLiveEventStream::Open( const mpLiveEventState &current )")
    require('fileSystem->OpenFileAppend( Path(), false, "fs_savepath" )' in opened
            and 'fileSystem->OpenFileWrite( Path(), "fs_savepath" )' in opened
            and opened.index("OpenFileAppend") < opened.index("OpenFileWrite"),
            "the match log is appended and the live file is started again")
    require("MPModFormatUtc( static_cast<mpModTime_t>( ::time( NULL ) ), utc );" in opened
            and "gameType.c_str(), current, utc )" in opened, "each stream in the match log says when it began")
    events = read("src/mpgame/mp/LiveEvents.h")
    line = function_body(events, "inline bool MPLiveEventStreamLine(")
    require('line.String( "utc", utc );' in line and line.index('"gameType"') < line.index('"utc"') < line.index("MPLiveEventRoster("),
            "the UTC time is an optional member of the stream line, so the live file's format is unchanged")
    header = read("src/mpgame/MultiplayerGame.h")
    require("mpLiveEventStream matchLog { mpLiveEventStream::MATCH_LOG };" in header, "the game owns a match log beside the live stream")
    mp = read("src/mpgame/MultiplayerGame.cpp")
    require(mp.count("matchLog.Close();") == 2 and mp.count("matchLog.Update();") == 1
            and mp.count("matchLog.Kill( attacker, victim, methodOfDeath, quadKill );") == 1,
            "the match log follows the live stream: every frame, every death the server scores, closed with the map")


def check_admins() -> None:
    """A second password gives a player the right to ask for a kick, a mute or a short ban, and nothing else."""
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    engine = read("src/framework/async/AsyncNetwork.cpp")
    for member, name in (("serverAdminPassword", "net_serverAdminPassword"), ("clientAdminPassword", "net_clientAdminPassword")):
        password = re.search(r'^idCVar\s+idAsyncNetwork::' + member + r'\(.*$', engine, re.M)
        require(password is not None and f'"{name}", "",' in password.group(0)
                and "CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_PRIVATE | CVAR_CASE_SENSITIVE" in password.group(0)
                and "CVAR_ARCHIVE" not in password.group(0) and "CVAR_SERVERINFO" not in password.group(0),
                f"{name} is private: never written to a config, never shown, never sent")
    # a game cvar is not registered, so not private, while the other game module is loaded
    for module in ("src/mpgame/gamesys/SysCvar.cpp", "src/game/gamesys/SysCvar.cpp"):
        require("AdminPassword" not in read(module), "the admin passwords are engine cvars, private from startup")
    limit = re.search(r'^idCVar g_adminMaxBanMinutes\(.*$', cvars, re.M)
    require(limit is not None and '"1440"' in limit.group(0) and ", 0, 525600 )" in limit.group(0)
            and "CVAR_GAME | CVAR_ARCHIVE | CVAR_INTEGER" in limit.group(0), "an admin's ban lasts a day at most by default")
    require("extern idCVar\tg_adminMaxBanMinutes;" in read("src/mpgame/gamesys/SysCvar.h"), "g_adminMaxBanMinutes is exported from SysCvar.h")

    # the login: the referee's exchange, the admin password's own service
    header = read("src/mpgame/MultiplayerGame.h")
    mp = read("src/mpgame/MultiplayerGame.cpp")
    require("mpRefereeAuthenticationService matchAdminAuthentication;" in header
            and "uint64_t\t\tmatchAdminConnection[ MAX_CLIENTS ];" in header,
            "the admin password has its own verifier, challenges and lockout")
    require("matchAdminConnection[ clientNum ] != 0 &&" in header
            and "matchAdminConnection[ clientNum ] == matchConnectionId[ clientNum ];" in header,
            "a grant belongs to the connection that logged in, never to the slot")
    require('static const char MP_ADMIN_AUTH_REQUEST_TOKEN[] = "challenge:admin";' in mp
            and "static const char MP_ADMIN_AUTH_PROOF_TAG = 'a';" in mp and "static const char MP_REFEREE_AUTH_PROOF_TAG = 'v';" in mp,
            "an admin login is its own kind of the exchange: a referee proof is not an admin proof")
    setup = function_body(mp, "bool idMultiplayerGame::InitializeAdminAuthentication( void )")
    require('#define MP_ADMIN_SERVER_PASSWORD_CVAR\t"net_serverAdminPassword"' in mp
            and "cvarSystem->GetCVarString( MP_ADMIN_SERVER_PASSWORD_CVAR )" in setup
            and "matchAdminAuthentication.InstallCredentialFromPassword(" in setup
            and "InstallOpaqueAdminCredential()" in setup and "MP_ADMIN_MIN_PASSWORD_BYTES" in setup
            and setup.count('cvarSystem->SetCVarString( MP_ADMIN_SERVER_PASSWORD_CVAR, "" );') == 3
            and "matchRefereeAuthentication" not in setup,
            "the password is turned into a verifier and cleared; a server without one answers with a challenge nobody can satisfy")
    require(setup.index("matchAdminCredentialIsReal = true;") < setup.index('ServerAdminRevokeAll( "the password changed" );')
            and "memset( matchAdminConnection" not in setup,
            "a new password ends the logins made with the old one; a new match session alone ends none")
    require("matchAdminAuthentication.InstallCredentialVerifier( salt, verifier )" in function_body(mp, "bool idMultiplayerGame::InstallOpaqueAdminCredential( void )"),
            "without a password the verifier is one nobody knows")
    require("static const int MP_ADMIN_MIN_PASSWORD_BYTES = 8;" in mp, "an admin password is at least eight bytes")
    bind = function_body(mp, "bool idMultiplayerGame::BindAdminAuthenticationSession( void )")
    require(bind.index("if ( sessionId != 0 && sessionId == matchAdminSessionId ) {") < bind.index("matchAdminAuthentication.BeginSession( sessionId,")
            and "return BindAdminAuthenticationSession();" in setup
            and "BeginSession" not in function_body(mp, "void idMultiplayerGame::ServerAdminLoginsOff( void )"),
            "the login service is bound once for each match session: it refuses to begin the one it is in, "
            "and a password set while a map runs must not leave logins refused until the next map")
    connect = function_body(mp, "void idMultiplayerGame::ServerClientConnect( int clientNum )")
    carried = connect.index("if ( matchAdminCarry[ clientNum ].Length() > 0 ) {")
    require(connect.index("ClearMatchOperationTransportSlot( clientNum );") < connect.index("matchConnectionId[ clientNum ] = ++nextMatchConnectionId;") < carried
            and "matchAdminCarry[ clientNum ].Cmp( address ) == 0" in connect[carried:]
            and "matchAdminConnection[ clientNum ] = matchConnectionId[ clientNum ];" in connect[carried:]
            and "matchAdminCarry[ clientNum ].Clear();" in connect[carried:]
            and "serverDropInFlight[ clientNum ] = false;" in connect,
            "a map change keeps the grant of the connection that logged in, by its address and port; nobody else inherits it")
    left = function_body(mp, "void idMultiplayerGame::DisconnectClient( int clientNum )")
    require(left.index("matchAdminCarry[ clientNum ].Clear();") < left.index("ClearMatchOperationTransportSlot( clientNum );", left.index("matchAdminCarry[ clientNum ].Clear();")),
            "a disconnect ends the grant before the slot can be taken again")
    run = function_body(mp, "void idMultiplayerGame::Run( void )") if "void idMultiplayerGame::Run( void )" in mp else mp
    require("adminPassword->GetString()[ 0 ] != '\\0'" in run and "InitializeAdminAuthentication();" in run,
            "a password set while the server runs is taken at once")
    adapter = function_body(mp, "bool idMultiplayerGame::ApplyMatchOperationContinuation( int clientNum,")
    removal = adapter[adapter.index("if ( kind == MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE ) {"):]
    removal = removal[:removal.index("if ( kind == MP_OPERATION_CONTINUATION_ROSTER_SUBSTITUTE ) {")]
    require("( hostActs || !IsServerAdmin( targetSlot ) )" in removal
            and removal.index('gameLocal.ModerationLogClient( "kick", "match control", targetSlot, "", 0 );')
            < removal.index("BeginServerDrop( targetSlot );") < removal.index('networkSystem->ServerDropClient( targetSlot, "#str_07134" );')
            < removal.index("EndServerDrop( targetSlot );"),
            "a referee's removal of a player is a kick in the admin log, and only the host removes an admin")
    session = function_body(mp, "bool idMultiplayerGame::BeginMatchSession( void )")
    require(session.index("if ( !InitializeRefereeAuthentication() ) {") < session.index("InitializeAdminAuthentication();"),
            "every match session sets the admin login up, and a failure there does not stop the match")
    continuation = function_body(mp, "bool idMultiplayerGame::ApplyMatchOperationContinuation( int clientNum,")
    dispatch = continuation.find("if ( MatchAuthIsAdminCredential( credential ) ) {")
    require(0 < dispatch < continuation.find("!actor->human || actor->active ||")
            and "return ApplyAdminAuthentication( clientNum, request, execution );" in continuation[dispatch:dispatch + 160],
            "an admin login is handed to the admin service before the referee's checks, so a player may log in")
    login = function_body(mp, "bool idMultiplayerGame::ApplyAdminAuthentication( int clientNum,")
    verified = login.index("if ( verified != MP_REFEREE_AUTH_VERIFY_AUTHENTICATED ) {")
    grant = login.index("matchAdminConnection[ clientNum ] = matchConnectionId[ clientNum ];")
    require("matchRefereeAuthentication" not in login and "matchAdminAuthentication.IssueChallenge( binding, now, nonce," in login
            and login.index("matchAdminAuthentication.VerifyProof( binding, now,") < verified < grant
            and "MP_ADMIN_AUTH_PROOF_TAG" in login[:verified],
            "the grant is given only after the admin password's own proof is verified")
    require("!actor->human" in login[:login.index("mpRefereeAuthBinding binding;")]
            and "actor->active" not in login[:login.index("mpRefereeAuthBinding binding;")],
            "an admin may be playing; a bot may not log in")
    pacing = login[login.index("auto refused = [this, clientNum]( const char *why, bool wrongProof ) {"):login.index("if ( MatchAuthIsAdminChallengeRequest( credential ) ) {")]
    require(pacing.index("if ( gameNow < next && next - gameNow <= ADMIN_REFUSAL_NOTICE_MSEC ) {")
            < pacing.index('gameLocal.ServerSendChatMessage( clientNum, "server", "#str_232653", "" );')
            < pacing.index("if ( gameNow < nextLog && nextLog - gameNow <= ADMIN_LOCKOUT_LOG_MSEC ) {")
            < pacing.index('gameLocal.ModerationLogClient( "admin_login_failed", "server", clientNum,'),
            "a refused login is answered at most once in two seconds, and a locked-out connection is logged once a minute")
    require('refused( verified == MP_REFEREE_AUTH_VERIFY_THROTTLED ? "locked out" : "", true );' in login[verified:grant]
            and 'gameLocal.ModerationLogClient( "admin_login", "server", clientNum, "", 0 );' in login[grant:],
            "logins and failed logins are in the admin log")
    referee = login[grant:]
    require(referee.index("if ( !actor->active && matchSession.FindRosterSeat( actor->id ) < 0 ) {")
            < referee.index("MPMatchRoleBit( MP_MATCH_ROLE_REFEREE );"),
            "an admin who is watching becomes a referee as well; one who is playing does not")
    cleared = function_body(mp, "void idMultiplayerGame::ClearMatchOperationTransportSlot( int clientNum )")
    require("matchAdminAuthentication.InvalidateSlot( clientNum );" in cleared and "matchAdminConnection[ clientNum ] = 0;" in cleared,
            "a slot's next occupant inherits no grant")
    logout = function_body(mp, "bool idMultiplayerGame::ServerAdminLogout( int clientNum )")
    require("matchAdminConnection[ clientNum ] = 0;" in logout and "matchAdminAuthentication.InvalidateSlot( clientNum );" in logout
            and "matchAdminCarry[ clientNum ].Clear();" in logout and "!held || !gaveReferee ||" in logout
            and "~MPMatchRoleBit( MP_MATCH_ROLE_REFEREE )" in logout,
            "a logout gives up the grant and the referee role that came with it, never one earned with the referee's password")
    proof = function_body(mp, "bool idMultiplayerGame::CompleteRefereeAuthChallenge(")
    require(proof.index("pendingRefereePasswordIsAdmin ?") < proof.index("ClearPendingRefereePassword();", proof.index("MPRefereeAuthBuildProofFromPassword"))
            and "challenge.challengeGeneration, proof, credential, proofTag );" in proof,
            "the client tags its proof with the kind of login it asked for")
    require("pendingRefereePasswordIsAdmin = false;" in function_body(mp, "void idMultiplayerGame::ClearPendingRefereePassword( void )"),
            "a pending password forgets its kind when it is wiped")

    # the request: typed, bounded, for connected players only
    game = read("src/mpgame/Game_local.h")
    require(game.index("GAME_RELIABLE_MESSAGE_MAPDB,") < game.index("GAME_RELIABLE_MESSAGE_MODERATION,") < game.index("GAME_RELIABLE_MESSAGE_COUNT,"),
            "GAME_RELIABLE_MESSAGE_MODERATION is appended: the ordinals are the wire format")
    require("RELIABLE_FLOOD_MODERATION," in game and "const int RELIABLE_FLOOD_MODERATION_DELAY\t= 500;" in game,
            "an admin's requests are paced like chat")
    network = read("src/mpgame/Game_network.cpp")
    server = function_body(network, "void idGameLocal::ServerProcessReliableMessage( int clientNum, const idBitMsg &msg )")
    case = server.index("case GAME_RELIABLE_MESSAGE_MODERATION: {")
    require("CheckClientReliableFlood( clientNum, RELIABLE_FLOOD_MODERATION, RELIABLE_FLOOD_MODERATION_DELAY )" in server[case:case + 320]
            and "ServerModerationRequest( clientNum, msg );" in server[case:case + 420],
            "the server paces the request before it reads it")
    require(network.count("case GAME_RELIABLE_MESSAGE_MODERATION: {") == 2, "the server handles the request and the repeater ignores it")
    request = function_body(network, "void idGameLocal::ServerModerationRequest( int clientNum, const idBitMsg &msg )")
    gate = request.index("if ( !mpGame.IsServerAdmin( clientNum ) ) {")
    check = request.index("MPModAdminCheck( verb, clientNum, targetSlot, target, maxBanMinutes );")
    act = request.index("switch ( verb ) {")
    require(request.index("if ( msg.GetRemainingReadBits() != 0 ) {") < gate < check < act
            and '"#str_232654"' in request[gate:gate + 160] and "return;" in request[gate:gate + 200],
            "a request is read whole, then refused unless the connection is logged in, before anything is decided")
    require("GetSpawnId( entities[ targetSlot ] ) == identity" in request[gate:check]
            and "target.admin = mpGame.IsServerAdmin( targetSlot );" in request[gate:check]
            and "target.host = targetSlot == localClientNum;" in request[gate:check]
            and "target.bot = botManager.IsBot( targetSlot );" in request[gate:check]
            and "( i == clientNum || mpGame.IsServerAdmin( i ) )" in request[gate:check]
            and "MPModSamePlayer( clientModAddress[ i ], clientModAddress[ targetSlot ] )" in request[gate:check]
            and "target.coversAdmin = true;" in request[gate:check],
            "the target is the player the admin chose; the host, another admin, a bot and a range that holds an admin are refused")
    require("ServerBanClient( targetSlot, MPModAdminBanSeconds( seconds, maxBanMinutes ), reason, \"admin\" ) >= 0;" in request[act:],
            "an admin's ban is of the player's own range (and, since MP-17, their identity), for no longer than the host allows")
    for forbidden in ("MPModParseAddress", "RemoveBan", "ScheduleServerShutdown", "MPModFormatAddress", "BufferCommandText( CMD_EXEC_NOW, reason",
                      "GetBanListCount"):
        require(forbidden not in request, f"an admin's request never reaches {forbidden}: no addresses, no unban, no shutdown")
    require(request.count("cmdSystem->BufferCommandText(") == 1 and 'va( "kick %d\\n", targetSlot )' in request,
            "the only console line an admin's request runs is the kick of the slot the server checked")
    require(request.index("} actorScope( modActorClient, clientNum );") < act and "~actorScope_t() { actor = -1; }" in request
            and "modActorClient = -1;" not in request,
            "the admin log names the admin for what the request did, and for nothing after it, however the request ends")
    log = function_body(network, "void idGameLocal::ModerationWriteLog( mpModLogLine_t &line )")
    require(log.index('MPModLogNumber( line, "admin_slot", modActorClient );') < log.index("MPModLogEnd( line );")
            and '"admin_name"' in log and '"admin_address"' in log, "the admin log records the admin's slot, name and address")
    require(log.index('cvarSystem->GetCVarString( "net_serverRconFrom" )') < log.index('MPModLogString( line, "rcon", rconFrom );')
            < log.index("MPModLogEnd( line );"), "a console line that came by rcon is logged with where it came from")
    client = function_body(network, "bool idGameLocal::ClientModerationRequest( int verb, int clientNum, mpModTime_t seconds, const char *reason )")
    require("identity = GetSpawnId( entities[ clientNum ] );" in client and "outMsg.WriteByte( GAME_RELIABLE_MESSAGE_MODERATION );" in client,
            "the client names the player by slot and spawn identity")
    forward = function_body(network, "static void ModerationClientCommand( int verb, const idCmdArgs &args )")
    require("ModerationFindClient( argv[ 1 ] )" in forward and "MPModParseAddress" not in forward and "MPModJoinAddressArgs" not in forward,
            "from a client the commands take a connected player, never an address")
    for name, signature in (("unban", COMMANDS["unban"][1]), ("banlist", COMMANDS["banlist"][1]), ("status", COMMANDS["status"][1]),
                            ("serverShutdown", COMMANDS["serverShutdown"][1])):
        require("ModerationClientCommand" not in function_body(network, signature), f"{name} is never an admin's request")

    cmds = read("src/mpgame/gamesys/SysCmds.cpp")
    require(re.search(r'cmdSystem->AddCommand\( "admin",\s*idGameLocal::Admin_f,\s*CMD_FL_GAME,', cmds) is not None, "admin is a game command")
    kick = function_body(cmds, "static void Cmd_Kick_f( const idCmdArgs &args )")
    require(kick.index("if ( gameLocal.isClient ) {") < kick.index("idGameLocal::ClientKick_f( args );") < kick.index("gameLocal.GetClientByCmdArgs( args )"),
            "gameKick on a client is an admin's request")
    admin = function_body(network, "void idGameLocal::Admin_f( const idCmdArgs &args )")
    require("gameLocal.mpGame.ServerAdminLoginsOff();" in admin and "gameLocal.mpGame.ServerAdminLogout( i )" in admin
            and 'gameLocal.ModerationLogClient( "admin_logout", "console", i, "revoked", 0 );' in admin
            and "gameLocal.mpGame.IsServerAdmin( i )" in admin,
            "the host sees who is an admin, ends a login and withdraws the password from the server's console")
    require('const char *password = cvarSystem->GetCVarString( "net_clientAdminPassword" );' in admin and "RequestAdminAuthentication( password )" in admin
            and "args.Argc() != 1" in admin and "RequestAdminAuthentication( args" not in admin,
            "the password comes from the private cvar and never from the command line, which the console history keeps")
    require("MP_MOD_ADMIN_LOGOUT" in admin, "admin logout gives the right up")

    guide = read("docs/user/server-security.md")
    for token in ("net_serverAdminPassword", "net_clientAdminPassword", "g_adminMaxBanMinutes", "admin logout", "admin_login",
                  "admin_name", "gameKick"):
        require(token in guide, f"the server security guide covers {token}")


def check_text() -> None:
    expected = {"232640": None, "232641": None, "232642": None, "232643": None, "232644": None, "232645": None,
                "232646": None, "232647": None, "232648": None, "232649": None, "232650": None, "232651": None,
                "232652": None, "232653": None, "232654": None, "232656": None, "232657": None}
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for ident in expected:
            match = re.search(r'^\t"#str_' + ident + r'"\t"([^"\n]+)"$', table, re.M)
            require(match is not None and len(match.group(1)) > 8, f"{language}: #str_{ident} is present")
        # the server puts the player's name after this one
        require(re.search(r'^\t"#str_232655"\t"[^"\n]+: "$', table, re.M) is not None, f"{language}: #str_232655 ends where the name goes")
    english = read("content/baseoq4/pak0/strings/english_openq4.lang")
    require('"#str_232640"\t"Too many players are connected from your address."' in english, "the per-address refusal text")
    require('"#str_232641"\t"You are muted on this server. Your chat reaches nobody."' in english, "the mute notice")
    guide = read("docs/user/server-security.md")
    for token in ("ban <", "unban <", "banlist", "mute <", "unmute <", "status", "banlist.txt", "logs/admin.ndjson",
                  "g_chatFloodMessages", "g_chatFloodSeconds", "g_chatFloodWait", "g_reservedNames", "g_adminLog",
                  "net_serverMaxClientsPerAddress", "serverShutdown", "callvote mute", "g_voteMuteMinutes",
                  "g_matchLog", "logs/match.ndjson"):
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
    check_match_log()
    check_admins()
    check_text()
    print("mp_moderation: ok")


if __name__ == "__main__":
    main()
