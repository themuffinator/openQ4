# Shared Classic Interaction-Lighting Domain

## Status

The fixed-classic interaction transaction is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification on stock views
on Windows OpenGL/Vulkan recorded 13 November 2026; non-Windows platforms and
human appearance review belong to platform qualification)**; see
[REN-01 qualification](#ren-01-qualification-13-november-2026). It contains two
complete Milestone D ownership corridors behind the
`r_rendererSharedWorldInteraction 1` setting:

- the previously completed unshadowed interaction corridor; and
- shadow-coupled interaction ownership for classic stencil volumes, projected
  single-map and CSM/parallel shadows, point-light cube maps, mixed mapped and
  stencil lights, and same-light mapped-plus-stencil supplements.

Together with shared root 2D GUI and world ambient/material ownership, this is
the expanded third complete shared classic-frame domain. It is now joined by
the separate [shared fog/blend domain](classic-fog-blend-domain-modernization.md).
OpenGL and Vulkan
consume the same backend-neutral sealed light, receiver,
interaction-primitive, shadow-caster, and mapped-pass contract. The established interaction renderer remains the
supported default and the whole-view rollback.

This is a complete interaction-phase ownership boundary for an accepted view,
not a claim that the complete frame or every Quake 4 lighting feature is shared.
Fog/blend lights, custom lighting programs, unsupported receiver geometry,
translucent moment-map casters, special views, and any incomplete resource or
backend transaction leave every interaction in the view on the untouched
classic path. Ambient/material, fog/blend, GUI, subview, and post work retain
their existing owners.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Why the boundary is whole-view

An idTech 4 interaction is not one material draw. Each contributing light
multiplies its active light stages by an ordered bump/diffuse/specular surface
decomposition. One surface can occur under multiple lights and in local,
global, or translucent receiver chains. Stencil receivers additionally depend
on ordered global/local volume families, while mapped receivers depend on a
complete per-light map and may require same-light stencil supplements. A partial
handoff can therefore draw a receiver twice, omit a contribution, sample an
incomplete map, or apply a different stencil state.

The shared domain accepts or rejects the entire interaction contribution of one
ordinary root 3D view:

- every non-fog/non-blend light with material interactions;
- every local, global, and translucent receiver in classic order;
- every active light stage and decomposed surface interaction;
- every draw and no-op disposition;
- every stencil-volume, mapped-caster, alpha-stage, and supplement record;
- every mapped receiver pass and its sealed projected or point state; and
- exact light, surface, primitive, shadow, map, resource, and stable-hash
  accounting.

If any part cannot be represented, no shared record from that view is published.
If backend preflight cannot retain the complete plan, that backend records one
named whole-view fallback and performs zero shared main-target draws.

## Packet identity and sealed records

Interaction packets carry the source `viewLight_t`, light ordinal, receiver
class, receiver ordinal, and global interaction source ordinal. Shadow packets
carry the source light, light ordinal, caster class, chain ordinal, and source
ordinal. Preparation verifies these identities while walking the original
light, receiver, stencil, and shadow-map chains. This distinguishes the same
surface under two lights and proves that no receiver or caster was reordered or
silently dropped.

`ClassicInteractionDomain` prepares bounded frame-local arenas for:

- views, including packet/pass identity, shadow mode, expected ranges,
  readiness, fallback detail, semantic hash, and per-backend outcomes;
- lights, including source order, receiver spans, interaction spans, exact
  shadow-chain ranges, mapped-pass indices for LOCAL and GLOBAL receivers, and
  the physical stencil work implied by LOCAL -> GLOBAL -> TRANSLUCENT order;
- receiver surfaces and interaction primitives, including packet/material/
  geometry/instance identity, scissor/state, images, projections, matrices,
  evaluated colors, vertex-color mode, and explicit no-op disposition;
- stencil and supplement casters, including selected classic volume geometry,
  cap/preload choice, depth bounds, scissor, cull, transforms, and draw/no-op
  disposition;
- mapped casters and perforated alpha stages, including static/dynamic and
  GLOBAL/LOCAL chain identity, ambient geometry, texture transform, alpha
  comparison, hashed-alpha policy, and generation-checked image identity; and
- mapped passes, including receiver/resource-owner identity, map and supplement
  counts, resource alias, plan/generation diagnostics, cache/update/scratch
  policy, projected single-map or CSM state, point-cube state, filter/bias
  policy, completeness masks, and a backend-independent semantic hash.

The semantic map hash deliberately ignores backend allocation identity such as
the resource plan id and generation, while remaining sensitive to mapped and
supplement caster coverage, dynamic/alpha state, completeness, class, cascade,
filter, and bias semantics. Backend consumers do not reinterpret authored
material stages or reread shader registers after publication. Retained legacy
pointers are sealed geometry/resource bridges only.

## Exact classic interaction and shadow order

Preparation mirrors the fixed-classic interaction rules once for both
backends:

- light-stage order and LOCAL, GLOBAL, then TRANSLUCENT receiver order;
- condition-register handling with checked finite register access;
- bump-stage flushes, repeated diffuse/specular flushes, and the final flush;
- surface-color clamping and light-color multiplication using the view's
  classic light scale;
- texture-translation wrapping outside the classic `+/-40` range;
- black and flat-normal substitution, ambient-light specular suppression,
  vertex-color semantics, and explicit no-op records; and
- exact shadow mode per receiver: `NONE`, `STENCIL`, `PROJECTED`, `POINT`, or
  `HYBRID`.

Stencil work is planned as physical receiver-order submission rather than as a
unique-caster count. A mode transition clears and rebuilds the required family.
For example, with one caster in each full/supplement GLOBAL/LOCAL chain,
`STENCIL -> HYBRID -> STENCIL` plans five logical volume submissions and three
preloads; `HYBRID -> HYBRID -> HYBRID` reuses the prepared family and plans two
logical submissions and one preload. Both backends reconcile these exact totals
after drawing.

Mapped passes reconcile the exact `MAP_GLOBAL_STATIC`, `MAP_GLOBAL_DYNAMIC`,
`MAP_LOCAL_STATIC`, and `MAP_LOCAL_DYNAMIC` ranges. A `HYBRID` receiver also
requires complete `SUPPLEMENT_GLOBAL` and, where GLOBAL ownership requires it,
`SUPPLEMENT_LOCAL` ranges. LOCAL and GLOBAL receivers carry explicit
`shadowMapPassIndex` values; a compatible GLOBAL resource alias must name and
match its LOCAL owner exactly. Point maps always expose six faces. Projected
passes distinguish one-map projection from multi-cascade parallel/CSM state.

## Eligibility and whole-view fallback

The shared corridor admits ordinary root fixed-classic interaction views with:

- no shadows, classic stencil volumes, mapped projected/parallel/CSM lights,
  mapped point lights, mixed per-light modes, or complete hybrid supplements;
- static and dynamic opaque mapped casters and receivers, including
  authoritative CPU-skinned `idDrawVert` streams (every character's posed
  generated topology);
- perforated/alpha-tested mapped casters with sealed alpha stages;
- exact cache reuse, cache update, or bounded non-cacheable scratch resources;
  and
- fixed-classic receiver primitives that both backends can preflight in full.

Preparation or backend preflight rejects the complete view for any of these
conditions:

- translucent moment-map casters, incomplete mapped or supplement chains, a
  stale/signature-mismatched resource, an invalid alias, or a map admission/
  allocation/update failure;
- fog/blend lights inside the interaction pass;
- GPU-palette skinning, custom/new-style GLSL lighting, parallax,
  enhanced-material (`r_enhancedMaterials`, inferred PBR, or a PBR-authored
  receiver; `r_pbrMaterials`, on by default, leaves a view without one
  eligible), cel, flat-
  diffuse, simple/test, or other alternate interaction modes;
- unsupported or unsealed material-deform output, GPU-posed generated
  geometry, GPU-palette-skinned, packed, primitive-batch, missing-cache, depth-hack,
  negative-scale, or otherwise unsealed receiver geometry;
- subview, mirror, x-ray, editor, offscreen, render-demo, global-material, or
  other non-root view ownership;
- cinematic, defaulted, unloaded, unbound, or invalid image resources;
- out-of-range/non-finite registers or derived values;
- packet/light/receiver/material/geometry/instance identity mismatch;
- bounded arena, geometry, descriptor, uniform, cache, atlas, or scratch
  overflow; or
- any backend program, target, resource, or transaction-preflight failure.

Fallback is decided before the first shared main-framebuffer write. Shadow-map
preparation may write only retained backend-private cache/scratch resources; a
failure restores or abandons those reservations and leaves the classic visible
path untouched. After ownership commits, an unexpected coverage mismatch is a
diagnostic fault and the classic interaction stream is never layered over the
partially submitted shared result.

## Backend execution

OpenGL preflights the complete interaction view inside the ARB2 interaction
slot. It retains all interaction, volume, mapped-caster, and perforated-alpha
geometry; reserves cache or scratch resources for every non-aliased mapped
pass; renders projected/CSM and point-cube maps from sealed caster chains; then
revalidates texture handle, storage generation, render target, dimensions,
atlas region, signatures, and physical aliasing for every pass. Failure restores
the main target and GL state before the classic walker is selected. Commit binds
the retained projected or point resource, draws mapped receivers, and rebuilds
stencil/supplement families only when receiver order requires it.
Cache allocation during one sealed schedule never evicts a map prepared earlier
in that same transaction; excess projected or point work uses the bounded
scratch policy instead.

Vulkan snapshots its shared-geometry and uniform cursors before mapped
preflight. The shadow transaction reserves scheduler/cache/atlas/point-cube
resources, retains every mapped caster and alpha descriptor, builds projected
or point receiver blocks, and records the complete command plan without an
attachment write. Failure aborts shadow reservations and restores both cursors.
Commit occurs before any shared main-target interaction write, renders the
retained maps, binds the mapped receiver pipeline/descriptor set, and executes
the same receiver-order stencil/supplement plan. Projected single-map,
parallel/CSM, point cube, cache hit/update, scratch/non-cacheable, static,
dynamic, and perforated paths are included.

Both backends report ownership only when drawable/no-op primitives,
drawable/no-op shadow casters, physical logical/preload volume submissions,
mapped passes, and hybrid passes exactly match the shared view.

## Controls and diagnostics

- `r_rendererSharedWorldInteraction 0|1` controls both interaction corridors
  and defaults to `0`.
- `rendererClassicInteractionDomainSelfTest` exercises bounded publication,
  semantic hashing, mapped coverage, duplicate reporting, coverage mismatch,
  atomic rewind, and receiver-order stencil planning.
- `gfxInfo` aggregate and view lines report
  `shadow=L/C/D+N volumes=V+P maps=total+hybrid
  modes=projected+point csm=passes`, plus exact GL/Vulkan
  `draw+noop/shadow+shadowNoop+logical+preload/maps+hybrid` outcomes.
- Per-map diagnostics report view, light, LOCAL/GLOBAL receiver, shadow mode,
  light class, cascade count, alias, plan id, generation, mapped+supplement
  caster counts, `features=static+dynamic+alpha+translucent`, and semantic hash.

The setting participates in renderer default-safety reporting. Unrelated stock
baseline, gameplay benchmark, and startup profiles force it off.

## Validation contract

Automated native and static coverage proves:

- multi-light packet identity and all receiver/caster chain identities;
- exact primitive, map, supplement, no-op, and physical stencil reconciliation;
- projected single-map, parallel/CSM, point-cube, mixed, and hybrid sealed state;
- semantic-hash stability across plan/generation changes and sensitivity to
  caster, supplement, dynamic, alpha, completeness, filter, and bias changes;
- static/dynamic and opaque/perforated caster preparation;
- cache reuse/update, projected/point scratch resources, exact aliases, and
  resource revalidation;
- GL and Vulkan preflight/commit/abort ordering before visible ownership;
- atomic named fallback with zero committed backend counters and no shared
  main-target draw; and
- default-off isolation plus direct CI registration.

Runtime qualification uses only the engine's registered `screenshot` command
in a bordered/windowed 1280x720 run. The `interaction` profile creates a
controlled stock `maps/tools/mv2` scene with a fixed camera, stock crate, one
projected test light, and one point test light. Its five cases are:

- unshadowed ownership;
- stencil ownership;
- projected plus point mapped ownership;
- projected-map plus point-stencil mixed ownership; and
- a constrained map-update budget that must produce one named atomic backend
  fallback with zero committed ownership counters and no shared main-target
  draw.

The `interaction-shadow-stock` profile is the stock-map release-qualification
set for projected, point, CSM/parallel, dynamic mapped-caster,
perforated/cutout, same-light hybrid-supplement, and translucent-moment fallback
coverage. Its scene entries are candidates, not proof by label: each final run
must retain the sampled six-component `viewpos`, and every owned frame must have
nonzero, exactly reconciled backend counters plus one valid diagnostic record
per mapped pass. The projected target requires an actual single-map
`class=projected` record; the ordinary `game/airdefense2` point candidate must
report a six-face point record; dynamic and perforated targets require their
explicit feature bit; the hybrid target requires a nonzero supplement and
physical volume submission; and the translucent-moment target must fall back
atomically.

The controlled test lights do not form a real CSM case: enabling the CSM preset
there still produces only a single projected map. CSM acceptance therefore
belongs to a retained stock multi-cascade projected/parallel view; the current
`shadow-csm-airdefense1` entry remains fail-closed until its final camera reports
a nonzero multi-cascade record.

For each supported owned shadow setting, compare shared off/on under identical
settings and require the engine TGA to match the classic reference. Separately
compare the shadowed result with its shadows-off engine TGA and require a
material image difference; an identical shadows-on/off image is not acceptable
shadow evidence. Every intentional fallback must match the same-settings
classic TGA exactly. No operating-system screen capture is permitted.

## Retained local evidence and promotion boundary

The earlier two-light unshadowed `maps/tools/mv2` capture and its old
shadows-enabled rollback remain historical development evidence only. The
current controlled profile adds a second crate plus projected and point test
lights, so its five-case capture below supersedes those older counts and hashes.

The completed controlled shadow corridor also passed locally on 2026-08-21:
all five OpenGL cases and all five Vulkan cases passed from the staged
development worktree. Shared-off/on engine TGAs matched exactly (`RMS 0`,
maximum channel delta `0`) for unshadowed, stencil, mapped, mixed, and
map-budget fallback. Stencil, mapped, and mixed captures were materially
different from their shadows-off references: OpenGL recorded RMS deltas
`5.5301`, `5.4817`, and `5.5313`; Vulkan recorded `5.5613`, `5.5625`, and
`5.5625`. Enabled diagnostics reconciled nonzero casters, physical stencil
work, projected and six-face point maps, and mixed ownership; the constrained
budget cases recorded one named backend fallback with zero committed ownership
counters and no shared main-target draw.
Private map preflight/cache/scratch work may occur before that decision; the
evidence proves zero committed ownership counters and no shared main-target
draw.

The controlled and stock profiles above remain the authoritative release
runtime acceptance set. Every stock-profile case still requires fixed-camera
and reference qualification, including projected, point, CSM/parallel, dynamic,
perforated, hybrid, and translucent-moment fallback. Clean committed-source
recapture, a freshly staged final package, target-platform coverage, and driver coverage are still required
before the default-off option can be promoted. Retain reports,
logs, engine screenshots, image hashes/deltas, exact per-map records, final
poses, and Vulkan validation results with that release evidence.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
owns the interaction phase of a frozen stock `game/airdefense1` gameplay view
on OpenGL and Vulkan under the default preset (mapped projected shadows,
stencil point lights, HDR scene target), with stencil shadows only, with every
light shadow-mapped (point cubes and a distant point light's projected map),
with 4x MSAA and with the post chain off, all with the static shadow cache on
(the shipped default). On
`maps/tools/mv2` it owns a CPU-skinned stock character (`char_doctor`) and a
spawned crate as dynamic receivers and casters under a projected test light
and point-cube maps. Every frame is byte-identical to the classic one;
`r_skipInteractions 1` is the visible control, and the first-person weapon's
depth hack is the natural zero-commit rollback (a named fallback, nothing
owned, the classic frame). The stencil-only view is not compared on OpenGL
with 4x MSAA: there, classic frames flip isolated pixels between identical
frozen frames with every domain off.

These fixes made this possible:

- The adapters refused any render target, so under the default presets (the
  game's forward scene target on both backends, Vulkan's own scene target)
  nothing was ever owned. They now draw where the classic walker draws.
- Vulkan used to move a view onto the swapchain while any shared domain was
  requested. That changed the orientation and interpolation rounding of the
  classic work in the same frame (up to 18 levels along silhouettes), so a
  shared frame could never match the classic frame. The view now stays in its
  scene target.

- The domain read `deformedSurface` as GPU posing and refused every
  CPU-skinned MD5 surface, as a receiver or a mapped caster, which is every
  character. That flag marks generated
  topology whose ambient cache already holds the final pose; classic fixed the
  same misreading in issue #73. Posed generated topology is now admitted, and
  primitive-batch MD5R and GPU-palette skinning stay refused.
- A distant point light (its centre far outside its radius) draws a
  projected map, as classic classifies it, but its pass was labelled a point
  light, so both backends looked for a cube and declined. The pass class now
  names the map form drawn; the view's shadow mode and point/projected counts
  follow it, and OpenGL decides cubes from classic's own classification.
- A perforated caster whose alpha stages were all inactive (or had zero
  alpha) kept the start of an alpha range with no stages; the no-op and
  OpenGL checks refused it. It now seals no range.

Dense stock views also overflowed the per-frame geometry records (1024) that
every shared domain reads; a frame whose draws fit can no longer overflow them.

The all-mapped view first passed only with the static shadow cache off. Two
gaps with the cache on were closed on 14 November 2026:

- On Vulkan, while the domain switch was on, classic stopped caching any light
  with live casters and redrew its maps every view, because the sealed stream
  never composed. The redrawn maps hold the same depth, but the classic walk
  reads an exact hit with no live casters (the distant point light's LOCAL
  map) where it lives in the cache, and a tile in its own image is sampled
  through other arithmetic than the same depth in an atlas tile. So 20 to 26
  pixels of the classic frame moved by one to three levels, even when the
  domain declined the view. Classic now keeps its cache rules whatever the
  switch says, and the sealed stream, which copied every exact hit into the
  atlas, follows them. A sealed pass with live casters may consume the
  same composition (`allowCacheCompose`): it publishes its sealed static chains
  alone, the live chains are drawn over the restored tile or a copy of the
  resident cube, and the sealed receivers read an exact hit in place as the
  classic ones do. A pass with alpha casters still neither reuses nor
  composes, which declines the view on Vulkan when such a pass comes from the
  cache.
- On OpenGL, the sealed schedule found the distant point light's cache record
  that classic had published and then declined the view (a named
  `shadowMapTransaction` fallback). Classic stamps its records with its
  classification's light class, which is `POINT` for that light, while the
  sealed path stamped and compared the pass class, `PROJECTED`, the form of the
  map. Both now use classic's class.

The OpenGL failure detail of the interaction stream also overflowed `int` for
the reject reasons from `SHADOW_MAP_ALPHA` on. It now reads as decimal
`RR LLL IIII`: the reason, the light plus one, and the innermost item plus one
(the primitive or shadow caster when one is named, else the surface).

## Milestone D completion and release-promotion boundary

The shared root GUI, world ambient/material, fixed-classic interaction, and
fog/blend corridors are four independently guarded complete domains. The
interaction domain admits both unshadowed and shadow-coupled eligible views;
the fog/blend domain owns a complete eligible phase independently because its
compositing order and rollback boundary differ from interaction lighting.
Native/static and controlled GL/Vulkan fog/blend qualification pass, while
authored-stock and release promotion remain open. These domains do not make the
whole classic frame modern.

The independent default-off
[material-deform contract](classic-deform-domain-modernization.md) now seals
both sides of the classic interaction behavior: mapped casters require their
finalized completed or intentional-empty CPU result, while receivers explicitly
retain source geometry through the `notApplicable` role. Unsupported, skipped,
failed, stale, or mismatched records still reject the complete interaction view.

The completed cinematic/authored-post corridor seals eligible root video/audio
views and complete post tails while retaining the established dynamic-stage
executor. Eligible authored-post tails inside a sealed special-view tree now
complete and publish with that tree's root transaction; see [Shared Classic
Cinematic and Authored-Post
Transaction](classic-cinematic-post-domain-modernization.md) and [Shared
Special-Subview Transaction](classic-subview-domain-modernization.md).
Render-demo and Raven special-frame ownership have a dedicated
[transaction](classic-special-frame-domain-modernization.md).

These independently guarded corridors complete Milestone D's scoped
implementation. They do not make every classic-frame category or the aggregate
modern-visible renderer shared-owned. Stock-scene, clean-package, human-review,
and target-platform/driver evidence remain release-promotion gates; temporal
presentation is the next roadmap milestone.
