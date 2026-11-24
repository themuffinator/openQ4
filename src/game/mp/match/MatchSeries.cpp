//----------------------------------------------------------------
// MatchSeries.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SERIES_STANDALONE_TEST )
	#include "MatchSeries.h"
	#include <limits.h>
	#include <string.h>
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSeries.h"
	#include <limits.h>
#endif

#include "MatchSeriesRecovery.h"

namespace {

static int SeriesAsciiLower( int value ) {
	if ( value >= 'A' && value <= 'Z' ) {
		return value + ( 'a' - 'A' );
	}
	return value;
}

static bool SeriesTokenEquals( const char *lhs, const char *rhs ) {
	if ( lhs == NULL || rhs == NULL ) {
		return false;
	}

	while ( *lhs != '\0' && *rhs != '\0' ) {
		if ( SeriesAsciiLower( static_cast<unsigned char>( *lhs ) ) !=
			SeriesAsciiLower( static_cast<unsigned char>( *rhs ) ) ) {
			return false;
		}
		++lhs;
		++rhs;
	}
	return *lhs == *rhs;
}

static void ClearSelectedMap( mpSeriesSelectedMap &selection ) {
	selection.poolIndex = -1;
	selection.selectedBySide = MP_SERIES_SIDE_NONE;
	selection.decider = false;
	selection.hasStartingGameSide = false;
	selection.startingGameSide = MP_SERIES_SIDE_NONE;
	selection.gameSideChosenBy = MP_SERIES_SIDE_NONE;
}

static void ClearAttempt( mpSeriesMapAttempt &attempt ) {
	attempt.selectionIndex = -1;
	attempt.outcome = MP_SERIES_MAP_UNPLAYED;
	attempt.winnerSide = MP_SERIES_SIDE_NONE;
	attempt.score[ 0 ] = 0;
	attempt.score[ 1 ] = 0;
	attempt.matchSessionId = 0;
	attempt.rulesDigest = 0;
}

static void ClearAppliedVeto( mpSeriesAppliedVeto &veto ) {
	veto.action = MP_SERIES_VETO_BAN;
	veto.actingSide = MP_SERIES_SIDE_NONE;
	veto.poolIndex = -1;
	veto.selectedGameSide = MP_SERIES_SIDE_NONE;
}

static bool IsSeriesSide( int side ) {
	return side >= 0 && side < MP_SERIES_SIDE_COUNT;
}

static const int MP_SERIES_INT_MAX = 2147483647;

static const mpSeriesProfileDescriptor seriesProfileDescriptors[] = {
	{ MP_SERIES_PROFILE_BEST_OF_ONE, "best_of_one", "#str_41693", "#str_41694",
		1, 1, MP_SERIES_MAX_MAP_POOL, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE },
	{ MP_SERIES_PROFILE_BEST_OF_THREE, "best_of_three", "#str_41695", "#str_41696",
		3, 3, MP_SERIES_MAX_MAP_POOL, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE },
	{ MP_SERIES_PROFILE_BEST_OF_FIVE, "best_of_five", "#str_41697", "#str_41698",
		5, 5, MP_SERIES_MAX_MAP_POOL, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE }
};

static const int MP_SERIES_PROFILE_DESCRIPTOR_COUNT =
	static_cast<int>( sizeof( seriesProfileDescriptors ) /
		sizeof( seriesProfileDescriptors[ 0 ] ) );

static_assert( MP_SERIES_PROFILE_DESCRIPTOR_COUNT == MP_SERIES_PROFILE_COUNT,
	"Every built-in series profile requires one descriptor" );
static_assert( MP_SERIES_MAX_MAP_POOL + MP_SERIES_MAX_BEST_OF <=
	MP_SERIES_MAX_VETO_STEPS,
	"Complete veto profiles can exceed the bounded veto-step capacity" );

static void CopySeriesToken( char destination[ MP_SERIES_MAP_TOKEN_BYTES ],
	const char *source ) {
	int index = 0;
	while ( source != NULL && index + 1 < MP_SERIES_MAP_TOKEN_BYTES &&
		source[ index ] != '\0' ) {
		destination[ index ] = source[ index ];
		++index;
	}
	destination[ index ] = '\0';
}

static void AddVetoStep( mpSeriesConfiguration &configuration,
	mpSeriesVetoAction_t action, int expectedSide ) {
	mpSeriesVetoStep &step = configuration.vetoSteps[ configuration.vetoStepCount++ ];
	step.action = action;
	step.expectedSide = expectedSide;
}

} // namespace

int MPSeriesProfileDescriptorCount( void ) {
	return MP_SERIES_PROFILE_DESCRIPTOR_COUNT;
}

const mpSeriesProfileDescriptor *MPSeriesProfileDescriptorForId(
	mpSeriesProfileId_t profile ) {
	if ( profile < 0 || profile >= MP_SERIES_PROFILE_COUNT ) {
		return NULL;
	}
	const mpSeriesProfileDescriptor &descriptor = seriesProfileDescriptors[ profile ];
	return descriptor.id == profile ? &descriptor : NULL;
}

const mpSeriesProfileDescriptor *MPSeriesProfileByKey( const char *key ) {
	if ( key == NULL || key[ 0 ] == '\0' ) {
		return NULL;
	}
	for ( int index = 0; index < MP_SERIES_PROFILE_DESCRIPTOR_COUNT; ++index ) {
		if ( SeriesTokenEquals( seriesProfileDescriptors[ index ].key, key ) ) {
			return &seriesProfileDescriptors[ index ];
		}
	}
	return NULL;
}

