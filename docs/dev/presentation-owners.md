# High-refresh presentation owners

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Contract for HF-03 through HF-08 in the [roadmap](roadmap.md), 23 October 2026,
extended 24 and 25 October 2026.
It extends the [clock ownership contract](presentation-clock.md) (HF-01/HF-02),
which still owns the clocks themselves. Nothing here changes authoritative
simulation, usercmds, save formats, network timestamps or demo timing. Every
owner below is draw-only: it moves what is drawn between two authoritative
60 Hz samples, or toward input the game has not applied yet, and hands the
authoritative pose back before gameplay can read it.

## Late look (HF-03)

`in_presentationView 1` (default `0`) turns the drawn view with input that has
not reached the game yet. Previous-to-current interpolation deliberately draws
the eye a tick behind; mouse motion then waited for the next usercmd on top of
that. Late look removes both delays for the input-driven part of the view.

**Engine sampler.** `idUsercmdGen::GetPresentationViewAngles` (game API 54
passes the generator through `gameImport_t`) returns the angles the next
usercmd would carry if it were built now: the last built usercmd, plus mouse
and gyro motion drained under the async lock and scaled exactly as `MouseMove`
scales it (CPI, sensitivity, acceleration, zoom), plus the elapsed share of the
coming tick for held look keys and stick deflection
(`openq4::PresentationRateShare`). It writes no usercmd field, never consumes
`mouseDx/mouseDy` and never shifts the build stamp, so the next usercmd still
receives all of the input. The retired `GetPresentationViewDelta` reported only
pending mouse motion and would have dropped usercmds that were built but not
yet run (`com_asyncInput 1`); the game now compares absolute angles with the
usercmd it actually applied, which counts buffered usercmds exactly once.

The sampler returns false, and the view stays on the interpolated authoritative
path, when late look is off, input is inhibited (console, menus, retained UI,
or the native input driver owning the event pump, so the draw never drains it),
a radial selector owns the look input, a command or render demo drives the
game, or a VR headset owns the view. Android touch look arrives as a
per-usercmd delta with no continuous sample and is excluded.

**Filtered versus late look.** `m_smooth` above `1` and `m_filter` above `0`
blend several usercmds of motion. Showing raw motion ahead of that blend would
turn the view one way and then settle it another, so filtered input keeps the
authoritative view. This is a deliberate policy, not a missing feature.

**Game consumer.** `idPlayer::UpdatePresentationLook` runs once per prepared
draw, inside `CalculateRenderView` and only while
`idGameLocal::IsPresentationSceneActive()`. It freezes one correction:
`(current view + unapplied input) − interpolated view` in yaw and pitch, with
pitch clamped exactly as `UpdateViewAngles` clamps it
(`openq4::ComputePresentationLook`). `GetPresentationViewPos` applies that
correction last, in gravity-relative angles, pivoting the eye about the neck
the way `GetViewPos` places it (`g_viewNodalX/Z`). Everything held in front of
the eye follows: the interpolated view model through `GetPresentationViewPos`,
the non-interpolating view model through `ApplyPresentationLookToEyeAttachment`,
and bound effects, attached models, the GUI light, the muzzle-flash light and
the flashlight through the held view-model pose. The scope compass already uses
the drawn render view.

Late look applies only to this client's own first-person eye
(`AllowsPresentationLook`). It stands down while dead, driving a vehicle, in a
cinematic or camera view (unless noclipping), under an influence that locks the
view, during centre-view, in third person or `pm_modelView 1`, during the SP
benchmark view sweep, `g_stopTime`, demo or timedemo playback, VR, a radial
selector, or an MP Arena freeze. Any mode not named there is caught by the
coupling check: each authoritative sample records whether the view's yaw step
matched its usercmds' yaw step (`PresentationLookCoupled`); a held, scripted or
teleported view fails it, and late look stays off for that sample. More than
90° of unapplied input is a discontinuity (angle reset, map change), not motion.

Vehicles stay excluded (decision, 24 October 2026). In a vehicle, input steers
a turret, torso or body with its own turn-rate limits, and the eye rides a joint
of that vehicle. Unapplied input therefore does not tell where the next drawn
eye will point: a walker driven at 220°/s turned at 196°/s. A correction would
draw orientations the vehicle never reaches and snap back on the next tick, the
case the coupling check exists to refuse. Cockpit views stay interpolated and
exact within each tick (see the `vehicle` evidence below). Late look in vehicles
would need each vehicle's turn model evaluated on the draw, so it is not
planned.

Trams are vehicles too. In `game/tram1b` the ridden tram, `player_tram`, is a
`vehicle_tramcar_marine` driven along its track by a marine, with the player on
its rear gun. So the vehicle decision and the cockpit evidence cover trams, and
lifts carry the late-look case for movers (`lift` evidence below).

**Displayed aim versus authoritative firing.** A shot uses the usercmd that
carries the input up to its build. Late look draws the input up to the draw,
so the drawn aim never leads the input that the next usercmd will carry.
Without late look the drawn aim trails the shot's aim by up to a tick.

**Same-fire-frame alignment.** Bound muzzle effects, attached models and the
view lights follow the drawn gun. World-space shot visuals stay where the shot
left: hitscan tracers and impacts, and `launchFromBarrel` projectiles, which
spawn at the authoritative barrel and are then drawn by the generic entity
pass. On the first draw after the firing tick, their start can differ from the
drawn barrel by the rotation since that usercmd was built, at most one draw
interval of turning. Interpolation alone shows up to a whole tick of it, in the
other direction. No cosmetic retrace is performed: a draw-frame trace would be
gameplay work, and moving a tracer would misreport where the shot went.

**Never altered.** Traces, damage, projectile spawns, network events, RNG,
`viewAngles`, `deltaViewAngles`, `firstPersonViewOrigin/Axis` and the usercmd.
[`presentation_hf_contract.py`](https://github.com/themuffinator/openQ4/blob/master/tools/tests/presentation_hf_contract.py)
rejects writes to each of them from the late-look path, confines the correction
to the presentation functions and checks the SP/MP bodies are identical;
`--mutations` proves representative regressions fail it.

## Visual ownership inventory (HF-04)

| Visual | Disposition |
| --- | --- |
| View weapon, its bound effects and attached models, the GUI light, the lightning beam's near end | Existing owners (`UpdatePresentationWeapon`, `UpdatePresentationClientEntities`, `UpdatePresentationEffects`), now also turned by late look. |
| View muzzle-flash light and flashlight | **Implemented.** Re-placed from the drawn eye and gun when the light is already live. Think still creates, re-times and frees them; the flashlight carries Think's wall back-off instead of tracing on a draw frame. A VR hand keeps its tracked placement. |
| World muzzle-flash and flashlight lights (`_WORLD`) | Suppressed in the owner's own view. Other views see them on the owner's world-model joint: an SP subview may show the authoritative tick; MP remote owners are remote actors (HF-05/MP-07). Recorded exclusion. |
| Classic muzzle-flash dynamic light | A short world light at the fire position, intentionally fixed where the shot happened. |
| Unbound client debris, brass, BSE debris and MP client gibs (`rvClientMoveable`) and their trail effects | **Implemented.** Sampled after Think's authoritative push; drawn between the two tick samples with the scale envelope; the trail is pushed with the authoritative effect clock (`rvClientEffect::PresentAt`). Physics, bounce sounds and lifetime stay in Think. |
| Projectiles, movers, actors, bound client entities and lights | Existing generic entity pass and projectile replay fixes; terminal projectile states and restore reseeding unchanged. |
| Active articulated figures (ragdolls) | **Implemented.** The animator keeps the AF pose it replaces each tick; a draw blends the previous and current authoritative model-space poses (quaternion blend) over the root's interval. The AF bodies are never re-evaluated. |
| Angular-velocity joint modifiers | Exclusion: they integrate state from their last sample and cannot be evaluated at an earlier time; the authoritative pose is copied. |
| `idBrittleFracture` shards | Exclusion: the model callback rebuilds every shard from its own physics body each tick; a per-shard history would be a new owner for a rare, short-lived effect. |
| `idMultiModelAF` (one def per body) | Exclusion: bodies own separate render defs outside the entity pass; used by few props. |
| Unbound effects an owner repositions per tick: Makron bolt and gauntlet impact, the lightning gun's world beam (third person/other players), the vehicle lock-on guide and Awakening cockpit markers | Exclusion: their endpoints are trace or target positions chosen by gameplay each tick; interpolating them would draw beams to points the game never chose. The lock-on guide can trail an interpolated target by up to a tick of its motion. |

## Alternate views (HF-05)

Cinematic and private (security) cameras, third person, third person in a
vehicle and the death camera used to put the whole scene back on the 60 Hz tick.
`idPlayer::PresentAlternateView` now samples each such view once per tick,
exactly as the authoritative path built it, and draws it between its last two
samples on the scene's clock; `IsPresentationViewInterpolated` then admits the
scene to the same clock. Camera frame commands, cuts and third-person traces
stay in the authoritative call, which is idempotent within a tick; the draw pass
never calls `GetViewParms` at another time.

A view starts again from its current sample instead of blending when it
publishes an authored cut (`renderView.forceUpdate` from `idCameraAnim`),
changes kind or camera entity (spawn identity), skips a tick, runs at an altered
tic rate, jumps further than the motion already in flight (32 units plus four
times the last step, or 90°), when `g_presentationInterpolation 0`, during demo
or timedemo playback, when third person rides a carrier that cannot be drawn
interpolated, or when third person or the death camera follows a body held on
the tick (a remote player followed in multiplayer). A static private camera samples the same pose twice, so it costs
nothing and lets the world it shows move smoothly.

Remote actors (decision, 24 October 2026). While the local client plays, every
remote body stays on the authoritative tick, outside the presentation pass.
MP-07's server rewinds a target by the age of the pose the shooter saw (the
prediction row of the [clock contract](presentation-clock.md)). Drawing remote
bodies a further fraction of a tick behind would make that pose older than the
rewind allows for. Telling the server each command's draw phase would change the
network protocol. So remote bodies stay off the presentation clock while the
local client plays; this is a decision, not a pending item. A spectating client
fires nothing, so nothing it draws is ever rewound against. While the local
player spectates, remote bodies therefore join the presentation pass: a followed
player's body, third person behind it and the death camera draw on the camera's
clock instead of stepping at 60 Hz. A free-flying spectator already used the
first-person path. The local body's admission to the entity pass (SP actor, MP
local player) keeps its contract coverage (`actor_policy`), and the spectator
rule is pinned beside it. The MP Arena tableau camera is unchanged.

