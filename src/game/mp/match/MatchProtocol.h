//----------------------------------------------------------------
// MatchProtocol.h
//
// Versioned, bounded multiplayer match-operation wire contract.
//
// This layer deliberately contains no game-session state and no command
// strings.  It describes an operation, validates its structural shape, and
// serializes requests/results.  Principal resolution, authorization, phase
// policy beyond the descriptor mask, semantic validation, and commit are the
// responsibility of the authoritative match-session adapter.
//----------------------------------------------------------------

#ifndef __MP_MATCH_PROTOCOL_H__
#define __MP_MATCH_PROTOCOL_H__

#include "../MatchPhase.h"
#include <stddef.h>

class idBitMsg;

static const unsigned short MP_MATCH_PROTOCOL_MAGIC = 0x514d;
// Version 2 adds a required aggregate control revision to every request.  It
// prevents a client projection from authorizing an operation after any of the
// independently versioned session, rules, proposal, team or series aggregates
// has changed.
static const unsigned short MP_MATCH_PROTOCOL_SCHEMA_VERSION = 2;
// Registry version 5 appends stable participant removal and typed Duel
// contestant binding after the earlier starting-side veto additions.  No
// opcode, field or enum value is reused.
static const unsigned short MP_MATCH_OPERATION_REGISTRY_VERSION = 5;

static const int MP_MATCH_PROTOCOL_MAX_MESSAGE_BYTES = 1024;
static const int MP_MATCH_PROTOCOL_MAX_TOP_LEVEL_FIELDS = 16;
static const int MP_MATCH_PROTOCOL_MAX_ARGUMENTS = 8;
static const int MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS = 4;
static const int MP_MATCH_PROTOCOL_MAX_STRING_BYTES = 96;
static const int MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS = 32;
static const int MP_MATCH_NESTED_ARGUMENT_BASE = 64;

// Session-local sequence only.  The authoritative adapter resolves this in
// the current session; the wire never accepts a client-supplied session id.
typedef unsigned int mpMatchProtocolParticipantId_t;
static const mpMatchProtocolParticipantId_t MP_MATCH_INVALID_PARTICIPANT_ID = 0u;

// Session revisions are 64-bit compare-and-swap values end to end.  The wire
// codec serializes them as fixed low/high 32-bit words via idBitMsg.
typedef unsigned long long mpMatchProtocolRevision_t;
typedef unsigned long long mpMatchProtocolSessionId_t;

