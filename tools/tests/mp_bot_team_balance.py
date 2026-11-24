#!/usr/bin/env python3
"""Guard pre-spawn, same-instance multiplayer bot team balancing."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="replace")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    open_brace = source.find("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unterminated {signature}")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {needle!r} in {context}")


def main() -> None:
    header = read("src/game/bots/Bot.h")
    source = read("src/game/bots/Bot.cpp")
    multiplayer = read("src/game/MultiplayerGame.cpp")

    require(header, "initialTeamAssignmentPending", "rvBot reservation state")
    require(header, "teamAssignmentInstance", "rvBot instance reservation")
    require(header, "BalancedTeamForBot", "team-balancer API")

    init = body(source, "void rvBot::Init")
    require(init, "if ( !rejoining )", "new-bot initialization")
    require(
        init,
        "initialTeamAssignmentPending = true;",
        "new-bot initialization",
    )

    fill = body(source, "void rvBot::FillUserInfo")
    require(
        fill,
        "if ( initialTeamAssignmentPending ||",
        "inherited team override",
    )
    require(
        fill,
        "botManager.BalancedTeamForBot( clientNum, teamAssignmentInstance )",
        "pre-spawn team reservation",
    )
    require(fill, "initialTeamAssignmentPending = false;", "one-shot assignment")

    balance = body(source, "int rvBotManager::BalancedTeamForBot")
    require(balance, "for ( int i = 0; i < MAX_CLIENTS; i++ )", "slot scan")
    if "gameLocal.numClients" in balance:
        raise AssertionError("team balance must not trust the stale numClients watermark")
    require(
        balance,
        "player->GetInstance() != balanceInstance",
        "same-instance player count",
    )
    require(
        balance,
        'gameLocal.userInfo[i].GetString( "ui_team" )',
        "pending intended-team count",
    )
    require(
        balance,
        "bots[i].teamAssignmentInstance == balanceInstance",
        "same-instance pre-entity reservation",
    )
    require(
        balance,
        "teamCount[bots[i].teamAssignment]++",
        "pre-entity bot count",
    )

    verify = body(multiplayer, "int idMultiplayerGame::VerifyTeamSwitch")
    require(verify, "gameLocal.mpGame.IsInGame( i )", "live verifier population")
    require(
        verify,
        "!candidate->wantSpectate",
        "pending auto-join participation",
    )
    if "!candidate->spectating" in verify:
        raise AssertionError(
            "physical spawn state must not exclude a pending auto-joined player"
        )
    require(
        verify,
        "candidate->GetInstance() == player->GetInstance()",
        "same-instance verifier population",
    )
    require(verify, "candidate->team >= 0", "verifier lower team bound")
    require(verify, "candidate->team < TEAM_MAX", "verifier upper team bound")
    if verify.find("candidate->team < TEAM_MAX") > verify.find(
        "teamCount[candidate->team]++"
    ):
        raise AssertionError("team index must be validated before indexing teamCount")

    # One Marine human plus four synchronous bot reservations should finish
    # 3-2, not put all four bots on Strogg as the stale-watermark bug did.
    counts = [1, 0]
    assigned: list[int] = []
    for _ in range(4):
        team = 1 if counts[1] < counts[0] else 0
        assigned.append(team)
        counts[team] += 1
    if assigned != [1, 0, 1, 0] or counts != [3, 2]:
        raise AssertionError(f"unexpected balance model: {assigned=}, {counts=}")

    # Mirror the downstream VerifyTeamSwitch calls before any bot receives a
    # gameplay frame.  Physical spectating is deliberately true for every bot;
    # wantSpectate is false, so each latched reservation remains part of the
    # verifier population and is not overwritten with Strogg.
    participants = [(0, False, False, 0)]  # team, spectating, wantSpectate, instance
    verified: list[int] = []
    for requested in assigned:
        live_counts = [0, 0]
        for team, _spectating, want_spectate, instance in participants:
            if not want_spectate and instance == 0:
                live_counts[team] += 1
        if live_counts[0] > live_counts[1]:
            accepted = 1
        elif live_counts[1] > live_counts[0]:
            accepted = 0
        else:
            accepted = requested
        verified.append(accepted)
        participants.append((accepted, True, False, 0))
    if verified != [1, 0, 1, 0]:
        raise AssertionError(f"pending-spawn verifier overwrote reservations: {verified}")

    print("mp_bot_team_balance: ok")


if __name__ == "__main__":
    main()