Vehicle cockpits and joint-bound eyes. The driver's eye is sampled from the
seat's eye joint each tick and drawn between its two samples. The cockpit around
it is drawn from its interpolated root and a skeleton evaluated at the draw's
time. The vehicle is the eye's carrier, so if the vehicle cannot be drawn
interpolated, the eye goes back to the tick with it. The two paths differ only by
curvature within one tick. For an eye r from the root turning θ per tick, a
lerped eye and an eye carried by a slerped root differ by at most r·θ²/8. For an
animated joint with acceleration a, the difference is a·Δt²/8. A walker eye 100
units from its root turning 90°/s (1.5° per tick) gives under 0.01 units. A
torso bob of 5 units at 2 Hz adds under 0.03 units. Both are below a pixel at
any field of view. The `vehicle` runtime case (below) drives a walker cockpit:
within each tick its drawn view is linear in the draw's phase to 0.0001°.

Nested views. Remote camera views shown on GUI surfaces (`cameraTarget`, such
as security monitors) render from the camera entity's authoritative view. Stock
monitor cameras are static, and the world they show is drawn by the same
presentation pass as the main view, so they need no owner of their own.

## Cross-system behaviour (HF-06)

| System | Behaviour |
| --- | --- |
| Game/network demo playback, timedemo | Bypass presentation (`PreparePlayerSceneForRender` returns early); late look and alternate views also refuse. Deterministic playback and benchmark meaning are preserved. |
| Render demo playback | Draws recorded views; the late-look sampler refuses while `readDemo` is open. |
| Command demos | The sampler refuses while `cmdDemoFile` drives the game. |
| AVI capture | Keeps the fixed-rate tic path (presentation-clock contract). |
| TAA history and motion vectors | Camera motion, including late look, is per-frame view reprojection. Authored cuts reach the renderer through `forceUpdate`; interpolated alternate views give continuous history. |
| BSE | Draw-only pushes repeat the authoritative game time, so particles move without respawning or ageing; the renderer de-duplicates servicing per frame. |
| Audio | Not advanced by presentation; AUD-02 owns audible lifecycle. |
| VR | Late look refuses; alternate views play flat as before. |
| Save, restore, map reset | All new state is transient and reseeded: player look/alternate-view samples, debris samples, the animator's AF history; none is serialized. |
| Repeated draws, subviews | Draw owners run no Think, physics, scripts, sound or effect creation (contract tests and the gameplay guard below). Input is consumed once: live authoritative turn rates match the configured input in both late-look modes (below). |

Gameplay guard. `idEvent::ServiceEvents`, `idThread::Execute` and
`idEntity::RunPhysics` first call `idGameLocal::NotePresentationGameplay`.
Outside a prepared draw it returns at once. Inside one, between
`PreparePlayerSceneForRender` and `EndPresentationSceneForRender`, it counts the
call and prints the first eight by name. `idGameLocal::PlayEffect` first calls
`NotePresentationEffect`, which treats an effect started inside the renderer's
scene work (`idPresentationRendererScope` around the game's `RenderScene`
calls) as BSE's own. Such effects are particle impacts and nested segments,
started once per particle or authoritative effect step. They are counted apart.
Any other effect started in a draw is gameplay. The counts ride each
`g_presentationClockCapture` line as `gameplay=` and `bse=`, and
`presentation_look_runtime.py` fails any case that records gameplay. The guard
only reports and never changes what runs. The contract requires the call to come
first in each entry point, the renderer scopes around scene submission and
identical SP/MP bodies, and a mutation that removes a guard must fail.

HF-06 is qualified engine-side by the runtime cases in Evidence. Every case
passes the gameplay guard, including stereo draws (`vr`), a remote-camera
subview (`subview`), demo recording and playback (`demo`) and the `combined`
cross-system session. Viewing temporal AA in motion on the reference display,
and a physical headset run, belong to HF-08's by-eye review and the VR track.

## Supported frame-rate behaviour (HF-07)

Declared targets, pending physical qualification:

| Target | Frame interval | Budgets on the Windows x64 desktop reference (visible foreground window, `r_swapInterval 0`) |
| --- | --- | --- |
| 240 Hz, supported target | 4.17 ms | p95 ≤ 4.6 ms, p99 ≤ 6.3 ms frame interval; engine input-to-draw ≤ 1 ms with late look; no missed tick at p99 |
| 360 Hz, experimental | 2.78 ms | Reported only; not a release promise |

