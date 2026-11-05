#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and hostile executable contracts for the match-operation pipeline."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/mpgame/mp/match/MatchOperations.h"
SOURCE = ROOT / "src/mpgame/mp/match/MatchOperations.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for forbidden in (
        "idUserInterface",
        "idCVar",
        "idFile",
        "cmdSystem",
        "gameLocal",
        "idPlayer",
        "rvMultiplayerGame",
        "consoleCommand",
        "system(",
    ):
        if forbidden in combined:
            raise AssertionError(
                f"operation domain layer contains forbidden dependency {forbidden!r}"
            )

    for token in (
        "trustedTransportSlot",
        "localOperator",
        "sessionOperational",
        "countdownPrerequisitesSatisfied",
        "preauthenticatedRefereeGrant",
        "cooldownPolicyAccepted",
        "validatedRuleContext",
        "engineTime",
        "expectedRulesRevision",
        "expectedRulesDigest",
        "expectedStagedRulesDigest",
        "expectedProposalRevision",
        "expectedSeriesRevision",
    ):
        require(header, token, "explicit trusted adapter context")

    require(source, "capabilityPolicies[]", "explicit capability mapping table")
    capabilities = set(
        re.findall(r"\{\s*(MP_MATCH_PROTOCOL_CAP_[A-Z_]+)\s*,", source)
    )
    expected_capabilities = {
        "MP_MATCH_PROTOCOL_CAP_READY_SELF",
        "MP_MATCH_PROTOCOL_CAP_READY_TEAM",
        "MP_MATCH_PROTOCOL_CAP_FORCE_READY",
        "MP_MATCH_PROTOCOL_CAP_TEAM_SELF",
        "MP_MATCH_PROTOCOL_CAP_TEAM_LOCK",
        "MP_MATCH_PROTOCOL_CAP_QUEUE",
        "MP_MATCH_PROTOCOL_CAP_TIMEOUT_TEAM",
        "MP_MATCH_PROTOCOL_CAP_PAUSE_TECHNICAL",
        "MP_MATCH_PROTOCOL_CAP_RESUME",
        "MP_MATCH_PROTOCOL_CAP_REFEREE_SESSION",
        "MP_MATCH_PROTOCOL_CAP_RULES_STAGE",
        "MP_MATCH_PROTOCOL_CAP_RULES_COMMIT",
        "MP_MATCH_PROTOCOL_CAP_PROPOSAL_CREATE",
        "MP_MATCH_PROTOCOL_CAP_PROPOSAL_CAST",
        "MP_MATCH_PROTOCOL_CAP_PROPOSAL_CANCEL",
        "MP_MATCH_PROTOCOL_CAP_ROSTER_SELF",
        "MP_MATCH_PROTOCOL_CAP_ROSTER_MANAGE",
        "MP_MATCH_PROTOCOL_CAP_ROLE_ASSIGN",
        "MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE",
        "MP_MATCH_PROTOCOL_CAP_VETO_SELECT",
        "MP_MATCH_PROTOCOL_CAP_FORFEIT",
        "MP_MATCH_PROTOCOL_CAP_ABORT",
		"MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN",
		"MP_MATCH_PROTOCOL_CAP_ROSTER_LEAVE_SELF",
        "MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE",
    }
    if capabilities != expected_capabilities:
        raise AssertionError(
            "protocol/session capability table is not exhaustive: "
            f"missing={sorted(expected_capabilities - capabilities)}, "
            f"extra={sorted(capabilities - expected_capabilities)}"
        )

    if re.search(
        r"static_cast\s*<\s*(?:mpMatchCapabilityMask_t|mpMatchRoleMask_t|"
        r"mpMatchRosterRole_t|mpMatchTeam_t|mpMatchProtocolCapabilityMask_t)",
        combined,
    ):
        raise AssertionError("operation pipeline casts across policy domains")

    for opcode in range(1, 37):
        # Stable opcode values are append-only and the switch must mention each
        # symbolic member. The protocol contract separately checks the values.
        members = re.findall(r"MP_MATCH_OP_[A-Z_]+", source)
        if len(set(members)) < 36:
            raise AssertionError(
                f"operation dispatcher is not exhaustive near stable opcode {opcode}"
            )
            break

    for token in (
        "MP_OPERATION_CONTINUATION_POLICY_RATE_LIMIT",
        "MP_OPERATION_CONTINUATION_REFEREE_AUTHENTICATE",
        "MP_OPERATION_CONTINUATION_TEAM_CHANGE",
        "MP_OPERATION_CONTINUATION_TEAM_LOCK",
        "MP_OPERATION_CONTINUATION_QUEUE_JOIN",
        "MP_OPERATION_CONTINUATION_RULES_COMMIT",
        "MP_OPERATION_CONTINUATION_PROPOSAL_CREATE",
        "MP_OPERATION_CONTINUATION_ROSTER_INVITE",
		"MP_OPERATION_CONTINUATION_ROSTER_REMOVE",
        "MP_OPERATION_CONTINUATION_ROSTER_SUBSTITUTE",
		"MP_OPERATION_CONTINUATION_ROLE_ASSIGN",
		"MP_OPERATION_CONTINUATION_ROSTER_LEAVE",
        "MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE",
        "MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND",
        "MP_MATCH_ROSTER_SUBSTITUTE",
		"Every roster-role change also changes seat",
        "MP_OPERATION_CONTINUATION_SERIES_CONFIGURE_PROFILE",
        "MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP",
        "MP_OPERATION_CONTINUATION_SERIES_MATCH_RESULT",
        "MP_OPERATION_CONTINUATION_PROPOSAL_ACKNOWLEDGE",
        "MP_MATCH_VETO_SIDE",
        "MapStartingGameSide",
        "MP_MATCH_ARG_STARTING_SIDE",
        "MPSeriesProfileByKey",
        "MP_SERIES_REASON_PROFILE_BEST_OF_MISMATCH",
        "MPOperationMapProtocolCompetitionSide",
        "MP_MATCH_ARG_COMPETITION_SIDE",
        "phase != GAMEREVIEW && !recoveredReview",
    ):
        require(combined, token, "typed adapter continuations")

    require(
        source,
        "mpCompetitiveRules candidate = rules;",
        "copy-before-commit rule preflight",
    )
    require(
        source,
        "mpCompetitionSeries candidate = series;",
        "copy-before-map-load series preflight",
    )
    require(
        source,
        "ExecuteInternal( operation, context",
        "passed proposal re-entry through the same executor",
    )
    require(
        source,
        "proposals.Acknowledge(",
        "separate passed-proposal acknowledgement",
    )
    if re.search(
        r"SetParticipantRoles\s*\([^;]*MP_MATCH_ROLE_SERVER_OPERATOR",
        source,
        re.DOTALL,
    ):
        raise AssertionError("connection-scoped operator role can be assigned")

    # Declaration-level collision scan. Normal references from future adapters
    # are allowed; competing declarations under the owned prefix are not.
    declaration = re.compile(
        r"^\s*(?:class|struct|typedef|enum|#define|static\s+const|"
        r"const\s+\w[^;]*\bMPOperation|bool\s+MPOperation)",
        re.MULTILINE,
    )
    for path in (ROOT / "src").rglob("*.h"):
        if path == HEADER or any(part in {"builddir", ".tmp", ".git"} for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in declaration.finditer(text):
            line = text[match.start() : text.find("\n", match.start())]
            if "MP_OPERATION_" in line or "MPOperation" in line or "mpOperation" in line:
                raise AssertionError(
                    f"public MatchOperations identifier collides in {path}: {line.strip()}"
                )


SUPPORT_AND_TEST = r'''
#include "src/mpgame/mp/match/MatchOperations.h"

#include <stdio.h>
#include <string.h>

// Minimal protocol registry seam. MatchProtocol has its own codec executable
// contract; this harness exercises the real operation, session, proposal and
// series code with adversarial authoritative requests.
static mpMatchOperationDescriptor_t descriptors[ MP_MATCH_OP_COUNT ];
static bool descriptorsReady = false;

static void SetDescriptor( mpMatchOperationOpcode_t opcode,
        mpMatchProtocolCapabilityMask_t capability, mpMatchPhaseMask_t phases,
        unsigned int flags, mpMatchCooldownClass_t cooldown ) {
    descriptors[ opcode ].opcode = opcode;
    descriptors[ opcode ].token = "contract";
    descriptors[ opcode ].labelLocalizationId = MP_MATCH_LOCALIZATION_OPERATION_BASE;
    descriptors[ opcode ].confirmationLocalizationId = MP_MATCH_LOCALIZATION_NONE;
    descriptors[ opcode ].requiredCapability = capability;
    descriptors[ opcode ].legalPhaseMask = phases;
    descriptors[ opcode ].flags = flags;
    descriptors[ opcode ].cooldownClass = cooldown;
    descriptors[ opcode ].arguments = NULL;
    descriptors[ opcode ].argumentCount = 0;
}

static void InitDescriptors( void ) {
    if ( descriptorsReady ) {
        return;
    }
    memset( descriptors, 0, sizeof( descriptors ) );
    SetDescriptor( MP_MATCH_OP_READY_SET, MP_MATCH_PROTOCOL_CAP_READY_SELF,
        MP_MATCH_PHASE_WARMUP, 0, MP_MATCH_COOLDOWN_INTERACTION );
    SetDescriptor( MP_MATCH_OP_FORCE_READY, MP_MATCH_PROTOCOL_CAP_FORCE_READY,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN,
        MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
        MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET,
        MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_TEAM_JOIN, MP_MATCH_PROTOCOL_CAP_TEAM_SELF,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_GAMEREVIEW | MP_MATCH_PHASE_NEXTGAME,
        MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
        MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
        MP_MATCH_COOLDOWN_INTERACTION );
    SetDescriptor( MP_MATCH_OP_RULES_DISCARD, MP_MATCH_PROTOCOL_CAP_RULES_STAGE,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_GAMEREVIEW | MP_MATCH_PHASE_NEXTGAME,
        0, MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_TIMEOUT_REQUEST, MP_MATCH_PROTOCOL_CAP_TIMEOUT_TEAM,
        MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH,
        MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET | MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
        MP_MATCH_COOLDOWN_TEAM_ACTION );
    SetDescriptor( MP_MATCH_OP_RESUME_REQUEST, MP_MATCH_PROTOCOL_CAP_RESUME,
        MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH,
        MP_MATCH_OPERATION_FLAG_PROPOSABLE, MP_MATCH_COOLDOWN_TEAM_ACTION );
    SetDescriptor( MP_MATCH_OP_SERIES_START, MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE,
        MP_MATCH_PHASE_WARMUP, 0, MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_SERIES_ADVANCE, MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_GAMEREVIEW |
        MP_MATCH_PHASE_NEXTGAME, 0, MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_SERIES_STAGE_PROFILE,
        MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE,
        MP_MATCH_PHASE_WARMUP, 0, MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_VETO_SELECT, MP_MATCH_PROTOCOL_CAP_VETO_SELECT,
        MP_MATCH_PHASE_WARMUP, 0, MP_MATCH_COOLDOWN_TEAM_ACTION );
    SetDescriptor( MP_MATCH_OP_ABORT, MP_MATCH_PROTOCOL_CAP_ABORT,
        MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH,
        MP_MATCH_OPERATION_FLAG_PROPOSABLE, MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_BROADCASTER_SET,
        MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON |
        MP_MATCH_PHASE_SUDDENDEATH | MP_MATCH_PHASE_GAMEREVIEW |
        MP_MATCH_PHASE_NEXTGAME,
        MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
        MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
        MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_ROSTER_LEAVE,
        MP_MATCH_PROTOCOL_CAP_ROSTER_LEAVE_SELF,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON |
        MP_MATCH_PHASE_SUDDENDEATH | MP_MATCH_PHASE_GAMEREVIEW |
        MP_MATCH_PHASE_NEXTGAME,
        0, MP_MATCH_COOLDOWN_INTERACTION );
    SetDescriptor( MP_MATCH_OP_PARTICIPANT_REMOVE,
        MP_MATCH_PROTOCOL_CAP_PARTICIPANT_REMOVE,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON |
        MP_MATCH_PHASE_SUDDENDEATH | MP_MATCH_PHASE_GAMEREVIEW |
        MP_MATCH_PHASE_NEXTGAME,
        MP_MATCH_OPERATION_FLAG_PROPOSABLE |
        MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
        MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
        MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_PROPOSAL_CREATE, MP_MATCH_PROTOCOL_CAP_PROPOSAL_CREATE,
        MP_MATCH_PHASE_WARMUP | MP_MATCH_PHASE_COUNTDOWN | MP_MATCH_PHASE_GAMEON |
        MP_MATCH_PHASE_SUDDENDEATH | MP_MATCH_PHASE_GAMEREVIEW |
        MP_MATCH_PHASE_NEXTGAME,
        MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
        MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
        MP_MATCH_OPERATION_FLAG_NESTED_ARGUMENTS,
        MP_MATCH_COOLDOWN_INTERACTION );
    SetDescriptor( MP_MATCH_OP_FORFEIT, MP_MATCH_PROTOCOL_CAP_FORFEIT,
        MP_MATCH_PHASE_GAMEON | MP_MATCH_PHASE_SUDDENDEATH,
        MP_MATCH_OPERATION_FLAG_PROPOSABLE |
        MP_MATCH_OPERATION_FLAG_ALLOW_TEAM_TARGET |
        MP_MATCH_OPERATION_FLAG_REQUIRE_TEAM_TARGET,
        MP_MATCH_COOLDOWN_PRIVILEGED );
    SetDescriptor( MP_MATCH_OP_SERIES_CONTESTANT_BIND,
        MP_MATCH_PROTOCOL_CAP_SERIES_MANAGE, MP_MATCH_PHASE_WARMUP,
        MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET |
        MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET,
        MP_MATCH_COOLDOWN_PRIVILEGED );
    descriptorsReady = true;
}

const mpMatchOperationDescriptor_t *MPMatchOperationDescriptor(
        mpMatchOperationOpcode_t opcode ) {
    InitDescriptors();
    if ( opcode <= MP_MATCH_OP_INVALID || opcode >= MP_MATCH_OP_COUNT ||
            descriptors[ opcode ].opcode != opcode ) {
        return NULL;
    }
    return &descriptors[ opcode ];
}

bool MPMatchProtocolValidateRequest( const mpMatchOperationRequest_t &request,
        mpMatchProtocolError_t *error ) {
    if ( error != NULL ) {
        error->Clear();
    }
    if ( request.schemaVersion != MP_MATCH_PROTOCOL_SCHEMA_VERSION ) {
        if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_UNSUPPORTED_SCHEMA;
        return false;
    }
    if ( request.sessionId == 0 || request.requestId == 0 ||
            request.actorSlot >= MP_MATCH_PROTOCOL_MAX_ACTOR_SLOTS ||
            request.actorBindingGeneration == 0 ||
            MPMatchOperationDescriptor( request.opcode ) == NULL ) {
        if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_INVALID_TARGET;
        return false;
    }
    if ( request.opcode == MP_MATCH_OP_READY_SET ||
            request.opcode == MP_MATCH_OP_FORCE_READY ||
            request.opcode == MP_MATCH_OP_BROADCASTER_SET ) {
        if ( request.argumentCount < 1 ||
                request.arguments[ 0 ].fieldId != MP_MATCH_ARG_ENABLED ||
                request.arguments[ 0 ].value.type != MP_MATCH_VALUE_BOOL ) {
            if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE;
            return false;
        }
    }
    if ( request.opcode == MP_MATCH_OP_ABORT ) {
        if ( request.argumentCount != 1 ||
                request.arguments[ 0 ].fieldId != MP_MATCH_ARG_REASON ||
                request.arguments[ 0 ].value.type != MP_MATCH_VALUE_STRING ) {
            if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_ARGUMENT_TYPE;
            return false;
        }
    }
    if ( request.opcode == MP_MATCH_OP_VETO_SELECT ) {
        const mpMatchOperationArgument_t *action = NULL;
        const mpMatchOperationArgument_t *map = NULL;
        const mpMatchOperationArgument_t *startingSide = NULL;
        for ( int i = 0; i < request.argumentCount; ++i ) {
            if ( request.arguments[ i ].fieldId == MP_MATCH_ARG_VETO_ACTION ) {
                action = &request.arguments[ i ];
            } else if ( request.arguments[ i ].fieldId == MP_MATCH_ARG_MAP_TOKEN ) {
                map = &request.arguments[ i ];
            } else if ( request.arguments[ i ].fieldId == MP_MATCH_ARG_STARTING_SIDE ) {
                startingSide = &request.arguments[ i ];
            }
        }
        if ( action == NULL || map == NULL || action->value.type != MP_MATCH_VALUE_ENUM ||
                map->value.type != MP_MATCH_VALUE_STRING ||
                action->value.enumValue < MP_MATCH_VETO_BAN ||
                action->value.enumValue > MP_MATCH_VETO_SIDE ||
                ( ( action->value.enumValue == MP_MATCH_VETO_SIDE ) !=
                    ( startingSide != NULL ) ) ||
                ( startingSide != NULL &&
                    ( startingSide->value.type != MP_MATCH_VALUE_ENUM ||
                      startingSide->value.enumValue < MP_MATCH_STARTING_SIDE_MARINE ||
                      startingSide->value.enumValue > MP_MATCH_STARTING_SIDE_STROGG ) ) ) {
            if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_ARGUMENT_COUNT;
            return false;
        }
    }
    if ( request.opcode == MP_MATCH_OP_PARTICIPANT_REMOVE &&
            ( !request.hasParticipantTarget || request.participantTarget == 0 ||
              request.hasTeamTarget || request.argumentCount != 0 ) ) {
        if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_INVALID_TARGET;
        return false;
    }
    if ( request.opcode == MP_MATCH_OP_SERIES_CONTESTANT_BIND ) {
        if ( !request.hasParticipantTarget || request.participantTarget == 0 ||
                request.hasTeamTarget || request.argumentCount != 1 ||
                request.arguments[ 0 ].fieldId != MP_MATCH_ARG_COMPETITION_SIDE ||
                request.arguments[ 0 ].value.type != MP_MATCH_VALUE_ENUM ||
                request.arguments[ 0 ].value.enumValue < MP_MATCH_COMPETITION_SIDE_A ||
                request.arguments[ 0 ].value.enumValue > MP_MATCH_COMPETITION_SIDE_B ) {
            if ( error != NULL ) error->reason = MP_MATCH_PROTOCOL_REASON_ARGUMENT_RANGE;
            return false;
        }
    }
    return true;
}

void mpMatchProtocolError_s::Clear( void ) {
    reason = MP_MATCH_PROTOCOL_REASON_NONE;
    fieldId = 0;
    detail = 0;
}

void mpMatchOperationValue_s::Clear( void ) {
    type = MP_MATCH_VALUE_INVALID;
    signedValue = 0;
    unsignedValue = 0;
    enumValue = 0;
    stringLength = 0;
    stringValue[ 0 ] = '\0';
}
void mpMatchOperationValue_s::SetBool( bool value ) {
    Clear(); type = MP_MATCH_VALUE_BOOL; unsignedValue = value ? 1u : 0u;
}
void mpMatchOperationValue_s::SetInt32( int value ) {
    Clear(); type = MP_MATCH_VALUE_INT32; signedValue = value;
}
void mpMatchOperationValue_s::SetUInt32( unsigned int value ) {
    Clear(); type = MP_MATCH_VALUE_UINT32; unsignedValue = value;
}
void mpMatchOperationValue_s::SetEnum( unsigned short value ) {
    Clear(); type = MP_MATCH_VALUE_ENUM; enumValue = value;
}
bool mpMatchOperationValue_s::SetString( const char *value, int length ) {
    Clear();
    if ( value == NULL ) return false;
    if ( length < 0 ) length = static_cast<int>( strlen( value ) );
    if ( length < 0 || length > MP_MATCH_PROTOCOL_MAX_STRING_BYTES ) return false;
    memcpy( stringValue, value, static_cast<size_t>( length ) );
    stringValue[ length ] = '\0';
    stringLength = static_cast<unsigned short>( length );
    type = MP_MATCH_VALUE_STRING;
    return true;
}
void mpMatchOperationValue_s::SetOpcode( mpMatchOperationOpcode_t value ) {
    Clear(); type = MP_MATCH_VALUE_OPCODE; enumValue = static_cast<unsigned short>( value );
}
void mpMatchOperationValue_s::SetParticipantId( mpMatchProtocolParticipantId_t value ) {
    Clear(); type = MP_MATCH_VALUE_PARTICIPANT_ID; unsignedValue = value;
}
void mpMatchOperationArgument_s::Clear( void ) {
    fieldId = MP_MATCH_ARG_INVALID; value.Clear();
}
void mpMatchOperationRequest_s::Clear( void ) {
    schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION;
    sessionId = 0; requestId = 0; opcode = MP_MATCH_OP_INVALID;
    expectedSessionRevision = 0; actorSlot = 0; actorBindingGeneration = 0;
    hasParticipantTarget = false; participantTarget = 0;
    hasTeamTarget = false; teamTarget = MP_MATCH_TEAM_NONE; argumentCount = 0;
    for ( int i = 0; i < MP_MATCH_PROTOCOL_MAX_ARGUMENTS; ++i ) arguments[ i ].Clear();
}

// Small deterministic MatchRules implementation for linking this focused
// harness. The production MatchRules core has its own executable contract.
mpRuleValidationFailure_s::mpRuleValidationFailure_s( void ) { Clear(); }
void mpRuleValidationFailure_s::Clear( void ) {
    reason = MP_RULE_VALID; field = MP_RULE_FIELD_COUNT;
    actual = minimum = maximum = 0;
}
mpMatchRulesValidationContext_s::mpMatchRulesValidationContext_s( void ) {
    maxClients = maxTeamSize = maxRosterSizePerTeam = 32;
    maxCountdownSeconds = maxTimeoutCountPerTeam = maxTimeoutSeconds = 1000;
    maxOvertimeSeconds = maxOvertimePeriods = 1000;
    requireMapSupport = false; mapSupportCheckedGameType = 0;
    mapSupportsCheckedGameType = true;
}
mpRuleCommitResult_s::mpRuleCommitResult_s( void ) {
    disposition = MP_RULE_COMMIT_REJECTED; committedRevision = 0;
    committedDigest = candidateDigest = 0;
}
bool mpRuleCommitResult_s::Succeeded( void ) const {
    return disposition != MP_RULE_COMMIT_REJECTED;
}
mpMatchRulesDraft::mpMatchRulesDraft( void ) {
    memset( values, 0, sizeof( values ) ); sourceProfile = MP_MATCH_PROFILE_CASUAL;
    customized = false;
}
bool mpMatchRulesDraft::SetTypedValue( mpRuleFieldId_t field,
        mpRuleFieldType_t, int value, mpRuleValidationFailure_t &failure,
        bool markCustomized ) {
    if ( field < 0 || field >= MP_RULE_FIELD_COUNT ) return false;
    values[ field ] = value; customized = markCustomized; failure.Clear(); return true;
}
void mpMatchRulesDraft::SetRawProfileValue( mpRuleFieldId_t field, int value ) {
    if ( field >= 0 && field < MP_RULE_FIELD_COUNT ) values[ field ] = value;
}
bool mpMatchRulesDraft::SetBool( mpRuleFieldId_t field, bool value,
        mpRuleValidationFailure_t &failure ) {
    return SetTypedValue( field, MP_RULE_TYPE_BOOL, value ? 1 : 0, failure, true );
}
bool mpMatchRulesDraft::SetInteger( mpRuleFieldId_t field, int value,
        mpRuleValidationFailure_t &failure ) {
    return SetTypedValue( field, MP_RULE_TYPE_INTEGER, value, failure, true );
}
bool mpMatchRulesDraft::SetEnum( mpRuleFieldId_t field, int value,
        mpRuleValidationFailure_t &failure ) {
    return SetTypedValue( field, MP_RULE_TYPE_ENUM, value, failure, true );
}
bool mpMatchRulesDraft::SetParsedValue( mpRuleFieldId_t, const char *,
        mpRuleValidationFailure_t & ) { return false; }
int mpMatchRulesDraft::GetInteger( mpRuleFieldId_t field ) const { return values[ field ]; }
bool mpMatchRulesDraft::GetBool( mpRuleFieldId_t field ) const { return values[ field ] != 0; }
mpMatchProfileId_t mpMatchRulesDraft::SourceProfile( void ) const { return sourceProfile; }
bool mpMatchRulesDraft::IsCustomized( void ) const { return customized; }

mpMatchRulesSnapshot::mpMatchRulesSnapshot( void ) {
    memset( values, 0, sizeof( values ) ); schemaVersion = MP_MATCH_RULES_SCHEMA_VERSION;
    revision = 1; digest = 0x1001; sourceProfile = MP_MATCH_PROFILE_CASUAL;
    customized = false;
}
int mpMatchRulesSnapshot::GetInteger( mpRuleFieldId_t field ) const { return values[ field ]; }
bool mpMatchRulesSnapshot::GetBool( mpRuleFieldId_t field ) const { return values[ field ] != 0; }
uint32_t mpMatchRulesSnapshot::SchemaVersion( void ) const { return schemaVersion; }
uint32_t mpMatchRulesSnapshot::Revision( void ) const { return revision; }
uint64_t mpMatchRulesSnapshot::Digest( void ) const { return digest; }
mpMatchProfileId_t mpMatchRulesSnapshot::SourceProfile( void ) const { return sourceProfile; }
bool mpMatchRulesSnapshot::IsCustomized( void ) const { return customized; }
bool mpMatchRulesSnapshot::SameRuleValues( const mpMatchRulesSnapshot &other ) const {
    return memcmp( values, other.values, sizeof( values ) ) == 0;
}
void mpMatchRulesSnapshot::AssignFromDraft( const mpMatchRulesDraft &draft,
        uint32_t newRevision ) {
    for ( int i = 0; i < MP_RULE_FIELD_COUNT; ++i ) {
        values[ i ] = draft.GetInteger( static_cast<mpRuleFieldId_t>( i ) );
    }
    revision = newRevision; digest = 0x1000u + newRevision;
    sourceProfile = draft.SourceProfile(); customized = draft.IsCustomized();
}
void mpMatchRulesSnapshot::RebuildDigest( void ) {}

mpCompetitiveRules::mpCompetitiveRules( void ) : hasStaged( false ) {}
const mpMatchRulesSnapshot &mpCompetitiveRules::Committed( void ) const { return committed; }
mpMatchRulesDraft mpCompetitiveRules::BeginDraftFromCommitted( void ) const {
    mpMatchRulesDraft draft;
    for ( int i = 0; i < MP_RULE_FIELD_COUNT; ++i ) draft.values[ i ] = committed.values[ i ];
    draft.sourceProfile = committed.sourceProfile; draft.customized = committed.customized;
    return draft;
}
mpMatchRulesDraft mpCompetitiveRules::BeginDraftForNextWarmup( void ) const {
    mpMatchRulesDraft draft = BeginDraftFromCommitted();
    if ( hasStaged ) {
        for ( int i = 0; i < MP_RULE_FIELD_COUNT; ++i ) draft.values[ i ] = staged.values[ i ];
    }
    return draft;
}
bool mpCompetitiveRules::BeginDraftFromProfile( mpMatchProfileId_t profile, int,
        mpMatchRulesDraft &draft, mpRuleValidationFailure_t &failure ) const {
    draft = mpMatchRulesDraft(); draft.sourceProfile = profile; failure.Clear(); return true;
}
mpRuleCommitResult_t mpCompetitiveRules::Commit( const mpMatchRulesDraft &draft,
        const mpMatchRulesValidationContext_t &, mpRuleCommitBoundary_t boundary ) {
    mpRuleCommitResult_t result;
    mpMatchRulesSnapshot candidate; candidate.AssignFromDraft( draft, committed.revision + 1 );
    result.committedRevision = committed.revision; result.committedDigest = committed.digest;
    result.candidateDigest = candidate.digest;
    if ( boundary == MP_RULES_FROZEN_FOR_MAP ) {
        staged = candidate; hasStaged = true; result.disposition = MP_RULE_COMMIT_STAGED;
    } else {
        committed = candidate; hasStaged = false; result.disposition = MP_RULE_COMMIT_APPLIED;
        result.committedRevision = committed.revision; result.committedDigest = committed.digest;
    }
    return result;
}
mpRuleCommitResult_t mpCompetitiveRules::ApplyStagedAtWarmup(
        const mpMatchRulesValidationContext_t & ) {
    mpRuleCommitResult_t result;
    if ( !hasStaged ) return result;
    committed = staged; hasStaged = false; result.disposition = MP_RULE_COMMIT_APPLIED;
    result.committedRevision = committed.revision; result.committedDigest = committed.digest;
    result.candidateDigest = committed.digest; return result;
}
bool mpCompetitiveRules::HasStagedSnapshot( void ) const { return hasStaged; }
const mpMatchRulesSnapshot *mpCompetitiveRules::StagedSnapshot( void ) const {
    return hasStaged ? &staged : NULL;
}
bool mpCompetitiveRules::DiscardStagedSnapshot( void ) {
    if ( !hasStaged ) return false; hasStaged = false; return true;
}
const mpMatchProfileDescriptor_t *MPMatchProfileByKey( const char * ) { return NULL; }
const mpRuleFieldDescriptor_t *MPMatchRuleFieldByKey( const char * ) { return NULL; }

struct Fingerprint {
    uint64_t session;
    uint32_t rules;
    uint64_t digest;
    bool staged;
    uint64_t proposal;
    uint64_t series;
};

static Fingerprint Snapshot( const mpMatchSession &session,
        const mpCompetitiveRules &rules, const mpProposalService &proposals,
        const mpCompetitionSeries &series ) {
    Fingerprint value = { session.GetSessionRevision(), rules.Committed().Revision(),
        rules.Committed().Digest(), rules.HasStagedSnapshot(), proposals.GetRevision(),
        series.GetRevision() };
    return value;
}

static bool Same( const Fingerprint &a, const Fingerprint &b ) {
    return a.session == b.session && a.rules == b.rules && a.digest == b.digest &&
        a.staged == b.staged && a.proposal == b.proposal && a.series == b.series;
}

#define CHECK( expression ) do { if ( !( expression ) ) { \
    fprintf( stderr, "contract check failed at line %d: %s\n", __LINE__, #expression ); \
    return 1; } } while ( 0 )

static mpMatchOperationRequest_t MakeRequest( mpMatchOperationOpcode_t opcode,
        const mpMatchSession &session, unsigned int generation ) {
    mpMatchOperationRequest_t request;
    request.Clear();
    request.sessionId = session.GetSessionId();
    request.requestId = static_cast<unsigned int>( opcode ) + 100u;
    request.opcode = opcode;
    request.expectedSessionRevision = session.GetSessionRevision();
    request.actorSlot = 0;
    request.actorBindingGeneration = generation;
    return request;
}

static void AddEnabled( mpMatchOperationRequest_t &request, bool enabled ) {
    request.argumentCount = 1;
    request.arguments[ 0 ].fieldId = MP_MATCH_ARG_ENABLED;
    request.arguments[ 0 ].value.SetBool( enabled );
}

static void AddSeriesProfile( mpMatchOperationRequest_t &request,
        const char *profile, unsigned int bestOf ) {
    request.argumentCount = 2;
    request.arguments[ 0 ].fieldId = MP_MATCH_ARG_SERIES_PROFILE;
    request.arguments[ 0 ].value.SetString( profile );
    request.arguments[ 1 ].fieldId = MP_MATCH_ARG_BEST_OF;
    request.arguments[ 1 ].value.SetUInt32( bestOf );
}

static void AddVeto( mpMatchOperationRequest_t &request,
        unsigned short action, const char *mapToken,
        unsigned short startingSide = 0 ) {
    request.argumentCount = startingSide == 0 ? 2 : 3;
    request.arguments[ 0 ].fieldId = MP_MATCH_ARG_VETO_ACTION;
    request.arguments[ 0 ].value.SetEnum( action );
    request.arguments[ 1 ].fieldId = MP_MATCH_ARG_MAP_TOKEN;
    request.arguments[ 1 ].value.SetString( mapToken );
    if ( startingSide != 0 ) {
        request.arguments[ 2 ].fieldId = MP_MATCH_ARG_STARTING_SIDE;
        request.arguments[ 2 ].value.SetEnum( startingSide );
    }
}

static void AddCompetitionSide( mpMatchOperationRequest_t &request,
        unsigned short competitionSide ) {
    request.argumentCount = 1;
    request.arguments[ 0 ].fieldId = MP_MATCH_ARG_COMPETITION_SIDE;
    request.arguments[ 0 ].value.SetEnum( competitionSide );
}

static bool BuildMapCompleteSeries( mpSeriesProfileId_t profile,
        const char * const *mapPool, int mapCount, mpCompetitionSeries &series ) {
    mpSeriesConfiguration configuration;
    mpSeriesReason_t reason = MP_SERIES_REASON_COUNT;
    if ( !MPSeriesBuildProfileDraft( profile, GAME_DUEL, 0x314159u, 0,
            false, mapPool, mapCount, configuration, reason ) ||
            !series.Configure( configuration, series.GetRevision() ).WasApplied() ||
            !series.Start( series.GetRevision() ).WasApplied() ) {
        return false;
    }
    while ( series.GetState() == MP_SERIES_VETO ) {
        const int stepIndex = series.GetCurrentVetoStep();
        if ( stepIndex < 0 || stepIndex >= configuration.vetoStepCount ) {
            return false;
        }
        const mpSeriesVetoStep &step = configuration.vetoSteps[ stepIndex ];
        if ( step.action == MP_SERIES_VETO_SIDE ) {
            return false;
        }
        int poolIndex = -1;
        for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
            if ( series.GetMapDisposition( index ) == MP_SERIES_MAP_AVAILABLE ) {
                poolIndex = index;
                break;
            }
        }
        if ( poolIndex < 0 || !series.ApplyVeto( step.expectedSide, step.action,
                configuration.mapPool[ poolIndex ], MP_SERIES_SIDE_NONE,
                series.GetRevision() ).WasApplied() ) {
            return false;
        }
    }
    const char *mapToken = series.GetNextMapToken();
    return series.GetState() == MP_SERIES_READY && mapToken != NULL &&
        series.BeginMap( mapToken, series.GetRevision() ).WasApplied() &&
        series.CommitMapResult( MP_SERIES_MAP_DECIDED, 0, 10, 5,
            0x123456789ULL, 0x1001, series.GetRevision() ).WasApplied() &&
        series.GetState() == MP_SERIES_MAP_COMPLETE;
}

static mpOperationAdapterContext_t MakeContext( const mpCompetitiveRules &rules,
        const mpProposalService &proposals, const mpCompetitionSeries &series ) {
    mpOperationAdapterContext_t context;
    context.trustedTransportSlot = 0;
    context.sessionOperational = true;
    context.countdownPrerequisitesSatisfied = true;
    context.cooldownPolicyAccepted = true;
    context.engineTime = mpMatchEngineTime::FromMilliseconds( 10 );
    context.expectedRulesRevision = rules.Committed().Revision();
    context.expectedRulesDigest = rules.Committed().Digest();
    context.expectedStagedRules = rules.HasStagedSnapshot();
    context.expectedStagedRulesDigest = rules.HasStagedSnapshot() ?
        rules.StagedSnapshot()->Digest() : 0;
    context.expectedProposalRevision = proposals.GetRevision();
    context.expectedSeriesRevision = series.GetRevision();
    return context;
}

static int DuelTimeouts( void ) {
    for ( int mode = 0; mode < 2; ++mode )
    for ( int granted = 0; granted < 2; ++granted )
    for ( int active = 0; active < 2; ++active )
    for ( int side = -1; side <= 2; ++side ) {
        mpMatchSession session;
        CHECK( session.Reset( 42, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
        CHECK( session.TransitionPhase( WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED,
            mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );
        mpParticipantId player;
        CHECK( session.BindParticipant( 0, true, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
            session.GetSessionRevision(), player ).WasApplied() );
        unsigned int generation = 0;
        CHECK( session.GetSlotGeneration( 0, generation ) );
        CHECK( !session.SetParticipantActive( player, active != 0,
            session.GetSessionRevision() ).WasRejected() );
        CHECK( session.ConfigureTimeouts( 2, 60000, true, 500,
            MP_MATCH_RESUME_OWNER_OR_AUTHORITY, session.GetSessionRevision() ).WasApplied() );
        CHECK( session.FreezeRules( 1, 0x1001, session.GetSessionRevision() ).WasApplied() );
        CHECK( session.TransitionPhase( COUNTDOWN, MP_MATCH_TRANSITION_READY_GATE,
            mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );
        CHECK( session.TransitionPhase( GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE,
            mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );
        mpCompetitiveRules rules;
        mpProposalService proposals;
        mpCompetitionSeries series;
        mpMatchOperationExecutor executor;
        mpOperationAdapterContext_t context = MakeContext( rules, proposals, series );
        context.ruleGameType = mode ? GAME_DUEL : GAME_DM;
        context.actorCompetitionContestant = granted != 0;
        context.actorCompetitionSide = side;
        mpMatchOperationRequest_t timeout = MakeRequest( MP_MATCH_OP_TIMEOUT_REQUEST, session, generation );
        timeout.hasTeamTarget = true;
        timeout.teamTarget = side == 1 ? MP_MATCH_TEAM_STROGG : MP_MATCH_TEAM_MARINE;
        const Fingerprint before = Snapshot( session, rules, proposals, series );
        const bool permitted = mode && granted && active && side >= 0 && side < 2;
        if ( permitted ) {
            mpMatchOperationRequest_t opponent = timeout;
            opponent.teamTarget = side == 0 ? MP_MATCH_TEAM_STROGG : MP_MATCH_TEAM_MARINE;
            CHECK( executor.Execute( opponent, context, session, rules, proposals, series ).reason ==
                MP_OPERATION_REASON_TARGET_ALIGNMENT );
            CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
        }
        mpOperationExecutionResult_t result = executor.Execute( timeout, context,
            session, rules, proposals, series );
        if ( !permitted ) {
            CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
            CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
            continue;
        }
        CHECK( result.outcome == MP_OPERATION_APPLIED );
        CHECK( session.GetPause().ownerSide == side );
        CHECK( session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 10 ) ).WasApplied() );
        CHECK( session.GetPause().state == MP_MATCH_PAUSED );
        CHECK( session.GetTimeoutBudget( side ).remaining == 1 );
        CHECK( session.GetTimeoutBudget( 1 - side ).remaining == 2 );
        mpMatchOperationRequest_t resume = MakeRequest( MP_MATCH_OP_RESUME_REQUEST, session, generation );
        context.actorCompetitionSide = 1 - side;
        const Fingerprint paused = Snapshot( session, rules, proposals, series );
        CHECK( executor.Execute( resume, context, session, rules, proposals, series ).outcome ==
            MP_OPERATION_REJECTED );
        CHECK( Same( paused, Snapshot( session, rules, proposals, series ) ) );
        context.actorCompetitionSide = side;
        context.actorCompetitionContestant = false;
        CHECK( executor.Execute( resume, context, session, rules, proposals, series ).reason ==
            MP_OPERATION_REASON_NOT_AUTHORIZED );
        CHECK( Same( paused, Snapshot( session, rules, proposals, series ) ) );
        context.actorCompetitionContestant = true;
        CHECK( executor.Execute( resume, context, session, rules, proposals, series ).outcome ==
            MP_OPERATION_APPLIED );
        CHECK( session.GetPause().state == MP_MATCH_RESUME_COUNTDOWN );
        CHECK( session.AdvanceFrame( mpMatchEngineTime::FromMilliseconds( 511 ) ).WasApplied() );
        CHECK( session.GetPause().state == MP_MATCH_PAUSE_RUNNING );
        CHECK( session.FindParticipant( player )->roles == MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) );
        CHECK( session.ValidateInvariants() );
    }
    return 0;
}

// A side ballot is a team forfeit vote. Only an active member of the side may
// open it, only while forfeit itself is legal, and a passed ballot executes the
// ordinary forfeit for that side through the same revalidating executor.
static mpMatchOperationRequest_t SideBallotRequest( const mpMatchSession &session,
        int slot, unsigned int generation, mpMatchTeam_t team ) {
    mpMatchOperationRequest_t request;
    request.Clear();
    request.sessionId = session.GetSessionId();
    request.requestId = 500u + static_cast<unsigned int>( slot );
    request.opcode = MP_MATCH_OP_PROPOSAL_CREATE;
    request.expectedSessionRevision = session.GetSessionRevision();
    request.actorSlot = slot;
    request.actorBindingGeneration = generation;
    request.hasTeamTarget = true;
    request.teamTarget = team;
    request.argumentCount = 1;
    request.arguments[ 0 ].fieldId = MP_MATCH_ARG_PROPOSED_OPCODE;
    request.arguments[ 0 ].value.SetEnum( MP_MATCH_OP_FORFEIT );
    return request;
}

static int SideBallots( void ) {
    mpMatchSession session;
    CHECK( session.Reset( 77, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
    CHECK( session.TransitionPhase( WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED,
        mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );
    mpMatchReadinessPolicy policy;
    policy.policy = MP_MATCH_READY_INDIVIDUAL;
    policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
    policy.teamMode = true;
    policy.maximumActivePerSide = 8;
    policy.requiredSideMask = 3;
    CHECK( session.ConfigureReadiness( policy, session.GetSessionRevision() ).WasApplied() );
    // Slots 0 and 1 play Marine, slot 2 Strogg, slot 3 is an unsided spectator
    // and slot 4 a Marine who has been made inactive.
    const int sides[ 5 ] = { 0, 0, 1, MP_MATCH_SIDE_NONE, 0 };
    mpParticipantId people[ 5 ];
    unsigned int generations[ 5 ];
    for ( int slot = 0; slot < 5; ++slot ) {
        CHECK( session.BindParticipant( slot, true, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
            session.GetSessionRevision(), people[ slot ] ).WasApplied() );
        CHECK( session.GetSlotGeneration( slot, generations[ slot ] ) );
        if ( sides[ slot ] != MP_MATCH_SIDE_NONE ) {
            CHECK( session.SetParticipantSide( people[ slot ], sides[ slot ],
                session.GetSessionRevision() ).WasApplied() );
            CHECK( !session.SetParticipantActive( people[ slot ], slot != 4,
                session.GetSessionRevision() ).WasRejected() );
        }
    }
    mpCompetitiveRules rules;
    mpProposalService proposals;
    mpProposalCooldownPolicy_t cooldowns;
    cooldowns.Clear();
    CHECK( proposals.Reset( session.GetSessionId(),
        mpProposalEngineTime::FromMilliseconds( 0 ), cooldowns ) );
    mpCompetitionSeries series;
    mpMatchOperationExecutor executor;

    // Warmup: proposal creation is legal, forfeit is not, so no ballot opens.
    mpOperationAdapterContext_t context = MakeContext( rules, proposals, series );
    Fingerprint before = Snapshot( session, rules, proposals, series );
    mpMatchOperationRequest_t request = SideBallotRequest( session, 0, generations[ 0 ],
        MP_MATCH_TEAM_MARINE );
    mpOperationExecutionResult_t result = executor.Execute( request, context,
        session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.reason == MP_OPERATION_REASON_WRONG_PHASE );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    CHECK( session.BeginCountdown( 1, 0x1001, MP_MATCH_TRANSITION_REFEREE_FORCE_READY,
        people[ 0 ], session.GetSessionRevision() ).WasApplied() );
    CHECK( session.TransitionPhase( GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE,
        mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );

    // The opposing side, an unsided participant holding a referee grant and an
    // inactive member of the side are all refused before any adapter work.
    const int refusedSlots[ 3 ] = { 2, 3, 4 };
    for ( int index = 0; index < 3; ++index ) {
        const int slot = refusedSlots[ index ];
        context = MakeContext( rules, proposals, series );
        context.trustedTransportSlot = slot;
        context.preauthenticatedRefereeGrant = slot == 3;
        before = Snapshot( session, rules, proposals, series );
        request = SideBallotRequest( session, slot, generations[ slot ],
            MP_MATCH_TEAM_MARINE );
        result = executor.Execute( request, context, session, rules, proposals, series );
        CHECK( result.outcome == MP_OPERATION_REJECTED );
        CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
        CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    }

    // An active Marine opens a Marine-scoped ballot.
    context = MakeContext( rules, proposals, series );
    before = Snapshot( session, rules, proposals, series );
    request = SideBallotRequest( session, 0, generations[ 0 ], MP_MATCH_TEAM_MARINE );
    result = executor.Execute( request, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_PROPOSAL_CREATE );
    CHECK( result.continuation.proposalScope == MP_PROPOSAL_SCOPE_TEAM_A );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    // A passed Strogg-scoped forfeit whose recorded proposer is a Marine cannot
    // concede for Strogg at execution.
    mpMatchOperationRequest_t forfeit = MakeRequest( MP_MATCH_OP_FORFEIT, session,
        generations[ 0 ] );
    forfeit.hasTeamTarget = true;
    forfeit.teamTarget = MP_MATCH_TEAM_STROGG;
    mpProposalCreateParams_t create;
    create.Clear();
    create.sessionId = session.GetSessionId();
    create.proposalId = 31;
    create.scope = MP_PROPOSAL_SCOPE_TEAM_B;
    create.electorateCount = 1;
    create.electorate[ 0 ].participant = people[ 0 ].SequencePart();
    create.electorate[ 0 ].human = true;
    create.requiredQuorum = 1;
    create.requiredYes = 1;
    create.createdAt = mpProposalEngineTime::FromMilliseconds( 1 );
    create.expiresAt = mpProposalEngineTime::FromMilliseconds( 30001 );
    create.caller = people[ 0 ].SequencePart();
    create.callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_YES;
    create.operation = forfeit;
    CHECK( proposals.Create( create, proposals.GetRevision() ).WasApplied() );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_TEAM_B )->status ==
        MP_PROPOSAL_STATUS_PASSED );
    context = MakeContext( rules, proposals, series );
    before = Snapshot( session, rules, proposals, series );
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_TEAM_B, 31,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.reason == MP_OPERATION_REASON_TARGET_ALIGNMENT );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    CHECK( session.GetPhase() == GAMEON );

    // The Marines' passed ballot - two voters, both YES - forfeits Marine.
    forfeit.teamTarget = MP_MATCH_TEAM_MARINE;
    create.proposalId = 32;
    create.scope = MP_PROPOSAL_SCOPE_TEAM_A;
    create.electorateCount = 2;
    create.electorate[ 1 ].participant = people[ 1 ].SequencePart();
    create.electorate[ 1 ].human = true;
    create.requiredQuorum = 2;
    create.requiredYes = 2;
    create.operation = forfeit;
    CHECK( proposals.Create( create, proposals.GetRevision() ).WasApplied() );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_TEAM_A )->status ==
        MP_PROPOSAL_STATUS_ACTIVE );
    CHECK( proposals.CastBallot( session.GetSessionId(), MP_PROPOSAL_SCOPE_TEAM_A, 32,
        people[ 1 ].SequencePart(), MP_PROPOSAL_BALLOT_YES,
        mpProposalEngineTime::FromMilliseconds( 2 ),
        proposals.GetRevision() ).WasApplied() );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_TEAM_A )->status ==
        MP_PROPOSAL_STATUS_PASSED );
    context = MakeContext( rules, proposals, series );
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_TEAM_A, 32,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_PROPOSAL_ACKNOWLEDGE );
    CHECK( session.GetPhase() == GAMEREVIEW );
    CHECK( session.GetLastTransition().reason == MP_MATCH_TRANSITION_FORFEIT );
    CHECK( session.ValidateInvariants() );
    CHECK( proposals.ValidateInvariants() );
    return 0;
}

int main( void ) {
    CHECK( DuelTimeouts() == 0 );
    CHECK( SideBallots() == 0 );
    mpMatchSession session;
    CHECK( session.Reset( 0x123456789ULL, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
    CHECK( session.TransitionPhase( WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED,
        mpParticipantId::Invalid(), session.GetSessionRevision() ).WasApplied() );

    mpParticipantId player;
    CHECK( session.BindParticipant( 0, true, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
        session.GetSessionRevision(), player ).WasApplied() );
    unsigned int generation = 0;
    CHECK( session.GetSlotGeneration( 0, generation ) );

    mpMatchReadinessPolicy policy;
    policy.policy = MP_MATCH_READY_INDIVIDUAL;
    policy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
    policy.teamMode = false;
    policy.minimumActiveHumans = 1;
    policy.readyThresholdBasisPoints = 10000;
    policy.maximumActivePerSide = 0;
    policy.requiredSideMask = 0;
    policy.requireDeclaredRosterSeats = false;
    CHECK( session.ConfigureReadiness( policy, session.GetSessionRevision() ).WasApplied() );
    CHECK( session.SetParticipantActive( player, true,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( session.FreezeRules( 1, 0x1001,
        session.GetSessionRevision() ).WasApplied() );

    mpCompetitiveRules rules;
    mpProposalService proposals;
    mpProposalCooldownPolicy_t cooldowns;
    cooldowns.Clear();
    CHECK( proposals.Reset( session.GetSessionId(),
        mpProposalEngineTime::FromMilliseconds( 0 ), cooldowns ) );
    mpCompetitionSeries series;
    mpMatchOperationExecutor executor;

    // Explicit mappings prove the wire domains are not numerically cast.
    int side = 99;
    CHECK( MPOperationMapProtocolTeam( MP_MATCH_TEAM_MARINE, side ) && side == 0 );
    CHECK( MPOperationMapProtocolTeam( MP_MATCH_TEAM_STROGG, side ) && side == 1 );
    CHECK( MPOperationMapProtocolTeam( MP_MATCH_TEAM_SPECTATOR, side ) &&
        side == MP_MATCH_SIDE_NONE );
    for ( unsigned int raw = 0; raw < 256; ++raw ) {
        int competitionSide = 99;
        const bool mapped = MPOperationMapProtocolCompetitionSide(
            static_cast<unsigned short>( raw ), competitionSide );
        CHECK( mapped == ( raw == MP_MATCH_COMPETITION_SIDE_A ||
            raw == MP_MATCH_COMPETITION_SIDE_B ) );
        CHECK( competitionSide == ( raw == MP_MATCH_COMPETITION_SIDE_A ? 0 :
            ( raw == MP_MATCH_COMPETITION_SIDE_B ? 1 : MP_SERIES_SIDE_NONE ) ) );
    }
    mpMatchRosterRole_t rosterRole = MP_MATCH_ROSTER_ROLE_COUNT;
    mpMatchRoleMask_t principalRoles = 0;
    CHECK( MPOperationMapProtocolRosterRole( MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN,
        rosterRole, principalRoles ) );
    CHECK( rosterRole == MP_MATCH_ROSTER_CAPTAIN );
    CHECK( ( principalRoles & MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) != 0 );
    CHECK( ( principalRoles & MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ) ) != 0 );
    CHECK( MPOperationMapProtocolRosterRole(
        MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE, rosterRole, principalRoles ) );
    CHECK( rosterRole == MP_MATCH_ROSTER_SUBSTITUTE );
    CHECK( principalRoles == 0 );
    CHECK( MPOperationCapabilityPolicy( MP_MATCH_PROTOCOL_CAP_FORCE_READY ) != NULL );

    mpMatchOperationRequest_t ready = MakeRequest( MP_MATCH_OP_READY_SET, session, generation );
    AddEnabled( ready, true );
    mpOperationAdapterContext_t context = MakeContext( rules, proposals, series );

	// A failed live-adapter bootstrap may retain an old aggregate solely for a
	// checkpoint retry.  Every typed mutation rejects while it is non-operational.
	context.sessionOperational = false;
	Fingerprint before = Snapshot( session, rules, proposals, series );
	mpOperationExecutionResult_t result = executor.Execute(
		ready, context, session, rules, proposals, series );
	CHECK( result.reason == MP_OPERATION_REASON_SESSION_MISMATCH );
	CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
	context.sessionOperational = true;

    // Rate limiting is a typed pre-commit adapter boundary.
    context.cooldownPolicyAccepted = false;
	before = Snapshot( session, rules, proposals, series );
	result = executor.Execute(
        ready, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_POLICY_RATE_LIMIT );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    context.cooldownPolicyAccepted = true;

    // Transport spoof, stale binding, wrong session, stale CAS and structural
    // corruption all fail without touching any core.
    context.trustedTransportSlot = 1;
    result = executor.Execute( ready, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_TRANSPORT_MISMATCH );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    context.trustedTransportSlot = 0;

    mpMatchOperationRequest_t hostile = ready;
    hostile.actorBindingGeneration = generation + 1;
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_BINDING_STALE );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    hostile = ready;
    hostile.sessionId += 1;
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_SESSION_MISMATCH );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    hostile = ready;
    hostile.expectedSessionRevision -= 1;
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_STALE_SESSION_REVISION );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    hostile = ready;
    hostile.schemaVersion = MP_MATCH_PROTOCOL_SCHEMA_VERSION + 1;
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_PROTOCOL );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    // A player cannot acquire authority by choosing a privileged opcode.
    hostile = MakeRequest( MP_MATCH_OP_FORCE_READY, session, generation );
    AddEnabled( hostile, true );
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    // Local operator is an explicit trusted grant, never a connection role.
    context.localOperator = true;
    hostile.hasParticipantTarget = true;
    hostile.participantTarget = player.SequencePart();
    hostile.hasTeamTarget = true;
    hostile.teamTarget = MP_MATCH_TEAM_MARINE;
    result = executor.Execute( hostile, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_UNREPRESENTABLE );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    CHECK( ( session.FindParticipant( player )->roles &
        MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) == 0 );
    context.localOperator = false;

    // Missing authoritative team/queue state returns a typed continuation.
    mpMatchOperationRequest_t join = MakeRequest( MP_MATCH_OP_TEAM_JOIN, session, generation );
    join.hasTeamTarget = true;
    join.teamTarget = MP_MATCH_TEAM_SPECTATOR;
    result = executor.Execute( join, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_TEAM_CHANGE );
    CHECK( result.continuation.participant == player );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    // Independent rule/series CAS values are checked before any core call.
    mpMatchOperationRequest_t ruleRequest = MakeRequest(
        MP_MATCH_OP_RULES_DISCARD, session, generation );
    context.preauthenticatedRefereeGrant = true;
    context.expectedRulesRevision += 1;
    result = executor.Execute( ruleRequest, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_STALE_RULES_REVISION );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    context.expectedSeriesRevision += 1;
    mpMatchOperationRequest_t seriesRequest = MakeRequest(
        MP_MATCH_OP_SERIES_START, session, generation );
    result = executor.Execute( seriesRequest, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_STALE_SERIES_REVISION );
    CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );

    // A valid direct operation commits once; replay is stopped by session CAS.
    context = MakeContext( rules, proposals, series );
    result = executor.Execute( ready, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( session.GetSessionRevision() == before.session + 1 );
    Fingerprint afterReady = Snapshot( session, rules, proposals, series );
    result = executor.Execute( ready, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_STALE_SESSION_REVISION );
    CHECK( Same( afterReady, Snapshot( session, rules, proposals, series ) ) );

    // Enter countdown using the trusted local grant without ever storing an
    // operator role on the participant.
    mpMatchOperationRequest_t countdown = MakeRequest(
        MP_MATCH_OP_FORCE_READY, session, generation );
    AddEnabled( countdown, true );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
	context.countdownPrerequisitesSatisfied = false;
	before = Snapshot( session, rules, proposals, series );
	result = executor.Execute( countdown, context, session, rules, proposals, series );
	CHECK( result.reason == MP_OPERATION_REASON_SERIES_STATE );
	CHECK( Same( before, Snapshot( session, rules, proposals, series ) ) );
	CHECK( session.GetPhase() == WARMUP );
	context.countdownPrerequisitesSatisfied = true;
    result = executor.Execute( countdown, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( session.GetPhase() == COUNTDOWN );
    CHECK( ( session.FindParticipant( player )->roles &
        MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) == 0 );

    // A passed proposal re-enters the exact executor. Current actor binding,
    // proposal revision, phase and the operation's semantics are revalidated
    // against the session as it is at execution. Ack is a separate mutation,
    // preventing a cross-core partial commit.
    mpMatchOperationRequest_t target = MakeRequest( MP_MATCH_OP_ABORT, session, generation );
    target.argumentCount = 1;
    target.arguments[ 0 ].fieldId = MP_MATCH_ARG_REASON;
    CHECK( target.arguments[ 0 ].value.SetString( "contract abort" ) );
    mpProposalCreateParams_t create;
    create.Clear();
    create.sessionId = session.GetSessionId();
    create.proposalId = 7;
    create.scope = MP_PROPOSAL_SCOPE_GLOBAL;
    create.electorateCount = 1;
    create.electorate[ 0 ].participant = player.SequencePart();
    create.electorate[ 0 ].human = true;
    create.requiredQuorum = 1;
    create.requiredYes = 1;
    create.createdAt = mpProposalEngineTime::FromMilliseconds( 1 );
    create.expiresAt = mpProposalEngineTime::FromMilliseconds( 1001 );
    create.caller = player.SequencePart();
    create.callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_YES;
    create.operation = target;
    CHECK( proposals.Create( create, proposals.GetRevision() ).WasApplied() );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->status ==
        MP_PROPOSAL_STATUS_PASSED );

    // An unrelated session change inside the voting window - here a bot
    // joining - advances the session revision. It must not void the ballot.
    const uint64_t ballotCreationRevision = session.GetSessionRevision();
    mpParticipantId lateBot;
    CHECK( session.BindParticipant( 6, false, MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ),
        session.GetSessionRevision(), lateBot ).WasApplied() );
    CHECK( session.GetSessionRevision() != ballotCreationRevision );
    CHECK( session.GetPhase() == COUNTDOWN );

    context = MakeContext( rules, proposals, series );
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_GLOBAL, 7,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( result.continuation.kind ==
        MP_OPERATION_CONTINUATION_PROPOSAL_ACKNOWLEDGE );
    CHECK( session.GetPhase() == WARMUP );
    CHECK( proposals.GetRevision() == context.expectedProposalRevision );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->status ==
        MP_PROPOSAL_STATUS_PASSED );
    Fingerprint afterTarget = Snapshot( session, rules, proposals, series );

    // Revalidation also refuses a second run: the aborted match is no longer in
    // a phase the operation allows. Once-only execution itself belongs to the
    // adapter, which acknowledges every executed ballot.
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_GLOBAL, 7,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.reason == MP_OPERATION_REASON_WRONG_PHASE );
    CHECK( Same( afterTarget, Snapshot( session, rules, proposals, series ) ) );

    result = executor.AcknowledgePassedProposal( MP_PROPOSAL_SCOPE_GLOBAL, 7,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( proposals.GetRevision() == context.expectedProposalRevision + 1 );
    CHECK( proposals.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL ) != NULL );
    CHECK( !proposals.GetProposal( MP_PROPOSAL_SCOPE_GLOBAL )->IsOccupied() );

    // Series profile selection is descriptor-backed. Unknown keys and a
    // contradictory redundant best-of value fail before the adapter boundary.
    CHECK( session.SetParticipantActive( player, false,
        session.GetSessionRevision() ).WasApplied() );
    policy.teamMode = true;
    policy.maximumActivePerSide = 8;
    policy.requiredSideMask = 3;
    CHECK( session.ConfigureReadiness( policy,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( session.SetParticipantSide( player, 0,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( session.SetParticipantActive( player, true,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( session.SetParticipantRoles( player,
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
        MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ),
        session.GetSessionRevision() ).WasApplied() );
    mpMatchOperationRequest_t stage = MakeRequest(
        MP_MATCH_OP_SERIES_STAGE_PROFILE, session, generation );
    AddSeriesProfile( stage, "missing_profile", 3 );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    result = executor.Execute( stage, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.seriesReason == MP_SERIES_REASON_UNKNOWN_PROFILE );
    stage = MakeRequest( MP_MATCH_OP_SERIES_STAGE_PROFILE, session, generation );
    AddSeriesProfile( stage, "best_of_three", 5 );
    result = executor.Execute( stage, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.seriesReason == MP_SERIES_REASON_PROFILE_BEST_OF_MISMATCH );
    stage = MakeRequest( MP_MATCH_OP_SERIES_STAGE_PROFILE, session, generation );
    AddSeriesProfile( stage, "BEST_OF_THREE", 3 );
    result = executor.Execute( stage, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind ==
        MP_OPERATION_CONTINUATION_SERIES_CONFIGURE_PROFILE );

    // Starting-side selection crosses the protocol/session/series policy
    // domains explicitly and consumes exactly one independent series revision.
    const char *singleMapPool[] = { "mp/q4dm1" };
    mpSeriesConfiguration singleMap;
    mpSeriesReason_t buildReason = MP_SERIES_REASON_COUNT;
    CHECK( MPSeriesBuildProfileDraft( MP_SERIES_PROFILE_BEST_OF_ONE, 2,
		0x77, 0, true, singleMapPool, 1, singleMap, buildReason ) );
    CHECK( series.Configure( singleMap, series.GetRevision() ).WasApplied() );
    CHECK( series.Start( series.GetRevision() ).WasApplied() );

    mpParticipantId sideOnePlayer;
    CHECK( session.BindParticipant( 1, true,
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
        MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ),
        session.GetSessionRevision(), sideOnePlayer ).WasApplied() );
    unsigned int sideOneGeneration = 0;
    CHECK( session.GetSlotGeneration( 1, sideOneGeneration ) );
    CHECK( session.SetParticipantSide( sideOnePlayer, 1,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( session.SetParticipantActive( sideOnePlayer, true,
        session.GetSessionRevision() ).WasApplied() );

    mpMatchOperationRequest_t decider = MakeRequest(
        MP_MATCH_OP_VETO_SELECT, session, generation );
    AddVeto( decider, MP_MATCH_VETO_DECIDER, "mp/q4dm1" );
    context = MakeContext( rules, proposals, series );
    const uint64_t beforeDeciderRevision = series.GetRevision();
    // The turn is held to the series side the adapter resolved for the view,
    // never to the gameplay side: without one, or with the other side's, the
    // same captain is refused and the series does not move.
    result = executor.Execute( decider, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.reason == MP_OPERATION_REASON_TARGET_ALIGNMENT );
    CHECK( series.GetRevision() == beforeDeciderRevision );
    context.actorSeriesSide = 1;
    result = executor.Execute( decider, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( series.GetRevision() == beforeDeciderRevision );
    context.actorSeriesSide = 0;
    result = executor.Execute( decider, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_SERIES_PERSIST );
    CHECK( series.GetRevision() == beforeDeciderRevision + 1 );
    CHECK( series.GetState() == MP_SERIES_VETO );

    mpMatchOperationRequest_t missingStartingSide = MakeRequest(
        MP_MATCH_OP_VETO_SELECT, session, sideOneGeneration );
    missingStartingSide.actorSlot = 1;
    AddVeto( missingStartingSide, MP_MATCH_VETO_SIDE, "mp/q4dm1" );
    context = MakeContext( rules, proposals, series );
    const uint64_t beforeMalformedSide = series.GetRevision();
    result = executor.Execute( missingStartingSide, context,
        session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( result.reason == MP_OPERATION_REASON_PROTOCOL );
    CHECK( series.GetRevision() == beforeMalformedSide );

    mpMatchOperationRequest_t chooseSide = MakeRequest(
        MP_MATCH_OP_VETO_SELECT, session, sideOneGeneration );
    chooseSide.actorSlot = 1;
    AddVeto( chooseSide, MP_MATCH_VETO_SIDE, "mp/q4dm1",
        MP_MATCH_STARTING_SIDE_STROGG );
    context = MakeContext( rules, proposals, series );
    context.trustedTransportSlot = 1;
    // Gameplay side 1 is not enough when the series maps this seat to side 0.
    context.actorSeriesSide = 0;
    result = executor.Execute( chooseSide, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_REJECTED );
    CHECK( series.GetRevision() == beforeMalformedSide );
    context.actorSeriesSide = 1;
    result = executor.Execute( chooseSide, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( series.GetRevision() == beforeMalformedSide + 1 );
    CHECK( series.GetState() == MP_SERIES_READY );
    CHECK( series.GetSelectedMap( 0 )->hasStartingGameSide );
    CHECK( series.GetSelectedMap( 0 )->startingGameSide == 1 );
    CHECK( series.GetSelectedMap( 0 )->gameSideChosenBy == 1 );

    // Broadcaster access is an operator-only, idempotent role mutation for a
    // current inactive, neutral, unrostered human. It never combines tactical
    // audiences or grants server/referee capabilities.
    mpParticipantId broadcastTarget;
    CHECK( session.BindParticipant( 2, true,
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
        broadcastTarget ).WasApplied() );
    CHECK( MPOperationBroadcasterTargetIsValid( session, broadcastTarget ) );
    mpMatchOperationRequest_t broadcast = MakeRequest(
        MP_MATCH_OP_BROADCASTER_SET, session, generation );
    broadcast.hasParticipantTarget = true;
    broadcast.participantTarget = broadcastTarget.SequencePart();
    AddEnabled( broadcast, true );
    context = MakeContext( rules, proposals, series );
    result = executor.Execute( broadcast, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
    context.localOperator = true;
    result = executor.Execute( broadcast, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( session.FindParticipant( broadcastTarget )->roles ==
        MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ) );
    broadcast = MakeRequest( MP_MATCH_OP_BROADCASTER_SET, session, generation );
    broadcast.hasParticipantTarget = true;
    broadcast.participantTarget = broadcastTarget.SequencePart();
    AddEnabled( broadcast, true );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    result = executor.Execute( broadcast, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NO_CHANGE );
    broadcast = MakeRequest( MP_MATCH_OP_BROADCASTER_SET, session, generation );
    broadcast.hasParticipantTarget = true;
    broadcast.participantTarget = broadcastTarget.SequencePart();
    AddEnabled( broadcast, false );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    result = executor.Execute( broadcast, context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( session.FindParticipant( broadcastTarget )->roles ==
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) );
    CHECK( session.SetParticipantActive( broadcastTarget, true,
        session.GetSessionRevision() ).WasApplied() );
    CHECK( !MPOperationBroadcasterTargetIsValid( session, broadcastTarget ) );
    broadcast = MakeRequest( MP_MATCH_OP_BROADCASTER_SET, session, generation );
    broadcast.hasParticipantTarget = true;
    broadcast.participantTarget = broadcastTarget.SequencePart();
    AddEnabled( broadcast, true );
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    result = executor.Execute( broadcast, context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_TARGET_ALIGNMENT );
    CHECK( session.SetParticipantActive( broadcastTarget, false,
        session.GetSessionRevision() ).WasApplied() );

    // Coach and substitute roles deliberately lack generic team/join authority,
    // but every rostered human owns the exact targetless right to withdraw.
    mpMatchSession leaveSession;
    CHECK( leaveSession.Reset( 0x99887766ULL,
        mpMatchEngineTime::FromMilliseconds( 0 ) ) );
    CHECK( leaveSession.TransitionPhase( WARMUP,
        MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
        leaveSession.GetSessionRevision() ).WasApplied() );
    mpMatchReadinessPolicy leavePolicy;
    leavePolicy.policy = MP_MATCH_READY_DISABLED;
    leavePolicy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
    leavePolicy.teamMode = true;
    leavePolicy.minimumActiveHumans = 0;
    leavePolicy.readyThresholdBasisPoints = 0;
    leavePolicy.maximumActivePerSide = 1;
    leavePolicy.requiredSideMask = 0;
    leavePolicy.requireDeclaredRosterSeats = false;
    CHECK( leaveSession.ConfigureReadiness( leavePolicy,
        leaveSession.GetSessionRevision() ).WasApplied() );
    mpParticipantId coach;
    CHECK( leaveSession.BindParticipant( 0, true,
        MPMatchRoleBit( MP_MATCH_ROLE_COACH ), leaveSession.GetSessionRevision(),
        coach ).WasApplied() );
    CHECK( leaveSession.SetParticipantSide( coach, 0,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.DeclareRosterSeat( 0, 0, MP_MATCH_ROSTER_COACH,
        false, leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.AssignRosterSeat( 0, coach,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.CanSelfLeaveRoster( coach ) );
    unsigned int leaveGeneration = 0;
    CHECK( leaveSession.GetSlotGeneration( 0, leaveGeneration ) );
    context = MakeContext( rules, proposals, series );
    mpMatchOperationRequest_t unauthorizedJoin = MakeRequest(
        MP_MATCH_OP_TEAM_JOIN, leaveSession, leaveGeneration );
    unauthorizedJoin.hasTeamTarget = true;
    unauthorizedJoin.teamTarget = MP_MATCH_TEAM_MARINE;
    result = executor.Execute( unauthorizedJoin, context, leaveSession,
        rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
    CHECK( leaveSession.FreezeRules( 1, 0x2002,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.TransitionPhase( COUNTDOWN,
        MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
        leaveSession.GetSessionRevision() ).WasApplied() );
    context = MakeContext( rules, proposals, series );
    mpMatchOperationRequest_t leave = MakeRequest(
        MP_MATCH_OP_ROSTER_LEAVE, leaveSession, leaveGeneration );
    Fingerprint beforeLeave = Snapshot( leaveSession, rules, proposals, series );
    result = executor.Execute( leave, context, leaveSession, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_ROSTER_LEAVE );
    CHECK( result.continuation.participant == coach );
    CHECK( Same( beforeLeave, Snapshot( leaveSession, rules, proposals, series ) ) );
    CHECK( leaveSession.Reset( 0x99887767ULL,
        mpMatchEngineTime::FromMilliseconds( 0 ) ) );
    CHECK( leaveSession.TransitionPhase( WARMUP,
        MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.ConfigureReadiness( leavePolicy,
        leaveSession.GetSessionRevision() ).WasApplied() );
    mpParticipantId substituteActor;
    CHECK( leaveSession.BindParticipant( 0, true, 0,
        leaveSession.GetSessionRevision(), substituteActor ).WasApplied() );
    CHECK( leaveSession.SetParticipantSide( substituteActor, 1,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.DeclareRosterSeat( 0, 1, MP_MATCH_ROSTER_SUBSTITUTE,
        false, leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.AssignRosterSeat( 0, substituteActor,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.CanSelfLeaveRoster( substituteActor ) );
    CHECK( leaveSession.GetSlotGeneration( 0, leaveGeneration ) );
    context = MakeContext( rules, proposals, series );
    unauthorizedJoin = MakeRequest( MP_MATCH_OP_TEAM_JOIN, leaveSession,
        leaveGeneration );
    unauthorizedJoin.hasTeamTarget = true;
    unauthorizedJoin.teamTarget = MP_MATCH_TEAM_SPECTATOR;
    result = executor.Execute( unauthorizedJoin, context, leaveSession,
        rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
    CHECK( leaveSession.FreezeRules( 1, 0x2003,
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.TransitionPhase( COUNTDOWN,
        MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
        leaveSession.GetSessionRevision() ).WasApplied() );
    CHECK( leaveSession.GetSlotGeneration( 0, leaveGeneration ) );
    context = MakeContext( rules, proposals, series );
    leave = MakeRequest( MP_MATCH_OP_ROSTER_LEAVE, leaveSession,
        leaveGeneration );
    beforeLeave = Snapshot( leaveSession, rules, proposals, series );
    result = executor.Execute( leave, context, leaveSession, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_ROSTER_LEAVE );
    CHECK( result.continuation.participant == substituteActor );
    CHECK( Same( beforeLeave, Snapshot( leaveSession, rules, proposals, series ) ) );
    mpParticipantId unrostered;
    CHECK( leaveSession.BindParticipant( 1, true,
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), leaveSession.GetSessionRevision(),
        unrostered ).WasApplied() );
    unsigned int unrosteredGeneration = 0;
    CHECK( leaveSession.GetSlotGeneration( 1, unrosteredGeneration ) );
    leave = MakeRequest( MP_MATCH_OP_ROSTER_LEAVE, leaveSession,
        unrosteredGeneration );
    leave.actorSlot = 1;
    context.trustedTransportSlot = 1;
    result = executor.Execute( leave, context, leaveSession, rules, proposals, series );
    // Unrostered/active participants never receive the self-withdrawal
    // capability, so they fail at the common authority boundary before the
    // operation-specific roster alignment guard.
    CHECK( result.reason == MP_OPERATION_REASON_NOT_AUTHORIZED );
    CHECK( leaveSession.ValidateInvariants() );

    // Review advancement is an exact phase/state transition. A next-map
    // preflight leaves the original series untouched; a deciding map commits
    // COMPLETE once and returns a persistence continuation.
    mpMatchSession reviewSession = session;
    CHECK( reviewSession.BeginCountdown( 1, 0x1001,
        MP_MATCH_TRANSITION_REFEREE_FORCE_READY, player,
        reviewSession.GetSessionRevision() ).WasApplied() );
    CHECK( reviewSession.TransitionPhase( GAMEON,
        MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE, player,
        reviewSession.GetSessionRevision() ).WasApplied() );
    CHECK( reviewSession.TransitionPhase( GAMEREVIEW,
        MP_MATCH_TRANSITION_LIMIT_REACHED, player,
        reviewSession.GetSessionRevision() ).WasApplied() );
    const char *bestOfThreeMaps[] = { "mp/q4dm1", "mp/q4dm2", "mp/q4dm3" };
    mpCompetitionSeries nextMapSeries;
    CHECK( BuildMapCompleteSeries( MP_SERIES_PROFILE_BEST_OF_THREE,
        bestOfThreeMaps, 3, nextMapSeries ) );
    mpMatchOperationRequest_t advance = MakeRequest(
        MP_MATCH_OP_SERIES_ADVANCE, reviewSession, generation );
    context = MakeContext( rules, proposals, nextMapSeries );
    context.localOperator = true;
    const uint64_t beforeNextMapRevision = nextMapSeries.GetRevision();
    result = executor.Execute( advance, context, reviewSession, rules,
        proposals, nextMapSeries );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind ==
        MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP );
    CHECK( result.continuation.mapToken[ 0 ] != '\0' );
    CHECK( nextMapSeries.GetRevision() == beforeNextMapRevision &&
        nextMapSeries.GetState() == MP_SERIES_MAP_COMPLETE );

    advance = MakeRequest( MP_MATCH_OP_SERIES_ADVANCE, session, generation );
    context = MakeContext( rules, proposals, nextMapSeries );
    context.localOperator = true;
    result = executor.Execute( advance, context, session, rules,
        proposals, nextMapSeries );
    CHECK( result.reason == MP_OPERATION_REASON_SERIES_STATE );
    CHECK( nextMapSeries.GetRevision() == beforeNextMapRevision );

    // Only a validated restored review permits a completed-map checkpoint
    // to advance from a new warmup. The adapter preflight remains immutable.
    context.seriesReviewRecovered = true;
    result = executor.Execute( advance, context, session, rules,
        proposals, nextMapSeries );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind ==
        MP_OPERATION_CONTINUATION_SERIES_ADVANCE_AND_LOAD_MAP );
    CHECK( nextMapSeries.GetRevision() == beforeNextMapRevision &&
        nextMapSeries.GetState() == MP_SERIES_MAP_COMPLETE );

    const char *bestOfOneMaps[] = { "mp/q4dm1" };
    mpCompetitionSeries completeSeries;
    CHECK( BuildMapCompleteSeries( MP_SERIES_PROFILE_BEST_OF_ONE,
        bestOfOneMaps, 1, completeSeries ) );
    advance = MakeRequest( MP_MATCH_OP_SERIES_ADVANCE, reviewSession, generation );
    context = MakeContext( rules, proposals, completeSeries );
    context.localOperator = true;
    const uint64_t beforeCompleteRevision = completeSeries.GetRevision();
    result = executor.Execute( advance, context, reviewSession, rules,
        proposals, completeSeries );
    CHECK( result.outcome == MP_OPERATION_APPLIED );
    CHECK( result.continuation.kind == MP_OPERATION_CONTINUATION_SERIES_PERSIST );
    CHECK( completeSeries.GetState() == MP_SERIES_COMPLETE &&
        completeSeries.GetRevision() == beforeCompleteRevision + 1 );

    // Duel binding accepts exactly the two protocol enum values, never a raw
    // team or slot. Every accepted result is a pre-commit stable-id
    // continuation and all 254 hostile byte values leave every core unchanged.
    mpSeriesConfiguration duelConfiguration;
    mpSeriesReason_t duelReason = MP_SERIES_REASON_COUNT;
    CHECK( MPSeriesBuildProfileDraft( MP_SERIES_PROFILE_BEST_OF_ONE,
        GAME_DUEL, 0x2718u, 0, false, bestOfOneMaps, 1,
        duelConfiguration, duelReason ) );
    mpCompetitionSeries bindingSeries;
    CHECK( bindingSeries.Configure( duelConfiguration,
        bindingSeries.GetRevision() ).WasApplied() );
    for ( unsigned int raw = 0; raw < 256; ++raw ) {
        mpMatchOperationRequest_t bind = MakeRequest(
            MP_MATCH_OP_SERIES_CONTESTANT_BIND, session, generation );
        bind.hasParticipantTarget = true;
        bind.participantTarget = sideOnePlayer.SequencePart();
        AddCompetitionSide( bind, static_cast<unsigned short>( raw ) );
        context = MakeContext( rules, proposals, bindingSeries );
        context.localOperator = true;
        context.ruleGameType = GAME_DUEL;
        const Fingerprint bindingBefore = Snapshot( session, rules,
            proposals, bindingSeries );
        result = executor.Execute( bind, context, session, rules,
            proposals, bindingSeries );
        if ( raw == MP_MATCH_COMPETITION_SIDE_A ||
                raw == MP_MATCH_COMPETITION_SIDE_B ) {
            CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
            CHECK( result.continuation.kind ==
                MP_OPERATION_CONTINUATION_SERIES_CONTESTANT_BIND );
            CHECK( result.continuation.participant == sideOnePlayer );
            CHECK( result.continuation.side ==
                ( raw == MP_MATCH_COMPETITION_SIDE_A ? 0 : 1 ) );
        } else {
            CHECK( result.outcome == MP_OPERATION_REJECTED &&
                result.reason == MP_OPERATION_REASON_PROTOCOL );
        }
        CHECK( Same( bindingBefore, Snapshot( session, rules,
            proposals, bindingSeries ) ) );
    }

    // A passed participant-removal proposal re-resolves the stable identity.
    // Reusing its old slot cannot redirect the continuation to the newcomer.
    mpMatchOperationRequest_t removal = MakeRequest(
        MP_MATCH_OP_PARTICIPANT_REMOVE, session, generation );
    removal.hasParticipantTarget = true;
    removal.participantTarget = broadcastTarget.SequencePart();
    mpProposalCooldownPolicy_t removalCooldowns;
    removalCooldowns.Clear();
    CHECK( proposals.Reset( session.GetSessionId(),
        mpProposalEngineTime::FromMilliseconds( 100 ), removalCooldowns ) );
    mpProposalCreateParams_t removalCreate;
    removalCreate.Clear();
    removalCreate.sessionId = session.GetSessionId();
    removalCreate.proposalId = 77;
    removalCreate.scope = MP_PROPOSAL_SCOPE_GLOBAL;
    removalCreate.electorateCount = 1;
    removalCreate.electorate[ 0 ].participant = player.SequencePart();
    removalCreate.electorate[ 0 ].human = true;
    removalCreate.requiredQuorum = 1;
    removalCreate.requiredYes = 1;
    removalCreate.createdAt = mpProposalEngineTime::FromMilliseconds( 101 );
    removalCreate.expiresAt = mpProposalEngineTime::FromMilliseconds( 1101 );
    removalCreate.caller = player.SequencePart();
    removalCreate.callerVotePolicy = MP_PROPOSAL_CALLER_VOTE_YES;
    removalCreate.operation = removal;
    CHECK( proposals.Create( removalCreate,
        proposals.GetRevision() ).WasApplied() );
    context = MakeContext( rules, proposals, series );
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_GLOBAL, 77,
        context, session, rules, proposals, series );
    CHECK( result.outcome == MP_OPERATION_NEEDS_ADAPTER );
    CHECK( result.continuation.kind ==
        MP_OPERATION_CONTINUATION_PARTICIPANT_REMOVE );
    CHECK( result.continuation.participant == broadcastTarget );
    CHECK( result.continuation.proposalId == 77 );

    mpMatchOperationRequest_t selfRemoval = MakeRequest(
        MP_MATCH_OP_PARTICIPANT_REMOVE, session, generation );
    selfRemoval.hasParticipantTarget = true;
    selfRemoval.participantTarget = player.SequencePart();
    context = MakeContext( rules, proposals, series );
    context.localOperator = true;
    const Fingerprint beforeSelfRemoval = Snapshot( session, rules,
        proposals, series );
    result = executor.Execute( selfRemoval, context, session, rules,
        proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_TARGET_ALIGNMENT );
    CHECK( Same( beforeSelfRemoval, Snapshot( session, rules,
        proposals, series ) ) );

    unsigned int removedGeneration = 0;
    CHECK( session.GetSlotGeneration( 2, removedGeneration ) );
    CHECK( session.UnbindParticipant( 2, removedGeneration,
        session.GetSessionRevision() ).WasApplied() );
    mpParticipantId replacementOccupant;
    CHECK( session.BindParticipant( 2, true,
        MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ), session.GetSessionRevision(),
        replacementOccupant ).WasApplied() );
    context = MakeContext( rules, proposals, series );
    const Fingerprint beforeReusedSlot = Snapshot( session, rules,
        proposals, series );
    result = executor.ExecutePassedProposal( MP_PROPOSAL_SCOPE_GLOBAL, 77,
        context, session, rules, proposals, series );
    CHECK( result.reason == MP_OPERATION_REASON_TARGET_UNKNOWN );
    CHECK( result.continuation.participant != replacementOccupant );
    CHECK( Same( beforeReusedSlot, Snapshot( session, rules,
        proposals, series ) ) );

    CHECK( session.ValidateInvariants() );
    CHECK( proposals.ValidateInvariants() );
    CHECK( series.ValidateInvariants() );
    return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_operations_contract: executable checks skipped (no C++ compiler)")
        return

    with tempfile.TemporaryDirectory(prefix="mp-match-operations-") as directory:
        temp = Path(directory)
        harness = temp / "match_operations_contract.cpp"
        executable = temp / (
            "match_operations_contract.exe" if compiler.lower().endswith(".exe") else
            "match_operations_contract"
        )
        harness.write_text(SUPPORT_AND_TEST, encoding="utf-8", newline="\n")
        command = [
            compiler,
            "-std=c++11",
            "-DMP_MATCH_OPERATIONS_STANDALONE_TEST",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            "-DMP_MATCH_SERIES_STANDALONE_TEST",
            "-DMP_PROPOSAL_STANDALONE_TEST",
            "-I",
            str(ROOT),
            str(harness),
            str(SOURCE),
            str(ROOT / "src/mpgame/mp/match/MatchSession.cpp"),
            str(ROOT / "src/mpgame/mp/match/MatchProposal.cpp"),
            str(ROOT / "src/mpgame/mp/match/MatchSeries.cpp"),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-operation contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "hostile match-operation contract failed:\n" + ran.stdout + ran.stderr
            )


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_operations_contract: PASS")


if __name__ == "__main__":
    main()
