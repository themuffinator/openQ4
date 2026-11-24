//----------------------------------------------------------------
// MatchView.h
//
// Bounded, recipient-authorized competitive session projections.
//
// This is an adapter/value and wire boundary only.  It owns no game objects,
// GUI state, cvars, commands or persistence.  Sensitive source values carry
// an audience tag; MPMatchViewBuild validates every candidate before copying
// only values authorized for the recipient into mpSessionView.
//----------------------------------------------------------------

#ifndef __MP_MATCH_VIEW_H__
#define __MP_MATCH_VIEW_H__

#include "../MatchPhase.h"
#include "MatchProtocol.h"
#include <stddef.h>

class idBitMsg;

static const unsigned short MP_MATCH_VIEW_SCHEMA_VERSION = 4;

// A complete view can contain 32 public identities, 32 operation decisions,
// 32 map-pool entries, 64 veto/map-history entries, a four-entry evidence tail
// and recipient-authorized tactical data.  That state cannot truthfully fit in
// the 1024-byte operation request budget.  7936 is a hard codec limit, verified
// against the schema's
// maximum legal projection, and reserves 256 bytes in openQ4's 8192-byte game
// reliable-message buffer for the outer message tag and transport evolution.
// All nested arrays and strings are separately bounded and hostile
// over-capacity inputs fail closed.
static const int MP_MATCH_VIEW_MAX_MESSAGE_BYTES = 7936;
static const int MP_MATCH_VIEW_MAX_TOP_LEVEL_FIELDS = 26;
static const int MP_MATCH_VIEW_RESULT_NAME_BYTES = 64;
static const int MP_MATCH_VIEW_SIDE_NONE = -1;
static const int MP_MATCH_VIEW_SIDE_COUNT = 2;
static const int MP_MATCH_VIEW_MAX_PARTICIPANTS = 32;
static const int MP_MATCH_VIEW_MAX_ROLE_SUMMARIES = 16;
static const int MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES = 2;
static const int MP_MATCH_VIEW_MAX_ROSTER_SEATS = 32;
static const int MP_MATCH_VIEW_MAX_PROPOSALS = 2;
static const int MP_MATCH_VIEW_MAX_RULE_FIELDS = 64;
static const int MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES = 64;
static const int MP_MATCH_VIEW_MAX_INVITATIONS = 16;
static const int MP_MATCH_VIEW_MAX_QUEUE_ENTRIES = 32;
static const int MP_MATCH_VIEW_MAX_SERIES_MAP_POOL = 32;
static const int MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY = 64;
static const int MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY = 64;
static const int MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS = 256;
static const int MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS = 4;
static const int MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES = 96;
static const int MP_MATCH_VIEW_MAX_TEAM_VITALS = 32;
static const int MP_MATCH_VIEW_MAX_ITEM_TIMINGS = 16;
static const int MP_MATCH_VIEW_MAX_FOLLOW_TARGETS = 32;
static const int MP_MATCH_VIEW_ITEM_TOKEN_BYTES = 48;
static const int MP_MATCH_VIEW_MAP_TOKEN_BYTES = 64;
static const int MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES = 64;

typedef unsigned long long mpMatchViewAllowedOperationMask_t;
typedef unsigned int mpMatchViewAudienceMask_t;
typedef unsigned int mpMatchViewObserverKindMask_t;
typedef unsigned int mpMatchViewPublicRoleMask_t;

/*
===============================================================================

	Public lifecycle, readiness and competition summaries

	These enums are view-wire values.  Session, rules, proposal, team and series
	adapters translate explicitly rather than creating dependencies on their
	aggregates.

===============================================================================
*/

typedef enum {
	MP_MATCH_VIEW_PAUSE_RUNNING = 0,
	MP_MATCH_VIEW_PAUSE_PENDING,
	MP_MATCH_VIEW_PAUSED,
	MP_MATCH_VIEW_RESUME_COUNTDOWN,
	MP_MATCH_VIEW_PAUSE_STATE_COUNT
} mpMatchViewPauseState_t;

typedef enum {
	MP_MATCH_VIEW_PAUSE_KIND_NONE = 0,
	MP_MATCH_VIEW_PAUSE_KIND_TEAM_TIMEOUT,
	MP_MATCH_VIEW_PAUSE_KIND_TECHNICAL,
	MP_MATCH_VIEW_PAUSE_KIND_COUNT
} mpMatchViewPauseKind_t;

typedef enum {
	MP_MATCH_VIEW_PAUSE_REASON_NONE = 0,
	MP_MATCH_VIEW_PAUSE_REASON_TACTICAL,
	MP_MATCH_VIEW_PAUSE_REASON_PLAYER_DISCONNECT,
	MP_MATCH_VIEW_PAUSE_REASON_TECHNICAL_FAULT,
	MP_MATCH_VIEW_PAUSE_REASON_SERVER_FAULT,
	MP_MATCH_VIEW_PAUSE_REASON_REFEREE,
	MP_MATCH_VIEW_PAUSE_REASON_COUNT
} mpMatchViewPauseReason_t;

