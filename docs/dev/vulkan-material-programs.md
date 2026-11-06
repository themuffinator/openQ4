# Authored Vulkan material programs

Status: native drawing is implemented for authored GLSL ambient and per-light
stages and for authored ARB assembly material stages, which are translated to
GLSL. Unknown program names now load and compile their actual source
pair. Stock-name overrides also use their actual source; an embedded stock
implementation requires a reviewed source fingerprint or a canonical name with
no source files. Windows x64 NVIDIA/Intel support is qualified; the remaining
work below covers broader language and state coverage, including authored mod
content. Other hardware/platform qualification remains under VK-01.

## Compiler contract

`src/renderer/materialprogram/GLSLCompiler.cpp` accepts a vertex/fragment source
pair and explicit, case-sensitive material parameter and texture bindings. It
uses the pinned glslang 16.6.0 compiler in process, through a static library built
by the project's native Meson subproject. It does not invoke an installed SDK
executable, perform file access, or create GPU resources. Source inputs are
limited to one MiB each; empty and embedded-NUL inputs are rejected. Compilation
is serialized around the library's process lifetime and independent callers
can compile safely.

The original OpenGL GLSL pair is parsed and linked before translation. This is
necessary because the compatibility layer initializes per-invocation globals
from uniform storage; translating first could accidentally legalize an authored
write to a uniform. The glslang preprocessor expands macros and conditionals.
The translator operates on tokens, preserving diagnostic line records and
function-local variable shadowing. Generated Vulkan shaders are linked together
before emitting SPIR-V 1.3 for Vulkan 1.1. Failed compilation clears both output
modules and returns a diagnostic, with the shader filename for source failures.

The initial language subset covers GLSL 1.10/1.20 scalar/vector float uniforms,
2D/cube samplers, named varyings, the engine's explicitly bound vertex attribute
names, common legacy vertex/color/texture-coordinate inputs, model-view,
projection, normal and texture matrices, fragment color and coordinates, and
the supported 2D/cube explicit-LOD/gradient functions. Material names are not a
shader-family whitelist. Uniform and interface arrays, authored matrix/int/bool
uniforms, other sampler types, newer GLSL versions and other extensions remain
unsupported and receive diagnostics. Unrecognized legacy built-ins must pass the
Vulkan compiler; they are not silently replaced with dummy values.

The parameter capacity is the engine's 32 slots; the texture capacity is eight.
Active scalar/vector parameters must match the material's upload width. The
runtime draw interface is:

| Resource | Binding | Layout |
|---|---|---|
| Per-draw data | Set 0, binding 0 | 1,328-byte std140 block |
| Authored textures | Set 1, bindings 0–7 | Separate combined image samplers |
| Vertex inputs | Locations 0–5 | Position, color, normal, tangent, bitangent, UV |

The uniform block contains 32 parameter vectors, model-view and projection
matrices, eight texture matrices, stage color, four draw controls, two
CPU-composed model-view-projection matrices and a texture-orientation word
(1,328 bytes). Matrices are column-major. Images that Vulkan rendered keep
top-down rows; every authored 2D lookup on a declared sampler goes through a
generated helper that flips the coordinate (and the Y gradients) for the
slots the orientation word marks, so render targets sample the way OpenGL's
bottom-up rows do. A sampler passed through a function parameter keeps the
plain lookup. Authored
matrix operations retain GL clip coordinates; the generated vertex entry point
converts clip depth to Vulkan's range. Unmodified `ftransform()` or legacy MVP
positions use the same CPU-converted matrix as depth fill, preserving equal-depth
lighting coverage. Changed authored positions keep the ordinary clip conversion.
Window Y and `dFdy` select
the target's orientation through a pipeline specialization constant. In the authored GLSL
path, vertex color arrays are exposed directly, matching `draw_common.cpp`;
the wrapper does not invent stage tint or inverse modulation for those arrays.
The first native comparison covers texture coordinates, model-view/projection,
authored vertex offsets, fragment coordinates and derivatives in the ordinary
display path. Other target, color and geometry combinations remain unqualified.

## Runtime ownership

OpenGL and Vulkan share the source-pair search order in
`MaterialProgramSource.cpp`. Vulkan passes the complete file lengths to the
compiler, preserving embedded-NUL rejection. Shader identity includes the
program name, parameter names/order/semantics and texture names/order. Numeric
values and image selection remain per draw. `reloadGLSLprograms` invalidates the
authored source lookup on both backends; `reloadARBprograms` also performs this
GLSL reload. Vulkan reuses unchanged valid or invalid source pairs before
checking its version limit. Changed
versions stay alive while current or referenced by a frame in flight. A stale,
retired version may be evicted at either the version or compiler-storage limit;
its shader modules and pipelines are released before the slot is reused.

