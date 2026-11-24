#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Pin the Classic ruleset's grappling hook and the definition overlay it rides on.

The hook (si_ruleset 1, si_classicGrapple) is thrown, flown and anchored by the
server in idPlayer, pulled in idPhysics_Player so its owner predicts it, drawn
by every end from the throw it carries in the snapshot, and used by bots as a
route shortcut. The overlay that turns Quake 4's numbers into Classic ones must
only ever change the engine's entityDef dictionaries through the engine's own
code. See docs/user/classic-ruleset.md.
"""
from __future__ import annotations

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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_overlay() -> None:
    rules = read("src/game/mp/ClassicRules.cpp")
    apply_body = function_body(rules, "void rvClassicRules::ApplyDefinitions( void )")
    revert_body = function_body(rules, "void rvClassicRules::RevertDefinitions( void )")
    reparse = function_body(rules, "static void ClassicReparse(")
    # The decl's dictionary belongs to the engine; game-module idDict code on it
    # mixes the two modules' string pools and crashed the client on quit.
    for name, body in (("ApplyDefinitions", apply_body), ("RevertDefinitions", revert_body)):
        require(not re.search(r"(editable|decl|entry\.decl)->dict\.(Set|Delete|Clear|Copy|SetDefaults)\b", body)
                and not re.search(r"\bdict\.(Set|Delete)\(", body),
                f"{name} must not edit an engine entityDef dictionary with game-module idDict code")
    require("decl->FreeData();" in reparse and "decl->Parse(" in reparse and ", true )" in reparse,
            "ClassicReparse rebuilds the dictionary through the engine's FreeData and Parse, without media caching")
    require("ClassicReparse( entry.decl, text )" in apply_body, "ApplyDefinitions reparses each patched definition")
    require("ClassicReparse( entry.decl, entry.original )" in revert_body,
            "RevertDefinitions reparses the shipped text it saved")
    require("patched.Copy( entry.decl->dict )" in apply_body,
            "the patched copy is a game-side dictionary copied from the engine's")


def check_player() -> None:
    player = read("src/game/Player.cpp")
    header = read("src/game/mp/ClassicRules.h")
    update = function_body(player, "void idPlayer::UpdateClassicHook( void )")
    present = function_body(player, "void idPlayer::PresentClassicHook( void )")
    anchor = function_body(player, "bool idPlayer::UpdateClassicHookAnchor( void )")
    flight = function_body(player, "idVec3 idPlayer::ClassicHookFlightPoint( int time ) const")
    teleport = function_body(player, "void idPlayer::Teleport( const idVec3 &origin")
    write = function_body(player, "void idPlayer::WriteToSnapshot( idBitMsgDelta &msg ) const")

    require("gameLocal.isClient" in update.split("\n", 3)[1] + update.split("\n", 3)[2],
            "UpdateClassicHook is server only")
    require("rvClassicRules::GrappleAllowed()" in update, "the hook only works where the server allows it")
    require("ClassicHookFlightPoint( gameLocal.time )" in update and "ClassicHookFlightPoint( gameLocal.time )" in present,
            "server and presentation place a flying hook by the same function of time")
    require("classicHookStart" in flight and "CLASSIC_HOOK_FLY_SPEED" in flight, "flight is a straight line from the throw")
    for kind in ("idActor", "idItem", "idProjectile"):
        require(f"{kind}::GetClassType()" in update, f"the hook lets go of an {kind}")
    require("SURF_NOIMPACT" in update, "the hook lets go of the sky")
    require("classicHookAnchor = hit;" in update and "gameLocal.world" in update, "a bite on anything but the world anchors to it")
    require("UpdateClassicHookAnchor();" in update, "an attached hook follows its anchor every frame")
    require("IsHidden()" in anchor and "GetContents() == 0" in anchor and "ResetClassicHook( true )" in anchor,
            "the hook lets go of a removed, hidden or non-solid anchor")
    require("SetClassicGrapple( classicHookOrigin )" in anchor, "a moving anchor moves the predicted pull point")
    require("ResetClassicHook( false );" in teleport, "a teleport releases the hook")
    # the snapshot layout: the throw while flying, the point once attached, Classic maps only
    classic = write[write.index("if ( gameLocal.IsClassicRuleset() )"):]
    require("classicHookFireTime" in classic and "classicHookDir" in classic and "classicHookStart" in classic,
            "a flying hook's snapshot carries its throw")
    require("MarkReadOverflowed" in player[player.index("decodedHookState = msg.ReadBits"):][:1500],
            "an unknown hook state fails the snapshot read")
    for sound in ("THROW", "BITE", "REEL", "RELEASE"):
        require(f"CLASSIC_HOOK_SND_{sound}" in header and f"CLASSIC_HOOK_SND_{sound}" in player,
                f"hook sound {sound} is defined and used")
    require("SND_CHANNEL_HEART" in present and "StopSound( SND_CHANNEL_HEART, false )" in
            function_body(player, "void idPlayer::FreeClassicHookPresentation( void )"),
            "the reel loop is local and stops with the presentation")


def check_bots() -> None:
    bot = read("src/game/bots/Bot.cpp")
    hook = function_body(bot, "bool rvBot::UpdateHook( idPlayer *self, usercmd_t &cmd )")
    find = function_body(bot, "bool rvBot::FindHookShortcut( idPlayer *self, bool relaxed, idVec3 &bite, idVec3 &landing ) const")
    bite = function_body(bot, "bool rvBot::FindHookBite( idPlayer *self, const idVec3 &landing, const idVec3 &along, idVec3 &bite ) const")
    rescue = function_body(bot, "bool rvBot::FindHookRescue( idPlayer *self, idVec3 &bite, idVec3 &landing ) const")
    movement = function_body(bot, "void rvBot::UpdateMovement( idPlayer *self, usercmd_t &cmd )")
    aim = function_body(bot, "void rvBot::UpdateAim( idPlayer *self, usercmd_t &cmd )")
    cvars = read("src/game/gamesys/SysCvar.cpp")
    traits = read("src/game/bots/BotCharacter.cpp")

    require("bot_grapple.GetBool()" in hook and "rvClassicRules::GrappleAllowed()" in hook,
            "bots hook only where the server allows it and bot_grapple is on")
    require('"bot_grapple"' in cvars, "bot_grapple is declared")
    require(re.search(r"BOTTRAIT\(\s*hookUse,", traits) is not None and "traits.hookUse" in hook,
            "the hookUse trait decides how often a bot takes a shortcut")
    require("NAVTRAVEL_TELEPORT" in find and "NAVTRAVEL_JUMPPAD" in find, "shortcuts never reach past a teleporter or pad")
    require("FindHookBite( self, corner, along, bite )" in find, "shortcuts use the shared bite search")
    require("TraceBounds" in bite and "MASK_PLAYERSOLID" in bite, "the body must fit along the pull")
    require("BOT_HOOK_CEILING_REACH" in bite and "BOT_HOOK_BITE_RADIUS" in bite,
            "a bite is a ceiling over the landing or a wall near it, never far from it")
    # a rescue answers a fall the navmesh proves unsafe, back over trusted floor
    rescue_gate = hook[hook.index("BotAirLandingUnsafe( self, touchdown )"):][:400]
    require("BOT_HOOK_RESCUE_MIN_DROP" in rescue_gate and "FindHookRescue(" in rescue_gate,
            "a rescue needs an unsafe fall into nothing or well down")
    require("hookLastGround" in rescue and "FindHookBite(" in rescue, "a rescue lands on the floor the bot left or a corner")
    stuck = bot[bot.index("stuckChecks++;"):][:400]
    require("hookUnstickRequest = gameLocal.time;" in stuck, "a stall asks for the hook before the route is thrown away")
    require("FindHookShortcut( self, true," in hook, "a stall searches the nearest corners")
    require("BOT_HOOK_CARRIER_USE" in hook and "Withdrawing( self )" in hook,
            "carriers and withdrawing bots may turn away from a fight to throw")
    audit = read("tools/tests/mp_bot_audit.py")
    require('"hook_pulled"' in audit and "--min-hook-pulls" in audit, "the bot audit counts hook pulls and can require them")
    require("ENTITYNUM_WORLD" in function_body(bot, "static bool BotHookBiteOk( const trace_t &tr )"),
            "bots plan only on bites in the world")
    require("aimAngles.ToForward()" in hook, "the throw waits until the bot's own view would put the hook on the bite")
    require("Repath( self, goalOrigin, false )" in hook, "the route is rebuilt where the bot comes down")
    require(movement.index("UpdateHook( self, cmd )") < movement.index("HoldDodgeJump( self, cmd )"),
            "the hook owns movement before any other movement stage")
    require("hookPhase == BOTHOOK_AIM" in aim and "aimPoint = hookBite;" in aim, "turning to throw looks at the bite")


def check_rules() -> None:
    """The ruleset audit of 2026-11-23: rows and rules that had been missing."""
    rules = read("src/game/mp/ClassicRules.cpp")
    player = read("src/game/Player.cpp")
    item_h = read("src/game/Item.h")
    rows = set(re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*(NULL|"[^"]*")\s*\}', rules))
    # the zoomed nailgun's seeking nails had kept a 25 point splash
    require(("projectile_nail_seek", "def_splash_damage", "NULL") in rows, "seeking nails lose their splash")
    # slower rockets still reach as far as Quake 2's
    require(("projectile_rocket", "fuse", '"12"') in rows, "rockets live long enough at 650 units a second")
    # the expansion maps' napalm launcher respawned every five seconds
    for row in (("weapon_napalmgun_mp", "respawn", '"30"'), ("ammo_napalmgun_mp", "respawn", '"30"'),
                ("weapon_napalmgun_mp", "inv_start_ammo_napalmgun", "NULL")):
        require(row in rows, f"napalm launcher row {row}")
    modifier = function_body(player, "float idPlayer::PowerUpModifier( int type )")
    require(modifier.count("gameLocal.IsClassicRuleset() ? 4.0f : 3.0f") == 2, "Classic quad damage is four times")
    give = function_body(player, "bool idPlayer::Give( const char *statname, const char *value, bool dropped, bool updateHud )")
    require("PowerUpActive( POWERUP_SCOUT ) || !rvClassicRules::GiveArmor( this, value )" in give,
            "a Scout carrier takes no tiered armour")
    clear = function_body(player, "void idPlayer::ClearPowerup( int i )")
    require("i == POWERUP_REGENERATION" in clear and "classicMegaDecayTime" in clear,
            "Regeneration's overheal drains once the powerup ends")
    frag = function_body(rules, "bool rvClassicRules::AwardFragBonuses( idPlayer *victim, idPlayer *killer )")
    require("BaseFlag( killer->team )" in frag and "GetFlagEntity" not in frag,
            "flag defence is scored around the base flag, not the dropped or carried one")
    require("IsDroppedFlag()" in function_body(rules, "static idEntity *BaseFlag( int team )") and "IsDroppedFlag" in item_h,
            "the base flag is the one that was never dropped")
    capture = function_body(rules, "bool rvClassicRules::AwardCapture( idPlayer *capturer )")
    require("( p == capturer ) ? CLASSIC_CTF_CAPTURE_BONUS : CLASSIC_CTF_TEAM_BONUS" in capture,
            "the capturer earns assists too")
    require("lastHurtCarrier = 0" in capture, "a capture ends the danger to the carrier")


def main() -> int:
    try:
        check_overlay()
        check_player()
        check_bots()
        check_rules()
    except (AssertionError, ValueError) as error:
        print(f"mp_classic_grapple_contract: FAIL: {error}")
        return 1
    print("mp_classic_grapple_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