typedef enum {
	MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE = 0,
	MP_MATCH_VIEW_RESUME_BOTH_SIDES_OR_REFEREE,
	MP_MATCH_VIEW_RESUME_REFEREE_ONLY,
	MP_MATCH_VIEW_RESUME_POLICY_COUNT
} mpMatchViewResumePolicy_t;

typedef enum {
	MP_MATCH_VIEW_ROLE_NONE = 0,
	MP_MATCH_VIEW_ROLE_PLAYER,
	MP_MATCH_VIEW_ROLE_CAPTAIN,
	MP_MATCH_VIEW_ROLE_COACH,
	MP_MATCH_VIEW_ROLE_BROADCASTER,
	MP_MATCH_VIEW_ROLE_REFEREE,
	MP_MATCH_VIEW_ROLE_COUNT
} mpMatchViewPublicRole_t;

typedef enum {
	MP_MATCH_VIEW_ROSTER_PLAYER = 0,
	MP_MATCH_VIEW_ROSTER_CAPTAIN,
	MP_MATCH_VIEW_ROSTER_COACH,
	MP_MATCH_VIEW_ROSTER_SUBSTITUTE,
	MP_MATCH_VIEW_ROSTER_ROLE_COUNT
} mpMatchViewRosterRole_t;

typedef enum {
	MP_MATCH_VIEW_QUEUE_NONE = 0,
	MP_MATCH_VIEW_QUEUE_WAITING,
	MP_MATCH_VIEW_QUEUE_DEFERRED,
	MP_MATCH_VIEW_QUEUE_ADMITTED,
	MP_MATCH_VIEW_QUEUE_STATE_COUNT
} mpMatchViewQueueState_t;

typedef enum {
	MP_MATCH_VIEW_PROPOSAL_GLOBAL = 0,
	MP_MATCH_VIEW_PROPOSAL_SIDE,
	MP_MATCH_VIEW_PROPOSAL_SCOPE_COUNT
} mpMatchViewProposalScope_t;

typedef enum {
	MP_MATCH_VIEW_BALLOT_NONE = 0,
	MP_MATCH_VIEW_BALLOT_YES,
	MP_MATCH_VIEW_BALLOT_NO,
	MP_MATCH_VIEW_BALLOT_ABSTAIN,
	MP_MATCH_VIEW_BALLOT_COUNT
} mpMatchViewBallot_t;

typedef enum {
	MP_MATCH_VIEW_SERIES_DISABLED = 0,
	MP_MATCH_VIEW_SERIES_SETUP,
	MP_MATCH_VIEW_SERIES_VETO,
	MP_MATCH_VIEW_SERIES_READY,
	MP_MATCH_VIEW_SERIES_MAP_ACTIVE,
	MP_MATCH_VIEW_SERIES_MAP_COMPLETE,
	MP_MATCH_VIEW_SERIES_COMPLETE,
	MP_MATCH_VIEW_SERIES_CANCELLED,
	MP_MATCH_VIEW_SERIES_STATE_COUNT
} mpMatchViewSeriesState_t;

typedef enum {
	MP_MATCH_VIEW_VETO_BAN = 0,
	MP_MATCH_VIEW_VETO_PICK,
	MP_MATCH_VIEW_VETO_SIDE,
	MP_MATCH_VIEW_VETO_DECIDER,
	MP_MATCH_VIEW_VETO_ACTION_COUNT
} mpMatchViewVetoAction_t;

typedef enum {
	MP_MATCH_VIEW_MAP_AVAILABLE = 0,
	MP_MATCH_VIEW_MAP_BANNED,
	MP_MATCH_VIEW_MAP_SELECTED,
	MP_MATCH_VIEW_MAP_DISPOSITION_COUNT
} mpMatchViewMapDisposition_t;

typedef enum {
	MP_MATCH_VIEW_MAP_UNPLAYED = 0,
	MP_MATCH_VIEW_MAP_DECIDED,
	MP_MATCH_VIEW_MAP_FORFEIT,
	MP_MATCH_VIEW_MAP_ABORTED,
	MP_MATCH_VIEW_MAP_OUTCOME_COUNT
} mpMatchViewMapOutcome_t;

typedef enum {
	MP_MATCH_VIEW_RULE_BOOL = 0,
	MP_MATCH_VIEW_RULE_INTEGER,
	MP_MATCH_VIEW_RULE_ENUM,
	MP_MATCH_VIEW_RULE_TYPE_COUNT
} mpMatchViewRuleType_t;

