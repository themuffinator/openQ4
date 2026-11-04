# Awakening mission audit

This is supporting evidence for **Q4-04** in [the roadmap](roadmap.md), not a
separate development plan. Reviewed 4 November 2026 against the installed alpha
maps, their entity connections, scripts, actor definitions and openQ4's SP code.
The evidence distinguishes authored intent from completion claims.

## Intended progression

| Map | Intent inferred from scripts and placed entities | Important dependency |
| --- | --- | --- |
| `m01_stranarus_trench1` | Establish the squad during the failed insertion, regroup under fire, enter the facility and teach the freeze gun as an environmental tool. | Surviving squadmates operate doors and lead the route; extinguishing fires must make the passage usable. |
| `m01_stranarus_trench2` | Reach the damaged walkers, defend the repair, cover Navajas placing charges, then flank the beach turrets and rejoin the squad. | The player walker remains bound during repair and exit-locked through the turret attack. The authored unbind and later unlock are separate milestones. |
| `m02_trianfac` | Rescue Razor's survivors, obtain the spike gun, sabotage the generator despite its repair bots, defend Tak's download, reach the Nexus, fight the Tank and extract the sample. | Generator defence, objective updates and the squad's console actions must agree. The extraction animation must finish before the dropship idles. |

These conclusions follow the named objectives, dialogue calls, six repair bots,
their death counter, Retch callback, walker callbacks and end-level links. They
do not imply that missing voices, abandoned entities or unused systems should
be invented or re-enabled.

## Corrections

- **Fire collision:** `riFireFX` forced `CONTENTS_SOLID` on a volume whose
  definition explicitly requires projectile-only `common/shot_clip`. This
  obstructed players and squadmates even after freezing the fire. Active fires
  now retain projectile and touch collision; disabled and quenched fires clear
  it. Restore reapplies the correct contents, including for older saves.
- **Firelight:** the final freeze hit could overshoot the remaining health and
  leave a positive light radius because negative results were discarded.
  Damage contribution is capped at remaining health and the radius is clamped
  without per-hit integer rounding. Shared lights retain their `fireCount`
  contribution. Each stage also reapplies its authored `fxtarget` direction.
- **M02 repair bots:** all six threads attempted to bind the nonexistent
  `repbot_1`. Each now binds its own bot to its own repair station.
- **M02 generator defence:** the six-bot death counter points at deleted
  `trigger_relay_79`, while Retch death alone called the shutdown routine.
  The shutdown now waits until all six repair bots are dead as well. This
  implements the squad's instruction to stop the repairs and prevents bypassing
  the defence by killing only the Retch. Either kill order is supported; the
  waiting script thread is ordinary saved state.
- **M02 objectives:** the destroy-generator objective begins on arrival at the
  generator encounter and completes after the explosion. Previously it appeared
  only after the generators had already exploded. The Nexus objective completes
  on victory over the Tank. Existing localized/content-owned objective entities
  supply the wording.
- **M02 presentation:** Razor's accelerated struggle finishes before its rate
  resets, and the extraction dropship finishes `hangar_land` before entering
  `hangar_idle`. The download's fourth second-wave death starts its completion
  once rather than every time its counter is at least four.

## Script identity and asset ownership

The original expansion remains external. `MissionScripts.inl` applies bounded
corrections to the installed M02 script in memory; it does not install a copy or
replace the user's file. Every edit must match once, or a fresh map retains its
installed behaviour with a warning. The reviewed installed script has SHA-256
`52f91083b62daf55c64cce1ced7b61a502b7748110a96b8a9ce3eccdd50746fa`.

Fresh corrected maps compile as
`maps/game/m02_trianfac.openq4-v1.script`. Saves record that virtual source name,
so restore reconstructs the same corrected program. Older saves still compile
`maps/game/m02_trianfac.script` unchanged. Restart M02 to adopt its script
corrections; do not reinterpret an old suspended script with new statements.
Keep the v1 transformations stable after release; a later revision needs a new
identity. No save payload layout changed.

