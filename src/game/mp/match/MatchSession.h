//----------------------------------------------------------------
// MatchSession.h
//
// Authoritative, allocation-free multiplayer match-session values.
//
// This layer deliberately has no dependency on GUI state, network messages,
// cvars, files, or idMultiplayerGame.  Integrators translate their inputs to
// these typed operations and apply an accepted phase transition to rvGameState
// separately.
//----------------------------------------------------------------

#ifndef __MP_MATCH_SESSION_H__
#define __MP_MATCH_SESSION_H__

#include "../MatchPhase.h"

#include <stdint.h>
#include <stddef.h>

// Protocol/storage ceiling.  A platform may expose fewer live slots; the
// integration layer uses its actual MAX_CLIENTS when binding connections.
static const int MP_MATCH_MAX_CONNECTION_SLOTS = 32;
static const int MP_MATCH_MAX_PARTICIPANTS = 32;
static const int MP_MATCH_MAX_ROSTER_SEATS = 32;
static const int MP_MATCH_SIDE_NONE = -1;
static const int MP_MATCH_SIDE_COUNT = 2;
static const int MP_MATCH_MAX_TIMEOUTS_PER_SIDE = 32;
static const int MP_MATCH_MAX_TIMEOUT_MSEC = 60 * 60 * 1000;
static const int MP_MATCH_MAX_RESUME_COUNTDOWN_MSEC = 60 * 1000;
static const int MP_MATCH_MAX_LIVE_PERIOD_MSEC = 24 * 60 * 60 * 1000;

/*
===============================================================================

	Strong clock-domain values

	The constructors are intentionally named.  Code cannot accidentally pass a
	match timestamp where an engine/network or host timestamp is required.

===============================================================================
*/

typedef enum {
	MP_MATCH_CLOCK_ENGINE = 0,
	MP_MATCH_CLOCK_MATCH,
	MP_MATCH_CLOCK_HOST,
	MP_MATCH_CLOCK_DOMAIN_COUNT
} mpMatchClockDomain_t;

class mpMatchEngineTime {
public:
					mpMatchEngineTime( void );

	static mpMatchEngineTime FromMilliseconds( int64_t value );
	int64_t		Milliseconds( void ) const;
	bool			IsValid( void ) const;

	bool			operator==( const mpMatchEngineTime &rhs ) const;
	bool			operator!=( const mpMatchEngineTime &rhs ) const;
	bool			operator<( const mpMatchEngineTime &rhs ) const;
	bool			operator<=( const mpMatchEngineTime &rhs ) const;
	bool			operator>( const mpMatchEngineTime &rhs ) const;
	bool			operator>=( const mpMatchEngineTime &rhs ) const;

private:
	explicit		mpMatchEngineTime( int64_t value );
	int64_t		msec;
};

class mpMatchTime {
public:
					mpMatchTime( void );

	static mpMatchTime FromMilliseconds( int64_t value );
	int64_t		Milliseconds( void ) const;
	bool			IsValid( void ) const;

	bool			operator==( const mpMatchTime &rhs ) const;
	bool			operator!=( const mpMatchTime &rhs ) const;
	bool			operator<( const mpMatchTime &rhs ) const;
	bool			operator<=( const mpMatchTime &rhs ) const;
	bool			operator>( const mpMatchTime &rhs ) const;
	bool			operator>=( const mpMatchTime &rhs ) const;

private:
	explicit		mpMatchTime( int64_t value );
	int64_t		msec;
};

class mpMatchHostTime {
public:
					mpMatchHostTime( void );

	static mpMatchHostTime FromMilliseconds( int64_t value );
	int64_t		Milliseconds( void ) const;
	bool			IsValid( void ) const;

	bool			operator==( const mpMatchHostTime &rhs ) const;
	bool			operator!=( const mpMatchHostTime &rhs ) const;
	bool			operator<( const mpMatchHostTime &rhs ) const;
	bool			operator<=( const mpMatchHostTime &rhs ) const;
	bool			operator>( const mpMatchHostTime &rhs ) const;
	bool			operator>=( const mpMatchHostTime &rhs ) const;

private:
	explicit		mpMatchHostTime( int64_t value );
	int64_t		msec;
};

