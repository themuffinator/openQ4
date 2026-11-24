#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for recipient-authorized match views."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "src/game/mp/match/MatchView.h"
SOURCE_PATH = ROOT / "src/game/mp/match/MatchView.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature}")
    open_brace = source.find("{", start)
    if open_brace < 0:
        raise AssertionError(f"missing body for {signature}")
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unterminated body for {signature}")


def struct_body(header: str, tag: str) -> str:
    match = re.search(
        rf"typedef struct {re.escape(tag)}\s*\{{(?P<body>.*?)\n\}}\s*\w+\s*;",
        header,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"could not find struct {tag}")
    return match.group("body")


def schema_contract(header: str, source: str) -> None:
    for token in (
        "MP_MATCH_VIEW_SCHEMA_VERSION = 4",
        "MP_MATCH_VIEW_MAX_MESSAGE_BYTES = 7936",
        "MP_MATCH_VIEW_MAX_TOP_LEVEL_FIELDS = 26",
        "MP_MATCH_VIEW_MAX_PARTICIPANTS = 32",
        "MP_MATCH_VIEW_MAX_ROSTER_SEATS = 32",
        "MP_MATCH_VIEW_MAX_RULE_FIELDS = 64",
        "MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES = 64",
        "MP_MATCH_VIEW_MAX_SERIES_MAP_POOL = 32",
        "MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY = 64",
        "MP_MATCH_VIEW_MAX_SERIES_MAP_HISTORY = 64",
        "MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS = 256",
        "MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS = 4",
        "8192-byte game",
        "typedef unsigned long long mpMatchViewAllowedOperationMask_t",
        "mpMatchProtocolRevision_t\tcontrolRevision",
        "mpMatchProtocolRevision_t\tviewRevision",
        "mpMatchViewParticipantSummary_t",
        "mpMatchViewCommittedRules_t",
        "mpMatchViewOperationAvailability_t",
        "mpMatchViewInvitationSummary_t",
        "mpMatchViewQueueEntry_t",
        "mpMatchViewVetoHistory_t",
        "mpMatchViewSeriesMapHistory_t",
        "mpMatchViewEvidenceSummary_t",
        "competitionSide",
        "seriesId",
    ):
        require(header, token, "bounded version-4 view schema")

    field_block = re.search(
        r"typedef enum \{(?P<body>\s*MP_MATCH_VIEW_FIELD_SCHEMA.*?"
        r"MP_MATCH_VIEW_FIELD_TERMINAL_RESULT\s*=\s*25\s*)\}\s*mpMatchViewField_t;",
        source,
        re.DOTALL,
    )
    if field_block is None:
        raise AssertionError("could not locate required view-field schema")
    assignments = re.findall(
        r"MP_MATCH_VIEW_FIELD_[A-Z_]+\s*=\s*(\d+)", field_block.group("body")
    )
    if [int(value) for value in assignments] != list(range(1, 26)):
        raise AssertionError(f"required view fields are not append-only 1..25: {assignments}")
    require(source, "MP_MATCH_VIEW_REQUIRED_FIELD_COUNT = 25", "required field count")
    require(source, "static_assert( MP_MATCH_OP_COUNT <= 64", "operation mask ceiling")
    require(source, "static_cast<mpMatchProtocolSessionId_t>( sessionHigh ) << 32",
            "full-width session decode")


def truthful_control_contract(header: str, source: str) -> None:
    for token in (
        "readyEligible",
        "queuePosition",
        "teamReady",
        "locked",
        "participantSummaries",
        "human",
        "publicRoleMask",
        "globalProposal",
        "ownSideProposal",
        "callerParticipantId",
        "recipientEligible",
        "recipientBallot",
        "committedRules",
        "stagedRules",
        "changedFieldMask",
        "operationAvailabilityCount",
        "MPMatchViewSetOperationDecision",
        "MP_MATCH_PROTOCOL_REASON_OK",
        "rosterSeats",
        "invitations",
        "queueEntries",
        "mapPool",
        "vetoHistory",
        "mapHistory",
        "hasVetoTurn",
        "vetoTurnSide",
        "resumePolicy",
        "resumeRequiredSideMask",
        "resumeConsentingSideMask",
        "resumeConsented",
        "competitionSide",
        "seriesId",
        "evidenceRevision",
        "droppedRecordCount",
        "recentEventKinds",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1",
    ):
        require(header + source, token, "truthful Match Control projection")

    availability = function_body(source, "static bool ValidateOperationAvailability")
    require(availability, "operationAvailabilityCount != MP_MATCH_OP_COUNT - 1",
            "complete operation decision table")
    require(availability, "availability.opcode != static_cast<mpMatchOperationOpcode_t>( i + 1 )",
            "canonical operation ordering")
    require(availability, "reconstructed != state.allowedOperations",
            "mask/decision consistency")


def authorization_contract(header: str, source: str) -> None:
    final_view = struct_body(header, "mpSessionView_s")
    for token in (
        "mpMatchViewProposalCandidate_t",
        "mpMatchViewStagedRulesCandidate_t",
        "mpMatchViewRosterSeatCandidate_t",
        "mpMatchViewInvitationCandidate_t",
        "mpMatchViewQueueEntryCandidate_t",
        "mpMatchViewObserverCandidate_t",
    ):
        require(header, token, "source-side private candidate types")
    for token in ("authorization", "audienceSide", "audienceParticipantId", "Candidate"):
        reject(final_view, token, "serialized view must not retain authorization metadata")

    evidence = struct_body(header, "mpMatchViewEvidenceSummary_s")
    for token in ("qpath", "fileName", "directory", "backendReason", "backendText"):
        reject(evidence, token, "evidence projection must not expose server-local output data")

    build = function_body(source, "bool MPMatchViewBuild")
    require(build, "knownAudiences", "closed audience mask")
    require(build, "knownKinds", "closed observer-kind mask")
    require(build, "policy.recipientId != source.publicState.recipient.participantId",
            "recipient binding")
    require_before(build, "ValidateObserverCandidate", "CandidateAuthorized",
                   "all observer candidates validated before filtering")
    require_before(build, "ValidateAuthorizationTag", "CandidateAuthorized",
                   "all private candidates validated before filtering")
    require_before(build, "MPMatchViewValidate( built", "view = built",
                   "transactional recipient projection")
    require(source, "tag.audienceSide == policy.ownSide", "own-side isolation")
    require(source, "tag.audienceParticipantId == policy.recipientId",
            "recipient-only isolation")

    combined = header + source
    for pattern in (
        r"\bcredential(?:s)?\b",
        r"\bipAddress\b",
        r"\bremoteAddress\b",
        r"\bfileSystem\b",
    ):
        if re.search(pattern, combined, re.IGNORECASE):
            raise AssertionError(f"view boundary contains forbidden sensitive field {pattern!r}")


