//----------------------------------------------------------------
// MatchProtocol.cpp
//----------------------------------------------------------------

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../Game_local.h"
#include "MatchProtocol.h"

static_assert( sizeof( int ) == 4, "MatchProtocol requires a 32-bit int" );
static_assert( sizeof( unsigned int ) == 4, "MatchProtocol requires a 32-bit unsigned int" );
static_assert( sizeof( unsigned short ) == 2, "MatchProtocol requires a 16-bit unsigned short" );
static_assert( sizeof( mpMatchProtocolRevision_t ) == 8, "MatchProtocol requires a 64-bit revision value" );
static_assert( sizeof( mpMatchProtocolSessionId_t ) == 8, "MatchProtocol requires a 64-bit session id" );
static_assert( MAX_CLIENTS <= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS, "Protocol actor-slot ceiling is below MAX_CLIENTS" );

// The phase masks below deliberately derive from these append-only values.
// Any change is a wire migration and must fail compilation here first.
static_assert( INACTIVE == 0, "mpGameState_t wire value changed" );
static_assert( WARMUP == 1, "mpGameState_t wire value changed" );
static_assert( COUNTDOWN == 2, "mpGameState_t wire value changed" );
static_assert( GAMEON == 3, "mpGameState_t wire value changed" );
static_assert( SUDDENDEATH == 4, "mpGameState_t wire value changed" );
static_assert( GAMEREVIEW == 5, "mpGameState_t wire value changed" );
static_assert( NEXTGAME == 6, "mpGameState_t wire value changed" );
static_assert( STATE_COUNT == 7, "mpGameState_t wire count changed" );
static_assert( MP_MATCH_OP_COUNT <= 65536, "Operation opcode no longer fits its wire field" );
static_assert( MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS <= 256, "Actor slot no longer fits its wire field" );
static_assert( MP_MATCH_NESTED_ARGUMENT_BASE + MP_MATCH_ARG_COUNT <= 256,
	"Nested argument identifier no longer fits its wire field" );
static_assert( MP_MATCH_LOCALIZATION_REASON_ALIGNMENT ==
	MP_MATCH_LOCALIZATION_REASON_BASE + MP_MATCH_PROTOCOL_REASON_ALIGNMENT,
	"Protocol reasons and localization keys are no longer aligned" );
static_assert( MP_MATCH_LOCALIZATION_REASON_TEAM_BALANCE ==
	MP_MATCH_LOCALIZATION_REASON_BASE + MP_MATCH_PROTOCOL_REASON_TEAM_BALANCE,
	"Protocol reasons and localization keys are no longer aligned" );
static_assert( MP_MATCH_LOCALIZATION_COUNT ==
	MP_MATCH_LOCALIZATION_REASON_BASE + MP_MATCH_PROTOCOL_REASON_COUNT,
	"Protocol reason localization range is incomplete" );

