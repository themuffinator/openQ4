#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static transaction-order contract for the legacy round gameplay adapter."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/game/mp/RoundGameState.cpp"
HEADER = ROOT / "src/game/mp/RoundGameState.h"


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def bounded(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8", errors="strict")
    header = HEADER.read_text(encoding="utf-8", errors="strict")
    require(header, "bool\t\t\tSetRoundState", "round transition result API")
    require(header, "bool\t\t\tScheduleNextRound", "round scheduling result API")

    schedule = bounded(
        source,
        "bool rvRoundGameState::ScheduleNextRound",
        "bool rvRoundGameState::SetRoundState",
    )
    commit = schedule.index("SetRoundState( RS_COUNTDOWN )")
    for mutation in (
        "PrepareNextRound()",
        "ResetRound()",
        "roundNumber++",
        "roundWinner = TEAM_NONE",
    ):
        if schedule.index(mutation) < commit:
            raise AssertionError(f"{mutation} occurs before authoritative round commit")

    run = bounded(
        source,
        "void rvRoundGameState::Run",
        "bool rvRoundGameState::NewState",
    )
    active = run.index("if ( SetRoundState( RS_ACTIVE ) )")
    if run.index("RoundBegin()", active) < active:
        raise AssertionError("round-begin side effects precede authoritative commit")
    complete = run.index("if ( SetRoundState( RS_COMPLETE ) )")
    for mutation in ("AwardRound( winningTeam )", "RoundEnd( winningTeam )"):
        if run.index(mutation, complete) < complete:
            raise AssertionError(f"{mutation} precedes authoritative result commit")
    if run.count("if ( SetRoundState( RS_COMPLETE ) )") != 2:
        raise AssertionError("normal and timeout round completion are not both guarded")

    print("mp_match_round_adapter_contract: PASS")


if __name__ == "__main__":
    main()
