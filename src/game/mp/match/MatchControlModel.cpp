//----------------------------------------------------------------
// MatchControlModel.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_CONTROL_MODEL_STANDALONE_TEST )
#include <string.h>
#else
#include "../../../idlib/precompiled.h"
#pragma hdrstop
#endif

#include "MatchControlModel.h"

static_assert( MP_MATCH_CONTROL_MAX_TEAM_ROWS >=
	MP_MATCH_VIEW_MAX_ROSTER_SUMMARIES + MP_MATCH_VIEW_MAX_PARTICIPANTS +
	MP_MATCH_VIEW_MAX_ROSTER_SEATS + MP_MATCH_VIEW_MAX_INVITATIONS +
	MP_MATCH_VIEW_MAX_QUEUE_ENTRIES,
	"Match Control team-row capacity no longer covers a maximum accepted view" );
static_assert( MP_MATCH_CONTROL_MAX_PROFILE_ROWS >= MP_MATCH_PROFILE_COUNT,
	"Match Control profile-row capacity no longer covers the rule registry" );
static_assert( MP_MATCH_CONTROL_MAX_PROPOSAL_TEMPLATE_ROWS == 7,
	"Match Control proposal recipes require an explicit schema review" );
static_assert( MP_MATCH_PROTOCOL_MAX_ARGUMENTS >= 3,
	"Match Control proposal and veto requests require at least three arguments" );

namespace {

typedef struct mpMatchControlCommandDescriptor_s {
	const char *token;
	mpMatchControlCommand_t command;
	mpMatchOperationOpcode_t opcode;
} mpMatchControlCommandDescriptor_t;

static const mpMatchControlCommandDescriptor_t COMMANDS[] = {
	{ "ready_toggle", MP_MATCH_CONTROL_COMMAND_READY_TOGGLE, MP_MATCH_OP_READY_SET },
	{ "team_ready_toggle", MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE, MP_MATCH_OP_TEAM_READY_SET },
	{ "force_ready", MP_MATCH_CONTROL_COMMAND_FORCE_READY, MP_MATCH_OP_FORCE_READY },
	{ "timeout", MP_MATCH_CONTROL_COMMAND_TIMEOUT, MP_MATCH_OP_TIMEOUT_REQUEST },
	{ "tech_pause", MP_MATCH_CONTROL_COMMAND_TECH_PAUSE, MP_MATCH_OP_TECH_PAUSE_REQUEST },
	{ "resume", MP_MATCH_CONTROL_COMMAND_RESUME, MP_MATCH_OP_RESUME_REQUEST },
	{ "referee_logout", MP_MATCH_CONTROL_COMMAND_REFEREE_LOGOUT, MP_MATCH_OP_REF_LOGOUT },
	{ "forfeit", MP_MATCH_CONTROL_COMMAND_FORFEIT, MP_MATCH_OP_FORFEIT },
	{ "abort", MP_MATCH_CONTROL_COMMAND_ABORT, MP_MATCH_OP_ABORT },
	{ "team_join_marine", MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_MARINE, MP_MATCH_OP_TEAM_JOIN },
	{ "team_join_strogg", MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_STROGG, MP_MATCH_OP_TEAM_JOIN },
	{ "team_spectate", MP_MATCH_CONTROL_COMMAND_TEAM_SPECTATE, MP_MATCH_OP_TEAM_JOIN },
	{ "queue_join", MP_MATCH_CONTROL_COMMAND_QUEUE_JOIN, MP_MATCH_OP_QUEUE_JOIN },
	{ "queue_defer", MP_MATCH_CONTROL_COMMAND_QUEUE_DEFER, MP_MATCH_OP_QUEUE_DEFER },
	{ "queue_leave", MP_MATCH_CONTROL_COMMAND_QUEUE_LEAVE, MP_MATCH_OP_QUEUE_LEAVE },
	{ "roster_leave", MP_MATCH_CONTROL_COMMAND_ROSTER_LEAVE, MP_MATCH_OP_ROSTER_LEAVE },
	{ "roster_accept", MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT, MP_MATCH_OP_ROSTER_ACCEPT },
	{ "roster_invite", MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE, MP_MATCH_OP_ROSTER_INVITE },
	{ "roster_remove", MP_MATCH_CONTROL_COMMAND_ROSTER_REMOVE, MP_MATCH_OP_ROSTER_REMOVE },
	{ "roster_substitute", MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE, MP_MATCH_OP_ROSTER_SUBSTITUTE },
	{ "role_assign", MP_MATCH_CONTROL_COMMAND_ROLE_ASSIGN, MP_MATCH_OP_ROLE_ASSIGN },
	{ "broadcaster_set", MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET, MP_MATCH_OP_BROADCASTER_SET },
	{ "team_lock_toggle", MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE, MP_MATCH_OP_TEAM_LOCK_SET },
	{ "proposal_create", MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE, MP_MATCH_OP_PROPOSAL_CREATE },
	{ "proposal_yes", MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES, MP_MATCH_OP_PROPOSAL_CAST },
	{ "proposal_no", MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO, MP_MATCH_OP_PROPOSAL_CAST },
	{ "proposal_abstain", MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN, MP_MATCH_OP_PROPOSAL_CAST },
	{ "proposal_cancel", MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL, MP_MATCH_OP_PROPOSAL_CANCEL },
	{ "rules_select_profile", MP_MATCH_CONTROL_COMMAND_RULES_SELECT_PROFILE, MP_MATCH_OP_RULES_SELECT_PROFILE },
	{ "rules_stage_field", MP_MATCH_CONTROL_COMMAND_RULES_STAGE_FIELD, MP_MATCH_OP_RULES_STAGE_FIELD },
	{ "rules_commit", MP_MATCH_CONTROL_COMMAND_RULES_COMMIT, MP_MATCH_OP_RULES_COMMIT },
	{ "rules_discard", MP_MATCH_CONTROL_COMMAND_RULES_DISCARD, MP_MATCH_OP_RULES_DISCARD },
	{ "series_stage", MP_MATCH_CONTROL_COMMAND_SERIES_STAGE, MP_MATCH_OP_SERIES_STAGE_PROFILE },
	{ "series_start", MP_MATCH_CONTROL_COMMAND_SERIES_START, MP_MATCH_OP_SERIES_START },
	{ "series_cancel", MP_MATCH_CONTROL_COMMAND_SERIES_CANCEL, MP_MATCH_OP_SERIES_CANCEL },
	{ "series_advance", MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE, MP_MATCH_OP_SERIES_ADVANCE },
	{ "veto_ban", MP_MATCH_CONTROL_COMMAND_VETO_BAN, MP_MATCH_OP_VETO_SELECT },
	{ "veto_pick", MP_MATCH_CONTROL_COMMAND_VETO_PICK, MP_MATCH_OP_VETO_SELECT },
	{ "veto_decider", MP_MATCH_CONTROL_COMMAND_VETO_DECIDER, MP_MATCH_OP_VETO_SELECT },
	{ "veto_side_marine", MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE, MP_MATCH_OP_VETO_SELECT },
	{ "veto_side_strogg", MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG, MP_MATCH_OP_VETO_SELECT },
	{ "participant_remove", MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE,
		MP_MATCH_OP_PARTICIPANT_REMOVE },
	{ "series_contestant_bind", MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND,
		MP_MATCH_OP_SERIES_CONTESTANT_BIND }
};

static_assert( sizeof( COMMANDS ) / sizeof( COMMANDS[ 0 ] ) ==
	MP_MATCH_CONTROL_COMMAND_COUNT - 1,
	"Every fixed Match Control operation token requires one typed command" );

static const char TECH_PAUSE_REASON[] = "match_control_technical_pause";
static const char ABORT_REASON[] = "match_control_abort";

static void ClearError( mpMatchControlError_t *error ) {
	if ( error != NULL ) {
		error->Clear();
	}
}

static void SetError( mpMatchControlError_t *error,
	mpMatchControlErrorReason_t reason,
	mpMatchOperationOpcode_t opcode = MP_MATCH_OP_INVALID,
	int rowIndex = -1,
	unsigned char fieldId = 0,
	unsigned int detail = 0 ) {
	if ( error == NULL ) {
		return;
	}
	error->Clear();
	error->reason = reason;
	error->opcode = opcode;
	error->rowIndex = rowIndex;
	error->fieldId = fieldId;
	error->detail = detail;
}

static bool IsPlayableSide( int side ) {
	return side >= 0 && side < MP_MATCH_VIEW_SIDE_COUNT;
}

static mpMatchTeam_t ProtocolTeamForSide( int side ) {
	if ( side == 0 ) {
		return MP_MATCH_TEAM_MARINE;
	}
	if ( side == 1 ) {
		return MP_MATCH_TEAM_STROGG;
	}
	return MP_MATCH_TEAM_NONE;
}

static bool IsMachineKeyCharacter( unsigned char character ) {
	return ( character >= 'a' && character <= 'z' ) ||
		( character >= '0' && character <= '9' ) || character == '_';
}

static bool CopyMachineKey( char destination[ MP_MATCH_CONTROL_KEY_BYTES + 1 ],
	unsigned char &destinationLength, const char *source ) {
	if ( source == NULL ) {
		return false;
	}
	int length = 0;
	for ( ; length <= MP_MATCH_CONTROL_KEY_BYTES; ++length ) {
		const unsigned char character = static_cast<unsigned char>( source[ length ] );
		if ( character == '\0' ) {
			if ( length == 0 ) {
				return false;
			}
			memcpy( destination, source, static_cast<size_t>( length ) );
			destination[ length ] = '\0';
			destinationLength = static_cast<unsigned char>( length );
			return true;
		}
		if ( length == MP_MATCH_CONTROL_KEY_BYTES || !IsMachineKeyCharacter( character ) ) {
			return false;
		}
	}
	return false;
}

static mpMatchViewRuleType_t ViewRuleType( mpRuleFieldType_t type ) {
	switch ( type ) {
		case MP_RULE_TYPE_BOOL:
			return MP_MATCH_VIEW_RULE_BOOL;
		case MP_RULE_TYPE_INTEGER:
			return MP_MATCH_VIEW_RULE_INTEGER;
		case MP_RULE_TYPE_ENUM:
			return MP_MATCH_VIEW_RULE_ENUM;
		default:
			return MP_MATCH_VIEW_RULE_TYPE_COUNT;
	}
}

static bool RuleValueAllowed( const mpRuleFieldDescriptor_t &descriptor,
	int value ) {
	if ( value < descriptor.minimumValue || value > descriptor.maximumValue ) {
		return false;
	}
	if ( descriptor.type == MP_RULE_TYPE_BOOL ) {
		return value == 0 || value == 1;
	}
	if ( descriptor.type != MP_RULE_TYPE_ENUM ) {
		return descriptor.type == MP_RULE_TYPE_INTEGER;
	}
	for ( int index = 0; index < descriptor.numEnumValues; ++index ) {
		if ( descriptor.enumValues[ index ].value == value ) {
			return true;
		}
	}
	return false;
}

static const mpMatchViewParticipantSummary_t *FindParticipant(
	const mpSessionView &view, mpMatchProtocolParticipantId_t participantId ) {
	for ( int index = 0; index < view.publicState.participantSummaryCount; ++index ) {
		const mpMatchViewParticipantSummary_t &participant =
			view.publicState.participantSummaries[ index ];
		if ( participant.participantId == participantId ) {
			return &participant;
		}
	}
	return NULL;
}

static const mpMatchViewRosterSeat_t *FindRosterSeat(
	const mpSessionView &view,
	mpMatchProtocolParticipantId_t participantId ) {
	for ( int index = 0; index < view.rosterSeatCount; ++index ) {
		const mpMatchViewRosterSeat_t &seat = view.rosterSeats[ index ];
		if ( seat.occupied && seat.participantId == participantId ) {
			return &seat;
		}
	}
	return NULL;
}

static const mpMatchViewStagedRuleValue_t *FindStagedRule(
	const mpMatchViewStagedRules_t &staged, unsigned char fieldId ) {
	if ( !staged.present ) {
		return NULL;
	}
	for ( int index = 0; index < staged.valueCount; ++index ) {
		if ( staged.values[ index ].fieldId == fieldId ) {
			return &staged.values[ index ];
		}
	}
	return NULL;
}

static bool TeamRowsMatch( const mpMatchControlTeamRow_t &left,
	const mpMatchControlTeamRow_t &right ) {
	if ( left.kind != right.kind || left.side != right.side ) {
		return false;
	}
	switch ( left.kind ) {
		case MP_MATCH_CONTROL_TEAM_ROW_SIDE:
			return true;
		case MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT:
		case MP_MATCH_CONTROL_TEAM_ROW_QUEUE_ENTRY:
			return left.participantId == right.participantId;
		case MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT:
			return left.seatIndex == right.seatIndex &&
				left.participantId == right.participantId;
		case MP_MATCH_CONTROL_TEAM_ROW_INVITATION:
			return left.invitationId == right.invitationId;
		default:
			return false;
	}
}

static bool AddArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, mpMatchOperationArgument_t *&argument ) {
	if ( request.argumentCount >= MP_MATCH_PROTOCOL_MAX_ARGUMENTS ||
		fieldId == MP_MATCH_ARG_INVALID ) {
		return false;
	}
	argument = &request.arguments[ request.argumentCount++ ];
	argument->Clear();
	argument->fieldId = fieldId;
	return true;
}