/*
===============================================================================

	Stable identity, result and reason values

	ParticipantId is connection/session scoped.  Only mpMatchSession can mint an
	id; client slots, names, addresses and userinfo are never identity.

===============================================================================
*/

class mpParticipantId {
public:
					mpParticipantId( void );

	static mpParticipantId Invalid( void );
	bool			IsValid( void ) const;
	uint64_t	SessionPart( void ) const;
	uint32_t	SequencePart( void ) const;

	bool			operator==( const mpParticipantId &rhs ) const;
	bool			operator!=( const mpParticipantId &rhs ) const;

private:
	friend class mpMatchSession;

					mpParticipantId( uint64_t sessionPart, uint32_t sequencePart );
	uint64_t	session;
	uint32_t	sequence;
};

// Append only.  These values are persisted in evidence and operation results.
typedef enum {
	MP_MATCH_MUTATION_APPLIED = 0,
	MP_MATCH_MUTATION_NO_CHANGE,
	MP_MATCH_MUTATION_REJECTED,
	MP_MATCH_MUTATION_CODE_COUNT
} mpMatchMutationCode_t;

// Append only.  Presentation maps these stable reasons to localized strings.
typedef enum {
	MP_MATCH_REASON_NONE = 0,
	MP_MATCH_REASON_INVALID_ARGUMENT,
	MP_MATCH_REASON_OUT_OF_RANGE,
	MP_MATCH_REASON_STALE_REVISION,
	MP_MATCH_REASON_REVISION_EXHAUSTED,
	MP_MATCH_REASON_ILLEGAL_PHASE_TRANSITION,
	MP_MATCH_REASON_WRONG_PHASE,
	MP_MATCH_REASON_RULES_NOT_FROZEN,
	MP_MATCH_REASON_READINESS_BLOCKED,
	MP_MATCH_REASON_CLOCK_REGRESSION,
	MP_MATCH_REASON_CLOCK_OVERFLOW,
	MP_MATCH_REASON_SLOT_OCCUPIED,
	MP_MATCH_REASON_SLOT_EMPTY,
	MP_MATCH_REASON_SLOT_GENERATION_STALE,
	MP_MATCH_REASON_PARTICIPANT_CAPACITY,
	MP_MATCH_REASON_PARTICIPANT_ID_EXHAUSTED,
	MP_MATCH_REASON_PARTICIPANT_UNKNOWN,
	MP_MATCH_REASON_PARTICIPANT_ALREADY_ROSTERED,
	MP_MATCH_REASON_INVALID_ROLE,
	MP_MATCH_REASON_INVALID_SIDE,
	MP_MATCH_REASON_ROSTER_SEAT_OCCUPIED,
	MP_MATCH_REASON_ROSTER_SEAT_EMPTY,
	MP_MATCH_REASON_ROSTER_SEAT_UNDECLARED,
	MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE,
	MP_MATCH_REASON_PAUSE_NOT_ACTIVE,
	MP_MATCH_REASON_INVALID_PAUSE_KIND,
	MP_MATCH_REASON_TIMEOUT_UNAVAILABLE,
	MP_MATCH_REASON_TIMEOUT_WINDOW_CLOSED,
	MP_MATCH_REASON_TIMEOUT_ALREADY_CHARGED,
	MP_MATCH_REASON_RESUME_NOT_PERMITTED,
	MP_MATCH_REASON_RESUME_ALREADY_REQUESTED,
	MP_MATCH_REASON_ILLEGAL_ROUND_TRANSITION,
	MP_MATCH_REASON_LIVE_PERIOD_MISMATCH,
	MP_MATCH_REASON_LIVE_PERIOD_NOT_EXPIRED,
	MP_MATCH_REASON_LIVE_PERIOD_EXHAUSTED,
	MP_MATCH_REASON_COUNT
} mpMatchReason_t;

