# Light grids

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

Light grids, also called irradiance volumes, add soft bounced light to a map: the glow a lit floor throws onto nearby walls, and the fill light that lifts dark corners. The light is worked out ahead of time ("baked") for each map and stored in light-grid files. openQ4 ships light grids for the multiplayer maps and two single-player levels, including Air Defense Trenches. Maps without them look exactly as they always did.

Players only need the first two sections. The rest is for mappers who want to bake light grids for their own maps.

## Turning light grids on or off

Light grids are on by default. Use **Settings → Display → Image Quality → Irradiance Volumes** (on the System page in the classic menus), or the console:

```cfg
seta r_useLightGrid 1
```

Set it to `0` to turn them off. The change applies at once, without `vid_restart`.

## Reducing stutter in new areas

openQ4 loads each area's light-grid data as it comes into view. On some systems that can cause a brief pause when you enter a new area. **Settings → Display → Image Quality → Preload Light Grids** loads every area's data while the map loads instead: loading takes longer and uses more video memory, but area changes are smoother. It is off by default.

The setting takes effect at the next map load, so **Apply Changes** saves it without asking you to keep it. A line under the toggle says whether the current map preloads or streams its light grids, has none, or keeps the previous setting until the next map loads. In the classic menus, the toggle is on the System page under Advanced Rendering. From the console, use `r_lightGridPreload 1`, then reload the map or load a save.

## What light grids do

- Add soft bounced light (indirect diffuse light) from baked probes: sample points spread through each area of the map.
- Keep that light from leaking through walls and around corners.
- Blend smoothly through doorways and windows between neighbouring areas.
- Light your first-person weapon from the area you're standing in.
- Feed [PBR materials](pbr-materials.md) their diffuse light, weighted by their metalness and ambient occlusion, on OpenGL and Vulkan.

They don't add reflections or shiny highlights, and the bake is standard range, not HDR. Particles, decals and other effects that aren't lit surfaces don't take light-grid light.

## Baking light grids (for mappers)

Baking captures the lighting at every probe in a map and writes the result to your save folder (`fs_savepath`). Use the client executable, `openQ4-client_x64.exe`, not the dedicated server. Progress prints to the console and the log, not on screen. The output is openQ4's own format: it doesn't read or write Doom 3 BFG's EXR light-grid data.

### Bake the map you're playing

```cfg
devmap game/tram1
bakeLightGrids
```

This bakes the loaded map with default settings and reloads the new data straight away.

### Bake many maps from the command line

Batch baking loads each map itself, switches between single-player and multiplayer game code as needed, and quits at the end if you add `-quit`:

```text
openQ4-client_x64.exe +bakeLightGrids all -quit
openQ4-client_x64.exe +bakeLightGrids all-mp -quit
openQ4-client_x64.exe +bakeLightGrids game/tram1 game/process1 mp/q4dm1 -quit
```

- Multiplayer maps need cheats enabled first, for example `openQ4-client_x64.exe +set sv_cheats 1 +bakeLightGrids all-mp -quit` (or `net_allowCheats 1`).
- Without `force`, maps whose output already exists and matches the current bake settings are skipped.
- Multiplayer baking needs a window to draw into, so openQ4 sets `net_serverDedicated 0` while it bakes.
- For an unattended overnight run with its own log, add `+set logFileName logs/openq4_lightgrids.log`. Logs go to `fs_savepath/baseoq4/logs/`.

To rebuild one map from scratch, load it and run `bakeLightGrids force`.

### Bake options

```cfg
bakeLightGrids [all | all-mp | <map> ...] [force] [-quit] [limit<num>] [bounce<num>] [size<num>] [blends<num>] [samples<num>] [separateAreas] [grid ( x y z )]
```

With no map names, the loaded map is baked.

| Option | Example | What it does |
|---|---|---|
| `all` | `bakeLightGrids all` | Bakes every map openQ4 can find. |
| `all-mp` | `bakeLightGrids all-mp` | Bakes every multiplayer map. |
| map names | `bakeLightGrids game/tram1 mp/q4dm1` | Bakes only these maps. Use the path without `.map`. |
| `force` | `bakeLightGrids force` | Deletes existing output and bakes again. |
| `-quit` | `+bakeLightGrids all -quit` | Quits when the batch finishes. |
| `limit<num>` | `limit2048` | Most probes per area before the spacing widens. |
| `bounce<num>` | `bounce2` | Number of bounces. Each extra bounce builds on the previous bake. |
| `size<num>` | `size256` | Resolution of each probe's capture. |
| `blends<num>` | `blends4` | Captures blended per view, for extra smoothing. |
| `samples<num>` | `samples256` | Samples per output pixel. More is less noisy. |
| `separateAreas` | `separateAreas` | Builds one area's probes at a time, to lower peak memory on large maps. |
| `grid x y z` | `grid 64 64 128` | Probe spacing in game units. `grid ( 64 64 128 )` also works. |

