# Latest release and matching documentation

**The latest openQ4 release is [0.15.0, the Multiplayer Edition](https://github.com/themuffinator/openQ4/releases/tag/v0.15.0), published in November 2026.** It replaces 0.14.1. Its [release notes](../dev/releases/v0.15.0.md) cover everything new since 0.14.1.

**0.14.0 (tag `v0.14.000`) was withdrawn because it was broken.** Don't install it. Its tag and [historical notes](../dev/releases/v0.14.000.md) remain only so old builds can be reproduced. Everything it introduced is in 0.14.1 and later, with fixes.

## Installing or upgrading to 0.15.0

Download the complete package for your system from the [0.15.0 release page](https://github.com/themuffinator/openQ4/releases/tag/v0.15.0):

- **Windows x64:** installer or ZIP. **Windows ARM64** (experimental): installer or ZIP.
- **Linux x64:** AppImage or archive; the archive also holds the Steam Deck launcher. **Linux ARM64** (preview): AppImage or archive.
- **macOS** (preview, Apple Silicon): two unsigned archives, OpenGL and Metal bridge.
- **Android** (experimental): an ARM64 APK.

Then:

- **Keep your own Quake 4 installation.** Install or extract openQ4 into its own folder, then launch `openQ4-client_x64.exe` on Windows, the AppImage on Linux, or `openQ4-steamdeck` from the Linux archive on Steam Deck. openQ4 finds the original game by itself. If it can't, start it with `+set fs_basepath "<folder>"`, where the folder is the one that contains `q4base`, not `q4base` itself.
- **Install a complete package.** Replace your previous openQ4 install entirely, or install into a new folder; don't mix files from different versions.
- **Your settings, key bindings and saves carry over.** Saves from 0.14.1 load in 0.15.0. Saves from 0.13.2 (format 3) and 0.14.0 (format 4) still load too, but 0.13.2 can't read newer saves, so back up your saves if you might go back that far.
- **Multiplayer** needs openQ4 0.15.0 on both the client and the server. 0.15.0 uses network protocol 2.42 and 0.14.1 uses 2.41, so they refuse each other.
- **Demos** recorded by 0.15.0 don't play in 0.14.1 or earlier; older recordings play in 0.15.0.
- **Updates from the game.** 0.15.0 installs the usual way. From 0.15.0 on, the **Updates** button on the title screen can download and install the next release on Windows (the ZIP, or the installer's default folder) and Linux (the archive). Other packages get a link to the release page.

## Returning to an older release

[0.14.1](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1) and [0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2) are still available. Install the complete older package in a separate folder and never mix its files with a newer package. Their guides are in the [0.14.1 source snapshot](https://github.com/themuffinator/openQ4/tree/v0.14.1/docs/user) and the [0.13.2 source snapshot](https://github.com/themuffinator/openQ4/tree/v0.13.2/docs/user), with notes in the [0.14.1](../dev/releases/v0.14.1.md) and [0.13.2](../dev/releases/v0.13.2.md) release notes.

## Guides for the available release

These links show the guides exactly as they were when 0.15.0 shipped:

- [Getting started and system requirements](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/getting-started.md)
- [Multiplayer guide](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/multiplayer.md)
- [Client settings](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/client-settings.md)
- [Display and renderer settings](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/display-settings.md)
- [Controls](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/input-settings.md)
- [Virtual reality](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/vr.md)
- [Campaigns and The Awakening](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/campaigns.md)
- [Steam Deck](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/steam-deck.md)
- [Android setup](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/android-setup.md)
- [Server setup](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/user/server-setup.md)
- [Save compatibility policy](https://github.com/themuffinator/openQ4/blob/v0.15.0/docs/dev/savegame-compatibility-policy.md)
- [0.15.0 release notes](../dev/releases/v0.15.0.md)

## The guides in this folder

The guides in this folder are kept up to date with the current source code, and they describe 0.15.0. Features new in 0.15.0 are marked **New in 0.15.0**. Building the source yourself gives you a development build, not the published 0.15.0. The [development roadmap](../dev/roadmap.md) tracks the remaining work, and the [development change record](../dev/releases/unreleased.md) collects changes for a future release. Neither announces a release date.
