# Native vector coverage antialiasing

8 September 2026. Native vector elements now compute analytic pixel coverage
from their editable paths. This advances the renderer requirements in the
[replacement plan](../plans/idtech5-ui.md); it does not qualify the entire
component kit, composition system, font backend or migrated GUI corpus.

## Coverage and paint contract

Path flattening still uses the final output transform and a 0.15-pixel curve
error bound. libtess2 resolves the requested fill rule or the union of stroke
outlines and returns normalized boundary contours. These boundaries include
holes and intersections and exclude internal triangulation edges.

The coverage compiler integrates those boundaries over unit output-pixel cells.
For an oriented edge, its contribution to cell `(X,Y)` is
`integral(clamp(x-X, 0, 1) dy)` within the cell's vertical interval. This is
Green's theorem applied to the clipped area. Horizontal edges contribute zero;
other edges split at row boundaries and columns that they cross. The clamped
linear integral is evaluated directly as clamped trapezoids, so coverage does
not depend on a finite set of sample positions.

All boundary contributions are summed before applying color. A hole subtracts
its area; separate pieces occupying the same pixel add their disjoint coverage.
An opaque covered interior remains opaque without triangle seams. Translucent
stroke segments are united before this calculation, so their joins and crossings
do not repeatedly blend the stroke's alpha.

Each pixel's resolved coverage multiplies the paint's premultiplied RGBA exactly
once. Paint then passes through the existing canonical opacity and engine blend
contract. Linear gradients retain geometry at every stop plane and use the same
premultiplied interpolation as the geometric compiler. Paint is interpolated
across the coverage cells; this is a box area coverage filter, not analytic
integration of a varying paint color over a partially covered shape.

The result is untextured native geometry. Adjacent cells with equal coverage
merge into spans, and identical spans merge vertically into quads. A large
axis-aligned solid interior can therefore become one quad. No baked bitmap,
coverage texture atlas, fixed-resolution SVG raster or GPU-specific path is
introduced. Density and fractional position changes regenerate coverage at the
actual output pixel grid.

## Bounds, hit testing and lifetime

`VectorOptions::antialias` defaults to true. `pixelBounds` optionally limits
coverage to integer output cells with exclusive right/bottom edges. Runtime
elements pass the intersection of viewport and retained scissor bounds. Cropping
the coverage work retains contributions from boundaries outside those bounds;
a huge shape surrounding the viewport still fills the viewport correctly.

The retained render manager continues to own final clipping and resource
lifetime. A subsequent [cache and measurement checkpoint](runtime-performance.md)
separates opacity from path compilation and reuses whole-pixel translations with
bounded padded coverage regions. Video restart recreates the geometry through
the existing document reload path.

The first gameplay run exposed an existing surface-capacity check that tested
only the previous vertex count. Consecutive valid retained batches could then
combine into a surface larger than the frame allocator's block. The GUI model
now splits before adding an incoming batch that would exceed its vertex limit;
the clipping path checks each resulting polygon too. Retained submissions still
use whole-triangle batches of at most 12,000 vertices. This fixes the observed
`R_FrameAlloc: invalid size 1150080` failure without increasing allocator limits.

`HitTestPath` explicitly uses geometric triangles, independent of antialiasing.
A partially covered pixel outside the mathematical shape cannot enlarge the
target, and holes remain excluded. This API is still awaiting semantic input
and world-GUI routing in the larger replacement work.

Coverage work has explicit limits: 65,536 rows per clipped edge, 2,097,152
edge-work steps and 1,048,576 difference events per fill/stroke compilation.
The existing output vertex limit also applies to coverage quads and gradient
subdivision. Invalid or unclosed coverage reports an error; budget failure
retains the caller's previous complete mesh. No coarse or aliased fallback
silently substitutes for a failed path.

## Verification

The native vector suite compares coverage against an independent implementation
that clips ordinary triangles to each pixel square and sums their areas. It
checks affine and mirrored holes, nonzero/even-odd behavior, self-intersections,
rounded translucent stroke unions and fractional translation. Analytic checks
cover half-pixel diagonal coverage, alpha-weighted area conservation, 0.1-pixel
strokes, disjoint fragments in one pixel, exact geometric hit targets, transparent
gradient stops, viewport bounds and transactional budget failures.

