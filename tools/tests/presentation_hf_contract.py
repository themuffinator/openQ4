#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this inspects; see docs/dev/game-test-coverage.md.
"""Static contract for the HF-03 to HF-07 presentation owners.

Late look (in_presentationView), weapon view lights, unbound client debris and
alternate views are draw-only. These checks pin the read-only engine sampler,
the frozen per-draw correction, the identical SP/MP bodies and the gameplay
state each owner must never write, and the HF-06 guard that makes event
dispatch, scripts, physics and effect spawns report themselves when they run
inside a prepared draw, and HF-07's declared frame-rate policy (hidden-window,
loading-screen and preset caps). Behaviour of the look policy itself is
executed by openq4-presentation-look (tools/tests/native/PresentationLookTest.cpp).
Run with --mutations to prove a representative check of each owner fails when
its production body regresses.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TREES = ("src/game", "src/mpgame")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="surrogateescape").replace("\r\n", "\n")


def function(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"Unterminated {signature!r} in {context}")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def reject_pattern(haystack: str, pattern: str, context: str) -> None:
    match = re.search(pattern, haystack)
    if match:
        raise AssertionError(f"Unexpected write {match.group(0)!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    a, b = haystack.find(first), haystack.find(second)
    if a < 0 or b < 0 or a >= b:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def normalized(body: str) -> str:
    return " ".join(body.split())


def check_engine(usercmd_h: str, usercmd_cpp: str, game_h: str, common_cpp: str) -> None:
    context = "engine late-look sampler"
    require(usercmd_h, "virtual bool\t\tGetPresentationViewAngles( float &yaw, float &pitch ) = 0;", context)
    reject(usercmd_h + usercmd_cpp, "GetPresentationViewDelta", f"{context}: the pending-delta API could double-count buffered usercmds")

    sampler = function(usercmd_cpp, "bool idUsercmdGenLocal::GetPresentationViewAngles( float &yaw, float &pitch )", context)
    for token in (
        "in_presentationView.GetBool()",
        "Inhibited()",
        "m_smooth.GetInteger() > 1 || GetMouseFilterSamples() > 0",
        "sessLocal.cmdDemoFile != NULL || sessLocal.readDemo != NULL",
        "vrSystem->IsActive()",
        "BUTTON_WEAPONWHEEL | BUTTON_QUICKCHAT",
        "Sys_EnterCriticalSection();",
        "Mouse();",
        "const idVec3 builtAngles = viewangles;",
        "Sys_LeaveCriticalSection();",
        "NormalizeMouseDeltasForCpi( mx, my, strafeMx, strafeMy )",
        "CalculateMouseSensitivity( mx, my, NULL, NULL ) * GetZoomLookSensitivityScale()",
        "openq4::PresentationRateShare( yawRate, elapsedSeconds, tickSeconds )",
    ):
        require(sampler, token, context)
    require_before(sampler, "Sys_EnterCriticalSection();", "Mouse();", f"{context}: drained under the async lock")
    require_before(sampler, "Mouse();", "Sys_LeaveCriticalSection();", f"{context}: drained under the async lock")
    # Read-only: the next usercmd must still receive all of the input.
    for pattern, what in (
        (r"\bviewangles\s*\[[^\]]+\]\s*[-+*/]?=(?!=)", "generator view angles"),
        (r"\bviewangles\s*=(?!=)", "generator view angles"),
        (r"\bmouseD[xy]\s*[-+*/]?=(?!=)", "pending mouse motion"),
        (r"\bcmd\.\w+(\[[^\]]+\])?\s*[-+*/|&]?=(?!=)", "the usercmd being built"),
        (r"\bbuffered\s*\[", "buffered usercmds"),
        (r"\blastBuildMsec\s*=(?!=)", "the build stamp"),
    ):
        reject_pattern(sampler, pattern, f"{context} must not write {what}")

    rate = function(usercmd_cpp, "bool idUsercmdGenLocal::PresentationLookRate( float &yawRate, float &pitchRate )", context)
    for token in ("in_yawSpeed.GetFloat()", "in_pitchSpeed.GetFloat()", "in_angleSpeedKey.GetFloat()",
                  "Sys_GetJoystickAxisState( axis, value )", "retainedAxisBlocked[axis]",
                  "in_joystickInvertLook", "ScoreboardScrolls()", "in_joystickLookSensitivity",
                  "GetZoomLookSensitivityScale()"):
        require(rate, token, f"{context} held-rate mirror")
    reject_pattern(rate, r"\b(viewangles|joystickAxis|scoreboardStick\w*)\s*(\[[^\]]+\])?\s*[-+*/]?=(?!=)", f"{context} held-rate read-only")
    reject(rate, "ScrollScoreboard", f"{context} held-rate read-only")

    make_current = function(usercmd_cpp, "void idUsercmdGenLocal::MakeCurrent( void )", context)
    require(make_current, "lastBuildMsec = Sys_GetClockTicks() * 1000.0 / Sys_ClockTicksPerSecond();", f"{context} build stamp")

    probe = function(usercmd_cpp, "void idUsercmdGenLocal::MouseProbe( void )", "mouse probe")
    require(probe, "mouseDx += counts;", "mouse probe enters the ordinary mouse path")
    reject(probe, "continuousMouseX", "mouse probe never moves a GUI cursor")
    require(usercmd_cpp, 'in_presentationViewProbe( "in_presentationViewProbe", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_FLOAT', "mouse probe is a cheat-gated dev cvar")
    require(usercmd_cpp, 'in_presentationViewProbeZoom( "in_presentationViewProbeZoom", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_BOOL', "zoom probe is a cheat-gated dev cvar")
    buttons = function(usercmd_cpp, "void idUsercmdGenLocal::CmdButtons( void )", "zoom probe")
    require(normalized(buttons), "if ( in_presentationViewProbeZoom.GetBool() ) { cmd.buttons |= BUTTON_ZOOM; }", "zoom probe only holds the zoom button")
    # HF-08 review: firing under late look without input
    require(usercmd_cpp, 'in_presentationViewProbeAttack( "in_presentationViewProbeAttack", "0", CVAR_SYSTEM | CVAR_CHEAT | CVAR_BOOL', "fire probe is a cheat-gated dev cvar")
    require(normalized(buttons), "if ( in_presentationViewProbeAttack.GetBool() ) { cmd.buttons |= BUTTON_ATTACK; }", "fire probe only holds the attack button")
    require(usercmd_cpp, 'in_presentationView( "in_presentationView", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL', "late look stays opt-in")

    check_frame_rate_policy()
    check_present_timing()

    # HF-08 physical latency: the attack press, as the event arrives, flashes the
    # next drawn frame; the overlay takes it every frame so no press goes stale.
    set_button = function(usercmd_cpp, "void idUsercmdGenLocal::SetButtonAction( int action, bool down )", "latency flash")
    require_before(set_button, "if ( action == UB_ATTACK && buttonState[ action ] == 0 ) {", "buttonState[ action ]++;", "latency flash arms on the press edge")
    console = read("src/framework/Console.cpp")
    require(console, 'com_latencyFlash( "com_latencyFlash", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT', "latency flash is opt-in")
    require(console, "if ( Usercmd_ConsumeLatencyFlash() && com_latencyFlash.GetBool() ) {", "latency flash consumed every frame, drawn when enabled")
    require(console, '"latencyFlashTest", Con_LatencyFlashTest_f, CMD_FL_SYSTEM | CMD_FL_CHEAT', "latency flash test is cheat-gated")

    require(game_h, "const int GAME_API_VERSION\t\t= 58;", "game ABI v58")
    require(game_h, "idUsercmdGen *\t\t\t\tusercmdGen;", "gameImport_t carries the generator")
    require(common_cpp, "gameImport.usercmdGen\t\t\t\t= ::usercmdGen;", "engine hands the generator to the game")


def check_frame_rate_policy(common: str | None = None) -> None:
    """HF-07's declared frame-rate policy (presentation-owners.md)."""
    common = read("src/framework/Common.cpp") if common is None else common
    context = "HF-07 frame-rate policy"
    cap = function(common, "static int Common_GetRequestedPresentationCap( void )", context)
    require(cap, "vrSystem->IsPacing()", f"{context}: a headset paces itself")
    require(cap, "openq4::PresentationCapForWindow( com_maxfps.GetInteger(), Common_IsGameWindowShown() )", f"{context}: hidden and minimized windows capped at 60")
    pacing = read("src/framework/PresentationPacing.h")
    require(pacing, "constexpr int HIDDEN_WINDOW_PRESENTATION_CAP = 60;", f"{context}: the hidden-window cap is 60")
    require(common, 'com_presentationStallProbe( "com_presentationStallProbe", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_CHEAT,', f"{context}: the hitch probe is cheat-gated")
    throttle = function(common, "static void Common_ThrottlePresentationFrame( void )", context)
    require(throttle, "commonThrottleLatenessMsec[ commonThrottleLatenessNext ] =", f"{context}: each wait records how late it woke")
    require_before(throttle, "if ( waited ) {", "commonThrottleLatenessMsec[ commonThrottleLatenessNext ] =",
                   f"{context}: only frames that waited count as late wakes (an overrun is not the limiter's lateness)")
    # The bulk of a wait sleeps on SDL's high-resolution timer and the spin covers the
    # measured oversleep (PresentationPacing.h); com_preciseFrameWait 0 is the rollback.
    require(common, 'com_preciseFrameWait( "com_preciseFrameWait", "1", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT,', f"{context}: the precise wait has a rollback")
    require(throttle, "openq4::LimiterSpinMsec( commonLimiterOversleep )", f"{context}: the spin covers the measured oversleep")
    require(throttle, "openq4::LegacyLimiterSpinMsec( frameMsec )", f"{context}: the legacy wait keeps its fixed spin")
    require(throttle, "commonLimiterOversleep.Add( sleptMsec - sleepMsec );", f"{context}: every precise sleep is measured")
    require(throttle, "spinNowClock >= waitDeadlineClock", f"{context}: the spin keeps its deadline")
    sleep = function(common, "static void Common_PreciseSleep( double msec )", context)
    require(sleep, "SDL_DelayNS(", f"{context}: precise sleeps use SDL's high-resolution delay")
    shown = function(common, "static bool Common_IsGameWindowShown( void )", context)
    require(shown, "return Sys_SDL_IsGameWindowShown();", f"{context}: the window state comes from SDL on every platform")
    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    shown_sdl = function(backend, "bool Sys_SDL_IsGameWindowShown(void)", context)
    require(shown_sdl, "SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED", f"{context}: hidden and minimized windows show nothing")
    require(backend, '"windowMinimizeTest", SDL3_WindowMinimizeTest_f, CMD_FL_SYSTEM | CMD_FL_CHEAT,', f"{context}: the minimize test is cheat-gated")
    win = read("src/sys/win32/win_main.cpp")
    init = function(win, "void Sys_Init(void)", context)
    require_before(init, "timeBeginPeriod(1);", "Sys_KeepTimerResolutionWhenOccluded();", f"{context}: the 1 ms timer survives occlusion")
    keep = function(win, "static void Sys_KeepTimerResolutionWhenOccluded(void)", context)
    require(keep, "state.ControlMask = ignoreTimerResolution;", f"{context}: the opt-out names timer resolution")
    require(keep, "state.StateMask = 0;", f"{context}: timer resolution is never ignored")
    require(keep, "win_keepTimerResolution.SetBool(true);", f"{context}: the result outlives the pre-log Sys_Init")
    # pacing statistics at 50 us on the limiter's own frame-start stamp
    require(pacing, "constexpr int FRAME_PACING_BIN_USEC = 50;", f"{context}: 50 us interval bins")
    update = function(read("src/framework/Session.cpp"), "void idSessionLocal::UpdateFramePacingStats( int requestedWaitMsec, int actualWaitMsec, int gameTicsToRun )", context)
    require(update, "openQ4_GetPresentationFrameStartMsec()", f"{context}: intervals use the limiter's high-resolution stamp")
    require(update, "frameIntervalBins[ openq4::FramePacingBin( frameDeltaMsec ) ]", f"{context}: intervals are binned finely")
    begin = function(common, "void openQ4_BeginPresentationFrame( void )", context)
    require_before(begin, "Common_ThrottlePresentationFrame();", "commonPresentationFrameStartClock = Common_GetPresentationClockNow();", f"{context}: the stamp follows the wait")
    # the engine's 240 Hz budget is the one the runtime harness judges
    harness = read("tools/tests/presentation_look_runtime.py")
    budgets = re.search(r'BUDGETS = \{240: \{"intervalP95": ([\d.]+), "intervalP99": ([\d.]+)', harness)
    if budgets is None:
        raise AssertionError(f"{context}: the harness declares no 240 Hz budget")
    require(pacing, f"constexpr float SUPPORTED_CAP_P95_BUDGET_MSEC = {budgets[1]}f;", f"{context}: p95 budget agrees with the harness")
    require(pacing, f"constexpr float SUPPORTED_CAP_P99_BUDGET_MSEC = {budgets[2]}f;", f"{context}: p99 budget agrees with the harness")
    session = read("src/framework/Session.cpp")
    require(session, "openQ4_PrintPresentationThrottleStats(", f"{context}: framePacingSnapshot reports the limiter's lateness")
    require(common, "late p50=%.3f p95=%.3f p99=%.3f max=%.3f ms, spin mean=%.3f ms", f"{context}: and how long it busy-waits")
    require(session, "openQ4_PrintPresentationLatencyStats(", f"{context}: framePacingSnapshot reports input to swap (HF-08)")
    require(session, "openQ4_ResetPresentationLatencyStats();", f"{context}: framePacingReset clears it")
    renderer = read("src/renderer/RenderSystem.cpp")
    end = function(renderer, "void idRenderSystemLocal::EndFrame(", context)
    require_before(end, "Usercmd_TakeFrameInputClocks(", "R_IssueRenderCommandsOnRenderThread()", f"{context}: freeze input clocks before handoff")
    require_before(end, "if ( r_hiddenWindow.GetBool() )", "frameLookSampleClock = frameFlashPressClock = 0.0;", f"{context}: hidden-window receipts are not latency evidence")
    complete = function(renderer, "void idRenderSystemLocal::CompleteBackEndFrame(", context)
    require(complete, "Common_RecordFrameInputToSwap( frameLookSampleClock, frameFlashPressClock, frameSwapClock );", f"{context}: latency uses the completed frame's swap receipt")
    swap = function(renderer, "void R_SwapBuffersWithInputTiming(", context)
    require_before(swap, "GLimp_SwapBuffers();", "sys->GetClockTicks()", f"{context}: stamp after swap, not at handoff")
    require(swap, "afterSwap.presentedSequence > beforeSwap.presentedSequence", f"{context}: failed/withheld frames are not latency samples")
    require(session, "openQ4_ResetPresentationThrottleStats();", f"{context}: framePacingReset clears it")
    frame = function(common, "void idCommonLocal::Frame( void )", context)
    require_before(frame, "Common_PresentationStallProbe();", "Sys_GenerateEvents();", f"{context}: the hitch lands in an ordinary frame")
    require(common, 'com_maxfps( "com_maxfps", "240"', f"{context}: 240 default cap")
    session = read("src/framework/Session.cpp")
    require(session, "static const float SESSION_MAX_BLOCKING_LOAD_REDRAW_HZ = 60.0f;", f"{context}: loading redraw ceiling")
    load = function(session, "static float Session_GetBlockingLoadFrameIntervalMsec( void )", context)
    require(load, "return Max( ceilingMsec, 1000.0f / static_cast<float>( presentationCap ) );", f"{context}: loading honours a lower cap only")
    require(load, "return Max( ceilingMsec, common->GetUserCmdMsecFloat() );", f"{context}: uncapped loading at most once per tick")
    presets = read("src/framework/PerformancePreset.cpp")
    caps = {}
    for name, fps in re.findall(r'\{ "(\w+)", \d+, "[\w-]+",\s*\d+, \d+, \d+, (\d+),', presets):
        caps[name] = int(fps)
    expected = {"minimum": 30, "lowpower": 30, "low": 60, "performance": 60, "balanced": 120, "enhanced": 144, "quality": 144, "ultra": 240, "maximum": 240}
    if caps != expected:
        raise AssertionError(f"{context}: preset caps {caps} differ from the declared {expected}")
    require(presets, 'if(signals.steamDeck&&tier>Performance){tier=Performance;', f"{context}: Steam Deck is capped at performance without raising a lower tier")
    require(presets, 'if(!powerLimited||signals.displayRefreshHz<result.maxFps)result.maxFps=signals.displayRefreshHz;',
            f"{context}: a detected start follows the display, and a power-limited device never above its tier's cap")


