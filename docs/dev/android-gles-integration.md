# Android/GLES integration and attribution

This is an adaptation of **[Emile Belanger (emileb)](https://github.com/emileb)'s work** to official openQ4 after 0.12.0. Emile is the original author of the Android port, SigmaTouch integration, GLES renderer and shader variants, ETC2/EAC encoder, and associated mobile loading and memory improvements. The official integration must not be represented as independently originating those features.

## Source revisions

| Input | Pinned revision | Role |
|---|---|---|
| Official openQ4 at branch creation | [`ec4d3ae5`](https://github.com/themuffinator/openQ4/commit/ec4d3ae5) | Initial 0.13.0 main baseline; subsequent-to-0.12 fixes retained |
| Final official-main sync | [`01d0b4624601e3557f57e6d111cc37b8a48d9d69`](https://github.com/themuffinator/openQ4/commit/01d0b4624601e3557f57e6d111cc37b8a48d9d69) | Five commits landed during the integration, including 0.13.1 release verification/full-history safeguards and map-compiler CRC/AAS diagnostics; merged before final validation |
| Matching companion game sources | `f9bf8a692de539b9d149cc7b2c47759dbb1ddd62` on `android-gles` | Shared renderer and player-model headers aligned with this engine; canonical SP/MP sources remain in `openQ4-game` |
| Emile's GLES shader variants | [`bc6f6f32bcb0d46eb6bb03419a942ef4c44e8b99`](https://github.com/emileb/openQ4/commit/bc6f6f32bcb0d46eb6bb03419a942ef4c44e8b99) | Renderer, ES GLSL variants, stencil shadows, ETC2/EAC and mobile optimizations; already adapted to part of official renderer modernization |
| Emile's Android port | [`9aacbf478383f3dea9053a9425cc2788b47af063`](https://github.com/emileb/openQ4/commit/9aacbf478383f3dea9053a9425cc2788b47af063) | Native lifecycle, platform services, logging, touch-host bridge and overlay rendering |

The Android branch shares official ancestor `0299dfefbcf56dc566d7f43b6952f067303b0fd2`; the shader-variants branch shares the later `0751161ef49f525cf84dc86bf54506366e5497c2`. GLES is integrated from the latter, with the Android-specific delta after `3225fb66` applied separately. This avoids reverting modern renderer, networking, replay, chat and packaging work to the Android branch's older snapshot. Original fork authorship is retained in integration history as well as [CONTRIBUTORS.md](../../CONTRIBUTORS.md) and the README.

The integration is prepared locally in both repositories. Publish the companion `android-gles` branch before the engine branch so hosted CI can fetch the pinned game-source commit.

The final upstream sync preserves the tested renderer tree. Its map-compiler and release-tooling changes are included in the final native builds and APK; the companion source pin remains the matching integration revision above.

## Licence and source boundaries

Both pinned fork branches and official main have the identical GPLv3 `LICENSE` blob, `e62ec04cdeece724caeeeeaeb6ae1f6af1bb6b9a`. The port and renderer are incorporated under that existing licence, with upstream notices retained. Khronos GLES headers under `src/external/gles/` retain their MIT/SPDX notices and full permission text in `KHR/khrplatform.h`; their upstream sources are the [OpenGL Registry](https://github.com/KhronosGroup/OpenGL-Registry) and [EGL Registry](https://github.com/KhronosGroup/EGL-Registry). These permissive header terms are compatible with the engine's GPLv3 distribution.

SigmaTouch/OpenTouch host libraries and their application-specific integration remain external dependencies, not supplied or relicensed by this repository. The included standalone SDLActivity host does not include those libraries. The optional bridge is Emile's contributed source; a SigmaTouch host must provide its own correctly licensed touch library. Retail game assets and the companion Quake 4 SDK-derived game modules keep their existing separate terms. See [source provenance](source-provenance.md).

The public [MobileTouchControls licence](https://github.com/emileb/MobileTouchControls/blob/master/License.txt) specifies GPLv2 without an explicit later-version grant and invites contacting Emile about another licence. That text is not a compatible grant for linking it into this GPLv3 engine. The default Android build therefore excludes that library; distributing a combined SigmaTouch build requires an appropriate compatible grant from its author. No external touch-library source or binaries are imported here.

## Adaptations for current official openQ4

- Preserve 0.13.0's actual-context OpenGL capability guards and modern renderer contracts while adding the ES module and its specialized material shaders.
- Preserve bounded in-game menu refreshes while retaining Emile's lazy player-model media loading and diagnostics.
- Keep current pack manifests and mandatory runtime-pack validation. The fork's broad checksum bypass is not adopted.
- Keep Meson as the primary build system and build matching SP and MP modules from the canonical companion repository. The fork's standalone CMake snapshot is replaced by current Meson integration.
- Keep generated image/audio caches separate from saves through `fs_cachepath`, falling back to `fs_savepath` when unspecified.
- Preserve official case-insensitive loose-file recovery. The fork's exact-case negative directory cache is omitted because it incorrectly rejects recoverable directory names and can retain stale misses.
- Restore released audio payloads through a temporary sample, preserving OpenAL buffers that another playing voice may still reference.
- Keep desktop signal handling and synchronize touch-to-engine command delivery across threads.
- Use the actual ES context profile when compiling the current engine's GPU-skinning compute shader; ES 3.0 keeps CPU skinning, while suitable ES 3.1+ contexts can use compute.
- Keep GUI-bearing MD5/MD5R surfaces on CPU skinning so their geometry matches the CPU-positioned GUI quads. This preserves animated weapon and world displays while other eligible model surfaces continue to use GPU skinning.
- Keep GLES depth, material and stencil-shadow draws working with the current multiplayer default that leaves geometry indices in CPU memory; upload those indices through the renderer's existing temporary-buffer fallback.
- Separate staged companion sources by target platform/architecture so concurrent desktop and Android builds cannot overwrite each other's generated source view. Active CI workflows use the matching companion revision above.

Build instructions, host requirements and platform limitations are in [android-build.md](android-build.md). GLES and Android remain experimental; desktop modern-renderer features are not automatically promises of GLES feature parity.

## Validation

Validation on 2026-09-08:

- Windows MSVC client, dedicated server, OpenGL/Vulkan modules and both companion game modules compile and stage through the standard Meson wrapper. All **12 native tests** pass.
- The **175 source checks** in the push validation profile were run. Six initial failures exposed three additional companion-header mismatches, three outdated macOS guard/packaging assertions, the reviewed provenance count change and runtime-tool discovery; the affected checks were corrected and rerun successfully.
- Windowed stock `game/hangar1` SP and `mp/q4dm1` MP OpenGL runs, plus an MP Vulkan run, write the engine's own `screenshot` output and exit cleanly. Their renderer-tier self-test passes all 12 cases. The images were inspected. These are gameplay startup/renderer checks, not comprehensive campaign or multi-client qualification.
- Android NDK r27d, SDL 3.4.10 and OpenAL Soft 1.24.3 cross-build the ARM64 engine, GLES renderer and both game modes. ELF inspection verifies AArch64, `SDL_main` / `GetRenderAPI` / `GetGameAPI`, shared libc++, and 16 KiB load-segment alignment. The engine has no desktop OpenGL dependency.
- The standalone Activity compiles against Android API 35 and the exact SDL 3.4.10 Java sources. Gradle 8.12 / Android Gradle Plugin 8.7.3 assemble a debug APK with API 24 minimum, API 35 target, GLES 3.0 requirement, extracted native libraries and network permission. APK signature and 16 KiB zip alignment verification pass; native libraries, packs and notices match staging.
- The production touch queue/analog-input harness, audio-payload lifetime regression and GLES optional-dispatch regression pass. The SigmaTouch-enabled SDL bridge also passes an NDK syntax check without linking external touch libraries.
- All 20 embedded GLES shader programs/variants compile and link in real Mesa llvmpipe ES 3.2 and forced ES 3.0 contexts under Xvfb. A GPU readback probe verifies blocks from the production ETC2 RGB/RGBA and EAC RG encoder; the production GPU-skinning compute shader also compiles and dispatches a checked four-weight transform.
- The native Linux client, GLES module and matching SP/MP modules compile. A windowed, input-disabled stock `game/hangar1` SP run under isolated Xvfb produces inspected engine screenshots with a lit world, weapon and HUD, both with the normal post-processing chain and with post-processing disabled.
- The final Linux GLES `mp/q4dm1` run joins gameplay with normal server settings, CPU indices and GPU skinning enabled. Its inspected engine screenshot shows the lit world, pickups, weapon and HUD; all 12 renderer-tier cases pass and no GL errors are reported. An earlier nearly black MP capture exposed the missing CPU-index fallback and was rejected before the fix.
- A paired SP check with GPU skinning disabled/enabled exposed an occluded rear weapon display. After the GUI-surface CPU guard, the warmed GPU-enabled capture restores the display while 152 other surface submissions still use GPU skinning. That final screenshot and comparison evidence are retained under `sp-gpu-gui-fixed`, `sp-cpu-warm` and `sp-gpu-warm` within the Linux evidence directory.
- Eleven focused build, CI and packaging checks pass after the final companion-stage and CI-pin updates. The touch-input regression also passes with both Clang and MSVC.
- After the final official-main sync, release-tooling, map-compiler lifecycle and Android build regressions pass. Windows and Android native builds/staging pass again, all 12 Windows native tests pass, and both configured game-source manifests retain the clean pinned companion commit.

Evidence is retained locally under `.tmp/android-build/`, `.tmp/android-apk-verification.json`, `.tmp/android-gles-validation.log`, `.tmp/android-gles-native-tests.log`, `.tmp/android-gles-windows-gui-final-{build,tests,install}.log`, `.tmp/android-gles-windows-main-final-{build,tests,install}.log`, `.tmp/android-gles-linux-evidence/`, `.tmp/android-gles-shader-es30.log`, `.tmp/android-gles-texture-probe.log`, and the `.tmp/android-gles-windows-{sp-gl,mp-gl,mp-vulkan}/` smoke directories. Windows and Android binaries and the APK were rebuilt and verified after both gameplay fixes and the final official-main sync. The portable `tools/debug/render_smoke.py` requires the requested API to remain active, validates that the engine TGA contains varying, nonblack image data, uses an isolated save/cache directory, and captures via the registered engine screenshot command after map load. Visual inspection is still required. An initial smoke using the unsupported CVar spelling `vk` was discarded; the Vulkan evidence explicitly uses `vulkan` and confirms the active API.

No physical Android device, full SigmaTouch host, or Apple hardware was exercised. Source, shader, cross-build and APK checks do not establish touch usability, device lifecycle behavior or mobile visual parity. GLES omits temporal resolve, CRT, some desktop post effects and immediate debug drawing. ES 3.0 devices without border-clamp extensions use texture-edge clamping, which still needs asset-specific device visual checks.

## Other issues discovered

- **Fixed:** the pre-existing PBR material-resource self-test diagnostic had six source-alpha arguments but only five `%d` placeholders, allowing a later integer to be consumed as a `%s` pointer on the failure path. The formatting now matches its arguments.
- **Dependency limitation:** unmodified OpenAL Soft 1.25.2 fails to compile with NDK r27d's libc++ ranges implementation. The tested Android baseline uses unmodified OpenAL Soft 1.24.3; desktop audio providers are unchanged.
- **Existing diagnostics:** stock map starts still report non-precached declarations and MP stock-content/AAS warnings. The compiler also reports existing missing-return paths in `Polynomial.h`, mixed-case `declManager.h` includes, hidden overloads, float/enum comparisons and legacy class-memory operations; these are outside the port's runtime changes.
- **Local Linux environment:** the installed Wayland scanner's DTD predates SDL's `deprecated-since` XML attribute. Protocol generation continues, but the scanner emits a warning.
- **Existing developer configuration:** `.vscode/meson-task.ps1` still selects `builddir-perf`, despite the agent guide naming `builddir` as standard. This integration's primary Windows build uses `builddir` and does not change the user's launch/build configuration.
