//----------------------------------------------------------------
// MatchSeriesReport.h
//
// Bounded, allocation-free final competition-series reports.  This is an
// immutable result artifact builder, not mutable cross-map recovery state.
// It owns no game pointers and performs no filesystem, network, GUI, command,
// recorder, cvar, clock, or authentication I/O.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SERIES_REPORT_H__
#define __MP_MATCH_SERIES_REPORT_H__

#include "MatchSeries.h"

#include <stdint.h>

static const uint32_t MP_SERIES_REPORT_SCHEMA_VERSION = 1;
static const int MP_SERIES_REPORT_MAX_MAP_RESULTS = MP_SERIES_MAX_MAP_ATTEMPTS;
static const int MP_SERIES_REPORT_MAX_PARTICIPANTS = 32;
static const int MP_SERIES_REPORT_MAX_TEAMS = MP_SERIES_SIDE_COUNT;
static const int MP_SERIES_REPORT_PROFILE_KEY_BYTES = 32;
static const int MP_SERIES_REPORT_MODE_TOKEN_BYTES = 32;
static const int MP_SERIES_REPORT_DISPLAY_NAME_BYTES = 64;
static const int MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES = 160;
static const int MP_SERIES_REPORT_MAX_JSON_BYTES = 131072;
static const int MP_SERIES_REPORT_CONTESTANT_NONE = -1;
static const uint16_t MP_SERIES_REPORT_CHECKPOINT_STATE_VERSION = 1;

/*
===============================================================================

	Stable identity

===============================================================================
*/

typedef enum {
	MP_SERIES_REPORT_CONTESTANT_INVALID = 0,
	MP_SERIES_REPORT_CONTESTANT_PARTICIPANT,
	MP_SERIES_REPORT_CONTESTANT_SIDE,
	MP_SERIES_REPORT_CONTESTANT_KIND_COUNT
} mpSeriesReportContestantKind_t;

// A participant contestant carries a stable participant sequence and a
// role-neutral display name.  A team contestant carries no participant
// sequence and uses a stable side label.  Connection slots and role masks are
// deliberately absent.
struct mpSeriesReportContestantInput {
	mpSeriesReportContestantKind_t kind;
	uint32_t	participantSequence;
	const char *label;
};

struct mpSeriesReportContestant {
	mpSeriesReportContestantKind_t kind;
	uint32_t	participantSequence;
	char		label[ MP_SERIES_REPORT_DISPLAY_NAME_BYTES + 1 ];
};

struct mpSeriesReportIdentityInput {
	uint64_t			seriesId;
	mpSeriesProfileId_t	profile;
	const char *		profileKey;
	int				bestOf;
	uint32_t			rulesSchema;
	uint32_t			rulesRevision;
	uint64_t			rulesDigest;
	int				gameType;
	const char *		modeToken;
	mpSeriesReportContestantInput contestants[ MP_SERIES_SIDE_COUNT ];
};

struct mpSeriesReportIdentity {
	uint64_t			seriesId;
	mpSeriesProfileId_t	profile;
	char				profileKey[ MP_SERIES_REPORT_PROFILE_KEY_BYTES + 1 ];
	int				bestOf;
	uint32_t			rulesSchema;
	uint32_t			rulesRevision;
	uint64_t			rulesDigest;
	int				gameType;
	char				modeToken[ MP_SERIES_REPORT_MODE_TOKEN_BYTES + 1 ];
	mpSeriesReportContestant contestants[ MP_SERIES_SIDE_COUNT ];
};

/*
===============================================================================

	Ordered map results and typed artifact references

===============================================================================
*/

typedef enum {
	MP_SERIES_REPORT_MAP_DECIDED = 0,
	MP_SERIES_REPORT_MAP_FORFEIT,
	MP_SERIES_REPORT_MAP_ABORTED,
	MP_SERIES_REPORT_MAP_DRAW,
	MP_SERIES_REPORT_MAP_OUTCOME_COUNT
} mpSeriesReportMapOutcome_t;

