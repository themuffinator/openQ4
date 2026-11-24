# Multiplayer modifiers and presets

A modifier changes how a game type plays without being a game type: Instagib,
Quad Hog, Vampiric Damage, Weapons Frenzy and the single-weapon arenas run
over Deathmatch, Team Deathmatch, Capture the Flag and the rest. A preset
names a game type, a ruleset, modifiers and limits together. This is roadmap
item MP-14; the player guide is
[Modifiers and presets](../user/server-setup.md#modifiers-and-presets).

## The decision

OPT-05 kept "factories and rulesets" outside the accepted scope. MP-11 has
since shipped a ruleset (`si_ruleset`) and Instagib shipped as a switch over
any mode, so the question changed from whether to how. The decision recorded
here:

- **One layer, not one switch per feature.** Every modifier is a row in
  `mpModifierTable` (`src/game/mp/Modifiers.cpp`), asked for by one
  server-info cvar and latched with the others. Instagib is a row of it.
- **Typed data, never executable configuration.** A preset is a list of
  allow-listed settings, built in or read from `presets/<name>.cfg`. Nothing a
  vote can name is ever passed to the command system.
- **Modifiers are not game types.** The append-only `gameType_t` table is
  untouched. Overload, Harvester, Domination and Attack & Defend stay reserved
  under OPT-05.

## The switches

| Setting | Default | Latched | What it asks for |
|---|---|---|---|
| `si_instagib` | 0 | yes | `MPMOD_INSTAGIB` |
| `si_quadHog` | 0 | yes | `MPMOD_QUADHOG` |
| `si_vampiric` | 0 | yes | `MPMOD_VAMPIRIC` |
| `si_weaponsFrenzy` | 0 | yes | `MPMOD_FRENZY` |
| `si_weaponArena` | `none` | yes | a row of `mpWeaponArenaTable` |
| `si_powerups` | 1 | yes | map powerups spawn |
| `si_selfDamage` | 100 | no | percent, 0 to 999 |
| `si_knockback` | 100 | no | percent, 0 to 999 |
| `si_fallDamage` | 100 | no | percent, 0 to 999 |
| `si_weaponRespawn` | 0 | no | seconds, 0 for each weapon's own time |
| `si_preset` | empty | checked | the preset last applied |
| `si_modifiers` | empty | published | the latched set as tokens, read-only |

The scales are whole percentages so they read the same on every platform and
fit the menus, which deal in integers. Both game trees declare the ten
settable cvars word for word, because the Create Server page binds them while
`game_sp` is the loaded module.

## The latch

`idMultiplayerGame::SetGameType` runs on the server and on every client
before the map is populated, on a map load and on a restart. Its last step,
`LatchModifiers`, calls `rvMPModifiers::Resolve`, a pure function of the
server info and the game type:

- Instagib owns the loadout and the damage rule. It switches off the weapon
  arena, Quad Hog, Vampiric Damage and Weapons Frenzy.
- Quad Hog resolves only in `GAME_DM`.
- Weapons Frenzy stands aside for a weapon arena and for `GTF_FULLARSENAL`
  modes.
- The single-player Arena campaign, hosted by `game_mp`, resolves to nothing.

Everything a peer does while populating the map reads the latch and nothing
else (`PickupInhibited` is pinned to that), so entity numbering agrees.
`CheckModifierChange` runs every server frame: when the server info asks for
something other than the latch, it queues one `serverMapRestart` through
`QueueRulesRestart`. A caller that has just written new rules and wants a
restart regardless (the `preset` command, a passed vote) calls
`QueueRulesRestart` itself and gets the same single restart.

The server also publishes the latch as `si_modifiers`
(`instagib quadhog vampiric frenzy arena:<weapon> nopowerups`). The engine
builds the server info it answers browser queries from before the game has
resolved anything, so a changed value triggers one `rescanSI`.

## What each rule does

| Rule | Where | Behaviour |
|---|---|---|
| Fixed loadout | `idPlayer::GiveFixedLoadout`, `idMultiplayerGame::FixedLoadoutWeapon` | Instagib's railgun or the arena's weapon with ammo -1, and the gauntlet. Warmup's arsenal, buying and weapon drops stand aside; a full-arsenal mode still grants its armour and health under an arena |
| Pickups | `idMultiplayerGame::PickupInhibited` from `idGameLocal::InhibitEntitySpawn` | Full-arsenal modes and Instagib clear everything. An arena clears `weapon_` and `ammo_`. Vampiric clears `item_health`. Quad Hog clears `powerup_`; `si_powerups 0` does too, except in `GTF_ARENA` modes. Objectives are set aside first |
| Quad Hog | `RunQuadHog`, `SpawnQuadHogItem`, `QuadHogFragScores` | Server only. A quad named `openq4_quad_hog_<n>` is spawned over a random player spawn as a dropped powerup with no time limit. The hog is whoever holds `POWERUP_QUADDAMAGE`; the stock drop on death leaves it where they fell. A quad nobody holds for 20 s is removed and another spawns 5 s later. In `PlayerDeath` a frag scores only when the killer or the victim holds the quad; a suicide always costs |
| Vampiric Damage | `ApplyVampiricDamage` from `idPlayer::Damage` | Server only, before the hit is applied. The attacker gains `g_vampiricPercent` (50) of the health the hit removes, capped at twice max health (max health under the Classic ruleset, which does not drain overheal). Never from self or team damage |
| Weapons Frenzy | `idItem::Pickup`, `idPlayer::UpdatePowerUps` | Weapon and ammo pickups return in a quarter of their time, floor 2 s. Carried ammo gains a twentieth of the type's limit each second. Unlimited ammo is left alone |
| Scales | `rvMPModifiers::SelfDamageScale` and friends | Self damage scales the hit and the armour's share after `CalcDamagePoints` when `attacker == this && inflictor != this`, so a suicide is never scaled and the push is untouched. Knockback multiplies every push. Fall damage scales the three landing damage calls; at 0 a landing costs nothing |
| Weapon respawn | `idItem::Pickup` | Replaces a weapon's respawn time before the bots and the item timers hear of the pickup |

## Presentation

- `idMultiplayerGame::LocalizeGametype` wraps the game type's name with each
  modifier's title format (`#str_231961`, `231963`, `231965`, `231967`) and
  names an arena's weapon after it (`#str_231969`). Scoreboard, join screen,
  welcome card and in-game menu all read it.
- The loading card (`retainedLoadingServer` in `Game_local.cpp`) resolves the
  same switches from the server's own info, before anything is latched.
- The server browser reads `si_modifiers` (added to `BROWSER_KEYS`). Its rows
  show the qualified title and its details card gains a **Modifiers** line
  (`idServerScan::MatchTypeName`, `GetServerDetails`). The text is a remote
  server's, so it is matched against the browser's copy of the tables and
  never displayed.
- Create Server's **Modifiers** tab holds Preset, Instagib, Quad Hog,
  Vampiric Damage, Weapons Frenzy, Weapon Arena, Powerups and Weapon Respawn;
  the three percentages are on **Rules**.

## Presets

`mpPresetTable` (`src/game/mp/Presets.cpp`) is the built-in list. Row 0,
`standard`, holds every composition setting at its cvar default, and every
other preset is applied over it. `rvMPPresets::Apply` writes the settings and
`si_preset`; the caller restarts.

`si_preset` is a statement about the settings, not a request. At the latch
the server compares the composition settings (the standard row's, and
`si_gameType`) with what the named preset resolves to and clears `si_preset`
when they differ. Limits are not part of that comparison.

A server's own preset is `presets/<name>.cfg`: one `key value` a line, an
optional leading `set` or `seta`, `//` comments, at most 8,192 bytes. The name
is lower-case letters, digits, `-` and `_`. Any key outside the allow list
(the composition, the limits in `mpPresetLimitKeys`) refuses the whole
preset. A file cannot replace a built-in name.

The Create Server page is also shown while `game_sp` is loaded, so the
session applies a built-in preset itself from its own copy of the table
(`RETAINED_CREATE_PRESETS`) and shows **Custom** by the same comparison.

## Commands

| Command | Effect |
|---|---|
| `modifier <name> <0\|1>` | Sets one modifier and rescans; the latch check restarts |
| `weaponArena <weapon\|none>` | Sets the arena and rescans |
| `preset <name>` | Applies a preset and restarts |
| `listPresets` | Lists built-in and file presets |

All are server-side and refused while a managed match owns the rules
(`RejectManagedLegacyMutation`). `rvMPModifiers::ValidateModifier`,
`ValidateWeaponArena` and `rvMPPresets::Validate` return a reason without
printing, for the vote surface MP-12 builds on them.

## Checks

- `tools/tests/mp_modifiers_contract.py`: the pins, the three mirrored tables
  (browser, Create Server arenas, Create Server presets), string coverage in
  all twelve languages, and an executable half that compiles the shipped
  `Modifiers.cpp` and `Presets.cpp` against doubles and runs resolve, token
  round trips, titles, preset application and drift, preset files and the
  commands.
- `tools/tests/mp_instagib_contract.py`: Instagib's own rule set.
- `tools/tests/mp_modifiers_smoke.py`: scripted listen-server runs with bots
  (`quadhog`, `vampiric`, `presets`, `instactf`). It needs the game assets.

## Open

- Votes (`callvote modifier`, `callvote preset`) belong to MP-12's vote
  surface, which reserves `si_voteFlags` bits 20 and 21 for them.
- The managed match rules do not record the ruleset, the modifiers or the
  scales, so a managed match's digest does not vouch for them.
- Last Man Standing on the Clan Arena state is not built.
- The HUD shows the hog through the stock Quad Damage effect and the centre
  print; there is no dedicated hog marker on the scoreboard.
- Bots take the quad as they take any powerup (five times in a 150 s test
  match); they do not hunt the hog as an objective.
- The Create Server Modifiers tab's caption is sized to its longest
  translation and draws smaller than its neighbours; the UI register has no
  record of the tab or of the join card's eighth details line yet.
- The three percentages and `si_weaponRespawn` are covered by the contract
  only; no scripted run exercises them in play.
