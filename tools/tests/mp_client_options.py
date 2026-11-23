#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Exercise and pin the MP-15 competitive client options.

Compiles tools/tests/native/MpClientOptionsTest.cpp, which runs the engine-free
cores (chat tokens, weapon preference order, hit sounds by damage, landmark
place names, team overlay packing, the item-timer policy), then pins the places
the game module calls them from: the say command, the pickup and empty-weapon
paths, the hit pulse, the two unreliable messages and who they are sent to.
See docs/user/multiplayer-client-options.md.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tmp" / "mp-client-options-test"
LANGUAGES = ("brazilian", "czech", "english", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")


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
    source = ROOT / "tools/tests/native/MpClientOptionsTest.cpp"
    exe = OUT / ("mp_client_options.exe" if os.name == "nt" else "mp_client_options")
    if os.name == "nt":
        command = OUT / "build.cmd"
        command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                           "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                               "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", str(source),
                               "/Fo:" + str(OUT / "mp_client_options.obj"), "/Fe:" + str(exe)]) + "\n",
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
        return match.group(0)

    timers = declaration("si_itemTimers")
    require("CVAR_SERVERINFO" in timers and '"1"' in timers and ", 0, 2 )" in timers,
            "si_itemTimers is a server-info policy from 0 to 2 that ships at spectators only")
    for name in ("si_maxBrightskin", "si_forcedModels"):
        require("CVAR_SERVERINFO" in declaration(name), f"{name} travels in the server info so every client applies it")
    order = declaration("ui_weaponOrder")
    require("CVAR_USERINFO" in order and "CVAR_ARCHIVE" in order,
            "ui_weaponOrder travels in the user info: the server makes both weapon decisions")
    for name in ("hud_hitSound", "hud_crosshairAmmo", "hud_crosshairNames", "hud_itemTimers", "hud_teamLocations",
                 "hud_localClock"):
        text = declaration(name)
        require("CVAR_ARCHIVE" in text and "CVAR_SERVERINFO" not in text and "CVAR_USERINFO" not in text,
                f"{name} is the player's own archived display choice")
    require(", 0, 3 )" in declaration("hud_hitSound"), "hud_hitSound offers off, Quake 4 and the two tone sets")
    # multiplayer only: the single-player module carries none of this
    for name in ("ChatTokens.h", "WeaponOrder.h", "HitSounds.h", "Locations.h", "TeamInfo.h", "ItemTimers.h"):
        require((ROOT / "src/mpgame/mp" / name).is_file(), f"src/mpgame/mp/{name} exists")
        require(not (ROOT / "src/game/mp" / name).exists(), f"{name} is not mirrored into the single-player module")
    require("hud_hitSound" not in read("src/game/gamesys/SysCvar.cpp"), "game-sp registers no MP-15 cvar")


def check_chat() -> None:
    cmds = read("src/mpgame/gamesys/SysCmds.cpp")
    say = function_body(cmds, "static void Cmd_Say( bool team, const idCmdArgs &args )")
    expand = say.find("MPChatExpandForPlayer( gameLocal.GetLocalPlayer(), text.c_str(), args.Argc() > 2, expanded )")
    send = say.find("GAME_RELIABLE_MESSAGE_TCHAT")
    local = say.find("gameLocal.mpGame.ProcessChatMessage(")
    require(0 < expand < send and expand < local, "the say command expands tokens on the sender, before the line is sent")
    require('cvarSystem->GetCVarInteger( "net_serverDedicated" ) == 0' in say[:expand],
            "a dedicated server's console is not expanded for a player it does not have")
    adapter = read("src/mpgame/mp/ChatTokens.cpp")
    body = function_body(adapter, "int MPChatExpandForPlayer( idPlayer *player, const char *text, bool consoleWords, idStr &expanded )")
    require("player->spectating" in body and "MP_CHAT_TOKEN_TEXT_BYTES" in body,
            "a spectator's line is left alone and an expanded line keeps the chat budget")
    require("MPChatRejoinTokens( text, words, sizeof( words ) );" in body and "if ( consoleWords )" in body,
            "a token typed as console words (a key binding) is put back together before it is expanded")
    nearby = function_body(adapter, "static idStr ChatNearbyText( idPlayer *player )")
    require("mate->fl.networkStale" in nearby and "CHAT_NEARBY_DISTANCE" in nearby and "mate->team != player->team" in nearby,
            "#n names only team mates this end currently sees nearby")
    require('cmdSystem->AddCommand( "listLocations"' in cmds, "listLocations is registered")