def codec_contract(header: str, source: str) -> None:
    for forbidden in (
        "MatchSession.h",
        "MatchSeries.h",
        "MatchRules.h",
        "MatchTeam.h",
        "Game_local.h",
        "MultiplayerGame.h",
        "gameLocal.",
        "cvarSystem",
        "cmdSystem",
        "BufferCommandText",
        "ExecuteCommandText",
        "WriteString(",
        "ReadString(",
        "idList<",
        "idStr ",
        "new mpMatch",
    ):
        reject(header + source, forbidden, "neutral bounded view boundary")

    for token in (
        "MP_MATCH_ENVELOPE_SESSION_VIEW",
        "MP_MATCH_VIEW_OPTIONAL_EXTENSION_BIT",
        "MP_MATCH_VIEW_FIELD_ID_MASK",
        "MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD",
        "MP_MATCH_VIEW_ERROR_MISSING_REQUIRED_FIELD",
        "MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD",
        "MP_MATCH_VIEW_ERROR_TRAILING_DATA",
        "MP_MATCH_VIEW_ERROR_TRUNCATED",
        "MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE",
        "MP_MATCH_VIEW_ERROR_INVALID_STRING",
        "SaveReadState",
        "RestoreReadState",
        "SaveWriteState",
        "RestoreWriteState",
        "GetRemainingReadBits",
        "GetRemainingWriteBits",
        "message.GetWriteBit() != 0",
        "message.GetReadBit() != 0",
        "mpSessionView decoded",
        "view = decoded",
    ):
        require(source, token, "transactional fail-closed codec")

    decode = function_body(source, "bool MPMatchViewDecode")
    require_before(decode, "DecodePayload", "view = decoded", "decode then publish")
    require_before(decode, "MPMatchViewValidate( decoded", "view = decoded",
                   "validate then publish")
    require(source, "fieldId <= MP_MATCH_VIEW_FIELD_TERMINAL_RESULT",
            "closed required-field range")
    require(source, "!known && ( rawTag & MP_MATCH_VIEW_OPTIONAL_EXTENSION_BIT ) == 0",
            "unknown required fields fail closed")
    require(source, "if ( !seen[ fieldId ] )", "all required fields enforced")


def freshness_contract(source: str) -> None:
    accept = function_body(source, "mpMatchViewAcceptResult_t MPMatchViewAccept")
    require_before(accept, "MPMatchViewValidate( incoming", "current = incoming",
                   "validate before replacement")
    require_before(accept, "sessionId != incoming.publicState.sessionId", "current = incoming",
                   "new-session replacement")
    require_before(accept, "viewRevision < current.publicState.viewRevision",
                   "MP_MATCH_VIEW_ACCEPT_REJECTED_STALE", "stale rejection")
    require_before(accept, "viewRevision == current.publicState.viewRevision",
                   "MP_MATCH_VIEW_ACCEPT_NO_CHANGE", "equal-revision immutability")
    if accept.count("current = incoming") != 2:
        raise AssertionError("incoming view may publish only for new sessions or newer revisions")


