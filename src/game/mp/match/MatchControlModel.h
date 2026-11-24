//----------------------------------------------------------------
// MatchControlModel.h
//
// Allocation-free typed model for the multiplayer Match Control GUI.
//
// The model consumes an already accepted, recipient-scoped mpSessionView and
// retains only typed identifiers and bounded machine keys.  Display strings,
// localized labels and player names are deliberately outside this boundary.
//----------------------------------------------------------------

#ifndef __MP_MATCH_CONTROL_MODEL_H__
#define __MP_MATCH_CONTROL_MODEL_H__

#include "MatchRules.h"
#include "MatchSeries.h"
#include "MatchView.h"

static const int MP_MATCH_CONTROL_MAX_TEAM_ROWS = 128;
static const int MP_MATCH_CONTROL_MAX_REPLACEMENT_ROWS = MP_MATCH_VIEW_MAX_PARTICIPANTS;
static const int MP_MATCH_CONTROL_MAX_PROPOSAL_TEMPLATE_ROWS = 7;
static const int MP_MATCH_CONTROL_MAX_PROFILE_ROWS = 16;
static const int MP_MATCH_CONTROL_MAX_RULE_ROWS = MP_MATCH_VIEW_MAX_RULE_FIELDS;
static const int MP_MATCH_CONTROL_MAX_SERIES_MAP_ROWS = MP_MATCH_VIEW_MAX_SERIES_MAP_POOL;
static const int MP_MATCH_CONTROL_MAX_SERIES_HISTORY_ROWS =
	MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY + MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY;
static const int MP_MATCH_CONTROL_MAX_EVIDENCE_ROWS =
	1 + MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS;
static const int MP_MATCH_CONTROL_KEY_BYTES = 48;

// Fixed GUI tokens are parsed once into this closed enum.  Selection/refresh
// commands and secure referee authentication intentionally live outside the
// request builder.
typedef enum {
	MP_MATCH_CONTROL_COMMAND_INVALID = 0,
	MP_MATCH_CONTROL_COMMAND_READY_TOGGLE,
	MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE,
	MP_MATCH_CONTROL_COMMAND_FORCE_READY,
	MP_MATCH_CONTROL_COMMAND_TIMEOUT,
	MP_MATCH_CONTROL_COMMAND_TECH_PAUSE,
	MP_MATCH_CONTROL_COMMAND_RESUME,
	MP_MATCH_CONTROL_COMMAND_REFEREE_LOGOUT,
	MP_MATCH_CONTROL_COMMAND_FORFEIT,
	MP_MATCH_CONTROL_COMMAND_ABORT,
	MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_MARINE,
	MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_STROGG,
	MP_MATCH_CONTROL_COMMAND_TEAM_SPECTATE,
	MP_MATCH_CONTROL_COMMAND_QUEUE_JOIN,
	MP_MATCH_CONTROL_COMMAND_QUEUE_DEFER,
	MP_MATCH_CONTROL_COMMAND_QUEUE_LEAVE,
	MP_MATCH_CONTROL_COMMAND_ROSTER_LEAVE,
	MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT,
	MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE,
	MP_MATCH_CONTROL_COMMAND_ROSTER_REMOVE,
	MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE,
	MP_MATCH_CONTROL_COMMAND_ROLE_ASSIGN,
	MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET,
	MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE,
	MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
	MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES,
	MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO,
	MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN,
	MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL,
	MP_MATCH_CONTROL_COMMAND_RULES_SELECT_PROFILE,
	MP_MATCH_CONTROL_COMMAND_RULES_STAGE_FIELD,
	MP_MATCH_CONTROL_COMMAND_RULES_COMMIT,
	MP_MATCH_CONTROL_COMMAND_RULES_DISCARD,
	MP_MATCH_CONTROL_COMMAND_SERIES_STAGE,
	MP_MATCH_CONTROL_COMMAND_SERIES_START,
	MP_MATCH_CONTROL_COMMAND_SERIES_CANCEL,
	MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE,
	MP_MATCH_CONTROL_COMMAND_VETO_BAN,
	MP_MATCH_CONTROL_COMMAND_VETO_PICK,
	MP_MATCH_CONTROL_COMMAND_VETO_DECIDER,
	MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE,
	MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG,
	MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE,
	MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND,
	MP_MATCH_CONTROL_COMMAND_COUNT
} mpMatchControlCommand_t;

