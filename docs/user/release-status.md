# Latest release and matching documentation

**The latest available openQ4 release is [0.14.1](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1), published in November 2026.**
It replaces 0.13.2 and the withdrawn 0.14.0. Its [release notes](../dev/releases/v0.14.1.md)
recap everything new since 0.13.2.

**0.14.0, tagged `v0.14.000`, was released and then withdrawn because it was
broken.** It is not an available or recommended release. Its Git tag and
[historical notes](../dev/releases/v0.14.000.md) remain for diagnosis and
reproducing old builds; neither means that the release is still supported.
Everything 0.14.0 introduced is included, refined, in 0.14.1.

## Installing or upgrading to 0.14.1

Download the complete package for your system from the
[0.14.1 release](https://github.com/themuffinator/openQ4/releases/tag/v0.14.1):
Windows x64 and experimental Windows ARM64 installers/ZIPs, Linux x64
archives/AppImages, preview Linux ARM64 archives/AppImages, preview Apple
Silicon macOS OpenGL/Metal bridge unsigned archives, and an experimental
Android ARM64 APK.

- Keep your legitimate Quake 4 installation. Install or extract openQ4 into its
  own folder and launch `openQ4-client_x64.exe` on Windows, the Linux AppImage,
  or `openQ4-steamdeck` from the Linux archive on Steam Deck. Let openQ4 locate
  the original game, or point `+set fs_basepath` at the directory containing
  `q4base`, not at `q4base` itself.
- 0.14.1 repacks openQ4's own data into a core pack plus light-grid and
  loadscreen packs. Install a complete package, and remove any `baseoq4` folder
  left by an older openQ4 rather than mixing files.
- Your settings, bindings and saves carry over. New saves use format 5.
  Supported format 3 saves from 0.13.2 and format 4 saves from 0.14.0 still
  load, but older builds cannot read format 5, so back up your saves before
  upgrading if you might go back.
- Multiplayer needs matching openQ4 0.14.1 clients and servers.

## Returning to 0.13.2

[0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2) remains
available for comparison. Install a complete 0.13.2 package in a separate
folder; do not mix its executables, modules or `baseoq4` files with a newer
package. 0.13.2 writes save format 3 and cannot read the format 4 or 5 saves of
later builds, so use a backup or an eligible earlier save. An older package is
not a guarantee that every existing save will load. Its own guides are pinned to
the [0.13.2 source snapshot](https://github.com/themuffinator/openQ4/tree/v0.13.2/docs/user),
with [corrections in its release notes](../dev/releases/v0.13.2.md).

## Guides for the available release

These links are pinned to the 0.14.1 source snapshot so later development does
not silently change the installation or settings instructions:

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

Retail Quake 4 1.4.2 servers and clients use protocol 2.85 and cannot join
openQ4 games, or the other way round; openQ4 refuses them with an explanation.
An older server-security paragraph claiming compatibility was incorrect. Mods
that depend on the original proprietary game DLLs are also unsupported.

## Development is newer than the public download

The other guides in this directory follow the current source tree. After
0.14.1 they may describe features, defaults and fixes that have not reached an
available release. Building that source produces a development build, not
another copy of the published 0.14.1 binaries. The
[development roadmap](../dev/roadmap.md) tracks remaining qualification, and the
[development change record](../dev/releases/unreleased.md) collects changes for
a future release; neither announces one.