def source_listing_contract() -> None:
    listing = subprocess.run(
        [
            "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "game/mp/match/MatchView.cpp" not in {line.strip() for line in listing}:
        raise AssertionError("MatchView.cpp is absent from the canonical MP source list")


HARNESS = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

typedef unsigned char byte;

class idBitMsg {
public:
    idBitMsg() : data(0), capacity(0), size(0), readCount(0), overflowed(false), allowOverflow(false) {}
    void Init(byte *value, int bytes) { data = value; capacity = bytes; size = 0; readCount = 0; overflowed = false; }
    void Init(const byte *value, int bytes) { data = const_cast<byte *>(value); capacity = bytes; size = bytes; readCount = 0; overflowed = false; }
    void SetAllowOverflow(bool value) { allowOverflow = value; }
    void BeginWriting() { size = 0; readCount = 0; overflowed = false; }
    void BeginReading() const { readCount = 0; }
    void SetSize(int value) { size = value; }
    int GetSize() const { return size; }
    const byte *GetData() const { return data; }
    bool IsOverflowed() const { return overflowed; }
    int GetWriteBit() const { return 0; }
    int GetReadBit() const { return 0; }
    int GetRemainingWriteBits() const { return (capacity - size) * 8; }
    int GetRemainingReadBits() const { return (size - readCount) * 8; }
    void SaveWriteState(int &savedSize, int &savedBit) const { savedSize = size; savedBit = 0; }
    void RestoreWriteState(int savedSize, int) { size = savedSize; overflowed = false; }
    void SaveReadState(int &savedCount, int &savedBit) const { savedCount = readCount; savedBit = 0; }
    void RestoreReadState(int savedCount, int) const { readCount = savedCount; }
    void WriteByte(int value) { byte raw = static_cast<byte>(value); WriteData(&raw, 1); }
    void WriteUShort(int value) { byte raw[2] = { static_cast<byte>(value), static_cast<byte>(value >> 8) }; WriteData(raw, 2); }
    void WriteLong(int value) { uint32_t rawValue = static_cast<uint32_t>(value); byte raw[4] = { static_cast<byte>(rawValue), static_cast<byte>(rawValue >> 8), static_cast<byte>(rawValue >> 16), static_cast<byte>(rawValue >> 24) }; WriteData(raw, 4); }
    void WriteData(const void *source, int bytes) {
        if (bytes < 0 || size > capacity - bytes) { overflowed = true; if (!allowOverflow) return; size = capacity; return; }
        memcpy(data + size, source, bytes); size += bytes;
    }
    int ReadByte() const { byte raw = 0; return ReadData(&raw, 1) == 1 ? raw : 0; }
    int ReadUShort() const { byte raw[2] = {0, 0}; return ReadData(raw, 2) == 2 ? raw[0] | (raw[1] << 8) : 0; }
    int ReadLong() const { byte raw[4] = {0, 0, 0, 0}; return ReadData(raw, 4) == 4 ? static_cast<int>(static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) | (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24)) : 0; }
    int ReadData(void *target, int bytes) const {
        if (bytes < 0 || readCount > size - bytes) { overflowed = true; return 0; }
        memcpy(target, data + readCount, bytes); readCount += bytes; return bytes;
    }
private:
    byte *data;
    int capacity;
    int size;
    mutable int readCount;
    mutable bool overflowed;
    bool allowOverflow;
};

#define MP_MATCH_VIEW_STANDALONE_TEST 1
#include "game/mp/match/MatchView.cpp"

mpMatchLocalizationId_t MPMatchProtocolReasonLocalizationId(mpMatchProtocolReason_t reason) {
    return reason == MP_MATCH_PROTOCOL_REASON_NONE ? MP_MATCH_LOCALIZATION_NONE :
        static_cast<mpMatchLocalizationId_t>(MP_MATCH_LOCALIZATION_REASON_BASE + reason);
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static void Allow(mpMatchViewPublicState_t &state, mpMatchOperationOpcode_t opcode) {
    MPMatchViewSetOperationDecision(state, opcode, MP_MATCH_PROTOCOL_REASON_OK);
}

static mpSessionView BaseView() {
    mpSessionView view;
    view.Clear();
    view.publicState.sessionId = 0x1122334455667788ull;
    view.publicState.sessionRevision = 7;
    view.publicState.controlRevision = 8;
    view.publicState.viewRevision = 9;
    view.publicState.recipient.participantId = 1;
    view.publicState.recipient.slot = 0;
    view.publicState.recipient.bindingGeneration = 3;
    view.publicState.recipient.side = 0;
    view.publicState.recipient.competitionSide = 0;
    view.publicState.recipient.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
    view.publicState.recipient.active = true;
    view.publicState.recipient.readyEligible = true;
    view.publicState.participantSummaryCount = 1;
    mpMatchViewParticipantSummary_t &participant = view.publicState.participantSummaries[0];
    participant.participantId = 1;
    participant.slot = 0;
    participant.side = 0;
    participant.publicRoleMask = view.publicState.recipient.publicRoleMask;
    participant.connected = true;
    participant.human = true;
    participant.active = true;
    Allow(view.publicState, MP_MATCH_OP_READY_SET);
    return view;
}

static void AddRules(mpSessionView &view) {
    mpMatchViewCommittedRules_t &rules = view.publicState.committedRules;
    rules.present = true;
    rules.rulesSchemaVersion = 1;
    rules.revision = 2;
    rules.digest = 0x1234;
    rules.profileId = 0;
    rules.boundary = MP_MATCH_VIEW_RULES_OPEN_FOR_COMMIT;
    rules.valueCount = 2;
    rules.values[0].fieldId = 0;
    rules.values[0].type = MP_MATCH_VIEW_RULE_ENUM;
    rules.values[0].value = 1;
    rules.values[0].editable = false;
    rules.values[1].fieldId = 1;
    rules.values[1].type = MP_MATCH_VIEW_RULE_BOOL;
    rules.values[1].value = 0;
    rules.values[1].editable = true;
}

static void FillProposal(mpMatchViewProposalSummary_t &proposal, bool global, int side) {
    proposal.present = true;
    proposal.proposalId = global ? 10 : 11;
    proposal.opcode = MP_MATCH_OP_ABORT;
    proposal.scope = global ? MP_MATCH_VIEW_PROPOSAL_GLOBAL : MP_MATCH_VIEW_PROPOSAL_SIDE;
    proposal.side = global ? MP_MATCH_VIEW_SIDE_NONE : side;
    proposal.callerParticipantId = 2;
    proposal.yesCount = 1;
    proposal.castCount = 1;
    proposal.eligibleCount = 2;
    proposal.requiredQuorumCount = 1;
    proposal.requiredYesCount = 2;
    proposal.expiresAtEngineMsec = 2000;
    proposal.recipientEligible = true;
    proposal.recipientBallot = MP_MATCH_VIEW_BALLOT_YES;
}

static bool Encoded(const mpSessionView &view, byte *buffer, int capacity, int &size) {
    idBitMsg message;
    message.Init(buffer, capacity);
    message.BeginWriting();
    mpMatchViewError_t error;
    if (!MPMatchViewEncode(message, view, &error)) return false;
    size = message.GetSize();
    return true;
}

static int FindFieldData(const byte *buffer, int size, int wantedField) {
    if (size < 16) return -1;
    int cursor = 16;
    int fieldCount = buffer[15];
    for (int i = 0; i < fieldCount; ++i) {
        if (cursor + 3 > size) return -1;
        int fieldId = buffer[cursor] & 0x7f;
        int length = buffer[cursor + 1] | (buffer[cursor + 2] << 8);
        if (cursor + 3 + length > size) return -1;
        if (fieldId == wantedField) return cursor + 3;
        cursor += 3 + length;
    }
    return -1;
}

static mpSessionView MaxView() {
    mpSessionView view = BaseView();
    view.publicState.participantSummaryCount = MP_MATCH_VIEW_MAX_PARTICIPANTS;
    for (int i = 0; i < MP_MATCH_VIEW_MAX_PARTICIPANTS; ++i) {
        mpMatchViewParticipantSummary_t &participant = view.publicState.participantSummaries[i];
        participant.participantId = i + 1;
        participant.slot = i;
        participant.side = i & 1;
        participant.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
        participant.connected = true;
        participant.human = true;
        participant.active = true;
    }
    view.publicState.recipient = mpMatchViewRecipient_t();
    view.publicState.recipient.Clear();
    view.publicState.recipient.participantId = 1;
    view.publicState.recipient.slot = 0;
    view.publicState.recipient.bindingGeneration = 3;
    view.publicState.recipient.side = 0;
    view.publicState.recipient.competitionSide = 0;
    view.publicState.recipient.publicRoleMask = MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
    view.publicState.recipient.active = true;
    view.publicState.recipient.readyEligible = true;
    view.publicState.roleSummaryCount = 8;
    int roleIndex = 0;
    for (int role = MP_MATCH_VIEW_ROLE_PLAYER; role <= MP_MATCH_VIEW_ROLE_COACH; ++role) {
        for (int side = 0; side < 2; ++side) {
            view.publicState.roleSummaries[roleIndex].role = static_cast<mpMatchViewPublicRole_t>(role);
            view.publicState.roleSummaries[roleIndex].side = side;
            view.publicState.roleSummaries[roleIndex].count = 1;
            ++roleIndex;
        }
    }
    view.publicState.roleSummaries[roleIndex].role = MP_MATCH_VIEW_ROLE_BROADCASTER;
    view.publicState.roleSummaries[roleIndex].side = MP_MATCH_VIEW_SIDE_NONE;
    view.publicState.roleSummaries[roleIndex++].count = 1;
    view.publicState.roleSummaries[roleIndex].role = MP_MATCH_VIEW_ROLE_REFEREE;
    view.publicState.roleSummaries[roleIndex].side = MP_MATCH_VIEW_SIDE_NONE;
    view.publicState.roleSummaries[roleIndex].count = 1;
    view.publicState.rosterSummaryCount = 2;
    for (int side = 0; side < 2; ++side) {
        mpMatchViewRosterSummary_t &summary = view.publicState.rosterSummaries[side];
        summary.side = side;
        summary.declaredSeats = 16;
        summary.occupiedSeats = 16;
        summary.connectedOccupants = 16;
        summary.readyOccupants = 16;
        summary.activeParticipants = 16;
        summary.queueDepth = 16;
        summary.teamReady = true;
        summary.locked = true;
    }
    FillProposal(view.publicState.globalProposal, true, -1);
    FillProposal(view.ownSideProposal, false, 0);
    view.publicState.denial.present = true;
    view.publicState.denial.opcode = MP_MATCH_OP_ABORT;
    view.publicState.denial.reason = MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED;
    view.publicState.denial.localizationId =
        MPMatchProtocolReasonLocalizationId(MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED);

    mpMatchViewCommittedRules_t &rules = view.publicState.committedRules;
    rules.present = true;
    rules.rulesSchemaVersion = 1;
    rules.revision = 4;
    rules.digest = 0xabcdef;
    rules.profileId = -1;
    rules.customized = true;
    rules.boundary = MP_MATCH_VIEW_RULES_FROZEN_FOR_MAP;
    rules.valueCount = MP_MATCH_VIEW_MAX_RULE_FIELDS;
    view.stagedRules.present = true;
    view.stagedRules.revision = 5;
    view.stagedRules.digest = 0xabcdef01;
    view.stagedRules.profileId = -1;
    view.stagedRules.customized = true;
    view.stagedRules.changedFieldMask = ~0ull;
    view.stagedRules.valueCount = MP_MATCH_VIEW_MAX_RULE_FIELDS;
    for (int i = 0; i < MP_MATCH_VIEW_MAX_RULE_FIELDS; ++i) {
        rules.values[i].fieldId = i;
        rules.values[i].type = MP_MATCH_VIEW_RULE_INTEGER;
        rules.values[i].value = i;
        rules.values[i].editable = true;
        view.stagedRules.values[i].fieldId = i;
        view.stagedRules.values[i].type = MP_MATCH_VIEW_RULE_INTEGER;
        view.stagedRules.values[i].value = i + 1;
    }

    mpMatchViewSeriesSummary_t &series = view.publicState.series;
    series.present = true;
    series.seriesId = 0xfedcba9876543210ull;
    series.state = MP_MATCH_VIEW_SERIES_READY;
    series.revision = 65;
    series.gameType = 1;
    series.bestOf = 15;
    series.currentMapNumber = 14;
    series.wins[0] = 7;
    series.wins[1] = 7;
    series.currentVetoStep = 64;
    series.vetoStepCount = 64;
    series.mapPoolCount = 32;
    char mapToken[MP_MATCH_VIEW_MAP_TOKEN_BYTES + 1];
    memset(mapToken, 'a', MP_MATCH_VIEW_MAP_TOKEN_BYTES);
    mapToken[MP_MATCH_VIEW_MAP_TOKEN_BYTES] = '\0';
    memcpy(mapToken, "maps/", 5);
    const char mapSuffixes[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    for (int i = 0; i < 32; ++i) {
        mapToken[MP_MATCH_VIEW_MAP_TOKEN_BYTES - 1] = mapSuffixes[i];
        mpMatchViewSeriesMap_t &map = series.mapPool[i];
        map.poolIndex = i;
        map.SetMapToken(mapToken, MP_MATCH_VIEW_MAP_TOKEN_BYTES);
        if (i < 15) {
            map.disposition = MP_MATCH_VIEW_MAP_SELECTED;
            map.selectionNumber = i + 1;
            map.decider = i == 14;
            map.selectedBySide = map.decider ? MP_MATCH_VIEW_SIDE_NONE : (i & 1);
        }
    }
    series.SetNextMap(series.mapPool[14].mapToken, series.mapPool[14].tokenLength);
    series.vetoHistoryCount = 64;
    for (int i = 0; i < 64; ++i) {
        series.vetoHistory[i].sequenceNumber = i + 1;
        series.vetoHistory[i].action = MP_MATCH_VIEW_VETO_BAN;
        series.vetoHistory[i].actingSide = i & 1;
        series.vetoHistory[i].mapPoolIndex = i % 32;
    }
    series.mapHistoryCount = 64;
    for (int i = 0; i < 64; ++i) {
        series.mapHistory[i].attemptNumber = i + 1;
        series.mapHistory[i].mapPoolIndex = i % 15;
        if (i < 14) {
            series.mapHistory[i].outcome = MP_MATCH_VIEW_MAP_DECIDED;
            series.mapHistory[i].winnerSide = i & 1;
            series.mapHistory[i].scores[i & 1] = 10;
            series.mapHistory[i].scores[(i & 1) ^ 1] = 5;
        } else {
            series.mapHistory[i].outcome = MP_MATCH_VIEW_MAP_ABORTED;
            series.mapHistory[i].winnerSide = MP_MATCH_VIEW_SIDE_NONE;
        }
    }

    view.rosterSeatCount = MP_MATCH_VIEW_MAX_ROSTER_SEATS;
    for (int i = 0; i < view.rosterSeatCount; ++i) {
        mpMatchViewRosterSeat_t &seat = view.rosterSeats[i];
        seat.seatIndex = i / 2;
        seat.side = i & 1;
        seat.role = MP_MATCH_VIEW_ROSTER_PLAYER;
        seat.required = true;
        seat.occupied = true;
        seat.participantId = i + 1;
        seat.connected = true;
        seat.ready = true;
        seat.active = true;
    }
    view.invitationCount = MP_MATCH_VIEW_MAX_INVITATIONS;
    for (int i = 0; i < view.invitationCount; ++i) {
        view.invitations[i].invitationId = i + 1;
        view.invitations[i].side = i & 1;
        view.invitations[i].role = MP_MATCH_VIEW_ROSTER_SUBSTITUTE;
        view.invitations[i].inviterParticipantId = 1;
        view.invitations[i].inviteeParticipantId = i + 100;
        view.invitations[i].expiresAtEngineMsec = i + 1000;
    }
    view.queueEntryCount = MP_MATCH_VIEW_MAX_QUEUE_ENTRIES;
    for (int i = 0; i < view.queueEntryCount; ++i) {
        view.queueEntries[i].participantId = i + 1000;
        view.queueEntries[i].side = i & 1;
        view.queueEntries[i].position = i / 2 + 1;
        view.queueEntries[i].state = MP_MATCH_VIEW_QUEUE_WAITING;
    }
    view.teamVitalCount = MP_MATCH_VIEW_MAX_TEAM_VITALS;
    view.followTargetCount = MP_MATCH_VIEW_MAX_FOLLOW_TARGETS;
    for (int i = 0; i < MP_MATCH_VIEW_MAX_TEAM_VITALS; ++i) {
        view.teamVitals[i].participantId = i + 1;
        view.teamVitals[i].participantSide = i & 1;
        view.teamVitals[i].health = 999;
        view.teamVitals[i].armor = 999;
        view.teamVitals[i].alive = true;
        view.followTargets[i].participantId = i + 1;
        view.followTargets[i].participantSide = i & 1;
        view.followTargets[i].selectable = true;
    }
    view.itemTimingCount = MP_MATCH_VIEW_MAX_ITEM_TIMINGS;
    for (int i = 0; i < view.itemTimingCount; ++i) {
        mpMatchViewItemTiming_t &item = view.itemTimings[i];
        memset(item.token, 'x', MP_MATCH_VIEW_ITEM_TOKEN_BYTES);
        item.token[MP_MATCH_VIEW_ITEM_TOKEN_BYTES - 1] = static_cast<char>('A' + i);
        item.token[MP_MATCH_VIEW_ITEM_TOKEN_BYTES] = '\0';
        item.tokenLength = MP_MATCH_VIEW_ITEM_TOKEN_BYTES;
        item.available = true;
        item.matchDeadlineMsec = i + 1;
    }
    mpMatchViewEvidenceSummary_t &evidence = view.publicState.evidence;
    evidence.evidenceState = MP_MATCH_VIEW_EVIDENCE_FINALIZED;
    evidence.mvdState = MP_MATCH_VIEW_MVD_AVAILABLE;
    evidence.reportState = MP_MATCH_VIEW_REPORT_AVAILABLE;
    evidence.evidenceRevision = 0xfedcba9876543210ull;
    evidence.eventCount = MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS;
    evidence.droppedRecordCount = 0xffffffffu;
    evidence.droppedRecordCountSaturated = true;
    evidence.participantStatsCount = MP_MATCH_VIEW_MAX_PARTICIPANTS;
    evidence.teamStatsCount = MP_MATCH_VIEW_SIDE_COUNT;
    evidence.resultRecorded = true;
    evidence.recentEventCount = MP_MATCH_VIEW_MAX_RECENT_EVIDENCE_EVENTS;
    evidence.recentEventKinds[0] = MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION;
    evidence.recentEventKinds[1] = MP_MATCH_VIEW_EVIDENCE_EVENT_ROSTER_CHANGE;
    evidence.recentEventKinds[2] = MP_MATCH_VIEW_EVIDENCE_EVENT_MAP_RESULT;
    evidence.recentEventKinds[3] = MP_MATCH_VIEW_EVIDENCE_EVENT_OUTPUT_FAILURE;
    view.publicState.lifecycle.Clear();
    view.publicState.lifecycle.phase = GAMEREVIEW;
    auto &terminal = view.publicState.terminalResult;
    terminal.outcome = MP_MATCH_VIEW_RESULT_FORFEIT;
    terminal.reason = MP_MATCH_VIEW_RESULT_REASON_FORFEIT;
    terminal.resultRevision = view.publicState.sessionRevision;
    terminal.winnerParticipantId = 0xffffffffu;
    char maximumName[MP_MATCH_VIEW_RESULT_NAME_BYTES + 1];
    memset(maximumName, 'W', sizeof(maximumName)-1); maximumName[sizeof(maximumName)-1] = 0;
    MPMatchViewSetResultWinnerName(terminal, maximumName);
    return view;
}

#include "game/mp/match/MatchSeries.h"

static void ProjectLiveSeriesSelection(const mpSeriesSelectedMap *selection,
        int selectionIndex, mpMatchViewSeriesMap_t &map) {
    // Compiled from the live adapter, rather than a second projection.
    @LIVE_SERIES_SELECTION@
}

static int LiveSeriesSelectionContract() {
    for (int side = 0; side < 2; ++side) {
        for (int decider = 0; decider < 2; ++decider) {
            for (int startingSide = -1; startingSide < 2; ++startingSide) {
                mpSeriesSelectedMap selection = {};
                selection.selectedBySide = side;
                selection.decider = decider != 0;
                selection.hasStartingGameSide = startingSide >= 0;
                selection.startingGameSide = startingSide;
                selection.gameSideChosenBy = 1 - side;
                mpMatchViewSeriesMap_t map; map.Clear();
                map.poolIndex = 0;
                map.disposition = MP_MATCH_VIEW_MAP_SELECTED;
                CHECK(map.SetMapToken("mp/q4dm1"));
                ProjectLiveSeriesSelection(&selection, 0, map);
                mpMatchViewError_t error; error.Clear();
                CHECK(ValidateSeriesMap(map, 0, 1, &error));
                CHECK(selection.selectedBySide == side);
                CHECK(map.selectedBySide == (decider ? MP_MATCH_VIEW_SIDE_NONE : side));
                if (decider) {
                    map.selectedBySide = side;
                    CHECK(!ValidateSeriesMap(map, 0, 1, &error));
                }
            }
        }
    }
    return 0;
}

int main() {
    CHECK(LiveSeriesSelectionContract() == 0);
    mpSessionView base = BaseView();
    mpMatchViewError_t error;
    CHECK(MPMatchViewValidate(base, &error));

    AddRules(base);
    FillProposal(base.publicState.globalProposal, true, -1);
    mpMatchViewSource_t source;
    source.Clear();
    source.publicState = base.publicState;
    source.publicState.recipient.competitionSide = 1;
    for (int side = 0; side < 2; ++side) {
        mpMatchViewProposalCandidate_t &candidate = source.proposalCandidates[source.proposalCandidateCount++];
        candidate.authorization.audience = MP_MATCH_VIEW_AUDIENCE_OWN_SIDE;
        candidate.authorization.audienceSide = side;
        FillProposal(candidate.value, false, side);
        candidate.value.proposalId += side;
    }
    mpMatchViewStagedRulesCandidate_t &staged = source.stagedRulesCandidates[source.stagedRulesCandidateCount++];
    staged.authorization.audience = MP_MATCH_VIEW_AUDIENCE_RECIPIENT;
    staged.authorization.audienceParticipantId = 1;
    staged.value.present = true;
    staged.value.revision = 3;
    staged.value.digest = 0x4321;
    staged.value.profileId = -1;
    staged.value.customized = true;
    staged.value.changedFieldMask = 1ull << 1;
    staged.value.valueCount = 1;
    staged.value.values[0].fieldId = 1;
    staged.value.values[0].type = MP_MATCH_VIEW_RULE_BOOL;
    staged.value.values[0].value = 1;
    for (int side = 0; side < 2; ++side) {
        mpMatchViewRosterSeatCandidate_t &seat = source.rosterSeatCandidates[source.rosterSeatCandidateCount++];
        seat.authorization.audience = MP_MATCH_VIEW_AUDIENCE_OWN_SIDE;
        seat.authorization.audienceSide = side;
        seat.value.side = side;
        seat.value.seatIndex = 0;
        seat.value.role = MP_MATCH_VIEW_ROSTER_PLAYER;
        seat.value.occupied = true;
        seat.value.participantId = side + 1;
        seat.value.connected = true;
    }
    for (int recipient = 1; recipient <= 2; ++recipient) {
        mpMatchViewInvitationCandidate_t &invitation = source.invitationCandidates[source.invitationCandidateCount++];
        invitation.authorization.audience = MP_MATCH_VIEW_AUDIENCE_RECIPIENT;
        invitation.authorization.audienceParticipantId = recipient;
        invitation.value.invitationId = recipient;
        invitation.value.side = recipient - 1;
        invitation.value.role = MP_MATCH_VIEW_ROSTER_PLAYER;
        invitation.value.inviterParticipantId = 20;
        invitation.value.inviteeParticipantId = recipient;
        invitation.value.expiresAtEngineMsec = 5000;
    }
    for (int side = 0; side < 2; ++side) {
        mpMatchViewQueueEntryCandidate_t &entry = source.queueEntryCandidates[source.queueEntryCandidateCount++];
        entry.authorization.audience = MP_MATCH_VIEW_AUDIENCE_OWN_SIDE;
        entry.authorization.audienceSide = side;
        entry.value.participantId = side + 10;
        entry.value.side = side;
        entry.value.position = 1;
        entry.value.state = MP_MATCH_VIEW_QUEUE_WAITING;
        mpMatchViewObserverCandidate_t &vital = source.observerCandidates[source.observerCandidateCount++];
        CHECK(vital.SetTeamVital(MP_MATCH_VIEW_AUDIENCE_OWN_SIDE, side,
            side + 10, side, 100, 50, true));
    }
    mpMatchViewObserverCandidate_t &spectatorFollow =
        source.observerCandidates[source.observerCandidateCount++];
    CHECK(spectatorFollow.SetFollowTarget(MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1,
        1, 2, 1, true));
    mpMatchViewObserverCandidate_t &spectatorFollowOtherSide =
        source.observerCandidates[source.observerCandidateCount++];
    CHECK(spectatorFollowOtherSide.SetFollowTarget(
        MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0, 0, 1, 0, true));
    mpMatchViewRecipientPolicy_t policy;
    policy.Clear();
    policy.recipientId = 1;
    policy.audiences |= MPMatchViewAudienceBit(MP_MATCH_VIEW_AUDIENCE_OWN_SIDE);
    policy.ownSide = 0;
    policy.observerKinds = MPMatchViewObserverKindBit(MP_MATCH_VIEW_OBSERVER_TEAM_VITAL) |
        MPMatchViewObserverKindBit(MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET);
    mpSessionView built;
    built.Clear();
    CHECK(MPMatchViewBuild(source, policy, built, &error));
    CHECK(built.publicState.recipient.side == 0);
    CHECK(built.publicState.recipient.competitionSide == 1);
    CHECK(built.publicState.globalProposal.present);
    CHECK(built.ownSideProposal.present && built.ownSideProposal.side == 0);
    CHECK(built.stagedRules.present && built.stagedRules.valueCount == 1);
    CHECK(built.rosterSeatCount == 1 && built.rosterSeats[0].side == 0);
    CHECK(built.invitationCount == 1 && built.invitations[0].inviteeParticipantId == 1);
    CHECK(built.queueEntryCount == 1 && built.queueEntries[0].side == 0);
    CHECK(built.teamVitalCount == 1 && built.teamVitals[0].participantSide == 0);
    CHECK(built.followTargetCount == 0);

    mpMatchViewRecipientPolicy_t spectatorPolicy = policy;
    spectatorPolicy.audiences |=
        MPMatchViewAudienceBit(MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1);
    mpSessionView spectatorBuilt;
    spectatorBuilt.Clear();
    CHECK(MPMatchViewBuild(source, spectatorPolicy, spectatorBuilt, &error));
    CHECK(spectatorBuilt.followTargetCount == 1);
    CHECK(spectatorBuilt.followTargets[0].participantId == 2);

    mpMatchViewRecipientPolicy_t otherSpectatorPolicy = policy;
    otherSpectatorPolicy.audiences |=
        MPMatchViewAudienceBit(MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0);
    mpSessionView otherSpectatorBuilt;
    otherSpectatorBuilt.Clear();
    CHECK(MPMatchViewBuild(source, otherSpectatorPolicy, otherSpectatorBuilt, &error));
    CHECK(otherSpectatorBuilt.followTargetCount == 1);
    CHECK(otherSpectatorBuilt.followTargets[0].participantId == 1);

    mpMatchViewSource_t hostileSpectator = source;
    hostileSpectator.observerCandidates[0].authorization.audience =
        MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0;
    hostileSpectator.observerCandidates[0].authorization.audienceSide = 0;
    CHECK(!MPMatchViewBuild(hostileSpectator, spectatorPolicy, spectatorBuilt, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_POLICY);

    mpMatchViewSource_t wrongAudienceSource = source;
    wrongAudienceSource.publicState.recipient.side = 1;
    mpSessionView wrongAudienceSide = built;
    CHECK(!MPMatchViewBuild(wrongAudienceSource, policy, wrongAudienceSide, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_POLICY);

    mpSessionView invalidCompetitionSide = built;
    invalidCompetitionSide.publicState.recipient.competitionSide = 2;
    CHECK(!MPMatchViewValidate(invalidCompetitionSide, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_STATE);

    mpSessionView botRecipient = built;
    botRecipient.publicState.participantSummaries[0].human = false;
    CHECK(!MPMatchViewValidate(botRecipient, &error));

    mpMatchViewEvidenceSummary_t &evidence = built.publicState.evidence;
    evidence.evidenceState = MP_MATCH_VIEW_EVIDENCE_CAPTURING;
    evidence.mvdState = MP_MATCH_VIEW_MVD_RECORDING;
    evidence.reportState = MP_MATCH_VIEW_REPORT_PENDING;
    evidence.evidenceRevision = 17;
    evidence.eventCount = 2;
    evidence.participantStatsCount = 1;
    evidence.recentEventCount = 2;
    evidence.recentEventKinds[0] = MP_MATCH_VIEW_EVIDENCE_EVENT_PHASE_TRANSITION;
    evidence.recentEventKinds[1] = MP_MATCH_VIEW_EVIDENCE_EVENT_ROLE_CHANGE;
    CHECK(MPMatchViewValidate(built, &error));

    mpMatchViewSource_t hostile = source;
    hostile.rosterSeatCandidates[1].value.occupied = false;
    mpSessionView unchanged = built;
    CHECK(!MPMatchViewBuild(hostile, policy, unchanged, &error));
    CHECK(unchanged.publicState.viewRevision == built.publicState.viewRevision);

    byte encoded[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 32];
    int encodedSize = 0;
    CHECK(Encoded(built, encoded, sizeof(encoded), encodedSize));
    idBitMsg encodedMessage;
    encodedMessage.Init(encoded, encodedSize);
    encodedMessage.SetSize(encodedSize);
    encodedMessage.BeginReading();
    mpSessionView decoded;
    decoded.Clear();
    CHECK(MPMatchViewDecode(encodedMessage, decoded, &error));
    CHECK(decoded.publicState.sessionId == built.publicState.sessionId);
    CHECK(decoded.ownSideProposal.proposalId == built.ownSideProposal.proposalId);
    CHECK(decoded.stagedRules.changedFieldMask == built.stagedRules.changedFieldMask);
    CHECK(decoded.rosterSeatCount == built.rosterSeatCount);
    CHECK(decoded.publicState.recipient.competitionSide == 1);
    CHECK(decoded.publicState.participantSummaries[0].human);
    CHECK(decoded.publicState.evidence.evidenceRevision == 17);
    CHECK(decoded.publicState.evidence.recentEventCount == 2);

    byte malformed[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 32];
    memcpy(malformed, encoded, encodedSize);
    int schemaData = FindFieldData(malformed, encodedSize, 1);
    CHECK(schemaData > 0);
    malformed[schemaData] = 2;
    malformed[schemaData + 1] = 0;
    idBitMsg versionTwo;
    versionTwo.Init(malformed, encodedSize);
    versionTwo.SetSize(encodedSize);
    versionTwo.BeginReading();
    mpSessionView versionSentinel = decoded;
    versionSentinel.publicState.viewRevision = 67890;
    CHECK(!MPMatchViewDecode(versionTwo, versionSentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_UNSUPPORTED_SCHEMA);
    CHECK(versionSentinel.publicState.viewRevision == 67890);

    memcpy(malformed, encoded, encodedSize);
    malformed[encodedSize] = 0xee;
    idBitMsg trailing;
    trailing.Init(malformed, encodedSize + 1);
    trailing.SetSize(encodedSize + 1);
    trailing.BeginReading();
    mpSessionView sentinel = decoded;
    sentinel.publicState.viewRevision = 12345;
    CHECK(!MPMatchViewDecode(trailing, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_TRAILING_DATA);
    CHECK(sentinel.publicState.viewRevision == 12345);

    idBitMsg truncated;
    truncated.Init(encoded, encodedSize - 1);
    truncated.SetSize(encodedSize - 1);
    truncated.BeginReading();
    int beforeCount = -1, beforeBit = -1;
    truncated.SaveReadState(beforeCount, beforeBit);
    CHECK(!MPMatchViewDecode(truncated, sentinel, &error));
    int afterCount = -2, afterBit = -2;
    truncated.SaveReadState(afterCount, afterBit);
    CHECK(beforeCount == afterCount && beforeBit == afterBit);

    int payloadLength = malformed[13] | (malformed[14] << 8);
    int firstLength = malformed[17] | (malformed[18] << 8);
    int firstFieldBytes = 3 + firstLength;
    memcpy(malformed, encoded, encodedSize);
    memcpy(malformed + encodedSize, malformed + 16, firstFieldBytes);
    malformed[15]++;
    int duplicatePayloadLength = payloadLength + firstFieldBytes;
    malformed[13] = static_cast<byte>(duplicatePayloadLength);
    malformed[14] = static_cast<byte>(duplicatePayloadLength >> 8);
    idBitMsg duplicate;
    duplicate.Init(malformed, encodedSize + firstFieldBytes);
    duplicate.SetSize(encodedSize + firstFieldBytes);
    duplicate.BeginReading();
    CHECK(!MPMatchViewDecode(duplicate, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_DUPLICATE_FIELD);

    memcpy(malformed, encoded, encodedSize);
    malformed[encodedSize + 0] = 0x80 | 26;
    malformed[encodedSize + 1] = 1;
    malformed[encodedSize + 2] = 0;
    malformed[encodedSize + 3] = 0x5a;
    malformed[15]++;
    int extensionPayloadLength = payloadLength + 4;
    malformed[13] = static_cast<byte>(extensionPayloadLength);
    malformed[14] = static_cast<byte>(extensionPayloadLength >> 8);
    idBitMsg extension;
    extension.Init(malformed, encodedSize + 4);
    extension.SetSize(encodedSize + 4);
    extension.BeginReading();
    CHECK(MPMatchViewDecode(extension, sentinel, &error));
    malformed[encodedSize] = 26;
    idBitMsg requiredUnknown;
    requiredUnknown.Init(malformed, encodedSize + 4);
    requiredUnknown.SetSize(encodedSize + 4);
    requiredUnknown.BeginReading();
    CHECK(!MPMatchViewDecode(requiredUnknown, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_UNKNOWN_REQUIRED_FIELD);

    memcpy(malformed, encoded, encodedSize);
    malformed[15] = 23;
    idBitMsg tooFewFields;
    tooFewFields.Init(malformed, encodedSize);
    tooFewFields.SetSize(encodedSize);
    tooFewFields.BeginReading();
    CHECK(!MPMatchViewDecode(tooFewFields, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_COUNT);

    memcpy(malformed, encoded, encodedSize);
    int oversizedPayload = MP_MATCH_VIEW_MAX_MESSAGE_BYTES - 15 + 1;
    malformed[13] = static_cast<byte>(oversizedPayload);
    malformed[14] = static_cast<byte>(oversizedPayload >> 8);
    idBitMsg oversized;
    oversized.Init(malformed, encodedSize);
    oversized.SetSize(encodedSize);
    oversized.BeginReading();
    CHECK(!MPMatchViewDecode(oversized, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_PAYLOAD_TOO_LARGE);

    memcpy(malformed, encoded, encodedSize);
    int participantsData = FindFieldData(malformed, encodedSize, 18);
    CHECK(participantsData > 0);
    malformed[participantsData] = MP_MATCH_VIEW_MAX_PARTICIPANTS + 1;
    idBitMsg excessiveParticipants;
    excessiveParticipants.Init(malformed, encodedSize);
    excessiveParticipants.SetSize(encodedSize);
    excessiveParticipants.BeginReading();
    CHECK(!MPMatchViewDecode(excessiveParticipants, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_COUNT);

    memcpy(malformed, encoded, encodedSize);
    participantsData = FindFieldData(malformed, encodedSize, 18);
    CHECK(participantsData > 0);
    malformed[participantsData + 11] = 0x80;
    idBitMsg unknownParticipantFlags;
    unknownParticipantFlags.Init(malformed, encodedSize);
    unknownParticipantFlags.SetSize(encodedSize);
    unknownParticipantFlags.BeginReading();
    CHECK(!MPMatchViewDecode(unknownParticipantFlags, sentinel, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_STATE);

    byte tooSmallBuffer[32];
    idBitMsg tooSmall;
    tooSmall.Init(tooSmallBuffer, sizeof(tooSmallBuffer));
    tooSmall.BeginWriting();
    tooSmall.WriteByte(0xaa);
    int savedSize = tooSmall.GetSize();
    CHECK(!MPMatchViewEncode(tooSmall, built, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_BUFFER_TOO_SMALL);
    CHECK(tooSmall.GetSize() == savedSize);

    mpSessionView current = decoded;
    mpSessionView older = decoded;
    older.publicState.viewRevision--;
    CHECK(MPMatchViewAccept(current, older, &error) == MP_MATCH_VIEW_ACCEPT_REJECTED_STALE);
    CHECK(current.publicState.viewRevision == decoded.publicState.viewRevision);
    CHECK(MPMatchViewAccept(current, decoded, &error) == MP_MATCH_VIEW_ACCEPT_NO_CHANGE);
    mpSessionView newer = decoded;
    newer.publicState.viewRevision++;
    CHECK(MPMatchViewAccept(current, newer, &error) == MP_MATCH_VIEW_ACCEPT_ADVANCED);

    mpSessionView maximum = MaxView();
    if (!MPMatchViewValidate(maximum, &error)) {
        fprintf(stderr, "maximum validation reason=%d field=%u detail=%u\n",
            error.reason, error.fieldId, error.detail);
        return __LINE__;
    }
    maximum.publicState.series.seriesId = 0;
    CHECK(!MPMatchViewValidate(maximum, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_STATE);
    maximum.publicState.series.seriesId = 0xfedcba9876543210ull;
    maximum.publicState.evidence.reportState = MP_MATCH_VIEW_REPORT_PENDING;
    CHECK(!MPMatchViewValidate(maximum, &error));
    CHECK(error.reason == MP_MATCH_VIEW_ERROR_INVALID_STATE);
    maximum.publicState.evidence.reportState = MP_MATCH_VIEW_REPORT_AVAILABLE;
    int maximumSize = 0;
    idBitMsg maximumMessage;
    maximumMessage.Init(encoded, sizeof(encoded));
    maximumMessage.BeginWriting();
    if (!MPMatchViewEncode(maximumMessage, maximum, &error)) {
        fprintf(stderr, "maximum encode reason=%d field=%u detail=%u size=%d\n",
            error.reason, error.fieldId, error.detail, maximumMessage.GetSize());
        return __LINE__;
    }
    maximumSize = maximumMessage.GetSize();
    CHECK(maximumSize <= MP_MATCH_VIEW_MAX_MESSAGE_BYTES);
    CHECK(maximumSize > 1024);
    idBitMsg maximumRead;
    maximumRead.Init(encoded, maximumSize);
    maximumRead.SetSize(maximumSize);
    maximumRead.BeginReading();
    mpSessionView maximumDecoded;
    maximumDecoded.Clear();
    CHECK(MPMatchViewDecode(maximumRead, maximumDecoded, &error));
    CHECK(maximumDecoded.publicState.series.seriesId == 0xfedcba9876543210ull);
    CHECK(maximumDecoded.publicState.series.vetoHistoryCount ==
        MP_MATCH_VIEW_MAX_SERIES_VETO_HISTORY);
    CHECK(maximumDecoded.publicState.evidence.eventCount ==
        MP_MATCH_VIEW_MAX_EVIDENCE_EVENTS);
    CHECK(maximumDecoded.publicState.evidence.droppedRecordCountSaturated);
    fprintf(stdout, "mp_match_view_contract: worst_case_bytes=%d limit=%d\n",
        maximumSize, MP_MATCH_VIEW_MAX_MESSAGE_BYTES);
    return 0;
}
'''


TERMINAL_CASES = r'''
static int TerminalResultContract() {
    mpSessionView view = BaseView();
    view.publicState.lifecycle.phase = GAMEREVIEW;
    auto &result = view.publicState.terminalResult;
    result.outcome = MP_MATCH_VIEW_RESULT_FORFEIT;
    result.reason = MP_MATCH_VIEW_RESULT_REASON_FORFEIT;
    result.resultRevision = 3;
    result.winnerParticipantId = 0xffffffffu; // A departed identity need not be in current roster.
    MPMatchViewSetResultWinnerName(result, "Frozen \xE2\x98\x83 winner");
    mpMatchViewError_t error;
    CHECK(MPMatchViewValidate(view, &error));
    byte encoded[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 32]; int size = 0;
    CHECK(Encoded(view, encoded, sizeof(encoded), size));
    const int terminalData = FindFieldData(encoded, size, 25);
    CHECK(terminalData > 0);
    idBitMsg message; message.Init(encoded, size); message.SetSize(size); message.BeginReading();
    mpSessionView decoded; decoded.Clear();
    CHECK(MPMatchViewDecode(message, decoded, &error));
    CHECK(decoded.publicState.terminalResult.winnerParticipantId == 0xffffffffu);
    CHECK(strcmp(decoded.publicState.terminalResult.winnerName, result.winnerName) == 0);
    for (auto phase : {WARMUP, NEXTGAME, GAMEREVIEW, COUNTDOWN, GAMEON, SUDDENDEATH, INACTIVE}) {
        mpSessionView candidate = view; candidate.publicState.lifecycle.Clear();
        candidate.publicState.lifecycle.phase = phase;
        CHECK(MPMatchViewValidate(candidate, &error) ==
            (phase == WARMUP || phase == NEXTGAME || phase == GAMEREVIEW));
    }
    for (int scenario = 0; scenario < 8; ++scenario) {
        mpSessionView candidate = view;
        auto &bad = candidate.publicState.terminalResult;
        switch (scenario) {
            case 0: bad.resultRevision = 0; break;
            case 1: bad.resultRevision = candidate.publicState.sessionRevision + 1; break;
            case 2: bad.winnerSide = 1; break; // Team and individual winners cannot coexist.
            case 3: bad.winnerParticipantId = 0; break;
            case 4: bad.winnerNameLength = 0; break;
            case 5: bad.winnerName[bad.winnerNameLength + 1] = 'x'; break;
            case 6: bad.reason = MP_MATCH_VIEW_RESULT_REASON_LIMIT_REACHED; break;
            case 7: bad.outcome = MP_MATCH_VIEW_RESULT_NONE; break;
        }
        CHECK(!MPMatchViewValidate(candidate, &error));
        CHECK(error.fieldId == 25);
    }
    for (auto outcome : {MP_MATCH_VIEW_RESULT_NONE, MP_MATCH_VIEW_RESULT_DECIDED,
            MP_MATCH_VIEW_RESULT_FORFEIT, MP_MATCH_VIEW_RESULT_DRAW, MP_MATCH_VIEW_RESULT_ABORTED}) {
        mpSessionView candidate = view;
        auto &valid = candidate.publicState.terminalResult; valid.Clear(); valid.outcome = outcome;
        if (outcome != MP_MATCH_VIEW_RESULT_NONE) valid.resultRevision = 3;
        valid.reason = outcome == MP_MATCH_VIEW_RESULT_NONE ? MP_MATCH_VIEW_RESULT_REASON_NONE :
            outcome == MP_MATCH_VIEW_RESULT_FORFEIT ? MP_MATCH_VIEW_RESULT_REASON_FORFEIT :
            outcome == MP_MATCH_VIEW_RESULT_ABORTED ? MP_MATCH_VIEW_RESULT_REASON_MATCH_ABORTED :
            MP_MATCH_VIEW_RESULT_REASON_LIMIT_REACHED;
        if (outcome == MP_MATCH_VIEW_RESULT_DECIDED || outcome == MP_MATCH_VIEW_RESULT_FORFEIT) valid.winnerSide = 1;
        CHECK(MPMatchViewValidate(candidate, &error));
        if (outcome == MP_MATCH_VIEW_RESULT_ABORTED || outcome == MP_MATCH_VIEW_RESULT_DRAW) {
            valid.winnerSide = 0; CHECK(!MPMatchViewValidate(candidate, &error));
        }
    }
    for (int scenario = 0; scenario < 6; ++scenario) {
        byte corrupt[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 32]; memcpy(corrupt, encoded, size);
        switch (scenario) {
            case 0: corrupt[terminalData] = 255; break;
            case 1: corrupt[terminalData + 1] = 255; break;
            case 2: corrupt[terminalData + 10] = 2; break;
            case 3: corrupt[terminalData + 15] = 65; break;
            case 4: corrupt[terminalData + 16] = 0xc0; break; // Overlong UTF-8.
            case 5: corrupt[terminalData + 16] = 0; break; // Embedded NUL.
        }
        idBitMsg bad; bad.Init(corrupt, size); bad.SetSize(size); bad.BeginReading();
        int before, beforeBit, after, afterBit; bad.SaveReadState(before, beforeBit);
        mpSessionView sentinel = view; sentinel.publicState.viewRevision = 9000;
        CHECK(!MPMatchViewDecode(bad, sentinel, &error));
        bad.SaveReadState(after, afterBit);
        CHECK(before == after && beforeBit == afterBit && sentinel.publicState.viewRevision == 9000);
    }
    for (const char *untrusted : {"", "\n\r\t\x7f", "\xc0\x80", "\xed\xa0\x80",
            "\xf4\x90\x80\x80", "\xe2\x80\xae", "Good \xe2\x98\x83"}) {
        mpSessionView candidate = view;
        MPMatchViewSetResultWinnerName(candidate.publicState.terminalResult, untrusted);
        CHECK(MPMatchViewValidate(candidate, &error));
    }
    char boundary[68]; memset(boundary, 'a', 63); memcpy(boundary + 63, "\xe2\x98\x83", 4);
    MPMatchViewSetResultWinnerName(result, boundary); CHECK(result.winnerNameLength == 63);
    CHECK(MPMatchViewValidate(view, &error));
    MPMatchViewSetResultWinnerName(result, nullptr);
    CHECK(strcmp(result.winnerName, "player-4294967295") == 0);
    mpSessionView current = view, incoming = view;
    incoming.publicState.viewRevision++;
    MPMatchViewSetResultWinnerName(incoming.publicState.terminalResult, "Tampered winner");
    CHECK(MPMatchViewAccept(current, incoming, &error) == MP_MATCH_VIEW_ACCEPT_REJECTED_INVALID);
    CHECK(current.publicState.viewRevision == view.publicState.viewRevision);
    incoming = view; incoming.publicState.viewRevision++;
    incoming.publicState.terminalResult.resultRevision--;
    CHECK(MPMatchViewAccept(current, incoming, &error) == MP_MATCH_VIEW_ACCEPT_REJECTED_STALE);
    incoming = view; incoming.publicState.viewRevision++; incoming.publicState.lifecycle.phase = WARMUP;
    CHECK(MPMatchViewAccept(current, incoming, &error) == MP_MATCH_VIEW_ACCEPT_ADVANCED);
    incoming.publicState.viewRevision++; incoming.publicState.terminalResult.Clear();
    incoming.publicState.lifecycle.phase = COUNTDOWN;
    CHECK(MPMatchViewAccept(current, incoming, &error) == MP_MATCH_VIEW_ACCEPT_ADVANCED);
    CHECK(current.publicState.terminalResult.outcome == MP_MATCH_VIEW_RESULT_NONE);
    incoming = view; incoming.publicState.sessionId++;
    CHECK(MPMatchViewAccept(current, incoming, &error) == MP_MATCH_VIEW_ACCEPT_REPLACED_SESSION);
    return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_view_contract: executable checks skipped (no C++ compiler)")
        return
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-view-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_view_contract.cpp"
        executable = temp_dir / "match_view_contract.exe"
        live = read(ROOT / "src/game/MultiplayerGame.cpp")
        start = live.index("map.selectedBySide = selection->")
        end = live.index("break;", start)
        executable_source = HARNESS.replace("@LIVE_SERIES_SELECTION@", live[start:end])
        executable_source = executable_source.replace("int main() {", TERMINAL_CASES +
            "\nint main() {\n    CHECK(TerminalResultContract() == 0);", 1)
        harness.write_text(executable_source, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1" else []),
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
                "standalone match-view contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"match-view invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )
        if ran.stdout.strip():
            print(ran.stdout.strip())


def main() -> None:
    header = read(HEADER_PATH)
    source = read(SOURCE_PATH)
    schema_contract(header, source)
    truthful_control_contract(header, source)
    authorization_contract(header, source)
    codec_contract(header, source)
    freshness_contract(source)
    source_listing_contract()
    executable_contract()
    print("mp_match_view_contract: PASS")


if __name__ == "__main__":
    main()
