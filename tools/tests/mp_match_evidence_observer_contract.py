#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for committed match-evidence observation."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
HEADER = MATCH_DIR / "MatchEvidenceObserver.h"
SOURCE = MATCH_DIR / "MatchEvidenceObserver.cpp"
EVIDENCE_SOURCE = MATCH_DIR / "MatchEvidence.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str, evidence_source: str) -> None:
    combined = header + source
    for dependency in (
        "Game_local.h",
        "MultiplayerGame.h",
        "idFile",
        "idBitMsg",
        "idCVar",
        "idUserInterface",
        "idList<",
        "std::vector",
        "std::string",
        "gameLocal",
        "fileSystem",
        "networkSystem",
    ):
        if dependency in combined:
            raise AssertionError(
                f"evidence observer contains forbidden adapter dependency {dependency!r}"
            )
    if re.search(r"\bnew\s+", combined) or re.search(r"\bdelete\s+", combined):
        raise AssertionError("evidence observer must remain allocation-free")

    for token in (
        "participants[ MP_MATCH_MAX_PARTICIPANTS ]",
        "roster[ MP_MATCH_MAX_ROSTER_SEATS ]",
        "proposal[ MP_PROPOSAL_SCOPE_COUNT ]",
        "for ( int rawScope = 0; rawScope < MP_PROPOSAL_SCOPE_COUNT; ++rawScope )",
        "EvidencePauseState",
        "EvidencePauseKind",
        "case MP_MATCH_ROSTER_SUBSTITUTE: return MP_EVIDENCE_ROSTER_SUBSTITUTE;",
        "evidence.GetMetadata().sessionId != sessionId",
        "stamp.sessionRevision != session.GetSessionRevision()",
        "current->castCount != 0",
        "MP_MATCH_EVIDENCE_OBSERVER_STANDALONE_TEST",
    ):
        require(combined, token, "bounded observer behavior")
    if "static_cast<mpEvidencePause" in source:
        raise AssertionError("persisted pause enums must be mapped explicitly")
    require(
        evidence_source,
        'case MP_EVIDENCE_RESULT_DRAW: return "draw";',
        "canonical draw serialization",
    )


