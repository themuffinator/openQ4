# Temporal Presentation

Milestone E adds a temporal presentation path shared by OpenGL and Vulkan. It
combines delayed GPU-time dynamic resolution with native-resolution temporal
accumulation, while preserving the existing SMAA/spatial path as the immediate
rollback. Since 2026-10-06 `r_temporalAA` defaults to `2` (automatic): TAAU
replaces the bilinear stretch whenever the 3D scene renders below native
resolution (`r_screenFraction < 100` with scale mode `1` or `2`, or dynamic
resolution), and stays off at native resolution. It is never automatic on the
OpenGL ES module, which has no temporal resolve, or on an OpenGL context
without GLSL 1.30. `R_MigrateLegacyTemporalAADefault` moves an archived `0`
to `2` once (`r_temporalAADefaultMigrated`).

## Ownership Contract

The renderer selects one immutable presentation state at `BeginFrame`:

- `sceneWidth` and `sceneHeight` size the game-owned 3D and authored-post chain;
- `outputWidth` and `outputHeight` size the temporal histories and final output;
- 3D rendering and authored scene effects complete before temporal resolve;
- HUD, menus, console, and other root 2D views are submitted after that resolve
  directly at native output resolution.

Renderer ABI v11 exposes this frame state and one append-only temporal-resolve
entry point to both game modules. The SP authored-post path owns two native-sized
history render targets; direct and MP paths that do not use that post chain use
an equivalent lazy backend-owned pair. Either owner swaps only after the exact
frame/generation resolve is accepted. A generation change, native extent change,
video/backend restart, map/session discontinuity, camera cut, capture, missing
current-frame depth stamp, or rejected write invalidates continuity. A dynamic
scene-size change does not: history is native-sized, so retaining it is required
for TAAU.

No loose material or shader asset is required. Backend resolve programs are
engine-owned resources, and retail Quake 4 materials remain unchanged.

## Dynamic Resolution

`r_rendererDynamicResolution 1` enables the controller and automatically enables
the backend-neutral whole-frame timing stream needed by it. OpenGL uses the
existing four-slot timestamp-query ring; Vulkan reads timestamps only after the
ordinary frame fence for that slot retires. Neither backend waits for a current
frame query.

The controller associates each delayed sample with the exact scale of its source
frame, rejects stale/out-of-order/generation-mismatched samples, drops resolution
quickly above budget, and raises it only after an under-budget streak. Dimensions
are bounded and aligned before target allocation. Unsupported timing keeps the
configured safe ceiling and reports `dynamicActive=0`.

Manual `r_screenFraction` scaling remains available when the automatic
controller is off. With temporal AA active, even mode `0` uses the scene-target
route so TAAU receives the requested low-resolution scene instead of the legacy
back-buffer crop path. Root UI-only frames never enter either scaling route.

Relevant controls:

| CVar | Default | Purpose |
|---|---:|---|
| `r_rendererDynamicResolution` | `0` | Enable automatic 3D scene scaling |
| `r_dynamicResolutionMinScale` | `50` | Minimum automatic scale, percent |
| `r_dynamicResolutionMaxScale` | `100` | Maximum automatic scale, percent |
| `r_dynamicResolutionTargetMsec` | `0` | Explicit GPU budget; `0` derives it from presentation timing |
| `r_dynamicResolutionTargetUtilization` | `90` | Share of the presentation interval available to GPU work |
| `r_dynamicResolutionDropStep` | `5` | Maximum percentage-point drop per over-budget sample |
| `r_dynamicResolutionRaiseStep` | `2` | Percentage-point increase after a recovery streak |
| `r_dynamicResolutionRaiseFrames` | `30` | Retired under-budget samples required before a raise |
| `r_dynamicResolutionAlignment` | `8` | Scene-dimension alignment |
| `r_dynamicResolutionCaptureNative` | `0` | Force known captures to native scene resolution instead of freezing scale |

`rendererTemporalPresentationStatus` prints the latched extents, delayed sample
identity and age, target budget, controller decision, image-history generation,
and capture state.

## Temporal History And Motion

An active temporal request replaces the game SMAA tail only when the renderer
accepts a validated temporal resolve command. Rejection leaves the established
SMAA or spatial final pass in place. The resolve uses an eight-sample Halton
sequence, previous camera projection, depth reprojection and per-pixel object
velocity.

### Per-pixel ownership

