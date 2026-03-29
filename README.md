<div align="center">

<img src="assets/img/banner.png" alt="OpenQ4 banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenQ4)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenQ4)

**Quake 4 reborn — modern systems, stunning visuals, the classic feel.**

[Features](#features) • [Installation](#installation) • [Building](BUILDING.md) • [Documentation](#documentation) • [Credits](#credits)

</div>

---

## About

**OpenQ4** is a free, open-source replacement for the Quake 4 engine that brings your classic game into the modern era. Built on the shoulders of [Quake4Doom](https://github.com/idSoftware/Quake4Doom), it keeps everything you love about the original — the brutal combat, the tight gameplay, the iconic atmosphere — and layers on a fresh set of visuals and quality-of-life upgrades that make it feel right at home on today's hardware.

Plug in a controller and play from the couch, enjoy crisp widescreen and ultrawide support, or push the visuals further with HDR rendering, dynamic shadow maps, and a suite of post-processing effects. The best part? It all runs on your existing copy of Quake 4 — no new assets, no subscription, just your game looking and playing better than ever.

> [!NOTE]
> **OpenQ4 does not include game assets.** You must own a legitimate copy of Quake 4 to play. OpenQ4 is not compatible with legacy Quake 4 game mods.

---

<p align="center">
  <img src="assets/img/shot1.png" alt="OpenQ4 gameplay screenshot showing in-engine combat" width="92%">
</p>
<p align="center"><sub>OpenQ4 running with stock Quake 4 assets.</sub></p>

---

## Installation

Getting up and running takes just four steps:

### Step 1 — Get Quake 4

You'll need a copy of Quake 4 installed on your system. Grab it from Steam or GOG if you don't have it yet:

<p>
  <a href="https://store.steampowered.com/app/2210/Quake_4/" target="_blank" rel="noopener noreferrer" aria-label="Purchase Quake 4 on Steam"><img src="https://img.shields.io/badge/Steam-Buy_Now-1b2838?logo=steam&logoColor=white&style=for-the-badge" alt="Buy on Steam"></a>
  &nbsp;&nbsp;
  <a href="https://www.gog.com/game/quake_iv" target="_blank" rel="noopener noreferrer" aria-label="Purchase Quake 4 on GOG"><img src="https://img.shields.io/badge/GOG-Buy_Now-86328a?logo=gog.com&logoColor=white&style=for-the-badge" alt="Buy on GOG"></a>
</p>

### Step 2 — Get the latest OpenQ4 release

Head to the **[Releases page](https://github.com/themuffinator/OpenQ4/releases)** and download the latest archive for your platform (Windows, Linux, or macOS).

### Step 3 — Extract and go

Unzip the archive to any folder you like.

### Step 4 — Play!

Launch `OpenQ4-client_x64` (that's `OpenQ4-client_x64.exe` on Windows). OpenQ4 will find your Quake 4 installation automatically — no fiddling with paths required in most cases.

> [!NOTE]
> **Windows players:** The package is completely self-contained — no extra software needs to be installed.

> [!NOTE]
> **Linux players:** OpenQ4 currently runs through XWayland on Wayland desktops. Make sure `DISPLAY` is set in your environment.

> [!TIP]
> If OpenQ4 can't find your Quake 4 installation automatically, see the [manual path configuration](TECHNICAL.md#advanced-configuration) section in the technical reference.

---

## Features

<p align="center">
  <img src="assets/img/shot2.png" alt="OpenQ4 gameplay screenshot showing dynamic combat scene" width="49%">
  <img src="assets/img/shot3.png" alt="OpenQ4 gameplay screenshot showing environment detail and lighting" width="49%">
</p>
<p align="center"><sub>Modern rendering upgrades running with original Quake 4 assets.</sub></p>

### Visuals Worth Showing Off

- **HDR Rendering** — Richer highlights and deeper shadows with filmic tone mapping; tweak exposure, contrast, saturation, and vibrance to taste
- **Dynamic Shadow Maps** *(experimental)* — Proper real-time shadows for lights throughout the game, with support for transparent surfaces
- **Ambient Occlusion (SSAO)** — Subtle contact shadows that add real depth to every environment
- **Bloom** — Natural light bleed around bright sources, tunable to stay tasteful or go dramatic
- **CRT Emulation** — Optional retro post-processing with scanlines, phosphor mask, screen curvature, and chromatic aberration — for that authentic late-2000s CRT look
- **Modern Anti-Aliasing** — MSAA and SMAA for smooth edges across a wide range of hardware
- **Resolution Scaling** — Dial down for performance on older machines, or push past native for supersampled sharpness

### Your Setup, Your Way

- **Controller Support** — Full gamepad play with plug-and-play hotplug, dual-stick aiming, and complete button remapping
- **Widescreen and Ultrawide Ready** — UI, field-of-view, and weapon framing all adapt automatically — no manual tweaks needed
- **Multi-Monitor Support** — Choose your display, let OpenQ4 auto-detect it, or go borderless windowed; it all just works
- **Modern Fullscreen Modes** — Exclusive fullscreen, borderless windowed, and desktop-native options to suit any workflow

### Under the Hood

- **Single-Player and Multiplayer** — The full Quake 4 experience in one engine, one install
- **Smart Game Detection** — Finds your Steam or GOG copy automatically and verifies your files at startup
- **Runs Your Game Files** — Designed from the ground up to work with your existing Quake 4 content — nothing replaced, nothing repackaged
- **Modern Audio** — Full positional 3D audio with WAV and Ogg Vorbis support
- **Windows, Linux, macOS** — One codebase, three platforms

---

## Documentation

- [Display Settings](docs-user/display-settings.md) — Multi-monitor and display configuration
- [Shadow Mapping](docs-user/shadow-mapping.md) — Shadow quality, transparency, and troubleshooting
- [Multiplayer Networking](docs-user/multiplayer-networking.md) — Lag compensation and network tuning
- [Technical Reference](TECHNICAL.md) — Advanced configuration, compatibility status, file layout, dependencies, and versioning

---

<p align="center">
  <img src="assets/img/shot4.png" alt="OpenQ4 gameplay screenshot showing atmospheric environment" width="92%">
</p>
<p align="center"><sub>Built for modern displays, modern GPUs, and modern systems without changing the original game feel.</sub></p>

---

## Building from Source

Want to compile OpenQ4 yourself? Full instructions, compiler requirements, and notes on the [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) companion repository live in **[BUILDING.md](BUILDING.md)**.

---

## Contributing

OpenQ4 is an open project and welcomes contributions of all kinds — bug reports, code fixes, new features, documentation, and platform testing.

1. Fork the repository
2. Create a feature branch
3. Make your changes and test thoroughly
4. Submit a pull request

Keep compatibility with official Quake 4 assets in mind, follow the existing code style, and see [BUILDING.md](BUILDING.md) for build setup instructions.

---

## License

<small>
<p>OpenQ4 is licensed under the <a href="https://www.gnu.org/licenses/gpl-3.0">GNU General Public License v3.0</a> (GPLv3). You are free to use, modify, and distribute the software under its terms.</p>
<p>See the <a href="LICENSE">LICENSE</a> file for full details.</p>
<p><strong>Note:</strong> The GPLv3 license applies to OpenQ4's engine code only. Game library code in <a href="https://github.com/themuffinator/OpenQ4-GameLibs">OpenQ4-GameLibs</a> is derived from the Quake 4 SDK and subject to id Software's EULA. Quake 4 game assets remain the property of id Software and ZeniMax Media.</p>
</small>

---

## Credits

### Core Contributors
- **themuffinator** — OpenQ4 development and maintenance
- **Justin Marshall** — [Quake4Doom](https://github.com/idSoftware/Quake4Doom), BSE reverse engineering
- **Robert Backebans** — [RBDOOM3](https://github.com/RobertBeckebans/RBDOOM-3-BFG) modernization work

### Playtesters
Papaya (`papayathebun` on Discord), JohnnyBoy (`johnnyboy.2000` on Discord), coffee009

### Original Developers
- **id Software** — idTech 4 engine and Quake 4
- **Raven Software** — Quake 4 game development

### Third-Party Libraries
- **Sean Barrett** — [stb_vorbis](https://github.com/nothings/stb) audio codec
- **GLEW Team** — Nigel Stewart, Milan Ikits, Marcelo E. Magallon, Lev Povalahev
- **OpenAL Soft Contributors** — 3D audio implementation
- **SDL Team** — Cross-platform framework
- **Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez** — [SMAA](https://www.iryoku.com/smaa/) reference implementation and lookup textures

### Special Thanks
The Quake and id Tech community for continued support and enthusiasm, and all contributors who have submitted bug reports, patches, and improvements.

---

## Links

- [Website](https://www.darkmatter-quake.com)
- [Repository](https://github.com/themuffinator/OpenQ4)
- [Game Library](https://github.com/themuffinator/OpenQ4-GameLibs)
- [Issue Tracker](https://github.com/themuffinator/OpenQ4/issues)
- [Releases](https://github.com/themuffinator/OpenQ4/releases)
- [Quake 4 on Steam](https://store.steampowered.com/app/2210/)
- [Quake 4 on GOG](https://www.gog.com/game/quake_iv)

---

## Disclaimer

<small>
<p>OpenQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media. Quake 4 is a trademark of ZeniMax Media Inc.</p>
<p>You must own a legitimate copy of Quake 4 to use this software. OpenQ4 does not include any copyrighted game assets.</p>
<p><strong>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.</strong> OpenQ4 is experimental software under active development. Use at your own risk.</p>
<p><strong>Copyright &copy; 2026 The OpenQ4 Project</strong> — Licensed under <a href="LICENSE">GPLv3</a>.</p>
</small>

---

[Back to Top](#openq4)

</div>
