#!/usr/bin/env python3
"""Static and native contracts for the closed Match Control localization bridge."""

from __future__ import annotations

from collections import Counter
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()
MATCH_ROOT = GAME_ROOT / "src/mpgame/mp/match"
HEADER = MATCH_ROOT / "MatchControlLocalization.h"
SOURCE = MATCH_ROOT / "MatchControlLocalization.cpp"
PROJECTION_SOURCE = MATCH_ROOT / "MatchControlProjection.cpp"
MULTIPLAYER_SOURCE = GAME_ROOT / "src/mpgame/MultiplayerGame.cpp"
PROTOCOL_HEADER = MATCH_ROOT / "MatchProtocol.h"
STRINGS_ROOT = ROOT / "content/baseoq4/pak0/strings"
MATCH_CONTROL_GUI = ROOT / "content/baseoq4/pak0/guis/matchcontrol.gui"
LANGUAGE_FILES = (
    "english_openq4.lang",
    "french_openq4.lang",
    "italian_openq4.lang",
    "spanish_openq4.lang",
)
ENTRY_RE = re.compile(r'^\s*"(?P<id>#str_\d+)"\s+"(?P<value>.*)"\s*$')
FIXED_RETURN_RE = re.compile(r"\breturn\s+([^;]+);")


def keyed(names: tuple[str, ...], first: int, step: int = 1) -> tuple[tuple[str, str], ...]:
    return tuple((name, f"#str_{first + index * step}") for index, name in enumerate(names))


