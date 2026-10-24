#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and executable contracts for competitive gameplay pause safety."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated {signature}")


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
        raise AssertionError(
            f"expected {first!r} before {second!r} in {context}"
        )


def central_frame_contracts() -> None:
    game_local = read("src/mpgame/Game_local.cpp")
    game_network = read("src/mpgame/Game_network.cpp")
    multiplayer = read("src/mpgame/MultiplayerGame.cpp")
    events = read("src/mpgame/gamesys/Event.cpp")
    entity = read("src/mpgame/Entity.cpp")

    frame = body(game_local, "gameReturn_t idGameLocal::RunFrame")
    require_before(
        frame,
        "time += GetMSec();",
        "mpGame.BeginCompetitiveFrame();",
        "engine clock precedes authoritative pause boundary",
    )
    for later in (
        "random.RandomInt();",
        "botManager.Think();",
        "ServerProcessEntityNetworkEventQueue();",
        "// let entities think",
        "idEvent::ShiftEvents( msec )",
    ):
        require_before(
            frame,
            "mpGame.BeginCompetitiveFrame();",
            later,
            "pause commit precedes gameplay work",
        )

    entity_start = frame.find("// let entities think")
    entity_end = frame.find("// remove any entities that have stopped thinking")
    if entity_start < 0 or entity_end <= entity_start:
        raise AssertionError("could not isolate the entity-thinking section")
    entity_section = frame[entity_start:entity_end]
    frozen_start = entity_section.find("if ( competitiveGameplayFrozen ) {")
    frozen_end = entity_section.find("} else if", frozen_start)
    if frozen_start < 0 or frozen_end <= frozen_start:
        raise AssertionError("could not isolate the frozen entity pass")
    frozen = entity_section[frozen_start:frozen_end]
    require(frozen, "spawnedEntities.Next()", "complete frozen-owner traversal")
    require(frozen, "ent->ThinkMatchPaused( msec );", "deadline rebasing hook")
    reject(frozen, "activeEntities.Next()", "complete frozen-owner traversal")
    reject(frozen, "ent->Think();", "frozen gameplay simulation")
    reject(frozen, "RunPhysics", "frozen gameplay simulation")

    require(
        frame,
        "if ( !competitiveGameplayFrozen ) {\n\t\t\tServerProcessEntityNetworkEventQueue();",
        "network gameplay-event suppression",
    )
    require(
        frame,
        "if ( competitiveGameplayFrozen ) {\n\t\t\tif ( !idEvent::ShiftEvents( msec ) )",
        "posted-event rebasing branch",
    )
    require_before(
        frame,
        "idEvent::ShiftEvents( msec )",
        "mpGame.Run();",
        "gameplay deadlines before live match operations",
    )

    begin = body(multiplayer, "void idMultiplayerGame::BeginCompetitiveFrame")
    if begin.count("matchSession.AdvanceFrame(") != 1:
        raise AssertionError("the authoritative session clock must advance once per frame")
    if begin.count("RebaseCompetitivePauseFrame(") != 1:
        raise AssertionError("adapter-owned deadlines must rebase once per frozen frame")
    require_before(
        begin,
        "matchSession.AdvanceFrame(",
        "if ( IsGameplayFrozen() )",
        "session clock before pause overlay application",
    )

    frozen_predicate = body(multiplayer, "bool idMultiplayerGame::IsGameplayFrozen")
    require(
        frozen_predicate,
        "matchSession.GetPause().state != MP_MATCH_PAUSE_RUNNING",
        "server pause predicate reads the post-boundary overlay",
    )

    session = read("src/mpgame/mp/match/MatchSession.cpp")
    running_clock = body(session, "bool mpMatchSession::IsMatchClockRunning")
    require(running_clock, "pause.state == MP_MATCH_PAUSE_RUNNING", "frozen match clock")
    reject(
        running_clock,
        "MP_MATCH_PAUSE_PENDING",
        "pending boundary frame must not advance match time",
    )

    # The adapter simulates the frame using the post-transition overlay, so the
    # accrual has to be decided from the same post-transition overlay.  Resolving
    # the transitions after the accrual loses one frame per pause/resume cycle.
    advance = body(session, "mpMatchMutationResult mpMatchSession::AdvanceFrame")
    require(
        advance,
        "pause.state == MP_MATCH_PAUSE_RUNNING || resumeCompletes",
        "post-transition match-clock gate",
    )
    require_before(
        advance,
        "const bool resumeCompletes",
        "AddNonNegativeMsec( matchTime.Milliseconds(), delta, nextMatchMsec )",
        "pause-overlay transitions decided before the accrual",
    )
    reject(
        advance,
        "IsMatchClockRunning()",
        "pre-transition accrual gate",
    )

    shift_events = body(events, "bool idEvent::ShiftEvents")
    require(shift_events, "event->time > maxInt - deltaMsec", "overflow preflight")
    require_before(
        shift_events,
        "event->time > maxInt - deltaMsec",
        "event->time += deltaMsec",
        "transactional event-queue shift",
    )
    if shift_events.count("for ( idEvent *event = EventQueue.Next()") != 2:
        raise AssertionError("event shift must retain separate validation and commit passes")

    entity_pause = body(entity, "void idEntity::ThinkMatchPaused")
    require(entity_pause, "physics->GetTime()", "trajectory-local physics time source")
    require(entity_pause, "physics->UpdateTime", "mover trajectory rebase")
    reject(entity_pause, "physics->UpdateTime( gameLocal.time", "dormant trajectory time jump")
    require(entity_pause, "gameRenderWorld->UpdateEntityDef", "rebased shader parameters submitted")
    reject(entity_pause, "Evaluate(", "frozen collision evaluation")
    reject(entity_pause, "RunPhysics", "frozen collision evaluation")
    animated_pause = body(entity, "void idAnimatedEntity::ThinkMatchPaused")
    require(animated_pause, "animator.ShiftTime( deltaMsec );", "animation clock rebase")

    damage = body(entity, "void idEntity::Damage( idEntity *inflictor")
    require(damage, "IsGameplayFrozen()", "direct-damage suppression")
    radius = body(game_local, "void idGameLocal::RadiusDamage")
    require(radius, "IsGameplayFrozen()", "radius-damage suppression")

    prediction = body(game_network, "gameReturn_t idGameLocal::ClientPrediction")
    require_before(
        prediction,
        "time += GetMSec();",
        "const bool competitiveGameplayFrozen = mpGame.IsGameplayFrozen();",
        "client time precedes the projected pause boundary",
    )
    frozen_prediction = body(
        prediction, "if ( competitiveGameplayFrozen )"
    )
    new_frozen_frame = body(frozen_prediction, "if ( isNewFrame )")
    require(
        new_frozen_frame,
        "spawnedEntities.Next()",
        "complete client deadline-owner traversal",
    )
    require(
        new_frozen_frame,
        "ent->ThinkMatchPaused( frameMsec );",
        "client deadline rebasing hook",
    )
    require_before(
        new_frozen_frame,
        "usercmds = NULL;",
        "ent->ThinkMatchPaused( frameMsec );",
        "non-presenting deadline pass",
    )
    require_before(
        new_frozen_frame,
        "ent->ThinkMatchPaused( frameMsec );",
        "usercmds = savedUsercmds;",
        "restore prediction input after deadline pass",
    )
    require(
        new_frozen_frame,
        "idEvent::ShiftEvents( frameMsec )",
        "once-per-frame client posted-event rebase",
    )
    if frozen_prediction.count("ent->ThinkMatchPaused( frameMsec );") != 1:
        raise AssertionError("client entities must rebase exactly once per new frame")
    if frozen_prediction.count("idEvent::ShiftEvents( frameMsec )") != 1:
        raise AssertionError("client posted events must rebase exactly once per new frame")

    require(
        frozen_prediction,
        "pauseViewPlayer->ThinkMatchPaused( 0 );",
        "zero-delta view presentation on prediction replays",
    )
    reject(
        new_frozen_frame,
        "pauseViewPlayer->ThinkMatchPaused( 0 );",
        "view presentation must not be limited to real new frames",
    )
    for token in (
        "clientNum == MAX_CLIENTS && isRepeater",
        "GetDemoFollowClient()",
        "clientNum == MAX_CLIENTS && player && isNewFrame && !isRepeater",
        "followPlayer = player->spectator",
    ):
        require(frozen_prediction, token, "repeater/free-view pause routing")

    for forbidden in (
        "ClientPredictionThink();",
        "clientSpawnedEntities.Next()",
        "cent->Think();",
        "idEvent::ServiceEvents();",
        "bse->",
        "RunPhysics",
        "ent->Think();",
    ):
        reject(frozen_prediction, forbidden, "frozen client gameplay simulation")