typedef enum {
	MP_MATCH_CONTROL_TEAM_ROW_SIDE = 0,
	MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT,
	MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT,
	MP_MATCH_CONTROL_TEAM_ROW_INVITATION,
	MP_MATCH_CONTROL_TEAM_ROW_QUEUE_ENTRY,
	MP_MATCH_CONTROL_TEAM_ROW_KIND_COUNT
} mpMatchControlTeamRowKind_t;

typedef struct mpMatchControlTeamRow_s {
	mpMatchControlTeamRowKind_t kind;
	int					side;
	mpMatchProtocolParticipantId_t participantId;
	unsigned char		seatIndex;
	mpMatchViewRosterRole_t rosterRole;
	unsigned int		invitationId;
	mpMatchViewQueueState_t queueState;
	unsigned char		queuePosition;
	bool				occupied;
	bool				connected;
	bool				human;
	bool				active;
	mpMatchViewPublicRoleMask_t publicRoleMask;
	bool				teamReady;
	bool				teamLocked;
} mpMatchControlTeamRow_t;

typedef struct mpMatchControlReplacementRow_s {
	mpMatchProtocolParticipantId_t participantId;
	unsigned char			slot;
	int					side;
	bool				connected;
	bool				human;
	bool				active;
	bool				rostered;
	unsigned char		rosterSeatIndex;
	mpMatchViewRosterRole_t rosterRole;
} mpMatchControlReplacementRow_t;

typedef enum {
	MP_MATCH_CONTROL_PROPOSAL_TARGET_NONE = 0,
	MP_MATCH_CONTROL_PROPOSAL_TARGET_PARTICIPANT,
	// The recipient's own side.  It is both the operation's team target and the
	// ballot scope, so only that side's active players vote.
	MP_MATCH_CONTROL_PROPOSAL_TARGET_OWN_SIDE,
	MP_MATCH_CONTROL_PROPOSAL_TARGET_KIND_COUNT
} mpMatchControlProposalTargetKind_t;

typedef struct mpMatchControlProposalTemplateRow_s {
	mpMatchOperationOpcode_t opcode;
	// Ballot scope and operation target are orthogonal.  A participant-removal
	// proposal has a global ballot and a stable ParticipantId target.
	bool				globalOnly;
	mpMatchControlProposalTargetKind_t targetKind;
} mpMatchControlProposalTemplateRow_t;

typedef struct mpMatchControlProfileRow_s {
	mpMatchProfileId_t	profileId;
	unsigned char		keyLength;
	char				key[ MP_MATCH_CONTROL_KEY_BYTES + 1 ];
} mpMatchControlProfileRow_t;

typedef struct mpMatchControlRuleRow_s {
	unsigned char		fieldId;
	mpMatchViewRuleType_t type;
	int				minimumValue;
	int				maximumValue;
	int				committedValue;
	bool				hasStagedValue;
	int				stagedValue;
	bool				editable;
	bool				editValueValid;
	int				editValue;
	unsigned char		keyLength;
	char				key[ MP_MATCH_CONTROL_KEY_BYTES + 1 ];
} mpMatchControlRuleRow_t;

typedef struct mpMatchControlSeriesMapRow_s {
	mpMatchViewSeriesMap_t map;
} mpMatchControlSeriesMapRow_t;

typedef enum {
	MP_MATCH_CONTROL_HISTORY_VETO = 0,
	MP_MATCH_CONTROL_HISTORY_MAP,
	MP_MATCH_CONTROL_HISTORY_KIND_COUNT
} mpMatchControlSeriesHistoryKind_t;

typedef struct mpMatchControlSeriesHistoryRow_s {
	mpMatchControlSeriesHistoryKind_t kind;
	mpMatchViewVetoHistory_t veto;
	mpMatchViewSeriesMapHistory_t map;
} mpMatchControlSeriesHistoryRow_t;

