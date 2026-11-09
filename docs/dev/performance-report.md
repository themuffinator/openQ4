# Frame-rate report: 0.13.2 against the current source

Measured 7-8 November 2026 on the RTX 4060 Laptop (section 2); the 0.13.2 baseline is
the tag rebuilt with the same per-frame logging as the current source and validated
against the release binary. Section 10 declares the FPS-01 budgets from the reference
run of the evening of 8 November; section 11 re-tiers the presets to refresh classes
(FPS-02) and redeclares the budgets from the run of 9 November. The run chain's output lives under `.tmp/perf-suite/`
(appendix); the plan it feeds is the FPS category of the
[development roadmap](roadmap.md#frame-rate-recovery-and-performance-budgets).

## 1. Summary

- **The frame-rate drop since 0.13.2 is the current Ultra preset, not slower code.** At
  2560x1440 on OpenGL the current Ultra runs the seven stock-map scenes at 19-58 fps where
  0.13.2's Ultra ran them at 125-303; the same current binary at the 0.13.2 settings runs
  them at 119-273, within noise of 0.13.2 in five scenes (section 3.1). The settings carry
  12-49 ms of every 25-53 ms frame; the code carries at most 0.6 ms.
- **What the Ultra frame is made of (OpenGL).** The 16-sample RGBA16F scene target is the
  largest single cost, 12-29 ms per frame, because every forward lighting pass shades 16
  samples per pixel; 4 samples double the frame rate everywhere, 8 samples recover 36-52%
  (section 4.2). Shadow maps are the indoor residual: 33 of hub2's 54 ms, 30 of them
  point lights whose six cube faces re-render whenever a caster moves (section 4.1). The
  screen-space chain (SSAO at full resolution, 16-sample motion blur, tone map, bloom,
  SMAA ultra) is 10-14 ms at 16 samples. The GPU is the limit in every scene (section
  4.3).
- **Vulkan renders a different Ultra.** The device offers 2, 4 and 8 colour samples for
  the scene format, so the backend clamps the preset's 16 to 8 and the current Ultra runs
  1.7-2.7x faster there (50-121 fps). At equal settings the Vulkan backend regressed where
  OpenGL's did not: +0.8 to +2.3 ms per frame in every scene, 85-90% of it GPU-side draw
  time; 1.3 ms of the airdefense1 share is the soft-particle depth capture, a feature
  0.13.2's backend did not draw, which breaks the forward render pass to resolve the
  8-sample depth; the five backend features added since 0.13.2 that stay on at those
  settings cost nothing measurable (section 3.3).
- **The one OpenGL equal-settings regression is the retained HUD in storage2.** The scene
  is CPU-bound at 3.4 ms per frame, and the retained HUD's layer composition binds an
  offscreen target per layer and mask, each bind flushing the 2D batch as a full-screen
  view: 17 two-dimensional views and 34 render-target commands per frame against the
  classic HUD's 7 and 17, about 0.4 ms of main-thread time, +0.5 to +1.2 ms of frame time
  depending on the scene's regime (section 6). The 2 October build equals 0.13.2 and the
  15 October build carries the step, bracketing the 8 October commit that made the
  retained HUD the gameplay HUD.
- **Fresh installs.** 0.13.2's auto-detected settings run airdefense1 at 229 fps uncapped
  (226 on Vulkan); the current build's Enhanced tier runs it at 190 on both backends
  and caps at 120 (section 3.2). The current build loads levels 20-45% faster.
- **Hitches.** The entity spawn path stalls 115-225 ms on both builds and backends
  (synchronous model, animation and sound loads). The current OpenGL Ultra pays 2-3x the
  first-use draw excess of 0.13.2 (resource builds at 16 samples), 4-16 ms on Vulkan with
  its restored pipeline cache. hub2 at Ultra shows 100-124 ms frames every turn from
  point-light cube re-renders; one 393 ms storage2 frame sat in the event pump (section
  5).
- **Headroom.** The current Ultra needs a 3-12x speed-up for 220 fps average and 180 fps
  1% low on this GPU; no optimization of its recipe closes that. 0.13.2's own Ultra meets
  220 only in the lighter scenes and 180 only in storage2. A re-tiered Ultra built from
  the measured levers lands near 100 fps at 2560x1440 on this laptop (section 7), so the
  targets have to be declared per tier and machine.
- **Deliverables.** `tools/tests/performance_suite.py` (scripted scenarios, per-frame
  traces, toggle ladders, attribution, hitch events, idle-launch retry, the report), the
  `profileFrameWork` CSV path and early stop, this report, and the roadmap's FPS-01 to
  FPS-09 (section 8) with the regression gate of section 9.
- **Budgets.** FPS-01 named this laptop the reference machine and declared a warm-mean
  and p99 budget per scenario for Ultra, Quality and Enhanced on both backends
  (section 10, `docs/dev/performance-budgets.json`); `performance_suite.py check` gates a
  candidate run against them.
- **Re-tiered presets.** FPS-02 rebuilt the three tiers from the measured feature costs
  to hold refresh classes on the reference machine on both backends: Enhanced 144 fps
  average and 90 fps 1% low, Quality 100/60, Ultra 75/45. All three meet them in every
  scene (Ultra 91-188 fps, Quality 120-238, Enhanced 166-298). The previous Ultra is
  now **Maximum**, an everything-on choice with no frame-rate target. Point-light cube
  maps were the lever: Ultra and Quality use 512 cubes, and Enhanced leaves point lights
  on stencil shadows. A fresh install caps at the display's refresh rate (section 11).
- **Caveats.** One shared development laptop; three launches
  that missed the foreground were repeated; the 25 September bisection build did not
  build in pass 2 and is bracketed by the equal 2 October build.

## 2. Test methodology

### Hardware and software

| Item | Value |
|---|---|
| CPU | Intel Core i7-13700H (14 cores / 20 threads, Raptor Lake, laptop) |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU, 8 GB, driver 591.74 (Windows driver 32.0.15.9174), max SM clock 3105 MHz |
| Display path | hybrid laptop: the Intel Iris Xe drives the internal 2560x1440 240 Hz panel, the RTX renders |
| RAM | 40 GB |
| OS | Windows 11 Home 10.0.26200 |
| Toolchain | MSVC 19.50 (VS 18), Meson 1.10.2, ninja 1.13; `--buildtype=debugoptimized -Db_ndebug=true` (the release configuration), x64 |

The machine is a shared development box and, since FPS-01, the project's reference
machine for frame-rate budgets (section 10). Each
launch records the system CPU load and the GPU utilisation, clock, temperature,
power and memory once a second (`load` in every `result.json`); the report quotes
those numbers where a run may have been disturbed.

### Builds under test

| Label | Source | Notes |
|---|---|---|
| `v0132` | tag `v0.13.2` (fcdd56967, 2026-09-14) rebuilt from source with the release flags; game libraries from the companion repository commit the release used (openQ4-game 8f411dbc) | carries the same per-frame logging command as the current build; runs with the unmodified release `pak0.pk4`/`pak1.pk4` (the engine's expected pak checksums were patched to the release values because a rebuild packs three load-screen images and 51 light-grid packs differently) |
| `v0132rel` | the published 0.13.2 Windows x64 release package, unmodified | validates the rebuild: same pacing snapshot numbers; no per-frame trace (the command does not exist there) and integer-millisecond percentiles only |
| `head` | `main` at d856fe6bb (2026-11-07) plus the frame-logging extension described below (now part of `main`) | built in a detached worktree with the same flags; `pak1.pk4` is split into `pak1.pk4` + `pak2.pk4` since the content-pack work |
| `headeq` | the same `head` binaries at the 0.13.2 Ultra settings | isolates code changes from settings changes |
| `b<sha>` | `main` at intermediate first-parent commits, one per week between the two tags | bisection builds, measured at the 0.13.2 Ultra settings against `v0132` |

### Settings

Every build is measured at its own **Ultra** preset (`applyPerformancePreset ultra`),
then `com_maxfps 0` and `r_swapInterval 0` (no frame cap, no vsync), fullscreen
2560x1440 (`r_mode -1`, `r_customWidth 2560`, `r_customHeight 1440`,
`r_fullscreen 1`, `r_fullscreenDesktop 0`). The first launch of each configuration
applies the preset and archives it; the measured launches start from that archived
configuration and re-assert the cap and vsync values on the command line.
`developer 1` and `logFile 2` are on, as in every other openQ4 timing harness, so
late-load warnings are visible in the logs.

The two Ultra presets are not the same preset:

| Setting | 0.13.2 Ultra | current Ultra |
|---|---|---|
| MSAA (`r_multiSamples`) | 8 | 16; on OpenGL this GPU really renders 16 samples (`GL_MAX_SAMPLES` 32; `Renderer AA: MSAA requested=16 effective=16 reason=active`) into the RGBA16F scene target, on Vulkan the device offers 2, 4 and 8 samples for that format and the backend clamps to 8 (`Forward render target MSAA: requested 16, effective 8`) |
| Post AA (`r_postAA`) | 1 (SMAA medium) | 3 (SMAA ultra) |
| Temporal AA (`r_temporalAA`) | 0 | 2 (automatic; stays off at native scale) |
| Shadow maps (`r_useShadowMap`) | 0 (stencil shadows) | 1: 4096 projected / 2048 point, cascaded (4), PCSS 13 taps, point lights shadow-mapped, 16384 atlas, unlimited updates per view |
| SSAO | 0 | 1, 32 samples, full resolution on OpenGL |
| Bloom | 0 | 1, 5 mips |
| HDR tone map | 0 | 1 (the RGBA16F scene target is on in both) |
| Motion blur | 0 | 1, 16 samples |
| PBR materials | 0 | 1 |
| Portal distance cull (`r_portalsDistanceCull`) | 1 | 0 (default changed in d361e62ea) |
| Speakers / falloff / radio | 6 / quadratic / 0.5 | device layout / linear / 0.9 |
| Frame cap (`com_maxfps`) | 240 | 240 (lifted to 0 for every measurement) |

`headeq` forces exactly the 24 cvars whose effective value differs between the two
archived Ultra configurations (`listCvars` diff) back to the 0.13.2 values;
everything else, including features that did not exist in 0.13.2 (asynchronous
sound decoding, precise frame wait, level image prefetch, the retained user
interface, the PBR material pipeline when off), stays at the current build's
defaults.

### Scenarios

All scenarios run on retail single-player maps from the Steam installation, are
driven entirely by console scripts (`g_autoExecAfterMapLoad`), and never synthesise
keyboard or mouse input. Camera motion is `benchmarkViewSweep 720 30000`: two full
turns in 30 s, driven by game time, so every build sees the same frustum sequence.
The player is in `god`/`notarget`/`noclip` so the scripted world runs undisturbed.
Each pass lasts 30 s plus a 1 s margin; pass 1 of a launch is the **cold** pass
(first use of shaders, models, sounds and interface documents after the load), pass
2 the **warm** pass. Each configuration runs three launches (rounds) per scenario,
builds interleaved within a round so thermal drift and background load fall on all
builds alike.

| Scenario | Map, area | Why it is in the suite |
|---|---|---|
| `outdoor_airdefense1` (core: outdoor, portal sky) | Air Defense 1, raised vantage 456 units above the landing zone (`setviewpos 10324 -6964 420 12 0 0`) | The classic opening: the whole valley with the portal sky, the distant refinery towers, the scripted marine/strogg battle, burning wrecks and BSE smoke columns. Long sight lines and the most visible areas per frame of any stock map. |
| `outdoor_convoy1` (core: outdoor, long distance) | Convoy 1, canyon vantage 460 units above the convoy road (`setviewpos -20115 56796 700 12 0 0`) | The longest sight lines in the campaign: the convoy road, canyon walls, vehicles, the refinery on the horizon under the portal sky, mostly terrain and static geometry with few effects, so it isolates visibility and draw-call cost. |
| `combat_airdefense1` (core: combat) | Air Defense 1, open battlefield west of the landing zone (`9400 -6700`) | A spawned wave of 8 strogg (marines, grunts, a gunner), 6 marines, a light tank and a gladiator fights around the player: AI think, pathfinding, weapon fire, projectiles, damage effects, ragdolls. The wave is respawned per pass so the warm pass sees the same workload as the cold pass. |
| `effects_airdefense1` (core: effect-heavy) | same battlefield | Twelve exploding barrels detonated one every 2.4 s during the window while four gunners (grenades), two light tanks (rockets) and four hyperblaster marines fight: explosion lights and shadows, debris, smoke, blaster glows, plus the preset's whole post chain (SSAO, bloom, HDR tone map, motion blur, SMAA/TAA). |
| `indoor_storage2` (additional: tight indoor) | Storage 2, upper deck at the tactical squad spawn (`-656 1264 -2688`) | Dim Strogg interior with 42 shadow-casting lights within 700 units; the map where the equal-settings regression was first seen. |
| `indoor_hub2` (additional: indoor shadow-heavy) | Nexus Hub 2 processing hall at the sentry spawn (`3568 -5780 -296`) | The densest shadow-casting light cluster of the campaign (55 within 700 units) in a tall multi-level hall: shadow-map atlas pressure, many interactions per surface. |
| `vehicle_walker` (additional: vehicle) | Walker, the mech bay; the player enters the walker by script | Vehicle physics and cockpit view with the level's scripted assault. |
| `transition_ad1_ad2` (transition) | Air Defense 1 then `map game/airdefense2` | Level-load and map-change cost; the loads of every other launch are recorded too (cold = fresh save path, warm = later rounds). |
| `hitch_firstuse` (hitch catalog) | Air Defense 1 battlefield, static view | One first-use event at a time with every frame recorded: `give all`, the first spawn of a model class (gladiator, light tank), the first explosion, and on the current build the first shot (`in_presentationViewProbeAttack`), the scope overlay (`testWeaponScope 1`) and the flashlight. |

Discarded candidates, for the record: the spawn points of storage2, medlabs, hub2,
putra, process2, dispersal, waste and hangar2 are dark start areas or cinematic
locks (the medlabs player is bound to the operating table and ignores
`setviewpos`); building_b's spawn and the Air Defense 1 positions around
(9200, -5400) are indoors; hangar1 starts in corridors.

### Measurements and definitions

- **Frame time** is the interval between consecutive frame starts on the
  presentation clock (`startMs` of consecutive rows in the per-frame CSV), i.e. the
  real cadence of presented frames.
- **Average fps** = 1000 / mean frame time over the 30 s window.
- **1% low** = 1000 / p99 frame time; **0.1% low** = 1000 / p99.9 frame time (the
  slowest 1% and 0.1% of frames). The suite also records the "slowest-1%-average"
  form.
- **Worst frame** = the longest frame in the window; **variance** is reported as the
  standard deviation and the coefficient of variation of frame times.
- A **hitch** is a frame longer than twice the median frame time of its pass.
- The engine's own `framePacingSnapshot` (present rate and p50/p95/p99/max) is
  recorded per pass as a cross-check; on the release 0.13.2 binary it is the only
  timing source (integer milliseconds).
- Per-pass tables report the median over the passes of a group (cold or warm); the
  spread of the per-pass averages is shown in brackets.
- The **settings share** of a gap is `head - headeq` (same binary, two presets); the
  **code share** is `headeq - v0132` (two binaries, one preset).
- A launch whose GPU stays idle (`nvidia-smi` utilization below 20% over the launch)
  while frames are being logged did not hold the foreground fullscreen window and
  rendered nothing. Three launches of the chain did this, each the first measured
  launch of its run and two of them with nobody at the machine; the current build then
  paces every frame at roughly 45 Hz with one swapchain recreation in the log, 0.13.2's
  Vulkan backend recreates its swapchain every frame and runs at 15 fps. The suite now
  waits three seconds before every client launch, repeats a launch whose GPU stayed
  idle (`--max-retries`, default 1, the first attempt kept under `round<N>_suspect<k>`),
  lists such launches under **Warnings** in the report and can leave them out of the
  tables (`report --drop-suspect`). The three were run again in phase 3; their first
  attempts are excluded from this document.

### Per-frame logging

`profileFrameWork <n> [csv]` already captured up to 60000 frames with a per-phase
split (wait, events, configuration, commands, session, draw, tail) but wrote only
when the requested count was reached. The suite needed a bounded-duration capture,
so the command now accepts an optional relative `.csv` path below the save path and
`profileFrameWork 0` stops early and writes the frames captured so far
(`src/framework/FrameWorkProfile.h` `Stop()`, `src/framework/Common.cpp`). The same
profiler was ported to the 0.13.2 worktree for the baseline rebuild (not shipped).

### Reproducing a result

```bash
python tools/tests/performance_suite.py list
python tools/tests/performance_suite.py run --output .tmp/perf-suite/matrix-gl \
  --config v0132=.tmp/stock-runtime/perf-0132:gl:own-ultra \
  --config head=.tmp/stock-runtime/perf-head:gl:own-ultra \
  --config headeq=.tmp/stock-runtime/perf-head:gl:eq-0132-ultra \
  --scenarios timed --rounds 3 --passes 2 --duration-ms 30000 --width 2560 --height 1440
python tools/tests/performance_suite.py report --runs .tmp/perf-suite/matrix-gl --out .tmp/perf-suite/matrix-gl/report
```

`report` writes `summary.md` (per-scenario tables, the build comparison with the
settings/code split and the 220/180 target factors, feature ladders for toggle runs,
the longest frames with their phase split, first-use events, level loads and the
per-launch load), `aggregate.json` and `passes.json`, plus one frame-time plot per
scenario. `--attribution` adds the renderer metrics, GPU timers and the 256-frame
benchmark capture to each pass, `--cpu-sample 20` records CPU stacks of pass 1 with
`tools/debug/cpu_sampler.py`, `--toggle LABEL=cmd;cmd` turns a launch into a
feature ladder (base, toggle, base, ...), `--shots` screenshots the view at the end
of each pass, and `--hidden`/`--windowed` run the scripts without the fullscreen
display (smoke tests only: hidden windows are capped at 60 Hz).

## 3. Results

### 3.1 OpenGL: both builds at their own Ultra, and the current build at the 0.13.2 settings

Three launches per build and scenario, interleaved; the warm pass (pass 2 of each
launch) is the steady state, the median of the three launches is shown. The full
per-pass tables, plots and the cold passes are in `.tmp/perf-suite/matrix-gl/report/`.

| Scenario | Build | avg fps | 1% low | 0.1% low | mean ms | p99 ms | worst ms |
|---|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | v0132 | 188.7 | 116.4 | 47.4 | 5.30 | 8.59 | 40.7 |
| | headeq | 169.3 | 87.6 | 50.8 | 5.91 | 11.41 | 29.3 |
| | head | 39.9 | 22.0 | 14.8 | 25.08 | 45.39 | 84.2 |
| outdoor_convoy1 | v0132 | 157.7 | 90.1 | 62.4 | 6.34 | 11.11 | 113.1 |
| | headeq | 160.2 | 91.4 | 65.8 | 6.24 | 10.95 | 19.7 |
| | head | 27.5 | 16.0 | 12.4 | 36.37 | 62.50 | 86.0 |
| combat_airdefense1 | v0132 | 125.0 | 72.5 | 55.3 | 8.00 | 13.79 | 41.4 |
| | headeq | 132.9 | 88.7 | 53.6 | 7.53 | 11.28 | 43.5 |
| | head | 25.4 | 16.7 | 12.7 | 39.44 | 59.84 | 161.1 |
| effects_airdefense1 | v0132 | 127.7 | 94.9 | 64.5 | 7.83 | 10.53 | 24.1 |
| | headeq | 119.4 | 62.4 | 43.1 | 8.37 | 16.03 | 35.1 |
| | head | 25.1 | 15.8 | 13.3 | 39.82 | 63.33 | 75.5 |
| indoor_storage2 | v0132 | 303.0 | 180.2 | 117.5 | 3.30 | 5.55 | 11.8 |
| | headeq | 272.7 | 147.9 | 85.7 | 3.67 | 6.76 | 26.0 |
| | head | 37.3 | 22.1 | 16.0 | 26.77 | 45.32 | 393.3 |
| indoor_hub2 | v0132 | 234.7 | 154.5 | 134.8 | 4.26 | 6.47 | 13.9 |
| | headeq | 232.1 | 154.8 | 130.5 | 4.31 | 6.46 | 15.2 |
| | head | 18.7 | 10.6 | 8.1 | 53.40 | 94.36 | 124.1 |
| vehicle_walker (one pass per launch) | v0132 | 217.5 | 164.0 | 87.1 | 4.60 | 6.10 | 17.5 |
| | headeq | 214.2 | 159.6 | 100.5 | 4.67 | 6.27 | 16.2 |
| | head | 58.0 | 36.2 | 28.8 | 17.23 | 27.63 | 39.9 |

The gap between the two Ultra experiences, split into the share the settings carry
(same binary, two presets) and the share the code carries (two binaries, one preset),
in milliseconds per frame of the warm pass:

| Scenario | head - v0132 | settings share (head - headeq) | code share (headeq - v0132) | code share, % of 0.13.2 frame | code share by phase (session / draw) |
|---|---:|---:|---:|---:|---|
| outdoor_airdefense1 | +19.78 | +19.17 | +0.61 | +11.5% | +0.21 / +0.38 |
| outdoor_convoy1 | +30.03 | +30.13 | -0.10 | -1.6% | -0.01 / -0.08 |
| combat_airdefense1 | +31.44 | +31.91 | -0.47 | -5.9% | -0.20 / -0.28 |
| effects_airdefense1 | +31.99 | +31.45 | +0.54 | +6.9% | +0.26 / +0.19 |
| indoor_storage2 | +23.47 | +23.10 | +0.37 | +11.2% | +0.04 / +0.33 |
| indoor_hub2 | +49.14 | +49.09 | +0.05 | +1.2% | +0.03 / +0.01 |
| vehicle_walker | +12.63 | +12.56 | +0.07 | +1.5% | +0.02 / +0.05 |

Cold passes (pass 1, the first 30 s after the level is live) differ little from the
warm ones on any build; the first-use costs fall inside the first seconds and the
window averages them out. The 0.13.2 and equal-settings builds show them as a higher
hitch count rather than a lower average (storage2: 86 frames above twice the median
cold against 51 warm on 0.13.2; 122 against 87 on headeq):

| Scenario | v0132 cold avg / 1% low | headeq cold avg / 1% low | head cold avg / 1% low |
|---|---:|---:|---:|
| outdoor_airdefense1 | 188.9 / 105.5 | 172.2 / 88.4 | 39.8 / 21.5 |
| outdoor_convoy1 | 150.9 / 91.9 | 156.3 / 88.2 | 23.4 / 14.6 |
| combat_airdefense1 | 113.4 / 59.0 | 127.4 / 76.0 | 25.6 / 17.2 |
| effects_airdefense1 | 132.2 / 94.7 | 131.2 / 66.3 | 25.1 / 15.6 |
| indoor_storage2 | 294.3 / 163.8 | 266.2 / 127.7 | 37.5 / 22.6 |
| indoor_hub2 | 232.9 / 152.9 | 232.0 / 145.3 | 19.4 / 11.4 |

What the OpenGL matrix establishes:

- **The frame-rate drop is the preset, not the code.** At the 0.13.2 settings the
  current build is within noise of 0.13.2 on convoy1, hub2 and the walker, faster in
  the combat wave, and 7-12% slower only in storage2, airdefense1 and the effects
  scene. The current Ultra preset costs 12 to 49 ms per frame on this GPU at
  2560x1440, which turns 125-303 fps into 19-58 fps.
- **The equal-settings difference is a renderer difference.** Of the +0.4 to +0.6 ms
  in storage2 and airdefense1, 0.33-0.38 ms is in the draw phase (renderer front end,
  backend and swap) and 0.04-0.21 ms in the session phase (game tic and
  presentation). Section 6 bisects it; the toggle ladders in section 4 price the
  features that exist only in the current build (retained HUD, soft particles, light
  grid, reverb, presentation interpolation).
- **The current build has more short spikes at equal settings.** Its 1% low is 18-34%
  below 0.13.2 in storage2, airdefense1 and the effects scene while the averages are
  within 12%. In the warm passes of the effects scene it has 183 frames above twice
  the median against 17 on 0.13.2; the extra spike time is 2.6 s of draw and 0.9 s
  of session over three launches. The combat wave shows the opposite (its 1% low is
  22% better), so this is scene-specific, not a general tail regression.
- **Levels load 35-45% faster on the current build**: airdefense1 in 7.1 s against
  10.3 s, convoy1 6.7 s against 10.7 s, hub2 3.2 s against 5.4 s, storage2 3.1 s
  against 4.8 s, walker 3.3 s against 5.4 s (engine `msec to load`, median of the
  warm launches).
- **Variance scales with the frame time.** Standard deviations are 0.7-1.9 ms at
  3-8 ms frames on both builds, 6-15 ms at the current Ultra's 25-53 ms frames; the
  coefficient of variation stays in the same 15-35% band, so the heavier preset does
  not add relative jitter beyond its longer frames, except for its outright stalls
  (section 5).

### 3.2 What a fresh install gets

The `own-default` configurations launch each build with no saved configuration and no
preset argument, so the build applies its own first-launch choice; the measurement then
lifts the frame cap and vsync like every other run.

| | 0.13.2 first launch | current first launch |
|---|---|---|
| Detection | legacy machine spec: `This system qualifies for Ultra quality!` (`com_machineSpec 3`, 7956 MB video memory) | `First-launch performance preset 'enhanced': dedicated GPU capacity; not a measured frame rate; GPU=discrete, RAM=40658 MiB, dedicated VRAM=8188 MiB, drawable=2560x1440, CPUs=20` |
| MSAA / post AA | 8 / SMAA medium | 2 / SMAA high, TAA automatic |
| Anisotropic filtering | 16 | 4 |
| Shadow maps | off (stencil) | 1024 projected / 512 point, 5 taps |
| SSAO / bloom / HDR tone map / PBR | off / off / off / off | 12 samples / on / on / on |
| Portal distance cull | 1 | 0 |
| Frame cap (`com_maxfps`) | 240 (the cvar default, untouched) | 120 (the Enhanced recipe) |
| Startup banner | `Quality profile: com_machineSpec=3 com_videoRam=7956 com_performancePreset='balanced'` | `Quality profile: com_machineSpec=0 com_videoRam=64 com_performancePreset='minimum'`, printed before the device exists; the detected tier is applied afterwards and `com_videoRam` is never corrected |

Uncapped, the two first-launch configurations are close; capped, they are not:

| Scenario (warm) | 0.13.2 default: avg / 1% low / mean ms | current default: avg / 1% low / mean ms | uncapped difference |
|---|---:|---:|---:|
| outdoor_airdefense1 | 229.2 / 149.9 / 4.36 | 189.8 / 116.7 / 5.27 | -17% avg, -22% 1% low |
| combat_airdefense1 | 136.5 / 93.8 / 7.32 | 131.2 / 81.3 / 7.62 | -4% avg, -13% 1% low |

A player with a 240 Hz display therefore moved from about 229 fps in the Air Defense
valley to a 120 fps cap, with 4x instead of 16x anisotropic filtering: half the frame
rate and softer textures from the first launch, before any Ultra selection. Both the
cap and the filtering level are recipe choices in `src/framework/PerformancePreset.cpp`
(FPS-02 in the roadmap), not costs of the new rendering features, which account for the
4-17% uncapped difference.

### 3.3 Vulkan: the same matrix on the Vulkan backend

The same three builds and scenarios with `r_renderApi vulkan`, three interleaved
launches each; the warm pass is shown, the median of the three launches. Full tables,
plots and cold passes are in `.tmp/perf-suite/matrix-vk/report/`. 0.13.2 shipped the
Vulkan backend as an opt-in preview, so this is a backend-to-backend comparison of
two releases of the same renderer, not a measurement of what most players see.

| Scenario | Build | avg fps | 1% low | 0.1% low | mean ms | p99 ms | worst ms |
|---|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | v0132 | 220.0 | 122.9 | 78.5 | 4.55 | 8.13 | 21.0 |
| | headeq | 151.4 | 96.5 | 67.2 | 6.60 | 10.36 | 26.5 |
| | head | 68.9 | 45.2 | 38.8 | 14.51 | 22.14 | 31.1 |
| outdoor_convoy1 | v0132 | 193.8 | 124.6 | 77.6 | 5.16 | 8.03 | 18.4 |
| | headeq | 136.7 | 103.0 | 65.7 | 7.31 | 9.71 | 21.1 |
| | head | 72.5 | 53.8 | 37.2 | 13.80 | 18.57 | 37.2 |
| combat_airdefense1 | v0132 | 151.9 | 108.9 | 70.3 | 6.58 | 9.19 | 20.1 |
| | headeq | 113.2 | 60.6 | 45.2 | 8.83 | 16.49 | 41.5 |
| | head | 51.6 | 39.4 | 27.6 | 19.39 | 25.40 | 52.5 |
| effects_airdefense1 | v0132 | 153.7 | 107.9 | 63.9 | 6.51 | 9.26 | 30.4 |
| | headeq | 113.0 | 77.1 | 48.8 | 8.85 | 12.97 | 28.8 |
| | head | 50.5 | 37.7 | 30.4 | 19.81 | 26.54 | 39.1 |
| indoor_storage2 | v0132 | 373.7 | 227.8 | 159.4 | 2.68 | 4.39 | 14.3 |
| | headeq | 285.9 | 183.0 | 123.9 | 3.50 | 5.46 | 12.8 |
| | head | 68.2 | 37.8 | 31.4 | 14.67 | 26.49 | 35.0 |
| indoor_hub2 | v0132 | 244.8 | 161.7 | 123.0 | 4.08 | 6.18 | 21.7 |
| | headeq | 197.1 | 114.5 | 78.4 | 5.07 | 8.73 | 18.4 |
| | head | 49.6 | 31.8 | 28.9 | 20.18 | 31.46 | 41.2 |
| vehicle_walker (one pass per launch) | v0132 | 270.9 | 195.1 | 125.2 | 3.69 | 5.12 | 10.8 |
| | headeq | 180.6 | 121.3 | 82.9 | 5.54 | 8.25 | 14.7 |
| | head | 121.3 | 83.8 | 57.9 | 8.24 | 11.93 | 19.8 |

The gap split as in 3.1, milliseconds per frame of the warm pass:

| Scenario | head - v0132 | settings share (head - headeq) | code share (headeq - v0132) | code share, % of 0.13.2 frame | code share by phase (session / draw) |
|---|---:|---:|---:|---:|---|
| outdoor_airdefense1 | +9.96 | +7.91 | +2.05 | +45.1% | +0.26 / +1.73 |
| outdoor_convoy1 | +8.64 | +6.49 | +2.15 | +41.7% | +0.21 / +1.94 |
| combat_airdefense1 | +12.81 | +10.56 | +2.25 | +34.2% | +0.43 / +1.84 |
| effects_airdefense1 | +13.30 | +10.96 | +2.34 | +35.9% | +0.31 / +2.02 |
| indoor_storage2 | +11.99 | +11.17 | +0.82 | +30.6% | +0.08 / +0.74 |
| indoor_hub2 | +16.10 | +15.11 | +0.99 | +24.3% | +0.09 / +0.82 |
| vehicle_walker (cold) | +4.55 | +2.70 | +1.85 | +50.1% | +0.14 / +1.65 |

Backend against backend, warm average fps (OpenGL from 3.1, Vulkan from above):

| Scenario | v0132 GL / VK | headeq GL / VK | head GL / VK |
|---|---:|---:|---:|
| outdoor_airdefense1 | 188.7 / 220.0 (+17%) | 169.3 / 151.4 (-11%) | 39.9 / 68.9 (+73%) |
| outdoor_convoy1 | 157.7 / 193.8 (+23%) | 160.2 / 136.7 (-15%) | 27.5 / 72.5 (+164%) |
| combat_airdefense1 | 125.0 / 151.9 (+22%) | 132.9 / 113.2 (-15%) | 25.4 / 51.6 (+103%) |
| effects_airdefense1 | 127.7 / 153.7 (+20%) | 119.4 / 113.0 (-5%) | 25.1 / 50.5 (+101%) |
| indoor_storage2 | 303.0 / 373.7 (+23%) | 272.7 / 285.9 (+5%) | 37.3 / 68.2 (+83%) |
| indoor_hub2 | 234.7 / 244.8 (+4%) | 232.1 / 197.1 (-15%) | 18.7 / 49.6 (+165%) |
| vehicle_walker | 217.5 / 270.9 (+25%) | 214.2 / 180.6 (-16%) | 58.0 / 121.3 (+109%) |

What the Vulkan matrix establishes:

- **Ultra renders half the samples on Vulkan.** The device reports 2, 4 and 8 colour
  samples for the scene format, so the backend clamps the preset's 16 to 8 (`Forward
  render target MSAA: requested 16, effective 8`), while the OpenGL path renders all
  16 (GL_MAX_SAMPLES 32). That, not a faster backend, is most of the 1.7-2.7x between
  the two Ultra columns; section 4.2 prices the OpenGL preset at 8 samples directly.
  Even at 8 samples the current Ultra is GPU-bound at 50-72 fps (GPU 82-89% busy).
- **The Vulkan backend regressed at equal settings, OpenGL's did not.** 0.13.2's
  Vulkan backend was 4-25% faster than its OpenGL one; the current backend at the
  0.13.2 settings is 5-16% slower than the current OpenGL in six of seven scenes.
  Its code share is +0.8 to +2.3 ms per frame in every scene, 85-90% of it in the
  draw phase, against +0.6 ms at most on OpenGL. The backend has 55 commits since
  2 October (render-thread prototype and frame snapshots, depth clip control,
  window-view multisampling, the portal-sky mask, shadow ownership, PBR and probe
  work, HDR capture targets); the features among them that stay active at the
  0.13.2 settings are priced in section 4.1 and the storage2 attribution in 4.3.
- **The current build allocates more.** Whole-GPU memory in use (`nvidia-smi`) is
  1299-1360 MiB on 0.13.2, 1622-1986 MiB on the current build at the same settings,
  and 3375-7290 MiB at the current Ultra; hub2 at Ultra leaves under 1 GiB of the
  RTX 4060's 8 GiB free, with the 16384 shadow atlas (1 GiB), the 8-sample FP16
  scene and 1739 resident images. The engine's own device-local figure at the
  `gfxInfo` dump is 2931 of a 6365 MiB budget on hub2, lower because the dump runs
  ten seconds in.
- **Tails follow the averages.** The current build's 1% low at equal settings is
  12-44% below 0.13.2 in every scene; its hitch counts (frames above twice the
  median) are higher at both of its settings.
- **Levels load 20-27% faster on the current build under Vulkan too** (airdefense1
  6.8 s against 8.6 s, convoy1 5.5-5.8 s against 7.6 s, hub2 3.1-3.2 s against 4.5 s,
  storage2 2.7 s against 3.8 s, walker 3.3 s against 4.7 s).

**Vulkan feature ladders at the current Ultra.** The procedure of section 4.1 on the
Vulkan backend (8 real samples), effects_airdefense1 and indoor_hub2; the outdoor
ladder's phase-2 launch missed the foreground and was repeated in phase 3 (below). Base
windows were 18.6-21.4 ms in the effects scene and 19.9-23.3 ms in hub2, so single
toggles under about 1.5 ms in hub2 are inside its spread. Two Vulkan-only toggles are
added: SSAO at half resolution (`r_ssaoResolution 2`) and the render-thread prototype
(`r_vkRenderThread 1`).

| Window (feature turned off) | effects delta ms (of 19.8) | effects % | hub2 delta ms (of 22.1) | hub2 % | hub2 fps in the window |
|---|---:|---:|---:|---:|---:|
| `shadowMap` | -4.92 | -25.4% | -6.77 | -31.3% | 67.4 |
| `pointShadows` | -6.70 | -31.9% | -6.42 | -28.1% | 60.8 |
| `csm` | -2.45 | -12.8% | -0.49 | -2.2% | 46.6 |
| `pcss` | +0.23 | +1.1% | -1.91 | -8.3% | 47.0 |
| `ssao` | -1.14 | -5.6% | -2.51 | -11.9% | 53.8 |
| `ssaoHalf` (`r_ssaoResolution 2`) | -0.12 | -0.6% | -1.25 | -5.5% | 46.4 |
| `hdr` | -1.00 | -5.0% | +1.64 | +7.7% | 43.8 |
| `motionBlur` | -0.44 | -2.2% | +1.36 | +6.4% | 44.0 |
| `bloom` | -1.57 | -7.7% | -1.31 | -6.3% | 51.0 |
| `smaaMedium` | -0.87 | -4.3% | +2.44 | +11.3% | 41.5 |
| `postOff` | +0.07 | +0.4% | +0.10 | +0.5% | 45.3 |
| `pbr` | +0.32 | +1.7% | -0.65 | -2.8% | 44.8 |
| `allPostOff` | -4.46 | -23.1% | -6.70 | -30.3% | 64.9 |
| `allModernOff` | -10.34 | -53.8% | -16.85 | -76.7% | 195.2 |
| `renderThread` (`r_vkRenderThread 1`) | -0.39 | -2.0% | -0.35 | -1.6% | 44.8 |

- **At 8 samples the shadow maps are a quarter to a third of the frame in both scenes**
  (5-7 ms), the point-light cube maps most of it as on OpenGL, the cascades 2.5 ms in the
  effects scene and the 13-tap PCSS filter 1.9 ms in hub2. The screen-space chain is
  another 4.5-6.7 ms (23-30%): SSAO 1.1-2.5 ms, bloom 1.3-1.6 ms, the tone map 1 ms in
  the effects scene; half-resolution SSAO recovers 1.25 ms in hub2.
- **With every modern feature off inside the launch the Vulkan frame reaches the
  equal-settings frame** (8.9 ms against headeq's 8.85 in the effects scene, 5.1 against
  5.07 in hub2), unlike OpenGL where the 16-sample target kept 16.7 and 8.9 ms (section
  4.1): on Vulkan the preset and the 0.13.2 settings already share the sample count, so
  the modern features are the whole of the Ultra gap there.
- **The render-thread prototype gains nothing yet**: within 0.4 ms of the base windows
  in both scenes, with 4 ms of the frame moved into the session phase (2.3 to 6.6 ms in
  the effects scene), a lower 1% low (27.7 against 38-45 fps) and the longest frames of
  the ladder (52 ms).

**Restart variants on Vulkan.** As in section 4.2, one launch each, warm pass, the current
build at its Ultra preset with 8 (the clamped preset), 4 and no samples, and with the
screen-space chain off from launch:

| Scenario | configuration | avg fps | 1% low | mean ms | saving vs the preset | VRAM MiB |
|---|---|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | Ultra, MSAA 8 (preset) | 68.4 | 46.5 | 14.62 | | 4984 |
| | Ultra, MSAA 4 | 84.2 | 55.6 | 11.87 | -2.75 (-19%) | 4812 |
| | Ultra, MSAA off | 99.6 | 63.3 | 10.04 | -4.58 (-31%) | 4667 |
| | Ultra, post chain off | 93.1 | 58.1 | 10.74 | -3.88 (-27%) | 4717 |
| effects_airdefense1 | Ultra, MSAA 8 (preset) | 50.1 | 40.3 | 19.96 | | 4790 |
| | Ultra, MSAA 4 | 59.7 | 40.6 | 16.75 | -3.21 (-16%) | 4908 |
| | Ultra, MSAA off | 68.0 | 43.8 | 14.70 | -5.26 (-26%) | 5343 |
| | Ultra, post chain off | 70.9 | 55.3 | 14.10 | -5.86 (-29%) | 4815 |
| indoor_hub2 | Ultra, MSAA 8 (preset) | 50.4 | 30.2 | 19.83 | | 7482 |
| | Ultra, MSAA 4 | 54.3 | 31.5 | 18.41 | -1.42 (-7%) | 7181 |
| | Ultra, MSAA off | 69.7 | 38.2 | 14.36 | -5.47 (-28%) | 6717 |
| | Ultra, post chain off | 66.4 | 38.4 | 15.05 | -4.78 (-24%) | 6895 |

Eight samples cost 4.6-5.5 ms per frame on Vulkan (26-31%), against the 12-29 ms of the
16-sample OpenGL target; the post chain 3.9-5.9 ms (24-29%). With MSAA off the Vulkan
Ultra runs 18-30% faster than the OpenGL Ultra with MSAA off (10.0 against 12.2 ms,
14.7 against 18.8, 14.4 against 20.6): the backend's shadow-map and post passes are
faster than the modern OpenGL executor's, while its classic forward path is slower (the
code share above). hub2 still holds 6.7-7.5 GB of video memory in every variant.

**Defaults on Vulkan.** At each build's auto-detected first-launch settings, warm:
combat_airdefense1 157.3 fps on the current build against 147.7 on 0.13.2 (+6.5%; the
cold passes are equal at 151), 20% above the same preset on OpenGL (131.2); outdoor
airdefense1 189.6 fps, the same as on OpenGL (189.8). 0.13.2's outdoor default launch
missed the foreground and was repeated in phase 3 (below).

**Drift and the equal-settings ladder on Vulkan.** The thirteen-window drift launches of
section 4.2 on Vulkan: in airdefense1 the current build is slower than 0.13.2 by +1.74 to
+2.00 ms in every one of the thirteen windows (6.52 against 4.62 ms on average, +41%),
where OpenGL had the two builds within 0.05 ms; in storage2 the current build steps
through the same scripted regime as on OpenGL (3.6-3.8 ms, then 6.9-7.1 ms from the third
window, 4.2-5.5 ms at the end). 0.13.2's storage2 drift launch missed the foreground and
was repeated in phase 3. The equal-settings ladder on Vulkan, airdefense1 (base windows
6.50-6.74 ms on the current build, 4.46-4.88 on 0.13.2):

| Window | 0.13.2 (of 4.6 ms) | current at 0.13.2 settings (of 6.5 ms) | Note |
|---|---:|---:|---|
| `portalCullOff` | -0.29 | -0.25 | as on OpenGL |
| `presInterp` | -0.03 | +0.03 | no cost |
| `lightGrid` | +0.02 | +0.03 | no cost |
| `reverb` | -0.06 | -0.02 | no cost |
| `hudClassic` | -0.02 | +0.05 | no cost |
| `softParticles` (`r_softParticles 0`) | -0.06 | **-1.31 (-20%)** | 0.33-0.37 ms on both builds on OpenGL |

**Soft particles are 1.3 ms of the Vulkan code share in airdefense1.** 0.13.2's Vulkan
backend never drew them (its `RB_DrawSurfHasSoftParticleStage` returns false and it has no
soft-particle pipeline); the current backend does, and for the first soft particle of a
view it captures the scene depth with `VK_Exec_CaptureCurrentDepth`, which leaves the
forward render pass, resolves the 8-sample depth into `_currentDepth` and re-enters the
pass with its attachments reloaded. That is four times what the same effect costs on
OpenGL. It accounts for 1.25 of the +1.9 ms in airdefense1; the rest of that, and the
whole +0.8 ms in storage2, which has few particles, is pursued by the phase-3 ladder of
the backend features that stay on at the 0.13.2 settings and by the attribution below.

**CPU and GPU attribution on Vulkan.** The attribution launches of section 4.3 on the
Vulkan backend; medians of the 256 retained frames after the warm pass, milliseconds (the
Vulkan capture reports the backend inside `submit`, and `fence wait` is the part of it
spent waiting for the GPU):

| Scenario | Build | CPU frame | front end | submit | fence wait | GPU frame | frame interval (matrix) |
|---|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | head (Ultra) | 10.3 | 2 | 8 | 6.7 | 11.7 | 14.5 |
| | headeq | 4.9 | 1 | 3 | 2.4 | 6.0 | 6.6 |
| combat_airdefense1 | head (Ultra) | 17.7 | 3 | 14 | 12.0 | 19.8 | 19.4 |
| | headeq | 8.0 | 2 | 5 | 3.0 | 9.2 | 8.8 |
| effects_airdefense1 | head (Ultra) | 16.9 | 2.5 | 14 | 11.3 | 18.6 | 19.8 |
| | headeq | 9.1 | 2 | 6 | 4.7 | 10.3 | 8.9 |
| indoor_hub2 | head (Ultra) | 21.7 | 2 | 19 | 16.9 | 22.5 | 20.2 |
| | headeq | 6.1 | 1 | 4 | 3.1 | 6.7 | 5.1 |

`rendererMetrics summary` medians over each launch (integer milliseconds; draws per frame):

| Scenario | v0132: fe / submit / draws | headeq: fe / submit / draws | head: fe / submit / draws |
|---|---|---|---|
| outdoor_airdefense1 | 1 / 2 / 435 | 1 / 3 / 502 | 2 / 9 / 462 |
| combat_airdefense1 | 2 / 2 / 766 | 3 / 4 / 859 | 3 / 12 / 1517 |
| effects_airdefense1 | 2 / 3 / 694 | 2 / 5 / 741 | 3 / 14 / 1422 |
| indoor_hub2 | 1 / 2 / 337 | 1 / 3 / 338 | 2 / 17 / 1067 |

- **The Vulkan Ultra is GPU-bound like the OpenGL one**: the GPU frame is 11.7-22.5 ms
  against frame intervals of 14.5-20.2, and 7-17 of the 8-19 ms the CPU spends in
  `submit` is the fence wait. **So is the current build at the 0.13.2 settings**: its GPU
  frame (6.0-10.3 ms) is at or above the frame interval (5.1-8.9), which is where the
  draw-phase code share of the gap split comes from: the frame waits on GPU work that
  0.13.2's backend did not issue.
- **The current build issues 7-15% more draws than 0.13.2 in the airdefense1 scenes at
  equal settings** (502 against 435, 859 against 766, 741 against 694) and the same number
  in hub2 (338 against 337), while on OpenGL the two builds issue the same draws
  everywhere (section 4.3). The extra draws follow the particle-heavy scenes, consistent
  with the soft-particle stages that the Vulkan backend now draws through their own
  pipeline; its `submit` median is one to two integer milliseconds above 0.13.2's.
- **Main-thread samples** (150 Hz over 20 s of the warm pass): both builds spend 25-28%
  of the thread waiting for the fence or the present at the 0.13.2 settings; on 0.13.2
  the time inside the backend resolves to `VK_GuiExecutor_BeginFrame` (26% inclusive, the
  frame-start wait), `R_AddEffectSurfaces` (18%) and `VK_GuiExecutor_Draw3DView` (14%),
  and the game side (`rvBSE::Render`, 8-10% inclusive) is the same on both builds. For
  the current build the renderer module's symbols did not load in this run, so its 26% of
  renderer time appears under the module's exports (`GetRenderAPI`,
  `openQ4_Mem_GetModuleStats`); the storage2 attribution of phase 3 runs with the
  symbols staged and carries the comparison.

**Release binary.** The 0.13.2 release executable and the rebuilt 0.13.2 are within 5% of
each other on Vulkan as on OpenGL (present interval -3.4% to +5.2% across airdefense1,
combat and hub2, two launches each), so the rebuild with the frame logger stands for the
release in this report.

**Vulkan feature ladder, airdefense1 (phase 3).** The repeated launch held the GPU at
97%; base windows 14.2-15.3 ms. Savings: `shadowMap` -3.93 ms (-27%), `pointShadows`
-3.65 (-26%), `csm` -2.61 (-18%), `bloom` -1.39 (-9%), `hdr` -1.21 (-8%), `ssao` -0.87
(-6%), `motionBlur` -0.73 (-5%), `postOff` -0.50, `smaaMedium` -0.10, `pbr` -0.07,
`renderThread` -0.02; `allPostOff` -4.24 (-30%) and `allModernOff` -7.68 (-54%, to 6.53
ms, the equal-settings frame of 6.60). Two toggles cost rather than save: `pcss` +2.14 ms
(the 9-tap mode-0 filter is slower than the 13-tap PCSS here, or its pipeline permutation
was built inside the window: longest frame 31.8 ms) and `ssaoHalf` +0.64 (half-resolution
SSAO does not pay outdoors; it saved 1.25 ms in hub2).

**The backend features that stay on at the 0.13.2 settings cost nothing measurable.**
The `toggles-eq-vk2` ladder on airdefense1 (bases 6.57-6.89 ms, every toggle twice):
retained snapshot indices -0.16 / -0.01 ms, window-view multisampling +0.04 / +0.06, the
portal-sky mask +0.06 / +0.05, authored ARB program translation +0.01 / +0.04, the precise
frame wait +0.02 / -0.01; soft particles -1.20 / -1.27 ms (-18 / -19%) once more. Of the
+1.9 ms Vulkan code share in airdefense1, 1.25 ms is therefore the soft-particle capture
and about 0.65 ms stays unattributed by toggles (the attribution's extra draws point at
the soft-particle stages themselves). In storage2 the scripted regime confounds the small
toggles; one quiet-regime window reads snapshot retention at -0.31 ms (-7.5%), a single
unconfirmed sample.

**Re-runs of the two lost 0.13.2 launches.** The default-preset launch on Vulkan: 0.13.2's
auto-detected settings run airdefense1 at 226 fps warm (220 cold) against the current
build's 190 (191), -16%, the same picture as OpenGL's 229 against 190 (section 3.2). The
storage2 drift launch on Vulkan: 0.13.2 steps through the same scripted regime (3.0 ms,
then 5.2-5.8, then 3.2-3.9) and the current build sits above it by +0.6-0.8 ms in the quiet
regime and +1.1-1.9 ms in the busy one (+21-36%); where OpenGL's difference was a constant
1.0-1.2 ms, Vulkan's grows with the scene's load, GPU-bound extra work against OpenGL's
CPU-bound one (section 6).

## 4. Where the frame time goes

### 4.1 OpenGL feature ladders: what each Ultra feature costs in one launch

One launch per scene on the current build at its Ultra preset; windows of 15 s alternate
between the full preset (`base0`, `base1`, ...) and one feature turned off, and every
toggle is read against the mean of its two neighbouring base windows, so slow drift
inside the launch cancels. The base windows stayed within 23.9-24.2 ms in airdefense1,
37.2-41.8 ms in the effects wave (its phase-2 launch overflowed the console buffer and
was repeated in phase 3) and 50.5-55.2 ms in hub2. Negative numbers are savings; the
last column is the frame rate the window reached.

| Window (feature turned off) | airdefense1 delta ms (of 24.0) | airdefense1 % | effects delta ms (of 39.7) | effects % | hub2 delta ms (of 52.6) | hub2 % | hub2 fps in the window |
|---|---:|---:|---:|---:|---:|---:|---:|
| `shadowMap` (`r_useShadowMap 0`, stencil shadows instead) | -5.06 | -21.0% | -14.85 | -36.4% | -33.35 | -62.2% | 49.4 |
| `pointShadows` (`r_shadowMapPointLights 0`, point lights back to stencil) | -4.97 | -20.7% | -11.59 | -30.6% | -30.34 | -57.5% | 44.6 |
| `csm` (`r_shadowMapCSM 0`) | -2.91 | -12.1% | -7.80 | -19.1% | -5.19 | -9.6% | 20.5 |
| `pcss` (filter mode 0, 9 taps) | -0.40 | -1.7% | +0.24 | +0.6% | -6.91 | -12.9% | 21.4 |
| `ssao` (`r_ssao 0`) | -2.14 | -8.9% | -5.44 | -13.2% | -6.12 | -11.3% | 20.8 |
| `hdr` (`r_hdrToneMap 0`) | -2.95 | -12.2% | -3.54 | -9.3% | +0.07 | +0.1% | 19.2 |
| `motionBlur` (`r_motionBlur 0`) | -2.31 | -9.6% | +0.33 | +0.8% | -2.46 | -4.8% | 20.5 |
| `bloom` (`r_bloom 0`) | -0.79 | -3.3% | -1.74 | -4.3% | -1.92 | -3.6% | 19.3 |
| `smaaMedium` (`r_postAA 1`) | -0.10 | -0.4% | +0.31 | +0.8% | +1.45 | +2.9% | 19.2 |
| `postOff` (`r_postAA 0`) | -0.50 | -2.1% | -2.09 | -5.2% | -1.68 | -3.3% | 20.4 |
| `pbr` (`r_pbrMaterials 0`) | +0.04 | +0.2% | +1.33 | +3.3% | +3.33 | +6.4% | 18.2 |
| `allPostOff` (SSAO, bloom, tone map, motion blur, post AA off) | -4.86 | -20.3% | -9.65 | -24.3% | -12.38 | -23.6% | 25.0 |
| `allModernOff` (all of the above plus shadow maps and PBR off; SMAA medium kept) | -7.22 | -30.2% | -23.31 | -58.3% | -44.89 | -83.5% | 113.0 |

Reading the ladders:

- **Shadow maps dominate the indoor scene.** In hub2 they are 33 of 53 ms, and 30 ms of
  that is the point lights: a point light with any moving caster in range re-renders all
  six cube faces every frame, because only projected lights compose dynamic casters over
  cached static depth (`RB_ShadowMapStaticCacheable` in `src/renderer/draw_arb2.cpp`).
  The cascades and the 13-tap PCSS filter are each worth 5-7 ms there. Outdoors the
  shadow maps cost 5 ms of 24; in the effects wave, 15 of 40 ms (point lights 12, the
  cascades 8), with the screen-space chain another 10 ms (SSAO 5.4, the tone map 3.5).
- **The screen-space chain is the second block.** SSAO (32 samples at full resolution on
  OpenGL), motion blur (16 samples) and the HDR tone map each cost 2-3 ms in airdefense1
  and 2-6 ms in hub2; bloom under 2 ms; SMAA ultra against medium is within noise.
  Turning the whole chain off in the launch saves 5 ms outdoors and 12 ms indoors.
- **In-launch toggles under-estimate the restart cost.** With every modern feature turned
  off inside the launch, airdefense1 still runs 16.7 ms and hub2 8.9 ms, against 5.9 and
  4.3 ms for the same binary launched at the 0.13.2 settings. The two differences that an
  in-launch toggle cannot undo are the scene target's 16 samples (an `r_multiSamples`
  change needs a restart) and the render targets and prepasses the post chain allocated
  at startup. Section 4.2 measures those with restart configurations.
- **PBR off, SMAA medium and the HDR tone map in hub2 read as zero or slightly
  negative savings**: those features cost nothing measurable in these scenes at this
  frame time (the +1.5 to +3.3 ms readings are within the 2-5 ms spread of the hub2 base
  windows).

**Equal-settings ladder.** The same procedure on both builds at the 0.13.2 Ultra settings,
on airdefense1 (base windows stable within 0.03 ms) and storage2, for the features that
exist only in the current build or whose defaults changed:

| Window | 0.13.2 (airdefense1, of 5.2 ms) | current at 0.13.2 settings (airdefense1, of 5.2 ms) | Note |
|---|---:|---:|---|
| `portalCullOff` (`r_portalsDistanceCull 0`, the new default) | -0.08 | -0.20 | culling off is marginally faster here |
| `presInterp` (`g_presentationInterpolation 0`) | +0.05 | -0.03 | no cost |
| `lightGrid` (`r_useLightGrid 0`) | 0.00 | 0.00 | no cost |
| `reverb` (`s_useEAXReverb 0`) | 0.00 | 0.00 | no cost |
| `hudClassic` (`ui_retained 0`) | -0.05 (cvar absent: control window) | -0.02 | the retained HUD costs nothing measurable here |
| `softParticles` (`r_softParticles 0`) | -0.37 | -0.33 | 6-7% on both builds, not a regression |

None of the suspected small-cost features explains the +0.6 ms the current build spends
in airdefense1 at equal settings; it sits in the renderer front end, backend and game
tic outside these toggles, which is what the bisection (section 6) and the attribution
(section 4.3) address. The storage2 ladder is not usable for feature costs: the scene's
own script roughly doubled the frame time in the middle of the launch on both builds
alike (3.4 to 5.9 ms), but at every same-position window the current build was 6-15%
slower than 0.13.2, confirming the matrix.

**Repeated storage2 ladder** (phase 3: three toggles, each three times per launch, both
builds on both backends). Stencil shadows (`r_shadows 0`) are a third of the
equal-settings frame in the quiet regime, -2.0 to -2.5 ms (-35 to -43%) on both builds
and backends, and 0.7-1.6 ms in the busy regime. Presentation interpolation reads -0.1
to -0.45 ms on the current build and within 0.2 ms on 0.13.2, inside the regime noise. The
retained HUD (`ui_retained 0`), in the one window whose neighbouring bases are stable
(the third, in the busy regime), costs 0.35 ms per frame on OpenGL and 0.22 ms on Vulkan
on the current build; the same window on 0.13.2, where the cvar does not exist, reads
-0.02 and +0.17 ms. Section 6 puts the HUD's main-thread cost at about 0.4 ms per frame.

### 4.2 Restart variants: MSAA sample count and the post chain

MSAA and the render targets the post chain allocates can only change with a restart, so
these are separate configurations of the current build at its Ultra preset: 16 samples
(the preset), 4, none, and the preset with the screen-space chain off from launch (SSAO,
bloom, tone map and motion blur off, SMAA medium kept, shadow maps and 16x MSAA kept).
One launch each, warm pass; the GPU was 81-90% busy in every one of them.

| Scenario | configuration | avg fps | 1% low | mean ms | saving vs 16x Ultra | VRAM MiB |
|---|---|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | Ultra, MSAA 16 | 39.3 | 24.4 | 25.46 | | 5782 |
| | Ultra, MSAA 8 (phase 3) | 62.7 | 35.3 | 15.94 | -9.06 (-36%) | 5232 |
| | Ultra, MSAA 4 | 74.2 | 42.9 | 13.48 | -11.98 (-47%) | 4944 |
| | Ultra, MSAA off | 81.9 | 46.1 | 12.20 | -13.26 (-52%) | 4717 |
| | Ultra, post chain off | 64.4 | 29.9 | 15.53 | -9.93 (-39%) | 5387 |
| effects_airdefense1 | Ultra, MSAA 16 | 24.6 | 14.1 | 40.63 | | 5808 |
| | Ultra, MSAA 8 (phase 3) | 42.8 | 30.4 | 23.35 | -16.59 (-42%) | 5268 |
| | Ultra, MSAA 4 | 49.8 | 38.9 | 20.09 | -20.54 (-51%) | 4971 |
| | Ultra, MSAA off | 53.3 | 36.4 | 18.75 | -21.88 (-54%) | 4745 |
| | Ultra, post chain off | 36.0 | 21.4 | 27.78 | -12.85 (-32%) | 5029 |
| indoor_hub2 | Ultra, MSAA 16 | 18.6 | 10.3 | 53.67 | | 5731 |
| | Ultra, MSAA 8 (phase 3) | 39.2 | 17.5 | 25.50 | -27.63 (-52%) | 5181 |
| | Ultra, MSAA 4 | 41.0 | 17.8 | 24.39 | -29.28 (-55%) | 4894 |
| | Ultra, MSAA off | 48.6 | 20.0 | 20.57 | -33.10 (-62%) | 4666 |
| | Ultra, post chain off | 25.3 | 12.2 | 39.59 | -14.08 (-26%) | 5335 |

- **The 16-sample scene target is the largest single cost in every scene**, 12 to
  29 ms per frame, because it multiplies with every forward lighting pass: each light's
  interaction pass shades 16 samples per covered pixel, so the light-dense hub2 loses
  more to it than the outdoor scenes. Going from 16 to 4 samples doubles the frame rate
  everywhere (39 to 74, 25 to 50, 19 to 41 fps); turning MSAA off entirely adds only
  another 1 to 4 ms. Eight samples (a separate phase-3 run, whose own 16-sample launches
  read 25.0, 39.9 and 53.1 ms) sit in between at 15.9, 23.4 and 25.5 ms: 36-52% below 16
  samples and 2.5-4.9 ms above 4. At 8 samples the OpenGL Ultra is still 9-29% slower
  than the Vulkan Ultra at its 8 (14.6, 20.0 and 19.8 ms, section 3.3).
- **The post chain costs 10-14 ms at 16 samples**, two to three times what the
  in-launch ladder measured, because its passes read and resolve the 16-sample target
  and allocate their own targets at startup; at 4 samples the same passes are
  proportionally cheaper. The two costs overlap and must be reduced together.
- **Shadow maps remain the indoor residual**: hub2 at MSAA 4 is still 24 ms against the
  4.3 ms of the same binary at the 0.13.2 settings, and section 4.1 placed 30 ms of the
  16-sample frame in the point-light cube maps.
- **Video memory** at the current Ultra is 5.7-5.8 GB of the 8 GB card (1 GiB of it the
  16384 shadow atlas), 4.7-4.9 GB at MSAA 4 or off; 0.13.2's Ultra and the current
  Enhanced default use 0.8-1.3 GB.

**Scene regime and the equal-settings difference.** The drift launches play seven
identical 15 s windows back to back on storage2 and airdefense1 with both builds at the
0.13.2 settings. In storage2 the frame time steps from 2.3-3.4 ms to 4.6-5.8 ms about 45 s
after the level is live and partly recovers after two and a half minutes, identically on
both builds: the level's own script, not the engine, drives that. Against it the current
build is slower by a constant 1.0-1.2 ms in every window (3.38 against 2.37 ms in the
quiet opening, 5.8 against 4.7 ms in the busy regime; 0.9-1.0 ms of it in the draw phase).
In airdefense1 the two builds are within 0.05 ms of each other in all thirteen windows,
whereas the three-launch matrix read +0.6 ms there: the scripted valley battle plays out
differently from launch to launch, so that scene's equal-settings difference is inside its
launch-to-launch spread. The storage2 difference is the one solid equal-settings
regression on OpenGL (sections 6 and 4.3 pursue it); the effects scene's extra spike
frames are the other.

### 4.3 CPU and GPU attribution (OpenGL)

The attribution launches add `r_rendererMetrics 1` and `r_rendererGpuTimers 1` to each
pass and run `rendererBenchmarkCapture` after it, which writes the 256 retained frames
with the CPU front end, submit, backend and present times and the delayed whole-frame GPU
time (the 0.13.2 binary prints the capture summary but has no CSV path argument). The
main-thread stack sampler did not run in this phase: its start marker did not allow for
the timestamp prefix the log puts on every echoed token (fixed; the Vulkan phase and the
storage2 follow-up carry the samples).

Median of the 256 frames after the warm pass, milliseconds:

| Scenario | Build | CPU frame | front end | submit | backend | GPU frame | frame interval (matrix) |
|---|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | head (Ultra) | 17.3 | 2 | 3 | 3 | 18.8 | 25.1 |
| | headeq | 4.5 | 1 | 3 | 3 | 4.8 | 5.9 |
| combat_airdefense1 | head (Ultra) | 33.9 | 4 | 6 | 6 | 36.6 | 39.4 |
| | headeq | 7.3 | 2 | 5 | 5 | 8.1 | 7.5 |
| effects_airdefense1 | head (Ultra) | 36.7 | 4 | 6 | 6 | 38.4 | 39.8 |
| | headeq | 7.0 | 1 | 1.5 | 1 | 7.4 | 8.4 |
| indoor_hub2 | head (Ultra) | 46.5 | 4 | 7 | 7 | 49.2 | 53.4 |
| | headeq | 5.0 | 1 | 4 | 4 | 5.2 | 4.3 |

`rendererMetrics summary` medians over each launch (integer milliseconds; draws per frame):

| Scenario | v0132: fe / submit / be / draws | headeq: fe / submit / be / draws | head: fe / submit / be / draws |
|---|---|---|---|
| outdoor_airdefense1 | 1 / 2 / 2 / 428 | 1 / 3 / 3 / 420 | 2 / 3 / 3 / 624 |
| combat_airdefense1 | 2 / 4 / 4 / 788 | 2 / 3 / 3 / 789 | 5 / 6 / 6 / 1794 |
| effects_airdefense1 | 2 / 4 / 4 / 661 | 2 / 2 / 2 / 669 | 4 / 6 / 6 / 1676 |
| indoor_hub2 | 1 / 3 / 3 / 331 | 1 / 3 / 3 / 344 | 3 / 6 / 6 / 1246 |

- **The current Ultra is GPU-bound in every scene.** The GPU frame equals the frame
  interval within a few milliseconds, the GPU was 81-90% busy over the launches, and the
  CPU's own work (front end plus submit plus backend) is 8-17 ms of a 25-53 ms frame; the
  rest of the CPU frame is the driver waiting for the queue. Cutting CPU work alone would
  not raise the Ultra frame rate on this GPU; section 4.2's sample count and section 4.1's
  shadow maps and post passes are the levers.
- **The shadow passes multiply the draw calls** 1.5x outdoors and 2.3-3.8x in the combat,
  effects and hub2 scenes (point-light cube maps are six passes per light per frame).
  That is also the CPU-side cost that would surface once the GPU is relieved.
- **At equal settings the current build issues the same draw calls as 0.13.2** (420
  against 428, 789 against 788, 669 against 661, 344 against 331), so the storage2
  difference of section 4.2 is not a draw-count or visibility difference; the stack
  samples of the follow-up run attribute it.


## 5. Hitch catalog

**First-use events** (`hitch_firstuse`: one event at a time on a static view, every frame
recorded, two launches per build; "excess" is the event's longest frame minus the pass
median):

| Event | 0.13.2 Ultra: median / longest frame (excess) | current Ultra: median / longest frame (excess) | Where the time goes |
|---|---:|---:|---|
| `give all` (first weapons and their GUIs) | 7.1 / 17.8 ms (+10.7) | 41.5 / 63.6 ms (+22) | spread over several frames (7 above twice the median on 0.13.2) |
| first spawn of a gladiator | 7.2 / 210.6 ms (+203) | 44.0 / 226.9 ms (+183) | the spawn command loads the model, animations, skins and sounds synchronously: 171-212 ms in the `commands` phase on both builds |
| first spawn of a light tank | 7.5 / 136.5 ms (+129) | 44.1 / 175.7 ms (+132) | same class, 124-161 ms in `commands` |
| first explosion | 7.9 / 22.6 ms (+15) | 45.5 / 79.3 ms (+34) | effect, debris and light setup, `draw` dominated |
| first shot (current build only) | | 46.5 / 94.5 ms (+48) | 91 ms in `draw`: first use of the muzzle flash and projectile materials at Ultra |
| scope overlay (current build only) | | 46.0 / 89.9 ms (+44) | `draw` |
| flashlight (current build only) | | 46.7 / 79.6 ms (+33) | `draw` |

The spawn hitches are the same class on both builds and belong to the entity spawn path,
not to rendering. Everything else costs two to three times more excess on the current
Ultra than the same event on 0.13.2, in the draw phase: the first use of a material or
effect at Ultra also builds its shadow-map, post and 16-sample resources.

**Sustained spikes during the sweeps** (matrix, warm passes):

- **hub2, current Ultra: 100-124 ms frames at the same sweep direction every turn** (about
  10-12 s and 25-27 s into each 30 s pass, `draw` 97-123 ms). The point-shadow ladder
  removes them: with `r_shadowMapPointLights 0` the window's p99 fell from 84-116 ms to
  37 ms and its worst frame from 100+ to 43 ms. Lights entering the view re-render their
  cube maps; this is the hitch form of the FPS-03 cost.
- **storage2, current Ultra: one 393 ms frame** (134 ms in `events`, the SDL event pump,
  plus 252 ms in `draw`) 2.4 s into a warm pass; and 98.7 ms with 83 ms in `events` in the
  same pass. Event-pump bursts of this size were seen before under desktop activity; this
  one coincides with no recorded run disturbance and stays in the catalog.
- **0.13.2 convoy1: 113 ms** (70 ms in `events`) once in a warm pass; otherwise 0.13.2's
  worst frames are 24-60 ms, almost all `draw`.
- **convoy1 round 2, current Ultra, cold pass: a disturbed launch.** It recorded 241
  frames over 62 s of wall time, two draw stalls of 2.0 and 3.0 s and 47 s without a
  complete frame, at 21:08-21:10 on 7 November, the minute this session's tooling started
  on the machine and the per-launch GPU sampler stopped getting readings. Rounds 1 and 3
  of the same configuration show a worst frame of 78 ms. It is excluded from the catalog
  as external; it is the reason the cold `worst` column of section 3.1 reads 3016.9 ms for
  that scene.

**Level loads and the transition** (engine `msec to load`, median of the warm launches):
the current build loads airdefense1 in 7.1 s against 9.0 s (game init 3.3 against 4.2 s,
media finish 0.9 against 2.1 s) and changes from airdefense1 to airdefense2 in 1.8 s
against 2.8-3.2 s. Cold loads (a fresh save path, the generated image cache empty) are
0.2-3 s longer on both builds.

**On Vulkan** (`hitch-vk`, two launches per build): the spawn hitches are the same class
and size as on OpenGL (gladiator 176-224 ms, light tank 115-145 ms, 106-210 ms of each in
`commands` on both builds), so they belong to the entity spawn path whatever the backend.
The first-use draw excess that costs the current OpenGL Ultra 33-48 ms per event is 4-16
ms on Vulkan (explosion 31 ms against a 23 ms median, first shot 31, scope 39, flashlight
28): the Vulkan backend restores its pipeline cache at startup (`Vulkan: pipeline cache
restored`) and has no 16-sample resources to build, which places the OpenGL first-use cost
in the modern executor's program and resource builds at Ultra. The airdefense1 to
airdefense2 transition takes 1.8-1.9 s on the current build against 2.8-3.2 s on 0.13.2,
as on OpenGL, with 29-39 ms frames (6-20 ms of session, 13-26 ms of draw) at the current
Ultra while the level streams in, against 17 ms at most on 0.13.2.

**Cold against warm passes.** The first 30 s after a level is live costs the 0.13.2 and
equal-settings builds extra spike frames rather than a lower average (storage2: 86 frames
above twice the median cold against 51 warm on 0.13.2, 122 against 87 on the current
build); at the current Ultra the first-use frames hide inside the 25-53 ms frame time.


## 6. Bisection of the equal-settings difference

The bisection builds one commit per week between the 0.13.2 tag and the current head
(`.tmp/perf/bisect-shas.txt`), stages its client, renderer modules and game library as a
runtime beside the two baselines, and measures it at the 0.13.2 settings against the
0.13.2 rebuild on storage2 and airdefense1 (two passes each, OpenGL). Commits older than
the frame-logger patch have no per-frame trace, so every build is also compared on the
engine's own pacing snapshots (present interval and integer percentiles), which the
report prints for all of them.

Pass 1 (phase 2) built one of the six candidates, 834564945 of 2 October:

| Scenario | pass | 834564945 present ms (Hz) | 0.13.2 present ms (Hz) | delta |
|---|---|---:|---:|---:|
| indoor_storage2 | cold | 2.92 (342.9) | 2.83 (354.4) | +0.09 (+3.2%) |
| indoor_storage2 | warm | 2.90 (345.2) | 2.85 (351.0) | +0.04 (+1.6%) |
| outdoor_airdefense1 | cold | 4.74 (211.4) | 4.78 (209.2) | -0.04 (-0.9%) |
| outdoor_airdefense1 | warm | 4.86 (205.8) | 4.79 (209.0) | +0.08 (+1.6%) |

The 2 October build is within 3% of 0.13.2 in both scenes, so the storage2 difference of
sections 3.1 and 4.2 (+1.0-1.2 ms per frame at equal settings, 0.9-1.0 ms of it in the
draw phase) entered after 2 October. The other five candidates (1083fc9ec, 156ced3fd,
56a74bd48, b9c663758, 8a44904fb) did not build in pass 1: a dedicated-server link
failure (the optimized-MSVC COMDAT trap), a stale extracted RmlUi subproject tree in the
shared bisection worktree (`VectorElement.h(108) C3668`), and a tracked resource file
(`openQ4Version.rc`) the worktree had lost. Pass 2 (phase 3) rebuilds them client-only
after `git reset --hard` and a clean subproject extraction, staging from the build
directory.

**What the storage2 difference is made of.** The phase-3 attribution launches
(`attrib-storage2-gl`, `attrib-storage2-vk`: two launches per build and backend at the
0.13.2 settings, renderer metrics, GPU timers and 20 s of main-thread stacks, with the
symbols of both builds resolved this time). Warm pass, median of the two launches:

| | 0.13.2, OpenGL | current, OpenGL | 0.13.2, Vulkan | current, Vulkan |
|---|---:|---:|---:|---:|
| mean frame ms (fps) | 3.41 (294) | 3.91 (256) | 2.85 (351) | 3.63 (275) |
| session / draw phase ms | 0.33 / 3.05 | 0.38 / 3.50 | 0.29 / 2.54 | 0.38 / 3.22 |
| GPU frame ms (benchmark capture; 0.13.2 has none) | | 2.65-2.85 | | 3.49-3.62 |
| draws per frame / draw-view commands per frame | 289-292 / 8 | 298-301 / 18 | 295 / 8 | 302-304 / 18 |
| main thread in the HUD draw (`idPlayer::DrawHUD`, inclusive) | under 6% (under 0.2 ms) | 14-16% (0.56-0.61 ms) | 4.2-4.4% (0.12 ms) | 15.3-15.6% (0.55-0.57 ms) |
| main thread in the scene front end (`R_RenderView`) | 24% (0.82 ms) | 21% (0.83 ms) | 29% (0.82 ms) | 23% (0.82 ms) |
| main thread in the backend less the present wait | 0.90 ms | 1.09 ms | 0.73 ms | 0.89 ms |
| main thread in the game tic (`idSessionLocal::Frame`) | 9% (0.31 ms) | 8% (0.32 ms) | 10% (0.28 ms) | 10-11% (0.37 ms) |

- The current build is +0.50 ms per frame on OpenGL and +0.78 ms on Vulkan in these
  launches (+15% and +27%); the drift windows of section 4.2 read +1.0-1.2 ms on OpenGL
  in the same regime, so the difference moves with what the scene shows.
- **The retained HUD is about 0.4 ms of main-thread time per frame.** On the current
  build `idPlayer::DrawHUD` leads to `idUserInterfaceLocal::DrawRemasteredHud`,
  `RetainedUI_DrawViewRoot` and the RmlUi frame (`openq4::ui::Runtime::Frame`,
  `Rml::Context::Render`), 14-16% of the thread against 4% for 0.13.2's classic HUD. On
  OpenGL storage2 is CPU-bound at these settings (GPU frame 2.7-2.9 ms of a 3.9 ms
  interval; 13% of the thread waiting in the swap against 19% on 0.13.2), so the cost
  reaches the frame time; the repeated ladder's `ui_retained 0` window recovers 0.22-0.35
  ms of it. The retained HUD was excluded as a cause in section 4.1 on airdefense1, where
  the frame is GPU-bound and CPU work is hidden; storage2 is where it shows.
- **On Vulkan the frame is GPU-bound at these settings** (GPU frame 3.5-3.6 ms, the frame
  interval 3.63), so its +0.78 ms is GPU work the 0.13.2 backend did not issue, the HUD's
  own draws included; the backend's main-thread time less the wait grows 0.16-0.19 ms on
  both backends.
- **The ten extra draw-view commands per frame are the retained HUD's.** The current
  build issues 18 against 0.13.2's 8 in storage2 on both backends, with 7-9 more draws,
  where airdefense1, combat, effects and hub2 show the same count on both builds (8-9). A
  per-frame metrics run (`r_rendererMetrics 2`, `viewdiag2`) breaks the current build's
  storage2 frame into 1 3D view, 17 2D views and 34 render-target commands with the
  retained HUD; with `ui_retained 0` the same frame has 7 2D views and 17 render-target
  commands, exactly 0.13.2's, and with the HUD off 6 and 18. The retained HUD composes its
  layers through offscreen targets (`BindTarget` in `src/ui/RetainedUI.cpp`, the layer and
  mask composition in `src/ui/retained/Runtime.cpp`), and every render-target bind flushes
  the pending 2D batch as a full-screen view (`BindRenderTexture` in
  `src/renderer/RenderSystem.cpp`): in storage2's HUD state that is ten extra 2D views and
  seventeen render-target switches per frame, each a viewport and scissor setup, a
  command round trip and, on Vulkan, a render-pass boundary with attachment store and
  load. That is where the HUD's 0.4 ms of main-thread time and most of the Vulkan GPU
  share go; airdefense1's HUD state at its test position composes no such layers (9 views
  on both builds), which is why the ladders there saw nothing. The scene front end, the
  game tic and the visible surfaces are otherwise equal, so the storage2 difference is
  not visibility or interaction work.

**Pass 2** rebuilt the candidates client-only. The 15 October build (156ced3fd) carries
the storage2 difference: warm pass 5.08 ms against 0.13.2's 4.14 (+0.93 ms, +22%; the
cold passes, 5.45 against 3.24, straddle the scene's regime step because the newer build
loads faster) while airdefense1 is within 2% (4.90 against 4.81 ms). With the 2 October
build within 3% of 0.13.2, the storage2 step entered between 2 and 15 October, the window
in which 7b7671c86 (8 October, "Bring the gameplay HUD into the new UI with the Remastered
layout") made the retained HUD the gameplay HUD; that is the composition the attribution
above names. The 25 September build did not build in pass 2 (its own build wrapper
rejected the keep-going argument; it is bracketed by the equal 2 October build), and the
22 October, 29 October and 3 November builds were left unmeasured when the chain was
paused on the morning of 8 November; they would only confirm persistence of a step the
current build still shows.

On Vulkan the equal-settings difference is larger and present in every scene (section
3.3). Its first attributed component is the soft-particle depth capture, 1.3 ms in
airdefense1, a feature the 0.13.2 backend did not have; the rest of the Vulkan share and
the storage2 difference on both backends are what the phase-3 ladders and the storage2
attribution pursue.

## 7. Headroom against the 220 / 180 targets

The targets are 220 fps average (a 4.55 ms mean frame) and 180 fps 1% low (a 5.56 ms
p99) at 2560x1440. The suite's `needs` column is the factor by which a configuration's
mean frame time and p99 must shrink to reach them; warm passes, median of three
launches, average / 1% low (the walker scene is one pass per launch):

| Scenario | 0.13.2 Ultra, OpenGL | 0.13.2 Ultra, Vulkan | current at 0.13.2 settings, OpenGL | current at 0.13.2 settings, Vulkan | current Ultra, OpenGL | current Ultra, Vulkan |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 1.17 / 1.55 | 1.00 / 1.46 | 1.30 / 2.05 | 1.45 / 1.87 | 5.51 / 8.18 | 3.19 / 3.98 |
| outdoor_convoy1 | 1.40 / 2.00 | 1.14 / 1.44 | 1.37 / 1.97 | 1.61 / 1.75 | 8.00 / 11.25 | 3.03 / 3.35 |
| combat_airdefense1 | 1.76 / 2.48 | 1.45 / 1.65 | 1.66 / 2.03 | 1.94 / 2.97 | 8.66 / 10.78 | 4.26 / 4.57 |
| effects_airdefense1 | 1.72 / 1.90 | 1.43 / 1.67 | 1.84 / 2.88 | 1.95 / 2.33 | 8.76 / 11.39 | 4.36 / 4.77 |
| indoor_storage2 | 0.73 / 1.00 | 0.59 / 0.79 | 0.81 / 1.22 | 0.77 / 0.98 | 5.90 / 8.14 | 3.23 / 4.76 |
| indoor_hub2 | 0.94 / 1.17 | 0.90 / 1.11 | 0.95 / 1.16 | 1.12 / 1.57 | 11.76 / 16.98 | 4.44 / 5.66 |
| vehicle_walker | 1.01 / 1.10 | 0.81 / 0.92 | 1.03 / 1.13 | 1.22 / 1.48 | 3.79 / 4.97 | 1.81 / 2.15 |

A factor at or below 1.00 means the target is met.

- **The targets describe the 0.13.2 class of settings in the lighter scenes, on this
  GPU.** 0.13.2's own Ultra reaches 220 average only in storage2 and hub2 on OpenGL (303
  and 235 fps) and in airdefense1, storage2, hub2 and the walker on Vulkan; its 1% low
  reaches 180 only in storage2 (180 on OpenGL, 228 on Vulkan) and the walker (195 on
  Vulkan). The combat and effects waves miss both targets on every build and backend,
  0.13.2 included (x1.4-1.8 average), because their frame is set by the scripted
  battle's lights, particles and casters, not by a setting.
- **Closing the equal-settings difference returns the current build to that line, no
  further.** At the 0.13.2 settings the current build needs x0.8-1.95; removing the code
  share of sections 3.1 and 3.3 (0.3-1.2 ms on OpenGL, 0.8-2.3 ms on Vulkan) puts it where
  0.13.2 is, which still misses 220 in the outdoor, combat and effects scenes.
- **No optimization of the current Ultra recipe reaches the targets on this GPU.** The
  current Ultra needs x5.5-11.8 average on OpenGL and x3.0-4.4 on Vulkan, x8-17 and
  x3.4-5.7 for the 1% low. The measured levers (sections 4.1 and 4.2, OpenGL warm): MSAA
  16 to 4 halves the frame (x1.9-2.2); the post chain off from launch x1.4-1.6; shadow maps
  off x1.2-2.6 (hub2 54 to 20 ms). Multiplied they reach the 0.13.2-settings frame, because
  together they are the 0.13.2 settings. A re-tiered Ultra with 4 samples and SMAA, shadow
  maps under a point-light budget, half-resolution SSAO and the tone map would land, by
  these ladders, near 8-12 ms outdoors (85-125 fps) and 10-15 ms in hub2 on this GPU: a
  100 fps class tier at 2560x1440 on an RTX 4060 Laptop, not 220. The tier that holds
  220 / 180 here is the 0.13.2-class recipe (8 samples, SMAA medium, stencil shadows, no
  screen-space chain), and only in the lighter scenes.
- **The 1% low target is the harder half.** Where the average passes, the p99 often does
  not (hub2 on OpenGL: 235 average, 155 fps 1% low on 0.13.2); section 5 places those
  frames in scripted spawns, first-use loads and the point-light cube re-renders, so the
  1% low target needs the hitch work of section 8 as much as the frame-rate work, and a
  definition that says whether level-script spawn frames count.
- **Transfer to other machines.** Every figure here is from the RTX 4060 Laptop, which
  FPS-01 made the reference machine; the ratios between configurations transfer (the
  sample count, shadow maps and post chain cost the same share of a frame on a faster card
  of the same architecture), the absolute frame rates do not. Section 10 holds the
  absolute budgets for this machine; the roadmap's targets are to be declared per tier and
  hardware, and 220 / 180 should name the tier and the machine it applies to.

## 8. Optimization roadmap

The plan lives in the [development roadmap](roadmap.md#frame-rate-recovery-and-performance-budgets)
as the FPS category; this is its summary, in the order the measurements rank the work.

| Task | What it does | Why, from this report |
|---|---|---|
| FPS-01 performance suite, baselines and budgets (closed) | Keep `performance_suite.py` the single frame-rate measurement; both backends measured on the reference machine; a warm-mean and p99 budget declared per scenario, tier and backend (section 10); the `check` table and the report's `aggregate.json` go beside the release notes. | A fixed line to measure the work below, and every release, against (section 10). |
| FPS-02 re-tier the presets to declared budgets | Rebuild Ultra, Quality and Enhanced from the measured feature costs; declare the resolution and frame-time budget each tier must hold; keep an explicit "everything on" choice with no frame-rate claim; decide the fresh-install cap, anisotropy and tier; fix the startup banner. | The preset, not the code, carries the 4-12x drop (section 3.1); fresh installs lost half their frame rate to a 120 cap and 4x anisotropy (section 3.2). |
| FPS-03 shadow-map cost | Compose dynamic casters over cached point-light cube maps, cull cube faces, apply the update budget at every tier, size the atlas by tier and video memory, cheaper PCSS with distance, remove the quadratic light scan on Vulkan. | 30 of hub2's 54 ms are point-light cube maps re-rendered every frame; they also produce the periodic 100 ms frames (sections 4.1, 5). |
| FPS-04 post-processing chain | Half-resolution SSAO on OpenGL, pass merging, MSAA-aware resolves, tone-map and motion-blur sample policy per tier. | 10-14 ms at 16 samples, 2-3 ms per pass at 4 (sections 4.1, 4.2). |
| FPS-05 MSAA and the HDR scene target | Sample-count policy per tier and resolution, resolve cost, MSAA 4 + SMAA as the Ultra candidate, the effective count in `gfxInfo` and the settings menu. | 16 samples are the largest single cost on OpenGL (12-29 ms per frame, section 4.2); the Vulkan backend clamps the same preset to 8 samples, so the two backends render different Ultra images and the Vulkan Ultra is 1.7-2.7x faster for that reason alone (section 3.3). |
| FPS-06 equal-settings regressions | Compose the retained HUD's layers in one pass instead of a render-target round trip per layer; make the Vulkan soft-particle capture read the prepass depth without breaking the forward pass; bisect the rest; close within 3% average and 10% p99 of 0.13.2 on every scene. | Section 6: the retained HUD's layer composition is ten extra 2D views and seventeen render-target switches per frame in storage2 (0.4 ms of main-thread time on OpenGL, GPU render-pass boundaries on Vulkan); section 3.3: the Vulkan soft-particle capture (1.3 ms in airdefense1); the five new Vulkan backend features cost nothing measurable. |
| FPS-07 hitch catalog | Attribute and remove the first-use and sustained spikes of section 5 or give them budgets; a declared ceiling for the hitch scenario. | Section 5. |
| FPS-08 CPU-side renderer and game cost | Front end, submission and tic cost once the GPU is relieved; render-thread stage 2 under REN-07; shared with HF-07. | Draw calls multiply 1.5-3.8x with shadow maps; the Quality floor of HF-07 is CPU-bound; the Vulkan render-thread prototype gains nothing at the GPU-bound Ultra and lengthens the tails (section 3.3). |
| FPS-09 regression gate | The suite in the release checklist with the FPS-01 budgets as gates, the cvar-default diff, the preset self-test and the toggle ladder as commit evidence. | Section 9. |


## 9. Regression prevention

1. **Run the suite before every release and on every renderer, game-loop or
   asset-loading change that reaches `main`.** `tools/tests/performance_suite.py run
   --scenarios timed --rounds 3 --passes 2` with `--config` entries for `own-ultra`,
   `own-quality` and `own-enhanced` on the reference machine with the staged `.install`,
   then `report` and `check --budgets docs/dev/performance-budgets.json`. The report's
   `summary.md` and `aggregate.json` and the `check` table are the artifacts to keep next
   to the release notes (`docs/dev/releases/vX.Y.Z.md`).
2. **Budgets per scenario, not one global number.** Section 10 records, for each timed
   scenario, tier and backend, the median warm mean frame time and p99 frame time on the
   reference machine; `check` fails a candidate when either exceeds its budget by more
   than 5 % (mean) or 10 % (p99). Rerun a failing scenario before treating it as a
   regression (section 10.4), and also fail the release when a new hitch class (a frame
   above 2x the median that was not in the previous catalog) appears in
   `hitch_firstuse`. When a release intentionally changes a preset, redeclare with
   `budgets` and commit the new file with the release notes.
3. **Per-system budgets** from the attribution runs: game tic, renderer front end,
   backend submit, GPU, sound update and HUD each get a millisecond budget at Ultra
   2560x1440 on the reference machine; the `profileFrameWork` phase means and
   `r_rendererMetrics 1` summaries are compared against them. A system that grows
   past its budget needs a documented reason in the commit.
4. **Automated checks that need no GPU time**: the native `FrameWorkProfile` test,
   the cvar-default diff (`listCvars` of a fresh config against the committed
   baseline) that fails when a default with a performance cost changes without a
   release note, and the preset self-test (`performancePresetSelfTest`) stay in CI.
5. **Keep the cheap in-launch A/B kit.** The toggle ladder (`--toggle`) answers
   "what does this feature cost here" in one launch; use it before and after
   touching any post effect, shadow or presentation code, and paste the two ladder
   rows into the commit message.
6. **Machine hygiene for measurements**: no builds, no second client, no console
   utilities, a visible fullscreen window; the suite records CPU and GPU load per
   launch so a disturbed run can be recognised, repeats on its own a launch whose GPU
   stayed idle (the client missed the foreground and rendered nothing), and flags any
   that still did under **Warnings** in the report.

## 10. Declared budgets (FPS-01)

These are the frame-time budgets of the current presets on the reference machine, the
RTX 4060 Laptop of section 2, fullscreen 2560x1440, frame cap and vsync off. They are
regression ceilings for the build as it stands, not targets: no tier holds 220 / 180 in
every scene. FPS-02 has since re-tiered the presets and redeclared the budgets (section
11); [`performance-budgets.json`](performance-budgets.json) holds section 11's budgets,
and the tables below remain the record of the presets before the re-tier. The FPS-01
Ultra recipe lives on as the Maximum preset, so section 10.1 is what Maximum costs.

- **Build.** `main` 94e0b14ec, debugoptimized with `b_ndebug=true` (the release
  configuration), staged into the fixed runtime `.tmp/stock-runtime/perf-ref`, client
  SHA-256 `44064cda370395a79c14a2e899647027ad113283d7c03e155e24f476ed2efd4b`.
- **Run.** `ref-gl` and `ref-vk` (appendix): the seven timed scenarios at the build's own
  `own-ultra`, `own-quality` and `own-enhanced` profiles, the three tiers interleaved,
  three rounds of two passes each, 63 launches per backend. One Vulkan Quality launch
  missed the foreground and was repeated.
- **Budget.** For each scenario, tier and backend: the median over the three launches of
  the warm pass's (p2) mean frame interval and p99 frame interval from
  `profileFrameWork`. The gate limit is the budget plus 5 % (mean) or 10 % (p99).
  The walker scene measures one pass per launch, so its budget is that cold pass. In
  storage2 every launch measures the same window position relative to the map script's
  frame-time doubling (section 4.2), so the p2 windows compare like with like.

Commands:

```
python tools/tests/performance_suite.py budgets --runs .tmp/perf-suite/ref-gl .tmp/perf-suite/ref-vk \
    --machine "RTX 4060 Laptop reference (MSI Vector GP77 13VF)" --out docs/dev/performance-budgets.json
python tools/tests/performance_suite.py check --budgets docs/dev/performance-budgets.json --runs <candidate runs>
```

`check` prints one row per budget (budget, measured, delta, verdict), notes a host or
display that differs from the reference, exits 1 on any failure, and with
`--require-all` also fails a budget the candidate did not measure. The reference runs
checked against their own file pass all 42 budgets.

### 10.1 Ultra

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 25.18 (40) | 42.77 (23) | 26.44 / 47.05 | 14.34 (70) | 21.39 (47) | 15.06 / 23.53 |
| outdoor_convoy1 | 34.98 (29) | 55.26 (18) | 36.73 / 60.79 | 16.07 (62) | 20.73 (48) | 16.87 / 22.80 |
| combat_airdefense1 | 39.19 (26) | 50.74 (20) | 41.15 / 55.81 | 18.97 (53) | 23.21 (43) | 19.92 / 25.53 |
| effects_airdefense1 | 39.98 (25) | 52.98 (19) | 41.98 / 58.28 | 19.49 (51) | 25.14 (40) | 20.46 / 27.65 |
| indoor_storage2 | 26.68 (38) | 40.92 (24) | 28.01 / 45.01 | 14.54 (69) | 27.14 (37) | 15.27 / 29.85 |
| indoor_hub2 | 51.54 (19) | 87.12 (12) | 54.12 / 95.83 | 20.87 (48) | 31.34 (32) | 21.91 / 34.47 |
| vehicle_walker | 17.25 (58) (single pass) | 29.56 (34) | 18.11 / 32.52 | 8.22 (122) (single pass) | 10.62 (94) | 8.63 / 11.68 |

### 10.2 Quality

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 6.82 (147) | 10.92 (92) | 7.16 / 12.01 | 6.71 (149) | 9.63 (104) | 7.05 / 10.59 |
| outdoor_convoy1 | 10.24 (98) | 13.94 (72) | 10.75 / 15.33 | 9.84 (102) | 28.61 (35) | 10.33 / 31.47 |
| combat_airdefense1 | 9.73 (103) | 17.36 (58) | 10.22 / 19.10 | 8.89 (112) | 13.26 (75) | 9.34 / 14.59 |
| effects_airdefense1 | 9.50 (105) | 11.77 (85) | 9.97 / 12.95 | 8.64 (116) | 13.72 (73) | 9.07 / 15.09 |
| indoor_storage2 | 7.07 (141) | 11.13 (90) | 7.42 / 12.24 | 6.67 (150) | 11.47 (87) | 7.00 / 12.62 |
| indoor_hub2 | 11.20 (89) | 19.90 (50) | 11.76 / 21.89 | 8.57 (117) | 12.52 (80) | 9.00 / 13.77 |
| vehicle_walker | 5.83 (172) (single pass) | 8.31 (120) | 6.12 / 9.14 | 5.21 (192) (single pass) | 8.03 (124) | 5.47 / 8.83 |

### 10.3 Enhanced

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 5.65 (177) | 10.03 (100) | 5.93 / 11.03 | 5.16 (194) | 6.84 (146) | 5.42 / 7.52 |
| outdoor_convoy1 | 6.48 (154) | 8.63 (116) | 6.80 / 9.49 | 6.12 (163) | 18.24 (55) | 6.43 / 20.06 |
| combat_airdefense1 | 8.11 (123) | 15.07 (66) | 8.52 / 16.58 | 6.92 (144) | 13.12 (76) | 7.27 / 14.43 |
| effects_airdefense1 | 7.67 (130) | 15.21 (66) | 8.05 / 16.73 | 6.59 (152) | 16.70 (60) | 6.92 / 18.37 |
| indoor_storage2 | 6.25 (160) | 11.68 (86) | 6.56 / 12.85 | 4.52 (221) | 7.91 (126) | 4.75 / 8.70 |
| indoor_hub2 | 6.90 (145) | 10.24 (98) | 7.25 / 11.26 | 5.97 (168) | 8.52 (117) | 6.27 / 9.37 |
| vehicle_walker | 4.67 (214) (single pass) | 7.96 (126) | 4.90 / 8.76 | 4.29 (233) (single pass) | 7.65 (131) | 4.50 / 8.41 |

### 10.4 Notes on the budgets

- **The Ultra budgets differ by backend for a reason.** Vulkan clamps the preset's 16
  samples to 8 (section 3.3), so the two backends' Ultra budgets measure different
  images. FPS-05 makes the sample count the same; the budgets follow.
- **Vulkan convoy1 at Quality and Enhanced carries a presentation stall.** The warm
  window of two of the three Quality launches and one of the three Enhanced launches
  holds a run of slow frames (87 and 151 frames above 25 ms, worst 121 ms, at Quality;
  41 frames, worst 99 ms, at Enhanced). The other launches, OpenGL and the Vulkan Ultra
  launches show none. The median keeps the stall in those two p99 budgets (28.61 and
  18.24 ms against means of 9.84 and 6.12 ms). Corrected on 9 November (FPS-02): this
  is not a game-side hitch. The slow frames arrive steadily, about 45 a second, 20 to
  31 seconds into the warm pass, while the per-frame work stays short; the time is
  spent waiting on presentation. Some of those launches log `GPU frame timing reset:
  reason=Vulkan swapchain recreation` at that moment. It belongs to the Vulkan
  presentation work under FPS-07; once it is fixed, redeclare those budgets lower.
- **p99 noise between days.** Checking the 7 November Ultra runs of d856fe6bb
  (`matrix-gl`/`matrix-vk`, label `head`) against these budgets: every mean is within
  4 % except Vulkan convoy1 (14 % faster, inside that scene's launch spread of 58-71
  fps). The OpenGL p99 is 11-20 % higher in four scenes, and the Vulkan walker p99 12 %
  higher. That run interleaved three builds and is a different build, so it does not
  separate a tail improvement from noise. Until FPS-09 settles the round count, treat a
  p99-only failure as provisional: rerun that scenario before calling it a regression.

## 11. Re-tiered presets (FPS-02)

FPS-02 rebuilt Enhanced, Quality and Ultra so that each holds a declared frame-rate
class on the reference machine, kept the previous Ultra recipe as an explicit
everything-on preset named **Maximum** that carries no frame-rate target, and
redeclared the budgets of section 10 from the re-tiered build. The machine-readable
budgets in [`performance-budgets.json`](performance-budgets.json) are now this
section's.

### 11.1 Refresh classes

Each class applies to every timed scenario, on OpenGL and on Vulkan, as the warm-pass
mean and p99 frame interval on the reference machine (RTX 4060 Laptop, fullscreen
2560x1440, frame cap and vsync off):

| Preset | Average fps | 1% low fps | Mean ms | p99 ms |
|---|---:|---:|---:|---:|
| Enhanced | 144 | 90 | 6.94 | 11.1 |
| Quality | 100 | 60 | 10.0 | 16.7 |
| Ultra | 75 | 45 | 13.3 | 22.2 |
| Maximum | none | none | none | none |

The classes follow common display refresh rates, so a tier names the display it can
keep fed. The 220 fps average and 180 fps 1% low of section 7 stay a goal for the
lighter scenes, not a tier gate: no recipe with shadow-mapped lights and the modern
post chain reaches it in hub2, combat or convoy1 on this GPU.

### 11.2 What the levers cost

The candidates were screened on the FPS-01 reference runtime (`perf-ref`, 94e0b14ec):
each was a shipped preset with cvar overrides (`<preset>+cvar=value`), measured in five
scenes (combat, effects, hub2, convoy1, storage2), one launch of two passes per scene
and backend (`screen-*`, `screen2-*`, `screen3-*` in the appendix). The table gives
the worst scene's warm mean and p99 in milliseconds.

| Candidate | OpenGL worst mean / p99 | Vulkan worst mean / p99 |
|---|---:|---:|
| Enhanced (FPS-01): 2x MSAA, all lights shadow-mapped | 8.11 / 15.07 | 6.92 / 16.70 |
| Enhanced, point lights on stencil | 6.26 / 10.9 | 6.15 / 9.1 |
| Enhanced, MSAA off | 6.74 / 12.8 | 5.51 / 9.4 |
| Enhanced, MSAA off, point lights on stencil, 16x AF (chosen) | 6.12 / 10.4 | 4.82 / 8.5 |
| Enhanced, every light on stencil | 6.41 / 10.7 | 6.28 / 9.7 |
| Quality (FPS-01): 4x MSAA, 1024 point cubes | 11.20 / 19.90 | 9.84 / 28.61 |
| Quality, 512 point cubes | 9.54 / 14.0 | 10.16 / 12.2 |
| Quality, 2x MSAA | 10.24 / 18.4 | 8.82 / 11.2 |
| Quality, 2x MSAA, 512 point cubes (chosen) | 8.67 / 11.8 | 9.07 / 12.3 |
| Ultra base: Quality with 4096 projected maps, 13 taps, 8192 atlas, 32 SSAO, SMAA ultra | 13.13 / 21.6 | 11.81 / 15.1 |
| base + four cascades | 17.00 / 23.5 | 11.91 / 15.3 |
| base + 8-sample motion blur | 13.75 / 22.6 | 12.00 / 15.6 |
| base + 2048 point cubes | 22.07 / 49.9 | 15.77 / 26.6 |
| base with 2x MSAA | 12.12 / 21.1 | 10.36 / 13.8 |
| base with 512 point cubes (chosen) | 11.11 / 14.7 | 10.64 / 14.4 |

The FPS-01 rows are the section 10 budgets of the same runtime.

- **Point-light cube maps are the lever.** In hub2 on OpenGL, 2048 cubes double the
  Ultra base's p99 (21.6 to 49.9 ms) and 512 cubes cut it to 14.7; no other single
  change moves a tier as far. At Enhanced, leaving point lights on the stencil shadows
  retail used frees 2-4 ms of p99 in every scene.
- **Projected shadow maps at 1024 cost nothing over stencil.** Enhanced with every
  light on stencil is no faster than Enhanced with only point lights on stencil, so
  Enhanced keeps shadow maps for projected and parallel lights.
- **Cascades cost about 4 ms per frame on OpenGL** in the airdefense1 scenes and hub2,
  and next to nothing on Vulkan. Ultra leaves them to Maximum. At an 8192 atlas the
  Vulkan backend also logged shadow-pass failures with cascades on; Maximum's 16384
  atlas does not.
- **MSAA is worth about 1 ms per halving** once point cubes are small (Ultra base 4x to
  2x: 13.1 to 12.1 ms on OpenGL, 11.8 to 10.4 on Vulkan). Ultra keeps 4x, which both
  backends honour, so the two backends now render the same Ultra image (FPS-05).
- **Motion blur at 8 samples costs about 0.6 ms**; Ultra leaves it to Maximum.
- **Anisotropic filtering is free** at this resolution; every modern tier uses 16x.

### 11.3 The recipes

| Preset | Changes from the FPS-01 recipe |
|---|---|
| Enhanced | MSAA 2x to off (SMAA high stays), point lights back on stencil shadows (`r_shadowMapPointLights 0`; projected and parallel lights keep 1024 maps), anisotropy 4x to 16x, frame cap 120 to 144. |
| Quality | MSAA 4x to 2x, point cubes 1024 to 512, anisotropy 8x to 16x. |
| Ultra | MSAA 16x request to 4x, point cubes 2048 to 512 with 9-tap fixed filtering, atlas 16384 to 8192, cascades and motion blur off. Projected maps stay 4096 with 13-tap PCSS; SSAO 32 samples, SMAA ultra and the 240 cap stay. |
| Maximum | New: the FPS-01 Ultra recipe unchanged. Its cost is section 10.1. |

`r_shadowMapPointLights` was already a preset target fixed at 1; it now follows the
preset table, so the SYSTEM catalog keeps its target list. Auto-Detect still never
recommends Quality or higher.

### 11.4 Fresh installs and the startup banner

- **Frame cap.** An automatic recommendation (Auto-Detect, or the first launch of a
  fresh installation) caps `com_maxfps` at the whole-hertz refresh rate of the display
  the window is on, and keeps the tier's own cap when that rate is unknown or outside
  30-1000 Hz. A preset chosen by name keeps its table cap. On the reference machine's
  240 Hz panel the detected Enhanced therefore presents up to 240 fps instead of the
  120 that section 3.2 measured.
- **Banner.** The `Quality profile:` line now says when it is printing the first-launch
  bootstrap preset, and prints the detected profile again once detection has applied
  it, with `com_videoRam` corrected to the measured video memory (8188 MB on OpenGL,
  7956 MB on Vulkan on this laptop) instead of the bootstrap 64 MB.

### 11.5 The Vulkan point-light policy fix

With point-light shadow maps disabled by policy, the Vulkan shadow-map admission loop
tested the light's casters and the atlas capacity before the policy, found no map
casters for a point light (none are collected when its maps are off), and reported a
hard `shadow map pass failed for lightDef` that stuck to the light. The light still
drew stencil shadows, but the failure was logged and counted in every Vulkan launch
with point maps off. The policy check now runs before the caster and capacity checks
(`src/renderer/Vulkan/vk_ShadowMap.cpp`). The Vulkan smoke at Enhanced logs no failure
and draws the same 41 stencil volumes, and `renderer_vulkan_shadow_compatibility.py`
passes.

### 11.6 Results and budgets

- **Build.** `main` a4f7f2002 with the FPS-02 change (committed as the FPS-02 commit),
  debugoptimized with `b_ndebug=true`, staged into `.tmp/stock-runtime/perf-head`,
  client SHA-256 `6d1e35432d096b389502ffb1551b074d0b711b9d688a7bf36df91c04f9d69f1b`.
- **Run.** `fps02-gl` and `fps02-vk`: the seven timed scenarios at `own-ultra`,
  `own-quality` and `own-enhanced`, interleaved, three rounds of two passes, 63
  launches per backend. Every launch held the foreground; none was repeated. No launch
  logged a shadow-pass failure or a Vulkan swapchain recreation.
- **Budgets.** Redeclared with the section 10 command and method (`--runs
  .tmp/perf-suite/fps02-gl .tmp/perf-suite/fps02-vk`); 42 entries from 126 launches.
  The runs checked against the new file pass all 42 with `--require-all`.

Every tier meets its class in every scene on both backends:

| Preset | Class mean / p99 ms | OpenGL worst mean / p99 ms (fps) | Vulkan worst mean / p99 ms (fps) |
|---|---:|---:|---:|
| Enhanced | 6.94 / 11.1 | 6.03 combat / 10.39 combat (166 / 96) | 5.04 combat / 8.98 combat (198 / 111) |
| Quality | 10.0 / 16.7 | 8.30 hub2 / 11.70 combat (120 / 86) | 7.61 hub2 / 10.69 combat (131 / 94) |
| Ultra | 13.3 / 22.2 | 10.97 hub2 / 15.40 storage2 (91 / 65) | 10.40 storage2 / 15.01 storage2 (96 / 67) |

Across the seven scenes Enhanced runs 166-298 fps, Quality 120-238 and Ultra 91-188.
Against FPS-01, hub2 at Ultra on OpenGL went from 19 to 91 fps.

#### 11.6.1 Ultra

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 6.70 (149) | 9.34 (107) | 7.04 / 10.27 | 7.65 (131) | 9.88 (101) | 8.03 / 10.87 |
| outdoor_convoy1 | 8.90 (112) | 12.71 (79) | 9.35 / 13.98 | 8.64 (116) | 12.76 (78) | 9.07 / 14.04 |
| combat_airdefense1 | 9.32 (107) | 12.95 (77) | 9.79 / 14.24 | 9.67 (103) | 11.58 (86) | 10.15 / 12.74 |
| effects_airdefense1 | 9.03 (111) | 11.65 (86) | 9.48 / 12.81 | 10.22 (98) | 12.42 (80) | 10.73 / 13.66 |
| indoor_storage2 | 9.36 (107) | 15.40 (65) | 9.83 / 16.94 | 10.40 (96) | 15.01 (67) | 10.92 / 16.51 |
| indoor_hub2 | 10.97 (91) | 14.45 (69) | 11.52 / 15.89 | 9.38 (107) | 12.46 (80) | 9.85 / 13.71 |
| vehicle_walker | 5.81 (172) (single pass) | 8.46 (118) | 6.10 / 9.31 | 5.32 (188) (single pass) | 6.08 (164) | 5.59 / 6.69 |

#### 11.6.2 Quality

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 5.46 (183) | 9.60 (104) | 5.73 / 10.56 | 5.42 (185) | 7.63 (131) | 5.69 / 8.39 |
| outdoor_convoy1 | 7.31 (137) | 10.48 (95) | 7.67 / 11.53 | 6.36 (157) | 9.46 (106) | 6.68 / 10.41 |
| combat_airdefense1 | 7.92 (126) | 11.70 (86) | 8.32 / 12.87 | 7.27 (138) | 10.69 (94) | 7.63 / 11.76 |
| effects_airdefense1 | 7.53 (133) | 10.19 (98) | 7.91 / 11.21 | 7.35 (136) | 9.01 (111) | 7.72 / 9.91 |
| indoor_storage2 | 5.70 (176) | 7.33 (136) | 5.99 / 8.06 | 5.14 (194) | 6.44 (155) | 5.40 / 7.08 |
| indoor_hub2 | 8.30 (120) | 11.33 (88) | 8.71 / 12.46 | 7.61 (131) | 10.07 (99) | 7.99 / 11.08 |
| vehicle_walker | 4.55 (220) (single pass) | 6.04 (166) | 4.78 / 6.64 | 4.19 (238) (single pass) | 4.85 (206) | 4.40 / 5.33 |

#### 11.6.3 Enhanced

| Scenario | GL mean ms (fps) | GL p99 ms (1% low) | GL gate mean / p99 ms | VK mean ms (fps) | VK p99 ms (1% low) | VK gate mean / p99 ms |
|---|---:|---:|---:|---:|---:|---:|
| outdoor_airdefense1 | 4.42 (226) | 6.95 (144) | 4.64 / 7.64 | 3.92 (255) | 5.23 (191) | 4.12 / 5.75 |
| outdoor_convoy1 | 4.83 (207) | 6.87 (146) | 5.07 / 7.56 | 4.09 (244) | 6.28 (159) | 4.29 / 6.91 |
| combat_airdefense1 | 6.03 (166) | 10.39 (96) | 6.33 / 11.43 | 5.04 (198) | 8.98 (111) | 5.29 / 9.88 |
| effects_airdefense1 | 5.36 (187) | 8.38 (119) | 5.63 / 9.22 | 4.58 (218) | 7.29 (137) | 4.81 / 8.02 |
| indoor_storage2 | 4.15 (241) | 5.69 (176) | 4.36 / 6.26 | 3.36 (298) | 4.46 (224) | 3.53 / 4.91 |
| indoor_hub2 | 3.87 (258) | 5.65 (177) | 4.06 / 6.21 | 3.43 (292) | 4.14 (242) | 3.60 / 4.55 |
| vehicle_walker | 3.93 (254) (single pass) | 4.56 (219) | 4.13 / 5.02 | 3.50 (286) (single pass) | 4.06 (246) | 3.67 / 4.47 |

### 11.7 Notes

- **Enhanced has the least margin.** The OpenGL combat wave uses 87 % of the mean class
  and 94 % of the p99 class. There the regression gate (+10 % p99, 11.43 ms) sits above
  the class (11.1 ms), so a candidate can pass `check` and still miss the class. A
  release that claims the class compares against both.
- **The Vulkan convoy1 stall did not recur.** Section 10.4's presentation stall left no
  trace in these 63 Vulkan launches (convoy1 p99 9.46 ms at Quality against 28.61 at
  FPS-01). It stays open under FPS-07 until its cause is found.
- **Maximum is not budgeted.** It carries no frame-rate claim; section 10.1 records what
  it cost at 94e0b14ec, and the FPS-03 to FPS-05 work lowers that cost.

## Appendix: artifacts

Everything below lives in the repository's ignored `.tmp/` tree on the measurement machine;
nothing of it is committed. A run directory holds `plan.json`, one `summary.jsonl` record per
launch, each launch's engine log, per-frame CSVs, cvar dump and (where taken) stack samples
under `<label>/<scenario>/round<N>/`, and `report/summary.md` with `aggregate.json`,
`passes.json` and one frame-time plot per scenario, regenerated at any time with
`python tools/tests/performance_suite.py report --runs <dir> --out <dir>/report`.

**Runtimes** (`.tmp/stock-runtime/`, each a client, its renderer modules, game libraries
and paks; SHA-256 of `openQ4-client_x64.exe`):

| Runtime | Build | Client SHA-256 |
|---|---|---|
| `perf-head` | `main` d856fe6bb + the frame-logger extension, debugoptimized, b_ndebug (worktree `.tmp/wt-perf-head`, build `.tmp/perf-build-head`) for sections 3-9; restaged on 9 November from `.install` with the FPS-02 build for section 11 | `aa07229e9204932206709afaa7575c677baf70a5ca86457ccb818e64976c40b1` (sections 3-9), `6d1e35432d096b389502ffb1551b074d0b711b9d688a7bf36df91c04f9d69f1b` (section 11) |
| `perf-0132` | tag `v0.13.2` (fcdd56967) + the same extension, release paks (worktree `.tmp/wt-perf-0132`, build `.tmp/perf-build-0132`) | `0313af56a5ba763180304ceb7f72af8a6a330367fd5cce6e0b367b2d6bf59385` |
| `v0132-release` | the published 0.13.2 Windows x64 package, unmodified | `a15d3a1a956843914d4dff1cee081e777f14d571f172a5050749c85dff543ac1` |
| `perf-bisect-834564945` | `main` at 834564945 (2 October), pass 1 | `93b50504578f6b2172897b0164b3355799b819c725e2460406b0c5e9b26e14af` |
| `perf-bisect-156ced3fd` | `main` at 156ced3fd (15 October), pass 2, client-only staging | `32dc13aafe9d1776034e5afdf244cff31cd35040037dce49fafa6426805235fd` |
| `perf-ref` | `main` 94e0b14ec, debugoptimized, b_ndebug, staged from `.install` (the FPS-01 reference runtime, section 10) | `44064cda370395a79c14a2e899647027ad113283d7c03e155e24f476ed2efd4b` |

`perf-bisect-56a74bd48` (22 October) was built but not measured before the chain was paused.
The renderer and game modules of `perf-head` carry PDB paths into `.tmp/perf-build-head`,
which was relinked after staging; the suite therefore hands the sampler the runtime's own
directories as its symbol path (section 3.3).

**Run directories** (`created` is the local time the run's plan was written; a run's
launches follow it in the order of its `summary.jsonl`):

| Run directory | builds | API | scenarios | launches | created | note |
|---|---|---|---|---:|---|---|
| `.tmp/perf-suite/matrix-gl` | v0132/head/headeq | gl | the seven timed scenarios | 63 | 2026-11-07 20:33 | section 3.1 |
| `.tmp/perf-suite/toggles-gl` | head | gl | outdoor_airdefense1, effects_airdefense1, indoor_hub2 | 3 | 2026-11-07 22:02 | effects launch overflowed the console buffer, repeated below |
| `.tmp/perf-suite/toggles-eq-gl` | v0132/headeq | gl | indoor_storage2, outdoor_airdefense1 | 4 | 2026-11-07 22:41 | section 4.1 |
| `.tmp/perf-suite/msaa-gl` | head/headm4/headm0/headnopost | gl | outdoor_airdefense1, effects_airdefense1, indoor_hub2 | 12 | 2026-11-07 22:58 | section 4.2 |
| `.tmp/perf-suite/default-gl` | v0132def/headdef | gl | outdoor_airdefense1, combat_airdefense1 | 4 | 2026-11-07 23:16 | section 3.2 |
| `.tmp/perf-suite/drift-gl` | v0132/headeq | gl | indoor_storage2, outdoor_airdefense1 | 4 | 2026-11-07 23:22 | section 4.2 |
| `.tmp/perf-suite/attrib-gl` | v0132/head/headeq | gl | outdoor_airdefense1, combat_airdefense1, effects_airdefense1, indoor_hub2 | 12 | 2026-11-07 23:39 | section 4.3 |
| `.tmp/perf-suite/hitch-gl` | v0132/head | gl | hitch_firstuse, transition_ad1_ad2 | 8 | 2026-11-07 23:56 | section 5 |
| `.tmp/perf-suite/release-gl` | v0132rel/v0132 | gl | outdoor_airdefense1, combat_airdefense1, indoor_hub2 | 12 | 2026-11-08 00:03 | section 2 |
| `.tmp/perf-suite/bisect-gl/834564945` | v0132/b834564945 | gl | indoor_storage2, outdoor_airdefense1 | 8 | 2026-11-08 00:31 | section 6 |
| `.tmp/perf-suite/matrix-vk` | v0132/head/headeq | vk | the seven timed scenarios | 63 | 2026-11-08 00:50 | section 3.3 |
| `.tmp/perf-suite/toggles-vk` | head | vk | outdoor_airdefense1, effects_airdefense1, indoor_hub2 | 3 | 2026-11-08 02:12 | outdoor launch idle, excluded and repeated below |
| `.tmp/perf-suite/toggles-eq-vk` | v0132/headeq | vk | indoor_storage2, outdoor_airdefense1 | 4 | 2026-11-08 02:48 | section 3.3 |
| `.tmp/perf-suite/msaa-vk` | head/headm4/headm0/headnopost | vk | outdoor_airdefense1, effects_airdefense1, indoor_hub2 | 12 | 2026-11-08 03:06 | section 3.3 |
| `.tmp/perf-suite/default-vk` | v0132def/headdef | vk | outdoor_airdefense1, combat_airdefense1 | 4 | 2026-11-08 03:23 | 0.13.2 outdoor launch idle, excluded and repeated below |
| `.tmp/perf-suite/drift-vk` | v0132/headeq | vk | indoor_storage2, outdoor_airdefense1 | 4 | 2026-11-08 03:29 | 0.13.2 storage2 launch idle, excluded and repeated below |
| `.tmp/perf-suite/attrib-vk` | v0132/head/headeq | vk | outdoor_airdefense1, combat_airdefense1, effects_airdefense1, indoor_hub2 | 12 | 2026-11-08 03:47 | section 3.3 |
| `.tmp/perf-suite/hitch-vk` | v0132/head | vk | hitch_firstuse, transition_ad1_ad2 | 8 | 2026-11-08 04:04 | section 5 |
| `.tmp/perf-suite/release-vk` | v0132rel/v0132 | vk | outdoor_airdefense1, combat_airdefense1, indoor_hub2 | 12 | 2026-11-08 04:11 | section 3.3 |
| `.tmp/perf-suite/toggles-gl-effects` | head | gl | effects_airdefense1 | 1 | 2026-11-08 04:30 | section 4.1 |
| `.tmp/perf-suite/msaa8-gl` | head/headm8 | gl | outdoor_airdefense1, effects_airdefense1, indoor_hub2 | 6 | 2026-11-08 04:45 | section 4.2 |
| `.tmp/perf-suite/toggles-eq-repeat-gl` | v0132/headeq | gl | indoor_storage2 | 2 | 2026-11-08 04:54 | section 4.1 |
| `.tmp/perf-suite/toggles-eq-repeat-vk` | v0132/headeq | vk | indoor_storage2 | 2 | 2026-11-08 05:07 | section 4.1 |
| `.tmp/perf-suite/attrib-storage2-gl` | v0132/headeq | gl | indoor_storage2 | 4 | 2026-11-08 05:20 | section 6, stack samples |
| `.tmp/perf-suite/attrib-storage2-vk` | v0132/headeq | vk | indoor_storage2 | 4 | 2026-11-08 05:26 | section 6, stack samples |
| `.tmp/perf-suite/toggles-eq-vk2` | headeq | vk | indoor_storage2, outdoor_airdefense1 | 2 | 2026-11-08 05:32 | section 3.3 |
| `.tmp/perf-suite/toggles-vk-outdoor` | head | vk | outdoor_airdefense1 | 1 | 2026-11-08 05:48 | section 3.3 |
| `.tmp/perf-suite/ref-gl` | ultra/quality/enhanced (`perf-ref`) | gl | the seven timed scenarios | 63 | 2026-11-08 18:32 | section 10, FPS-01 reference run |
| `.tmp/perf-suite/ref-vk` | ultra/quality/enhanced (`perf-ref`) | vk | the seven timed scenarios | 63 | 2026-11-08 19:59 | section 10; Quality outdoor round 3 idle, repeated |
| `.tmp/perf-suite/default-vk-redo` | v0132def | vk | outdoor_airdefense1 | 1 | 2026-11-08 05:58 | section 3.3 |
| `.tmp/perf-suite/drift-vk-redo` | v0132 | vk | indoor_storage2 | 1 | 2026-11-08 06:00 | section 3.3 |
| `.tmp/perf-suite/viewdiag`, `viewdiag2` | v0132/headeq | gl | indoor_storage2, hidden window, `r_rendererMetrics 2` | 3 | 2026-11-08 06:10 | section 6, the view breakdown |
| `.tmp/perf-suite/bisect-gl/156ced3fd` | v0132/b156ced3fd | gl | indoor_storage2, outdoor_airdefense1 | 8 | 2026-11-08 06:16 | section 6 |
| `.tmp/perf-suite/screen-gl`, `screen-vk` | ten preset+override candidates (`perf-ref`) | gl, vk | combat, effects, hub2, convoy1, storage2 | 50 each | 2026-11-08 22:01, 23:13 | section 11.2 |
| `.tmp/perf-suite/screen2-gl`, `screen2-vk` | four candidates (`perf-ref`) | gl, vk | the same five | 20 each | 2026-11-09 00:30, 00:59 | section 11.2 |
| `.tmp/perf-suite/screen3-gl`, `screen3-vk` | three candidates (`perf-ref`) | gl, vk | the same five | 15 each | 2026-11-09 01:29, 01:51 | section 11.2 |
| `.tmp/perf-suite/fps02-gl` | ultra/quality/enhanced (`perf-head`, FPS-02 build) | gl | the seven timed scenarios | 63 | 2026-11-09 02:38 | section 11.6, FPS-02 reference run |
| `.tmp/perf-suite/fps02-vk` | ultra/quality/enhanced (`perf-head`, FPS-02 build) | vk | the seven timed scenarios | 63 | 2026-11-09 04:02 | section 11.6 |

**Chain scripts and kits** (`.tmp/perf/`): `phase2.sh` and `phase3.sh` with their step
scripts (`run_matrix.sh`, `run_toggles.sh`, `run_msaa.sh`, `run_default.sh`,
`run_drift.sh`, `run_extra.sh`, `run_bisect.sh`, `run_bisect2.sh`,
`build_bisect.ps1`, `build_bisect_client.ps1`), the chain logs under
`.tmp/perf-suite/*.log` (`phase2.log`, `phase3.log`, `bisect-gl.log` carry the step
markers and the pause), the bisection build logs under `.tmp/perf/bisect-logs/<sha>/`, and
the 0.13.2 game-library checkout the first bisection candidate needed
(`.tmp/perf/game-repo/`).
