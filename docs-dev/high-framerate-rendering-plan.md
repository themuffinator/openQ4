# OpenQ4 High-Framerate Rendering Plan (2026-04-15)

## Purpose

This document defines a staged implementation plan for allowing true high-refresh presentation in OpenQ4 while preserving Quake 4 gameplay behavior and stock-asset compatibility.

The goal is not to blindly raise the simulation tick. The goal is to let the engine present frames at modern refresh rates while keeping the authoritative game simulation stable and compatible.

## Recommended Target

- Primary supported target: `240 FPS` presentation.
- Stretch target after stabilization: `360 FPS` presentation.
- Baseline gameplay simulation target: keep `60 Hz` authoritative game/usercmd timing unless a later, separate project proves higher simulation rates safe.

Rationale:

- `240 Hz` is a practical modern high-refresh target and a good fit for current PC hardware.
- `360 Hz` is worth keeping in scope, but it should be treated as a follow-up validation target rather than the first milestone.
- Raising the core simulation tick to `120/240/360 Hz` would touch prediction, networking, demos, scripts, physics, AI, animation, and content assumptions all at once.

## Current State

OpenQ4 is currently structured around a `60 Hz` usercmd / async-tic model:

- `src/framework/UsercmdGen.h`
  - `USERCMD_HZ = 60`
  - `USERCMD_MSEC = 1000 / USERCMD_HZ`
- `src/framework/Common.h`
  - `GetUserCmdMSec()` returns `16`
  - `GetUserCmdHz()` returns `60`
- `src/framework/Common.cpp`
  - `idCommonLocal::Async()` advances `com_ticNumber`
  - `idCommonLocal::SingleAsyncTic()` increments `com_ticNumber`
- `src/framework/Session.cpp`
  - `idSessionLocal::Frame()` waits for `latchedTicNumber >= minTic` before continuing, which effectively ties frame progression to new tics
- `E:\Repositories\OpenQ4-GameLibs\src\game\Game_local.cpp`
  - game code reads `common->GetUserCmdMSec()` / `common->GetUserCmdHz()`
  - render view generation and per-frame game flow are built around that cadence

Important detail:

- the current `GetUserCmdMSec() == 16` representation is only an approximation of `60 Hz`
- `16 ms` is actually `62.5 Hz`
- that should be corrected before higher-framerate work is trusted

## Guiding Decisions

1. Keep authoritative gameplay timing at `60 Hz` for the first implementation.
2. Decouple presentation from simulation instead of globally increasing `USERCMD_HZ`.
3. Treat interpolation as required for "true" high-framerate support.
4. Preserve demo, cinematic, BSE, and multiplayer behavior unless explicitly reworked.
5. Validate both single-player and multiplayer in actual gameplay, not only at the main menu.

## Non-Goals For This Plan

- Shipping a `120/240/360 Hz` gameplay simulation change.
- Reworking multiplayer protocol or snapshot frequency as part of the first milestone.
- Changing stock content timing to chase higher benchmark numbers.

## Primary Touch Points

Engine:

- `src/framework/Common.h`
- `src/framework/Common.cpp`
- `src/framework/Session.h`
- `src/framework/Session.cpp`
- `src/framework/UsercmdGen.h`
- `src/framework/UsercmdGen.cpp`
- `src/framework/Console.cpp`
- `src/renderer/RenderWorld.cpp`
- `src/renderer/RenderWorld_demo.cpp`
- `src/sys/win32/win_main.cpp`

GameLibs:

- `E:\Repositories\OpenQ4-GameLibs\src\game\Game_local.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\game\Player.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\game\Entity.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\game\Camera.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\mpgame\Game_local.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\mpgame\Player.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\mpgame\Entity.cpp`
- `E:\Repositories\OpenQ4-GameLibs\src\mpgame\Camera.cpp`

## Phase 0: Measurement And Safety Rails

Before changing behavior, add enough diagnostics to measure the real runtime cadence.

Status: `Complete` as of `2026-04-16`.

Tasks:

- Add temporary or permanent instrumentation for:
  - async tic cadence
  - presentation cadence
  - sleep overshoot / wake jitter
  - number of game tics consumed per rendered frame
- Extend existing `com_showFPS` / diagnostics if needed so high-refresh behavior is visible in-engine.
- Log whether the game is currently:
  - simulation-bound
  - vsync-bound
  - presentation-cap-bound

Exit criteria:

- We can prove the current frame loop behavior in SP and MP.
- We can identify whether a change improves frame pacing or only changes an FPS counter.

Current progress:

- Added `com_showFramePacing` with:
  - `0` off
  - `1` in-engine HUD overlay
  - `2` HUD overlay plus console logging
