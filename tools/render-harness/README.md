# OpenQ4 RBDOOM Parity Harness (Phase 0)

This folder contains the first executable artifact for the renderer parity effort: a deterministic capture + log harness for `docs-dev/proposals/rbdoom3-bfg-parity-modernization-plan.md` Phase 0.

## Why this exists

Before touching rendering code, we need hard data:

- baseline map quality snapshots
- deterministic startup/error diagnostics from each scenario
- repeatable runtime timing envelopes

Phase 0 is split into:

1. scene suite definition (`phase0-capture-suite.json`)
2. runner script (`Phase0Capture.ps1`)
3. baseline summary outputs in `.tmp/rbdoom-phase0-runs/<timestamp>/`

## How to run

From repository root (PowerShell), run:

```powershell
pwsh .\tools\render-harness\Phase0Capture.ps1
```

Use optional overrides as needed:

```powershell
pwsh .\tools\render-harness\Phase0Capture.ps1 `
  -SuiteFile .\tools\render-harness\phase0-capture-suite.json `
  -InstallDir (Resolve-Path .install).Path `
  -SavePath (Resolve-Path .home).Path `
  -BaseGamePath "D:\SteamLibrary\steamapps\common\Quake 4" `
  -Executable OpenQ4-client_x64.exe
```

## Scene schema

`phase0-capture-suite.json` currently supports:

- `name`: stable scene ID
- `mode`: `"sp"` or `"mp"`
- `map`: map name passed as `+map` (SP) or `+spawnServer` (MP) argument
- `capture_delay_ms`: delay before auto-screenshot
- `post_timeout_ms`: process timeout
- `capture_variants`: optional scene-specific variant list, defaults to `common.capture_variants`
- `extra_commands`: flat console token list injected before loading the target map

`common.capture_variants` supports per-variant settings:

- `name`: label used in scene output directory and summary rows
- `suffix`: optional screenshot/run-name suffix (defaults to `name`)
- `r_useShadowMapping`: `0` or `1` to force legacy/stencil vs shadow-map capture path
- `cvars`: optional object of console variable overrides (string/number values), for example:
  - `r_useParallelShadowMaps`
  - `r_shadowMapSplits`
  - `r_shadowMapCascadeBlend`
  - `r_usePBR`
  - `r_useIndirectLighting`
  - `r_usePostLightingStack`
  - `r_useSSAO`
  - `r_useTAA`
  - `r_useTonemap`
  - `r_useHiZ`
  - `r_useSSR`
  - `r_ssrStrength`
  - `r_useMaskedOcclusionCulling`
  - `r_postAA`
- `extra_commands`: optional additional console tokens for this variant
- `capture_delay_ms` / `post_timeout_ms`: optional per-variant timing overrides

## Outputs

For each run, output lands in:

- `.tmp/rbdoom-phase0-runs/<timestamp>/<scene-name>_<variant>/openq4.log`
- `.tmp/rbdoom-phase0-runs/<timestamp>/<scene-name>_<variant>/screenshot.tga` (if captured)
- `.tmp/rbdoom-phase0-runs/<timestamp>/summary.csv`
- `.tmp/rbdoom-phase0-runs/<timestamp>/summary.json`

The summary now includes `variant`, `r_useShadowMapping`, `r_useParallelShadowMaps`,
`r_shadowMapSplits`, `r_usePBR`, `r_useIndirectLighting`, `r_usePostLightingStack`,
`r_useSSAO`, `r_useTAA`, `r_useTonemap`, `r_useHiZ`, `r_useSSR`,
`r_ssrStrength`, `r_useMaskedOcclusionCulling`, and `r_postAA` columns to support paired
diff checks, including Stage-2 parallel/sun cascade production runs, Stage-3 PBR captures,
Stage-4 indirect-light captures, Stage-5 post-light stack captures, and Stage-6 SSR/HiZ/occlusion captures.

## Acceptance checklist for this stage

- [ ] Suite runs without manual cleanup on a clean `.home` savepath.
- [ ] Every scene variant produces a readable log capture file.
- [ ] Every scene variant either produces a screenshot or marks the reason (timeout/crash/no screenshot log line).
- [ ] No automated run reports non-zero warning/error spikes above baseline.
- [ ] Summary file is stable and can be used to compare commits.
- [ ] `interaction.vfp` loads successfully without `GL_PROGRAM_ERROR` in the harness log for both VP and FP program IDs.
- [ ] PBR capture variants (`r_usePBR 1`) complete without `GL_PROGRAM_ERROR` and produce screenshots/logs.
- [ ] Indirect-light capture variants (`r_useIndirectLighting 1`) complete without regressions in SP and MP runs.
- [ ] Phase-5 post-light variants (`r_usePostLightingStack 1`, `r_useSSAO 1`, `r_useTAA 1`, `r_useTonemap 1`) complete without new runtime or shader errors.
- [ ] Phase-6 variants (`r_useHiZ 1`, `r_useSSR 1`, `r_useMaskedOcclusionCulling 1`) complete without new runtime or shader errors.
- [ ] For `r_shadowMapQuality` checkpoints (`0`, `1`, `2`, `3`) the map sample counts read as `1`, `4`, `8`, `16` in light logs (or equivalent cvar-derived confirmation).
- [ ] With `r_useShadowMapping 1` and forced mapped shadows, output screenshots demonstrate non-flat penumbras and no full-black artifacts in at least one SP/MP scene.
