# Renderer Recovery And Performance Parity Plan

## Purpose

This plan replaces the completed checklist in `docs-dev/plans/2026-05-13-clustered-hybrid-gl-renderer.md` as the active recovery plan for the modern OpenGL renderer. The previous plan produced useful renderer scaffolding, but the current implementation is not yet a gameplay-ready performance renderer: a quick in-map run shows extremely low frame rate, repeated `idStr::snPrintf` overflow warnings, and visible rendering defects such as character model mirror stitching.

The goal is to turn the current side-path implementation into a high-performance clustered hybrid deferred/forward+ renderer while retaining the existing uncapped framerate work, keeping stock Quake 4 asset compatibility, and preserving ARB2 as the rollback path until the modern path proves parity and speed in real gameplay.

## Non-Negotiable Requirements

- Preserve the previously implemented uncapped framerate behavior. Rendering changes must not reintroduce a `60 Hz` presentation cap, stall repeated-state presentation frames, or alter the authoritative `60 Hz` simulation cadence.
- The renderer must improve performance on multiple hardware generations, not only expose modern features on high-end GL 4.5/4.6 systems.
- The target renderer is OpenGL-only and tiered: GL 3.3 baseline, GL 4.1 macOS-class path, GL 4.3 GPU-driven path, GL 4.5 low-overhead path, GL 4.6 optional experiments.
- The final visible path is a modern clustered hybrid deferred/forward+ pipeline, not a duplicate side renderer running before the legacy renderer.
- CPU work is allowed where it is measurably cheaper or necessary for GL 3.3 support, but it must be bounded, parallel-friendly, and observable.
- HDR, bloom, SSAO, motion blur, lens flare, GUI composition, subviews, render-to-texture, BSE effects, light grids, shadows, and legacy material behavior must be integrated or explicitly owned by fallback passes until parity is proven.
- Shadow mapping is a first-class light subsystem, not a bolt-on debug path. Projected-light maps, point-light maps, CSM, hashed-alpha cutout casters, optional translucent moments, stencil-shadow fallback, shadow budgets, and shadow diagnostics must be part of the renderer's pass ownership, clustering, material, tiering, and validation contracts.
- The solution must continue to work with shipped Quake 4 assets and repo-authored `content/baseoq4/` overrides only. Do not hide renderer gaps with replacement asset content.

## Inspection Summary

The completed plan is internally inconsistent with its own definition of done:

- The implementation summary marks Phases 15 to 17 complete, but the definition of done still leaves GL 3.3 visible rendering, GL 4.1 parity, GL 4.3 GPU-driven operation, stock SP/MP gameplay validation, RenderDoc capture validation, and release notes unchecked.
- Phase 16 explicitly states that it added benchmark capture plumbing but "does not claim a real gameplay performance win." The safe validation report also confirms automated checks are startup/self-test only and do not enter gameplay.
- `RB_ExecuteBackEndCommands` calls `R_ModernGLExecutor_PrepareFrame` before the legacy ARB2 command stream still runs, then calls `R_ModernGLExecutor_ComposeVisibleFrame` before swap (`src/renderer/tr_backend.cpp:735`, `src/renderer/tr_backend.cpp:794`). This can turn opt-in modern rendering into extra work on top of ARB2 rather than a replacement path.
- `R_ModernGLExecutor_PrepareFrame` can execute visible depth, G-buffer, deferred resolve, forward+, GPU-driven validation, and diagnostic submission before pass ownership is proven (`src/renderer/ModernGLExecutor.cpp:2948`, `src/renderer/ModernGLExecutor.cpp:2994`). If visible composition is blocked by legacy-owned post/subview/BSE/GUI work, the expensive side passes may still have already run.
- Modern visible auto-promotion and explicit `r_rendererModernVisible` are not represented consistently. `PrepareFrame` computes a local auto-promoted `modernVisibleRequested`, while stats and ownership analysis still derive `stats.modernVisibleRequested` directly from `r_rendererModernVisible` in multiple places (`src/renderer/ModernGLExecutor.cpp:990`, `src/renderer/ModernGLExecutor.cpp:2949`). This can make diagnostics and execution disagree.
- The current shader library is not a final material implementation. G-buffer normals are constant, deferred lighting uses placeholder direction/attenuation logic, forward+ uses simplified lighting, and real bump/specular/material stage evaluation is not implemented (`src/renderer/ModernGLShaderLibrary.cpp:249`, `src/renderer/ModernGLShaderLibrary.cpp:340`).
- Clustered lighting has fixed low ceilings: 128 lights, 8x6x16 clusters, and 4 lights per cluster (`src/renderer/ModernClusteredLighting.cpp:12`, `src/renderer/ModernClusteredLighting.cpp:16`). This guarantees overflow pressure in real scenes instead of scaling.
- Shadow mapping currently lives mainly inside the ARB2 path as a GLSL island (`src/renderer/draw_arb2.cpp`), with projected lights, point lights, optional CSM, hashed-alpha cutout casters, and experimental translucent moments documented in `docs-user/shadow-mapping.md`. The modern renderer plan has to promote that work into shared light/caster/resource records instead of letting the new deferred/forward+ path bypass or duplicate it.
- Existing shadow documentation and technical review call out the right quality goals and risks: fallback to legacy shadows when shadow maps fail, CSM stabilization and guard bands, cutout material fidelity, translucent-moment cost/limits, and structural artifact risks such as peter-panning or scattered projected shadows that cannot be solved by CVar tuning alone. These become renderer acceptance gates, not optional polish.
- The active log contains tens of thousands of `idStr::snPrintf: overflow ... in 96` warnings. The primary renderer-side suspect is per-frame debug-name formatting into `materialResourceTextureBinding_t::debugName[96]` from full material image names (`src/renderer/MaterialResourceTable.h:105`, `src/renderer/MaterialResourceTable.cpp:351`). The ARB2 shadow-map overlay also has 96-byte debug lines that should be audited (`src/renderer/draw_arb2.cpp:6870`).