- Added async timing aggregation so the engine can report:
  - average / min / max async delta
  - async effective Hz
  - async work time
  - wake jitter against the intended `60 Hz` tic cadence
- Added session-side presentation diagnostics for:
  - presentation-frame delta / Hz
  - tic delta consumed per rendered frame
  - game tics run per rendered frame
  - requested vs actual wait time
  - average oversleep / wake jitter
  - current pacing classification (`simulation`, `vsync`, `presentation-cap`, `uncapped`)
- Added an in-engine frame-pacing overlay under `src/framework/Console.cpp` so the current pacing mode is visible during runtime without relying on an external profiler.
- Added a direct frame-pacing snapshot path plus active-MP frame sampling from the common frame loop so local multiplayer autoscreenshot validation no longer loses its post-load pacing data when `session->Frame()` is bypassed.
- Validated a staged SP run to gameplay on `game/airdefense1` with `com_showFramePacing 2`, including an autoscreenshot and post-load pacing logs that settle onto the expected `60 Hz` async cadence.
- Validated a staged local MP run to gameplay on `mp/q4dm1` with `com_showFramePacing 2`, including server spawn, in-map autoscreenshot, and pacing classification logging.

Validation notes:

- The automated SP run now produces representative post-load pacing samples once the loading-continue gate is skipped for scripted validation.
- The automated MP run now produces a live autoscreenshot pacing snapshot and final summary after map load, so detached local-client validation no longer depends on the HUD overlay to confirm that post-load pacing data survived through teardown. Focused manual MP inspection is still the better source for representative presentation-Hz readings.

## Phase 1: Correct The Base 60 Hz Timing

The current `16 ms` approximation should be replaced with an exact representation of `60 Hz`.

Status: `Complete` as of `2026-04-16`.

Tasks:

- Replace hardcoded `16 ms` assumptions with a precise `60 Hz` representation.
- Prefer fractional accumulation or a numerator/denominator representation over integer truncation.
- Audit all callers that currently use:
  - `GetUserCmdMSec()`
  - `USERCMD_MSEC`
  - raw `16`-based assumptions tied to gameplay time
- Keep external gameplay behavior equivalent to the current intended `60 Hz` cadence.

Notes:

- This phase is a prerequisite for trustworthy high-refresh work.
- If exact timing requires API expansion, add a more precise helper rather than forcing everything through integer milliseconds.

Current progress:

- Added exact 60 Hz helper math in the engine and companion GameLibs instead of routing all timing through truncated `16 ms` integers.
- Moved async tic pacing and frame timestamps onto exact tic-time accumulation in the engine.
- Updated SP and MP game-frame bookkeeping, prediction/snapshot timing, and several one-tic gameplay checks to use exact tic timestamps.
- Moved generic script `wait` scheduling onto exact tic alignment in the SP and MP game libs so one-tic waits no longer round through legacy integer-millisecond timing.
- Switched script-facing frame-time queries to return the exact base-tic duration instead of alternating between truncated `16/17 ms` values.
- Replaced more “previous frame” camera, AI, and vehicle animation sampling that still treated the current frame span as a stand-in for the last exact tic.
- Updated cinematic end transitions to hold their one-frame post-stop state until the next exact tic instead of adding the current frame span.
- Cleaned up additional UI, loader, mini-game, and BSE one-frame assumptions so they no longer quietly run at `62.5 Hz`.
- Fixed remaining “next frame” gameplay hold/snapshot cases that were still using the current frame span as a proxy for the next exact tic.
- Taught the remaining legacy network one-tic defaults (`net_clientPrediction` and `net_clientLagOMeterResolution`) to follow the exact base-tic duration instead of hard-wiring `16 ms`.
- Added phase-0 runtime diagnostics alongside the cleanup so the exact-`60 Hz` baseline can be verified in-engine instead of inferred from code inspection alone.
- Revalidated the cleanup in staged SP and MP gameplay runs after the timing work landed.

Follow-up watch items:

- Keep an eye out for any smaller archived tuning defaults that may still deserve an explicit exact-tic alias during future validation, but treat that as opportunistic cleanup rather than a blocker for phase completion.

Exit criteria:

- Async tic pacing reflects true `60 Hz` timing.
- No obvious SP/MP behavior regression from the timing cleanup alone.

## Phase 2: Separate Presentation From Game-Tic Gating

OpenQ4 currently waits for a new tic before `idSessionLocal::Frame()` proceeds. That is the main architectural blocker.

Status: `Complete` as of `2026-04-16`.

Tasks:

- Refactor `idSessionLocal::Frame()` so the engine can render a presentation frame even when no new game tic has arrived.
- Keep `RunGameTic()` on its `60 Hz` cadence.
- Introduce a dedicated presentation cap cvar.
  - Suggested user-facing name: `com_maxfps`
  - Suggested supported range: `0` uncapped, otherwise clamp to a safe upper bound such as `1000`
