<div align="center">

# openQ4 Technical Reference

</div>

This document covers technical details for advanced users and developers: compatibility status, file layout, configuration cvars, asset validation, build dependencies, versioning, and the SDK/game library structure.

For installation and a feature overview, see the [README](README.md). For building from source, see [BUILDING.md](BUILDING.md).

---

## Table of Contents

- [Quake 4 Compatibility Status](#quake-4-compatibility-status)
- [Game Directory Structure](#game-directory-structure)
- [Asset Validation](#asset-validation)
- [Advanced Configuration](#advanced-configuration)
- [Mod Manifests](#mod-manifests)
- [SDK and Game Library](#sdk-and-game-library)
- [Dependencies](#dependencies)
- [Versioning](#versioning)

---

## Quake 4 Compatibility Status

This status reflects compatibility with official Quake 4 assets (`q4base` PK4s), not proprietary game DLL compatibility.

### Compatible

- ✅ **Basic Set of Effects (BSE) Reconstruction**: Core BSE runtime behavior rebuilt and integrated so stock effects execute through the openQ4 engine/game pipeline
- ✅ **Sound Shaders**: Effect-driven sound shader paths restored, including effect sound capability checks and runtime playback behavior
- ✅ **Screen Effects**: BSE-driven screen/camera effect paths used by stock content are operational
- ✅ **Material Shaders**: Material handling compatibility restored to remove startup reliance on custom `q4base` material overrides
- ✅ **Modern Display Handling**: Automatic aspect-ratio/FOV behavior, multi-monitor targeting, and desktop-native fullscreen paths integrated
- ✅ **Steam Deck Runtime Path**: Linux SDL3 backend, controller/menu integration, and a dedicated `openQ4-steamdeck` launcher/profile are in place as of March 30, 2026
- ✅ **Stock-Asset Validation Path**: Repeated validation loops with stock assets keep parser/runtime compatibility regressions visible and actionable
- ✅ **Door/Trigger Script Progression Stability (OpenD3 Parity)**: Right-associative script compiler pointer-temp handling guards x64 storage width mismatches, preventing interpreter write corruption in affected trigger/door event chains

### In Progress

- ❌ **Ongoing Compatibility Sweep**: Additional map-by-map gameplay validation remains in progress to catch residual regressions

Current known regressions and follow-up work are tracked in [TODO.md](TODO.md) and [docs/dev/release-completion.md](docs/dev/release-completion.md).

---

## Game Directory Structure

```
openQ4/
├── openQ4-client_x64      # Main executable (.exe on Windows)
├── openQ4-ded_x64         # Dedicated server (.exe on Windows)
├── openQ4-steamdeck       # Linux Steam Deck launcher
└── baseoq4/               # Unified game directory
    ├── pak0.pk4           # Core openQ4 runtime content
    ├── pak1.pk4           # Level-related openQ4 content
    ├── game-sp_x64        # Single-player module (.dll / .so / .dylib)
    └── game-mp_x64        # Multiplayer module (.dll / .so / .dylib)
```

- **Single-player**: loads `game-sp_<arch>`
- **Multiplayer**: loads `game-mp_<arch>`
- **BSE runtime**: linked directly into `openQ4-client_<arch>`; dedicated server builds keep a disabled/stub path
- **Source-owned runtime content**: author core runtime overrides in `content/baseoq4/pak0/` and level-related content in `content/baseoq4/pak1/`
- **Generated staging output**: treat `.install/baseoq4/` as build output, not an editing target
- **Runtime identity**: the in-game directory remains `baseoq4/` even though the repo source path now lives under `content/baseoq4/`
- No separate mod folders or manual mode switching required

---

## Asset Validation

openQ4 automatically validates your Quake 4 installation to ensure you have legitimate, unmodified media files.

**How it works:**
1. Engine validates required official `q4base` media PK4 checksums at startup
2. Refuses to run if required assets are missing or modified
3. Ignores retail game-binary PK4 archives such as `game000.pk4` through `game300.pk4` and `gamex*.pk4` because openQ4 ships its own game modules
4. Allows optional official patch/menu and language PK4s when present without making them startup requirements
5. Auto-discovers your installation (checks Steam, GOG, or current directory)

**Configuration:**
- `fs_validateOfficialPaks 1` (default) — Enable asset validation
- See [official-pk4-checksums.md](docs/dev/official-pk4-checksums.md) for the checksum reference

---

## Advanced Configuration

<details>
<summary><b>Display and Graphics Settings</b></summary>

### Multi-Monitor Support
- `r_screen -1` — Auto-detect current display (default)
- `r_screen 0..N` — Select specific monitor
- Use `listDisplays` console command to see available monitors

### Display Modes
- `r_fullscreen 0|1` — Toggle fullscreen
- `r_fullscreenDesktop 1` — Desktop native fullscreen (default)
- `r_fullscreenDesktop 0` — Exclusive fullscreen (uses `r_mode`)
- `r_borderless` — Borderless windowed mode
- Use `listDisplayModes [displayIndex]` to see available modes

### Window Settings
- `r_windowWidth` / `r_windowHeight` — Window dimensions
- Aspect ratio, FOV behavior, and UI framing are automatically derived from render size

### Rendering and Post-Processing
- `r_bloom 0|1` — Toggle bloom post-processing
- `r_hdrToneMap 0|1` — Toggle HDR filmic tone mapping and color correction
- `r_ssao 0|1` — Toggle screen-space ambient occlusion
- `r_crt 0|1` — Toggle CRT emulation post-processing
- `r_crtChromatic` — Optional CRT channel convergence offset; defaults to `0` and is capped to a subtle range to avoid global RGB edge artifacts
- `r_useShadowMap 0|1` — Enable the experimental shadow-map path
- `r_shadowMapCSM 0|1` — Enable projected-light cascaded shadow maps (when shadow maps are active)
- `r_shadowMapHashedAlpha 0|1` — Hashed alpha testing for cutout/perforated shadow casters
- `r_shadowMapTranslucentMoments 0|1` — Experimental blended/translucent shadow overlay
- `r_stencilTranslucentShadows 0|1` — Let translucent materials cast and receive stencil shadows in the classic shadow-volume path (`regenerateWorld` or a map reload is required after toggling)
- `r_softParticles 0|1` — Enable optional depth-faded BSE particles so smoke/dust and additive bursts fade against solid scene depth
- `r_softParticleFadeDistance` — World-unit fade distance used when `r_softParticles` is enabled (default `64`)
- `r_enhancedMaterials 0|1` — Route eligible stock material interactions through the enhanced GLSL shading path; animated, deformed, and packed character geometry remains on the classic ARB2 interaction path for visual parity
- `r_enhancedMaterialNormalScale` — Boost tangent-space normal detail when enhanced materials are active
- `r_enhancedMaterialSpecularBoost` — Increase specular intensity when enhanced materials are active
- `r_enhancedMaterialFresnel` — Add grazing-angle fresnel to existing materials when enhanced materials are active
- `r_useRepeatedStateReuse 0|1` — Keep model-space skinned snapshots across transform-only entity updates so repeated-state presentation frames skip CPU re-skinning (default `1`; reuse hits show as `snapshotsReused` under `r_showUpdates 1`)
- `r_useRedundantStateFiltering 0|1` — Skip redundant legacy-backend GL calls (repeated program env parameters, vertex attrib array toggles, and vertex/index buffer rebinds); default `1`
- `com_forceGenericSIMD 0|1` — Force the generic scalar math path instead of the SSE2 SIMD processor used for skinning, shadow, and bounds math on x86-64 (default `0`)
- `r_hdrAutoExposureAsync 0|1` — Read the HDR auto-exposure luminance sample back asynchronously with one frame of latency instead of stalling the GPU pipeline every frame (default `1`)
- See [docs/user/shadow-mapping.md](docs/user/shadow-mapping.md) for the full shadow-map CVar reference, presets, transparency behavior, and debug modes

### Renderer Backend
- `r_renderApi best|gl|vulkan|gl-module|gles` — Rendering API; default `gl` on desktop and `gles` on Android. `best` selects that platform default. `vulkan` selects the experimental Vulkan module; `gles` selects Emile Belanger's experimental OpenGL ES 3.0 module. Changes apply on engine restart. See the [Android/GLES guide](docs/dev/android-build.md) for dependencies, limitations and SigmaTouch integration.
- See [docs/user/display-settings.md](docs/user/display-settings.md#renderer-backend-opengl-default-vulkan-is-experimental)

### Presentation Clock
- `g_presentationInterpolation 0|1` — Draw movers, and everything riding them, on the same interpolated presentation time as the first-person camera instead of the 60 Hz simulation clock (default `1`). Setting `0` puts the camera and every entity back on the simulation clock together
- `g_showPresentationPose 0|1` — Developer diagnostic: report per frame whether the local player reached the presentation clock, and the root transform, joint modifier, animation time, and drawn joint the frame used (default `0`)

### Modernization Paths (experimental, default-off)

Each of these is independently reversible, and `r_rendererModernQuality 0` rolls
back every Milestone F domain at once. They are previews rather than finished
features; see the [idTech 5 modernization roadmap](docs/dev/idtech5-modernization-roadmap.md)
for scope and the [engine capability matrix](docs/dev/engine-capability-matrix.md)
for authoritative status.

- `r_temporalAA 0|1` — Native-history temporal anti-aliasing/upscaling on OpenGL and Vulkan (default `0`); SMAA remains the immediate rollback
- `r_rendererDynamicResolution 0|1` — GPU-time driven resolution scaling (default `0`), tuned with `r_dynamicResolutionMinScale` / `r_dynamicResolutionMaxScale`
- `r_rendererModernQuality 0|1` — Master gate for the modern material and advanced-lighting domains (default `1`); `0` rolls all of them back together
- `r_pbrMaterials 0|1` — Guarded metallic/roughness lighting for dual-authored `pbr { ... }` materials (default `0`); retail materials keep their classic fallback
- `r_rendererReflectionProbes 0|1` — Authored, bounded OpenGL specular probes (default `0`); incomplete probe data returns to the analytic environment
- `r_rendererClusteredDecals 0|1` — Bounded OpenGL clustered-decal corridor (default `0`)
- `r_rendererFroxelVolumetrics 0|1` — Bounded view-aligned froxel volumetrics (default `0`)
- `r_rendererSSR 0|1` — Bounded depth-normal screen-space reflections (default `0`)
- `r_rendererSSGI 0|1` — Fixed-tap depth-derived screen-space indirect light (default `0`)
- `r_gpuSkinning 0|1` — Backend compute skinning for eligible MD5/MD5R surfaces (default `0`); CPU positions stay authoritative for gameplay and stencil volumes
- `com_levelLoadModernization 0|1` — Learned level-load preload plus generated model, world, collision, and animation caches (default `0`); private to `fs_savepath` and falls back to the installed assets
- See [docs/user/temporal-presentation.md](docs/user/temporal-presentation.md), [docs/user/advanced-screen-space-lighting.md](docs/user/advanced-screen-space-lighting.md), and [docs/user/level-load-cache.md](docs/user/level-load-cache.md)

### Resolution Scaling
- `r_screenFraction` — `10..200`; values below `100` reduce or simulate reduced resolution, while values above `100` supersample the root scene in a single-sample offscreen target and resolve to the native back buffer
- `r_resolutionScaleMode 0` — Legacy cropped viewport scaling below native resolution
- `r_resolutionScaleMode 1` — Bilinear fullscreen upscale
- `r_resolutionScaleMode 2` — High-quality fullscreen upscale + sharpening
- `r_resolutionScaleSharpness` — HQ sharpen strength (`0.0` to `1.5`)

### Shader Compatibility
- `r_interactionColorMode` — Interaction shader mode (`0` auto, `1` packed env16.xy, `2` vector env16/env17)
- `r_shaderReport 1` — Print shader summaries after startup and `vid_restart`
- `r_shaderReport 2` — Also warn when invalid ARB programs are skipped at runtime
- `reportShaderPrograms` — Print current ARB program validity plus material/shadow GLSL load state

</details>

<details>
<summary><b>Input and Controller Settings</b></summary>

### Controller Support
- `in_joystick` — Enable/disable gamepad input
- `in_joystickDeadZone` — Radial analog stick dead zone
- `in_joystickLookSensitivity` — Controller look speed scale
- `in_joystickLookCurve` — Controller look response curve
- `in_joystickMoveCurve` — Controller movement response curve
- `in_joystickInvertLook` — Invert controller look pitch
- `in_joystickSouthpaw` — Swap movement and look sticks
- `in_joystickTriggerThreshold` — Trigger sensitivity
- `in_joystickRumble` / `in_joystickRumbleScale` — Enable and scale controller rumble
- `com_platformProfile` — Startup profile selector (`default` or `steamdeck`)

### Features
- Hotplug support — connect or disconnect a controller at any time
- Dual-stick analog movement and look with radial dead-zone shaping
- Full button mapping support
- `K_JOY7` and `K_JOY8` both open the in-game menu

</details>

<details>
<summary><b>File System Paths</b></summary>

### Path Discovery Order
1. Override (if specified via cvar or command line)
2. Current working directory
3. Steam installation
4. GOG installation

On Linux, Steam auto-discovery checks `~/.steam/steam`, `~/.local/share/Steam`, and the Flatpak Steam root at `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`, then expands any extra libraries listed in `libraryfolders.vdf`.

### Path Variables
- `fs_basepath` — Game installation directory (auto-detected)
- `fs_homepath` — Writable user directory
- `fs_savepath` — Save games and configs (defaults to `fs_homepath`)
- `fs_cachepath` — Regenerable image/audio cache root (defaults to `fs_savepath`). Android hosts can use the application's cache directory without putting saves or configuration there.
- `fs_cdpath` — Locked runtime overlay path (use `.install/` as launch dir for testing)

### Manual Path Configuration

If your Quake 4 installation is not auto-detected, launch with:

```
openQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

</details>

### Crash Diagnostics

On Windows, openQ4 installs an unhandled-exception crash handler in packaged and local builds. Crashes write `openq4_crash_*.log` and `openq4_crash_*.dmp` files under a `crashes/` directory beside the executable, for example `.install/crashes/` when launching from the staged package root. Public Windows release packages include matching PDB diagnostic symbols so those dumps can be symbolized.

---

## Mod Manifests

Runnable openQ4 mods require a `mod.json` file in the root of the mod directory. This applies to `baseoq4/` as well as any external mod folder selected through the mod menu or requested by multiplayer auto-restart.

The manifest is a flat JSON object with these required string fields:

- `name`
- `version`
- `releaseDate`
- `website`
- `author`
- `requiredopenQ4Version`

`requiredopenQ4Version` is matched against the current openQ4 engine version. Mods without a manifest, or with a mismatched required engine version, are hidden from the mod menu and rejected for automatic mod switching.

Game modules are optional for mods. At runtime, openQ4 first checks the selected mod directory for the active `game-sp_<arch>` or `game-mp_<arch>` module; if it is not present, the engine falls back to the matching module in `baseoq4/`. Content-only mods therefore need a compatible `mod.json` and their content files, not copied openQ4 dynamic libraries. Mods that intentionally ship custom game code can still provide their own module in the mod directory.

Example:

```json
{
  "name": "openQ4",
  "version": "0.1.010",
  "releaseDate": "2026-04-14",
  "website": "https://www.darkmatter-quake.com",
  "author": "themuffinator / DarkMatter Productions",
  "requiredopenQ4Version": "0.1.010"
}
```

---

## SDK and Game Library

The game code is derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15) and maintained in the companion [openQ4-game](https://github.com/themuffinator/openQ4-game) repository. The SDK is subject to id Software's EULA, which permits modification for use with Quake 4 and non-commercial distribution of modifications, but prohibits commercial use and standalone game creation. For complete terms, see the [EULA](https://github.com/themuffinator/openQ4-game/blob/main/doc/legacy/EULA.Development%20Kit.rtf).

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [SDL3](https://www.libsdl.org/) | 3.4.4 | Cross-platform window/input/display |
| [GLEW](http://glew.sourceforge.net/) | 2.3.1 | OpenGL extension wrangler |
| [OpenAL Soft](https://openal-soft.org/) | 1.25.1 | 3D audio rendering |
| [stb_vorbis](https://github.com/nothings/stb) | 1.22 | Ogg Vorbis audio decoding |

All dependencies are automatically fetched and built during the Meson configure step.

---

## Versioning

openQ4 uses numeric release versions from `meson.build` and appends an explicit build track when the build is not stable:

- `stable` — release builds, e.g. `X.Y.Z`
- `dev` — default local builds, e.g. `X.Y.Z-dev+gabcdef12`
- `beta` / `rc` — optional pre-release labels, e.g. `X.Y.Z-beta.1+gabcdef12`

Published releases are currently on the `v0.6.x` line; the `0.1.010` version in `meson.build` is the internal repo version floor, not a release line. The manual GitHub release workflow treats the repo version as the minimum next release version, then consults existing stable `v*` tags plus the scale of changes since the previous release to decide whether to emit the next patch release or advance the minor release milestone. Manual release dispatch also exposes explicit `auto`, `major (x..)`, `minor (.x.)`, and `patch (..x)` bump choices. Track labels, git metadata, and Windows/macOS resource/build numbers are generated automatically.

---

[← Back to README](README.md)