bool MPSeriesBuildProfileDraft( mpSeriesProfileId_t profile, int gameType,
	uint64_t deterministicSeed, int initialSide, bool requireStartingGameSide,
	const char * const *mapTokens, int mapTokenCount,
	mpSeriesConfiguration &draft, mpSeriesReason_t &reason ) {
	reason = MP_SERIES_REASON_NONE;
	const mpSeriesProfileDescriptor *descriptor =
		MPSeriesProfileDescriptorForId( profile );
	if ( descriptor == NULL ) {
		reason = MP_SERIES_REASON_UNKNOWN_PROFILE;
		return false;
	}
	if ( gameType < 0 ) {
		reason = MP_SERIES_REASON_INVALID_GAME_TYPE;
		return false;
	}
	if ( initialSide < 0 || initialSide >= MP_SERIES_SIDE_COUNT ) {
		reason = MP_SERIES_REASON_WRONG_VETO_SIDE;
		return false;
	}
	if ( mapTokens == NULL || mapTokenCount < descriptor->minimumMapPool ||
		mapTokenCount > descriptor->maximumMapPool ||
		mapTokenCount > MP_SERIES_MAX_MAP_POOL ) {
		reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
		return false;
	}

	mpSeriesConfiguration candidate;
	memset( &candidate, 0, sizeof( candidate ) );
	candidate.gameType = gameType;
	candidate.sourceProfile = profile;
	candidate.bestOf = descriptor->bestOf;
	candidate.deterministicSeed = deterministicSeed;
	candidate.initialSide = initialSide;
	candidate.requireStartingGameSide = requireStartingGameSide;
	candidate.mapPoolCount = mapTokenCount;
	for ( int index = 0; index < mapTokenCount; ++index ) {
		if ( !mpCompetitionSeries::IsSafeMapToken( mapTokens[ index ] ) ) {
			reason = MP_SERIES_REASON_INVALID_MAP_TOKEN;
			return false;
		}
		CopySeriesToken( candidate.mapPool[ index ], mapTokens[ index ] );
	}

	if ( descriptor->vetoPolicy != MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE ) {
		reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
		return false;
	}

	int mapAuthority = initialSide;
	const int banCount = mapTokenCount - descriptor->bestOf;
	for ( int index = 0; index < banCount; ++index ) {
		AddVetoStep( candidate, MP_SERIES_VETO_BAN, mapAuthority );
		mapAuthority = 1 - mapAuthority;
	}
	for ( int index = 0; index + 1 < descriptor->bestOf; ++index ) {
		const int selectingSide = mapAuthority;
		AddVetoStep( candidate, MP_SERIES_VETO_PICK, selectingSide );
		if ( requireStartingGameSide ) {
			AddVetoStep( candidate, MP_SERIES_VETO_SIDE, 1 - selectingSide );
		}
		mapAuthority = 1 - mapAuthority;
	}
	const int deciderAuthority = mapAuthority;
	AddVetoStep( candidate, MP_SERIES_VETO_DECIDER, deciderAuthority );
	if ( requireStartingGameSide ) {
		AddVetoStep( candidate, MP_SERIES_VETO_SIDE, 1 - deciderAuthority );
	}

	if ( !mpCompetitionSeries::ValidateConfiguration( candidate, reason ) ) {
		return false;
	}
	draft = candidate;
	return true;
}

bool mpSeriesMutationResult::WasApplied( void ) const {
	return code == MP_SERIES_MUTATION_APPLIED;
}

bool mpSeriesMutationResult::WasRejected( void ) const {
	return code == MP_SERIES_MUTATION_REJECTED;
}

mpCompetitionSeries::mpCompetitionSeries( void ) {
	Reset();
}

void mpCompetitionSeries::Reset( void ) {
	state = MP_SERIES_DISABLED;
	revision = 0;
	memset( &configuration, 0, sizeof( configuration ) );
	configuration.sourceProfile = MP_SERIES_PROFILE_CUSTOM;
	configuration.gameType = -1;
	configuration.initialSide = MP_SERIES_SIDE_NONE;
	currentVetoStep = 0;
	appliedVetoCount = 0;
	selectedMapCount = 0;
	nextSelectionIndex = 0;
	currentSelectionIndex = -1;
	attemptCount = 0;
	wins[ 0 ] = 0;
	wins[ 1 ] = 0;
	mapLoadFailureCount = 0;

	for ( int i = 0; i < MP_SERIES_MAX_MAP_POOL; ++i ) {
		mapDisposition[ i ] = MP_SERIES_MAP_AVAILABLE;
	}
	for ( int i = 0; i < MP_SERIES_MAX_VETO_STEPS; ++i ) {
		ClearAppliedVeto( appliedVetoes[ i ] );
	}
	for ( int i = 0; i < MP_SERIES_MAX_BEST_OF; ++i ) {
		ClearSelectedMap( selectedMaps[ i ] );
	}
	for ( int i = 0; i < MP_SERIES_MAX_MAP_ATTEMPTS; ++i ) {
		ClearAttempt( attempts[ i ] );
	}
}

bool mpCompetitionSeries::IsExpectedRevision( uint64_t expectedRevision ) const {
	return expectedRevision == revision;
}

bool mpCompetitionSeries::CanCommit( void ) const {
	return revision != UINT64_MAX;
}

mpSeriesMutationResult mpCompetitionSeries::Applied( uint64_t previousRevision ) {
	mpSeriesMutationResult result;
	result.code = MP_SERIES_MUTATION_APPLIED;
	result.reason = MP_SERIES_REASON_NONE;
	result.previousRevision = previousRevision;
	result.currentRevision = revision;
	return result;
}

mpSeriesMutationResult mpCompetitionSeries::NoChange( mpSeriesReason_t reason ) const {
	mpSeriesMutationResult result;
	result.code = MP_SERIES_MUTATION_NO_CHANGE;
	result.reason = reason;
	result.previousRevision = revision;
	result.currentRevision = revision;
	return result;
}

