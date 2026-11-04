# Retained UI measurements and vector caching

## Moving weapon scopes, 4 November 2026

The railgun split ring/diamond sweep and the nailgun yaw ring/lock arrow
recompiled analytic output-pixel coverage on every rotation or scale change.
The previous scope tests checked a single changed yaw, so they did not bound
the sustained cost. The machinegun compass had the same problem at a smaller
scale.

The scope generator now marks only the moving furniture as `antialias: false`:
compass dots/ring, rail split ring/sweep, nail lock arrow and spreading lines.
These use the existing retained local-mesh path, with transforms applied at
submission and bounded recompilation when a growing sweep needs a finer scale.
This deliberately trades exact pixel-edge coverage on those low-contrast
decorations for cached triangles. Stationary aiming marks, aperture edges,
glow gradients and lens-focus masks retain analytic antialiasing; geometry,
colors, timings, ammo and lock behavior are unchanged. No bitmap art is added.

The engine also retains completed opacity layers for a single untextured mesh
with at least 1,024 vertices. Previously this cache admitted compound plates
only, repeatedly submitting the nailgun's large, unchanged gradient while its
lock strength changed. The solid-coverage atlas cannot represent that gradient.
The existing revision/clip keys, shared memory budget and eviction rules still
apply, and small single meshes keep their direct/atlas path.

The native benchmark's `--scope` mode continuously changes yaw, target bearing
and acquisition strength as well as playing the authored idle timelines. At
1920x1080, 180 measured frames after warmup, Windows x64 optimized build:

| Scope | Before CPU p50 / p95 | After CPU p50 / p95 |
| --- | --- | --- |
| Machinegun | 0.766 / 1.420 ms | 0.009 / 0.020 ms |
| Railgun | 6.986 / 10.522 ms | 0.018 / 0.023 ms |
| Nailgun | 8.805 / 15.218 ms | 0.037 / 0.090 ms |

These are isolated runtime CPU measurements with a counted host, not GPU time
or whole-game FPS. Other jobs were active on the machine; this is regression
evidence, not UI-07's idle-machine or 1,000-frame qualification.

The production-document optics test now runs 240 moving frames at 1080p, 4K
and portrait, including acquisition/release, resize and changing opacity. Each
scope compiles at most 16 paths in an entire run (including scale-tier changes),
instead of per frame. The gate rejects the previous documents: the machinegun
alone compiles and uploads 3,840 paths in 240 frames. Existing geometry budgets,
centering, ammo and lock-state assertions also pass.

Reproduce with `openq4-retained-ui-benchmark --scope <scope.q4ui> 180 1920 1080`
and the Meson `openq4-ui-retained-screens-optics` test. The windowed SP harness
`tools/tests/ui_scope_performance.py` profiles each moving scope over
`game/airdefense1`, captures through the engine `screenshot` command, and
verifies the weapon-owned railgun scope selection. The motion profiles use
`testGUI` overlays with semantic state changes, not synthesized aim input.
Final 1080p OpenGL and Vulkan runs pass with unchanged runtime hashes, no
retained warnings/errors or geometry-ring overflow, and inspected engine
captures of moving scopes and lock readouts. The shared runtime's composition
suite and scope routing/generation checks pass too. Local measurements and
logs are under `.tmp/scope-performance/` (`final-gl/report.json`,
`final-vulkan/report.json`, `native-final.log`, `cpu-comparison.json`). Full
GPU/platform/resource-cycle qualification remains open.

### Smooth scope contours

The 4 November scope contour follow-up replaces raster-sized staircase segments
in `rail_ap`, `nail_hook` and `nail_ring2` with authored straight lines and cubic
curves. These geometric teeth existed before the moving-mesh optimization and
were visible even on the antialiased stationary aperture/hooks. The nail ring
now has a circular 252 u outer boundary and 225/242 u inner arcs, preserving its
four broad notched sectors and its measured (320,241) center. Hook tips and
symmetry, the rail's rounded shoulders and ring bite, colors and animation
bindings remain. The path reader preserves cubic/quadratic commands instead
of flattening authored curves to source-resolution polygons.

Contour tests bound the outer ring's radial error below 0.1 source unit and
check inner notches and mirrored hooks; the old staircase fails that check.
The native optics suite still bounds moving compilation at 1080p, 4K and
portrait. Isolated 1080p scope CPU medians remain 0.024 ms (rail) and 0.040 ms
(nail), with median zero path compilation/uploads. Both staged 1080p OpenGL
and Vulkan gameplay probes pass, with unchanged runtime hashes, no retained
warnings or geometry overflow, and engine screenshots confirming smooth
moving and locked contours. Whole-game frame timings remain subject to other
concurrent workloads and are not a controlled FPS comparison. Local evidence
is in `.tmp/scope-contours/` (`optics.log`, `railgun-cpu.json`, `nailgun-cpu.json`,
`final-gl/report.json` and `final-vulkan/report.json`).

## Original caching checkpoint

8 September 2026. This checkpoint adds runtime CPU measurements and removes
redundant path compilation during fades and whole-pixel movement. It advances
the [replacement plan](../archive/2026-10-18/plans/idtech5-ui.md); full performance qualification,
composition, production artwork, editor and GUI migration gates remain open.

The later [composition checkpoint](composition.md) supersedes the per-primitive
opacity path described below: canonical fades now use isolated layers and reuse
both geometry and vertex tint. Measurements here remain the historical baseline.

## Cache behavior

Vector elements retain compiled, premultiplied path geometry independently of
opacity. A fade updates the submitted vertex colors without repeating curve
subdivision, stroke union, coverage integration or gradient subdivision.
Opacity still uses the existing per-primitive contract; isolated group fades
require the planned composition renderer.

Whole output pixels of translation are applied when submitting cached geometry.
Only the fractional translation phase participates in path compilation. The
viewport/scissor region is transformed into that coordinate space and expanded
to a 256-pixel grid for the cache key. Small movements can reuse the region;
crossing a cache-region boundary recompiles it. The render manager always clips
to the exact current viewport/scissor, including after movement or resize.

Changes to fractional position, scale, rotation, shear, layout dimensions or
the cached coverage region rebuild paths. Source reload clears both geometry
and tint caches. Density/video changes use the existing regeneration path.
This is a bounded cache per element, not an accumulating history of positions.

The native retained test compares animated cached geometry against a newly
loaded document at the same final position and opacity. It also checks live
clipping during translation, zero path compilation for opacity-only changes,
no geometry upload during whole-pixel movement, fractional-phase invalidation
and zero retained geometry allocations after shutdown.

## Tessellation precision regression

The moving 200% fixture exposed a single-precision tessellation failure at a
thin chamfered rail. A boundary returned two nominally coincident points at
`791.79998779296875` and `791.7999267578125`, creating a tiny extra loop. Its
calculated coverage at pixel `(791,0)` was `1.000010482367812`. The invariant
check rejected the path; static captures had not exercised that phase.

The pinned libtess2 subproject now uses a small
[double-precision patch](../../../subprojects/packagefiles/libtess2/double-precision.patch).
The patch changes `TESSreal` and the corresponding subnormal assertions; the
archive pin, allocator bounds and upstream license/credits are retained.
The compiler checks the expected scalar size, preventing a stale unpatched
header from silently using an incompatible ABI. No system libtess2 is used.

The boundary integrator also evaluates clamped linear trapezoids directly,
avoiding subtraction of nearly equal antiderivatives. The coverage invariant
remains intact. The failing rail is now exercised over ten fractional phases
with independent clipped-triangle area checks, and the 60-frame 200% motion
benchmark completes without path errors.