LOCALIZATION_OPERATIONS = keyed(
    (
        "MP_MATCH_LOCALIZATION_OPERATION_READY_SET",
        "MP_MATCH_LOCALIZATION_OPERATION_TEAM_READY_SET",
        "MP_MATCH_LOCALIZATION_OPERATION_FORCE_READY",
        "MP_MATCH_LOCALIZATION_OPERATION_TEAM_JOIN",
        "MP_MATCH_LOCALIZATION_OPERATION_TEAM_LOCK_SET",
        "MP_MATCH_LOCALIZATION_OPERATION_QUEUE_JOIN",
        "MP_MATCH_LOCALIZATION_OPERATION_QUEUE_DEFER",
        "MP_MATCH_LOCALIZATION_OPERATION_QUEUE_LEAVE",
        "MP_MATCH_LOCALIZATION_OPERATION_TIMEOUT_REQUEST",
        "MP_MATCH_LOCALIZATION_OPERATION_TECH_PAUSE_REQUEST",
        "MP_MATCH_LOCALIZATION_OPERATION_RESUME_REQUEST",
        "MP_MATCH_LOCALIZATION_OPERATION_REF_AUTHENTICATE",
        "MP_MATCH_LOCALIZATION_OPERATION_REF_LOGOUT",
        "MP_MATCH_LOCALIZATION_OPERATION_RULES_SELECT_PROFILE",
        "MP_MATCH_LOCALIZATION_OPERATION_RULES_STAGE_FIELD",
        "MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT",
        "MP_MATCH_LOCALIZATION_OPERATION_RULES_DISCARD",
        "MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CREATE",
        "MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CAST",
        "MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CANCEL",
        "MP_MATCH_LOCALIZATION_OPERATION_ROSTER_INVITE",
        "MP_MATCH_LOCALIZATION_OPERATION_ROSTER_ACCEPT",
        "MP_MATCH_LOCALIZATION_OPERATION_ROSTER_REMOVE",
        "MP_MATCH_LOCALIZATION_OPERATION_ROSTER_SUBSTITUTE",
        "MP_MATCH_LOCALIZATION_OPERATION_ROLE_ASSIGN",
        "MP_MATCH_LOCALIZATION_OPERATION_SERIES_STAGE_PROFILE",
        "MP_MATCH_LOCALIZATION_OPERATION_SERIES_START",
        "MP_MATCH_LOCALIZATION_OPERATION_SERIES_CANCEL",
        "MP_MATCH_LOCALIZATION_OPERATION_SERIES_ADVANCE",
        "MP_MATCH_LOCALIZATION_OPERATION_VETO_SELECT",
        "MP_MATCH_LOCALIZATION_OPERATION_FORFEIT",
        "MP_MATCH_LOCALIZATION_OPERATION_ABORT",
        "MP_MATCH_LOCALIZATION_OPERATION_BROADCASTER_SET",
        "MP_MATCH_LOCALIZATION_OPERATION_ROSTER_LEAVE",
        "MP_MATCH_LOCALIZATION_OPERATION_PARTICIPANT_REMOVE",
        "MP_MATCH_LOCALIZATION_OPERATION_SERIES_CONTESTANT_BIND",
    ),
    42310,
)
LOCALIZATION_CONFIRMATIONS = tuple(
    zip(
        (
            "MP_MATCH_LOCALIZATION_CONFIRM_FORCE_READY",
            "MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT",
            "MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_REMOVE",
            "MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_SUBSTITUTE",
            "MP_MATCH_LOCALIZATION_CONFIRM_SERIES_CANCEL",
            "MP_MATCH_LOCALIZATION_CONFIRM_FORFEIT",
            "MP_MATCH_LOCALIZATION_CONFIRM_ABORT",
            "MP_MATCH_LOCALIZATION_CONFIRM_SERIES_START",
            "MP_MATCH_LOCALIZATION_CONFIRM_SERIES_ADVANCE",
            "MP_MATCH_LOCALIZATION_CONFIRM_VETO_SELECT",
            "MP_MATCH_LOCALIZATION_CONFIRM_PARTICIPANT_REMOVE",
        ),
        ("#str_42350", "#str_42351", "#str_42352", "#str_42353",
         "#str_42354", "#str_42355", "#str_42356", "#str_42357",
         "#str_42358", "#str_42359", "#str_42349"),
    )
)
REASON_SUFFIXES = (
    "OK", "UNSUPPORTED_SCHEMA", "UNKNOWN_ENVELOPE", "UNKNOWN_OPCODE",
    "UNKNOWN_FIELD", "TRUNCATED", "PAYLOAD_TOO_LARGE", "BUFFER_TOO_SMALL",
    "ARGUMENT_COUNT", "ARGUMENT_TYPE", "ARGUMENT_RANGE", "STRING_LENGTH",
    "STRING_CHARACTERS", "DUPLICATE_FIELD", "TRAILING_DATA",
    "INVALID_SESSION_ID", "INVALID_REQUEST_ID", "INVALID_ACTOR_SLOT",
    "INVALID_BINDING_GENERATION", "INVALID_PARTICIPANT", "INVALID_TEAM",
    "INVALID_TARGET", "NOT_PROPOSABLE", "REGISTRY_INVALID", "NOT_AUTHORIZED",
    "ILLEGAL_PHASE", "STALE_REVISION", "CONFLICT", "COOLDOWN", "INTERNAL",
    "ALIGNMENT",
)
LOCALIZATION_REASONS = keyed(
    tuple(f"MP_MATCH_LOCALIZATION_REASON_{suffix}" for suffix in REASON_SUFFIXES),
    42360,
)
PROTOCOL_REASONS = keyed(
    tuple(f"MP_MATCH_PROTOCOL_REASON_{suffix}" for suffix in REASON_SUFFIXES),
    42360,
)


