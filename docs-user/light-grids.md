# Light Grid and Irradiance Volume Guide

This guide covers openQ4's user-facing light-grid system: what it does, how to enable or disable it, how to bake data for one map or many maps, where the baked files go, and how to troubleshoot common problems.

openQ4 keeps the existing feature naming in the engine and README, so you will see both of these terms:
- `light grid`
- `irradiance volume`

In practice they refer to the same openQ4 feature: precomputed indirect diffuse lighting sampled from a 3D probe layout.

## Quick Start

Recommended defaults:

```cfg
seta r_useLightGrid 1
seta r_showLightGrid 0
```

The same runtime toggle is available in the menu at `Settings -> System -> Irradiance Volumes`.

Bake the currently loaded map from the in-game console:

```cfg
bakeLightGrids
```

Bake every discovered multiplayer map from the command line, then quit when finished:

```text
openQ4-client_x64.exe +bakeLightGrids all-mp -quit
```

Show probe positions while testing a baked map:

```cfg
r_showLightGrid 1
```

Notes:
- `r_useLightGrid` is enabled by default.
- Use the client executable for baking. Do not use the dedicated server executable.
- Batch baking prints live progress to the console and log instead of relying on an in-viewport progress screen.

## What the System Does

openQ4's current light-grid path:
- adds indirect diffuse lighting from precomputed probes
- loads one `.lightgrid` metadata file per map
- loads baked irradiance and visibility atlas images per portal area
- samples that data at runtime when `r_useLightGrid 1`
- uses visibility moments during interpolation to reduce wall and corner light leaks
- relocates probes away from solid or near-solid map space where possible, then uses those relocated positions at runtime
- blends indirect light between adjacent visible portal areas near doorway/window boundaries
- keeps the current portal area and visible portal neighbors resident for a short age window to reduce atlas reload hitches while moving between areas
- draws one material-selected representative diffuse stage for the indirect pass, avoiding repeated light-grid redraws on multi-diffuse materials
- lights eligible first-person weapon/viewmodel surfaces from the active view area's light grid through a dedicated weapon pass

Current scope and limits:
- diffuse-only
- non-PBR
- LDR bake output
- writes `.tga` atlas images, not BFG `.exr`
- intended for openQ4's native bake/load path, not drop-in BFG asset parity
- translucent effects, decals, and other non-lighting surfaces remain outside the runtime light-grid pass

If no baked assets are found, openQ4 can still generate a runtime probe layout for debugging and baking, but there will be no indirect-light contribution until actual baked files exist.

## Runtime Controls

