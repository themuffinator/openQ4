#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static security contract for the live Match Control game adapter."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "game" / "MultiplayerGame.cpp"
HEADER = ROOT / "src" / "game" / "MultiplayerGame.h"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def main() -> None:
    source = read(SOURCE)
    header = read(HEADER)
    combined = header + "\n" + source
    adapter_start = source.index("bool idMultiplayerGame::AcceptClientMatchView")
    handler_start = source.index("bool idMultiplayerGame::HandleMatchControlCommand")
    adapter_end = source.find("\n/*", handler_start)
    if adapter_end < 0:
        raise AssertionError("could not bound live Match Control adapter")
    adapter = source[adapter_start:adapter_end]

    # Prepared GUI requests are bound to the accepted view used to select their
    # typed identifiers.  Transport may bind an entirely empty internal request,
    # but must reject partial bindings and must never silently rebase a prepared
    # request onto a newer view.
    for token in (
        "const bool unbound = request.sessionId == 0 && request.requestId == 0",
        "const bool fullyBound = request.sessionId != 0 && request.requestId != 0",
        "request.requestId != expectedRequestId",
        "request.expectedSessionRevision !=",
        "clientMatchView.publicState.sessionRevision",
        "request.expectedControlRevision !=",
        "clientMatchView.publicState.controlRevision",
        "request.actorSlot != clientMatchView.publicState.recipient.slot",
        "request.actorBindingGeneration !=",
        "nextClientMatchRequestId = request.requestId;",
    ):
        require(source, token, "snapshot-bound request submission")

    # The rest of these pins are filled by the live UI glue.  Keeping them here
    # makes accidental removal of recipient-view ingestion, closed command
    # dispatch, batched projection, or secret wiping a release-breaking test.
    for token in (
        '"mp/match/MatchControlModel.h"',
        '"mp/match/MatchControlProjection.h"',
        "AcceptClientMatchView",
        "clientMatchControlModel.IngestAcceptedView",
        "bindingChanged && accepted == MP_MATCH_VIEW_ACCEPT_NO_CHANGE",
        "ClearClientMatchControlConnectionState( true )",
        "MPMatchControlClearManagedContext( *scoreBoard )",
        "MPMatchControlClearManagedContext( *localPlayer->mphud )",
        "MPMatchControlClearMenu( *mainGui, true )",
        "clientMatchMenuProjectedViewRevision",
        "clientPendingMatchConfirmationValid",
        "ClearClientPendingMatchConfirmation( true )",
        'strcmp( token, "confirm" ) == 0',
        'strcmp( token, "cancel_confirm" ) == 0',
        'strcmp( token, "action_side_a" ) == 0',
        'strcmp( token, "action_side_b" ) == 0',
        "clientMatchControlModel.CanChooseActionSide( side )",
        "clientMatchControlModel.SetActionSideChoice(",
        '"arm_roster_remove"',
        "clientPendingMatchConfirmation = request",
        "operationDescriptor->confirmationLocalizationId != MP_MATCH_LOCALIZATION_NONE",
        "gameLocal.isListenServer && clientNum == gameLocal.localClientNum",
        "StoreClientMatchOperationResult( result )",
        "clientMatchView.publicState.viewRevision == matchViewRevision",
        "MPMatchControlProjectMenu",
        "MPMatchControlProjectManagedContext",
        'idStr::Icmp( cmd, "matchControl" )',
        "MPMatchControlCommandFromToken",
        "mainGui->GetStateString(",
        '"match_referee_credential"',
        'SetStateString( "match_referee_credential", "" )',
        "MPRefereeAuthSecureZero",
        "ClearPendingRefereePassword",
        "ClearMatchOperationTransportSlot( clientNum )",
        "lastMatchRequestResult[ clientNum ].Clear()",
        "matchRefereeAuthentication.InvalidateSlot( clientNum )",
    ):
        require(combined, token, "live Match Control adapter")

    receive_start = source.index(
        "void idMultiplayerGame::ServerReceiveMatchOperation"
    )
    receive_end = source.index(
        "void idMultiplayerGame::ClientReceiveMatchOperationResult", receive_start
    )
    receive = source[receive_start:receive_end]
    binding_gate = receive.index(
        "matchSession.ResolveSlotBinding( clientNum,\n"
        "\t\t\trequest.actorBindingGeneration, ingressActor )"
    )
    replay_lookup = receive.index(
        "request.requestId <= lastMatchRequestId[ clientNum ]"
    )
    if binding_gate >= replay_lookup:
        raise AssertionError(
            "connection binding must be authenticated before consulting replay cache"
        )

    accept_start = source.index("bool idMultiplayerGame::AcceptClientMatchView")
    clear_connection = source.index(
        "void idMultiplayerGame::ClearClientMatchControlConnectionState",
        accept_start,
    )
    accept = source[accept_start:clear_connection]
    for token in (
        "clientPendingMatchConfirmation.expectedSessionRevision !=",
        "clientPendingMatchConfirmation.expectedControlRevision !=",
        "clientPendingMatchConfirmation.actorBindingGeneration !=",
        "ClearClientPendingMatchConfirmation( true )",
    ):
        require(accept, token, "prepared confirmation invalidation")

    connect_start = source.index("void idMultiplayerGame::ServerClientConnect")
    spawn_start = source.index("void idMultiplayerGame::SpawnPlayer", connect_start)
    require(
        source[connect_start:spawn_start],
        "ClearMatchOperationTransportSlot( clientNum )",
        "connection-slot admission reset",
    )
    disconnect_start = source.index("void idMultiplayerGame::DisconnectClient")
    require(
        source[disconnect_start:],
        "ClearMatchOperationTransportSlot( clientNum )",
        "connection-slot teardown reset",
    )

    update_start = source.index("void idMultiplayerGame::UpdateMainGui")
    update_end = source.index("idUserInterface* idMultiplayerGame::StartMenu", update_start)
    update = source[update_start:update_end]
    require(
        update,
        "clientMatchMenuProjectedViewRevision !=\n\t\t\t\tclientMatchView.publicState.viewRevision",
        "revision-keyed menu projection cache",
    )

    for forbidden in (
        "MP_MATCH_LOCALIZATION_OPERATION_BASE +",
        "MP_MATCH_LOCALIZATION_REASON_BASE +",
        "#str_%d",
        "atoi( mainGui->GetStateString",
        "sscanf( mainGui->GetStateString",
    ):
        if forbidden in adapter:
            raise AssertionError(
                f"live Match Control adapter contains unsafe presentation path {forbidden!r}"
            )

    print("mp_match_control_live_adapter_contract: PASS")


if __name__ == "__main__":
    main()
