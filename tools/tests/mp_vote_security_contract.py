#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Guard multiplayer ready, admin-message, and vote security invariants."""

from __future__ import annotations

import re
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
def _resolve_engine_root() -> Path:
    """Locate a sibling openQ4 checkout, if the caller has one.

    The directory is named "openQ4"; hardcoding "OpenQ4" only ever resolved on
    case-insensitive filesystems, so on Linux the engine-side assertions were
    skipped even when the engine was checked out beside this repository.
    """

    override = os.environ.get("OPENQ4_ENGINE_REPO", "").strip()
    if override:
        return Path(override)
    # The game sources live in the engine repository now, so this checkout is
    # the engine. Looking for a sibling named openQ4 first tested a different
    # checkout from a worktree, or none at all.
    if (ROOT / "src/framework").is_dir():
        return ROOT
    for name in ("openQ4", "OpenQ4"):
        candidate = ROOT.parent / name
        if candidate.is_dir():
            return candidate
    return ROOT.parent / "openQ4"


ENGINE_ROOT = _resolve_engine_root()


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="replace")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
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
    raise AssertionError(f"unterminated {signature}")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {needle!r} in {context}")


def reject(text: str, needle: str, context: str) -> None:
    if needle in text:
        raise AssertionError(f"unexpected {needle!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(
            f"expected {first!r} before {second!r} in {context}"
        )


def case_block(function: str, label: str, next_label: str) -> str:
    start = function.find(label)
    end = function.find(next_label, start + len(label))
    if start < 0 or end < 0:
        raise AssertionError(f"missing case range {label!r}..{next_label!r}")
    return function[start:end]