The velocity pass (`RB_RenderMotionVectorBuffer`, `VK_Post_RenderMotionVectors`)
owns every visible moving surface per pixel. `R_ScenePackets_TemporalSurfaceMotion`
classifies each view drawSurf for both backends:

| Domain | History treatment |
|---|---|
| Static world (no entity, or a static world area model) | Not drawn: camera/depth reprojection is exact |
| Rigid entities (static model, no callback) | Exact vector from the previous model transform |
| Posed (skinned MD5) surfaces | Exact vector from the previous transform and each vertex's previous model-space position |
| First-person weapon | As above, with the weapon's own depth-hack projection on both frames and its 0..0.5 depth range |
| Particles/BSE, in-world GUI, subview and post surfaces | Reactive coverage of the pixels they cover |
| Translucent entity surfaces, unposed dynamic models, newly visible entities, packed MD5R | Reactive coverage |

The previous positions come from the front end. `idMD5Mesh::UpdateSurface`
reuses each entity's triangle surface, so just before a new pose overwrites
`verts`, `R_TemporalPresentation_CapturePreviousPositions` copies the pose that
was drawn on the immediately preceding frame (`positionsFrame == frame - 1`;
animation-LOD frames that redraw the old pose keep the chain continuous). A
posed surface that is drawn again without being posed at all this frame (game
time stopped, a render frame between two game tics, an entity at rest whose
cached model was kept) records the same thing from
`R_TemporalPresentation_PreviousPositionCache`: its previous positions are its
current ones, and the vector comes from the model transform. Such frames used
to have no capture, so the surface was reactive on each of them. For a
main-view drawSurf whose geometry is still that surface, `R_AddDrawSurf`
uploads the capture as a frame-temp `idVec3` stream
(`drawSurf->previousPositionCache`). OpenGL binds it to texture coordinate 1;
Vulkan binds it at vertex binding 1 (`VK_EXTRA_VERTEX_POSITION_PREVIOUS`). A
skinned surface without a capture, a material deform's replacement geometry
and packed MD5R stay reactive.

The RGBA16F velocity target carries the vector in `rg` (scene pixels), the
reactive amount in `b` and an exact-vector flag in `a`. Reactive surfaces write
only `b` (`glColorMask`, or a Vulkan pipeline with R/G/A masked), and only where
they are not behind the finished opaque depth, so an exact vector underneath
survives. The amount is `r_temporalAAReactiveEffects` (0.85) for effects and
translucent geometry, 0.5 for in-world GUIs and 1 for subview and post surfaces.
A complete pass reports per-pixel ownership, and the shared packet policy then
adds no screen regions (`R_ScenePackets_BuildTemporalViewMotionPolicy(...,
perPixelOwnership)`). Only a pass that could not draw a surface falls back to
the two conservative regions, or to the whole view when the policy is
unavailable. Root 2D UI is never part of the temporal history or scene scaling.

`rendererTemporalPresentationStatus` prints the last pass's counts:
`Temporal ownership: rigid= posed= weapon= reactive= missed= complete=`.

### Jitter and the history grid

