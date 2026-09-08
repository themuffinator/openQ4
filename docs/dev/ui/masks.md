# Editable vector alpha masks

8 September 2026. Canonical `.q4ui` nodes can mask their completed subtrees with
editable vector paths. This adds shaped content clipping, holes and soft reveals
to the [retained composition runtime](composition.md). Full GUI translation,
production artwork and the visual editor remain incomplete under the
[replacement plan](../plans/idtech5-ui.md).

## Source and appearance

Every node type accepts `mask: {"paths": [...]}` and optional mask `extensions`.
Paths have their own stable IDs, independent of the node's artwork path IDs.
They use the existing [vector schema](vector-paths.md): dp/relative coordinates,
lines, quadratic/cubic curves, multiple contours, nonzero/evenodd fills, strokes
and solid/linear paints. The [document editing API](document-format.md) preserves
surrounding comments and source bytes, validates edits transactionally and
reports mask errors at their JSON pointer and line/column.

Mask coordinates resolve against the node's border box, including padding, and
follow its transform and density. Paths paint in source order into transparent
black. Their resulting **alpha** multiplies every channel of the completed
premultiplied subtree. RGB is ignored: opaque black and white masks reveal the
same content. Multiple mask paths combine by source-over; use contours in a
single evenodd/nonzero path for holes. Mask absence leaves the node unmasked;
an explicitly empty path array hides it. Intrinsic paint alpha, nested masks and
animated group opacity each contribute once, inside-out.

Mask geometry shares the artwork compiler, coverage integration, clipping and
integer-translation cache. Curves use a tighter 0.025 physical-pixel flattening
bound because their edges may cross high-contrast text and whole subtrees.
Analytic pixel coverage is still evaluated on the flattened geometry; RGBA8
submission and composition introduce bounded quantization. Stationary masks and
opacity changes reuse their compiled geometry. These temporary RGBA targets are
rendering resources, not authored bitmap art.

## Renderer and resource lifetime

Derived RmlUi markup uses a native `q4-mask(alpha)` decorator as a mask image.
All canonical nodes use native elements that own separate artwork and mask
geometry caches. Mask rendering therefore sees the actual layout box and
transform, with no parallel layout implementation or generated vector image.

`SaveLayerAsMaskImage` copies the mask into an independently leased slot. The
snapshot remains immutable until its filter is released, even if the original
layer is popped, reused or drawn again. Filters copy content to separate scratch
before applying masks, preserving the source for subsequent composition. The
engine multiplies scratch RGBA by sampled mask alpha using destination blending,
then composites the filtered result with group opacity. Source and destination
never sample the same attachment. Full clipped quads include zero-alpha regions
outside paths, so holes remove content instead of leaving the old destination.
GL/GLES and Vulkan texture origins are resolved at the existing host boundary.

The runtime allocates the lowest available target slot from a pool of 48; the
engine also caps the highest slot at 256 MiB of full-viewport RGBA8 storage.
Stack depth and simultaneously leased targets are measured separately. Snapshot
leases end at filter release and scratch leases after composition. A viewport
generation rejects stale snapshots after any resize, including resizing back
to the original dimensions. Allocation failure diagnoses once, suppresses the
remaining retained frame and unwinds to the base output. The next frame can
recover. Named GPU-image storage still needs trimming across repeated resizes;
the allocation guard is not a measurement of total driver memory.

No engine/game interface signature or API version changes in this checkpoint.
Build and stage the client and **both renderer modules**: generated mask
materials are implemented by the renderer modules. Existing renderer API 13 and
game API 47 requirements from the composition checkpoint still apply.

## Verification

The native retained test independently rasterizes triangle interiors into
premultiplied sample buffers. It checks nested masks and opacity against expected
overlap colors, alpha rather than luminance, a transparent hole, a chamfer's
half-covered pixel, soft gradient reveals, empty masks, byte-preserving source
round trips, failed edits, unchanged-geometry reuse and live 100/125/150/200%
density changes. Failures at each of eight mask/content/snapshot/scratch
allocations must restore base output and recover without leaking target leases.
Document, retained runtime and vector native suites pass.

The [mask fixture](../../../tools/ui/fixtures/mask-smoke.q4ui) compares matching
panels containing localized text and a nested chamfered control mask. One panel
also has a larger outer chamfer, an offset cubic hole and a horizontal alpha
gradient. The [capture verifier](../../../tools/ui/verify_mask_capture.py)
integrates horizontal spans at 128 subpixel rows, solving the cubic directly.
It does not call the runtime tessellator. Each masked pixel is checked against
`reference * maskAlpha + backdrop * (1 - maskAlpha)`, with a five-level maximum
channel error and no visible leakage into fully hidden pixels.