typedef enum {
	MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT = 0,
	MP_MATCH_VIEW_RULES_FROZEN_FOR_MAP,
	MP_MATCH_VIEW_RULES_BOUNDARY_COUNT
} mpMatchViewRulesBoundary_t;

typedef struct mpMatchViewLifecycle_s {
	mpGameState_t			phase;
	roundState_t			round;
	mpMatchViewPauseState_t	pauseState;
	mpMatchViewPauseKind_t	pauseKind;
	mpMatchViewPauseReason_t	pauseReason;
	int					pauseOwnerSide;
	bool					hasPauseExpiry;
	unsigned long long		pauseExpiryEngineMsec;
	bool					hasResumeDeadline;
	unsigned long long		resumeDeadlineEngineMsec;
	mpMatchViewResumePolicy_t resumePolicy;
	unsigned char			resumeRequiredSideMask;
	unsigned char			resumeConsentingSideMask;

	void Clear( void );
} mpMatchViewLifecycle_t;

typedef struct mpMatchViewClocks_s {
	unsigned long long	engineTimeMsec;
	unsigned long long	matchTimeMsec;
	unsigned int		livePeriod;		// 0 regulation, 1..n overtime
	bool				isOvertime;
	bool				hasLiveDeadline;
	unsigned long long	liveDeadlineMatchMsec;

	void Clear( void );
} mpMatchViewClocks_t;

typedef struct mpMatchViewReadiness_s {
	unsigned int	blockers;
	unsigned short	readyCount;
	unsigned short	eligibleCount;
	unsigned short	activeHumans;
	unsigned short	vacantRequiredSeats;

	void Clear( void );
} mpMatchViewReadiness_t;

typedef struct mpMatchViewTimeoutBudget_s {
	unsigned char	configured;
	unsigned char	remaining;
	unsigned char	consumed;
	unsigned short	durationSeconds;

	void Clear( void );
} mpMatchViewTimeoutBudget_t;

typedef struct mpMatchViewRoleSummary_s {
	mpMatchViewPublicRole_t	role;
	int						side;
	unsigned char			count;

	void Clear( void );
} mpMatchViewRoleSummary_t;

typedef struct mpMatchViewRosterSummary_s {
	int				side;
	unsigned char	declaredSeats;
	unsigned char	occupiedSeats;
	unsigned char	connectedOccupants;
	unsigned char	readyOccupants;
	unsigned char	activeParticipants;
	unsigned char	queueDepth;
	bool			teamReady;
	bool			locked;

	void Clear( void );
} mpMatchViewRosterSummary_t;

typedef struct mpMatchViewParticipantSummary_s {
	mpMatchProtocolParticipantId_t participantId;
	unsigned char			slot;		// 0xff while disconnected
	int					side;
	mpMatchViewPublicRoleMask_t publicRoleMask;
	bool					connected;
	bool					human;		// explicit bot exclusion; never inferred from slot/name
	bool					active;

	void Clear( void );
} mpMatchViewParticipantSummary_t;

typedef struct mpMatchViewProposalSummary_s {
	bool					present;
	unsigned int			proposalId;
	mpMatchOperationOpcode_t	opcode;
	mpMatchViewProposalScope_t scope;
	int						side;
	mpMatchProtocolParticipantId_t callerParticipantId;
	unsigned short			yesCount;
	unsigned short			noCount;
	unsigned short			abstainCount;
	unsigned short			castCount;
	unsigned short			eligibleCount;
	unsigned short			requiredQuorumCount;
	unsigned short			requiredYesCount;
	unsigned long long		expiresAtEngineMsec;
	bool						recipientEligible;
	mpMatchViewBallot_t		recipientBallot;

	void Clear( void );
} mpMatchViewProposalSummary_t;

typedef struct mpMatchViewRuleValue_s {
	unsigned char		fieldId;
	mpMatchViewRuleType_t type;
	int					value;
	bool					editable;

	void Clear( void );
} mpMatchViewRuleValue_t;

typedef struct mpMatchViewCommittedRules_s {
	bool					present;
	unsigned int			rulesSchemaVersion;
	unsigned int			revision;
	unsigned long long		digest;
	int						profileId;
	bool						customized;
	mpMatchViewRulesBoundary_t boundary;
	unsigned char			valueCount;
	mpMatchViewRuleValue_t	values[ MP_MATCH_VIEW_MAX_RULE_FIELDS ];

	void Clear( void );
} mpMatchViewCommittedRules_t;

typedef struct mpMatchViewStagedRuleValue_s {
	unsigned char		fieldId;
	mpMatchViewRuleType_t type;
	int					value;

	void Clear( void );
} mpMatchViewStagedRuleValue_t;

