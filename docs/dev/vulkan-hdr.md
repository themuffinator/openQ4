# Vulkan HDR scene and exposure

Status: implemented with local Windows stock-map and GPU correctness evidence;
visual/platform qualification remains open. Vulkan is supported on qualified
Windows x64 NVIDIA/Intel configurations; Linux and macOS remain experimental.
This describes internal scene range
and tone mapping, not HDR10/scRGB display output or full stock-material
linearization.

## Controls and ownership

| Control | Default | Vulkan behavior |
|---|---|---|
| `r_hdrToneMap` | `0` | Enables the existing tone-map/color-correction pass. |
| `r_hdrSceneTarget` | `1` | Selects an RGBA16F scene when tone mapping or an HDR debug view is explicitly enabled and post processing is allowed. Ordinary default rendering retains its LDR behavior. |
| `r_hdrAutoExposure` | `0` | Meters scene luminance before bloom/tone mapping and adapts exposure within the configured limits. |
| `r_hdrAutoExposureAsync` | `1` | Uses completed frame-slot readbacks. `0` deliberately submits and waits without presenting the partial frame, then resumes recording. |
| `r_hdrExposure` | `1` | Multiplies the automatic exposure; remains the exposure control when automatic exposure is disabled. |

Canonical in-tree game code selects RGBA16F for the forward scene and
its MSAA resolve. Game post-AA targets remain LDR, because engine tone mapping
runs before authored post effects, SMAA and HUD composition. The Vulkan-owned
direct/scaled scene target follows the same HDR request. A live HDR request
change rebuilds game targets; backend target format changes retire old image
storage behind the existing fences.

The authored SMAA programs use GL-style texture coordinates. Images record
their stored row order, including uploaded lookup textures, rendered images
and explicit `CopyRender` images. Canonical offscreen views now retain GL row
order through the [framebuffer-orientation implementation](vulkan-image-origin.md).
The material executor supplies an image-orientation bit mask in reserved
uniform slot 12.y. Each SMAA pass converts coordinates after calculating its
neighbor/search offset; reversing just the vertex varying would also reverse
the directional edge/weight algorithm. This fixes the inverted world seen with
an upright HUD. All 174 GL/native orientation and recovery controls at 0x/4x/8x
and four staged stock SP/MP profiles pass locally. The subsequent classic-color
fix also closes all ten previously failing 0x classic scene pairs; those and
eighteen PBR scene pairs differ by at most one byte. The v34 sample-location
work below closes the measured classic and linear HDR multisample edge gap.
The later preview and target-lifetime fixes below close additional bounded
differences. v40's explicit RGBA8 resolve also closes the classic 8x color-edge
SMAA discrepancy. The v44 framebuffer change also closes the 125%/8x PBR
coverage comparison; native HUD anisotropy remains open. See the records in the
[gap ledger](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).

The backend-owned MP/direct scene also honors `r_multiSamples`: multisampled
color/depth resolve into a separate sampled target before post processing.
MSAA alone retains this scene target after HDR/TAA are disabled. Temporal and
spatial effects accept resolved depth only when its current generation is valid.

The luminance pyramid uses the same four-tap logarithmic reduction as the
OpenGL HDR shader. Every level is RGBA16F, halves the dimensions with rounding
up, and clamps edge samples. The final 1x1 pixel is copied into a mapped buffer
owned by its frame slot. Normal consumption occurs only after that slot's
existing fence has completed; no additional queue/device wait is introduced
by asynchronous exposure. Invalid floating-point samples are ignored.

Stock tone mapping preserves values through 0.5 and uses a smooth rational
shoulder above it. The shoulder joins with slope one and reserves meaningful
display range for exposed texture detail; the former .98 knee squeezed those
highlights into nearly white output. OpenGL uses the same stock curve. Its
admitted linear PBR scene keeps the separate filmic curve and single sRGB
encoding; this change does not claim Vulkan scene linearization.

The native composite also contains that linear filmic/output-transfer branch.
With PBR, modern quality and HDR enabled, eligible complete views now
accumulate classic and PBR lighting in separate FP16 color attachments during
the existing light/shadow walk. The completed classic result is decoded and
added to PBR **per sample**, before any MSAA resolve, fog or ordered transparent
replay. Both attachments share the existing depth/stencil image and use the
same blend state. Explicit sample-mask draws avoid requiring optional sample
shading or independent blending. Alpha follows the original primary attachment.

Admission examines the whole view and prepares output resources before taking
ownership. It currently covers simple opaque/perforated classic stages, native
PBR lighting/environment/emission, bounded baked area lighting, fog/blend lights
and fully prepared native PBR transparency. [Baked HDR admission](vulkan-pbr-baked.md)
prepares every receiver before composition and retains fallback for unsupported
grid/material combinations. It declines portal/subviews, scene feedback, custom program
stages, unsupported ambient blending, player/cel overlays,
unprepared or classic lit transparency, and combinations with SSAO, motion blur
or cel ink. Those views retain the existing complete
native path and stock tone curve. The requested mode depends on renderer
settings, not visible PBR counts. Linear ownership ends at the output pass,
before authored post effects and HUD. This bounded path does not complete the
broader HDR/PBR parity requirements.

## PBR previews with tone mapping off

Since Stage C of the [PBR production-readiness plan](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md)
the floating-point preview and the linear HDR scene are the laboratory mode
`r_pbrLinearScene 1` (default 0). Production composition writes every PBR draw,
encoded on its own, into the classic display-referred framebuffer and shares
the classic HDR tone map, so no PBR view changes the presentation of stock
surfaces. The preview now stores those same per-draw encoded values; only its
storage stays floating point until the scene is resolved.

With `r_pbrLinearScene 1`, an eligible view containing authored PBR retains its
values until MSAA resolve even with `r_hdrToneMap 0`. Previously an RGBA8 scene
clipped every covered sample independently: a quarter-covered emissive value
of four became 0.25 instead of one, making bright silhouettes too dark.
Production keeps the classic per-sample limit, exactly like classic glow.

The preview reuses complete-view material admission and separate classic/PBR
accumulation. Its classic attachment keeps the original destination format and
stage-color clamping; only PBR uses RGBA16F. The opaque combine adds the two
numeric domains without decoding classic color. Fog, blend lights and ordered
PBR transparency then finish on a floating-point target sharing the original
depth/stencil image. All prepared transparent pipeline variants are obtained
for that target before their handles change together. A failed preparation
retains the original target and prepared resources.

