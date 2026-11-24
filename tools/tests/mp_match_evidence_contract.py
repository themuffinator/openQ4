#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for the competitive match evidence core."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchEvidence.h"
SOURCE = ROOT / "src/game/mp/match/MatchEvidence.cpp"
MATCH_DIR = HEADER.parent


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    require(header, '#include "../MatchPhase.h"', "dependency-light evidence header")
    if "MatchSession.h" in header or "MatchProtocol.h" in header:
        raise AssertionError("evidence core depends on session or protocol implementation")

    for token in (
        "MP_MATCH_EVIDENCE_SCHEMA_VERSION = 2",
        "MP_MATCH_EVIDENCE_MAX_EVENTS = 256",
        "MP_MATCH_EVIDENCE_MAX_PARTICIPANTS = 32",
        "MP_MATCH_EVIDENCE_MAX_TEAMS = 2",
        "MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES = 160",
        "mpEvidenceArtifactLinkInput",
        "MPMatchEvidenceIsSafeArtifactQPath",
        "LinkSeriesId",
        "LinkArtifact",
		"MP_EVIDENCE_OUTPUT_SERIES_REPORT",
		'return "seriesReport"',
        "void\t\t\t\t\tClear( void );",
        "class mpMatchEvidence",
        "SerializeCanonicalJson",
        "ValidateInvariants",
    ):
        require(header + source, token, "bounded evidence API")

    event_block = re.search(
        r"typedef enum \{(?P<body>.*?)\} mpEvidenceEventKind_t;", header, re.DOTALL
    )
    if event_block is None:
        raise AssertionError("missing typed evidence event enum")
    actual_event_kinds = re.findall(r"\bMP_EVIDENCE_EVENT_[A-Z0-9_]+\b", event_block.group("body"))
    expected_event_kinds = [
        "MP_EVIDENCE_EVENT_INVALID",
        "MP_EVIDENCE_EVENT_PHASE_TRANSITION",
        "MP_EVIDENCE_EVENT_ROUND_TRANSITION",
        "MP_EVIDENCE_EVENT_PAUSE_TRANSITION",
        "MP_EVIDENCE_EVENT_ROLE_CHANGE",
        "MP_EVIDENCE_EVENT_PROPOSAL",
        "MP_EVIDENCE_EVENT_ROSTER_CHANGE",
        "MP_EVIDENCE_EVENT_MAP_RESULT",
        "MP_EVIDENCE_EVENT_OUTPUT_FAILURE",
        "MP_EVIDENCE_EVENT_KIND_COUNT",
    ]
    if actual_event_kinds != expected_event_kinds:
        raise AssertionError(f"typed event journal changed unexpectedly: {actual_event_kinds}")

    combined = header + source
    forbidden_dependencies = (
        "idFile",
        "idBitMsg",
        "idCVar",
        "idUserInterface",
        "cmdSystem",
        "gameLocal",
        "idMultiplayerGame",
        "rvGameState",
        "idList<",
        "std::vector",
        "std::string",
        "std::thread",
        "StartMVD(",
        "StopMVD(",
    )
    for token in forbidden_dependencies:
        if token in combined:
            raise AssertionError(f"evidence value layer contains forbidden dependency {token!r}")
    if re.search(r"\bnew\s+", combined) or re.search(r"\bdelete\s+", combined):
        raise AssertionError("evidence core must remain allocation-free")

    # The schema deliberately has no network identity, authentication material,
    # or arbitrary backend text.  Its sole qpath is a typed, core-validated MVD
    # artifact link rather than a caller-selected persistence destination.
    for field in ("ipAddress", "address", "credential", "password", "secret", "guid", "userinfo"):
        if re.search(rf"\b{re.escape(field)}\b", header, re.IGNORECASE):
            raise AssertionError(f"privacy-sensitive field leaked into evidence schema: {field}")

    if len(re.findall(r"\+\+evidenceRevision", source)) != 1:
        raise AssertionError("evidence revision must have exactly one increment owner")
    require(source, "++counter", "explicit overflow/drop accounting")
    require(source, "dropCounterSaturated = true", "saturated drop accounting")
    require(source, 'PutLiteral( "\\\\u00" )', "control-character JSON escaping")
    require(source, "mpEvidenceJsonWriter counter( NULL, 0 )", "JSON count pass")
    serialize = source[source.index("mpEvidenceSerializeResult mpMatchEvidence::SerializeCanonicalJson") :]
    capacity_check = serialize.index("capacity < result.requiredCapacity")
    output_writer = serialize.index("mpEvidenceJsonWriter writer( buffer")
    if output_writer < capacity_check:
        raise AssertionError("serializer can touch output before proving full capacity")
    require(
        header,
        "On every failure the caller's buffer remains unchanged.",
        "serialization atomicity contract",
    )
    require(source, "MP_MATCH_EVIDENCE_STANDALONE_TEST", "standalone compile seam")

    # Public names in parallel value cores must remain unambiguous for adapters.
    public_name_pattern = re.compile(
        r"\b(?:mpEvidence[A-Za-z0-9_]*|mpMatchEvidence|MPEvidence[A-Za-z0-9_]*|"
        r"MP_(?:MATCH_)?EVIDENCE_[A-Z0-9_]+)\b"
    )
    own_names = set(public_name_pattern.findall(header))
    other_text = "\n".join(
        read(path)
        for path in sorted(MATCH_DIR.glob("Match*.h"))
        if path != HEADER
    )
    declared_elsewhere: set[str] = set()
    declared_elsewhere.update(
        re.findall(
            r"\b(?:class|struct|union)\s+(mp(?:Evidence[A-Za-z0-9_]*|MatchEvidence))\b",
            other_text,
        )
    )
    declared_elsewhere.update(
        re.findall(r"}\s+(mpEvidence[A-Za-z0-9_]*)\s*;", other_text)
    )
    declared_elsewhere.update(
        re.findall(
            r"\b(MP_(?:MATCH_)?EVIDENCE_[A-Z0-9_]+)\s*(?:=|,)", other_text
        )
    )
    declared_elsewhere.update(
        re.findall(r"\b(MPEvidence[A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{", other_text)
    )
    collisions = own_names & declared_elsewhere
    if collisions:
        raise AssertionError(f"evidence public-name collision: {sorted(collisions)}")