def owned_deadline_contracts() -> None:
    projectile = read("src/mpgame/Projectile.cpp")
    mover = read("src/mpgame/Mover.cpp")
    moveable = read("src/mpgame/Moveable.cpp")
    actor = read("src/mpgame/Actor.cpp")
    item = read("src/mpgame/Item.cpp")
    trigger = read("src/mpgame/Trigger.cpp")
    light = read("src/mpgame/Light.cpp")
    state = read("src/mpgame/gamesys/State.cpp")
    player = read("src/mpgame/Player.cpp")
    weapon = read("src/mpgame/Weapon.cpp")
    game_state = read("src/mpgame/mp/GameState.cpp")
    round_state = read("src/mpgame/mp/RoundGameState.cpp")
    tourney = read("src/mpgame/mp/Tourney.cpp")
    round_modes = read("src/mpgame/mp/RoundModes.cpp")

    projectile_pause = body(projectile, "void idProjectile::ThinkMatchPaused")
    for token in (
        "speed.SetStartTime",
        "rotation.SetStartTime",
        "mpMatchShiftTimeOrigin( launchTime",
        "mpMatchShiftOptionalDeadline( lightEndTime",
        "gameRenderWorld->UpdateLightDef",
    ):
        require(projectile_pause, token, "projectile pause clocks")
    guided_pause = body(projectile, "void idGuidedProjectile::ThinkMatchPaused")
    for token in ("launchTime", "driftTime", "turn_max.SetStartTime"):
        require(guided_pause, token, "guided-projectile pause clocks")
    drifting_pause = body(projectile, "void rvDriftingProjectile::ThinkMatchPaused")
    require(drifting_pause, "driftOffset", "drifting-projectile offset clocks")
    require(drifting_pause, "driftSpeed.SetStartTime", "drifting-projectile speed clock")

    mover_pause = body(mover, "void idMover::ThinkMatchPaused")
    require(mover_pause, "splineStartTime", "spline mover phase clock")
    require(mover_pause, "splineStateThread.ShiftMatchTime", "spline mover state delays")
    binary_pause = body(mover, "void idMover_Binary::ThinkMatchPaused")
    require(binary_pause, "stateStartTime", "binary mover interpolation origin")
    moveable_pause = body(moveable, "void idMoveable::ThinkMatchPaused")
    require(moveable_pause, "initialSpline->ShiftTime", "moveable initial spline")
    barrel_pause = body(moveable, "void idExplodingBarrel::ThinkMatchPaused")
    for token in ("explodeFinishTime", "UpdateEntityDef"):
        require(barrel_pause, token, "exploding-moveable clocks")

    actor_pause = body(actor, "void idActor::ThinkMatchPaused")
    for token in (
        "pain_debounce_time",
        "deathPushTime",
        "stateThread.ShiftMatchTime",
        "headAnim.GetStateThread().ShiftMatchTime",
        "torsoAnim.GetStateThread().ShiftMatchTime",
        "legsAnim.GetStateThread().ShiftMatchTime",
    ):
        require(actor_pause, token, "actor gameplay clocks")

    state_pause = body(state, "void rvStateThread::ShiftMatchTime")
    require(state_pause, "states.Next()", "queued state-delay clocks")
    require(state_pause, "interrupted.Next()", "interrupted state-delay clocks")
    if state_pause.count("mpMatchShiftOptionalDeadline( call->parms.time") != 2:
        raise AssertionError("both state-thread queues must shift their delay origins")

    item_pause = body(item, "void idItemPowerup::ThinkMatchPaused")
    require(item_pause, "droppedTime", "dropped-powerup remaining lifetime")
    for signature, field in (
        ("void idTrigger_Multi::ThinkMatchPaused", "nextTriggerTime"),
        ("void idTrigger_EntityName::ThinkMatchPaused", "nextTriggerTime"),
        ("void idTrigger_Hurt::ThinkMatchPaused", "nextTime"),
    ):
        require(body(trigger, signature), field, "trigger cooldown clocks")

    light_pause = body(light, "void idLight::ThinkMatchPaused")
    require(light_pause, "fadeStart", "light fade origin")
    require(light_pause, "fadeEnd", "light fade deadline")
    require(light_pause, "PresentLightDefChange();", "paused light render submission")

    player_pause = body(player, "void idPlayer::ThinkMatchPaused")
    require(player_pause, "inventory.ShiftMatchTime", "powerup and regeneration clocks")
    require(player_pause, "weapon->ShiftMatchTime", "weapon gameplay clocks")
    require(player_pause, "lastArenaChange", "Tourney input cooldown")
    require(player_pause, "lastSpectateTeleport", "spectator teleport cooldown")
    weapon_pause = body(weapon, "void rvWeapon::ShiftMatchTime")
    require(weapon_pause, "stateThread.ShiftMatchTime", "weapon state-delay clocks")

    base_state_pause = body(game_state, "void rvGameState::ShiftMatchTime")
    for token in ("nextStateTime", "fragLimitTimeout", "overtimeStartTime"):
        require(base_state_pause, token, "base match-state deadlines")
    round_pause = body(round_state, "void rvRoundGameState::ShiftMatchTime")
    for token in ("roundStartTime", "roundStateTime"):
        require(round_pause, token, "round deadlines")
    tourney_pause = body(tourney, "void rvTourneyArena::ShiftMatchTime")
    for token in ("nextStateTime", "fragLimitTimeout", "matchStartTime"):
        require(tourney_pause, token, "Tourney arena deadlines")
    freeze_pause = body(round_modes, "void rvFreezeTagGameState::ShiftMatchTime")
    require(freeze_pause, "lastThawAnnounce", "Freeze Tag notification cadence")


