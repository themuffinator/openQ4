#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for competitive disclosure policy."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchDisclosurePolicy.h"
SOURCE = ROOT / "src/game/mp/match/MatchDisclosurePolicy.cpp"
VIEW_SOURCE = ROOT / "src/game/mp/match/MatchView.cpp"
SESSION_SOURCE = ROOT / "src/game/mp/match/MatchSession.cpp"
MULTIPLAYER_SOURCE = ROOT / "src/game/MultiplayerGame.cpp"
PLAYER_SOURCE = ROOT / "src/game/Player.cpp"
GAME_STATE_SOURCE = ROOT / "src/game/mp/GameState.cpp"


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


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_MATCH_DISCLOSURE_POLICY_VERSION = 1",
        "lockedSpectatorSideMask",
        "allowSpectatorInvitations",
        "allowCoachObservation",
        "allowLiveBroadcasterObservation",
        "allowBroadcasterItemTiming",
        "allowRefereeObservation",
        "allowRefereeItemTiming",
        "itemTimingDelayMsec",
        "invitationSideMask",
        "MP_MATCH_DISCLOSURE_PRINCIPAL_REPEATER",
        "MP_MATCH_DISCLOSURE_REASON_REPEATER_PUBLIC_ONLY",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1",
        "MPMatchDisclosureBuildGrant",
        "MPMatchDisclosureCanFollow",
        "MPMatchDisclosureBuildView",
        "MPMatchDisclosureSetItemTimingCandidate",
        "RepeaterSourceIsPublicOnly",
        "observedAtMatchTime",
        "currentMatchTime",
        "MP_MATCH_DISCLOSURE_ITEM_DELAYED",
        "MP_MATCH_DISCLOSURE_ITEM_CLOCK_REGRESSION",
        "MP_MATCH_DISCLOSURE_ITEM_CLOCK_OVERFLOW",
    ):
        require(combined, token, "disclosure boundary")

    for token in (
        "MultiplayerGame.h",
        "Game_local.h",
        "idMultiplayerGame",
        "idPlayer",
        "cvarSystem",
        "cmdSystem",
        "fileSystem",
        "userInfo",
        "new ",
        "idList<",
        "idStr ",
    ):
        reject(combined, token, "pure allocation-free disclosure boundary")

    require_before(
        source,
        "SourceMatchesRecipient( source, recipient )",
        "MPMatchViewBuild( source, grant.viewPolicy",
        "identity binding before recipient projection",
    )
    require_before(
        source,
        "ObserverSourceIsSafe( policy, source )",
        "MPMatchViewBuild( source, grant.viewPolicy",
        "observer tag validation before recipient projection",
    )
    require(
        source,
        "observedMsec > INT64_MAX - policy.itemTimingDelayMsec",
        "overflow-safe item holdback",
    )
    require(
        source,
        "currentMsec < observedMsec",
        "match-clock regression rejection",
    )
    require(
        source,
        "recipient.repeater",
        "repeater fail-closed branch",
    )

    recipient_shape = function_body(
        source, "static bool RecipientShapeIsValid"
    )
    for token in (
        "const bool activePlayerSideValid",
        "player && !captain && recipient.side == MP_MATCH_SIDE_NONE",
        "!activePlayerSideValid",
    ):
        require(token=token, text=recipient_shape, context="active FFA recipient shape")

    grant = function_body(source, "bool MPMatchDisclosureBuildGrant")
    require(
        grant,
        "if ( IsSide( recipient.side ) ) {\n"
        "\t\t\tAddObserverKind( MP_MATCH_VIEW_OBSERVER_TEAM_VITAL, grant );\n"
        "\t\t}",
        "team-only active-player tactical disclosure",
    )