namespace {

static const int MP_MATCH_ENVELOPE_HEADER_BYTES = 15;
static const int MP_MATCH_MAX_PAYLOAD_BYTES = MP_MATCH_PROTOCOL_MAX_MESSAGE_BYTES - MP_MATCH_ENVELOPE_HEADER_BYTES;

static const mpMatchPhaseMask_t PHASE_LOBBY =
	MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_GAMEREVIEW | MP_MATCH_PHASE_NEXTGAME;
static const mpMatchPhaseMask_t PHASE_LIVE =
	MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH;
static const mpMatchPhaseMask_t PHASE_INTERACTIVE = PHASE_LOBBY | PHASE_LIVE;

typedef enum {
	REQUEST_FIELD_SCHEMA = 1,
	REQUEST_FIELD_REQUEST_ID = 2,
	REQUEST_FIELD_OPCODE = 3,
	REQUEST_FIELD_EXPECTED_REVISION = 4,
	REQUEST_FIELD_ACTOR_SLOT = 5,
	REQUEST_FIELD_BINDING_GENERATION = 6,
	REQUEST_FIELD_PARTICIPANT_TARGET = 7,
	REQUEST_FIELD_TEAM_TARGET = 8,
	REQUEST_FIELD_ARGUMENTS = 9,
	REQUEST_FIELD_CONTROL_REVISION = 10
} requestField_t;

typedef enum {
	RESULT_FIELD_SCHEMA = 1,
	RESULT_FIELD_REQUEST_ID = 2,
	RESULT_FIELD_OPCODE = 3,
	RESULT_FIELD_STATUS = 4,
	RESULT_FIELD_REASON = 5,
	RESULT_FIELD_REVISION = 6,
	RESULT_FIELD_LOCALIZATION = 7,
	RESULT_FIELD_PARAMETERS = 8
} resultField_t;

static const unsigned char OPTIONAL_EXTENSION_BIT = 0x80;
static const unsigned char FIELD_ID_MASK = 0x7f;

static const mpMatchArgumentDescriptor_t ARG_ENABLED[] = {
	{ MP_MATCH_ARG_ENABLED, MP_MATCH_VALUE_BOOL, true, 0, 1, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_ENABLED_OPTIONAL_REASON[] = {
	{ MP_MATCH_ARG_ENABLED, MP_MATCH_VALUE_BOOL, true, 0, 1, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE },
	{ MP_MATCH_ARG_REASON, MP_MATCH_VALUE_STRING, false, 0, 0, 1, 96, MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE }
};

static const mpMatchArgumentDescriptor_t ARG_OPTIONAL_REASON[] = {
	{ MP_MATCH_ARG_REASON, MP_MATCH_VALUE_STRING, false, 0, 0, 1, 96, MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE }
};

static const mpMatchArgumentDescriptor_t ARG_REQUIRED_REASON[] = {
	{ MP_MATCH_ARG_REASON, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 96, MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE }
};

static const mpMatchArgumentDescriptor_t ARG_CREDENTIAL[] = {
	{ MP_MATCH_ARG_CREDENTIAL, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 96,
		MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE | MP_MATCH_ARGUMENT_FLAG_SENSITIVE }
};

static const mpMatchArgumentDescriptor_t ARG_PROFILE[] = {
	{ MP_MATCH_ARG_PROFILE, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 48, MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN }
};

static const mpMatchArgumentDescriptor_t ARG_RULE_FIELD[] = {
	{ MP_MATCH_ARG_SETTING_ID, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 48, MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN },
	{ MP_MATCH_ARG_SETTING_VALUE, MP_MATCH_VALUE_ANY_SCALAR, true, -2147483647 - 1, 2147483647, 1, 64,
		MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE }
};

static const mpMatchArgumentDescriptor_t ARG_PROPOSAL_CREATE[] = {
	{ MP_MATCH_ARG_PROPOSED_OPCODE, MP_MATCH_VALUE_OPCODE, true, MP_MATCH_OP_READY_SET, MP_MATCH_OP_COUNT - 1,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_PROPOSAL_CAST[] = {
	{ MP_MATCH_ARG_PROPOSAL_ID, MP_MATCH_VALUE_UINT32, true, 1, 2147483647, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE },
	{ MP_MATCH_ARG_BALLOT_CHOICE, MP_MATCH_VALUE_ENUM, true, MP_MATCH_BALLOT_YES, MP_MATCH_BALLOT_ABSTAIN,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_PROPOSAL_ID[] = {
	{ MP_MATCH_ARG_PROPOSAL_ID, MP_MATCH_VALUE_UINT32, true, 1, 2147483647, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_ROSTER_INVITE[] = {
	{ MP_MATCH_ARG_ROLE, MP_MATCH_VALUE_ENUM, false, MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER, MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_ROSTER_ACCEPT[] = {
	{ MP_MATCH_ARG_INVITATION_ID, MP_MATCH_VALUE_UINT32, true, 1, 2147483647, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_ROSTER_SUBSTITUTE[] = {
	{ MP_MATCH_ARG_REPLACEMENT_PARTICIPANT, MP_MATCH_VALUE_PARTICIPANT_ID, true, 1, 2147483647,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_ROLE[] = {
	{ MP_MATCH_ARG_ROLE, MP_MATCH_VALUE_ENUM, true, MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER, MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_SERIES_PROFILE[] = {
	{ MP_MATCH_ARG_SERIES_PROFILE, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 48, MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN },
	{ MP_MATCH_ARG_BEST_OF, MP_MATCH_VALUE_UINT32, false, 1, 15, 0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_VETO[] = {
	{ MP_MATCH_ARG_VETO_ACTION, MP_MATCH_VALUE_ENUM, true, MP_MATCH_VETO_BAN, MP_MATCH_VETO_SIDE,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE },
	{ MP_MATCH_ARG_MAP_TOKEN, MP_MATCH_VALUE_STRING, true, 0, 0, 1, 63, MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN },
	{ MP_MATCH_ARG_STARTING_SIDE, MP_MATCH_VALUE_ENUM, false,
		MP_MATCH_STARTING_SIDE_MARINE, MP_MATCH_STARTING_SIDE_STROGG,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchArgumentDescriptor_t ARG_COMPETITION_SIDE[] = {
	{ MP_MATCH_ARG_COMPETITION_SIDE, MP_MATCH_VALUE_ENUM, true,
		MP_MATCH_COMPETITION_SIDE_A, MP_MATCH_COMPETITION_SIDE_B,
		0, 0, MP_MATCH_ARGUMENT_FLAG_NONE }
};

static const mpMatchOperationDescriptor_t OPERATION_DESCRIPTORS[] = {
	{ MP_MATCH_OP_READY_SET, "ready_set", MP_MATCH_LOCALIZATION_OPERATION_READY_SET, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_READY_SELF, MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN, MP_MATCH_OPERATION_FLAG_NONE, MP_MATCH_COOLDOWN_INTERACTION,
		ARG_ENABLED, 1 },
	{ MP_MATCH_OP_TEAM_READY_SET, "team_ready_set", MP_MATCH_LOCALIZATION_OPERATION_TEAM_READY_SET, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_READY_TEAM, MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN,
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_TEAM_ACTION, ARG_ENABLED, 1 },
	{ MP_MATCH_OP_FORCE_READY, "force_ready", MP_MATCH_LOCALIZATION_OPERATION_FORCE_READY, MP_MATCH_LOCALIZATION_CONFIRM_FORCE_READY,
		MP_MATCH_PROTOCOL_CAP_FORCE_READY, MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_ENABLED_OPTIONAL_REASON, 2 },
	{ MP_MATCH_OP_TEAM_JOIN, "team_join", MP_MATCH_LOCALIZATION_OPERATION_TEAM_JOIN, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_TEAM_SELF, PHASE_LOBBY,
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_INTERACTION, 0, 0 },
	{ MP_MATCH_OP_TEAM_LOCK_SET, "team_lock_set", MP_MATCH_LOCALIZATION_OPERATION_TEAM_LOCK_SET, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_TEAM_LOCK, PHASE_LOBBY,
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_TEAM_ACTION, ARG_ENABLED, 1 },
	{ MP_MATCH_OP_QUEUE_JOIN, "queue_join", MP_MATCH_LOCALIZATION_OPERATION_QUEUE_JOIN, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_QUEUE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE, MP_MATCH_COOLDOWN_INTERACTION, 0, 0 },
	{ MP_MATCH_OP_QUEUE_DEFER, "queue_defer", MP_MATCH_LOCALIZATION_OPERATION_QUEUE_DEFER, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_QUEUE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE, MP_MATCH_COOLDOWN_INTERACTION, 0, 0 },
	{ MP_MATCH_OP_QUEUE_LEAVE, "queue_leave", MP_MATCH_LOCALIZATION_OPERATION_QUEUE_LEAVE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_QUEUE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE, MP_MATCH_COOLDOWN_INTERACTION, 0, 0 },
	// openQ4: COUNTDOWN is legal at the protocol gate so that the descriptor does
	// not reject a countdown timeout ahead of the timeout_request_window rule.
	// Whether the countdown window is actually open is the session's decision
	// ( mpMatchSession::RequestTeamTimeout ), which rejects with WRONG_PHASE.
	{ MP_MATCH_OP_TIMEOUT_REQUEST, "timeout_request", MP_MATCH_LOCALIZATION_OPERATION_TIMEOUT_REQUEST, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_TIMEOUT_TEAM, PHASE_LIVE,
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_TEAM_ACTION, 0, 0 },
	{ MP_MATCH_OP_TECH_PAUSE_REQUEST, "tech_pause_request", MP_MATCH_LOCALIZATION_OPERATION_TECH_PAUSE_REQUEST, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_PAUSE_TECHNICAL, PHASE_LIVE, MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET,
		MP_MATCH_COOLDOWN_TEAM_ACTION, ARG_REQUIRED_REASON, 1 },
	{ MP_MATCH_OP_RESUME_REQUEST, "resume_request", MP_MATCH_LOCALIZATION_OPERATION_RESUME_REQUEST, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_RESUME, PHASE_LIVE, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
		MP_MATCH_COOLDOWN_TEAM_ACTION, 0, 0 },
	{ MP_MATCH_OP_REF_AUTHENTICATE, "ref_authenticate", MP_MATCH_LOCALIZATION_OPERATION_REF_AUTHENTICATE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_REFEREE_SESSION, MP_MATCH_PHASE_ALL, MP_MATCH_OPERATION_FLAG_SENSITIVE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_CREDENTIAL, 1 },
	{ MP_MATCH_OP_REF_LOGOUT, "ref_logout", MP_MATCH_LOCALIZATION_OPERATION_REF_LOGOUT, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_REFEREE_SESSION, MP_MATCH_PHASE_ALL, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_RULES_SELECT_PROFILE, "rules_select_profile", MP_MATCH_LOCALIZATION_OPERATION_RULES_SELECT_PROFILE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_RULES_STAGE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_PROFILE, 1 },
	{ MP_MATCH_OP_RULES_STAGE_FIELD, "rules_stage_field", MP_MATCH_LOCALIZATION_OPERATION_RULES_STAGE_FIELD, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_RULES_STAGE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_RULE_FIELD, 2 },
	{ MP_MATCH_OP_RULES_COMMIT, "rules_commit", MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT, MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT,
		MP_MATCH_PROTOCOL_CAP_RULES_COMMIT, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_RULES_DISCARD, "rules_discard", MP_MATCH_LOCALIZATION_OPERATION_RULES_DISCARD, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_RULES_STAGE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_PROPOSAL_CREATE, "proposal_create", MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CREATE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_PROPOSAL_CREATE, PHASE_INTERACTIVE,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
		MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS,
		MP_MATCH_COOLDOWN_INTERACTION, ARG_PROPOSAL_CREATE, 1 },
	{ MP_MATCH_OP_PROPOSAL_CAST, "proposal_cast", MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CAST, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_PROPOSAL_CAST, PHASE_INTERACTIVE, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_INTERACTION, ARG_PROPOSAL_CAST, 2 },
	{ MP_MATCH_OP_PROPOSAL_CANCEL, "proposal_cancel", MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CANCEL, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_PROPOSAL_CANCEL, PHASE_INTERACTIVE, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_INTERACTION, ARG_PROPOSAL_ID, 1 },
	{ MP_MATCH_OP_ROSTER_INVITE, "roster_invite", MP_MATCH_LOCALIZATION_OPERATION_ROSTER_INVITE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE, PHASE_LOBBY,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_ROSTER_INVITE, 1 },
	{ MP_MATCH_OP_ROSTER_ACCEPT, "roster_accept", MP_MATCH_LOCALIZATION_OPERATION_ROSTER_ACCEPT, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_ROSTER_SELF, MP_MATCH_PHASE_WARMUP, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_INTERACTION, ARG_ROSTER_ACCEPT, 1 },
	{ MP_MATCH_OP_ROSTER_REMOVE, "roster_remove", MP_MATCH_LOCALIZATION_OPERATION_ROSTER_REMOVE, MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_REMOVE,
		MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE, PHASE_LOBBY,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_ROSTER_SUBSTITUTE, "roster_substitute", MP_MATCH_LOCALIZATION_OPERATION_ROSTER_SUBSTITUTE, MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_SUBSTITUTE,
		MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE, MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_ROSTER_SUBSTITUTE, 1 },
	{ MP_MATCH_OP_ROLE_ASSIGN, "role_assign", MP_MATCH_LOCALIZATION_OPERATION_ROLE_ASSIGN, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_ROLE_ASSIGN, MP_MATCH_PHASE_WARMUP,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_ROLE, 1 },
	{ MP_MATCH_OP_SERIES_STAGE_PROFILE, "series_stage_profile", MP_MATCH_LOCALIZATION_OPERATION_SERIES_STAGE_PROFILE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_SERIES_PROFILE, 2 },
	{ MP_MATCH_OP_SERIES_START, "series_start", MP_MATCH_LOCALIZATION_OPERATION_SERIES_START, MP_MATCH_LOCALIZATION_CONFIRM_SERIES_START,
		MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, MP_MATCH_PHASE_WARMUP, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_SERIES_CANCEL, "series_cancel", MP_MATCH_LOCALIZATION_OPERATION_SERIES_CANCEL, MP_MATCH_LOCALIZATION_CONFIRM_SERIES_CANCEL,
		MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, PHASE_INTERACTIVE, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_OPTIONAL_REASON, 1 },
	{ MP_MATCH_OP_SERIES_ADVANCE, "series_advance", MP_MATCH_LOCALIZATION_OPERATION_SERIES_ADVANCE, MP_MATCH_LOCALIZATION_CONFIRM_SERIES_ADVANCE,
		MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, PHASE_LOBBY, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_VETO_SELECT, "veto_select", MP_MATCH_LOCALIZATION_OPERATION_VETO_SELECT, MP_MATCH_LOCALIZATION_CONFIRM_VETO_SELECT,
		MP_MATCH_PROTOCOL_CAP_VETO_SELECT, MP_MATCH_PHASE_WARMUP, MP_MATCH_OPERATION_FLAG_NONE,
		MP_MATCH_COOLDOWN_TEAM_ACTION, ARG_VETO, 3 },
	{ MP_MATCH_OP_FORFEIT, "forfeit", MP_MATCH_LOCALIZATION_OPERATION_FORFEIT, MP_MATCH_LOCALIZATION_CONFIRM_FORFEIT,
		MP_MATCH_PROTOCOL_CAP_FORFEIT, MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH,
		// openQ4: proposable only as a side ballot.  Its required team target
		// becomes the ballot scope, so a team's own players can vote to concede.
		MP_MATCH_OPERATION_FLAG_PROPOSABLE |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_OPTIONAL_REASON, 1 },
	{ MP_MATCH_OP_ABORT, "abort", MP_MATCH_LOCALIZATION_OPERATION_ABORT, MP_MATCH_LOCALIZATION_CONFIRM_ABORT,
		MP_MATCH_PROTOCOL_CAP_ABORT, PHASE_LIVE, MP_MATCH_OPERATION_FLAG_PROPOSABLE,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_REQUIRED_REASON, 1 },
	{ MP_MATCH_OP_BROADCASTER_SET, "broadcaster_set",
		MP_MATCH_LOCALIZATION_OPERATION_BROADCASTER_SET, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN, PHASE_INTERACTIVE,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_ENABLED, 1 },
	{ MP_MATCH_OP_ROSTER_LEAVE, "roster_leave",
		MP_MATCH_LOCALIZATION_OPERATION_ROSTER_LEAVE, MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_ROSTER_LEAVE_SELF, PHASE_INTERACTIVE,
		MP_MATCH_OPERATION_FLAG_NONE, MP_MATCH_COOLDOWN_INTERACTION, 0, 0 },
	{ MP_MATCH_OP_PARTICIPANT_REMOVE, "participant_remove",
		MP_MATCH_LOCALIZATION_OPERATION_PARTICIPANT_REMOVE,
		MP_MATCH_LOCALIZATION_CONFIRM_PARTICIPANT_REMOVE,
		MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE, PHASE_INTERACTIVE,
		MP_MATCH_OPERATION_FLAG_PROPOSABLE |
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, 0, 0 },
	{ MP_MATCH_OP_SERIES_CONTESTANT_BIND, "series_contestant_bind",
		MP_MATCH_LOCALIZATION_OPERATION_SERIES_CONTESTANT_BIND,
		MP_MATCH_LOCALIZATION_NONE,
		MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, MP_MATCH_PHASE_WARMUP,
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
		MP_MATCH_COOLDOWN_PRIVILEGED, ARG_COMPETITION_SIDE, 1 }
};

static_assert( sizeof( OPERATION_DESCRIPTORS ) / sizeof( OPERATION_DESCRIPTORS[ 0 ] ) == MP_MATCH_OP_COUNT - 1,
	"Every stable match-operation opcode requires one descriptor" );

static void SetError( mpMatchProtocolError_t *error, mpMatchProtocolReason_t reason,
	unsigned char fieldId = 0, unsigned int detail = 0 ) {
	if ( error == 0 ) {
		return;
	}
	error->reason = reason;
	error->fieldId = fieldId;
	error->detail = detail;
}

static void ClearError( mpMatchProtocolError_t *error ) {
	if ( error != 0 ) {
		error->Clear();
	}
}

static bool IsWireValueType( mpMatchValueType_t type ) {
	return type > MP_MATCH_VALUE_INVALID && type < MP_MATCH_VALUE_TYPE_COUNT;
}

static bool IsGenericScalarType( mpMatchValueType_t type ) {
	return type == MP_MATCH_VALUE_BOOL || type == MP_MATCH_VALUE_INT32 ||
		type == MP_MATCH_VALUE_UINT32 || type == MP_MATCH_VALUE_ENUM ||
		type == MP_MATCH_VALUE_STRING;
}

static bool IsMachineToken( const char *token ) {
	if ( token == 0 || token[ 0 ] == '\0' ) {
		return false;
	}
	for ( int i = 0; token[ i ] != '\0'; ++i ) {
		const unsigned char c = static_cast<unsigned char>( token[ i ] );
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) {
			return false;
		}
		if ( i >= 47 ) {
			return false;
		}
	}
	return true;
}

static const mpMatchArgumentDescriptor_t *FindArgumentDescriptor(
	const mpMatchOperationDescriptor_t &descriptor, unsigned char fieldId ) {
	for ( int i = 0; i < descriptor.argumentCount; ++i ) {
		if ( descriptor.arguments[ i ].fieldId == fieldId ) {
			return &descriptor.arguments[ i ];
		}
	}
	return 0;
}

static const mpMatchOperationArgument_t *FindArgument( const mpMatchOperationRequest_t &request,
	unsigned char fieldId ) {
	for ( int i = 0; i < request.argumentCount; ++i ) {
		if ( request.arguments[ i ].fieldId == fieldId ) {
			return &request.arguments[ i ];
		}
	}
	return 0;
}

static bool ValidateStoredString( const mpMatchOperationValue_t &value,
	const mpMatchArgumentDescriptor_t &descriptor, unsigned char fieldId, mpMatchProtocolError_t *error ) {
	if ( value.stringLength > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ||
		value.stringLength < descriptor.minimumStringBytes ||
		value.stringLength > descriptor.maximumStringBytes ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_LENGTH, fieldId, value.stringLength );
		return false;
	}
	if ( value.stringValue[ value.stringLength ] != '\0' ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_LENGTH, fieldId, value.stringLength );
		return false;
	}

	for ( int i = 0; i < value.stringLength; ++i ) {
		const unsigned char c = static_cast<unsigned char>( value.stringValue[ i ] );
		if ( c == 0 || c < 32 || c > 126 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS, fieldId, i );
			return false;
		}
		if ( ( descriptor.flags & MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN ) != 0 ) {
			if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
				( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.' ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS, fieldId, i );
				return false;
			}
		}
		if ( ( descriptor.flags & MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN ) != 0 ) {
			if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
				( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '/' ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS, fieldId, i );
				return false;
			}
		}
	}

	if ( ( descriptor.flags & MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN ) != 0 ) {
		if ( value.stringLength == 0 || value.stringValue[ 0 ] == '/' ||
			value.stringValue[ value.stringLength - 1 ] == '/' ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS, fieldId, 0 );
			return false;
		}
		for ( int i = 0; i + 1 < value.stringLength; ++i ) {
			if ( value.stringValue[ i ] == '/' && value.stringValue[ i + 1 ] == '/' ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS, fieldId, i );
				return false;
			}
		}
	}
	return true;
}

static bool ValidateValue( const mpMatchOperationValue_t &value,
	const mpMatchArgumentDescriptor_t &descriptor, unsigned char fieldId, mpMatchProtocolError_t *error ) {
	if ( !IsWireValueType( value.type ) ||
		( descriptor.type == MP_MATCH_VALUE_ANY_SCALAR && !IsGenericScalarType( value.type ) ) ||
		( descriptor.type != MP_MATCH_VALUE_ANY_SCALAR && value.type != descriptor.type ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, static_cast<unsigned int>( value.type ) );
		return false;
	}

	switch ( value.type ) {
		case MP_MATCH_VALUE_BOOL:
			if ( value.unsignedValue > 1u ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, fieldId, value.unsignedValue );
				return false;
			}
			break;
		case MP_MATCH_VALUE_INT32:
			if ( value.signedValue < descriptor.minimumValue || value.signedValue > descriptor.maximumValue ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, fieldId,
					static_cast<unsigned int>( value.signedValue ) );
				return false;
			}
			break;
		case MP_MATCH_VALUE_UINT32:
		case MP_MATCH_VALUE_PARTICIPANT_ID:
			if ( descriptor.minimumValue < 0 || value.unsignedValue < static_cast<unsigned int>( descriptor.minimumValue ) ||
				value.unsignedValue > static_cast<unsigned int>( descriptor.maximumValue ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, fieldId, value.unsignedValue );
				return false;
			}
			break;
		case MP_MATCH_VALUE_ENUM:
		case MP_MATCH_VALUE_OPCODE:
			if ( value.enumValue < descriptor.minimumValue || value.enumValue > descriptor.maximumValue ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, fieldId, value.enumValue );
				return false;
			}
			break;
		case MP_MATCH_VALUE_STRING:
			return ValidateStoredString( value, descriptor, fieldId, error );
		default:
			SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, static_cast<unsigned int>( value.type ) );
			return false;
	}
	return true;
}

static bool ValidateTargetPolicy( const mpMatchOperationDescriptor_t &descriptor,
	bool hasParticipantTarget, unsigned int participantTarget, bool hasTeamTarget,
	mpMatchTeam_t teamTarget, mpMatchProtocolError_t *error ) {
	if ( hasParticipantTarget ) {
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET ) == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_TARGET, REQUEST_FIELD_PARTICIPANT_TARGET, participantTarget );
			return false;
		}
		if ( participantTarget == MP_MATCH_INVALID_PARTICIPANT_ID || participantTarget > 0x7fffffffu ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_PARTICIPANT, REQUEST_FIELD_PARTICIPANT_TARGET, participantTarget );
			return false;
		}
	} else if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET ) != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_TARGET, REQUEST_FIELD_PARTICIPANT_TARGET, 0 );
		return false;
	}

	if ( hasTeamTarget ) {
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET ) == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_TARGET, REQUEST_FIELD_TEAM_TARGET, teamTarget );
			return false;
		}
		if ( teamTarget <= MP_MATCH_TEAM_NONE || teamTarget >= MP_MATCH_TEAM_COUNT ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_TEAM, REQUEST_FIELD_TEAM_TARGET, teamTarget );
			return false;
		}
	} else if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET ) != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_TARGET, REQUEST_FIELD_TEAM_TARGET, 0 );
		return false;
	}

	return true;
}

