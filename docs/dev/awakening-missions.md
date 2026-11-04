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
- The security alarm's 1.5-second recording was both global and looping,
  repeating at full gain throughout the prison. It now plays locally at
  -12 dB, immediately on activation and then every 12–16 seconds. Its 96/1400
  unit distance range keeps the alert around the security encounter. The
  flashing lights and original objective/console callbacks remain intact.
  A pulse finishes naturally when disabled; no new pulse starts afterward,
  including when the console is used during a quiet interval.
- Three computer alarms also looped four-second recordings continuously.
  They now play the same installed samples at -18 dB within 360 units,
  every 19–29 seconds. They remain local machinery ambience rather than an
  additional level-wide security warning. These timings and levels are an
  editorial refinement inferred from the security sequence and dialogue.
- The security override console now completes once and then becomes
  noninteractive. Its reusable relay previously moved both shutters another
  120 units and toggled the disabled alarm back on with every additional press.
  Completion is claimed before deferred dialogue, so repeated activations
  cannot race it. The completed GUI state and script flag serialize normally.
- The turret-room approach chooses its camera branch once. Previously,
  destroying camera 8 during the staggered grunt/gunner arrival also started
  the bypass, allowing the door hack before the attackers died and starting
  another copy when their death counter fired. Both paths now use the existing
  one-shot hack relay; the ambush retains its authored timing and jump.
  The separately appended `m04_prison_v3.script` preserves older helper files.
- Prison cameras use collision bounds fitted to their hanging model and full
  scan animation: a 32-unit horizontal half-width and z=-36..4, instead of the
  inherited z=0..74 actor box. Camera 8's old box overlapped
  `cellblk3_UPRspawndoor_4`, leaving it hidden, invulnerable and waiting to
  activate. The repair preserves camera origins, viewing angles and health,
  and respects explicit map bounds. `aiDumpState -v` now identifies solid
  blockers when diagnosing a hidden AI that cannot wake.
  A second startup fault left the camera without any animation when a
  teammate supplied an unseen enemy before its first combat frame. It now
  initializes its pose, sweeps while the target is occluded and uses the idle
  tracking pose when the target becomes visible, without restarting the sweep
  every frame. Combat turrets without `scanAnim` retain their original path.
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

Alarm pacing also applies when loading an existing M04 save. The migration
matches the exact map, original console function, speaker names and shaders,
and leaves custom pacing alone. It replaces the old emitter and preserves
whether it was on or off. Revised saves keep the existing speaker timer,
including its pending reminder during a quiet interval. No script statements
or serialized fields are added for this change. Computer samples use implicit
one-shot declarations: setting `s_looping 0` alone cannot clear a loop flag
inside the original sound declaration.

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

The third-pass Windows build and stage pass. `awakening_alarm_runtime.py`
uses the real OpenAL mixer with its null output backend, never physical input
or desktop capture. The accepted alarm results are:

| Evidence directory | Result |
| --- | --- |
| `round3-alarm-baseline/` | Previous binary: actual global/looping flags and full gain; produces active/off regression saves. |
| `round3-alarm-final/` | Fresh map: 1.5-second security pulses with silent gaps and reminders, all three quieter computer samples, console shutoff, and silence at the distant bridge. Normal wall occlusion is enabled. |
| `round3-alarm-active/`, `round3-alarm-old-off/` | Old active and inactive saves migrate without losing their logical alert state. |
| `round3-alarm-gap/`, `round3-alarm-new-off/` | Revised saves preserve a pending reminder through a quiet gap, and keep an inactive alarm off. |

Each candidate run checks shutoff during a gap, restart, and the actual
`unlockSecRoomDoors` callback during a pulse; no later reminder revives the
disabled alert. These checks establish mixer gain, timing and state, not a
headphone listening assessment. Early alarm probes contain diagnostic failures
from declaration-name filtering, inaudible distant one-shots, and placing the
listener inside console dressing. Their reports are not acceptance results;
the saved gap/off checkpoints used by restore probes remain regression fixtures.

`round3-lock-gl/` passes M03 acquisition, locked save/load, five/one-rocket
volleys, empty-magazine rejection and lock clearing on the new build. A custom
110-degree FOV remains 110 through entry, restore and exit; the existing script
offset is -10 while seated and returns to zero on exit. The engine screenshot
also shows five loaded indicators, the selected launcher and a locked reticle.
Gameplay/CI contracts and repository path checks pass. Full playthroughs,
Linux/macOS runtime qualification and the other Q4-04 gates remain open.

