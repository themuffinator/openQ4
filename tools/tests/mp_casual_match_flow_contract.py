#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Pin the casual match-flow surface of roadmap MP-12.

Casual servers get the votes Quake players type (callvote/cv with the new
fields), a pass percentage, a per-map vote limit and a spectator-vote policy;
team timeouts and a technical pause from the console (si_timeouts); a spectator
lock; an inactivity policy; a pause when a playing human disconnects; and an
intermission that names the next map and reads out the awards. Every piece
keeps its default off or at the inherited value, and the managed match keeps
its own typed authority path.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGUAGES = ("english", "german", "french", "italian", "spanish", "brazilian", "czech",
             "hungarian", "polish", "russian", "turkish", "ukrainian")
STRING_IDS = tuple(f"#str_{value}" for value in range(42751, 42781))


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unbalanced body for {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def before(text: str, first: str, second: str, label: str) -> None:
    require(first in text and second in text and text.index(first) < text.index(second), label)


def check_votes() -> None:
    header = read("src/mpgame/MultiplayerGame.h")
    game = read("src/mpgame/MultiplayerGame.cpp")
    network = read("src/mpgame/Game_network.cpp")
    commands = read("src/mpgame/gamesys/SysCmds.cpp")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")

    for name, value in (("NEXTMAP", 0x1000), ("ROUNDLIMIT", 0x2000), ("RULESET", 0x4000), ("INSTAGIB", 0x8000),
                        ("OVERTIME", 0x10000), ("COINTOSS", 0x20000), ("RANDOMMAP", 0x40000), ("READYALL", 0x80000)):
        require(re.search(rf"VOTEFLAG_{name}\s*=\s*0x{value:X},", header, re.IGNORECASE) is not None,
                f"VOTEFLAG_{name} keeps bit value 0x{value:X}")
    # MP-18 added VOTEFLAG_MUTE at bit 20 (tools/tests/mp_moderation.py pins it); the
    # modifier, spectator chat, preset and arena votes (MP-14, MP-16) take bits 21 to 24
    for name, value in (("MODIFIER", 0x200000), ("SPECTATORCHAT", 0x400000), ("PRESET", 0x800000), ("ARENA", 0x1000000)):
        require(re.search(rf"VOTEFLAG_{name}\s*=\s*0x{value:X},", header, re.IGNORECASE) is not None,
                f"VOTEFLAG_{name} keeps bit value 0x{value:X}")
    require(re.search(r"#define\s+NUM_VOTES\s+25\b", header) is not None, "NUM_VOTES covers the new flags")
    # their payloads follow the MP-12 fields on every message, only when their flags are set
    require(game.count("WriteVoteExtensionFields( outMsg, voteData );") == 3, "the three vote writers send the extension fields")
    require(network.count("idMultiplayerGame::ReadVoteExtensionFields( msg, voteData )") == 2, "both client vote readers read them")
    require("ReadVoteExtensionFields( msg, voteData )" in function_body(game, "void idMultiplayerGame::ServerCallPackedVote"),
            "the packed vote call reads them before the kick identity")
    before(function_body(game, "void idMultiplayerGame::ServerCallPackedVote"), "ReadVoteExtensionFields( msg, voteData )",
           "ReadVoteKickTarget( msg, voteData.m_kick )", "the extension fields come before the kick identity")
    writer = function_body(game, "void idMultiplayerGame::WriteVoteExtensionFields")
    for flag in ("VOTEFLAG_MODIFIER", "VOTEFLAG_SPECTATORCHAT", "VOTEFLAG_PRESET", "VOTEFLAG_ARENA"):
        require(flag in writer, f"{flag} has a payload only when set")
    for word in ('"modifier"', '"arena"', '"preset"', '"spectatorchat"'):
        require(word in function_body(game, "void idMultiplayerGame::CallVote_f"), f"callvote takes {word}")
    for field in ("m_roundLimit", "m_ruleset", "m_instagib", "m_overtime"):
        require(field in function_body(header, "struct voteStruct_t"), f"voteStruct_t carries {field}")

    for name, handler in (("callvote", "CallVote_f"), ("cv", "CallVote_f"), ("vote", "Vote_f"),
                          ("timeout", "Timeout_f"), ("timein", "TimeIn_f"), ("unpause", "TimeIn_f"), ("pause", "Pause_f")):
        require(re.search(rf'AddCommand\(\s*"{name}",\s*idMultiplayerGame::{handler}\b', commands) is not None,
                f"console command {name} reaches {handler}")

    # the flag word outgrew a short on every vote message, both ends
    require(game.count("outMsg.WriteLong( voteData.m_fieldFlags );") == 3, "three vote writers send the flag word as a long")
    require("const int wireFieldFlags = msg.ReadLong();" in game, "the packed vote call reads a long flag word")
    # (a Quake 4 1.4.2 network demo's votes keep their short, MP-22)
    require(network.count("voteData.m_fieldFlags = RetailSnapshotFormat() ? ( msg.ReadShort() & 0x0fff ) : msg.ReadLong();") == 2,
            "both client vote readers read a long flag word")
    require("voteData.m_fieldFlags = msg.ReadShort();" not in network and "outMsg.WriteShort( voteData.m_fieldFlags );" not in game,
            "no vote message still carries a short flag word")

    callvote = function_body(game, "void idMultiplayerGame::CallVote_f")
    for token in ('"nextmap"', '"shuffle"', '"balance"', '"scorelimit"', '"roundlimit"', '"ruleset"', '"instagib"',
                  '"overtime"', '"cointoss"', '"random"', '"readyall"'):
        require(token in callvote, f"callvote understands {token}")
    require("GTF_CAPTURELIMIT" in callvote, "scorelimit follows the game type's own limit")

    server_call = function_body(game, "void idMultiplayerGame::ServerCallPackedVote")
    before(server_call, "VoteLimitAccepted( clientNum )", "const int wireFieldFlags", "the per-map vote limit is applied before the payload")
    require('"#str_42756"' in server_call and '"si_spectatorVote"' in server_call, "a spectating human is told why their vote was refused")
    for flag in ("ROUNDLIMIT", "RULESET", "INSTAGIB", "OVERTIME"):
        require(f"voteData.m_fieldFlags & VOTEFLAG_{flag} ) ) {{" in server_call, f"the server decodes and validates {flag}")
    before(server_call, "VOTEFLAG_ROUNDLIMIT", "VOTEFLAG_RULESET", "roundlimit precedes ruleset on the wire")
    before(server_call, "VOTEFLAG_RULESET", "VOTEFLAG_INSTAGIB", "ruleset precedes instagib on the wire")
    before(server_call, "VOTEFLAG_INSTAGIB", "VOTEFLAG_OVERTIME", "instagib precedes overtime on the wire")
    before(server_call, "VOTEFLAG_OVERTIME", "ReadVoteKickTarget( msg, voteData.m_kick )", "the kick identity still closes the payload")
    require("VoteReadyAllAvailable()" in server_call and '"#str_42753"' in server_call, "ready-all needs a ready-up warmup")
    require("PickRandomMap( si_gameType.GetString(), candidate )" in server_call and '"#str_42754"' in server_call,
            "a random-map vote needs another map for the game type")
    require('gameLocal.serverInfo.GetInt( "si_voteFlags" ) & VOTEFLAG_ALL' in server_call, "si_voteFlags still masks the whole vote")

    execute = function_body(game, "void idMultiplayerGame::ExecutePackedVote")
    require("si_roundLimit.SetInteger( currentVoteData.m_roundLimit )" in execute, "roundlimit applies")
    require("si_overtime.SetInteger( currentVoteData.m_overtime )" in execute, "overtime applies")
    require("si_instagib.SetBool( currentVoteData.m_instagib != 0 )" in execute, "instagib applies through the server info")
    require("si_ruleset.SetInteger( currentVoteData.m_ruleset )" in execute and "needRestart = true;" in execute, "ruleset applies with a restart")
    require("ForceReady();" in execute, "ready-all forces readiness")
    require('"#str_42768" : "#str_42769"' in execute, "the coin toss announces heads or tails")
    require("PickRandomMap( si_gameType.GetString(), drawn )" in execute, "random map draws at execution")
    require('"serverNextMap\\n"' in execute, "nextmap advances the cycle")

    check_vote = function_body(game, "void idMultiplayerGame::CheckVote")
    require("g_votePercent.GetInteger()" in check_vote and "yesVotes * 100 >= voteEligibleCount * passPercent" in check_vote,
            "the pass percentage comes from g_votePercent")
    require("( voteEligibleCount - noVotes ) * 100 < voteEligibleCount * passPercent" in check_vote,
            "a vote fails once the undecided could no longer carry it")
    eligible = function_body(game, "static bool IsEligibleVotePlayerSlot")
    require('"si_spectatorVote"' in eligible and "botManager.IsBot( clientNum )" in eligible, "spectator policy never admits bots")
    lines = function_body(game, "void idMultiplayerGame::CasualVoteLines")
    for flag in ("NEXTMAP", "ROUNDLIMIT", "RULESET", "INSTAGIB", "OVERTIME", "COINTOSS", "RANDOMMAP", "READYALL"):
        require(f"VOTEFLAG_{flag}" in lines, f"the vote card names {flag}")
    require("CasualVoteLines( data, lines );" in function_body(game, "void idMultiplayerGame::RetainedVoteLines"),
            "the retained Vote page shows the new rows")
    require("CasualVoteLines( currentVoteData, casualLines );" in function_body(game, "void idMultiplayerGame::ClientStartPackedVote"),
            "the HUD notice shows the new rows")
    stamp = function_body(game, "void idMultiplayerGame::StampVoteRateLimit")
    require("votesCalledThisMap[ clientNum ]++" in stamp, "a started vote counts against g_voteLimit")
    require("votesCalledThisMap[ clientNum ] = 0;" in function_body(game, "void idMultiplayerGame::ResetVoteCooldownSlot"),
            "the vote count belongs to the connection")

    require(re.search(r'"g_votePercent",\s*"51",[^\n]*, 1, 100 \);', cvars) is not None, "g_votePercent defaults to the inherited majority")
    require(re.search(r'"g_voteLimit",\s*"0",', cvars) is not None, "g_voteLimit defaults to no limit")
    require(re.search(r'"si_spectatorVote",\s*"0",[^\n]*CVAR_SERVERINFO', cvars) is not None, "si_spectatorVote defaults off and is server info")
    for bit, label in ((12, "next map"), (13, "round limit"), (14, "ruleset"), (15, "instagib"), (16, "overtime"),
                       (17, "coin toss"), (18, "random map"), (19, "ready all")):
        require(f"bit {bit} (+{1 << bit}) {label}" in cvars, f"si_voteFlags help names bit {bit}")


def check_timeouts() -> None:
    game = read("src/mpgame/MultiplayerGame.cpp")
    rules = read("src/mpgame/mp/match/MatchRules.cpp")
    operations = read("src/mpgame/mp/match/MatchOperations.cpp")
    operations_h = read("src/mpgame/mp/match/MatchOperations.h")
    network = read("src/mpgame/Game_network.cpp")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")

    require(re.search(r'"si_timeouts",\s*"0",[^\n]*CVAR_SERVERINFO[^\n]*, 0, 10 \);', cvars) is not None, "si_timeouts defaults to none")
    require(re.search(r'"si_timeoutLength",\s*"60",[^\n]*, 10, 600 \);', cvars) is not None, "si_timeoutLength defaults to a minute")
    require("if ( timeoutCount > 0 && !ModeInMask( gameType, MP_RULE_MODES_TIMEOUT ) ) {" in rules
            and "( !managed || !ModeInMask( gameType, MP_RULE_MODES_TIMEOUT ) )" not in rules,
            "the rules core no longer confines timeouts to managed matches")
    import_body = function_body(game, "bool idMultiplayerGame::InitializeCompetitiveRules")
    require('gameLocal.serverInfo.GetInt( "si_timeouts" )' in import_body and "MPGameTypeHasAny( gameLocal.gameType, GTF_TEAM )" in import_body,
            "the casual import takes the budget from si_timeouts in team modes")
    require("MP_RULE_TEAM_TIMEOUT_COUNT, casualTimeouts, failure" in import_body
            and "MP_RULE_TEAM_TIMEOUT_SECONDS, casualTimeoutSeconds, failure" in import_body, "count and length import together")

    require("bool casualSideTimeout;" in operations_h and "casualSideTimeout = false;" in operations, "the adapter context carries the casual grant, off by default")
    authorized = function_body(operations, "static bool IsAuthorized")
    casual = authorized[authorized.index("context.casualSideTimeout"):]
    casual = casual[: casual.index("return true;")]
    require("principal.actorState->active" in casual and "IsPlayableSide( principal.actorState->side )" in casual
            and "MP_MATCH_OP_TIMEOUT_REQUEST" in casual and "MP_MATCH_OP_RESUME_REQUEST" in casual,
            "the casual grant covers an active side player's timeout and resume only")
    for opcode in ("MP_MATCH_OP_TECH_PAUSE_REQUEST", "MP_MATCH_OP_TEAM_LOCK_SET", "MP_MATCH_OP_FORFEIT"):
        require(opcode not in casual, f"the casual grant never reaches {opcode}")
    context = function_body(game, "void idMultiplayerGame::BuildMatchOperationContext")
    require("context.casualSideTimeout = !IsManagedMatch() && state != NULL && state->human &&" in context, "the grant needs a casual human")
    allowed = function_body(game, "mpMatchViewAllowedOperationMask_t idMultiplayerGame::AllowedMatchOperationsFor")
    require("matchSession.GetTimeoutBudget( state->side ).configured > 0" in allowed, "the view offers timeouts only with a budget")

    for name in ("Timeout_f", "TimeIn_f", "Pause_f"):
        body = function_body(game, f"void idMultiplayerGame::{name}")
        require("SubmitConsoleMatchOperation( request," in body, f"{name} goes through the typed operation path")
    require("MP_MATCH_OP_TIMEOUT_REQUEST" in function_body(game, "void idMultiplayerGame::Timeout_f"), "timeout requests a team timeout")
    require("MP_MATCH_OP_RESUME_REQUEST" in function_body(game, "void idMultiplayerGame::TimeIn_f"), "timein requests a resume")
    pause = function_body(game, "void idMultiplayerGame::Pause_f")
    require("MP_MATCH_OP_TECH_PAUSE_REQUEST" in pause and "MP_MATCH_ARG_REASON" in pause, "pause requests a technical pause with a reason")
    submit = function_body(game, "bool idMultiplayerGame::SubmitConsoleMatchOperation")
    require("mp.SubmitMatchOperation( request )" in submit and "ExecuteTrustedLocalMatchOperation( request, execution )" in submit,
            "clients submit through their view, the dedicated console as the operator")
    require("publicState.allowedOperations & MPMatchViewOperationBit( request.opcode )" in submit and "#str_42775" in submit,
            "a console request the accepted view does not allow is refused with a message before it is sent")
    require("mp.consoleMatchRequestId = allowed ? mp.nextClientMatchRequestId + 1 : 0;" in submit,
            "the console registers its request before the submit, since a listen host answers inside it")
    stored = function_body(game, "bool idMultiplayerGame::StoreClientMatchOperationResult")
    require("result.requestId == consoleMatchRequestId" in stored and "MP_MATCH_RESULT_REJECTED" in stored
            and "MPMatchControlLocalizationKey( result.localizationId )" in stored and "consoleMatchCommand.c_str()" in stored,
            "the server's refusal of a console request is printed where the command was typed")
    allowed = function_body(game, "mpMatchViewAllowedOperationMask_t idMultiplayerGame::AllowedMatchOperationsFor")
    require(".remaining > 0;" in allowed and "( casualSideTimeout && opcode == MP_MATCH_OP_TIMEOUT_REQUEST )" in allowed
            and "( casualSide && opcode == MP_MATCH_OP_RESUME_REQUEST )" in allowed,
            "the view's casual TIMEOUT bit follows the remaining budget, RESUME stays with the side")
    announce = function_body(game, "void idMultiplayerGame::AnnounceCasualPauseTransitions")
    require("if ( IsManagedMatch() ) {\n\t\treturn;\n\t}" in announce, "the centre prints stay off managed matches")
    for string in ("#str_42770", "#str_42771", "#str_42772", "#str_42773", "#str_42780"):
        require(f'"{string}"' in announce, f"the pause announcement uses {string}")
    require("mpGame.RebaseClientPauseClock( frameMsec );" in network, "the client's HUD clock follows a frozen frame")
    rebase = function_body(game, "void idMultiplayerGame::RebaseClientPauseClock")
    require("if ( !gameLocal.isClient || deltaMsec <= 0 )" in rebase and "ShiftCompetitiveDeadline( matchStartedTime, deltaMsec );" in rebase,
            "the client rebase moves only the match clock origin")


def check_policies() -> None:
    player = read("src/mpgame/Player.cpp")
    player_h = read("src/mpgame/Player.h")
    game = read("src/mpgame/MultiplayerGame.cpp")
    session = read("src/mpgame/mp/match/MatchSession.cpp")
    session_h = read("src/mpgame/mp/match/MatchSession.h")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")

    require(re.search(r'"si_spectatorLock",\s*"0",[^\n]*CVAR_SERVERINFO[^\n]*, 0, 2 \);', cvars) is not None, "si_spectatorLock defaults off")
    require(re.search(r'"g_inactivity",\s*"0",[^\n]*, 0, 3600 \);', cvars) is not None, "g_inactivity defaults off")
    require(re.search(r'"g_pauseOnDisconnect",\s*"0",[^\n]*, 0, 300 \);', cvars) is not None, "g_pauseOnDisconnect defaults off")

    userinfo = function_body(player, "bool idPlayer::UserInfoChanged( void )")
    require('gameLocal.serverInfo.GetInt( "si_spectatorLock" )' in userinfo, "the userinfo path reads the spectator lock")
    require("( liveState == SUDDENDEATH || ( livePlay && spectatorLock >= 1 ) ) && joiningPlay && !heldOpen" in userinfo,
            "lock 1 keeps spectators out during live play and sudden death stays as it was")
    # someone arriving with ui_autoJoin is held like a spectator coming back; bots fill by their own rules
    require("const bool arriving = !gameLocal.isClient && initialJoinPending && !botManager.IsBot( entityNumber );" in userinfo
            and "const bool joiningPlay = !spec && ( wantSpectate == true || arriving );" in userinfo,
            "the lock applies to a client that connects with ui_autoJoin")
    # the side a disconnect pause is held for may take its player back
    require('gameLocal.mpGame.DisconnectPauseAdmits( userInfo->GetString( "ui_team" ) )' in userinfo,
            "a disconnect pause lets the side it waits for take a player in under the lock")
    require("livePlay && spectatorLock >= 2 && spec && !wantSpectate" in userinfo, "lock 2 keeps players in")
    toggle = function_body(game, "void idMultiplayerGame::ToggleSpectate")
    require('"si_spectatorLock"' in toggle and '"#str_42776"' in toggle, "the client says why it cannot join")

    require("int\t\t\t\t\t\tinactivityLastInputTime;" in player_h and "void\t\t\t\t\tCheckInactivity( const usercmd_t &oldCmd );" in player_h,
            "idPlayer carries the inactivity clock")
    think = function_body(player, "void idPlayer::Think( void )")
    before(think, "usercmd = gameLocal.usercmds[", "CheckInactivity( oldCmd );", "the inactivity check sees the new command against the old one")
    inactivity = function_body(player, "void idPlayer::CheckInactivity")
    for token in ("gameLocal.isClient", "IsFakeClient()", "spectating", "botManager.IsBot( entityNumber )", "IsArenaCampaignMatch()"):
        require(token in inactivity, f"inactivity never counts {token}")
    require("g_inactivity.GetInteger() * 1000" in inactivity, "the limit is g_inactivity seconds")
    require('"#str_42777"' in inactivity and '"#str_42778"' in inactivity and '"#str_42779"' in inactivity, "warn, move and drop messages")
    before(inactivity, 'GetBool( "si_spectators" )', 'QueueClientDrop( entityNumber, "#str_42779" )', "spectators first, a drop only without them")
    # dropping a client deletes its player at once; this is that player's own Think
    require("ServerDropClient" not in inactivity, "the inactivity drop is queued, never made from inside the player's Think")
    queued = function_body(game, "void idMultiplayerGame::RunQueuedClientDrops")
    require("gameLocal.GetSpawnId( occupant ) != spawnId" in queued and "networkSystem->ServerDropClient( clientNum, reason );" in queued,
            "a queued drop runs for the occupant it was queued for")
    before(function_body(game, "void idMultiplayerGame::Run( void )"), "RunQueuedClientDrops();", "CommonRun();",
           "queued drops run before the frame's rules")
    require("SHIFT_PLAYER_TIME( inactivityLastInputTime );" in function_body(player, "void idPlayer::ThinkMatchPaused"),
            "a pause does not count as idle time")

    require("uint64_t expectedRevision, int durationMsec = 0 );" in session_h, "a technical pause stays open-ended by default")
    require("technicalPauseDurationMsec = durationMsec > 0 ? durationMsec : 0;" in function_body(session, "mpMatchMutationResult mpMatchSession::RequestTechnicalPause"),
            "a duration is recorded at the request")
    advance = function_body(session, "mpMatchMutationResult mpMatchSession::AdvanceFrame")
    require("( pause.kind == MP_MATCH_PAUSE_KIND_TECHNICAL && technicalPauseExpires )" in advance, "only a timed technical pause expires")
    require("technicalPauseExpires = true;" in advance, "the expiry is armed when the pause commits")
    clear = function_body(session, "void mpMatchSession::ClearPause")
    require("technicalPauseDurationMsec = 0;" in clear and "technicalPauseExpires = false;" in clear, "ClearPause disarms the expiry")

    disconnect = function_body(game, "void idMultiplayerGame::DisconnectClient")
    require("if ( !BeginDisconnectPause( disconnectedPlayingHuman && !serverDrop, disconnectedGameSide ) ) {" in disconnect,
            "the disconnect pause gates the abort check")
    # a kick or an idle drop is the server's own doing, not a lost connection
    require("const bool serverDrop = clientNum >= 0 && clientNum < MAX_CLIENTS && serverDropInFlight[ clientNum ];" in disconnect,
            "a client the server removed itself does not pause the match")
    require("disconnectedPlayingHuman = disconnectedState->human && disconnectedState->active;" in disconnect, "only a playing human pauses the match")
    begin = function_body(game, "bool idMultiplayerGame::BeginDisconnectPause")
    for token in ("g_pauseOnDisconnect.GetInteger()", "IsManagedMatch()", "!gameLocal.IsTeamGame()", "state != GAMEON && state != SUDDENDEATH",
                  "matchSession.GetPause().state != MP_MATCH_PAUSE_RUNNING", "MP_MATCH_PAUSE_REASON_PLAYER_DISCONNECT", "seconds * 1000",
                  "( disconnectPauseUsedMask & ( 1 << side ) ) != 0", "disconnectPauseHumans = CountPlayingHumans( side );"):
        require(token in begin, f"the disconnect pause checks {token}")
    # one pause a side a match, and it ends when the side has its player back
    back = function_body(game, "void idMultiplayerGame::CheckDisconnectPauseReturn")
    require("CountPlayingHumans( disconnectPauseSide ) > disconnectPauseHumans" in back
            and "matchSession.RequestResumeByAuthority( matchSession.GetSessionRevision() );" in back,
            "the disconnect pause ends once the side has a player more than it was left with")
    announce = function_body(game, "void idMultiplayerGame::AnnounceCasualPauseTransitions")
    require("abortCheckAfterPause" in announce and "CheckAbortGame();" in announce, "the abort check runs when the pause ends")
    require(announce.index("CheckAbortGame();") < announce.index("if ( IsManagedMatch() ) {"), "the deferred abort check does not depend on the announcement")


def check_strings() -> None:
    entry = re.compile(r'^\s*"(?P<id>#str_\d+)"\s+"(?P<value>.*)"\s*$')
    tables = {}
    for language in LANGUAGES:
        values = {}
        for line in read(f"content/baseoq4/pak0/strings/{language}_openq4.lang").splitlines():
            match = entry.match(line)
            if match:
                values[match.group("id")] = match.group("value")
        tables[language] = values
    english = tables["english"]
    for string_id in STRING_IDS:
        for language, values in tables.items():
            require(string_id in values and values[string_id].strip(), f"{language} defines {string_id}")
            require(values[string_id].count("%d") == english[string_id].count("%d") and values[string_id].count("%s") == english[string_id].count("%s"),
                    f"{language} {string_id} keeps the placeholders")
    sources = "".join(read(path) for path in ("src/mpgame/MultiplayerGame.cpp", "src/mpgame/Player.cpp"))
    referenced = set(re.findall(r'"(#str_427[5-8]\d)"', sources))
    for string_id in referenced:
        require(string_id in english, f"{string_id} used by the game code exists in the English table")
    for string_id in ("#str_42751", "#str_42757", "#str_42770", "#str_42776", "#str_42780"):
        require(string_id in referenced, f"{string_id} is used")


def check_intermission() -> None:
    game = read("src/mpgame/MultiplayerGame.cpp")
    host = read("src/mpgame/mp/MapDatabaseHost.cpp")
    cvars = read("src/mpgame/gamesys/SysCvar.cpp")

    require(re.search(r'"g_intermissionAnnounce",\s*"0",\s*CVAR_GAME \| CVAR_ARCHIVE \| CVAR_BOOL,', cvars) is not None,
            "g_intermissionAnnounce defaults off: a casual server keeps Quake 4's quiet scoreboard")
    gate = function_body(game, "bool idMultiplayerGame::IntermissionAnnounces( void ) const")
    require("gameLocal.isServer && g_intermissionAnnounce.GetBool() && !IsManagedMatch() && !IsArenaCampaignMatch()" in gate,
            "only a casual server that turned it on announces; a managed match and the Arena campaign keep their own flow")
    require("RunIntermissionAwards();" in function_body(game, "void idMultiplayerGame::Run( void ) {"), "the reel runs every frame")
    reel = function_body(game, "void idMultiplayerGame::RunIntermissionAwards( void ) {")
    for token, label in (
        ("gameState->GetMPGameState() != GAMEREVIEW", "the reel plays during the review only"),
        ("intermissionAwardsMatch != identityMatchStamp", "the reel is built once a match"),
        ("statManager->GetPlayerStat( i )", "it reads the stat manager's own end-of-match awards"),
        ("stat->endGameAwards[ round ]", "one award a player a round"),
        ('gameLocal.ServerSendChatMessage( -1, "server", endGameAwardInfo[ award ].name, va( ": %s", name ) );',
         "each award is read to everyone, localized by each client"),
        ("INTERMISSION_AWARD_GAP_MS", "one line every two seconds"),
    ):
        require(token in reel, label)
    ended = function_body(host, "void mpMapDatabaseHost::ServerMatchEnded( void ) {")
    require("if ( !ServerOfferPick() ) {" in ended and "ServerAnnounceNext();" in ended,
            "a pick names its own candidates; otherwise the intermission names the next map")
    announce = function_body(host, "void mpMapDatabaseHost::ServerAnnounceNext( void ) {")
    require("gameLocal.mpGame.IntermissionAnnounces()" in announce and "ServerPredictNext( &selection )" in announce and
            "intermissionAnnounced = database.Entry( selection.index )->token;" in announce,
            "the intermission names the map the rotation will load")
    advance = function_body(host, "mpMapAdvance_t mpMapDatabaseHost::ServerAdvance( void ) {")
    require("if ( intermissionAnnounced.Icmp( entry->token ) != 0 ) {" in advance,
            "the change does not name again the map the intermission named")
    require("intermissionAnnounced.Clear();" in function_body(host, "void mpMapDatabaseHost::ServerMapStarted( const char *map ) {"),
            "a new map forgets the last intermission")


def main() -> int:
    try:
        check_votes()
        check_timeouts()
        check_policies()
        check_strings()
        check_intermission()
    except (AssertionError, ValueError) as error:
        print(f"mp_casual_match_flow_contract: FAIL: {error}")
        return 1
    print("mp_casual_match_flow_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
