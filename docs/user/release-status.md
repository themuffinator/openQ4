# Latest release and matching documentation

**The latest available openQ4 release is [0.13.2](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2), published on 14 September 2026.**

**0.14.0, tagged `v0.14.000`, was released and then withdrawn because it was
broken.** It is not an available or recommended release. Its Git tag and
[historical notes](../dev/releases/v0.14.000.md) remain for diagnosis and
reproducing old builds; neither means that the release is still supported.
GitHub's latest-release link currently resolves to 0.13.2. Status checked on
31 October 2026.

## Installing or returning to 0.13.2

Download the complete package from the [0.13.2 release](https://github.com/themuffinator/openQ4/releases/tag/v0.13.2).
The available downloads are Windows x64 and experimental Windows ARM64
installers/ZIPs, Linux x64 archives/AppImages, preview Linux ARM64
archives/AppImages, and preview Apple Silicon macOS OpenGL/Metal bridge
unsigned archives. This release has no Android APK. The 0.13.2 notes call the
macOS packages experimental because they predate macOS becoming a preview.

Keep your legitimate Quake 4 installation. Install or extract openQ4 into its
own folder and launch `openQ4-client_x64.exe` on Windows, the Linux AppImage,
or `openQ4-steamdeck` from the Linux archive on Steam Deck. Let openQ4 locate
the original game, or point `+set fs_basepath` at the directory containing
`q4base`, not at `q4base` itself.

If you tried 0.14.0, back up your saves and settings before returning to
0.13.2. Install a complete 0.13.2 package in a separate folder; do not mix its
executables, modules or `baseoq4` files with the withdrawn release. 0.13.2
writes save format 3 and cannot read the newer format 4 saves. Keep those
newer saves separately and use an eligible earlier save or start a new game.
An older package is not a guarantee that every existing save will load.

## Guides for the available release

These links are pinned to the 0.13.2 source snapshot so later development does
not silently change the installation or settings instructions:

- [Getting started and system requirements](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/getting-started.md)
- [Client settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/client-settings.md)
- [Display and renderer settings](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/display-settings.md)
- [Controls](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/input-settings.md)
- [Steam Deck](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/steam-deck.md)
- [Server setup](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/server-setup.md)
- [Save compatibility policy](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/dev/savegame-compatibility-policy.md)
- [0.13.2 release notes and corrections](../dev/releases/v0.13.2.md)

One correction to the historical documentation: retail Quake 4 1.4.2 servers
and clients use protocol 2.85, while openQ4 0.13.2 uses 2.41. They cannot join
each other's games. An older server-security paragraph claiming compatibility
was incorrect. Use matching openQ4 clients and servers. Mods that depend on
the original proprietary game DLLs are also unsupported.

For map authors, the light-grid capture memory and viewport fixes are also
later development work. On 0.13.2, bake in a 640x480 window; large bakes may
still exhaust memory. Earlier wording that attributed these fixes to 0.13.2
was incorrect. See the [development guide's correction](light-grids.md#a-map-i-baked-with-0132-or-earlier-lights-oddly).

## Development is newer than the public download

The other guides in this directory follow the current source tree. They may
describe features, defaults and fixes that have not reached an available
release. Building that source produces a development build, not another copy
of the published 0.13.2 binaries.

| Area | Available 0.13.2 package | Current development documentation |
| --- | --- | --- |
| Vulkan | Experimental and opt-in on every platform; OpenGL is recommended | Later Windows qualification does not apply retroactively to 0.13.2 |
| Saves | Writes format 3; rejects newer formats | Format 4 and additional historical-save recovery are later work |
| Android | No APK in the release assets | APK packaging and device work are unreleased |
| Features and UI | Use the guides pinned above | New campaign integration, VR, subtitles, dialogue volume, language additions and UI/rendering changes must not be advertised as a 0.13.2 feature list |

The [development roadmap](../dev/roadmap.md) tracks remaining qualification.
[Unreleased notes](../dev/releases/unreleased.md) and
[release-completion evidence](../dev/release-completion.md) describe work for
a future release; they do not announce one.
