#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and native contracts for the typed Match Control client model."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/mpgame/mp/match/MatchControlModel.h"
SOURCE = ROOT / "src/mpgame/mp/match/MatchControlModel.cpp"

EXPECTED_TOKENS = [
    "ready_toggle",
    "team_ready_toggle",
    "force_ready",
    "timeout",
    "tech_pause",
    "resume",
    "referee_logout",
    "forfeit",
    "abort",
    "team_join_marine",
    "team_join_strogg",
    "team_spectate",
    "queue_join",
    "queue_defer",
    "queue_leave",
    "roster_leave",
    "roster_accept",
    "roster_invite",
    "roster_remove",
    "roster_substitute",
    "role_assign",
    "broadcaster_set",
    "team_lock_toggle",
    "proposal_create",
    "proposal_yes",
    "proposal_no",
    "proposal_abstain",
    "proposal_cancel",
    "rules_select_profile",
    "rules_stage_field",
    "rules_commit",
    "rules_discard",
    "series_stage",
    "series_start",
    "series_cancel",
    "series_advance",
    "veto_ban",
    "veto_pick",
    "veto_decider",
    "veto_side_marine",
    "veto_side_strogg",
    "participant_remove",
    "series_contestant_bind",
]


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    tokens = re.findall(
        r'\{\s*"([a-z0-9_]+)"\s*,\s*MP_MATCH_CONTROL_COMMAND_', source
    )
    if tokens != EXPECTED_TOKENS:
        raise AssertionError(
            "Match Control operation-token registry drifted:\n"
            f"expected {EXPECTED_TOKENS}\nactual   {tokens}"
        )

    for token in (
        "static_assert( sizeof( COMMANDS ) / sizeof( COMMANDS[ 0 ] ) ==",
        "MP_MATCH_CONTROL_COMMAND_COUNT - 1",
        "MPMatchViewValidate( view",
        "MPMatchProtocolValidateRequest( candidate",
        "candidate.expectedSessionRevision = sessionRevision",
        "candidate.expectedControlRevision = controlRevision",
        "candidate.actorBindingGeneration = recipient.bindingGeneration",
        "MP_MATCH_ARG_INVITATION_ID",
        "MP_MATCH_ARG_REPLACEMENT_PARTICIPANT",
        "MP_MATCH_OP_BROADCASTER_SET",
        "MP_MATCH_ARG_PROPOSAL_ID",
        "MP_MATCH_ARG_BALLOT_CHOICE",
        "MP_MATCH_NESTED_ARGUMENT_BASE",
        "selectedTemplate->globalOnly",
        "selectedTemplate->targetKind",
        "MP_MATCH_CONTROL_PROPOSAL_TARGET_PARTICIPANT",
        "MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_REFEREE )",
        "MP_MATCH_ARG_SETTING_ID",
        "MP_MATCH_ARG_SETTING_VALUE",
        "MP_MATCH_ARG_SERIES_PROFILE",
        "MP_MATCH_ARG_BEST_OF",
        "MP_MATCH_ARG_VETO_ACTION",
        "MP_MATCH_ARG_STARTING_SIDE",
        "MP_MATCH_ARG_COMPETITION_SIDE",
        "OperationContextAccepted( MP_MATCH_OP_SERIES_ADVANCE )",
        "OperationContextAccepted( MP_MATCH_OP_SERIES_CONTESTANT_BIND )",
        'static const char TECH_PAUSE_REASON[] = "match_control_technical_pause"',
        'static const char ABORT_REASON[] = "match_control_abort"',
        "queue_join is side-neutral",
        "Scope is resolved authoritatively by proposal_id",
        "Role choice is intentionally absent from this request",
    ):
        require(combined, token, "typed request construction boundary")

    for token in (
        "mpMatchControlTeamRow_t",
        "mpMatchControlReplacementRow_t",
        "mpMatchControlProposalTemplateRow_t",
        "mpMatchControlProfileRow_t",
        "mpMatchControlRuleRow_t",
        "mpMatchControlSeriesMapRow_t",
        "mpMatchControlSeriesHistoryRow_t",
        "mpMatchControlEvidenceRow_t",
        "mpMatchControlSideChoice_t",
        "SetActionSideChoice",
        "CanChooseActionSide",
        "ActionSideUsesCompetitionLabels",
        "ResolveActionSide",
        "RestoreSelectionsFrom",
        "Output is transactional and remains unchanged on failure",
    ):
        require(combined, token, "bounded typed view model")

    for forbidden in (
        "idUserInterface",
        "idCVar",
        "idStr",
        "idList",
        "std::vector",
        "std::string",
        "new ",
        "malloc(",
        "realloc(",
        "cmdSystem",
        "cvarSystem",
        "fileSystem",
        "consoleCommand",
        "atoi(",
        "sscanf(",
        "strtok(",
    ):
        if forbidden in combined:
            raise AssertionError(
                f"typed Match Control model contains forbidden dependency {forbidden!r}"
            )

    if re.search(r"(?:name|label|display)[A-Za-z_]*\s*(?:==|!=|,|\))", source, re.I):
        raise AssertionError("request construction appears to inspect presentation identity")

    listed = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if listed.returncode != 0:
        raise AssertionError("could not inspect mpgame source discovery:\n" + listed.stderr)
    if "mpgame/mp/match/MatchControlModel.cpp" not in listed.stdout.splitlines():
        raise AssertionError("MatchControlModel.cpp is not compiled into the MP game module")