The fourth-pass Windows build/stage and gameplay, CI and path contracts pass.
`awakening_prison_runtime.py` uses real map relays, normal damage and the
authored death counter; no console script is compiled into its saves. Its
OpenGL results under `.tmp/awakening-m03-m04/` are:

| Evidence directory | Result |
| --- | --- |
| `round4-console-baseline/` | Previous binary reproduces 120 additional shutter units and a restarted alarm on the second activation. |
| `round4-console-fixed/`, `round4-console-restore/` | Repeat use keeps both shutters stationary and the alarm off; completed GUI state survives a fresh-process restore. The real OpenAL mixer uses null output. |
| `round4-camera-blocker/` | Diagnostic identifies the upper cell door preventing camera startup; not an acceptance run. |
| `round4-original-qualified/` | Original installed encounter function, with the camera now functional, reproduces one premature hack and a second after the two deaths. |
| `round4-early-qualified/` | Early camera destruction bypasses the ambush and starts one hack; the gunner stays hidden. |
| `round4-late-qualified/`, `round4-alive-qualified/` | Camera wakes/scans; late destruction or leaving it alive keeps the hack gated on both deaths and starts it once. |
| `round4-pending/`, `round4-pending-restore/` | Save during the initial delay, with the camera destroyed and both attackers still hidden; a new process resumes the ambush and waits for both deaths. |

The engine capture in `round4-alive-qualified/` shows the hanging camera in
gameplay. Rendering is skipped during most later sequence probes to reduce
GPU contention, then enabled for the engine screenshot. Earlier diagnostics
include an observer inside the ambush trigger and a helper compilation error;
their failure reports are retained and are not acceptance evidence.
`round4-native/` compiles the actual camera state method and checks initial
unseen/visible targets, an uninterrupted sweep, loss/reacquisition of visibility
and the ordinary combat turret path. The existing fire-volume controls also
pass. This remains focused encounter qualification, not a full playthrough or
physical GUI interaction test.

An unrelated existing multi-screen GUI setter issue was also observed:
`Event_SetGuiParm` repeatedly prepends `gui_` while iterating screens, leaving
redundant `gui_gui_noninteractive` spawn arguments. Each screen still receives
the correct `noninteractive` state, so it does not invalidate the console fix.
The shared setter cleanup remains outside this mission change.

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
- **Restored fire effects:** client effects already survive saves. Starting a
  new loop in the owner's restore accumulated flames on each load. Restore now
  reconciles the completed client-entity graph, retaining one effect for the
  saved stage and stopping duplicate or obsolete stage effects.
- **M01 corpse saves:** the placed Strogg marine corpse requests the missing
  `strogg_marine` articulated figure. Its default placeholder cannot round-trip
  through strict AF restore. In `q4xbase`, that missing declaration now resolves
  to the shipped `monster_strogg_marine` rig; valid custom declarations remain
  untouched. Saves already containing the invalid placeholder remain invalid.
- **M01 stair gate:** the Retch introduction reclosed an AI gate behind a
  trailing Navajas, preventing his arrival at the walkers. Once released, this
  specific gate stays open. Older saves reopen it after the one-shot arrival
  trigger has been consumed.
- **Walker restore:** the stop-animation name pointed into a temporary string
  after loading. Both game libraries now own that string, with the same saved
  representation.
- **M01 courtyard route:** Navajas's waiting point led him into the opened
  north door leaf on the way to the final regroup. The original point now
  sits on the doorway centreline, on both spawn and restore. Revised mapper
  coordinates are preserved; actors still traverse the route normally.
- **M01 cinematic save identity:** changing the original cinematic program
  for the harness fill light made earlier saves silently restart M01. The
  earlier program is retained under its original filename, and fresh starts
  use the separately named lighting revision. Restore probes reject a map
  restart rather than counting it as a successful load.
- **M02 extraction actor:** the final scene spawned `darnell_ending` but sent
  the original downstairs Darnell to the extraction position. Version 2 moves
  the extraction actor, allowing the authored scene to finish without that
  unreachable move.
- **M02 lift-door approach:** Tak's eight-unit arrival requirement placed him
  between Darnell and Mitchell's collision bounds. He stopped 28 units short,
  leaving the door-lifting script waiting. His original waiting point moves
  38 units back into clear space on spawn and restore; the squad's positions
  and authored door-lifting and duck-under animations remain in use.
- **M02 boss console:** the reusable test GUI could restart the Tank introduction
  when the player fired beside it after the handoff. Its first activation now
  consumes the console and shows the authored inactive screen. A visible boss
  also guards older post-introduction saves; neither their scripts nor their
  save layout changes.

## Script identity and asset ownership

