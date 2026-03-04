# RBDOOM-3-BFG Parity Modernization Plan for OpenQ4

Date: 2026-02-09  
Author: Codex (analysis + implementation plan)

## 1. Goal

Bring OpenQ4 to the same technical standard as modern RBDOOM-3-BFG, with special focus on lighting and shadowing, while preserving OpenQ4 rules:

- stock Quake 4 asset compatibility first
- no reliance on shipping custom `q4base/` content
- unified `openq4/` runtime layout
- Meson + SDL3 cross platform direction

## 1.5 Current Status (2026-03-03)

- Phase 0 is now operationalized with a concrete capture harness.
- Phase 1 shadow-mapping core is implemented and validated behind runtime controls.
- Phase 2 parallel/sun cascades are now implemented as the production mapped-shadow path:
  - `r_useParallelShadowMaps` defaults to enabled
  - cascaded split selection + transition blending are active
  - capture harness variants now treat parallel cascades as first-class (not preview)
- Phase 3 PBR interaction path is now implemented on the OpenGL renderer:
  - material parser support for `basecolormap` + `rmaomap`/`pbrmap` style tokens
  - optional GGX/PBR interaction branch gated by `r_usePBR` with conservative defaults
  - Stage-0 harness now includes a mapped-shadow PBR capture variant for SP/MP regression
- Phase 4 indirect-lighting foundations are now implemented:
  - per-area environment probes + light-grid samples with runtime fallback generation
  - savepath cache load/save path (`openq4/indirect/*.indirect`)
  - bake commands: `bakeEnvironmentProbes`, `bakeLightGrids`, `reloadIndirectLightCache`
  - per-view sampled indirect ambient pass integrated into frame rendering
- Phase 5 modern post-lighting stack is now integrated on the OpenGL path:
  - renderer-native pass chain for SSAO -> TAA -> tonemap with optional SMAA (`r_postAA 1`)
  - phase-5 ARB programs: `openq4_phase5_ssao.fp`, `openq4_phase5_tonemap.fp`
  - phase-5 stack owns post ordering when enabled (`r_usePostLightingStack 1`) and suppresses legacy material post chain to avoid edge-mask presentation regressions
- Phase 6 SSR/HiZ/occlusion foundations are now integrated on the OpenGL path:
  - renderer-native HiZ generation + SSR pass integration in post-light stack order
  - phase-6 ARB programs: `openq4_phase6_hiz.fp`, `openq4_phase6_ssr.fp`
  - conservative masked occlusion culling prototype (`r_useMaskedOcclusionCulling`) with diagnostics and default-safe runtime gating
- Phase 7 backend abstraction seam and Vulkan-first bootstrap are now integrated:
  - renderer backend API seam (`R_InitGraphicsBackend`/`R_ShutdownGraphicsBackend`/`R_SwapGraphicsBackendBuffers`) isolates platform backend entry points from render-pipeline call sites
  - Vulkan runtime bootstrap detection path (`r_graphicsAPI`) with safe OpenGL compatibility fallback and optional strict requirement (`r_requireVulkanBootstrap`)
  - backend diagnostics exposed via `renderBackendInfo`, `gfxInfo`, and runtime status cvar `r_activeGraphicsAPI`

## 2. Baseline and Evidence

### OpenQ4 / Quake4Doom baseline state

- OpenQ4 renderer is still the legacy OpenGL/stencil-shadow path (`src/renderer/OpenGL`, `src/renderer/tr_stencilshadow.cpp`, `src/renderer/draw_common.cpp`).
- OpenQ4 has no modern RBDOOM renderer modules:
  - missing `src/renderer/NVRHI`
  - missing `src/renderer/Passes`
  - missing `src/renderer/RenderWorld_lightgrid.cpp`
  - missing `src/renderer/RenderWorld_envprobes.cpp`
- OpenQ4 has basic post AA hook (`r_postAA`, SMAA material path) but no TAA/SSAO/SSR pipeline.
- Quake4Doom snapshot timestamps cluster around 2021-09, and source fingerprints align with pre-modern renderer architecture.

### Upstream RBDOOM evolution landmarks