typedef enum {
	MP_SERIES_REPORT_ARTIFACT_EVIDENCE = 0,
	MP_SERIES_REPORT_ARTIFACT_MVD,
	MP_SERIES_REPORT_ARTIFACT_KIND_COUNT
} mpSeriesReportArtifactKind_t;

typedef enum {
	MP_SERIES_REPORT_ARTIFACT_NOT_REQUESTED = 0,
	MP_SERIES_REPORT_ARTIFACT_AVAILABLE,
	MP_SERIES_REPORT_ARTIFACT_FAILED,
	MP_SERIES_REPORT_ARTIFACT_DROPPED,
	MP_SERIES_REPORT_ARTIFACT_PENDING,
	MP_SERIES_REPORT_ARTIFACT_STATUS_COUNT
} mpSeriesReportArtifactStatus_t;

struct mpSeriesReportArtifactInput {
	mpSeriesReportArtifactStatus_t status;
	uint16_t				reason;
	const char *			qpath;
};

struct mpSeriesReportArtifact {
	mpSeriesReportArtifactStatus_t status;
	uint16_t				reason;
	char					qpath[ MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES + 1 ];
};

struct mpSeriesReportMapResultInput {
	uint32_t			attempt;	// stable, monotonically increasing series attempt
	uint64_t			sessionId;
	const char *		mapToken;
	uint64_t			rulesDigest;
	mpSeriesReportMapOutcome_t outcome;
	uint16_t			reason;
	int					winnerContestant;
	int32_t				score[ MP_SERIES_SIDE_COUNT ];
	mpSeriesReportArtifactInput artifacts[ MP_SERIES_REPORT_ARTIFACT_KIND_COUNT ];
};

struct mpSeriesReportMapResult {
	uint32_t			sequence;
	uint32_t			attempt;
	uint64_t			sessionId;
	char				mapToken[ MP_SERIES_MAP_TOKEN_BYTES ];
	uint64_t			rulesDigest;
	mpSeriesReportMapOutcome_t outcome;
	uint16_t			reason;
	int8_t				winnerContestant;
	int32_t				score[ MP_SERIES_SIDE_COUNT ];
	mpSeriesReportArtifact artifacts[ MP_SERIES_REPORT_ARTIFACT_KIND_COUNT ];
};

bool MPMatchSeriesReportIsSafeMapToken( const char *mapToken );
bool MPMatchSeriesReportIsSafeArtifactQPath( mpSeriesReportArtifactKind_t kind,
	const char *qpath );

/*
===============================================================================

	Optional aggregate statistics

===============================================================================
*/

struct mpSeriesReportParticipantStatsInput {
	uint32_t	participantSequence;
	int			contestant;
	const char *displayName;
	uint32_t	mapsPlayed;
	uint32_t	mapsWon;
	int64_t		score;
	uint64_t	kills;
	uint64_t	deaths;
	uint64_t	suicides;
	uint64_t	damageGiven;
	uint64_t	damageReceived;
	uint64_t	shots;
	uint64_t	hits;
};

struct mpSeriesReportParticipantStats {
	uint32_t	participantSequence;
	int8_t		contestant;
	char		displayName[ MP_SERIES_REPORT_DISPLAY_NAME_BYTES + 1 ];
	uint32_t	mapsPlayed;
	uint32_t	mapsWon;
	int64_t		score;
	uint64_t	kills;
	uint64_t	deaths;
	uint64_t	suicides;
	uint64_t	damageGiven;
	uint64_t	damageReceived;
	uint64_t	shots;
	uint64_t	hits;
};

struct mpSeriesReportTeamStatsInput {
	int			contestant;
	uint32_t	mapsPlayed;
	uint32_t	mapsWon;
	int64_t		score;
	uint64_t	objectives;
	uint64_t	roundsWon;
	uint64_t	damageGiven;
};