- Add clear interaction rules for:
  - `r_swapInterval`
  - demos
  - cinematic playback
  - loading screens
  - minimized / background behavior

Design intent:

- multiple render frames may occur between simulation tics
- simulation still advances only when its own cadence says it should

Exit criteria:

- Engine can present above `60 FPS` without increasing the simulation tick.
- SP and MP remain functional with repeated-state rendering.

Current progress:

- Added `com_maxfps` as the new presentation-cap cvar in the common frame loop.
- Moved presentation throttling into `idCommonLocal::Frame()` so foreground rendering no longer depends on `idSessionLocal::Frame()` sleeping for new async tics.
- Kept demo playback and fixed-rate capture on explicit tic waits so the repeated-state path does not silently alter those timing-sensitive modes.
- Refactored normal single-player `idSessionLocal::Frame()` flow so it can render repeated-state frames when no new game tic has arrived, while still only running game tics when the authoritative async cadence advances.
- Added a Windows hidden/minimized safety clamp so a decoupled presentation path does not spin uncapped in the background.
- Switched the presentation-cap scheduler to SDL's high-resolution performance counter after validation exposed that the older millisecond sleep path undershot low caps too aggressively and the legacy Windows sys clock helpers were not a safe source for frame pacing.
- Refactored the foreground async-network path so listen-server / client netplay no longer blocks the render loop in `AsyncClient::RunFrame()` and `AsyncServer::RunFrame()` while waiting for the next `60 Hz` game frame; dedicated-style paths keep the old blocking behavior because they are not presenting repeated-state frames.
- Extended the same presentation throttle / real-time clock to blocking GUI loops (`idCommonLocal::GUIFrame()` and `ShowLoadingGui()`), so load screens and modal GUI redraw paths no longer bypass the new phase-2 pacing policy.
- Extended the blocking map-load pacifier and post-load loading-screen loops to the same presentation-time policy, so `PacifierUpdate()`, loading-bar ease-out, and the scripted post-load continue/menu transition no longer fall back to legacy one-tic redraw pacing whenever `com_maxfps` explicitly requests higher presentation rates.
- Extended the blocking wipe-completion loop to the same presentation-time scheduler, so `CompleteWipe()` no longer bypasses `com_maxfps` during map-transition fade completion.
- Added timed modal-GUI pacing harnesses for both wait-box and standard message-box flows, and fixed the scripted modal test path so it no longer consumes queued console commands from inside its own GUI pump loop.
- Hardened `syncNextGameFrame` so game-code requests from cinematic/savegame handoff paths now wait for the next real async tic instead of consuming an extra game frame early on a repeated-state presentation frame.
- Moved cinematic camera-view timing in the SP and MP player render paths onto a presentation-time source, so camera materials and cinematic HUD redraws continue to animate on repeated-state presentation frames instead of stalling at the `60 Hz` game-tic cadence.
- Added direct SP/MP cinematic validation commands (`cinematicStatus`, `listCinematics`, `startCinematic`, `stopCinematic`, `skipCinematic`) so phase-2 cinematic coverage no longer depends on brittle scripted trigger chains.
- Validated a staged single-player gameplay run on `game/airdefense1` with `r_swapInterval 0` and `com_maxfps 240`; the autoscreenshot snapshot now reports `present=11.03 ms (90.7 Hz)` while `async=16.67 ms (60.0 Hz)`, confirming presentation can outrun the simulation cadence without raising the sim tick.
- Validated a staged single-player gameplay run on `game/airdefense1` with `r_swapInterval 0` and `com_maxfps 30`; the autoscreenshot snapshot reports `present=33.83 ms (29.6 Hz)` with `async=16.67 ms (60.0 Hz)` and `bound=presentation-cap`, confirming the new cap path now throttles to the requested neighborhood instead of oversleeping into the mid-20s.
- Revalidated staged local multiplayer gameplay on `mp/q4dm1` with `r_swapInterval 0` and `com_maxfps 240` after the async-network change; the autoscreenshot snapshot now reports `present=9.59 ms (104.3 Hz)` while `async=16.67 ms (60.0 Hz)`, confirming foreground MP is no longer effectively hard-gated to one presentation frame per async tick.
- Revalidated the staged loading-screen / disconnect-to-menu flow on `mp/q4dm1` with `r_swapInterval 0` and `com_maxfps 240`; the loading GUI now stays in the expected high-refresh range instead of free-running into multi-kHz redraw, and the scripted snapshots report `present=5.47 ms (182.7 Hz)` in-game before disconnect plus `present=7.40 ms (135.2 Hz)` after returning to the main menu, confirming the phase-2 pacing path now carries through the map-load and menu transition.
- Revalidated the staged local MP load / disconnect flow again on `mp/q4dm1` after the pacifier and post-load-loop update; the early loading sample now reports `present=4.90 ms (204.2 Hz)` while the settled scripted snapshots report `present=9.37 ms (106.7 Hz)` in-game before disconnect plus `present=4.24 ms (236.0 Hz)` after returning to the main menu, confirming the remaining blocking load-screen path no longer collapses back to one presentation frame per `60 Hz` tic when `com_maxfps 240` is requested.
- Revalidated a staged single-player transition to `game/airdefense1` with `com_wipeSeconds 2`, `r_swapInterval 0`, and `com_maxfps 240`; the run completed through the modified blocking wipe path and the post-load snapshot reports `present=5.19 ms (192.6 Hz)` while `async=16.67 ms (60.0 Hz)`, confirming the transition still reaches repeated-state high-refresh gameplay after the wipe update.
- Revalidated fullscreen single-player gameplay with `r_swapInterval 1` and `com_maxfps 240`; on the current high-refresh display the autoscreenshot snapshot reports `present=6.97 ms (143.5 Hz)` while `async=16.67 ms (60.0 Hz)`, confirming vsync now caps presentation to display refresh instead of silently collapsing phase-2 behavior back to `60 FPS`.
- Revalidated fullscreen single-player gameplay with `r_swapInterval 1` and `com_maxfps 30`; the autoscreenshot snapshot reports `present=35.48 ms (28.2 Hz)` with `async=16.67 ms (60.0 Hz)` and `bound=presentation-cap`, which is consistent with a low requested cap being quantized to the monitor refresh divisor instead of landing on an arbitrary exact `30.0 Hz` under vsync.
- Revalidated staged single-player gameplay on `game/airdefense1` with `r_swapInterval 0` and `com_maxfps 240`, then exercised both modal GUI coverage paths in-session: the timed message-box snapshot reports `present=4.16 ms (240.5 Hz)` and the timed wait-box snapshot reports `present=4.16 ms (240.2 Hz)`, confirming the standard modal GUI loops now stay on the same presentation-timed path as gameplay instead of collapsing back to one redraw per async tic.
- Revalidated staged single-player cinematic handling on `game/airdefense1` with `r_swapInterval 0`, `com_maxfps 240`, and `g_autoSkipCinematics 0`; after map load, `startCinematic cin_opening` and `skipCinematic` produced a live handoff in the log (`cinematic entered`, `skipCinematic: requested=1`, `cinematic exited`, `syncNextGameFrame requested by game code`, `syncNextGameFrame consuming next async tic`) while the active cinematic stayed on the repeated-state presentation path at roughly `137-176 Hz`, confirming the phase-2 cinematic skip/exit path now survives high-refresh presentation without prematurely consuming an extra game frame.
- Revalidated the single-player loading-continue gate itself on `game/airdefense1` with `com_skipLoadingContinue 0`, `com_loadingContinueAutoAdvance 1000`, `r_swapInterval 0`, and `com_maxfps 240`; the clean staged run now logs `Loading continue gate entered (auto-advance 1000 ms)` followed by `Loading continue gate completed via auto-advance after 1004 ms`, then settles into repeated-state presentation at roughly `120-130 Hz`, confirming the last blocking SP post-load gate remains on the phase-2 presentation-timed path instead of collapsing back to one redraw per async tic.