typedef struct mpMatchViewStagedRules_s {
	bool					present;
	unsigned int			revision;
	unsigned long long		digest;
	int						profileId;
	bool						customized;
	unsigned long long		changedFieldMask;
	unsigned char			valueCount;
	mpMatchViewStagedRuleValue_t values[ MP_MATCH_VIEW_MAX_RULE_FIELDS ];

	void Clear( void );
} mpMatchViewStagedRules_t;

typedef struct mpMatchViewOperationAvailability_s {
	mpMatchOperationOpcode_t opcode;
	bool					available;
	mpMatchProtocolReason_t reason;
	mpMatchLocalizationId_t localizationId;
	unsigned char			fieldId;
	unsigned int			detail;

	void Clear( void );
} mpMatchViewOperationAvailability_t;

typedef struct mpMatchViewSeriesMap_s {
	unsigned char			poolIndex;
	mpMatchViewMapDisposition_t disposition;
	int						selectedBySide;
	unsigned char			selectionNumber;	// 0 when not selected
	bool						decider;
	bool						hasStartingGameSide;
	int						startingGameSide;
	int						gameSideChosenBy;
	unsigned char			tokenLength;
	char					mapToken[ MP_MATCH_VIEW_MAP_TOKEN_BYTES + 1 ];

	void Clear( void );
	bool SetMapToken( const char *mapTokenValue, int length = -1 );
} mpMatchViewSeriesMap_t;

typedef struct mpMatchViewVetoHistory_s {
	unsigned char			sequenceNumber;
	mpMatchViewVetoAction_t action;
	int						actingSide;
	unsigned char			mapPoolIndex;
	bool						hasSelectedGameSide;
	int						selectedGameSide;

	void Clear( void );
} mpMatchViewVetoHistory_t;

typedef struct mpMatchViewSeriesMapHistory_s {
	unsigned char			attemptNumber;
	unsigned char			mapPoolIndex;
	mpMatchViewMapOutcome_t outcome;
	int						winnerSide;
	unsigned short			scores[ MP_MATCH_VIEW_SIDE_COUNT ];

	void Clear( void );
} mpMatchViewSeriesMapHistory_t;

typedef struct mpMatchViewSeriesSummary_s {
	bool					present;
	unsigned long long		seriesId;
	mpMatchViewSeriesState_t state;
	unsigned long long		revision;
	int						gameType;
	unsigned char			bestOf;
	unsigned char			currentMapNumber;
	unsigned char			wins[ MP_MATCH_VIEW_SIDE_COUNT ];
	bool						hasNextMap;
	unsigned char			nextMapLength;
	char					nextMap[ MP_MATCH_VIEW_MAP_TOKEN_BYTES + 1 ];
	unsigned char			currentVetoStep;
	unsigned char			vetoStepCount;
	bool						hasVetoTurn;
	mpMatchViewVetoAction_t vetoTurnAction;
	int						vetoTurnSide;
	unsigned char			mapPoolCount;
	mpMatchViewSeriesMap_t mapPool[ MP_MATCH_VIEW_MAX_SERIES_MAP_POOL ];
	unsigned char			vetoHistoryCount;
	mpMatchViewVetoHistory_t vetoHistory[ MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY ];
	unsigned char			mapHistoryCount;
	mpMatchViewSeriesMapHistory_t mapHistory[ MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY ];

	void Clear( void );
	bool SetNextMap( const char *mapToken, int length = -1 );
} mpMatchViewSeriesSummary_t;

// Evidence projection values intentionally carry status and bounded counters
// only.  Server-local artifact paths and backend diagnostic text never cross
// this recipient boundary.
typedef enum {
	MP_MATCH_VIEW_EVIDENCE_DISABLED = 0,
	MP_MATCH_VIEW_EVIDENCE_CAPTURING,
	MP_MATCH_VIEW_EVIDENCE_FINALIZED,
	MP_MATCH_VIEW_EVIDENCE_FAILED,
	MP_MATCH_VIEW_EVIDENCE_STATE_COUNT
} mpMatchViewEvidenceState_t;

typedef enum {
	MP_MATCH_VIEW_MVD_DISABLED = 0,
	MP_MATCH_VIEW_MVD_PENDING,
	MP_MATCH_VIEW_MVD_RECORDING,
	MP_MATCH_VIEW_MVD_AVAILABLE,
	MP_MATCH_VIEW_MVD_FAILED,
	MP_MATCH_VIEW_MVD_STATE_COUNT
} mpMatchViewMVDState_t;

typedef enum {
	MP_MATCH_VIEW_REPORT_DISABLED = 0,
	MP_MATCH_VIEW_REPORT_PENDING,
	MP_MATCH_VIEW_REPORT_AVAILABLE,
	MP_MATCH_VIEW_REPORT_FAILED,
	MP_MATCH_VIEW_REPORT_STATE_COUNT
} mpMatchViewReportState_t;

