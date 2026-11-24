#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static inventory for typed participant/series Match Control actions."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_H = ROOT / "src/game/mp/match/MatchProtocol.h"
PROTOCOL_CPP = ROOT / "src/game/mp/match/MatchProtocol.cpp"
OPERATIONS_H = ROOT / "src/game/mp/match/MatchOperations.h"
OPERATIONS_CPP = ROOT / "src/game/mp/match/MatchOperations.cpp"
MODEL_H = ROOT / "src/game/mp/match/MatchControlModel.h"
MODEL_CPP = ROOT / "src/game/mp/match/MatchControlModel.cpp"
OPERATIONS_TEST = ROOT / "tools/tests/mp_match_operations_contract.py"
MODEL_TEST = ROOT / "tools/tests/mp_match_control_model_contract.py"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def main() -> None:
    protocol_h = read(PROTOCOL_H)
    protocol_cpp = read(PROTOCOL_CPP)
    operations_h = read(OPERATIONS_H)
    operations_cpp = read(OPERATIONS_CPP)
    model_h = read(MODEL_H)
    model_cpp = read(MODEL_CPP)
    operations_test = read(OPERATIONS_TEST)
    model_test = read(MODEL_TEST)
    combined = protocol_h + protocol_cpp + operations_h + operations_cpp + model_h + model_cpp

    for token in (
        "MP_MATCH_OP_PARTICIPANT_REMOVE = 35",
        "MP_MATCH_OP_SERIES_CONTESTANT_BIND = 36",
        "MP_MATCH_OP_COUNT = 37",
        "MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE = ( 1u << 24 )",
        "MP_MATCH_ARG_COMPETITION_SIDE = 18",
        "MP_MATCH_COMPETITION_SIDE_A = 1",
        "MP_MATCH_COMPETITION_SIDE_B = 2",
        'MP_MATCH_OP_PARTICIPANT_REMOVE, "participant_remove"',
        'MP_MATCH_OP_SERIES_CONTESTANT_BIND, "series_contestant_bind"',
        "MP_MATCH_OPERATION_FLAG_PROPOSABLE |",
        "MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET",
        "ARG_COMPETITION_SIDE, 1",
    ):
        require(protocol_h + protocol_cpp, token, "append-only protocol action schema")

    continuation = re.search(
        r"typedef struct mpOperationAdapterContinuation_s \{(?P<body>.*?)\n\} "
        r"mpOperationAdapterContinuation_t;",
        operations_h,
        re.DOTALL,
    )
    if continuation is None:
        raise AssertionError("could not find operation continuation schema")
    if re.search(r"\bslot\b", continuation.group("body"), re.IGNORECASE):
        raise AssertionError("adapter continuation exposes a connection slot")

    for token in (
        "MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE",
        "MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND",
        "result.continuation.participant = principal.target",
        "principal.target == principal.actor",
        "MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR )",
        "MPOperationMapProtocolCompetitionSide",
        "context.ruleGameType != GAME_DUEL",
        "series.GetConfiguration().gameType != GAME_DUEL",
        "phase == WARMUP || phase == NEXTGAME",
        "phase != GAMEREVIEW && !recoveredReview",
        "candidate.GetState() == MP_SERIES_READY",
        "candidate.GetState() != MP_SERIES_COMPLETE",
    ):
        require(operations_h + operations_cpp, token, "authoritative typed operation core")
    if re.search(r"continuation\s*\.\s*\w*slot", operations_cpp, re.IGNORECASE):
        raise AssertionError("operation result writes a transport slot")

    for token in (
        "MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE",
        "MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND",
        "MP_MATCH_CONTROL_PROPOSAL_TARGET_PARTICIPANT",
        "candidate.participantTarget = selectedTeam->participantId",
        "selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT",
        "OperationContextAccepted( MP_MATCH_OP_SERIES_ADVANCE )",
        "OperationContextAccepted( MP_MATCH_OP_SERIES_CONTESTANT_BIND )",
        "MP_MATCH_ARG_COMPETITION_SIDE",
        "MP_MATCH_COMPETITION_SIDE_A : MP_MATCH_COMPETITION_SIDE_B",
        "request = candidate",
    ):
        require(model_h + model_cpp, token, "transactional Match Control model")

    # The aggregate runs these native contracts separately. Their source-level
    # markers keep the hostile coverage discoverable from this focused inventory.
    for token in (
        "for ( unsigned int raw = 0; raw < 256; ++raw )",
        "MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND",
        "MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE",
        "session.UnbindParticipant( 2, removedGeneration",
        "replacementOccupant",
        "MP_OPERATION_REASON_TARGET_UNKNOWN",
        "MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP",
        "completeSeries.GetState() == MP_SERIES_COMPLETE",
    ):
        require(operations_test, token, "hostile native operation contract")
    for token in (
        "memcmp(&guarded, &guardedBefore, sizeof(guarded)) == 0",
        "MP_MATCH_VIEW_SERIES_MAP_COMPLETE",
        "request.participantTarget == 102",
        "MP_MATCH_COMPETITION_SIDE_B",
    ):
        require(model_test, token, "hostile native client-model contract")

    for forbidden in (
        "consoleCommand",
        "BufferCommandText",
        "ExecuteCommandText",
        "idUserInterface",
    ):
        if forbidden in combined:
            raise AssertionError(f"typed action core contains forbidden adapter dependency {forbidden!r}")

    print("mp_match_control_actions_contract: PASS")


if __name__ == "__main__":
    main()