Native stock selection uses `material_program_sources.json` and the generated
`vk_MaterialSourceSignatures.h`. Both source files must match one reviewed pair;
only CRLF line endings are normalized. The table contains hashes, not retail
shader text. `tools/build/vulkan_material_sources.py --check` is registered in
the native Meson tests and script CI. It compares the generated table with the
reviewed manifest and never automatically approves changed shader source.
Different directories with the same basename are authored programs. Modified,
empty and incomplete pairs cannot silently select a stock implementation.
Some retail guide families have no GLSL source files; their canonical names
retain the existing built-in default only while every candidate source is
absent. Source selection is cached until an explicit shader reload or device
shutdown. A complete pair retains the shared filesystem search order.

OpenGL reload invalidates parsed material stages and the engine's material GLSL
stages. Failed attempts are cached in the current GL context, so a broken file
does not compile again on every draw. Explicit reload or a new context retries
it. Neither backend needs a game restart to load a repaired supported shader.

Each frame slot owns a descriptor pool reset only after its fence completes.
The larger uniform block uses the existing frame ring with the device's uniform
alignment. All eight texture descriptors use the existing image residency and
transition path. Shader modules, layouts, pools and CPU records are destroyed
with the device, after the executor destroys its pipelines. GL handle fields
are never used to store Vulkan handles.

Limits are explicit: 256 retained program versions, 32 MiB of retained compiler
source/module data, 8,192 authored draws per frame slot, and the executor's
shared 512-entry material pipeline cache. Allocation, type and unsupported
binding failures diagnose and skip the affected stage. Exhaustion/recovery
drills are still required; these bounds are not a performance claim.

## Authored lighting

Supported `customLighting` GLSL stages now execute once per contributing light
using their actual shader pair. Per-draw bindings include local light/view
origins, projected-light and falloff planes, bump/diffuse/specular texture rows,
diffuse/specular colors, vertex-color modulation, model/projection rows and the
identity color matrix. Light projection/falloff and other engine texture
semantics resolve from the current interaction. Stage conditions, colors and
texture transforms remain material-controlled. Draws accumulate with ONE/ONE
blending and inherit the receiver's geometry, depth, cull, bias and stencil state.

Arbitrary authored shaders do not have the engine's native shadow-map sampler
contract. When mapped shadows are requested, only affected receivers run through
a separate stencil-filtered interaction walk; other surfaces retain their mapped
shadows. This matches OpenGL's retail stencil caster policy and does not add
support for expanded map-only casters. The front end conservatively retains
volumes for custom or unresolved interactions before submitting any receiver.
The cost of that conservative retention still needs performance qualification.
The two verified native lighting families keep their existing mapped path.

Explicit linear/nearest image filters now select the base mip level even when
an image already has a mip chain. This matches OpenGL's sampler behavior across
image reload. The lighting investigation also repaired OpenGL's material texture
table: unused declared array slots now receive valid material images instead of
retaining a comparison-enabled shadow texture from a previous pass.

## Authored ARB assembly

`src/renderer/materialprogram/ARBTranslator.cpp` translates
`ARB_vertex_program` and `ARB_fragment_program` 1.0 material programs into the
compatibility GLSL above, so they reuse the same compiler, descriptor layout,
pipeline cache and draw path. The section is chosen exactly as
`R_LoadARBProgram` chooses it (the first `!!ARBvp`/`!!ARBfp` up to the first
`END`), so a program OpenGL truncates is rejected the same way. Every
instruction of both languages is supported, including `_SAT`, `KIL`, `SWZ`,
`TXP`/`TXB`, `ARL` relative addressing, `PARAM` arrays and ranges, `ALIAS`,
`OUTPUT` and `ARB_position_invariant` (which keeps the depth fill's exact
position). Matrix state binds the model-view, projection, MVP and texture
matrices with their `inverse`/`transpose`/`invtrans` rows. A stage with only
one program gets the fixed-function stage it replaces: identity texture
matrices with unit 0's coordinates, or the interpolated color.

The parameters are the ones `draw_common.cpp` gives ARB material stages:
`vertexParm` feeds both programs' locals unless `fragmentParm` is present;
locals the material does not set read zero. The vertex environment is env 0
(current-render scale), env 1 (global eye) and env 5-8 (local eye and model
rows); the fragment environment is env 0 and env 1 (window-to-texture scale).
Textures bind by unit from `fragmentMap`, with their own sampler state.
Anything a material stage has no defined value for is rejected with a
diagnostic rather than replaced: fixed-function lighting, fog, texture
generation and other legacy state, other environment indices, `vertex.weight`
and attributes the engine does not supply, `1D`/`3D`/`RECT` targets, a unit
used with two targets, `ARB_fog_*` and NV options.

