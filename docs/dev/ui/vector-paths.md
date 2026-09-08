# Native vector path checkpoint

8 September 2026. Canonical `.q4ui` documents now contain editable vector paths
rendered as native geometry through the engine. The runtime and future editor
share the path model/compiler. This does **not** complete the production vector
renderer or artwork migration. [Coverage antialiasing](coverage-antialiasing.md)
has subsequently been added, followed by [isolated opacity layers](composition.md).
Masks, broader layer effects, additional
paint/stroke features and full visual qualification remain Stage 3 requirements
in the [plan](../plans/idtech5-ui.md).

## Source model

A `vector` node participates in ordinary retained layout and can own children,
such as a panel's controls or a button's label. Its `paths` array is painted in
source order before its children. The vector viewport is the node's **border
box**, including padding. Layout places children inside that padding, so a frame
surrounds its content inset without a separate stretched image.

Each path has a stable `id`, unique within the node, and `commands`. Commands
have stable IDs unique within their path. They use the same validated,
comment-preserving [source editing API](document-format.md).

| Command `op` | `points` | Meaning |
| --- | --- | --- |
| `move` | One point | Begin contour |
| `line` | One point | Endpoint |
| `quadratic` | Two points | Control, endpoint |
| `cubic` | Three points | Control 1, control 2, endpoint |
| `close` | Omitted or empty | Close contour |

A path starts with `move`. Multiple contours and intersecting fills are supported.
Fill implicitly closes open contours; their strokes remain open without an
explicit `close`. A drawing command after close begins another subpath at the
closed start. Consecutive duplicate points are removed without incorrectly
turning an open return-to-start stroke into a joined closed stroke. Elliptical
arc syntax and SVG import/export remain unimplemented and are rejected.

Points are `[x,y]`. Each coordinate is a numeric dp offset or an object such as
`{"fraction":1,"dp":-12}`. The fraction refers to the local width/height; the dp
offset stays independent of that dimension. Either field may be omitted and
defaults to zero. Thus `[12,0]` and `[{"fraction":1,"dp":-12},0]` delimit a top
span with fixed 12 dp corner cuts. Resizing lengthens the span while retaining
45-degree corners. Coordinates are data, not script strings.

Node layout/opacity/transform can animate with canonical timelines. Path points
and paint fields are source-editable but are not yet binding/timeline targets.
Reusable components, shared path assets and path editor interactions remain open.

## Fill, stroke and paint

`fillRule` is `nonzero` (default) or `evenodd`. Actual winding rules preserve
holes without overpainting the background. Stroke outlines are united under a
nonzero fill before triangulation, so overlapping segment rectangles, caps and
joins do not repeatedly blend a translucent stroke.

`fill` is a paint. `stroke` contains `paint` and these optional fields:

| Field | Default | Behavior |
| --- | --- | --- |
| `widthDp` | 1 | Centered geometric stroke, transformed with path |
| `minimumPixels` | 0 | Physical width floor; use 1 for essential rails |
| `cap` | `butt` | `butt`, `square`, `round` |
| `join` | `miter` | `miter`, `bevel`, `round` |
| `miterLimit` | 4 | Longer miters fall back to bevel |

Round joins/caps adapt to output density. Explicit zero-length strokes support
round/square caps; move-only contours do not produce dots. Butt caps stop at their
endpoints. Stroke inside/outside alignment, dashes and variable width remain open.

Paint supports `{"type":"none"}`, solid color and linear gradients. Solid paint
is `{"type":"solid","color":<typed color or color token>}`. A gradient is:

```json
{
  "type":"linear",
  "from":[0,0],
  "to":[{"fraction":1},0],
  "stops":[
    {"at":0,"color":{"type":"token","value":"marine.olive"}},
    {"at":1,"color":{"type":"color","value":[0.545,0.588,0.294,0]}}
  ]
}
```

Gradients contain 2–256 strictly increasing stops, including 0 and 1, and clamp
outside their endpoint interval. Equal-position hard stops, radial/conic/image
paints and paint transforms remain open. Coincident resolved endpoints produce
a path diagnostic rather than undefined color.

