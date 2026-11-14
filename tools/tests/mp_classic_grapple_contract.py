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
    rules = read("src/mpgame/mp/ClassicRules.cpp")
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
    player = read("src/mpgame/Player.cpp")
    header = read("src/mpgame/mp/ClassicRules.h")
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
    bot = read("src/mpgame/bots/Bot.cpp")
    hook = function_body(bot, "bool rvBot::UpdateHook( idPlayer *self, usercmd_t &cmd )")
    find = function_body(bot, "bool rvBot::FindHookShortcut( idPlayer *self, idVec3 &bite, idVec3 &landing ) const")
    movement = function_body(bot, "void rvBot::UpdateMovement( idPlayer *self, usercmd_t &cmd )")
    aim = function_body(bot, "void rvBot::UpdateAim( idPlayer *self, usercmd_t &cmd )")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    traits = read("src/mpgame/bots/BotCharacter.cpp")

    require("bot_grapple.GetBool()" in hook and "rvClassicRules::GrappleAllowed()" in hook,
            "bots hook only where the server allows it and bot_grapple is on")
    require('"bot_grapple"' in cvars, "bot_grapple is declared")
    require(re.search(r"BOTTRAIT\(\s*hookUse,", traits) is not None and "traits.hookUse" in hook,
            "the hookUse trait decides how often a bot takes a shortcut")
    require("NAVTRAVEL_TELEPORT" in find and "NAVTRAVEL_JUMPPAD" in find, "shortcuts never reach past a teleporter or pad")
    require("TraceBounds" in find and "MASK_PLAYERSOLID" in find, "the body must fit along the pull")
    require("ENTITYNUM_WORLD" in function_body(bot, "static bool BotHookBiteOk( const trace_t &tr )"),
            "bots plan only on bites in the world")
    require("aimAngles.ToForward()" in hook, "the throw waits until the bot's own view would put the hook on the bite")
    require("Repath( self, goalOrigin, false )" in hook, "the route is rebuilt where the bot comes down")
    require(movement.index("UpdateHook( self, cmd )") < movement.index("HoldDodgeJump( self, cmd )"),
            "the hook owns movement before any other movement stage")
    require("hookPhase == BOTHOOK_AIM" in aim and "aimPoint = hookBite;" in aim, "turning to throw looks at the bite")


def main() -> int:
    try:
        check_overlay()
        check_player()
        check_bots()
    except (AssertionError, ValueError) as error:
        print(f"mp_classic_grapple_contract: FAIL: {error}")
        return 1
    print("mp_classic_grapple_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
