#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Regression contracts for fail-closed competitive map transactions."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MULTIPLAYER_HEADER = ROOT / "src/game/MultiplayerGame.h"
MULTIPLAYER_SOURCE = ROOT / "src/game/MultiplayerGame.cpp"
EVIDENCE_SOURCE = ROOT / "src/game/mp/match/MatchEvidence.cpp"
EVIDENCE_STORAGE_SOURCE = ROOT / "src/game/mp/match/MatchEvidenceStorage.cpp"
OPERATIONS_HEADER = ROOT / "src/game/mp/match/MatchOperations.h"
OPERATIONS_SOURCE = ROOT / "src/game/mp/match/MatchOperations.cpp"


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def require_order(text: str, tokens: tuple[str, ...], context: str) -> None:
    cursor = -1
    for token in tokens:
        position = text.find(token, cursor + 1)
        if position < 0:
            raise AssertionError(f"missing {token!r} in {context}")
        if position <= cursor:
            raise AssertionError(f"out-of-order {token!r} in {context}")
        cursor = position


def function(text: str, signature: str, context: str) -> str:
    begin = text.find(signature)
    if begin < 0:
        raise AssertionError(f"missing function {signature!r} in {context}")
    opening = text.find("{", begin + len(signature))
    if opening < 0:
        raise AssertionError(f"missing body for {signature!r} in {context}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[begin : index + 1]
    raise AssertionError(f"unterminated function {signature!r} in {context}")


def lifecycle_contracts(header: str, source: str) -> None:
    for token in (
        "bool\t\t\tmatchSessionOperational;",
        "bool\t\t\tmatchEvidenceFinalizationPending;",
        "bool\t\t\tCanEnterMatchCountdown( void ) const;",
    ):
        require(header, token, "transaction lifecycle members")

    begin = function(source, "bool idMultiplayerGame::BeginMatchSession", "session begin")
    require_order(
        begin,
        (
            "matchSessionOperational = false;",
            "if ( !FinalizeMatchEvidence( true ) )",
            "sys->SecureRandomBytes( &randomBase",
            "++nextMatchSessionId;",
            "matchSession.Reset( nextMatchSessionId",
            "CompetitionSeriesMapMatchesRuntime( matchSeries",
            "PersistCompetitionSeriesCandidate( matchSeries, matchSeriesReport",
            "matchSessionOperational = true;",
        ),
        "boot-unique fail-closed session begin",
    )
    require(begin, "reservedSessionRange = UINT64_C( 1 ) << 32", "session id reserve")

    # The unified game module (CORE-07) runs Reset for campaign maps too, so the
    # match-session half lives in ResetMatchSession, which only network play runs.
    reset = function(source, "void idMultiplayerGame::Reset()", "map reset")
    require_order(
        reset,
        (
            "const bool matchSession = gameLocal.IsMultiplayerMode();",
            "if ( !FinalizeMatchEvidence( true ) )",
            "Clear();",
            "if ( matchSession ) {\n\t\tResetMatchSession();",
        ),
        "reset finalization/recovery transaction",
    )
    session = function(source, "void idMultiplayerGame::ResetMatchSession(", "match-session reset")
    require_order(
        session,
        (
            "bool recoveryAvailable = true;",
            "recoveryAvailable = RestoreCompetitionSeriesIfRequested();",
            "recoveryAvailable && !BeginMatchSession()",
        ),
        "reset finalization/recovery transaction",
    )
    clear = function(source, "void idMultiplayerGame::Clear()", "clear")
    require_order(
        clear,
        (
            "matchSessionOperational = false;",
            "if ( !matchEvidenceFinalizationPending )",
            "matchEvidence.Clear();",
        ),
        "pending evidence retention",
    )
    shutdown = function(source, "void idMultiplayerGame::Shutdown", "shutdown")
    require_order(
        shutdown,
        ("if ( !FinalizeMatchEvidence( true ) )", "Clear();"),
        "shutdown finalization retention",
    )


def immutable_identity_contracts(source: str) -> None:
    runtime_identity = function(
        source, "static bool CompetitionSeriesMapMatchesRuntime", "runtime identity"
    )
    for token in (
        "configuration.gameType != runtimeGameType",
        "series.GetCurrentSelectionIndex()",
        "MPMapSupportsGameType( mapDecl, runtimeGameType )",
        "NormalizeMapDeclPath( loadedMap, runtimePath )",
        "idStr::Icmp( selectedPath.c_str(), runtimePath.c_str() ) == 0",
    ):
        require(runtime_identity, token, "immutable map/mode identity")
    if 'serverInfo.GetString( "si_map" )' in runtime_identity:
        raise AssertionError("runtime series identity trusts mutable si_map")

    restore = function(
        source,
        "bool idMultiplayerGame::RestoreCompetitionSeriesIfRequested",
        "series restore",
    )
    require_order(
        restore,
        (
            "MPMatchSeriesRecoveryRestoreCores(",
            "CompetitionSeriesMapMatchesRuntime( candidate, gameLocal.gameType",
            "gameLocal.GetMapName()",
            "matchSeries = candidate;",
        ),
        "restore identity before publication",
    )

    evidence_begin = function(source, "bool idMultiplayerGame::BeginMatchEvidence", "evidence begin")
    require(evidence_begin, "CompetitionSeriesMapMatchesRuntime( matchSeries", "series evidence identity")
    require(evidence_begin, "metadata.map = evidenceMap.c_str();", "immutable evidence map")

    commit = function(
        source,
        "bool idMultiplayerGame::CommitCompetitionSeriesMapEvidence",
        "map result commit",
    )
    for token in (
        "matchSeriesAwaitingMapSession",
        "matchSeriesLinkedSessionId != matchSession.GetSessionId()",
        "CompetitionSeriesMapMatchesRuntime( matchSeries, gameLocal.gameType",
        "gameLocal.GetMapName()",
        "metadata.sessionId != matchSession.GetSessionId()",
        "metadata.seriesId != matchSeriesId",
        "metadata.rulesDigest != matchRules.Committed().Digest()",
        "metadata.modeId != static_cast<uint32_t>",
        "idStr::Icmp( selectedPath.c_str(), evidencePath.c_str() ) != 0",
    ):
        require(commit, token, "commit-time immutable identity")


def terminal_evidence_contracts(multiplayer: str, evidence: str) -> None:
    append = function(evidence, "mpEvidenceWriteResult mpMatchEvidence::AppendValidatedEvent", "event append")
    require_order(
        append,
        (
            "bool hasMapResult = false;",
            "kind == MP_EVIDENCE_EVENT_MAP_RESULT || hasMapResult",
            "MP_MATCH_EVIDENCE_MAX_EVENTS - 1",
            "if ( eventCount >= eventCapacity )",
        ),
        "reserved terminal result capacity",
    )

    record = function(
        multiplayer, "void idMultiplayerGame::RecordMatchEvidenceResult", "result journal"
    )
    require_order(
        record,
        (
            "const mpEvidenceWriteResult written = matchEvidence.AppendMapResult(",
            "if ( written.code != MP_EVIDENCE_WRITE_ACCEPTED )",
            "matchEvidenceFinalizationPending = true;",
        ),
        "terminal append failure handling",
    )
    finalize = function(
        multiplayer, "bool idMultiplayerGame::FinalizeMatchEvidence", "evidence finalizer"
    )
    for token in (
        "matchEvidenceFinalizationPending && !hasTerminalResult",
        "if ( !hasTerminalResult )",
        "matchEvidenceFinalizationPending = true;",
        "CommitCompetitionSeriesMapEvidence( evidenceStorage )",
        "matchEvidenceFinalizationPending = false;",
    ):
        require(finalize, token, "terminal finalization gate")


def operation_gate_contracts(header: str, operations: str, multiplayer: str) -> None:
    for token in ("bool sessionOperational;", "bool countdownPrerequisitesSatisfied;"):
        require(header, token, "trusted operation context")
    for signature in (
        "mpOperationExecutionResult_t mpMatchOperationExecutor::ExecuteInternal",
        "mpOperationExecutionResult_t mpMatchOperationExecutor::ExecutePassedProposal",
        "mpOperationExecutionResult_t mpMatchOperationExecutor::AcknowledgePassedProposal",
    ):
        body = function(operations, signature, "pure operation gate")
        require(body, "if ( !context.sessionOperational )", "pure operation gate")
        require(body, "MP_OPERATION_REASON_SESSION_MISMATCH", "typed unavailable result")

    build_context = function(
        multiplayer, "void idMultiplayerGame::BuildMatchOperationContext", "adapter context"
    )
    require(build_context, "context.sessionOperational = matchSessionOperational;", "operation latch")
    require(
        build_context,
        "context.countdownPrerequisitesSatisfied = CanEnterMatchCountdown();",
        "countdown prerequisite latch",
    )
    countdown = function(
        multiplayer, "bool idMultiplayerGame::CanEnterMatchCountdown", "countdown gate"
    )
    for token in (
        "if ( !matchSessionOperational )",
        "gameLocal.gameType != GAME_DUEL",
        "!matchSeriesNeedsBindingRecovery",
    ):
        require(countdown, token, "recovered Duel countdown gate")

    for signature in (
        "void idMultiplayerGame::BeginCompetitiveFrame",
        "mpMatchViewAllowedOperationMask_t idMultiplayerGame::AllowedMatchOperationsFor",
        "bool idMultiplayerGame::BindCompetitionSeriesContestant",
        "void idMultiplayerGame::ServerReceiveMatchOperation",
        "bool idMultiplayerGame::ExecuteTrustedLocalMatchOperation",
        "bool idMultiplayerGame::SubmitMatchOperation",
        "bool idMultiplayerGame::ServerReconcileManagedUserInfo",
        "void idMultiplayerGame::SynchronizeMatchParticipant",
        "bool idMultiplayerGame::CanCommitMatchPhaseTransition",
        "bool idMultiplayerGame::CommitMatchRoundTransition",
        "bool idMultiplayerGame::BeginMatchOvertimePeriod",
    ):
        body = function(multiplayer, signature, "live non-operational gate")
        require(body, "matchSessionOperational", f"non-operational gate {signature}")


def path_identity_contract(storage: str, multiplayer: str) -> None:
    require(storage, '"match-results/session-"', "evidence path namespace")
    require(storage, "finalPath.PutUnsigned64( metadata.sessionId )", "session path identity")
    mvd = function(multiplayer, "void idMultiplayerGame::StartMatchMVDIfRequired", "MVD path")
    require(mvd, "matchSession.GetSessionId()", "MVD session path identity")


def main() -> int:
    header = MULTIPLAYER_HEADER.read_text(encoding="utf-8")
    multiplayer = MULTIPLAYER_SOURCE.read_text(encoding="utf-8")
    evidence = EVIDENCE_SOURCE.read_text(encoding="utf-8")
    storage = EVIDENCE_STORAGE_SOURCE.read_text(encoding="utf-8")
    operations_header = OPERATIONS_HEADER.read_text(encoding="utf-8")
    operations = OPERATIONS_SOURCE.read_text(encoding="utf-8")

    lifecycle_contracts(header, multiplayer)
    immutable_identity_contracts(multiplayer)
    terminal_evidence_contracts(multiplayer, evidence)
    operation_gate_contracts(operations_header, operations, multiplayer)
    path_identity_contract(storage, multiplayer)
    print("mp_match_transaction_hardening_contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