def check_present_timing(device: str | None = None) -> None:
    """HF-07 presented-frame timing (VK_KHR_present_wait): opt-in, and the waiter never
    touches a retired or destroyed swapchain."""
    device = read("src/renderer/Vulkan/VulkanDevice.cpp") if device is None else device
    context = "HF-07 presented-frame timing"
    require(device, 'r_vkPresentTiming( "r_vkPresentTiming", "0", CVAR_RENDERER | CVAR_BOOL,', f"{context}: off by default")
    create = function(device, "static bool VK_Device_CreateSwapchain( void )", context)
    require_before(create, "VK_PresentTiming_Quiesce();", "vkCreateSwapchainKHR(", f"{context}: the waiter lets go before oldSwapchain retires")
    require_before(create, "vkCtx.swapchain = newSwapchain;", "VK_PresentTiming_Resume( newSwapchain );", f"{context}: it resumes on the new swapchain")
    destroy = function(device, "static void VK_Device_DestroySwapchainObjects( void )", context)
    if destroy.strip().splitlines()[0].strip() != "VK_PresentTiming_Quiesce();":
        raise AssertionError(f"{context}: destroying the swapchain must quiesce the waiter first")
    shutdown = function(device, "void VK_Device_Shutdown( void )", context)
    require_before(shutdown, "VK_PresentTiming_Stop();", "vkDeviceWaitIdle( vkCtx.device );", f"{context}: the waiter stops before the device goes")
    waiter = function(device, "static void VK_PresentTiming_Waiter( void )", context)
    require(waiter, "vkWaitForPresentKHR( device, swapchain, pending.id, VK_PRESENT_TIMING_WAIT_NS )", f"{context}: bounded waits")
    require_before(waiter, "lock.unlock();", "vkWaitForPresentKHR(", f"{context}: the wait runs outside the lock")
    present = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(present, "const uint64_t presentId = VK_PresentTiming_NextId();", f"{context}: each present takes an id")
    require(present, "pi.pNext = &presentIds;", f"{context}: the id is chained on the present")
    require(present, "VK_PresentTiming_Presented( presentId );", f"{context}: a queued present is handed to the waiter")
    init = read("src/renderer/RenderSystem_init.cpp")
    require(init, 'cmdSystem->AddCommand( "rendererPresentTiming", R_RendererVulkanPresentTiming_f,', f"{context}: the report command")