typedef enum {
	MP_MATCH_CONTROL_EVIDENCE_SUMMARY = 0,
	MP_MATCH_CONTROL_EVIDENCE_RECENT_EVENT,
	MP_MATCH_CONTROL_EVIDENCE_ROW_KIND_COUNT
} mpMatchControlEvidenceRowKind_t;

typedef struct mpMatchControlEvidenceRow_s {
	mpMatchControlEvidenceRowKind_t kind;
	mpMatchViewEvidenceSummary_t summary;
	unsigned char			recentEventIndex;
	mpMatchViewEvidenceEventKind_t recentEventKind;
} mpMatchControlEvidenceRow_t;

typedef enum {
	MP_MATCH_CONTROL_PROPOSAL_GLOBAL = 0,
	MP_MATCH_CONTROL_PROPOSAL_OWN_SIDE,
	MP_MATCH_CONTROL_PROPOSAL_CHOICE_COUNT
} mpMatchControlProposalChoice_t;

// One deliberately context-neutral side selector serves both gameplay teams
// (Marine/Strogg) and stable Duel competition sides (A/B).  The protocol uses
// the same bounded 0/1 target domain for both; presentation chooses the label
// appropriate to the current mode.  NONE means "use my own side" for a
// contestant, and requires an explicit choice from a neutral authority.
typedef enum {
	MP_MATCH_CONTROL_SIDE_CHOICE_NONE = -1,
	MP_MATCH_CONTROL_SIDE_CHOICE_ZERO = 0,
	MP_MATCH_CONTROL_SIDE_CHOICE_ONE = 1,
	MP_MATCH_CONTROL_SIDE_CHOICE_COUNT
} mpMatchControlSideChoice_t;

typedef enum {
	MP_MATCH_CONTROL_INGEST_REJECTED = 0,
	MP_MATCH_CONTROL_INGEST_NO_CHANGE,
	MP_MATCH_CONTROL_INGEST_UPDATED,
	MP_MATCH_CONTROL_INGEST_REPLACED_SESSION,
	MP_MATCH_CONTROL_INGEST_RESULT_COUNT
} mpMatchControlIngestResult_t;

typedef enum {
	MP_MATCH_CONTROL_ERROR_NONE = 0,
	MP_MATCH_CONTROL_ERROR_INVALID_VIEW,
	MP_MATCH_CONTROL_ERROR_STALE_VIEW,
	MP_MATCH_CONTROL_ERROR_CAPACITY,
	MP_MATCH_CONTROL_ERROR_UNKNOWN_COMMAND,
	MP_MATCH_CONTROL_ERROR_INVALID_REQUEST_ID,
	MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE,
	MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
	MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
	MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
	MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
	MP_MATCH_CONTROL_ERROR_PROPOSAL_MISSING,
	MP_MATCH_CONTROL_ERROR_PROTOCOL_INVALID,
	MP_MATCH_CONTROL_ERROR_COUNT
} mpMatchControlErrorReason_t;

typedef struct mpMatchControlError_s {
	mpMatchControlErrorReason_t reason;
	mpMatchOperationOpcode_t opcode;
	mpMatchProtocolReason_t protocolReason;
	mpMatchViewErrorReason_t viewReason;
	int				rowIndex;
	unsigned char		fieldId;
	unsigned int		detail;

	void Clear( void );
} mpMatchControlError_t;

bool MPMatchControlCommandFromToken( const char *token,
	mpMatchControlCommand_t &command );
const char *MPMatchControlCommandToken( mpMatchControlCommand_t command );
mpMatchOperationOpcode_t MPMatchControlCommandOpcode(
	mpMatchControlCommand_t command );

class mpMatchControlModel {
public:
	mpMatchControlModel();
	void Clear( void );

	mpMatchControlIngestResult_t IngestAcceptedView( const mpSessionView &view,
		mpMatchControlError_t *error = 0 );
	bool IsReady( void ) const { return ready; }
	mpMatchProtocolSessionId_t SessionId( void ) const { return sessionId; }
	mpMatchProtocolRevision_t ViewRevision( void ) const { return viewRevision; }
	const mpMatchViewRecipient_t &Recipient( void ) const { return recipient; }