Programs without a reviewed native family are translated automatically, as
are pairs mixing families or naming an authored program on either side.
`r_vkARBTranslation 0` restores the old skip; `2` also translates the stock
families, as a diagnostic comparison against their native implementations.
`reportShaderPrograms` lists each registered program as native, translated or
unsupported.

The native `openq4-arb-translator` test translates and compiles original
fixtures covering every instruction, the fixed-function counterparts and 28
rejection cases, and with `--arb-dir` the installed game's own `.vfp` files
(their material programs translate; the interaction programs are correctly
refused). `tools/tests/renderer_vulkan_arb_programs.py` draws eight original
programs in the PBR laboratory on both backends: every one of its 15 controls,
including image and shader reload and partial and full restart, matches
OpenGL's native ARB execution within one display level. The retail
`heatHazeWithMask.vfp` translated with `r_vkARBTranslation 2` also draws
validation-clean on `game/airdefense1`.

## Validation and remaining work

The native `openq4-material-compiler` test covers valid paired programs,
parameters at the capacity boundary, 2D/cube textures, named/legacy attribute
aliases, normal transforms, macros, local variable shadowing, derivatives,
explicit LOD, deterministic recompilation and concurrent callers. Negative cases
cover syntax and link failures, uniform writes, invalid/duplicate bindings,
unsupported interfaces and source-size constraints. An independent `spirv-val`
check validates the emitted modules. These checks establish compilation and
module validity; they are not image, performance, or gameplay qualification.

The v50 Windows native-Meson run passed all 31 cases and the registered Meson
test. The v51 suite adds two invalid parameter-width cases and passes 33 cases.
Independent validation passes all 24 emitted modules and verifies the
uniform member offsets, matching vertex/fragment interfaces, boundary texture
slots and deterministic recompilation. The latest compiler evidence is retained
in `.tmp/vulkan-gap-closure/authored-glsl/compiler-qualification.json`; the
prior foundation evidence remains in the `material-programs` directory.
All 2,978 extracted upstream files and both overlay files were compared with
their pinned inputs. Other operating systems have not yet run this compiler
suite. The v51 runtime links the compiler into the Vulkan module; it requires
the matching staged engine and renderer modules. The prior PBR cutout fix is
retained, and its separate filtering discrepancy remains open.

The expanded v51 scene uses two independently compiled material programs and
engine `screenshot` captures in hidden, windowed, input-disabled gameplay. The
ten-control v51a run passes GL/Vulkan comparisons within one display level;
image reload, partial restart and full restart reproduce each backend's base
image exactly. Its unchanged-source shader control did not detect that Vulkan
had not registered the reload command; v52 adds actual live-edit coverage.
The reusable harness is
`tools/tests/renderer_vulkan_material_programs.py`; reports remain under
`.tmp/vulkan-gap-closure/authored-glsl/`. This is local image evidence, not
stock-map, performance, long-session or platform promotion evidence.

The capacity scene exposed a separate GL reference bug: ambient GLSL stages
linked the named tangent/bitangent/normal attributes to lanes 9/10/11 without
enabling and supplying those arrays. Those stages now bind the surface basis
and legacy normal array around the draw. The capacity fixture uses all three
named basis vectors to choose cube faces; a constant-color cube is a separate
control. This preserves a meaningful reference instead of reproducing stale
attribute values in Vulkan.

After that repair, all twenty v51d and final v51e controls pass. The 32-parameter/eight-texture
program, the changed last parameter and each independently changed texture slot
match GL pixel for pixel. The remaining controls differ by at most one 8-bit
display level, within the unchanged limit of two. Both backends show more than
29,000 changed pixels for each binding control and exact recovery images. The
tests cover named basis attributes, scalar/vec2/vec3/vec4 parameters and 2D/cube
samplers. These results do not establish untested language, blend or target
combinations. Failed intermediate reports remain retained with their diagnostics.
The final staged build also passes the render-target/startup, HDR and temporal
motion GPU regression groups with zero tracked warnings. Runtime, sources,
compiler outputs and reports are bound by
`.tmp/vulkan-gap-closure/authored-glsl/checkpoint-v51-authored-glsl.json`.