def check_guard(source: str, signature: str, expected: str, context: str) -> str:
    first = function(source, signature, context).strip().splitlines()[0].strip()
    if first != expected:
        raise AssertionError(f"{context}: {signature} must report itself before any work (first line {first!r})")
    return first


def check_tree(tree: str) -> dict[str, str]:
    player_h = read(f"{tree}/Player.h")
    player = read(f"{tree}/Player.cpp")
    weapon = read(f"{tree}/Weapon.cpp")
    weapon_h = read(f"{tree}/Weapon.h")
    game_local = read(f"{tree}/Game_local.cpp")
    game_local_h = read(f"{tree}/Game_local.h")
    moveable = read(f"{tree}/client/ClientMoveable.cpp")
    moveable_h = read(f"{tree}/client/ClientMoveable.h")
    effect = read(f"{tree}/client/ClientEffect.cpp")
    context = tree

    require(game_local, "usercmdGen\t\t\t\t\t= import->usercmdGen;", f"{context} imports the generator")
    require(game_local_h, "bool\t\t\t\t\tIsPresentationSceneActive( void ) const { return presentationClockSample >= 0.0f; }", f"{context} draw-scope query")
    require(game_local_h, '#include "../framework/PresentationLook.h"', f"{context} shared look policy")

    # ---- HF-03 late look ----
    look = function(player, "void idPlayer::UpdatePresentationLook( void )", context)
    require(look, "presentationLookActive = false;", f"{context} look resets every view build")
    require(look, "gameLocal.IsPresentationSceneActive()", f"{context} look only inside a prepared draw")
    require(look, "presentationLookCoupled", f"{context} look follows only a view that follows input")
    require(look, "AllowsPresentationLook()", f"{context} look eligibility")
    require(look, "usercmdGen->GetPresentationViewAngles( inputYaw, inputPitch )", f"{context} one input sample per draw")
    require(look, "sample.appliedYaw = presentationCurCmdAngles.yaw;", f"{context} compares with the applied usercmd")
    require(look, "openq4::ComputePresentationLook( sample )", f"{context} shared policy")
    require(look, "pm_minviewpitch.GetFloat()", f"{context} same pitch clamp as UpdateViewAngles")
    for pattern in (r"\bviewAngles\s*[.\[=]", r"\bdeltaViewAngles\s*[.\[=]", r"\busercmd\s*[.=]", r"\bfirstPersonView(Origin|Axis)\s*="):
        reject_pattern(look, pattern, f"{context} late look never writes authoritative view state")

    apply_view = function(player, "void idPlayer::ApplyPresentationLookToView( idVec3 &origin, idMat3 &axis ) const", context)
    require(apply_view, "!presentationLookActive || !gameLocal.IsPresentationSceneActive()", f"{context} correction scoped to the draw")
    require(apply_view, "physicsObj.GetGravityAxis()", f"{context} gravity-relative angles")
    require(apply_view, "g_viewNodalX.GetFloat()", f"{context} neck pivot")
    require(apply_view, "g_viewNodalZ.GetFloat()", f"{context} neck pivot")
    attach = function(player, "void idPlayer::ApplyPresentationLookToEyeAttachment( const idVec3 &eyeOrigin, const idMat3 &eyeAxis, idVec3 &origin, idMat3 &axis ) const", context)
    require(attach, "eyeAxis.Transpose()", f"{context} world-to-eye (see idlib mat3 product trap)")
    require(attach, "ApplyPresentationLookToView( drawnEyeOrigin, drawnEyeAxis );", f"{context} attachment uses the drawn eye")

    get_view = function(player, "void idPlayer::GetPresentationViewPos( idVec3 &origin, idMat3 &axis ) const", context)
    require(get_view.rstrip().splitlines()[-1].strip(), "ApplyPresentationLookToView( origin, axis );", f"{context} look applied after interpolation")

    render_view = function(player, "void idPlayer::CalculateRenderView( void )", context)
    require_before(render_view, "UpdatePresentationViewState();", "UpdatePresentationLook();", f"{context} look uses this draw's samples")
    require_before(render_view, "UpdatePresentationLook();", "GetPresentationViewPos( renderView->vieworg, renderView->viewaxis );", f"{context} look frozen before the camera")

    update_state = function(player, "void idPlayer::UpdatePresentationViewState( void )", context)
    require(update_state, "SHORT2ANGLE( usercmd.angles[0] )", f"{context} samples the applied usercmd")
    require(update_state, "openq4::PresentationLookCoupled(", f"{context} coupling evidence")
    require(update_state, "presentationPrevCmdAngles = presentationCurCmdAngles;", f"{context} look endpoints collapse with the camera")

    allows = function(player, "bool idPlayer::AllowsPresentationLook( void ) const", context)
    for token in ("gameLocal.GetLocalPlayer() != this", "health <= 0", "weaponWheelActive", "IsInVehicle()",
                  "privateCameraView != NULL", "gameLocal.GetCamera() != NULL", "influenceActive >= INFLUENCE_LEVEL2",
                  "centerView.IsDone( gameLocal.time )", "pm_thirdPerson.GetBool()", "pm_modelView.GetInteger() == 1",
                  "gameLocal.GetDemoState() != DEMO_NONE", "gameLocal.IsTimeDemo()", "vrSystem->IsActive()"):
        require(allows, token, f"{context} late-look eligibility")
    if tree == "src/game":
        require(allows, "viewSweepActive", f"{context} benchmark sweep owns the view")
        require(allows, "g_stopTime.GetBool()", f"{context} stop-time captures stay deterministic")
    else:
        require(allows, "ArenaCampaignLocksPlayers()", f"{context} Arena freeze owns the view")
        require(allows, "BUTTON_WEAPONWHEEL | BUTTON_QUICKCHAT", f"{context} radial selectors own the look input")

    save = function(player, "void idPlayer::Save( idSaveGame *savefile ) const", context)
    for field in ("presentationLook", "presentationPrevLookAngles", "presentationCurCmdAngles", "presentationAltView"):
        reject(save, field, f"{context} presentation state stays out of saves")
    for field in ("presentationLookCorrection;", "presentationLookCoupled;", "presentationAltViewDrawn;"):
        require(player_h, field, f"{context} transient presentation members")

    # Only the presentation functions may read the correction.
    allowed = {"UpdatePresentationLook", "ApplyPresentationLookToView", "ResetPresentationViewState",
               "GetPresentationLookDiagnostic"}
    for match in re.finditer(r"presentationLookCorrection", player):
        owner = re.findall(r"\n\w[^\n]*idPlayer::(\w+)\s*\(", player[:match.start()])
        if not owner or owner[-1] not in allowed:
            raise AssertionError(f"{context}: presentationLookCorrection used outside presentation functions ({owner[-1] if owner else '?'})")

    viewmodel = function(weapon, "void rvWeapon::GetPresentationViewModelTransform( idVec3 &origin, idMat3 &axis ) const", context)
    require(viewmodel, "owner->ApplyPresentationLookToEyeAttachment( playerViewOrigin, playerViewAxis, origin, axis );", f"{context} gun stays in front of a turned eye")
    require(viewmodel, "owner->GetPresentationViewPos( playerOrigin, playerAxis );", f"{context} interpolated gun follows the turned eye")

    # ---- HF-04 weapon view lights ----
    effects = function(weapon, "void rvWeapon::UpdatePresentationEffects( void )", context)
    require(effects, "lightHandles[ WPLIGHT_MUZZLEFLASH ] != -1", f"{context} only a live muzzle flash is re-placed")
    require(effects, "lightHandles[ WPLIGHT_FLASHLIGHT ] != -1", f"{context} only a live flashlight is re-placed")
    require(effects, "GetPresentationEye( eyeOrigin, eyeAxis );", f"{context} muzzle light from the drawn eye")
    require(effects, "light.origin += presentationFlashlightBackoff;", f"{context} carried wall back-off")
    require(effects, "owner->GetVRWeaponTransform( vrOrigin, vrAxis, true )", f"{context} VR hand keeps its placement")
    for token in ("TracePoint", "FreeLight", "AddLightDef", "muzzleFlashEnd =", "MuzzleFlash (", "StartSound", "PlayEffect"):
        reject(effects, token, f"{context} draw frames never trace, create, free or re-time weapon lights")
    flashlight = function(weapon, "void rvWeapon::UpdateFlashlight ( void )", context)
    require(flashlight, "presentationFlashlightBackoff = light.origin - jointOrigin;", f"{context} Think records the back-off")
    require(weapon_h, "presentationFlashlightBackoff;", f"{context} transient back-off")
    weapon_save = function(weapon, "void rvWeapon::Save ( idSaveGame *savefile ) const", context)
    reject(weapon_save, "presentationFlashlightBackoff", f"{context} back-off stays out of saves")

    # ---- HF-04 unbound client debris, brass and gibs ----
    think = function(moveable, "void rvClientMoveable::Think ( void )", context)
    require_before(think, "gameRenderWorld->UpdateEntityDef( entityDefHandle, &renderEntity );", "SamplePresentationPose();", f"{context} sample after the authoritative push")
    sample = function(moveable, "void rvClientMoveable::SamplePresentationPose( void )", context)
    for token in ("bindMaster", "g_presentationInterpolation.GetBool()", "presentationPoseTime == gameLocal.time",
                  "common->GetUserCmdMsecFloat()", "gameLocal.GetMHz() == common->GetUserCmdHz()",
                  "CLIENT_PRESENTATION_STEP_TOLERANCE", "presentationNode.AddToEnd( gameLocal.presentationClientEntities );",
                  "presentationNode.Remove();"):
        require(sample, token, f"{context} debris sampling")
    draw = function(moveable, "void rvClientMoveable::UpdatePresentationPose( void )", context)
    for token in ("renderEntity_t presentationRenderEntity = renderEntity;", "gameLocal.GetPresentationInterpolationFraction()",
                  "gameLocal.InterpolatePresentationAxis(", "scale.GetCurrentValue( gameLocal.GetTime() )",
                  "gameRenderWorld->UpdateEntityDef( entityDefHandle, &presentationRenderEntity );", "trailEffect->PresentAt( origin, axis );"):
        require(draw, token, f"{context} debris draw")
    for token in ("AddEntityDef", "RunPhysics", "StartSound", "PostEventMS", "SetOrigin", "worldOrigin ="):
        reject(draw, token, f"{context} debris draw is presentation-only")
    restore = function(moveable, "void rvClientMoveable::RestoreAuthoritativePresentationPose( void )", context)
    require(restore, "gameRenderWorld->UpdateEntityDef( entityDefHandle, &renderEntity );", f"{context} authoritative handback")
    moveable_save = function(moveable, "void rvClientMoveable::Save( idSaveGame *savefile ) const", context)
    reject(moveable_save, "presentation", f"{context} debris presentation stays out of saves")
    require(moveable_h, "int\t\t\t\t\t\tpresentationPoseTime;", f"{context} transient debris samples")
    present_at = function(effect, "void rvClientEffect::PresentAt ( const idVec3 &origin, const idMat3 &axis )", context)
    require(present_at, "gameRenderWorld->UpdateEffectDef( effectDefHandle, &renderEffect, gameLocal.time );", f"{context} trail keeps the authoritative effect clock")
    require(present_at, "renderEffect.origin = authoritativeOrigin;", f"{context} trail origin restored")
    require(present_at, "bindMaster", f"{context} bound effects keep their bind path")
    for token in ("FreeEffectDef", "PostEventMS", "Stop", "AddEffectDef"):
        reject(present_at, token, f"{context} trail lifetime stays in Think")

    # ---- HF-04 active articulated figures (ragdolls) ----
    anim = read(f"{tree}/anim/Anim_Blend.cpp")
    anim_h = read(f"{tree}/anim/Anim.h")
    create_frame = function(anim, "bool idAnimator::CreateFrame( int currentTime, bool force )", context)
    require(create_frame, "if ( joints != presentationJoints ) {", f"{context} presentation evaluations never shift AF history")
    require(create_frame, "this->numJoints * sizeof( presentationPrevAFJoints[0] )", f"{context} AF history sized by the member, not CreateFrame's local")
    require_before(create_frame, "presentationPrevAFTime = authoritativeFrameTime;", "lastTransformTime = currentTime;", f"{context} history keeps the replaced pose")
    reject(create_frame, "lastTransformTime >= 0 && lastTransformTime != currentTime", f"{context} ForceUpdate clears lastTransformTime every AF tic")
    create_presentation = function(anim, "bool idAnimator::CreatePresentationFrame( int currentTime, idJointMat **jointsPtr )", context)
    af_branch = create_presentation[create_presentation.find("if ( !canEvaluateAtPresentationTime ) {"):create_presentation.find("CreateFrame( currentTime, true );")]
    for token in ("SIMDProcessor->BlendJoints( previousPose, currentPose, afLerp, blendIndex, numJoints );",
                  "currentTime < authoritativeFrameTime", "presentationPrevAFTime >= 0"):
        require(af_branch, token, f"{context} AF poses blend between authoritative samples")
    for token in ("SetAFPoseJointMod", "FinishAFPose", "AFPoseJointMods[", "GetPhysics"):
        reject(af_branch, token, f"{context} AF presentation never touches the physics pose")
    require(anim_h, "presentationPrevAFJoints;\t// previous authoritative AF pose; draw-only, never serialized", f"{context} transient AF history")
    reject(function(anim, "void idAnimator::Save( idSaveGame *savefile ) const", context), "presentationPrevAF", f"{context} AF history stays out of saves")
    require(function(anim, "void idAnimator::FreeData( void )", context), "Mem_Free16( presentationPrevAFJoints );", f"{context} AF history freed with the model")

    update_sweep = function(game_local, "void idGameLocal::UpdatePresentationEntityPoses( void )", context)
    require(update_sweep, "presentationClientEntities.Next()", f"{context} debris drawn with the entity pass")
    clear_sweep = function(game_local, "void idGameLocal::ClearPresentationEntityPoses( void )", context)
    require(clear_sweep, "cent->RestoreAuthoritativePresentationPose();", f"{context} debris handed back with the entity pass")

    # ---- HF-05 alternate views ----
    alt = function(player, "void idPlayer::PresentAlternateView( int kind, const idEntity *source )", context)
    for token in ("renderView->forceUpdate", "gameLocal.GetSpawnId( source )", "g_presentationInterpolation.GetBool()",
                  "gameLocal.IsPresentationSceneActive()", "gameLocal.GetDemoState() != DEMO_NONE", "gameLocal.IsTimeDemo()",
                  "common->GetUserCmdMsecFloat()", "gameLocal.GetMHz() == common->GetUserCmdHz()",
                  "carrier->CanInterpolatePresentationPose()", "( followsBody && !AllowsPresentationInterpolation() )", "presentationAltViewDrawn = true;",
                  "gameLocal.InterpolatePresentationAxis( presentationPrevAltAxis, presentationCurAltAxis, fraction )"):
        require(alt, token, f"{context} alternate-view sampling")
    for token in ("GetViewParms", "CallFrameCommands", "Think(", "TracePoint", "Stop("):
        reject(alt, token, f"{context} alternate views never re-run camera or third-person work on a draw")
    require_before(render_view, "if ( renderView->fov_y == 0 ) {", "PresentAlternateView( altViewKind, altViewSource );", f"{context} the complete view is sampled")
    for kind in ("PRESENTATION_ALT_VIEW_PRIVATE_CAMERA", "PRESENTATION_ALT_VIEW_CAMERA", "PRESENTATION_ALT_VIEW_THIRD_PERSON",
                 "PRESENTATION_ALT_VIEW_THIRD_PERSON_VEHICLE", "PRESENTATION_ALT_VIEW_DEATH"):
        require(render_view, f"altViewKind = {kind};", f"{context} {kind} is sampled")
    interpolated = function(player, "bool idPlayer::IsPresentationViewInterpolated( void ) const", context)
    require_before(interpolated, "if ( presentationAltViewDrawn ) {", "if ( !CanInterpolatePresentationView() ) {", f"{context} interpolated alternate views admit the scene")

    # ---- HF-06 gameplay never runs inside a prepared draw ----
    note = function(game_local, "void idGameLocal::NotePresentationGameplay( const char *what )", context)
    require(note.strip().splitlines()[0], "if ( presentationClockSample < 0.0f ) {", f"{context} guard counts only inside a prepared draw")
    require(note, "presentationGameplayCount++;", f"{context} guard feeds g_presentationClockCapture")
    for token in ("Error(", "assert(", "CancelEvents", "PostEvent"):
        reject(note, token, f"{context} guard only reports")
    record = function(game_local, "void idGameLocal::RecordPresentationClockSample( void )", context)
    require_before(record, "sample.gameplay = presentationGameplayCount;", "presentationGameplayCount = 0;", f"{context} each sample reports its own breaches")
    require_before(record, "sample.bse = presentationRendererEffectCount;", "presentationRendererEffectCount = 0;", f"{context} each sample reports its own BSE starts")
    require(record, "gameplay=%d bse=%d", f"{context} capture prints both counts")
    guarded = (
        (read(f"{tree}/gamesys/Event.cpp"), "void idEvent::ServiceEvents( void )", 'gameLocal.NotePresentationGameplay( "idEvent::ServiceEvents" );'),
        (read(f"{tree}/script/Script_Thread.cpp"), "bool idThread::Execute( void )", 'gameLocal.NotePresentationGameplay( "idThread::Execute" );'),
        (read(f"{tree}/Entity.cpp"), "bool idEntity::RunPhysics( void )", 'gameLocal.NotePresentationGameplay( "idEntity::RunPhysics" );'),
        (game_local, "rvClientEffect* idGameLocal::PlayEffect", "gameLocal.NotePresentationEffect();"),
    )
    guards = [check_guard(source, signature, first, context) for source, signature, first in guarded]
    # BSE services effects inside RenderScene; a particle impact or nested
    # segment starts its effect there once per particle or effect step. Only
    # those are counted apart, and only inside the renderer's scene work.
    effect = function(game_local, "void idGameLocal::NotePresentationEffect( void )", context)
    require(effect.strip().splitlines()[0], "if ( presentationClockSample < 0.0f ) {", f"{context} effect guard only inside a prepared draw")
    require_before(effect, "if ( presentationRendererDepth > 0 ) {", "presentationRendererEffectCount++;", f"{context} BSE starts counted apart")
    require(effect.strip().splitlines()[-1].strip(), 'NotePresentationGameplay( "idGameLocal::PlayEffect" );', f"{context} any other effect start is gameplay")
    require(game_local_h, "idPresentationRendererScope( void ) { gameLocal.presentationRendererDepth++; }", f"{context} renderer scope")
    require(game_local_h, "~idPresentationRendererScope( void ) { gameLocal.presentationRendererDepth--; }", f"{context} renderer scope unwinds")
    if tree == "src/game":
        render = read(f"{tree}/Game_render.cpp")
        for signature in ("static void openQ4_RenderSceneDirect( const renderView_t *view, idRenderWorld *renderWorld, idCamera *portalSky, int renderFlags )",
                          "static void openQ4_RenderSceneWorld( const renderView_t *view, idRenderWorld *renderWorld, idCamera *portalSky, int renderFlags )"):
            body = function(render, signature, context)
            require_before(body, "idPresentationRendererScope rendererScope;", "RenderScene(", f"{context} scene work scoped")
    else:
        single = function(read(f"{tree}/PlayerView.cpp"), "void idPlayerView::SingleView( idUserInterface *hud, const renderView_t *view, int renderFlags )", context)
        require_before(single, "idPresentationRendererScope rendererScope;", "RenderScene(", f"{context} scene work scoped")

    return {
        "look": normalized(look),
        "apply_view": normalized(apply_view),
        "attach": normalized(attach),
        "get_view": normalized(get_view),
        "viewmodel": normalized(viewmodel),
        "effects": normalized(effects),
        "eye": normalized(function(weapon, "void rvWeapon::GetPresentationEye( idVec3 &origin, idMat3 &axis ) const", context)),
        "sample": normalized(sample),
        "draw": normalized(draw),
        "restore": normalized(restore),
        "present_at": normalized(present_at),
        "update_sweep": normalized(update_sweep),
        "clear_sweep": normalized(clear_sweep),
        "alt": normalized(alt),
        "af_branch": normalized(af_branch),
        "interpolated": normalized(interpolated),
        "note": normalized(note),
        "effect": normalized(effect),
        "guards": " ".join(guards),
    }