def compile_deadline_harness() -> None:
    compiler = next(
        (path for name in ("c++", "g++", "clang++", "cl") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_pause_contract: no standalone C++ compiler; static checks only")
        return

    tmp_root = ROOT / ".tmp"
    tmp_root.mkdir(exist_ok=True)
    # The Windows ARM64 runners refuse to delete the just-run harness executable
    # (WinError 5) after the checks pass; a leftover file in .tmp must not turn
    # that pass into a failure.
    with tempfile.TemporaryDirectory(prefix="mp-pause-contract-", dir=tmp_root, ignore_cleanup_errors=True) as raw:
        temp = Path(raw)
        source = temp / "deadline_harness.cpp"
        source.write_text(
            r'''
#include "mp/match/MatchDeadline.h"

int main() {
    if ( mpMatchAddMillisecondsClamped( 2147483640, 16 ) != 2147483647 ) return 1;
    if ( mpMatchAddMillisecondsClamped( 10, 16 ) != 26 ) return 2;

    int optional = 0;
    mpMatchShiftOptionalDeadline( optional, 16 );
    if ( optional != 0 ) return 3;
    optional = 10;
    mpMatchShiftOptionalDeadline( optional, 16 );
    if ( optional != 26 ) return 4;

    int origin = 0;
    mpMatchShiftTimeOrigin( origin, 16 );
    if ( origin != 16 ) return 5;
    origin = -1;
    mpMatchShiftTimeOrigin( origin, 16 );
    if ( origin != -1 ) return 6;

    float floatOrigin = 0.0f;
    mpMatchShiftTimeOrigin( floatOrigin, 16 );
    if ( floatOrigin != 16.0f ) return 7;
    return 0;
}
'''.lstrip(),
            encoding="utf-8",
        )

        compiler_name = Path(compiler).name.lower()
        if compiler_name in ("cl", "cl.exe"):
            executable = temp / "deadline_harness.exe"
            command = [
                compiler,
                "/nologo",
                "/EHsc",
                f"/I{ROOT / 'src/mpgame'}",
                str(source),
                f"/Fe:{executable}",
            ]
        else:
            executable = temp / "deadline_harness"
            command = [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-pedantic",
                f"-I{ROOT / 'src/mpgame'}",
                str(source),
                "-o",
                str(executable),
            ]
        subprocess.run(command, cwd=temp, check=True)
        subprocess.run([str(executable)], cwd=temp, check=True)


def compile_prediction_clock_harness() -> None:
    """Execute the production clock/rebase delta through snapshot corrections."""
    prediction = body(read("src/mpgame/Game_network.cpp"),
                      "gameReturn_t idGameLocal::ClientPrediction")
    clock = prediction.split("// update the real client time and the new frame flag", 1)[1]
    clock = clock.split("if ( clientNum == MAX_CLIENTS )", 1)[0]
    frozen = body(body(prediction, "if ( competitiveGameplayFrozen )"), "if ( isNewFrame )")
    delta = frozen[frozen.index("const int frameMsec ="):].split(";", 1)[0] + ";"
    source_text = r'''
#include <cassert>
#include <cstdio>
static int GetMSec() { return 16; }
// The clock block also counts replay passes for net_mpPredictDebug's pose age.
static int clientReplayTics = 0;
static int PausedDelta(int time, int &realClientTime) {
    bool isNewFrame = false;
    @CLOCK@
    if (isNewFrame) { @DELTA@ return frameMsec; }
    return 0;
}
int main() {
    // Joining or seeking a paused match establishes a fresh clock baseline.
    for (int initial : {16, 100000}) {
        int real = 0;
        assert(PausedDelta(initial, real) == 16);
        assert(real == initial);
        assert(PausedDelta(initial, real) == 0);
        assert(PausedDelta(initial - 16, real) == 0);
        assert(real == initial);
        assert(PausedDelta(initial + 16, real) == 16);
        // A newer snapshot can skip many predicted frames after a stall.
        assert(PausedDelta(initial + 1040, real) == 1024);
        assert(real == initial + 1040);
        assert(PausedDelta(initial + 512, real) == 0);
        assert(PausedDelta(initial + 1040, real) == 0);
        assert(PausedDelta(initial + 1056, real) == 16);
    }
    int real = 5000, animationStart = 4000, expiry = 9000;
    for (int clock : {5016, 5016, 5000, 6032, 5600, 6032, 6048, 7072}) {
        const int delta = PausedDelta(clock, real);
        animationStart += delta;
        expiry += delta;
        assert(real - animationStart == 1000);
        assert(expiry - real == 4000);
    }
    puts("client pause clock: snapshot jumps, replays and initial baselines PASS");
}
'''.replace("@CLOCK@", clock).replace("@DELTA@", delta)
    source_text = "#include <initializer_list>\n" + source_text
    compiler = shutil.which("clang++") or shutil.which("g++") or shutil.which("c++")
    if compiler is None:
        raise AssertionError("client prediction clock check requires a C++ compiler")
    # The Windows ARM64 runners refuse to delete the just-run harness executable
    # (WinError 5) after the checks pass; a leftover file in .tmp must not turn
    # that pass into a failure.
    with tempfile.TemporaryDirectory(prefix="mp-pause-prediction-", dir=ROOT / ".tmp", ignore_cleanup_errors=True) as raw:
        temp = Path(raw)
        source, executable = temp / "prediction.cpp", temp / "prediction.exe"
        source.write_text(source_text, encoding="utf-8")
        subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(executable)], cwd=temp, check=True)
        subprocess.run([str(executable)], cwd=temp, check=True)