static bool ValidateRegistryInternal( mpMatchProtocolError_t *error ) {
	bool seenOpcodes[ MP_MATCH_OP_COUNT ];
	memset( seenOpcodes, 0, sizeof( seenOpcodes ) );

	const unsigned int knownOperationFlags =
		MP_MATCH_OPERATION_FLAG_PROPOSABLE |
		MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
		MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
		MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET |
		MP_MATCH_OPERATION_FLAG_SENSITIVE |
		MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS;
	const unsigned int knownArgumentFlags =
		MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE |
		MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN |
		MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN |
		MP_MATCH_ARGUMENT_FLAG_SENSITIVE;

	for ( int i = 0; i < MP_MATCH_OP_COUNT - 1; ++i ) {
		const mpMatchOperationDescriptor_t &descriptor = OPERATION_DESCRIPTORS[ i ];
		const int opcode = static_cast<int>( descriptor.opcode );
		if ( opcode <= MP_MATCH_OP_INVALID || opcode >= MP_MATCH_OP_COUNT ||
			opcode != i + 1 || seenOpcodes[ opcode ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		seenOpcodes[ opcode ] = true;
		if ( !IsMachineToken( descriptor.token ) ||
			descriptor.labelLocalizationId != MP_MATCH_LOCALIZATION_OPERATION_BASE + opcode ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		for ( int j = 0; j < i; ++j ) {
			if ( strcmp( descriptor.token, OPERATION_DESCRIPTORS[ j ].token ) == 0 ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
				return false;
			}
		}
		if ( descriptor.confirmationLocalizationId != MP_MATCH_LOCALIZATION_NONE &&
			( descriptor.confirmationLocalizationId <= MP_MATCH_LOCALIZATION_CONFIRM_BASE ||
			descriptor.confirmationLocalizationId >= MP_MATCH_LOCALIZATION_REASON_BASE ) ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( descriptor.requiredCapability == 0 ||
			( descriptor.requiredCapability & ~MP_MATCH_PROTOCOL_CAP_ALL ) != 0 ||
			( descriptor.requiredCapability & ( descriptor.requiredCapability - 1u ) ) != 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( descriptor.legalPhaseMask == 0 || ( descriptor.legalPhaseMask & ~MP_MATCH_PHASE_ALL ) != 0 ||
			( descriptor.flags & ~knownOperationFlags ) != 0 ||
			descriptor.cooldownClass < MP_MATCH_COOLDOWN_NONE || descriptor.cooldownClass >= MP_MATCH_COOLDOWN_COUNT ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET ) != 0 &&
			( descriptor.flags & MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET ) == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET ) != 0 &&
			( descriptor.flags & MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET ) == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) != 0 &&
			( descriptor.flags & MP_MATCH_OPERATION_FLAG_SENSITIVE ) != 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS ) != 0 &&
			descriptor.opcode != MP_MATCH_OP_PROPOSAL_CREATE ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( descriptor.opcode == MP_MATCH_OP_PROPOSAL_CREATE &&
			( descriptor.flags & MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS ) == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
		if ( descriptor.argumentCount > MP_MATCH_PROTOCOL_MAX_ARGUMENTS ||
			( descriptor.argumentCount != 0 && descriptor.arguments == 0 ) ||
			( descriptor.argumentCount == 0 && descriptor.arguments != 0 ) ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}

		bool seenArguments[ MP_MATCH_NESTED_ARGUMENT_BASE ];
		memset( seenArguments, 0, sizeof( seenArguments ) );
		for ( int j = 0; j < descriptor.argumentCount; ++j ) {
			const mpMatchArgumentDescriptor_t &argument = descriptor.arguments[ j ];
			if ( argument.fieldId == MP_MATCH_ARG_INVALID || argument.fieldId >= MP_MATCH_NESTED_ARGUMENT_BASE ||
				seenArguments[ argument.fieldId ] ||
				( argument.type != MP_MATCH_VALUE_ANY_SCALAR && !IsWireValueType( argument.type ) ) ||
				argument.minimumValue > argument.maximumValue ||
				( argument.flags & ~knownArgumentFlags ) != 0 ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, argument.fieldId, opcode );
				return false;
			}
			seenArguments[ argument.fieldId ] = true;
			const bool mayBeString = argument.type == MP_MATCH_VALUE_STRING || argument.type == MP_MATCH_VALUE_ANY_SCALAR;
			if ( mayBeString ) {
				if ( argument.maximumStringBytes == 0 ||
					argument.minimumStringBytes > argument.maximumStringBytes ||
					argument.maximumStringBytes > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, argument.fieldId, opcode );
					return false;
				}
			} else if ( argument.minimumStringBytes != 0 || argument.maximumStringBytes != 0 ||
				( argument.flags & ( MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE |
				MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN | MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN ) ) != 0 ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, argument.fieldId, opcode );
				return false;
			}
			if ( ( argument.flags & MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN ) != 0 &&
				( argument.flags & MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN ) != 0 ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, argument.fieldId, opcode );
				return false;
			}
		}
	}

	for ( int opcode = MP_MATCH_OP_READY_SET; opcode < MP_MATCH_OP_COUNT; ++opcode ) {
		if ( !seenOpcodes[ opcode ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID, 0, opcode );
			return false;
		}
	}
	return true;
}