	const mpMatchViewOperationAvailability_t *OperationAvailability(
		mpMatchOperationOpcode_t opcode ) const;
	const mpMatchViewOperationAvailability_t *CommandAvailability(
		mpMatchControlCommand_t command ) const;
	// The accepted view carries the recipient-scoped authorization decision,
	// while a small number of series operations also have state-machine guards
	// that are narrower than their protocol phase mask.  Presentation must use
	// both so it never advertises an action the typed executor will reject.
	bool OperationContextAccepted( mpMatchOperationOpcode_t opcode ) const;

	int TeamRowCount( void ) const { return teamRowCount; }
	const mpMatchControlTeamRow_t *TeamRow( int index ) const;
	int ReplacementRowCount( void ) const { return replacementRowCount; }
	const mpMatchControlReplacementRow_t *ReplacementRow( int index ) const;
	int ProposalTemplateRowCount( void ) const { return proposalTemplateRowCount; }
	const mpMatchControlProposalTemplateRow_t *ProposalTemplateRow( int index ) const;
	int ProfileRowCount( void ) const { return profileRowCount; }
	const mpMatchControlProfileRow_t *ProfileRow( int index ) const;
	int RuleRowCount( void ) const { return ruleRowCount; }
	const mpMatchControlRuleRow_t *RuleRow( int index ) const;
	int SeriesMapRowCount( void ) const { return seriesMapRowCount; }
	const mpMatchControlSeriesMapRow_t *SeriesMapRow( int index ) const;
	int SeriesHistoryRowCount( void ) const { return seriesHistoryRowCount; }
	const mpMatchControlSeriesHistoryRow_t *SeriesHistoryRow( int index ) const;
	int EvidenceRowCount( void ) const { return evidenceRowCount; }
	const mpMatchControlEvidenceRow_t *EvidenceRow( int index ) const;

	bool SelectTeamRow( int index );
	bool SelectReplacementRow( int index );
	bool SelectProposalTemplateRow( int index );
	bool SelectProfileRow( int index );
	bool SelectRuleRow( int index );
	bool SelectSeriesMapRow( int index );

	int SelectedTeamRow( void ) const { return selectedTeamRow; }
	int SelectedReplacementRow( void ) const { return selectedReplacementRow; }
	int SelectedProposalTemplateRow( void ) const { return selectedProposalTemplateRow; }
	int SelectedProfileRow( void ) const { return selectedProfileRow; }
	int SelectedRuleRow( void ) const { return selectedRuleRow; }
	int SelectedSeriesMapRow( void ) const { return selectedSeriesMapRow; }

	bool SetRoleChoice( mpMatchProtocolRosterRole_t role );
	mpMatchProtocolRosterRole_t RoleChoice( void ) const { return roleChoice; }
	bool SetProposalChoice( mpMatchControlProposalChoice_t choice );
	mpMatchControlProposalChoice_t ProposalChoice( void ) const { return proposalChoice; }
	bool SetActionSideChoice( mpMatchControlSideChoice_t choice );
	mpMatchControlSideChoice_t ActionSideChoice( void ) const {
		return actionSideChoice;
	}
	// Presentation may expose fixed side-selection controls, but it must not
	// reconstruct authority from role labels.  These queries use the same
	// accepted recipient view and authority rules as request construction.
	bool CanChooseActionSide( int side ) const;
	bool ActionSideUsesCompetitionLabels( void ) const;
	bool SetSeriesProfileChoice( mpSeriesProfileId_t profile );
	mpSeriesProfileId_t SeriesProfileChoice( void ) const { return seriesProfileChoice; }
	bool SetSelectedRuleValue( int value );

