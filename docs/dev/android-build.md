# Android, GLES and Sigma Touch

The Android platform, GLES renderer and Sigma Touch adapter originate in
[emileb's Android branch](https://github.com/emileb/openQ4/tree/android) and
[GLES shader variants branch](https://github.com/emileb/openQ4/tree/gles-shader-variants).
Credit for this port belongs to **emileb**. openQ4 adapts his implementation to
the current engine, renderer API, multiplayer module and package checks.

## Supported build shape

Android targets `arm64-v8a`, Android API 24 or later, SDL3 and OpenGL ES 3.0.
The native application is `libquake4.so`. The renderer is
`librenderer-gles_arm64.so`; the companion `openQ4-game` repository supplies
`libgame-sp_arm64.so` and `libgame-mp_arm64.so`. Both modes use `baseoq4/`.
Android has no dedicated-server target. Desktop defaults remain unchanged.

Meson is the engine's build entry point on Android as on desktop. The port's
older standalone engine CMake file is superseded by the current Meson source
lists, generated version/savegame headers and renderer/game export maps.
The engine embeds BSE and each game mode keeps its own idlib archive.

The [standalone Android application](../../mobile/android/README.md) packages
these native outputs using Gradle and SDLActivity. It supports ordinary SDL
gamepad, keyboard, mouse and menu input; a full touch gameplay overlay uses the
optional compatible Sigma Touch host. Device gameplay remains a separate
validation step.

## Toolchain and dependencies

Use the `android-gles` branch in both openQ4 and its sibling `openQ4-game`
checkout. This integration pins the companion to
`f9bf8a692de539b9d149cc7b2c47759dbb1ddd62`; checking out that commit directly is
also supported. A companion checkout from an older `main` revision carries
incompatible engine-interface headers. Publish the companion branch before
pushing the engine branch so remote CI can fetch the pinned commit.

Use the [Android NDK](https://developer.android.com/ndk/downloads), Python 3,
Meson 1.6 or newer, Ninja and CMake. NDK r27d is the initial cross-build baseline.
The cross-file generator follows Android's
[other build systems guidance](https://developer.android.com/ndk/guides/other_build_systems):
it invokes Clang directly with the target/API instead of Windows `.cmd` wrappers.
All native libraries must use the same NDK, ABI, API and shared C++ runtime.

Obtain SDL3 3.4.10 sources (the repository's pinned subproject) and OpenAL Soft
1.24.3 sources separately. SDL is zlib licensed; OpenAL Soft is LGPL licensed.
These are the existing engine dependencies, built for the target. Preserve their
license notices when distributing the libraries, and satisfy OpenAL Soft's
source/relinking requirements. See [SDL](https://github.com/libsdl-org/SDL) and
[OpenAL Soft](https://github.com/kcat/openal-soft).
The dependency helper also stages the notices for the shared NDK C++ runtime
and OpenAL's bundled fmt implementation.

The helper uses each dependency's own CMake build and installs into one prefix;
it neither replaces the engine's Meson build nor downloads sources implicitly.
From the repository root on Windows, for example:

```powershell
python tools/build/prepare_android_deps.py --ndk E:/_SOURCE/_CODE/android-ndk-r27d --sdl-source subprojects/SDL3-3.4.10 --openal-source E:/_SOURCE/_CODE/openal-soft-1.24.3 --prefix .tmp/android-build/deps --build-root .tmp/android-build/dependency-builds-1243
python tools/build/android_cross.py --ndk E:/_SOURCE/_CODE/android-ndk-r27d --api 24 --out .tmp/android-build/arm64.ini
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir/android-arm64 --cross-file .tmp/android-build/arm64.ini -Dandroid_deps_root=E:/Repositories/openQ4/.tmp/android-build/deps -Dbuildtype=release
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir/android-arm64 -j 8
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir/android-arm64 --no-rebuild --skip-subprojects
```

Adjust absolute paths for your checkout. On Linux/macOS, run the same Python
helpers and invoke `meson setup`, `meson compile` and `meson install` directly.
Use a separate build directory so desktop settings and staged binaries remain
available. Android game-header staging has its own directory under `.tmp/`.

The dependency prefix must contain `include/SDL3`, `include/AL`,
`lib/libSDL3.so`, `lib/libopenal.so` and `lib/libc++_shared.so`.
The helper enables flexible 16 KiB page sizes for dependency builds; the Meson
cross file requests 16 KiB ELF segment alignment for engine and modules.

## Packaging

Installation goes to `.install/android/arm64-v8a/`: native modules in `lib/`,
generated packs and `mod.json` in `baseoq4/`. Copy dependency runtime libraries
from the prefix alongside the native modules in the host's `jniLibs/arm64-v8a/`.
Use a host that exposes an extracted native library directory for runtime module
loading. Keep the application's SDL Java sources and native SDL library from the
same SDL release.

Package **both** generated packs and the matching `mod.json`. The engine checks
their checksums and required version. Do not reuse stale metadata or disable
checks to load a mismatched package. This integration retains the normal full
pack contents; emileb's reduced CMake-only pak1 recipe is not applied because the
current renderer and content can evolve independently.

Players must provide their own installed Quake 4 base PK4 assets. Retail assets
are not included in a build or an APK. The host must provide filesystem paths
that allow the engine to read those assets and write its save/configuration data.
Automated MP launches must explicitly pass `+set ui_autoJoin 1`; interactive
launches use `+set ui_autoJoin 0`.

## Optional Sigma Touch host adapter

The default Android build uses SDL and has no Sigma Touch dependency. For a
legally compatible external host, set `-Dsigmatouch_root=<Clibs_OpenTouch>` and
`-Dsigmatouch_controls_root=<touchcontrols-include-root>`, and supply the target
`touchcontrols` and `saffal` libraries under `android_deps_root/lib`. This enables
`OPENQ4_SIGMATOUCH` and builds emileb's portable API and touch-layout adapters
without injecting the engine PCH into the host sources.

External host dependencies are **not** included or licensed by this repository.
The examined MobileTouchControls license grants GPLv2 without an "or later"
clause, and the examined Clibs_OpenTouch tree has no top-level license grant.
Those versions cannot be assumed compatible with openQ4's GPLv3 licensing.
Distribution of a linked Sigma Touch build requires an appropriate compatible
license grant for those dependencies. The adapter option is not evidence that
such a grant exists, and does not enable or remove host validation checks.

## Desktop GLES development

On Linux with GLES/EGL development libraries, enable
`-Dbuild_renderer_gles=enabled` and launch with `+set r_renderApi gles`.
Windows and macOS may supply an ANGLE SDK through `-Dgles_root=<prefix>` with
link libraries under `lib/` and runtime libraries available to the loader.
Dependencies and renderer modules must have the same architecture.

The original `prepare_macos_angle.sh` helper remains a local diagnostic tool.
It extracts ANGLE from an installed Chrome; those binaries are not a
redistributable SDK. A GLES module using that implicit local staging directory
is never installed. Explicitly supplied ANGLE builds require their own license
notices and deployment; desktop GLES remains opt-in.

For runtime checks use windowed desktop launches and the engine's `screenshot`
command. Enter a map before claiming gameplay validation. Android cross-link
success does not verify the app lifecycle, device driver, touch host or gameplay.
