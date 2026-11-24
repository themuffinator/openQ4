#!/usr/bin/env python3
"""Guards Arena's Restart match resetting the live match in place.

The framework (src/framework/ArenaCampaign.cpp) restarts a live Arena match on
its running listen server with serverMapRestart instead of tearing the server
down and relaunching it. The bots keep their client slots through a map
restart, so the roster is never removed and added again, which is what used to
make a fresh server per restart necessary: population racing queued bot
removals.

The checks pin the parts no compiler sees together:

* the framework attempts the in-place restart first, only for its own listen
  server with exactly the authored roster connected and the bots populated,
  and keeps the full relaunch as the fallback;
* the authorization is a read-only CVar holding the live match token for the
  length of one serverMapRestart; game_mp clears it only after the map really
  restarted, and the framework reads that back before closing the pause menu;
* game_mp takes the authorized restart ahead of the administration guard that
  refuses every other restart of an Arena match, and refuses one whose result
  is already due;
* the restart path never removes, adds or repopulates bots, keeps client
  entities through the map clear, and gives each bot a new life on respawn.

Single-reversal controls then mutate the sources and require each check to fail.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

SOURCES = {
    "arena": ROOT / "src" / "framework" / "ArenaCampaign.cpp",
    "arena_header": ROOT / "src" / "framework" / "ArenaCampaign.h",
    "game_local": GAME_LIBS_ROOT / "src" / "game" / "Game_local.cpp",
    "mp": GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.cpp",
    "mp_header": GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.h",
    "player": GAME_LIBS_ROOT / "src" / "game" / "Player.cpp",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def forbid(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    positions = []
    for needle in needles:
        require(haystack, needle, context)
        positions.append(haystack.index(needle))
    if positions != sorted(positions):
        raise AssertionError(f"{context} must hold {needles!r} in this order")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r}")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[brace + 1:end - 1]


def validate_framework(sources: dict[str, str]) -> None:
    arena = sources["arena"]
    require(sources["arena_header"], "bool\t\t\t\t\tRestartMatchInPlace( int token );", "the campaign declaration")
    require(arena, '"ui_arenaRestartMatch", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT | CVAR_ROM,',
            "the read-only restart authorization")

    restart = body(arena, "void idArenaCampaign::RestartMatch()")
    # In place first, the full relaunch only when that is refused.
    require_order(restart, (
        "const int token = ActiveMatchToken();",
        "state->transitionPhase != ARENA_TRANSITION_MATCH",
        "if ( RestartMatchInPlace( token ) ) {",
        "return;\n\t}\n\tcommon->Printf( \"arena campaign: relaunching match %d\\n\", token );",
        "sessLocal.Stop();",
        "RetryResult();",
    ), "RestartMatch")

    in_place = body(arena, "bool idArenaCampaign::RestartMatchInPlace( int token )")
    for token, reason in (
        ("!state->botsPopulated", "only a populated roster restarts in place"),
        ("!idAsyncNetwork::server.IsActive()", "only the campaign's running server"),
        ('idStr::Icmp( cvarSystem->GetCVarString( "com_activeGameModule" ), "game_mp" ) != 0', "only game_mp"),
        ("!sessLocal.IsMapSpawned()", "only a spawned map"),
        ("if ( clients != 1 + match->bots.Num() ) {", "exactly the host and the authored bots"),
    ):
        require(in_place, token, reason)
    require_order(in_place, (
        "if ( clients != 1 + match->bots.Num() ) {",
        "ui_arenaRestartMatch.SetInteger( token );",
        'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "serverMapRestart\\n" );',
        "const bool restarted = ui_arenaRestartMatch.GetInteger() == 0;",
        "ui_arenaRestartMatch.SetInteger( 0 );",
        "if ( !restarted ) {",
        'sessLocal.HandleGameMenuReturn( game->HandleGuiCommands( "arenaPause resume" ) );',
        "ui_arenaMenuOpen.SetBool( ArenaMenuCoversMatch() );",
    ), "RestartMatchInPlace")
    require(in_place, 'idStr::Icmp( gui->Name(), "guis/arena_pause.gui" ) == 0', "only the pause menu closes")
    # The roster stays connected: nothing in this path touches the bots.
    for stale in ("removebot", "addbot", "kick", "PopulateBots", "botsPopulated =", "sessLocal.Stop()", "AbortMatch()"):
        forbid(in_place, stale, "RestartMatchInPlace")

    populate = body(arena, "void idArenaCampaign::PopulateBots()")
    require(populate, "if ( !IsActive() || state->botsPopulated ) {", "a restarted match is never repopulated")


def validate_game(sources: dict[str, str]) -> None:
    game_local = sources["game_local"]
    mp = sources["mp"]
    mp_header = sources["mp_header"]

    command = body(game_local, "void idGameLocal::MapRestart_f( const idCmdArgs &args )")
    require_order(command, (
        "if ( args.Argc() == 1 && gameLocal.mpGame.ArenaCampaignRestartAuthorized() ) {",
        "gameLocal.mpGame.RestartArenaCampaignMatch();",
        'gameLocal.mpGame.RejectManagedLegacyMutation( "server map restart" )',
    ), "serverMapRestart")

    require(mp_header, "bool\t\t\tArenaCampaignRestartAuthorized( void ) const;", "the game declaration")
    require(mp_header, "void\t\t\tRestartArenaCampaignMatch( void );", "the game declaration")
    require(mp_header, "int\t\t\t\tmapRestartCount;", "the restart counter")
    require(body(mp, "idMultiplayerGame::idMultiplayerGame()"), "mapRestartCount = 0;", "the restart counter starts at zero")

    authorized = body(mp, "bool idMultiplayerGame::ArenaCampaignRestartAuthorized( void ) const")
    require_order(authorized, (
        "if ( !IsArenaCampaignMatch() ) {",
        'const int token = gameLocal.serverInfo.GetInt( "si_arenaCampaign" );',
        'return token > 0 && cvarSystem->GetCVarInteger( "ui_arenaRestartMatch" ) == token;',
    ), "the restart authorization")

    restart = body(mp, "void idMultiplayerGame::RestartArenaCampaignMatch( void )")
    require(restart, "( state != WARMUP && state != COUNTDOWN && state != GAMEON && state != SUDDENDEATH ) ||",
            "only a live match restarts in place")
    require(restart, "arenaResultPending || arenaResultReported ) {", "a match whose result is due never restarts in place")
    require_order(restart, (
        "arenaResultPending || arenaResultReported ) {",
        "const int restarts = mapRestartCount;",
        "gameLocal.MapRestart( -1 );",
        "if ( mapRestartCount == restarts ) {",
        "gameLocal.sessionCommand = sessionCommand;",
        "matchStartedTime = gameLocal.time;",
        "SetPlayerScore( player, 0 );",
        'cvarSystem->SetCVarInteger( "ui_arenaRestartMatch", 0 );',
    ), "RestartArenaCampaignMatch")
    # Only a restart that happened is acknowledged.
    if restart.index('cvarSystem->SetCVarInteger( "ui_arenaRestartMatch", 0 );') < restart.index("if ( mapRestartCount == restarts ) {"):
        raise AssertionError("game_mp must acknowledge the restart only after the map restarted")
    for stale in ("removebot", "addbot", "kick", "RemoveBot", "AddBot"):
        forbid(restart, stale, "RestartArenaCampaignMatch")

    map_restart = body(mp, "void idMultiplayerGame::MapRestart( void )")
    require_order(map_restart, (
        "if ( !BeginMatchSession() ) {",
        "return;",
        "mapRestartCount++;",
    ), "a restart counts only once the match session restarted")

    # The pause menu's Restart still goes to the campaign.
    pause = body(mp, "const char *idMultiplayerGame::HandleArenaPauseCommand( const char *action )")
    require(pause, 'cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "arenaCampaignRestart\\n" );', "Restart asks the campaign")

    # The bots ride through the map restart: client entities survive the clear,
    # the ceremony state resets so warmup introduces them again, and each
    # respawn is a new life for the bot behind it.
    local_restart = body(game_local, "void idGameLocal::LocalMapRestart( int instance )")
    require(local_restart, "MapClear( false, instance );", "a map restart keeps the client entities")
    clear_map = body(mp, "void idMultiplayerGame::ClearMap( void )")
    require(clear_map, "ResetArenaCampaignCeremonyState();", "a map restart replays the introductions")
    require(sources["player"], "botManager.OnPlayerRespawned( entityNumber );", "a restarted bot starts a new life")


def validate(sources: dict[str, str]) -> None:
    validate_framework(sources)
    validate_game(sources)


def expect_failure(sources: dict[str, str], label: str) -> None:
    try:
        validate(sources)
    except AssertionError:
        return
    raise AssertionError(f"control did not fail: {label}")


CONTROLS = (
    ("the framework skips the in-place attempt", "arena",
     "if ( RestartMatchInPlace( token ) ) {", "if ( false ) {"),
    ("the roster is not checked", "arena",
     "if ( clients != 1 + match->bots.Num() ) {", "if ( false ) {"),
    ("any outcome counts as a restart", "arena",
     "const bool restarted = ui_arenaRestartMatch.GetInteger() == 0;", "const bool restarted = true;"),
    ("the console can authorize a restart", "arena",
     '"ui_arenaRestartMatch", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT | CVAR_ROM,',
     '"ui_arenaRestartMatch", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,'),
    ("the in-place path removes the bots first", "arena",
     'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "serverMapRestart\\n" );',
     'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "removebot all\\n" );\n\tcmdSystem->BufferCommandText( CMD_EXEC_NOW, "serverMapRestart\\n" );'),
    ("the pause menu stays over the restarted match", "arena",
     'sessLocal.HandleGameMenuReturn( game->HandleGuiCommands( "arenaPause resume" ) );', ""),
    ("serverMapRestart ignores the authorization", "game_local",
     "if ( args.Argc() == 1 && gameLocal.mpGame.ArenaCampaignRestartAuthorized() ) {", "if ( false ) {"),
    ("game_mp does not compare the token", "mp",
     'return token > 0 && cvarSystem->GetCVarInteger( "ui_arenaRestartMatch" ) == token;', "return token > 0;"),
    ("the ceremony can be restarted under its result", "mp",
     'arenaResultPending || arenaResultReported ) {\n\t\tgameLocal.Printf( "arena campaign: no live match to restart in place\\n" );',
     'false ) {\n\t\tgameLocal.Printf( "arena campaign: no live match to restart in place\\n" );'),
    ("game_mp acknowledges before the map restarted", "mp",
     "const int restarts = mapRestartCount;",
     'cvarSystem->SetCVarInteger( "ui_arenaRestartMatch", 0 );\n\tconst int restarts = mapRestartCount;'),
    ("a completed restart is not counted", "mp", "\tmapRestartCount++;\n", "\n"),
)


def main() -> int:
    try:
        sources = {name: read(path) for name, path in SOURCES.items()}
        validate(sources)
        for label, name, original, mutated in CONTROLS:
            if sources[name].count(original) != 1:
                raise AssertionError(f"control anchor is not unique: {label}")
            mutated_sources = dict(sources)
            mutated_sources[name] = sources[name].replace(original, mutated, 1)
            expect_failure(mutated_sources, label)
    except AssertionError as error:
        print(f"arena_restart_in_place: FAILED - {error}")
        return 1
    print(f"arena_restart_in_place: ok ({len(CONTROLS)} single-reversal controls fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