Interaction notes from current validation:

- `r_swapInterval 1` does not imply `60 FPS`; on a high-refresh display it still allows presentation above `60 FPS` while the simulation remains at `60 Hz`.
- When `com_maxfps` is lower than display refresh and vsync is enabled, the effective presentation cadence follows the display's refresh quantization. Expect the result to land near the nearest refresh divisor rather than an exact arbitrary cap such as `30.0 Hz`.
- Windowed vsync validation is less authoritative than fullscreen on the current SDL3 path because compositor behavior can mask whether swap-interval control is actually the limiting factor.

## Phase 3: Presentation Interpolation

Repeated-state rendering is not enough for "true" high-framerate support. Motion smoothness requires interpolation.

Status: `Implementation complete; final manual validation in progress` as of `2026-04-17`.

Tasks:

- Add interpolation state between previous and current simulation snapshots.
- Start with the local player and camera path:
  - `Player.cpp`
  - `Camera.cpp`
  - render view generation
- Then expand to common entity presentation paths:
  - entity transforms
  - view weapon / first-person presentation
  - projectiles and effects that visually stutter at `60 Hz`
- Use interpolation only for presentation state.
- Do not feed interpolated state back into gameplay, collision, prediction, or networking logic.

Initial interpolation scope:

- camera origin and axis
- FOV transitions
- first-person weapon placement
- local player body / entity transform presentation

