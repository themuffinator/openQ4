<a id="top"></a>

> [!IMPORTANT]
> **Calling all vibe masters.** openQ4 got this far on sharp prompts, stubborn validation runs and a healthy disregard for sleep, but the Strogg aren't beaten yet. The remaining work is mapped out in the [development roadmap](docs/dev/roadmap.md), and we'd welcome more hands (and more agents) to help bring it home. Before you unleash your favourite model, check in on the **#openq4** channel of the [DarkMatter Discord](https://discord.gg/T32mFejwR4): we'll point you at the work that needs you most and keep everyone's agents from fragging each other over the same roadmap item. Then brief your model with [AGENTS.md](AGENTS.md) and open a pull request. Reviews are human, standards are high, and the snacks are strictly virtual.

<div align="center">

<img src="assets/docs/img/banner.png" alt="openQ4 banner">

[![Licensing: GPL engine and SDK game code](https://img.shields.io/badge/Licensing-GPL%20engine%20%7C%20SDK%20game%20code-blue.svg)](LICENSING.md)
[![Status](https://img.shields.io/badge/status-Beta%20Development-d97a1f.svg)](https://github.com/themuffinator/openQ4/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS%20experimental-lightgrey.svg)](https://github.com/themuffinator/openQ4)
[![Architecture](https://img.shields.io/badge/arch-x64%20%7C%20ARM64-orange.svg)](https://github.com/themuffinator/openQ4)
[![Proudly vibecoded](assets/docs/img/badge-proudly-vibecoded.svg)](AGENTS.md)

**Play Quake 4 on modern systems with an open-source engine and SDK-licensed game-code replacement built around the original retail assets.**

<a href="https://github.com/themuffinator/openQ4/releases">
  <img src="https://img.shields.io/badge/Download-Latest%20Release-2d8f4e?style=for-the-badge&logo=github" alt="Download the latest openQ4 release">
</a>
<a href="https://github.com/themuffinator/openQ4/stargazers">
  <img src="https://img.shields.io/github/stars/themuffinator/openQ4?style=for-the-badge&logo=github&label=Star%20on%20GitHub" alt="Star openQ4 on GitHub">
</a>
<a href="https://github.com/themuffinator/openQ4/fork">
  <img src="https://img.shields.io/github/forks/themuffinator/openQ4?style=for-the-badge&logo=github&label=Fork%20on%20GitHub" alt="Fork openQ4 on GitHub">
</a>

<a href="https://discord.gg/T32mFejwR4">
  <img src="https://img.shields.io/badge/Join%20the-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the openQ4 Discord server">
</a>

[Latest release: 0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2) | [Release status](docs/user/release-status.md) | [Get Started](#getting-started) | [Features](#features-in-0132) | [Player Guides](#player-guides) | [Build from Source](BUILDING.md) | [Technical Reference](TECHNICAL.md)

</div>

---

<p align="center">
  <img src="assets/docs/img/readme-airdefense1-cinematic.png" alt="openQ4 airdefense1 intro cinematic showing ships approaching Stroggos" width="92%">
</p>

## What is openQ4?

**openQ4** replaces the Quake 4 engine and game binaries with a GPL-covered engine and SDK-licensed, source-available game modules. It keeps the original game playable on modern PCs while improving visuals, audio, controls and everyday usability.

It is for players who want the original Quake 4 experience on today's hardware.

> [!NOTE]
> openQ4 does **not** include Quake 4 assets. You still need a legitimate Quake 4 copy from Steam or GOG.

---

## Features in 0.13.2

- **Built for modern displays.** Widescreen, ultrawide, multi-monitor, borderless and fullscreen setups all work. On 144 Hz and 240 Hz screens the camera, weapons and moving objects are drawn smoothly between the game's 60 Hz ticks, while gameplay, networking, demos and saves keep their original timing.
- **Optional visual upgrades.** Bloom, HDR, anti-aliasing, soft particles, baked light grids, cel shading, a CRT filter and cleaner [shadow maps](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/shadow-mapping.md), each one switchable.
- **Real liquids.** Wade, swim and drown in water, take damage in slime and lava, with underwater visuals and audio. Retail Quake 4 has no liquids, so they appear in maps built for them; see the [Liquids guide](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/liquids.md).
- **OpenAL audio**, including bundled OpenAL Soft in experimental macOS packages.
- **Controller support and quality-of-life fixes**, including a better console and modern settings behaviour.
- **More single-player.** Alongside the original campaign there is an experimental [Arena Campaign](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/arena-campaign.md): bot matches across five tiers of the stock maps, with boss matches and saved progress.
- **Better multiplayer** (experimental): a server browser with sorting, filters and favourites, [readable chat with history](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/multiplayer-chat.md), Duel queues and spectator match controls.
- **A demo library and player** with pause, speed, stepping, rewind and fast-forward, plus free-fly playback of full-match recordings.
- **Single-player and multiplayer in one install** on Windows, Linux and Steam Deck, with experimental Apple Silicon macOS packages.
- **An experimental Vulkan renderer**, chosen with `r_renderApi vulkan`. OpenGL is the recommended default in this release.
- **Renderer previews**, all off by default: [temporal anti-aliasing and dynamic resolution](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/temporal-presentation.md) and [volumetrics, reflections and indirect light](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/advanced-screen-space-lighting.md).

See the [0.13.2 notes](docs/dev/releases/v0.13.2.md) and [release status](docs/user/release-status.md) for upgrade advice and the withdrawn 0.14.0 release.

---

## Screenshots

<p align="center">
  <img src="assets/docs/img/readme-bloom-hdr.png" alt="openQ4 bloom and HDR side-by-side comparison on mp q4dm2" width="92%">
</p>
<p align="center"><sub>Bloom and HDR on mp/q4dm2: normal rendering on the left, enhanced post-processing on the right.</sub></p>

<p align="center">
  <img src="assets/docs/img/readme-lightgrid.png" alt="openQ4 light-grid indirect diffuse off and on comparison" width="92%">
</p>
<p align="center"><sub>Baked light-grid lighting on mp/q4dm2, off and on.</sub></p>

<p align="center">
  <img src="assets/docs/img/readme-crt.png" alt="openQ4 CRT post-process off and on comparison on mp q4dm8" width="92%">
</p>
<p align="center"><sub>The CRT filter on mp/q4dm8, off and on.</sub></p>

> [!TIP]
> **OpenGL is the default and recommended renderer in 0.13.2.** Vulkan is experimental on every platform in this release: set `r_renderApi vulkan` and restart to try it. An initialization failure falls back to OpenGL. On macOS, Vulkan uses bundled MoltenVK. See [Display Settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/display-settings.md).

---

## System requirements

You need a legitimate Quake 4 install plus the openQ4 package that matches your operating system and CPU architecture.

| Tier | Practical target |
|---|---|
| **Minimum** | 64-bit CPU, 4 GB RAM, a working OpenGL compatibility driver with ARB2-era vertex/fragment program support, and about 12 GB free for the openQ4 package plus retail Quake 4 assets. Use the `minimum` or `lowpower` performance preset on constrained systems. |
| **Recommended** | Modern quad-core CPU, 8 GB RAM, OpenGL 4.1+ compatibility-class GPU with 2 GB+ VRAM, current graphics drivers, and 15 GB+ free. For high resolutions, `quality`, or `ultra`, 16 GB RAM and 6 GB+ VRAM gives much better headroom. |

Packages are available for Windows x64, Linux x64 and Steam Deck/SteamOS, with preview Linux ARM64, experimental Apple Silicon/arm64 macOS and experimental Windows ARM64 builds:

- **Linux** ships as directly executable AppImages and archives for `x86_64` and `aarch64`. Linux ARM64 needs a desktop OpenGL compatibility driver.
- **macOS** players can get [signed and notarized builds for Apple Silicon and Intel Macs](https://www.macsourceports.com/game/quake4) from Schnapple at Mac Source Ports. Our GitHub OpenGL/Metal bridge packages remain unsigned Apple Silicon previews; players have run them only on current macOS.
- **Windows ARM64** packages are built but have not yet been confirmed to run on real hardware.

See [Getting Started](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/getting-started.md#system-requirements) for platform details.

---

## Getting started

1. Install **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4).
2. Download the latest available release, [openQ4 0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2).
3. On Linux, make the matching `x86_64` or `aarch64` AppImage executable and launch it; for an extracted archive, launch `openQ4-client_<arch>` (or `openQ4-steamdeck` on Steam Deck).
4. If openQ4 does not find your Quake 4 install automatically, follow the path setup notes in the [Getting Started guide](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/getting-started.md).

When upgrading, replace the whole openQ4 package rather than individual files. If returning from withdrawn 0.14.0, follow the [rollback and save guidance](docs/user/release-status.md#installing-or-returning-to-0132).

---

## Player guides

Start with [release status and documentation corrections](docs/user/release-status.md). The guides below are pinned to 0.13.2.

### Start here

- [Getting Started](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/getting-started.md) - system requirements, installation, first launch, and common setup questions
- [Client Settings Guide](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/client-settings.md) - where to find the most useful in-game settings
- [Server Setup Guide](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/server-setup.md) - dedicated server setup and common server variables
- [Server and Remote-Console Security](docs/user/release-status.md#guides-for-the-available-release) - secure remote console and password handling

### Play and tune

- [Display Settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/display-settings.md) - fullscreen, windowed mode, resolution scale, multi-monitor, and renderer choice
- [Input Settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/input-settings.md) - keyboard, mouse, controller, and bindings
- [Gameplay Settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/gameplay-settings.md) - gameplay and audio options
- [Arena Campaign](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/arena-campaign.md) (experimental) - tiers, unlocks, maps, game types, and bots
- [Steam Deck](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/steam-deck.md) - launcher, controls, and handheld notes
- [Multiplayer Networking](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/multiplayer-networking.md) (experimental) - connection tuning and lag compensation
- [Multiplayer Chat](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/multiplayer-chat.md) - chat controls, history, and layout
- [Competitive Matches](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/competitive-matches.md) (experimental) - match rules, voting, readiness, rounds, and One Flag
- [Demo Library and Multi-View Demos](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/multiview-demos.md) - browsing, playback controls, and recording full matches
- [Shadow Mapping](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/shadow-mapping.md) - shadow-map options, flashlight shadows, and troubleshooting
- [Light Grids](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/light-grids.md) - baked indirect lighting
- [Classic Dynamic Lights](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/classic-dynamic-lights.md) - Quake II/III style lights on muzzle flashes, projectiles, and explosions
- [Cel Shading](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/cel-shading.md) - the cel-shaded look
- [Temporal AA and Dynamic Resolution](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/temporal-presentation.md) (experimental) - temporal anti-aliasing, upscaling, and automatic resolution scaling
- [Advanced Screen-Space Lighting](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/advanced-screen-space-lighting.md) (experimental) - volumetrics, reflections, and indirect light
- [DDS Texture Replacements](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/texture-replacements.md) - installing texture packs
- [Level-Load Cache](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/level-load-cache.md) (experimental) - optional local load caches, and how to clear them

### Mapping and modding

- [Liquids](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/liquids.md) - water, slime, and lava behaviour, and how to add them to maps
- [Map Entity Strings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/map-entity-strings.md) - replace or extend a map's entities without editing the map

---

## Compatibility

- openQ4 targets the **official Quake 4 retail assets** and ships its **own engine and game modules**.
- It is **not** a drop-in runtime for mods built on the original proprietary Quake 4 game DLLs.
- Retail Quake 4 multiplayer clients and servers cannot connect to openQ4. Use matching openQ4 builds; see the [documentation correction](docs/user/release-status.md#guides-for-the-available-release).
- The project is in **beta development**, so compatibility work is ongoing. Current-source features are documented separately from the available package in [release status](docs/user/release-status.md).

## Reporting problems

Please use the [issue tracker](https://github.com/themuffinator/openQ4/issues) and include crash logs or setup details when you can. For macOS crashes, follow the [macOS support-data guide](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/macos-support-data.md) first. Windows ARM64 and Linux ARM64 have their own issue templates; a report that everything simply worked is as useful there as a bug.

---

## Contributing

Current source builds default to the modern interface and preserve saved preferences. See [Modern and classic screens](docs/user/client-settings.md#modern-and-classic-screens) for release defaults and switching interfaces.

Bug reports, compatibility reports, testing feedback, and code contributions are all welcome. To build openQ4 yourself, start with [BUILDING.md](BUILDING.md); for project conventions, see [CONTRIBUTING.md](.github/CONTRIBUTING.md).

Developers can also find advanced configuration and file layout in [TECHNICAL.md](TECHNICAL.md), the experimental ARM64 APK and source builds in [Android, GLES and SigmaTouch](docs/dev/android-build.md), and the current implemented/experimental/missing status of every subsystem in the [engine capability matrix](docs/dev/engine-capability-matrix.md).

---

## Credits

- **themuffinator** - openQ4 development and maintenance
- **[Schnapple / Mac Source Ports](https://www.macsourceports.com/game/quake4)** - signed and notarized macOS builds for Apple Silicon and Intel Macs
- **[Emile Belanger (emileb)](https://github.com/emileb)** - contributor and original author of the Android port, SigmaTouch integration, OpenGL ES 3.0 renderer, GLES shader variants, ETC2/EAC compression, and associated mobile memory/loading work. This integration builds on his [Android branch](https://github.com/emileb/openQ4/tree/android) and [GLES shader-variants branch](https://github.com/emileb/openQ4/tree/gles-shader-variants); see the [contribution and adaptation record](docs/dev/android-gles-integration.md).
- **DarkMatter Productions** - project stewardship and website
- **[The RmlUi Team, CodePoint, Shift Technology and contributors](https://github.com/mikke89/RmlUi/tree/6.3)** - MIT-licensed layout library behind the new interface, with [openQ4 patches](subprojects/packagefiles/rmlui/README.openq4.md); [licence notice](LICENSES/RmlUi.txt)
- **[Baptiste Lepilleur and the JsonCpp authors](https://github.com/open-source-parsers/jsoncpp/tree/1.9.8)** - JSON parser for the new interface's source files, used under its MIT option with an [openQ4 patch](subprojects/packagefiles/jsoncpp/subnormal-numbers.patch); [licence notice](LICENSES/JsonCpp.txt)
- **[Mikko Mononen, Eric Veach and the libtess2 contributors](https://github.com/memononen/libtess2/tree/8dbd6483e920311a58c9af10a10beb278efebc36)** - SGI-B-2.0 polygon tessellator for the new interface's vector shapes, with an [openQ4 precision patch](subprojects/packagefiles/libtess2/double-precision.patch); [licence notice](LICENSES/libtess2.txt)
- **[Q2REX Project Team](https://github.com/themuffinator/Q2REX)** - design reference for the [multiplayer chat panel](docs/user/multiplayer-chat.md)
- **[MuffMode](https://github.com/themuffinator/MuffMode)** and **[Q4MAX](https://www.moddb.com/mods/q4max)** - multiplayer workflow and usability references
- **[Team Beef](https://github.com/Team-Beef-Studios)** - design reference for [VR mode](docs/user/vr.md): [PreyVR](https://github.com/Team-Beef-Studios/PreyVR) (LuboÅ¡ VonÃ¡sek, GLKarin, DrBeef, Baggyg, Bummser and contributors) and [Doom3Quest](https://github.com/DrBeef/Doom3Quest) (DrBeef and contributors) shaped its head-driven movement, two-handed aim, zoom, comfort options and physical crouch; ideas only, no code copied
- **Justin Marshall** - Quake4Doom and early BSE reverse engineering reference work, and recovering and publishing Quake 4: The Awakening
- **Robert Beckebans** - renderer modernization reference work, including RBDOOM-3-BFG inspiration
- **id Software's official Doom 3 and Doom 3 BFG source releases** - retained idTech 4 source lineage; see the [source provenance inventory](docs/dev/source-provenance.md)
- **id Software** and **Raven Software** - Quake 4 and the underlying technology
- **Raven Software** and **Ritual Entertainment** - Quake 4: The Awakening, the unreleased expansion whose single-player campaign openQ4 can run
- **Chris Robinson and the [OpenAL Soft](https://openal-soft.org/) contributors** - cross-platform OpenAL runtime bundled in macOS packages
- **Sam Lantinga and the [SDL contributors](https://github.com/libsdl-org/SDL/tree/release-3.4.18)** - cross-platform window, input and microphone support, including the dummy audio driver used in automated voice tests; SDL uses the zlib licence
- **Sean Barrett and the [stb_vorbis contributors](https://github.com/nothings/stb/blob/master/stb_vorbis.c)** - MIT/public-domain Vorbis decoding; openQ4 adds a virtual-filesystem callback path for bounded compressed streaming
- **The Khronos Group and [glslang contributors](https://github.com/KhronosGroup/glslang/tree/16.6.0)** - GLSL-to-SPIR-V compiler used by the Vulkan renderer; its complete licence ships under `licenses/`
- **[The Khronos Group, Valve Corporation and LunarG](https://github.com/KhronosGroup/OpenXR-SDK/tree/release-1.1.65)** - Apache-2.0 OpenXR loader and headers behind [VR mode](docs/user/vr.md); [licence notice](LICENSES/openxr-sdk.txt)
- **[The Khronos Group](https://github.com/KhronosGroup/OpenGL-Registry)** - MIT-licensed OpenGL ES API headers, with their original notices retained
- **akacross** (Discord user) - Thorough playtesting on Linux and Windows, a huge help moving the project forward!

---

## License and disclaimer

openQ4 engine code is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0). See [LICENSE](LICENSE) for details. Files retaining Doom 3 or Doom 3 BFG Edition headers also retain their upstream notices and are accompanied by the corresponding published Additional Terms; the [source-provenance inventory](docs/dev/source-provenance.md) records their scope, pinned audit references, and intermediate lineage without offering a legal conclusion.

The game-library code in [`src/game`](src/game/) and [`src/mpgame`](src/mpgame/) is derived from the Quake 4 SDK and remains subject to [id Software's SDK EULA](LICENSES/QUAKE-4-SDK-EULA.rtf). [LICENSING.md](LICENSING.md) distinguishes component terms and the unresolved linked-distribution licensing question. Quake 4 assets remain the property of id Software and ZeniMax Media.

openQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media.

---

[Website](https://www.darkmatter-quake.com) | [Repository](https://github.com/themuffinator/openQ4) | [Game Sources](src/game/) | [Issues](https://github.com/themuffinator/openQ4/issues) | [Releases](https://github.com/themuffinator/openQ4/releases)

[Back to Top](#top)
