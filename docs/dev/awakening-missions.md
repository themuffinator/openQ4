# Awakening mission audits

This is supporting evidence for **Q4-04** in [the roadmap](roadmap.md), not a
separate development plan. Reviewed 4 November 2026 against the installed alpha
maps, their entity connections, scripts, actor definitions and openQ4's SP code.
The evidence distinguishes authored intent from completion claims.

## M03 and M04 review, 4 November 2026

M03 alternates defending the Falcon with repairing its systems: five fighter
waves, crew instructions, damage-control objectives and arrival at the prison.
The cockpit's instruments describe two different things: the ship diagram
tracks the Falcon's hull and repair objectives; the three physical screens
track the selected gun, cannon heat and missile lock/reload state. The turrets
themselves are invulnerable. Giving their hull gauge the ship's health, or
displaying a second player status bar, would misrepresent that design.

M04 moves from infiltration and camera/security encounters to defending
Darnell's bridge hack, evacuating Fox Squad from two cellblock landings, the
Strauss capture and extraction toward M05. Reinforcement destinations and
encounter activation are part of that choreography, not decorative markers.

### Corrections

- Both M03 cockpit screens had shared GUI instances. The idle turret could
  overwrite the active one's heat, weapon selection and rocket status. Each
  cockpit now owns unique, normally serialized screens. Event-only screen
  timers start paused, preventing every lock state from running on the first
  draw and ending at a false **LOCKED** indication.
- The cockpit keeps its combat reticle even when a cinematic or nearby crew
  member selects the on-foot interaction cursor. Default, acquiring/locked,
  friendly and out-of-range reticles have editable vector coverage; weapon
  targeting still supplies their colours and materials.
- A follow-up review found that the five rocket indicators did not control
  ammunition: a full charge fired one rocket, while an empty magazine could
  fire after the shorter weapon cooldown. The launcher now fires the displayed
  charge, from one to five rockets, refuses empty/acquiring shots, and catches
  up reload timing after a long frame. The existing multi-shot spread and sound
  support the charged-volley interpretation. This is inferred from the authored
  magazine and weapon code, not a recovered design specification.
- M03 damage, repair markers and directional warnings reach both turret HUDs
  and the player's HUD. Lasting damage/marker state is stored in the ordinary
  GUI dictionary, including for a view not drawn before saving. The on-foot
  Remastered panel honours the mission's explicit show/hide events.
- Repair credits now replenish the actual Falcon hull, capped at its initial
  health, and are consumed once. Previously they only increased a failure
  allowance while the mover stopped accepting damage at zero health. Repaired
  diagram sections clear and rearm, including when the original health monitor
  resumes from dialogue with a stale sample. Repairs do not resurrect a dead
  ship. The misspelled first-wave Navajas reference binds to the existing actor.
- Each engine-terminal press owns one stage. The first no longer waits for the
  second and then executes completion again. Existing dialogue, timed failure,
  reminders, effects, objective completion and the return-to-gun instruction
  remain; the repair reward is awarded once.
- M04 upper/lower reinforcements use their respective authored cover markers;
  all four spawn points previously called the upper callback, whose destination
  did not exist. The tunnel repair bot uses its own two workstations without
  stale `repbot_1` bindings. Camera initialization leaves idle/scanning to the
  camera's AI instead of calling an unsupported mover animation event.
  With the real defenders present, the first repair could leave an attacker
  trying to occupy their cover, unable to finish scripted movement or attack.
  The second revision hands control back to combat AI 128 units from the marker.
  A new `m04_prison_v2.script` retains the original helper unchanged for saves
  that already reference it.
- The delayed bridge gunner's missing script reference binds to the authored
  enemy staging point (`strogg_ctrlRm_jumper_1_pos_1`) on the upper bridge
  approach, retaining its original height and stopping before the drop. The
  entrance trigger is below its connected walking AAS region; the gunner
  cannot follow the marines' scripted drop. This placement is an inference
  from the bridge choreography and navigation, not a recovered gunner coordinate.
- M04 no longer requests the nonexistent `weaponmod_goobgun_burst` starting
  upgrade. The base weapon already provides burst fire; the later flamethrower
  upgrade is not granted in its place.
- The shared fire-volume repair from M01/M02 is retained. Directly quenching a
  large fire now skips creating an intermediate small-fire effect, stops the
  old stages and aims the new stage immediately as well as after deferred
  target resolution.

### Compatibility and scope

Campaign callback repairs require single-player `q4xbase`, the exact chapter
map, expected original functions and authored anchors. They edit spawn
arguments and append separately named openQ4 helper scripts; they do not ship
or modify recovered map/script files. Their helpers participate in the ordinary
saved-script list. Start M03/M04 afresh to acquire the new callbacks and entity
references; old saves retain their original mission threads. No save layout
changes or expansion multiplayer module are introduced.