Follow-up interpolation scope:

- moving entities
- projectiles
- client effects / BSE-bound visual elements
- remote players in MP

Exit criteria:

- Camera and first-person motion are visibly smoother above `60 FPS`.
- Input feel remains tied to the real authoritative simulation, not a fake smoothing layer.

Current progress:

- Started the first local-player interpolation slice in the SP and MP game libs.
- `idGameLocal::Draw()` and `idMultiplayerGame::Draw()` now recalculate the active player's render view on each presentation frame instead of only reusing the last game-tic render view.
- Added previous/current first-person presentation snapshots in `Player.cpp` / `Player.h` for SP and MP, then blended:
  - camera origin
  - camera axis
  - player FOV
  on repeated-state presentation frames.
- Added a presentation-only `rvViewWeapon` refresh path so the first-person weapon/viewmodel follows the interpolated camera between game tics without advancing weapon scripts or gameplay state again.
- Moved simple camera-view FOV evaluation in `Camera.cpp` onto presentation time so FOV blends on static/attached cameras no longer quantize to the `60 Hz` game-tic cadence.
- Extended `idCameraView::GetViewParms()` in the SP and MP game libs to read attached/static camera origins and axes from presentation-space entity transforms, so camera entities bound to moving actors or props no longer step once per simulation tic on repeated-state frames.
- Added presentation snapshot blending to `idCameraAnim::GetViewParms()` for exact-`60 Hz` cinematic camera defs in the SP and MP game libs, so camera origin/axis/FOV now interpolate between simulation snapshots on repeated-state frames while frame commands, cut handling, and loop/stop transitions remain driven by the authoritative game-tic path.
- Added per-entity previous/current render-transform snapshots in the SP and MP `idEntity` base class, then used a repeated-state render-only update path to interpolate selected entity origins/axes without feeding that state back into gameplay or prediction.
- The active viewed player now refreshes their player body, head attachment, and world-weapon model through that presentation-only entity-transform path during `Draw()`, so those local player-world visuals no longer stay hard-locked to the last `60 Hz` simulation snapshot on repeated-state frames.
- Extended the MP `Draw()` path to refresh the same presentation-only entity interpolation for every in-scene player in the viewed instance, so remote player bodies now ride the repeated-state render path too instead of only the actively viewed player receiving transform smoothing.
- Added a projectile presentation refresh path in the SP and MP draw loops that walks spawned in-instance projectiles on repeated-state frames and updates their interpolated render transform without advancing projectile gameplay again.
- Added projectile light snapshots alongside the render-transform path so attached projectile dynamic lights now follow the same repeated-state interpolation instead of jumping once per `60 Hz` simulation tick.
- Added a broader repeated-state active-entity presentation pass in the SP and MP draw loops that walks non-player active entities in the viewed instance and refreshes their interpolated render transform, extending transform smoothing to movers, AI-driven entities, and other active scene objects that already ride the normal entity presentation path.
- Kept the generic active-entity pass clear of the bespoke player, projectile, and first-person view-weapon paths so those earlier phase-3 presentation hooks remain authoritative for their special cases.
- Expanded the repeated-state client-entity presentation pass in the SP and MP draw loops beyond `rvClientEffect`, so active `rvClientEntity` instances can now refresh presentation-only render state on repeated frames without re-running their gameplay `Think()` path.
- Bridged `rvClientEffect` / `rvClientCrawlEffect` onto that generic presentation pass, keeping presentation-time bind/joint sampling for attached effects and crawl trails so those visuals no longer stay locked to pure game-tic servicing.
- Added repeated-state `rvClientModel` presentation refresh in SP and MP, so bound client models now resample their bind/joint placement on presentation frames and update their render defs between `60 Hz` simulation tics.
- Added presentation transform snapshots to `rvClientMoveable` in SP and MP, then used them to interpolate repeated-state origin/axis updates, presentation-time scale, sound-emitter origin, and trail-effect follow transforms without feeding the blended state back into gameplay.
- Added a weapon-level repeated-state presentation hook and used it for the SP/MP lightning gun, so its manually owned beam/trail endpoints and tube-joint offsets now refresh on presentation frames instead of staying quantized to the authoritative game-tic path.
- Extended that same weapon-level repeated-state path through the SP/MP rocket launcher and nailgun guide visuals, so zoomed guide markers, lock cursors, and guide-effect placement now follow presentation-time view updates and target motion instead of stepping only on simulation ticks.
- Extended the weapon-owned manual-effect coverage through the SP/MP gauntlet contact effect, so an existing wall/flesh impact effect now re-traces from the presentation-time view and follows repeated-state camera motion instead of waiting for the next simulation tick to move or clear.
- Extended the repeated-state non-model hook through the SP/MP hover-enemy ground-contact effects, so Strogg Hover and Heavy Hover Tank dust/contact visuals now resample their presentation-time hover joints and ground traces instead of keeping those looping effects pinned to the last simulation-tic sample.
- Extended that same active-entity non-model path through the SP/MP Repair Bot repair visuals, so the repair beam end-point and impact effect now resample their presentation-time arm trace and moving-owner basis instead of waiting for the next simulation tick.
- Extended the SP/MP Makron lightning-sweep servicing onto repeated-state presentation frames, so the sweep bolt/impact/muzzle effects now follow presentation-time joint motion and in-flight sweep interpolation without re-running damage or advancing the sweep state machine off the authoritative tick.
- Extended the repeated-state vehicle-part presentation coverage through the SP/MP vehicle-weapon guide effect and hoverpad dust effect, so those bespoke vehicle-owned visuals now resample presentation-time target motion, driver view axis, and hover traces instead of staying pinned to the last simulation tick.
- Extended repeated-state presentation through SP/MP `func_fx` look-at-target servicing, so looped effect entities that continually aim at `target_null` targets now refresh their beam/end-origin direction from presentation-time entity transforms instead of only retargeting on simulation ticks.
- Split BSE render servicing between authoritative owner time and presentation time, so BSE-managed particle updates, lifetime checks, and looping now advance on repeated-state render frames while owner interpolation/spawn timing still keys off simulation snapshots instead of feeding presentation time back into gameplay-facing effect state.
- Added a repeated-state non-model-visual hook for active entities and used it to refresh several entity-owned dynamic lights on presentation frames, including `idLight` transforms, actor flashlights, and the Strogg Hover headlight attachment so those lights follow interpolated motion instead of stepping once per simulation tic.
- Extended the repeated-state non-model-visual coverage through `rvVehicle` position/part ownership so vehicle lights and vehicle-weapon muzzle-flash lights now refresh against interpolated vehicle/joint presentation in SP and MP instead of only moving on post-physics game ticks.
- Added repeated-state `idExplodingBarrel` non-model-visual refresh for the burn light / IPS attachment path so those temporary visuals follow the interpolated barrel presentation instead of staying quantized to the last simulation snapshot.
- Added `pm_presentViewBias` in the SP and MP player view paths so the first-person camera/FOV/viewmodel interpolation can bias toward the latest authoritative snapshot on repeated-state frames, reducing the visible one-tic local-view lag without extrapolating gameplay state or remote entity presentation.
- Added a same-fire-frame presentation refresh path for SP/MP first-person view-weapon client effects and moved local/spectated hitscan cosmetic origin sampling onto the presentation-space weapon transform, so muzzle flashes and tracer/path starts no longer stay pinned to the last authoritative weapon pose while the camera is moving.
- Extended the repeated-state light-side coverage through the MP special-carrier halo lights owned by `idMultiplayerGame`, so CTF/powerup render lights now resample their carrier from presentation-space player transforms on repeated-state frames instead of stepping only when `CheckSpecialLights()` runs on the simulation tic.
- Extended the repeated-state effect-side coverage through the SP/MP vehicle crash/scrape effect owner, so persistent world-space crash FX now interpolate their contact origin/axis/attenuation between collision snapshots on repeated-state frames instead of stepping only when the next `60 Hz` collision update rewrites the effect.
- Extended the repeated-state effect-side coverage through the SP/MP projectile fly/trail attenuation path plus the player haste/flag/arena powerup-effect parameters, so projectile trails and bound powerup visuals now refresh their attenuation/local-origin presentation state on repeated-state frames instead of stepping only when gameplay ticks rewrite those effect parameters.
- Extended the repeated-state light-side coverage through SP/MP weapon-owned render lights, so active muzzle-flash, world muzzle-flash, flashlight, and GUI light defs now refresh from presentation-space weapon/player transforms on repeated-state frames instead of remaining pinned to the last simulation-tic weapon pose.
- Added SP/MP discontinuity guards to first-person presentation snapshots and moved view-weapon child-effect refresh onto repeated-state presentation frames, so listen-server prediction corrections, teleports, and other large one-tic local-view jumps now snap cleanly instead of slinging the client view weapon, muzzle effects, or tracer starts across the screen while moving.
- Tightened the MP first-person presentation path so active prediction-error decay now disables one-tic first-person interpolation for the affected view snapshot, preventing listen-server clients from blending the view weapon against a moving correction target while local prediction smoothing is still settling.
- Reworked the MP first-person view-weapon presentation path to capture per-tic player-view and full viewmodel transforms, then interpolate that captured weapon pose directly on draw frames instead of rebuilding the weapon from an interpolated camera plus live weapon-lag inputs, eliminating the mixed-state listen-server wobble path that could still show up while moving.
- Hardened the MP listen-server first-person weapon path against prediction reruns by keying presentation continuity to real sim-time deltas instead of `framenum` churn, moving view-angle weapon-lag history onto a dedicated sim-frame log, and collapsing duplicate same-tic weapon-acceleration samples, preventing rerun-induced history corruption from exaggerating or jittering the local view gun while moving or when movement input first changes.
- Finished tightening that MP view-weapon boundary so `rvWeapon::Think()` now only recaptures the authoritative viewmodel transform on real simulation frames, leaving repeated-state render frames to the presentation-only draw hook instead of letting the weapon rebuild a fresh live transform and reintroduce forward shove/jitter while the client is walking.
- Retuned the SP/MP `pm_presentViewBias` behavior so the default path falls back to straight interpolation again and any optional bias now eases toward the newest snapshot without reaching it early and stalling mid-tic, addressing the remaining “surge forward then jitter” first-person feel reported on listen-server clients at high presentation rates.
- Tightened that MP first-person view-weapon presentation boundary again so repeated-state draw refresh now applies the interpolated viewmodel transform to the render entity and view-light joints without writing that presentation-space pose back into the shared weapon state, preventing the listen-server local view gun from inheriting render-only forward offsets and reintroducing the lunge while moving.
- Unified the MP draw path with the SP render-prep sequence so `idMultiplayerGame::Draw()` now recalculates the render view, refreshes presentation-space player/world/view-weapon transforms, and only then calls `RenderPlayerView()`, closing the last listen-server gap where the local view gun could still render from a stale simulation pose and lunge forward while walking.
- Extended that unified first-person draw refresh to bound view-weapon client effects on both new and repeated frames, so muzzle flashes and related firing FX now follow the corrected presentation-space gun immediately instead of lingering at the pre-correction pose for the fire frame.
- Narrowed the lightning-gun follow-up to the local first-person beam only: the draw-time presentation path now refreshes `trailEffectView` from the corrected presentation-space muzzle/chest joint and a presentation-time local beam trace on both simulation and repeated-state render frames, while the authoritative world/chain-lightning effects stay on the normal gameplay update path.
- Extended the bespoke visual-owner follow-up through SP/MP gib skeleton side-models and the custom `idSecurityCamera::Present()` path, so repeated-state render frames now resample those visuals from presentation-space transforms instead of leaving them pinned to the last `60 Hz` simulation update.
- Extended the camera-side follow-up through remote entity render views, security-camera feeds, portal-sky/playback camera sources, and the SP/MP steam-pipe side model, so repeated-state render frames now sample presentation-space camera origins/axes or interpolate playback/body-side transforms instead of stepping those views and side visuals on the last simulation tic.
- Continued the bespoke visual/effect audit through SP/MP `idMultiModelAF` side-model ownership plus the MP jump-pad activation effect origin path, so repeated-state frames now interpolate those extra AF body render defs and the MP jump-pad FX now spawn from the same centered physics-space origin the SP path already uses instead of an older render-entity sample.
- Continued the bespoke render-owner audit through SP/MP `idBrittleFracture`, so repeated-state frames now rebuild the dynamic fracture callback model from per-shard presentation snapshots instead of pinning dropped shard geometry to the last simulation tic, and the fracture entity stays scene-active through the last interpolation interval so settling shards do not snap on their final step.
- Fixed the MP non-predicted projectile terminal-snapshot path so remote clients now reconstruct late-arriving authoritative `EXPLODED` / `IMPACTED` projectile snapshots into visible client-side detonate/impact effects instead of simply hiding the projectile, restoring host-fired projectile hit visuals for joiners even when the projectile ends before they ever saw a launched state.
- Reworked the MP remote hitscan cosmetic path so the legacy hitscan trace message now stays narrow and clients keep using it only for path/tracer replay, while the server mirrors the authoritative impact effect through a separate unreliable impact packet to all relevant remote viewers, including the firing client, and the replay path bypasses local impact-rate throttling while applying hitscan tint only for defs that explicitly request it, restoring sustained host-fired and self-fired bullet/shotgun surface impacts and keeping railgun-class impact coloring intact without depending on a late retrace to rediscover the same hit.
- Closed the remaining code-side bespoke owner audit after the final MP `rvViewWeapon` child-FX parity fix: the surviving persistent effect and render-light owners now either refresh through the generic `rvClientEntity`, entity non-model, projectile, weapon/view-weapon, or vehicle-part presentation paths, or were already covered by the earlier bespoke hooks, so no additional Phase-3-only render/effect/light code gaps remain in the current SP/MP game-lib surface.
- Revalidated an automated staged SP run on `game/airdefense1` with `r_swapInterval 0`, `com_maxfps 240`, `com_skipLoadingContinue 1`, and `g_autoSkipCinematics 0`; the map still enters the opening cinematic on the repeated-state presentation path and the live pacing logs settle into roughly `191-232 Hz` presentation while the simulation remains at `60 Hz`, confirming the current Phase 3 build still survives real in-map cinematic entry after the late bespoke-owner fixes.
- Revalidated an automated staged listen-server bring-up on `mp/q4dm1` with `r_swapInterval 0` and `com_maxfps 240`; both the host and the local joiner reach in-map multiplayer play from the current Phase 3 build, and the settled pacing logs on both sides hold near the requested `240 Hz` presentation target while the simulation remains at `60 Hz`.