static bool AddBoolArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, bool value ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	argument->value.SetBool( value );
	return true;
}

static bool AddUIntArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, unsigned int value ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	argument->value.SetUInt32( value );
	return true;
}

static bool AddEnumArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, unsigned short value ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	argument->value.SetEnum( value );
	return true;
}

static bool AddParticipantArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, mpMatchProtocolParticipantId_t value ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	argument->value.SetParticipantId( value );
	return true;
}

static bool AddOpcodeArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, mpMatchOperationOpcode_t value ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	argument->value.SetOpcode( value );
	return true;
}

static bool AddStringArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, const char *value, int length = -1 ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	return argument->value.SetString( value, length );
}

static bool AddRuleValueArgument( mpMatchOperationRequest_t &request,
	unsigned char fieldId, const mpMatchControlRuleRow_t &row ) {
	mpMatchOperationArgument_t *argument = NULL;
	if ( !row.editValueValid || !AddArgument( request, fieldId, argument ) ) {
		return false;
	}
	switch ( row.type ) {
		case MP_MATCH_VIEW_RULE_BOOL:
			argument->value.SetBool( row.editValue != 0 );
			return true;
		case MP_MATCH_VIEW_RULE_INTEGER:
			argument->value.SetInt32( row.editValue );
			return true;
		case MP_MATCH_VIEW_RULE_ENUM:
			if ( row.editValue < 0 || row.editValue > 65535 ) {
				return false;
			}
			argument->value.SetEnum( static_cast<unsigned short>( row.editValue ) );
			return true;
		default:
			return false;
	}
}

static unsigned char NestedField( unsigned char fieldId ) {
	return static_cast<unsigned char>( MP_MATCH_NESTED_ARGUMENT_BASE + fieldId );
}

} // namespace

void mpMatchControlError_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	reason = MP_MATCH_CONTROL_ERROR_NONE;
	opcode = MP_MATCH_OP_INVALID;
	protocolReason = MP_MATCH_PROTOCOL_REASON_NONE;
	viewReason = MP_MATCH_VIEW_ERROR_NONE;
	rowIndex = -1;
}

bool MPMatchControlCommandFromToken( const char *token,
	mpMatchControlCommand_t &command ) {
	command = MP_MATCH_CONTROL_COMMAND_INVALID;
	if ( token == NULL ) {
		return false;
	}
	for ( int index = 0; index < static_cast<int>( sizeof( COMMANDS ) /
			sizeof( COMMANDS[ 0 ] ) ); ++index ) {
		if ( strcmp( token, COMMANDS[ index ].token ) == 0 ) {
			command = COMMANDS[ index ].command;
			return true;
		}
	}
	return false;
}

const char *MPMatchControlCommandToken( mpMatchControlCommand_t command ) {
	for ( int index = 0; index < static_cast<int>( sizeof( COMMANDS ) /
			sizeof( COMMANDS[ 0 ] ) ); ++index ) {
		if ( COMMANDS[ index ].command == command ) {
			return COMMANDS[ index ].token;
		}
	}
	return NULL;
}

mpMatchOperationOpcode_t MPMatchControlCommandOpcode(
	mpMatchControlCommand_t command ) {
	for ( int index = 0; index < static_cast<int>( sizeof( COMMANDS ) /
			sizeof( COMMANDS[ 0 ] ) ); ++index ) {
		if ( COMMANDS[ index ].command == command ) {
			return COMMANDS[ index ].opcode;
		}
	}
	return MP_MATCH_OP_INVALID;
}

mpMatchControlModel::mpMatchControlModel() {
	Clear();
}

void mpMatchControlModel::Clear( void ) {
	ready = false;
	sessionId = 0;
	sessionRevision = 0;
	controlRevision = 0;
	viewRevision = 0;
	phase = INACTIVE;
	recipient.Clear();
	globalProposal.Clear();
	ownSideProposal.Clear();
	series.Clear();
	memset( teamKnown, 0, sizeof( teamKnown ) );
	memset( teamReady, 0, sizeof( teamReady ) );
	memset( teamLocked, 0, sizeof( teamLocked ) );
	for ( int index = 0; index < MP_MATCH_OP_COUNT; ++index ) {
		availability[ index ].Clear();
	}
	memset( teamRows, 0, sizeof( teamRows ) );
	teamRowCount = 0;
	memset( replacementRows, 0, sizeof( replacementRows ) );
	replacementRowCount = 0;
	memset( proposalTemplateRows, 0, sizeof( proposalTemplateRows ) );
	proposalTemplateRowCount = 0;
	memset( profileRows, 0, sizeof( profileRows ) );
	profileRowCount = 0;
	memset( ruleRows, 0, sizeof( ruleRows ) );
	ruleRowCount = 0;
	memset( seriesMapRows, 0, sizeof( seriesMapRows ) );
	seriesMapRowCount = 0;
	memset( seriesHistoryRows, 0, sizeof( seriesHistoryRows ) );
	seriesHistoryRowCount = 0;
	memset( evidenceRows, 0, sizeof( evidenceRows ) );
	evidenceRowCount = 0;
	selectedTeamRow = -1;
	selectedReplacementRow = -1;
	selectedProposalTemplateRow = -1;
	selectedProfileRow = -1;
	selectedRuleRow = -1;
	selectedSeriesMapRow = -1;
	roleChoice = MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER;
	proposalChoice = MP_MATCH_CONTROL_PROPOSAL_GLOBAL;
	actionSideChoice = MP_MATCH_CONTROL_SIDE_CHOICE_NONE;
	actionSideChoiceExplicit = false;
	seriesProfileChoice = MP_SERIES_PROFILE_BEST_OF_ONE;
}

