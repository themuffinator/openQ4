# macOS Support Data

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This page shows Mac players how to put together a useful bug report. macOS
support is a preview for Apple Silicon (arm64) Macs, and the maintainer may not
have a Mac to reproduce your problem, so a complete report helps far more than
a screenshot. Reports that openQ4 simply works are welcome too, especially on
a macOS version older than the current one.

The short version:

1. [Capture the terminal output](#capture-terminal-output) while you
   reproduce the problem.
2. [Run the support collector](#run-the-support-collector) to make a support
   archive.
3. Attach both, plus the crash report if macOS made one, to your
   [GitHub issue](https://github.com/themuffinator/openQ4/issues).

Never attach your retail Quake 4 `q4base/*.pk4` files.

## What to attach

Attach or paste as many of these as you can:

- The exact openQ4 version and the name of the file you downloaded, such as
  `openq4-0.14.1-macos-arm64-metal-unsigned.tar.gz`, and whether you used the
  OpenGL package, the Metal bridge package, or both.
- How you started it: `openQ4.app`, `openQ4-client_arm64` from Terminal,
  `openQ4-ded_arm64` from Terminal, or more than one of these. Say whether
  `openQ4.app` ran from the download or after you dragged it to
  `/Applications`, and whether the Terminal tools ran from a complete copy of
  the package.
- Your macOS version and Mac model. The output of `sw_vers` plus the chip, such
  as M4 Max or M5, is enough.
- Full terminal output as text, not only a screenshot.
- `~/Library/Application Support/openQ4/baseoq4/logs/openq4.log`, if it exists.
- The matching `.ips` or `.crash` file from `~/Library/Logs/DiagnosticReports`,
  if macOS wrote one.
- `SYMBOLS.txt` from the package folder (the support archive also copies it to
  `package/SYMBOLS.txt`).
- The support archive. Its `package/build-metadata.txt` records exactly which
  build you have; without the archive, copy the `openq4_commit` and
  `openq4_game_commit` lines from the package's `VERSION.txt` instead.
- For sound problems, the OpenAL lines from `openq4.log`: `OpenAL vendor:`,
  `OpenAL renderer:`, `OpenAL active device:`, and any `OpenAL EFX ...` warning
  or status lines.

If you share files from the support archive one at a time, the most useful are
`system/rosetta.txt`, `logs/renderer-summary.txt`, `logs/renderer-config.txt`,
`package/binary-architecture.txt`, `package/dylib-dependencies.txt`,
`package/signing.txt` and `package/quarantine.txt`.

## Capture terminal output

In Terminal, run the app's executable and send its output to a text file. You
can run it from any folder:

```sh
cd /tmp
"/Applications/openQ4.app/Contents/MacOS/openQ4" > ~/Desktop/openq4-terminal.txt 2>&1
```

For a dedicated-server problem, run the dedicated server from the package
folder and save its output separately:

```sh
cd "/path/to/openQ4 package"
./openQ4-ded_arm64 +set dedicated 1 > ~/Desktop/openq4-ded-terminal.txt 2>&1
```

Reproduce the problem, then attach the text file or paste its full contents
into the issue.

## Run the support collector

macOS packages include `collect_macos_support_info.sh` in the package folder.
Run it from Terminal:

```sh
cd "/path/to/openQ4 package"
./collect_macos_support_info.sh
```

If macOS refuses to run the script directly, run it through `sh`:

```sh
sh ./collect_macos_support_info.sh
```

It creates an archive named like:

```text
openq4-macos-support-YYYYMMDD-HHMMSSZ.tar.gz
```

### What the collector protects

Review the archive before you attach it publicly. The collector:

- replaces `/Users/<name>` in paths and removes anything that looks like an
  email address;
- does not dump the environment, does not launch openQ4, and does not copy retail `q4base` PK4 assets;
- reads only renderer and performance settings from your config (`r_*`,
  `com_machineSpec` and `com_performancePreset`), never your bindings, player,
  network or audio-device settings;
- does not follow symlinked package, log, or crash-report inputs; it notes each skipped link instead;
- notes a skipped file instead of copying it when a DiagnosticReports filename contains unusual characters;
- still makes an archive when Terminal did not set `HOME`, and notes that it
  skipped your logs and crash reports;
- strips control characters from copied text, keeps only the last 2 MiB of a
  log or text file and the last 8 MiB of a crash report (noting the cut), and
  caps the output of system tools;
- makes the archive readable only by you and never overwrites an existing
  archive.

### What the archive contains

| File | What it records |
| --- | --- |
| `package/path-resolution.txt` | The package root, app path, expected loose runtime paths, and any log lines that mention `fs_basepath`, `fs_cdpath`, and `fs_savepath`, all gathered without launching openQ4 |
| `package/build-metadata.txt` | The `VERSION.txt` lines, including `openq4_commit`, `openq4_dirty`, `openq4_game_commit` and `openq4_game_dirty`, plus the game module file names in `baseoq4/` |
| `package/SYMBOLS.txt` | The symbol manifest, if the package has one (see [Crash reports and symbols](#crash-reports-and-symbols)) |
| `logs/openal-summary.txt` | From an existing log: the OpenAL vendor, renderer, version, requested, default or active device, and `OpenAL EFX` warning and status lines |
| `logs/renderer-summary.txt` | From an existing log: renderer start-up, driver workarounds, lighting fallbacks, render targets and MSAA, the selected folders and game module, and crash signals, such as `R_InitOpenGL`, `Renderer driver quirks`, `material_interaction`, `GL_FRAMEBUFFER_*`, `Forward render target MSAA`, `Filesystem paths:`, `Selected game module:`, `ARB2 interaction driver bypass`, `Game module search failed:`, `Game module load failed:`, `dlopen ... failed:` and `fatal signal SIGSEGV` |
| `logs/renderer-config.txt` | Only the renderer and performance lines from a saved `openQ4Config.cfg`, so graphics settings can be compared without your full config |
| `system/rosetta.txt` | `arch`, `uname -m` and `sysctl.proc_translated`, to spot Terminal running under Rosetta, which is not supported |
| `package/binary-architecture.txt` | `file` and `lipo -archs` for the app, Terminal tools and game modules, to spot a wrong-architecture file |
| `package/dylib-dependencies.txt` | `otool -L` and `otool -D` for the same files, to spot unexpected libraries or broken game module names |
| `package/signing.txt` | `codesign`, `spctl --assess --type execute --verbose=4` and `xcrun stapler validate` checks for the app, Terminal tools and game modules, without launching openQ4 |
| `package/quarantine.txt` | The names of extended attributes and whether `com.apple.quarantine` is present; their values are not copied |

The architecture, signing and quarantine reports also look for x64 or x86
openQ4 executables if any are present, so wrong-architecture files show up even
though packages are Apple Silicon only.

## If the game can't find or load its game module

If start-up stops with `couldn't find game dynamic library`, include the
`Game module search failed:` line. It lists the package folders that were
checked, which tells a damaged app apart from an older package layout. A
current app looks in `openQ4.app/Contents/Frameworks` for
`game-sp_arm64.dylib` and `game-mp_arm64.dylib`. Do not copy game modules into
your retail `q4base` folder.

If the module was found but start-up stops with `couldn't load game dynamic
library`, include both the `Game module load failed:` line and the
`dlopen ... failed:` line before it. Together they name the module and macOS's
reason, such as a code-signing or missing-library problem.

## Start-up crashes and flat lighting

Some Macs crashed at start-up in Apple's older OpenGL 2.1 driver, as in
[GitHub issue #73](https://github.com/themuffinator/openQ4/issues/73), with the
last lines of output in the lighting shader setup. Current builds default to
`r_appleARB2Interactions 0`: normal surfaces draw their lighting with a
compatible shader, and anything else falls back to the simpler
`SimpleInteraction.vfp` program, so you get proper lighting rather than the
flat look of the earlier workaround.

For diagnosis, `r_appleARB2Interactions 1` and `2` force the simple and full
older lighting paths, and `3` is an emergency mode that skips lighting
altogether. A change needs `vid_restart`.

If your log contains `ARB2 interaction driver bypass`, the emergency mode is on
(older packages used it by default). It skips lighting to avoid the crash, so
the game looks flat. Please still attach the terminal output and the support
archive.

For a start-up crash, keep the lines around any of these markers when you paste
output:

- `----- R_InitOpenGL -----`
- `R_ReloadARBPrograms`
- `interaction color mode`
- `renderer startup phase`
- `last renderer startup phase`
- `first ARB2 interaction handoff`
- `automatic stock GLSL interactions with simple ARB per-surface fallback`
- `material_interaction`
- `ARB2 interaction driver bypass`
- `ARB2 interaction bypass state restored`
- `ARB2 interaction bypass light scale`
- `ARB2 interaction bypass light scale skipped`
- `ARB2 interaction bypass ambient rescue`
- `ARB2 interaction bypass frame tail`
- `ARB2 light interaction`
- `Renderer upload manager`
- `using ARB2 renderSystem`
- `Unsupported Apple OpenGL 2.1 compatibility path`
- `SimpleInteraction.vfp`
- `Forward render target MSAA: requested`
- `idRenderTexture::InitRenderTexture`
- `GL_FRAMEBUFFER_`
- `Filesystem paths:`
- `Selected game module:`
- `Game module search failed:`
- `Game module load failed:`
- `dlopen ... failed:`
- `fatal signal SIGSEGV`
- `last game module phase:`
- `Quality profile:`
- `reported OpenGL framebuffer attributes:`
- `Apple GL 2.1 interaction routes:`
- `the Apple GL 2.1 corridor was not applied`

## Crash reports and symbols

macOS packages include a small `SYMBOLS.txt` file in the package folder. It
names the matching debug-symbol (dSYM) archive, a separate
`...-symbols.tar.xz` download, and lists the checksums and identifiers of the
packaged programs.

If you attach an `.ips` or `.crash` report, also attach `SYMBOLS.txt`, or run
the support collector, which copies it to `package/SYMBOLS.txt`. The maintainer
uses it to pick the right symbols to read your crash report; the support
archive does not include the `.dSYM` debug files themselves. Maintainer details
are in [macOS Symbolication Workflow](../dev/macos-symbolication.md).

## Manual locations

If the collector isn't available, collect these by hand:

```text
~/Library/Application Support/openQ4/baseoq4/logs/openq4.log
~/Library/Logs/DiagnosticReports/openQ4*.ips
~/Library/Logs/DiagnosticReports/openQ4*.crash
~/Library/Logs/DiagnosticReports/openQ4-client*.ips
~/Library/Logs/DiagnosticReports/openQ4-client*.crash
~/Library/Logs/DiagnosticReports/openQ4-ded*.ips
~/Library/Logs/DiagnosticReports/openQ4-ded*.crash
```

Also include the output of:

```sh
sw_vers
uname -a
system_profiler SPHardwareDataType
system_profiler SPDisplaysDataType
```

## Package layout check

The macOS app is self-contained:

```text
openQ4.app/Contents/MacOS/openQ4
openQ4.app/Contents/Resources/baseoq4/
openQ4.app/Contents/Frameworks/game-sp_arm64.dylib
openQ4.app/Contents/Frameworks/game-mp_arm64.dylib
```

The app finds openQ4's own files inside `Contents/Resources` wherever Finder
starts it from, and finds your Steam or GOG Quake 4 installation separately for
the retail `q4base` files. Dragging only `openQ4.app` to `/Applications` is supported. Keep the whole extracted package only if you use the Terminal client, the
dedicated server or the support collector, which find the app next to them. Do
not add retail game files to the signed app.
