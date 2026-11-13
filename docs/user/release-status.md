# Latest release and matching documentation

**The latest openQ4 release is [0.14.1](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1), published in November 2026.** It replaces 0.13.2 and the withdrawn 0.14.0. Its [release notes](../dev/releases/v0.14.1.md) cover everything new since 0.13.2.

**0.14.0 (tag `v0.14.000`) was withdrawn because it was broken.** Don't install it. Its tag and [historical notes](../dev/releases/v0.14.000.md) remain only so old builds can be reproduced. Everything it introduced is in 0.14.1, with fixes.

## Installing or upgrading to 0.14.1

Download the complete package for your system from the [0.14.1 release page](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1):

- **Windows x64:** installer or ZIP. **Windows ARM64** (experimental): installer or ZIP.
- **Linux x64:** AppImage or archive; the archive also holds the Steam Deck launcher. **Linux ARM64** (preview): AppImage or archive.
- **macOS** (preview, Apple Silicon): two unsigned archives, OpenGL and Metal bridge.
- **Android** (experimental): an ARM64 APK.

Then:

- **Keep your own Quake 4 installation.** Install or extract openQ4 into its own folder, then launch `openQ4-client_x64.exe` on Windows, the AppImage on Linux, or `openQ4-steamdeck` from the Linux archive on Steam Deck. openQ4 finds the original game by itself. If it can't, start it with `+set fs_basepath "<folder>"`, where the folder is the one that contains `q4base`, not `q4base` itself.
- **Install a complete package.** 0.14.1 repacks openQ4's own data into three packs (core, light grids and loadscreens). Delete any `baseoq4` folder left by an older openQ4 rather than mixing old and new files.
- **Your settings, key bindings and saves carry over.** New saves use format 5. Saves from 0.13.2 (format 3) and 0.14.0 (format 4) still load, but older versions can't read format 5 saves. Back up your saves before upgrading if you might go back.
- **Multiplayer** needs openQ4 0.14.1 on both the client and the server.

## Returning to 0.13.2

[0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2) is still available. Install the complete 0.13.2 package in a separate folder and never mix its files with a newer package. 0.13.2 can't read saves made by later versions, so use a backup of your 0.13.2 saves. Its guides are in the [0.13.2 source snapshot](https://github.com/themuffinator/openQ4/tree/v0.13.2/docs/user), with corrections in its [release notes](../dev/releases/v0.13.2.md).

## Guides for the available release

These links show the guides exactly as they were when 0.14.1 shipped:

- [Getting started and system requirements](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/getting-started.md)
- [Client settings](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/client-settings.md)
- [Display and renderer settings](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/display-settings.md)
- [Controls](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/input-settings.md)
- [Campaigns and The Awakening](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/campaigns.md)
- [Steam Deck](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/steam-deck.md)
- [Android setup](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/android-setup.md)
- [Server setup](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/user/server-setup.md)
- [Save compatibility policy](https://github.com/themuffinator/openQ4/blob/v0.14.1/docs/dev/savegame-compatibility-policy.md)
- [0.14.1 release notes](../dev/releases/v0.14.1.md)

**Correction to the 0.14.1 server-security guide:** one paragraph claimed that retail Quake 4 could connect. It can't. Retail Quake 4 1.4.2 clients and servers use network protocol 2.85 and cannot join openQ4 games, or the other way round; openQ4 turns them away with an explanation. Mods that need the original game DLLs don't load either.

## The guides in this folder

The guides in this folder are kept up to date with the current source code, and they describe 0.14.1. Anything added since 0.14.1 is marked **Not in 0.14.1**: those features arrive in the next release. Building the source yourself gives you a development build, not the published 0.14.1. The [development roadmap](../dev/roadmap.md) tracks the remaining work, and the [development change record](../dev/releases/unreleased.md) collects changes for a future release. Neither announces a release date.