mpSeriesMutationResult mpCompetitionSeries::Rejected( mpSeriesReason_t reason ) const {
	mpSeriesMutationResult result;
	result.code = MP_SERIES_MUTATION_REJECTED;
	result.reason = reason;
	result.previousRevision = revision;
	result.currentRevision = revision;
	return result;
}

bool mpCompetitionSeries::IsSafeMapToken( const char *mapToken ) {
	if ( mapToken == NULL || mapToken[ 0 ] == '\0' ) {
		return false;
	}

	int length = 0;
	char previous = '\0';
	for ( const char *cursor = mapToken; *cursor != '\0'; ++cursor ) {
		const unsigned char value = static_cast<unsigned char>( *cursor );
		if ( ++length >= MP_SERIES_MAP_TOKEN_BYTES ) {
			return false;
		}
		if ( !( ( value >= 'a' && value <= 'z' ) ||
			( value >= 'A' && value <= 'Z' ) ||
			( value >= '0' && value <= '9' ) || value == '_' || value == '-' || value == '/' ) ) {
			return false;
		}
		if ( value == '/' && ( previous == '\0' || previous == '/' ) ) {
			return false;
		}
		previous = static_cast<char>( value );
	}

	return previous != '/';
}

bool mpCompetitionSeries::ValidateConfiguration( const mpSeriesConfiguration &candidate,
	mpSeriesReason_t &reason ) {
	reason = MP_SERIES_REASON_NONE;
	if ( candidate.sourceProfile != MP_SERIES_PROFILE_CUSTOM ) {
		const mpSeriesProfileDescriptor *profile =
			MPSeriesProfileDescriptorForId( candidate.sourceProfile );
		if ( profile == NULL ) {
			reason = MP_SERIES_REASON_UNKNOWN_PROFILE;
			return false;
		}
		if ( candidate.bestOf != profile->bestOf ) {
			reason = MP_SERIES_REASON_PROFILE_BEST_OF_MISMATCH;
			return false;
		}
		if ( candidate.mapPoolCount < profile->minimumMapPool ||
			candidate.mapPoolCount > profile->maximumMapPool ||
			profile->vetoPolicy != MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE ) {
			reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
			return false;
		}
	}
	if ( candidate.gameType < 0 ) {
		reason = MP_SERIES_REASON_INVALID_GAME_TYPE;
		return false;
	}
	if ( candidate.bestOf < 1 || candidate.bestOf > MP_SERIES_MAX_BEST_OF ||
		( candidate.bestOf & 1 ) == 0 ) {
		reason = MP_SERIES_REASON_INVALID_BEST_OF;
		return false;
	}
	if ( !IsSeriesSide( candidate.initialSide ) ) {
		reason = MP_SERIES_REASON_WRONG_VETO_SIDE;
		return false;
	}
	if ( candidate.mapPoolCount < candidate.bestOf ||
		candidate.mapPoolCount > MP_SERIES_MAX_MAP_POOL ) {
		reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
		return false;
	}

	for ( int i = 0; i < candidate.mapPoolCount; ++i ) {
		if ( !IsSafeMapToken( candidate.mapPool[ i ] ) ) {
			reason = MP_SERIES_REASON_INVALID_MAP_TOKEN;
			return false;
		}
	}
	for ( int i = 0; i < candidate.mapPoolCount; ++i ) {
		for ( int j = i + 1; j < candidate.mapPoolCount; ++j ) {
			if ( SeriesTokenEquals( candidate.mapPool[ i ], candidate.mapPool[ j ] ) ) {
				reason = MP_SERIES_REASON_DUPLICATE_MAP;
				return false;
			}
		}
	}

	if ( candidate.vetoStepCount < 1 || candidate.vetoStepCount > MP_SERIES_MAX_VETO_STEPS ||
		candidate.vetoSteps[ 0 ].expectedSide != candidate.initialSide ) {
		reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
		return false;
	}

	int mapSelections = 0;
	int mapBans = 0;
	int sideSelections = 0;
	int pendingSideSelections = 0;
	int deciders = 0;
	int expectedMapAuthority = candidate.initialSide;
	int pendingSelectionAuthority = MP_SERIES_SIDE_NONE;
	int deciderSelectionOrdinal = -1;
	for ( int i = 0; i < candidate.vetoStepCount; ++i ) {
		const mpSeriesVetoStep &step = candidate.vetoSteps[ i ];
		if ( step.action < MP_SERIES_VETO_BAN || step.action >= MP_SERIES_VETO_ACTION_COUNT ||
			!IsSeriesSide( step.expectedSide ) ) {
			reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
			return false;
		}

		switch ( step.action ) {
			case MP_SERIES_VETO_BAN:
				if ( pendingSideSelections != 0 || step.expectedSide != expectedMapAuthority ) {
					reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
					return false;
				}
				++mapBans;
				expectedMapAuthority = 1 - expectedMapAuthority;
				break;
			case MP_SERIES_VETO_PICK:
				if ( pendingSideSelections != 0 || step.expectedSide != expectedMapAuthority ) {
					reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
					return false;
				}
				++mapSelections;
				if ( candidate.requireStartingGameSide ) {
					++pendingSideSelections;
					pendingSelectionAuthority = step.expectedSide;
				}
				expectedMapAuthority = 1 - expectedMapAuthority;
				break;
			case MP_SERIES_VETO_DECIDER:
				if ( pendingSideSelections != 0 || step.expectedSide != expectedMapAuthority ) {
					reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
					return false;
				}
				++mapSelections;
				if ( candidate.requireStartingGameSide ) {
					++pendingSideSelections;
					pendingSelectionAuthority = step.expectedSide;
				}
				++deciders;
				deciderSelectionOrdinal = mapSelections;
				expectedMapAuthority = 1 - expectedMapAuthority;
				break;
			case MP_SERIES_VETO_SIDE:
				if ( pendingSideSelections != 1 ||
					step.expectedSide != 1 - pendingSelectionAuthority ) {
					reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
					return false;
				}
				--pendingSideSelections;
				pendingSelectionAuthority = MP_SERIES_SIDE_NONE;
				++sideSelections;
				break;
			default:
				reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
				return false;
		}
	}

	const int requiredSideSelections = candidate.requireStartingGameSide ?
		mapSelections : 0;
	const int minimumTerminalSteps = candidate.requireStartingGameSide ? 2 : 1;
	bool terminalPatternValid = false;
	if ( candidate.vetoStepCount >= minimumTerminalSteps ) {
		terminalPatternValid = candidate.requireStartingGameSide ?
			candidate.vetoSteps[ candidate.vetoStepCount - 2 ].action ==
				MP_SERIES_VETO_DECIDER &&
			candidate.vetoSteps[ candidate.vetoStepCount - 1 ].action ==
				MP_SERIES_VETO_SIDE :
			candidate.vetoSteps[ candidate.vetoStepCount - 1 ].action ==
				MP_SERIES_VETO_DECIDER;
	}
	if ( mapSelections != candidate.bestOf ||
		mapSelections + mapBans != candidate.mapPoolCount ||
		sideSelections != requiredSideSelections || pendingSideSelections != 0 ||
		deciders != 1 || deciderSelectionOrdinal != candidate.bestOf ||
		candidate.vetoStepCount < minimumTerminalSteps || !terminalPatternValid ) {
		reason = MP_SERIES_REASON_INVALID_VETO_PATTERN;
		return false;
	}

	return true;
}