Manual validation still needed before Phase 3 sign-off:

- Complete a human gameplay feel pass for the new view interpolation in SP and MP at `com_maxfps 240`, especially while moving, taking abrupt camera kicks, teleporting, and recovering from other large one-tic first-person deltas.
- Decide whether any non-zero default for `pm_presentViewBias` is still desirable after the current fixes; if so, tune it from gameplay feel rather than static analysis so the view remains responsive without regaining the earlier lunge/jitter behavior.
- Finish a manual cut-heavy / looping-cinematic pass beyond the automated opening-cinematic bring-up, confirming that presentation-time camera interpolation still snaps cleanly across authored cuts and stays stable through loop/stop/hand-off transitions.

## Phase 4: High-Refresh Compatibility Pass

Once decoupled presentation and interpolation are working, audit systems that can break at high presentation rates.

Systems to verify:

- demo record / playback timing
- AVI capture path
- cinematics
- GUI timing and cursor behavior
- console FPS display and diagnostics
- BSE effect servicing and effect timestamps
- animation presentation assumptions
- sound synchronization expectations
- viewmodel depth hack and subview rendering

Special attention:

- `src/renderer/RenderWorld_demo.cpp`
- `src/framework/Session.cpp`
- `src/renderer/tr_light.cpp`
- any render path using `renderView.time`

