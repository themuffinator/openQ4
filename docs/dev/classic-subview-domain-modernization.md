# Shared Special-Subview Transaction

## Status

The special-subview Milestone D corridor is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification of every
subview kind and a nested chain on Windows OpenGL/Vulkan recorded 13 November
2026; non-Windows platforms and human appearance review belong to platform
qualification)**; see [REN-01 qualification](#ren-01-qualification-13-november-2026). `r_rendererSharedSubview 1` lets an
eligible direct `SS_SUBVIEW` mirror publish its complete camera, clip-plane,
viewport, and scissor record and lets eligible remote-camera, mirror,
reflection, refraction, and x-ray
surfaces publish their exact child-scene-to-image capture transaction. The
capture edge preserves 2D or cubemap destination type, the exact cubemap face,
and color or depth aspect. Eligible nested special views seal their complete
parent/child tree and publish shared ownership only when its outermost special
view has completed. Eligible authored-post tails inside those views retain their
separate dynamic-stage records but join the same atomic root completion and
rollback decision. OpenGL and Vulkan consume the same sealed records.

This seals a subview handoff, not its material/light implementation. Each child
scene still uses its established classic 3D walker, so it is not a claim that
child ambient, interaction, or fog work is shared-owned. The direct record
selects the complete established child-view executor; a capture record selects
only the exact transfer after that executor returns. When
`r_rendererSharedCinematicPost 1` is also enabled, an eligible child
authored-post tail may be sealed by its dedicated domain without transferring
the child's other material or lighting work.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Transaction boundary

The front end emits a child `RC_DRAW_VIEW`, restores the parent view, and then
emits `RC_COPY_RENDER` for a capture-backed surface. A direct `SS_SUBVIEW`
mirror has no copy command: its child output is already composited into the
parent target. Both forms must therefore be recovered from immutable records,
not mutable global view state:

1. the child scene packet retains its exact `viewDef`, parent source surface,
   camera origin/axis, final culling parity, clip plane, viewport, scissor,
   time, and packet index;
2. a capture record retains the child identity, destination image, source
   rectangle, destination type/format, cube face, and color/depth mode when a
   transfer exists;
3. admission reconciles the direct source with no capture, or a capture source
   with exactly one full-viewport 2D/cubemap color or depth capture;
4. the backend reports direct ownership only after the complete classic child
   view returns, or capture ownership only after the sealed transfer completes;
5. a nested child identifies its exact `superView` domain parent, the
   depth-first child-before-parent scene-packet order, a bounded root/depth,
   and every descendant in that root transaction; and
6. every admitted nested cinematic/post range identifies the same root and
   records exact dynamic-stage coverage before any subview or post member
   publishes shared ownership.

OpenGL performs a direct child through the established `RB_DrawView` executor
or performs a capture transfer with `CopyFramebuffer`/`CopyDepthbuffer` sourced
from the sealed record. Vulkan similarly retains its established 3D executor
for a direct child and its image-copy implementation for a capture, creating
an exact-format six-layer depth target for a depth cubemap when required. A
changed camera, clip plane, viewport, scissor, image, destination type/aspect,
cube face, or copy rectangle never borrows the shared record: the normal
command executes on the classic path and the subview records a named fallback
instead.

Nested work remains executed by the mature classic child-view executor and
copy implementation. The shared corridor does not substitute a partial child
material or lighting pass. Its additional guarantee is ownership atomicity:
if a nested source, camera, resource, transfer, or completion edge is rejected,
the whole sealed special-view tree remains on the established command path.
The same rule covers admitted authored-post tails. Their backend work may finish
first, but ownership stays unpublished until the outermost special view verifies
all descendants and dynamic ranges. A cinematic/post rejection returns the
whole special tree to classic ownership; a special-tree rejection marks every
nested cinematic/post range for that root fallback.

## Eligibility and fallback

The current corridor accepts an eligible 3D child view that has all of the
following:

- a parent scene and source draw surface still present in that scene;
- either an `SS_SUBVIEW` direct mirror with no capture and exactly one sealed
  clip plane, or exactly one `RC_COPY_RENDER` record matching the full child
  viewport and a 2D/cubemap color/depth target with its legal exact face;
- for a capture, one matching parent dynamic stage: `DI_REMOTE_RENDER`,
  `DI_MIRROR_RENDER`, `DI_REFLECTION_RENDER`, `DI_REFRACTION_RENDER`, or
  `DI_XRAY_RENDER`; and
- the exact kind-specific camera/clip semantics: one portal plane for direct
  mirrors, dynamic mirrors, and reflections; x-ray state with no portal plane;
  and ordinary no-clip state for remote-camera/refraction, plus a loaded
  destination image for a capture backend; and
- for nesting, an exact parent subview record for every `superView` link, a
  strict descendant-before-parent packet order, and a fully admissible
  parent/child subtree. Child completion is deferred until the root view has
  completed every direct or capture-backed member; and
- when `r_rendererSharedCinematicPost 1` admits a child authored-post tail, a
  ready cinematic/post record bound to the same special root, followed by exact
  backend completion for every such dynamic range before root publication.

Editor/render-demo/global-material views and every missing or
duplicate/mismatched parent, capture, material, semantic, resource, command
order, or capacity condition retain the established complete classic fallback.
A malformed or rejected nested member returns its complete root transaction to
that path; no admitted descendant remains reported shared-owned on its own. A
capture target with an unsupported texture type, illegal cube face, or
color/depth-aspect mismatch receives the named `unsupportedCaptureTarget`
fallback. Named nesting fallbacks distinguish a missing parent,
child-before-parent order failure, rejected parent/child tree, and incomplete
backend completion. `nestedCinematicPostFallback` identifies a dynamic-stage
rejection propagated to the complete special tree, while
`backendNestedCinematicPostIncomplete` identifies root finalization attempted
before all admitted dynamic ranges completed with exact coverage. The setting
cannot split a capture, a direct child, a nested post range, or a nested tree:
the root transaction consumes every sealed member or owns none of them.

## Control and diagnostics

`r_rendererSharedSubview` remains independently selectable from the GUI,
world-ambient, interaction, fog/blend, cinematic/post, and deform controls.
When both subview and cinematic/post ownership admit a nested tail, the two
domains deliberately share one completion and rollback boundary:

- `0` (default): every direct and capture-backed subview uses the established
  backend path;
- `1`: an eligible direct mirror or remote-camera/mirror/reflection/refraction/
  x-ray 2D/cubemap color or depth capture—including a fully admissible nested
  tree and its separately admitted authored-post tails—may consume the sealed
  transaction; all other subviews use the classic fallback.

The stock-baseline and ordinary gameplay-benchmark harnesses explicitly set the
control to `0`. `gfxInfo` reports packet/capture counts, ready and fallback
views, direct mirror/remote/mirror/reflection/refraction/x-ray totals,
color-cubemap/depth-2D/depth-cubemap target counts, semantic hash, individual
capture rectangles/types/faces/aspects, parent/root/depth/subtree records, and
OpenGL/Vulkan ownership/fallback counters including nested-tree completion and
rollback totals. The cinematic/post `gfxInfo` line complements this with
`nested=<views>/<transactions>` and `nestedCinematic=<stages>` counts; ownership
must not appear there before the matching outer special-view transaction
finishes.

Focused dependency-light validation:

```text
rendererScenePacketSelfTest
rendererClassicSubviewDomainSelfTest
gfxInfo
```

`tools/tests/renderer_classic_subview_domain.py` guards the front-end and
legacy command capture association, bounded parent/source/capture admission,
direct-mirror and special capture scope, sealed camera/clip/scissor semantics,
sealed GL/Vulkan copy arguments, exact post-view/post-copy ownership reporting,
subview-before-cinematic preparation, deferred nested dynamic completion,
bidirectional whole-root fallback, conservative defaults, diagnostics, and CI
registration.

## Controlled nested acceptance evidence

The final 2026-08-23 R6 acceptance passed all six cases on both OpenGL and
Vulkan for one deliberately narrow topology: a stock-material, capture-backed
color-2D mirror whose child view alone contains one frozen cinematic stage and
one `_currentRender` authored-post stage. Both-on normal owned exactly one
mirror and its one nested cinematic/post transaction on the active backend,
with zero mismatch, duplicate, or untracked counters. Both-on
`r_skipPostProcess 1` reported zero ownership, one fallback in each domain, and
the named `nestedCinematicPostFallback` outcome. Subview-only owned the mirror
while cinematic request/view counts stayed zero; all normal enabled cases and
the shared/classic skip pair had exact same-backend engine-TGA parity. All four
exact comparisons per backend reported RMS `0`, maximum delta `0`, and zero
differing channels; the inactive backend's counters stayed zero.

The authoritative acceptance report is
`.tmp/renderer-milestone-d/acceptance-20260823-r6-all/renderer_milestone_d_acceptance_report.json`
(SHA-256
`b49031c48267fbfa86c295082707525a390d6090c24f600d00644237a67b252a`),
bound to fixture manifest SHA-256
`24a2a4926f4b1263e311ad762bf40d391b33de225732d12154ebd7625705282d`
and immutable runtime manifest SHA-256
`dbece44597ec5e0686eed215f6ae99c5d6ba4cdd959c1efa7c4acaf1bc2ca673`.
The exact command, capture contract, deltas, inventory seals, and passing
foundation-report hashes are
recorded in the
[cinematic/post transaction evidence](classic-cinematic-post-domain-modernization.md#controlled-nested-acceptance-evidence)
and the [renderer validation matrix](renderer-validation-matrix.md).

## Release-qualification boundary

R6 proves one capture-backed color-2D mirror coupled to a nested dynamic tail;
it is not evidence for a direct mirror, a multi-level nested-subview chain,
remote-camera/reflection/refraction/x-ray forms, color cubemaps, depth captures,
illegal cube-face/aspect rollback, `_currentDepth`, in-world GUI, or
render-demo/Raven special-frame ownership.

Before promotion, retain engine-render-target screenshots—not OS captures—for
fixed direct mirror, dynamic mirror/reflection, x-ray, remote-camera, and
refraction views with the setting off and on, separately on OpenGL and Vulkan.
Require exact same-settings output, nonzero reconciled ownership, named
zero-commit fallback on a deliberately malformed semantic and capture edge,
clean backend diagnostics, retained final package evidence, and target-platform/
driver coverage. Retain a multi-level nested direct/capture chain with nonzero root-tree
ownership plus a malformed descendant that reports a whole-tree rollback on
both backends. R6 already covers one color-2D capture; extend retained target
coverage to color cubemaps and depth-2D/depth-cubemap ownership plus an
illegal-face/aspect zero-commit fallback on both backends. Repeat the R6 nested
authored-post and forced `nestedCinematicPostFallback` gate from clean committed
source and a final package, and add an otherwise equivalent `_currentDepth`
case. In-world GUI retains its own provenance-tagged transaction documented
in [Shared Classic In-World GUI
Domain](classic-inworld-gui-domain-modernization.md).

Together with the other independently guarded shared corridors, this completes
Milestone D's scoped implementation. It does not make every child material,
lighting category, or the aggregate modern-visible renderer shared-owned.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
now covers the forms R6 left open, on OpenGL and Vulkan under the default
preset and with the post chain off. Stock materials author only direct mirrors
and remote cameras, so the harness compiles validation rooms (stock textures,
models and the stock `camera1.gui` screen, plus test materials) into the run's
isolated save path:

- **Room A:** a reflection, a dynamic (capture-backed) mirror, a remote camera
  aimed by a `func_cameraview`, and a direct mirror side by side, all owned
  (four root transactions).
- **Rooms C and D:** an x-ray capture and a refraction capture, each alone in
  view because both draw from the viewer's own camera.
- **Room B:** a direct mirror facing a capture-backed mirror, a nested
  direct/capture chain owned as one two-view tree.
- **Room E:** a direct mirror that sees an x-ray panel. The x-ray child copies
  its parent mirror view, clip plane included, as classic `R_XrayViewBySurface`
  (like `R_RemoteRender` and `R_RefractionRender`) always has; the domain
  refuses that inherited semantics and the whole tree declines, nothing owned
  and the frame classic.

A reflection or refraction render-map stage only refreshes its image and draws
nothing itself, so each test material adds a stage that samples
`_reflectionRender` or `_refractionRender` in screen space. The refraction one
draws at half brightness so that it stands apart from the room behind it.

Every owned frame is byte-identical to the classic one. The control hides the
subview surfaces (each is a `func_static`, so `r_skipEntities 1`): skipping
subviews alone shows nothing on a frozen scene, which re-captures identical
images, and Vulkan keeps a skipped direct mirror's previous pixels. The rollback is the
qualification diagnostic `r_rendererSharedFaultInject 1`, which declines every
subview at the backend boundary with a named `backendRejected` fallback.

Cubemap and depth subview targets have no producer: material dynamic images
are always 2D colour captures. They remain covered only by
`rendererClassicSubviewDomainSelfTest`. The stock `game/tram1` remote screen
was owned during exploration, but that scene keeps moving under `g_stopTime`,
so it cannot give an exact comparison. Human appearance review and non-Windows
platforms belong to platform qualification.
