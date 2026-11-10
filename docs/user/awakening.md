# Quake 4: The Awakening on openQ4

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

The Awakening is the Quake 4 expansion Raven Software and Ritual Entertainment
never released. Justin Marshall recovered and published its alpha. openQ4 plays
its single-player campaign from your copy of that content, using openQ4's own
engine and game code. It never loads the expansion's leaked game DLL, and
openQ4 ships none of the expansion's files. Where the alpha left something
unfinished, such as the chaingun turret's skin, openQ4 adds its own art
instead.

This page lists what openQ4 changes to make the campaign run, including a first
direction pass on the unfinished opening. To install the content and choose the campaign, see
[Campaigns](campaigns.md).

## At a glance

- All 13 campaign maps load and have gameplay/save probes, from the trenches of
  Stranarus to the Valkaryne's lair. Complete playthrough qualification remains
  in progress.
- Every new monster, weapon and vehicle is rebuilt in openQ4's game code: the
  Pain Lord, the Valkaryne, the Tank, the Retch and the walker; the goob,
  freeze and spike guns; the speeder bike and the dropship and MCC cannons.
- Where the alpha's own code or content stopped something working as designed,
  openQ4 fixes it. The Valkaryne finale, the beach's death-ray turret, the Pain
  Lord's weak spot and Retches that could never move are the biggest examples
  (see [Fixes to the alpha](#fixes-to-the-alpha)).
- The alpha is unfinished. Some sounds, effects, models and scripted voice-over
  are missing from the content itself (see [Known issues in the
  alpha](#known-issues-in-the-alpha)).
- The campaign works with openQ4's other features. VR seats you in the
  expansion's turrets and on its speeder bike.
- Multiplayer is not part of this integration. Multiplayer always runs on stock
  Quake 4 content.

## Running the campaign

What openQ4 changed so the expansion's content loads at all:

- **Campaign choice.** Single Player → Campaign offers The Awakening once
  openQ4 finds its content. Choosing it mounts the expansion (`q4xbase`) and
  restarts the filesystem, so its overrides never leak into Quake 4, Arena or
  multiplayer. Its saves and settings live apart, under `fs_savepath/q4xbase`.
  `fs_awakeningpath` points openQ4 at content kept outside the Quake 4 folder.
- **Retail definitions kept.** The expansion ships files with retail's names
  (`def/debris.def`, `def/ai/persona.def`, `sound/music.sndshd` and others)
  that would replace retail's whole file. openQ4 also reads the retail file
  underneath, so the 239 definitions they would hide survive: debris in death
  effects, marine chatter and the menu music.
- **New file formats.** Version 2 collision models (the expansion's model clip
  hulls), DXT3 textures, textures named `.jpg` that only exist as `.tga` (the
  Valkaryne's limbs), and music under `sound/music/`, which now follows the
  music volume.
- **Menus and controls.** The menu commands its interface uses
  (`GetCVarValue`, `SetCVarValue`, `GetVecCVarValue`) work, and its
  `_altattack` binding is the alternate-fire button.
- **Ten new script events** for its levels: docking the Valkaryne, the Pain
  Lord's processing machine, the speeder bike's speed, gravity and boost, and
  two control-point queries.
- **Saves.** The expansion's marines have up to 540 animations, more than the
  256 openQ4's saves allowed for retail's models (at most 244), so a game saved
  on the prison level (m04) never loaded. It does now, on every campaign map.
- **A quieter log.** Flares in the Valkaryne's lair (m09) are not the single
  quads the flare effect expects, which logged a warning hundreds of times a
  second; openQ4 now warns once per material.

Cryofac and the finale also receive mission-script corrections: cooling controls
cannot restart the cold damage when pressed repeatedly, squad and animation
handoffs complete, and the Valkaryne's sphere phase starts during her intro.
Restart m08 or m09 to use these changes; older saves keep their original scripts.

## What openQ4 rebuilt

| Element | Where | What it does |
|---|---|---|
| Fire vents (`func_fire_volume`) | m01, m01 part 2, m02, m03, m04 | Burn whoever touches them. Only the freeze gun puts them out, dimming their firelight as they shrink. |
| Walker | m01, m01 part 2, m06 invasion | A marine walker on your side; it fights the Strogg with its cannon and rockets. |
| Retch | m01 part 2, m02, m04, m05, m08 | Buffs a Strogg ally's damage or defence with a beam, fires lightning and dodges. |
| Tank | m02, m04, m08, m09 | Railgun, machine gun and shoulder rockets behind armour. Break its launcher to stop the rockets and its chest plate to reach its heart, which takes six times the damage. |
| Pain Lord | m05 | A boss who works a marine-processing machine. Two half-processed marines on his back shield him; he is stunned at two thirds and one third of his health, and enraged when the marines are torn off. If he finishes his third processing, Strauss dies and the mission fails. |
| Valkaryne | m08 (set piece), m09 | Docked on her platform she is untouchable and fires death-ray volleys; on foot she uses her railguns, grenades, rockets and claws. |
| Goob gun | m02 on | Its globs burn what they hit, and burst into six smaller globs. Its flamethrower mod adds a short-range alternate fire. |
| Freeze gun | player arsenal | Chills enemies until they slow, freezes them solid as they die, and shatters them. Ammo regeneration requires the regeneration powerup. |
| Spike gun | player arsenal | Spikes stick where they land, stay in the enemies they kill and pin the bodies to the wall behind. Its scope mod fires explosive spikes. |
| Turrets | m01, m01 part 2, m04, m05 | Tighten their aim the longer they fire, pause before re-firing, and security cameras sweep. |
| Speeder bike | m06 invasion, the three m07 races | Hover bike with a gun and a crouch-activated boost. |
| Dropship and MCC cannons | m03, m06 invasion | Pulse cannon that overheats, and a missile launcher whose rockets lock on and home. |
| Scripted targets | m03, m04, m06, m07, m09 | Set pieces name the only enemies an AI may fight (`onlyTarget`), so scripted battles stay on script. |

Rebuilding these went down to the details the levels rely on, matching the
alpha's own code:

- **Enemies.** Elite grunts pull you in with a gravity well, elite gunners
  spray their fire, and tactical elites throw grenades. The Tank scatters its
  rockets once its launcher smokes, plays its own break effects, and loses its
  rockets when either armoured part breaks. Space flyers (m03) follow their
  attack runs instead of turning to face you, climbing, diving and banking with
  them, and the fighters of m03 and m06 bank into their turns.
- **Weapons.** The goob gun bursts into its six globs and shows its
  flamethrower's flame, with your character holding the flamethrower's pose.
  Spikes knock bodies back towards the wall they pin them to, and keep their
  launch orientation in flight.
- **Freeze and support effects.** Frozen enemies resume their shatter countdown
  after loading. Freeze hits respect boss invulnerability and work on enemies
  with one health point. Thawing preserves the Pain Lord's enraged speed, and
  Retch buffs coexist with ice without stacking or erasing each other's effects.
- **Cockpit targeting.** Missile locks require a visible target in range. A
  distant enemy near the crosshair no longer blocks a valid closer target, and
  rockets and target markers follow the aim offsets of large enemies.
- **Vehicles.** The dropship and MCC cannons keep their gunner from harm,
  recentre when you leave, and freeze during m03's scripted ammo jam. Their
  pulse cannon's barrels kick back with every shot and fold out of the way while
  the missiles are selected; the cannon keeps cooling and the launcher keeps
  reloading while you use the other weapon. The speeder bike turns, grips and
  hovers as built: a held turn holds steady, the bike reaches racing speed, and
  it keeps itself off walls and tunnel ceilings. The race tracks' rough edges
  (m07) slow it, their damage going to its shield. The races are against the
  clock: if a lockdown gate closes before you get past it, the mission fails.
- **Displays.** The race clock counts down on the speeder bike's HUD (m07), the
  MCC's health shows on the MCC cannon's (m06), the bike's dashboard shows your
  speed, the dropship status panel's view cone turns with your view (m03), and
  the crosshair shows when a marine is too busy to talk. With the Remastered HUD
  the dropship guns (m03) show the ship-status panel under the radio display,
  with its damage, objective markers and attack warnings, and an Exit prompt;
  the same panel appears on your own HUD while you move about the dropship.
  The classic interface (`ui_retained 0`) lays your on-foot HUD out the same
  way: the gauges, the 13-weapon strip and the pickup lines sit where the
  modern HUD has them, the HUD Size, Opacity and layout settings apply, and
  the dropship panel shows the same damage, markers and warnings. The
  dropship guns, the MCC cannon and the speeder bike are laid out the same
  way there too, with the displays described here. The
  MCC cannon (m06) shows the MCC's hull under the radio display, its gauge
  filling with the hull and turning red at a quarter, with an Exit prompt.
  The speeder bike (m07) shows its race clock and checkpoints at the top, the
  clock turning red in the last ten seconds; its prompt names your crouch key
  while a boost is ready and warns while it fires, the screen's edges streak
  as you boost, and its gauges warn when the hull or shields run low.

## Fixes to the alpha

The alpha's own code or content kept these from working as designed. openQ4
fixes them:

- **Falcon defence (m03).** Both turrets show their own cannon heat, weapon
  selection and missile status, and keep their aiming reticle visible. The
  ship diagram agrees between guns and on foot. Repairs restore hull health
  and clear repaired sections; the engine repair completes once. Ship state
  survives switching guns and saving/loading.
  Missile volleys fire the loaded charge shown on the cockpit display, from
  one to five rockets; an empty magazine must reload before firing again.
- **The prison (m04).** Reinforcements use the correct upper or lower
  cellblock landing, the maintenance bot uses its own workstations, and the
  bridge ambush has a valid destination. Camera scanning uses its existing AI.
  The invalid starting goob-gun upgrade request is removed.
  Cellblock attackers switch to combat before reaching the marines' occupied
  cover positions.
  The security override completes once, keeping the alarm off and shutters in
  place. Destroying the turret-room camera during the ambush no longer starts
  the door hack early or twice.
  The camera's collision box now fits its hanging model, letting it appear
  and scan instead of remaining hidden against the cell door above it.
  Start m03/m04 afresh to receive these mission callback corrections; existing
  saves retain the mission threads with which they were created.
  The prison's security alarm now gives quieter, local warning pulses with
  pauses, and computer alarms are less intrusive. This audio improvement also
  applies to existing saves; the security console still switches the alert off.

- **Fire passages (m01, m02).** Freezing a vent clears its invisible collision
  and puts its light out fully. Burning vents allow passage while inflicting
  their authored touch damage. Saves restore the correct collision state.
- **Trianfac's generator defence (m02).** Repair bots attach to their own
  stations, and Tak waits for all six to be destroyed before completing the
  sabotage. The destroy-generator objective appears before the explosion and
  completes afterwards. The extraction dropship finishes landing before it
  idles, and defeating the Tank completes the Nexus objective. Restart m02 to
  adopt these script corrections; existing saves retain their original scripts.
- **The Valkaryne finale (m09).** Her docking script was never run, so she
  never took her platform: she wandered harmlessly and could be killed before
  the Makron sphere, skipping the fight built around it. She now docks behind
  the sphere's shields and fires death-ray volleys, with about two seconds of
  warning; the arena's pillars give cover. When the sphere dies she undocks and
  fights on foot, and her death starts the escape.
- **The beach's death-ray turret (m01 part 2)** is meant to bombard the
  landing, never you. It no longer turns on you after the turrets beside it,
  a sound or a hit draw its attention.
- **The Pain Lord's head.** His definition makes headshots count double, but
  listed his legs over the whole body, so in the alpha every headshot counted
  as a leg hit (three quarters damage). Headshots now count.
- **The rescue and MCC defense (m05/m06).** Heavy hits no longer skip the Pain
  Lord's shield stages, destroyed captives disappear consistently, and interrupting
  the final processing attempt buys time to save Strauss. A rescue objective
  tracks the fight and completes with victory. Mitchell no longer
  reappears in the MCC arrival. The cannon defense has a longer opening fighter
  wave, keeps its flight runs working, and stops monitoring the MCC's hull once
  the escape begins. Destroyed escape teleporters stop their reinforcements and
  rotation, and repeated triggers cannot restart them. The service-lift lamp
  travels with its platform, and the rescue teleport clears its destination
  before returning control. Idle followers give Tak room to reach you for armor
  repairs. The upper freight panel no longer presents a broken interaction;
  several lights, MCC monitors and character/effect textures now use their
  available assets. Fast command-room teleporter kills also cancel its delayed
  activation. Restart these maps to adopt corrected scripts and entity
  assignments; older saves keep their original mission flow and receive the
  AI improvements.
- **The Retch** asks for navigation data that two levels lack (Trianfac and
  Stranarus Trench part 2), so four of them could never move. They now walk.
- **The goob gun's burning effect** shows from the first hit; the alpha looked
  it up in the wrong place.
- **The Strogg chaingun turret you man (Stranarus Trench, the prison)** was
  never textured: the alpha gave it grey placeholder shading, so it showed as
  bare clay, with a dotted stand-in in its drum's windows. openQ4 paints it as
  a battle-worn Strogg emplacement: oxblood and field-grey armour plate,
  brushed-steel covers, oily gunmetal, chrome bands and brass bolts, edges
  rubbed through to bare metal, braided hoses with brass fittings and colour
  bands, a drum with brass rounds behind its windows, glowing vents and a red
  sensor lens.
  Firing it now has the weight of the game's other mounted guns: a heavier
  muzzle flash that lights the gun, tracers, heavier impacts, ejected casings
  and smoke from the muzzle after a long burst. Its servos whine as it turns,
  and its rounds now land under the crosshair: the alpha fired them parallel
  to your line of sight from a muzzle well to the side, so close targets were
  missed. The zoom button magnifies its view, as the machinegun's does.
  It also has its own HUD instead of the alpha's on-foot HUD with an empty
  ammunition gauge: your health and armor, since you stand exposed behind
  the gun, and its barrel heat (see the next section).
- **The machinegun hits as hard as in Quake 4 again.** The alpha's turret
  file set the turret's damage by redefining the damage the player's
  machinegun shares, so in the alpha your machinegun hit for 40 a round
  instead of 15, more than its zoomed shots (20), and lost its bleeding, flesh
  hit sounds and knockback. The machinegun is back to Quake 4's values; the
  turret keeps its own 40.

## Deliberate differences from the alpha

openQ4 departs from the alpha's behaviour in a few places, to improve unfinished
scenes, keep your settings or keep the game fair:

- **The opening dropship (m01).** The failed rescue now holds a view across
  the aisle, with room for Darnell's reach and Atkins' rising chair. The warning
  precedes the fall, Markswell's harness follows his sit, and Tak cuts the jammed
  harnesses while the squad exits. The final escape camera waits for the main
  column to clear. Engine, impact, alarm, wind and cutting cues accompany the
  lighting change and hull breach; the cabin mix fades out before the ground
  encounter. Existing voices and animations still provide the performances,
  so this remains a polish pass on unfinished alpha content. Reduced Flashing
  omits the added spark bursts. For comparison, set `g_awakeningM01Cinematic 0`
  before starting the map to use the alpha's direction. Restart m01 after
  updating the sequence: saves made with a different revision of its script
  may require a fresh map start.

- **The speeder races (m07).** Clocks, carried time and objectives now follow
  each checkpoint consistently. Finishing clears the countdown and fills the
  progress display, which stays full even if another progress trigger fires.
  Missing a gate stops driving and firing during the failure screen; late
  checkpoints cannot turn that failure into completion.
  The first lockdown keeps its radio warning before starting
  the clock. Bomber attacks clean up correctly, and two broken flyer destruction
  sequences now work. Shooting down a bomber stops its volley cleanly and
  ejects its pilot once. Restart the current race leg to use these changes;
  older saves keep their original mission scripts. Leaving or disabling
  the bike ends its boost while keeping the recharge time.

- **Your field of view is yours.** The alpha's m03 gun scripts and the speeder
  bike's boost rewrote your FOV setting. openQ4 applies those changes to the
  view only and never touches the setting.
- **Burns merge.** In the alpha one goob shot could light up to eight separate
  fires on one enemy, about 350 damage. Each enemy now carries one burn, which
  later hits feed up to three times its strength.
- **The Retch's fallback.** Where a level lacks the Retch's own navigation
  data, it uses the next size down, which its body fits.
- **The chaingun turret heats up.** The alpha's turret fired at full rate and
  full accuracy for as long as you held the trigger. Now about five seconds of
  continuous fire overheats it: it vents steam and will not fire for about
  two seconds, until it has cooled to 40%. A third of a second off the
  trigger starts it cooling, and it cools from full in about two seconds, so
  bursts never overheat it. Cold, it is tighter than the alpha's gun (the 2
  degrees its turret position asked for, which nothing read); as it heats it
  spreads to the alpha's 5 degrees, and zoomed rounds spread half as much.
  The HUD shows the heat on the gauge where your ammunition usually shows and
  as a bar under the crosshair, red from 75% and while the gun vents, with
  OVERHEATED. Its damage per round and rate of fire are unchanged, and the
  prison's scripted gunner never overheats.
- **An overheating pulse cannon says so.** The alpha's overheat signal reached
  none of the cannon's screens; openQ4 shows their JAMMED warning while it
  cools, and never clears the scripted ammo jam's warning early.
- **The ammo-jam lock and banking flight paths** apply only in The Awakening.
  Stock Quake 4 vehicles and flight paths are unchanged.
- **Multiplayer-only content stays out.** The vampiric gauntlet mod belongs to
  the expansion's multiplayer arsenal and has no effect in the campaign.

## Known issues in the alpha

These are holes in the expansion's content itself. The authored opening-scene
direction reuses installed media; apart from the chaingun turret's skin and
effects (see [Fixes to the alpha](#fixes-to-the-alpha)), openQ4 does not
supply missing performances or replacement art:

- Some voice-over never plays: thirteen scripted voice-over files are never
  loaded by the expansion's own scripts.
- About 100 sound references, a handful of in-map screens, effects, models and
  one teleport-dropper animation are missing from the content.
- m09's Valkaryne arena script shows and hides a force field and shield pieces
  the map does not contain, and the Tank's rocket flash names a joint its
  model lacks. Both only lose an effect. m09's frosty-breath effects name a
  joint only marines have, and two of its triggers call script functions the
  developers commented out or never wrote.
- The m08 and m09 developer give lists contain typos. m08's first-lab scientists
  call a script function (`FirstLabCleared`) the map never defines; nothing
  waits on it.
- The background harvester in the m07 races is retail's cinematic one, which
  has no turning animations. openQ4 no longer has it try to turn, which flooded
  the log and cost it its attacks.
- On the first race map (`m07_race1`) the navigation data for its scripted
  gunner and fighters is empty, so they stay where they are placed.
- The expansion's code also has a grapple hook, a concussion gun, a gravity-gun
  turret and a speeder-bike spline tether, but nothing in its campaign gives or
  places them, so openQ4 does not implement them. The objective compass is
  switched off in the expansion's own HUD.

## For modders and testers

The keys openQ4 honours in Awakening content (none appears in retail Quake 4,
so stock behaviour is unchanged):

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

A vehicle also passes script `guiEvent` and `setGuiParm` calls to its HUD as
well as its world screens, as the expansion's code did. The pulse cannon moves
its vehicle's `pulse_cannon_l` and `pulse_cannon_r` joints; a vehicle without
them fires the same, with still barrels.

openQ4's additions to Awakening content live in its own files and never
replace the expansion's. While The Awakening is running, any definition
lookup of `<name>` first tries `<name>_q4x_openq4`; openQ4's turret weapon
(`vehicle_chaingun_weapon_q4x_openq4`) inherits the expansion's, keeps its
damage and rate of fire and adds the effects, heat, zoom and convergence, and
`damage_machinegun_q4x_openq4` gives the player's machinegun Quake 4's damage
back. The turret's HUD is `guis/vehicles/turret_hud.gui`; the vehicle's HUD
reads the gun's `vehicle_heat`, `vehicle_overheat`, `vehicle_heatwarn` and
`vehicle_zoom`, and `g_debugTurretGun 1` prints its heat. Material files named `materials/*_openq4.mtr` that sort ahead of a
game's own file (`00_` names) supply the material instead of it; the turret's
skin works this way and keeps the expansion's normal maps. Any vehicle weapon
can now eject casings (`def_ejectBrass`, `joint_view_eject`, `ejectOffset`)
and trail `fx_burstSmoke` after a burst of `burstSmokeShots` or more.

Cheat commands and settings that help when testing the campaign without input:

- `openq4_testInput <ms> [attack] [zoom] [run] [crouch] [jump] [forward|back|right|left <0-127>] [aim <entity>] [yaw|pitch <deg/s>] [impulse <n>]`
  holds input on the local player for a stretch of game time, including a
  gunner's turret aim and weapon choice.
- `openq4_viewReport` prints your view, field of view and god mode.
- `ai_debugActions <name|*>` prints an AI's actions and attacks, and the
  Awakening's docking, buffs, armour breaks and freezing.
- `damage <entity> <amount> [damage def] [joint]` hits an entity, in a chosen
  joint's damage zone.
- `g_debugVehicle 1` reports the cannons' overheating and missile lock;
  `g_debugDamage 1` reports damage and burns.

The development record, with the evidence behind each change, is the
[support plan](../dev/archive/2026-10-18/plans/q4x-awakening.md); the original gap analysis is the
[support audit](../dev/q4x-awakening-support-audit.md).

## Credits

Quake 4: The Awakening was developed by Raven Software and Ritual
Entertainment. Justin Marshall recovered and published the expansion. openQ4's
support for it is written independently and includes none of its content; the
chaingun turret's skin and effects are openQ4's own work.
