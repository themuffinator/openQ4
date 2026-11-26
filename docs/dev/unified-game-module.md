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
- **An image the platform keeps.** A platform may keep the library's image
  loaded after the engine unloads it: dyld never unloads an image with
  thread-local variables, and this module has some. The next load then serves
  the other mode from the same image, whose static cvars are already
  registered, so `idModeCVar` registers the mode cvars again itself and their
  defaults and flags still follow the mode, as after a fresh load. The rest of
  the module's state is reset by `Shutdown` and `Init`, as the old libraries'
  was when one was entered twice in a session. This path has not yet run on
  macOS.
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
- A behaviour only one library had stays in that library's mode. The
  every-map A/B of 25 November (see [Evidence](#every-official-map-against-the-pre-merge-build-25-november-2026))
  found eleven places where merged text had reached the other mode, and each is
  now gated:
  - Campaign only, as the single-player library had them: The Awakening's
    content keys `impactEntity` (projectile spawn and map precache),
    `impactForce`, `applyRotation`, `velscale` (`trigger_hurt`),
    `fx_altmuzzleflash[_world]`, `globalLight` in
    `ParseSpawnArgsToRenderLight`, and a persona keeping its entity's
    `classname`. No retail multiplayer map or def uses them; custom ones now
    behave as they did on a multiplayer server.
  - Network play only, as the multiplayer library had them: the 6,144-byte
    script locals stack (the campaign keeps 12,288), the "is a null entity"
    script warning for every name, and freeing a failed `script` command's
    partial definitions.
  - Campaign saves keep the single-player library's `entityDefBits` (negated)
    and its `player_idealweapon` HUD-state spelling, so a save holds the same
    bytes as before. The HUD reads the key either way.

  `tools/tests/unified_game_mode_parity_contract.py` pins these gates.
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
| Mods with their own game code | Must rebuild | The single `game_<arch>` name and the current game API (64 added the mode; 65 since 25 November). A mod that still ships only `game-sp_<arch>`/`game-mp_<arch>` runs on `baseoq4`'s module, and the console warns once, naming the file. Content-only mods are unaffected. |

## Build and packaging

- Meson builds one core library (`openq4_game_core`) and one module target
  (`game_module_target`), linked with one idlib archive (`openq4_game_idlib`).
- `build_games` builds the module. `build_game_sp` and `build_game_mp` are
  deprecated and ignored.
- Packages carry `baseoq4/game_<arch>` (and its `.pdb` or dSYM). The retired
  `game-sp_<arch>`/`game-mp_<arch>` files are removed by staging, packaging and
  the Windows installer, and validators reject a stage that still has them. The
  engine never loads them. An in-game update from 0.15.0 leaves them in place,
  because 0.15.0's own updater plans that update. The next in-game update,
  planned by a build with the unified module, moves them and their debug files
  to the update's backup folder, as it does packs a release no longer lists;
  until then they are harmless and can be deleted by hand.
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

### Every official map against the pre-merge build (25 November 2026)

Windows x64, `debugoptimized`, three runtimes built side by side: the
pre-merge build (`45c7c4fc7`, both old libraries), the merge commit itself
(`22159d559`, so nothing else differs), and the merge commit with the gates
above. Every run used the retail packs alone. The Steam folder's loose
recompiled map files were left out, and The Awakening's content came from the
local expansion install. Clients ran with a hidden window, no sound and no
input. Multiplayer servers ran on the dedicated server.

- **Maps.** All 29 Quake 4 campaign maps, all 13 Awakening maps, and all 49
  multiplayer maps (the 33 retail and 16 community-pack maps), each multiplayer
  map in every game type its map definition lists: 105 server cases.
- **Campaign play.** Each map loads and plays 300 fixed tics, then saves, on
  both builds. Every map passes on both. The warnings and errors each one logs
  are identical, line for line, on all 42 maps.
- **Campaign state, byte for byte.** A live save differs even between two runs
  of one build, because the AI and animation depend on timing. So each map
  was also saved with game time stopped, twice per build. The saves were split at
  their sync markers, and a part counted only if both builds were stable in
  it and still disagreed. Before the gates, that found the `entityDefBits` and
  HUD-key differences on every map. With the gates, all 42 maps write the same
  object table and the same bytes as the pre-merge build except:
  - the server-info dictionary: 43 added multiplayer `si_*` keys,
    `si_useReady` 1 instead of 0, and the build stamp;
  - the save's length field, which grows by exactly that much;
  - fields both builds save without initialising. These are
    `rvClientEntity::worldVelocity`, an animator's joints before its first
    update, and a few AI and vehicle-effect members. In four maps their bytes
    were stable within each build but different between them. The same build
    wrote different bytes once its heap layout moved: `0xCC` fill, text
    fragments, `0xFF` runs. So they are leftover memory, not behaviour, and
    the pre-merge build has them too.
- **Network play, server.** All 105 cases start, run 600 frames and list their
  entities on both builds. Each entity list (102 to 1,168 entities) is identical,
  and so are the warnings. The one exception is a timing-dependent "forcing down
  tourney history", which both builds print on different Tourney runs.
- **Network play, client.** A listen server with its own player joined, on each
  of the 49 maps: identical entity lists and warnings on all 49.
- **Current main.** `main` (`58f23d2ef`) with the gates gives the same result:
  all 42 campaign maps pass and log exactly the pre-merge build's warnings, and
  all 105 server cases pass with the pre-merge build's entity lists.
- **Issues the maps report.** No map logs an error or a fatal on either build.
  The warnings are developer-level reports of retail content: entities and
  rigid bodies in solid, unknown joints, overlapping locations, uncached
  declarations, and The Awakening's redefined declarations. The pre-merge build
  prints the same ones. A separate openQ4 defect, present in both builds,
  shows up in every network session: `guis/menu/mp_escape.rui` names a
  missing timeline node, so the stock escape menu is used.
- **One stall.** Once, under six concurrent clients, the merge build stopped
  advancing frames on core2 after loading. The same map then passed on its
  retry, four times on the current build and four times on the pre-merge
  build. It was not reproduced.

Differences that remain by design: every cvar now registers in both modes, so
a campaign save's server info lists the multiplayer settings, and the
multiplayer-only console commands answer "multiplayer only" in the campaign
instead of being unknown (`pause` is bound by default). Some differences remain
in network play, none of which retail content reaches:
- the campaign's developer cheat commands now exist there;
- `SetCamera` no longer dereferences a missing local player (the old
  multiplayer library crashed on a dedicated server);
- double vision skips a missing material;
- `g_skill` accepts 0 to 4 and is still clamped to 3 at map load.

Not yet run: macOS, an Android emulator or device run, an Awakening campaign
playthrough beyond loading saves, and a human play session.

### Third review (25 November 2026)

A source review of the whole merge against both pre-merge trees, for what the
every-map A/B cannot reach: player input, weapons, vehicles, menus, console
commands and the multiplayer main menu.

- **Cvars and commands, mechanically.** Every static cvar declaration (507 in
  the single-player tree, 582 in the multiplayer tree, 612 merged) and every
  console command registration was compared by name, default and flags. The
  only differences are the documented ones above and the single-player tree's
  dropped bot fork (its `bot_*` cvars, `g_spectatorChat` and `fillBots`).
- **Five hunk-by-hunk reviews.** The campaign side in three parts (player,
  movement and HUD; the game core, menus, scripts and animation; entities,
  weapons, projectiles, vehicles and AI) and the network side in two
  (gameplay and prediction; the core, the main menu with no map loaded, and the
  single-player-only sources now compiled into network play). None found a
  difference in gameplay, saves, the wire format or anything visible. They
  found these minor ones, now gated to the mode whose library had them:
  - network play's hitscan and projectile-prediction debug prints, the
    vehicle branch of the `teleport` cheat, the convoy2b actor switch's
    lookup on every spawn, the campaign's per-entity media dictionary copy at
    map load, the frame-section timers behind `g_frametime`, and the weapon
    wheel clearing a blur the multiplayer library never had;
  - two header comments that described save code wrongly.

  Left as they are, with no effect on stock content: Quake 4's AI and vehicle
  keys on a custom multiplayer map that spawns monsters or vehicles, the ban
  list's log wording and a few no-op calls the campaign now makes each frame.
- **Recovered game regressions.** Five of the 74 tests in
  `game_regression_suite.py` failed after the merge: they pinned code the merge
  moved, compiled extracted functions without the stubs the merged code needs,
  or required the damage numbers to be absent from `src/game/`. The suite had
  already failed before the merge on another test, so the native-test
  comparison counted it as unchanged. All five now follow the merged code; the
  damage-number test pins the network-play gates instead of the old tree's
  absence.
- **Re-entering a kept image.** `idModeCVar::ApplyModuleMode` used to change
  the declarations only for network play and relied on a fresh image for the
  campaign's. It now applies each mode's own and registers again when the
  image was already registered, and `unified_game_mode_parity_contract.py`
  runs it against a stand-in cvar system through a campaign, network play and
  campaign sequence in one image (the old code fails it).
- **No warnings about the mode's own defaults.** At every mode change, both the
  pre-merge and the unified build still reported `pm_usecylinder` and `si_pure`
  as "declared multiple times with different initial value": the cvar system
  kept the previous module's declaration as current. As the module finalizes,
  `idModeCVar::ReleaseModuleMode` now drops that mark from the mode cvars, so
  the next mode's declaration is a first one. A campaign, network play and
  campaign run in one process now logs none of these warnings (the pre-merge
  build logs seven), and its other 158 warnings are the pre-merge build's,
  line for line.
- **Retired modules.** The in-game updater retires leftover `game-sp_*` and
  `game-mp_*` files (`UpdateCoreTest`), and the module search warns once when a
  mod ships only those (a headless dedicated server run with a mod that ships
  only `game-sp_x64.dll` warns once; one without stays quiet).
- **Runtime, against the pre-merge build.** Six campaign maps (airdefense1,
  hub1, walker, convoy1, and The Awakening's m01 trench and m07 race) play 300
  tics and save on both builds, with the same warnings. Eight server cases on
  q4dm1, q4ctf1 and q4dm7 and a listen server on q4dm1 give the same entity
  lists. In saves taken with game time stopped, airdefense1 and hub1 hold the
  same object tables as the pre-merge build, and the bytes match a build of
  `main` without this review's changes exactly, outside run-to-run noise. They
  differ from the pre-merge build in a few places that later work changed on
  purpose: the player's gun keeps `noSelfShadow` (#237), and the renderer
  round's extra joint buffer per animator moves the heap, which changes the
  fields neither build initialises. The dedicated-server smoke and the
  campaign, network play and campaign switch pass.

## Follow-ups

- Keep one loaded library across a mode change instead of reloading it, once no
  module-lifetime state depends on the mode.
- Remove engine-side workarounds for the split: the multiplayer menu cvars the
  engine declares for the single-player module (`Session_DeclareMultiplayerMenuGameCVars`)
  and the comments that explain the other module's settings as bare console cvars.