## Outstanding Work

| Area | Outstanding work | Current risk |
|---|---|---|
| Runtime performance | Stop running modern side passes before proving they will replace legacy ownership for that frame. | Modern opt-in can double-render or worse, explaining very low FPS. |
| Warning hygiene | Eliminate per-frame `snPrintf` overflows and add source attribution for any future overflow warning. | Console/log spam can dominate debugging and may contribute CPU overhead. |
| Pass ownership | Create a real pass-owner scheduler so modern and legacy paths do not both draw the same work. | The current bridge is observable but not a replacement renderer. |
| Material parity | Implement stock material stage evaluation, alpha test, blend modes, texture matrices, shader registers, normal/specular maps, and fallback boundaries. | Characters and world materials cannot match ARB2 yet. |
| Geometry parity | Bind full vertex attributes, correct normal/tangent basis, skinning/deform handling, culling, mirrored transforms, and viewmodel rules. | Model seams, mirror stitching, and lighting errors are expected. |
| Cluster scalability | Replace fixed tiny cluster/list caps with tier-appropriate dynamic buffers and overflow-safe algorithms. | Overflow warnings and incorrect lighting pressure persist in normal scenes. |
| Shadow mapping | Promote projected/point/CSM shadow maps into the modern light pipeline with correct caster/material semantics, atlas ownership, budgets, and fallback. | Modern lighting can become fast but visually wrong, or shadow maps can dominate frame time. |
| Post/HDR integration | Route modern scene color through the existing HDR, tone mapping, bloom, SSAO, motion blur, lens flare, and GUI pipeline. | Modern visible composition is not the shipped visual pipeline. |
| Subviews/BSE/render demos | Promote or explicitly isolate subviews, remote cameras, copy-render, BSE, and render-demo paths. | Modern visible is blocked or visually incomplete in real content. |
| GL tier policy | Make each tier a coherent implementation path, not just a capability label. | GL 3.3 and GL 4.1 cannot be trusted as performance baselines yet. |
| Validation | Add gameplay, image comparison, RenderDoc, and benchmark sign-off before any default promotion. | Startup self-tests are passing while in-game rendering is slow and wrong. |

## Phase 0: Stabilize Defaults And Reproduce

Goal: get a clean, repeatable baseline without losing the high-framerate work.

- [ ] Confirm defaults leave all modern visible/side-path cvars off unless a targeted test enables them: `r_rendererModernVisible`, `r_rendererModernVisibleDepth`, `r_rendererModernOpaque`, `r_rendererModernDeferred`, `r_rendererForwardPlus`, `r_rendererModernSubmit`, `r_rendererGpuValidation`, `r_rendererClusterDebug`.
- [ ] Capture the user's failing command line, cvar state, map, resolution, driver, selected GL tier, and whether `r_rendererModernAutoPromote` is enabled.
- [ ] Run the SP launch task into the same gameplay scene for at least 10 seconds, not just the menu, with `r_swapInterval 0` and both `com_maxfps 0` and `com_maxfps 240`.
- [ ] Run an ARB2 baseline with modern side paths disabled and record P50/P95/P99, GPU timings, draw counts, warnings, and screenshots.
- [ ] Capture shadow baselines with `r_shadows 1`, `r_useShadowMap 0`, `r_useShadowMap 1`, `r_shadowMapCSM 0/1`, and `r_shadowMapTranslucentMoments 0/1` where supported. Record shadow-map pass time, mapped/fallback light counts, cascade count, atlas size, caster counts, and screenshots.
- [ ] Run the same scene with each modern cvar enabled one at a time to identify the first severe FPS cliff.
- [ ] Preserve high-refresh presentation acceptance from `docs-dev/high-framerate-rendering-plan.md`: presentation can exceed `60 Hz`, simulation remains `60 Hz`, modal/menu/cinematic paths still behave.

Acceptance:

- [ ] A reproducible `.tmp/renderer-recovery/` capture set exists with logs, screenshots, cvars, and metrics for ARB2 and each modern side path.
- [ ] Modern defaults do not reduce ARB2 gameplay FPS by more than 1 percent and emit no new renderer warnings.

## Phase 1: Eliminate Overflow Warnings

Goal: make the log trustworthy and remove the repeated per-frame warning path.

- [x] Audit all renderer `char[96]` debug/status buffers that are written with `idStr::snPrintf`.
- [x] Fix `materialResourceTextureBinding_t::debugName[96]` so full image paths cannot warn every frame. Prefer a stable truncated copy or a wider debug-only field; do not call warning-producing formatting on long asset paths.
- [x] Audit the ARB2 shadow-map debug overlay line buffers and either widen them or format abbreviated values.
- [x] Add source-context diagnostics for renderer-side debug string overflows during development, then keep the production path quiet.
- [x] Avoid rebuilding debug strings every frame unless the data changed. Material table records can keep stable ids and optional debug names without warning-producing formatting on every frame.

Acceptance:

- [ ] The failing gameplay scene exits with zero `idStr::snPrintf: overflow` warnings.
- [ ] The warning count remains zero with `r_rendererMetrics 2`, `r_rendererClusterDebug`, and shadow-map debug overlays enabled.