MATCH_CLOCK_HARNESS = r'''
#include "mpgame/mp/match/MatchSession.h"

#define CHECK( condition ) do { if ( !( condition ) ) { return __LINE__; } } while ( 0 )
#define STEP( session ) do { const int failedLine = StepFrame( session ); \
	if ( failedLine != 0 ) { return failedLine; } } while ( 0 )

static const int FRAME_MSEC = 16;
static const int TIMEOUT_MSEC = 2000;
static const int RESUME_COUNTDOWN_MSEC = 100;

static int64_t engineMsec = 0;
static int simulatedFrames = 0;
static int resumeCompletions = 0;

static bool AppliedOnce( const mpMatchMutationResult &result ) {
	return result.WasApplied() && result.currentRevision == result.previousRevision + 1;
}

// Mirrors idMultiplayerGame::IsGameplayFrozen on the server.  The adapter reads
// the overlay after AdvanceFrame has committed this frame's boundary, so the
// match clock has to accrue exactly when this predicate is false.
static bool GameplayFrozen( const mpMatchSession &session ) {
	return session.GetPause().state != MP_MATCH_PAUSE_RUNNING;
}

static int StepFrame( mpMatchSession &session ) {
	const mpMatchPauseState_t before = session.GetPause().state;
	const int64_t beforeMatchMsec = session.GetMatchTime().Milliseconds();
	engineMsec += FRAME_MSEC;
	CHECK( !session.AdvanceFrame(
		mpMatchEngineTime::FromMilliseconds( engineMsec ) ).WasRejected() );

	const bool frozen = GameplayFrozen( session );
	const int64_t accrued = session.GetMatchTime().Milliseconds() - beforeMatchMsec;
	CHECK( accrued == ( frozen ? 0 : static_cast<int64_t>( FRAME_MSEC ) ) );
	if ( !frozen ) {
		++simulatedFrames;
	}
	if ( before != MP_MATCH_PAUSE_RUNNING && !frozen ) {
		++resumeCompletions;
	}
	return 0;
}

static int ConfigureLobby( mpMatchSession &session, uint64_t sessionId,
		bool allowTimeoutDuringCountdown ) {
	CHECK( session.Reset( sessionId, mpMatchEngineTime::FromMilliseconds( 0 ) ) );
	CHECK( AppliedOnce( session.FreezeRules( 1, 0x1234,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( WARMUP,
		MP_MATCH_TRANSITION_SESSION_INITIALIZED, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.ConfigureRegulationPeriod( 600000,
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.ConfigureTimeouts( 1, TIMEOUT_MSEC,
		allowTimeoutDuringCountdown, RESUME_COUNTDOWN_MSEC,
		MP_MATCH_RESUME_OWNER_OR_AUTHORITY, session.GetSessionRevision() ) ) );
	return 0;
}

int main() {
	mpMatchSession session;
	const int lobbyFailure = ConfigureLobby( session, 7, false );
	if ( lobbyFailure != 0 ) {
		return lobbyFailure;
	}
	CHECK( AppliedOnce( session.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( AppliedOnce( session.TransitionPhase( GAMEON,
		MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE, mpParticipantId::Invalid(),
		session.GetSessionRevision() ) ) );
	CHECK( session.GetMatchTime().Milliseconds() == 0 );

	// Every pause/resume cycle must leave the match clock equal to the number of
	// frames the adapter actually simulated.  Accruing before the pause overlay
	// transition dropped the resume-completing frame from the match clock, which
	// is how a timed match reached its live-period deadline late and lost its
	// configured overtime.
	for ( int cycle = 0; cycle < 4; ++cycle ) {
		for ( int frame = 0; frame < 5; ++frame ) {
			STEP( session );
		}
		CHECK( AppliedOnce( session.RequestTechnicalPause(
			MP_MATCH_PAUSE_REASON_TECHNICAL_FAULT, session.GetSessionRevision() ) ) );
		STEP( session );
		CHECK( session.GetPause().state == MP_MATCH_PAUSED );
		for ( int frame = 0; frame < 3; ++frame ) {
			STEP( session );
		}
		CHECK( AppliedOnce( session.RequestResumeByAuthority(
			session.GetSessionRevision() ) ) );
		CHECK( session.GetPause().state == MP_MATCH_RESUME_COUNTDOWN );
		while ( session.GetPause().state != MP_MATCH_PAUSE_RUNNING ) {
			STEP( session );
		}
	}
	CHECK( resumeCompletions == 4 );
	CHECK( session.GetMatchTime().Milliseconds() ==
		static_cast<int64_t>( simulatedFrames ) * FRAME_MSEC );

	// The automatic team-timeout expiry walks the same overlay: PAUSED becomes
	// RESUME_COUNTDOWN on one boundary and RUNNING on a later one.
	CHECK( AppliedOnce( session.RequestTeamTimeout( 0,
		MP_MATCH_PAUSE_REASON_TACTICAL, session.GetSessionRevision() ) ) );
	STEP( session );
	CHECK( session.GetPause().state == MP_MATCH_PAUSED );
	CHECK( session.GetTimeoutBudget( 0 ).remaining == 0 );
	while ( session.GetPause().state != MP_MATCH_PAUSE_RUNNING ) {
		STEP( session );
	}
	CHECK( resumeCompletions == 5 );
	CHECK( session.GetMatchTime().Milliseconds() ==
		static_cast<int64_t>( simulatedFrames ) * FRAME_MSEC );
	CHECK( simulatedFrames > 0 );
	CHECK( session.ValidateInvariants() );

	// The timeout_request_window rule is the session's decision.  With the
	// countdown window closed the session itself refuses with WRONG_PHASE; the
	// protocol descriptor must not pre-empt that verdict.
	mpMatchSession countdown;
	const int closedFailure = ConfigureLobby( countdown, 8, false );
	if ( closedFailure != 0 ) {
		return closedFailure;
	}
	CHECK( AppliedOnce( countdown.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		countdown.GetSessionRevision() ) ) );
	CHECK( !countdown.IsTimeoutAllowedDuringCountdown() );
	const uint64_t closedRevision = countdown.GetSessionRevision();
	CHECK( countdown.RequestTeamTimeout( 0, MP_MATCH_PAUSE_REASON_TACTICAL,
		closedRevision ).reason == MP_MATCH_REASON_WRONG_PHASE );
	CHECK( countdown.GetSessionRevision() == closedRevision );
	CHECK( countdown.GetPause().state == MP_MATCH_PAUSE_RUNNING );

	mpMatchSession openWindow;
	const int openFailure = ConfigureLobby( openWindow, 9, true );
	if ( openFailure != 0 ) {
		return openFailure;
	}
	CHECK( AppliedOnce( openWindow.TransitionPhase( COUNTDOWN,
		MP_MATCH_TRANSITION_READY_GATE, mpParticipantId::Invalid(),
		openWindow.GetSessionRevision() ) ) );
	CHECK( openWindow.IsTimeoutAllowedDuringCountdown() );
	CHECK( AppliedOnce( openWindow.RequestTeamTimeout( 0,
		MP_MATCH_PAUSE_REASON_TACTICAL, openWindow.GetSessionRevision() ) ) );
	CHECK( openWindow.GetPause().state == MP_MATCH_PAUSE_PENDING );
	CHECK( openWindow.ValidateInvariants() );
	return 0;
}
'''


