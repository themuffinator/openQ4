#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Guard connection-scoped, explicit Duel series recovery bindings."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="strict")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated {signature}")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def main() -> None:
    multiplayer = read("src/game/MultiplayerGame.cpp")
    header = read("src/game/MultiplayerGame.h")
    commands = read("src/game/gamesys/SysCmds.cpp")

    frame = body(multiplayer, "void idMultiplayerGame::BeginCompetitiveFrame")
    require(
        frame,
        "matchSeriesNeedsBindingRecovery && gameLocal.IsTeamGame()",
        "side-owned automatic recovery",
    )
    reject(
        frame,
        "deterministic initial\n\t// contestant ordering",
        "Duel slot-order identity inference",
    )

    resolve = body(multiplayer, "int idMultiplayerGame::ResolveCompetitionSide")
    require(resolve, "matchSeriesCompetitionConnection[ slot ] == matchConnectionId[ slot ]", "connection lifetime binding")
    require(resolve, "matchSeriesGameSideForCompetition[ competitionSide ]", "team-side recovery")

    bind = body(multiplayer, "bool idMultiplayerGame::BindCompetitionSeriesContestant")
    for token in (
        "gameLocal.gameType != GAME_DUEL",
        "matchSession.GetPhase() != WARMUP",
        "static_cast<idPlayer *>( entity )->IsFakeClient()",
        "matchConnectionId[ clientNum ] == 0",
        "participantState->human",
        "participantState->active",
        "matchSeriesCompetitionConnection[ clientNum ] =",
        "matchConnectionId[ clientNum ]",
        "++matchControlRevision",
        "++matchViewRevision",
        "SendChangedMatchViews( true )",
    ):
        require(bind, token, "explicit Duel recovery binding")
    reject(bind, "ui_name", "name-derived identity")
    reject(bind, "IP", "address-derived identity")
    reject(bind, "guid", "GUID-derived identity")
    reject(bind, "PersistCompetitionSeries()", "persisted transient identity")

    command = body(multiplayer, "void idMultiplayerGame::SeriesBind_f")
    require(command, "ParseBoundedVoteInteger", "strict slot parser")
    for token in (
        "GetSlotGeneration( clientNum, generation )",
        "ResolveSlotBinding( clientNum, generation,",
        "request.opcode = MP_MATCH_OP_SERIES_CONTESTANT_BIND",
        "request.participantTarget = participant.SequencePart()",
        "MP_MATCH_ARG_COMPETITION_SIDE",
        "SubmitMatchOperation( request )",
        "ExecuteTrustedLocalMatchOperation( request, execution )",
    ):
        require(command, token, "typed trusted-local binding entry")
    reject(
        command,
        "BindCompetitionSeriesContestant(",
        "console bypass of typed binding authorization",
    )
    require(header, "SeriesBind_f", "command declaration")
    require(commands, 'AddCommand( "matchSeriesBind"', "command registration")

    print("mp_match_series_binding_contract: PASS")


if __name__ == "__main__":
    main()