Implementation note 2026-05-14:
- Replaced warning-producing renderer debug/status copies with `idStr::Copynz` or quiet `idStr::vsnPrintf` truncation in material resource records, clustered-light debug labels, modern status fields, render-graph debug labels, GL state-cache reasons, and the ARB2 shadow-map overlay.
- Material texture debug names now use short semantic labels in normal frames and only build full `semantic:image` labels for verbose renderer diagnostics, where truncations are counted with a first-source label instead of warning every frame.
- Validation: `tools/build/meson_setup.ps1 compile -C builddir` and `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects` passed. A bounded MP loading smoke with `r_rendererMetrics 2`, `r_rendererClusterDebug 1`, `r_useShadowMap 1`, and `r_shadowMapDebugOverlay 1` produced `0` `idStr::snPrintf: overflow` warnings in `.home/q4base/logs/openq4_phase1_overflow.log`, but the hidden-window run was stopped after 45 seconds before entering gameplay.

## Phase 2: Stop Paying For Blocked Modern Frames

Goal: modern opt-in work must be cheap when it cannot become visible.

- [x] Split the modern executor into explicit modes: `analyze`, `sidecar-diagnostic`, and `visible-replacement`.
- [x] Run pass-owner/fallback analysis before executing expensive graph passes.
- [x] If `r_rendererModernVisible 1` is blocked by legacy-owned post, subview, BSE, render-demo, shadow ownership, or non-ready GUI work, skip depth/G-buffer/deferred/forward+ execution unless a debug overlay or explicit sidecar cvar requested that pass.
- [x] Make auto-promotion and explicit visible requests use the same `modernVisibleRequested` state in stats, owner analysis, execution, metrics, and `gfxInfo`.
- [x] Add metrics for `wouldExecute`, `executed`, `skippedBlocked`, `skippedNoConsumer`, and `duplicatedWithLegacy` per pass.
- [x] Make `r_rendererModernSubmit` remain a masked diagnostic path only; it must not silently enable the whole visible pipeline.
- [x] Treat shadow-map passes as consumers and producers in the skip analysis. Do not render modern shadow maps if the receiver lighting pass will be legacy-owned, unless `r_shadowMapDebugOverlay`, `r_shadowMapReport`, or a shadow validation cvar explicitly requests a sidecar capture.

Acceptance:

- [ ] Enabling `r_rendererModernVisible 1` in a scene with known legacy fallbacks does not execute modern G-buffer/deferred/forward+ work unless composition can occur.
- [x] Metrics explain the skip reason in one line.
- [ ] The previous 5 FPS scene returns near the ARB2 baseline when modern composition is blocked.

Phase 2 implementation notes:

- `ModernGLExecutor` now computes one effective modern-visible request for explicit and auto-promoted frames, then chooses `analyze`, `sidecar-diagnostic`, or `visible-replacement` after compatibility ownership is known.
- Blocked modern-visible frames stay in analyze-only mode unless an explicit sidecar/debug request exists. Depth/shadow-depth, G-buffer, deferred resolve, forward+, cluster lighting, GPU-driven buffer updates, and indirect submit are no longer executed just because the executor is enabled.
- Shadow-depth work is gated through the same visible-depth producer path. `r_shadowMapDebugOverlay` and `r_shadowMapReport` request a shadow sidecar only when shadow maps and shadows are enabled; otherwise legacy-owned lighting does not cause modern shadow-map work to duplicate it.
- `modernPassGate` metrics report per-pass `would`, `exec`, `skipBlocked`, `skipNoConsumer`, and `dupLegacy` counters. `rendererModernCompatibilitySelfTest` now verifies blocked post/subview/render-demo/BSE ownership produces `skipBlocked=1/1/1/1` with no modern pass execution.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, and the compatibility self-test with zero `idStr::snPrintf` overflow warnings. A bounded SP smoke produced the new gate telemetry but was stopped while loading `game/core2`, so real in-game FPS recovery remains open.

## Phase 3: Replace Passes Instead Of Duplicating Them

Goal: when a pass is modern-owned, the legacy renderer must not also draw that same pass.

- [x] Introduce a per-frame pass-owner table with states: `legacy`, `modern`, `mixed`, `disabled`, `blocked`.
- [x] Route backend commands through the owner table. Legacy ARB2 should skip only the exact passes whose modern replacement has completed and passed resource/fallback checks.
- [x] Add a fail-closed handoff: if a modern-owned pass fails after legacy skip has been armed, restore legacy ownership before drawing the frame.
- [x] Ensure present, GUI, and post composition order is explicit. Do not clear the back buffer for modern composition if a legacy fallback frame is the real output.
- [x] Add explicit ownership for shadow-caster passes, shadow-map resources, and shadow receiver sampling. The renderer must never both sample a modern shadow map and also apply a legacy shadow/stencil contribution for the same light.
- [x] Keep `r_renderer arb2` and `r_glTier legacy` as immediate rollback paths.
- [x] Add a `rendererPassOwnershipSelfTest` that constructs mixed modern/legacy ownership and verifies no duplicate draw ownership.

Acceptance:

- [x] Modern visible frames have `duplicatedWithLegacy=0` for owned passes.
- [x] Legacy fallback frames have `modernExecuted=0` for blocked visible passes unless an explicit debug cvar requested a sidecar pass.
- [ ] ARB2 rollback renders the same frame without stale modern GL state.

Phase 3 implementation notes:

- `ModernGLExecutor` now builds a per-frame pass-ownership table after modern side/visible passes execute. Each graph pass records a state, skip eligibility, legacy-run expectation, reason string, duplicate-hazard flag, and skip accounting.
- Legacy ARB2 pass calls consult the table for precise skip points: depth fill, ARB2 interaction/shadow work, light-grid indirect, ambient/material passes, fog/blend lights, fullscreen GUI command replay, and standalone special-effect commands. Skips are armed only for `modern` ownership after resource/fallback checks pass; sidecar diagnostics remain `mixed` and do not suppress ARB2.
- Visible composition now requires the ownership table to be handoff-ready before it can clear the back buffer. If a late readiness check fails, pass ownership is fail-closed back to blocked/legacy ownership before metrics are recorded.
- Shadow ownership is explicit in the table: a completed modern shadow-map pass can replace legacy stencil-shadow contribution for that frame; otherwise shadow work remains legacy or mixed diagnostic ownership.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, `rendererPassOwnershipSelfTest`, `rendererModernCompatibilitySelfTest`, and `rendererModernVisibleSelfTest`, all with zero `idStr::snPrintf` overflow warnings in the validation logs. A bounded `game/airdefense1` smoke reached loading/map-initialization frames and confirmed graph-present ownership (`table=4`, `skipArmed=1`) without overflow warnings, but was stopped before gameplay; in-game ARB2 rollback parity still needs a real SP/MP map run.

## Phase 4: Geometry And Character Correctness

Goal: fix visible defects such as character model mirror stitching before broader promotion.

- [ ] Create a deterministic character test scene around the observed Viper Squad case, with ARB2 and modern screenshots at the same camera/FOV.
- [x] Bind and validate the full `idDrawVert` layout for modern shaders: position, texcoord, normal, tangent, bitangent sign/color as needed by Quake 4 materials.
- [x] Validate model-view/projection handling for world models, viewmodels, subviews, mirrors, remote cameras, and weapon FOV.
- [x] Implement correct normal/tangent-space reconstruction, including mirrored UV islands and negative/mirrored transforms.
- [x] Audit front-face/cull mode handling for mirrored views and models; handle cull inversion where the legacy path does.
- [x] Decide the modern skinning contract per tier: CPU-skinned geometry as baseline, GPU palette skinning only when matrix palette data is real and validated.
- [x] Keep deform surfaces, turbulent materials, and unsupported GPU palette cases on legacy until their modern path is correct.
- [ ] Add screenshot comparison gates for character seams, viewmodel alignment, alpha-tested armor decals, and dynamic model shadows.

Acceptance:

- [ ] The Viper Squad mirror-stitching artifact is gone or explicitly isolated to a known legacy/content issue.
- [ ] Modern and ARB2 character captures match within documented tolerances for diffuse, normal/specular lighting, silhouette, and seams.

Phase 4 implementation note:

- Modern draw submission now binds the complete `idDrawVert` contract on the direct and GPU-driven indirect paths: position, color, primary UV, tangent, bitangent, and normal.
- G-buffer and clustered forward shaders now receive per-draw model-view matrices and reconstruct view-space normals/tangent handedness instead of writing constant normals.
- Shader reflection and executor self-tests now validate the model-view uniform, tangent-space attribute contract, and `idDrawVert` offsets. Deformed and GPU-palette-skinned surfaces remain explicit legacy fallbacks until those contracts are implemented correctly.
- The promoted modern passes keep conservative culling disabled while material cull parity is incomplete, avoiding mirror/subview cull inversions until the material-state phase owns two-sided and inverted-cull decisions.
- Remaining Phase 4 validation work is visual: the Viper Squad repro and screenshot comparisons still need an authored deterministic scene/capture harness before the acceptance boxes can be closed.

## Phase 5: Real Material Evaluation

Goal: replace placeholder shader output with stock-material-compatible rendering.

- [x] Expand `MaterialResourceTable` from first semantic texture lookup into a stage program contract: conditions, color registers, alpha test registers, texture matrices, texgen, blend/depth state, polygon offset, and sort behavior.
- [ ] Implement GLSL material evaluation for the conservative stock subset first: bump, diffuse, specular, additive, filter, alpha-test, and emissive/light-grid stages.
- [x] Implement a matching shadow-caster material contract for promoted materials: alpha-test registers, hashed-alpha coverage, texture matrices, vertex color influence, polygon offset, two-sided/cull behavior, and unsupported-stage fallback must match the visible material path.
- [x] Keep `newStage`, dynamic image, `_currentRender`, screen texgen, and complex post stages as explicit fallbacks until promoted.
- [x] Add normal/specular map support to G-buffer and forward+ programs.
- [ ] Preserve gamma/HDR correctness: texture sampling, linear lighting, HDR scene output, tone mapping, and GUI LDR overlay must be ordered correctly.
- [ ] Add material parity captures for common SP/MP wall, armor, weapon, glass, decal, GUI, and animated material cases.

Acceptance:

- [x] The modern G-buffer no longer writes constant normals for promoted materials.
- [ ] Promoted opaque/perforated materials match ARB2 captures closely enough to survive gameplay validation.

Phase 5 implementation notes:

- `MaterialResourceTable` now records a real promoted-material contract instead of only the first usable texture: evaluated stage counts, additive/filter/blend participation, condition registers, alpha-test registers, texture matrices, texgen, vertex-color use, polygon offset, depth/color-mask state, lighting/shadow flags, and conservative fallback reasons.
- The shadow-caster contract is now derived beside the visible material contract. Promoted casters carry alpha-test threshold/binding data and reject materials whose texture matrix, vertex color, polygon offset, unsupported texgen, or unsupported stage behavior would make cutout shadow coverage diverge from visible rendering.
- `_currentRender`, screen texgen, dynamic image/new-stage style effects, additive/filter stages, texture matrices, vertex-color-dependent stages, and polygon-offset-sensitive materials remain explicit fallbacks until their shader semantics are owned by the modern path.
- G-buffer, clustered forward+, and transparent forward programs now bind diffuse, normal, specular, and emissive material textures, sample alpha-test thresholds from legacy shader registers, reconstruct view-space normals from the Phase 4 tangent basis, and use specular/emissive contribution instead of placeholder constant output for promoted draws.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, `rendererShaderLibrarySelfTest`, `rendererMaterialResourceTableSelfTest`, `rendererGBufferSelfTest`, and `rendererForwardPlusSelfTest`. The Phase 5 validation logs contained no renderer `idStr::snPrintf` overflow, shader compile, or program link failures. Visual material parity captures, full HDR/post ordering, and additive/filter material promotion remain open acceptance work.

## Phase 6: Scalable Clustered Lighting

Goal: replace the fixed tiny cluster prototype with a production clustered-light system.

- [x] Replace the fixed `8x6x16` and `4 lights per cluster` limits with tier-specific dynamic allocation.
- [x] GL 3.3 path: use CPU binning and upload through UBOs sized to respect conservative uniform-block limits; cache-friendly job fan-out remains a later CPU scaling pass.
- [ ] GL 4.1 path: keep the GL 3.3 algorithm with better buffer/update APIs where available, no compute dependency.
- [ ] GL 4.3+ path: use SSBOs, compute binning, prefix sums or append buffers, and indirect-friendly visible light lists.
- [ ] GL 4.5+ path: persistent-mapped updates, DSA, multi-bind, and zero-stall fence retirement.
- [x] Add overflow spill lists instead of hard failure; overflow must degrade quality predictably and report exact pressure.
- [ ] Bin per view/subview and respect scissor/portal/PVS visibility. Do not use one main-view cluster grid for all subviews.
- [ ] Integrate projected lights, point lights, fog/blend lights, ambient lights, shadow eligibility, shadow-map descriptor handles, stencil fallback state, and light-grid contributions with real material passes.
- [x] Add budget controls by preset, but make adaptive cluster sizing respond to measured light density and resolution rather than static presets only.
- [ ] Feed shadow priority into clustering and light lists: visible influence, scissor area, distance, caster count, update frequency, and shadow-map pixel cost should determine which lights get mapped at a given quality budget.

Acceptance:

- [ ] No ordinary SP/MP validation scene hits cluster overflow at baseline settings.
- [ ] Stress scenes report bounded overflow degradation without warnings or catastrophic FPS loss.
- [ ] GL 3.3 CPU binning and GL 4.3 compute binning produce matching light and shadow-eligibility lists in validation.

Phase 6 implementation notes:

- `ModernClusteredLighting` now builds dynamic per-scene cluster grids from benchmark budgets and measured viewport/light pressure instead of the old fixed `8x6x16`/four-light prototype. The default baseline path now uses the budgeted `6x4x12` grid and raises/reduces dimensions under `r_rendererAdaptiveClusterGrid` while fitting the selected upload path.
- Cluster references now have a tiered capacity model: GL 3.3/4.1 use UBO-safe light/index buffers with a larger per-cluster budget, while GL 4.3+ uploads CPU-binned light and cluster-index records through SSBOs for larger visible light lists. Deferred and forward+ GLSL now fetch packed cluster indices through UBO or SSBO helpers from the same shader source.
- Dense clusters no longer become immediate hard overflows. References beyond the shader-visible per-cluster budget are counted in spill lists with exact spill-cluster/reference metrics, and only spill-list exhaustion becomes a hard overflow.
- Clustered shader helpers now bound-check both UBO and SSBO fetch paths. Out-of-range light or index reads collapse to empty records instead of depending on driver behavior under clipped/scissor edge cases.
- `rendererClusterGridSelfTest` now validates the no-overflow six-light baseline case, upload accounting, GL 3.3 UBO fallback behavior, GL 4.3+ SSBO behavior, monotonic depth slicing, and a dense-light spill case that reports spill pressure without hard overflow. `rendererForwardPlusSelfTest` now uses upload-manager-backed vertex/index buffers and explicit identity transforms instead of placeholder cache handles.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, the GL 4.5/SSBO run of `rendererShaderLibrarySelfTest`, `rendererClusterGridSelfTest`, and `rendererForwardPlusSelfTest`, a separate GL 4.5 `r_rendererModernDeferred 1` run of `rendererDeferredResolveSelfTest`, and a forced `r_glTier gl33` run of `rendererShaderLibrarySelfTest` plus `rendererClusterGridSelfTest`. The Phase 6 validation logs contained no renderer `idStr::snPrintf` overflow, shader compile, or program link failures. Compute binning, shadow descriptor prioritization, and full per-subview shader consumption remain Phase 6/7 follow-up work.

## Phase 7: Shadow Mapping Productionization And Renderer Integration

Goal: make shadow mapping correct, budgeted, and integral to the modern renderer instead of leaving it as a separate ARB2-era island.