Starting points:

```text
Fast test:  openQ4-client_x64.exe +bakeLightGrids game/tram1 size64 samples32 limit1024 -quit
Balanced:   openQ4-client_x64.exe +bakeLightGrids game/tram1 size128 samples128 blends1 bounce1 grid 64 64 128 -quit
Higher:     openQ4-client_x64.exe +bakeLightGrids game/tram1 size256 samples256 blends2 bounce2 grid 48 48 96 -quit
```

Tuning:

- Raise `size` if the light looks too soft, `samples` if it looks noisy, and `blends` for extra smoothing.
- Lower the `grid` spacing if light changes too abruptly between probes. Raise `limit` if a large area's spacing widens too much.
- Leave `r_lightGridBakeWorkers 0` to use your processor cores automatically, or set a count yourself.
- Leave `r_lightGridBakeAsyncReadback 1` for the fastest bake. It works best with `blends1` and capture sizes that fit in the window.
- If memory use is high, lower `r_lightGridBakeMemoryMB` (try `8`, then `4`), then `r_lightGridBakeReadbackSlots` (`2` or `1`). For the least memory, also set `r_lightGridBakeAsyncReadback 0`, and add `separateAreas`.

### Where the files go

For `game/tram1`, the bake writes to `fs_savepath`:

```text
maps/game/tram1.lightgridpack
maps/game/tram1.lightgrid
env/maps/game/tram1/area0_lightgrid_amb.tga
env/maps/game/tram1/area0_lightgrid_vis.tga
env/maps/game/tram1/area0_lightgrid_pos.tga
...
```

- The `.lightgridpack` is the file the game uses. It holds every area's data in one file, so openQ4 can load just the areas in view.
- The `.lightgrid` file and the per-area `.tga` images are loose copies, kept as a fallback and for inspection: `amb` holds the light, `vis` the data that stops leaks through walls, and `pos` where each probe was moved to keep it out of walls.

openQ4 loads these automatically with the map, and a valid `.lightgridpack` is always preferred. Graphics chips that can't read its compressed images (OpenGL ES without S3TC, as on most Mali and PowerVR GPUs) unpack them on the processor instead, so they never need the loose images. `r_lightGridSoftwareDecode 1` forces that path on any renderer, for testing.

openQ4's own light grids ship in `baseoq4/pak1.pk4`, which holds only each map's `.lightgridpack` and `.lightgrid`.

### Checking a bake

1. Load the map and set `r_showLightGrid 1` to show the probes in your current area.
2. Check that the probes sit in the playable spaces.
3. Toggle `r_useLightGrid 0` and `1`: shadowed spaces should fill in more naturally, and the light should change smoothly between areas rather than popping.
4. Check that the files exist under `fs_savepath`.

| `r_showLightGrid` | Shows |
|---|---|
| `0` | Nothing |
| `1` | Valid probes in the current area |
| `2` | Valid probes in every area |
| `3` | Every probe, including invalid ones |

Probes that were moved out of walls draw in orange; probes still close to a wall draw in yellow. Mode `3` helps when a bake left probes invalid or sparse.

### Packing and testing packs

- `lightGridPackLoose <map>` turns a map's existing loose bake (its `.lightgrid` and `.tga` images in the save folder) into a `.lightgridpack` without baking again.
- `lightGridPackTest [map ...]` loads every map's `.lightgridpack`, or only the maps you name, as a map load would, and prints one line per map and a summary, for example `LIGHTGRID_PACK_TEST_DONE tested=51 accepted=51 rejected=0 loose_only=1`. Add `+set fs_searchAddons 1` to include the community map pack's maps when it is installed.

### Bake log

During a batch bake, the console and log show map loads, switches between single-player and multiplayer, the bounce, area and probe counts, how many probes were moved out of walls, image writes, and the total time, ending with a timing summary.

