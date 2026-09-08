<div align="center">

# Building openQ4 from Source

</div>

This guide covers everything required to compile openQ4 from source on Windows, Linux, and experimental macOS.

Experimental Android native builds, SigmaTouch host integration and the optional desktop GLES renderer are covered in the [Android/GLES build guide](docs/dev/android-build.md). These are source-build targets; existing desktop release packages remain unchanged.

> [!NOTE]
> **Regular players do not need to build from source.** Download the latest release from the [Releases page](https://github.com/themuffinator/openQ4/releases) and follow the [Getting Started instructions](README.md#getting-started) instead.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [GameLibs Companion Repository](#gamelibs-companion-repository)
- [Build Setup](#build-setup)
- [Build Options](#build-options)
- [Validation Scripts](#validation-scripts)
- [Building on Windows](#building-on-windows)
- [Building on Linux / Experimental macOS](#building-on-linux--experimental-macos)
- [Output Files](#output-files)
- [Packaging a Distributable](#packaging-a-distributable)

---

## Prerequisites

### Compiler

| Platform | Minimum | Notes |
|----------|---------|-------|
| **Windows** | MSVC 19.46+ (Visual Studio 2026+) | Use the Developer PowerShell or run `tools/build/openq4_devcmd.cmd` to initialise the environment |
| **Linux** | GCC 13+ or Clang 17+ | Distro packages are fine |
| **macOS (experimental)** | Xcode 16+ / Clang 17+ | Install Command Line Tools via `xcode-select --install` |

### Build Tools

- **[Meson](https://mesonbuild.com/)** 1.6.0 or newer
- **[Ninja](https://ninja-build.org/)** (recommended backend)
- **Python 3** (used by `tools/build/sync_icons.py`, `baseoq4/pak0.pk4` / `baseoq4/pak1.pk4` generation, and the wrapper scripts)

### Windows Note

On Windows, always invoke Meson through `tools/build/meson_setup.ps1` rather than calling `meson` directly from an arbitrary shell. The wrapper ensures MSVC tools (`cl.exe`, `link.exe`, etc.) are on `PATH` and performs automatic icon-set synchronisation before setup, compile, and install steps.

For Windows `arm64` builds, openQ4 also needs an ARM64 OpenAL Soft package. The release workflow prepares that automatically. For local builds, use `tools/build/prepare_windows_openal.ps1` to create one under `.tmp/`, then pass `-Dopenal_root_override=<path>` during `meson setup`.

```powershell
# Open a regular PowerShell window and use the wrapper:
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 <meson-command> [args...]
```

Alternatively, open `tools/build/openq4_devcmd.cmd` first to initialise the Visual Studio environment, then call `meson` directly.

---

## GameLibs Companion Repository

openQ4's game code (single-player and multiplayer modules) lives in a **separate companion repository** — [openQ4-game](https://github.com/themuffinator/openQ4-game). This separation clearly identifies the SDK-licensed components derived from the [Quake 4 SDK](https://www.moddb.com/games/quake-4/downloads/quake-4-sdk-v15), and makes openQ4-game the canonical source-input repository for SDK/game-library code.

> [!IMPORTANT]
> **The openQ4 build expects openQ4-game to be checked out alongside openQ4**, at `../openQ4-game` relative to this repository. If the companion repository is missing or at a different path, game-module builds will fail.

### Setting Up

```bash
# Clone both repositories side-by-side:
git clone https://github.com/themuffinator/openQ4.git
git clone https://github.com/themuffinator/openQ4-game.git

# Result:
#   ./openQ4/            ← this repository
#   ./openQ4-game/   ← game library source
```

To use a custom location, set the environment variable before configuring:

```bash
export OPENQ4_GAMELIBS_REPO=/path/to/openQ4-game   # Linux / macOS
$env:OPENQ4_GAMELIBS_REPO = "C:\path\to\openQ4-game"  # PowerShell
```

To rebuild the game libraries as part of the openQ4 build, set `OPENQ4_BUILD_GAMELIBS=1` before running compile.

During configure, openQ4 stages the canonical `src/game` and `src/mpgame` source inputs from openQ4-game into `.tmp/gamelibs_stage/` on Windows or `.tmp/gamelibs_stage-<system>-<arch>/` on other targets and writes a source manifest with file hashes and git state. Meson requires that manifest before compiling a distinct SP module from `src/game` and MP module from `src/mpgame`, so Linux and experimental macOS builds consume the companion repository directly without maintaining local game-source mirrors. The standalone openQ4-game build supports Windows/MSVC, Linux x64/ARM64 with GCC or Clang, and experimental macOS/Clang developer outputs for compiler and ABI validation; openQ4's staged build remains the integrated runtime, packaging, and gameplay-validation path.

---

## Build Setup

Third-party libraries such as SDL3, GLEW, OpenAL Soft, and stb_vorbis are managed as Meson subprojects. On Linux, the default SDL3 backend requires OpenGL/EGL plus the SDL3 runtime integration development packages. The Linux dedicated target uses the same compile-time declarations but a separate headless source/link set, so its executable does not directly depend on OpenAL, OpenGL/GLX, SDL, PipeWire, Wayland, or X11/Xext at runtime. Staged and extracted-package validation apply a fail-closed core-runtime allowlist to both that executable and the multiplayer module it loads. The bundled SDL build compiles against PipeWire, Wayland, xkbcommon, and libdecor headers, then loads their stable runtime SONAMEs dynamically; PipeWire and libdecor remain optional on the player's system. IBus support is enabled when its headers are available, with the DBus-based Fcitx path always available. The native IBus/Fcitx UI owns composition/candidate presentation and is positioned from openQ4's active console/GUI text-input area. Committed text is limited to Quake 4's stock single-byte edit-widget and bitmap-font range; codepoints outside that range are ignored instead of being corrupted into unrelated characters. X11/Xext are optional helpers used only when available for the SDL3 NVIDIA VRAM probe and XWayland fallback. Configure with `-Dlinux_x11=disabled` to build the native Wayland/EGL-only path and exclude SDL's X11 driver, GLX GLEW code, and engine X11 helpers. An explicit `enabled` or `disabled` value forces the bundled SDL fallback so the requested driver set is honored; `auto` may use a suitable system SDL and inherits its compiled driver set. The legacy `-Dplatform_backend=native` path still requires X11/GLX and VidMode development packages on Linux and the legacy Carbon framework on macOS. On experimental macOS builds, SDL3 is the default platform path and does not link Carbon; it is also the only release backend, while the native Cocoa/OpenGL backend is comparison-only diagnostic infrastructure. `-Dmacos_graphics_bridge=metal` enables the Metal-ready SDL3/Cocoa bridge while keeping the stock-compatible OpenGL renderer rather than introducing a native Metal rewrite. The renderer/backend policy is tracked in `docs/dev/macos-renderer-backend-policy.md`, and the legacy native backend containment policy is tracked in `docs/dev/macos-native-backend-containment-policy.md`. Experimental macOS release and validation jobs use `-Dmacos_openal_provider=system` with the checksum-pinned, dynamically linked OpenAL Soft runtime built by `tools/build/prepare_macos_openal_soft.sh`; published apps carry that runtime, its LGPL notice, and its corresponding source archive. Apple's framework remains a compile-only compatibility diagnostic. The macOS audio-provider policy is tracked in `docs/dev/macos-openal-provider-policy.md`.

The macOS `system` option is deliberately pkg-config-only. Prepare the same
pinned OpenAL Soft 1.25.1 dependency used by CI, expose it to Meson, then stage
its runtime/license/source payload after `meson install`:

```bash
prepared_root="$PWD/.tmp/openal-soft-macos-arm64"
bash tools/build/prepare_macos_openal_soft.sh --architecture arm64 --output-root "$prepared_root"
export PKG_CONFIG_PATH="$prepared_root/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# Run the normal Meson setup/compile/install commands with -Dmacos_openal_provider=system.
bash tools/build/prepare_macos_openal_soft.sh --verify-only --architecture arm64 \
  --output-root "$prepared_root" --stage-install-dir .install
```

Meson copies a wrap's `patch_directory` only when it first extracts that subproject. After pulling changes under `subprojects/packagefiles/sdl3/`, an existing ignored `subprojects/SDL3-*` tree can therefore retain stale build definitions even after `setup --wipe`. Refresh that generated extraction before rebuilding; this is especially important when validating `-Dlinux_x11=disabled`, because stale SDL definitions can silently retain X11 libraries:

```bash
bash tools/build/meson_setup.sh subprojects purge --confirm sdl3
```

```powershell
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 subprojects purge --confirm sdl3
```

Then rerun the normal `setup --wipe`, compile, and install steps. The purge removes the generated SDL source extraction, not `subprojects/packagefiles/sdl3/` or the configured build directory.

On current Debian/Ubuntu systems, install the SDL3/Linux package set (`binutils`, `libasound2-dev`, `libdbus-1-dev`, `libdecor-0-dev`, `libdrm-dev`, `libegl1-mesa-dev`, `libfribidi-dev`, `libgbm-dev`, `libgl1-mesa-dev`, `libopengl-dev`, `libibus-1.0-dev`, `libjack-dev`, `libopenal-dev`, `libpipewire-0.3-dev`, `libpulse-dev`, `libsndio-dev`, `libthai-dev`, `libudev-dev`, `libwayland-dev`, and `libxkbcommon-dev`). Add `libx11-dev`, `libxext-dev`, `libxcursor-dev`, `libxfixes-dev`, `libxi-dev`, `libxrandr-dev`, `libxss-dev`, `libxtst-dev`, `libxxf86dga-dev`, and `libxxf86vm-dev` when validating the optional SDL3 X11 helper path or the native Linux backend.

---

## Build Options

Pass any of these with `-D<option>=<value>` on the `meson setup` command line:

| Option | Default | Description |
|--------|---------|-------------|
| `build_engine` | `true` | Build `openQ4-client_<arch>` and `openQ4-ded_<arch>` |
| `build_games` | `true` | Build game modules |
| `build_game_sp` | `true` | Build single-player game module |
| `build_game_mp` | `true` | Build multiplayer game module |
| `platform_backend` | `sdl3` | `sdl3` or `legacy_win32` on Windows, `sdl3` or `native` on Linux/experimental macOS; macOS `native` is comparison-only and not a release backend |
| `linux_x11` | `auto` | Linux SDL3 X11/XWayland driver and optional engine X11 helpers: `enabled`/`disabled` force bundled SDL with that selection; `auto` may use system SDL; `disabled` is Wayland/EGL-only |
| `macos_graphics_bridge` | `opengl` | Experimental macOS-only graphics bridge: `opengl` or `metal`; `metal` requires `platform_backend=sdl3` and keeps rendering on the OpenGL compatibility path |
| `macos_openal_provider` | `apple_framework` | Experimental macOS-only selector: official release/validation jobs explicitly use `system` with pinned bundled OpenAL Soft (`dependency('openal', method: 'pkg-config')`); `apple_framework` is retained for compatibility diagnostics |
| `build_renderer_vk` | `true` | Build the Vulkan renderer module `renderer-vk_<arch>`; requires `platform_backend=sdl3`. On macOS the module runs through MoltenVK, a Vulkan-on-Metal translation layer, and stays opt-in behind `r_renderApi vulkan` with OpenGL as the default renderer |
| `version_track` | `dev` | Build track label (`stable`, `dev`, `beta`, `rc`) |
| `version_iteration` | *(empty)* | Dot-separated iteration counter for pre-release builds |
| `version_base_override` | *(empty)* | Override the generated release version without editing `meson.build` |
| `enforce_msvc_2026` | `false` | Enforce MSVC 2026+ requirement (Windows only) |

---

## Validation Scripts

openQ4 includes local validation profiles under `tools/validation/`. They share one Python runner and use the platform build wrappers so Windows validation still goes through `tools/build/meson_setup.ps1`.

GitHub Actions runs the same validation entrypoints on every pushed commit, pull request, and manual dispatch. Branch pushes run the faster push profile across Linux x64, Linux ARM64, experimental macOS ARM64, and Windows. Pull requests/manual dispatches run the full PR profile on Windows x64, Linux ARM64, and experimental macOS ARM64; the staged Linux ARM64 client also runs an assetless no-map renderer startup smoke under a virtual X display. Native Wayland PR jobs run assetless lifecycle, fullscreen/window, input-capture, and display cases under headless Weston on both x64 and ARM64. They verify that preferred libdecor is actually loaded, that the opt-out prevents it from loading, and that the Wayland-only build has no direct X11/GLX dependency. Staged Linux package validation rejects direct PipeWire dependencies from clients and permits only core C/C++/POSIX/compiler runtimes for the x64 and ARM64 dedicated executable plus its loaded MP module. These hosted ARM64 cases prove build/package and window/compositor/input startup, not audio or real SP/MP gameplay.

Manual releases default to `linux_arm64_support_tier=preview`. Selecting `first-class` is a hard gate: the workflow requires accepted physical ARM64 native-Wayland SP/MP, stock-map dedicated-server, audio, input, display, and package evidence in [the Linux ARM64 signoff record](docs/dev/linux-arm64-signoff-evidence.md). The accepted TOML must come from an immutable `linux_arm64_evidence_ref` and match the triggering openQ4 SHA, resolved openQ4-game SHA, release version/tags, exact first-class archive name, archive SHA-256, and all four runtime ELF SHA-256 values. Removing the preview label without that candidate-bound record fails before the release matrix builds.

Pull requests also run Linux Sanitizer Validation on x64 with ASan+UBSan enabled. The lane builds the engine and staged game-module code without precompiled headers, stages the instrumented runtime separately from release dependency validation, then runs an assetless SDL3 window/fullscreen lifecycle smoke through native Wayland under headless Weston and Mesa software rendering. Sanitizer failures stop the smoke immediately, and its logs are retained with the build diagnostics.

Experimental manual macOS release artifacts are Apple Silicon/arm64 only. The detailed matrix policy lives in `docs/dev/macos-support-matrix-policy.md`: current packages are `arm64 only`, target macOS 11 or later, and require separate oldest-floor plus latest-public-macOS signoff evidence before macOS support can be promoted beyond experimental. Credentialed runs publish signed/notarized DMGs, while runs without Apple Developer ID signing and notarization credentials publish clearly labeled unsigned/unnotarized `-unsigned.tar.gz` archives. The manual release workflow defaults to `macos_support_tier=experimental`; selecting `macos_support_tier=first-class` fails the release matrix instead of publishing unsigned fallback packages when signing/notarization secrets are missing. Hosted `macos-15-intel` jobs now provide experimental x86_64 engine/GameLib compile, stage, architecture, assetless renderer, and dedicated-module-loader coverage for both bridge variants, but Intel Mac and universal2 packages are not published until physical Intel gameplay/package/signing evidence or a fully validated universal packaging lane exists. Rosetta is not a supported release target.

### IPv4 Networking Self-Test

After building the engine, run the no-argument `netIPv4SelfTest` command from the openQ4 console:

```text
netIPv4SelfTest
```

This developer diagnostic checks strict numeric IPv4 parsing, DNS-enabled
`localhost`, invalid ports, and isolated network-CVar handling. It binds an
explicit `127.0.0.1` high port plus an ephemeral sender, verifies initialized
counters and bidirectional payloads, accepts an exact-capacity datagram, rejects
an oversized datagram without losing the following marker, and exercises socket
close/reopen. A successful run ends with `IPv4 network self-test: passed`; any
failed check ends with `IPv4 network self-test: FAILED` and should be treated as
a validation failure.

The self-test is local and non-destructive. It does not prove public
reachability, router NAT or firewall configuration, external DNS records,
master-server availability, or end-to-end internet play; validate those
separately when they are relevant to a change.

### IPv6 Networking Self-Test

Run the no-argument `netIPv6SelfTest` command from the openQ4 console:

```text
netIPv6SelfTest
```

This developer diagnostic checks bracketed and unbracketed IPv6 literals,
rejected malformed endpoints, IPv4-mapped normalization, canonical RFC 5952
text and its round-trip through the parser, and LAN classification of loopback
and link-local addresses. It then binds an IPv6-only receiver and sender on
`::1` so every datagram provably crosses the IPv6 socket, verifying initialized
counters and bidirectional payloads, an exact-capacity datagram, rejection of an
oversized datagram without losing the following marker, and socket
close/reopen. A final dual-stack check binds one port and confirms it receives
over both families. A successful run ends with
`IPv6 network self-test: passed`; any failed check ends with
`IPv6 network self-test: FAILED` and should be treated as a validation failure.

A host with IPv6 disabled is a supported configuration: the parsing and
formatting checks still run, and the transport section reports that it was
skipped rather than failing.

The same limitations apply as for IPv4: the IPv6 self-test is local and
non-destructive, and does not prove public reachability, firewall
configuration, external DNS records, or end-to-end internet play.

### macOS Static Validation

Use this track for macOS support work when you do not have access to macOS. It runs static/policy tests, synthetic Apple GL 2.1 checks, synthetic macOS package/archive fixtures, shell syntax checks, and push/PR dry-run validation. It does not run openQ4 on macOS.

```powershell
powershell -ExecutionPolicy Bypass -File tools/validation/validate_macos_static.ps1
```

```bash
bash tools/validation/validate_macos_static.sh
```

You can also call the shared runner directly:

```bash
python tools/validation/openq4_validate.py macos-static
```

When shared renderer code changed, renderer self-test binaries may be run only on an available non-macOS host after a local build/stage:

```bash
python tools/validation/openq4_validate.py macos-static --runtime --runtime-cases renderer-default-safety-selftest --runtime-tiers auto --runtime-basepath "" --runtime-skip-official-pak-validation
```

See `docs/dev/macos-local-validation-track.md` for the evidence boundary and the checks owned by this no-platform-access profile.

### Push Validation

Use this before pushing local work. It runs lightweight Python checks, reconfigures/reuses `builddir/` as a debug build, and compiles the engine plus game modules.

```powershell
powershell -ExecutionPolicy Bypass -File tools/validation/validate_push.ps1
```

```bash
bash tools/validation/validate_push.sh
```

### PR Validation

Use this before opening or updating a pull request. It performs a clean release-style debug build in `.tmp/validation/pr-builddir`, stages `.install/`, and verifies the staged runtime payload contains the expected engine executables, SP/MP game modules, required `baseoq4` files, Windows diagnostic symbols when applicable, and no root-level build-only linker artifacts.

```powershell
powershell -ExecutionPolicy Bypass -File tools/validation/validate_pr.ps1
```

```bash
bash tools/validation/validate_pr.sh
```

Useful options:

- Add `--runtime` to run the safe renderer startup validation matrix after the staged install.
- Add `--runtime-skip-official-pak-validation` with a targeted no-map runtime case when intentionally running an assetless engine-startup smoke, such as CI coverage without retail Quake 4 PK4s.
- Add `--build-gamelibs` when you also want the Windows wrapper to build the standalone openQ4-game outputs during compile.
- Use `--game-libs-repo <path>` when the companion repository is not at `../openQ4-game`.
- Use `--build-dir <path>` plus `--no-clean` to validate a specific existing build tree.

### Linux Stock-Map Dedicated Gameplay

After staging a Linux package, use the opt-in stock-map harness on a native
Wayland session with the retail Quake 4 PK4s available. It starts the packaged
dedicated server on `mp/q4dm1`, connects the separately packaged client, and
requires a post-load dedicated-ready marker, matching declaration checksums,
ordered nonempty engine logs, player spawn, first active draw, the renderer tier
contract plus a nonzero benchmark sample window, a nontrivial screenshot, and
clean shutdown. The server gets an intentionally invalid SDL video-driver value
as a headless canary; touching any splash, console, window, or other video path
therefore fails the run instead of being hidden by a dummy backend. The run
keeps isolated homes, full logs, SHA-256s, and JSON/Markdown reports under the
requested `.tmp` evidence directory:

```bash
python tools/tests/linux_dedicated_stock_map_smoke.py \
  --dedicated-executable .install/openQ4-ded_$(uname -m | sed 's/x86_64/x64/;s/aarch64/arm64/') \
  --client-executable .install/openQ4-client_$(uname -m | sed 's/x86_64/x64/;s/aarch64/arm64/') \
  --basepath /path/to/Quake4 \
  --output-root .tmp/linux-dedicated-stock-map
```

Run this from an existing native Wayland session with `XDG_RUNTIME_DIR` and
`WAYLAND_DISPLAY` set. The harness forces both SDL video-driver spellings to
`wayland`, removes X11 overrides from the client environment, and never copies
or modifies the retail assets. Hosted assetless CI does not run this test.

For physical ARM64 signoff, add `--physical-hardware` to this dedicated/client
run too. The harness records independent host-virtualization signals and rejects
the attestation if a known VM/emulator is detected. A clean automated inspection
cannot prove bare metal; the flag remains an explicit operator attestation.
The accepted first-class evidence commit retains this exact `report.json` at
`docs/dev/linux-arm64-evidence/dedicated-report.json`; do not edit or reformat it
after the harness writes it.

### Linux Stock SP Wayland Save/Load And Audio

Use the separate opt-in SP harness to exercise the packaged client and SP game
module against retail media. It enters `game/airdefense1`, automatically skips
the opening cinematic, saves an isolated slot, reloads it, waits for the second
active gameplay lifecycle, validates a post-restore TGA, and requires OpenAL
plus the engine sound system to initialize without audio, OpenGL, or fatal
errors:

```bash
python tools/tests/linux_wayland_stock_sp_smoke.py \
  --install-root .install \
  --basepath /path/to/Quake4 \
  --output-root .tmp/linux-wayland-stock-sp
```

The runner requires a real socket named by `XDG_RUNTIME_DIR` and
`WAYLAND_DISPLAY`, launches only a client whose ELF architecture matches the
Linux host, forces SDL's Wayland driver, and removes `DISPLAY`. It keeps the
isolated home, save files, hashes, engine log, stdout/stderr, nontrivial
screenshot, and JSON/Markdown lifecycle reports below the output root. Normal
CI compiles the runner and executes its static contract only; it never runs the
retail-media test.

For physical ARM64 signoff, add `--physical-hardware` only when the operator can
attest that no VM or emulator is involved. The runner records the same host
inspection as the dedicated/client harness and rejects known virtualization
indicators before launch. Add
`--human-audio-playback-verified` only when a person actually heard gameplay
audio during the run. The automated result verifies software audio
initialization; it cannot hear speakers and does not infer audible playback.
Retain the exact JSON as
`docs/dev/linux-arm64-evidence/stock-sp-report.json` on the evidence branch;
the accepted manifest binds its raw bytes and the release gate validates its
native ARM64, Wayland, physical-host, and candidate-binary identity.

### Linux Sanitizer Validation

On Linux, use this when touching memory-sensitive code, SDL event normalization, display/window geometry, staged source handling, or parser/launcher logic:

```bash
bash tools/validation/validate_pr.sh \
  --skip-python-tests \
  --no-install \
  --build-dir .tmp/validation/linux-sanitizer-builddir \
  --buildtype debugoptimized \
  --jobs 2 \
  --extra-setup-arg=-Duse_pch=false \
  --extra-setup-arg=-Db_sanitize=address,undefined
```

Stage the instrumented client, then run the same lifecycle case from a native Wayland session:

```bash
bash tools/build/meson_setup.sh install \
  -C .tmp/validation/linux-sanitizer-builddir \
  --no-rebuild \
  --skip-subprojects

ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
SDL_VIDEO_DRIVER=wayland \
SDL_VIDEODRIVER=wayland \
LIBGL_ALWAYS_SOFTWARE=1 \
OPENQ4_WAYLAND_DISABLE_LIBDECOR=1 \
python tools/tests/renderer_validation_matrix.py \
  --cases sdl3-wayland-window-lifecycle \
  --tiers auto \
  --timeout 120 \
  --basepath "" \
  --skip-official-pak-validation \
  --output-dir .tmp/renderer-validation/sanitizer
```

This matches the PR sanitizer build and runtime case; CI supplies the native Wayland session with headless Weston. No stock Quake 4 assets are required. The build still requires the normal Linux development dependencies and the sibling `openQ4-game` checkout so the game modules are compiled from staged source input. Separate staging is intentional: release package validation rejects sanitizer-only runtime dependencies, while this lane needs those dependencies to execute the instrumented client.

Linux engine and game translation units disable only UBSan's `vptr` check. The runtime-loaded modules consume polymorphic engine interfaces, while engine code also consumes game interfaces whose key functions and RTTI are owned by those modules; instrumenting either side for `vptr` creates link-time RTTI references that cannot be resolved without changing the executable/module ABI or weakening the modules' `-z defs` contract. AddressSanitizer and all other requested UBSan checks remain enabled, and in-tree BSE retains `vptr` coverage because its polymorphic types resolve within the executable.

### Manual macOS Sanitizer Validation

Use the manually dispatched **macOS Sanitizer** workflow in
`.github/workflows/macos-sanitizer.yml` when changing Objective-C++, SDL/Cocoa
integration, filesystem/package-root discovery, dynamic game-module loading,
or other macOS-sensitive lifetime code. Select `both` bridges and leave the
assetless runtime probe enabled for the normal diagnostic run. This is a
`workflow_dispatch` investigation tool rather than a required release-candidate
lane.

The workflow builds both OpenGL and Metal bridge configurations with ASan+UBSan,
stages the instrumented client, dedicated server, and SP/MP modules, verifies
their arm64 slices and sanitizer references, and runs
`renderer-default-safety-selftest` without stock Quake 4 assets. It uses the
equivalent Meson options:

```bash
bash tools/build/meson_setup.sh setup --wipe builddir . \
  --backend ninja \
  --buildtype=debug \
  --wrap-mode=forcefallback \
  -Dplatform_backend=sdl3 \
  -Dmacos_graphics_bridge=opengl \
  -Dmacos_openal_provider=system \
  -Duse_pch=false \
  -Didlib_asserts=true \
  -Db_sanitize=address,undefined
```

PCH is deliberately disabled so sanitizer compilation does not reuse an
uninstrumented header image. `-Didlib_asserts=true` is what makes idlib's
allocator-ownership and alignment `assert()`s exist at all on Clang:
`src/idlib/Lib.h` keys them off `_DEBUG`, which MSVC supplies through `/MTd`
but no GCC or Clang openQ4 build defines. Keep it off for ordinary debug
builds — `AssertFailed` raises `SIGTRAP` on Linux and `SIGINT` on macOS, so a
firing assert stops the process. Failed and successful runs retain host/toolchain
details, Meson logs, staged-source metadata, binary instrumentation reports,
and runtime output. This workflow improves macOS-only diagnostics, but it does not replace real Apple-hardware gameplay signoff for SP, MP, audio, input,
displays, or release packages.

---

## Building on Windows

> [!NOTE]
> Release packaging targets the static MSVC CRT so end users do not need a separate Visual C++ Redistributable install.

### VS Code Fast Build

Use **Build openQ4 (Meson Optimized)** as the default local/agent build from VS Code. It runs the Meson compile path, lets Ninja skip unaffected code and PK4 targets, then incrementally copies only changed runtime files from `builddir-perf/` into `.install/` for the existing launch configurations.

That directory is configured `debugoptimized`, not `debug`, because the launch
configurations run whatever this task stages into `.install/`. An unoptimized
client is roughly half the frame rate: `game/airdefense1` measured 146.5 Hz
against 315.7 Hz for the same source. `debugoptimized` keeps full debug symbols,
so the `cppvsdbg` launch configurations still work; expect the usual optimized-code
stepping and inlining behaviour. `tools/tests/vscode_fast_build.py` pins the
optimization level so this cannot regress silently.

`builddir/` is left alone for `tools/validation/openq4_validate.py`, which
configures it `debug` on purpose. Keep the two separate: pointing both at one
directory makes every validation run and every VS Code build reconfigure the
other's optimization level and force a full rebuild. Use the Debug Build steps
below when you genuinely want an unoptimized client.

Use **Full Build and Stage openQ4 (Meson Optimized)** when you intentionally need the full configure + compile + `meson install --no-rebuild --skip-subprojects` path. If you add or remove files under `content/baseoq4/pak0/` or `content/baseoq4/pak1/`, run **Configure openQ4 (Meson Optimized)** once so Meson refreshes the pack dependency list; edits to existing content files are picked up by the fast build.

### Debug Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Run directly from builddir
builddir\openQ4-client_<arch>.exe
```

### Optimized Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Stage distributable package into .install/
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

### From a Visual Studio Developer Command Prompt

Once the MSVC environment is initialised you can call `meson` directly:

```batch
meson setup builddir . --backend ninja --buildtype=release
meson compile -C builddir
```

---

## Building on Linux / Experimental macOS

> [!NOTE]
> As of July 13, 2026, Linux and experimental macOS default to the SDL3 backend. `-Dplatform_backend=native` remains available as the fallback POSIX comparison path, but on macOS it is diagnostic-only and not a supported release backend. Native Wayland is the default in a Wayland session, including through `openQ4-steamdeck`; XWayland is selected only when `OPENQ4_FORCE_X11=1` or an SDL X11 video-driver override is explicitly set.

### Debug Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Run directly from builddir
./builddir/openQ4-client_<arch>
```

Use `-Dplatform_backend=native` during setup if you need to compare against the legacy Linux X11/GLX backend or the experimental macOS Cocoa/OpenGL diagnostic backend. macOS native backend results are comparison data only; release packages and release support claims use SDL3.

For experimental macOS Metal bring-up without a native renderer rewrite, configure with:

```bash
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dplatform_backend=sdl3 -Dmacos_graphics_bridge=metal
```

This mode links the Metal/QuartzCore bridge frameworks and applies the SDL render/GPU hint defaults at first SDL video use — including the early startup splash/system-console windows — with a `metal,gpu,software` renderer-driver fallback list; the actually-created SDL renderer driver is logged as signoff evidence, and the active bridge is logged during OpenGL startup. The visible renderer remains openQ4's OpenGL compatibility path so shipped Quake 4 asset behavior stays the guiding constraint. It is a Metal bridge package, not a native Metal renderer.

### Experimental macOS Vulkan Through MoltenVK

`build_renderer_vk` also applies to macOS, so a normal SDL3 macOS configure builds `renderer-vk_<arch>.dylib` alongside the client. Apple ships no Vulkan driver, so the module runs on top of MoltenVK, a Vulkan-on-Metal translation layer that is bundled in the package. It is a translation layer, not a Metal renderer, and it neither replaces nor removes the OpenGL renderer.

Build and packaging facts worth knowing before touching this path:

- OpenGL remains the default and recommended macOS renderer in both package variants. Vulkan is opt-in at run time with `r_renderApi vulkan` plus an engine restart, and `r_renderApi best` still resolves to `gl` on macOS.
- The module and `libMoltenVK.dylib` are staged into `openQ4.app/Contents/Frameworks` inside the existing `OpenGL` and `Metal bridge` packages. They add no third package variant and no new `macos_graphics_bridge` value.
- The darwin target is a `shared_library` with `name_suffix: 'dylib'`, an `@loader_path` install name, hidden symbol visibility, dead stripping, and `-Wl,-exported_symbols_list,tools/build/macos_renderer_module.exp`, which exports only `GetRenderAPI`. The macOS client still links a second copy of the renderer statically, so any further exported symbol would interpose at `dlopen` time.
- `tools/build/prepare_macos_moltenvk.sh` acquires, verifies, and stages the pinned MoltenVK release. The pin is `v1.4.1` because that is the newest release that still runs on macOS 11.0; `v1.4.2` raised its runtime floor to macOS 12.0. Do not advance the pin without executing the full macOS floor change documented in `docs/dev/macos-support-matrix-policy.md`.
- Loose `.install/` development runs probe MoltenVK both beside the executable and under `.install/Frameworks/`; app bundles continue to use `Contents/Frameworks/`. The SDL surface path and Vulkan module use the same ordered candidates so they cannot bind different loader images.
- Both staged binaries are nested code and are signed inside-out with the Developer ID Application identity before the outer app is signed and notarized. The published MoltenVK dylib is ad-hoc signed only, so a credentialed release run must re-sign it.
- Configure with `-Dbuild_renderer_vk=false` to leave the module and translation layer out entirely; `r_renderApi vulkan` then falls back to OpenGL through the normal fail-closed ladder.
- The decision-gate plan is `docs/dev/macos-moltenvk-decision.md`, the staged record is `docs/dev/plans/2026-07-25-macos-moltenvk.md`, and the sourcing/pinning/signing policy is `docs/dev/macos-moltenvk-provider-policy.md`. `python tools/tests/macos_moltenvk_policy.py` pins the parts of this contract that no Windows or Linux build can observe.

macOS Vulkan is experimental inside an experimental platform and has no accepted real-Apple-hardware evidence, so it must not be described as a supported renderer.

### Linux Packager Notes

For downstream Linux packages, treat official openQ4 Linux archives as targeting an Ubuntu 24.04-class userspace unless broader distro coverage has been validated for the release. Meson 1.6.0 or newer is required. The SDL3 dependency floor is SDL3 `>=3.4.4`; the bundled fallback wrap currently tracks SDL3 3.4.10.

Package the default SDL3 backend with OpenGL plus Wayland/EGL support, and keep X11/GLX available where practical for fallback and diagnostics. The launcher or package notes should document both SDL spellings for explicit driver selection because users and SDL documentation may use either form:

- Native Wayland: `SDL_VIDEODRIVER=wayland` or `SDL_VIDEO_DRIVER=wayland`.
- X11/XWayland: `SDL_VIDEODRIVER=x11`, `SDL_VIDEO_DRIVER=x11`, or the project-specific `OPENQ4_FORCE_X11=1`.
- Wayland decoration fallback: `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` when libdecor causes startup or window-decoration issues.
- Wayland decoration preference: `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` when a compositor behaves better with libdecor.
- Wayland window-operation diagnostics: `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1`; use only for troubleshooting because some compositors can block during window animations.

Build from the companion `openQ4-game` checkout through `OPENQ4_GAMELIBS_REPO` or the default sibling path. The staged `openq4_gamelibs_stage_manifest.json` records the exact source-input hashes and git state used by the engine build. Locate the configured target's manifest with `python tools/build/gamelibs_stage_path.py --build-dir builddir --manifest`; keep it in CI artifacts when investigating packaging or reproducibility problems, but do not package the source stage as a runtime payload.

Release packaging publishes detached Linux debug symbols as `openq4-<version>-linux-<arch>-debugsymbols.tar.xz`. Keep that archive paired with the matching runtime package so crash reports from optimized Linux builds can be symbolized.

Official Linux release jobs also turn the already-verified package directory into standalone `openq4-<version>-x86_64.AppImage` and `openq4-<version>-preview-aarch64.AppImage` downloads. `tools/build/package_linux_appimage.py` preserves the complete release-package payload under the AppDir, uses `linuxdeploy` only to add the native dependency closure and package-relative runpaths, keeps build IDs plus `.gnu_debuglink` associations intact, and validates the extracted type-2 image against the same detached symbols. The workflow pins and SHA-256 verifies native `linuxdeploy` and `appimagetool` releases, derives the type-2 runtime from the checksummed appimagetool, and supplies `SOURCE_DATE_EPOCH` rather than downloading a mutable runtime during packaging.

The release smoke runs the final image without requiring FUSE by setting `APPIMAGE_EXTRACT_AND_RUN=1`, then repeats the assetless SDL3 lifecycle under headless Wayland/Weston and X11/Xvfb. This validates the AppRun relocation path, bundled library resolution, `baseoq4` lookup, both supported window systems, and clean teardown. A normal desktop launch can execute the AppImage directly; `APPIMAGE_EXTRACT_AND_RUN=1` is also a useful fallback on hosts where FUSE mounting is unavailable.

Native x64 and ARM64 builds are the build/package authority. For an independent ARM64 build/ABI check from an x64 Ubuntu host, use the checked-in multiarch cross file and package manifest described in `docs/dev/linux-arm64-cross-compilation.md`. Before signing off a Linux ARM64 release, also run stock-asset SP and MP gameplay with working audio/input on real ARM64 hardware under native Wayland; hosted assetless Weston/Xvfb startup is not a substitute.

Before shipping a downstream package, run at least:

```bash
bash tools/validation/validate_push.sh --install
bash tools/validation/validate_pr.sh --runtime --runtime-cases renderer-default-safety-selftest,sdl3-x11-display-diagnostics --runtime-tiers auto --runtime-basepath "" --runtime-skip-official-pak-validation
```

### Optimized Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Stage distributable package into .install/
bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects
```

---

## Output Files

### Build directory (`builddir/`)

| File | Description |
|------|-------------|
| `openQ4-client_<arch>[.exe]` | Main engine executable |
| `openQ4-ded_<arch>[.exe]` | Dedicated server |
| `baseoq4/pak0.pk4` | Compiled openQ4 core runtime content pack |
| `baseoq4/pak1.pk4` | Compiled openQ4 level content pack |
| `baseoq4/mod.json` | openQ4 game-directory manifest |
| `baseoq4/game-sp_<arch>[.dll/.so/.dylib]` | Single-player game module |
| `baseoq4/game-mp_<arch>[.dll/.so/.dylib]` | Multiplayer game module |

- BSE (Basic Set of Effects) is linked directly into `openQ4-client_<arch>`; the dedicated server keeps a disabled/stub path.
- `content/baseoq4/pak0/` and `content/baseoq4/pak1/` are compiled into deterministic `pak0.pk4` and `pak1.pk4` before the engine is built; the generated pack checksums are embedded into the executable so startup can reject missing or modified openQ4 runtime packs.
- On Windows, the wrapper stages `OpenAL32.dll` next to the executables and rejects builds that still depend on external MSVC/UCRT runtime DLLs.

### Install directory (`.install/`)

After running the install step, `.install/` is the staged distributable package root. Windows stages its required runtime DLLs. The Linux client continues to rely on the documented system OpenGL/EGL, OpenAL, Wayland or X11, input, and audio libraries supplied by the target distribution; the Linux dedicated executable deliberately does not directly require those client libraries:

```
.install/
├── openQ4-client_<arch>[.exe]  # Main executable
├── openQ4-client_<arch>.pdb    # (Windows) client diagnostic symbols
├── openQ4-ded_<arch>[.exe]     # Dedicated server
├── openQ4-ded_<arch>.pdb       # (Windows) dedicated-server diagnostic symbols
├── openQ4-steamdeck            # (Linux) Steam Deck launcher
├── OpenAL32.dll                # (Windows) runtime dependency
├── share/applications/         # (Linux) desktop entries
└── baseoq4/
    ├── pak0.pk4                         # Compiled openQ4 core runtime content
    ├── pak1.pk4                         # Compiled openQ4 level content
    ├── mod.json                         # openQ4 game-directory manifest
    ├── game-sp_<arch>[.dll/.so/.dylib]   # Single-player module
    ├── game-sp_<arch>.pdb                # (Windows) SP diagnostic symbols
    ├── game-mp_<arch>[.dll/.so/.dylib]   # Multiplayer module
    └── game-mp_<arch>.pdb                # (Windows) MP diagnostic symbols
```

> [!NOTE]
> Public release packages stay on the `stable` version track while using an optimized diagnostics profile. Windows, Linux, and experimental macOS packages use Meson `buildtype=debugoptimized` with `b_ndebug=true`, so release binaries are optimized and runtime asserts are disabled while diagnostic symbols remain available. Windows packages include matching PDB files. Linux debug info is split into separate `openq4-<version>-linux-<arch>-debugsymbols.tar.xz` assets. Credentialed experimental macOS release packaging builds both `-opengl` and `-metal` variants from the SDL3 backend so the Metal bridge remains additive. MSVC import libraries (`*.lib`) are development-only artifacts and are not required in the package.

Repo-authored runtime overrides live under `content/baseoq4/pak0/` and `content/baseoq4/pak1/`. The build compiles those source-owned roots into `.install/baseoq4/pak0.pk4` and `.install/baseoq4/pak1.pk4`; `mod.json` and platform-specific game modules remain loose beside them.

For macOS release artifacts, `tools/build/package_nightly.py` transforms that
staging layout into a self-contained `openQ4.app`: `mod.json` and the PK4s move
to `Contents/Resources/baseoq4`, while the Mach-O SP/MP modules move flat into
`Contents/Frameworks` so they can be signed as nested code before the outer
app. The packaged loose client and dedicated server discover those sibling app
roots; the large staged payload is not duplicated beside the bundle.

On Linux desktops, you can create a user desktop shortcut for the staged runtime after the install step:

```bash
bash tools/linux/install_desktop_launcher.sh
```

If Quake 4 is not in a location that openQ4 can auto-detect, pass the retail install root that contains `q4base/`:

```bash
bash tools/linux/install_desktop_launcher.sh --basepath "/path/to/Quake 4"
```

---

## Packaging a Distributable

The `meson install` step (via the wrapper) stages all required binaries into `.install/`. This directory can be zipped and distributed as a release archive.

Release archives also generate a packaged offline HTML documentation site under `docs/`. If you run the release packager manually instead of using GitHub Actions, make sure `python -m pip install markdown` is available in the same environment.

The manually dispatched GitHub release workflow publishes architecture-qualified release assets such as `openq4-<version>-windows-x64.zip`, `openq4-<version>-windows-arm64.zip`, `openq4-<version>-linux-x64.tar.xz`, `openq4-<version>-x86_64.AppImage`, and the matching Linux ARM64/aarch64 preview or first-class names. AppImage filenames use the standard `x86_64`/`aarch64` architecture spelling and omit a redundant `linux` segment. The same workflow also publishes experimental macOS Apple Silicon/arm64 OpenGL and Metal bridge packages for macOS 11 or later. When Apple signing and notarization credentials are configured, those experimental macOS packages are `openq4-<version>-macos-arm64-opengl.dmg` and `openq4-<version>-macos-arm64-metal.dmg`; otherwise they are clearly labeled fallback archives named `openq4-<version>-macos-arm64-opengl-unsigned.tar.gz` and `openq4-<version>-macos-arm64-metal-unsigned.tar.gz`. macOS `macos-x64` and `macos-universal2` artifacts are intentionally not published by the current release matrix. A first-class macOS release must use the signed/notarized DMG path and current evidence in `docs/dev/macos-signoff-evidence.md` for both the documented macOS floor and latest public macOS; the workflow input `macos_support_tier=first-class` fails the metadata job when Apple signing/notary secrets are absent. Release workflow packages use `version_track=stable` and Meson `buildtype=debugoptimized` with `b_ndebug=true` on every platform, giving end users optimized binaries while preserving diagnostics. Windows payloads include PDB files and write crash logs plus minidumps under `crashes/` beside the executable after unhandled exceptions. Linux release runs also publish detached debug-symbol archives such as `openq4-<version>-linux-x64-debugsymbols.tar.xz` and `openq4-<version>-linux-arm64-debugsymbols.tar.xz`. Credentialed experimental macOS release payloads are signed/stapled, packed into compressed DMG images, submitted for final DMG notarization/stapling, and verified with `hdiutil` before upload. Credential-free experimental macOS fallback archives are ad-hoc signed only for bundle validity, are not Developer ID signed or notarized, and stay marked `unsigned` in the filename. Windows payloads also get native installer executables such as `openq4-<version>-windows-x64-setup.exe` and `openq4-<version>-windows-arm64-setup.exe`. Each installer is compiled from the already-packaged Windows release directory so its file set matches the archive instead of diverging from it, writes install metadata to the registry for upgrade detection, registers a normal Windows uninstaller entry, and can optionally register `openq4://` browser links.

Manual releases must be dispatched from the `main` branch. The `openq4_game_ref` input selects the companion source revision and defaults to `main`; the metadata job resolves that branch, tag, or full commit SHA once, publishes the immutable 40-character SHA to every matrix job, and records both repository commits in package `VERSION.txt` metadata. Every build checks out those exact commits and rejects a dirty checkout or staging manifest whose `projectGitCommit`, `gameLibsGitCommit`, or clean-state fields disagree. Release creation and reruns are likewise bound to the triggering openQ4 `GITHUB_SHA`: an existing version tag must already resolve to that candidate, and both release/tag targets are verified after publication. The publishing job rejects missing, extra, symlinked, or non-regular downloaded assets, constructs the upload list only from the exact expected filename whitelist, rechecks a first-class Linux ARM64 archive against its accepted SHA-256 after artifact download, and verifies that the final GitHub release exposes exactly the approved asset names.

Linux release validation brackets every binary mutation. The normal staged ELF/package gate runs before debug-symbol separation; immediately after `objcopy`, the workflow revalidates the stripped client, dedicated server, and distinct SP/MP modules for native architecture, PIE/RELRO/NOW/non-executable-stack hardening, the closed `GetGameAPI` module ABI, safe/resolvable `DT_NEEDED` dependencies, retained build IDs, and matching `.gnu_debuglink` filename/CRC data against the detached files. After packaging, the workflow extracts the exact `.tar.xz` upload candidate into a bounded isolated temporary directory and repeats those ELF, dependency, build-ID, and debuglink checks on the extracted bytes before artifact upload.

Linux release builds also force the bundled SDL configuration with both native Wayland and X11/XWayland enabled. After debug-symbol separation, the workflow launches the exact stripped staged client assetlessly under headless Weston with `DISPLAY` removed and SDL forced to Wayland, then under Xvfb with SDL forced to X11. After AppImage construction, the exact upload candidate repeats those two bounded lifecycle probes through AppRun. Both gates require the requested SDL driver, SDL3 renderer/window startup, renderer diagnostics, normal process exit, and SDL3 OpenGL teardown; failed runs publish the Weston, Xvfb, engine, stdout/stderr, and matrix-report diagnostics. This hosted assetless release gate improves package regression coverage but does not change the documented Linux ARM64 preview tier or replace physical-hardware gameplay, audio, input, display, and package signoff.

Linux ARM64 first-class evidence uses a fail-closed two-pass flow because an in-tree record cannot contain its own commit SHA. A pushed branch must already contain the prospective first-class code and user-facing wording; an opt-in, non-publishing run (`generate_linux_arm64_evidence_candidate=true`, with the tier selector left at `preview`) builds only native Linux ARM64 and creates a workflow artifact containing the exact first-class-named archive and a pending structured TOML record. It creates no tag or GitHub release. After physical testing, the accepted TOML is committed on a separate evidence branch/tag and passed back by `linux_arm64_evidence_ref`; the exact tested candidate commit must be fast-forwarded unchanged onto `main`, with the explicit version and openQ4-game SHA frozen. Linux release compilation derives `SOURCE_DATE_EPOCH` from that candidate commit, and the archive writer normalizes tar metadata. Before any first-class ARM64 artifact upload, the native job still recomputes and compares the post-strip staged binaries, packaged copies, and final archive. Toolchain drift or any byte mismatch stops publication and requires a new candidate plus physical signoff; hashes are never refreshed independently of evidence.

Configure `MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64`, `MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE_PASSWORD`, `MACOS_DEVELOPER_ID_APPLICATION_IDENTITY`, `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, and `MACOS_NOTARY_APP_PASSWORD` as repository secrets when a signed/notarized experimental macOS release is required. The workflow imports the Developer ID Application certificate into a temporary keychain, signs macOS payloads with the Hardened Runtime, submits the package payload with `notarytool`, staples the app ticket, and validates the result with `codesign`, `spctl`, and `xcrun stapler validate`. If those secrets are absent, the manual release workflow still publishes experimental macOS packages as `-unsigned.tar.gz` archives that are ad-hoc signed, not notarized, and expected to trigger normal Gatekeeper warnings on first launch. Do not select `macos_support_tier=first-class` unless those secrets are configured and the current macOS signoff evidence is complete.

Official experimental macOS release jobs do not pass a custom entitlements file by default. If an entitlement plist is supplied for a future release experiment, `tools/build/package_nightly.py` validates that it is a plist dictionary and rejects App Sandbox or `get-task-allow` entitlements until the project has a reviewed sandbox/file-access design. App entitlements apply only to the main executable; nested game dylibs in `Contents/Frameworks` are signed first without entitlements before the outer app is signed. The current direct-distribution package relies on Hardened Runtime protections without sandboxing because openQ4 must read user-selected Quake 4 assets, saves, logs, and staged runtime overlays outside an App Sandbox container.

The release workflow also posts an openQ4 release announcement to Discord after the GitHub release is published. Configure the repository secret `DISCORD_RELEASE_WEBHOOK` with the Discord webhook URL before running the workflow; the metadata job checks for it before the package matrix starts. Optional repository variables can tune the announcement without editing workflow files: `DISCORD_RELEASE_EMOJI`, `DISCORD_RELEASE_MENTIONS`, `DISCORD_FEEDBACK_CHANNEL`, and `DISCORD_RELEASE_AVATAR_URL`. If no overrides are set, the broadcaster uses `assets/img/avatar.png`, the `<:quake4:1425986174941397105>` header emote, and the `<@&1425985498693898260> <@&1390287267276525628>` role mentions. Releases created by the manual workflow announce themselves because GitHub releases created with `GITHUB_TOKEN` do not retrigger release workflows; `.github/workflows/discord-release.yml` covers releases published manually through GitHub.

If you want to build that installer manually on Windows after packaging a release directory, install [Inno Setup](https://jrsoftware.org/isinfo.php) and run:

```powershell
python tools/build/build_windows_installer.py --package-dir .tmp\openq4-0.1.010-windows-arm64 --version 0.1.010 --version-tag 0.1.010 --arch arm64 --output-dir .tmp\release-artifacts
```

To include the icon set synchronisation step before building (validated and generated by the wrapper automatically):

```powershell
# Set OPENQ4_SKIP_ICON_SYNC=1 to bypass icon sync in automated/CI workflows
$env:OPENQ4_SKIP_ICON_SYNC = "1"
```

The manually dispatched GitHub release workflow injects `version_base_override` from `tools/build/openq4_release_version.py`. That helper uses the repo version in `meson.build` as the release floor and, once stable `v*` tags exist, automatically chooses between a patch bump (`0.11.0` -> `0.11.1`) and a minor bump (`0.11.0` -> `0.12.0`) based on the scale of changes since the previous release. The workflow also accepts manual `bump_mode` choices for `auto`, `major (x..)`, `minor (.x.)`, and `patch (..x)`, plus `version_override` when a release needs an explicit version.

Builds that do **not** pin `version_base_override` (local builds, CI validation builds) resolve their base version through `tools/build/openq4_version.py`, which raises the `meson.build` floor to the newest published `v*` tag when git history is available. That keeps a development build reporting `openQ4 0.11.0-dev+g<sha>` — the release line it was branched from — instead of a stale floor version that was never published. When there is no git history (an exported tarball), the `meson.build` floor is used as-is. The same helper emits `OPENQ4_VERSION_DATE`, the commit date of `HEAD` (falling back to `SOURCE_DATE_EPOCH`, then the current UTC date), which is what the main game window caption renders.

---

[← Back to README](README.md)
