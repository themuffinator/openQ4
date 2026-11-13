# Quake 4: The Awakening on openQ4

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

The Awakening is the Quake 4 expansion Raven Software and Ritual Entertainment never released. Justin Marshall recovered and published its alpha. openQ4 plays its single-player campaign from your own copy of that content, using openQ4's own engine and game code: it never loads the expansion's leaked game DLL, and openQ4 ships none of the expansion's files. Where the alpha left something unfinished, such as the chaingun turret's skin, openQ4 adds its own art instead.

The Awakening is built from unfinished alpha content. All 13 maps load, play and save, from the trenches of Stranarus to the Valkaryne's lair, but complete start-to-finish playthroughs are still being tested, and some sounds, effects, models and voice-over are missing from the content itself (see [Known issues in the alpha](#known-issues-in-the-alpha)).

## Install and play

1. Put the expansion's `q4xbase` folder next to Quake 4's `q4base` folder, or point openQ4 at it with `fs_awakeningpath`. [Campaigns](campaigns.md#install-the-awakening) has the full steps and what to do if the campaign stays unavailable.
2. Open **Single Player → Campaign**, choose **The Awakening**, then **New Game** or **Chapters**.

The Awakening keeps its saves and settings apart from Quake 4's, in the `q4xbase` folder inside openQ4's save folder. Returning to Quake 4, Arena or multiplayer removes the expansion's files, so its changes never leak into them. Multiplayer is not part of this integration and always runs on stock Quake 4 content; a server can still sell by the expansion's buying rules (see [Buying in multiplayer](buying.md)).

The campaign works with openQ4's other features; in VR, for example, you sit in the expansion's turrets and ride its speeder bike.

**Not in 0.14.1:** mods can play over The Awakening, and a mod's `mod.json` can tie it to one campaign. See [Mods and The Awakening](campaigns.md#mods-and-the-awakening).

### Saves and updates

You can save and load on every campaign map. The expansion's marines have up to 540 animations, more than openQ4's saves used to allow, so a game saved on the prison level (m04) never loaded; it does now.

Many of the fixes below correct the maps' mission scripts, and a saved game keeps the scripts it was made with. A save made with an older openQ4 version therefore keeps that version's mission flow, and an old save of the opening level (m01) may need a fresh start of the map. Start a map afresh to get its script fixes. Fixes in openQ4's own code, such as AI behaviour and the prison alarm's sound, apply to old saves too.

## What openQ4 rebuilt

Every new monster, weapon and vehicle is rebuilt in openQ4's game code, down to the details the levels rely on:

| Element | Where | What it does |
|---|---|---|
| Fire vents | m01, m01 part 2, m02, m03, m04 | Burn whoever touches them. Only the freeze gun puts them out, dimming their firelight as they shrink. |
| Walker | m01, m01 part 2, m06 invasion | A marine walker on your side; it fights the Strogg with its cannon and rockets. |
| Retch | m01 part 2, m02, m04, m05, m08 | Boosts a Strogg ally's damage or defence with a beam, fires lightning and dodges. |
| Tank | m02, m04, m08, m09 | Railgun, machine gun and shoulder rockets behind armour. Break its launcher to stop the rockets and its chest plate to reach its heart, which takes six times the damage. |
| Pain Lord | m05 | A boss who works a marine-processing machine. Two half-processed marines on his back shield him; he is stunned at two thirds and one third of his health, and enraged when the marines are torn off. If he finishes his third processing, Strauss dies and the mission fails. |
| Valkaryne | m08 (set piece), m09 | Docked on her platform she is untouchable and fires death-ray volleys; on foot she uses her railguns, grenades, rockets and claws. |
| Goob gun | m02 on | Its globs burn what they hit and burst into six smaller globs. Its flamethrower mod adds a short-range alternate fire. |
| Freeze gun | player arsenal | Chills enemies until they slow, freezes them solid as they die, and shatters them. Ammo regeneration needs the regeneration powerup. |
| Spike gun | player arsenal | Spikes stick where they land, stay in the enemies they kill and pin the bodies to the wall behind. Its scope mod fires explosive spikes. |
| Turrets | m01, m01 part 2, m04, m05 | Tighten their aim the longer they fire and pause before firing again; security cameras sweep. |
| Speeder bike | m06 invasion, the three m07 races | Hover bike with a gun and a boost on the crouch key. |
| Dropship and MCC cannons | m03, m06 invasion | A pulse cannon that overheats, and a missile launcher whose rockets lock on and home. |
| Scripted battles | m03, m04, m06, m07, m09 | Set pieces name the only enemies each fighter may attack (`onlyTarget`), so scripted battles stay on script. |

- **Enemies.** Elite grunts pull you in with a gravity well, elite gunners spray their fire, and tactical elites throw grenades. The Tank scatters its rockets once its launcher smokes and loses them when either armoured part breaks. Space flyers (m03) climb, dive and bank along their attack runs instead of turning to face you, and the fighters of m03 and m06 bank into their turns.
- **Weapons.** The goob gun's flamethrower shows its flame, and your character holds its pose. Spikes knock bodies back towards the wall they pin them to and keep their orientation in flight.
- **Freeze and support effects.** Frozen enemies resume their shatter countdown after loading. Freeze hits respect boss invulnerability and work on enemies with one health point left. Thawing keeps the Pain Lord's enraged speed, and Retch boosts and ice work together without stacking or cancelling each other.
- **Cockpit targeting.** Missile locks need a visible target in range. A distant enemy near the crosshair no longer blocks a valid closer target, and rockets and target markers aim correctly at large enemies.
- **Vehicles.** The dropship and MCC cannons protect their gunner, recentre when you leave and freeze during m03's scripted ammo jam. The pulse cannon's barrels kick back with every shot and fold away while the missiles are selected; the cannon keeps cooling and the launcher keeps reloading while you use the other weapon. The speeder bike turns, grips and hovers as built: it holds a steady turn, reaches racing speed and keeps itself off walls and tunnel ceilings. The race tracks' rough edges (m07) slow it, with the damage going to its shield. The races are against the clock: if a lockdown gate closes before you get past, the mission fails.
- **Vehicle HUDs.** The dropship guns (m03) show the ship-status panel under the radio display, with its damage, objective markers, attack warnings and an Exit prompt; the same panel appears on your own HUD while you move about the dropship, and its view cone turns with your view. The MCC cannon (m06) shows the MCC's hull, its gauge turning red at a quarter. The speeder bike (m07) shows your speed, the race clock (red in the last ten seconds) and checkpoints; its prompt names your crouch key while a boost is ready, the screen's edges streak as you boost, and its gauges warn when the hull or shields run low. The crosshair also shows when a marine is too busy to talk.
- **Both interfaces.** These HUDs work in the new interface and in the classic one (`ui_retained 0`). The classic interface also lays out your on-foot HUD like the new one, with the gauges, the 13-weapon strip and pickup messages in the same places, and the HUD Size, Opacity and layout settings apply.

## Fixes to the alpha

The alpha's own code or content kept these from working as designed. openQ4 fixes them:

- **The Valkaryne finale (m09).** Her docking script was never run, so she never took her platform: she wandered harmlessly and could be killed before the Makron sphere, skipping the fight built around it. She now docks behind the sphere's shields and fires death-ray volleys, with about two seconds of warning; the arena's pillars give cover. When the sphere dies she undocks and fights on foot, and her death starts the escape. Her sphere phase also starts during her intro.
- **The beach's death-ray turret (m01 part 2)** is meant to bombard the landing, never you. It no longer turns on you when the turrets beside it, a sound or a hit draw its attention.
- **The Pain Lord's head.** His definition makes headshots count double, but it listed his legs over his whole body, so in the alpha every headshot counted as a leg hit (three quarters damage). Headshots now count.
- **The Retch** asks for navigation data that two levels lack (Trianfac and Stranarus Trench part 2), so four of them could never move. They now walk.
- **Fire passages (m01, m02).** Freezing a vent removes its invisible barrier and puts its light out fully. Burning vents let you through while dealing their intended touch damage, and saves restore the right state.
- **Trianfac's generator defence (m02).** Repair bots attach to their own stations, and Tak waits until all six are destroyed before finishing the sabotage. The destroy-generator objective appears before the explosion and completes after it. The extraction dropship finishes landing, and defeating the Tank completes the Nexus objective.
- **Falcon defence (m03).** Both turrets show their own cannon heat, weapon selection and missile status, and keep their aiming reticle visible. The ship diagram matches between the guns and on foot. Repairs restore hull health and clear repaired sections, and the engine repair completes once. Ship state survives switching guns, saving and loading. Missile volleys fire the charge shown on the cockpit display, from one to five rockets, and an empty magazine must reload before firing again.
- **The prison (m04).** Reinforcements land in the correct upper or lower cellblock, the maintenance bot uses its own workstations, and the bridge ambush works. Cellblock attackers switch to combat before reaching the marines' occupied cover. The security override completes once, keeping the alarm off and the shutters in place, and destroying the turret-room camera during the ambush no longer starts the door hack early or twice. The hanging camera now appears and scans instead of staying hidden inside the cell door above it. The security alarm gives quieter, local warning pulses with pauses, and computer alarms are less intrusive; the security console still switches the alert off.
- **The rescue and MCC defence (m05, m06).** Heavy hits no longer skip the Pain Lord's shield stages, destroyed captives disappear consistently, and interrupting his final processing attempt buys time to save Strauss. A rescue objective tracks the fight and completes with victory. Mitchell no longer reappears in the MCC arrival. The cannon defence has a longer opening fighter wave, keeps its flight runs working and stops watching the MCC's hull once the escape begins. Destroyed escape teleporters stop their reinforcements and cannot be restarted, and fast kills in the command room cancel its delayed teleporter. The service-lift lamp travels with its platform, the rescue teleport clears its destination before handing back control, and idle followers give Tak room to reach you for armour repairs. The upper freight panel no longer offers a broken interaction, and several lights, MCC monitors and textures now use assets the content does have.
- **Cryofac (m08) and the finale (m09).** Pressing Cryofac's cooling controls repeatedly cannot restart the cold damage, and squad and animation handoffs complete in both levels.
- **The goob gun's burning effect** shows from the first hit; the alpha looked it up in the wrong place.
- **The Strogg chaingun turret you man (Stranarus Trench, the prison)** was never textured: the alpha left it bare grey. openQ4 paints it as a battle-worn Strogg emplacement, with oxblood and field-grey armour, brushed steel, brass fittings, glowing vents and a red sensor lens. Firing it now has the weight of the game's other mounted guns: a heavier muzzle flash that lights the gun, tracers, heavier impacts, ejected casings and smoke after a long burst, with servos whining as it turns. Its rounds land under the crosshair (the alpha fired them parallel to your view from a muzzle well to the side, so close targets were missed), and the zoom button magnifies its view, as the machinegun's does. It has its own HUD, showing your health and armour, since you stand exposed behind the gun, and its barrel heat (see [Deliberate differences from the alpha](#deliberate-differences-from-the-alpha)).
- **The machinegun hits as hard as in Quake 4 again.** The alpha set the chaingun turret's damage by changing the damage your machinegun shares with it, so your machinegun hit for 40 a round instead of 15, more than its zoomed shots (20), and lost its bleeding, flesh-hit sounds and knockback. The machinegun is back to Quake 4's values, and the turret keeps its own 40.

## Deliberate differences from the alpha

openQ4 departs from the alpha in a few places, to improve unfinished scenes, keep your settings or keep the game fair:

- **The opening dropship (m01).** openQ4 gives the unfinished opening its own direction. The failed rescue holds a view across the aisle, the warning comes before the fall, Tak cuts the jammed harnesses while the squad exits, and the escape camera waits for the column to clear. Engine, impact, alarm, wind and cutting sounds accompany the lighting change and hull breach. The alpha's own voices and animations still provide the performances, so this is a polish pass on unfinished content. **Reduced Flashing** leaves out the added spark bursts. To see the alpha's own direction, set `g_awakeningM01Cinematic 0` in the console before starting the map.
- **The speeder races (m07).** Clocks, carried-over time and objectives follow each checkpoint consistently. Finishing clears the countdown and fills the progress display. Missing a gate stops driving and firing during the failure screen, and a late checkpoint cannot turn that failure into a win. The first lockdown plays its radio warning before the clock starts. Bomber attacks clean up correctly, a downed bomber stops its volley and ejects its pilot once, and two broken flyer destruction sequences now work. Leaving or disabling the bike ends its boost but keeps the recharge time.
- **Your field of view is yours.** The alpha's m03 gun scripts and the speeder bike's boost rewrote your field-of-view setting. openQ4 changes only the view and never touches the setting.
- **Burns merge.** In the alpha one goob shot could light up to eight separate fires on one enemy, about 350 damage. Each enemy now carries one burn, which later hits strengthen up to three times over.
- **The Retch's fallback.** Where a level lacks the Retch's own navigation data, it uses the next size down, which its body fits.
- **The chaingun turret heats up.** The alpha's turret fired at full rate and full accuracy for as long as you held the trigger. Now about five seconds of continuous fire overheats it: it vents steam and will not fire for about two seconds, until it has cooled to 40%. A third of a second off the trigger starts it cooling, and it cools from full in about two seconds, so bursts never overheat it. Cold, it is more accurate than the alpha's gun (2 degrees of spread); as it heats it spreads to the alpha's 5 degrees, and zoomed rounds spread half as much. The HUD shows the heat where your ammunition usually shows and as a bar under the crosshair, red from 75% and while the gun vents, with OVERHEATED. Its damage per round and rate of fire are unchanged, and the prison's scripted gunner never overheats.
- **An overheating pulse cannon says so.** The alpha's overheat signal reached none of the cannon's screens; openQ4 shows their JAMMED warning while it cools, and never clears the scripted ammo jam's warning early.
- **Changes stay in The Awakening.** The ammo-jam lock and banking flight paths apply only in The Awakening; stock Quake 4 vehicles and flight paths are unchanged.
- **Multiplayer-only content stays out.** The vampiric gauntlet mod belongs to the expansion's multiplayer arsenal and has no effect in the campaign.

## Known issues in the alpha

These are holes in the expansion's content itself. The opening scene's new direction reuses installed media, and apart from the chaingun turret's skin and effects, openQ4 does not supply missing performances or replacement art:

- Some voice-over never plays: the expansion's own scripts never load thirteen scripted voice-over files.
- About 100 sounds, a handful of in-map screens, effects and models, and one teleport-dropper animation are missing from the content.
- A few effects never show because the content refers to parts that do not exist: the force field and shield pieces in the Valkaryne's arena (m09) and the Tank's rocket flash.
- The background harvester in the m07 races is Quake 4's cinematic one, which has no turning animations. openQ4 no longer has it try to turn, which flooded the log and cost it its attacks.
- On the first race map (`m07_race1`) the scripted gunner and fighters have no navigation data, so they stay where they are placed.
- The expansion's code also has a grapple hook, a concussion gun, a gravity-gun turret and a speeder-bike tether, but nothing in its campaign gives or places them, so openQ4 does not implement them. The objective compass is switched off in the expansion's own HUD.

## For modders and testers

Everything below also applies to a mod running over the expansion (not in 0.14.1; see [Mods and The Awakening](campaigns.md#mods-and-the-awakening)).

### How openQ4 loads the content

- **Retail definitions kept.** The expansion ships files with retail's names (`def/debris.def`, `def/ai/persona.def`, `sound/music.sndshd` and others) that would replace retail's whole file. openQ4 also reads the retail file underneath, so the 239 definitions they would hide survive: debris in death effects, marine chatter and the menu music.
- **File formats.** Version 2 collision models (the expansion's model clip hulls), DXT3 textures, textures named `.jpg` that exist only as `.tga` (the Valkaryne's limbs), and music under `sound/music/`, which follows the music volume.
- **Menus and controls.** The menu commands the expansion's interface uses (`GetCVarValue`, `SetCVarValue`, `GetVecCVarValue`) work, and its `_altattack` binding is the alternate-fire button. The expansion's own menus are not used.
- **Ten new script events** for its levels: docking the Valkaryne, the Pain Lord's processing machine, the speeder bike's speed, gravity and boost, and two control-point queries.
- **A quieter log.** Flares in the Valkaryne's lair (m09) are not the single quads the flare effect expects; openQ4 warns once per material instead of hundreds of times a second.

### Keys openQ4 honours in Awakening content

None of these appears in retail Quake 4, so stock behaviour is unchanged.

| Key | On | Effect |
|---|---|---|
| `requestDocking` | Valkaryne | Script function she runs the first time she has an enemy (any namespace). |
| `ignoreplayer` | AI | Never takes the player as an enemy, by any route. |
| `q4xElite` | grunt, gunner | Gravity well (`gravityWellTime`) or spread fire. |
| `grenadedelay` | tactical elites | Milliseconds between offhand grenades (needs a `throw_grenade` animation). |
| `canturn`, `noFaceEnemy` | Strogg flyer | Keep a flyer on its spline's heading. |
| `useMasterPitch`, `useMasterRoll` | bound AI | Climb, dive and bank with the mover it is bound to. |
| `impactEntity` | projectile | The burst it spawns on impact (also read as `def_impactEntity`). |
| `impactForce`, `applyRotation` | projectile | Push on what it hits; keep the launch orientation. |
| `fx_altmuzzleflash`, `useAltFireAnim` | weapon | Alternate-fire muzzle flash and player torso pose. |
| `useGodMode`, `resetOnExit` | vehicle | Protect the gunner; recentre turrets when the gunner leaves. |
| `allowDisableMovement` | vehicle turret | `0` keeps a turret turning while its vehicle's movement is disabled. |
| `playMoveSound` | vehicle turret | `1` loops the part's `snd_move` while it turns, between `moveMinVolume` and `moveMaxVolume` dB by speed. |
| `heatPerShot`, `heatTolerance`, `safeHeatLevel`, `heatCoolDelay`, `heatDissipationPerSecond`, `overheatDissipationPerSecond`, `heatWarnLevel` | `idVehicleTurretGun` | Barrel heat for a player gunner: heat per round, the overheat limit and the level it must cool to, the pause before it cools and how fast it cools, and the HUD's warning fraction. |
| `spreadCold`, `zoomSpreadScale`, `snd_overheat`, `snd_overheatFire`, `snd_overheatRecover`, `fx_overheat` | `idVehicleTurretGun` | The spread of a cold gun (heat opens it to `spread`), the zoomed spread, and the overheat's sounds and venting effect. |
| `convergence` | vehicle weapon | `1` sends rounds from an offset muzzle to the point under the crosshair. |
| `velscale` | `trigger_hurt` | Scales the speed of what it hurts. |
| `traceRelativeDirection` | hover pad | Probe along a direction in the vehicle's frame. |
| `g_splineRollLookahead`, `g_splineRollMultiplier` (settings) | spline movers | How far ahead a mover facing along its spline looks to bank into a turn (500 is five spline points), and how hard it banks (2.0). |

A vehicle also passes script `guiEvent` and `setGuiParm` calls to its HUD as well as its world screens, as the expansion's code did. The pulse cannon moves its vehicle's `pulse_cannon_l` and `pulse_cannon_r` joints; a vehicle without them fires the same, with still barrels.

### openQ4's own additions

openQ4's additions to Awakening content live in its own files and never replace the expansion's:

- While The Awakening is running, any definition lookup of `<name>` first tries `<name>_q4x_openq4`. openQ4's turret weapon (`vehicle_chaingun_weapon_q4x_openq4`) inherits the expansion's, keeps its damage and rate of fire, and adds the effects, heat, zoom and convergence; `damage_machinegun_q4x_openq4` gives the player's machinegun Quake 4's damage back.
- The turret's HUD is `guis/vehicles/turret_hud.gui`. The vehicle's HUD reads the gun's `vehicle_heat`, `vehicle_overheat`, `vehicle_heatwarn` and `vehicle_zoom`.
- Material files named `materials/*_openq4.mtr` that sort ahead of a game's own file (`00_` names) supply the material instead of it; the turret's skin works this way and keeps the expansion's normal maps.
- Any vehicle weapon can eject casings (`def_ejectBrass`, `joint_view_eject`, `ejectOffset`) and trail `fx_burstSmoke` after a burst of `burstSmokeShots` or more.

### Content errors you may see in the log

- m09's frosty-breath effects name a joint only marines have, and two of its triggers call script functions the developers commented out or never wrote.
- The m08 and m09 developer give lists contain typos.
- m08's first-lab scientists call a script function (`FirstLabCleared`) the map never defines; nothing waits on it.

### Troubleshooting and diagnostics

These help when testing the campaign. The commands need cheats enabled; the settings do not.

| Command or setting | What it does |
|---|---|
| `openq4_testInput <ms> [attack] [zoom] [run] [crouch] [jump] [forward\|back\|right\|left <0-127>] [aim <entity>] [yaw\|pitch <deg/s>] [impulse <n>]` | Holds input on the local player for a stretch of game time, including a gunner's turret aim and weapon choice. |
| `openq4_viewReport` | Prints your view, field of view and god mode. |
| `ai_debugActions <name\|*>` | Prints an AI's actions and attacks, and the Awakening's docking, boosts, armour breaks and freezing. |
| `damage <entity> <amount> [damage def] [joint]` | Hits an entity, in a chosen joint's damage zone. |
| `g_debugVehicle 1` | Reports the cannons' overheating and missile lock. |
| `g_debugDamage 1` | Reports damage and burns. |
| `g_debugTurretGun 1` | Prints the chaingun turret's heat. |
| `g_awakeningM01Cinematic 0` | Plays the opening dropship scene (m01) with the alpha's own direction. Set it before starting the map. |

The development record, with the evidence behind each change, is the [support plan](../dev/archive/2026-10-18/plans/q4x-awakening.md); the original gap analysis is the [support audit](../dev/q4x-awakening-support-audit.md).

## Credits

Quake 4: The Awakening was developed by Raven Software and Ritual Entertainment. Justin Marshall recovered and published the expansion. openQ4's support for it is written independently and includes none of its content; the chaingun turret's skin and effects are openQ4's own work.
