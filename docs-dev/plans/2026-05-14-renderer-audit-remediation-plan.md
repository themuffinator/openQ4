# Modern Renderer Audit And Remediation Implementation Plan

## Purpose

This plan audits the current modern OpenGL renderer against `docs-dev/plans/2026-05-14-renderer-recovery-performance-parity.md` after reports that framerate is still poor and lighting is broken.

The conclusion is that the renderer has useful scaffolding, metrics, resource naming, and fallback hooks, but it is not yet a production replacement path. Several checklist items were marked complete before their acceptance gates were proven in real gameplay, and several implemented systems are still structural placeholders. The next implementation pass must prioritize correctness first, then make the correct path fast. The target remains high performance with no rendering-quality downgrade.

## Audit Findings

### Plan Status

- The recovery plan's final `Done Means` is still mostly unchecked: modern disabled overhead, modern-visible duplicate work, coherent tier gameplay, SP/MP validation, warning-free required scenes, complete visual fallback ownership, stable shadow maps, and ARB2-or-better P95/P99 frame time are all open.
- Phase 0 evidence is still missing for the user's failing scene: no complete `.tmp/renderer-recovery/` capture set, ARB2 baseline, one-cvar-at-a-time cliff isolation, or shadow baseline matrix.
- Phase 2 and Phase 3 have implementation notes, but their most important gameplay acceptance gates remain open: blocked modern frames must recover to near ARB2 speed, and rollback must render without stale modern GL state.
- Phases 4 through 8 still contain correctness gaps that directly explain broken lighting: material parity, cluster scalability, shadow-map productionization, and the real hybrid deferred/forward+ pipeline are not actually complete.
- Phase 10 says Hi-Z and visibility are complete, but the code only has conservative scissor/frustum rejection and Hi-Z resource construction; it does not consume Hi-Z for occlusion rejection.

### Structural Problems In The Current Implementation

- `R_ScenePackets_AddDrawView` currently adds every `viewDef->drawSurfs` item to depth, ARB2 interaction, light-grid, ambient, fog/blend, and authored-post pass packets. This makes the modern plan think one sorted legacy draw list is valid for multiple semantically different passes. It inflates draw counts, produces wrong pass ownership, and can feed the modern path surfaces that the legacy pass would never draw.
- The modern draw plan is built from the synthetic pass packets, not from the true legacy pass inputs. Interactions are not derived from `viewLight_t` interaction chains, shadow work is not derived from actual shadow caster records, fog/blend is not light-owned, and post surfaces are not isolated by sort range before modern execution.
- The lighting shaders are still placeholders. Deferred and forward+ paths use a hardcoded light direction, scissor checks, simple scale factors, and clamped output. They do not evaluate Quake 4 projected-light matrices, falloff maps, point-light vectors, light shader registers, specular math, or real shadow visibility.
- Clustered lighting uploads only the first grid to the GPU. Multiple views, subviews, remote cameras, and GUI/world mixtures cannot be lit correctly from one main-view grid.
- Cluster Z binning uses camera-space light depths on the CPU but shader lookup uses raw depth or `gl_FragCoord.z`, so clusters can be selected incorrectly even when the light list is otherwise valid.
- Cluster spill references are counted on the CPU but are not uploaded or sampled by shaders. Any cluster that spills silently loses lighting contribution.
- The shadow planner creates descriptors and render-graph resource names, but modern lighting does not sample projected, point, cascade, cutout, or translucent shadow resources. `ModernClusterShadowVisibility` effectively treats mapped shadows as valid metadata, not real shadow-map comparisons.
- `shadowMap` is treated as one depth resource for modern shadow depth, while the graph names projected, point, and cascade atlases. The executor does not fill those atlases with correct light-space matrices, faces, cascade tiles, or material cutout semantics.
- The G-buffer pass clears the shared `sceneDepth` after the visible depth pass, which can discard depth from promoted alpha-tested, skinned, deformed, or fallback surfaces. Forward+ then reads an incomplete depth buffer.
- Modern passes disable culling broadly instead of carrying material cull state, mirrored-view cull inversion, two-sided semantics, and shadow-caster cull behavior through the pipeline.
- Hi-Z is built with framebuffer blit plus `glGenerateMipmap`, calls `glGetIntegerv` in-frame, and is not used for draw rejection. This adds potential driver stalls without providing occlusion savings.
- The GL 4.3 "GPU-driven" path still consumes CPU-authored visibility flags. Its compute shader validates/copies counters and writes indirect commands; it does not perform real GPU frustum, occlusion, material, or light-list culling.
- The current compatibility/ownership gates can skip legacy passes when a modern pass has executed, but the modern pass can still be visually incomplete. The gate must key off visual parity readiness, not only resource/pipeline execution.