def integration_contract(multiplayer: str, player: str, game_state: str) -> None:
    require(
        multiplayer,
        '#include "mp/match/MatchDisclosurePolicy.h"',
        "live disclosure adapter include",
    )
    policy = function_body(
        multiplayer, "mpMatchDisclosurePolicy_t idMultiplayerGame::BuildMatchDisclosurePolicy"
    )
    for token in (
        "GetBool( MP_RULE_MANAGED_MATCH )",
        "const bool protectLivePov",
        "managedMatch &&",
        "phase == COUNTDOWN",
        "phase == GAMEON",
        "phase == SUDDENDEATH",
        "MPMatchDisclosureAllSideBits() : 0",
        "policy.allowSpectatorInvitations = false",
        "policy.allowCoachObservation = true",
        "policy.allowLiveBroadcasterObservation = true",
        "policy.allowBroadcasterItemTiming = managedMatch",
        "policy.allowRefereeObservation = true",
        "policy.allowRefereeItemTiming = managedMatch",
        "policy.itemTimingDelayMsec = 0",
    ):
        require(policy, token, "managed live disclosure policy adapter")

    recipient = function_body(
        multiplayer, "bool idMultiplayerGame::BuildMatchDisclosureRecipient"
    )
    for token in (
        "gameLocal.isServer",
        "clientNum >= gameLocal.numClients",
        "clientNum >= MAX_CLIENTS",
        "idPlayer::GetClassType()",
        "player->IsFakeClient()",
        "botManager.IsBot( clientNum )",
        "matchSession.GetSlotGeneration",
        "matchSession.ResolveSlotBinding",
        "!state->connected",
        "!state->human",
        "state->slot != clientNum",
        "recipient.sessionId = matchSession.GetSessionId()",
        "recipient.sessionRevision = matchSession.GetSessionRevision()",
        "recipient.participantId = participant.SequencePart()",
        "recipient.bindingGeneration = generation",
        "recipient.side = state->side",
        "recipient.roles = state->roles",
        "recipient.active = state->active",
        "recipient.repeater = gameLocal.isRepeater",
    ):
        require(recipient, token, "fresh disclosure recipient binding")

    target_side = function_body(
        multiplayer, "int idMultiplayerGame::ResolveMatchDisclosureTargetSide"
    )
    for token in (
        "target.side >= 0",
        "target.side < MP_MATCH_SIDE_COUNT",
        "target.side == MP_MATCH_SIDE_NONE",
        "!gameLocal.IsTeamGame()",
        "return 0",
        "return MP_MATCH_SIDE_NONE",
    ):
        require(target_side, token, "non-team synthetic disclosure domain")

    follow = function_body(multiplayer, "bool idMultiplayerGame::CanSpectatorFollow")
    for token in (
        "gameLocal.isServer",
        "observerSlot == targetSlot",
        "observerPlayer->IsFakeClient()",
        "botManager.IsBot( observerSlot )",
        "!observerPlayer->spectating",
        "targetPlayer->spectating",
        "targetPlayer->wantSpectate",
        "!playerState[ targetSlot ].ingame",
        "BuildMatchDisclosureRecipient",
        "matchSession.GetSlotGeneration( targetSlot",
        "matchSession.ResolveSlotBinding( targetSlot",
        "targetParticipant == observerParticipant",
        "!targetState->connected",
        "!targetState->active",
        "targetState->slot != targetSlot",
        "ResolveMatchDisclosureTargetSide",
        "MPMatchDisclosureCanFollow( BuildMatchDisclosurePolicy()",
    ):
        require(follow, token, "fresh server camera authorization")
    reject(follow, "targetState->human", "active bots remain legal camera targets")
    reject(follow, "botManager.IsBot( targetSlot )", "active bot target support")
    require_before(
        follow,
        "matchSession.ResolveSlotBinding( targetSlot",
        "MPMatchDisclosureCanFollow( BuildMatchDisclosurePolicy()",
        "fresh target binding before camera authorization",
    )

    build = function_body(multiplayer, "bool idMultiplayerGame::BuildMatchView")
    for token in (
        "BuildMatchDisclosureRecipient",
        "BuildMatchDisclosurePolicy",
        "ResolveMatchDisclosureTargetSide",
        "MPMatchDisclosureBuildGrant",
        "MPMatchDisclosureCanFollow",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0",
        "MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1",
        "MPMatchDisclosureBuildView",
    ):
        require(build, token, "live disclosure adapter")

    for token in (
        "const bool repeaterRecipient = disclosureRecipient.repeater",
        "!repeaterRecipient && rulesRecipient",
        "repeaterRecipient || seat == NULL",
        "teamsSnapshotValid && !repeaterRecipient",
        "!repeaterRecipient && teamsSnapshotValid && teamsSnapshot.recipientQueued",
        "repeaterRecipient ? 0 : AllowedMatchOperationsFor",
        "MP_MATCH_PROTOCOL_REASON_NOT_AUTHORIZED",
    ):
        require(build, token, "repeater public-only live adapter")

    for forbidden in (
        "disclosureRecipient.invitationSideMask",
        "disclosurePolicy.allowSpectatorInvitations = true",
        "MPMatchViewBuild(",
        ".SetItemTiming(",
    ):
        reject(build, forbidden, "live disclosure adapter bypass")

    require_before(
        build,
        "MPMatchDisclosureBuildGrant",
        "source.observerCandidates",
        "grant before tactical candidate population",
    )
    require_before(
        build,
        "MPMatchDisclosureCanFollow",
        "follow.SetFollowTarget",
        "server follow authorization before projection",
    )
    require_before(
        build,
        "MPMatchDisclosureBuildView",
        "return true",
        "canonical disclosure view build",
    )

    attack_cycle = function_body(player, "void idPlayer::SpectateCycle")
    require(attack_cycle, "CycleSpectatorFollow( direction )", "attack uses the shared follow cycle")
    cycle = function_body(player, "bool idPlayer::CycleSpectatorFollow")
    for token in (
        "Min( gameLocal.numClients, MAX_CLIENTS )",
        "attempt <= count",
        "SpectatorFollowClient( candidateSlot )",
        "candidate->spectating",
        "gameLocal.mpGame.CanSpectatorFollow",
        "spectator = candidateSlot",
        "SpectateFreeFly( true )",
    ):
        require(cycle, token, "bounded normal spectator cycle")
    require_before(
        cycle,
        "gameLocal.mpGame.CanSpectatorFollow",
        "spectator = candidateSlot",
        "authorize normal camera before assignment",
    )
    update_spectating = function_body(player, "void idPlayer::UpdateSpectating")
    require(
        update_spectating,
        "gameLocal.mpGame.CanSpectatorFollow",
        "continuous selected-POV revocation",
    )
    require_before(
        update_spectating,
        "gameLocal.mpGame.CanSpectatorFollow",
        "usercmd.upmove",
        "revalidate current camera before input processing",
    )

    tourney_cycle = function_body(
        game_state, "bool rvTourneyGameState::CycleSpectatorTarget"
    )
    for token in (
        "direction != 1 && direction != -1",
        "const int targetCount = MAX_ARENAS * 2",
        "step <= targetCount",
        "AS_INACTIVE",
        "AS_DONE",
        "gameLocal.mpGame.CanSpectatorFollow",
        "player->JoinInstance( targetArena )",
        "player->spectator = target->entityNumber",
        "player->spectator = player->entityNumber",
    ):
        require(tourney_cycle, token, "bounded Tourney spectator cycle")
    require_before(
        tourney_cycle,
        "gameLocal.mpGame.CanSpectatorFollow",
        "player->spectator = target->entityNumber",
        "authorize Tourney camera before assignment",
    )
    next_cycle = function_body(game_state, "void rvTourneyGameState::SpectateCycleNext")
    prev_cycle = function_body(game_state, "void rvTourneyGameState::SpectateCyclePrev")
    require(next_cycle, "CycleSpectatorTarget( player, 1 )", "Tourney next adapter")
    require(prev_cycle, "CycleSpectatorTarget( player, -1 )", "Tourney previous adapter")


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
    if "game/mp/match/MatchDisclosurePolicy.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError(
            "MatchDisclosurePolicy.cpp is absent from the canonical MP source list"
        )


