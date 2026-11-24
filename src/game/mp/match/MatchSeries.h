//----------------------------------------------------------------
// MatchSeries.h
//
// Bounded, transactional best-of series and map-veto state.
//
// This core owns no console commands, GUI state, cvars, filesystem paths or
// map loading.  An adapter supplies validated installed-map information and
// persists a copied recovery view after each committed mutation.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_H__
#define __MP_MATCH_SERIES_H__

#include <stdint.h>
#include <stddef.h>

static const int MP_SERIES_SIDE_NONE = -1;
static const int MP_SERIES_SIDE_COUNT = 2;
static const int MP_SERIES_MAX_BEST_OF = 15;
static const int MP_SERIES_MAX_MAP_POOL = 32;
static const int MP_SERIES_MAX_VETO_STEPS = 64;
static const int MP_SERIES_MAX_MAP_ATTEMPTS = 64;
static const int MP_SERIES_MAP_TOKEN_BYTES = 64;

// Defined by MatchSeriesRecovery.h.  The series core only exposes a narrow,
// transactional snapshot seam; filesystem and wire-format policy remain in
// the recovery module.
struct mpSeriesRecoveryState;

// Built-in profiles describe policy only.  They never contain executable
// configuration text, map-loading commands or filesystem paths.
typedef enum {
	MP_SERIES_PROFILE_BEST_OF_ONE = 0,
	MP_SERIES_PROFILE_BEST_OF_THREE,
	MP_SERIES_PROFILE_BEST_OF_FIVE,
	MP_SERIES_PROFILE_COUNT,
	MP_SERIES_PROFILE_CUSTOM = -1
} mpSeriesProfileId_t;

typedef enum {
	// Every pool map is consumed. Map authority alternates, the non-selecting
	// side chooses the starting game side, and the final map is a decider.
	MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE = 0,
	MP_SERIES_VETO_POLICY_COUNT
} mpSeriesVetoPolicy_t;

struct mpSeriesProfileDescriptor {
	mpSeriesProfileId_t	id;
	const char *			key;
	const char *			labelLocalizationKey;
	const char *			descriptionLocalizationKey;
	int					bestOf;
	int					minimumMapPool;
	int					maximumMapPool;
	mpSeriesVetoPolicy_t	vetoPolicy;
};

// Append only: these values are written to recovery records and evidence.
typedef enum {
	MP_SERIES_DISABLED = 0,
	MP_SERIES_SETUP,
	MP_SERIES_VETO,
	MP_SERIES_READY,
	MP_SERIES_MAP_ACTIVE,
	MP_SERIES_MAP_COMPLETE,
	MP_SERIES_COMPLETE,
	MP_SERIES_CANCELLED,
	MP_SERIES_STATE_COUNT
} mpSeriesState_t;

typedef enum {
	MP_SERIES_VETO_BAN = 0,
	MP_SERIES_VETO_PICK,
	MP_SERIES_VETO_SIDE,
	MP_SERIES_VETO_DECIDER,
	MP_SERIES_VETO_ACTION_COUNT
} mpSeriesVetoAction_t;

typedef enum {
	MP_SERIES_MAP_AVAILABLE = 0,
	MP_SERIES_MAP_BANNED,
	MP_SERIES_MAP_SELECTED,
	MP_SERIES_MAP_DISPOSITION_COUNT
} mpSeriesMapDisposition_t;

typedef enum {
	MP_SERIES_MAP_UNPLAYED = 0,
	MP_SERIES_MAP_DECIDED,
	MP_SERIES_MAP_FORFEIT,
	MP_SERIES_MAP_ABORTED,
	MP_SERIES_MAP_OUTCOME_COUNT
} mpSeriesMapOutcome_t;

typedef enum {
	MP_SERIES_MUTATION_APPLIED = 0,
	MP_SERIES_MUTATION_NO_CHANGE,
	MP_SERIES_MUTATION_REJECTED,
	MP_SERIES_MUTATION_CODE_COUNT
} mpSeriesMutationCode_t;

