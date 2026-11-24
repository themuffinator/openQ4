//----------------------------------------------------------------
// MatchControlProjection.h
//
// Presentation-only projection for Match Control and managed-match context.
//
// This boundary consumes one already accepted, recipient-scoped view and the
// typed model built from that same view.  It writes complete GUI state batches,
// but deliberately does not call StateChanged and does not make authorization
// decisions.  Participant and map display text must be supplied explicitly by
// the caller so stable protocol identifiers never become display labels.
//----------------------------------------------------------------

#ifndef __MP_MATCH_CONTROL_PROJECTION_H__
#define __MP_MATCH_CONTROL_PROJECTION_H__

#include "MatchControlModel.h"

class idUserInterface;

static const int MP_MATCH_CONTROL_PROJECTION_NAME_BYTES = 96;
static const int MP_MATCH_CONTROL_PROJECTION_MAP_BYTES = 128;
static const int MP_MATCH_CONTROL_PROJECTION_ROW_BYTES = 512;

typedef bool ( *mpMatchControlResolveParticipantText_t )(
	void *callbackContext,
	mpMatchProtocolParticipantId_t participantId,
	char *destination,
	int destinationBytes );

typedef bool ( *mpMatchControlResolveMapText_t )(
	void *callbackContext,
	const char *mapToken,
	char *destination,
	int destinationBytes );

typedef struct mpMatchControlProjectionContext_s {
	void *callbackContext;
	mpMatchControlResolveParticipantText_t resolveParticipantText;
	mpMatchControlResolveMapText_t resolveMapText;

	// This is an explicit trusted-local presentation decision.  The projection
	// never derives operator visibility from recipient roles or availability.
	bool localOperatorVisible;

	// Used only to render authoritative deadline countdowns.  Zero falls back to
	// the accepted view's sampled engine time.
	unsigned long long displayEngineTimeMsec;

	// Normal refreshes preserve all caller-owned edit and choice states.  The
	// caller may request safe defaults once when constructing a fresh surface.
	bool initializeChoices;

	// The caller owns these values and chooses which accepted/latest feedback to
	// expose.  Cross-session results are rejected by the projection.
	const mpMatchOperationResult_t *authoritativeResult;
	const mpMatchControlError_t *localError;

	void Clear( void );
} mpMatchControlProjectionContext_t;

// Copies bounded, valid UTF-8 display text; control characters (including GUI
// list separators) become spaces, invalid sequences become '?', repeated
// spaces collapse, and incomplete trailing code points are never emitted.
// Returns the number of bytes written, excluding the terminator.
int MPMatchControlSanitizeDisplayText( const char *source,
	char *destination, int destinationBytes );

// Writes all Match Control scalar, list, selection and operation-availability
// states.  The caller batches this with its other menu state and calls
// StateChanged exactly once afterwards.
void MPMatchControlProjectMenu( idUserInterface &gui,
	const mpSessionView &acceptedView,
	const mpMatchControlModel &model,
	const mpMatchControlProjectionContext_t &context );

// Clears every projection-owned Match Control output.  Caller-owned edit,
// credential and choice values are retained unless initializeChoices is true.
void MPMatchControlClearMenu( idUserInterface &gui,
	bool initializeChoices = false );

// Writes the eight read-only managed-match context strings, then writes the
// single visibility gate last.  Use the same function for the in-play HUD and
// scoreboard so both surfaces receive an identical recipient-safe projection.
void MPMatchControlProjectManagedContext( idUserInterface &gui,
	const mpSessionView &acceptedView,
	const mpMatchControlModel &model,
	const mpMatchControlProjectionContext_t &context );

// Clears the eight context strings and writes visibility last.  Does not call
// StateChanged.
void MPMatchControlClearManagedContext( idUserInterface &gui );

#endif // __MP_MATCH_CONTROL_PROJECTION_H__
