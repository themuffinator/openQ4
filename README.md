<a id="top"></a>

<div align="center">

<img src="assets/img/banner.png" alt="OpenQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenQ4)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenQ4)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**Open-source Quake 4 engine and game-code replacement built for stock assets on modern systems**

[Quick Start](#quick-start) | [Highlights](#highlights) | [Compatibility](#compatibility-and-scope) | [Building](BUILDING.md) | [Documentation](#documentation) | [Credits](#credits)

</div>

---

## Overview

**OpenQ4** is a complete open-source replacement for the Quake 4 engine and game binaries. The project keeps official Quake 4 asset compatibility as its guiding constraint while modernizing rendering, display handling, input, packaging, and the build pipeline for current hardware and operating systems.

Single-player and multiplayer live under one `openq4/` directory with `game-sp` and `game-mp`. SDK-derived game code is maintained in the companion [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) repository, while BSE is treated as first-party source under `src/bse/` and linked directly into the client executable.

> [!NOTE]
> OpenQ4 does not include Quake 4 assets. You need a legitimate copy of Quake 4 from Steam or GOG to play.

> [!NOTE]
> OpenQ4 uses its own engine and game modules. It does not target compatibility with the proprietary Quake 4 DLLs and is not a drop-in runtime for legacy mods.

<p align="center">
  <img src="assets/img/shot1.png" alt="OpenQ4 gameplay screenshot showing stock Quake 4 assets running in OpenQ4" width="92%">
</p>
<p align="center"><sub>OpenQ4 running against stock Quake 4 content.</sub></p>

---

## Highlights

### Engine and compatibility

- Complete engine and game-code replacement for Quake 4
- Unified `openq4/` game directory for both single-player and multiplayer
- Startup auto-discovery for Steam and GOG installs
- Official `q4base` PK4 validation enabled by default
- In-tree BSE runtime integrated into `OpenQ4-client_<arch>`
- Canonical SDK-derived game-library source kept in [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs)

### Rendering and presentation

- Multi-scale **bloom** with luminance-based extraction
- **FP16 HDR** scene targets, filmic tone mapping, color controls, and log-average auto exposure
- **SSAO** and optional **CRT** post-processing
- Experimental **shadow mapping** for projected and point lights, projected-light CSM, alpha-tested transparency shadows, and optional translucent shadow accumulation
- Experimental **irradiance-volume** indirect diffuse from [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG)-inspired `.lightgrid` data and per-area atlases, including native `bakeLightGrids` generation for OpenQ4-compatible assets with batch/CLI map loading and live console progress. See the [Light Grid Guide](docs-user/light-grids.md).
- Screen-fraction scaling, supersample-style presets, **MSAA**, and **SMAA**

### Modern usability

- **Automatic aspect-ratio**, FOV, zoom, and view-weapon framing from live render size
- Single-player weapon wheel currently under development, including slow-motion audio/presentation treatment on hold
- **Multi-monitor** targeting plus borderless, desktop fullscreen, and exclusive fullscreen paths
- **Controller** hotplug and analog input support
- **SDL3-first Linux runtime** with an explicit `OpenQ4-steamdeck` launcher/profile for **Steam Deck**
- Meson-based builds, `builddir/` for local artifacts, and `.install/` for staged runtime packages
- **Windows**, **Linux**, and **macOS** targets with x64 as the current active baseline

<p align="center">
  <img src="assets/img/shot2.png" alt="OpenQ4 gameplay screenshot showing combat and bloom" width="49%">
  <img src="assets/img/shot3.png" alt="OpenQ4 gameplay screenshot showing environment lighting and shadow detail" width="49%">
</p>
<p align="center"><sub>Modern renderer upgrades layered onto the original game assets.</sub></p>

---

## Quick Start

1. Install **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4).
2. Download the latest OpenQ4 package from the [Releases page](https://github.com/themuffinator/OpenQ4/releases).
3. Extract the archive to a folder of your choice.
4. Launch `OpenQ4-client_x64` (`OpenQ4-client_x64.exe` on Windows).
5. On Steam Deck, launch `OpenQ4-steamdeck` instead of the generic client entrypoint.

OpenQ4 will try to locate your Quake 4 install automatically. If detection fails, set `fs_basepath` manually or see [TECHNICAL.md](TECHNICAL.md#advanced-configuration).

> [!TIP]
> Windows release packages are intended to be self-contained.

> [!TIP]
> As of March 30, 2026, Linux packages default to the SDL3 backend. On Steam Deck and other Wayland sessions where both `WAYLAND_DISPLAY` and `DISPLAY` are present, `OpenQ4-steamdeck` prefers XWayland unless you already set `SDL_VIDEODRIVER` yourself.

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

- `openq4/` remains the single game directory for SP and MP
- BSE stays integrated in-tree under `src/bse/`
- Dedicated server builds keep the disabled BSE manager path unless that changes by design
- Canonical SDK/game-library edits belong in `../OpenQ4-GameLibs`, not a mirrored `src/game` tree
- Compatibility with proprietary Quake 4 game DLLs is a non-goal
- The current irradiance-volume path is intentionally non-PBR and LDR: it adds indirect diffuse only, bakes OpenQ4-native `.tga` atlases, and does not bring over the BFG EXR/environment-probe reflection stack

For unattended baking, run the client with startup commands such as `+bakeLightGrids all -quit`, `+bakeLightGrids all-mp -quit`, or `+bakeLightGrids force game/tram1 -quit`. OpenQ4 will switch modules as needed, load each map automatically, skip already-complete targets unless `force` is present, and stream progress to the console/log while writing outputs to `fs_savepath`.

Gameplay parity work is still ongoing. For current follow-up items, see [TODO.md](TODO.md) and [docs-dev/release-completion.md](docs-dev/release-completion.md).

---

## Building from Source

Full setup instructions live in [BUILDING.md](BUILDING.md). The short version:

- Keep [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) checked out beside this repo at `../OpenQ4-GameLibs`
- Use `builddir/` for local Meson builds
- Use `.install/` as the staged runtime package root
- On Windows, call `tools/build/meson_setup.ps1` instead of raw `meson` from an arbitrary shell
- When staging `.install/`, use `meson install -C builddir --no-rebuild --skip-subprojects` or the wrapper equivalent

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
- [docs-user/light-grids.md](docs-user/light-grids.md) - light-grid baking, runtime toggles, output paths, and troubleshooting
- [docs-user/steam-deck.md](docs-user/steam-deck.md) - Steam Deck launcher, controls, and asset discovery notes
- [docs-user/shadow-mapping.md](docs-user/shadow-mapping.md) - shadow-map settings, presets, and troubleshooting
- [docs-user/multiplayer-networking.md](docs-user/multiplayer-networking.md) - multiplayer networking and lag compensation
- [docs-dev/platform-support.md](docs-dev/platform-support.md) - platform roadmap and backend status
- [docs-dev/official-pk4-checksums.md](docs-dev/official-pk4-checksums.md) - official asset validation reference
- [docs-dev/input-key-matrix.md](docs-dev/input-key-matrix.md) - keyboard and controller input reference
- [docs-dev/release-completion.md](docs-dev/release-completion.md) - current release checklist and open work
- [TODO.md](TODO.md) - tracked issues and planned tasks

<p align="center">
  <img src="assets/img/shot4.png" alt="OpenQ4 gameplay screenshot showing atmospheric environment" width="92%">
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