Cases to qualify: caps 30/60/120/144/165/240/360 and `0`, vsync and VRR where
available, low-FPS catch-up (fewer draws than ticks), menus and loading, hidden
windows, and the preset caps. The declared policy, pinned by
`presentation_hf_contract.py`, is as follows. Loading screens redraw at the
lower of `com_maxfps` and 60 Hz, never faster, and back off when a redraw is
slow. Uncapped, they redraw at most once per tick. A window that shows nothing
(hidden, minimized, not created yet, or an app in the background) presents at
most 60 Hz on every platform, uncapped included; the state comes from SDL's
window flags (`Sys_SDL_IsGameWindowShown`, `openq4::PresentationCapForWindow`).
Before 25 October 2026 only Windows applied it, and only to hidden windows: a
minimized client, such as a full-screen game minimized on leaving it, kept
drawing at its cap. The `hidden` runtime case (a hidden window capped at 240
presented 60.0 Hz, p95 17.4 ms) and the `minimized` case (the cheat-gated
`windowMinimizeTest` minimizes the window) check it. A headset paces itself. The presets set `minimum`/`lowpower` 30, `low` 60,
`performance` 60, `balanced` 120, `enhanced` and `quality` 144, and `ultra` and
`maximum` 240, and a detected Steam Deck starts on `performance`. An automatic
recommendation (Auto-Detect or a fresh installation) instead caps at the whole-hertz
refresh rate of the window's display, keeping the tier cap when that rate is unknown
or outside 30-1000 Hz (FPS-02), and on a power-limited device (phone, Deck, Pi,
explicit low-power request) whenever the tier cap is the lower of the two. Use
[`presentation_look_runtime.py`](https://github.com/themuffinator/openQ4/blob/master/tools/tests/presentation_look_runtime.py)
for engine-side latency and smoothness, `com_showFramePacing 2` for p50/p95/p99
intervals and the gameplay benchmark for frame times, all in a visible
foreground window. Physical display latency (scan-out, compositor, panel)
needs external measurement and is not claimed by these tools.

Pacing statistics. `com_showFramePacing` and `framePacingSnapshot` time each
presentation frame from the high-resolution stamp the limiter takes as it
releases the frame, binned at 50 µs (`PresentationPacing.h`). Until 25 October
2026 they used whole milliseconds, which cannot judge this budget: a steady
4.17 ms cadence landed in the 4 and 5 ms bins and read p95 5 ms. A percentile
reports the upper edge of its bin, so it errs long by under 0.05 ms and never
short. The overlay adds the display's refresh rate, the limiter's wake
lateness and, at a 240 cap, whether the budget is met. The snapshot line adds
`swapInterval`, `cap` and `refresh`. Its `bound=` names what holds the rate:
`presentation-cap` when frames arrive within −5/+8% of the cap at any cap (it
used to apply only below about 55 Hz), `vsync` when vsync is on and frames are
no faster than the display's refresh, `simulation` when frames wait for game
tics, and `frame-work` (formerly `uncapped`) when nothing waits and the
frame's own work sets the rate.

Physical latency (HF-08). Set `com_latencyFlash 1` and point a click-to-photon
sensor (NVIDIA LDAT, OSLTT or a photodiode) at the left edge of the screen.
Each press of the attack button arms the flash as the event arrives. The first
frame drawn afterwards paints a white bar over the left tenth of the screen, and
the sensor reads the time from click to light. That frame samples input as late
as late look does, so the reading is the engine, swap chain, compositor and
panel latency a late-look view has. The flash does not depend on
`in_presentationView`. A late-look view reaches the screen on the flash's frame.
The classic view trails it by the engine-side delay measured above, about one
tick (16 ms). Measure full screen on the reference machine at the HF-07 caps,
with vsync off and on. Shots still land on the next tick, so muzzle-flash
latency is longer by up to a tick. `latencyFlashTest` (cheat) arms the flash without a device, and the
`flash` runtime case checks that each arming draws exactly one frame.

The engine's own share of that delay is measured too. `framePacingSnapshot`
prints `Presentation latency …: lookSamples=… sampleToSwap … flashes=…
pressToSwap …`: for every frame, the time from the draw's first late-look
input sample, and from the press behind a drawn flash, to the return of the
renderer's successful swap or present call. Since 3 November, those clocks
travel with the frame through the optional Vulkan render thread. The backend
stamps the successful swap; the main thread records the receipt at retirement.
Returning from `UpdateScreen` is not sufficient when the backend is asynchronous.
Failed presents, hidden offscreen frames and readbacks do not produce latency
samples, and a reset discards receipts whose input predates it. Renderer module
API 30 carries the clock handoff/receipt; engine and renderer modules must be
built together. A sensor reading minus `pressToSwap`
is the GPU, compositor and display share. The harness reports both as
`inputToSwapMs`. Its `sensor` case is the operator's session for this: the
operator's own mouse works (no other case reads devices), late look and the
flash are on, and for `--sensor-seconds` each fire press flashes the left edge.
The snapshot then gives the engine's press-to-swap time for those same
presses.

By-eye review (HF-08). `presentation_look_runtime.py --scenario review
--fullscreen` runs three captioned sessions for an operator watching the
reference display; on-screen captions name each segment as late look is
switched off and then on over the same motion. `review-play` (airdefense1)
shows a steady and a rapid turn under temporal AA, firing while turning (the
cheat-gated `in_presentationViewProbeAttack` holds fire as the zoom probe
holds zoom: muzzle flash, its light, tracers and impacts), third person, a
ragdoll with barrel debris, and the death camera. `review-cinematic` plays
airdefense1's opening cinematic with its cuts, and `review-monitor` shows
tram1's remote-camera monitor. The run still applies the engine-side checks
(no gameplay in a draw, no errors) and writes `review-checklist.md`, a sheet of
what to judge in each segment, for the operator's verdicts.

The cap matrix runs as one command:

```
presentation_look_runtime.py --basepath <Quake 4> --output-dir <new dir>
  --scenario pacing --fps 30 60 120 144 165 240 360 0 [--renderer vulkan]
  [--swap-interval 1] [--fullscreen] [--reference]
```

Each cap turns the view at 220°/s with late look on. Each case reports the
prepared-draw interval p50/p95/p99 at sub-millisecond resolution, the presented
rate, ticks no draw showed, whether the rate fell below 90% of the cap, the
late-look sample-to-draw time and the engine's `framePacingSnapshot` summary.
Every case also reports `prepareMs`, the cost of preparing each draw (camera,
late look, entity poses, client debris). The pacing case adds the renderer's
front-end, back-end and GPU summary, so a slow run shows where the frame went.
The 240 cap is judged against the budget above. Other caps are reported only.
`--fullscreen` uses the native desktop mode instead of a 960×540 window.
For the optional Vulkan worker, add `--cvar r_vkRenderThread 1` for tic-only
overlap or `--cvar r_vkRenderThread 2` for full front-end overlap; its default
remains zero. The reference-suite wrapper accepts `--render-thread 0|1|2` and
records that selection. It returns failure for a failed child, a missing or
malformed report, or VRR coverage lost because no usable refresh was reported.
Its `automatedPassed` result covers automation only: `physicalQualification`
stays `pending` until sensor readings and the operator's review are recorded.
For variable refresh, add a run with `--swap-interval 1 --fps <refresh − 3>`
with VRR (G-Sync, FreeSync) on in the driver. With VRR the presented rate
follows the cap; without it, vsync rounds it down to a divisor of the refresh.
`--swap-interval -1` is adaptive vsync (SDL's adaptive interval on OpenGL,
`FIFO_RELAXED` on Vulkan).
`--reference` is the operator's statement that the window was visible, in the
foreground, on the reference machine. Without it every verdict reads "not
qualifying", whatever the numbers. With it, a 240 case over budget fails the
run.

Limiter precision. The frame cap waits against a fixed cadence. Since
25 October 2026 it sleeps most of each period with `SDL_DelayNS`, a
high-resolution waitable timer on Windows and `nanosleep` elsewhere, then
busy-waits only the time its sleeps have been waking late: the 95th percentile
of the last 64 sleeps plus 0.1 ms, between 0.25 and 2 ms
(`openq4::LimiterOversleepWindow`). Ignoring the slowest few sleeps leaves out
preemption, which no spin can hide and which would otherwise keep a wide spin
burning a core. The native `openq4-presentation-pacing` test drives that loop
against model timers: a timer waking up to 0.3 ms late leaves no frame late
with a spin under 0.45 ms, a coarse one widens it, and preemption every 200
frames neither widens it nor delays the frames between. `com_preciseFrameWait 0`
restores the previous wait, whole-millisecond sleeps and a fixed spin of an
eighth of the frame (0.5 to 1.5 ms). `framePacingSnapshot` prints how late each
wait woke against its target (`Presentation throttle …: waits=… late
p50/p95/p99/max`), and the harness reports it as `throttleLateMs`. Only frames
that arrive before their target wait and are counted. Until 25 October 2026 a
frame whose own work overran the period was counted too, with its overrun, so
a work-bound run read as a late limiter: on a loaded machine at a 240 cap,
`late p50` was 6.5 ms although the frames took 14 ms. In a visible window a p95
above about 0.3 ms would be a limiter defect worth fixing.

Windows 11 honours `timeBeginPeriod` only while a process has a visible window,
unless the process opts out. Without the opt-out a fully occluded or minimised
window falls back to the default 15.6 ms timer resolution. Its waits then wake
late by up to a scheduler tick, and the async thread's 1 ms tick timer, which
drives the 60 Hz game clock, fires in bursts, so a listen server behind another
window ticks unevenly. `Sys_Init` now opts out
(`PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`, resolved at run time and
refused harmlessly before Windows 11) and logs `Timer resolution: 1 ms kept
while the window is occluded`. Only a visible foreground run still qualifies
HF-07: the compositor and driver may throttle an occluded window's
presentation whatever the engine's timers do.

Presented-frame timing (Vulkan). Draw intervals say when the engine finished a
frame, not when it reached the display. With `r_vkPresentTiming 1` (off by
default; applies when the device starts) and a device exposing
`VK_KHR_present_id` and `VK_KHR_present_wait`, every present carries an id and
a waiter thread blocks in `vkWaitForPresentKHR` until the presentation engine
reports that frame presented. `rendererPresentTiming [reset | <reason>]`
reports the interval between presented frames, the time each waited after
`vkQueuePresentKHR` returned, and how many were replaced before display. The
waiter only ever waits on the current swapchain: every recreation, surface
recovery and shutdown quiesces it first (its waits are bounded at 4 ms), and the
contract pins that order. The harness's `--present-timing` collects it and holds
the presented intervals to the same 240 Hz budget. OpenGL has no portable
equivalent, so its pacing stays draw-side. On this laptop at a 144 cap, frames
drawn at p95/p99 11.3/13.5 ms reached the display at 13.6/22.1 ms, with 2.4 ms
median queueing after the present call; under the Khronos validation layer the
waiter ran 1,013 frames without a validation or threading message.

Reference suite.
[`presentation_reference_suite.py`](https://github.com/themuffinator/openQ4/blob/master/tools/tests/presentation_reference_suite.py)
runs the whole HF-07/HF-08 procedure in one command at the reference desktop:
the cap matrix full screen for OpenGL and Vulkan (with presented-frame timing),
vsync and adaptive vsync, variable refresh at 3 Hz under the refresh the first
run reports, the hidden, minimized, menu and hitch cases, the sensor session and
the review sessions. It writes `reference-summary.md` with each 240 Hz verdict
and where the review checklist is:

```
presentation_reference_suite.py --basepath <Quake 4> --output-dir <new dir> --reference
```

`--windowed` runs every case in a window instead, a rehearsal that checks the
procedure end to end without taking over the display; `--dry-run` prints the
plan. Each case gets 600 s, because every client loads from a fresh save path
and regenerates its images first. A case whose window drew too slowly to fill
its capture still reports what it captured, and the summary lists any case it
could not judge rather than leaving it out.

An occluded window on Windows does not qualify: on 23 October 2026 the same
client capped at 240 presented at 57 Hz on OpenGL (p99 232 ms) and 15 Hz on
Vulkan (p99 725 ms) behind other windows on a loaded machine. HF-07 remains open
until physical runs on the reference hardware meet the budgets above.

## Evidence

Native: `openq4-presentation-look` executes the production policy header in a
model of the usercmd pipeline at 30–997 Hz with jitter, buffered and direct
usercmds, steady and flick-speed turns through the wrap and both pitch clamps:
the drawn view equals the input sampled at the draw within 0.02°, interpolation
alone always trails by a tick, no draw turns further than the input did, and
held rates draw without a staircase. Static: `presentation_hf_contract.py`
(engine sampler, SP/MP owners, alternate views, AF history, mutations) and the
updated `presentation_interpolation_contract.py`.

Live single player, `game/airdefense1`, OpenGL, synthetic probe at 2000
counts/s (220°/s), 512 draws per case, `com_maxfps 240` on an occluded window
that presented at about 62 Hz (draw interval p50 16.2 ms, p95 25.6 ms). Retained
under `.tmp/hf/look-run3` and `.tmp/hf/scen-run1`:

| Measure | Late look off | Late look on |
| --- | --- | --- |
| Authoritative turn rate (expected −220.00°/s) | −220.00°/s | −219.95°/s |
| Drawn yaw against the sampled input | — | p95 0.003°, max 0.007° (command quantization) |
| Delay behind the input (line fitted at usercmd build, 3.9 ms build-to-draw bias removed) | about 16 ms | about 0 ms |
| Frame-to-frame residual of the drawn yaw, p95 | 2.15° | 0.09° |

Third person with `g_presentationInterpolation 1` recorded 512 interpolated
draws (residual p95 2.06°, the same as interpolated first person); with `0` the
scene correctly stayed on the authoritative tick and recorded none.

Owner activity, from the capture's per-draw counters (`.tmp/hf/scen-run4`), on
airdefense1's posed `env_ragdoll_marine` figures plus a spawned Strogg marine
killed by `damage`, the player's own death (`testDeath`, death camera) and a
`launching_barrel_small` thrown by `damage`:

| Scenario | AF poses requested on draws | Blended | Client debris drawn |
| --- | --- | --- | --- |
| Ragdoll | 2,764 | 2,747 | — |
| Player death | 2,589 | 2,571 | — |
| Exploding barrel | 1,869 | 1,860 | up to 2 at once |

The unblended remainder are draws at the end of a tick, which show the
authoritative pose. A first build blended nothing: the AF calls `ForceUpdate()`
every tick, clearing `lastTransformTime`, so the history now keeps the time of
the pose held in `joints[]` itself, and the contract rejects the old condition.
No run logged an error. These runs measure engine-side behaviour on one Windows
machine; they are not physical input or display qualification, nor a visual
review of the drawn motion, and the occluded window did not reach the requested
rate.

### Second round, 24 October 2026

The same harness ran on the guard build with OpenGL, 512 draws per case and a
240 cap unless stated. Output is retained under `.tmp/hf/r2-run1` and
`.tmp/hf/r2-pacing1`. After the BSE split below, the spectator, walker,
late-look and 240 pacing cases were run again (`.tmp/hf/r2-run2`). All seven
passed, with no gameplay and no BSE starts in any draw and no errors.

Gameplay inside draws. No SP case (late look, third person, death camera,
ragdoll, barrel debris, walker cockpit) and no MP case (listen host, remote
client) counted any. One spectator run counted three `PlayEffect` calls in the
draw where a bot was gibbed. The MP gib effect's splat particles carry an impact
effect, and BSE starts it when a particle hits geometry while the renderer
services effects inside `RenderScene`, once per particle. That is BSE's retail
design, not game work repeating per draw. The guard now counts effects started
inside the renderer's scene work apart (`bse=`); any other `PlayEffect` inside a
draw is still gameplay.

Late look in multiplayer (loopback, 127.0.0.1):

| Case | Late look | Turn rate (expected −220.00°/s) | Delay behind the input | Drawn vs sampled input, p95 | Residual p95 | Draw interval p50/p95 |
| --- | --- | --- | --- | --- | --- | --- |
| Listen host | off | −220.01°/s | about 16 ms | — | 0.26° | 4.3/5.6 ms |
| Listen host | on | −220.00°/s | about 0 ms | 0.0001° | 0.08° | 4.4/5.7 ms |
| Remote client | off | −220.00°/s | about 16 ms | — | 1.17° | 4.5/19.3 ms |
| Remote client | on | −220.01°/s | about 0 ms | 0.0001° | 0.08° | 4.6/18.0 ms |

Input reached the game exactly once through client prediction as well as on the
host. The client's slower p95 interval is two clients sharing one machine. Single
player on the same build gave −220.01/−220.00°/s and a residual p95 of 1.37°
without late look against 0.08° with it.

Spectating. A listen host with two bots listed up to 68 entities in the
presentation pass while playing, 71 while spectating free and 70 while
following a bot. That fits the bots joining the pass only while the host
spectates. In the free case,
a killed bot's ragdoll was blended on 183 of 184 AF draws, which happens only to
bodies on the presentation clock. Following a bot gave 512 interpolated draws of
its view, with a step deviation p95 of 0.04°, linear within each tick to
0.0001° in the rerun.

Walker cockpit. The `vehicle` case spawns a walker and enters it by script.
In a rerun (`.tmp/hf/r2-run2`, draws about every 5.5 ms, walker turning at
196°/s), all 512 cockpit draws were interpolated, with no errors and no gameplay
in a draw. Within each tick the drawn cockpit yaw is linear in the draw's phase
to 0.0001° (p95, `tickLinearityDeg`), so the joint-bound eye adds no
presentation error. The view's
step deviation (`drawnStepJitterDeg` p95 1.7°, against 0.5° for first person
without late look and 0.15° with it) comes entirely from per-tick changes in the
walker's simulated turn (p95 2.2° per tick). That is input reaching the view
once per tick plus the walker's own turning. Late look removes the input part
for first person and is excluded in vehicles.

Cap matrix (`--scenario pacing`, late look on). This is engine-side and not
qualifying: the run was not declared `--reference`.

| Cap | Presented | Draw interval p50/p95/p99 | Ticks no draw showed | Engine snapshot: bound, p95/p99 |
| --- | --- | --- | --- | --- |
| 30 | 30.0 Hz | 33.3/34.5/35.5 ms | 483 of 511 (expected below 60) | presentation cap, 34/35 ms |
| 60 | 60.0 Hz | 16.7/17.2/17.6 ms | 1 of 510 | simulation, 18/18 ms |
| 120 | 118.7 Hz | 8.4/10.7/11.1 ms | 0 | uncapped, 10/10 ms |
| 144 | 143.8 Hz | 7.0/9.6/13.3 ms | 0 | uncapped, 9/10 ms |
| 165 | 164.6 Hz | 6.1/8.8/10.9 ms | 0 | uncapped, 8/11 ms |
| 240 | 188.6 Hz, below the cap | 5.3/8.5/12.3 ms | 0 | uncapped, 8/11 ms |
| 360 | 178.1 Hz, below the cap | 5.6/8.0/10.2 ms, one 921 ms stall | 1 | uncapped, 11/13 ms |
| Uncapped | 186.8 Hz | 5.4/8.1/9.7 ms | 0 | uncapped, 8/10 ms |

At every cap the late-look view matched the input sampled for its draw (p95
0.0001°, 0.002° at 30). The 240 case misses the budget (p95 8.48 ms against
4.6, p99 12.27 ms against 6.3) because this machine did not reach the cap: the
engine classified those frames as not cap-bound. The rerun at 240 reached
204 Hz (p95 7.2 ms, p99 8.4 ms) and is still over budget. The 921 ms stall was a
single frame the engine classified as simulation-bound. Physical qualification
on the reference machine remains HF-07.

### Zoom, lifts and packet loss, 24 October 2026

Two cheat-gated development cvars drive these cases without device input.
`in_presentationViewProbe` feeds synthetic mouse counts through the ordinary
mouse path. `in_presentationViewProbeZoom` holds the zoom button in the usercmd,
exactly as a held key would. Output is retained under `.tmp/hf/r3-run1` and
`.tmp/hf/r3-run2`.

| Case | Late look | Turn rate (expected) | Delay behind the input | Drawn vs sampled input, p95 | Residual p95 |
| --- | --- | --- | --- | --- | --- |
| Railgun zoomed (`pm_zoomedSlow 50`) | off | −110.02°/s (−110.00) | about 15 ms | — | 0.46° |
| Railgun zoomed | on | −110.01°/s (−110.00) | about 0 ms | 0.0001° | 0.04° |
| Riding airdefense2's `func_elevator_3` up 193 units | off | −220.01°/s (−220.00) | about 16 ms | — | 0.22° |
| Riding the lift | on | −220.01°/s (−220.00) | about 0 ms | 0.0007° | 0.09° |
| Remote client, 5% of packets dropped at the client | off | −220.00°/s (−220.00) | about 16 ms | — | 1.07° |
| Remote client, 5% dropped | on | −220.00°/s (−220.00) | about 0 ms | 0.0001° | 0.08° |

Zoomed look follows the scaled sensitivity in both modes, so the late-look
sampler applies the same zoom scale as the usercmd. On the lift, the drawn eye
height was linear within each tick to 0.0004 units (`tickLinearityHeight`) while
the late-look correction turned it. Under loss, input still reached the game
exactly once and the drawn view still equalled the input sampled for the draw.
No case counted gameplay or BSE starts inside a draw, and none logged an error.

Delay, jitter and loss together. Once the network impairment rework landed
(the old latency queue dropped datagrams over 1400 bytes, so a delayed client
could not connect), `mploss` set `net_forceLatency 40`, `net_forceJitter 10`
and `net_forceDrop 5` on the client. That is at least 80 ms round trip, with
reordering, and `netImpairment` confirmed the conditions held after the join:
4–5% of datagrams dropped and about 50 reordered each run
(`.tmp/hf/r6-loss2`). Input was still consumed once (−219.99 and −220.00°/s).
With late look the drawn view matched the input sampled for each draw (p95
0.0001°), about 16 ms of delay disappeared, and the frame-to-frame residual fell
from 0.84° to 0.08°. Late look is a local view correction, so network delay
does not reach it; prediction recovery keeps its MP-07 behaviour.

Demo recording and playback (HF-06). The `demo` case records a render demo
(`recordDemo`) while late look turns the view and the capture runs, then plays
it back with `timeDemoQuit` in the same session. All 512 draws during recording
were interpolated with late look active. The view matched the input sampled for
each draw (p95 0.002°) and no draw counted gameplay. Playback then took the demo
bypass and rendered the recorded 569 frames in 2.9 s before the client exited
cleanly (`.tmp/hf/r3-run3`).

### Rapid turns, VR, subviews, Vulkan and vsync, 24 October 2026

Output is retained under `.tmp/hf/r4-run2`, `.tmp/hf/r4-vk` and
`.tmp/hf/r4-vsync`. No case counted gameplay or BSE starts inside a draw, and
none logged an error.

- **Rapid turns** (`flick`, 2200°/s). Input was consumed once in both modes
  (−2200.29 and −2200.06°/s against −2200.00). With late look the drawn view
  matched the input sampled for each draw (p95 0.0001°), the delay of about
  15 ms disappeared and the frame-to-frame residual fell from 4.67° to 0.09°.
- **VR** (`vr`). The in-tree OpenXR test runtime at 90 Hz received 2,340 stereo
  frames. All 512 captured draws were interpolated, late look stood down on
  every one, and no gameplay ran in either eye's draw.
- **Remote-camera subview** (`subview-monitor`, tram1's `bigScreen`). The
  renderer drew a remote-camera view in every metrics frame (18 views per frame,
  against 9 at the airdefense1 position) and nothing ran gameplay.
- **Where a 240-capped frame goes** (OpenGL, 196 Hz). Preparing each draw
  (camera, late look, entity poses, debris) took 0.35 ms at p50 and 0.52 ms at
  p95. The renderer reported about 1 ms front end, 1 ms back end and 2 ms GPU.
  Presentation work is about 7% of the frame and is not what keeps this machine
  under the cap.
- **Vsync** (`--swap-interval 1`). This machine's 2560×1440 panel runs at
  240 Hz through hybrid graphics. With vsync, the 240 cap presented at 138 Hz
  (p50 7.3 ms) and uncapped at 130 Hz, with no tick left undrawn: frames that
  miss a 4.17 ms refresh wait for the next.
- **Vulkan cap matrix**, on a window behind others. 60 held (59.9 Hz). 144
  presented 80 Hz (p95 54 ms), 240 presented 120 Hz (p95 24 ms, 29 ticks
  undrawn), and uncapped 164 Hz with one 754 ms stall. Draw preparation spiked
  to 52 ms, so the process was being descheduled. This matches the earlier
  finding that an occluded Vulkan window is throttled; it is not qualifying
  evidence either way.

Cross-system session (HF-06, `combined`, `.tmp/hf/r7-combined`). One session ran
`r_temporalAA 1`, `r_useShadowMap 1`, late look, the retained UI and audio
together while the probe turned the view. It captured 256 draws live, then the
same after `saveGame`/`loadGame` (a map reset), after `vid_restart` (the game
re-created its render targets) and after an `aviGame` capture had run the
fixed-tic AVI path for a second. All four captures were interpolated with late
look active on every draw, no gameplay ran inside any draw and nothing logged an
error. The inherited AVI path has its frame writes commented out, so this
exercises its clock, not its output. Screenshots taken mid-turn at 220°/s under
TAA and late look show no ghosting or double edges on the weapon or the
geometry.

Hitch catch-up and menus (HF-07, `.tmp/hf/r10-hitch`, `.tmp/hf/r10-menu`). The
cheat-gated `com_presentationStallProbe 100` stalls one frame for 100 ms once a
second, so the game then catches up several ticks in one frame. Over 10–11
stalls per run, the drawn view never stepped against the turn in either mode,
input was consumed once (−220.02 and −220.03°/s), and with late look the view
still matched the input sampled for each draw (p95 0.003°). The view jumps
forward by the turn made during the stall (about 31–34°), as it should. The
residual against the input line stayed at p95 0.09° with late look, against
2.2° without. The main menu follows `com_maxfps` like gameplay: it held 58 Hz at
a 60 cap and reached 164 Hz at a 240 cap on this machine, never above the cap.

### Frame-cap precision, minimized windows and engine latency, 25 October 2026

The same laptop (occluded window, OpenGL, 960×540) under heavy load from
other builds. Output is retained under `.tmp/hf/r12-run2`, `.tmp/hf/r12-ab2-*`
and `.tmp/hf/r12-review3`. No case counted gameplay inside a draw and none
logged an error. `win_keepTimerResolution` read 1 in every run, so the
Windows 11 timer opt-out took effect.

Frame-cap wait, two interleaved pairs per cap (`--cvar com_preciseFrameWait 0`
for the previous wait; late wakes counted only for frames that waited):

| Cap | Wait | Late wake p95 / p99 | Busy-wait per frame | Engine interval p95 |
| --- | --- | --- | --- | --- |
| 60 | precise | 0.00–0.06 / 0.05–0.11 ms | 0.35 ms | 16.70–16.75 ms |
| 60 | previous | 0.42–0.46 / 0.78–0.91 ms | 0.72–0.76 ms | 17.10–17.15 ms |
| 144 | precise | 0.00 / 0.17–0.20 ms | 0.37–0.40 ms | 9.3–10.2 ms |
| 144 | previous | 0.91–1.05 / 1.36–1.41 ms | 0.26–0.27 ms | 10.7–11.3 ms |

At 144 the previous wait's whole-millisecond sleeps overshot its 0.87 ms spin
and woke about a millisecond late at p95, a seventh of the period; the precise
wait spun slightly longer and was not late. At 60 it halved the busy-wait. At a
240 cap this loaded machine was bound by its own frame work (143–168 Hz), so
the limiter rarely waited and the comparison says nothing there.

Windows that show nothing. `windowMinimizeTest` minimized the client at a 240
cap: the snapshot read `cap=60`, `bound=presentation-cap`, 59.5 Hz over
1,432 frames, and the limiter's late wakes p95 0.00 ms. Late look kept
consuming input once (−220.00°/s). An earlier run under heavier load showed
Windows itself slowing a minimized client to about 7 Hz, below the cap, which
the `minimized` case allows.

Engine latency (`inputToSwapMs`). With late look the draw's input sample reached
the return of the swap in 10 ms at p50 (14 ms p95) when frames took 12–14 ms
under load, and 5–7.5 ms at p50 in the lighter A/B runs. The five
`latencyFlashTest` presses each drew one flash frame and reached the swap in
12.6 ms at p50 (17.4 ms max) under load. These are this machine's numbers; the
reference run measures its own.

Review kit (`--scenario review`, `.tmp/hf/r12-review3`). All three sessions ran
every segment with no gameplay in a draw and no errors. The play session
captured 3,425 interpolated draws before the death camera and 512 during it,
blended ragdoll poses on 1,764 draws and drew up to 9 debris pieces; the fire probe emptied and
reloaded the machinegun (ammunition 40 → 31 → 23 across the shots). The
cinematic and monitor sessions captured 512 draws each. Screenshots do not show
the captions, because taking one re-lays out the console and clears its notify
lines; an operator watching the session sees each caption for 8 s.

### Reference-suite rehearsal, 26 October 2026

The whole suite ran windowed on the development laptop (`--windowed
--skip-sensor`, `.tmp/hf/r12-rehearsal`): 10 runs, 32 cases, with the window
behind others on a busy machine, so none of its numbers qualify. It found three
faults in the procedure, now fixed. With vsync on, the covered OpenGL window
fell to about 13 Hz. The capture then never filled, never dumped, and the case
reported nothing, although the engine's snapshot had the data. The same
happened to Vulkan at 30 and 60. Every case now dumps a partial capture: a
rerun captured 512 draws for each. The hidden case timed out at the harness's
default four minutes while its fresh save path was still regenerating images.
Two reruns finished in 158 and 88 s at 58.5 and 60.1 Hz, so the suite now gives
each case 600 s. The summary also left out cases it could not judge; it now
lists them. Everything else ran cleanly: no case counted gameplay in a draw or
logged an error. The variable-refresh cases ran at 237, from the reported
240 Hz refresh. Vulkan reported presented intervals in every case it captured.
Through every 100 ms stall the hitch cases never stepped the view backwards,
and the review sessions completed.

### Threaded lifetime validation, 3 November 2026

The native tests now execute the production worker state machine, bulk world
cleanup, debug-text expiry, swap receipt and latency collector. They force
1,006 handoffs through both completion orders, errors, disable/restart and map
teardown, including a worker that has finished before the main thread syncs.
Delayed main-thread collection does not inflate the recorded swap time. The
presentation test also executes deferred acquire/present recovery against
controlled driver/window boundaries; no surface call runs on the worker.

The live lifecycle probe uses the staged x64 runtime and installed stock assets,
with a visible 960 x 540 window and device input disabled. It cycles caps
30/60/120/144/165/240/360/uncapped, toggles VSync, exercises debug primitives,
injects a Vulkan surface-loss result, restarts the renderer, toggles the worker,
and arms five engine-only latency flashes. It uses the existing gameplay
benchmark for SP `game/airdefense1` and MP `mp/q4dm1` listen-host/client, with an
OpenGL SP control. Reports, binary hashes, engine logs and engine-rendered
screenshots are retained under `.tmp/high-refresh/`; original failed runs are
kept beside the repaired reruns. These are functional checks on a shared
machine, not the native-resolution Quality budget or physical qualification.

The final SP and MP Vulkan runs passed, with submitted/completed counts
2,636/2,636 (SP), 2,542/2,542 (host) and 1,776/1,776 (client) at their last
explicit stats checkpoint. The OpenGL control passed too. Each of the four
roles recorded all five flash receipts and 128 presentation samples with zero
gameplay executed during drawing. Engine screenshots show the expected worlds
and HUDs after recovery/restart. The nine focused Meson tests pass, including
the million-tick ownership test and the eight qualification-runner cases.

A separate diagnostic issue remains: Vulkan `gfxInfo` can report the OpenGL
bootstrap tier as `NullRenderer` after a restart even while native Vulkan
rendering is active. The engine screenshots and successful presentation
receipts verify that the world still renders; that tier label is not used as
proof of renderer or pacing qualification.

### Submitted-frame ownership, 4 November 2026

`RC_DRAW_SPECIAL_EFFECTS` carries the admitted mask and all controller
parameters. Both backend adapters and reconstructed scene packets consume the
queued values, including `r_forceSpecialEffects`; a later controller reset or
edit cannot replace them. Only first-use image creation and resource teardown
retain the controller's worker synchronization.

Command preflight now latches the first `RC_SET_BUFFER` frame number before
building backend resources. A mid-frame flush without that command retains the
current backend identity. Vulkan shadow-cache stamps, prepared light tables,
GPU timing/readbacks and query retirement, classic deformation validation and
clustered generations use the submitted frame. Front-end deformation capture
still uses the front-end token; explicit tokens cross the boundary without
revalidating against mutable front-end state.

The two interaction failures and two shadow invariant failures use a worker
exception receipt. Main-thread collection retires once before dispatching the
original recoverable/fatal error. Native tests execute the actual worker through
1,009 handoffs, including both severities and a successful frame afterwards.
Fatal teardown is replaced by a throwing test boundary; the test asserts that
no common error or teardown starts on the worker.

Eleven focused Meson tests pass, including production enqueue/GL/Vulkan effect
consumption across later edits, submitted shadow records and query ages with
the front-end counter far ahead, and the existing clock/pacing/latency coverage.
The corrected screenshot/native-UI fixtures and CI test discovery pass locally.
Build, test and runtime evidence is retained under `.tmp/high-refresh-next/`.

Windowed stock-asset gameplay passed on the staged Windows x64 build: Vulkan
SP `game/airdefense1`, Vulkan MP `mp/q4dm1` listen host/client and an OpenGL SP
control. Each role passed all eight classic-domain self-tests, exercised forced
blur and Alpha Labs enable/disable, and recorded five successful latency-flash
receipts plus 128 samples with zero gameplay executed during drawing. Vulkan
also cycled the cap matrix, surface recovery, `vid_restart` and worker toggles;
its last submitted/completed checkpoints were 1,734/1,734 (SP), 1,254/1,254
(host) and 1,350/1,350 (client). Shadow GPU queries resolved without dropped
spans in the inspected MP logs. Engine captures of blur and the final world/HUD
were inspected; no OS capture or device-input injection was used. A separate
OpenGL gameplay run passed the updated scene-packet, render-graph/resource,
modern draw-plan and executor self-tests (including their shader, pixel-transfer,
probe-atlas and submit-plan subtests).

These shared-domain probes can fall back on scene-packet capacity in the SP
scene; they do not qualify full shared ownership. The earlier Vulkan
`NullRenderer` bootstrap label also remains. Unrelated failures in the earlier
PR CI were the stale generated Demos page, an image-policy mutation fixture
whose source needle no longer matches, and a macOS MVD navigation negative
control that did not fail. Their test/production inputs are unchanged by this
PR. The registration and swap-wrapper fixture failures introduced by this PR
are fixed and pass locally.

That checkpoint closed the audited backend error item and part of frame
identity. The subsequent implementation below addresses the stage-2 ownership
list. Physical qualification and the native-resolution Quality floor remain
open.

### Full front-end overlap, 4 November 2026

`r_vkRenderThread 2` moves the normal join to the next `EndFrame`, after GUI/VR
command assembly. Mode 1 retains tic-only overlap and mode 0 remains the
default. There is still only one submitted backend frame at a time. The next
front end uses a second arena, vertex-cache lists, scene-packet frame and
temporal state. A thread-local selector keeps backend allocations and metrics
attached to the submitted slot, including when its worker finishes before the
main thread collects it.

Each submitted view freezes geometry, cache headers/CPU bytes, skinning
palettes, MD5R transforms, previous positions, entity definitions and light
definitions. Shared aliases remain shared inside the snapshot. Classic
deformation records are rebased only for finalized draws; uninitialized
interaction/volume records are not read. Sticky light failures return through
identity-checked receipts. Deferred ambient and interaction surfaces retire as
one batch at a completed handoff, before the worker wakes, or during synchronous
retirement/shutdown. Clearing the live arena pointer before shutdown frees its
slots prevents cleanup from allocating into an already freed slot.

Static images/programs and usable light-grid areas prepare at handoff.
Name-only image lookup uses a registry lock; cached shader validation does not
join on every SP frame. Mutating images, materials, renderer settings or device
state still joins. The configuration barrier extends renderer API to **31**,
so engine and renderer modules must be built/staged together. Captures,
loading, checked image-policy execution, locked surfaces and the Alpha Labs
live-light path remain synchronous. Scene history seals only after a successful
threaded handoff; inline reconstruction retains the preceding history until
retirement.

Native tests execute the production allocator/retirement methods with a held
consumer, allocations larger than an arena block, invalid sizes, shared
triangle retirement, cache-slot reuse and teardown/reinitialization. Two
negative controls detect shared arena storage and split triangle retirement.
The worker test covers 1,011 handoffs, both error severities and a renderer
configuration mutation barrier. `rendererFrameOwnershipSelfTest` mutates source
streams and definitions after snapshotting and checks retained values/aliases,
including poisoned non-finalized deformation records. The reference runner
also tests forwarding mode 2 without claiming physical qualification.

Evidence is retained under `.tmp/high-refresh-overlap/`; build/stage logs for
the earlier iterations are also under `.tmp/high-refresh-next/`. Thirteen
focused Meson tests and 28 related source/native checks passed. Windowed
stock-asset gameplay covered Vulkan SP `game/airdefense1`, MP `mp/q4dm1`
listen-host/client, and an OpenGL SP control, with device input disabled. The
broad checks covered the cap matrix, VSync, forced effects, eight classic-domain
self-tests, worker toggles, surface recovery and renderer restart. Additional
combination checks enabled GPU skinning, TAA, motion blur and dynamic resolution
together, then disabled them and reloaded shaders. Engine-rendered captures
were used throughout; no OS capture or device-input injection was used.

The final retirement build passed SP (`vk-sp-airdefense1-verified`), MP and
OpenGL (`*-retirement`). Last explicit Vulkan checkpoints were:

| Role | Submitted / completed | Front ends started while busy | Front ends finished while busy |
| --- | --- | --- | --- |
| SP | 486 / 486 | 73 | 2 |
| MP host | 682 / 682 | 669 | 1 |
| MP client | 866 / 866 | 857 | 145 |

Each role, including OpenGL, recorded five successful latency-flash receipts
and 128 presentation samples with zero gameplay during drawing. All exited
cleanly. The preceding SP retirement run completed gameplay but was correctly
rejected by runtime verification: staging removed an import library after the
initial inventory. Its failed report is retained; the verified rerun used an
unchanged staged runtime. `retirement-tests.log`, `source-tests/summary.json`,
`retirement-summary.json` and `verified-summary.json` retain the checks and
outcomes. Benchmark reports include the exact binary hashes.

The prototype proves overlap, not a performance gain. The first implementation
also copies static streams; reducing this cost requires measured, explicitly
retained immutable storage. Quiet paired mode 0/1/2 runs, the every-frame Quality
floor, physical display/input checks and Wayland/macOS runtime coverage remain
open under HF-07/HF-08 and REN-07. Shared-domain scene-packet capacity fallback
and the post-restart Vulkan `gfxInfo` bootstrap label remain separate known
issues.

### Mode comparisons and snapshot reuse, 4 November 2026

`tools/tests/renderer_overlap_benchmark.py` compares modes 0/1/2 in one
windowed stock-asset SP launch. It applies Quality, then selects uncapped
presentation with VSync off. Each mode occupies each order position once
(0/1/2, 2/0/1, 1/2/0), with a four-second warm-up rotation followed by an
eight-second measured rotation at the same position. Default drawable size is
1280 x 720. Frame-time means/percentiles and snapshot copy counters are kept
for every window. The ordinary gameplay harness verifies the runtime inventory,
captures the engine render target and rejects engine failures. Missing,
duplicated, mislabeled or invalid measurement windows fail the comparison.

Run from the repository, using a new evidence directory:

```text
python tools/tests/renderer_overlap_benchmark.py --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --output-dir .tmp/high-refresh-perf/comparison
```

When `psutil` is available, `host-load.json` records host CPU utilization and
competing games/build tools once per second. Missing monitoring does not imply
a quiet host. These windowed comparisons do not qualify physical presentation
or the native-resolution Quality floor.

`rendererFrameSnapshotStats reset` starts optional accounting;
`rendererFrameSnapshotStats sample` prints and stops it. Counters include
frames/views, copy calls/bytes, geometry/cache bytes, reused bytes and elapsed
snapshot microseconds. The time includes preparation and copying on the front
end, including any scheduling delay; bytes are copied payload/header bytes,
not total arena or process memory. Ordinary play leaves accounting disabled.

The first optimization shares exact source-address/byte-length ranges within
one submitted view. Triangle and cache headers remain separate records; repeated
vertex, index, auxiliary, palette and joint ranges reuse owned copies. The
lookup ends with that view, so another view rebuilding the same source address
receives fresh bytes. All geometry preparation runs before any copying,
including CPU fallback/cache updates reached through later light chains. No
mutable game storage is borrowed, and static streams are still copied each
view. Native tests execute this preparation order and the production copy
cache; negative controls detect borrowed source storage, size-blind reuse and
late preparation. The engine ownership self-test also covers distinct triangle
records sharing streams and a later view observing changed source contents.

The reuse lookup uses frame-arena storage, with no per-stream heap nodes or
individual frees. It grows at half occupancy to at most 65,536 slots. At its
limit, untracked ranges take the ordinary owned-copy path; existing entries
remain usable. Native tests exercise growth, collisions and this fallback with
40,000 distinct ranges. The bounded lookup does not impose a geometry limit.

The verified baseline and final candidate each completed all nine measurement
windows. Values below are medians of the three window statistics, in
milliseconds; lower is better. They are shared-machine observations, **not
performance qualification**.

| Build | Mode | Mean | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Before reuse | 0 | 9.034 | 9.00 | 12.85 | 14.05 |
| Before reuse | 1 | 7.175 | 6.90 | 10.70 | 11.55 |
| Before reuse | 2 | 10.953 | 10.95 | 14.35 | 16.90 |
| Bounded reuse | 0 | 14.787 | 14.75 | 21.45 | 27.30 |
| Bounded reuse | 1 | 12.872 | 11.65 | 22.45 | 34.35 |
| Bounded reuse | 2 | 14.493 | 14.35 | 20.40 | 24.30 |

The final mode-2 windows copied 8,790,400 bytes per frame and reused 4,637,978
bytes per frame: **34.54% of the requested copy bytes were avoided within that
run**. The earlier baseline copied 13,269,339 bytes per frame. Lookup-table
storage is additional arena memory and is excluded from these payload/header
counters. Frame-weighted elapsed snapshot time was 3.486 ms before reuse and
3.982 ms with bounded reuse; this does not establish a timing improvement.
Other games or build tools were present in all 163 baseline and 207 final
host samples, with mean host CPU utilization of 88.7% and 90.0%, respectively.
Even the unchanged inline mode slowed substantially between runs. Quiet
paired comparisons are still required before judging elapsed cost or choosing
a default.

Evidence is retained under `.tmp/high-refresh-perf/`: `baseline-verified/` and
`final/` contain comparison JSON, host samples, engine logs and gameplay reports
with exact runtime hashes. `baseline-instrumentation.patch` preserves the
instrumented baseline. `candidate/` and `candidate-unordered.patch` record the
intermediate heap-table implementation; it avoided 34.70% of copy bytes but
also ran under competing load. The initial `baseline/` attempt correctly
failed evidence parsing because engine tokenization split hyphenated labels;
the runner now uses underscores and the verified baseline passed.

The final Windows x64 build/stage and all 15 focused Meson tests pass, including
the new snapshot-copy and comparison-parser tests. Validation wiring and
repository path portability checks also pass. See `final-build.log`,
`final-install.log`, `final-tests.log` and `validation-hardening.log`.

Windowed stock-asset Vulkan SP, MP host/client and the OpenGL SP control pass
with the final staged binaries. Final submitted/completed checkpoints match:
988/988 SP, 1,032/1,032 MP host and 647/647 MP client. Each Vulkan role both
started and finished front ends while the backend was busy. Every role recorded
five latency-flash receipts and 128 draw samples with no gameplay during
drawing. Checks cover the ownership self-test, GPU skinning, TAA, motion blur,
scaling/dynamic resolution, shader reload and Vulkan surface recovery/restart.
Engine render-target captures were inspected; no OS capture or device-input
injection was used. `reuse-final-summary.json` records SP/GL; the final MP
record is `reuse-mp-clean-summary.json`.

The older temporary proof script sent an SP-only view sweep to MP, and the
shared gameplay harness also used SP's `viewpos` command in MP. The first MP
record is therefore superseded. The next run removed the sweep and correctly
failed the stricter unknown-command check on `viewpos`. The shared harness now
uses MP's registered `getviewpos`, with SP/host/client generation and parsing
covered by the comparison tests. The final MP rerun has no unknown commands;
MP coverage is stationary gameplay, not a view sweep. The broader benchmark
contract check also passes (`budget-contract-final.log`).

The MP host logged a reliable-message overflow during the client's shutdown
interval; this also appears in the earlier pre-optimization overlap evidence
(`.tmp/high-refresh-overlap/vk-mp-q4dm1-listen-validated/`). This remains an
unrelated MP issue, alongside the existing scene-packet capacity fallback and
post-restart Vulkan bootstrap label. Neither the copy-volume reduction nor
these functional runs close the quiet timing, physical display/input or
cross-platform qualification gates.

### Retained index streams, 4 November 2026

Mode 2 retains owned immutable index, silhouette-index and CPU index-cache
payloads across views/frames. The producer validates the complete byte range
before reuse; source addresses are lookup hints, not lifetime guarantees.
Changed contents or lengths replace the cached version. Submitted frame leases
keep old versions alive independently of eviction, model destruction or source
address reuse. Retirement releases leases after the worker joins, and frame
shutdown clears leases and cached payloads. The backend only reads these index
streams. Vertex, skinning, palette and other mutable streams retain their
existing per-view ownership path.

The cache holds at most 64 MiB of payload and 32,768 entries. Leases for in-flight
frames can temporarily retain evicted versions in addition to that budget.
Oversized or non-word-sized ranges use ordinary frame copies. Pressure
evicts least-recently-used entries until a new stream fits. Until 5 November
it cleared the whole cache, which stalled a frame; see
[long-frame attribution](#long-frame-attribution-5-november-2026).
Validation reads the full stream, so avoided copy bytes do not imply equivalent
CPU-time savings. `r_vkSnapshotRetain 0` disables retention and clears cached
entries at retirement; it does not invalidate submitted leases.

The comparison runner accepts `--retain-indexes 0` or `1` and records the
selection. Run each against the same staged binary in a new output directory.
The production-header native test covers unchanged reuse, changed contents,
length changes, live leases through clear/eviction, lease release,
capacity fallback and a stale-content negative control. The arena ownership
suite checks shutdown/reinitialization with the new cleanup hook.

Both same-binary runs passed all nine windows at windowed 1280 x 720 Quality,
uncapped with VSync off. Each cell below is the median of three window
statistics (milliseconds). These are shared-machine observations.

| Retention | Mode | Mean | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| off | 0 | 8.374 | 7.95 | 13.55 | 22.60 |
| off | 1 | 7.683 | 7.20 | 12.50 | 19.00 |
| off | 2 | 9.862 | 9.05 | 17.45 | 23.45 |
| on | 0 | 14.826 | 13.80 | 24.25 | 36.20 |
| on | 1 | 17.474 | 13.70 | 35.75 | 42.25 |
| on | 2 | 18.475 | 17.30 | 29.80 | 45.35 |

Mode-2 copied bytes averaged 8,534,227/frame with retention disabled and
7,568,048/frame enabled, an observed 11.32% reduction between these runs.
Elapsed snapshot time averaged 2.900 ms and 5.225 ms, respectively. Unchanged
modes 0/1 also slowed substantially; no timing improvement is established.
Competing games/build tools appeared in all 164 disabled and 169 enabled host
samples (mean host CPU 90.2% and 86.9%). Quiet paired runs remain required.

Evidence is under `.tmp/high-refresh-retain/off/` and `on/`, including full
window statistics, host load, engine captures, runtime hashes and launch
configuration. The Windows x64 build/stage and all 15 focused Meson tests
passed (`build.log`, `install.log`, `tests.log`). No renderer API change is
required by this iteration.

The final stock-asset Vulkan SP and MP host/client plus OpenGL SP control pass.
Submitted/completed counts match at 768/768 SP, 1,122/1,122 host and 863/863
client. Each Vulkan role starts and finishes front ends while the worker is
busy. All four roles record five flash receipts and 128 presentation samples
with no gameplay during drawing. Ownership self-tests, GPU skinning, TAA,
motion blur, scaling/dynamic resolution, shader reload and Vulkan surface
recovery/restart pass. Engine screenshots were inspected; tests were windowed
with no OS capture or device-input injection. See `retained-summary.json` and
its per-case gameplay reports. Path portability and validation wiring pass too.
The earlier MP shutdown overflow was not reproduced in this run; existing
scene-packet capacity fallback and post-restart Vulkan bootstrap labeling remain
outside this iteration.

### Quiet Quality floor baseline, 5 November 2026

All earlier Quality-floor timings ran beside a dozen peer builds and clients.
On 5 November the host was briefly quiet (9–37% CPU at launch, no other game
client), and four fullscreen runs were taken from `main` at `2fdb9fe98`
(`builddir`, debugoptimized, staged into `.install`). The workload was native
2560 x 1440 Quality on Vulkan: `game/airdefense1`, a 360° rotation every 12 s
from the start position, Remastered HUD, uncapped, VSync off. Windows taken
after peer builds resumed fell to 44–48 Hz with simulation-bound pacing, so they
are excluded. Times are in milliseconds.

| Run | Mean | p50 | p95 | p99 | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| `r_vkRenderThread 0` | 9.89 | 9.85 | 12.20 | 15.65 | 22.35 |
| `r_vkRenderThread 1` (two windows) | 9.49–9.66 | 9.35–9.55 | 11.90–12.60 | 12.95–16.95 | 14.08–66.18 |
| `r_vkRenderThread 2` | 9.71 | 9.65 | 11.90 | 12.90 | 18.87 |

In a quiet window the three render-thread modes are within noise of each
other, because this host is **GPU-bound** at Quality. With GPU timers on, a
2,016-frame capture in mode 0 measured 10.02 ms of GPU work per frame (p50
9.77, p99 12.95, maximum 14.88), and 2,014 of those frames exceeded 6.67 ms on
the GPU alone. The CPU side was 4.50 ms of frame building and 4.09 ms of
recording/submission per frame. Mode 2 overlaps those two, but cannot shorten
the GPU frame.

A 2,048-frame `r_vkGpuPhaseProfile` attributes the GPU frame: the main view
6.43 ms (maximum 10.44) and the portal sky 1.51 ms, plus a 0.16 ms target
resolve. Within the main view, SSAO takes 1.94 ms (0.39 ms depth capture,
1.55 ms drawing), lights 1.64 ms, bloom and tone mapping 1.08 ms, the depth
prepass 0.70 ms, shadow maps 0.60 ms, the ambient walks 0.83 ms and SMAA
0.22 ms. `profileFrameWork` puts the game session at 1.05 ms mean (p99 4.38 ms).

One launch alternated windows in mode 2 to bound what each area could give
back. Each value is a range over two rotated windows:

| Window | Present mean | p50 | p99 | Max |
| --- | ---: | ---: | ---: | ---: |
| Quality | 8.30–8.70 | 8.80–9.05 | 14.45–14.50 | 24.99–37.88 |
| `r_ssao 0` | 6.73–6.91 | 6.10–6.35 | 11.05–11.30 | 28.83–33.39 |
| `r_bloom 0` | 8.02–8.33 | 8.35 | 12.70–14.70 | 23.15–26.59 |
| `r_skipSubviews 1` | 8.58–8.71 | 8.90–9.10 | 12.55–12.65 | 19.75–20.79 |

`r_skipSubviews` does not remove the portal sky, so that row is the baseline
again. Even with SSAO disabled, the p99 is about 11 ms, and every
configuration has isolated 20–38 ms frames. Meeting the every-frame floor on
this reference therefore needs two things. The worst GPU frames, at about
15 ms, must fall below 6.67 ms, a cut of more than half that exact
reordering cannot supply. The isolated long frames must also be removed. That
is a workload decision for the Quality preset (SSAO resolution or sample
count, bloom, MSAA, or dynamic resolution), not a render-thread mode. Evidence
is under `.tmp/hf-floor/runs/` (`cycle-base`, `phases-base`, `frames0`,
`sens1`), with launch arguments and runtime hashes. `sens2` and the later
`cycle-base` windows ran under peer load and are not used.

### Half-resolution SSAO for Quality, 5 November 2026

The user chose to halve SSAO's resolution for Quality while Ultra keeps full
resolution. `r_ssaoResolution` (archived, default `0`) selects it on Vulkan.
`0` is automatic: full resolution at the maximum `r_ssaoSamples` (32, Ultra)
and half below it (Quality's 20, Enhanced's 12). `1` forces full resolution
and `2` forces half. The presets already own `r_ssaoSamples`, so no preset
target or SYSTEM catalog version changes. OpenGL keeps its full-resolution
pass.

The classic multiply pass becomes two draws (`VK_Post_DrawSSAOHalf`). The
first draws a half-size RGBA16F target. Each texel computes the AO of the even
pixel of its 2×2 block exactly as the full-resolution pass computes it there,
and stores that pixel's view depth, or a negative marker where sky or a
foreground model takes no AO. The second draws at full resolution: sky and
foreground pixels are untouched as before, and every other pixel blends one,
two or four neighbouring texels, its own block and, on odd rows/columns, the
next. A tap counts only within 4% of the pixel's view depth (at least two
units), so AO does not cross silhouettes; with no tap that close, the closest
in depth stands in. Even pixels therefore reproduce the full-resolution value.
`r_ssaoDebug` now shows the half-resolution AO when that path is active,
white wherever no AO applies. Native PBR's occlusion field and
`r_vkSSAOReferenceComposition` keep the full-resolution pass.

Image check. AO-only captures (`r_ssaoDebug 1`, particles and gun hidden) of
five fixed airdefense1 views compare full against half resolution on the pixels
that receive AO. Mean AO agrees within 0.002 in every view; the mean absolute
difference is 0.001–0.004 (under one level in 255) and the 99th percentile
0.018–0.043. The difference image shows only the noise pattern's texture, with
no halos at silhouettes. A Khronos validation-layer run through the full, half
and debug paths logged no messages.

GPU cost (paired windows in one fullscreen 2560 x 1440 Quality launch,
`r_vkRenderThread 2`, 600-frame `r_vkGpuPhaseProfile` per window):

| Windows | SSAO draw | SSAO depth copy | Main view | GPU frame |
| --- | ---: | ---: | ---: | ---: |
| Full, first pair (`gpuab1`) | 1.75 | 0.41 | 7.08 | 8.69 |
| Half, first pair (`gpuab1`) | 0.76 | 0.43 | 6.08 | 7.72 |
| Full, median of five (`gpuab2`) | 2.12 | 0.49 | 8.12 | 10.13 |
| Half, median of five (`gpuab2`) | 0.95 | 0.48 | 7.86 | 9.92 |
| SSAO off, median of five (`gpuab2`) | — | — | 4.10 | 6.09 |

Times are in milliseconds. Peer sessions' clients shared the GPU during
`gpuab2`, so whole-frame medians mix contended and quiet windows. The SSAO
draw is the robust measure: 55–57% cheaper in every pairing, about 1.0–1.2 ms
per frame. The first pair, run before the contention, removed 1.0 ms from the
main view.

The off windows show a cost the SSAO phases do not include. The world-depth
snapshot the prepass copies for SSAO (`VK_PostProcess_CaptureWorldDepth`)
ends rendering mid-prepass, resolves the whole 4x depth buffer and blits it.
In the quieter windows of a diagnostic A/B (`snap1`, a worktree-only switch,
not committed), the prepass took 0.79–0.84 ms with the snapshot and
0.32–0.33 ms without it (0.31 ms with SSAO off), and lighting took 1.70 ms
against 1.43–1.49 ms. The snapshot therefore costs about 0.75 ms per frame.
Removing it without changing the AO needs another source of world-only depth,
such as a separate single-sample world depth draw. That is the next measured
GPU lever.

A new `ssaoSnapshot` phase in `r_vkGpuPhaseProfile` times the copy itself
(`snapphase1`, three rounds). It takes 0.42–0.50 ms of the prepass, against a
prepass of 0.69–0.83 ms with SSAO and 0.25–0.32 ms without. Lighting then
costs about 0.13 ms more and the first ambient walk about 0.1 ms more, which
is smaller than the first estimate. A worktree-only timer around the flipped
blit in `VK_Exec_CopyRender` measured about 0.2 ms for all of a frame's depth
blits under heavy contention. Each depth capture's cost is therefore mostly
the render-pass resolve of the whole 4x depth buffer, which is bandwidth-bound.
This applies equally to the final-depth capture SSAO takes after the scene
(`ssaoDepth`, about 0.4 ms). A cheaper copy mechanism would recover little.
Only a world-depth source that never reads the multisampled buffer removes the
snapshot, at the cost of recording the world's depth draws twice.

Where the floor stands. In the quiet windows of `snapphase1`, the
half-resolution Quality frame averaged 6.90–7.42 ms of GPU work. Removing the
snapshot (about 0.45 ms net) and confining the portal sky (1.3–1.6 ms) to the
screen bounds of the main view's sky surfaces could bring the average under
6.67 ms. The portal-sky change needs a view-wide scissor in every Vulkan pass,
because a surface's scissor replaces the view's. However, the frames looking
at the most geometry run about 60% above the mean (the main view peaked at
10.4 ms against a 6.4 ms mean in the baseline), and frames that run a game tic
exceed 6.67 ms on the main thread alone. Exact optimization of the current
Quality workload therefore cannot hold every frame at or under 6.67 ms on this
reference. Closing the floor needs a further decision: a percentile budget,
further Quality reductions, or dynamic resolution.

The user chose the percentile budget: **p99 frame interval at or under
6.67 ms**, with the maximum reported. `airdefense_performance.py` now passes
`--max-p99-ms 6.6667` and no maximum gate, accepts `--render-thread 0|1|2`
(recorded as `renderThread`), and accepts `--runtime-dir` for a candidate
under `.tmp/stock-runtime/`.

A portal-sky clip was prototyped and rejected. The idea was to clear the
backdrop view's depth to 0 outside the main view's sky region so its passes
reject those pixels. airdefense1's compiled `.proc` contains no sky faces:
dmap leaves the openings empty, and the backdrop shows wherever the main view
draws nothing. Neither the main view's draw list nor its stage-less faces
locate the sky, and full-frame captures with the clip on lost the whole sky
(`pshots1`, `pshots2`: up to 2.4 million pixels changed against an off/off
capture noise of at most 27 levels). An exact clip would need the main view's
depth before the backdrop is drawn, which reorders the frame.

### Portal-sky mask, 5 November 2026

The main view's depth is now available before the backdrop is drawn, without
reordering the frame: `VK_PostProcess_PortalSkyMask` draws the world part of
it early. Before a command list runs, `VK_PostProcess_PlanPortalSkyMask` pairs
each portal-sky view with the next main view of the same world, viewport and
render target. At the backdrop's depth clear, the main view's world surfaces
(the SSAO world filter) are drawn depth-only into a single-sample target the
size of the framebuffer. `VK_Exec_DrawWorldDepth` uses the prepass's own
per-surface fill, `VK_Exec_FillDepthSurface`, extracted unchanged from
`VK_GuiExecutor_Draw3DView`. The depth is captured through the single-sample
copy path, so there is no multisampled resolve.

`post_skymask.frag` then sets the backdrop's depth: 1 wherever the captured
world depth, or any of its eight neighbours, is still far, and 0 elsewhere.
The neighbours cover multisampled silhouette pixels. Every depth-tested pass
of the backdrop rejects the 0 pixels before shading. This is exact because
the main prepass writes black over every opaque or perforated pixel it
covers. Translucent surfaces, alpha-tested holes and models leave far depth
and keep the backdrop behind them.

The same capture is the main view's SSAO world depth, so its prepass takes no
snapshot (`VK_PostProcess_WorldDepthCaptures` returns 0 for the plain SSAO
case; cel ink and the native-PBR split keep their captures).
`r_vkPortalSkyMask 0` restores the full backdrop and the snapshot. The mask
applies only while the backdrop's viewport equals its main view's.
Presentation scaling rescales views in place, and at 100% it is the identity.

Exactness. Full-frame captures at seven fixed views compared mask off, on and
off again, with game time stopped after positioning. Particles and the gun
were hidden and auto-exposure was off; the animated HUD bar is the only
off/off noise. With SSAO off (`pmask4`), six views are pixel-identical
outside the HUD. The seventh has scene animation that time stopping does not
freeze, and its off/off captures already differ by up to 153 levels. With
SSAO on (`pmask3`), the remaining differences are one-level AO changes on world
surfaces, plus thin lines of up to 57 levels on world silhouettes against the
sky. SSAO now reads world depth at pixel centres instead of multisample 0, so
an edge pixel whose first sample was sky now receives AO. A validation-layer
run through the mask, capture and SSAO reuse logged no messages (`pvalid1`).

Cost. Three peer games held the GPU at 96–100%, so absolute phase times were
unusable. Each window's phases are therefore divided by its bloom time, which
the mask does not touch (`maskab2`, four alternating rounds, medians):

| Per bloom | Mask off | Mask on |
| --- | ---: | ---: |
| GPU frame (main + portal + resolve) | 6.37 | 5.35 |
| Depth prepass | 0.73 | 0.28 |
| Portal-sky view, including the early world depth and mask | 1.26 | 1.00 |
| Main view | 4.81 | 4.19 |

The GPU frame falls by 16%, about 1.1 ms at the quiet bloom cost of about
1.1 ms. The snapshot is gone from the prepass, and the backdrop is cheaper even
after paying for the early world depth. The early pass records the world's
depth draws a second time on the backend thread. That CPU cost could not be
measured under the same contention and needs a quiet paired run.

First p99 qualification attempt. `airdefense_performance.py --render-thread 2`
ran three fullscreen Quality runs on the candidate runtime
(`.tmp/stock-runtime/hf-candidate`: half-resolution SSAO, portal-sky mask;
before the streamed-audio change `867fa47fd`). Each run started after three
consecutive 30-second checks found no game clients or builds, CPU under 25%
and GPU under 15%. The workload checks passed; pacing failed at 77.2, 58.9 and
57.6 FPS mean, with p99 26.65, 27.45 and 26.15 ms. The same build measured
about 105 Hz in probe rotations an hour earlier, and peer builds were running
again (79% CPU) when the runs ended. The quiet check guards only the start of
a six-minute run, so this records contention, not the build's p99.
Qualification needs a host that stays quiet throughout, or the reference
desktop. Evidence: `.tmp/airdefense-perf/p99-mode2-20261105-2142`.

Evidence is under `.tmp/hf-floor/runs/` (`dshots1`, `valid1`, `gpuab1`,
`gpuab2`, `snap1`), built from a detached worktree so that peer sessions'
staging could not replace the runtime under test.

### Long-frame attribution, 5 November 2026

`profileFrameWork` now writes each frame's start time on the presentation
clock (`startMs`, QueryPerformanceCounter on Windows) as the CSV's last
column. `tools/debug/cpu_sampler.py` records the same clock with every
sample, and its `frames` command places main-thread samples in the frames
that ran them. It compares frames over a threshold with the rest and lists
the longest frames' stacks. A 36-second fullscreen Quality sweep in mode 2
(`hitch1`, 3,600 frames, 26 over 14 ms) attributed the long frames to three
causes:

- **Retained-index cache clears.** Long draw phases of 20–23 ms were mostly
  frees of thousands of retained index streams. Under pressure,
  `idFrameSnapshotRetainedIndexes` cleared the whole cache (up to 64 MiB or
  32,768 streams) in one frame and re-copied everything afterwards. Frame-
  temporary geometry adds a new source address every frame, so the cache
  filled and cleared periodically. It now evicts least-recently-used streams
  one at a time until a new stream fits. Streams reused every frame stay
  resident, and one-shot streams age out at the tail. The production-header
  test covers recency order and the entry cap, and a whole-clear negative
  control fails it. The rerun (`hitch2`) has no bulk-free samples (eight in
  `hitch1`). Its overall timings ran under 52% host load and are not
  comparable. Retention's steady cost remains: validating each reused stream
  with `memcmp`, hashing and freeing evicted streams took about 7–11% of the
  main thread in these runs. Not retaining a stream until its address recurs,
  so that one-shot streams take ordinary frame-arena copies, is the next
  candidate, to be judged by a quiet A/B.
- **Window-message processing.** About a quarter of long-frame samples, against
  under 0.5% elsewhere, were inside SDL's event pump. Windows was initializing
  text-services/IME state and reading colour-profile settings there. The engine
  enables text input only for the console or a focused text field, and neither
  was active, so other windows' activity on this shared desktop drove these
  messages. Recheck on a quiet reference desktop before treating them as an
  engine cost.
- **Streaming sound starts.** `idSoundSystemLocal::Render` opens, seeks and
  decodes the first buffer of Ogg Vorbis streams on the main thread when a
  streamed voice starts, a few milliseconds each. Moving stream preparation
  off the main thread belongs with the audio work. Done the same day; see
  [streamed sound starts off the main thread](#streamed-sound-starts-off-the-main-thread-5-november-2026).

Evidence is under `.tmp/hf-floor/runs/hitch1` and `hitch2`
(`samples.json`, `frame-work.csv`, engine logs).

### Streamed sound starts off the main thread, 5 November 2026

The attribution's `hitch1` run caught 8 main-thread samples in Vorbis stream
work (6 of them inside frames over 6.67 ms). All were under
`idSoundVoice_OpenAL::QueueNextBuffer`: `SoundVorbisStream::Open`
(`start_decoder`, Huffman setup), `stb_vorbis_seek` (`seek_to_sample_coarse`
reading and inflating the PK4 member) and the first blocks'
`stb_vorbis_get_samples_short_interleaved`. Steady refills decoded on the
main thread as well.

Each streamed voice's `idSoundOggStream` now decodes one queue buffer at a
time as a HIGH-priority job (`s_asyncStreamDecode`, default 1, AUD-03). The
main thread keeps admission, the VFS open and every OpenAL call. The worker
parses the headers, checks them against the sample's load, applies a counted
length, seeks and decodes. The voice queues the block on a later update and
requests the next one at once. While a block is pending, `Update`, `UnPause`
and `IsPlaying` report the voice as starting rather than finished, so no
start is lost. A voice stopped or recycled meanwhile cancels its block and
parks the stream; `idSoundHardware_OpenAL::Update` frees parked streams once
their worker lets go, and `Shutdown` (`s_restart`, quit) joins them. With no
accepting worker the block decodes inline, as before. Implementation:
[audio modernization](audio-modernization.md#compressed-streaming-october-21).

Windows x64, MSVC debugoptimized, `aeb14a86f` plus this change, native
2560 x 1440 fullscreen Quality `game/airdefense1` hitch sweeps (36 s,
`profileFrameWork 3600`) with `cpu_sampler.py record` on the main thread and
`s_muteUnfocused 0`. Another session's full build loaded the machine for most
runs, so frame times here are not comparable with the quiet baseline; only
which thread does the work is measured. The first-block timing counters were
added between runs (client SHA-256 `b77c3728…5ebf9c` for `rot-dbg` and the
timed audit); the decode path is the same in every run. Runs are under
`.tmp/async-stream/runs/` and `.tmp/async-stream/scene-*`.

| Run | `s_asyncStreamDecode` | Stream blocks (worker / inline) | Main-thread Vorbis samples | First block: wait / decode |
| --- | ---: | ---: | ---: | --- |
| `rot-1` | 1 | 322 / 0 | 0 | — |
| `rot-dbg` | 1 | 319 / 0 | 0 | 23–24 ms / 1–2 ms (2 starts) |
| `test-async` | 1 | 14 / 0 | 0 | — |
| `rot-0` (control) | 0 | 0 / 319 | 4, all `idSoundOggStream::Decode` under `QueueNextBuffer` | 1.5 ms on the main thread |
| `test-inline` (control) | 0 | 0 / 54 | 3, under `QueueNextBuffer` | 2.1 ms on the main thread |

`rot-1`'s only main-thread sample in any `idSoundOggStream` frame was the
parked-stream sweep (`ReapReleased`), not decoding. No `stb_vorbis_*` or
`SoundVorbisStream` frame appears on the main thread with workers enabled.

`audio_scene_audit.py --repeat 2` over the `airdefense1`, `hangar2`, `medlabs`
and `dispersal` openings (hidden client, 60 FPS) passes all 13 checks with
workers and with `--stream-decode inline`. Both make 35 stream opens and play
the same streams in every opening, including on recycled voices, and their
residency matches within 8 KiB. Workers took 2,063 blocks and the main thread
none. Inline, first blocks cost the main thread 7.4 ms on average and up to
105 ms. With workers that work becomes start latency instead: 33 ms on average
and at most 95 ms, which includes waiting for the next 60 Hz poll. A further
single pass with worker timing (17 starts) separates the two: the worker spent
5.1 ms on average and at most 24 ms opening, seeking and decoding a first block,
and starts waited 23.3 ms on average. The longest wait, 68 ms, was
`hangar2_opening` at the map's first frames. It took 8 ms on the worker; the
rest was the length of those frames before the next poll.

A start is now heard about one to two frames later than its channel's clock
(23–33 ms on average in these runs), at the same sample position. An
expensive first block delays that one sound rather than stalling the frame.
The worker does not skip the block's first samples to catch up, so an onset
is never cut. `listSounds` reports worker and inline blocks, the first-block
wait and decode times and the slowest start, and `s_debugHardware 1` logs
each start.