def check_weapon_order() -> None:
    player = read("src/mpgame/Player.cpp")
    give = player[player.index("bool idInventory::Give("):]
    give = give[:give.index("\n}\n")]
    switch = give.index("owner->AutoSwitchOnPickup( *idealWeapon, i )")
    require("IsBuyingAllowedInTheCurrentGameMode()" in give[give.rindex("if ( idealWeapon && !checkOnly", 0, switch):switch],
            "buying still switches to what was bought")
    require('GetBool( "ui_autoSwitch" )' not in give, "ui_autoSwitch is read as its three modes, not as Quake 4's switch")
    pickup = function_body(player, "bool idPlayer::AutoSwitchOnPickup( int held, int picked )")
    smart = pickup.find("MPSmartSwitchAllows(")
    ranked = pickup.find("MPWeaponOrderSwitchOnPickup( order, held, picked )")
    require('GetInt( "ui_autoSwitch" )' in pickup and "mode <= MP_AUTO_SWITCH_NEVER" in pickup
            and "mode >= MP_AUTO_SWITCH_SMART" in pickup[:smart] and 0 < smart < ranked
            and "!MPWeaponOrderActive( order )" in pickup[smart:],
            "smart switching decides first, then a pickup switches only upward in the player's order, "
            "and as Quake 4 does without one")
    require("inventory.HasAmmo( heldWeapon ) != 0" in pickup[:smart], "an empty weapon in hand gives way to a pickup")
    role = function_body(player, "int idPlayer::SmartSwitchRole( int slot )")
    require("AmmoIndexForWeaponClass( weap, &ammoRequired )" in role and "MPSmartSwitchRole(" in role,
            "a weapon that needs no ammunition is one smart switching leaves for anything")
    for module in ("src/game/gamesys/SysCvar.cpp", "src/mpgame/gamesys/SysCvar.cpp"):
        line = re.search(r'^idCVar ui_autoSwitch\(.*$', read(module), re.M).group(0)
        require('"2"' in line and "CVAR_INTEGER" in line and "CVAR_USERINFO" in line and ", 0, 2," in line,
                f"{module}: ui_autoSwitch ships as Smart (2) in the user info, from 0 to 2")
    common = read("src/framework/Common.cpp")
    migrate = function_body(common, "static void Common_MigrateLegacyAutoSwitchDefault( void )")
    require('idStr::Cmp( autoSwitch, "1" ) == 0' in migrate and 'SetCVarInteger( "ui_autoSwitch", 2, CVAR_ARCHIVE )' in migrate
            and "ui_autoSwitchDefaultMigrated.SetBool( true )" in migrate,
            "a profile on Quake 4's old default moves to Smart once; 0 is left alone")
    best = function_body(player, "void idPlayer::NextBestWeapon( void )")
    ranked = best.find("MPWeaponOrderBest( order, usable )")
    stock = best.find("while ( w > 0 )")
    require(0 < ranked < stock and "inventory.HasAmmo( weap )" in best[:ranked],
            "an empty weapon falls back to the best ranked weapon with ammunition, then to Quake 4's choice")
    order = function_body(player, "const mpWeaponOrder_t &idPlayer::GetWeaponOrder( void )")
    require("!IsFakeClient()" in order and 'GetString( "ui_weaponOrder" )' in order and "weaponOrderText.Cmp( text )" in order,
            "the order is read from the player's user info, reparsed when it changes, and bots have none")


def check_hit_sounds() -> None:
    player = read("src/mpgame/Player.cpp")
    pulse = function_body(player, "void idPlayer::TriggerHitSound( bool armor )")
    require("rvHitSounds::OnPulse( this, armor );" in pulse and "PlayShaderDirectly" not in pulse,
            "the snapshot hit pulse sounds through rvHitSounds and nowhere else")
    feedback = read("src/mpgame/mp/HitFeedback.cpp")
    receive = function_body(feedback, "void rvDamageNumbers::ClientReceive( const idBitMsg &msg )")
    require("rvHitSounds::OnHitInfo( gameLocal.GetLocalPlayer(), damage, flags );" in receive,
            "the hit-info message names the amount for the tone")
    send = function_body(feedback, "void rvDamageNumbers::ServerSend(")
    require("rvHitSounds::OnHitInfo( attackingPlayer, reported, flags );" in send,
            "a listen server's host hears tones for its own hits")
    sounds = read("src/mpgame/mp/HitSounds.cpp")
    on_info = function_body(sounds, "void rvHitSounds::OnHitInfo( idPlayer *player, int damage, int hitFlags )")
    require("HITFLAG_TEAM | HITFLAG_SELF" in on_info, "team and self hits stay silent, as in Quake 4")
    play = function_body(sounds, "static void HitSoundPlay( idPlayer *player, bool armor, float pitch )")
    require('"snd_armorHit"' in play and '"snd_weaponHit"' in play and "parms.frequencyShift = pitch;" in play,
            "a tone is Quake 4's own hit or armour-hit sound at a pitch: no audio is added")
    require("gameLocal.random" not in sounds, "a cosmetic sound must not draw from the prediction random stream")
    game = read("src/mpgame/MultiplayerGame.cpp")
    require("rvHitSounds::Frame();" in function_body(game, "void idMultiplayerGame::CommonRun( void )"),
            "an unanswered pulse is settled once a frame on servers and clients")


