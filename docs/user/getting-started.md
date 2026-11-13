# Getting started with openQ4

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This guide gets you from download to playing. It covers what you need, installing, where openQ4 looks for Quake 4, updating, and notes for each platform.

## What you need

- **Your own copy of Quake 4** from [Steam](https://store.steampowered.com/app/2210/Quake_4/) or [GOG](https://www.gog.com/en/game/quake_4). openQ4 doesn't include any Quake 4 game files; it uses the ones from your installation (the `q4base` folder).
- **The openQ4 package for your system** from the [Releases page](https://github.com/themuffinator/openQ4/releases). Pick the one that matches your computer: `x64` for most PCs, `arm64` for ARM machines. Linux ARM64 packages are a preview and Windows ARM64 packages are experimental.
- **Disk space** for openQ4 and Quake 4 together; see below.

## System requirements

openQ4 can scale down a long way through its performance presets, but very old or incomplete graphics drivers may still fail even on fast hardware.

| | Minimum | Recommended |
|---|---|---|
| **Processor** | 64-bit, dual-core, matching the package (`x64` or `arm64`) | Modern quad-core |
| **Operating system** | Windows 7 or later (x64) or Windows on Arm (experimental ARM64 package); a 64-bit Linux desktop comparable to Ubuntu 24.04; SteamOS 3 on Steam Deck; macOS 11 or later on Apple Silicon (preview) | Windows 10 or 11, a current Linux or SteamOS, or an up-to-date macOS |
| **Memory** | 4 GB | 8 GB; 16 GB for the higher presets, high resolutions or plenty of background apps |
| **Graphics** | Working OpenGL drivers with the older shader features Quake 4 itself needed, and about 1 GB of video memory | OpenGL 4.1 or Vulkan graphics with 2 GB or more; 6 GB or more for the Quality preset and above or high-resolution screens |
| **Storage** | About 12 GB free for openQ4 plus Quake 4 | 15 GB or more, leaving room for saves, logs and updates |
| **Game files** | Quake 4's original `q4base` files from Steam or GOG | The same files, unmodified |
| **Input and sound** | Keyboard, mouse and working audio output | A controller is optional; online play wants a broadband connection |

**Choosing settings.** On first launch openQ4 picks a performance preset for your hardware. To change it, open **Settings → Display → Performance Preset** (the **System** tab in the classic menus), or press **Auto-Detect** beside it to pick again. A PC at the minimum requirements lands on **Low**, which keeps Quake 4's original shadows at a reduced resolution. Below that, use **Low Power** or **Minimum**, which draw no shadows; just above it, use **Performance**. Turn on expensive extras such as high anti-aliasing, a higher resolution scale or shadow maps only once the game runs smoothly.

## Install

1. Install **Quake 4** through Steam or GOG.
2. Download the openQ4 package for your platform.
3. Install or extract openQ4 into **its own folder**. On Linux you can instead run the AppImage directly.
4. Launch it:
   - **Windows:** `openQ4-client_x64.exe` (or `openQ4-client_arm64.exe`).
   - **Linux:** the AppImage, or `openQ4-client_x64` from the archive.
   - **Steam Deck:** `openQ4-steamdeck` from the Linux archive.
   - **macOS:** `openQ4.app`; see [macOS](#macos).

openQ4 looks for your Quake 4 installation by itself.

### Where to keep the files

**Recommended:** leave Quake 4 where Steam or GOG installed it, keep openQ4 in a separate folder, and let openQ4 find Quake 4.

**Portable setup:** if you want everything in one folder, put these side by side:

- `baseoq4/` from openQ4
- `q4base/` copied from your Quake 4 installation

> [!IMPORTANT]
> Never copy Quake 4's `.pk4` files into `baseoq4/`; that folder is for openQ4's own files. Copy the **complete** `q4base/` folder, including the language archive without a number, such as `zpak_english.pk4`. Files such as `zpak_english_01.pk4` are small patches, not a replacement. Without the main language archive, characters are silent and some story scenes never continue. If it's missing, verify or reinstall the game; don't rename a patch file.

## If auto-detection fails

Start openQ4 with `fs_basepath` pointing at the **Quake 4 folder that contains `q4base`**:

```text
openQ4-client_x64 +set fs_basepath "C:\path\to\Quake 4"
```

Point it at the folder above `q4base`, never at `q4base` or `baseoq4` themselves. File locations are explained in more detail in [TECHNICAL.md](../../TECHNICAL.md).

## Updating openQ4

openQ4's own content comes in three packs in `baseoq4/`:

| Pack | What's in it | Size |
| --- | --- | ---: |
| `pak0.pk4` | Menus, fonts, text, shaders and other core files | about 14 MB |
| `pak1.pk4` | Baked light grids (pre-calculated lighting) | about 270 MB |
| `pak2.pk4` | Loading screens | about 530 MB |

The light grids and loading screens rarely change, and each release's notes say whether they did. When they didn't, you don't need the whole package:

1. Download the **update archive** for your platform, for example `openq4-<version>-windows-x64-update.zip` or `openq4-<version>-linux-x64-update.tar.xz`. It holds everything except `pak1.pk4` and `pak2.pk4`.
2. Extract it **into your existing openQ4 folder**, replacing files. It has no folder of its own inside.

If a release did change `pak1.pk4` or `pak2.pk4`, download that file from the release page too and save it in `baseoq4/` under the same name, or install the full package instead. If a pack is missing or out of date, openQ4 tells you which one and where to get it when it starts.

The Windows installer and the Linux AppImage always update as whole downloads. Windows debugging files (`.pdb`) are not in the player package; they are published separately as `openq4-<version>-windows-<arch>-symbols.zip` for anyone analysing a crash.

## Platform notes

### Windows

- Use the installer or the `.zip`; both contain everything openQ4 needs.
- openQ4 is tested mainly on Windows 10 and 11. Windows 7, 8 and 8.1 should still work but get less testing.
- If openQ4 crashes, look in the `crashes/` folder beside the program for a log and a dump file, and attach both to your report.
- **Windows ARM64 packages are experimental.** They are built natively for Arm, and automated tests start the dedicated server, but nothing has started the game client automatically yet, and nobody has reported running them on real hardware. If the game doesn't start, try each renderer in turn with `+set r_renderApi vulkan` or `+set r_renderApi gl`. Please tell us what happens, even if it simply works, with the Windows ARM64 issue template.

### Linux

- **The easiest way to play** is the AppImage, a single file that runs without installing: `openq4-<version>-x86_64.AppImage` on most PCs, or the clearly marked `openq4-<version>-preview-aarch64.AppImage` on ARM64. Make it executable with `chmod +x openq4-*.AppImage` and run it.
- The AppImage contains openQ4 and the libraries it needs, but **not** the Quake 4 assets. It finds your Steam or GOG installation the same way the other packages do; if it doesn't, add `+set fs_basepath "/path/to/Quake 4"` when you launch it.
- The `.tar.xz` archive has the same game as loose files, plus the dedicated server, the Steam Deck launcher and offline documentation. Extract it anywhere.
- openQ4 targets a 64-bit desktop comparable to Ubuntu 24.04, with working OpenGL graphics drivers. It runs on both Wayland and X11 desktops.
- **Linux ARM64 packages are preview builds** until they have been fully tested on real ARM64 hardware. They need a 64-bit ARM computer with full desktop OpenGL drivers; boards that only offer OpenGL ES aren't supported.

### Steam Deck

- Launch `openQ4-steamdeck` from the Linux archive. It turns on the Deck's controls and settings (it sets `OPENQ4_STEAMDECK=1`).
- Direct launches of `openQ4-client_x64` on a Steam Deck or SteamOS also auto-select the Deck profile, unless you've already chosen another one.
- Tune the controls under **Settings → Game Options → Controller**: gyro, touchpad, touchscreen and the low-battery rumble limit.
- If the game window has trouble starting, set `OPENQ4_FORCE_X11=1` to run it through the older X11 display system instead of Wayland.
- When reporting a controller problem, run `listControllers` in the console and include what it prints.
- The [Steam Deck guide](steam-deck.md) has the details.

### macOS

**The simplest option is the signed build.** Schnapple publishes a [signed and notarized openQ4 through Mac Source Ports](https://www.macsourceports.com/game/quake4) that runs on both Apple Silicon and Intel Macs on macOS 11 or later. Follow the download and install steps there; you still need your own Quake 4 game files.

Our GitHub macOS packages remain a preview for Apple Silicon/arm64 Macs on macOS 11 or later. Players have run them on macOS 26, including the campaign and multiplayer with sound, but nothing older than the current macOS has been tested. Our GitHub releases do not yet publish Intel Mac or universal2 packages, and Rosetta is not a supported release target. The rest of this section is about those GitHub packages.

**Installing.** Open the DMG or unpack the `-unsigned.tar.gz` archive, then drag `openQ4.app` to `/Applications` (or another folder you can write to) and open it. The app contains openQ4's `baseoq4` data and signed SP/MP modules, so the app alone is all you need to play. Keep the rest of the package only if you want the separate diagnostic client, the dedicated server or the support collector script. Don't copy Quake 4's `q4base` files into the app.

**Signing and first launch.** Releases built with Apple Developer ID credentials publish signed/notarized OpenGL and Metal bridge DMGs. Those credentials aren't set up yet, so current releases publish clearly labeled `-unsigned.tar.gz` archives instead. Unsigned archives are ad-hoc signed only for bundle validity (enough for macOS to accept the app's structure) and are not notarized, so macOS asks you to approve the app the first time you open it.

**Which package.** There are two: **OpenGL**, and **Metal bridge**, which runs the same OpenGL renderer through Apple's Metal; it is not a native Metal renderer. Both include openQ4's experimental Vulkan renderer too. Apple doesn't provide Vulkan, so it runs through MoltenVK, a translation layer from Vulkan to Metal that ships inside the app.

**Renderer choice.** A fresh install starts on Automatic, which tries Vulkan and falls back to OpenGL. A saved OpenGL choice stays on OpenGL. Vulkan is experimental on macOS (it is fully supported only on Windows), and only one player has reported it working there. If it can't start, openQ4 logs why and uses OpenGL instead. To stay on OpenGL, enter `r_renderApi gl` in the console and restart. See [Display Settings](display-settings.md#vulkan-on-macos-through-moltenvk).

**Sound** works out of the box: the packages bundle OpenAL Soft, so no Homebrew or separate audio install is required. Its licence notice and source archive are inside `openQ4.app/Contents/Resources/licenses/openal-soft/`.

**If it crashes,** attach the full terminal output as text, plus `openq4.log` and any `.ips` crash report. If the package includes `collect_macos_support_info.sh`, run it from the package folder. See [macOS Support Data](macos-support-data.md).

#### Getting the game files from a GOG installer on a Mac

openQ4 needs the installed game's `q4base/` folder; it can't read a GOG setup `.exe` or its `.bin` files directly. Either copy the complete `q4base/` folder from a Windows installation, or extract the GOG offline installer on the Mac:

- **Without Terminal:** [Mac Source Ports Extractor](https://www.macsourceports.com/utility/extractor/) opens supported GOG installers. Keep the `.exe` and its `.bin` files together and extract into a new folder. A player [confirmed this works for Quake 4](https://github.com/themuffinator/openQ4/issues/185#issuecomment-6253641976), though not every installer version has been tried.
- **With Homebrew:** install [innoextract](https://formulae.brew.sh/formula/innoextract) with `brew install innoextract`. Keep the setup `.exe` and all its `.bin` parts together under their original names. In a new, empty folder, run (replacing the example path):

  ```sh
  innoextract --gog "/path/to/setup_quake_4.exe"
  ```

Find `q4base/` in the extracted files (it may be inside an `app/` folder) and keep the whole folder, including the language archives. Point openQ4 at the folder **above** it, for example:

```sh
"/Applications/openQ4.app/Contents/MacOS/openQ4" +set fs_basepath "/path/to/extracted/app"
```

Keep the game files outside the `.app`. Installer formats vary: innoextract can't open every installer version, and some GOG installers need `unar` or `unrar` as well. If extraction fails, note the installer's exact filename and the error when you ask for help, or copy `q4base/` from a Windows installation instead.

## Reporting a problem

openQ4 doesn't write a log file unless you ask it to. To capture one, start the game with:

```text
openQ4-client_x64 +set logFile 2 +set logFileName logs/openq4.log
```

Reproduce the problem, quit, and find `openq4.log` in the `baseoq4/logs` folder inside your save folder:

| System | Save folder |
|---|---|
| Windows | `%LOCALAPPDATA%\openQ4` |
| Linux and Steam Deck | `~/.local/share/openq4` |
| macOS | `~/Library/Application Support/openQ4` |

Attach the log to a new [issue](https://github.com/themuffinator/openQ4/issues) with your system, graphics card and the openQ4 version. After a Windows crash, also attach the files from the `crashes/` folder. On a Mac, follow [macOS Support Data](macos-support-data.md).

## Next steps

- Find your way around the menus: [Client Settings](client-settings.md).
- Tune graphics and screen options: [Display Settings](display-settings.md).
- Set up keys, mouse or controller: [Input Settings](input-settings.md).
- Choose a campaign or install The Awakening: [Campaigns](campaigns.md).
- Host a game: [Server Setup](server-setup.md).
