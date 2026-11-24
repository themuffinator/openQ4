#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and hostile native contracts for the final competition-series report."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchSeriesReport.h"
SOURCE = ROOT / "src/game/mp/match/MatchSeriesReport.cpp"
SOURCE_LISTER = ROOT / "tools/build/list_sources.py"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    require(header, '#include "MatchSeries.h"', "dependency-light report header")
    for forbidden_header in (
        "MatchSeriesRecovery.h",
        "MatchSession.h",
        "MatchProtocol.h",
        "MatchEvidenceStorage.h",
    ):
        if forbidden_header in header:
            raise AssertionError(
                f"final report value layer depends on {forbidden_header}"
            )

    combined = header + source
    for token in (
        "MP_SERIES_REPORT_SCHEMA_VERSION = 1",
        "MP_SERIES_REPORT_MAX_MAP_RESULTS = MP_SERIES_MAX_MAP_ATTEMPTS",
        "MP_SERIES_REPORT_MAX_PARTICIPANTS = 32",
        "class mpCompetitionSeriesReport",
        "AppendMapResult",
        "ReconcileMapArtifact",
        "RecordParticipantStats",
        "RecordTeamStats",
        "SerializeCanonicalJson",
        "ValidateInvariants",
        "MPMatchSeriesReportIsSafeArtifactQPath",
        "On\n\t// every failure the caller's buffer remains byte-for-byte unchanged.",
        "MP_MATCH_SERIES_REPORT_STANDALONE_TEST",
    ):
        require(combined, token, "bounded final-report contract")

    forbidden_dependencies = (
        "idFile",
        "idBitMsg",
        "idCVar",
        "idUserInterface",
        "idMultiplayerGame",
        "gameLocal",
        "cmdSystem",
        "idList<",
        "std::vector",
        "std::string",
        "std::thread",
    )
    for token in forbidden_dependencies:
        if token in combined:
            raise AssertionError(f"series-report core contains dependency {token!r}")
    for pattern in (
        r"\bnew\b",
        r"\bdelete\b",
        r"\bmalloc\s*\(",
        r"\bcalloc\s*\(",
        r"\brealloc\s*\(",
        r"\bfree\s*\(",
    ):
        if re.search(pattern, combined):
            raise AssertionError(
                f"series-report value/serialization core allocates via {pattern!r}"
            )

    # The only human-entered value retained is a bounded role-neutral display
    # label. Network identity and authority material have no report fields.
    for field in (
        "ipAddress",
        "networkAddress",
        "guid",
        "accountId",
        "credential",
        "password",
        "secret",
        "userinfo",
        "connectionSlot",
        "roleMask",
    ):
        if re.search(rf"\b{re.escape(field)}\b", header, re.IGNORECASE):
            raise AssertionError(f"privacy-sensitive report field leaked: {field}")

    if len(re.findall(r"\+\+reportRevision", source)) != 1:
        raise AssertionError("report revision must have one mutation owner")
    require(source, "IdentityEqual( identity, candidate )", "idempotent identity")
    require(source, "FinalEqual( finalResult, candidate )", "idempotent finalization")
    require(source, 'PutLiteral( "\\\\u00" )', "control-character JSON escaping")
    require(source, "mpSeriesReportJsonWriter counter( NULL, 0 )", "JSON count pass")
    serializer = source[source.index(
        "mpSeriesReportSerializeResult mpCompetitionSeriesReport::SerializeCanonicalJson"
    ) :]
    if serializer.index("capacity < result.requiredCapacity") > serializer.index(
        "mpSeriesReportJsonWriter writer( buffer"
    ):
        raise AssertionError("serializer writes before proving complete capacity")

    listed = subprocess.run(
        [
            shutil.which("python") or shutil.which("python3") or "python",
            str(SOURCE_LISTER),
            str(ROOT / "src"),
            "game",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if listed.returncode != 0:
        raise AssertionError("source discovery failed:\n" + listed.stdout + listed.stderr)
    if "game/mp/match/MatchSeriesReport.cpp" not in listed.stdout.splitlines():
        raise AssertionError("Meson recursive source discovery omits MatchSeriesReport.cpp")


HARNESS = r'''
#include "game/mp/match/MatchSeriesReport.h"

#include <stdio.h>
#include <string.h>

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static mpSeriesReportIdentityInput Identity( void ) {
	mpSeriesReportIdentityInput identity = {};
	identity.seriesId = 0x1122334455667788ULL;
	identity.profile = MP_SERIES_PROFILE_BEST_OF_THREE;
	identity.profileKey = "best_of_three";
	identity.bestOf = 3;
	identity.rulesSchema = 2;
	identity.rulesRevision = 7;
	identity.rulesDigest = 0x0123456789abcdefULL;
	identity.gameType = 1;
	identity.modeToken = "duel";
	identity.contestants[ 0 ].kind = MP_SERIES_REPORT_CONTESTANT_PARTICIPANT;
	identity.contestants[ 0 ].participantSequence = 101;
	identity.contestants[ 0 ].label = "Quo\\te\"\n\x01\xc3\xa9";
	identity.contestants[ 1 ].kind = MP_SERIES_REPORT_CONTESTANT_PARTICIPANT;
	identity.contestants[ 1 ].participantSequence = 202;
	identity.contestants[ 1 ].label = "Rival";
	return identity;
}

static mpSeriesReportMapResultInput Map( uint32_t attempt, const char *map,
	mpSeriesReportMapOutcome_t outcome, int winner, int score0, int score1 ) {
	mpSeriesReportMapResultInput result = {};
	result.attempt = attempt;
	result.sessionId = 9000 + attempt;
	result.mapToken = map;
	result.rulesDigest = 0x0123456789abcdefULL;
	result.outcome = outcome;
	result.reason = static_cast<uint16_t>( 20 + attempt );
	result.winnerContestant = winner;
	result.score[ 0 ] = score0;
	result.score[ 1 ] = score1;
	return result;
}

static bool ChangedOnce( const mpSeriesReportWriteResult &result ) {
	return ( result.WasAccepted() || result.WasDropped() ) &&
		result.currentRevision == result.previousRevision + 1;
}

int main() {
	static_assert( sizeof( mpCompetitionSeriesReport ) < 65536,
		"bounded report unexpectedly became a giant value" );
	mpCompetitionSeriesReport report;
	CHECK( report.ValidateInvariants() );
	CHECK( !report.IsInitialized() );
	char untouched[ 64 ];
	memset( untouched, 0x5a, sizeof( untouched ) );
	CHECK( report.SerializeCanonicalJson( untouched, sizeof( untouched ) ).code ==
		MP_SERIES_REPORT_SERIALIZE_INVALID_STATE );
	for ( int index = 0; index < static_cast<int>( sizeof( untouched ) ); ++index ) {
		CHECK( untouched[ index ] == 0x5a );
	}

	mpSeriesReportIdentityInput invalid = Identity();
	invalid.seriesId = 0;
	CHECK( report.Initialize( invalid ).reason == MP_SERIES_REPORT_REASON_INVALID_ARGUMENT );
	invalid = Identity();
	invalid.rulesDigest = 0;
	CHECK( report.Initialize( invalid ).reason ==
		MP_SERIES_REPORT_REASON_INVALID_RULES_IDENTITY );
	invalid = Identity();
	invalid.bestOf = 2;
	CHECK( report.Initialize( invalid ).reason == MP_SERIES_REPORT_REASON_INVALID_BEST_OF );
	invalid = Identity();
	invalid.contestants[ 1 ].participantSequence = 101;
	CHECK( report.Initialize( invalid ).reason == MP_SERIES_REPORT_REASON_INVALID_ARGUMENT );
	invalid = Identity();
	invalid.contestants[ 1 ].label = "\xc0\xaf";
	CHECK( report.Initialize( invalid ).reason == MP_SERIES_REPORT_REASON_INVALID_ARGUMENT );
	CHECK( !report.IsInitialized() );

	mpSeriesReportIdentityInput identity = Identity();
	CHECK( ChangedOnce( report.Initialize( identity ) ) );
	CHECK( report.GetReportRevision() == 1 );
	CHECK( report.Initialize( identity ).code == MP_SERIES_REPORT_WRITE_NO_CHANGE );
	CHECK( report.GetReportRevision() == 1 );
	invalid = Identity();
	invalid.seriesId++;
	CHECK( report.Initialize( invalid ).reason ==
		MP_SERIES_REPORT_REASON_IDENTITY_CONFLICT );
	CHECK( report.GetIdentity().seriesId == identity.seriesId );
	CHECK( report.ValidateInvariants() );

	static const char *unsafeMaps[] = {
		"", "/q4dm1", "q4dm1/", "maps//q4dm1", "maps/../q4dm1",
		"maps\\q4dm1", "maps/q4dm1.cfg", "maps/q4 dm1", "C:q4dm1"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( unsafeMaps ) /
			sizeof( unsafeMaps[ 0 ] ) ); ++index ) {
		CHECK( !MPMatchSeriesReportIsSafeMapToken( unsafeMaps[ index ] ) );
	}
	CHECK( MPMatchSeriesReportIsSafeMapToken( "maps/mp/q4dm1" ) );

	static const char *unsafeEvidence[] = {
		"", "/match-results/session-1.json", "match-results\\session-1.json",
		"match-results/../session-1.json", "match-results//session-1.json",
		"match-results/session 1.json", "match-results/session-1.json.pending-1",
		"demos/session-1.json"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( unsafeEvidence ) /
			sizeof( unsafeEvidence[ 0 ] ) ); ++index ) {
		CHECK( !MPMatchSeriesReportIsSafeArtifactQPath(
			MP_SERIES_REPORT_ARTIFACT_EVIDENCE, unsafeEvidence[ index ] ) );
	}
	CHECK( MPMatchSeriesReportIsSafeArtifactQPath(
		MP_SERIES_REPORT_ARTIFACT_EVIDENCE,
		"match-results/session-9001_series-7_q4dm1.json" ) );
	CHECK( MPMatchSeriesReportIsSafeArtifactQPath(
		MP_SERIES_REPORT_ARTIFACT_MVD, "demos/series-7/map_001.mvd" ) );
	CHECK( !MPMatchSeriesReportIsSafeArtifactQPath(
		MP_SERIES_REPORT_ARTIFACT_MVD, "demos/series-7/map_001.demo" ) );
	char overlongPath[ MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES + 2 ];
	memset( overlongPath, 'a', sizeof( overlongPath ) );
	memcpy( overlongPath, "demos/", 6 );
	memcpy( overlongPath + sizeof( overlongPath ) - 5, ".mvd", 5 );
	CHECK( !MPMatchSeriesReportIsSafeArtifactQPath(
		MP_SERIES_REPORT_ARTIFACT_MVD, overlongPath ) );

	// Pending MVD rows have an explicit staged qpath and can only move forward.
	mpCompetitionSeriesReport reconciliation;
	CHECK( ChangedOnce( reconciliation.Initialize( identity ) ) );
	mpSeriesReportMapResultInput pendingMap = Map( 1, "maps/mp/pending",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	pendingMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].status =
		MP_SERIES_REPORT_ARTIFACT_PENDING;
	pendingMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].reason = 41;
	pendingMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].qpath =
		"demos/series-7/operator.mvd.part";
	CHECK( ChangedOnce( reconciliation.AppendMapResult( pendingMap ) ) );
	mpSeriesReportArtifactInput pendingUpdate = {};
	pendingUpdate.status = MP_SERIES_REPORT_ARTIFACT_PENDING;
	pendingUpdate.reason = 42;
	pendingUpdate.qpath = "demos/series-7/operator.mvd.part";
	CHECK( ChangedOnce( reconciliation.ReconcileMapArtifact( 1,
		MP_SERIES_REPORT_ARTIFACT_MVD, pendingUpdate ) ) );
	pendingUpdate.qpath = "demos/series-7/other.mvd.part";
	CHECK( reconciliation.ReconcileMapArtifact( 1,
		MP_SERIES_REPORT_ARTIFACT_MVD, pendingUpdate ).reason ==
		MP_SERIES_REPORT_REASON_ARTIFACT_RECONCILIATION_CONFLICT );
	mpSeriesReportArtifactInput published = {};
	published.status = MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
	published.qpath = "demos/series-7/operator.mvd";
	CHECK( ChangedOnce( reconciliation.ReconcileMapArtifact( 1,
		MP_SERIES_REPORT_ARTIFACT_MVD, published ) ) );
	CHECK( reconciliation.ReconcileMapArtifact( 1,
		MP_SERIES_REPORT_ARTIFACT_MVD, published ).code ==
		MP_SERIES_REPORT_WRITE_NO_CHANGE );
	mpSeriesReportArtifactInput overwrite = {};
	overwrite.status = MP_SERIES_REPORT_ARTIFACT_FAILED;
	overwrite.reason = 43;
	overwrite.qpath = "demos/series-7/operator.mvd.part";
	CHECK( reconciliation.ReconcileMapArtifact( 1,
		MP_SERIES_REPORT_ARTIFACT_MVD, overwrite ).reason ==
		MP_SERIES_REPORT_REASON_ARTIFACT_RECONCILIATION_CONFLICT );
	mpSeriesReportMapResultInput failedPending = Map( 2, "maps/mp/failed",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	failedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].status =
		MP_SERIES_REPORT_ARTIFACT_PENDING;
	failedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].reason = 44;
	failedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].qpath =
		"demos/series-7/failed.mvd.part";
	CHECK( ChangedOnce( reconciliation.AppendMapResult( failedPending ) ) );
	mpSeriesReportArtifactInput failed = {};
	failed.status = MP_SERIES_REPORT_ARTIFACT_FAILED;
	failed.reason = 45;
	failed.qpath = "demos/series-7/failed.mvd.part";
	CHECK( ChangedOnce( reconciliation.ReconcileMapArtifact( 2,
		MP_SERIES_REPORT_ARTIFACT_MVD, failed ) ) );
	mpSeriesReportMapResultInput sealedPending = Map( 3, "maps/mp/sealed",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	sealedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].status =
		MP_SERIES_REPORT_ARTIFACT_PENDING;
	sealedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].reason = 46;
	sealedPending.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].qpath =
		"demos/series-7/sealed.mvd.part";
	CHECK( ChangedOnce( reconciliation.AppendMapResult( sealedPending ) ) );
	mpSeriesReportFinalInput sealedFinal = {};
	sealedFinal.outcome = MP_SERIES_REPORT_FINAL_CANCELLED;
	sealedFinal.reason = 47;
	sealedFinal.winnerContestant = MP_SERIES_REPORT_CONTESTANT_NONE;
	sealedFinal.authorizer = MPSeriesReportSystemAuthorizer();
	CHECK( ChangedOnce( reconciliation.Finalize( sealedFinal ) ) );
	overwrite.qpath = "demos/series-7/sealed.mvd.part";
	CHECK( reconciliation.ReconcileMapArtifact( 3,
		MP_SERIES_REPORT_ARTIFACT_MVD, overwrite ).reason ==
		MP_SERIES_REPORT_REASON_FINALIZED );
	CHECK( reconciliation.GetMapResult( 2 )->artifacts[
		MP_SERIES_REPORT_ARTIFACT_MVD ].status ==
		MP_SERIES_REPORT_ARTIFACT_PENDING );
	CHECK( reconciliation.ValidateInvariants() );

	mpSeriesReportMapResultInput map1 = Map( 1, "maps/mp/q4dm1",
		MP_SERIES_REPORT_MAP_DECIDED, 0, 15, 8 );
	map1.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].status =
		MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
	map1.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].qpath =
		"match-results/session-9001_series-7_q4dm1.json";
	map1.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].status =
		MP_SERIES_REPORT_ARTIFACT_FAILED;
	map1.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].reason = 71;
	const uint64_t beforeMap = report.GetReportRevision();
	CHECK( ChangedOnce( report.AppendMapResult( map1 ) ) );
	CHECK( report.GetReportRevision() == beforeMap + 1 );
	CHECK( report.AppendMapResult( map1 ).code == MP_SERIES_REPORT_WRITE_NO_CHANGE );
	map1.score[ 0 ]++;
	CHECK( report.AppendMapResult( map1 ).reason ==
		MP_SERIES_REPORT_REASON_MAP_ATTEMPT_CONFLICT );
	map1.score[ 0 ]--;

	mpSeriesReportMapResultInput badMap = Map( 2, "maps/mp/q4dm2",
		MP_SERIES_REPORT_MAP_ABORTED, 0, 4, 4 );
	CHECK( report.AppendMapResult( badMap ).reason ==
		MP_SERIES_REPORT_REASON_INVALID_MAP_RESULT );
	badMap.winnerContestant = MP_SERIES_REPORT_CONTESTANT_NONE;
	badMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].status =
		MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
	badMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].qpath =
		"match-results/../escape.json";
	CHECK( report.AppendMapResult( badMap ).reason ==
		MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH );
	badMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].status =
		MP_SERIES_REPORT_ARTIFACT_DROPPED;
	badMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].reason = 72;
	badMap.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].qpath = "";
	CHECK( ChangedOnce( report.AppendMapResult( badMap ) ) );

	mpSeriesReportMapResultInput oldMap = Map( 1, "maps/mp/old",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	CHECK( report.AppendMapResult( oldMap ).reason ==
		MP_SERIES_REPORT_REASON_MAP_ATTEMPT_CONFLICT );
	mpSeriesReportMapResultInput map3 = Map( 3, "maps/mp/q4dm3",
		MP_SERIES_REPORT_MAP_DECIDED, 0, 12, 10 );
	map3.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].status =
		MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
	map3.artifacts[ MP_SERIES_REPORT_ARTIFACT_EVIDENCE ].qpath =
		"match-results/session-9003_series-7_q4dm3.json";
	map3.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].status =
		MP_SERIES_REPORT_ARTIFACT_AVAILABLE;
	map3.artifacts[ MP_SERIES_REPORT_ARTIFACT_MVD ].qpath =
		"demos/series-7/map_003.mvd";
	CHECK( ChangedOnce( report.AppendMapResult( map3 ) ) );
	CHECK( report.GetMapResultCount() == 3 );
	CHECK( report.GetMapResult( -1 ) == NULL );
	CHECK( report.GetMapResult( 3 ) == NULL );

	mpSeriesReportParticipantStatsInput participant = {};
	participant.participantSequence = 101;
	participant.contestant = 0;
	participant.displayName = identity.contestants[ 0 ].label;
	participant.mapsPlayed = 2;
	participant.mapsWon = 2;
	participant.score = 27;
	participant.kills = 27;
	participant.deaths = 18;
	participant.damageGiven = 4100;
	participant.damageReceived = 3000;
	participant.shots = 100;
	participant.hits = 44;
	CHECK( ChangedOnce( report.RecordParticipantStats( participant ) ) );
	CHECK( report.RecordParticipantStats( participant ).code ==
		MP_SERIES_REPORT_WRITE_NO_CHANGE );
	participant.hits = 101;
	CHECK( report.RecordParticipantStats( participant ).reason ==
		MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS );
	participant.hits = 44;
	participant.score++;
	CHECK( report.RecordParticipantStats( participant ).reason ==
		MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CONFLICT );
	participant.score--;

	mpSeriesReportTeamStatsInput team = {};
	team.contestant = 0;
	team.mapsPlayed = 2;
	team.mapsWon = 2;
	team.score = 27;
	team.roundsWon = 2;
	team.damageGiven = 4100;
	CHECK( ChangedOnce( report.RecordTeamStats( team ) ) );
	team.contestant = 1;
	team.mapsWon = 0;
	team.score = 18;
	team.damageGiven = 3000;
	CHECK( ChangedOnce( report.RecordTeamStats( team ) ) );
	CHECK( report.ValidateInvariants() );

	mpSeriesReportFinalInput finalResult = {};
	finalResult.outcome = MP_SERIES_REPORT_FINAL_COMPLETE;
	finalResult.reason = 91;
	finalResult.winnerContestant = 1;
	finalResult.authorizer = MPSeriesReportSystemAuthorizer();
	CHECK( report.Finalize( finalResult ).reason ==
		MP_SERIES_REPORT_REASON_INVALID_FINAL_OUTCOME );
	finalResult.winnerContestant = 0;
	CHECK( ChangedOnce( report.Finalize( finalResult ) ) );
	CHECK( report.IsFinalized() );
	CHECK( report.Finalize( finalResult ).code == MP_SERIES_REPORT_WRITE_NO_CHANGE );
	mpSeriesReportFinalInput conflictFinal = finalResult;
	conflictFinal.reason++;
	CHECK( report.Finalize( conflictFinal ).reason ==
		MP_SERIES_REPORT_REASON_FINALIZATION_CONFLICT );
	mpSeriesReportMapResultInput postFinal = Map( 4, "maps/mp/q4dm4",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	CHECK( report.AppendMapResult( postFinal ).reason ==
		MP_SERIES_REPORT_REASON_FINALIZED );
	CHECK( report.ValidateInvariants() );

	char tiny[ 32 ];
	memset( tiny, 0x6b, sizeof( tiny ) );
	mpSeriesReportSerializeResult tooSmall = report.SerializeCanonicalJson(
		tiny, static_cast<int>( sizeof( tiny ) ) );
	CHECK( tooSmall.code == MP_SERIES_REPORT_SERIALIZE_BUFFER_TOO_SMALL );
	CHECK( tooSmall.requiredCapacity > static_cast<int>( sizeof( tiny ) ) );
	for ( int index = 0; index < static_cast<int>( sizeof( tiny ) ); ++index ) {
		CHECK( tiny[ index ] == 0x6b );
	}
	char json[ 32768 ];
	char second[ 32768 ];
	mpSeriesReportSerializeResult serialized = report.SerializeCanonicalJson(
		json, static_cast<int>( sizeof( json ) ) );
	CHECK( serialized.Succeeded() );
	CHECK( serialized.requiredCapacity == serialized.bytesWritten + 1 );
	mpSeriesReportSerializeResult replay = report.SerializeCanonicalJson(
		second, static_cast<int>( sizeof( second ) ) );
	CHECK( replay.Succeeded() && replay.bytesWritten == serialized.bytesWritten );
	CHECK( strcmp( json, second ) == 0 );
	CHECK( strstr( json, "\"digest\":\"0123456789abcdef\"" ) != NULL );
	CHECK( strstr( json, "\"outcome\":\"complete\"" ) != NULL );
	CHECK( strstr( json, "Quo\\\\te\\\"\\n\\u0001\xc3\xa9" ) != NULL );
	for ( int index = 0; index < serialized.bytesWritten; ++index ) {
		CHECK( static_cast<unsigned char>( json[ index ] ) >= 0x20 );
	}
	static const char *privateKeys[] = {
		"ipAddress", "credential", "password", "secret", "userinfo", "roleMask"
	};
	for ( int index = 0; index < static_cast<int>( sizeof( privateKeys ) /
			sizeof( privateKeys[ 0 ] ) ); ++index ) {
		CHECK( strstr( json, privateKeys[ index ] ) == NULL );
	}

	// A separate custom team series can be cancelled exactly once without a map.
	mpCompetitionSeriesReport cancelled;
	mpSeriesReportIdentityInput teamIdentity = {};
	teamIdentity.seriesId = 77;
	teamIdentity.profile = MP_SERIES_PROFILE_CUSTOM;
	teamIdentity.profileKey = "league_bo7";
	teamIdentity.bestOf = 7;
	teamIdentity.rulesSchema = 2;
	teamIdentity.rulesRevision = 3;
	teamIdentity.rulesDigest = 44;
	teamIdentity.gameType = 3;
	teamIdentity.modeToken = "tdm";
	teamIdentity.contestants[ 0 ].kind = MP_SERIES_REPORT_CONTESTANT_SIDE;
	teamIdentity.contestants[ 0 ].label = "Marine";
	teamIdentity.contestants[ 1 ].kind = MP_SERIES_REPORT_CONTESTANT_SIDE;
	teamIdentity.contestants[ 1 ].label = "Strogg";
	CHECK( ChangedOnce( cancelled.Initialize( teamIdentity ) ) );
	mpSeriesReportFinalInput cancelledFinal = {};
	cancelledFinal.outcome = MP_SERIES_REPORT_FINAL_CANCELLED;
	cancelledFinal.reason = 92;
	cancelledFinal.winnerContestant = MP_SERIES_REPORT_CONTESTANT_NONE;
	cancelledFinal.authorizer = MPSeriesReportServerOperatorAuthorizer();
	CHECK( ChangedOnce( cancelled.Finalize( cancelledFinal ) ) );
	CHECK( cancelled.ValidateInvariants() );
	CHECK( cancelled.SerializeCanonicalJson( second,
		static_cast<int>( sizeof( second ) ) ).Succeeded() );
	CHECK( strstr( second, "\"outcome\":\"cancelled\"" ) != NULL );

	// Exercise fixed capacities and explicit drops without permitting a partial
	// winning report to masquerade as complete.
	mpCompetitionSeriesReport bounded;
	teamIdentity.seriesId = 88;
	teamIdentity.profileKey = "endurance_bo15";
	teamIdentity.bestOf = 15;
	CHECK( ChangedOnce( bounded.Initialize( teamIdentity ) ) );
	for ( uint32_t attempt = 1; attempt <= MP_SERIES_REPORT_MAX_MAP_RESULTS;
			++attempt ) {
		mpSeriesReportMapResultInput aborted = Map( attempt, "maps/mp/retry",
			MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
		aborted.rulesDigest = teamIdentity.rulesDigest;
		CHECK( ChangedOnce( bounded.AppendMapResult( aborted ) ) );
	}
	mpSeriesReportMapResultInput overflow = Map(
		MP_SERIES_REPORT_MAX_MAP_RESULTS + 1, "maps/mp/retry",
		MP_SERIES_REPORT_MAP_ABORTED, MP_SERIES_REPORT_CONTESTANT_NONE, 0, 0 );
	overflow.rulesDigest = teamIdentity.rulesDigest;
	CHECK( bounded.AppendMapResult( overflow ).code ==
		MP_SERIES_REPORT_WRITE_DROPPED );
	CHECK( bounded.GetDroppedMapResultCount() == 1 );
	for ( uint32_t index = 0; index < MP_SERIES_REPORT_MAX_PARTICIPANTS; ++index ) {
		mpSeriesReportParticipantStatsInput stats = {};
		stats.participantSequence = 1000 + index;
		stats.contestant = static_cast<int>( index & 1 );
		stats.displayName = "bounded";
		CHECK( ChangedOnce( bounded.RecordParticipantStats( stats ) ) );
	}
	mpSeriesReportParticipantStatsInput overflowStats = {};
	overflowStats.participantSequence = 9999;
	overflowStats.contestant = 0;
	overflowStats.displayName = "overflow";
	CHECK( bounded.RecordParticipantStats( overflowStats ).code ==
		MP_SERIES_REPORT_WRITE_DROPPED );
	CHECK( bounded.GetDroppedParticipantStatsCount() == 1 );
	CHECK( ChangedOnce( bounded.Finalize( cancelledFinal ) ) );
	CHECK( bounded.ValidateInvariants() );

	CHECK( fwrite( json, 1, static_cast<size_t>( serialized.bytesWritten ), stdout ) ==
		static_cast<size_t>( serialized.bytesWritten ) );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_series_report_contract: native checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-series-report-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_series_report_contract.cpp"
        executable = temp_dir / (
            "match_series_report_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_series_report_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_SERIES_REPORT_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone series-report contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"series-report invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )

        try:
            artifact = json.loads(ran.stdout.decode("utf-8", errors="strict"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AssertionError(f"report is not canonical UTF-8 JSON: {error}") from error
        if artifact["schema"] != 1 or artifact["seriesId"] != 0x1122334455667788:
            raise AssertionError("serialized report identity differs from committed identity")
        if artifact["profile"] != {
            "id": 1,
            "key": "best_of_three",
            "bestOf": 3,
        }:
            raise AssertionError("serialized profile identity drifted")
        if artifact["rules"]["digest"] != "0123456789abcdef":
            raise AssertionError("rules identity is not fixed-width canonical hexadecimal")
        if [entry["attempt"] for entry in artifact["maps"]["entries"]] != [1, 2, 3]:
            raise AssertionError("map results are not in committed attempt order")
        if artifact["seriesScore"] != [2, 0]:
            raise AssertionError("derived final series score differs from map results")
        if artifact["output"] != {
            "evidence": {"notRequested": 0, "available": 2, "pending": 0, "failed": 0, "dropped": 1},
            "mvd": {"notRequested": 1, "available": 1, "pending": 0, "failed": 1, "dropped": 0},
            "dropCounterSaturated": False,
        }:
            raise AssertionError("artifact output/drop summary differs from map truth")
        if artifact["final"] != {
            "outcome": "complete",
            "reason": 91,
            "winner": 0,
            "authorizer": {"kind": "system", "participant": 0},
        }:
            raise AssertionError("final exactly-once outcome differs from committed result")

        canonical_digest = hashlib.sha256(ran.stdout).hexdigest()
        expected_digest = "e03adbc26a27609102ed6a53bc7db97945107acc5cb0d902a7b6b8ea5a5d9d07"
        if canonical_digest != expected_digest:
            raise AssertionError(
                f"canonical series-report digest drifted: {canonical_digest}"
            )
        print(f"mp_match_series_report_contract: canonical sha256 {canonical_digest}")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_series_report_contract: PASS")


if __name__ == "__main__":
    main()
