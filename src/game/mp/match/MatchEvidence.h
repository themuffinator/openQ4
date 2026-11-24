//----------------------------------------------------------------
// MatchEvidence.h
//
// Fixed-capacity competitive match evidence values.  This core owns no game
// pointers and performs no file, network, GUI, command, recorder, or cvar I/O.
//----------------------------------------------------------------

#ifndef __MP_MATCH_EVIDENCE_H__
#define __MP_MATCH_EVIDENCE_H__

#include "../MatchPhase.h"

#include <stdint.h>
#include <stddef.h>

static const uint32_t MP_MATCH_EVIDENCE_SCHEMA_VERSION = 2;
static const int MP_MATCH_EVIDENCE_MAX_EVENTS = 256;
static const int MP_MATCH_EVIDENCE_MAX_PARTICIPANTS = 32;
static const int MP_MATCH_EVIDENCE_MAX_TEAMS = 2;
static const int MP_MATCH_EVIDENCE_MAX_BUILD_BYTES = 48;
static const int MP_MATCH_EVIDENCE_MAX_MAP_BYTES = 96;
static const int MP_MATCH_EVIDENCE_MAX_MODE_BYTES = 32;
static const int MP_MATCH_EVIDENCE_MAX_DISPLAY_NAME_BYTES = 64;
static const int MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES = 160;
static const uint8_t MP_MATCH_EVIDENCE_NO_ROSTER_SEAT = 255;

/*
===============================================================================

	Artifact metadata

===============================================================================
*/

struct mpEvidenceMetadataInput {
	uint64_t		sessionId;
	uint64_t		seriesId;		// zero when the map is not part of a series
	uint64_t		rulesDigest;
	uint32_t		modeId;
	const char *	build;
	const char *	map;
	const char *	mode;
};

struct mpEvidenceMetadata {
	uint32_t	schemaVersion;
	uint64_t	sessionId;
	uint64_t	seriesId;
	uint64_t	rulesDigest;
	uint32_t	modeId;
	char		build[ MP_MATCH_EVIDENCE_MAX_BUILD_BYTES + 1 ];
	char		map[ MP_MATCH_EVIDENCE_MAX_MAP_BYTES + 1 ];
	char		mode[ MP_MATCH_EVIDENCE_MAX_MODE_BYTES + 1 ];
};

// Append only: artifact-kind values are emitted in structured artifacts.
typedef enum {
	MP_EVIDENCE_ARTIFACT_INVALID = 0,
	MP_EVIDENCE_ARTIFACT_MVD,
	MP_EVIDENCE_ARTIFACT_KIND_COUNT
} mpEvidenceArtifactKind_t;

static const int MP_MATCH_EVIDENCE_MAX_ARTIFACTS =
	MP_EVIDENCE_ARTIFACT_KIND_COUNT - 1;

struct mpEvidenceArtifactLinkInput {
	mpEvidenceArtifactKind_t kind;
	const char *			qpath;
};

struct mpEvidenceArtifactLink {
	uint64_t				sessionRevision;
	mpEvidenceArtifactKind_t kind;
	char					qpath[ MP_MATCH_EVIDENCE_MAX_ARTIFACT_QPATH_BYTES + 1 ];
};

// Artifact links are informational qpaths, but they are still restricted to
// the engine's portable, relative namespace.  MVD links additionally require
// the canonical demos/ root and .mvd extension.
bool MPMatchEvidenceIsSafeArtifactQPath( mpEvidenceArtifactKind_t kind,
	const char *qpath );

// A stamp is supplied only after the authoritative operation/outcome has
// committed.  Several evidence records may share one session revision.
struct mpEvidenceCommittedStamp {
	uint64_t	sessionRevision;
	uint64_t	matchTimeMsec;
	uint64_t	hostTimeUtcMsec;
};

typedef enum {
	MP_EVIDENCE_ACTOR_SYSTEM = 0,
	MP_EVIDENCE_ACTOR_PARTICIPANT,
	MP_EVIDENCE_ACTOR_SERVER_OPERATOR,
	MP_EVIDENCE_ACTOR_KIND_COUNT
} mpEvidenceActorKind_t;