Linear gradients split triangles at every stop plane. Middle stops survive even
when no original vertex lies there. Colors interpolate as premultiplied sRGB;
transparent endpoints cannot tint visible interpolation. Float colors survive
paint subdivision and engine clipping. RmlUi's geometry boundary currently
quantizes to 8-bit channels; higher precision/dithering remains quality work.

Canonical opacity now uses [isolated group composition](composition.md).
Each node's paths and descendants paint into a transparent target before its
opacity applies once. Nested groups compose inside-out. Primitive paint alpha
remains independent, so fades reuse both path coverage and vertex colors.

## Compilation and rendering

`Vector.h/.cpp` have no engine, RmlUi, platform or JSON dependency. Compilation
takes layout extents and one affine map from local dp to output pixels. Quadratic
curves convert to cubics; recursive de Casteljau subdivision checks transformed
control-hull distance to the output segment, with a default 0.15-pixel tolerance.
Using the segment rather than its infinite line catches collinear overshoots.

Strokes are built locally before the affine map, preserving geometric behavior
under nonuniform scale and shear. The physical minimum uses the smallest singular
scale to protect every orientation; round subdivision uses the largest singular
scale to bound error. Mirroring retains winding semantics.

libtess2 resolves fills/stroke unions. The current antialiased path compiles its
normalized boundaries into coverage quads; geometric hit testing uses triangles.
The paint compiler splits geometry at gradient stops. Native elements submit
through RmlUi's render manager with the already
applied transform disabled for that submission. Existing clipping, render order
and resource lifetime remain in control. The subsequent
[performance checkpoint](runtime-performance.md) separates opacity updates from
path compilation and reuses whole-pixel translations. It also patches the pinned
tessellator to double precision for stable thin rails during fractional motion.

Video/font generation changes recreate vector elements with their document. The
fixture contains no baked SVG image, copied retail texture or material script.
Dedicated binaries do not link the vector library or tessellator.

`HitTestPath` uses the same compiled geometry and actual transform, including
holes/strokes. It establishes geometry agreement; menu focus/input, world rays
and semantic targets are not connected yet. The public inverse affine map is
also tested.

## Limits and failures

Compiler errors retain the caller's previous complete mesh and report a path
diagnostic. Runtime reports failed paths without emitting partial geometry for
them; other paths can still render during authoring. Invalid source IDs, commands,
paint types and values receive document source locations.

Default input/output vertex budget is 262,144; caller limits are 3–1,048,576.
Subdivision beyond depth 24 fails rather than silently coarsening a curve.
Output coordinates are limited to ±1e6 pixels; tessellator allocations to 64 MiB
per invocation. Numeric inputs, counts and indices are checked. Degenerate affine
scale yields empty geometry. Projected world GUIs still need their separate
density/projective subdivision integration.

The allocator retains 16-byte payload alignment. Windows tests caught that MSVC's
`max_align_t` alone is only 8-byte aligned although the tessellator's `jmp_buf`
requires 16. Standard C++ aligned allocation and its accounting header now
preserve the required alignment on every target; this
was fixed before gameplay captures.

## Dependency

