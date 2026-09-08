# Isolated retained UI composition

8 September 2026. The replacement runtime now fades complete subtrees through
transparent render targets. This advances Stage 3 of the
[full replacement plan](../plans/idtech5-ui.md). [Vector alpha masks](masks.md)
have subsequently been added; other composition effects,
full font/artwork qualification, the editor and all GUI migration remain open.

## Rendering contract

Canonical node opacity covers its own paint and descendants. Each group paints
with its intrinsic colors into transparent black, then the completed result is
composited once onto its parent. A half-opacity blue child over an opaque red
parent produces purple inside that parent; fading the parent over green mixes
the completed purple with green. Multiplying every primitive's alpha separately
would expose the wrong amount of the red paint and background.

Canonical markup resets RmlUi's inherited primitive opacity to 1. Authored and
animated opacity drives its opacity filter, which provides the subtree rendering
boundary. Each canonical node keeps a local stacking context even at opacity 1,
so crossing that boundary cannot reorder descendants against adjacent groups.
Opaque groups need no temporary layer. Fractional paint alpha remains unchanged.
Fades now reuse both compiled vector paths and their vertex tint buffers.

The retained renderer sends physical-pixel layer operations through `Host`.
The engine uses its existing command-ordered render textures on OpenGL and
Vulkan. Layer images contain premultiplied RGBA8; composition samples them at
their native dimensions, without an atlas or downscale. These are transient
composition targets, not authored bitmap artwork. The render manager's exact
rectangular clip applies to composition, and geometry retains its own clipping.
The host resolves the GL/GLES and Vulkan attachment-origin difference when
sampling a layer, independently of the document transform and display scale.

Straight-alpha font/image materials keep their normal RGB blend but write
source-over alpha independently. `GLS_ALPHA_COVERAGE` carries this distinction
through the legacy and shared material paths and Vulkan pipeline keys. Vector
paint and completed layers use premultiplied source-over for both RGB and alpha.
This prevents translucent glyph edges from writing squared alpha into a layer.

The runtime leases reusable target slots for stack layers, mask snapshots and
filter scratch. The current pool uses full UI viewport dimensions, at most 48
targets and a 256 MiB limit for the highest allocated slot
at that viewport size. Resizing invalidates the pool; video/language recreation
clears targets and document
geometry. The image manager owns named image storage and the renderer defers
target deletion until queued commands are safe. Unused named image slots may
retain their backing storage until reused or renderer shutdown; trimming those
slots and measuring total GPU memory across repeated resizes remain open.
Allocation failure is diagnosed, suppresses the affected frame's remaining
retained draws and restores the base
target. It cannot redirect the failed group onto gameplay by accident.

This checkpoint implements normal source-over group opacity; the subsequent
mask checkpoint adds canonical alpha masks through RmlUi mask images. General
blend modes, same-layer/backdrop operations, reusable layer textures, RmlUi's
separate stencil-style clip-mask API, blur and other filters remain unimplemented. Bounded regions,
deeper/extreme-resolution stress and GPU frame pacing remain qualification work.
The current engine preview's base target is the normal 2D output; world/editor
target ownership is part of the pending integration work.

## Module compatibility