def main() -> None:
    multiplayer = read("src/mpgame/MultiplayerGame.cpp")
    multiplayer_h = read("src/mpgame/MultiplayerGame.h")
    network = read("src/mpgame/Game_network.cpp")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")
    # The GUI lives in the engine repository, which this repository's CI does
    # not check out. Skip only the engine-side assertions when it is absent,
    # the way the other cross-repository contracts here do.
    mpmain_path = ENGINE_ROOT / "content/baseoq4/pak0/guis/mpmain.gui"
    mpmain = (
        mpmain_path.read_text(encoding="utf-8", errors="replace")
        if mpmain_path.is_file()
        else None
    )

    ready = body(multiplayer, "bool idMultiplayerGame::AllPlayersReady")
    reject(ready, "team[ p->team ]", "ready scan spectator safety")

    set_ready = body(multiplayer, "void idMultiplayerGame::ServerSetPlayerReady")
    require(set_ready, "!playerState[ clientNum ].ingame", "ready in-game eligibility")
    require(set_ready, "player->wantSpectate", "ready spectator eligibility")
    require(set_ready, "!MPIsHumanMatchParticipant( player )", "ready bot eligibility")
    human_participant = body(multiplayer, "static bool MPIsHumanMatchParticipant")
    require(human_participant, "!botManager.IsBot( player->entityNumber )", "AI slot eligibility")
    send_ready = body(multiplayer, "void idMultiplayerGame::SendReady")
    require(send_ready, "outMsg.WriteByte( isReady ? 1 : 0 )", "ready byte transport")
    reject(send_ready, "WriteBits( isReady", "ready partial-byte transport")

    slot_check = body(multiplayer, "static bool IsValidVotePlayerSlot")
    require(slot_check, "clientNum < 0", "vote slot lower bound")
    require(slot_check, "clientNum >= gameLocal.numClients", "vote slot live range")
    require(slot_check, "clientNum >= MAX_CLIENTS", "vote slot storage range")
    require(slot_check, "gameLocal.entities[ clientNum ]", "vote slot entity lookup")
    require(slot_check, "entity->IsType( idPlayer::GetClassType() )", "vote slot player type")

    eligible_vote = body(multiplayer, "static bool IsEligibleVotePlayerSlot")
    require(eligible_vote, "gameLocal.mpGame.IsInGame", "vote electorate connection state")
    require(eligible_vote, "!player->spectating", "vote electorate spectator exclusion")
    require(eligible_vote, "!player->wantSpectate", "vote electorate pending spectator exclusion")
    require(eligible_vote, "!player->IsFakeClient()", "vote electorate special-view exclusion")
    require(eligible_vote, "!botManager.IsBot( clientNum )", "vote electorate bot exclusion")

    bounded_parse = body(multiplayer, "static bool ParseBoundedVoteInteger")
    require(bounded_parse, "text[ 0 ] == '\\0'", "strict bounded integer empty input")
    require(bounded_parse, "maxExclusive <= 0", "strict bounded integer range")
    require(
        bounded_parse,
        "*cursor < '0' || *cursor > '9'",
        "strict bounded integer syntax",
    )
    require(
        bounded_parse,
        "parsed > maxValue / 10",
        "strict bounded integer overflow protection",
    )
    require(
        bounded_parse,
        "digit > maxValue % 10",
        "strict bounded integer upper bound",
    )

    ranged_parse = body(multiplayer, "static bool ParseVoteIntegerRange")
    require(ranged_parse, "maxValue < minValue", "strict ranged integer bounds")
    require(
        ranged_parse,
        "*cursor < '0' || *cursor > '9'",
        "strict ranged integer syntax",
    )
    require(ranged_parse, "parsed > maxValue / 10", "strict ranged overflow protection")
    require(ranged_parse, "parsed < minValue", "strict ranged lower bound")

    bounded_string = body(multiplayer, "static bool HasBoundedMessageString")
    require(bounded_string, "msg.GetReadBit() != 0", "typed string byte alignment")
    require(bounded_string, "memchr", "typed string terminator bound")

    slot_parse = body(multiplayer, "static bool ParseVotePlayerSlot")
    require(
        slot_parse,
        "ParseBoundedVoteInteger( text, MAX_CLIENTS, clientNum )",
        "legacy slot parse bound",
    )
    require(slot_parse, "IsValidVotePlayerSlot( clientNum )", "legacy live-player validation")

    legacy_flag = body(multiplayer, "static int LegacyVoteFieldFlag")
    for vote_name, field_name in (
        ("VOTE_RESTART", "VOTEFLAG_RESTART"),
        ("VOTE_BUYING", "VOTEFLAG_BUYING"),
        ("VOTE_AUTOBALANCE", "VOTEFLAG_TEAMBALANCE"),
        ("VOTE_KICK", "VOTEFLAG_KICK"),
        ("VOTE_MAP", "VOTEFLAG_MAP"),
        ("VOTE_NEXTMAP", "VOTEFLAG_MAP"),
        ("VOTE_GAMETYPE", "VOTEFLAG_GAMETYPE"),
        ("VOTE_TIMELIMIT", "VOTEFLAG_TIMELIMIT"),
        ("VOTE_ROUNDLIMIT", "VOTEFLAG_TOURNEYLIMIT"),
        ("VOTE_CAPTURELIMIT", "VOTEFLAG_CAPTURELIMIT"),
        ("VOTE_FRAGLIMIT", "VOTEFLAG_FRAGLIMIT"),
        ("VOTE_CONTROLTIME", "VOTEFLAG_CONTROLTIME"),
    ):
        require(legacy_flag, vote_name, f"legacy vote policy mapping for {vote_name}")
        require(legacy_flag, field_name, f"legacy vote policy bit for {vote_name}")

    packed = body(multiplayer, "void idMultiplayerGame::ServerCallPackedVote")
    require_before(
        packed,
        "if ( !IsEligibleVotePlayerSlot( clientNum ) )",
        "const int wireFieldFlags = msg.ReadShort();",
        "packed in-game caller validation",
    )
    require_before(
        packed,
        "matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH )",
        "const int wireFieldFlags = msg.ReadShort();",
        "managed packed-vote authority convergence",
    )

    for token in (
        "wireFieldFlags & ~VOTEFLAG_ALL",
        "msg.GetRemainingReadBits() != 0",
        "wireFieldFlags & disallowedVotes",
        "if ( !validVote )",
        "outMsg.WriteByte( voteData.m_buying ? 1 : 0 );",
        "outMsg.WriteShort( idMath::ClampShort( voteData.m_controlTime ) );",
    ):
        require(packed if "wireFieldFlags" in token or "validVote" in token or "Remaining" in token else multiplayer, token, "atomic packed vote transport")
    require_before(
        packed,
        "if ( !IsValidVotePlayerSlot( voteData.m_kick ) )",
        "voteData.m_kick == gameLocal.localClientNum",
        "packed kick target validation",
    )
    require(
        packed,
        "voteData.m_fieldFlags &= ( ~VOTEFLAG_BUYING );",
        "packed buying same-value flag clear",
    )
    require(
        packed,
        "!HasBoundedMessageString( msg, sizeof( buffer ) )",
        "packed map token bound",
    )
    require(
        packed,
        "voteData.m_teamBalance != 0 && voteData.m_teamBalance != 1",
        "packed team-balance boolean validation",
    )
    reject(packed, "voteData.m_buying &= ( ~VOTEFLAG_BUYING );", "packed buying value")

    packed_client = body(multiplayer, "void idMultiplayerGame::ClientStartPackedVote")
    require_before(
        packed_client,
        "if ( !IsValidVotePlayerSlot( clientNum ) )",
        "gameLocal.userInfo[ clientNum ]",
        "packed caller userInfo access",
    )
    require_before(
        packed_client,
        "!IsValidVotePlayerSlot( voteData.m_kick )",
        "gameLocal.userInfo[ currentVoteData.m_kick ]",
        "packed kick userInfo access",
    )

    packed_execute = body(multiplayer, "void idMultiplayerGame::ExecutePackedVote")
    require_before(
        packed_execute,
        "IsValidVotePlayerSlot( currentVoteData.m_kick )",
        'va( "kick %d", currentVoteData.m_kick )',
        "packed kick execution",
    )

    legacy = body(multiplayer, "void idMultiplayerGame::ServerCallVote")
    require_before(
        legacy,
        "if ( !IsEligibleVotePlayerSlot( clientNum ) )",
        "voteIndex = (vote_flags_t)msg.ReadByte",
        "legacy in-game caller validation",
    )
    require_before(
        legacy,
        "matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH )",
        "voteIndex = (vote_flags_t)msg.ReadByte",
        "managed legacy-vote authority convergence",
    )
    require_before(
        legacy,
        "const int legacyFieldFlag = LegacyVoteFieldFlag( voteIndex );",
        "switch ( voteIndex )",
        "legacy vote policy mapping",
    )
    require_before(
        legacy,
        'gameLocal.serverInfo.GetInt( "si_voteFlags" ) & legacyFieldFlag',
        "switch ( voteIndex )",
        "legacy si_voteFlags enforcement",
    )
    require_before(
        legacy,
        "!HasBoundedMessageString( msg, sizeof( value ) )",
        "switch ( voteIndex )",
        "legacy vote string bound",
    )
    require_before(
        legacy,
        "msg.GetRemainingReadBits() != 0",
        "switch ( voteIndex )",
        "legacy vote trailing-data rejection",
    )
    for vote_name, next_vote in (
        ("VOTE_TIMELIMIT", "VOTE_FRAGLIMIT"),
        ("VOTE_FRAGLIMIT", "VOTE_GAMETYPE"),
        ("VOTE_BUYING", "VOTE_CAPTURELIMIT"),
        ("VOTE_CAPTURELIMIT", "VOTE_ROUNDLIMIT"),
        ("VOTE_AUTOBALANCE", "VOTE_CONTROLTIME"),
        ("VOTE_CONTROLTIME", "default:"),
    ):
        legacy_numeric = case_block(
            legacy, f"case {vote_name}:", f"case {next_vote}:" if next_vote != "default:" else "default:"
        )
        require(
            legacy_numeric,
            "ParseVoteIntegerRange",
            f"strict legacy numeric parse for {vote_name}",
        )
        reject(legacy_numeric, "strtol", f"legacy numeric parse for {vote_name}")
    legacy_gametype = case_block(legacy, "case VOTE_GAMETYPE:", "case VOTE_KICK:")
    require(
        legacy_gametype,
        "ParseBoundedVoteInteger( value, MPVoteGameTypeCount(), vote_gameTypeIndex )",
        "strict legacy game-type parse",
    )
    reject(legacy_gametype, "strtol", "legacy game-type parse")
    reject(legacy_gametype, "assert", "legacy game-type validation")
    legacy_kick = case_block(legacy, "case VOTE_KICK:", "case VOTE_MAP:")
    require_before(
        legacy_kick,
        "ParseVotePlayerSlot( value, vote_clientNum )",
        "gameLocal.userInfo[ vote_clientNum ]",
        "legacy kick userInfo access",
    )

    legacy_client = body(multiplayer, "void idMultiplayerGame::ClientStartVote")
    require_before(
        legacy_client,
        "if ( !IsValidVotePlayerSlot( clientNum ) )",
        "gameLocal.userInfo[ clientNum ]",
        "legacy caller userInfo access",
    )

    execute = body(multiplayer, "void idMultiplayerGame::ExecuteVote")
    execute_kick = case_block(execute, "case VOTE_KICK:", "case VOTE_MAP:")
    require_before(
        execute_kick,
        "ParseVotePlayerSlot( voteValue.c_str(), kickClientNum )",
        'va( "kick %d", kickClientNum )',
        "legacy kick execution",
    )

    check_vote = body(multiplayer, "void idMultiplayerGame::CheckVote")
    require_before(
        check_vote,
        "AbortInheritedVoteForManagedMatch()",
        "if ( voteExecTime )",
        "managed vote abort before inherited delayed execution",
    )
    require(check_vote, "voteEligibleCount <= 0", "frozen electorate empty check")
    require(check_vote, "float(yesVotes) / voteEligibleCount", "frozen yes threshold")
    require(check_vote, "float(noVotes) / voteEligibleCount", "frozen no threshold")
    reject(check_vote, "numVoters", "moving vote denominator")

    managed_abort = body(
        multiplayer, "bool idMultiplayerGame::AbortInheritedVoteForManagedMatch"
    )
    require(
        managed_abort,
        "matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH )",
        "managed inherited-vote authority check",
    )
    require(managed_abort, "ClientUpdateVote( VOTE_ABORTED", "managed vote abort notice")
    require(managed_abort, "voteExecTime = 0", "managed delayed execution cancellation")
    require(managed_abort, "voteEligibleCount = 0", "managed electorate reset")
    require(managed_abort, "playerState[ i ].vote = PLAYER_VOTE_NONE", "managed ballot reset")
    require(managed_abort, "ClearVote();", "managed vote presentation reset")

    clear_vote = body(multiplayer, "void idMultiplayerGame::ClearVote")
    require(clear_vote, "clientNum < 0 || clientNum >= MAX_CLIENTS", "vote clear slot bounds")
    for token in (
        'SetStateInt( "voteNotice", 0 )',
        'SetStateString( "voteNoticeText", "" )',
        'SetStateString( va( "voteInfo_%d", line ), "" )',
        'SetStateInt( "vote_going", 0 )',
        'SetStateInt( "playerVoted", 0 )',
        'SetStateString( "voteCount", "" )',
        'DeleteStateVar( va( "voteData_item_%d", item ) )',
    ):
        require(clear_vote, token, "complete inherited-vote presentation clear")

    update_vote_lifecycle = body(multiplayer, "void idMultiplayerGame::ClientUpdateVote")
    require(
        update_vote_lifecycle,
        "VOTE_ABORTED == status",
        "aborted vote terminal presentation clear",
    )
    require(
        update_vote_lifecycle,
        "else if ( mainGui )",
        "terminal vote count remains cleared",
    )

    cast_vote = body(multiplayer, "void idMultiplayerGame::CastVote")
    require_before(
        cast_vote,
        "matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH )",
        "playerState[ clientNum ].vote",
        "managed legacy-ballot authority convergence",
    )

    start_vote = body(multiplayer, "void idMultiplayerGame::ServerStartVote")
    start_packed = body(multiplayer, "void idMultiplayerGame::ServerStartPackedVote")
    for start_body, label in ((start_vote, "legacy"), (start_packed, "packed")):
        require(start_body, "voteEligibleCount = 0", f"{label} electorate reset")
        require(start_body, "IsEligibleVotePlayerSlot( i )", f"{label} electorate snapshot")
        require(start_body, "voteEligibleCount++", f"{label} electorate count")

    callvote = body(multiplayer, "void idMultiplayerGame::CallVote_f")
    require(callvote, "MPGameTypeByName( szArg2 )", "console gametype lookup")
    require(callvote, "MPGameTypeIsSelectable", "console hidden-gametype rejection")
    require(callvote, "ParseVoteIntegerRange", "console typed numeric parsing")

    if not re.search(r"#define\s+NUM_VOTES\s+12\b", multiplayer_h):
        raise AssertionError("vote-mask loop must cover CONTROLTIME bit 11")
    require(multiplayer_h, "#define VOTEFLAG_ALL", "known packed vote mask")
    require(cvars, '"bit 11 (+2048) control time"', "CONTROLTIME vote-mask help")

    server_messages = body(network, "void idGameLocal::ServerProcessReliableMessage")
    admin = case_block(
        server_messages,
        "case GAME_RELIABLE_MESSAGE_SERVER_ADMIN:",
        "case GAME_RELIABLE_MESSAGE_GETADMINBANLIST:",
    )
    require_before(
        admin,
        "if ( !isListenServer || clientNum != localClientNum )",
        "mpGame.HandleServerAdminKickPlayer",
        "server-admin sender authentication",
    )
    require(admin, "targetClientNum", "server-admin target separation")
    reject(admin, "int clientNum = msg.ReadByte();", "server-admin sender shadowing")

    ban_list = case_block(
        server_messages,
        "case GAME_RELIABLE_MESSAGE_GETADMINBANLIST:",
        "case GAME_RELIABLE_MESSAGE_GETVOTEMAPS:",
    )
    require_before(
        ban_list,
        "if ( !isListenServer || clientNum != localClientNum )",
        "ServerSendBanList( clientNum );",
        "ban-list sender authentication",
    )

    reliable_string = body(network, "static bool HasBoundedReliableString")
    require(reliable_string, "memchr", "reliable typed string terminator bound")
    client_messages = body(network, "void idGameLocal::ClientProcessReliableMessage")
    update_vote = case_block(
        client_messages,
        "case GAME_RELIABLE_MESSAGE_UPDATEVOTE:",
        "case GAME_RELIABLE_MESSAGE_PORTALSTATES:",
    )
    for token in (
        "result < idMultiplayerGame::VOTE_UPDATE",
        "yesCount > MAX_CLIENTS",
        "multiVote != 0 && multiVote != 1",
        "voteData.m_fieldFlags & ~VOTEFLAG_ALL",
        "!HasBoundedReliableString( msg, sizeof( mapNameBuffer ) )",
        "voteData.m_gameType >= MPVoteGameTypeCount()",
        "msg.GetRemainingReadBits() != 0",
    ):
        require(update_vote, token, "vote-update structural validation")

    ready_message = case_block(
        server_messages,
        "case GAME_RELIABLE_MESSAGE_READY:",
        "default:",
    )
    require(ready_message, "msg.GetRemainingReadBits() != 8", "ready payload width")
    require(ready_message, "readyValue != 0 && readyValue != 1", "ready boolean validation")

    cast_vote_message = case_block(
        server_messages,
        "case GAME_RELIABLE_MESSAGE_CASTVOTE:",
        "case GAME_RELIABLE_MESSAGE_CALLPACKEDVOTE:",
    )
    require(cast_vote_message, "msg.GetRemainingReadBits() != 8", "cast-vote payload width")
    require(
        cast_vote_message,
        "voteValue != 0 && voteValue != 1",
        "cast-vote exact boolean validation",
    )
    require_before(
        cast_vote_message,
        "voteValue != 0 && voteValue != 1",
        "mpGame.CastVote",
        "cast-vote validation before mutation",
    )

    if mpmain is None:
        print(
            "mp_vote_security_contract: skipped mpmain.gui checks "
            f"(no engine checkout at {ENGINE_ROOT})"
        )
    else:
        require(
            mpmain,
            'if( "gui::si_allowVoting" == 1 && "gui::si_managedMatch" == 0)',
            "managed in-game vote menu hiding",
        )
        require(mpmain, "windowDef vote_managed_notice", "managed vote local rejection")
        require(mpmain, 'text\t"#str_41773"', "localized managed vote rejection")
        require(
            mpmain,
            'if ( "gui::si_managedMatch" == 1 )',
            "managed vote panel fail-closed guard",
        )

    require(
        multiplayer,
        "uiKickSelection >= 0 && uiKickSelection < MAX_CLIENTS && IsCurrentPlayerTarget( kickVoteMap[ uiKickSelection ], kickVoteMapIdentities[ uiKickSelection ] )",
        "packed vote GUI kick selection",
    )
    require(
        multiplayer,
        "sel < 0 || sel >= MAX_CLIENTS || !IsCurrentPlayerTarget( kickVoteMap[ sel ], kickVoteMapIdentities[ sel ] )",
        "packed vote GUI kick label selection",
    )

    print("mp_vote_security_contract: ok")


if __name__ == "__main__":
    main()