typedef enum {
	MP_MATCH_VIEW_EVIDENCE_EVENT_NONE = 0,
	MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION,
	MP_MATCH_VIEW_EVIDENCE_EVENT_ROUND_TRANSITION,
	MP_MATCH_VIEW_EVIDENCE_EVENT_PAUSE_TRANSITION,
	MP_MATCH_VIEW_EVIDENCE_EVENT_ROLE_CHANGE,
	MP_MATCH_VIEW_EVIDENCE_EVENT_PROPOSAL,
	MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE,
	MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT,
	MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE,
	MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT
} mpMatchViewEvidenceEventKind_t;

typedef struct mpMatchViewEvidenceSummary_s {
	mpMatchViewEvidenceState_t evidenceState;
	mpMatchViewMVDState_t	mvdState;
	mpMatchViewReportState_t reportState;
	unsigned long long		evidenceRevision;
	unsigned short			eventCount;
	unsigned int			droppedRecordCount;
	bool					droppedRecordCountSaturated;
	unsigned char			participantStatsCount;
	unsigned char			teamStatsCount;
	bool					resultRecorded;
	unsigned char			recentEventCount;
	mpMatchViewEvidenceEventKind_t recentEventKinds[
		MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS ];

	void Clear( void );
} mpMatchViewEvidenceSummary_t;

typedef struct mpMatchViewDenial_s {
	bool					present;
	mpMatchOperationOpcode_t	opcode;
	mpMatchProtocolReason_t	reason;
	mpMatchLocalizationId_t	localizationId;
	unsigned char			fieldId;
	unsigned int			detail;

	void Clear( void );
} mpMatchViewDenial_t;

// Recipient-scoped operation binding.  These values are not secrets: the
// server still binds every request to its trusted transport slot and resolves
// this generation against the current session.
typedef struct mpMatchViewRecipient_s {
	mpMatchProtocolParticipantId_t participantId;
	unsigned char			slot;
	unsigned int			bindingGeneration;
	int					side;			// current gameplay/team side
	int					competitionSide;	// stable series contestant side
	mpMatchViewPublicRoleMask_t publicRoleMask;
	bool					ready;
	bool					active;
	bool					readyEligible;
	mpMatchViewQueueState_t queueState;
	int					queueSide;
	bool					hasQueuePosition;
	unsigned char		queuePosition;
	bool					resumeConsented;

	void Clear( void );
} mpMatchViewRecipient_t;

/*
===============================================================================

	Recipient-authorized private projections

	Audience metadata exists only on source candidates.  The final wire view
	contains no audience tags; presence proves authorization occurred at source.

===============================================================================
*/

typedef enum {
	MP_MATCH_VIEW_AUDIENCE_PUBLIC = 0,
	MP_MATCH_VIEW_AUDIENCE_OWN_SIDE,
	MP_MATCH_VIEW_AUDIENCE_REFEREE,
	MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
	MP_MATCH_VIEW_AUDIENCE_RECIPIENT,
	MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0,
	MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1,
	MP_MATCH_VIEW_AUDIENCE_COUNT
} mpMatchViewAudience_t;

typedef enum {
	MP_MATCH_VIEW_OBSERVER_TEAM_VITAL = 0,
	MP_MATCH_VIEW_OBSERVER_ITEM_TIMING,
	MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET,
	MP_MATCH_VIEW_OBSERVER_KIND_COUNT
} mpMatchViewObserverKind_t;

typedef struct mpMatchViewRecipientPolicy_s {
	mpMatchViewAudienceMask_t		audiences;
	mpMatchProtocolParticipantId_t recipientId;
	int							ownSide;
	mpMatchViewObserverKindMask_t observerKinds;

	void Clear( void );
} mpMatchViewRecipientPolicy_t;

typedef struct mpMatchViewAuthorizationTag_s {
	mpMatchViewAudience_t audience;
	int					audienceSide;
	mpMatchProtocolParticipantId_t audienceParticipantId;

	void Clear( void );
} mpMatchViewAuthorizationTag_t;

typedef struct mpMatchViewRosterSeat_s {
	unsigned char			seatIndex;
	int						side;
	mpMatchViewRosterRole_t role;
	bool						required;
	bool						occupied;
	mpMatchProtocolParticipantId_t participantId;
	bool						connected;
	bool						ready;
	bool						active;

	void Clear( void );
} mpMatchViewRosterSeat_t;

typedef struct mpMatchViewInvitationSummary_s {
	unsigned int			invitationId;
	int						side;
	mpMatchViewRosterRole_t role;
	mpMatchProtocolParticipantId_t inviterParticipantId;
	mpMatchProtocolParticipantId_t inviteeParticipantId;
	unsigned long long		expiresAtEngineMsec;

	void Clear( void );
} mpMatchViewInvitationSummary_t;

