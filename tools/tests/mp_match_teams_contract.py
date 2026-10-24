#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Hostile static and executable contracts for competitive team management."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/mpgame/mp/match"
HEADER = MATCH_DIR / "MatchTeams.h"
SOURCE = MATCH_DIR / "MatchTeams.cpp"
SESSION_SOURCE = MATCH_DIR / "MatchSession.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES = MP_MATCH_MAX_PARTICIPANTS",
        "MP_MATCH_TEAMS_MAX_INVITATIONS = MP_MATCH_MAX_ROSTER_SEATS",
        "MP_MATCH_TEAMS_MAX_INVITATION_MSEC",
        "MP_MATCH_TEAMS_MAX_INVITATION_ID = 2147483647u",
        "mpMatchQueueTicketId_t",
        "mpMatchRosterInvitationId_t",
        "expectedTeamsRevision",
        "expectedSessionRevision",
        "mpMatchTeamsTransactionPlan_t",
        "MPMatchTeamsAssignRosterRole",
        "MPMatchTeamsClearRosterRole",
        "MP_MATCH_ROSTER_SUBSTITUTE",
        "outgoingRosterSeat",
        "assignOutgoingRosterRole",
        "assignOutgoingRosterSeat",
        "IsPersistentBenchSwap",
        "MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION",
        "BuildRecipientSnapshot",
        "PlanNextQueueAdmission",
        "PlanRosterInvitationAcceptance",
        "CommitTransactionPlan",
        "ValidateInvariants",
    ):
        require(combined, token, "fixed team-management schema")

    for token in (
        "const mpMatchSession &session",
        "session.GetSessionRevision()",
        "session.FindParticipant( participant )",
        "session.GetParticipantByIndex( i )",
        "queuePosition != 0",
        "invitation->target != participant",
        "invitation->side != requestedSide",
        "invitation->role != seat->role",
        "candidate.RemoveExpiredInvitations( session, engineNow )",
        "candidate.RemoveQueueAt( queuePosition )",
        "candidate.RemoveInvitationAt( invitationIndex )",
        "PlansEqual( plan, current.plan )",
        "recipient.IsValid()",
    ):
        require(source, token, "fail-closed implementation")

    for forbidden in (
        "idUserInterface",
        "idBitMsg",
        "idCVar",
        "idFile",
        "idList<",
        "idStr",
        "cmdSystem",
        "BufferCommandText",
        "fileSystem",
        "std::vector",
        "std::string",
        "malloc(",
        "calloc(",
        "realloc(",
        "new mpMatch",
    ):
        if forbidden in combined:
            raise AssertionError(
                f"team-management core contains forbidden dependency {forbidden!r}"
            )

    if source.count("++revision") != 1:
        raise AssertionError("all team-core mutations must share one revision increment owner")
    if "mpMatchSession &session" in header.replace("const mpMatchSession &session", ""):
        raise AssertionError("team core must never receive a mutable match session")

    snapshot = header.split("typedef struct mpMatchTeamsRecipientSnapshot_s", 1)[1]
    snapshot = snapshot.split("} mpMatchTeamsRecipientSnapshot_t;", 1)[0]
    if "mpMatchQueueEntry_t" in snapshot:
        raise AssertionError("recipient snapshots must not expose other queue identities")