static bool RegistryIsValid( void ) {
	static const bool valid = ValidateRegistryInternal( 0 );
	return valid;
}

static bool ValidateRequestArguments( const mpMatchOperationRequest_t &request,
	const mpMatchOperationDescriptor_t &descriptor, mpMatchProtocolError_t *error ) {
	bool seen[ 256 ];
	memset( seen, 0, sizeof( seen ) );

	for ( int i = 0; i < request.argumentCount; ++i ) {
		const mpMatchOperationArgument_t &argument = request.arguments[ i ];
		if ( argument.fieldId == MP_MATCH_ARG_INVALID || seen[ argument.fieldId ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD, argument.fieldId, i );
			return false;
		}
		seen[ argument.fieldId ] = true;

		if ( argument.fieldId >= MP_MATCH_NESTED_ARGUMENT_BASE ) {
			if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS ) == 0 ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, argument.fieldId, i );
				return false;
			}
			continue;
		}

		const mpMatchArgumentDescriptor_t *argumentDescriptor =
			FindArgumentDescriptor( descriptor, argument.fieldId );
		if ( argumentDescriptor == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, argument.fieldId, i );
			return false;
		}
		if ( !ValidateValue( argument.value, *argumentDescriptor, argument.fieldId, error ) ) {
			return false;
		}
	}

	for ( int i = 0; i < descriptor.argumentCount; ++i ) {
		if ( descriptor.arguments[ i ].required && !seen[ descriptor.arguments[ i ].fieldId ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, descriptor.arguments[ i ].fieldId, request.argumentCount );
			return false;
		}
	}

	if ( ( descriptor.flags & MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS ) == 0 ) {
		return true;
	}

	const mpMatchOperationArgument_t *proposedOpcode = FindArgument( request, MP_MATCH_ARG_PROPOSED_OPCODE );
	if ( proposedOpcode == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, MP_MATCH_ARG_PROPOSED_OPCODE, request.argumentCount );
		return false;
	}
	const mpMatchOperationDescriptor_t *targetDescriptor = MPMatchOperationDescriptor(
		static_cast<mpMatchOperationOpcode_t>( proposedOpcode->value.enumValue ) );
	if ( targetDescriptor == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE, MP_MATCH_ARG_PROPOSED_OPCODE,
			proposedOpcode->value.enumValue );
		return false;
	}
	if ( ( targetDescriptor->flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_NOT_PROPOSABLE, MP_MATCH_ARG_PROPOSED_OPCODE,
			proposedOpcode->value.enumValue );
		return false;
	}
	if ( !ValidateTargetPolicy( *targetDescriptor, request.hasParticipantTarget,
		request.participantTarget, request.hasTeamTarget, request.teamTarget, error ) ) {
		return false;
	}

	bool seenNested[ MP_MATCH_NESTED_ARGUMENT_BASE ];
	memset( seenNested, 0, sizeof( seenNested ) );
	for ( int i = 0; i < request.argumentCount; ++i ) {
		const mpMatchOperationArgument_t &argument = request.arguments[ i ];
		if ( argument.fieldId < MP_MATCH_NESTED_ARGUMENT_BASE ) {
			continue;
		}
		const unsigned char nestedField = argument.fieldId - MP_MATCH_NESTED_ARGUMENT_BASE;
		if ( nestedField == MP_MATCH_ARG_INVALID || nestedField >= MP_MATCH_NESTED_ARGUMENT_BASE || seenNested[ nestedField ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD, argument.fieldId, i );
			return false;
		}
		seenNested[ nestedField ] = true;
		const mpMatchArgumentDescriptor_t *nestedDescriptor =
			FindArgumentDescriptor( *targetDescriptor, nestedField );
		if ( nestedDescriptor == 0 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, argument.fieldId, i );
			return false;
		}
		if ( !ValidateValue( argument.value, *nestedDescriptor, argument.fieldId, error ) ) {
			return false;
		}
	}
	for ( int i = 0; i < targetDescriptor->argumentCount; ++i ) {
		if ( targetDescriptor->arguments[ i ].required && !seenNested[ targetDescriptor->arguments[ i ].fieldId ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT,
				MP_MATCH_NESTED_ARGUMENT_BASE + targetDescriptor->arguments[ i ].fieldId, request.argumentCount );
			return false;
		}
	}
	return true;
}

static bool ValidateVetoArgumentShape( const mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error ) {
	if ( request.opcode != MP_MATCH_OP_VETO_SELECT ) {
		return true;
	}

	const mpMatchOperationArgument_t *action = FindArgument( request,
		MP_MATCH_ARG_VETO_ACTION );
	const mpMatchOperationArgument_t *startingSide = FindArgument( request,
		MP_MATCH_ARG_STARTING_SIDE );
	// Generic descriptor validation has already established the action's type
	// and range.  Keep the conditional contract here so encoders, decoders and
	// direct in-process callers all reject ambiguous side selections equally.
	if ( action == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT,
			MP_MATCH_ARG_VETO_ACTION, request.argumentCount );
		return false;
	}
	const bool requiresStartingSide = action->value.enumValue == MP_MATCH_VETO_SIDE;
	if ( requiresStartingSide != ( startingSide != 0 ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT,
			MP_MATCH_ARG_STARTING_SIDE, request.argumentCount );
		return false;
	}
	return true;
}

static bool IsValidLocalizationId( mpMatchLocalizationId_t id ) {
	if ( id == MP_MATCH_LOCALIZATION_NONE ) {
		return true;
	}
	if ( id > MP_MATCH_LOCALIZATION_REASON_BASE && id < MP_MATCH_LOCALIZATION_COUNT ) {
		return true;
	}
	for ( int i = 0; i < MP_MATCH_OP_COUNT - 1; ++i ) {
		if ( OPERATION_DESCRIPTORS[ i ].labelLocalizationId == id ||
			OPERATION_DESCRIPTORS[ i ].confirmationLocalizationId == id ) {
			return true;
		}
	}
	return false;
}

static bool ValidateResultParameter( const mpMatchOperationArgument_t &parameter,
	mpMatchProtocolError_t *error ) {
	if ( parameter.fieldId == MP_MATCH_ARG_INVALID || parameter.fieldId >= MP_MATCH_NESTED_ARGUMENT_BASE ||
		!IsWireValueType( parameter.value.type ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, parameter.fieldId, parameter.value.type );
		return false;
	}
	if ( parameter.value.type == MP_MATCH_VALUE_BOOL && parameter.value.unsignedValue > 1u ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, parameter.fieldId, parameter.value.unsignedValue );
		return false;
	}
	if ( parameter.value.type == MP_MATCH_VALUE_PARTICIPANT_ID &&
		parameter.value.unsignedValue == MP_MATCH_INVALID_PARTICIPANT_ID ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_PARTICIPANT, parameter.fieldId, parameter.value.unsignedValue );
		return false;
	}
	if ( parameter.value.type == MP_MATCH_VALUE_OPCODE &&
		MPMatchOperationDescriptor( static_cast<mpMatchOperationOpcode_t>( parameter.value.enumValue ) ) == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE, parameter.fieldId, parameter.value.enumValue );
		return false;
	}
	if ( parameter.value.type == MP_MATCH_VALUE_STRING ) {
		const mpMatchArgumentDescriptor_t genericString = {
			parameter.fieldId, MP_MATCH_VALUE_STRING, false, 0, 0, 0,
			MP_MATCH_PROTOCOL_MAX_STRING_BYTES, MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE
		};
		return ValidateStoredString( parameter.value, genericString, parameter.fieldId, error );
	}
	return true;
}

static int ValueWireLength( const mpMatchOperationValue_t &value ) {
	switch ( value.type ) {
		case MP_MATCH_VALUE_BOOL:
			return 1;
		case MP_MATCH_VALUE_INT32:
		case MP_MATCH_VALUE_UINT32:
		case MP_MATCH_VALUE_PARTICIPANT_ID:
			return 4;
		case MP_MATCH_VALUE_ENUM:
		case MP_MATCH_VALUE_OPCODE:
			return 2;
		case MP_MATCH_VALUE_STRING:
			return value.stringLength;
		default:
			return -1;
	}
}

static void WriteFieldHeader( idBitMsg &message, unsigned char tag, int length ) {
	message.WriteByte( tag );
	message.WriteUShort( length );
}

static void WriteByteField( idBitMsg &message, unsigned char tag, unsigned char value ) {
	WriteFieldHeader( message, tag, 1 );
	message.WriteByte( value );
}

