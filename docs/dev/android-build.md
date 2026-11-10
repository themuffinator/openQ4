# Android, GLES and Sigma Touch

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

The Android platform, GLES renderer and Sigma Touch adapter originate in
[emileb's Android branch](https://github.com/emileb/openQ4/tree/android) and
[GLES shader variants branch](https://github.com/emileb/openQ4/tree/gles-shader-variants).
Credit for this port belongs to **emileb**. openQ4 adapts his implementation to
the current engine, renderer API, multiplayer module and package checks.

## Supported build shape

Android targets `arm64-v8a`, Android API 24 or later, SDL3 and OpenGL ES 3.0.
The native application is `libquake4.so`. The renderer is
`librenderer-gles_arm64.so`; the in-tree `src/game/` and `src/mpgame/` supply
`libgame-sp_arm64.so` and `libgame-mp_arm64.so`. Both modes use `baseoq4/`.
Android has no dedicated-server target. Desktop defaults remain unchanged.

Meson is the engine's build entry point on Android as on desktop. The port's
older standalone engine CMake file is superseded by the current Meson source
lists, generated version/savegame headers and renderer/game export maps.
The engine embeds BSE and each game mode keeps its own idlib archive.

The [standalone Android application](../../mobile/android/README.md) packages
these native outputs using Gradle and SDLActivity. It supports ordinary SDL
gamepad, keyboard, mouse and menu input, plus the experimental first-party
gameplay controls described in the [player guide](../user/android-setup.md).
Sigma Touch remains an optional compatible external host. Device gameplay
remains a separate validation step.

## Toolchain and dependencies

Use the current openQ4 checkout. Canonical SDK game sources and matching engine
interfaces build together; neither archived companion repository is a build
input. Historical Android integration revisions are recorded in the
[integration evidence](android-gles-integration.md).

Use the [Android NDK](https://developer.android.com/ndk/downloads), Python 3,
Meson 1.6 or newer, Ninja and CMake. The cross build uses NDK r30 (30.0.16248370).
The cross-file generator follows Android's
[other build systems guidance](https://developer.android.com/ndk/guides/other_build_systems):
it invokes Clang directly with the target/API instead of Windows `.cmd` wrappers.
All native libraries must use the same NDK, ABI, API and shared C++ runtime.

Obtain SDL3 3.4.18 sources (the repository's pinned subproject, including its
patches) and OpenAL Soft 1.25.2 sources separately; OpenAL Soft 1.25 needs NDK r30's
libc++, as r27d lacked the ranges support it uses. SDL is zlib licensed; OpenAL Soft is LGPL licensed.
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
python tools/build/prepare_android_deps.py --ndk E:/_SOURCE/_CODE/android-ndk-r30 --sdl-source subprojects/SDL3-3.4.18 --openal-source E:/_SOURCE/_CODE/openal-soft-1.25.2 --prefix .tmp/android-build/deps --build-root .tmp/android-build/dependency-builds-1252
python tools/build/android_cross.py --ndk E:/_SOURCE/_CODE/android-ndk-r30 --api 24 --out .tmp/android-build/arm64.ini
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir/android-arm64 --cross-file .tmp/android-build/arm64.ini -Dandroid_deps_root=E:/Repositories/openQ4/.tmp/android-build/deps -Dbuildtype=release
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir/android-arm64 -j 8
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir/android-arm64 --no-rebuild --skip-subprojects
```

Adjust absolute paths for your checkout. On Linux/macOS, run the same Python
helpers and use `bash tools/build/meson_setup.sh setup`, `compile` and `install`
with the corresponding arguments above. The wrapper refreshes the in-tree
source inventory when sources change; use it for subsequent rebuilds too.
Use a separate build directory so desktop settings and staged binaries remain
available. Android builds use the same canonical game and engine headers.

The dependency prefix must contain `include/SDL3`, `include/AL`,
`lib/libSDL3.so`, `lib/libopenal.so` and `lib/libc++_shared.so`.
The helper enables flexible 16 KiB page sizes for dependency builds; the Meson
cross file requests 16 KiB ELF segment alignment for engine and modules.

## Packaging

Manual Releases now calls the reusable Android APK workflow and requires its
signed APK plus matching dependency source archive before publishing. Android
retains an experimental support tier, independent of GitHub's overall
prerelease flag. See the standalone guide for signing and upgrade details.

Installation goes to `.install/android/arm64-v8a/`: native modules in `lib/`,
generated packs and `mod.json` in `baseoq4/`. Copy dependency runtime libraries
from the prefix alongside the native modules in the host's `jniLibs/arm64-v8a/`.
Use a host that exposes an extracted native library directory for runtime module
loading. Keep the application's SDL Java sources and native SDL library from the
same SDL release.

Meson also installs `android-build.json` with the ABI, package variant, actual
native minimum API and SDL version. The standalone Gradle host requires this file, uses its API for
the APK's minimum SDK and rejects a mismatched SDL source tree. APK version names
follow the staged mod metadata; see the standalone guide for custom version-code
numbering when publishing several builds of one engine version.

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

## Diagnostic emulator build

The recovered `x86_64` target uses the same API 24 floor, SDL and GLES3 code,
with `_x64` renderer/game modules. It is a development target under AND-01,
separate from the experimental ARM64 release. Keep its dependency prefix,
native build and stage separate:

```powershell
python tools/build/prepare_android_deps.py --ndk E:/_SOURCE/_CODE/android-ndk-r30 --abi x86_64 --api 24 --sdl-source subprojects/SDL3-3.4.18 --openal-source E:/_SOURCE/_CODE/openal-soft-1.25.2 --prefix .tmp/android-emulator/deps --build-root .tmp/android-emulator/dependency-builds
python tools/build/android_cross.py --ndk E:/_SOURCE/_CODE/android-ndk-r30 --abi x86_64 --api 24 --out .tmp/android-emulator/x64.ini
$dependencyPrefix = (Resolve-Path .tmp/android-emulator/deps).Path
& tools/build/meson_setup.ps1 setup builddir/android-x64 --cross-file .tmp/android-emulator/x64.ini "-Dandroid_deps_root=$dependencyPrefix" --buildtype=release
& tools/build/meson_setup.ps1 compile -C builddir/android-x64 -j 4
& tools/build/meson_setup.ps1 install -C builddir/android-x64 --no-rebuild --skip-subprojects
```

Package that stage with `-Popenq4Abi=x86_64`,
`-Popenq4Stage=<checkout>/.install/android/x86_64` and `assembleDebug`, retaining
the matching SDL source argument and an absolute project-cache directory from
the standalone build instructions. Gradle writes
`builddir/android-apk-emulator/app/outputs/apk/debug/app-debug.apk`.
The application ID is `com.darkmatter.openq4.emulator`, with its own private
saves and external files directory. Its localized launcher name identifies
the emulator build. No release APK or release bundle task is enabled for this
ABI; the phone release workflow and certificate policy remain unchanged.

For verification, pass `--variant emulator` to
`tools/build/verify_android_apk.py`, with the exact staged version, SDK build
tools and the public certificate for the diagnostic signing key. It requires a
debuggable emulator package, API 24, exactly seven matching x86_64 libraries,
16 KiB alignment and the same content/notices checks. The default verifier
continues to require a nondebuggable ARM64 phone release and rejects this APK.
An ABI label alone does not qualify a binary: ELF machine/API and every
packaged library's bytes are checked.

## Physical device qualification

`tools/tests/android_device_qualification.py` is the AND-02 device harness. It is
opt-in and needs a USB-connected phone that is unlocked when each phase starts,
plus a debuggable APK. It sends no OS touch or keyboard input. To start the game
it sends setup the debuggable-only `diagnostic_launch` extra (`sp` or `mp`).
Setup still runs its own installed-data check first, and release builds ignore
the extra. A pending diagnostic launch or a `hold` keeps the screen on, so a
phone's display timeout cannot stall a run between phases. Setup also keeps the
screen on while it checks or extracts data.

```powershell
python tools/tests/android_device_qualification.py --adb "C:/Program Files (x86)/Android/android-sdk/platform-tools/adb.exe" --serial <serial> --phases campaign,sustain,mp --output .tmp/and02/<run> --check-budgets
```

The harness has three phases:

- `campaign` plays stock single-player maps in order: `hangar1`, `hangar2` and
  `mcc_landing` through each level's own end trigger, then fresh loads of
  `airdefense1`, `convoy1` and `process1`. It finishes with a save and reload on
  `process1`.
- `sustain` runs `--sustain-minutes` of view sweeps and sends one
  `RUNNING_CRITICAL` memory-trim request halfway through.
- `mp` runs a Q4DM1 listen server with `--bots` bots.

What each part records:

- **Every stage:** a frame trace, a timed view sweep, `listSounds` audio
  residency and a capture.
- **Each phase:** device, GLES and OpenAL device facts, and storage before and
  after.
- **Every 15 seconds:** thermal status, temperatures, app PSS and battery.

`--cold` moves the generated image cache aside for the campaign and restores
it afterwards. The harness preserves the player's config and log. It removes
every script, save, capture and trace it creates. `report.json` lands under the
given `.tmp` directory. With `--check-budgets` it compares the run against the
per-model limits in
[`android-device-budgets.json`](android-device-budgets.json).

## Meta Quest flavor verification

The standalone headset build is the phone build declared as an immersive
OpenXR app. Its integration, distribution and device qualification are AND-05;
AND-01 provides the checks it must pass. `-Dandroid_headset=true` on an
arm64-v8a configuration stages package variant `quest` and installs the
`openq4-headset` marker that starts the game in VR. The phone Gradle host
refuses that stage until the flavor's Gradle side lands.

`verify_android_apk.py --variant quest` then requires its own application ID,
`com.darkmatter.openq4.quest` (never the phone's, so neither APK replaces the
other), a nondebuggable release signature, the same seven ARM64 libraries (the
OpenXR loader links into the client), the marker asset, and a manifest that
requires head tracking, requests the OpenXR runtime permission, opens the game
with the immersive OpenXR and Quest categories and names its supported
headsets. Phone and emulator APKs must carry none of these and no marker, and
no stage or APK of any variant may carry the headset-free OpenXR test runtime
(`libopenq4-xr-test-runtime.so`). The application ID and API floor are the
flavor's until AND-05's distribution decision.

Create AVDs using the [Android emulator tools](https://developer.android.com/studio/run/emulator-commandline).
Set `ANDROID_USER_HOME` and the AVD data path inside the checkout's `.tmp/`
before invoking SDK/AVD tools; keep SDK downloads and VM disks off the system
drive. Retain the emulator/image version, renderer and capture method with each
run. Emulator results qualify that virtual configuration only; they cannot
establish Adreno/Mali driver, physical touch, audible output or thermal behavior.
The image must expose GLES3 as well as the required Android API level. The
API 24 default x86_64 revision 8 image reports GLES2 with both SwiftShader and
the tested NVIDIA host renderer, so it cannot qualify gameplay at the API floor.
The runtime probe rejects that configuration before installing or copying data;
the APK's API 24 minimum is unchanged.

`tools/tests/android_emulator_smoke.py` is an opt-in native gameplay probe for a
booted, rooted AOSP/userdebug emulator. It verifies the diagnostic APK, copies
and hashes the caller's retail archives, then runs silent Hangar 1 movement,
look/fire/release and save/load checks. `--mode mp` instead runs a Q4DM1
listen server with explicit auto-join and the same movement/release checks;
it does not create a save. `--mode rotate` applies the Interface setup's
Landscape and portrait choice, turns the AVD to portrait mid-match through its
`user_rotation` setting when the engine asks, drives the portrait thumb deck,
turns back and saves/loads. It checks the window and engine captures follow
each turn, the deck arrangement and that neither turn moves the player, and it
restores the AVD's rotation settings and the app's setup preferences.
`--mode lifecycle` writes a touch layout editor record into the setup
preferences (as **Apply layout** does), checks that the engine places, sizes,
fades and hides the controls it names, then sends the app to the background
with the activity manager's Home intent, requires the `Suspended` save, ends
the backgrounded process with `am kill` as Android's memory management would,
relaunches it and loads that save, which must restore the player where the app
was left. `--mode resume --server <IPv4>:<port> --away <seconds>` joins a
dedicated server the emulator can reach, stays in the background for the given
time and returns the same process: a stay shorter than the 40-second time-out
must keep its connection, a longer one must reconnect at once. Captures come from the engine's
`screenshot` command. The probe starts the internal game Activity from the
rooted AVD, so it does not qualify setup navigation or physical input. It refuses
physical-device serials and an already running diagnostic app, preserves existing
settings, and removes its exact scripts/save slot/captures after collecting them:

```powershell
python tools/tests/android_emulator_smoke.py --adb <sdk>/platform-tools/adb.exe --serial emulator-5580 --apk builddir/android-apk-emulator/app/outputs/apk/debug/app-debug.apk --stage .install/android/x86_64 --build-tools <sdk>/build-tools/35.0.0 --certificate .tmp/emulator-debug-cert.pem --assets "E:/SteamLibrary/steamapps/common/Quake 4/q4base" --version <staged-version> --output .tmp/android-emulator/run-1
```

An identical installed APK is retained after full SHA-256 verification;
otherwise installation updates the diagnostic app without clearing its data.
The report includes a copy of the runner and the tested source/package hashes.
Use a fresh evidence directory for each run and inspect its engine captures
before accepting a visual result. Inspect a failed report and any retained test
files before retrying; the probe does not overwrite an existing diagnostic slot.

## Optional Sigma Touch host adapter

The default Android build uses SDL and has no Sigma Touch dependency. For a
legally compatible external host, set `-Dsigmatouch_root=<Clibs_OpenTouch>` and
`-Dsigmatouch_controls_root=<touchcontrols-include-root>`, and supply the target
`touchcontrols` and `saffal` libraries under `android_deps_root/lib`. This enables
`OPENQ4_SIGMATOUCH` and builds emileb's portable API and touch-layout adapters
without injecting the engine PCH into the host sources.

This external host also requires its compatible SDL3 callback extensions
(`SDL_beloko_extra.h` and the corresponding linked exports). Ordinary SDL3
dependency builds support the standalone Activity; they do not supply those
host-specific callbacks. Keep host headers, native SDL and touch libraries from
one compatible integration. The touch editor's labels are resolved after engine
initialization, since the host constructs its controls before starting openQ4.

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

The local push/commit validation profiles require a C++ compiler and JDK 17 or
21 (`javac`/`java` on `PATH`, or `JAVA_HOME`). Their Android host regression runs
the actual Activity against temporary filesystem fixtures without a device.
