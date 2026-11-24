//----------------------------------------------------------------
// MatchEvidenceView.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_EVIDENCE_VIEW_STANDALONE_TEST )
	#include "MatchEvidenceView.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchEvidenceView.h"
#endif

#include <string.h>

static_assert( MP_MATCH_EVIDENCE_MAX_EVENTS <=
	MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS,
	"MatchView must represent every retained evidence event" );
static_assert( MP_MATCH_EVIDENCE_MAX_PARTICIPANTS <=
	MP_MATCH_VIEW_MAX_PARTICIPANTS,
	"MatchView must represent every retained participant statistic" );
static_assert( MP_MATCH_EVIDENCE_MAX_TEAMS <= MP_MATCH_VIEW_SIDE_COUNT,
	"MatchView must represent every retained team statistic" );

namespace {

static mpMatchEvidenceViewResult_t Result( mpMatchEvidenceViewReason_t reason ) {
	mpMatchEvidenceViewResult_t result;
	result.reason = reason;
	return result;
}

static mpMatchViewEvidenceEventKind_t ProjectEventKind(
		mpEvidenceEventKind_t kind ) {
	switch ( kind ) {
		case MP_EVIDENCE_EVENT_PHASE_TRANSITION:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION;
		case MP_EVIDENCE_EVENT_ROUND_TRANSITION:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_ROUND_TRANSITION;
		case MP_EVIDENCE_EVENT_PAUSE_TRANSITION:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_PAUSE_TRANSITION;
		case MP_EVIDENCE_EVENT_ROLE_CHANGE:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_ROLE_CHANGE;
		case MP_EVIDENCE_EVENT_PROPOSAL:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_PROPOSAL;
		case MP_EVIDENCE_EVENT_ROSTER_CHANGE:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE;
		case MP_EVIDENCE_EVENT_MAP_RESULT:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT;
		case MP_EVIDENCE_EVENT_OUTPUT_FAILURE:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE;
		default:
			return MP_MATCH_VIEW_EVIDENCE_EVENT_NONE;
	}
}

static void AddDroppedRecords( uint64_t value, uint32_t &total,
		bool &saturated ) {
	if ( saturated ) {
		return;
	}
	if ( value > static_cast<uint64_t>( 0xffffffffu - total ) ) {
		total = 0xffffffffu;
		saturated = true;
		return;
	}
	total += static_cast<uint32_t>( value );
}

static bool ValidateProjection( const mpMatchViewEvidenceSummary_t &summary ) {
	if ( summary.evidenceState < MP_MATCH_VIEW_EVIDENCE_DISABLED ||
		summary.evidenceState >= MP_MATCH_VIEW_EVIDENCE_STATE_COUNT ||
		summary.mvdState < MP_MATCH_VIEW_MVD_DISABLED ||
		summary.mvdState >= MP_MATCH_VIEW_MVD_STATE_COUNT ||
		summary.reportState < MP_MATCH_VIEW_REPORT_DISABLED ||
		summary.reportState >= MP_MATCH_VIEW_REPORT_STATE_COUNT ||
		summary.eventCount > MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS ||
		summary.participantStatsCount > MP_MATCH_VIEW_MAX_PARTICIPANTS ||
		summary.teamStatsCount > MP_MATCH_VIEW_SIDE_COUNT ||
		summary.recentEventCount > MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ||
		summary.recentEventCount != ( summary.eventCount <
			MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ? summary.eventCount :
			MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ) ||
		( summary.droppedRecordCountSaturated &&
			summary.droppedRecordCount != 0xffffffffu ) ) {
		return false;
	}
	for ( int index = 0; index < summary.recentEventCount; ++index ) {
		if ( summary.recentEventKinds[ index ] <=
				MP_MATCH_VIEW_EVIDENCE_EVENT_NONE ||
			summary.recentEventKinds[ index ] >=
				MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT ) {
			return false;
		}
	}
	for ( int index = summary.recentEventCount;
			index < MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS; ++index ) {
		if ( summary.recentEventKinds[ index ] !=
				MP_MATCH_VIEW_EVIDENCE_EVENT_NONE ) {
			return false;
		}
	}
	if ( summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_DISABLED ) {
		return summary.mvdState == MP_MATCH_VIEW_MVD_DISABLED &&
			summary.reportState == MP_MATCH_VIEW_REPORT_DISABLED &&
			summary.evidenceRevision == 0 && summary.eventCount == 0 &&
			summary.droppedRecordCount == 0 &&
			!summary.droppedRecordCountSaturated &&
			summary.participantStatsCount == 0 &&
			summary.teamStatsCount == 0 && !summary.resultRecorded &&
			summary.recentEventCount == 0;
	}
	if ( summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_FAILED ) {
		return summary.evidenceRevision == 0 && summary.eventCount == 0 &&
			summary.droppedRecordCount == 0 &&
			!summary.droppedRecordCountSaturated &&
			summary.participantStatsCount == 0 &&
			summary.teamStatsCount == 0 && !summary.resultRecorded &&
			summary.recentEventCount == 0 &&
			( summary.mvdState == MP_MATCH_VIEW_MVD_DISABLED ||
				summary.mvdState == MP_MATCH_VIEW_MVD_FAILED ) &&
			( summary.reportState == MP_MATCH_VIEW_REPORT_DISABLED ||
				summary.reportState == MP_MATCH_VIEW_REPORT_FAILED );
	}
	return summary.evidenceRevision != 0 &&
		( !summary.resultRecorded || summary.eventCount != 0 ) &&
		( summary.reportState != MP_MATCH_VIEW_REPORT_AVAILABLE ||
			summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_FINALIZED ) &&
		( summary.reportState != MP_MATCH_VIEW_REPORT_PENDING ||
			summary.evidenceState != MP_MATCH_VIEW_EVIDENCE_FINALIZED ) &&
		( summary.mvdState != MP_MATCH_VIEW_MVD_AVAILABLE ||
			summary.evidenceState == MP_MATCH_VIEW_EVIDENCE_FINALIZED ) &&
		( summary.mvdState != MP_MATCH_VIEW_MVD_PENDING ||
			summary.evidenceState != MP_MATCH_VIEW_EVIDENCE_FINALIZED );
}

static mpMatchViewMVDState_t ProjectMVDState(
		const mpMatchEvidenceViewLifecycle_t &lifecycle, bool hasArtifact,
		bool startFailed, bool stopFailed ) {
	// A live recorder is the strongest current fact.  Historical failures may
	// have been recovered by a later successful start.
	if ( lifecycle.mvdRecording ) {
		return MP_MATCH_VIEW_MVD_RECORDING;
	}
	if ( lifecycle.finalized ) {
		if ( lifecycle.mvdRequired ) {
			return hasArtifact && !stopFailed ? MP_MATCH_VIEW_MVD_AVAILABLE :
				MP_MATCH_VIEW_MVD_FAILED;
		}
		if ( hasArtifact && !stopFailed ) {
			return MP_MATCH_VIEW_MVD_AVAILABLE;
		}
		return startFailed || stopFailed ? MP_MATCH_VIEW_MVD_FAILED :
			MP_MATCH_VIEW_MVD_DISABLED;
	}
	if ( lifecycle.mvdRequired ) {
		return stopFailed || ( startFailed && !hasArtifact ) || hasArtifact ?
			MP_MATCH_VIEW_MVD_FAILED : MP_MATCH_VIEW_MVD_PENDING;
	}
	return startFailed || stopFailed ? MP_MATCH_VIEW_MVD_FAILED :
		MP_MATCH_VIEW_MVD_DISABLED;
}

} // namespace

