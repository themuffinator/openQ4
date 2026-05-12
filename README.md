<a id="top"></a>

<div align="center">

<img src="assets/docs/img/banner.png" alt="OpenQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Status](https://img.shields.io/badge/status-Beta%20Development-d97a1f.svg)](https://github.com/themuffinator/OpenQ4/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenQ4)
[![Architecture](https://img.shields.io/badge/arch-x64%20%7C%20ARM64-orange.svg)](https://github.com/themuffinator/OpenQ4)
[![Windows Installer](https://img.shields.io/badge/windows-installer%20available-2d8f4e.svg)](https://github.com/themuffinator/OpenQ4/releases)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**Open-source Quake 4 source-port and game-code replacement built for modern systems**

[Quick Start](#quick-start) | [Highlights](#highlights) | [Compatibility](#compatibility-and-scope) | [Building](BUILDING.md) | [Documentation](#documentation) | [Credits](#credits)

</div>

---

## Overview

**OpenQ4** is a complete open-source replacement for the Quake 4 engine and game binaries. The project keeps official Quake 4 asset compatibility as its guiding constraint while modernizing rendering, display handling, input, packaging, and the build pipeline for current hardware and operating systems.

Single-player and multiplayer live under one `baseoq4/` directory with `game-sp` and `game-mp`. SDK-derived game code is maintained in the companion [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) repository, while BSE is treated as first-party source under `src/bse/` and linked directly into the client executable.

> [!NOTE]
> OpenQ4 does not include Quake 4 assets. You need a legitimate copy of Quake 4 from Steam or GOG to play.

> [!NOTE]
> OpenQ4 uses its own engine and game modules. It does not target compatibility with the proprietary Quake 4 DLLs and is not a drop-in runtime for legacy mods.

<p align="center">
  <img src="assets/docs/img/shot1.png" alt="OpenQ4 gameplay screenshot showing stock Quake 4 assets running in OpenQ4" width="92%">
</p>
<p align="center"><sub>OpenQ4 running against stock Quake 4 content.</sub></p>

---

## Highlights

### Engine and compatibility

- Complete engine and game-code replacement for Quake 4
- Unified `baseoq4/` game directory for both single-player and multiplayer
- Startup auto-discovery for Steam and GOG installs
- Official `q4base` media PK4 validation enabled by default, with retail game-binary PK4s ignored
- In-tree BSE runtime integrated into `OpenQ4-client_<arch>`
- Canonical SDK-derived game-library source kept in [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs)

### Rendering and presentation

- GL renderer modernization groundwork: explicit tier/capability probing, SDL3 context negotiation, `gfxInfo` tier/upload/GPU-timer reporting, opt-in renderer metrics, and a feature-gated dynamic upload stream while the ARB2 compatibility bridge remains the active shipping path
- Multi-scale **bloom** with luminance-based extraction
- **FP16 HDR** scene targets, filmic tone mapping, color controls, and log-average auto exposure
- Depth-aware **lens flares** with lightweight corona and high-quality ghost/streak modes
- **SSAO** and optional **CRT** post-processing
- Classic **stencil shadows** remain the default and now include translucent material caster/receiver support by default, with experimental **shadow mapping** for projected and point lights, projected-light CSM, alpha-tested transparency shadows, and optional translucent shadow accumulation
- Optional **enhanced material shading** upgrades stock normal/specular response through a renderer-only GLSL path, with no asset or material script changes required
- Experimental **irradiance-volume** indirect diffuse from [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG)-inspired `.lightgrid` data and per-area atlases, including native `bakeLightGrids` generation for OpenQ4-compatible assets with batch/CLI map loading and live console progress. See the [Light Grid Guide](docs-user/light-grids.md).
- Screen-fraction scaling, supersample-style presets, **MSAA**, and **SMAA**

### Modern usability

- **Automatic aspect-ratio**, FOV, zoom, and view-weapon framing from live render size
- Initial **multiplayer bot** content pack for AAS-enabled maps, currently seeded with one prototype character (`major`)
- Single-player weapon wheel currently under development, including slow-motion audio/presentation treatment on hold
- **Multi-monitor** targeting plus borderless, desktop fullscreen, and exclusive fullscreen paths
- **Controller** hotplug and analog input support
- FnQuake3-inspired **enhanced console UX** with completion popup, smooth scrollback, mouse selection, clipboard editing, and draggable scroll bars
- CPMA/CNQ3-style `^a`-`^z` **rainbow text color escapes**, including console rendering and live input preview
- **SDL3-first Linux runtime** with an explicit `OpenQ4-steamdeck` launcher/profile for **Steam Deck**
- Meson-based builds, `builddir/` for local artifacts, and `.install/` for staged runtime packages
- **Beta-stage** manual releases for **Windows**, **Linux**, and **macOS**
- Architecture-qualified release assets for **x64** and **ARM64**
- Native **Windows installers** ship alongside the Windows `.zip` release packages, with uninstall support and optional `openq4://` browser-link registration

<p align="center">
  <img src="assets/docs/img/shot2.png" alt="OpenQ4 gameplay screenshot showing combat and bloom" width="49%">
  <img src="assets/docs/img/shot3.png" alt="OpenQ4 gameplay screenshot showing environment lighting and shadow detail" width="49%">
</p>
<p align="center"><sub>Modern renderer upgrades layered onto the original game assets.</sub></p>

---

## Quick Start

1. Install **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4).
2. Download the latest OpenQ4 release from the [Releases page](https://github.com/themuffinator/OpenQ4/releases).
3. Choose the asset that matches your CPU architecture (`x64` or `arm64`, the ARM64 release label).
4. On Windows, use the matching installer (`openq4-<version>-windows-<arch>-setup.exe`) or extract the matching `.zip` package manually. The installer detects existing OpenQ4 installs, registers a normal Windows uninstaller entry, and can optionally enable `openq4://` browser links.
5. On Linux and macOS, extract the release archive to a folder of your choice.
6. Launch `OpenQ4-client_<arch>` (`OpenQ4-client_<arch>.exe` on Windows).
7. On Steam Deck, launch `OpenQ4-steamdeck` instead of the generic client entrypoint.

OpenQ4 will try to locate your Quake 4 install automatically. If detection fails, set `fs_basepath` manually or see [TECHNICAL.md](TECHNICAL.md#advanced-configuration).

> [!TIP]
> Windows release packages are intended to be self-contained.

> [!TIP]
> As of March 30, 2026, Linux packages default to the SDL3 backend. On Steam Deck and other Wayland sessions where both `WAYLAND_DISPLAY` and `DISPLAY` are present, `OpenQ4-steamdeck` prefers XWayland unless you already set `SDL_VIDEODRIVER` yourself.

> [!TIP]
> Current platform baselines are `Windows 7+` for binary compatibility with active validation centered on newer Windows releases, `macOS 11+` for the arm64 package line, and Linux release packages built on `Ubuntu 24.04`-class environments with OpenGL plus X11/GLX or XWayland available.

---

## Compatibility and Scope

OpenQ4 is developed against the shipped Quake 4 assets, not replacement repo-side content. Current compatibility work already covers several engine paths that stock content depends on:

- BSE reconstruction and client-side effect execution
- Effect-driven sound shader behavior
- BSE screen and camera effect paths
- Material and shader compatibility work needed to reduce custom override dependence
- Runtime irradiance-volume sampling plus a native `bakeLightGrids` command that writes `.lightgrid` metadata and `env/<map>/area*_lightgrid_amb.tga` atlases to `fs_savepath`, can auto-load named maps, `all`, or `all-mp`, skips already-complete targets unless `force` is used, reports bake progress through the console/log instead of leaving a static frame up, parallelizes CPU probe integration with configurable worker threads, and uses async GPU readback where supported
- Modern display handling for widescreen, ultrawide, and multi-monitor setups
- Official asset validation through `fs_validateOfficialPaks 1`

Project scope is intentionally explicit:

- `baseoq4/` remains the single game directory for SP and MP
- BSE stays integrated in-tree under `src/bse/`
- Dedicated server builds keep the disabled BSE manager path unless that changes by design
- Canonical SDK/game-library edits belong in `../OpenQ4-GameLibs`, not a mirrored `src/game` tree
- Compatibility with proprietary Quake 4 game DLLs is a non-goal
- The current irradiance-volume path is intentionally non-PBR and LDR: it adds indirect diffuse only, bakes OpenQ4-native `.tga` atlases, and does not bring over the BFG EXR/environment-probe reflection stack

For unattended baking, run the client with startup commands such as `+bakeLightGrids all -quit`, `+set sv_cheats 1 +bakeLightGrids all-mp -quit`, or `+bakeLightGrids force game/tram1 -quit`. OpenQ4 will switch modules as needed, load each map automatically, skip already-complete targets unless `force` is present, and stream progress to the console/log while writing outputs to `fs_savepath`. Multiplayer-target bakes are cheat-protected, so enable `sv_cheats` or `net_allowCheats` before `bakeLightGrids` when targeting MP maps.

Gameplay parity work is still ongoing. For current follow-up items, see [TODO.md](TODO.md) and [docs-dev/release-completion.md](docs-dev/release-completion.md).

---

## Mod Manifests

Runnable OpenQ4 mods now require a `mod.json` file in the root of the mod directory. This applies to `baseoq4/` as well as any external mod folder selected through the mod menu or requested by multiplayer auto-restart.

The manifest is a flat JSON object with these required string fields:

- `name`
- `version`
- `releaseDate`
- `website`
- `author`
- `requiredOpenQ4Version`

`requiredOpenQ4Version` is matched against the current OpenQ4 engine version (for example `0.1.010`). Mods without a manifest, or with a mismatched required engine version, are hidden from the mod menu and rejected for automatic mod switching.

```json
{
  "name": "OpenQ4",
  "version": "0.1.010",
  "releaseDate": "2026-04-14",
  "website": "https://www.darkmatter-quake.com",
  "author": "themuffinator / DarkMatter Productions",
  "requiredOpenQ4Version": "0.1.010"
}
```

---

## Building from Source

Full setup instructions live in [BUILDING.md](BUILDING.md). The short version:

- Keep [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) checked out beside this repo at `../OpenQ4-GameLibs`
- Use `builddir/` for local Meson builds
- Use `.install/` as the staged runtime package root
- Author repo-managed runtime overrides under `content/baseoq4/`; Meson stages them into `.install/baseoq4/`
- On Windows, call `tools/build/meson_setup.ps1` instead of raw `meson` from an arbitrary shell
- When staging `.install/`, use `meson install -C builddir --no-rebuild --skip-subprojects` or the wrapper equivalent
- In the Codex app, use the checked-in `.codex/environments/openq4.toml` actions to run the same build and launch entries defined in `.vscode/tasks.json` and `.vscode/launch.json`

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

Set `OPENQ4_BUILD_GAMELIBS=1` if you want the wrapper to trigger game-library builds during `compile`. For local runtime validation, launch from `.install/` so `fs_cdpath` points at the staged OpenQ4 overlay.

---

## Documentation

- [BUILDING.md](BUILDING.md) - platform requirements and build workflow
- [TECHNICAL.md](TECHNICAL.md) - advanced configuration, file layout, dependencies, and compatibility notes
- [docs-user/display-settings.md](docs-user/display-settings.md) - display, fullscreen, and multi-monitor behavior
- [docs-user/gameplay-settings.md](docs-user/gameplay-settings.md) - cinematic skip, corpse cleanup, corpse sinking, and music-volume controls
- [docs-user/light-grids.md](docs-user/light-grids.md) - light-grid baking, runtime toggles, output paths, and troubleshooting
- [docs-user/steam-deck.md](docs-user/steam-deck.md) - Steam Deck launcher, controls, and asset discovery notes
- [docs-user/shadow-mapping.md](docs-user/shadow-mapping.md) - shadow-map settings, presets, and troubleshooting
- [docs-user/multiplayer-networking.md](docs-user/multiplayer-networking.md) - multiplayer networking and lag compensation
- [docs-dev/platform-support.md](docs-dev/platform-support.md) - platform roadmap and backend status
- [docs-dev/high-framerate-rendering-plan.md](docs-dev/high-framerate-rendering-plan.md) - staged plan for 240 FPS presentation support
- [docs-dev/gl-renderer-modernization.md](docs-dev/gl-renderer-modernization.md) - GL tier selection, context ladder, metrics, and renderer-modernization scaffolding
- [docs-dev/official-pk4-checksums.md](docs-dev/official-pk4-checksums.md) - official asset validation reference
- [docs-dev/input-key-matrix.md](docs-dev/input-key-matrix.md) - keyboard and controller input reference
- [docs-dev/release-completion.md](docs-dev/release-completion.md) - current release checklist and open work
- [TODO.md](TODO.md) - tracked issues and planned tasks

<p align="center">
  <img src="assets/docs/img/shot4.png" alt="OpenQ4 gameplay screenshot showing atmospheric environment" width="92%">
</p>
<p align="center"><sub>Built to preserve the original game feel on modern displays and GPUs.</sub></p>

---

## Contributing

OpenQ4 welcomes code, documentation, testing, and compatibility reports. Keep stock-asset compatibility in mind, prefer engine-side fixes over shipping replacement content, and update documentation when behavior or workflow changes.

---

## Credits

### Project

- **themuffinator** - OpenQ4 development and maintenance
- **DarkMatter Productions** - project stewardship and website

### Upstream and reference work

- **Justin Marshall** - [Quake4Doom](https://github.com/jmarshall23/Quake4Doom) and initial BSE reverse engineering
- **CPMADevs CNQ3** - reference for CPMA/CNQ3-style rainbow text color escape support, adapted via the local FnQ3 implementation
- **Robert Beckebans** - renderer modernization reference work, including irradiance-volume integration reference material from [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG)
- **id Software** - idTech 4 engine and Quake 4
- **Raven Software** - Quake 4 game development

### Community and third-party libraries

- **Map-Center community** - feedback and playtesting, especially Papaya, JohnnyBoy, and coffee009
- **Sean Barrett** - [stb_vorbis](https://github.com/nothings/stb) audio codec
- **GLEW Team** - Nigel Stewart, Milan Ikits, Marcelo E. Magallon, and Lev Povalahev
- **OpenAL Soft contributors** - 3D audio implementation
- **SDL contributors** - cross-platform framework
- **SMAA authors** - Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, and Diego Gutierrez

---

## License

OpenQ4 engine code is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0). See [LICENSE](LICENSE) for the full text.

The game-library code in [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) is derived from the Quake 4 SDK and remains subject to id Software's SDK EULA. Quake 4 game assets remain the property of id Software and ZeniMax Media.

---

## Disclaimer

OpenQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media. Quake 4 is a trademark of ZeniMax Media Inc.

The software is provided "as is" without warranty of any kind. OpenQ4 is experimental software under active development and requires a legitimate Quake 4 installation.

---

## Links

[Website](https://www.darkmatter-quake.com) | [Repository](https://github.com/themuffinator/OpenQ4) | [Game Library](https://github.com/themuffinator/OpenQ4-GameLibs) | [Issue Tracker](https://github.com/themuffinator/OpenQ4/issues) | [Releases](https://github.com/themuffinator/OpenQ4/releases)

[Back to Top](#top)
