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
