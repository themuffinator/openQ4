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

- 16 November (#230): no fighter wave could end. Every Strogg ship gets the
  death callback `stroggKilledFeedback( entity self, entity killer )`, which
  counts kills where `killer == $player1` and decrements the live-ship count
  each wave waits on. The stock SDK's `idAI::OnDeath` passes only the dying
  AI, so every call was refused (`Parm '' doesn't match expected type
  'entity'`): the player's kills stayed at zero and the live count only grew.
  The alpha's own game code evidently passed the killer too. With The Awakening
  active, a death callback that declares exactly a second entity parameter and
  was given none now receives the attacker from `idAI::Killed`, or the world
  when there is none. No other Awakening death callback (map `script_death`
  keys or script `setScript( "death" )`) declares one.

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
  -6 dB, immediately on activation and then every 5–6 seconds. Its 320/2000
  unit distance range holds full level across the security room and its
  approach and keeps the alert around the security encounter. The flashing
  lights and original objective/console callbacks remain intact. A pulse
  finishes naturally when disabled; no new pulse starts afterward, including
  when the console is used during a quiet interval.
- Three computer alarms also looped four-second recordings continuously at
  -7/-9 dB within 140/612 units. They now play the same installed samples at
  -12 dB within 96/560 units, every 9–15 seconds. They remain local machinery
  ambience below the siren rather than an additional level-wide security
  warning. These timings and levels are an editorial refinement inferred from
  the security sequence and dialogue.
- Pacing revision 1 (4 November) used a -12 dB siren every 12–16 seconds with
  a 96/1400 range, and -18 dB computers within 40/360 units every 19–29
  seconds. Playtesting found it too sparse and barely audible: with linear
  falloff the siren was near -17 dB a corridor away, before occlusion, and
  sounded for about a tenth of the time. Revision 2 (17 November) doubles the
  siren's amplitude, widens its full-level radius and gives it a steady
  alarm rhythm of about one pulse in four, while staying well below the
  alpha's constant full-gain loop.
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
whether it was on or off. The speaker's `oq4_prisonAlarm` spawn arg records
the pacing revision, so a save made under revision 1 (whose computers already
use the one-shot samples) is re-paced the same way; a revision-1 alarm that
was on starts a pulse at load. Saves at the current revision keep the existing
speaker timer, including its pending reminder during a quiet interval. No script statements
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

Pacing revision 2 (17 November) was qualified the same way, from the fixed
`.tmp/runtime/awakening-alarm/` runtime because other tests held `.install`.
The probe now follows the 5–6 second cadence, shuts the siren off both at an
arbitrary point and inside a guaranteed gap, and adds a listener about 620
units out in the approach corridor. Evidence is under `.tmp/awakening-alarm/`:

| Evidence directory | Result |
| --- | --- |
| `rev1-baseline/` | Revision-1 binary writes active/off regression saves. |
| `rev2-fresh/` | Siren mixer gain 0.325 at the speaker and 0.238 in the corridor, silent gaps, a reminder 5–6 s later, console shutoff, silence at the bridge; all three computers play at 0.16 and finish their one-shots. |
| `rev2-rev1-active/`, `rev2-rev1-off/` | Revision-1 saves re-pace to revision 2 and keep their on/off state. |
| `rev2-gap/`, `rev2-new-off/` | Revision-2 saves keep a pending reminder through a quiet gap, and keep an inactive alarm off. |

At the same mixer scaling, revision 1 would have given about 0.16 at the
speaker and 0.10 in the corridor (derived from its -12 dB gain and 96/1400
falloff, not measured), so the corridor level is about 2.4 times higher and
the siren sounds for about a quarter of the time instead of a tenth. As
before, these checks establish gain, timing and state, not a listening test.

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
- **Heat-exchanger jets (17 November 2026):** M04's four `func_fire_volume`
  vents are the only fires that set `fxtarget`, and none of their flames could
  be seen. `riFireFX::Event_LookAtTarget` built the effect axis from the raw
  vector to the target; `idVec3::ToMat3()` keeps that vector's length, so the
  roughly 100-unit aim scaled each jet about a hundredfold and scattered its
  particles outside the tunnel. The direction is now normalised, and a target
  at the vent's own origin is skipped. Each pair of jets now meets under its
  ceiling grate, and the small-flame and smoke stages follow the same aim.
  Damage, collision and lights were unaffected. Loading a save re-aims the
  running effect, so existing saves show the jets too.
  `awakening_chapter_behavior.py` checks the axis is unit length.
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

## Retch ambush, m01 part 2 — 17 November 2026

`m04_building_unit2_seq_5` is redirected to `oq4_m01_retch::ambush`
(`scripts/openq4/awakening/m01_retch.script`, `src/game/awakening/M01Retch.inl`)
on a fresh start, while the trigger and the scene's nine named entities match;
`g_awakeningM01Retch 0` keeps the alpha's function.

The writers' VO notes give the scene: Atlas Marine 6 meets Falcon ("You're Falcon
squad, good. The entire walker unit was wiped out... some spider-legged Strogg
piece of shit is tearing them apart", 9.3 s), dies screaming ("Arrrghh!"), Tak:
"What the hell is that thing?!", Navajas: "Kill it!". The marine and the Retch
share `st_retch_intro` (151 frames): the Retch waits 116 units back, behind the
door, until frame 88 (3.67 s), lunges, lifts him and throws him down by frame
144. The marine's loop (`st_retch_intro_loop`, scene frames 1-113) and held end
pose (`st_retch_intro_end_idle`) were authored but the dead pose was never used.

| Alpha | openQ4 |
| --- | --- |
| Hard cut to the camera and back. | Short fades in and out. |
| The kill starts after the briefing ends; the door opens 4 s into it, 0.3 s after the Retch has passed through it. | The kill starts 3.67 s before the briefing ends; the door opens 0.45 s before the lunge. |
| The death scream plays after the kill has finished. | The scream starts on the grab; Tak reacts as the marine is thrown. |
| The camera holds an idle Retch for 6 s. | Control returns as Navajas finishes "Kill it!". |
| The marine stands back up into his idle before he is killed. | He holds the authored dead pose, then dies where he fell. |
| The door closes as the camera ends. | It closes once the Retch is clear of the doorway (or after 8 s); seq_6 still reopens it after the Retch and the light tank die. |
| The Retch stays in its scripted entrance. | It is released to its AI with the player as its enemy, facing them. |

The trigger can fire twice when a teleport lands on it; the scene runs once.
Evidence: `.tmp/m01-polish/retch-base/` (alpha) and `retch-new/` (openQ4),
hidden OpenGL probes with engine screenshots every 0.25 s. Not verified: the
fight itself in normal play. In the probes the Retch stays in turret mode
because the map's `m04_building_unit2_seq_5_retch_blocker` AAS obstacle
surrounds it until the fight is won; it never saw the probe player, who had
come to rest behind a wall.

## M02 squad sequences — 17 November 2026

Four reported problems in `m02_trianfac`, fixed as script version 3
(`maps/game/m02_trianfac.openq4-v3.script`). Each was reproduced on the v2
program and checked on v3 with hidden, input-free probes (see below).

- **Gray at the generator.** Stepping off the generator lift runs
  `TeamToGeneratorRoom` at once (`trigger_once_151` is just outside the gate).
  The lift's own arrival code was still sending Tak, Markswell and then Gray back
  to their lift marks one at a time, so Gray was ordered back to the lift after
  the generator room had sent him to the console. He ended up working the
  console from beside the lift and dying far from the Retch dropped onto the
  console. On v2 the probe shows him turning back to `razor1_pos_27` beside the
  lift. The lift now gives its orders only if the generator room has not started.
  On v3 he walks straight to the console, works it there and dies beside the
  Retch's landing point (Gray at (-427, 1955), Retch at (-447, 1958)).
- **The extra marine in the Tank fight.** `BringOnTheTank` ends by moving its
  stand-in Dekard to `dekard_ending_pos_4`, still visible. That mark is past the
  arena's north gate, which the scene leaves open, so a second "Dekard" stood in
  view for the whole fight. He is now hidden there and shown again when
  `BossKilled` starts the ending walk. Ruled out on the way: the player is hidden
  during the cinematic, the Strogg marine released by `trigger_once_154` is
  removed when the camera starts, and Tak stays at the momentary-door console
  even with a follow order.
- **The spike gun handoff.** Tak only faced the gun on the crate, played the
  empty-handed `player_gift2` set and the gun vanished. He now kneels
  (`stand_to_crouch`) and picks it up. The world model is bound to his
  `MG_stock_joint`, the joint his own machinegun is skinned to, and his
  machinegun surface is hidden while he holds it. He stands, turns to the player
  and examines it (`weapon_check_1`, then `weapon_check_2` while still talking).
  He then offers it with the armed `player_gift` set, held for at least 1.5 s
  and until his line ends. The weapon is given at the handoff, and his machinegun
  comes back after `player_gift_end`.
- **The stuck door.** `stuck_door` was spawned non-solid. A player could walk
  through it before Darnell and Mitchell lifted it, and reaching
  `trigger_once_2` dropped it 48 units into the floor while they were still
  lifting. Their hold animations also played once and stopped. The door now
  blocks players and projectiles only (`CONTENTS_PLAYERCLIP |
  CONTENTS_PROJECTILECLIP`). The squad's marks stand inside its bounds, and Tak
  still ducks through the first 32-unit opening. The full 48-unit lift leaves
  about 45 units, enough for a crouching player. Darnell and Mitchell are placed
  exactly on their marks on arrival. Their scripted moves stop anywhere within
  8 units, which could leave less than a player's width between them. Each hold
  repeats one cycle at a time from where its lift animation put them, because
  the cycles' root motion walked Darnell into the gap. A hold ends with its
  stage, so nothing takes the pose again after the drop. The drop waits until
  the door is fully raised, and the urging lines stop once the player is through.
  The closed door is then fully solid, and Darnell and Mitchell lose any follow
  order, since they stay behind.

Probe traps: `setviewpos` onto a live `trigger_once` touches it twice before its
removal runs, starting the trigger's function twice. Fire such triggers with
`trigger <name>` or walk into them with `openq4_testInput`. `s_noSound 1` makes
spoken and radio lines end at once, which shortens every wait on them.
`g_autoSkipCinematics 1` skips `BringOnTheTank` and the ending entirely.

Evidence: `.tmp/m02fix/runs/` (`gray2-v3`, `door2-v2`/`door2-v3`,
`cannon-v3`, `dekard2-v2`/`dekard3-v3`, `nexus3-v3`, `save-*`/`load-*`), runtime
`.tmp/runtime/awakening-m02/`. `tools/tests/awakening_m02_squad_contract.py`
pins the correction table and, with the expansion installed, the SHA-256 of
every version's program.

## Script identity and asset ownership

The original expansion remains external. `MissionScripts.inl` applies bounded
corrections to the installed M02 script in memory; it does not install a copy or
replace the user's file. Every edit must match once, or a fresh map retains its
installed behaviour with a warning. The reviewed installed script has SHA-256
`52f91083b62daf55c64cce1ced7b61a502b7748110a96b8a9ce3eccdd50746fa`.

Fresh corrected maps compile as
`maps/game/m02_trianfac.openq4-v3.script`. Saves record that virtual source name,
so restore reconstructs the same corrected program. Older saves still compile
`maps/game/m02_trianfac.script` unchanged. Restart M02 to adopt its script
corrections; do not reinterpret an old suspended script with new statements.
The v1 transformations remain unchanged for existing corrected saves. Version 2
adds only the extraction-actor correction; version 3 adds the
[squad sequences](#m02-squad-sequences--17-november-2026). Each correction
records the version that introduced it, and a version applies its own and every
earlier one. No save payload layout changed.

The authored M01 cinematic likewise keeps its original program as
`scripts/openq4/awakening/m01_dropship.script`; the route-qualified lighting
revision uses `m01_dropship-v2.script`, and the dramatic-feedback pass
`m01_dropship-v3.script`. v4 also directs the hangar walk-on and adds the
squad's nameplates, the damaged cabin's harnesses and the rope approach; v5
shows the landing zone during the descent. Fresh starts now select v6, which
keeps Markswell's entrance, briefing and sit in one movement; see the
[M01 audit](m01-interior-feedback-audit.md#revision-4-17-november-2026) and the
revisions that follow it.
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
  establish those results. The frost's timing and damage were later confirmed
  at runtime; see [M09 frost hazard](#m09-frost-hazard--21-november-2026).
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

The integrated route runner preserves the earlier mission tools' walker driving,
weak-point height aiming, fixed runtime selection and explicit stop support.
Its default pauses game time between requests; use
`--no-pause-between-requests` to reproduce the continuous timing mode used for
the M08/M09 evidence above. The recorded per-profile command streams remain the
authority for whether pauses, restores or diagnostic setup occurred.
The gameplay evidence predates landing into the main checkout; its binary
identities are retained with each profile. Integration preserves the earlier
mission tooling and is checked by compilation, source contracts and controller
protocol checks. No additional gameplay replay was started after the user
requested testing to stop.


### Blast-door controls, media and navigation follow-up — 4 November 2026

M09 revision 4 restores the three optional escape controls using the surviving
`tunneldoors1`/`2`/`3` movers, the installed Strogg bottom-monitor model and the
existing localized `generic_seal.gui`. The panels sit on the exit side of each
doorway. Each relay latches its request, disables the panel, and waits for both
leaves to reach their authored closed origins before awarding the original
explosion-delay flag. These `start_open` movers reverse their logical positions;
the closure deliberately uses their second position. Touching a sealed door
cannot reopen it. Duplicate callbacks cannot add bonuses or toggle the doors.
The optional choice remains optional; the earlier successful open-door escape
is still supported. This supersedes the missing-panel finding above.

Fresh M09 starts select `maps/game/m09_valkaryne.openq4-v4.script`. Published
v1–v3 transformations remain unchanged for save recompilation. Custom map
sidecars retain ownership, supplied panels are retained, and a missing or
mismatched door anchor prevents adding that panel.

Media repair uses the player's installed assets, without shipping recovered
content. M08's missing table variants use the surviving retail table models;
missing decorative surgical tools retain their named entities but omit error
cubes and collision. M09's missing escort-objective image uses the surviving
`m09_obj_01`, and the shotgun Strogg marine uses its corresponding retail arm
material when the expansion variant is absent. The white and water-drip image
references use shipped white/neutralized drip images. Installed originals always
win. The earlier fire-image correction was already present in the main checkout.

Focused evidence is retained under `.tmp/m08-m09-refinement/`. These probes use
hidden windowed SP, scene callbacks, engine captures and checkpoint restores;
they do not inject player input or establish a new traversal/balance result.

M08 revision 3 also serializes the freezer staging request behind `FirstFreeze`.
An early request previously teleported the other fireteam and reassigned Tak and
Navajas while the thaw dialogue still owned them. In the restored checkpoint,
the approach marker is reachable with cryodoor 6 locked, but its exit marker and
`tak_pos_32` are not; opening that door makes both paths valid. The earlier
scene can also overwrite a prematurely issued move with its dialogue-facing
command. The new saved completion latch lets the thaw finish operating the door
and handing off both marines before the next scene starts. No AAS reachability,
collision geometry, NPC collision or teleport fallback is changed.

The lower-console checkpoint completes normally: Tak reaches the far doorway,
Navajas reaches his lower-floor station, and cryodoor 5 unlocks without a
scripted-movement warning. Its initial blocked route beyond the locked door is
expected. The M09 PROC/CM/AAS CRC discrepancy remains an external render-data
provenance mismatch; CM and AAS agree with the loaded map. It is not repaired by
falsifying CRCs or rebuilding geometry as part of this focused mission patch.

Focused validation passed on the staged Windows x64 OpenGL runtime:

- `freezer-sequence-gl`: an intentionally early freezer request waits for the
  thaw completion latch; Tak and Navajas reach their staging markers, cryodoor 6
  unlocks, and no scripted-movement failure occurs. Actor relocation is isolated
  scene setup, not traversal evidence; this console-instrumented run creates no
  qualification save.
- `seals-gui-gl` and `seals-gui-restored-gl`: GUI command activation closes all six
  door leaves, awards all three delay flags, and disables each panel. A fresh
  process restores a save taken while the first door is moving, completes it,
  and accepts each remaining panel once. Duplicate requests do not reopen doors.
- `media-m08`: the identified missing model/image warnings are absent, and engine
  captures confirm the repaired props. The console-route checkpoint and media
  probe were recorded before the additional M08 v3 ordering change; their binary
  identities and script revisions remain in their reports.

`legacy-v3-restore-gl` also restores an older M09 revision 3 save and writes
a new save retaining that revision. The native build, Awakening gameplay/CI
contracts and repository path checks pass.
Reports, exact command streams, binary hashes, captures and validation limits are
indexed by `.tmp/m08-m09-refinement/validation-summary.json`. Existing recovered
content still emits unrelated duplicate-declaration, embedded-item and empty ASE
mesh warnings. The isolated freezer setup also emits a medic-follow warning;
that is distinct from the scripted route, which completes successfully.

## Silent names — 14 November 2026

A round on one class of alpha defect: content that names something which does
not exist, beside something that does and is plainly what was meant. The engine
fails all of these without a word. `idAI::Speak` returns when its key is absent,
a radio entity shows its HUD over a sound that is not there, an effect on a
missing joint is skipped, and an actor with no eye joint and no `eye_height`
keeps a zero eye offset. A boot of all thirteen maps therefore shows none of
them as errors, and earlier mission reviews, which followed warnings and
callbacks, did not reach them.

The corrections live in `src/game/awakening/ContentRepairs.inl`, one table row
per name. They apply only in The Awakening's single player, only while the
written name is still missing, and nothing is matched by spelling at run time.
`g_awakeningContentRepairs 0` leaves every name as the alpha has it. No script
is changed, so no mission script gains a revision and saved programs are
untouched.

### How the intended names were found

- **Dialogue.** The content ships ten generated `scripts/maps/*_vo.script`
  files that nothing loads. Each wraps one recorded line and keeps the writers'
  text in a comment (speaker, context, words). A static pass resolved every
  `aiSpeak`/`aiSpeakWait` call of the campaign's scripts against its speaker's
  definition chain (`inherit1`/`inherit2` included) and every
  `radioChatterPlay` against the sound declarations. Thirty-nine calls name nothing.
  The writers' text and, on m06, the mission script's own comments identify the
  surviving recording for seventeen of them.
- **Joints.** A second pass checked every `fx` and `ai_attack` frame command of
  the expansion's model declarations, and every joint key of its entity
  definitions, against the joints of the mesh.

### Dialogue restored

| Map | Speaker | Script names | Plays | Evidence |
| --- | --- | --- | --- | --- |
| m01 (both parts) | Navajas | `lipsync_tak_strg1` | `vo_nava_strgg1` | The line is "Strogg!". The script hands Navajas Tak's take; his own take of the same word is recorded. |
| m04 | Tak | `tak_pris_door2` | `vo_tak_pris_door2` | The key lost its `lipsync_` prefix. |
| m04 | Darnell | `vo_darn_aboveus` | `vo_darn_aboveus` | The script passes the sound's name where the key `lipsync_darn_aboveus` belongs. |
| m04 | Private Hoff (`insane_marine`) | `lipsync_insane1_mumble` | `vo_insane1_mumble` | He is placed as a generic unarmed marine, without the dialogue definition that carries the key. |
| m08 | Navajas | `lipsync_nava_smrttak` | `vo_nava_smarttak` | Misspelt. "You're a smart man, there, Tak, but your priorities are a bit out of whack." |
| m08 | Navajas | `lipsync_nava_whathll` | `vo_nava_whathellgo` | Misspelt. "What the hell is going on?", as the cryocene floods in. |
| m08 | Tak | `lipsync_tak_deathscrm` | `vo_tak_die` | Renamed in the recording pass; played as the Valkaryne kills him. |
| m08 | radio | `vo_darn_dir` | `vo_darn_die` | Misspelt. Darnell's death cry. |
| m06 invasion | radio | `vo_7_13_11`, `_18`, `_2`, `_6`, `_12`, `_5`, `_1`, `_9`, `_3` | `vo_marine6_theydroppte`, `vo_marine1_gotcnt3`, `vo_marine1_thereact`, `vo_marine3_panthersq`, `vo_marine6_thatnegpan`, `vo_marine3_nexassalms`, `vo_marine1_contaceng`, `vo_marine5_igotthreemen`, `vo_marine2_thecomport` | The script comments each call with its words ("they're dropping teleporters!", "panther squad requesting support on deck 12", ...). The lines were re-recorded under speaker names and the script kept the scene numbers, so the cannon defence's whole radio backdrop was silent. |

Dialogue keys are added to the speaker's spawn arguments when the map spawns
(`Awakening_RestoreDialogueKeys`, after the definition's defaults). Radio names
are translated when the radio entity plays (`Awakening_RecordedLine`). A key or
sound the content does supply is never replaced.

**Never recorded (16 November).** Twenty `vo_4_*` radio calls with no
recording sit in `m04_stranarus_trench_intro.script`, a draft whose namespace
no map calls: neither m01 map has its radio entity or the marines it directs.
Their words are known: openQ4 places them, with nine more of the draft's
radio lines, at the matching scenes of the shipped battle, and they play once
they are voiced (`scripts/openq4/awakening/m01_radio.script`). The two m08 names (`lipsync_nava_herightsrg`,
`vo_darn_dntwrryct`) are absent from the writers' dialogue list, which holds the
rest of that exchange, but both are recorded (`sound/vo_english/m08_cryo/`)
without declarations. Darnell's played the missing-sample tone over the radio;
it is now silent. The
[missing voice lines register](awakening-missing-voice-lines.md) lists every
line with the pieces it lacks and its resolution. About 250 recorded lines are also named by no script or map
(race chatter, further squad conversation). The
[second pass](#replies-the-scenes-stop-short-of--14-november-2026) counts them
strictly, sorts them by kind and places the seven whose scene is provable.

### Eyes at the feet

`monster_q4x_tank` sets `joint_eyeOffset` to `rt_up_lid`; its mesh has
`l_uplid` and `r_uplid`. `monster_q4x_retch` sets it to `jaw`; its skull is the
single joint `head`. Neither sets `eye_height`, so both kept a zero eye offset:
every sight check started at the floor between their feet, and anything knee
high between them and the player hid the player. The joints resolve to
`r_uplid` and `head` (`Awakening_IntendedJoint`, from `idActor`'s setup).
Saves made earlier hold no eye joint for these two; `idActor::Restore` resolves
it when the actor has no `eye_height` and no separate head.

This changes combat as the definitions intended: both now keep sight of a
player behind low cover. Their attacks, ranges and damage are unchanged.

### Effects

- **Tank rocket flash.** Its `rocket_attack` frame commands are the retail
  Iron Maiden's (`ai_attack rocket gunBarrelReal`, `fx fx_rocket_flash
  gunBarrelReal`) with the attack moved to `tube1`. The flash kept the Maiden's
  joint, which the Tank lacks, and the Tank's definition never received an
  `fx_rocket_flash` key; no Tank rocket flash exists in the content. The flash
  now plays on `tube1` with the Maiden's `effects/monsters/iron_maiden/
  muzzleflash.fx`, the effect those frame commands were written for and one
  every installation has. This is the one place the round supplies a name the
  Tank's own files never held. The four launches still leave `tube1`, as
  authored.
- **Opening dropships (m01).** Each of the three ships lights `fx_thrusters` on
  `engine_righ` and then `engine_left`. Retail's dropship mesh has
  `engine_left` and `engine_center` but never had a right engine joint (its
  export list names one; retail's own animations use only the other two). The
  right plume is placed by reflecting `engine_left` through the plane midway
  between `gun_left_01` and `gun_right_01`, attached to `engine_left` so it
  follows the animation.

**Left alone: never played.** The Strogg marine's `teleport_fail` animation
(`r_wrist`, `head` for `rWrist`, `head_1`) and the fuel mixer's `blowup`
(`base_pivot` for `base`) carry the same kind of slip, but no script or code
plays either. The spike gun's `charging` animation is core-cannon residue.
Retail's dispersal harvester (m01) fails its own muzzle effects as it does in
retail. None is corrected without a way to see the result.

### Validation

`tools/tests/awakening_content_repairs.py` checks the tables, the call sites and
the single-player scope from the sources. With the content available it also
proves every row against it: the script speaks the written name, the name is
absent, the replacement exists with its recording or joint, and the Iron Maiden
still plays the borrowed flash from the same frame command. A row whose written
name later appears in the content fails as obsolete.

Runtime, Windows x64 OpenGL, hidden window, no input, the staged client with
the rebuilt single-player module, sound device open at zero volume. The cheat
command `openq4_testSpeak <entity> <key or sound>` makes the call a mission
script makes and reports a missing line; `aiDumpState` now prints each AI's eye
height.

| Probe | Alpha names (`g_awakeningContentRepairs 0`) | Corrected |
| --- | --- | --- |
| m08 dialogue | — | Navajas 3343 ms and 1323 ms, Tak 940 ms: the lengths of the three recordings. Darnell's cry resolves. The two unrecorded lines still report missing. |
| m04 dialogue | — | Tak 917 ms, Darnell 847 ms, Hoff 4549 ms. |
| m01 dialogue, both parts | — | Navajas 1201 ms. |
| m06 radio | — | All nine calls play their recorded line. |
| Tank and Retch eye height | 0.0 and 0.0 | 124.6 and 67.3 units above the origin. |
| Tank rocket volley | Four "Failed to play effect" per volley | None; the flash plays on `tube1`, loaded with the map. |
| m01 dropship thrusters | Four failures in the first 30 s | None; the right plume sits 154.7 units from `engine_left`, the figure the bind pose gives by hand. |

Restore: a save made with `g_awakeningContentRepairs 0` holds a Tank and a
Retch at eye height 0.0. Loaded in a fresh process with the corrections on,
they report 125.2 and 57.2 (the Retch mid-animation). An AI that has not
animated yet, such as a hidden one, reports 0 in any campaign; that is the
retail behaviour. A stock run (`game/airdefense1` on `baseoq4`) prints no
correction and loads as before.

A boot of all thirteen maps before the round (25 s each, developer mode) logged
no error; the same boot on the final
module logs none either, with identical warning counts on twelve maps and four
fewer on m01 part 1 (the thruster failures).

Limits. The lines were started by the tester command and the effects by spawned
actors, not by playing each scene, so scene timing with the restored lines
(overlap with neighbouring dialogue, lip sync on screen) has not been watched or
heard. The Tank's flash and the dropship's right plume are confirmed as played
and placed, not judged by eye. Sight over low cover follows from the measured
eye heights; no encounter was replayed to compare difficulty. Linux and macOS
were not run.

## Replies the scenes stop short of — 14 November 2026

A second pass on the same day, on recorded lines nothing plays. The first pass
counted about 250; that count let a generated copy of a line's key in a
hand-written definition stand for a use. Counted strictly (a script speaks the
key, or a map or definition binds the line to a talk, busy or speaker key),
the alpha plays 981 of the 1,357 lines in its nine current voice-over files and
leaves 376 unplayed. The two passes restore thirty lines, six of which the
alpha does speak, silently.

### Reading the writers' order

Each generated `<mission>_vo.script` lists its lines in the order of the
writers' document, and every line carries its text. Laid beside the calls that
do play (`.tmp/awakening-round/voorder.py`), the unused lines fall into four
kinds:

- **Earlier takes.** Most of them. "Walkers nearly functional, Sir." sits next
  to the played "Walker number one nearly functional, Sir."; "Our last two
  walkers" next to "Our last walker". The script plays the rewrite. Nothing is
  missing.
- **Talk replies beyond the first set.** A marine's replies when the player
  uses him. The maps bind two or three per character and the writers wrote more
  for later phases of a level (m02 has fourteen). Which phase each belongs to
  would have to be invented.
- **Ambient pools.** The races' radio chatter, below.
- **Replies with a scene.** A line the writers placed directly after one the
  script speaks, where the scene ends on the line before or holds a silent wait
  there. These are the ones restored.

The rule for restoring a line: it directly follows a played line in the
writers' order, the script has exactly one place for it, and nothing else in the
scene has to be retimed. A line that needs a new trigger, a new wait loop or a
choice between two scenes stays out.

### Sounds a map names wrongly

`Awakening_CorrectEntitySounds` replaces a map entity's sound key at spawn and
in both precache passes, from the table `awakeningEntitySounds`. A second audit
(`.tmp/awakening-round/mapsoundaudit.py`) checked every sound key of every
campaign map entity against the declarations and samples of the expansion and
of retail.

| Map | Entity | Map has | Plays | Evidence |
| --- | --- | --- | --- | --- |
| m08 | `music_intro`, `music_cryo_cue`, `music_cryo_exit` | `sound/music/m8_cryo_intro.ogg`, `..._cue.ogg`, `..._exit.ogg` | `sound/music/m9_cryo_*.ogg` | The three files do not exist. The tracks are on disk as `m9_cryo_intro`, `m9_cryo_cue` and `m9_cryo_exit`, declared in `music.sndshd` under "M9 Cryo": Cryofac was mission 9 when its music was delivered. The level played no music at its start, at its cue or at its exit. |
| m06 | `talkRichards_1` | `vo_1_1_11_360_2` | `vo_marine_noideawe` | Renner asks a pinned-down squad "What is your position?". The reply's speaker still holds the retail MCC line it was copied with ("But a Strogg unit is headed directly for Cougar squad."). Its twin `talkRichards_2` holds the recorded second reply, and the script's commented-out call beside the trigger names the first: `vo_marine_noideawe`. This row replaces a sound that does play, because it is another scene's line. |
| m04 | `sq2_marine_2`, `sq2_marine_3` | `lipsync_talk_loop_1` | read as `lipsync_talk_loop1` | `idAI::TalkTo` builds `lipsync_talk_loop1`, the spelling of every retail map. Only this map has the underscore, so Meakim and Pappas went silent after two replies. Their third replies, `vo_fox2_roughrider` and `vo_fox3_makesnerv`, are added under the key the game reads (the dialogue-key table of the first pass). |

A sound declaration the engine makes up for a name it was asked for no longer
counts as "the content supplies it": the checks use `idDecl::IsImplicit`, so a
stale name stays corrected after something has looked it up.

### Declarations that cannot reach their recording

A line can have its key, its lip-sync declaration and its recording and still
be silent, because its sound declaration asks for a sample path that does not
exist. A third audit (`.tmp/awakening-round/sampleaudit.py`) checked every
voice declaration's sample against the disk: seventeen reach nothing, and
seven of those have their recording elsewhere.

| Map | Line | Declaration asks for | Recording | Evidence |
| --- | --- | --- | --- | --- |
| m08 | Navajas sets and blows the charges: `vo_nava_herewegset2`, `_seconchrg2`, `_thatsit2`, `_demvawaychrg2`, `_thosechrglive2`, `_detonat2` | `sound/vo/M08-Cryo/...` | `sound/vo/m08_cryo/...` | These six alone spell the mission's folder with a hyphen (every other m08 line has `M08_Cryo`), and the hyphen also ends the path for the declaration parser. The script speaks all six; each played the default 185 ms blank. |
| m04 | Private Pappas's second reply, `vo_fox3_prismed` | `sound/vo/M04_Prison/fox_prismed.wav`, under the name `vo_fox_prismed` | `fox3_prismed.wav` | The map and the definitions name `vo_fox3_prismed`; the generated declaration and lip-sync data dropped the 3, and the sample name it asks for does not exist either. |

`Awakening_SpeechNames` is asked where a line starts
(`idAFAttachment::StartLipSyncing`, and `idAI::Speak` for a speaker without a
head). For these rows it starts the line under the lip-sync declaration that
carries its mouth shapes and plays the recording by its path, so the lip
movement is kept. The recordings are preloaded with their map, and a
declaration that does reach a sample is left alone.

Ten declarations have no recording under any name. The Pain Lord's three tube
marines' screams (`vo_marine1_agony1` to `vo_marine3_agony1`, spoken by
m05's script) now play stand-in screams through the same table (16 November):
the expansion's unused `scrmar4_pain` take for the first, and two of retail
Quake 4's medical-lab Stroggification screams for the others, so the three men
do not share one voice. The screams have no words, and each line keeps its own
caption. The other seven are two m01 lines nothing calls and five empty
generated entries in m06, duplicates of the recorded `vo_7_6_2` to `vo_7_6_5`.
See the [register](awakening-missing-voice-lines.md).

### Three replies in Cryofac

Fresh starts of m08 compile `maps/game/m08_cryofac.openq4-v4.script`. Revisions
1 to 3 are unchanged, and a save keeps the revision it was made with.

| Scene | The script speaks | Added | Evidence |
| --- | --- | --- | --- |
| First cave (`FirstCave2`) | Navajas: "Then why is this place so poorly defended?" | Tak: "Good question.." | Next line in the writers' order; the scene moves on to the walk directly after. |
| After the freezer (`TurnOffFreeze`) | Navajas: "I'm sorry, Sarge. Darnell. He was a good soldier..." | Tak: "Good soldier? Darnell was a good man. Falcon Squad won't be the same without him." | Next line in the writers' order, and an answer to Navajas's own words. The alpha has this call commented out one scene earlier, before Darnell dies. |
| Tak's death (`FinalValkaryne`) | Tak's death cry; the Valkaryne's "Your race dies here" | Navajas: "Tak! That Strogg bitch! She's gonna pay. Dekard, let's go..." | Next line in the writers' order after Tak's death. It plays once the Valkaryne has gone and before Navajas turns for the ladder, so it talks over nothing, and "let's go" is what he then does. |

### Not placed

- **The races (m07), all twenty-one unused lines.** Each race script holds an
  ambient radio thread (`miscRadioChatterThread`): a random line every 6 to 25
  seconds, each once. It is commented out whole, names thirty-eight `vo_8_3_*`
  sounds that no longer exist, and does not compile as written. Its start is
  marked in the second and third maps only. Sixteen marine lines survive that
  could be its pool, but nothing says which map each belongs to, and the lines
  are not interchangeable: six are about the Hannibal ("The Hannibal can't take
  this kind of punishment"), which blows up partway through the first map, and
  two are about a ridge. The six Hannibal lines also sit, in the writers'
  order, inside the opening cinematic, whose shots leave one second where they
  would need seven. Tak's two lock warnings mirror Darnell's in the second map,
  as the boost calls swap speakers between maps, but the third map's only
  gate-locking function is never called. "Harvester!" and the two "Look out!"
  lines each have more than one event they could answer.
- **Cryofac.** Tak's "I wonder how they maintain the temperature differential"
  and Navajas's answer lie between two scenes with no scene of their own. The
  Valkaryne's two further monitor lines would take 13.9 seconds in a scene that
  holds the screen for nine. "Everybody, over here! Look!" has two plausible
  scenes. Navajas's three hurry-up lines after Tak's death need a loop that
  waits for the player, which the scene does not have.
- **Two that pass the rule but were not done.** m02's ending: Darnell's
  "Allright! Vansen's back..." between Vansen's hail and Markswell's reply.
  m01: Darnell's "Yes sir." between two of Markswell's lines. Both sit in
  cinematics whose camera timing has to be watched with the line in, and
  neither scene can be reached without playing its level.

### Validation

`tools/tests/awakening_content_repairs.py` covers the new table and the script
revision: each written sound is missing (or is the retail line the row says),
each intended one is on disk, and each added reply has one anchor in the
installed script, follows its anchor in the writers' order, is not already
spoken, and is a key its speaker has with its recording. Seven negative
controls (a reply out of order, one already spoken, an obsolete sound row, a
leftover that is not a retail line, a talk key the game never reads, a declared
radio line, a present joint) were each rejected.

Runtime, as for the first pass (Windows x64 OpenGL, hidden, no input, sound
device open at zero volume; `fas_debug 1` prints each lip-synced line as it
starts and `s_showStartSound 1` each sound):

| Probe | Result |
| --- | --- |
| m08 start | "mission corrections v4 enabled". The three speakers name `m9_cryo_*`; `sound/music/m9_cryo_intro` starts 5.0 s into the level. Before, the same run starts no music. |
| m08 replies | Tak 981 ms and 4078 ms, Navajas 6107 ms, the lengths of the recordings. |
| m08 `FirstCave2`, through its own trigger | `vo_tak_maynotknw`, `vo_tak_lookathis`, `vo_nava_whyisplc`, `vo_tak_goodquest`, in that order. |
| m06 `rennerChatter` | `vo_renner_whatpos`, the speaker plays `marine_noideawe`, `vo_renner_yousigisnt`, the second speaker plays `marine_ifwemovedie`. |
| m04 Fox marines | Pappas: 3624 ms, 2849 ms (the restored second reply, started under its lip-sync data), 4241 ms. Meakim's third reply: 3778 ms. |
| m08 charge lines | With `g_awakeningContentRepairs 0` the first line lasts 185 ms, the default blank. Corrected: 1602, 1834, 2275, 1703, 1741 and 696 ms, the lengths of the six recordings. |
| m08 saved on revision 4 and restored in a fresh process | The restore compiles `m08_cryofac.openq4-v4.script`; `FirstCave2` then speaks the added reply in order. |

A boot of all thirteen maps on the final module logs no error. Twelve maps
have the warning counts they had before the pass; m01 part 1 has one more, a
ragdoll contact warning for a marine whose other leg warned before.

Limits. The freezer reply and Navajas's outburst were proved by the script
revision compiling (each edit must match the installed script exactly once, or
the revision is refused) and by their keys playing, not by running those two
scenes: both sit behind the level's cold-damage and Valkaryne sequences. No
scene was watched or heard, so the pacing of the three added replies and the
level of Cryofac's music against its ambience are unreviewed. Linux and macOS
were not run.

## Cryofac's opening, door seals and second ice wall — 18 November 2026

Three faults a player reported in m08. All three are entity repairs on the
recovered map, applied when Cryofac runs a corrected program
(`g_awakeningLateMissions 1`, the default) and only where the map still has its
recovered values. The mission script is unchanged, so no new script revision
was needed and saves of every revision load as before.

- **The squad starts inside the parked speeders.** Three speeders stand where
  the opening places Tak and Navajas. Both marines spawned inside a bike's
  collision and were pushed up onto it (z 799 and 791 on the first frame
  against a floor at 768). Navajas's first mark, `navajas_intropos_1`, lies 22
  units inside the third bike: he walked into its flank, never arrived, and
  `Intro()` waited on him until the 30-second scripted-move watchdog gave up.
  Tak, with his own move finished, wandered off to follow the player in the
  meantime. `tak` moves to `-2224 -6480 768`, `navajas` to `-2096 -6470 768`
  and `navajas_intropos_1` to `-2120 -6408 768`: the nearest clear floor beside
  the same bikes, measured against each bike's rotated collision. Facings and
  the rest of the route are unchanged.
- **The cryodoor seals answer the player.** The six `cryodoor_N_panel` seals
  (`generic_seal.gui`) target nothing. The doors open from script when Tak
  works the console (`TakOpenDoor`) or, for cryodoor 4, on approach. A
  player's press still played the seal's open animation beside a door that
  stayed shut. They now spawn `gui_noninteractive`, so the crosshair does not
  take them. Tak's lift panel (`tak_elevA_panel`) has the same lack of target,
  but no probe placement could focus it with the corrections off, so it was
  left alone.
- **Napalm on most of the second ice wall did nothing.** The wall is two ice
  meshes (`door_ice_2a`, `door_ice_2b`) over one napalm-only damage box
  (`door_ice_damage_2`, `damage_filter goob`). A grid of shot traces from the
  approach shows the box exposed only in a strip down the middle (x -2080 to
  -1760, z 840 to 900). Shots on the western third, the bottom row and the top
  edge strike the meshes, which take no damage. Glob splash usually reaches the
  box anyway, which hid it; the flamethrower mod has no splash and never
  melted the wall from the west. After the map spawns, both meshes forward
  their damage to the box (`SetDamageEntity`), so the box's filter and health
  decide as before. `RemoveDoorIce2` still removes all three. The first ice
  door needs nothing: its box fills the doorway and the melting model has no
  collision.

### Validation

Windows x64 OpenGL, hidden, no input, `developer 1`, the user-supplied alpha
content, `.tmp/runtime/awakening-missions` (the client, renderers and packs of
the current build, with the rebuilt single-player module).

| Probe | Before | After |
| --- | --- | --- |
| Marine positions every 2 s from map start | Spawn at z 799/791; Navajas stuck 35.4 units from `navajas_intropos_1`; watchdog abandons the move at 30 s; both reach `intropos_2` after about 34 s | Spawn on the floor (768.25); both reach `intropos_2` within 6 s and `intropos_3` within 14 s; no watchdog warning |
| `openq4_viewReport` focus, aimed at each seal from its walkway | With `g_awakeningLateMissions 0`: all six seals take focus | `focus none` for all six; the lift call panel `elevator7_call_2` still takes focus |
| Flamethrower, 2.5 s from the west of the ice wall (x -2260) | Wall stands | Wall removed |
| Rocket at the same spot, then the flamethrower | — | The rocket leaves the wall; the flamethrower removes it |
| Saved on the new module, restored in a fresh process, flamethrower from the west | — | Wall removed: the forwarding is saved state |

`openq4_viewReport` now ends with `focus <entity>` (the entity whose
interactive GUI the crosshair is on, or `none`).
`tools/tests/awakening_content_repairs.py` pins the three repairs.

Limits. Saves made in Cryofac before this change keep their own entity state:
their marines are wherever the save left them, their seals stay focusable and
the west of the ice wall stays inert until the map is restarted. No scene was
watched with a player moving; Linux and macOS were not run.

## Tak ignores enemies after an interrupted repair — 21 November 2026

Reported by the user: across the campaign Tak wanders while monsters attack
and never returns fire, while every other marine fights.

Tak is the squad's tech: `char_q4x_marine_tech` takes `spawnclass rvAIMedic`
from `char_marine_tech_extras` but, unlike stock's unarmed techs, inherits
`char_q4x_marine`'s machinegun (m01, m02, m04 and m08; m03's Tak is a plain
marine). `rvAIMedic::TakePatient` makes the healer `becomePassive( 1 )`: not
aware, ignoring enemies, enemy cleared. It remembered the stance from before,
but the stock code handed it back only from `DropPatient` while the healer was
unscripted. Two ordinary ways for a heal to end skipped that:

- a scripted move (`aiScriptedMove`) starts a move, `OnStartMoving` calls
  `DropPatient`, and the healer is already scripted;
- a scripted face or animation calls `SetState`, and `OnStateThreadClear`
  forgot the patient without calling `DropPatient` at all.

The healer then stayed passive and blind to enemies for good. It also meant
`SituationAllowsPatient` always passed (it returns true while ignoring
enemies), so the next armour repair remembered the leaked stance as the one to
restore. The Awakening's scripts move Tak constantly and repair armour at 35 or
less, so a repair interrupted by the next scene was routine. Stock never showed
it: its medics and techs carry no weapon.

The fix (`src/game/ai/AI_Medic.cpp`):

- `patientPassive` records that the current passivity is the heal's.
  `RestorePatientCombatFlags` gives the remembered awareness and ignore-enemies
  back once; `DropPatient` calls it scripted or not, and `OnStateThreadClear`
  calls it when another state takes the healer from its patient.
- A script's or `trigger_ai`'s own `becomePassive`/`becomeAggressive` clears
  the flag (rvAIMedic overrides both events and chains to idAI), so a scene
  that makes Tak passive during a heal, as m01's trench script does, stays in
  control.
- A heal taken over another keeps the stance from before the first.
- The flag is not saved: `Restore` derives it from the patient, so the save
  payload is unchanged.

### Validation

Windows x64 OpenGL, hidden, no input, `developer 1`, the user-supplied alpha
content, m02 Trianfac 12 s after the start with god and notarget. Each probe
spawns a Strogg marine in clear view about 260 units from Tak and samples
`$tak.getEnemy()` every 0.5 s. Before: the staged `.install` build. After:
`.tmp/runtime/awakening-missions` (client, GL renderer and single-player module
built from one tree, with `.install`'s packs).

| Probe | Before | After |
| --- | --- | --- |
| No repair (control) | Enemy after 3 s; the Strogg dies | — |
| `takePatient`, then `scriptedFace` 1.5 s later | Never an enemy in 12 s; the other marines kill the Strogg | Enemy at once; Tak kills it within 3 s |
| `takePatient`, then `scriptedMove` to a mark | — | Enemy throughout (the scripted move holds his fire, as it does without a repair) |
| `takePatient` left to finish, armour 0 | — | Enemy at once after the repair; the Strogg dies within 4 s |

`tools/tests/ai_medic_combat_stance_restore.py` pins the fix in both trees.

Limits. A save made while Tak already ignored enemies keeps that stance until a
script makes him aggressive (m01's trench script does; m02, m04 and m08 never
do), so restart the level or load an earlier save. The armour a full repair
gives was not read back, and no level was played through with a player moving;
Linux and macOS were not run.

## M09 frost hazard — 21 November 2026

The user asked what M09's strategy is meant to be. The recovered map and script
answer it: the level's pressure is a freezing fog that follows the player down
each descent and then times the boss fight. It is three slabs, `fog01_hurt`,
`fog02_hurt` and `fog03_hurt`, each a `trigger_hurt` 1,296 units square and 512
thick, bound with its fog light to a mover (`mvr_fog01`–`03`). The damage def
is `damage_triggerhurt_toxin`: 5 points a tick, `noArmor`. A slab is a band,
not a fill: once it has passed, the space above it is safe again.

| Stage | Started by | Movement | Then |
| --- | --- | --- | --- |
| First shaft | `main` (`moveIceCubeDown` → `navajasPlantsBomb`) | Lower edge 1,808 → 904 over 120 s | `trigger_once_99` (z 632) takes it down to 344 over another 120 s |
| Second shaft | `animateNavajas`, after Navajas falls | Lower edge 584 → −448 over 120 s | `trigger_once_100` (z −640) takes it to −1,392, the floor's level, over 120 s; the exit door unlocks at `trigger_once_105` |
| Finale | `initValk` | Lower edge −4,072 → −4,736 over 180 s | Stops 64 units above the −4,800 arena floor; the script's comment calls it the three minutes allowed for the boss |

The first shaft's floors are about 256 units apart (z 1,408, 1,152, 896, 640,
320). With the player's fall thresholds (`soft`/`hard`/`fatal_fall_delta` 30,
45 and 65; delta is 0.2132 per unit dropped at the default gravity), a drop of
more than about 141 units costs 10, more than 211 costs 30 and more than 305
kills. One floor straight down is a hard fall and the 320-unit last drop is
fatal, so the descent is the beam, pipe and machinery route recorded above, not
a staircase. Nothing in the map signposts it.

### Validation

Windows x64 OpenGL, hidden, no input, `developer 1`, the user-supplied alpha
content, default skill, `notarget`. The staged `.install` client, single-player
module and GL renderer had the same hashes before and after the runs
(`e36d2210…`, `51e7863c…`, `560e659b…`). One console script per run teleports
the player onto static world floor inside the slab's footprint, fires the map's
own triggers, and samples the mover's origin and the player's health every
second; `g_debugDamage` logs each hit. The first shaft starts naturally. The
second starts from `trigger_once_9`, the egg sequence that ends in Navajas's
death, and the finale from its two intro triggers.

| Check | Expected from the map | Observed |
| --- | --- | --- |
| First shaft, starting floor | Hurt once the lower edge reaches head height (feet + 77) | Moves at 7.7 units/s from about 2 s in; damage from about 42 s |
| First shaft, z 640 floor | Safe while the slab rests at its first stop | No damage until `trigger_once_99`; it then moves at 4.7 units/s, with damage about 40 s later |
| Second shaft, z 64 floor | Hurt about 52 s after the fog starts | Fog starts about 33 s into the egg sequence, moves at 8.7 units/s; damage about 52 s later |
| Second shaft, bottom floor | Hurt about 110 s after `trigger_once_100` | Moves at 7.9 units/s; damage about 110 s later, and it stays |
| Finale, main floor standing | Hurt about three minutes in | Starts about 12 s after the intro; damage from about 186 s |
| Finale, crouching (`openq4_testInput … crouch`) | Below the slab | No damage for the 15 s crouch; it resumes on standing |
| Finale, entrance ledge (−3,808, 736, −4,672) | Outside the footprint, though inside the band's height | No damage |

Each hit cost 4 health and no armour. The fog's trigger is not the world, so
`CalcDamagePoints` applies the skill scale (none on Normal), and on Easy and
Normal `g_useDynamicProtection` then scales player damage down from 1.0. By the
same code Hard multiplies it by 1.7 and turns dynamic protection off: about 8 a
tick, not measured. Logs: `.tmp/awk/runs/m09frost_s1`, `m09frost_s2` and
`m09frost_fin`.

Two failed attempts are not results. Two probes sharing one savepath raced on
the settings-recovery file lease and the second exited with a fatal error
(OS error 32), so the runs were made one at a time. A first second-shaft spot at
(−2,584, −112, 140) was not static floor and the player fell to z −2,617; the
rerun used the cryo floor at (−2,688, −440, 64). Starting the second shaft early
also logs a null `$mover_icecube` warning from the first shaft's still-running
opening thread; that is the probe's shortcut, not a map fault.

Limits. This confirms the hazard's timing, reach and damage, not a descent made
with player movement against it, the Valkaryne's attacks, or balance. Hard and
Nightmare were not run, and neither were Vulkan, Linux or macOS.