FUNCTION_CASES: dict[str, tuple[tuple[str, str], ...]] = {
    "MPMatchControlLocalizationKey": (
        LOCALIZATION_OPERATIONS + LOCALIZATION_CONFIRMATIONS + LOCALIZATION_REASONS
    ),
    "MPMatchControlProtocolReasonKey": PROTOCOL_REASONS,
    "MPMatchControlPhaseKey": keyed(
        ("INACTIVE", "WARMUP", "COUNTDOWN", "GAMEON", "SUDDENDEATH",
         "GAMEREVIEW", "NEXTGAME"), 42400
    ),
    "MPMatchControlRoundKey": keyed(
        ("RS_INACTIVE", "RS_COUNTDOWN", "RS_ACTIVE", "RS_COMPLETE"), 42410
    ),
    "MPMatchControlPauseStateKey": keyed(
        ("MP_MATCH_VIEW_PAUSE_RUNNING", "MP_MATCH_VIEW_PAUSE_PENDING",
         "MP_MATCH_VIEW_PAUSED", "MP_MATCH_VIEW_RESUME_COUNTDOWN"), 42420
    ),
    "MPMatchControlPauseKindKey": keyed(
        ("MP_MATCH_VIEW_PAUSE_KIND_NONE", "MP_MATCH_VIEW_PAUSE_KIND_TEAM_TIMEOUT",
         "MP_MATCH_VIEW_PAUSE_KIND_TECHNICAL"), 42430
    ),
    "MPMatchControlPauseReasonKey": keyed(
        ("MP_MATCH_VIEW_PAUSE_REASON_NONE", "MP_MATCH_VIEW_PAUSE_REASON_TACTICAL",
         "MP_MATCH_VIEW_PAUSE_REASON_PLAYER_DISCONNECT",
         "MP_MATCH_VIEW_PAUSE_REASON_TECHNICAL_FAULT",
         "MP_MATCH_VIEW_PAUSE_REASON_SERVER_FAULT",
         "MP_MATCH_VIEW_PAUSE_REASON_REFEREE"), 42440
    ),
    "MPMatchControlResumePolicyKey": keyed(
        ("MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE",
         "MP_MATCH_VIEW_RESUME_BOTH_SIDES_OR_REFEREE",
         "MP_MATCH_VIEW_RESUME_REFEREE_ONLY"), 42450
    ),
    "MPMatchControlPublicRoleKey": keyed(
        ("MP_MATCH_VIEW_ROLE_NONE", "MP_MATCH_VIEW_ROLE_PLAYER",
         "MP_MATCH_VIEW_ROLE_CAPTAIN", "MP_MATCH_VIEW_ROLE_COACH",
         "MP_MATCH_VIEW_ROLE_BROADCASTER", "MP_MATCH_VIEW_ROLE_REFEREE"), 42460
    ),
    "MPMatchControlRosterRoleKey": keyed(
        ("MP_MATCH_VIEW_ROSTER_PLAYER", "MP_MATCH_VIEW_ROSTER_CAPTAIN",
         "MP_MATCH_VIEW_ROSTER_COACH", "MP_MATCH_VIEW_ROSTER_SUBSTITUTE"), 42470
    ),
    "MPMatchControlProtocolRosterRoleKey": keyed(
        ("MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER",
         "MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN",
         "MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH",
         "MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE"), 42470
    ),
    "MPMatchControlQueueStateKey": keyed(
        ("MP_MATCH_VIEW_QUEUE_NONE", "MP_MATCH_VIEW_QUEUE_WAITING",
         "MP_MATCH_VIEW_QUEUE_DEFERRED", "MP_MATCH_VIEW_QUEUE_ADMITTED"), 42480
    ),
    "MPMatchControlProposalScopeKey": keyed(
        ("MP_MATCH_VIEW_PROPOSAL_GLOBAL", "MP_MATCH_VIEW_PROPOSAL_SIDE"), 42490
    ),
    "MPMatchControlBallotKey": keyed(
        ("MP_MATCH_VIEW_BALLOT_NONE", "MP_MATCH_VIEW_BALLOT_YES",
         "MP_MATCH_VIEW_BALLOT_NO", "MP_MATCH_VIEW_BALLOT_ABSTAIN"), 42500
    ),
    "MPMatchControlProtocolBallotKey": keyed(
        ("MP_MATCH_BALLOT_YES", "MP_MATCH_BALLOT_NO", "MP_MATCH_BALLOT_ABSTAIN"),
        42501
    ),
    "MPMatchControlSeriesStateKey": keyed(
        ("MP_MATCH_VIEW_SERIES_DISABLED", "MP_MATCH_VIEW_SERIES_SETUP",
         "MP_MATCH_VIEW_SERIES_VETO", "MP_MATCH_VIEW_SERIES_READY",
         "MP_MATCH_VIEW_SERIES_MAP_ACTIVE", "MP_MATCH_VIEW_SERIES_MAP_COMPLETE",
         "MP_MATCH_VIEW_SERIES_COMPLETE", "MP_MATCH_VIEW_SERIES_CANCELLED"), 42510
    ),
    "MPMatchControlVetoActionKey": keyed(
        ("MP_MATCH_VIEW_VETO_BAN", "MP_MATCH_VIEW_VETO_PICK",
         "MP_MATCH_VIEW_VETO_SIDE", "MP_MATCH_VIEW_VETO_DECIDER"), 42520
    ),
    "MPMatchControlProtocolVetoActionKey": (
        ("MP_MATCH_VETO_BAN", "#str_42520"),
        ("MP_MATCH_VETO_PICK", "#str_42521"),
        ("MP_MATCH_VETO_SIDE", "#str_42522"),
        ("MP_MATCH_VETO_DECIDER", "#str_42523"),
    ),
    "MPMatchControlMapDispositionKey": keyed(
        ("MP_MATCH_VIEW_MAP_AVAILABLE", "MP_MATCH_VIEW_MAP_BANNED",
         "MP_MATCH_VIEW_MAP_SELECTED"), 42530
    ),
    "MPMatchControlMapOutcomeKey": keyed(
        ("MP_MATCH_VIEW_MAP_UNPLAYED", "MP_MATCH_VIEW_MAP_DECIDED",
         "MP_MATCH_VIEW_MAP_FORFEIT", "MP_MATCH_VIEW_MAP_ABORTED"), 42540
    ),
    "MPMatchControlRuleTypeKey": keyed(
        ("MP_MATCH_VIEW_RULE_BOOL", "MP_MATCH_VIEW_RULE_INTEGER",
         "MP_MATCH_VIEW_RULE_ENUM"), 42550
    ),
    "MPMatchControlRulesBoundaryKey": keyed(
        ("MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT",
         "MP_MATCH_VIEW_RULES_FROZEN_FOR_MAP"), 42560
    ),
    "MPMatchControlEvidenceStateKey": keyed(
        ("MP_MATCH_VIEW_EVIDENCE_DISABLED", "MP_MATCH_VIEW_EVIDENCE_CAPTURING",
         "MP_MATCH_VIEW_EVIDENCE_FINALIZED", "MP_MATCH_VIEW_EVIDENCE_FAILED"), 42570
    ),
    "MPMatchControlMVDStateKey": keyed(
        ("MP_MATCH_VIEW_MVD_DISABLED", "MP_MATCH_VIEW_MVD_PENDING",
         "MP_MATCH_VIEW_MVD_RECORDING", "MP_MATCH_VIEW_MVD_AVAILABLE",
         "MP_MATCH_VIEW_MVD_FAILED"), 42580
    ),
    "MPMatchControlReportStateKey": keyed(
        ("MP_MATCH_VIEW_REPORT_DISABLED", "MP_MATCH_VIEW_REPORT_PENDING",
         "MP_MATCH_VIEW_REPORT_AVAILABLE", "MP_MATCH_VIEW_REPORT_FAILED"), 42590
    ),
    "MPMatchControlEvidenceEventKindKey": keyed(
        ("MP_MATCH_VIEW_EVIDENCE_EVENT_NONE",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_ROUND_TRANSITION",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_PAUSE_TRANSITION",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_ROLE_CHANGE",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_PROPOSAL",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT",
         "MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE"), 42600
    ),
    "MPMatchControlOperationResultStatusKey": keyed(
        ("MP_MATCH_RESULT_REJECTED", "MP_MATCH_RESULT_COMMITTED",
         "MP_MATCH_RESULT_NO_CHANGE", "MP_MATCH_RESULT_PENDING"), 42620
    ),
    "MPMatchControlTeamKey": keyed(
        ("MP_MATCH_TEAM_NONE", "MP_MATCH_TEAM_MARINE", "MP_MATCH_TEAM_STROGG",
         "MP_MATCH_TEAM_SPECTATOR"), 42630
    ),
    "MPMatchControlStartingSideKey": (
        ("MP_MATCH_STARTING_SIDE_MARINE", "#str_42631"),
        ("MP_MATCH_STARTING_SIDE_STROGG", "#str_42632"),
    ),
    "MPMatchControlReadinessBlockerKey": keyed(
        ("MP_MATCH_BLOCKER_RULES_NOT_FROZEN", "MP_MATCH_BLOCKER_RULES_INVALID",
         "MP_MATCH_BLOCKER_MAP_INVALID",
         "MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_HUMANS",
         "MP_MATCH_BLOCKER_ACTIVE_PARTICIPANT_UNASSIGNED",
         "MP_MATCH_BLOCKER_TEAM_OVERSIZED",
         "MP_MATCH_BLOCKER_VACANT_REQUIRED_ROSTER_SEAT",
         "MP_MATCH_BLOCKER_VETO_INCOMPLETE",
         "MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY",
         "MP_MATCH_BLOCKER_TEAM_NOT_READY",
         "MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE"), 42640
    ),
    "MPMatchControlRuleFieldKey": keyed(
        ("MP_RULE_GAME_TYPE", "MP_RULE_MANAGED_MATCH", "MP_RULE_WARMUP_ENABLED",
         "MP_RULE_READINESS_POLICY", "MP_RULE_READY_THRESHOLD_BASIS_POINTS",
         "MP_RULE_BOTS_CAN_READY", "MP_RULE_MIN_ACTIVE_HUMANS",
         "MP_RULE_MIN_TEAM_SIZE", "MP_RULE_REQUIRE_BOTH_TEAMS",
         "MP_RULE_ROSTER_SIZE_PER_TEAM", "MP_RULE_COUNTDOWN_SECONDS",
         "MP_RULE_TIME_LIMIT_MINUTES", "MP_RULE_FRAG_LIMIT",
         "MP_RULE_CAPTURE_LIMIT", "MP_RULE_CONTROL_TIME_SECONDS",
         "MP_RULE_ROUND_LIMIT", "MP_RULE_ROUND_TIME_LIMIT_SECONDS",
         "MP_RULE_ROUND_COUNTDOWN_SECONDS", "MP_RULE_ROUND_REVIEW_SECONDS",
         "MP_RULE_MERCY_LIMIT", "MP_RULE_OVERTIME_POLICY",
         "MP_RULE_OVERTIME_PERIOD_SECONDS", "MP_RULE_OVERTIME_MAX_PERIODS",
         "MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY",
         "MP_RULE_SUDDEN_DEATH_RESPAWN_INCREASE",
         "MP_RULE_SUDDEN_DEATH_RESPAWN_MAX", "MP_RULE_TEAM_DAMAGE",
         "MP_RULE_FORFEIT_ON_EMPTY_TEAM", "MP_RULE_BUYING_ENABLED",
         "MP_RULE_TEAM_TIMEOUT_COUNT", "MP_RULE_TEAM_TIMEOUT_SECONDS",
         "MP_RULE_TIMEOUT_REQUEST_WINDOW", "MP_RULE_TIMEOUT_RESUME_POLICY"),
        41600, 2
    ),
    "MPMatchControlMatchProfileKey": (
        ("MP_MATCH_PROFILE_CUSTOM", "#str_42700"),
        *keyed(("MP_MATCH_PROFILE_CASUAL", "MP_MATCH_PROFILE_COMPETITIVE_DM",
                "MP_MATCH_PROFILE_COMPETITIVE_TOURNEY",
                "MP_MATCH_PROFILE_COMPETITIVE_DUEL",
                "MP_MATCH_PROFILE_COMPETITIVE_TDM",
                "MP_MATCH_PROFILE_COMPETITIVE_CTF",
                "MP_MATCH_PROFILE_COMPETITIVE_DEADZONE",
                "MP_MATCH_PROFILE_COMPETITIVE_ROUND"), 41677, 2),
    ),
    "MPMatchControlSeriesProfileKey": (
        ("MP_SERIES_PROFILE_CUSTOM", "#str_42700"),
        *keyed(("MP_SERIES_PROFILE_BEST_OF_ONE",
                "MP_SERIES_PROFILE_BEST_OF_THREE",
                "MP_SERIES_PROFILE_BEST_OF_FIVE"), 42710),
    ),
    "MPMatchControlErrorReasonKey": keyed(
        ("MP_MATCH_CONTROL_ERROR_INVALID_VIEW", "MP_MATCH_CONTROL_ERROR_STALE_VIEW",
         "MP_MATCH_CONTROL_ERROR_CAPACITY", "MP_MATCH_CONTROL_ERROR_UNKNOWN_COMMAND",
         "MP_MATCH_CONTROL_ERROR_INVALID_REQUEST_ID",
         "MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE",
         "MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED",
         "MP_MATCH_CONTROL_ERROR_SELECTION_INVALID",
         "MP_MATCH_CONTROL_ERROR_INVALID_SIDE",
         "MP_MATCH_CONTROL_ERROR_INVALID_VALUE",
         "MP_MATCH_CONTROL_ERROR_PROPOSAL_MISSING",
         "MP_MATCH_CONTROL_ERROR_PROTOCOL_INVALID"), 42720
    ),
}


