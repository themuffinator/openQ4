#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Pin multiplayer buying's two rule sets, the Awakening's catalog and the buy card.

A buying server sells by Quake 4's rules (retail's buy menu, priced by the
content) or The Awakening's (the expansion's prices, weapon mods for the weapon
in hand, the Dark Matter Gun, FC Armor Regen and its cash awards, as far as the
base content defines them). Create Server, the vote and the admin page offer
one Buying choice over si_isBuyingEnabled and si_buyingRules. The retained buy
card covers the stock buy menu and buys through the game's catalog. See
docs/user/multiplayer.md and docs/dev/ui/multiplayer-menus.md.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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


def document(relative: str) -> dict:
    """A retained document: JSON after its generated-file comment lines."""
    return json.loads("\n".join(line for line in read(relative).splitlines() if not line.startswith("//")))


def catalog(buying: str) -> list:
    body = buying[buying.index("const buyCatalogItem_t buyCatalog[] = {"):]
    body = body[:body.index("};")]
    rows = re.findall(r'\{\s*"(\w+)",\s*(\d+),\s*(BUY_CATEGORY_\w+),\s*(true|false),\s*(\d+),\s*"(#str_\d+)",\s*(?:"(#str_\d+)"|NULL)\s*\}', body)
    return [{"item": item, "impulse": int(impulse), "category": category, "quake4": quake4 == "true", "awakening": int(price),
             "name": name, "detail": detail or None} for item, impulse, category, quake4, price, name, detail in rows]


# The expansion's own prices, from its buy menu's labels (q4xbase guis/buymenu.gui).
AWAKENING_PRICES = {
    "weapon_shotgun": 500, "weapon_hyperblaster": 700, "weapon_nailgun": 900, "weapon_grenadelauncher": 1000,
    "weapon_lightninggun": 1500, "weapon_rocketlauncher": 2000, "weapon_railgun": 3000, "weapon_dmg": 5000,
    "wpmod_hyperblaster_bounce1": 8000, "wpmod_nailgun_power": 5000, "wpmod_lightninggun_chain": 6000,
    "wpmod_rocketlauncher_burst": 12000,
    "item_armor_small": 300, "item_armor_large": 700, "ammorefill": 500,
    "ammo_regen": 10000, "health_regen": 12500, "damage_boost": 15000, "fc_armor_regen": 3000,
}
# Retail's buy menu (pak019 guis/buymenu.gui and ItemCostConstants).
QUAKE4_ITEMS = {"weapon_shotgun", "weapon_hyperblaster", "weapon_grenadelauncher", "weapon_nailgun", "weapon_rocketlauncher",
                "weapon_railgun", "weapon_lightninggun", "weapon_napalmgun", "item_armor_small", "item_armor_large", "ammorefill",
                "ammo_regen", "health_regen", "damage_boost"}
# Retail's buy impulses keep their numbers.
RETAIL_IMPULSES = {"weapon_shotgun": 100, "weapon_hyperblaster": 102, "weapon_grenadelauncher": 103, "weapon_nailgun": 104,
                   "weapon_rocketlauncher": 105, "weapon_railgun": 106, "weapon_lightninggun": 107, "weapon_napalmgun": 109,
                   "item_armor_small": 118, "item_armor_large": 119, "ammorefill": 120, "ammo_regen": 123, "health_regen": 124,
                   "damage_boost": 125}