mpMatchControlIngestResult_t mpMatchControlModel::IngestAcceptedView(
	const mpSessionView &view, mpMatchControlError_t *error ) {
	ClearError( error );
	mpMatchViewError_t viewError;
	viewError.Clear();
	if ( !MPMatchViewValidate( view, &viewError ) ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW );
		if ( error != NULL ) {
			error->viewReason = viewError.reason;
			error->fieldId = viewError.fieldId;
			error->detail = viewError.detail;
		}
		return MP_MATCH_CONTROL_INGEST_REJECTED;
	}

	const bool sameSession = ready && sessionId == view.publicState.sessionId;
	const mpMatchViewRecipient_t &incomingRecipient = view.publicState.recipient;
	const bool sameBinding = sameSession &&
		recipient.participantId == incomingRecipient.participantId &&
		recipient.slot == incomingRecipient.slot &&
		recipient.bindingGeneration == incomingRecipient.bindingGeneration;
	if ( sameBinding && view.publicState.viewRevision < viewRevision ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_STALE_VIEW );
		return MP_MATCH_CONTROL_INGEST_REJECTED;
	}
	if ( sameBinding && view.publicState.viewRevision == viewRevision ) {
		return MP_MATCH_CONTROL_INGEST_NO_CHANGE;
	}

	mpMatchControlModel candidate;
	candidate.sessionId = view.publicState.sessionId;
	candidate.sessionRevision = view.publicState.sessionRevision;
	candidate.controlRevision = view.publicState.controlRevision;
	candidate.viewRevision = view.publicState.viewRevision;
	candidate.phase = view.publicState.lifecycle.phase;
	candidate.recipient = view.publicState.recipient;
	const int defaultActionSide = candidate.DefaultActionSide();
	if ( IsPlayableSide( defaultActionSide ) ) {
		candidate.actionSideChoice = static_cast<mpMatchControlSideChoice_t>(
			defaultActionSide );
	}
	candidate.globalProposal = view.publicState.globalProposal;
	candidate.ownSideProposal = view.ownSideProposal;
	candidate.series = view.publicState.series;
	for ( int index = 0; index < view.publicState.operationAvailabilityCount; ++index ) {
		const mpMatchViewOperationAvailability_t &entry =
			view.publicState.operationAvailability[ index ];
		const int opcode = static_cast<int>( entry.opcode );
		if ( opcode <= MP_MATCH_OP_INVALID || opcode >= MP_MATCH_OP_COUNT ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW, entry.opcode,
				index );
			return MP_MATCH_CONTROL_INGEST_REJECTED;
		}
		candidate.availability[ opcode ] = entry;
	}
	if ( !candidate.BuildRows( view, error ) ) {
		return MP_MATCH_CONTROL_INGEST_REJECTED;
	}
	if ( sameBinding ) {
		candidate.RestoreSelectionsFrom( *this );
	}
	candidate.ready = true;
	*this = candidate;
	return sameSession ? MP_MATCH_CONTROL_INGEST_UPDATED :
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION;
}

bool mpMatchControlModel::BuildRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	return BuildTeamRows( view, error ) &&
		BuildReplacementRows( view, error ) &&
		BuildProfileRows( view, error ) &&
		BuildRuleRows( view, error ) &&
		BuildProposalTemplateRows( error ) &&
		BuildSeriesRows( view, error ) &&
		BuildEvidenceRows( view, error );
}

bool mpMatchControlModel::BuildTeamRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	for ( int index = 0; index < view.publicState.rosterSummaryCount; ++index ) {
		const mpMatchViewRosterSummary_t &summary =
			view.publicState.rosterSummaries[ index ];
		if ( !IsPlayableSide( summary.side ) ||
			teamRowCount >= MP_MATCH_CONTROL_MAX_TEAM_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		teamKnown[ summary.side ] = true;
		teamReady[ summary.side ] = summary.teamReady;
		teamLocked[ summary.side ] = summary.locked;
		mpMatchControlTeamRow_t &row = teamRows[ teamRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_TEAM_ROW_SIDE;
		row.side = summary.side;
		row.seatIndex = 0xffu;
		row.rosterRole = MP_MATCH_VIEW_ROSTER_ROLE_COUNT;
		row.teamReady = summary.teamReady;
		row.teamLocked = summary.locked;
	}

	for ( int index = 0; index < view.publicState.participantSummaryCount; ++index ) {
		if ( teamRowCount >= MP_MATCH_CONTROL_MAX_TEAM_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		const mpMatchViewParticipantSummary_t &participant =
			view.publicState.participantSummaries[ index ];
		mpMatchControlTeamRow_t &row = teamRows[ teamRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT;
		row.side = participant.side;
		row.participantId = participant.participantId;
		row.seatIndex = 0xffu;
		row.rosterRole = MP_MATCH_VIEW_ROSTER_ROLE_COUNT;
		row.connected = participant.connected;
		row.human = participant.human;
		row.active = participant.active;
		row.publicRoleMask = participant.publicRoleMask;
		if ( participant.participantId == recipient.participantId ) {
			selectedTeamRow = teamRowCount - 1;
		}
	}

	for ( int index = 0; index < view.rosterSeatCount; ++index ) {
		if ( teamRowCount >= MP_MATCH_CONTROL_MAX_TEAM_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		const mpMatchViewRosterSeat_t &seat = view.rosterSeats[ index ];
		const mpMatchViewParticipantSummary_t *participant = seat.occupied ?
			FindParticipant( view, seat.participantId ) : NULL;
		if ( seat.occupied && ( participant == NULL ||
			participant->side != seat.side ||
			participant->connected != seat.connected ||
			participant->active != seat.active ) ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		mpMatchControlTeamRow_t &row = teamRows[ teamRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT;
		row.side = seat.side;
		row.participantId = seat.participantId;
		row.seatIndex = seat.seatIndex;
		row.rosterRole = seat.role;
		row.occupied = seat.occupied;
		row.connected = seat.connected;
		row.human = participant != NULL && participant->human;
		row.active = seat.active;
		row.publicRoleMask = participant != NULL ?
			participant->publicRoleMask : 0;
	}

	for ( int index = 0; index < view.invitationCount; ++index ) {
		if ( teamRowCount >= MP_MATCH_CONTROL_MAX_TEAM_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		const mpMatchViewInvitationSummary_t &invitation = view.invitations[ index ];
		mpMatchControlTeamRow_t &row = teamRows[ teamRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_TEAM_ROW_INVITATION;
		row.side = invitation.side;
		row.participantId = invitation.inviteeParticipantId;
		row.seatIndex = 0xffu;
		row.rosterRole = invitation.role;
		row.invitationId = invitation.invitationId;
	}

	for ( int index = 0; index < view.queueEntryCount; ++index ) {
		if ( teamRowCount >= MP_MATCH_CONTROL_MAX_TEAM_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		const mpMatchViewQueueEntry_t &entry = view.queueEntries[ index ];
		const mpMatchViewParticipantSummary_t *participant =
			FindParticipant( view, entry.participantId );
		mpMatchControlTeamRow_t &row = teamRows[ teamRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_TEAM_ROW_QUEUE_ENTRY;
		row.side = entry.side;
		row.participantId = entry.participantId;
		row.seatIndex = 0xffu;
		row.rosterRole = MP_MATCH_VIEW_ROSTER_ROLE_COUNT;
		row.queueState = entry.state;
		row.queuePosition = entry.position;
		row.connected = participant != NULL && participant->connected;
		row.human = participant != NULL && participant->human;
		row.active = participant != NULL && participant->active;
		row.publicRoleMask = participant != NULL ?
			participant->publicRoleMask : 0;
	}
	if ( selectedTeamRow < 0 && teamRowCount > 0 ) {
		selectedTeamRow = 0;
	}
	return true;
}

bool mpMatchControlModel::BuildReplacementRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	for ( int index = 0; index < view.publicState.participantSummaryCount; ++index ) {
		const mpMatchViewParticipantSummary_t &participant =
			view.publicState.participantSummaries[ index ];
		if ( !participant.connected || !participant.human ||
			participant.slot == 0xffu ) {
			continue;
		}
		const mpMatchViewRosterSeat_t *seat = FindRosterSeat( view,
			participant.participantId );
		const bool persistentBench = seat != NULL &&
			seat->role == MP_MATCH_VIEW_ROSTER_SUBSTITUTE &&
			seat->side == participant.side && seat->connected &&
			!seat->active && !participant.active;
		if ( seat != NULL && !persistentBench ) {
			continue;
		}
		if ( replacementRowCount >= MP_MATCH_CONTROL_MAX_REPLACEMENT_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_ROSTER_SUBSTITUTE, index );
			return false;
		}
		mpMatchControlReplacementRow_t &row =
			replacementRows[ replacementRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.participantId = participant.participantId;
		row.slot = participant.slot;
		row.side = participant.side;
		row.connected = participant.connected;
		row.human = participant.human;
		row.active = participant.active;
		row.rostered = seat != NULL;
		row.rosterSeatIndex = seat != NULL ? seat->seatIndex : 0xffu;
		row.rosterRole = seat != NULL ? seat->role :
			MP_MATCH_VIEW_ROSTER_ROLE_COUNT;
	}
	if ( replacementRowCount > 0 ) {
		selectedReplacementRow = 0;
	}
	return true;
}

bool mpMatchControlModel::BuildProfileRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	int gameType = -1;
	if ( view.publicState.committedRules.present ) {
		for ( int index = 0; index < view.publicState.committedRules.valueCount; ++index ) {
			const mpMatchViewRuleValue_t &value =
				view.publicState.committedRules.values[ index ];
			if ( value.fieldId == MP_RULE_GAME_TYPE ) {
				gameType = value.value;
				break;
			}
		}
	}
	if ( gameType < 0 || gameType >= 32 ) {
		return true;
	}
	const int profileCount = MPMatchProfileCount();
	if ( profileCount < 0 || profileCount > MP_MATCH_CONTROL_MAX_PROFILE_ROWS ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
			MP_MATCH_OP_RULES_SELECT_PROFILE, profileCount );
		return false;
	}
	const unsigned int gameBit = 1u << static_cast<unsigned int>( gameType );
	for ( int index = 0; index < profileCount; ++index ) {
		const mpMatchProfileDescriptor_t *profile = MPMatchProfile( index );
		if ( profile == NULL || ( profile->applicableGameTypes & gameBit ) == 0 ) {
			continue;
		}
		if ( profileRowCount >= MP_MATCH_CONTROL_MAX_PROFILE_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_RULES_SELECT_PROFILE, index );
			return false;
		}
		mpMatchControlProfileRow_t &row = profileRows[ profileRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.profileId = profile->id;
		if ( !CopyMachineKey( row.key, row.keyLength, profile->key ) ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
				MP_MATCH_OP_RULES_SELECT_PROFILE, index, MP_MATCH_ARG_PROFILE );
			return false;
		}
		if ( static_cast<int>( profile->id ) ==
			view.publicState.committedRules.profileId ) {
			selectedProfileRow = profileRowCount - 1;
		}
	}
	if ( selectedProfileRow < 0 && profileRowCount > 0 ) {
		selectedProfileRow = 0;
	}
	return true;
}

bool mpMatchControlModel::BuildRuleRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	if ( !view.publicState.committedRules.present ) {
		return true;
	}
	for ( int index = 0; index < view.publicState.committedRules.valueCount; ++index ) {
		if ( ruleRowCount >= MP_MATCH_CONTROL_MAX_RULE_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_RULES_STAGE_FIELD, index );
			return false;
		}
		const mpMatchViewRuleValue_t &value =
			view.publicState.committedRules.values[ index ];
		const mpRuleFieldDescriptor_t *descriptor = MPMatchRuleField( value.fieldId );
		if ( descriptor == NULL || static_cast<int>( descriptor->id ) != value.fieldId ||
			ViewRuleType( descriptor->type ) != value.type ||
			!RuleValueAllowed( *descriptor, value.value ) ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW,
				MP_MATCH_OP_RULES_STAGE_FIELD, index, value.fieldId );
			return false;
		}
		mpMatchControlRuleRow_t &row = ruleRows[ ruleRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.fieldId = value.fieldId;
		row.type = value.type;
		row.minimumValue = descriptor->minimumValue;
		row.maximumValue = descriptor->maximumValue;
		row.committedValue = value.value;
		row.editable = value.editable;
		if ( !CopyMachineKey( row.key, row.keyLength, descriptor->key ) ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
				MP_MATCH_OP_RULES_STAGE_FIELD, index, value.fieldId );
			return false;
		}
		const mpMatchViewStagedRuleValue_t *staged =
			FindStagedRule( view.stagedRules, value.fieldId );
		if ( staged != NULL ) {
			if ( staged->type != value.type ||
				!RuleValueAllowed( *descriptor, staged->value ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW,
					MP_MATCH_OP_RULES_STAGE_FIELD, index, value.fieldId );
				return false;
			}
			row.hasStagedValue = true;
			row.stagedValue = staged->value;
		}
		row.editValue = row.hasStagedValue ? row.stagedValue : row.committedValue;
		row.editValueValid = row.editable &&
			RuleValueAllowed( *descriptor, row.editValue );
		if ( selectedRuleRow < 0 && row.editable ) {
			selectedRuleRow = ruleRowCount - 1;
		}
	}
	return true;
}

bool mpMatchControlModel::BuildProposalTemplateRows(
	mpMatchControlError_t *error ) {
	static const mpMatchOperationOpcode_t templateOpcodes[] = {
		MP_MATCH_OP_RESUME_REQUEST,
		MP_MATCH_OP_RULES_SELECT_PROFILE,
		MP_MATCH_OP_RULES_STAGE_FIELD,
		MP_MATCH_OP_RULES_COMMIT,
		MP_MATCH_OP_ABORT,
		MP_MATCH_OP_PARTICIPANT_REMOVE,
		MP_MATCH_OP_FORFEIT
	};
	for ( int index = 0; index < static_cast<int>( sizeof( templateOpcodes ) /
			sizeof( templateOpcodes[ 0 ] ) ); ++index ) {
		const mpMatchOperationDescriptor_t *descriptor =
			MPMatchOperationDescriptor( templateOpcodes[ index ] );
		if ( descriptor == NULL ||
			( descriptor->flags & MP_MATCH_OPERATION_FLAG_PROPOSABLE ) == 0 ||
			proposalTemplateRowCount >=
				MP_MATCH_CONTROL_MAX_PROPOSAL_TEMPLATE_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
				MP_MATCH_OP_PROPOSAL_CREATE, index );
			return false;
		}
		mpMatchControlProposalTemplateRow_t &row =
			proposalTemplateRows[ proposalTemplateRowCount++ ];
		row.opcode = templateOpcodes[ index ];
		const unsigned int targetFlags =
			MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
			MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET;
		row.globalOnly = ( descriptor->flags & targetFlags ) == 0;
		if ( templateOpcodes[ index ] == MP_MATCH_OP_FORFEIT ) {
			// The one explicit side-ballot recipe: the required team target is
			// the recipient's own side, which the server also uses as the scope.
			if ( ( descriptor->flags & targetFlags ) != targetFlags ||
				( descriptor->flags &
					MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET ) != 0 ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
					MP_MATCH_OP_PROPOSAL_CREATE, index );
				return false;
			}
			row.targetKind = MP_MATCH_CONTROL_PROPOSAL_TARGET_OWN_SIDE;
		} else if ( ( descriptor->flags &
				MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET ) != 0 ) {
			row.targetKind = MP_MATCH_CONTROL_PROPOSAL_TARGET_PARTICIPANT;
		} else if ( ( descriptor->flags &
				MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET ) == 0 ) {
			row.targetKind = MP_MATCH_CONTROL_PROPOSAL_TARGET_NONE;
		} else {
			// An optional target would require an explicit template recipe.  Do not
			// silently infer one from whichever row happens to be selected.
			SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
				MP_MATCH_OP_PROPOSAL_CREATE, index );
			return false;
		}
	}
	if ( proposalTemplateRowCount > 0 ) {
		selectedProposalTemplateRow = 0;
	}
	return true;
}