Exit criteria:

- No obvious time-base desync between presentation, demos, and special effects.
- High-refresh mode is stable across normal gameplay systems.

## Phase 5: Supported Cap And User Exposure

After the architecture is stable, lock down the officially supported high-refresh target.

Recommendation:

- Officially support `240 FPS`.
- Allow higher values for experimentation.
- Treat `360 FPS` as supported only after dedicated validation passes succeed.

User-facing behavior to define:

- final cvar name and help text
- default value
- interaction with vsync
- whether `0` means uncapped
- whether menu / background / dedicated modes use separate caps

## Alternative Path Explicitly Rejected For The First Milestone

Do not start by raising `USERCMD_HZ` from `60` to `120/240/360`.

Why:

- game code consumes `common->GetUserCmdMSec()` and `common->GetUserCmdHz()` directly
- multiplayer prediction and async networking are built around the existing cadence
- demo capture / playback and timing-sensitive systems assume the current model
- content behavior may silently drift even if the engine appears stable

If higher simulation rates are ever pursued, that should be a separate design document after the presentation path is already decoupled.

## Validation Matrix

Per phase, validate at minimum:

- SP launch task to in-game movement and combat
- MP launch task to in-game movement and combat
- log review via `.home\\baseoq4\\logs\\openq4.log`
- vsync off and vsync on
- windowed and fullscreen
- low cap, `240`, and uncapped modes

Specific checks:

- camera pans feel smoother above `60 FPS`
- no runaway CPU spin when capped
- no accelerated or slowed gameplay
- no broken demo timing
- no stuck or jittering GUI cursor
- no visible weapon/viewmodel wobble caused by interpolation mismatch
- no BSE or particle timing anomalies during gameplay

## Suggested Delivery Order

1. Phase 0 instrumentation.
2. Phase 1 exact `60 Hz` cleanup.
3. Phase 2 presentation-frame decoupling.
4. Phase 3 local-player / camera interpolation.
5. Phase 4 compatibility sweep.
6. Expose and document `240 FPS` as the first supported high-refresh target.

## Definition Of Done For The First Milestone

The first milestone is complete when:

- OpenQ4 can present at `240 FPS`
- gameplay simulation still runs at the intended `60 Hz`
- camera and first-person presentation are genuinely smoother than the current build
- SP and MP gameplay remain stable
- no major regressions are found in demos, cinematics, GUI, or BSE-heavy scenes