def check_catalog() -> list:
    buying = read("src/mpgame/mp/Buying.cpp")
    items = catalog(buying)
    assert len(items) == 20, len(items)
    names = [row["item"] for row in items]
    assert len(set(names)) == len(names), names
    impulses = [row["impulse"] for row in items]
    assert len(set(impulses)) == len(impulses), impulses
    assert all(100 <= impulse <= 127 for impulse in impulses), impulses
    for item, impulse in RETAIL_IMPULSES.items():
        assert next(row for row in items if row["item"] == item)["impulse"] == impulse, item
    assert {row["item"] for row in items if row["quake4"]} == QUAKE4_ITEMS
    assert {row["item"]: row["awakening"] for row in items if row["awakening"] > 0} == AWAKENING_PRICES
    # Mods the multiplayer weapons already outdo, and the expansion's own
    # weapons, stay out of the catalog.
    for absent in ("wpmod_shotgun_ammo", "wpmod_railgun_penetrate", "wpmod_nailgun_rof", "wpmod_grenade_concussion_blast",
                   "weapon_spikegun", "weapon_goobgun", "weapon_freezegun", "wpmod_spikegun_scope", "wpmod_goobgun_flamethrower",
                   "weapon_machinegun"):
        assert absent not in names, absent
    order = ["BUY_CATEGORY_WEAPONS", "BUY_CATEGORY_MODS", "BUY_CATEGORY_GEAR", "BUY_CATEGORY_TEAM"]
    assert [order.index(row["category"]) for row in items] == sorted(order.index(row["category"]) for row in items)
    assert all((row["detail"] is None) == (row["category"] == "BUY_CATEGORY_WEAPONS") for row in items)
    # The Awakening's cash awards over the content's constants.
    awards = dict((key, int(value)) for key, value in re.findall(r'\{\s*"(\w+)",\s*(-?\d+)\s*\}', buying[buying.index("awakeningBalanceConstants[]"):]))
    assert awards == {"teamCashAward_flagCapture": 1000, "teamCashAward_gameModeWin": 3000, "teamCashAward_gameModeTie": 2000,
                      "teamCashAward_gameModeLoss": 2000, "playerCashAward_flagCapture": 1000, "playerCashAward_flagReturned": 500,
                      "playerCashAward_flagStolen": 200, "playerCashAward_killingOpponent": 500, "playerCashAward_killingTeammate": 0,
                      "playerCashAward_killingOpponentFlagCarrier": 500, "ammo_rocketlauncher": 20, "ammo_dmg": 8}, awards
    sold = function_body(buying, "bool riBuyingManager::IsItemSold( const char *item )")
    assert "entry->awakeningPrice <= 0 : !entry->quake4" in sold
    assert 'idClass::GetClass( weaponDict->GetString( "weaponclass", "rvWeapon" ) )' in sold
    assert 'va( "def_mod%d", m + 1 )' in sold
    price = function_body(buying, "int riBuyingManager::GetItemPrice( const char *item )")
    assert "entry->awakeningPrice" in price and '"ItemCostConstants"' in price
    kill = function_body(buying, "int riBuyingManager::GetOpponentKillCashAward( void )")
    assert "opponentKillRules != rules" in kill
    mode = function_body(buying, "void MP_SetBuyingModeCVars( int mode )")
    assert "si_isBuyingEnabled.SetBool( mode != BUYING_MODE_OFF );" in mode
    assert "if ( mode != BUYING_MODE_OFF ) {" in mode
    onoff = function_body(buying, "int MP_BuyingModeFromOnOff( int on, int currentMode )")
    assert "currentMode != BUYING_MODE_OFF ? currentMode : BUYING_MODE_QUAKE4" in onoff
    return items


def check_rules_cvar() -> None:
    for tree in ("game", "mpgame"):
        cvars = read(f"src/{tree}/gamesys/SysCvar.cpp")
        assert re.search(r'idCVar si_buyingRules\(\s*"si_buyingRules",\s*"0",\s*CVAR_GAME \| CVAR_SERVERINFO \| CVAR_ARCHIVE \| CVAR_INTEGER,'
                         r'[^;]*, 0, 1 \);', cvars), tree
        assert "extern idCVar	si_buyingRules;" in read(f"src/{tree}/gamesys/SysCvar.h"), tree