// Stable, append-only operation values.  Never reuse a retired value.
typedef enum {
	MP_MATCH_OP_INVALID = 0,
	MP_MATCH_OP_READY_SET = 1,
	MP_MATCH_OP_TEAM_READY_SET = 2,
	MP_MATCH_OP_FORCE_READY = 3,
	MP_MATCH_OP_TEAM_JOIN = 4,
	MP_MATCH_OP_TEAM_LOCK_SET = 5,
	MP_MATCH_OP_QUEUE_JOIN = 6,
	MP_MATCH_OP_QUEUE_DEFER = 7,
	MP_MATCH_OP_QUEUE_LEAVE = 8,
	MP_MATCH_OP_TIMEOUT_REQUEST = 9,
	MP_MATCH_OP_TECH_PAUSE_REQUEST = 10,
	MP_MATCH_OP_RESUME_REQUEST = 11,
	MP_MATCH_OP_REF_AUTHENTICATE = 12,
	MP_MATCH_OP_REF_LOGOUT = 13,
	MP_MATCH_OP_RULES_SELECT_PROFILE = 14,
	MP_MATCH_OP_RULES_STAGE_FIELD = 15,
	MP_MATCH_OP_RULES_COMMIT = 16,
	MP_MATCH_OP_RULES_DISCARD = 17,
	MP_MATCH_OP_PROPOSAL_CREATE = 18,
	MP_MATCH_OP_PROPOSAL_CAST = 19,
	MP_MATCH_OP_PROPOSAL_CANCEL = 20,
	MP_MATCH_OP_ROSTER_INVITE = 21,
	MP_MATCH_OP_ROSTER_ACCEPT = 22,
	MP_MATCH_OP_ROSTER_REMOVE = 23,
	MP_MATCH_OP_ROSTER_SUBSTITUTE = 24,
	MP_MATCH_OP_ROLE_ASSIGN = 25,
	MP_MATCH_OP_SERIES_STAGE_PROFILE = 26,
	MP_MATCH_OP_SERIES_START = 27,
	MP_MATCH_OP_SERIES_CANCEL = 28,
	MP_MATCH_OP_SERIES_ADVANCE = 29,
	MP_MATCH_OP_VETO_SELECT = 30,
	MP_MATCH_OP_FORFEIT = 31,
	MP_MATCH_OP_ABORT = 32,
	MP_MATCH_OP_BROADCASTER_SET = 33,
	MP_MATCH_OP_ROSTER_LEAVE = 34,
	// A removal target is always a session-scoped ParticipantId.  Connection
	// slots are transport bindings and are never legal operation identities.
	MP_MATCH_OP_PARTICIPANT_REMOVE = 35,
	// Binds a current Duel participant to the abstract competition side A/B.
	// The adapter may retain a connection-lifetime binding, but the request and
	// continuation remain stable-identity based.
	MP_MATCH_OP_SERIES_CONTESTANT_BIND = 36,
	MP_MATCH_OP_COUNT = 37
} mpMatchOperationOpcode_t;