- `v1.3.0` (2021-10-30): PBR GGX, baked GI (light grids + env probes), HDR/filmic integration, stronger shadow mapping path.
- `v1.4.0` (2022-03-06): stability and tooling passes.
- `v1.5.0` (2023-04-29): OpenGL replaced by DX12/Vulkan through NVRHI, major renderer rewrite, Donut SSAO, TAA default, shadow atlas pipeline.
- `v1.5.1` (2023-05-23): hotfix.
- `v1.6.0` (2025-05-10): masked occlusion culling, SSR refinements, improved light editor, GI/lightdata workflow upgrades, filesystem/tooling improvements.
- `v1.6.0..master` (2025-05 to 2026-02): ongoing Vulkan/DX12 stability, push constants, TAA/SSAO/tonemap fixes, macOS/driver compatibility.

### Churn indicator

Upstream commit counts touching renderer/shader stack:

- `v1.3.0..master`: `neo/renderer` 655 commits, `neo/shaders` 149 commits
- `v1.4.0..v1.5.0`: `neo/renderer` 344 commits, `neo/shaders` 61 commits
- `v1.5.1..v1.6.0`: `neo/renderer` 215 commits, `neo/shaders` 60 commits
- `v1.6.0..master`: `neo/renderer` 63 commits, `neo/shaders` 27 commits

This confirms the parity gap is mostly renderer architecture and lighting/shadow pipeline depth.

## 3. Major Enhancements Since the Fork Window

## 3.1 Renderer platform architecture

- Migration from legacy OpenGL renderer to NVRHI-backed DX12/Vulkan architecture.
- HLSL-first shader workflow with precompiled shader outputs.
- Push-constant and descriptor-layout evolution for modern GPU APIs.

## 3.2 Lighting and shadowing (highest impact)

- Shadow mapping pipeline became primary path:
  - atlas-based shadow allocation
  - point, spot, and parallel/sun lights
  - cascaded shadow maps for sun/parallel lights
  - quality tuning and acne/peter-panning mitigation controls
  - LOD logic for distant/small lights (skip/blend behavior)
- PBR-forward direct lighting:
  - GGX Cook-Torrance shading path
  - roughness/metalness/AO support (`rmaomap`)
  - compatibility fallback from legacy materials
- Indirect lighting stack:
  - environment probes (auto placement + manual entities)
  - per-area irradiance light grids with SH data
  - bake commands and cached file formats
- Screen-space and post-lighting:
  - improved SSAO (Donut-based path)
  - TAA integration and later SMAA/TAA coexistence fixes
  - HiZ support and SSR integration
  - modern tonemapping and filmic path improvements

## 3.3 Performance + visibility systems

- Masked software occlusion culling (MOC) integrated with renderer front-end culling.
- More robust GPU profiling/stat instrumentation.

## 3.4 Tooling and workflow upgrades

- Improved in-game light editor workflow.
- Better map conversion and standalone compile workflows.
- GI and light data cache workflows (including HDR-to-bimage caching path).

## 4. OpenQ4 Gap Matrix (Prioritized)

Priority P0 (critical to "same standard"):

- Modern shadow mapping system (atlas + CSM + point/spot support)
- PBR interaction path with legacy compatibility
- GI stack (env probes + light grids + bake/runtime load)
- SSAO + TAA + tonemap integration into the frame graph

Priority P1 (needed for production quality/performance):

- Renderer API modernization strategy (RHI layer and backend split)
- HiZ + SSR
- Masked occlusion culling
- GPU timing/diagnostic instrumentation parity

Priority P2 (important, but after lighting core):

- Full light editor modernization
- Expanded mapping/baking tooling
- Additional visual modes and non-core rendering extras

## 5. Recommended Delivery Strategy

Use a dual-track strategy:

- Track A: deliver lighting/shadow parity on current OpenGL renderer first (shortest path to visible Quake 4 gains).
- Track B: in parallel, build a backend abstraction seam so Vulkan/DX12 (or Vulkan-first) can land without redoing gameplay integration twice.

Reason:

- A direct big-bang backend swap is high risk for Quake 4 behavior parity.
- Lighting/shadow quality problems can be solved earlier with targeted renderer evolution.

## 6. Implementation Plan

## Phase 0: Fork-Delta Capture and Render Test Harness (2-3 weeks)

Deliverables:

- Frozen parity target list from RBDOOM features and cvars.
- Golden-scene capture suite for Quake 4 maps (same camera paths, fixed cvars, deterministic screenshots).
- Performance telemetry baseline for OpenQ4.

Tasks:

- Add automated capture command sets for representative SP/MP maps.
- Add frame timing buckets for shadow pass, ambient pass, post stack.
- Define pass/fail thresholds per scene.

Exit criteria:

- Repeatable visual/perf baselines committed in docs and test scripts.

## Phase 1: Shadow Mapping Core (6-10 weeks)

Deliverables:

- Shadow map rendering path in OpenQ4, initially toggleable beside stencil shadows.
- Atlas allocator with debug visualization and stats.
- Point + spot light shadow maps.

Tasks:

- Introduce shadow map textures and per-light shadow metadata.
- Implement shadow caster gather rules preserving existing Quake 4 material semantics.
- Add cvars for atlas size, map resolution, bias, sample count.
- Implement PCF/Vogel-like sampling path.

Exit criteria:

- Stencil and shadow-map paths switchable.
- Shadow acne and peter-panning within acceptable tuning range across golden scenes.

## Phase 2: Parallel/Sun Lights and Cascades (4-6 weeks)

Deliverables:

- Parallel light shadow support with cascaded splits.
- Stable split selection and transition blending.

Tasks:

- Define parallel-light representation compatible with Quake 4 light entities.
- Add split logic and camera-dependent matrix generation.
- Add debug overlays for cascade partitions and aliasing hotspots.

Exit criteria:

- Outdoor maps and long-view scenes show stable sun shadows without severe pumping.

## Phase 3: PBR Interaction Path with Compatibility Fallback (6-8 weeks)

Deliverables:

- PBR shading path (GGX-based) integrated into light interactions.
- Legacy material fallback path with conservative defaults.

Tasks:

- Extend material parsing for `basecolormap`/`rmaomap` style keywords while preserving legacy Doom3/Q4 keywords.
- Implement roughness/metalness/AO usage in direct + indirect terms.
- Add feature gates so stock assets maintain expected look by default.

Exit criteria:

- No regressions on stock Quake 4 assets.
- Optional PBR overrides function through `openq4` staged content without requiring repo `q4base`.

Implementation notes (2026-03-03):

- `Material.cpp` accepts `basecolormap` as diffuse alias and `rmaomap`/`pbrmap` as specular payload aliases.
- PBR metadata is carried in interactions and uploaded through new program params (`PP_PBR_PARAMS0/1`).
- `interaction.vfp` includes a compatibility-gated PBR branch plus mapped-shadow modulation while preserving legacy fallback behavior when `r_usePBR = 0`.
- Regression validation runs through `tools/render-harness/Phase0Capture.ps1` including SP + MP maps and PBR variants.
- Validation evidence run `20260303-105303`: `12/12` scene variants `ok`, `0` GL program errors, `0` logged `ERROR:` lines (warnings remain at baseline levels).

## Phase 4: Indirect Lighting Stack (Env Probes + Light Grids) (8-12 weeks)

Deliverables:

- Environment probe system with fallback data when no baked content exists.
- Per-area light grid support with bake + load paths.
- Commands comparable to `bakeEnvironmentProbes` and `bakeLightGrids`.

Tasks:

- Implement probe placement policy using portal-area topology.
- Implement light grid data structures and interpolation for dynamic models and surfaces.
- Add cache format under `fs_savepath/openq4` (not packaged custom assets by default).
- Provide background bake jobs and resumable cache generation.

Exit criteria:

- Pitch-black ambient failure cases resolved without violating stock-asset behavior.
- Clean startup and runtime when baked data is absent (graceful fallback).

## Phase 5: Modern Post Lighting Stack (SSAO, TAA, Tonemap, Optional SMAA) (6-10 weeks)

Deliverables:

- Integrated pass chain for SSAO, TAA, tonemap.
- Existing SMAA path retained as an option.

Tasks:

- Introduce pass ordering and history buffers.
- Stabilize motion vectors and GUI/transparency handling for TAA.
- Add r_show/perf counters for pass timings.

Exit criteria:

- Visual stability in motion and acceptable ghosting profile.
- Pass cost is measurable and tunable from cvars.

## Phase 6: SSR + HiZ + Occlusion Culling (6-8 weeks)

Deliverables:

- HiZ generation and SSR path (optional toggle).
- Masked occlusion culling prototype then production mode.

Tasks:

- Integrate hierarchical depth path and screen-space trace.
- Restrict SSR to materials/classes where artifacts are manageable.
- Add MOC guardrails and diagnostics to prevent over-culling gameplay-critical entities.

Exit criteria:

- Net positive performance in heavy scenes with stable correctness.

## Phase 7: Renderer Backend Abstraction and Vulkan-First Bring-Up (parallel, 12-20+ weeks)

Deliverables:

- Backend abstraction seam in OpenQ4 renderer.
- Vulkan backend boot path with feature parity subset, then staged completion.

Tasks:

- Isolate API-specific resource, command, and pipeline concepts from gameplay/scene logic.
- Start Vulkan with the new lighting stack already validated in OpenGL.
- Keep platform behavior aligned with SDL3 + Meson.

Exit criteria:

- Vulkan path can run core campaign maps with parity checks passing.

## 7. Lighting and Shadowing Special Plan (Detail)

The lighting/shadow track should be implemented as four strict gates:

Gate L1:

- Atlas shadow maps (point/spot), tunable bias/sampling, fallback to stencil path.

Gate L2:

- Parallel/sun cascades, stable splits, blending of distant/small-light behavior.

Gate L3:

- PBR direct interactions + legacy fallback calibration for stock Quake 4 assets.

Gate L4:

- Indirect stack (env probes + light grids) with runtime fallback and no required packaged override assets.

Only after L4 should SSR/MOC be considered default-on candidates.

## 8. Risks and Mitigations

Risk:

- Quake 4 authored content diverges from Doom 3 BFG assumptions in light/entity behavior.

Mitigation:

- Keep dual-path rendering toggles during migration and compare per-map screenshots.

Risk:

- Shipping prebaked GI data conflicts with OpenQ4 asset rules.

Mitigation:

- Prefer runtime/generated cache in `fs_savepath`; keep packaged data optional and minimal.

Risk:

- Backend rewrite stalls gameplay parity work.

Mitigation:

- Deliver lighting features first on existing backend, then swap backend underneath validated pass logic.

Risk:

- Performance regressions from new passes.

Mitigation:

- Add hard perf budgets and per-pass timers from Phase 0 onward.

## 9. Immediate Next Steps (Recommended Order)

1. Author Phase 4 indirect-lighting implementation spec (env probes + light grids + fallback policy + cvar gating).
2. Begin Phase 4 implementation on OpenGL renderer with graceful fallback when no baked data exists.
3. Keep Phase 0 harness as a regression gate after each Phase 4 milestone and diff `summary.json` output.
4. Keep Procedure 1 runtime loop active for both SP and MP map coverage after renderer changes.

Status update (2026-03-03):

1. Phase 4 spec/implementation delivered on OpenGL path with savepath caches + fallback generation.
2. Harness coverage expanded with indirect-light variant (`mapped_parallel_cascades_pbr_indirect`).
3. Phase 5 post-light stack integrated with phase-owned ordering (`r_usePostLightingStack`).
4. Validation evidence run `20260303-120222` (phase-5 focused SP/MP suite): `3/3` variants `ok`, `0` logged `GL_PROGRAM_ERROR` lines, `0` summary `errors`.
5. Phase 6 integration landed with HiZ/SSR post passes, masked occlusion culling prototype, and harness variant/cvar coverage updates.
6. Phase 7 integration landed with backend abstraction seam, Vulkan runtime bootstrap path, and harness backend variant/cvar coverage updates.

## 11) End-to-End Progress Checklist (Execution View)

### Current progression