bool mpMatchEvidenceViewResult_t::Succeeded( void ) const {
	return reason == MP_MATCH_EVIDENCE_VIEW_REASON_NONE;
}

mpMatchEvidenceViewResult_t MPMatchEvidenceBuildView(
		const mpMatchEvidence &evidence,
		const mpMatchEvidenceViewLifecycle_t &lifecycle,
		mpMatchViewEvidenceSummary_t &summary ) {
	const bool evidenceInitialized = evidence.IsInitialized();
	if ( lifecycle.initialized != evidenceInitialized ||
		( lifecycle.finalized && !lifecycle.initialized ) ||
		( lifecycle.persisted && !lifecycle.finalized ) ||
		( lifecycle.mvdRecording && !lifecycle.initialized ) ) {
		return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_LIFECYCLE );
	}
	if ( !evidence.ValidateInvariants() ) {
		return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVIDENCE );
	}

	mpMatchViewEvidenceSummary_t candidate;
	memset( &candidate, 0, sizeof( candidate ) );
	candidate.evidenceState = MP_MATCH_VIEW_EVIDENCE_DISABLED;
	candidate.mvdState = MP_MATCH_VIEW_MVD_DISABLED;
	candidate.reportState = MP_MATCH_VIEW_REPORT_DISABLED;

	if ( !lifecycle.initialized ) {
		candidate.evidenceState = MP_MATCH_VIEW_EVIDENCE_FAILED;
		candidate.mvdState = lifecycle.mvdRequired ? MP_MATCH_VIEW_MVD_FAILED :
			MP_MATCH_VIEW_MVD_DISABLED;
		candidate.reportState = MP_MATCH_VIEW_REPORT_FAILED;
		if ( !ValidateProjection( candidate ) ) {
			return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_PROJECTION );
		}
		summary = candidate;
		return Result( MP_MATCH_EVIDENCE_VIEW_REASON_NONE );
	}

	candidate.evidenceState = lifecycle.finalized ?
		MP_MATCH_VIEW_EVIDENCE_FINALIZED : MP_MATCH_VIEW_EVIDENCE_CAPTURING;
	candidate.reportState = lifecycle.finalized ?
		( lifecycle.persisted ? MP_MATCH_VIEW_REPORT_AVAILABLE :
			MP_MATCH_VIEW_REPORT_FAILED ) : MP_MATCH_VIEW_REPORT_PENDING;
	candidate.evidenceRevision = evidence.GetEvidenceRevision();
	candidate.eventCount = static_cast<unsigned short>( evidence.GetEventCount() );
	candidate.participantStatsCount = static_cast<unsigned char>(
		evidence.GetParticipantStatsCount() );
	candidate.teamStatsCount = static_cast<unsigned char>(
		evidence.GetTeamStatsCount() );

	uint32_t droppedRecords = 0;
	bool droppedRecordsSaturated = evidence.IsDropCounterSaturated();
	if ( droppedRecordsSaturated ) {
		droppedRecords = 0xffffffffu;
	} else {
		AddDroppedRecords( evidence.GetDroppedEventCount(), droppedRecords,
			droppedRecordsSaturated );
		AddDroppedRecords( evidence.GetDroppedParticipantStatsCount(),
			droppedRecords, droppedRecordsSaturated );
		AddDroppedRecords( evidence.GetDroppedTeamStatsCount(), droppedRecords,
			droppedRecordsSaturated );
	}
	candidate.droppedRecordCount = droppedRecords;
	candidate.droppedRecordCountSaturated = droppedRecordsSaturated;

	bool mvdStartFailed = false;
	bool mvdStopFailed = false;
	const int firstRecentEvent = evidence.GetEventCount() >
		MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ? evidence.GetEventCount() -
		MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS : 0;
	for ( int index = 0; index < evidence.GetEventCount(); ++index ) {
		const mpEvidenceEvent *event = evidence.GetEvent( index );
		if ( event == NULL ) {
			return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVENT );
		}
		const mpMatchViewEvidenceEventKind_t projectedKind =
			ProjectEventKind( event->kind );
		if ( projectedKind == MP_MATCH_VIEW_EVIDENCE_EVENT_NONE ) {
			return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVENT );
		}
		if ( event->kind == MP_EVIDENCE_EVENT_MAP_RESULT ) {
			candidate.resultRecorded = true;
		} else if ( event->kind == MP_EVIDENCE_EVENT_OUTPUT_FAILURE ) {
			if ( event->data.outputFailure.output ==
					MP_EVIDENCE_OUTPUT_MVD_START ) {
				mvdStartFailed = true;
			} else if ( event->data.outputFailure.output ==
					MP_EVIDENCE_OUTPUT_MVD_STOP ) {
				mvdStopFailed = true;
			}
		}
		if ( index >= firstRecentEvent ) {
			candidate.recentEventKinds[ candidate.recentEventCount++ ] =
				projectedKind;
		}
	}

	bool hasMVDArtifact = false;
	for ( int index = 0; index < evidence.GetArtifactCount(); ++index ) {
		const mpEvidenceArtifactLink *artifact = evidence.GetArtifact( index );
		if ( artifact == NULL ) {
			return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_ARTIFACT );
		}
		if ( artifact->kind == MP_EVIDENCE_ARTIFACT_MVD ) {
			hasMVDArtifact = true;
		} else {
			return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_ARTIFACT );
		}
	}
	candidate.mvdState = ProjectMVDState( lifecycle, hasMVDArtifact,
		mvdStartFailed, mvdStopFailed );

	if ( !ValidateProjection( candidate ) ) {
		return Result( MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_PROJECTION );
	}
	summary = candidate;
	return Result( MP_MATCH_EVIDENCE_VIEW_REASON_NONE );
}
