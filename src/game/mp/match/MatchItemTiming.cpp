//----------------------------------------------------------------
// MatchItemTiming.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_ITEM_TIMING_STANDALONE_TEST )
	#include "MatchItemTiming.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchItemTiming.h"
#endif

#include <limits.h>
#include <string.h>

static_assert( MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS ==
	MP_MATCH_VIEW_MAX_ITEM_TIMINGS,
	"item timing registry and recipient view capacities diverged" );
static_assert( MP_MATCH_ITEM_TIMING_TOKEN_BYTES == MP_MATCH_VIEW_ITEM_TOKEN_BYTES,
	"item timing registry and recipient view token bounds diverged" );

namespace {

static const uint64_t MP_MATCH_ITEM_TIMING_UINT64_MAX =
	static_cast<uint64_t>( ~( static_cast<uint64_t>( 0 ) ) );

static bool IsAdapterSeparator( unsigned char value ) {
	return value == '_' || value == '-' || value == '.';
}

static bool IsCanonicalAdapterToken( const char *token ) {
	if ( token == NULL || token[ 0 ] < 'a' || token[ 0 ] > 'z' ) {
		return false;
	}
	int length = 0;
	bool previousSeparator = false;
	while ( length <= MP_MATCH_ITEM_TIMING_TOKEN_BYTES && token[ length ] != '\0' ) {
		const unsigned char value = static_cast<unsigned char>( token[ length ] );
		const bool alphaNumeric = ( value >= 'a' && value <= 'z' ) ||
			( value >= '0' && value <= '9' );
		const bool separator = IsAdapterSeparator( value );
		if ( !alphaNumeric && !separator ) {
			return false;
		}
		if ( separator && ( length == 0 || previousSeparator ) ) {
			return false;
		}
		previousSeparator = separator;
		++length;
	}
	return length >= 1 && length <= MP_MATCH_ITEM_TIMING_TOKEN_BYTES &&
		token[ length ] == '\0' && !previousSeparator;
}

static bool IsSemanticKind( mpMatchItemTimingKind_t kind ) {
	return kind > MP_MATCH_ITEM_TIMING_KIND_INVALID &&
		kind < MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN;
}

static bool IsSemanticToken( const char *token ) {
	if ( token == NULL ) {
		return false;
	}
	for ( int kind = MP_MATCH_ITEM_TIMING_KIND_INVALID + 1;
		kind < MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN; ++kind ) {
		const char *semantic = MPMatchItemTimingSemanticToken(
			static_cast<mpMatchItemTimingKind_t>( kind ) );
		if ( semantic != NULL && strcmp( token, semantic ) == 0 ) {
			return true;
		}
	}
	return false;
}

static bool CopyToken( const char *source,
		char destination[ MP_MATCH_ITEM_TIMING_TOKEN_BYTES + 1 ] ) {
	if ( source == NULL ) {
		return false;
	}
	int length = 0;
	while ( length <= MP_MATCH_ITEM_TIMING_TOKEN_BYTES && source[ length ] != '\0' ) {
		++length;
	}
	if ( length < 1 || length > MP_MATCH_ITEM_TIMING_TOKEN_BYTES ) {
		return false;
	}
	memcpy( destination, source, static_cast<size_t>( length ) );
	destination[ length ] = '\0';
	return true;
}

static void ClearObservation( mpMatchItemTimingObservation &observation ) {
	observation.sourceId = 0;
	observation.kind = MP_MATCH_ITEM_TIMING_KIND_INVALID;
	memset( observation.token, 0, sizeof( observation.token ) );
	observation.observedAtMatchTime = mpMatchTime::FromMilliseconds( 0 );
	observation.matchDeadline = mpMatchTime::FromMilliseconds( 0 );
	observation.available = false;
	observation.firstRegistryRevision = 0;
	observation.lastRegistryRevision = 0;
}

static bool BuildObservation( const mpMatchItemTimingObservationInput &input,
		mpMatchItemTimingObservation &observation,
		mpMatchItemTimingReason_t &reason ) {
	ClearObservation( observation );
	if ( input.sourceId == 0 ) {
		reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_SOURCE_ID;
		return false;
	}
	const char *token = NULL;
	if ( IsSemanticKind( input.kind ) ) {
		if ( input.adapterToken != NULL && input.adapterToken[ 0 ] != '\0' ) {
			reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN;
			return false;
		}
		token = MPMatchItemTimingSemanticToken( input.kind );
	} else if ( input.kind == MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN ) {
		if ( !MPMatchItemTimingIsAdapterToken( input.adapterToken ) ) {
			reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN;
			return false;
		}
		token = input.adapterToken;
	} else {
		reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_KIND;
		return false;
	}
	if ( token == NULL || !CopyToken( token, observation.token ) ) {
		reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN;
		return false;
	}
	if ( !input.observedAtMatchTime.IsValid() || !input.matchDeadline.IsValid() ) {
		reason = MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION;
		return false;
	}
	const int64_t observedMsec = input.observedAtMatchTime.Milliseconds();
	const int64_t deadlineMsec = input.matchDeadline.Milliseconds();
	if ( observedMsec >
		INT64_MAX - MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC ) {
		reason = MP_MATCH_ITEM_TIMING_REASON_CLOCK_OVERFLOW;
		return false;
	}
	if ( ( input.available && deadlineMsec > observedMsec ) ||
		( !input.available && deadlineMsec <= observedMsec ) ) {
		reason = MP_MATCH_ITEM_TIMING_REASON_INVALID_DEADLINE;
		return false;
	}

	observation.sourceId = input.sourceId;
	observation.kind = input.kind;
	observation.observedAtMatchTime = input.observedAtMatchTime;
	observation.matchDeadline = input.matchDeadline;
	observation.available = input.available;
	reason = MP_MATCH_ITEM_TIMING_REASON_NONE;
	return true;
}

static bool ObservationStateEqual( const mpMatchItemTimingObservation &lhs,
		const mpMatchItemTimingObservation &rhs ) {
	return lhs.sourceId == rhs.sourceId && lhs.kind == rhs.kind &&
		strcmp( lhs.token, rhs.token ) == 0 &&
		lhs.observedAtMatchTime == rhs.observedAtMatchTime &&
		lhs.matchDeadline == rhs.matchDeadline && lhs.available == rhs.available;
}

static bool ObservationIdentityEqual( const mpMatchItemTimingObservation &lhs,
		const mpMatchItemTimingObservation &rhs ) {
	return lhs.sourceId == rhs.sourceId && lhs.kind == rhs.kind &&
		strcmp( lhs.token, rhs.token ) == 0;
}

} // namespace

