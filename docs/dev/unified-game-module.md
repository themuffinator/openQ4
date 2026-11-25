# Unified game module

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.15.0 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Contract for CORE-07 in the [roadmap](roadmap.md), 24 November 2026.

openQ4 built its game code twice: `src/game/` into `game-sp_<arch>` for the
campaign and `src/mpgame/` into `game-mp_<arch>` for network play. The two trees
were Quake 4's own split (Raven shipped separate single-player and multiplayer
game libraries), and openQ4 kept them in step by hand: at the merge, 204 files
existed in both trees with differences, about 67,000 differing lines in all, and
most new features were written twice or shared through `../game/` includes from
the multiplayer tree.

One library, `game_<arch>`, built from `src/game/` alone, now serves both. This
document is the contract between the engine and that library, the rules the
merged code follows, and the compatibility guarantees and their evidence.

## Engine contract

- **Modes, not modules.** The logical names `game_sp` and `game_mp` remain, now
  as the two *modes* of the one library. `com_activeGameModule`,
  `com_nextGameModule`, the module selection from `si_gameType`, the campaign
  remount and every reload path keep their meaning and their values, so
  configurations, scripts, launch options and harnesses are unchanged.
- **One binary.** Every mode resolves to `game_<arch>` (`game_x64.dll`,
  `game_arm64.so`, `game_universal2.dylib`, `libgame_arm64.so` on Android). The
  loader logs `Selected game module: logical='game_mp' binary='game_x64' ...`.
- **The mode crosses the API.** Game API 64 appends `gameModuleMode_t mode` to
  `gameImport_t`: `GAME_MODULE_SINGLEPLAYER` for `game_sp`,
  `GAME_MODULE_MULTIPLAYER` for `game_mp`. A load serves one mode until the
  engine unloads it.
