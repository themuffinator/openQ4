<div align="center">

# OpenQ4

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-0.0.1-green.svg)](https://github.com/themuffinator/OpenQ4)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenQ4)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenQ4)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**A modern, full binary replacement for Quake 4**

[Features](#features) • [Quick Start](#quick-start) • [Building](#building-from-source) • [Documentation](#documentation) • [Credits](#credits)

</div>

---

> [!WARNING]
> **Development Notice:** This project is largely developed through the latest exploratory and agentic AI "vibe coding" practices, prioritizing rapid development and creative problem-solving over strict formal methodologies. If you prefer projects with rigorous development processes or conventional software engineering practices, OpenQ4 will not align with your expectations and you are therefore encouraged to avoid this project entirely.

---

## About

**OpenQ4** is a complete replacement for the Quake 4 engine and game binaries. Built on the foundation of [Quake4Doom](https://github.com/idSoftware/Quake4Doom), this project aims to modernize and preserve the classic id Tech 4 title for current and future generations of gamers and provide a platform for future development. Whilst the project aims to be as open-source as possible, the BSE library will remain closed-source.

### What You Need

To play OpenQ4, you need:
- A legitimate copy of Quake 4 ([Steam](https://store.steampowered.com/app/2210/) or [GOG](https://www.gog.com/game/quake_iv) version recommended)
- The latest OpenQ4 release (from this repository)
- A modern 64-bit operating system

> **Note:** OpenQ4 does NOT include game assets. You must own Quake 4 to play. The engine will automatically detect your Quake 4 installation from Steam or GOG. OpenQ4 is not compatible with legacy Quake 4 game mods.

---

## Features

### Core Features
- **Full Game Support**: Complete single-player campaign and multiplayer modes
- **Unified Game Directory**: Single `openbase/` directory for both SP and MP game binaries
- **Asset Validation**: Automatic verification of official Quake 4 assets to ensure authenticity
- **Auto-Discovery**: Smart detection of your Quake 4 installation (Steam/GOG)

### Modern Enhancements
- **SDL3 Backend**: Modern cross-platform input and display handling
- **Controller Support**: Full gamepad/joystick support with hotplug and analog controls
- **Multi-Monitor**: Configure display output across multiple monitors
- **Display Modes**: Fullscreen, borderless windowed, and desktop-native modes
- **Audio**: Support for WAV and Ogg Vorbis formats with [OpenAL Soft](https://openal-soft.org/)
- **Dynamic UI**: Responsive interface that adapts to any aspect ratio

### Technical Improvements
- **C++23**: Modern C++ standards for better performance and maintainability
- **Meson Build System**: Fast, reliable builds with dependency management
- **Crash Diagnostics**: Automatic crash dumps and logs for debugging
- **OpenGL Rendering**: Enhanced rendering with [GLEW](http://glew.sourceforge.net/) 2.3.1

---

## Quick Start

### Prerequisites
- **Quake 4** installed ([Steam](https://store.steampowered.com/app/2210/) or [GOG](https://www.gog.com/game/quake_iv))
- **Windows**: Visual Studio 2026+ (or MSVC 19.46+)
- **Build Tools**: [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/themuffinator/OpenQ4.git
   cd OpenQ4
   ```

2. **Build the engine** (Windows)
   ```powershell
   # Setup the build
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype debug --wrap-mode=forcefallback
   
   # Compile
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
   
   # Install (optional - creates distributable package)
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
   ```

3. **Run the game**
   ```powershell
   builddir/OpenQ4.exe
   ```

The engine will automatically find your Quake 4 installation and validate the game files.

---

## Building from Source

<details>
<summary><b>Detailed Build Instructions</b></summary>

### Requirements
- **Meson** (>= 1.2.0)
- **Ninja** build system
- **Visual Studio 2026** or MSVC 19.46+ (Windows)
- **C++23** compatible compiler

### Build Options
```
-Dbuild_engine=true|false     # Build OpenQ4 and OpenQ4-ded executables
-Dbuild_games=true|false      # Build game modules
-Dbuild_game_sp=true|false    # Build single-player module
-Dbuild_game_mp=true|false    # Build multiplayer module
-Denforce_msvc_2026=true      # Enforce MSVC 2026+ requirement (optional)
```

### Build Commands

**Windows (PowerShell)**
```powershell
# Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype release

# Build
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# Create distributable package
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

**From Visual Studio Developer Command Prompt**
```batch
meson setup builddir . --backend ninja --buildtype release
meson compile -C builddir
```

### Output Files

**Build directory** (`builddir/`):
- `OpenQ4.exe` - Main engine executable
- `OpenQ4-ded.exe` - Dedicated server
- `openbase/game_sp.dll` - Single-player game module
- `openbase/game_mp.dll` - Multiplayer game module

**Install directory** (`install/`):
- Complete distributable package with all binaries
- Ready for deployment or testing with `fs_devpath`

</details>

---

## Game Directory Structure

OpenQ4 uses a unified game directory approach:

```
OpenQ4/
├── OpenQ4.exe              # Main executable
├── OpenQ4-ded.exe          # Dedicated server
└── openbase/               # Unified game directory
    ├── game_sp.dll         # Single-player module
    └── game_mp.dll         # Multiplayer module
```

The engine automatically selects the correct module based on game mode:
- **Single-player**: Loads `game_sp.dll`
- **Multiplayer**: Loads `game_mp.dll`

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

- [Platform Support](doc/platform-support.md) - Cross-platform roadmap and status
- [Display Settings](user-docs/display-settings.md) - Multi-monitor and display configuration
- [Input Key Matrix](doc/input-key-matrix.md) - Keyboard and controller input reference
- [Release Completion](doc/release-completion.md) - Release checklist and changelog

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
- See [official-pk4-checksums.md](doc/official-pk4-checksums.md) for checksum reference

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
- Aspect ratio is automatically handled from render size

### Advanced Graphics
- `r_interactionColorMode` - Shader compatibility mode
  - `0` - Auto-detect from interaction.vfp
  - `1` - Packed env16.xy
  - `2` - Vector env16/env17

</details>

<details>
<summary><b>Input and Controller Settings</b></summary>

### Controller Support
- `in_joystick` - Enable/disable gamepad
- `in_joystickDeadZone` - Analog stick dead zone
- `in_joystickTriggerThreshold` - Trigger sensitivity

### Features
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
- `fs_devpath` - Development overlay path (use `install/` for testing)

</details>

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
- Automatic sync via `tools/build/meson_setup.ps1`
- Optional game library builds with `OPENQ4_BUILD_GAMELIBS=1`

### Build Automation
- Missing or stale build directories are auto-regenerated
- Visual Studio environment auto-detected and loaded
- Use `OPENQ4_SKIP_GAMELIBS_SYNC=1` to skip game library sync
- Use `OPENQ4_GAMELIBS_REPO=<path>` to override repository location

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

OpenQ4 is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0) (GPLv3).

This means you are free to:
- Use the software for any purpose
- Modify the source code
- Distribute copies
- Distribute modified versions

See the [LICENSE](LICENSE) file for full details.

> **Note:** The GPLv3 license applies to OpenQ4's engine code only. The game library code in [OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs) is derived from the Quake 4 SDK and subject to id Software's EULA. Quake 4 game assets remain the property of id Software and ZeniMax Media.

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

- **Website:** [darkmatter-quake.com](https://www.darkmatter-quake.com)
- **Repository:** [github.com/themuffinator/OpenQ4](https://github.com/themuffinator/OpenQ4)
- **Game Library:** [github.com/themuffinator/OpenQ4-GameLibs](https://github.com/themuffinator/OpenQ4-GameLibs)
- **Issue Tracker:** [github.com/themuffinator/OpenQ4/issues](https://github.com/themuffinator/OpenQ4/issues)
- **Quake 4 on Steam:** [store.steampowered.com/app/2210](https://store.steampowered.com/app/2210/)
- **Quake 4 on GOG:** [gog.com/game/quake_iv](https://www.gog.com/game/quake_iv)

---

## Disclaimer

OpenQ4 is an independent project and is not affiliated with, endorsed by, or sponsored by id Software, Raven Software, Bethesda, or ZeniMax Media. Quake 4 is a trademark of ZeniMax Media Inc.

You must own a legitimate copy of Quake 4 to use this software. OpenQ4 does not include any copyrighted game assets.

---

### Use At Your Own Risk

**THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.**
OpenQ4 is experimental software under active development. Use at your own risk. The developers and contributors are not responsible for any damage, data loss, or issues that may arise from using this software.

**Copyright © 2026 The OpenQ4 Project**
All rights reserved. Licensed under [GPLv3](LICENSE).

---

[Back to Top](#openq4)

</div>
