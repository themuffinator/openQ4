# Shared Classic 2D GUI Domain

## Status

The first Milestone D classic-frame domain is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification on Windows
OpenGL/Vulkan recorded 13 November 2026; non-Windows platforms and human
appearance review belong to platform qualification)** for complete
fixed-function 2D GUI views; see [REN-01 qualification](#ren-01-qualification-13-november-2026).
It is now joined by
the separate [shared in-world GUI domain](classic-inworld-gui-domain-modernization.md),
[shared world ambient/material domain](classic-world-ambient-domain-modernization.md)
and [shared interaction-lighting domain](classic-interaction-domain-modernization.md),
plus the [shared fog/blend domain](classic-fog-blend-domain-modernization.md).
OpenGL and Vulkan
consume the same ordered, per-draw evaluated material records. The established
classic view remains the supported default and the whole-view rollback.

This is deliberately a complete domain, not a single-texture GUI shortcut. A
view is shared-owned only when every source surface and every ambient material
stage can be represented, resolved, preflighted, and submitted in source order.
One unsupported surface, stage, image, register, state, cache, descriptor, or
pipeline keeps the complete view on the classic backend walker.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Domain boundary

The initial domain covers root generated 2D `RC_DRAW_VIEW` work on the ordinary
backbuffer. This includes eligible static menu, console, HUD, loading, and other
fullscreen GUI views whose material stages use the classic fixed-function
ambient contract.

The shared path preserves:

- original surface and repeated-stage order, including repeated image semantics;
- exact stage conditions and evaluated RGBA values;
- 2x3 texture matrices, including classic large-scroll translation wrapping;
- blend factors, color/depth write masks, depth comparison, culling, and legacy
  fixed-function alpha comparison state that the classic color walker executes;
- explicit texgen, vertex-color modes, material/private polygon offset, image
  sampler state, geometry, model-view/projection matrices, and scissors;
- inactive and recognized framebuffer no-op stages as ordered diagnostic
  records rather than silently deleting them.

The first domain intentionally rejects world/in-world GUI views, subviews,
mirrors, editor or render-demo mutations, offscreen render targets, weapon/model
depth hacks, suppress-in-subview materials, decal-color streams, decal-sorted
materials, current-render/depth images, cinematics,
dynamic images, custom programs, post-process sorting, combined material/private
polygon offsets, authored depth/coverage `alphaTest`,
non-explicit texgen, missing/default/unloaded
images, unsupported backend state, and every bounded-pool or resource failure.
Those cases execute the untouched classic view.

## Shared record flow

1. Front-end scene packets identify materials actually referenced by a GUI pass.
   Intrinsic material class does not decide eligibility, so a material first seen
   in the world can still be compiled correctly when a 2D view uses it.
2. `MaterialResourceTable` compiles each referenced material into a bounded
   ordered authored-pass span. Repeated semantic bindings remain distinct, and
   each stage receives an opaque generation-checked texture-resource id.
3. `ClassicGuiDomain` walks the original view surfaces transactionally, matches
   drawable surfaces to GUI draw packets in source order, evaluates the authored
   span once against that draw's bounded shader-register block, resolves every
   opaque image binding, and hashes the stable evaluated contract.
4. OpenGL and Vulkan preflight the complete prepared view before the first
   framebuffer-affecting command. Each backend then submits only `DRAW`
   dispositions, accounts for every inactive/no-op record, and records one
   owned or fallback outcome for the view.

Preparation retains no partial view. A failed transaction rolls back its draw
and evaluated-pass arenas and publishes only a precise fallback reason.

## Backend execution

The OpenGL adapter uses the classic fixed-function state surface, but it does
not read `shaderStage_t` or raw shader registers. Cache availability, texture
identity and sampler state, matrix/scissor state, alpha comparison, blend/depth/
cull/write state, vertex colors, polygon offset, and capacity are all checked
before `RB_BeginDrawingView` commits the view.

The Vulkan adapter similarly consumes only the sealed records. It validates an
exact pipeline-cache key, prepares descriptors and geometry offsets, and checks
the active swapchain render scope before recording the first indexed draw. It
never accepts the older pipeline-cache behavior that could substitute a
differently keyed pipeline after cache exhaustion. Alpha comparisons not
expressible by the current GUI shader, and authored `alphaTest` stages whose
classic meaning belongs to the separate depth/coverage pass, cause complete
classic rollback.

Neither backend can return to the classic walker after its first visible draw;
an unexpected post-commit coverage-report failure is diagnostic only, preventing
double rendering.

## Control and diagnostics

`r_rendererSharedGui` is the single cross-backend control:

- `0` (default): every 2D view uses the established classic backend path;
- `1`: an eligible complete view may use the shared domain; any rejection uses
  the complete classic view.

The setting is independent of `r_rendererModernVisible`, allowing Vulkan and
OpenGL to exercise the same domain without enabling the larger hybrid-lighting
experiment. Baseline and benchmark tools explicitly force it off unless a run
is intentionally collecting shared-domain evidence.

`gfxInfo` reports prepared/valid state, ready and fallback view counts, source
and no-op surfaces, evaluated/drawable/inactive/active-no-op pass counts, a
deterministic contract hash, and per-backend owned/fallback counts. Material
resource diagnostics report referenced/eligible/fallback records, pass-pool
use, the first material/stage failure, and stale opaque resource rejection.

Use these renderer commands for focused validation:

```text
rendererContractsSelfTest
rendererMaterialResourceTableSelfTest
rendererClassicGuiDomainSelfTest
gfxInfo
```

The dependency-light contract test covers per-draw bounded register evaluation,
non-finite data, malformed enums, overflow, atomic destination reset, repeated
order, alpha comparison, and every inactive/no-op disposition. The material
self-test covers repeated same-semantic stages, packet-derived admission,
generation-safe texture ids, state translation, explicit alpha/decal/post-process
rejection, and atomic pass-pool exhaustion. The domain self-test covers its
bounded ranges, stable hashing, and diagnostic vocabulary. The static
`renderer_classic_gui_domain.py` contract locks down the transactional
preparation and preflight-before-commit structure and prohibits raw material or
register reads in either shared backend consumer. Runtime screenshot and
ownership capture remains the end-to-end proof of whole-view rollback.

Runtime promotion evidence must use the engine `screenshot` command in a
bordered window. Capture the same static menu/loading/HUD domain with the option
off and on for both OpenGL and Vulkan, retain the domain hash and ownership
diagnostics, and exercise at least one unsupported material/view that reports a
whole-view fallback with no mixed or dropped-stage count.

Local implementation evidence collected on 20 August 2026 used the stock
`game/storage1` scene at 1280x720 in a bordered window. The benchmark harness
passed with the option both off and on for OpenGL and Vulkan and retained an
engine TGA for each run. With the option on, the final sampled frame reported
one complete view owned and six complete views on explicit classic fallback on
each backend; earlier sampled frames reported two owned views. The off runs
reported no prepared shared views. Visual inspection of all four engine captures
showed the complete stock scene and crosshair. These are development-worktree
results, not clean-package or cross-platform release-promotion evidence.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
owns the stock legacy HUD (`ui_retained 0`) over a frozen `game/airdefense1`
view on OpenGL and Vulkan, under the default preset and with the post chain
off: one owned root view, byte-identical to the classic frame; hiding the HUD
is the visible control and `r_showOverDraw 1` the named zero-commit rollback.

- **Vulkan HUD batching.** Vulkan used to keep the game's final scene
  composite and the HUD that follows in one 2D view, so its first surface (a
  post-process blit) turned the whole HUD away. The composite now ends its 2D
  batch on both backends, as OpenGL already did for its `_currentRender`
  capture; the classic frame is unchanged.
- **Scope boundaries, by design.** The retained HUD (the default interface)
  samples `_currentRender` for its backdrop blur (`materialPassIneligible`),
  and the game's fullscreen post passes are post-process views
  (`sourceSurfaceFallback`); both stay on the classic path.

## Milestone D completion and release-promotion boundary

This domain meets Milestone D's first complete-domain implementation gate; it
does not make the whole classic frame modern. The second complete-domain
implementation now covers eligible ambient-only 3D world views behind its own
default-off setting, established depth prerequisite, pre-fog/post-fog split,
and whole-view rollback. Stock `maps/tools/mv2` option-off/on engine captures
now match exactly on GL and Vulkan, and a stock deform override proves named
zero-draw whole-view fallback on both backends; see
[Shared Classic World Ambient/Material Domain](classic-world-ambient-domain-modernization.md).

Eligible unshadowed and shadow-coupled fixed-classic interaction lighting now
form the expanded third complete default-off shared domain with whole-view
rollback. Its shadow corridor covers stencil volumes, projected/CSM/parallel
maps, point cubes, mixed and same-light hybrid ownership, dynamic/perforated
mapped casters, and named atomic translucent-moment fallback. The fourth
complete domain now transactionally owns eligible fog/blend phases on both
backends behind its own default-off setting; native/static and controlled
GL/Vulkan runtime qualification pass, while authored-stock and release
promotion remain open. The independent default-off
[material-deform contract](classic-deform-domain-modernization.md) now lets this
domain consume a sealed completed or intentional-empty CPU deform result; an
unsupported, skipped, failed, stale, or mismatched record still rolls the
complete GUI view back before drawing.

Subview, cinematic/authored-post, and render-demo/Raven special-frame ownership
now have dedicated default-off transactions. Eligible authored-post tails
inside a sealed special-view tree keep their own semantic records but publish
or roll back atomically with that tree; see [Shared Classic Cinematic and
Authored-Post Transaction](classic-cinematic-post-domain-modernization.md),
[Shared Special-Subview Transaction](classic-subview-domain-modernization.md),
and [Shared Render-Demo and Raven Special-Frame
Transaction](classic-special-frame-domain-modernization.md).

These independently guarded corridors complete Milestone D's scoped
implementation. They do not make the aggregate `r_rendererModernVisible` path
or every classic-frame category shared-owned. Final-package, human-review, and
target-platform/driver promotion evidence remains separate; temporal
presentation is the next roadmap milestone.
