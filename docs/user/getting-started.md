# Getting Started with openQ4

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

This guide is for players who want to install openQ4 and start playing as quickly as possible.

## What You Need

- A legitimate copy of **Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4)
- The latest openQ4 release from the [Releases page](https://github.com/themuffinator/openQ4/releases)
- A release package that matches your operating system and CPU architecture (`x64` or `arm64`); Linux ARM64 downloads are currently preview packages, and Windows ARM64 downloads are experimental
- Enough free disk space for both the openQ4 package and the retail Quake 4 `q4base/` assets

> [!NOTE]
> openQ4 does not include Quake 4 assets. It uses the original retail game files from your existing installation.

## System Requirements

These requirements are practical guidance for the current beta packages. openQ4 can scale down through performance presets, but old or incomplete OpenGL drivers may still fail even when the raw hardware looks fast enough.

| Component | Minimum | Recommended |
|---|---|---|
| CPU | 64-bit CPU matching the package architecture; dual-core class or better | Modern quad-core or better x64/arm64 CPU |
| Operating system | Windows 7 or later for x64 packages, or Windows on Arm for the experimental ARM64 package; a modern 64-bit Linux desktop userspace comparable to Ubuntu 24.04; preview Linux ARM64 on 64-bit little-endian AArch64 hardware; preview macOS 11 or later on Apple Silicon/arm64, which players have so far run only on current macOS; SteamOS 3.x on Steam Deck | Windows 10/11, a current Linux distribution or SteamOS release, or an up-to-date Apple Silicon macOS install |
| Memory | 4 GB RAM | 8 GB RAM for balanced play; 16 GB if you want higher presets, high resolutions, or room for background apps |
| Graphics | OpenGL compatibility driver with the ARB2-era features openQ4 requires, including vertex/fragment program support; 1 GB VRAM is the practical low-end target | OpenGL 4.1+ compatibility-class GPU with 2 GB+ VRAM for balanced 1080p play; 6 GB+ VRAM for `quality`, `ultra`, high-resolution displays, or heavier post-processing |
| Storage | About 12 GB free for openQ4 plus the retail Quake 4 assets | 15 GB+ free for the package, assets, saves, logs, crash dumps, and future updates |
| Assets | Official retail Quake 4 `q4base/` PK4 files from Steam or GOG | The same assets left unmodified so default validation can confirm them cleanly |
| Input and audio | Keyboard/mouse and a working audio output | Controller optional; Steam Deck controls use the Deck profile; broadband network recommended for online multiplayer |

Use the `Settings -> System` Performance Preset dropdown or its adjacent Auto-Detect button after first launch if you are unsure where to start. The console command is `autoDetectPerformancePreset`. Auto-Detect places a system at these minimum requirements on `low`, which keeps the original shadows at a reduced scene size. Systems below them should use `lowpower` or `minimum`, which draw no shadows, and systems a little above them `performance`, before enabling expensive options such as high MSAA, high resolution scale, shadow maps, or heavier post-processing.

## Recommended Install Flow

1. Install **Quake 4** through Steam or GOG.
2. Download the openQ4 release that matches your platform and CPU architecture.
3. Install or extract openQ4 to its **own folder**, or use the matching Linux AppImage directly.
4. Launch `openQ4-client_<arch>` from an extracted package, or launch the Linux AppImage.
5. On Steam Deck, launch `openQ4-steamdeck` when it is included in the package.

openQ4 will try to find your Quake 4 install automatically.

## Folder Layout Options

### Recommended

- Leave retail Quake 4 in its original Steam or GOG folder.
- Keep openQ4 in a separate folder.
- Let openQ4 detect the retail `q4base/` assets automatically.

### Portable / Manual

If you prefer a self-contained setup, keep these side by side in the same root folder:

- `baseoq4/` from openQ4
- `q4base/` copied from your Quake 4 install

> [!IMPORTANT]
> Do **not** copy retail PK4 files into `baseoq4/`. That folder is for openQ4 runtime files.
> Copy the complete retail `q4base/`, including the unsuffixed language-media archive such as
> `zpak_english.pk4`. Small numbered files such as `zpak_english_01.pk4` are patches, not a
> replacement for the base archive. If the unsuffixed archive is missing, character dialogue is
> silent and conversation-gated campaign scenes may not advance; verify or reinstall the retail
> files instead of renaming a numbered patch.

## Updating openQ4

openQ4's own content comes in three packs in `baseoq4/`:

| Pack | Contents | Size |
| --- | --- | ---: |
| `pak0.pk4` | Menus, fonts, strings, shaders and other core runtime files | about 14 MB |
| `pak1.pk4` | Baked light grids | about 270 MB |
| `pak2.pk4` | Loadscreens | about 530 MB |

The light grids and loadscreens change only when they are re-baked or
recaptured, so most releases leave them alone. Each release's notes say whether
they changed. When they did not, you do not need the whole package again:

1. Download the **update archive** for your platform, for example
   `openq4-<version>-windows-x64-update.zip` or
   `openq4-<version>-linux-x64-update.tar.xz`. It holds everything except
   `pak1.pk4` and `pak2.pk4`.
2. Extract it **into your existing openQ4 folder**, replacing files. It has no
   top-level folder of its own.

If a release did change `pak1.pk4` or `pak2.pk4`, download that file from the
release page too and save it in `baseoq4/` under the same name, or install the
full package instead. If a pack is missing or does not match, openQ4 says which
one and where to get it when it starts.

The Windows installer and the Linux AppImage update as whole downloads. Windows
debugging symbols (`.pdb` files) are no longer in the player package; they are
published separately as `openq4-<version>-windows-<arch>-symbols.zip` for crash
analysis.

## Platform Notes

### Windows

- You can use the matching installer or the `.zip` release.
- Windows release packages are meant to be self-contained.
- Current validation focuses on Windows 11 and Windows 10. Windows 7/8/8.1 remain legacy compatibility targets rather than the main test matrix.
- If openQ4 crashes, check the `crashes/` folder beside the executable for log and dump files.
- Windows ARM64 packages are experimental. They are built natively for Arm64 and automated tests start the dedicated server, but nothing has started the game client automatically yet, and no real-hardware report has confirmed that the packages run. If the renderer does not start on a Windows on Arm device, launch with `+set r_renderApi vulkan`. Please report what happens, including runs that simply work, with the Windows ARM64 issue template.

### Linux

- For the simplest desktop launch, download `openq4-<version>-x86_64.AppImage` on x64 or the clearly marked `openq4-<version>-preview-aarch64.AppImage` on ARM64, then run `chmod +x openq4-*.AppImage` and launch the file. It does not need system-wide installation.
- The AppImage contains openQ4 and its packaged runtime dependencies, but it does **not** contain Quake 4 assets. Retail assets remain in your legitimate Steam/GOG install and are found through the same automatic discovery used by archive packages; if needed, append `+set fs_basepath "/path/to/Quake 4"` when launching the AppImage.
- The `.tar.xz` archive remains available when you want the loose client, dedicated server, Steam Deck launcher, desktop metadata, or offline documentation as ordinary files. Extract it to a folder of your choice.
- Linux packages default to the SDL3 runtime path and should be treated as targeting an Ubuntu 24.04-class 64-bit desktop userspace with working OpenGL plus Wayland/EGL or X11/GLX support.
- Linux ARM64 packages are preview builds until real ARM64 hardware completes native-Wayland SP/MP gameplay, dedicated-server, audio, input, and package signoff.
- Linux ARM64 requires 64-bit little-endian AArch64 hardware and a desktop OpenGL compatibility driver. GLES-only boards are not covered by the current package.

### Steam Deck

- Use `openQ4-steamdeck` when it is included in the package. It enables the `steamdeck` platform profile and sets `OPENQ4_STEAMDECK=1`.
- Direct `openQ4-client_<arch>` launches on Steam Deck or SteamOS also auto-select the Deck profile while `com_platformProfile` is still `default`.
- Native Wayland is the default SDL path when available. Set `OPENQ4_FORCE_X11=1` to force the XWayland fallback from either the Steam Deck launcher or a direct client launch.
- If a native Wayland compositor has libdecor-related startup or window-decoration trouble, set `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` for that launch.
- Tune Deck controller behavior under `Settings -> Game Options -> Controller`, including gyro, touchpad mode, touchscreen routing, and low-battery rumble caps.
- Run `listControllers` from the console when reporting Deck input, battery, gyro, touchpad, or touchscreen issues.
- For more detail, see the [Steam Deck guide](steam-deck.md).

### macOS

- **Signed macOS download:** Schnapple provides a [signed and notarized openQ4 build through Mac Source Ports](https://www.macsourceports.com/game/quake4), packaged as Universal 2 for Apple Silicon and Intel Macs on macOS 11 or later. Visit the page for the download and installation instructions; your own Quake 4 game data is still required.
- Our GitHub macOS packages remain a preview for Apple Silicon/arm64 Macs on macOS 11 or later. Players have run them on macOS 26, including the single-player campaign and multiplayer with sound, but nothing older than the current macOS has been tested. Our GitHub releases do not yet publish Intel Mac or universal2 packages, and Rosetta is not a supported release target. The package notes below describe those GitHub builds.
- GitHub release runs with Apple Developer ID signing and notarization credentials publish signed/notarized OpenGL and Metal bridge DMGs. Those credentials are not set up yet, so our current GitHub releases publish clearly labeled `-unsigned.tar.gz` archives instead.
- The Metal bridge package still uses openQ4's OpenGL renderer path; it is not a native Metal renderer.
- Both macOS packages also carry openQ4's experimental Vulkan renderer. Apple ships no Vulkan driver, so on macOS it runs on top of MoltenVK, a Vulkan-on-Metal translation layer bundled inside the package. It is a translation layer, not a Metal renderer.
- macOS packages bundle OpenAL Soft for audio; no Homebrew or separate audio install is required. Its LGPL notice and corresponding source archive are included inside `openQ4.app/Contents/Resources/licenses/openal-soft/`.
- Fresh macOS configurations use Automatic, which tries Vulkan through MoltenVK and falls back to OpenGL; a saved OpenGL choice stays on OpenGL. Vulkan is a runtime option rather than a separate download, so there is no third macOS package to install.
- Expect problems if you try it. The Vulkan renderer is experimental on macOS (it is stable only on Windows), and on macOS only one player has reported it working. If it cannot start, including while creating the window, openQ4 logs the reason and renders with OpenGL instead. To stay on OpenGL, run `r_renderApi gl` and restart. See [Display Settings](display-settings.md#vulkan-on-macos-through-moltenvk).
- Unsigned macOS archives are ad-hoc signed only for bundle validity, are not notarized, and may require normal Gatekeeper approval on first launch.
- On macOS, open the DMG or unpack the `-unsigned.tar.gz` archive, then drag `openQ4.app` to `/Applications` (or another user-writable folder) and launch it. The app contains openQ4's `baseoq4` data and signed SP/MP modules; keep the whole package only if you also want the loose diagnostic client, dedicated server, or support collector. Do not copy retail `q4base` assets into the signed app.
- For crashes like GitHub issue #73, attach full terminal output as text plus `openq4.log` and any `.ips` report. See [macOS Support Data](macos-support-data.md), and run `collect_macos_support_info.sh` from the package root when it is included.

#### Using a GOG offline installer on macOS

openQ4 needs the installed game's `q4base/` data. It does not read a GOG setup
`.exe` or its `.bin` files directly. You can copy the complete `q4base/` folder
from your own Windows installation, or extract a supported offline installer on
the Mac using [innoextract](https://constexpr.org/innoextract/).

For a graphical option, [Mac Source Ports Extractor](https://www.macsourceports.com/utility/extractor/)
opens and extracts supported GOG installers without Terminal. Keep the `.exe`
and its matching `.bin` files together, then extract the installer into a new
folder. A player [confirmed this route for Quake 4](https://github.com/themuffinator/openQ4/issues/185#issuecomment-6253641976);
the exact installer filename was not recorded, so this is not a guarantee for
every installer version. The same `q4base/` placement instructions below apply.

If you use Homebrew, its [innoextract package](https://formulae.brew.sh/formula/innoextract)
can be installed with `brew install innoextract`. Keep the setup `.exe` and all
matching `.bin` parts together with their original filenames. From a new empty
folder for the extracted game, run the following, replacing the example path:

```sh
innoextract --gog "/path/to/setup_quake_4.exe"
```

Find `q4base/` in the extracted files; it may be inside an `app/` subfolder.
Keep the entire folder, including language-media PK4s. Point openQ4 at its
**parent**, for example:

```sh
"/Applications/openQ4.app/Contents/MacOS/openQ4" +set fs_basepath "/path/to/extracted/app"
```

Keep the retail data outside the signed `.app` bundle. Installer formats vary:
innoextract does not support every future Inno Setup version, and some GOG RAR
payloads require `unar` or `unrar` for `--gog` extraction. If extraction fails,
retain the exact installer filename and error when requesting help; copying
`q4base/` from your own Windows installation remains an alternative. This is an
extraction procedure, not a claim that every GOG installer has been tested on macOS.

## If Auto-Detection Fails

Point openQ4 at the **Quake 4 root folder** that contains `q4base/`.

Example:

```text
openQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

Do not point it directly at `q4base/` or `baseoq4/`.

For advanced path behavior and file layout details, see [TECHNICAL.md](../../TECHNICAL.md).

## Next Steps

- Want better video options? See [Display Settings](display-settings.md).
- Want to tune controls? See [Input Settings](input-settings.md).
- Want a quick overview of player-facing options? See [Client Settings Guide](client-settings.md).
- Want to host games? See [Server Setup Guide](server-setup.md).