[libtess2](https://github.com/memononen/libtess2/tree/8dbd6483e920311a58c9af10a10beb278efebc36)
is pinned at `8dbd6483e920311a58c9af10a10beb278efebc36`, archive SHA-256
`083f507dd3b27eba01fa540b9efe3bbeed36d5bd4ea0e8869897d21f52c5f98b`.
Its seven C sources build as a static Meson subproject. No example/GL backend
implementation is incorporated.

The upstream [SGI-B-2.0 notice](../../licenses/libtess2.txt) is retained, credited
in README and installed into `licenses/`. This version is on the FSF's
[GPL-compatible license list](https://www.gnu.org/licenses/license-list.html#SGIFreeB).
Curve/stroke construction, responsive coordinates, paint splitting, source
validation and retained element integration are openQ4 implementation.

## Verification and remaining gates

`openq4-ui-vector-test` checks analytic polygon/stroke areas, both fill rules,
same/opposite-winding holes, crossing contours, affine/mirrored maps, inverse/hit
agreement, cubic-circle and quadratic integrals, density-dependent subdivision,
caps/joins, physical minimum strokes, middle gradient stops, transparency,
budgets, error transactions, collapsed scale and editable responsive cuts.
Stroke area checks catch independently overlapping segment geometry.

`openq4-retained-ui-test` additionally renders a vector element through real RmlUi
to the host, checking inherited rectangular clipping at two densities and paint
alpha multiplied by parent and child opacity. Existing document/motion regression
checks remain in place. All three native targets pass on Windows.

The [fixture](../../../tools/ui/fixtures/vector-smoke.q4ui) demonstrates 12/8 dp
panel cuts, two rails, header wash, lower-left button cuts, fading action rails,
triangle markers, a cubic ring with a hole and a rounded stroke. It uses existing
localized strings and the normative family colors. It is a geometry fixture,
**not** a finished component kit, functional controls or accepted artwork corpus.

The original geometry checkpoint below preceded coverage antialiasing. Adaptive
geometry alone did not establish smooth pixel coverage at fractional density,
thin diagonals or small curves. Those hard edges prompted the subsequent
[analytic coverage implementation](coverage-antialiasing.md).
Masks, broader composition effects, full paint/stroke operations, font quality,
performance, editor interactions and all 271 GUI migrations also remain open.
The goal and its complete visual acceptance requirements are unchanged.

### Original geometry checkpoint: windowed gameplay evidence

The final Windows client built and staged successfully, and the retained UI,
document and vector native test targets passed (3/3). Client SHA-256:
`0679361c8525b5058d64ae68c7342c8a5be92ccbe4b38b50ceb5dec4f45efb57`.
The dedicated target also built; its link inputs contain no retained UI,
RmlUi, JsonCpp or libtess2 libraries. This is Windows evidence; other platforms
remain unqualified.

Both final captures used the same client and `vector-smoke.q4ui` source SHA-256
`78dc7bc86a65d6983ac23646f5534f205ec670bb85ade94cffecb4c8da921bbe`.
The harness uses a hidden, windowed game, disabled mouse/controller input, an
active map and the registered engine `screenshot` command. Images were reviewed
from the resulting render-target TGA files. No host input was injected.

| Final capture under `.tmp/ui/vectors/` | Gameplay and density | Result |
| --- | --- | --- |
| `sp-gl-125-restart-final` | SP `airdefense1`, OpenGL, 125%, full video restart and replay of the entry timeline | Exit 0; no warnings/errors or retained diagnostics. Frames, labels and curves retained after restart. |
| `mp-vulkan-200-final` | MP `q4dm1`, Vulkan, 200%, reduced motion, explicit auto-join | Exit 0; no errors or retained diagnostics. Frames, holes, gradients and rounded check stroke render at doubled density. |

SP screenshot SHA-256:
`643ef105f97de8d0b4d32b3fdf90db4fdab53dc206f5a8713f7c987a34bf5424`.
MP screenshot SHA-256:
`de336be0caa87ea9c5ea64dbf3492dd3606819777cefaad7c6f0339f0148407d`.
The MP run retains 93 existing gameplay/content warnings, including absent AAS,
optional sounds and non-precached declarations. They match the earlier stock
baseline count; they are not treated as a clean-log pass. Earlier SP captures at
100% OpenGL and 200% Vulkan are supplementary: those precede the final portable
aligned-allocation change. The latter retains the two previously observed Vulkan
vertex-memory/postprocess warnings.

Reproduce the final capture scenarios after building/staging with the normal
MSVC-aware Meson wrapper (choose new output directories):

```powershell
$assets = 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4'
python tools/ui/capture_legacy_baseline.py --assets $assets --mode sp --renderer gl --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 1.25 --timeline enter --video-restart --output .tmp/ui/vectors/sp-gl-125-review
python tools/ui/capture_legacy_baseline.py --assets $assets --mode mp --renderer vulkan --retained-document tools/ui/fixtures/vector-smoke.q4ui --density 2 --timeline enter --reduced-motion --output .tmp/ui/vectors/mp-vulkan-200-review
```

These are reviewed geometry checkpoints. Their capture reports retain
`replacement_acceptance: false`; hard raster edges are visible and still fail
the production quality gate.