def check_match_rules() -> None:
    """The match rules allow buying wherever a game type does (GTF_BUYING): a
    narrower mask rejected a buying CTF server's casual rules and its match
    never left warmup."""
    rules = read("src/mpgame/mp/match/MatchRules.cpp")
    mask = rules[rules.index("static const uint32_t MP_RULE_MODES_BUYING ="):]
    mask = mask[:mask.index(";")]
    allowed = set(re.findall(r"MP_RULE_GAME_BIT\( (GAME_\w+) \)", mask))
    if "MP_RULE_MODES_CAPTURE" in mask:
        capture = rules[rules.index("static const uint32_t MP_RULE_MODES_CAPTURE ="):]
        allowed |= set(re.findall(r"MP_RULE_GAME_BIT\( (GAME_\w+) \)", capture[:capture.index(";")]))
    types = read("src/mpgame/mp/GameTypes.cpp")
    buying = set()
    for row in re.findall(r"\{[^{}]*?\b(GAME_\w+)\b[^{}]*?GTF_BUYING[^{}]*?\}", types, re.S):
        buying.add(row)
    assert buying, "no GTF_BUYING game types found"
    assert allowed == buying, (sorted(allowed), sorted(buying))


def check_player() -> None:
    player = read("src/mpgame/Player.cpp")
    status = function_body(player, "itemBuyStatus_t idPlayer::ItemBuyStatus( const char* itemName )")
    assert status.index("mpBuyingManager.IsItemSold( itemName )") < status.index('!idStr::Cmpn( itemName, "wpmod_", 6 )')
    assert "HasItemPrice" not in player and "idBuyItemPrice" not in player and "buyItemsByImpulse" not in player
    assert "idBuyItemPrice" not in read("src/mpgame/Player.h")
    cost = function_body(player, "int idPlayer::GetItemCost( const char* itemName )")
    assert "mpBuyingManager.GetItemPrice( itemName )" in cost
    # Bought mods ride with the weapons a buying mode carries over a death.
    spawn = function_body(player, "void idPlayer::SpawnToPoint( const idVec3 &spawn_origin, const idAngles &spawn_angles )")
    saved = spawn.index("memcpy( carriedWeaponMods, inventory.weaponMods, sizeof( carriedWeaponMods ) );")
    assert saved < spawn.index("Init();") < spawn.index("inventory.weaponMods[ weaponIndex ] |= carriedWeaponMods[ weaponIndex ];")
    # Every client builds each weapon with its mods.
    write = function_body(player, "void idPlayer::WriteToSnapshot( idBitMsgDelta &msg ) const")
    assert write.rstrip("}").rstrip().endswith("WriteWeaponModsToSnapshot( msg );"), write[-200:]
    read_snapshot = function_body(player, "void idPlayer::ReadFromSnapshot( const idBitMsgDelta &msg )")
    assert read_snapshot.index("decodedCash = msg.ReadLong();") < read_snapshot.index("ReadWeaponModsFromSnapshot( msg, decodedWeaponMods );")
    assert read_snapshot.index("ReadWeaponModsFromSnapshot( msg, decodedWeaponMods );") < read_snapshot.index("if ( msg.IsReadOverflowed() ) {\n\t\treturn;\n\t}\n\n\tlastSnapshotSequence")
    assert read_snapshot.index("ReadWeaponModsFromSnapshot") < read_snapshot.index("// no msg reading below this")
    assert "if ( i == currentWeapon && weapon != NULL ) {\n\t\t\t\tcurrentWeapon = -1;" in read_snapshot
    latch = read_snapshot[read_snapshot.index("int latchWeaponMods[ MAX_WEAPONS ];"):]
    assert latch.index("Init();") < latch.index("memcpy( inventory.weaponMods, latchWeaponMods, sizeof( latchWeaponMods ) );")
    modwrite = function_body(player, "void idPlayer::WriteWeaponModsToSnapshot( idBitMsgDelta &msg ) const")
    assert "msg.WriteBits( moddedWeapons, MAX_WEAPONS );" in modwrite
    assert "msg.WriteBits( inventory.weaponMods[ i ] & WEAPON_MODS_SNAPSHOT_MASK, MAX_WEAPONMODS );" in modwrite
    assert "const int	WEAPON_MODS_SNAPSHOT_MASK = ( 1 << MAX_WEAPONMODS ) - 1;" in read("src/mpgame/Player.h")
    # Bots buy the mod for the gun in hand where the rules sell mods.
    bots = function_body(read("src/mpgame/bots/Bot.cpp"), "void rvBot::UpdateBuying( idPlayer *self )")
    assert "entry.category != BUY_CATEGORY_MODS" in bots and "self->SlotForWeapon( modDict->GetString( \"weapon\" ) ) == self->GetCurrentWeapon()" in bots
    weapon = function_body(read("src/mpgame/Weapon.cpp"), "void rvWeapon::Init( idPlayer* _owner, const idDeclEntityDef* def, int _weaponIndex, bool _isStrogg )")
    assert 'if ( mods && gameLocal.isMultiplayer && weaponDef->dict.GetInt( "clipSize" ) == 0 ) {' in weapon
    assert weapon.index("spawnArgs.Copy ( *modDict );") < weapon.index('spawnArgs.SetInt( "clipSize", 0 );')


