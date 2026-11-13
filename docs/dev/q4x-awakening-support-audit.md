# Quake 4: The Awakening (`q4xbase`) — openQ4 support audit

> **Source consolidation, 1 October 2026.** Current game sources and the
> independent Awakening SP extensions live in openQ4. The expansion uses the
> shared `baseoq4` SP module and an isolated campaign content mount. Historical
> layer/module findings below remain useful content evidence; expansion MP is
> excluded. See [current architecture](archive/2026-10-18/plans/q4x-awakening.md) and
> [campaign installation](../user/campaigns.md).

Audit of the unreleased Raven/Ritual Quake 4 expansion drop at
`E:\Games\Quake_4_Alpha-main\q4xbase` against the current openQ4 engine
(`openQ4/src`) and canonical game code (`src/game`, `src/mpgame`).

The question answered here is narrow and practical: **if a user launched openQ4 with
`fs_game q4xbase`, what breaks, and what code change fixes each break?**

> [!NOTE]
> **Status, 27 September 2026.** Implementation has started; the
> [support plan](archive/2026-10-18/plans/q4x-awakening.md) tracks it. All 13 campaign maps now load.
> Tier 0 is resolved: the ten events are real, the build writes `q4xbase/mod.json`,
> `baseoq4` is searched under any mod, and the expansion's classes live in a separate
> game-library layer (`openQ4-game-awakening`) linked with the unchanged openQ4-game
> objects. Running the code corrected four findings below:
> - `iff`, `spawn_iff`, `bindOrientied`, `ignoreAAS` and `ai_valkaryneShots` have no
>   reader in the expansion's own DLL either; they are dead keys, not gaps. `velScale`
>   and `canTurn` were first filed here too, but the October gameplay audit found their
>   readers (the [plan's Phase 5](archive/2026-10-18/plans/q4x-awakening.md)).
> - The turret keys (`dynamicAccuracy*`, `accuracyLerpTime`, `delayedTracking`,
>   `lockDelay`, `scanAnim`) belong to the expansion's modified `rvMonsterTurret`.
> - The "content pass" on the shadowing decl files is unnecessary: the engine now reads
>   the shadowed retail files as well (decl layering).
> - The freeze gun's status effect lives on `idAI`, with no skin swap: a freeze factor
>   that slows animation, overlays, and a frozen death that shatters.

## Scope

| | |
|---|---|
| Singleplayer | 13 maps (`m01_stranarus_trench1/2` … `m09_valkaryne`), 24,766 entity blocks |
| Multiplayer | `q4xctf1`–`q4xctf6`, `q4xtourney1` |
| Excluded | `q4xdm*` — re-exports of retail DM maps (their `mapDef`s point at retail `q4dm` loadscreens and thumbnails) |
| Content | 116 `.def`, 50 `.mtr`, 193 `.fx`, 60 `.gui`, 31 `.sndshd`, 2,915 sound files, 979 model/anim files, 7 `.af` |
| Reference binary | `q4xbase/gamex86.dll` — the expansion's own game DLL, RTTI- and string-diffed against retail's |

Findings are graded by what actually happens at runtime: **blocker** = the game or map
does not load; **major** = a level or headline feature is broken or unplayable;
**minor** = degraded; **content** = no code change helps.

---

## Bottom line

The expansion is much closer to running than its size suggests. The engine needs
comparatively little: every file format, every material/GUI/BSE/sound-shader keyword,
and every one of the 389 retail script events already work. **Essentially all of the
work is new game-DLL code** — a roster of ~24 C++ classes plus the systems around them.

Three things must land before *anything* boots, and one of them takes down retail maps
too. After that, the work is dominated by five boss AI classes, the speeder-bike
vehicle, the cockpit-cannon subsystem, and four new weapons.

---

## Tier 0 — nothing runs until these are fixed

### 1. Ten unregistered script events abort startup (game code)

`q4xbase/scripts/events.script` is retail's file plus exactly 10 new `scriptEvent`
declarations. `idCompiler::ParseEventDef` raises a fatal error on the first unknown
event name, and `idProgram::CompileText` turns that into `gameLocal.Error`. With
`fs_game q4xbase` the game dies while compiling `scripts/main.script` at startup, so
**no map loads at all — expansion or retail, SP or MP.**

```
resetTalkCount                      ()      -> void    idActor / idAI
setSpeederBikeSpeed                 (f,f)   -> void    riVehicleSpeederBike
setSpeederBikeMaxGravityDistance    (f,f)   -> void    riVehicleSpeederBike
setSpeederBikeBoostEnabled          (f)     -> void    riVehicleSpeederBike
docked                              ()      -> void    riMonsterValkaryne
undock                              ()      -> void    riMonsterValkaryne
requestProcessing                   ()      -> void    riMonsterPainLord
atProcessingStation                 ()      -> void    riMonsterPainLord
controlPointController              ()      -> float   idThread (sys.*)
controlPercentToWin                 ()      -> float   idThread (sys.*)
```

Registering all ten as `idEventDef`s — **even as stubs** — is enough to boot, because the
interpreter degrades an unimplemented event dispatch to a developer warning and a zero
return. Three of them (`controlPointController`, `controlPercentToWin`,
`setSpeederBikeMaxGravityDistance`) are never called by shipped content but still must be
registered; omitting them leaves the game unbootable exactly as if none were added.

This is the cheapest high-value change in the entire audit.

### 2. `fs_game q4xbase` is rejected — no `mod.json` manifest

openQ4 requires a `mod.json` manifest for any `fs_game`; `FS_ParseModManifest` rejects
the mod and `fs_game` is silently reset to `""`. The expansion ships none.

Content-side fix (ship `q4xbase/mod.json`), or relax the requirement engine-side. Note
`requiredopenQ4Version` must parse as `major.minor.patch` and be `<= OPENQ4_VERSION_BASE`.

### 3. `q4x.bat` drops openQ4's own runtime content

`start quake4 +set fs_game q4xbase` never adds `baseoq4` to the search path, so openQ4's
own `pak0`/`pak1` (glprogs, fonts, strings, GUIs) fall out. Launch must be:

```bash
quake4 +set fs_game q4xbase +set fs_game_base baseoq4
```

Alternatively, make `idFileSystemLocal::Startup` always include `baseoq4`
(`framework/FileSystem.cpp`, ordering at ~5239-5253 already yields the correct
`q4xbase > baseoq4 > q4base` precedence).

### 4. The expansion's `gamex86.dll` can never be loaded

openQ4 only accepts `game-sp_<arch>` / `game-mp_<arch>` (`framework/Common.cpp:5376-5383`).
There is no ABI hazard, but there is also no shortcut: **100% of the expansion's game
code must be reimplemented** in `openQ4-game`. Decide the packaging model early, since it
shapes every item below — fold the q4x classes into `src/game`/`src/mpgame` behind runtime
spawnclass registration, or add a third meson target.

---

## Tier 1 — missing C++ classes

Diffing the RTTI tables of `q4xbase/gamex86.dll` against retail's yields 29 new classes.
Every one is absent from **both** openQ4-game trees. Grouped by subsystem:

### Boss / elite AI — 39 placed entities across 7 of 13 SP maps

| Class | Role | Used by |
|---|---|---|
| `riMonsterValkaryne` | Final boss; dock/undock state machine, volley count from a new `ai_valkaryneShots` cvar | `m09_valkaryne`, `m08_cryofac` |
| `riMonsterPainLord` | `m05_bio` boss; processing-station states, damage-region health, spike-pinning, 6 script callbacks | `m05_bio` |
| `riMonsterRetch` | Buffs nearby friendlies (damage/defense multipliers, beam FX); needs an `aas96` profile | 5 maps, 17 spawns |
| `riMonsterWalker` | Walking AI — **distinct from** the existing `rvVehicleWalker` (inherits `actor_default`, not a vehicle base) | 3 maps, 10 spawns |
| `riMonsterTank` | Destructible chest/launcher damage regions, `canBePinned` | 4 maps, 5 spawns |

### Vehicles — the m07 race act and m03 air assault

| Class | Role |
|---|---|
| `riVehicleSpeederBike` | `rvVehicleRigid` subclass; shield subsystem, crash-speed damage tiers, ADSR boost envelope, FOV push, gravity-distance clamp |
| `riVehiclePartBoost` | Boost part — without it the bike spawns with no boost |
| `riVehiclePartSplineTether` | Track tether (latent; not currently placed) |
| `riVehicleCockpitWeapon` | Base cockpit weapon: reticule/lock-on layer, heat/overheat state machine |
| `riVCWMissileTurret`, `riVCWPulseCannon` | The two cockpit cannons |
| `riReticuleEffect` | Lock-on reticule: 4 crosshair materials/colours by lock state, 3 guide FX, distance scaling |
| `rvVehicleGravGun` | Grav-gun turret (latent); depends on porting `idGrabber`/`idForce_Grab` |

Missing vehicle-part spawnclasses hit `gameLocal.Error` — a hard drop, not a warning —
so `m03_airassault` and the `m07` race maps fail outright.

### Weapons — missing `weaponclass` is fatal on weapon select

| Class | Notes |
|---|---|
| `WeaponGoobGun` | Flame/goo launcher. **Start from `WeaponNapalmGun.cpp`, which openQ4 already has** — this is a rename-and-extend, not a from-scratch class |
| `rvWeaponFreezeGun` | Plus a frozen status effect on `idActor`: movement lock, `filter_freeze` skin swap, shatter-gib death path, screen overlays |
| `WeaponSpikeGun` | Sticky projectile that pins corpses to surfaces within `maxPinDistance` |
| `WeaponGrappleHook` | Plus a rope/constraint mode on `idPhysics_Player`, 7 cvars, and `IMPULSE_27` (currently `// <unused>` in `framework/UsercmdGen.h:90`) |
| `rvWeaponConcussionGun` | In the DLL but no weapon def ships — latent |

`rvWeaponGauntlet` and `rvWeaponHyperblaster` already exist in openQ4 as retail classes.

### Damage, projectiles, objectives

| Class | Role |
|---|---|
| `DOTEntity` / `FireDOTEntity` | Damage-over-time pair, dispatched via `spawnclass` **on a damage def** — a hook openQ4 does not have. Without it the goob gun does impact damage only |
| `riFireFX` | Backs `func_fire_volume` — 20 placements. Two health-driven fire stages, periodic damage pulses, FX orientation from `fxtarget` |
| `riProjectileSpaceRocket` | `idProjectile` subclass with target-offset aim bias and actor pass-through |
| `idTarget_ObjectiveBeacon` | 12 instances across 5 maps; anchors the compass system (see Tier 2) |
| `riPickupItemGroup` / `riPickupGroupHelper` | MP shared-respawn item grouping (`q4xctf6` balance) |
| `WeaponGroup` | Helper backing `g_weaponGroup0..7` / `g_weaponPickupPriority` |

### Explicitly **not** gaps

`idEntityFx`, `idCameraFollow`, `idTrigger_GuiOverlay`, `rvDeathPush` and `rvPlaybackEnt`
appear as `spawnclass` values in the defs but are absent from **both** the expansion DLL
and the retail DLL. They are dead Doom 3 leftovers that fail identically on stock
Quake 4. Do not implement them.

---

## Tier 2 — missing game features

**Powerup enum is off by one, and one type overruns the array.** Verified by reading
every `"type"` in `def/powerups.def` against the enum in `Player.h`:

| Expansion def | Its `type` | What openQ4 has at that index |
|---|---|---|
| `powerup_quad_damage` … `powerup_moderator` | 0–11 | correct |
| `powerup_adrenaline` | 12 | `POWERUP_DEADZONE` — **wrong powerup applied** |
| `powerup_deadzone` | 13 | `POWERUP_TEAM_AMMO_REGEN` — shifted |
| `powerup_team_ammo_regen` / `_health_regen` / `_damage_mod` | 14/15/16 | all shifted by one |
| `powerup_fc_armor_regen` | 17 | `POWERUP_MAX` is 16 — **out of range** |

Type 17 is **not** a memory overrun: `idPlayer::GivePowerUp` guards
`powerup < 0 || powerup >= POWERUP_MAX` and returns false with a warning
(`mpgame/Player.cpp:5582-5586`), so `powerup_fc_armor_regen` is simply rejected and never
works. The genuine bug is the silent one — indices 12–16 are all *in range*, so no guard
fires and the **wrong powerup is applied**: `powerup_adrenaline` lands on
`POWERUP_DEADZONE`, `powerup_deadzone` lands on `POWERUP_TEAM_AMMO_REGEN`, and so on.
`powerup_deadzone` is placed 22 times across the CTF maps, so this is actively hit.

Fix by **appending** `POWERUP_ADRENALINE` and `POWERUP_FC_ARMOR_REGEN` at the end of the
enum (16, 17 — `inventory.powerups` is an `int` bitmask, so the ceiling is 32) and
resolving powerup identity by **def name** rather than the def's numeric `"type"`.
Appending rather than inserting keeps every existing index byte-identical, which matters
because the index is sent over the wire as a short in `EVENT_POWERUP`
(`mpgame/Player.cpp:5591`). Affects `Player.h` in **both** trees.

**The buy menu is completely dead.** Four separate mismatches:
- the GUI issues `sq_buy` / `sq_buyMenu`; openQ4 registers `buy` / `buyMenu` (a two-line alias fix per tree)
- the GUI reads `gui::canbuy_*`; openQ4 publishes `buyStatus_*`
- 13 offered items have no `ItemBuyStatus`/`GetItemCost` entry
- openQ4's `idPlayer::ItemBuyStatus` blanket-refuses `wpmod_` purchases, which are the expansion's headline buying feature

> **Resolved, 13 November 2026 (not in 0.14.1).** The expansion's buying is a
> server option on base content: **Buying** offers Quake 4's rules or The
> Awakening's (its prices, its cash awards over the content's, the Dark
> Matter Gun, FC Armor Regen and four weapon mods), bought from a retained buy
> card. Items that need the expansion's own content, and the two mods the
> multiplayer weapons already outdo, are left out. See
> [multiplayer buying](mp-buying.md).

**`maps.def` spells the key `"TeamDM"`; openQ4 looks up `"Team DM"`.** Add a
whitespace-tolerant compare in `MPMapSupportsGameType`/`MPGameTypeByName`
(`mpgame/mp/GameTypes.cpp:222-265`). Worth doing engine-side rather than patching the
content, because openQ4's own Clan Arena / Freeze Tag / Red Rover all carry
`mapDeclKey "Team DM"`.

**There is no compass system at all.** `idTarget_ObjectiveBeacon` (Tier 1) is only half
of it: nothing writes `gui::objectiveYaw`, `gui::playerYaw` is set only on weapon-zoom
GUIs, and the show/hide named events have no firer. Note the compass `windowDef`s are
commented out in the shipped `q4xhud.gui`, so none of this is testable end-to-end until
the content is uncommented.

**~30 def keys on existing classes have no reader.** Encounters spawn but play wrong:

| Key | Effect | Scale |
|---|---|---|
| `velScale` on `trigger_hurt` | Toucher velocity scaling | **122 instances** |
| `iff` / `spawn_iff` / `onlyTarget[2,3]` | Friend-foe and target restriction | `m03_airassault`, m01 trench, m09 |
| `useMasterRoll` / `useMasterPitch` / `bindOrientied` | Per-axis bind orientation | m03 dropship hierarchy |
| `canTurn` | Flyer turn lock | m03, m06_mcc_invasion |
| `ignoreAAS`, `requestDocking` | Boss navigation/state | m05_bio, m09 |
| `dynamicAccuracyMin/Max`, `accuracyLerpTime`, `delayedTracking`, `lockDelay` | Turret accuracy model | most q4x AI |
| `vampiric` on the gauntlet mod | Health leech fraction (0.35) | `q4xdm11` |
| `scanAnim` on `env_q4x_security_camera` | Data-driven sweep animation | scattered |

**Smaller items:** `g_weaponGroup0..7` / `g_weaponPickupPriority` unregistered (default.cfg
sets the latter unconditionally); DeadZone `sq_*` round/buy timing cvars exist only as
commented-out lines at `SysCvar.cpp:49-51`; `gui::mcchealth` and `gui::shealth` never
written; `mphud.gui` fires `addDeathLine` but never `removeDeathLine`, so the obituary
list grows unbounded; `FAS_ExtractViseme` has a missing `else` at `LipSync.cpp:273` in
both trees that discards the 100%-intensity viseme table, leaving cinematic mouths
under-articulated.

---

## Tier 3 — engine-side gaps

**All 157 per-model `.cm` collision files are `CM "2"`; openQ4 accepts only `"3"`**
(`cm/CollisionModel_files.cpp:704`). Every authored clip hull is rejected and silently
rebuilt from the render mesh at load. This is expansion-specific: the expansion's model
`.cm` files are 157/157 v2, while retail ships 870 v3 against only 4 v2 — and those 4 are
themselves `models/mapobjects/q4x/…` from the q4x mappack in `pak019`. All 32 *map* `.cm`
files are v3 and load fine.

Widening the version gate is necessary but **not sufficient**. The v2 polygon grammar
genuinely differs — verified by diffing records directly:

```
v3:  … "material" ( u v ) ( u v ) ( u v ) 691      <- trailing primitiveNum
v2:  … "material" ( u v ) ( u v ) ( u v )          <- record ends here
```

`ParsePolygons` calls `src->ParseInt()` unconditionally at
`cm/CollisionModel_files.cpp:493` once it sees the texBounds `(`. On a v2 file that
swallows the *next* polygon's vertex count, desyncing the whole block from the second
polygon onward. Both the gate and a version-conditional `primitiveNum` read are needed.

**DXT3 (BC2) DDS is rejected by the loader** — 3 files, one referenced by explicit
extension with no `.tga` fallback (`textures/hal9000/support_d` renders as the default
checkerboard). Needs an enum member, blockSize 16, the `DXT3` FourCC, and a BC2 decode
branch in `imagetools/Image_files.cpp`.

**The Vulkan backend cannot run `glprogs/glsl/speedblur`** — custom GLSL stages resolve
through a hardcoded family whitelist. The GL backend loads it from source. Needs a
family enum, a basename match in `vk_Backend.cpp:759`, and a SPIR-V pair (structurally
near-identical to the existing `material_sniper_stretch2` shaders). Latent today: the
speeder HUD window that uses it ships `visible 0`.

**`sound/music/` is not recognised as a music path**, so the expansion's soundtrack is
never tagged `SSF_MUSIC` and `s_musicVolume` silently does nothing. One-line fix in
`SND_IsMusicSamplePath()` (`sound/snd_shader.cpp`).

**The main-menu command table lacks `GetCVarValue` and `GetVecCVarValue`**
(`framework/Session_menu.cpp:2416`), so the crosshair size and R/G/B colour sliders
initialise to zero.

**Optional:** `gui4`/`gui5` keys exceed `MAX_RENDERENTITY_GUI` (3), losing 4 decorative
screens. Raising it touches `renderEntity_t` layout — probably not worth it.

---

## Already supported — verified negative results

These matter as much as the gaps, because several looked like obvious blockers:

- **Material guides are fully implemented.** The 255 `Guide name … not found` warnings in
  `q4xbase/warnings.txt` are **not** an openQ4 defect. All 13 guide names the expansion
  uses are defined in retail's `guides/material.guide` (`pak021.pk4`), openQ4 loads all
  64 (`Loading guides.... 64 loaded` in `q4base/logs/openq4.log`), and the real
  `q4xdm14` run under openQ4 in this same install produced zero guide warnings. That
  warnings file came from the retail alpha `quake4.exe` bundled in the drop, which failed
  to read the retail pk4s at all. Do not treat it as an openQ4 defect list.
- **File formats:** `.map` Version 3, `.proc` PROC "4", world `.cm` CM "3", AAS
  DewmAAS "1.08", MD5Version 10, ASE 200, LWO2, RoQ 0x1084 — every header on every file
  checked, no outliers. No parser work is needed to open this content.
- **All 389 retail script events** are registered in both trees with no signature drift;
  every `sys.*` call resolves; every syntax construct the scripts use compiles.
- **All 74 material/stage keywords**, **all 7 GUI window classes** and every GUI keyword
  and script command, **all 15 sound-shader keywords**, and every `.fx` token, segment
  kind and particle kind are implemented. The expansion's BSE vocabulary is a strict
  subset of the 1,156 retail effects openQ4 already runs.
- **Lipsync is fully precomputed.** The 36 `.lipsync` decls play through the existing
  path; the Annosoft runtime (`liblipsync_tltb.dll`, the `.alex` lexicons, `.hmm` models)
  is dev-time authoring data and is never needed at runtime.
- **No new gametype.** openQ4's roster is a strict superset of what the expansion
  declares; DeadZone already exists (`riDeadZonePowerup` is registered and the bots
  understand it).
- **MP bots work on all maps** via the runtime navmesh — the absence of AAS on the MP
  maps is irrelevant, and the stray `q4xtourney1.aas32` is dead weight.
- Campaign chaining (`target_endLevel` + `nextMap` + `endOfGame`), all 330 SP classnames
  (via case-insensitive decl lookup and `inherit1..N`), entity budgets (peak 3,039
  spawned vs `MAX_GENTITIES` 4096), portal sky, subview cameras, world extents, the 7
  `.af` files, the `progimg/addnormals` cache, RXGB normal maps, and non-power-of-two
  resampling are all fine.

---

## Content gaps — no code change helps

The drop has real authoring holes. Listed because they affect playability and would
otherwise be mistaken for code bugs:

**Shadowing is the recurring pattern** — the expansion ships same-named files that
override retail's whole file rather than merging:

| File | Retail definitions lost | Consequence |
|---|---|---|
| `def/debris.def` | 71 entityDefs | Breaks the BSE `debris` particle in 64 `.fx` (harvester, hornet, turret, flatbed death effects) |
| `def/ai/persona.def` | 87 persona defs | 6 referenced by expansion maps — marines in m01/m06 have no chatter |
| `sound/music.sndshd` | 77 music/ambience shaders | Main-menu music is silent as shipped |
| `materials/q4x_mapobjects.mtr` | 4 material decls | Cryo hangingman renders default |

Renaming the expansion's copies (e.g. `q4x_debris.def`) fixes all of these.

**Other content holes:** 107 of 2,121 sample references resolve to nothing; 13 script
files (all `*_vo.script`, 1,827 functions) are never `#include`d, so the scripted VO
layer is dead; `m09_valkaryne.proc` was compiled from a different `.map` revision than
its `.cm`/`.aas`; `m02_trianfac` ships no `.aas96` despite spawning `monster_q4x_retch`
which requires it (openQ4's retches fall back to `aas48`); 5 in-map GUIs, 11 effect decls, 20 ASE material names, 6 models and
`teleport_dropper/hide_bite.md5anim` are absent; `q4xdm7`/`8`/`9` have `mapDef`s but no
maps; `default.cfg` sets an invalid `si_gameType` and a nonexistent `si_map`.

**One risk checked and cleared:** an unresolved `frame N call <fn>` is *not* a warning —
`idDeclModelDef::ParseAnim` calls `MakeDefault()` and returns false
(`anim/Anim_Blend.cpp:2807-2812`), collapsing the entire modelDef so every character
using it loses its mesh and animations. The expansion has 246 such sites across 185
distinct targets, so a single miss would silently delete a character model. All 185 were
resolved against the actual compiled closure — `scripts/main.script` expanded through its
`#include` graph (64 units, zero unresolved includes) plus the 9 worldspawn-autoloaded map
scripts, 3,899 functions — and **all 246 sites resolve. Nothing is missing.** The 13
uncompiled VO script files hold no frame-call targets, so leaving them out of the build
costs the scripted VO but does not endanger any model.

---

## Suggested order of work

1. **Register the 10 script events as stubs**, ship `mod.json`, fix the launch line. The
   game boots and retail maps play again. Hours, not days.
2. **Powerup enum fix** — it is a memory-safety bug and it is small.
3. **`riFireFX`, `DOTEntity`/`FireDOTEntity`, `idTarget_ObjectiveBeacon`, `velScale`** —
   small self-contained classes/keys with wide reach across the campaign.
4. **The four weapons.** `WeaponGoobGun` first (it is `WeaponNapalmGun` renamed and
   extended), then spike, freeze, grapple.
5. **The five boss AI classes** — the largest single tranche, and what makes the SP
   campaign actually play.
6. **Vehicles**: cockpit-cannon subsystem (unblocks `m03_airassault`), then the speeder
   bike (unblocks the whole `m07` race act).
7. **MP layer**: buy-menu protocol, `"TeamDM"` alias, weapon groups.
8. **Engine items**: CM v2 gate + parser, DXT3, Vulkan speedblur, music path, the two
   menu cvar commands.
9. **Content pass** on the four shadowing files (`debris.def`, `persona.def`,
   `music.sndshd`, `q4x_mapobjects.mtr`) — renaming them recovers 239 lost retail
   definitions for near-zero effort.

Nothing here requires a new renderer capability, a new file-format parser, or a new
gametype. The expansion is a game-DLL project.

---

## Method and confidence

Eleven parallel domain audits, each followed by an independent adversarial verification
pass instructed to refute rather than confirm: 234 findings, 4 refuted, 61 reclassified
(mostly severity), the rest confirmed with file:line evidence on both sides. Retail
comparisons were made by reading the shipped `pak*.pk4` archives directly with correct
pak-override precedence, so "retail already has this" claims are checked rather than
assumed. The expansion's own `gamex86.dll` was RTTI- and string-diffed against retail's
to recover the authoritative class roster, which independently corroborated the
def-derived and script-derived gap lists.

Four claims were additionally re-checked by hand because they were high-consequence or
because the finder and its verifier disagreed: the material-guide question (guides
resolve — not a gap), the frame-call `MakeDefault` risk (246/246 resolve — not a gap),
the `CM "2"` polygon grammar (the verifier was right; the gate alone is not enough), and
the powerup enum overrun (confirmed, with the index table above).

Two caveats. The expansion DLL is built from an older, console-derived Q4 branch than
retail 1.4.2 (PDB path `c:\Ritual\Q4x\Win32\Release\Gamex86.pdb`), so a minority of
string-delta entries are 1.2-era regressions rather than new features; those were
excluded. And nothing here has been confirmed by actually running openQ4 with
`fs_game q4xbase` — the Tier 0 items prevent it. The first empirical step is to register
the ten event stubs and see how far the campaign gets.