The review found unused M03 ammo-jam/rear-fire sequences and an abandoned M04
ending cinematic with references to actors the map does not contain. These are
not reactivated. The shipped ending uses its direct end-level relay. Missing
M04 scientist dressing, effects and screen assets remain content limitations;
they are distinct from the corrected live encounter callbacks.

### Validation

`tools/tests/awakening_m03_m04_runtime.py` drives hidden, windowed SP using engine
commands and engine render-target screenshots, with independent writable
profiles and the installed alpha assets. No device input or desktop capture is
used. Its M03 test-only sidecar selects the encounters; M04 invokes authored
spawners. These are focused encounter probes, not full mission playthroughs.

The probe checks both gun entries, damage/repair state, an occupied save loaded
in a fresh process, real cannon shots through overheat and cooling, independent
screen values, missile selection/firing, engine completion/reward count, upper
and lower reinforcement arrival, repair-bot/bridge state and fire trace/damage
behaviour. Evidence is retained under `.tmp/awakening-m03-m04/`.

M03 passed in OpenGL and Vulkan (`release-gl/m03/`, `release-vulkan/m03/`):
the probe restored 9,000 hull health to 12,000, cleared the repaired diagram
section, loaded the occupied gun in a new process, overheated/cooled its cannon,
selected/fired missiles without a false lock, and completed the engine repair
at 13,000 health with one reward. Both physical screen sets remained unique.
These M03 runs preceded the final M04-only destination correction.

The final M04 production build passed both renderers (`qualified-gl/`,
`qualified-vulkan/`). Both cellblock groups reached their respective landing;
the repair bot reached its own workstation; the gunner reached approximately
`(986, 660, 272)` and reported `MOVE_STATUS_DONE`. The fire accepted freeze
shots, never blocked the player trace, and stopped intercepting projectiles
after extinguishing. These runs assert the production script reference; they
do not reposition the missing destination or the gunner in the test fixture.

`awakening_chapter_behavior.py` compiles the production fire methods against
counted doubles and covers freeze filtering, fractional/shared light dimming,
overkill, stages, one-shot target activation and active/off/out contents.
`awakening_status_behavior.py` now passes 111 targeting/status/magazine checks.
The follow-up harness `awakening_chapter_followup.py` adds positive acquisition,
lock/save/load, full/partial volleys and an occupied cellblock evacuation.
The core harness now emits explicit failures for script state checks: script
`assert` is disabled in release builds and was insufficient evidence for exact
repair totals and one-shot completion. Follow-up runtime results below supersede
those earlier assertion-only claims. Q4-04 stays open for full playthroughs and
the remaining campaign gates.

The second-pass build passes the stronger core probe (`round2-core-gl/`) and
the positive missile-lock probe on both renderers (`round2-lock-gl/`,
`round2-lock-vulkan/`). Acquiring/locked/cleared screen values agree with the
weapon; a full magazine and lock survive a fresh-process restore. The launcher
fires five, rejects an empty shot 432 ms later despite its 400 ms cooldown, and
fires one after 640 ms. The core probe explicitly verifies the 12,000/13,000
hull totals, one engine repair reward, independent screens, heat/cooling, both
reinforcement approaches, the maintenance bot and bridge gunner.

The occupied rescue probe uses four normal simulation tics per rendered frame.
It observes the initial fight, then periodically kills nearby attacking Strogg
through the ordinary AI death event as a substitute for player covering fire.
An unattended run left the medic blocked by fighting enemies; it does not prove
a navigation defect or justify making the encounter complete without defence.
This is a sequence/route check, not a combat-balance or full-playthrough claim.
`round2-rescue-qualified/` passes: attackers leave scripted movement and fire,
all six prisoner callbacks remove their actors, the final cell opens, and no
scripted movement failure or timeout is reported. Original scenario timing,
actor health and marine routes are unchanged in production. The earlier
`round2-rescue-*` diagnostics retain fixture failures and the unattended control;
they are not acceptance runs.

An unrelated developer-console compiler fault was reproduced while constructing
that probe: an include inside a function reaches the file-boundary diagnostic
with an unnamed global scope, and `idVarDef::Name()` dereferences its null name
instead of reporting the unsupported include. The helper now compiles at file
scope. Crash dumps, logs and a symbolized stack are retained under
`.tmp/awakening-m03-m04/round2-compiler-crash/`; this compiler robustness issue
remains open and is not part of the campaign repairs.

The final workspace check found `E:/Repositories/openQ4-worktrees/e931/openQ4`
(detached at `85c61fe16`) clean and fully merged. Removal is suggested after the
required user approval. The previously identified `claude/ui-right-stick`
worktree disappeared from the final inventory; this task did not remove it.
Missing alpha media remains an unrelated content limitation, as described above.

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
3 November 2026), so it appears stale. Removing it is recommended after the
user's required worktree approval. `E:/Repositories/openQ4-worktrees/e07a/openQ4`
initially appeared stale at detached commit `908b2c012`, but the final check found
active work on `codex/q4-legacy-event-data` with uncommitted changes; it is no
longer a cleanup candidate. Other worktrees had unmerged work or active changes
and were preserved.
