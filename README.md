<div align="center">

<img src="assets/img/banner.png" alt="OpenQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Versioning](https://img.shields.io/badge/versioning-semver%20%2B%20tracks-green.svg)](https://github.com/themuffinator/OpenQ4)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenQ4)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenQ4)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**A modern, full binary replacement for Quake 4 with contemporary rendering and display support**

[Features](#features) • [Compatibility](#quake-4-compatibility-status) • [Quick Start](#quick-start) • [Building](#building-from-source) • [Documentation](#documentation) • [TODO](TODO.md) • [Credits](#credits)

</div>

---

> [!WARNING]
> **Development Notice:** This project leans heavily on exploratory, agentic AI "vibe coding" for speed. If you want a strictly engineered, process-heavy codebase, this probably isn't for you.

---

## About

The **OpenQ4 Project** is a complete replacement for the Quake 4 engine and game binaries. Built on the foundation of [Quake4Doom](https://github.com/idSoftware/Quake4Doom), OpenQ4 is focused on making Quake 4 feel native on modern hardware without losing stock-asset compatibility. Current work already brings in bloom, HDR tone mapping, SSAO, automatic aspect-ratio handling, multi-monitor support, and an in-progress shadow-mapping path with CSM work underway, alongside broader platform and tooling modernization.

## Versioning

OpenQ4 uses semantic base versions from `meson.build` and appends an explicit build track:

- `stable`: release builds such as `X.Y.Z`
- `dev`: default local builds such as `X.Y.Z-dev+gabcdef12`
- prerelease labels like `nightly`, `beta`, or `rc`: for example `X.Y.Z-nightly.20260307.1+gabcdef12`

Meson exposes this through `-Dversion_track=<label>` and `-Dversion_iteration=<dot-separated-iteration>`. Local builds default to `dev`; CI nightlies set `version_track=nightly`.

The base semantic version stays intentionally manual and must be bumped in `meson.build` when OpenQ4 moves to the next release line. Track labels, iterations, git metadata, and resource build numbers are generated automatically.

### What You Need

To play OpenQ4, you need:
- A legitimate copy of Quake 4 ([Steam](https://store.steampowered.com/app/2210/) or [GOG](https://www.gog.com/game/quake_iv) version recommended)
- The latest OpenQ4 release (from this repository)
- A modern 64-bit operating system

---

> [!NOTE]
> **OpenQ4** does NOT include game assets. You must own Quake 4 to play. The engine will automatically detect your Quake 4 installation from Steam or GOG. OpenQ4 is not compatible with legacy Quake 4 game mods.

---

<p align="center">
  <img src="assets/img/shot1.png" alt="OpenQ4 gameplay screenshot showing in-engine combat" width="92%">
</p>
<p align="center"><sub>OpenQ4 running with stock Quake 4 assets.</sub></p>

---

## Features

### Core Features
- **Full Game Support**: Complete single-player campaign and multiplayer modes in one engine stack
- **Unified Game Directory**: Single `openq4/` directory for both SP and MP game binaries
- **Stock Asset Compatibility**: Built to run against official Quake 4 assets instead of shipping replacement content
- **Auto-Discovery and Validation**: Smart Steam/GOG detection plus official asset verification

### Rendering Upgrades
- **Bloom**: Tunable post-process bloom for stronger highlights and a more modern presentation
- **HDR Tone Mapping**: Filmic-style HDR tonemapping and color controls for exposure, contrast, saturation, and vibrance
- **SSAO**: Screen-space ambient occlusion for added depth and contact shadowing in the final frame
- **CRT Emulation**: Optional CRT post-process with scanlines, mask, curvature, and chromatic offset controls
- **Shadow Mapping Pipeline**: Experimental shadow-map support for projected and point lights, with cascaded shadow maps (CSM) actively under development
- **Resolution Scaling and Supersample Controls**: Screen-fraction rendering supports lower-resolution upscale modes and menu-exposed supersample-style presets for image-quality tuning
- **Modern AA and Upscaling**: MSAA, SMAA, and high-quality resolution-scaling paths for cleaner output across a wide range of hardware

### Display and UX Improvements
- **Automatic Aspect-Ratio Management**: UI, FOV, zoom behavior, and view-weapon framing adapt from live render size instead of legacy manual aspect toggles
- **Multi-Monitor and Multi-Screen Support**: Target specific displays, auto-detect the active monitor, and keep window/UI behavior sane across modern desktop setups
- **Modern Display Modes**: Fullscreen, borderless windowed, exclusive fullscreen, and desktop-native fullscreen paths
- **Controller Support**: Full gamepad/joystick support with hotplug and analog controls
- **Responsive UI**: Interface scaling and layout behavior that hold up on 4:3, widescreen, ultrawide, and multi-screen environments

### Platform and Engine Modernization
- **SDL3 Backend**: Modern cross-platform input, windowing, and display handling
- **Modern System Compatibility**: 64-bit-first runtime and build targets for current Windows, Linux, and macOS environments
- **Audio**: Support for WAV and Ogg Vorbis formats with [OpenAL Soft](https://openal-soft.org/)
- **C++23**: Modern C++ standards for better performance and maintainability
- **Meson Build System**: Fast, reliable builds with dependency management
- **Crash Diagnostics**: Automatic crash dumps and logs for debugging
- **OpenGL Renderer Modernization**: Expanded renderer paths and post-processing while preserving classic Quake 4 behavior

<p align="center">
  <img src="assets/img/shot2.png" alt="OpenQ4 gameplay screenshot showing dynamic combat scene" width="49%">
  <img src="assets/img/shot3.png" alt="OpenQ4 gameplay screenshot showing environment detail and lighting" width="49%">
</p>
<p align="center"><sub>Modern renderer upgrades and modern-system support while preserving classic Quake 4 gameplay.</sub></p>

---

## Quake 4 Compatibility Status

This status focuses on compatibility with official Quake 4 assets (`q4base` PK4s), not proprietary game DLL compatibility.

### Compatible
- ✅ **Basic Set of Effects (BSE) Reconstruction**: Core BSE runtime behavior has been rebuilt and integrated so stock effects execute through the OpenQ4 engine/game pipeline.
- ✅ **Sound Shaders**: Effect-driven sound shader paths are restored, including effect sound capability checks and runtime playback behavior.
- ✅ **Screen Effects**: BSE-driven screen/camera effect paths used by stock content are operational in current OpenQ4 builds.
- ✅ **Material Shaders**: Material handling compatibility has been restored to remove startup reliance on custom repo-side `q4base` material overrides.
- ✅ **Modern Display Handling**: Automatic aspect-ratio/FOV behavior, multi-monitor targeting, and desktop-native fullscreen paths are integrated into the compatibility baseline for current systems.
- ✅ **Stock-Asset Validation Path**: Repeated validation loops with stock assets have been used to keep parser/runtime compatibility regressions visible and actionable.
- ✅ **Door/Trigger Script Progression Stability (OpenD3 Parity)**: Right-associative script compiler pointer-temp handling now guards x64 storage width mismatches (4-byte object-ref temp vs 8-byte pointer temp) by allocating pointer-sized result defs when required, preventing interpreter write corruption in affected trigger/door event chains.

### Unresolved
- ❌ **Ongoing Compatibility Sweep**: Additional map-by-map gameplay validation remains in progress to catch residual non-script regressions.

Current known compatibility regressions and follow-up work are tracked in [TODO.md](TODO.md) and [docs-dev/release-completion.md](docs-dev/release-completion.md).

---

## Quick Start

### Prerequisites
- **Quake 4** installed ([Steam](https://store.steampowered.com/app/2210/) or [GOG](https://www.gog.com/game/quake_iv))
- **Windows**: Visual Studio 2026+ (or MSVC 19.46+)
- **Linux**: GCC 13+ or Clang 17+
- **macOS**: Xcode 16+ (Clang 17+)

- **Build Tools**: [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)

> [!NOTE]
> Windows release packaging is intended to use the static MSVC CRT so end users do not need separate Visual C++ redistributable installs for OpenQ4 itself.

> [!NOTE]
> Linux runtime currently uses an X11/GLX platform path. On Wayland desktops, run OpenQ4 through XWayland (`DISPLAY` must be available).

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/themuffinator/OpenQ4.git
   cd OpenQ4
   ```

2. **Build the engine**

   **Windows (PowerShell)**
   ```powershell
   # Setup the build
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

   # Compile
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

   # Install (optional - creates distributable package)
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
   ```

   **Linux / macOS (Terminal)**
   ```bash
   # Setup the build
   bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

   # Compile
   bash tools/build/meson_setup.sh compile -C builddir

   # Install (optional - creates distributable package)
   bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects
   ```

3. **Run the game**

   **Windows**
   ```powershell
   builddir/OpenQ4-client_x64.exe
   ```

   **Linux / macOS**
   ```bash
   builddir/OpenQ4-client_x64
   ```

The engine will automatically find your Quake 4 installation and validate the game files.

> [!NOTE]
> `tools/build/meson_setup.ps1` automatically runs `tools/build/sync_icons.py` before `setup`, `compile`, and `install` to validate and generate the canonical icon set in `assets/icons/` (including required PNG sizes). Set `OPENQ4_SKIP_ICON_SYNC=1` to bypass this in local workflows.
> BSE is built into `OpenQ4-client_<arch>`. Public Windows packages should still be self-contained by shipping the required runtime DLLs next to the executable.

---

## Building from Source

<details>
<summary><b>Detailed Build Instructions</b></summary>

### Requirements
- **Meson** (>= 1.2.0)
- **Ninja** build system
- **C++23** compatible compiler:
  - **Windows**: Visual Studio 2026 (MSVC 19.46+)
  - **Linux**: GCC 13+ or Clang 17+
  - **macOS**: Xcode 16+ (Clang 17+)

> [!NOTE]
> Windows release packaging is intended to use the static MSVC CRT so end users do not need separate Visual C++ redistributable installs for OpenQ4 itself.

### Build Options
```
-Dbuild_engine=true|false     # Build OpenQ4-client_<arch> and OpenQ4-ded_<arch> executables
-Dbuild_games=true|false      # Build game modules
-Dbuild_game_sp=true|false    # Build single-player module
-Dbuild_game_mp=true|false    # Build multiplayer module
-Denforce_msvc_2026=true      # Enforce MSVC 2026+ requirement (Windows only, optional)
```

### Build Commands

**Windows (PowerShell)**
```powershell
# Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype=release

# Build
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# Create distributable package
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

**From Visual Studio Developer Command Prompt**
```batch
meson setup builddir . --backend ninja --buildtype=release
meson compile -C builddir
```

**Linux / macOS (Terminal)**
```bash
# Configure
bash tools/build/meson_setup.sh setup builddir . --backend ninja --buildtype=release

# Build
bash tools/build/meson_setup.sh compile -C builddir

# Create distributable package
bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects
```

### Output Files

**Build directory** (`builddir/`):
- `OpenQ4-client_x64` (`.exe` on Windows) - Main engine executable
- `OpenQ4-ded_x64` (`.exe` on Windows) - Dedicated server
- `openq4/game-sp_x64` (`.dll` / `.so` / `.dylib`) - Single-player game module
- `openq4/game-mp_x64` (`.dll` / `.so` / `.dylib`) - Multiplayer game module
- BSE is linked into `OpenQ4-client_x64`; the dedicated server keeps the disabled stub path and does not ship a separate BSE binary.
- On Windows, wrapper-driven builds also stage `OpenAL32.dll` and the required MSVC/UCRT runtime DLLs next to the executables so the tree is runnable without installing extra redistributables.

**Install directory** (`.install/`):
- Complete distributable package with all binaries
- Ready for deployment or testing with `fs_cdpath`
- On Windows, `.install/` is intended to be self-contained for end users. Do not require separate VC++ Redistributable or OpenAL installs for public builds.

</details>

---

## Game Directory Structure

OpenQ4 uses a unified game directory approach:

```
OpenQ4/
├── OpenQ4-client_x64      # Main executable (.exe on Windows)
├── OpenQ4-ded_x64         # Dedicated server (.exe on Windows)
└── openq4/                # Unified game directory
    ├── game-sp_x64        # Single-player module (.dll / .so / .dylib)
    └── game-mp_x64        # Multiplayer module (.dll / .so / .dylib)
```

The engine automatically selects the correct module based on game mode:
- **Single-player**: Loads `game-sp_<arch>` (for example `game-sp_x64`)
- **Multiplayer**: Loads `game-mp_<arch>` (for example `game-mp_x64`)
- **BSE runtime**: Linked directly into `OpenQ4-client_<arch>`; dedicated server builds keep the disabled/stub manager path because effect playback depends on client-side renderer/audio state
- **Windows runtime UX**: Public Windows packages must be self-contained. Ship the required runtime DLLs in the package; do not expect users to install VC++ or OpenAL separately. Do not distribute raw `buildtype=debug` artifacts.

No need for separate mod folders or manual switching!

---

## SDK and Game Library

The game code in OpenQ4 is derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15), which is distributed under id Software's End User License Agreement. The SDK source code is maintained in the companion [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) repository.

### SDK EULA Summary

The Quake 4 SDK is provided under id Software's EULA, which permits:
- Modification of the SDK code for use with Quake 4
- Creation of custom game modifications
- Non-commercial distribution of modifications

**Important Restrictions:**
- SDK code cannot be used for commercial purposes without id Software permission
- SDK code cannot be used to create standalone games
- Modified code must be used only with a legitimate copy of Quake 4
- Original id Software and Raven Software copyrights must be preserved

For complete terms, refer to the [EULA](https://github.com/themuffinator/OpenQ4-GameLibs/blob/main/doc/legacy/EULA.Development%20Kit.rtf).

### Game Library Repository

The [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) repository contains:
- SDK-derived game code for single-player and multiplayer
- Synchronized automatically during OpenQ4 builds
- Maintained separately to clearly identify SDK-licensed components

---

## Project Goals

### Primary Objectives
- Complete code replacement for Quake 4 (engine + game code)
- Support genuine Quake 4 assets without redistribution
- Feature parity for single-player and multiplayer
- Modernize rendering, audio, and platform support
- Full support for Windows, Linux, and macOS (x64 baseline)

### Non-Goals
- Binary compatibility with proprietary Quake 4 DLLs
- Support for third-party mods built against original SDK

OpenQ4 maintains complete freedom to evolve independently while preserving compatibility with official Quake 4 content.

---

## Documentation

- [Platform Support](docs-dev/platform-support.md) - Cross-platform roadmap and status
- [SDL3 Linux/macOS Migration](docs-dev/sdl3-linux-macos-migration.md) - Staged backend convergence plan for non-Windows platforms
- [Display Settings](docs-user/display-settings.md) - Multi-monitor and display configuration
- [Multiplayer Networking](docs-user/multiplayer-networking.md) - MP lag compensation and prediction cvars
- [Input Key Matrix](docs-dev/input-key-matrix.md) - Keyboard and controller input reference
- [Release Completion](docs-dev/release-completion.md) - Release checklist and changelog
- [Project TODO](TODO.md) - Known issues and upcoming features

---

## Asset Validation

OpenQ4 automatically validates your Quake 4 installation to ensure you have legitimate, unmodified game files. This protects the multiplayer experience and ensures compatibility.

**How it works:**
1. Engine validates official `q4base` PK4 checksums at startup
2. Refuses to run if required assets are missing or modified
3. Auto-discovers your Quake 4 installation (checks Steam, GOG, or current directory)
4. Uses proper paths for configuration and save files

**Configuration:**
- `fs_validateOfficialPaks 1` (default) - Enable asset validation
- See [official-pk4-checksums.md](docs-dev/official-pk4-checksums.md) for checksum reference

---

## Advanced Configuration

<details>
<summary><b>Display and Graphics Settings</b></summary>

### Multi-Monitor Support
- `r_screen -1` - Auto-detect current display (default)
- `r_screen 0..N` - Select specific monitor
- Use `listDisplays` console command to see available monitors

### Display Modes
- `r_fullscreen 0|1` - Toggle fullscreen
- `r_fullscreenDesktop 1` - Desktop native fullscreen (default)
- `r_fullscreenDesktop 0` - Exclusive fullscreen (uses `r_mode`)
- `r_borderless` - Borderless windowed mode
- Use `listDisplayModes [displayIndex]` to see available modes

### Window Settings
- `r_windowWidth` / `r_windowHeight` - Window dimensions
- Aspect ratio, FOV behavior, and UI framing are automatically derived from render size

### Advanced Graphics
- `r_bloom 0|1` - Toggle bloom post-processing
- `r_hdrToneMap 0|1` - Toggle HDR tonemapping and color correction
- `r_ssao 0|1` - Toggle screen-space ambient occlusion
- `r_crt 0|1` - Toggle CRT emulation post-processing
- `r_useShadowMap 0|1` - Enable the experimental shadow-map path
- `r_shadowMapCSM 0|1` - Enable experimental projected-light cascaded shadow maps when shadow maps are active
- `r_interactionColorMode` - Shader compatibility mode
  - `0` - Auto-detect from interaction.vfp
  - `1` - Packed env16.xy
  - `2` - Vector env16/env17
- `r_shaderReport`
  - `0` - Off (default)
  - `1` - Print shader summaries after startup and `vid_restart` / `reloadARBprograms`
  - `2` - Also warn when invalid ARB programs are skipped at runtime
- `reportShaderPrograms` - Print current ARB program validity plus shadow GLSL load state
- If a required interaction shader fails to load, OpenQ4 enters a rescue path:
  - interaction passes are skipped instead of binding the invalid program
  - the final scene gets a small ambient floor lift to avoid a flat-black world while you diagnose the shader failure
- Screen-fraction scaling (`r_screenFraction`)
  - Values below `100` reduce internal resolution for performance
  - Values above `100` are exposed as supersample-style presets in the video settings for image-quality tuning
  - `r_resolutionScaleMode 0` - Legacy viewport scaling (default)
  - `r_resolutionScaleMode 1` - Bilinear fullscreen upscale
  - `r_resolutionScaleMode 2` - High-quality fullscreen upscale + sharpening
  - `r_resolutionScaleSharpness` - HQ sharpen strength (`0.0` to `1.5`)

</details>

<details>
<summary><b>Input and Controller Settings</b></summary>

### Controller Support
- `in_joystick` - Enable/disable gamepad
- `in_joystickDeadZone` - Analog stick dead zone
- `in_joystickTriggerThreshold` - Trigger sensitivity

### Controller Features
- Hotplug support (connect/disconnect anytime)
- Dual-stick analog movement and look
- Full button mapping support

</details>

<details>
<summary><b>File System Paths</b></summary>

### Path Discovery Order
1. Override (if specified)
2. Current working directory
3. Steam installation
4. GOG installation

### Path Variables
- `fs_basepath` - Game installation directory (auto-detected)
- `fs_homepath` - Writable user directory
- `fs_savepath` - Save games and configs (defaults to `fs_homepath`)
- `fs_cdpath` - Locked runtime overlay path (current working directory; use `.install/` as launch dir for testing)

### Shader Triage
- Debug launches in this repo write `logs/openq4.log` under the active save path; with the current launch configs that is typically `${workspaceFolder}\\.home\\q4base\\logs\\openq4.log`
- For flat-black world rendering, start with `reloadARBprograms` and then `reportShaderPrograms`
- `r_shaderReport 2` enables one-shot warnings when invalid ARB programs are skipped during gameplay

</details>

<p align="center">
  <img src="assets/img/shot4.png" alt="OpenQ4 gameplay screenshot showing atmospheric environment" width="92%">
</p>
<p align="center"><sub>Built for modern displays, modern GPUs, and modern systems without changing the original game feel.</sub></p>

---

## Dependencies

OpenQ4 manages dependencies through Meson subprojects:

| Library | Version | Purpose |
|---------|---------|---------|
| [SDL3](https://www.libsdl.org/) | 3.4.0 | Cross-platform window/input/display |
| [GLEW](http://glew.sourceforge.net/) | 2.3.1 | OpenGL extension wrangler |
| [OpenAL Soft](https://openal-soft.org/) | 1.25.1 | 3D audio rendering |
| [stb_vorbis](https://github.com/nothings/stb) | 1.22 | Ogg Vorbis audio decoding |

All dependencies are automatically handled during the build process - no manual setup required!

---

## Debugging and Development

### Crash Diagnostics
Debug builds (`buildtype=debug`) include automatic crash handling:
- Crash logs saved to `crashes/*.log`
- Memory dumps saved to `crashes/*.dmp`
- Timestamps included for easy identification

### Companion Repository
The game library source code is maintained separately in [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs):
- Expected location: `../OpenQ4-GameLibs`
- OpenQ4 consumes this source directly at configure/build time (no in-repo `src/game` mirror)
- Optional game library builds with `OPENQ4_BUILD_GAMELIBS=1`

### Build Automation
- Missing or stale build directories are auto-regenerated (Windows wrapper)
- Visual Studio environment auto-detected and loaded (Windows only)
- On Linux/macOS, use `meson` commands directly (no wrapper required)
- Use `OPENQ4_GAMELIBS_REPO=<path>` to override repository location

### Levelshot Capture
- Use `levelshot [size]` in-game to capture a loading-screen tile set.
- Each tile is captured from a raw 4:3 source view, then resampled to square output so the loading GUI's 4:3 stretch remains tile-accurate.
- The command writes the current map's loadscreen plus `_left`, `_right`, `_top`, and `_bottom` variants as both `.tga` and `.dds`, without the view weapon.
- OpenQ4 always uses the current map name under `gfx/guis/loadscreens/`; `size` defaults to `512`.
- Set `com_showLevelshotBounds 1` to draw a live centered 4:3 framing guide while composing shots; the guide is not included in `levelshot` output.

---

## Contributing

OpenQ4 is an open-source project and welcomes contributions! Whether you're fixing bugs, adding features, or improving documentation, your help is appreciated.

### How to Contribute
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### Development Guidelines
- Maintain compatibility with official Quake 4 assets
- Follow existing code style and conventions
- Document significant changes
- Test on multiple platforms when possible
- Keep performance in mind for older hardware

---

## License

<small>
<p>OpenQ4 is licensed under the <a href="https://www.gnu.org/licenses/gpl-3.0">GNU General Public License v3.0</a> (GPLv3).</p>
<p>This means you are free to:</p>
<ul>
  <li>Use the software for any purpose</li>
  <li>Modify the source code</li>
  <li>Distribute copies</li>
  <li>Distribute modified versions</li>
</ul>
<p>See the <a href="LICENSE">LICENSE</a> file for full details.</p>
<p><strong>Note:</strong> The GPLv3 license applies to OpenQ4's engine code only. The game library code in <a href="https://github.com/themuffinator/OpenQ4-GameLibs">OpenQ4-GameLibs</a> is derived from the Quake 4 SDK and subject to id Software's EULA. Quake 4 game assets remain the property of id Software and ZeniMax Media.</p>
</small>

---

## Credits

OpenQ4 builds upon the work of many talented developers and projects:

### Core Contributors
- **themuffinator** - OpenQ4 development and maintenance
- **Justin Marshall** - [Quake4Doom](https://github.com/idSoftware/Quake4Doom), BSE reverse engineering
- **Robert Backebans** - [RBDOOM3](https://github.com/RobertBeckebans/RBDOOM-3-BFG) modernization work

### Original Developers
- **id Software** - idTech 4 engine and Quake 4
- **Raven Software** - Quake 4 game development

### Third-Party Libraries
- **Sean Barrett** - [stb_vorbis](https://github.com/nothings/stb) audio codec
- **GLEW Team** - Nigel Stewart, Milan Ikits, Marcelo E. Magallon, Lev Povalahev
- **OpenAL Soft Contributors** - 3D audio implementation
- **SDL Team** - Cross-platform framework

### Special Thanks
- The Quake and id Tech community for continued support and enthusiasm
- All contributors who have submitted bug reports, patches, and improvements

---

## Links

- [Website](https://www.darkmatter-quake.com)
- [Repository](https://github.com/themuffinator/OpenQ4)
- [Game Library](https://github.com/themuffinator/OpenQ4-GameLibs)
- [Issue Tracker](https://github.com/themuffinator/OpenQ4/issues)
- [Quake 4 on Steam](https://store.steampowered.com/app/2210/)
- [Quake 4 on GOG](https://www.gog.com/game/quake_iv)

---

## Disclaimer

<small>
<p>OpenQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media. Quake 4 is a trademark of ZeniMax Media Inc.</p>
<p>You must own a legitimate copy of Quake 4 to use this software. OpenQ4 does not include any copyrighted game assets.</p>
</small>

---

### Use At Your Own Risk

<small>
<p><strong>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.</strong> OpenQ4 is experimental software under active development. Use at your own risk. The developers and contributors are not responsible for any damage, data loss, or issues that may arise from using this software.</p>
<p><strong>Copyright &copy; 2026 The OpenQ4 Project</strong><br>All rights reserved. Licensed under <a href="LICENSE">GPLv3</a>.</p>
</small>

---

[Back to Top](#openq4)

</div>