struct mpMatchMutationResult {
	mpMatchMutationCode_t	code;
	mpMatchReason_t		reason;
	uint64_t			previousRevision;
	uint64_t			currentRevision;

	bool				WasApplied( void ) const;
	bool				WasRejected( void ) const;
};

/*
===============================================================================

	Lifecycle and pause overlay

===============================================================================
*/

// Append only; evidence stores the numeric value.
typedef enum {
	MP_MATCH_TRANSITION_NONE = 0,
	MP_MATCH_TRANSITION_SESSION_INITIALIZED,
	MP_MATCH_TRANSITION_READY_GATE,
	MP_MATCH_TRANSITION_REFEREE_FORCE_READY,
	MP_MATCH_TRANSITION_COUNTDOWN_ABORTED,
	MP_MATCH_TRANSITION_RULES_INVALIDATED,
	MP_MATCH_TRANSITION_ROSTER_INVALIDATED,
	MP_MATCH_TRANSITION_REQUIRED_PLAYER_LOST,
	MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE,
	MP_MATCH_TRANSITION_REGULATION_TIE,
	MP_MATCH_TRANSITION_LIMIT_REACHED,
	MP_MATCH_TRANSITION_FORFEIT,
	MP_MATCH_TRANSITION_MATCH_ABORTED,
	MP_MATCH_TRANSITION_REVIEW_COMPLETE,
	MP_MATCH_TRANSITION_REFEREE_ADVANCE,
	MP_MATCH_TRANSITION_SAME_MAP_RESTART,
	MP_MATCH_TRANSITION_NEXT_MAP_READY,
	MP_MATCH_TRANSITION_SESSION_END,
	MP_MATCH_TRANSITION_MAP_SHUTDOWN,
	MP_MATCH_TRANSITION_FATAL_RESET,
	MP_MATCH_TRANSITION_REASON_COUNT
} mpMatchTransitionReason_t;

typedef enum {
	MP_MATCH_PAUSE_RUNNING = 0,
	MP_MATCH_PAUSE_PENDING,
	MP_MATCH_PAUSED,
	MP_MATCH_RESUME_COUNTDOWN,
	MP_MATCH_PAUSE_STATE_COUNT
} mpMatchPauseState_t;

typedef enum {
	MP_MATCH_PAUSE_KIND_NONE = 0,
	MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT,
	MP_MATCH_PAUSE_KIND_TECHNICAL,
	MP_MATCH_PAUSE_KIND_COUNT
} mpMatchPauseKind_t;

typedef enum {
	MP_MATCH_PAUSE_REASON_NONE = 0,
	MP_MATCH_PAUSE_REASON_TACTICAL,
	MP_MATCH_PAUSE_REASON_PLAYER_DISCONNECT,
	MP_MATCH_PAUSE_REASON_TECHNICAL_FAULT,
	MP_MATCH_PAUSE_REASON_SERVER_FAULT,
	MP_MATCH_PAUSE_REASON_REFEREE,
	MP_MATCH_PAUSE_REASON_COUNT
} mpMatchPauseReason_t;

typedef enum {
	MP_MATCH_RESUME_OWNER_OR_AUTHORITY = 0,
	MP_MATCH_RESUME_BOTH_TEAMS_OR_AUTHORITY,
	MP_MATCH_RESUME_AUTHORITY_ONLY,
	MP_MATCH_RESUME_POLICY_COUNT
} mpMatchResumePolicy_t;

struct mpMatchPauseView {
	mpMatchPauseState_t	state;
	mpMatchPauseKind_t	kind;
	mpMatchPauseReason_t	reason;
	int					ownerSide;
	uint32_t			resumeConsentMask;
	bool				budgetCharged;
	mpMatchEngineTime	requestedAt;
	mpMatchEngineTime	pausedAt;
	mpMatchEngineTime	pauseExpiry;
	mpMatchEngineTime	resumeDeadline;
};