const char *MPMatchItemTimingSemanticToken( mpMatchItemTimingKind_t kind ) {
	switch ( kind ) {
		case MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE: return "quad";
		case MP_MATCH_ITEM_TIMING_KIND_HASTE: return "haste";
		case MP_MATCH_ITEM_TIMING_KIND_REGENERATION: return "regeneration";
		case MP_MATCH_ITEM_TIMING_KIND_INVISIBILITY: return "invisibility";
		case MP_MATCH_ITEM_TIMING_KIND_MEGA_HEALTH: return "mega_health";
		case MP_MATCH_ITEM_TIMING_KIND_LARGE_ARMOR: return "large_armor";
		case MP_MATCH_ITEM_TIMING_KIND_SMALL_ARMOR: return "small_armor";
		default: return NULL;
	}
}

bool MPMatchItemTimingIsAdapterToken( const char *token ) {
	return IsCanonicalAdapterToken( token ) && !IsSemanticToken( token );
}

bool mpMatchItemTimingMutationResult::WasApplied( void ) const {
	return code == MP_MATCH_ITEM_TIMING_MUTATION_APPLIED;
}

bool mpMatchItemTimingMutationResult::WasRejected( void ) const {
	return code == MP_MATCH_ITEM_TIMING_MUTATION_REJECTED;
}

mpMatchItemTimingRegistry::mpMatchItemTimingRegistry( void ) {
	Clear();
}

void mpMatchItemTimingRegistry::Clear( void ) {
	initialized = false;
	mapInstanceId = 0;
	revision = 0;
	lastObservedMatchTime = mpMatchTime::FromMilliseconds( 0 );
	for ( int index = 0; index < MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS; ++index ) {
		ClearObservation( observations[ index ] );
	}
	observationCount = 0;
}

