# OpenQ4 Release Completion List

Use this file as the source list for release changelog entries.

Process:
1. Add completed work under "Ready For Changelog".
2. When cutting a release, copy relevant entries into that release section in `CHANGELOG.md` (or release notes).
3. Move shipped items into a historical release section here (optional), and keep remaining work in "Carry Forward".

## Ready For Changelog

- [x] Material handling fixes completed; engine startup no longer depends on custom material script overrides in repo `q4base/`.
- [x] Menu rendering issues fixed.
- [x] SDL3 backend integrated as the default platform path (legacy Win32 backend remains transitional).
- [x] Meson + Ninja build system introduced as canonical build path.
- [x] External dependencies moved to Meson subprojects/wraps (`sdl3`, `glew`, `stb_vorbis`, `openal-soft-prebuilt`).
- [x] Vendored GLEW updated to 2.3.1 (both `subprojects/glew` and `src/external/glew`) while preserving local static-link/include-path compatibility tweaks.
- [x] Vendored OpenAL Soft updated to 1.25.1 headers/defs/import libs (both `subprojects/openal-soft-prebuilt` and `src/external/openal-soft`), with Win64 runtime DLL refreshed under `src/external/openal-soft/bin/win64/OpenAL32.dll`.
- [x] Dependency refresh validated with clean Meson reconfigure (`setup --wipe`) and successful rebuild in `builddir`.
- [x] Ogg Vorbis (`.ogg`) playback support integrated (decoded via `stb_vorbis`).
- [x] C++23-targeting baseline enabled on MSVC (`cpp_std=vc++latest`).
- [x] MSVC 2026 toolchain direction documented and implemented as an optional enforceable baseline (`-Denforce_msvc_2026=true`).
- [x] Meson setup wrapper improved to auto-recover missing/stale build directories and avoid VS tool discovery null-crash.
- [x] Windows SDL3 key/input parity improvements: backspace fix, control-char synthesis, locale-aware RightAlt behavior.
- [x] Manual key matrix audit completed and documented for console, GUI edit fields, chat, binds, numpad, and modifiers.
- [x] GUI scaling behavior updated to preserve uniform/aspect-correct rendering on window resize.
- [x] Engine-side console/UI relayout now handles wide and narrow/tall aspect ratios, with live updates on screen size/aspect changes.
- [x] Platform/architecture roadmap documentation added for Windows/Linux/macOS direction with x64 baseline.
- [x] Legacy/redundant build-system artifacts reduced (CMake path retired from active source tree).
- [x] BSE manager/renderer lifecycle contract restored: effect completion now propagates correctly through `ServiceEffect` -> `UpdateEffectDef`, preventing immediate client-effect teardown and enabling proper expiry reporting.
- [x] BSE sound capability path restored at render-world boundary: `EffectDefHasSound` now delegates to decl metadata and manager checks instead of unconditional false.
- [x] BSE segment runtime no longer compiles as pure no-op stubs; core segment timing/attenuation/check/update/count plumbing has been re-enabled as the phase-4 runtime foundation.
- [x] BSE game-lib network/event parity restored: `EVENT_PLAYEFFECT_JOINT` no longer hits an assert placeholder, receive paths now validate decl/filter/rate-limit consistently, and networked effects restore gravity assignment parity.
- [x] BSE save/restore stability improved for active effects by clearing stale `referenceSoundHandle` on restore so emitters are safely reallocated.
- [x] Temporary BSE visibility fallback added (`bse_fallbackSprite`, default `1`): active client effects now emit a sprite-backed render entity until full particle-surface rendering is fully restored.
- [x] BSE segment activation timing fixed: `rvBSE::Service` no longer applies a large negative per-segment spawn offset that delayed segment starts by multiple seconds.
- [x] BSE segment spawn lifecycle restored for `SEG_EMITTER`, `SEG_TRAIL`, and `SEG_SPAWNER`, including attenuation-aware interval/count spawning and loop-time progression tracking.
- [x] BSE trail/child spawn placement improved: non-zero spawn offsets now propagate into particle init positions, and trail segments spawn using interpolated local movement offsets.
- [x] BSE moving-emitter transform parity improved: non-locked particle position/velocity evaluation now converts from spawn-space into current effect-space using stored spawn origin/axis.
- [x] BSE owner wind propagation restored by transforming `renderEffect_t::windVector` into local effect wind each update.
- [x] BSE decal segment path restored from no-op to active world projection (`SEG_DECAL` now builds/projections decal winding via `ProjectDecalOntoWorld` using sampled spawn size/rotation).
- [x] BSE spawn ordering parity improved: runtime particle insertion is no longer unconditional LIFO; linked segments preserve stable chronological/end-time list order while complex segments retain front-insert behavior.
- [x] BSE segment draw-order refinements applied for linked-strip rendering: depth-sort is disabled for linked strip topologies and initial strip index budgeting is handled explicitly.
- [x] BSE runtime sort utility implemented (`rvSegment::Sort`) with stable depth ordering for deterministic per-segment ordering when used.
- [x] BSE network/entity receive rate-limiting corrected by removing duplicate cost consumption (`Filtered` already applies category rate checks; explicit second `CanPlayRateLimited` checks removed).
- [x] BSE particle spawn-parameter sanitization restored (`rvParticleTemplate::FixupParms` no longer compiles as a no-op stub).
- [x] BSE segment attenuation scaling parity restored: emitter interval/count attenuation now uses `bse_scale` interpolation semantics.
- [x] BSE particle cap parity restored: `bse_maxParticles` now drives runtime segment allocation/count clamps instead of compile-time-only `MAX_PARTICLES` paths.
- [x] BSE owner-state parity improved: `mLightningAxis` is now derived from current origin/end-origin direction with stable basis fallback.
- [x] Temporary BSE trace spam removed from hot runtime paths (spawn/expire/remove and segment render skip/state traces), and renderer counter cvar default restored to off (`bse_frameCounters=0`).
- [x] Full clean rebuild validation completed in `builddir/` with both `OpenQ4.exe` and `OpenQ4-ded.exe` linking successfully after BSE parity updates.
- [x] BSE particle gravity parity restored: template `gravity` ranges now contribute spawn acceleration in `rvParticle::FinishSpawn`.
- [x] BSE debris parity advanced: debris particles now spawn client moveable entities (`entityDef`) through `game->SpawnClientMoveable` and stop CPU-side particle rendering.
- [x] BSE shader-parm safety defaults hardened: client effects now initialize RGBA/brightness/timeoffset in `rvClientEffect::Init`, with zeroed-parm fallback in `rvBSE::UpdateFromOwner`.
- [x] BSE template runtime contract completed by implementing missing declared helpers (`rvParticleTemplate::Compare`, `GetTraceModel`, `GetTrailCount`, `ShutdownStatic`).
- [x] BSE unlocked-bounce matrix reprojection parity improved: post-impact velocity/position persistence now uses inverse `current->init` axis mapping with origin-delta compensation, avoiding frame-space double transforms in moving/rotating emitter paths.
- [x] BSE owner-kinematics parity improved: `rvBSE::UpdateFromOwner` now preserves last valid owner velocity across same-timestamp updates instead of zeroing `mCurrentVelocity` on tiny frame deltas.
- [x] BSE oriented-particle matrix parity improved: oriented quad corner ordering/signs now match decompiled transform basis usage, removing mirrored orientation in rotated oriented-particle paths.
- [x] Startup command parsing hardening completed: oversized `+wait` stress launches no longer crash during early `StartupVariable` cvar processing (command-line overflow/drop handling and `idCmdArgs::AppendArg` bounds checks tightened).
- [x] Multiplayer server-side hitscan lag compensation added with configurable rewind controls (`net_mpLagCompensation`, `net_mpLagCompMaxMS`, `net_mpLagCompBiasMS`) and server diagnostics (`net_mpLagCompDebug`).
- [x] Multiplayer non-local prediction mode is now runtime-selectable through `net_mpPredictMode` (`0` legacy limited behavior, `1` enhanced per-frame prediction).
- [x] Scope/zoom handling documentation updated to reflect multiplayer zoom stability and scope yaw alignment behavior.

## Carry Forward

- [ ] Non-English key layout parity still needs completion in SDL3 key translation.
- [ ] Console toggle localization parity for non-English layouts still needs completion.
- [ ] UTF-8 text input should be decoded to characters before queuing `SE_CHAR` events.
- [ ] Linux and macOS bring-up needs full compile/link/runtime validation to reach first-class status.
- [ ] Replace temporary MSVC compatibility flag (`/Zc:strictStrings-`) with full strict-strings codebase compliance.