struct mpMatchTransitionView {
	mpGameState_t			from;
	mpGameState_t			to;
	mpMatchTransitionReason_t reason;
	mpParticipantId		authorizer;
	uint32_t			forcedReadinessBlockers;
};

// Round transitions are subordinate to the live parent phase and use a
// separate reason vocabulary so a parent/round pair can never be ambiguous.
typedef enum {
	MP_MATCH_ROUND_TRANSITION_NONE = 0,
	MP_MATCH_ROUND_TRANSITION_PARENT_ACTIVE,
	MP_MATCH_ROUND_TRANSITION_COUNTDOWN_COMPLETE,
	MP_MATCH_ROUND_TRANSITION_RESULT_COMMITTED,
	MP_MATCH_ROUND_TRANSITION_NEXT_ROUND,
	MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE,
	MP_MATCH_ROUND_TRANSITION_SESSION_RESET,
	MP_MATCH_ROUND_TRANSITION_REASON_COUNT
} mpMatchRoundTransitionReason_t;

struct mpMatchRoundTransitionView {
	roundState_t				from;
	roundState_t				to;
	mpMatchRoundTransitionReason_t reason;
};

struct mpMatchLivePeriodView {
	uint32_t		period;		// 0 regulation, 1..n overtime
	mpMatchTime	start;
	mpMatchTime	deadline;
	bool			hasDeadline;
};

/*
===============================================================================

	Roles and capability bundles

===============================================================================
*/

typedef enum {
	MP_MATCH_ROLE_NONE = 0,
	MP_MATCH_ROLE_PLAYER,
	MP_MATCH_ROLE_CAPTAIN,
	MP_MATCH_ROLE_COACH,
	MP_MATCH_ROLE_BROADCASTER,
	MP_MATCH_ROLE_REFEREE,
	MP_MATCH_ROLE_SERVER_OPERATOR,
	MP_MATCH_ROLE_COUNT
} mpMatchRole_t;

typedef uint32_t mpMatchRoleMask_t;
typedef uint64_t mpMatchCapabilityMask_t;

typedef enum {
	MP_MATCH_CAP_SELF_READY = 0,
	MP_MATCH_CAP_TEAM_READY,
	MP_MATCH_CAP_SELF_TEAM_AND_QUEUE,
	MP_MATCH_CAP_ELIGIBLE_PROPOSAL,
	MP_MATCH_CAP_OWN_TEAM_ROSTER,
	MP_MATCH_CAP_OWN_TEAM_LOCK,
	MP_MATCH_CAP_OWN_TEAM_TIMEOUT,
	MP_MATCH_CAP_TEAM_OBSERVATION,
	MP_MATCH_CAP_TEAM_COMMUNICATION,
	MP_MATCH_CAP_BROADCAST_OBSERVATION,
	MP_MATCH_CAP_PHASE_CONTROL,
	MP_MATCH_CAP_PAUSE_CONTROL,
	MP_MATCH_CAP_ROSTER_CONTROL,
	MP_MATCH_CAP_RULES_BOUNDARY,
	MP_MATCH_CAP_VETO_CONTROL,
	MP_MATCH_CAP_PROPOSAL_MODERATION,
	MP_MATCH_CAP_SERVER_POLICY,
	MP_MATCH_CAP_AUTH_CREDENTIALS,
	MP_MATCH_CAP_FILESYSTEM_OUTPUT,
	// Connection-scoped and self-only.  Granted only while an inactive human
	// occupies a coach or substitute seat; active player/captain withdrawal
	// remains an explicit roster-control decision.
	MP_MATCH_CAP_SELF_ROSTER_LEAVE,
	MP_MATCH_CAP_COUNT
} mpMatchCapability_t;

