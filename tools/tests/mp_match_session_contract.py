#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contracts and executable invariants for the authoritative MP session core."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchSession.h"
SOURCE = ROOT / "src/game/mp/match/MatchSession.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def extract_table(source: str, name: str) -> set[tuple[str, str, str]]:
    match = re.search(
        rf"static const \w+ {re.escape(name)}\[\] = \{{(?P<body>.*?)\n\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"could not find table {name}")
    return {
        tuple(part.strip() for part in row)
        for row in re.findall(
            r"\{\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*\}",
            match.group("body"),
        )
    }


def static_contracts(header: str, source: str) -> None:
    require(header, '#include "../MatchPhase.h"', "dependency-neutral header")
    if "GameState.h" in header or re.search(r"=\s*MAX_CLIENTS\b", header):
        raise AssertionError("MatchSession.h reintroduced a game-local include dependency")
    for token in (
        "MP_MATCH_MAX_CONNECTION_SLOTS = 32",
        "MP_MATCH_MAX_PARTICIPANTS = 32",
        "MP_MATCH_MAX_ROSTER_SEATS = 32",
        "MP_MATCH_ROSTER_SUBSTITUTE",
        "MPMatchRosterRoleIsActive",
        "MPMatchPrincipalRolesForRosterRole",
		"MP_MATCH_CAP_SELF_ROSTER_LEAVE",
    ):
        require(header, token, "fixed protocol ceilings")
    require(
        source,
        "static_assert( MAX_CLIENTS <= MP_MATCH_MAX_CONNECTION_SLOTS",
        "platform client-capacity guard",
    )

    forbidden = (
        "idUserInterface",
        "idBitMsg",
        "idCVar",
        "idFile",
        "cmdSystem",
        "rvGameState::NewState",
        "idList<",
        "new mpMatch",
    )
    combined = header + source
    for token in forbidden:
        if token in combined:
            raise AssertionError(f"session value layer contains forbidden dependency {token!r}")

    expected_phases = {
        ("INACTIVE", "WARMUP", "MP_MATCH_TRANSITION_SESSION_INITIALIZED"),
        ("WARMUP", "COUNTDOWN", "MP_MATCH_TRANSITION_READY_GATE"),
        ("WARMUP", "COUNTDOWN", "MP_MATCH_TRANSITION_REFEREE_FORCE_READY"),
        ("COUNTDOWN", "WARMUP", "MP_MATCH_TRANSITION_COUNTDOWN_ABORTED"),
        ("COUNTDOWN", "WARMUP", "MP_MATCH_TRANSITION_RULES_INVALIDATED"),
        ("COUNTDOWN", "WARMUP", "MP_MATCH_TRANSITION_ROSTER_INVALIDATED"),
        ("COUNTDOWN", "WARMUP", "MP_MATCH_TRANSITION_REQUIRED_PLAYER_LOST"),
        ("COUNTDOWN", "GAMEON", "MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE"),
        ("GAMEON", "SUDDENDEATH", "MP_MATCH_TRANSITION_REGULATION_TIE"),
        ("GAMEON", "GAMEREVIEW", "MP_MATCH_TRANSITION_LIMIT_REACHED"),
        ("GAMEON", "GAMEREVIEW", "MP_MATCH_TRANSITION_FORFEIT"),
        ("GAMEON", "GAMEREVIEW", "MP_MATCH_TRANSITION_MATCH_ABORTED"),
        ("SUDDENDEATH", "GAMEREVIEW", "MP_MATCH_TRANSITION_LIMIT_REACHED"),
        ("SUDDENDEATH", "GAMEREVIEW", "MP_MATCH_TRANSITION_FORFEIT"),
        ("SUDDENDEATH", "GAMEREVIEW", "MP_MATCH_TRANSITION_MATCH_ABORTED"),
        ("GAMEREVIEW", "NEXTGAME", "MP_MATCH_TRANSITION_REVIEW_COMPLETE"),
        ("GAMEREVIEW", "NEXTGAME", "MP_MATCH_TRANSITION_REFEREE_ADVANCE"),
        ("NEXTGAME", "WARMUP", "MP_MATCH_TRANSITION_SAME_MAP_RESTART"),
        ("NEXTGAME", "WARMUP", "MP_MATCH_TRANSITION_NEXT_MAP_READY"),
        ("NEXTGAME", "INACTIVE", "MP_MATCH_TRANSITION_SESSION_END"),
    }
    for state in (
        "WARMUP",
        "COUNTDOWN",
        "GAMEON",
        "SUDDENDEATH",
        "GAMEREVIEW",
        "NEXTGAME",
    ):
        expected_phases.add((state, "INACTIVE", "MP_MATCH_TRANSITION_MAP_SHUTDOWN"))
    for state in (
        "WARMUP",
        "COUNTDOWN",
        "GAMEON",
        "SUDDENDEATH",
        "GAMEREVIEW",
        "NEXTGAME",
    ):
        expected_phases.add((state, "INACTIVE", "MP_MATCH_TRANSITION_FATAL_RESET"))
    actual_phases = extract_table(source, "legalPhaseTransitions")
    if actual_phases != expected_phases:
        raise AssertionError(
            "legal phase table differs from the authoritative specification: "
            f"missing={sorted(expected_phases - actual_phases)}, "
            f"extra={sorted(actual_phases - expected_phases)}"
        )

    expected_rounds = {
        ("RS_INACTIVE", "RS_COUNTDOWN", "MP_MATCH_ROUND_TRANSITION_PARENT_ACTIVE"),
        ("RS_COUNTDOWN", "RS_ACTIVE", "MP_MATCH_ROUND_TRANSITION_COUNTDOWN_COMPLETE"),
        ("RS_ACTIVE", "RS_COMPLETE", "MP_MATCH_ROUND_TRANSITION_RESULT_COMMITTED"),
        ("RS_COMPLETE", "RS_COUNTDOWN", "MP_MATCH_ROUND_TRANSITION_NEXT_ROUND"),
    }
    for state in ("RS_COUNTDOWN", "RS_ACTIVE", "RS_COMPLETE"):
        expected_rounds.add(
            (state, "RS_INACTIVE", "MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE")
        )
        expected_rounds.add(
            (state, "RS_INACTIVE", "MP_MATCH_ROUND_TRANSITION_SESSION_RESET")
        )
    actual_rounds = extract_table(source, "legalRoundTransitions")
    if actual_rounds != expected_rounds:
        raise AssertionError(
            "legal round table differs from the authoritative specification: "
            f"missing={sorted(expected_rounds - actual_rounds)}, "
            f"extra={sorted(actual_rounds - expected_rounds)}"
        )
    if any(row[:2] == ("RS_ACTIVE", "RS_COUNTDOWN") for row in actual_rounds):
        raise AssertionError("forbidden direct RS_ACTIVE -> RS_COUNTDOWN transition exists")

    for token in (
        "class mpMatchEngineTime",
        "class mpMatchTime",
        "class mpMatchHostTime",
        "class mpParticipantId",
        "mpMatchPauseState_t",
        "mpMatchReadinessBlocker_t",
        "MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE",
        "minimumActivePerRequiredSide",
        "insufficientActiveSideMask",
        "mpMatchTimeoutBudgetState",
        "mpMatchCapability_t",
        "BeginOvertimePeriod",
        "BeginCountdown",
        "expectedCurrentPeriod",
        "livePeriod.start = livePeriod.deadline",
        "NormalizeRoundForParentTransition",
        "ValidateInvariants",
    ):
        require(combined, token, "session core API/invariants")

    # Exactly one helper owns semantic revision increments.  Mutations call it
    # once after all validation; frame-only clock progression does not churn it.
    increment_sites = re.findall(r"\+\+sessionRevision", source)
    if len(increment_sites) != 1:
        raise AssertionError(
            f"sessionRevision must have one increment owner, found {len(increment_sites)}"
        )
    require(source, "result.currentRevision = sessionRevision", "revision result")
    require(source, "if ( !IsExpectedRevision( expectedRevision ) )", "CAS mutations")

    if source.count("--budget.remaining") != 1 or source.count("++budget.consumed") != 1:
        raise AssertionError("timeout budget must have exactly one charge site")
    require(source, "pause.state == MP_MATCH_PAUSE_PENDING", "frame-boundary pause commit")
    require(source, "slotGeneration != slotGenerations[ slot ]", "generation-safe slot lookup")
    require(source, "participant.SessionPart() != sessionId", "session-scoped identity")
    require(
        source,
        "readiness.blockers &= ~MPMatchReadinessBlockerBit(",
        "candidate-frozen atomic countdown readiness",
    )


HARNESS = r'''
#include "game/mp/match/MatchSession.h"

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static bool AppliedOnce( const mpMatchMutationResult &result ) {
	return result.WasApplied() && result.currentRevision == result.previousRevision + 1;
}

int main() {
	mpMatchSession session;
	CHECK( session.ValidateInvariants() );
	CHECK( !session.Reset( 0, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( session.Reset( 42, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( session.GetSessionRevision() == 1 );

	CHECK( AppliedOnce( session.FreezeRules( 7, 0x1234, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.ConfigureRegulationPeriod( 1000,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.ConfigureTimeouts( 1, 2000, 0, 100,
		MP_MATCH_RESUME_OWNER_OR_AUTHORITY, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( GAMEON,
		MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( session.GetLivePeriod().period == 0 );
	CHECK( session.GetLivePeriod().hasDeadline );
	CHECK( session.GetLivePeriod().deadline.Milliseconds() == 1000 );

	const uint64_t liveRevision = session.GetSessionRevision();
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 500 ) ).WasApplied() );
	CHECK( session.GetSessionRevision() == liveRevision );
	CHECK( session.GetMatchTime().Milliseconds() == 500 );
	CHECK( session.BeginOvertimePeriod( 0, 500, session.GetSessionRevision() ).reason ==
		MP_MATCH_REASON_LIVE_PERIOD_NOT_EXPIRED );
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1000 ) ).WasApplied() );
	CHECK( AppliedOnce( session.BeginOvertimePeriod( 0, 500,
		session.GetSessionRevision() ) ) );
	CHECK( session.GetLivePeriod().period == 1 );
	CHECK( session.GetLivePeriod().start.Milliseconds() == 1000 );
	CHECK( session.GetLivePeriod().deadline.Milliseconds() == 1500 );
	const uint64_t overtimeRevision = session.GetSessionRevision();
	CHECK( session.BeginOvertimePeriod( 0, 500, overtimeRevision ).reason ==
		MP_MATCH_REASON_LIVE_PERIOD_MISMATCH );
	CHECK( session.GetSessionRevision() == overtimeRevision );
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1500 ) ).WasApplied() );
	CHECK( AppliedOnce( session.BeginOvertimePeriod( 1, 500,
		session.GetSessionRevision() ) ) );
	CHECK( session.GetLivePeriod().period == 2 );
	CHECK( session.GetLivePeriod().start.Milliseconds() == 1500 );
	CHECK( session.GetLivePeriod().deadline.Milliseconds() == 2000 );

	CHECK( AppliedOnce( session.TransitionRound( RS_COUNTDOWN,
		MP_MATCH_ROUND_TRANSITION_PARENT_ACTIVE, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionRound( RS_ACTIVE,
		MP_MATCH_ROUND_TRANSITION_COUNTDOWN_COMPLETE, session.GetSessionRevision() ) ) );
	CHECK( session.TransitionRound( RS_COUNTDOWN,
		MP_MATCH_ROUND_TRANSITION_NEXT_ROUND, session.GetSessionRevision() ).reason ==
		MP_MATCH_REASON_ILLEGAL_ROUND_TRANSITION );

	CHECK( AppliedOnce( session.RequestTeamTimeout( 0,
		MP_MATCH_PAUSE_REASON_TACTICAL, session.GetSessionRevision() ) ) );
	const uint64_t pendingRevision = session.GetSessionRevision();
	CHECK( session.RequestTeamTimeout( 0, MP_MATCH_PAUSE_REASON_TACTICAL,
		pendingRevision ).reason == MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	CHECK( session.GetSessionRevision() == pendingRevision );
	CHECK( session.GetTimeoutBudget( 0 ).remaining == 1 );
	// The request is made after the 1500ms gameplay frame.  Committing it at
	// the next frame boundary must not count the 100ms simulation frame which
	// the pause suppresses.
	CHECK( AppliedOnce( session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1600 ) ) ) );
	CHECK( session.GetPause().state == MP_MATCH_PAUSED );
	CHECK( session.GetMatchTime().Milliseconds() == 1500 );
	CHECK( session.GetTimeoutBudget( 0 ).remaining == 0 );
	CHECK( session.GetTimeoutBudget( 0 ).consumed == 1 );
	const uint64_t pausedRevision = session.GetSessionRevision();
	CHECK( session.TransitionPhase( GAMEREVIEW, MP_MATCH_TRANSITION_LIMIT_REACHED,
		mpParticipantId::Invalid(), pausedRevision ).reason ==
		MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	CHECK( session.GetSessionRevision() == pausedRevision );
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1700 ) ).WasApplied() );
	CHECK( session.GetMatchTime().Milliseconds() == 1500 );
	CHECK( AppliedOnce( session.RequestResumeByTeam( 0,
		session.GetSessionRevision() ) ) );
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1799 ) ).WasApplied() );
	// Mirror of the rule above: the frame that COMMITS a pause is suppressed and
	// is not accrued, and the frame that COMPLETES a resume is simulated and so
	// must be accrued.  The 1ms 1799->1800 frame completes the resume and counts
	// (1500 -> 1501); the 1800->1900 frame then adds its full 100ms.  Accruing
	// against the pre-transition state instead loses one frame per pause cycle,
	// which is enough to deny a tied timed match its configured overtime.
	CHECK( AppliedOnce( session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1800 ) ) ) );
	CHECK( session.GetPause().state == MP_MATCH_PAUSE_RUNNING );
	CHECK( session.GetMatchTime().Milliseconds() == 1501 );
	CHECK( !session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 1900 ) ).WasApplied() );
	CHECK( session.GetMatchTime().Milliseconds() == 1601 );

	CHECK( AppliedOnce( session.TransitionRound( RS_COMPLETE,
		MP_MATCH_ROUND_TRANSITION_RESULT_COMMITTED, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionRound( RS_COUNTDOWN,
		MP_MATCH_ROUND_TRANSITION_NEXT_ROUND, session.GetSessionRevision() ) ) );
	const uint64_t beforeReview = session.GetSessionRevision();
	CHECK( AppliedOnce( session.TransitionPhase( GAMEREVIEW,
		MP_MATCH_TRANSITION_LIMIT_REACHED, mpParticipantId::Invalid(), beforeReview ) ) );
	CHECK( session.GetRoundState() == RS_INACTIVE );
	CHECK( session.GetSessionRevision() == beforeReview + 1 );
	CHECK( session.ValidateInvariants() );

	CHECK( session.Reset( 99, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	mpParticipantId first;
	CHECK( AppliedOnce( session.BindParticipant( 3, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(), first ) ) );
	CHECK( first.IsValid() && first.SessionPart() == 99 );
	int slot = -1;
	uint32_t generation = 0;
	CHECK( session.ResolveParticipant( first, slot, generation ) );
	CHECK( slot == 3 && generation != 0 );
	mpParticipantId resolved;
	CHECK( session.ResolveSlotBinding( 3, generation, resolved ) && resolved == first );
	mpParticipantId sequenceResolved;
	CHECK( session.ResolveParticipantSequence( first.SequencePart(), sequenceResolved ) &&
		sequenceResolved == first );
	const uint64_t beforeStale = session.GetSessionRevision();
	CHECK( session.SetParticipantRoles( first, 0, beforeStale - 1 ).reason ==
		MP_MATCH_REASON_STALE_REVISION );
	CHECK( session.GetSessionRevision() == beforeStale );
	CHECK( AppliedOnce( session.UnbindParticipant( 3, generation,
		session.GetSessionRevision() ) ) );
	CHECK( !session.ResolveSlotBinding( 3, generation, resolved ) );
	mpParticipantId second;
	CHECK( AppliedOnce( session.BindParticipant( 3, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(), second ) ) );
	CHECK( second != first );
	uint32_t secondGeneration = 0;
	CHECK( session.ResolveParticipant( second, slot, secondGeneration ) );
	CHECK( secondGeneration > generation );
	CHECK( !session.ResolveSlotBinding( 3, generation, resolved ) );

	const mpMatchCapabilityMask_t playerCaps =
		MPMatchCapabilitiesForRole( MP_MATCH_ROLE_PLAYER );
	const mpMatchCapabilityMask_t refereeCaps =
		MPMatchCapabilitiesForRole( MP_MATCH_ROLE_REFEREE );
	const mpMatchCapabilityMask_t operatorCaps =
		MPMatchCapabilitiesForRole( MP_MATCH_ROLE_SERVER_OPERATOR );
	CHECK( playerCaps & MPMatchCapabilityBit( MP_MATCH_CAP_SELF_READY ) );
	CHECK( !( refereeCaps & MPMatchCapabilityBit( MP_MATCH_CAP_FILESYSTEM_OUTPUT ) ) );
	CHECK( operatorCaps & MPMatchCapabilityBit( MP_MATCH_CAP_FILESYSTEM_OUTPUT ) );
	CHECK( !MPMatchRoleMaskIsValid(
		MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ), true ) );
	CHECK( !MPMatchRoleMaskIsValid( MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ), true ) );
	CHECK( !MPMatchRoleMaskIsValid( MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_COACH ), true ) );
	CHECK( !MPMatchRoleMaskIsValid( MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ), true ) );
	CHECK( MPMatchRoleMaskIsValid( MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ), true ) );
	CHECK( MPMatchRosterRoleIsValid( MP_MATCH_ROSTER_SUBSTITUTE ) );
	CHECK( !MPMatchRosterRoleIsActive( MP_MATCH_ROSTER_SUBSTITUTE ) );
	CHECK( MPMatchPrincipalRolesForRosterRole(
		MP_MATCH_ROSTER_SUBSTITUTE ) == 0 );
	CHECK( session.ValidateInvariants() );

	// Starting countdown is one compare-and-swap transaction: readiness is
	// evaluated as if the supplied rules identity were frozen, but neither the
	// identity nor phase is published when any gate rejects it.
	CHECK( session.Reset( 103, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	mpMatchReadinessPolicy atomicPolicy;
	atomicPolicy.policy = MP_MATCH_READY_INDIVIDUAL;
	atomicPolicy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	atomicPolicy.teamMode = true;
	atomicPolicy.minimumActiveHumans = 2;
	atomicPolicy.minimumActivePerRequiredSide = 1;
	atomicPolicy.readyThresholdBasisPoints = 10000;
	atomicPolicy.maximumActivePerSide = 2;
	atomicPolicy.requiredSideMask = 3;
	atomicPolicy.requireDeclaredRosterSeats = false;
	CHECK( AppliedOnce( session.ConfigureReadiness( atomicPolicy,
		session.GetSessionRevision() ) ) );
	mpMatchReadinessPolicy invalidSideMinimum = atomicPolicy;
	invalidSideMinimum.requiredSideMask = 0;
	const uint64_t beforeInvalidSideMinimum = session.GetSessionRevision();
	CHECK( session.ConfigureReadiness( invalidSideMinimum,
		beforeInvalidSideMinimum ).reason == MP_MATCH_REASON_INVALID_ARGUMENT );
	CHECK( session.GetSessionRevision() == beforeInvalidSideMinimum );
	invalidSideMinimum = atomicPolicy;
	invalidSideMinimum.minimumActivePerRequiredSide = 17;
	CHECK( session.ConfigureReadiness( invalidSideMinimum,
		beforeInvalidSideMinimum ).reason == MP_MATCH_REASON_INVALID_ARGUMENT );
	CHECK( session.GetSessionRevision() == beforeInvalidSideMinimum );
	invalidSideMinimum = atomicPolicy;
	invalidSideMinimum.minimumActivePerRequiredSide = 2;
	invalidSideMinimum.maximumActivePerSide = 1;
	CHECK( session.ConfigureReadiness( invalidSideMinimum,
		beforeInvalidSideMinimum ).reason == MP_MATCH_REASON_INVALID_ARGUMENT );
	CHECK( session.GetSessionRevision() == beforeInvalidSideMinimum );
	mpMatchReadinessPolicy oneRequiredSide = atomicPolicy;
	oneRequiredSide.requiredSideMask = 1;
	CHECK( AppliedOnce( session.ConfigureReadiness( oneRequiredSide,
		session.GetSessionRevision() ) ) );
	CHECK( session.EvaluateReadiness().insufficientActiveSideMask == 1u );
	CHECK( AppliedOnce( session.ConfigureReadiness( atomicPolicy,
		session.GetSessionRevision() ) ) );
	mpParticipantId atomicMarine;
	mpParticipantId atomicStrogg;
	CHECK( AppliedOnce( session.BindParticipant( 0, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		atomicMarine ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( atomicMarine, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( atomicMarine, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantReady( atomicMarine, true,
		session.GetSessionRevision() ) ) );
	mpMatchReadinessView atomicReadiness = session.EvaluateReadiness();
	CHECK( atomicReadiness.activePerSide[ 0 ] == 1 &&
		atomicReadiness.activePerSide[ 1 ] == 0 );
	CHECK( atomicReadiness.insufficientActiveSideMask == 2u );
	CHECK( atomicReadiness.blockers & MPMatchReadinessBlockerBit(
		MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE ) );
	CHECK( AppliedOnce( session.FreezeRules( 8, 0x8008,
		session.GetSessionRevision() ) ) );
	const uint64_t beforeAtomicReject = session.GetSessionRevision();
	const mpMatchTransitionView beforeAtomicTransition = session.GetLastTransition();
	const mpMatchMutationResult missingSide = session.BeginCountdown( 9, 0x9009,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		beforeAtomicReject );
	CHECK( missingSide.reason == MP_MATCH_REASON_READINESS_BLOCKED );
	CHECK( session.GetSessionRevision() == beforeAtomicReject &&
		session.GetPhase() == WARMUP && session.HasFrozenRules() );
	CHECK( session.GetFrozenRulesRevision() == 8 &&
		session.GetFrozenRulesDigest() == 0x8008 );
	CHECK( session.GetLastTransition().from == beforeAtomicTransition.from &&
		session.GetLastTransition().to == beforeAtomicTransition.to &&
		session.GetLastTransition().reason == beforeAtomicTransition.reason );
	CHECK( AppliedOnce( session.BindParticipant( 1, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		atomicStrogg ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( atomicStrogg, 1,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( atomicStrogg, true,
		session.GetSessionRevision() ) ) );
	atomicReadiness = session.EvaluateReadiness();
	CHECK( atomicReadiness.insufficientActiveSideMask == 0 );
	CHECK( atomicReadiness.blockers & MPMatchReadinessBlockerBit(
		MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY ) );
	const uint64_t beforeUnreadyReject = session.GetSessionRevision();
	CHECK( session.BeginCountdown( 9, 0x9009,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		beforeUnreadyReject ).reason == MP_MATCH_REASON_READINESS_BLOCKED );
	CHECK( session.GetSessionRevision() == beforeUnreadyReject &&
		session.HasFrozenRules() );
	CHECK( session.GetFrozenRulesRevision() == 8 &&
		session.GetFrozenRulesDigest() == 0x8008 );
	CHECK( AppliedOnce( session.SetParticipantReady( atomicStrogg, true,
		session.GetSessionRevision() ) ) );
	const uint64_t beforeAtomicCommit = session.GetSessionRevision();
	CHECK( AppliedOnce( session.BeginCountdown( 9, 0x9009,
		MP_MATCH_TRANSITION_READY_GATE, atomicMarine,
		beforeAtomicCommit ) ) );
	CHECK( session.GetSessionRevision() == beforeAtomicCommit + 1 &&
		session.GetPhase() == COUNTDOWN && session.HasFrozenRules() );
	CHECK( session.GetFrozenRulesRevision() == 9 &&
		session.GetFrozenRulesDigest() == 0x9009 );
	CHECK( session.GetLastTransition().authorizer == atomicMarine &&
		session.GetLastTransition().forcedReadinessBlockers == 0 );
	CHECK( session.ValidateInvariants() );

	// Referee force may override ordinary population/readiness blockers, but an
	// external rules/map/veto blocker still rejects without publishing a freeze.
	CHECK( session.Reset( 104, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	mpMatchReadinessPolicy forcePolicy;
	forcePolicy.policy = MP_MATCH_READY_INDIVIDUAL;
	forcePolicy.minimumActiveHumans = 2;
	forcePolicy.readyThresholdBasisPoints = 10000;
	CHECK( AppliedOnce( session.ConfigureReadiness( forcePolicy,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetExternalReadinessBlockers(
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_MAP_INVALID ),
		session.GetSessionRevision() ) ) );
	const uint64_t beforeForceReject = session.GetSessionRevision();
	CHECK( session.BeginCountdown( 10, 0xa00a,
		MP_MATCH_TRANSITION_REFEREE_FORCE_READY, mpParticipantId::Invalid(),
		beforeForceReject ).reason == MP_MATCH_REASON_READINESS_BLOCKED );
	CHECK( session.GetSessionRevision() == beforeForceReject &&
		session.GetPhase() == WARMUP && !session.HasFrozenRules() );
	CHECK( AppliedOnce( session.SetExternalReadinessBlockers( 0,
		session.GetSessionRevision() ) ) );
	const uint64_t beforeForceCommit = session.GetSessionRevision();
	CHECK( AppliedOnce( session.BeginCountdown( 10, 0xa00a,
		MP_MATCH_TRANSITION_REFEREE_FORCE_READY, mpParticipantId::Invalid(),
		beforeForceCommit ) ) );
	CHECK( session.GetSessionRevision() == beforeForceCommit + 1 &&
		session.GetPhase() == COUNTDOWN && session.HasFrozenRules() );
	CHECK( session.GetLastForcedReadinessBlockers() &
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_HUMANS ) );
	CHECK( !( session.GetLastForcedReadinessBlockers() &
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_MAP_INVALID ) ) );
	CHECK( session.ValidateInvariants() );

	CHECK( session.Reset( 100, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.FreezeRules( 1, 1, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	mpMatchReadinessPolicy policy;
	policy.policy = MP_MATCH_READY_INDIVIDUAL_AND_TEAM;
	policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	policy.teamMode = true;
	policy.minimumActiveHumans = 2;
	policy.minimumActivePerRequiredSide = 1;
	policy.readyThresholdBasisPoints = 5100;
	policy.maximumActivePerSide = 1;
	policy.requiredSideMask = 3;
	policy.requireDeclaredRosterSeats = true;
	CHECK( AppliedOnce( session.ConfigureReadiness( policy,
		session.GetSessionRevision() ) ) );
	mpParticipantId marine;
	mpParticipantId strogg;
	mpParticipantId substitute;
	const mpMatchRoleMask_t captain = MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN );
	CHECK( AppliedOnce( session.BindParticipant( 0, true, captain,
		session.GetSessionRevision(), marine ) ) );
	CHECK( AppliedOnce( session.BindParticipant( 1, true, captain,
		session.GetSessionRevision(), strogg ) ) );
	CHECK( AppliedOnce( session.BindParticipant( 2, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
		session.GetSessionRevision(), substitute ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( marine, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( strogg, 1,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( marine, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( strogg, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 0, 0, MP_MATCH_ROSTER_CAPTAIN,
		true, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 1, 1, MP_MATCH_ROSTER_CAPTAIN,
		true, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 0, marine,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 1, strogg,
		session.GetSessionRevision() ) ) );
	CHECK( !session.CanSelfLeaveRoster( marine ) );
	CHECK( ( session.GetParticipantCapabilities( marine ) &
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE ) ) == 0 );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 2, 0,
		MP_MATCH_ROSTER_SUBSTITUTE, false, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( substitute, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantRoles( substitute, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 2, substitute,
		session.GetSessionRevision() ) ) );
	const mpMatchParticipantState *substituteState =
		session.FindParticipant( substitute );
	CHECK( substituteState != NULL && !substituteState->active &&
		substituteState->side == 0 &&
		( substituteState->roles & MPMatchRosterPrincipalRoleMask() ) == 0 );
	CHECK( session.CanSelfLeaveRoster( substitute ) );
	CHECK( ( session.GetParticipantCapabilities( substitute ) &
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE ) ) != 0 );
	const uint64_t beforeHostileSubstitute = session.GetSessionRevision();
	CHECK( session.SetParticipantActive( substitute, true,
		beforeHostileSubstitute ).reason == MP_MATCH_REASON_INVALID_ROLE );
	CHECK( session.SetParticipantRoles( substitute,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), beforeHostileSubstitute ).reason ==
		MP_MATCH_REASON_INVALID_ROLE );
	CHECK( session.SetParticipantSide( substitute, 1,
		beforeHostileSubstitute ).reason == MP_MATCH_REASON_INVALID_SIDE );
	CHECK( session.GetSessionRevision() == beforeHostileSubstitute );
	CHECK( session.ValidateInvariants() );
	CHECK( AppliedOnce( session.SetParticipantReady( marine, true,
		session.GetSessionRevision() ) ) );
	CHECK( session.EvaluateReadiness().requiredReadyParticipants == 2 );
	CHECK( session.EvaluateReadiness().blockers &
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY ) );
	CHECK( AppliedOnce( session.SetParticipantReady( strogg, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetTeamReady( 0, true, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetTeamReady( 1, true, session.GetSessionRevision() ) ) );
	CHECK( session.EvaluateReadiness().IsReady() );
	CHECK( AppliedOnce( session.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantReady( marine, false,
		session.GetSessionRevision() ) ) );
	CHECK( session.GetPhase() == WARMUP );
	CHECK( session.GetLastTransition().reason == MP_MATCH_TRANSITION_ROSTER_INVALIDATED );
	CHECK( session.ValidateInvariants() );

	// Excluding bots from the ready vote never excludes their occupied gameplay
	// slots from side assignment and capacity enforcement.  COUNT_AS_READY bots
	// are automatically affirmative because they expose no mutable ready input.
	CHECK( session.Reset( 101, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.FreezeRules( 1, 2,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	policy.policy = MP_MATCH_READY_INDIVIDUAL;
	policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	policy.teamMode = true;
	policy.minimumActiveHumans = 1;
	policy.minimumActivePerRequiredSide = 0;
	policy.readyThresholdBasisPoints = 10000;
	policy.maximumActivePerSide = 1;
	policy.requiredSideMask = 0;
	policy.requireDeclaredRosterSeats = false;
	CHECK( AppliedOnce( session.ConfigureReadiness( policy,
		session.GetSessionRevision() ) ) );
	mpParticipantId capacityHuman;
	mpParticipantId capacityBot;
	CHECK( AppliedOnce( session.BindParticipant( 0, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		capacityHuman ) ) );
	CHECK( AppliedOnce( session.BindParticipant( 1, false,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		capacityBot ) ) );
	CHECK( ( session.GetParticipantCapabilities( capacityHuman ) &
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE ) ) == 0 );
	CHECK( ( session.GetParticipantCapabilities( capacityBot ) &
		MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE ) ) == 0 );
	CHECK( AppliedOnce( session.SetParticipantSide( capacityHuman, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( capacityBot, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( capacityHuman, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( capacityBot, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantReady( capacityHuman, true,
		session.GetSessionRevision() ) ) );
	mpMatchReadinessView botReadiness = session.EvaluateReadiness();
	CHECK( botReadiness.activeParticipants == 2 );
	CHECK( botReadiness.activeHumans == 1 );
	CHECK( botReadiness.activePerSide[ 0 ] == 2 );
	CHECK( botReadiness.readyEligibleParticipants == 1 );
	CHECK( botReadiness.readyParticipants == 1 );
	CHECK( botReadiness.blockers &
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_TEAM_OVERSIZED ) );
	policy.botPolicy = MP_MATCH_BOTS_COUNT_AS_READY;
	CHECK( AppliedOnce( session.ConfigureReadiness( policy,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantReady( capacityHuman, true,
		session.GetSessionRevision() ) ) );
	botReadiness = session.EvaluateReadiness();
	CHECK( botReadiness.readyEligibleParticipants == 2 );
	CHECK( botReadiness.readyParticipants == 2 );
	CHECK( botReadiness.requiredReadyParticipants == 2 );
	CHECK( ( botReadiness.blockers & MPMatchReadinessBlockerBit(
		MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY ) ) == 0 );
	CHECK( botReadiness.blockers &
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_TEAM_OVERSIZED ) );
	CHECK( session.ValidateInvariants() );

	// A live managed participant may always withdraw.  Admission remains
	// phase-gated, but vacating/deactivating/clearing the side must not leave a
	// ghost gameplay participant when userinfo changes or a client departs.
	CHECK( session.Reset( 102, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.FreezeRules( 1, 2,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	policy.policy = MP_MATCH_READY_DISABLED;
	policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	policy.teamMode = true;
	policy.minimumActiveHumans = 1;
	policy.minimumActivePerRequiredSide = 0;
	policy.readyThresholdBasisPoints = 0;
	policy.maximumActivePerSide = 1;
	policy.requiredSideMask = 0;
	policy.requireDeclaredRosterSeats = true;
	CHECK( AppliedOnce( session.ConfigureReadiness( policy,
		session.GetSessionRevision() ) ) );
	mpParticipantId livePlayer;
	CHECK( AppliedOnce( session.BindParticipant( 0, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		livePlayer ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( livePlayer, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( livePlayer, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 0, 0,
		MP_MATCH_ROSTER_PLAYER, true, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 0, livePlayer,
		session.GetSessionRevision() ) ) );
	CHECK( !session.CanSelfLeaveRoster( livePlayer ) );
	CHECK( AppliedOnce( session.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( GAMEON,
		MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.VacateRosterSeat( 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( livePlayer, false,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( livePlayer,
		MP_MATCH_SIDE_NONE, session.GetSessionRevision() ) ) );
	CHECK( session.SetParticipantActive( livePlayer, true,
		session.GetSessionRevision() ).reason == MP_MATCH_REASON_WRONG_PHASE );
	CHECK( session.ValidateInvariants() );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_session_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-session-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_session_contract.cpp"
        executable = temp_dir / (
            "match_session_contract.exe" if compiler.lower().endswith(".exe") else "match_session_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-session contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"match-session executable invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_session_contract: PASS")


if __name__ == "__main__":
    main()