Meson applies the patch through its documented
[wrap diff mechanism](https://mesonbuild.com/Wrap-dependency-system-manual.html#diff-files).
Existing checkouts with an already-extracted unpatched dependency must refresh
that generated subproject before rebuilding:

```powershell
& tools/build/meson_setup.ps1 subprojects update --reset libtess2
```

This refresh replaces the generated libtess2 source directory. Keep any local
dependency changes as reviewed patches before refreshing it. Clean checkouts
apply the patch automatically when the pinned archive is extracted.

## Measurement interfaces

`Runtime::Statistics()` returns the most recent frame's CPU update/render time,
path compilation and geometry preparation time, compilation/cache/upload counts,
host draw counts and submitted vertex/index totals. Resident compiled-geometry
counts/bytes follow resource lifetime, including hidden elements. Vector cache
bytes describe the elements visited during that frame. These are tracked CPU
buffers, not total process memory, RmlUi's entire allocation graph or GPU memory.

The developer command `ui_retainedProfile <1..3600 frames>` records a bounded
sequence of rendered frames. Its final log line is JSON prefixed by
`Retained UI profile: `. Engine CPU timing includes retained evaluation,
submission and the surrounding GUI flushes. GPU execution, presentation and
the rest of gameplay are outside this measurement. Profile data does not change
the presentation clock or inject any input.

Without a preview document, the same command profiles the session-owned root
views (the title, pause, SYSTEM and loading screens) by submitted frame. Its
line is prefixed by `Retained UI root profile: ` and reports the views drawn per
frame, their summed retained CPU time, the wall interval from each frame's first
root draw to the next frame's, and their layer composites, backdrop passes and
backdrop fallbacks. A hidden window on Windows is capped at 60 fps, so its
frame interval measures CPU-bound frames, not GPU cost.

The capture harness accepts `--profile-frames N`, waits for the requested frames
before taking its engine screenshot and requires completed profile records.
With video restart, it checks both the original and recreated preview. The
report retains profile records alongside launch arguments and binary/log hashes.

## Native benchmark

`openq4-retained-ui-benchmark` loads the real vector fixture and uses the real
canonical evaluator, RmlUi layout and retained renderer. It exercises stationary,
opacity-only, whole-pixel and fractional translation scenarios, reporting JSON
lines with CPU percentiles, compilation/upload counts and tracked buffer peaks.
It uses synthetic font metrics and an empty draw host; it excludes the game's
font backend, engine vertex conversion, actual graphics APIs and GPU execution.
It is a diagnostic executable, not a timing threshold in the normal test suite.

```powershell
& tools/build/meson_setup.ps1 compile -C builddir openq4-retained-ui-benchmark
& builddir/openq4-retained-ui-benchmark.exe tools/ui/fixtures/vector-smoke.q4ui 1.25 60
& builddir/openq4-retained-ui-benchmark.exe tools/ui/fixtures/vector-smoke.q4ui 2 60
```

The baseline was `d096cc9f` with measurement instrumentation, before cache
changes, using the same fixture and scenarios. Its preserved benchmark binary
SHA-256 is `957e5c207fa893ec9f8bc83afc312d4f58bf5a93cdf24e67236b4385ef41b27d`.
Both comparisons use the existing Windows **debug, optimization 0** build.
They demonstrate work avoided in this fixture; they are not shipping-build
frame-rate results or cross-platform performance claims.

The 60-frame baseline compiled 780 paths during both opacity and whole-pixel
movement. The new cache compiles zero in either scenario. Opacity still rebuilds
780 tint submissions; whole-pixel movement also eliminates those uploads.
Fractional motion continues to compile the changing coverage. Additional cached
geometry increases tracked resident CPU memory and is included in the reports.

The final native benchmark binary SHA-256 is
`d5edcaa4dc38662c4d1307578b1013e027cdea4b11b034357e17b0828b94e200`.
Reports are under `.tmp/ui/ui-profile-baseline-{125,200}.jsonl` and
`.tmp/ui/ui-profile-final-{125,200}.jsonl`. Median CPU times in milliseconds:

| Scenario | 125% before | 125% after | 200% before | 200% after |
| --- | ---: | ---: | ---: | ---: |
| Stationary | 0.85 | 0.57 | 1.39 | 0.81 |
| Opacity | 15.96 | 1.13 | 20.34 | 1.52 |
| Whole-pixel movement | 14.96 | 0.62 | 16.14 | 0.88 |
| Fractional movement | 17.38 | 13.12 | 39.64 | 22.32 |

These are individual local runs and include measurement variability. The final
implementation also has more precise tessellation, which removes numerical
fragmentation as well as fixing the rejected rail. The stationary fixture's
tracked buffer peak rises from 407,696 to 807,224 bytes at 125%, and from
480,600 to 1,470,080 bytes at 200%. Cold first-frame evaluation remains about
24 ms and 36 ms respectively in this unoptimized benchmark. Fractional movement
and cold compilation therefore remain explicit optimization/qualification work.

All three native test targets pass after the precision patch. Client, dedicated
and both renderer modules were built/staged before the final gameplay profiles.

`--screen` runs a production root document as the session presents it:
`onInit`, the `open` timeline and a four-second settle, then timed 60 Hz frames
on its view-height canvas, with each declared CVar source at its type's default.
It reports frame, update, render and tessellation times, paths compiled, draw
calls, submitted vertices, and layers pushed and elided:

```powershell
& builddir/openq4-retained-ui-benchmark.exe --screen content/baseoq4/pak0/guis/menu/title.q4ui 240 1280 720
```

An optional sixth argument names a document event that runs after the settle,
so the timed frames show what it opens; `first_ms` is the frame that opens
it. Create Server's ADVANCED OPTIONS, with PLAYER BAN LIST nested in it,
opens in a 2.8-3.4 ms first frame against 2.5-2.7 ms without the list
(debugoptimized, two runs each, 30 frames at 1280x720):

```powershell
& builddir/openq4-retained-ui-benchmark.exe --screen content/baseoq4/pak0/guis/menu/createserver.q4ui 30 1280 720 advancedModalShow
```

## Gameplay evidence

The final hidden, windowed runs used the registered engine screenshot command
after active SP/MP gameplay, with mouse/controller input disabled and no host
input injection. The unchanged vector fixture's frames, thin rails, gradients,
ring/hole, check stroke and localized labels were reviewed from render-target
TGA files. Each profile contains 180 actual rendered frames, including entry
motion. SP repeats the profile after full video restart and timeline replay.

| Profile under `.tmp/ui/performance/` | CPU p50 / p95 / max (ms) | Path compilations | Outcome |
| --- | --- | ---: | --- |
| `sp-gl-125-restart`, original preview | 1.47 / 2.10 / 28.31 | 57 | SP `airdefense1`, OpenGL, 125%; exit 0, no warnings/errors/retained diagnostics |
| Same run, recreated preview | 1.48 / 12.37 / 13.80 | 130 | Second 180-frame profile and timeline replay confirmed |
| `mp-vulkan-200` | 2.23 / 7.72 / 45.48 | 109 | MP `q4dm1`, Vulkan, 200%, auto-join; exit 0, no errors/retained diagnostics |

The high maxima and motion percentiles remain visible here because cold and
fractional-motion work are not yet fully qualified. These unoptimized CPU
profiles include GUI flushes but do not measure GPU completion or frame pacing.

Both runs used client SHA-256
`c3ce0146e2c7b1466210728450ca454d5ed1dfa039e93cb7493ea53b72ff0df3`,
fixture SHA-256
`78dc7bc86a65d6983ac23646f5534f205ec670bb85ade94cffecb4c8da921bbe`.
The final rebuilt OpenGL module SHA-256 is
`95ff9ca571526cc7668756552d91334093ecb472a1a227bba155962a5d354118`;
the Vulkan module SHA-256 is
`0c858cd74c859d9bacab961bd52cbcfbea1169bf7f5a1398a9d8936fc9707f41`.
SP screenshot SHA-256:
`68fd1b33c134e3d7d049d6bc696f7f24752a567e85fd92c2a4d5b612b12fb48a`.
MP screenshot SHA-256:
`120bd3c469282c0b23ce221f7f34f88556620633665bfa9a1de51f84180f8921`.

The MP log retains 93 existing content warnings; comparison with the preceding
MP capture found no new warning messages. The capture reports retain full
launch arguments, hashes, profile records and `replacement_acceptance: false`.
Windowed gameplay and these fixture screenshots do not qualify other platforms
or the complete 271-resource GUI corpus.

```powershell
$assets = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4'
python tools/ui/capture_legacy_baseline.py --assets $assets --mode sp --renderer gl --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 1.25 --timeline enter --video-restart --profile-frames 180 --output .tmp/ui/performance/sp-gl-review
python tools/ui/capture_legacy_baseline.py --assets $assets --mode mp --renderer vulkan --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 2 --timeline enter --profile-frames 180 --output .tmp/ui/performance/mp-vulkan-review
```

## Retained title screen CPU

1 October 2026. With `ui_retained 1` the title screen took 9.5 ms of retained
CPU a frame on a debugoptimized build, and 15-19 ms when the GPU fell behind
the hidden window's 60 fps cap: graphics calls inside the timed region then
block. The title is mostly static, so nearly all of that was work repeated
every frame. A temporary per-phase probe on the 1280x720 home screen split it:

| Work per frame (debugoptimized, 60 fps cap) | Before | After |
| --- | ---: | ---: |
| Path tessellation | 3.1 ms, 2 paths | 0.4 ms, 1 path |
| Host vertex conversion | 2.4 ms, 192,000 indices | none for unchanged meshes |
| Composition layers | 33 | 15 |
| Runtime update | 1.1 ms | 0.7 ms |

The changes keep the presentation:

- **Zero-opacity layers.** Any opacity below 1 is an isolated `filter`
  layer, and the hidden focus rails, hot markers and message lines sit at 0.
  A pushed layer now stays pending, recording its draws, until something
  needs its target: a nested layer, a mask snapshot, a backdrop or a visible
  composite. Composited at zero opacity, or into an empty clip, it is dropped
  without a target, a clear or a composite, which is exact: a premultiplied
  composite at zero opacity leaves its destination unchanged. The new
  `layerElisions` statistic counts them, and `layerPushes` now counts the
  layers that received a target.
- **Sub-pixel jitter.** The glint's rim turns back under its turning parent,
  so it stays put, but single-precision composition moves its sub-pixel phase
  by about 1e-4 px between frames, and the exact-match cache key compiled it
  almost every frame. A phase change below 1/1024 px now keeps the compiled
  mesh. Coverage moves no further than the phase, a quarter of one 8-bit step;
  every other key term must still match exactly.
- **Tessellation.** The coverage emitter extends rows through sorted vectors
  instead of a map per row, gradient stop planes clip only the cells they
  cross, and scratch buffers are reused. The turning wedge mask, which must be
  tessellated every frame, falls from 0.9 to 0.3 ms natively. A local harness compiled
  the committed tessellator beside this one and compared 264,984 cases, every
  path and mask of the six production screens and three fixtures across
  sizes, densities, sub-pixel phases, rotations, mirroring, bounds and both
  antialiasing modes: every mesh is byte-identical.
- **Retained meshes.** `RenderGeometry` keeps each geometry's transformed and
  clipped submission and reuses it while its translation, transform, clip,
  texture and multiply resolution are unchanged. Hosts receive a stable mesh
  identity and a revision through the optional `Host::DrawMesh` and
  `ReleaseMesh`, whose defaults draw immediately. The engine host converts
  a mesh once per revision, material and view size. Each chunk passes the
  contiguous vertex range its whole triangles use, so vertices are no longer
  duplicated per index, and the material is classified once per mesh rather
  than once per vertex.
- **Update.** `ApplyMotion` holds every authored property, nearly all static,
  and formatted all of them each frame. A value already applied under the same
  state revision and text ratio is now skipped; text and backdrop pairs are
  still reapplied. Input eligibility looked up each canonical node with its own
  search of the element tree, and now resolves all of them with one
  breadth-first walk in `GetElementById`'s order.

Each pair alternated a baseline client (origin/main `c5119c41`) with this
change on one machine (Windows 11, RTX 4060 Laptop GPU): a hidden 1280x720
window, the title at home for 4 s, then `ui_retainedProfile 120`. Medians of
`retained_cpu_p50_ms`, with the median frame interval:

| Build and renderer | Before | After |
| --- | ---: | ---: |
| debugoptimized, OpenGL, 60 fps cap | 9.59 ms | 3.59 ms |
| debugoptimized, OpenGL, uncapped | 9.43 ms | 3.51 ms |
| debugoptimized, Vulkan, 60 fps cap | 9.72 ms (19.8 ms frames) | 3.77 ms (16.7 ms frames) |
| release (`b_ndebug=true`), OpenGL, 60 fps cap | 10.21 ms | 3.89 ms |
| release, OpenGL, uncapped | 9.53 ms | 4.77 ms |
| release, Vulkan, 60 fps cap | 9.69 ms (20.1 ms frames) | 3.55 ms (16.7 ms frames) |

Baseline runs also fell into a slower, GPU-bound mode at times: Vulkan at
25-27 ms with 38 ms frames, OpenGL at 15.6 ms with 19.7 ms frames. The native
`--screen` benchmark of the title, with an empty draw host, falls from 5.3 to
1.0-1.3 ms on debugoptimized and 0.96 ms on release.

Engine captures of the title scenario in
`tools/ui/capture_retained_screens.py`, with reduced motion so that every frame
settles, are byte-identical before and after on OpenGL and Vulkan for the home,
depth, focus, exit-confirmation (over soft focus), and depart screenshots. The
hand-off to the stock settings page differs between two baseline runs as well,
because of its own animation. A motion-on capture shows the glint, rim and
montage unchanged.

What remains per frame: the engine's GUI path still copies every retained
vertex, into the GUI model and then into frame memory and the vertex cache at
each flush. That is about 2 ms for the title's 127,000 vertices, and static GPU
buffers for retained meshes would remove it. The wedge mask turns and is
tessellated every frame. Fifteen layers remain, for the 0.4-opacity rails and
markers and for the glint and its mask. A cold first frame takes 45-50 ms,
most of it stroke unions, the reticle grid alone 28 ms.

## Gameplay HUD state updates

The game writes HUD state every frame, and the multiplayer document carries
about 1,700 bindings with the Competitive preset. Each changed batch used to
copy the whole state, evaluate every binding and then reapply every bound
property. With dependency-tracked evaluation (see
[bindings](bindings.md#updates-ownership-and-lifetime)) a batch evaluates and
applies only what it reaches. The native benchmark
(`openq4-ui-retained-screens-test --hud-bench`, debugoptimized, one variable
changed per batch) on `remastered_mp.q4ui`:

| Document | State update | Frame after a change |
| --- | ---: | ---: |
| Remastered bindings, every binding evaluated | 0.71 ms | 3.1 ms |
| With Competitive, every binding evaluated | 1.53 ms | 5.3 ms |
| With Competitive, dependency-tracked | 0.01 ms | 0.38 ms |

## Gameplay instrument performance

The October 2026 gameplay investigation found that the Remastered ECG's
repeated core/glow vector strokes remained expensive after tessellation:
each moving frame still transformed and submitted their geometry. With the
user-approved bitmap exception, the ECG uses two adjacent copies of each
installed stock texture through the existing vector mask. Full texture periods,
Marine/Strogg rates, low-health tint and essential motion are preserved. No
extracted proprietary texture is added to the repository. The native Marine
HUD diagnostic drops from 64,272 vertices / 51 draws to 22,180 / 25, with warm
frame CPU time falling from 2.896 to 0.585 ms on this machine.

The radio graph's bar transforms used the full HUD canvas as their reference
box, moving the visible ink outside its mask. Correcting that revealed a
second cost: forty changing audio columns added about 1.4–1.6 ms per frame in
the native fixture; two multiplayer talker graphs added about 3.6 ms. The
approved fallback scrolls a fixed repeated waveform, keeps existing masks,
colours, speaker labels and show/hide events, and freezes under Reduced Motion.
These graphs indicate activity rather than measured loudness. Documents no
longer declare audio peak fields, so the HUD peer skips their unused history
reads and state writes. Warm preset graphs compile no vector paths and add
about 0.01 ms (Marine), 0.02 ms (Strogg), or 0.05 ms (both MP talkers) in the
same native diagnostic. These timings exclude GPU execution and use synthetic
fonts; the optional `--hud-audio-bench` mode records all phases.

The original four gameplay scenes were repeated on an i7-13700H / RTX 4060
Laptop GPU, NVIDIA 591.74, Windows, using the optimized staged client. Each
1280x720 window used MSAA 8, SMP off, VSync off and uncapped rendering, with
25 seconds warmup followed by alternating UI-off/on windows of 10 seconds.
Multiplayer used a solo listen server without bots and explicitly enabled
auto-join. Recorded camera poses match the original runs, allowing 0.125
world units for ground settling; Storage 2 excludes its moving-lift samples.

| Scene | UI on before (FPS) | UI on after (FPS) | Gain | UI off after (FPS) | UI-on P95 before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Storage 2 / Vulkan | 86.5 | 216.2 | +150.1% | 170.8 | 19 / 6 ms |
| Air Defense 1 / Vulkan | 121.0 | 204.5 | +69.0% | 354.0 | 15 / 7 ms |
| Q4DM1 / Vulkan | 112.9 | 158.5 | +40.4% | 366.8 | 13 / 9 ms |
| Q4DM1 / OpenGL | 104.6 | 139.7 | +33.6% | 232.7 | 16 / 10 ms |

FPS values are medians of smoothed end-of-window snapshots, not averages
over each entire window. P95 uses the full sample histogram in millisecond
bins. CPU load varied substantially: the stock UI controls varied as well,
and a separate unchanged-baseline Air Defense repeat fell from 121 to 84.4
FPS. Treat gains as local observations. In particular, Storage 2's faster
UI-on result does not establish that retained UI is cheaper than stock UI.
Multiplayer retains measurable UI overhead after this instrument fix. The
comparison isolates the task's changes against the original working-tree
build; it is not a checkout comparison with the previous release.

Separate 300-frame retained CPU profiles fall from 2.7091 to 0.949 ms at p50
in Air Defense, from 4.623 to 1.602 ms in Vulkan Q4DM1, and from 3.25 to
1.4934 ms in OpenGL Q4DM1. Storage 2 measures 1.199 ms after the fix; its
original CPU profile is excluded because its setup changed after FPS sampling.

Qualification includes all three native retained UI/screen suites, generated
document consistency, SP decoder/playback-history checks, and engine render
target captures on OpenGL and Vulkan. Six moving graph pairs change pixels;
all six Reduced Motion pairs remain identical. In sixteen paired HUD captures,
all changed pixels are confined to the approved ECG area. Live microphone and
network voice were not exercised. Logs, runtime identities, per-window results,
captures and the method are retained in
`.tmp/ui-perf-fix-20261011/REPORT.md`; original UI on/off evidence is in
`.tmp/ui-ab-20261011/`.

## Initialization animation (11 October 2026)

The initialization view now advances between stage changes at cooperative
startup checkpoints: existing print/read progress offers, completed GUI loads
and menu-image preloads. A scoped main-thread owner ends before normal session
presentation, including on exception. The pump does no work on worker threads,
inside its own draw or during retained document loading/resource callbacks.
It never starts a rendering thread, sleeps, waits for animation to finish or
invents progress. The retained clock already measures monotonic elapsed time;
the defect was the lack of frames during synchronous initialization.

Redraws target at most 30 Hz, respect lower presentation caps, and reserve nine
times the previous completed frame cost for loading before another optional
frame. Slow drawing or a blocking present therefore reduces animation frequency.
The first cold compilation is paid once and does not poison the warm budget.
Stage changes still present immediately. Reduced Motion skips optional redraws
and preserves the existing still poses. Normal gameplay does not run this pump.

The dim rotating plate band, teeth and soft light sweep use the existing
`antialias: false` authoring option. Fill-only paths without analytic coverage
now retain local triangles across motion; the render manager applies their
current transform and clip. Curve precision uses a conservative output-scale
bucket, invalidated on density/size changes. Lettering, bright details, masks
and strokes retain their original output-space coverage. No raster assets are
introduced. Unchanged startup messages also avoid state publication and binding
updates on every animated frame.

Matched 120-frame native benchmarks in the existing Windows `debugoptimized`
preset, after the same 240-frame warmup:

| View | Previous CPU p50 / p95 | Updated CPU p50 / p95 | Updated paths compiled / uploads per median frame |
| --- | ---: | ---: | ---: |
| 1280 × 720 | 13.530 / 21.496 ms | 0.532 / 0.892 ms | 0 / 0 |
| 3840 × 2160 | 28.229 / 35.964 ms | 0.482 / 0.942 ms | 0 / 0 |

These measure retained CPU work with a native test host, not GPU time or total
game frame cost. An actual OpenGL screen profile measured 3.128 ms median
retained submission and a 16.661 ms median frame interval. Real startup runs
presented 14–19 frames, including 11–16 optional updates, with about 60–75 ms
total drawing time. Reduced Motion presented only the three remaining startup
stages (12.7 ms drawing in the measured Vulkan run). Work that has no safe
checkpoint still produces gaps: observed maxima were 329–412 ms. This is a
bounded improvement, not a guarantee of uninterrupted 30 fps or zero startup
cost on every device. Earlier startup, before the graphics device exists,
cannot show the initialization view.

Validation covers the native pacing and transform-cache checks, retained
screens through 4K, vector/mask tests, client and dedicated loading-pacifier
reentry tests, and generated-document consistency. Staged engine tests enter
Air Defense 1 in SP/OpenGL and Q4DM1 in MP/Vulkan with host input disabled.
Engine `screenshot` pairs change under animation on both renderers; Vulkan
Reduced Motion pairs are pixel-identical. There were no retained UI warnings
or runtime errors. The MP smoke still reports missing stock AAS variants and
late material/skin precaches; these are unrelated to initialization animation.
The native benchmark's existing JSON output also requires forward-slash input
paths on Windows because backslashes are emitted without JSON escaping.
Other platforms and visible-window GPU timings remain unqualified.

Logs, command lines, captures, runtime identities and measurements are retained
under `.tmp/initialization-animation/`. `ui_retainedTrace 1` now reports optional
`INITIALIZATION_UPDATE` frames and an `INITIALIZATION_PRESENTATION` summary of
frame count, total draw cost and largest gap without collecting host input.

## Legacy instruments and HUD submission (11 October 2026)

The radio and voice trace now samples the installed legacy waveform bitmap,
with the stock material's 21-interval vertical scale table and 0.6-width/second
horizontal scroll. It uses one stationary image quad with repeating UVs. The
ECG uses the same UV path for its installed Marine/Strogg textures. These are
runtime asset references; proprietary bitmap data is not included in Git.
Reduced Motion freezes the decorative radio/voice trace; the essential ECG
keeps its existing motion policy.

Icons already originate in editable SVG paths and become cached native meshes.
Loading those same shapes through another SVG renderer would not eliminate
tessellation, vertex submission or mask composition. Keep SVG as the authoring
format and reuse compiled geometry. Raster atlases remain a possible measured
tradeoff for especially expensive artwork, with density, memory and invalidation
costs; a blanket icon format replacement is not justified by current evidence.

A native isolation probe with full health/armor hides the two visible gauge
symbols while leaving the surrounding HUD intact. That removes three draws
and 1,672 Marine / 1,596 Strogg vertices; both versions compile zero paths per
warm frame. Three alternating repeats did not show a stable total CPU gain
beyond timing variation. This bounds these particular symbols' cost, not every
icon, GPU work, weapon selection, flashing or all density settings.

The runtime now reuses immutable mesh submissions and cached bounds. Stationary
masked vector panels can reuse their rendered pixels, with revisions, clips and
viewport changes invalidating the cache. Textured content is excluded from that
cache so time-varying materials cannot freeze. An additive image fully covering
one non-overlapping coverage mask can instead texture the mask's triangles
directly; other masks and alpha images retain the general compositor. Cached
target allowances account for every view sharing the host. Allocation pressure
can evict another view's idle cached pixels; references in the current frame
still prevent resizing their target. Native checks cover shared target pressure
and three 4K contexts sharing two cached targets while reserving scratch space.
Pending layers composite only their artwork's pixel bounds. The engine avoids redundant
target binds, preserving GUI batching and explicit backdrop flushes.

The adapter publishes changed application values while preserving atomic event
batches, case-insensitive state names, pending values and save/restore behavior.
Presentation-only documents skip unchanged layout work; controls, scrollbars,
modals and pointer interaction continue to update normally. CVar sampling visits
only CVar declarations, and animated UV changes avoid layout invalidation.
Empty focus no longer looks up the unnamed document and scans its canonical
tree. Hidden branches retain their current inline animation values without
invalidating visible layout; showing a branch resolves them before drawing.
The bundled RmlUi presentation update similarly defers `display:none`
descendants. This is an explicit opt-in limited to canonical presentation
documents; ordinary RmlUi, CSS animations and interactive widgets keep full
updates. The complete patch stack is checked against the original archive.

Qualification includes the complete native retained/screen/HUD suites and the
compiled production adapter tests, including failed publication retries and
pending state across events and saves. Native checks cover exact waveform
samples, wrapping, Reduced Motion restarts, masked holes/edges, opacity, resize,
partial-image fallback and cache invalidation. Gameplay timing and engine
render-target captures on OpenGL and Vulkan also qualify the installed bitmap:
six graph animation pairs change and six Reduced Motion graph regions remain
pixel-identical. The Air Defense 1 gauge capture preserves the centered layout
and the previous one-step 8-bit background-color tolerance against legacy.
Evidence is retained in `.tmp/ui-legacy-fidelity-perf/REPORT.md`; gameplay timing
is recorded there separately from these rendering checks.

The final Windows `debugoptimized` native benchmark reduced Marine frame work
from 0.644 to 0.147 ms, Strogg from 0.815 to 0.258 ms, and the MP overlay from
0.285 to 0.064 ms. These measurements exclude the engine and GPU; they describe
the combined working-tree candidate, including the concurrent transform-cache
improvement. A new SVG loader would not address the remaining engine cost.

The isolated, uncapped 1280×720 gameplay comparison used 8× MSAA, SMP disabled,
three ten-second windows per condition and one warmed process per scene:

| Scene / renderer | Legacy frame ms | New UI frame ms | Difference | New UI root CPU p50 ms |
| --- | ---: | ---: | ---: | ---: |
| Air Defense 1 SP / Vulkan | 2.98 | 3.58 | +0.60 | 0.380 |
| Q4DM1 MP / OpenGL | 3.80 | 5.05 | +1.25 | 0.474 |

Frame columns are medians of the three window means, not per-frame medians.
These results **do not meet negligible-overhead acceptance**. Root CPU excludes
later renderer/backend execution; the gap cannot be assigned wholly to icons
or layout. The final trace recorded zero SP layout updates per warmed frame
and 0.63 MP updates. Further profiling must separate remaining state/motion,
submission, backend and GPU work before choosing another rendering method.
The final OpenGL captures are 1280×720 and Vulkan captures are 2538×1312:
the requested 3840×2160 window was clamped, so this is not a 4K GPU qualification.
SP gameplay logged no warnings; MP retained the stock missing AAS variants,
`mp_buying_givecash` and late-precache warnings. Test-GUI documents also log
late precaches when loaded outside the level-loading path.

## Coverage atlases and presentation replay (11 October 2026)

The follow-up keeps the centered legacy gauge dimensions, saturation and installed
bitmap instruments. SVG-derived artwork still supplies the editable source. At
runtime, unchanged solid, whole-pixel coverage cells can share a nearest-filtered
RGBA8 atlas. This removes repeated vertex conversion/submission without changing
the cells' premultiplied colours or resampling their edges. Gradients, overlapping
cells, fractional coordinates, unsupported topology and oversized shapes keep
the original meshes. Overlap retains the GPU's original destination blending.
The atlas has at most 32 pages of 512×512 pixels (32 MiB each of CPU and GPU
storage). Live allocations never move; completely unused pages wait four host
frames before reuse. Rasterization also bounds visited-cell work. Named image
uploads preserve their sampler; the cinematic upload helper's linear/repeat
defaults must not replace the atlas's nearest/clamp sampling.

Canonical presentation documents record a bounded draw list after their layout
settles. Static meshes and valid cached layer composites replay directly. Bitmap
offset and scale values remain live: a scrolling ECG or radio waveform updates
the recorded sampling coordinates every frame. Geometry/style changes, document
replacement, resize, resource retirement and target eviction invalidate the
recording. Clipped/perspective image sampling, transient composition, backdrops,
live binding text and interactive documents keep the full rendering path. The
diagnostic CVars `ui_retainedAtlas` and `ui_retainedReplay` independently disable
these optimizations; neither is archived.

Hidden branches defer CSS writes while their canonical animation clocks continue.
Revealing a branch flushes the deferred values before layout. Fully transparent
canonical stacking contexts skip painting, and unchanged vector paint reuses its
last geometry and transform. Plain legacy text reuses glyph layout and batches
adjacent glyphs from the same material; live escapes still consult the host.

The GUI-root boundary now preserves batching when the target and actual viewport
are unchanged. The renderer API advances to 24 for the appended `BeginGuiRoot`
slot. Explicit `BindRenderTexture` calls retain their original flush/barrier
semantics, and cropped/editor/different-aspect paths retain required viewport
flushes. Stage matching renderer modules with the engine.

The implementation review also repairs same-frame legacy fallback when retained
rendering fails and reclaims obsolete, unleased target allocations under memory
pressure. Unuploaded atlas entries draw their original geometry until upload
succeeds. Isolated images and non-overlapping glyph runs apply opacity directly;
overlapping glyphs retain the compositor. CVar sampling allocates state changes
only for changed values, and the legacy HUD bridge caches normalization while
still publishing every application write. Its declared-key lookup now follows
the legacy dictionary's case-insensitive names. Native tests cover failed allocations, shared-view leases, resize,
hidden animation/reveal, replacement readbacks, static replay and live sampling.
The visible-window follow-up retains crosshair coverage quads through clipping
and replays unchanged captured HUD packets. Changed packets, failed preparation,
resizing and resource replacement invalidate that replay. Isolated opacity in
noninteractive presentations resolves only the affected element's filter.
Leaf text and images retain live opacity values in their recorded commands;
fades within (0,1) need neither style updates nor a document repaint. Disjoint
quads multiply their original premultiplied colours, avoiding cumulative rounding.
Overlapping text retains its completed composition target in the existing bounded
layer cache, after the same geometry has painted in an earlier host frame.
That admission delay allows a font's first GPU upload to finish; freezing its
first draw produced missing text on Vulkan. Images do not enter that text cache, so material animation stays
live. Zero/full-opacity transitions repaint, and changes to text, clipping,
geometry, resources or target leases invalidate reuse. Other isolated opacity
changes repaint without relaying out the document. Interactive/layout changes retain
the full update. Engine captures compare cached text against ordinary group
composition at six opacity levels with zero changed pixels on both OpenGL and
Vulkan. HUD geometry readbacks run after layout changes; hidden
animations retain resolved element handles. The legacy peer advances its
scripts, expressions and transitions through `RunTimeEvents`, with normal
layout restored immediately whenever Classic renders. Per-document CPU work is
now included in the root profiler.
Unchanged motion defaults avoid CSS formatting and use a hashed property index.
Forwarded HUD state also uses a transparent ASCII-case-insensitive hash lookup,
retaining the legacy dictionary's naming and event-write semantics.
Detailed runtime and per-draw stopwatches run only while `ui_retainedProfile`
requests timing. Ordinary gameplay retains work counters without querying the
clock for every draw. Native/tool hosts keep timing enabled by default, and the
runtime test verifies that disabling timing preserves the submitted geometry.
Prompt labels and glyphs refresh on binding/program writes, controller appearance
changes and resource/language reloads; older non-SDL input backends retain polling
because they do not publish keyboard-layout changes. The adapter tests cover
paired glyph updates, case-insensitive writes, deletion and controller changes.
Timing CSVs now append `frameBuildUs` and `frameSubmitUs`, splitting `cpuUs`
immediately before command submission. In serial mode submission includes backend
execution/presentation; in SMP mode it measures submission and waiting, not a
separate backend-thread interval.
Evidence and the final timing assessment belong in
`.tmp/ui-negligible-overhead/REPORT.md`. The earlier acceptance failure above is
historical evidence, not a claim that this follow-up has met the performance goal.

Final Windows captures show zero changed gauge pixels outside the live ECG
between the original meshes and optimized rendering, including MP over gray.
Decorative Strogg grain is held still for this comparison; unfrozen grain
changes between captures even with optimizations disabled. Radio/voice motion
and Reduced Motion pass on OpenGL/Vulkan for Marine, Strogg and MP.
The first atlas build removed 5,100 submitted vertices per MP frame and 3,240
per SP frame in the measured scenes. Earlier diagnostic medians were distorted
by shared 500 ms presentation stalls. A coordinated visible-window retry
removed those stalls and measured snapshot-average increases of 0.343 ms
SP/Vulkan and 0.570 ms MP/OpenGL. The logged `present` field was subsequently
confirmed to be a smoothed recent average, not a whole-window mean; earlier
whole-window labels were incorrect. These remain diagnostics, not acceptance.
`framePacingSnapshot` now also prints `Frame timing window` with sample count,
summed interval milliseconds and their arithmetic mean since `framePacingReset`.
The gameplay harness exports these separately as `pacingWindowSamples`,
`pacingWindowSpanMs` and `pacingWindowMeanMs`, without changing the live pacing
display or its existing fields. The old presentation stalls are no longer an
acceptance blocker. Final measured acceptance belongs in the evidence report.
The subsequent true-window checks still do not establish negligible overhead.
After the user paused background jobs, default-settings whole-window comparisons
add 0.049 ms in SP/Vulkan and 0.358 ms in MP/OpenGL. A further leaf-opacity
optimization avoids formatting CSS when the recorded filter already reads the
live value; the last installed CSS remains the comparison baseline for later
topology changes. Eight native suites pass. A sequential engine test revisits
an earlier alpha and crosses zero/full opacity without reloading the view:
all seven reference/optimized pairs match exactly on each graphics API (28
captures). This qualifies correctness, not a measured frame-time improvement.

The updated build's MP aggregate of +0.011 ms hides opposing ABBA block
differences (+0.364/-0.605 ms), so it is not accepted as negligible overhead.
Twelve shorter pairs add 0.683 ms, with all six blocks slower. Since switching
off destroys the retained HUD, a further check allows six seconds of settling
after each switch: MP still adds 0.218 ms (+4.93%), with block differences of
+0.035/+0.420 ms. No samples or stalls are discarded. The target remains open;
the user requested a pause after this validation pass. Test reservations are
released at the checkpoint. CPU frontend work remains the main investigation
direction; changing already-cached SVG icon sources does not remove that work.

## Hidden animation input reuse (13 October 2026)

Hidden presentation branches now retain their deferred-input status. Their
timelines keep advancing, while repeated state/value lookups and ancestor
visibility walks wait until a display or presentation change. Revealing a
branch applies its current values before layout; visible animation, input and
the installed legacy ECG/radio/voice bitmaps keep their existing behavior.
Native regression checks cover zero repeated hidden-property scans, current
and completed-value reveal, and document replacement. Eight optimized UI
suites pass.

The coordinated 1280x720, 8x MSAA, uncapped gameplay repeat uses four five-second
windows per condition, six seconds of settling and ABBA ordering. Whole-window
means include every interval. Q4DM1/OpenGL measures 3.371 ms legacy versus
3.459 ms retained (+2.60%); Air Defense 1/Vulkan measures 2.565 versus 2.640 ms
(+2.93%). MP root CPU p50 falls from 0.091 to 0.070 ms and actual animation
input scans from about 12 to 2 per frame. The baseline had substantial host
variation, so this does not isolate the whole-frame benefit of the small
runtime change. Reversing the condition order confirms 3.438 versus 3.587 ms
on MP/OpenGL (+4.32%) and 2.524 versus 2.569 ms on SP/Vulkan (+1.79%). Both
balanced orders therefore place the new UI within 1.8-4.3% of legacy in these
two scenes. This qualifies near-legacy performance on the tested Windows
machine, not zero overhead or other-platform performance. Each completed
candidate verifies that its runtime hashes remain unchanged throughout the run.

Fresh engine captures qualify the Marine, Strogg and multiplayer bitmap graphs
on OpenGL and Vulkan: all six animated pairs change and all six Reduced Motion
pairs remain identical. Native audio fixtures compile no paths after warm-up;
both visible MP voice displays add about 0.0032 ms of host CPU work. Live
microphone/network voice is outside this capture fixture. The coordinated scope
fix also preserves Vulkan dialog text with atlas caching and replay enabled.
Evidence and full settings are in `.tmp/ui-near-legacy-20261013/REPORT.md`.
This follow-up preserves the shipping legacy-UI default; the broader negligible
overhead requirement below remains open.

## Complementary renderer checks (13 October 2026)

The same staged build was then tested in single-player/OpenGL and
multiplayer/Vulkan, with the same 1280x720, MSAA 8, uncapped settings, 25-second
warm-up, six-second settling and four five-second windows per condition. Each
scene was repeated with the condition order reversed. Every interval contributes
to the means; no slow windows were discarded.

| Scene and condition order | Legacy ms | New UI ms | Difference |
| --- | ---: | ---: | ---: |
| Air Defense 1 SP / OpenGL, ABBA twice | 3.552 | 3.634 | +2.31% |
| Air Defense 1 SP / OpenGL, BAAB twice | 3.971 | 3.787 | -4.63% |
| Q4DM1 MP / Vulkan, ABBA twice | 2.480 | 2.521 | +1.66% |
| Q4DM1 MP / Vulkan, BAAB twice | 2.515 | 2.519 | +0.13% |

The reverse OpenGL run contains a pronounced legacy-side slowdown; the negative
difference is not evidence that the new UI is intrinsically faster. Vulkan's
individual balanced blocks also vary. These measurements support near parity in
the sampled gameplay, but cannot resolve an exact small overhead under this host
variation. The MP fixture is a solo listen server, not a populated match.

Retained root CPU p50 is 0.0567-0.0639 ms in SP and 0.0681-0.0858 ms in MP.
All four 300-frame profiles report zero layout rebuilds; actual animated-property
scans remain about one per SP frame and two per MP frame. Source polling,
property application and draw submission account for the remaining bounded work.
There is no additional dominant UI bottleneck demonstrated by these profiles,
so this follow-up makes no runtime or content changes. Engine screenshots confirm
the expected HUD gauges and crosshair in both combinations. All four runs use
identical runtime hashes, unchanged during sampling and matching the staged build.
Logs, full settings, per-window results and captures are recorded in
`.tmp/ui-cross-api-20261013/REPORT.md`.

## Menu and gameplay optimization (13 October 2026)

Interactive screens now retain separate trees containing controls, modals and
their ancestors. Eligibility and modal visibility no longer scan decorative
artwork or rebuild the full element index every frame. Ancestor visibility,
pointer eligibility, mask bounds and modal ordering keep their existing rules;
document replacement and shutdown retire the cached handles.

The coverage atlas now splits wide or tall stationary vector meshes into tiles.
It keeps the existing 32 pages of 512x512 RGBA8 storage (32 MiB each of CPU and
GPU pixel storage), nearest sampling and identity padding. Empty tiles and
unused tile borders are omitted, including a check that preserves additive RGB
with zero alpha. Multiply artwork uses white RGBA for empty pixels and padding,
with its original destination-colour blend. Both blend modes share the same
bounded pages. A mesh
uses the original geometry until every required page upload succeeds; partial
allocation failure releases all of its tile references. Fractional motion,
gradients, overlap and oversized bounds retain the geometry path. Temporary
rasterization is independently capped at four million pixels, so widely
separated small shapes cannot allocate an unbounded bitmap.

Single cached coverage meshes can apply live isolated opacity directly. Stable
compound plates retain their completed target so an overlapping fill and rail
still receive group opacity once. New cache entries must have survived an
earlier presentation frame; animated masks cannot evict the current working set.

Changing analytic masks can also sample unchanged source artwork from that
bounded layer pool. The source is immutable while the mask and group opacity
remain live; `ui_retainedSourceCache 0` selects the original composition path.
Same-frame animated-mask comparisons on OpenGL and Vulkan at MSAA 8 differ
by at most one RGBA8 code value. Exact identity transforms share the null
transform's geometry key. Affine basis vectors are transformed as directions,
avoiding precision loss from subtracting translated float positions. Compilation
reuse across small transform noise is limited to less than 1/1024 pixel over
the complete compiled region, including artwork outside its authored box.

Quiet canonical menus skip layout when no visible value editor or scrollbar
needs a native update. Interaction, focus reveal and owned-widget settling still
run. Hidden widgets resume ordinary updates when shown. Dense analytic coverage
uses row bins and small per-row sorts; sparse, widely separated contours retain
the bounded comparison sort. The same coverage integration and paint rules
remain in use.

Single-player pause screens retain the unchanged 3D scene after two consecutive
stable presentation frames. The HUD, view effects and menu timelines continue
to render. Resuming, a changed view or simulation time, a presentation gap,
resize, video restart and special vision paths invalidate the scene. The
unarchived `ui_retainedPauseCache 0` diagnostic restores ordinary scene drawing.
Engine and game modules must be rebuilt and staged together (game API 52).
Reused pause scenes also skip redundant entity and weapon pose preparation.
The separable soft-focus shader pairs adjacent Gaussian taps where possible
and computes successive weights by recurrence. Wider kernels retain their
original sample spacing.

The root profiler includes CPU and frame-interval p99/maximum values and vector
compile/upload times. `ui_retainedGeometry` lists active meshes, revisions and
atlas residency. Native coverage and production-adapter checks cover tile
boundaries, padding for both blends, sparse/additive tiles, upload failure/retry,
revision retirement and exhaustion of the page limit. Native tests also cover
compound alpha, quiet-button feedback and pause-scene invalidation.

On this Windows host, the 1,200-frame native title benchmark's median falls
from 0.508 to 0.384 ms after row binning and button-layout changes; median path
compilation falls from 0.275 to 0.188 ms. Trimming atlas tiles lets the paused
HUD and menu retain about 83,000 of 86,000 submitted vertices, up from about
54,000, within the same page limit. In 300-frame OpenGL and Vulkan profiles,
retained pause-root CPU p50 falls from about 0.5 to 0.26 ms. Whole-frame timing
varies substantially between runs, so these are diagnostic checkpoints rather
than final parity acceptance.

Engine captures on both APIs at 1280x720 qualify title/pause, focus and modal
composition with caches disabled, enabled and disabled again. Vulkan title
captures match exactly; other differences are isolated one-code-value rounding.
Pause-cache checks also cover cold startup, video restart, resume and reopening.
Shader-source A/B/A captures qualify the blur against its original kernel.
Evidence is under `.tmp/ui-runtime-optimization/`. Gameplay was within about
8% of legacy before these menu changes; final gameplay repeats and title/pause
whole-window parity remain open. The user explicitly included both menus in
the acceptance scope.

The subsequent balanced four-window menu comparison keeps all frame intervals
and stalls, with six seconds of settling and five seconds per window. Staged
runtime hashes match the optimized build and remain unchanged during each run:

| Scene / renderer | Legacy ms | Retained ms |
| --- | ---: | ---: |
| Title / OpenGL | 0.676 | 1.453 |
| Pause / OpenGL | 0.670 | 1.355 |
| Title / Vulkan | 0.405 | 1.179 |
| Pause / Vulkan | 0.400 | 0.798 |

The remaining menu gap is substantial. A separate source-cache toggle comparison
reduces title time by about 8% on OpenGL; Vulkan differs by about +2%, within
the variation of this run, so no Vulkan whole-frame gain is established.
Settled interactive screens now replay their paint commands, including fresh
backdrop capture. Transient targets are reserved and cached leases validated
before replay draws to the destination. Atlas opacity draws have a read-only
residency check, so a disabled, resized or dirty atlas returns to traversal
before any partial frame is painted. Visible native value widgets continue
their ordinary update path. Unchanged button geometry also avoids repeated
focus/hit-bound walks; input, bound state, layout and visibility changes refresh
it. Native tests cover stationary hover, moving/disabled controls, a changing
background under replayed blur, resize and unsupported-host fallback. Eight
UI suites pass, including focus, popup, scrollbar, number, HUD and view effects.

This combined change reduces pause-root CPU p50 to 0.080 ms on OpenGL and
0.069 ms on Vulkan. Balanced pause whole-window means are 1.095 versus 0.697 ms
legacy on OpenGL, and 0.684 versus 0.409 ms legacy on Vulkan. The animated title
still adds substantial work (1.519 versus 0.666 ms OpenGL; 1.266 versus 0.479 ms
Vulkan). The latter run includes pronounced host variation; all intervals
remain included. Menu parity is still unqualified. Engine capture A/B/A checks
pass on both APIs at 1280x720, and the preceding replay change also passes at
720x1280, with exact or isolated one-code-value differences.

The matching pre-replay gameplay repeat places Air Defense 1 within 3.04%
(OpenGL) and 1.35% (Vulkan) of legacy, and solo Q4DM1 within 3.09% and 1.51%.
Each uses four five-second windows per condition and six seconds of settling,
with staged build hashes unchanged throughout. The shorter replay/interaction
recheck measures +3.58%/+3.79% for Air Defense 1 and +7.82%/+1.41% for Q4DM1
(OpenGL/Vulkan). A slower OpenGL MP window remains included. The experiment
with eight 1024-square atlas pages is rejected: within the same 32 MiB budget,
page fragmentation leaves substantially more pause geometry uncached. The
qualified atlas remains 32 pages of 512 square pixels.

Moving convex three/four-sided linear-alpha masks now have a material shader
path. It intersects the shape with each physical pixel and samples cached
artwork through that coverage, eliminating the title glint's per-frame vector
compilation and upload. Stationary masks retain their caches; unsupported
geometry, strokes, stop values and hosts use ordinary coverage. Mask snapshots
remain immutable, clipping stays in physical pixels, and source targets are
never modified. `ui_retainedLinearMasks 0` disables this path for comparison.
Native checks exercise reflected/sheared transforms, source-cache on/off,
overlapping artwork, live opacity, resize and feature fallback. Nine UI suites
pass. Engine captures compare CPU and shader masks at the same animation time
on both APIs, including four-sided masks and video restart; all differ by at
most one colour code value.

A balanced feature-only title comparison measures 1.539 to 1.000 ms on OpenGL
and 1.255 to 0.574 ms on Vulkan. Root CPU p50 falls to 0.174/0.183 ms, and vector
compilation/upload counts are zero during the profile. These compare the
retained path with its optimization off/on; they do not establish parity with
legacy. The next comparison must again include legacy title and pause screens.

Decorative transforms outside control/modal ancestry now resolve their paint
properties without a full layout update. Moving controls still update layout
and hit bounds. The focused native fixture and all nine UI suites pass.
Sharp four-pixel mask ramps revealed two additional correctness requirements:
subdivided CPU masks must keep their single-sample target under window MSAA,
and Vulkan GUI uploads must ignore pointer-only memos whose cache headers may
have been recycled during an in-progress presentation. World depth/light-pass
and GPU-skinning reuse remain intact. Production-function adapter tests cover
that distinction. Clean SMP 1 / MSAA 8 captures of CPU/shader mask transitions
and video restart pass on both APIs with at most one colour-value difference.

The subsequent `linear-transform-menu-ab` comparison includes legacy again:

| Scene / renderer | Legacy ms | Retained ms |
| --- | ---: | ---: |
| Title / OpenGL | 0.724 | 0.938 |
| Pause / OpenGL | 0.680 | 1.083 |
| Title / Vulkan | 0.409 | 0.525 |
| Pause / Vulkan | 0.405 | 0.640 |

All runtime hashes stay unchanged. Title vector compilation/upload remains
zero; root CPU p50 is 0.143/0.161 ms for title and 0.077/0.066 ms for pause
(OpenGL/Vulkan). These are improvements, but menu parity remains open.

A subsequent atlas change groups sixteen independent 512-square pages into
each 2048-square image. Allocation and four-frame retirement still use 32
separate page slots, and the GPU cap stays 32 MiB; a dirty upload temporarily
assembles 16 MiB. Failure/retry and recycling preserve live neighbouring pages.
API draw counts drop from 73 to 40 for title and 96 to 59 for pause with the
same atlas residency. Both APIs pass cached/uncached title, pause, focus and
modal captures at SMP 1 / MSAA 8, and exercise video restart. The timings vary
too much to establish a stable cross-renderer gain from this change alone.

Decorative transforms now use RmlUi's typed primitives directly. Native tests
compare them with independently parsed CSS at densities 1, 1.5 and 2, with
DP/PX translation, reflected/nonuniform scaling, rotation and snapshot restore.
Sharp mask captures also pass on both APIs. With the shipped metric settings
(renderer metrics 0, whole-frame GPU timing 1) during balanced windows, the
combined candidate measures:

| Scene / renderer | Legacy ms | Retained ms |
| --- | ---: | ---: |
| Title / OpenGL | 0.601 | 0.732 |
| Pause / OpenGL | 0.652 | 0.997 |
| Title / Vulkan | 0.359 | 0.431 |
| Pause / Vulkan | 0.363 | 0.620 |

Detailed profiling runs separately after those windows. Title root CPU p50 is
0.106/0.091 ms. Earlier runs enabled per-pass metrics throughout; their timing
overhead accounts for only part of the gap. Both menus remain above legacy.

A settled opaque blur covering the complete viewport now finishes its second
pass on the base surface, avoiding a temporary destination and final composite.
The live backdrop is still captured every frame. Partial, translucent, streak
and unsupported cases retain their layered path; `ui_retainedDirectBackdrop 0`
selects that reference. Native tests and production-adapter checks cover those
guards. Engine captures pass on both APIs at 1280x720 and 720x1280, including
focus, modal and video restart with SMP 1 / MSAA 8. Portrait comparisons are
exact; wide differences are isolated one-value pixels. Default-metric pause
means are 0.544/0.684 ms for legacy/retained on OpenGL and 0.344/0.552 ms on
Vulkan, so menu parity remains open.

Decorative-only projection now also avoids refreshing unrelated control hit
bounds; native coverage keeps moving controls and modal ancestry live. The
convex-mask shader skips pixel clipping for wholly contained/outside pixels,
with CPU/shader sharp-mask comparisons still within one colour value on both
APIs. Moving blur weights to vertices and grouping disjoint host draws were
tested and rejected because neither established a useful frame-time gain.

`ui_retainedHalfBlur` filters each axis before reducing that axis for opaque,
direct full-view 6–8 pixel kernels. Its horizontal target keeps every source
row; the vertical pass then halves height, followed by a linear resolve. A
half-centred kernel averages two original Gaussian kernels before pairing
samples, avoiding the high-frequency aliasing of integer-centred pairs at this
sampling phase. Other kernel widths, odd sizes, offsets and partial/layered
effects retain the full-resolution path. Every frame still captures live
content. Resource, resize and fallback guards are covered by the host adapter.

The upsample is a spatial approximation. Wide production title, pause, focus
and modal comparisons differ by at most one colour value; portrait fallback
comparisons are exact. Both APIs exercise SMP 1, MSAA 8 and video restart.
Full-view stripe, step and colour-grid fixtures at widths 6, 7.5 and 8 produce
identical reference-return captures; candidate differences reach 4–5 values at
edges with mean absolute error below 0.3/255. Separate GPU diagnostic medians
fall from 277 to 173 microseconds on Vulkan and 434 to 365 on OpenGL. Default
whole-frame feature comparisons improve only about 3%, with retained variation;
this is not menu parity. Evidence is under
`.tmp/ui-runtime-optimization/reduced-blur-{filtered,patterns,portrait}` and
`reduced-blur-feature-default`.

Ordinary single-player pause now holds the presentation scene clock at the
authoritative game time. Previously its drift correction periodically restarted
interpolation despite a stopped simulation, moving the camera back toward the
previous pose and invalidating the scene cache. HUD/menu animation keeps its
independent clock. Native tests cover a long pause, resume, multiplayer and demo
behavior. Cache/reference engine captures without `g_stopTime` pass on both
APIs with SMP 1 / MSAA 8, video restart and resume/reopen; differences are
isolated one-value HUD pixels. The eleven UI/material suites pass.

The `menu-live-clock` comparison uses ordinary pause, rather than a diagnostic
simulation stop. Vulkan title means are 0.346/0.367 ms (legacy/retained), but
pause remains 0.348/0.563 ms. OpenGL title is 0.690/0.849 ms with large window
variation, and pause is 0.621/0.952 ms. These runs do not establish menu parity.

`ui_retainedHudStateCache` skips an identical stock HUD expression walk at the
same simulation time while a working Remastered peer owns drawing. It compares
the complete dictionary's pooled immutable entries, including direct dictionary
writes, and invalidates for timed scripts, input/named events, activation,
presentation writes, reload/restore and Classic fallback. Explicit redraw and
GUI editing/script diagnostics bypass it. The cached dictionary is never saved.
HUD drawing and animation continue on their original clocks. Production-method
tests cover repeated publications, mutations, clock changes and bypasses.
Both APIs pass ordinary pause, restart and resume/reopen captures within one
value in isolated HUD pixels. Feature-only pause means improve from
0.553 to 0.486 ms on Vulkan and 0.940 to 0.873 ms on OpenGL. Whole-menu parity
still needs to be established against legacy with the final candidate.

## Final candidate checks (14 October 2026)

The final candidate also reduces repeated GUI number publication. Integral
floats keep their original six-decimal dictionary representation, including
negative zero; fractional, large and exceptional values retain the original
formatter. Integer publication uses a locale-independent integer conversion.
Fixed cinematic rectangle keys no longer need formatting each frame. Unchanged
stock dictionary values keep their pooled storage, but every application write
still reaches the replacement so it can override an intervening UI event.
Debug logging retains its old-value checks only while that logging is enabled.
The native fixture compares 65,536 float bit patterns with the original
formatter; the production-method adapter covers publication, direct dictionary
mutation and event overrides. All eleven UI/material suites and the five
renderer/HUD adapters pass after the final source changes.

Classic presentation lights now receive the same held scene clock as the
camera and entity poses during an ordinary single-player pause. This avoids
retiring or fading lights behind the reused view. Multiplayer retains its
original presentation path.

A separate flattened-menu surface experiment was removed. Its final isolated
pause comparison improved Vulkan by about 5% and OpenGL by about 1.5%, while
adding a render target, invalidation machinery and up to five colour values of
composition rounding. The existing bounded artwork caches and command replay
remain. Prototype measurements with `ui_retainedFrameCache 1` do not describe
the final candidate, and that experimental CVar is no longer registered.

The refreshed `menu-qualified-final` comparison uses normal animation, ordinary
pause, default GPU timers, detailed renderer metrics off, MSAA 8 and SMP 0 at
1280x720. Each uncapped condition has four five-second windows in ABBAABBA
order; 144/240 Hz each have two per condition. No slow windows were discarded.
The staged binary hashes match the optimized build and remain unchanged.

| Scene | Legacy uncapped mean ms | Retained uncapped mean ms | Difference |
| --- | ---: | ---: | ---: |
| Vulkan title | 0.367735 | 0.378361 | +2.89% |
| Vulkan pause | 0.346451 | 0.483137 | +39.45% |
| OpenGL title | 0.602364 | 0.708452 | +17.61% |
| OpenGL pause | 0.600295 | 0.867025 | +44.43% |

At 144 Hz, all four mean comparisons differ by at most 0.021%; at the default
240 Hz, at most 0.293%. Both conditions' p99 is 8–9 ms at 144 Hz and 6 ms at
240 Hz. One retained OpenGL title window includes a 22 ms maximum; it is kept
in the result. These caps give comparable observed pacing on this hardware,
but do not erase the uncapped overhead. **The requested title/pause parity gate
remains open.** Final engine title/pause captures were inspected on both APIs.

Separate diagnostics, with detailed renderer metrics enabled, put title GPU
medians below legacy on both APIs (OpenGL 219/252 us; Vulkan 101/119 us,
retained/legacy). Pause remains higher (353/243 us; 173/113 us). Title therefore
still warrants CPU/submission investigation, particularly on OpenGL; pause has
both additional HUD/front-end work and soft-focus GPU work. These diagnostic
samples must not be substituted for the whole-window frame-time measurements.

The refreshed uncapped gameplay windows (`gameplay-final-numbers`) give:

| Scene | Legacy mean ms | Retained mean ms | Difference |
| --- | ---: | ---: | ---: |
| OpenGL Air Defense 1 | 4.098225 | 3.936005 | -3.96% |
| Vulkan Air Defense 1 | 2.421579 | 2.532122 | +4.56% |
| OpenGL Q4DM1 | 3.345477 | 3.449113 | +3.10% |
| Vulkan Q4DM1 | 2.265403 | 2.674543 | +18.06% |
| Vulkan Q4DM1, reverse order | 2.280696 | 2.265670 | -0.66% |

The apparent OpenGL SP gain is within substantial window variation. Vulkan MP
had one retained window at 3.323 ms while all legacy windows stayed around
2.27 ms; the reverse-order repeat stayed close throughout. Both runs remain
in the evidence. Three cases support the earlier near-legacy gameplay finding;
Vulkan MP's variation prevents a precise new overhead claim from this sample.
These are warmed static cameras and a solo MP fixture, not a loaded server or
other-hardware qualification.

Final ordinary-pause, video restart and resume/reopen captures with SMP 1 and
MSAA 8 pass on both APIs (`pause-final-numbers`): at most one colour value in
246 HUD pixels, also present in reference-return comparisons. After the timing
runs, review removed an extra dictionary lookup that could read a freed key
when that key was supplied by the overwritten value. The production adapter
now models separate key/value pools and covers that alias. The rebuilt HUD and
screen suites pass; this narrow correctness fix is subsequent to the timed
binary identities recorded above. The final staged SP/Classic-switch smoke
also passes on both renderers with matching build/stage hashes and no retained
warnings or errors (`publication-review-smoke`).

## Moving mask and crossfade replay (14 October 2026)

The title's counter-rotating glint can now replay its stationary source while
refreshing the analytic mask. Replay first validates the complete projected
artwork, clip and source-cache lease; geometry changes, unsupported paint or
lost resources use the ordinary traversal. This stays under the existing
`ui_retainedReplay` switch. Eight live mask poses, reference returns and video
restart produce identical RGB pixels on both APIs with SMP 1 / MSAA 8
(`mask-replay-visual`). Native coverage also exercises changing opacity,
resizing and disabling the analytic-mask host path.

Four-window feature comparisons (`mask-replay-feature`) give 0.737702 versus
0.663832 ms on OpenGL and 0.380998 versus 0.367069 ms on Vulkan with replay
off/on. Both include their slower windows; these are feature comparisons,
not a fresh legacy-parity result. A separate recorded-draw ordering experiment
was removed: OpenGL gains were inconsistent and a Vulkan candidate window
slowed substantially. Its results remain in `record-order-feature`.

Fitted leaf images now support the same isolated-opacity replay as scrolling
images. The title montage uses opacity for its white-tinted fades, preserving
the authored timings. Decorative leaves beside buttons avoid a menu layout
walk; interactive ancestry, owned widgets and compound groups retain normal
style preparation. Native tests compare replayed and normal draws through
zero/full-opacity crossings and verify that hover and control updates remain
correct. Twelve same-frame reference/candidate captures per API are RGB-exact,
including video restart (`menu-fade-qualified`). Two expected late-preload
notices belong to the private preview's stock materials and are retained in
the reports; there are no other retained warnings or runtime errors.

All eleven UI/material suites pass in `test-menu-fades-relink.log`. Earlier
widget crashes in this increment were traced to stale Host-interface objects:
restoring a rejected experiment also restored old header timestamps. Rebuilding
all interface dependents resolves those failures; they are not waived. The
updated combined candidate is staged and compared in `menu-fades-comparison`.
Each uncapped condition has four five-second windows with four-second settling,
normal motion, MSAA 8 and SMP 0. The affected uncertain cases also have four
windows per condition in the opposite order (`menu-fades-reverse`).

| Scene/order | Legacy mean ms | Retained mean ms | Difference |
| --- | ---: | ---: | ---: |
| Vulkan title, ABBAABBA | 0.348621 | 0.352338 | +1.07% |
| OpenGL title, ABBAABBA | 0.587101 | 0.686237 | +16.89% |
| OpenGL title, BAABBAAB | 0.602469 | 0.662329 | +9.94% |
| OpenGL pause, ABBAABBA | 0.614057 | 0.917462 | +49.41% |
| Vulkan pause, ABBAABBA | 0.518419 | 0.511126 | -1.41% |
| Vulkan pause, BAABBAAB | 0.344365 | 0.481389 | +39.79% |

Vulkan pause's first order includes a 0.926770 ms legacy window, versus its
other legacy windows at 0.363564–0.413072 ms. It is retained in the mean, but
that run cannot establish parity. The opposite order is stable and confirms
the remaining pause overhead. No cause for the isolated slow window has been
established. OpenGL title's retained root p50 is now 0.0506 ms, with zero layout
updates and one replay per frame in the sampled 300 frames; whole-frame
OpenGL title remains 10–17% above legacy across these two orders. **The menu
parity gate remains open.**

A shader-only constant Gaussian-weight experiment was also rejected. Its
OpenGL original/candidate means were 0.908385/0.901855 ms, and Vulkan
0.476746/0.505918 ms. Separate GPU medians did not improve consistently. The
experiment changed only private test-profile shader overrides and was never
incorporated in the staged package (`constant-blur-feature`).

The combined candidate also passes ordinary pause, video restart, resume and
reopen on both renderers with SMP 1 / MSAA 8 (`pause-menu-fades`). Compared
captures differ by at most one colour value in 244 OpenGL / 210 Vulkan HUD
pixels, including reference-return variation. Current paused and resumed
engine captures were visually inspected.

A subsequent dirty-backdrop cache was rejected. Exact queued-2D comparisons
localized the live ECG and allowed partial blur updates. Cropping its source
draws reduced Vulkan diagnostic GPU duration to about 85 microseconds from
173, but whole-window means worsened from 0.486059 to 0.521626 ms (+7.32%).
OpenGL's apparent 3.44% gain depended on a slow baseline window, and its
candidate GPU medians were higher. Its native admission/cropping adapter and
OpenGL pause/resize/restart/resume captures passed; Vulkan pixel qualification
was not run after the timing rejection. All prototype changes are removed;
`backdrop-cache-rejection.json` and `rejected-backdrop-cache/` preserve the
results and implementation. This does not change the accepted parity findings.

## Large documents

5 October 2026. The multiplayer Escape card (`mp_escape.q4ui`) is the largest
retained document: 7.6 MB, 4,499 nodes, 366 controls, 2,173 bindings, 1,925
timelines with 5,240 tracks and 34,395 authored properties. The game loads it
with every multiplayer map, and on a debugoptimized build its load took
8-12 s and each frame half a second, because several steps did work
proportional to the whole document once per node, binding, track or changed
property:

- **Node and element lookup.** `DocumentModel::FindNode` searched the tree and
  RmlUi's `GetElementById` searches the whole document breadth-first. A load
  configured each node with two searches and the value view resolved every
  node with one; each frame resolved every changed property, every control's
  bounds and every focus query the same way. A validated document's model now
  indexes its node IDs (a copy starts unindexed and searches), and the runtime
  resolves each canonical node's element once per load into a map that lives
  as long as the document. Canonical elements never change while it is open:
  text nodes own no canonical children, and a choice popup moves under the
  document but lives on. The value view and number fields take the same
  lookup.
- **Validation.** Each binding was checked against every timeline track, and
  each widget control against every binding and track. Both now look the
  owned (node, property) pairs up in sets built once.
- **Applied properties.** A new state revision made `ApplyMotion` format all
  34,395 properties again. Only the properties bindings own can change with
  the state, so the rest keep their fast path.
- **Modals and eligibility.** The modal check visited every node on every
  state read and binding pass; it now visits only the nodes on a path from the
  root to an authored modal. Input eligibility records only controls and skips
  the subtree under an ineligible node, whose controls read as ineligible.
- **State reads.** Each frame's read of the CVar sources copied the whole
  state; it now copies it only when a source has changed.
- **Transactional copies.** Events and completion programs evaluate on copies
  of the state and motion. The document's timelines, authored values,
  declarations and bindings never change after a load, so copies share them
  and duplicate only live values.
- **Playing tracks.** Every play first advanced every playing track, so the
  hundreds of control feedback timelines started at load sampled one another
  quadratically. Sampling again at the same clock reproduces the same values,
  so a burst of plays at one clock samples once; a presentation write or a
  restore still forces a fresh sample.

`openq4-retained-ui-benchmark --screen` now also reports `load_ms`, the
runtime's document load. One machine (Windows 11, debugoptimized), the
committed runtime (`dde802ef`) against this change, three runs each, 240
frames at 1280x720 after the four-second settle; medians:

| Document | Load before | Load after | Frame p50 before | Frame p50 after | Frame p95 before | Frame p95 after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `mp_escape.q4ui` | 9,484 ms | 624 ms | 504.0 ms | 13.4 ms | 764.3 ms | 61.0 ms |
| `mp_welcome.q4ui` | 231 ms | 117 ms | 4.50 ms | 0.73 ms | 5.25 ms | 0.85 ms |
| `title.q4ui` | 80 ms | 78 ms | 0.96 ms | 0.67 ms | 1.12 ms | 0.88 ms |
| `pause.q4ui` | 70 ms | 63 ms | 1.12 ms | 0.29 ms | 1.23 ms | 0.36 ms |

The native screens test, which loads the Escape card four times, fell from
104-187 s to 42 s. The canvas suite (`ui_document_canvas.py`) and its 30
mutations, the native suite and the script tests that read these sources pass.

What remains: parsing the 7.6 MB source and reading its nodes take most of the
load, the Escape card's frame was mostly the presentation pass over its values
(see [Settled frames](#settled-frames)), and
each event or completion program still copies the motion and state's live
values: up to 100 ms on the Escape card's page and modal changes in the
native screens test.

## Settled frames

8 October 2026. Main-thread stack samples of the screen benchmark on the
Escape card (cdb, 30 samples) put about two thirds of a settled frame in
`Runtime::Impl::ApplyMotion` and most of the rest in `ValueControlView::Paint`,
not in RmlUi's layout. Every frame the presentation pass walked all 34,395
authored values, comparing each against the input it last applied: two string
compares per key, and the 1,269 text values translated and looked up again.

`Motion` now stamps every write that can change its values (a sample, a play,
a cancel, a reduced-motion jump, an external write, a restore, a reset) from a
counter shared by all copies, so an unchanged stamp means unchanged values.
`ApplyMotion` returns at once while the stamp, the state revision and the text
ratio are those of its last complete pass; a language change reloads the
document. A pass that finds an element missing leaves the next pass complete.
`RuntimeStatistics::motionInputsScanned` counts the values a frame's passes
walk, and the native screens test pins it at zero on a settled Load Game page
and above zero after a state change. Documents with a looping timeline, such
as the title's montage and glint, still walk their values each frame.

Same machine and build type as above, 60 frames at 1280x720 after the settle:

| Document | Frame p50 before | Frame p50 after | Update p50 before | Update p50 after |
| --- | ---: | ---: | ---: | ---: |
| `mp_escape.q4ui` | 14.21 ms | 5.85 ms | 13.44 ms | 5.07 ms |
| `mp_welcome.q4ui` | 0.82 ms | 0.28 ms | 0.71 ms | 0.18 ms |
| `joingame.q4ui` | 1.89 ms | 0.50 ms | – | 0.25 ms |
| `createserver.q4ui` | 1.66 ms | 0.89 ms | – | 0.61 ms |
| `loadgame.q4ui` | 0.41 ms | 0.17 ms | – | 0.08 ms |
| `pause.q4ui` | 0.39 ms | 0.16 ms | – | 0.07 ms |
| `title.q4ui` | 1.09 ms | 1.02 ms | – | 0.37 ms |

What remained on the Escape card was `ValueControlView::Paint`, which
reasserted every choice option's label, availability and display each frame
through string-keyed lookups and RML readbacks (`GetInnerRML`).

### Settled lists

The runtime now counts `ApplyMotion`'s passes that can write presentation
and hands the count to `ValueControlView::Paint`. A closed list whose
readback (value, labels, availability and option count) equals the one its
last paint applied in the same pass is left as painted: nothing else writes
its rows. An open list, a changed readback or a new pass paints in full, and
a count of 0 keeps the old behaviour. A document whose timelines loop, such
as the title, starts a pass every frame, so its lists would still paint in
full; Join Game and the title have none.

Interleaved A/B, the same build with and without the skip, alternating runs,
median frame p50 of five runs of 60 frames at 1280x720 (debugoptimized). The
machine ran lighter than for the table above, where the Escape card measured
5.85 ms:

| Document | Without | With |
| --- | ---: | ---: |
| `mp_escape.q4ui` | 2.13 ms | 0.67 ms |
| `createserver.q4ui` | 0.91 ms | 0.62 ms |
| `mp_welcome.q4ui` | 0.26 ms | 0.18 ms |
| `joingame.q4ui` | 0.52 ms | 0.51 ms |
| `title.q4ui` | 1.10 ms | 1.00 ms |

## Startup preload

9 October 2026. With `ui_retained 1` the title waited while the session
loaded every retained page it might hand off to and precached every
multiplayer map's levelshot (`idSessionLocal::PreloadRetainedScreens`, inside
`idSessionLocal::Init`), then every Arena match's (`idArenaCampaign::Init`).
Timestamped startups (`com_timestampPrints 2`) of a build with temporary laps
around each step split the twelve pages' loads; medians of three, on a
debugoptimized build, where the laps add some cost of their own:

| Step, over the twelve pages | Time |
| --- | ---: |
| The GUI's parse and validation of the document | 1,989 ms |
| The view's load: the runtime parsed and validated the same document again, then built its markup, elements and bindings | 2,691 ms |
| The GUI's application values and first state | 1,307 ms |
| The 50 multiplayer levelshots | 338 ms |

Join Game (1,390 ms), Create Server (794 ms) and Demos (630 ms) were the
largest pages. The levelshots had cost 1.7 s while community maps' were
extracted again on every launch, which `a82b13ab` stopped. The Arena's 20
match levelshots, largely the multiplayer maps' pictures, took another
0.3 s after those, or 0.65 s once they no longer loaded first.

What changed:

- **One parse per load.** A `Document` copy now shares its parsed source and
  model: a load or an edit replaces the parsed document and never changes
  it. The GUI hands its validated document to its view
  (`RetainedUI_LoadParsedView`, `Runtime::LoadDocument( const Document& )`),
  so each page is parsed once. Every later load gains too: the pages a level
  loads, and the reload of every GUI at each map change.
- **Cheaper validation.** A check builds its pointer and message only when it
  fails (`Require` became a macro over `Fail`). The finiteness walk builds no
  pointers and finds the failing value's pointer only on failure; field
  checks read member names in place instead of copying them; the
  per-command and per-control lookup tables are built once. Diagnostics are
  unchanged: the native document tests compare pointers and messages. The
  runtime's own load of the twelve pages, which includes its parse, fell
  from 1,882 to 1,228 ms (`openq4-retained-ui-benchmark --screen`
  `load_ms`, interleaved, five runs each).
- **Parsing on the job system's workers.** Before the stock main menu loads,
  the session reads the startup documents and queues one parse each on the
  job system (`PrefetchRetainedScreens`, `UI_RetainedPrefetchDocuments`). A
  page's load takes its parse when the source is unchanged, waiting for it
  if it is still running, and keeps only the markup, element and binding
  work on the main thread. Parses no load took are dropped after the
  preload. A synchronous job system (`jobs_enable 0`, `jobs_deterministic 1`)
  or a full job queue leaves the load to parse inline, as before.
- **Only what the title reaches.** Startup loads the title, Load Game,
  Credits, Demos, Arena, Multiplayer, Create Server and Join Game. The pause
  screens, the pages only they reach (Save Game, Restart Level) and the
  death screen load with the first single-player level
  (`PrepareRetainedLevel`), once a session, as the Strogg pause already did.
- **Levelshots on idle frames.** Startup lists the multiplayer maps and the
  Arena matches (`QueueRetainedLevelshot`). Once a front-end menu has
  presented for a second, each frame precaches as many of their levelshots
  as fit 4 ms, at least one, as precaches rather than late loads
  (`PrecacheRetainedLevelshots`). Create Server reads any map levelshot
  still waiting when it opens, and the Arena page any match levelshot, so
  browsing either never loads one. A level in play holds the queue until the
  front end returns, and an engine reload, already a load, reads it at once.
  `ui_retainedTrace 1` logs each step (`RETAINED_LEVELSHOTS`).

Two interleaved A/Bs against the build before this work (`03ddba97`), each of
eight rounds: hidden 1280x720 OpenGL window, `s_noSound 1`, debugoptimized,
each side with its own warm save path, every launch quitting from its
autoexec. All sides read the same `pak0.pk4` (the older copies' embedded
checksum was patched to it), so they load identical documents. The first
rotated the order of three builds to separate the steps; the second
alternated the final build with the baseline. The first title frame is timed
from the log's opening at process start. Medians:

| Build | First title frame | Session initialization | Pages loaded at startup |
| --- | ---: | ---: | ---: |
| Before | 14.2 s | 6.9 s | 12 |
| One parse, cheaper validation, level pages and multiplayer levelshots deferred | 9.5 s | 3.3 s | 8 |
| The same, parsing on workers | 9.2 s | 2.7 s | 8 |

| Build | First title frame | Session initialization |
| --- | ---: | ---: |
| Before | 13.6 s | 6.8 s |
| After: the above, and the Arena levelshots on idle frames | 8.8 s | 2.1 s |

Paired by round, the title came 4.7 s sooner (median; 3.6 to 4.9 s in seven
rounds, and 13.4 s in one where the machine stalled the baseline's launch).
Two traced startups (`ui_retainedTrace 1`) read all 70 levelshots in 66
and 68 idle frames, from 0.6-0.8 s to 2.1-2.3 s after the first title frame:
0.6-0.75 s of reading in all, at most 38 ms of it in one frame.

Hand-offs, engine probes with console verbs only (the menu audit's capture
harness with the probe scenarios `probe-preload-settled`,
`probe-preload-rush` and `probe-preload-arena-rush`), longest frame over each
step. The settled probe waits six seconds at the title first; the rush
probes go to their page as soon as they reach the title, within its first
two seconds:

| Probe | Step | Before | After |
| --- | --- | ---: | ---: |
| Settled | The title's first six seconds | 422 ms | 410 ms |
| Settled | LOAD GAME | 35 ms | 50 ms |
| Settled | MULTIPLAYER | 99 ms | 93 ms |
| Settled | CREATE SERVER | 122 ms | 118 ms |
| Settled | Browsing 12 maps | 43 ms | 57 ms |
| Rush | MULTIPLAYER | 84 ms | 52 ms |
| Rush | CREATE SERVER | 108 ms | 708 ms |
| Rush | Browsing 12 maps | 46 ms | 41 ms |
| Arena rush | SINGLE PLAYER | 72 ms | 62 ms |
| Arena rush | ARENA | 109 ms | 278 ms |

One run of each probe varies by tens of milliseconds: the first A/B's
probes, without the Arena change, measured the settled LOAD GAME at 76 and
31 ms, MULTIPLAYER at 121 and 77 ms, CREATE SERVER at 147 and 92 ms and the
browsing at 67 and 53 ms. The title's longest frame is its first. In the
settled probe the idle frames read all 70 levelshots, at most 33 ms of a
frame; in the rush probe Create Server opened with 35 of the 50 map
levelshots still waiting and read them in 575 ms, and the Arena page read
its 20 in 181 ms. Browsing stays smooth either way.

What remains:

- **A page opened at once.** Opening Create Server or the Arena page in the
  title's first two seconds, before the idle frames have finished, reads the
  levelshots still waiting in one frame (708 ms in the rush probe). Decoding
  levelshots on the job system's workers would remove it.
- **Map changes.** Every map change re-initialized every loaded GUI
  (`uiManager->Reload( true )` in `ExecuteMapChange`), the retained pages
  included: the startup's document work again, now one parse per page, on
  the main thread. The retained pages now keep their views
  ([map changes](#map-changes)).
- **Outside the menus.** Of the remaining wait before the title, hashing
  `pak0.pk4` and `pak1.pk4` (800 MB) against the executable's checksums takes
  about 1.4 s on every launch, and compiling the game's scripts about 1.2 s.

## Map changes

9 October 2026. Every map change re-initialized every loaded GUI
(`uiManager->Reload( true )` in `idSessionLocal::ExecuteMapChange`). With
`ui_retained 1` that took in every retained page: the eight the title reaches
at the first map change, and from the second on also both pause screens, Save
Game, Restart Level, the death screen, the loading screen and the weapon
wheel. Each read and parsed its document again, built a new view (markup,
elements, bindings) and restored the old view's snapshot into it: the
startup's document work again, on the main thread, inside the load. A restart
of the same map did it too.

The reload comes from Doom 3. The level load's purge
(`idDeclManagerLocal::BeginLevelLoad`) defaults every declaration not parsed
outside a level load, and the image manager frees every image a level load
read. A stock GUI's windows hold material pointers, and parsing it again finds
them inside the load, so their images load with the level instead of on the
first draw after it. A retained page needs less. The materials its view holds
survive a purge as objects, and its text and layers draw from generated
resources. What it needs is for the pictures it names to resolve again inside
the load: the preload and the level loads parse them as load media
(`SetInsideLoad`), so every map change purges them, and a purged picture drawn
later is parsed again mid-frame ("Loading non pre-cached material decl"), its
image read then too where a level load had read it. A language or renderer
change already rebuilds every retained view on its own (`RefreshResources`).

The first change made map changes call `UI_ReloadForLevelLoad`, initially
leaving stock GUI re-initialization intact (the follow-up below removes it).
A retained page whose document reads back byte for
byte unchanged keeps its view (`idUserInterfaceRetained::KeepForLevelLoad`):
the pictures it names resolve again inside the load, as its reload had them
do, and its pending input, programs and session requests are dropped, as a new
view would drop them. A changed document, or a view that cannot be prepared,
re-initializes as before; `reloadGuis all` still re-initializes everything;
pictures bound to state still resolve where their publishers precache them.
Each kept page logs `RETAINED_GUI_KEPT`.

Interleaved chains of map changes, the build before (`5d378666`) against this
one, in ABBA order: six rounds of each in single player and five in
multiplayer. Debugoptimized, hidden 1280x720 OpenGL window, `s_noSound 1`,
each side with its own warm save path and the same `pak0.pk4` (the older
client's embedded checksum patched to it), console verbs only. Single player
starts at the title and runs `map game/airdefense1`, then `game/airdefense2`
and `game/airdefense1` again; multiplayer runs a listen server's
`spawnServer mp/q4dm1` from the title, then `mp/q4dm2`. Timestamps
(`com_timestampPrints 2`) give the GUIs' share of each change, from the
level-load purge to the loading screen's first state, and `com_speeds` the
frame that held the whole change, from the wipe to the autosave. Medians, and
the median of each round's difference:

| Map change | Retained pages | GUIs, before | GUIs, after | Whole change, before | Whole change, after | Difference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Title to airdefense1 | 8 | 2.18 s | 0.65 s | 11.1 s | 9.6 s | -1.8 s |
| airdefense1 to airdefense2 | 15 | 2.53 s | 0.57 s | 6.3 s | 4.4 s | -2.1 s |
| airdefense2 to airdefense1 | 16 | 2.76 s | 0.66 s | 11.1 s | 9.6 s | -2.2 s |
| Title to mp/q4dm1 | 8 | 2.12 s | 0.59 s | 5.3 s | 3.8 s | -1.5 s |
| mp/q4dm1 to mp/q4dm2 | 11 | 2.77 s | 0.65 s | 5.5 s | 3.3 s | -2.2 s |

Re-initialized, the retained pages took 1.5 to 2.1 s of each change; kept,
47 to 85 ms, mostly reading each document back to compare it (Join Game's
4.3 MB the most, 12 to 19 ms). With the Escape card opted in
(`ui_retainedMultiplayer 1`, three rounds) the second multiplayer change
re-initialized 12 pages, the 7.8 MB card among them, in 3.4 s and kept them
in 127 ms: the whole change went from 7.0 to 3.5 s. The 120 frames after
each load were as smooth in both builds (longest 20 to 32 ms), and no
"Loading non pre-cached" warning named a retained picture in either; the
other warnings, player models and tables, were the same in both.

Hand-offs after the second single-player map change, two runs of each build
alternating (the kit's `probe-mapchange-sp`: a save on airdefense1, a map
change to airdefense2, then the pause, SAVE GAME, LOAD GAME and RESTART LEVEL
and back, a death and the death screen's LOAD GAME), longest frame over each
step:

| Step | Before | After |
| --- | ---: | ---: |
| The pause | 119, 107 ms | 101, 101 ms |
| SAVE GAME | 62, 54 ms | 42, 46 ms |
| LOAD GAME | 54, 55 ms | 38, 38 ms |
| RESTART LEVEL | 41, 31 ms | 31, 31 ms |
| The death screen | 148, 154 ms | 128, 145 ms |
| Its LOAD GAME | 228, 162 ms | 230, 220 ms |

Single runs vary by tens of milliseconds. With `fs_debug 1` over the steps
both builds opened the same files: the saves, the soft focus's shader and the
levelshots the front end's idle frames precache once the level is over (the
probe starts in the level, so the title never read them); no picture was read
late.

### Stock GUI reuse

9 October 2026, follow-up to the retained-page change. `GUI_LEVEL_PURGE_DONE`
(with `com_showLevelLoadTimes 1` and `com_timestampPrints 2`) now separates
level-load setup and the declaration/renderer/sound/BSE purge from the first
stock GUI. The earlier table's stock estimate included that preceding work.

An unchanged stock GUI now implements the same `KeepForLevelLoad` hook. It
keeps its parsed window tree, compares the root and every included source
byte for byte, and resolves its media inside the load. The parser observation
also remembers failed include candidates: adding a file that takes precedence
invalidates the cache even if the previously selected include is unchanged.
Explicit `reloadGuis all`, language reloads, changed/missing sources, failed
parses, incremental editor parses and GUIs restored from a save or demo use the full
reload. Custom game/editor widgets and native chat surfaces retain their
existing reload path.

A background-only walk is insufficient. During the original parse a scoped
declaration observer records materials, script-only background alternatives,
script sounds, expression tables and widget resources. Re-finding those
references after the purge restores their contents and level-use marks;
GUI material sorting is reapplied. The live window walk also references
current backgrounds, including simple windows and directly assigned scroller
backgrounds, and expression tables. Slider thumbs refresh their dimensions.
Previously displayed model previews rebuild their render-world references
inside the load so the model purge cannot leave a stale preview behind.

Window identities survive; their authored presentation/script values are
restored from an in-memory snapshot taken immediately after parsing. This
resets a previously open Load/Save subpage, timeline flags, transitions and
focus just as a fresh parse did. Constructor-assigned scroller backgrounds
are retained separately because they have no authored background name in
the snapshot. The backend repeats initialization/activation and drops
pending commands and bind handling.
The `text` and `name` dictionary defaults reset as in `InitFromFile`; other
game-published values stay live. GUI state publishers continue to update
bound pictures normally. The cache
is engine-private, counted in `listGuis`, and does not change game-module or
save-file formats. The source/media observation scopes are thread-local and
inactive outside parsing; no replacement game assets are added.

Four interleaved pairs per mode (AB, BA, AB, BA), after a separate warm-up
for each side, measured 40 map changes. Windows x64 debugoptimized, OpenGL,
1280x720 hidden/windowed, 60 fps, sound disabled, stock installed assets and
`ui_retained 1`; no device input. The client was the only runtime difference:
before was `37f1d1d5` plus the purge marker, SHA-256 prefix
`2be75b8952b9177d`; after was `c442b13083dd6903`. Renderer/game modules and
both staged packages matched. Peer clients and CPU tests were paused; free
commit checks were 25–29 GB. Timings below are milliseconds; before/after
columns are medians, and savings are medians of paired differences.

| Map change | Stock before → after | Stock saved | Setup/purge before → after | Whole change saved |
| --- | ---: | ---: | ---: | ---: |
| Title → `game/airdefense1` | 176.0 → 13.5 | 162.5 | 1.0 → 2.0 | 222.5 |
| → `game/airdefense2` | 327.5 → 62.5 | 266.5 | 57.0 → 58.5 | 385.0 |
| → `game/airdefense1` | 355.5 → 61.5 | 294.0 | 33.5 → 36.5 | 432.5 |
| Title → `mp/q4dm1` | 173.0 → 17.5 | 156.0 | 1.5 → 2.0 | 169.5 |
| → `mp/q4dm2` | 325.5 → 62.5 | 262.0 | 19.5 → 20.5 | 329.5 |

The stock share falls 81–92%. The candidate keeps 8 stock GUIs from the
title, 49 then 46 in the SP chain, and 29 on the second MP change. Setup/purge
paired differences are only 0–2 ms. Whole-change savings use the frame that
contains the change, including the wipe and autosave. For the longest of the
first 120 post-load frames, paired median differences range from −0.5 to
+2.5 ms; the first MP map moves from a 20 ms median to 22 ms. Every SP change
and each second MP change has zero non-precached warnings. Both clients emit
the same 68 existing warnings on the first MP map (player/entity/effect
definitions and menu/weapon media); names and multiplicities match in every
pair.

Two interleaved pairs of hand-offs after the second SP map change, longest
frame over each step:

| Step | Before | After |
| --- | ---: | ---: |
| Pause | 87, 81 ms | 76, 80 ms |
| SAVE GAME | 38, 32 ms | 28, 29 ms |
| LOAD GAME | 29, 27 ms | 29, 26 ms |
| RESTART LEVEL | 22, 25 ms | 23, 19 ms |
| Resume | 18, 18 ms | 18, 18 ms |
| Death | 104, 105 ms | 104, 104 ms |
| Death screen's LOAD GAME | 152, 151 ms | 147, 143 ms |

All steps complete and the engine screenshots show the expected pages.
`fs_debug 1` records identical file-read counters in every pair: no new late
image reads. These direct-to-map probes skip title warm-up, so both builds
still open 300 images (123 distinct paths) during the hand-offs, including
existing deferred levelshots, objective and player-preview textures. Both
also produce the same 27 precache warnings after the first load. Those
baseline reads/warnings remain; this change does not claim to remove them.

A separate stock-only pair (`ui_retained 0`) saves on airdefense1, changes to
airdefense2, displays reused monitor GUIs, opens Pause/Save/Load, and restores
the saved airdefense1 while Load Game is open. Pause and Save then reopen
correctly, with no unreachable actions or errors and identical precache
warnings. Engine captures confirm the menu defaults and reused monitor bank.
This caught an earlier prototype retaining its open Load subpage; restoring
the authored window snapshot fixes it without rebuilding the tree.

The fast menu gate, all 51 related script checks (including the new compiled
stock-cache regression), and all 160 native checks pass. The full suites
passed after the authored-state reset fix; the final dictionary-default
adjustment also passed its targeted script checks and the full native suite.
The regression exercises byte changes, failed include candidates, scoped
observation, authored-state reset, purge re-referencing and the real
window/scroller traversal.
Evidence is under `.tmp/mapchange/`: `stock-defaults-{sp,mp}-report.txt`,
`stock-defaults-warning-check.json`, `stock-defaults-handoff-comparison.json`,
`stock-defaults-identity.log`, `stock-defaults-before-identity.log`,
`stock-defaults-screen-comparison.json`, `stock-defaults-gate.log`,
`stock-defaults-scripts.log`, `stock-final-native.log`, `test-summary.json`,
and `runs/stock-defaults-*` (logs and engine captures).
The fixed independent runtimes are `.tmp/runtime/mc-stock-{before,after}/`.

## Document loads and level prefetch

15 October 2026. The retained documents are large JSON files: the shipped
set is about 100 MB of source, and a level load parses 15 to 25 MB of it
(the Remastered HUD, the weapon wheel, the pause pages and, in multiplayer,
the cards and the quick chat). A new `--load` mode of
`openq4-retained-ui-benchmark` splits one document's load into the JSON
parse alone, `Document::Load` (parse, validation and index), the derived
markup and the runtime's view, with the private memory each phase retains:

```powershell
& builddir-perf/openq4-retained-ui-benchmark.exe --load content/baseoq4/pak0/guis/hud/remastered_marine.q4ui 3
```

It found three costs with one cause: the loader kept and walked more than it
needed.

- **The parsed tree was retained.** Every loaded `Document` kept its
  JsonCpp tree beside the source and the model, for the editor's pointer
  edits, and a GUI and its view shared one. A tree is 10 to 20 times its
  source: 13.8 MB for the title, 47.5 MB for Join Game, 70.5 MB for the
  Escape card, 72.4 MB for the Marine HUD. A session at the title held
  about 135 MB of trees for its eight pages, and a level 100 to 200 MB
  more. The tree is now a load-time intermediate; an edit parses the
  retained source again to find the span it replaces. `document_kb` in the
  benchmark reports source plus model: 4.8 MB for the title, 33 MB for the
  Escape card.
- **Validation cost more than parsing.** Stack samples of the loader (cdb,
  `.tmp/uiperf/sample.ps1`) put a third of `Document::Load` in the parse and
  most of the rest in the validator, in allocations: a pointer string built
  for every checked field, member-name vectors for every object, vectors of
  nodes, paths, commands and keys grown one push at a time (MSVC's `std::map`
  move is not `noexcept`, so each growth copied every node below), and the
  properties of every node resolved through a second map. The hot readers
  now name a value by its parent's pointer and a leaf and build the pointer
  only on failure, read member names in place, reserve every vector from
  its array's size, resolve shorthands without copying, and read a path
  command's operation and points without string copies. Diagnostics are
  unchanged: the native document suites compare pointers and messages.
- **JsonCpp decoded every fraction through a string stream.** Its
  `decodeDouble` copies each token into a `std::string` and reads it back
  through an `istringstream`, with the locale machinery that implies, for
  every number that is not a plain integer; vector artwork is mostly such
  numbers. `subprojects/packagefiles/jsoncpp/fast-double.patch` decodes a
  token with `std::from_chars` where the standard library offers it (MSVC
  and libstdc++ 11; libc++ gained it in LLVM 20), and leaves any token it
  does not accept whole, or that is out of range, to the original path, so
  overflow, underflow and subnormal handling are as before. The wrap lists
  it after `subnormal-numbers.patch`; an existing checkout refreshes the
  generated subproject with `meson_setup.ps1 subprojects update --reset jsoncpp`.

Interleaved A/B of the committed loader (with the tree already dropped) and
this one, the same machine and build type as above, minimum of two runs of
five loads each:

| Document | Parse before | Parse after | Load before | Load after |
| --- | ---: | ---: | ---: | ---: |
| `title.q4ui` | 31.7 ms | 22.9 ms | 73.9 ms | 59.1 ms |
| `createserver.q4ui` | 62.6 ms | 42.1 ms | 155.5 ms | 107.9 ms |
| `joingame.q4ui` | 87.5 ms | 67.2 ms | 216.9 ms | 169.6 ms |
| `mp_escape.q4ui` | 122.6 ms | 107.7 ms | 449.8 ms | 295.2 ms |
| `remastered_marine.q4ui` | 155.6 ms | 94.1 ms | 332.7 ms | 223.5 ms |
| `remastered_mp.q4ui` | 96.7 ms | 71.9 ms | 235.6 ms | 192.7 ms |
| `weapon_wheel_marine.q4ui` | 47.8 ms | 32.2 ms | 107.5 ms | 79.3 ms |
| `artwork.q4ui` | 75.4 ms | 55.0 ms | 159.4 ms | 132.0 ms |

Every load gains, on the workers that parse the title's pages at startup as
on the main thread; the view's cost (markup, elements, bindings) is
unchanged and remains the larger share for the control-heavy pages.

### Gameplay documents

The Remastered HUD's document loaded on the first gameplay frame after a
level load, twice over: `idRemasteredHud` parsed the source once to read its
state declarations and extension data, then `FindGui` parsed it again to
load it. The weapon wheel loaded on its first opening, the quick chat on
its first, each a document of 4 to 6 MB. On a debugoptimized build the
Marine HUD cost about 0.8 s on the frame after the loading screen, the
wheel's first opening about 150 ms, and in multiplayer the Marine and
multiplayer HUDs together about 1.5 s on the first frame.

- The stock HUD's `InitFromFile` now creates its Remastered peer and loads
  the document with it, inside the level load; the peer reads its state
  types from the loaded model (`UI_RetainedDocumentModel`) and the
  generator's join events and readbacks from `DocumentModel::extensions`,
  the root extension object kept as compact JSON, so the source parses
  once. A save restore keeps the load for the first draw, outside its
  serialized stream, and the join of the stock dictionary still happens on
  that first draw.
- The game loads the local player's weapon wheel, and in multiplayer the
  quick chat, as the player spawns (`PrecacheWeaponWheelGui`,
  `PrecacheQuickChatGui`): inside the load in single player and on a listen
  server; a client joining a server spawns after its load and loads them
  then, from parses already made.
- As a map change begins, `idSessionLocal::PrefetchRetainedLevel` queues the
  level's documents on the job system's workers through the same prefetch
  the startup uses: the pause pages `PrepareRetainedLevel` loads, and the
  gameplay documents the game will ask for
  (`UI_RetainedPrefetchGameplayDocuments`): in single player the Remastered
  HUDs and weapon wheels of both families, since the level's player decides
  the family, with the objectives and vehicle HUDs; in multiplayer the
  multiplayer and Marine HUDs, the scoreboard, the wheel and the quick
  chat; under `q4xbase` The Awakening's own HUDs and wheels. A stock HUD's
  load preloads only a document the prefetch expects
  (`UI_RetainedPrefetched`), since single player precaches the scoreboard
  and never draws it. Once the single-player spawn has taken its family's
  documents the other family's parses are dropped; a map unload drops the
  rest.

Engine probes (`.tmp/uiperf/probe.py`: a hidden 1280x720 OpenGL window,
console verbs only, `framePacingSnapshot` after a two-second settle, then
the wheel's and the quick chat's first opening), the build before this work
with pristine game modules against this one, two to four rounds each,
debugoptimized, longest frame of each window:

| Window | Before | After |
| --- | ---: | ---: |
| Air Defense 1, the two seconds after the load | 624-651 ms | 95-97 ms |
| Air Defense 1, the weapon wheel's first opening | 148-178 ms | 46-51 ms |
| Q4DM1 listen server, the weapon wheel's first opening | 231-278 ms | 44-66 ms |
| Q4DM1 listen server, the quick chat's first opening | 132-193 ms | 29-44 ms |

The documents now load inside the level load, so the loading screen lasts
as long as those loads take there; Air Defense 1 measured 6.6-7.5 s before
and 6.9-8.1 s after on a shared machine, Q4DM1 1.4-1.9 s before and
1.65-1.8 s after. Neither run logged a late (non-precached) load, and the
pause pages' loads inside the level fell from about 60 to 10 ms each with
their parses prefetched.

### The view build and the remaining validation

15 October 2026, continued. With the parse and the validator faster, the
runtime's view of a control-heavy page (`Runtime::LoadDocument(const
Document&)`: the derived markup, RmlUi's parse of it, the configuration of
every element, the motion, state and view initializers and the first
presentation pass) was the larger half of its load: about 170 ms of the
Escape card's 450, 75 of the MP HUD's 310. Stack samples of the benchmark's
view phase (the same sampler) put a quarter of it in the first presentation
pass, a quarter in `Motion::Reset`, a quarter in RmlUi's parse of the
markup and the rest in the value-control view and the state; the validator's
samples were in reading nodes and timelines (a map lookup for each field of
each small object), the walk of the tree for non-finite numbers, and
copying and destroying its check of the initial state. Most of it was
copying or parsing again what the load already had.

- **The first presentation pass parsed every property twice.** The derived
  markup carries each authored value as an inline style, which RmlUi parses
  as it instances the element; the first `ApplyMotion` then formatted the
  same value again, set it again through RmlUi's property parser, and
  recorded the CSS in a map of 34 000 string-pair keys. The pass now reads
  the authored values in step with the live ones (the two maps share one
  key order) and, while the markup is fresh (until the first complete
  pass), records a value still at its authored one as applied without a
  second parse. Text, images and their coordinates, backdrops, scaled
  typography, a replayed opacity, the choice popups and viewports the
  value-control view styles at load, and every bound or animated value take
  the ordinary path. The applied CSS lives with each input's record instead
  of the map, so no pass searches it, and the load sets each canonical
  element's `z-index` as a typed property.
- **`Motion::Reset` built the authored values twice.** Every (node,
  property) value went into a map keyed by two strings in document order,
  each insert a search through tens of thousands of keys sharing long
  prefixes, and the whole map was then copied for the live values. The
  nodes are taken in ID order now, so every insert appends, and the live
  values share the authored map until the first write copies it (the
  copy-on-write the transactional copies already used): a page that never
  animates never copies it, and one that does copies once, on its first
  sample. The timelines are referenced in the shared model, not copied.
- **The state, the validator's state and the views copied the model.**
  `State::Reset` copied the declarations, bindings, presentation variables
  and every value control with its choice options, twice per load (the
  validator's check of the initial state, then the runtime's), and the
  value and number views copied every node's property map. A `Document` now
  shares its model (`SharedModel`, with the document's lifetime) and the
  state, motion and views reference it; the validator reads into that same
  model, so its initial-state check shares it too, and its state copies are
  neither made nor destroyed. The document also keeps that validated initial
  state (`InitialState`, shared by its copies), and the runtime starts from a
  copy of it instead of evaluating every binding a second time.
- **The validator looked each field up.** A typed value or a path command
  found each of its two to four fields through a map lookup and walked its
  members again for unknown fields; one walk finds them now
  (`FindMembers`), the timeline reader keeps each key's lookups, and the
  node reader keeps no intermediate property map. A control's five state
  timelines were each found by a search of every timeline (3.5 million
  string comparisons for the Escape card's 366 controls and 1 925
  timelines); the timelines are indexed once they are read. The derived
  markup is appended piece by piece instead of through a temporary per
  property. The tree is walked for
  non-finite numbers and invalid strings only when the lexical scan before
  the parse saw a token that could produce one (a number whose integer
  digits and exponent reach 309, or a NUL escape); every other value of a
  source that scan and the UTF-8 check accept is finite and valid, so the
  walk is skipped. The same checks run in the same order and report the
  same pointers: the native document suites compare them.

Interleaved A/B of the committed loader (3c0f4507a) and this one, the same
machine and build type, minimum of six runs of five loads each. The machine
was busy with other repositories' builds all afternoon (40 to 100 percent
load), so each run was given high process priority
(`.tmp/uiperf/ab_view_high.ps1`): the parse, which is unchanged, then agreed
within 2 percent between the binaries for every document but the Escape
card (8 percent), where runs at normal priority had swung by half and were
discarded.

| Document | Load before | Load after | View before | View after | View MB before | View MB after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `title.q4ui` | 50.0 ms | 43.4 ms | 16.5 ms | 9.6 ms | 10.9 | 6.3 |
| `createserver.q4ui` | 106.9 ms | 89.1 ms | 45.3 ms | 26.4 ms | 27.1 | 18.4 |
| `joingame.q4ui` | 161.5 ms | 149.6 ms | 60.2 ms | 32.9 ms | 37.4 | 21.1 |
| `mp_escape.q4ui` | 302.9 ms | 242.3 ms | 200.9 ms | 111.5 ms | 103.0 | 66.6 |
| `mp_summary.q4ui` | 149.9 ms | 126.8 ms | 89.9 ms | 49.6 ms | 53.5 | 32.9 |
| `remastered_marine.q4ui` | 214.6 ms | 183.9 ms | 40.1 ms | 22.5 ms | 26.5 | 18.5 |
| `remastered_mp.q4ui` | 170.6 ms | 150.9 ms | 76.1 ms | 39.5 ms | 44.9 | 26.2 |
| `remastered_scoreboard.q4ui` | 123.2 ms | 111.0 ms | 74.4 ms | 40.6 ms | 45.0 | 27.8 |
| `weapon_wheel_marine.q4ui` | 78.4 ms | 68.6 ms | 12.7 ms | 7.9 ms | 9.2 | 6.0 |

The view builds 38 to 48 percent faster and retains 30 to 44 percent less
memory (the shared model instead of its copies). `Document::Load` is 7 to 20
percent faster with the parse unchanged, so the validation and index it
keeps are 12 to 28 percent faster. A page whose timelines play still copies
the authored values once, on its first animated frame, where the load used
to copy them.

What remained of the view was mostly RmlUi's: its parse of the markup, the
instancing of every element and the inline style parse of every property,
then the first pass's own records and index, the authored map `Motion::Reset`
still builds and the markup string itself. The validator's remaining cost
is the parse (JsonCpp's tree, unchanged here) and the node and timeline
readers' lookups.

#### Elements built from the model

15 October 2026, later. Stack samples of the view after the change above put
its largest piece in `Runtime::LoadMarkup`: RmlUi parsed the 800 KB of
derived markup (`Document::BuildMarkup`), instanced every `q4-node` and
`q4-vector` through the element instancer, parsed each element's inline
`style` attribute property by property, and updated the document twice
(once in `Context::LoadDocument`, once in `ElementDocument::Show`). The view
now builds its elements from the loaded model and no markup is derived or
parsed for a canonical document (`Document::BuildMarkup` and
`Runtime::LoadMarkup` remain for the raw-RML tests):

- **Typed inline properties.** Each element is created with
  `Rml::Factory::InstanceElement` and appended to its parent before its
  children, in the model's node order, as the parser appended them. It gets
  the same id and the structural styles the markup wrote (`opacity:1`,
  `position:relative` for controls, `mask-image:q4-mask(alpha)`, the backdrop
  filter, the image decorator), and each authored property as a typed
  `Rml::Property` that is what RmlUi's parser produces from the value's CSS:
  the parser reads one representative declaration per property name and
  kind (a number, a length per unit, a colour, a font, a transform) and each
  keyword once, and the load keeps that parse's definition and parser index
  (`InlineProperties`, with RmlUi's specification). A number is the float
  RmlUi's `strtof` reads from the digits `Value::Css` writes (an integer below
  2^24 converts exactly either way; any other value is formatted and read
  once per distinct value, since a direct double-to-float cast can differ by
  an ulp), a colour's channels are the `round(x*255)` integers RmlUi's
  `rgba()` reads, a font is a string property, and a transform is the same
  translate, scale and rotate primitives built with the constructors the
  parser uses (the rotation converts degrees as the parser does). The
  shorthands (`overflow`, `padding`, `border-width`) and the effect
  properties (filters, decorators, the mask) still read their CSS through
  the parser, so their longhands and declaration lists are the parser's own,
  parsed once per distinct declaration. Each element's properties go through
  a dictionary filled in the order the style string declared them, so the
  element's inline-property map (and its serialization) takes the same
  order; the `style` attribute string itself is no longer retained.
- **One update.** The built document is shown once; the markup load's update
  of the hidden document before `Show` is gone. Nothing in the retained
  runtime observes the show or load events.
- **The first pass's records.** The pass recorded each value's key as two
  string copies, indexed every key in a hash map of string pairs, formatted
  every value's CSS to record it, looked each value's bound property up in
  the state's map and resolved each value's element from the id map. It now
  keys its records by pointers into the motion's authored map (the live
  values keep the authored key set for the document's life), finds a changed
  key's record by binary search over those keys, records a value the load
  built as applied without formatting it (its record formats on demand, when
  a later value compares against it or the replayed-opacity path keeps it),
  and reads the bound properties in step with the values. Property names are
  told apart by their first letter before any comparison, in the pass, the
  build and `VectorElement::Configure`.

The build was checked against the markup it replaces: a development hook
loaded the derived markup beside the built document for every retained
document in the tree (63) and compared the two trees, element by element:
tags, ids, attributes, every inline property (unit, variant type and bits,
definition, parser index, specificity, source; transform primitives,
decorator and filter declarations), the properties' order, then boxes,
offsets, visibility and computed values after an update, and the documents'
serialization. All 63 matched exactly, with and without the pre-show update.

Interleaved A/B of the committed loader (8fb8cd3e4) and this one, the same
machine and build type, minimum of six runs of five loads each, every run at
high process priority (the machine was at 60 to 70 percent with other
repositories' builds); the parse, which is unchanged, agrees within 5
percent between the binaries for every document.

| Document | Load before | Load after | View before | View after | View MB before | View MB after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `title.q4ui` | 51.6 ms | 51.1 ms | 9.3 ms | 7.2 ms | 7.1 | 6.0 |
| `createserver.q4ui` | 105.5 ms | 106.1 ms | 25.6 ms | 18.6 ms | 18.6 | 16.3 |
| `joingame.q4ui` | 160.4 ms | 166.4 ms | 31.7 ms | 21.0 ms | 21.5 | 17.2 |
| `mp_escape.q4ui` | 258.7 ms | 246.7 ms | 108.5 ms | 68.1 ms | 65.4 | 53.8 |
| `mp_summary.q4ui` | 132.0 ms | 132.4 ms | 46.8 ms | 31.8 ms | 32.0 | 27.0 |
| `remastered_marine.q4ui` | 224.8 ms | 233.6 ms | 22.4 ms | 14.0 ms | 17.6 | 14.9 |
| `remastered_mp.q4ui` | 186.1 ms | 186.1 ms | 41.8 ms | 29.3 ms | 27.1 | 23.9 |
| `remastered_scoreboard.q4ui` | 123.6 ms | 130.4 ms | 43.8 ms | 28.8 ms | 27.9 | 24.1 |
| `weapon_wheel_marine.q4ui` | 87.1 ms | 91.3 ms | 8.2 ms | 6.1 ms | 6.8 | 6.2 |

The view builds 23 to 38 percent faster (the Escape card's from 108 to 68
ms, the Match Summary's from 47 to 32, the Join Game page's from 32 to 21,
the MP HUD's from 42 to 29, the Marine HUD's from 22 to 14) and retains 10
to 20 percent less memory (the markup and the style attributes are gone).
The document load is unchanged. The benchmark's markup column still times
`Document::BuildMarkup`, which the view no longer calls.

What remains of the view: RmlUi's element construction and the inline
property inserts (about a fifth), the one update that lays the document out
(a tenth), `Motion::Reset`'s authored map (the key strings and nodes of a
`std::map` of string pairs, a twentieth), `VectorElement::Configure`'s copies
of each node's paths, the control feedback timelines' first play (which
copies the live values once), and the value-control view's and the
interaction's initialization.

## Current-build measurements and preset/scale reproduction, 22 October 2026

The staged Windows debugoptimized client at `1e259bfc.dirty`, SHA-256
`6c08a406a1856c25f33c4c9490a7515f494d8bc5823d15e1a4e37350d28e5444`,
ran sequentially on an i7-13700H / RTX 4060 Laptop GPU (NVIDIA 591.74).
Evidence is under `.tmp/outstanding/ui/`; `summary.json` binds each run's
command, log, engine screenshot hashes and root profiles. The source changes
after that commit are multiplayer bot/replay changes, not UI changes.
All runs used a hidden, windowed 1280x720 client, installed stock assets,
disabled host input and the engine's `screenshot` command. Vulkan rendered
offscreen without presentation. These are host CPU observations, not display
latency or GPU timings.

Vulkan retained title, System and in-game pause each have five separate
1,000-frame warm samples. The table gives the range across all five, including
the slower first System sample; it does not select the best repeat.

| View | Root CPU p50 ms | Root CPU p95 ms | Root CPU p99 ms | Whole-frame interval p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Title, native scene scale | 1.251–1.431 | 3.900–4.208 | 4.761–5.585 | 30.308–30.711 |
| System, native scene scale | 1.096–1.408 | 1.880–5.017 | 3.110–8.211 | 17.906–24.296 |
| SP pause plus HUD, 50% scene scale | 0.198–0.357 | 0.463–1.465 | 0.808–1.900 | 17.982–29.725 |

Title/System report one root view; pause reports four, including HUD work.
All samples report zero backdrop fallbacks. Against UI-07's inherited proposal
at 60 Hz (p95 1.667 ms, p99 3.333 ms), title and System do not pass. Pause is
within those CPU limits in this configuration only. These measurements do not
close GPU, cold/restart/resize timing, 1,000-cycle resource bounds, higher
refresh rates, all UI families or other-platform requirements.

The bounded reproduction matrix for issues #168/#169 covers GL and Vulkan,
`ui_retained` 0 and 1, title and SP `game/airdefense1` pause at 100%/50%
`r_screenFraction`, and four alternating `minimum`/`ultra` applications with
`vid_restart` while System is open. All four runs exit normally and produce
ten engine captures each. Inspected title/pause text remains readable and
uncropped; the six static System images per run (native, half scale and four
restarts) are byte-identical across both APIs. The System route explicitly
uses `ui_retainedSystem 1` in all runs, so this does not qualify the legacy
System implementation. The earlier scaled-scene contract also passes its
frontend/OpenGL/game checks and rejects nine mutations.

No disappearing-text or scaled/cropped-menu symptom was reproduced in this
matrix. It is bounded evidence, not closure of the reported intermittent
failure on different hardware. A separate discrepancy is visible: console
`applyPerformancePreset` updates the renderer settings, but the already-open
System page continues to display its old `Balanced` selection. Its binding
refresh and the full interactive apply flow remain separate work under UI-01.

## Remaining requirements

Fractional motion and cold compilation still need optimized-build profiling,
and long documents need resource/performance stress tests. Geometry preparation
for opacity, retained clipping and engine vertex conversion can be improved
further, and the engine GUI path still copies every retained vertex each
frame. GPU timings, frame pacing at the supported refresh rates, the complete
density/aspect matrix, all GUI families and other platforms remain unqualified.
Composition/masks, complete paints/strokes/fonts, the visual editor and all
current GUI replacements remain separate required work.

## Airdefense maximum-quality reference, 27 October 2026

User-qualified target: fullscreen native 2560 x 1440, starting position in
`game/airdefense1`, rotating camera, new Marine HUD, all supported graphics
quality maxima, and **every measured frame <=6.67 ms**. A mean or percentile
pass is insufficient. The revised Ultra preset defines the quality workload;
SMAA Ultra is the highest stable post-AA quality, while color-edge mode is a
prototype. MSAA requests 16 and the current Vulkan backend/device uses 8.
Native render scale stays 100%; diagnostic/experimental modes and artistic
strength controls are not numeric quality levels.

Reference host: i7-13700H, RTX 4060 Laptop GPU, hybrid display path, 240 Hz panel.
Build: standard `builddir`, debugoptimized/O2, staged into `.install` with matching
modules. The engine's focus-loss minimization fallback now honors SDL's explicit
hint; the test sets that hint to keep the fullscreen surface visible without
injecting input or changing OS focus. Default minimize behavior is preserved.

`tools/tests/airdefense_performance.py` warms 1,000 HUD frames and a full 360-degree
rotation, then requires a complete 720-degree rotation inside a 27-second window.
It records actual CVar readbacks, native framebuffer/MSAA/backend evidence,
starting coordinates, full-window frame rate, maximum interval and an engine
`screenshot`. The gameplay harness now computes the average from the entire
recorded window and has an optional maximum-frame gate. The executable path is
stable and all writable state is isolated under `.tmp/airdefense-perf`.

First complete rotation with revised-Ultra CVar readbacks:

| Measure | Observed |
| --- | --- |
| Samples / interval | 1,694 / 27.014 seconds |
| Whole-window mean | 15.946622 ms / 62.709206 FPS |
| p95 / p99 / maximum | 21.90 / 22.95 / 24.08 ms |
| Retained HUD CPU mean | 0.032756 ms update + 0.079163 ms render |
| Retained HUD CPU p95 / p99 | 0.1951 / 0.3094 ms |
| Draws / submitted vertices | 19 / 10,348 per frame |

Evidence: `.tmp/airdefense-perf/qualification-ultra-vk-corrected/qualification.json`
and its underlying report, log and engine capture. The settings/display/rotation
checks passed, with no missing-shadow warning; every performance gate failed.
Further GPU profiling found portal-sky views invalidated main-camera motion-blur
history, so that enabled effect was not drawing. This run therefore also fails
the stronger functional workload gate now requiring actual SSAO, motion-blur
and bloom draw evidence before sampling. Both renderer paths now preserve main
history across unrelated subviews; world/time/projection/restart validation
still rejects discontinuities. The Vulkan diagnostic after this fix records
motion blur drawing with object vectors at approximately 1.1 ms GPU time.
This is a **failed reference**, not a 150 FPS claim. The earlier three Vulkan
runs averaging 330-339 FPS used MSAA off and stencil shadows, and remain
explicitly diagnostic only after the maximum-quality requirement.

Current changes reduce repeated particle-distance work with stable depth ties
and avoid copying opaque cached HUD raster quads. Neither is credited with an
unmeasured scene-level gain. `profileFrameWork` adds bounded phase timing and a
CSV after collection; its output frame can stall and is excluded from acceptance
runs. Further work is tracked under HF-07, REN-07 and UI-07 in the roadmap.

The subsequent preset audit added explicit object vectors, hashed cutouts,
translucent receivers and four stabilized cascades to Ultra. At 4096 per cascade,
the previous 8192 atlas could hold only one required ownership set in this scene.
Both backends and the shared planner now accept a 16384 atlas, bounded by device
limits. Ultra requests that capacity; the diagnostic rotation retains both LOCAL
and GLOBAL sets without the earlier missing-resource warning. The older table
also predates this cascade workload and is not its performance qualification.

Vulkan diagnostics (`max-phases-full-vk`) now attribute approximately 13.9 ms to
lighting/shadows, 1.2 ms to the depth prepass, 2.3 ms to SSAO drawing, 0.6 ms to
its final-depth capture, 1.1 ms to motion blur and 1.7 ms to bloom/tone mapping.
These are GPU phase measurements, not acceptance frame intervals. SSAO's scalar
factor can blend into existing samples without copying scene color; paired
rotations measured about 0.6 ms saved, with unchanged sample count/resolution.
The reference composition remains available for image comparisons.

Complete revised-Ultra qualification after the cascade/history fixes:

| Backend | Frames / window | Mean FPS | p95 / p99 / maximum ms |
| --- | --- | --- | --- |
| Vulkan | 1,039 / 27.013 s | 38.463512 | 31.70 / 33.45 / 37.55 |
| OpenGL | 834 / 27.009 s | 30.879080 | 45.95 / 51.20 / 69.85 |

Both runs completed the rotations, kept native fullscreen and the new HUD,
reported the requested quality values, drew SSAO/motion blur/bloom, and emitted
no missing-shadow warnings. Both fail every performance gate. OpenGL also falls
below the harness's 1,000-frame qualification minimum despite completing the
time window. Evidence and engine captures are retained under
`.tmp/airdefense-perf/qualification-complete-ultra-{vk,gl}`. The strict 150 FPS
goal remains open. Skipping shadow samples for exactly zero light contribution
showed no repeatable gain in alternating runs, so that experiment was removed.

Vulkan now bounds the projected-shadow clear to fresh ownership blocks instead
of clearing unused atlas space. Every allocated cascade retains its size,
contents and filtering; cache-hit blocks are restored after the fresh pass.
Paired fullscreen 1440p rotations (`max-shadowclear-vk`) measured means of
25.662/25.624 ms with the full-atlas reference and 23.320/23.718 ms with the
bounded clear. A frozen-scene capture pair is byte-identical (zero changed RGB
pixels). `r_vkShadowFullAtlasClear 1` retains the diagnostic reference; this is
not a graphics-quality option. The Vulkan shadow compatibility contract passes.
The subsequent strict qualification (`qualification-shadow-clear-vk`) records
1,130 frames in 27.005 seconds: mean 23.897799 ms / 41.844858 FPS,
p95 29.05 ms, p99 30.55 ms, maximum 33.87 ms. Workload/display/quality checks pass;
all frame-rate gates still fail. The reference before this change was 38.46 FPS,
but the paired runs above are the narrower evidence for its contribution.

Two subsequent shadow experiments were rejected and removed. Preferring D32
float over D24/stencil increased mean frame time from 23.799 to 24.817 ms
(`max-shadowformat-{packed,float}-a-vk`). Conservative raster-depth bounds for
parallel and then general projected casters preserved frozen RGB captures
exactly, but did not improve paired rotations (`max-shadowdepth-vk` and
`max-shadowdepth-bounded-vk`). The latter measured reference means
23.550/23.665 ms versus candidate 23.648/23.756 ms. No shader, pipeline or depth
format changes from these experiments remain in the implementation.

Post-processing now leaves unused scene depth in attachment layout while
rendering color-only intermediates. Motion-vector passes still transition it
when they sample the actual scene depth. Paired rotations (`max-postdepth-vk`)
measured reference means 23.517/23.541 ms versus 23.275/23.424 ms, with identical
frozen RGB captures. `r_vkPostDepthTransitionReference 1` restores the old behavior.

Matching-format MSAA captures in GL row order now resolve their requested
rectangle directly into the destination, avoiding a full-source scratch resolve
and an identity blit. Flipped or format-converting captures retain the scratch
path. `max-copyresolve-vk` alternates the reference and optimized paths: means
23.203/23.293 ms versus 22.309/22.468 ms. Frozen RGB captures are byte-identical.
`r_vkCopyResolveScratchReference 1` retains the diagnostic reference. These are
paired local observations, not achievement of the strict frame-time floor.
The strict `qualification-copy-resolve-vk` run records 1,196 frames in 27.021 s:
22.592475 ms mean / 44.262525 FPS, p95 27.85 ms, p99 29.55 ms and maximum
36.84 ms. Workload/display/quality checks pass; all frame-rate gates still fail.
The render-target and HDR suites pass with Vulkan validation enabled
(`copy-capture-validation`). Added GPU readback fixtures cover RGBA8/FP16,
2x/4x/8x fractional coverage, both image origins, full/interior/clamped rectangles
and reuse of the original multisampled source.
`max-validation-vk` also completes the actual maximum-quality fullscreen map
rotation with the validation layer enabled and no validation warnings/errors;
SSAO, bloom/tone mapping, motion vectors/blur and shadowed interactions draw.
Its instrumented timing is diagnostic, not the qualification result above.

The temporal-motion suite initially passed before restart and then timed out on
both reference and optimized paths. A non-invasive debugger stack found a crash
behind the engine's error dialog: Vulkan's separate backend state retained a view
pointer into the engine's freed frame memory. Device teardown now clears that
state before console GPU work can reuse it. `temporal-restart-fixed` passes all
36 temporal fixtures before and after restart, plus the probe-source lifecycle
suite across partial/full restart and MSAA changes, with Vulkan validation enabled.
Earlier failing logs and the debugger stack remain under `temporal-restart-debug`
and `temporal-restart-stack.log` for provenance.

Fresh projected ownership maps now reuse opaque global-caster depth: LOCAL renders
the global chains, then GLOBAL copies that depth and adds its local chains.
Resolution, cascades, filtering and the receiver ownership split are unchanged.
Cached/composed passes, perforated global casters and the sealed executor retain
their original paths. With the subsequent depth-union path disabled,
`r_vkShadowOwnershipCopy 0` selects the original drawing reference.
The in-atlas copy uses GENERAL layout between attachment scopes and resumes with
LOAD, preserving the already-cleared fresh blocks and existing cache transfers.

`max-ownership-extended-vk/comparison.json` contains five alternating native
fullscreen reference/candidate rotations. Mean frame times average 22.522 versus
22.140 ms (0.382 ms saved); reference p99 ranges 28.70–29.60 ms and candidate
p99 28.60–29.25 ms. The two initial pairs in `max-ownership-vk` also improve.
Frozen captures include exact RGB matches. A rotated pair in
`max-ownership-views-vk` differs at 2,722 pixels, with maximum channel MAE
0.000458/255; the other three pairs are identical. The repeated reference in
`max-ownership-views-control-vk` exposes residual temporal variation in one view,
so these captures are not a claim of bitwise determinism for all effects.
The extended timing run's additional angle-labelled captures stayed at the frozen
camera pose; only the separate views probes advance the camera between pairs.
The validation-layer map probes have no Vulkan warnings/errors. The shadow
compatibility contract passes. `shadow-resize-vk` completes 100 windowed cycles
between 4096/8192 atlases and 1024/2048 tiles: tracked resident shadow storage
returns to 160/352 MiB each cycle, without growth or validation errors.
The staged default-path qualification `qualification-ownership-vk` records 1,173
frames over 27.011 s: **43.426925 FPS** mean (23.027189 ms), p95 29.10 ms,
p99 31.05 ms and maximum **38.36 ms**. The native/fullscreen/maximum-quality
workload gates pass, but every performance gate fails. This unpaired run is slower
than the preceding 44.3 FPS qualification; the small paired saving is not evidence
of an improved worst-frame floor.

Fresh PCSS ownership maps now avoid the depth copy as well. GLOBAL stores its
local casters; each unfiltered receiver/blocker-search read takes the minimum
with the corresponding LOCAL global-caster depth. This preserves the exact
minimum-depth union without changing resolution, filter taps or caster coverage.
It is restricted to uncached/uncomposed projected passes with opaque global
casters and manual PCSS filtering. Hardware comparison, moment/debug views,
point lights and sealed executor paths keep the established representations.
`r_vkShadowOwnershipUnion 0` restores the copy path; also set
`r_vkShadowOwnershipCopy 0` for the original duplicated-caster reference.

`max-union-extended-vk/comparison.json` records five alternating native fullscreen
pairs: whole-window mean frame times average **23.627 versus 21.098 ms**, saving
**2.529 ms**. All five candidate p99 values improve (29.30–31.10 ms versus
32.75–34.00 ms), but the largest individual candidate frame is 37.36 ms versus
37.23 ms in the reference. This is not a strict-floor pass. The final frozen
capture is RGB-identical. In `max-union-views-vk`, reference/candidate images are
identical at four distinct views; the repeat-reference control has small temporal
variation at one view. Vulkan validation reports no warnings/errors.
`shadow-resize-union-vk` completes 100 windowed atlas/tile resize cycles, returning
to the same 160/352 MiB storage levels, with the union path active and no validation
errors. Shader-header consistency and shadow compatibility contracts pass.
The embedded SSAO shader was also regenerated to match the previously edited
source; its constant-white factor path no longer needs the white texture read.

Two preliminary scratch runs are explicitly rejected: `max-phases-ownership-vk`
used the unsupported `vk` CVar alias, ran OpenGL and copied a stale timing CSV;
that stale copy was removed and the probe now checks artifact freshness.
`max-union-vk` omitted shader-header regeneration and failed image parity; its
speed numbers do not qualify the union algorithm. The corrected diagnostic run,
`max-phases-ownership-vk-verified`, measures 23.709 ms mean GPU time before the
union change, including 12.424 ms in world lighting and 2.223 ms in SSAO drawing.
Its final pacing maximum includes the profiler CSV write and is not acceptance
evidence. The corrected union measurements above use regenerated shaders.

The staged default-on qualification `qualification-union-vk` records 1,354
frames over 26.995 s: **50.157245 FPS** mean (19.937299 ms), p95 25.00 ms,
p99 26.35 ms and maximum **38.72 ms**. Native fullscreen, rotation, HUD and all
maximum-quality/effect workload gates pass; the required performance gates fail.
This unpaired qualification improves average/p99 versus the previous run but not
the maximum frame time. The 150 FPS every-frame requirement remains open.

The zero-contribution receiver shortcut was tested again after the ownership
union change, then removed. Five alternating pairs in
`max-zerolight-extended-vk` average **21.268 versus 21.325 ms**, providing no gain.
Three frozen views match exactly; at 90 degrees the candidate matches the
repeated-reference control. The extended run's final capture differs without
such a control and cannot establish parity. No zero-light shader or CVar remains.

The optional Vulkan GPU phase trace now nests `worldShadowMaps` inside
`worldLights`, using the existing frame-slot query pool and retirement fence.
It introduces no extra wait and is inactive when profiling is disabled.
`max-phases-union-default-detail-vk/summary.json` records 1,024 native maximum-
quality frames: **19.876 ms** mean GPU time, **8.539 ms** world lighting, of
which **6.715 ms** is map production and **1.824 ms** is the remaining lighting
work. Map production peaks at 10.936 ms. SSAO drawing averages 2.253 ms, its
depth capture 0.584 ms, depth prepass 1.178 ms, motion blur 0.843 ms and bloom
1.395 ms. These are diagnostic spans, not qualification frame intervals; the
final CSV write stalls and must remain outside acceptance windows.
`max-phases-union-validation-vk/validation.json` passes with the validation layer
enabled: all 1,024 frames contain correctly nested map/light/whole-frame spans,
and no Vulkan validation warning or error is reported. GPU timing and shadow
compatibility contracts pass on the restored, staged build.

The scratch profiler had inherited `r_shadowMapStaticCache 0` from resize tests.
Its maximum-quality setup now explicitly restores the default cache settings
(`r_shadowMapStaticCache 1`, `r_shadowMapCacheCSM 0`) and reports them. The
default-cache capture above supersedes `max-phases-union-vk`, whose map-production
mean was 8.140 ms with caching disabled. Earlier paired scratch comparisons used
the same inherited value on both sides; their deltas are controlled comparisons,
but their absolute times do not describe the default configuration. Fresh strict
qualification profiles already used the correct cache defaults.

Point-cube ownership copying was also rejected and removed. Five alternating
native rotations in `max-pointcopy-extended-vk` average **20.141 ms** with the
original drawing and **21.991 ms** with six-face copies before local-caster
drawing. Every candidate pair is slower. Its validation-layer image probe has
no validation errors, but includes temporal image differences; no copy-path
quality or speed claim is retained.

Shadow depth passes now omit their unused stencil attachments by default.
The D24/stencil allocation, depth samples, caster geometry, filtering and
combined-aspect layout barriers remain unchanged. Both projected and point
casters disable stencil tests/writes, and receivers sample only depth, so those
passes need not clear, load or store stencil. Pipeline metadata follows the
attachment selection; both cached variants retire on shutdown/reconfiguration.
`r_vkShadowStencilAttachmentReference 1` restores the previous attachments for
comparison. It is a diagnostic reference, not a graphics quality setting.

Five alternating native maximum-quality pairs in `max-shadowstencil-extended-vk`
average **19.809 ms versus 18.589 ms**, saving **1.220 ms (6.16%)**. Every mean
improves. Four p99 values improve; the first changes from 28.20 to 28.30 ms.
The largest candidate frame is 37.98 ms versus 35.47 ms in the reference, so
this does not establish an improved worst-frame floor. Graphics settings and
the normal sound/camera workload are unchanged during those timing windows.

Frozen comparisons exposed audio-driven random camera shake that continues
while game time is stopped. `max-shadowstencil-views-stable-settled-vk` suppresses
that shake only for image comparisons and settles each view before capture.
Three distinct views match exactly; the fourth differs at 35 pixels by at most
one 8-bit intensity level. Earlier uncontrolled/repeat-reference variations are
retained separately, not treated as bitwise parity evidence. All image probes
run with Vulkan validation enabled and report no validation warnings/errors.
`shadow-resize-stencil-vk/validation.json` passes 100 windowed atlas/point-size
cycles with repeatable 70/262 MiB intermediate storage (280 MiB at the final
point size), followed by a video restart and resumed gameplay with the optimized
default still selected. GPU timing and shadow compatibility contracts pass.

The staged default-path qualification `qualification-shadow-stencil-vk` records
1,420 frames over 27.021 seconds: **52.551085 FPS** mean (19.029103 ms),
**23.85 ms p95**, **25.05 ms p99**, and **29.82 ms maximum**. All native-display,
rotation, maximum-quality/HUD/effect workload checks pass; every performance gate
fails. This unpaired run is better than the previous 49.95 FPS / 33.04 ms maximum,
but the paired measurements above are the evidence for the optimization's
contribution. The 150 FPS every-frame requirement remains open.

The optional GPU trace now also separates the main and portal-sky views,
ambient walks, HDR composition, fog, authored post materials, native SMAA draws
and the first render-target resolve. Each phase has one frame-owned query pair;
subsequent occurrences are ignored, and no query is emitted while profiling is
disabled. The main-view interval contains its depth/light/post intervals; the
portal interval is separate. Mask capacity is checked at compile time.

`max-phases-views-vk/summary.json` records 1,024 default-cache, native maximum-
quality frames after the stencil change: **18.317 ms** mean GPU time, including
**15.008 ms** for the main view, **2.269 ms** for portal-sky rendering and
**0.238 ms** for the target resolve. Main-view map production averages
**5.275 ms** and SSAO drawing **2.188 ms**. SMAA's three draws total **0.243 ms**;
HDR composition and fog each take less than 0.001 ms here because their heavy
paths are inactive. A further **0.559 ms** lies outside the view/resolve/SMAA
intervals. This is diagnostic attribution, not a new strict qualification.
`max-phases-views-validation-vk/validation.json` passes all 1,024 expected masks,
nested/disjoint interval checks and Vulkan validation with no warnings/errors.
GPU timing contracts and the complete embedded shader-header check pass.

Precomputing SSAO's spiral offsets was tested and removed. Five alternating
pairs in `max-ssaospiral-extended-vk/comparison.json` average **16.554 ms** for
the original calculation and **16.620 ms** for the candidate. The frozen view
and repeat-reference control are RGB-identical, but the change provides no
useful speed gain. The original shader calculation and uniform layout remain.

A conservative portal-sky discard prototype was also tested and removed.
It certified opaque coverage from the following main view's triangles and used
up to eight optional [Vulkan discard rectangles](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_discard_rectangles.html). Report-only rotations found
8–20% screen coverage. An independent homogeneous barycentric oracle checked
1,060 random rectangle certificates; four frozen views and their repeat-reference
controls were RGB-identical, with Vulkan validation enabled and no errors.
However, five alternating maximum-quality pairs in
`max-portalocclusion-extended-vk/comparison.json` average **18.633 versus
18.593 ms**, a **0.21%** difference below the 1.67–4.39% p95 repeat spread.
The candidate's worst frame is 41.41 ms versus 35.88 ms for the reference.
Separate diagnostic traces measure portal rendering at 2.260 versus 2.163 ms;
they do not establish a whole-frame benefit. The optional device extension,
pipeline state, geometry helper and experimental CVar were all removed. The
isolated `rejected-prototype.patch` is retained with the evidence for reproducibility.

After removing that prototype and re-staging the build,
`qualification-portal-revert-vk/qualification.json` records **1,441 frames in
27.010 seconds**, averaging **53.351496 FPS** (18.743617 ms), with **24.35 ms
p95**, **26.40 ms p99** and a **32.79 ms maximum**. Native fullscreen, rotation,
maximum settings, retained HUD and effect workload checks pass. All performance
gates still fail. This unpaired restored-build run is the latest qualification;
its variation from the earlier 52.55 FPS run is not credited to the removed
experiment. GPU timing/shadow contracts and `git diff --check` pass.

The frame-owned GPU profiler now also separates fresh projected maps, projected
cache transfers, dynamic composition and point cubes. All four ranges remain
disabled outside the explicit diagnostic, use the existing frame retirement
fence, and exclude portal/subviews. `max-phases-shadowclasses-vk/validation.json`
passes 1,024 masks and nested map-production intervals with Vulkan validation
enabled. Point production appears in 329 frames. Mean projected production is
**4.400 ms**, point production **0.914 ms**, and combined map production
**5.323 ms**; no projected cache/compose work occurs in this rotation. The whole
GPU frame averages 18.372 ms. These diagnostic samples prioritize projected-map
work; they do not replace the strict qualification above.

A conservative bounded-clear prototype for fresh PCSS local-caster tiles was
rejected. It retained the 4096-square coordinate grid, derived per-cascade
rectangles from caster bounds, and substituted empty depth outside those
rectangles before the existing ownership union/filtering. The active rotating
path usually covered 56–94% of those tiles. Four frozen Vulkan views match their
repeat-reference controls exactly, with no validation messages; two views have
small one-level differences from their earlier reference that are also present
in the repeat control. Five alternating pairs in
`max-sparseunion-extended-vk/comparison.json` average **18.753 versus 18.792 ms**,
a **0.204% regression** within 2.31–4.58% p95 repeat spread. Four of five means
are slower. The shader/uniform changes, bounded-clear logic and experimental
CVar were removed; `rejected-prototype.patch` preserves the isolated experiment.
The initial prototype's non-power-of-two uniform slice triggered OpenGL startup
recovery. Those runs are explicitly invalid; the corrected comparisons confirm
Vulkan remained active. The scratch harness now rejects backend recovery or
missing final Vulkan readback before accepting artifacts. Native GPU timing and
shadow contracts pass after restoration.

Two projected-caster submission experiments were also rejected and removed.
Bounded front-to-back sorting (`max-shadoworder-pairs-vk`) averages **18.884746
versus 18.891235 ms**, with mean p99 **24.94/25.66 ms** over five pairs. Four
frozen views match their repeat-reference controls exactly with validation
active; one initial reference differs in 49 pixels, identically to its control.

Compatible opaque indirect batching (`max-shadowbatch-pairs-vk`) averages
**18.490569 versus 18.508991 ms**, with mean p99 **25.47/25.05 ms**. Three of
five means regress. It preserves draw order, complete geometry and identical
push constants/cull state, and falls back to direct draws for unsupported,
incompatible or full-buffer cases. Vulkan validation reports no errors. The
four-view capture probe has a one-RGB-level difference in 167 pixels at 270
degrees that is absent from both reference controls; the other candidate views
match their repeated controls exactly. No image-quality or speedup claim is
made for the removed experiment. The checked indirect API contract is documented
by [Khronos](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirect.html).

Both isolated prototype patches, commands, runtime identities and measurements
are retained in their respective pair directories. The submission/feature/buffer
changes and experimental CVars are removed. These results prioritize reducing
unneeded shadow-map coverage over caster ordering or command batching.

The subsequent receiver-coverage diagnostic projects conservative surface
bounds for all three receiver chains into each cascade, expands by the complete
PCSS kernel guard, and includes the preceding cascade's blend band. It only
reports coverage; no caster or atlas write changes. All 76 reports (19 per
cascade during the rotation) cover 100% of the tile, with zero uncertain bounds.
This rules out a useful simple rectangular crop from these bounds, rather than
proving that every individual shadow texel is needed. The diagnostic is removed;
its isolated patch, runtime identity, log and `coverage.json` remain under
`.tmp/airdefense-perf/max-phases-receiver-detail-vk`. Its GPU-profile CSV flush
is intrusive and the terminal pacing snapshot is not qualification evidence.

A fixed 32-call SSAO sequence also fails the whole-frame acceptance check.
It keeps the original sample positions, arithmetic and accumulation order,
exposing each index as a compiler constant while retaining the original loop
as a runtime reference. Five alternating rotations measure 18.868628 ms for
the reference and 18.980067 ms for the candidate (0.591% slower); mean p99 is
27.33/27.63 ms and the largest frames are 38.94/47.14 ms. Three individual
pairs improve, but the full retained set does not establish a gain. Frozen
captures have zero Vulkan validation messages but also show reference/control
drift, so they do not prove exact parity. The shader, switch and generated
header are restored. Evidence and the isolated rejected patch remain under
`.tmp/airdefense-perf/max-ssaounroll-{views,pairs}-vk`.

Read-only hardware telemetry on the restored build confirms that this remains
a GPU workload bottleneck. The diagnostic records 73 one-second samples;
all 55 samples with GPU utilization at least 99% report 100% utilization, P0,
2550 MHz graphics, 8001 MHz memory and zero active clock-event flags.
Temperature spans 55–63 C and power draw 57.24–77.15 W in that subset. The
power-limit field is unavailable, rather than zero. NVIDIA documents P0 and
the clock-event fields in its [nvidia-smi reference](https://docs.nvidia.com/deploy/nvidia-smi/index.html#performance-state).
The accompanying 1,024 GPU intervals average 18.378 ms overall, 4.415 ms for
projected maps, 0.918 ms for point maps, 2.195 ms for SSAO, 1.399 ms for bloom
and 0.829 ms for motion blur. These closely reproduce the previous phase
profile. One-second polling cannot exclude individual transient events; no
device settings were changed and this instrumented run is not qualification.
The runtime hashes, raw telemetry, phase CSV and summary are retained in
`.tmp/airdefense-perf/max-phases-hardware-vk`.

After restoring all three rejected code experiments and removing the temporary
coverage report, the build is staged again. Equivalent PNGs replace duplicate
engine TGA captures; patch files preserve each rejected change. Cleanup is
recorded in `.tmp/airdefense-perf/caster-investigation-cleanup.json`.

## Checkpoint save stalls, 27 October 2026

The user's checkpoint-stall request is tracked under REN-07 alongside the
fullscreen rendering goal. These save probes are **windowed**, request
2560×1440, and actually render at the desktop-constrained 2538×1312 client
extent. They use all Ultra quality options and the retained HUD. They do not
qualify the separate native-fullscreen frame-time floor.

The initial synchronous Vulkan saves spent 378–417 ms serializing, 75–127 ms
redrawing/readback for the thumbnail, and 522–620 ms in the complete save call.
The engine now serializes into a bounded memory file, keeping the current wire
layout and 512 MiB total-file limit. CRC processing uses sixteen-byte slices with
the same polynomial, incremental state and stored checksum. A native test
compares it to an independent bitwise oracle across unaligned/chunked inputs.

Checkpoints request a thumbnail from an ordinary presented frame. OpenGL polls
a pixel-buffer fence without waiting; Vulkan retires its copy behind the
existing frame-slot fence. Only the 320×240 result is converted/resampled on the
CPU. The native resampling test checks GL RGBA/bottom-up versus Vulkan
BGRA/top-down parity, including odd dimensions and single-pixel images. Render
API 26 appends the capture lifecycle. On unavailable/failed capture, the existing
transactional missing-preview path remains available; the snapshot is preserved.

For snapshots fitting the existing 16 MiB durable-file bound, the portable job
service builds the integrity trailer, persists the staged payload and description,
and verifies every byte against readback. Workers own copied bytes and exact native paths;
they do not access game state, the VFS, renderer, CVars or console. Larger saves,
disabled/deterministic workers and saturated queues retain the synchronous
storage fallback. After successful worker readback, main-thread header/footer/trailer
validation reads the verified immutable payload in memory and compares the trailer
with the worker's CRC, bound to that exact memory view and protected length. This
avoids a second disk read, stdio offset queries and hashing the snapshot again.
Loads, recovery and synchronous saves still calculate the complete CRC themselves;
the synchronous path reopens its staged file. Validation and the existing payload-last sidecar
transaction still gate publication. Load, delete, map teardown, another save
and shutdown drain a pending checkpoint. Game API 56 delivers the HUD saved
notification only after commit.

Representative whole-frame peaks around the three stationary checkpoint probes:

| Revision/backend | First checkpoint | Second | Third |
| --- | ---: | ---: | ---: |
| Original synchronous / Vulkan | 662.26 ms | 531.27 ms | 544.75 ms |
| Buffered, deferred capture/storage / Vulkan | 128.97 ms | 48.81 ms | 51.67 ms |
| Buffered, deferred capture/storage / OpenGL | 104.93 ms | 60.12 ms | 51.70 ms |

Evidence: `.tmp/airdefense-perf/checkpoint-baseline-vk`,
`checkpoint-background-vk` and `checkpoint-background-gl`. The new harness also
warms and runs camera rotation so motion effects are exercised before and during
saving; earlier stationary probes are retained as labelled diagnostic evidence.
`com_saveGameProfile 1` reports phase CPU work across the save's lifetime,
excluding time waiting between frames; it is not a single-frame duration.
Level 2 also reports per-class object time and bytes. The latter adds per-object
clock reads and console output and is diagnostic only.

The subsequent rotating probes in `checkpoint-final-vk` and `checkpoint-final-gl`
include batched dictionary serialization, cached immutable retained-UI document
identity, and background description persistence. Their three checkpoint-window
maxima are 38.23/35.80/39.21 ms on Vulkan and 82.62/55.84/46.06 ms on OpenGL.
These are windowed, all-quality-max measurements, not the fullscreen acceptance
run. Vulkan snapshot serialization still takes 22.6–26.6 ms. Description
preparation now takes 0.02–0.16 ms; its durable write runs on the worker.

The dictionary writer retains the existing little-endian length-prefixed bytes
and limits. `openq4-save-dict` exercises both SP/MP production writers against an
independent wire oracle, including null/empty dictionaries, the entry/string
limits, Unicode, buffer boundaries and write failure. The UI snapshot cache is
rebuilt on document reload; live state, motion and interaction are still captured
at save time. `openq4-retained-ui`, `openq4-crc32`, `openq4-file-memory` and
`openq4-capture-resample` pass, as does the durable-file fault suite
`openq4-ui-document-files`.

The next revision batches scalar/vector fields in a 16 KiB game-module buffer,
flushing before UI/sound serializers and successful close. The footer includes
pending bytes, and error unwinding discards the buffer rather than retrying it.
The wire test also exercises mixed scalars, large direct writes, buffer
boundaries and partial-write rejection. With the bounded shadow clear included,
`checkpoint-buffered-fields-vk` peaks at 34.87/33.44/29.89 ms; warm snapshots take
18.1–18.8 ms (23.5 ms for the first). Immediate load/quit/reopen and failed
description overwrite pass. These combined frame improvements are not attributed
solely to serialization: the shadow-clear optimization also reduces GPU work.
The corresponding `checkpoint-buffered-fields-gl` run passes immediate
load/quit/reopen and peaks at 81.17/57.48/45.57 ms. Both backend thumbnail headers
are 320×240 RGBA and the engine-written images were inspected. The disabled-job
fallback and obstructed description overwrite pass in `checkpoint-fallback-verified`;
the earlier `checkpoint-final-fallback` report falsely failed its warning matcher,
which had not yet accepted the synchronous description-write error text.

The subsequent verified-memory validation revision passes immediate load, quit,
reopen, preview checks and failed-payload overwrite preservation in
`checkpoint-verified-memory-vk`. Its validation phases are generally 4.3–4.7 ms,
versus 5.8–6.3 ms in the buffered-field reference; there are slower outliers.
Sixteen-byte CRC slicing further reduces an isolated 8 MiB checksum median from
3.602 to 2.899 ms (`crc-slicing{8,16}-bench.log`). The independent bitwise oracle
also checks 15/16/17-byte chunks and 31/32/33-byte lengths.
`checkpoint-crc16-vk` passes the lifecycle and failed-description-write cases,
with ordinary validation phases of 3.6–3.8 ms and outliers up to 5.8 ms.
Its checkpoint-window maxima are **64.14/31.67/33.89 ms**, so lower validation
CPU cost has not eliminated the first-save spike or established a better overall
frame-time floor. Warm serialization remains 19.1–19.5 ms. The native CRC,
memory-file, scalar/dictionary-wire, resampling, retained-UI and durable-file
fault suites all pass (`checkpoint-crc16-native.log`).
`checkpoint-crc16-fallback` also passes immediate load/quit/reopen and failed
description overwrite with the job scheduler disabled.
The follow-up `checkpoint-crc16-profile` passes reload and records 250 CPU frames.
Its three slowest frames are the checkpoint command frames: 40.283/34.123/32.098 ms,
with 32.644/28.299/26.705 ms in command processing and only 3.1–4.6 ms in drawing.
This localizes the remaining pause to synchronous save setup/snapshot work;
the full-frame reference also includes GPU waits in its ordinary drawing phase.
The profiler's final CSV write remains outside the measured checkpoint windows.

Snapshot allocation now starts at the 16 MiB background-storage bound, avoiding
repeated buffer growth/copies while serializing typical checkpoints. Larger saves
retain the bounded synchronous fallback. With the direct MSAA capture revision
held constant, `checkpoint-copyresolve-reference` serializes in 26.370/22.791/18.332 ms;
`checkpoint-snapshot-reserve-vk` takes 21.869/16.022/16.726 ms. The reference
deliberately uses the preceding staged snapshot allocator; its binary hashes are
retained in `plan.json`. The candidate passes load/quit/reopen and failed-description
overwrite preservation. Its three checkpoint-window peaks are 69.02/28.33/30.65 ms
versus 62.40/36.17/32.11 ms in the reference: less warm serialization work does
not remove the first-save spike or establish a better worst-frame floor.
`checkpoint-snapshot-reserve-gl` passes load/quit/reopen on OpenGL, with warm
serialization of 15.491/14.736 ms and checkpoint-window maxima of
72.65/53.75/43.95 ms. Both backend runs request 2560×1440 windowed and capture
2538×1312; neither is a native-fullscreen qualification.

The joined-worker checksum proof removes the second full-payload CRC pass from
checkpoint completion. `checkpoint-proof-vk-verified` passes immediate load,
quit/reopen, independent corruption rejection followed by a valid load, and
failed-payload overwrite preservation. Its ordinary validation phases take
0.206/0.235/0.280 ms, versus the preceding 3.6–3.8 ms. The three checkpoint-window
maxima remain 62.05/32.98/28.37 ms; serialization takes 21.900/19.185/15.987 ms.
This removes validation work but does not remove the live-snapshot stall.
`checkpoint-proof-vk` preserves an earlier harness failure: a hyphenated script
name was tokenized into separate words, preventing the final recovery marker.
The corrected harness uses an underscore; the damaged save was rejected in both
runs. Savegame v3/v4 and corruption contracts pass.
`checkpoint-proof-gl-fallback` also passes load/quit/reopen and independent
corruption rejection on OpenGL with workers disabled, exercising the full CRC
validation fallback. It is a windowed lifecycle test, not a performance qualification.

Compiled-script checksum caching now removes another repeated main-thread pass.
Both game modules invalidate the save-only cache for instruction edits, statement
allocation, definition allocation/renumbering, reset and teardown. Successful
compilation and restart warm it before gameplay; interpreter reads are const.
Load validation retains the original independent checksum calculation, and no
saved bytes or checksum algorithm change. `com_saveGameProfile 2` compares the
cached value against a fresh calculation and reports that diagnostic cost.

`checkpoint-script-oracle-vk` passes five cache/reference comparisons, immediate
load/quit/reopen and damaged-payload rejection. The avoided reference calculation
costs 2.922–3.833 ms in its four pre-reload saves (5.867 ms after reload), over
84,495 instructions. `checkpoint-script-mutations-vk` additionally passes two
console compilations containing branch/loop fixups and constant folding: the
instruction counts grow to 84,503 and 84,512, then return to 84,495 after loading
the untouched checkpoint. Every cached value matches the fresh checksum. These
oracle runs deliberately rehash and are not performance qualifications.

With the oracle disabled, `checkpoint-script-cache-vk` passes immediate
load/quit/reopen and failed-payload overwrite preservation. The globals phase
falls to 0.500/0.533/0.525 ms; total serialization is 18.362/14.465/15.766 ms.
Checkpoint-window maxima are still **60.50/27.57/28.71 ms**. This is a targeted
CPU saving, not stall removal or proof of an improved strict frame-time floor.
The windowed test requests 2560×1440 and captures 2538×1312. Both native script
restore fixtures and the v3/v4/corruption contracts pass.
The windowed `checkpoint-script-mp` smoke also completes with two autonomous bots
moving, firing and scoring on `mp/q4dm1`, exercising the rebuilt MP interpreter.
Its existing missing-AAS/non-precache asset warnings remain; this does not qualify
all bot navigation or eliminate those warnings.

Checkpoint preview persistence now also runs on the bounded job system. After
the renderer closes its 320×240 TGA, a worker reads only that fixed-size image,
checks its format, durably replaces the staged file and compares its readback
byte for byte. Completion joins both the payload and preview jobs before the
unchanged payload-last transaction. The main thread keeps the preview header
check but avoids a second sync when the worker verified persistence. Disabled,
unavailable or failed jobs retain synchronous validation/sync. Renderer preview
polling now reports failed TGA writes instead of reporting success.

With the current shadow union enabled, `checkpoint-preview-reference-vk` and
`checkpoint-preview-worker-vk` compare rotating, maximum-quality windows with
the same 2560×1440 request and 2538×1312 drawable. Main-thread preview completion
falls from **1.270/1.247/1.160 ms** to **0.304/0.291/0.323 ms**. The worker takes
11.8–12.1 ms outside the game thread for these first three saves. Window maxima
are **26.02/28.68/31.28 ms** before and **25.23/24.45/27.36 ms** after; this single
pair is evidence of the removed sync, not a reliable worst-frame improvement.
Live-state serialization still takes **15.293/11.854/13.533 ms** in the candidate.
Both retain CPU phase CSVs; their final file flush occurs outside the measured
checkpoint windows.

The candidate passes immediate load/quit/reopen, independent corrupt-save
rejection and obstructed preview staging with both committed slots unchanged.
`checkpoint-preview-worker-gl` also passes load/quit/reopen and obstructed
description staging at 640×480 with maximum quality. Native production-worker
fault injection rejects failed sync, failed/mismatched readback, truncated images,
wrong dimensions and alternate image formats; capture orientation and save
corruption/wire contracts pass. `checkpoint-preview-fallback-vk` passes immediate
load/quit/reopen and blocked-payload preservation at 640×480 with workers disabled;
no preview jobs run in that mode. None of these functional probes qualifies the
native fullscreen frame-time floor.

The staged build's subsequent strict fullscreen rerun,
`qualification-preview-vk/qualification.json`, records 1,349 frames over
27.006 seconds: **49.951809 FPS** mean (20.019295 ms), **26.15 ms p99** and
**33.04 ms maximum**. Native resolution, maximum-quality, HUD, effects and
rotation checks pass; every performance gate still fails. This ordinary rotating
run does not save checkpoints, so its lower maximum is not attributed to preview
storage changes.

A fixed-size scalar inlining experiment was rejected and reverted. The paired
windowed profiles `checkpoint-inline-{reference,candidate}-vk` both pass
load/quit/reopen, independent corruption rejection and failed-payload overwrite
preservation. Warm serialization means are **12.108 versus 12.134 ms**; checkpoint
windows peak at 52.97/26.30/23.88 ms before and 56.44/27.73/24.35 ms after. These
observations do not show useful improvement. The original buffered serializer
is restored. Additional native wire tests are retained: signed extremes,
floating-point infinities/NaN/subnormal bit patterns, partial buffer tails and
failed boundary flushes pass for both SP and MP production writers.

`checkpoint-lifecycle-{vk,gl}` pass immediate load, immediate quit and reopening
the last checkpoint in another process. `checkpoint-fallback-vk` repeats this
with jobs disabled and verifies that an obstructed overwrite leaves both saved
payloads and their sidecars byte-identical. All saved payload checksums validate,
and engine-written thumbnails were inspected. The saved bytes remain compatible
with the current bounded v4 reader.
The latest Vulkan run also obstructs description staging after payload storage;
both previously committed slots and sidecars remain byte-identical. The OpenGL
run repeats payload obstruction. A fresh-profile failure found during development
(`checkpoint-description-vk`) was fixed by creating the save directory before
dispatching the storage job; `checkpoint-final-vk` verifies the first save succeeds.

`com_saveGameProfile 3` now optionally aggregates non-null dictionary-write time,
count and serialized bytes in both game modules. It latches once per writer;
normal saves perform no timing calls or offset queries for this instrumentation.
The native production-writer oracle verifies unchanged bytes, exact accounting
and disabled/enabled clock behavior for SP and MP, including empty/null
dictionaries, buffer boundaries and maximum valid counts. The checkpoint harness
exposes `--profile-dictionaries` and records these diagnostic samples separately.

`checkpoint-dictionaries-vk/report.json` passes immediate load, immediate quit,
reopen, independent corrupt-save rejection and obstructed-payload preservation.
At a 2538×1312 windowed drawable with maximum quality, its first three dictionary
totals are **3.479/2.823/2.991 ms**, covering about **3.94 MB** across
1,769–1,772 dictionaries. Total serialization is **22.650/16.107/16.703 ms** and
checkpoint-window maxima are **61.06/28.82/29.94 ms**. Per-class and dictionary
clock overhead is included, so these are attribution data rather than a new
performance qualification. The measured warm dictionary cost makes reuse of
unchanged serialized dictionaries a concrete next investigation. Save
corruption/v3 contracts and native wire-format tests pass.

Two validation-by-scanning cache prototypes were rejected. Comparing all keys
and values against stored bytes regressed mean warm serialization from
**12.993 to 13.9604 ms** over five alternating pairs. Retaining immutable value
strings and comparing their identities, while still comparing key bytes,
regressed **12.5658 to 13.8858 ms**. Their oracle runs pass byte comparisons,
load/quit/reopen, corrupt-save rejection and failed-payload preservation. The
first collector failed on trailing whitespace in an echo marker; its ten
completed samples were recovered from the unchanged log, with the missing
collector exit status explicitly recorded. Evidence is in
`checkpoint-dictionary-cache-{oracle,pairs}-vk` and
`checkpoint-dictionary-pins-{oracle,pairs}-vk`. Neither implementation remains.

A third prototype observed dictionary mutations instead of scanning strings.
Every content mutation, source destruction, cache eviction or replacement
detaches the registration in both directions. Copies receive independent
identities; address reuse cannot prove a hit. It retains no pooled strings,
caps each serialized record at 64 KiB and the cache at 16 MiB including its
fixed table, and clears at map/module shutdown. That initial prototype kept
**`g_saveDictionaryCache 0`** as the default; mode 1 reuses bytes and mode 2 compares every hit
with the original writer. Mode 0 bypasses a populated cache until map shutdown;
it does not allocate one. The dictionary layout requires matching game ABI 57
and renderer ABI 27 modules, without changing save-file bytes.

Production-mutator native tests cover replacement, insertion, deletion, copy,
defaults, assignment, transfer, source destruction/reconstruction and observer
replacement/destruction. SP/MP writer tests cover byte parity, failed writes,
buffer tails, bounded eviction and invalid records. The live oracle
`checkpoint-dictionary-observer-oracle-vk` verifies all **4,614 hits** against
fresh serialization, keeps about **5.9 MB**, and passes lifecycle, corrupt-load
and failed-overwrite checks. Save corruption/v3 and module API contracts pass.
`checkpoint-dictionary-observer-fallback-gl` also passes at 640×480 with maximum
quality, fresh-byte verification and jobs disabled, including load/quit/reopen
and obstructed-description preservation. Its renderer and resolution make it a
functional fallback check, not native frame-rate qualification.

The five-pair normal and reversed-order probes
`checkpoint-dictionary-observer-{pairs,reverse}-vk` both pass collection and
reload checks. Across ten samples per mode, mean serialization falls from
**14.7989 to 12.8492 ms** (13.17%) and the median from **13.4855 to 11.9275 ms**
(11.55%). No outliers are removed: serialization peaks at **28.613/22.978 ms**,
and checkpoint windows at **40.59/46.88 ms**, respectively. These windowed
maximum-quality runs use a 2538×1312 drawable. They do not establish an improved
worst-frame floor. Ten fresh-session runs in
`checkpoint-dictionary-observer-cold-vk` all pass save/reload and independent
CRC checks, but first-save mean serialization regresses **21.9358→25.6528 ms**
(16.94%) and median **18.494→22.260 ms**. Each mode has five samples, interleaved
in both orders, with no outliers removed. That prototype was not enabled. Its
immediate population is replaced by the deferred path below.

The rebuilt default path (`g_saveDictionaryCache 0`) is requalified in
`qualification-dictionary-observer-vk/qualification.json`: **1,432 frames over
27.016 seconds, 53.005345 FPS mean (18.866022 ms), 24.90 ms p99 and 31.81 ms
maximum**. Native fullscreen 2560×1440, maximum quality, rotation, HUD and actual
post-effect checks pass; every performance gate fails. The engine-written
capture was inspected. This supersedes the previous default-build measurement
without attributing its small timing difference to the disabled prototype.

Deferred population now registers misses without allocating their payloads in
the save writer. Later game frames use the same encoder to fill only dictionaries
whose content/lifetime observation remains valid. Mutated or destroyed sources
are discarded before dereferencing them. The first population pass after a save
does no work; subsequent calls inspect at most 128 slots, encode at most 24
records and stop after 128 KiB of attempted payload or a 0.25 ms deadline. Limits
are checked between records, so one bounded record may overrun them (64 KiB plus
one encoder chunk). Map/module shutdown detaches all observers. Native tests
cover source destruction, mutation, repeated misses, deadline/byte bounds,
oversized records and both production writers against independent wire bytes.

`checkpoint-dictionary-deferred-oracle-vk` passes all **4,774** fresh-byte
comparisons, load/quit/reopen, corrupt-save rejection and failed-payload
preservation. Initial population spreads over 74 game frames; its largest
observed batch is **0.566 ms**. The five-pair normal and reversed runs
`checkpoint-dictionary-deferred-{pairs,reverse}-vk` retain every sample: combined
warm serialization mean is **13.5596→11.4689 ms** (15.42%) and median
**12.811→10.4655 ms** (18.31%). Checkpoint-window maxima are **35.32/28.95 ms**;
this is not a strict frame-time qualification. Both writable filesystem paths
are now isolated in each pair run; the older pair harness had inherited the
oracle's developer path while isolating saves/logs. New plans record runtime
hashes before launch.

All ten fresh-session runs in `checkpoint-dictionary-deferred-cold-vk` pass
reload/CRC checks. First-save means **16.3564/16.1674 ms** and medians
**16.220/15.978 ms** show no measured first-save penalty; their small difference
is not credited as a speedup. With these checks, deferred reuse is enabled by
default (`g_saveDictionaryCache 1`), with bypass 0 and verification 2 retained.
The staged default run `checkpoint-dictionary-deferred-default-vk` passes
lifecycle, corrupt-load and blocked-preview preservation. Its warm dictionary
totals are **1.837/1.646/1.546 ms**, and its largest population batch is
**0.251 ms**. Detailed profiling includes clock/class overhead. The original
worker storage and independent load-time integrity checks remain in force.

The staged OpenGL synchronous fallback `checkpoint-dictionary-deferred-fallback-gl`
passes **4,895** fresh-byte comparisons, load/quit/reopen and blocked-description
preservation at 640×480, with a largest population batch of **0.236 ms**.
`checkpoint-dictionary-deferred-previous-vk` loads an unchanged checkpoint from
before the dictionary observer ABI change and captures the restored game.

The dictionary-cache default was qualified by `qualification-dictionary-deferred-vk`:
**1,418 frames over 27.021 seconds, 52.477896 FPS mean (19.055642 ms), 25.90 ms
p99 and 35.98 ms maximum**. Native fullscreen 2560×1440, maximum quality,
rotation, HUD and actual post-effect checks pass; all performance gates fail.
The engine-written capture was inspected. These checkpoint changes do not
address the GPU rendering cost; the strict frame-time goal remains open.

An opt-in experiment transfers the completed checkpoint snapshot to the storage
job (`com_saveGameCopyOnWorker 1`, default **0**) instead of allocating and copying its entire
payload on the game thread. The worker only reads immutable bytes; its job list
joins before the source is destroyed. Rejected submissions return the same
snapshot to the existing synchronous fallback. `openq4-checkpoint-storage`
extracts the production handoff/worker and covers deferred/immediate execution,
failed list creation/add/submit, source lifetime, exact bytes/trailer, and
payload/description write/readback failures. The native preview/dictionary
suites also pass. `checkpoint-snapshot-worker-vk` passes load/quit/reopen,
independent corrupt-load rejection and blocked-payload preservation.

The OpenGL synchronous fallback `checkpoint-snapshot-worker-fallback-gl` also
passes lifecycle and blocked-preview preservation. The normal/reversed five-pair
runs `checkpoint-snapshot-worker-{pairs,reverse}-vk` retain every sample. Combined
handoff mean falls **1.9377 to 0.1900 ms**, but total checkpoint CPU work rises
**26.0365 to 28.8637 ms** (medians **22.0680/23.3145 ms**). Mean checkpoint-window
p99 is **27.05/33.235 ms**; maxima are **44.20/70.86 ms**. A candidate commit
outlier takes **19.602 ms**, while the reverse run also has setup/serialization
outliers before handoff. The local copy improvement does not establish a total
frame-time improvement. **Worker-side copying remains disabled by default**;
these samples do not satisfy REN-07's regression gate. The bounded dictionary
cache remains enabled independently. Further work must address both live-state
snapshot cost and game-thread transaction setup/commit work.

The rebuilt/staged default with dictionary reuse enabled and worker-side copying
disabled is qualified in `qualification-snapshot-reference-vk`: **1,444 frames
over 27.014 seconds, 53.452840 FPS mean (18.708080 ms), 25.05 ms p99 and
37.14 ms maximum**. Native fullscreen 2560x1440, maximum quality, rotation,
HUD/effects and runtime identity checks pass; all pacing gates fail. Warning
counters are zero and the engine-written capture is inspected. This is the
previous default-build qualification, without attributing run-to-run variation
to the disabled copy experiment.

The legacy-GUI probe `checkpoint-ui-identities-vk/ui-identities.json` finds
**69 GUI writes for 26 instances per snapshot**. The 43 repeated writes account
for **2.18–2.38 ms** and 470,618 repeated bytes in three diagnostic saves.
Although the retained HUD is visible, these checkpoint GUI payloads are legacy
`.gui` state. The earlier `checkpoint-ui-validation-pairs-vk` comparison did not
exercise retained `Q4UI` snapshots at all; its timing difference is invalid as
optimization evidence. That Motion-validation candidate was reverted, with its
patch and the invalidation reason retained beside the report.

An opt-in, per-snapshot legacy-GUI cache (`g_saveUserInterfaceCache`, default
**0**) reuses only complete memory-backed `.gui` payloads. It caps entries at
256, each record at 1 MiB and all payloads at 8 MiB; closing or abandoning the
snapshot releases them. Game Save methods must invalidate an instance before
changing it; both player save-notification paths do so. Retained `.q4ui` state
and file-backed outputs use the original writer. Native tests extract both
production game writers and verify independent wire bytes, unique flags,
mutation invalidation, snapshot lifetime, limits, failed fresh/cached writes,
and stale-record rejection. The Vulkan lifecycle oracle checks **215 hits**
against fresh bytes, with at most **370,836 resident payload bytes**. The
OpenGL synchronous-job fallback adds **215** matched hits (371,106 bytes at
most), passes load/quit/reopen, and preserves both committed slots when the
staged description is blocked (`checkpoint-gui-cache-fallback-gl`).

`checkpoint-gui-cache-{pairs,reverse}-vk` retains ten alternating pairs across
both orders. Combined mean serialization falls **13.6593→9.9167 ms** (27.40%;
medians **11.7125→9.8505 ms**), but total checkpoint CPU work rises
**26.2428→27.2290 ms** and mean window p99 rises **28.109→31.554 ms**. The largest
window is **59.02/72.49 ms**, and a candidate commit reaches **24.675 ms**.
These results fail promotion: the cache stays disabled, with no end-to-end gain
claimed. `checkpoint-gui-cache-comparison.json` records that decision explicitly.

Optional level-3 save profiling now splits the commit phase into file
transaction, preview cleanup, sound and notification work. The three saves in
`checkpoint-commit-attribution-vk` spend **1.312/1.343/4.964 ms** in file commit,
**0.344–0.452 ms** in notification, and **0.001 ms** in sound unmuting. This
attributes ordinary commit cost to file operations, without claiming to have
reproduced the 24.675 ms outlier. Moving the transaction off the game thread
must preserve payload-last publication, rollback, recovery and load/quit joins.

An experimental `com_saveGameCommitOnWorker` switch (default **0**) moves the
three-file checkpoint publication onto one bounded job. Exact paths are resolved
on the game thread; the worker never accesses the VFS, game or UI. Description
and preview publish before the payload. Rollback moves published components
back to temporary names before restoring backups, retaining the existing
recovery evidence if any step fails. Loading, quitting and save-menu reads join
both storage and publication; the saved notification follows successful commit.
The ordinary save and disabled-job paths remain synchronous.

The production transaction, recovery routine, operation guard and pending-save
poll are compiled into `openq4-checkpoint-commit`. Fault injection covers every
namespace operation with all eight original-sidecar combinations, including
recovery from every intermediate mutation. Native Unicode paths, scheduler
rejection, job lifetime, reentrant menu polling and two-stage joins also pass.
`checkpoint-commit-worker-vk` passes lifecycle, corruption rejection and blocked
payload preservation. `checkpoint-commit-rollback-vk` then denies rename of only
the isolated profile's previews: the real transaction rolls back, all six final
files retain their SHA-256 hashes, no transaction files remain, and the old
checkpoint reloads. `checkpoint-commit-fallback-gl` passes load/quit/reopen and
blocked-description preservation with jobs disabled; 215 GUI-cache oracle hits
also match fresh bytes in that fallback.

The normal/reversed `checkpoint-commit-{pairs,reverse}-vk` runs do **not** justify
promotion. Across ten samples per mode, mean game-thread commit work falls
**5.1779→1.7021 ms**, but serialization rises **20.4742→29.9986 ms**, total work
**36.1266→42.4517 ms**, and mean window p99 **38.500→49.276 ms**. The serialization
difference occurs before that save queues its commit job; its cause is not
established. All samples remain part of the evidence.

A separate 20-save `checkpoint-commit-transitions-vk` run balances each
previous/current setting combination five times. Serialization is nearly equal
(**12.9510/12.9148 ms**); mean commit work falls **5.4165→1.8559 ms** and total
work **29.1073→23.5721 ms**. Mean window p99 falls **28.548→27.117 ms**, while its
median rises **25.500→27.315 ms**; maximum windows are **46.13/32.85 ms**. This
supports further investigation but does not erase the earlier regressions or
establish a consistent whole-frame improvement. Worker publication remains
disabled. Reports and the transition grouping are under `.tmp/airdefense-perf/`.

`checkpoint-combined-transitions-vk` tests GUI reuse and worker publication
together over another balanced 20-save sequence. Mean serialization falls
**13.6697→11.7462 ms** and commit work **7.4805→1.8356 ms**, but setup rises
**6.9329→11.0802 ms**. Total work improves **32.1553→29.2471 ms** while mean
window p99 regresses **29.223→34.725 ms**; maximum windows are **46.90/65.33 ms**.
The worst candidate window includes **32.694 ms** of setup before serialization
or its worker dispatch. The combination is not promoted, and all outliers are
retained. Optional level-3 profiling now splits setup into health/eligibility,
free-space, path/guard, interrupted-save recovery, temporary cleanup and snapshot
allocation phases to identify that remaining synchronous work.

`checkpoint-setup-attribution-vk` assigns **1.851/4.297/2.707 ms** to recovery,
with free-space checks and temporary cleanup each below 0.1 ms. Investigation
also identifies an observer cost: these test launches inherit `developer 1`
and `logFile 2`, so each explicit recovery read emits an unbuffered debug
message. The same staged runtime with `developer 0`, still using level-3 save
profiling, measures recovery at **0.084/0.127/0.170 ms** in
`checkpoint-setup-nodeveloper-vk`; save integrity and reload pass. The original
probe also records frame-work CSV data. This is a logging-mode comparison,
not an engine speedup or reproduction of the 32.694 ms outlier. Earlier save
phase comparisons remain debug-mode evidence; buffered-logging results must
be identified separately before any default is promoted.

`checkpoint-combined-buffered-vk` repeats the balanced 20-save comparison with
verified `developer 0` and `logFile 1`. Total CPU work falls **22.0763→20.2443 ms**
and serialization **15.0282→12.9398 ms**. However, mean window p99 rises
**23.930→24.398 ms**, and maximum windows are **33.66/39.84 ms**. The p99 median
improves **23.595→21.045 ms**, but these mixed results still fail promotion.
Both GUI reuse and background publication remain disabled.

A narrower change removes duplicate developer-only tracing from explicit file
reads; the existing `fs_debug` trace remains available. This avoids logging each
routine recovery existence probe. `checkpoint-read-trace-vk`, with the original
`developer 1` / `logFile 2` launch and level-3 save profiling, measures recovery
at **0.113/0.087/0.137 ms** in its first three saves, with **0.186/0.207 ms** in
its immediate load/quit cases. Lifecycle, corruption rejection and blocked
preview preservation pass. This change is enabled; it removes demonstrated
logging overhead without changing file operations. Checkpoint windows still
peak at **40.64/27.19/34.97 ms**, so no complete stall-removal claim is made.

The latest default qualification, with GUI reuse, worker-side snapshot copying
and worker publication disabled, is `qualification-checkpoint-commit-vk`:
**1,453 frames over 27.068 seconds, 53.680435 FPS mean (18.628761 ms), 27.65 ms
p99 and 80.06 ms maximum**. Native fullscreen 2560×1440, maximum quality,
completed rotation, HUD/effects and runtime identity checks pass; all pacing
gates fail. Recorded warning counters are zero and the engine-written capture
is inspected. The 80.06 ms outlier is retained without attributing its cause;
this run contains no saves and does not measure the logging fix. The earlier
`qualification-gui-reference-vk` result (53.75 FPS, 32.86 ms maximum) remains
historical evidence and is not substituted for this latest result.

**The stall is reduced, not fully removed.** Live-state serialization still
takes roughly 10–12 ms in typical warm rotating Vulkan samples, with longer
outliers retained in the comparisons above. CPU phase profiling also exposes a first
draw spike; the first motion-blur draw occurred after that checkpoint in the
stationary probe. These remain investigation targets, and neither REN-07 nor
the strict 150 FPS goal is closed.

Unrelated warning observed in startup/loading UI: the wordmark halo reports
coverage 1.0001655 outside the normalized range. It appears before checkpoint
measurement; the logs above retain the warning for UI follow-up.

### Projected shadow pipeline counters, 27 October 2026

`r_vkGpuPipelineStatistics 1`, set before renderer startup, optionally adds
pipeline counters to `r_vkGpuPhaseProfile`. The feature defaults to zero and
is enabled only when supported by the device. Query allocation/reset occurs
only during a bounded phase capture. Results retire with the existing frame
fence and never request `VK_QUERY_RESULT_WAIT_BIT`; video restart retires both
query pools. Unsupported or disabled counters have explicit validity zero.
The ordering and interpretation follow the [Vulkan query specification](https://docs.vulkan.org/spec/latest/chapters/queries.html):
invocations are implementation-relative work counts, not unique visible pixels.

`r_vkGpuProjectedStatisticsScope 0` measures the fresh projected-atlas span.
Scopes 1–4 select LOCAL cascades 0–3, and 5–8 select GLOBAL cascades 0–3, for
the first matching light. Scoped files use `gpu-phases-scopeN.csv` and record
the admitted light-array index and whether its projection has constant W. Changing
scope discards pending samples from the old request. These scoped counters
must not be interpreted as totals over all lights or cached-map composition.

The native maximum-quality rotation in `max-phases-pipeline-vk` records 1,024
valid samples with no Vulkan validation messages. Fresh projected maps average
**4.509 ms**, **72,225 input triangles** and **143,463,762 fragment invocations**
per frame. A separate stationary diagnostic, `max-phases-pipeline-scopes-vk`,
freezes simulation and audio-driven camera shake and records 128 samples per
scope. All eight select perspective light-array slot 0 (render light 320 in
the later fit trace):

| Cascade | LOCAL fragment invocations | GLOBAL fragment invocations |
| --- | ---: | ---: |
| 0 | 37,486,987 | 19,068,865 |
| 1 | 33,425,502 | 7,730,755 |
| 2 | 26,789,180 | 2,530,004 |
| 3 | 22,767,297 | 287,081 |

The scoped values are identical across each stationary sample set. They locate
fragment work, but neither diagnostic qualifies frame rate. Windowed
`shadow-resize-pipeline-phases-{on,off}-vk` exercises a video restart between
64-sample captures: enabled counters remain valid; disabled counters remain
invalid/zero; both runs have no validation messages. Production query-guard
tests, native GPU timing and shadow compatibility contracts pass.

An opaque-only caster specialization was tested and removed. It preserves
cutout shaders and leaves point casters unchanged. In four frozen views,
two candidate images match the reference exactly; the other differences also
occur in unchanged controls. No validation errors occur. Three alternating
maximum-quality rotations in `max-opaque-pairs-vk` nevertheless average
**18.642750→18.755759 ms**, a **0.606% regression**. Candidate p99 values and
maximum frame times are worse in every pair. These arithmetic means use the
full-window timing marker, not the HUD's smoothed `present` value. The rejected
patch, image comparisons and all samples remain in the evidence directory.

The restored renderer keeps only the optional counters from this investigation.
The latest strict qualification remains `qualification-checkpoint-commit-vk`
above; the 150 FPS floor and checkpoint-stall removal are still open.

### Quality workload and deferred shadow scrolling, 28 October 2026

The user changed subsequent tests to **Quality**, the preset immediately below
Ultra. Native 2560 x 1440 fullscreen, rotating `game/airdefense1`, the Remastered
HUD and the strict 150 FPS floor remain required. Quality's 144 FPS cap is
disabled for measurement. Its 4x MSAA, SMAA High, 2048/1024 shadow sizes,
4096 atlas, no cascades, 20 SSAO samples and disabled motion blur remain intact.
Ultra still selects maximum supported quality. Both fullscreen qualification
and checkpoint harnesses now default to Quality and record the selected profile;
the fullscreen negative controls cover both workloads and reject mismatched
readbacks. Historical Ultra results are not reclassified as Quality evidence.

`qualification-quality-initial-vk/qualification.json` records **3,083 frames in
27.009 seconds**, **114.145 FPS / 8.760785 ms mean**, **13.80 ms p99** and a
**24.08 ms maximum**. Workload checks pass; every performance gate fails. This
run used the corrected scroll prototype with its feature disabled, before its
removal below. It does not measure checkpoint saving.

The unfinished Vulkan circular shadow-reuse experiment is removed from the
active source after this scope change. Its exact-grid replay found 1,919 of
2,332 rotating cascade transitions eligible, with 99.63% average retained area;
the planner checked 7,227,860 texels against full redraws. It needed an enlarged
uniform block and power-of-two allocation stride, plus wrapped receiver reads.
An initial shader misinterpreted endpoint rectangles as origin/extent; its
completed timing comparison therefore cannot support a performance claim.
After correcting that error, `max-scroll-views-address-vk` matches all four
candidate/reference RGB captures exactly with nonzero offsets confirmed during
capture and no Vulkan validation messages. The corrected timing runs were
interrupted by the pause and subsequent workload change. Accumulated scrolling
and lifecycle qualification remain incomplete.

The patch and source hashes remain in
`.tmp/airdefense-perf/projected-scroll-experiment/`; no scroll CVar, shader branch,
grid metadata or unused test/helper remains in the active renderer. Existing
optional pipeline counters and previously accepted optimizations remain.
The strict frame floor and checkpoint-stall removal are still open.

### Quality frame attribution and effect culling, 3 November 2026

The machine ran about a dozen other sessions' builds and clients throughout,
so absolute timings below are inflated and only their proportions are used.
Every run is the native 2560 x 1440 fullscreen Quality rotation from
`game/airdefense1` on Vulkan, from a separately staged runtime.

A 2,048-frame GPU phase profile (`r_vkGpuPhaseProfile`) averages 9.21 ms per
frame (p99 11.40 ms, maximum 14.40 ms): 6.96 ms for the main view and 1.39 ms
for the portal sky. Within the main view, SSAO takes 2.06 ms (0.37 ms depth
capture, 1.69 ms drawing), lights 1.90 ms including 0.65 ms of shadow maps,
bloom and tone mapping 1.05 ms, the depth prepass 0.83 ms, the two ambient
walks 0.90 ms and SMAA 0.21 ms.

The CPU is the tighter limit. With renderer metrics on, the main thread spends
7.3 ms per frame building the frame (game draw and render front end) and
3.8 ms recording and submitting it on Vulkan, while the frame-fence wait is
17 µs at the median: the GPU waits for the CPU. One thread runs the game tic,
the front end and the backend in turn, and the 60 Hz tic lands in about half
of the frames. `profileFrameWork` puts the tic at 2.91 ms on the frames that
run one, against a 6.67 ms budget for the whole frame. GPU work alone
therefore cannot close the every-frame floor.

`tools/debug/cpu_sampler.py` attributes main-thread time without an elevated
shell (ETW sampling needs one). It suspends the busiest threads for about
0.3 ms per sample, copies the stack, and walks it offline with dbghelp. An
earlier version walked the live stack and held the thread for about 20 ms per
sample. The engine answered each stall with catch-up tics, which inflated the
game tic to 53% of the samples. Keep the client running until `record`
finishes: the walk still reads code and unwind data from the process. A clean
4,091-sample profile over 30 seconds of rotation:

| Main-thread share | Work |
| --- | --- |
| 19% | Game tic: BSE client effects 4.6%, player 3.6%, AI 2.8%, articulated figures 1.8% |
| 36% | Scene front end: effects 18.7% (particle geometry 10.2%, servicing 3.6%, area tests 3.2%), models and interactions 14.2% (shadow volumes for prelit lights 2.6%) |
| 7% | HUD: legacy GUI state 2.2%, Remastered/retained drawing 4.5% |
| 6% | Presentation poses (exact bounds from every mesh vertex 2.4%) |
| 26% | Vulkan backend: per-frame geometry copies into the ring 3.6%, single-draw interactions 3.5%, shadow atlas 2.7%, frame submit 2.4% |

Three exact changes follow from it:

- Effects take the cheap frustum test before the portal-area walk, both before
  and after their geometry is rebuilt. Both tests must pass and neither has side
  effects, so the same effects draw. Area tests fell from 3.2% to 1.0% of the
  main thread, and the effect front end from 18.7% to 15.5%.
- `idPlayer::UpdatePowerUps` ran the stock HUD's `clearPowerups` script every
  tic. With no powerup owned and none shown since this HUD's last clear, the
  script changes nothing, so it now runs once (1.1% to under 0.1%).
- BSE servicing no longer copies each effect's name into an `idStr` before the
  filter check.

The candidate run logged no new warnings or errors. The ring copies of static
world geometry, prelight fallback volumes and exact presentation bounds remain
measured candidates; each needs a lifetime or policy change, not just a
reordering. The remaining gap needs backend recording off the main thread, a
cheaper tic and front end, and less GPU post-processing work at Quality.