FUNCTION_TYPES = {
    "MPMatchControlLocalizationKey": "mpMatchLocalizationId_t",
    "MPMatchControlProtocolReasonKey": "mpMatchProtocolReason_t",
    "MPMatchControlPhaseKey": "mpGameState_t",
    "MPMatchControlRoundKey": "roundState_t",
    "MPMatchControlPauseStateKey": "mpMatchViewPauseState_t",
    "MPMatchControlPauseKindKey": "mpMatchViewPauseKind_t",
    "MPMatchControlPauseReasonKey": "mpMatchViewPauseReason_t",
    "MPMatchControlResumePolicyKey": "mpMatchViewResumePolicy_t",
    "MPMatchControlPublicRoleKey": "mpMatchViewPublicRole_t",
    "MPMatchControlRosterRoleKey": "mpMatchViewRosterRole_t",
    "MPMatchControlProtocolRosterRoleKey": "mpMatchProtocolRosterRole_t",
    "MPMatchControlQueueStateKey": "mpMatchViewQueueState_t",
    "MPMatchControlProposalScopeKey": "mpMatchViewProposalScope_t",
    "MPMatchControlBallotKey": "mpMatchViewBallot_t",
    "MPMatchControlProtocolBallotKey": "mpMatchBallotChoice_t",
    "MPMatchControlSeriesStateKey": "mpMatchViewSeriesState_t",
    "MPMatchControlVetoActionKey": "mpMatchViewVetoAction_t",
    "MPMatchControlProtocolVetoActionKey": "mpMatchVetoAction_t",
    "MPMatchControlMapDispositionKey": "mpMatchViewMapDisposition_t",
    "MPMatchControlMapOutcomeKey": "mpMatchViewMapOutcome_t",
    "MPMatchControlRuleTypeKey": "mpMatchViewRuleType_t",
    "MPMatchControlRulesBoundaryKey": "mpMatchViewRulesBoundary_t",
    "MPMatchControlEvidenceStateKey": "mpMatchViewEvidenceState_t",
    "MPMatchControlMVDStateKey": "mpMatchViewMVDState_t",
    "MPMatchControlReportStateKey": "mpMatchViewReportState_t",
    "MPMatchControlEvidenceEventKindKey": "mpMatchViewEvidenceEventKind_t",
    "MPMatchControlOperationResultStatusKey": "mpMatchOperationResultStatus_t",
    "MPMatchControlTeamKey": "mpMatchTeam_t",
    "MPMatchControlStartingSideKey": "mpMatchStartingSide_t",
    "MPMatchControlReadinessBlockerKey": "mpMatchReadinessBlocker_t",
    "MPMatchControlRuleFieldKey": "unsigned char",
    "MPMatchControlMatchProfileKey": "mpMatchProfileId_t",
    "MPMatchControlSeriesProfileKey": "mpSeriesProfileId_t",
    "MPMatchControlErrorReasonKey": "mpMatchControlErrorReason_t",
}