// Stable semantic rejection reasons. Presentation maps these to localization
// ids; the core never owns display text.
typedef enum {
	MP_SERIES_REASON_NONE = 0,
	MP_SERIES_REASON_INVALID_ARGUMENT,
	MP_SERIES_REASON_STALE_REVISION,
	MP_SERIES_REASON_REVISION_EXHAUSTED,
	MP_SERIES_REASON_WRONG_STATE,
	MP_SERIES_REASON_INVALID_BEST_OF,
	MP_SERIES_REASON_INVALID_GAME_TYPE,
	MP_SERIES_REASON_INVALID_MAP_TOKEN,
	MP_SERIES_REASON_DUPLICATE_MAP,
	MP_SERIES_REASON_INVALID_VETO_PATTERN,
	MP_SERIES_REASON_WRONG_VETO_ACTION,
	MP_SERIES_REASON_WRONG_VETO_SIDE,
	MP_SERIES_REASON_MAP_NOT_AVAILABLE,
	MP_SERIES_REASON_MAP_NOT_SELECTED,
	MP_SERIES_REASON_SIDE_ALREADY_SELECTED,
	MP_SERIES_REASON_SELECTION_CAPACITY,
	MP_SERIES_REASON_ATTEMPT_CAPACITY,
	MP_SERIES_REASON_WRONG_MAP,
	MP_SERIES_REASON_INVALID_RESULT,
	MP_SERIES_REASON_SERIES_DECIDED,
	// Append-only profile-registry failures.
	MP_SERIES_REASON_UNKNOWN_PROFILE,
	MP_SERIES_REASON_PROFILE_BEST_OF_MISMATCH,
	MP_SERIES_REASON_COUNT
} mpSeriesReason_t;

struct mpSeriesMutationResult {
	mpSeriesMutationCode_t	code;
	mpSeriesReason_t		reason;
	uint64_t			previousRevision;
	uint64_t			currentRevision;

	bool				WasApplied( void ) const;
	bool				WasRejected( void ) const;
};

struct mpSeriesVetoStep {
	mpSeriesVetoAction_t	action;
	int					expectedSide;
};

// Exact, bounded history of committed veto operations.  Recovery, evidence
// and recipient projections may consume this, but only ApplyVeto appends it.
struct mpSeriesAppliedVeto {
	mpSeriesVetoAction_t	action;
	int					actingSide;
	int					poolIndex;
	int					selectedGameSide;
};

struct mpSeriesConfiguration {
	mpSeriesProfileId_t	sourceProfile;
	int					gameType;
	int					bestOf;
	uint64_t			deterministicSeed;
	int					initialSide;
	// Team modes can require a Marine/Strogg starting-side choice for every
	// selected map. Duel leaves this false because it has no enforceable team
	// faction to choose.
	bool					requireStartingGameSide;
	int					mapPoolCount;
	char				mapPool[ MP_SERIES_MAX_MAP_POOL ][ MP_SERIES_MAP_TOKEN_BYTES ];
	int					vetoStepCount;
	mpSeriesVetoStep	vetoSteps[ MP_SERIES_MAX_VETO_STEPS ];
};

int MPSeriesProfileDescriptorCount( void );
const mpSeriesProfileDescriptor *MPSeriesProfileDescriptorForId(
	mpSeriesProfileId_t profile );
const mpSeriesProfileDescriptor *MPSeriesProfileByKey( const char *key );

// Builds a complete, validated draft from an adapter-supplied installed map
// pool.  On failure, draft is unchanged and reason identifies the rejected
// profile input.  Map token strings are copied into bounded storage.
bool MPSeriesBuildProfileDraft( mpSeriesProfileId_t profile,
	int gameType, uint64_t deterministicSeed, int initialSide,
	bool requireStartingGameSide,
	const char * const *mapTokens, int mapTokenCount,
	mpSeriesConfiguration &draft, mpSeriesReason_t &reason );

struct mpSeriesSelectedMap {
	int		poolIndex;
	int		selectedBySide;
	bool	decider;
	bool	hasStartingGameSide;
	int		startingGameSide;
	int		gameSideChosenBy;
};