HARNESS = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
#include "game/mp/match/MatchDisclosurePolicy.h"

mpMatchLocalizationId_t MPMatchProtocolReasonLocalizationId(mpMatchProtocolReason_t reason) {
    return reason == MP_MATCH_PROTOCOL_REASON_NONE ? MP_MATCH_LOCALIZATION_NONE :
        static_cast<mpMatchLocalizationId_t>(MP_MATCH_LOCALIZATION_REASON_BASE + reason);
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static mpMatchRoleMask_t Role(mpMatchRole_t role) {
    return MPMatchRoleBit(role);
}

static mpMatchViewPublicRoleMask_t ViewRoles(mpMatchRoleMask_t roles) {
    mpMatchViewPublicRoleMask_t result = 0;
    if (roles & Role(MP_MATCH_ROLE_PLAYER)) result |= MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER);
    if (roles & Role(MP_MATCH_ROLE_CAPTAIN)) result |= MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_CAPTAIN);
    if (roles & Role(MP_MATCH_ROLE_COACH)) result |= MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_COACH);
    if (roles & Role(MP_MATCH_ROLE_BROADCASTER)) result |= MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_BROADCASTER);
    if (roles & Role(MP_MATCH_ROLE_REFEREE)) result |= MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_REFEREE);
    return result;
}

static mpMatchDisclosureRecipient_t Recipient(mpMatchRoleMask_t roles, int side, bool active) {
    mpMatchDisclosureRecipient_t recipient;
    recipient.Clear();
    recipient.sessionId = 0x1122334455667788ull;
    recipient.sessionRevision = 7;
    recipient.participantId = 20;
    recipient.slot = 3;
    recipient.bindingGeneration = 9;
    recipient.side = side;
    recipient.roles = roles;
    recipient.active = active;
    return recipient;
}

static void SetParticipant(mpMatchViewParticipantSummary_t &target,
    mpMatchProtocolParticipantId_t participantId, int slot, int side,
    mpMatchViewPublicRoleMask_t roles, bool active) {
    target.Clear();
    target.participantId = participantId;
    target.slot = static_cast<unsigned char>(slot);
    target.side = side;
    target.publicRoleMask = roles;
    target.connected = true;
    target.human = true;
    target.active = active;
}

static mpMatchViewSource_t SourceFor(const mpMatchDisclosureRecipient_t &recipient) {
    mpMatchViewSource_t source;
    source.Clear();
    source.publicState.sessionId = recipient.sessionId;
    source.publicState.sessionRevision = recipient.sessionRevision;
    source.publicState.controlRevision = 8;
    source.publicState.viewRevision = 9;
    source.publicState.recipient.participantId = recipient.participantId;
    source.publicState.recipient.slot = static_cast<unsigned char>(recipient.slot);
    source.publicState.recipient.bindingGeneration = recipient.bindingGeneration;
    source.publicState.recipient.side = recipient.side;
    source.publicState.recipient.publicRoleMask = ViewRoles(recipient.roles);
    source.publicState.recipient.active = recipient.active;
    source.publicState.participantSummaryCount = 3;
    SetParticipant(source.publicState.participantSummaries[0], recipient.participantId,
        recipient.slot, recipient.side, ViewRoles(recipient.roles), recipient.active);
    SetParticipant(source.publicState.participantSummaries[1], 101, 4, 0,
        MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER), true);
    SetParticipant(source.publicState.participantSummaries[2], 102, 5, 1,
        MPMatchViewRoleBit(MP_MATCH_VIEW_ROLE_PLAYER), true);
    return source;
}

static bool PushVital(mpMatchViewSource_t &source, mpMatchViewAudience_t audience,
    int tagSide, int side, int health) {
    if (source.observerCandidateCount >= MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES) return false;
    mpMatchViewObserverCandidate_t &candidate =
        source.observerCandidates[source.observerCandidateCount];
    if (!candidate.SetTeamVital(audience, tagSide, 101 + side, side,
        health, 50 + side, true)) return false;
    ++source.observerCandidateCount;
    return true;
}