The original expansion remains external. `MissionScripts.inl` applies bounded
corrections to the installed M02 script in memory; it does not install a copy or
replace the user's file. Every edit must match once, or a fresh map retains its
installed behaviour with a warning. The reviewed installed script has SHA-256
`52f91083b62daf55c64cce1ced7b61a502b7748110a96b8a9ce3eccdd50746fa`.

Fresh corrected maps compile as
`maps/game/m02_trianfac.openq4-v2.script`. Saves record that virtual source name,
so restore reconstructs the same corrected program. Older saves still compile
`maps/game/m02_trianfac.script` unchanged. Restart M02 to adopt its script
corrections; do not reinterpret an old suspended script with new statements.
The v1 transformations remain unchanged for existing corrected saves. Version 2
adds only the extraction-actor correction. No save payload layout changed.

The authored M01 cinematic likewise keeps its original program as
`scripts/openq4/awakening/m01_dropship.script`; the route-qualified lighting
revision uses `m01_dropship-v2.script`. The separately qualified dramatic-feedback
pass now selects v3 for fresh starts; see the [M01 audit](m01-interior-feedback-audit.md).
The short-lived
unversioned lighting snapshot (`42200dc6f`) did not distinguish its changed
program in saves. M01 saves written with that snapshot can require restarting
the map; they must not be treated as pre-lighting saves or silently accepted as
successful restores. The pre-lighting and separately named revisions are the
compatibility controls.

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

## Follow-up route validation

The follow-up uses `tools/tests/awakening_route_runtime.py` with explicitly
authorized game-only input. Movement, aiming, firing, vehicle boarding/exit and
GUI interaction use normal game input and collision. The hidden, windowed
profiles disable host input. God mode and occasional ammunition refills exclude
combat-balance claims. Checkpoint resumes exclude an uninterrupted-playthrough
claim. Game time pauses between controller requests to prevent tool latency from
consuming escort timeouts. Captures are engine screenshots with rendering enabled;
simulation-only segments with `r_skipBackEnd 1` do not qualify appearance.

- **M01 part 1:** `route-m01-rigfix`, `route-m01-follow`, `route-m01-building`
  and `route-m01-exit` cover the unskipped insertion, squad doors, freeze-gun
  handoff, fire passages and combat waves. Normal GUI interaction enters part 2
  (`CAMPAIGN_CHAPTER reached 2`) and writes its autosave.
- **M01 part 2:** `route-m01-defence` resumes the Retch checkpoint with the
  repaired stair gate. Navajas reaches the walker bay. `route-m01-combat`
  covers occupied repair defence, walker release and the first demolition.
  `route-m01-traverse` checks restored walker steering/stopping and the second
  defence. `route-m01-demolition` replays the second charge placement and retreat
  with the player parked clear of Navajas's route. Both barriers are destroyed.
- **M01 courtyard and transition:** `route-m01-courtyard-replay` restores the
  second-demolition checkpoint, traverses the breach and kills all three turrets
  through ordinary walker/squad fire. Navajas reaches the corrected regroup with
  no movement timeout. The walker unlocks, normal jump input exits it, the squad
  reaches the final door, and normal GUI interaction enters M02
  (`CAMPAIGN_CHAPTER reached 3`) and writes its autosave.
- **M02 entry and generators:** the same continuation covers the processor
  consoles, Raven briefing and spike-gun handoff. `route-m02-generator` rides
  the squad lift; `route-m02-defence` restores its arrival, completes the
  natural generator ambush and Retch fight, and destroys the repair bots with
  ordinary weapon/squad fire. The shutdown waits while bots remain alive, then
  clears its objective and sends Markswell back on the lift. Tak proceeds
  through the maintenance exit. A Retch corpse obstructed the narrow stairs;
  normal jumps from clear ground passed it. Failed approaches that fell into
  the lower pit were restored and replayed, not accepted as route completion.
  All three corridor vents extinguish through freeze-gun fire and finish with
  zero collision contents. Tak rejoins Darnell and Mitchell, and all three
  reach the terminal room before the console sequence releases the defenders.
- **M02 download and lift:** `route-m02-download` cold-restores a partial
  defence with one replacement attacker dead and three still pending or alive.
  Ordinary weapon/squad fire completes the waves, releases the exit and moves
  the squad on. The following shaft encounters and door approach exposed Tak's
  blocked waiting point. `route-m02-lift` restores that already-stalled save
  with the correction: Tak reaches the console, normal crouch input crosses
  the raised door, it closes behind the player, and the lift carries both
  Tak and the player up 608 units. No actor was teleported or script callback
  invoked by the controller to achieve this recovery.