def compile_match_clock_harness() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_pause_contract: match-clock executable checks skipped")
        return

    tmp_root = ROOT / ".tmp"
    tmp_root.mkdir(exist_ok=True)
    # The Windows ARM64 runners refuse to delete the just-run harness executable
    # (WinError 5) after the checks pass; a leftover file in .tmp must not turn
    # that pass into a failure.
    with tempfile.TemporaryDirectory(prefix="mp-match-clock-", dir=tmp_root, ignore_cleanup_errors=True) as raw:
        temp = Path(raw)
        source = temp / "match_clock_harness.cpp"
        source.write_text(MATCH_CLOCK_HARNESS.lstrip(), encoding="utf-8", newline="\n")
        executable = temp / (
            "match_clock_harness.exe" if compiler.lower().endswith(".exe") else
            "match_clock_harness"
        )
        command = [
            compiler,
            "-std=c++11",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(source),
            str(ROOT / "src/mpgame/mp/match/MatchSession.cpp"),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone match-clock contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                "match-clock contract failed at harness line "
                f"{ran.returncode}:\n" + ran.stdout + ran.stderr
            )


def main() -> None:
    central_frame_contracts()
    owned_deadline_contracts()
    compile_deadline_harness()
    compile_prediction_clock_harness()
    compile_match_clock_harness()
    print("mp_match_pause_contract: ok")


if __name__ == "__main__":
    main()