typedef struct mpMatchViewQueueEntry_s {
	mpMatchProtocolParticipantId_t participantId;
	int						side;
	unsigned char			position;	// 1-based while queued
	mpMatchViewQueueState_t state;

	void Clear( void );
} mpMatchViewQueueEntry_t;

typedef struct mpMatchViewProposalCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewProposalSummary_t value;

	void Clear( void );
} mpMatchViewProposalCandidate_t;

typedef struct mpMatchViewStagedRulesCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewStagedRules_t value;

	void Clear( void );
} mpMatchViewStagedRulesCandidate_t;

typedef struct mpMatchViewRosterSeatCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewRosterSeat_t value;

	void Clear( void );
} mpMatchViewRosterSeatCandidate_t;

typedef struct mpMatchViewInvitationCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewInvitationSummary_t value;

	void Clear( void );
} mpMatchViewInvitationCandidate_t;

typedef struct mpMatchViewQueueEntryCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewQueueEntry_t value;

	void Clear( void );
} mpMatchViewQueueEntryCandidate_t;

typedef struct mpMatchViewObserverCandidate_s {
	mpMatchViewAuthorizationTag_t authorization;
	mpMatchViewObserverKind_t kind;
	mpMatchProtocolParticipantId_t participantId;
	int						participantSide;
	unsigned short			primaryValue;
	unsigned short			secondaryValue;
	bool						active;
	unsigned long long		matchDeadlineMsec;
	unsigned char			tokenLength;
	char					token[ MP_MATCH_VIEW_ITEM_TOKEN_BYTES + 1 ];

	void Clear( void );
	bool SetTeamVital( mpMatchViewAudience_t audienceTag, int tagSide,
		mpMatchProtocolParticipantId_t participant, int side,
		int health, int armor, bool alive );
	bool SetItemTiming( mpMatchViewAudience_t audienceTag, int tagSide,
		const char *itemToken, unsigned long long deadlineMsec, bool available );
	bool SetFollowTarget( mpMatchViewAudience_t audienceTag, int tagSide,
		mpMatchProtocolParticipantId_t participant, int side, bool selectable );
} mpMatchViewObserverCandidate_t;

typedef struct mpMatchViewTeamVital_s {
	mpMatchProtocolParticipantId_t participantId;
	int						participantSide;
	unsigned short			health;
	unsigned short			armor;
	bool						alive;

	void Clear( void );
} mpMatchViewTeamVital_t;

typedef struct mpMatchViewItemTiming_s {
	bool					available;
	unsigned long long	matchDeadlineMsec;
	unsigned char		tokenLength;
	char					token[ MP_MATCH_VIEW_ITEM_TOKEN_BYTES + 1 ];

	void Clear( void );
} mpMatchViewItemTiming_t;

typedef struct mpMatchViewFollowTarget_s {
	mpMatchProtocolParticipantId_t participantId;
	int						participantSide;
	bool						selectable;

	void Clear( void );
} mpMatchViewFollowTarget_t;

/*
===============================================================================

	Public source state and final recipient view

	Adapter population contract:
	- sessionRevision tracks the session aggregate; controlRevision advances for
	  every accepted control-plane mutation; viewRevision advances for every
	  changed recipient projection, including sampled clocks.
	- participantSummaries contains every publicly addressable session identity
	  and must contain a human row exactly matching recipient.
	- operationAvailability contains one ordered decision for every opcode;
	  MPMatchViewSetOperationDecision keeps allowedOperations consistent.
	- a present series contains the complete bounded map pool, applied veto
	  history and map-attempt history under one nonzero stable series id, never a
	  truncated summary.
	- evidence contains only recipient-safe lifecycle/integrity summaries and a
	  bounded event-kind tail; local artifact paths and backend text stay server-side.
	- side proposals, staged rules, roster seats, invitations, queues and
	  tactical observer values enter only through source candidate arrays.

===============================================================================
*/

typedef enum {
	MP_MATCH_VIEW_RESULT_NONE = 0,
	MP_MATCH_VIEW_RESULT_DECIDED,
	MP_MATCH_VIEW_RESULT_FORFEIT,
	MP_MATCH_VIEW_RESULT_DRAW,
	MP_MATCH_VIEW_RESULT_ABORTED,
	MP_MATCH_VIEW_RESULT_OUTCOME_COUNT
} mpMatchViewResultOutcome_t;

typedef enum {
	MP_MATCH_VIEW_RESULT_REASON_NONE = 0,
	MP_MATCH_VIEW_RESULT_REASON_LIMIT_REACHED,
	MP_MATCH_VIEW_RESULT_REASON_FORFEIT,
	MP_MATCH_VIEW_RESULT_REASON_MATCH_ABORTED,
	MP_MATCH_VIEW_RESULT_REASON_SESSION_END,
	MP_MATCH_VIEW_RESULT_REASON_MAP_SHUTDOWN,
	MP_MATCH_VIEW_RESULT_REASON_FATAL_RESET,
	MP_MATCH_VIEW_RESULT_REASON_COUNT
} mpMatchViewResultReason_t;