- **M02 Nexus, Tank and extraction:** `route-m02-lift` continues through the
  upper squad route and defended Nexus to the normal console interaction.
  `route-m02-extraction` resumes that checkpoint on the console fix, plays the
  full introduction, and defeats the 1,700-health Tank with ordinary machinegun
  fire aimed at its head. The normal death callback completes the objective,
  plays the dropship landing before idle, brings the sample carriers and the
  correct Darnell into the scene, then enters M03 (`CAMPAIGN_CHAPTER reached 4`)
  and writes its autosave. Engine captures cover the extraction and transition.
  Fresh and pre-fix post-introduction saves both pass the repeated-console
  interaction control. No direct damage, actor relocation or scripted encounter
  invocation is used in these accepted route journals.
  `route-m02-victory-restore` cold-loads the naturally won boss checkpoint,
  retains the consumed console and dead Tank, and completes extraction into
  M03 with another autosave.
- Failed navigation attempts remain in the journals: live enemies, the player,
  rubble and console geometry obstructed some direct approaches. Those attempts
  are not accepted as completed routes. Diagnostic actor moves or damage in
  earlier investigation profiles are not the acceptance runs above.

Additional focused controls, under `.tmp/awakening-m01-m02/`:

| Evidence directory | Result and scope |
| --- | --- |
| `corpse-rig-restore` | A fresh M01 checkpoint using the real corpse rig restores and resaves. |
| `fire-effects-repaired`, `fire-effects-repeat`, `fire-effects-m02-partial`, `fire-effects-final` | Burning and partly frozen checkpoints reconcile duplicate saved loops to one active stage effect, including repeated Vulkan restores. |
| `fire-effects-versioned` | The pre-lighting M01 checkpoint restores and resaves on the versioned build; one flame loop remains, with no map restart. |
| `cinematic-versioned` | Full unskipped M01 playback reaches handoff, saves and reloads the new cinematic program. Engine captures and beat timestamps are retained; no audio qualification is claimed. |
| `v1-restore-after-v2` | Earlier corrected M02 bytecode still restores after adding the separately named v2 program. |
| `extraction-complete`, `extraction-restored`, `extraction-final` | Focused v2 Tank-introduction/death/extraction chains reach M03 and its autosave. These invoke the encounter and apply diagnostic Tank damage; they do not qualify normal boss combat. |

`fire-effects-integrated` preserves the negative control: it restored neither
the earlier checkpoint nor its gameplay position, instead restarting M01 after
the unversioned lighting script changed. The versioned run replaces that result.

The route follow-up builds/stages the Windows client, dedicated server, both game
libraries and both renderers. Eight SP/MP restore-AF, restore-class,
restore-animation and vehicle-HUD checks pass; the final console change also
passes both restore-class checks. Gameplay/CI contracts, Python syntax and the
11,292-path portability check pass. `route-validation.json` indexes the accepted
journals, runtime identities and M03 transitions; its bypass-command audit is
empty for every accepted profile. The installed M02 script hash is unchanged.

The final extraction uses the fixed `.tmp/runtime/awakening-missions` runtime
because other chats continue to stage `.install`. Its executable hash is
`19a9bf867118c8145903fa71302c3b4fe0f170b15ca6334cf5fd09c19d9eaf11`;
matching game/content hashes are recorded in each profile's `binaries.json`.
An earlier `.install` session exited unexpectedly while paused; that incomplete
`route-m02-tank` run is retained and is not acceptance evidence.

The broader full-build attempts have separate limitations: `console-build.log`
records an unrelated UI fixture's missing `UI_WorldGuiInit` stub, and
`completion-build.log` records a concurrent compiler-output permission collision
on `Matrix.cpp.obj`. Neither is reported as a passing full-build result.
The successful targeted runtime build and native checks are retained separately.

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
- M01/M02 now have checkpointed route coverage through squad routing, both
  walker demolition defences, generator/download defence, Tank combat and
  extraction into M03. This does not qualify uninterrupted play, ammunition or
  difficulty balance, audio, or other platforms. The earlier guide's assertion
  that all thirteen maps had been played through exceeded the evidence.
- The Tank's installed animation still requests `fx_rocket_flash` on the absent
  `gunBarrelReal` joint. This presentation warning did not block the fight;
  the audit does not invent an effect or alter the installed model.

## Workspace findings

The user-approved completed `f744` worktree and its merged branch were removed.
The previously noted `claude/ui-right-stick` worktree is also absent from the
follow-up inventory. Other chats continue to use the listed project worktrees;
none was removed by inference. Unrelated roadmap edits in the main checkout
are preserved and excluded from this task's staged changes.