The v52 live-edit suite, `tools/tests/renderer_vulkan_material_reload.py`, adds
sixteen controls per backend. It exercises a stock basename under a different
directory, a canonical stock source replacement, changed fragment and vertex
source, syntax failure and repair, a fragment-only canonical source pair and
repair, both reload commands, and partial/full video restart. All sixteen
GL/Vulkan pairs differ by at most one display level; each backend's recovery
and deliberate failure images match their references exactly. The captures
also require the three unchanged SMAA sources to select verified native code.
The two deliberate failures each produce exactly one primary diagnostic before
repair, including a subsequent still-invalid capture. Expected compiler
messages are accepted only inside explicit bounds; unrelated warnings, API
errors and unknown commands still fail the run. Raw logs remain retained.

This suite found that the earlier Vulkan reload functions had no registered
commands: registration was inside OpenGL initialization. Both commands now
register in shared renderer initialization. The first failed Vulkan run is
retained, and the common laboratory now rejects unknown commands. The initial
GL diagnostic parser also needed to strip engine color codes, and the Vulkan
parser needed the specific follow-on compiler messages for the deliberately
undefined identifier. These parser failures remain failed reports. Accepted
source-edit evidence is `gl-v52b`, `vk-v52c` and `compare-v52b` under
`.tmp/vulkan-gap-closure/material-reload/`.

The final staged runtime also passes 35 OpenGL HDR/bloom controls, including
exact full-frame restoration after both GLSL reload commands. The Vulkan
render-target/startup, HDR and temporal-motion GPU groups pass without tracked
warnings. Both registered native tests pass; the 33-case compiler output is
byte-identical to all 24 independently validated v51 modules. The implementation
and retained evidence are bound by
`.tmp/vulkan-gap-closure/material-reload/checkpoint-v52-material-reload.json`.

The v53 fixture adds 26 authored-lighting controls with 27 semantic bindings,
point/projected lights, changed projection/falloff textures, material transforms,
vertex colors, conditions, multiple stages/lights, rotation and shadow/recovery
controls. Both v53e backend runs pass independently with exact image/shader/video
recovery and exact mapped-to-stencil equivalence on the custom receiver. Every
receiver comparison stays within one display level, including exact shadowed
receiver output. Projected-shadow full frames differ by at most one level. The strict
full-frame comparison still fails for three point-stencil background pixels
(maximum 30 levels) and nineteen point-map pixels (maximum five). The receiver
analysis only locates these failures; the original full-frame gate remains
failed. Reports and engine captures are retained under
`.tmp/vulkan-gap-closure/authored-lighting/`.

Development runs exposed fixture projection/removal errors, canonical transform
rounding holes, an overly restrictive stencil-admission check, the OpenGL
sampler-table conflict and a one-pixel Vulkan image-reload change. Those reports
remain failed evidence. The v53e sampler correction closes the last exact-recovery
failure without changing the threshold. Compiler qualification passes all 33
native cases and independently validates 24 newly generated modules with the
expanded uniform block; see `authored-lighting/compiler-qualification.json`.

The final v53e runtime also passes twenty ordinary authored-material comparisons
within one display level, with exact recovery, all 35 OpenGL HDR/bloom controls
and the three Vulkan render-target/startup, HDR and temporal-motion GPU groups.
These runs report no tracked API/engine diagnostics. Source/runtime identity,
the passing scoped checks and the failed full-frame comparison are retained in
`.tmp/vulkan-gap-closure/authored-lighting/checkpoint-v53-authored-lighting.json`.