- [x] Create a shared per-frame shadow plan from the clustered light records. Each shadowed light must carry type, priority, update policy, caster lists, receiver policy, fallback reason, and estimated GPU cost.
- [x] Move shadow-map resources into the render graph: projected atlases, point-light face atlases or cubemap-compatible layouts, cascade tiles, depth formats, translucent moment MRTs, resolve targets, and debug overlays must have named lifetime/resource ownership.
- [x] Preserve the existing user-facing contract from `docs-user/shadow-mapping.md`: `r_shadows` remains the master switch, `r_useShadowMap` enables supported mapped lights, unavailable or failed shadow maps fall back to legacy shadows, and unsupported translucent moments fail closed.
- [x] Keep stencil shadows as a real fallback path for lights/materials that cannot be safely mapped. Fallback must be per light and counted; it must not leave the light unshadowed and must not double-shadow a modern receiver.
- [ ] Promote projected-light and point-light shadow maps first, then CSM, then optional translucent moments. Do not block base shadow-map correctness on the experimental translucent path.
- [ ] Make shadow-map correctness diagnostics authoritative: atlas/depth view, cascade index, projected UV, projected depth, projected W, invalid-coordinate mask, face/tile labels, and selected light metadata must be usable during gameplay and RenderDoc capture.
- [ ] Add a hard shadow-validation mode that can disable caster polygon offset, receiver bias, PCF, and cascade blending independently. If shadows still detach or scatter in that mode, the bug is structural and must be investigated as projection/resource/state corruption.
- [ ] Guard receiver sampling against invalid projective coordinates. NaN/Inf, bad `w`, out-of-atlas taps, stale tile ids, and wrong face/cascade ids should become lit or debug-colored, never undefined sampling.
- [ ] Validate GL state around every shadow pass: FBO, viewport, scissor, depth/stencil state, color mask, active texture unit, sampler compare state, bound shadow texture, and restored state on legacy handoff.
- [ ] Harden CSM: per-light CSM eligibility, stable texel snapping, split/fitting policy, PCF guard bands, cascade tile padding/clamping, cascade blend correctness, and automatic fallback to single-map or stencil when projection validity fails.
- [ ] Redesign bias/filtering as a predictable model: caster polygon offset is small and bounded; receiver constant/normal bias scales per light/face/cascade; optional receiver-plane depth bias is available for large PCF kernels on tiers where it pays for itself.
- [ ] Finalize cutout caster semantics. Hashed alpha stays the default for perforated materials, but texture coordinate transforms, alpha-test thresholds, stage color, vertex color, and culling must match visible material evaluation.
- [ ] Keep translucent moments explicitly optional and budget-gated. Either align the reconstruction with a documented moment-shadow-map model and add leakage controls, or keep it labelled experimental with material participation gates and clear metrics.
- [ ] Add a shadow budget system: maximum mapped lights, maximum shadow-map pixels per frame, CSM cascade downgrade, point-light face downgrade, translucent-overlay disable, resolution scaling, and update throttling for static or low-influence lights.
- [ ] Cache and reuse shadow maps when safe: static lights with stable caster sets, unchanged point faces, unchanged projected atlas tiles, and low-motion cascades should avoid redundant rendering without causing temporal artifacts.
- [ ] Expose metrics for mapped lights, fallback lights, skipped lights, per-light caster counts, shadow draw count, shadow pixels, atlas occupancy, update throttles, shadow GPU time, translucent pass cost, CSM invalidation, bias mode, and debug overlay state.
- [ ] Integrate modern shadow descriptors into deferred and forward+ shaders. A light list entry must provide all data needed to sample or intentionally skip shadowing: map type, matrix, atlas rect, face/cascade, compare/filter mode, bias, translucent overlay handle, and fallback state.
- [ ] Ensure shadow sampling happens in the correct lighting space and output stage: deferred opaque, forward+ alpha/transparent/viewmodel, light-grid contribution, fog/blend exclusions, and HDR composition must agree on whether the light is shadowed.

Acceptance:

- [ ] `r_useShadowMap 1` produces projected and point-light shadows that match or improve ARB2 captures in required SP/MP gameplay scenes without peter-panning, scattered projection artifacts, stale atlas sampling, or missing fallback shadows.
- [ ] `r_shadowMapCSM 1` is stable under camera motion, has no visible cascade tile leakage, and falls back per light when projection validity fails.
- [ ] Cutout casters such as grates/fences cast cutout shadows, not solid blobs, and remain stable at distance.
- [ ] Translucent moments are either correct enough for documented supported materials or are clearly gated and off by default.
- [ ] Shadow budgets keep P95 frame time within the selected renderer preset instead of allowing shadow maps to dominate frame time.
- [ ] RenderDoc captures show named shadow resources and passes with valid contents, expected state, and no duplicate legacy/modern shadow contribution for the same light.

Phase 7 implementation notes:

- Added `ModernShadowPlanner` as the shared per-frame shadow decision layer. It walks packet-frame `viewLight_t` lists, classifies projected/point/CSM candidates, records caster/receiver/translucent counts, assigns mapped/stencil-fallback/skipped policy, preserves `r_shadows`/`r_useShadowMap` fail-closed behavior, and applies the first budget layer for benchmark preset shadow resolution, update rate, max mapped light count, and total pixel cost.
- Render graph shadow resources are now explicit: `shadowMap` remains as the compatibility depth target while `projectedShadowAtlas`, `pointShadowAtlas`, `cascadeShadowAtlas`, `shadowDescriptors`, and optional `translucentShadowMoments` are named producer/consumer resources. Shadow atlas handles size from the active renderer budget and GL texture limits instead of the main viewport.
- Clustered lighting now consumes the shadow plan and uploads shadow descriptor index/policy state in each modern light record. Cluster diagnostics and `gfxInfo` report mapped, fallback, skipped, and descriptor counts, giving deferred and forward+ a single shadow-policy source for later receiver sampling.
- Added `rendererShadowPlannerSelfTest` and shadow-planner `gfxInfo` diagnostics. Full budget downgrades, receiver shader sampling, shadow-map cache reuse, hard bias/filter validation controls, shadow GPU timing, and gameplay screenshot/RenderDoc proof remain follow-up work before Phase 7 is visually complete.
- Validation passed for `tools/build/meson_setup.ps1 compile -C builddir`, `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`, `python -m py_compile tools/tests/renderer_validation_matrix.py`, staged GL 4.5 runs of `rendererShadowPlannerSelfTest`, `rendererRenderGraphSelfTest`, `rendererRenderGraphResourceSelfTest`, `rendererVisiblePathSelfTest`, `rendererShaderLibrarySelfTest`, `rendererClusterGridSelfTest`, `rendererForwardPlusSelfTest`, and `rendererDeferredResolveSelfTest`, plus a forced `r_glTier gl33` run of `rendererShaderLibrarySelfTest`, `rendererShadowPlannerSelfTest`, and `rendererClusterGridSelfTest`. Phase 7 validation logs contained no renderer `idStr::snPrintf` overflow, `WARNING: idStr`, shader compile, or program link failures.