The retained suite continues to check native vector submission, parent/child
opacity and inherited clipping at two densities. All three native targets pass
on Windows. The additional gradient/coverage checks also pass after the final
test update. The client, dedicated server and both renderer modules built and
staged successfully through the MSVC-aware Meson wrapper.

### Final gameplay captures

The unchanged `vector-smoke.q4ui` fixture was captured through the registered
engine screenshot command after entering gameplay. Both runs were hidden and
windowed, with mouse/controller input disabled and no host input injection.
Render-target TGA files were reviewed visually; the rails and curved ring/check
show smooth coverage, holes remain clear, and gradients/labels are retained.

| Capture under `.tmp/ui/coverage/` | Scenario | Outcome |
| --- | --- | --- |
| `sp-gl-125-restart-qualified` | SP `airdefense1`, OpenGL, 125% density; full video restart and entry-timeline replay | Exit 0; 0 warnings, 0 errors, 0 retained diagnostics; two timeline plays confirmed |
| `mp-vulkan-200-final` | MP `q4dm1`, Vulkan, 200% density; reduced motion and explicit auto-join | Exit 0; 0 errors/retained diagnostics; 93 existing warnings, no new warning messages compared with the preceding MP geometry capture |

Both used client SHA-256
`61ac37020ce580f8ef0917c0c7838fe2958a41810b9d496a2846f42b7ce00048`.
OpenGL module SHA-256:
`213b99ddd3ee63000454f27ac023e74db405f442bbe2cf29a62bdce8b6a3f4fc`.
Vulkan module SHA-256:
`d48be7c2353a831b3b32623a2d203688a078c1fdd0c1853e725229e2ac42accb`.
Fixture SHA-256:
`78dc7bc86a65d6983ac23646f5534f205ec670bb85ade94cffecb4c8da921bbe`.

SP screenshot SHA-256:
`9cf6cad40acdafe6496cbbe2527e1ef4bc823a2e7466873db159ea1290cc6e49`.
MP screenshot SHA-256:
`a9cdf92fd326dbb992747e8d8b09bb6db5e419cf87da708784b3789728b7d386`.
Capture reports include complete launch arguments, binary/log hashes and keep
`replacement_acceptance: false`. Their reviewed coverage status does not imply
production artwork or complete replacement acceptance.

The earlier `sp-gl-125-restart` and `sp-gl-125-restart-final` attempts failed with
the allocation error described above. The latter still used the old renderer
modules; rebuilding only the client/dedicated targets does not rebuild those
DLLs. The final successful evidence includes both rebuilt/staged modules and
supersedes those failed attempts. The MP warnings are the existing AAS, optional
sound and non-precached-content diagnostics, not a clean-log result.

Reproduction uses the [capture harness](../../../tools/ui/capture_legacy_baseline.py)
and new output directories:

```powershell
& tools/build/meson_setup.ps1 compile -C builddir openQ4-client_x64 openQ4-ded_x64 renderer-gl_x64 renderer-vk_x64 openq4-retained-ui-test openq4-ui-document-test openq4-ui-vector-test
& tools/build/meson_setup.ps1 test -C builddir --no-rebuild openq4-retained-ui openq4-ui-document openq4-ui-vector
& tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
$assets = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4'
python tools/ui/capture_legacy_baseline.py --assets $assets --mode sp --renderer gl --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 1.25 --timeline enter --video-restart --output .tmp/ui/coverage/sp-gl-review
python tools/ui/capture_legacy_baseline.py --assets $assets --mode mp --renderer vulkan --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 2 --timeline enter --reduced-motion --output .tmp/ui/coverage/mp-vulkan-review
```

## Remaining quality work

This implementation addresses native path edge coverage. Isolated group
composition, masks, image/radial/conic paints, additional stroke features,
font rendering and the production component/artwork corpus remain incomplete.
Separate paint operations still compose independently; this does not provide
an isolated layer or shared coverage between independently painted shapes.

The subsequent [performance checkpoint](runtime-performance.md) removes redundant
compilation during opacity/whole-pixel changes and patches tessellation to double
precision. Optimized-build and GPU costs still need qualification.
The render bridge still quantizes premultiplied channels to 8 bits. Temporal quality,
the full density/aspect/backend matrix, world projection and non-Windows targets
need broader qualification before Stage 3 can close.
