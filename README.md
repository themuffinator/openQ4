<a id="top"></a>

<div align="center">

<img src="assets/docs/img/banner.png" alt="openQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Status](https://img.shields.io/badge/status-Beta%20Development-d97a1f.svg)](https://github.com/themuffinator/openQ4/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS%20experimental-lightgrey.svg)](https://github.com/themuffinator/openQ4)
[![Architecture](https://img.shields.io/badge/arch-x64%20%7C%20ARM64-orange.svg)](https://github.com/themuffinator/openQ4)

**Play Quake 4 on modern systems with an open-source engine and game-code replacement built around the original retail assets.**

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

[Get Started](docs/user/getting-started.md) | [Features](#why-players-use-openq4) | [Player Docs](#player-guides) | [Build from Source](BUILDING.md) | [Technical Reference](TECHNICAL.md)

</div>

---

<p align="center">
  <img src="assets/docs/img/readme-airdefense1-cinematic.png" alt="openQ4 airdefense1 intro cinematic showing ships approaching Stroggos" width="92%">
</p>

## What is openQ4?

**openQ4** is an open-source replacement for the Quake 4 engine and game binaries, built to keep the original game playable on modern PCs while improving presentation, audio, controls, packaging, and day-to-day usability.

It is designed for players who want the original Quake 4 experience with a cleaner path to running it on today's hardware.

> [!NOTE]
> openQ4 does **not** include Quake 4 assets. You still need a legitimate Quake 4 copy from Steam or GOG.

> [!IMPORTANT]
> I am getting the question a lot - particularly in the past week or two (Aug 2026) - so I feel I need to emphasise this point now: **openQ4 is not compatible with legacy Quake 4 game code**. This includes the recent Awakening leak by Justin Marshall. This position won't (and cannot) change. An Awakening mod for openQ4 isn't off the cards, however.

> [!IMPORTANT]
> Another point to make, as sadly luddites have become more common rather than less in 2026 and certainly more vocal than ever. Yes, this software is mostly vibe coded and with the extensive work that has gone into it it wouldn't be humanly possible to achieve without a small studio. That does not mean it isn't thoroughly checked and tested by multiple people, it also doesn't mean it is unsafe for use, nor does it mean we don't understand the codebase. It does have a few remaining issues to iron out - remaining macOS support being the main one - but as feedback will show it is otherwise stable. Whether you choose to follow unsubstantiated claims by luddites is entirely up to you, but I would first take a moment to examine their track records before presuming their expertise and credibility on the matter.

---

## Why players use openQ4

Development on the `idtech5-ui` branch is building a complete scalable vector
interface and visual editor. This work is not yet available as a finished
feature; see the [visual specification](docs/dev/ui-visual-design.md) and
[implementation progress](docs/dev/plans/idtech5-ui.md). The current runtime
includes [isolated group fades](docs/dev/ui/composition.md),
[editable vector masks](docs/dev/ui/masks.md) and
[button navigation and state feedback](docs/dev/ui/interaction.md), plus
[menu input ownership](docs/dev/ui/input-routing.md) and
[live state/expression bindings](docs/dev/ui/bindings.md). A
[native GUI importer](docs/dev/ui/legacy-import.md) now preserves the current
GUI set's structure, expressions and dependency references. A
[presentation boundary](docs/dev/ui/presentation-bridge.md) separates game and
menu consumers from legacy window objects, and the runtime now supports
[independent document instances](docs/dev/ui/instances.md) with
[durable instance state and manager ownership](docs/dev/ui/instance-persistence.md). Complete GUI migration,
game action integration and the visual editor remain in development.
The [product completion plan](docs/dev/plans/ui-product-completion.md) records
the implementation audit and the gates for complete gameplay, artwork, editor
and platform delivery. The [requirement register](docs/dev/ui/product-requirements.md)
tracks the complete scope and required acceptance evidence.

The [0.13.1 patch notes](docs/dev/releases/v0.13.1.md) cover the release save fix, download verification, and map-compilation corrections. Replace the complete package when upgrading.

- **Modern display support** for widescreen, ultrawide, multi-monitor, borderless, and fullscreen setups.
- **Optional visual upgrades** such as bloom, HDR, anti-aliasing, baked light grids, and enhanced shadow options, plus soft particles that fade effects into the world instead of cutting a hard edge against it.
- **Smoother motion at high refresh rates.** The camera, weapons, movers, and everything riding them are drawn on an interpolated presentation clock between the game's 60 Hz ticks, so a 144 Hz or 240 Hz display shows motion at its own rate. Simulation, networking, collision, demos, and saves keep their original timing.
- **Real liquids** with wading, swimming, drowning, and damaging slime and lava, plus underwater visuals and audio. Retail Quake 4 has none of this, so liquids are something you author - see the [Liquids guide](docs/user/liquids.md).
- **Reliable OpenAL audio** with compatibility-first voice handling; experimental macOS packages bundle OpenAL Soft so large stock levels are not constrained by Apple's legacy buffer pool.
- **Improved input and quality-of-life features** including controller support, better console UX, and modern settings behavior.
- **Single-player and multiplayer in one install** with active compatibility work aimed at the stock game.
- **Readable multiplayer chat** with adjustable layout, retained conversation history, channel switching and sent-message recall.
- **A usable multiplayer server browser** with sorting, filters and saved favorites, plus managed Duel queues and contextual Match Control feedback. Match Control retains the last result into warmup and offers Previous, Next and Free Camera controls for spectators, alongside follow commands governed by server permissions. Automated gameplay checks cover these controls and results on OpenGL and Vulkan; multiplayer remains experimental.
- **Smarter repeat level loads** with exact-match learned source preparation and validated local model, world, collision, and animation caches that fall back safely to the installed assets.
- **A stock-map Arena Campaign** (experimental) with five escalating bot tiers, varied combat game types, boss matches, and persistent ladder progress beside the original story.
- **A unified demo library and player** with pause, speed, stepping, rewind/fast-forward controls, honest legacy-format status, and full-world free-fly/player-follow playback for server-side multi-view recordings.
- **Cross-platform support** with Windows packages, directly executable Linux AppImages and archives for x86_64 plus preview aarch64, Steam Deck support on Linux, and experimental Apple Silicon/arm64 macOS OpenGL/Metal bridge packages through the signed/notarized DMG lane for credentialed release runs.
- **Opt-in renderer modernization**, each piece default-off and independently reversible: [temporal AA/upscaling with GPU-time dynamic resolution](docs/user/temporal-presentation.md), [bounded volumetrics, screen-space reflections, and indirect light](docs/user/advanced-screen-space-lighting.md), GPU skeletal animation, and guarded PBR materials with authored specular probes. These are previews rather than finished features, and a single setting rolls the whole set back.
- **Open development** with releases, issue tracking, and community feedback all happening in public.

---

## System requirements

You need a legitimate Quake 4 install plus the openQ4 package that matches your operating system and CPU architecture.

| Tier | Practical target |
|---|---|
| **Minimum** | 64-bit CPU, 4 GB RAM, a working OpenGL compatibility driver with ARB2-era vertex/fragment program support, and about 12 GB free for the openQ4 package plus retail Quake 4 assets. Use the `minimum` or `lowpower` performance preset on constrained systems. |
| **Recommended** | Modern quad-core CPU, 8 GB RAM, OpenGL 4.1+ compatibility-class GPU with 2 GB+ VRAM, current graphics drivers, and 15 GB+ free. For high resolutions, `quality`, or `ultra`, 16 GB RAM and 6 GB+ VRAM gives much better headroom. |

Packaged support currently focuses on Windows, Linux x64, Steam Deck/SteamOS, preview Linux ARM64, and experimental Apple Silicon/arm64 macOS. Linux ARM64 requires a desktop OpenGL compatibility driver and remains preview until real-hardware Wayland gameplay, audio, and input signoff is accepted. See the [Getting Started guide](docs/user/getting-started.md#system-requirements) for the platform-specific requirements and caveats.

---

## Renderer showcase

<p align="center">
  <img src="assets/docs/img/readme-bloom-hdr.png" alt="openQ4 bloom and HDR side-by-side comparison on mp q4dm2" width="92%">
</p>
<p align="center"><sub>Bloom and HDR on mp/q4dm2 from the same loadscreen camera: normal rendering on the left, enhanced post-processing on the right.</sub></p>

<p align="center">
  <img src="assets/docs/img/readme-lightgrid.png" alt="openQ4 light-grid indirect diffuse off and on comparison" width="92%">
</p>
<p align="center"><sub>Baked light-grid indirect diffuse on mp/q4dm2, shown off and on from the same loadscreen camera.</sub></p>

<p align="center">
  <img src="assets/docs/img/readme-crt.png" alt="openQ4 CRT post-process off and on comparison on mp q4dm8" width="92%">
</p>
<p align="center"><sub>CRT post-processing on mp/q4dm8, shown off and on with a clean no-HUD camera.</sub></p>

<p align="center">
  <img src="assets/docs/img/readme-crt-q4dm6.png" alt="openQ4 CRT post-process off and on comparison on mp q4dm6" width="92%">
</p>
<p align="center"><sub>A second CRT comparison on mp/q4dm6 shows the same post-process across a brighter indoor arena.</sub></p>

> **Renderer backends:** OpenGL remains the default and recommended release renderer on every platform. The **Vulkan** backend is **experimental and opt-in** (`r_renderApi vulkan`, applied on engine restart), but now renders the stock Quake 4 material-program families, including environment and heat-haze effects, displacement and depth/blur post effects, and guide-driven parallax, custom-lighting, water, and refractive-glass stages. Vulkan also supports 4x MSAA with SMAA. On Windows and Linux it drives a Vulkan driver directly. Apple ships no Vulkan driver, so on experimental macOS the same module runs on top of **MoltenVK**, a Vulkan-on-Metal translation layer bundled inside both existing macOS packages — a runtime option rather than a third download, and not a Metal renderer. Broader parity and platform validation are still in progress, so visual artifacts or instability remain possible; an initialization failure falls back safely to OpenGL. See [Display Settings → Renderer Backend](docs/user/display-settings.md#renderer-backend-opengl-default-vulkan-is-experimental).

---

## Quick start

1. Install **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4).
2. Download the latest openQ4 build from the [Releases page](https://github.com/themuffinator/openQ4/releases).
3. On Linux, make the matching `x86_64` or `aarch64` AppImage executable and launch it; for an extracted archive, launch `openQ4-client_<arch>` (or `openQ4-steamdeck` on Steam Deck).
4. If openQ4 does not find your Quake 4 install automatically, follow the path setup notes in the [Getting Started guide](docs/user/getting-started.md).

**Need the step-by-step version?** Start with [docs/user/getting-started.md](docs/user/getting-started.md).

---

## Player guides

### Start here

- [Getting Started](docs/user/getting-started.md) - system requirements, installation, first launch, and common setup questions
- [Client Settings Guide](docs/user/client-settings.md) - where to find the most useful in-game settings
- [Server Setup Guide](docs/user/server-setup.md) - basic dedicated server setup and common server variables
- [Server and Remote-Console Security](docs/user/server-security.md) - the authenticated `rcon2` protocol and password handling

### Play and tune

- [Display Settings](docs/user/display-settings.md) - fullscreen, windowed mode, resolution scale, and multi-monitor behavior
- [Input Settings](docs/user/input-settings.md) - keyboard, mouse, controller, and binding help
- [Gameplay Settings](docs/user/gameplay-settings.md) - gameplay and audio toggles for everyday play
- [Arena Campaign](docs/user/arena-campaign.md) (experimental) - single-player arena tiers, unlock rules, maps, game types, and bot rosters
- [Steam Deck](docs/user/steam-deck.md) - launcher, controls, and Linux handheld notes
- [Multiplayer Networking](docs/user/multiplayer-networking.md) (experimental) - multiplayer tuning and lag-comp behavior
- [Multiplayer Chat](docs/user/multiplayer-chat.md) - chat controls, scrollback, message recall and saved layout settings
- [Competitive Matches](docs/user/competitive-matches.md) (experimental) - match rules, human voting, optional readiness, round flow, stock-map One Flag support, and match endings
- [Demo Library and Multi-View Demos](docs/user/multiview-demos.md) - browse formats, use playback controls, and record or replay complete multiplayer matches
- [Liquids](docs/user/liquids.md) - water, slime, and lava behaviour, and how to author them
- [Classic Dynamic Lights](docs/user/classic-dynamic-lights.md) - Quake II/III style dynamic lights on muzzle flashes, bright projectiles, and explosions
- [Shadow Mapping](docs/user/shadow-mapping.md) - optional shadow-map settings, flashlight, door and cutout shadows, stabilized cascades, cache budgets, and fallback diagnostics
- [Light Grids](docs/user/light-grids.md) - advanced lighting guide for players and testers
- [Advanced Screen-Space Lighting](docs/user/advanced-screen-space-lighting.md) (experimental) - bounded froxel volumetrics, SSR, SSGI, performance controls, and one-setting rollback
- [Temporal AA and Dynamic Resolution](docs/user/temporal-presentation.md) (experimental) - temporal anti-aliasing/upscaling and GPU-time resolution scaling
- [Cel Shading](docs/user/cel-shading.md) - banded lighting and outline settings for the cel-shaded look
- [DDS Texture Replacements](docs/user/texture-replacements.md) - install and diagnose DXT/BC7 texture packs
- [Level-Load Cache](docs/user/level-load-cache.md) - learned preload and generated model, world, collision, and animation cache behavior, controls, rollback, and cleanup

### Build and technical docs

- [BUILDING.md](BUILDING.md) - compile openQ4 from source
- [Android, GLES and SigmaTouch](docs/dev/android-build.md) - experimental native Android builds, optional touch-host integration, and desktop GLES testing
- [TECHNICAL.md](TECHNICAL.md) - advanced configuration, file layout, compatibility notes, and mod details
- [Map Entity Strings](docs/user/map-entity-strings.md) - replace or extend a map's runtime entities without editing the original map

---

## Compatibility at a glance

- openQ4 targets the **official Quake 4 retail assets**.
- It ships its **own engine and game modules**.
- It is **not** a drop-in runtime for the original proprietary Quake 4 DLL mods.
- The project is still in **beta development**, so compatibility work is ongoing.

Developers and testers should use the [engine capability matrix](docs/dev/engine-capability-matrix.md) for authoritative implemented/experimental/missing status, the [idTech 5-level modernization roadmap](docs/dev/idtech5-modernization-roadmap.md) for the compatibility-safe implementation order, the [shared interaction-lighting contract](docs/dev/classic-interaction-domain-modernization.md), [shared fog/blend contract](docs/dev/classic-fog-blend-domain-modernization.md), and [shared material-deform contract](docs/dev/classic-deform-domain-modernization.md) for the default-off ownership and rollback boundaries, the [loading/cache contract](docs/dev/loading-cache-modernization.md) for the source-authoritative preload and generated-cache boundary plus pending evidence, and the [stock-asset baseline](docs/dev/stock-asset-baseline.md) for reproducible PK4, SP/MP, save/load, demo, log, and engine-screenshot evidence.

The [shadow-mapping audit](docs/dev/shadowmapping-final-audit-2026-09-05.md) records shadow correctness and cache repairs, repeatable stock-map comparisons, and the remaining compatibility and validation limits.

The [memory-file robustness notes](docs/dev/memory-file-robustness.md) document buffer-growth measurements and native regression coverage for in-memory file generation.

If you run into problems, please use the [issue tracker](https://github.com/themuffinator/openQ4/issues) and include crash logs or setup details when possible. For experimental macOS crashes, use the [macOS support-data guide](docs/user/macos-support-data.md) before filing or updating an issue.

---

## Contributing

Bug reports, compatibility reports, testing feedback, and code contributions are all welcome. If you want to help build the project itself, start with [BUILDING.md](BUILDING.md). For details on how to report issues, submit changes, and follow project conventions, see [CONTRIBUTING.md](.github/CONTRIBUTING.md).

---

## Credits

- **themuffinator** - openQ4 development and maintenance
- **[Emile Belanger (emileb)](https://github.com/emileb)** - contributor and original author of the Android port, SigmaTouch integration, OpenGL ES 3.0 renderer, GLES shader variants, ETC2/EAC compression, and associated mobile memory/loading work. This integration builds on his [Android branch](https://github.com/emileb/openQ4/tree/android) and [GLES shader-variants branch](https://github.com/emileb/openQ4/tree/gles-shader-variants); see the [contribution and adaptation record](docs/dev/android-gles-integration.md).
- **DarkMatter Productions** - project stewardship and website
- **[The RmlUi Team, CodePoint, Shift Technology and contributors](https://github.com/mikke89/RmlUi/tree/6.3)** - MIT-licensed retained layout library used by the `idtech5-ui` runtime integration; [retained licence notice](docs/licenses/RmlUi.txt).
- **[Baptiste Lepilleur and the JsonCpp authors](https://github.com/open-source-parsers/jsoncpp/tree/1.9.6)** - JSON document parser used under its MIT option for editable retained UI sources; [retained licence notice](docs/licenses/JsonCpp.txt).
- **[Mikko Mononen, Eric Veach and the libtess2 contributors](https://github.com/memononen/libtess2/tree/8dbd6483e920311a58c9af10a10beb278efebc36)** - SGI-B-2.0 polygon tessellator used by native retained vector paths with an [openQ4 precision patch](subprojects/packagefiles/libtess2/double-precision.patch); [retained licence notice](docs/licenses/libtess2.txt).
- **[Q2REX Project Team](https://github.com/themuffinator/Q2REX)** - design reference for the native [multiplayer chat panel, scaling and history](docs/user/multiplayer-chat.md)
- **[MuffMode](https://github.com/themuffinator/MuffMode)** and **[Q4MAX](https://www.moddb.com/mods/q4max)** - multiplayer workflow and usability references; see the [competitive reference audit](docs/dev/competitive-match-reference-audit.md)
- **Justin Marshall** - Quake4Doom and early BSE reverse engineering reference work
- **Robert Beckebans** - renderer modernization reference work, including RBDOOM-3-BFG inspiration
- **id Software's official Doom 3 and Doom 3 BFG source releases** - retained idTech 4 source lineage; see the [audited provenance inventory](docs/dev/source-provenance.md)
- **id Software** and **Raven Software** - Quake 4 and the underlying technology
- **Chris Robinson and the [OpenAL Soft](https://openal-soft.org/) contributors** - maintained cross-platform OpenAL runtime bundled by macOS packages
- **[The Khronos Group](https://github.com/KhronosGroup/OpenGL-Registry)** - MIT-licensed OpenGL ES API headers, with their original notices retained
- **akacross** (Discord user) - Thorough playtesting on Linux and Windows, a huge help moving the project forward!

---

## License and disclaimer

openQ4 engine code is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0). See [LICENSE](LICENSE) for details. Files retaining Doom 3 or Doom 3 BFG Edition headers also retain their upstream notices and are accompanied by the corresponding published Additional Terms; the [source-provenance inventory](docs/dev/source-provenance.md) records their scope, pinned audit references, and intermediate lineage without offering a legal conclusion.

The game-library code in [openQ4-game](https://github.com/themuffinator/openQ4-game) is derived from the Quake 4 SDK and remains subject to id Software's SDK EULA. Quake 4 assets remain the property of id Software and ZeniMax Media.

openQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media.

---

[Website](https://www.darkmatter-quake.com) | [Repository](https://github.com/themuffinator/openQ4) | [Game Library](https://github.com/themuffinator/openQ4-game) | [Issues](https://github.com/themuffinator/openQ4/issues) | [Releases](https://github.com/themuffinator/openQ4/releases)

[Back to Top](#top)
