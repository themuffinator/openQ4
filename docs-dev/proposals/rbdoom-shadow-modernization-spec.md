# RBDOOM-3-BFG Shadow Modernization Specification (Phase 1)

Date: 2026-03-03
Owner: OpenQ4 + Codex (renderer parity track)

## 1) Objective

Bring Quake 4 shadowing on OpenQ4 to parity with modern RBDOOM-3-BFG behavior while keeping legacy compatibility and avoiding asset-side dependencies.

Current OpenQ4 is on Doom 3-era stencil shadows. This phase adds a new shadow-mapping path behind a runtime switch and keeps `r_shadows` semantics intact.

## 2) Functional Targets (Phase 1 scope)

- Add a controllable runtime switch between stencil and shadow-mapping paths.
- Implement atlas-based shadow map allocation for point and spot/beam lights.
- Add point/spot shadow map rendering and depth sampling flow on OpenGL.
- Keep all existing material and content semantics operational.
- Add diagnostics for atlas occupancy, allocation failures, and quality tuning.
- Add a phase-2 warm-up gate for parallel/sun lights: `r_useParallelShadowMaps` (default 0).

Non-targets for this phase:

- Parallel/cascaded sun shadows (Phase 2)
- GI/env-probe/light-grid systems (Phase 4)
- NVRHI/Vulkan/DX12 backend migration (Phase 7)

## 3) Pipeline Blueprint

1. Keep current front-end light and interaction classification.
2. Add dispatch in the renderer:
   - `r_useShadowMapping = 0` => existing stencil path.
   - `r_useShadowMapping = 1` => shadow-map path.
3. Add atlas tile reservation and tracking for active shadow-casting lights.
4. Render point/spot occluder depth into per-light map regions.
5. Sample shadow maps in lighting pass; if atlas allocation or render fails, gracefully degrade to stencil for that light and record counters.

## 4) Atlas Plan

- Primary storage: one shadow atlas texture controlled by `r_shadowMapAtlasSize`.
- Per-light tile sizing from `r_shadowMapImageSize` (fixed in this phase).
- Light-order and occupancy:
  - prioritize direct lights (high priority / near camera) first,
  - fallback to fixed-size tiles for distant lights.
- Overflow policy:
  - keep a per-frame overflow counter,
  - skip map render for overflowed lights,
  - keep fallback path safe and deterministic for visual continuity.

## 5) Artifact and Debug Controls

- `r_shadowMapRegularDepthBiasScale`
- `r_shadowMapSunDepthBiasScale` (reserved for phase 2)
- `r_shadowMapSamples`
- `r_useParallelShadowMaps` (bool, default 0): allow parallel/sun lights into mapped shadowing when enabled.
- `r_shadowMapOccluderFacing`
- `r_showShadowMaps`
- `r_showShadowMapLODs`

## 6) Initial CVar Contract (Phase 1)

- `r_useShadowMapping` (bool, default 0): runtime renderer switch.
- `r_useShadowAtlas` (bool, default 1): shadow map atlas on/off.
- `r_shadowMapAtlasSize` (int, default 2048): atlas dimension.
- `r_shadowMapImageSize` (int, default 1024): base tile resolution.
- `r_shadowMapSamples` (int, default 16, 1..64): PCF sample count.
- `r_shadowMapRegularDepthBiasScale` (float, default 0.999).
- `r_shadowMapSunDepthBiasScale` (float, default 0.999, phase 2 placeholder).
- `r_shadowMapSplits` (int, default 0, 0..4, phase 2 placeholder).
- `r_shadowMapOccluderFacing` (int, default 2, front/back/both).
- `r_showShadowMaps` (bool, default 0).
- `r_showShadowMapLODs` (int, default 0).
- `r_shadowMapSplits` (int, default 0): preview path for future CSM. Values >0 are logged and treated as single-map previews for parallel lights.

## 7) Phase 1 Checklist

- [x] Add phase-1 cvar set in `RenderSystem_init.cpp` and declarations in `tr_local.h`.
- [x] Add migration-safe transition guard so `r_useShadowMapping=1` does not silently blank shadows.
- [x] Add one-shot fallback diagnostic when `r_useShadowMapping` is enabled before the full shadow-map path is active.
- [x] Add per-light atlas allocator skeleton and overflow counters.
- [x] Add point-light shadow map render target allocation.
- [x] Add spot-light projection matrix generation for map rendering.
- [x] Add depth sample path (PCF) and bias tuning hooks through interaction payload and ARB parameter plumbing.
- [x] Add debug visibility for atlas occupancy and rejection counters.
- [x] Add phase-2 warm-up guard `r_useParallelShadowMaps` in `R_SelectShadowMapForViewLight`.
- [x] Add Stage-2 preview instrumentation: parallel split requests are counted and reported (`c_shadowMapSplitRequests`, `c_shadowMapCascadeUnsupported`) with one-shot warning when `r_shadowMapSplits>0`.
- [x] Connect captures to Phase 0 harness and add before/after snapshots.
- [x] Implement shader-side PCF kernels and ship compatibility + quality presets.

### 7.1 Shader Kernel Completion Notes

- `interaction.vfp` now uses explicit tier masks for 1/4/8/16 sample families with consistent normalization.
- Quality preset mapping is enforced through `r_shadowMapQuality`:
  - `1` -> 4 samples
  - `2` -> 8 samples
  - `3` -> 16 samples
- Custom sample request path remains available with `r_shadowMapSamples` when quality preset is `0`.

## 8) Acceptance Gate

- `r_useShadowMapping = 0` is unchanged in capture baseline.
- `r_useShadowMapping = 1` renders valid light/shadow pass for default map set with no hard failures.
- No gameplay asset replacement required.

## 9) References

- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderSystem_init.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/tr_frontend_addmodels.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderBackend.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderCommon.h`