### Core Settings

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_useLightGrid` | `1` | `0..1` | Master toggle for runtime indirect diffuse from baked light grids. |
| `r_lightGridPortalBlend` | `64` | `0..256` | World-unit radius for cross-area light-grid blending near visible portal boundaries. `0` disables portal blending. |
| `r_lightGridResidencyFrames` | `180` | `0..3600` | Frames to keep light-grid atlas images resident after visible or portal-neighbor use. `0` keeps only the current visible/neighbor set. |
| `r_showLightGrid` | `0` | `0..3` | Draws probe positions for debugging. |
| `r_lightGridBakeWorkers` | `0` | `-1..8` | CPU worker threads for probe integration during baking. `-1` disables threading, `0` auto-picks from logical CPU cores. |
| `r_lightGridBakeAsyncReadback` | `1` | `0..1` | Uses async pixel-pack-buffer readback during baking when the driver supports it. Falls back to synchronous readback when unsupported. |
| `r_lightGridBakeMemoryMB` | `12` | `4..256` | Caps transient bake memory used for in-flight probe jobs. Lower values reduce RAM/pagefile use but may slow baking. |
| `r_lightGridBakeReadbackSlots` | `0` | `0..16` | Caps async readback buffers during baking. `0` picks an automatic value; lower values reduce driver/GPU memory use. |

### `r_showLightGrid` Modes

| Value | What it shows |
|---|---|
| `0` | Off |
| `1` | Valid probes in the current area only |
| `2` | Valid probes in all areas |
| `3` | All probes, including invalid ones |

Practical use:
- `r_useLightGrid 0` lets you compare baked indirect lighting against the baseline renderer.
- `r_showLightGrid 1` is the best first validation mode after baking a map.
- `r_showLightGrid 3` is useful if a bake created probe slots that ended up invalid or sparse.
- Relocated probes draw in orange, and probes that remain near solid space draw in yellow.

These cvars are runtime controls. They do not require `vid_restart`.

## Baking from the In-Game Console

If you already have a map loaded, run:

```cfg
bakeLightGrids
```

This bakes the currently loaded map using default settings and writes the output to `fs_savepath`.

Typical flow:

```cfg
devmap game/tram1
bakeLightGrids
```

After the bake completes:
- openQ4 writes the `.lightgrid` metadata file.
- openQ4 writes one atlas per area.
- The newly written data is reloaded automatically.

## Batch Baking from the Command Line

This is the preferred workflow when you want progress information without manually loading maps one by one.

Bake all maps:

```text
openQ4-client_x64.exe +bakeLightGrids all -quit
```

Bake all multiplayer maps only:

```text
openQ4-client_x64.exe +bakeLightGrids all-mp -quit
```

Bake a selected list of maps:

```text
openQ4-client_x64.exe +bakeLightGrids game/tram1 game/process1 mp/q4dm1 -quit
```

Behavior:
- openQ4 discovers or accepts target map names.
- It loads each map automatically.
- It switches between `game_sp` and `game_mp` automatically when needed.
- Without `force`, it skips maps whose required `.lightgrid` metadata and area atlas files already exist and whose stored bake-settings/layout hash matches the current bake.
- It prints live progress plus a final phase timing/counter summary to the console and log as it bakes probes and areas.
- It exits at the end if `-quit` is supplied.

Important:
- Use `openQ4-client_x64.exe`, not `openQ4-ded_x64.exe`.
- Multiplayer-map baking requires a render-capable client path, so openQ4 forces `net_serverDedicated 0` during that workflow when necessary.

## Bake Command Syntax

General syntax:

```cfg
bakeLightGrids [all | all-mp | <map> ...] [force] [-quit] [limit<num>] [bounce<num>] [size<num>] [blends<num>] [samples<num>] [grid ( x y z )]
```

If no map names are given:
- openQ4 bakes the currently loaded map.

If map names, `all`, or `all-mp` are given:
- openQ4 runs in batch mode and loads maps automatically.
- Multiplayer targets are cheat-protected. Enable cheats first with `sv_cheats 1` or `net_allowCheats 1`.

### Bake Options

| Option | Example | What it does |
|---|---|---|
| `all` | `bakeLightGrids all` | Bake every discovered map. |
| `all-mp` | `bakeLightGrids all-mp` | Bake every discovered multiplayer map only. |
| map name(s) | `bakeLightGrids game/tram1 mp/q4dm1` | Bake only the listed maps. |
| `force` | `bakeLightGrids force` | Remove existing outputs for the target map(s) and rebuild them even if all required files already exist. |
| `-quit` | `+bakeLightGrids all -quit` | Quit after the batch completes. |
| `limit<num>` | `limit2048` | Maximum probe count per area before the grid spacing grows. |
| `bounce<num>` | `bounce2` | Number of bake passes. Bounce 2+ reuses the previous openQ4 bake through the runtime light-grid path. |
| `size<num>` | `size256` | Cubemap capture resolution used during baking. |
| `blends<num>` | `blends4` | Number of capture blends/jittered accumulations per face. |
| `samples<num>` | `samples256` | Irradiance integration samples per output texel. |
| `grid x y z` | `grid 64 64 128` | Probe spacing in world units. |
| `grid ( x y z )` | `grid ( 64 64 128 )` | Same as above, with explicit parentheses. |

### Recommended Starting Presets

Fast test bake:

```text
openQ4-client_x64.exe +bakeLightGrids game/tram1 size64 samples32 limit1024 -quit
```

For multiplayer maps or `all-mp`, enable cheats before starting the bake, for example `openQ4-client_x64.exe +set sv_cheats 1 +bakeLightGrids all-mp -quit`.

Balanced quality:

```text
openQ4-client_x64.exe +bakeLightGrids game/tram1 size128 samples128 blends1 bounce1 grid 64 64 128 -quit
```

Higher-quality bake:

```text
openQ4-client_x64.exe +bakeLightGrids game/tram1 size256 samples256 blends2 bounce2 grid 48 48 96 -quit
```

Practical tuning advice:
- Increase `size` if captured lighting detail looks too soft.
- Increase `samples` if the result looks noisy or inconsistent.
- Increase `blends` if you want extra accumulation smoothing during capture.
- Reduce `grid` spacing if lighting changes too abruptly between probes.
- Raise `limit` if an area is large and you do not want the grid spacing to auto-expand as aggressively.
- If baking is CPU-limited, leave `r_lightGridBakeWorkers 0` for automatic threading or set an explicit worker count to tune for your machine.
- Leave `r_lightGridBakeAsyncReadback 1` enabled for the fastest default bake path. It is most effective with `blends1` and capture sizes that fit inside the current viewport.
- If memory or pagefile usage is high, reduce `r_lightGridBakeMemoryMB` first. `8` is a good conservative value and `4` is the lowest supported budget.
- If driver or GPU-side memory use is still high, set `r_lightGridBakeReadbackSlots 2` or `1`.
- If you need the absolute lowest-memory path, combine `r_lightGridBakeAsyncReadback 0` with a lower `r_lightGridBakeMemoryMB` value.

## Output Files and Paths

The bake writes to `fs_savepath`.

For a map such as `game/tram1`, the main outputs are:

```text
maps/game/tram1.lightgrid
env/maps/game/tram1/area0_lightgrid_amb.tga
env/maps/game/tram1/area0_lightgrid_vis.tga
env/maps/game/tram1/area0_lightgrid_pos.tga
env/maps/game/tram1/area1_lightgrid_amb.tga
env/maps/game/tram1/area1_lightgrid_vis.tga
env/maps/game/tram1/area1_lightgrid_pos.tga
...
```

What each file is for:
- `maps/.../*.lightgrid`
  Stores probe layout metadata, area assignment, bounds, spacing, probe origins, and deterministic bake stats used to detect stale outputs.
- `env/maps/.../area*_lightgrid_amb.tga`
  Stores the baked per-area indirect diffuse atlas data used at runtime.
- `env/maps/.../area*_lightgrid_vis.tga`
  Stores baked per-area visibility and distance moments used to reduce indirect-light leakage during runtime interpolation.
- `env/maps/.../area*_lightgrid_pos.tga`
  Stores compact per-probe relocation offsets so runtime visibility checks use the actual baked probe positions instead of ideal grid centers.

openQ4 loads these files automatically when the corresponding map is loaded.

## Typical Workflows

### 1. Bake One Map and Test It Immediately

```cfg
devmap game/tram1
bakeLightGrids
r_showLightGrid 1
```

Then:
- walk through the map
- confirm probes appear where expected
- toggle `r_useLightGrid 0` and `1` to compare the lighting difference

### 2. Batch Bake a Whole Asset Set Overnight

```text
openQ4-client_x64.exe +set logFileName logs/openq4_lightgrids.log +bakeLightGrids all -quit
```

This gives you:
- unattended processing
- live console progress
- a dedicated bake log file

### 3. Re-Bake Only Multiplayer Maps

```text
openQ4-client_x64.exe +bakeLightGrids all-mp -quit
```

### 4. Force a Clean Re-Bake of the Current Map

```cfg
devmap game/tram1
bakeLightGrids force
```

## Verifying That a Bake Worked

Use this checklist after a bake:

1. Load the target map normally.
2. Set `r_showLightGrid 1`.
3. Confirm probe points appear in the current area.
4. Toggle `r_useLightGrid 0` and `r_useLightGrid 1`.
5. Confirm the scene changes in a sensible indirect-diffuse way.
6. Check that the expected files exist under `fs_savepath`.

Good signs:
- probe positions line up with playable spaces
- indirect lighting changes smoothly instead of popping hard between areas
- the baked version fills in ambient shadowed spaces more naturally than the baseline renderer

## Troubleshooting

### `bakeLightGrids: no primary world/view loaded.`

Cause:
- You ran `bakeLightGrids` with no explicit map targets and no current map loaded.

Fix:
- Load a map first, then run `bakeLightGrids`.
- Or use explicit map targets:

```text
openQ4-client_x64.exe +bakeLightGrids game/tram1 -quit
```

### `bakeLightGrids: no valid map targets were found.`

Cause:
- The supplied map names were wrong or not found by openQ4.

Fix:
- Use map paths without the `.map` extension.
- Prefer names such as `game/tram1` or `mp/q4dm1`.

### `bakeLightGrids: no valid probes were generated`

Cause:
- The generated probe layout did not find valid positions in the target areas.

Fix:
- Increase grid spacing and try again.
- Check the map in `r_showLightGrid 3` after load to see how probe validity is distributed.
- Verify the map actually loads and plays correctly before baking.

### The bake appears slow

Cause:
- This is expected on larger maps or higher-quality settings.

Fix:
- Watch the console or log instead of the viewport.
- Use lower `size`, `samples`, `blends`, or `bounce` values for iteration builds.
- Use the batch CLI workflow for unattended runs.

### Memory or virtual memory usage is high while baking

Cause:
- The bake keeps temporary probe captures, readback buffers, and atlas write buffers alive while each batch is processed.

Fix:
- Lower `r_lightGridBakeMemoryMB`. Start with `8`, then `4` if needed.
- Lower `r_lightGridBakeReadbackSlots` to `2` or `1`.
- Disable async readback with `r_lightGridBakeAsyncReadback 0` if the driver still allocates too much memory.
- Reduce `r_lightGridBakeWorkers` if you want fewer in-flight integration jobs.
- Use smaller `size` values during iteration bakes.

### I do not see any lighting change in-game

Check:
- `r_useLightGrid 1`
- the map has actually been baked
- the output files were written under `fs_savepath`
- the loaded map matches the baked map

Useful test:

```cfg
r_useLightGrid 0
r_useLightGrid 1
```

If the scene never changes, either the bake did not load or the map currently has no usable light-grid data.

### Probe points draw, but the result still looks wrong

Try:
- rebaking with a smaller `grid` spacing
- increasing `samples`
- increasing `size`
- checking that the map is using the expected saved files, not stale older bake output
- confirming `area*_lightgrid_amb.tga`, `area*_lightgrid_vis.tga`, and `area*_lightgrid_pos.tga` exist for each baked area

### I want to turn the feature off completely

```cfg
seta r_useLightGrid 0
```

To turn debug drawing off:

```cfg
r_showLightGrid 0
```

## Limitations and Expectations

openQ4's current light-grid system is intentionally scoped. End users should expect:
- indirect diffuse only
- no specular/reflection-probe lighting from this system
- no HDR/EXR bake output
- no BFG PBR pipeline
- one atlas per portal area
- one companion visibility atlas per portal area
- one compact probe-position atlas per portal area
- console/log progress rather than a fully interactive bake UI

That is by design for the current openQ4 implementation.

## Log and Console Output

During batch bakes, openQ4 reports:
- map load progress
- module switches between SP and MP
- bounce count
- area count
- probe count
- relocated and near-solid probe counts
- atlas writes
- visibility trace count
- total completion time

If you want a separate bake log:

```text
openQ4-client_x64.exe +set logFileName logs/openq4_lightgrids.log +bakeLightGrids all -quit
```

On a standard local setup, logs are written under `fs_savepath/baseoq4/logs/`.

## Related Documentation

- [README.md](../README.md)
- [docs-user/shadow-mapping.md](shadow-mapping.md)
- [TECHNICAL.md](../TECHNICAL.md)