struct mpEvidenceActorRef {
	mpEvidenceActorKind_t kind;
	uint32_t			participantSequence;
};

mpEvidenceActorRef MPEvidenceSystemActor( void );
mpEvidenceActorRef MPEvidenceParticipantActor( uint32_t participantSequence );
mpEvidenceActorRef MPEvidenceServerOperatorActor( void );

/*
===============================================================================

	Typed journal events

===============================================================================
*/

// Append only: event-kind values are emitted in structured artifacts.
typedef enum {
	MP_EVIDENCE_EVENT_INVALID = 0,
	MP_EVIDENCE_EVENT_PHASE_TRANSITION,
	MP_EVIDENCE_EVENT_ROUND_TRANSITION,
	MP_EVIDENCE_EVENT_PAUSE_TRANSITION,
	MP_EVIDENCE_EVENT_ROLE_CHANGE,
	MP_EVIDENCE_EVENT_PROPOSAL,
	MP_EVIDENCE_EVENT_ROSTER_CHANGE,
	MP_EVIDENCE_EVENT_MAP_RESULT,
	MP_EVIDENCE_EVENT_OUTPUT_FAILURE,
	MP_EVIDENCE_EVENT_KIND_COUNT
} mpEvidenceEventKind_t;

struct mpEvidencePhaseTransition {
	mpGameState_t		from;
	mpGameState_t		to;
	uint16_t			reason;
	mpEvidenceActorRef actor;
};

struct mpEvidenceRoundTransition {
	roundState_t	from;
	roundState_t	to;
	uint16_t		reason;
};

typedef enum {
	MP_EVIDENCE_PAUSE_RUNNING = 0,
	MP_EVIDENCE_PAUSE_PENDING,
	MP_EVIDENCE_PAUSED,
	MP_EVIDENCE_RESUME_COUNTDOWN,
	MP_EVIDENCE_PAUSE_STATE_COUNT
} mpEvidencePauseState_t;

typedef enum {
	MP_EVIDENCE_PAUSE_NONE = 0,
	MP_EVIDENCE_PAUSE_TEAM_TIMEOUT,
	MP_EVIDENCE_PAUSE_TECHNICAL,
	MP_EVIDENCE_PAUSE_KIND_COUNT
} mpEvidencePauseKind_t;

struct mpEvidencePauseTransition {
	mpEvidencePauseState_t from;
	mpEvidencePauseState_t to;
	mpEvidencePauseKind_t kind;
	int8_t			ownerSide;	// -1 when the pause has no team owner
	uint16_t		reason;
	mpEvidenceActorRef actor;
};

struct mpEvidenceRoleChange {
	uint32_t			targetParticipant;
	uint64_t			previousRoles;
	uint64_t			currentRoles;
	mpEvidenceActorRef	authorizer;
};

typedef enum {
	MP_EVIDENCE_PROPOSAL_CREATED = 0,
	MP_EVIDENCE_PROPOSAL_BALLOT_CAST,
	MP_EVIDENCE_PROPOSAL_PASSED,
	MP_EVIDENCE_PROPOSAL_FAILED,
	MP_EVIDENCE_PROPOSAL_CANCELLED,
	MP_EVIDENCE_PROPOSAL_EXPIRED,
	MP_EVIDENCE_PROPOSAL_ACTION_COUNT
} mpEvidenceProposalAction_t;

struct mpEvidenceProposalEvent {
	uint64_t			proposalId;
	mpEvidenceProposalAction_t action;
	uint16_t			opcode;
	int8_t				scopeSide;	// -1 is global
	uint32_t			targetParticipant;
	mpEvidenceActorRef	actor;
};

typedef enum {
	MP_EVIDENCE_ROSTER_SEAT_DECLARED = 0,
	MP_EVIDENCE_ROSTER_SEAT_CLEARED,
	MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED,
	MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED,
	MP_EVIDENCE_ROSTER_SUBSTITUTED,
	MP_EVIDENCE_ROSTER_LOCK_CHANGED,
	MP_EVIDENCE_ROSTER_ACTION_COUNT
} mpEvidenceRosterAction_t;