bool mpMatchControlModel::BuildSeriesRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	if ( !view.publicState.series.present ) {
		return true;
	}
	const mpMatchViewSeriesSummary_t &source = view.publicState.series;
	for ( int index = 0; index < source.mapPoolCount; ++index ) {
		if ( seriesMapRowCount >= MP_MATCH_CONTROL_MAX_SERIES_MAP_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_VETO_SELECT, index );
			return false;
		}
		seriesMapRows[ seriesMapRowCount++ ].map = source.mapPool[ index ];
	}
	for ( int index = 0; index < source.vetoHistoryCount; ++index ) {
		if ( seriesHistoryRowCount >= MP_MATCH_CONTROL_MAX_SERIES_HISTORY_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_VETO_SELECT, index );
			return false;
		}
		mpMatchControlSeriesHistoryRow_t &row =
			seriesHistoryRows[ seriesHistoryRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_HISTORY_VETO;
		row.veto = source.vetoHistory[ index ];
	}
	for ( int index = 0; index < source.mapHistoryCount; ++index ) {
		if ( seriesHistoryRowCount >= MP_MATCH_CONTROL_MAX_SERIES_HISTORY_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_SERIES_ADVANCE, index );
			return false;
		}
		mpMatchControlSeriesHistoryRow_t &row =
			seriesHistoryRows[ seriesHistoryRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_HISTORY_MAP;
		row.map = source.mapHistory[ index ];
	}
	if ( seriesMapRowCount > 0 ) {
		selectedSeriesMapRow = 0;
	}
	return true;
}

bool mpMatchControlModel::BuildEvidenceRows( const mpSessionView &view,
	mpMatchControlError_t *error ) {
	if ( evidenceRowCount >= MP_MATCH_CONTROL_MAX_EVIDENCE_ROWS ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY );
		return false;
	}
	mpMatchControlEvidenceRow_t &summary = evidenceRows[ evidenceRowCount++ ];
	memset( &summary, 0, sizeof( summary ) );
	summary.kind = MP_MATCH_CONTROL_EVIDENCE_SUMMARY;
	summary.summary = view.publicState.evidence;
	for ( int index = 0; index < view.publicState.evidence.recentEventCount; ++index ) {
		if ( evidenceRowCount >= MP_MATCH_CONTROL_MAX_EVIDENCE_ROWS ) {
			SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY,
				MP_MATCH_OP_INVALID, index );
			return false;
		}
		mpMatchControlEvidenceRow_t &row = evidenceRows[ evidenceRowCount++ ];
		memset( &row, 0, sizeof( row ) );
		row.kind = MP_MATCH_CONTROL_EVIDENCE_RECENT_EVENT;
		row.recentEventIndex = static_cast<unsigned char>( index );
		row.recentEventKind = view.publicState.evidence.recentEventKinds[ index ];
	}
	return true;
}

