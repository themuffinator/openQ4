//----------------------------------------------------------------
// MatchControlLocalization.cpp
//----------------------------------------------------------------

#if !defined( MP_MATCH_CONTROL_LOCALIZATION_STANDALONE_TEST )
#include "../../../idlib/precompiled.h"
#pragma hdrstop
#endif

#include "MatchControlLocalization.h"

namespace {

static const char UNKNOWN_KEY[] = "#str_42301";
static const char NO_DETAIL_KEY[] = "#str_42302";

}

// These assertions deliberately turn an appended wire/view value into a build
// failure until this closed mapping and its four translations are reviewed.
static_assert( MP_MATCH_LOCALIZATION_COUNT == 2033,
	"Match protocol localization ids require an explicit localization review" );
static_assert( MP_MATCH_OP_COUNT == 37,
	"Match operations require an explicit localization review" );
static_assert( MP_MATCH_PROTOCOL_REASON_COUNT == 33,
	"Match protocol reasons require an explicit localization review" );
static_assert( STATE_COUNT == 7,
	"Match phases require an explicit localization review" );
static_assert( RS_STATE_COUNT == 4,
	"Match rounds require an explicit localization review" );
static_assert( MP_MATCH_VIEW_PAUSE_STATE_COUNT == 4,
	"Match pause states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_PAUSE_KIND_COUNT == 3,
	"Match pause kinds require an explicit localization review" );
static_assert( MP_MATCH_VIEW_PAUSE_REASON_COUNT == 6,
	"Match pause reasons require an explicit localization review" );
static_assert( MP_MATCH_VIEW_RESUME_POLICY_COUNT == 3,
	"Match resume policies require an explicit localization review" );
static_assert( MP_MATCH_VIEW_ROLE_COUNT == 6,
	"Match public roles require an explicit localization review" );
static_assert( MP_MATCH_VIEW_ROSTER_ROLE_COUNT == 4,
	"Match roster roles require an explicit localization review" );
static_assert( MP_MATCH_VIEW_QUEUE_STATE_COUNT == 4,
	"Match queue states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_PROPOSAL_SCOPE_COUNT == 2,
	"Match proposal scopes require an explicit localization review" );
static_assert( MP_MATCH_VIEW_BALLOT_COUNT == 4,
	"Match ballots require an explicit localization review" );
static_assert( MP_MATCH_VIEW_SERIES_STATE_COUNT == 8,
	"Match series states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_VETO_ACTION_COUNT == 4,
	"Match veto actions require an explicit localization review" );
static_assert( MP_MATCH_VIEW_MAP_DISPOSITION_COUNT == 3,
	"Match map dispositions require an explicit localization review" );
static_assert( MP_MATCH_VIEW_MAP_OUTCOME_COUNT == 4,
	"Match map outcomes require an explicit localization review" );
static_assert( MP_MATCH_VIEW_RULE_TYPE_COUNT == 3,
	"Match rule types require an explicit localization review" );
static_assert( MP_MATCH_VIEW_RULES_BOUNDARY_COUNT == 2,
	"Match rules boundaries require an explicit localization review" );
static_assert( MP_MATCH_VIEW_EVIDENCE_STATE_COUNT == 4,
	"Match evidence states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_MVD_STATE_COUNT == 5,
	"Match MVD states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_REPORT_STATE_COUNT == 4,
	"Match report states require an explicit localization review" );
static_assert( MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT == 9,
	"Match evidence event kinds require an explicit localization review" );
static_assert( MP_MATCH_RESULT_STATUS_COUNT == 4,
	"Match operation result states require an explicit localization review" );
static_assert( MP_MATCH_TEAM_COUNT == 4,
	"Match teams require an explicit localization review" );
static_assert( MP_MATCH_BLOCKER_COUNT == 12,
	"Match readiness blockers require an explicit localization review" );
static_assert( MP_RULE_FIELD_COUNT == 34,
	"Match rule fields require an explicit localization review" );
static_assert( MP_MATCH_PROFILE_COUNT == 8,
	"Match profiles require an explicit localization review" );
static_assert( MP_SERIES_PROFILE_COUNT == 3,
	"Match series profiles require an explicit localization review" );
static_assert( MP_MATCH_CONTROL_ERROR_COUNT == 13,
	"Match Control errors require an explicit localization review" );