## Implementation Plan

### Phase 0: Evidence Lock

Goal: stop guessing and make every improvement measurable.

- Capture the user's failing scene exactly: command line, map, save path, cvars, resolution, GL tier, GPU/driver, `r_rendererModernAutoPromote`, `r_rendererModernVisible`, `r_rendererModernDeferred`, `r_rendererForwardPlus`, `r_rendererOcclusion`, `r_rendererHiZ`, `r_useShadowMap`, and shadow-map cvars.
- Produce ARB2 and modern captures under `.tmp/renderer-recovery/`: logs, screenshots, `rendererBenchmarkCapture`, `framePacingSnapshot`, `gfxInfo`, render-graph dump, material table dump, cluster dump, shadow-plan dump, and GPU timings.
- Run one-cvar-at-a-time tests to identify the first severe FPS cliff: executor only, visible depth, G-buffer, deferred, forward+, shadow maps, Hi-Z, GPU validation, cluster debug.
- Make the plan fail closed until this evidence exists. No default-promotion or "complete" claims without gameplay captures.

Acceptance:

- The failing scene has reproducible ARB2 and modern evidence.
- Modern disabled overhead is below 1 percent of ARB2 P95 frame time.
- Renderer warnings are zero in the evidence logs.

### Phase 1: Fix Scene Packet Semantics

Goal: stop manufacturing pass work that the renderer should never submit.

- Replace `R_ScenePackets_AddDrawView` pass cloning with pass-specific packet builders:
  - depth: depth-fill eligible draw surfaces only, respecting sort, coverage, suppress flags, subview rules, and material depth behavior;
  - ambient/material: non-light material stages up to post-process sort boundary;
  - post: `SS_POST_PROCESS` and `_currentRender` dependent surfaces only;
  - interactions: derive from `viewLight_t::localInteractions`, `globalInteractions`, and translucent interactions;
  - shadow casters: derive from `localShadowMapCasters`, `globalShadowMapCasters`, legacy stencil chains, and material shadow contract;
  - fog/blend: derive from fog/blend lights and their interaction chains;
  - light-grid: only surfaces that can receive the light grid;
  - GUI: only fullscreen GUI/view GUI surfaces.
- Add counters proving packet counts match the legacy pass's actual draw candidates.
- Preserve the legacy sorted draw order for surfaces where order is semantically required, especially post, decals, transparency, and GUI.

Acceptance:

- Modern draw packet counts no longer multiply by the number of pass categories.
- Packet validation catches any pass receiving an ineligible material class.
- ARB2 metrics and packet metrics agree within documented filtering rules.

Round 3 Phase 1 status:

- Completed pass-specific scene packet builders for depth, ambient/material, post, interactions, shadow-map casters, stencil-shadow casters, fog/blend, light-grid, and GUI.
- Updated scene-packet, render-graph, material-table, draw-plan, and submit-plan self-tests to validate semantic packet counts instead of the former cloned draw counts.
- Validation: `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, and `python tools/tests/renderer_validation_matrix.py --tiers auto --timeout 90 --output-dir .tmp/renderer-validation/phase1-scene-packets` all pass.

### Phase 2: Restore Correct Pass Ownership

Goal: modern-owned means visually complete, not merely executed.

- Change ownership readiness from "pass ran" to "pass produced complete parity data for all surfaces/lights it intends to replace."
- Treat any material, geometry, shadow, light, post, GUI, subview, or BSE fallback as a blocker for skipping the corresponding legacy contribution, unless mixed composition is explicitly implemented for that category.
- Do not skip `RENDER_PASS_ARB2_INTERACTION` until modern lighting evaluates every contributing light type for that view or falls back per light without dropping contribution.
- Do not skip stencil or mapped shadow contribution until modern receiver sampling is real and per-light fallback is proven.
- Add a per-frame "ownership reason tree" that names the first blocker by view, pass, material/light id, and resource.

Acceptance:

- `duplicatedWithLegacy=0` and `droppedByModern=0` are both required.
- A modern visible frame with any unresolved lighting/shadow fallback keeps legacy lighting visible.
- ARB2 rollback after a modern frame has no stale FBO, texture, sampler, depth, stencil, blend, cull, or viewport state.

Round 3 Phase 2 status:

- Completed an early modern-visible ownership-readiness scan before expensive visible pass planning. Packet material/geometry fallbacks, interaction lighting, fog/blend, light-grid contribution, and shadow receiver ownership now block visible replacement instead of allowing a modern pass to suppress legacy work.
- ARB2 interaction, fog/blend, light-grid, mapped-shadow, and stencil-shadow ownership now fail closed until their modern parity is proven. Diagnostic sidecar execution remains possible, but legacy lighting/shadow contribution stays visible when a replacement is incomplete.
- Added first-blocker ownership diagnostics with view/pass/material-or-light id/resource/reason strings, plus `droppedByModern` accounting alongside the existing duplicate-with-legacy accounting.
- Updated pass-ownership and compatibility self-tests to require fail-closed lighting/shadow ownership and zero dropped-modern hazards.
- Validation: `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, and `python tools/tests/renderer_validation_matrix.py --tiers auto --timeout 90 --output-dir .tmp/renderer-validation/phase2-pass-ownership-rerun` all pass.

### Phase 3: Material And Geometry Parity

Goal: make promoted material output trustworthy before optimizing it.

- Build a Quake 4 material evaluation contract that carries stage conditions, shader registers, texture matrices, texgen, vertex color mode, alpha-test mode/register, color expressions, blend/depth state, polygon offset, sort, cull/two-sided state, and deform/skinning support.
- Implement the conservative stock subset in GLSL: bump, diffuse, specular, alpha-test, emissive, additive, filter, blend, and light-grid inputs.
- Move unsupported features into explicit fallback buckets with stable reasons: dynamic images, `_currentRender`, screen/sky texgen, custom GLSL/newStage, unsupported deforms, unsupported GPU palette skinning.
- Fix culling and transforms: material cull mode, mirrored view/model cull inversion, viewmodel FOV/depth hack, negative transforms, CPU-skinned model bounds, and tangent basis orientation.
- Stop clearing shared depth in the G-buffer pass after depth prepass. Use the existing depth with `EQUAL/LEQUAL` policy or rebuild a complete depth target intentionally.

Acceptance:

- Viper Squad/character seam captures match ARB2 within tolerance.
- Alpha-tested decals and cutout surfaces match depth and color behavior.
- G-buffer depth contains all promoted depth contributors and does not lose fallback depth accidentally.

Round 3 Phase 3 status:

- Added explicit material and geometry promotion contracts so modern draw planning fails closed for scene-capture images, dynamic images, screen/sky texgen, custom GLSL/newStage programs, texture matrices, vertex color, polygon offset, unsupported deforms, unsupported GPU palette skinning, and missing cache-backed geometry.
- Modern submissions now carry material cull/two-sided state, mirrored-view and negative-transform cull inversion, material color registers, transparent add/filter/blend state, and weapon/model depth hacks instead of treating promoted draws as generic flat debug geometry.
- Opaque G-buffer setup now preserves a valid populated scene-depth prepass and only clears/rebuilds depth when no reusable modern depth target exists, with counters for depth reuse versus rebuild.
- Draw/submit/executor self-tests were tightened around the explicit fallback contracts, default-material eligibility, cache-backed geometry readiness, and depth/G-buffer/forward+/visible-frame paths.
- Validation: `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, targeted Phase 3 self-test launch, and `python tools/tests/renderer_validation_matrix.py --tiers auto --timeout 90 --output-dir .tmp/renderer-validation/phase3-material-geometry`.

### Phase 4: Real Light Records And Clustered Lighting

Goal: replace lighting placeholders with Quake 4-compatible light evaluation.

- Define one shared light descriptor used by clustering, deferred, forward+, and shadows:
  - type: projected, point, ambient, fog, blend, special;
  - world and view-space origin;
  - radius and falloff;
  - projection planes/matrix for projected lights;
  - cube/projection/falloff image handles and sampler state;
  - shader color/register evaluation;
  - scissor, depth range, portal/PVS visibility;
  - shadow descriptor id and fallback policy.
- Fix cluster lookup space: store linear view-space depth parameters and reconstruct view-space position/depth consistently in deferred and forward+ shaders.
- Upload per-view cluster grids, not a single first grid. Every scene/subview gets its own grid id and draw commands reference the correct grid.
- Replace fixed cluster arrays with:
  - GL 3.3 CPU-binned UBO batches with explicit capacity and high-quality fallback to legacy lighting when capacity is exceeded;
  - GL 4.3+ SSBO variable-length light lists using per-cluster offsets/counts;
  - GL 4.5 persistent mapped updates and multi-bind for light/list buffers.
- Remove "spill counted but unsampled." Spill lists must either be sampled or force a visible fallback for the affected cluster/light.
- Evaluate point/projected attenuation and specular in shaders using actual light data, not fixed direction/scale.

Acceptance:

- Cluster debug shows no ordinary SP/MP scene losing lights at baseline.
- GL 3.3 and GL 4.3 cluster lists match in validation.
- Deferred/forward+ light output matches ARB2 captures for point and projected lights before shadows are enabled.

Round 3 Phase 4 status:

- Added a shared modern light descriptor contract for point, projected, fog, ambient, blend, and special lights. Cluster records now carry world/view origin, radius, evaluated color, falloff scale, view-space projection planes, projection/falloff/cube image handles, sampler state, scissor/depth policy, PVS visibility, and shadow descriptor/fallback metadata for the lighting, forward, deferred, and shadow consumers.
- Cluster uploads now cover every per-view grid instead of only the first grid. Each grid owns an index-record range, draw consumers bind the grid matching their `viewDef`, and the params UBO updates only when the bound grid changes.
- The GL 3.3 path remains explicit CPU-binned UBO batches with capacity accounting, while GL 4.3+ uses SSBO light/index records. GL 4.4/4.5-capable drivers use DSA buffer updates and the renderer state-cache multi-bind path for the light/list buffers where available.
- Cluster spill and capacity loss are now visible correctness states: unsampled spill references mark the frame lossy, deferred/forward+/visible ownership fail closed when light contribution would be dropped, and metrics report lossy clusters/references, uploaded index records, grid switches, and bind failures.
- Deferred and forward+ clustered shaders now use reconstructed view-space depth for Z-slice lookup and evaluate point/projected light position, radius attenuation, projection masks, diffuse, and specular terms from the uploaded light records instead of fixed placeholder direction/scale math.
- Validation: `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, and a targeted Phase 4 self-test launch covering clustered lighting, deferred resolve, forward+, and modern visible frame paths. Full safe validation matrix is required before commit.

### Phase 5: Production Shadow Mapping

Goal: shadows become real lighting data, not metadata.

- Promote the existing ARB2 shadow-map implementation into shared shadow services where possible instead of re-creating divergent rules.
- Build real modern shadow resources:
  - projected-light atlas tiles;
  - point-light cube faces or atlas-compatible six-face layout;
  - CSM cascade atlas with stable split/fitting policy;
  - optional translucent moment resources gated behind budget and material support.
- Extend shadow descriptors with matrix, atlas rect, face/cascade id, depth format, compare mode, bias model, PCF kernel, update frame, caster count, receiver scissor, and fallback reason.
- Render casters from light-space pass packets with correct material cutout semantics, texture matrices, alpha thresholds, two-sided/cull state, polygon offset, and dynamic/static update policy.
- Implement receiver sampling in deferred and forward+ shaders with guard rails for invalid coordinates, bad `w`, stale ids, out-of-atlas taps, and unsupported translucent paths.
- Keep stencil fallback per light. A fallback light must not be unshadowed and must not double-shadow a modern receiver.
- Add hard debug modes for atlas depth, projected UV, projected depth, projected W, face/cascade id, invalid-coordinate mask, bias off, PCF off, caster offset off, and receiver-plane bias off.

Acceptance:

- `r_useShadowMap 1` produces correct projected and point shadows in gameplay.
- Cutout grates/fences cast cutout shadows, not solid blobs.
- CSM is stable under camera motion and falls back per light on invalid projection.
- RenderDoc shows valid named shadow resources and receiver sampling state.

Round 3 Phase 5 status:

- Extended the modern shadow descriptor contract with per-light matrix rows, tile atlas rects, cascade split depths, depth/compare/bias metadata, PCF kernel, update frame, caster/receiver totals, receiver scissor, and readiness flags for atlas tiles, caster passes, cutout casters, receiver guards, stable cascades, and modern receiver sampling.
- Shadow atlas budgeting now accounts for physical tiles instead of one slot per light. Projected lights, point lights, and CSM cascades consume the number of atlas tiles they actually need, so selection pressure matches the planned render cost.
- Clustered deferred and forward+ now fail closed when the shadow plan maps a light but the modern receiver sampler is not production-ready. Those lights are surfaced as per-light stencil fallbacks with `receiver-sampling-unavailable`, preventing unshadowed modern lighting and preventing double-shadow ownership handoff.
- ARB2 shadow-map diagnostics gained hard validation modes for bias off, PCF off, caster polygon offset off, and receiver-plane/normal-bias off, while the existing projected receiver shader keeps invalid `w`, NaN/Inf, out-of-range depth, and out-of-atlas coordinates guarded.

### Phase 6: Advanced Conservative Frustum And Occlusion Culling

Goal: save CPU and GPU work without visible popping or quality loss.

- Preserve Quake 4 area/portal/PVS as visibility tier zero.
- Build a modern visibility packet per view with world-space bounds, object-oriented bounds where available, screen-space bounds, material opacity class, static/dynamic id, previous visibility state, and shadow-caster participation.
- Add SIMD CPU frustum culling for GL 3.3:
  - SoA plane tests over bounds;
  - portal/scissor clipping before draw packet emission;
  - conservative near-plane and mirrored-view handling;
  - no culling of surfaces whose bounds are invalid or whose material/fallback status requires legacy handling.
- Replace the current Hi-Z mip generation with an explicit max-depth pyramid pass:
  - no `glGetIntegerv` in-frame;
  - compute path on GL 4.3+;
  - fullscreen reduction fallback where compute is unavailable;
  - correct handling of normal versus reversed depth;
  - dilated conservative bounds to avoid false occlusion.
- Implement GPU occlusion culling on GL 4.3+:
  - project bounding boxes to screen;
  - choose Hi-Z mip from screen-space extent;
  - reject only when conservatively behind pyramid depth;
  - compact visible draw commands into indirect buffers with prefix sums or append counters;
  - keep readback asynchronous and diagnostic-only.
- Add temporal policy:
  - never require same-frame CPU readback;
  - use previous-frame occlusion only with hysteresis and forced re-test intervals;
  - keep dynamic, animated, alpha-tested, and near-camera objects conservative;
  - expose an instant `r_rendererOcclusion 0` kill switch.
- Add shadow-specific culling:
  - main-camera occlusion cannot cull a caster if it can affect a visible receiver;
  - cull shadow casters against light frusta, receiver scissor, portal/PVS influence, cached static caster sets, and optional light-space Hi-Z;
  - throttle shadow map updates for stable static lights without changing image quality.

Acceptance:

- Dense scenes show reduced CPU draw preparation, GPU draws, triangles, and pass time versus ARB2 or modern-with-culling-off.
- No visual popping in camera sweeps, doors/portals, dynamic characters, projectiles, cutouts, or shadow casters.
- Hi-Z rejection counts are nonzero in dense scenes and correspond to saved draw/triangle work.

Round 3 Phase 6 implementation notes:

- Added a cached conservative visibility packet to each modern submit command. The packet carries projected screen bounds, depth range, frustum eligibility/rejection, near-plane fallback state, static/dynamic safety, Hi-Z candidacy, and shadow-caster participation. Invalid bounds, near-plane intersections, subviews, viewmodels, dynamic/deformed/skinned geometry, non-opaque materials, and legacy/fallback-sensitive paths remain fail-open.
- Reused those packets in the executor for CPU frustum decisions, offscreen-scissor rejection, screen-space scissor tightening before draw submission, Hi-Z candidate accounting, and shadow-caster safety metrics. Main-camera frustum/screen culling is explicitly skipped for shadow-map and stencil-shadow caster commands.
- Replaced the previous `glGenerateMipmap` Hi-Z build with an explicit max-depth reduction program and executor-owned mip FBO. The build path no longer performs in-frame `glGetIntegerv` state snapshots; it uses the shared GL state cache, copies scene depth into Hi-Z level zero, then reduces each following mip by writing conservative maximum depth.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, `git diff --check`, and a staged client self-test run covering `rendererModernGLExecutorSelfTest`, `rendererModernVisibilitySelfTest`, `rendererVisiblePathSelfTest`, `rendererGpuDrivenSelfTest`, and `gfxInfo`. The executor self-test now reports `hizReduce=1`, visibility reports scissor/frustum/screen rejections with no query stall, and GPU-driven validation reports zero CPU/GPU mismatches. A bounded SP `game/airdefense1` smoke also passed through `tools/tests/renderer_gameplay_benchmark.py --profile smoke --settle-frames 5 --sample-frames 5 --timeout 300 --limit 1 --output-dir .tmp/renderer-gameplay/phase6-smoke-short`.
- Dense-scene performance claims and nonzero shader-side Hi-Z rejection remain acceptance-gated on gameplay captures; this phase makes the visibility packet, conservative CPU rejection, Hi-Z resource build, and no-stall diagnostics fit for that measured follow-up without trading away rendering quality.

### Phase 7: Fit-For-Purpose Pipelines

Goal: execute the minimum correct passes with low driver overhead.

- Use a stable pipeline layout:
  - depth prepass for promoted opaque/perforated depth;
  - G-buffer for opaque/perforated material data;
  - tiled/clustered deferred resolve for opaque lighting;
  - forward+ for alpha-tested exceptions, viewmodels, decals, transparent, fog/blend, particles, and BSE once supported;
  - HDR scene composition once;
  - post stack and GUI overlay in the established order.
- Split shader permutations by real material/light needs, not monolithic debug variants.
- Batch by pipeline, material, geometry buffer, texture/sampler set, and scissor while preserving required sort order.
- Use persistent mapped ring buffers for per-frame constants, draw records, light records, and shadow descriptors on GL 4.5; use orphaned/map-range buffers on GL 3.3/4.1.
- Cache FBOs and avoid per-frame attachment churn where possible.
- Remove in-frame state queries from hot paths.
- Use DSA, multi-bind, sampler objects, and named debug objects on capable tiers.
- Keep GPU timers nonblocking and make validation readbacks opt-in only.

Acceptance:

- GL 3.3 has a bounded CPU path with no surprise compute dependency.
- GL 4.3 performs real GPU culling and indirect generation.
- GL 4.5 reduces CPU submit overhead measurably through persistent buffers, DSA, and multi-bind.

Round 3 Phase 7 implementation notes:

- Added a per-frame modern pipeline policy that derives G-buffer, deferred, and forward+ demand from actual submit-plan work plus explicit debug-overlay requests. Empty requested passes are skipped before resource-heavy work, while modern visible ownership can still compose from the single completed lighting source when only deferred or forward+ is genuinely needed.
- Extended draw-plan and executor stats with pipeline command counts, pipeline/material/geometry/texture/scissor batch transitions, transparent sort-order validation, skipped-pass counts, FBO cache hits, attachment updates, and tier-fit booleans for GL 3.3 CPU, GL 4.3 GPU-driven, and GL 4.5 low-overhead execution.
- Cached repeated G-buffer MRT and forward+ framebuffer attachment layouts, invalidating the cache on resource changes or incomplete FBOs. The low-overhead self-test now proves the second identical G-buffer bind hits the cache instead of reattaching the full layout.
- Removed framebuffer readback queries from the visible composition handoff. Composition now targets the known current render texture or default framebuffer through the shared state-cache handoff and keeps validation readbacks opt-in through `r_rendererGpuValidation`.
- `gfxInfo`, detailed renderer metrics, `rendererModernVisibleSelfTest`, and `rendererLowOverheadSelfTest` now report the Phase 7 pipeline contract so GL 3.3, GL 4.3, and GL 4.5 paths can be checked without assuming one monolithic pass stack.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, `git diff --check`, a staged client self-test run covering draw-plan, submit-plan, executor, deferred, forward+, modern-visible, low-overhead, visibility, and GPU-driven, plus a bounded SP `game/airdefense1` smoke through `tools/tests/renderer_gameplay_benchmark.py --profile smoke --settle-frames 5 --sample-frames 5 --timeout 300 --limit 1 --output-dir .tmp/renderer-gameplay/phase7-smoke-short` whose `gfxInfo` output included the `Modern GL pipeline` contract.

