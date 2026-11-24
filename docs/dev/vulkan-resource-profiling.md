# Vulkan executor resource profiling

Roadmap VK-06 requires a measured workload before changing submission or resource
lifetime. `r_vkResourceProfile 1` enables bounded operation counters in the
executor. It defaults off and creates no GPU queries, readbacks or extra GPU
waits. `rendererVulkanMemoryInfo` prints them alongside heap and pipeline data;
`rendererVulkanMemoryInfo reset` prints the old interval and clears the counters.
A device restart clears them too. The report synchronizes the renderer thread
before reading its counters.

| Field | Meaning |
| --- | --- |
| `frames` | Newly opened executor frame slots; screenshot resumes do not start another slot |
| `imageHits`, `imageUpdates` | Reused image-generation descriptors and writes of new descriptors |
| `imageRetirements` | Old image descriptors queued for fence-protected retirement |
| `residentSets`, `retiredSets` | Current image-cache and deferred descriptor counts, even when profiling is disabled |
| `ringBytes`, `ringAllocs` | Bytes copied by the executor's mapped-ring allocator, excluding alignment padding and GPU-written reservations |
| `targetChanges` | Render-target assignments |
| `scopes`, `barriers`, `submits` | Executor dynamic-rendering scopes, synchronization2 barrier calls and queue submission attempts |
| `Vulkan static geometry:` line | Resident vertex streams, reported whether or not profiling is enabled; see [resident static geometry](#resident-static-geometry-24-november-2026) |
| `Vulkan shadow cache:` line (`r_shadowMapReport 1`) | Among the shadow pass's counters: `casterDraws`, `casterIndexes`, `cubeScopes` and `cpuMs`; see [shadow-map savings from OpenGL](#shadow-map-savings-from-opengl-24-november-2026) |

These are operation counts, not elapsed CPU/GPU time. They exclude commands
recorded in other translation units, including dedicated upload and shadow
command buffers. They cannot establish asynchronous transfer overlap or identify
a slow shader. Counts include attempts that a later view failure rolls back.
Pair them with the existing renderer CPU/GPU phase timings, pipeline creation
times, VMA heap counts and unchanged engine captures. Compare steady-state
intervals separately from loading and first-use compilation.

`tools/tests/renderer_vulkan_presentation.py` collects these counters in a visible
1280x720 stock Air Defense 1 workload, together with presented-frame timing,
engine CPU/GPU metrics, Windows foreground observations and AC/battery state.
It verifies the camera at both endpoints and samples for competing openQ4
clients once a second; an observed competitor fails the pacing gate.
It disables mouse/controller input and uses the engine's screenshot command.
It defaults to three repeats, a 120 FPS cap and a 28 ms presented-p99 budget.
It pins the balanced performance preset before applying the declared classic
rendering overrides, checks the baseline CPU/GPU timing identity, and retains
the saved configuration and per-frame phase CSV. CPU/GPU percentiles describe
the last 256 retained samples; presented intervals cover the sampled 20 seconds.
The report preserves the selected rendering GPU but labels the display-adapter
route **unverified**: identifying a cross-adapter route still needs the separate
queue trace described in [frame pacing](vulkan-frame-pacing.md).

```powershell
python tools/tests/renderer_vulkan_presentation.py --runtime-dir .install `
  --basepath 'E:\SteamLibrary\steamapps\common\Quake 4' `
  --output-dir .tmp/vk-presentation --devices 0 2
```

The caller must have permission to show the game window. Unsupported presented
timing, missing samples, an offscreen path, unobserved/lost foreground ownership,
timeouts and a missed budget fail the run. Non-Windows hosts currently lack
the foreground observer and therefore cannot obtain a foreground qualification
from this harness. A pass is scoped evidence, not support promotion or an
input-to-photon measurement.

## Initial resource measurements (3 November 2026)

The clean `29e237378` build ran three 20-second intervals per adapter on AC.
NVIDIA 591.74 and Intel 101.5590 kept their respective heap block/allocation
counts unchanged between the initial and final snapshots. Every interval kept
28 pipelines, with no creation after sampling started. Image descriptors had
0–5 updates, zero retirements and 397–401 resident sets at the end. Each frame
recorded one executor submission, four target changes, nine rendering scopes
and 34 barrier calls, with about 4.6 MiB copied into mapped rings.

These are diagnostic operation counts, not passing pacing evidence. Foreground
ownership was never observed, another client overlapped two runs, and this first
harness selected GPU-dependent first-run presets. The subsequent harness pins
the preset. The original reports remain in `.tmp/vk-roadmap/presentation-ac/`;
see [the presentation failures](vulkan-frame-pacing.md#visible-window-diagnostics-3-november-2026)
before using any timing value.

The corrected balanced-preset pair in `.tmp/vk-roadmap/presentation-balanced/`
has the same per-frame submission/target/scope/barrier counts, five image
descriptor updates per run, zero retirements and 401 resident sets. Both runs
also retain their heap/allocation and 28-pipeline plateaus. This confirms the
operation-count observation with the profile fixed; focus and presentation
limitations still prevent performance qualification.

Decisions for this workload:

- Keep the descriptor and pipeline caches: the recorded steady intervals have
  negligible descriptor churn and no pipeline churn. They do not justify a new
  cache or broader pipeline warm-up; the earlier cold-cache decision remains.
- Defer indirect drawing/Hi-Z and parallel recording until a dense or
  recording-limited workload identifies their critical path. The large
  presented-frame tail here is not explained by the short retained GPU timing
  window. These measurements cannot show that either mechanism would fix it.
- Defer transfer/upload overlap until an upload-heavy workload measures waits
  and potential overlap. Mapped-ring byte counts alone establish neither a
  transfer-queue bottleneck nor an overlap benefit.

No optimization is promoted from these short intervals. Long-session plateaus,
matching-output comparisons and improved frame-time tails remain required for
any future resource/submission change under VK-06.

## Restart residency failure (4 November 2026)

A stock map tour on the clean `3866569f` build reproduced image-table exhaustion
at the fifth map's video restart, after airdefense1, hangar1, convoy1 and hub1
followed by medlabs. The restart reloaded file handles already purged from
earlier maps; the 4096-entry table filled and logged 2,023 repeated warnings.
The owned failing run was stopped and the queued Intel baseline was cancelled.
Its raw logs, captures, configuration and unchanged runtime identity remain in
`.tmp/vk-roadmap/soak-nov4/aborted-report.json`; this is failed, incomplete soak
evidence.

Ordinary reloads now leave purged, unreferenced file images lazy. Loaded images,
active map/UI references and generated targets still reload. Checked settings
recovery retains its explicit residency snapshot as the authority. Table-full
warnings are limited to one per continuous exhaustion episode, and
`rendererVulkanMemoryInfo` now includes the resident image count and capacity.
Native controls cover a 12,000-handle catalogue with only 2,401 referenced
images, checked-recovery selection, all 4096 slots and warning recovery after a
slot becomes available. These controls pass on Windows and Linux (WSL). The
existing checked-recovery mutation controls and all 533 metadata checks also
pass on Windows.

The corrected clean `da0080eb` package completes two cycles of all eight maps
on each local adapter, with 600 frames per stop and the same fifth-map restart.
Both runs produce all 16 drawn engine captures, all 32 labelled residency
samples, zero Vulkan warnings and zero validation messages. The staged package
hashes are unchanged after testing.

| Adapter | Duration | Peak image slots | Largest same-map allocation growth | Largest same-map process-memory growth |
| --- | ---: | ---: | ---: | ---: |
| RTX 4060 Laptop, 591.74 | 690.5 s | 2,372 / 4,096 | 7.22% | 6.70% |
| Iris Xe, 101.5590 | 699.8 s | 2,372 / 4,096 | 7.22% | 15.73% |

The failing restart now changes medlabs residency from 2,370 to 2,369 instead
of filling the table. Every map's counts match between adapters. Counts increase
between the first and second visits (airdefense1: 2,023 to 2,170), so this does
not establish a long-session resource plateau. The shorter stops target the
reproduced restart defect; concurrent functional testing and the offscreen
workload provide no pacing or optimization claim.

The qualification report and hash-checked `residency-proof.json` are retained
under `.tmp/vk-roadmap/residency-fixed-tour/`. Both runs pass the bounded tour
checks and retain 117 stock-content warnings (47 unique, identical normalized
digest). These include unknown look joints, invalid reverb areas, uncached
declarations and script/entity warnings. The missing `textures/proto/cap_lg`
image is also present in the retained failed baseline. No warnings were removed
or converted into general clean-startup qualification. The exact measured
package is `runtime-da0080eb.zip`, SHA-256
`daf52c9e2804fe3536233cf34ebdf6e33a7298b4804dcd2e182df0dc9d23467e`.

## Five-cycle soak (6 November 2026)

The same eight-map stock ring ran for five cycles (40 stops, 600 frames per
stop, the video restart at the fifth stop) on the clean `1243eb6a` package,
with validation enabled, on both local Vulkan adapters and on OpenGL as the
process-memory reference. All three runs reach all 40 stops and pass the soak
checks. Vulkan validation reports nothing. Every run logs the same 47 unique
stock-content warnings (normalized digest `e316f0f9…`) as the 4 November tour.

| Run | Duration | Peak image slots | Process growth, 1st to 2nd visit | Process growth, 2nd to 5th visit |
| --- | ---: | ---: | ---: | ---: |
| RTX 4060 Laptop, 591.74 | 1,620.6 s | 2,379 / 4,096 | up to 454 MiB | up to 94 MiB |
| Iris Xe, 101.5590 | 1,781.8 s | 2,379 / 4,096 | up to 654 MiB | up to 60 MiB |
| OpenGL, same NVIDIA GPU | 1,340.0 s | — | up to 416 MiB | up to 97 MiB |

Growth after the first cycle is small and of the same size on both renderers,
so it does not come from the Vulkan backend. Vulkan heap allocations stay
exactly 12 or 13 above the resident image count at every sample. Nothing
outside images accumulates: no buffers, pools or pipelines.

Resident images do not stop growing completely. Once the first cycle is over,
every map reports one more slot per cycle, identically on both adapters (for
example airdefense1: 2,027, 2,174, 2,176, 2,177, 2,178). A three-cycle NVIDIA
repeat listed every image at each stop and found two sources, both front-end
and both bounded:

- **Retained-UI layer targets** (`_retainedLayerImageN`, window-sized RGBA8).
  The retained interface keeps a slot pool for composition layers and cached
  layers. It reuses slots of matching size first, evicts its layer cache when
  the pool is full, and is capped at 48 slots and 256 MiB. In the repeat it
  rose from 6 to 12 slots by the twelfth stop and then held. Trimmed slots keep
  a 1x1 image, so the slot count never falls back.
- **A second `gfx/objectives/core1_obj_1`.** core1 loads the objective image
  with different sampling parameters, and that second copy stays loaded.
  Revisits do not add more copies.

On this ring the slot count therefore approaches a fixed ceiling rather than
growing without limit. Only a run longer than the 48-slot layer cap could show
the actual ceiling, and these runs do not reach it. The five-cycle report,
`plateau-analysis.json` (per-visit allocations, resident slots and process
memory) and engine logs are under `.tmp/vk-roadmap/nov6/soak5/`; the image-listing repeat is in `soak-imagelist/`.

## Resident static geometry (24 November 2026)

A CPU sampling profile of the stock Air Defense 1 skyline sweep (1920x1080,
quality preset, RTX 4060 Laptop, 591.74) put the Vulkan back end at 24.9% of
the client's sampled CPU. Inside it, 23.5% was `VK_Exec_UploadTriGeometry`,
nearly all of it the copy into the frame rings: the largest single cost of
executing the command list. `r_vkResourceProfile` counted 3.6 MB a frame in
that sweep (2.75 MB of vertices, 0.74 MB of indexes, the rest uniforms). The
vertex cache of the Vulkan module is CPU memory, so every stream a frame
draws was copied again each frame, where OpenGL uploads a static stream to a
buffer object once.

Vertex streams that outlive their frame are now copied once into a persistent
buffer and drawn from it (`r_vkStaticGeometry`, default 1; the buffer is
`r_vkStaticGeometryMegs`, default 96 MiB, created on first use). The design
and its limits:

- **Identity.** The vertex cache recycles a freed block's header at once, so a
  header pointer cannot identify a stream. Every static allocation now takes a
  serial no other allocation has had (`vertCache_t::serial`; zero for frame
  temporaries and free headers), and a resident copy is keyed on the header
  and that serial. A recycled header is recognised at its next draw, and the
  old copy is never drawn for it.
- **Promotion.** A stream becomes resident when it is still the same
  allocation 150 ms after it was first drawn. What an animated model rebuilds
  every game tic never lasts that long and stays in the rings, as do frame
  temporaries, the frame snapshot's copies (`r_vkRenderThread 2`) and surfaces
  the GPU skinning pass may write.
- **Lifetime.** Nothing is told when the cache frees a block, because the
  front end frees them while a frame may be in flight. Streams that are no
  longer drawn give their space back when the buffer fills, and a released
  range returns to the allocator only after every frame that could have read
  it has finished. A device that cannot give the buffer keeps every stream in
  the rings and says so once.
- **Qualification.** `r_vkStaticGeometry 2` compares every resident stream
  with its source at every draw and streams it again on a difference. It reads
  back from device-visible memory, so it is slow: a drill, not a mode to play
  in.

`rendererVulkanMemoryInfo` adds a line:

| Field | Meaning |
| --- | --- |
| `resident`, `residentBytes` | Streams in the persistent buffer and their size |
| `tracked`, `retired` | Cache blocks being watched, and released ranges still waiting for their frames to finish |
| `hits`, `hitBytes` | Draws served from the buffer and the bytes they did not copy (once per stream and frame) |
| `promotions`, `promotedBytes` | Streams copied into the buffer |
| `recycled` | Headers seen again holding another allocation |
| `evictions`, `declined` | Idle streams removed under pressure, and promotions refused because the buffer was full |
| `mismatches` | Differences `r_vkStaticGeometry 2` found (always zero so far) |

Measured in one launch, with the cache turned off and on in interleaved
windows at the same view (the machine was shared with other work, so the
times are relative):

| Workload | Upload time per frame, off to on | Ring traffic per frame, off to on |
| --- | ---: | ---: |
| Air Defense 1 skyline, still (3 x 600 frames each) | 439 to 238 µs (-46%) | 2.67 to 1.27 MB |
| Air Defense 1 skyline, 360 degree sweep (2 x 8 s each) | 527 to 262 µs (-50%) | 3.48 to 1.86 MB |

The resident set is small: 118 to 233 streams, 1.8 to 2.8 MB, serving 1.35 to
1.63 MB of draws a frame. What still goes through the rings is index data
(OpenGL also sends client-memory indexes with every draw by default,
`r_useIndexBuffers 0`) and per-tic animated models.

Output is unchanged. Frozen captures on Air Defense 1 are bit-identical with
the cache off, on, in the comparing mode and after a `vid_restart` that
recreates the buffer, and on Hub 2 with it off and on. The comparing mode
found no difference in about 60,000 compared draws, across a view sweep on each map
and a change of map in one session, where 1,841 recycled headers were
recognised. The validation layer reports nothing.
`tools/tests/renderer_vulkan_static_geometry.py` runs the production functions
against an allocator double: the promotion delay, recycled headers, the frames
a released range waits, the exclusions, the comparing mode, eviction under
pressure and a device that refuses the buffer.

This is the first optimization promoted under VK-06. It does not yet carry
the frame-time tail measurement VK-06 asks for: the sampling profiler and a
quiet machine were not available after the change, so the evidence is the
upload time above, not presented frame times. Index streams of static
surfaces are the natural next step. They need an identity for a surface's
index array first, which today is a bare pointer.

## Shadow-map savings from OpenGL (24 November 2026)

OpenGL had two shadow-map savings Vulkan lacked: point shadow cubes drawn as
one layered target (`r_shadowMapPointLayered`) and the cached still scenery of
a cascaded light's whole-light cascade (`r_shadowMapCascadeFarCache`). Both
were built for Vulkan and measured before being kept. The first landed. The
second was left out.

### What the shadow pass costs

The Vulkan shadow report (`r_shadowMapReport 1`, the `Vulkan shadow cache:`
line) gained what the comparison needed and keeps it:

| Field | Meaning |
| --- | --- |
| `casterDraws`, `casterIndexes` | Indexed draws the caster walks recorded for the view, and their indexes. They do not depend on load |
| `cubeScopes` | Rendering scopes the point cubes opened |
| `cpuMs=prepare/render` | Wall time of the view's shadow preparation and of recording the shadow pass, on the recording thread |

`r_shadowMapReport 2` also prints `seed=union`, `copy` or `none` for each
light: how a freshly drawn GLOBAL ownership map got its LOCAL map's depth.

Every figure below is Vulkan at the Quality preset, 2560x1440, RTX 4060
Laptop (591.74), from hidden-window launches with the two states alternated
in windows of one launch. Other projects were building on the machine
throughout, so whole-frame times were unusable and are not quoted. GPU times
are timestamp spans (`r_vkGpuPhaseProfile`, and the report's own
`Vulkan shadow timings:`), which that load does not reach. CPU times are the
median and the floor of the report's `cpuMs` over about seventy views a
window. For scale, an Air Defense 1 frame took about 6 ms of GPU and 3.3 ms
of CPU in the one quiet launch.

| Scene | Caster draws | Shadow pass, CPU | Shadow pass, GPU |
| --- | ---: | ---: | ---: |
| Air Defense 1 skyline, still | 81 | 0.13 ms | 0.12 ms |
| Convoy 1 start, still | 566 | 0.25 ms | 0.56 ms |
| Hub 2, turning, shadows stored | 26 | 0.15 ms | 0.41 ms, of which 0.27 ms copying stored tiles |
| Hub 2, still, static cache off (ten cubes redrawn) | 852 | 0.74 ms | 0.89 ms |
| Air Defense 1 battle (the performance suite's wave), frozen | 592 | 0.31 ms | 0.28 ms |

### Layered point cubes (landed)

A point cube was six rendering scopes, one a face, and twelve when it was
published and composed. It is now one scope over a six-layer attachment:
`shadow_point_caster_layered.vert` is the caster vertex stage with one more
line, `gl_Layer`, set from the cube face the push block already carried. The
faces are still walked one at a time with their own matrices and per-face
caster culling, so each face draws exactly the casters it drew before. One
clear covers the six faces. Composing loads the six layers once and draws
the moving casters over the copy.

Writing `gl_Layer` from a vertex stage needs
`VK_EXT_shader_viewport_index_layer`, which the device enables where it is
offered. Without it, or for a cube whose layered view could not be made, the
faces are drawn one scope at a time as before. `r_shadowMapPointLayered 0`
forces that for comparison. The log says which path a device takes
(`Vulkan: point shadow cubes ...`). The sealed shared-interaction stream uses
the same scopes.

| Workload | Cube scopes a view | Shadow pass CPU, median | CPU, floor | Point pass GPU |
| --- | ---: | ---: | ---: | ---: |
| Hub 2, still, static cache off: ten cubes redrawn a view | 60 to 10 | 0.74 to 0.60 ms, 6 of 6 pairs | 0.54 to 0.37 ms, 6 of 6 | 0.25 to 0.24 ms, 4 of 6 |
| Air Defense 1 battle, frozen: three cubes composing | 18 to 3 | 0.31 to 0.24 ms, 7 of 8 | 0.21 to 0.19 ms, 8 of 8 | 0.14 to 0.13 ms, 8 of 8 |
| Hub 2, turning, shadows stored (the two states from two launches) | 7.2 to 1.2 | 0.15 to 0.15 ms | 0.08 to 0.08 ms | 0.06 to 0.07 ms |
| Air Defense 1 battle, turning | 26 to 4 | 0.38 to 0.39 ms, 3 of 6 | 0.24 to 0.24 ms | 0.20 to 0.17 ms, 4 of 6 |

A scope costs about three microseconds to record. The saving is therefore
where cubes are redrawn or composed, and nothing where every lamp in view is
read from its stored cube: the two turning rows change by less than their
windows differ from one another, the battle's because the fight itself
differs from window to window.

The first row was repeated after the day's other shadow work was merged in,
at a quieter moment (four pairs): the pass's CPU median fell from 0.40 to
0.28 ms and its floor from 0.27 to 0.22 ms, and the point pass's GPU time
from 0.24 to 0.18 ms, each in four pairs of four.

Frozen captures (`g_stopTime 1`, 1920x1080, compared above the animated HUD)
are bit-identical layered, face by face and layered again: Hub 2 at four
poses with the static cache off, Hub 2 at four poses with shadows stored, and
the Air Defense 1 battle at three poses, composing. A capture with shadows
off, taken in the same launches, differs from all of them. The validation
layer reports nothing on either path.

The laptop's Intel Iris Xe (101.5590) takes the layered path too. The
validation layer reports nothing there, and captures at two Hub 2 poses are
bit-identical layered and face by face with temporal anti-aliasing, motion
blur, SSAO and bloom off. With those on, two captures of the same state
differ on that adapter by more than the two paths do, so they say nothing
either way.

Also measured and dropped: on the face-by-face path, opening no scope for a
composed face that no moving caster reaches. It removed 3.7 of 7.2 scopes a
view on Hub 2, and the time it saved could not be told from none. Only a
device without the extension would ever run it.

### Whole-light cascade cache (measured, left out)

OpenGL keeps the still scenery of a cascaded light's whole-light cascade in an
atlas cell and draws only moving casters over it. On OpenGL that slot was 161
of 439 cascade caster draws on Air Defense 1.

The Vulkan prototype did the same within the scopes the composed projected
passes already use, because a depth tile cannot be copied inside a rendering
scope: the fresh scope drew the light's global static chain alone into the
cascade's tile in the view that published it and nothing otherwise; the cache
phase stored the tile in a projected cache record of its own (one tile,
counted against the projected cache budget) or copied it back; the compose
scope drew the other casters over it. It stayed out of the sealed transaction
and off the copy that seeds a fresh GLOBAL map from its LOCAL block. Where the
receivers unite the two maps instead it applied to the LOCAL map only. That
last point decided whether it did anything at all: Air Defense 1's sun is on
the union path in every view, so a version that avoided the union never
engaged there.

Frozen captures with the cache off, on, on again and off were bit-identical
on Air Defense 1 at four poses and on Convoy 1 at two of three; the third
differed equally between its two off captures.

| Scene | Caster draws a view | Shadow pass CPU, median | Shadow pass GPU |
| --- | ---: | ---: | ---: |
| Air Defense 1 skyline, still (the sun on the union path) | 81 to 62 | 0.13 to 0.12 ms, 7 of 8 pairs | 0.12 to 0.15 ms, worse in 4 of 5 |
| The same with 32-bit float depth (`r_vkShadowDepthFormatTest 1`) | 83 to 61 | 0.20 to 0.18 ms, 5 of 6 | 0.08 to 0.11 ms, worse in 5 of 6 |
| The same at Ultra | 79 to 59 | 0.19 to 0.15 ms, 4 of 6 | 0.20 to 0.32 ms, worse in 5 of 5 |
| Air Defense 1, turning | 146 to 136 | not resolved | 0.14 to 0.18 ms, worse in 4 of 4 |
| Convoy 1, still (both ownership maps drawn in full) | 566 to 390 | 0.25 to 0.21 ms, 5 of 6 | 0.56 to 0.56 ms |

Why it does not pay on Vulkan:

- **There is less to save.** A caster draw costs Vulkan well under a
  microsecond to record, and the union already keeps the global chains out
  of the GLOBAL map, so the slot is about 20 draws on Air Defense 1, whose
  sun has 37 static casters, where it was 161 on OpenGL.
- **The copy costs what the drawing did.** Copying the tile back takes about
  0.05 ms on this GPU with packed 24-bit depth (0.02 to 0.07 ms across the
  runs), 0.03 ms with 32-bit float depth and 0.12 ms at Ultra. Copying the
  stencil aspect with the depth made no difference. Drawing a few dozen
  static casters into the same tile takes less.

So the mechanism trades 0.01 to 0.04 ms of CPU for 0.03 to 0.12 ms of GPU
where the GPU is the limit, and breaks even on Convoy 1. Vulkan keeps
redrawing the slot, and `r_shadowMapCascadeFarCache` stays an OpenGL switch.
`tools/tests/renderer_vulkan_shadow_compatibility.py` pins both outcomes: the
layered path and its fallback, and that the Vulkan back end holds no
whole-light cache.

### What the measurements exposed

- **Depth tile copies are slow.** Hub 2's shadow pass spends 0.27 of its
  0.41 ms of GPU time in the cache phase, copying stored projected tiles into
  the atlas so that moving casters can be drawn over them. Reading stored
  tiles in place (`r_vkShadowSampleResident`) avoids the copy only where
  nothing is composed.
- **One cutout caster doubles a cascaded light.** The union and the copy
  require every global caster to be opaque. Convoy 1's sun has five to seven
  cutout casters, so both of its ownership maps draw every chain into every
  cascade: 566 caster draws and 0.49 ms of GPU a view, against 81 draws on
  Air Defense 1.