static void WriteUShortField( idBitMsg &message, unsigned char tag, unsigned short value ) {
	WriteFieldHeader( message, tag, 2 );
	message.WriteUShort( value );
}

static void WriteUIntField( idBitMsg &message, unsigned char tag, unsigned int value ) {
	WriteFieldHeader( message, tag, 4 );
	message.WriteLong( static_cast<int>( value ) );
}

static void WriteUInt64Value( idBitMsg &message, unsigned long long value ) {
	message.WriteLong( static_cast<int>( value & 0xffffffffull ) );
	message.WriteLong( static_cast<int>( value >> 32 ) );
}

static void WriteRevisionField( idBitMsg &message, unsigned char tag, mpMatchProtocolRevision_t value ) {
	WriteFieldHeader( message, tag, 8 );
	WriteUInt64Value( message, value );
}

static void WriteValue( idBitMsg &message, const mpMatchOperationValue_t &value ) {
	switch ( value.type ) {
		case MP_MATCH_VALUE_BOOL:
			message.WriteByte( static_cast<unsigned char>( value.unsignedValue ) );
			break;
		case MP_MATCH_VALUE_INT32:
			message.WriteLong( value.signedValue );
			break;
		case MP_MATCH_VALUE_UINT32:
		case MP_MATCH_VALUE_PARTICIPANT_ID:
			message.WriteLong( static_cast<int>( value.unsignedValue ) );
			break;
		case MP_MATCH_VALUE_ENUM:
		case MP_MATCH_VALUE_OPCODE:
			message.WriteUShort( value.enumValue );
			break;
		case MP_MATCH_VALUE_STRING:
			message.WriteData( value.stringValue, value.stringLength );
			break;
		default:
			break;
	}
}

static int ArgumentListWireLength( const mpMatchOperationArgument_t *arguments, int count ) {
	int length = 1;
	for ( int i = 0; i < count; ++i ) {
		const int valueLength = ValueWireLength( arguments[ i ].value );
		if ( valueLength < 0 ) {
			return -1;
		}
		length += 4 + valueLength;
	}
	return length;
}

static void WriteArgumentList( idBitMsg &message, unsigned char tag,
	const mpMatchOperationArgument_t *arguments, int count ) {
	const mpMatchOperationArgument_t *ordered[ MP_MATCH_PROTOCOL_MAX_ARGUMENTS ];
	for ( int i = 0; i < count; ++i ) {
		ordered[ i ] = &arguments[ i ];
	}
	for ( int i = 1; i < count; ++i ) {
		const mpMatchOperationArgument_t *candidate = ordered[ i ];
		int insertion = i;
		while ( insertion > 0 && ordered[ insertion - 1 ]->fieldId > candidate->fieldId ) {
			ordered[ insertion ] = ordered[ insertion - 1 ];
			--insertion;
		}
		ordered[ insertion ] = candidate;
	}

	const int length = ArgumentListWireLength( arguments, count );
	WriteFieldHeader( message, tag, length );
	message.WriteByte( count );
	for ( int i = 0; i < count; ++i ) {
		const mpMatchOperationArgument_t &argument = *ordered[ i ];
		const int valueLength = ValueWireLength( argument.value );
		message.WriteByte( argument.fieldId );
		message.WriteByte( argument.value.type );
		message.WriteUShort( valueLength );
		WriteValue( message, argument.value );
	}
}

static bool ReadField( idBitMsg &message, unsigned char &tag, byte *data, int &length,
	mpMatchProtocolError_t *error ) {
	if ( message.GetReadBit() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ALIGNMENT );
		return false;
	}
	if ( message.GetRemainingReadBits() < 24 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	tag = static_cast<unsigned char>( message.ReadByte() );
	length = message.ReadUShort();
	if ( length < 0 || length > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, tag, static_cast<unsigned int>( length ) );
		return false;
	}
	if ( message.GetRemainingReadBits() < length * 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, tag, static_cast<unsigned int>( length ) );
		return false;
	}
	if ( length > 0 && message.ReadData( data, length ) != length ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, tag, static_cast<unsigned int>( length ) );
		return false;
	}
	return true;
}

static bool ReadByteValue( const byte *data, int length, unsigned char &value ) {
	if ( length != 1 ) {
		return false;
	}
	value = data[ 0 ];
	return true;
}

static bool ReadUShortValue( const byte *data, int length, unsigned short &value ) {
	if ( length != 2 ) {
		return false;
	}
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	value = static_cast<unsigned short>( message.ReadUShort() );
	return message.GetRemainingReadBits() == 0;
}

static bool ReadUIntValue( const byte *data, int length, unsigned int &value ) {
	if ( length != 4 ) {
		return false;
	}
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	value = static_cast<unsigned int>( message.ReadLong() );
	return message.GetRemainingReadBits() == 0;
}

static bool ReadUInt64Value( const byte *data, int length, unsigned long long &value ) {
	if ( length != 8 ) {
		return false;
	}
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	const unsigned int low = static_cast<unsigned int>( message.ReadLong() );
	const unsigned int high = static_cast<unsigned int>( message.ReadLong() );
	value = static_cast<unsigned long long>( low ) |
		( static_cast<unsigned long long>( high ) << 32 );
	return message.GetRemainingReadBits() == 0;
}

static bool DecodeValue( mpMatchValueType_t type, const byte *data, int length,
	mpMatchOperationValue_t &value, unsigned char fieldId, mpMatchProtocolError_t *error ) {
	mpMatchOperationValue_t decoded;
	decoded.Clear();

	switch ( type ) {
		case MP_MATCH_VALUE_BOOL: {
			unsigned char raw = 0;
			if ( !ReadByteValue( data, length, raw ) || raw > 1 ) {
				SetError( error, raw > 1 ? MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE : MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE,
					fieldId, raw );
				return false;
			}
			decoded.SetBool( raw != 0 );
			break;
		}
		case MP_MATCH_VALUE_INT32: {
			unsigned int raw = 0;
			if ( !ReadUIntValue( data, length, raw ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, length );
				return false;
			}
			decoded.SetInt32( static_cast<int>( raw ) );
			break;
		}
		case MP_MATCH_VALUE_UINT32: {
			unsigned int raw = 0;
			if ( !ReadUIntValue( data, length, raw ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, length );
				return false;
			}
			decoded.SetUInt32( raw );
			break;
		}
		case MP_MATCH_VALUE_ENUM: {
			unsigned short raw = 0;
			if ( !ReadUShortValue( data, length, raw ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, length );
				return false;
			}
			decoded.SetEnum( raw );
			break;
		}
		case MP_MATCH_VALUE_STRING:
			if ( length < 0 || length > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ||
				!decoded.SetString( reinterpret_cast<const char *>( data ), length ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_STRING_LENGTH, fieldId, length );
				return false;
			}
			break;
		case MP_MATCH_VALUE_OPCODE: {
			unsigned short raw = 0;
			if ( !ReadUShortValue( data, length, raw ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, length );
				return false;
			}
			decoded.SetOpcode( static_cast<mpMatchOperationOpcode_t>( raw ) );
			break;
		}
		case MP_MATCH_VALUE_PARTICIPANT_ID: {
			unsigned int raw = 0;
			if ( !ReadUIntValue( data, length, raw ) ) {
				SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, length );
				return false;
			}
			decoded.SetParticipantId( raw );
			break;
		}
		default:
			SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, static_cast<unsigned int>( type ) );
			return false;
	}

	value = decoded;
	return true;
}

static bool DecodeArgumentList( const byte *data, int length, mpMatchOperationArgument_t *arguments,
	int maximumArguments, unsigned char &argumentCount, mpMatchProtocolError_t *error ) {
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	if ( message.GetRemainingReadBits() < 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	const int count = message.ReadByte();
	if ( count < 0 || count > maximumArguments ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, 0, count );
		return false;
	}

	mpMatchOperationArgument_t decoded[ MP_MATCH_PROTOCOL_MAX_ARGUMENTS ];
	bool seen[ 256 ];
	memset( seen, 0, sizeof( seen ) );
	for ( int i = 0; i < count; ++i ) {
		if ( message.GetRemainingReadBits() < 32 ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, 0, i );
			return false;
		}
		const unsigned char fieldId = static_cast<unsigned char>( message.ReadByte() );
		const int typeValue = message.ReadByte();
		const int valueLength = message.ReadUShort();
		if ( fieldId == MP_MATCH_ARG_INVALID || seen[ fieldId ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD, fieldId, i );
			return false;
		}
		seen[ fieldId ] = true;
		if ( typeValue <= MP_MATCH_VALUE_INVALID || typeValue >= MP_MATCH_VALUE_TYPE_COUNT ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, typeValue );
			return false;
		}
		if ( valueLength < 0 || valueLength > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ||
			message.GetRemainingReadBits() < valueLength * 8 ) {
			SetError( error, valueLength > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ?
				MP_MATCH_PROTOCOL_REASON_STRING_LENGTH : MP_MATCH_PROTOCOL_REASON_TRUNCATED, fieldId, valueLength );
			return false;
		}
		byte valueData[ MP_MATCH_PROTOCOL_MAX_STRING_BYTES ];
		if ( valueLength > 0 && message.ReadData( valueData, valueLength ) != valueLength ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, fieldId, valueLength );
			return false;
		}
		decoded[ i ].Clear();
		decoded[ i ].fieldId = fieldId;
		if ( !DecodeValue( static_cast<mpMatchValueType_t>( typeValue ), valueData,
			valueLength, decoded[ i ].value, fieldId, error ) ) {
			return false;
		}
	}
	if ( message.GetRemainingReadBits() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRAILING_DATA, 0, message.GetRemainingReadBits() );
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		arguments[ i ] = decoded[ i ];
	}
	argumentCount = static_cast<unsigned char>( count );
	return true;
}

static bool IsKnownRequestField( unsigned char fieldId ) {
	return fieldId >= REQUEST_FIELD_SCHEMA &&
		fieldId <= REQUEST_FIELD_CONTROL_REVISION;
}

static bool IsKnownResultField( unsigned char fieldId ) {
	return fieldId >= RESULT_FIELD_SCHEMA && fieldId <= RESULT_FIELD_PARAMETERS;
}