def check_votes() -> None:
    mp = read("src/mpgame/MultiplayerGame.cpp")
    assert "voteData.m_buying < BUYING_MODE_OFF || voteData.m_buying >= BUYING_MODE_COUNT" in mp
    assert "voteData.m_buying == MP_BuyingModeFromCVars()" in mp
    assert mp.count("outMsg.WriteByte( idMath::ClampInt( BUYING_MODE_OFF, BUYING_MODE_COUNT - 1, voteData.m_buying ) );") == 3
    assert "voteData.m_buying ? 1 : 0" not in mp
    assert "MP_SetBuyingModeCVars( currentVoteData.m_buying );" in mp
    assert "MP_SetBuyingModeCVars( atoi( voteValue ) );" in mp
    assert "MP_SetBuyingModeCVars( data.buying );" in mp
    assert 'cvarSystem->SetCVarString( "si_isBuyingEnabled", voteValue );' not in mp
    assert mp.count("retainedVoteDraft[ RVF_BUYING ] = MP_BuyingModeFromServerInfo( si );") == 1
    assert mp.count("retainedAdminDraft[ RVF_BUYING ] = MP_BuyingModeFromServerInfo( si );") == 1
    assert 'publish( va( "mp.vote.buying%d", i ), common->GetLocalizedString( MP_BuyingModeName( i ) ) );' in mp
    assert 'publish( va( "mp.admin.buying%d", i ), common->GetLocalizedString( MP_BuyingModeName( i ) ) );' in mp
    assert "MP_BuyingModeFromOnOff( mainGui->GetStateInt( \"buying\" ), currentBuying )" in mp
    assert "int			buying;			// buyingMode_t" in read("src/mpgame/MultiplayerGame.h")
    network = read("src/mpgame/Game_network.cpp")
    assert "( voteData.m_buying < BUYING_MODE_OFF || voteData.m_buying >= BUYING_MODE_COUNT ) ||" in network