## Phase 8: Hybrid Deferred/Forward+ Pipeline

Goal: build the actual target renderer around pass ownership and material parity.

- [ ] Depth prepass: own static, CPU-skinned, and safe alpha-tested depth; keep stencil shadows and unsupported deforms legacy until parity.
- [ ] G-buffer: albedo, normal, material/specular, emissive/light-grid, motion vectors where needed, depth, and optional velocity.
- [ ] Deferred resolve: point/projected lights, shadow-map/stencil-fallback contribution, light-grid/baked contribution, fog exclusions, HDR output.
- [ ] Forward+: alpha-tested surfaces that cannot be deferred, transparent surfaces, viewmodels, decals, particles, BSE geometry once ready, and special material cases.
- [ ] Shadows: consume the Phase 7 shadow descriptors and fallback states; do not reimplement shadow policy in the lighting shaders.
- [ ] Composition: resolve deferred and forward+ into one HDR scene target exactly once, then feed post-processing.
- [ ] Mixed fallback: support per-pass legacy composition without double-clearing or double-lighting.

Acceptance:

- [ ] Modern visible frames render a complete gameplay scene without requiring ARB2 color fallback for the common opaque/deferred and forward+ surface set.
- [ ] Fallbacks are limited, visible in metrics, and do not duplicate owned work.

## Phase 9: HDR, Bloom, SSAO, And Post Integration

Goal: preserve and integrate the previously existing post stack instead of bypassing it.

- [ ] Make the modern scene target FP16 HDR by default on supported tiers, with documented fallback formats.
- [ ] Route auto exposure, tone mapping, bloom, SSAO, motion blur, lens flare, CRT, resolution scale, and GUI overlay through the render graph.
- [ ] Preserve authored copy-render and `_currentRender` semantics for materials that depend on the previous scene color/depth.
- [ ] Integrate SSAO with modern depth/normals and keep quality/performance presets.
- [ ] Ensure bloom and tone mapping operate after deferred/forward+ composition and before GUI.
- [ ] Keep cinematic, AVI capture, render demos, and high-refresh presentation timing compatible.

Acceptance:

- [ ] Modern and legacy post captures match for startup GUI, `game/storage2`, `game/medlabs`, and `game/mcc_landing`.
- [ ] HDR/bloom/SSAO can be toggled without breaking the modern pass-owner graph or uncapped framerate pacing.

## Phase 10: Visibility, Occlusion, And Detailed Scene Performance

Goal: use modern visibility techniques to make detailed scenes faster, not just prettier.

- [ ] Preserve Quake 4 area/portal/PVS culling as the first visibility tier.
- [ ] Build a Hi-Z depth pyramid from the modern depth prepass on capable tiers.
- [ ] Add CPU coarse occluder tests for GL 3.3 where they beat naive submission.
- [ ] Add GL 4.3+ GPU culling against frustum, portal/scissor, and Hi-Z occlusion data before indirect command generation.
- [ ] Use temporal coherence for occlusion results and avoid synchronous query stalls.
- [ ] Consider parallax/relief occlusion mapping only for materials where it is asset-compatible and faster than additional geometry; otherwise prioritize occlusion culling for scene detail.
- [ ] Feed occlusion and visibility results into shadow caster selection. A scene object culled from the main view may still cast into a visible shadow receiver, so caster culling must use light frusta, receiver influence, and portal/scissor data rather than main-camera visibility alone.
- [ ] Add shadow-specific occlusion/caster culling where profitable: cached static caster sets, light-space bounds, receiver-scissor clipping, Hi-Z-assisted caster rejection on GL 4.3+, and no synchronous occlusion-query stalls.
- [ ] Add metrics for rejected objects, occlusion test cost, false-positive fallbacks, and saved draw/triangle work.

Acceptance:

- [ ] Dense scenes show measurable CPU and GPU workload reduction versus ARB2 without visual popping.
- [ ] Occlusion can be disabled instantly for debugging and never stalls the render thread on query readback.

## Phase 11: Tiered OpenGL Implementation Contract

Goal: each GL tier should have a clear, tested workload model.