SPECIAL_CASES = {
    "MP_MATCH_LOCALIZATION_NONE": "NO_DETAIL_KEY",
    "MP_MATCH_PROTOCOL_REASON_NONE": "NO_DETAIL_KEY",
    "MP_MATCH_CONTROL_ERROR_NONE": "NO_DETAIL_KEY",
    "MP_MATCH_LOCALIZATION_OPERATION_BASE": "UNKNOWN_KEY",
    "MP_MATCH_LOCALIZATION_CONFIRM_BASE": "UNKNOWN_KEY",
    "MP_MATCH_LOCALIZATION_REASON_BASE": "UNKNOWN_KEY",
    "MP_MATCH_LOCALIZATION_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_PROTOCOL_REASON_COUNT": "UNKNOWN_KEY",
    "STATE_COUNT": "UNKNOWN_KEY",
    "RS_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_PAUSE_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_PAUSE_KIND_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_PAUSE_REASON_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_RESUME_POLICY_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_ROLE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_ROSTER_ROLE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_QUEUE_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_PROPOSAL_SCOPE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_BALLOT_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_SERIES_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_VETO_ACTION_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_MAP_DISPOSITION_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_MAP_OUTCOME_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_RULE_TYPE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_RULES_BOUNDARY_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_EVIDENCE_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_MVD_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_REPORT_STATE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_RESULT_STATUS_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_TEAM_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_BLOCKER_COUNT": "UNKNOWN_KEY",
    "MP_RULE_FIELD_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_PROFILE_COUNT": "UNKNOWN_KEY",
    "MP_SERIES_PROFILE_COUNT": "UNKNOWN_KEY",
    "MP_MATCH_CONTROL_ERROR_COUNT": "UNKNOWN_KEY",
}


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def parse_language(path: Path) -> dict[str, str]:
    pairs = [
        (match.group("id"), match.group("value"))
        for line in read(path).splitlines()
        if (match := ENTRY_RE.match(line)) is not None
    ]
    duplicates = sorted(
        key for key, count in Counter(key for key, _ in pairs).items() if count != 1
    )
    if duplicates:
        raise AssertionError(f"{path.name} has duplicate keys: {duplicates}")
    return dict(pairs)