// Frozen public match outcome, independent of optional evidence recording.
// Retained through review, nextgame and the following warmup; cleared when a
// new countdown or session begins. Identity and sanitized UTF-8 name refer to
// the result revision, never to a present slot occupant or mutable roster row.
// winnerSide is a gameplay team, not a series competition side. Individual
// winners use winnerParticipantId/name instead. Draws and aborts have no winner.
typedef struct mpMatchViewTerminalResult_s {
	mpMatchViewResultOutcome_t outcome;
	mpMatchViewResultReason_t reason;
	mpMatchProtocolRevision_t resultRevision;
	int winnerSide;
	mpMatchProtocolParticipantId_t winnerParticipantId;
	unsigned char winnerNameLength;
	char winnerName[ MP_MATCH_VIEW_RESULT_NAME_BYTES + 1 ];

	void Clear( void );
} mpMatchViewTerminalResult_t;

// Sanitizes untrusted public names without truncating a UTF-8 code point.
// The fallback is stable within the enclosing session and requires no roster.
void MPMatchViewSetResultWinnerName( mpMatchViewTerminalResult_t &result,
	const char *name );

typedef struct mpMatchViewPublicState_s {
	unsigned short				schemaVersion;
	mpMatchProtocolSessionId_t	sessionId;
	mpMatchProtocolRevision_t	sessionRevision;
	mpMatchProtocolRevision_t	controlRevision;
	mpMatchProtocolRevision_t	viewRevision;
	mpMatchViewLifecycle_t		lifecycle;
	mpMatchViewClocks_t			clocks;
	mpMatchViewReadiness_t		readiness;
	mpMatchViewTimeoutBudget_t	timeoutBudgets[ MP_MATCH_VIEW_SIDE_COUNT ];
	unsigned char				roleSummaryCount;
	mpMatchViewRoleSummary_t	roleSummaries[ MP_MATCH_VIEW_MAX_ROLE_SUMMARIES ];
	unsigned char				rosterSummaryCount;
	mpMatchViewRosterSummary_t	rosterSummaries[ MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES ];
	unsigned char				participantSummaryCount;
	mpMatchViewParticipantSummary_t participantSummaries[ MP_MATCH_VIEW_MAX_PARTICIPANTS ];
	mpMatchViewProposalSummary_t	globalProposal;
	mpMatchViewSeriesSummary_t	series;
	mpMatchViewEvidenceSummary_t	evidence;
	mpMatchViewTerminalResult_t terminalResult;
	mpMatchViewAllowedOperationMask_t allowedOperations;
	unsigned char				operationAvailabilityCount;
	mpMatchViewOperationAvailability_t operationAvailability[ MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES ];
	mpMatchViewDenial_t			denial;
	mpMatchViewRecipient_t		recipient;
	mpMatchViewCommittedRules_t	committedRules;

	void Clear( void );
} mpMatchViewPublicState_t;

typedef struct mpMatchViewSource_s {
	mpMatchViewPublicState_t	publicState;
	unsigned char				proposalCandidateCount;
	mpMatchViewProposalCandidate_t proposalCandidates[ MP_MATCH_VIEW_SIDE_COUNT ];
	unsigned char				stagedRulesCandidateCount;
	mpMatchViewStagedRulesCandidate_t stagedRulesCandidates[ 4 ];
	unsigned char				rosterSeatCandidateCount;
	mpMatchViewRosterSeatCandidate_t rosterSeatCandidates[ MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ];
	unsigned char				invitationCandidateCount;
	mpMatchViewInvitationCandidate_t invitationCandidates[ MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ];
	unsigned char				queueEntryCandidateCount;
	mpMatchViewQueueEntryCandidate_t queueEntryCandidates[ MP_MATCH_VIEW_MAX_PRIVATE_CANDIDATES ];
	unsigned char				observerCandidateCount;
	mpMatchViewObserverCandidate_t observerCandidates[ MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES ];

	void Clear( void );
} mpMatchViewSource_t;

