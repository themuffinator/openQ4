# OpenQ4 Phase 3 Spec: PBR Interaction Path with Legacy Fallback

Date: 2026-03-03  
Owner: OpenQ4 renderer track

## 1. Purpose

Define the Phase 3 implementation contract for RBDOOM-style direct-light PBR support on OpenQ4's legacy OpenGL renderer, while preserving stock Quake 4 behavior by default.

## 2. Scope

- Add material-keyword compatibility for `basecolormap` and `rmaomap`/`pbrmap`.
- Add a gated PBR interaction branch (GGX-like) in the ARB2 interaction shader path.
- Keep default visuals conservative (`r_usePBR = 0`) so stock assets remain on legacy behavior unless explicitly enabled.
- Extend render harness coverage so PBR toggles are validated in SP + MP capture runs.

## 3. Implemented Changes

## 3.1 Material parsing

- `basecolormap` is accepted as a `diffusemap` alias.
- `rmaomap` and `pbrmap` are accepted as `specularmap`-class payload aliases.
- `rmaomap`/`pbrmap` usage marks materials with `MF_PBR_RMAO`.

## 3.2 Interaction payload and gating

- `drawInteraction_t` now carries:
  - `usePBR`
  - `pbrParams0` (`enable`, `roughnessScale`, `metalnessScale`, `aoScale`)
  - `pbrParams1` (`minRoughness`, `dielectricF0`, reserved, reserved)
- New program params:
  - `PP_PBR_PARAMS0 = 26`
  - `PP_PBR_PARAMS1 = 27`
- ARB2 backend uploads PBR params only when enabled for the interaction; disabled defaults are uploaded otherwise.

## 3.3 Runtime controls

- `r_usePBR` (default `0`)
- `r_pbrRoughnessScale` (default `1.0`)
- `r_pbrMetalnessScale` (default `1.0`)
- `r_pbrAOScale` (default `1.0`)
- `r_pbrMinRoughness` (default `0.04`)
- `r_pbrDielectricF0` (default `0.04`)

## 3.4 Shader behavior

- `interaction.vfp` retains legacy direct-light result and blends to PBR output only when the PBR enable param is set.
- RMA channels are interpreted as:
  - `R`: roughness
  - `G`: metalness
  - `B`: ambient occlusion
- Existing shadow-map branch compatibility and ARB swizzle safety are preserved.

## 4. Validation Contract

## 4.1 Build + staging

- Build and stage with:
  - `tools/build/meson_setup.ps1 compile -C builddir`
  - `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`

## 4.2 Harness regression

- Run `tools/render-harness/Phase0Capture.ps1`.
- Required suite coverage:
  - legacy stencil
  - mapped shadows
  - mapped parallel cascades
  - mapped parallel cascades + PBR (`r_usePBR 1`)
- Validate SP + MP scenes produce logs and screenshots and show no `GL_PROGRAM_ERROR` for `interaction.vfp`.

Latest run evidence (2026-03-03):

- Run directory: `.tmp/rbdoom-phase0-runs/20260303-105303`
- Results: `12/12` variants status `ok`
- `GL_PROGRAM_ERROR` hits: `0`
- Aggregated `ERROR:` count in summary rows: `0`

## 5. Exit Criteria for Phase 3

- PBR interaction path is integrated and cvar-gated.
- Legacy fallback remains default and stable for stock assets.
- Harness captures include a PBR variant and remain regression-usable.