	// requestId must be a caller-owned, nonzero monotonically increasing value.
	// Output is transactional and remains unchanged on failure.
	bool BuildRequest( mpMatchControlCommand_t command, unsigned int requestId,
		mpMatchOperationRequest_t &request, mpMatchControlError_t *error = 0 ) const;

private:
	bool BuildRows( const mpSessionView &view, mpMatchControlError_t *error );
	bool BuildTeamRows( const mpSessionView &view, mpMatchControlError_t *error );
	bool BuildReplacementRows( const mpSessionView &view,
		mpMatchControlError_t *error );
	bool BuildProfileRows( const mpSessionView &view, mpMatchControlError_t *error );
	bool BuildRuleRows( const mpSessionView &view, mpMatchControlError_t *error );
	bool BuildProposalTemplateRows( mpMatchControlError_t *error );
	bool BuildSeriesRows( const mpSessionView &view, mpMatchControlError_t *error );
	bool BuildEvidenceRows( const mpSessionView &view, mpMatchControlError_t *error );
	void RestoreSelectionsFrom( const mpMatchControlModel &previous );
	bool HasProjectedGlobalAuthority( void ) const;
	bool CanManageSide( int side ) const;
	int DefaultActionSide( void ) const;
	bool ResolveActionSide( bool requireKnownTeam, bool allowCompetitionSide,
		int &side ) const;
	bool ParticipantIsRostered( mpMatchProtocolParticipantId_t participantId ) const;

	bool ready;
	mpMatchProtocolSessionId_t sessionId;
	mpMatchProtocolRevision_t sessionRevision;
	mpMatchProtocolRevision_t controlRevision;
	mpMatchProtocolRevision_t viewRevision;
	mpGameState_t phase;
	mpMatchViewRecipient_t recipient;
	mpMatchViewProposalSummary_t globalProposal;
	mpMatchViewProposalSummary_t ownSideProposal;
	mpMatchViewSeriesSummary_t series;
	mpMatchViewOperationAvailability_t availability[ MP_MATCH_OP_COUNT ];
	bool teamKnown[ MP_MATCH_VIEW_SIDE_COUNT ];
	bool teamReady[ MP_MATCH_VIEW_SIDE_COUNT ];
	bool teamLocked[ MP_MATCH_VIEW_SIDE_COUNT ];

	mpMatchControlTeamRow_t teamRows[ MP_MATCH_CONTROL_MAX_TEAM_ROWS ];
	int teamRowCount;
	mpMatchControlReplacementRow_t replacementRows[
		MP_MATCH_CONTROL_MAX_REPLACEMENT_ROWS ];
	int replacementRowCount;
	mpMatchControlProposalTemplateRow_t proposalTemplateRows[
		MP_MATCH_CONTROL_MAX_PROPOSAL_TEMPLATE_ROWS ];
	int proposalTemplateRowCount;
	mpMatchControlProfileRow_t profileRows[ MP_MATCH_CONTROL_MAX_PROFILE_ROWS ];
	int profileRowCount;
	mpMatchControlRuleRow_t ruleRows[ MP_MATCH_CONTROL_MAX_RULE_ROWS ];
	int ruleRowCount;
	mpMatchControlSeriesMapRow_t seriesMapRows[
		MP_MATCH_CONTROL_MAX_SERIES_MAP_ROWS ];
	int seriesMapRowCount;
	mpMatchControlSeriesHistoryRow_t seriesHistoryRows[
		MP_MATCH_CONTROL_MAX_SERIES_HISTORY_ROWS ];
	int seriesHistoryRowCount;
	mpMatchControlEvidenceRow_t evidenceRows[ MP_MATCH_CONTROL_MAX_EVIDENCE_ROWS ];
	int evidenceRowCount;

	int selectedTeamRow;
	int selectedReplacementRow;
	int selectedProposalTemplateRow;
	int selectedProfileRow;
	int selectedRuleRow;
	int selectedSeriesMapRow;
	mpMatchProtocolRosterRole_t roleChoice;
	mpMatchControlProposalChoice_t proposalChoice;
	mpMatchControlSideChoice_t actionSideChoice;
	bool actionSideChoiceExplicit;
	mpSeriesProfileId_t seriesProfileChoice;
};

#endif // __MP_MATCH_CONTROL_MODEL_H__