HARNESS = r'''
#include "mpgame/mp/match/MatchTeams.h"
#include "mpgame/mp/match/MatchProtocol.h"

static const mpMatchPhaseMask_t PHASE_LOBBY = MP_MATCH_PHASE_WARMUP |
	MP_MATCH_PHASE_GAMEREVIEW | MP_MATCH_PHASE_NEXTGAME;
static const mpMatchPhaseMask_t PHASE_LIVE = MP_MATCH_PHASE_COUNTDOWN |
	MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH;
static const mpMatchPhaseMask_t PHASE_INTERACTIVE = PHASE_LOBBY | PHASE_LIVE;
static const mpMatchPhaseMask_t acceptancePhases = @ROSTER_ACCEPT_PHASES@;
static const mpMatchPhaseMask_t assignmentPhases = @ROLE_ASSIGN_PHASES@;
static const mpMatchPhaseMask_t substitutionPhases = @ROSTER_SUBSTITUTE_PHASES@;
static const mpMatchPhaseMask_t withdrawalPhases = @ROSTER_LEAVE_PHASES@;

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static bool SessionApplied( const mpMatchMutationResult &result ) {
	return result.WasApplied() &&
		result.currentRevision == result.previousRevision + 1;
}

static bool TeamsApplied( const mpMatchTeamsMutationResult_t &result ) {
	return result.WasApplied() &&
		result.currentRevision == result.previousRevision + 1;
}

static mpMatchReadinessPolicy Readiness( bool teamMode ) {
	mpMatchReadinessPolicy policy;
	policy.policy = MP_MATCH_READY_DISABLED;
	policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	policy.teamMode = teamMode;
	policy.minimumActiveHumans = 0;
	policy.readyThresholdBasisPoints = 0;
	policy.maximumActivePerSide = teamMode ? 4 : 0;
	policy.requiredSideMask = 0;
	policy.requireDeclaredRosterSeats = false;
	return policy;
}

static mpMatchTeamsPolicy_t TeamPolicy() {
	mpMatchTeamsPolicy_t policy;
	policy.Clear();
	policy.teamMode = true;
	policy.queueEnabled = true;
	policy.requireRosterMembership = false;
	policy.invitationBypassesLock = true;
	policy.requireInvitationForSubstitution = false;
	policy.allowLiveJoin = false;
	policy.allowLiveSubstitution = false;
	policy.maximumActiveTotal = 8;
	policy.maximumActivePerSide = 4;
	return policy;
}

static mpMatchTeamsPolicy_t DuelPolicy() {
	mpMatchTeamsPolicy_t policy;
	policy.Clear();
	policy.queueEnabled = true;
	policy.maximumActiveTotal = 1;
	return policy;
}

static bool InitializeSession( mpMatchSession &session, uint64_t sessionId,
		bool teamMode, int initialTime ) {
	if ( !session.Reset( sessionId,
		mpMatchEngineTime::FromMilliseconds( initialTime ) ) ) {
		return false;
	}
	const mpMatchMutationResult configured = session.ConfigureReadiness(
		Readiness( teamMode ), session.GetSessionRevision() );
	if ( configured.WasRejected() ) {
		return false;
	}
	return SessionApplied( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) );
}

static bool Bind( mpMatchSession &session, int slot, mpParticipantId &out ) {
	return SessionApplied( session.BindParticipant( slot, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(), out ) );
}

static bool ApplyIncomingRole( mpMatchSession &session,
		const mpMatchTeamsTransactionPlan_t &plan ) {
	const mpMatchParticipantState *incoming =
		session.FindParticipant( plan.incomingParticipant );
	mpMatchRoleMask_t roles = 0;
	return incoming != NULL && MPMatchTeamsAssignRosterRole( incoming->roles,
		plan.rosterRole, roles ) && !session.SetParticipantRoles(
			plan.incomingParticipant, roles,
			session.GetSessionRevision() ).WasRejected();
}

static bool ApplyPlanToSessionCopy( mpMatchSession &session,
		const mpMatchTeamsTransactionPlan_t &plan ) {
	if ( plan.vacateRosterSeat && session.VacateRosterSeat( plan.rosterSeat,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.vacateOutgoingRosterSeat && session.VacateRosterSeat(
			plan.outgoingRosterSeat, session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.setOutgoingActive && session.SetParticipantActive(
			plan.outgoingParticipant, plan.outgoingActive,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.setOutgoingSide && session.SetParticipantSide(
			plan.outgoingParticipant, plan.outgoingSide,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.clearOutgoingRosterRole || plan.assignOutgoingRosterRole ) {
		const mpMatchParticipantState *outgoing =
			session.FindParticipant( plan.outgoingParticipant );
		mpMatchRoleMask_t roles = 0;
		const bool mapped = outgoing != NULL &&
			( plan.clearOutgoingRosterRole ?
				MPMatchTeamsClearRosterRole( outgoing->roles, roles ) :
				MPMatchTeamsAssignRosterRole( outgoing->roles,
					plan.outgoingRosterRole, roles ) );
		if ( !mapped || session.SetParticipantRoles( plan.outgoingParticipant,
				roles, session.GetSessionRevision() ).WasRejected() ) {
			return false;
		}
	}
	if ( plan.setIncomingSide && session.SetParticipantSide(
			plan.incomingParticipant, plan.incomingSide,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.assignIncomingRosterRole && plan.incomingActive &&
		!ApplyIncomingRole( session, plan ) ) {
		return false;
	}
	if ( plan.setIncomingActive && session.SetParticipantActive(
			plan.incomingParticipant, plan.incomingActive,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.assignIncomingRosterRole && !plan.incomingActive &&
		!ApplyIncomingRole( session, plan ) ) {
		return false;
	}
	if ( plan.assignRosterSeat && session.AssignRosterSeat( plan.rosterSeat,
			plan.incomingParticipant, session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	if ( plan.assignOutgoingRosterSeat && session.AssignRosterSeat(
			plan.outgoingRosterSeat, plan.outgoingParticipant,
			session.GetSessionRevision() ).WasRejected() ) {
		return false;
	}
	return session.ValidateInvariants();
}

static int InvitationAuthorityAndPhases() {
	mpMatchSession session;
	CHECK( InitializeSession( session, 0x300000001ull, true, 100 ) );
	mpParticipantId captain, bench, target, authority;
	CHECK( Bind( session, 0, captain ) );
	CHECK( Bind( session, 1, bench ) );
	CHECK( Bind( session, 2, target ) );
	CHECK( Bind( session, 3, authority ) );
	CHECK( SessionApplied( session.SetParticipantSide( captain, 0, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantRoles( captain,
		MPMatchPrincipalRolesForRosterRole( MP_MATCH_ROSTER_CAPTAIN ), session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantActive( captain, true, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantSide( bench, 0, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantRoles( bench, 0, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 0, 0, MP_MATCH_ROSTER_CAPTAIN,
		true, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 1, 0, MP_MATCH_ROSTER_SUBSTITUTE,
		false, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 2, 0, MP_MATCH_ROSTER_PLAYER,
		false, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.AssignRosterSeat( 0, captain, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.AssignRosterSeat( 1, bench, session.GetSessionRevision() ) ) );
	mpMatchTeams teams;
	const mpMatchEngineTime now = mpMatchEngineTime::FromMilliseconds( 100 );
	CHECK( teams.Reset( session.GetSessionId(), now ) );
	mpMatchTeamsPolicy_t policy = TeamPolicy();
	CHECK( TeamsApplied( teams.SetSideLocked( session, 0, true, teams.GetRevision() ) ) );
	mpMatchRosterInvitationId_t invitation = 0;
	CHECK( TeamsApplied( teams.IssueRosterInvitation( session, target, 0,
		MP_MATCH_ROSTER_PLAYER, captain, 1000, now, teams.GetRevision(), invitation ) ) );
	CHECK( teams.PlanRosterInvitationAcceptance( session, target, invitation, policy, now ).IsAllowed() );
	mpMatchTeamsRecipientSnapshot_t view;
	CHECK( teams.BuildRecipientSnapshot( session, target, now, view ) && view.invitationCount == 1 );

	// Demotion, activity loss and switching sides all withdraw the old captain's
	// grant before acceptance, projection or another invitation can use it.
	for ( int loss = 0; loss < 3; ++loss ) {
		mpMatchSession changed = session;
		CHECK( SessionApplied( changed.VacateRosterSeat( 0, changed.GetSessionRevision() ) ) );
		if ( loss == 0 ) {
			CHECK( SessionApplied( changed.SetParticipantRoles( captain,
				MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), changed.GetSessionRevision() ) ) );
		} else if ( loss == 1 ) {
			CHECK( SessionApplied( changed.SetParticipantActive( captain, false, changed.GetSessionRevision() ) ) );
		} else {
			CHECK( SessionApplied( changed.SetParticipantSide( captain, 1, changed.GetSessionRevision() ) ) );
		}
		const uint64_t revision = teams.GetRevision();
		CHECK( teams.PlanRosterInvitationAcceptance( changed, target, invitation, policy, now ).reason ==
			MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
		CHECK( teams.BuildRecipientSnapshot( changed, target, now, view ) && view.invitationCount == 0 );
		mpMatchRosterInvitationId_t rejected = 99;
		CHECK( teams.IssueRosterInvitation( changed, target, 0, MP_MATCH_ROSTER_PLAYER,
			captain, 1000, now, revision, rejected ).reason == MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
		CHECK( rejected == 0 && teams.GetRevision() == revision );
	}

	// A real persistent captain/bench swap leaves the former captain connected,
	// but its invitation disappears and its reservation is released permanently.
	mpMatchSession swappedSession = session;
	mpMatchTeams swappedTeams = teams;
	const mpMatchTeamsJoinDecision_t swap = swappedTeams.PlanSubstitution(
		swappedSession, captain, bench, 0, 0, policy, now );
	CHECK( swap.IsAllowed() && swap.plan.IsPersistentBenchSwap() );
	CHECK( TeamsApplied( swappedTeams.CommitTransactionPlan( swap.plan, swappedSession,
		policy, now, swappedTeams.GetRevision() ) ) );
	CHECK( ApplyPlanToSessionCopy( swappedSession, swap.plan ) );
	CHECK( swappedTeams.PlanRosterInvitationAcceptance( swappedSession, target,
		invitation, policy, now ).reason == MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
	CHECK( swappedTeams.BuildRecipientSnapshot( swappedSession, target, now, view ) && view.invitationCount == 0 );
	CHECK( TeamsApplied( swappedTeams.ExpireRosterInvitations( swappedSession, now, swappedTeams.GetRevision() ) ) );
	CHECK( swappedTeams.FindInvitation( invitation ) == NULL );
	mpMatchRosterInvitationId_t replacementInvite = 0;
	CHECK( TeamsApplied( swappedTeams.IssueRosterInvitation( swappedSession, target, 0,
		MP_MATCH_ROSTER_PLAYER, bench, 1000, now, swappedTeams.GetRevision(), replacementInvite ) ) );
	CHECK( replacementInvite != invitation && swappedTeams.PlanRosterInvitationAcceptance(
		swappedSession, target, replacementInvite, policy, now ).IsAllowed() );

	// Referees must retain that current role. Only an explicitly operator-issued
	// invitation can outlive role changes, and it still cannot outlive its binding.
	for ( int operatorGrant = 0; operatorGrant < 2; ++operatorGrant ) {
		mpMatchSession granted = session;
		mpMatchTeams grants;
		CHECK( grants.Reset( session.GetSessionId(), now ) );
		if ( !operatorGrant ) {
			CHECK( SessionApplied( granted.SetParticipantRoles( authority,
				MPMatchRoleBit( MP_MATCH_ROLE_REFEREE ), granted.GetSessionRevision() ) ) );
		}
		mpMatchRosterInvitationId_t grantedId = 0;
		CHECK( TeamsApplied( grants.IssueRosterInvitation( granted, target, 0,
			MP_MATCH_ROSTER_PLAYER, authority, 1000, now, grants.GetRevision(), grantedId, operatorGrant != 0 ) ) );
		CHECK( grants.PlanRosterInvitationAcceptance( granted, target, grantedId, policy, now ).IsAllowed() );
		CHECK( !granted.SetParticipantRoles( authority, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
			granted.GetSessionRevision() ).WasRejected() );
		CHECK( grants.PlanRosterInvitationAcceptance( granted, target, grantedId, policy, now ).IsAllowed() == ( operatorGrant != 0 ) );
		uint32_t generation = 0;
		CHECK( granted.GetSlotGeneration( 3, generation ) );
		CHECK( SessionApplied( granted.UnbindParticipant( 3, generation, granted.GetSessionRevision() ) ) );
		mpParticipantId replacement;
		CHECK( Bind( granted, 3, replacement ) && replacement != authority );
		CHECK( grants.PlanRosterInvitationAcceptance( granted, target, grantedId, policy, now ).reason ==
			MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
		CHECK( grants.BuildRecipientSnapshot( granted, target, now, view ) && view.invitationCount == 0 );
	}

	// Use the production descriptor phase expressions with the real team/session
	// implementations. Review never advertises a transition the session rejects.
	CHECK( SessionApplied( session.FreezeRules( 1, 1, session.GetSessionRevision() ) ) );
	for ( int phase = WARMUP; phase <= NEXTGAME; ++phase ) {
		if ( phase == SUDDENDEATH ) { continue; }
		if ( phase != WARMUP ) {
			const mpMatchTransitionReason_t reason = phase == COUNTDOWN ? MP_MATCH_TRANSITION_READY_GATE :
				phase == GAMEON ? MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE :
				phase == GAMEREVIEW ? MP_MATCH_TRANSITION_LIMIT_REACHED : MP_MATCH_TRANSITION_REVIEW_COMPLETE;
			CHECK( SessionApplied( session.TransitionPhase( static_cast<mpGameState_t>( phase ), reason,
				mpParticipantId::Invalid(), session.GetSessionRevision() ) ) );
		}
		const mpMatchTeamsJoinDecision_t accept = teams.PlanRosterInvitationAcceptance(
			session, target, invitation, policy, now );
		const mpMatchTeamsJoinDecision_t substitute = teams.PlanSubstitution(
			session, captain, bench, 0, 0, policy, now );
		CHECK( accept.IsAllowed() == ( ( acceptancePhases & ( 1u << phase ) ) != 0 ) );
		CHECK( substitute.IsAllowed() == ( ( substitutionPhases & ( 1u << phase ) ) != 0 ) );
		CHECK( ( ( assignmentPhases & ( 1u << phase ) ) != 0 ) == ( phase == WARMUP ) );
		if ( accept.IsAllowed() || substitute.IsAllowed() ) {
			mpMatchSession candidate = session;
			CHECK( ApplyPlanToSessionCopy( candidate, accept.IsAllowed() ? accept.plan : substitute.plan ) );
		}
		if ( phase == GAMEREVIEW || phase == NEXTGAME ) {
			CHECK( accept.reason == MP_MATCH_TEAMS_REASON_WRONG_PHASE );
			CHECK( substitute.reason == MP_MATCH_TEAMS_REASON_WRONG_PHASE );
			const uint64_t revision = session.GetSessionRevision();
			CHECK( session.SetParticipantActive( target, true, revision ).reason == MP_MATCH_REASON_WRONG_PHASE );
			CHECK( session.SetParticipantSide( target, 0, revision ).reason == MP_MATCH_REASON_WRONG_PHASE );
			CHECK( session.AssignRosterSeat( 2, target, revision ).reason == MP_MATCH_REASON_WRONG_PHASE );
			CHECK( session.GetSessionRevision() == revision );
		}
		CHECK( ( withdrawalPhases & ( 1u << phase ) ) != 0 && session.CanSelfLeaveRoster( bench ) );
		mpMatchSession withdrawn = session;
		CHECK( SessionApplied( withdrawn.VacateRosterSeat( 1, withdrawn.GetSessionRevision() ) ) );
		CHECK( SessionApplied( withdrawn.SetParticipantRoles( bench,
			MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), withdrawn.GetSessionRevision() ) ) );
		CHECK( !withdrawn.SetParticipantActive( bench, false, withdrawn.GetSessionRevision() ).WasRejected() );
		CHECK( SessionApplied( withdrawn.SetParticipantSide( bench, MP_MATCH_SIDE_NONE, withdrawn.GetSessionRevision() ) ) );
	}
	return 0;
}

int main() {
	const int authorityResult = InvitationAuthorityAndPhases();
	if ( authorityResult != 0 ) { return authorityResult; }
	mpMatchRoleMask_t mappedRoles = 0;
	const mpMatchRoleMask_t refereeRole = MPMatchRoleBit( MP_MATCH_ROLE_REFEREE );
	CHECK( !MPMatchTeamsAssignRosterRole(
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) | refereeRole,
		MP_MATCH_ROSTER_CAPTAIN, mappedRoles ) );
	CHECK( MPMatchTeamsAssignRosterRole(
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
		MP_MATCH_ROSTER_CAPTAIN, mappedRoles ) );
	CHECK( ( mappedRoles & MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) != 0 );
	CHECK( ( mappedRoles & MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ) ) != 0 );
	CHECK( MPMatchTeamsClearRosterRole( mappedRoles, mappedRoles ) );
	CHECK( mappedRoles == 0 );
	CHECK( !MPMatchTeamsAssignRosterRole( refereeRole,
		MP_MATCH_ROSTER_SUBSTITUTE, mappedRoles ) );

	const uint64_t teamSessionId = 0x100000001ull;
	mpMatchSession session;
	CHECK( InitializeSession( session, teamSessionId, true, 100 ) );
	mpParticipantId issuer;
	mpParticipantId outgoing;
	mpParticipantId incoming;
	mpParticipantId other;
	mpParticipantId observer;
	mpParticipantId bench;
	CHECK( Bind( session, 0, issuer ) );
	CHECK( SessionApplied( session.SetParticipantRoles( issuer, refereeRole,
		session.GetSessionRevision() ) ) );
	CHECK( Bind( session, 1, outgoing ) );
	CHECK( Bind( session, 2, incoming ) );
	CHECK( Bind( session, 3, other ) );
	CHECK( Bind( session, 4, observer ) );
	CHECK( Bind( session, 6, bench ) );
	CHECK( SessionApplied( session.SetParticipantSide( outgoing, 0,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantActive( outgoing, true,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 0, 0,
		MP_MATCH_ROSTER_PLAYER, true, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.AssignRosterSeat( 0, outgoing,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 1, 0,
		MP_MATCH_ROSTER_PLAYER, true, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 2, 0,
		MP_MATCH_ROSTER_CAPTAIN, false, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 3, 0,
		MP_MATCH_ROSTER_SUBSTITUTE, false, session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantSide( bench, 0,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.SetParticipantRoles( bench, 0,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.AssignRosterSeat( 3, bench,
		session.GetSessionRevision() ) ) );
	CHECK( SessionApplied( session.DeclareRosterSeat( 4, 0,
		MP_MATCH_ROSTER_SUBSTITUTE, false, session.GetSessionRevision() ) ) );

	mpMatchTeams teams;
	CHECK( teams.Reset( teamSessionId, mpMatchEngineTime::FromMilliseconds( 100 ) ) );
	CHECK( teams.ValidateInvariants() );
	mpMatchTeamsPolicy_t teamPolicy = TeamPolicy();
	CHECK( teams.JoinQueue( session, bench, 0, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 105 ), teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_INVALID_ROLE );
	CHECK( TeamsApplied( teams.SetSideLocked( session, 0, true,
		teams.GetRevision() ) ) );
	CHECK( teams.IsSideLocked( 0 ) );
	CHECK( teams.EvaluateJoin( session, incoming, 0, 0, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 110 ) ).reason ==
		MP_MATCH_TEAMS_REASON_TEAM_LOCKED );
	const mpMatchTeamsRevision_t beforeBadSide = teams.GetRevision();
	CHECK( teams.SetSideLocked( session, -1, true, teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_INVALID_SIDE );
	CHECK( teams.GetRevision() == beforeBadSide );

	mpMatchRosterInvitationId_t invitationId = 0;
	CHECK( TeamsApplied( teams.IssueRosterInvitation( session, incoming, 0,
		MP_MATCH_ROSTER_PLAYER, issuer, 1000,
		mpMatchEngineTime::FromMilliseconds( 120 ), teams.GetRevision(),
		invitationId ) ) );
	CHECK( invitationId == 1 );
	mpMatchRosterInvitationId_t duplicateId = 0;
	const mpMatchTeamsRevision_t beforeDuplicate = teams.GetRevision();
	CHECK( teams.IssueRosterInvitation( session, incoming, 0,
		MP_MATCH_ROSTER_PLAYER, issuer, 1000,
		mpMatchEngineTime::FromMilliseconds( 121 ), teams.GetRevision(),
		duplicateId ).code == MP_MATCH_TEAMS_MUTATION_NO_CHANGE );
	CHECK( duplicateId == invitationId && teams.GetRevision() == beforeDuplicate );

	mpMatchTeamsRecipientSnapshot_t ownView;
	mpMatchTeamsRecipientSnapshot_t otherView;
	CHECK( teams.BuildRecipientSnapshot( session, incoming,
		mpMatchEngineTime::FromMilliseconds( 122 ), ownView ) );
	CHECK( teams.BuildRecipientSnapshot( session, other,
		mpMatchEngineTime::FromMilliseconds( 122 ), otherView ) );
	CHECK( ownView.invitationCount == 1 );
	CHECK( ownView.invitations[ 0 ].target == incoming );
	CHECK( otherView.invitationCount == 0 );
	CHECK( ownView.queueCount == 0 && ownView.sideLocked[ 0 ] );

	mpMatchTeamsJoinDecision_t wrongTarget =
		teams.PlanRosterInvitationAcceptance( session, other, invitationId,
		teamPolicy, mpMatchEngineTime::FromMilliseconds( 123 ) );
	CHECK( wrongTarget.reason == MP_MATCH_TEAMS_REASON_INVITATION_TARGET_MISMATCH );
	mpMatchTeamsJoinDecision_t acceptance =
		teams.PlanRosterInvitationAcceptance( session, incoming, invitationId,
		teamPolicy, mpMatchEngineTime::FromMilliseconds( 123 ) );
	CHECK( acceptance.IsAllowed() );
	CHECK( acceptance.plan.kind == MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE );
	CHECK( acceptance.plan.consumeInvitation && acceptance.plan.assignRosterSeat );
	CHECK( acceptance.plan.rosterSeat == 1 && acceptance.plan.side == 0 );

	// A session mutation invalidates the plan without consuming its invitation.
	mpParticipantId lateJoiner;
	CHECK( Bind( session, 5, lateJoiner ) );
	const mpMatchTeamsRevision_t beforeStalePlan = teams.GetRevision();
	CHECK( teams.CommitTransactionPlan( acceptance.plan, session, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 124 ), teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_STALE_SESSION_REVISION );
	CHECK( teams.GetRevision() == beforeStalePlan );
	CHECK( teams.FindInvitation( invitationId ) != 0 );
	acceptance = teams.PlanRosterInvitationAcceptance( session, incoming,
		invitationId, teamPolicy, mpMatchEngineTime::FromMilliseconds( 124 ) );
	CHECK( acceptance.IsAllowed() );
	CHECK( TeamsApplied( teams.CommitTransactionPlan( acceptance.plan, session,
		teamPolicy, mpMatchEngineTime::FromMilliseconds( 124 ),
		teams.GetRevision() ) ) );
	CHECK( teams.FindInvitation( invitationId ) == 0 );
	CHECK( teams.CommitTransactionPlan( acceptance.plan, session, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 125 ), teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );

	// Substitute invitations reserve a declared bench seat and never activate
	// their recipient merely for joining the roster.
	mpMatchRosterInvitationId_t benchInvitationId = 0;
	CHECK( TeamsApplied( teams.IssueRosterInvitation( session, observer, 0,
		MP_MATCH_ROSTER_SUBSTITUTE, issuer, 1000,
		mpMatchEngineTime::FromMilliseconds( 126 ), teams.GetRevision(),
		benchInvitationId ) ) );
	mpMatchTeamsJoinDecision_t benchAcceptance =
		teams.PlanRosterInvitationAcceptance( session, observer,
			benchInvitationId, teamPolicy,
			mpMatchEngineTime::FromMilliseconds( 127 ) );
	CHECK( benchAcceptance.IsAllowed() );
	CHECK( benchAcceptance.plan.rosterRole == MP_MATCH_ROSTER_SUBSTITUTE &&
		benchAcceptance.plan.rosterSeat == 4 &&
		!benchAcceptance.plan.incomingActive &&
		!benchAcceptance.plan.setIncomingActive );
	CHECK( TeamsApplied( teams.CommitTransactionPlan( benchAcceptance.plan,
		session, teamPolicy, mpMatchEngineTime::FromMilliseconds( 127 ),
		teams.GetRevision() ) ) );

	// A rostered substitute is durable bench membership.  It satisfies strict
	// substitution authorization and produces a two-seat atomic swap plan.
	mpMatchTeamsPolicy_t strictTeamPolicy = teamPolicy;
	strictTeamPolicy.requireRosterMembership = true;
	strictTeamPolicy.requireInvitationForSubstitution = true;
	mpMatchTeamsJoinDecision_t benchSwap = teams.PlanSubstitution( session,
		outgoing, bench, 0, 0, strictTeamPolicy,
		mpMatchEngineTime::FromMilliseconds( 128 ) );
	CHECK( benchSwap.IsAllowed() );
	CHECK( benchSwap.plan.IsPersistentBenchSwap() );
	CHECK( benchSwap.plan.rosterSeat == 0 &&
		benchSwap.plan.outgoingRosterSeat == 3 );
	CHECK( benchSwap.plan.rosterRole == MP_MATCH_ROSTER_PLAYER &&
		benchSwap.plan.outgoingRosterRole == MP_MATCH_ROSTER_SUBSTITUTE );
	CHECK( benchSwap.plan.vacateRosterSeat &&
		benchSwap.plan.vacateOutgoingRosterSeat &&
		benchSwap.plan.assignRosterSeat &&
		benchSwap.plan.assignOutgoingRosterSeat &&
		benchSwap.plan.assignOutgoingRosterRole &&
		!benchSwap.plan.clearOutgoingRosterRole &&
		!benchSwap.plan.setOutgoingSide );
	CHECK( teams.PlanSubstitution( session, outgoing, other, 0, 0,
		strictTeamPolicy, mpMatchEngineTime::FromMilliseconds( 128 ) ).reason ==
		MP_MATCH_TEAMS_REASON_ROSTER_REQUIRED );
	mpMatchTeamsTransactionPlan_t forgedBenchSwap = benchSwap.plan;
	forgedBenchSwap.outgoingRosterSeat = 4;
	const mpMatchTeamsRevision_t beforeForgedBench = teams.GetRevision();
	CHECK( teams.CommitTransactionPlan( forgedBenchSwap, session,
		strictTeamPolicy, mpMatchEngineTime::FromMilliseconds( 128 ),
		teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
	CHECK( teams.GetRevision() == beforeForgedBench );
	CHECK( TeamsApplied( teams.CommitTransactionPlan( benchSwap.plan, session,
		strictTeamPolicy, mpMatchEngineTime::FromMilliseconds( 128 ),
		teams.GetRevision() ) ) );
	mpMatchSession swappedSession = session;
	CHECK( ApplyPlanToSessionCopy( swappedSession, benchSwap.plan ) );
	CHECK( swappedSession.GetRosterSeat( 0 )->occupant == bench );
	CHECK( swappedSession.GetRosterSeat( 3 )->occupant == outgoing );
	const mpMatchParticipantState *promoted = swappedSession.FindParticipant( bench );
	const mpMatchParticipantState *benched = swappedSession.FindParticipant( outgoing );
	CHECK( promoted != NULL && promoted->active && promoted->side == 0 &&
		( promoted->roles & MPMatchRosterPrincipalRoleMask() ) ==
			MPMatchPrincipalRolesForRosterRole( MP_MATCH_ROSTER_PLAYER ) );
	CHECK( benched != NULL && !benched->active && benched->side == 0 &&
		( benched->roles & MPMatchRosterPrincipalRoleMask() ) == 0 );

	// The looser policy preserves an explicit emergency-unrostered path.
	mpMatchTeamsJoinDecision_t substitution = teams.PlanSubstitution( session,
		outgoing, other, 0, 0, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 130 ) );
	CHECK( substitution.IsAllowed() );
	CHECK( !substitution.plan.IsPersistentBenchSwap() );
	CHECK( substitution.plan.kind == MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION );
	CHECK( substitution.plan.vacateRosterSeat &&
		substitution.plan.assignRosterSeat );
	CHECK( substitution.plan.rosterSeat == 0 );
	mpMatchTeamsTransactionPlan_t forged = substitution.plan;
	forged.rosterSeat = 1;
	const mpMatchTeamsRevision_t beforeForged = teams.GetRevision();
	CHECK( teams.CommitTransactionPlan( forged, session, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 130 ), teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE );
	CHECK( teams.GetRevision() == beforeForged );
	CHECK( TeamsApplied( teams.CommitTransactionPlan( substitution.plan, session,
		teamPolicy, mpMatchEngineTime::FromMilliseconds( 130 ),
		teams.GetRevision() ) ) );
	const mpMatchTeamsRevision_t beforeClockSample = teams.GetRevision();
	CHECK( teams.ExpireRosterInvitations( session,
		mpMatchEngineTime::FromMilliseconds( 135 ), teams.GetRevision() ).code ==
		MP_MATCH_TEAMS_MUTATION_NO_CHANGE );
	CHECK( teams.GetRevision() == beforeClockSample );
	CHECK( teams.GetLastEngineTime() ==
		mpMatchEngineTime::FromMilliseconds( 135 ) );
	mpMatchRosterInvitationId_t rejectedRollbackId = 0;
	CHECK( teams.IssueRosterInvitation( session, other, 0,
		MP_MATCH_ROSTER_PLAYER, issuer, 10,
		mpMatchEngineTime::FromMilliseconds( 134 ), teams.GetRevision(),
		rejectedRollbackId ).reason == MP_MATCH_TEAMS_REASON_CLOCK_REGRESSION );
	CHECK( rejectedRollbackId == 0 && teams.GetRevision() == beforeClockSample );

	// Expiry is engine-time based, exact at the deadline and rollback-safe.
	mpMatchRosterInvitationId_t expiringId = 0;
	CHECK( TeamsApplied( teams.IssueRosterInvitation( session, other, 0,
		MP_MATCH_ROSTER_PLAYER, issuer, 10,
		mpMatchEngineTime::FromMilliseconds( 140 ), teams.GetRevision(),
		expiringId ) ) );
	CHECK( teams.PlanRosterInvitationAcceptance( session, other, expiringId,
		teamPolicy, mpMatchEngineTime::FromMilliseconds( 150 ) ).reason ==
		MP_MATCH_TEAMS_REASON_INVITATION_EXPIRED );
	CHECK( TeamsApplied( teams.ExpireRosterInvitations( session,
		mpMatchEngineTime::FromMilliseconds( 150 ), teams.GetRevision() ) ) );
	CHECK( teams.FindInvitation( expiringId ) == 0 );
	const mpMatchTeamsRevision_t beforeClockRegression = teams.GetRevision();
	CHECK( teams.ExpireRosterInvitations( session,
		mpMatchEngineTime::FromMilliseconds( 149 ), teams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_CLOCK_REGRESSION );
	CHECK( teams.GetRevision() == beforeClockRegression );

	// Even if disconnect cleanup is delayed, an issuer-bound invitation fails
	// closed and is omitted from the recipient projection.
	mpMatchRosterInvitationId_t staleIssuerInvite = 0;
	CHECK( TeamsApplied( teams.IssueRosterInvitation( session, observer, 0,
		MP_MATCH_ROSTER_PLAYER, issuer, 100,
		mpMatchEngineTime::FromMilliseconds( 160 ), teams.GetRevision(),
		staleIssuerInvite ) ) );
	uint32_t issuerGeneration = 0;
	CHECK( session.GetSlotGeneration( 0, issuerGeneration ) );
	CHECK( SessionApplied( session.UnbindParticipant( 0, issuerGeneration,
		session.GetSessionRevision() ) ) );
	CHECK( teams.PlanRosterInvitationAcceptance( session, observer,
		staleIssuerInvite, teamPolicy,
		mpMatchEngineTime::FromMilliseconds( 161 ) ).reason ==
		MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE );
	CHECK( teams.BuildRecipientSnapshot( session, observer,
		mpMatchEngineTime::FromMilliseconds( 161 ), ownView ) );
	CHECK( ownView.invitationCount == 0 );
	CHECK( TeamsApplied( teams.RemoveParticipant( teamSessionId, issuer,
		mpMatchEngineTime::FromMilliseconds( 162 ), teams.GetRevision() ) ) );
	CHECK( teams.FindInvitation( staleIssuerInvite ) == 0 );

	// Stable global FIFO: no late join bypass, defer mints a new tail ticket,
	// and only the head can receive an automatic admission plan.
	const uint64_t duelSessionId = 0x200000001ull;
	mpMatchSession duel;
	CHECK( InitializeSession( duel, duelSessionId, false, 1000 ) );
	mpParticipantId active;
	mpParticipantId queuedA;
	mpParticipantId queuedB;
	mpParticipantId queuedC;
	mpParticipantId outsider;
	CHECK( Bind( duel, 0, active ) );
	CHECK( Bind( duel, 1, queuedA ) );
	CHECK( Bind( duel, 2, queuedB ) );
	CHECK( Bind( duel, 3, queuedC ) );
	CHECK( Bind( duel, 4, outsider ) );
	CHECK( SessionApplied( duel.SetParticipantActive( active, true,
		duel.GetSessionRevision() ) ) );
	mpMatchTeams duelTeams;
	CHECK( duelTeams.Reset( duelSessionId,
		mpMatchEngineTime::FromMilliseconds( 1000 ) ) );
	mpMatchTeamsPolicy_t duelPolicy = DuelPolicy();
	CHECK( TeamsApplied( duelTeams.JoinQueue( duel, queuedA,
		MP_MATCH_SIDE_NONE, duelPolicy, mpMatchEngineTime::FromMilliseconds( 1010 ),
		duelTeams.GetRevision() ) ) );
	CHECK( TeamsApplied( duelTeams.JoinQueue( duel, queuedB,
		MP_MATCH_SIDE_NONE, duelPolicy, mpMatchEngineTime::FromMilliseconds( 1020 ),
		duelTeams.GetRevision() ) ) );
	CHECK( TeamsApplied( duelTeams.JoinQueue( duel, queuedC,
		MP_MATCH_SIDE_NONE, duelPolicy, mpMatchEngineTime::FromMilliseconds( 1030 ),
		duelTeams.GetRevision() ) ) );
	CHECK( duelTeams.GetQueueCount() == 3 );
	CHECK( duelTeams.GetQueueEntry( 0 )->participant == queuedA );
	CHECK( duelTeams.PlanNextQueueAdmission( duel, duelPolicy,
		mpMatchEngineTime::FromMilliseconds( 1030 ) ).disposition ==
		MP_MATCH_TEAMS_JOIN_QUEUE );
	CHECK( duelTeams.EvaluateJoin( duel, outsider, MP_MATCH_SIDE_NONE, 0,
		duelPolicy, mpMatchEngineTime::FromMilliseconds( 1030 ) ).reason ==
		MP_MATCH_TEAMS_REASON_QUEUE_WAIT );

	CHECK( SessionApplied( duel.SetParticipantActive( active, false,
		duel.GetSessionRevision() ) ) );
	mpMatchTeamsJoinDecision_t promotion = duelTeams.PlanNextQueueAdmission(
		duel, duelPolicy, mpMatchEngineTime::FromMilliseconds( 1040 ) );
	CHECK( promotion.IsAllowed() );
	CHECK( promotion.plan.incomingParticipant == queuedA );
	CHECK( promotion.plan.kind == MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION );
	CHECK( promotion.plan.queueTicketId == duelTeams.GetQueueEntry( 0 )->ticketId );
	// Mutating the session makes the plan stale while preserving queue order.
	mpParticipantId later;
	CHECK( Bind( duel, 5, later ) );
	CHECK( duelTeams.CommitTransactionPlan( promotion.plan, duel, duelPolicy,
		mpMatchEngineTime::FromMilliseconds( 1040 ),
		duelTeams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_STALE_SESSION_REVISION );
	CHECK( duelTeams.GetQueueEntry( 0 )->participant == queuedA );
	promotion = duelTeams.PlanNextQueueAdmission( duel, duelPolicy,
		mpMatchEngineTime::FromMilliseconds( 1040 ) );
	CHECK( TeamsApplied( duelTeams.CommitTransactionPlan( promotion.plan, duel,
		duelPolicy, mpMatchEngineTime::FromMilliseconds( 1040 ),
		duelTeams.GetRevision() ) ) );
	CHECK( duelTeams.GetQueueCount() == 2 );
	CHECK( duelTeams.GetQueueEntry( 0 )->participant == queuedB );

	const mpMatchQueueTicketId_t oldHeadTicket =
		duelTeams.GetQueueEntry( 0 )->ticketId;
	const mpMatchQueueTicketId_t oldTailTicket =
		duelTeams.GetQueueEntry( 1 )->ticketId;
	CHECK( TeamsApplied( duelTeams.DeferQueue( duel, queuedB, oldHeadTicket,
		mpMatchEngineTime::FromMilliseconds( 1050 ), duelTeams.GetRevision() ) ) );
	CHECK( duelTeams.GetQueueEntry( 0 )->participant == queuedC );
	CHECK( duelTeams.GetQueueEntry( 1 )->participant == queuedB );
	CHECK( duelTeams.GetQueueEntry( 1 )->ticketId > oldTailTicket );
	CHECK( duelTeams.LeaveQueue( duelSessionId, queuedB, oldHeadTicket,
		duelTeams.GetRevision() ).reason ==
		MP_MATCH_TEAMS_REASON_QUEUE_TICKET_MISMATCH );
	CHECK( TeamsApplied( duelTeams.LeaveQueue( duelSessionId, queuedB,
		duelTeams.GetQueueEntry( 1 )->ticketId, duelTeams.GetRevision() ) ) );
	CHECK( duelTeams.GetQueueCount() == 1 );
	CHECK( duelTeams.ValidateInvariants() );

	// Disconnect cleanup is one bounded mutation and removes both target- and
	// issuer-bound authority without relying on a slot or display name.
	CHECK( TeamsApplied( duelTeams.RemoveParticipant( duelSessionId, queuedC,
		mpMatchEngineTime::FromMilliseconds( 1060 ), duelTeams.GetRevision() ) ) );
	CHECK( duelTeams.GetQueueCount() == 0 );
	CHECK( duelTeams.ValidateInvariants() );
	return 0;
}
'''


