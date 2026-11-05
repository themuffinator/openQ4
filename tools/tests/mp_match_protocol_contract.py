#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contracts for the bounded competitive match-operation protocol."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/mpgame/mp/match/MatchProtocol.h"
SOURCE = ROOT / "src/mpgame/mp/match/MatchProtocol.cpp"
SESSION_HEADER = ROOT / "src/mpgame/mp/match/MatchSession.h"
SESSION_SOURCE = ROOT / "src/mpgame/mp/match/MatchSession.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def enum_body(header: str, type_name: str) -> str:
    match = re.search(
        rf"typedef enum \{{(?P<body>.*?)\}}\s*{re.escape(type_name)}\s*;",
        header,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"could not find enum {type_name}")
    return match.group("body")


def global_match_symbols(text: str) -> set[str]:
    return set(re.findall(r"\b(?:MP_MATCH_[A-Z0-9_]+|mpMatch[A-Za-z0-9_]+)\b", text))


def stable_opcode_contract(header: str, source: str) -> None:
    body = enum_body(header, "mpMatchOperationOpcode_t")
    assignments = {
        name: int(value)
        for name, value in re.findall(r"\b(MP_MATCH_OP_[A-Z0-9_]+)\s*=\s*(\d+)\b", body)
    }
    if assignments.get("MP_MATCH_OP_INVALID") != 0:
        raise AssertionError("invalid opcode must remain zero")
    if assignments.get("MP_MATCH_OP_COUNT") != 37:
        raise AssertionError("opcode count changed without a protocol migration")

    operation_values = {
        name: value
        for name, value in assignments.items()
        if name not in {"MP_MATCH_OP_INVALID", "MP_MATCH_OP_COUNT"}
    }
    expected_values = list(range(1, assignments["MP_MATCH_OP_COUNT"]))
    if sorted(operation_values.values()) != expected_values:
        raise AssertionError("operation opcodes must be unique and contiguous append-only values")

    required_categories = {
        "MP_MATCH_OP_READY_SET",
        "MP_MATCH_OP_TEAM_READY_SET",
        "MP_MATCH_OP_QUEUE_JOIN",
        "MP_MATCH_OP_TIMEOUT_REQUEST",
        "MP_MATCH_OP_TECH_PAUSE_REQUEST",
        "MP_MATCH_OP_RESUME_REQUEST",
        "MP_MATCH_OP_REF_AUTHENTICATE",
        "MP_MATCH_OP_REF_LOGOUT",
        "MP_MATCH_OP_RULES_STAGE_FIELD",
        "MP_MATCH_OP_RULES_COMMIT",
        "MP_MATCH_OP_PROPOSAL_CREATE",
        "MP_MATCH_OP_PROPOSAL_CAST",
        "MP_MATCH_OP_PROPOSAL_CANCEL",
        "MP_MATCH_OP_ROSTER_INVITE",
        "MP_MATCH_OP_ROSTER_SUBSTITUTE",
        "MP_MATCH_OP_SERIES_STAGE_PROFILE",
        "MP_MATCH_OP_VETO_SELECT",
        "MP_MATCH_OP_FORFEIT",
        "MP_MATCH_OP_ABORT",
		"MP_MATCH_OP_BROADCASTER_SET",
		"MP_MATCH_OP_ROSTER_LEAVE",
        "MP_MATCH_OP_PARTICIPANT_REMOVE",
        "MP_MATCH_OP_SERIES_CONTESTANT_BIND",
    }
    missing = required_categories - operation_values.keys()
    if missing:
        raise AssertionError(f"operation registry is missing categories: {sorted(missing)}")

    table_match = re.search(
        r"static const mpMatchOperationDescriptor_t OPERATION_DESCRIPTORS\[\]\s*=\s*\{"
        r"(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if table_match is None:
        raise AssertionError("could not find operation descriptor registry")
    table_body = table_match.group("body")
    rows = re.findall(r"\{\s*(MP_MATCH_OP_[A-Z0-9_]+)\s*,", table_body)
    if len(rows) != len(operation_values):
        raise AssertionError(
            f"descriptor registry has {len(rows)} rows for {len(operation_values)} opcodes"
        )
    if set(rows) != set(operation_values) or any(rows.count(opcode) != 1 for opcode in rows):
        raise AssertionError("every stable opcode must have exactly one descriptor")

    tokens = re.findall(r'\{\s*MP_MATCH_OP_[A-Z0-9_]+\s*,\s*"([^"]+)"', table_body)
    if len(tokens) != len(rows) or len(tokens) != len(set(tokens)):
        raise AssertionError("descriptor machine tokens must be present and unique")
    invalid_tokens = [token for token in tokens if re.fullmatch(r"[a-z0-9_]{1,48}", token) is None]
    if invalid_tokens:
        raise AssertionError(f"descriptor tokens are not canonical: {invalid_tokens}")


def descriptor_row(source: str, opcode: str) -> str:
    rows = re.findall(
        rf"\{{\s*{re.escape(opcode)}\s*,(?P<body>.*?)\}},",
        source,
        re.DOTALL,
    )
    if len(rows) != 1:
        raise AssertionError(f"{opcode} must have exactly one descriptor row")
    return rows[0]


def timeout_window_phase_contract(source: str, session_source: str) -> None:
    live = re.search(r"static const mpMatchPhaseMask_t PHASE_LIVE\s*=\s*(?P<mask>[^;]+);", source)
    if live is None:
        raise AssertionError("could not find the live phase mask")
    live_phases = set(re.findall(r"MP_MATCH_PHASE_[A-Z]+", live.group("mask")))
    if live_phases != {
        "MP_MATCH_PHASE_COUNTDOWN",
        "MP_MATCH_PHASE_GAMEON",
        "MP_MATCH_PHASE_SUDDENDEATH",
    }:
        raise AssertionError(f"the live phase mask drifted: {sorted(live_phases)}")

    # The timeout_request_window rule may open a countdown timeout, and the
    # session (mpMatchSession::RequestTeamTimeout) is what enforces the window.
    # The descriptor phase gate runs first, so a descriptor which excludes
    # COUNTDOWN would reject a legally configured countdown timeout before the
    # session ever sees it.
    for opcode in ("MP_MATCH_OP_READY_SET", "MP_MATCH_OP_TEAM_READY_SET"):
        ready_phases = set(re.findall(r"MP_MATCH_PHASE_[A-Z]+", descriptor_row(source, opcode)))
        if ready_phases != {"MP_MATCH_PHASE_WARMUP", "MP_MATCH_PHASE_COUNTDOWN"}:
            raise AssertionError(f"{opcode} must allow readiness withdrawal during countdown")
    row = descriptor_row(source, "MP_MATCH_OP_TIMEOUT_REQUEST")
    phases = set(re.findall(r"MP_MATCH_PHASE_[A-Z]+", row))
    if "PHASE_LIVE" in row:
        phases |= live_phases
    for required in (
        "MP_MATCH_PHASE_COUNTDOWN",
        "MP_MATCH_PHASE_GAMEON",
        "MP_MATCH_PHASE_SUDDENDEATH",
    ):
        if required not in phases:
            raise AssertionError(
                f"timeout_request must remain legal in {required} at the protocol gate"
            )
    if "MP_MATCH_PHASE_WARMUP" in phases:
        raise AssertionError("timeout_request must not become legal in warmup")

    require(
        session_source,
        "if ( !IsLivePhase() && !( phase == COUNTDOWN && timeoutAllowedDuringCountdown ) )",
        "session-owned countdown timeout window",
    )


def dependency_and_namespace_contract(header: str, source: str, session: str) -> None:
    require(header, '#include "../MatchPhase.h"', "protocol phase dependency")
    for forbidden in ("MatchSession.h", "Game_local.h", "MultiplayerGame.h"):
        if forbidden in header:
            raise AssertionError(f"protocol header contains game/session dependency {forbidden!r}")
    require(source, "static_assert( MAX_CLIENTS <= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS",
            "platform slot-capacity guard")
    for phase, value in (
        ("INACTIVE", 0),
        ("WARMUP", 1),
        ("COUNTDOWN", 2),
        ("GAMEON", 3),
        ("SUDDENDEATH", 4),
        ("GAMEREVIEW", 5),
        ("NEXTGAME", 6),
        ("STATE_COUNT", 7),
    ):
        require(source, f"static_assert( {phase} == {value}", "canonical phase ordinals")

    protocol_symbols = global_match_symbols(header)
    session_symbols = global_match_symbols(session)
    collisions = sorted(protocol_symbols & session_symbols)
    if collisions:
        raise AssertionError(
            "protocol/session globals require explicit adapter names; collisions="
            + repr(collisions)
        )
    for token in (
        "mpMatchProtocolCapabilityMask_t",
        "MP_MATCH_PROTOCOL_CAP_ALL",
        "mpMatchProtocolRosterRole_t",
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER",
        "MP_MATCH_PROTOCOL_REASON_STALE_REVISION",
    ):
        require(header, token, "protocol-specific adapter namespace")


def bounded_schema_contract(header: str, source: str) -> None:
    for token in (
        "MP_MATCH_PROTOCOL_MAGIC = 0x514d",
        "MP_MATCH_PROTOCOL_SCHEMA_VERSION = 2",
        "MP_MATCH_OPERATION_REGISTRY_VERSION = 5",
        "MP_MATCH_PROTOCOL_MAX_MESSAGE_BYTES = 1024",
        "MP_MATCH_PROTOCOL_MAX_TOP_LEVEL_FIELDS = 16",
        "MP_MATCH_PROTOCOL_MAX_ARGUMENTS = 8",
        "MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS = 4",
        "MP_MATCH_PROTOCOL_MAX_STRING_BYTES = 96",
        "MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS = 32",
        "MP_MATCH_NESTED_ARGUMENT_BASE = 64",
        "typedef unsigned long long mpMatchProtocolRevision_t",
        "typedef unsigned long long mpMatchProtocolSessionId_t",
    ):
        require(header, token, "bounded protocol constants")

    for token in (
        "expectedSessionRevision",
        "expectedControlRevision",
        "sessionId",
        "actorSlot",
        "actorBindingGeneration",
        "hasParticipantTarget",
        "participantTarget",
        "hasTeamTarget",
        "teamTarget",
        "mpMatchOperationRequest_t",
        "mpMatchOperationResult_t",
        "mpMatchProtocolEnvelope_t",
        "mpMatchProtocolError_t",
        "mpMatchOperationDescriptor_t",
        "MP_MATCH_VALUE_ANY_SCALAR",
        "MP_MATCH_OPERATION_FLAG_PROPOSABLE",
        "MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS",
        "MP_MATCH_LOCALIZATION_REASON_BASE",
    ):
        require(header, token, "typed request/result schema")

    require(source, "ValidateRegistryInternal", "descriptor registry validator")
    require(source, "ValidateRequestArguments", "descriptor-driven argument validation")
    require(source, "ValidateTargetPolicy", "typed target validation")
    require(source, "WriteUInt64Value", "full-width session/revision encoding")
    require(source, "ReadUInt64Value", "full-width revision decoding")

    require(source, "REQUEST_FIELD_CONTROL_REVISION", "aggregate control CAS field")
    require(source, "1u << REQUEST_FIELD_CONTROL_REVISION",
            "required aggregate control CAS field")
    require(source, "static_cast<mpMatchProtocolSessionId_t>( sessionHigh ) << 32",
            "full-width envelope session decoding")
    require(source, "MP_MATCH_PROTOCOL_REASON_NOT_PROPOSABLE", "proposal eligibility rejection")
    # Forfeit is the one side-ballot operation: proposable, with the required
    # team target that becomes the ballot scope.
    forfeit = re.search(
        r'\{\s*MP_MATCH_OP_FORFEIT,\s*"forfeit".*?\}', source, re.DOTALL)
    if forfeit is None or not re.search(
            r'MP_MATCH_OPERATION_FLAG_PROPOSABLE\s*\|\s*'
            r'MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET\s*\|\s*'
            r'MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET', forfeit.group(0)):
        raise AssertionError(
            "forfeit must stay proposable with a required team target (side ballot)")
    require(source, "targetDescriptor->arguments", "nested proposal argument validation")


def veto_side_contract(header: str, source: str) -> None:
    argument_body = enum_body(header, "mpMatchArgumentField_t")
    argument_values = {
        name: int(value)
        for name, value in re.findall(
            r"\b(MP_MATCH_ARG_[A-Z0-9_]+)\s*=\s*(\d+)\b", argument_body
        )
    }
    if argument_values.get("MP_MATCH_ARG_CREDENTIAL") != 16:
        raise AssertionError("the prior credential field id changed")
    if argument_values.get("MP_MATCH_ARG_STARTING_SIDE") != 17:
        raise AssertionError("starting-side field must be appended at id 17")
    if argument_values.get("MP_MATCH_ARG_COMPETITION_SIDE") != 18:
        raise AssertionError("competition-side field must be appended at id 18")

    veto_body = enum_body(header, "mpMatchVetoAction_t")
    veto_values = {
        name: int(value)
        for name, value in re.findall(
            r"\b(MP_MATCH_VETO_[A-Z0-9_]+)\s*=\s*(\d+)\b", veto_body
        )
    }
    expected = {
        "MP_MATCH_VETO_BAN": 1,
        "MP_MATCH_VETO_PICK": 2,
        "MP_MATCH_VETO_DECIDER": 3,
        "MP_MATCH_VETO_SIDE": 4,
    }
    if veto_values != expected:
        raise AssertionError(f"veto values are not append-only: {veto_values!r}")

    for token in (
        "MP_MATCH_STARTING_SIDE_MARINE = 1",
        "MP_MATCH_STARTING_SIDE_STROGG = 2",
        "MP_MATCH_ARG_STARTING_SIDE, MP_MATCH_VALUE_ENUM, false",
        "MP_MATCH_STARTING_SIDE_MARINE, MP_MATCH_STARTING_SIDE_STROGG",
        "ValidateVetoArgumentShape",
        "requiresStartingSide != ( startingSide != 0 )",
        "MP_MATCH_ARG_MAP_TOKEN, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 63",
        "MP_MATCH_COMPETITION_SIDE_A = 1",
        "MP_MATCH_COMPETITION_SIDE_B = 2",
        "MP_MATCH_ARG_COMPETITION_SIDE, MP_MATCH_VALUE_ENUM, true",
        "MP_MATCH_COMPETITION_SIDE_A, MP_MATCH_COMPETITION_SIDE_B",
    ):
        require(header + source, token, "strict starting-side veto schema")


def fail_closed_codec_contract(header: str, source: str) -> None:
    combined = header + source
    forbidden = (
        "cmdSystem",
        "BufferCommandText",
        "AppendCommandText",
        "ExecuteCommandText",
        "WriteString(",
        "ReadString(",
        "idList<",
        "idStr ",
        "new mpMatch",
        "fileSystem->",
        "rcon",
    )
    for token in forbidden:
        if token in combined:
            raise AssertionError(f"protocol contains forbidden command/dynamic dependency {token!r}")

    for token in (
        "SaveReadState",
        "RestoreReadState",
        "SaveWriteState",
        "RestoreWriteState",
        "GetRemainingReadBits",
        "GetRemainingWriteBits",
        "MP_MATCH_TRAILING_REJECT",
        "MP_MATCH_TRAILING_ALLOW",
        "OPTIONAL_EXTENSION_BIT",
        "CheckAndMarkField",
        "MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD",
        "MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD",
        "MP_MATCH_PROTOCOL_REASON_TRAILING_DATA",
        "MP_MATCH_PROTOCOL_REASON_TRUNCATED",
        "MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE",
        "MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS",
        "mpMatchOperationRequest_t decoded",
        "mpMatchOperationResult_t decoded",
        "request = decoded",
        "result = decoded",
    ):
        require(source, token, "transactional bounded codec")

    # Encoding always writes descriptor arguments in field-id order, so two
    # equivalent typed requests produce the same bytes regardless of caller order.
    require(source, "ordered[ insertion - 1 ]->fieldId > candidate->fieldId",
            "canonical argument ordering")
    require(source, "message.GetWriteBit() != 0", "byte-aligned write contract")
    require(source, "message.GetReadBit() != 0", "byte-aligned read contract")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    session = read(SESSION_HEADER) if SESSION_HEADER.is_file() else ""
    session_source = read(SESSION_SOURCE)
    stable_opcode_contract(header, source)
    timeout_window_phase_contract(source, session_source)
    dependency_and_namespace_contract(header, source, session)
    bounded_schema_contract(header, source)
    veto_side_contract(header, source)
    fail_closed_codec_contract(header, source)
    print("mp_match_protocol_contract: PASS")


if __name__ == "__main__":
    main()
