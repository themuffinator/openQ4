#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contracts for managed userinfo, roster and local-operator adapters."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def main() -> None:
    game_local = read("src/game/Game_local.cpp")
    player = read("src/game/Player.cpp")
    item = read("src/game/Item.cpp")
    multiplayer = read("src/game/MultiplayerGame.cpp")
    multiplayer_h = read("src/game/MultiplayerGame.h")
    operations = read("src/game/mp/match/MatchOperations.cpp")
    session = read("src/game/mp/match/MatchSession.cpp")
    syscmds = read("src/game/gamesys/SysCmds.cpp")

    reconcile = "gameLocal.mpGame.ServerReconcileManagedUserInfo( entityNumber"
    userinfo_changed = "static_cast<idPlayer *>( entities[ clientNum ] )->UserInfoChanged()"
    require(player, reconcile, "central player userinfo ingress")
    require(game_local, userinfo_changed, "player userinfo application")
    if reconcile in game_local:
        raise AssertionError("managed userinfo reconciliation is split across ingress paths")
    if player.index(reconcile) > player.index("spec = ( idStr::Icmp"):
        raise AssertionError("managed userinfo must be reconciled before spectator intent")

    for token in (
        "IsManagedMatch()",
        "matchTeams.EvaluateJoin(",
        "ApplyMatchTeamsTransaction( decision, execution )",
        "matchTeams.JoinQueue(",
        "ApplyMatchSpectatorTransition( participant, execution )",
        'info.Set( "ui_spectate", authoritativeSpectate )',
        "AdvanceMatchViewRevision( true )",
    ):
        require(multiplayer, token, "managed compatibility ingress")
    require(player, "gameLocal.mpGame.IsManagedMatch()", "managed initial join gate")
    require(player, "if ( managedMatch )", "managed spectator-policy bypass")
    require(multiplayer, 'serverInfo.SetBool( "si_managedMatch"',
            "client-visible managed match marker")
    # Managed Duel keeps accepted admission before the gameplay seats catch up.
    # The native userinfo contract exercises both stale and newly admitted seats.
    require(multiplayer,
        "const bool active = acceptedDuel != NULL ? acceptedDuel->active :",
            "durable participation intent")
    if "!player->wantSpectate &&\n\t\t!player->spectating" in multiplayer:
        raise AssertionError("transient death spectating still withdraws managed participants")

    for token in (
        "if ( player->IsFakeClient() )",
        "matchTeams.FindQueuePosition( participant )",
        "invitation->target == participant",
        "ApplyMatchSpectatorTransition( participant, execution )",
    ):
        require(multiplayer, token, "fail-closed managed bot cleanup")

    require(multiplayer_h, "ReconcileGameplayPhaseAfterMatchMutation",
            "countdown side-effect reconciliation declaration")
    require(multiplayer, "idMultiplayerGame::ReconcileGameplayPhaseAfterMatchMutation",
            "countdown side-effect reconciliation")
    if multiplayer.count("ReconcileGameplayPhaseAfterMatchMutation();") < 3:
        raise AssertionError("roster transactions do not consistently reconcile gameplay phase")

    for token in (
        "if ( active && phase != WARMUP && phase != COUNTDOWN && !casualLiveJoin )",
        "if ( side != MP_MATCH_SIDE_NONE && phase != WARMUP && phase != COUNTDOWN",
        "Vacating is also used by fail-closed withdrawal/disconnect handling",
    ):
        require(session, token, "live withdrawal rules")

    require(
        operations,
        "MP_OPERATION_CONTINUATION_ROSTER_REMOVE",
        "atomic roster removal",
    )
    require(
        multiplayer,
        "ApplyMatchSpectatorTransition( removed, execution )",
        "atomic roster removal adapter",
    )
    for token in (
        "MP_MATCH_OP_ROSTER_LEAVE",
        "MP_OPERATION_CONTINUATION_ROSTER_LEAVE",
        "MP_MATCH_CAP_SELF_ROSTER_LEAVE",
        "CanSelfLeaveRoster",
    ):
        require(operations + session, token, "self-only roster withdrawal")
    require(multiplayer, "kind == MP_OPERATION_CONTINUATION_ROSTER_LEAVE",
            "self-only roster withdrawal adapter")
    if "replacement == principal.target || session.FindRosterSeat( replacement ) >= 0" in operations:
        raise AssertionError("persistent bench substitutes are rejected before the team core")

    for token in (
        "MPOperationBroadcasterTargetIsValid",
        "MP_MATCH_OP_BROADCASTER_SET",
        "MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER )",
    ):
        require(operations, token, "typed broadcaster operation")
    require(multiplayer_h, "Broadcaster_f", "trusted-local broadcaster adapter")
    require(multiplayer, "idMultiplayerGame::Broadcaster_f", "trusted-local broadcaster adapter")
    require(syscmds, '"matchBroadcaster"', "dedicated operator command")
    require(
        syscmds,
        'AddCommand( "forceReady"',
        "documented typed force-ready operator command",
    )

    force_team_start = multiplayer.index(
        "void idMultiplayerGame::ForceTeamChange_f"
    )
    force_team_end = multiplayer.index(
        "void idMultiplayerGame::RemoveClientFromBanList_f", force_team_start
    )
    force_team = multiplayer[force_team_start:force_team_end]
    for token in (
        "gameLocal.mpGame.IsManagedMatch()",
        "ParseBoundedVoteInteger( args.Argv( 1 ), clientLimit, clientNum )",
        "clientNum >= 0 && clientNum < clientLimit",
    ):
        require(force_team, token, "bounded legacy force-team command")
    if "atoi" in force_team or "gameLocal.entities[ atoi" in force_team:
        raise AssertionError("force-team command retains unchecked numeric indexing")

    for token in (
        '#include "mp/match/MatchItemTiming.h"',
        "mpMatchItemTimingRegistry matchItemTiming",
        "matchItemTiming.BeginMap( matchSession.GetSessionId() )",
        "InitializeMatchItemTimingObservations();",
        "CompetitiveItemTimingKind( const idItem *item )",
        'idStr::Icmp( className, "powerup_quad_damage" )',
        'idStr::Icmp( className, "item_health_mega" )',
        'idStr::Icmp( className, "item_armor_large_mp" )',
        "static_cast<uint64_t>( item->entityNumber ) + 1u",
        "matchItemTiming.ProjectCandidate( observation->sourceId",
        "matchViewObservedItemTimingRevision !=",
        "matchItemTiming.GetRevision()",
    ):
        require(multiplayer_h + multiplayer, token,
                "authoritative item-timing live adapter")
    for token in (
        "ObserveCompetitiveItemPickup( this, respawn )",
        "ObserveCompetitiveItemAvailable( this )",
    ):
        require(item, token, "authoritative item lifecycle hook")
    if ".SetItemTiming(" in multiplayer:
        raise AssertionError("live adapter bypasses item disclosure projection")

    print("mp_match_live_adapter_contract: PASS")


if __name__ == "__main__":
    main()