mpMatchRoleMask_t		MPMatchRoleBit( mpMatchRole_t role );
mpMatchCapabilityMask_t	MPMatchCapabilityBit( mpMatchCapability_t capability );
mpMatchCapabilityMask_t	MPMatchCapabilitiesForRole( mpMatchRole_t role );
mpMatchCapabilityMask_t	MPMatchCapabilitiesForRoles( mpMatchRoleMask_t roles );
bool						MPMatchRoleMaskIsValid( mpMatchRoleMask_t roles, bool connectionScoped );

/*
===============================================================================

	Roster and readiness values

===============================================================================
*/

typedef enum {
	MP_MATCH_ROSTER_PLAYER = 0,
	MP_MATCH_ROSTER_CAPTAIN,
	MP_MATCH_ROSTER_COACH,
	// A substitute is a persistent roster/side member who is not an active
	// player and receives no player, captain or coach capabilities.  Promotion
	// into an active seat is performed only by an explicit roster transaction.
	MP_MATCH_ROSTER_SUBSTITUTE,
	MP_MATCH_ROSTER_ROLE_COUNT
} mpMatchRosterRole_t;

bool MPMatchRosterRoleIsValid( mpMatchRosterRole_t role );
bool MPMatchRosterRoleIsActive( mpMatchRosterRole_t role );
mpMatchRoleMask_t MPMatchRosterPrincipalRoleMask( void );
mpMatchRoleMask_t MPMatchPrincipalRolesForRosterRole(
	mpMatchRosterRole_t role );

typedef enum {
	MP_MATCH_READY_DISABLED = 0,
	MP_MATCH_READY_INDIVIDUAL,
	MP_MATCH_READY_TEAM,
	MP_MATCH_READY_INDIVIDUAL_AND_TEAM,
	MP_MATCH_READY_POLICY_COUNT
} mpMatchReadyPolicy_t;

typedef enum {
	MP_MATCH_BOTS_EXCLUDED = 0,
	MP_MATCH_BOTS_COUNT_AS_READY,
	MP_MATCH_BOT_POLICY_COUNT
} mpMatchBotPolicy_t;

typedef uint32_t mpMatchReadinessBlockerMask_t;

typedef enum {
	MP_MATCH_BLOCKER_RULES_NOT_FROZEN = 0,
	MP_MATCH_BLOCKER_RULES_INVALID,
	MP_MATCH_BLOCKER_MAP_INVALID,
	MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_HUMANS,
	MP_MATCH_BLOCKER_ACTIVE_PARTICIPANT_UNASSIGNED,
	MP_MATCH_BLOCKER_TEAM_OVERSIZED,
	MP_MATCH_BLOCKER_VACANT_REQUIRED_ROSTER_SEAT,
	MP_MATCH_BLOCKER_VETO_INCOMPLETE,
	MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY,
	MP_MATCH_BLOCKER_TEAM_NOT_READY,
	// Append-only: evidence and presentation persist the numeric blocker id.
	MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE,
	MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_PARTICIPANTS,
	MP_MATCH_BLOCKER_COUNT
} mpMatchReadinessBlocker_t;

mpMatchReadinessBlockerMask_t MPMatchReadinessBlockerBit( mpMatchReadinessBlocker_t blocker );

struct mpMatchReadinessPolicy {
					mpMatchReadinessPolicy( void );

	mpMatchReadyPolicy_t	policy;
	mpMatchBotPolicy_t	botPolicy;
	bool				teamMode;
	int					minimumActiveHumans;
	// Bots count towards population independently of their ready-vote policy.
	int					minimumActiveParticipants;
	// Optional larger roster on at least one side (casual teamForcePresent=0).
	int					minimumActiveOnAnySide;
	// Applied independently to every side selected by requiredSideMask.  This
	// keeps team-presence policy separate from captain/team-ready authority.
	int					minimumActivePerRequiredSide;
	uint32_t			readyThresholdBasisPoints;
	int					maximumActivePerSide;
	uint32_t			requiredSideMask;
	bool				requireDeclaredRosterSeats;
	// The server's gametype may temporarily move active players between sides
	// while their declared roster seats retain their between-round affiliation.
	bool				allowRoundSideChanges;
};

