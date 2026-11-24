#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for the authoritative typed proposal service."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchProposal.h"
SOURCE = ROOT / "src/game/mp/match/MatchProposal.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def proposal_symbols(text: str) -> set[str]:
    return set(re.findall(r"\b(?:MP_PROPOSAL_[A-Z0-9_]+|mpProposal[A-Za-z0-9_]*)\b", text))


def static_contracts(header: str, source: str, existing_match_headers: str) -> None:
    require(header, '#include "MatchProtocol.h"', "typed operation dependency")
    for forbidden in (
        "MatchSession.h",
        "Game_local.h",
        "MultiplayerGame.h",
        "idBitMsg",
        "idUserInterface",
        "idCVar",
        "idFile",
        "idList<",
        "idStr",
        "cmdSystem",
        "BufferCommandText",
        "fileSystem",
        "time(",
        "std::chrono",
        "new mpProposal",
    ):
        if forbidden in header + source:
            raise AssertionError(f"proposal value layer contains forbidden dependency {forbidden!r}")

    for token in (
        "MP_PROPOSAL_MAX_ELECTORATE = 32",
        "MP_PROPOSAL_SCOPE_COUNT = 3",
        "MP_PROPOSAL_SCOPE_GLOBAL = 0",
        "MP_PROPOSAL_SCOPE_TEAM_A = 1",
        "MP_PROPOSAL_SCOPE_TEAM_B = 2",
        "mpProposalSessionId_t",
        "mpProposalId_t",
        "mpProposalRevision_t",
        "mpProposalEngineTime",
        "mpProposalCreateParams_t",
        "mpProposalRecord_t",
        "mpProposalService",
        "proposals[ MP_PROPOSAL_SCOPE_COUNT ]",
        "cooldowns[ MP_MATCH_OP_COUNT ]",
        "expectedRevision",
        "requiredQuorum",
        "requiredYes",
        "callerVotePolicy",
        "operationLocalizationId",
        "legalPhaseMask",
        "mpMatchOperationRequest_t operation",
        "InvalidateForPhase",
        "Acknowledge",
        "ValidateInvariants",
    ):
        require(header, token, "fixed proposal schema")

    for token in (
        "MPMatchProtocolValidateRequest( params.operation",
        "MP_MATCH_OPERATION_FLAG_PROPOSABLE",
        "MP_MATCH_OPERATION_FLAG_SENSITIVE",
        "params.operation.sessionId != params.sessionId",
        "params.expiresAt.Milliseconds() - params.createdAt.Milliseconds()",
        "MP_PROPOSAL_REASON_ELECTORATE_DUPLICATE",
        "MP_PROPOSAL_REASON_ELECTORATE_MEMBER_NOT_HUMAN",
        "MP_PROPOSAL_REASON_ALREADY_VOTED",
        "record.yesCount + remaining < record.requiredYes",
        "record.castCount >= record.requiredQuorum",
        "currentCooldown.hasDeadline && params.createdAt < currentCooldown.until",
        "engineNow < lastEngineTime",
        "engineNow >= current.expiresAt",
        "proposals[ params.scope ].IsOccupied()",
        "record.operation = params.operation",
        "record.electorate[ insertion - 1 ].participant > candidate.participant",
    ):
        require(source, token, "frozen deterministic proposal behavior")

    if source.count("++serviceRevision") != 1:
        raise AssertionError("proposal service revision must have exactly one increment owner")
    require(source, "result.currentRevision = serviceRevision", "CAS result revision")
    require(source, "reason = MP_PROPOSAL_REASON_STALE_REVISION", "stale CAS rejection")
    require(source, "nextProposals[ MP_PROPOSAL_SCOPE_COUNT ]", "atomic multi-scope lifecycle")
    require(source, "nextCooldowns[ MP_MATCH_OP_COUNT ]", "atomic cooldown lifecycle")

    # Every symbol introduced by this header lives in the requested namespace.
    enum_blocks = re.findall(r"typedef enum \{(.*?)\}\s*(mpProposal\w+)\s*;", header, re.DOTALL)
    for body, type_name in enum_blocks:
        if not type_name.startswith("mpProposal"):
            raise AssertionError(f"unprefixed proposal enum type {type_name}")
        values = re.findall(r"\b([A-Z][A-Z0-9_]+)\b", body)
        invalid = [value for value in values if not value.startswith("MP_PROPOSAL_")]
        if invalid:
            raise AssertionError(f"unprefixed proposal enum values: {invalid}")

    collisions = proposal_symbols(header) & proposal_symbols(existing_match_headers)
    if collisions:
        raise AssertionError(f"proposal globals collide with existing cores: {sorted(collisions)}")


