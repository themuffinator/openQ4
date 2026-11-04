#!/usr/bin/env python3
"""Guards the multiplayer bot's navigation contract.

openQ4 multiplayer bots occupy real client slots and are driven by user commands
the game writes into the engine's own array.  That only works while a handful of
non-obvious agreements hold, none of which the compiler can check:

  * The engine hands back the client slot it allocated for a bot.  It used to
    return a bare 1, which is indistinguishable from client 1.
  * Bots are re-begun after a map change.  A remote client announces itself with
    CLIENT_RELIABLE_MESSAGE_INGAME; a bot has no remote end to do that.
  * A bot's user info is restored on every update.  The engine broadcasts a bot
    with nothing but a name, and an idPlayer without ui_autoJoin sits in the
    join menu as a spectator forever.
  * Bots think before any entity does, so their commands are in place when the
    players read them.
  * Navigation is generated from the collision world with the player's own
    bounding box and step height, which is what makes it work on maps that have
    no .aas file - which is every stock Quake 4 multiplayer map.
  * A non-walk link retains both its entry and exit and is completed from
    grounded traversal state, not merely because its adjacent grid corner is
    within the ordinary steering radius.
  * Item and roam goals are reachable in the requested direction; weak
    connected areas cannot make an upward-only ledge look reachable.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    a = haystack.find(first)
    b = haystack.find(second)
    if a == -1:
        raise AssertionError(f"Missing {first!r} in {context}")
    if b == -1:
        raise AssertionError(f"Missing {second!r} in {context}")
    if a > b:
        raise AssertionError(f"{first!r} must appear before {second!r} in {context}")


def cvar_signature(source: str, name: str, context: str) -> tuple[str, set[str]]:
    match = re.search(
        rf'idCVar\s+\w+\(\s*"{re.escape(name)}"\s*,\s*"([^"]*)"\s*,\s*([^,]+),',
        source,
        re.IGNORECASE,
    )
    if match is None:
        raise AssertionError(f"Missing CVar declaration for {name!r} in {context}")
    return match.group(1), {flag.strip() for flag in match.group(2).split("|")}


def validate_engine() -> None:
    source = read(ROOT / "src" / "framework" / "async" / "AsyncServer.cpp")

    # The allocator has to report which slot it took.
    alloc = source[source.index("int idAsyncServer::AllocOpenClientSlotForAI") :]
    alloc = alloc[: alloc.index("\n}\n") + 3]
    require(alloc, "return botClientId;", "idAsyncServer::AllocOpenClientSlotForAI")
    if re.search(r"^\treturn 1;$", alloc, re.MULTILINE):
        raise AssertionError(
            "AllocOpenClientSlotForAI returns a bare 1 again; the game cannot tell which slot it got"
        )

    # Bots have to be put back into the game after a map change.
    change = source[source.index("void idAsyncServer::ExecuteMapChange") :]
    change = change[: change.index("\nint idAsyncServer::GetPort")]
    require(change, "botClient[MAX_ASYNC_CLIENTS]", "idAsyncServer::ExecuteMapChange")
    require(change, "NA_BOT", "idAsyncServer::ExecuteMapChange")
    require(change, "game->ServerClientBegin( i, true,", "idAsyncServer::ExecuteMapChange")
    # The name is captured before InitClient wipes the user info that holds it.
    require_order(
        change,
        "botClientName[i] = sessLocal.mapSpawnData.userInfo[i].GetString",
        "InitClient( i, clients[i].clientId",
        "idAsyncServer::ExecuteMapChange",
    )

    # And nothing may try to talk to a bot over the network.
    for guard in (
        "void idAsyncServer::SendReliableMessage",
        "void idAsyncServer::CheckClientTimeouts",
    ):
        block = source[source.index(guard) :][:2000]
        require(block, "NA_BOT", guard)


def validate_game_wiring() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        print(f"mp_bot_navigation: skipped game checks (no GameLibs checkout at {GAME_LIBS_ROOT})")
        return

    game_local = read(mp / "Game_local.cpp")

    # Bots fill their user command slots before anything reads them.
    require_order(
        game_local,
        "usercmds = clientCmds;",
        "botManager.Think();",
        "idGameLocal::RunFrame",
    )
    run_frame = game_local[game_local.index("gameReturn_t idGameLocal::RunFrame") :][:6000]
    require_order(run_frame, "botManager.Think();", "SetupPlayerPVS();", "idGameLocal::RunFrame")
    player = read(mp / "Player.cpp")
    player_think = player[player.index("void idPlayer::Think( void )"):player.index("void idPlayer::ThinkMatchPaused(")]
    require_order(player_think, "usercmd = gameLocal.usercmds[", "botManager.RefreshKnockbackMove( this, usercmd );",
                  "player refreshes hits after copying the bot command")
    require_order(player_think, "botManager.RefreshKnockbackMove( this, usercmd );", "Move();",
                  "same-frame knock recovery precedes physics")
    require_order(game_local, "CaptureMPLagCompensationFrame();", "botManager.FinishFrameProfile();",
                  "bot profile includes movement refreshes after player physics")

    # The slot is claimed before the player entity is created, because
    # idPlayer::Spawn reads the user info.
    spawn = game_local[game_local.index("void idGameLocal::SpawnPlayer") :][:3000]
    require_order(
        spawn,
        "botManager.OnSpawnPlayer( clientNum, isBot, botName );",
        "SpawnEntityDef( args, &ent )",
        "idGameLocal::SpawnPlayer",
    )

    # Every user info update restores the bot's identity.
    set_user_info = game_local[game_local.index("const idDict* idGameLocal::SetUserInfo") :][:3000]
    require(set_user_info, "botManager.IsBot( clientNum )", "idGameLocal::SetUserInfo")
    require(set_user_info, "botManager.FillUserInfo(", "idGameLocal::SetUserInfo")

    require(game_local, "botManager.OnMapShutdown();", "idGameLocal::MapShutdown")
    require(
        read(mp / "Game_network.cpp"),
        "botManager.OnClientDisconnect( clientNum );",
        "idGameLocal::ServerClientDisconnect",
    )

    # The manager's own lifecycle. rvBotManager::Shutdown is what hands every
    # bot's character back, so it has to run before the roster is freed or each
    # live bot is left pointing at released memory.
    require(game_local, "botManager.Init();", "idGameLocal::Init")
    require(game_local, "botManager.Shutdown();", "idGameLocal::Shutdown")
    require_order(
        game_local,
        "botManager.Shutdown();",
        "botCharacterManager.Shutdown();",
        "idGameLocal::Shutdown character release ordering",
    )

    multiplayer_game = read(mp / "MultiplayerGame.cpp")

    # Kill and death reactions - chat, kill streaks, grudges - hang off this one
    # call, and it went unwired once already. Scores are committed by the time
    # statManager->Kill returns, so a bot reacting here sees the post-frag board.
    require(
        multiplayer_game,
        "botManager.OnPlayerDeath( dead, killer, methodOfDeath );",
        "idMultiplayerGame::PlayerDeath",
    )
    require_order(
        multiplayer_game,
        "statManager->Kill( dead, killer, methodOfDeath );",
        "botManager.OnPlayerDeath( dead, killer, methodOfDeath );",
        "bot death reaction reads a committed scoreboard",
    )

    # idGameLocal::RunFrame keeps advancing gameLocal.time through a competitive
    # pause while it skips rvBotManager::Think, so every absolute bot deadline
    # has to be carried forward with the clock exactly as the players' are.
    require(
        multiplayer_game,
        "botManager.ShiftMatchTime( deltaMsec );",
        "idMultiplayerGame::RebaseCompetitivePauseFrame",
    )

    bot = read(mp / "bots" / "Bot.cpp")
    require(bot, "profileFrameTicks += idLib::sys->GetClockTicks() - start;", "same-frame recovery CPU is measured")

    # Without ui_autoJoin a spawning idPlayer parks itself in the join menu.
    for key in ('"ui_autoJoin"', '"ui_spectate"', '"ui_ready"', '"ui_name"'):
        require(bot, key, "rvBot::FillUserInfo")

    # Full movement speed needs the run button; idPlayer::AdjustSpeed otherwise
    # drops the bot to pm_walkspeed.
    require(bot, "cmd.buttons |= BUTTON_RUN;", "rvBot::ApplyMove")

    # The player rebuilds its view from the command plus its delta angles.
    require(bot, "ANGLE2SHORT( aimAngles[i] - self->GetDeltaViewAngles()[i] )", "rvBot::UpdateAim")

    # An unreachable moving goal must not cost a failed search every frame, and
    # rule objectives must be considered only after a committed traversal has
    # been allowed to finish.
    # Sliced to the function's closing brace rather than a fixed character
    # count, so the pin keeps covering the whole of UpdateGoal as it grows.
    goal = bot[bot.index("void rvBot::UpdateGoal") :]
    goal = goal[: goal.index("\n}\n") + 3]
    require(goal, "gameLocal.time - enemyPathTime > BOT_REPATH_COMBAT_MSEC", "rvBot::UpdateGoal")
    require(goal, "gameLocal.time < nextGoalSelectTime", "rvBot::UpdateGoal")
    require_order(
        goal,
        "traversalCorner == pathCorner",
        "BotFindObjective( self, objective )",
        "rvBot::UpdateGoal traversal commitment",
    )
    require(goal, "BOT_GOAL_SWITCH_MARGIN", "rvBot::UpdateGoal hysteresis")
    require(goal, "criticalHealth", "rvBot::UpdateGoal survival priority")
    require(goal, "BOTGOAL_OBJECTIVE", "rvBot::UpdateGoal mode objective")

    # Goal selection must return the successful directed route along with the
    # entity.  A weak connected-area match is only a rejection shortcut:
    # one-way ledges are not proof that the item can be approached from below.
    item_goal = bot[bot.index("idEntity *rvBot::PickItemGoal") :]
    item_goal = item_goal[: item_goal.index("\n}\n") + 3]
    require(item_goal, "navMesh.FindPath(", "rvBot::PickItemGoal")
    require(item_goal, "candidates[i].item->GetPhysics()->GetOrigin(), goalPath, false, BotMaxSafeFallDelta( self ) )", "rvBot::PickItemGoal")

    # A special link is an action.  q4dm1's crate links are only one 24-unit
    # cell apart but rise 32-48 units; the ordinary 28x72 proximity test used
    # to consume each landing before UpdateMovement could press jump.
    advance = bot[bot.index("bool rvBot::AdvancePath") :]
    advance = advance[: advance.index("\n}\n") + 3]
    require(advance, "actionEntry ? BOT_TRAVEL_ENTRY_REACH", "rvBot::AdvancePath")
    require(advance, "traversalStarted && self->pfl.onGround", "rvBot::AdvancePath")
    require(advance, "BotReachedTravelEnd(", "rvBot::AdvancePath")
    for travel in (
        "NAVTRAVEL_JUMP",
        "NAVTRAVEL_DROP",
        "NAVTRAVEL_JUMPPAD",
        "NAVTRAVEL_TELEPORT",
    ):
        require(advance, travel, "rvBot::AdvancePath")

    travel_end = bot[bot.index("static bool BotReachedTravelEnd") :]
    travel_end = travel_end[: travel_end.index("\n}\n") + 3]
    require(
        travel_end,
        "Min( travelDistance * 0.5f",
        "BotReachedTravelEnd destination-side progress",
    )
    require(
        travel_end,
        "idMath::Fabs( end.z - start.z ) <= zTolerance",
        "BotReachedTravelEnd same-floor progress guard",
    )
    require(advance, "BotTouchesTravelVolume(", "rvBot::AdvancePath volume entry")
    require(advance, "self->IsInTeleport()", "rvBot::AdvancePath teleporter activation")

    movement = bot[bot.index("void rvBot::UpdateMovement") :]
    movement = movement[: movement.index("\n}\n") + 3]
    require(movement, "corner.travelType != NAVTRAVEL_WALK", "rvBot::UpdateMovement")
    require(movement, "!committedTravel && foe", "rvBot::UpdateMovement")
    require(movement, "!committedTravel && gameLocal.time < unstickUntil", "rvBot::UpdateMovement")
    require(movement, "const bool routeProgress", "rvBot::UpdateMovement route progress")
    require(movement, "stuckPathCorner", "rvBot::UpdateMovement corner progress")
    require(movement, "stuckChecks >= 2", "rvBot::UpdateMovement")
    require(movement, "const bool hasMoveDir", "rvBot::UpdateMovement vertical action")
    require(movement, "transportInProgress", "rvBot::UpdateMovement authored transport progress")
    require(movement, "BOT_TRAVEL_TIMEOUT_MSEC", "rvBot::UpdateMovement traversal timeout")
    require(movement, "BotMoveDirectionSafe", "rvBot::UpdateMovement safe combat steering")
    require_order(movement, "AlignJumpDropApproach( self, cmd )", "bool routing = AdvancePath( self );",
                  "jump/drop approach brakes before consuming the takeoff")
    require_order(movement, "HandleJumpTakeoff( self, cmd )", "corner.travelType == NAVTRAVEL_JUMP || ( climbHop && steeringIntoVolume )",
                  "the exact-state jump gate owns grounded launch before generic press handling")
    require_order(movement, "GuardLandingContinuation( self, cmd )", "bool routing = AdvancePath( self );",
                  "actual landing continuation is checked before the flight releases control")
    require(movement, "gameLocal.time - noRouteSince", "rvBot::UpdateMovement no-route recovery")
    # A bot knocked or carried onto another floor replaces its route at once
    # instead of pressing toward a corner overhead until the stuck detector
    # fires, and the goal is decided again from where it now stands.
    require_order(
        movement,
        "bool routing = AdvancePath( self );",
        "routing && OffRoute( self, offHeight )",
        "rvBot::UpdateMovement off-route check follows route advance",
    )
    require(movement, "nextGoalSelectTime = Min( nextGoalSelectTime, gameLocal.time );",
            "rvBot::UpdateMovement off-route goal re-decision")
    # A drop landing under its own ledge lip must not collapse the steering.
    require(movement, "moveDir * across < BOT_CORNER_REACH", "rvBot::UpdateMovement drop push-through")
    # A pad or teleporter entry steers into the volume itself.
    require(movement, "bounds.GetCenter() - origin", "rvBot::UpdateMovement volume entry steering")
    # Grounded movement never walks off into the void or a kill volume.
    require(movement, "BotBrakeAtEdge( self, edgeMove )", "rvBot::UpdateMovement void-edge guard")
    require(movement, "corner.travelType != NAVTRAVEL_DROP || knockSliding", "only a settled planned drop can override the edge brake")
    require(movement, "!navMesh.DropTakeoffClear(", "live drop proof before overriding the edge brake")
    require_order(movement, "BotBrakeAtEdge( self, brakeDir )", "bool waitingForItem = false;",
                  "rvBot::UpdateMovement brakes unrouted momentum before idle/item holds")
    require_order(movement, "BotBrakeAtEdge( self, edgeMove )", "ApplyMove( moveDir, cmd );",
                  "rvBot::UpdateMovement guards before it moves")
    # A knock's slide toward an edge is braked even on an action's approach.
    require(movement, "( !leavesOnPurpose || knockSliding )", "rvBot::UpdateMovement knocked edge guard")
    # A held pad approach guards its momentum at the edge, input or none.
    require(movement, "if ( holdForPad && self->pfl.onGround ) {", "rvBot::UpdateMovement held approach edge guard")
    require(movement, "( hasMoveDir || transportBrake || heldEdgeBrake )", "rvBot::UpdateMovement applies the held edge brake")
    # A shotgun's pellets are one throw: a frame's kicks are summed.
    require(bot, "knockbackFrameKick += kick;", "rvBot::OnKnockback pellet sum")
    # Momentum never carries a grounded bot into a pad or teleporter its route
    # did not choose, on a route or off one (q4ctf5's unplanned pad entries).
    require(movement, "BotBrakeForTransport( self, moveDir, BotPlannedTransport( path, pathCorner ) )",
            "rvBot::UpdateMovement unplanned transport guard")
    require(movement, "BotBrakeForTransport( self, brakeDir, -1 )",
            "rvBot::UpdateMovement unrouted transport guard")
    require_order(movement, "BotBrakeForTransport( self, moveDir, BotPlannedTransport( path, pathCorner ) )",
                  "ApplyMove( moveDir, cmd );", "rvBot::UpdateMovement guards transports before it moves")
    require_order(movement, "BotBrakeForTransport( self, brakeDir, -1 )", "bool waitingForItem = false;",
                  "rvBot::UpdateMovement brakes unrouted momentum short of a pad before idle/item holds")
    # A bot waits short of a pad while someone flies its corridor the other way.
    require(movement, "HoldForPad( self, moveDir )", "rvBot::UpdateMovement pad hold owns movement")
    require(movement, "hasMoveDir && !holdForPad && self->pfl.onGround",
            "rvBot::UpdateMovement cannot climb-hop into a blocked pad")
    require(movement, "flat * moveDir < 0.0f && flat.LengthSqr() > Square( BOT_PAD_LAUNCH_SPEED * 1.25f )",
            "rvBot::UpdateMovement coasts when thrown away from its landing")
    require(advance, "BotTouchesTravelVolume( self, path[pathCorner + 1] )", "rvBot::AdvancePath chained pad takes over")
    require(movement, "BotRidesPadArc( origin, velocity, corner, gravity )", "rvBot::UpdateMovement rides a natural pad arc")
    # In a pad's flight or a drop's fall the bot steers like PadLanding predicts.
    require(movement, "NavAirSteer( origin, velocity, corner.origin, gravity, wish )",
            "rvBot::UpdateMovement arrival air steering")
    # Pressing along the route toward a corner above without closing in hops,
    # on any leg including the committed approach to an action.
    require(movement, "steeringIntoVolume || corner.origin.z - origin.z > BOT_CLIMB_RISE",
            "rvBot::UpdateMovement climb hop rise or volume entry")
    require(movement, "climbHop = gameLocal.time - climbCheckTime >= BOT_CLIMB_HOP_MSEC",
            "rvBot::UpdateMovement climb hop window")
    require(movement, "if ( corner.travelType == NAVTRAVEL_JUMP || ( climbHop && steeringIntoVolume ) )",
            "rvBot::UpdateMovement preserves the planned volume-entry hop")
    require(movement, "else if ( climbHop || ( !committedTravel && ( gameLocal.time < unstickUntil || wantDodgeJump ) ) )",
            "rvBot::UpdateMovement proves ordinary climbs even on a committed approach")
    off_route = bot[bot.index("bool rvBot::OffRoute") :]
    off_route = off_route[: off_route.index("\n}\n") + 3]
    require(off_route, "self->pfl.onGround", "rvBot::OffRoute grounded only")
    require(off_route, "path.RouteStart()", "rvBot::OffRoute first-leg start")
    require(off_route, "BOT_OFF_ROUTE_LATERAL", "rvBot::OffRoute carried far from the leg")
    # A volume entry only suspends the check beside the entry itself.
    require(off_route, "if ( traversalEntered && traversalCorner == pathCorner + 1 ) {", "rvBot::OffRoute stale volume entry")
    require(off_route, "-BOT_OFF_ROUTE_BELOW", "rvBot::OffRoute fallen below the leg")
    # A drop or jump that came down below its takeoff without completing landed
    # somewhere else, and is replanned from there.
    require(off_route, "traversalStartOrigin.z - pm_stepsize.GetFloat()", "rvBot::OffRoute action landed elsewhere")
    require(off_route, "corner.travelType == NAVTRAVEL_WALK && offHeight > BOT_OFF_ROUTE_ABOVE",
            "rvBot::OffRoute above only on walks")
    # Nearer than those limits, a bot displaced from a walk leg by more than a
    # corner's reach or a step of height asks the navmesh whether the walk to
    # its corner still exists, a few times a second: combat footwork off the
    # side of q4ctf1's ramp used to press at the corner until the stuck
    # detector fired, and not at all while the fight kept the detector quiet.
    require(off_route, "navMesh.WalkLegAccepted( origin, corner.origin, NULL )",
            "rvBot::OffRoute displaced leg proof")
    require(off_route, "aside.LengthSqr() > Square( BOT_CORNER_REACH ) || idMath::Fabs( offHeight ) > pm_stepsize.GetFloat()",
            "rvBot::OffRoute displacement that needs the proof")
    require(off_route, "gameLocal.time - offRouteProofTime >= BOT_OFF_ROUTE_PROOF_MSEC",
            "rvBot::OffRoute throttled leg proof")
    require(advance, "( past - direction * ahead ).LengthSqr() <= Square( BOT_CORNER_REACH * 2.0f )",
            "rvBot::AdvancePath long-drop and level-jump overshoot")
    # Only real launch speed takes a pad, and a pad flight ends on landing.
    require(advance, "traversalEntered && padLaunched", "rvBot::AdvancePath pad launch evidence")
    require(advance, "BotPadFlightComplete( traversalStarted", "rvBot::AdvancePath pad flight ends on landing")
    # A jump that fell short goes back to its run-up and tries again.
    require(advance, "jumpRetries < BOT_JUMP_RETRIES", "rvBot::AdvancePath failed jump retry")
    # Only a jump that came down on the takeoff's side of its link fell short;
    # jumps and drops alike complete ahead of their landing on its floor.
    require(advance, "travelled * direction < linkLength * 0.5f", "rvBot::AdvancePath retry only short of the link")
    require_order(advance, "const float linkLength = direction.Normalize();", "jumpRetries++;",
                  "rvBot::AdvancePath overshoot completion precedes the retry")
    require(advance, "stuckPathCorner		= pathCorner;", "rvBot::AdvancePath retry resets the stuck window")
    safe = bot[bot.index("static bool BotMoveDirectionSafe") :]
    safe = safe[: safe.index("\n}\n") + 3]
    require(safe, "navMesh.TransportAt( swept ) >= 0", "BotMoveDirectionSafe refuses jump pads and teleporters")
    if "climb > pm_stepsize.GetFloat()" in movement:
        raise AssertionError(
            "UpdateMovement guesses jumps from corner height again; jump input must follow "
            "the route's explicit NAVTRAVEL_JUMP action"
        )

    jump = bot[bot.index("void rvBot::PressJump") :]
    jump = jump[: jump.index("\n}\n") + 3]
    require(jump, "self->pfl.onGround", "rvBot::PressJump")
    require(jump, "gameLocal.framenum + clientNum", "rvBot::PressJump")

    # Newly observed pad impulses must own movement before a goal refresh can
    # restore a stale walking route. Voluntary hops use the touchdown proof.
    think = bot[bot.index("void rvBot::Think(") :]
    require_order(think, "AdoptPadLaunch( self );", "UpdateGoal( self );",
                  "rvBot::Think adopts a real pad launch before goal selection")
    require(movement, "TryDodgeJump( self, moveDir, cmd );", "rvBot::UpdateMovement proves discretionary jumps")
    require(bot, "navMesh.DodgeJumpClear(", "rvBot::TryDodgeJump uses the production touchdown proof")
    require_order(movement, "HoldDodgeJump( self, cmd )", "GuardLandingHealth( self, cmd )",
                  "accepted hop input precedes every movement branch")
    think = bot[bot.index("void rvBot::Think("):bot.index("void rvBot::Think(") + 9000]
    require_order(think, "AdoptPadLaunch( self )", "UpdateMovement( self, cmd )",
                  "observed pad launch supersedes a discretionary hop")
    spawn = bot[bot.index("void rvBot::OnSpawn("):bot.index("void rvBot::OnSpawn(") + 5000]
    require(spawn, "ResetDodgeJump();", "respawn clears the voluntary flight")
    traversal_reset = bot[bot.index("void rvBot::ResetTraversal("):bot.index("void rvBot::ResetTraversal(") + 600]
    if "ResetDodgeJump" in traversal_reset:
        raise AssertionError("route replacement must not cancel an accepted hop")

    # A foe falling into a kill volume is not chased.
    require(bot, "!navMesh.IsHazardous( enemyLastSeenOrigin, true )", "rvBot::UpdateGoal no chase into the void")

    # Bots outlive a map change, the navmesh does not.
    require(bot, "!navMesh.IsValid() && NumBots() > 0", "rvBotManager::Think")

    # A kicked bot's slot has to come back: it never sends a packet, so the
    # engine's zombie timeout can never reap it on its own.
    server = read(ROOT / "src" / "framework" / "async" / "AsyncServer.cpp")
    timeouts = server[server.index("void idAsyncServer::CheckClientTimeouts") :][:2000]
    require(timeouts, "clientState == SCS_ZOMBIE", "idAsyncServer::CheckClientTimeouts bot reaping")
    require(timeouts, "clientState = SCS_FREE", "idAsyncServer::CheckClientTimeouts bot reaping")


def validate_navmesh() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    nav = read(mp / "bots" / "NavMesh.cpp")

    # The agent is the player, taken from the cvars the server is running.
    agent = nav[nav.index("const idBounds &rvNavMesh::GetAgentBounds") :][:800]
    require(agent, "pm_bboxwidth.GetFloat()", "rvNavMesh::GetAgentBounds")
    require(agent, "pm_normalheight.GetFloat()", "rvNavMesh::GetAgentBounds")

    # Generation must not see other players as walls.
    mask = re.search(r"#define\s+NAV_CLIP_MASK\s+\(([^)]*)\)", nav)
    if mask is None:
        raise AssertionError("NAV_CLIP_MASK is not defined in NavMesh.cpp")
    if "CONTENTS_BODY" in mask.group(1):
        raise AssertionError(
            "NAV_CLIP_MASK includes CONTENTS_BODY; a player standing in a doorway would "
            "erase the doorway from the navmesh"
        )
    for flag in ("CONTENTS_SOLID", "CONTENTS_PLAYERCLIP"):
        require(mask.group(1), flag, "NAV_CLIP_MASK")

    # Traversal mirrors what idPhysics_Player will actually allow.
    step = nav[nav.index("bool rvNavMesh::TryStep") :][:3000]
    require(step, "pm_stepsize.GetFloat()", "rvNavMesh::TryStep")
    require(step, "pm_jumpheight.GetFloat()", "rvNavMesh::TryStep")
    # A jump lands only where floor reaches the middle of the box, not on a
    # node that merely clips a platform's edge.
    require(step, "!NavLandingSupported( toFloor )", "rvNavMesh::TryStep supported jump landing")
    require(nav, "NavLandingSupported( nodes[landing].origin )", "rvNavMesh::AddGapJumpLinks supported landing")
    # A trench no deeper than NAV_GAP_MAX_FALL is a floor in the landing band.
    # The scan links the drop into it and then keeps looking for the far rim
    # at the takeoff's level instead of stopping at the trench floor.
    gap = nav[nav.index("void rvNavMesh::AddGapJumpLinks") :]
    gap = gap[: gap.index("\n}\n") + 3]
    for token in (
        "bool seekingRim = false;",
        "const bool belowTakeoff = nodes[landing].origin.z < origin.z - stepSize;",
        "if ( seekingRim && belowTakeoff ) {",
        "if ( belowTakeoff && !seekingRim ) {",
        "GapJumpClear( origin, nodes[landing].origin, runSpeed, jumpSpeed, gravity )",
        "trenchRimLinks = rimAdded;",
    ):
        require(gap, token, "rvNavMesh::AddGapJumpLinks trench rim")
    require(nav, "%d gap jumps (%d over trenches)", "navmesh build summary")

    # A listen server reads no snapshots, so its RunFrame clears the debug
    # lines that bot_debugNav draws or the renderer's line pool fills up.
    game_local = (GAME_LIBS_ROOT / "src/mpgame/Game_local.cpp").read_text(encoding="utf-8")
    run_frame = game_local[game_local.index("gameReturn_t idGameLocal::RunFrame(") :]
    run_frame = run_frame[: run_frame.index("\tdo {")]
    require(run_frame, "gameRenderWorld->DebugClear( time );", "MP server frame debug line clear")

    # Closed proximity doors are usable traversal, not permanent holes in a
    # graph that happens to be generated before the first player approaches.
    # Every probe sweeps the world and solid entities but never a body: MP
    # players carry CONTENTS_SOLID for their combat box, and a bot's own box
    # overlaps every sweep that starts where it stands.
    door_trace = nav[nav.index("static bool NavTraceTranslation") :]
    door_trace = door_trace[: door_trace.index("\n}\n") + 3]
    require(door_trace, "CONTENTS_BODY | CONTENTS_CORPSE", "NavTraceTranslation ignores bodies")
    require(door_trace, "idActor::GetClassType()", "NavTraceTranslation ignores actors")
    require(door_trace, "GetWorldCollisionModel()", "NavTraceTranslation world sweep")
    require(door_trace, "NavDoorIsWalkActivated", "NavTraceTranslation usable door filter")
    require(nav, "return NavTraceTranslation( trace, start, end, navAgentTrace, navAgentBounds );",
            "NavAgentTranslation sweeps the agent box")
    door_filter = nav[nav.index("static bool NavDoorIsWalkActivated") :]
    door_filter = door_filter[: door_filter.index("\n}\n") + 3]
    require(door_filter, "idDoor::GetClassType()", "NavDoorIsWalkActivated")
    require(door_filter, "IsLocked()", "NavDoorIsWalkActivated locked door")
    require(door_filter, "IsNoTouch()", "NavDoorIsWalkActivated scripted door")
    require(door_filter, 'GetString( "requires"', "NavDoorIsWalkActivated required door")
    require(step, "NavAgentTranslation", "rvNavMesh::TryStep door traversal")

    # Off-mesh links are what keep a Quake 4 multiplayer map one component.
    links = nav[nav.index("void rvNavMesh::AddOffMeshLinks") :]
    links = links[: links.index("\n}\n") + 3]
    require(links, "rvJumpPad::GetClassType()", "rvNavMesh::AddOffMeshLinks")
    require(links, "idTrigger_Multi::GetClassType()", "rvNavMesh::AddOffMeshLinks")
    require(links, "idPlayerStart::GetClassType()", "rvNavMesh::AddOffMeshLinks")
    # A pad's target is its arc's apex: link where the arc really lands.
    require(links, "PadLandings( ent, from, entryBounds, to, landings, costs );", "rvNavMesh::AddOffMeshLinks pad landings")
    landings_fn = nav[nav.index("bool rvNavMesh::PadLandings") :]
    landings_fn = landings_fn[: landings_fn.index("\n}\n") + 3]
    require(landings_fn, "PadLanding( pad, launch, NULL, landing, &flight )", "rvNavMesh::PadLandings natural landing first")
    require(nav, "bool rvNavMesh::IsSteeredPadLanding", "rvNavMesh::IsSteeredPadLanding")
    require(landings_fn, "standing.IntersectsBounds( entryBounds )", "rvNavMesh::PadLandings no trampoline landings")
    require(landings_fn, "NAV_PAD_LEVEL_BAND", "rvNavMesh::PadLandings one candidate per floor level")
    require(landings_fn, "NavLandingSupported( node.origin )", "rvNavMesh::PadLandings supported candidates")
    # The natural landing is held to the same proof as an air-control one: its
    # node has floor under the middle of the hull, and the flight, steered for
    # it, reaches it from every side a bot enters by.  q4dz1's vertical pads
    # linked a node hanging over their shaft that only the centre's arc hit.
    require(landings_fn, "NearestLandingNode( landing, from, entryBounds )",
            "rvNavMesh::PadLandings natural landing on a supported node")
    require(landings_fn, "PadFlightReaches( pad, launches, 1, nodes[landed].origin, flight, true )",
            "rvNavMesh::PadLandings natural landing proven from the centre as the bot flies it")
    require(landings_fn, "PadFlightReaches( pad, launches, numLaunches, nodes[candidate].origin, centreFlight, false )",
            "rvNavMesh::PadLandings air-control landings hold from every side a bot enters by")
    reaches_fn = nav[nav.index("bool rvNavMesh::PadFlightReaches") :]
    reaches_fn = reaches_fn[: reaches_fn.index("\n}\n") + 3]
    require(reaches_fn, "for ( int l = 0; l < numLaunches; l++ )", "rvNavMesh::PadFlightReaches every launch")
    # A natural landing is flown as the bot flies it: ridden when the pad's
    # own arc comes down on it (q4ctf1's pad 17 slides along a wall; steering
    # for its node drops the flight 192 units), steered for otherwise.
    require(reaches_fn, "natural && PadLanding( pad, launches[l], NULL, landing, &flight )",
            "rvNavMesh::PadFlightReaches rides a natural landing the arc reaches")
    require(reaches_fn, "NavPadArcRidden( launches[l], landing, target )", "rvNavMesh::PadFlightReaches ridden test")
    require(reaches_fn, "PadLanding( pad, launches[l], &target, landing, &flight )", "rvNavMesh::PadFlightReaches steers for the landing")
    require(reaches_fn, "NAV_PAD_LANDING_REACH", "rvNavMesh::PadFlightReaches keeps only landings the flight makes")
    ridden_fn = nav[nav.index("static bool NavPadArcRidden") :]
    ridden_fn = ridden_fn[: ridden_fn.index("\n}\n") + 3]
    require(ridden_fn, "along <= NAV_PAD_RIDE_SHORT", "NavPadArcRidden lands on or past the node, as BotRidesPadArc")
    require(ridden_fn, "Square( NAV_PAD_RIDE_SIDE )", "NavPadArcRidden within a hull to the side")
    nearest_fn = nav[nav.index("int rvNavMesh::NearestLandingNode") :]
    nearest_fn = nearest_fn[: nearest_fn.index("\n}\n") + 3]
    require(nearest_fn, "NavLandingSupported( node.origin )", "rvNavMesh::NearestLandingNode supported node")
    require(nearest_fn, "standing.IntersectsBounds( entryBounds )", "rvNavMesh::NearestLandingNode never on the pad")
    require(nearest_fn, "NAVNODE_TRANSPORT", "rvNavMesh::NearestLandingNode never on a transport")
    pad = nav[nav.index("bool rvNavMesh::PadLanding") :]
    pad = pad[: pad.index("\n}\n") + 3]
    require(pad, "GetCurrentGravity( pad )", "rvNavMesh::PadLanding pad gravity")
    require(pad, "NAV_PAD_AIR_ACCELERATE", "rvNavMesh::PadLanding steers like a bot in flight")
    require(pad, "NavAirSteer( position, velocity, *steerTo, g, wish )", "rvNavMesh::PadLanding shares the bot's air steering")
    require(pad, "NavAgentTranslation( trace, position, next )", "rvNavMesh::PadLanding swept arc")

    # Smoothing may never skip a link that has to be entered deliberately.
    pull = nav[nav.index("void rvNavMesh::StringPull") :]
    pull = pull[: pull.index("\n}\n") + 3]
    require(pull, "!= NAVTRAVEL_WALK", "rvNavMesh::StringPull")
    require_order(
        pull,
        "path.Append( source, NAVTRAVEL_WALK );",
        "path.Append( nodes[nodePath[i + 1]].origin, immediateTravel,",
        "rvNavMesh::StringPull action entry and exit",
    )
    require(pull, "LinkActionEntity(", "rvNavMesh::StringPull volume identity")
    require(pull, "rise > pm_stepsize.GetFloat() * 2.0f", "rvNavMesh::StringPull run-up for tall jumps")

    # Pads and teleporters carry whoever touches them, so the ground under a
    # live volume keeps the transport as its only exit; no route walks across
    # one, starts or ends on one, or shortcuts into one.
    build = nav[nav.index("bool rvNavMesh::Build") :]
    build = build[: build.index("\n}\n") + 3]
    require_order(build, "AddOffMeshLinks();", "SealTransportVolumes();", "rvNavMesh::Build transport sealing")
    require_order(build, "SealTransportVolumes();", "BuildComponents();", "rvNavMesh::Build sealing before closure")
    seal = nav[nav.index("void rvNavMesh::SealTransportVolumes") :]
    seal = seal[: seal.index("\n}\n") + 3]
    require(seal, "standing.IntersectsBounds( volume )", "rvNavMesh::SealTransportVolumes standing box test")
    # The bounding box only approximates a pad: the box against the brush decides.
    require(seal, "collisionModelManager->Contents( nodes[n].origin, &navAgentTrace", "rvNavMesh::SealTransportVolumes exact touch")
    require(seal, "nodes[n].flags |= NAVNODE_TRANSPORT;", "rvNavMesh::SealTransportVolumes flag")
    require(seal, "nodes[n].firstLink = -1;", "rvNavMesh::SealTransportVolumes exits")
    require(seal, "transport.destinations[d]", "rvNavMesh::SealTransportVolumes transport exits")
    require(seal, "transport.costs[d]", "rvNavMesh::SealTransportVolumes per-landing price")
    enters = nav[nav.index("bool rvNavMesh::LineEntersTransport") :]
    enters = enters[: enters.index("\n}\n") + 3]
    require(enters, "NavLineTouchesVolumeBrush( from, to, clip )", "rvNavMesh::LineEntersTransport exact brush test")
    require(nav, "static float NavPadFlightCost", "pad links priced by flight time")
    require(links, "IsActive( TH_THINK )", "rvNavMesh::AddOffMeshLinks only live pads seal")
    nearest = nav[nav.index("int rvNavMesh::FindNearestNode") :]
    nearest = nearest[: nearest.index("\n}\n") + 3]
    require(nearest, "nodes[i].flags & NAVNODE_TRANSPORT", "rvNavMesh::FindNearestNode never snaps onto a pad")
    walkable = nav[nav.index("bool rvNavMesh::WalkableLine") :]
    walkable = walkable[: walkable.index("\n}\n") + 3]
    require(walkable, "LineEntersTransport( from, to )", "rvNavMesh::WalkableLine refuses stepping onto a pad")
    # A player flush against a wall is a legal endpoint the clip epsilon stops
    # a sweep just short of.
    require(walkable, "NavSweepNearlyClear( trace, start, end )", "rvNavMesh::WalkableLine flush-wall endpoints")
    # A shortcut steeper than a walk is another floor, however clear the sweep.
    require(walkable, "stepSize + flat.Length()", "rvNavMesh::WalkableLine walk slope bound")
    require(walkable, "Max( 2,", "rvNavMesh::WalkableLine interior ground sample")
    # The straight sweep climbs with the line and clears a riser's top; the
    # floor samples, endpoints included, may not rise more than a step plus
    # the ground between them.  q4ctf1 string-pulled walks onto the lip of a
    # 64-unit platform and the side of a ramp that way.
    require(walkable, "floor.z - previousFloor > stepSize", "rvNavMesh::WalkableLine per-sample rise bound")
    require(walkable, "betweenFloor.z - previousFloor > stepSize + NAV_WALK_RISE_SLACK", "rvNavMesh::WalkableLine bisected rise")
    require(walkable, "rises more than a step between samples", "rvNavMesh::WalkableLine interior rise refused")
    require(walkable, "rises more than a step onto its end", "rvNavMesh::WalkableLine end rise refused")
    # A hull half over an edge still finds the floor under its other half, so
    # the ground a support box's width to either side of every interior sample
    # has to be there too: q4ctf1's platform edge nodes and q4dz1's catwalk
    # rim stand four units past the edge, and legs along them walked bots off.
    require(walkable, "NavDropBeside( floor, beside ) || NavDropBeside( floor, -beside )",
            "rvNavMesh::WalkableLine ground beside the line")
    require(walkable, "hangs over an edge", "rvNavMesh::WalkableLine rim line refused")
    # Such nodes stay (a player can stand there, and a catwalk may have nothing
    # else) but routing pays to pass through them, so the chain a cell inland
    # is preferred wherever one exists.
    add_node = nav[nav.index("int rvNavMesh::AddNode") :]
    add_node = add_node[: add_node.index("\n}\n") + 3]
    require(add_node, "NavRimNode( origin )", "rvNavMesh::AddNode flags rim nodes")
    require(add_node, "NAVNODE_RIM", "rvNavMesh::AddNode rim flag")
    flood = nav[nav.index("void rvNavMesh::FloodFill") :]
    flood = flood[: flood.index("\n}\n") + 3]
    require(flood, "nodes[neighbour].flags & NAVNODE_RIM", "rvNavMesh::FloodFill prices the step onto a rim node")
    require(flood, "( nodes[current].flags & NAVNODE_RIM ) ? NAV_RIM_PENALTY : 0.0f", "rvNavMesh::FloodFill prices the reverse step onto a rim node")
    rim = nav[nav.index("static bool NavRimNode") :]
    rim = rim[: rim.index("\n}\n") + 3]
    require(rim, "navSupportBounds[1].x + NAV_RIM_OVERHANG", "NavRimNode probes past the support box")
    drop_beside = nav[nav.index("static bool NavDropBeside") :]
    drop_beside = drop_beside[: drop_beside.index("\n}\n") + 3]
    # A probe that begins inside a wall is a wall, not a drop.
    require(drop_beside, "if ( trace.fraction <= 0.0f ) {", "NavDropBeside wall beside is not a drop")
    # A bot reaches a corner up to a corner's reach beside it, so a string-pulled
    # leg that climbs or descends is proven from either side of its corner too:
    # q4ctf1's lip nodes along its ramp proved a diagonal line the bot, on the
    # floor beside it, could not take.
    leg = nav[nav.index("bool rvNavMesh::WalkableLeg") :]
    leg = leg[: leg.index("\n}\n") + 3]
    require(leg, "WalkableLine( from + side, to ) && WalkableLine( from - side, to )", "rvNavMesh::WalkableLeg side proofs")
    require(leg, "NAV_LEG_SIDE_SLACK", "rvNavMesh::WalkableLeg corner reach slack")
    pull = nav[nav.index("void rvNavMesh::StringPull") :]
    pull = pull[: pull.index("\n}\n") + 3]
    require(pull, "WalkableLeg( nodes[nodePath[i]].origin, nodes[nodePath[j]].origin )", "rvNavMesh::StringPull proves legs from beside the corner")
    require(pull, "WalkableLeg( lastOrigin, goal )", "rvNavMesh::StringPull proves the final leg from beside the corner")
    require_order(
        walkable,
        "NAV_GROUND_OFFSET",
        "probeLift = stepSize",
        "rvNavMesh::WalkableLine low-clearance flat sweep",
    )
    require(
        walkable,
        "NavAgentTranslation",
        "rvNavMesh::WalkableLine usable closed doors",
    )

    # Areas are weak/undirected by design, so all public reachability decisions
    # must still respect outgoing links.
    reachable = nav[nav.index("bool rvNavMesh::IsReachable") :]
    reachable = reachable[: reachable.index("\n}\n") + 3]
    require(reachable, "FindPath( start, goal, path )", "rvNavMesh::IsReachable")

    roam = nav[nav.index("bool rvNavMesh::RandomReachablePoint") :]
    roam = roam[: roam.index("\n}\n") + 3]
    # Draws are filtered by the exact component closure, which is constant
    # time, and each accepted draw is proven by a real route.  The draw is
    # widened past idRandom's 15 bits so large maps can reach every node.
    require(roam, "NodeCanReach( startNode, candidate )", "rvNavMesh::RandomReachablePoint")
    require(roam, "RandomInt() << 15", "rvNavMesh::RandomReachablePoint wide draw")
    require(roam, "FindPath( origin, nodes[candidate].origin", "rvNavMesh::RandomReachablePoint")
    require(roam, "attempts < NAV_MAX_ROUTE_REPAIRS", "rvNavMesh::RandomReachablePoint retry")
    require(roam, "nodes[candidate].flags & NAVNODE_TRANSPORT", "rvNavMesh::RandomReachablePoint pads are no stop")

    same_node = nav[nav.index("if ( startNode == goalNode )", nav.index("bool rvNavMesh::FindPathInternal")) :]
    same_node = same_node[: same_node.index("\n\t}\n") + 4]
    require(
        same_node,
        "if ( !WalkableLine( routeStart, routeGoal ) )",
        "rvNavMesh::FindPath same node",
    )
    require(
        same_node,
        "WalkableLine( routeStart, via )",
        "rvNavMesh::FindPath same-node inbound leg",
    )
    require(
        same_node,
        "WalkableLine( via, routeGoal )",
        "rvNavMesh::FindPath same-node outbound leg",
    )
    require(same_node, "path.Append( via, NAVTRAVEL_WALK )", "rvNavMesh::FindPath same-node via route")

    # FindPath is a thin wrapper that counts the query for bot_profile; the
    # search itself lives in FindPathInternal.
    wrapper = nav[nav.index("bool rvNavMesh::FindPath(") :]
    wrapper = wrapper[: wrapper.index("\n}\n") + 3]
    require(wrapper, "FindPathInternal( start, goal, path, nearGoal, maxImpactDelta )", "rvNavMesh::FindPath wrapper")
    require(wrapper, "stats.pathSearches++", "rvNavMesh::FindPath profiling")

    find_path = nav[nav.index("bool rvNavMesh::FindPathInternal") :]
    find_path = find_path[: find_path.index("\n}\n") + 3]
    require(
        find_path,
        "path[path.Num() - 1].origin - routeGoal",
        "rvNavMesh::FindPath exact-goal completion",
    )
    require(
        find_path,
        "FindNearestNode( routeStart, 256.0f, true )",
        "rvNavMesh::FindPath reachable start snap",
    )
    require(
        find_path,
        "!WalkableLine( routeStart, path[0].origin )",
        "rvNavMesh::FindPath safe first shortcut",
    )
    require(find_path, "path.Clear();", "rvNavMesh::FindPath rejected partial path")
    # Exact directed reachability rejects an unreachable goal before any
    # search, and A* is guided by ALT landmarks on top of the transport-safe
    # straight-line bound.  A node a landmark proves cannot lead to the goal
    # is never opened.
    require(find_path, "NodeCanReach( startNode, goalNode )", "rvNavMesh::FindPath component rejection")
    require(find_path, "Heuristic( next, goalNode )", "rvNavMesh::FindPath landmark heuristic")
    require(find_path, "h >= NAV_UNREACHED", "rvNavMesh::FindPath dead-end pruning")
    # Goal choice prices only the targets that can still win: best landmark
    # bound first, each by an exact corner-free A*, stopping once the best few
    # are certain.  A whole-map field per decision was most of the bots' CPU.
    ranked = nav[nav.index("int rvNavMesh::BuildCostField( const idVec3 &origin, float maxCost, const navFieldTarget_t") :]
    ranked = ranked[: ranked.index("\n}\n") + 3]
    require(ranked, "Heuristic( startNode, node )", "rvNavMesh::BuildCostField landmark bound per target")
    require(ranked, "PathCost( startNode, node, maxCost )", "rvNavMesh::BuildCostField exact pricing")
    require(ranked, "best[keepBest - 1] >= bound[i]", "rvNavMesh::BuildCostField branch and bound stop")
    # Weak bounds must not let exact searches cost more than one field.
    require(ranked, "spent > budget", "rvNavMesh::BuildCostField pricing work budget")
    require(ranked, "RankedSettle( startNode, maxCost, startLeg,", "rvNavMesh::BuildCostField ranked field fallback")
    heuristic = nav[nav.index("float rvNavMesh::Heuristic") :]
    heuristic = heuristic[: heuristic.index("\n}\n") + 3]
    require(heuristic, "heuristicScale", "rvNavMesh::Heuristic transport-safe straight-line bound")
    require(heuristic, "landmarkGoal - landmarkNode", "rvNavMesh::Heuristic forward landmark bound")
    require(heuristic, "nodeLandmark - goalLandmark", "rvNavMesh::Heuristic backward landmark bound")
    require(heuristic, "node * NAV_LANDMARK_STRIDE", "rvNavMesh::Heuristic node-major landmark rows")
    require(heuristic, "return NAV_INFINITE_COST;", "rvNavMesh::Heuristic unreachable proof")
    require(find_path, "linkRejectTag", "rvNavMesh::FindPath blocked-edge repair")
    require(find_path, "LinkTraversable(", "rvNavMesh::FindPath live route validation")
    require(find_path, "linkRejectTag[link] = rejectSerial;", "rvNavMesh::FindPath local repair exclusions")
    require(find_path, "LandingSurvivable( nodes[nodePath[i]].origin, landing, maxImpactDelta )", "rvNavMesh::FindPath health-aware selected-edge validation")
    require(find_path, "SetTravelCost(", "rvNavMesh::FindPath weighted route cost")
    # A goal beside a pad cannot be stood on; its nearest node is the arrival.
    require(find_path, "TransportAtFloor( routeGoal ) >= 0", "rvNavMesh::FindPath transport-side goals")
    # A chased player in a spot no node fits is approached, not refused.
    require(find_path, "goalNode == -1 && nearGoal", "rvNavMesh::FindPath pursuit near-goal")
    # A refused final leg stands when the goal's snap proved the walk the other
    # way, or when the box stops within touch of a goal set against a wall.
    require(find_path, "WalkableLine( routeGoal, last )", "rvNavMesh::FindPath reverse-proven final leg")
    require(find_path, "LegTouchesGoal( last, routeGoal, touch )", "rvNavMesh::FindPath goal within touch")
    require(find_path, "TransportAtFloor( routeGoal ) < 0", "rvNavMesh::FindPath no reverse proof into a pad")
    if find_path.count("path.routeStart = routeStart;") != find_path.count("return true;") + 1:
        raise AssertionError(
            "FindPathInternal must record the grounded route start on every success path; "
            "rvBot::OffRoute measures the first leg from it"
        )
    require(find_path, "routeStart", "rvNavMesh::FindPath airborne start projection")
    require(find_path, "routeGoal", "rvNavMesh::FindPath airborne goal projection")
    require(find_path, "GroundUnder( start", "rvNavMesh::FindPath safe landing projection")
    require(find_path, "start.z - projected.z > pm_stepsize", "rvNavMesh::FindPath airborne-only fallback")
    if find_path.index("GroundUnder( start") > find_path.index("FindNearestNode( routeStart"):
        raise AssertionError(
            "FindPath snaps before projecting an airborne endpoint; a nearby in-air node can "
            "then preserve an unreachable exact goal instead of routing to the landing floor"
        )

    link_nodes = nav[nav.index("void rvNavMesh::LinkNodes") :]
    link_nodes = link_nodes[: link_nodes.index("\n}\n") + 3]
    require(link_nodes, "cost / geometricCost", "rvNavMesh admissible heuristic scale")
    require(link_nodes, "Min( heuristicScale, edgeScale )", "rvNavMesh heuristic lower bound")

    length_from = nav[nav.index("float rvNavPath::LengthFrom") :]
    length_from = length_from[: length_from.index("\n}\n") + 3]
    require(length_from, "hasTravelCost", "rvNavPath retained graph cost")
    require(length_from, "return travelCost;", "rvNavPath weighted goal comparison")

    begin_search = nav[nav.index("void rvNavMesh::BeginSearch") :]
    begin_search = begin_search[: begin_search.index("\n}\n") + 3]
    require(begin_search, "0x7ffffffe", "rvNavMesh search serial rollover")
    require(begin_search, "openTag[i] = 0", "rvNavMesh search tag reset")

    traversable = nav[nav.index("bool rvNavMesh::LinkTraversable") :]
    traversable = traversable[: traversable.index("\n}\n") + 3]
    for travel in (
        "NAVTRAVEL_WALK",
        "NAVTRAVEL_DROP",
        "NAVTRAVEL_JUMP",
        "NAVTRAVEL_JUMPPAD",
        "NAVTRAVEL_TELEPORT",
    ):
        require(traversable, travel, "rvNavMesh::LinkTraversable")
    grid_validation = traversable[: traversable.index("case NAVTRAVEL_JUMPPAD")]
    # Only solids that can move after the build may invalidate a proven link.
    # Stock maps are mostly func_static brush entities, which generation
    # already proved; sweeping them again rejected working links and cost two
    # repair searches per route.
    require(
        grid_validation,
        "DynamicTranslation(",
        "rvNavMesh::LinkTraversable dynamic-entity validation",
    )
    require(grid_validation, "dynamicSolids.Num() == 0", "rvNavMesh::LinkTraversable mover-free maps")
    require(grid_validation, "DynamicSolidNear( swept )", "rvNavMesh::LinkTraversable broad phase")
    dynamic = nav[nav.index("bool rvNavMesh::DynamicTranslation") :]
    dynamic = dynamic[: dynamic.index("\n}\n") + 3]
    # The sweep asks only the recorded dynamic solids, never players or static
    # props: the pass-entity form spent one of its two passes on the bot's own
    # body and declared a link clear once a third static blocker turned up.
    require(dynamic, "dynamicSolids[i]", "rvNavMesh::DynamicTranslation sweeps recorded movers only")
    require(dynamic, "NavDoorIsWalkActivated( ent )", "rvNavMesh::DynamicTranslation passes walk doors")
    if "TranslationEntities(" in dynamic:
        raise AssertionError(
            "rvNavMesh::DynamicTranslation sweeps every solid entity again; players and static "
            "props must not take part in live link validation"
        )
    if "TryStep(" in grid_validation:
        raise AssertionError(
            "LinkTraversable re-samples generated grid topology; live repair must only "
            "invalidate links blocked by newly present solid entities"
        )
    if "WalkableLine(" in grid_validation:
        raise AssertionError(
            "LinkTraversable uses the stricter string-pull proof on adjacent graph edges; "
            "stair-riser midpoint probes can reject links that TryStep legally generated"
        )
    require(
        nav,
        "collisionModelManager->Translation( &hit, start, end, &navAgentTrace",
        "rvNavMesh dynamic route-edge entity sweep",
    )
    require(traversable, "IsActive( TH_THINK )", "rvNavMesh::LinkTraversable live jump pad")

    header = read(mp / "bots" / "NavMesh.h")
    require(header, "LengthFrom", "rvNavPath complete length")
    require(header, "travelCost", "rvNavPath weighted route cost")
    require(header, "heuristicScale", "rvNavMesh scaled admissible heuristic")
    for travel in (
        "NAVTRAVEL_WALK",
        "NAVTRAVEL_DROP",
        "NAVTRAVEL_JUMP",
        "NAVTRAVEL_JUMPPAD",
        "NAVTRAVEL_TELEPORT",
    ):
        require(header, travel, "navTravelType_t")

    # The debug colour table is indexed by travel type, so it has to keep pace.
    enum_body = header[header.index("typedef enum {") : header.index("} navTravelType_t;")]
    travel_types = [
        name for name in re.findall(r"\b(NAVTRAVEL_\w+)\b", enum_body) if name != "NAVTRAVEL_NUM"
    ]
    table_start = nav.index("travelColors[NAVTRAVEL_NUM]")
    table = nav[table_start : nav.index("};", table_start)]
    if table.count("&color") != len(travel_types):
        raise AssertionError(
            "rvNavMesh::DebugDraw travelColors has "
            f"{table.count('&color')} entries for {len(travel_types)} travel types"
        )


def validate_cvars() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    declared = read(mp / "gamesys" / "SysCvar.cpp")
    exported = read(mp / "gamesys" / "SysCvar.h")
    sp_bots = read(GAME_LIBS_ROOT / "src" / "game" / "bots" / "Bot.cpp")

    for name in (
        "bot_enable",
        "bot_minPlayers",
        "bot_skill",
        "bot_debug",
        "bot_debugNav",
        "bot_navCellSize",
        "bot_pause",
    ):
        require(declared, f'idCVar {name}(', "mpgame SysCvar.cpp")
        require(exported, f"extern idCVar {name};", "mpgame SysCvar.h")

    # These legacy SP bot diagnostics share global names with the richer MP
    # diagnostics. Keep their declaration signatures aligned so loading SP
    # before MP cannot leave the CVar system with conflicting types or policy.
    for name in ("bot_debug", "bot_debugnav"):
        sp_signature = cvar_signature(sp_bots, name, "game bots/Bot.cpp")
        mp_signature = cvar_signature(declared, name, "mpgame SysCvar.cpp")
        if sp_signature != mp_signature:
            raise AssertionError(f"SP/MP bot CVar signatures differ for {name!r}")
        if "CVAR_INTEGER" not in sp_signature[1] or "CVAR_BOOL" in sp_signature[1]:
            raise AssertionError(f"{name} must retain the shared integer diagnostic type")

    commands = read(mp / "gamesys" / "SysCmds.cpp")
    for command in ("addbot", "removebot", "kickbots", "botlist", "navmesh"):
        require(commands, f'"{command}"', "mpgame console commands")


def validate_liquid_dive() -> None:
    """A swimming bot follows a route under the surface while it has air to spare.

    The navmesh keeps a pool's floor, so a route can drop onto it.  The bot used
    to surface whenever its eyes were under water and then trod water above that
    corner until the stuck check gave up.  Lava and slime still always climb.
    """
    bot = read(GAME_LIBS_ROOT / "src/mpgame/bots/Bot.cpp")
    context = "rvBot::UpdateLiquidMovement dive"
    body = bot[bot.index("void rvBot::UpdateLiquidMovement(") :]
    body = body[: body.index("\n}\n") + 3]
    for token in (
        "if ( swimming && pathCorner >= 0 && pathCorner < path.Num() ) {",
        "corner.z < self->GetPhysics()->GetOrigin().z - BOT_LIQUID_DIVE_DEPTH",
        "& CONTENTS_WATER )",
        "self->GetAirTics() > pm_airTics.GetInteger() / 2",
        "cmd.upmove = -127;",
    ):
        require(body, token, context)
    require_order(body, "feetLiquid & ( CONTENTS_LAVA | CONTENTS_SLIME )", "cmd.upmove = -127;", context)
    require_order(body, "cmd.upmove = -127;", "if ( LiquidAtEye() ) {", context)
    require(bot, "const float BOT_LIQUID_DIVE_DEPTH\t= 16.0f;", context)

    player_h = read(GAME_LIBS_ROOT / "src/mpgame/Player.h")
    require(player_h, "GetAirTics( void ) const { return airTics; }", "idPlayer air accessor")

    # "Half the reservoir is six seconds of stock air": underwater the whole
    # pm_air reservoir drains over pm_waterAir frames at 60 Hz.
    player = read(GAME_LIBS_ROOT / "src/mpgame/Player.cpp")
    require(player, "airDrain = fullAir / waterAir;", "idPlayer::UpdateAir underwater drain")
    cvars = read(GAME_LIBS_ROOT / "src/mpgame/gamesys/SysCvar.cpp")
    air, _ = cvar_signature(cvars, "pm_air", "SysCvar.cpp")
    water_air, _ = cvar_signature(cvars, "pm_waterAir", "SysCvar.cpp")
    if (air, water_air) != ("1800", "720") or int(water_air) // 2 != 6 * 60:
        raise AssertionError(f"stock air no longer gives six seconds at half a reservoir: pm_air {air}, pm_waterAir {water_air}")


def main() -> int:
    try:
        from mp_bot_audit import world_deaths
        sample = "Kane was killed by world\nRhodes drowned\nKane was railed by Rhodes\nObserver died\n"
        assert world_deaths(sample, {"Kane", "Rhodes"}) == ["Rhodes", "Kane"]
        validate_engine()
        validate_game_wiring()
        validate_navmesh()
        validate_cvars()
        validate_liquid_dive()
    except AssertionError as error:
        print(f"mp_bot_navigation: FAILED - {error}")
        return 1

    print("mp_bot_navigation: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
