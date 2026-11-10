# Light Grid and Irradiance Volume Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

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
- prefers one `.lightgridpack` file per map when present
- falls back to one `.lightgrid` metadata file plus baked per-area atlas images
- samples that data at runtime when `r_useLightGrid 1`
- uses visibility moments during interpolation to reduce wall and corner light leaks
- relocates probes away from solid or near-solid map space where possible, then uses those relocated positions at runtime
- blends indirect light between adjacent visible portal areas near doorway/window boundaries
- streams packed light-grid atlases for visible areas and their portal neighbors on demand, avoiding large up-front map-load stalls and VRAM spikes
- reads the map's `.lightgridpack` into memory once while the map loads, so an area that streams in costs only its own atlas upload; that copy is dropped as soon as every atlas is resident, unless `r_lightGridResidencyFrames` may still purge some
- draws one material-selected representative diffuse stage for the indirect pass, avoiding repeated light-grid redraws on multi-diffuse materials
- lights eligible first-person weapon/viewmodel surfaces from the active view area's light grid through a dedicated weapon pass

Current scope and limits:
- diffuse-only
- not a PBR bake: [PBR materials](pbr-materials.md) receive its diffuse light,
  weighted by their metalness and ambient occlusion, on OpenGL and Vulkan
- LDR bake output
- writes `.tga` atlas images, not BFG `.exr`
- intended for openQ4's native bake/load path, not drop-in BFG asset parity
- translucent effects, decals, and other non-lighting surfaces remain outside the runtime light-grid pass

If no baked assets are found, openQ4 leaves the runtime grid empty. Starting a light-grid bake generates the requested probe layout on demand; this avoids paying the collision and visibility setup cost on every stock-map load when there is no atlas to render.

## Runtime Controls

### Core Settings

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_useLightGrid` | `1` | `0..1` | Master toggle for runtime indirect diffuse from baked light grids. |
| `r_lightGridPortalBlend` | `64` | `0..256` | World-unit radius for cross-area light-grid blending near visible portal boundaries. `0` disables portal blending. |
| `r_lightGridPreload` | `0` | `0..1` | `0` streams visible packed and loose atlas areas on demand. `1` preloads every atlas while loading the map, avoiding traversal uploads at the cost of longer loads and higher VRAM use. |
| `r_lightGridResidencyFrames` | `0` | `0..3600` | Frames to keep packed and loose atlas images resident after visible or portal-neighbor use. `0` disables runtime purging. |
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
- openQ4 writes the `.lightgridpack` runtime pack.
- openQ4 writes the `.lightgrid` metadata file.
- openQ4 writes loose fallback/debug atlases per area.
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
- Without `force`, it skips maps whose `.lightgridpack` output already exists and whose stored bake-settings/layout hash matches the current bake. If no valid pack exists, it can still skip when the older `.lightgrid` metadata and required loose atlas files are complete.
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
maps/game/tram1.lightgridpack
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
- `maps/.../*.lightgridpack`
  Preferred runtime artifact. It stores the probe metadata plus an indexed set of per-area irradiance, visibility, and probe-position image chunks in the engine's binary image payload format, letting openQ4 load the current/neighbor areas from one map-local file.
- `maps/.../*.lightgrid`
  Loose fallback/debug metadata. It stores probe layout metadata, area assignment, bounds, spacing, probe origins, and deterministic bake stats used to detect stale outputs.
- `env/maps/.../area*_lightgrid_amb.tga`
  Loose fallback/debug baked per-area indirect diffuse atlas data.
- `env/maps/.../area*_lightgrid_vis.tga`
  Loose fallback/debug baked per-area visibility and distance moments used to reduce indirect-light leakage during runtime interpolation.
- `env/maps/.../area*_lightgrid_pos.tga`
  Loose fallback/debug compact per-probe relocation offsets so runtime visibility checks use the actual baked probe positions instead of ideal grid centers.

openQ4 loads these files automatically when the corresponding map is loaded. A valid `.lightgridpack` wins on every renderer: one that cannot sample DXT textures (OpenGL ES without S3TC, as on most Mali and PowerVR GPUs) decodes the pack's DXT irradiance atlases on the CPU, so it never needs the loose TGAs. `r_lightGridSoftwareDecode 1` forces that path on any renderer for testing.

The bundled light-grid pack (`baseoq4/pak1.pk4`) therefore carries only each map's `.lightgridpack` and `.lightgrid`; the loose atlas TGAs of packed maps stay in the source tree (`content/baseoq4/pak1/env/`) for inspection, and a `.lightgrid` without a pack or atlases (which cannot light anything) is not packed either.

### Checking packs

`lightGridPackTest` loads every map's `.lightgridpack` (or only the maps you name) exactly as a map load does, uploads each area's atlases and prints one line per map and a summary:

```text
LIGHTGRID_PACK_TEST maps/mp/q4dm1 accepted areas=52 chunks=156/156 resident=52/52 decode=gpu
LIGHTGRID_PACK_TEST_DONE tested=51 accepted=51 rejected=0 loose_only=1
```

`tools/tests/lightgrid_pack_load_test.py` runs it on OpenGL, on OpenGL with software decoding and on Vulkan; `+set fs_searchAddons 1` includes the community map pack's maps when it is installed.

`lightGridPackLoose <map>` packs a map's existing loose bake (its `.lightgrid` plus the atlas TGAs under `env/<map>/` in the save path) into a `.lightgridpack` without baking again. A version-4 bake that wrote only irradiance atlases packs into irradiance-only chunks, which the runtime treats exactly as it treats those loose files.

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
- Published 0.13.2 can retain each captured probe face until the bake finishes, so large maps such as Air Defense Trenches can run out of memory partway through. Current development builds release each capture; that fix has not shipped in an available release.

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

### `bakeLightGrids` says the pack "will not load from this bake"

Cause:
- openQ4 ships light grids for the stock multiplayer maps and Air Defense Trenches, and it loads those bundled copies before anything in `fs_savepath`. Your bake is complete and correct, but when the map next loads the bundled light grid is used instead.

Fix:
- None is needed to play: the bundled light grid keeps working.
- Developers who re-bake a bundled map replace its files under `content/baseoq4/pak1/` with the bake's output and rebuild the package.

### A map I baked with 0.13.2 or earlier lights oddly

Bakes made with published openQ4 0.13.2 or earlier are only right if the game window was 640x480 while baking. At any other window size, each probe recorded a magnified corner of its surroundings instead of its full view, so its indirect light could come from the wrong direction. The window-size fix is in current development builds, not in the available 0.13.2 package.

Fix:
- On 0.13.2, use a 640x480 window when re-baking with `bakeLightGrids force`, or use a development build containing the fix.

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
- one preferred map-local runtime pack with streamable per-area binary chunks
- loose per-area atlas files retained as fallback/debug outputs
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

### Reducing traversal stutter

Use **Settings > Display > Post FX > Preload Light Grids** to load all baked light-grid atlases during the next map load. This can avoid upload-related pauses when entering new areas, at the cost of longer loading and more video memory. The default remains off. From the console, use `r_lightGridPreload 1`, then reload the map or load a save.

The modern interface's Display page offers the same **Preload Light Grids** toggle under Irradiance Volumes. **Apply Changes** saves it without asking you to keep it, since nothing changes until the next map loads. A line under the toggle tells you whether the current map preloads or streams its light grids, has none, or keeps the previous setting until the next map loads.

## Related Documentation

- [README.md](../../README.md)
- [docs/user/shadow-mapping.md](shadow-mapping.md)
- [TECHNICAL.md](../../TECHNICAL.md)