- [x] Phase 0 complete (harness + deterministic suite + baseline capture + summary reporting).
- [x] Phase 1 foundational work active/completed for shadow-mapping migration.
- [x] Phase 2: parallel/sun shadow path is productionized with cascades + transition blending on mapped shadows.
- [x] Phase 3: PBR interaction path is implemented on OpenQ4 legacy OpenGL with legacy fallback + harness coverage.
- [x] Phase 4: env-probe/light-grid indirect-light cache stack is implemented with runtime fallback + bake/reload commands.
- [x] Phase 5: SSAO/TAA/modern tonemap stack is implemented with optional SMAA and post-stack ownership over legacy post materials.
- [x] Phase 6: SSR/HiZ/Occlusion Culling is implemented on the OpenGL path with conservative runtime defaults.
- [x] Phase 7: backend abstraction seam + Vulkan-first bootstrap path are implemented on the OpenGL compatibility renderer.

### NVRHI status and positioning

- [ ] `src/renderer/NVRHI` is not present in OpenQ4.
- [x] Phase-7 backend abstraction seam is in-tree without importing NVRHI.
- [x] Current strategy remains: stabilize core visual parity on OpenGL first, then port validated rendering features into a full Vulkan/NVRHI renderer path.

## 12. Primary Source References

- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/RELEASE-NOTES.md`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/README.md`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderSystem_init.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderBackend.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderWorld_lightgrid.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderWorld_envprobes.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/Material.cpp`
- `E:/_SOURCE/_CODE/Quake4Doom-master/src/renderer/RenderSystem_init.cpp`
- `e:/Repositories/OpenQ4/src/renderer/RenderSystem_init.cpp`
- `e:/Repositories/OpenQ4/src/renderer/tr_stencilshadow.cpp`

## 13. Shadow-Mapping Failure Analysis Flow (Definitive Debug Procedure)

Use this flow when `r_useShadowMapping 1` does not produce expected mapped shadows.

### Runtime toggles

- `r_showInteractions 1`
- `r_shadowMapDebugFlow 1` (summary flow counters)
- `r_shadowMapDebugFlow 2` (per-light pipeline traces)
- `r_shadowMapDebugLight -1` (all lights) or set to a specific `render-light index`/`lightId`

### Pipeline checkpoints

1. Selection stage (`R_SelectShadowMapForViewLight`):
- Verify `smdbg.select` output.
- Confirm each tested light reports:
  - `use:1` for mapped candidates.
  - Fallback reason when rejected (`noShadows`, `atlasFull`, etc).

2. Render stage (`R_RenderShadowMapForViewLight`):
- Verify `smdbg.render` output for selected lights.
- Confirm:
  - `validCascades > 0`
  - `submitted > 0`
  - `drawn:1`
- If `drawn:0`, reason is explicit in the trace (`noGeometry`, `renderFailed`, `missingTile`, etc).

3. Backend light path stage (`RB_ARB2_DrawInteractions`):
- Verify `smdbg.path` output.
- Confirm whether each light used mapped or stencil path:
  - `mapped:1` expected for strict mapped mode.
  - `mapped:0` indicates actual stencil path execution.

4. Frame summary (`R_PerformanceCounters`):
- Use `shadowMapFlow` line:
  - `lights`, `shadowSurfs`, `selected`, `renderAttempts`, `renderSuccess`, `mappedPath`, `stencilPath`.
- Correlate with `shadowMapFallbacks` reason breakdown.

### Interpretation map

- `atlasFull` with `overflow > 0`:
  - atlas capacity insufficient (`r_shadowMapAtlasSize` too small, `r_shadowMapImageSize` too large, too many eligible lights).
- `noShadows` high:
  - lights intentionally non-shadow-casting (`light.parms.noShadows` or shader-level no-shadow semantics).
- `renderFailed` high with `submitted > 0`:
  - shadow-map raster/projection/render-target failure.
- `renderAttempts > 0` but `renderSuccess == 0`:
  - mapped rendering is selected but never succeeds.
- `mappedPath < shadowSurfs` and `stencilPath > 0`:
  - active runtime fallback back to stencil still present.