Design and entity references are from Raven Software and Ritual Entertainment's
Awakening alpha, recovered by Justin Marshall. See the existing
[upstream credits](../../AGENTS.md#upstream-credits) and
[SDK licensing](../../LICENSING.md). These changes use the player's installed
assets and newly authored implementation; recovered maps, performances and
scripts are not added to the repository.

## Verification

`tools/tests/awakening_missions_runtime.py` launches only the staged SP client,
hidden and windowed, with isolated `.tmp` profiles. `testMission` inspects live
entity/animation/light/objective state and can start an existing zero-argument
script function without compiling a console script into the save. The tests use
engine damage, authored scene functions and engine screenshots; they never send
mouse/keyboard input or capture the desktop.

Windows x64 `debugoptimized` build and staging passed. The gameplay contract,
Awakening CI contract, 98 status-behaviour checks and repository path check
passed. The installed M02 source hash remained unchanged.

Runtime evidence is kept under `.tmp/awakening-m01-m02/`: per-run command and
binary hashes, reports, engine logs, saves and screenshots. These are focused
encounter and restore checks, not an uninterrupted playthrough or a
combat-balance assessment. No Linux or macOS runtime result is claimed.

| Evidence directory | Result |
| --- | --- |
| `fire-m01`, `fire-m02` | Both maps: ordinary damage leaves fire health unchanged; freeze damage reduces 75 to 35, then quenches it. Active contents are projectile/touch only; quenched contents and light radius are zero. |
| `fire-partial-restored` | Partly frozen M02 vent restores at 35 health, retains projectile/touch contents and extinguishes fully. |
| `fire-out-vulkan` | Quenched M02 vent restores and resaves on Vulkan with zero contents and light. |
| `legacy-write`, `legacy-load`, `fire-legacy-restored` | A save made before these changes loads and resaves with its original M02 program. Legacy solid fire contents are repaired on restore, and the vent can still be quenched. |
| `repair-controlled`, `repair-restored` | All six bots reach and bind their own stations. A suspended shutdown callback waits while they live, survives a fresh-process restore, then finishes after their deaths. All six saved positions, bindings and health values match on restore; the generator objective remains active until shutdown. |
| `objective`, `nexus` | The generator encounter issues its objective; the Tank victory callback completes the Nexus objective. |
| `landing-verified` | The dropship plays `hangar_land` before `hangar_idle`. This checks sequencing, not the final cinematic framing. |
| `walker`, `squad-verified` | M01 part 2 walker entry yields one driver in seat 0 with the repair exit lock, and saves. Killing Tak already fails the mission and prevents saving. |

After landing in the main checkout, the client and both game modules were
rebuilt and staged. `repair-main` repeats the suspended generator restore and
completion on that merged build; `fire-legacy-main` restores the original M02
save and quenches its fire on Vulkan. Both pass. `suite.json` indexes all 16
accepted runs. Disposable helpers, duplicate save inputs and superseded captures
were removed; regression saves, logs and the reusable asset cache were retained.

The repair probe disables squad attacks so they cannot kill the bots before
inspection. It calls the existing shutdown callback directly, then kills the
bots through normal damage; it does not qualify the natural Retch fight.
The third station is projected 16 units down onto its AAS area, so the probe
allows that vertical projection plus the authored eight-unit arrival range.

An initial restore probe caught Windows backslash spelling of the saved virtual
script name. Both lookup paths now normalize separators, and corrected-script
restores pass. Superseded probe reports retain their original failures:
uncontrolled squad attacks killed the bots early, a preliminary geometric check
omitted the AAS projection, and an initial squad check expected a redundant
failure handler. Their replacements above are the acceptance evidence.

## Remaining alpha findings

- The empty M01 mission-critical death callback is not, by itself, a missing
  failure handler. Killing Tak in part 2 already fails the mission and refuses
  a save. The actor objective system handles failure independently of that
  callback; no additional fallback was retained.
- Both M01 maps retain dangling decorative, light and spawner targets. Most
  affected spawners also have valid locations; removing their missing slots or
  guessing replacement locations could change encounters. The audit did not
  treat a missing target as sufficient evidence to invent its replacement.
- M02 retains the dead repair-counter relay reference and other abandoned
  targets, missing media and pickups embedded in geometry. The generator no
  longer depends on that relay. These content warnings are recorded in the
  runtime reports rather than hidden by the fixes.
- Squad movement can still report unreachable goals or the existing scripted
  movement timeout. Focused positioning is not proof of a traversable full route.
- Complete M01 walker defence/demolition/traversal, M02 squad routing, defended
  download, Tank combat, extraction framing and mission-to-mission carryover
  remain Q4-04 acceptance work until exercised end to end. The earlier guide's
  assertion that all maps had been played through exceeded the evidence.

## Workspace findings

The worktree `E:/Repositories/openQ4/.claude/worktrees/ui-roadmap-work-aaa011`
on `claude/ui-right-stick` was clean and fully merged at inspection (last commit
3 November 2026). It was removed separately during the M08/M09 audit; this task
did not remove it. `E:/Repositories/openQ4-worktrees/e07a/openQ4`
initially appeared stale at detached commit `908b2c012`, but the final check found
active work on `codex/q4-legacy-event-data` with uncommitted changes; it is no
longer a cleanup candidate. Other worktrees had unmerged work or active changes
and were preserved. The final M08/M09 inspection found
`E:/Repositories/openQ4-worktrees/e931/openQ4` clean, detached at `85c61fe16`,
and fully merged into the main checkout's branch. It is a cleanup candidate
after checking whether its owner still needs it and obtaining the user's
required worktree-removal approval.

## M08 Cryofac and M09 Valkaryne — 4 November 2026

### Intent and evidence

This review reads the user-supplied `m08_cryofac.map/.script`,
`m09_valkaryne.map`, `scripts/maps/m09_valkaryne.script`, actor definitions and
animation metadata. The recovered content remains external. Corrections are
independently authored, applied in memory by `LateMissions.inl`, and covered by
the game library's SDK EULA. Original design/content credit remains with
Raven/Ritual and [Justin Marshall's recovery](https://github.com/jmarshall23).

M08 teaches cold as both obstruction and threat: thaw the entrance, follow Tak
through controlled doors and a shootable stalactite, then defeat the Tank guarding
the descent. Below, the other fireteam dies in the freezing chamber and the player
must shut down the leaking cryogenic system.
Tak's final encounter transfers the surviving escort role to Navajas. This is
inferred from the door/console target chains, dialogue and named animation;
it is not a claim to have the designers' original specification.

M09 escalates that pursuit through two descending Makron-egg transports and
Navajas's death, then factory machinery and a two-stage boss arena. The rotating
Makron-sphere shields and rising cold impose pressure while the docked Valkaryne
attacks. Destroying the sphere releases her; killing her opens the timed escape,
extraction and Patton ending. The abandoned resurrection scene is commented out;
this review does not invent a new ending or reactivate it.

### Corrections

- M08's first thaw moves `navajas`, correcting two `navahas` references. Its
  flashlight trigger resolves the existing `FlashLightsOff` callback, and an
  unlocked cryodoor frame displays the same enabled state as other doors.
- Cooling requests are latched once and wait for the authored window breach.
  Repeated or early button presses cannot stack fog movement or toggle the
  cold damage back on. The pending request is saved with the script globals.
- The final Valkaryne stays until `cr_kill_tak` finishes. The recovered script
  removed her, and consequently her bound victim, about two seconds into an
  animation with 201 frames at 24 Hz. This short-lived actor remains awake for
  the attack so leaving its visibility area cannot stall the completion wait.
- M09's repair bots execute their intended five repair cycles. The recovered
  `for` statements put the increment in the condition and never enter the body.
- Navajas's death scene moves the newly activated `navajas03`, rather than the
  already removed `navajas02`.
- Each light-taunt loop addresses existing lights and restores the same set it
  extinguished: 1–63, 1–14 and 1–10. The old loops included zero, skipped half
  the first room and restored nonexistent lights 15/11 in the other rooms.
- The boss intro explicitly docks the Valkaryne before presenting the arena.
  Her game class rejects damage before docking and through undocking, consumes
  the one-time docking request on release, and ignores a late docking event.
  Invulnerable hits cannot interrupt docked volleys with a pain reaction; the
  sphere's authored scripted reaction still runs.
  Script callbacks must be zero-argument functions. The sphere's rotating shell
  stops through its actual owner, and the finale's hex-spawning loop ends after
  victory rather than running throughout extraction. Calls to the optional,
  removed forcefield and the already-collected escort objective are guarded.
- Missing extraction now fails the active escape objective instead of leaving
  a permanent black screen. The dropship waits the thirty seconds promised by
  the radio, departing before the forty-five-second doors finish closing.
  Departure reserves the outcome so a late boarding trigger cannot start a
  competing success sequence. The existing, otherwise unused failure item
  takes the existing escape objective's title before pickup removes the
  objective entity, with no new display text.

### Save and content boundaries

Fresh M08 starts use `maps/game/m08_cryofac.openq4-v2.script`. Fresh M09 starts
compile `maps/game/m09_valkaryne.openq4-v3.script` into `oq4_m09_v3`, redirecting
the map's existing callbacks and docking request. The original `map_m09`
program loaded by `main.script` remains untouched. Old saves retain original
script statement indices; new saves recompile the virtual sources. M09's
custom map sidecars take precedence. All source anchors must match their
expected counts, otherwise fresh starts retain installed behavior. Do not edit
the published v1 transformations or retained v2 transformations; their virtual
names remain reconstructible. M09 v3 extends the traversal corrections below
without altering the older statement layouts.

### Validation and remaining gaps

`tools/tests/awakening_finale_runtime.py` exercises hidden, windowed SP runs,
normal damage, existing script callbacks, engine screenshots and fresh-process
save restore. It never sends keyboard/mouse input or captures the desktop.
Windows x64 `debugoptimized` compilation/staging and the Awakening gameplay,
CI and repository-path contracts pass. Evidence lives under `.tmp/m08-m09/`;
each listed run retains its command, binary SHA-256 identity, report and
`q4xbase/logs/savegame-cross-load.log`. `source-anchors.json` records the external
source hashes and the checked replacement counts. These are local development
checks, not a final-package or complete campaign certification.

| Focused check | Passing evidence directories |
| --- | --- |
| First thaw reaches unlocked cryodoor 6 | `thaw-gl` |
| Early/repeated cooling request, exit unlock, pending-request restore | `cooling-accept-gl`, `cooling-accept-restored-vk` |
| Full 8.334-second Tak attack, cleanup, saved-scene restore | `tak-qualified-gl`, `tak-accept-restored-vk` |
| All three taunts restore their original light colors | `lights-gl` |
| Navajas death hands off to the descending attacker | `navajas-gl` |
| Early/docked immunity, uninterrupted docked attack, sphere break, mobile damage and escape; docked-save restore | `boss-accept-gl`, `boss-accept-restored-vk` |
| Save before the thirty-second departure deadline, reject late boarding, fail mission and block saves; departure restore | `extraction-accept-gl`, `extraction-accept-restored-vk` |
| Original-program M08/M09 saves survive fresh-process restore with corrections enabled | `legacy-m08-gl`, `legacy-m08-restored-vk`, `legacy-m09-gl`, `legacy-m09-restored-vk` |

The boss's engine captures were inspected in both phases. The Tak probe reads
the animation's actual start/end times and requires it to remain present near
the end of the clip before removal. Cooling checks both the fog position and
the unlocked exit, rather than merely accepting a stationary fog mover. The
probes retain the known `coldInHere` missing-host warning, and reject other
null-entity, unsupported-event or terminated-script failures.

The original-program saves were made with `g_awakeningLateMissions 0` on this
build; restoration uses the default enabled setting. Their saved and resaved
payloads retain the original script identity, while corrected scene saves
contain the virtual v1 name. This checks script-program compatibility, not
every historical binary/save format or cross-platform restore combination.

The following recovered-content gaps remain distinct from verified code fixes:

- Abandoned callbacks are removed only from their exact recovered map entities,
  only with a recognized corrected program and only when the function is absent.
  This covers M08 `RemoveDoorIce3`, `ValkaryneEscape`, `VO_9_5_1` and scientist
  `FirstLabCleared`, plus M09 `spawnTactical02`/`spawnTrashCan01`. Surviving
  trigger targets remain intact. No replacement encounter or dialogue is invented.
  Deleted decorative targets remain a recovered-content limitation.
- M09 revision 2 binds cold breath to its actor, retaining the authored offset,
  instead of the nonexistent `legs_channel` joint. It attaches all 29 surviving
  hosts and removes seven orphan effects. The original 36-pair source structure
  is validated before transformation. Optional removed fog/glass steam effects
  and two absent extraction sound entities are guarded rather than dispatched.
  Missing media/navigation warnings remain.
- Complete physical traversal, normal weapon balance, the cold hazard
  sequence, escape timing, extraction framing/audio, and cross-platform runtime
  qualification remain Q4-04 gates. Focused callbacks and forced damage cannot
  establish those results.
- The Vulkan boss restore reported an unrelated renderer fallback for light
  498 (`lights/round`): a required shadow resource was unavailable and its
  receivers rendered without that shadow. This audit does not qualify Vulkan
  shadow parity or change the renderer.

### Continued traversal investigation

Revision 2 tightens both marines' first-thaw arrival tolerance from 32 to 8
units. Repeated normal-input attempts showed the old early stopping positions
left less than a player-width gap in the entrance throat. A fresh v2 run crossed
after melting the ice with normal goo ammunition. The lower CoreMonitor scene
now sends Navajas to position 28 beside Tak, rather than position 3 upstairs; a
focused scene probe and normal traversal confirm the lower-floor arrival and
door unlock.

Local evidence is under `.tmp/m08-m09-traversal/`. Current revision-2 checks
pass in `cold-final`, `cold-final-restored`, `entrance-final`, `boss-final`,
`boss-final-restored`, `extraction-success`, `extraction-success-restored` and
`extraction-missed-final`. Both maps pass corrected and original callback checks
in `callbacks-m08`, `callbacks-m08-baseline`, `callbacks-m09` and
`callbacks-m09-baseline`. `v1-m08-final` and `v1-m09-final` restore the previously
qualified, committed v1 saves. `core-monitor-local` also passes the lower-floor
scene check. These remain focused probes: the successful-extraction setup forces
boarding and does not establish physical access to the dropship. Failed
exploratory runs are retained separately and do not constitute qualification.

`awakening_route_runtime.py` records bounded, explicitly authorized engine test
input, request acknowledgements, physical navigation, inventory and binary
identity. Its report deliberately never certifies traversal or balance by itself.
The M08 route uses skill 1, no god/notarget, normal collision and finite
ammunition. Because no qualified M07 carryover save was available, the authored
M08 developer loadout was granted once at entry, then developer mode was disabled.
That loadout also grants weapon upgrades, including the freeze gun's authored
zero-ammunition-cost upgrade. This is an upper-bound inventory investigation,
not representative campaign balance. The successful route uses finite ammunition
for its fired weapons; exploratory freeze-gun combat is excluded from that route.
It has physically crossed the thawed entrance, squad doors, cave corridors and
shootable stalactite, cleared Navajas's demolition/tank encounter and approach
guards, descended the lift with both squadmates, completed the lower console and
first-freeze scenes, and operated the cooling GUI after the window breach.
Cooling stops through the authored button and opens the exit without a forced
callback. The successful approach leaves 79 HP/55 armour; the subsequent health
pickup restores 100 HP. Tak's normal interaction restores armour to 100 before
the final six-elite wave. Firing retreats and cover clear that wave with 88
HP/58 armour. Its rail ammunition drops replenish the player from one round to
eleven before the final tank. Head-aimed rail fire during a retreat defeats the
tank with the same health/armour and seven rounds left.

The full M08 route now reaches the final squad scene, clears the raised ladder
rim with a normal jump, descends the shaft, and naturally changes to M09. The
handoff preserves 88 HP/58 armour and all observed ammunition. Evidence spans
`m08-clearance-route`, `m08-head-route`, `m08-lower-route`, `m08-final-route` and
`m08-arena-route`, including restored checkpoints and failed attempts. This is
physical-route evidence with the inventory limitation above, not a deathless
playthrough or representative M07-to-M09 campaign balance certification.
The normal player restore path also applies the authored skill-specific minimum
health (`minRestoreHealth1` is 10 in this setup). Low-health checkpoint restores
therefore cannot establish uninterrupted survival; the v3 replay explicitly
records those gains and remains a restored-checkpoint traversal investigation.

The tank's authored head multiplier is 6 and its leg multiplier 0.05. Automated
centre-mass firing therefore cannot establish that this encounter is over-tuned.
Two accessible health pickups before the fight restore the observed 67 HP to 100;
head-aimed normal rail fire clears the tank, leaving 90 HP and a natural Tak
armour refill. The subsequent guards leave 62 HP/82 armour before the lift.
M09 continues from the natural handoff. The first hover tank can be defeated
with head-aimed rail fire, leaving 74 HP/9 armour. The first shaft is a platform
route: clearing the upper railing reaches a narrow intermediate beam, and a
running jump reaches the west wall ledge with 55 HP. Walking through its combat
trigger, descending the southwest supports, and making a second running jump
reaches the middle walkway. These routes use normal collision and input; no
geometry edits or actor teleports were needed. The local `m09-collision-route`
profile retains the checkpoints and both successful and failed attempts.

The current `m09-verified-route` profile clears the first shaft's remaining
grunt/convoy pair, sentry and Iron Maiden, then physically exits into the cave.
Its inner pipe jump crosses `trigger_once_77`, sending the Valkaryne away;
the outer catwalk can bypass this authored retreat trigger. Following the inner
pipe around the support reaches the exit with 12 HP, and the health pack restores
62. No attack-stat or geometry changes were needed. The cave's first three
enemies are cleared with 49 HP remaining; an Iron Maiden's phase-out is not
treated as death. The south ledge reaches the next health pack without fall
damage, restoring 99 HP. Prioritizing the second Maiden, then changing to the
shotgun while retreating from the Berserker, leaves 44 HP. Standing still during
the diagnostic weapon change had unnecessarily lost another 28 HP. The driver
now supports movement during verified combat weapon changes. The second-shaft
egg transfer and Navajas death scene also run through their natural trigger;
the entrance's raised floor edge is crossed with a normal jump.

The second shaft's normal-input route now reaches the floor and the next cave
in `m09-docked-route`. It uses the machinery beside the northeast rim, the north
pipe, west machinery, curved southwest pipe and lower inner walkway. The east
lower walkway requires passing inside the decorative machinery; walking at its
outer edge blocks progress. Health packs support the descent. The bottom grunt
is defeated with 73 HP remaining; the following natural pickups provide 50
armour and five rail rounds. Head-aimed rail fire defeats the cave tank with
70 HP/40 armour and one rail round. This route predates the restored Maiden
encounter below, so its inventory is not a qualification of current M09 balance.

M09 v3 adds three progression corrections. It completes the escort objective
when Navajas announces the bomb is ready, repairs the second shaft's two
mistyped Maiden/casket targets, and waits for the far stair landing before
collapsing the cave stairway. The original collapse removes a 128-unit section
with a 64-unit rise before the player can cross; the normal jump height is 48.
The resulting fall enters a structural void instead of reaching the crane
controls. `stairs-current` and its fresh-process Vulkan restore both cross with
normal input and confirm that the collapse happens afterward. These focused
probes use forced scene setup and god mode, so they validate clearance and save
behavior only. The first probe's incorrect eye-height setup is retained as a
failed test, not a traversal result.

The docked Valkaryne also drifted off its eastern perch under gravity. Docking
now suspends gravity, clears falling velocity, and preserves the previous
policy for undocking, including saved games. Four scripted perches, a restored
perch, and mobile boss release pass focused checks. Escort completion and the
restored Maiden encounter also pass fresh/restored probes.

The fresh v3 replay exposed a qualification gap in the earlier second-shaft
route: its source save predated the gravity correction. With the attacker on
its proper perch, the north pipe became lethal even while running. The recovered
beam locks its aim at frame 70 and fires at 96, with animation rate 2: about
half a second to escape a ten-degree spread at the campaign's 160-unit run
speed. The native docked attack now commits its target at windup start. Its
damage, spread and burst remain unchanged. The same physical pipe route then
crosses with no beam damage and reaches the west health pickup. A standing
control still dies from the original burst; restoring a checkpoint during the
windup preserves the safe moving route. Evidence is in `m09-beam-route`.
Enemy aim lock and target positions already serialize through the existing save
layout.

The fresh v3 replay from the physical M08 exit reaches the second shaft's
floor with 100 HP after the beam correction. Its restored Maiden and grunt
encounter is now active and both enemies are defeated with finite ammunition;
the winning checkpoint has 27 HP before recovery pickups. Rocket impacts at
the narrow exit jamb caused avoidable self-damage, so those shots do not justify
changing enemy health. Natural recovery reaches 77 HP/50 armour with five rail
rounds. The cave tank takes four rail shots, leaving 74 HP/40 armour. The real
route then crosses the corrected stairs, activates the far-landing marine and
observes collapse behind the player. The crane door works when approached
through its frame's centre; a diagonal AAS suggestion hits the frame. The
crane controls raise the pipe through the actual terminal interaction. Returning
through the door activates the marine, scientist and gunner ambush; the winning
finite-ammo route clears them with 29 HP. Five more points are lost approaching
the pipe. The platform's northern lip, reached by walking around its diagonal
railing, supports the normal running jump onto the pipe. Jumping directly from
behind that railing misses the pipe and is excluded from clearance conclusions.
The western landing also requires jumping north onto the platform, rather than
diagonally into its machinery. Both crossings retain 24 HP. The next gunner is
defeated while strafing, without further damage. Evidence and checkpoint ancestry
are retained in `m09-crane-route`. The factory continuation is recorded below;
the finale and extraction are recorded below. Representative campaign
balance remains a separate qualification limit.

The continuing route reaches the Nexus health pickup at 74 HP. Its railgun
Strogg is intentionally passive during the destroyed-command-stream scene;
the next gunner attacks it. The gunner can be defeated without further player
damage. Both actual controls of `elevator5` work: the upper terminal calls the
platform, and its onboard GUI takes the player to the lower walkway. The
scientist/Maiden trigger runs, including opening the casket. The Berserker,
lower gunner and Maiden are defeated with 39 HP remaining. Maiden phase-outs
are explicitly distinguished from death. The lower rail and shotgun pickups
are reachable, and the next passage naturally awards the Dark Matter Gun with
ten rounds. The destructible cave ice responds to ordinary blaster fire.
These are checkpointed segments, not an uninterrupted survival run.

In `m09-factory-current`, reloading behind cover before breaking the ice keeps
39 HP through the jumping-marine encounter. The winding cave is traversable
despite gaps in its navigation areas; its next marine leaves 27 HP. The ledge
descent into the machinery chamber costs nine fall-damage points. Strogg
infighting reduces the Gladiator before one Dark Matter shot defeats it. Both
machinery terminals operate through their actual GUI surfaces, and their two
relays unlock the lift control. The call button raises the platform and starts
the timed defence. The surviving route defeats its three marines and two
Maidens, then boards the arrived lift and operates its onboard descent control.
The first defence wave ends at 6 HP, so the following restore applies the
documented 10-HP minimum. The later wave adds no further damage. This does not
qualify uninterrupted survival or representative campaign balance. The stale
light-tank target on `trigger_once_19` has no corresponding recovered entity;
no replacement tank has been invented.

The descent lift reaches the finale supply area. Its health, armour and
ammunition pickups are physically reachable, restoring 100 HP/100 armour. The
normal entrance triggers play the boss introduction and dock the Valkaryne.
Nailgun fire through the rotating shield openings destroys the 1,000-HP core;
the final core checkpoint retains 99 HP/99 armour, and the boss undocks with
4,000 HP. Earlier Dark Matter shots against the shields caused no core damage.
Attempts trapped against the seat or pillars, empty-magazine firing and
close explosive shots do not support a balance adjustment. `m09-factory-current` closed with 323 requests, no duplicate
dispatches, a clean process exit and unchanged runtime binaries.

Combat review found an unchecked AI cast in the shared melee damage path: it
read `aifl.meleeSuperhero` from player targets. The defender modifier now requires
an actual AI target. This removes undefined access without changing the authored
melee damage or the modifier for AI-versus-AI combat. Continued boss validation
uses the rebuilt runtime in `m09-boss-current`.

The final `m09-boss-current` route defeats the mobile Valkaryne with normal
weapon fire and 25 HP remaining. The winning branch uses the story-supported
freeze upgrade to slow and damage the boss, then Dark Matter fire while moving
out of close range. It retains two Dark Matter rounds, 134 nails, ten rail rounds,
22 rockets, 45 shells and 46 hyperblaster cells. Boss health, damage and attack
rates were not reduced. This is a checkpointed combat result, not an uninterrupted
campaign run or a qualification of the developer loadout's other upgrades.

Victory naturally opens the exit beneath the seat. The ramp and all three
tunnel sections are traversed before their damage timers, retaining 25 HP. The
actual evacuation-lift GUI starts the ascent. Normal jumps across the upper
supports and railing reach the ring walkway before the lift falls; the first
drop costs nine HP. The east ladder reaches the upper ring, the perimeter route
reaches the ship, and a running jump naturally consumes the boarding trigger
and binds the player to the departing mover at 16 HP. The thirty-second boarding
window is sufficient for this route. Restoring that boarding checkpoint runs
the Patton ending through the authored end-of-game target (`g_nightmare=1`).
No forced victory callback, god mode, noclip, teleport or replenishment was used
in these boss/escape segments. Normal scripted cinematic relocation is retained.

The recovered map has none of the three optional blast-door control panels
named by its script, and no activation chains for those doors. The radio still
asks the player to close them. The successful route leaves them open and beats
the shorter explosion timers; optional closure and its delay bonuses remain
unimplemented recovered content. Testing was wrapped up at the user's request
after physical extraction and the ending succeeded; no replacement panels were
invented. The final session closed cleanly after 85 requests, with no duplicate
dispatches, script errors or runtime changes. Local evidence and the final
`validation-summary.json` remain under `.tmp/m08-m09-traversal/`; disposable
geometry/replay/combat helpers and a superseded plot were removed. Missing media and the navigation/geometry mismatches below remain
separate content-quality findings.

The recovered M06 `m06b::giveGun2()` story handoff triggers `goodies`, whose
`item1` is `weaponmod_freezegun_ammo`. That upgrade sets `ammoRequired` to zero.
It is therefore an authored late-campaign capability, even though the developer
loadout also grants it. Freeze-gun trials are permitted when assessing intended
combat options, but have not been substituted into the successful finite-ammo
route above. This source finding does not qualify the remaining developer-granted
upgrades or replace an independently traversed M07 carryover.

This investigation also exposed a test-input defect: the transient impulse XOR
survived in-process restores while the player's sequence flag reset. It could
select weapon 0 on load and swallow a subsequent selection. Player initialization
and restore now clear the transient test state. The route harness verifies
weapon selection before combat and provides `--check-input-restore` to exercise
repeated reloads. Earlier M09 firing attempts with mismatched weapon/inventory
observations are excluded from combat-balance conclusions. Short selection pulses
were also lengthened to survive slow frames; movement keeps aiming at its
non-solid waypoint until the burst ends.

Selection checks now also wait for the weapon to finish raising, preserving
requested evasive movement and stopping if the player dies. Magazine, capacity,
available ammunition and readiness diagnostics distinguish a short firing test
from an actual reload failure. The cave checkpoint's empty machinegun reloads
normally once raised; its premature reload input did not establish a game bug.

The recovered M09 PROC carries geometry CRC 4046346523 while its CM and AAS32
carry 238919626 (M08's PROC and CM agree). This is an asset-generation mismatch,
not yet proof of an obstruction or a reason to replace the supplied geometry.
The physical M09 route now reaches the ending, subject to the checkpoint and
loadout limits above. Console `script` probes must not produce qualification saves: their temporary `console`
program is not available on restore. Route aim uses the test input joint option
instead.