The history is an image on the unjittered output grid: what the player sees is
that image, sharpened. Each frame is drawn with a sub-pixel projection offset
(eight-sample Halton), so three rules keep that offset out of the history
(issue #216):

- **No reprojection carries jitter.** The camera history stores each frame's
  jitter-free projection offsets (`projectionMatrix[8]`/`[9]` before
  `R_SetupProjection` adds the sample). `R_TemporalPresentation_FinalizeViewProjection`
  publishes the previous camera as those offsets plus the jitter applied to the
  current frame (`TemporalHistoryCore_AlignPreviousProjectionOffsets`, the
  rule motion blur already used). The current frame's depth is reconstructed
  with its own jittered projection, so reprojecting a scene texel through the
  aligned previous camera returns that texel plus its motion and nothing else:
  a still camera maps every pixel onto itself. The resolve samples history at
  `output position + (reprojected texel - texel)`. OpenGL's velocity pass takes
  the same aligned camera from the view; Vulkan aligns the camera it keeps for
  its own velocity history (`VK_PostProcess_TemporalMotionVectors`). The world
  position is formed relative to the camera, not in absolute coordinates,
  whose rounding was a constant sub-pixel history offset per pixel.
- **The scene is sampled where the output pixel lies.** At native resolution
  the raw jittered samples are the sub-pixel samples the history integrates.
  When the scene is smaller than the output (TAAU), each output pixel samples
  the scene recentered by the applied jitter, so successive frames register on
  the output grid. The jitter comes from the projection itself
  (`R_TemporalPresentation_AppliedJitterUV`: half the difference between the
  applied and jitter-free offsets), which holds for every viewport and scene
  target route.
- **What the jittered view did not draw is sampled in place.** A portal sky is
  drawn without jitter into the same target before the main view. Where depth
  is still cleared at both positions and no reactive surface covers the pixel,
  the recentered position would move that backdrop by the jitter.

Where history is rejected (reactive coverage, no usable history, the first
frame after a cut) the current frame shows through; at native resolution that
share is taken from the recentered frame, so the surface does not shake by the
jitter. The pass that presents the resolved history never recenters it.

Before this, history was reprojected through each frame's jittered projection.
With a still camera every stored frame was resampled onto the current frame's
jitter, so the presented image moved by the whole jitter step each frame (up
to 0.99 px at native resolution and 1.78 px at 50% scale) and was softened by
the resampling.

### Resolve

Both resolve shaders (`draw_common.cpp` builtin, `temporal_resolve.frag`) share
one algorithm:

- a 3x3 neighbourhood in YCoCg gives a mean/variance box (1.25 sigma times
  the upscale factor, at most 3.75, intersected with the sample range);
  history is clipped toward the box centre rather than clamped per channel. A
  low-resolution frame under-samples what the history holds per output pixel,
  so its box is wider;
- when the nearest-depth texel of the 3x3 carries an exact object vector, that
  vector is used, so a moving silhouette carries its motion onto the
  background texels around it; a still occluder never pins the moving
  background beside it (each texel then keeps its own velocity);
- history is reconstructed with a five-tap Catmull-Rom filter, not bilinear;
- reactivity comes only from the per-pixel reactive channel (times
  `r_temporalAAReactiveScale`), the fallback regions, and depth disocclusion
  that only applies while the pixel moves. Colour differences are not reactive:
  they are what clipping handles, and treating them as reactive switched
  anti-aliasing off at exactly the high-contrast edges that need it;
- feedback (`r_temporalAAFeedback`, 0.9) falls by up to 30% for fast motion,
  and the blend is luminance-weighted so a single bright sample cannot flicker.
  A pixel at rest (under 0.05 output pixels per frame, fading out by 0.5) takes
  half the usual share of each new frame, and proportionally less again when
  upscaling: 0.945 at native resolution, 0.9675 at 50%. That halves the ripple
  each jittered sample leaves on a still edge; nothing at rest can ghost.

The image the player sees gets contrast-adaptive sharpening
(`r_temporalAASharpness`, 0.5, AMD CAS cross pattern). It is applied only when
the resolved history is presented (OpenGL: the scene present program; Vulkan:
flag 16 of the resolve block, with the amount in `reactiveRect1.x`, which a
history-less present does not read), never written back into the history.

Vulkan keeps TAA transform history separate from motion blur. It commits that
history only after the matching color history write, and requires the previous
backend frame, view identity, generation, model identity and scene extent to
match. Captures, cuts, resizes, missing previous geometry and failed resource
admission cannot reuse old vectors. Its single-sample RGBA16F velocity target
tests fragments against the current resolved scene depth, including MSAA scenes.
Vectors carry camera and object motion only (the stored previous camera is
aligned to the current frame's jitter before the pass), use scene-pixel units,
and scale to native presentation pixels in the resolve. Unsupported geometry
keeps its explicit reactive ownership.

`rendererTemporalPresentationStatus` also reports Vulkan's eligible/drawn rigid
surfaces, whether the last vector pass was complete, and completed vector views.
The stock HDR gameplay gate checks that completed vector views increase during
each scaled/restarted interval. A single frame can correctly reject history
after a time discontinuity or newly visible entity; cumulative freshness proves
the real production draws without requiring unsafe reuse on that frame.

`r_temporalAAFeedback`, `r_temporalAAReactiveScale`, `r_temporalAAReactiveEffects`,
`r_temporalAASharpness` and `r_temporalAADebug` control maximum history weight,
rejection strength, effect coverage strength, present sharpening and the
velocity/reactive/history-weight diagnostic views.

## Cuts And Captures

Per-view identity includes the render world/map, root or subview chain, view id,
flags, and capture surface provenance. History is rejected for identity or
generation changes, native output resizes, time discontinuities, teleports,
large rotation cuts, and projection cuts. Equal simulation times remain valid
so high-refresh presentation can accumulate between game tics.

Known screenshot and save-preview frames render without temporal jitter. A
capture that begins after the game queued its scene marks the source timing
sample ineligible, advances only the image-history generation, and makes both
backends bypass history reads and writes. The current scene is spatially
presented for readback, then ordinary history starts cleanly on a later frame.

The stock renderer remains a numeric SDR pipeline: temporal targets preserve the
same UNORM/code-value contract as their source, and neither resolve shader adds
an sRGB or gamma transfer. Native UI is composed after the scene resolve.

## Motion Blur

Motion blur (`r_motionBlur`) is a separate consumer with its own camera
history. It runs inside the main 3D view after SSAO and before bloom, on the
jittered, possibly scaled scene, and so before the temporal resolve. Both
backends route every policy decision through `src/renderer/MotionBlurCore.h`;
OpenGL (`RB_STD_MotionBlur`) and Vulkan (`VK_Post_MotionBlur`) keep only their
state structures and GPU work.

- **History.** `MotionBlurCore_ValidateHistory` rejects the previous camera
  after a video restart, a world/map change, a viewport change, an authored
  camera cut (`renderView.forceUpdate` from `Camera.cpp`; screenshot frames also
  set it, so capture frames are excluded), time running backwards, a gap over
  100 ms, a teleport over 512 units or a projection change. A still camera is
  rejected only without object vectors. Equal presentation times are not a
  discontinuity: high-refresh frames can share a millisecond, and a paused game
  keeps its history with zero motion. Subviews, portal skies and 2D views pass
  without reading or writing the main camera's history.
- **Temporal AA jitter.** `R_SetupProjection` records the jitter-free
  `projectionMatrix[8]`/`[9]` in `viewDef->temporalNeutralProjectionOffset`
  before adding the sub-pixel offset; the OpenGL direct fallback may later
  recenter the applied matrix, so the neutral values are stored, not derived.
  The history test compares neutral offsets, and the previous camera is
  aligned to the current frame's applied jitter
  (`MotionBlurCore_AlignPreviousOffsets`), so a depth-reconstructed point
  reprojects to the current pixel plus its motion and neither frame's jitter
  appears as velocity. Before this, every temporal AA frame read as a
  projection change and motion blur never drew while TAA was active.
- **Exact object vectors.** With `r_motionBlurObjectVectors 1` the velocity pass
  uses the same per-surface classification as temporal AA
  (`R_ScenePackets_TemporalSurfaceMotion`): rigid entities from their previous
  model transform, posed (skinned) surfaces from previous model-space
  positions, the view weapon with its own depth-hack projection on both frames.
  Previous positions are captured when either consumer wants them
  (`R_TemporalPresentation_PreviousPositionsRequested`). Surfaces without an
  exact vector (effects, translucency, GUIs, newly visible entities) are left
  to camera reprojection, except that a view weapon without one is written as
  zero motion: it moves with the camera, and reprojection used to smear it
  whenever the view turned. Motion blur's entity history is independent of
  temporal AA's but built by the same collector.
- **Frame-rate independence.** Velocities measure the motion between two
  presented frames. `MotionBlurCore_ShutterScale` multiplies them by
  `r_motionBlurShutterMsec` (16.667) over the smoothed presentation interval
  (an exponential average of integer-millisecond `renderView.time` deltas,
  clamped to 1/8–8x), so a camera speed blurs by the same length at 60, 144 or
  240 Hz. `r_motionBlurShutterMsec 0` keeps the original per-frame length. The
  scale travels in `motionBlurObjectParams.z` to `motionblur.fs` and
  `post_motionblur.frag`; the Vulkan SPIR-V is the committed
  `post_shaders_spv.h`.

`rendererMotionBlurStatus` reports cumulative counters since the last
`rendererMotionBlurStatus reset`: requested and drawn main views, views with an
object-vector target, views that cancelled temporal AA jitter, subview skips,
disabled views with the last reason, every history decision, exact rigid/
posed/weapon surfaces, zero-motion weapon surfaces, camera-reprojection
fallbacks, and the effective strength, length cap, samples, shutter scale and
smoothed frame interval. Its camera line gives the mean centre-of-view
rotation speed per frame and after the shutter scale, which is the
frame-rate-independent input the qualification compares.

### Supported combinations

Motion blur is supported with temporal AA (forced or automatic), dynamic or
manual resolution scaling, supersampling, MSAA, HDR tone mapping, bloom and
SSAO on OpenGL and Vulkan, and with `r_vkRenderThread 1`/`2`. Below 10 FPS
every interval exceeds the 100 ms gap and the scene stays sharp. Motion blur
needs an offscreen scene target, so on Apple's OpenGL 2.1 compatibility profile
it removes MSAA from the 3D scene (offscreen targets cannot be multisampled
there; see [display settings](../user/display-settings.md#anti-aliasing-settings-new)).
Vulkan's MSAA stops at 8x whatever the request. `r_jitter` (multi-frame
screenshot accumulation) and `r_skipPostProcess` disable it, and the status
command names the reason.

### Motion blur qualification, 6 November 2026

`tools/tests/renderer_motion_blur_qualification.py` (REN-08) is the opt-in
runtime gate. Each case launches the staged client into `game/airdefense1`,
pans with `benchmarkViewSweep`, and reads `rendererMotionBlurStatus`; the
mirror case uses the Milestone D mirror fixture
(`renderer_milestone_d_fixture.py`, `--mirror-runtime`). Windows x64, RTX 4060
Laptop GPU, 2560 x 1440 windowed, uncapped unless stated, on a shared machine:

- Reproduced before the fix: with `r_temporalAA 1` neither backend ever drew a
  motion blur pass during a 360-degree pan.
- All 41 backend cases pass on OpenGL and Vulkan: baseline, forced temporal AA,
  75% automatic upscaling, HDR + bloom + SSAO + temporal AA, 150%
  supersampling, 4x MSAA, 60 FPS and uncapped with and without the shutter,
  `g_stopTime` pause, 8 FPS, camera-only vectors, `r_vkRenderThread` 1 and 2,
  dynamic resolution, the mirror fixture and the weapon debug captures. Every
  moving main view draws (400/400, 240/240), temporal AA cases cancel the
  jitter on every drawn view, and subviews (the portal sky, the mirror) are
  skipped every frame without resetting the history.
- Frame-rate coherence: the shutter-scaled centre speed uncapped against 60
  FPS is 0.990 on both backends; with `r_motionBlurShutterMsec 0` it is
  0.42 (GL) and 0.39 (Vulkan), the original frame-rate-dependent length.
- Weapon: in the debug view while panning, mean weapon motion is 0.25 with
  exact ownership against 0.80 with camera-only reprojection, which smears it.
- Expected resets, each a single frame: resuming from `g_stopTime` can step
  presentation time back (`time-reversed`); a frame whose camera did not move
  without object vectors is `camera-still`; at 8 FPS every interval exceeds
  the gap limit and nothing blurs.
- An earlier 4x MSAA GL run failed with 106 frames over 100 ms while other
  workloads shared the GPU; the uncontended rerun passes. Dynamic-resolution
  scale steps used to reset GL's history (2 resets); the history now keeps
  scale-only viewport changes.

Not covered: physical Linux, macOS (including the Apple GL 2.1 profile, whose
restriction is documented rather than measured) and Android, and a human
review of the blur's appearance on the reference display.

## Validation

Dependency-light policy coverage lives in
`openq4-temporal-presentation-core-test` and
`openq4-temporal-history-core-test`. The static cross-repository/backend contract
is checked by `tools/tests/renderer_temporal_presentation.py`. Runtime acceptance
uses windowed engine-TGA captures and mode-specific SP/MP gameplay; operating-
system capture and injected user input are not part of the workflow.

`rendererTemporalPresentationStatus` also keeps cumulative counters until
`rendererTemporalPresentationStatus reset`. Every resolve command, on both
backends, records how it ended: blended with history, seeded fresh history,
capture (history untouched), spatial (temporal AA off) or fallback with a
named reason; and whether its vector pass was complete, partial, empty or not
attempted. Root views count each history decision (`none` is reuse), and
every history invalidation is counted by reason.

`tools/tests/renderer_temporal_qualification.py` (REN-04) is the opt-in
runtime gate built on those counters. It launches the staged client into
`game/airdefense1`, pans with `benchmarkViewSweep`, and takes one snapshot
before and one after an event, on OpenGL and Vulkan. Every case requires
history-backed resolves with complete ownership before the event, the expected
decision for the event, history-backed resolves again afterwards and no
unexplained fallback.

Cases: native temporal AA; 75% automatic upscaling; dynamic resolution; an
unreachable GPU target that drops the scale, then a generous one that
recovers it; stale timing (every sample older than the accepted age, so the
controller resets to its safe ceiling); a `g_stopTime` pause; a screenshot; a
teleport cut; `vid_restart`; switching temporal AA off and on (the SMAA/spatial
rollback); 4x MSAA; `r_vkRenderThread 2`; Vulkan presentation drills
(`r_vkPresentationFailureTest` 3 withheld images, 2 surface loss, 1 device
loss); and the Milestone D mirror fixture.

The first run, 6 November 2026, reproduced three Vulkan gaps, now fixed:

- **Multiple views.** A subview starts as a copy of its parent view, so it
  inherited `temporalPrepared` and the parent's jitter; mirrors, x-ray,
  remote-camera and refraction views were drawn jittered, and on Vulkan the
  direct render of such a view retired the main view's history every frame.
  On the mirror fixture all 390 resolves fell back. `tr_subview.cpp` now
  clears the inherited temporal state (`R_ResetSubviewTemporalState`), so the
  subview is prepared under its own identity without jitter. Vulkan also keeps
  one depth proof per render target, as OpenGL does, instead of one shared
  stamp.
- **Withheld presents.** While the presentation engine withheld every image,
  each renderer command of a frame retried the 1 s acquire: about 27 s per
  frame, so the drill hung the harness. A timed-out acquisition now ends the
  frame's attempts (`acquireTimedOut`); the next frame retries.
- **Device loss.** Recovery inserts `vid_restart` at the head of the command
  buffer, but a pending console `wait` holds the whole buffer, so in the
  harness the renderer stayed down for the full 480-frame wait. Gameplay has
  no pending console wait; the harness now waits frame by frame after those
  drills. After the restart temporal AA resolves with history again.

With these fixes the mirror resolves 390/390 with history and complete
ownership on both backends; withheld presents fall back on exactly the ten
withheld frames; surface and device loss reseed once and continue. The
screen-space qualification below also found Vulkan capture frames presenting
without depth; that fix keeps screenshots' screen-space effects.

Not covered: a human review of ghosting and disocclusion trails on the
reference display (image stability at rest is measured below); physical Linux,
macOS and Android; and runtime capture of the native UI composition, which
screenshots cannot show because capture frames bypass history (the separation
is checked natively by `renderer_native_ui_output.py`).

### Stability qualification, 11 November 2026 (issue #216)

`tools/tests/renderer_temporal_stability.py` is the opt-in runtime gate for
the picture itself. It launches the staged client into `game/airdefense1` at a
fixed skyline pose, stops game time (`g_stopTime`), lets the history converge
and reads the resolved history back on 24 consecutive frames with
`screenshot image` (a readback, not a capture frame; it now also reads the
backend-owned RGBA16F histories `_backendTemporalHistory*` and
`_vkTemporalDirectHistory*`). Between consecutive frames it measures the
global sub-pixel translation (least-squares optical flow) and the share of
pixels changing by more than 4/255, and over the last jitter cycle the mean
distance from the cycle mean.

Windows x64, GeForce RTX 4060 Laptop GPU, 1280 x 720 hidden window, uncapped,
on a shared machine. "Before" is the unfixed tree (55bbe166a for native
resolution, a 10 November build for the upscaled cases):

| Case | Backend | Before: largest step | Before: pixels changing | After: largest step | After: pixels changing | After: ripple |
|---|---|---:|---:|---:|---:|---:|
| Native (`r_temporalAA 1`) | OpenGL | 0.99 px | 13.4% | 0.032 px | 0.05% | 0.05 |
| Native | Vulkan | 0.98 px | 12.7% | 0.030 px | 0.05% | 0.05 |
| Upscaled from 75% | OpenGL | 1.24 px | 12.7% | 0.001 px | 0.03% | 0.03 |
| Upscaled from 75% | Vulkan | 1.24 px | 11.8% | 0.001 px | 0.03% | 0.02 |
| Upscaled from 50% | OpenGL | 1.78 px | 13.5% | 0.003 px | 0.05% | 0.03 |
| Upscaled from 50% | Vulkan | 1.74 px | 12.6% | 0.003 px | 0.05% | 0.03 |
| Native, 4x MSAA | OpenGL | not run | not run | 0.032 px | 0.05% | 0.05 |
| Native, 4x MSAA | Vulkan | not run | not run | 0.030 px | 0.04% | 0.04 |
| 67%, sharpened scale mode | OpenGL | not run | not run | 0.002 px | 0.04% | 0.03 |
| 67%, sharpened scale mode | Vulkan | not run | not run | 0.002 px | 0.03% | 0.03 |

Before the fix the ripple was 0.77 to 0.85 in every measured case. The
multiplayer case (backend-owned history, `mp/q4dm1`) cannot stop game time and
spawns at a random point, so it is judged on the median of a 4 x 3 grid of
screen cells: 0.05 to 0.13 px across runs on either backend, against 1.0 px
for the unfixed single-player resolve by the same statistic. The unfixed build
cannot read that history back, so multiplayer has no direct "before".

While the camera pans (12 degrees per second at 60 FPS, about 3 pixels per
frame) gradient energy is unchanged within run-to-run variation (native 4.83
before and 4.86 after on OpenGL, 4.60 and 4.50 on Vulkan; 50% scale 2.91 and
2.80, 2.51 and 2.52), and the captured frames show no trails.

The unfixed translation matched the Halton jitter step frame for frame (a fit
of 1.1 times the step). What remains at native resolution fits 0.05 times the
jitter sample, which is the at-rest share of one raw sample; an upscaled frame
is sampled recentered and has no such term. In a textured crop, gradient
energy rose from 81% to 100% of the fixed native result at native resolution,
from 58% to 71% at 75% scale and from 40% to 52% at 50%, because history is no
longer resampled every frame. `renderer_temporal_qualification.py` passes
unchanged on both backends (13 cases, 23 runs, no unexpected fallback).

Findings left open:

- **Upscaled texture detail.** At 50% scale the image holds about half the
  native detail. Most of the gap is texture mip selection: the scene is
  rendered, and its mips chosen, at the lower resolution. Temporal upscaling
  needs a negative texture LOD bias (log2 of the scale) on both backends to
  recover it; no LOD-bias plumbing exists yet.
- **Vulkan texture sharpness.** In plain frames without temporal AA, the same
  crop has 92% of OpenGL's gradient energy under Vulkan. The resolve preserves
  that difference; it is not a temporal AA effect.
- **Reactive surfaces.** A surface that rejects history shows the recentered
  current frame: it does not move, but its edges are not anti-aliased and their
  aliasing changes with the jitter phase. Multiplayer cannot stop game time,
  so its case holds only the global translation to a limit.

`rendererVulkanTemporalMotionSelfTest` numerically reads GPU velocity and resolve
attachments for positive/negative translation, rotation, perspective interpolation, camera plus jitter,
invalid previous clip positions, asymmetric depth occlusion and scaled scissors.
Thirty-six fixtures cover two scene extents and both stored image origins;
missing geometry, new entities and
stale view/frame/generation/extent/capture state must reject exact ownership.
The required `renderer-vk-temporal-motion-selftest` matrix case repeats them
after full restart with active validation. These tests establish numerical
behavior, not moving-scene visual parity, performance or platform promotion.

The 2026-10-06 per-pixel ownership was checked in `game/airdefense1` (devmap,
1280x720 windowed, 60 fps cap), on OpenGL and on Vulkan with
`r_vkValidation 1`. History was read back with `screenshot image
_temporalHistoryAlbedo0`; a plain `screenshot` renders a capture frame that
bypasses history. Every captured frame reported complete ownership with no
missed surface: 53 rigid, 11 posed (2 of them the weapon) and 73-78 reactive
surfaces at the spawn view, and 3 rigid, 11 posed and 23 reactive in the
middle of a `benchmarkViewSweep 90 3000`. Vulkan logged no validation message.
Against the previous resolve (the same view on a065b928), the weapon outline
and the long wall-top edge lost their stair-steps. At `r_screenFraction 75`
the automatic mode engaged (960x540 into 1280x720) and replaced the bilinear
stretch's blocky edges with smooth ones; at `100` it stayed off. The mid-sweep
history showed no ghost trails around the weapon or the skyline. The
sharpened image the player sees cannot be captured by `screenshot`; its CAS
step was evaluated by applying the identical filter to the read-back history.
