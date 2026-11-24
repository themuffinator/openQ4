#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and native contracts for the recipient-safe evidence projection."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchEvidenceView.h"
SOURCE = ROOT / "src/game/mp/match/MatchEvidenceView.cpp"
EVIDENCE_SOURCE = ROOT / "src/game/mp/match/MatchEvidence.cpp"


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
        "mpMatchEvidenceViewLifecycle_t",
        "bool\tinitialized;",
        "bool\tfinalized;",
        "bool\tpersisted;",
        "bool\tmvdRequired;",
        "bool\tmvdRecording;",
        "MPMatchEvidenceBuildView",
        "MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_LIFECYCLE",
        "MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVIDENCE",
        "ValidateInvariants()",
        "ProjectEventKind",
        "ProjectMVDState",
        "AddDroppedRecords",
        "0xffffffffu",
        "ValidateProjection",
        "summary = candidate",
        "MP_MATCH_EVIDENCE_VIEW_STANDALONE_TEST",
    ):
        require(combined, token, "evidence view projection")

    if "qpath" in combined.lower():
        raise AssertionError("recipient evidence projection may not inspect or expose qpaths")
    for token in (
        "idFile",
        "idBitMsg",
        "idCVar",
        "idUserInterface",
        "cmdSystem",
        "gameLocal",
        "idMultiplayerGame",
        "MultiplayerGame.h",
        "std::string",
        "std::vector",
        "StartMVD(",
        "StopMVD(",
    ):
        if token in combined:
            raise AssertionError(f"pure evidence projection contains forbidden dependency {token!r}")
    if re.search(r"\bnew\s+", combined) or re.search(r"\bdelete\s+", combined):
        raise AssertionError("evidence view projection must remain allocation-free")

    assign_index = source.rfind("summary = candidate")
    validation_index = source.rfind("ValidateProjection( candidate )")
    if assign_index < validation_index:
        raise AssertionError("projection output can commit before final validation")

    listed = subprocess.run(
        [
            shutil.which("python") or "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.splitlines()
    if "game/mp/match/MatchEvidenceView.cpp" not in listed:
        raise AssertionError("Meson source discovery omitted MatchEvidenceView.cpp")


HARNESS = r'''
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned char byte;

class idBitMsg {
public:
	idBitMsg() : data( 0 ), capacity( 0 ), size( 0 ), readCount( 0 ),
		overflowed( false ), allowOverflow( false ) {}
	void Init( byte *value, int bytes ) { data = value; capacity = bytes;
		size = 0; readCount = 0; overflowed = false; }
	void Init( const byte *value, int bytes ) { data = const_cast<byte *>( value );
		capacity = bytes; size = bytes; readCount = 0; overflowed = false; }
	void SetAllowOverflow( bool value ) { allowOverflow = value; }
	void BeginWriting() { size = 0; readCount = 0; overflowed = false; }
	void BeginReading() const { readCount = 0; }
	void SetSize( int value ) { size = value; }
	int GetSize() const { return size; }
	const byte *GetData() const { return data; }
	bool IsOverflowed() const { return overflowed; }
	int GetWriteBit() const { return 0; }
	int GetReadBit() const { return 0; }
	int GetRemainingWriteBits() const { return ( capacity - size ) * 8; }
	int GetRemainingReadBits() const { return ( size - readCount ) * 8; }
	void SaveWriteState( int &savedSize, int &savedBit ) const {
		savedSize = size; savedBit = 0; }
	void RestoreWriteState( int savedSize, int ) { size = savedSize;
		overflowed = false; }
	void SaveReadState( int &savedCount, int &savedBit ) const {
		savedCount = readCount; savedBit = 0; }
	void RestoreReadState( int savedCount, int ) const { readCount = savedCount; }
	void WriteByte( int value ) { byte raw = static_cast<byte>( value );
		WriteData( &raw, 1 ); }
	void WriteUShort( int value ) { byte raw[ 2 ] = {
		static_cast<byte>( value ), static_cast<byte>( value >> 8 ) };
		WriteData( raw, 2 ); }
	void WriteLong( int value ) { uint32_t rawValue = static_cast<uint32_t>( value );
		byte raw[ 4 ] = { static_cast<byte>( rawValue ),
			static_cast<byte>( rawValue >> 8 ), static_cast<byte>( rawValue >> 16 ),
			static_cast<byte>( rawValue >> 24 ) }; WriteData( raw, 4 ); }
	void WriteData( const void *source, int bytes ) {
		if ( bytes < 0 || size > capacity - bytes ) { overflowed = true;
			if ( !allowOverflow ) return; size = capacity; return; }
		memcpy( data + size, source, bytes ); size += bytes;
	}
	int ReadByte() const { byte raw = 0;
		return ReadData( &raw, 1 ) == 1 ? raw : 0; }
	int ReadUShort() const { byte raw[ 2 ] = { 0, 0 };
		return ReadData( raw, 2 ) == 2 ? raw[ 0 ] | ( raw[ 1 ] << 8 ) : 0; }
	int ReadLong() const { byte raw[ 4 ] = { 0, 0, 0, 0 };
		return ReadData( raw, 4 ) == 4 ? static_cast<int>(
			static_cast<uint32_t>( raw[ 0 ] ) |
			( static_cast<uint32_t>( raw[ 1 ] ) << 8 ) |
			( static_cast<uint32_t>( raw[ 2 ] ) << 16 ) |
			( static_cast<uint32_t>( raw[ 3 ] ) << 24 ) ) : 0; }
	int ReadData( void *target, int bytes ) const {
		if ( bytes < 0 || readCount > size - bytes ) { overflowed = true; return 0; }
		memcpy( target, data + readCount, bytes ); readCount += bytes; return bytes;
	}
private:
	byte *data;
	int capacity;
	int size;
	mutable int readCount;
	mutable bool overflowed;
	bool allowOverflow;
};

#define MP_MATCH_VIEW_STANDALONE_TEST 1
#include "game/mp/match/MatchView.cpp"

#define private public
#include "game/mp/match/MatchEvidenceView.h"
#undef private

mpMatchLocalizationId_t MPMatchProtocolReasonLocalizationId(
		mpMatchProtocolReason_t reason ) {
	return reason == MP_MATCH_PROTOCOL_REASON_NONE ? MP_MATCH_LOCALIZATION_NONE :
		static_cast<mpMatchLocalizationId_t>(
			MP_MATCH_LOCALIZATION_REASON_BASE + reason );
}

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )

static mpEvidenceCommittedStamp Stamp( uint64_t revision ) {
	mpEvidenceCommittedStamp stamp = {};
	stamp.sessionRevision = revision;
	stamp.matchTimeMsec = revision * 10;
	stamp.hostTimeUtcMsec = UINT64_C( 1770000000000 ) + revision;
	return stamp;
}

static mpMatchEvidenceViewLifecycle_t Lifecycle( bool initialized,
		bool finalized, bool persisted, bool required, bool recording ) {
	mpMatchEvidenceViewLifecycle_t lifecycle = {};
	lifecycle.initialized = initialized;
	lifecycle.finalized = finalized;
	lifecycle.persisted = persisted;
	lifecycle.mvdRequired = required;
	lifecycle.mvdRecording = recording;
	return lifecycle;
}

static bool Accepted( const mpEvidenceWriteResult &result ) {
	return result.WasAccepted();
}

static bool ResetEvidence( mpMatchEvidence &evidence ) {
	mpEvidenceMetadataInput metadata = {};
	metadata.sessionId = UINT64_C( 0x1122334455667788 );
	metadata.seriesId = 0;
	metadata.rulesDigest = UINT64_C( 0x0123456789abcdef );
	metadata.modeId = 4;
	metadata.build = "openQ4-test";
	metadata.map = "maps/mp/test";
	metadata.mode = "duel";
	return evidence.Reset( metadata );
}

static bool AppendPhase( mpMatchEvidence &evidence, uint64_t revision ) {
	mpEvidencePhaseTransition phase = {};
	phase.from = WARMUP;
	phase.to = COUNTDOWN;
	phase.reason = 1;
	phase.actor = MPEvidenceSystemActor();
	return Accepted( evidence.AppendPhaseTransition( Stamp( revision ), phase ) );
}

static bool AppendAllKinds( mpMatchEvidence &evidence ) {
	mpEvidencePhaseTransition phase = {};
	phase.from = WARMUP;
	phase.to = COUNTDOWN;
	phase.reason = 1;
	phase.actor = MPEvidenceSystemActor();
	if ( !Accepted( evidence.AppendPhaseTransition( Stamp( 1 ), phase ) ) ) return false;

	mpEvidenceRoundTransition round = {};
	round.from = RS_COUNTDOWN;
	round.to = RS_ACTIVE;
	round.reason = 2;
	if ( !Accepted( evidence.AppendRoundTransition( Stamp( 2 ), round ) ) ) return false;

	mpEvidencePauseTransition pause = {};
	pause.from = MP_EVIDENCE_PAUSE_RUNNING;
	pause.to = MP_EVIDENCE_PAUSE_PENDING;
	pause.kind = MP_EVIDENCE_PAUSE_TEAM_TIMEOUT;
	pause.ownerSide = 0;
	pause.reason = 3;
	pause.actor = MPEvidenceSystemActor();
	if ( !Accepted( evidence.AppendPauseTransition( Stamp( 3 ), pause ) ) ) return false;

	mpEvidenceRoleChange role = {};
	role.targetParticipant = 1;
	role.previousRoles = 0;
	role.currentRoles = 1;
	role.authorizer = MPEvidenceServerOperatorActor();
	if ( !Accepted( evidence.AppendRoleChange( Stamp( 4 ), role ) ) ) return false;

	mpEvidenceProposalEvent proposal = {};
	proposal.proposalId = 7;
	proposal.action = MP_EVIDENCE_PROPOSAL_CREATED;
	proposal.opcode = 4;
	proposal.scopeSide = -1;
	proposal.actor = MPEvidenceSystemActor();
	if ( !Accepted( evidence.AppendProposal( Stamp( 5 ), proposal ) ) ) return false;

	mpEvidenceRosterEvent roster = {};
	roster.action = MP_EVIDENCE_ROSTER_SEAT_DECLARED;
	roster.seat = 0;
	roster.side = 0;
	roster.role = MP_EVIDENCE_ROSTER_CAPTAIN;
	roster.authorizer = MPEvidenceServerOperatorActor();
	if ( !Accepted( evidence.AppendRosterChange( Stamp( 6 ), roster ) ) ) return false;

	mpEvidenceMapResult result = {};
	result.outcome = MP_EVIDENCE_RESULT_DECIDED;
	result.winnerSide = 0;
	result.sideScore[ 0 ] = 20;
	result.sideScore[ 1 ] = 17;
	result.reason = 5;
	result.authorizer = MPEvidenceSystemActor();
	if ( !Accepted( evidence.AppendMapResult( Stamp( 7 ), result ) ) ) return false;

	mpEvidenceOutputFailure output = {};
	output.output = MP_EVIDENCE_OUTPUT_MVD_START;
	output.reason = 6;
	return Accepted( evidence.AppendOutputFailure( Stamp( 8 ), output ) );
}

static bool LinkMVDArtifact( mpMatchEvidence &evidence, uint64_t revision ) {
	mpEvidenceArtifactLinkInput artifact = {};
	artifact.kind = MP_EVIDENCE_ARTIFACT_MVD;
	artifact.qpath = "demos/series-1/map-1.mvd";
	return Accepted( evidence.LinkArtifact( Stamp( revision ), artifact ) );
}

static bool AppendStopFailure( mpMatchEvidence &evidence, uint64_t revision ) {
	mpEvidenceOutputFailure output = {};
	output.output = MP_EVIDENCE_OUTPUT_MVD_STOP;
	output.reason = 9;
	return Accepted( evidence.AppendOutputFailure( Stamp( revision ), output ) );
}

static bool RecordStats( mpMatchEvidence &evidence, uint64_t revision ) {
	mpEvidenceParticipantStatsInput participant = {};
	participant.participantSequence = 1;
	participant.side = 0;
	participant.displayName = "player";
	participant.score = 20;
	participant.kills = 20;
	participant.deaths = 17;
	participant.damageGiven = 4200;
	participant.damageReceived = 3900;
	participant.shots = 100;
	participant.hits = 42;
	return Accepted( evidence.RecordParticipantFinalStats(
			Stamp( revision ), participant ) ) &&
		Accepted( evidence.RecordTeamFinalStats(
			Stamp( revision ), 0, 20, 0, 0, 4200 ) ) &&
		Accepted( evidence.RecordTeamFinalStats(
			Stamp( revision ), 1, 17, 0, 0, 3900 ) );
}

static bool SameBytes( const mpMatchViewEvidenceSummary_t &left,
		const mpMatchViewEvidenceSummary_t &right ) {
	return memcmp( &left, &right, sizeof( left ) ) == 0;
}

static bool BuildsValidView( const mpMatchEvidence &evidence,
		const mpMatchEvidenceViewLifecycle_t &lifecycle,
		mpMatchViewEvidenceSummary_t &summary ) {
	return MPMatchEvidenceBuildView( evidence, lifecycle, summary ).Succeeded() &&
		ValidateEvidence( summary, NULL );
}

int main() {
	mpMatchViewEvidenceSummary_t summary;
	memset( &summary, 0x5a, sizeof( summary ) );

	// A match that was expected to capture evidence but never initialized is a
	// visible failure, with no residual counters or local artifact details.
	mpMatchEvidence empty;
	mpMatchEvidenceViewResult_t projected = MPMatchEvidenceBuildView( empty,
		Lifecycle( false, false, false, false, false ), summary );
	CHECK( projected.Succeeded() );
	CHECK( ValidateEvidence( summary, NULL ) );
	CHECK( summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_FAILED );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_DISABLED );
	CHECK( summary.reportState == MP_MATCH_VIEW_REPORT_FAILED );
	CHECK( summary.evidenceRevision == 0 && summary.eventCount == 0 );
	CHECK( summary.recentEventCount == 0 );
	for ( int index = 0; index < MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS; ++index ) {
		CHECK( summary.recentEventKinds[ index ] ==
			MP_MATCH_VIEW_EVIDENCE_EVENT_NONE );
	}
	CHECK( BuildsValidView( empty,
		Lifecycle( false, false, false, true, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_FAILED );

	// Lifecycle contradictions are rejected transactionally.
	memset( &summary, 0xa5, sizeof( summary ) );
	mpMatchViewEvidenceSummary_t before = summary;
	projected = MPMatchEvidenceBuildView( empty,
		Lifecycle( false, true, false, false, false ), summary );
	CHECK( !projected.Succeeded() );
	CHECK( projected.reason == MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_LIFECYCLE );
	CHECK( SameBytes( summary, before ) );
	projected = MPMatchEvidenceBuildView( empty,
		Lifecycle( false, false, true, false, false ), summary );
	CHECK( !projected.Succeeded() && SameBytes( summary, before ) );
	projected = MPMatchEvidenceBuildView( empty,
		Lifecycle( false, false, false, false, true ), summary );
	CHECK( !projected.Succeeded() && SameBytes( summary, before ) );

	mpMatchEvidence clean;
	CHECK( ResetEvidence( clean ) );
	CHECK( AppendPhase( clean, 1 ) );
	CHECK( clean.ValidateInvariants() );
	projected = MPMatchEvidenceBuildView( clean,
		Lifecycle( false, false, false, false, false ), summary );
	CHECK( !projected.Succeeded() );
	CHECK( projected.reason == MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_LIFECYCLE );
	CHECK( SameBytes( summary, before ) );

	// Capturing and report state come strictly from explicit lifecycle facts.
	CHECK( BuildsValidView( clean,
		Lifecycle( true, false, false, false, false ), summary ) );
	CHECK( summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_CAPTURING );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_DISABLED );
	CHECK( summary.reportState == MP_MATCH_VIEW_REPORT_PENDING );
	CHECK( summary.evidenceRevision == clean.GetEvidenceRevision() );
	CHECK( summary.eventCount == 1 && summary.recentEventCount == 1 );
	CHECK( summary.recentEventKinds[ 0 ] ==
		MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION );
	CHECK( BuildsValidView( clean,
		Lifecycle( true, false, false, true, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_PENDING );
	CHECK( BuildsValidView( clean,
		Lifecycle( true, false, false, false, true ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_RECORDING );
	CHECK( BuildsValidView( clean,
		Lifecycle( true, true, false, false, false ), summary ) );
	CHECK( summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_FINALIZED );
	CHECK( summary.reportState == MP_MATCH_VIEW_REPORT_FAILED );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_DISABLED );
	CHECK( BuildsValidView( clean,
		Lifecycle( true, true, true, true, false ), summary ) );
	CHECK( summary.reportState == MP_MATCH_VIEW_REPORT_AVAILABLE );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_FAILED );

	// Tail order is chronological, results are discovered across the complete
	// bounded journal, and final statistic counts are projected exactly.
	mpMatchEvidence rich;
	CHECK( ResetEvidence( rich ) );
	CHECK( AppendAllKinds( rich ) );
	CHECK( RecordStats( rich, 9 ) );
	CHECK( rich.ValidateInvariants() );
	CHECK( BuildsValidView( rich,
		Lifecycle( true, false, false, true, false ), summary ) );
	CHECK( summary.eventCount == 8 );
	CHECK( summary.participantStatsCount == 1 );
	CHECK( summary.teamStatsCount == 2 );
	CHECK( summary.resultRecorded );
	CHECK( summary.recentEventCount == 4 );
	CHECK( summary.recentEventKinds[ 0 ] == MP_MATCH_VIEW_EVIDENCE_EVENT_PROPOSAL );
	CHECK( summary.recentEventKinds[ 1 ] == MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE );
	CHECK( summary.recentEventKinds[ 2 ] == MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT );
	CHECK( summary.recentEventKinds[ 3 ] == MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_FAILED );

	// A linked final artifact proves recovery from a start failure.  A stop
	// failure remains conservative, while a live recorder remains the strongest
	// current fact even during finalization cleanup.
	CHECK( LinkMVDArtifact( rich, 10 ) );
	CHECK( BuildsValidView( rich,
		Lifecycle( true, false, false, true, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_FAILED );
	CHECK( BuildsValidView( rich,
		Lifecycle( true, true, true, true, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_AVAILABLE );
	CHECK( summary.reportState == MP_MATCH_VIEW_REPORT_AVAILABLE );
	CHECK( AppendStopFailure( rich, 11 ) );
	CHECK( BuildsValidView( rich,
		Lifecycle( true, true, true, true, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_FAILED );
	CHECK( BuildsValidView( rich,
		Lifecycle( true, true, true, true, true ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_RECORDING );

	mpMatchEvidence optionalArtifact = clean;
	CHECK( LinkMVDArtifact( optionalArtifact, 2 ) );
	CHECK( BuildsValidView( optionalArtifact,
		Lifecycle( true, true, true, false, false ), summary ) );
	CHECK( summary.mvdState == MP_MATCH_VIEW_MVD_AVAILABLE );

	// All three 64-bit drop sources combine without wrap.  Projection overflow
	// and an already-saturated source both use the wire's canonical max marker.
	mpMatchEvidence drops = clean;
	drops.droppedEventCount = 2;
	drops.firstDroppedSessionRevision = 1;
	drops.lastDroppedSessionRevision = 1;
	drops.droppedParticipantStatsCount = 3;
	drops.droppedTeamStatsCount = 4;
	drops.evidenceRevision = 11; // reset + one event + nine drops
	CHECK( drops.ValidateInvariants() );
	CHECK( BuildsValidView( drops,
		Lifecycle( true, false, false, false, false ), summary ) );
	CHECK( summary.droppedRecordCount == 9 );
	CHECK( !summary.droppedRecordCountSaturated );

	drops.droppedEventCount = UINT64_C( 0xfffffffe );
	drops.droppedParticipantStatsCount = 2;
	drops.droppedTeamStatsCount = 0;
	drops.evidenceRevision = UINT64_C( 4294967298 );
	CHECK( drops.ValidateInvariants() );
	CHECK( BuildsValidView( drops,
		Lifecycle( true, false, false, false, false ), summary ) );
	CHECK( summary.droppedRecordCount == 0xffffffffu );
	CHECK( summary.droppedRecordCountSaturated );

	drops.dropCounterSaturated = true;
	drops.droppedEventCount = UINT64_MAX;
	drops.evidenceRevision = 2;
	CHECK( drops.ValidateInvariants() );
	CHECK( BuildsValidView( drops,
		Lifecycle( true, false, false, false, false ), summary ) );
	CHECK( summary.droppedRecordCount == 0xffffffffu );
	CHECK( summary.droppedRecordCountSaturated );

	// Invalid evidence is never partially projected.
	mpMatchEvidence corrupt = clean;
	corrupt.eventCount = -1;
	memset( &summary, 0x3c, sizeof( summary ) );
	before = summary;
	projected = MPMatchEvidenceBuildView( corrupt,
		Lifecycle( true, false, false, false, false ), summary );
	CHECK( !projected.Succeeded() );
	CHECK( projected.reason == MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVIDENCE );
	CHECK( SameBytes( summary, before ) );

	return 0;
}
'''


def native_contracts() -> None:
    compiler = next(
        (
            path
            for name in ("clang++", "g++", "c++")
            if (path := shutil.which(name))
        ),
        None,
    )
    if compiler is None:
        print("mp_match_evidence_view_contract: native checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-evidence-view-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_evidence_view_contract.cpp"
        executable = temp_dir / (
            "match_evidence_view_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_evidence_view_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_EVIDENCE_STANDALONE_TEST",
            "-DMP_MATCH_EVIDENCE_VIEW_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(EVIDENCE_SOURCE),
            str(SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(
            command, cwd=ROOT, text=True, capture_output=True
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone evidence-view contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"evidence-view native invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    native_contracts()
    print("mp_match_evidence_view_contract: PASS")


if __name__ == "__main__":
    main()
