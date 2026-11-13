# Shared Classic World Ambient/Material Domain

## Status

The second Milestone D classic-frame domain is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification on Windows
OpenGL/Vulkan recorded 13 November 2026; non-Windows platforms and human
appearance review belong to platform qualification)** for complete eligible 3D
world ambient/material views; see [REN-01 qualification](#ren-01-qualification-13-november-2026). OpenGL and Vulkan consume the same ordered,
per-draw evaluated material records. The established classic view remains the
supported default and the whole-view rollback.

Implementation, dependency-light validation, and local Windows stock-runtime
image/ownership/rollback evidence are complete for both backends. The retained
OpenGL and Vulkan captures prove exact option-off/on output for one eligible
view and exact complete-view fallback for one deform blocker. These are
development-worktree results, not clean committed-package or cross-platform
release-promotion evidence.

This is an ambient/material ownership boundary, not modern interaction-lighting
ownership. The established depth prepass remains authoritative, and any view
containing non-owned interaction, shadow, light-grid, fog/blend, special-effect,
GUI, or post work remains completely on its established backend walker.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Domain boundary

The domain considers complete ordinary 3D world `RC_DRAW_VIEW` work. A view is
eligible only when it is a normal world scene with an enabled ambient packet,
contains no non-owned packet work, and every original source surface can be
classified, matched, evaluated, resolved, and preflighted in source order.
The only benign render-flag mask is `RF_NO_GUI | RF_PENUMBRA_MAP |
RF_PRIMARY_VIEW`; any other flag rejects the view.

For an eligible view, the shared path preserves:

- original surface and repeated ambient-stage order;
- exact stage conditions and evaluated RGBA values;
- 2x3 texture matrices, blend factors, color/depth write masks, depth
  comparison, culling, fixed-function alpha comparison, explicit texgen,
  vertex-color mode, and polygon offset;
- generation-checked texture-resource identity and sampler state;
- geometry, instance/model-view and projection matrices, viewport, and
  scissors;
- inactive and recognized no-op passes as ordered coverage records rather than
  silently deleting them; and
- a deterministic complete-view hash and per-backend coverage report.

The first version deliberately rejects portal skies, suppress-in-subview and
in-world GUI materials, subviews and mirrors, editor/render-demo mutations,
clip planes, global material overrides, BSE/outline/dynamic-texcoord special
surfaces, unsealed or non-admitted material deforms and skinned geometry,
weapon/model depth hacks, negative
scale, decals and decal-color streams, post-process sorting, synthetic or
missing spaces, current-render/depth images, cinematics, dynamic/defaulted/
unloaded images, custom programs, non-explicit texgen, player-visibility
surface work, unsupported fixed-function state, and every bounded-pool or stale
resource failure. One rejection keeps the complete view classic.

Unpacketized or backend-local view mutations are also explicit blockers:
portal-distance fades, a positive forced-ambient floor, cel/world-outline work,
overdraw or single-triangle diagnostics, ambient/render skip modes, same-view
blur/AL special-effect commands, and any render target other than the window,
the renderer's own scene target or the game's forward scene target (REN-01). A
benign zero-mask special-effect command is a no-op rather than a false blocker.

The domain owns only the two ambient walks, so the classic phases both walkers
draw before, between or after them keep their owners and are admitted (REN-01):
stencil shadows, shadow maps and ARB2 interactions before the pre-fog walk,
the light grid between it and fog, fog/blend lights between the walks, and
SSAO, motion blur and bloom after the post-fog walk. Packet categories with
active deferred, forward+, authored-post, special-effect, GUI or present work
remain whole-view blockers. (The first version admitted only depth and ambient
packets and required an empty light list.)

## Depth and phase contract

The shared domain owns ambient/material color work only. It does not replace or
redraw the established depth prepass. Before a view can be published, every
opaque or perforated ambient draw must have a matching `RENDER_PASS_DEPTH`
packet for the same source surface, material, geometry, and instance. A
translucent ambient draw has no depth-packet prerequisite. Missing or mismatched
coverage rejects the complete view.

An authored stage `hasAlphaTest` remains coverage metadata for the established
depth pass; it is not reapplied as an ambient color test. The world compiler can
therefore retain such a stage only because the complete-view transaction proves
the matching depth prerequisite. Fixed-function alpha-comparison bits that the
classic color walker actually executes remain sealed in the evaluated color
pass.

World materials are split at the established fog boundary:

- material sort values below `SS_MEDIUM` execute in the pre-fog phase;
- material sort values from `SS_MEDIUM` up to the rejected post-process range
  execute in the post-fog phase.

Both backends submit the pre-fog records, leave the established fog position in
the frame sequence intact, and then submit the post-fog records. This phase
split preserves ordering without claiming ownership of fog or blend lights.

## Shared record flow

1. Front-end scene packets identify materials actually referenced by
   `RENDER_PASS_AMBIENT` world draws. Intrinsic material class does not decide
   admission.
2. `MaterialResourceTable` compiles those references into a distinct bounded
   world-pass pool. Every record uses `RENDERER_MATERIAL_PASS_SURFACE`,
   `RENDERER_PROGRAM_FIXED`, enabled depth testing, the authored depth-write
   bit, and a translated `LESS`, `EQUAL`, or `ALWAYS` comparison.
3. `ClassicWorldAmbientDomain` walks every original surface transactionally,
   matches the ambient packet stream and depth prerequisites, evaluates each
   authored pass once against the bounded instance-register block, resolves
   every opaque texture id, classifies the pre/post-fog phase, and hashes the
   stable evaluated contract.
4. The view publishes its draw and evaluated-pass spans only after the complete
   transaction succeeds. A failure rolls the arenas back to their checkpoints
   and publishes only a precise fallback reason.
5. OpenGL and Vulkan preflight the complete prepared view before the first
   shared ambient draw. Each backend then submits only sealed `DRAW` records,
   accounts for all inactive/no-op records, and records one owned or fallback
   outcome for the view.

## Backend execution

The OpenGL adapter completes geometry/cache, texture, sampler, state, scissor,
capacity, and coverage preflight before committing the first pre-fog draw. The
Vulkan adapter completes geometry offsets, descriptors, exact pipeline keys,
render-scope, state, scissor, capacity, and coverage preflight before the view's
depth clear or first framebuffer-affecting command.

Vulkan draws a native-size root view into its lower-origin scene target. Since
REN-01 (13 November 2026) this adapter and the other shared view adapters
(interaction, in-world GUI, cinematic/post, fog/blend) draw into that target,
or the game's forward scene target, exactly where the classic walker draws;
a scaled scene, whose rectangles the temporal presenter rewrites, keeps the
classic walker. Before that, a requested shared domain kept its view on the
swapchain instead, which changed the orientation and interpolation rounding of
the classic work beside it, so a shared frame could never match the classic
one byte for byte, and under the default presets (HDR, temporal AA) the
adapters never ran at all.

The actual shared consumers do not reread `shaderStage_t`, `GetStage`, or raw
`shaderRegisters`; those inputs are sealed by the front-end transaction. The
legacy draw-surface pointer remains only as a bounded geometry submission
bridge.

Neither backend can return to the classic ambient walker after its first shared
draw. Any pre-commit failure leaves the established complete ambient walk
untouched. An unexpected post-commit coverage mismatch is diagnostic only, so
the engine cannot double-render the view while attempting a late fallback.

## Control and diagnostics

`r_rendererSharedWorldAmbient` is the single cross-backend control:

- `0` (default): every 3D world view uses the established classic ambient path;
- `1`: a complete eligible view may use the shared domain; any rejection uses
  the complete classic view.

The option is independent of `r_rendererSharedGui`. Requesting it also prevents
the older aggregate modern-visible path from suppressing or recreating ambient
work before the complete-view transaction is decided. Stock-baseline and
gameplay-benchmark tooling explicitly force the option off unless a run is
intentionally collecting shared-domain evidence.

`gfxInfo` reports whether ownership was requested, prepared, and frame-valid;
world/ready/fallback view and surface counts; evaluated, drawable, inactive,
active-no-op, pre-fog, and post-fog pass counts; the deterministic hash; status;
and OpenGL/Vulkan owned/fallback coverage. Material-table diagnostics separately
report world-referenced, eligible, fallback, pass-pool, overflow, and first
failure information.

Use these renderer commands for focused validation:

```text
rendererContractsSelfTest
rendererMaterialResourceTableSelfTest
rendererClassicWorldAmbientDomainSelfTest
gfxInfo
```

The dependency-light tests cover the distinct packet-derived world-pass pool,
fixed surface/program/depth semantics, depth-prerequisite and phase vocabulary,
bounded transaction reset and stable hashing, and backend coverage accounting.
The static `renderer_classic_world_ambient_domain.py` regression additionally
locks down complete GL/Vulkan preflight-before-commit ordering, the pre-fog / fog
/ post-fog split, sealed consumer bodies, conservative default/bootstrap state,
benchmark/baseline isolation, and validation-workflow registration.

## Local runtime evidence

Runtime qualification used stock assets, a bordered 1280x720 window, and the
engine `screenshot` command. The exact stock map/view/camera and isolation
settings are retained by each harness report; no custom map, material, or shader
was added.

The gameplay harness registers the controlled stock `sp-mv2-ambient` case on
`maps/tools/mv2`. Use its exact validation-matrix command:

```powershell
python tools\tests\renderer_gameplay_benchmark.py --profile world-ambient --pacing-only --no-gpu-timers
```

The final profile launches with `ui_showGun 0`, `g_showHud 0`, and
`r_multiSamples 0`. After the normal spawn it runs `noclip`, then
`setviewpos 0 0 256 80 0 0`. Its post-map controls select the fast direct
no-post path, set `r_postAA 0`, select an unmatched `r_singleLight`, and disable
subview, light-grid, player-visibility overlay, portal-distance-fade, cel, and
debug islands while leaving ambient, deform, and normal rendering enabled. The
exact post-map set is:

```text
g_renderFastNoPost 1
g_renderFastNoPostDirect 1
r_postAA 0
r_singleLight 2147483647
r_skipSubviews 1
r_useLightGrid 0
r_skipPlayerVisibilityEffects 1
r_portalsDistanceCull 0
r_forceAmbient 0
r_celShading 0
r_celShadingWorld 0
r_showOverDraw 0
r_singleTriangle 0
r_skipAmbient 0
r_skipNewAmbient 0
r_skipDeforms 0
r_skipRender 0
```

It does not set `r_skipPostProcess` or `r_skipGuiShaders` and uses no
replacement asset. The base command retains the default-off comparison; append
`--set-cvar r_rendererSharedWorldAmbient=1` for the enabled run and select
Vulkan with `--render-api vk`. Because these are controlled visual and ownership
captures, `--pacing-only --no-gpu-timers` deliberately prevents them from being
mistaken for promotable per-map CPU/GPU budget evidence.

| Capture | Result | Retained evidence |
|---|---|---|
| OpenGL eligible view, option off/on | **Pass** | `.tmp/renderer-gameplay/world-ambient-gl-off-final3` and `.tmp/renderer-gameplay/world-ambient-gl-on-final3`; engine-TGA comparison RMS `0`, maximum delta `0`; enabled diagnostics `views=1 ready=1 fallback=0 surfaces=1/1 passes=1 draw=1 pre=1 post=0 hash=dc18ed8c0539bbfc GL=1/0`; view hash `bad7344c6394edf8`; both screenshots SHA-256 `3050cb852ebf8a7ba50b847926ab52255edbb19ded8545ccfe4f3f5683708334` |
| Vulkan eligible view, option off/on | **Pass** | `.tmp/renderer-gameplay/world-ambient-vk-off-final` and `.tmp/renderer-gameplay/world-ambient-vk-on-final`; engine-TGA comparison RMS `0`, maximum delta `0`; the same domain/view hashes and counts with `VK=1/0`; both screenshots SHA-256 `5549ee3dde0fa959f8185335dd9cc2b3306c8b6dd442d12285031054ef6d6ddf`; Vulkan validation/VUID/call-failure gates clean |
| OpenGL deform blocker, option off/on | **Pass** | `.tmp/renderer-gameplay/world-ambient-gl-fallback-off-final` and `.tmp/renderer-gameplay/world-ambient-gl-fallback-on-final`; stock override `r_materialOverride shaderDemos/move`; engine-TGA comparison RMS `0`, maximum delta `0`; enabled diagnostics `ready=0 fallback=1 surfaces=0/1 draw=0`, `failure=sourceSurfaceFallback detail=13 sourceClass=fallbackDeform`, GL fallback; both screenshots SHA-256 `7044775af6be6bc075ffababbda3d3bbd62632b44b4836651bec25a6ea0ef3e4` |
| Vulkan deform blocker, option off/on | **Pass** | `.tmp/renderer-gameplay/world-ambient-vk-fallback-off-final` and `.tmp/renderer-gameplay/world-ambient-vk-fallback-on-final`; the same stock override and named zero-draw failure with Vulkan fallback; engine-TGA comparison RMS `0`, maximum delta `0`; both screenshots SHA-256 `380115efeb2354b8d1d43c2c5aed146538e267e87dca759ffd4ac7ff42f7db10` |

The safe matrix retained at `.tmp/renderer-validation/world-ambient-final-2`
also passed all three selected cases: `renderer-foundation-selftests`,
`renderer-default-safety-selftest`, and `renderer-vk-clear-startup`.

Two additional OpenGL lifecycle captures passed. The live-toggle run retained
at `.tmp/renderer-gameplay/world-ambient-gl-toggle-final` first reported owned
coverage with `requested=1` and `GL=1/0`, then reported `prepared=0` and
`status=empty` after the setting changed to `0`; its final engine TGA matched the
option-off reference exactly. The partial-video-restart run at
`.tmp/renderer-gameplay/world-ambient-gl-restart-final` reported owned coverage
before and after `vid_restart partial`, with no stale/fallback outcome, the final
stable hash `dc18ed8c0539bbfc`, and an exact final engine-TGA match. Both final
lifecycle screenshots have the eligible OpenGL reference SHA-256
`3050cb852ebf8a7ba50b847926ab52255edbb19ded8545ccfe4f3f5683708334`.

This completes the local implementation/runtime gate. The evidence comes from a
development worktree and does not replace a clean committed-package recapture
or target-platform/driver qualification before promotion.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
owns the stock `maps/tools/mv2` floor on OpenGL and Vulkan, under the default
preset (HDR scene target, SSAO, bloom) and with the post chain off, both with
every light suppressed and under the map's own lights. Each frame is
byte-identical to the classic one; `r_skipAmbient 1` is the visible control
and the first-person weapon's depth hack the named zero-commit rollback.

Three restrictions went: the adapters refused any render target (so nothing
owned under the default presets), both refused a non-empty light list, and
the domain refused any classic light, light-grid, fog or screen-space pass
beside its walks. None of those changes what the two ambient walks draw.

The per-surface boundary is unchanged: decals, BSE effects, authored
post-process surfaces, in-world GUI and subview surfaces, depth hacks and the
other rejections above still keep the complete view classic. Typical lit stock
gameplay views (`game/airdefense1`, for example) contain at least one of them,
so this domain owns plain world views but not yet most gameplay frames.

## Milestone D completion and release-promotion boundary

The GUI and world ambient/material corridors are joined by the
[fixed-classic interaction domain](classic-interaction-domain-modernization.md)
and [fog/blend domain](classic-fog-blend-domain-modernization.md) as the first
four complete shared classic-frame domains. The interaction domain accepts
eligible unshadowed and shadow-coupled views; its shadow extension covers
stencil, projected/CSM/parallel, point, mixed, and hybrid ownership with
dynamic/perforated mapped casters and atomic translucent-moment fallback. The
fog/blend implementation seals the complete eligible phase for both backends;
native/static and controlled GL/Vulkan runtime qualification pass, while
authored-stock and release promotion remain open.
These domains still do not make the whole classic frame modern.

The independent default-off
[material-deform contract](classic-deform-domain-modernization.md) now admits
sealed completed or intentional-empty CPU deform results. The earlier
`shaderDemos/move` evidence remains the conservative
`r_rendererSharedDeform 0` rollback baseline; deform-enabled qualification uses
the dedicated profile and requires nonempty ownership plus exact same-backend
parity.

The completed cinematic/authored-post corridor seals eligible root video/audio
views and complete authored-post tails, including eligible tails nested inside
a sealed special-view tree. Nested dynamic completion and special-view
completion publish or roll back as one root transaction; see [Shared Classic
Cinematic and Authored-Post
Transaction](classic-cinematic-post-domain-modernization.md) and [Shared
Special-Subview Transaction](classic-subview-domain-modernization.md).
Render-demo and Raven special-frame ownership have their own dedicated sealed
[transaction](classic-special-frame-domain-modernization.md).

These independently guarded corridors complete Milestone D's scoped
implementation without making the aggregate modern-visible renderer or every
classic-frame category shared-owned. Clean-package, human-review, and
target-platform/driver evidence remain release-promotion gates; temporal
presentation is the next roadmap milestone.