void mpMatchControlModel::RestoreSelectionsFrom(
	const mpMatchControlModel &previous ) {
	if ( previous.roleChoice >= MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER &&
		previous.roleChoice <= MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE ) {
		roleChoice = previous.roleChoice;
	}
	if ( previous.proposalChoice >= MP_MATCH_CONTROL_PROPOSAL_GLOBAL &&
		previous.proposalChoice < MP_MATCH_CONTROL_PROPOSAL_CHOICE_COUNT ) {
		proposalChoice = previous.proposalChoice;
	}
	// An implicit own-side default follows the recipient when game-side
	// assignments change between maps.  A genuinely explicit choice remains
	// stable until changed or rebound, even when it happens to equal the
	// principal's old default.
	if ( previous.actionSideChoiceExplicit &&
		previous.actionSideChoice >= MP_MATCH_CONTROL_SIDE_CHOICE_ZERO &&
		previous.actionSideChoice < MP_MATCH_CONTROL_SIDE_CHOICE_COUNT &&
		IsPlayableSide( static_cast<int>( previous.actionSideChoice ) ) ) {
		actionSideChoice = previous.actionSideChoice;
		actionSideChoiceExplicit = true;
	}
	if ( previous.seriesProfileChoice >= MP_SERIES_PROFILE_BEST_OF_ONE &&
		previous.seriesProfileChoice < MP_SERIES_PROFILE_COUNT ) {
		seriesProfileChoice = previous.seriesProfileChoice;
	}

	const mpMatchControlTeamRow_t *oldTeam = previous.TeamRow(
		previous.selectedTeamRow );
	if ( oldTeam != NULL ) {
		for ( int index = 0; index < teamRowCount; ++index ) {
			if ( TeamRowsMatch( *oldTeam, teamRows[ index ] ) ) {
				selectedTeamRow = index;
				break;
			}
		}
	}
	const mpMatchControlReplacementRow_t *oldReplacement =
		previous.ReplacementRow( previous.selectedReplacementRow );
	if ( oldReplacement != NULL ) {
		for ( int index = 0; index < replacementRowCount; ++index ) {
			if ( replacementRows[ index ].participantId ==
				oldReplacement->participantId ) {
				selectedReplacementRow = index;
				break;
			}
		}
	}
	const mpMatchControlProposalTemplateRow_t *oldTemplate =
		previous.ProposalTemplateRow( previous.selectedProposalTemplateRow );
	if ( oldTemplate != NULL ) {
		for ( int index = 0; index < proposalTemplateRowCount; ++index ) {
			if ( proposalTemplateRows[ index ].opcode == oldTemplate->opcode ) {
				selectedProposalTemplateRow = index;
				break;
			}
		}
	}
	const mpMatchControlProfileRow_t *oldProfile =
		previous.ProfileRow( previous.selectedProfileRow );
	if ( oldProfile != NULL ) {
		for ( int index = 0; index < profileRowCount; ++index ) {
			if ( profileRows[ index ].profileId == oldProfile->profileId ) {
				selectedProfileRow = index;
				break;
			}
		}
	}
	const mpMatchControlRuleRow_t *oldRule = previous.RuleRow(
		previous.selectedRuleRow );
	if ( oldRule != NULL ) {
		for ( int index = 0; index < ruleRowCount; ++index ) {
			mpMatchControlRuleRow_t &row = ruleRows[ index ];
			if ( row.fieldId != oldRule->fieldId ) {
				continue;
			}
			selectedRuleRow = index;
			const mpRuleFieldDescriptor_t *descriptor = MPMatchRuleField( row.fieldId );
			if ( row.editable && oldRule->editValueValid && descriptor != NULL &&
				RuleValueAllowed( *descriptor, oldRule->editValue ) ) {
				row.editValue = oldRule->editValue;
				row.editValueValid = true;
			}
			break;
		}
	}
	const mpMatchControlSeriesMapRow_t *oldMap = previous.SeriesMapRow(
		previous.selectedSeriesMapRow );
	if ( oldMap != NULL ) {
		for ( int index = 0; index < seriesMapRowCount; ++index ) {
			const mpMatchViewSeriesMap_t &candidate = seriesMapRows[ index ].map;
			if ( candidate.poolIndex == oldMap->map.poolIndex &&
				candidate.tokenLength == oldMap->map.tokenLength &&
				memcmp( candidate.mapToken, oldMap->map.mapToken,
					candidate.tokenLength ) == 0 ) {
				selectedSeriesMapRow = index;
				break;
			}
		}
	}
}

const mpMatchViewOperationAvailability_t *
mpMatchControlModel::OperationAvailability(
	mpMatchOperationOpcode_t opcode ) const {
	const int index = static_cast<int>( opcode );
	if ( !ready || index <= MP_MATCH_OP_INVALID || index >= MP_MATCH_OP_COUNT ||
		availability[ index ].opcode != opcode ) {
		return NULL;
	}
	return &availability[ index ];
}

const mpMatchViewOperationAvailability_t *
mpMatchControlModel::CommandAvailability(
	mpMatchControlCommand_t command ) const {
	return OperationAvailability( MPMatchControlCommandOpcode( command ) );
}

bool mpMatchControlModel::OperationContextAccepted(
		mpMatchOperationOpcode_t opcode ) const {
	if ( !ready || opcode <= MP_MATCH_OP_INVALID || opcode >= MP_MATCH_OP_COUNT ) {
		return false;
	}

	switch ( opcode ) {
		case MP_MATCH_OP_SERIES_ADVANCE: {
			const mpMatchViewOperationAvailability_t *advance =
				OperationAvailability( opcode );
			// A server-restored completed map opens in warmup. The accepted
			// recipient decision is the authority for continuing that review;
			// the client cannot infer recovery from the public series alone.
			const bool recoveredReview = phase == WARMUP && advance != NULL &&
				advance->available && advance->reason == MP_MATCH_PROTOCOL_REASON_OK;
			return series.present &&
				( ( ( phase == GAMEREVIEW || recoveredReview ) &&
					series.state == MP_MATCH_VIEW_SERIES_MAP_COMPLETE ) ||
				( ( phase == WARMUP || phase == NEXTGAME ) &&
					series.state == MP_MATCH_VIEW_SERIES_READY ) );
		}

		case MP_MATCH_OP_SERIES_CONTESTANT_BIND:
			return series.present && series.gameType == GAME_DUEL &&
				phase == WARMUP &&
				series.state != MP_MATCH_VIEW_SERIES_DISABLED &&
				series.state != MP_MATCH_VIEW_SERIES_COMPLETE &&
				series.state != MP_MATCH_VIEW_SERIES_CANCELLED;

		default:
			return true;
	}
}

bool mpMatchControlModel::HasProjectedGlobalAuthority( void ) const {
	if ( ( recipient.publicRoleMask &
		MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_REFEREE ) ) != 0 ) {
		return true;
	}
	// broadcaster_set is deliberately local-operator-only.  Its exact
	// recipient-scoped availability is therefore the non-disclosing authority
	// proof for a local operator whose private server role is not projected.
	const mpMatchViewOperationAvailability_t *broadcaster =
		OperationAvailability( MP_MATCH_OP_BROADCASTER_SET );
	return broadcaster != NULL && broadcaster->available &&
		broadcaster->reason == MP_MATCH_PROTOCOL_REASON_OK;
}

bool mpMatchControlModel::CanManageSide( int side ) const {
	return IsPlayableSide( side ) &&
		( side == recipient.side || HasProjectedGlobalAuthority() );
}

int mpMatchControlModel::DefaultActionSide( void ) const {
	if ( IsPlayableSide( recipient.side ) ) {
		return recipient.side;
	}
	return IsPlayableSide( recipient.competitionSide ) ?
		recipient.competitionSide : MP_MATCH_VIEW_SIDE_NONE;
}

bool mpMatchControlModel::ResolveActionSide( bool requireKnownTeam,
		bool allowCompetitionSide, int &side ) const {
	side = static_cast<int>( actionSideChoice );
	if ( !IsPlayableSide( side ) ) {
		side = IsPlayableSide( recipient.side ) ? recipient.side :
			( allowCompetitionSide && IsPlayableSide( recipient.competitionSide ) ?
				recipient.competitionSide : MP_MATCH_VIEW_SIDE_NONE );
	}
	if ( !IsPlayableSide( side ) ||
		( requireKnownTeam && !teamKnown[ side ] ) ) {
		return false;
	}
	if ( HasProjectedGlobalAuthority() ) {
		return true;
	}
	const int ownSide = IsPlayableSide( recipient.side ) ? recipient.side :
		( allowCompetitionSide && IsPlayableSide( recipient.competitionSide ) ?
			recipient.competitionSide : MP_MATCH_VIEW_SIDE_NONE );
	return side == ownSide;
}

bool mpMatchControlModel::ParticipantIsRostered(
	mpMatchProtocolParticipantId_t participantId ) const {
	if ( participantId == 0 ) {
		return false;
	}
	for ( int index = 0; index < teamRowCount; ++index ) {
		const mpMatchControlTeamRow_t &row = teamRows[ index ];
		if ( row.kind == MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT &&
			row.occupied && row.participantId == participantId ) {
			return true;
		}
	}
	return false;
}

const mpMatchControlTeamRow_t *mpMatchControlModel::TeamRow( int index ) const {
	return index >= 0 && index < teamRowCount ? &teamRows[ index ] : NULL;
}

const mpMatchControlReplacementRow_t *
mpMatchControlModel::ReplacementRow( int index ) const {
	return index >= 0 && index < replacementRowCount ?
		&replacementRows[ index ] : NULL;
}

const mpMatchControlProposalTemplateRow_t *
mpMatchControlModel::ProposalTemplateRow( int index ) const {
	return index >= 0 && index < proposalTemplateRowCount ?
		&proposalTemplateRows[ index ] : NULL;
}

const mpMatchControlProfileRow_t *mpMatchControlModel::ProfileRow(
	int index ) const {
	return index >= 0 && index < profileRowCount ? &profileRows[ index ] : NULL;
}

const mpMatchControlRuleRow_t *mpMatchControlModel::RuleRow( int index ) const {
	return index >= 0 && index < ruleRowCount ? &ruleRows[ index ] : NULL;
}

const mpMatchControlSeriesMapRow_t *mpMatchControlModel::SeriesMapRow(
	int index ) const {
	return index >= 0 && index < seriesMapRowCount ?
		&seriesMapRows[ index ] : NULL;
}

const mpMatchControlSeriesHistoryRow_t *
mpMatchControlModel::SeriesHistoryRow( int index ) const {
	return index >= 0 && index < seriesHistoryRowCount ?
		&seriesHistoryRows[ index ] : NULL;
}