struct mpSeriesMapAttempt {
	int				selectionIndex;
	mpSeriesMapOutcome_t outcome;
	int				winnerSide;
	int				score[ MP_SERIES_SIDE_COUNT ];
	uint64_t		matchSessionId;
	uint64_t		rulesDigest;
};

class mpCompetitionSeries {
public:
						mpCompetitionSeries( void );

	void				Reset( void );
	mpSeriesMutationResult Configure( const mpSeriesConfiguration &configuration,
							uint64_t expectedRevision );
	mpSeriesMutationResult Start( uint64_t expectedRevision );
	mpSeriesMutationResult ApplyVeto( int actingSide, mpSeriesVetoAction_t action,
							const char *mapToken, int selectedGameSide,
							uint64_t expectedRevision );
	mpSeriesMutationResult BeginMap( const char *mapToken, uint64_t expectedRevision );
	mpSeriesMutationResult ReportMapLoadFailure( const char *mapToken,
							uint64_t expectedRevision );
	mpSeriesMutationResult CommitMapResult( mpSeriesMapOutcome_t outcome,
							int winnerSide, int side0Score, int side1Score,
							uint64_t matchSessionId, uint64_t rulesDigest,
							uint64_t expectedRevision );
	mpSeriesMutationResult AdvanceAfterMap( uint64_t expectedRevision );
	mpSeriesMutationResult Cancel( uint64_t expectedRevision );

	mpSeriesState_t		GetState( void ) const;
	uint64_t			GetRevision( void ) const;
	const mpSeriesConfiguration &GetConfiguration( void ) const;
	int					GetCurrentVetoStep( void ) const;
	int					GetAppliedVetoCount( void ) const;
	const mpSeriesAppliedVeto *GetAppliedVeto( int index ) const;
	int					GetSelectedMapCount( void ) const;
	const mpSeriesSelectedMap *GetSelectedMap( int index ) const;
	mpSeriesMapDisposition_t GetMapDisposition( int poolIndex ) const;
	const char *			GetNextMapToken( void ) const;
	int					GetNextSelectionIndex( void ) const;
	int					GetCurrentSelectionIndex( void ) const;
	int					GetAttemptCount( void ) const;
	const mpSeriesMapAttempt *GetAttempt( int index ) const;
	int					GetWins( int side ) const;
	int					GetMapLoadFailureCount( void ) const;
	bool				ValidateInvariants( void ) const;
	bool				ExportRecoveryState( mpSeriesRecoveryState &recovery ) const;
	bool				RestoreRecoveryState( const mpSeriesRecoveryState &recovery );

	static bool			ValidateConfiguration( const mpSeriesConfiguration &configuration,
							mpSeriesReason_t &reason );
	static bool			IsSafeMapToken( const char *mapToken );

private:
	bool				IsExpectedRevision( uint64_t expectedRevision ) const;
	bool				CanCommit( void ) const;
	mpSeriesMutationResult Applied( uint64_t previousRevision );
	mpSeriesMutationResult NoChange( mpSeriesReason_t reason ) const;
	mpSeriesMutationResult Rejected( mpSeriesReason_t reason ) const;
	int					FindPoolMap( const char *mapToken ) const;
	int					FindSelectedMap( int poolIndex ) const;
	int					FindPendingSideSelection( int poolIndex ) const;
	bool				HasSeriesWinner( void ) const;

	mpSeriesState_t		state;
	uint64_t			revision;
	mpSeriesConfiguration configuration;
	int					currentVetoStep;
	mpSeriesAppliedVeto appliedVetoes[ MP_SERIES_MAX_VETO_STEPS ];
	int					appliedVetoCount;
	mpSeriesMapDisposition_t mapDisposition[ MP_SERIES_MAX_MAP_POOL ];
	mpSeriesSelectedMap	selectedMaps[ MP_SERIES_MAX_BEST_OF ];
	int					selectedMapCount;
	int					nextSelectionIndex;
	int					currentSelectionIndex;
	mpSeriesMapAttempt	attempts[ MP_SERIES_MAX_MAP_ATTEMPTS ];
	int					attemptCount;
	int					wins[ MP_SERIES_SIDE_COUNT ];
	int					mapLoadFailureCount;
};

#endif // __MP_MATCH_SERIES_H__