typedef enum {
	MP_EVIDENCE_ROSTER_PLAYER = 0,
	MP_EVIDENCE_ROSTER_CAPTAIN,
	MP_EVIDENCE_ROSTER_COACH,
	MP_EVIDENCE_ROSTER_SUBSTITUTE,
	MP_EVIDENCE_ROSTER_ROLE_COUNT
} mpEvidenceRosterRole_t;

struct mpEvidenceRosterEvent {
	mpEvidenceRosterAction_t action;
	uint8_t			seat;
	int8_t				side;
	mpEvidenceRosterRole_t role;
	uint32_t			participant;
	uint32_t			replacementParticipant;
	bool				locked;
	mpEvidenceActorRef	authorizer;
};

typedef enum {
	MP_EVIDENCE_RESULT_DECIDED = 0,
	MP_EVIDENCE_RESULT_FORFEIT,
	MP_EVIDENCE_RESULT_ABORTED,
	MP_EVIDENCE_RESULT_DRAW,
	MP_EVIDENCE_RESULT_OUTCOME_COUNT
} mpEvidenceResultOutcome_t;

struct mpEvidenceMapResult {
	mpEvidenceResultOutcome_t outcome;
	int8_t				winnerSide;
	uint32_t			winnerParticipant;
	int32_t				sideScore[ MP_MATCH_EVIDENCE_MAX_TEAMS ];
	uint16_t			reason;
	mpEvidenceActorRef	authorizer;
};

typedef enum {
	MP_EVIDENCE_OUTPUT_MVD_START = 0,
	MP_EVIDENCE_OUTPUT_MVD_STOP,
	MP_EVIDENCE_OUTPUT_MAP_ARTIFACT,
	MP_EVIDENCE_OUTPUT_SERIES_RECOVERY,
	MP_EVIDENCE_OUTPUT_SERIES_REPORT,
	MP_EVIDENCE_OUTPUT_KIND_COUNT
} mpEvidenceOutputKind_t;

// Output failures carry a stable numeric adapter reason only.  Arbitrary
// backend text and paths never enter the evidence core.
struct mpEvidenceOutputFailure {
	mpEvidenceOutputKind_t output;
	uint16_t				reason;
};

union mpEvidenceEventData {
	mpEvidencePhaseTransition	phase;
	mpEvidenceRoundTransition	round;
	mpEvidencePauseTransition	pause;
	mpEvidenceRoleChange			role;
	mpEvidenceProposalEvent		proposal;
	mpEvidenceRosterEvent			roster;
	mpEvidenceMapResult			result;
	mpEvidenceOutputFailure		outputFailure;
};

struct mpEvidenceEvent {
	uint64_t				sequence;
	mpEvidenceCommittedStamp	stamp;
	mpEvidenceEventKind_t		kind;
	mpEvidenceEventData		data;
};

/*
===============================================================================

	Bounded final statistics

===============================================================================
*/

struct mpEvidenceParticipantStatsInput {
	uint32_t	participantSequence;
	int8_t		side;
	const char *displayName;
	int32_t		score;
	uint32_t	kills;
	uint32_t	deaths;
	uint32_t	suicides;
	uint32_t	damageGiven;
	uint32_t	damageReceived;
	uint32_t	shots;
	uint32_t	hits;
};

struct mpEvidenceParticipantFinalStats {
	uint64_t	sessionRevision;
	uint32_t	participantSequence;
	int8_t		side;
	char		displayName[ MP_MATCH_EVIDENCE_MAX_DISPLAY_NAME_BYTES + 1 ];
	int32_t		score;
	uint32_t	kills;
	uint32_t	deaths;
	uint32_t	suicides;
	uint32_t	damageGiven;
	uint32_t	damageReceived;
	uint32_t	shots;
	uint32_t	hits;
};

struct mpEvidenceTeamFinalStats {
	uint64_t	sessionRevision;
	int8_t		side;
	int32_t		score;
	uint32_t	objectives;
	uint32_t	roundsWon;
	uint32_t	damageGiven;
};

/*
===============================================================================

	Stable mutation and serialization results

===============================================================================
*/

