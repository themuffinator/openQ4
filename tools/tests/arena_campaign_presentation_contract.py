#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static contract checks for the Arena Campaign match presentation."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(
            f"Expected {first!r} before {second!r} in {context}"
        )


def main() -> None:
    multiplayer = read("src/game/MultiplayerGame.cpp")
    multiplayer_h = read("src/game/MultiplayerGame.h")
    player = read("src/game/Player.cpp")
    game_state = read("src/game/mp/GameState.cpp")
    round_state = read("src/game/mp/RoundGameState.cpp")
    round_modes_h = read("src/game/mp/RoundModes.h")

    # The feature is gated by the framework-owned campaign token. Ordinary MP
    # must keep its existing ready-up, spectator and camera paths.
    require(multiplayer, 'GetInt( "si_arenaCampaign" ) > 0', "campaign predicate")
    require(multiplayer_h, "bool\t\t\tIsArenaCampaignMatch( void ) const;", "public campaign predicate")
    require(multiplayer, "if ( IsArenaCampaignMatch() ) {\n\t\treadyPlayerCount = numClients;", "ready-up bypass")
    require_before(
        multiplayer,
        "if ( IsArenaCampaignMatch() ) {\n\t\treadyPlayerCount = numClients;",
        "int readyCount = 0;",
        "Arena ready-up bypass ordering",
    )
    require(game_state, "if ( !keepArenaTableau ) {\n\t\t\t\t\tplayer->ServerSpectate( true );", "normal MP review spectating")

    # Arena always gets a real, readable entrance countdown and presentation.
    require(game_state, "ARENA_CAMPAIGN_ENTRANCE_MIN_MSEC = 3000", "entrance minimum")
    # NewState is a guarded transition now, so pin the arena branch itself
    # rather than a bare statement that any gametype could satisfy.
    require(
        game_state,
        "gameLocal.mpGame.AllPlayersReady() && NewState( COUNTDOWN )",
        "Arena countdown transition",
    )
    require(game_state, "BeginArenaCampaignEntrancePresentation();", "entrance GUI event")
    require(game_state, "ClearArenaCampaignPresentation();", "presentation cleanup")
    require(
        game_state,
        "if ( arenaCampaign ) {\n\t\t\t\tplayer->GUIMainNotice( \"\" );\n\t\t\t} else if( gameLocal.gameType != GAME_TOURNEY )",
        "Arena warmup-notice suppression",
    )
    require(
        game_state,
        "} else if( currentState == COUNTDOWN ) {\n\t\t\tif ( arenaCampaign ) {\n\t\t\t\tplayer->GUIMainNotice( \"\" );\n\t\t\t} else if( gameLocal.gameType != GAME_TOURNEY )",
        "Arena countdown-notice suppression",
    )
    require(game_state, "ScheduleAnnouncerSound( AS_GENERAL_THREE", "countdown announcer preserved")
    require(game_state, "ScheduleAnnouncerSound( AS_GENERAL_FIGHT", "fight announcer preserved")
    require(
        multiplayer,
        "gameState->GetMPGameState() == COUNTDOWN && IsArenaCampaignMatch()",
        "Arena countdown clock label gate",
    )
    require(
        multiplayer,
        'countdownLabel = common->GetLocalizedString( "#str_42079" );',
        "localized Entering Arena clock label",
    )
    require(multiplayer, "static char buff[64];", "localized countdown clock capacity")
    require(
        multiplayer,
        'IsArenaCampaignMatch() && ( state == WARMUP || state == COUNTDOWN )',
        "late stock-notice suppression gate",
    )
    require(
        multiplayer,
        '_mphud->SetStateString( "main_notice_text", "" );',
        "late stock-notice suppression",
    )
    require(
        multiplayer,
        "spectateText0.Clear();\n\t\tspectateText1.Clear();\n\t\tspectateText2.Clear();",
        "stock ready/countdown copy suppression",
    )

    # Freeze movement and attacks during entrance and result review, including
    # round-derived modes, without changing the normal multiplayer state rules.
    require(
        multiplayer,
        "return state == COUNTDOWN ||\n\t\t( state == GAMEREVIEW && ( arenaResultPending || arenaResultReported ) );",
        "reported-result player freeze lifetime",
    )
    require(
        multiplayer,
        "const bool victory = state == GAMEREVIEW &&\n\t\t( arenaResultPending || arenaResultReported );",
        "reported-result camera lifetime",
    )
    require(player, "gameLocal.mpGame.ArenaCampaignLocksPlayers()", "player movement freeze")
    # PM_FREEZE leaves MovePlayer before gravity, so a player still in the air
    # when a presentation begins would hang there in a falling pose. Gravity is
    # allowed to finish and the hold only applies once they are on the ground;
    # a neutral usercmd is what actually stops them acting in the meantime.
    require(
        player,
        "physicsObj.SetMovementType( physicsObj.OnGround() ? PM_FREEZE : PM_NORMAL );",
        "frozen movement mode settles to the floor first",
    )
    require(player, "physicsObj.SetPlayerInput( held, viewAngles );", "neutral input while locked")
    # The final tableau hands look control back so the player can orbit the
    # frozen victor, so the facing freeze has to yield to it - and the model must
    # not follow, or the victor would spin on the spot as the camera circled.
    require(
        player,
        "if ( gameLocal.isMultiplayer && gameLocal.mpGame.ArenaCampaignLocksPlayers() && !arenaFreeLook ) {\n"
        "\t\t// The Arena entrance and spawn-in freeze facing as well as movement.\n"
        "\t\t// Keep input deltas current so returning to gameplay cannot snap the view.\n"
        "\t\tif ( !vrTrackedAim ) {\n\t\t\tUpdateDeltaViewAngles( viewAngles );\n\t\t}\n"
        # The held view still logs its angles: the gun's turning sway reads this
        # ring by frame number and would otherwise replay the last turn.
        "\t\tloggedViewAngles[ gameLocal.framenum & (NUM_LOGGED_VIEW_ANGLES-1) ] = viewAngles;\n"
        "\t\treturn;",
        "frozen Arena facing without input snap or gun sway replay (a tracked VR aim keeps its delta)",
    )
    require(
        player,
        "if ( !arenaFreeLook ) {\n\t\t// orient the model towards the direction we're looking",
        "tableau look input does not turn the frozen body",
    )
    require(multiplayer_h, "ArenaCampaignAllowsFreeLook", "tableau free-look predicate")
    require(game_state, "return gameLocal.mpGame.ArenaCampaignLocksPlayers();", "base weapon lock")
    require(round_state, "if ( rvGameState::WeaponsLocked() )", "round-mode weapon lock chaining")
    require(round_modes_h, "class rvClanArenaGameState : public rvRoundGameState", "Clan Arena base state")
    require(round_modes_h, "class rvRedRoverGameState : public rvRoundGameState", "Red Rover base state")
    require(round_state, "void rvRoundGameState::Run( void ) {\n\tint winningTeam;\n\n\trvGameState::Run();", "round-mode global entrance path")
    require(round_state, "NewState( GAMEREVIEW );", "round-mode terminal review path")
    require(
        round_state,
        "if ( !rvGameState::NewState( newState ) ) {",
        "round-mode shared state transitions",
    )

    # The local camera is collision clipped, keeps the focused body visible and
    # cleanly falls through to stock cameras outside Arena presentation states.
    require(player, "BuildArenaCampaignPresentationView( this, renderView )", "local render-view hook")
    require(multiplayer, "TraceArenaCampaignCamera", "camera collision clipping helper")
    require(multiplayer, "ArenaCampaignCameraHasLineOfSight", "camera-to-focus visibility helper")
    require(multiplayer, "MASK_OPAQUE, focusPlayer", "opaque camera visibility trace")
    require(multiplayer, "entranceProbeAngles", "stable entrance sweep probes")
    require(multiplayer, "fallbackHeights", "tight-spawn height fallback")
    require(multiplayer, "for ( int direction = 0; direction < 8; direction++ )", "tight-spawn direction search")
    require(
        multiplayer,
        "horizontalClearance < ARENA_CAMERA_MIN_HORIZONTAL_CLEARANCE",
        "minimum establishing-shot clearance",
    )
    require(multiplayer, "arenaEntranceCameraForward = forward;", "latched entrance facing basis")
    require(multiplayer, "arenaEntranceCameraRadial = candidateRadial;", "latched fallback orbit anchor")
    require(multiplayer, "arenaEntranceCameraResolved = true;", "one-time entrance camera resolution")
    require(
        multiplayer,
        "cameraClearance < ARENA_CAMERA_MIN_USABLE_DISTANCE",
        "degenerate camera distance guard",
    )
    require(
        multiplayer,
        "continue into its normal valid first-person path",
        "unsafe camera stock-view fallback",
    )
    for field_name in (
        "arenaEntranceCameraResolved",
        "arenaEntranceCameraFallback",
        "arenaEntranceCameraValid",
        "arenaEntranceCameraForward",
        "arenaEntranceCameraRadial",
    ):
        require(multiplayer_h, field_name, "latched entrance camera state")
    require(multiplayer, "view->viewID = 0;", "focused player visibility")
    require(multiplayer, "arenaPresentationVictor = bestClient;", "victor selection")
    require(multiplayer, "arenaPresentationFocus = arenaPresentationVictor;", "victor camera focus")

    # Raven's active special-effect controller stores focus normalized against
    # parm 7's distance scale. Both render backends translate this pair at their
    # boundary; passing world-space focus directly would clamp it to 1.0.
    require(multiplayer, "SPECIAL_EFFECT_BLUR, 4, ARENA_DOF_EFFECT_RANGE", "DOF controller range")
    require(
        multiplayer,
        "focusDistance / ARENA_DOF_DISTANCE_SCALE",
        "DOF normalized focus",
    )
    require(multiplayer, "SPECIAL_EFFECT_BLUR, 6", "DOF strength")
    require(
        multiplayer,
        "SPECIAL_EFFECT_BLUR, 7, ARENA_DOF_DISTANCE_SCALE",
        "DOF distance scale",
    )
    if "SPECIAL_EFFECT_BLUR, 5, focusDistance" in multiplayer:
        raise AssertionError("Arena DOF must not pass unclamped world-space focus to the controller")
    require(multiplayer, "SetArenaCampaignDepthOfField( false );", "DOF cleanup")
    clear_block = multiplayer[
        multiplayer.index("void idMultiplayerGame::Clear()") :
        multiplayer.index("void idMultiplayerGame::ClearMap( void )")
    ]
    require_before(
        clear_block,
        "SetArenaCampaignDepthOfField( false );",
        "arenaPresentationBlurEnabled = false;",
        "Clear disables DOF before resetting ownership",
    )
    require(multiplayer, "void idMultiplayerGame::ClearMap( void ) {\n\t// MapClear", "map-exit presentation cleanup")

    # GUI names are an engine/content contract; keep these exact and source all
    # visible result labels from the language table.
    for state_name in (
        "arena_presentPhase",
        "arena_presentTitle",
        "arena_presentSubtitle",
        "arena_presentVictor",
        "arena_presentOutcome",
        "arena_presentScore",
    ):
        require(multiplayer, f'"{state_name}"', "Arena HUD state contract")
    for event_name in (
        "arenaCampaignEntrance",
        "arenaCampaignVictory",
        "arenaCampaignPresentationClear",
    ):
        require(multiplayer, f'HandleNamedEvent( "{event_name}" )', "Arena HUD event contract")
    for localized_result in ("#str_42030", "#str_42031", "#str_42077"):
        require(multiplayer, f'GetLocalizedString( "{localized_result}" )', "localized result label")

    # Results remain on screen long enough to read and then use the existing,
    # one-shot framework handoff instead of opening the stock MP summary.
    require(multiplayer, "ARENA_RESULT_REVIEW_MSEC = 6000", "victory presentation duration")
    require(multiplayer, "ShowArenaCampaignVictoryPresentation();", "Arena victory GUI")
    require(multiplayer, 'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d"', "framework result handoff")
    require(multiplayer, "arenaResultReported = true;", "one-shot result guard")
    success_handoff = multiplayer[multiplayer.index("arenaResultReported = true;") :]
    require_before(
        success_handoff,
        "arenaResultReported = true;",
        'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d"',
        "reported-result handoff ordering",
    )
    if "ClearArenaCampaignPresentation();" in success_handoff.split(
        'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d"', 1
    )[0]:
        raise AssertionError("Successful Arena handoff clears the wipe-source presentation")

    print("arena_campaign_presentation_contract: ok")


if __name__ == "__main__":
    main()