def run() -> None:
    check_engine(read("src/framework/UsercmdGen.h"), read("src/framework/UsercmdGen.cpp"),
                 read("src/game/Game.h"), read("src/framework/Common.cpp"))
    sp, mp = (check_tree(tree) for tree in TREES)
    for name, body in sp.items():
        if body != mp[name]:
            raise AssertionError(f"SP/MP presentation drift: {name}")


def mutations() -> None:
    """Each mutation must make the contract fail."""
    usercmd_cpp = read("src/framework/UsercmdGen.cpp")
    cases = {
        "sampler consumes mouse motion": usercmd_cpp.replace(
            "\tconst idVec3 builtAngles = viewangles;\n", "\tconst idVec3 builtAngles = viewangles;\n\tmouseDx = 0;\n", 1),
        "sampler skips the filter guard": usercmd_cpp.replace(
            "m_smooth.GetInteger() > 1 || GetMouseFilterSamples() > 0", "false", 1),
        "held rates scroll the scoreboard": usercmd_cpp.replace(
            "\t\tlookAxisY = 0.0f;\n\t}\n\tfloat joystickLookSensitivity", "\t\tScrollScoreboard( 1 );\n\t}\n\tfloat joystickLookSensitivity", 1),
    }
    for name, mutated in cases.items():
        if mutated == usercmd_cpp:
            raise AssertionError(f"mutation did not apply: {name}")
        try:
            check_engine(read("src/framework/UsercmdGen.h"), mutated, read("src/game/Game.h"), read("src/framework/Common.cpp"))
        except AssertionError:
            continue
        raise AssertionError(f"mutation survived: {name}")
    player = read("src/game/Player.cpp")
    mutated = player.replace("\tpresentationLookActive = true;\n}", "\tpresentationLookActive = true;\n\tviewAngles.yaw += presentationLookCorrection.yaw;\n}", 1)
    if mutated == player:
        raise AssertionError("mutation did not apply: late look writes the view")
    look = function(mutated, "void idPlayer::UpdatePresentationLook( void )", "mutation")
    try:
        reject_pattern(look, r"\bviewAngles\s*[.\[=]", "mutation")
    except AssertionError:
        pass
    else:
        raise AssertionError("mutation survived: late look writes the view")
    event = read("src/game/gamesys/Event.cpp")
    unguarded = event.replace('\tgameLocal.NotePresentationGameplay( "idEvent::ServiceEvents" );\n', "", 1)
    if unguarded == event:
        raise AssertionError("mutation did not apply: event dispatch loses its guard")
    try:
        check_guard(unguarded, "void idEvent::ServiceEvents( void )", 'gameLocal.NotePresentationGameplay( "idEvent::ServiceEvents" );', "mutation")
    except AssertionError:
        pass
    else:
        raise AssertionError("mutation survived: event dispatch loses its guard")
    common = read("src/framework/Common.cpp")
    for name, mutated in {
        "minimized windows present uncapped": common.replace(
            "openq4::PresentationCapForWindow( com_maxfps.GetInteger(), Common_IsGameWindowShown() )",
            "openq4::PresentationCapForWindow( com_maxfps.GetInteger(), true )", 1),
        "the limiter spins a fixed slice": common.replace(
            "? openq4::LimiterSpinMsec( commonLimiterOversleep )", "? openq4::LegacyLimiterSpinMsec( frameMsec )", 1),
    }.items():
        if mutated == common:
            raise AssertionError(f"mutation did not apply: {name}")
        try:
            check_frame_rate_policy(mutated)
        except AssertionError:
            continue
        raise AssertionError(f"mutation survived: {name}")
    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    mutated = device.replace("\tVK_PresentTiming_Quiesce();\n\tconst VkResult res = vkCreateSwapchainKHR(",
                             "\tconst VkResult res = vkCreateSwapchainKHR(", 1)
    if mutated == device:
        raise AssertionError("mutation did not apply: the waiter keeps a retiring swapchain")
    try:
        check_present_timing(mutated)
    except AssertionError:
        pass
    else:
        raise AssertionError("mutation survived: the waiter keeps a retiring swapchain")
    print("presentation_hf_contract mutations: 8 rejected")


def main() -> None:
    run()
    if "--mutations" in sys.argv[1:]:
        mutations()
    print("presentation_hf_contract: ok")


if __name__ == "__main__":
    main()