Final windowed, hidden engine-command captures entered SP `game/airdefense1`
and MP `mp/q4dm1` with host input disabled. The GL run also performed a full
video restart. Both screenshots were visually inspected and compared against
the independent oracle. Capture metadata and logs are retained under
`.tmp/ui/masks/`; `summary.json` records exact package/source hashes and warning
comparison. These are development fixtures, with `replacement_acceptance:false`.

| Capture | Comparison | Diagnostics | Shared GUI owner |
| --- | --- | --- | --- |
| `sp-gl-125-precision`, GL 1280×720 at 125% | 157,080 pixels; max error 3.413/255, p99 1.362; 18,388 hidden pixels, zero leakage | 0 warnings/errors | 0/18 owned; fallback |
| `mp-vulkan-200-precision`, Vulkan 1920×1080 at 200% | 401,472 pixels; max error 2.602/255, p99 1.348; 47,364 hidden pixels, zero leakage | 93 existing warnings, 0 errors; no new warning messages versus composition checkpoint | 0/20 owned; fallback |

The three-mask fixture uses four simultaneous target leases and a maximum stack
depth of three. Each 60-frame profile reports 180 snapshots and mask applications.
Post-restart GL reuses all mask/artwork geometry with zero compiles/uploads.
MSVC debug `/Od` engine-side CPU submission p50/p95 is 8.987/9.519 ms after
GL restart and 12.798/13.483 ms on Vulkan. Initial samples peak at 53.100 and
69.904 ms respectively. These are **not** acceptable shipping performance
claims: optimized builds, submission cost and GPU timing still need work.
Correct pixels here qualify the GL/Vulkan fallback paths, not shared GUI ownership.

An earlier complete-package capture failed the five-level comparison at curve
edges (GL 12 pixels, Vulkan 44 pixels). Tightening the mask flattening bound
resolved those failures without weakening the comparison. An earlier client-only
build also demonstrated why the renderer modules must be staged together:
older modules could not generate the new mask materials. Final captures use the
same fully staged package, verified against every executable/module hash.

| Artifact | SHA-256 |
| --- | --- |
| Fixture source | `6f654cfaa6aa84fc59164808e98b1367fb68daa25a302ca3457f406240a32042` |
| GL engine screenshot | `3fa6175804123531970249c933e797afc87d5b79dd0eab70b8ba135ed922833a` |
| Vulkan engine screenshot | `d5d042156c8fae1d7c2017df102d919cca13b99ad9b7559a31a165cff52a1b24` |
| Client | `7bbd570f67edfa9bac1467b44563e99896cd109ed5ee33b6a95123fcd8f42937` |
| GL module | `cb046811f1ad6b5b09a5e7dd50253b36b0e3756c169bf8b4f151c364b57a1e93` |
| Vulkan module | `89f416a156aee3b697af939bfe1a9b72d1f586e5d228f4b12d79b44bd29e4eb6` |
| SP module | `e7e5418d797ad667dfff627dfff5d64a22782897729299c365dc3337b8fdc7da` |
| MP module | `9772e16271698dd22801b9c05fe75df6d91c872cc46332063ccdadecb7a36aaf` |

Reproduce with a fully built/staged package and a new output directory:

```powershell
& tools/build/meson_setup.ps1 compile -C builddir
& tools/build/meson_setup.ps1 test -C builddir --no-rebuild openq4-retained-ui openq4-ui-document openq4-ui-vector
& tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode sp --renderer gl --retained-document tools/ui/fixtures/mask-smoke.q4ui --density 1.25 --shared-gui --video-restart --profile-frames 60 --output .tmp/ui/masks/sp-gl-new
python tools/ui/verify_mask_capture.py .tmp/ui/masks/sp-gl-new/save/baseoq4/screenshots/ui-baseline.tga --density 1.25
```

For MP use `--mode mp --renderer vulkan --density 2 --width 1920 --height 1080`
and a separate output directory; the harness explicitly sets `ui_autoJoin 1`.

## Remaining work

This is canonical alpha masking, not implementation of RmlUi's separate
stencil-style `EnableClipMask`/`RenderToClipMask` interface. General blend modes,
same-layer/backdrop filters, reusable layer textures and other effects remain
unsupported. Canonical properties do not expose those unsupported operations.
Mask control points and paints are source-editable; direct timeline/game binding,
mask-aware interaction policies, editor tools and world-surface integration remain
open. Shared GUI ownership, cropped targets, total GPU pool trimming, optimized
CPU/GPU pacing, Linux/macOS/GLES and the full GUI corpus remain qualification
gates. No stock GUI resource becomes accepted merely by passing this fixture.