const mpMatchControlEvidenceRow_t *mpMatchControlModel::EvidenceRow(
	int index ) const {
	return index >= 0 && index < evidenceRowCount ? &evidenceRows[ index ] : NULL;
}

bool mpMatchControlModel::SelectTeamRow( int index ) {
	const mpMatchControlTeamRow_t *row = TeamRow( index );
	if ( row == NULL ) {
		return false;
	}
	selectedTeamRow = index;
	if ( row->kind == MP_MATCH_CONTROL_TEAM_ROW_SIDE &&
		IsPlayableSide( row->side ) ) {
		actionSideChoice = static_cast<mpMatchControlSideChoice_t>( row->side );
		actionSideChoiceExplicit = true;
	}
	return true;
}

bool mpMatchControlModel::SelectReplacementRow( int index ) {
	if ( ReplacementRow( index ) == NULL ) {
		return false;
	}
	selectedReplacementRow = index;
	return true;
}

bool mpMatchControlModel::SelectProposalTemplateRow( int index ) {
	if ( ProposalTemplateRow( index ) == NULL ) {
		return false;
	}
	selectedProposalTemplateRow = index;
	return true;
}

bool mpMatchControlModel::SelectProfileRow( int index ) {
	if ( ProfileRow( index ) == NULL ) {
		return false;
	}
	selectedProfileRow = index;
	return true;
}

bool mpMatchControlModel::SelectRuleRow( int index ) {
	if ( RuleRow( index ) == NULL ) {
		return false;
	}
	selectedRuleRow = index;
	return true;
}

bool mpMatchControlModel::SelectSeriesMapRow( int index ) {
	if ( SeriesMapRow( index ) == NULL ) {
		return false;
	}
	selectedSeriesMapRow = index;
	return true;
}

bool mpMatchControlModel::SetRoleChoice( mpMatchProtocolRosterRole_t role ) {
	if ( role < MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER ||
		role > MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE ) {
		return false;
	}
	roleChoice = role;
	return true;
}

bool mpMatchControlModel::SetProposalChoice(
	mpMatchControlProposalChoice_t choice ) {
	if ( choice < MP_MATCH_CONTROL_PROPOSAL_GLOBAL ||
		choice >= MP_MATCH_CONTROL_PROPOSAL_CHOICE_COUNT ) {
		return false;
	}
	proposalChoice = choice;
	return true;
}

bool mpMatchControlModel::SetActionSideChoice(
		mpMatchControlSideChoice_t choice ) {
	if ( choice != MP_MATCH_CONTROL_SIDE_CHOICE_NONE &&
		( choice < MP_MATCH_CONTROL_SIDE_CHOICE_ZERO ||
			choice >= MP_MATCH_CONTROL_SIDE_CHOICE_COUNT ) ) {
		return false;
	}
	actionSideChoice = choice;
	actionSideChoiceExplicit = choice != MP_MATCH_CONTROL_SIDE_CHOICE_NONE;
	return true;
}

bool mpMatchControlModel::CanChooseActionSide( int side ) const {
	if ( !ready || !IsPlayableSide( side ) ) {
		return false;
	}
	const bool competitionSides = ActionSideUsesCompetitionLabels();
	if ( !competitionSides && !teamKnown[ side ] ) {
		return false;
	}
	if ( HasProjectedGlobalAuthority() ) {
		return true;
	}
	const int ownSide = IsPlayableSide( recipient.side ) ? recipient.side :
		( competitionSides && IsPlayableSide( recipient.competitionSide ) ?
			recipient.competitionSide : MP_MATCH_VIEW_SIDE_NONE );
	return side == ownSide;
}

bool mpMatchControlModel::ActionSideUsesCompetitionLabels( void ) const {
	return ready && ( ( series.present && series.gameType == GAME_DUEL ) ||
		( !IsPlayableSide( recipient.side ) && IsPlayableSide( recipient.competitionSide ) ) );
}

bool mpMatchControlModel::SetSeriesProfileChoice(
	mpSeriesProfileId_t profile ) {
	if ( profile < MP_SERIES_PROFILE_BEST_OF_ONE ||
		profile >= MP_SERIES_PROFILE_COUNT ||
		MPSeriesProfileDescriptorForId( profile ) == NULL ) {
		return false;
	}
	seriesProfileChoice = profile;
	return true;
}

bool mpMatchControlModel::SetSelectedRuleValue( int value ) {
	if ( selectedRuleRow < 0 || selectedRuleRow >= ruleRowCount ) {
		return false;
	}
	mpMatchControlRuleRow_t &row = ruleRows[ selectedRuleRow ];
	const mpRuleFieldDescriptor_t *descriptor = MPMatchRuleField( row.fieldId );
	if ( !row.editable || descriptor == NULL ||
		!RuleValueAllowed( *descriptor, value ) ) {
		return false;
	}
	row.editValue = value;
	row.editValueValid = true;
	return true;
}