mpSeriesMutationResult mpCompetitionSeries::Configure( const mpSeriesConfiguration &candidate,
	uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_DISABLED && state != MP_SERIES_CANCELLED && state != MP_SERIES_COMPLETE ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}
	mpSeriesReason_t reason;
	if ( !ValidateConfiguration( candidate, reason ) ) {
		return Rejected( reason );
	}

	const uint64_t previousRevision = revision;
	configuration = candidate;
	state = MP_SERIES_SETUP;
	currentVetoStep = 0;
	appliedVetoCount = 0;
	selectedMapCount = 0;
	nextSelectionIndex = 0;
	currentSelectionIndex = -1;
	attemptCount = 0;
	wins[ 0 ] = 0;
	wins[ 1 ] = 0;
	mapLoadFailureCount = 0;
	for ( int i = 0; i < MP_SERIES_MAX_MAP_POOL; ++i ) {
		mapDisposition[ i ] = MP_SERIES_MAP_AVAILABLE;
	}
	for ( int i = 0; i < MP_SERIES_MAX_VETO_STEPS; ++i ) {
		ClearAppliedVeto( appliedVetoes[ i ] );
	}
	for ( int i = 0; i < MP_SERIES_MAX_BEST_OF; ++i ) {
		ClearSelectedMap( selectedMaps[ i ] );
	}
	for ( int i = 0; i < MP_SERIES_MAX_MAP_ATTEMPTS; ++i ) {
		ClearAttempt( attempts[ i ] );
	}
	++revision;
	return Applied( previousRevision );
}

mpSeriesMutationResult mpCompetitionSeries::Start( uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_SETUP ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}

	const uint64_t previousRevision = revision;
	state = MP_SERIES_VETO;
	++revision;
	return Applied( previousRevision );
}

int mpCompetitionSeries::FindPoolMap( const char *mapToken ) const {
	if ( !IsSafeMapToken( mapToken ) ) {
		return -1;
	}
	for ( int i = 0; i < configuration.mapPoolCount; ++i ) {
		if ( SeriesTokenEquals( configuration.mapPool[ i ], mapToken ) ) {
			return i;
		}
	}
	return -1;
}

int mpCompetitionSeries::FindSelectedMap( int poolIndex ) const {
	for ( int i = 0; i < selectedMapCount; ++i ) {
		if ( selectedMaps[ i ].poolIndex == poolIndex ) {
			return i;
		}
	}
	return -1;
}

int mpCompetitionSeries::FindPendingSideSelection( int poolIndex ) const {
	const int index = FindSelectedMap( poolIndex );
	if ( index < 0 || selectedMaps[ index ].hasStartingGameSide ) {
		return -1;
	}
	return index;
}

