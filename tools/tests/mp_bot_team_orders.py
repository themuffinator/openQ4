#!/usr/bin/env python3
"""Pin the multiplayer bots' team orders.

A human player can order the bots on their own team in team chat - "kane
defend", "everyone follow me", "sorg and bagby hold here" - and the objective
planner honours the order before it shares roles out.  The parser is spliced
verbatim from src/game/bots/Bot.cpp into
tools/tests/native/MpBotTeamOrdersFixture.cpp and run against phrasings that
must and must not be orders; the wiring around it - who may give orders, when
an order lapses, how every mode's planner reads it - is pinned by source.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOT = ROOT / "src/game/bots/Bot.cpp"
BOT_H = ROOT / "src/game/bots/Bot.h"
OBJECTIVE = ROOT / "src/game/bots/BotObjective.cpp"


def function(source: str, signature: str) -> str:
    """Return one function, from its signature to the matching closing brace."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def run_parser(bot: str, header: str) -> None:
    kinds = header[header.index("typedef enum {\n\tBOTORDER_NONE") :]
    kinds = kinds[: kinds.index("} botOrderKind_t;") + len("} botOrderKind_t;")]
    start = bot.index("static int BotOrderWordsAt(")
    parser = function(bot, "static int BotParseTeamOrder(")
    end = bot.index(parser) + len(parser)
    bodies = bot[start:end]
    harness = (ROOT / "tools/tests/native/MpBotTeamOrdersFixture.cpp").read_text(encoding="utf-8")
    harness = harness.replace("// PRODUCTION_ORDER_KINDS", kinds).replace("// PRODUCTION_FUNCTIONS", bodies)

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the team-order parser test")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-bot-orders-", dir=output) as directory:
        path = Path(directory)
        cpp, exe = path / "orders.cpp", path / ("orders.exe" if os.name == "nt" else "orders")
        cpp.write_text(harness, encoding="utf-8")
        command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)


def validate_wiring(bot: str, objective: str) -> None:
    chat = function(bot, "void rvBotManager::OnChatMessage(")
    # Orders are gameplay: honoured with bot chat off, and not answered as chat.
    if chat.index("HandleTeamOrder(") > chat.index("bot_chat.GetInteger() <= 0"):
        raise AssertionError("OnChatMessage gates team orders behind bot_chat; orders must work with chat off")
    require(chat, "if ( HandleTeamOrder( sourceClientNum, teamOnly, normalizedText ) ) {",
            "rvBotManager::OnChatMessage order before small talk")

    handle = function(bot, "bool rvBotManager::HandleTeamOrder(")
    # Only a human, only in team chat, only in a team game, only own team.
    require(handle, "!teamOnly || !gameLocal.IsTeamGame() || IsBot( sourceClientNum )",
            "rvBotManager::HandleTeamOrder who may order")
    require(handle, "bot->team != speaker->team", "rvBotManager::HandleTeamOrder own team only")
    require(handle, "gameLocal.spawnIds[sourceClientNum]", "rvBotManager::HandleTeamOrder commander identity")
    require(handle, "BotOrderAnchor( speaker )", "rvBotManager::HandleTeamOrder spot defence anchor")
    require(handle, "acknowledged < 2", "rvBotManager::HandleTeamOrder bounded acknowledgments")

    get = function(bot, "const botOrder_t *rvBotManager::GetOrder(")
    require(get, "bot->team != order.team", "rvBotManager::GetOrder lapses on a team change")
    require(get, "gameLocal.spawnIds[commander] != order.commanderSpawnId",
            "rvBotManager::GetOrder follow ends when the commander leaves")

    ack = function(bot, "void rvBot::QueueOrderAck(")
    require(ack, "BOTCHAT_ORDER", "rvBot::QueueOrderAck chat event")
    require(ack, "chatTeamOnly	= true;", "rvBot::QueueOrderAck team chat")
    require(ack, "AllowChat( clientNum )", "rvBot::QueueOrderAck flood throttle")
    if "chatiness" in ack:
        raise AssertionError("QueueOrderAck rolls chatiness; an order must always be acknowledged")

    for hook in ("void rvBotManager::Init(", "void rvBotManager::OnMapShutdown(",
                 "void rvBotManager::OnClientDisconnect("):
        require(function(bot, hook), "ClearOrder(", f"{hook} clears orders")

    # Every planner reads the order, and team-mates allocate around ordered bots.
    for planner in ("static bool BotFindCTFObjective(", "static bool BotFindFreezeTagObjective(",
                    "static bool BotFindDeadZoneObjective(", "bool BotFindObjective("):
        body = function(objective, planner)
        require(body, "botManager.GetOrder( self->entityNumber )", f"{planner} reads the order")
    for planner in ("static bool BotFindCTFObjective(", "static bool BotFindFreezeTagObjective(",
                    "static bool BotFindDeadZoneObjective("):
        require(function(objective, planner), "BotObjectiveBlockOrdered(", f"{planner} allocates around orders")
    ctf = function(objective, "static bool BotFindCTFObjective(")
    if ctf.index("BotObjectiveFromOrder(") < ctf.index("self->PowerUpActive( carriedPowerup )"):
        raise AssertionError("A CTF order outranks carrying the flag home; the carrier's duty must come first")
    # An escort beside a stopped leader is working, not stalled: the give-up
    # used to fire and put the leader on the avoid list.
    goal = function(bot, "void rvBot::UpdateGoal(")
    require(goal, "BOT_ESCORT_KEEP_RADIUS", "rvBot::UpdateGoal escort keeps its leader")
    if goal.index("BOT_ESCORT_KEEP_RADIUS") > goal.index("giving up on goal"):
        raise AssertionError("UpdateGoal checks the escort radius after the give-up has already fired")
    # Fighting is not stalling, and an escort that did lose its way retries
    # within seconds instead of refusing its leader for twenty.
    require(goal, "if ( combatSteeringTime > goalProgressTime ) {", "rvBot::UpdateGoal combat is not a stall")
    require(goal, "AbandonGoal( escort ? BOT_ESCORT_RETRY_MSEC : BOT_GOAL_AVOID_MSEC )",
            "rvBot::UpdateGoal escort retry")
    from_order = function(objective, "static bool BotObjectiveFromOrder(")
    for kind in ("BOTORDER_FOLLOW", "BOTORDER_DEFEND", "BOTORDER_ATTACK"):
        require(from_order, kind, "BotObjectiveFromOrder")


def main() -> None:
    bot = BOT.read_text(encoding="utf-8")
    header = BOT_H.read_text(encoding="utf-8")
    objective = OBJECTIVE.read_text(encoding="utf-8")
    validate_wiring(bot, objective)
    run_parser(bot, header)
    print("mp_bot_team_orders: ok")


if __name__ == "__main__":
    main()