static bool CheckAndMarkField( unsigned char rawTag, bool *seen, bool known,
	mpMatchProtocolError_t *error ) {
	const unsigned char fieldId = rawTag & FIELD_ID_MASK;
	if ( fieldId == 0 || seen[ fieldId ] ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD, fieldId, rawTag );
		return false;
	}
	seen[ fieldId ] = true;
	if ( ( rawTag & OPTIONAL_EXTENSION_BIT ) != 0 && known ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, fieldId, rawTag );
		return false;
	}
	if ( ( rawTag & OPTIONAL_EXTENSION_BIT ) == 0 && !known ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, fieldId, rawTag );
		return false;
	}
	return true;
}

static bool DecodeRequestPayload( const byte *data, int length, mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error ) {
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	if ( message.GetRemainingReadBits() < 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	const int fieldCount = message.ReadByte();
	if ( fieldCount < 0 || fieldCount > MP_MATCH_PROTOCOL_MAX_TOP_LEVEL_FIELDS ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, 0, fieldCount );
		return false;
	}

	mpMatchOperationRequest_t decoded;
	decoded.Clear();
	bool seen[ 128 ];
	memset( seen, 0, sizeof( seen ) );
	byte fieldData[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	for ( int i = 0; i < fieldCount; ++i ) {
		unsigned char rawTag = 0;
		int fieldLength = 0;
		if ( !ReadField( message, rawTag, fieldData, fieldLength, error ) ) {
			return false;
		}
		const unsigned char fieldId = rawTag & FIELD_ID_MASK;
		const bool known = IsKnownRequestField( fieldId );
		if ( !CheckAndMarkField( rawTag, seen, known, error ) ) {
			return false;
		}
		if ( !known ) {
			continue;
		}

		switch ( fieldId ) {
			case REQUEST_FIELD_SCHEMA: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.schemaVersion = value;
				break;
			}
			case REQUEST_FIELD_REQUEST_ID:
				if ( !ReadUIntValue( fieldData, fieldLength, decoded.requestId ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				break;
			case REQUEST_FIELD_OPCODE: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.opcode = static_cast<mpMatchOperationOpcode_t>( value );
				break;
			}
			case REQUEST_FIELD_EXPECTED_REVISION:
				if ( !ReadUInt64Value( fieldData, fieldLength, decoded.expectedSessionRevision ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				break;
			case REQUEST_FIELD_CONTROL_REVISION:
				if ( !ReadUInt64Value( fieldData, fieldLength,
					decoded.expectedControlRevision ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE,
						fieldId, fieldLength );
					return false;
				}
				break;
			case REQUEST_FIELD_ACTOR_SLOT: {
				unsigned char value = 0;
				if ( !ReadByteValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.actorSlot = value;
				break;
			}
			case REQUEST_FIELD_BINDING_GENERATION:
				if ( !ReadUIntValue( fieldData, fieldLength, decoded.actorBindingGeneration ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				break;
			case REQUEST_FIELD_PARTICIPANT_TARGET:
				if ( !ReadUIntValue( fieldData, fieldLength, decoded.participantTarget ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.hasParticipantTarget = true;
				break;
			case REQUEST_FIELD_TEAM_TARGET: {
				unsigned char value = 0;
				if ( !ReadByteValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.hasTeamTarget = true;
				decoded.teamTarget = static_cast<mpMatchTeam_t>( value );
				break;
			}
			case REQUEST_FIELD_ARGUMENTS:
				if ( !DecodeArgumentList( fieldData, fieldLength, decoded.arguments,
					MP_MATCH_PROTOCOL_MAX_ARGUMENTS, decoded.argumentCount, error ) ) {
					return false;
				}
				break;
			default:
				SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, fieldId, rawTag );
				return false;
		}
	}

	if ( message.GetRemainingReadBits() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRAILING_DATA, 0, message.GetRemainingReadBits() );
		return false;
	}
	const unsigned int required =
		( 1u << REQUEST_FIELD_SCHEMA ) |
		( 1u << REQUEST_FIELD_REQUEST_ID ) |
		( 1u << REQUEST_FIELD_OPCODE ) |
		( 1u << REQUEST_FIELD_EXPECTED_REVISION ) |
		( 1u << REQUEST_FIELD_CONTROL_REVISION ) |
		( 1u << REQUEST_FIELD_ACTOR_SLOT ) |
		( 1u << REQUEST_FIELD_BINDING_GENERATION ) |
		( 1u << REQUEST_FIELD_ARGUMENTS );
	unsigned int present = 0;
	for ( int i = 1; i < 32; ++i ) {
		if ( i < 128 && seen[ i ] ) {
			present |= 1u << i;
		}
	}
	if ( ( present & required ) != required ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, 0, required & ~present );
		return false;
	}
	request = decoded;
	return true;
}

static bool DecodeResultPayload( const byte *data, int length, mpMatchOperationResult_t &result,
	mpMatchProtocolError_t *error ) {
	idBitMsg message;
	message.Init( data, length );
	message.SetSize( length );
	message.BeginReading();
	if ( message.GetRemainingReadBits() < 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	const int fieldCount = message.ReadByte();
	if ( fieldCount < 0 || fieldCount > MP_MATCH_PROTOCOL_MAX_TOP_LEVEL_FIELDS ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, 0, fieldCount );
		return false;
	}

	mpMatchOperationResult_t decoded;
	decoded.Clear();
	bool seen[ 128 ];
	memset( seen, 0, sizeof( seen ) );
	byte fieldData[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	for ( int i = 0; i < fieldCount; ++i ) {
		unsigned char rawTag = 0;
		int fieldLength = 0;
		if ( !ReadField( message, rawTag, fieldData, fieldLength, error ) ) {
			return false;
		}
		const unsigned char fieldId = rawTag & FIELD_ID_MASK;
		const bool known = IsKnownResultField( fieldId );
		if ( !CheckAndMarkField( rawTag, seen, known, error ) ) {
			return false;
		}
		if ( !known ) {
			continue;
		}

		switch ( fieldId ) {
			case RESULT_FIELD_SCHEMA: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.schemaVersion = value;
				break;
			}
			case RESULT_FIELD_REQUEST_ID:
				if ( !ReadUIntValue( fieldData, fieldLength, decoded.requestId ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				break;
			case RESULT_FIELD_OPCODE: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.opcode = static_cast<mpMatchOperationOpcode_t>( value );
				break;
			}
			case RESULT_FIELD_STATUS: {
				unsigned char value = 0;
				if ( !ReadByteValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.status = static_cast<mpMatchOperationResultStatus_t>( value );
				break;
			}
			case RESULT_FIELD_REASON: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.reason = static_cast<mpMatchProtocolReason_t>( value );
				break;
			}
			case RESULT_FIELD_REVISION:
				if ( !ReadUInt64Value( fieldData, fieldLength, decoded.resultingSessionRevision ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				break;
			case RESULT_FIELD_LOCALIZATION: {
				unsigned short value = 0;
				if ( !ReadUShortValue( fieldData, fieldLength, value ) ) {
					SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE, fieldId, fieldLength );
					return false;
				}
				decoded.localizationId = static_cast<mpMatchLocalizationId_t>( value );
				break;
			}
			case RESULT_FIELD_PARAMETERS:
				if ( !DecodeArgumentList( fieldData, fieldLength, decoded.parameters,
					MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS, decoded.parameterCount, error ) ) {
					return false;
				}
				break;
			default:
				SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, fieldId, rawTag );
				return false;
		}
	}

	if ( message.GetRemainingReadBits() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRAILING_DATA, 0, message.GetRemainingReadBits() );
		return false;
	}
	const unsigned int required =
		( 1u << RESULT_FIELD_SCHEMA ) |
		( 1u << RESULT_FIELD_REQUEST_ID ) |
		( 1u << RESULT_FIELD_OPCODE ) |
		( 1u << RESULT_FIELD_STATUS ) |
		( 1u << RESULT_FIELD_REASON ) |
		( 1u << RESULT_FIELD_REVISION ) |
		( 1u << RESULT_FIELD_LOCALIZATION ) |
		( 1u << RESULT_FIELD_PARAMETERS );
	unsigned int present = 0;
	for ( int i = 1; i < 32; ++i ) {
		if ( i < 128 && seen[ i ] ) {
			present |= 1u << i;
		}
	}
	if ( ( present & required ) != required ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD, 0, required & ~present );
		return false;
	}
	result = decoded;
	return true;
}

static bool BuildRequestPayload( const mpMatchOperationRequest_t &request, byte *data,
	int &length, mpMatchProtocolError_t *error ) {
	idBitMsg payload;
	payload.Init( data, MP_MATCH_MAX_PAYLOAD_BYTES );
	payload.SetAllowOverflow( true );
	payload.BeginWriting();
	const int fieldCount = 8 + ( request.hasParticipantTarget ? 1 : 0 ) +
		( request.hasTeamTarget ? 1 : 0 );
	payload.WriteByte( fieldCount );
	WriteUShortField( payload, REQUEST_FIELD_SCHEMA, request.schemaVersion );
	WriteUIntField( payload, REQUEST_FIELD_REQUEST_ID, request.requestId );
	WriteUShortField( payload, REQUEST_FIELD_OPCODE, static_cast<unsigned short>( request.opcode ) );
	WriteRevisionField( payload, REQUEST_FIELD_EXPECTED_REVISION, request.expectedSessionRevision );
	WriteRevisionField( payload, REQUEST_FIELD_CONTROL_REVISION,
		request.expectedControlRevision );
	WriteByteField( payload, REQUEST_FIELD_ACTOR_SLOT, request.actorSlot );
	WriteUIntField( payload, REQUEST_FIELD_BINDING_GENERATION, request.actorBindingGeneration );
	if ( request.hasParticipantTarget ) {
		WriteUIntField( payload, REQUEST_FIELD_PARTICIPANT_TARGET, request.participantTarget );
	}
	if ( request.hasTeamTarget ) {
		WriteByteField( payload, REQUEST_FIELD_TEAM_TARGET, static_cast<unsigned char>( request.teamTarget ) );
	}
	WriteArgumentList( payload, REQUEST_FIELD_ARGUMENTS, request.arguments, request.argumentCount );
	if ( payload.IsOverflowed() || payload.GetSize() > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, payload.GetSize() );
		return false;
	}
	length = payload.GetSize();
	return true;
}

static bool BuildResultPayload( const mpMatchOperationResult_t &result, byte *data,
	int &length, mpMatchProtocolError_t *error ) {
	idBitMsg payload;
	payload.Init( data, MP_MATCH_MAX_PAYLOAD_BYTES );
	payload.SetAllowOverflow( true );
	payload.BeginWriting();
	payload.WriteByte( 8 );
	WriteUShortField( payload, RESULT_FIELD_SCHEMA, result.schemaVersion );
	WriteUIntField( payload, RESULT_FIELD_REQUEST_ID, result.requestId );
	WriteUShortField( payload, RESULT_FIELD_OPCODE, static_cast<unsigned short>( result.opcode ) );
	WriteByteField( payload, RESULT_FIELD_STATUS, static_cast<unsigned char>( result.status ) );
	WriteUShortField( payload, RESULT_FIELD_REASON, static_cast<unsigned short>( result.reason ) );
	WriteRevisionField( payload, RESULT_FIELD_REVISION, result.resultingSessionRevision );
	WriteUShortField( payload, RESULT_FIELD_LOCALIZATION, static_cast<unsigned short>( result.localizationId ) );
	WriteArgumentList( payload, RESULT_FIELD_PARAMETERS, result.parameters, result.parameterCount );
	if ( payload.IsOverflowed() || payload.GetSize() > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, payload.GetSize() );
		return false;
	}
	length = payload.GetSize();
	return true;
}

static bool WriteEnvelope( idBitMsg &message, mpMatchEnvelopeKind_t kind,
	mpMatchProtocolSessionId_t sessionId, const byte *payload, int payloadLength,
	mpMatchProtocolError_t *error ) {
	if ( sessionId == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID );
		return false;
	}
	if ( payloadLength < 0 || payloadLength > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, payloadLength );
		return false;
	}
	if ( message.GetWriteBit() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ALIGNMENT );
		return false;
	}

	byte encoded[ MP_MATCH_PROTOCOL_MAX_MESSAGE_BYTES ];
	idBitMsg staging;
	staging.Init( encoded, sizeof( encoded ) );
	staging.SetAllowOverflow( true );
	staging.BeginWriting();
	staging.WriteUShort( MP_MATCH_PROTOCOL_MAGIC );
	staging.WriteUShort( MP_MATCH_PROTOCOL_SCHEMA_VERSION );
	staging.WriteByte( kind );
	WriteUInt64Value( staging, sessionId );
	staging.WriteUShort( payloadLength );
	if ( payloadLength > 0 ) {
		staging.WriteData( payload, payloadLength );
	}
	if ( staging.IsOverflowed() || staging.GetSize() > MP_MATCH_PROTOCOL_MAX_MESSAGE_BYTES ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, staging.GetSize() );
		return false;
	}
	if ( message.IsOverflowed() || message.GetRemainingWriteBits() < staging.GetSize() * 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_BUFFER_TOO_SMALL, 0, staging.GetSize() );
		return false;
	}

	int savedSize = 0;
	int savedBit = 0;
	message.SaveWriteState( savedSize, savedBit );
	message.WriteData( encoded, staging.GetSize() );
	if ( message.IsOverflowed() ) {
		message.RestoreWriteState( savedSize, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_BUFFER_TOO_SMALL, 0, staging.GetSize() );
		return false;
	}
	return true;
}