static bool PushFollow(mpMatchViewSource_t &source, mpMatchViewAudience_t audience,
    int tagSide, int side) {
    if (source.observerCandidateCount >= MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES) return false;
    mpMatchViewObserverCandidate_t &candidate =
        source.observerCandidates[source.observerCandidateCount];
    if (!candidate.SetFollowTarget(audience, tagSide, 101 + side, side, true)) return false;
    ++source.observerCandidateCount;
    return true;
}

static bool PushItem(mpMatchViewSource_t &source,
    const mpMatchDisclosurePolicy_t &policy, mpMatchViewAudience_t audience) {
    if (source.observerCandidateCount >= MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES) return false;
    mpMatchViewObserverCandidate_t &candidate =
        source.observerCandidates[source.observerCandidateCount];
    if (MPMatchDisclosureSetItemTimingCandidate(policy, audience,
        mpMatchTime::FromMilliseconds(200), mpMatchTime::FromMilliseconds(100),
        "quad", mpMatchTime::FromMilliseconds(500), false, candidate) !=
        MP_MATCH_DISCLOSURE_ITEM_READY) return false;
    ++source.observerCandidateCount;
    return true;
}

static bool AddSafeObservers(mpMatchViewSource_t &source,
    const mpMatchDisclosurePolicy_t &policy) {
    for (int side = 0; side < 2; ++side) {
        if (!PushVital(source, MP_MATCH_VIEW_AUDIENCE_OWN_SIDE, side, side, 100 + side)) return false;
        if (policy.allowLiveBroadcasterObservation &&
            !PushVital(source, MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
                MP_MATCH_VIEW_SIDE_NONE, side, 100 + side)) return false;
        if (policy.allowRefereeObservation &&
            !PushVital(source, MP_MATCH_VIEW_AUDIENCE_REFEREE,
                MP_MATCH_VIEW_SIDE_NONE, side, 100 + side)) return false;
        const mpMatchViewAudience_t spectatorAudience = side == 0 ?
            MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0 :
            MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1;
        if (!PushFollow(source, spectatorAudience, side, side)) return false;
        if (policy.allowCoachObservation &&
            !PushFollow(source, MP_MATCH_VIEW_AUDIENCE_OWN_SIDE, side, side)) return false;
        if (policy.allowLiveBroadcasterObservation &&
            !PushFollow(source, MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
                MP_MATCH_VIEW_SIDE_NONE, side)) return false;
        if (policy.allowRefereeObservation &&
            !PushFollow(source, MP_MATCH_VIEW_AUDIENCE_REFEREE,
                MP_MATCH_VIEW_SIDE_NONE, side)) return false;
    }
    if (policy.allowBroadcasterItemTiming &&
        !PushItem(source, policy, MP_MATCH_VIEW_AUDIENCE_BROADCASTER)) return false;
    if (policy.allowRefereeItemTiming &&
        !PushItem(source, policy, MP_MATCH_VIEW_AUDIENCE_REFEREE)) return false;
    return true;
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

static int FieldCount(const byte *buffer, int size, int wantedField) {
    if (size < 16) return -1;
    int cursor = 16;
    const int fieldCount = buffer[15];
    for (int i = 0; i < fieldCount; ++i) {
        if (cursor + 3 > size) return -1;
        const int fieldId = buffer[cursor] & 0x7f;
        const int length = buffer[cursor + 1] | (buffer[cursor + 2] << 8);
        if (cursor + 3 + length > size) return -1;
        if (fieldId == wantedField) return length > 0 ? buffer[cursor + 3] : -1;
        cursor += 3 + length;
    }
    return -1;
}

static bool Contains(const byte *buffer, int size, const char *needle) {
    const int length = static_cast<int>(strlen(needle));
    for (int i = 0; i <= size - length; ++i) {
        if (memcmp(buffer + i, needle, length) == 0) return true;
    }
    return false;
}

static int CheckWireCounts(const mpSessionView &view, int vitals, int items,
    int follows, bool containsItemToken) {
    byte buffer[MP_MATCH_VIEW_MAX_MESSAGE_BYTES + 32];
    int size = 0;
    CHECK(Encoded(view, buffer, sizeof(buffer), size));
    CHECK(FieldCount(buffer, size, MP_MATCH_VIEW_FIELD_TEAM_VITALS) == vitals);
    CHECK(FieldCount(buffer, size, MP_MATCH_VIEW_FIELD_ITEM_TIMINGS) == items);
    CHECK(FieldCount(buffer, size, MP_MATCH_VIEW_FIELD_FOLLOW_TARGETS) == follows);
    CHECK(Contains(buffer, size, "quad") == containsItemToken);
    return 0;
}

static mpMatchDisclosurePolicy_t FullPolicy() {
    mpMatchDisclosurePolicy_t policy;
    policy.Clear();
    policy.allowSpectatorInvitations = true;
    policy.allowCoachObservation = true;
    policy.allowLiveBroadcasterObservation = true;
    policy.allowBroadcasterItemTiming = true;
    policy.allowRefereeObservation = true;
    policy.allowRefereeItemTiming = true;
    policy.itemTimingDelayMsec = 100;
    return policy;
}

static int RoleAndWireMatrix() {
    const mpMatchDisclosurePolicy_t policy = FullPolicy();
    mpMatchDisclosureReason_t reason;
    mpMatchViewError_t viewError;
    mpSessionView view;

    mpMatchDisclosureRecipient_t player = Recipient(Role(MP_MATCH_ROLE_PLAYER), 0, true);
    mpMatchViewSource_t source = SourceFor(player);
    CHECK(AddSafeObservers(source, policy));
    if (!MPMatchDisclosureBuildView(policy, player, source, view, &reason, &viewError)) {
        fprintf(stderr, "player view reason=%d viewReason=%d field=%u detail=%u\n",
            reason, viewError.reason, viewError.fieldId, viewError.detail);
        return __LINE__;
    }
    CHECK(view.teamVitalCount == 1 && view.teamVitals[0].participantSide == 0);
    CHECK(view.followTargetCount == 0 && view.itemTimingCount == 0);
    CHECK(CheckWireCounts(view, 1, 0, 0, false) == 0);

    mpMatchDisclosureRecipient_t captain = Recipient(
        Role(MP_MATCH_ROLE_PLAYER) | Role(MP_MATCH_ROLE_CAPTAIN), 1, true);
    source = SourceFor(captain);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, captain, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 1 && view.teamVitals[0].participantSide == 1);
    CHECK(CheckWireCounts(view, 1, 0, 0, false) == 0);

    mpMatchDisclosureRecipient_t coach = Recipient(Role(MP_MATCH_ROLE_COACH), 0, false);
    source = SourceFor(coach);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, coach, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 1 && view.teamVitals[0].participantSide == 0);
    CHECK(view.followTargetCount == 1 && view.followTargets[0].participantSide == 0);
    CHECK(view.itemTimingCount == 0);
    CHECK(CheckWireCounts(view, 1, 0, 1, false) == 0);
    CHECK(MPMatchDisclosureCanFollow(policy, coach, 101, 0, true));
    CHECK(!MPMatchDisclosureCanFollow(policy, coach, 102, 1, true));

    mpMatchDisclosureRecipient_t spectator = Recipient(0, MP_MATCH_SIDE_NONE, false);
    source = SourceFor(spectator);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, spectator, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 0 && view.itemTimingCount == 0 &&
        view.followTargetCount == 0);
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_NONE);
    CHECK(CheckWireCounts(view, 0, 0, 0, false) == 0);

    spectator.invitationSideMask = MPMatchDisclosureSideBit(0);
    source = SourceFor(spectator);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, spectator, source, view, &reason, &viewError));
    CHECK(view.followTargetCount == 1 && view.followTargets[0].participantSide == 0);
    CHECK(CheckWireCounts(view, 0, 0, 1, false) == 0);
    CHECK(MPMatchDisclosureCanFollow(policy, spectator, 101, 0, true));
    CHECK(!MPMatchDisclosureCanFollow(policy, spectator, 102, 1, true));

    mpMatchDisclosureRecipient_t broadcaster = Recipient(
        Role(MP_MATCH_ROLE_BROADCASTER), MP_MATCH_SIDE_NONE, false);
    source = SourceFor(broadcaster);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, broadcaster, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 2 && view.followTargetCount == 2 &&
        view.itemTimingCount == 1);
    CHECK(CheckWireCounts(view, 2, 1, 2, true) == 0);

    mpMatchDisclosureRecipient_t referee = Recipient(
        Role(MP_MATCH_ROLE_REFEREE), MP_MATCH_SIDE_NONE, false);
    source = SourceFor(referee);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, referee, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 2 && view.followTargetCount == 2 &&
        view.itemTimingCount == 1);
    CHECK(CheckWireCounts(view, 2, 1, 2, true) == 0);

    broadcaster.repeater = true;
    source = SourceFor(broadcaster);
    CHECK(MPMatchDisclosureBuildView(policy, broadcaster, source, view, &reason, &viewError));
    CHECK(view.teamVitalCount == 0 && view.followTargetCount == 0 &&
        view.itemTimingCount == 0);
    CHECK(CheckWireCounts(view, 0, 0, 0, false) == 0);
    mpMatchDisclosureGrant_t grant;
    CHECK(MPMatchDisclosureBuildGrant(policy, broadcaster, grant));
    CHECK(grant.principal == MP_MATCH_DISCLOSURE_PRINCIPAL_REPEATER &&
        grant.repeaterPublicOnly &&
        grant.reason == MP_MATCH_DISCLOSURE_REASON_REPEATER_PUBLIC_ONLY);
    CHECK(!MPMatchDisclosureCanFollow(policy, broadcaster, 101, 0, true));

    mpMatchViewSource_t repeaterHostile = source;
    repeaterHostile.rosterSeatCandidateCount = 1;
    CHECK(!MPMatchDisclosureBuildView(policy, broadcaster, repeaterHostile,
        view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE);
    repeaterHostile = source;
    CHECK(MPMatchViewSetOperationDecision(repeaterHostile.publicState,
        MP_MATCH_OP_READY_SET, MP_MATCH_PROTOCOL_REASON_OK));
    CHECK(!MPMatchDisclosureBuildView(policy, broadcaster, repeaterHostile,
        view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE);
    repeaterHostile = source;
    repeaterHostile.publicState.recipient.readyEligible = true;
    CHECK(!MPMatchDisclosureBuildView(policy, broadcaster, repeaterHostile,
        view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE);
    return 0;
}

static int PlayerRecipientShapeMatrix() {
    const mpMatchDisclosurePolicy_t policy = FullPolicy();
    const mpMatchViewAudienceMask_t ownSideAudience =
        MPMatchViewAudienceBit(MP_MATCH_VIEW_AUDIENCE_OWN_SIDE);
    const mpMatchViewObserverKindMask_t teamVitalKind =
        MPMatchViewObserverKindBit(MP_MATCH_VIEW_OBSERVER_TEAM_VITAL);
    mpMatchDisclosureGrant_t grant;
    mpMatchDisclosureReason_t reason;
    mpMatchViewError_t viewError;
    mpSessionView view;

    // Active FFA humans are authoritative players without a team side. They
    // must receive a valid public projection, never synthetic team telemetry.
    mpMatchDisclosureRecipient_t ffaPlayer = Recipient(
        Role(MP_MATCH_ROLE_PLAYER), MP_MATCH_SIDE_NONE, true);
    CHECK(MPMatchDisclosureBuildGrant(policy, ffaPlayer, grant));
    CHECK(grant.valid && grant.principal == MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER &&
        grant.reason == MP_MATCH_DISCLOSURE_REASON_NONE);
    CHECK(grant.viewPolicy.ownSide == MP_MATCH_VIEW_SIDE_NONE);
    CHECK((grant.viewPolicy.audiences & ownSideAudience) == 0);
    CHECK((grant.viewPolicy.observerKinds & teamVitalKind) == 0);
    CHECK(grant.followSideMask == 0 && !grant.itemTimingAllowed);
    mpMatchViewSource_t source = SourceFor(ffaPlayer);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, ffaPlayer, source, view,
        &reason, &viewError));
    CHECK(view.teamVitalCount == 0 && view.followTargetCount == 0 &&
        view.itemTimingCount == 0);
    CHECK(CheckWireCounts(view, 0, 0, 0, false) == 0);

    // A captain remains a team role and cannot use the neutral-player shape.
    mpMatchDisclosureRecipient_t neutralCaptain = Recipient(
        Role(MP_MATCH_ROLE_PLAYER) | Role(MP_MATCH_ROLE_CAPTAIN),
        MP_MATCH_SIDE_NONE, true);
    CHECK(!MPMatchDisclosureBuildGrant(policy, neutralCaptain, grant));
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT);

    // A team player retains the existing own-side tactical projection.
    mpMatchDisclosureRecipient_t teamPlayer = Recipient(
        Role(MP_MATCH_ROLE_PLAYER), 1, true);
    CHECK(MPMatchDisclosureBuildGrant(policy, teamPlayer, grant));
    CHECK(grant.valid && grant.principal == MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER &&
        grant.reason == MP_MATCH_DISCLOSURE_REASON_NONE);
    CHECK(grant.viewPolicy.ownSide == 1);
    CHECK((grant.viewPolicy.audiences & ownSideAudience) != 0);
    CHECK((grant.viewPolicy.observerKinds & teamVitalKind) != 0);
    source = SourceFor(teamPlayer);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, teamPlayer, source, view,
        &reason, &viewError));
    CHECK(view.teamVitalCount == 1 && view.teamVitals[0].participantSide == 1);
    CHECK(CheckWireCounts(view, 1, 0, 0, false) == 0);

    // An inactive neutral observer stays a spectator and does not gain a
    // player principal or own-side audience from the FFA exception.
    mpMatchDisclosureRecipient_t spectator = Recipient(
        0, MP_MATCH_SIDE_NONE, false);
    CHECK(MPMatchDisclosureBuildGrant(policy, spectator, grant));
    CHECK(grant.valid &&
        grant.principal == MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR);
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_SPECTATOR_LOCKED);
    CHECK(grant.viewPolicy.ownSide == MP_MATCH_VIEW_SIDE_NONE);
    CHECK((grant.viewPolicy.audiences & ownSideAudience) == 0);
    source = SourceFor(spectator);
    CHECK(AddSafeObservers(source, policy));
    CHECK(MPMatchDisclosureBuildView(policy, spectator, source, view,
        &reason, &viewError));
    CHECK(view.teamVitalCount == 0 && view.followTargetCount == 0 &&
        view.itemTimingCount == 0);
    CHECK(CheckWireCounts(view, 0, 0, 0, false) == 0);
    return 0;
}