mpSeriesMutationResult mpCompetitionSeries::ApplyVeto( int actingSide,
	mpSeriesVetoAction_t action, const char *mapToken, int selectedGameSide,
	uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_VETO || currentVetoStep < 0 ||
		currentVetoStep >= configuration.vetoStepCount ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}
	if ( appliedVetoCount != currentVetoStep ||
		appliedVetoCount >= MP_SERIES_MAX_VETO_STEPS ) {
		return Rejected( MP_SERIES_REASON_INVALID_VETO_PATTERN );
	}

	const mpSeriesVetoStep &expected = configuration.vetoSteps[ currentVetoStep ];
	if ( action != expected.action ) {
		return Rejected( MP_SERIES_REASON_WRONG_VETO_ACTION );
	}
	if ( actingSide != expected.expectedSide ) {
		return Rejected( MP_SERIES_REASON_WRONG_VETO_SIDE );
	}

	const int poolIndex = FindPoolMap( mapToken );
	if ( poolIndex < 0 ) {
		return Rejected( MP_SERIES_REASON_INVALID_MAP_TOKEN );
	}

	int sideSelectionIndex = -1;
	const bool addsSelection = action == MP_SERIES_VETO_PICK || action == MP_SERIES_VETO_DECIDER;
	const bool finishesVeto = currentVetoStep + 1 == configuration.vetoStepCount;
	if ( finishesVeto && selectedMapCount + ( addsSelection ? 1 : 0 ) != configuration.bestOf ) {
		return Rejected( MP_SERIES_REASON_INVALID_VETO_PATTERN );
	}
	if ( action == MP_SERIES_VETO_SIDE ) {
		if ( !IsSeriesSide( selectedGameSide ) ) {
			return Rejected( MP_SERIES_REASON_INVALID_ARGUMENT );
		}
		sideSelectionIndex = FindPendingSideSelection( poolIndex );
		if ( sideSelectionIndex < 0 ) {
			return Rejected( FindSelectedMap( poolIndex ) >= 0 ?
				MP_SERIES_REASON_SIDE_ALREADY_SELECTED : MP_SERIES_REASON_MAP_NOT_SELECTED );
		}
		if ( sideSelectionIndex + 1 != selectedMapCount ) {
			return Rejected( MP_SERIES_REASON_INVALID_VETO_PATTERN );
		}
	} else {
		if ( selectedGameSide != MP_SERIES_SIDE_NONE ) {
			return Rejected( MP_SERIES_REASON_INVALID_ARGUMENT );
		}
		if ( mapDisposition[ poolIndex ] != MP_SERIES_MAP_AVAILABLE ) {
			return Rejected( MP_SERIES_REASON_MAP_NOT_AVAILABLE );
		}
		if ( action != MP_SERIES_VETO_BAN && selectedMapCount >= configuration.bestOf ) {
			return Rejected( MP_SERIES_REASON_SELECTION_CAPACITY );
		}
		if ( action == MP_SERIES_VETO_DECIDER ) {
			int availableMaps = 0;
			for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
				if ( mapDisposition[ index ] == MP_SERIES_MAP_AVAILABLE ) {
					++availableMaps;
				}
			}
			if ( availableMaps != 1 ) {
				return Rejected( MP_SERIES_REASON_INVALID_VETO_PATTERN );
			}
		}
	}

	const uint64_t previousRevision = revision;
	mpSeriesAppliedVeto &appliedVeto = appliedVetoes[ appliedVetoCount++ ];
	appliedVeto.action = action;
	appliedVeto.actingSide = actingSide;
	appliedVeto.poolIndex = poolIndex;
	appliedVeto.selectedGameSide = action == MP_SERIES_VETO_SIDE ?
		selectedGameSide : MP_SERIES_SIDE_NONE;
	if ( action == MP_SERIES_VETO_BAN ) {
		mapDisposition[ poolIndex ] = MP_SERIES_MAP_BANNED;
	} else if ( action == MP_SERIES_VETO_PICK || action == MP_SERIES_VETO_DECIDER ) {
		mpSeriesSelectedMap &selection = selectedMaps[ selectedMapCount++ ];
		selection.poolIndex = poolIndex;
		selection.selectedBySide = actingSide;
		selection.decider = action == MP_SERIES_VETO_DECIDER;
		selection.hasStartingGameSide = false;
		selection.startingGameSide = MP_SERIES_SIDE_NONE;
		selection.gameSideChosenBy = MP_SERIES_SIDE_NONE;
		mapDisposition[ poolIndex ] = MP_SERIES_MAP_SELECTED;
	} else {
		mpSeriesSelectedMap &selection = selectedMaps[ sideSelectionIndex ];
		selection.hasStartingGameSide = true;
		selection.startingGameSide = selectedGameSide;
		selection.gameSideChosenBy = actingSide;
	}

	++currentVetoStep;
	if ( currentVetoStep == configuration.vetoStepCount ) {
		state = MP_SERIES_READY;
	}
	++revision;
	return Applied( previousRevision );
}

const char *mpCompetitionSeries::GetNextMapToken( void ) const {
	if ( ( state != MP_SERIES_READY && state != MP_SERIES_MAP_COMPLETE ) ||
		nextSelectionIndex < 0 || nextSelectionIndex >= selectedMapCount ) {
		return NULL;
	}
	const int poolIndex = selectedMaps[ nextSelectionIndex ].poolIndex;
	if ( poolIndex < 0 || poolIndex >= configuration.mapPoolCount ) {
		return NULL;
	}
	return configuration.mapPool[ poolIndex ];
}

mpSeriesMutationResult mpCompetitionSeries::BeginMap( const char *mapToken,
	uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_READY ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}
	const char *expectedMap = GetNextMapToken();
	if ( expectedMap == NULL || !SeriesTokenEquals( expectedMap, mapToken ) ) {
		return Rejected( MP_SERIES_REASON_WRONG_MAP );
	}

	const uint64_t previousRevision = revision;
	currentSelectionIndex = nextSelectionIndex;
	state = MP_SERIES_MAP_ACTIVE;
	++revision;
	return Applied( previousRevision );
}

mpSeriesMutationResult mpCompetitionSeries::ReportMapLoadFailure( const char *mapToken,
	uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_READY ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}
	const char *expectedMap = GetNextMapToken();
	if ( expectedMap == NULL || !SeriesTokenEquals( expectedMap, mapToken ) ) {
		return Rejected( MP_SERIES_REASON_WRONG_MAP );
	}
	if ( mapLoadFailureCount == MP_SERIES_INT_MAX ) {
		return Rejected( MP_SERIES_REASON_ATTEMPT_CAPACITY );
	}

	const uint64_t previousRevision = revision;
	++mapLoadFailureCount;
	++revision;
	return Applied( previousRevision );
}

