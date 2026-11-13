# Shared Classic Fog/Blend Domain

## Status

The fourth Milestone D classic-frame domain is **Implemented and qualified for
individual promotion (default-off; REN-01 runtime qualification on authored
stock fog on Windows OpenGL/Vulkan recorded 13 November 2026; non-Windows
platforms and human appearance review belong to platform qualification)**; see
[REN-01 qualification](#ren-01-qualification-13-november-2026).
`r_rendererSharedWorldFogBlend 1` allows the complete fog/blend phase of one
eligible ordinary root 3D view to be evaluated once into backend-neutral sealed
records and consumed by OpenGL or Vulkan. The established backend fog/blend
walker remains the supported default and the complete-phase rollback.

This is a complete fog/blend ownership boundary, not a claim that the complete
frame, the older aggregate `r_rendererModernVisible` experiment, or all of
Milestone D is modern-owned. Shared root GUI, world ambient/material, and
fixed-classic interaction ownership remain independent domains with independent
settings and rollback decisions.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Why the boundary is a complete phase

Fog and blend lights are compositing operations, not ordinary additive
interaction primitives. The established frame order is interaction lighting,
pre-fog ambient/material work, the complete fog/blend pass, and then post-fog
material work. A fog light also draws its frustum cap even when it has no
receiver packet, while a blend light can contain multiple conditionally active
stages with distinct projection images, matrices, colors, and framebuffer blend
state.

The shared domain therefore accepts or rejects the complete fog/blend phase of
one view. It never hands off one light or receiver after another light has
already changed the main target. The transaction covers:

- every fog or blend light in original `viewLights` order;
- exact GLOBAL then LOCAL receiver-chain identity and order;
- every blend-light material stage, including inactive and recognized no-op
  dispositions;
- every fog receiver plus the fog light's frustum-cap work;
- evaluated light registers, texture matrices, projection/falloff planes,
  colors, density, scissors, culling, depth, and blend state;
- generation-checked projection, falloff, `_fog`, and `_fogEnter` resources;
  and
- exact light, receiver, stage, cap, draw/no-op, resource, semantic-hash, and
  backend coverage accounting.

If any required record cannot be represented, the transaction publishes no
shared phase. If backend preflight cannot retain the complete plan, that backend
records one named fallback and performs zero shared main-target fog/blend draws.

## Exact classic fog contract

The fog corridor preserves the established fixed-classic behavior:

- only fog lights admitted by the original view-light walk participate;
- GLOBAL receivers execute before LOCAL receivers, while the classic path's
  non-fogged translucent chain remains outside this pass;
- the first light-material stage supplies the evaluated fog color and density;
- distance and entry texgen planes are derived per receiver space from the view
  origin, model matrix, fog plane, and classic fog-distance rule;
- `_fog` and `_fogEnter` retain their ordinary RGBA lookup behavior;
- receiver work uses the classic depth-equal, depth-mask, source-alpha blend
  state and original per-surface scissor/cull semantics; and
- the fog frustum cap uses its sealed geometry, fog plane, back-sided culling,
  depth-less comparison, view scissor, and the same evaluated fog color.

A fog light with no eligible receiver still owns its required frustum-cap work.
Missing that cap from packet or backend accounting is a complete-phase failure,
not an empty successful view.

## Exact classic blend-light contract

The blend corridor walks every authored light-material stage in order. Each
record retains its checked condition, active/no-op disposition, evaluated RGBA,
authored blend factors and write/depth state, projection image, falloff image,
optional texture matrix, localized projection/falloff planes, and GLOBAL or
LOCAL receiver identity.

The backend result remains the projected stage image multiplied by the falloff
sample and evaluated stage color, then composited with the stage's authored
framebuffer blend state. The shared path does not reinterpret a blend light as
a clustered additive light or collapse repeated stages into one draw.

## Eligibility and whole-phase fallback

The first shared corridor admits ordinary root 3D views whose complete active
fog/blend phase can be sealed and preflighted. It preserves the established
`r_skipFogLights`, `r_skipBlendLights`, overdraw, x-ray-view, material `noFog`,
surface-coverage, and suppress-in-subview decisions rather than weakening them
to obtain ownership.

Preparation or backend preflight rejects the complete phase for conditions such
as:

- subview, mirror, x-ray, editor, offscreen, render-demo, or other non-root view
  ownership;
- packet/light/receiver/stage/geometry identity or ordering mismatch;
- unsupported or unsealed material-deform provenance, generated/deformed,
  packed, GPU-palette, missing-cache, depth-hack, or other unsealed receiver or
  cap geometry;
- custom, cinematic, dynamic, defaulted, unloaded, stale, or invalid image
  resources;
- invalid or non-finite registers, colors, matrices, planes, density, or state;
- unsupported blend, depth, cull, sampler, descriptor, target, or pipeline
  state;
- bounded light, receiver, stage, texture, geometry, uniform, descriptor, or
  pipeline-capacity overflow; or
- any backend transaction-preflight failure.

Fallback is decided before the first shared main-framebuffer write. An
unexpected post-commit coverage mismatch is a diagnostic fault; the classic
fog/blend stream is never layered over a partially submitted shared result.

## Shared record and backend flow

1. Scene packets identify fog/blend receiver work and preserve its source light,
   receiver class, chain ordinal, surface, material, geometry, and instance
   identities.
2. `ClassicFogBlendDomain` walks the original view lights transactionally,
   evaluates all required light stages and fog state once, resolves every opaque
   resource id, seals receiver/cap records, and computes a backend-independent
   semantic hash.
3. The domain publishes the view only after exact packet/source reconciliation
   and complete bounded preparation. Failure rewinds every frame-local arena to
   its checkpoint and retains only the named fallback.
4. OpenGL and Vulkan independently preflight the complete published phase before
   the first shared draw. The consumers use only sealed records; they do not
   reread light material stages or shader registers during submission.
5. A backend reports ownership only when its fog lights, blend lights,
   receivers, ordered stages, caps, drawable/no-op records, and total submitted
   work reconcile exactly with the shared view.

OpenGL retains all required geometry, images, sampler/state transitions, and
program/fixed-function requirements before commit, then executes the sealed
phase at the established fog point. Vulkan retains geometry-ring, uniform,
descriptor, exact pipeline-key, render-scope, target, and capacity requirements
before command recording can affect the main target. Both backends leave the
surrounding pre-fog/post-fog material ordering unchanged.

## Control and diagnostics

`r_rendererSharedWorldFogBlend` is the single cross-backend control:

- `0` (default): every fog/blend phase uses the established backend walker;
- `1`: an eligible complete phase may use the shared domain; any rejection uses
  the complete established phase.

The setting is independent of `r_rendererSharedWorldInteraction` and
`r_rendererSharedWorldAmbient`. Stock-baseline, ordinary gameplay-benchmark,
and renderer default-safety profiles force it off unless a run intentionally
collects shared fog/blend evidence.

`gfxInfo` reports requested/prepared/valid state; ready and fallback view
counts; fog/blend light, receiver, stage, cap, drawable, inactive, and no-op
counts; the stable semantic hash and first failure; and exact OpenGL/Vulkan
owned/fallback coverage.

Use these commands for focused validation:

```text
rendererContractsSelfTest
rendererClassicFogBlendDomainSelfTest
gfxInfo
```

The dependency-light self-test covers bounded publication and rewind, stable
hashing, fog density and plane state, receiver/cap accounting, repeated and
inactive blend stages, source-order reconciliation, backend coverage, and named
atomic fallback. `tools/tests/renderer_classic_fog_blend_domain.py` guards
packet/source identity, complete OpenGL/Vulkan preflight before commit, sealed
consumer bodies, the pre-fog/fog/post-fog insertion order, conservative default
and bootstrap state, benchmark/baseline isolation, and validation-workflow
registration.

Native and static validation passes. The controlled runtime evidence below
establishes exact same-backend parity, nonempty ownership, visible fog/blend
contributions, and atomic rollback for the bounded fixture; it is not a
substitute for the remaining authored-stock and release-package gate.

## Controlled runtime qualification

The registered `fog-blend` gameplay profile uses a controlled
`sp-mv2-fog-blend` scene on stock `maps/tools/mv2`. It may spawn only shipped
light declarations, including `lights/fog_generic` for fog and the deterministic
`lights/fog_ambient` declaration for blend-light coverage. The animated
`lights/stream_fog` declaration is intentionally excluded from reference-image
comparisons because its time-driven flicker tables make captures from separate
runs non-comparable. No authored stock-map use of the shipped blend-light
declarations has been identified, so this controlled stock-declaration scene is
the explicit blend qualification route.

The 2026-08-21 development-worktree run used the staged Windows x64 runtime,
bordered/windowed 1280x720 output, and only the engine's registered `screenshot`
command. OpenGL and Vulkan both passed the following acceptance set:

- The mixed fog-plus-blend shared/classic engine TGAs match exactly at RMS `0`,
  maximum delta `0`, and zero differing RGB channels. The OpenGL shared image is
  SHA-256 `2364ae2cb2d3a779662d7a3bff1787e1cf00b160eaf0e778691cceeb8faef6ec`;
  the Vulkan image is
  `1d1e7eea9562bf8b79926d7f6ba9cf612543e48c14cd977825a34ba3ab6c45d5`.
- Each mixed run owns one ready view containing two lights (one fog and one
  blend), six reconciled GLOBAL receivers, two active stages, seven drawable
  primitives, three fog receivers, one fog cap, and three blend draws. OpenGL
  reports domain/view hashes `6a8bd7fc8e02ab52` / `9d4a3224d67600ea`;
  Vulkan reports `2bd11c573a8ab38f` / `ed0bb8aac24c2771`. Backend mismatch,
  duplicate, and untracked counters remain zero.
- With `r_skipBlendLights 1`, the shared fog-only result still matches its
  same-settings classic TGA exactly on both backends. It owns three fog
  receivers and one fog cap, records the blend light as one sealed no-op, and
  commits zero blend draws. The OpenGL and Vulkan image SHA-256 values are
  `e0be5e2cdf19a72eae8895c8036bf80b734e6a980917eb27bf1b6e06dd7c0557`
  and `81c84e165de0bafd33afaa98e24f8f73d64f9d574bf1d08cacaf7bd61669de45`.
- The mixed result differs materially from that fog-only reference on both
  backends at RMS `36.7624`, maximum delta `71`, and `1,921,110` changed RGB
  channels, proving the three owned blend draws affect the final image.
- The fog-only result differs materially from the matching established
  `r_skipFogLights 1` outer-phase skip at RMS `61.3216` and maximum delta `162`:
  `2,764,380` RGB channels change on OpenGL and `2,764,369` on Vulkan. This
  comparison intentionally uses the classic outer skip because the established
  `r_skipFogLights` behavior suppresses the complete fog/blend phase.
- The intentional `r_singleTriangle 1` blocker reports
  `failure=unsupported-state detail=200`, one complete-phase fallback, zero
  committed shared content counters, and exact same-settings classic TGA parity
  on both backends. The active backend alone records the fallback; the inactive
  backend remains zeroed.
- Logs report no OpenGL error, framebuffer failure, Vulkan validation warning,
  VUID, Vulkan call failure, fatal error, or engine error line.

Generate and consume separate classic references for OpenGL and Vulkan. A
same-looking or cross-backend screenshot without exact ownership diagnostics is
not sufficient. Earlier Vulkan Phase G fog evidence proves the established
Vulkan fog implementation, not this shared transaction, and cannot close this
gate.

The retained local reports are under
`.tmp/renderer-gameplay/fog-blend-{gl,vk}-{classic-stable-pass,owned-stable-qualified,classic-skip-stable,owned-fog-delta-qualified,classic-skipfog-pass,classic-rollback,shared-rollback-qualified}`.
They bind the staged runtime and dirty source revision used for this development
qualification.

## REN-01 qualification, 13 November 2026

The authored-stock fog case is now part of the
[shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01):
a fixed camera inside a stock `lights/fog_generic` volume near the
`game/airdefense1` bunker. On OpenGL and Vulkan, under the default preset, with
4x MSAA and with the post chain off, the domain owns the fog phase and the
frame is byte-identical to the classic one; turning fog and blend lights off
is the visible control and `r_showOverDraw 1` the named zero-commit rollback.

Under the default presets the adapters had refused the scene target the view
draws into (OpenGL: the game's forward target; Vulkan: the domain was never
attempted while a scene target was active). They now draw where the classic
walker draws. A view the domain never prepared, such as a portal sky, still
reaches the backend gate and is reported as an untracked fallback; it draws
classic and is not a coverage fault.

Human appearance review and non-Windows platforms belong to platform
qualification.

## Milestone D completion and release-promotion boundary

Root GUI, world ambient/material, fixed-classic interaction, and fog/blend are
now four independently guarded complete shared domains with retained local
runtime qualification. They do not make the complete classic frame modern, and
their clean-package/platform release-promotion gates remain separate.

The independent default-off
[material-deform contract](classic-deform-domain-modernization.md) now records
the classic source-preserving fog-receiver role explicitly. A material-deform
receiver is admissible only with a fresh `notApplicable` record; unsupported,
skipped, failed, stale, or mismatched records still reject the complete phase.

The completed cinematic/authored-post corridor seals eligible root video/audio
views and complete post tails while preserving the established dynamic-stage
executor. Eligible authored-post tails inside a sealed special-view tree now
complete and publish with that tree's root transaction; see [Shared Classic
Cinematic and Authored-Post
Transaction](classic-cinematic-post-domain-modernization.md) and [Shared
Special-Subview Transaction](classic-subview-domain-modernization.md).
Render-demo and Raven special-frame ownership have a dedicated
[transaction](classic-special-frame-domain-modernization.md).

These independently guarded corridors complete Milestone D's scoped
implementation. They do not make every classic-frame category or the aggregate
modern-visible renderer shared-owned. Authored-stock, clean-package,
human-review, and target-platform/driver evidence remain release-promotion
gates; temporal presentation is the next roadmap milestone.
