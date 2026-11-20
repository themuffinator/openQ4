#!/usr/bin/env python3
"""Contract checks for the multiplayer modifier layer and presets (roadmap MP-14).

The modifiers (Instagib, Quad Hog, Vampiric Damage, Weapons Frenzy, the
single-weapon arenas), the powerup switch and the scales are a rules layer over
any game type; a preset names a set of them. Three kinds of thing can go wrong:
the peers disagreeing about what was latched, one of the copies of a table
drifting from the game's (the browser's, the Create Server page's), and a
preset file changing something it has no business changing. The first half of
this file pins the places that keep those whole; the second compiles the real
mp/Modifiers.cpp and mp/Presets.cpp against small doubles and runs them.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MP = ROOT / "src/mpgame"
STRINGS = ROOT / "content/baseoq4/pak0/strings"
LANGUAGES = ("english", "brazilian", "czech", "french", "german", "hungarian", "italian", "polish", "russian",
             "spanish", "turkish", "ukrainian")
SWITCHES = ("si_quadHog", "si_vampiric", "si_weaponsFrenzy", "si_weaponArena", "si_powerups", "si_selfDamage",
            "si_knockback", "si_fallDamage", "si_weaponRespawn", "si_preset")
# Strings with printf arguments, and how many each takes.
FORMATS = {"#str_231961": 1, "#str_231963": 1, "#str_231965": 1, "#str_231967": 1, "#str_231969": 2, "#str_231970": 1,
           "#str_231978": 1}


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


def table(text: str, declaration: str) -> str:
    start = require(text, declaration, declaration)
    return text[start:text.index("};", start)]


def game_tables() -> tuple[list, list, list]:
    """(modifiers, weapon arenas, presets) as the game declares them."""
    modifiers_source = read(MP / "mp/Modifiers.cpp")
    modifiers = re.findall(r'\{ (MPMOD_\w+),\s*"(\w+)",\s*"(si_\w+)",\s*"(#str_\d+)",\s*"(#str_\d+)" \},',
                           table(modifiers_source, "static const mpModifierInfo_t mpModifierTable[ MPMOD_COUNT ] = {"))
    arenas = re.findall(r'\{ "(\w+)",\s*"(weapon_\w+)",\s*"(#str_\d+)" \},',
                        table(modifiers_source, "static const mpWeaponArenaInfo_t mpWeaponArenaTable[] = {"))
    presets = re.findall(r'\{ "([a-z0-9_-]+)",\s*"(#str_\d+)",\s*"([^"]*)" \},',
                         table(read(MP / "mp/Presets.cpp"), "static const mpPresetInfo_t mpPresetTable[] = {"))
    if len(modifiers) != 4 or len(arenas) != 10 or len(presets) < 2:
        raise AssertionError(f"could not read the game tables: {len(modifiers)} modifiers, {len(arenas)} arenas, {len(presets)} presets")
    return modifiers, arenas, presets


def validate_cvars() -> None:
    mp = read(MP / "gamesys/SysCvar.cpp")
    sp = read(ROOT / "src/game/gamesys/SysCvar.cpp")
    header = read(MP / "gamesys/SysCvar.h")
    for name in SWITCHES:
        pattern = re.compile(r"^idCVar %s\(.*$" % name, re.M)
        ours, theirs = pattern.findall(mp), pattern.findall(sp)
        if len(ours) != 1 or len(theirs) != 1:
            raise AssertionError(f"{name} must be declared once in each game tree")
        # The create-server menu binds it under game_sp; a different default,
        # range or help makes the module swap warn and changes what the menu shows.
        if ours[0] != theirs[0]:
            raise AssertionError(f"{name}'s game_sp declaration must match game_mp's")
        require(ours[0], "CVAR_SERVERINFO", name)
        require(header, f"extern idCVar\t{name};", "SysCvar.h")
    published = re.search(r"^idCVar si_modifiers\(.*$", mp, re.M)
    if published is None or "CVAR_SERVERINFO | CVAR_ROM" not in published.group(0):
        raise AssertionError("si_modifiers must be a read-only server-info cvar")
    for name, default in (("si_powerups", '"1"'), ("si_selfDamage", '"100"'), ("si_knockback", '"100"'),
                          ("si_fallDamage", '"100"'), ("si_weaponRespawn", '"0"'), ("si_weaponArena", '"none"'),
                          ("si_quadHog", '"0"'), ("si_vampiric", '"0"'), ("si_weaponsFrenzy", '"0"'), ("si_preset", '""')):
        line = re.search(r"^idCVar %s\(.*$" % name, mp, re.M).group(0)
        if not re.search(r'"%s",\s*%s,' % (name, re.escape(default)), line):
            raise AssertionError(f"{name} must default to {default}: the stock game is every switch at its default")
    for name in ("si_selfDamage", "si_knockback", "si_fallDamage", "si_weaponRespawn"):
        line = re.search(r"^idCVar %s\(.*$" % name, mp, re.M).group(0)
        require(line, ", 0, 999 );", f"{name} range (the Create Server row's and the game's clamp)")
    header_text = read(MP / "mp/Modifiers.h")
    require(header_text, "MP_SCALE_PERCENT_MAX\t\t= 999;", "mp/Modifiers.h")
    require(header_text, "MP_WEAPON_RESPAWN_MAX\t\t= 999;", "mp/Modifiers.h")


def validate_tables() -> None:
    modifiers, arenas, presets = game_tables()
    header = read(MP / "mp/Modifiers.h")
    enum = re.findall(r"^\t(MPMOD_\w+)", header[header.index("typedef enum {"):header.index("} mpModifier_t;")], re.M)
    if enum != [row[0] for row in modifiers] + ["MPMOD_COUNT"]:
        raise AssertionError(f"mpModifierTable must follow mpModifier_t: {enum}")
    mp_cvars = read(MP / "gamesys/SysCvar.cpp")
    for _, name, cvar, _, _ in modifiers:
        require(mp_cvars, f"idCVar {cvar}(", f"modifier {name}")
    # The arena cvar's help names every arena an operator can type.
    arena_help = re.search(r"^idCVar si_weaponArena\(.*$", mp_cvars, re.M).group(0)
    for name, _, _ in arenas:
        require(arena_help, name, "si_weaponArena help")

    # The server browser reads si_modifiers with its own copy of the tables.
    scan = read(ROOT / "src/framework/async/ServerScan.cpp")
    mirrored = re.findall(r'\{ "(\w+)",\s*"(#str_\d+)",\s*"(#str_\d+)" \},',
                          table(scan, "static const serverBrowserModifier_t SERVER_BROWSER_MODIFIERS[] = {"))
    if mirrored != [(name, label, title) for _, name, _, label, title in modifiers]:
        raise AssertionError("SERVER_BROWSER_MODIFIERS drifted from mpModifierTable")
    mirrored_arenas = re.findall(r'\{ "arena:(\w+)",\s*"(#str_\d+)",\s*NULL \},',
                                 table(scan, "static const serverBrowserModifier_t SERVER_BROWSER_WEAPON_ARENAS[] = {"))
    if mirrored_arenas != [(name, label) for name, _, label in arenas]:
        raise AssertionError("SERVER_BROWSER_WEAPON_ARENAS drifted from mpWeaponArenaTable")
    reply = read(ROOT / "src/framework/async/OutOfBandReply.h")
    require(table(reply, "static const char *const BROWSER_KEYS[] = {"), '"si_modifiers"', "BROWSER_KEYS")
    details = body(scan, "bool idServerScan::GetServerDetails(")
    require(details, "ServerBrowserModifierLabels( info )", "idServerScan::GetServerDetails")
    require(scan, "ServerBrowserText( ServerBrowserMatchType( server.serverInfo ).c_str(), 64 )", "idServerScan::GUIAdd")

    # The Create Server page offers the arenas and the presets by index.
    menu = read(ROOT / "src/framework/Session_menu.cpp")
    offered = re.findall(r'"(\w+)"', table(menu, "static const char *const RETAINED_CREATE_WEAPON_ARENAS[] = {"))
    if offered != ["none"] + [name for name, _, _ in arenas]:
        raise AssertionError("RETAINED_CREATE_WEAPON_ARENAS drifted from mpWeaponArenaTable")
    offered_presets = re.findall(r'\{ "([a-z0-9_-]+)",\s*"([^"]*)" \},',
                                 table(menu, "static const retainedCreatePreset_t RETAINED_CREATE_PRESETS[] = {"))
    if offered_presets != [(name, settings) for name, _, settings in presets]:
        raise AssertionError("RETAINED_CREATE_PRESETS drifted from mpPresetTable: the page would apply other settings than the game")
    generator = read(ROOT / "tools/ui/retained_create_server.py")
    arena_choice = re.search(r'"weaponarena": \(\((.*?)\), tuple\(range\((\d+)\)\)\),', generator, re.S)
    if arena_choice is None or re.findall(r'"(#str_\d+)"', arena_choice.group(1)) != ["#str_41200"] + [label for _, _, label in arenas] \
            or int(arena_choice.group(2)) != len(arenas) + 1:
        raise AssertionError("the Create Server weapon arena choice drifted from mpWeaponArenaTable")
    preset_choice = re.search(r'"preset": \(\((.*?)\), tuple\(range\((\d+)\)\)\),', generator, re.S)
    if preset_choice is None or re.findall(r'"(#str_\d+)"', preset_choice.group(1)) != ["#str_231977"] + [label for _, label, _ in presets] \
            or int(preset_choice.group(2)) != len(presets) + 1:
        raise AssertionError("the Create Server preset choice drifted from mpPresetTable")
    for verb, state, cvar, maximum in (("createWeaponArena", "weaponarena", "si_weaponArena", len(arenas)),
                                       ("createPreset", "preset", "si_preset", len(presets))):
        row = re.search(r'\{ "%s",\s*"%s",\s*"%s",\s*0, (\d+),\s*false \}' % (verb, state, cvar), menu)
        if row is None or int(row.group(1)) != maximum:
            raise AssertionError(f"RETAINED_CREATE_SETTINGS {verb} must run 0 to {maximum}")
        require(generator, f'("{state}", "{verb}", ', "create-server ADVANCED rows")
        if not re.search(r'\("%s", "%s", "#str_\d+", "choice", 0, %d, None\)' % (state, verb, maximum), generator):
            raise AssertionError(f"the Create Server {state} row must run 0 to {maximum}")


def validate_presets() -> None:
    _, arenas, presets = game_tables()
    source = read(MP / "mp/Presets.cpp")
    if presets[0][0] != "standard":
        raise AssertionError("row 0 of mpPresetTable must be the standard preset every other one starts from")
    standard = presets[0][2].split()
    composition = set(standard[0::2]) | {"si_gameType"}
    # Every composition switch is in the standard preset at its cvar default, so
    # applying it is the stock game and si_preset can vouch for all of them.
    switches = set(SWITCHES) - {"si_preset"} | {"si_instagib", "si_ruleset"}
    if set(standard[0::2]) != switches:
        raise AssertionError(f"the standard preset must set exactly the composition switches: {sorted(set(standard[0::2]) ^ switches)}")
    cvars = read(MP / "gamesys/SysCvar.cpp")
    for key, value in zip(standard[0::2], standard[1::2]):
        line = re.search(r"^idCVar %s\(.*$" % key, cvars, re.M).group(0)
        if not re.search(r'"%s",\s*"%s",' % (key, value), line):
            raise AssertionError(f"the standard preset's {key} {value} is not the cvar's default")
    limits = set(re.findall(r'"(si_\w+)"', table(source, "static const char *const mpPresetLimitKeys[] = {")))
    # Nothing a preset may set names a file, a password, a player or a command.
    for key in composition | limits:
        if not key.startswith("si_") or key in ("si_name", "si_motd", "si_map", "si_mapCycle", "si_pure", "si_usePass",
                                                 "si_maxPlayers", "si_serverURL", "si_allowVoting", "si_voteFlags"):
            raise AssertionError(f"{key} is not a setting a preset may change")
    types = re.findall(r'"([^"]+)"', table(read(MP / "mp/GameTypes.cpp"), "const char *si_gameTypeArgs[] = {"))
    names = set()
    for name, label, settings in presets:
        if name in names or not re.fullmatch(r"[a-z0-9_-]{1,32}", name):
            raise AssertionError(f"preset name {name!r} is not unique lower-case text")
        names.add(name)
        pairs = settings.split('"')[0].split()
        tokens = re.findall(r'"[^"]*"|\S+', settings)
        if len(tokens) % 2:
            raise AssertionError(f"preset {name}: a setting has no value")
        index = 0
        while index < len(tokens):
            key, value = tokens[index], tokens[index + 1].strip('"')
            # A two-word game type ("Team DM") would need quoting; none is used.
            if key not in composition | limits:
                raise AssertionError(f"preset {name}: {key} is not in the allow list")
            if key == "si_gameType":
                if value not in types or value == "singleplayer":
                    raise AssertionError(f"preset {name}: {value} is not a multiplayer game type")
            elif key == "si_weaponArena":
                if value != "none" and value not in [arena for arena, _, _ in arenas]:
                    raise AssertionError(f"preset {name}: {value} is not a weapon arena")
            elif not value.isdigit():
                raise AssertionError(f"preset {name}: {key} {value} is not a whole number")
            index += 2
        del pairs
    # si_preset is a statement about the settings: it is checked, and cleared, at the latch.
    game = read(MP / "MultiplayerGame.cpp")
    latch = body(game, "void idMultiplayerGame::LatchModifiers( void ) {")
    require(latch, "!rvMPPresets::Matches( preset.c_str(), gameLocal.serverInfo )", "idMultiplayerGame::LatchModifiers")
    require(latch, 'si_preset.SetString( "" );', "idMultiplayerGame::LatchModifiers")
    # A preset file is parsed, never executed.
    reader = body(source, "static bool MPPresetReadFile(")
    for forbidden in ("BufferCommandText", "BufferCommandArgs", '"exec"'):
        if forbidden in reader:
            raise AssertionError("a preset file must be parsed, never executed")
    require(reader, "MP_PRESET_FILE_MAX", "MPPresetReadFile")
    add = body(source, "static bool MPPresetAddSetting(")
    require(add, "rvMPPresets::SettingAllowed( key )", "MPPresetAddSetting")
    apply = body(source, "bool rvMPPresets::Apply(")
    require(apply, "gameLocal.mpGame.InvalidateCasualRulesImport();", "rvMPPresets::Apply (a casual session mirrors its limits back)")
    require(apply, "if ( !Settings( name, settings, reason ) ) {\n\t\treturn false;", "rvMPPresets::Apply")


def validate_latch_and_rules() -> None:
    game = read(MP / "MultiplayerGame.cpp")
    require(body(game, "void idMultiplayerGame::SetGameType( void ) {"), "LatchModifiers();", "idMultiplayerGame::SetGameType")
    latch = body(game, "void idMultiplayerGame::LatchModifiers( void ) {")
    for token in ("ModifiersWanted( modifiers );", "ResetQuadHog();", "rvMPModifiers::Tokens( modifiers, tokens );",
                  'gameLocal.serverInfo.Set( "si_modifiers", tokens.c_str() );'):
        require(latch, token, "idMultiplayerGame::LatchModifiers")
    change = body(game, "void idMultiplayerGame::CheckModifierChange( void ) {")
    require(change, "if ( wanted == modifiers ) {\n\t\treturn;", "idMultiplayerGame::CheckModifierChange")
    run = body(game, "void idMultiplayerGame::Run( void ) {")
    require(run, "CheckModifierChange();\n\tRunQuadHog();", "idMultiplayerGame::Run")

    # What each rule leaves off the map; every peer runs this at populate.
    inhibited = body(game, "bool idMultiplayerGame::PickupInhibited( const char *classname ) const {")
    for token in ("if ( ( weapon || ammo ) && HasFixedLoadout() ) {",
                  'HasModifier( MPMOD_VAMPIRIC ) && idStr::FindText( classname, "item_health" ) == 0',
                  "HasModifier( MPMOD_QUADHOG ) ||",
                  "!modifiers.powerups && !MPGameTypeHasAny( gameLocal.gameType, GTF_ARENA )"):
        require(inhibited, token, "idMultiplayerGame::PickupInhibited")
    for forbidden in ("serverInfo", "cvarSystem", ".GetBool(", ".GetInteger("):
        if forbidden in inhibited:
            raise AssertionError("PickupInhibited must read only the latch: peers populate the map from it")

    # Quad Hog
    death = body(game, "void idMultiplayerGame::PlayerDeath(")
    gate = require(death, "if ( QuadHogFragScores( killer, dead ) ) {", "idMultiplayerGame::PlayerDeath")
    require(death[gate:gate + 160], "AddPlayerScore( killer, ( killer == dead ) ? -1 : 1 );", "Quad Hog scoring gate")
    scores = body(game, "bool idMultiplayerGame::QuadHogFragScores(")
    require(scores, "killer->PowerUpActive( POWERUP_QUADDAMAGE ) || dead->PowerUpActive( POWERUP_QUADDAMAGE )", "QuadHogFragScores")
    require(scores, "killer == dead", "QuadHogFragScores (a suicide always costs)")
    hog = body(game, "void idMultiplayerGame::RunQuadHog( void ) {")
    for token in ("gameLocal.isClient || !HasModifier( MPMOD_QUADHOG )", 'CenterPrint( -1, "#str_231978", CPARM_CLIENT, holder );',
                  "MP_QUADHOG_LOOSE_MS", "SpawnQuadHogItem()"):
        require(hog, token, "idMultiplayerGame::RunQuadHog")
    spawn = body(game, "bool idMultiplayerGame::SpawnQuadHogItem( void ) {")
    for token in ('args.Set( "classname", "powerup_quad_damage" );', 'args.Set( "dropped", "1" );', 'args.Set( "time", "-1" );',
                  "gameLocal.NumSpawnSpots() <= 0"):
        require(spawn, token, "idMultiplayerGame::SpawnQuadHogItem")

    # Vampiric Damage
    vampiric = body(game, "void idMultiplayerGame::ApplyVampiricDamage(")
    for token in ("gameLocal.isClient || !HasModifier( MPMOD_VAMPIRIC )", "attacker == victim",
                  "gameLocal.IsTeamGame() && attacker->team == victim->team", "Min( damage, healthBefore )",
                  "MP_VAMPIRIC_HEALTH_FACTOR"):
        require(vampiric, token, "idMultiplayerGame::ApplyVampiricDamage")

    player = read(MP / "Player.cpp")
    damage = body(player, "void idPlayer::Damage(")
    instagib = require(damage, "const bool instagibHit", "idPlayer::Damage")
    self_scale = require(damage, "rvMPModifiers::SelfDamageScale()", "idPlayer::Damage")
    knock = require(damage, "// determine knockback", "idPlayer::Damage")
    if not instagib < self_scale < knock:
        raise AssertionError("si_selfDamage must scale the hit after it is computed and before it is applied")
    # A suicide is its own inflictor; scaling it would let si_selfDamage 0 block `kill`.
    require(damage[instagib:knock], "attacker == this && inflictor != this", "si_selfDamage rule")
    require(damage, "kick *= rvMPModifiers::KnockbackScale();", "idPlayer::Damage")
    credit = require(damage, "GetGameState()->PlayerDamage(", "idPlayer::Damage")
    drain = require(damage, "gameLocal.mpGame.ApplyVampiricDamage( static_cast< idPlayer * >( attacker ), this, damage, preDamageHealth );",
                    "idPlayer::Damage")
    applied = require(damage, "health -= damage;", "idPlayer::Damage")
    if not credit < drain < applied:
        raise AssertionError("Vampiric Damage must be paid from the health before the hit is applied")
    crash = body(player, "void idPlayer::CrashLand(")
    require(crash, "const float fallDamageScale = rvMPModifiers::FallDamageScale();", "idPlayer::CrashLand")
    require(crash, "if ( fallDamageScale <= 0.0f ) {\n\t\tnoDamage = true;", "idPlayer::CrashLand")
    for name in ("damage_fatalfall", "damage_hardfall", "damage_softfall"):
        require(crash, f'"{name}", fallDamageScale, 0 );', "idPlayer::CrashLand")
    powerups = body(player, "void idPlayer::UpdatePowerUps( void ) {")
    frenzy = require(powerups, "gameLocal.mpGame.HasModifier( MPMOD_FRENZY ) ) {", "idPlayer::UpdatePowerUps")
    for token in ("inventory.ammo[ ammoIndex ] < 0", "MP_FRENZY_REGEN_DIVISOR", "MP_FRENZY_REGEN_PULSE_MS"):
        require(powerups[frenzy:frenzy + 1200], token, "Weapons Frenzy ammo regeneration")
    spawn_to = body(player, "void idPlayer::SpawnToPoint(")
    require(spawn_to, "gameLocal.mpGame.FixedLoadoutWeapon() : NULL;", "idPlayer::SpawnToPoint")
    require(spawn_to, "if ( !fixedLoadout ) {\n\t\t\tmemset( inventory.clip, -1, sizeof( inventory.clip ) );", "full-arsenal grant under a weapon arena")

    item = body(read(MP / "Item.cpp"), "bool idItem::Pickup( idPlayer *player ) {")
    respawn = require(item, "rvMPModifiers::WeaponRespawnSeconds()", "idItem::Pickup")
    bots = require(item, "botManager.OnItemPickup(", "idItem::Pickup")
    if not respawn < bots:
        raise AssertionError("the respawn time must be decided before the bots and item timers hear of the pickup")
    require(item, "Min( respawn, Max( MP_FRENZY_RESPAWN_FLOOR, respawn * MP_FRENZY_RESPAWN_SCALE ) )", "idItem::Pickup")

    commands = read(MP / "gamesys/SysCmds.cpp")
    for name, function in (("modifier", "MP_Modifier_f"), ("weaponArena", "MP_WeaponArena_f"), ("preset", "MP_Preset_f"),
                           ("listPresets", "MP_ListPresets_f")):
        if not re.search(r'cmdSystem->AddCommand\( "%s",\s*%s,\s*CMD_FL_GAME,' % (name, function), commands):
            raise AssertionError(f"the {name} command must be registered")
    refused = body(read(MP / "mp/Presets.cpp"), "static bool MPRulesCommandRefused(")
    require(refused, "gameLocal.isClient", "MPRulesCommandRefused")
    require(refused, "RejectManagedLegacyMutation( action )", "MPRulesCommandRefused (a managed match owns its rules)")

    # The loading card resolves the same switches the game latches.
    local = read(MP / "Game_local.cpp")
    session = read(ROOT / "src/framework/Session.cpp")
    keys = '"si_instagib", "si_quadHog", "si_vampiric", "si_weaponsFrenzy", "si_weaponArena",\n'
    require(local, keys, "retainedLoadingServer query keys")
    require(session, keys, "idSessionLocal::PublishRetainedLoadingServer")
    require(local, "rvMPModifiers::Resolve( query, info->type, true, modifiers );", "retainedLoadingServer")


def validate_menu() -> None:
    menu = read(ROOT / "src/framework/Session_menu.cpp")
    generator = read(ROOT / "tools/ui/retained_create_server.py")
    document = read(ROOT / "content/baseoq4/pak0/guis/menu/createserver.rui")
    tabs = re.search(r"CREATE_TABS = \((.*?)\n\)\n", generator.replace("\r\n", "\n"), re.S).group(1)
    for verb, state, cvar, maximum, boolean, kind in (
            ("createQuadHog", "quadhog", "si_quadHog", 1, "true", "toggle"),
            ("createVampiric", "vampiric", "si_vampiric", 1, "true", "toggle"),
            ("createFrenzy", "frenzy", "si_weaponsFrenzy", 1, "true", "toggle"),
            ("createPowerups", "powerups", "si_powerups", 1, "true", "toggle"),
            ("createSelfDamage", "selfdamage", "si_selfDamage", 999, "false", "number"),
            ("createKnockback", "knockback", "si_knockback", 999, "false", "number"),
            ("createFallDamage", "falldamage", "si_fallDamage", 999, "false", "number"),
            ("createWeaponRespawn", "weaponrespawn", "si_weaponRespawn", 999, "false", "number")):
        if not re.search(r'\{ "%s",\s*"%s",\s*"%s",\s*0, %d,\s*%s \}' % (verb, state, cvar, maximum, boolean), menu):
            raise AssertionError(f"RETAINED_CREATE_SETTINGS must carry {verb} for {cvar}, 0 to {maximum}")
        if not re.search(r'\("%s", "%s", "#str_\d+", "%s", 0, %d, ' % (state, verb, kind, maximum), generator):
            raise AssertionError(f"the Create Server page must offer {state} as a {kind}, 0 to {maximum}")
        require(tabs, f'"create-{state}"', "CREATE_TABS")
        require(document, f'"{verb}"', "createserver.rui (regenerate with build_retained_screens.py)")
    for state in ("weaponarena", "preset"):
        require(tabs, f'"create-{state}"', "CREATE_TABS")
    # The retained runtime refuses a document that asks for a verb it does not
    # know, and the stock screen then stays in use: every row's verb is listed.
    runtime = read(ROOT / "src/ui/UserInterfaceRetained.cpp")
    known = read(ROOT / "tools/tests/native/UiRetainedScreensTest.cpp")
    for verb in ("createQuadHog", "createVampiric", "createFrenzy", "createWeaponArena", "createPowerups", "createSelfDamage",
                 "createKnockback", "createFallDamage", "createWeaponRespawn", "createPreset"):
        require(runtime, f'"{verb}"', "UserInterfaceRetained.cpp's application operations")
        require(known, f'"{verb}"', "UiRetainedScreensTest.cpp's session commands")
    # The page shows Custom once a setting the preset vouches for has changed,
    # as the game's rvMPPresets::Matches decides at the next map load.
    value = menu[menu.index("static int Session_RetainedCreatePreset( void ) {"):menu.index("static void Session_SetRetainedCreatePreset(")]
    require(value, "Session_RetainedCreatePresetVouchesFor( key )", "Session_RetainedCreatePreset")
    setter = menu[menu.index("static void Session_SetRetainedCreatePreset( int value ) {"):]
    require(setter[:900], 'cvarSystem->SetCVarString( "si_preset", RETAINED_CREATE_PRESETS[ value - 1 ].name );', "Session_SetRetainedCreatePreset")
    # The join details card has a line for a modified match's modifiers.
    join = read(ROOT / "tools/ui/retained_join_game.py")
    lines = re.search(r"^DETAIL_SETTINGS = (\d+)", join, re.M).group(1)
    if lines != "8" or f"RETAINED_JOIN_DETAIL_SETTINGS = {lines};" not in menu:
        raise AssertionError("the join details card must hold the seven stock settings and the modifiers line")


def validate_strings() -> None:
    modifiers, arenas, presets = game_tables()
    used = {label for _, _, _, label, _ in modifiers} | {title for _, _, _, _, title in modifiers}
    used |= {label for _, _, label in arenas} | {label for _, label, _ in presets}
    used |= {"#str_231968", "#str_231969", "#str_231970", "#str_231971", "#str_231972", "#str_231973", "#str_231974",
             "#str_231975", "#str_231976", "#str_231977", "#str_231978", "#str_231979", "#str_231986", "#str_231987"}
    english = dict(re.findall(r'"(#str_\d+)"\t"([^"]*)"', read(STRINGS / "english_openq4.lang")))
    for language in LANGUAGES:
        text = "\n".join(read(path) for path in sorted(STRINGS.glob(f"{language}_*.lang")))
        table_ = dict(re.findall(r'"(#str_\d+)"\t"([^"]*)"', text))
        own = dict(re.findall(r'"(#str_\d+)"\t"([^"]*)"', read(STRINGS / f"{language}_openq4.lang")))
        for ident in sorted(used):
            # Quake 4's own weapon names live in the stock tables, which not every language ships here.
            if ident not in table_ and (ident in english or language in ("english",)):
                raise AssertionError(f"{language}: {ident} is missing")
        for ident, count in FORMATS.items():
            value = own.get(ident)
            if value is None or value.count("%s") != count or value.count("%") != count:
                raise AssertionError(f"{language}_openq4.lang: {ident} must take exactly {count} %s")


# --------------------------------------------------------------------------
# The executable half: mp/Modifiers.cpp and mp/Presets.cpp as shipped, against
# doubles of the engine services they touch.
# --------------------------------------------------------------------------

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define BIT( n ) ( 1 << ( n ) )
#define __LINESTR__ "0"
static std::string Lower(std::string s){for(char &c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
class idStr {
public:
    std::string value;
    idStr() = default;
    idStr(const char *s): value(s ? s : "") {}
    const char *c_str() const { return value.c_str(); }
    int Length() const { return static_cast<int>(value.size()); }
    void Clear() { value.clear(); }
    void ToLower() { value = Lower(value); }
    void CapLength(int n) { if(n < Length()) value.resize(n); }
    void Fill(char c,int n) { value.assign(n, c); }
    idStr Mid(int start,int len) const { idStr out; out.value = value.substr(start, len); return out; }
    int Find(const char *text) const { const auto at = value.find(text); return at == std::string::npos ? -1 : static_cast<int>(at); }
    void StripFileExtension() { const auto at = value.rfind('.'); if(at != std::string::npos) value.resize(at); }
    int Cmp(const char *s) const { return value.compare(s); }
    idStr &operator+=(const char *s) { value += s; return *this; }
    idStr &operator+=(const idStr &s) { value += s.value; return *this; }
    bool operator==(const idStr &s) const { return value == s.value; }
    char &operator[](int n) { return value[n]; }
    char operator[](int n) const { return value[n]; }
    operator const char *() const { return c_str(); }
    static int Length(const char *s) { return static_cast<int>(std::strlen(s)); }
    static int Icmp(const char *a,const char *b) { return Lower(a).compare(Lower(b)); }
    static int Icmpn(const char *a,const char *b,int n) { return Lower(std::string(a).substr(0,n)).compare(Lower(std::string(b).substr(0,n))); }
};
const char *va(const char *format,...) {
    static char buffers[16][4096]; static unsigned which = 0;
    char *out = buffers[which++ % 16]; va_list ap; va_start(ap, format);
    std::vsnprintf(out, 4096, format, ap); va_end(ap); return out;
}
template<class T> T Min(T a,T b){return std::min(a,b);}
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath { static int ClampInt(int lo,int hi,int v){return std::clamp(v,lo,hi);} };
class idStrList {
public:
    std::vector<idStr> data;
    int Num() const { return static_cast<int>(data.size()); }
    void Clear() { data.clear(); }
    void Append(const idStr &s) { data.push_back(s); }
    int FindIndex(const idStr &s) const { for(int i=0;i<Num();++i) if(data[i]==s) return i; return -1; }
    const idStr &operator[](int i) const { return data[i]; }
};
struct idKeyValue {
    idStr key, value;
    const idStr &GetKey() const { return key; }
    const idStr &GetValue() const { return value; }
};
class idDict {
public:
    std::vector<idKeyValue> data;
    void Clear() { data.clear(); }
    const idKeyValue *FindKey(const char *key) const { for(const auto &p:data) if(!idStr::Icmp(p.key.c_str(),key)) return &p; return nullptr; }
    void Set(const char *key,const char *value) {
        for(auto &p:data) if(!idStr::Icmp(p.key.c_str(),key)) { p.value = value; return; }
        idKeyValue kv; kv.key = key; kv.value = value; data.push_back(kv);
    }
    const char *GetString(const char *key,const char *fallback="") const { auto p = FindKey(key); return p ? p->value.c_str() : fallback; }
    bool GetBool(const char *key,const char *fallback="0") const { return std::atoi(GetString(key, fallback)) != 0; }
    int GetInt(const char *key,const char *fallback="0") const { return std::atoi(GetString(key, fallback)); }
    int GetNumKeyVals() const { return static_cast<int>(data.size()); }
    const idKeyValue *GetKeyVal(int i) const { return &data[i]; }
};
// The engine's tokenizer as these sources use it: words and quoted strings.
class idCmdArgs {
public:
    std::vector<std::string> args;
    void TokenizeString(const char *text,bool) {
        args.clear();
        const char *at = text;
        while(*at) {
            while(*at == ' ' || *at == '\t') ++at;
            if(!*at) break;
            std::string token;
            if(*at == '"') { ++at; while(*at && *at != '"') token += *at++; if(*at == '"') ++at; }
            else { while(*at && *at != ' ' && *at != '\t') token += *at++; }
            args.push_back(token);
        }
    }
    int Argc() const { return static_cast<int>(args.size()); }
    const char *Argv(int i) const { return i >= 0 && i < Argc() ? args[i].c_str() : ""; }
};
static std::map<std::string,std::string> strings;
struct Common {
    std::string printed;
    const char *GetLocalizedString(const char *id) { auto at = strings.find(id); return at == strings.end() ? id : at->second.c_str(); }
    void Printf(const char *format,...) { char out[4096]; va_list ap; va_start(ap, format); std::vsnprintf(out, sizeof(out), format, ap); va_end(ap); printed += out; }
} commonLocal, *common = &commonLocal;
struct CvarSystem {
    std::map<std::string,std::string> values;
    void SetCVarString(const char *name,const char *value) { values[Lower(name)] = value; }
    void SetCVarBool(const char *name,bool value) { values[Lower(name)] = value ? "1" : "0"; }
    const char *GetCVarString(const char *name) { return values[Lower(name)].c_str(); }
    bool GetCVarBool(const char *name) { return std::atoi(GetCVarString(name)) != 0; }
} cvarLocal, *cvarSystem = &cvarLocal;
enum { CMD_EXEC_NOW, CMD_EXEC_INSERT, CMD_EXEC_APPEND };
struct CmdSystem { std::vector<std::string> buffered; void BufferCommandText(int,const char *text) { buffered.push_back(text); } } cmdLocal, *cmdSystem = &cmdLocal;
struct idFile {
    std::string data; size_t at = 0;
    int Length() const { return static_cast<int>(data.size()); }
    int Read(void *buffer,int len) { const int n = std::min<int>(len, static_cast<int>(data.size() - at)); std::memcpy(buffer, data.data() + at, n); at += n; return n; }
};
struct idFileList {
    std::vector<std::string> names;
    int GetNumFiles() const { return static_cast<int>(names.size()); }
    const char *GetFile(int i) const { return names[i].c_str(); }
};
struct FileSystem {
    std::map<std::string,std::string> files;
    idFile *OpenFileRead(const char *path) { auto at = files.find(path); if(at == files.end()) return nullptr; idFile *f = new idFile; f->data = at->second; return f; }
    void CloseFile(idFile *f) { delete f; }
    idFileList *ListFiles(const char *directory,const char *extension,bool) {
        idFileList *list = new idFileList; const std::string prefix = std::string(directory) + "/";
        for(const auto &p:files) if(p.first.compare(0, prefix.size(), prefix) == 0 && p.first.size() > std::strlen(extension) &&
            p.first.compare(p.first.size() - std::strlen(extension), std::string::npos, extension) == 0) list->names.push_back(p.first.substr(prefix.size()));
        return list;
    }
    void FreeFileList(idFileList *list) { delete list; }
} fileLocal, *fileSystem = &fileLocal;
enum { GAMESTATE_UNINITIALIZED, GAMESTATE_NOMAP, GAMESTATE_STARTUP, GAMESTATE_ACTIVE, GAMESTATE_SHUTDOWN };
'''

GAME_SUPPORT = r'''
const int GTF_FULLARSENAL = BIT( 21 );
struct mpGameTypeInfo_t { int type; const char *name; int flags; bool selectable; };
static const mpGameTypeInfo_t gameTypes[] = {
@GAMETYPES@
};
const mpGameTypeInfo_t *MPGameTypeByName(const char *name) { for(const auto &row:gameTypes) if(!idStr::Icmp(row.name,name)) return &row; return nullptr; }
bool MPGameTypeIsSelectable(int type) { for(const auto &row:gameTypes) if(row.type == type) return row.selectable; return false; }
bool MPGameTypeHasAny(int type,int mask) { for(const auto &row:gameTypes) if(row.type == type) return (row.flags & mask) != 0; return false; }
struct MpGame {
    int latched = 0; bool managed = false; int restarts = 0, imports = 0;
    void InvalidateCasualRulesImport() { ++imports; }
    bool HasModifier(int modifier) const { return (latched & BIT(modifier)) != 0; }
    bool RejectManagedLegacyMutation(const char *) { return managed; }
    void QueueRulesRestart() { ++restarts; }
    bool AnyMapPlaysGameType(const char *) { return true; }
};
struct GameLocal {
    bool isMultiplayer = true, isClient = false, isServer = true;
    int gameType = GAME_DM, state = GAMESTATE_ACTIVE;
    idDict serverInfo;
    MpGame mpGame;
    int GameState() const { return state; }
} gameLocal;
'''

MAIN = r'''
static mpModifierState_t Resolved(const char *settings,int gameType,bool allowed=true) {
    idDict info; idCmdArgs args; args.TokenizeString(settings, false);
    for(int i = 0; i + 1 < args.Argc(); i += 2) info.Set(args.Argv(i), args.Argv(i + 1));
    mpModifierState_t state; rvMPModifiers::Resolve(info, gameType, allowed, state); return state;
}
static std::string TokensOf(const mpModifierState_t &state) { idStr out; rvMPModifiers::Tokens(state, out); return out.c_str(); }
static std::string TitleOf(const mpModifierState_t &state,const char *type) { idStr out; rvMPModifiers::Title(state, type, out); return out.c_str(); }
static std::string LabelsOf(const mpModifierState_t &state) { idStr out; rvMPModifiers::Labels(state, out); return out.c_str(); }
static void CvarsToServerInfo() { gameLocal.serverInfo.Clear(); for(const auto &p:cvarLocal.values) gameLocal.serverInfo.Set(p.first.c_str(), p.second.c_str()); }
static bool Refused(const char *name,const char *text,const char *why) {
    fileLocal.files[std::string("presets/") + name + ".cfg"] = text;
    idStr reason; const bool ok = rvMPPresets::Validate(name, reason);
    if(ok || !std::strstr(reason.c_str(), why)) std::printf("preset %s: ok=%d reason='%s', wanted '%s'\n", name, ok, reason.c_str(), why);
    return !ok && std::strstr(reason.c_str(), why) != nullptr;
}
int main() {
    // ---- what the switches resolve to
    assert(TokensOf(Resolved("", GAME_DM)).empty());
    assert(Resolved("", GAME_DM).powerups && Resolved("", GAME_DM).weaponArena == -1);
    assert(TokensOf(Resolved("si_instagib 1", GAME_CTF)) == "instagib");
    // Instagib owns the loadout and the damage rule: it switches the others off.
    assert(TokensOf(Resolved("si_instagib 1 si_quadHog 1 si_vampiric 1 si_weaponsFrenzy 1 si_weaponArena railgun si_powerups 0", GAME_DM)) == "instagib");
    // Quad Hog is a Deathmatch rule.
    assert(TokensOf(Resolved("si_quadHog 1", GAME_DM)) == "quadhog");
    for(int type : { (int)GAME_TDM, (int)GAME_CTF, (int)GAME_DUEL, (int)GAME_TOURNEY, (int)GAME_CA, (int)GAME_FREEZETAG, (int)GAME_DEADZONE })
        assert(TokensOf(Resolved("si_quadHog 1", type)).empty());
    assert(TokensOf(Resolved("si_vampiric 1", GAME_TDM)) == "vampiric");
    // Weapons Frenzy needs pickups and spendable ammo.
    assert(TokensOf(Resolved("si_weaponsFrenzy 1", GAME_CTF)) == "frenzy");
    assert(TokensOf(Resolved("si_weaponsFrenzy 1 si_weaponArena rocketlauncher", GAME_DM)) == "arena:rocketlauncher");
    assert(TokensOf(Resolved("si_weaponsFrenzy 1", GAME_CA)).empty());
    assert(TokensOf(Resolved("si_weaponArena grenadelauncher", GAME_CA)) == "arena:grenadelauncher");
    assert(TokensOf(Resolved("si_quadHog 1 si_vampiric 1 si_weaponsFrenzy 1 si_powerups 0", GAME_DM)) == "quadhog vampiric frenzy nopowerups");
    // An unknown arena is no arena on every peer; "none" is the default.
    assert(Resolved("si_weaponArena bfg", GAME_DM).weaponArena == -1 && Resolved("si_weaponArena none", GAME_DM).weaponArena == -1);
    assert(Resolved("si_weaponArena RailGun", GAME_DM).weaponArena == rvMPModifiers::WeaponArenaByName("railgun"));
    // The Arena campaign, and single player, take none of it.
    assert(TokensOf(Resolved("si_instagib 1 si_weaponArena railgun si_powerups 0", GAME_DM, false)).empty());
    // A server info from before the switch existed spawns powerups.
    assert(Resolved("si_vampiric 1", GAME_DM).powerups);

    // ---- the published summary reads back as the same latch
    for(int mask = 0; mask < BIT(MPMOD_COUNT); ++mask) for(int arena = -1; arena < rvMPModifiers::WeaponArenaCount(); ++arena) for(int powerups = 0; powerups < 2; ++powerups) {
        mpModifierState_t state, back; state.mask = mask; state.weaponArena = arena; state.powerups = powerups != 0;
        rvMPModifiers::FromTokens(TokensOf(state).c_str(), back);
        assert(back == state);
    }
    mpModifierState_t hostile;
    rvMPModifiers::FromTokens("instagib %s%n arena:../../x arena: nopowerups nopowerups unknown", hostile);
    assert(hostile.mask == BIT(MPMOD_INSTAGIB) && hostile.weaponArena == -1 && !hostile.powerups);
    rvMPModifiers::FromTokens(nullptr, hostile);
    assert(hostile.mask == 0 && hostile.powerups);

    // ---- what the match is called
    assert(TitleOf(Resolved("", GAME_DM), "Deathmatch") == "Deathmatch");
    assert(TitleOf(Resolved("si_instagib 1", GAME_DM), "Deathmatch") == "Instagib Deathmatch");
    assert(TitleOf(Resolved("si_quadHog 1", GAME_DM), "Deathmatch") == "Quad Hog Deathmatch");
    assert(TitleOf(Resolved("si_vampiric 1 si_weaponsFrenzy 1", GAME_TDM), "Team DM") == "Frenzy Vampiric Team DM");
    assert(TitleOf(Resolved("si_weaponArena rocketlauncher", GAME_DM), "Deathmatch") == "Deathmatch (Rocket Launcher)");
    assert(TitleOf(Resolved("si_vampiric 1 si_weaponArena railgun", GAME_DM), "Deathmatch") == "Vampiric Deathmatch (Railgun)");
    assert(LabelsOf(Resolved("", GAME_DM)).empty());
    assert(LabelsOf(Resolved("si_quadHog 1 si_vampiric 1 si_weaponArena nailgun si_powerups 0", GAME_DM)) == "Quad Hog, Vampiric Damage, Nailgun only, No Powerups");

    // ---- the switches as an operator or a vote sets them
    idStr reason;
    assert(!rvMPModifiers::ValidateModifier("bfg", 1, reason) && reason.Length());
    assert(!rvMPModifiers::ValidateModifier("vampiric", 2, reason) && !rvMPModifiers::ValidateModifier(nullptr, 1, reason));
    gameLocal.gameType = GAME_CTF;
    assert(!rvMPModifiers::ValidateModifier("quadhog", 1, reason) && rvMPModifiers::ValidateModifier("quadhog", 0, reason));
    assert(!rvMPModifiers::SetModifier("quadhog", 1, reason) && !cvarSystem->GetCVarBool("si_quadHog"));
    gameLocal.gameType = GAME_DM;
    assert(rvMPModifiers::SetModifier("QuadHog", 1, reason) && cvarSystem->GetCVarBool("si_quadHog"));
    assert(rvMPModifiers::SetModifier("quadhog", 0, reason) && !cvarSystem->GetCVarBool("si_quadHog"));
    assert(!rvMPModifiers::SetWeaponArena("bfg", reason) && rvMPModifiers::SetWeaponArena("NAILGUN", reason));
    assert(std::string(cvarSystem->GetCVarString("si_weaponArena")) == "nailgun");
    assert(rvMPModifiers::SetWeaponArena("off", reason) && std::string(cvarSystem->GetCVarString("si_weaponArena")) == "none");
    idStrList names; rvMPModifiers::ModifierNames(names); assert(names.Num() == MPMOD_COUNT);
    rvMPModifiers::WeaponArenaNames(names); assert(names.Num() == rvMPModifiers::WeaponArenaCount() + 1 && names[0] == idStr("none"));

    // ---- the scales, clamped the way the cvars are
    gameLocal.serverInfo.Clear();
    assert(rvMPModifiers::SelfDamageScale() == 1.0f && rvMPModifiers::KnockbackScale() == 1.0f && rvMPModifiers::FallDamageScale() == 1.0f);
    assert(rvMPModifiers::WeaponRespawnSeconds() == 0.0f);
    gameLocal.serverInfo.Set("si_selfDamage", "0"); gameLocal.serverInfo.Set("si_knockback", "150");
    gameLocal.serverInfo.Set("si_fallDamage", "-20"); gameLocal.serverInfo.Set("si_weaponRespawn", "5000");
    assert(rvMPModifiers::SelfDamageScale() == 0.0f && rvMPModifiers::KnockbackScale() == 1.5f && rvMPModifiers::FallDamageScale() == 0.0f);
    assert(rvMPModifiers::WeaponRespawnSeconds() == 999.0f);
    gameLocal.isMultiplayer = false;
    assert(rvMPModifiers::SelfDamageScale() == 1.0f && rvMPModifiers::WeaponRespawnSeconds() == 0.0f);
    gameLocal.isMultiplayer = true;

    // ---- the built-in presets
    for(int i = 0; i < rvMPPresets::BuiltInCount(); ++i) {
        const mpPresetInfo_t *preset = rvMPPresets::BuiltIn(i);
        idDict settings;
        assert(rvMPPresets::Settings(preset->name, settings, reason));
        // every preset carries the whole composition, so two never mix
        assert(settings.FindKey("si_instagib") && settings.FindKey("si_weaponArena") && settings.FindKey("si_ruleset") && settings.FindKey("si_powerups"));
        cvarLocal.values.clear();
        cvarSystem->SetCVarString("si_gameType", "Team DM"); cvarSystem->SetCVarString("si_instagib", "1"); cvarSystem->SetCVarString("si_weaponArena", "dmg");
        cvarSystem->SetCVarString("si_fragLimit", "25");
        assert(rvMPPresets::Apply(preset->name, reason));
        assert(std::string(cvarSystem->GetCVarString("si_preset")) == preset->name);
        CvarsToServerInfo();
        assert(rvMPPresets::Matches(preset->name, gameLocal.serverInfo));
        // a limit changed by a vote does not make it another preset
        gameLocal.serverInfo.Set("si_fragLimit", "99"); gameLocal.serverInfo.Set("si_captureLimit", "3");
        assert(rvMPPresets::Matches(preset->name, gameLocal.serverInfo));
        // a composition setting changed by hand does
        gameLocal.serverInfo.Set("si_vampiric", std::atoi(gameLocal.serverInfo.GetString("si_vampiric")) ? "0" : "1");
        assert(!rvMPPresets::Matches(preset->name, gameLocal.serverInfo));
    }
    cvarLocal.values.clear();
    cvarSystem->SetCVarString("si_gameType", "CTF"); cvarSystem->SetCVarString("si_instagib", "1");
    assert(rvMPPresets::Apply("standard", reason) && !cvarSystem->GetCVarBool("si_instagib"));
    assert(std::string(cvarSystem->GetCVarString("si_gameType")) == "CTF");		// a preset that names no game type keeps it
    assert(rvMPPresets::Apply("QuadHog", reason) && std::string(cvarSystem->GetCVarString("si_gameType")) == "DM" && cvarSystem->GetCVarBool("si_quadHog"));
    assert(std::string(cvarSystem->GetCVarString("si_preset")) == "quadhog");
    assert(rvMPPresets::Apply("rocketarena", reason) && !cvarSystem->GetCVarBool("si_quadHog"));	// the one before does not linger
    assert(std::string(cvarSystem->GetCVarString("si_weaponArena")) == "rocketlauncher" && std::string(cvarSystem->GetCVarString("si_selfDamage")) == "0");
    CvarsToServerInfo();
    assert(TokensOf(Resolved("si_weaponArena rocketlauncher si_powerups 0", GAME_DM)) == "arena:rocketlauncher nopowerups");

    // ---- a server's own presets: parsed, never executed, and only the allow list
    const std::map<std::string,std::string> before = cvarLocal.values;
    assert(!rvMPPresets::Apply("nosuch", reason) && cvarLocal.values == before);
    for(const char *name : { "", "../server", "Bad", "a b", "x;quit", "thisnameisfartoolongtobeapresetnameatall" }) assert(!rvMPPresets::ValidName(name) && !rvMPPresets::Validate(name, reason));
    fileLocal.files["presets/friday.cfg"] =
        "// Friday night\r\nseta si_gameType \"Team DM\"\r\nset si_vampiric 1 // heal by hurting\r\n\r\nsi_weaponArena LightningGun\nsi_fragLimit 60\n\tsi_teamDamage 1\n";
    idDict friday;
    assert(rvMPPresets::Settings("friday", friday, reason));
    assert(std::string(friday.GetString("si_gameType")) == "Team DM" && friday.GetBool("si_vampiric") && friday.GetInt("si_fragLimit") == 60);
    assert(std::string(friday.GetString("si_weaponArena")) == "lightninggun" && !friday.GetBool("si_instagib") && friday.GetBool("si_teamDamage"));
    assert(rvMPPresets::Apply("friday", reason) && std::string(cvarSystem->GetCVarString("si_preset")) == "friday");
    CvarsToServerInfo();
    assert(rvMPPresets::Matches("friday", gameLocal.serverInfo));
    assert(Refused("rcon", "net_serverRemoteConsolePassword 1\n", "not a setting a preset may change"));
    assert(Refused("pass", "si_vampiric 1\ng_password 1234\n", "not a setting a preset may change"));
    assert(Refused("name", "si_name 1\n", "not a setting a preset may change"));
    assert(Refused("map", "si_map mp/q4dm1\n", "not a setting a preset may change"));
    assert(Refused("exec", "exec server.cfg\n", "not a setting a preset may change"));
    assert(Refused("cmd", "si_instagib 1; quit\n", "expected a setting and its value"));
    assert(Refused("type", "si_gameType Overload\n", "is not a game type"));
    assert(Refused("arena", "si_weaponArena bfg\n", "unknown weapon arena"));
    assert(Refused("text", "si_fragLimit lots\n", "is a whole number"));
    assert(Refused("neg", "si_fragLimit -1\n", "is a whole number"));
    assert(Refused("lone", "si_instagib\n", "expected a setting and its value"));
    assert(Refused("crlf", "// one\r\nsi_vampiric 1\r\ng_password x\r\n", "presets/crlf.cfg line 3: "));	// CR LF is one line break
    assert(Refused("lf", "// one\nsi_vampiric 1\ng_password x\n", "presets/lf.cfg line 3: "));
    assert(Refused("empty", "", "empty or longer"));
    assert(Refused("huge", std::string(9000, ' ').c_str(), "empty or longer"));
    assert(cvarLocal.values.at("si_preset") == "friday");		// a refused preset changed nothing
    // a file cannot replace a built-in preset, and bad names are not listed
    fileLocal.files["presets/instagib.cfg"] = "si_vampiric 1\n";
    fileLocal.files["presets/Loud.cfg"] = "si_vampiric 1\n";
    fileLocal.files["presets/notes.txt"] = "x";
    idDict builtIn;
    assert(rvMPPresets::Settings("instagib", builtIn, reason) && builtIn.GetBool("si_instagib") && !builtIn.GetBool("si_vampiric"));
    rvMPPresets::Names(names);
    assert(names.FindIndex("friday") >= rvMPPresets::BuiltInCount() && names.FindIndex("loud") >= 0 && names.FindIndex("notes") < 0);
    int instagibs = 0; for(int i = 0; i < names.Num(); ++i) instagibs += names[i] == idStr("instagib"); assert(instagibs == 1);
    assert(rvMPPresets::SettingIsComposition("si_gameType") && rvMPPresets::SettingIsComposition("si_knockback") && !rvMPPresets::SettingIsComposition("si_fragLimit"));
    assert(rvMPPresets::SettingAllowed("si_fragLimit") && !rvMPPresets::SettingAllowed("si_preset") && !rvMPPresets::SettingAllowed("si_modifiers"));

    // ---- the console commands: server only, never inside a managed match
    idCmdArgs command;
    cvarLocal.values.clear(); cmdLocal.buffered.clear();
    gameLocal.isClient = true; gameLocal.isServer = false;
    command.TokenizeString("modifier vampiric 1", false); MP_Modifier_f(command);
    command.TokenizeString("preset instagib", false); MP_Preset_f(command);
    assert(!cvarSystem->GetCVarBool("si_vampiric") && !cvarSystem->GetCVarBool("si_instagib") && cmdLocal.buffered.empty());
    gameLocal.isClient = false; gameLocal.isServer = true; gameLocal.mpGame.managed = true;
    command.TokenizeString("weaponArena railgun", false); MP_WeaponArena_f(command);
    command.TokenizeString("preset instagib", false); MP_Preset_f(command);
    assert(std::string(cvarSystem->GetCVarString("si_weaponArena")).empty() && !cvarSystem->GetCVarBool("si_instagib") && gameLocal.mpGame.restarts == 0);
    gameLocal.mpGame.managed = false;
    command.TokenizeString("modifier vampiric 1", false); MP_Modifier_f(command);
    assert(cvarSystem->GetCVarBool("si_vampiric") && cmdLocal.buffered.size() == 1 && cmdLocal.buffered[0].compare(0, 8, "rescanSI") == 0);
    command.TokenizeString("preset instactf", false); MP_Preset_f(command);
    assert(cvarSystem->GetCVarBool("si_instagib") && !cvarSystem->GetCVarBool("si_vampiric") && gameLocal.mpGame.restarts == 1);
    assert(std::string(cvarSystem->GetCVarString("si_gameType")) == "CTF" && std::string(cvarSystem->GetCVarString("si_captureLimit")) == "8");
    assert(gameLocal.mpGame.imports > 0);		// the preset's limits are imported at the restart, not mirrored over
    command.TokenizeString("preset nosuch", false); MP_Preset_f(command);
    assert(gameLocal.mpGame.restarts == 1 && std::string(cvarSystem->GetCVarString("si_preset")) == "instactf");
    // before a map is running the settings are written and nothing restarts
    gameLocal.state = GAMESTATE_NOMAP; cmdLocal.buffered.clear();
    command.TokenizeString("preset frenzy", false); MP_Preset_f(command);
    command.TokenizeString("modifier quadhog 1", false); MP_Modifier_f(command);
    assert(cvarSystem->GetCVarBool("si_weaponsFrenzy") && cvarSystem->GetCVarBool("si_quadHog") && gameLocal.mpGame.restarts == 1 && cmdLocal.buffered.empty());
    std::puts("mp_modifiers_contract: executable checks passed");
    return 0;
}
'''


def strip_includes(source: str) -> str:
    return "\n".join(line for line in source.splitlines() if not line.startswith("#include") and not line.startswith("#pragma hdrstop"))


def run_executable() -> None:
    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    types = read(MP / "mp/GameTypes.cpp")
    rows = []
    listing = types[types.index("mpGameTypeInfoTable[] = {"):types.index("};", types.index("mpGameTypeInfoTable[] = {"))]
    for ident, name, flags, public in re.findall(
            r'\{\s*(GAME_\w+),\s*"([^"]+)",[^{}]*?\n\s*([A-Z_| ]+?),\s*(?:\n\s*)?MP_GAMESTATE_\w+,\s*(true|false)\s*\}', listing):
        arsenal = "GTF_FULLARSENAL" if "GTF_FULLARSENAL" in flags else "0"
        rows.append(f'    {{ {ident}, "{name}", {arsenal}, {public} }},')
    if len(rows) < 13:
        raise AssertionError("could not read the game type table")
    english = {}
    for path in (STRINGS / "english_openq4.lang", STRINGS / "english_guis.lang"):
        english.update(re.findall(r'"(#str_\d+)"\t"([^"]*)"', read(path)))
    wanted = sorted(set(re.findall(r'"(#str_\d+)"', read(MP / "mp/Modifiers.cpp") + read(MP / "mp/Presets.cpp"))))
    fill = "static void FillStrings() {\n" + "".join(
        '    strings["%s"] = "%s";\n' % (ident, english[ident].replace("\\", "\\\\")) for ident in wanted) + "}\n"
    program = "\n".join((
        SUPPORT,
        strip_includes(read(MP / "mp/GameTypeIds.h")),
        GAME_SUPPORT.replace("@GAMETYPES@", "\n".join(rows)),
        strip_includes(read(MP / "mp/Modifiers.h")),
        strip_includes(read(MP / "mp/Presets.h")),
        strip_includes(read(MP / "mp/Modifiers.cpp")),
        strip_includes(read(MP / "mp/Presets.cpp")),
        fill,
        MAIN.replace("int main() {", "int main() {\n    FillStrings();", 1),
    ))
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-modifiers-", dir=temporary) as directory:
        source, binary = Path(directory) / "modifiers.cpp", Path(directory) / "modifiers.exe"
        source.write_text(program, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-const-variable",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


def main() -> int:
    validate_cvars()
    validate_tables()
    validate_presets()
    validate_latch_and_rules()
    validate_menu()
    validate_strings()
    run_executable()
    print("mp_modifiers_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