def enum_members(header: str, type_name: str) -> set[str]:
    match = re.search(
        rf"typedef\s+enum\s*\{{(?P<body>[^}}]*)\}}\s*{re.escape(type_name)}\s*;",
        header,
    )
    if match is None:
        raise AssertionError(f"could not find enum {type_name}")
    members: set[str] = set()
    for line in match.group("body").splitlines():
        candidate = re.match(r"\s*([A-Z][A-Z0-9_]*)\b", line)
        if candidate is not None:
            members.add(candidate.group(1))
    return members


def static_contracts(header: str, source: str) -> None:
    for function, cases in FUNCTION_CASES.items():
        if f"const char *{function}(" not in header:
            raise AssertionError(f"missing declaration for {function}")
        if f"const char *{function}(" not in source:
            raise AssertionError(f"missing definition for {function}")
        for enum_name, key in cases:
            expected = f'case {enum_name}: return "{key}";'
            if expected not in source:
                raise AssertionError(f"missing fixed mapping: {expected}")

    for enum_name, result in SPECIAL_CASES.items():
        expected = f"case {enum_name}: return {result};"
        if expected not in source:
            raise AssertionError(f"missing explicit sentinel mapping: {expected}")

    protocol_header = read(PROTOCOL_HEADER)
    expected_localization_ids = {
        enum_name
        for enum_name, _ in FUNCTION_CASES["MPMatchControlLocalizationKey"]
    } | {
        enum_name for enum_name in SPECIAL_CASES
        if enum_name.startswith("MP_MATCH_LOCALIZATION_")
    }
    actual_localization_ids = enum_members(protocol_header, "mpMatchLocalizationId_t")
    if actual_localization_ids != expected_localization_ids:
        raise AssertionError(
            "protocol localization-id coverage drifted; "
            f"missing={sorted(actual_localization_ids - expected_localization_ids)}, "
            f"stale={sorted(expected_localization_ids - actual_localization_ids)}"
        )

    for type_name, function in (
        ("mpMatchProtocolRosterRole_t", "MPMatchControlProtocolRosterRoleKey"),
        ("mpMatchBallotChoice_t", "MPMatchControlProtocolBallotKey"),
        ("mpMatchVetoAction_t", "MPMatchControlProtocolVetoActionKey"),
        ("mpMatchStartingSide_t", "MPMatchControlStartingSideKey"),
    ):
        expected = {enum_name for enum_name, _ in FUNCTION_CASES[function]}
        actual = enum_members(protocol_header, type_name)
        if actual != expected:
            raise AssertionError(
                f"{type_name} localization coverage drifted; "
                f"missing={sorted(actual - expected)}, stale={sorted(expected - actual)}"
            )

    for returned in FIXED_RETURN_RE.findall(source):
        if returned in ("UNKNOWN_KEY", "NO_DETAIL_KEY"):
            continue
        if re.fullmatch(r'"#str_\d+"', returned):
            continue
        raise AssertionError(f"localization key is not a fixed literal: {returned}")

    for forbidden in ("va(", "sprintf(", "snprintf(", "#str_%", "std::", "idStr"):
        if forbidden in source:
            raise AssertionError(f"localization bridge contains forbidden construction {forbidden!r}")

    listed = subprocess.run(
        [sys.executable, str(GAME_ROOT / "src/buildscripts/list_sources.py"),
         str(GAME_ROOT / "src"), "mpgame", "mpgame/Callbacks.cpp",
         "mpgame/gamesys/Callbacks.cpp"],
        cwd=GAME_ROOT,
        text=True,
        capture_output=True,
    )
    if listed.returncode != 0:
        raise AssertionError("could not inspect MP source discovery:\n" + listed.stderr)
    if "mpgame/mp/match/MatchControlLocalization.cpp" not in listed.stdout.splitlines():
        raise AssertionError("MatchControlLocalization.cpp is not compiled into game_mp")

    referenced = set(re.findall(r'"(#str_\d+)"', source))
    referenced.update(("#str_42301", "#str_42302"))
    presentation = (
        read(PROJECTION_SOURCE) + read(MULTIPLAYER_SOURCE) + read(MATCH_CONTROL_GUI)
        + read(GAME_ROOT / "src/mpgame/mp/match/MatchRules.cpp")
    )
    referenced.update(
        key
        for key in re.findall(r'"(#str_\d+)"', presentation)
        if 42300 < int(key.removeprefix("#str_")) < 42800
    )
    language_tables = {name: parse_language(STRINGS_ROOT / name) for name in LANGUAGE_FILES}
    for name, table in language_tables.items():
        missing = sorted(referenced - set(table))
        if missing:
            raise AssertionError(f"{name} lacks bridge keys: {missing}")
        for key in referenced:
            value = table[key].strip()
            if not value or re.search(r"\b(?:todo|tbd|fixme|placeholder)\b|\?\?\?", value, re.I):
                raise AssertionError(f"{name} has incomplete text for {key}: {value!r}")

    new_referenced = {
        key for key in referenced if 42300 < int(key.removeprefix("#str_")) < 42800
    }
    for name, table in language_tables.items():
        actual = {
            key for key in table if 42300 < int(key.removeprefix("#str_")) < 42800
        }
        if actual != new_referenced:
            raise AssertionError(
                f"{name} closed bridge range drifted; "
                f"missing={sorted(new_referenced - actual)}, "
                f"unexpected={sorted(actual - new_referenced)}"
            )