static bool ReadEnvelope( const idBitMsg &message, mpMatchEnvelopeKind_t expectedKind,
	mpMatchProtocolSessionId_t &sessionId, byte *payload, int &payloadLength,
	mpMatchTrailingDataPolicy_t trailingPolicy,
	mpMatchProtocolError_t *error ) {
	int savedCount = 0;
	int savedBit = 0;
	message.SaveReadState( savedCount, savedBit );
	if ( message.GetReadBit() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ALIGNMENT );
		return false;
	}
	if ( message.GetRemainingReadBits() < MP_MATCH_ENVELOPE_HEADER_BYTES * 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	const int magic = message.ReadUShort();
	const int schema = message.ReadUShort();
	const int kind = message.ReadByte();
	const unsigned int sessionLow = static_cast<unsigned int>( message.ReadLong() );
	const unsigned int sessionHigh = static_cast<unsigned int>( message.ReadLong() );
	const mpMatchProtocolSessionId_t wireSessionId =
		static_cast<mpMatchProtocolSessionId_t>( sessionLow ) |
		( static_cast<mpMatchProtocolSessionId_t>( sessionHigh ) << 32 );
	const int wirePayloadLength = message.ReadUShort();
	if ( magic != MP_MATCH_PROTOCOL_MAGIC ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE, 0, magic );
		return false;
	}
	if ( schema != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA, 0, schema );
		return false;
	}
	if ( kind != expectedKind ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE, 0, kind );
		return false;
	}
	if ( wireSessionId == 0 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID );
		return false;
	}
	if ( wirePayloadLength < 0 || wirePayloadLength > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, wirePayloadLength );
		return false;
	}
	if ( message.GetRemainingReadBits() < wirePayloadLength * 8 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, 0, wirePayloadLength );
		return false;
	}
	if ( wirePayloadLength > 0 && message.ReadData( payload, wirePayloadLength ) != wirePayloadLength ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, 0, wirePayloadLength );
		return false;
	}
	if ( trailingPolicy == MP_MATCH_TRAILING_REJECT && message.GetRemainingReadBits() != 0 ) {
		const int trailingBits = message.GetRemainingReadBits();
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRAILING_DATA, 0, trailingBits );
		return false;
	}
	sessionId = wireSessionId;
	payloadLength = wirePayloadLength;
	return true;
}

static bool IsStructuralFailureReason( mpMatchProtocolReason_t reason ) {
	return reason >= MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA && reason <= MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID;
}

} // namespace

void mpMatchOperationValue_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	type = MP_MATCH_VALUE_INVALID;
}

void mpMatchOperationValue_t::SetBool( bool value ) {
	Clear();
	type = MP_MATCH_VALUE_BOOL;
	unsignedValue = value ? 1u : 0u;
}

void mpMatchOperationValue_t::SetInt32( int value ) {
	Clear();
	type = MP_MATCH_VALUE_INT32;
	signedValue = value;
}

void mpMatchOperationValue_t::SetUInt32( unsigned int value ) {
	Clear();
	type = MP_MATCH_VALUE_UINT32;
	unsignedValue = value;
}

void mpMatchOperationValue_t::SetEnum( unsigned short value ) {
	Clear();
	type = MP_MATCH_VALUE_ENUM;
	enumValue = value;
}

bool mpMatchOperationValue_t::SetString( const char *value, int length ) {
	if ( length < 0 ) {
		if ( value == 0 ) {
			return false;
		}
		length = 0;
		while ( length <= MP_MATCH_PROTOCOL_MAX_STRING_BYTES && value[ length ] != '\0' ) {
			++length;
		}
	}
	if ( length < 0 || length > MP_MATCH_PROTOCOL_MAX_STRING_BYTES || ( value == 0 && length != 0 ) ) {
		return false;
	}
	for ( int i = 0; i < length; ++i ) {
		if ( value[ i ] == '\0' ) {
			return false;
		}
	}
	Clear();
	type = MP_MATCH_VALUE_STRING;
	stringLength = static_cast<unsigned short>( length );
	if ( length > 0 ) {
		memcpy( stringValue, value, length );
	}
	stringValue[ length ] = '\0';
	return true;
}

void mpMatchOperationValue_t::SetOpcode( mpMatchOperationOpcode_t value ) {
	Clear();
	type = MP_MATCH_VALUE_OPCODE;
	enumValue = static_cast<unsigned short>( value );
}

void mpMatchOperationValue_t::SetParticipantId( mpMatchProtocolParticipantId_t value ) {
	Clear();
	type = MP_MATCH_VALUE_PARTICIPANT_ID;
	unsignedValue = value;
}

void mpMatchOperationArgument_t::Clear( void ) {
	fieldId = MP_MATCH_ARG_INVALID;
	value.Clear();
}

void mpMatchOperationRequest_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	opcode = MP_MATCH_OP_INVALID;
	teamTarget = MP_MATCH_TEAM_NONE;
	for ( int i = 0; i < MP_MATCH_PROTOCOL_MAX_ARGUMENTS; ++i ) {
		arguments[ i ].Clear();
	}
}

void mpMatchOperationResult_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	opcode = MP_MATCH_OP_INVALID;
	status = MP_MATCH_RESULT_REJECTED;
	reason = MP_MATCH_PROTOCOL_REASON_NONE;
	localizationId = MP_MATCH_LOCALIZATION_NONE;
	for ( int i = 0; i < MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS; ++i ) {
		parameters[ i ].Clear();
	}
}

void mpMatchProtocolEnvelope_t::Clear( void ) {
	schemaVersion = 0;
	kind = MP_MATCH_ENVELOPE_INVALID;
	sessionId = 0;
	payloadBytes = 0;
}

void mpMatchProtocolError_t::Clear( void ) {
	reason = MP_MATCH_PROTOCOL_REASON_NONE;
	fieldId = 0;
	detail = 0;
}

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor( mpMatchOperationOpcode_t opcode ) {
	const int index = static_cast<int>( opcode ) - 1;
	if ( index < 0 || index >= MP_MATCH_OP_COUNT - 1 ) {
		return 0;
	}
	const mpMatchOperationDescriptor_t &descriptor = OPERATION_DESCRIPTORS[ index ];
	return descriptor.opcode == opcode ? &descriptor : 0;
}

int MPMatchOperationDescriptorCount( void ) {
	return sizeof( OPERATION_DESCRIPTORS ) / sizeof( OPERATION_DESCRIPTORS[ 0 ] );
}

bool MPMatchProtocolValidateRegistry( mpMatchProtocolError_t *error ) {
	ClearError( error );
	return ValidateRegistryInternal( error );
}

