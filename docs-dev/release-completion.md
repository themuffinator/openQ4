# OpenQ4 Release Completion List

Use this file as the source list for release changelog entries.

Process:
1. Add completed work under "Ready For Changelog".
2. When cutting a release with curated notes, save them in `docs-dev/releases/vX.Y.Z.md`; the manual release workflow will use that tracked file instead of the auto-generated history summary.
3. If no tracked release file exists, the workflow falls back to the generated release notes from commit history.
4. Move shipped items into a historical release section here (optional), and keep remaining work in "Carry Forward".

## Ready For Changelog

- [x] Collision-model parity improved for generated/runtime CM data: retail Quake 4 primitive tracking and polygon texture-basis data are now preserved during CM generation/serialization, render-model collision no longer over-merges across distinct source surfaces, and `.proc` CRC validation now matches retail CM load behavior more closely.
- [x] Collision-model load parity improved for stock maps and SP entities: CM parsing now reuses existing model slots instead of exhausting submodel capacity, world/map submodel naming matches retail expectations more closely, retail-style SP clipmodel fallback no longer probes entity names, and stock `game/medlabs` map init no longer emits `contains different model`/`no free slots` CM failures.
- [x] Collision-model lifetime parity now more closely matches retail Quake 4: map teardown drops CM reference counts instead of clearing the entire manager, purges free model geometry in place for slot reuse, and SP/MP map shutdown now purges unreferenced CM data before the next level load.
- [x] MCC med-bed intro overlay now keeps its retail 128x128 ambient data/static layers tiled at native scale across the expanded GUI canvas, and edge-hugging detail widgets anchor to the appropriate wide/tall screen sides.
- [x] Main menu placeholder art rotation now uses a randomized montage of eligible loadscreen levelshots, with proper wide/tall expansion-tile composition and slow zoom transitions per shot.
- [x] Startup/loadscreen placeholder now hands off to the main menu automatically after 3 seconds, main-menu entry transitions use a short black fade-in stretched to native screen extents, and startup logo videos can be skipped with default-on `com_skipLogoVideos 1`.
- [x] Added a depth-aware `r_lensFlare` graphics option with quality levels for lightweight light coronas and high-quality lens ghost/streak overlays.
- [x] Material handling fixes completed; engine startup no longer depends on custom material script overrides in repo `q4base/`.
- [x] Retail lighting parity restored for distance-cull portal fades, Raven special-effect pass ordering, ARB2 specular scaling, and `noSelfShadow` stencil-shadow routing.
- [x] Added a live renderer light-report tool (`r_showViewLights`) that prints detailed per-light diagnostics for lights affecting the current view origin/player position.
- [x] Added a persistent visual overlay for `r_showViewLights` (`r_showViewLightsVisuals`) that draws each last-reported light's origin marker, color, and radius volume in-world until the next report refresh.
- [x] Retail `deform rectsprite` material support restored so shipped Quake 4 multiplayer flag-display shaders follow the renderer's rectangular autosprite path.
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
- [x] Console mouse handling now uses native console-space cursor routing/drawing bounded to the live console rect, and adds archived `con_height` control for the console open height (`0.1` to `1.0`, default `0.5`).
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
- [x] High-refresh view interpolation now includes tunable local first-person biasing (`pm_presentViewBias`) so the camera, FOV, and view weapon spend less time a full tic behind at high presentation rates without changing authoritative gameplay timing.
- [x] High-refresh local first-person biasing was retuned so the default interpolation path no longer surges to the newest snapshot and stalls mid-tic; `pm_presentViewBias` now remains an opt-in bias that eases forward more gently when needed.
- [x] High-refresh first-person weapon FX now stay aligned with the interpolated local/spectated camera on the fire frame: view-weapon muzzle flashes refresh in presentation space immediately, and hitscan tracer/path origins sample the presentation-space muzzle instead of the last authoritative pose.
- [x] High-refresh multiplayer carrier/powerup halo lights now follow interpolated player presentation on repeated-state frames instead of stepping only at the 60 Hz simulation tick, keeping CTF/powerup light halos aligned with moving carriers.
- [x] High-refresh vehicle crash/scrape effects now interpolate persistent world-space crash FX between collision snapshots on repeated-state frames, keeping scrape contact visuals smoother when vehicles slide along world geometry.
- [x] High-refresh projectile fly trails and player powerup effects now keep their presentation-time attenuation/origin parameters in sync with interpolated movement on repeated-state frames instead of stepping only when the 60 Hz gameplay tick rewrites those effects.
- [x] Multiplayer joiners now reconstruct terminal non-predicted projectile snapshots into visible detonate/impact FX instead of silently hiding those projectiles when they arrive already exploded, restoring host-fired projectile hit visuals on clients.
- [x] Multiplayer clients now receive authoritative hitscan impact FX through a dedicated remote-impact packet while the legacy hitscan trace message continues to drive path/tracer replay; that replay now bypasses local effect-rate throttling, reaches the firing remote client as well as other viewers, preserves the authored bullet/shotgun impact color, and still reapplies tint for railgun-class impacts that explicitly opt into hitscan coloring.
- [x] High-refresh listen-server view-weapon movement offsets now collapse duplicate same-tic acceleration samples during prediction reruns, preventing forward/side movement input changes from stacking extra shove into the local first-person gun after the earlier view-bias and turn-history fixes.
- [x] High-refresh listen-server view-weapon presentation no longer feeds render-only interpolated weapon poses back into shared weapon state, preventing the local first-person gun from regaining the forward lunge while repeated-state draw frames refresh muzzle-flash / flashlight / GUI-light alignment.
- [x] Multiplayer draw now reuses the same render-prep path as single-player, so the local or spectated first-person view recalculates its render view and refreshes presentation-space player/world/view-weapon transforms immediately before `RenderPlayerView()` instead of drawing the weapon from a stale simulation pose during listen-server movement.
- [x] High-refresh first-person muzzle flashes and other bound view-weapon firing FX now refresh from the corrected draw-time presentation pose on both simulation and repeated-state frames, so they no longer stick at the pre-correction gun position on the fire frame after the listen-server lunge fix.
- [x] High-refresh lightning-gun follow-up was narrowed to the local first-person beam only: `trailEffectView` now refreshes from the corrected presentation-space muzzle/chest joint and a presentation-time local beam trace on both simulation and repeated-state render frames, while the authoritative world/chain-lightning effects remain on the normal gameplay update path.
- [x] High-refresh bespoke visual-owner follow-up now refreshes gib skeleton side-models and security-camera model presentation on repeated-state frames, keeping those visuals aligned with interpolated transforms instead of stepping only at the 60 Hz simulation cadence.
- [x] High-refresh alternative camera/view-source follow-up now refreshes remote entity render views, security-camera feeds, portal-sky/playback cameras, and steam-pipe side models from presentation-space or interpolated repeated-state data instead of leaving those views and side visuals pinned to the last 60 Hz simulation sample.
- [x] High-refresh bespoke-visual follow-up now interpolates SP/MP `idMultiModelAF` side-model render defs on repeated frames and aligns the MP jump-pad activation FX origin with the SP centered-physics path, closing another pair of effect/render-owner presentation gaps that still sat outside the base entity refresh hooks.
- [x] High-refresh brittle-fracture callback models now rebuild from interpolated shard transforms on repeated-state frames, keeping dropped shard geometry smooth through shatter/fall/settle motion instead of stepping only on the 60 Hz simulation tick.
- [x] High-refresh weapon render lights now stay aligned with interpolated weapon/player presentation on repeated-state frames, keeping active muzzle-flash, world muzzle-flash, flashlight, and weapon GUI light defs from stepping at the 60 Hz simulation tick.
- [x] Ragdoll activation quality improved without changing the fixed 60 Hz simulation cadence: startup now keeps owner/world motion, handles initial penetrations more cleanly, and preserves slightly richer contact support for grounded corpses.
- [x] Rigid-body physics timing-safe quality pass applied without changing the fixed simulation cadence: angular velocity handoff now respects world inertia, water is handled as drag instead of a one-time collision-state hack, rigid-body contacts keep richer deduplicated support points, and impacts now preserve time-of-impact momentum while consuming a small bounded amount of leftover fixed-step time.
- [x] Script compiler x64 pointer-temp parity fix ported from OpenD3: right-associative indirect-expression retagging now guards 4-byte object-ref temp vs 8-byte pointer temp storage mismatch by allocating pointer-sized result defs when needed, preventing trigger/door script chain corruption.
- [x] Retail AAS placeholder parity restored: stock dummy `.aas` files now load/discard like retail instead of warning-spamming and failing stock map init, and AAS tactical data is cleared correctly between loads.
- [x] Bloom stability and quality improved: live `r_bloom`/`r_bloom*` changes now use the offscreen scene-target path immediately, scratch render targets rebuild their FBO attachments safely after runtime reallocations, map handoffs no longer rely on the fragile back-buffer bloom path, and bright-pass extraction now keeps only highlight energy so broad lights stop producing solid white bloom discs.
- [x] Manual release packaging builds are healthy again: the engine restored the shared `idCommon` timing/error/tool accessors expected by the SP/MP game libs, fixing the cross-platform release-build break that stopped the `v0.1.011` workflow before artifacts were produced.
- [x] High-refresh mover riding is now stable on lifts such as `game/storage2`: the player keeps descending mover carry, ignores tiny mover-seam pseudo-landings/step jolts, and interpolates first-person view state relative to the mover so the camera stays smooth above 60 FPS instead of vibrating against platform motion.
- [x] High-refresh projectile and weapon-hit impact regressions were corrected by keeping client-predicted projectile collision/explode work on authoritative game ticks only while repeated-state redraws use the dedicated projectile interpolation path, preventing lingering rocket models and impact/scorch FX from replaying every render frame after a hit.
- [x] Renderer effect lifetime replay regression fixed: expired one-shot BSE impact effects now stay finished until their client-effect owner frees the handle, preventing bullet marks, scorch decals, and hit sounds from being recreated every frame after a hit.
- [x] Phase 4 repeated-state rendering overhead reduced: SP/MP scene refresh no longer scans all spawned entities every redraw for projectile interpolation, and active entities now skip repeated-state transform/non-model refresh work unless they actually have presentation deltas or interpolation-sensitive auxiliary visuals, recovering a major `agame/airdefense1` FPS regression introduced during the high-refresh work.
- [x] High-refresh BSE overhead reduced again: repeated-state client effects now keep renderer owner-time on authoritative game ticks instead of wall-clock presentation time, avoiding render-rate ambient effect spawning on maps like `agame/airdefense1`, and renderer effect servicing now runs once per rendered frame instead of repeating per view.
- [x] Repeated-state client-entity work is now pruned more aggressively in SP/MP: static client models and client effects no longer rerun redraw-time presentation work by default, while bound client entities only stay on the high-refresh path when their owner actually has an interpolated transform delta and client moveables stay there only while their own transform/scale interpolation is active.
- [x] Collision-model retail parity improved again: `.proc` loading now matches Quake 4's looser version-token behavior, CM precache requests stay on the `.cm`-only path instead of generating render-model collision data, and extension-mismatched authored collision caches (for example `.lwo`/`.ase` names requested through runtime render aliases) now reuse the existing loaded CM without renaming or duplicating cache entries.
- [x] Level-load filesystem robustness improved: empty-path `ReadFile` probes now resolve as normal missing-file checks instead of raising a fatal dialog, which unblocks stock map startup paths such as `game/airdefense1` and keeps the runtime log alive for any follow-up diagnostics.

## Carry Forward

- [ ] Non-English key layout parity still needs completion in SDL3 key translation.
- [ ] Console toggle localization parity for non-English layouts still needs completion.
- [ ] UTF-8 text input should be decoded to characters before queuing `SE_CHAR` events.
- [ ] Linux and macOS bring-up needs full compile/link/runtime validation to reach first-class status.
- [ ] Replace temporary MSVC compatibility flag (`/Zc:strictStrings-`) with full strict-strings codebase compliance.