mpMatchItemTimingMutationResult mpMatchItemTimingRegistry::BeginMap(
		uint64_t newMapInstanceId ) {
	if ( newMapInstanceId == 0 ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_INVALID_MAP_INSTANCE );
	}
	if ( initialized ) {
		return mapInstanceId == newMapInstanceId ?
			NoChange( MP_MATCH_ITEM_TIMING_REASON_NONE ) :
			Rejected( MP_MATCH_ITEM_TIMING_REASON_MAP_INSTANCE_CONFLICT );
	}
	initialized = true;
	mapInstanceId = newMapInstanceId;
	revision = 1;
	return mpMatchItemTimingMutationResult {
		MP_MATCH_ITEM_TIMING_MUTATION_APPLIED,
		MP_MATCH_ITEM_TIMING_REASON_NONE, 0, revision
	};
}

bool mpMatchItemTimingRegistry::IsInitialized( void ) const {
	return initialized;
}

uint64_t mpMatchItemTimingRegistry::GetMapInstanceId( void ) const {
	return mapInstanceId;
}

uint64_t mpMatchItemTimingRegistry::GetRevision( void ) const {
	return revision;
}

mpMatchTime mpMatchItemTimingRegistry::GetLastObservedMatchTime( void ) const {
	return lastObservedMatchTime;
}

bool mpMatchItemTimingRegistry::CanCommit( void ) const {
	return initialized && revision != MP_MATCH_ITEM_TIMING_UINT64_MAX;
}

mpMatchItemTimingMutationResult mpMatchItemTimingRegistry::CommitApplied( void ) {
	const uint64_t previousRevision = revision;
	++revision;
	return mpMatchItemTimingMutationResult {
		MP_MATCH_ITEM_TIMING_MUTATION_APPLIED,
		MP_MATCH_ITEM_TIMING_REASON_NONE, previousRevision, revision
	};
}

mpMatchItemTimingMutationResult mpMatchItemTimingRegistry::NoChange(
		mpMatchItemTimingReason_t reason ) const {
	return mpMatchItemTimingMutationResult {
		MP_MATCH_ITEM_TIMING_MUTATION_NO_CHANGE, reason, revision, revision
	};
}

mpMatchItemTimingMutationResult mpMatchItemTimingRegistry::Rejected(
		mpMatchItemTimingReason_t reason ) const {
	return mpMatchItemTimingMutationResult {
		MP_MATCH_ITEM_TIMING_MUTATION_REJECTED, reason, revision, revision
	};
}

int mpMatchItemTimingRegistry::FindSourceIndex( uint64_t sourceId ) const {
	for ( int index = 0; index < observationCount; ++index ) {
		if ( observations[ index ].sourceId == sourceId ) {
			return index;
		}
	}
	return -1;
}

int mpMatchItemTimingRegistry::FindTokenIndex( const char *token ) const {
	if ( token == NULL ) {
		return -1;
	}
	for ( int index = 0; index < observationCount; ++index ) {
		if ( strcmp( observations[ index ].token, token ) == 0 ) {
			return index;
		}
	}
	return -1;
}

mpMatchItemTimingMutationResult mpMatchItemTimingRegistry::Observe(
		const mpMatchItemTimingObservationInput &input,
		uint64_t expectedRevision ) {
	if ( !initialized ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_NOT_INITIALIZED );
	}
	mpMatchItemTimingObservation candidate;
	mpMatchItemTimingReason_t reason;
	if ( !BuildObservation( input, candidate, reason ) ) {
		return Rejected( reason );
	}
	const int sourceIndex = FindSourceIndex( candidate.sourceId );
	if ( sourceIndex >= 0 &&
		ObservationStateEqual( observations[ sourceIndex ], candidate ) ) {
		return NoChange( MP_MATCH_ITEM_TIMING_REASON_NONE );
	}
	if ( expectedRevision != revision ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_REVISION_EXHAUSTED );
	}
	if ( sourceIndex >= 0 ) {
		mpMatchItemTimingObservation &stored = observations[ sourceIndex ];
		if ( !ObservationIdentityEqual( stored, candidate ) ) {
			return Rejected( MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_SOURCE );
		}
		if ( candidate.observedAtMatchTime < stored.observedAtMatchTime ||
			candidate.observedAtMatchTime < lastObservedMatchTime ) {
			return Rejected( MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION );
		}
		if ( candidate.observedAtMatchTime == stored.observedAtMatchTime ) {
			return Rejected( MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_OBSERVATION );
		}
		const uint64_t nextRevision = revision + 1;
		candidate.firstRegistryRevision = stored.firstRegistryRevision;
		candidate.lastRegistryRevision = nextRevision;
		stored = candidate;
		lastObservedMatchTime = candidate.observedAtMatchTime;
		return CommitApplied();
	}

	if ( FindTokenIndex( candidate.token ) >= 0 ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_TOKEN );
	}
	if ( candidate.observedAtMatchTime < lastObservedMatchTime ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION );
	}
	if ( observationCount >= MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS ) {
		return Rejected( MP_MATCH_ITEM_TIMING_REASON_CAPACITY );
	}
	const uint64_t nextRevision = revision + 1;
	candidate.firstRegistryRevision = nextRevision;
	candidate.lastRegistryRevision = nextRevision;
	observations[ observationCount++ ] = candidate;
	lastObservedMatchTime = candidate.observedAtMatchTime;
	return CommitApplied();
}

