# OpenQ4 Phase 4 Spec: Indirect Lighting Stack (Environment Probes + Light Grid)

Date: 2026-03-03  
Owner: OpenQ4 renderer track

## 1. Purpose

Deliver the Phase 4 indirect-lighting baseline on the legacy OpenGL renderer with:

- per-area environment probes
- per-area light-grid samples
- savepath cache load/save
- graceful runtime fallback when no baked cache exists

## 2. Runtime Features

- `idRenderWorldLocal` now owns:
  - area bounds for indirect lighting
  - environment probe list
  - light-grid point list
  - per-area fallback irradiance
- Map load flow:
  - compute area bounds
  - try loading `openq4/indirect/<map>.indirect` from `fs_savepath`
  - if missing/invalid, build runtime fallback probe/grid data
- Per-view sampling:
  - sample nearest area grid/probe from camera origin
  - apply conservative floor and intensity scaling
  - expose result as `tr.indirectAmbientColor`
  - update ambient-normal cube map for ambient-light interaction compatibility
  - apply additive fullscreen indirect ambient pass (cvar-gated)

## 3. Commands

- `bakeEnvironmentProbes`
  - rebuild probes from current world/light state and write cache
- `bakeLightGrids`
  - rebuild area light-grid samples and write cache
- `reloadIndirectLightCache`
  - reload cache from savepath, falling back to runtime generation if missing

## 4. Cvars

- `r_useIndirectLighting` (`1`)
- `r_indirectLightIntensity` (`0.08`)
- `r_indirectMinAmbient` (`0.02`)
- `r_indirectGridCellSize` (`512`)
- `r_indirectFullscreenPass` (`1`)

## 5. Validation

- Build and stage:
  - `tools/build/meson_setup.ps1 compile -C builddir`
  - `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`
- Harness:
  - `tools/render-harness/Phase0Capture.ps1`
  - include `mapped_parallel_cascades_pbr_indirect` variant

## 6. Exit Criteria Mapping

- Env probes: implemented with area-centered probe data and cache persistence.
- Light grid: implemented with per-area sampled points and nearest-point sampling.
- Cache behavior: absent cache path falls back to runtime generation without startup failure.
- Commands: bake/reload command surface present and wired through renderer command system.