HARNESS = r'''
#include <initializer_list>
#include <stdint.h>
#include <string.h>

#define MP_MATCH_CONTROL_MODEL_STANDALONE_TEST 1
#include "mpgame/mp/match/MatchControlModel.cpp"

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

static bool rejectView = false;
static bool rejectProtocol = false;
static bool allowTeamTargetOnTemplates = false;

static void Zero(void *value, unsigned long long bytes) {
	memset(value, 0, static_cast<size_t>(bytes));
}

void mpMatchOperationValue_s::Clear(void) {
	Zero(this, sizeof(*this));
	type = MP_MATCH_VALUE_INVALID;
}

void mpMatchOperationValue_s::SetBool(bool value) {
	Clear(); type = MP_MATCH_VALUE_BOOL; unsignedValue = value ? 1u : 0u;
}

void mpMatchOperationValue_s::SetInt32(int value) {
	Clear(); type = MP_MATCH_VALUE_INT32; signedValue = value;
}

void mpMatchOperationValue_s::SetUInt32(unsigned int value) {
	Clear(); type = MP_MATCH_VALUE_UINT32; unsignedValue = value;
}

void mpMatchOperationValue_s::SetEnum(unsigned short value) {
	Clear(); type = MP_MATCH_VALUE_ENUM; enumValue = value;
}

bool mpMatchOperationValue_s::SetString(const char *value, int length) {
	Clear();
	if (value == NULL) { return false; }
	if (length < 0) { length = static_cast<int>(strlen(value)); }
	if (length < 1 || length > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ||
		value[length] != '\0') { return false; }
	for (int i = 0; i < length; ++i) {
		const unsigned char c = static_cast<unsigned char>(value[i]);
		if (c < 33 || c > 126) { return false; }
	}
	type = MP_MATCH_VALUE_STRING;
	stringLength = static_cast<unsigned short>(length);
	memcpy(stringValue, value, static_cast<size_t>(length));
	stringValue[length] = '\0';
	return true;
}

void mpMatchOperationValue_s::SetOpcode(mpMatchOperationOpcode_t value) {
	Clear(); type = MP_MATCH_VALUE_OPCODE; enumValue = static_cast<unsigned short>(value);
}

void mpMatchOperationValue_s::SetParticipantId(mpMatchProtocolParticipantId_t value) {
	Clear(); type = MP_MATCH_VALUE_PARTICIPANT_ID; unsignedValue = value;
}

void mpMatchOperationArgument_s::Clear(void) {
	Zero(this, sizeof(*this)); value.Clear();
}

void mpMatchOperationRequest_s::Clear(void) {
	Zero(this, sizeof(*this));
	schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
	opcode = MP_MATCH_OP_INVALID;
	teamTarget = MP_MATCH_TEAM_NONE;
	for (int i = 0; i < MP_MATCH_PROTOCOL_MAX_ARGUMENTS; ++i) {
		arguments[i].Clear();
	}
}

void mpMatchProtocolError_s::Clear(void) {
	Zero(this, sizeof(*this)); reason = MP_MATCH_PROTOCOL_REASON_NONE;
}

void mpMatchViewRecipient_s::Clear(void) {
	Zero(this, sizeof(*this)); side = MP_MATCH_VIEW_SIDE_NONE;
	competitionSide = MP_MATCH_VIEW_SIDE_NONE; queueSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewProposalSummary_s::Clear(void) {
	Zero(this, sizeof(*this)); side = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewSeriesSummary_s::Clear(void) {
	Zero(this, sizeof(*this)); gameType = -1; vetoTurnSide = MP_MATCH_VIEW_SIDE_NONE;
}

void mpMatchViewOperationAvailability_s::Clear(void) {
	Zero(this, sizeof(*this)); opcode = MP_MATCH_OP_INVALID;
	reason = MP_MATCH_PROTOCOL_REASON_NONE;
}

void mpMatchViewError_s::Clear(void) {
	Zero(this, sizeof(*this)); reason = MP_MATCH_VIEW_ERROR_NONE;
}

mpMatchViewPublicRoleMask_t MPMatchViewRoleBit(mpMatchViewPublicRole_t role) {
	return role > MP_MATCH_VIEW_ROLE_NONE && role < MP_MATCH_VIEW_ROLE_COUNT ?
		(1u << static_cast<unsigned int>(role)) : 0u;
}

bool MPMatchViewValidate(const mpSessionView &view, mpMatchViewError_t *error) {
	if (error != NULL) { error->Clear(); }
	if (rejectView || view.publicState.schemaVersion != MP_MATCH_VIEW_SCHEMA_VERSION ||
		view.publicState.sessionId == 0 || view.publicState.sessionRevision == 0 ||
		view.publicState.controlRevision == 0 || view.publicState.viewRevision == 0 ||
		view.publicState.operationAvailabilityCount != MP_MATCH_OP_COUNT - 1) {
		if (error != NULL) { error->reason = MP_MATCH_VIEW_ERROR_INVALID_STATE; }
		return false;
	}
	for (int i = 0; i < view.publicState.operationAvailabilityCount; ++i) {
		if (view.publicState.operationAvailability[i].opcode !=
			static_cast<mpMatchOperationOpcode_t>(i + 1)) {
			if (error != NULL) { error->reason = MP_MATCH_VIEW_ERROR_INVALID_STATE; }
			return false;
		}
	}
	return true;
}

static mpMatchOperationDescriptor_t operationDescriptors[MP_MATCH_OP_COUNT];

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor(
	mpMatchOperationOpcode_t opcode) {
	if (opcode <= MP_MATCH_OP_INVALID || opcode >= MP_MATCH_OP_COUNT) { return NULL; }
	mpMatchOperationDescriptor_t &descriptor = operationDescriptors[opcode];
	Zero(&descriptor, sizeof(descriptor));
	descriptor.opcode = opcode;
	if (opcode == MP_MATCH_OP_RESUME_REQUEST ||
		opcode == MP_MATCH_OP_RULES_SELECT_PROFILE ||
		opcode == MP_MATCH_OP_RULES_STAGE_FIELD ||
		opcode == MP_MATCH_OP_RULES_COMMIT || opcode == MP_MATCH_OP_ABORT) {
		descriptor.flags = MP_MATCH_OPERATION_FLAG_PROPOSABLE |
			(allowTeamTargetOnTemplates ? MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET : 0u);
	} else if (opcode == MP_MATCH_OP_PARTICIPANT_REMOVE) {
		descriptor.flags = MP_MATCH_OPERATION_FLAG_PROPOSABLE |
			MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
			MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET |
			(allowTeamTargetOnTemplates ? MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET : 0u);
	} else if (opcode == MP_MATCH_OP_FORFEIT) {
		descriptor.flags = MP_MATCH_OPERATION_FLAG_PROPOSABLE |
			MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
			MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET;
	}
	return &descriptor;
}

static const mpRuleEnumValueDescriptor_t gameTypeValues[] = {
	{ GAME_TDM, "tdm", "#str_test" }
};

static const mpRuleFieldDescriptor_t gameTypeRule = {
	MP_RULE_GAME_TYPE, "game_type", MP_RULE_TYPE_ENUM, GAME_TDM, GAME_TDM,
	GAME_TDM, gameTypeValues, 1, 1u << GAME_TDM, MP_RULE_FROZEN_REJECT,
	"#str_test", "#str_test", NULL
};

static const mpRuleFieldDescriptor_t managedRule = {
	MP_RULE_MANAGED_MATCH, "managed_match", MP_RULE_TYPE_BOOL, 0, 1, 1,
	NULL, 0, 1u << GAME_TDM, MP_RULE_FROZEN_STAGE,
	"#str_test", "#str_test", NULL
};

static const mpRuleFieldDescriptor_t timeLimitRule = {
	MP_RULE_TIME_LIMIT_MINUTES, "time_limit_minutes", MP_RULE_TYPE_INTEGER,
	0, 60, 15, NULL, 0, 1u << GAME_TDM, MP_RULE_FROZEN_STAGE,
	"#str_test", "#str_test", NULL
};

const mpRuleFieldDescriptor_t *MPMatchRuleField(int field) {
	if (field == MP_RULE_GAME_TYPE) { return &gameTypeRule; }
	if (field == MP_RULE_MANAGED_MATCH) { return &managedRule; }
	if (field == MP_RULE_TIME_LIMIT_MINUTES) { return &timeLimitRule; }
	return NULL;
}

static const mpMatchProfileDescriptor_t profiles[] = {
	{ MP_MATCH_PROFILE_CASUAL, "casual", "#str_test", "#str_test",
		1u << GAME_TDM, false },
	{ MP_MATCH_PROFILE_COMPETITIVE_TDM, "competitive_tdm", "#str_test",
		"#str_test", 1u << GAME_TDM, true }
};

int MPMatchProfileCount(void) { return 2; }

const mpMatchProfileDescriptor_t *MPMatchProfile(int profile) {
	return profile >= 0 && profile < 2 ? &profiles[profile] : NULL;
}

static const mpSeriesProfileDescriptor seriesProfiles[] = {
	{ MP_SERIES_PROFILE_BEST_OF_ONE, "best_of_one", "#str_test", "#str_test",
		1, 1, 32, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE },
	{ MP_SERIES_PROFILE_BEST_OF_THREE, "best_of_three", "#str_test", "#str_test",
		3, 3, 32, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE },
	{ MP_SERIES_PROFILE_BEST_OF_FIVE, "best_of_five", "#str_test", "#str_test",
		5, 5, 32, MP_SERIES_VETO_POLICY_ALTERNATING_COMPLETE }
};

const mpSeriesProfileDescriptor *MPSeriesProfileDescriptorForId(
	mpSeriesProfileId_t profile) {
	return profile >= MP_SERIES_PROFILE_BEST_OF_ONE &&
		profile < MP_SERIES_PROFILE_COUNT ? &seriesProfiles[profile] : NULL;
}

bool MPMatchProtocolValidateRequest(const mpMatchOperationRequest_t &request,
	mpMatchProtocolError_t *error) {
	if (error != NULL) { error->Clear(); }
	if (rejectProtocol || request.schemaVersion != MP_MATCH_PROTOCOL_SCHEMA_VERSION ||
		request.sessionId == 0 || request.requestId == 0 ||
		request.opcode <= MP_MATCH_OP_INVALID || request.opcode >= MP_MATCH_OP_COUNT ||
		request.expectedSessionRevision == 0 || request.expectedControlRevision == 0 ||
		request.actorSlot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ||
		request.actorBindingGeneration == 0 ||
		request.argumentCount > MP_MATCH_PROTOCOL_MAX_ARGUMENTS ||
		(request.hasParticipantTarget && request.participantTarget == 0) ||
		(request.hasTeamTarget && (request.teamTarget <= MP_MATCH_TEAM_NONE ||
			request.teamTarget >= MP_MATCH_TEAM_COUNT))) {
		if (error != NULL) { error->reason = MP_MATCH_PROTOCOL_REASON_INTERNAL; }
		return false;
	}
	for (int i = 0; i < request.argumentCount; ++i) {
		const mpMatchOperationArgument_t &argument = request.arguments[i];
		if (argument.fieldId == 0 || argument.value.type <= MP_MATCH_VALUE_INVALID ||
			argument.value.type >= MP_MATCH_VALUE_TYPE_COUNT) {
			if (error != NULL) { error->reason = MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE; }
			return false;
		}
		for (int prior = 0; prior < i; ++prior) {
			if (request.arguments[prior].fieldId == argument.fieldId) {
				if (error != NULL) { error->reason = MP_MATCH_PROTOCOL_REASON_DUPLICATE_FIELD; }
				return false;
			}
		}
	}
	return true;
}

static void SetMap(mpMatchViewSeriesMap_t &map, int poolIndex,
	const char *token, mpMatchViewMapDisposition_t disposition,
	unsigned char selectionNumber = 0) {
	Zero(&map, sizeof(map));
	map.poolIndex = static_cast<unsigned char>(poolIndex);
	map.disposition = disposition;
	map.selectedBySide = MP_MATCH_VIEW_SIDE_NONE;
	map.startingGameSide = MP_MATCH_VIEW_SIDE_NONE;
	map.gameSideChosenBy = MP_MATCH_VIEW_SIDE_NONE;
	map.selectionNumber = selectionNumber;
	map.tokenLength = static_cast<unsigned char>(strlen(token));
	memcpy(map.mapToken, token, map.tokenLength + 1u);
}

static mpSessionView GoodView(void) {
	mpSessionView view;
	Zero(&view, sizeof(view));
	view.publicState.schemaVersion = MP_MATCH_VIEW_SCHEMA_VERSION;
	view.publicState.sessionId = 0x123456789ull;
	view.publicState.sessionRevision = 11;
	view.publicState.controlRevision = 21;
	view.publicState.viewRevision = 1;
	view.publicState.lifecycle.phase = WARMUP;
	view.publicState.recipient.participantId = 101;
	view.publicState.recipient.slot = 0;
	view.publicState.recipient.bindingGeneration = 7;
	view.publicState.recipient.side = 0;
	view.publicState.recipient.competitionSide = 0;
	view.publicState.recipient.publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
	view.publicState.recipient.ready = false;
	view.publicState.operationAvailabilityCount = MP_MATCH_OP_COUNT - 1;
	for (int i = 0; i < MP_MATCH_OP_COUNT - 1; ++i) {
		mpMatchViewOperationAvailability_t &entry =
			view.publicState.operationAvailability[i];
		entry.opcode = static_cast<mpMatchOperationOpcode_t>(i + 1);
		entry.available = true;
		entry.reason = MP_MATCH_PROTOCOL_REASON_OK;
		entry.localizationId = static_cast<mpMatchLocalizationId_t>(
			MP_MATCH_LOCALIZATION_OPERATION_BASE + i + 1);
	}

	view.publicState.rosterSummaryCount = 2;
	view.publicState.rosterSummaries[0].side = 0;
	view.publicState.rosterSummaries[0].teamReady = false;
	view.publicState.rosterSummaries[0].locked = false;
	view.publicState.rosterSummaries[1].side = 1;
	view.publicState.rosterSummaries[1].teamReady = true;
	view.publicState.rosterSummaries[1].locked = true;

	view.publicState.participantSummaryCount = 5;
	mpMatchViewParticipantSummary_t &recipient =
		view.publicState.participantSummaries[0];
	recipient.participantId = 101; recipient.slot = 0; recipient.side = 0;
	recipient.connected = true; recipient.human = true; recipient.active = true;
	recipient.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
	mpMatchViewParticipantSummary_t &outgoing =
		view.publicState.participantSummaries[1];
	outgoing.participantId = 102; outgoing.slot = 1; outgoing.side = 0;
	outgoing.connected = true; outgoing.human = true; outgoing.active = true;
	outgoing.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
	mpMatchViewParticipantSummary_t &replacement =
		view.publicState.participantSummaries[2];
	replacement.participantId = 103; replacement.slot = 2;
	replacement.side = MP_MATCH_VIEW_SIDE_NONE;
	replacement.connected = true; replacement.human = true; replacement.active = false;
	replacement.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
	mpMatchViewParticipantSummary_t &bot =
		view.publicState.participantSummaries[3];
	bot.participantId = 104; bot.slot = 3; bot.side = MP_MATCH_VIEW_SIDE_NONE;
	bot.connected = true; bot.human = false; bot.active = false;
	bot.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
	mpMatchViewParticipantSummary_t &bench =
		view.publicState.participantSummaries[4];
	bench.participantId = 105; bench.slot = 4; bench.side = 0;
	bench.connected = true; bench.human = true; bench.active = false;
	bench.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);

	view.rosterSeatCount = 2;
	view.rosterSeats[0].seatIndex = 0;
	view.rosterSeats[0].side = 0;
	view.rosterSeats[0].role = MP_MATCH_VIEW_ROSTER_CAPTAIN;
	view.rosterSeats[0].occupied = true;
	view.rosterSeats[0].participantId = 102;
	view.rosterSeats[0].connected = true;
	view.rosterSeats[0].active = true;
	view.rosterSeats[1].seatIndex = 1;
	view.rosterSeats[1].side = 0;
	view.rosterSeats[1].role = MP_MATCH_VIEW_ROSTER_SUBSTITUTE;
	view.rosterSeats[1].occupied = true;
	view.rosterSeats[1].participantId = 105;
	view.rosterSeats[1].connected = true;
	view.rosterSeats[1].active = false;
	view.invitationCount = 1;
	view.invitations[0].invitationId = 77;
	view.invitations[0].side = 0;
	view.invitations[0].role = MP_MATCH_VIEW_ROSTER_PLAYER;
	view.invitations[0].inviteeParticipantId = 101;
	view.queueEntryCount = 1;
	view.queueEntries[0].participantId = 103;
	view.queueEntries[0].side = MP_MATCH_VIEW_SIDE_NONE;
	view.queueEntries[0].position = 1;
	view.queueEntries[0].state = MP_MATCH_VIEW_QUEUE_WAITING;

	view.publicState.globalProposal.present = true;
	view.publicState.globalProposal.proposalId = 501;
	view.publicState.globalProposal.opcode = MP_MATCH_OP_RESUME_REQUEST;
	view.publicState.globalProposal.scope = MP_MATCH_VIEW_PROPOSAL_GLOBAL;
	view.publicState.globalProposal.side = MP_MATCH_VIEW_SIDE_NONE;
	view.publicState.globalProposal.callerParticipantId = 999;
	view.publicState.globalProposal.recipientEligible = true;
	view.ownSideProposal.present = true;
	view.ownSideProposal.proposalId = 502;
	view.ownSideProposal.opcode = MP_MATCH_OP_RULES_COMMIT;
	view.ownSideProposal.scope = MP_MATCH_VIEW_PROPOSAL_SIDE;
	view.ownSideProposal.side = 0;
	view.ownSideProposal.callerParticipantId = 998;
	view.ownSideProposal.recipientEligible = true;

	view.publicState.committedRules.present = true;
	view.publicState.committedRules.profileId = MP_MATCH_PROFILE_CASUAL;
	view.publicState.committedRules.valueCount = 3;
	view.publicState.committedRules.values[0].fieldId = MP_RULE_GAME_TYPE;
	view.publicState.committedRules.values[0].type = MP_MATCH_VIEW_RULE_ENUM;
	view.publicState.committedRules.values[0].value = GAME_TDM;
	view.publicState.committedRules.values[0].editable = true;
	view.publicState.committedRules.values[1].fieldId = MP_RULE_MANAGED_MATCH;
	view.publicState.committedRules.values[1].type = MP_MATCH_VIEW_RULE_BOOL;
	view.publicState.committedRules.values[1].value = 1;
	view.publicState.committedRules.values[1].editable = true;
	view.publicState.committedRules.values[2].fieldId = MP_RULE_TIME_LIMIT_MINUTES;
	view.publicState.committedRules.values[2].type = MP_MATCH_VIEW_RULE_INTEGER;
	view.publicState.committedRules.values[2].value = 15;
	view.publicState.committedRules.values[2].editable = true;
	view.stagedRules.present = true;
	view.stagedRules.valueCount = 1;
	view.stagedRules.values[0].fieldId = MP_RULE_MANAGED_MATCH;
	view.stagedRules.values[0].type = MP_MATCH_VIEW_RULE_BOOL;
	view.stagedRules.values[0].value = 0;

	view.publicState.series.present = true;
	view.publicState.series.seriesId = 9001;
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_VETO;
	view.publicState.series.revision = 3;
	view.publicState.series.gameType = GAME_TDM;
	view.publicState.series.bestOf = 3;
	view.publicState.series.hasVetoTurn = true;
	view.publicState.series.vetoTurnAction = MP_MATCH_VIEW_VETO_BAN;
	view.publicState.series.vetoTurnSide = 0;
	view.publicState.series.mapPoolCount = 2;
	SetMap(view.publicState.series.mapPool[0], 0, "mp/q4dm1",
		MP_MATCH_VIEW_MAP_AVAILABLE);
	SetMap(view.publicState.series.mapPool[1], 1, "mp/q4dm2",
		MP_MATCH_VIEW_MAP_AVAILABLE);
	view.publicState.series.vetoHistoryCount = 1;
	view.publicState.series.vetoHistory[0].sequenceNumber = 1;
	view.publicState.series.vetoHistory[0].action = MP_MATCH_VIEW_VETO_BAN;
	view.publicState.series.vetoHistory[0].actingSide = 1;
	view.publicState.series.vetoHistory[0].mapPoolIndex = 2;
	view.publicState.series.mapHistoryCount = 1;
	view.publicState.series.mapHistory[0].attemptNumber = 1;
	view.publicState.series.mapHistory[0].mapPoolIndex = 2;
	view.publicState.series.mapHistory[0].outcome = MP_MATCH_VIEW_MAP_DECIDED;
	view.publicState.series.mapHistory[0].winnerSide = 0;

	view.publicState.evidence.evidenceState = MP_MATCH_VIEW_EVIDENCE_CAPTURING;
	view.publicState.evidence.mvdState = MP_MATCH_VIEW_MVD_RECORDING;
	view.publicState.evidence.reportState = MP_MATCH_VIEW_REPORT_PENDING;
	view.publicState.evidence.evidenceRevision = 8;
	view.publicState.evidence.recentEventCount = 2;
	view.publicState.evidence.recentEventKinds[0] =
		MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION;
	view.publicState.evidence.recentEventKinds[1] =
		MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE;
	return view;
}

static const mpMatchOperationArgument_t *Argument(
	const mpMatchOperationRequest_t &request, unsigned char fieldId) {
	for (int i = 0; i < request.argumentCount; ++i) {
		if (request.arguments[i].fieldId == fieldId) { return &request.arguments[i]; }
	}
	return NULL;
}

static bool OnlyArguments(const mpMatchOperationRequest_t &request,
	unsigned char a = 0, unsigned char b = 0, unsigned char c = 0) {
	const unsigned char expected[] = { a, b, c };
	int count = 0;
	for (int i = 0; i < 3; ++i) { if (expected[i] != 0) { ++count; } }
	if (request.argumentCount != count) { return false; }
	for (int i = 0; i < count; ++i) {
		if (request.arguments[i].fieldId != expected[i]) { return false; }
	}
	return true;
}

static int FindTeamRow(const mpMatchControlModel &model,
	mpMatchControlTeamRowKind_t kind, int side,
	mpMatchProtocolParticipantId_t participantId = 0) {
	for (int i = 0; i < model.TeamRowCount(); ++i) {
		const mpMatchControlTeamRow_t *row = model.TeamRow(i);
		if (row->kind == kind && row->side == side &&
			(participantId == 0 || row->participantId == participantId)) { return i; }
	}
	return -1;
}

static int FindReplacement(const mpMatchControlModel &model,
	mpMatchProtocolParticipantId_t participantId) {
	for (int i = 0; i < model.ReplacementRowCount(); ++i) {
		if (model.ReplacementRow(i)->participantId == participantId) { return i; }
	}
	return -1;
}

static int FindTemplate(const mpMatchControlModel &model,
	mpMatchOperationOpcode_t opcode) {
	for (int i = 0; i < model.ProposalTemplateRowCount(); ++i) {
		if (model.ProposalTemplateRow(i)->opcode == opcode) { return i; }
	}
	return -1;
}

static int FindProfile(const mpMatchControlModel &model, mpMatchProfileId_t profile) {
	for (int i = 0; i < model.ProfileRowCount(); ++i) {
		if (model.ProfileRow(i)->profileId == profile) { return i; }
	}
	return -1;
}

static int FindRule(const mpMatchControlModel &model, unsigned char fieldId) {
	for (int i = 0; i < model.RuleRowCount(); ++i) {
		if (model.RuleRow(i)->fieldId == fieldId) { return i; }
	}
	return -1;
}

static int FindMap(const mpMatchControlModel &model, const char *token) {
	for (int i = 0; i < model.SeriesMapRowCount(); ++i) {
		if (strcmp(model.SeriesMapRow(i)->map.mapToken, token) == 0) { return i; }
	}
	return -1;
}

static void ConfigureVeto(mpSessionView &view, mpMatchControlCommand_t command) {
	mpMatchViewSeriesSummary_t &series = view.publicState.series;
	series.hasVetoTurn = true;
	series.vetoTurnSide = 0;
	series.mapPoolCount = 2;
	SetMap(series.mapPool[0], 0, "mp/q4dm1", MP_MATCH_VIEW_MAP_AVAILABLE);
	SetMap(series.mapPool[1], 1, "mp/q4dm2", MP_MATCH_VIEW_MAP_AVAILABLE);
	if (command == MP_MATCH_CONTROL_COMMAND_VETO_PICK) {
		series.vetoTurnAction = MP_MATCH_VIEW_VETO_PICK;
	} else if (command == MP_MATCH_CONTROL_COMMAND_VETO_DECIDER) {
		series.vetoTurnAction = MP_MATCH_VIEW_VETO_DECIDER;
		series.mapPool[0].disposition = MP_MATCH_VIEW_MAP_BANNED;
	} else if (command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE ||
		command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG) {
		series.vetoTurnAction = MP_MATCH_VIEW_VETO_SIDE;
		series.mapPool[0].disposition = MP_MATCH_VIEW_MAP_SELECTED;
		series.mapPool[0].selectedBySide = 0;
		series.mapPool[0].selectionNumber = 2;
		series.mapPool[1].disposition = MP_MATCH_VIEW_MAP_BANNED;
	} else {
		series.vetoTurnAction = MP_MATCH_VIEW_VETO_BAN;
	}
	++view.publicState.viewRevision;
}

static bool VerifyBase(const mpMatchControlModel &model,
	mpMatchControlCommand_t command, unsigned int requestId,
	const mpMatchOperationRequest_t &request) {
	return request.schemaVersion == MP_MATCH_PROTOCOL_SCHEMA_VERSION &&
		request.sessionId == model.SessionId() && request.requestId == requestId &&
		request.opcode == MPMatchControlCommandOpcode(command) &&
		request.expectedSessionRevision == 11 &&
		request.expectedControlRevision == 21 && request.actorSlot == 0 &&
		request.actorBindingGeneration == 7;
}

static bool VerifyShape(mpMatchControlCommand_t command,
	const mpMatchOperationRequest_t &request) {
	const mpMatchOperationArgument_t *argument = NULL;
	switch (command) {
		case MP_MATCH_CONTROL_COMMAND_READY_TOGGLE:
		case MP_MATCH_CONTROL_COMMAND_FORCE_READY:
			argument = Argument(request, MP_MATCH_ARG_ENABLED);
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_ENABLED) && argument != NULL &&
				argument->value.type == MP_MATCH_VALUE_BOOL;
		case MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE:
		case MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE:
			return !request.hasParticipantTarget && request.hasTeamTarget &&
				request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request, MP_MATCH_ARG_ENABLED) &&
				Argument(request, MP_MATCH_ARG_ENABLED)->value.type == MP_MATCH_VALUE_BOOL;
		case MP_MATCH_CONTROL_COMMAND_TIMEOUT:
		case MP_MATCH_CONTROL_COMMAND_FORFEIT:
			return !request.hasParticipantTarget && request.hasTeamTarget &&
				request.teamTarget == MP_MATCH_TEAM_MARINE && OnlyArguments(request);
		case MP_MATCH_CONTROL_COMMAND_TECH_PAUSE:
			argument = Argument(request, MP_MATCH_ARG_REASON);
			return request.hasTeamTarget && request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request, MP_MATCH_ARG_REASON) && argument != NULL &&
				strcmp(argument->value.stringValue, "match_control_technical_pause") == 0;
		case MP_MATCH_CONTROL_COMMAND_ABORT:
			argument = Argument(request, MP_MATCH_ARG_REASON);
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_REASON) && argument != NULL &&
				strcmp(argument->value.stringValue, "match_control_abort") == 0;
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
		case MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE:
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request);
		case MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_MARINE:
		case MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_STROGG:
		case MP_MATCH_CONTROL_COMMAND_TEAM_SPECTATE: {
			mpMatchTeam_t expected = MP_MATCH_TEAM_MARINE;
			if (command == MP_MATCH_CONTROL_COMMAND_TEAM_JOIN_STROGG) {
				expected = MP_MATCH_TEAM_STROGG;
			} else if (command == MP_MATCH_CONTROL_COMMAND_TEAM_SPECTATE) {
				expected = MP_MATCH_TEAM_SPECTATOR;
			}
			return !request.hasParticipantTarget && request.hasTeamTarget &&
				request.teamTarget == expected && OnlyArguments(request);
		}
		case MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT:
			argument = Argument(request, MP_MATCH_ARG_INVITATION_ID);
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_INVITATION_ID) && argument != NULL &&
				argument->value.type == MP_MATCH_VALUE_UINT32 &&
				argument->value.unsignedValue == 77;
		case MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE:
			return request.hasParticipantTarget && request.participantTarget == 103 &&
				request.hasTeamTarget && request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request, MP_MATCH_ARG_ROLE) &&
				Argument(request, MP_MATCH_ARG_ROLE)->value.enumValue ==
					MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH;
		case MP_MATCH_CONTROL_COMMAND_ROSTER_REMOVE:
			return request.hasParticipantTarget && request.participantTarget == 102 &&
				request.hasTeamTarget && request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request);
		case MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE:
			argument = Argument(request, MP_MATCH_ARG_REPLACEMENT_PARTICIPANT);
			return request.hasParticipantTarget && request.participantTarget == 102 &&
				request.hasTeamTarget && request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request, MP_MATCH_ARG_REPLACEMENT_PARTICIPANT) &&
				argument != NULL && argument->value.type == MP_MATCH_VALUE_PARTICIPANT_ID &&
				argument->value.unsignedValue == 103 &&
				Argument(request, MP_MATCH_ARG_ROLE) == NULL;
		case MP_MATCH_CONTROL_COMMAND_ROLE_ASSIGN:
			return request.hasParticipantTarget && request.participantTarget == 102 &&
				request.hasTeamTarget && request.teamTarget == MP_MATCH_TEAM_MARINE &&
				OnlyArguments(request, MP_MATCH_ARG_ROLE) &&
				Argument(request, MP_MATCH_ARG_ROLE)->value.enumValue ==
					MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH;
		case MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET:
			return request.hasParticipantTarget && request.participantTarget == 103 &&
				!request.hasTeamTarget && OnlyArguments(request, MP_MATCH_ARG_ENABLED) &&
				Argument(request, MP_MATCH_ARG_ENABLED)->value.type == MP_MATCH_VALUE_BOOL &&
				Argument(request, MP_MATCH_ARG_ENABLED)->value.unsignedValue == 1;
		case MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE:
			return request.hasParticipantTarget && request.participantTarget == 102 &&
				!request.hasTeamTarget && OnlyArguments(request);
		case MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND:
			return request.hasParticipantTarget && request.participantTarget == 102 &&
				!request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_COMPETITION_SIDE) &&
				Argument(request, MP_MATCH_ARG_COMPETITION_SIDE)->value.type ==
					MP_MATCH_VALUE_ENUM &&
				Argument(request, MP_MATCH_ARG_COMPETITION_SIDE)->value.enumValue ==
					MP_MATCH_COMPETITION_SIDE_B;
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE:
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_PROPOSED_OPCODE) &&
				Argument(request, MP_MATCH_ARG_PROPOSED_OPCODE)->value.enumValue ==
					MP_MATCH_OP_RESUME_REQUEST;
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES:
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO:
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN: {
			unsigned short expected = MP_MATCH_BALLOT_YES;
			if (command == MP_MATCH_CONTROL_COMMAND_PROPOSAL_NO) {
				expected = MP_MATCH_BALLOT_NO;
			} else if (command == MP_MATCH_CONTROL_COMMAND_PROPOSAL_ABSTAIN) {
				expected = MP_MATCH_BALLOT_ABSTAIN;
			}
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_PROPOSAL_ID,
					MP_MATCH_ARG_BALLOT_CHOICE) &&
				Argument(request, MP_MATCH_ARG_PROPOSAL_ID)->value.unsignedValue == 501 &&
				Argument(request, MP_MATCH_ARG_BALLOT_CHOICE)->value.enumValue == expected;
		}
		case MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL:
			return !request.hasParticipantTarget && !request.hasTeamTarget &&
				OnlyArguments(request, MP_MATCH_ARG_PROPOSAL_ID) &&
				Argument(request, MP_MATCH_ARG_PROPOSAL_ID)->value.unsignedValue == 501;
		case MP_MATCH_CONTROL_COMMAND_RULES_SELECT_PROFILE:
			return OnlyArguments(request, MP_MATCH_ARG_PROFILE) &&
				strcmp(Argument(request, MP_MATCH_ARG_PROFILE)->value.stringValue,
					"competitive_tdm") == 0;
		case MP_MATCH_CONTROL_COMMAND_RULES_STAGE_FIELD:
			return OnlyArguments(request, MP_MATCH_ARG_SETTING_ID,
				MP_MATCH_ARG_SETTING_VALUE) &&
				strcmp(Argument(request, MP_MATCH_ARG_SETTING_ID)->value.stringValue,
					"managed_match") == 0 &&
				Argument(request, MP_MATCH_ARG_SETTING_VALUE)->value.type ==
					MP_MATCH_VALUE_BOOL &&
				Argument(request, MP_MATCH_ARG_SETTING_VALUE)->value.unsignedValue == 1;
		case MP_MATCH_CONTROL_COMMAND_SERIES_STAGE:
			return OnlyArguments(request, MP_MATCH_ARG_SERIES_PROFILE,
				MP_MATCH_ARG_BEST_OF) &&
				strcmp(Argument(request, MP_MATCH_ARG_SERIES_PROFILE)->value.stringValue,
					"best_of_three") == 0 &&
				Argument(request, MP_MATCH_ARG_BEST_OF)->value.unsignedValue == 3;
		case MP_MATCH_CONTROL_COMMAND_VETO_BAN:
		case MP_MATCH_CONTROL_COMMAND_VETO_PICK:
		case MP_MATCH_CONTROL_COMMAND_VETO_DECIDER:
		case MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE:
		case MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG: {
			unsigned short action = MP_MATCH_VETO_BAN;
			if (command == MP_MATCH_CONTROL_COMMAND_VETO_PICK) { action = MP_MATCH_VETO_PICK; }
			else if (command == MP_MATCH_CONTROL_COMMAND_VETO_DECIDER) {
				action = MP_MATCH_VETO_DECIDER;
			} else if (command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE ||
				command == MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG) {
				action = MP_MATCH_VETO_SIDE;
			}
			const bool side = action == MP_MATCH_VETO_SIDE;
			const char *map = command == MP_MATCH_CONTROL_COMMAND_VETO_DECIDER ?
				"mp/q4dm2" : "mp/q4dm1";
			if (!OnlyArguments(request, MP_MATCH_ARG_VETO_ACTION,
				MP_MATCH_ARG_MAP_TOKEN, side ? MP_MATCH_ARG_STARTING_SIDE : 0) ||
				Argument(request, MP_MATCH_ARG_VETO_ACTION)->value.enumValue != action ||
				strcmp(Argument(request, MP_MATCH_ARG_MAP_TOKEN)->value.stringValue, map) != 0) {
				return false;
			}
			if (!side) { return Argument(request, MP_MATCH_ARG_STARTING_SIDE) == NULL; }
			const unsigned short expected = command ==
				MP_MATCH_CONTROL_COMMAND_VETO_SIDE_MARINE ?
				MP_MATCH_STARTING_SIDE_MARINE : MP_MATCH_STARTING_SIDE_STROGG;
			return Argument(request, MP_MATCH_ARG_STARTING_SIDE)->value.enumValue == expected;
		}
		default:
			return false;
	}
}

int main(void) {
	mpSessionView view = GoodView();
	mpMatchControlModel model;
	mpMatchControlError_t error;
	CHECK(model.IngestAcceptedView(view, &error) ==
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION);
	CHECK(model.IsReady() && model.SessionId() == view.publicState.sessionId);
	CHECK(model.TeamRowCount() == 11);
	CHECK(model.ReplacementRowCount() == 3);
	CHECK(FindReplacement(model, 103) >= 0 && FindReplacement(model, 104) < 0 &&
		FindReplacement(model, 102) < 0);
	CHECK(FindReplacement(model, 105) >= 0 &&
		model.ReplacementRow(FindReplacement(model, 105))->rostered &&
		model.ReplacementRow(FindReplacement(model, 105))->rosterRole ==
			MP_MATCH_VIEW_ROSTER_SUBSTITUTE);
	CHECK(model.ProposalTemplateRowCount() == 7);
	CHECK(model.ProfileRowCount() == 2 && model.RuleRowCount() == 3);
	CHECK(model.SeriesMapRowCount() == 2 && model.SeriesHistoryRowCount() == 2);
	CHECK(model.EvidenceRowCount() == 3);
	CHECK(model.EvidenceRow(0)->kind == MP_MATCH_CONTROL_EVIDENCE_SUMMARY);
	CHECK(model.EvidenceRow(2)->recentEventKind ==
		MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE);

	const int side0 = FindTeamRow(model, MP_MATCH_CONTROL_TEAM_ROW_SIDE, 0);
	const int invitation = FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_INVITATION, 0, 101);
	const int outgoing = FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_ROSTER_SEAT, 0, 102);
	const int outgoingParticipant = FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, 0, 102);
	const int broadcasterTarget = FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, MP_MATCH_VIEW_SIDE_NONE, 103);
	const int replacement = FindReplacement(model, 103);
	const int competitiveProfile = FindProfile(model, MP_MATCH_PROFILE_COMPETITIVE_TDM);
	const int managedRuleIndex = FindRule(model, MP_RULE_MANAGED_MATCH);
	CHECK(side0 >= 0 && invitation >= 0 && outgoing >= 0 && outgoingParticipant >= 0 &&
		broadcasterTarget >= 0 && replacement >= 0 &&
		competitiveProfile >= 0 && managedRuleIndex >= 0);
	CHECK(model.SetRoleChoice(MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH));
	CHECK(!model.SetRoleChoice(static_cast<mpMatchProtocolRosterRole_t>(99)));
	CHECK(model.SetSeriesProfileChoice(MP_SERIES_PROFILE_BEST_OF_THREE));
	CHECK(model.SelectProfileRow(competitiveProfile));
	CHECK(model.SelectRuleRow(managedRuleIndex));
	CHECK(model.SetSelectedRuleValue(1));
	CHECK(!model.SetSelectedRuleValue(2));

	bool exercised[MP_MATCH_CONTROL_COMMAND_COUNT];
	Zero(exercised, sizeof(exercised));
	unsigned int requestId = 100;
	for (int raw = MP_MATCH_CONTROL_COMMAND_READY_TOGGLE;
		raw < MP_MATCH_CONTROL_COMMAND_COUNT; ++raw) {
		const mpMatchControlCommand_t command =
			static_cast<mpMatchControlCommand_t>(raw);
		mpMatchControlCommand_t parsed = MP_MATCH_CONTROL_COMMAND_INVALID;
		const char *token = MPMatchControlCommandToken(command);
		CHECK(token != NULL && MPMatchControlCommandFromToken(token, parsed) &&
			parsed == command);
		CHECK(model.CommandAvailability(command) != NULL &&
			model.CommandAvailability(command)->available);

		if (command == MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE ||
			command == MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE) {
			CHECK(model.SelectTeamRow(side0));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT) {
			CHECK(model.SelectTeamRow(invitation));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_ROSTER_REMOVE ||
			command == MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE ||
			command == MP_MATCH_CONTROL_COMMAND_ROLE_ASSIGN) {
			CHECK(model.SelectTeamRow(outgoing));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET) {
			CHECK(model.SelectTeamRow(broadcasterTarget));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE) {
			CHECK(model.SelectTeamRow(outgoingParticipant));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE ||
			command == MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE) {
			CHECK(model.SelectReplacementRow(replacement));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE) {
			CHECK(model.SelectProposalTemplateRow(FindTemplate(model,
				MP_MATCH_OP_RESUME_REQUEST)));
		}
		if (command == MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE) {
			view.publicState.lifecycle.phase = WARMUP;
			view.publicState.series.state = MP_MATCH_VIEW_SERIES_READY;
			++view.publicState.viewRevision;
			CHECK(model.IngestAcceptedView(view, &error) ==
				MP_MATCH_CONTROL_INGEST_UPDATED);
		}
		if (command == MP_MATCH_CONTROL_COMMAND_SERIES_CONTESTANT_BIND) {
			view.publicState.lifecycle.phase = WARMUP;
			view.publicState.series.gameType = GAME_DUEL;
			view.publicState.series.state = MP_MATCH_VIEW_SERIES_SETUP;
			++view.publicState.viewRevision;
			CHECK(model.IngestAcceptedView(view, &error) ==
				MP_MATCH_CONTROL_INGEST_UPDATED);
			CHECK(model.SelectTeamRow(FindTeamRow(model,
				MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, 0, 102)));
			CHECK(model.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_ONE));
		}
		if (command >= MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES &&
			command <= MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL) {
			CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_GLOBAL));
		}
		if (command >= MP_MATCH_CONTROL_COMMAND_VETO_BAN &&
			command <= MP_MATCH_CONTROL_COMMAND_VETO_SIDE_STROGG) {
			ConfigureVeto(view, command);
			CHECK(model.IngestAcceptedView(view, &error) ==
				MP_MATCH_CONTROL_INGEST_UPDATED);
			const char *map = command == MP_MATCH_CONTROL_COMMAND_VETO_DECIDER ?
				"mp/q4dm2" : "mp/q4dm1";
			CHECK(model.SelectSeriesMapRow(FindMap(model, map)));
		}

		mpMatchOperationRequest_t request;
		request.Clear();
		++requestId;
		CHECK(model.BuildRequest(command, requestId, request, &error));
		CHECK(error.reason == MP_MATCH_CONTROL_ERROR_NONE);
		CHECK(VerifyBase(model, command, requestId, request));
		CHECK(VerifyShape(command, request));
		exercised[raw] = true;
	}
	for (int raw = MP_MATCH_CONTROL_COMMAND_READY_TOGGLE;
		raw < MP_MATCH_CONTROL_COMMAND_COUNT; ++raw) { CHECK(exercised[raw]); }
	mpMatchControlCommand_t invalidCommand = MP_MATCH_CONTROL_COMMAND_READY_TOGGLE;
	CHECK(!MPMatchControlCommandFromToken("select_team_row", invalidCommand));
	CHECK(invalidCommand == MP_MATCH_CONTROL_COMMAND_INVALID);

	// The six global proposal recipes are typed and retain a global ballot. Removal
	// additionally carries one stable participant target.
	const mpMatchOperationOpcode_t recipes[] = {
		MP_MATCH_OP_RESUME_REQUEST, MP_MATCH_OP_RULES_SELECT_PROFILE,
		MP_MATCH_OP_RULES_STAGE_FIELD, MP_MATCH_OP_RULES_COMMIT, MP_MATCH_OP_ABORT,
		MP_MATCH_OP_PARTICIPANT_REMOVE
	};
	for (int i = 0; i < 6; ++i) {
		CHECK(model.SelectProposalTemplateRow(FindTemplate(model, recipes[i])));
		if (recipes[i] == MP_MATCH_OP_PARTICIPANT_REMOVE) {
			CHECK(model.SelectTeamRow(FindTeamRow(model,
				MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, 0, 102)));
		}
		CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_OWN_SIDE));
		mpMatchOperationRequest_t request; request.Clear();
		CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
			++requestId, request, &error));
		CHECK(!request.hasTeamTarget);
		CHECK(request.hasParticipantTarget ==
			(recipes[i] == MP_MATCH_OP_PARTICIPANT_REMOVE));
		if (request.hasParticipantTarget) {
			CHECK(request.participantTarget == 102);
		}
		CHECK(Argument(request, MP_MATCH_ARG_PROPOSED_OPCODE)->value.enumValue == recipes[i]);
		if (recipes[i] == MP_MATCH_OP_RULES_SELECT_PROFILE) {
			CHECK(Argument(request, static_cast<unsigned char>(
				MP_MATCH_NESTED_ARGUMENT_BASE + MP_MATCH_ARG_PROFILE)) != NULL);
		} else if (recipes[i] == MP_MATCH_OP_RULES_STAGE_FIELD) {
			CHECK(Argument(request, static_cast<unsigned char>(
				MP_MATCH_NESTED_ARGUMENT_BASE + MP_MATCH_ARG_SETTING_ID)) != NULL);
			CHECK(Argument(request, static_cast<unsigned char>(
				MP_MATCH_NESTED_ARGUMENT_BASE + MP_MATCH_ARG_SETTING_VALUE)) != NULL);
		} else if (recipes[i] == MP_MATCH_OP_ABORT) {
			CHECK(Argument(request, static_cast<unsigned char>(
				MP_MATCH_NESTED_ARGUMENT_BASE + MP_MATCH_ARG_REASON)) != NULL);
		} else {
			CHECK(request.argumentCount == 1);
		}
	}

	// The seventh recipe is the one side ballot: a team forfeit vote. Its scope
	// and team target are always the recipient's own side, whatever the action
	// selector says, and it carries no participant target or nested reason.
	CHECK(model.ProposalTemplateRowCount() == 7);
	CHECK(model.SelectProposalTemplateRow(FindTemplate(model, MP_MATCH_OP_FORFEIT)));
	CHECK(!model.ProposalTemplateRow(model.SelectedProposalTemplateRow())->globalOnly);
	CHECK(model.ProposalTemplateRow(model.SelectedProposalTemplateRow())->targetKind ==
		MP_MATCH_CONTROL_PROPOSAL_TARGET_OWN_SIDE);
	for (int choice = 0; choice < 3; ++choice) {
		const mpMatchControlSideChoice_t sideChoice = choice == 0 ?
			MP_MATCH_CONTROL_SIDE_CHOICE_NONE : (choice == 1 ?
				MP_MATCH_CONTROL_SIDE_CHOICE_ZERO : MP_MATCH_CONTROL_SIDE_CHOICE_ONE);
		model.SetActionSideChoice(sideChoice);
		mpMatchOperationRequest_t sideBallot; sideBallot.Clear();
		CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
			++requestId, sideBallot, &error));
		CHECK(sideBallot.hasTeamTarget && sideBallot.teamTarget == MP_MATCH_TEAM_MARINE);
		CHECK(!sideBallot.hasParticipantTarget && sideBallot.argumentCount == 1);
		CHECK(Argument(sideBallot, MP_MATCH_ARG_PROPOSED_OPCODE)->value.enumValue ==
			MP_MATCH_OP_FORFEIT);
	}
	CHECK(model.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_NONE));

	// Removal never falls back to the selected row's transport slot, and every
	// failed target/series-state preflight leaves caller output byte-identical.
	mpMatchOperationRequest_t guarded; memset(&guarded, 0x5a, sizeof(guarded));
	const mpMatchOperationRequest_t guardedBefore = guarded;
	CHECK(model.SelectTeamRow(FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, 0, 101)));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE,
		++requestId, guarded, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED &&
		memcmp(&guarded, &guardedBefore, sizeof(guarded)) == 0);
	view.publicState.lifecycle.phase = GAMEREVIEW;
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_READY;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(!model.OperationContextAccepted(MP_MATCH_OP_SERIES_ADVANCE));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE,
		++requestId, guarded, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_INVALID &&
		memcmp(&guarded, &guardedBefore, sizeof(guarded)) == 0);
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_MAP_COMPLETE;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.OperationContextAccepted(MP_MATCH_OP_SERIES_ADVANCE));
	mpMatchOperationRequest_t reviewAdvance; reviewAdvance.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE,
		++requestId, reviewAdvance, &error));
	CHECK(!reviewAdvance.hasParticipantTarget && !reviewAdvance.hasTeamTarget &&
		reviewAdvance.argumentCount == 0);

	// A restored completed map is in warmup. Only the server's accepted
	// availability can authorize its continuation, and never during live play.
	for (int phase : {WARMUP, COUNTDOWN, GAMEON, GAMEREVIEW, NEXTGAME}) {
		for (bool permitted : {false, true}) {
			view.publicState.lifecycle.phase = static_cast<mpGameState_t>(phase);
			mpMatchViewOperationAvailability_t &advanceAvailability =
				view.publicState.operationAvailability[MP_MATCH_OP_SERIES_ADVANCE - 1];
			advanceAvailability.available = permitted;
			advanceAvailability.reason = permitted ? MP_MATCH_PROTOCOL_REASON_OK :
				MP_MATCH_PROTOCOL_REASON_CONFLICT;
			++view.publicState.viewRevision;
			CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
			CHECK(model.OperationContextAccepted(MP_MATCH_OP_SERIES_ADVANCE) ==
				(phase == GAMEREVIEW || (phase == WARMUP && permitted)));
			guarded = guardedBefore;
			const bool accepted = model.BuildRequest(MP_MATCH_CONTROL_COMMAND_SERIES_ADVANCE,
				++requestId, guarded, &error);
			CHECK(accepted == (permitted && (phase == WARMUP || phase == GAMEREVIEW)));
			if (!accepted) CHECK(memcmp(&guarded, &guardedBefore, sizeof(guarded)) == 0);
		}
	}

	// Duel binding has a deliberately narrower presentation boundary than the
	// generic series-management capability and cannot leak into other modes,
	// terminal series states, or review.
	view.publicState.lifecycle.phase = WARMUP;
	view.publicState.series.gameType = GAME_DUEL;
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_SETUP;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.OperationContextAccepted(MP_MATCH_OP_SERIES_CONTESTANT_BIND));
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_COMPLETE;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(!model.OperationContextAccepted(MP_MATCH_OP_SERIES_CONTESTANT_BIND));
	view.publicState.series.state = MP_MATCH_VIEW_SERIES_SETUP;
	view.publicState.series.gameType = GAME_DM;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(!model.OperationContextAccepted(MP_MATCH_OP_SERIES_CONTESTANT_BIND));
	view.publicState.series.gameType = GAME_DUEL;
	view.publicState.lifecycle.phase = GAMEREVIEW;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(!model.OperationContextAccepted(MP_MATCH_OP_SERIES_CONTESTANT_BIND));

	// Ballot scope chooses an existing proposal id only; it never serializes scope.
	CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_OWN_SIDE));
	mpMatchOperationRequest_t ownSideVote; ownSideVote.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_YES,
		++requestId, ownSideVote, &error));
	CHECK(Argument(ownSideVote, MP_MATCH_ARG_PROPOSAL_ID)->value.unsignedValue == 502);
	CHECK(!ownSideVote.hasTeamTarget && ownSideVote.argumentCount == 2);

	// Rule values retain their wire type, including integer fields.
	CHECK(model.SelectRuleRow(FindRule(model, MP_RULE_TIME_LIMIT_MINUTES)));
	CHECK(model.SetSelectedRuleValue(20));
	mpMatchOperationRequest_t integerRule; integerRule.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_RULES_STAGE_FIELD,
		++requestId, integerRule, &error));
	CHECK(Argument(integerRule, MP_MATCH_ARG_SETTING_VALUE)->value.type ==
		MP_MATCH_VALUE_INT32);
	CHECK(Argument(integerRule, MP_MATCH_ARG_SETTING_VALUE)->value.signedValue == 20);

	// Invite and substitution share a bounded candidate superset but enforce
	// their different target semantics when the request is built.
	const int selfCandidate = FindReplacement(model, 101);
	const int benchCandidate = FindReplacement(model, 105);
	CHECK(selfCandidate >= 0 && benchCandidate >= 0);
	CHECK(model.SelectTeamRow(outgoing));
	CHECK(model.SelectReplacementRow(benchCandidate));
	mpMatchOperationRequest_t benchSubstitution; benchSubstitution.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE,
		++requestId, benchSubstitution, &error));
	CHECK(Argument(benchSubstitution,
		MP_MATCH_ARG_REPLACEMENT_PARTICIPANT)->value.unsignedValue == 105);
	CHECK(Argument(benchSubstitution, MP_MATCH_ARG_ROLE) == NULL);
	CHECK(model.SelectReplacementRow(selfCandidate));
	mpMatchOperationRequest_t rejectedCandidate; rejectedCandidate.Clear();
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_ROSTER_SUBSTITUTE,
		++requestId, rejectedCandidate, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED);
	CHECK(model.SelectTeamRow(side0));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_ROSTER_INVITE,
		++requestId, rejectedCandidate, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED);
	CHECK(model.SelectReplacementRow(replacement));

	// A Duel contestant has no gameplay team. Forfeit and timeout use the
	// server-accepted competition side without granting team-management powers.
	mpMatchViewOperationAvailability_t &duelOperatorAuthority =
		view.publicState.operationAvailability[MP_MATCH_OP_BROADCASTER_SET - 1];
	duelOperatorAuthority.available = false;
	duelOperatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	CHECK(model.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_NONE));
	view.publicState.recipient.side = MP_MATCH_VIEW_SIDE_NONE;
	view.publicState.recipient.competitionSide = 1;
	view.publicState.participantSummaries[0].side = MP_MATCH_VIEW_SIDE_NONE;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	mpMatchOperationRequest_t duelForfeit; duelForfeit.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_FORFEIT,
		++requestId, duelForfeit, &error));
	CHECK(duelForfeit.hasTeamTarget &&
		duelForfeit.teamTarget == MP_MATCH_TEAM_STROGG);
	mpMatchOperationRequest_t duelTimeout; duelTimeout.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_TIMEOUT,
		++requestId, duelTimeout, &error));
	CHECK(duelTimeout.hasTeamTarget && duelTimeout.teamTarget == MP_MATCH_TEAM_STROGG);
	CHECK(model.ActionSideUsesCompetitionLabels());
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE,
		++requestId, duelTimeout, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE);
	view.publicState.recipient.side = 0;
	view.publicState.recipient.competitionSide = 0;
	view.publicState.participantSummaries[0].side = 0;
	duelOperatorAuthority.available = true;
	duelOperatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_OK;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);

	// Broadcaster state is derived only from the selected participant's typed
	// role mask; the request never parses a row label or display name.
	view.publicState.participantSummaries[2].publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_BROADCASTER);
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectTeamRow(FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, MP_MATCH_VIEW_SIDE_NONE, 103)));
	mpMatchOperationRequest_t revokeBroadcaster; revokeBroadcaster.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET,
		++requestId, revokeBroadcaster, &error));
	CHECK(Argument(revokeBroadcaster, MP_MATCH_ARG_ENABLED)->value.unsignedValue == 0);
	view.publicState.participantSummaries[2].publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);

	// Occupied private roster rows must agree with their public participant
	// record before the model will retain them as actionable selections.
	mpSessionView inconsistent = view;
	inconsistent.rosterSeats[0].active = false;
	mpMatchControlModel rejectedModel;
	CHECK(rejectedModel.IngestAcceptedView(inconsistent, &error) ==
		MP_MATCH_CONTROL_INGEST_REJECTED);
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_VIEW &&
		!rejectedModel.IsReady());

	// Same-session refresh preserves selections by typed stable identity.
	CHECK(model.SelectTeamRow(outgoing));
	CHECK(model.SelectReplacementRow(replacement));
	CHECK(model.SelectProposalTemplateRow(FindTemplate(model, MP_MATCH_OP_ABORT)));
	CHECK(model.SelectProfileRow(competitiveProfile));
	CHECK(model.SelectRuleRow(FindRule(model, MP_RULE_TIME_LIMIT_MINUTES)));
	CHECK(model.SetSelectedRuleValue(22));
	CHECK(model.SelectSeriesMapRow(FindMap(model, "mp/q4dm1")));
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.TeamRow(model.SelectedTeamRow())->participantId == 102);
	CHECK(model.ReplacementRow(model.SelectedReplacementRow())->participantId == 103);
	CHECK(model.ProposalTemplateRow(model.SelectedProposalTemplateRow())->opcode ==
		MP_MATCH_OP_ABORT);
	CHECK(model.ProfileRow(model.SelectedProfileRow())->profileId ==
		MP_MATCH_PROFILE_COMPETITIVE_TDM);
	CHECK(model.RuleRow(model.SelectedRuleRow())->fieldId == MP_RULE_TIME_LIMIT_MINUTES);
	CHECK(model.RuleRow(model.SelectedRuleRow())->editValue == 22);
	CHECK(strcmp(model.SeriesMapRow(model.SelectedSeriesMapRow())->map.mapToken,
		"mp/q4dm1") == 0);

	// Stale and invalid inputs cannot mutate an accepted model.
	const mpMatchProtocolRevision_t acceptedRevision = model.ViewRevision();
	mpSessionView stale = view;
	--stale.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(stale, &error) == MP_MATCH_CONTROL_INGEST_REJECTED);
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_STALE_VIEW &&
		model.ViewRevision() == acceptedRevision);
	rejectView = true;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_REJECTED);
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_VIEW &&
		model.ViewRevision() == acceptedRevision);
	rejectView = false;

	// Failures preserve the caller's output exactly.
	mpMatchOperationRequest_t sentinel;
	memset(&sentinel, 0x5a, sizeof(sentinel));
	mpMatchOperationRequest_t before = sentinel;
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_RESUME, 0, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_REQUEST_ID &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	rejectProtocol = true;
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_RESUME,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_PROTOCOL_INVALID &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	rejectProtocol = false;

	// Exact projected availability is authoritative and its denial is retained.
	mpMatchViewOperationAvailability_t &ready =
		view.publicState.operationAvailability[MP_MATCH_OP_READY_SET - 1];
	ready.available = false;
	ready.reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	ready.fieldId = MP_MATCH_ARG_ENABLED;
	ready.detail = 44;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_READY_TOGGLE,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE &&
		error.protocolReason == MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED &&
		error.fieldId == MP_MATCH_ARG_ENABLED && error.detail == 44 &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	ready.available = true; ready.reason = MP_MATCH_PROTOCOL_REASON_OK;
	ready.fieldId = 0; ready.detail = 0; ++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);

	// A selected row must have the exact semantic kind required by the operation.
	CHECK(model.SelectTeamRow(side0));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_ROSTER_ACCEPT,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	// Side-scoped management and cancellation reject another side/proposer for
	// an ordinary captain. The operator-only broadcaster availability is the
	// exact, non-disclosing proof that a local operator may override both.
	mpMatchViewOperationAvailability_t &operatorAuthority =
		view.publicState.operationAvailability[MP_MATCH_OP_BROADCASTER_SET - 1];
	operatorAuthority.available = false;
	operatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	view.publicState.globalProposal.callerParticipantId = 999;
	view.publicState.recipient.publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectTeamRow(FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT, MP_MATCH_VIEW_SIDE_NONE, 103)));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_BROADCASTER_SET,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_OPERATION_UNAVAILABLE &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	const int side1 = FindTeamRow(model, MP_MATCH_CONTROL_TEAM_ROW_SIDE, 1);
	CHECK(side1 >= 0 && model.SelectTeamRow(side1));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_GLOBAL));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_INVALID &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	view.publicState.globalProposal.callerParticipantId = 101;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	mpMatchOperationRequest_t proposerCancel; proposerCancel.Clear();
	CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_GLOBAL));
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL,
		++requestId, proposerCancel, &error));
	view.publicState.globalProposal.callerParticipantId = 999;
	operatorAuthority.available = true;
	operatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_OK;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectTeamRow(FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_SIDE, 1)));
	mpMatchOperationRequest_t operatorLock; operatorLock.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE,
		++requestId, operatorLock, &error));
	CHECK(operatorLock.teamTarget == MP_MATCH_TEAM_STROGG);
	mpMatchOperationRequest_t moderatorCancel; moderatorCancel.Clear();
	CHECK(model.SetProposalChoice(MP_MATCH_CONTROL_PROPOSAL_GLOBAL));
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CANCEL,
		++requestId, moderatorCancel, &error));

	// Competition-side enforcement is strict for contestants but permits the
	// local-operator override represented by that exact availability decision.
	ConfigureVeto(view, MP_MATCH_CONTROL_COMMAND_VETO_BAN);
	view.publicState.recipient.competitionSide = 1;
	operatorAuthority.available = false;
	operatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectSeriesMapRow(FindMap(model, "mp/q4dm1")));
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_VETO_BAN,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	operatorAuthority.available = true;
	operatorAuthority.reason = MP_MATCH_PROTOCOL_REASON_OK;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectSeriesMapRow(FindMap(model, "mp/q4dm1")));
	mpMatchOperationRequest_t operatorVeto; operatorVeto.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_VETO_BAN,
		++requestId, operatorVeto, &error));

	// Protocol evolution cannot silently reinterpret proposal scope.
	allowTeamTargetOnTemplates = true;
	++view.publicState.viewRevision;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.SelectProposalTemplateRow(FindTemplate(model,
		MP_MATCH_OP_RESUME_REQUEST)));
	CHECK(!model.ProposalTemplateRow(model.SelectedProposalTemplateRow())->globalOnly);
	CHECK(!model.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
		++requestId, sentinel, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_INVALID &&
		memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	allowTeamTargetOnTemplates = false;

	// A transport rebind in the same session is a new recipient context even
	// when the view revision is unchanged; no prior actionable selection leaks.
	CHECK(model.SelectTeamRow(FindTeamRow(model,
		MP_MATCH_CONTROL_TEAM_ROW_SIDE, 1)));
	view.publicState.recipient.bindingGeneration = 8;
	CHECK(model.IngestAcceptedView(view, &error) == MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(model.Recipient().bindingGeneration == 8);
	CHECK(model.TeamRow(model.SelectedTeamRow())->kind ==
		MP_MATCH_CONTROL_TEAM_ROW_PARTICIPANT);
	CHECK(model.TeamRow(model.SelectedTeamRow())->participantId == 101);
	mpMatchOperationRequest_t rebound; rebound.Clear();
	CHECK(model.BuildRequest(MP_MATCH_CONTROL_COMMAND_READY_TOGGLE,
		++requestId, rebound, &error));
	CHECK(rebound.actorBindingGeneration == 8);

	// A neutral referee has no implicit side. Every side-scoped control remains
	// closed until A/Marine or B/Strogg is chosen explicitly, after which the
	// same bounded selector drives ready, timeout, lock and forfeit coherently.
	mpSessionView neutralView = GoodView();
	neutralView.publicState.recipient.side = MP_MATCH_VIEW_SIDE_NONE;
	neutralView.publicState.recipient.competitionSide = MP_MATCH_VIEW_SIDE_NONE;
	neutralView.publicState.recipient.publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_REFEREE);
	mpMatchControlModel neutralModel;
	CHECK(neutralModel.IngestAcceptedView(neutralView, &error) ==
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION);
	CHECK(neutralModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_NONE);
	CHECK(neutralModel.CanChooseActionSide(0));
	CHECK(neutralModel.CanChooseActionSide(1));
	CHECK(!neutralModel.CanChooseActionSide(-1));
	const mpMatchControlCommand_t sideCommands[] = {
		MP_MATCH_CONTROL_COMMAND_TEAM_READY_TOGGLE,
		MP_MATCH_CONTROL_COMMAND_TIMEOUT,
		MP_MATCH_CONTROL_COMMAND_TEAM_LOCK_TOGGLE,
		MP_MATCH_CONTROL_COMMAND_FORFEIT
	};
	for (int commandIndex = 0; commandIndex < 4; ++commandIndex) {
		mpMatchOperationRequest_t missing; missing.Clear();
		CHECK(!neutralModel.BuildRequest(sideCommands[commandIndex],
			++requestId, missing, &error));
		CHECK(error.reason == MP_MATCH_CONTROL_ERROR_SELECTION_REQUIRED);
	}
	for (int side = 0; side < MP_MATCH_VIEW_SIDE_COUNT; ++side) {
		CHECK(neutralModel.SetActionSideChoice(
			static_cast<mpMatchControlSideChoice_t>(side)));
		for (int commandIndex = 0; commandIndex < 4; ++commandIndex) {
			mpMatchOperationRequest_t targeted; targeted.Clear();
			CHECK(neutralModel.BuildRequest(sideCommands[commandIndex],
				++requestId, targeted, &error));
			CHECK(targeted.hasTeamTarget && targeted.teamTarget ==
				(side == 0 ? MP_MATCH_TEAM_MARINE : MP_MATCH_TEAM_STROGG));
		}
		// Choosing a side for direct authority never lets a referee call that
		// side's own vote.
		CHECK(neutralModel.SelectProposalTemplateRow(FindTemplate(neutralModel,
			MP_MATCH_OP_FORFEIT)));
		mpMatchOperationRequest_t refereeSideBallot;
		memset(&refereeSideBallot, 0x5a, sizeof(refereeSideBallot));
		const mpMatchOperationRequest_t refereeSideBallotBefore = refereeSideBallot;
		CHECK(!neutralModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
			++requestId, refereeSideBallot, &error));
		CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE &&
			memcmp(&refereeSideBallot, &refereeSideBallotBefore,
				sizeof(refereeSideBallot)) == 0);
	}

	// Syntax-only choice mutation cannot confer authority. An ordinary captain
	// may retain the convenient own-side default but cannot manufacture any of
	// the four requests against the opponent, even when availability was broad.
	mpSessionView captainView = GoodView();
	captainView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].available = false;
	captainView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].reason =
		MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	mpMatchControlModel captainModel;
	CHECK(captainModel.IngestAcceptedView(captainView, &error) ==
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION);
	CHECK(captainModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_ZERO);
	CHECK(captainModel.CanChooseActionSide(0));
	CHECK(!captainModel.CanChooseActionSide(1));
	CHECK(captainModel.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_ONE));
	for (int commandIndex = 0; commandIndex < 4; ++commandIndex) {
		mpMatchOperationRequest_t hostile; hostile.Clear();
		CHECK(!captainModel.BuildRequest(sideCommands[commandIndex],
			++requestId, hostile, &error));
		CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE);
	}
	CHECK(!captainModel.SetActionSideChoice(
		static_cast<mpMatchControlSideChoice_t>(99)));
	CHECK(captainModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_ONE);
	CHECK(captainModel.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_NONE));
	mpMatchOperationRequest_t ownDefault; ownDefault.Clear();
	CHECK(captainModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_TIMEOUT,
		++requestId, ownDefault, &error));
	CHECK(ownDefault.teamTarget == MP_MATCH_TEAM_MARINE);

	// Duel targets are stable competition sides, not transient gameplay-team
	// presentation. A contestant bound to B still forfeits B after the visible
	// team rows reorder, while a neutral referee can explicitly choose A or B.
	mpSessionView duelView = GoodView();
	duelView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].available = false;
	duelView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].reason =
		MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	duelView.publicState.recipient.side = MP_MATCH_VIEW_SIDE_NONE;
	duelView.publicState.recipient.competitionSide = 1;
	duelView.publicState.series.gameType = GAME_DUEL;
	duelView.publicState.rosterSummaries[0].side = 1;
	duelView.publicState.rosterSummaries[1].side = 0;
	mpMatchControlModel duelModel;
	CHECK(duelModel.IngestAcceptedView(duelView, &error) ==
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION);
	CHECK(duelModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_ONE);
	CHECK(duelModel.ActionSideUsesCompetitionLabels());
	CHECK(!duelModel.CanChooseActionSide(0));
	CHECK(duelModel.CanChooseActionSide(1));
	mpMatchOperationRequest_t duelB; duelB.Clear();
	CHECK(duelModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_FORFEIT,
		++requestId, duelB, &error));
	CHECK(duelB.teamTarget == MP_MATCH_TEAM_STROGG);
	CHECK(duelModel.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_ZERO));
	mpMatchOperationRequest_t duelOpponent; duelOpponent.Clear();
	CHECK(!duelModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_FORFEIT,
		++requestId, duelOpponent, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE);
	// A Duel contestant has no gameplay team, so there is no side electorate.
	CHECK(duelModel.SelectProposalTemplateRow(FindTemplate(duelModel,
		MP_MATCH_OP_FORFEIT)));
	mpMatchOperationRequest_t duelSideBallot; duelSideBallot.Clear();
	CHECK(!duelModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_PROPOSAL_CREATE,
		++requestId, duelSideBallot, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE);

	// An explicit referee selection survives ordinary view refreshes so an
	// authority loss is detected rather than silently retargeting the request.
	// A connection rebind, however, resets it to the new recipient's own side.
	mpSessionView staleSideView = GoodView();
	staleSideView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].available = false;
	staleSideView.publicState.operationAvailability[
		MP_MATCH_OP_BROADCASTER_SET - 1].reason =
		MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
	staleSideView.publicState.recipient.side = MP_MATCH_VIEW_SIDE_NONE;
	staleSideView.publicState.recipient.competitionSide = MP_MATCH_VIEW_SIDE_NONE;
	staleSideView.publicState.recipient.publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_REFEREE);
	mpMatchControlModel staleSideModel;
	CHECK(staleSideModel.IngestAcceptedView(staleSideView, &error) ==
		MP_MATCH_CONTROL_INGEST_REPLACED_SESSION);
	CHECK(staleSideModel.SetActionSideChoice(MP_MATCH_CONTROL_SIDE_CHOICE_ONE));
	staleSideView.publicState.recipient.side = 0;
	staleSideView.publicState.recipient.competitionSide = 0;
	staleSideView.publicState.recipient.publicRoleMask =
		MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
	++staleSideView.publicState.viewRevision;
	CHECK(staleSideModel.IngestAcceptedView(staleSideView, &error) ==
		MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(staleSideModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_ONE);
	mpMatchOperationRequest_t staleTarget; staleTarget.Clear();
	CHECK(!staleSideModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_FORFEIT,
		++requestId, staleTarget, &error));
	CHECK(error.reason == MP_MATCH_CONTROL_ERROR_INVALID_SIDE);
	++staleSideView.publicState.recipient.bindingGeneration;
	CHECK(staleSideModel.IngestAcceptedView(staleSideView, &error) ==
		MP_MATCH_CONTROL_INGEST_UPDATED);
	CHECK(staleSideModel.ActionSideChoice() == MP_MATCH_CONTROL_SIDE_CHOICE_ZERO);
	mpMatchOperationRequest_t reboundOwnSide; reboundOwnSide.Clear();
	CHECK(staleSideModel.BuildRequest(MP_MATCH_CONTROL_COMMAND_FORFEIT,
		++requestId, reboundOwnSide, &error));
	CHECK(reboundOwnSide.teamTarget == MP_MATCH_TEAM_MARINE);

	return 0;
}
'''


def native_contracts() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_control_model_contract: native checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-control-model-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_control_model_contract.cpp"
        executable = temp_dir / (
            "match_control_model_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_control_model_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'src'}",
                str(harness),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone Match Control model contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "Match Control model native invariant failed at harness line "
                f"{ran.returncode}:\n{ran.stdout}{ran.stderr}"
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    native_contracts()
    print("mp_match_control_model_contract: PASS")


if __name__ == "__main__":
    main()