typedef struct mpSessionView_s {
	mpMatchViewPublicState_t	publicState;
	mpMatchViewProposalSummary_t ownSideProposal;
	mpMatchViewStagedRules_t	stagedRules;
	unsigned char				rosterSeatCount;
	mpMatchViewRosterSeat_t	rosterSeats[ MP_MATCH_VIEW_MAX_ROSTER_SEATS ];
	unsigned char				invitationCount;
	mpMatchViewInvitationSummary_t invitations[ MP_MATCH_VIEW_MAX_INVITATIONS ];
	unsigned char				queueEntryCount;
	mpMatchViewQueueEntry_t	queueEntries[ MP_MATCH_VIEW_MAX_QUEUE_ENTRIES ];
	unsigned char				teamVitalCount;
	mpMatchViewTeamVital_t		teamVitals[ MP_MATCH_VIEW_MAX_TEAM_VITALS ];
	unsigned char				itemTimingCount;
	mpMatchViewItemTiming_t	itemTimings[ MP_MATCH_VIEW_MAX_ITEM_TIMINGS ];
	unsigned char				followTargetCount;
	mpMatchViewFollowTarget_t	followTargets[ MP_MATCH_VIEW_MAX_FOLLOW_TARGETS ];

	void Clear( void );
} mpSessionView;

/*
===============================================================================

	Validation, wire errors and stale-view acceptance

===============================================================================
*/

typedef enum {
	MP_MATCH_VIEW_ERROR_NONE = 0,
	MP_MATCH_VIEW_ERROR_UNSUPPORTED_SCHEMA,
	MP_MATCH_VIEW_ERROR_UNKNOWN_ENVELOPE,
	MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD,
	MP_MATCH_VIEW_ERROR_TRUNCATED,
	MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE,
	MP_MATCH_VIEW_ERROR_BUFFER_TOO_SMALL,
	MP_MATCH_VIEW_ERROR_ALIGNMENT,
	MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD,
	MP_MATCH_VIEW_ERROR_MISSING_REQUIRED_FIELD,
	MP_MATCH_VIEW_ERROR_TRAILING_DATA,
	MP_MATCH_VIEW_ERROR_INVALID_SESSION,
	MP_MATCH_VIEW_ERROR_INVALID_REVISION,
	MP_MATCH_VIEW_ERROR_INVALID_ENUM,
	MP_MATCH_VIEW_ERROR_INVALID_COUNT,
	MP_MATCH_VIEW_ERROR_INVALID_STRING,
	MP_MATCH_VIEW_ERROR_INVALID_STATE,
	MP_MATCH_VIEW_ERROR_INVALID_POLICY,
	MP_MATCH_VIEW_ERROR_CAPACITY,
	MP_MATCH_VIEW_ERROR_STALE,
	MP_MATCH_VIEW_ERROR_COUNT
} mpMatchViewErrorReason_t;

typedef struct mpMatchViewError_s {
	mpMatchViewErrorReason_t	reason;
	unsigned char			fieldId;
	unsigned int			detail;

	void Clear( void );
} mpMatchViewError_t;

typedef enum {
	MP_MATCH_VIEW_ACCEPT_REJECTED_INVALID = 0,
	MP_MATCH_VIEW_ACCEPT_REJECTED_STALE,
	MP_MATCH_VIEW_ACCEPT_NO_CHANGE,
	MP_MATCH_VIEW_ACCEPT_ADVANCED,
	MP_MATCH_VIEW_ACCEPT_REPLACED_SESSION,
	MP_MATCH_VIEW_ACCEPT_RESULT_COUNT
} mpMatchViewAcceptResult_t;

mpMatchViewAudienceMask_t MPMatchViewAudienceBit( mpMatchViewAudience_t audience );
mpMatchViewObserverKindMask_t MPMatchViewObserverKindBit( mpMatchViewObserverKind_t kind );
mpMatchViewPublicRoleMask_t MPMatchViewRoleBit( mpMatchViewPublicRole_t role );
mpMatchViewAllowedOperationMask_t MPMatchViewOperationBit( mpMatchOperationOpcode_t opcode );
mpMatchViewAllowedOperationMask_t MPMatchViewAllOperationBits( void );

// Set one canonical operation decision and keep the convenience mask in sync.
// MP_MATCH_PROTOCOL_REASON_OK means available; every other accepted reason is
// a localized denial.  Clear() pre-populates one denied entry per opcode.
bool MPMatchViewSetOperationDecision( mpMatchViewPublicState_t &state,
	mpMatchOperationOpcode_t opcode, mpMatchProtocolReason_t reason,
	unsigned char fieldId = 0, unsigned int detail = 0 );

bool MPMatchViewValidate( const mpSessionView &view, mpMatchViewError_t *error = 0 );
bool MPMatchViewBuild( const mpMatchViewSource_t &source,
	const mpMatchViewRecipientPolicy_t &policy, mpSessionView &view,
	mpMatchViewError_t *error = 0 );

bool MPMatchViewEncode( idBitMsg &message, const mpSessionView &view,
	mpMatchViewError_t *error = 0 );
bool MPMatchViewDecode( const idBitMsg &message, mpSessionView &view,
	mpMatchViewError_t *error = 0 );

mpMatchViewAcceptResult_t MPMatchViewAccept( mpSessionView &current,
	const mpSessionView &incoming, mpMatchViewError_t *error = 0 );

#endif // __MP_MATCH_VIEW_H__