// Capability bits are wire-neutral policy identifiers, not an ordered role
// hierarchy.  A principal may receive any combination from the session.
typedef unsigned int mpMatchProtocolCapabilityMask_t;
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_READY_SELF = ( 1u << 0 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_READY_TEAM = ( 1u << 1 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_FORCE_READY = ( 1u << 2 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_TEAM_SELF = ( 1u << 3 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_TEAM_LOCK = ( 1u << 4 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_QUEUE = ( 1u << 5 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_TIMEOUT_TEAM = ( 1u << 6 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_PAUSE_TECHNICAL = ( 1u << 7 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_RESUME = ( 1u << 8 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_REFEREE_SESSION = ( 1u << 9 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_RULES_STAGE = ( 1u << 10 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_RULES_COMMIT = ( 1u << 11 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_PROPOSAL_CREATE = ( 1u << 12 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_PROPOSAL_CAST = ( 1u << 13 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_PROPOSAL_CANCEL = ( 1u << 14 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ROSTER_SELF = ( 1u << 15 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE = ( 1u << 16 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ROLE_ASSIGN = ( 1u << 17 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE = ( 1u << 18 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_VETO_SELECT = ( 1u << 19 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_FORFEIT = ( 1u << 20 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ABORT = ( 1u << 21 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN = ( 1u << 22 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ROSTER_LEAVE_SELF = ( 1u << 23 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE = ( 1u << 24 );
static const mpMatchProtocolCapabilityMask_t MP_MATCH_PROTOCOL_CAP_ALL = ( 1u << 25 ) - 1u;

// Legal-phase bits are derived from the canonical append-only mpGameState_t
// values in MatchPhase.h.  They are masks, never a duplicate phase enum.
typedef unsigned int mpMatchPhaseMask_t;
static const mpMatchPhaseMask_t MP_MATCH_PHASE_INACTIVE = ( 1u << INACTIVE );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_WARMUP = ( 1u << WARMUP );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_COUNTDOWN = ( 1u << COUNTDOWN );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_GAMEON = ( 1u << GAMEON );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_SUDDENDEATH = ( 1u << SUDDENDEATH );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_GAMEREVIEW = ( 1u << GAMEREVIEW );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_NEXTGAME = ( 1u << NEXTGAME );
static const mpMatchPhaseMask_t MP_MATCH_PHASE_ALL = ( 1u << STATE_COUNT ) - 1u;

typedef enum {
	MP_MATCH_TEAM_NONE = 0,
	MP_MATCH_TEAM_MARINE = 1,
	MP_MATCH_TEAM_STROGG = 2,
	MP_MATCH_TEAM_SPECTATOR = 3,
	MP_MATCH_TEAM_COUNT
} mpMatchTeam_t;

typedef enum {
	MP_MATCH_VALUE_INVALID = 0,
	MP_MATCH_VALUE_BOOL = 1,
	MP_MATCH_VALUE_INT32 = 2,
	MP_MATCH_VALUE_UINT32 = 3,
	MP_MATCH_VALUE_ENUM = 4,
	MP_MATCH_VALUE_STRING = 5,
	MP_MATCH_VALUE_OPCODE = 6,
	MP_MATCH_VALUE_PARTICIPANT_ID = 7,
	MP_MATCH_VALUE_TYPE_COUNT,
	// Descriptor-only sentinel; it is never legal on the wire.
	MP_MATCH_VALUE_ANY_SCALAR = 255
} mpMatchValueType_t;

// Stable semantic field identifiers.  Proposal target arguments are encoded
// as MP_MATCH_NESTED_ARGUMENT_BASE plus one of these identifiers.
typedef enum {
	MP_MATCH_ARG_INVALID = 0,
	MP_MATCH_ARG_ENABLED = 1,
	MP_MATCH_ARG_REASON = 2,
	MP_MATCH_ARG_PROFILE = 3,
	MP_MATCH_ARG_SETTING_ID = 4,
	MP_MATCH_ARG_SETTING_VALUE = 5,
	MP_MATCH_ARG_PROPOSED_OPCODE = 6,
	MP_MATCH_ARG_BALLOT_CHOICE = 7,
	MP_MATCH_ARG_REPLACEMENT_PARTICIPANT = 8,
	MP_MATCH_ARG_ROLE = 9,
	MP_MATCH_ARG_SERIES_PROFILE = 10,
	MP_MATCH_ARG_VETO_ACTION = 11,
	MP_MATCH_ARG_MAP_TOKEN = 12,
	MP_MATCH_ARG_PROPOSAL_ID = 13,
	MP_MATCH_ARG_INVITATION_ID = 14,
	MP_MATCH_ARG_BEST_OF = 15,
	MP_MATCH_ARG_CREDENTIAL = 16,
	// Append-only.  This is a map/game-side choice, not the actor's roster
	// team and therefore is carried as a veto argument rather than teamTarget.
	MP_MATCH_ARG_STARTING_SIDE = 17,
	// Abstract Duel competition side.  This is neither a gameplay team nor a
	// connection slot, and must be mapped explicitly at every policy boundary.
	MP_MATCH_ARG_COMPETITION_SIDE = 18,
	MP_MATCH_ARG_COUNT
} mpMatchArgumentField_t;

typedef enum {
	MP_MATCH_BALLOT_YES = 1,
	MP_MATCH_BALLOT_NO = 2,
	MP_MATCH_BALLOT_ABSTAIN = 3
} mpMatchBallotChoice_t;

typedef enum {
	MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER = 1,
	MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN = 2,
	MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH = 3,
	MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE = 4
} mpMatchProtocolRosterRole_t;

typedef enum {
	MP_MATCH_VETO_BAN = 1,
	MP_MATCH_VETO_PICK = 2,
	MP_MATCH_VETO_DECIDER = 3,
	// Append-only: selects the starting in-map side for a previously selected
	// map.  MP_MATCH_ARG_STARTING_SIDE is mandatory for this action and
	// forbidden for every other veto action.
	MP_MATCH_VETO_SIDE = 4
} mpMatchVetoAction_t;

typedef enum {
	MP_MATCH_STARTING_SIDE_MARINE = 1,
	MP_MATCH_STARTING_SIDE_STROGG = 2
} mpMatchStartingSide_t;

typedef enum {
	MP_MATCH_COMPETITION_SIDE_A = 1,
	MP_MATCH_COMPETITION_SIDE_B = 2
} mpMatchCompetitionSide_t;

typedef enum {
	MP_MATCH_COOLDOWN_NONE = 0,
	MP_MATCH_COOLDOWN_INTERACTION,
	MP_MATCH_COOLDOWN_TEAM_ACTION,
	MP_MATCH_COOLDOWN_PRIVILEGED,
	MP_MATCH_COOLDOWN_COUNT
} mpMatchCooldownClass_t;

typedef enum {
	MP_MATCH_OPERATION_FLAG_NONE = 0,
	MP_MATCH_OPERATION_FLAG_PROPOSABLE = ( 1u << 0 ),
	MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET = ( 1u << 1 ),
	MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET = ( 1u << 2 ),
	MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET = ( 1u << 3 ),
	MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET = ( 1u << 4 ),
	MP_MATCH_OPERATION_FLAG_SENSITIVE = ( 1u << 5 ),
	MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS = ( 1u << 6 )
} mpMatchOperationFlags_t;

typedef enum {
	MP_MATCH_ARGUMENT_FLAG_NONE = 0,
	MP_MATCH_ARGUMENT_FLAG_STRING_PRINTABLE = ( 1u << 0 ),
	MP_MATCH_ARGUMENT_FLAG_STRING_TOKEN = ( 1u << 1 ),
	MP_MATCH_ARGUMENT_FLAG_STRING_MAP_TOKEN = ( 1u << 2 ),
	MP_MATCH_ARGUMENT_FLAG_SENSITIVE = ( 1u << 3 )
} mpMatchArgumentFlags_t;

// Numeric localization keys are intentionally free of presentation text.
// UI adapters map them to localized language-table identifiers.
typedef enum {
	MP_MATCH_LOCALIZATION_NONE = 0,
	MP_MATCH_LOCALIZATION_OPERATION_BASE = 1000,
	MP_MATCH_LOCALIZATION_OPERATION_READY_SET = 1001,
	MP_MATCH_LOCALIZATION_OPERATION_TEAM_READY_SET = 1002,
	MP_MATCH_LOCALIZATION_OPERATION_FORCE_READY = 1003,
	MP_MATCH_LOCALIZATION_OPERATION_TEAM_JOIN = 1004,
	MP_MATCH_LOCALIZATION_OPERATION_TEAM_LOCK_SET = 1005,
	MP_MATCH_LOCALIZATION_OPERATION_QUEUE_JOIN = 1006,
	MP_MATCH_LOCALIZATION_OPERATION_QUEUE_DEFER = 1007,
	MP_MATCH_LOCALIZATION_OPERATION_QUEUE_LEAVE = 1008,
	MP_MATCH_LOCALIZATION_OPERATION_TIMEOUT_REQUEST = 1009,
	MP_MATCH_LOCALIZATION_OPERATION_TECH_PAUSE_REQUEST = 1010,
	MP_MATCH_LOCALIZATION_OPERATION_RESUME_REQUEST = 1011,
	MP_MATCH_LOCALIZATION_OPERATION_REF_AUTHENTICATE = 1012,
	MP_MATCH_LOCALIZATION_OPERATION_REF_LOGOUT = 1013,
	MP_MATCH_LOCALIZATION_OPERATION_RULES_SELECT_PROFILE = 1014,
	MP_MATCH_LOCALIZATION_OPERATION_RULES_STAGE_FIELD = 1015,
	MP_MATCH_LOCALIZATION_OPERATION_RULES_COMMIT = 1016,
	MP_MATCH_LOCALIZATION_OPERATION_RULES_DISCARD = 1017,
	MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CREATE = 1018,
	MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CAST = 1019,
	MP_MATCH_LOCALIZATION_OPERATION_PROPOSAL_CANCEL = 1020,
	MP_MATCH_LOCALIZATION_OPERATION_ROSTER_INVITE = 1021,
	MP_MATCH_LOCALIZATION_OPERATION_ROSTER_ACCEPT = 1022,
	MP_MATCH_LOCALIZATION_OPERATION_ROSTER_REMOVE = 1023,
	MP_MATCH_LOCALIZATION_OPERATION_ROSTER_SUBSTITUTE = 1024,
	MP_MATCH_LOCALIZATION_OPERATION_ROLE_ASSIGN = 1025,
	MP_MATCH_LOCALIZATION_OPERATION_SERIES_STAGE_PROFILE = 1026,
	MP_MATCH_LOCALIZATION_OPERATION_SERIES_START = 1027,
	MP_MATCH_LOCALIZATION_OPERATION_SERIES_CANCEL = 1028,
	MP_MATCH_LOCALIZATION_OPERATION_SERIES_ADVANCE = 1029,
	MP_MATCH_LOCALIZATION_OPERATION_VETO_SELECT = 1030,
	MP_MATCH_LOCALIZATION_OPERATION_FORFEIT = 1031,
	MP_MATCH_LOCALIZATION_OPERATION_ABORT = 1032,
	MP_MATCH_LOCALIZATION_OPERATION_BROADCASTER_SET = 1033,
	MP_MATCH_LOCALIZATION_OPERATION_ROSTER_LEAVE = 1034,
	MP_MATCH_LOCALIZATION_OPERATION_PARTICIPANT_REMOVE = 1035,
	MP_MATCH_LOCALIZATION_OPERATION_SERIES_CONTESTANT_BIND = 1036,
	MP_MATCH_LOCALIZATION_CONFIRM_BASE = 1100,
	MP_MATCH_LOCALIZATION_CONFIRM_FORCE_READY = 1103,
	MP_MATCH_LOCALIZATION_CONFIRM_RULES_COMMIT = 1116,
	MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_REMOVE = 1123,
	MP_MATCH_LOCALIZATION_CONFIRM_ROSTER_SUBSTITUTE = 1124,
	MP_MATCH_LOCALIZATION_CONFIRM_SERIES_CANCEL = 1128,
	MP_MATCH_LOCALIZATION_CONFIRM_SERIES_START = 1127,
	MP_MATCH_LOCALIZATION_CONFIRM_SERIES_ADVANCE = 1129,
	MP_MATCH_LOCALIZATION_CONFIRM_VETO_SELECT = 1130,
	MP_MATCH_LOCALIZATION_CONFIRM_FORFEIT = 1131,
	MP_MATCH_LOCALIZATION_CONFIRM_ABORT = 1132,
	MP_MATCH_LOCALIZATION_CONFIRM_PARTICIPANT_REMOVE = 1135,
	MP_MATCH_LOCALIZATION_REASON_BASE = 2000,
	MP_MATCH_LOCALIZATION_REASON_OK = 2001,
	MP_MATCH_LOCALIZATION_REASON_UNSUPPORTED_SCHEMA = 2002,
	MP_MATCH_LOCALIZATION_REASON_UNKNOWN_ENVELOPE = 2003,
	MP_MATCH_LOCALIZATION_REASON_UNKNOWN_OPCODE = 2004,
	MP_MATCH_LOCALIZATION_REASON_UNKNOWN_FIELD = 2005,
	MP_MATCH_LOCALIZATION_REASON_TRUNCATED = 2006,
	MP_MATCH_LOCALIZATION_REASON_PAYLOAD_TOO_LARGE = 2007,
	MP_MATCH_LOCALIZATION_REASON_BUFFER_TOO_SMALL = 2008,
	MP_MATCH_LOCALIZATION_REASON_ARGUMENT_COUNT = 2009,
	MP_MATCH_LOCALIZATION_REASON_ARGUMENT_TYPE = 2010,
	MP_MATCH_LOCALIZATION_REASON_ARGUMENT_RANGE = 2011,
	MP_MATCH_LOCALIZATION_REASON_STRING_LENGTH = 2012,
	MP_MATCH_LOCALIZATION_REASON_STRING_CHARACTERS = 2013,
	MP_MATCH_LOCALIZATION_REASON_DUPLICATE_FIELD = 2014,
	MP_MATCH_LOCALIZATION_REASON_TRAILING_DATA = 2015,
	MP_MATCH_LOCALIZATION_REASON_INVALID_SESSION_ID = 2016,
	MP_MATCH_LOCALIZATION_REASON_INVALID_REQUEST_ID = 2017,
	MP_MATCH_LOCALIZATION_REASON_INVALID_ACTOR_SLOT = 2018,
	MP_MATCH_LOCALIZATION_REASON_INVALID_BINDING_GENERATION = 2019,
	MP_MATCH_LOCALIZATION_REASON_INVALID_PARTICIPANT = 2020,
	MP_MATCH_LOCALIZATION_REASON_INVALID_TEAM = 2021,
	MP_MATCH_LOCALIZATION_REASON_INVALID_TARGET = 2022,
	MP_MATCH_LOCALIZATION_REASON_NOT_PROPOSABLE = 2023,
	MP_MATCH_LOCALIZATION_REASON_REGISTRY_INVALID = 2024,
	MP_MATCH_LOCALIZATION_REASON_NOT_AUTHORIZED = 2025,
	MP_MATCH_LOCALIZATION_REASON_ILLEGAL_PHASE = 2026,
	MP_MATCH_LOCALIZATION_REASON_STALE_REVISION = 2027,
	MP_MATCH_LOCALIZATION_REASON_CONFLICT = 2028,
	MP_MATCH_LOCALIZATION_REASON_COOLDOWN = 2029,
	MP_MATCH_LOCALIZATION_REASON_INTERNAL = 2030,
	MP_MATCH_LOCALIZATION_REASON_ALIGNMENT = 2031,
	MP_MATCH_LOCALIZATION_REASON_TEAM_BALANCE = 2032,
	MP_MATCH_LOCALIZATION_COUNT = 2033
} mpMatchLocalizationId_t;

typedef enum {
	MP_MATCH_PROTOCOL_REASON_NONE = 0,
	MP_MATCH_PROTOCOL_REASON_OK,
	MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA,
	MP_MATCH_PROTOCOL_REASON_UNKNOWN_ENVELOPE,
	MP_MATCH_PROTOCOL_REASON_UNKNOWN_OPCODE,
	MP_MATCH_PROTOCOL_REASON_UNKNOWN_FIELD,
	MP_MATCH_PROTOCOL_REASON_TRUNCATED,
	MP_MATCH_PROTOCOL_REASON_PAYLOAD_TOO_LARGE,
	MP_MATCH_PROTOCOL_REASON_BUFFER_TOO_SMALL,
	MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT,
	MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE,
	MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE,
	MP_MATCH_PROTOCOL_REASON_STRING_LENGTH,
	MP_MATCH_PROTOCOL_REASON_STRING_CHARACTERS,
	MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD,
	MP_MATCH_PROTOCOL_REASON_TRAILING_DATA,
	MP_MATCH_PROTOCOL_REASON_INVALID_SESSION_ID,
	MP_MATCH_PROTOCOL_REASON_INVALID_REQUEST_ID,
	MP_MATCH_PROTOCOL_REASON_INVALID_ACTOR_SLOT,
	MP_MATCH_PROTOCOL_REASON_INVALID_BINDING_GENERATION,
	MP_MATCH_PROTOCOL_REASON_INVALID_PARTICIPANT,
	MP_MATCH_PROTOCOL_REASON_INVALID_TEAM,
	MP_MATCH_PROTOCOL_REASON_INVALID_TARGET,
	MP_MATCH_PROTOCOL_REASON_NOT_PROPOSABLE,
	MP_MATCH_PROTOCOL_REASON_REGISTRY_INVALID,
	MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED,
	MP_MATCH_PROTOCOL_REASON_ILLEGAL_PHASE,
	MP_MATCH_PROTOCOL_REASON_STALE_REVISION,
	MP_MATCH_PROTOCOL_REASON_CONFLICT,
	MP_MATCH_PROTOCOL_REASON_COOLDOWN,
	MP_MATCH_PROTOCOL_REASON_INTERNAL,
	MP_MATCH_PROTOCOL_REASON_ALIGNMENT,
	// Appended: the server's team-balance rule (si_autobalance) refuses a
	// voluntary team join, so the join is rejected before anything commits.
	MP_MATCH_PROTOCOL_REASON_TEAM_BALANCE,
	MP_MATCH_PROTOCOL_REASON_COUNT
} mpMatchProtocolReason_t;

typedef enum {
	MP_MATCH_RESULT_REJECTED = 0,
	MP_MATCH_RESULT_COMMITTED,
	MP_MATCH_RESULT_NO_CHANGE,
	MP_MATCH_RESULT_PENDING,
	MP_MATCH_RESULT_STATUS_COUNT
} mpMatchOperationResultStatus_t;

typedef enum {
	MP_MATCH_ENVELOPE_INVALID = 0,
	MP_MATCH_ENVELOPE_REQUEST = 1,
	MP_MATCH_ENVELOPE_RESULT = 2,
	MP_MATCH_ENVELOPE_SESSION_VIEW = 3,
	MP_MATCH_ENVELOPE_COUNT
} mpMatchEnvelopeKind_t;

typedef enum {
	MP_MATCH_TRAILING_REJECT = 0,
	MP_MATCH_TRAILING_ALLOW = 1
} mpMatchTrailingDataPolicy_t;

typedef struct mpMatchOperationValue_s {
	mpMatchValueType_t	type;
	int					signedValue;
	unsigned int			unsignedValue;
	unsigned short		enumValue;
	unsigned short		stringLength;
	char				stringValue[ MP_MATCH_PROTOCOL_MAX_STRING_BYTES + 1 ];

	void	Clear( void );
	void	SetBool( bool value );
	void	SetInt32( int value );
	void	SetUInt32( unsigned int value );
	void	SetEnum( unsigned short value );
	bool	SetString( const char *value, int length = -1 );
	void	SetOpcode( mpMatchOperationOpcode_t value );
	void	SetParticipantId( mpMatchProtocolParticipantId_t value );
} mpMatchOperationValue_t;

typedef struct mpMatchOperationArgument_s {
	unsigned char			fieldId;
	mpMatchOperationValue_t	value;

	void	Clear( void );
} mpMatchOperationArgument_t;

typedef struct mpMatchArgumentDescriptor_s {
	unsigned char		fieldId;
	mpMatchValueType_t	type;
	bool				required;
	int				minimumValue;
	int				maximumValue;
	unsigned short		minimumStringBytes;
	unsigned short		maximumStringBytes;
	unsigned int		flags;
} mpMatchArgumentDescriptor_t;

typedef struct mpMatchOperationDescriptor_s {
	mpMatchOperationOpcode_t	opcode;
	const char *				token;
	mpMatchLocalizationId_t	labelLocalizationId;
	mpMatchLocalizationId_t	confirmationLocalizationId;
	mpMatchProtocolCapabilityMask_t requiredCapability;
	mpMatchPhaseMask_t		legalPhaseMask;
	unsigned int			flags;
	mpMatchCooldownClass_t	cooldownClass;
	const mpMatchArgumentDescriptor_t *arguments;
	unsigned char			argumentCount;
} mpMatchOperationDescriptor_t;

typedef struct mpMatchOperationRequest_s {
	unsigned short			schemaVersion;
	mpMatchProtocolSessionId_t sessionId;
	unsigned int			requestId;
	mpMatchOperationOpcode_t	opcode;
	mpMatchProtocolRevision_t expectedSessionRevision;
	mpMatchProtocolRevision_t expectedControlRevision;
	unsigned char			actorSlot;
	unsigned int			actorBindingGeneration;
	bool					hasParticipantTarget;
	mpMatchProtocolParticipantId_t participantTarget;
	bool					hasTeamTarget;
	mpMatchTeam_t			teamTarget;
	unsigned char			argumentCount;
	mpMatchOperationArgument_t arguments[ MP_MATCH_PROTOCOL_MAX_ARGUMENTS ];

	void	Clear( void );
} mpMatchOperationRequest_t;

typedef struct mpMatchOperationResult_s {
	unsigned short			schemaVersion;
	mpMatchProtocolSessionId_t sessionId;
	unsigned int			requestId;
	mpMatchOperationOpcode_t	opcode;
	mpMatchOperationResultStatus_t status;
	mpMatchProtocolReason_t	reason;
	mpMatchProtocolRevision_t resultingSessionRevision;
	mpMatchLocalizationId_t	localizationId;
	unsigned char			parameterCount;
	mpMatchOperationArgument_t parameters[ MP_MATCH_PROTOCOL_MAX_RESULT_PARAMETERS ];

	void	Clear( void );
} mpMatchOperationResult_t;

typedef struct mpMatchProtocolEnvelope_s {
	unsigned short			schemaVersion;
	mpMatchEnvelopeKind_t	kind;
	mpMatchProtocolSessionId_t sessionId;
	unsigned short			payloadBytes;

	void	Clear( void );
} mpMatchProtocolEnvelope_t;

typedef struct mpMatchProtocolError_s {
	mpMatchProtocolReason_t	reason;
	unsigned char			fieldId;
	unsigned int			detail;

	void	Clear( void );
} mpMatchProtocolError_t;

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor( mpMatchOperationOpcode_t opcode );
int MPMatchOperationDescriptorCount( void );
bool MPMatchProtocolValidateRegistry( mpMatchProtocolError_t *error = 0 );

bool MPMatchProtocolValidateRequest( const mpMatchOperationRequest_t &request, mpMatchProtocolError_t *error = 0 );
bool MPMatchProtocolValidateResult( const mpMatchOperationResult_t &result, mpMatchProtocolError_t *error = 0 );

bool MPMatchProtocolEncodeRequest( idBitMsg &message, const mpMatchOperationRequest_t &request, mpMatchProtocolError_t *error = 0 );
bool MPMatchProtocolDecodeRequest( const idBitMsg &message, mpMatchOperationRequest_t &request, mpMatchTrailingDataPolicy_t trailingPolicy = MP_MATCH_TRAILING_REJECT, mpMatchProtocolError_t *error = 0 );
bool MPMatchProtocolEncodeResult( idBitMsg &message, const mpMatchOperationResult_t &result, mpMatchProtocolError_t *error = 0 );
bool MPMatchProtocolDecodeResult( const idBitMsg &message, mpMatchOperationResult_t &result, mpMatchTrailingDataPolicy_t trailingPolicy = MP_MATCH_TRAILING_REJECT, mpMatchProtocolError_t *error = 0 );

// Peeking never advances message state.  It validates only the fixed envelope
// header; the typed decoder remains responsible for payload validation.
bool MPMatchProtocolPeekEnvelope( const idBitMsg &message, mpMatchProtocolEnvelope_t &envelope, mpMatchProtocolError_t *error = 0 );

mpMatchLocalizationId_t MPMatchProtocolReasonLocalizationId( mpMatchProtocolReason_t reason );

#endif // __MP_MATCH_PROTOCOL_H__
