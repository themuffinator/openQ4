<div align="center">

# OpenQ4 Technical Reference

</div>

This document covers technical details for advanced users and developers: compatibility status, file layout, configuration cvars, asset validation, build dependencies, versioning, and the SDK/game library structure.

For installation and a feature overview, see the [README](README.md). For building from source, see [BUILDING.md](BUILDING.md).

---

## Table of Contents

- [Quake 4 Compatibility Status](#quake-4-compatibility-status)
- [Game Directory Structure](#game-directory-structure)
- [Asset Validation](#asset-validation)
- [Advanced Configuration](#advanced-configuration)
- [SDK and Game Library](#sdk-and-game-library)
- [Dependencies](#dependencies)
- [Versioning](#versioning)

---

## Quake 4 Compatibility Status

This status reflects compatibility with official Quake 4 assets (`q4base` PK4s), not proprietary game DLL compatibility.

### Compatible

- ✅ **Basic Set of Effects (BSE) Reconstruction**: Core BSE runtime behavior rebuilt and integrated so stock effects execute through the OpenQ4 engine/game pipeline
- ✅ **Sound Shaders**: Effect-driven sound shader paths restored, including effect sound capability checks and runtime playback behavior
- ✅ **Screen Effects**: BSE-driven screen/camera effect paths used by stock content are operational
- ✅ **Material Shaders**: Material handling compatibility restored to remove startup reliance on custom `q4base` material overrides
- ✅ **Modern Display Handling**: Automatic aspect-ratio/FOV behavior, multi-monitor targeting, and desktop-native fullscreen paths integrated
- ✅ **Steam Deck Runtime Path**: Linux SDL3 backend, controller/menu integration, and a dedicated `OpenQ4-steamdeck` launcher/profile are in place as of March 30, 2026
- ✅ **Stock-Asset Validation Path**: Repeated validation loops with stock assets keep parser/runtime compatibility regressions visible and actionable
- ✅ **Door/Trigger Script Progression Stability (OpenD3 Parity)**: Right-associative script compiler pointer-temp handling guards x64 storage width mismatches, preventing interpreter write corruption in affected trigger/door event chains

### In Progress

- ❌ **Ongoing Compatibility Sweep**: Additional map-by-map gameplay validation remains in progress to catch residual regressions

Current known regressions and follow-up work are tracked in [TODO.md](TODO.md) and [docs-dev/release-completion.md](docs-dev/release-completion.md).

---

## Game Directory Structure

```
OpenQ4/
├── OpenQ4-client_x64      # Main executable (.exe on Windows)
├── OpenQ4-ded_x64         # Dedicated server (.exe on Windows)
├── OpenQ4-steamdeck       # Linux Steam Deck launcher
└── baseoq4/               # Unified game directory
    ├── game-sp_x64        # Single-player module (.dll / .so / .dylib)
    └── game-mp_x64        # Multiplayer module (.dll / .so / .dylib)
```

- **Single-player**: loads `game-sp_<arch>`
- **Multiplayer**: loads `game-mp_<arch>`
- **BSE runtime**: linked directly into `OpenQ4-client_<arch>`; dedicated server builds keep a disabled/stub path
- **Source-owned runtime content**: author repo-managed overrides in `content/baseoq4/`
- **Generated staging output**: treat `.install/baseoq4/` as build output, not an editing target
- **Runtime identity**: the in-game directory remains `baseoq4/` even though the repo source path now lives under `content/baseoq4/`
- No separate mod folders or manual mode switching required

---

## Asset Validation

OpenQ4 automatically validates your Quake 4 installation to ensure you have legitimate, unmodified media files.

**How it works:**
1. Engine validates required official `q4base` media PK4 checksums at startup
2. Refuses to run if required assets are missing or modified
3. Ignores retail game-binary PK4 archives such as `game000.pk4` through `game300.pk4` and `gamex*.pk4` because OpenQ4 ships its own game modules
4. Auto-discovers your installation (checks Steam, GOG, or current directory)

**Configuration:**
- `fs_validateOfficialPaks 1` (default) — Enable asset validation
- See [official-pk4-checksums.md](docs-dev/official-pk4-checksums.md) for the checksum reference

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
- See [docs-user/shadow-mapping.md](docs-user/shadow-mapping.md) for the full shadow-map CVar reference, presets, transparency behavior, and debug modes

### Resolution Scaling
- `r_screenFraction` — Values below `100` reduce internal resolution; values above `100` expose supersample-style presets in the video menu
- `r_resolutionScaleMode 0` — Legacy viewport scaling (default)
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
- `in_joystickDeadZone` — Analog stick dead zone
- `in_joystickTriggerThreshold` — Trigger sensitivity
- `com_platformProfile` — Startup profile selector (`default` or `steamdeck`)

### Features
- Hotplug support — connect or disconnect a controller at any time
- Dual-stick analog movement and look
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
- `fs_cdpath` — Locked runtime overlay path (use `.install/` as launch dir for testing)

### Manual Path Configuration

If your Quake 4 installation is not auto-detected, launch with:

```
OpenQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

</details>

---

## SDK and Game Library

The game code is derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15) and maintained in the companion [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) repository. The SDK is subject to id Software's EULA, which permits modification for use with Quake 4 and non-commercial distribution of modifications, but prohibits commercial use and standalone game creation. For complete terms, see the [EULA](https://github.com/themuffinator/OpenQ4-GameLibs/blob/main/doc/legacy/EULA.Development%20Kit.rtf).

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

OpenQ4 uses numeric release versions from `meson.build` and appends an explicit build track when the build is not stable:

- `stable` — release builds, e.g. `X.Y.Z`
- `dev` — default local builds, e.g. `X.Y.Z-dev+gabcdef12`
- `beta` / `rc` — optional pre-release labels, e.g. `X.Y.Z-beta.1+gabcdef12`

The current beta release line is `0.1.010`. The manual GitHub release workflow treats the repo version as the minimum next release version, then consults existing stable `v*` tags plus the scale of changes since the previous release to decide whether to emit the next serial release or advance the middle release milestone. Track labels, git metadata, and Windows/macOS resource/build numbers are generated automatically.

---

[← Back to README](README.md)