HARNESS = r'''
#include "game/mp/match/MatchEvidenceObserver.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

static unsigned long long allocationCount = 0;

void *operator new( std::size_t size ) {
	++allocationCount;
	void *result = std::malloc( size == 0 ? 1 : size );
	if ( result == NULL ) {
		std::abort();
	}
	return result;
}

void *operator new[]( std::size_t size ) {
	++allocationCount;
	void *result = std::malloc( size == 0 ? 1 : size );
	if ( result == NULL ) {
		std::abort();
	}
	return result;
}

void operator delete( void *value ) noexcept {
	std::free( value );
}

void operator delete[]( void *value ) noexcept {
	std::free( value );
}

void operator delete( void *value, std::size_t ) noexcept {
	std::free( value );
}

void operator delete[]( void *value, std::size_t ) noexcept {
	std::free( value );
}

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static const mpMatchOperationDescriptor_t TEST_DESCRIPTOR = {
	MP_MATCH_OP_RULES_COMMIT, "rules_commit", MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT,
	MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT, MP_MATCH_PROTOCOL_CAP_RULES_COMMIT,
	MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON,
	MP_MATCH_OPERATION_FLAG_PROPOSABLE, MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0
};

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor(
		mpMatchOperationOpcode_t opcode ) {
	return opcode == MP_MATCH_OP_RULES_COMMIT ? &TEST_DESCRIPTOR : NULL;
}

bool MPMatchProtocolValidateRequest( const mpMatchOperationRequest_t &request,
		mpMatchProtocolError_t *error ) {
	if ( error != NULL ) {
		std::memset( error, 0, sizeof( *error ) );
	}
	return request.schemaVersion == MP_MATCH_PROTOCOL_SCHEMA_VERSION &&
		request.sessionId != 0 && request.requestId != 0 &&
		request.actorSlot < MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS &&
		request.actorBindingGeneration != 0 && request.argumentCount == 0 &&
		!request.hasParticipantTarget && !request.hasTeamTarget &&
		request.opcode == MP_MATCH_OP_RULES_COMMIT;
}

static bool AppliedOnce( const mpMatchMutationResult &result ) {
	return result.WasApplied() &&
		result.currentRevision == result.previousRevision + 1;
}

static bool ProposalAppliedOnce( const mpProposalMutationResult_t &result ) {
	return result.WasApplied() &&
		result.currentRevision == result.previousRevision + 1;
}

static mpEvidenceCommittedStamp Stamp( const mpMatchSession &session ) {
	mpEvidenceCommittedStamp stamp;
	stamp.sessionRevision = session.GetSessionRevision();
	stamp.matchTimeMsec = static_cast<uint64_t>(
		session.GetMatchTime().Milliseconds() );
	stamp.hostTimeUtcMsec = 1785780000000ULL + stamp.sessionRevision;
	return stamp;
}

static bool ResetWithoutAllocation( mpMatchEvidenceObserver &observer,
		const mpMatchSession &session, const mpProposalService &proposals ) {
	const unsigned long long before = allocationCount;
	observer.Reset( session, proposals );
	return allocationCount == before;
}

static bool ObserveWithoutAllocation( mpMatchEvidenceObserver &observer,
		mpMatchEvidence &evidence, const mpEvidenceCommittedStamp &stamp,
		const mpMatchSession &session, const mpProposalService &proposals,
		mpEvidenceActorRef actor ) {
	const unsigned long long before = allocationCount;
	observer.Observe( evidence, stamp, session, proposals, actor );
	return allocationCount == before;
}

static int CountKind( const mpMatchEvidence &evidence,
		mpEvidenceEventKind_t kind ) {
	int count = 0;
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		const mpEvidenceEvent *event = evidence.GetEvent( index );
		if ( event != NULL && event->kind == kind ) {
			++count;
		}
	}
	return count;
}

static int CountProposalAction( const mpMatchEvidence &evidence,
		mpEvidenceProposalAction_t action ) {
	int count = 0;
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		const mpEvidenceEvent *event = evidence.GetEvent( index );
		if ( event != NULL && event->kind == MP_EVIDENCE_EVENT_PROPOSAL &&
			event->data.proposal.action == action ) {
			++count;
		}
	}
	return count;
}

static const mpEvidenceEvent *LastKind( const mpMatchEvidence &evidence,
		mpEvidenceEventKind_t kind ) {
	for ( int index = evidence.GetEventCount() - 1; index >= 0; --index ) {
		const mpEvidenceEvent *event = evidence.GetEvent( index );
		if ( event != NULL && event->kind == kind ) {
			return event;
		}
	}
	return NULL;
}

static mpMatchOperationRequest_t Operation( uint64_t sessionId,
		unsigned int requestId, unsigned short actorSlot,
		unsigned int actorBindingGeneration ) {
	mpMatchOperationRequest_t operation;
	std::memset( &operation, 0, sizeof( operation ) );
	operation.schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	operation.sessionId = sessionId;
	operation.requestId = requestId;
	operation.opcode = MP_MATCH_OP_RULES_COMMIT;
	operation.expectedSessionRevision = 1;
	operation.actorSlot = actorSlot;
	operation.actorBindingGeneration = actorBindingGeneration;
	operation.teamTarget = MP_MATCH_TEAM_NONE;
	return operation;
}

static mpProposalCreateParams_t Proposal( uint64_t sessionId,
		mpProposalId_t proposalId, mpProposalScope_t scope,
		unsigned short actorSlot, unsigned int actorBindingGeneration,
		unsigned int caller ) {
	mpProposalCreateParams_t params;
	params.Clear();
	params.sessionId = sessionId;
	params.proposalId = proposalId;
	params.scope = scope;
	params.electorateCount = 3;
	params.electorate[ 0 ].participant = caller;
	params.electorate[ 0 ].human = true;
	params.electorate[ 1 ].participant = caller + 1;
	params.electorate[ 1 ].human = true;
	params.electorate[ 2 ].participant = caller + 2;
	params.electorate[ 2 ].human = true;
	params.requiredQuorum = 2;
	params.requiredYes = 2;
	params.createdAt = mpProposalEngineTime::FromMilliseconds( 10 );
	params.expiresAt = mpProposalEngineTime::FromMilliseconds( 1000 );
	params.caller = caller;
	params.callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_YES;
	params.operation = Operation( sessionId,
		static_cast<unsigned int>( proposalId ), actorSlot,
		actorBindingGeneration );
	return params;
}

int main() {
	const uint64_t sessionId = 0x123456789abcdef0ULL;
	mpMatchSession session;
	CHECK( session.Reset( sessionId, mpMatchEngineTime::FromMilliseconds( 0 ) ) );

	mpProposalCooldownPolicy_t cooldowns;
	cooldowns.Clear();
	mpProposalService proposals;
	CHECK( proposals.Reset( sessionId,
		mpProposalEngineTime::FromMilliseconds( 0 ), cooldowns ) );

	mpEvidenceMetadataInput metadata;
	metadata.sessionId = sessionId;
	metadata.seriesId = 0;
	metadata.rulesDigest = 0x8877665544332211ULL;
	metadata.modeId = 4;
	metadata.build = "observer-contract";
	metadata.map = "maps/mp/observer";
	metadata.mode = "tdm";
	mpMatchEvidence evidence;
	CHECK( evidence.Reset( metadata ) );

	mpParticipantId participant;
	CHECK( AppliedOnce( session.BindParticipant( 3, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		participant ) ) );
	mpParticipantId coachParticipant;
	CHECK( AppliedOnce( session.BindParticipant( 4, true,
		MPMatchRoleBit( MP_MATCH_ROLE_COACH ), session.GetSessionRevision(),
		coachParticipant ) ) );
	uint32_t generation = 0;
	CHECK( session.GetSlotGeneration( 3, generation ) );

	mpMatchEvidenceObserver observer;
	CHECK( ResetWithoutAllocation( observer, session, proposals ) );
	CHECK( evidence.GetEventCount() == 0 );

	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, participant,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_PHASE_TRANSITION ) == 1 );
	const mpEvidenceEvent *phase = LastKind(
		evidence, MP_EVIDENCE_EVENT_PHASE_TRANSITION );
	CHECK( phase != NULL && phase->data.phase.from == INACTIVE &&
		phase->data.phase.to == WARMUP &&
		phase->data.phase.actor.kind == MP_EVIDENCE_ACTOR_SERVER_OPERATOR );

	const mpMatchRoleMask_t captainRoles =
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN );
	CHECK( AppliedOnce( session.SetParticipantRoles( participant, captainRoles,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE ) == 1 );
	const mpEvidenceEvent *role = LastKind( evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE );
	CHECK( role != NULL &&
		role->data.role.targetParticipant == participant.SequencePart() &&
		role->data.role.previousRoles == MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) &&
		role->data.role.currentRoles == captainRoles );

	mpMatchReadinessPolicy readiness;
	readiness.policy = MP_MATCH_READY_DISABLED;
	readiness.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	readiness.teamMode = true;
	readiness.minimumActiveHumans = 0;
	readiness.readyThresholdBasisPoints = 0;
	readiness.maximumActivePerSide = 0;
	readiness.requiredSideMask = 0;
	readiness.requireDeclaredRosterSeats = false;
	CHECK( AppliedOnce( session.ConfigureReadiness( readiness,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( participant, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantActive( participant, true,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.SetParticipantSide( coachParticipant, 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 0, 0,
		MP_MATCH_ROSTER_CAPTAIN, false, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 0, participant,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_ROSTER_CHANGE ) == 2 );
	CHECK( evidence.GetEvent( evidence.GetEventCount() - 2 )->data.roster.action ==
		MP_EVIDENCE_ROSTER_SEAT_DECLARED );
	const mpEvidenceEvent *roster = LastKind(
		evidence, MP_EVIDENCE_EVENT_ROSTER_CHANGE );
	CHECK( roster != NULL && roster->data.roster.action ==
		MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED &&
		roster->data.roster.side == 0 &&
		roster->data.roster.role == MP_EVIDENCE_ROSTER_CAPTAIN &&
		roster->data.roster.participant == participant.SequencePart() );
	const int beforeRosterReplacement = evidence.GetEventCount();
	CHECK( AppliedOnce( session.VacateRosterSeat( 0,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.DeclareRosterSeat( 0, 0,
		MP_MATCH_ROSTER_COACH, false, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.AssignRosterSeat( 0, coachParticipant,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( evidence.GetEventCount() == beforeRosterReplacement + 4 );
	static const mpEvidenceRosterAction_t replacementActions[] = {
		MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED,
		MP_EVIDENCE_ROSTER_SEAT_CLEARED,
		MP_EVIDENCE_ROSTER_SEAT_DECLARED,
		MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED
	};
	for ( int index = 0; index < 4; ++index ) {
		const mpEvidenceEvent *event = evidence.GetEvent(
			beforeRosterReplacement + index );
		CHECK( event != NULL && event->kind == MP_EVIDENCE_EVENT_ROSTER_CHANGE &&
			event->data.roster.action == replacementActions[ index ] );
	}
	CHECK( evidence.GetEvent( beforeRosterReplacement )->data.roster.participant ==
		participant.SequencePart() );
	const mpEvidenceEvent *coachAssigned = evidence.GetEvent(
		beforeRosterReplacement + 3 );
	CHECK( coachAssigned->data.roster.role == MP_EVIDENCE_ROSTER_COACH &&
		coachAssigned->data.roster.participant == coachParticipant.SequencePart() );

	CHECK( AppliedOnce( session.FreezeRules( 1, metadata.rulesDigest,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.ConfigureTimeouts( 1, 1000, false, 100,
		MP_MATCH_RESUME_OWNER_OR_AUTHORITY, session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_REFEREE_FORCE_READY, participant,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( GAMEON,
		MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE, participant,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() ) ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_PHASE_TRANSITION ) == 3 );

	CHECK( AppliedOnce( session.RequestTeamTimeout( 0,
		MP_MATCH_PAUSE_REASON_TACTICAL, session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() ) ) );
	CHECK( AppliedOnce( session.AdvanceFrame(
		mpMatchEngineTime::FromMilliseconds( 1 ) ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceSystemActor() ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_PAUSE_TRANSITION ) == 2 );
	const mpEvidenceEvent *pause = LastKind(
		evidence, MP_EVIDENCE_EVENT_PAUSE_TRANSITION );
	CHECK( pause != NULL &&
		pause->data.pause.from == MP_EVIDENCE_PAUSE_PENDING &&
		pause->data.pause.to == MP_EVIDENCE_PAUSED &&
		pause->data.pause.kind == MP_EVIDENCE_PAUSE_TEAM_TIMEOUT &&
		pause->data.pause.ownerSide == 0 &&
		pause->data.pause.reason == MP_MATCH_PAUSE_REASON_TACTICAL );

	const mpProposalId_t globalId = 0x100000001ULL;
	const mpProposalId_t teamAId = 0x200000002ULL;
	const mpProposalId_t teamBId = 0x300000003ULL;
	CHECK( ProposalAppliedOnce( proposals.Create( Proposal( sessionId, globalId,
		MP_PROPOSAL_SCOPE_GLOBAL, 3, generation, participant.SequencePart() ),
		proposals.GetRevision() ) ) );
	CHECK( ProposalAppliedOnce( proposals.Create( Proposal( sessionId, teamAId,
		MP_PROPOSAL_SCOPE_TEAM_A, 3, generation, participant.SequencePart() ),
		proposals.GetRevision() ) ) );
	CHECK( ProposalAppliedOnce( proposals.Create( Proposal( sessionId, teamBId,
		MP_PROPOSAL_SCOPE_TEAM_B, 3, generation, participant.SequencePart() ),
		proposals.GetRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() ) ) );
	CHECK( CountProposalAction( evidence, MP_EVIDENCE_PROPOSAL_CREATED ) == 3 );
	CHECK( CountProposalAction( evidence, MP_EVIDENCE_PROPOSAL_BALLOT_CAST ) == 3 );
	bool sawGlobal = false;
	bool sawTeamA = false;
	bool sawTeamB = false;
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		const mpEvidenceEvent *event = evidence.GetEvent( index );
		if ( event == NULL || event->kind != MP_EVIDENCE_EVENT_PROPOSAL ||
			event->data.proposal.action != MP_EVIDENCE_PROPOSAL_CREATED ) {
			continue;
		}
		sawGlobal = sawGlobal || ( event->data.proposal.proposalId == globalId &&
			event->data.proposal.scopeSide == -1 );
		sawTeamA = sawTeamA || ( event->data.proposal.proposalId == teamAId &&
			event->data.proposal.scopeSide == 0 );
		sawTeamB = sawTeamB || ( event->data.proposal.proposalId == teamBId &&
			event->data.proposal.scopeSide == 1 );
	}
	CHECK( sawGlobal && sawTeamA && sawTeamB );

	CHECK( ProposalAppliedOnce( proposals.CastBallot( sessionId,
		MP_PROPOSAL_SCOPE_GLOBAL, globalId, participant.SequencePart() + 1,
		MP_PROPOSAL_BALLOT_YES, mpProposalEngineTime::FromMilliseconds( 20 ),
		proposals.GetRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() + 1 ) ) );
	CHECK( CountProposalAction( evidence, MP_EVIDENCE_PROPOSAL_BALLOT_CAST ) == 4 );
	CHECK( CountProposalAction( evidence, MP_EVIDENCE_PROPOSAL_PASSED ) == 1 );
	const mpEvidenceEvent *terminal = LastKind(
		evidence, MP_EVIDENCE_EVENT_PROPOSAL );
	CHECK( terminal != NULL && terminal->data.proposal.proposalId == globalId &&
		terminal->data.proposal.action == MP_EVIDENCE_PROPOSAL_PASSED &&
		terminal->data.proposal.actor.kind == MP_EVIDENCE_ACTOR_PARTICIPANT &&
		terminal->data.proposal.actor.participantSequence ==
			participant.SequencePart() + 1 );

	const int beforeDuplicate = evidence.GetEventCount();
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceSystemActor() ) );
	CHECK( evidence.GetEventCount() == beforeDuplicate );

	CHECK( AppliedOnce( session.SetParticipantRoles( participant,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision() ) ) );
	mpEvidenceCommittedStamp staleStamp = Stamp( session );
	--staleStamp.sessionRevision;
	CHECK( ObserveWithoutAllocation( observer, evidence, staleStamp, session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( evidence.GetEventCount() == beforeDuplicate );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( evidence.GetEventCount() == beforeDuplicate + 1 );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE ) == 2 );

	CHECK( AppliedOnce( session.UnbindParticipant( 3, generation,
		session.GetSessionRevision() ) ) );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceParticipantActor( participant.SequencePart() ) ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE ) == 3 );
	const mpEvidenceEvent *removedRole = LastKind(
		evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE );
	CHECK( removedRole != NULL &&
		removedRole->data.role.targetParticipant == participant.SequencePart() &&
		removedRole->data.role.previousRoles == MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) &&
		removedRole->data.role.currentRoles == 0 );
	mpParticipantId replacement;
	CHECK( AppliedOnce( session.BindParticipant( 3, true,
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
		replacement ) ) );
	CHECK( replacement.IsValid() && replacement != participant );
	CHECK( ObserveWithoutAllocation( observer, evidence, Stamp( session ), session,
		proposals, MPEvidenceServerOperatorActor() ) );
	CHECK( CountKind( evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE ) == 4 );
	const mpEvidenceEvent *addedRole = LastKind(
		evidence, MP_EVIDENCE_EVENT_ROLE_CHANGE );
	CHECK( addedRole != NULL &&
		addedRole->data.role.targetParticipant == replacement.SequencePart() &&
		addedRole->data.role.previousRoles == 0 &&
		addedRole->data.role.currentRoles == MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) );

	mpEvidenceMapResult draw;
	std::memset( &draw, 0, sizeof( draw ) );
	draw.outcome = MP_EVIDENCE_RESULT_DRAW;
	draw.winnerSide = -1;
	draw.sideScore[ 0 ] = 7;
	draw.sideScore[ 1 ] = 7;
	draw.reason = MP_MATCH_TRANSITION_LIMIT_REACHED;
	draw.authorizer = MPEvidenceSystemActor();
	CHECK( evidence.AppendMapResult( Stamp( session ), draw ).WasAccepted() );
	char json[ 65536 ];
	const mpEvidenceSerializeResult serialized = evidence.SerializeCanonicalJson(
		json, static_cast<int>( sizeof( json ) ) );
	CHECK( serialized.Succeeded() );
	CHECK( std::strstr( json, "\"proposalId\":4294967297" ) != NULL );
	CHECK( std::strstr( json, "\"outcome\":\"draw\"" ) != NULL );
	CHECK( evidence.ValidateInvariants() );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_evidence_observer_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-evidence-observer-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_evidence_observer_contract.cpp"
        executable = temp_dir / (
            "match_evidence_observer_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_evidence_observer_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-DMP_MATCH_EVIDENCE_OBSERVER_STANDALONE_TEST",
            "-DMP_MATCH_EVIDENCE_STANDALONE_TEST",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            "-DMP_PROPOSAL_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            str(EVIDENCE_SOURCE),
            str(MATCH_DIR / "MatchSession.cpp"),
            str(MATCH_DIR / "MatchProposal.cpp"),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-evidence-observer contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "match-evidence-observer invariant failed at harness line "
                f"{ran.returncode}:\n{ran.stdout}{ran.stderr}"
            )


def main() -> None:
    static_contracts(read(HEADER), read(SOURCE), read(EVIDENCE_SOURCE))
    executable_contract()
    print("mp_match_evidence_observer_contract: PASS")


if __name__ == "__main__":
    main()