bool mpMatchControlModel::BuildRequest( mpMatchControlCommand_t command,
	unsigned int requestId, mpMatchOperationRequest_t &request,
	mpMatchControlError_t *error ) const {
	ClearError( error );
	const mpMatchOperationOpcode_t opcode = MPMatchControlCommandOpcode( command );
	if ( opcode == MP_MATCH_OP_INVALID ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_UNKNOWN_COMMAND );
		return false;
	}
	if ( !ready || sessionId == 0 || sessionRevision == 0 ||
		controlRevision == 0 || recipient.participantId == 0 ||
		recipient.slot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ||
		recipient.bindingGeneration == 0 ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VIEW, opcode );
		return false;
	}
	if ( requestId == 0 ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_REQUEST_ID, opcode );
		return false;
	}
	const mpMatchViewOperationAvailability_t *operation =
		OperationAvailability( opcode );
	if ( operation == NULL || !operation->available ||
		operation->reason != MP_MATCH_PROTOCOL_REASON_OK ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE, opcode );
		if ( error != NULL && operation != NULL ) {
			error->protocolReason = operation->reason;
			error->fieldId = operation->fieldId;
			error->detail = operation->detail;
		}
		return false;
	}

	mpMatchOperationRequest_t candidate;
	candidate.Clear();
	candidate.sessionId = sessionId;
	candidate.requestId = requestId;
	candidate.opcode = opcode;
	candidate.expectedSessionRevision = sessionRevision;
	candidate.expectedControlRevision = controlRevision;
	candidate.actorSlot = recipient.slot;
	candidate.actorBindingGeneration = recipient.bindingGeneration;

	const mpMatchControlTeamRow_t *selectedTeam = TeamRow( selectedTeamRow );
	const mpMatchControlReplacementRow_t *selectedReplacement =
		ReplacementRow( selectedReplacementRow );
	const mpMatchControlProfileRow_t *selectedProfile =
		ProfileRow( selectedProfileRow );
	const mpMatchControlRuleRow_t *selectedRule = RuleRow( selectedRuleRow );
	const mpMatchControlProposalTemplateRow_t *selectedTemplate =
		ProposalTemplateRow( selectedProposalTemplateRow );
	const mpMatchControlSeriesMapRow_t *selectedMap =
		SeriesMapRow( selectedSeriesMapRow );

	switch ( command ) {
		case MP_MATCH_CONTROL_COMMAND_READY_TOGGLE:
			if ( !AddBoolArgument( candidate, MP_MATCH_ARG_ENABLED,
					!recipient.ready ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE: {
			int targetSide = MP_MATCH_VIEW_SIDE_NONE;
			if ( !ResolveActionSide( true, false, targetSide ) ) {
				SetError( error,
					actionSideChoice == MP_MATCH_CONTROL_SIDE_CHOICE_NONE ?
						MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED :
						MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
					opcode );
				return false;
			}
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( targetSide );
			if ( !AddBoolArgument( candidate, MP_MATCH_ARG_ENABLED,
					!teamReady[ targetSide ] ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_FORCE_READY:
			if ( !AddBoolArgument( candidate, MP_MATCH_ARG_ENABLED, true ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_TIMEOUT: {
			int targetSide = MP_MATCH_VIEW_SIDE_NONE;
			const bool contestant = ActionSideUsesCompetitionLabels();
			if ( !ResolveActionSide( !contestant, contestant, targetSide ) ) {
				SetError( error,
					actionSideChoice == MP_MATCH_CONTROL_SIDE_CHOICE_NONE ?
						MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED :
						MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
					opcode );
				return false;
			}
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( targetSide );
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_FORFEIT: {
			int forfeitingSide = MP_MATCH_VIEW_SIDE_NONE;
			if ( !ResolveActionSide( false, true, forfeitingSide ) ) {
				SetError( error,
					actionSideChoice == MP_MATCH_CONTROL_SIDE_CHOICE_NONE ?
						MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED :
						MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
					opcode );
				return false;
			}
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( forfeitingSide );
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_TECH_PAUSE:
			if ( IsPlayableSide( recipient.side ) ) {
				candidate.hasTeamTarget = true;
				candidate.teamTarget = ProtocolTeamForSide( recipient.side );
			}
			if ( !AddStringArgument( candidate, MP_MATCH_ARG_REASON,
					TECH_PAUSE_REASON ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
					opcode, -1, MP_MATCH_ARG_REASON );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_ABORT:
			if ( !AddStringArgument( candidate, MP_MATCH_ARG_REASON,
					ABORT_REASON ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
					opcode, -1, MP_MATCH_ARG_REASON );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_RESUME:
		case MP_MATCH_CONTROL_COMMAND_REFEREE_LOGOUT:
		case MP_MATCH_CONTROL_COMMAND_QUEUE_JOIN:
		case MP_MATCH_CONTROL_COMMAND_QUEUE_DEFER:
		case MP_MATCH_CONTROL_COMMAND_QUEUE_LEAVE:
		case MP_MATCH_CONTROL_COMMAND_ROSTER_LEAVE:
		case MP_MATCH_CONTROL_COMMAND_RULES_COMMIT:
		case MP_MATCH_CONTROL_COMMAND_RULES_DISCARD:
		case MP_MATCH_CONTROL_COMMAND_SERIES_START:
		case MP_MATCH_CONTROL_COMMAND_SERIES_CANCEL:
			// Intentionally no targets and no arguments.  In particular,
			// queue_join is side-neutral; authoritative admission picks a side.
			break;

		case MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE:
			if ( !OperationContextAccepted( MP_MATCH_OP_SERIES_ADVANCE ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_MARINE:
			candidate.hasTeamTarget = true;
			candidate.teamTarget = MP_MATCH_TEAM_MARINE;
			break;

		case MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_STROGG:
			candidate.hasTeamTarget = true;
			candidate.teamTarget = MP_MATCH_TEAM_STROGG;
			break;

		case MP_MATCH_CONTROL_COMMAND_TEAM_SPECTATE:
			candidate.hasTeamTarget = true;
			candidate.teamTarget = MP_MATCH_TEAM_SPECTATOR;
			break;

		case MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE: {
			int targetSide = MP_MATCH_VIEW_SIDE_NONE;
			if ( !ResolveActionSide( true, false, targetSide ) ) {
				SetError( error,
					actionSideChoice == MP_MATCH_CONTROL_SIDE_CHOICE_NONE ?
						MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED :
						MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( targetSide );
			if ( !AddBoolArgument( candidate, MP_MATCH_ARG_ENABLED,
					!teamLocked[ targetSide ] ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT:
			if ( selectedTeam == NULL ||
				selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_INVITATION ||
				selectedTeam->invitationId == 0 ||
				selectedTeam->participantId != recipient.participantId ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow, MP_MATCH_ARG_INVITATION_ID );
				return false;
			}
			if ( !AddUIntArgument( candidate, MP_MATCH_ARG_INVITATION_ID,
					selectedTeam->invitationId ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE:
			if ( selectedTeam == NULL || !IsPlayableSide( selectedTeam->side ) ||
				!CanManageSide( selectedTeam->side ) ||
				selectedReplacement == NULL ||
				selectedReplacement->participantId == 0 ||
				!selectedReplacement->connected || !selectedReplacement->human ||
				selectedReplacement->rostered ||
				selectedReplacement->participantId == recipient.participantId ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedReplacement->participantId;
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( selectedTeam->side );
			if ( !AddEnumArgument( candidate, MP_MATCH_ARG_ROLE,
					static_cast<unsigned short>( roleChoice ) ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_ROSTER_REMOVE:
			if ( selectedTeam == NULL ||
				selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT ||
				!selectedTeam->occupied || selectedTeam->participantId == 0 ||
				!IsPlayableSide( selectedTeam->side ) ||
				!CanManageSide( selectedTeam->side ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( selectedTeam->side );
			break;

		case MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE:
			if ( selectedTeam == NULL ||
				selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT ||
				!selectedTeam->occupied || selectedTeam->participantId == 0 ||
				!IsPlayableSide( selectedTeam->side ) ||
				!CanManageSide( selectedTeam->side ) ||
				!selectedTeam->connected || !selectedTeam->human ||
				!selectedTeam->active ||
				( selectedTeam->rosterRole != MP_MATCH_VIEW_ROSTER_PLAYER &&
					selectedTeam->rosterRole != MP_MATCH_VIEW_ROSTER_CAPTAIN ) ||
				selectedReplacement == NULL ||
				selectedReplacement->participantId == 0 ||
				!selectedReplacement->connected || !selectedReplacement->human ||
				selectedReplacement->active ||
				selectedReplacement->participantId == selectedTeam->participantId ||
				( selectedReplacement->rostered &&
					( selectedReplacement->rosterRole !=
						MP_MATCH_VIEW_ROSTER_SUBSTITUTE ||
					selectedReplacement->side != selectedTeam->side ) ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow,
					MP_MATCH_ARG_REPLACEMENT_PARTICIPANT );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( selectedTeam->side );
			// The authoritative operation inherits selectedTeam->rosterRole.
			// Role choice is intentionally absent from this request.
			if ( !AddParticipantArgument( candidate,
					MP_MATCH_ARG_REPLACEMENT_PARTICIPANT,
					selectedReplacement->participantId ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_ROLE_ASSIGN:
			if ( selectedTeam == NULL ||
				( selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT &&
					selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT ) ||
				selectedTeam->participantId == 0 ||
				!selectedTeam->connected || !selectedTeam->human ||
				!IsPlayableSide( selectedTeam->side ) ||
				!CanManageSide( selectedTeam->side ) ||
				( selectedTeam->publicRoleMask &
					( MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_BROADCASTER ) |
						MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_REFEREE ) ) ) != 0 ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			candidate.hasTeamTarget = true;
			candidate.teamTarget = ProtocolTeamForSide( selectedTeam->side );
			if ( !AddEnumArgument( candidate, MP_MATCH_ARG_ROLE,
					static_cast<unsigned short>( roleChoice ) ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET: {
			const mpMatchViewPublicRoleMask_t playerRole =
				MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_PLAYER );
			const mpMatchViewPublicRoleMask_t broadcasterRole =
				MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_BROADCASTER );
			if ( selectedTeam == NULL ||
				selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT ||
				selectedTeam->participantId == 0 ||
				!selectedTeam->connected || !selectedTeam->human ||
				selectedTeam->active ||
				selectedTeam->side != MP_MATCH_VIEW_SIDE_NONE ||
				ParticipantIsRostered( selectedTeam->participantId ) ||
				( selectedTeam->publicRoleMask != playerRole &&
					selectedTeam->publicRoleMask != broadcasterRole ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			if ( !AddBoolArgument( candidate, MP_MATCH_ARG_ENABLED,
					selectedTeam->publicRoleMask != broadcasterRole ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE:
			if ( selectedTeam == NULL ||
					selectedTeam->kind != MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT ||
					selectedTeam->participantId == 0 ||
					selectedTeam->participantId == recipient.participantId ||
					!selectedTeam->connected || !selectedTeam->human ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow );
				return false;
			}
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			break;

		case MP_MATCH_CONTROL_COMMAND_RULES_SELECT_PROFILE:
			if ( selectedProfile == NULL || selectedProfile->keyLength == 0 ||
				!AddStringArgument( candidate, MP_MATCH_ARG_PROFILE,
					selectedProfile->key, selectedProfile->keyLength ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedProfileRow, MP_MATCH_ARG_PROFILE );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_RULES_STAGE_FIELD:
			if ( selectedRule == NULL || !selectedRule->editable ||
				!selectedRule->editValueValid ||
				!AddStringArgument( candidate, MP_MATCH_ARG_SETTING_ID,
					selectedRule->key, selectedRule->keyLength ) ||
				!AddRuleValueArgument( candidate, MP_MATCH_ARG_SETTING_VALUE,
					*selectedRule ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedRuleRow, MP_MATCH_ARG_SETTING_VALUE );
				return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE:
			if ( selectedTemplate == NULL || selectedTemplate->opcode <=
					MP_MATCH_OP_INVALID ||
				!AddOpcodeArgument( candidate, MP_MATCH_ARG_PROPOSED_OPCODE,
					selectedTemplate->opcode ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedProposalTemplateRow,
					MP_MATCH_ARG_PROPOSED_OPCODE );
				return false;
			}
			if ( selectedTemplate->targetKind ==
					MP_MATCH_CONTROL_PROPOSAL_TARGET_OWN_SIDE ) {
				// A side ballot always belongs to the recipient's own team. The
				// action-side selector is ignored: authority over another side
				// never calls that side's vote.
				if ( selectedTemplate->globalOnly ||
						selectedTemplate->opcode != MP_MATCH_OP_FORFEIT ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
						opcode, selectedProposalTemplateRow,
						MP_MATCH_ARG_PROPOSED_OPCODE );
					return false;
				}
				if ( !IsPlayableSide( recipient.side ) || !teamKnown[ recipient.side ] ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_SIDE, opcode,
						selectedProposalTemplateRow );
					return false;
				}
				candidate.hasTeamTarget = true;
				candidate.teamTarget = ProtocolTeamForSide( recipient.side );
				break;
			}
			if ( !selectedTemplate->globalOnly ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
					opcode, selectedProposalTemplateRow,
					MP_MATCH_ARG_PROPOSED_OPCODE );
				return false;
			}
			if ( selectedTemplate->targetKind ==
					MP_MATCH_CONTROL_PROPOSAL_TARGET_PARTICIPANT ) {
				if ( selectedTeam == NULL || selectedTeam->kind !=
						MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT ||
						selectedTeam->participantId == 0 ||
						selectedTeam->participantId == recipient.participantId ||
						!selectedTeam->connected || !selectedTeam->human ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
						opcode, selectedTeamRow );
					return false;
				}
				candidate.hasParticipantTarget = true;
				candidate.participantTarget = selectedTeam->participantId;
			} else if ( selectedTemplate->targetKind !=
					MP_MATCH_CONTROL_PROPOSAL_TARGET_NONE ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
					opcode, selectedProposalTemplateRow );
				return false;
			}
			// Ballot scope remains global.  A participant target identifies the
			// proposed operation's subject and never becomes a connection slot.
			switch ( selectedTemplate->opcode ) {
				case MP_MATCH_OP_RESUME_REQUEST:
				case MP_MATCH_OP_RULES_COMMIT:
				case MP_MATCH_OP_PARTICIPANT_REMOVE:
					break;
				case MP_MATCH_OP_RULES_SELECT_PROFILE:
					if ( selectedProfile == NULL ||
						!AddStringArgument( candidate,
							NestedField( MP_MATCH_ARG_PROFILE ),
							selectedProfile->key, selectedProfile->keyLength ) ) {
						SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
							opcode, selectedProfileRow, MP_MATCH_ARG_PROFILE );
						return false;
					}
					break;
				case MP_MATCH_OP_RULES_STAGE_FIELD:
					if ( selectedRule == NULL || !selectedRule->editable ||
						!selectedRule->editValueValid ||
						!AddStringArgument( candidate,
							NestedField( MP_MATCH_ARG_SETTING_ID ),
							selectedRule->key, selectedRule->keyLength ) ||
						!AddRuleValueArgument( candidate,
							NestedField( MP_MATCH_ARG_SETTING_VALUE ),
							*selectedRule ) ) {
						SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
							opcode, selectedRuleRow, MP_MATCH_ARG_SETTING_VALUE );
						return false;
					}
					break;
				case MP_MATCH_OP_ABORT:
					if ( !AddStringArgument( candidate,
							NestedField( MP_MATCH_ARG_REASON ), ABORT_REASON ) ) {
						SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
							opcode, -1, MP_MATCH_ARG_REASON );
						return false;
					}
					break;
				default:
					SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
						opcode, selectedProposalTemplateRow );
					return false;
			}
			break;

		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES:
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO:
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN:
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL: {
			const mpMatchViewProposalSummary_t &proposal =
				proposalChoice == MP_MATCH_CONTROL_PROPOSAL_GLOBAL ?
				globalProposal : ownSideProposal;
			if ( !proposal.present || proposal.proposalId == 0 ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_PROPOSAL_MISSING, opcode );
				return false;
			}
			if ( command == MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL &&
				proposal.callerParticipantId != recipient.participantId &&
				!HasProjectedGlobalAuthority() ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID, opcode );
				return false;
			}
			if ( command != MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL &&
				!proposal.recipientEligible ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID, opcode );
				return false;
			}
			// Scope is resolved authoritatively by proposal_id.  Never serialize
			// the GUI's global/side choice as a team target or argument.
			if ( !AddUIntArgument( candidate, MP_MATCH_ARG_PROPOSAL_ID,
					proposal.proposalId ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			if ( command != MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL ) {
				unsigned short ballot = MP_MATCH_BALLOT_YES;
				if ( command == MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO ) {
					ballot = MP_MATCH_BALLOT_NO;
				} else if ( command ==
					MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN ) {
					ballot = MP_MATCH_BALLOT_ABSTAIN;
				}
				if ( !AddEnumArgument( candidate,
						MP_MATCH_ARG_BALLOT_CHOICE, ballot ) ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
					return false;
				}
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_SERIES_STAGE: {
			const mpSeriesProfileDescriptor *profile =
				MPSeriesProfileDescriptorForId( seriesProfileChoice );
			if ( profile == NULL || profile->bestOf < 1 || profile->bestOf > 15 ||
				!AddStringArgument( candidate, MP_MATCH_ARG_SERIES_PROFILE,
					profile->key ) ||
				!AddUIntArgument( candidate, MP_MATCH_ARG_BEST_OF,
					static_cast<unsigned int>( profile->bestOf ) ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
					opcode, -1, MP_MATCH_ARG_SERIES_PROFILE );
				return false;
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND: {
			if ( !OperationContextAccepted( MP_MATCH_OP_SERIES_CONTESTANT_BIND ) ||
					selectedTeam == NULL || selectedTeam->kind !=
						MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT ||
					selectedTeam->participantId == 0 || !selectedTeam->connected ||
					!selectedTeam->human || !selectedTeam->active ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedTeamRow, MP_MATCH_ARG_COMPETITION_SIDE );
				return false;
			}
			int competitionSide = MP_MATCH_VIEW_SIDE_NONE;
			if ( !ResolveActionSide( false, true, competitionSide ) ) {
				SetError( error,
					actionSideChoice == MP_MATCH_CONTROL_SIDE_CHOICE_NONE ?
						MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED :
						MP_MATCH_CONTROL_ERROR_INVALID_SIDE,
					opcode, selectedTeamRow, MP_MATCH_ARG_COMPETITION_SIDE );
				return false;
			}
			const unsigned short protocolSide = competitionSide == 0 ?
				MP_MATCH_COMPETITION_SIDE_A : MP_MATCH_COMPETITION_SIDE_B;
			candidate.hasParticipantTarget = true;
			candidate.participantTarget = selectedTeam->participantId;
			if ( !AddEnumArgument( candidate, MP_MATCH_ARG_COMPETITION_SIDE,
					protocolSide ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
				return false;
			}
			break;
		}

		case MP_MATCH_CONTROL_COMMAND_VETO_BAN:
		case MP_MATCH_CONTROL_COMMAND_VETO_PICK:
		case MP_MATCH_CONTROL_COMMAND_VETO_DECIDER:
		case MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE:
		case MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG: {
			if ( !series.present || !series.hasVetoTurn || selectedMap == NULL ||
				selectedMap->map.tokenLength == 0 ||
				selectedMap->map.tokenLength > 63 ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED,
					opcode, selectedSeriesMapRow, MP_MATCH_ARG_MAP_TOKEN );
				return false;
			}
			if ( !HasProjectedGlobalAuthority() &&
				IsPlayableSide( recipient.competitionSide ) &&
				recipient.competitionSide != series.vetoTurnSide ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_SIDE, opcode );
				return false;
			}
			mpMatchViewVetoAction_t expectedViewAction = MP_MATCH_VIEW_VETO_BAN;
			unsigned short protocolAction = MP_MATCH_VETO_BAN;
			if ( command == MP_MATCH_CONTROL_COMMAND_VETO_PICK ) {
				expectedViewAction = MP_MATCH_VIEW_VETO_PICK;
				protocolAction = MP_MATCH_VETO_PICK;
			} else if ( command == MP_MATCH_CONTROL_COMMAND_VETO_DECIDER ) {
				expectedViewAction = MP_MATCH_VIEW_VETO_DECIDER;
				protocolAction = MP_MATCH_VETO_DECIDER;
			} else if ( command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE ||
				command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG ) {
				expectedViewAction = MP_MATCH_VIEW_VETO_SIDE;
				protocolAction = MP_MATCH_VETO_SIDE;
			}
			if ( series.vetoTurnAction != expectedViewAction ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
					opcode, selectedSeriesMapRow, MP_MATCH_ARG_VETO_ACTION );
				return false;
			}
			if ( protocolAction == MP_MATCH_VETO_SIDE ) {
				unsigned char newestSelection = 0;
				for ( int index = 0; index < seriesMapRowCount; ++index ) {
					if ( seriesMapRows[ index ].map.selectionNumber > newestSelection ) {
						newestSelection = seriesMapRows[ index ].map.selectionNumber;
					}
				}
				if ( selectedMap->map.disposition != MP_MATCH_VIEW_MAP_SELECTED ||
					selectedMap->map.hasStartingGameSide ||
					selectedMap->map.selectionNumber == 0 ||
					selectedMap->map.selectionNumber != newestSelection ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
						opcode, selectedSeriesMapRow, MP_MATCH_ARG_MAP_TOKEN );
					return false;
				}
			} else if ( selectedMap->map.disposition !=
				MP_MATCH_VIEW_MAP_AVAILABLE ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
					opcode, selectedSeriesMapRow, MP_MATCH_ARG_MAP_TOKEN );
				return false;
			}
			if ( protocolAction == MP_MATCH_VETO_DECIDER ) {
				int availableMaps = 0;
				for ( int index = 0; index < seriesMapRowCount; ++index ) {
					if ( seriesMapRows[ index ].map.disposition ==
						MP_MATCH_VIEW_MAP_AVAILABLE ) {
						++availableMaps;
					}
				}
				if ( availableMaps != 1 ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_SELECTION_INVALID,
						opcode, selectedSeriesMapRow, MP_MATCH_ARG_MAP_TOKEN );
					return false;
				}
			}
			if ( !AddEnumArgument( candidate, MP_MATCH_ARG_VETO_ACTION,
					protocolAction ) ||
				!AddStringArgument( candidate, MP_MATCH_ARG_MAP_TOKEN,
					selectedMap->map.mapToken, selectedMap->map.tokenLength ) ) {
				SetError( error, MP_MATCH_CONTROL_ERROR_INVALID_VALUE,
					opcode, selectedSeriesMapRow, MP_MATCH_ARG_MAP_TOKEN );
				return false;
			}
			if ( protocolAction == MP_MATCH_VETO_SIDE ) {
				const unsigned short startingSide = command ==
					MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE ?
					MP_MATCH_STARTING_SIDE_MARINE :
					MP_MATCH_STARTING_SIDE_STROGG;
				if ( !AddEnumArgument( candidate, MP_MATCH_ARG_STARTING_SIDE,
						startingSide ) ) {
					SetError( error, MP_MATCH_CONTROL_ERROR_CAPACITY, opcode );
					return false;
				}
			}
			break;
		}

		default:
			SetError( error, MP_MATCH_CONTROL_ERROR_UNKNOWN_COMMAND, opcode );
			return false;
	}

	mpMatchProtocolError_t protocolError;
	protocolError.Clear();
	if ( !MPMatchProtocolValidateRequest( candidate, &protocolError ) ) {
		SetError( error, MP_MATCH_CONTROL_ERROR_PROTOCOL_INVALID, opcode,
			-1, protocolError.fieldId, protocolError.detail );
		if ( error != NULL ) {
			error->protocolReason = protocolError.reason;
		}
		return false;
	}
	request = candidate;
	return true;
}
