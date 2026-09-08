# Standalone Android application

This SDL3 Activity hosts Emile Belanger's Android/GLES port without SigmaTouch.
It supports the engine's SDL gamepad, mouse, keyboard and menu touch input; the
configurable SigmaTouch overlay remains an optional, separately licensed host.

Build the native Android targets and run Meson install first, following
[the Android build guide](../../docs/dev/android-build.md). The staging directory
must contain `lib/libquake4.so`, `lib/libSDL3.so`, the renderer/game modules and
their shared dependencies, plus `baseoq4/pak0.pk4` and the remaining staged packs.

Use Java 17 or 21, Gradle 8.12, and Android SDK platform 35. Supply the same SDL3
source tree used for the native library (currently SDL 3.4.10):

```sh
gradle -p mobile/android \
  --project-cache-dir=/absolute/path/to/openQ4/.tmp/android-gradle-project \
  -Psdl3SourceRoot=/path/to/SDL3-3.4.10 \
  -Popenq4Stage=/path/to/openQ4/.install/android/arm64-v8a \
  assembleDebug
```

Set `ANDROID_HOME` to an installed Android SDK, or set `sdk.dir` in the ignored
`mobile/android/local.properties`. The APK is written under
`builddir/android-apk/app/outputs/apk/debug/`. Native compilation stays in Meson;
Gradle only packages staged libraries and openQ4-owned overlays. Android currently targets `arm64-v8a` and API 24 or later.

Meson installation writes `android-build.json` beside `lib/`; the host requires
that metadata and uses its native API as the APK's minimum Android version.
The supplied SDL Java source version must match the staged native SDL library.
Do not mix Java classes and native libraries from different SDL releases.

The APK version name comes from the staged `baseoq4/mod.json`, and its default
version code is `major * 1000000 + minor * 1000 + patch` (for example, `0.13.1`
becomes `13001`). Distributors publishing multiple APKs with the same engine
version, including prereleases, can provide an increasing
`-Popenq4VersionCode=<integer>`; preserve increasing codes across later releases.

Use an absolute `--project-cache-dir` path: Gradle resolves a relative path
against `mobile/android`, which can otherwise place generated cache files in
the source tree. Compiler and APK intermediates remain under `builddir/`.

The debug APK is approximately 660 MB because it retains the full official
openQ4 overlay packs. First launch also copies those packs into private storage;
allow room for both the APK and extracted content, plus the retail installation
and saves.

Install the APK, then copy the **contents of your licensed retail `q4base`** to
`/sdcard/Android/data/com.darkmatter.openq4/files/q4base/`. The engine's original
PK4 verification remains enabled. No retail game data is included in the APK.

```sh
adb install builddir/android-apk/app/outputs/apk/debug/app-debug.apk
adb shell mkdir -p /sdcard/Android/data/com.darkmatter.openq4/files/q4base
adb push /path/to/Quake4/q4base/. /sdcard/Android/data/com.darkmatter.openq4/files/q4base/
adb shell am start -n com.darkmatter.openq4/.OpenQ4Activity
```

The default launch opens single-player. To start with multiplayer menus, close
the existing app and launch with `--ez multiplayer true`; `ui_autoJoin` is set
explicitly to `0`. The normal gamepad and keyboard bindings work in gameplay.
The standalone app does not provide a full touch gameplay overlay.

Saves/configuration/logs live in private `files/saves/baseoq4/`, and generated
texture caches live in the Android cache directory. With a debug APK, retrieve
the engine log using:

```sh
adb shell run-as com.darkmatter.openq4 cat files/saves/baseoq4/logs/openq4.log
```

Each packaged overlay revision extracts to its own private directory before
engine startup; incomplete extraction is retried. Old generated revisions are
removed only after the current package is complete, so updates do not retain
another full copy indefinitely. Saves are kept separately. App data removal also removes
these copies and local saves. The native libraries must be extracted by the
package manager. Gradle's `useLegacyPackaging` setting writes the corresponding
`extractNativeLibs` attribute into the packaged manifest.

The custom Activity and build files are openQ4 GPL-3.0-or-later sources. SDL's
Android Java classes are referenced directly from the supplied source tree;
they retain [SDL's zlib license](https://github.com/libsdl-org/SDL/blob/release-3.4.10/LICENSE.txt).
The SDL Activity contract and Gradle versions follow
[SDL 3.4.10's Android project](https://github.com/libsdl-org/SDL/tree/release-3.4.10/android-project).
No SDL template source is copied into this repository.

## Validation

The standalone debug APK was assembled with Gradle 8.12, Android plugin 8.7.3,
SDK/build-tools 35 and the matching SDL 3.4.10 Java/native sources. Its APK v2
signature, ZIP alignment and packaged manifest were verified. All seven native
libraries, both overlay packs, mod metadata, contributor credits and dependency
notices match the Meson staging files. Device installation and gameplay have
not been tested as part of this integration.

The host's extraction/retry and upgrade cleanup are exercised by
`python tools/tests/android_host_storage.py` with a JDK. The test compiles the
actual Activity with minimal Android API substitutes and runs against temporary
filesystem fixtures; it does not require or control an Android device.
