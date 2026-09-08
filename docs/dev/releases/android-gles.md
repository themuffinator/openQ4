# Android/GLES — unreleased branch notes

## Highlights

- **Experimental Android and OpenGL ES 3.0 support**, based on **[Emile Belanger (emileb)](https://github.com/emileb)'s original Android port and GLES renderer**. His contribution includes the SigmaTouch bridge, specialized material shaders, stencil shadows, ETC2/EAC texture compression, and mobile memory/loading improvements.
- **Matching single-player and multiplayer modules** use the current official engine and game sources, retaining changes made since 0.12.0.
- **Generated texture and audio caches can live separately from saves**, allowing a mobile host to reclaim cache space without removing player progress.
- **Animated weapon and in-world displays stay visible** with GPU skinning enabled.

## Upgrade notes

- Android is currently a source-build target with a standalone SDLActivity APK host. Build matching engine, renderer, game modules and runtime packs together, then assemble the APK using the included Gradle project. Players must supply original Quake 4 assets. The standalone app supports ordinary SDL/gamepad input; the external SigmaTouch host library is not included.
- The initial debug APK contains the complete current runtime packs and is approximately 660 MB, with additional private storage needed to extract those packs. Physical Android device and SigmaTouch host qualification remains open.
- GLES remains experimental and does not yet provide every advanced effect from the desktop renderers. Desktop OpenGL remains the default; GLES must be selected explicitly on desktop.
- Read the [Android/GLES build guide](../android-build.md) for dependencies, host integration and validation limits. Do not mix modules or runtime packs from the fork's older 0.12.0-based build with this integration.

## Credit

Thank you to **Emile Belanger (emileb)** for the original [Android/SigmaTouch work](https://github.com/emileb/openQ4/tree/android) and [GLES shader variants](https://github.com/emileb/openQ4/tree/gles-shader-variants). The [integration record](../android-gles-integration.md) documents his contribution, exact source revisions and adaptations for official openQ4.