- **Changing mode still reloads.** The engine unloads and reloads the library
  whenever the next session needs the other mode, exactly as it swapped the two
  libraries before. The reload gives each mode a fresh module: static state,
  registered cvars and `idGameLocal::Init` run as they did for the separate
  libraries. Keeping one loaded library across a mode change is possible later
  (see [Follow-ups](#follow-ups)) but is not part of this contract.
- **MVD headers.** Format 1.2+ MVDs carry their own game schema version, not the
  API version, so the API bump does not affect them. Legacy 1.0/1.1 MVDs carry
  API 39-44 and stay accepted.

## Game-side contract

- `gameLocal.IsMultiplayerMode()` is the load mode: true for `game_mp`. It is
  fixed for the module's lifetime and valid from `GetGameAPI` on, before any cvar
  registers.
- `gameLocal.isMultiplayer` keeps its Doom 3 meaning: the current map is a
  network game. It is always false in the campaign mode; in the multiplayer mode
  it is true while a map is loaded (server, client or demo) and false in the menus
  before one.
- A behaviour that differed between the two libraries is gated in the smallest
  place that preserves both:
  - code that only runs with a map loaded (entity think, damage, physics,
    weapons, AI, a live player's HUD) tests `gameLocal.isMultiplayer`;
  - code that can run without a map (`Init`, `Shutdown`, map load, precache,
    console commands, menu and GUI handlers, cvar setup) tests
    `gameLocal.IsMultiplayerMode()`.
- Cvars are declared once. The few whose default or flags differed between the
  libraries are `idModeCVar`s (`gamesys/SysCvar.h`): the single-player
  declaration is the constructor's, and `idModeCVar::ApplyModuleMode` swaps in
  the multiplayer default and flags just before `idCVar::RegisterStaticVars`.
  A value the player set is kept as before; only the reset value and flags
  follow the mode. They are `pm_usecylinder` (box in the campaign, cylinder and
  archived in network play), `si_pure` (0 / 1) and `si_gameType`, `si_map` and
  `si_mapCycle` (archived only in network play). Differences with no effect on
  the campaign took the multiplayer declaration (`si_useReady`,
  `net_mpLagCompensation`); `g_fov` and `g_gunX/Y/Z` are archived in both modes,
  since the campaign already inherited the archived values.
- Code the campaign always ran through the multiplayer game object
  (`idMultiplayerGame::Reset`, `Precache`, `Shutdown`) keeps network-play-only
  work behind the mode: the match session, evidence and live-event logs,
  identities, function-key migration and the Arena pause menu run only for
  `game_mp`.
- No new compile-time mode switches. `GAME_MPAPI` and the second idlib flavour
  are gone; a single idlib archive is compiled with the game module's flags.

## Compatibility

| Area | Guarantee | How it holds |
| --- | --- | --- |
| Campaign saves (`.save`, base and Awakening) | Existing saves load; new saves keep the same layout and load in older builds | Every `Save`/`Restore`/`WriteToSaveGame`/`ReadFromSaveGame` body keeps the single-player text; class and event names are unchanged; the script program checksum covers ops and operands only. The saved server-info dictionary now also lists the multiplayer server settings, which the campaign registers too; loading reads the dictionary as data, so neither direction is affected. |
| Network play with older openQ4 builds | Same wire format | Snapshot, event, reliable and unreliable message code keeps the multiplayer text; the protocol version is unchanged. |
| Network demos (`.netdemo`, retail `.ndmo85`) and MVDs | Play as before | Read paths keep the multiplayer text; MVD schema versioning is independent of the API version. |
| Render demos (`.demo`) | Unaffected | Recorded and played by the engine. |
| Configurations | Same cvars, same archived values | Mode-specific defaults are `idModeCVar`s; every cvar now registers in both modes, so the other mode's settings are no longer bare console cvars between sessions. |
| Mods with their own game code | Must rebuild | Game API 64 and the single `game_<arch>` name; content-only mods are unaffected. |

## Build and packaging

- Meson builds one core library (`openq4_game_core`) and one module target
  (`game_module_target`), linked with one idlib archive (`openq4_game_idlib`).
- `build_games` builds the module. `build_game_sp` and `build_game_mp` are
  deprecated and ignored.
- Packages carry `baseoq4/game_<arch>` (and its `.pdb` or dSYM). The retired
  `game-sp_<arch>`/`game-mp_<arch>` files are removed by staging, packaging and
  the Windows installer, and validators reject a stage that still has them. The
  engine never loads them. An in-game update from 0.15.0 leaves them in place
  (its manifest has no retired-file list, and 0.15.0's own updater plans that
  update); they are harmless and can be deleted by hand.
- The legacy game-layer tooling (`tools/build/game_layer.py`,
  `stage_gamelibs.py`) builds a layer's `shared` and `game` sources into the one
  module and refuses the retired `mpgame` tree.

## Method

The merge was mechanical where it could be and reviewed where it had to be.

1. Every file that differs between the trees was diffed (whitespace-insensitive)
   into hunks, and each hunk side was tagged against the Quake 4 1.4.2 SDK as
   stock Raven code or openQ4-authored. 4,774 hunks in 204 files.
2. Hunks resolved without judgment: formatting- and comment-only differences,
   precompiled-header lines, and the multiplayer-only subsystems
   (`MultiplayerGame`, `Game_network.cpp`, `mp/`, `bots/`) where the single-player
   copy was an older fork.
3. Every other hunk got an explicit decision: one side, both, a mode gate, or
   hand-written combined text, under fixed rules (save code keeps the
   single-player text, network code the multiplayer text, signatures take the
   superset, moved code appears once, real behaviour differences are gated).
4. Files only in one tree were kept; the single-player tree's older bot fork and
   its duplicate generated callbacks were dropped; cross-tree includes became
   local ones.

## Evidence

Recorded on 24 November 2026 on Windows x64 (MSVC, `debugoptimized`) against
the pre-merge baseline (`45c7c4fc7`, both old libraries built and staged as a
separate runtime).

- **Build.** The unified core (237 objects) compiled without an error or a new
  warning on the first pass; `game_x64.dll` is 7.4 MB against 5.7 MB + 7.0 MB
  for the two libraries it replaces.
- **Serializer identity (static).** Every function's ordered sequence of
  save-file and message `Write*`/`Read*` calls was compared with both original
  trees. Against the single-player tree, the only differing save functions are
  the two intended layout guards below; against the multiplayer tree, no
  snapshot, event, demo or message function differs. A scan of constants used
  inside those functions found two whose merged values would have changed a
  campaign save, both now guarded:
  - `POWERUP_MAX` grew from 16 to 18 (The Awakening's multiplayer powerups);
    `idInventory::Save`/`Restore` loop to `POWERUP_SAVED_COUNT`, the 16 the
    campaign numbered.
  - `idProjectile::STUCK` is 6 on the wire (after the multiplayer `IMPACTED`)
    and 5 in campaign saves; `Save`/`Restore` translate it.
- **Campaign saves.** On airdefense1, medlabs, hub1 and process1, each build
  wrote a save and loaded the other build's: all eight load into gameplay, and
  both builds print the same warnings for each map. 25 older saves already on
  the test machine (base game and The Awakening, from earlier openQ4 builds)
  load the same way in both builds; the four the old build refuses (two of
  them deliberately damaged) and the two that time out there behave the same.
- **Network play.** A pure dedicated server with two bots, a joined client, its
  client-view MVD recording, playback, seeking, `timeDemo`, export and recovery
  pass (`mp_client_demo_smoke`). Pre-merge and unified builds join each other
  in all four directions (pure and unpure, either side serving), with no
  unknown messages; voice even flows both ways, because the protocol is the
  same (`voice_mixed_version`).
- **Demos.** The Quake 4 1.4.2 network demos (`id_perftest`, `id_serverdemo`)
  and their damaged variants pass all 64 checks (`mp_retail_netdemo_smoke`). An
  MVD recorded by either build plays to its clean end in the other.
- **Mode switches.** One process loaded airdefense1 (`game_sp`), started a
  listen server on q4dm1 (`game_mp`) and loaded hub1 again (`game_sp`), each
  time from `game_x64`, with no errors. The pre-merge build, on the same run,
  printed five "CVar declared multiple times with different values" warnings
  (`g_skill`, `gamename`, `net_mpLagCompensation`, `si_useReady`,
  `ui_handicap`) that the unified module no longer has.
- **Tests.** 315 of 326 native tests pass, including every save/restore,
  registration and HUD fixture generated from the merged sources; the other 11
  fail identically on the unmodified tree. Of the 515 Python checks, the 13 that
  fail also fail on the unmodified tree (most on the 23 November video-capture
  symbols).

### Linux, ARM64 and Android (25 November 2026)

Built from `1328be2da` in WSL Ubuntu 24.04 (GCC 13.3, Meson through
`tools/build/meson_setup.sh`, `debugoptimized`, desktop GL only) and stamped
`0.15.0-dev+g1328be2d`, build 2761. Runs used the Quake 4 1.4.2 retail packs
alone, with no loose files. To keep the build small, the light-grid and
load-screen pack sources were cut to their required files, so `pak1` and `pak2`
hold only q4dm1's light grid and the generic load screen. `pak0` is
byte-identical to the Windows build's. Neither change touches the game module.

- **Linux x64 module.** `game_x64.so` exports only `GetGameAPI` and
  `openQ4_Mem_GetModuleStats`. It has no `STB_GNU_UNIQUE` symbol and no
  `NODELETE` flag, so `dlclose` can unload it.
- **Dedicated server.** `linux_dedicated_server_smoke.py` passes: `game_mp`
  loads from `game_x64.so`, initialises, passes the IPv4 self-test, runs DM and
  shuts down cleanly.
- **Campaign saves.** `savegame_cross_load.py` runs at a fixed tic under Xvfb
  with llvmpipe. Linux writes an airdefense1 save and restores it. It also
  restores a save written by the Windows unified build (`0.15.0-dev+ge9fd6e6a`),
  and that Windows build restores the Linux save. Every restore rebuilds its
  input's object table exactly (1,886 and 1,888 objects, none created by the
  restore), with no unresolved reference. Every save is payload v5,
  `le64-portable1`. Restored from the same Linux save and saved again before
  any game time passed, the Linux and Windows saves are the same length and
  differ in 42 of 8,271,709 state bytes.
- **Network play and demos.** `mp_client_demo_smoke.py` passes all 22 checks.
  A pure dedicated server runs two bots, and a joined client records its own
  view. The checks then cover playback, step, seek and rewind, `timeDemo`,
  export with sound, recovery, and refusing a recording made with other game
  data.
- **Mode switches.** One client process loads airdefense1 (`game_sp`), then a
  listen server on q4dm1 (`game_mp`), then hub1 (`game_sp`), each from
  `game_x64.so`, without an error. The dynamic loader's own log
  (`LD_DEBUG=files`) shows the module initialised three times and finalised,
  with its link map destroyed, at each mode change, so each mode starts from a
  fresh module. The mode-dependent defaults follow the mode: `si_pure` and
  `pm_usecylinder` default to 0, then 1, then 0 again.
- **Linux ARM64 cross build.** The `linux-arm64-cross.yml` configuration
  (Wayland only, `tools/cross/linux-arm64.ini`) builds `openQ4-client_arm64`,
  `openQ4-ded_arm64` and `baseoq4/game_arm64.so`. All three are AArch64 ELF64
  with no X11 or GLX dependency. The module exports only `GetGameAPI` and
  `openQ4_Mem_GetModuleStats`, with RELRO, BIND_NOW and a non-executable stack.
  The push validator accepts the stage. `package_release.py` packages it, and
  both the full and the update archive carry `baseoq4/game_arm64.so` and no
  retired module name. The assetless dedicated smoke passes under qemu user
  mode; that runs on the CPU only and is not sign-off evidence.
- **Android.** The arm64-v8a build (NDK r30, SDL 3.4.18, OpenAL Soft 1.25.2,
  configured as `build_android_release.sh` does) produces `libgame_arm64.so`.
  All six libraries pass `android_elf.py` (API 24, 16 KiB alignment). Gradle
  builds the release APK; signed with a throwaway rehearsal key, it passes
  `verify_android_apk.py`. The GLES renderer did not link on main then: two
  calls reached desktop-only sources (from `0106c7c7b` and `ecb274b19`), so
  that APK was built with them compiled out in a scratch copy. The renderer
  fix, the same day, made the scratch copy unnecessary: `librenderer-gles_arm64.so`
  links in the same NDK r30 configuration, and
  `renderer_gles_symbol_closure.py` now fails a new call from the ES module
  into a source it does not build. Nothing ran on a device.
- **macOS.** Not run. No Apple hardware or VM is reachable from the build
  machine, and the hosted macOS lanes run only on a pushed branch.

A module swap also resets two engine testing cvars set on the command line,
`com_skipLoadingContinue` and `com_loadingContinueAutoAdvance`, to their
defaults. A scripted run that changes mode must set them again before its next
map. The pre-merge Windows build does the same in the same three-phase run, so
this is not new.

Not yet run: macOS, an Android emulator or device run, an Awakening campaign
playthrough beyond loading saves, and a human play session.

## Follow-ups

- Keep one loaded library across a mode change instead of reloading it, once no
  module-lifetime state depends on the mode.
- Remove engine-side workarounds for the split: the multiplayer menu cvars the
  engine declares for the single-player module (`Session_DeclareMultiplayerMenuGameCVars`)
  and the comments that explain the other module's settings as bare console cvars.