int mpMatchItemTimingRegistry::GetObservationCount( void ) const {
	return observationCount;
}

const mpMatchItemTimingObservation *mpMatchItemTimingRegistry::GetObservation(
		int index ) const {
	return index >= 0 && index < observationCount ? &observations[ index ] : NULL;
}

const mpMatchItemTimingObservation *mpMatchItemTimingRegistry::FindObservation(
		uint64_t sourceId ) const {
	const int index = FindSourceIndex( sourceId );
	return index >= 0 ? &observations[ index ] : NULL;
}

mpMatchDisclosureItemResult_t mpMatchItemTimingRegistry::ProjectCandidate(
		uint64_t sourceId,
		const mpMatchDisclosurePolicy_t &policy,
		mpMatchViewAudience_t audience,
		mpMatchTime currentMatchTime,
		mpMatchViewObserverCandidate_t &candidate,
		mpMatchTime *notBeforeMatchTime ) const {
	const mpMatchItemTimingObservation *observation = FindObservation( sourceId );
	if ( !initialized || observation == NULL ) {
		candidate.Clear();
		if ( notBeforeMatchTime != NULL ) {
			*notBeforeMatchTime = mpMatchTime::FromMilliseconds( 0 );
		}
		return MP_MATCH_DISCLOSURE_ITEM_REJECTED;
	}
	return MPMatchDisclosureSetItemTimingCandidate( policy, audience,
		currentMatchTime, observation->observedAtMatchTime,
		observation->token, observation->matchDeadline,
		observation->available, candidate, notBeforeMatchTime );
}

bool mpMatchItemTimingRegistry::ValidateInvariants( void ) const {
	if ( !initialized ) {
		return mapInstanceId == 0 && revision == 0 && observationCount == 0 &&
			lastObservedMatchTime.IsValid() &&
			lastObservedMatchTime.Milliseconds() == 0;
	}
	if ( mapInstanceId == 0 || revision == 0 || observationCount < 0 ||
		observationCount > MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS ||
		!lastObservedMatchTime.IsValid() ||
		revision < static_cast<uint64_t>( observationCount ) + 1 ) {
		return false;
	}
	mpMatchTime maximumObserved = mpMatchTime::FromMilliseconds( 0 );
	for ( int index = 0; index < observationCount; ++index ) {
		const mpMatchItemTimingObservation &stored = observations[ index ];
		mpMatchItemTimingObservationInput input;
		input.sourceId = stored.sourceId;
		input.kind = stored.kind;
		input.adapterToken = stored.kind == MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN ?
			stored.token : NULL;
		input.observedAtMatchTime = stored.observedAtMatchTime;
		input.matchDeadline = stored.matchDeadline;
		input.available = stored.available;
		mpMatchItemTimingObservation rebuilt;
		mpMatchItemTimingReason_t reason;
		if ( !BuildObservation( input, rebuilt, reason ) ||
			!ObservationStateEqual( stored, rebuilt ) ||
			stored.firstRegistryRevision < 2 ||
			stored.firstRegistryRevision > stored.lastRegistryRevision ||
			stored.lastRegistryRevision > revision ||
			stored.observedAtMatchTime > lastObservedMatchTime ) {
			return false;
		}
		if ( stored.observedAtMatchTime > maximumObserved ) {
			maximumObserved = stored.observedAtMatchTime;
		}
		for ( int other = index + 1; other < observationCount; ++other ) {
			if ( stored.sourceId == observations[ other ].sourceId ||
				strcmp( stored.token, observations[ other ].token ) == 0 ) {
				return false;
			}
		}
	}
	return ( observationCount == 0 &&
		lastObservedMatchTime.Milliseconds() == 0 ) ||
		( observationCount > 0 && maximumObserved == lastObservedMatchTime );
}