struct mpMatchParticipantState {
	mpParticipantId	id;
	int					slot;
	uint32_t		slotGeneration;
	bool				connected;
	bool				human;
	bool				active;
	int					side;
	bool				ready;
	mpMatchRoleMask_t	roles;
};

struct mpMatchRosterSeat {
	bool				declared;
	bool				required;
	int					side;
	mpMatchRosterRole_t role;
	mpParticipantId	occupant;
};

struct mpMatchReadinessView {
	mpMatchReadinessBlockerMask_t blockers;
	int					activeHumans;
	int					activeParticipants;
	int					activePerSide[ MP_MATCH_SIDE_COUNT ];
	int					readyEligibleParticipants;
	int					readyParticipants;
	int					requiredReadyParticipants;
	int					unreadyParticipants;
	uint32_t			unreadySideMask;
	uint32_t			insufficientActiveSideMask;
	int					vacantRequiredSeats;

	bool				IsReady( void ) const;
};

struct mpMatchTimeoutBudgetState {
	int		configured;
	int		remaining;
	int		consumed;
};

/*
===============================================================================

	mpMatchSession

===============================================================================
*/

class mpMatchSession {
public:
							mpMatchSession( void );

	// A session id must be non-zero and unique for the map/session lifetime.
	// Successful reset starts the new session deterministically at revision 1.
	bool					Reset( uint64_t sessionId, mpMatchEngineTime initialEngineTime );

	uint64_t				GetSessionId( void ) const;
	uint64_t				GetSessionRevision( void ) const;
	mpGameState_t			GetPhase( void ) const;
	const mpMatchTransitionView &GetLastTransition( void ) const;

	static bool				IsLegalPhaseTransition( mpGameState_t from, mpGameState_t to,
								mpMatchTransitionReason_t reason );
	// The only boundary needed to start a fresh countdown: validate readiness
	// against the supplied committed snapshot, then freeze that identity and
	// publish WARMUP -> COUNTDOWN with one session revision.  A rejection leaves
	// both the previous frozen identity and lifecycle state untouched.
	mpMatchMutationResult	BeginCountdown( uint64_t rulesRevision, uint64_t rulesDigest,
								mpMatchTransitionReason_t reason,
								mpParticipantId authorizer, uint64_t expectedRevision );
	mpMatchMutationResult	TransitionPhase( mpGameState_t to, mpMatchTransitionReason_t reason,
								mpParticipantId authorizer, uint64_t expectedRevision );
	roundState_t			GetRoundState( void ) const;
	const mpMatchRoundTransitionView &GetLastRoundTransition( void ) const;
	static bool				IsLegalRoundTransition( roundState_t from, roundState_t to,
								mpMatchRoundTransitionReason_t reason );
	mpMatchMutationResult	TransitionRound( roundState_t to,
								mpMatchRoundTransitionReason_t reason,
								uint64_t expectedRevision );

	// Frozen rule metadata is an adapter boundary.  The typed immutable rules
	// snapshot is owned by mpCompetitiveRules and is intentionally not included.
	mpMatchMutationResult	FreezeRules( uint64_t rulesRevision, uint64_t rulesDigest,
								uint64_t expectedRevision );
	mpMatchMutationResult	ClearFrozenRules( uint64_t expectedRevision );
	bool					HasFrozenRules( void ) const;
	uint64_t				GetFrozenRulesRevision( void ) const;
	uint64_t				GetFrozenRulesDigest( void ) const;

	// A zero regulation duration represents an untimed regulation period.
	mpMatchMutationResult	ConfigureRegulationPeriod( int durationMsec,
								uint64_t expectedRevision );
	mpMatchMutationResult	BeginOvertimePeriod( uint32_t expectedCurrentPeriod,
								int durationMsec, uint64_t expectedRevision );
	const mpMatchLivePeriodView &GetLivePeriod( void ) const;
	int					GetRegulationDurationMsec( void ) const;