### Phase 8: Validation And Promotion Gates

Goal: make "complete" mean visible and fast in gameplay.

- Add deterministic visual tests for:
  - character seams and viewmodels;
  - common wall/metal/armor/weapon/glass/decal materials;
  - point and projected lights;
  - cutout and dynamic shadows;
  - CSM camera sweeps;
  - fog/blend lights;
  - post/HDR/SSAO/bloom;
  - GUI and subviews;
  - BSE-heavy scenes.
- Run required gameplay benchmarks for `game/airdefense1`, `game/airdefense2`, `game/storage2`, `game/medlabs`, `game/mcc_landing`, and `mp/q4dm1`.
- Capture one RenderDoc frame per GL tier with named passes/resources and verify resource contents.
- Gate promotion on:
  - zero renderer warnings;
  - no unresolved modern-visible fallback that drops visual contribution;
  - ARB2-or-better P95/P99 frame time in target scenes;
  - high-refresh presentation still uncapped with 60 Hz simulation;
  - rollback commands working after modern visible frames.

Acceptance:

- `r_rendererModernAutoPromote 1` is allowed only after the above evidence exists.
- Default remains ARB2-safe until target-hardware gameplay proves modern parity and performance.

Round 3 Phase 8 implementation notes:

- Added `r_rendererPromotionEvidence` as a separate Phase 8 evidence token required before `r_rendererModernAutoPromote 1` can activate automatic modern-visible promotion. The required token is `phase8=complete;warnings=0;visual=pass;gameplay=pass;renderdoc=pass;perf=arb2-or-better;presentation=pass;rollback=pass;debug=off`.
- Extended the default-promotion state with explicit evidence booleans for warning-free logs, deterministic visual coverage, required SP/MP gameplay, RenderDoc tier captures, ARB2-or-better performance, high-refresh presentation, rollback, and debug-off validation. `gfxInfo` now reports the evidence coverage and missing fields, and promotion fails closed with `validation-evidence-required` until every token is present.
- Updated `rendererDefaultPromotionSelfTest` to cover missing evidence, incomplete evidence, evidence-ready unsigned promotion, signed promotion, explicit ARB2 escape, forced legacy tier, compatibility-gate block, and missing ARB2 rollback.
- Updated the validation matrix report schema and Markdown output with a promotion-evidence gate section so safe automated results, manual gameplay/capture evidence, and the engine cvar contract all point at the same token.
- `rendererDefaultSafetySelfTest` now treats a non-empty promotion-evidence token as non-conservative on clean startup, keeping default ARB2 startup free of both the auto-promotion switch and stale release evidence.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `python -m py_compile tools/tests/renderer_validation_matrix.py tools/tests/renderer_gameplay_benchmark.py`, `python tools/tests/renderer_validation_matrix.py --list`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, a single staged-client run of `rendererDefaultPromotionSelfTest`, `rendererDefaultSafetySelfTest`, and `gfxInfo`, and a no-launch validation-report schema smoke under `.tmp/renderer-validation/phase8-report-schema-smoke`. The focused self-test log reports `RendererDefaultPromotion self-test passed (cases=8)`, `RendererDefaultSafety self-test passed`, default safety with `promotionEvidence=0`, and no renderer fatal/error/idStr/shader-link signatures.

## Recommended Fix Order

1. Capture the failing scene and isolate the first FPS cliff.
2. Fix scene packet semantics so the modern path stops multiplying work across fake passes.
3. Harden pass ownership so legacy lighting/shadows are never skipped by incomplete modern replacements.
4. Replace placeholder light/shadow shader math with real Quake 4 light descriptors and material evaluation.
5. Fix G-buffer/depth ownership, culling state, and character/material parity.
6. Implement real projected/point shadow resources and receiver sampling with per-light fallback.
7. Replace cluster spill/first-grid limitations with per-view variable-length light lists.
8. Implement conservative Hi-Z/frustum/occlusion culling and real GL 4.3 indirect compaction.
9. Optimize pipeline state, buffers, FBOs, and batching after visual correctness is proven.
10. Re-run full gameplay, RenderDoc, and benchmark gates before any default promotion.