struct mpSeriesReportTeamStats {
	int8_t		contestant;
	uint32_t	mapsPlayed;
	uint32_t	mapsWon;
	int64_t		score;
	uint64_t	objectives;
	uint64_t	roundsWon;
	uint64_t	damageGiven;
};

/*
===============================================================================

	Exactly-once finalization

===============================================================================
*/

typedef enum {
	MP_SERIES_REPORT_AUTHORIZER_SYSTEM = 0,
	MP_SERIES_REPORT_AUTHORIZER_PARTICIPANT,
	MP_SERIES_REPORT_AUTHORIZER_SERVER_OPERATOR,
	MP_SERIES_REPORT_AUTHORIZER_KIND_COUNT
} mpSeriesReportAuthorizerKind_t;

struct mpSeriesReportAuthorizer {
	mpSeriesReportAuthorizerKind_t kind;
	uint32_t	participantSequence;
};

mpSeriesReportAuthorizer MPSeriesReportSystemAuthorizer( void );
mpSeriesReportAuthorizer MPSeriesReportParticipantAuthorizer(
	uint32_t participantSequence );
mpSeriesReportAuthorizer MPSeriesReportServerOperatorAuthorizer( void );

typedef enum {
	MP_SERIES_REPORT_FINAL_NONE = 0,
	MP_SERIES_REPORT_FINAL_COMPLETE,
	MP_SERIES_REPORT_FINAL_CANCELLED,
	MP_SERIES_REPORT_FINAL_OUTCOME_COUNT
} mpSeriesReportFinalOutcome_t;

struct mpSeriesReportFinalInput {
	mpSeriesReportFinalOutcome_t outcome;
	uint16_t				reason;
	int					winnerContestant;
	mpSeriesReportAuthorizer	authorizer;
};

struct mpSeriesReportFinal {
	mpSeriesReportFinalOutcome_t outcome;
	uint16_t				reason;
	int8_t				winnerContestant;
	mpSeriesReportAuthorizer	authorizer;
};

// Exact logical state used by the one-file series recovery checkpoint.  This
// is deliberately a typed state transfer object rather than a memory image:
// the recovery codec writes every field explicitly and RestoreCheckpointState
// revalidates the complete report before publishing it.  It is not the public
// final JSON artifact.
struct mpSeriesReportCheckpointState {
	uint16_t					schemaVersion;
	bool					initialized;
	uint64_t				reportRevision;
	mpSeriesReportIdentity	identity;
	mpSeriesReportMapResult	mapResults[ MP_SERIES_REPORT_MAX_MAP_RESULTS ];
	int					mapResultCount;
	mpSeriesReportParticipantStats participantStats[ MP_SERIES_REPORT_MAX_PARTICIPANTS ];
	int					participantStatsCount;
	mpSeriesReportTeamStats	teamStats[ MP_SERIES_REPORT_MAX_TEAMS ];
	int					teamStatsCount;
	uint64_t				droppedMapResultCount;
	uint64_t				droppedParticipantStatsCount;
	uint64_t				droppedTeamStatsCount;
	bool					dropCounterSaturated;
	mpSeriesReportFinal		finalResult;

	void					Clear( void );
};

/*
===============================================================================

	Mutation and serialization results

===============================================================================
*/

typedef enum {
	MP_SERIES_REPORT_WRITE_ACCEPTED = 0,
	MP_SERIES_REPORT_WRITE_NO_CHANGE,
	MP_SERIES_REPORT_WRITE_DROPPED,
	MP_SERIES_REPORT_WRITE_REJECTED,
	MP_SERIES_REPORT_WRITE_CODE_COUNT
} mpSeriesReportWriteCode_t;