The v56 [point-shadow correction](vulkan-point-shadow-parity.md#general-radial-depth-correction)
retains all 26 independent lighting controls and twenty ordinary material
comparisons. Its complete mapped-shadow image now stays within two display
levels of the unchanged GL reference. Three point-stencil background pixels
still fail, so the full lighting comparison remains failed overall. This
follow-up does not extend authored-shader admission or weaken the image gate.

The 3 November rerun on the integrated `29e237378` build and NVIDIA 591.74
again passes all 26 independent controls on each backend, including exact
reload/restart recovery and mapped-to-stencil equivalence on the authored
receiver. The unchanged full-frame two-level gate fails three stencil pixels
(maximum 30), one point-map pixel (maximum three), and one projected-map pixel
(maximum four). These small background/shadow-boundary differences remain
failures; the older mapped-shadow pass is not promoted to this driver/build.
Reports, source/runtime hashes and engine captures are retained in
`.tmp/vk-roadmap/lighting-gl/`, `lighting-vk/` and `lighting-comparison/`.

Remaining work includes:

- Extend source-selection qualification to additional retail/mod source pairs,
  filesystem overlays, and long repeated-edit sessions.
- Qualify additional custom-lighting combinations, including mixed stages,
  translucent receivers, alpha coverage, animated geometry and resource failure.
  The generic evaluator now handles the per-light semantics above in addition
  to numeric registers, view origin, current render viewport/scale and supported
  post-process size/color-space bindings. Full-scene shadow parity remains open.
- Qualify offscreen/HDR, texture generation, alpha and blend state, vertex
  colors, decals and animated geometry for authored programs, including
  flipped render-texture sampling in a real capture scene.
- Extend the GLSL language subset and qualify descriptor/pipeline/uniform/cache
  exhaustion, repeated source edits and device-failure recovery.
- Qualify translated ARB programs against real mod content and the
  `_currentRender` contract on a post-process scene (the laboratory renders
  through the fast no-post path, so its fixtures do not sample it).

The broader HDR/PBR/shadow, filtering, performance, CI, hardware and release
requirements in the [gap-closure plan](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md)
remain in force.

## Rejection, capacity and fallback policy (23 October 2026, VK-04)

**What draws.** A stock GLSL or ARB pair whose sources match a recorded
signature draws through its native Vulkan family. Every other program stage is
authored: GLSL compiles in process, ARB is translated first, and a valid
version draws through the generic executor. `r_vkARBTranslation 2` also
translates stock pairs as a diagnostic; when that translation fails, the native
family still draws and the log says so once per material.

**What is skipped, and how you see it.** The unit of fallback is the stage.
An authored stage that cannot draw is skipped, and the rest of the material and
frame draw normally. Nothing substitutes another program or a flat colour. Each
skip is reported once per program and reason until the next reload: compile or
translation failure, missing or mismatched texture, unavailable `fragmentMap`,
pipeline, uniform-ring or descriptor exhaustion, or no open rendering scope.
These lines replace the earlier single warning per program, which hid every
later reason. An authored ARB material that cannot draw at all is named once
per material, not once per session. The set of reported pairs is bounded at
512; later pairs are counted as suppressed. `reportShaderPrograms` now lists
every version as compiled or invalid, current or stale, with its draws and skips
since the last reload, the last skip reason and the compiler diagnostic.

**GL parity for shader parameters.** OpenGL's `RB_BindGLSLShaderParm` declines a
binding it has no value for, such as a per-light vector on an ambient stage or a
sample array, and still draws the stage: the parameter takes its registers or
keeps its default. Vulkan now does the same instead of skipping the stage.
Per-light bindings outside an interaction stay quiet, as on GL; any other
binding with no Vulkan value is reported once. Authored pipelines also honour
the stage's alpha-to-coverage bit, as GL's `GL_State` and the native families do.

**Bounded resources.**

| Resource | Limit | At the limit |
|---|---|---|
| Registered ARB program names | 1,024 (GL: 200, with 48 built-ins) | Warns once per name; the declaring stage is skipped. It no longer ends the session. |
| Authored program versions | 256 | A new version evicts the least recently drawn version that no current key names and that no frame in flight can still use. Its shader modules and pipelines are released. If every version is current or in flight, the stage is skipped and retried once the frames have retired; its sources are not re-read on every draw. |
| Compiler storage | 32 MiB | The version is invalid ("storage exhausted"); evictions return their bytes. |
| Authored draws per frame | 8,192 (four chained pools of 2,048) | The draw is skipped and reported. |
| Program pipelines (native and authored) | 512 (was 128) | Reported once; later program stages are skipped. |
| Per-frame vertex, index and uniform rings | 4 MiB uniform | Reported at the first overflowing frame and at each power of two, naming the ring. The old message called every ring "geometry". |

**Reload.** `reloadGLSLprograms` and `reloadARBprograms` clear the lookup state,
the skip counts and the reported pairs. Each version is re-validated on its next
use, and an unchanged source reuses its version, including an invalid one,
which reports its diagnostic again once. Repeated edits no longer exhaust the
table: stale versions are evicted as above.

### Laboratory results on the committed build (23 October 2026)

The three authored-program laboratories ran against a clean build of
`fef4b5875` (`0.13.2-dev+gfef4b587`, `dirty 0`): a detached worktree built and
staged as one debugoptimized runtime, then a lean laboratory copy with the
laboratory map compiled by its client. Each suite captured an OpenGL reference
and a Vulkan candidate with validation enabled, then ran its own comparison.

| Suite | Controls | GL | Vulkan | GL/Vulkan comparison |
|---|---:|---|---|---|
| `renderer_vulkan_material_programs.py` (authored GLSL, 32-parameter/8-texture capacity program, image/shader reload, partial/full restart) | 20 | pass | pass | pass: maximum one display level, 11 exact |
| `renderer_vulkan_material_reload.py` (source selection, live vertex/fragment edits, syntax failure and repair, partial pair, both reload commands, restarts) | 16 | pass | pass | pass: maximum one display level, 5 exact |
| `renderer_vulkan_arb_programs.py` (translated ARB programs, reload and restarts) | 15 | pass | pass | pass: maximum one display level, 5 exact |

The comparison limit is two display levels. Within each backend, the recovery
and deliberate-failure images matched their references exactly; the reload
suite's two deliberate failures each produced exactly one primary diagnostic.
The Vulkan logs contain no validation messages and none of the new skip or
approximation reports, so the bounded version table's reuse, repair and
new-version paths all drew correctly.

The first OpenGL capture of the GLSL suite was killed by the harness's 600 s
default after 19 of 20 captures, during the final full restart, while another
session's native tests loaded the machine. It is retained as
`gl-timeout-600s`; the rerun with a longer limit took 123 s. One reload capture
was interrupted when the runner paused for another session and was repeated.
Evidence: `.tmp/vkrm/labs/` in the main checkout.

Not covered: eviction at 256 versions and the other capacity limits, which
these suites do not reach; the authored-lighting laboratory
(`renderer_vulkan_material_lighting.py`), whose strict full-frame gate still
fails on three point-stencil background pixels; and a mod-content corpus.

## Compiler-storage recovery (3 November 2026, VK-04)

Repeated edits to a large shader could exhaust retained compiler storage before
reaching 256 versions. The old allocator reclaimed versions only at the version
limit, so further edits were rejected even though obsolete, retired sources
occupied most of the byte budget. Both GLSL and translated ARB now reclaim those
versions before compilation and before retaining its result. Current versions
and versions still referenced by frames in flight remain protected. Empty slots
are reused, and the report counts live versions rather than empty slots.

`reportShaderPrograms summary` prints the bounded totals without listing every
version. The original detailed report remains the default. The production-policy
test `renderer_vulkan_material_cache.py` exercises both budgets, exact-fit and
oversize requests, current/in-flight protection and reuse of reclaimed slots.
`renderer_vulkan_material_capacity.py` drives actual source edits, reloads and
draws under validation, using engine captures to require unchanged output and
restart recovery. Its small-source run reaches 256 versions; its padded-source
run reaches the byte budget first. These are separate from descriptor, uniform
and pipeline exhaustion qualification.

The RTX 4060 Laptop GPU passed both runs with validation enabled:

| Source-edit workload | Drawn versions | Peak live versions | Evictions | Peak retained bytes |
| --- | ---: | ---: | ---: | ---: |
| Small sources, 270 edits | 271 | 256 | 15 | 3,825,067 |
| 192 KiB padding, 180 edits | 181 | 158 | 23 | 33,424,823 |

All five captures in each run (initial, stressed, reloaded, partial restart,
full restart) match exactly; no version is invalid and no validation message
is logged. The old-renderer control reached 158 valid versions, then rejected
92 edits for storage exhaustion without evicting anything. That control timed
out after 250 edits, so it is a defect reproduction, not a completed suite.
The tested binaries, commands, source-edit ledger, harness snapshots, captures
and reports are retained in `.tmp/vk-roadmap/capacity-candidate-versions/`,
`capacity-candidate-storage/` and `capacity-storage/`. These development builds
carry a dirty `g908b2c01` version; the reports bind the exact runtime hashes.

## Draw-resource failure and capacity controls (4 November 2026)

A native driver-refusal reproduction found that failed descriptor allocation
consumed the uniform reservation made immediately before it. After 3,000 refused
bind attempts, no draw was recorded but the four-MiB ring cursor had advanced
from 256 to 4,193,328 bytes; later attempts failed for uniform exhaustion. The
reservation is now returned when descriptor allocation fails, before any
descriptor update or command records a reference to it. The same 3,000 refusals
leave the cursor at 256, preserve earlier bytes, and allow the next successful
draw to use the remaining budget.

`renderer_vulkan_material_resources.py` executes the production binding,
allocation, pool reset and pipeline-cache functions against counted Vulkan
driver doubles. Windows and Linux (WSL) controls pass:

| Resource | Exercised boundary and recovery |
| --- | --- |
| Uniform ring | The production four-MiB limit accepts 2,730 blocks with 256-byte alignment, then refuses without crossing guards; the next frame recovers. |
| Authored descriptors | Four pools of 2,048 draws reach 8,192; overflow leaves existing allocations intact. A larger host-side test ring isolates this quota; it is not a production setting change. |
| Program pipelines | 512 entries remain usable at capacity. Releasing one identity frees exactly its two variants; retained keys remain unchanged and the freed entries can be reused. |
| Failure paths | Pool creation/reset, shader/pipeline creation, texture residency and descriptor allocation refuse safely and recover; frame slots remain independent. |

The cache controls also distinguish target format, MSAA, orientation, alpha
coverage and vertex-stream variants. Reports and generated source remain in
`.tmp/vk-roadmap/material-resource-{baseline,candidate,windows,linux}/`; the
baseline is an intentional failed reproduction. These native controls do not
replace the remaining GPU pressure, authored-lighting or mod-content image
qualification. The ordinary source-version eviction evidence above retains its
separate scope.

`renderer_vulkan_uniform_pressure.py` now qualifies the real four-MiB ring on
the clean `323b44dab` build. It draws 128 identical authored stages on 16
coincident specimens (2,048 requested draws), then 32 specimens (4,096), and
returns to one stage. Both RTX 4060 Laptop and Intel Iris Xe reach the real
capacity warning and skip excess stages without changing the image. Draw/skip
counters prove that reducing the workload resumes drawing without further
skips; shader reload and full video restart also recover exactly. OpenGL and
both Vulkan adapters pass all eight independent controls, with all sixteen
paired full-frame comparisons within one display level. Validation reports no
messages. These deliberately redundant stages make safe rejection observable;
arbitrary omitted stages can still change a material's appearance.

Reports, fixtures, raw logs and engine images are retained under
`.tmp/vk-roadmap/uniform-fixed-{gl,nvidia,intel}/` and
`uniform-comparison-{nvidia,intel}/`. The earlier `uniform-probe-nvidia/` remains
failed evidence: its warning matcher omitted idTech log color codes. The
corrected checker accepts only the exact authored-program and four-MiB capacity
warnings, requires them inside the pressure interval, and rejects any other
diagnostic. The rebuilt ordinary authored GLSL suite also passes all 20 paired
controls with exact recovery in `material-normal-323-{gl,vk,comparison}/`.

Pipeline pressure then reproduced another recovery defect on the same build:
65 program identities with eight color-mask variants fill the shared 512-entry
cache. Existing keys and an unchanged reload remain usable, but a new program
after reload stays absent (29,943 changed pixels, maximum error 174) until a
video restart. `pipeline-new-program-nvidia/` retains that failed control;
`pipeline-probe-nvidia/` retains the narrower unchanged-program pass.

Binding now retries after reclaiming retired stale versions when the pipeline
cache is full, using the same current/in-flight protection as version and byte
pressure. It does not evict on an ordinary driver allocation failure with free
cache capacity, nor evict just to service an existing cache hit. The finite
scan skips pipeline-free versions and stops if no safe victim remains. Native
policy and binding-retry controls pass on Windows and Linux (WSL).

The clean `dc81503ee` build passes `renderer_vulkan_pipeline_pressure.py` on
RTX 4060 Laptop and Intel Iris Xe with validation enabled. Each run exercises
63 programs below capacity, 65 at pressure, reuse of an existing key, a new
66th program after reload, settled recovery and full restart. Both adapters
reclaim exactly one retired version, draw the new program without skips, and
preserve every image exactly. OpenGL and both Vulkan adapters pass all eight
independent controls; all sixteen paired captures differ by at most one display
level. No validation or unexpected diagnostic is logged. The original pressure
warning remains required, and no warning is allowed after recovery.

Reports and engine images are retained in
`.tmp/vk-roadmap/pipeline-fixed-{gl,nvidia,intel}/` and
`pipeline-comparison-{nvidia,intel}/`. The exact package is
`runtime-dc81503e.zip`, SHA-256
`2c161eb5103891d3e8704d1d54d1e84b22c1fbfb1e523b5bb0dde7f03d0a1c38`.
Run either pressure harness with `--runtime-root`, `--basepath`, `--output-dir`
and `--backend gl|vk`; `--device` selects the Vulkan adapter. Each has a separate
`--compare GL_REPORT VK_REPORT --output-dir ...` mode. They require the existing
compiled original PBR laboratory runtime and keep writable fixtures in the
isolated test profile. Descriptor-driver refusal was then a native test only;
the GPU drill below now covers it. Broader authored-lighting/mod content and
the existing strict five-pixel lighting mismatch remain open.

## Descriptor refusal on real drivers (6 November 2026)

`r_vkMaterialDescriptorPoolTest N` (developer drill, default 0) gives each
frame slot one real descriptor pool sized for N authored draws and never
chains a second one. Later authored draws in that frame therefore reach the
driver's own `vkAllocateDescriptorSets` refusal rather than the renderer's
four-pool quota. Changing the value replaces a slot's pools at its next frame
start, after its fence; clearing it restores the production pools.

`renderer_vulkan_descriptor_pressure.py` stacks 128 identical authored stages
on one specimen and sets the drill to 32 draws. Its cases are: below the limit,
drill, drill cleared, shader reload, video restart with the drill still set,
and video restart with it cleared. Skipped stages must leave the image
unchanged. The refusal diagnostic may appear only inside the two drill
intervals. Skips must stop once both frame slots have switched back, and draws
must resume. Reload and restart must recover exactly.

The clean `1243eb6a` build passes on the RTX 4060 Laptop (591.74) and the Iris
Xe (101.5590), with validation enabled and no validation messages. Both drivers
refused the over-quota allocations: 3,264 skips during the drill and 3,360
after the drill restart. Another 224 skips landed during the one-to-two-frame
slot changeover, after which the count stayed fixed while draws resumed. Each
adapter's eight images match its own base exactly. All sixteen GL/Vulkan pairs
differ by at most one display level. The native resource test also executes
the drill's pool replacement and driver-refused path against its doubles.
Reports and engine images are in `.tmp/vk-roadmap/nov6/descriptor-{gl,nvidia,intel}/`
and `descriptor-comparison-{nvidia,intel}/`. This closes the descriptor-driver
failure subpoint only. It does not cover broader authored lighting, mod
content or the strict lighting image gate.

## Clip-space depth control (6 November 2026, VK-04)

OpenGL clips depth to [-w, w] and stores z/2w + 1/2 in the depth buffer.
Vulkan's default clip range is [0, w], so the backend remapped every matrix it
submitted on the CPU (`VK_FixupClipSpaceZ`), and an authored vertex shader
that moves the position remapped it in the generated shader. Those products
round differently from OpenGL's. In the strict lighting comparison that left
three point-stencil background pixels failing by up to 30 levels.

When the device offers `VK_EXT_depth_clip_control`, every graphics pipeline
now clips in OpenGL's range (`negativeOneToOne`), matrices pass through
unchanged and depth rounds as on OpenGL. The CPU-built point-shadow depth row
follows the same convention. A displaced authored position keeps its OpenGL
depth: the generated vertex shader skips its own remap while the uniform
block's `textureOrientation.y` is 1. `r_vkDepthClipControl 0` (read when the
device is created) keeps the remap for comparison. The log prints
`Vulkan: clip-space depth [-1, 1] (VK_EXT_depth_clip_control)`, or
`[0, 1] (remapped projections)` without it. Both qualification adapters (RTX
4060 Laptop, NVIDIA 591.74; Iris Xe, Intel 101.5590) expose the extension.