static int SpectatorLockMatrix() {
    mpMatchDisclosurePolicy_t policy = FullPolicy();
    mpMatchDisclosureGrant_t grant;
    mpMatchDisclosureRecipient_t neutral = Recipient(0, MP_MATCH_SIDE_NONE, false);
    CHECK(MPMatchDisclosureBuildGrant(policy, neutral, grant));
    CHECK(grant.followSideMask == 0 &&
        grant.reason == MP_MATCH_DISCLOSURE_REASON_SPECTATOR_LOCKED);

    neutral.invitationSideMask = MPMatchDisclosureSideBit(1);
    CHECK(MPMatchDisclosureBuildGrant(policy, neutral, grant));
    CHECK(grant.followSideMask == MPMatchDisclosureSideBit(1));
    CHECK(!MPMatchDisclosureCanFollow(policy, neutral, 101, 0, true));
    CHECK(MPMatchDisclosureCanFollow(policy, neutral, 102, 1, true));

    policy.allowSpectatorInvitations = false;
    CHECK(MPMatchDisclosureBuildGrant(policy, neutral, grant));
    CHECK(grant.followSideMask == 0);

    policy.lockedSpectatorSideMask = MPMatchDisclosureSideBit(1);
    CHECK(MPMatchDisclosureBuildGrant(policy, neutral, grant));
    CHECK(grant.followSideMask == MPMatchDisclosureSideBit(0));

    policy.lockedSpectatorSideMask = 0;
    CHECK(MPMatchDisclosureBuildGrant(policy, neutral, grant));
    CHECK(grant.followSideMask == MPMatchDisclosureAllSideBits());

    mpMatchDisclosureRecipient_t affiliated = Recipient(
        Role(MP_MATCH_ROLE_PLAYER), 0, false);
    affiliated.invitationSideMask = MPMatchDisclosureSideBit(1);
    policy.lockedSpectatorSideMask = MPMatchDisclosureAllSideBits();
    policy.allowSpectatorInvitations = true;
    CHECK(MPMatchDisclosureBuildGrant(policy, affiliated, grant));
    CHECK(grant.followSideMask == MPMatchDisclosureSideBit(0));
    CHECK(MPMatchDisclosureCanFollow(policy, affiliated, 101, 0, true));
    CHECK(!MPMatchDisclosureCanFollow(policy, affiliated, 102, 1, true));
    CHECK(!MPMatchDisclosureCanFollow(policy, affiliated, 101, 0, false));
    CHECK(!MPMatchDisclosureCanFollow(policy, affiliated,
        MP_MATCH_INVALID_PARTICIPANT_ID, 0, true));
    return 0;
}

