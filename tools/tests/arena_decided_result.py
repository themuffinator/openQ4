#!/usr/bin/env python3
"""Guards a decided Arena result surviving the end-of-match ceremony being cut short.

game_mp decides an Arena match's result when the match ends, but hands it to the
framework (arenaComplete) only after the ceremony. Leaving the match or quitting
the game during the ceremony used to throw the result away: a disconnect
reported the match abandoned, and a quit recorded nothing.

The checks pin the parts no compiler sees together:

* game_mp publishes the decided, contested result in the read-only
  ui_arenaDecidedResult, with the same arguments the handoff carries, every
  frame while the handoff waits, and clears it once the result is reported or
  discarded; an aborted match publishes nothing;
* the framework parses it with the handoff's own strict parser, refuses an
  aborted outcome, a stale token or an unpopulated match, and accepts it as the
  handoff would;
* a disconnect records and reports it before abandoning the match; a quit and
  an error drop record it before AbortMatch, and the quit's configuration write
  comes after, so the record is kept;
* recording is its own step with no teardown, shared by the normal return.

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
    "session": ROOT / "src" / "framework" / "Session.cpp",
    "common": ROOT / "src" / "framework" / "Common.cpp",
    "mp": GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.cpp",
    "mp_header": GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.h",
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


def validate_game(sources: dict[str, str]) -> None:
    mp = sources["mp"]
    require(sources["mp_header"], "void\t\t\tPublishArenaCampaignDecidedResult( void );", "the game declaration")
    run = body(mp, "void idMultiplayerGame::Run( void )")
    require_order(run, ("UpdateArenaCampaignResult();", "PublishArenaCampaignDecidedResult();",
                        "if ( IsGameplayFrozen() || arenaPaused ) {"),
                  "the decided result is published every frame, frozen or not, after the handoff runs")
    publish = body(mp, "void idMultiplayerGame::PublishArenaCampaignDecidedResult( void )")
    require(publish, "if ( !IsArenaCampaignMatch() ) {", "only an Arena match publishes")
    require(publish, "if ( arenaResultPending && !arenaResultReported && arenaResultOutcome != ARENA_RESULT_ABORTED ) {",
            "only a decided, unreported, contested result is published")
    report = body(mp, "void idMultiplayerGame::UpdateArenaCampaignResult( void )")
    for field in ("arenaResultToken,", "arenaResultOutcome,", "arenaResultPlayerScore,", "arenaResultOpponentScore,",
                  "ArenaCampaignAwardMask( resultHost != NULL ? resultHost->entityNumber : -1 ) );"):
        require(publish, field, "the published result carries the handoff's arguments")
        require(report, field, "the handoff carries the same arguments")
    require(publish, 'decided = va( "%d %d %d %d %d",', "the published result's format")
    require(report, 'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d",', "the handoff's format")
    require_order(publish, ('decided.Cmp( cvarSystem->GetCVarString( "ui_arenaDecidedResult" ) ) != 0',
                            'cvarSystem->SetCVarString( "ui_arenaDecidedResult", decided.c_str() );'),
                  "the result is written only when it changes, and cleared when it is no longer pending")


def validate_framework(sources: dict[str, str]) -> None:
    arena = sources["arena"]
    header = sources["arena_header"]
    require(arena, '"ui_arenaDecidedResult", "", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ROM,', "the read-only decided result")
    require(body(arena, "static void ArenaClearMatchPresentation()"), 'ui_arenaDecidedResult.SetString( "" );',
            "the decided result is cleared with the match")
    for declaration in ("bool\t\t\t\t\tFinishDecidedResult();", "void\t\t\t\t\tRecordDecidedResult();",
                        "bool\t\t\t\t\tTakeDecidedResult();", "bool\t\t\t\t\tRecordPendingCompletion();"):
        require(header, declaration, "the campaign declarations")

    take = body(arena, "bool idArenaCampaign::TakeDecidedResult()")
    require_order(take, (
        "if ( state->completionPending ) {",
        "const idStr decided = ui_arenaDecidedResult.GetString();",
        'const idCmdArgs args( va( "arenaComplete %s", decided.c_str() ), false );',
        "!ArenaParseCompletion( args, token, outcome, playerScore, opponentScore, awards )",
        "AcceptCompletion( token, outcome, playerScore, opponentScore, awards );",
    ), "TakeDecidedResult")
    for refusal in ("outcome == ARENA_OUTCOME_ABORTED", "!state->botsPopulated",
                    "token != ui_arenaActiveMatch.GetInteger()", "ArenaMatchForToken( state, token ) == NULL"):
        require(take, refusal, "an aborted, unpopulated or stale result is refused")

    finish_decided = body(arena, "bool idArenaCampaign::FinishDecidedResult()")
    require_order(finish_decided, ("if ( !TakeDecidedResult() ) {", "FinishPendingCompletion();"),
                  "leaving records and reports the result")
    record_decided = body(arena, "void idArenaCampaign::RecordDecidedResult()")
    require(record_decided, "if ( TakeDecidedResult() && RecordPendingCompletion() ) {",
            "quitting records the result")

    record = body(arena, "bool idArenaCampaign::RecordPendingCompletion()")
    for teardown in ("sessLocal.", "OpenBrowser", "StartWipe", "AbortMatch", "ArenaRestoreSavedCVars"):
        forbid(record, teardown, "recording tears nothing down")
    require(record, "ui_arenaProgress.SetInteger( progress );", "recording keeps the progression")
    require(record, "ui_arenaBest.SetString( ArenaWriteBestResults( bestResults ) );", "recording keeps the best result")
    finish = body(arena, "void idArenaCampaign::FinishPendingCompletion()")
    require_order(finish, ("if ( !RecordPendingCompletion() ) {", "ReturnToBrowserAfterStartFailure( ARENA_FAILURE_RESULT );",
                           "OpenBrowser();"), "the normal return records before it tears down")

    queue = body(arena, "bool idArenaCampaign::QueueCompletion( const idCmdArgs &args )")
    require(queue, "ArenaParseCompletion( args, token, outcome, playerScore, opponentScore, awards )",
            "the handoff and the decided result share one parser")
    require(queue, "AcceptCompletion( token, outcome, playerScore, opponentScore, awards );",
            "the handoff and the decided result share one acceptance")


def validate_callers(sources: dict[str, str]) -> None:
    disconnect = body(sources["session"], "static void Session_Disconnect_f( const idCmdArgs &args )")
    require_order(disconnect, ("if ( arenaCampaign.FinishDecidedResult() ) {", "return;",
                               "const int arenaToken = arenaCampaign.ActiveMatchToken();", "arenaCampaign.AbortMatch();",
                               "arenaCampaign.ReportAbandoned( arenaToken );"),
                  "a disconnect keeps a decided result before abandoning the match")
    common = sources["common"]
    quit_block = common[common.index("// Arena temporarily overrides archived multiplayer and user settings."):]
    quit_block = quit_block[:quit_block.index("Shutdown();")]
    require_order(quit_block, ("arenaCampaign.RecordDecidedResult();", "arenaCampaign.AbortMatch();", "WriteConfiguration();"),
                  "a quit records the decided result, then restores and writes the configuration")
    drop_block = common[common.index("// A recoverable drop can bypass the ordinary disconnect command."):]
    drop_block = drop_block[:drop_block.index("session->Stop();")]
    require_order(drop_block, ("arenaCampaign.RecordDecidedResult();", "arenaCampaign.AbortMatch();"),
                  "an error drop records the decided result before restoring")


def validate(sources: dict[str, str]) -> None:
    validate_game(sources)
    validate_framework(sources)
    validate_callers(sources)


def expect_failure(sources: dict[str, str], label: str) -> None:
    try:
        validate(sources)
    except AssertionError:
        return
    raise AssertionError(f"control did not fail: {label}")


CONTROLS = (
    ("game_mp never publishes the decided result", "mp",
     "\tUpdateArenaCampaignResult();\n\tPublishArenaCampaignDecidedResult();\n", "\tUpdateArenaCampaignResult();\n"),
    ("an aborted match publishes a result", "mp",
     "if ( arenaResultPending && !arenaResultReported && arenaResultOutcome != ARENA_RESULT_ABORTED ) {",
     "if ( arenaResultPending && !arenaResultReported ) {"),
    ("the console can write the decided result", "arena",
     '"ui_arenaDecidedResult", "", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ROM,', '"ui_arenaDecidedResult", "", CVAR_SYSTEM | CVAR_NOCHEAT,'),
    ("a stale token is accepted", "arena",
     "\t\t token != ui_arenaActiveMatch.GetInteger() || ArenaMatchForToken( state, token ) == NULL ) {\n\t\tcommon->Warning( \"arena campaign: ignored decided result",
     "\t\t ArenaMatchForToken( state, token ) == NULL ) {\n\t\tcommon->Warning( \"arena campaign: ignored decided result"),
    ("an aborted outcome is recorded", "arena",
     "\t\t outcome == ARENA_OUTCOME_ABORTED || !state->botsPopulated ||", "\t\t !state->botsPopulated ||"),
    ("a disconnect abandons a decided match", "session",
     "\tif ( arenaCampaign.FinishDecidedResult() ) {\n\t\treturn;\n\t}\n", ""),
    ("a quit drops the decided result", "common",
     "\t\t\tarenaCampaign.RecordDecidedResult();\n\t\t\tarenaCampaign.AbortMatch();\n\t\t\tWriteConfiguration();",
     "\t\t\tarenaCampaign.AbortMatch();\n\t\t\tWriteConfiguration();"),
    ("the quit writes the configuration before recording", "common",
     "\t\t\tarenaCampaign.RecordDecidedResult();\n\t\t\tarenaCampaign.AbortMatch();\n\t\t\tWriteConfiguration();",
     "\t\t\tWriteConfiguration();\n\t\t\tarenaCampaign.RecordDecidedResult();\n\t\t\tarenaCampaign.AbortMatch();"),
    ("recording opens the browser", "arena",
     "\tui_arenaSelectedMatch.SetInteger( match );\n\treturn true;\n}",
     "\tui_arenaSelectedMatch.SetInteger( match );\n\tOpenBrowser();\n\treturn true;\n}"),
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
        print(f"arena_decided_result: FAILED - {error}")
        return 1
    print(f"arena_decided_result: ok ({len(CONTROLS)} single-reversal controls fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
