# Shared Classic Cinematic and Authored-Post Transaction

## Status

This Milestone D corridor is **Implemented and qualified for individual
promotion (default-off; REN-01 runtime qualification on Windows OpenGL/Vulkan
recorded 13 November 2026; non-Windows platforms and human appearance review
belong to platform qualification)**; see
[REN-01 qualification](#ren-01-qualification-13-november-2026).
`r_rendererSharedCinematicPost 1` lets OpenGL and Vulkan use the same sealed
transaction boundary for two dynamic classic-frame islands:

- an eligible root 2D view containing `videoMap` or `soundMap` playback; and
- the complete ordered `SS_POST_PROCESS` tail of an eligible ordinary root 3D
  view or of an eligible child in a sealed special-view tree.

The implementation is original openQ4 work. It incorporates no external source
code and changes no stock Quake 4 asset.

## Transaction boundary

The shared domain does not attempt to turn a cinematic decoder, current-frame
capture, or custom authored shader into a static material record. Those inputs
are dynamic by contract. Instead, front-end scene packets seal the complete
source range, exact admitted draw-packet identity/order, view identity, and the
cinematic render-view clock before either backend can claim it.

For root cinematics, all root sources remain in one transaction; a matching GUI
packet sequence proves the packet-admitted surface order. The stored time is
derived from the same `floatTime` and render-view parameter consumed by the
classic video path. For authored post, the transaction starts exactly at the
first `SS_POST_PROCESS` surface and must consume the entire remaining tail with
matching `RENDER_PASS_AUTHORED_POST` packet identities. It records cinematic,
`_currentRender`, and `_currentDepth` stage use for diagnostics.

Special-view topology is prepared first. A nested authored-post record must
resolve the already sealed subview member and root, then retain that root view,
root scene-packet index, and exact nesting depth. Missing, rejected, or changed
special-view topology is not treated as an ordinary root post range.

After preflight, each backend dispatches the established dynamic-stage adapter:
OpenGL retains `RB_STD_DrawShaderPasses`; Vulkan retains
`VK_Exec_DrawAmbientStages`. Therefore video decode, scratch-image upload,
sound/video timing, feedback capture, custom program stages, and ordered
post-material execution stay on their known classic implementations. This is a
sealed ownership and fallback boundary, not a second decoder or post shader
implementation.

Nested completion is deliberately two-phase. The dynamic-stage executor first
records exact source-surface completion without publishing cinematic/post
ownership. The outermost special-view transaction then verifies every admitted
dynamic range and every direct/capture member before publishing all nested
cinematic/post and subview records. A cinematic/post rejection marks the whole
special-view tree fallback; a subview rejection marks every nested
cinematic/post record for that root fallback. No descendant can remain reported
shared-owned after its root transaction fails.

## Eligibility and fallback

The root cinematic path accepts only a non-subview, non-editor, non-world 2D
view with no post-process sources and at least one cinematic stage. The authored
post path accepts either an ordinary root 3D view or a child view with an exact
`superView` and source surface while `r_rendererSharedSubview 1` has already
prepared a ready member/root record. In either form, the post tail must be
complete and packet-matching. Both paths reject invalid scene/pass ranges,
changed source identity, overflow, non-finite cinematic time, render targets
other than the window, the renderer's own scene target or the game's forward
scene target, debug/skip modes, feedback-capture failure, and backend
rejection.

A rejection uses the complete classic fallback path. Once an adapter starts, it
retains the whole sealed range and reports
ownership only if its exact source-surface count is reconciled; it never mixes a
partially shared cinematic or post chain with the classic walker.

Nested admission and rollback have explicit diagnostics. `missingSpecialView`
identifies a post tail that cannot bind to a ready special member/root;
`specialViewTransactionRejected` propagates special-tree rejection into its
dynamic records; and `backendSpecialViewIncomplete` names incomplete backend
coupling in the cinematic/post vocabulary. In the subview vocabulary,
`nestedCinematicPostFallback` propagates a dynamic-stage rejection to the whole
tree and `backendNestedCinematicPostIncomplete` rejects root finalization when
an admitted dynamic range did not finish with exact coverage.

## Control and diagnostics

`r_rendererSharedCinematicPost` remains independently selectable from GUI,
in-world GUI, world ambient, interaction, fog/blend, subview, and deform
ownership. When it and `r_rendererSharedSubview` are both enabled for an
eligible nested tail, their records deliberately share one atomic root
transaction:

- `0` (default): cinematics and authored post surfaces use the normal backend
  path;
- `1`: an eligible complete root cinematic view, root authored-post tail, or
  authored-post tail inside a sealed special view may use the transaction; all
  other work remains classic-owned. Nested admission remains unavailable when
  `r_rendererSharedSubview` is off.

`gfxInfo` reports request/preparation state, source scenes, root/post counts,
ready/fallback views, cinematic and current-render/depth stage counts, semantic
hash, and OpenGL/Vulkan ownership/fallback/mismatch/duplicate totals. Its
`nested=<views>/<transactions>`
and `nestedCinematic=<stages>` fields expose admitted special-view members,
distinct root transactions, and nested cinematic stages. Focused
dependency-light validation is:

```text
rendererScenePacketSelfTest
rendererClassicCinematicPostDomainSelfTest
gfxInfo
```

`tools/tests/renderer_classic_cinematic_post_domain.py` guards the bounded
domain interface, packet/timing admission, subview-before-cinematic preparation,
exact OpenGL/Vulkan dynamic-stage handoff, deferred nested completion,
bidirectional transaction fallback, conservative defaults, release-harness
isolation, diagnostics, and CI registration.

## Controlled nested acceptance evidence

The final controlled Windows acceptance on 2026-08-23 passed all six cases on
both OpenGL and Vulkan. It used an isolated temporary runtime and the generated
`maps/tools/milestone_d_nested_dynamic` fixture: one stock-material,
capture-backed color-2D mirror whose child-only authored-post material has one
frozen stock ROQ cinematic stage and one `_currentRender` stage. The fixture is
validation-only `.tmp` content and is neither committed nor shipped.

- The foundation self-test report is
  `.tmp/renderer-validation/milestone-d-foundation-20260823-r6/renderer_validation_report.json`
  (SHA-256
  `0659f1f2ee5108a422cd6c41bdc78809731f25ad7eda4b5ee84328a9163ce7ac`),
  with accompanying Markdown SHA-256
  `a619169ee97f2d6e8c7dfab991adbab5b5b282fd2e1197e2d59eb3609c87cddf`.
  It records a passing foundation case with no warning signature.
- The fixture manifest is
  `.tmp/renderer-milestone-d/qualification-20260823-r6/fixture_manifest.json`
  (SHA-256
  `24a2a4926f4b1263e311ad762bf40d391b33de225732d12154ebd7625705282d`).
  It pins the generated map/material/`.proc`/`.cm`, the copied retail ROQ, the
  required runtime components, and a 3,884-file, 7,281,560,043-byte stock
  dependency inventory sealed by SHA-256
  `13abad18f70eb8b4bf6ea0e9697b317718bb065ee76f86070787005b045dda7a`.
  Its eight-file, 25,330,583-byte fixture evidence inventory is sealed by
  SHA-256
  `0572fb8d9c132f7e97060345464f97750e59e69819871fce90eed13d1348d8ae`.
  The fixture and both inventories remained unchanged across acceptance.
- The acceptance report is
  `.tmp/renderer-milestone-d/acceptance-20260823-r6-all/renderer_milestone_d_acceptance_report.json`
  (SHA-256
  `b49031c48267fbfa86c295082707525a390d6090c24f600d00644237a67b252a`).
  Its runtime at
  `.tmp/stock-runtime/milestone-d-qualification-20260823-r6` remained
  unchanged with manifest SHA-256
  `dbece44597ec5e0686eed215f6ae99c5d6ba4cdd959c1efa7c4acaf1bc2ca673`.
  Runtime, fixture, and stock verification failures were all empty.

Each backend ran classic normal, subview-only normal, cinematic-only normal,
both-on normal, classic post-skip, and both-on post-skip sequentially. The fixed
capture script disabled input and produced actual windowed mode `-1` runs at
1280x720 with pacing-only sampling, GPU timers off, and the engine `screenshot`
command writing 1280x720 TGA images from the render target. All 12 cases passed.
Each backend's four exact comparisons reported RMS `0`, maximum delta `0`, and
zero differing channels: the three enabled normal combinations matched classic
normal, and both-on skip matched classic skip. Normal versus skip remained
visibly non-vacuous: OpenGL changed 46,359 RGB channels at RMS `2.0073` /
maximum `72`; Vulkan changed 3,329 channels at RMS `0.2506` / maximum `18`.

In both-on normal, the active backend reported one prepared and valid nested
post view, one nested special transaction, one nested cinematic stage, one
cinematic stage, and one `_currentRender` stage. Its cinematic
owned/fallback/mismatch/duplicate tuple was `1/0/0/0`, while the matching
subview owned/fallback tuple was `1/0`. With `r_skipPostProcess 1`, those tuples
were `0/1/0/0` and `0/1`, and the subview named
`nestedCinematicPostFallback`; classic/shared output remained exact. The
inactive backend's counters stayed zero in every run. The independent-CVar
cases stayed classic-equivalent: subview-only owned the mirror with cinematic
request/view counts at zero, while cinematic-only was prepared but correctly
vacuous because this fixture authors the post tail only inside the subview.

A supplemental windowed Vulkan run exercised the terminal `r_skipAmbient 1`
rollback path. Its report at
`.tmp/renderer-gameplay/milestone-d-vk-skipambient-20260823-r6/renderer_gameplay_benchmark_report.json`
(SHA-256
`f5083b492b832557bf30e6bc268b1dec94079e3828fb615ce10261c4e26c5c99`)
passed and recorded cinematic `VK=0/1/0/0` plus subview fallback `1`, with the
named `nestedCinematicPostFallback` reason and no mismatch or duplicate. This
is targeted early-exit coverage, not an expansion of the promotion scope.

The retained invocation is:

```powershell
python tools/tests/renderer_milestone_d_acceptance.py `
  --runtime-dir .tmp/stock-runtime/milestone-d-qualification-20260823-r6 `
  --fixture-manifest .tmp/renderer-milestone-d/qualification-20260823-r6/fixture_manifest.json `
  --render-api all `
  --output-dir .tmp/renderer-milestone-d/acceptance-20260823-r6-all
```

## Release-qualification boundary

The final R6 acceptance closes the controlled runtime gate only for the
single nested cinematic-plus-`_currentRender` mirror fixture above. It does not
qualify root cinematic views, ordinary-root authored post, `_currentDepth`,
multi-level or other broad special-view forms, in-world GUI, render-demo/Raven
special frames, a final release package, non-Windows platforms, or broader
driver coverage.

Before promotion, retain engine-written `screenshot` captures—not OS
captures—for stock root cinematic and ordinary-root authored-post scenes with
the setting off and on, separately on OpenGL and Vulkan. Add a controlled
`_currentDepth` nested case and the applicable broader special-view forms.
Require nonzero reconciled ownership where content is eligible, exact
same-settings output, a deterministic cinematic clock, and named malformed
packet/capture or dynamic-stage fallback with zero committed ownership. Also
retain clean final-package diagnostics, human review, and target-platform/driver
coverage.

Render-demo and Raven special-frame ownership now have their own sealed
[transaction](classic-special-frame-domain-modernization.md). Direct
special-subview ownership is now documented in the [Shared Special-Subview
Transaction](classic-subview-domain-modernization.md), including atomic
authored-post completion and rollback for eligible nested tails.

Together with the other independently guarded shared corridors, this completes
Milestone D's scoped implementation. It does not make every classic-frame
category or the aggregate modern-visible renderer shared-owned.

## REN-01 qualification, 13 November 2026

The [shared-domain qualification harness](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)
covers the open forms on OpenGL and Vulkan under the default preset and with
the post chain off:

- **Ordinary-root authored post.** The stock `game/airdefense1` gameplay view's
  post tail is owned and byte-identical to the classic frame;
  `r_skipPostProcess 1` is the visible control.
- **`_currentDepth`, `_currentRender` and a cinematic stage.** In the
  harness's validation room, one post panel reads `_currentDepth` and another
  plays the stock `video/idlogo` cinematic blended with `_currentRender`. The
  world clock is frozen, so the cinematic clock is deterministic; the frame is
  byte-identical to the classic one, and an empty post range
  (`r_skipPostProcess 1`) is the natural zero-commit rollback.
- **Root cinematic view.** A root 2D view playing stock `video/idlogo` is owned
  on both backends, `r_skipDynamicTextures 1` is the visible control and the
  qualification diagnostic `r_rendererSharedFaultInject 4` the named rollback.
  GUI videos run on the wall clock (`com_frameRealTime`), which no capture can
  pin, so this case records parity rather than requiring it; the exact parity
  of cinematic stages is the frozen post case above.

Under the default presets the root post tail draws into the game's forward
scene target (OpenGL) or a scene target (Vulkan), which the adapters used to
refuse; they now draw where the classic walker draws. Human appearance review
and non-Windows platforms belong to platform qualification.
