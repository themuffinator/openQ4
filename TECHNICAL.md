<div align="center">

# openQ4 Technical Reference

</div>

> **Development reference:** This page follows current source. For the available 0.13.2 package and version-matched documentation, see [release status](docs/user/release-status.md).

This reference is for advanced players, server operators and modders. It covers the install layout, asset checks, useful console settings, file paths, mod packaging and version numbers.

For installation and a feature overview, see the [README](README.md). For building from source, see [BUILDING.md](BUILDING.md).

---

## Table of Contents

- [Compatibility](#compatibility)
- [Install Layout](#install-layout)
- [Asset Validation](#asset-validation)
- [Console Settings](#console-settings)
- [File System Paths](#file-system-paths)
- [Crash Reports](#crash-reports)
- [Mods](#mods)
- [Game Code and The Awakening](#game-code-and-the-awakening)
- [Version Numbers](#version-numbers)

---

## Compatibility

openQ4 runs the official Quake 4 game data (the `q4base` PK4s) with its own engine and game modules. It does not load the original proprietary game DLLs, so mods that depend on them are not supported.

What works with stock content:

- **Effects:** stock particle, sound and screen effects run through openQ4's own effects system.
- **Materials:** stock materials load without replacement material files.
- **Scripted sequences:** doors, triggers and scripted events progress correctly in 64-bit builds.
- **Modern displays:** automatic aspect ratio and FOV, multi-monitor selection and desktop fullscreen.
- **Steam Deck:** the Linux build includes controller and menu support plus the `openQ4-steamdeck` launcher.

Map-by-map gameplay checks are still in progress. Please report anything that breaks on the [issue tracker](https://github.com/themuffinator/openQ4/issues).

---

## Install Layout

Windows and Linux packages use this layout; on macOS the same files live inside `openQ4.app`.

```
openQ4/
├── openQ4-client_<arch>     # The game (.exe on Windows)
├── openQ4-ded_<arch>        # Dedicated server (.exe on Windows)
├── openQ4-steamdeck         # Steam Deck launcher (Linux)
├── renderer-gl_<arch>       # OpenGL renderer (.dll / .so)
├── renderer-vk_<arch>       # Vulkan renderer (.dll / .so / .dylib): stable on Windows, experimental elsewhere
└── baseoq4/                 # The single openQ4 game directory
    ├── pak0.pk4             # openQ4 core runtime content
    ├── pak1.pk4             # openQ4 light grids (clients only)
    ├── pak2.pk4             # openQ4 loadscreens (clients only)
    ├── mod.json             # Game-directory manifest
    ├── game-sp_<arch>       # Single-player module (.dll / .so / .dylib)
    └── game-mp_<arch>       # Multiplayer module (.dll / .so / .dylib)
```

- Single-player loads `game-sp_<arch>` and multiplayer loads `game-mp_<arch>`. Quake 4 and The Awakening both use the same single-player module.
- Your retail game data stays in the Quake 4 install's `q4base` directory; openQ4 reads it from there. Optional Awakening content goes in its own `q4xbase` directory, described in the [campaign guide](docs/user/campaigns.md).
- Windows packages also include `OpenAL32.dll` and `.pdb` debug symbols. Linux debug symbols are a separate download.
- When upgrading, replace the whole package. The engine, renderer modules and game modules must come from the same release.

---

## Asset Validation

openQ4 checks that your Quake 4 installation has legitimate, unmodified game data.

1. At startup it verifies the checksums of the required official `q4base` media PK4s.
2. It refuses to run if required assets are missing or modified.
3. It ignores the retail game-code PK4s (`game000.pk4` to `game300.pk4` and `gamex*.pk4`), because openQ4 ships its own game modules.
4. Optional official patch, menu and language PK4s are used when present but are not required.
5. It finds your installation automatically in Steam, GOG or the current directory.

`fs_validateOfficialPaks 1` (the default, set at startup) enables these checks. The [official PK4 checksum reference](docs/dev/official-pk4-checksums.md) lists the expected files.

openQ4 also checks its own `baseoq4/pak0.pk4`, `pak1.pk4` and `pak2.pk4` against checksums built into the executable, and refuses to start if one is missing or modified; for the light-grid and loadscreen packs the message is in the player's language and says where to get the pack. A dedicated server needs only `pak0.pk4`. A pack that passed is remembered in `generated/packs/verified.txt` under the save path and read again only after the file changes, so later launches skip reading the 800 MB of light grids and loadscreens. `+set fs_cacheVerifiedPaks 0` checks every pack at every launch. See [Runtime Pack Verification](docs/dev/runtime-pack-verification.md).

---

## Console Settings

Many of these are also in the in-game Settings menu. The [player guides](README.md#player-guides) explain each area in more depth.

### Display

- `r_screen -1` — use the current display (default); `r_screen 0..N` picks a monitor. `listDisplays` lists them.
- `r_fullscreen 0|1` — windowed or fullscreen.
- `r_fullscreenDesktop 1` — desktop fullscreen (default); `0` uses exclusive fullscreen at `r_mode`. `listDisplayModes [displayIndex]` lists the available modes.
- `r_borderless` — borderless window.
- `r_windowWidth` / `r_windowHeight` — window size. Aspect ratio, FOV and HUD framing follow the render size automatically.

See [Display Settings](docs/user/display-settings.md).

### Resolution Scaling

- `r_screenFraction 10..200` — render scale in percent. Below `100` renders fewer pixels; above `100` supersamples.
- `r_resolutionScaleMode` — `0` legacy cropped viewport, `1` bilinear upscale, `2` high-quality upscale with sharpening, `3` nearest-neighbour upscale. The single-player game presents its below-native scene through `idRenderSystem::PresentScaledScene` so modes `2` and `3` reach it; multiplayer and other direct views use the renderer's own scene target.
- `r_resolutionScaleSharpness 0.0..1.5` — sharpening strength for mode `2`.

### Renderer

- `r_renderApi best|gl|vulkan|gl-module|gles` selects the renderer. Fresh desktop `best` prefers compatible high-performance Vulkan hardware with OpenGL fallback; existing explicit backend choices persist. Vulkan is stable on Windows and experimental on Linux/macOS. Android defaults to GLES3. `r_renderDevice auto|<UUID>` selects the GPU; `rendererDevices` lists stable IDs. Use the retained System menu to Apply and Restart Renderer, or restart the process after console changes. See [Rendering Backend and Device](docs/user/display-settings.md#rendering-backend-and-device).
- `g_presentationInterpolation 0|1` — draw the camera, weapons and moving objects smoothly between the game's 60 Hz ticks on high-refresh displays (default `1`). `0` returns everything to the simulation clock.

### Post-Processing and Materials

- `r_bloom 0|1` — bloom.
- `r_hdrToneMap 0|1` — HDR filmic tone mapping and colour correction.
- `r_ssao 0|1` — screen-space ambient occlusion.
- `r_crt 0|1` — CRT monitor filter. `r_crtChromatic` adds a subtle colour-channel offset (default `0`).
- `r_softParticles 0|1` — fade smoke, dust and bursts softly where they meet walls and floors. `r_softParticleFadeDistance` sets the fade distance in world units (default `64`).
- `r_enhancedMaterials 0|1` — enhanced shading for eligible stock materials; animated and character surfaces keep the classic look. Tune with `r_enhancedMaterialNormalScale`, `r_enhancedMaterialSpecularBoost` and `r_enhancedMaterialFresnel`.

### Shadows

- `r_useShadowMap 0|1` — shadow maps instead of classic stencil shadows; the `quality` and `ultra` performance presets turn it on.
- `r_shadowMapFilterMode 0|1|2` — projected-light filtering: fixed PCF, rotated Poisson, or PCSS-lite (default), with `r_shadowMapFilterRadius` (default `2.0`) as its minimum radius.
- `r_shadowMapCSM 0|1` — cascaded shadow maps for distant outdoor lighting.
- `r_shadowMapHashedAlpha 0|1` — shadows from cutout surfaces such as grates and fences.
- `r_shadowMapTranslucentMoments 0|1` — experimental shadows from translucent surfaces.
- `r_stencilTranslucentShadows 0|1` — let translucent materials cast and receive classic stencil shadows. Reload the map (or run `regenerateWorld`) after changing it.

See [Shadow Mapping](docs/user/shadow-mapping.md) for presets and troubleshooting.

### Experimental Previews

These are off by default, and each can be turned on independently:

- `r_temporalAA 0|1` — temporal anti-aliasing and upscaling.
- `r_rendererDynamicResolution 0|1` — adjust resolution automatically to hold frame time, within `r_dynamicResolutionMinScale` and `r_dynamicResolutionMaxScale`.
- `r_pbrMaterials 0|1` — physically based lighting for materials authored with PBR data; retail materials keep their classic look.
- `r_rendererReflectionProbes 0|1` — authored reflection probes (OpenGL).
- `r_rendererClusteredDecals 0|1` — clustered decals (OpenGL).
- `r_rendererFroxelVolumetrics 0|1` — volumetric lighting.
- `r_rendererSSR 0|1` — screen-space reflections.
- `r_rendererSSGI 0|1` — screen-space indirect light.
- `r_gpuSkinning 0|1` — animate eligible models on the GPU.
- `com_levelLoadModernization 0|1` — local level-load caches.

`r_rendererModernQuality 0` switches off the modern material and lighting previews together (default `1`). See [Temporal AA and Dynamic Resolution](docs/user/temporal-presentation.md), [Advanced Screen-Space Lighting](docs/user/advanced-screen-space-lighting.md), [PBR Materials](docs/user/pbr-materials.md) and [Level-Load Cache](docs/user/level-load-cache.md).

### Shader Troubleshooting

- `r_shaderReport 1` — print a shader summary after startup and `vid_restart`; `2` also warns when an invalid shader program is skipped.
- `reportShaderPrograms` — print the current shader program status.
- `r_interactionColorMode` — interaction shader colour mode (`0` auto, `1` packed, `2` vector). Leave on `0` unless asked to test.

### Controllers

- `in_joystick` — enable or disable gamepad input.
- `in_joystickDeadZone` — analog stick dead zone.
- `in_joystickLookSensitivity` — look speed.
- `in_joystickLookCurve` / `in_joystickMoveCurve` — look and movement response curves.
- `in_joystickInvertLook` — invert look pitch.
- `in_joystickSouthpaw` — swap the movement and look sticks.
- `in_joystickTriggerThreshold` — trigger sensitivity.
- `in_joystickRumble` / `in_joystickRumbleScale` — enable and scale rumble.
- `com_platformProfile` — startup profile (`default` or `steamdeck`).

Controllers can be connected or disconnected at any time, every button can be bound, and `K_JOY7` and `K_JOY8` both open the in-game menu. See [Input Settings](docs/user/input-settings.md).

---

## File System Paths

openQ4 looks for your Quake 4 installation in this order:

1. A path you set with a cvar or on the command line
2. The current working directory
3. Steam
4. GOG

On Linux, Steam discovery checks `~/.steam/steam`, `~/.local/share/Steam` and the Flatpak Steam root at `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`, plus any extra libraries listed in `libraryfolders.vdf`.

- `fs_basepath` — Quake 4 installation directory (detected automatically).
- `fs_homepath` — writable user directory.
- `fs_savepath` — saves and configuration (defaults to `fs_homepath`). Each game directory keeps its own folder here, so Quake 4 uses `baseoq4` and The Awakening uses `q4xbase`.
- `fs_cachepath` — regenerable image and audio caches (defaults to `fs_savepath`). Safe to delete.
- `fs_awakeningpath` — optional location of The Awakening's content; see the [campaign guide](docs/user/campaigns.md).

If your Quake 4 installation is not found automatically, launch with:

```
openQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

---

## Crash Reports

On Windows, a crash writes `openq4_crash_*.log` and `openq4_crash_*.dmp` files to a `crashes/` folder beside the executable. Release packages include matching debug symbols, so please attach both files when [reporting a crash](https://github.com/themuffinator/openQ4/issues). On macOS, follow the [macOS support-data guide](docs/user/macos-support-data.md).

---

## Mods

Every runnable mod needs a `mod.json` file in its root directory. This includes `baseoq4/` itself, mods picked from the Mods menu, and mods a multiplayer server switches to.

The manifest is a flat JSON object with these required string fields:

- `name`
- `version`
- `releaseDate`
- `website`
- `author`
- `requiredopenQ4Version` — the oldest openQ4 version the mod works with, as `major.minor.patch`

Mods without a valid manifest, or that need a newer openQ4 than the one running, are hidden from the Mods menu and cannot be switched to automatically.

Example:

```json
{
  "name": "My Mod",
  "version": "1.0.0",
  "releaseDate": "2026-10-01",
  "website": "https://example.com",
  "author": "Your Name",
  "requiredopenQ4Version": "0.13.2"
}
```

Game modules are optional. openQ4 first looks for `game-sp_<arch>` or `game-mp_<arch>` in the mod's directory and otherwise uses the one in `baseoq4/`, so a content-only mod needs just its `mod.json` and content files. Mods with their own game code can ship their own modules.

A mod built on the retail base (`fs_game_base` other than `baseoq4`) still gets openQ4's runtime content: the filesystem searches `baseoq4/` between the mod and `q4base/`, so openQ4's shaders, GUIs and fonts remain available.

When a mod ships a declaration file with the same name as one beneath it (for example `def/player.def`), openQ4 reads the mod's copy first and then the copies below it. The mod's definitions win, and the base file's other definitions are still found. Set `decl_layerModFiles 0` at startup to have a mod's file hide the base file entirely, as in the original game.

---

## Game Code and The Awakening

openQ4's game code is derived from the Quake 4 SDK and lives in [`src/game`](src/game/) and [`src/mpgame`](src/mpgame/). It remains under the [Quake 4 SDK EULA](LICENSES/QUAKE-4-SDK-EULA.rtf), while the engine is GPL-licensed. [LICENSING.md](LICENSING.md) explains the separate terms.

Support for **Quake 4: The Awakening** is written independently for openQ4 and built into the same single-player module as Quake 4. It is active only while The Awakening campaign is running, keeps its saves and settings separate, and is removed when you return to Quake 4, Arena or multiplayer. The expansion's original game DLLs and older openQ4 `q4xbase` modules are ignored, and its multiplayer changes are not supported. See the [campaign guide](docs/user/campaigns.md).

The Awakening was developed by Raven Software and Ritual Entertainment, and recovered and published by Justin Marshall. openQ4 includes none of its content.

---

## Version Numbers

Release builds report a plain version such as `X.Y.Z`. Development and pre-release builds add a track and the source commit, for example `X.Y.Z-dev+gabcdef12` or `X.Y.Z-beta.1+gabcdef12`. Type `si_version` in the console to see the running build's version, and include it in bug reports.

A release's version number is assigned when it is published, so a build from source can report an older number than the latest release.

---

[← Back to README](README.md)