def check_card(items: list) -> None:
    mp = read("src/mpgame/MultiplayerGame.cpp")
    cover = function_body(mp, "void idMultiplayerGame::SetRetainedMenuCover( bool covered, idUserInterface *card )")
    assert "( currentMenu == 4 && buyMenu != NULL )" in cover
    publish = function_body(mp, "void idMultiplayerGame::PublishRetainedMenu( idUserInterface *card )")
    assert publish.index("if ( currentMenu == 4 ) {") < publish.index('publish( "mp.title"')
    assert "if ( PublishRetainedBuy( card ) ) {" in publish
    buy = function_body(mp, "bool idMultiplayerGame::PublishRetainedBuy( idUserInterface *card )")
    for state in ("shown", "available", "owned", "dear", "label", "price", "detail", "reason"):
        assert f'"mp.buy%d.{state}"' in buy, state
    command = function_body(mp, "bool idMultiplayerGame::HandleRetainedMenuCommand( const idCmdArgs &args, int &icmd )")
    branch = command[command.index('if ( !sub.Icmp( "buy" )'):]
    branch = branch[:branch.index('if ( !sub.Icmp( "vote" )')]
    assert "currentMenu != 4" in branch and "index >= buyCatalogCount" in branch
    assert "player->ItemBuyStatus( item ) == IBS_CAN_BUY" in branch and "GenerateImpulseForBuyAttempt( item );" in branch
    assert branch.rstrip().endswith("return false;\n\t}") or branch.rstrip().endswith("return false;\n\t}".rstrip()), branch[-80:]
    assert "if ( !retainedMenuCovered ) {\n\t\t\t\tbuyMenu->Redraw(gameLocal.time);" in mp

    menu = read("src/framework/Session_menu.cpp")
    assert 'static const char *const RETAINED_MP_BUY_GUI = "guis/menu/mp_buy.rui";' in menu
    assert f"static const int RETAINED_MP_BUY_ITEMS = {len(items)};" in menu
    assert "return Session_IsGameMenu( gui ) || Session_IsGameSummary( gui ) || Session_IsArenaPause( gui ) || Session_IsBuyMenu( gui );" in menu
    assert '!idStr::Icmp( gui->Name(), "guis/buymenu.gui" )' in menu
    assert 'buy ? "guis/buymenu.gui"' in menu
    assert 'gameCommand = va( "retained buy %d", atoi( indexText.c_str() ) );' in menu
    assert '{ "createBuying",		"buying",		"si_isBuyingEnabled",		0, 2,	false },' in menu
    assert 'cvarSystem->SetCVarInteger( "si_buyingRules", value == 2 ? 1 : 0 );' in menu
    adapter = read("src/ui/UserInterfaceRetained.cpp")
    assert '"mpBuy"' in adapter[adapter.index("bool SessionMenuCommand("):adapter.index("bool SessionMenuPlayerCommand(")]
    request = function_body(adapter, "std::optional<std::string> SessionMenuBuyRequest(const ActionInvocation& invocation)")
    assert "index < 0 || index >= 64" in request

    buy_card = document("content/baseoq4/pak0/guis/menu/mp_buy.rui")
    assert buy_card["id"] == "openq4.mp_buy"
    buys = {name: action for name, action in buy_card["actions"].items() if name.startswith("buy")}
    assert len(buys) == len(items), sorted(buys)
    for index in range(len(items)):
        action = buys[f"buy{index}"]
        assert action["operation"] == "session.menu", action
        assert action["arguments"]["command"]["literal"] if isinstance(action["arguments"]["command"], dict) else action["arguments"]["command"]
    for index in range(len(items)):
        for state in ("shown", "available", "owned", "dear", "label", "price", "reason", "detail"):
            assert f"mp.buy{index}.{state}" in buy_card["state"], (index, state)
    for state in ("mp.buy.title", "mp.buy.rules", "mp.buy.credits", "mp.buy.mods_count"):
        assert state in buy_card["state"], state
    escape = document("content/baseoq4/pak0/guis/menu/mp_escape.rui")
    assert escape["state"]["mp.vote.buying"]["type"] == "number"
    assert "mp.vote.buying_count" in escape["state"] and "mp.admin.buying_count" in escape["state"]
    create = document("content/baseoq4/pak0/guis/menu/createserver.rui")
    assert create["state"]["create.buying"]["type"] == "number"


def check_strings(items: list) -> None:
    tables = {}
    for path in sorted((ROOT / "content/baseoq4/pak0/strings").glob("english_*.lang")):
        for key, value in re.findall(r'"(#str_\d+)"\s+"([^"]*)"', path.read_text(encoding="utf-8")):
            tables.setdefault(key, value)
    for row in items:
        assert row["name"] in tables, row
        if row["detail"]:
            assert row["detail"] in tables, row
    for key in range(231900, 231928):
        assert f"#str_{key}" in tables, key
    assert tables["#str_231900"] == "The Awakening"
    assert tables["#str_231920"].count("%s") == 1


def main() -> int:
    items = check_catalog()
    check_rules_cvar()
    check_match_rules()
    check_player()
    check_votes()
    check_card(items)
    check_strings(items)
    print("mp_buying_rules_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