mpSeriesMutationResult mpCompetitionSeries::CommitMapResult( mpSeriesMapOutcome_t outcome,
	int winnerSide, int side0Score, int side1Score, uint64_t matchSessionId,
	uint64_t rulesDigest, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_MAP_ACTIVE || currentSelectionIndex != nextSelectionIndex ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}
	if ( attemptCount >= MP_SERIES_MAX_MAP_ATTEMPTS ) {
		return Rejected( MP_SERIES_REASON_ATTEMPT_CAPACITY );
	}
	if ( outcome != MP_SERIES_MAP_DECIDED && outcome != MP_SERIES_MAP_FORFEIT &&
		outcome != MP_SERIES_MAP_ABORTED ) {
		return Rejected( MP_SERIES_REASON_INVALID_RESULT );
	}
	if ( ( outcome == MP_SERIES_MAP_ABORTED && winnerSide != MP_SERIES_SIDE_NONE ) ||
		( outcome != MP_SERIES_MAP_ABORTED && !IsSeriesSide( winnerSide ) ) ||
		matchSessionId == 0 || rulesDigest == 0 ) {
		return Rejected( MP_SERIES_REASON_INVALID_RESULT );
	}

	const uint64_t previousRevision = revision;
	mpSeriesMapAttempt &attempt = attempts[ attemptCount++ ];
	attempt.selectionIndex = currentSelectionIndex;
	attempt.outcome = outcome;
	attempt.winnerSide = winnerSide;
	attempt.score[ 0 ] = side0Score;
	attempt.score[ 1 ] = side1Score;
	attempt.matchSessionId = matchSessionId;
	attempt.rulesDigest = rulesDigest;
	if ( outcome == MP_SERIES_MAP_DECIDED || outcome == MP_SERIES_MAP_FORFEIT ) {
		++wins[ winnerSide ];
	}
	state = MP_SERIES_MAP_COMPLETE;
	++revision;
	return Applied( previousRevision );
}

bool mpCompetitionSeries::HasSeriesWinner( void ) const {
	const int needed = configuration.bestOf / 2 + 1;
	return wins[ 0 ] >= needed || wins[ 1 ] >= needed;
}

mpSeriesMutationResult mpCompetitionSeries::AdvanceAfterMap( uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state != MP_SERIES_MAP_COMPLETE || attemptCount <= 0 ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}

	const mpSeriesMapAttempt &lastAttempt = attempts[ attemptCount - 1 ];
	if ( lastAttempt.selectionIndex != currentSelectionIndex ) {
		return Rejected( MP_SERIES_REASON_INVALID_RESULT );
	}
	const bool seriesWinner = HasSeriesWinner();
	if ( !seriesWinner && lastAttempt.outcome != MP_SERIES_MAP_ABORTED &&
		nextSelectionIndex + 1 >= selectedMapCount ) {
		return Rejected( MP_SERIES_REASON_INVALID_RESULT );
	}
	const uint64_t previousRevision = revision;
	currentSelectionIndex = -1;
	if ( seriesWinner ) {
		state = MP_SERIES_COMPLETE;
	} else if ( lastAttempt.outcome == MP_SERIES_MAP_ABORTED ) {
		state = MP_SERIES_READY;
	} else {
		++nextSelectionIndex;
		state = MP_SERIES_READY;
	}
	++revision;
	return Applied( previousRevision );
}

mpSeriesMutationResult mpCompetitionSeries::Cancel( uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_SERIES_REASON_STALE_REVISION );
	}
	if ( state == MP_SERIES_CANCELLED ) {
		return NoChange( MP_SERIES_REASON_NONE );
	}
	if ( state == MP_SERIES_DISABLED || state == MP_SERIES_COMPLETE ) {
		return Rejected( MP_SERIES_REASON_WRONG_STATE );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_SERIES_REASON_REVISION_EXHAUSTED );
	}

	const uint64_t previousRevision = revision;
	state = MP_SERIES_CANCELLED;
	currentSelectionIndex = -1;
	++revision;
	return Applied( previousRevision );
}

mpSeriesState_t mpCompetitionSeries::GetState( void ) const {
	return state;
}

uint64_t mpCompetitionSeries::GetRevision( void ) const {
	return revision;
}

const mpSeriesConfiguration &mpCompetitionSeries::GetConfiguration( void ) const {
	return configuration;
}

int mpCompetitionSeries::GetCurrentVetoStep( void ) const {
	return currentVetoStep;
}

int mpCompetitionSeries::GetAppliedVetoCount( void ) const {
	return appliedVetoCount;
}

const mpSeriesAppliedVeto *mpCompetitionSeries::GetAppliedVeto( int index ) const {
	if ( index < 0 || index >= appliedVetoCount ) {
		return NULL;
	}
	return &appliedVetoes[ index ];
}

int mpCompetitionSeries::GetSelectedMapCount( void ) const {
	return selectedMapCount;
}

const mpSeriesSelectedMap *mpCompetitionSeries::GetSelectedMap( int index ) const {
	if ( index < 0 || index >= selectedMapCount ) {
		return NULL;
	}
	return &selectedMaps[ index ];
}

mpSeriesMapDisposition_t mpCompetitionSeries::GetMapDisposition( int poolIndex ) const {
	if ( poolIndex < 0 || poolIndex >= configuration.mapPoolCount ) {
		return MP_SERIES_MAP_DISPOSITION_COUNT;
	}
	return mapDisposition[ poolIndex ];
}

int mpCompetitionSeries::GetNextSelectionIndex( void ) const {
	return nextSelectionIndex;
}

int mpCompetitionSeries::GetCurrentSelectionIndex( void ) const {
	return currentSelectionIndex;
}

int mpCompetitionSeries::GetAttemptCount( void ) const {
	return attemptCount;
}

const mpSeriesMapAttempt *mpCompetitionSeries::GetAttempt( int index ) const {
	if ( index < 0 || index >= attemptCount ) {
		return NULL;
	}
	return &attempts[ index ];
}

int mpCompetitionSeries::GetWins( int side ) const {
	return IsSeriesSide( side ) ? wins[ side ] : 0;
}