def check_messages() -> None:
    local = read("src/mpgame/Game_local.h")
    enum = local[local.index("GAME_UNRELIABLE_MESSAGE_EVENT,"):]
    enum = enum[:enum.index("};")]
    names = re.findall(r"GAME_UNRELIABLE_MESSAGE_\w+", enum)
    require(names[-4:] == ["GAME_UNRELIABLE_MESSAGE_HITINFO", "GAME_UNRELIABLE_MESSAGE_TEAMINFO",
                           "GAME_UNRELIABLE_MESSAGE_ITEMTIMERS", "GAME_UNRELIABLE_MESSAGE_COUNT"],
            "the two messages are appended after HITINFO: the ordinals are the wire format")
    network = read("src/mpgame/Game_network.cpp")
    process = function_body(network, "void idGameLocal::ProcessUnreliableMessage( const idBitMsg &msg )")
    require("type >= GAME_UNRELIABLE_MESSAGE_COUNT" in process, "an unknown message type is refused by the enum's own bound")
    require("mpGame.teamInfo.ClientReceive( msg );" in process and "mpGame.itemTimers.ClientReceive( msg );" in process,
            "both messages are dispatched")
    record = function_body(network, "void idGameLocal::RecordUnreliableMessage( const idBitMsg &msg )")
    require("QueueUnreliableMessage" not in record and "dest.WriteChar( -1 );" in record,
            "RecordUnreliableMessage reaches recordings only, addressed to every viewer")

    team = read("src/mpgame/mp/TeamInfo.cpp")
    frame = function_body(team, "void rvTeamInfo::ServerFrame( void )")
    send = function_body(team, "static void TeamInfoSend( int recipient, const mpTeamInfoEntry_t *records, int count )")
    require("gameLocal.QueueUnreliableMessage( recipient, msg );" in send and "TeamInfoSend( recipient, mates, count );" in frame
            and "SendUnreliableMessage(" not in team and "RecordUnreliableMessage" not in team,
            "team info goes to its recipient alone: not to who follows them, not into a recording")
    require("mate->team != member->team" in frame, "a member is sent their own team and nobody else")
    watch = frame.find("->wantSpectate )")
    require(0 < watch < frame.find("gameLocal.mpGame.IsManagedMatch()") < frame.find("TeamInfoSend( recipient, watched, count );"),
            "someone who chose to watch hears about every player, but not in a managed match")
    trailer = send.rfind("msg.WriteByte( records[ i ].ammo );")
    require(send.rfind("msg.WriteShort( records[ i ].origin[ 2 ] );") < trailer,
            "the ammunition trails the entries, so a 0.15.0 client reads the message as before")
    receive = function_body(team, "void rvTeamInfo::ClientReceive( const idBitMsg &msg )")
    require("entry.clientNum < MAX_CLIENTS" in receive and "GetRemainingReadBits()" in receive
            and "msg.GetRemainingReadBits() < read * 8" in receive,
            "a received record is bounded before it is kept, and an older server's missing trailer leaves ammunition unknown")
    resolve = function_body(team, "void rvTeamInfo::Resolve( const idPlayer *player, mpTeamInfoEntry_t &entry ) const")
    uses = function_body(team, "bool rvTeamInfo::UsesRecord( const idPlayer *player ) const")
    require("UsesRecord( player )" in resolve and "TeamInfoFromEntity( player, entry );" in resolve,
            "a row is the server's record when there is one for it and the player's own entity otherwise")
    require("gameLocal.isClient" in uses and "( local->wantSpectate || player->team == local->team )" in uses
            and "!gameLocal.IsServerDemoPlaying()" in uses and "MPTeamInfoFresh(" in uses,
            "a record is used only while fresh and for the viewer's own side, or by someone who chose to watch; "
            "a server and a demo read the players directly")

    timers = read("src/mpgame/mp/ItemTimers.cpp")
    frame = function_body(timers, "void rvItemTimers::ServerFrame( void )")
    admit = frame.find("MPItemTimersAdmit( policy, managed, player->wantSpectate )")
    queue = frame.find("gameLocal.QueueUnreliableMessage( recipient, msg );")
    require(0 < admit < queue and "SendUnreliableMessage(" not in timers,
            "the item-timer policy is enforced where the list is sent, client by client")
    require("gameLocal.mpGame.IsManagedMatch()" in frame and "policy <= ITEMTIMERS_OFF || managed" in frame,
            "a managed match keeps its own disclosure policy: the casual stream stays silent")
    rows = function_body(timers, "int rvItemTimers::GetRows( itemTimerRow_t *rows, int maxRows ) const")
    require("MPItemTimersAdmit( si_itemTimers.GetInteger(), gameLocal.mpGame.IsManagedMatch(), local->wantSpectate )" in rows,
            "a listen server's host reads the timers under the same policy as everyone else")
    game = read("src/mpgame/MultiplayerGame.cpp")
    run = function_body(game, "void idMultiplayerGame::Run( void )")
    rebase = function_body(game, "void idMultiplayerGame::RebaseCompetitivePauseFrame( int deltaMsec )")
    require("itemTimers.ShiftMatchTime( deltaMsec );" in rebase,
            "a pause moves the item timers with the respawns they describe")
    require("teamInfo.ServerFrame();" in run and "itemTimers.ServerFrame();" in run, "the server runs both streams each frame")
    require("itemTimers.OnPickup( item, respawnSeconds );" in game and "itemTimers.OnAvailable( item );" in game
            and "gameLocal.mpGame.itemTimers.OnHeld( this );" in read("src/mpgame/Item.cpp"),
            "the timers follow every pickup, return and held mega health")


