#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Hostile native contracts for managed team text/voice routing policy."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/game/mp/match/MatchTeamCommunication.h"
SOURCE = ROOT / "src/game/mp/match/MatchTeamCommunication.cpp"
SESSION_SOURCE = ROOT / "src/game/mp/match/MatchSession.cpp"
MULTIPLAYER_HEADER = ROOT / "src/game/MultiplayerGame.h"
MULTIPLAYER_SOURCE = ROOT / "src/game/MultiplayerGame.cpp"
VOICE_SOURCE = ROOT / "src/game/mp/VoiceComms.cpp"


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


def function_body(text: str, signature: str, context: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature!r} in {context}")
    brace = text.find("{", start)
    if brace < 0:
        raise AssertionError(f"missing body for {signature!r} in {context}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : index + 1]
    raise AssertionError(f"unterminated body for {signature!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "mpMatchTeamCommunicationBinding_t",
        "sessionId",
        "participant",
        "slotGeneration",
        "MPMatchBuildTeamCommunicationBinding",
        "MPMatchEvaluateManagedTeamCommunication",
        "MPMatchMayReceiveManagedTeamText",
        "MPMatchMayReceiveManagedTeamVoice",
        "MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_BINDING_STALE",
        "MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_BINDING_STALE",
        "MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_INELIGIBLE",
        "MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_INELIGIBLE",
        "MP_MATCH_TEAM_COMMUNICATION_REASON_SIDE_MISMATCH",
        "casual routing remains outside this core and therefore unchanged",
    ):
        require(combined, token, "managed team-communication boundary")

    for forbidden in (
        "MultiplayerGame.h",
        "VoiceComms",
        "Game_local.h",
        "idPlayer",
        "idBitMsg",
        "idUserInterface",
        "idCVar",
        "cmdSystem",
        "networkSystem",
        "gameLocal",
        "new ",
        "idList<",
        "idStr ",
    ):
        reject(combined, forbidden, "dependency-light routing core")

    for token in (
        "session.ResolveSlotBinding( binding.slot, binding.slotGeneration",
        "resolvedParticipant != binding.participant",
        "binding.sessionId != session.GetSessionId()",
        "!participant.human",
        "participant.roles == player",
        "participant.roles == ( player | captain )",
        "participant.roles == coach",
        "senderState->side != recipientState->side",
    ):
        require(source, token, "binding-safe same-side policy")
    if source.count("session.FindParticipant( resolvedParticipant )") != 1:
        raise AssertionError(
            "current endpoint resolution should perform one participant lookup"
        )

    require_before(
        source,
        "ResolveCurrentBinding( session, sender, senderState )",
        "IsManagedTeamCommunicator( *senderState )",
        "sender binding before sender-role decision",
    )
    require_before(
        source,
        "ResolveCurrentBinding( session, recipient, recipientState )",
        "IsManagedTeamCommunicator( *recipientState )",
        "recipient binding before recipient-role decision",
    )


def live_adapter_contracts(
    multiplayer_header: str, multiplayer_source: str, voice_source: str
) -> None:
    for token in (
        '#include "mp/match/MatchTeamCommunication.h"',
        "IsManagedTeamCommunicationActive",
        "BuildManagedTeamCommunicationBinding",
    ):
        require(multiplayer_header, token, "live adapter declarations")

    active = function_body(
        multiplayer_source,
        "bool idMultiplayerGame::IsManagedTeamCommunicationActive",
        "managed communication activation",
    )
    for token in (
        "gameLocal.isServer",
        "!gameLocal.isClient",
        "!gameLocal.isRepeater",
        "competitiveRulesInitialized",
        "competitiveRulesValidForSession",
        "matchRules.Committed().Revision() != 0",
        "matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH )",
        "matchRules.Committed().GetInteger( MP_RULE_GAME_TYPE ) == gameLocal.gameType",
        "matchSession.GetSessionId() != 0",
    ):
        require(active, token, "validated committed managed-session gate")

    binding = function_body(
        multiplayer_source,
        "bool idMultiplayerGame::BuildManagedTeamCommunicationBinding",
        "connection-generation binding adapter",
    )
    for token in (
        "binding.Clear()",
        "IsManagedTeamCommunicationActive()",
        "player->IsFakeClient()",
        "botManager.IsBot( clientNum )",
        "matchSession.GetSlotGeneration( clientNum, slotGeneration )",
        "MPMatchBuildTeamCommunicationBinding( matchSession, clientNum",
    ):
        require(binding, token, "connection-generation binding adapter")

    chat = function_body(
        multiplayer_source,
        "void idMultiplayerGame::ProcessChatMessage",
        "managed text adapter",
    )
    for token in (
        "managedTeamCommunicationRequested",
        "BuildManagedTeamCommunicationBinding( clientNum, managedSender )",
        "MPMatchMayReceiveManagedTeamText( matchSession, managedSender",
        "BuildManagedTeamCommunicationBinding( i, managedRecipient )",
        "managedRecipient ) || to->IsPlayerMuted( p )",
        "to->team == p->team",
        # the whole team, an eliminated member included; not someone who left to watch
        "!to->wantSpectate",
        "!managedTeamCommunication",
    ):
        require(chat, token, "managed text adapter and casual fallback")
    require_before(
        chat,
        "if ( managedTeamCommunicationRequested )",
        "else if ( p->spectating",
        "authoritative coach routing before legacy spectator routing",
    )
    require_before(
        chat,
        "BuildManagedTeamCommunicationBinding( clientNum, managedSender )",
        "outMsg.WriteString( suffixed_name )",
        "sender authorization before managed text serialization",
    )
    managed_case = chat[chat.find("case 2:") :]
    require_before(
        managed_case,
        "BuildManagedTeamCommunicationBinding( i, managedRecipient )",
        "MPMatchMayReceiveManagedTeamText( matchSession, managedSender",
        "recipient binding before managed text policy",
    )
    policy_at = managed_case.find(
        "MPMatchMayReceiveManagedTeamText( matchSession, managedSender"
    )
    delivery_at = managed_case.find("ServerSendReliableMessage( i, outMsg )", policy_at)
    if policy_at < 0 or delivery_at < 0 or policy_at >= delivery_at:
        raise AssertionError("managed text policy must dominate network delivery")

    voice = function_body(
        voice_source,
        "void idMultiplayerGame::ReceiveAndForwardVoiceData",
        "managed voice adapter",
    )
    for token in (
        "managedTeamCommunicationRequested",
        "BuildManagedTeamCommunicationBinding( clientNum, managedSender )",
        "BuildManagedTeamCommunicationBinding( i,",
        "ManagedVoiceTransportAllows",
        "to->IsPlayerMuted( from )",
        "CanTalk( from, to",
        "to->AllowedVoiceDest( from->entityNumber )",
        "payloadBytes > MAX_VOICE_PACKET_SIZE",
    ):
        target = voice_source if token == "to->IsPlayerMuted( from )" else voice
        require(target, token, "managed voice adapter and transport safeguards")
    if voice.count("MPMatchMayReceiveManagedTeamVoice(") != 2:
        raise AssertionError(
            "managed voice must authorize sender/self and every exact recipient, including the listen host"
        )
    if voice.count("to->AllowedVoiceDest( from->entityNumber )") != 1:
        raise AssertionError("voice destination limiting must remain on the unified exact-recipient path")
    require_before(
        voice,
        "BuildManagedTeamCommunicationBinding( clientNum, managedSender )",
        "outMsg.WriteData( inMsg.GetReadData(), payloadBytes )",
        "sender authorization before voice packet serialization",
    )
    first_voice_policy = voice.find(
        "MPMatchMayReceiveManagedTeamVoice( matchSession, managedSender"
    )
    first_voice_delivery = voice.find("networkSystem->ServerSendVoice(")
    if (
        first_voice_policy < 0
        or first_voice_delivery < 0
        or first_voice_policy >= first_voice_delivery
    ):
        raise AssertionError("managed voice policy must dominate network delivery")

    # Text, names, and encoded voice bytes are payload, never role/team authority.
    adapters = chat + voice
    for match in re.finditer(
        r"MPMatchMayReceiveManagedTeam(?:Text|Voice)\([^;]+;", adapters, re.DOTALL
    ):
        call = match.group(0)
        for forbidden in (" text", " name", "inMsg", "payloadBytes"):
            reject(call, forbidden, "content-independent live authorization call")


HARNESS = r'''
#include "game/mp/match/MatchTeamCommunication.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK( condition ) do { if ( !( condition ) ) { \
	fprintf( stderr, "team communication check failed at line %d\n", __LINE__ ); \
	return __LINE__; } } while ( 0 )

static bool Applied( const mpMatchMutationResult &result ) {
	return result.code == MP_MATCH_MUTATION_APPLIED;
}

static bool ConfigureTeamSession( mpMatchSession &session, uint64_t sessionId ) {
	if ( !session.Reset( sessionId,
			mpMatchEngineTime::FromMilliseconds( 0 ) ) ) {
		return false;
	}
	mpMatchReadinessPolicy policy;
	policy.policy = MP_MATCH_READY_INDIVIDUAL;
	policy.teamMode = true;
	if ( !Applied( session.ConfigureReadiness( policy,
			session.GetSessionRevision() ) ) ) {
		return false;
	}
	return Applied( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED,
		mpParticipantId::Invalid(), session.GetSessionRevision() ) );
}

static bool BindEndpoint( mpMatchSession &session, int slot, bool human,
		mpMatchRoleMask_t roles, int side, bool active,
		mpMatchTeamCommunicationBinding_t &outBinding,
		mpParticipantId *outParticipant = NULL ) {
	mpParticipantId participant;
	if ( !Applied( session.BindParticipant( slot, human, roles,
			session.GetSessionRevision(), participant ) ) ) {
		return false;
	}
	if ( side != MP_MATCH_SIDE_NONE &&
		!Applied( session.SetParticipantSide( participant, side,
			session.GetSessionRevision() ) ) ) {
		return false;
	}
	if ( active && !Applied( session.SetParticipantActive( participant, true,
			session.GetSessionRevision() ) ) ) {
		return false;
	}
	uint32_t generation = 0;
	if ( !session.GetSlotGeneration( slot, generation ) ||
		!MPMatchBuildTeamCommunicationBinding( session, slot, generation,
			outBinding ) ) {
		return false;
	}
	if ( outParticipant != NULL ) {
		*outParticipant = participant;
	}
	return true;
}

static bool IsAllowedForBothMedia( const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &sender,
		const mpMatchTeamCommunicationBinding_t &recipient ) {
	return MPMatchMayReceiveManagedTeamText( session, sender, recipient ) &&
		MPMatchMayReceiveManagedTeamVoice( session, sender, recipient );
}

static bool IsDeniedForBothMedia( const mpMatchSession &session,
		const mpMatchTeamCommunicationBinding_t &sender,
		const mpMatchTeamCommunicationBinding_t &recipient ) {
	return !MPMatchMayReceiveManagedTeamText( session, sender, recipient ) &&
		!MPMatchMayReceiveManagedTeamVoice( session, sender, recipient );
}

int main( void ) {
	const mpMatchRoleMask_t player = MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
	const mpMatchRoleMask_t captain = MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN );
	const mpMatchRoleMask_t coach = MPMatchRoleBit( MP_MATCH_ROLE_COACH );
	const mpMatchRoleMask_t broadcaster =
		MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER );
	const mpMatchRoleMask_t referee = MPMatchRoleBit( MP_MATCH_ROLE_REFEREE );

	mpMatchSession session;
	CHECK( ConfigureTeamSession( session, 41 ) );
	mpMatchTeamCommunicationBinding_t player0;
	mpMatchTeamCommunicationBinding_t captain0;
	mpMatchTeamCommunicationBinding_t coach0;
	mpMatchTeamCommunicationBinding_t player1;
	mpMatchTeamCommunicationBinding_t coach1;
	mpMatchTeamCommunicationBinding_t spectator;
	mpMatchTeamCommunicationBinding_t broadcastObserver;
	mpMatchTeamCommunicationBinding_t refereeObserver;
	mpMatchTeamCommunicationBinding_t fakePlayer;
	mpMatchTeamCommunicationBinding_t substitute;
	CHECK( BindEndpoint( session, 0, true, player, 0, true, player0 ) );
	CHECK( BindEndpoint( session, 1, true, player | captain, 0, true,
		captain0 ) );
	CHECK( BindEndpoint( session, 2, true, coach, 0, false, coach0 ) );
	CHECK( BindEndpoint( session, 3, true, player, 1, true, player1 ) );
	CHECK( BindEndpoint( session, 4, true, coach, 1, false, coach1 ) );
	CHECK( BindEndpoint( session, 5, true, player, MP_MATCH_SIDE_NONE,
		false, spectator ) );
	CHECK( BindEndpoint( session, 6, true, broadcaster, 0, false,
		broadcastObserver ) );
	CHECK( BindEndpoint( session, 7, true, referee, 0, false,
		refereeObserver ) );
	CHECK( BindEndpoint( session, 8, false, player, 0, true, fakePlayer ) );
	CHECK( BindEndpoint( session, 9, true, 0, 0, false, substitute ) );
	CHECK( session.ValidateInvariants() );

	// Active player/captain and inactive coach form one same-side audience.
	CHECK( IsAllowedForBothMedia( session, player0, player0 ) );
	CHECK( IsAllowedForBothMedia( session, player0, captain0 ) );
	CHECK( IsAllowedForBothMedia( session, captain0, coach0 ) );
	CHECK( IsAllowedForBothMedia( session, coach0, player0 ) );
	CHECK( IsAllowedForBothMedia( session, coach0, coach0 ) );

	// Opponents and every non-team principal fail closed in both directions.
	CHECK( IsDeniedForBothMedia( session, player0, player1 ) );
	CHECK( IsDeniedForBothMedia( session, player0, coach1 ) );
	CHECK( IsDeniedForBothMedia( session, player0, spectator ) );
	CHECK( IsDeniedForBothMedia( session, player0, broadcastObserver ) );
	CHECK( IsDeniedForBothMedia( session, player0, refereeObserver ) );
	CHECK( IsDeniedForBothMedia( session, player0, fakePlayer ) );
	CHECK( IsDeniedForBothMedia( session, player0, substitute ) );
	CHECK( IsDeniedForBothMedia( session, spectator, player0 ) );
	CHECK( IsDeniedForBothMedia( session, broadcastObserver, player0 ) );
	CHECK( IsDeniedForBothMedia( session, refereeObserver, player0 ) );
	CHECK( IsDeniedForBothMedia( session, fakePlayer, player0 ) );
	CHECK( IsDeniedForBothMedia( session, substitute, player0 ) );

	CHECK( MPMatchEvaluateManagedTeamCommunication( session, player0, player1,
		MP_MATCH_TEAM_COMMUNICATION_TEXT ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_SIDE_MISMATCH );
	CHECK( MPMatchEvaluateManagedTeamCommunication( session, fakePlayer, player0,
		MP_MATCH_TEAM_COMMUNICATION_TEXT ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_INELIGIBLE );
	CHECK( MPMatchEvaluateManagedTeamCommunication( session, player0, spectator,
		MP_MATCH_TEAM_COMMUNICATION_VOICE ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_INELIGIBLE );

	// Exhaust the one-byte medium domain: only the two declared values pass.
	for ( unsigned int raw = 0; raw <= UINT8_MAX; ++raw ) {
		const mpMatchTeamCommunicationDecision_t decision =
			MPMatchEvaluateManagedTeamCommunication( session, player0, coach0,
				static_cast<mpMatchTeamCommunicationMedium_t>( raw ) );
		if ( raw < MP_MATCH_TEAM_COMMUNICATION_MEDIUM_COUNT ) {
			CHECK( decision.IsAllowed() );
		} else {
			CHECK( !decision.IsAllowed() );
			CHECK( decision.reason ==
				MP_MATCH_TEAM_COMMUNICATION_REASON_INVALID_MEDIUM );
		}
	}

	// Malformed and repeater/unbound endpoints never acquire a team audience.
	mpMatchTeamCommunicationBinding_t unbound;
	unbound.Clear();
	CHECK( !unbound.IsStructurallyValid() );
	CHECK( MPMatchEvaluateManagedTeamCommunication( session, unbound, player0,
		MP_MATCH_TEAM_COMMUNICATION_TEXT ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_SENDER_BINDING_STALE );
	CHECK( MPMatchEvaluateManagedTeamCommunication( session, player0, unbound,
		MP_MATCH_TEAM_COMMUNICATION_VOICE ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_RECIPIENT_BINDING_STALE );
	mpMatchTeamCommunicationBinding_t malformed = player0;
	++malformed.slotGeneration;
	CHECK( IsDeniedForBothMedia( session, malformed, coach0 ) );
	malformed = coach0;
	malformed.sessionId = 9999;
	CHECK( IsDeniedForBothMedia( session, player0, malformed ) );
	mpMatchTeamCommunicationBinding_t buildOutput = player0;
	CHECK( !MPMatchBuildTeamCommunicationBinding( session, -1, 0,
		buildOutput ) );
	CHECK( !buildOutput.IsStructurallyValid() );
	CHECK( !MPMatchBuildTeamCommunicationBinding( session,
		MP_MATCH_MAX_CONNECTION_SLOTS, 1, buildOutput ) );
	CHECK( !buildOutput.IsStructurallyValid() );

	// Slot reuse must not inherit the disconnected participant's audience.
	mpMatchTeamCommunicationBinding_t stalePlayer = player0;
	CHECK( Applied( session.UnbindParticipant( stalePlayer.slot,
		stalePlayer.slotGeneration, session.GetSessionRevision() ) ) );
	mpMatchTeamCommunicationBinding_t replacement;
	CHECK( BindEndpoint( session, stalePlayer.slot, true, player, 0, true,
		replacement ) );
	CHECK( replacement.slotGeneration != stalePlayer.slotGeneration );
	CHECK( IsDeniedForBothMedia( session, stalePlayer, coach0 ) );
	CHECK( IsAllowedForBothMedia( session, replacement, coach0 ) );

	// A new session invalidates every retained binding even when slots/generation
	// values are later reused.
	const mpMatchTeamCommunicationBinding_t priorSessionSender = replacement;
	const mpMatchTeamCommunicationBinding_t priorSessionRecipient = coach0;
	CHECK( ConfigureTeamSession( session, 42 ) );
	CHECK( IsDeniedForBothMedia( session, priorSessionSender,
		priorSessionRecipient ) );

	// This core is intentionally not the casual routing policy.
	mpMatchSession casual;
	CHECK( casual.Reset( 77, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( MPMatchEvaluateManagedTeamCommunication( casual, priorSessionSender,
		priorSessionRecipient, MP_MATCH_TEAM_COMMUNICATION_TEXT ).reason ==
		MP_MATCH_TEAM_COMMUNICATION_REASON_NOT_TEAM_MODE );

	// Exhaust the one-byte role-mask domain through the authoritative session
	// API.  Every accepted role combination gets an actual binding and decision;
	// only active player, active captain and inactive coach are communicators.
	for ( unsigned int raw = 0; raw <= UINT8_MAX; ++raw ) {
		mpMatchSession roleSession;
		CHECK( ConfigureTeamSession( roleSession, 1000 + raw ) );
		const mpMatchRoleMask_t roles =
			static_cast<mpMatchRoleMask_t>( raw );
		mpParticipantId roleParticipant;
		const mpMatchMutationResult bind = roleSession.BindParticipant( 0, true,
			roles, roleSession.GetSessionRevision(), roleParticipant );
		const bool roleMaskValid = MPMatchRoleMaskIsValid( roles, true );
		CHECK( bind.WasRejected() != roleMaskValid );
		if ( !roleMaskValid ) {
			continue;
		}
		CHECK( Applied( roleSession.SetParticipantSide( roleParticipant, 0,
			roleSession.GetSessionRevision() ) ) );
		const bool activeRole = roles == player || roles == ( player | captain );
		if ( activeRole ) {
			CHECK( Applied( roleSession.SetParticipantActive( roleParticipant, true,
				roleSession.GetSessionRevision() ) ) );
		}
		uint32_t roleGeneration = 0;
		CHECK( roleSession.GetSlotGeneration( 0, roleGeneration ) );
		mpMatchTeamCommunicationBinding_t roleBinding;
		CHECK( MPMatchBuildTeamCommunicationBinding( roleSession, 0,
			roleGeneration, roleBinding ) );
		mpMatchTeamCommunicationBinding_t roleRecipient;
		CHECK( BindEndpoint( roleSession, 1, true, player, 0, true,
			roleRecipient ) );
		const bool expected = activeRole || roles == coach;
		CHECK( MPMatchMayReceiveManagedTeamText( roleSession, roleBinding,
			roleRecipient ) == expected );
		CHECK( MPMatchMayReceiveManagedTeamVoice( roleSession, roleBinding,
			roleRecipient ) == expected );
		CHECK( MPMatchMayReceiveManagedTeamText( roleSession, roleRecipient,
			roleBinding ) == expected );
		CHECK( MPMatchMayReceiveManagedTeamVoice( roleSession, roleRecipient,
			roleBinding ) == expected );
		CHECK( roleSession.ValidateInvariants() );
	}

	return 0;
}
'''


def source_discovery_contract() -> None:
    lister = ROOT / "tools/build/list_sources.py"
    listed = subprocess.run(
        [
            os.environ.get("PYTHON", "python"),
            str(lister),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if listed.returncode != 0:
        raise AssertionError("source discovery failed:\n" + listed.stdout + listed.stderr)
    if "game/mp/match/MatchTeamCommunication.cpp" not in listed.stdout.replace(
        "\\", "/"
    ):
        raise AssertionError(
            "MatchTeamCommunication.cpp is not discovered by the GameLib source glob"
        )


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print(
            "mp_match_team_communication_contract: native checks skipped "
            "(no C++ compiler)"
        )
        return

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="match-team-communication-", dir=temp_root
    ) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_team_communication_contract.cpp"
        executable = temp_dir / (
            "match_team_communication_contract.exe"
            if compiler.lower().endswith(".exe")
            else "match_team_communication_contract"
        )
        harness.write_text(HARNESS, encoding="utf-8")
        compiled = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DMP_MATCH_SESSION_STANDALONE_TEST",
                f"-I{ROOT / 'src'}",
                str(harness),
                str(SOURCE),
                str(SESSION_SOURCE),
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone team-communication contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run(
            [str(executable)], cwd=temp_dir, text=True, capture_output=True
        )
        if ran.returncode != 0:
            raise AssertionError(
                "team-communication native decision failed "
                f"(exit {ran.returncode}):\n" + ran.stdout + ran.stderr
            )


def main() -> None:
    static_contracts(read(HEADER), read(SOURCE))
    live_adapter_contracts(
        read(MULTIPLAYER_HEADER), read(MULTIPLAYER_SOURCE), read(VOICE_SOURCE)
    )
    source_discovery_contract()
    executable_contract()
    print("mp_match_team_communication_contract: PASS")


if __name__ == "__main__":
    main()