bool MPMatchProtocolValidateRequest( const mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( !RegistryIsValid() ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID );
		return false;
	}
	if ( request.schemaVersion != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA, REQUEST_FIELD_SCHEMA, request.schemaVersion );
		return false;
	}
	if ( request.sessionId == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID );
		return false;
	}
	if ( request.requestId == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_REQUEST_ID, REQUEST_FIELD_REQUEST_ID );
		return false;
	}
	if ( request.expectedControlRevision == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_STALE_REVISION,
			REQUEST_FIELD_CONTROL_REVISION );
		return false;
	}
	const mpMatchOperationDescriptor_t *descriptor = MPMatchOperationDescriptor( request.opcode );
	if ( descriptor == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE, REQUEST_FIELD_OPCODE,
			static_cast<unsigned int>( request.opcode ) );
		return false;
	}
	if ( request.actorSlot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_ACTOR_SLOT, REQUEST_FIELD_ACTOR_SLOT, request.actorSlot );
		return false;
	}
	if ( request.actorBindingGeneration == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_BINDING_GENERATION, REQUEST_FIELD_BINDING_GENERATION );
		return false;
	}
	if ( request.argumentCount > MP_MATCH_PROTOCOL_MAX_ARGUMENTS ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, REQUEST_FIELD_ARGUMENTS, request.argumentCount );
		return false;
	}
	if ( !ValidateTargetPolicy( *descriptor, request.hasParticipantTarget, request.participantTarget,
		request.hasTeamTarget, request.teamTarget, error ) ) {
		return false;
	}
	if ( !ValidateRequestArguments( request, *descriptor, error ) ) {
		return false;
	}
	if ( !ValidateVetoArgumentShape( request, error ) ) {
		return false;
	}
	return true;
}

bool MPMatchProtocolValidateResult( const mpMatchOperationResult_t &result,
	mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( !RegistryIsValid() ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID );
		return false;
	}
	if ( result.schemaVersion != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA, RESULT_FIELD_SCHEMA, result.schemaVersion );
		return false;
	}
	if ( result.sessionId == 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID );
		return false;
	}
	if ( result.status < MP_MATCH_RESULT_REJECTED || result.status >= MP_MATCH_RESULT_STATUS_COUNT ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, RESULT_FIELD_STATUS, result.status );
		return false;
	}
	if ( result.reason < MP_MATCH_PROTOCOL_REASON_NONE || result.reason >= MP_MATCH_PROTOCOL_REASON_COUNT ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, RESULT_FIELD_REASON, result.reason );
		return false;
	}
	const bool structuralRejection = result.status == MP_MATCH_RESULT_REJECTED &&
		IsStructuralFailureReason( result.reason );
	if ( result.requestId == 0 && !structuralRejection ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_REQUEST_ID, RESULT_FIELD_REQUEST_ID );
		return false;
	}
	if ( MPMatchOperationDescriptor( result.opcode ) == 0 &&
		!( result.opcode == MP_MATCH_OP_INVALID && structuralRejection ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE, RESULT_FIELD_OPCODE,
			static_cast<unsigned int>( result.opcode ) );
		return false;
	}
	if ( result.status == MP_MATCH_RESULT_REJECTED &&
		( result.reason == MP_MATCH_PROTOCOL_REASON_NONE || result.reason == MP_MATCH_PROTOCOL_REASON_OK ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, RESULT_FIELD_REASON, result.reason );
		return false;
	}
	if ( result.status != MP_MATCH_RESULT_REJECTED &&
		result.reason != MP_MATCH_PROTOCOL_REASON_NONE && result.reason != MP_MATCH_PROTOCOL_REASON_OK ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, RESULT_FIELD_REASON, result.reason );
		return false;
	}
	if ( !IsValidLocalizationId( result.localizationId ) ||
		( result.status == MP_MATCH_RESULT_REJECTED &&
			result.localizationId != MPMatchProtocolReasonLocalizationId( result.reason ) ) ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, RESULT_FIELD_LOCALIZATION, result.localizationId );
		return false;
	}
	if ( result.parameterCount > MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT, RESULT_FIELD_PARAMETERS, result.parameterCount );
		return false;
	}
	bool seen[ MP_MATCH_NESTED_ARGUMENT_BASE ];
	memset( seen, 0, sizeof( seen ) );
	for ( int i = 0; i < result.parameterCount; ++i ) {
		const mpMatchOperationArgument_t &parameter = result.parameters[ i ];
		if ( parameter.fieldId < MP_MATCH_NESTED_ARGUMENT_BASE && seen[ parameter.fieldId ] ) {
			SetError( error, MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD, parameter.fieldId, i );
			return false;
		}
		if ( parameter.fieldId < MP_MATCH_NESTED_ARGUMENT_BASE ) {
			seen[ parameter.fieldId ] = true;
		}
		if ( !ValidateResultParameter( parameter, error ) ) {
			return false;
		}
	}
	return true;
}

bool MPMatchProtocolEncodeRequest( idBitMsg &message, const mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( !MPMatchProtocolValidateRequest( request, error ) ) {
		return false;
	}
	byte payload[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	int payloadLength = 0;
	if ( !BuildRequestPayload( request, payload, payloadLength, error ) ) {
		return false;
	}
	return WriteEnvelope( message, MP_MATCH_ENVELOPE_REQUEST, request.sessionId,
		payload, payloadLength, error );
}

bool MPMatchProtocolDecodeRequest( const idBitMsg &message, mpMatchOperationRequest_t &request,
	mpMatchTrailingDataPolicy_t trailingPolicy, mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( trailingPolicy != MP_MATCH_TRAILING_REJECT && trailingPolicy != MP_MATCH_TRAILING_ALLOW ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, 0, trailingPolicy );
		return false;
	}
	int savedCount = 0;
	int savedBit = 0;
	message.SaveReadState( savedCount, savedBit );
	mpMatchProtocolSessionId_t sessionId = 0;
	byte payload[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	int payloadLength = 0;
	if ( !ReadEnvelope( message, MP_MATCH_ENVELOPE_REQUEST, sessionId, payload,
		payloadLength, trailingPolicy, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	mpMatchOperationRequest_t decoded;
	decoded.Clear();
	if ( !DecodeRequestPayload( payload, payloadLength, decoded, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	decoded.sessionId = sessionId;
	if ( !MPMatchProtocolValidateRequest( decoded, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	request = decoded;
	return true;
}

bool MPMatchProtocolEncodeResult( idBitMsg &message, const mpMatchOperationResult_t &result,
	mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( !MPMatchProtocolValidateResult( result, error ) ) {
		return false;
	}
	byte payload[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	int payloadLength = 0;
	if ( !BuildResultPayload( result, payload, payloadLength, error ) ) {
		return false;
	}
	return WriteEnvelope( message, MP_MATCH_ENVELOPE_RESULT, result.sessionId,
		payload, payloadLength, error );
}

bool MPMatchProtocolDecodeResult( const idBitMsg &message, mpMatchOperationResult_t &result,
	mpMatchTrailingDataPolicy_t trailingPolicy, mpMatchProtocolError_t *error ) {
	ClearError( error );
	if ( trailingPolicy != MP_MATCH_TRAILING_REJECT && trailingPolicy != MP_MATCH_TRAILING_ALLOW ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE, 0, trailingPolicy );
		return false;
	}
	int savedCount = 0;
	int savedBit = 0;
	message.SaveReadState( savedCount, savedBit );
	mpMatchProtocolSessionId_t sessionId = 0;
	byte payload[ MP_MATCH_MAX_PAYLOAD_BYTES ];
	int payloadLength = 0;
	if ( !ReadEnvelope( message, MP_MATCH_ENVELOPE_RESULT, sessionId, payload,
		payloadLength, trailingPolicy, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	mpMatchOperationResult_t decoded;
	decoded.Clear();
	if ( !DecodeResultPayload( payload, payloadLength, decoded, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	decoded.sessionId = sessionId;
	if ( !MPMatchProtocolValidateResult( decoded, error ) ) {
		message.RestoreReadState( savedCount, savedBit );
		return false;
	}
	result = decoded;
	return true;
}

bool MPMatchProtocolPeekEnvelope( const idBitMsg &message, mpMatchProtocolEnvelope_t &envelope,
	mpMatchProtocolError_t *error ) {
	ClearError( error );
	int savedCount = 0;
	int savedBit = 0;
	message.SaveReadState( savedCount, savedBit );
	mpMatchProtocolEnvelope_t decoded;
	decoded.Clear();
	if ( message.GetReadBit() != 0 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_ALIGNMENT );
		return false;
	}
	if ( message.GetRemainingReadBits() < MP_MATCH_ENVELOPE_HEADER_BYTES * 8 ) {
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED );
		return false;
	}
	const int magic = message.ReadUShort();
	const int schema = message.ReadUShort();
	const int kind = message.ReadByte();
	const unsigned int sessionLow = static_cast<unsigned int>( message.ReadLong() );
	const unsigned int sessionHigh = static_cast<unsigned int>( message.ReadLong() );
	const mpMatchProtocolSessionId_t sessionId =
		static_cast<mpMatchProtocolSessionId_t>( sessionLow ) |
		( static_cast<mpMatchProtocolSessionId_t>( sessionHigh ) << 32 );
	const int payloadLength = message.ReadUShort();
	if ( magic != MP_MATCH_PROTOCOL_MAGIC ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE, 0, magic );
		return false;
	}
	if ( schema != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA, 0, schema );
		return false;
	}
	if ( kind <= MP_MATCH_ENVELOPE_INVALID || kind >= MP_MATCH_ENVELOPE_COUNT ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE, 0, kind );
		return false;
	}
	if ( sessionId == 0 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID );
		return false;
	}
	if ( payloadLength < 0 || payloadLength > MP_MATCH_MAX_PAYLOAD_BYTES ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE, 0, payloadLength );
		return false;
	}
	if ( message.GetRemainingReadBits() < payloadLength * 8 ) {
		message.RestoreReadState( savedCount, savedBit );
		SetError( error, MP_MATCH_PROTOCOL_REASON_TRUNCATED, 0, payloadLength );
		return false;
	}
	decoded.schemaVersion = static_cast<unsigned short>( schema );
	decoded.kind = static_cast<mpMatchEnvelopeKind_t>( kind );
	decoded.sessionId = sessionId;
	decoded.payloadBytes = static_cast<unsigned short>( payloadLength );
	message.RestoreReadState( savedCount, savedBit );
	envelope = decoded;
	return true;
}

mpMatchLocalizationId_t MPMatchProtocolReasonLocalizationId( mpMatchProtocolReason_t reason ) {
	if ( reason <= MP_MATCH_PROTOCOL_REASON_NONE || reason >= MP_MATCH_PROTOCOL_REASON_COUNT ) {
		return MP_MATCH_LOCALIZATION_NONE;
	}
	return static_cast<mpMatchLocalizationId_t>( MP_MATCH_LOCALIZATION_REASON_BASE + reason );
}
