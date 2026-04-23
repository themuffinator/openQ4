# OpenQ4 Platform And Architecture Roadmap

This document defines the long-term platform direction for OpenQ4 and how SDL3 + Meson are used to get there.

## Target End State

- First-class support on modern desktop operating systems:
  - Windows
  - Linux
  - macOS
- First-class support for modern 64-bit desktop architecture:
  - x64 (`x86_64`)
  - arm64 (`aarch64`)
- Keep original Quake 4 gameplay/module behavior compatible while modernizing platform and build layers.

## Current Baseline (0.1.010 beta line)

- Primary actively validated build target: Windows x64.
- Hosted release automation now covers Windows x64/arm64, Linux x64/arm64, and macOS arm64 package generation.
- Build system: Meson + Ninja.
- Dependency model: Meson subprojects/wraps.
- Platform backend direction: SDL3-first (legacy Win32 backend is transitional only).
- Language baseline target: C++23 semantics (`vc++latest` on current MSVC Meson front-end).
- Toolchain baseline direction: MSVC 19.46+ (Visual Studio 2026 generation), with compatibility fallback permitted during migration.
- As of March 30, 2026, Linux defaults to the SDL3 backend and keeps `-Dplatform_backend=native` as a fallback path.
- Steam Deck support is delivered through the explicit `OpenQ4-steamdeck` launcher/profile, not hardware auto-detection.
- When both `WAYLAND_DISPLAY` and `DISPLAY` are available, the Steam Deck launcher prefers XWayland by exporting `SDL_VIDEODRIVER=x11` unless the user already set an SDL video driver.
- Windows arm64 currently uses a custom OpenAL Soft package path during bring-up because the in-repo bundled Windows runtime payload is still x64-only.

## Runtime Baselines

- Windows packaged compatibility floor: `Windows 7` or later.
- Windows validation focus: current `Windows 11` releases first, with `Windows 10` retained as a practical compatibility target even though Microsoft's general Windows 10 servicing ended on `October 14, 2025`.
- Windows 7/8/8.1 are no longer hard-blocked by the current x64 binaries, but they are legacy and outside the actively validated support matrix.
- macOS packaged compatibility floor for the arm64 release line: `macOS 11` or later. Meson now pins the deployment target to `11.0` so the binary floor matches the documented floor.
- Linux packaged compatibility floor: release archives are built on pinned `Ubuntu 24.04` runners and should be treated as targeting a comparable modern 64-bit desktop userspace with OpenGL plus X11/GLX or XWayland available.
- Steam Deck support assumes a SteamOS 3.x style environment and the explicit `OpenQ4-steamdeck` launcher.

## SDL3 Direction

- SDL3 is the default backend path and the portability layer for:
  - window lifecycle
  - input event handling
  - context/window interop glue
- New platform-facing work should prefer SDL3 abstractions first.
- Platform-specific code should be isolated under `src/sys/<platform>/` when SDL3 cannot cover a requirement directly.

## Meson Direction

- Meson is the canonical build system going forward.
- External dependencies should be consumed via Meson dependency resolution and subprojects/wraps.
- New build logic should be host-aware and architecture-aware, with x64 as the active compatibility baseline.
- Meson configuration defaults to `cpp_std=vc++latest` (C++23-targeting mode on MSVC).
- Meson currently adds `/Zc:strictStrings-` on MSVC to preserve compatibility with legacy string-literal usage while the codebase is modernized.
- `tools/build/meson_setup.ps1` prefers VS 2026+ (major 18) when present; strict minimum enforcement can be enabled with `-Denforce_msvc_2026=true`.

## Bring-Up Staging

1. Keep Windows x64 stable with SDL3 default backend.
2. Keep Linux on the SDL3 backend by default and validate both x64 and arm64 release paths.
3. Validate Windows arm64 beyond compile/package bring-up, especially runtime audio and in-game coverage.
4. Promote Linux and macOS to first-class once they pass consistent compile/link/runtime validation loops.

## SDL3 Migration Staging (Linux/macOS)

- Linux now uses a real SDL3 runtime path when `-Dplatform_backend=sdl3` is selected, and that is the default Linux configuration as of March 30, 2026.
- macOS still maps `platform_backend=sdl3` to native platform sources until its SDL3 runtime path is implemented.
- This keeps one backend vocabulary across platforms while allowing Linux to validate the shared SDL3 stack directly.

## Definition Of Done For First-Class Platform Support

- Clean configure + build in Meson.
- Engine initializes and reaches map/session startup with stock Quake 4 assets.
- Core input, rendering, audio, and networking paths work without platform-specific content hacks.
- Regressions are tracked in docs and fixed in engine/platform code, not with asset overrides.
