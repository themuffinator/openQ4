#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contracts for the managed-match legacy administration boundary."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="strict")


def function(source: str, signature: str, next_marker: str) -> str:
    start = source.index(signature)
    end = source.index(next_marker, start + len(signature))
    return source[start:end]


def require_before(block: str, guard: str, mutation: str, context: str) -> None:
    if guard not in block or mutation not in block:
        raise AssertionError(f"missing managed guard or mutation in {context}")
    if block.index(guard) > block.index(mutation):
        raise AssertionError(f"managed guard follows mutation in {context}")


def main() -> None:
    multiplayer = read(ROOT / "src/game/MultiplayerGame.cpp")
    multiplayer_h = read(ROOT / "src/game/MultiplayerGame.h")
    game_local = read(ROOT / "src/game/Game_local.cpp")

    helper = function(
        multiplayer,
        "bool idMultiplayerGame::RejectManagedLegacyMutation",
        "idMultiplayerGame::IsManagedTeamCommunicationActive",
    )
    for token in (
        "if ( !IsManagedMatch() )",
        'common->GetLocalizedString( "#str_42749" )',
        "AddChatLine",
    ):
        if token not in helper:
            raise AssertionError(f"legacy mutation boundary lacks {token!r}")
    if "RejectManagedLegacyMutation" not in multiplayer_h:
        raise AssertionError("legacy mutation boundary is not exposed to command adapters")

    kick = function(
        multiplayer,
        "void idMultiplayerGame::HandleServerAdminKickPlayer",
        "idMultiplayerGame::HandleServerAdminForceTeamSwitch",
    )
    require_before(kick, "RejectManagedLegacyMutation", "BufferCommandText", "admin kick")

    switch = function(
        multiplayer,
        "void idMultiplayerGame::HandleServerAdminForceTeamSwitch",
        "idMultiplayerGame::HandleServerAdminCommands",
    )
    require_before(
        switch, "RejectManagedLegacyMutation", "BufferCommandText", "admin team switch"
    )

    settings = function(
        multiplayer,
        "bool idMultiplayerGame::HandleServerAdminCommands",
        "idMultiplayerGame::WriteStartState",
    )
    require_before(settings, "RejectManagedLegacyMutation", "si_gameType.SetString", "admin settings")
    for token in (
        "MPGameTypeByName(",
        "MPGameTypeIsSelectable( data.gameType )",
        "requestedGameType->name",
        "MultiplayerResolveMapDecl( data.mapName.c_str() )",
        "MPMapSupportsGameType( requestedMap, requestedGameType->type )",
    ):
        if token not in settings:
            raise AssertionError(f"registry-based admin validation lacks {token!r}")
    for forbidden in (
        'case GAME_1F_CTF:',
        'szGameType = "Arena CTF"',
        "default:\n\t\t\tcase GAME_DM",
        "PickMap( szGameType )",
        "data.minPlayers",
        "rcon si_minPlayers",
    ):
        if forbidden in settings:
            raise AssertionError(f"legacy admin fallback remains: {forbidden!r}")
    if "int\t\t\tminPlayers;" in multiplayer_h:
        raise AssertionError("uninitialized minPlayers remains in admin payload")

    gui = function(
        multiplayer,
        '} else if ( !idStr::Icmp( cmd, "handleServerAdmin" ) )',
        '} else if ( !idStr::Icmp( cmd, "handleServerAdminKick" ) )',
    )
    for token in (
        "adminGameTypeIndex < 0",
        "adminGameTypeIndex >= MPVoteGameTypeCount()",
        "MPVoteGameTypeToGameType( adminGameTypeIndex )",
    ):
        if token not in gui:
            raise AssertionError(f"admin GUI index is not fail-closed: missing {token!r}")

    shuffle = function(
        multiplayer,
        "void idMultiplayerGame::ShuffleTeams",
        "rvGameState* idMultiplayerGame::GetGameState",
    )
    require_before(shuffle, "RejectManagedLegacyMutation", "Set( \"ui_team\"", "team shuffle")
    if "if ( !gameLocal.IsTeamGame() )" not in shuffle:
        raise AssertionError("team shuffle is not restricted to team modes")

    command_contracts = (
        (
            "void idGameLocal::VerifyServerSettings_f",
            "idGameLocal::MapRestart_f",
            "PickMap(",
        ),
        (
            "void idGameLocal::MapRestart_f",
            "idGameLocal::NextMap",
            "gameLocal.MapRestart(",
        ),
        (
            "void idGameLocal::NextMap_f",
            "idGameLocal::GetStartingIndexForInstance",
            "gameLocal.NextMap(",
        ),
    )
    for signature, marker, mutation in command_contracts:
        block = function(game_local, signature, marker)
        require_before(block, "RejectManagedLegacyMutation", mutation, signature)

    print("mp_match_legacy_admin_contract: PASS")


if __name__ == "__main__":
    main()