typedef enum {
	MP_SERIES_REPORT_REASON_NONE = 0,
	MP_SERIES_REPORT_REASON_NOT_INITIALIZED,
	MP_SERIES_REPORT_REASON_INVALID_ARGUMENT,
	MP_SERIES_REPORT_REASON_INVALID_TEXT,
	MP_SERIES_REPORT_REASON_INVALID_PROFILE,
	MP_SERIES_REPORT_REASON_INVALID_BEST_OF,
	MP_SERIES_REPORT_REASON_INVALID_RULES_IDENTITY,
	MP_SERIES_REPORT_REASON_IDENTITY_CONFLICT,
	MP_SERIES_REPORT_REASON_FINALIZED,
	MP_SERIES_REPORT_REASON_REVISION_EXHAUSTED,
	MP_SERIES_REPORT_REASON_INVALID_MAP_RESULT,
	MP_SERIES_REPORT_REASON_INVALID_MAP_ORDER,
	MP_SERIES_REPORT_REASON_MAP_ATTEMPT_CONFLICT,
	MP_SERIES_REPORT_REASON_MAP_RESULT_CAPACITY,
	MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_STATUS,
	MP_SERIES_REPORT_REASON_INVALID_ARTIFACT_QPATH,
	MP_SERIES_REPORT_REASON_INVALID_PARTICIPANT_STATS,
	MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CONFLICT,
	MP_SERIES_REPORT_REASON_PARTICIPANT_STATS_CAPACITY,
	MP_SERIES_REPORT_REASON_INVALID_TEAM_STATS,
	MP_SERIES_REPORT_REASON_TEAM_STATS_CONFLICT,
	MP_SERIES_REPORT_REASON_TEAM_STATS_CAPACITY,
	MP_SERIES_REPORT_REASON_INVALID_FINAL_OUTCOME,
	MP_SERIES_REPORT_REASON_FINALIZATION_CONFLICT,
	MP_SERIES_REPORT_REASON_ARTIFACT_RECONCILIATION_CONFLICT,
	MP_SERIES_REPORT_REASON_COUNT
} mpSeriesReportReason_t;

struct mpSeriesReportWriteResult {
	mpSeriesReportWriteCode_t code;
	mpSeriesReportReason_t	reason;
	uint64_t				previousRevision;
	uint64_t				currentRevision;

	bool WasAccepted( void ) const;
	bool WasDropped( void ) const;
};

typedef enum {
	MP_SERIES_REPORT_SERIALIZE_SUCCESS = 0,
	MP_SERIES_REPORT_SERIALIZE_BUFFER_TOO_SMALL,
	MP_SERIES_REPORT_SERIALIZE_INVALID_ARGUMENT,
	MP_SERIES_REPORT_SERIALIZE_INVALID_STATE,
	MP_SERIES_REPORT_SERIALIZE_OUTPUT_TOO_LARGE,
	MP_SERIES_REPORT_SERIALIZE_CODE_COUNT
} mpSeriesReportSerializeCode_t;

struct mpSeriesReportSerializeResult {
	mpSeriesReportSerializeCode_t code;
	int					bytesWritten;		// excludes the terminating zero
	int					requiredCapacity;	// includes the terminating zero

	bool Succeeded( void ) const;
};

/*
===============================================================================

	mpCompetitionSeriesReport

===============================================================================
*/

class mpCompetitionSeriesReport {
public:
						mpCompetitionSeriesReport( void );

	void			Clear( void );
	// Identity is installed once.  An exact replay is a no-op; conflicting
	// identity is rejected without replacing the report.
	mpSeriesReportWriteResult Initialize( const mpSeriesReportIdentityInput &input );
	bool			IsInitialized( void ) const;
	bool			IsFinalized( void ) const;
	uint64_t		GetReportRevision( void ) const;
	const mpSeriesReportIdentity &GetIdentity( void ) const;

	mpSeriesReportWriteResult AppendMapResult(
		const mpSeriesReportMapResultInput &input );
	// A pending artifact may advance monotonically to available/failed, or
	// retain its qpath while its pending reason is refined at series seal.
	// Terminal rows and finalized reports are immutable.
	mpSeriesReportWriteResult ReconcileMapArtifact( uint32_t attempt,
		mpSeriesReportArtifactKind_t kind,
		const mpSeriesReportArtifactInput &input );
	int				GetMapResultCount( void ) const;
	const mpSeriesReportMapResult *GetMapResult( int index ) const;