int mpCompetitionSeries::GetMapLoadFailureCount( void ) const {
	return mapLoadFailureCount;
}

bool mpCompetitionSeries::ValidateInvariants( void ) const {
	if ( state < MP_SERIES_DISABLED || state >= MP_SERIES_STATE_COUNT ||
		appliedVetoCount < 0 || appliedVetoCount > MP_SERIES_MAX_VETO_STEPS ||
		selectedMapCount < 0 || selectedMapCount > MP_SERIES_MAX_BEST_OF ||
		attemptCount < 0 || attemptCount > MP_SERIES_MAX_MAP_ATTEMPTS ||
		nextSelectionIndex < 0 || nextSelectionIndex > selectedMapCount ||
		mapLoadFailureCount < 0 || wins[ 0 ] < 0 || wins[ 1 ] < 0 ) {
		return false;
	}

	if ( state == MP_SERIES_DISABLED ) {
		return revision == 0 && currentVetoStep == 0 && appliedVetoCount == 0 &&
			selectedMapCount == 0 && attemptCount == 0;
	}

	mpSeriesReason_t reason;
	if ( !ValidateConfiguration( configuration, reason ) ) {
		return false;
	}
	if ( currentVetoStep < 0 || currentVetoStep > configuration.vetoStepCount ||
		appliedVetoCount != currentVetoStep ||
		selectedMapCount > configuration.bestOf ||
		wins[ 0 ] + wins[ 1 ] > attemptCount ) {
		return false;
	}
	if ( state == MP_SERIES_SETUP && currentVetoStep != 0 ) {
		return false;
	}
	if ( state == MP_SERIES_VETO && currentVetoStep >= configuration.vetoStepCount ) {
		return false;
	}
	if ( state != MP_SERIES_SETUP && state != MP_SERIES_VETO &&
		state != MP_SERIES_CANCELLED &&
		currentVetoStep != configuration.vetoStepCount ) {
		return false;
	}

	int expectedSelectedMaps = 0;
	int expectedBannedMaps = 0;
	int mostRecentSelection = -1;
	mpSeriesMapDisposition_t historyDisposition[ MP_SERIES_MAX_MAP_POOL ];
	for ( int index = 0; index < MP_SERIES_MAX_MAP_POOL; ++index ) {
		historyDisposition[ index ] = MP_SERIES_MAP_AVAILABLE;
	}
	for ( int stepIndex = 0; stepIndex < currentVetoStep; ++stepIndex ) {
		const mpSeriesVetoStep &step = configuration.vetoSteps[ stepIndex ];
		const mpSeriesAppliedVeto &applied = appliedVetoes[ stepIndex ];
		if ( applied.action != step.action || applied.actingSide != step.expectedSide ||
			applied.poolIndex < 0 || applied.poolIndex >= configuration.mapPoolCount ) {
			return false;
		}
		if ( step.action == MP_SERIES_VETO_BAN ) {
			if ( applied.selectedGameSide != MP_SERIES_SIDE_NONE ||
				historyDisposition[ applied.poolIndex ] != MP_SERIES_MAP_AVAILABLE ) {
				return false;
			}
			historyDisposition[ applied.poolIndex ] = MP_SERIES_MAP_BANNED;
			++expectedBannedMaps;
		} else if ( step.action == MP_SERIES_VETO_PICK ||
			step.action == MP_SERIES_VETO_DECIDER ) {
			if ( applied.selectedGameSide != MP_SERIES_SIDE_NONE ||
				historyDisposition[ applied.poolIndex ] != MP_SERIES_MAP_AVAILABLE ) {
				return false;
			}
			historyDisposition[ applied.poolIndex ] = MP_SERIES_MAP_SELECTED;
			mostRecentSelection = expectedSelectedMaps++;
			if ( mostRecentSelection >= selectedMapCount ||
				selectedMaps[ mostRecentSelection ].poolIndex != applied.poolIndex ||
				selectedMaps[ mostRecentSelection ].selectedBySide != step.expectedSide ||
				selectedMaps[ mostRecentSelection ].decider !=
					( step.action == MP_SERIES_VETO_DECIDER ) ) {
				return false;
			}
		} else if ( step.action == MP_SERIES_VETO_SIDE ) {
			if ( mostRecentSelection < 0 || mostRecentSelection >= selectedMapCount ||
				!IsSeriesSide( applied.selectedGameSide ) ||
				applied.poolIndex != selectedMaps[ mostRecentSelection ].poolIndex ||
				historyDisposition[ applied.poolIndex ] != MP_SERIES_MAP_SELECTED ||
				!selectedMaps[ mostRecentSelection ].hasStartingGameSide ||
				selectedMaps[ mostRecentSelection ].startingGameSide !=
					applied.selectedGameSide ||
				selectedMaps[ mostRecentSelection ].gameSideChosenBy != step.expectedSide ) {
				return false;
			}
		}
	}
	if ( expectedSelectedMaps != selectedMapCount ) {
		return false;
	}

	bool seenPoolMap[ MP_SERIES_MAX_MAP_POOL ];
	memset( seenPoolMap, 0, sizeof( seenPoolMap ) );
	for ( int i = 0; i < selectedMapCount; ++i ) {
		const mpSeriesSelectedMap &selection = selectedMaps[ i ];
		if ( selection.poolIndex < 0 || selection.poolIndex >= configuration.mapPoolCount ||
			seenPoolMap[ selection.poolIndex ] ||
			mapDisposition[ selection.poolIndex ] != MP_SERIES_MAP_SELECTED ||
			!IsSeriesSide( selection.selectedBySide ) ||
			( selection.hasStartingGameSide &&
				( !IsSeriesSide( selection.startingGameSide ) || !IsSeriesSide( selection.gameSideChosenBy ) ) ) ) {
			return false;
		}
		seenPoolMap[ selection.poolIndex ] = true;
	}
	int actualSelectedMaps = 0;
	int actualBannedMaps = 0;
	for ( int i = 0; i < configuration.mapPoolCount; ++i ) {
		if ( mapDisposition[ i ] != historyDisposition[ i ] ) {
			return false;
		}
		if ( mapDisposition[ i ] == MP_SERIES_MAP_SELECTED ) {
			++actualSelectedMaps;
		} else if ( mapDisposition[ i ] == MP_SERIES_MAP_BANNED ) {
			++actualBannedMaps;
		} else if ( mapDisposition[ i ] != MP_SERIES_MAP_AVAILABLE ) {
			return false;
		}
	}
	if ( actualSelectedMaps != selectedMapCount ||
		actualBannedMaps != expectedBannedMaps ) {
		return false;
	}

	for ( int i = 0; i < attemptCount; ++i ) {
		const mpSeriesMapAttempt &attempt = attempts[ i ];
		if ( attempt.selectionIndex < 0 || attempt.selectionIndex >= selectedMapCount ||
			attempt.outcome <= MP_SERIES_MAP_UNPLAYED || attempt.outcome >= MP_SERIES_MAP_OUTCOME_COUNT ||
			attempt.matchSessionId == 0 || attempt.rulesDigest == 0 ||
			( attempt.outcome == MP_SERIES_MAP_ABORTED ?
				attempt.winnerSide != MP_SERIES_SIDE_NONE : !IsSeriesSide( attempt.winnerSide ) ) ) {
			return false;
		}
	}

	if ( state >= MP_SERIES_READY && state != MP_SERIES_CANCELLED &&
		selectedMapCount != configuration.bestOf ) {
		return false;
	}
	if ( state >= MP_SERIES_READY && state != MP_SERIES_CANCELLED ) {
		for ( int i = 0; i < selectedMapCount; ++i ) {
			if ( selectedMaps[ i ].hasStartingGameSide !=
					configuration.requireStartingGameSide ||
				selectedMaps[ i ].decider != ( i + 1 == selectedMapCount ) ) {
				return false;
			}
		}
		if ( actualSelectedMaps + actualBannedMaps != configuration.mapPoolCount ) {
			return false;
		}
	}
	if ( state == MP_SERIES_MAP_ACTIVE || state == MP_SERIES_MAP_COMPLETE ) {
		if ( currentSelectionIndex != nextSelectionIndex || currentSelectionIndex < 0 ||
			currentSelectionIndex >= selectedMapCount ) {
			return false;
		}
	} else if ( currentSelectionIndex != -1 ) {
		return false;
	}
	if ( state == MP_SERIES_COMPLETE && !HasSeriesWinner() ) {
		return false;
	}

	return true;
}