HARNESS = r'''
#include "game/mp/match/MatchEvidence.h"

#include <stdio.h>
#include <string.h>

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static mpEvidenceCommittedStamp Stamp( uint64_t revision ) {
	mpEvidenceCommittedStamp stamp;
	stamp.sessionRevision = revision;
	stamp.matchTimeMsec = revision * 10;
	stamp.hostTimeUtcMsec = 1770000000000ULL + revision;
	return stamp;
}

static bool ChangedOnce( const mpEvidenceWriteResult &result ) {
	return ( result.WasAccepted() || result.WasDropped() ) &&
		result.currentEvidenceRevision == result.previousEvidenceRevision + 1;
}

static mpEvidenceOutputFailure OutputFailure( uint16_t reason ) {
	mpEvidenceOutputFailure output;
	output.output = MP_EVIDENCE_OUTPUT_MVD_START;
	output.reason = reason;
	return output;
}

int main() {
	mpMatchEvidence evidence;
	CHECK( evidence.ValidateInvariants() );

	mpEvidenceMetadataInput invalidMetadata = {};
	invalidMetadata.modeId = 1;
	invalidMetadata.build = "build";
	invalidMetadata.map = "map";
	invalidMetadata.mode = "duel";
	CHECK( !evidence.Reset( invalidMetadata ) );
	CHECK( !evidence.IsInitialized() );

	mpEvidenceMetadataInput metadata;
	metadata.sessionId = 0x1122334455667788ULL;
	metadata.seriesId = 0;
	metadata.rulesDigest = 0x0123456789abcdefULL;
	metadata.modeId = 4;
	metadata.build = "openQ4-0.1.010";
	metadata.map = "maps/mp/test\"arena\\one";
	metadata.mode = "duel";
	CHECK( evidence.Reset( metadata ) );
	CHECK( evidence.GetEvidenceRevision() == 1 );
	CHECK( evidence.ValidateInvariants() );

	static const char *unsafeQPaths[] = {
		"", "match.mvd", "/demos/match.mvd", "demos\\match.mvd",
		"demos/../match.mvd", "demos//match.mvd",
		"demos/match.mvd/extra", "demos/match.demo", "demos/C:match.mvd",
		"demos/match name.mvd", "demos/match\nname.mvd", "demos/ma\xc3\xa9tch.mvd"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( unsafeQPaths ) /
			sizeof( unsafeQPaths[ 0 ] ) ); ++index ) {
		CHECK( !MPMatchEvidenceIsSafeArtifactQPath(
			MP_EVIDENCE_ARTIFACT_MVD, unsafeQPaths[ index ] ) );
	}
	char overlongQPath[ MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES + 2 ];
	memset( overlongQPath, 'a', sizeof( overlongQPath ) );
	memcpy( overlongQPath, "demos/", 6 );
	memcpy( overlongQPath + sizeof( overlongQPath ) - 5, ".mvd", 5 );
	CHECK( !MPMatchEvidenceIsSafeArtifactQPath(
		MP_EVIDENCE_ARTIFACT_MVD, overlongQPath ) );
	CHECK( !MPMatchEvidenceIsSafeArtifactQPath(
		MP_EVIDENCE_ARTIFACT_INVALID, "demos/match.mvd" ) );
	CHECK( MPMatchEvidenceIsSafeArtifactQPath(
		MP_EVIDENCE_ARTIFACT_MVD, "demos/series-73/map_001.mvd" ) );
	CHECK( MPMatchEvidenceIsSafeArtifactQPath(
		MP_EVIDENCE_ARTIFACT_MVD, "demos/.operator-recording.mvd" ) );

	const uint64_t beforeIdentity = evidence.GetEvidenceRevision();
	CHECK( ChangedOnce( evidence.LinkSeriesId( Stamp( 2 ), 73 ) ) );
	CHECK( evidence.GetMetadata().seriesId == 73 );
	CHECK( evidence.GetSeriesLinkSessionRevision() == 2 );
	CHECK( evidence.LinkSeriesId( Stamp( 2 ), 73 ).code ==
		MP_EVIDENCE_WRITE_NO_CHANGE );
	CHECK( evidence.GetEvidenceRevision() == beforeIdentity + 1 );
	CHECK( evidence.LinkSeriesId( Stamp( 2 ), 74 ).reason ==
		MP_EVIDENCE_REASON_SERIES_ID_CONFLICT );
	CHECK( evidence.LinkSeriesId( Stamp( 2 ), 0 ).reason ==
		MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	CHECK( evidence.GetMetadata().seriesId == 73 );
	CHECK( evidence.GetEvidenceRevision() == beforeIdentity + 1 );

	mpEvidenceArtifactLinkInput artifactLink;
	artifactLink.kind = MP_EVIDENCE_ARTIFACT_MVD;
	artifactLink.qpath = "demos/series-73/map_001.mvd";
	const uint64_t beforeArtifact = evidence.GetEvidenceRevision();
	CHECK( ChangedOnce( evidence.LinkArtifact( Stamp( 3 ), artifactLink ) ) );
	CHECK( evidence.GetArtifactCount() == 1 );
	CHECK( evidence.GetArtifact( 0 ) != NULL );
	CHECK( evidence.GetArtifact( 0 )->sessionRevision == 3 );
	CHECK( strcmp( evidence.GetArtifact( 0 )->qpath, artifactLink.qpath ) == 0 );
	CHECK( evidence.GetArtifact( -1 ) == NULL );
	CHECK( evidence.GetArtifact( 1 ) == NULL );
	CHECK( evidence.LinkArtifact( Stamp( 3 ), artifactLink ).code ==
		MP_EVIDENCE_WRITE_NO_CHANGE );
	CHECK( evidence.GetEvidenceRevision() == beforeArtifact + 1 );
	artifactLink.qpath = "demos/series-73/other.mvd";
	CHECK( evidence.LinkArtifact( Stamp( 3 ), artifactLink ).reason ==
		MP_EVIDENCE_REASON_ARTIFACT_CONFLICT );
	CHECK( strcmp( evidence.GetArtifact( 0 )->qpath,
		"demos/series-73/map_001.mvd" ) == 0 );
	artifactLink.qpath = "demos/../hostile.mvd";
	CHECK( evidence.LinkArtifact( Stamp( 3 ), artifactLink ).reason ==
		MP_EVIDENCE_REASON_INVALID_ARTIFACT_QPATH );
	CHECK( evidence.GetEvidenceRevision() == beforeArtifact + 1 );
	CHECK( evidence.ValidateInvariants() );

	mpEvidencePhaseTransition phase;
	phase.from = WARMUP;
	phase.to = COUNTDOWN;
	phase.reason = 1;
	phase.actor = MPEvidenceParticipantActor( 1 );
	CHECK( ChangedOnce( evidence.AppendPhaseTransition( Stamp( 10 ), phase ) ) );

	mpEvidenceRoundTransition round;
	round.from = RS_COUNTDOWN;
	round.to = RS_ACTIVE;
	round.reason = 2;
	CHECK( ChangedOnce( evidence.AppendRoundTransition( Stamp( 11 ), round ) ) );

	mpEvidencePauseTransition pause;
	pause.from = MP_EVIDENCE_PAUSE_RUNNING;
	pause.to = MP_EVIDENCE_PAUSE_PENDING;
	pause.kind = MP_EVIDENCE_PAUSE_TEAM_TIMEOUT;
	pause.ownerSide = 0;
	pause.reason = 3;
	pause.actor = MPEvidenceParticipantActor( 1 );
	CHECK( ChangedOnce( evidence.AppendPauseTransition( Stamp( 12 ), pause ) ) );

	mpEvidenceRoleChange role;
	role.targetParticipant = 1;
	role.previousRoles = 1;
	role.currentRoles = 3;
	role.authorizer = MPEvidenceServerOperatorActor();
	CHECK( ChangedOnce( evidence.AppendRoleChange( Stamp( 13 ), role ) ) );

	mpEvidenceProposalEvent proposal;
	proposal.proposalId = 7;
	proposal.action = MP_EVIDENCE_PROPOSAL_CREATED;
	proposal.opcode = 4;
	proposal.scopeSide = -1;
	proposal.targetParticipant = 0;
	proposal.actor = MPEvidenceParticipantActor( 1 );
	CHECK( ChangedOnce( evidence.AppendProposal( Stamp( 14 ), proposal ) ) );

	mpEvidenceRosterEvent roster;
	roster.action = MP_EVIDENCE_ROSTER_SEAT_DECLARED;
	roster.seat = 0;
	roster.side = 0;
	roster.role = MP_EVIDENCE_ROSTER_CAPTAIN;
	roster.participant = 0;
	roster.replacementParticipant = 0;
	roster.locked = false;
	roster.authorizer = MPEvidenceServerOperatorActor();
	CHECK( ChangedOnce( evidence.AppendRosterChange( Stamp( 15 ), roster ) ) );

	mpEvidenceMapResult mapResult;
	mapResult.outcome = MP_EVIDENCE_RESULT_DECIDED;
	mapResult.winnerSide = 0;
	mapResult.winnerParticipant = 0;
	mapResult.sideScore[ 0 ] = 20;
	mapResult.sideScore[ 1 ] = 17;
	mapResult.reason = 5;
	mapResult.authorizer = MPEvidenceSystemActor();
	CHECK( ChangedOnce( evidence.AppendMapResult( Stamp( 16 ), mapResult ) ) );
	CHECK( ChangedOnce( evidence.AppendOutputFailure( Stamp( 17 ), OutputFailure( 6 ) ) ) );

	CHECK( evidence.GetEventCount() == 8 );
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		CHECK( evidence.GetEvent( index ) != NULL );
		CHECK( evidence.GetEvent( index )->sequence == static_cast<uint64_t>( index + 1 ) );
	}
	CHECK( evidence.GetEvent( -1 ) == NULL );
	CHECK( evidence.GetEvent( evidence.GetEventCount() ) == NULL );

	const uint64_t beforeRejected = evidence.GetEvidenceRevision();
	CHECK( evidence.AppendPhaseTransition( Stamp( 9 ), phase ).reason ==
		MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION );
	CHECK( evidence.GetEvidenceRevision() == beforeRejected );
	role.currentRoles = role.previousRoles;
	CHECK( evidence.AppendRoleChange( Stamp( 18 ), role ).reason ==
		MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	CHECK( evidence.GetEvidenceRevision() == beforeRejected );

	const char escapedName[] = "Quo\\te\"\n\x01\xc3\xa9";
	mpEvidenceParticipantStatsInput stats;
	stats.participantSequence = 1;
	stats.side = 0;
	stats.displayName = escapedName;
	stats.score = 20;
	stats.kills = 21;
	stats.deaths = 17;
	stats.suicides = 1;
	stats.damageGiven = 4200;
	stats.damageReceived = 3900;
	stats.shots = 100;
	stats.hits = 42;
	CHECK( ChangedOnce( evidence.RecordParticipantFinalStats( Stamp( 18 ), stats ) ) );
	const uint64_t beforeDuplicate = evidence.GetEvidenceRevision();
	CHECK( evidence.RecordParticipantFinalStats( Stamp( 18 ), stats ).code ==
		MP_EVIDENCE_WRITE_NO_CHANGE );
	CHECK( evidence.GetEvidenceRevision() == beforeDuplicate );
	stats.score = 21;
	CHECK( evidence.RecordParticipantFinalStats( Stamp( 18 ), stats ).reason ==
		MP_EVIDENCE_REASON_DUPLICATE_PARTICIPANT_STATS );
	CHECK( evidence.GetEvidenceRevision() == beforeDuplicate );
	stats.score = 20;
	stats.hits = 101;
	CHECK( evidence.RecordParticipantFinalStats( Stamp( 18 ), stats ).reason ==
		MP_EVIDENCE_REASON_INVALID_ARGUMENT );
	CHECK( evidence.GetEvidenceRevision() == beforeDuplicate );
	stats.hits = 42;

	char invalidUtf8[] = { static_cast<char>( 0xc0 ), static_cast<char>( 0x80 ), 0 };
	mpEvidenceParticipantStatsInput invalidStats = stats;
	invalidStats.participantSequence = 2;
	invalidStats.displayName = invalidUtf8;
	CHECK( evidence.RecordParticipantFinalStats( Stamp( 18 ), invalidStats ).reason ==
		MP_EVIDENCE_REASON_INVALID_TEXT );
	CHECK( evidence.GetEvidenceRevision() == beforeDuplicate );

	CHECK( ChangedOnce( evidence.RecordTeamFinalStats( Stamp( 18 ), 0, 20, 0, 0, 4200 ) ) );
	CHECK( ChangedOnce( evidence.RecordTeamFinalStats( Stamp( 18 ), 1, 17, 0, 0, 3900 ) ) );
	const uint64_t beforeTeamDuplicate = evidence.GetEvidenceRevision();
	CHECK( evidence.RecordTeamFinalStats( Stamp( 18 ), 0, 20, 0, 0, 4200 ).code ==
		MP_EVIDENCE_WRITE_NO_CHANGE );
	CHECK( evidence.RecordTeamFinalStats( Stamp( 18 ), 0, 99, 0, 0, 4200 ).reason ==
		MP_EVIDENCE_REASON_DUPLICATE_TEAM_STATS );
	CHECK( evidence.GetEvidenceRevision() == beforeTeamDuplicate );

	for ( uint32_t participant = 2; participant <= MP_MATCH_EVIDENCE_MAX_PARTICIPANTS;
			++participant ) {
		stats.participantSequence = participant;
		stats.side = static_cast<int8_t>( participant & 1 );
		stats.displayName = "player";
		CHECK( ChangedOnce( evidence.RecordParticipantFinalStats( Stamp( 18 ), stats ) ) );
	}
	CHECK( evidence.GetParticipantStatsCount() == MP_MATCH_EVIDENCE_MAX_PARTICIPANTS );
	stats.participantSequence = MP_MATCH_EVIDENCE_MAX_PARTICIPANTS + 1;
	const mpEvidenceWriteResult droppedStats =
		evidence.RecordParticipantFinalStats( Stamp( 19 ), stats );
	CHECK( ChangedOnce( droppedStats ) );
	CHECK( droppedStats.WasDropped() );
	CHECK( droppedStats.reason == MP_EVIDENCE_REASON_PARTICIPANT_STATS_CAPACITY );
	CHECK( evidence.GetDroppedParticipantStatsCount() == 1 );

	for ( int index = evidence.GetEventCount(); index < MP_MATCH_EVIDENCE_MAX_EVENTS; ++index ) {
		CHECK( ChangedOnce( evidence.AppendOutputFailure(
			Stamp( 20 + static_cast<uint64_t>( index ) ), OutputFailure( 7 ) ) ) );
	}
	CHECK( evidence.GetEventCount() == MP_MATCH_EVIDENCE_MAX_EVENTS );
	const uint64_t firstDroppedRevision = 1000;
	const mpEvidenceWriteResult firstDroppedEvent = evidence.AppendOutputFailure(
		Stamp( firstDroppedRevision ), OutputFailure( 8 ) );
	CHECK( ChangedOnce( firstDroppedEvent ) );
	CHECK( firstDroppedEvent.WasDropped() );
	CHECK( firstDroppedEvent.reason == MP_EVIDENCE_REASON_EVENT_CAPACITY );
	CHECK( evidence.GetDroppedEventCount() == 1 );
	CHECK( evidence.GetFirstDroppedSessionRevision() == firstDroppedRevision );
	CHECK( evidence.GetLastDroppedSessionRevision() == firstDroppedRevision );
	const mpEvidenceWriteResult secondDroppedEvent = evidence.AppendOutputFailure(
		Stamp( firstDroppedRevision + 1 ), OutputFailure( 9 ) );
	CHECK( ChangedOnce( secondDroppedEvent ) );
	CHECK( secondDroppedEvent.WasDropped() );
	CHECK( evidence.GetDroppedEventCount() == 2 );
	CHECK( evidence.GetFirstDroppedSessionRevision() == firstDroppedRevision );
	CHECK( evidence.GetLastDroppedSessionRevision() == firstDroppedRevision + 1 );
	CHECK( evidence.ValidateInvariants() );

	// Non-terminal telemetry can consume at most 255 slots so a terminal map
	// result always remains journalable.  The result is the evidence seal and
	// must not be displaced by optional output diagnostics.
	mpMatchEvidence terminalCapacity;
	CHECK( terminalCapacity.Reset( metadata ) );
	for ( int index = 0; index < MP_MATCH_EVIDENCE_MAX_EVENTS - 1; ++index ) {
		CHECK( ChangedOnce( terminalCapacity.AppendOutputFailure(
			Stamp( 2000 + static_cast<uint64_t>( index ) ), OutputFailure( 10 ) ) ) );
	}
	CHECK( terminalCapacity.GetEventCount() == MP_MATCH_EVIDENCE_MAX_EVENTS - 1 );
	const mpEvidenceWriteResult reservedSlotDrop = terminalCapacity.AppendOutputFailure(
		Stamp( 3000 ), OutputFailure( 11 ) );
	CHECK( ChangedOnce( reservedSlotDrop ) );
	CHECK( reservedSlotDrop.WasDropped() );
	CHECK( reservedSlotDrop.reason == MP_EVIDENCE_REASON_EVENT_CAPACITY );
	CHECK( terminalCapacity.GetEventCount() == MP_MATCH_EVIDENCE_MAX_EVENTS - 1 );
	CHECK( ChangedOnce( terminalCapacity.AppendMapResult( Stamp( 3001 ), mapResult ) ) );
	CHECK( terminalCapacity.GetEventCount() == MP_MATCH_EVIDENCE_MAX_EVENTS );
	CHECK( terminalCapacity.GetEvent( MP_MATCH_EVIDENCE_MAX_EVENTS - 1 )->kind ==
		MP_EVIDENCE_EVENT_MAP_RESULT );
	CHECK( terminalCapacity.ValidateInvariants() );

	mpEvidenceSerializeResult sizing = evidence.SerializeCanonicalJson( NULL, 0 );
	CHECK( sizing.code == MP_EVIDENCE_SERIALIZE_BUFFER_TOO_SMALL );
	CHECK( sizing.bytesWritten == 0 );
	CHECK( sizing.requiredCapacity > 128 );
	char tooSmall[ 128 ];
	memset( tooSmall, 'Z', sizeof( tooSmall ) );
	mpEvidenceSerializeResult truncated = evidence.SerializeCanonicalJson(
		tooSmall, static_cast<int>( sizeof( tooSmall ) ) );
	CHECK( truncated.code == MP_EVIDENCE_SERIALIZE_BUFFER_TOO_SMALL );
	CHECK( truncated.bytesWritten == 0 );
	CHECK( truncated.requiredCapacity == sizing.requiredCapacity );
	for ( int index = 0; index < static_cast<int>( sizeof( tooSmall ) ); ++index ) {
		CHECK( tooSmall[ index ] == 'Z' );
	}

	char json[ 262144 ];
	char secondJson[ 262144 ];
	mpEvidenceSerializeResult serialized = evidence.SerializeCanonicalJson(
		json, static_cast<int>( sizeof( json ) ) );
	CHECK( serialized.Succeeded() );
	CHECK( serialized.requiredCapacity == serialized.bytesWritten + 1 );
	CHECK( serialized.requiredCapacity == sizing.requiredCapacity );
	CHECK( json[ serialized.bytesWritten ] == 0 );
	CHECK( strstr( json, "\"rulesDigest\":\"0123456789abcdef\"" ) != NULL );
	CHECK( strstr( json, "\"seriesId\":73,\"seriesLinkSessionRevision\":2" ) != NULL );
	CHECK( strstr( json,
		"\"artifacts\":[{\"kind\":\"mvd\",\"sessionRevision\":3,"
		"\"qpath\":\"demos/series-73/map_001.mvd\"}]" ) != NULL );
	CHECK( strstr( json, "\"map\":\"maps/mp/test\\\"arena\\\\one\"" ) != NULL );
	CHECK( strstr( json,
		"\"displayName\":\"Quo\\\\te\\\"\\n\\u0001\xc3\xa9\"" ) != NULL );
	for ( int index = 0; index < serialized.bytesWritten; ++index ) {
		CHECK( static_cast<unsigned char>( json[ index ] ) >= 0x20 );
	}
	static const char *kindNeedles[] = {
		"\"kind\":\"phase\"", "\"kind\":\"round\"", "\"kind\":\"pause\"",
		"\"kind\":\"role\"", "\"kind\":\"proposal\"", "\"kind\":\"roster\"",
		"\"kind\":\"result\"", "\"kind\":\"outputFailure\""
	};
	for ( int index = 0; index < static_cast<int>( sizeof( kindNeedles ) /
			sizeof( kindNeedles[ 0 ] ) ); ++index ) {
		CHECK( strstr( json, kindNeedles[ index ] ) != NULL );
	}
	static const char *privateKeys[] = {
		"ipAddress", "credential", "password", "secret", "userinfo"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( privateKeys ) /
			sizeof( privateKeys[ 0 ] ) ); ++index ) {
		CHECK( strstr( json, privateKeys[ index ] ) == NULL );
	}
	mpEvidenceSerializeResult second = evidence.SerializeCanonicalJson(
		secondJson, static_cast<int>( sizeof( secondJson ) ) );
	CHECK( second.Succeeded() );
	CHECK( second.bytesWritten == serialized.bytesWritten );
	CHECK( strcmp( secondJson, json ) == 0 );

	const uint64_t preservedRevision = evidence.GetEvidenceRevision();
	const int preservedEvents = evidence.GetEventCount();
	const uint64_t preservedSession = evidence.GetMetadata().sessionId;
	invalidMetadata.sessionId = 0;
	CHECK( !evidence.Reset( invalidMetadata ) );
	CHECK( evidence.GetEvidenceRevision() == preservedRevision );
	CHECK( evidence.GetEventCount() == preservedEvents );
	CHECK( evidence.GetMetadata().sessionId == preservedSession );
	CHECK( evidence.ValidateInvariants() );
	CHECK( fwrite( json, 1, static_cast<size_t>( serialized.bytesWritten ), stdout ) ==
		static_cast<size_t>( serialized.bytesWritten ) );
	evidence.Clear();
	CHECK( !evidence.IsInitialized() );
	CHECK( evidence.GetEvidenceRevision() == 0 );
	CHECK( evidence.GetEventCount() == 0 );
	CHECK( evidence.GetArtifactCount() == 0 );
	CHECK( evidence.GetParticipantStatsCount() == 0 );
	CHECK( evidence.GetTeamStatsCount() == 0 );
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
        print("mp_match_evidence_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-evidence-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_evidence_contract.cpp"
        executable = temp_dir / (
            "match_evidence_contract.exe" if compiler.lower().endswith(".exe")
            else "match_evidence_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_EVIDENCE_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-evidence contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"match-evidence executable invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )
        try:
            artifact = json.loads(ran.stdout.decode("utf-8", errors="strict"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AssertionError(f"serializer did not emit valid UTF-8 JSON: {error}") from error
        if artifact["schema"] != 2 or artifact["sessionId"] != 0x1122334455667788:
            raise AssertionError("serialized identity metadata differs from the source values")
        if artifact["seriesId"] != 73 or artifact["seriesLinkSessionRevision"] != 2:
            raise AssertionError("late-bound series identity was not serialized canonically")
        if artifact["artifacts"] != [
            {
                "kind": "mvd",
                "sessionRevision": 3,
                "qpath": "demos/series-73/map_001.mvd",
            }
        ]:
            raise AssertionError("typed MVD artifact link differs from the committed qpath")
        if artifact["rulesDigest"] != "0123456789abcdef":
            raise AssertionError("rules digest is not the fixed-width canonical value")
        canonical_digest = hashlib.sha256(ran.stdout).hexdigest()
        if canonical_digest != "a9c7b987ffe296b4dcce06b85dff39c60a2f0df89c3be2976bc8d647c54d823b":
            raise AssertionError(
                f"canonical evidence digest drifted unexpectedly: {canonical_digest}"
            )
        if artifact["journal"]["accepted"] != 256 or artifact["journal"]["dropped"] != 2:
            raise AssertionError("serialized journal accounting differs from the core")
        if artifact["participantStats"]["accepted"] != 32 or artifact["participantStats"]["dropped"] != 1:
            raise AssertionError("serialized participant-stat accounting differs from the core")
        if artifact["teamStats"]["accepted"] != 2:
            raise AssertionError("serialized team-stat accounting differs from the core")
        expected_kinds = [
            "phase", "round", "pause", "role", "proposal", "roster", "result",
            "outputFailure",
        ]
        if [event["kind"] for event in artifact["journal"]["events"][:8]] != expected_kinds:
            raise AssertionError("serialized typed event order differs from the journal")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_evidence_contract: PASS")


if __name__ == "__main__":
    main()
