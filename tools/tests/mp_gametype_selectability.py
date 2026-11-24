#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contracts for safe, data-driven multiplayer gametype exposure."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def main() -> None:
    header = read("src/game/mp/GameTypes.h")
    table = read("src/game/mp/GameTypes.cpp")
    multiplayer = read("src/game/MultiplayerGame.cpp")

    for token in (
        "mpGameStateFactory_t",
        "stateFactory",
        "selectable",
        "MPGameTypeIsSelectable",
        "MP_GAMESTATE_FACTORY_COUNT",
    ):
        require(header, token, "descriptor API")

    # These append-only wire rows stay documented, but cannot enter completion,
    # vote/menu order or SetGameType until their authoritative state exists.
    for game_type in (
        "GAME_OVERLOAD",
        "GAME_HARVESTER",
        "GAME_DOMINATION",
        "GAME_ATTACK_DEFEND",
    ):
        row = re.search(
            rf"\{{\s*{game_type},(?P<body>.*?)\}},", table, re.DOTALL
        )
        if row is None:
            raise AssertionError(f"Missing append-only descriptor row {game_type}")
        require(row.group("body"), "MP_GAMESTATE_NONE, false", game_type)

    completion = re.search(
        r"const char \*si_gameTypeArgs\[\] = \{(?P<body>.*?)\};",
        table,
        re.DOTALL,
    )
    vote_order = re.search(
        r"static const int mpVoteGameTypeOrder\[\] = \{(?P<body>.*?)\};",
        table,
        re.DOTALL,
    )
    if completion is None or vote_order is None:
        raise AssertionError("Could not locate gametype exposure lists")

    for name in ("Overload", "Harvester", "Domination", "Attack Defend"):
        if f'"{name}"' in completion.group("body"):
            raise AssertionError(f"Unavailable mode {name} is still cvar-selectable")
    for game_type in (
        "GAME_OVERLOAD",
        "GAME_HARVESTER",
        "GAME_DOMINATION",
        "GAME_ATTACK_DEFEND",
    ):
        if game_type in vote_order.group("body"):
            raise AssertionError(f"Unavailable mode {game_type} is still votable")

    for token in (
        "info->stateFactory",
        "!MPGameTypeIsSelectable( info->type )",
        "unknown or unavailable; using DM",
        'gameLocal.serverInfo.Set( "si_gameType", info->name )',
    ):
        require(multiplayer, token, "safe SetGameType dispatch")

    for token in (
        "selectability and state factory disagree",
        "appears %d times in si_gameTypeArgs",
        "appears %d times in mpVoteGameTypeOrder",
        "duplicate gametype token",
    ):
        require(table, token, "startup descriptor validation")

    print("multiplayer gametype selectability contracts: PASS")


if __name__ == "__main__":
    main()