typedef enum {
	MP_EVIDENCE_WRITE_ACCEPTED = 0,
	MP_EVIDENCE_WRITE_NO_CHANGE,
	MP_EVIDENCE_WRITE_DROPPED,
	MP_EVIDENCE_WRITE_REJECTED,
	MP_EVIDENCE_WRITE_CODE_COUNT
} mpEvidenceWriteCode_t;

typedef enum {
	MP_EVIDENCE_REASON_NONE = 0,
	MP_EVIDENCE_REASON_NOT_INITIALIZED,
	MP_EVIDENCE_REASON_INVALID_ARGUMENT,
	MP_EVIDENCE_REASON_INVALID_TEXT,
	MP_EVIDENCE_REASON_SESSION_REVISION_REGRESSION,
	MP_EVIDENCE_REASON_EVIDENCE_REVISION_EXHAUSTED,
	MP_EVIDENCE_REASON_EVENT_SEQUENCE_EXHAUSTED,
	MP_EVIDENCE_REASON_EVENT_CAPACITY,
	MP_EVIDENCE_REASON_PARTICIPANT_STATS_CAPACITY,
	MP_EVIDENCE_REASON_TEAM_STATS_CAPACITY,
	MP_EVIDENCE_REASON_DUPLICATE_PARTICIPANT_STATS,
	MP_EVIDENCE_REASON_DUPLICATE_TEAM_STATS,
	MP_EVIDENCE_REASON_SERIES_ID_CONFLICT,
	MP_EVIDENCE_REASON_INVALID_ARTIFACT_QPATH,
	MP_EVIDENCE_REASON_ARTIFACT_CONFLICT,
	MP_EVIDENCE_REASON_COUNT
} mpEvidenceReason_t;

struct mpEvidenceWriteResult {
	mpEvidenceWriteCode_t code;
	mpEvidenceReason_t	reason;
	uint64_t			previousEvidenceRevision;
	uint64_t			currentEvidenceRevision;

	bool WasAccepted( void ) const;
	bool WasDropped( void ) const;
};

typedef enum {
	MP_EVIDENCE_SERIALIZE_SUCCESS = 0,
	MP_EVIDENCE_SERIALIZE_BUFFER_TOO_SMALL,
	MP_EVIDENCE_SERIALIZE_INVALID_ARGUMENT,
	MP_EVIDENCE_SERIALIZE_INVALID_STATE,
	MP_EVIDENCE_SERIALIZE_OUTPUT_TOO_LARGE,
	MP_EVIDENCE_SERIALIZE_CODE_COUNT
} mpEvidenceSerializeCode_t;

struct mpEvidenceSerializeResult {
	mpEvidenceSerializeCode_t code;
	int					bytesWritten;		// excludes the terminating zero
	int					requiredCapacity;	// includes the terminating zero

	bool Succeeded( void ) const;
};

/*
===============================================================================

	mpMatchEvidence

===============================================================================
*/

class mpMatchEvidence {
public:
							mpMatchEvidence( void );
	void					Clear( void );

	// Invalid metadata leaves the existing artifact untouched.
	bool					Reset( const mpEvidenceMetadataInput &input );
	bool					IsInitialized( void ) const;
	const mpEvidenceMetadata &GetMetadata( void ) const;
	uint64_t				GetEvidenceRevision( void ) const;
	uint64_t				GetLastSessionRevision( void ) const;
	uint64_t				GetSeriesLinkSessionRevision( void ) const;

	// Reset may seed a known series id.  This enrichment seam covers the common
	// case where the durable series identity commits later.  Identity and
	// artifact links are immutable once non-zero/present: exact replays are
	// no-ops and conflicting replays are rejected without partial mutation.
	mpEvidenceWriteResult	LinkSeriesId( const mpEvidenceCommittedStamp &stamp,
								uint64_t seriesId );
	mpEvidenceWriteResult	LinkArtifact( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceArtifactLinkInput &artifact );
	int					GetArtifactCount( void ) const;
	const mpEvidenceArtifactLink *GetArtifact( int index ) const;