## Troubleshooting

### `bakeLightGrids: no primary world/view loaded.`

You ran `bakeLightGrids` with no map loaded and no map names. Load a map first, or name one: `openQ4-client_x64.exe +bakeLightGrids game/tram1 -quit`.

### `bakeLightGrids: no valid map targets were found.`

openQ4 couldn't find the maps you named. Use paths without the `.map` extension, such as `game/tram1` or `mp/q4dm1`.

### `bakeLightGrids: no valid probes were generated`

No probe found a valid position in the map's areas. Check that the map loads and plays normally, look at it with `r_showLightGrid 3`, and try a wider `grid` spacing.

### The bake appears slow

Large maps and high settings take time. Watch the console or log rather than the screen, use lower `size`, `samples`, `blends` or `bounce` values while you iterate, and run long batches unattended from the command line.

### Memory use is high while baking

The bake keeps probe captures and image buffers in memory while it works. Lower `r_lightGridBakeMemoryMB` (`8`, then `4`), lower `r_lightGridBakeReadbackSlots` to `2` or `1`, set `r_lightGridBakeAsyncReadback 0` if the driver still uses too much, reduce `r_lightGridBakeWorkers`, add `separateAreas`, or bake with a smaller `size`.

### I don't see any lighting change in-game

Check that `r_useLightGrid` is `1`, that the map you loaded is the one you baked, and that the files were written under `fs_savepath`. If toggling `r_useLightGrid 0` and `1` changes nothing, the map has no usable light-grid data.

### Probes draw, but the result still looks wrong

Re-bake with a smaller `grid` spacing, more `samples` or a larger `size`. Check that the map isn't loading an older bake, and that `area*_lightgrid_amb.tga`, `area*_lightgrid_vis.tga` and `area*_lightgrid_pos.tga` exist for each area.

### `bakeLightGrids` says the pack "will not load from this bake"

openQ4 ships light grids for the multiplayer maps and two single-player levels, and loads those before anything in `fs_savepath`. Your bake is complete, but the bundled copy is used when the map loads. Nothing needs fixing to play. To use your bake for that map, replace the bundled copy with it.

### A map I baked with 0.13.2 or earlier lights oddly

openQ4 0.13.2 and earlier baked correctly only when the game window was 640x480. At any other size, each probe recorded a magnified corner of its surroundings, so its light could come from the wrong direction. 0.14.1 bakes correctly at any window size: re-bake the map with `bakeLightGrids force`.

## Settings reference

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_useLightGrid` | `1` | `0..1` | Uses baked light grids where a map has them. Same as **Irradiance Volumes** in the menu. |
| `r_lightGridIntensity` | `1.0` | `0..16` | Scales the strength of light-grid light. |
| `r_lightGridPreload` | `0` | `0..1` | `1` loads every area's data while the map loads; `0` loads areas as they come into view. Same as **Preload Light Grids**. |
| `r_lightGridPortalBlend` | `64` | `0..256` | How far, in game units, light blends across doorways and windows between areas. `0` turns blending off. |
| `r_lightGridResidencyFrames` | `0` | `0..3600` | Frames to keep an area's data in video memory after it was last needed. `0` never unloads it. |
| `r_showLightGrid` | `0` | `0..3` | Shows probe positions (see [Checking a bake](#checking-a-bake)). |
| `r_lightGridSoftwareDecode` | `0` | `0..1` | Unpacks compressed light-grid images on the processor even where the graphics chip could read them. For testing. |
| `r_lightGridBakeWorkers` | `0` | `-1..8` | Processor threads used while baking. `0` picks automatically, `-1` uses none. |
| `r_lightGridBakeAsyncReadback` | `1` | `0..1` | Reads captures back from the graphics card without waiting, for faster bakes, where the driver supports it. |
| `r_lightGridBakeMemoryMB` | `12` | `4..256` | Memory the bake may use for work in progress. Lower uses less memory but can be slower. |
| `r_lightGridBakeReadbackSlots` | `0` | `0..16` | Buffers used for reading captures back. `0` picks automatically; lower uses less graphics memory. |

These settings apply without `vid_restart`.

## Related guides

- [Shadow mapping](shadow-mapping.md)
- [PBR materials](pbr-materials.md)
- [README](../../README.md)
- [Technical overview](../../TECHNICAL.md)
