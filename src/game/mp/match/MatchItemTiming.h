//----------------------------------------------------------------
// MatchItemTiming.h
//
// Bounded authoritative item-timing observations and disclosure projection.
// This core owns no entities, spawn dictionaries, GUI state, network transport,
// cvars, files, commands or wall/engine clock values.
//----------------------------------------------------------------

#ifndef __MP_MATCH_ITEM_TIMING_H__
#define __MP_MATCH_ITEM_TIMING_H__

#include "MatchDisclosurePolicy.h"

#include <stdint.h>

static const uint16_t MP_MATCH_ITEM_TIMING_SCHEMA_VERSION = 1;
static const int MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS =
	MP_MATCH_VIEW_MAX_ITEM_TIMINGS;
static const int MP_MATCH_ITEM_TIMING_TOKEN_BYTES =
	MP_MATCH_VIEW_ITEM_TOKEN_BYTES;

// Built-in kinds are semantic and independent of entityDef/class names.  A
// live adapter may instead supply one validated canonical adapter token when a
// map item has no built-in semantic kind.
typedef enum {
	MP_MATCH_ITEM_TIMING_KIND_INVALID = 0,
	MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE,
	MP_MATCH_ITEM_TIMING_KIND_HASTE,
	MP_MATCH_ITEM_TIMING_KIND_REGENERATION,
	MP_MATCH_ITEM_TIMING_KIND_INVISIBILITY,
	MP_MATCH_ITEM_TIMING_KIND_MEGA_HEALTH,
	MP_MATCH_ITEM_TIMING_KIND_LARGE_ARMOR,
	MP_MATCH_ITEM_TIMING_KIND_SMALL_ARMOR,
	MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN,
	MP_MATCH_ITEM_TIMING_KIND_COUNT
} mpMatchItemTimingKind_t;

// Returns the stable projection token for a built-in semantic kind.  Invalid
// and ADAPTER_TOKEN kinds return NULL.
const char *MPMatchItemTimingSemanticToken( mpMatchItemTimingKind_t kind );

// Adapter tokens are canonical lower-case machine tokens: they start with a
// letter, use only [a-z0-9_.-], have no leading/trailing/repeated separator,
// fit the MatchView token bound and do not alias a built-in semantic token.
bool MPMatchItemTimingIsAdapterToken( const char *token );

struct mpMatchItemTimingObservationInput {
	uint64_t				sourceId;	// stable and unique within mapInstanceId
	mpMatchItemTimingKind_t kind;
	const char *			adapterToken;	// only for KIND_ADAPTER_TOKEN
	mpMatchTime			observedAtMatchTime;
	mpMatchTime			matchDeadline;
	bool					available;
};

struct mpMatchItemTimingObservation {
	uint64_t				sourceId;
	mpMatchItemTimingKind_t kind;
	char					token[ MP_MATCH_ITEM_TIMING_TOKEN_BYTES + 1 ];
	mpMatchTime			observedAtMatchTime;
	mpMatchTime			matchDeadline;
	bool					available;
	uint64_t				firstRegistryRevision;
	uint64_t				lastRegistryRevision;
};

typedef enum {
	MP_MATCH_ITEM_TIMING_MUTATION_APPLIED = 0,
	MP_MATCH_ITEM_TIMING_MUTATION_NO_CHANGE,
	MP_MATCH_ITEM_TIMING_MUTATION_REJECTED,
	MP_MATCH_ITEM_TIMING_MUTATION_CODE_COUNT
} mpMatchItemTimingMutationCode_t;

typedef enum {
	MP_MATCH_ITEM_TIMING_REASON_NONE = 0,
	MP_MATCH_ITEM_TIMING_REASON_NOT_INITIALIZED,
	MP_MATCH_ITEM_TIMING_REASON_INVALID_MAP_INSTANCE,
	MP_MATCH_ITEM_TIMING_REASON_MAP_INSTANCE_CONFLICT,
	MP_MATCH_ITEM_TIMING_REASON_INVALID_SOURCE_ID,
	MP_MATCH_ITEM_TIMING_REASON_INVALID_KIND,
	MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN,
	MP_MATCH_ITEM_TIMING_REASON_STALE_REVISION,
	MP_MATCH_ITEM_TIMING_REASON_REVISION_EXHAUSTED,
	MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION,
	MP_MATCH_ITEM_TIMING_REASON_CLOCK_OVERFLOW,
	MP_MATCH_ITEM_TIMING_REASON_INVALID_DEADLINE,
	MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_SOURCE,
	MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_TOKEN,
	MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_OBSERVATION,
	MP_MATCH_ITEM_TIMING_REASON_CAPACITY,
	MP_MATCH_ITEM_TIMING_REASON_COUNT
} mpMatchItemTimingReason_t;

struct mpMatchItemTimingMutationResult {
	mpMatchItemTimingMutationCode_t code;
	mpMatchItemTimingReason_t reason;
	uint64_t previousRevision;
	uint64_t currentRevision;

	bool WasApplied( void ) const;
	bool WasRejected( void ) const;
};

class mpMatchItemTimingRegistry {
public:
						mpMatchItemTimingRegistry( void );

	void			Clear( void );

	// Map identity is installed once.  An exact replay is a no-op; replacing a
	// live registry requires an explicit Clear so stale map observations cannot
	// silently cross the boundary.
	mpMatchItemTimingMutationResult BeginMap( uint64_t mapInstanceId );
	bool			IsInitialized( void ) const;
	uint64_t		GetMapInstanceId( void ) const;
	uint64_t		GetRevision( void ) const;
	mpMatchTime		GetLastObservedMatchTime( void ) const;

	// expectedRevision is a registry compare-and-swap guard.  Exact replays are
	// recognized before the guard and remain idempotent even when the caller is
	// replaying the revision on which the observation originally committed.
	mpMatchItemTimingMutationResult Observe(
		const mpMatchItemTimingObservationInput &input,
		uint64_t expectedRevision );

	int				GetObservationCount( void ) const;
	const mpMatchItemTimingObservation *GetObservation( int index ) const;
	const mpMatchItemTimingObservation *FindObservation( uint64_t sourceId ) const;

	// This is the sole observer-candidate projection route.  The disclosure
	// service validates the committed policy, restricts audiences to broadcaster
	// or referee, applies match-clock delay holdback and constructs the candidate.
	mpMatchDisclosureItemResult_t ProjectCandidate(
		uint64_t sourceId,
		const mpMatchDisclosurePolicy_t &policy,
		mpMatchViewAudience_t audience,
		mpMatchTime currentMatchTime,
		mpMatchViewObserverCandidate_t &candidate,
		mpMatchTime *notBeforeMatchTime = 0 ) const;

	bool			ValidateInvariants( void ) const;

private:
	bool			CanCommit( void ) const;
	mpMatchItemTimingMutationResult CommitApplied( void );
	mpMatchItemTimingMutationResult NoChange(
		mpMatchItemTimingReason_t reason ) const;
	mpMatchItemTimingMutationResult Rejected(
		mpMatchItemTimingReason_t reason ) const;
	int				FindSourceIndex( uint64_t sourceId ) const;
	int				FindTokenIndex( const char *token ) const;

	bool			initialized;
	uint64_t		mapInstanceId;
	uint64_t		revision;
	mpMatchTime		lastObservedMatchTime;
	mpMatchItemTimingObservation observations[
		MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS ];
	int				observationCount;
};

#endif // __MP_MATCH_ITEM_TIMING_H__