`idRenderSystem::ClearRenderTarget` now accepts alpha, defaulting to 1 for all
existing scene clear calls. UI layers explicitly pass zero. Renderer API 13 and
canonical game API 47 require a matching engine, GL/Vulkan modules and SP/MP
modules; older binaries are rejected by the existing loader version checks.
Canonical game changes are in the companion repository. Build and stage the
whole development package when updating this branch.
The paired canonical game revision is
[`cc83b0c972c0bef9474462461818096dd690277e`](https://github.com/themuffinator/openQ4-game/commit/cc83b0c972c0bef9474462461818096dd690277e).

## Verification

The native retained test evaluates triangle interiors at selected points and
performs independent source-over composition into transparent layer buffers.
It checks nested overlap colors, animation samples, zero-opacity endpoints,
allocation failure and recovery, and target restoration. Existing density,
clipping, geometry-cache, fresh-versus-cached and restart/lifetime tests pass.
The document and vector test targets also pass. The benchmark now uses no-op
layer host calls alongside its existing empty draw host, so its timings exclude
the engine and GPU composition cost.

The [composition fixture](../../../tools/ui/fixtures/composition-smoke.q4ui)
places matching Quake 4-style vector panels at full and half group opacity over
a constant, brighter backdrop that exposes errors in translucent edges. Both
contain a nested translucent control. The
[capture verifier](../../../tools/ui/verify_composition_capture.py) compares each
matching pixel against `(full-opacity result + backdrop) / 2`, including frames,
gradients, controls and localized glyph edges. It rejects empty output and any
channel mismatch above 4/255. It reads the engine's TGA screenshot directly.

```powershell
$assets = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4'
python tools/ui/capture_legacy_baseline.py --assets $assets --mode sp --renderer gl --retained-document tools/ui/fixtures/composition-smoke.q4ui --density 1.25 --shared-gui --video-restart --profile-frames 60 --output .tmp/ui/composition/sp-gl-review
python tools/ui/verify_composition_capture.py .tmp/ui/composition/sp-gl-review/save/baseoq4/screenshots/ui-baseline.tga --density 1.25
python tools/ui/capture_legacy_baseline.py --assets $assets --mode mp --renderer vulkan --retained-document tools/ui/fixtures/composition-smoke.q4ui --density 2 --width 1920 --height 1080 --shared-gui --profile-frames 60 --output .tmp/ui/composition/mp-vulkan-review
python tools/ui/verify_composition_capture.py .tmp/ui/composition/mp-vulkan-review/save/baseoq4/screenshots/ui-baseline.tga --density 2
```

Runs are hidden and windowed with host mouse/controller input disabled. SP uses
`airdefense1`; MP uses `q4dm1` with explicit auto-join. Screenshot verification
does not qualify the full 271-resource corpus or any production screen.
The full goal remains active.

## Recorded gameplay results

Final reports are under `.tmp/ui/composition/`, using the matched API 13/47
build. The TGA images were inspected and the numerical verifier passed:

| Directory | Configuration | Compared pixels | Maximum channel error | Result |
| --- | --- | ---: | ---: | --- |
| `sp-gl-125-qualified` | SP/OpenGL, 1280×720, 125%, full video restart | 157,080 | 2.5/255 | Exit 0; zero warnings, errors or retained diagnostics |
| `mp-vulkan-200-qualified` | MP/Vulkan, 1920×1080, 200%, auto-join | 401,472 | 1.5/255 | Exit 0; zero errors or retained diagnostics |

Each 60-frame profile records 180 layer pushes and compositions with peak
nesting depth 2. SP repeats the profile after video restart. The final SP CPU
p50/p95 values are 1.43/1.75 ms initially and 1.40/1.54 ms after restart; MP is
2.01/2.35 ms. These are local unoptimized debug CPU measurements including GUI
flushes, not GPU completion or frame pacing. Cold maxima remain 26.41 ms for SP
and 35.21 ms for MP. GPU storage is separate from the CPU buffer counters.

Both captures request `--shared-gui`. **The ownership counters report fallback
for every captured GUI view** (`GL=0/16`, `VK=0/10`). Thus these are verified
outputs from the GL and Vulkan fallback draw paths, not evidence that the shared
GUI owner is qualified. Source contracts now represent separate alpha factors,
but full shared-owner coverage remains required. Do not infer ownership from
the enabled CVar or a correct image alone.

MP retains 93 existing stock-content warnings. Comparing warning messages with
the preceding performance capture found no new messages. The earlier failing
Vulkan capture remains under `mp-vulkan-200-api47`: it exposed the layer-origin
error, with maximum mismatch 97.5/255, before the host correction. It is not
accepted evidence. Final capture metadata records `replacement_acceptance:
false`; no production GUI entry has been marked migrated.

The three retained/document/vector native targets pass. Eight affected existing
source-contract checks also pass: game-module selection, level-load cache, MVD
server API, IPv4, GPU frame timing, multiplayer flat items, temporal presentation
and weapon-wheel time scale. These checks caught the companion renderer-header
mismatch during development; the final public headers now match. Client,
dedicated, GL/Vulkan and both canonical game modules were rebuilt and staged.

Reproduction hashes (SHA-256):

| Resource | Hash |
| --- | --- |
| Client | `fced29b0f7faaaa78ea90d810dd30b9341c614e73d545a04e5ce18a85ce22263` |
| GL module | `4deef6d14fac8e15a5b3f54404596e79029c870bedd6d139ed52538cfa83ab2b` |
| Vulkan module | `91666f4843ecac24118568ef974a49ebffd4bfd8512452671abc6453a7027fc8` |
| SP module | `c2ddb97018ecf93ea2dea75146566778ce926e20ff5e69d863bf1a7f73ae450c` |
| MP module | `f8feda507dcc56c265ea0ceff72525b01de4baf754c2ca4a5aba5eb8ef3509f7` |
| Fixture | `537727a464bdc22ecdf0b049a2af083cf8ef99091363641110a1f810dbdee490` |
| SP screenshot | `35ea3f5325a630d6c46ce7d2985ca3ece49eb4d78c3bc7248882a2f9ab59d076` |
| MP screenshot | `8c4970a4e6cf44b79fdfea01dbd8a3136998e6f184a4f255146efcb5fff15753` |
