#!/usr/bin/env python3
"""Contract checks for the Instagib modifier (si_instagib).

Instagib is a rule set over any multiplayer game type: everyone carries an
unlimited railgun and the gauntlet, any hit from another player kills and gibs,
falls do no harm, and the map's pickups are not spawned. Most of what can go
wrong is a peer disagreement - a client populating the map for other rules than
the server - or another loadout path running after Instagib's. These checks pin
the places that keep it whole.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MP = ROOT / "src/mpgame"
LANGUAGES = ("english", "brazilian", "czech", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> int:
    position = text.find(token)
    if position < 0:
        raise AssertionError(f"missing {token!r} in {context}")
    return position


def body(text: str, signature: str) -> str:
    """The text of the function that starts at `signature`, up to the next banner."""
    start = require(text, signature, signature)
    end = text.find("\n/*\n", start)
    return text[start:end if end > 0 else len(text)]


def validate_cvar() -> None:
    pattern = re.compile(r'^idCVar si_instagib\(.*$', re.M)
    mp = pattern.findall(read(MP / "gamesys/SysCvar.cpp"))
    sp = pattern.findall(read(ROOT / "src/game/gamesys/SysCvar.cpp"))
    if len(mp) != 1 or len(sp) != 1:
        raise AssertionError("si_instagib must be declared once in each game tree")
    # The create-server menu binds it under game_sp; a different default or help
    # makes the module swap warn and changes what the menu shows.
    if mp[0] != sp[0]:
        raise AssertionError("si_instagib's game_sp declaration must match game_mp's")
    for token in ('"0"', "CVAR_SERVERINFO", "CVAR_BOOL", "PC_CVAR_ARCHIVE"):
        require(mp[0], token, "si_instagib declaration")


def validate_latch() -> None:
    game = read(MP / "MultiplayerGame.cpp")
    set_type = body(game, "void idMultiplayerGame::SetGameType( void ) {")
    require(set_type, "instagib = InstagibWanted();", "idMultiplayerGame::SetGameType")
    wanted = body(game, "bool idMultiplayerGame::InstagibWanted( void ) const {")
    # The Arena campaign is hosted by game_mp; an archived preference must not reach it.
    require(wanted, "!IsArenaCampaignMatch()", "idMultiplayerGame::InstagibWanted")
    change = body(game, "void idMultiplayerGame::CheckInstagibChange( void ) {")
    require(change, "instagibRestartPending", "idMultiplayerGame::CheckInstagibChange")
    require(change, "serverMapRestart", "idMultiplayerGame::CheckInstagibChange")
    run = body(game, "void idMultiplayerGame::Run( void ) {")
    require(run, "CheckInstagibChange();", "idMultiplayerGame::Run")
    buying = body(game, "bool idMultiplayerGame::IsBuyingAllowedInTheCurrentGameMode( void ) {")
    require(buying, "IsInstagib()", "idMultiplayerGame::IsBuyingAllowedInTheCurrentGameMode")
    title = body(game, "const char *idMultiplayerGame::LocalizeGametype( void ) {")
    require(title, '"#str_231961"', "idMultiplayerGame::LocalizeGametype")


def validate_pickups() -> None:
    local = read(MP / "Game_local.cpp")
    require(local, "MPGameTypeHasAny( gameType, GTF_FULLARSENAL ) || mpGame.IsInstagib()", "InhibitEntitySpawn")
    # DeadZone's token is a powerup by classname and an objective by role.
    require(local, '"riDeadZonePowerup"', "InhibitEntitySpawn objective classes")
    require(local, '"si_instagib" };', "retainedLoadingServer query keys")
    session = read(ROOT / "src/framework/Session.cpp")
    require(session, '"si_instagib" };', "idSessionLocal::PublishRetainedLoadingServer")


def validate_player() -> None:
    player = read(MP / "Player.cpp")
    spawn = body(player, "void idPlayer::SpawnToPoint(")
    grant = require(spawn, "GiveInstagibLoadout();", "idPlayer::SpawnToPoint")
    # The warmup arsenal and Clan Arena's loadout must stand aside, or they
    # would hand out the arsenal over Instagib's.
    warmup = require(spawn, '!instagib && gameLocal.serverInfo.GetBool( "si_warmupWeapons" )', "warmup arsenal gate")
    arsenal = require(spawn, "!instagib &&\n\t\t MPGameTypeHasAny( gameLocal.gameType, GTF_FULLARSENAL )", "full arsenal gate")
    if not grant < warmup < arsenal:
        raise AssertionError("Instagib's loadout must be decided before the warmup and full-arsenal grants")
    loadout = body(player, "void idPlayer::GiveInstagibLoadout( void ) {")
    for token in ('SlotForWeapon( "weapon_railgun" )', 'SlotForWeapon( "weapon_gauntlet" )',
                  "inventory.weapons = ", "inventory.AmmoIndexForWeaponIndex( railgun ) ] = -1", "idealWeapon = railgun"):
        require(loadout, token, "idPlayer::GiveInstagibLoadout")

    damage = body(player, "void idPlayer::Damage(")
    calc = require(damage, "CalcDamagePoints( inflictor, attacker", "idPlayer::Damage")
    hit = require(damage, "const bool instagibHit", "idPlayer::Damage")
    knock = require(damage, "// determine knockback", "idPlayer::Damage")
    if not calc < hit < knock:
        raise AssertionError("the Instagib hit must be decided after CalcDamagePoints and before it is applied")
    rule = damage[hit:knock]
    for token in ("damage > 0", "attacker != this", "attacker->IsType( idPlayer::GetClassType() )",
                  "damage = Max( damage, health );", "armorSave = 0;"):
        require(rule, token, "Instagib damage rule")
    gib = require(damage, "health = Min( health, INSTAGIB_GIB_HEALTH );", "idPlayer::Damage")
    require(damage[gib - 200:gib], "!MPGameTypeHasAny( gameLocal.gameType, GTF_FREEZE )", "Instagib gib")
    if not re.search(r"const int\s+INSTAGIB_GIB_HEALTH\s*=\s*-\d+;", player):
        raise AssertionError("INSTAGIB_GIB_HEALTH must be a negative constant")

    crash = body(player, "void idPlayer::CrashLand(")
    require(crash, "gameLocal.mpGame.IsInstagib()", "idPlayer::CrashLand")
    drop = body(player, "void idPlayer::DropWeapon( void ) {")
    require(drop, "gameLocal.mpGame.IsInstagib()", "idPlayer::DropWeapon")


def validate_menu() -> None:
    menu = read(ROOT / "src/framework/Session_menu.cpp")
    require(menu, '{ "createInstagib",\t\t"instagib",\t\t"si_instagib",', "RETAINED_CREATE_SETTINGS")
    generator = read(ROOT / "tools/ui/retained_create_server.py")
    require(generator, '("instagib", "createInstagib", "#str_231960", "toggle", 0, 1, None)', "create-server ADVANCED rows")
    require(generator, '("#str_41704", ("create-instagib",', "create-server RULES tab")
    document = read(ROOT / "content/baseoq4/pak0/guis/menu/createserver.rui")
    require(document, '"createInstagib"', "createserver.rui (regenerate with build_retained_screens.py)")


def validate_strings() -> None:
    for language in LANGUAGES:
        table = read(ROOT / f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        require(table, '"#str_231960"\t"Instagib"', f"{language}_openq4.lang")
        title = re.search(r'"#str_231961"\t"([^"]*)"', table)
        if title is None or title.group(1).count("%s") != 1:
            raise AssertionError(f"{language}_openq4.lang: #str_231961 must name the game type with exactly one %s")


def main() -> int:
    validate_cvar()
    validate_latch()
    validate_pickups()
    validate_player()
    validate_menu()
    validate_strings()
    print("mp_instagib_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
