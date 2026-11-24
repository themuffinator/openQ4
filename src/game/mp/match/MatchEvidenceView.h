//----------------------------------------------------------------
// MatchEvidenceView.h
//
// Allocation-free projection of authoritative match evidence into the
// recipient-safe MatchView summary.  This layer owns no game, filesystem,
// recorder, network, GUI or persistence state.
//----------------------------------------------------------------

#ifndef __MP_MATCH_EVIDENCE_VIEW_H__
#define __MP_MATCH_EVIDENCE_VIEW_H__

#include "MatchEvidence.h"
#include "MatchView.h"

// These facts belong to the live adapter rather than the evidence value core.
// They are explicit so the projection cannot infer recorder or persistence
// success from filenames, backend messages, or other server-local details.
typedef struct mpMatchEvidenceViewLifecycle_s {
	bool	initialized;
	bool	finalized;
	bool	persisted;
	bool	mvdRequired;
	bool	mvdRecording;
} mpMatchEvidenceViewLifecycle_t;

typedef enum {
	MP_MATCH_EVIDENCE_VIEW_REASON_NONE = 0,
	MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_LIFECYCLE,
	MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVIDENCE,
	MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_EVENT,
	MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_ARTIFACT,
	MP_MATCH_EVIDENCE_VIEW_REASON_INVALID_PROJECTION,
	MP_MATCH_EVIDENCE_VIEW_REASON_COUNT
} mpMatchEvidenceViewReason_t;

typedef struct mpMatchEvidenceViewResult_s {
	mpMatchEvidenceViewReason_t reason;

	bool Succeeded( void ) const;
} mpMatchEvidenceViewResult_t;

// Builds transactionally: every failure leaves summary unchanged.  Successful
// output contains only bounded status, counters and event kinds; server-local
// artifact identifiers and diagnostic details never cross this boundary.
mpMatchEvidenceViewResult_t MPMatchEvidenceBuildView(
	const mpMatchEvidence &evidence,
	const mpMatchEvidenceViewLifecycle_t &lifecycle,
	mpMatchViewEvidenceSummary_t &summary );

#endif // __MP_MATCH_EVIDENCE_VIEW_H__