Before scene post processing, the copy shader averages the float scene's stored
samples at each exact texel and broadcasts the result to every sample of the
original destination. It applies the LDR limit after averaging, matching the
modern OpenGL handoff without an intermediate resolve image. Tone mapping,
automatic exposure and public linear-capture ownership remain separate. Stock-only views do not
allocate a preview target; unsupported complete views retain the native path.
Portal/subviews, scene feedback, custom programs, unsupported classic ambient
blending, baked grids, player/cel overlays and unprepared/classic transparency
remain outside this admission scope.

The v39 investigation found that a native 8x float resolve could darken bright
edges at 150% resolution. At one captured texel, two stored blue samples were
3.998046875 and six were zero: their average is 0.99951171875, but the resolved
image contained 0.98388671875. The resulting LDR value was 251 instead of 255;
SMAA amplified the discrepancy. The explicit shader average preserves those
stored samples and removes that resolve error. This is a scoped PBR preview
change; the linear HDR path retains its existing resolve.

The v39/v40 preview suite exposed a separate 125%/8x coverage mismatch
in an ordinary emissive specimen. At source texel `(833,383)`, OpenGL stores
background in all eight depth samples and zero RGB in every color sample;
Vulkan's resolved RGB is `(2,74,127)`. This one source pixel affects three
display pixels, with a maximum difference of 49. OpenGL 3.3 and 4.5 display
captures are identical. The final copy's depth mask is not responsible: both
the resolved and hybrid GL scene already contain zero there. The later
[viewport investigation](#125-percent-msaa-edge-investigation) compares
uploaded geometry and shader outputs. That retained comparison failed the
two-byte gate; the v44 implementation below passes it without changing the gate.
Replaying the archived v38 Vulkan module reproduces the same display failure
and nonzero source texel `(2,73,126)`, proving it predates the explicit average.
The first multisample depth probe repeated the preceding color data and is
rejected; the corrected probe verifies actual texture bindings, clears the
readback buffer and applies a full readback barrier. All probe captures retain
the clean run's exact rendered images, and no diagnostic code is retained.

The clean v39 build passes all 160 independent preview/recovery controls on
GL and Vulkan at 4x/8x, including the newly permanent 125/150/200% cases.
All forty 4x image pairs pass within one byte; 39 of forty pass at 8x, with
only the coverage case above failing. All 26 image-capture controls, the Vulkan
HDR GPU contract, shader/header checks, and both staged stock SP/MP runs pass.
The latter retain sixteen engine screenshots and no tracked rendering warnings.
Existing stock navigation, precache, item-placement and loading-image warnings
remain separate, as do the SDL3 wrap warning and GL diagnostic performance
notices. Sources, runtime and positive/negative evidence are pinned by
`.tmp/vulkan-gap-closure/msaa-input-parity/checkpoint-v39-preview-average.json`.

The v39 classic 8x color-edge SMAA investigation used temporary engine-side
probes found all 64 stored RGBA8 samples equal across GL and Vulkan at the
affected edge. Native resolve maps the same blue average of 159.375 to 159 and
160 respectively. A later SMAA contrast decision amplifies this one-byte
difference. An explicit attachment-average resolve changed no pixels and was
reverted. The [Vulkan specification](https://docs.vulkan.org/spec/latest/chapters/copies.html#copies-resolve)
leaves native normalized/float resolve precision implementation-defined; this
does not waive the project's existing full-frame comparison gate. Diagnostic
probes are removed from the production capture command. Evidence is retained
under `.tmp/vulkan-gap-closure/msaa-input-parity/`.

v40 replaces native resolve for sampled two-dimensional RGBA8 multisample
attachments with an explicit float average. On the final build, all thirteen
raw-image and display pairs pass within one byte. The classic source is now
exact and its displayed difference drops from 59 to one. GPU fixtures verify
all coverage counts at 2x/4x/8x, repeated resolves and preservation outside
partial extents. The existing mixed-attachment/depth and HDR contracts also
pass. This is separate from the 125% PBR geometry-coverage discrepancy above.
See the [RGBA8 resolve contract](renderer-image-capture.md#rgba8-multisample-resolve)
and `.tmp/vulkan-gap-closure/ldr-resolve/` for current evidence.

`rendererVulkanHDRInfo` and `gfxInfo` report `Vulkan PBR preview:` with its
request, commitment, rejection reason, completed-view count and actual samples.
The GPU HDR test includes 24 additional 0x/4x fixtures: bright samples must
survive until resolve, classic overshoot followed by modulation must retain
LDR clamping, and spatial orientation and alpha must remain correct.

OpenGL uses its existing exact authored fog/blend phase for eligible previews
as well as linear HDR scenes. The phase requires the floating-point scene
target, a single admitted root view and a successful whole-domain preflight;
it does not require tone mapping. Fog geometry, frustum caps and blend stages
run between opaque and ordered transparent draws, before MSAA resolve.
The forward shader skips its clustered fog approximation while retaining the
encoded classic-color contract. Shared fog ownership is consumed only after
the exact phase executes. Failed admission still keeps the native owner.
This uses the default `r_rendererModernLightingParity 0`. Viewport offsets or
extents that do not match the scene target still decline admission.

The v36 comparison passes all 111 full-frame image pairs: 37 cases at each of
0x/4x/8x on both APIs, with exact recovery after scaling, target changes,
HDR/PBR toggles, image reload and partial/full restart. The fixed maximum
difference stays at two display bytes, without region masks. Measured maxima
are one byte at 0x/4x and two at 8x. All seventeen earlier fog/blend failures per
tier now pass with the default GL admission setting. Separate HDR radiance
checks retain correct fog and blend contributions across 22 opaque specimens
and verify transparent background composition against independently hidden
geometry. Twenty-eight additional whole-scene controls pass on actual GL 3.3
and 4.5 compatibility contexts, including exact native fallback with scissoring
disabled and exact restoration. Both staged stock GL SP/MP profiles pass with
sixteen 8x captures, texture reload and partial restart, with no tracked
rendering warnings. Reports, engine images, sources and runtime hashes are
bound by `.tmp/vulkan-gap-closure/preview-fog/checkpoint-v36-preview-fog.json`.
These are local Windows/NVIDIA results for the admitted fixture scope;
broader material and platform qualification remains open.

Local v35 evidence includes all 37 preview/recovery cases on both APIs at
0x/4x/8x, the GPU checks before/after restart, 44 transparent resource controls
and staged stock SP/MP reload/restart gameplay. Twenty image pairs per tier
passed the original two-byte gate (all exact at 0x/4x). Seventeen per tier failed
for HDR-off fog/blend scenes, including diagnostic, ordinary emissive and
shared-walker variants. The GL comparison explicitly enables its experimental
fog parity domain (`r_rendererModernLightingParity 2`). These historical
failures are retained; they motivated the exact OpenGL preview phase above.
The reports and original failed transparent-pipeline run remain under
`.tmp/vulkan-gap-closure/pbr-preview/`, bound by
`checkpoint-v35-pbr-preview.json`. The final 4x SMAA pair passes 27/29 images,
including the five formerly failing HDR-off PBR views. Its remaining failures
are HUD colors and the distinct MSAA policy at 125% resolution.

## 125 percent MSAA edge investigation

The production v44 [framebuffer convention](vulkan-image-origin.md) closes this
specific failure: the 125%/8x display is exact against a fresh GL reference,
and all forty preview pairs differ by at most one value. The following v41
investigation remains the evidence for the original cause and rejected controls.

The v41 investigation retains the original 125%/8x emissive specimen and its
maximum-49 display failure. Captures with temporary probes are byte-identical
to v40. Both APIs receive all 1,223 identical uploaded positions and 6,624
indices; the horizontal, vertical and W transform rows also match exactly.
Actual GL viewport/scissor/sample queries agree with the recorded Vulkan
values after accounting for image orientation.

GL transform feedback captures `gl_Position` from the linked depth and color
programs. A separate Vulkan vertex-storage variant uses the same uploaded
bytes and GUI position expression. This variant is explicitly diagnostic;
it is not a readback from the original rasterizing Vulkan pipeline. All Y and
W results agree. There are 274 small X differences elsewhere in the mesh,
at most 0.00000891 projected pixels, but the vertices forming the affected
silhouette edge have identical XYW results. Vulkan's required Z conversion
is retained. Adding `invariant gl_Position` alone changes no captured pixels.

A second control uses a positive-height Vulkan viewport with matching
sample/scissor reflection and reversed winding state. After accounting for
the deliberate vertical reflection, both its complete raw scene and displayed
image match GL exactly. The post-process intermediate already matches without
reflection because of the existing texture-orientation handoff. This control
is not a proposed rendering mode or a production qualification result.

A CPU model using the captured GPU positions, float32 viewport arithmetic
and nearest-even rounding to eight fractional bits predicts the same edge
difference: GL covers no samples at `(833,383)`, while Vulkan covers sample 3
with triangle 231. Vertex 141 falls on different sides of that rounding
boundary after the opposite vertical viewport calculations. This is a model
of fixed-function behavior; GPU viewport outputs and the Vulkan sample mask
were not directly read. Together with the reflection control, it supports
viewport/rasterization rounding as the cause of this specific mismatch.

The investigation restores all six temporary renderer files exactly to v40.
Both rebuilt/staged PBR capture runs reproduce the original images exactly.
The render-target/HDR GPU contracts and shader/header checks pass, with no
tracked renderer warnings in the clean runs. Broader v40 suites were not rerun
because production rendering source is unchanged.
The original two-byte gate remains unchanged and failed. The next step is a
bounded viewport/coverage compatibility decision with a synthetic edge test;
changing material shading or resolve arithmetic is not supported by this
evidence. The broader material, platform and release gates stay open.
Source/runtime snapshots, accepted controls, rejected diagnostics and the
rounding model are retained under `.tmp/vulkan-gap-closure/clip-coverage/`.
Its `checkpoint-v41-clip-coverage.json` binds the clean restoration and evidence.

## Supersampling and render-target lifetime

Modern OpenGL scene targets retain requested MSAA when `r_screenFraction` is
above 100%, matching Vulkan's scene sampling policy. Color and depth still
resolve before post processing, at the actual scene extent. `gfxInfo` reports
the completed modern resolve before considering the separate classic GL
supersampling policy.

Repeated changes exposed a second defect: OpenGL retained old target sizes
until its 64-entry graph allocation pool overflowed. At 150/200% in the expanded
SMAA sequence, resource preparation failed and the whole view fell back.
The owner now retires the least recently used allocation only when the pool is
full and the allocation has no logical handle in the current frame. Current
frame resources remain protected. Bindings are invalidated after deletion;
an allocation revision also invalidates cached forward/G-buffer attachments,
so driver reuse of an object name cannot preserve an old attachment decision.
The resource dump exposes retirements and this revision for lifecycle tests.

The v37 39-case lifecycle sequence passes on both GL allocation paths, each
recycling 326 allocations before restart and restoring all reference images
exactly. Both 31-case SMAA profiles pass independently at 4x and 8x on both
APIs. All 50/75/125/150/200% scale pairs differ by at most one byte. The strict
cross-API gate still fails for the HUD at both sample counts and classic LDR
color-edge SMAA at 8x (thirteen channels, maximum 59). Thirty-eight independent
linear-capture controls pass; all seventeen shared float images and eighteen
display images are exact. Four GPU contracts and four staged stock SP/MP
profiles pass, retaining 32 stock captures and no tracked rendering warnings.
This Windows/NVIDIA evidence is bound by
`.tmp/vulkan-gap-closure/presentation-parity/checkpoint-v37-presentation.json`;
it does not establish other hardware, platform, performance or soak coverage.

The separate stock HUD comparison is sensitive to sampler behavior. Fixed
simulation tics and an initial time freeze remove animation timing as a
confounder. Five ordinary filtering controls then match exactly over the full
image; the 16x anisotropic control still differs. Reversing the Vulkan shader's
Y derivative did not change those pixels and was rejected. The anisotropic
filtering scheme is implementation-dependent under the
[Vulkan sampling contract](https://docs.vulkan.org/spec/latest/chapters/textures.html)
and [OpenGL specification](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf).
These controls isolate the observed difference; they do not waive the existing
two-byte native-HUD comparison or claim broader hardware parity.

## Multisample locations

`r_vkSampleLocations 1` (the default, read at Vulkan initialization) uses
`VK_EXT_sample_locations` when available to express the standard 2x/4x/8x
pattern in the classic renderer's lower-left convention. Canonical Vulkan
images use an upper-left origin. Using identical numeric offsets without that
conversion reflects the coverage pattern, producing different silhouette
pixels even when the fully covered colors agree.

The device checks each count's grid, coordinate range and precision, plus both
public depth formats. All graphics pipelines use the resulting fixed pattern;
compatible multisample depth images and their transition/resolve barriers carry
the same locations. Single-sample images retain their existing behavior. The
pattern stays fixed until device teardown, including across partial restarts.
Static pipeline state works with dynamic rendering without requiring
`variableSampleLocations`. See the Khronos
[sample locations extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_sample_locations.html)
and [depth layout transition rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-image-layout-transitions).

Unsupported counts retain the native pattern. `r_vkSampleLocations 0` forces
that fallback for diagnostics; it does not disable MSAA. Startup logs report
the compatible-count mask and the device's native-standard-pattern guarantee.
OpenGL sample positions are implementation-dependent: this alignment matches
the measured NVIDIA fixed-location attachments, not an unmeasured guarantee
for every OpenGL driver. Cross-API comparison gates remain unchanged.

Local v34 tests on the RTX 4060 Laptop GPU query the actual GL 4x/8x LDR and
HDR attachment locations. All 72 classic color pairs at 4x are byte-identical;
all 72 at 8x differ by at most one byte. The two 2x controls are exact too.
All 21 linear HDR 4x pairs pass the original absolute 0.005 plus relative
0.002 float gate, and their display images differ by at most one byte. Both
29-case SMAA/recovery runs pass their independent checks; 22 paired images
pass the original two-byte gate. At that checkpoint, five PBR-preview views
with HDR off, the HUD, and the 125% case failed that image comparison. The
latter uses GL's documented zero-MSAA supersampling policy versus actual 4x on
Vulkan. The later preview fix above addresses the five clamp/resolve failures;
the original failures remain preserved in the v34 evidence.
Forced-native 4x/8x controls pass the independent color and validation checks
while reproducing the expected GL edge differences; 4x native fallback is
byte-identical to pre-change Vulkan. The GPU suite and both staged SP/MP
profiles pass with sixteen 8x captures, reload/restart and no tracked rendering
warnings. `checkpoint-v34-msaa-samples.json` binds evidence and retained failures
in `.tmp/vulkan-gap-closure/msaa-samples/`. This is local qualification of sample
alignment, not full material, platform, performance or release qualification.

## Classic material colors

Classic fixed-function material colors retain their OpenGL boundaries in both
the ordinary stage walk and the shared GUI/world ambient adapters. Primary
colors (`SVC_IGNORE`) are clamped before texture modulation on every target.
Vertex/inverse-vertex stage tints are texture-environment constants: their
range is preserved when any active color attachment is FP16, and clamped on
fixed-point targets. The choice follows the actual attachment formats, not
the HDR CVar. Classic masked depth fill always uses clamped primary alpha.
These rules follow the defaults in
[ARB_color_buffer_float](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_color_buffer_float.txt).
Native PBR emission and programmable/soft-particle uniforms retain their own
range; PBR still bounds its final FP16 radiance after texture modulation.
The original vertex-color laboratory also verifies opaque alpha for RGB-only
ASE colors, avoiding an uninitialized fourth byte in the model importer.

The shared world ambient domain also consumes and validates modern lighting
records for surfaces without a classic ambient stage. They retain the no-op
contract, with no owned draw or depth prerequisite. This prevents otherwise
eligible views from falling back because of an apparent packet mismatch.

## Linear scene screenshots

`screenshot linear screenshots/<name>.pfm` exports the completed native linear
scene as bottom-up, little-endian RGB floats. It retains emission above one and
includes fog and ordered transparency before bloom, exposure, tone mapping and
HUD. Its dimensions follow the scene resolution, which may differ from the window.
The command accepts only a completed admitted HDR view; menus, encoded previews,
declined views and invalid paths are rejected instead of exporting an old scene.

The output chain keeps its existing scene copy in a dedicated image, separate
from scratch images reused by later effects. Completion is invalidated at the
next frame/main view and checked against the image generation and video-restart
count. Explicit captures submit and wait for GPU readback without presenting,
then resume the acquired frame with its target and allocation cursors intact.
Normal frames do not gain an additional scene copy or readback wait.

The v31 implementation is built and staged. All 21 composition/radiance controls
pass on both APIs at 0x and 4x; 20 native lifecycle cases at each sample count
and 18 GL lifecycle cases also pass. They include 50/75/125% scene extents,
off-centre orientation, exposure independence, image/video reload, exact repeated
PFM/TGA preservation, a 960x600 capture immediately before restart, and rejection
of encoded or declined scenes and invalid paths. Native startup rejection and
the HDR/temporal GPU fixtures and all four stock SP/MP 0x/4x profiles pass.
Public-command qualification exposed a separate font-image lifetime defect:
`reloadImages all` recreated empty TrueType scratch storage, leaving HUD glyphs
missing and Vulkan sampling an undefined image (`VUID-vkCmdDraw-None-09600`).
GUI, extended-character and console atlases now retain compact coverage and
regenerate their pixels after image reload and video restart. All four GL/native
0x/4x font sequences pass: HUD and atlas restoration is exact, seven linear
captures per sequence remain identical, and the native stale-image guard rejects
export before a new scene completes. The failed original runs remain under
`.tmp/vulkan-gap-closure/linear-capture/`; their validation errors are not suppressed.

The final GL 4x lifecycle test initially assumed supersampling also retained
MSAA. GL deliberately disables it above 100% scene scale; the corrected fixture
requires effective zero with reason `supersampling` at 125%, and actual 4x
elsewhere. Vulkan retains 4x at that scale. This policy correction changes no
float/display tolerance and the original failed test is retained.

The rebuilt font/capture modules repeat all 42 composition controls and 21
paired 0x float comparisons, both 4x lifecycle sequences and four native GPU
cases successfully. Stock SP0 and MP4 pass in the final integration batch;
concurrent staging terminates SP4 and MP0 before qualification, so that batch
stays failed. The editor task's subsequent SP4/MP0 reruns pass with the same
renderer/game modules and its updated client. Their exact runtime identities,
reports, logs and captures are independently checked and archived alongside
the original evidence. `checkpoint-v31-linear-capture.json` under the evidence
directory binds these results without assigning a pass to the interrupted batch.

All 21 **0x** GL/native float pairs pass the fixed absolute 0.005 plus relative
0.002 gate. The original **v31 4x full-image comparison failed**: the scalar sphere
differs at 254 silhouette pixels by one covered sample, with matching interior
radiance. All 20 comparable native 4x display images are byte-identical to the
retained pre-change v29c captures. An independent projected-mesh investigation
strongly supports reflected sample positions: the expected pattern for each API
misses one boundary pixel, while swapping the patterns misses 255. The engine
reports eye position to only 0.1 units in this investigation. This is an inference,
not a queried hardware sample-position result or a converted parity pass.
The later v34 measurement and alignment above now pass all 21 4x float pairs;
the original failed v31 evidence remains unchanged. See
the [Vulkan rasterization convention and standard locations](https://github.khronos.org/Vulkan-Site/spec/latest/chapters/primsrast.html).

A separate GL 4x shared-fog toggle differed by one FP16 storage step in 173
channels while its displayed images stayed identical. That equivalence check
now allows one encoded FP16 step; the cross-backend float and display gates are
unchanged. The original failure is retained. Returning to the menu rejects the
linear screenshot, but that run also emits 275 stock menu asset/precache warning
signatures, including missing `models/monsters/burn_misc_sm`; it is retained as
an unclean run and excluded from the clean lifecycle results.

## Prepared HDR post processing

The v29 candidate extends this ownership to bloom and automatic exposure.
Before taking over, it prepares every luminance/bloom target, pipeline,
descriptor and uniform slice, plus the exposure readback buffer and output
capture/resolve storage. Metering reads the completed linear scene before bloom;
the final composite adds bloom in linear space before tone mapping. Normal
exposure remains asynchronous, and the diagnostic synchronous mode retains the
same frame slot when it submits and resumes. Its final output uniform is filled
after adaptation and before that slice is consumed. A preparation rejection
returns unused uniform space as well as restoring the original target.

Exposure generations now include the actual numeric domain. Switching between
an admitted linear scene and a declined classic scene invalidates the previous
samples even when the FP16 format, size and user settings stay the same.
`rendererVulkanHDRInfo` reports that metering domain as `linearScene`.
The diagnostic `r_vkHDRPrepareFailure` values 1, 2 and 3 decline preparation at
exposure, bloom and final output respectively; 0 restores normal operation.
This is a default-off diagnostic, not a different rendering mode.

Canonical Vulkan render-texture writes now carry their orientation into ordinary
fixed material stages, including the shared GUI/ambient consumers. Their UV
transform compensates for top-down framebuffer storage after the authored
transform. MSAA resolves preserve that provenance; uploaded images and explicitly
flipped framebuffer copies use the ordinary texture convention. Native post
shaders retain their existing explicit coordinate contracts. This fixes the
upside-down game-owned presentation exposed by reduced-resolution PBR scenes.

The v30 GL candidate also aligns a single eligible root scene with the shared
scene resolution. Its graph color/depth attachments, draw packets, clustered
light grid and scissors are prepared in the same coordinates as the later
scene/post target. Original view and surface rectangles are restored before
the ordinary command stream continues. Native window imports and shadow
atlases retain their own sizes. Multi-view, portal and capture streams keep
their previous path; this does not qualify separate scene graphs for them.
`gfxInfo` reports `Renderer graph extent` so captures can verify scene and
presentation sizes independently. With matching rebuilt game modules, all 33
v30d post-processing pairs match Vulkan within one byte per channel, including
the three previously failing scaled/off-centre controls. All sixty composition
pairs at 50%, 75% and 125% also pass that bound. The two-byte acceptance gate
is unchanged. Six scaled lighting controls, twelve CPU/GPU animation controls
and both GL integration self-tests also pass. All four staged Vulkan SP/MP
0x/4x profiles and two GL stock gameplay profiles at 75% scene scale pass.
The latter use the benchmark's fixed 1280x720 display contract. This is local
Windows/NVIDIA correctness evidence, not performance or release promotion.
`checkpoint-v30-scene-scale.json` under `.tmp/vulkan-gap-closure/scene-scale/`
binds the source/module archive, image comparisons, terminal runs and retained
failed/interrupted evidence. Game and renderer files remained unchanged
through qualification. The fixed-viewport GL self-tests now explicitly request
a matching 640x480 window and 4x MSAA; the permanent harness rerun is recorded
in `gl-integration-v30i/`.

The wider 50% composition run then exposed an independent handoff defect:
the game's GL forward/resolve target was still RGBA8, so emissive values above
one clipped before tone mapping and faint transparent channels lost precision.
Canonical `openQ4-game` target selection now requests RGBA16F for explicit HDR
on both backends, with LDR post-AA targets unchanged. The failed emissive and
alpha controls are retained. The rebuilt game module passes their independent
radiance checks at all three scales, plus four clear controls at actual 4x MSAA
and 75% scale on each backend. A shortened MSAA script initially referenced
omitted entities; that harness failure is retained separately from the passing
corrected sequence.

Portal-sky backdrops retain their composed color through bloom/tone mapping.
Before temporary scene scaling, Vulkan associates an earlier portal-sky view
with the next main view only when frame, world, viewport and original render
target match. Portal-mask materials also request this preservation. A depth
test excludes far-plane backdrop pixels from the composite while preserving
foreground processing, including multisampled coverage. This matches the
OpenGL presentation boundary without requiring a seventh post texture sampler.

The exposure target is key value divided by average luminance, clamped to the
configured minimum/maximum. Exponential adaptation uses elapsed presentation
time and separate brightening/darkening speeds. Live exposure bounds apply
immediately even if adaptation is paused. Map/world identity, device restart,
scene format/sample count/extent and camera discontinuities invalidate the
sample generation. Old and duplicate completions cannot seed the new history.
Captures reuse the current exposure without queuing or adapting a new sample;
already submitted samples may still retire during a synchronous capture.

The explicitly synchronous exposure mode splits a graphics submission without
presenting, retains the acquired swapchain image and upload-ring ownership,
and excludes the split frame from whole-frame GPU timing. It is a diagnostic
mode, not a performance comparison configuration.

## Diagnostics and verification

`rendererVulkanHDRInfo` reports the actual scene format/sample count/extent,
exposure enable/initialization and asynchronous-readback state, generation, queued/completed sample
counts, average luminance, target and current exposure.
`skyPreserved` identifies the actual backdrop-preserving composite.
The additional `Vulkan HDR scene ownership` line, also present in `gfxInfo`,
reports requested/committed ownership, active linear state, rejection reason,
completed views and actual accumulation samples. A completed output reports
`committed=1 linearActive=0 reason=complete`; an FP16 target alone is insufficient.

`rendererVulkanHDRSelfTest` exercises 24 real GPU fixtures:

- RGBA16F scene capture at 8x8 and 17x9, with 0x and actual 4x MSAA.
- Bright radiance above one, mixed RGB radiance, and dark radiance, each with
  independently computed expected log luminance and exposure.
- Both synchronous and asynchronous readback, resize/storage retirement,
  and rejection of stale, duplicate and disabled completions.

The same command also runs 112 stock tone-map shader fixtures and 320 linear
filmic/output-transfer fixtures. The linear fixtures cover actual 0x/4x MSAA,
neutral/mixed/negative radiance through 65,504, four exposures, two reference
whites, the dark transfer region, and exact preservation of four alpha values.
Another 32 fixtures check bloom addition before tone mapping, display grading,
highlight controls, debug views and bypass behavior in both numeric domains.
Twenty portal-sky fixtures cover both domains at 0x/4x, verify mask-material and
preceding-view admission, and reject stale frames, wrong viewports and wrong
targets. The runtime matrix requires all these markers again after restart.
Another 24 production-boundary fixtures use distinct values in each sample,
two additive classic contributions, mixed/negative/overbright channels,
spatial row/column patterns and exact alpha. Both ordinary and actual 4x
targets are checked against scalar decode-before-resolve expectations. The
runtime matrix requires that marker before and after restart too.

The prepared post chain adds 32 GPU fixtures using independent luminance and
output references, with manual/automatic exposure, one/five bloom levels,
two sizes, exact alpha and actual 0x/4x MSAA. Six injected late rejections must
leave the scene and uniform cursor unchanged. Four domain-transition checks
reject stale completions and accept fresh samples. The staged v29c candidate
passes these checks before and after restart, plus all five integration GPU
cases (`.tmp/vulkan-gap-closure/hdr-post/integration-gpu-v29c/`).

`renderer_vulkan_hdr_post.py` also passes all 38 native controls at each of
0x/4x, plus two auxiliary captures per run. These exercise bloom levels,
threshold/knee/radius, synchronous/asynchronous exposure, bounded and free
adaptation, preparation rejection, image/video reload, resizing and repeated
capture. Three off-centre scaled controls independently check orientation,
including rejected linear ownership. The fixed-time failure sequence proves
generation changes only across numeric-domain transitions. All 33 OpenGL
controls pass their independent checks.

At the v29c checkpoint, strict full-frame comparison ended **30/33 passing**,
with the original two-byte threshold unchanged. The three 75% scale pairs
failed (maximum 76, 96 and 85 bytes/channel). The orientation defect was fixed;
the remaining edge differences reflected distinct raster domains: the GL modern graph used native
window-sized resources, then samples into the smaller game-owned scene before
tone mapping and final upscale. Vulkan rasterizes at the actual smaller scene
extent. A retained scalar resampling diagnostic approximates that GL sequence;
it is not an acceptance result or a substitute for aligned rendering domains.
Reports and negative controls are in `.tmp/vulkan-gap-closure/hdr-post/`,
including `post-parity-v29c-0.json` and `scaled-diagnosis-v29c.json`.
The same candidate passes all 40 native composition and 90 ambient captures
at 0x/4x, including independent ambient radiance/alpha checks. All twenty
composition pairs remain within one byte/channel of OpenGL. All four staged
stock SP/MP 0x/4x profiles pass. `checkpoint-v29-post.json` binds these results
and those three scaled parity failures to the archived module/source. The
separate v30 qualification above closes them without changing the threshold.

The v28 Windows/NVIDIA candidate passes 40 real-map composition captures and
86 ambient/material/recovery captures at 0x/4x. Independent radiance checks
cover metallic/dielectric response, material data and normal encodings,
emission, cutouts, alpha over a measured background, resource rejection and
exact restoration. All twenty paired OpenGL/native clear/fog/blend images at
0x pass the full-frame two-byte limit; the observed maximum is one byte per
channel. Shared-setting toggles remain setting-equivalence evidence only.
The five integration GPU cases also pass on that staged module, including
every HDR fixture before and after full restart. Evidence lives under
`.tmp/vulkan-gap-closure/hdr-scene/`: `composition-parity-v28b-0.json`,
`ambient-proof-v28-{0,4}.json` and
`integration-gpu-v28/renderer_validation_report.json`.

The native recovery profile additionally passes at both sample counts: 75%
scene scaling, a 960x600 window, immediate capture/partial restart, restoration
to 1280x800 and full restart retain actual FP16 ownership and the scalar tone
result. The restored full images are exact. Twelve captures, including the two
intermediate engine images, are retained in `recovery-v28c-{0,4}/report.json`.
Run this profile with `tools/tests/renderer_vulkan_hdr_scene.py --suite recovery`
and the stable `.tmp/runtime/candidate` laboratory runtime. The first telemetry
revision had unquoted hyphenated echo markers; its reported missing markers
are a retained harness failure, corrected in the accepted runs.

The same staged v28 module also passes all four stock SP/MP 0x/4x profiles,
including HDR/exposure toggles, 75% scale, capture, resize and full/partial
restart (`stock-gameplay-v28/report.json`). The SP 4x scaled, resized and
partial-restart intervals each complete 60 temporal views against the unchanged
minimum of ten. These stock profiles exercise compatibility integration with
PBR off; the laboratory controls above establish the admitted linear PBR path.
An unrelated dedicated process appeared during the queue, so these remain
correctness results without a host-isolation or performance claim. The prior
slow-presentation failure stays retained; its cause is not established by the
successful repeat. `checkpoint-v28-scene.json` binds the accepted reports,
captures, logs, frozen harnesses, source and matching staged/native modules.

The first GL reference run had a test setup error: deferred entity removal
had not completed before the same entity name was reused. Unique specimen
names fix that race. Both members of the accepted GL/native pair were captured
with the corrected, identical harness; the failed reference remains retained.
An initial scalar smoke also used a stale copied Vulkan module while staging
was incomplete. Its mismatch is recorded separately and excluded from the
accepted runtime evidence.

The staged v27 Windows/NVIDIA run passes both executions of every output
fixture, plus the 48 existing exposure fixtures, with active Vulkan validation.
This qualifies the shader boundary using controlled floating-point inputs.
It does not establish complete scene ownership or cross-backend image parity.
Evidence: `.tmp/vulkan-gap-closure/hdr-output/hdr-gpu-v27/renderer_validation_report.json`.

The same staged build passes startup/render-target, probe lifecycle, rigid-motion
and resource-recovery GPU cases. Stock SP at 0x and MP at 0x/4x pass all HDR,
capture, resize and restart checks. The initial SP 4x run fails the resized
temporal-history interval: four completed views out of 60, below the unchanged
minimum of ten. That run logged 130 ms average presentation while history
rejects time gaps over 100 ms. Another project's build/renderer was observed
during the test queue; the precise cause of the earlier slowdown is not proven.
The failed result is retained; the subsequent v28 repeat above passes the same
SP 4x profile without changing its temporal requirement. These are correctness
runs, not performance qualification. Reports and hashes are under
`.tmp/vulkan-gap-closure/hdr-output/`, with the bounded result recorded in
`checkpoint-v27-output.json`.

The mandatory `renderer-vk-hdr-selftest` runtime case runs this exercise before
and after a full `vid_restart`, requires the ordered engine-log evidence for
both passes, and rejects validation/VUID/call failures. Both Linux push and
pull-request workflow selections include it. Hosted results and physical
platform coverage remain separate requirements.

The first Windows run passed all 24 fixtures with active validation. A later
run passed all 48 executions around full device teardown/recreation. Evidence:
`.tmp/vulkan-gap-closure/hdr/restart-gpu/renderer_validation_report.json`.
`HDRExposureCoreTest.cpp` checks finite-sample handling, exposure bounds,
time discontinuities, 30/60/144 Hz adaptation equivalence and FP16 decoding.

`tools/tests/renderer_vulkan_hdr_gameplay.py` is the stock-map integration
harness. It uses isolated save paths and an independently copied runtime,
hashes the runtime before and after, disables input, and runs hidden windowed
SP `game/airdefense1` and auto-joined MP `mp/q4dm1` with 0x/4x MSAA. Its engine
captures and state checks cover HDR/LDR toggling, capture continuity, TAA at
75% scene scale, both exposure readback modes, full and partial resize/restart,
manual exposure and disabling HDR. These are correctness runs, not benchmark
or soak evidence.

The initial gameplay run exposed an existing full-restart dispatch error:
Vulkan entered `R_InitOpenGL` and failed to create a GL context. Full restart
now uses the same backend-specific initialization seam as initial startup.
The initial failed report is retained in
`.tmp/vulkan-gap-closure/hdr/gameplay-first/`.

The takeover also reproduced a crash when a screenshot immediately preceded a
partial restart: resumed, unsubmitted command recording still referenced the
old swapchain views. Resize now closes/submits that frame before retiring the
swapchain. Hidden startup consistently suppresses fullscreen/borderless modes.
The original immediate sequence passes on v63 for stock SP and MP at both 0x
and 4x, with active validation, no tracked rendering diagnostics and an
unchanged independent runtime. GPU attachment/MRT tests and 48 HDR fixtures
around full restart pass on the same module. Evidence:
`.tmp/pbr-audit/vulkan-v63-hdr-gameplay/report.json` and
`.tmp/pbr-audit/vulkan-v63-gpu/renderer_validation_report.json`.
Those earlier SP captures still show excessive sky/highlight exposure; they
qualify lifecycle and sample ownership, not visual parity or performance.

The same four stock cases pass again on the v68 PBR cutout/emission binary,
with active validation and an unchanged runtime
(`.tmp/pbr-audit/vulkan-v68-hdr-gameplay/report.json`). Its GPU suite additionally
reads actual FP16 native emission attachments at 0x/4x MSAA: a low texture
channel retains 4,096 while the bright channels bound to 65,504 after modulation.
This proves a single emission write, not arbitrary multi-light accumulation
or complete scene linearization.
The v69 specular-filtering candidate repeats the same four stock transition
cases and both GPU self-tests successfully, with unchanged runtime files
(`.tmp/pbr-audit/vulkan-v69-hdr-gameplay/report.json` and
`.tmp/pbr-audit/vulkan-v69-gpu/renderer_validation_report.json`).

Renderer ABI 14 also reconciles the engine's renderer header with the canonical
game header (ETC2 capability field and clear alpha). The clear command now
forwards its alpha argument. Engine and both renderer modules must be updated
together. The game scene-format integration requires the rebuilt game modules.

## Domain changes, settling and admission counts (23 October 2026, VK-05)

Complete-scene admission decides per view, and a view's content can flip the
decision from one frame to the next. Three consequences of a flip are now
handled explicitly:

- **Exposure.** Linear radiance and display-referred values meter differently,
  so each domain keeps its own adaptation. A change of domain alone drops the
  samples in flight (a new generation), saves the old domain's state and
  resumes the new domain's state when it has one, instead of restarting at
  exposure 1 on every flip. A stale saved state still snaps to its first new
  sample, as any adaptation older than a second does. Any other reset trigger
  (size, format, samples, world or map, restart, camera cut) forgets both.
- **Temporal history.** A domain change invalidates temporal history, so TAA
  never blends frames tone-mapped by two different curves.
- **Late failures.** A view admitted to the linear scene that never reaches its
  tone-map output, because a scene-effects step returned early, is now settled
  as `output` and warns once. Before, it stayed linear for the GUI and later
  views of the frame. A combine failure returns to the view's own target and
  releases the prepared output, instead of leaving the accumulation bound for the
  rest of the frame. The view is still abandoned, and the warning now prints
  once.

`rendererVulkanHDRInfo` adds the number of domain changes, the main view's
outcome (previously the reported reason was whichever 3D view ran last) and a
count of every admission outcome since the render world changed. Outcomes are
the decline reasons above, plus `accumulating`, `complete` and `output`; the
table is bounded and overflow is counted.

On stock airdefense1 with the laboratory linear scene requested, the counts
show why nothing is admitted: 181 main views declined for `ambient-stage` and
181 subviews for `subview`, with no domain change. Before, the single reported
reason belonged to whichever of the two views ran last.

The prepared-post self-test asserts the new contract: the first domain change
after a reset restarts adaptation, the change back resumes the saved linear
exposure, and both drop old samples and invalidate history. All Vulkan HDR,
render-target and temporal GPU self-tests pass with validation enabled and no
validation messages.

Two survey suggestions were not taken. Admitting classic ambient blends other
than replace or add would scale the PBR attachment too: a classic modulate decal
over a PBR pixel writes zero into it under the same blend state. Admitting the
cel world ink needs a linear-scene fixture first. Both stay declined.

## HDR cubemap capture (3 November 2026, VK-05/VK-07)

An engine cubemap face smaller than the window used to bypass Vulkan's scene
target. Complete-view admission therefore rejected it as `scene-target` and
used the display-referred fallback. In the linear laboratory with bloom, the
specimen's centre was 172 instead of GL's 224; 2,406 pixels on the +Y face
exceeded the unchanged two-level limit (maximum difference 81).

Direct HDR captures whose view rectangle fits inside the frame now reuse the
existing window-sized floating-point scene attachment. Their viewport and
scissors keep the capture's original size; the existing spatial presenter
copies the finished attachment one-to-one for readback. No additional scene
cache or temporal history is created. Complete-view material/post admission
still runs, and game-owned targets keep their existing ownership. Out-of-frame
or tiled rectangles retain the previous path and remain outside this result.

The extended `renderer_vulkan_hdr_post.py` passes 41 Vulkan and 36 OpenGL
controls, including failure injection, exposure, bloom, reloads, resizing,
restarts and exact main-view recovery after the cube capture. Its strict
comparison passes all 42 pairs (36 views and six faces), with a maximum
one-level difference at 0x MSAA on the RTX 4060 Laptop GPU. Vulkan validation
reports no messages. The focused manual/bloom/automatic-exposure cube probe
also produces the expected centre values, 207/224/224. Native capture-admission
checks reject missing, empty, negative and out-of-frame rectangles.

Retained evidence: `.tmp/vk-roadmap/hdr-post-{gl,vk}-fixed/` and
`hdr-post-comparison-fixed.json`; the original failed reports and captures
remain in `hdr-post-{gl,vk}/` and `hdr-post-comparison.json`. Reports include
the exact development-binary and harness hashes. This qualifies in-frame HDR
engine cube captures; it does not admit arbitrary portal/nested/material graphs
or change production PBR's display-referred contract.

After integration with the MP/save changes, the clean `29e237378` runtime also
passes all 41 Vulkan post/capture controls at 4x MSAA with validation enabled.
The report and engine captures are retained in
`.tmp/vk-roadmap/hdr-post-vk4-integrated/`. This is an independent Vulkan
lifecycle repeat; the 42 paired comparisons above use the recorded 0x set.

The staged stock-gameplay lifecycle runner also passes SP Air Defense 1 and
MP Q4DM1 at 0x and 4x MSAA: all four runs exit cleanly, keep the runtime
unchanged and retain eight valid engine captures each. They exercise HDR
toggles, exposure readback modes, temporal scaling, resize and both restart
paths. Evidence is in `.tmp/vk-roadmap/hdr-gameplay-fixed/`. The earlier run
in `hdr-gameplay/` exposed a fixture assumption: the first-run enhanced preset
enabled bloom, so disabling tone mapping still ran the bloom/sky-preservation
pass. The runner now explicitly pins bloom off for its disabled-HDR control.

## Stock content stays display-referred (6 November 2026, VK-05)

**Decision (project owner, 6 November 2026).** Classic stock material stages
are not re-blended in linear light. That covers blends other than replace
or add, dynamic, texgen and cinematic stages, materials sorted at or after
`SS_MEDIUM`, and the subviews that draw them. They keep Quake 4's
display-referred path, which matches OpenGL and the retail game. Complete-view
admission therefore declines any view that contains them, and the whole view
falls back to that path. This closes VK-05's classic-stage admission scope as
declined by design. Linear-light compositing of classic blends inside an HDR
scene, either directly or as a display-referred pass after tone mapping, is
not planned.

**Fallback qualification.** `renderer_hdr_stock_fallback.py` freezes the
stock single-player maps `airdefense1` and `process1` with PBR materials on and
toggles the laboratory linear scene (`r_pbrLinearScene`). It does this twice:
with tone mapping on, and with the HDR-off PBR preview. The view weapon and
HUD are hidden, because they animate on real time, and the frame settles for
300 frames before the first capture. On Vulkan each request, and its removal,
must leave the engine capture byte-identical to the display-referred frame,
and `rendererVulkanHDRInfo` must report the request with no linear view and no
admitted outcome. OpenGL, the reference, is held to one display level: its
tone-mapped `airdefense1` frame wanders by one level even between two
captures with identical settings.

On `8844bad24` (RTX 4060 Laptop, NVIDIA 591.74, validation enabled and no
validation messages), Vulkan passes on both maps in two separate runs. Every
main view declines (`ambient-stage` or `ambient-blend`, depending on which
classic stage is met first), subviews decline as `subview`, and all three
captures of each group are byte-identical. On OpenGL, `process1` and both
HDR-off groups are byte-identical. The tone-mapped `airdefense1` captures
differ by at most one level, including the pair with identical settings, so
that variation is OpenGL's own and not the request. The last run predates the
gate's final wording; its recorded captures meet it (Vulkan 0 levels,
OpenGL at most 1).

Reports and engine images: `.tmp/vk-roadmap/nov6/hdr-fallback/`.

## Remaining acceptance

- Classic stages stay declined by design (see
  [stock content stays display-referred](#stock-content-stays-display-referred-6-november-2026-vk-05)).
  For admitted PBR views, tiled and out-of-frame captures and authored
  post-effect combinations still need HDR-scene coverage.
- Extend the single-root scene-extent contract to separately owned multi-view,
  portal and authored capture graphs. The v30d result above closes the three
  retained scaled HDR comparisons for eligible single-root scenes; it does not
  qualify those more complex view streams.
- A retained v25 native/GL PBR diagnostic comparison reproduces the former
  linear-scene presentation contract. All 20 clear/fog/blend captures complete
  on both APIs, and toggling the shared fog setting produces identical images
  within each backend. This is setting-equivalence evidence, not proof that
  both consumers ran: native scene-target ownership suppresses the shared
  preflight even at 100% scale. Every v25 cross-API image pair fails. For example, the same unlit
  scalar albedo displays as 128 in Vulkan and 207 in GL; independent tone-curve
  oracles reproduce each result within one byte. The v28 boundary corrects
  that clear control to 207 through actual complete-view ownership and passes
  all twenty corresponding GL/native image pairs. Retain the older failure
  as a negative control; broad parity requires the complete material/post
  scope, not only this diagnostic. Earlier evidence:
  `.tmp/vulkan-gap-closure/pbr-direct-curvature/diagnostic-composition-hdr-parity-v25-0.json`.
- The 2026-09-23 fixed-view controls identify and repair the SP highlight loss:
  PBR toggles are byte-identical, the stock tone curve now retains highlight
  contrast, and the actual portal-sky owner preserves the backdrop. A static
  sky patch changes by zero bytes between exposures 1 and 8, matching OpenGL;
  the old Vulkan path washed out every channel in that patch. The same patch
  differs from the updated GL reference by a mean 0.045 bytes/channel. This is
  a scoped sky comparison, not whole-frame parity: actor poses and effects vary
  between process starts. Evidence: `.tmp/vulkan-gap-closure/hdr-highlight/`,
  with current GPU results in `gpu-sky-owner/` and the inspected image in
  `owner-vk/auto.png`. All four SP/MP 0x/4x lifecycle reruns pass with zero
  tracked warnings in `stock-gameplay/report.json`; the frozen-scene CLI passes
  independently in `qualified-vk/report.json` and `qualified-gl/report.json`.
- `tools/tests/renderer_hdr_highlights.py` retains eight frozen stock controls,
  verifies exposure changes foreground, PBR toggles/restoration are exact, and
  the static sky patch remains detailed and unchanged. GL automatic exposure
  requires modern handoff and is explicitly outside this classic comparison.
- Retain controlled OpenGL/Vulkan visual comparisons for the HDR path,
  including authored feedback/capture and post-effect interoperability.
- Verify map/camera discontinuities and resource-admission failures in runtime
  fixtures, beyond the numerical generation rejection tests.
- Complete the broader clean-package, platform/driver, performance and soak
  requirements in the [Vulkan gap ledger](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).