bool mpCompetitionSeries::ExportRecoveryState(
		mpSeriesRecoveryState &recovery ) const {
	if ( !ValidateInvariants() ) {
		return false;
	}
	mpSeriesRecoveryState candidate;
	memset( &candidate, 0, sizeof( candidate ) );
	candidate.schemaVersion = MP_SERIES_RECOVERY_SCHEMA_VERSION;
	candidate.state = state;
	candidate.revision = revision;
	candidate.configuration = configuration;
	candidate.currentVetoStep = currentVetoStep;
	memcpy( candidate.appliedVetoes, appliedVetoes, sizeof( appliedVetoes ) );
	candidate.appliedVetoCount = appliedVetoCount;
	memcpy( candidate.mapDisposition, mapDisposition, sizeof( mapDisposition ) );
	memcpy( candidate.selectedMaps, selectedMaps, sizeof( selectedMaps ) );
	candidate.selectedMapCount = selectedMapCount;
	candidate.nextSelectionIndex = nextSelectionIndex;
	candidate.currentSelectionIndex = currentSelectionIndex;
	memcpy( candidate.attempts, attempts, sizeof( attempts ) );
	candidate.attemptCount = attemptCount;
	candidate.wins[ 0 ] = wins[ 0 ];
	candidate.wins[ 1 ] = wins[ 1 ];
	candidate.mapLoadFailureCount = mapLoadFailureCount;
	recovery = candidate;
	return true;
}

bool mpCompetitionSeries::RestoreRecoveryState(
		const mpSeriesRecoveryState &recovery ) {
	if ( recovery.schemaVersion != MP_SERIES_RECOVERY_SCHEMA_VERSION ) {
		return false;
	}
	mpCompetitionSeries candidate;
	candidate.state = recovery.state;
	candidate.revision = recovery.revision;
	candidate.configuration = recovery.configuration;
	candidate.currentVetoStep = recovery.currentVetoStep;
	memcpy( candidate.appliedVetoes, recovery.appliedVetoes,
		sizeof( candidate.appliedVetoes ) );
	candidate.appliedVetoCount = recovery.appliedVetoCount;
	memcpy( candidate.mapDisposition, recovery.mapDisposition,
		sizeof( candidate.mapDisposition ) );
	memcpy( candidate.selectedMaps, recovery.selectedMaps,
		sizeof( candidate.selectedMaps ) );
	candidate.selectedMapCount = recovery.selectedMapCount;
	candidate.nextSelectionIndex = recovery.nextSelectionIndex;
	candidate.currentSelectionIndex = recovery.currentSelectionIndex;
	memcpy( candidate.attempts, recovery.attempts,
		sizeof( candidate.attempts ) );
	candidate.attemptCount = recovery.attemptCount;
	candidate.wins[ 0 ] = recovery.wins[ 0 ];
	candidate.wins[ 1 ] = recovery.wins[ 1 ];
	candidate.mapLoadFailureCount = recovery.mapLoadFailureCount;
	if ( !candidate.ValidateInvariants() ) {
		return false;
	}
	*this = candidate;
	return true;
}
