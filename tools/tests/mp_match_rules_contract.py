#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contracts for the transactional competitive match-rule core."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "src/game/mp/match/MatchRules.h"
SOURCE_PATH = ROOT / "src/game/mp/match/MatchRules.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(
            f"expected {first!r} before {second!r} in {context}"
        )


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature}")
    open_brace = source.find("{", start)
    if open_brace < 0:
        raise AssertionError(f"missing body for {signature}")
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unterminated body for {signature}")


def enum_members(header: str, first: str, sentinel: str) -> list[str]:
    block = re.search(
        rf"typedef enum \{{(?P<body>.*?)\b{re.escape(sentinel)}\b\s*\n\}}\s*\w+;",
        header,
        re.DOTALL,
    )
    if block is None or first not in block.group("body"):
        raise AssertionError(f"could not find enum {first}..{sentinel}")
    body = block.group("body")[block.group("body").index(first) :]
    return re.findall(r"^\s*(MP_RULE_[A-Z0-9_]+)", body, re.MULTILINE)


def main() -> None:
    header = read(HEADER_PATH)
    source = read(SOURCE_PATH)

    expected_fields = [
        "MP_RULE_GAME_TYPE",
        "MP_RULE_MANAGED_MATCH",
        "MP_RULE_WARMUP_ENABLED",
        "MP_RULE_READINESS_POLICY",
        "MP_RULE_READY_THRESHOLD_BASIS_POINTS",
        "MP_RULE_BOTS_CAN_READY",
        "MP_RULE_MIN_ACTIVE_HUMANS",
        "MP_RULE_MIN_TEAM_SIZE",
        "MP_RULE_REQUIRE_BOTH_TEAMS",
        "MP_RULE_ROSTER_SIZE_PER_TEAM",
        "MP_RULE_COUNTDOWN_SECONDS",
        "MP_RULE_TIME_LIMIT_MINUTES",
        "MP_RULE_FRAG_LIMIT",
        "MP_RULE_CAPTURE_LIMIT",
        "MP_RULE_CONTROL_TIME_SECONDS",
        "MP_RULE_ROUND_LIMIT",
        "MP_RULE_ROUND_TIME_LIMIT_SECONDS",
        "MP_RULE_ROUND_COUNTDOWN_SECONDS",
        "MP_RULE_ROUND_REVIEW_SECONDS",
        "MP_RULE_MERCY_LIMIT",
        "MP_RULE_OVERTIME_POLICY",
        "MP_RULE_OVERTIME_PERIOD_SECONDS",
        "MP_RULE_OVERTIME_MAX_PERIODS",
        "MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY",
        "MP_RULE_SUDDEN_DEATH_RESPAWN_INCREASE",
        "MP_RULE_SUDDEN_DEATH_RESPAWN_MAX",
        "MP_RULE_TEAM_DAMAGE",
        "MP_RULE_FORFEIT_ON_EMPTY_TEAM",
        "MP_RULE_BUYING_ENABLED",
        "MP_RULE_TEAM_TIMEOUT_COUNT",
        "MP_RULE_TEAM_TIMEOUT_SECONDS",
        "MP_RULE_TIMEOUT_REQUEST_WINDOW",
        "MP_RULE_TIMEOUT_RESUME_POLICY",
        "MP_RULE_MIN_ACTIVE_PLAYERS",
    ]
    actual_fields = enum_members(header, "MP_RULE_GAME_TYPE", "MP_RULE_FIELD_COUNT")
    if actual_fields != expected_fields:
        raise AssertionError(
            "append-only rule field schema drifted:\n"
            f"expected {expected_fields}\nactual   {actual_fields}"
        )

    descriptor_block = re.search(
        r"static const mpRuleFieldDescriptor_t ruleFields\[\] = \{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if descriptor_block is None:
        raise AssertionError("could not locate rule descriptor table")
    descriptor_ids = re.findall(
        r"MP_RULE_FIELD\(\s*(MP_RULE_[A-Z0-9_]+)",
        descriptor_block.group("body"),
    )
    if descriptor_ids != expected_fields:
        raise AssertionError("descriptor table is not a one-to-one field-id mirror")

    for token in (
        "mpRuleFieldType_t",
        "mpRuleFrozenMutation_t",
        "mpRuleValidationCallback_t",
        "applicableGameTypes",
        "nameLocalizationId",
        "descriptionLocalizationId",
        "MPValidateMatchRulesDescriptorTable",
        "MPValidateBuiltInMatchProfiles",
        "static_assert( MP_RULE_ARRAY_COUNT( ruleFields ) == MP_RULE_FIELD_COUNT",
    ):
        require(header + source, token, "typed descriptor schema")

    public_modes = [
        "GAME_DM",
        "GAME_TOURNEY",
        "GAME_TDM",
        "GAME_CTF",
        "GAME_1F_CTF",
        "GAME_ARENA_CTF",
        "GAME_ARENA_1F_CTF",
        "GAME_DEADZONE",
        "GAME_DUEL",
        "GAME_CA",
        "GAME_FREEZETAG",
        "GAME_REDROVER",
    ]
    game_values = re.search(
        r"static const mpRuleEnumValueDescriptor_t gameTypeValues\[\] = \{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if game_values is None:
        raise AssertionError("could not locate match-rule gametype enum")
    listed_modes = re.findall(r"\{\s*(GAME_[A-Z0-9_]+)\s*,", game_values.group("body"))
    if listed_modes != public_modes:
        raise AssertionError(
            f"rule gametype enum must list exactly 12 public modes: {listed_modes}"
        )
    for hidden in (
        "GAME_OVERLOAD",
        "GAME_HARVESTER",
        "GAME_DOMINATION",
        "GAME_ATTACK_DEFEND",
    ):
        if hidden in game_values.group("body"):
            raise AssertionError(f"unimplemented mode {hidden} entered the rule schema")

    expected_profiles = {
        "casual",
        "competitive_dm",
        "competitive_tourney",
        "competitive_duel",
        "competitive_tdm",
        "competitive_ctf",
        "competitive_deadzone",
        "competitive_round",
    }
    profile_block = re.search(
        r"static const mpRuleProfileDefinition_t profileDefinitions\[\] = \{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if profile_block is None:
        raise AssertionError("could not locate profile definitions")
    profile_keys = set(re.findall(r'\{\s*\{\s*MP_MATCH_PROFILE_[A-Z_]+,\s*"([^"]+)"', profile_block.group("body")))
    if profile_keys != expected_profiles:
        raise AssertionError(f"built-in profile set drifted: {profile_keys}")

    # Built-in team profiles intentionally have no declared roster.  Their
    # readiness gate must therefore be a unanimous individual vote; captain
    # readiness is reserved for custom rules that select the roster workflow.
    for array_name in (
        "competitiveTDMOverrides",
        "competitiveCTFOverrides",
        "competitiveDeadZoneOverrides",
        "competitiveRoundOverrides",
    ):
        overrides = re.search(
            rf"static const mpRuleProfileOverride_t {array_name}\[\] = \{{(?P<body>.*?)\n\}};",
            source,
            re.DOTALL,
        )
        if overrides is None:
            raise AssertionError(f"could not locate {array_name}")
        require(
            overrides.group("body"),
            "{ MP_RULE_READINESS_POLICY, MP_READY_INDIVIDUAL }",
            f"rosterless {array_name} readiness",
        )
        reject(
            overrides.group("body"),
            "MP_READY_INDIVIDUAL_AND_TEAM",
            f"rosterless {array_name} readiness",
        )

    recommended = function_body(
        source, "mpMatchProfileId_t MPRecommendedMatchProfileForGameType"
    )
    for mode in public_modes:
        require(recommended, mode, "recommended profile coverage")
    require(
        source,
        "profileDefinitions[ MP_MATCH_PROFILE_CASUAL ].descriptor.applicableGameTypes !=\n\t\t\tMP_RULE_MODES_ALL_PUBLIC",
        "casual all-mode coverage invariant",
    )
    require(
        source,
        "draft.GetBool( MP_RULE_MANAGED_MATCH ) != profile.managed",
        "profile managed-state invariant",
    )
    profile_validation = function_body(
        source, "bool MPValidateBuiltInMatchProfiles"
    )
    for token in (
        "MP_RULE_COMMIT_REJECTED",
        "MP_RULE_ERROR_FROZEN_FIELD",
        "MP_RULE_COMMIT_STAGED",
        "ApplyStagedAtWarmup",
        "Committed().Revision() != baselineRevision",
        "Committed().Digest() != baselineDigest",
        "draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM ) == 0",
        "draft.GetInteger( MP_RULE_READINESS_POLICY ) != MP_READY_INDIVIDUAL",
        "rosterless.failure.field != MP_RULE_ROSTER_SIZE_PER_TEAM",
        "MP_RULE_ROSTER_SIZE_PER_TEAM ) != 1",
    ):
        require(profile_validation, token, "runtime transaction invariants")

    # All newly reserved labels are descriptor references only; UI integration
    # supplies their translations in every language table.
    framework_ids = {
        int(value) for value in re.findall(r'#str_(416\d\d)', source)
    }
    expected_ids = set(range(41600, 41693))
    if framework_ids != expected_ids:
        missing = sorted(expected_ids - framework_ids)
        extra = sorted(framework_ids - expected_ids)
        raise AssertionError(
            f"match-rule localization band mismatch; missing={missing}, extra={extra}"
        )

    # The core must stay a value/service boundary.  Integration adapters own
    # cvars, console compatibility, GUI and filesystem work.
    for forbidden in (
        "cvarSystem",
        "cmdSystem",
        "gameLocal.",
        "fileSystem",
        "BufferCommandText",
        "ExecuteCommand",
        "GetCVar",
        "SetCVar",
    ):
        reject(source, forbidden, "side-effect-free rules core")

    parser = function_body(source, "bool MPParseBoundedRuleInteger")
    for token in (
        "BoundedStringLength( text, 12 )",
        "magnitude > ( magnitudeLimit - digit ) / 10u",
        "parsed < minimum || parsed > maximum",
    ):
        require(parser, token, "bounded integer parser")
    for forbidden in ("atoi", "strtol", "sscanf", "std::stoi"):
        reject(parser, forbidden, "bounded integer parser")

    parse_value = function_body(source, "bool MPParseMatchRuleValue")
    require(parse_value, "BoundedStringLength( text, 64 )", "bounded value parser")
    require(parse_value, "EnumContainsValue", "closed enum parser")
    require(parse_value, "MP_RULE_ERROR_UNKNOWN_ENUM_VALUE", "closed enum parser")

    validate = function_body(source, "bool mpCompetitiveRules::ValidateDraft")
    for token in (
        "context.maxClients < 1",
        "MP_RULE_ERROR_ROSTER_POLICY",
        "MP_RULE_ERROR_READINESS_POLICY",
        "MP_RULE_ERROR_WIN_CONDITION",
        "MP_RULE_ERROR_OVERTIME_POLICY",
        "MP_RULE_ERROR_TIMEOUT_POLICY",
        "MP_RULE_ERROR_MODE_POLICY",
        "usesTeamReady && rosterSizePerTeam == 0",
        "usesTeamReady && !draft.GetBool( MP_RULE_REQUIRE_BOTH_TEAMS )",
        "MP_RULE_REQUIRE_BOTH_TEAMS, 0, 1, 1",
        "MP_RULE_ROSTER_SIZE_PER_TEAM, rosterSizePerTeam, 1",
    ):
        require(validate, token, "cross-field validation")
    for token in ("MP_RULE_ERROR_MAP_CHECK_MISMATCH", "MP_RULE_ERROR_MAP_UNSUPPORTED"):
        require(source, token, "map-bound mode validation")

    commit = function_body(source, "mpRuleCommitResult_t mpCompetitiveRules::Commit")
    require_before(commit, "ValidateDraft( draft", "candidate.AssignFromDraft", "atomic commit")
    require_before(commit, "candidate.AssignFromDraft", "staged = candidate", "atomic staging")
    require_before(commit, "MP_RULE_ERROR_FROZEN_FIELD", "staged = candidate", "frozen rejection")
    require_before(commit, "staged = candidate", "committed = candidate", "staged/live separation")
    require(commit, "committed.Revision() + 1", "single rules revision increment")
    require(commit, "MP_RULE_COMMIT_STAGED", "next-warmup staging")
    pending_draft = function_body(
        source, "mpMatchRulesDraft mpCompetitiveRules::BeginDraftForNextWarmup"
    )
    require(pending_draft, "if ( !hasStaged )", "cumulative staged editing")
    require(pending_draft, "staged.values", "cumulative staged editing")

    apply_staged = function_body(
        source, "mpRuleCommitResult_t mpCompetitiveRules::ApplyStagedAtWarmup"
    )
    require_before(
        apply_staged,
        "ValidateDraft( draft",
        "committed = staged",
        "staged snapshot revalidation",
    )
    require_before(
        apply_staged,
        "committed = staged",
        "hasStaged = false",
        "staged publish ordering",
    )

    canonical = function_body(
        source, "void mpMatchRulesSnapshot::BuildCanonicalText"
    )
    require(canonical, '"schema=%u\\n"', "canonical schema")
    require(canonical, '"%d:%s=%d\\n"', "canonical ordered fields")
    digest = function_body(source, "void mpMatchRulesSnapshot::RebuildDigest")
    require(digest, "14695981039346656037", "deterministic FNV-1a offset")
    require(digest, "1099511628211", "deterministic FNV-1a prime")
    reject(digest, "sourceProfile", "profile-independent rules digest")

    # The source lister is what both standalone and engine-staged Meson builds
    # consume.  This catches a future exclusion before it becomes a link gap.
    listing = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "game/mp/match/MatchRules.cpp" not in {line.strip() for line in listing}:
        raise AssertionError("MatchRules.cpp is absent from the canonical MP source list")

    print("mp_match_rules_contract: ok")


if __name__ == "__main__":
    main()