	mpEvidenceWriteResult	AppendPhaseTransition( const mpEvidenceCommittedStamp &stamp,
								const mpEvidencePhaseTransition &event );
	mpEvidenceWriteResult	AppendRoundTransition( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceRoundTransition &event );
	mpEvidenceWriteResult	AppendPauseTransition( const mpEvidenceCommittedStamp &stamp,
								const mpEvidencePauseTransition &event );
	mpEvidenceWriteResult	AppendRoleChange( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceRoleChange &event );
	mpEvidenceWriteResult	AppendProposal( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceProposalEvent &event );
	mpEvidenceWriteResult	AppendRosterChange( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceRosterEvent &event );
	mpEvidenceWriteResult	AppendMapResult( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceMapResult &event );
	mpEvidenceWriteResult	AppendOutputFailure( const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceOutputFailure &event );

	int					GetEventCount( void ) const;
	const mpEvidenceEvent *GetEvent( int index ) const;
	uint64_t				GetDroppedEventCount( void ) const;
	uint64_t				GetFirstDroppedSessionRevision( void ) const;
	uint64_t				GetLastDroppedSessionRevision( void ) const;
	bool					IsDropCounterSaturated( void ) const;

	mpEvidenceWriteResult	RecordParticipantFinalStats(
								const mpEvidenceCommittedStamp &stamp,
								const mpEvidenceParticipantStatsInput &stats );
	mpEvidenceWriteResult	RecordTeamFinalStats( const mpEvidenceCommittedStamp &stamp,
								int side, int score, uint32_t objectives,
								uint32_t roundsWon, uint32_t damageGiven );
	int					GetParticipantStatsCount( void ) const;
	const mpEvidenceParticipantFinalStats *GetParticipantStats( int index ) const;
	int					GetTeamStatsCount( void ) const;
	const mpEvidenceTeamFinalStats *GetTeamStats( int index ) const;
	uint64_t				GetDroppedParticipantStatsCount( void ) const;
	uint64_t				GetDroppedTeamStatsCount( void ) const;

	// Two-pass serialization computes and validates the entire artifact before
	// touching output.  On every failure the caller's buffer remains unchanged.
	mpEvidenceSerializeResult SerializeCanonicalJson( char *buffer, int capacity ) const;

	bool					ValidateInvariants( void ) const;

private:
	bool					ValidateStamp( const mpEvidenceCommittedStamp &stamp ) const;
	bool					CanMutate( void ) const;
	mpEvidenceWriteResult	AppendValidatedEvent( const mpEvidenceCommittedStamp &stamp,
								mpEvidenceEventKind_t kind,
								const mpEvidenceEventData &data );
	mpEvidenceWriteResult	RecordDrop( const mpEvidenceCommittedStamp &stamp,
								mpEvidenceReason_t reason, uint64_t &counter );
	mpEvidenceWriteResult	Accepted( uint64_t previousRevision );
	mpEvidenceWriteResult	NoChange( mpEvidenceReason_t reason ) const;
	mpEvidenceWriteResult	Rejected( mpEvidenceReason_t reason ) const;
	int					FindParticipantStats( uint32_t participantSequence ) const;
	int					FindTeamStats( int side ) const;
	int					FindArtifact( mpEvidenceArtifactKind_t kind ) const;

	bool					initialized;
	mpEvidenceMetadata		metadata;
	uint64_t				evidenceRevision;
	uint64_t				lastSessionRevision;
	uint64_t				nextEventSequence;
	uint64_t				seriesLinkSessionRevision;

	mpEvidenceArtifactLink artifacts[ MP_MATCH_EVIDENCE_MAX_ARTIFACTS ];
	int					artifactCount;

	mpEvidenceEvent		events[ MP_MATCH_EVIDENCE_MAX_EVENTS ];
	int					eventCount;
	uint64_t				droppedEventCount;
	uint64_t				firstDroppedSessionRevision;
	uint64_t				lastDroppedSessionRevision;
	bool					dropCounterSaturated;

	mpEvidenceParticipantFinalStats participantStats[ MP_MATCH_EVIDENCE_MAX_PARTICIPANTS ];
	int					participantStatsCount;
	uint64_t				droppedParticipantStatsCount;
	mpEvidenceTeamFinalStats teamStats[ MP_MATCH_EVIDENCE_MAX_TEAMS ];
	int					teamStatsCount;
	uint64_t				droppedTeamStatsCount;
};

#endif // __MP_MATCH_EVIDENCE_H__