HARNESS = r'''
#include "game/mp/match/MatchProposal.h"

#include <cstring>

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static const mpMatchOperationDescriptor_t TEST_RULES_DESCRIPTOR = {
	MP_MATCH_OP_RULES_COMMIT, "rules_commit", MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT,
	MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT, MP_MATCH_PROTOCOL_CAP_RULES_COMMIT,
	MP_MATCH_PHASE_WARMUP, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
	MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0
};

static const mpMatchOperationDescriptor_t TEST_RESUME_DESCRIPTOR = {
	MP_MATCH_OP_RESUME_REQUEST, "resume_request", MP_MATCH_LOCALIZATION_OPERATION_RESUME_REQUEST,
	MP_MATCH_LOCALIZATION_NONE, MP_MATCH_PROTOCOL_CAP_RESUME,
	MP_MATCH_PHASE_GAMEON, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
	MP_MATCH_COOLDOWN_INTERACTION, 0, 0
};

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor( mpMatchOperationOpcode_t opcode ) {
	if ( opcode == MP_MATCH_OP_RULES_COMMIT ) {
		return &TEST_RULES_DESCRIPTOR;
	}
	if ( opcode == MP_MATCH_OP_RESUME_REQUEST ) {
		return &TEST_RESUME_DESCRIPTOR;
	}
	return 0;
}

bool MPMatchProtocolValidateRequest( const mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error ) {
	if ( error != 0 ) {
		std::memset( error, 0, sizeof( *error ) );
	}
	return request.schemaVersion == MP_MATCH_PROTOCOL_SCHEMA_VERSION &&
		request.sessionId != 0 && request.requestId != 0 &&
		request.actorSlot < MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS &&
		request.actorBindingGeneration != 0 && request.argumentCount == 0 &&
		!request.hasParticipantTarget && !request.hasTeamTarget &&
		MPMatchOperationDescriptor( request.opcode ) != 0;
}

static mpMatchOperationRequest_t Operation( mpProposalSessionId_t sessionId,
	mpMatchOperationOpcode_t opcode, unsigned int requestId ) {
	mpMatchOperationRequest_t operation;
	std::memset( &operation, 0, sizeof( operation ) );
	operation.schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	operation.sessionId = sessionId;
	operation.requestId = requestId;
	operation.opcode = opcode;
	operation.expectedSessionRevision = 0x100000002ull;
	operation.actorSlot = 1;
	operation.actorBindingGeneration = 7;
	operation.teamTarget = MP_MATCH_TEAM_NONE;
	return operation;
}

static mpProposalCreateParams_t Proposal( mpProposalSessionId_t sessionId,
	mpProposalId_t proposalId, mpProposalScope_t scope,
	mpMatchOperationOpcode_t opcode, int created, int expires,
	int electorateCount, int quorum, int yes,
	mpProposalCallerVotePolicy_t callerPolicy ) {
	mpProposalCreateParams_t params;
	params.Clear();
	params.sessionId = sessionId;
	params.proposalId = proposalId;
	params.scope = scope;
	params.electorateCount = static_cast<unsigned char>( electorateCount );
	for ( int i = 0; i < electorateCount; ++i ) {
		params.electorate[ i ].participant = static_cast<unsigned int>( ( electorateCount - i ) * 10 );
		params.electorate[ i ].human = true;
	}
	params.requiredQuorum = static_cast<unsigned char>( quorum );
	params.requiredYes = static_cast<unsigned char>( yes );
	params.createdAt = mpProposalEngineTime::FromMilliseconds( created );
	params.expiresAt = mpProposalEngineTime::FromMilliseconds( expires );
	params.caller = 10;
	params.callerVotePolicy = callerPolicy;
	params.operation = Operation( sessionId, opcode, static_cast<unsigned int>( proposalId ) );
	return params;
}

static bool AppliedOnce( const mpProposalMutationResult_t &result ) {
	return result.WasApplied() && result.currentRevision == result.previousRevision + 1;
}

int main() {
	const mpProposalSessionId_t sessionId = 0x123456789abcdef0ull;
	mpProposalCooldownPolicy_t policy;
	policy.Clear();
	policy.durationMsec[ MP_MATCH_COOLDOWN_INTERACTION ] = 100;
	policy.durationMsec[ MP_MATCH_COOLDOWN_PRIVILEGED ] = 200;
	mpProposalService service;
	CHECK( service.ValidateInvariants() );
	CHECK( !service.Reset( 0, mpProposalEngineTime::FromMilliseconds( 0 ), policy ) );
	CHECK( service.Reset( sessionId, mpProposalEngineTime::FromMilliseconds( 0 ), policy ) );
	CHECK( service.GetSessionId() == sessionId );
	CHECK( service.GetRevision() == 1 );

	mpProposalCreateParams_t invalid = Proposal( sessionId, 1, MP_PROPOSAL_SCOPE_GLOBAL,
		MP_MATCH_OP_RULES_COMMIT, 100, 1000, 3, 2, 3, MP_PROPOSAL_CALLER_VOTE_YES );
	invalid.electorate[ 1 ].participant = invalid.electorate[ 0 ].participant;
	CHECK( service.Create( invalid, service.GetRevision() ).reason ==
		MP_PROPOSAL_REASON_ELECTORATE_DUPLICATE );
	CHECK( service.GetRevision() == 1 );
	invalid = Proposal( sessionId, 1, MP_PROPOSAL_SCOPE_GLOBAL,
		MP_MATCH_OP_RULES_COMMIT, 100, 1000, 3, 2, 3, MP_PROPOSAL_CALLER_VOTE_YES );
	invalid.electorate[ 1 ].human = false;
	CHECK( service.Create( invalid, service.GetRevision() ).reason ==
		MP_PROPOSAL_REASON_ELECTORATE_MEMBER_NOT_HUMAN );
	CHECK( service.GetRevision() == 1 );

	mpProposalCreateParams_t first = Proposal( sessionId, 1, MP_PROPOSAL_SCOPE_GLOBAL,
		MP_MATCH_OP_RULES_COMMIT, 100, 1000, 3, 2, 3, MP_PROPOSAL_CALLER_VOTE_YES );
	CHECK( AppliedOnce( service.Create( first, service.GetRevision() ) ) );
	first.requiredYes = 1;
	first.electorate[ 0 ].participant = 999;
	const mpProposalRecord_t *record = service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL );
	CHECK( record != 0 && record->IsActive() );
	CHECK( record->requiredYes == 3 && record->electorateCount == 3 );
	CHECK( record->electorate[ 0 ].participant == 10 );
	CHECK( record->electorate[ 1 ].participant == 20 );
	CHECK( record->electorate[ 2 ].participant == 30 );
	CHECK( record->castCount == 1 && record->yesCount == 1 );

	const mpProposalRevision_t beforeStale = service.GetRevision();
	CHECK( service.CastBallot( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 1, 20,
		MP_PROPOSAL_BALLOT_YES, mpProposalEngineTime::FromMilliseconds( 200 ),
		beforeStale - 1 ).reason == MP_PROPOSAL_REASON_STALE_REVISION );
	CHECK( service.GetRevision() == beforeStale );
	CHECK( AppliedOnce( service.CastBallot( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 1, 20,
		MP_PROPOSAL_BALLOT_YES, mpProposalEngineTime::FromMilliseconds( 200 ),
		service.GetRevision() ) ) );
	const mpProposalRevision_t beforeDuplicate = service.GetRevision();
	CHECK( service.CastBallot( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 1, 20,
		MP_PROPOSAL_BALLOT_NO, mpProposalEngineTime::FromMilliseconds( 201 ),
		service.GetRevision() ).reason == MP_PROPOSAL_REASON_ALREADY_VOTED );
	CHECK( service.GetRevision() == beforeDuplicate );
	CHECK( AppliedOnce( service.CastBallot( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 1, 30,
		MP_PROPOSAL_BALLOT_NO, mpProposalEngineTime::FromMilliseconds( 300 ),
		service.GetRevision() ) ) );
	record = service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL );
	CHECK( record->status == MP_PROPOSAL_STATUS_FAILED );
	CHECK( record->electorateCount == 3 && record->requiredYes == 3 );
	CHECK( record->operation.expectedSessionRevision == 0x100000002ull );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 1,
		service.GetRevision() ) ) );

	mpProposalCreateParams_t cooldownBlocked = Proposal( sessionId, 2,
		MP_PROPOSAL_SCOPE_GLOBAL, MP_MATCH_OP_RULES_COMMIT, 400, 900,
		3, 2, 2, MP_PROPOSAL_CALLER_VOTE_NONE );
	CHECK( service.Create( cooldownBlocked, service.GetRevision() ).reason ==
		MP_PROPOSAL_REASON_COOLDOWN_ACTIVE );
	cooldownBlocked.createdAt = mpProposalEngineTime::FromMilliseconds( 500 );
	cooldownBlocked.expiresAt = mpProposalEngineTime::FromMilliseconds( 900 );
	CHECK( AppliedOnce( service.Create( cooldownBlocked, service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.Cancel( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 2,
		MP_PROPOSAL_CANCEL_REFEREE, mpProposalEngineTime::FromMilliseconds( 550 ),
		service.GetRevision() ) ) );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->cancellationReason ==
		MP_PROPOSAL_CANCEL_REFEREE );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 2,
		service.GetRevision() ) ) );

	mpProposalCreateParams_t pass = Proposal( sessionId, 3, MP_PROPOSAL_SCOPE_TEAM_A,
		MP_MATCH_OP_RESUME_REQUEST, 600, 900, 2, 2, 2, MP_PROPOSAL_CALLER_VOTE_YES );
	CHECK( AppliedOnce( service.Create( pass, service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.CastBallot( sessionId, MP_PROPOSAL_SCOPE_TEAM_A, 3, 20,
		MP_PROPOSAL_BALLOT_YES, mpProposalEngineTime::FromMilliseconds( 650 ),
		service.GetRevision() ) ) );
	record = service.GetProposal( MP_PROPOSAL_SCOPE_TEAM_A );
	CHECK( record->status == MP_PROPOSAL_STATUS_PASSED );
	CHECK( record->operation.opcode == MP_MATCH_OP_RESUME_REQUEST );
	CHECK( record->operation.sessionId == sessionId );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_TEAM_A, 3,
		service.GetRevision() ) ) );

	mpProposalCreateParams_t global = Proposal( sessionId, 4, MP_PROPOSAL_SCOPE_GLOBAL,
		MP_MATCH_OP_RULES_COMMIT, 800, 900, 3, 2, 2, MP_PROPOSAL_CALLER_VOTE_NONE );
	mpProposalCreateParams_t teamA = Proposal( sessionId, 5, MP_PROPOSAL_SCOPE_TEAM_A,
		MP_MATCH_OP_RULES_COMMIT, 800, 900, 3, 2, 2, MP_PROPOSAL_CALLER_VOTE_NONE );
	mpProposalCreateParams_t teamB = Proposal( sessionId, 6, MP_PROPOSAL_SCOPE_TEAM_B,
		MP_MATCH_OP_RESUME_REQUEST, 800, 900, 3, 2, 2, MP_PROPOSAL_CALLER_VOTE_NONE );
	CHECK( AppliedOnce( service.Create( global, service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.Create( teamA, service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.Create( teamB, service.GetRevision() ) ) );
	CHECK( service.GetActiveScopeMask() == MP_PROPOSAL_SCOPE_MASK_ALL );
	const mpProposalRevision_t beforeNoExpiry = service.GetRevision();
	CHECK( service.Expire( sessionId, mpProposalEngineTime::FromMilliseconds( 850 ),
		service.GetRevision() ).code == MP_PROPOSAL_MUTATION_NO_CHANGE );
	CHECK( service.GetRevision() == beforeNoExpiry );
	const mpProposalMutationResult_t expired = service.Expire( sessionId,
		mpProposalEngineTime::FromMilliseconds( 950 ), service.GetRevision() );
	CHECK( AppliedOnce( expired ) );
	CHECK( expired.affectedScopes == MP_PROPOSAL_SCOPE_MASK_ALL );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->status == MP_PROPOSAL_STATUS_EXPIRED );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_TEAM_A )->status == MP_PROPOSAL_STATUS_EXPIRED );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_TEAM_B )->status == MP_PROPOSAL_STATUS_EXPIRED );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_GLOBAL, 4,
		service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_TEAM_A, 5,
		service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.Acknowledge( sessionId, MP_PROPOSAL_SCOPE_TEAM_B, 6,
		service.GetRevision() ) ) );

	mpProposalCreateParams_t phase = Proposal( sessionId, 7, MP_PROPOSAL_SCOPE_GLOBAL,
		MP_MATCH_OP_RULES_COMMIT, 1200, 1600, 3, 2, 2, MP_PROPOSAL_CALLER_VOTE_NONE );
	CHECK( AppliedOnce( service.Create( phase, service.GetRevision() ) ) );
	CHECK( AppliedOnce( service.InvalidateForPhase( sessionId, GAMEON,
		mpProposalEngineTime::FromMilliseconds( 1250 ), service.GetRevision() ) ) );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->status ==
		MP_PROPOSAL_STATUS_PHASE_INVALIDATED );
	CHECK( service.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->operation.opcode ==
		MP_MATCH_OP_RULES_COMMIT );
	CHECK( service.ValidateInvariants() );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_proposal_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-proposal-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_proposal_contract.cpp"
        executable = temp_dir / (
            "match_proposal_contract.exe" if compiler.lower().endswith(".exe")
            else "match_proposal_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-DMP_PROPOSAL_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-proposal contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"match-proposal executable invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    # Collision-check the independent cores that predate MatchProposal.  Later
    # composition headers (for example MatchOperations) are expected to use the
    # proposal types and must not be mistaken for duplicate declarations.
    existing_core_names = (
        "MatchEvidence.h",
        "MatchProtocol.h",
        "MatchRules.h",
        "MatchSeries.h",
        "MatchSession.h",
    )
    existing_match_headers = "\n".join(
        read(HEADER.parent / name) for name in existing_core_names
    )
    static_contracts(header, source, existing_match_headers)
    executable_contract()
    print("mp_match_proposal_contract: PASS")


if __name__ == "__main__":
    main()