static int DisabledAndHostileMatrix() {
    mpMatchDisclosurePolicy_t policy;
    policy.Clear();
    mpMatchDisclosureGrant_t grant;

    mpMatchDisclosureRecipient_t coach = Recipient(Role(MP_MATCH_ROLE_COACH), 0, false);
    CHECK(MPMatchDisclosureBuildGrant(policy, coach, grant));
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_COACH_OBSERVATION_DISABLED &&
        grant.followSideMask == 0 && !grant.itemTimingAllowed);

    mpMatchDisclosureRecipient_t broadcaster = Recipient(
        Role(MP_MATCH_ROLE_BROADCASTER), MP_MATCH_SIDE_NONE, false);
    CHECK(MPMatchDisclosureBuildGrant(policy, broadcaster, grant));
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_BROADCAST_OBSERVATION_DISABLED);

    mpMatchDisclosureRecipient_t referee = Recipient(
        Role(MP_MATCH_ROLE_REFEREE), MP_MATCH_SIDE_NONE, false);
    CHECK(MPMatchDisclosureBuildGrant(policy, referee, grant));
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_REFEREE_OBSERVATION_DISABLED);
    CHECK((grant.viewPolicy.audiences &
        MPMatchViewAudienceBit(MP_MATCH_VIEW_AUDIENCE_REFEREE)) != 0);

    mpMatchDisclosureRecipient_t invalid = Recipient(
        Role(MP_MATCH_ROLE_PLAYER) | Role(MP_MATCH_ROLE_BROADCASTER), 0, true);
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));
    CHECK(grant.reason == MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT);
    invalid = Recipient(Role(MP_MATCH_ROLE_CAPTAIN), 0, false);
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));
    invalid = Recipient(Role(MP_MATCH_ROLE_COACH), MP_MATCH_SIDE_NONE, false);
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));
    invalid = Recipient(Role(MP_MATCH_ROLE_REFEREE), 0, false);
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));
    invalid = Recipient(Role(MP_MATCH_ROLE_SERVER_OPERATOR), MP_MATCH_SIDE_NONE, false);
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));
    invalid = Recipient(Role(MP_MATCH_ROLE_BROADCASTER), MP_MATCH_SIDE_NONE, true);
    invalid.repeater = true;
    CHECK(!MPMatchDisclosureBuildGrant(policy, invalid, grant));

    policy = FullPolicy();
    mpMatchDisclosureRecipient_t player = Recipient(Role(MP_MATCH_ROLE_PLAYER), 0, true);
    mpMatchViewSource_t source = SourceFor(player);
    CHECK(AddSafeObservers(source, policy));
    mpSessionView view;
    view.Clear();
    view.publicState.viewRevision = 77;
    mpMatchDisclosureReason_t reason;
    mpMatchViewError_t viewError;

    mpMatchDisclosureRecipient_t stale = player;
    stale.sessionRevision++;
    CHECK(!MPMatchDisclosureBuildView(policy, stale, source, view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_SOURCE_BINDING_MISMATCH &&
        view.publicState.viewRevision == 77);
    stale = player;
    stale.bindingGeneration++;
    CHECK(!MPMatchDisclosureBuildView(policy, stale, source, view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_SOURCE_BINDING_MISMATCH &&
        view.publicState.viewRevision == 77);

    mpMatchViewSource_t hostile = source;
    CHECK(PushVital(hostile, MP_MATCH_VIEW_AUDIENCE_PUBLIC,
        MP_MATCH_VIEW_SIDE_NONE, 1, 999));
    CHECK(!MPMatchDisclosureBuildView(policy, player, hostile, view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE &&
        view.publicState.viewRevision == 77);

    hostile = source;
    CHECK(PushFollow(hostile, MP_MATCH_VIEW_AUDIENCE_PUBLIC,
        MP_MATCH_VIEW_SIDE_NONE, 1));
    CHECK(!MPMatchDisclosureBuildView(policy, player, hostile, view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE);

    hostile = source;
    mpMatchViewObserverCandidate_t &publicItem =
        hostile.observerCandidates[hostile.observerCandidateCount++];
    CHECK(publicItem.SetItemTiming(MP_MATCH_VIEW_AUDIENCE_PUBLIC,
        MP_MATCH_VIEW_SIDE_NONE, "mega", 600, false));
    CHECK(!MPMatchDisclosureBuildView(policy, player, hostile, view, &reason, &viewError));
    CHECK(reason == MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE);
    return 0;
}

static int ItemDelayMatrix() {
    mpMatchDisclosurePolicy_t policy = FullPolicy();
    mpMatchViewObserverCandidate_t candidate;
    mpMatchTime notBefore;
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
        mpMatchTime::FromMilliseconds(199), mpMatchTime::FromMilliseconds(100),
        "quad", mpMatchTime::FromMilliseconds(500), false, candidate,
        &notBefore) == MP_MATCH_DISCLOSURE_ITEM_DELAYED);
    CHECK(notBefore.Milliseconds() == 200 && candidate.tokenLength == 0);
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
        mpMatchTime::FromMilliseconds(99), mpMatchTime::FromMilliseconds(100),
        "quad", mpMatchTime::FromMilliseconds(500), false, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_CLOCK_REGRESSION);
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
        mpMatchTime::FromMilliseconds(INT64_MAX),
        mpMatchTime::FromMilliseconds(INT64_MAX - 50),
        "quad", mpMatchTime::FromMilliseconds(INT64_MAX), false, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_CLOCK_OVERFLOW);
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_OWN_SIDE,
        mpMatchTime::FromMilliseconds(200), mpMatchTime::FromMilliseconds(100),
        "quad", mpMatchTime::FromMilliseconds(500), false, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED);
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
        mpMatchTime::FromMilliseconds(200), mpMatchTime::FromMilliseconds(100),
        "bad token", mpMatchTime::FromMilliseconds(500), false, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_REJECTED);
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
        mpMatchTime::FromMilliseconds(200), mpMatchTime::FromMilliseconds(100),
        "quad", mpMatchTime::FromMilliseconds(500), false, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_READY);
    CHECK(candidate.tokenLength == 4 && candidate.matchDeadlineMsec == 500);

    policy.itemTimingDelayMsec = 0;
    CHECK(MPMatchDisclosureSetItemTimingCandidate(policy,
        MP_MATCH_VIEW_AUDIENCE_REFEREE,
        mpMatchTime::FromMilliseconds(100), mpMatchTime::FromMilliseconds(100),
        "mega", mpMatchTime::FromMilliseconds(600), true, candidate) ==
        MP_MATCH_DISCLOSURE_ITEM_READY);

    policy.allowBroadcasterItemTiming = false;
    policy.allowRefereeItemTiming = false;
    policy.itemTimingDelayMsec = 1;
    CHECK(!policy.Validate());
    return 0;
}

