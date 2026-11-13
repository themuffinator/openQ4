# Shared Classic Material-Deform Contract

## Status

The Milestone D material-deform dependency is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification on Windows
OpenGL/Vulkan recorded 13 November 2026; non-Windows platforms and human
appearance review belong to platform qualification)**; see
[REN-01 qualification](#ren-01-qualification-13-november-2026).
`r_rendererSharedDeform 1` allows an existing shared classic domain to consume
the final geometry produced by the established CPU material-deform path when
that result has a complete sealed provenance record. The classic front-end
deform algorithms remain authoritative, and every unsupported, skipped, stale,
or incomplete record keeps the owning GUI view, world-ambient view,
interaction view, or fog/blend phase on its complete established path.

This is a cross-domain geometry dependency, not a fifth framebuffer pass and
not a GPU rewrite of `R_DeformDrawSurf`. It does not independently enable root
GUI, world ambient/material, interaction, or fog/blend ownership; the applicable
`r_rendererShared*` domain setting must also be enabled. All settings remain
off by default.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Why a separate contract is required

Two unrelated concepts previously shared the word "deformed":

- a material declaration can request `sprite`, `rectsprite`, `tube`, `flare`,
  `expand`, `move`, `turbulent`, `eyeball`, `particle`, or `particle2`; and
- `srfTriangles_t::deformedSurface` describes generated/dynamic model geometry,
  including ordinary CPU-skinned character surfaces.

The second flag does not prove that a material deform ran. Treating it as that
proof either rejects valid generated models or admits an unexecuted material
deform. `ClassicDeformDomain` instead brackets the established
`R_FinalizeDrawSurf` call and records the exact source and published geometry.
Scene-packet geometry records own one immutable copy per draw role and draw
packets retain a stable reference to that copy, so geometry records which share
a triangle pointer cannot alias different deform provenance without inflating
the fixed draw-packet arena.

## Sealed record

Each record contains:

- the consumer role: finalized draw, interaction receiver, fog receiver,
  shadow volume, or another explicitly classified use;
- the material-deform kind and one of `none`, `notApplicable`, `skipped`,
  `completed`, `empty`, `failed`, or `unsupported`;
- source and result material identity and stable name hashes;
- source and result geometry identity, vertex/index counts, cache pointers,
  buffer identity, byte ranges, tags, client-data availability, primitive-batch
  state, and upload lifetime;
- the evaluated deform-register indices and finite values, deform-declaration
  identity, view/model input hash, and flare scale where applicable;
- a nonzero front-end frame token and explicit CPU-finalization proof; and
- a stable semantic hash which excludes pointers, backend buffer names, and the
  frame token while retaining all behaviorally relevant values.

Validation is fail-closed. A drawable completed result must be a distinct,
frame-temporary geometry object with positive triangle work and enough sealed
vertex/index backing. An intentional empty result must be a distinct valid
result with exactly zero indexes and no drawable cache claim. Cache, pointer,
material, count, role, outcome, frame, or hash drift invalidates the record.

## Classic role semantics

The original renderer does not run material deforms for every consumer of a
surface, so the contract preserves that behavior rather than inventing new
lighting or fog geometry:

- root GUI and world ambient/material draw surfaces are finalized before scene
  packet construction and require `completed` or intentional `empty` output;
- mapped shadow casters also use their finalized result; an empty result becomes
  a sealed no-op caster and contributes no backend draw;
- classic interaction and fog receiver chains intentionally retain their source
  geometry and therefore require the explicit `notApplicable` receiver role;
- stencil shadow volumes retain their established geometry ownership and are
  not reclassified as material-deform output.

This distinction preserves the classic material-deform limitation noted in the
original code: the ambient result does not create a new interaction surface.
It also lets CPU-skinned model geometry remain governed by its own skinning and
dynamic-geometry checks instead of the material-deform switch.

## Supported kinds and rollback

The shared contract admits successful CPU results for the material kinds whose
classic algorithms are implemented: `sprite`, `rectsprite`, `tube`, `flare`,
`expand`, `move`, `turbulent`, and `eyeball`. Their algorithms, register
evaluation, topology generation, tangent generation, and frame-cache allocation
are unchanged.

`particle` and `particle2` remain explicit `unsupported` outcomes because the
current `R_ParticleDeform` implementation emits no replacement geometry.
`r_skipDeforms 1`, malformed topology, a failed frame-cache allocation,
non-finite or out-of-range inputs, and any stale or mismatched record are named
failures. The owning shared view or phase rejects before its first shared
framebuffer write and executes the complete classic path exactly once. The skip
setting also blocks interaction/fog receiver ownership even though those roles
are normally sealed as `notApplicable`; this keeps the explicit diagnostic
comparison path completely outside every shared deform-dependent phase.

## Domain and backend flow

1. The front end evaluates material registers and brackets the authoritative CPU
   deform call with a source/result snapshot.
2. Scene packets seal the record with the exact pass role and frame token.
   Geometry lookup includes deform provenance, preventing pointer-only aliasing.
3. `ClassicGuiDomain`, `ClassicWorldAmbientDomain`,
   `ClassicInteractionDomain`, and `ClassicFogBlendDomain` validate every
   material-deform record before publishing their bounded transaction. Deform
   outcome and semantic hash participate in each owning record and view hash.
4. OpenGL and Vulkan consume the ordinary final `idDrawVert`/index stream. They
   do not implement, rerun, or reinterpret a material deform.
5. Existing complete-domain backend preflight and reconciliation prove that a
   completed result was drawn once or an empty result was counted once without a
   draw. Any failure leaves zero mixed shared/classic ownership.

Vulkan GUI/world preflight separately snapshots its transient geometry rings,
direct-mapped upload memos, GPU-skin memo, and bound offset. A mid-list failure
restores that state before classic fallback; world sampler mutations are also
restored. Successful preflight commits only after the complete geometry batch
fits.

## Control and diagnostics

`r_rendererSharedDeform` is an archived Boolean control:

- `0` (default): a material deform remains a named blocker for shared classic
  domains, preserving the earlier behavior;
- `1`: completed/empty finalized results and explicit not-applicable receiver
  roles may participate; all other outcomes retain complete-domain rollback.

Default-safety, stock-baseline, ordinary gameplay-benchmark, and aggregate
modern-visible profiles force the option off unless a deform qualification run
enables it deliberately.

Use these commands for focused validation:

```text
rendererClassicDeformDomainSelfTest
rendererClassicGuiDomainSelfTest
rendererClassicWorldAmbientDomainSelfTest
rendererClassicInteractionDomainSelfTest
rendererClassicFogBlendDomainSelfTest
gfxInfo
```

`gfxInfo` reports the request state, scene-packet deform roles/outcomes,
semantic hashes, per-domain deform coverage, named failures, and OpenGL/Vulkan
owned/fallback reconciliation. The top-level outcome counters describe emitted
draw packets only; a finalized `empty` result has no draw packet by definition,
so GUI/world/mapped-caster empty results are reported by their owning domain's
empty/no-op counters without being double-counted across passes.
`tools/tests/renderer_classic_deform_domain.py` guards the outcome and freshness
contract, source/final cache proof, pointer/provenance isolation, all four
domain admission rules, backend preflight ordering, default-off isolation,
self-test registration, and validation-workflow coverage.

## Local qualification

The registered `deform` gameplay profile uses the stock `maps/tools/mv2` scene,
a fixed bordered/windowed 1280x720 camera, and the shipped
`shaderDemos/move` material through `r_materialOverride`. It leaves normal
material deformation enabled and combines `r_rendererSharedWorldAmbient 1`
with `r_rendererSharedDeform 1`. References must be captured separately for
OpenGL and Vulkan with the shared settings disabled, using only the engine's
registered `screenshot` command. Because the shipped material evaluates a
time-based move expression, the profile starts frozen, advances a fixed number
of one-tic frames, and freezes again before sampling. It also disables mouse
input before startup so physical input cannot move the retained camera.

The implementation gate requires:

- exact same-backend classic/shared TGA parity with nonzero completed deform
  ownership and zero coverage mismatches;
- a material effect delta against otherwise identical `r_skipDeforms 1`
  output;
- exact classic parity and zero committed shared draws for an intentional
  skipped, unsupported, failed, or stale-record blocker;
- clean OpenGL diagnostics and Vulkan validation output; and
- clean toggle, map-restart, frame-reset, native, static, and build checks.

The 2026-08-22 development-worktree run used the staged Windows x64 runtime.
Both backends passed the complete controlled gate:

- Normal classic/shared pairs match exactly at RMS `0` and maximum channel
  delta `0`. OpenGL's pair has screenshot SHA-256
  `f7cb398ac9a2b51003f6b33db25fde3b8167808cc3996a57fd6224e64d737500`;
  Vulkan's pair has
  `6dccd89c48eceddc830f9a6e70aa08e73024258f448927374bb6cd5cc87a9295`.
- Each shared run emits two valid `completed` records, no failed, unsupported,
  or invalid record, semantic hash `8ea371dfc9b550ff`, and one reconciled owned
  world-ambient draw. Both backends report domain hash `d498cca50db350d0`,
  `ready=1`, `fallback=0`, `surfaces=1/1`, `deform=1/1/0`, `passes=1`, and
  `draw=1`; only the active backend's owned counter is nonzero.
- With `r_skipDeforms 1`, each shared image again matches its otherwise
  identical classic image at RMS `0` and maximum delta `0`. OpenGL's skipped
  pair has SHA-256
  `4cecc8b621ae618ddbcbacb96efea15bd79b84e8613b3c41346dd289bce8e291`;
  Vulkan's has
  `e3b092f7b787d40af5e915febee14f3bd631a191b3be6eb9b47d883666c9eb0b`.
- The skipped runs emit two valid `skipped` records with semantic hash
  `4f7f7e6329163123`. World ownership fails closed with
  `failure=deformContract detail=9`, `ready=0`, `fallback=1`, and zero shared
  surfaces, passes, or draws; only the active backend records the fallback.
- The skipped image differs materially from the normal deform on both
  backends, proving the controlled effect is visible: OpenGL reports RMS
  `57.54`, maximum delta `194`, and `2,727,697` changed RGB channels; Vulkan
  reports RMS `55.03`, maximum delta `190`, and `2,728,103` changed channels.
- The gameplay reports contain no OpenGL error, framebuffer failure, Vulkan
  validation warning, VUID, Vulkan call failure, fatal error, or engine error
  line. The native foundation suite and Vulkan clear/startup safety case pass at
  `.tmp/renderer-validation/deform-foundation-final3` and
  `.tmp/renderer-validation/deform-vulkan-final2`.

The retained gameplay reports are under
`.tmp/renderer-gameplay/deform-{gl,vk}-final3-{classic,shared,skip-classic,skip-shared}`.
They bind the staged runtime and dirty source revision used for this local
qualification. Clean committed-source recapture, a freshly staged final
package, human image review, and target-platform/driver coverage remain
separate release-promotion gates.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
draws the stock `maps/tools/mv2` floor with the stock `shaderDemos/move`
deforming material on OpenGL and Vulkan, under the default preset and with the
post chain off, frozen on one game tic. With deform ownership on, the world
ambient domain owns the view and consumes every completed deform record; the
frame is byte-identical to the classic one and `r_skipDeforms 1` is the
visible control. Turning deform ownership off while ambient ownership stays on
is the natural zero-commit rollback: the ambient view falls back on its
unsealed deform and draws byte-identical to the classic frame.

## Milestone D completion and release-promotion boundary

Root GUI, world ambient/material, fixed-classic interaction, and fog/blend can
now consume sealed classic material-deform results through one independent
default-off dependency. This does not make the complete frame modern-owned.

The completed cinematic/authored-post corridor seals eligible root video/audio
views and complete post tails while retaining the existing dynamic stage
executors. Eligible authored-post tails inside a sealed special-view tree now
complete and publish with that tree's root transaction; see [Shared Classic
Cinematic and Authored-Post
Transaction](classic-cinematic-post-domain-modernization.md) and [Shared
Special-Subview Transaction](classic-subview-domain-modernization.md).
Render-demo and Raven special-frame ownership have a dedicated
[transaction](classic-special-frame-domain-modernization.md).

These independently guarded corridors complete Milestone D's scoped
implementation. They do not make every classic-frame category or the aggregate
modern-visible renderer shared-owned. Clean-package, human-review, and
target-platform/driver evidence remain release-promotion gates; temporal
presentation is the next roadmap milestone.