	mpSeriesReportWriteResult RecordParticipantStats(
		const mpSeriesReportParticipantStatsInput &input );
	mpSeriesReportWriteResult RecordTeamStats(
		const mpSeriesReportTeamStatsInput &input );
	// Adds a per-map delta to an existing aggregate, or creates the aggregate
	// when this is the participant/team's first completed map.  Callers must
	// invoke these only when the corresponding AppendMapResult was accepted;
	// an exact map replay is a no-op and must not count its stats twice.
	mpSeriesReportWriteResult AccumulateParticipantStats(
		const mpSeriesReportParticipantStatsInput &delta );
	mpSeriesReportWriteResult AccumulateTeamStats(
		const mpSeriesReportTeamStatsInput &delta );
	int				GetParticipantStatsCount( void ) const;
	const mpSeriesReportParticipantStats *GetParticipantStats( int index ) const;
	int				GetTeamStatsCount( void ) const;
	const mpSeriesReportTeamStats *GetTeamStats( int index ) const;

	uint64_t		GetDroppedMapResultCount( void ) const;
	uint64_t		GetDroppedParticipantStatsCount( void ) const;
	uint64_t		GetDroppedTeamStatsCount( void ) const;
	bool			IsDropCounterSaturated( void ) const;

	mpSeriesReportWriteResult Finalize( const mpSeriesReportFinalInput &input );
	const mpSeriesReportFinal &GetFinal( void ) const;

	// Only a finalized report can be serialized.  Two deterministic passes
	// prove the complete output size before touching the caller's buffer.  On
	// every failure the caller's buffer remains byte-for-byte unchanged.
	mpSeriesReportSerializeResult SerializeCanonicalJson( char *buffer,
		int capacity ) const;

	// Transactional state transfer for the unified mutable recovery record.
	// Export refuses invalid/uninitialized state.  Restore leaves this object
	// byte-for-byte unchanged on every failure.
	bool			ExportCheckpointState(
		mpSeriesReportCheckpointState &state ) const;
	bool			RestoreCheckpointState(
		const mpSeriesReportCheckpointState &state );

	bool			ValidateInvariants( void ) const;

private:
	bool			CanMutate( void ) const;
	mpSeriesReportWriteResult CommitAccepted( void );
	mpSeriesReportWriteResult Accepted( uint64_t previousRevision );
	mpSeriesReportWriteResult NoChange( mpSeriesReportReason_t reason ) const;
	mpSeriesReportWriteResult Rejected( mpSeriesReportReason_t reason ) const;
	mpSeriesReportWriteResult RecordDrop( mpSeriesReportReason_t reason,
		uint64_t &counter );
	int				FindMapAttempt( uint32_t attempt ) const;
	int				FindParticipantStats( uint32_t participantSequence ) const;
	int				FindTeamStats( int contestant ) const;

	bool			initialized;
	uint64_t		reportRevision;
	mpSeriesReportIdentity identity;
	mpSeriesReportMapResult mapResults[ MP_SERIES_REPORT_MAX_MAP_RESULTS ];
	int				mapResultCount;
	mpSeriesReportParticipantStats participantStats[ MP_SERIES_REPORT_MAX_PARTICIPANTS ];
	int				participantStatsCount;
	mpSeriesReportTeamStats teamStats[ MP_SERIES_REPORT_MAX_TEAMS ];
	int				teamStatsCount;
	uint64_t		droppedMapResultCount;
	uint64_t		droppedParticipantStatsCount;
	uint64_t		droppedTeamStatsCount;
	bool			dropCounterSaturated;
	mpSeriesReportFinal finalResult;
};

#endif // __MP_MATCH_SERIES_REPORT_H__