def locate_msvc() -> tuple[Path, str] | None:
    if os.name != "nt":
        return None
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / (
        "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if not vswhere.is_file():
        return None
    query = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
    )
    install = query.stdout.strip()
    if not install:
        return None
    devcmd = Path(install) / "Common7/Tools/VsDevCmd.bat"
    return (devcmd, "x64") if devcmd.is_file() else None


def executable_contract() -> None:
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    # The Windows ARM64 runners refuse to delete the just-run harness executable
    # (WinError 5) after the checks pass; a leftover file in .tmp must not turn
    # that pass into a failure.
    with tempfile.TemporaryDirectory(prefix="match-teams-", dir=temp_root, ignore_cleanup_errors=True) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_teams_contract.cpp"
        executable = temp_dir / "match_teams_contract.exe"
        harness_text = HARNESS
        protocol = read(MATCH_DIR / "MatchProtocol.cpp")
        for operation in ("ROSTER_ACCEPT", "ROLE_ASSIGN", "ROSTER_SUBSTITUTE", "ROSTER_LEAVE"):
            descriptor = re.search(
                rf"\{{\s*MP_MATCH_OP_{operation},(.*?)\}},",
                protocol,
                re.DOTALL,
            )
            if descriptor is None:
                raise AssertionError(f"missing production descriptor phases for {operation}")
            phases = descriptor[1].split(",")[4].strip()
            harness_text = harness_text.replace(f"@{operation}_PHASES@", phases)
        harness.write_text(harness_text, encoding="utf-8")

        msvc = locate_msvc()
        if msvc is not None:
            devcmd, architecture = msvc
            compile_script = temp_dir / "compile_msvc.cmd"
            compile_script.write_text(
                "@echo off\n"
                f'call "{devcmd}" -arch={architecture} '
                f'-host_arch={architecture} >nul\n'
                "if errorlevel 1 exit /b %errorlevel%\n"
                "cl.exe /nologo /std:c++17 /EHsc /W4 "
                "/DMP_MATCH_TEAMS_STANDALONE_TEST "
                "/DMP_MATCH_SESSION_STANDALONE_TEST "
                f'/I"{ROOT / "src"}" "{harness}" "{SOURCE}" '
                f'"{SESSION_SOURCE}" /Fe:"{executable}"\n',
                encoding="utf-8",
            )
            compiled = subprocess.run(
                [os.environ.get("ComSpec", "cmd.exe"), "/d", "/c", str(compile_script)],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )
            compiler_name = "MSVC"
        else:
            compiler = next(
                (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
                None,
            )
            if compiler is None:
                print("mp_match_teams_contract: executable checks skipped (no C++ compiler)")
                return
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-DMP_MATCH_TEAMS_STANDALONE_TEST",
                    "-DMP_MATCH_SESSION_STANDALONE_TEST",
                    f"-I{ROOT / 'src'}",
                    str(harness),
                    str(SOURCE),
                    str(SESSION_SOURCE),
                    "-o",
                    str(executable),
                ],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )
            compiler_name = Path(compiler).name
        if compiled.returncode != 0:
            raise AssertionError(
                f"standalone team-management contract did not compile with {compiler_name}:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=temp_dir, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"team-management invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def source_discovery_contract() -> None:
    lister = ROOT / "tools/build/list_sources.py"
    listed = subprocess.run(
        [
            os.environ.get("PYTHON", "python"),
            str(lister),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if listed.returncode != 0:
        raise AssertionError("source discovery failed:\n" + listed.stdout + listed.stderr)
    if "mpgame/mp/match/MatchTeams.cpp" not in listed.stdout.replace("\\", "/"):
        raise AssertionError("MatchTeams.cpp is not discovered by the GameLib source glob")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    source_discovery_contract()
    executable_contract()
    print("mp_match_teams_contract: PASS")


if __name__ == "__main__":
    main()