Results on `e5ad574d4` with this change, validation enabled and no
validation messages:

- Lighting: all 26 independent controls pass on each backend. On NVIDIA the
  three stencil pixels are gone; one point-map pixel (maximum three levels)
  and one projected-map pixel (maximum four) remain. They are single
  shadow-filter taps at near-edge receivers and are identical with
  `r_vkDepthClipControl 0`, so they do not come from the depth convention.
  Intel differs from the NVIDIA GL reference in one stencil pixel (30) and
  nine point-map and eight projected-map pixels (at most four). The strict
  full-frame gate stays failed.
- The first run of the authored laboratories exposed the double remap above:
  `authored-glsl-offset` differed in 14,185 pixels by up to 174 levels and
  `authored-reload-vertex_edit` failed too. With the shader fix, authored
  GLSL (20 controls), live reload and repair (16) and translated ARB (15) all
  pass within one display level.
- The view, tiled and image capture sequences are unchanged by it.
- The 29-map stock single-player sweep (OpenGL, NVIDIA and Intel Vulkan, Balanced preset with 2x MSAA, validation on) passes 84 of 87 runs and all 58 warning comparisons. The two failures are `mcc_2` captures on Vulkan taken 120 frames in, which land on the map's scripted fade to black. At 240 and 600 frames both renderers pass, and an older build failed the same capture on OpenGL instead.

Reports and engine images: `.tmp/vk-roadmap/nov6/depthclip/`.

## Dependency provenance

[glslang 16.6.0](https://github.com/KhronosGroup/glslang/tree/16.6.0) is pinned by
archive SHA-256 in `subprojects/glslang.wrap`. Its BSD, MIT, Apache-2.0, GPLv3 with
Bison exception and permissive preprocessor notices were reviewed for this
GPLv3 repository before incorporation. The native Meson overlay selects the
upstream compiler/SPIR-V/resource-limit sources, without changing their source
or notices. Optional HLSL, SPIRV-Tools and upstream test dependencies
are excluded. The complete upstream license is retained in
`LICENSES/glslang.txt` and included in the staging manifest under
`licenses/`; the README credits the original project.