| Tier | Contract |
|---|---|
| Legacy GL2 compatibility | ARB2 renderer, current GLSL shadow-map island where supported, stencil fallback, full rollback, no modern side-path cost by default. |
| Modern GL3.3 | VAO/VBO/UBO or TBO baseline, CPU clustered binning, CPU shadow planning, render-graph shadow atlases, map-range uploads, modern visible path for validated materials. |
| Modern GL4.1 | GL3.3 path plus safer MRT/post behavior, macOS-compatible shader variants, texture-array/sampler improvements where available, no compute requirement. |
| GPU-driven GL4.3 | SSBO scene/light/shadow descriptors, compute culling/binning, optional compute-assisted caster filtering, indirect command generation, CPU/GPU validation sampling. |
| Low-overhead GL4.5 | Persistent mapped streams, DSA resources, multi-bind shadow samplers/textures, named FBO updates, fence retirement, reduced CPU submit overhead. |
| Top GL4.6 | Optional bindless/SPIR-V and advanced shadow experiments only after GL4.5 parity and performance are already proven. |

Acceptance:

- [ ] Forced `r_glTier gl33`, `gl41`, `gl43`, `gl45`, and `gl46` captures all reach gameplay or fail closed with documented reasons.
- [ ] GL 3.3 is a real performance path, not merely a compatibility label.

## Phase 12: Gameplay Validation And Benchmarks

Goal: prove performance and correctness in the scenes that matter.

- [ ] Add a repeatable benchmark harness that can enter gameplay, wait for streaming, run a fixed camera/input path, capture screenshots, dump metrics, and quit cleanly.
- [ ] Use mode-specific SP/MP launch tasks. Do not count main-menu startup as renderer validation.
- [ ] Required SP scenes: `game/airdefense1`, `game/airdefense2`, `game/storage2`, `game/medlabs`, `game/mcc_landing`.
- [ ] Required MP scene: `mp/q4dm1` listen server plus local client connection.
- [ ] Required cvar coverage: `r_swapInterval 0`, `r_swapInterval 1`, `com_maxfps 0`, `com_maxfps 30`, `com_maxfps 240`, fullscreen, windowed, forced GL tiers.
- [ ] Required shadow cvar coverage: `r_shadows 1`, `r_useShadowMap 0/1`, `r_shadowMapCSM 0/1`, `r_shadowMapHashedAlpha 0/1`, `r_shadowMapDebugOverlay 1`, `r_shadowMapDebugMode 1..6`, and `r_shadowMapTranslucentMoments 1` on tiers that support it.
- [ ] Add shadow correctness validation scenes: angled projected-light caster/receiver, point-light six-face coverage, CSM camera sweep, cutout fence/grate casters at distance, dynamic character shadows, and optional translucent supported-material casters.
- [ ] Record ARB2 and modern P50/P95/P99 frame time, CPU front-end, CPU backend, GPU pass time, upload bytes, draw count, light count, shadowed light count, shadow-map pixels, shadow pass time, cluster pressure, fallbacks, and warnings.
- [ ] Add image comparison for deterministic scenes and human review checklists for nondeterministic BSE/cinematic scenes.
- [ ] Add RenderDoc captures for at least one scene per tier with named passes/resources, including shadow atlas/cubemap/translucent resources and receiver sampling state.

Acceptance:

- [ ] Modern visible path matches or beats ARB2 P95 frame time in the target scenes before any default promotion.
- [ ] No validation scene emits renderer overflow warnings.
- [ ] High-refresh presentation remains uncapped where requested and does not change simulation cadence.

## Phase 13: Default Promotion And Cleanup

Goal: promote only after the renderer has earned it.

- [ ] Keep `r_rendererModernAutoPromote 0` until gameplay, captures, and benchmark gates pass on target hardware.
- [ ] Remove or clearly quarantine diagnostic-only side paths after their replacement passes are real.
- [ ] Update `README.md`, `docs-dev/gl-renderer-modernization.md`, `docs-dev/renderer-validation-matrix.md`, and release notes with actual user-visible benefits and compatibility notes.
- [ ] Document the rollback commands prominently: `r_renderer arb2`, `r_glTier legacy`, and modern cvar disables.

Acceptance:

- [ ] Default promotion is reversible, tier-gated, warning-free, and demonstrably faster or equal in gameplay.

## Immediate Fix Order

1. Fix the per-frame `snPrintf` overflow warnings and add a zero-warning validation gate.
2. Prevent blocked modern visible frames from executing expensive side passes.
3. Unify explicit and auto-promoted modern-visible state in stats and execution.
4. Add pass-owner replacement logic so modern-owned passes skip duplicate ARB2 work.
5. Build the Viper Squad character seam capture and fix geometry/tangent/cull/skinning defects.
6. Replace placeholder material and lighting shaders with the first stock-compatible opaque/perforated path.
7. Rewrite clustered-light storage to remove tiny fixed limits and overflow-prone UBO assumptions.
8. Promote shadow mapping into the shared light/render-graph pipeline with correctness diagnostics, budgets, and stencil fallback before visible modern lighting is considered complete.
9. Integrate HDR/post and subview/BSE ownership after base geometry/material/lighting/shadow parity is stable.

## Done Means

- [ ] ARB2 default and rollback remain healthy.
- [ ] Modern disabled overhead is effectively zero.
- [ ] Modern visible does not duplicate ARB2 work for owned passes.
- [ ] GL 3.3, GL 4.1, GL 4.3, and GL 4.5 each have a coherent tested workload model.
- [ ] In-game SP/MP validation passes, not just startup self-tests.
- [ ] Renderer warnings are zero in the required scenes.
- [ ] Character seams, viewmodels, projected shadows, point shadows, CSM, cutout shadows, optional translucent shadows, HDR/post, GUI, BSE, subviews, and render demos are either modern-correct or explicitly fallback-owned.
- [ ] Shadow maps are budgeted, measurable, stable under motion, correct under debug overlays, and never duplicate legacy shadow contribution for the same light.
- [ ] Modern P95/P99 frame times meet or beat ARB2 in target scenes while preserving uncapped framerate support.