	// Clock samples are derived progression and do not churn sessionRevision.
	// AdvanceFrame commits at most one semantic pause-overlay transition.
	mpMatchMutationResult	AdvanceFrame( mpMatchEngineTime engineNow );
	mpMatchEngineTime		GetEngineTime( void ) const;
	mpMatchTime			GetMatchTime( void ) const;
	mpMatchTime			GetLivePeriodStartMatchTime( void ) const;
	bool					IsMatchClockRunning( void ) const;

	// Connection-scoped identity.  The caller must retain both slot and
	// generation when resolving an inbound connection operation.
	mpMatchMutationResult	BindParticipant( int slot, bool human, mpMatchRoleMask_t roles,
								uint64_t expectedRevision, mpParticipantId &outParticipant );
	mpMatchMutationResult	UnbindParticipant( int slot, uint32_t slotGeneration,
								uint64_t expectedRevision );
	bool					ResolveSlotBinding( int slot, uint32_t slotGeneration,
								mpParticipantId &outParticipant ) const;
	bool					GetSlotGeneration( int slot, uint32_t &outSlotGeneration ) const;
	bool					ResolveParticipantSequence( uint32_t sequence,
								mpParticipantId &outParticipant ) const;
	bool					ResolveParticipant( mpParticipantId participant, int &outSlot,
								uint32_t &outSlotGeneration ) const;
	const mpMatchParticipantState *FindParticipant( mpParticipantId participant ) const;
	const mpMatchParticipantState *GetParticipantByIndex( int index ) const;

	mpMatchMutationResult	SetParticipantRoles( mpParticipantId participant,
								mpMatchRoleMask_t roles, uint64_t expectedRevision );
	mpMatchCapabilityMask_t GetParticipantCapabilities( mpParticipantId participant ) const;
	bool					CanSelfLeaveRoster( mpParticipantId participant ) const;
	mpMatchMutationResult	SetParticipantActive( mpParticipantId participant, bool active,
								uint64_t expectedRevision );
	mpMatchMutationResult	SetParticipantSide( mpParticipantId participant, int side,
							uint64_t expectedRevision );
	mpMatchMutationResult	SetParticipantRoundSide( mpParticipantId participant, int side,
							uint64_t expectedRevision );
	mpMatchMutationResult	SetParticipantReady( mpParticipantId participant, bool ready,
								uint64_t expectedRevision );
	mpMatchMutationResult	SetTeamReady( int side, bool ready, uint64_t expectedRevision );
	bool					IsTeamReady( int side ) const;

	mpMatchMutationResult	ConfigureReadiness( const mpMatchReadinessPolicy &policy,
								uint64_t expectedRevision );
	mpMatchMutationResult	SetExternalReadinessBlockers( mpMatchReadinessBlockerMask_t blockers,
								uint64_t expectedRevision );
	const mpMatchReadinessPolicy &GetReadinessPolicy( void ) const;
	mpMatchReadinessView	EvaluateReadiness( void ) const;
	uint32_t				GetLastForcedReadinessBlockers( void ) const;

	mpMatchMutationResult	DeclareRosterSeat( int seat, int side, mpMatchRosterRole_t role,
								bool required, uint64_t expectedRevision );
	mpMatchMutationResult	ClearRosterSeat( int seat, uint64_t expectedRevision );
	mpMatchMutationResult	AssignRosterSeat( int seat, mpParticipantId participant,
								uint64_t expectedRevision );
	mpMatchMutationResult	VacateRosterSeat( int seat, uint64_t expectedRevision );
	const mpMatchRosterSeat *GetRosterSeat( int seat ) const;
	int					FindRosterSeat( mpParticipantId participant ) const;