def native_harness() -> str:
    checks: list[str] = []
    for function, cases in FUNCTION_CASES.items():
        for enum_name, key in cases:
            checks.append(f'\tCHECK_KEY( {function}( {enum_name} ), "{key}" );')

    for function, value_type in FUNCTION_TYPES.items():
        checks.append(
            f'\tCHECK_KEY( {function}( static_cast<{value_type}>( 255 ) ), "#str_42301" );'
        )
    checks.extend(
        (
            '\tCHECK_KEY( MPMatchControlLocalizationKey( MP_MATCH_LOCALIZATION_NONE ), "#str_42302" );',
            '\tCHECK_KEY( MPMatchControlProtocolReasonKey( MP_MATCH_PROTOCOL_REASON_NONE ), "#str_42302" );',
            '\tCHECK_KEY( MPMatchControlErrorReasonKey( MP_MATCH_CONTROL_ERROR_NONE ), "#str_42302" );',
        )
    )
    return r'''
#include <string.h>

#define MP_MATCH_CONTROL_LOCALIZATION_STANDALONE_TEST 1
#include "mpgame/mp/match/MatchControlLocalization.cpp"

#define CHECK_KEY(actual, expected) do { \
	if ((actual) == 0 || strcmp((actual), (expected)) != 0) return __LINE__; \
} while (0)

int main() {
''' + "\n".join(checks) + r'''
	return 0;
}
'''


def native_contracts() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("match_control_localization_bridge: native checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-localization-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_control_localization_bridge.cpp"
        executable = temp_dir / (
            "match_control_localization_bridge.exe"
            if compiler.lower().endswith(".exe")
            else "match_control_localization_bridge"
        )
        harness.write_text(native_harness(), encoding="utf-8")
        compiled = subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
             f"-I{GAME_ROOT / 'src'}", str(harness), "-o", str(executable)],
            cwd=GAME_ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone localization bridge did not compile:\n"
                + compiled.stdout + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=GAME_ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "native localization mapping failed at harness line "
                f"{ran.returncode}:\n{ran.stdout}{ran.stderr}"
            )


def main() -> None:
    static_contracts(read(HEADER), read(SOURCE))
    native_contracts()
    print("match_control_localization_bridge: PASS")


if __name__ == "__main__":
    main()