def check_cosmetics() -> None:
    player = read("src/mpgame/Player.cpp")
    strength = function_body(player, "static float Player_VisibilityStrength(")
    require("Min( teamColor ? teamValue : enemyValue, Player_ServerVisibilityLimit() )" in strength
            and strength.index("spectatorView") < strength.index("Player_ServerVisibilityLimit"),
            "si_maxBrightskin caps what a player puts on other players; someone watching keeps their setting")
    forced = function_body(player, "static const char *Player_ForcedModelCVarString( const idCVar &cvar )")
    require('gameLocal.serverInfo.GetBool( "si_forcedModels", "1" )' in forced, "si_forcedModels 0 turns forced models off")
    names = player[player.index("// openQ4 (MP-15): hud_crosshairNames"):]
    require("hud_crosshairNames.GetInteger() == 2 && gameLocal.IsTeamGame() && aimed->team == team" in names[:800],
            "hud_crosshairNames 2 names team mates only")


def check_hud_state() -> None:
    game = read("src/mpgame/MultiplayerGame.cpp")
    publish = function_body(game, "void idMultiplayerGame::PublishClientOptions( idUserInterface *_mphud, idPlayer *localPlayer )")
    for key in ("oq4_xhair_ammo", "oq4_it_count", "oq4_it_%d_name", "oq4_it_%d_time", "oq4_it_%d_up", "oq4_local_clock"):
        require(f'"{key}"' in publish, f"{key} is published")
    require("hud_itemTimers.GetBool() ? itemTimers.GetRows(" in publish, "the player can hide the timers they are sent")
    call = game.index("PublishClientOptions( _mphud, localPlayer );")
    require(call < game.index("PublishRemasteredHud( _mphud, localPlayer );\n\t_mphud->StateChanged"),
            "the options are published for every layout, before the Remastered keys")
    rows = function_body(game, "void idMultiplayerGame::PublishRemasteredCompetitive(")
    require("teamInfo.Resolve( player, info );" in rows and '"oq4_ts%d_%d_location"' in rows,
            "the team list reads team mates through the team info and names where they are")
    require("neutralView || player->team == localPlayer->team" in rows
            and "const bool neutralView = localPlayer->spectating && localPlayer->wantSpectate;" in rows,
            "an opponent's row is never given a location, and an eliminated player is not a neutral spectator")
    require('"%s_location"' in rows and "showLocations && open && !dead" in rows,
            "the scoreline names places only under the tiles this viewer may look over (hud_teamLocations)")


def check_strings() -> None:
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for key in ("#str_232300", "#str_232301"):
            match = re.search(r'"' + key + r'"\s+"([^"]+)"', table)
            require(match is not None and match.group(1).strip() != "", f"{language} carries {key}")


def main() -> int:
    try:
        run_native()
        check_cvars()
        check_chat()
        check_weapon_order()
        check_hit_sounds()
        check_messages()
        check_cosmetics()
        check_hud_state()
        check_strings()
    except (AssertionError, ValueError, subprocess.CalledProcessError) as error:
        print(f"mp_client_options: FAIL: {error}")
        return 1
    print("mp_client_options: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