	mpMatchMutationResult	ConfigureTimeouts( int perSide, int durationMsec,
								bool allowDuringCountdown, int resumeCountdownMsec,
								mpMatchResumePolicy_t resumePolicy, uint64_t expectedRevision );
	mpMatchMutationResult	RequestTeamTimeout( int side, mpMatchPauseReason_t reason,
								uint64_t expectedRevision );
	// durationMsec > 0 gives the technical pause an expiry (a disconnect pause);
	// 0 keeps the inherited open-ended pause that only an authority resumes.
	mpMatchMutationResult	RequestTechnicalPause( mpMatchPauseReason_t reason,
								uint64_t expectedRevision, int durationMsec = 0 );
	mpMatchMutationResult	RequestResumeByTeam( int side, uint64_t expectedRevision );
	mpMatchMutationResult	RequestResumeByAuthority( uint64_t expectedRevision );
	const mpMatchPauseView &GetPause( void ) const;
	const mpMatchTimeoutBudgetState &GetTimeoutBudget( int side ) const;
	int					GetTimeoutDurationMsec( void ) const;
	bool				IsTimeoutAllowedDuringCountdown( void ) const;
	int					GetResumeCountdownMsec( void ) const;
	mpMatchResumePolicy_t	GetResumePolicy( void ) const;

	// Cheap startup/test invariant.  It validates all fixed tables and current
	// state without allocating or logging.
	bool					ValidateInvariants( void ) const;

private:
	bool					IsExpectedRevision( uint64_t expectedRevision ) const;
	bool					CanCommit( void ) const;
	mpMatchMutationResult	Applied( uint64_t previousRevision );
	mpMatchMutationResult	NoChange( mpMatchReason_t reason ) const;
	mpMatchMutationResult	Rejected( mpMatchReason_t reason ) const;

	int					FindParticipantIndex( mpParticipantId participant ) const;
	int					FindFreeParticipantIndex( void ) const;
	void					ClearParticipantState( mpMatchParticipantState &participant );
	void					ClearPause( void );
	void					ClearTeamReady( void );
	void					InvalidateCountdownForRosterMutation( void );
	void					ResetLivePeriod( void );
	void					NormalizeRoundForParentTransition( void );
	bool					IsValidSide( int side ) const;
	bool					IsLivePhase( void ) const;
	bool					AddEngineDuration( mpMatchEngineTime base, int durationMsec,
								mpMatchEngineTime &outTime ) const;

	uint64_t				sessionId;
	uint64_t				sessionRevision;
	uint32_t			nextParticipantSequence;
	mpGameState_t			phase;
	mpMatchTransitionView	lastTransition;
	roundState_t			roundState;
	mpMatchRoundTransitionView lastRoundTransition;

	bool					frozenRulesValid;
	uint64_t				frozenRulesRevision;
	uint64_t				frozenRulesDigest;
	int					regulationDurationMsec;
	mpMatchLivePeriodView	livePeriod;

	mpMatchEngineTime		engineTime;
	mpMatchTime			matchTime;
	mpMatchTime			livePeriodStartMatchTime;
	mpMatchPauseView		pause;

	uint32_t			slotGenerations[ MP_MATCH_MAX_CONNECTION_SLOTS ];
	int					slotParticipantIndex[ MP_MATCH_MAX_CONNECTION_SLOTS ];
	mpMatchParticipantState participants[ MP_MATCH_MAX_PARTICIPANTS ];

	mpMatchReadinessPolicy readinessPolicy;
	mpMatchReadinessBlockerMask_t externalReadinessBlockers;
	bool					teamReady[ MP_MATCH_SIDE_COUNT ];
	uint32_t			lastForcedReadinessBlockers;
	mpMatchRosterSeat		roster[ MP_MATCH_MAX_ROSTER_SEATS ];

	mpMatchTimeoutBudgetState timeoutBudgets[ MP_MATCH_SIDE_COUNT ];
	int					timeoutDurationMsec;
	int					technicalPauseDurationMsec;	// openQ4 MP-12: 0 means open-ended
	bool				technicalPauseExpires;
	bool				timeoutAllowedDuringCountdown;
	int					resumeCountdownMsec;
	mpMatchResumePolicy_t	resumePolicy;
};

#endif // __MP_MATCH_SESSION_H__