const char *MPMatchControlLocalizationKey( mpMatchLocalizationId_t localizationId ) {
	switch ( localizationId ) {
		case MP_MATCH_LOCALIZATION_NONE: return NO_DETAIL_KEY;
		case MP_MATCH_LOCALIZATION_OPERATION_BASE: return UNKNOWN_KEY;
		case MP_MATCH_LOCALIZATION_OPERATION_READY_SET: return "#str_42310";
		case MP_MATCH_LOCALIZATION_OPERATION_TEAM_READY_SET: return "#str_42311";
		case MP_MATCH_LOCALIZATION_OPERATION_FORCE_READY: return "#str_42312";
		case MP_MATCH_LOCALIZATION_OPERATION_TEAM_JOIN: return "#str_42313";
		case MP_MATCH_LOCALIZATION_OPERATION_TEAM_LOCK_SET: return "#str_42314";
		case MP_MATCH_LOCALIZATION_OPERATION_QUEUE_JOIN: return "#str_42315";
		case MP_MATCH_LOCALIZATION_OPERATION_QUEUE_DEFER: return "#str_42316";
		case MP_MATCH_LOCALIZATION_OPERATION_QUEUE_LEAVE: return "#str_42317";
		case MP_MATCH_LOCALIZATION_OPERATION_TIMEOUT_REQUEST: return "#str_42318";
		case MP_MATCH_LOCALIZATION_OPERATION_TECH_PAUSE_REQUEST: return "#str_42319";
		case MP_MATCH_LOCALIZATION_OPERATION_RESUME_REQUEST: return "#str_42320";
		case MP_MATCH_LOCALIZATION_OPERATION_REF_AUTHENTICATE: return "#str_42321";
		case MP_MATCH_LOCALIZATION_OPERATION_REF_LOGOUT: return "#str_42322";
		case MP_MATCH_LOCALIZATION_OPERATION_RULES_SELECT_PROFILE: return "#str_42323";
		case MP_MATCH_LOCALIZATION_OPERATION_RULES_STAGE_FIELD: return "#str_42324";
		case MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT: return "#str_42325";
		case MP_MATCH_LOCALIZATION_OPERATION_RULES_DISCARD: return "#str_42326";
		case MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CREATE: return "#str_42327";
		case MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CAST: return "#str_42328";
		case MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CANCEL: return "#str_42329";
		case MP_MATCH_LOCALIZATION_OPERATION_ROSTER_INVITE: return "#str_42330";
		case MP_MATCH_LOCALIZATION_OPERATION_ROSTER_ACCEPT: return "#str_42331";
		case MP_MATCH_LOCALIZATION_OPERATION_ROSTER_REMOVE: return "#str_42332";
		case MP_MATCH_LOCALIZATION_OPERATION_ROSTER_SUBSTITUTE: return "#str_42333";
		case MP_MATCH_LOCALIZATION_OPERATION_ROLE_ASSIGN: return "#str_42334";
		case MP_MATCH_LOCALIZATION_OPERATION_SERIES_STAGE_PROFILE: return "#str_42335";
		case MP_MATCH_LOCALIZATION_OPERATION_SERIES_START: return "#str_42336";
		case MP_MATCH_LOCALIZATION_OPERATION_SERIES_CANCEL: return "#str_42337";
		case MP_MATCH_LOCALIZATION_OPERATION_SERIES_ADVANCE: return "#str_42338";
		case MP_MATCH_LOCALIZATION_OPERATION_VETO_SELECT: return "#str_42339";
		case MP_MATCH_LOCALIZATION_OPERATION_FORFEIT: return "#str_42340";
		case MP_MATCH_LOCALIZATION_OPERATION_ABORT: return "#str_42341";
		case MP_MATCH_LOCALIZATION_OPERATION_BROADCASTER_SET: return "#str_42342";
		case MP_MATCH_LOCALIZATION_OPERATION_ROSTER_LEAVE: return "#str_42343";
		case MP_MATCH_LOCALIZATION_OPERATION_PARTICIPANT_REMOVE: return "#str_42344";
		case MP_MATCH_LOCALIZATION_OPERATION_SERIES_CONTESTANT_BIND: return "#str_42345";
		case MP_MATCH_LOCALIZATION_CONFIRM_BASE: return UNKNOWN_KEY;
		case MP_MATCH_LOCALIZATION_CONFIRM_PARTICIPANT_REMOVE: return "#str_42349";
		case MP_MATCH_LOCALIZATION_CONFIRM_FORCE_READY: return "#str_42350";
		case MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT: return "#str_42351";
		case MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_REMOVE: return "#str_42352";
		case MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_SUBSTITUTE: return "#str_42353";
		case MP_MATCH_LOCALIZATION_CONFIRM_SERIES_CANCEL: return "#str_42354";
		case MP_MATCH_LOCALIZATION_CONFIRM_FORFEIT: return "#str_42355";
		case MP_MATCH_LOCALIZATION_CONFIRM_ABORT: return "#str_42356";
		case MP_MATCH_LOCALIZATION_CONFIRM_SERIES_START: return "#str_42357";
		case MP_MATCH_LOCALIZATION_CONFIRM_SERIES_ADVANCE: return "#str_42358";
		case MP_MATCH_LOCALIZATION_CONFIRM_VETO_SELECT: return "#str_42359";
		case MP_MATCH_LOCALIZATION_REASON_BASE: return UNKNOWN_KEY;
		case MP_MATCH_LOCALIZATION_REASON_OK: return "#str_42360";
		case MP_MATCH_LOCALIZATION_REASON_UNSUPPORTED_SCHEMA: return "#str_42361";
		case MP_MATCH_LOCALIZATION_REASON_UNKNOWN_ENVELOPE: return "#str_42362";
		case MP_MATCH_LOCALIZATION_REASON_UNKNOWN_OPCODE: return "#str_42363";
		case MP_MATCH_LOCALIZATION_REASON_UNKNOWN_FIELD: return "#str_42364";
		case MP_MATCH_LOCALIZATION_REASON_TRUNCATED: return "#str_42365";
		case MP_MATCH_LOCALIZATION_REASON_PAYLOAD_TOO_LARGE: return "#str_42366";
		case MP_MATCH_LOCALIZATION_REASON_BUFFER_TOO_SMALL: return "#str_42367";
		case MP_MATCH_LOCALIZATION_REASON_ARGUMENT_COUNT: return "#str_42368";
		case MP_MATCH_LOCALIZATION_REASON_ARGUMENT_TYPE: return "#str_42369";
		case MP_MATCH_LOCALIZATION_REASON_ARGUMENT_RANGE: return "#str_42370";
		case MP_MATCH_LOCALIZATION_REASON_STRING_LENGTH: return "#str_42371";
		case MP_MATCH_LOCALIZATION_REASON_STRING_CHARACTERS: return "#str_42372";
		case MP_MATCH_LOCALIZATION_REASON_DUPLICATE_FIELD: return "#str_42373";
		case MP_MATCH_LOCALIZATION_REASON_TRAILING_DATA: return "#str_42374";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_SESSION_ID: return "#str_42375";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_REQUEST_ID: return "#str_42376";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_ACTOR_SLOT: return "#str_42377";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_BINDING_GENERATION: return "#str_42378";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_PARTICIPANT: return "#str_42379";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_TEAM: return "#str_42380";
		case MP_MATCH_LOCALIZATION_REASON_INVALID_TARGET: return "#str_42381";
		case MP_MATCH_LOCALIZATION_REASON_NOT_PROPOSABLE: return "#str_42382";
		case MP_MATCH_LOCALIZATION_REASON_REGISTRY_INVALID: return "#str_42383";
		case MP_MATCH_LOCALIZATION_REASON_NOT_AUTHORIZED: return "#str_42384";
		case MP_MATCH_LOCALIZATION_REASON_ILLEGAL_PHASE: return "#str_42385";
		case MP_MATCH_LOCALIZATION_REASON_STALE_REVISION: return "#str_42386";
		case MP_MATCH_LOCALIZATION_REASON_CONFLICT: return "#str_42387";
		case MP_MATCH_LOCALIZATION_REASON_COOLDOWN: return "#str_42388";
		case MP_MATCH_LOCALIZATION_REASON_INTERNAL: return "#str_42389";
		case MP_MATCH_LOCALIZATION_REASON_ALIGNMENT: return "#str_42390";
		case MP_MATCH_LOCALIZATION_REASON_TEAM_BALANCE: return "#str_42391";
		case MP_MATCH_LOCALIZATION_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlProtocolReasonKey( mpMatchProtocolReason_t reason ) {
	switch ( reason ) {
		case MP_MATCH_PROTOCOL_REASON_NONE: return NO_DETAIL_KEY;
		case MP_MATCH_PROTOCOL_REASON_OK: return "#str_42360";
		case MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA: return "#str_42361";
		case MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE: return "#str_42362";
		case MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE: return "#str_42363";
		case MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD: return "#str_42364";
		case MP_MATCH_PROTOCOL_REASON_TRUNCATED: return "#str_42365";
		case MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE: return "#str_42366";
		case MP_MATCH_PROTOCOL_REASON_BUFFER_TOO_SMALL: return "#str_42367";
		case MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT: return "#str_42368";
		case MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE: return "#str_42369";
		case MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE: return "#str_42370";
		case MP_MATCH_PROTOCOL_REASON_STRING_LENGTH: return "#str_42371";
		case MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS: return "#str_42372";
		case MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD: return "#str_42373";
		case MP_MATCH_PROTOCOL_REASON_TRAILING_DATA: return "#str_42374";
		case MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID: return "#str_42375";
		case MP_MATCH_PROTOCOL_REASON_INVALID_REQUEST_ID: return "#str_42376";
		case MP_MATCH_PROTOCOL_REASON_INVALID_ACTOR_SLOT: return "#str_42377";
		case MP_MATCH_PROTOCOL_REASON_INVALID_BINDING_GENERATION: return "#str_42378";
		case MP_MATCH_PROTOCOL_REASON_INVALID_PARTICIPANT: return "#str_42379";
		case MP_MATCH_PROTOCOL_REASON_INVALID_TEAM: return "#str_42380";
		case MP_MATCH_PROTOCOL_REASON_INVALID_TARGET: return "#str_42381";
		case MP_MATCH_PROTOCOL_REASON_NOT_PROPOSABLE: return "#str_42382";
		case MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID: return "#str_42383";
		case MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED: return "#str_42384";
		case MP_MATCH_PROTOCOL_REASON_ILLEGAL_PHASE: return "#str_42385";
		case MP_MATCH_PROTOCOL_REASON_STALE_REVISION: return "#str_42386";
		case MP_MATCH_PROTOCOL_REASON_CONFLICT: return "#str_42387";
		case MP_MATCH_PROTOCOL_REASON_COOLDOWN: return "#str_42388";
		case MP_MATCH_PROTOCOL_REASON_INTERNAL: return "#str_42389";
		case MP_MATCH_PROTOCOL_REASON_ALIGNMENT: return "#str_42390";
		case MP_MATCH_PROTOCOL_REASON_TEAM_BALANCE: return "#str_42391";
		case MP_MATCH_PROTOCOL_REASON_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlPhaseKey( mpGameState_t phase ) {
	switch ( phase ) {
		case INACTIVE: return "#str_42400";
		case WARMUP: return "#str_42401";
		case COUNTDOWN: return "#str_42402";
		case GAMEON: return "#str_42403";
		case SUDDENDEATH: return "#str_42404";
		case GAMEREVIEW: return "#str_42405";
		case NEXTGAME: return "#str_42406";
		case STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlRoundKey( roundState_t round ) {
	switch ( round ) {
		case RS_INACTIVE: return "#str_42410";
		case RS_COUNTDOWN: return "#str_42411";
		case RS_ACTIVE: return "#str_42412";
		case RS_COMPLETE: return "#str_42413";
		case RS_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlPauseStateKey( mpMatchViewPauseState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_PAUSE_RUNNING: return "#str_42420";
		case MP_MATCH_VIEW_PAUSE_PENDING: return "#str_42421";
		case MP_MATCH_VIEW_PAUSED: return "#str_42422";
		case MP_MATCH_VIEW_RESUME_COUNTDOWN: return "#str_42423";
		case MP_MATCH_VIEW_PAUSE_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlPauseKindKey( mpMatchViewPauseKind_t kind ) {
	switch ( kind ) {
		case MP_MATCH_VIEW_PAUSE_KIND_NONE: return "#str_42430";
		case MP_MATCH_VIEW_PAUSE_KIND_TEAM_TIMEOUT: return "#str_42431";
		case MP_MATCH_VIEW_PAUSE_KIND_TECHNICAL: return "#str_42432";
		case MP_MATCH_VIEW_PAUSE_KIND_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlPauseReasonKey( mpMatchViewPauseReason_t reason ) {
	switch ( reason ) {
		case MP_MATCH_VIEW_PAUSE_REASON_NONE: return "#str_42440";
		case MP_MATCH_VIEW_PAUSE_REASON_TACTICAL: return "#str_42441";
		case MP_MATCH_VIEW_PAUSE_REASON_PLAYER_DISCONNECT: return "#str_42442";
		case MP_MATCH_VIEW_PAUSE_REASON_TECHNICAL_FAULT: return "#str_42443";
		case MP_MATCH_VIEW_PAUSE_REASON_SERVER_FAULT: return "#str_42444";
		case MP_MATCH_VIEW_PAUSE_REASON_REFEREE: return "#str_42445";
		case MP_MATCH_VIEW_PAUSE_REASON_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlResumePolicyKey( mpMatchViewResumePolicy_t policy ) {
	switch ( policy ) {
		case MP_MATCH_VIEW_RESUME_OWNER_OR_REFEREE: return "#str_42450";
		case MP_MATCH_VIEW_RESUME_BOTH_SIDES_OR_REFEREE: return "#str_42451";
		case MP_MATCH_VIEW_RESUME_REFEREE_ONLY: return "#str_42452";
		case MP_MATCH_VIEW_RESUME_POLICY_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlPublicRoleKey( mpMatchViewPublicRole_t role ) {
	switch ( role ) {
		case MP_MATCH_VIEW_ROLE_NONE: return "#str_42460";
		case MP_MATCH_VIEW_ROLE_PLAYER: return "#str_42461";
		case MP_MATCH_VIEW_ROLE_CAPTAIN: return "#str_42462";
		case MP_MATCH_VIEW_ROLE_COACH: return "#str_42463";
		case MP_MATCH_VIEW_ROLE_BROADCASTER: return "#str_42464";
		case MP_MATCH_VIEW_ROLE_REFEREE: return "#str_42465";
		case MP_MATCH_VIEW_ROLE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlRosterRoleKey( mpMatchViewRosterRole_t role ) {
	switch ( role ) {
		case MP_MATCH_VIEW_ROSTER_PLAYER: return "#str_42470";
		case MP_MATCH_VIEW_ROSTER_CAPTAIN: return "#str_42471";
		case MP_MATCH_VIEW_ROSTER_COACH: return "#str_42472";
		case MP_MATCH_VIEW_ROSTER_SUBSTITUTE: return "#str_42473";
		case MP_MATCH_VIEW_ROSTER_ROLE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlProtocolRosterRoleKey( mpMatchProtocolRosterRole_t role ) {
	switch ( role ) {
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER: return "#str_42470";
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN: return "#str_42471";
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH: return "#str_42472";
		case MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE: return "#str_42473";
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlQueueStateKey( mpMatchViewQueueState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_QUEUE_NONE: return "#str_42480";
		case MP_MATCH_VIEW_QUEUE_WAITING: return "#str_42481";
		case MP_MATCH_VIEW_QUEUE_DEFERRED: return "#str_42482";
		case MP_MATCH_VIEW_QUEUE_ADMITTED: return "#str_42483";
		case MP_MATCH_VIEW_QUEUE_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlProposalScopeKey( mpMatchViewProposalScope_t scope ) {
	switch ( scope ) {
		case MP_MATCH_VIEW_PROPOSAL_GLOBAL: return "#str_42490";
		case MP_MATCH_VIEW_PROPOSAL_SIDE: return "#str_42491";
		case MP_MATCH_VIEW_PROPOSAL_SCOPE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlBallotKey( mpMatchViewBallot_t ballot ) {
	switch ( ballot ) {
		case MP_MATCH_VIEW_BALLOT_NONE: return "#str_42500";
		case MP_MATCH_VIEW_BALLOT_YES: return "#str_42501";
		case MP_MATCH_VIEW_BALLOT_NO: return "#str_42502";
		case MP_MATCH_VIEW_BALLOT_ABSTAIN: return "#str_42503";
		case MP_MATCH_VIEW_BALLOT_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlProtocolBallotKey( mpMatchBallotChoice_t ballot ) {
	switch ( ballot ) {
		case MP_MATCH_BALLOT_YES: return "#str_42501";
		case MP_MATCH_BALLOT_NO: return "#str_42502";
		case MP_MATCH_BALLOT_ABSTAIN: return "#str_42503";
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlSeriesStateKey( mpMatchViewSeriesState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_SERIES_DISABLED: return "#str_42510";
		case MP_MATCH_VIEW_SERIES_SETUP: return "#str_42511";
		case MP_MATCH_VIEW_SERIES_VETO: return "#str_42512";
		case MP_MATCH_VIEW_SERIES_READY: return "#str_42513";
		case MP_MATCH_VIEW_SERIES_MAP_ACTIVE: return "#str_42514";
		case MP_MATCH_VIEW_SERIES_MAP_COMPLETE: return "#str_42515";
		case MP_MATCH_VIEW_SERIES_COMPLETE: return "#str_42516";
		case MP_MATCH_VIEW_SERIES_CANCELLED: return "#str_42517";
		case MP_MATCH_VIEW_SERIES_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlVetoActionKey( mpMatchViewVetoAction_t action ) {
	switch ( action ) {
		case MP_MATCH_VIEW_VETO_BAN: return "#str_42520";
		case MP_MATCH_VIEW_VETO_PICK: return "#str_42521";
		case MP_MATCH_VIEW_VETO_SIDE: return "#str_42522";
		case MP_MATCH_VIEW_VETO_DECIDER: return "#str_42523";
		case MP_MATCH_VIEW_VETO_ACTION_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlProtocolVetoActionKey( mpMatchVetoAction_t action ) {
	switch ( action ) {
		case MP_MATCH_VETO_BAN: return "#str_42520";
		case MP_MATCH_VETO_PICK: return "#str_42521";
		case MP_MATCH_VETO_SIDE: return "#str_42522";
		case MP_MATCH_VETO_DECIDER: return "#str_42523";
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlMapDispositionKey( mpMatchViewMapDisposition_t disposition ) {
	switch ( disposition ) {
		case MP_MATCH_VIEW_MAP_AVAILABLE: return "#str_42530";
		case MP_MATCH_VIEW_MAP_BANNED: return "#str_42531";
		case MP_MATCH_VIEW_MAP_SELECTED: return "#str_42532";
		case MP_MATCH_VIEW_MAP_DISPOSITION_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlMapOutcomeKey( mpMatchViewMapOutcome_t outcome ) {
	switch ( outcome ) {
		case MP_MATCH_VIEW_MAP_UNPLAYED: return "#str_42540";
		case MP_MATCH_VIEW_MAP_DECIDED: return "#str_42541";
		case MP_MATCH_VIEW_MAP_FORFEIT: return "#str_42542";
		case MP_MATCH_VIEW_MAP_ABORTED: return "#str_42543";
		case MP_MATCH_VIEW_MAP_OUTCOME_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlRuleTypeKey( mpMatchViewRuleType_t type ) {
	switch ( type ) {
		case MP_MATCH_VIEW_RULE_BOOL: return "#str_42550";
		case MP_MATCH_VIEW_RULE_INTEGER: return "#str_42551";
		case MP_MATCH_VIEW_RULE_ENUM: return "#str_42552";
		case MP_MATCH_VIEW_RULE_TYPE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlRulesBoundaryKey( mpMatchViewRulesBoundary_t boundary ) {
	switch ( boundary ) {
		case MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT: return "#str_42560";
		case MP_MATCH_VIEW_RULES_FROZEN_FOR_MAP: return "#str_42561";
		case MP_MATCH_VIEW_RULES_BOUNDARY_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlEvidenceStateKey( mpMatchViewEvidenceState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_EVIDENCE_DISABLED: return "#str_42570";
		case MP_MATCH_VIEW_EVIDENCE_CAPTURING: return "#str_42571";
		case MP_MATCH_VIEW_EVIDENCE_FINALIZED: return "#str_42572";
		case MP_MATCH_VIEW_EVIDENCE_FAILED: return "#str_42573";
		case MP_MATCH_VIEW_EVIDENCE_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlMVDStateKey( mpMatchViewMVDState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_MVD_DISABLED: return "#str_42580";
		case MP_MATCH_VIEW_MVD_PENDING: return "#str_42581";
		case MP_MATCH_VIEW_MVD_RECORDING: return "#str_42582";
		case MP_MATCH_VIEW_MVD_AVAILABLE: return "#str_42583";
		case MP_MATCH_VIEW_MVD_FAILED: return "#str_42584";
		case MP_MATCH_VIEW_MVD_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlReportStateKey( mpMatchViewReportState_t state ) {
	switch ( state ) {
		case MP_MATCH_VIEW_REPORT_DISABLED: return "#str_42590";
		case MP_MATCH_VIEW_REPORT_PENDING: return "#str_42591";
		case MP_MATCH_VIEW_REPORT_AVAILABLE: return "#str_42592";
		case MP_MATCH_VIEW_REPORT_FAILED: return "#str_42593";
		case MP_MATCH_VIEW_REPORT_STATE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlEvidenceEventKindKey( mpMatchViewEvidenceEventKind_t kind ) {
	switch ( kind ) {
		case MP_MATCH_VIEW_EVIDENCE_EVENT_NONE: return "#str_42600";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION: return "#str_42601";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_ROUND_TRANSITION: return "#str_42602";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_PAUSE_TRANSITION: return "#str_42603";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_ROLE_CHANGE: return "#str_42604";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_PROPOSAL: return "#str_42605";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE: return "#str_42606";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT: return "#str_42607";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE: return "#str_42608";
		case MP_MATCH_VIEW_EVIDENCE_EVENT_KIND_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlOperationResultStatusKey( mpMatchOperationResultStatus_t status ) {
	switch ( status ) {
		case MP_MATCH_RESULT_REJECTED: return "#str_42620";
		case MP_MATCH_RESULT_COMMITTED: return "#str_42621";
		case MP_MATCH_RESULT_NO_CHANGE: return "#str_42622";
		case MP_MATCH_RESULT_PENDING: return "#str_42623";
		case MP_MATCH_RESULT_STATUS_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlTeamKey( mpMatchTeam_t team ) {
	switch ( team ) {
		case MP_MATCH_TEAM_NONE: return "#str_42630";
		case MP_MATCH_TEAM_MARINE: return "#str_42631";
		case MP_MATCH_TEAM_STROGG: return "#str_42632";
		case MP_MATCH_TEAM_SPECTATOR: return "#str_42633";
		case MP_MATCH_TEAM_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlStartingSideKey( mpMatchStartingSide_t side ) {
	switch ( side ) {
		case MP_MATCH_STARTING_SIDE_MARINE: return "#str_42631";
		case MP_MATCH_STARTING_SIDE_STROGG: return "#str_42632";
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlReadinessBlockerKey( mpMatchReadinessBlocker_t blocker ) {
	switch ( blocker ) {
		case MP_MATCH_BLOCKER_RULES_NOT_FROZEN: return "#str_42640";
		case MP_MATCH_BLOCKER_RULES_INVALID: return "#str_42641";
		case MP_MATCH_BLOCKER_MAP_INVALID: return "#str_42642";
		case MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_HUMANS: return "#str_42643";
		case MP_MATCH_BLOCKER_ACTIVE_PARTICIPANT_UNASSIGNED: return "#str_42644";
		case MP_MATCH_BLOCKER_TEAM_OVERSIZED: return "#str_42645";
		case MP_MATCH_BLOCKER_VACANT_REQUIRED_ROSTER_SEAT: return "#str_42646";
		case MP_MATCH_BLOCKER_VETO_INCOMPLETE: return "#str_42647";
		case MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY: return "#str_42648";
		case MP_MATCH_BLOCKER_TEAM_NOT_READY: return "#str_42649";
		case MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE: return "#str_42650";
		case MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_PARTICIPANTS: return "#str_42682";
		case MP_MATCH_BLOCKER_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlRuleFieldKey( unsigned char fieldId ) {
	switch ( fieldId ) {
		case MP_RULE_GAME_TYPE: return "#str_41600";
		case MP_RULE_MANAGED_MATCH: return "#str_41602";
		case MP_RULE_WARMUP_ENABLED: return "#str_41604";
		case MP_RULE_READINESS_POLICY: return "#str_41606";
		case MP_RULE_READY_THRESHOLD_BASIS_POINTS: return "#str_41608";
		case MP_RULE_BOTS_CAN_READY: return "#str_41610";
		case MP_RULE_MIN_ACTIVE_HUMANS: return "#str_41612";
		case MP_RULE_MIN_ACTIVE_PLAYERS: return "#str_42680";
		case MP_RULE_MIN_TEAM_SIZE: return "#str_41614";
		case MP_RULE_REQUIRE_BOTH_TEAMS: return "#str_41616";
		case MP_RULE_ROSTER_SIZE_PER_TEAM: return "#str_41618";
		case MP_RULE_COUNTDOWN_SECONDS: return "#str_41620";
		case MP_RULE_TIME_LIMIT_MINUTES: return "#str_41622";
		case MP_RULE_FRAG_LIMIT: return "#str_41624";
		case MP_RULE_CAPTURE_LIMIT: return "#str_41626";
		case MP_RULE_CONTROL_TIME_SECONDS: return "#str_41628";
		case MP_RULE_ROUND_LIMIT: return "#str_41630";
		case MP_RULE_ROUND_TIME_LIMIT_SECONDS: return "#str_41632";
		case MP_RULE_ROUND_COUNTDOWN_SECONDS: return "#str_41634";
		case MP_RULE_ROUND_REVIEW_SECONDS: return "#str_41636";
		case MP_RULE_MERCY_LIMIT: return "#str_41638";
		case MP_RULE_OVERTIME_POLICY: return "#str_41640";
		case MP_RULE_OVERTIME_PERIOD_SECONDS: return "#str_41642";
		case MP_RULE_OVERTIME_MAX_PERIODS: return "#str_41644";
		case MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY: return "#str_41646";
		case MP_RULE_SUDDEN_DEATH_RESPAWN_INCREASE: return "#str_41648";
		case MP_RULE_SUDDEN_DEATH_RESPAWN_MAX: return "#str_41650";
		case MP_RULE_TEAM_DAMAGE: return "#str_41652";
		case MP_RULE_FORFEIT_ON_EMPTY_TEAM: return "#str_41654";
		case MP_RULE_BUYING_ENABLED: return "#str_41656";
		case MP_RULE_TEAM_TIMEOUT_COUNT: return "#str_41658";
		case MP_RULE_TEAM_TIMEOUT_SECONDS: return "#str_41660";
		case MP_RULE_TIMEOUT_REQUEST_WINDOW: return "#str_41662";
		case MP_RULE_TIMEOUT_RESUME_POLICY: return "#str_41664";
		case MP_RULE_FIELD_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlMatchProfileKey( mpMatchProfileId_t profile ) {
	switch ( profile ) {
		case MP_MATCH_PROFILE_CUSTOM: return "#str_42700";
		case MP_MATCH_PROFILE_CASUAL: return "#str_41677";
		case MP_MATCH_PROFILE_COMPETITIVE_DM: return "#str_41679";
		case MP_MATCH_PROFILE_COMPETITIVE_TOURNEY: return "#str_41681";
		case MP_MATCH_PROFILE_COMPETITIVE_DUEL: return "#str_41683";
		case MP_MATCH_PROFILE_COMPETITIVE_TDM: return "#str_41685";
		case MP_MATCH_PROFILE_COMPETITIVE_CTF: return "#str_41687";
		case MP_MATCH_PROFILE_COMPETITIVE_DEADZONE: return "#str_41689";
		case MP_MATCH_PROFILE_COMPETITIVE_ROUND: return "#str_41691";
		case MP_MATCH_PROFILE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlSeriesProfileKey( mpSeriesProfileId_t profile ) {
	switch ( profile ) {
		case MP_SERIES_PROFILE_CUSTOM: return "#str_42700";
		case MP_SERIES_PROFILE_BEST_OF_ONE: return "#str_42710";
		case MP_SERIES_PROFILE_BEST_OF_THREE: return "#str_42711";
		case MP_SERIES_PROFILE_BEST_OF_FIVE: return "#str_42712";
		case MP_SERIES_PROFILE_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}

const char *MPMatchControlErrorReasonKey( mpMatchControlErrorReason_t reason ) {
	switch ( reason ) {
		case MP_MATCH_CONTROL_ERROR_NONE: return NO_DETAIL_KEY;
		case MP_MATCH_CONTROL_ERROR_INVALID_VIEW: return "#str_42720";
		case MP_MATCH_CONTROL_ERROR_STALE_VIEW: return "#str_42721";
		case MP_MATCH_CONTROL_ERROR_CAPACITY: return "#str_42722";
		case MP_MATCH_CONTROL_ERROR_UNKNOWN_COMMAND: return "#str_42723";
		case MP_MATCH_CONTROL_ERROR_INVALID_REQUEST_ID: return "#str_42724";
		case MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE: return "#str_42725";
		case MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED: return "#str_42726";
		case MP_MATCH_CONTROL_ERROR_SELECTION_INVALID: return "#str_42727";
		case MP_MATCH_CONTROL_ERROR_INVALID_SIDE: return "#str_42728";
		case MP_MATCH_CONTROL_ERROR_INVALID_VALUE: return "#str_42729";
		case MP_MATCH_CONTROL_ERROR_PROPOSAL_MISSING: return "#str_42730";
		case MP_MATCH_CONTROL_ERROR_PROTOCOL_INVALID: return "#str_42731";
		case MP_MATCH_CONTROL_ERROR_COUNT: return UNKNOWN_KEY;
		default: return UNKNOWN_KEY;
	}
}