int main() {
    mpMatchDisclosurePolicy_t defaults;
    defaults.Clear();
    CHECK(defaults.Validate());
    CHECK(defaults.lockedSpectatorSideMask == MPMatchDisclosureAllSideBits());
    int failure = RoleAndWireMatrix();
    if (failure != 0) { fprintf(stderr, "RoleAndWireMatrix:%d\n", failure); return failure; }
    failure = PlayerRecipientShapeMatrix();
    if (failure != 0) { fprintf(stderr, "PlayerRecipientShapeMatrix:%d\n", failure); return failure; }
    failure = SpectatorLockMatrix();
    if (failure != 0) { fprintf(stderr, "SpectatorLockMatrix:%d\n", failure); return failure; }
    failure = DisabledAndHostileMatrix();
    if (failure != 0) { fprintf(stderr, "DisabledAndHostileMatrix:%d\n", failure); return failure; }
    failure = ItemDelayMatrix();
    if (failure != 0) { fprintf(stderr, "ItemDelayMatrix:%d\n", failure); return failure; }
    return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_disclosure_policy_contract: executable checks skipped (no C++ compiler)")
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-disclosure-", dir=temp_root) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_disclosure_contract.cpp"
        executable = temp_dir / (
            "match_disclosure_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_disclosure_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            "-DMP_MATCH_DISCLOSURE_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            str(SESSION_SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone disclosure contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"disclosure invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout
                + ran.stderr
            )


def main() -> None:
    static_contracts(read(HEADER), read(SOURCE))
    integration_contract(
        read(MULTIPLAYER_SOURCE), read(PLAYER_SOURCE), read(GAME_STATE_SOURCE)
    )
    source_listing_contract()
    executable_contract()
    print("mp_match_disclosure_policy_contract: PASS")


if __name__ == "__main__":
    main()
