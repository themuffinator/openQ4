# Awakening M07 mission audit

Supporting evidence for **Q4-04** in [the roadmap](roadmap.md), reviewed
4 November 2026. This is an audit of the player's installed alpha and the
canonical SP implementation, not another development plan.

## Inferred intent

| Leg | Authored sequence | Gameplay contract |
| --- | --- | --- |
| `m07_race` | Escape the collapsing MCC with the squad, see riders lost along the canyon, survive flyers and harvesters, pass two lockdown gates. | The opening establishes the loss of the MCC; the later squad fly-through introduces the timed lockdown. Each stage grants 60 seconds. |
| `m07_race1` | Race through aqueducts and moving pipe machinery, rollers, a bridge Tank and bomber attacks. | Two more gates grant 70 seconds each. Moving pipes and reamers are physical hazards, not evidence that the abandoned spline-tether controller should be restored. |
| `m07_race2` | Cross trenches and collapsing girders, regroup with Tak and Darnell, then enter the cryogenics facility in a scripted arrival. | Two final 70-second gates precede `raceExitCinematic` and the transition to `m08_cryofac`. |

The objectives explicitly require passing lockdown points. These are survival
races against closing gates, not races against the squad's spline-driven bikes.
Unused time at the first gate is added to the second gate **within each map**;
the following map starts its own allowance. Each gate has a five-second final
closure after the displayed countdown reaches zero. The corrections preserve
those allowances, movements and failure conditions.

## Corrections

- **Clock start:** the first map starts its timer after Markswell's radio
  explanation of the lockdown. That warning lead-in, opening cinematic and
  initial untimed departure remain intact. If a fast rider already crossed
  the checkpoint, the late radio callback cannot issue the first objective
  again or start a stale gate timer.
- **Single stage transitions:** repeated or late callbacks cannot reset a
  running timer, restart a gate, or resurrect a completed race. Completion
  explicitly updates the area state rather than relying on an adjacent trigger
  having fired first when a fast bike overlaps both volumes.
- **Carried time:** stage two establishes its deadline before starting the
  physical gate thread. Both use the same banked time.
- **Objectives:** the second objective is issued when its timer begins. The
  previous five-second delay could issue it after a fast completion had already
  tried to clear it.
- **Finish and timeout display:** countdown values clamp to zero on the final
  frame, both timer fields hide at completion, and the progress display reaches
  100 percent on all three legs. The original third map had no placed
  `raceProgress100` callback. Completed races ignore late gate-failure callbacks.
- **Stable result:** repeatable progress volumes still report the rider's
  position during the race, including backtracking. Once the leg is completed,
  they cannot reduce its full progress display. Once a gate fails, they cannot
  change its result or complete objectives. Failure disables driving and firing
  during the return-screen delay, which also cancels boost through the existing
  movement check. The final arrival cinematic cannot override a failed leg.
- **Player view reset:** the second and third maps called the camera-only
  `setFOV` event on the player, producing an unsupported-event warning. They
  now use the existing script FOV bridge to reset the temporary view offset,
  preserving the player's FOV setting.
- **Bomber encounters:** six copied attack sequences cleaned up bomber one
  instead of their own assemblies. Each now removes its own flyer, mover,
  launchers, damage volume and effects. Early exits after a flyer is shot down
  also clean up the detached mover and surviving parts. The second map placed two
  damageable flyers whose destruction relays called missing functions; those
  callbacks now use the map's existing explosion and pilot-ejection sequence.
  Destruction is claimed before its first wait, so repeated hits eject one
  pilot. A hit stops the volley, retires its temporary aim point and cleans up
  the whole assembly even before the attack begins. Removing the mover also
  releases an attack waiting for its spline to finish. Late attack/destruction
  callbacks cannot act on the removed flyer.
  No new attack routes or additional enemies are introduced.
- **Boost lifecycle:** leaving the seat, disabling movement, hiding the bike,
  or destroying it stops boost thrust and clears its FOV effect. The original
  recharge deadline survives cancellation and save/restore; re-entering cannot
  refill the boost. Zero-length decay cannot produce an invalid envelope at its
  endpoint. The authored acceleration, cooldown and input binding are retained.

## Script and save ownership

`M07MissionScript.inl` makes bounded edits to the installed scripts in memory.
Every required anchor must match its expected count; an unrecognized revision
keeps its installed behaviour on a fresh start and logs a warning. No recovered map,
script, voice, model, navigation file or texture is added to the repository.
Design references remain credited to Raven Software and Ritual Entertainment,
whose alpha was recovered by Justin Marshall; see the [upstream
credits](../../AGENTS.md#upstream-credits) and [licensing](../../LICENSING.md).

Fresh starts use `maps/game/m07_race.openq4-v3.script`,
`maps/game/m07_race1.openq4-v3.script` or
`maps/game/m07_race2.openq4-v3.script`. Saves record that virtual filename and
restore the same transformation. The original installed identities and v1/v2
transformations remain supported without changing their statement layouts.
Restart the current race leg to adopt v3's bomber lifecycle corrections;
earlier saves retain their saved script behaviour until the next map. Boost
fixes apply independently of that choice. Keep all versioned transformations
stable; further changes require a new identity.

## Validation

`awakening_m07_behavior.py` compiles the production boost class and mission
transformations with counted test doubles. Its normal asset-free run is in
both validation workflows. With `--awakening <q4xbase>`, it also checks all
three installed scripts, Windows path spelling, campaign/MP isolation, and
rejection of ambiguous anchors. It never copies those scripts into a fixture.

`awakening_m07_runtime.py` uses the staged SP client, windowed hidden rendering,
an isolated profile and the installed campaign. Existing map callbacks exercise
timer carryover, duplicate starts, early completion, gate failure and a saved
second stage. Captures come from the engine's `screenshot` command. Neither
harness captures the desktop or sends mouse/keyboard input.

The Windows x64 SP module was rebuilt through the Meson wrapper and staged in
the worktree's stable `.install/` path. Tests used independent copies of the
main checkout's client, renderers and matching package files; the engine was
not changed by this task. `engine-provenance.json`, `pack-provenance.json` and
each run's `plan.json` record those identities. The first-pass native fixture passed
67 checks with the installed scripts; its asset-free boost run passed 28.
Gameplay, speeder-physics, Awakening CI and repository-path contracts passed.
No Linux or macOS runtime result is claimed.

Evidence lives under `.tmp/awakening-m07/`; `suite.json` indexes accepted runs.

| Evidence directories | Qualification |
| --- | --- |
| `accepted-race`, `accepted-race1`, `accepted-race2-verified` | Fresh starts: duplicate callbacks preserve deadlines; first-gate time carries once; each second objective appears once; completion hides both timer fields and fills all ten progress pips. Completion survives beyond the second gate's full deadline and closing period. Bomber assemblies clean up without removing bomber one. Both missing destruction callbacks execute. Each leg changes to its authored next map, including the M08 arrival cinematic. |
| `accepted-restore-race`, `accepted-restore-race1`, `accepted-restore-race2` | Fresh-process reloads preserve the exact saved simulation time, seconds, hundredths, visible timer, objective and locked driver seat, then complete the same flow, cleanup and transition checks. The third leg reloads on Vulkan; the others use OpenGL. |
| `final-gate1-race`, `gate1-race1`, `qualified-gate1-race2` | Each first gate reaches zero and fails the mission after its closing delay. |
| `qualified-gate2-race`, `qualified-gate2-race1`, `qualified-gate2-race2` | Each second gate honours carried time, reaches zero, then fails the mission after final closure. |

Gate-failure checks preceded the final bomber-only cleanup changes; the final
flow/reload checks exercise the resulting module. Engine captures were inspected
for the completed HUD. The reports retain alpha media/navigation warnings;
passing means the stated assertions passed, not that the campaign has clean logs.

Superseded probes remain identifiable in their original reports: one engine/pack
pair failed package checksum verification; an early HUD probe read the state
dictionary instead of presentation visibility; a short wait hit the engine's
ten-tic acceleration limit; another wait missed failure before automatic map
unload. A stationary first-map probe died in the intended MCC blast. The expanded
bomber check caught a stranded mover after a flyer was shot down, which the final
cleanup change fixes. The first final-leg transition probe also included M08's
missing callbacks in its M07 check; that check now ends at M07 shutdown. Those
failed runs are not acceptance evidence.

The probes use accelerated simulation, god mode and notarget. The stationary
first-map fixture removes only `MCC_blast_pain`, because the authored MCC blast
correctly kills a rider who remains at the spawn. That isolation is confined
to the test profile and does not alter the installed map or gameplay. Timeout
probes sample repeatedly before the failure screen unloads the map; completion
probes assert that simulation time has exceeded the carried deadline plus its
five-second closure.

### Second review

The second review reproduced two additional defects in the v1 module. Calling
the authored progress callbacks after completion reduced the HUD from ten pips
to nine. After a missed gate, driving and firing remained enabled; late
checkpoint callbacks removed the failed objective, started a new timer and
filled the progress display while the player's failure flag was still set.
These reproductions live in `.tmp/awakening-m07-round2/boundaries-before` and
`failure-before`. The v2 transformation fixes those boundaries without altering
the gate allowances or the five-second closing period.

Two other suspicions did not reproduce and required no implementation change:
the next map reset its HUD to zero, and the rotating-pipe machinery completed
and repeated its cycle during a 100-second simulation. The machinery probe
observed two distinct cycle threads, each present in four samples spaced four
seconds apart, with no overlap. Its report is `machinery-before/report.json`.
This checks cycle liveness, not the safety or feel of driving through the pipes.

The expanded runtime harness has `boundaries`, `failed`, `failed2` and
`machinery` cases. `--input <race.save>` checks an in-progress restore;
`--case boundaries --saved-state finished --input <finished.save>` checks the
saved completion state. Both compare against the producing run's log, so keep
that profile's log beside the save. Boundary captures wait for the stock
objective popup to close naturally, because it temporarily suppresses drawing
the vehicle HUD. No input is sent to dismiss it.

The second-pass SP build passed. The native fixture passed **94 checks** with
the installed scripts and **28** without assets; the gameplay, speeder-physics,
Awakening CI and repository-path contracts also passed. The v1 transformation
body is unchanged from the first-pass commit. All **15 runtime runs** below
passed against the staged v2-capable module; `.tmp/awakening-m07-round2/suite.json`
indexes the reports and runtime hashes.

| Evidence directories | Qualification |
| --- | --- |
| `boundaries-race`, `boundaries-race1`, `boundaries-race2` | All ten progress callbacks below 100 percent leave a completed leg at ten pips. Both next M07 maps start at zero; the final leg reaches M08. |
| `failed-race`, `failed-race1`, `failed-race2`, and matching `failed2-*` runs | Both gates on all three legs honour the closing delay, reach zero, disable driving/firing and preserve their failed objective, countdown and progress after late timer/checkpoint/progress callbacks. The final-leg arrival callback cannot bypass failure. |
| `restore-v1-race`, `restore-v1-race1`, `restore-v1-race2` | Existing first-pass saves restore exactly, then pass the full deadline, bomber cleanup and next-map checks. The last uses Vulkan. |
| `restore-v2-race1` | A new in-progress save restores exactly, stays completed beyond the gate deadline and passes bomber cleanup and transition checks. |
| `restore-finished-race`, `restore-finished-race2` | New completed saves retain their result and reject progress regression after a fresh-process restore. The last uses Vulkan and waits for the objective popup before capture. |

The OpenGL in-progress restore and Vulkan completed restore captures were
inspected: all ten progress pips remain visible and both countdown fields are
hidden. The two expected v1 reproductions and the unchanged machinery probe
are indexed separately from the 15 acceptance runs. Existing content warnings
and the driven-playthrough qualification limits below still apply.

### Third review

The prior bomber probes exercised completed attack runs and the second map's
two restored destruction callbacks. They did not interrupt a paired volley or
save during the three-frame pilot-ejection sequence. New v2 reproductions in
`.tmp/awakening-m07-round3/before-baseline` and `volley-baseline` established:

- Two destruction callbacks in one frame spawned two pilots at the same spot.
- Destroying a flyer before its attack left its unbound base mover behind
  until a later attack attempted cleanup.
- Destroying it between the paired shots removed its bound launchers. The
  volley then tried to fire the missing second launcher and terminated before
  removing its temporary aiming entity; the target count increased from 45 to
  46 and stayed there.
- A later destruction callback terminated on a missing explosion entity.

The v3 correction assigns one destruction sequence to each flyer before it
yields, hides the hit flyer immediately, validates launchers after waits,
detaches and removes temporary aim points safely, and completes cleanup after
the existing explosion and pilot throw. An attack's mover wait is signalled
before removal. The third map's separate girder volley receives the same
launcher guards. Attack routes, projectile definitions, burst counts, paired
shot delays and the authored pilot velocity calculation remain intact.

The `encounters` runtime case exercises bomber two `--attack-phase before`,
`volley` or `retreat` (the last on `m07_race1`). `--bomber 4` on `m07_race2`
checks the girder volley. It repeats destruction callbacks, saves between the
explosion's waits, checks for one pilot and no stranded aim point, verifies
immediate assembly removal and eventual thread completion, and confirms that
bomber one survives. Passing `--input <bomber.save>` resumes that interrupted
sequence in a fresh process. These are callback probes; they do not establish
how often real projectile damage generates duplicate callbacks.

The pilot-ejection probes also retain existing ragdoll collision warnings,
including a parked flyer shot down by a direct callback. The original velocity
sample, spawn offset and throw are preserved; these tests do not certify
collision-free pilot placement throughout the authored flight paths.

The SP module rebuilt successfully; **112 native checks** with installed scripts,
**28 asset-free checks**, and the gameplay, speeder-physics, Awakening CI and path
contracts passed. Both earlier transformation bodies were compared against
`05a6db1c5` and are unchanged (`source-compatibility.json`). All **12 runtime
checks** below passed against the same staged module; the evidence index is
`.tmp/awakening-m07-round3/suite.json`.

| Evidence directories | Qualification |
| --- | --- |
| `before-race`, `before-race1`, `before-race2` | Destroying bomber two before its attack ejects one pilot, immediately removes its assembly, preserves bomber one, and makes a late attack/destruction callback harmless. |
| `volley-race`, `volley-race1`, `volley-race2` | Destruction between paired shots ejects one pilot, cancels the attack without script warnings and returns the temporary aim-target count to its baseline. |
| `girder-race2` | Bomber four's girder volley safely stops and retires its two authored map aim points. |
| `retreat-race1` | Destruction while the attack waits on its base mover removes the assembly and allows the waiting thread to finish. |
| `restore-volley-race1` | A Vulkan fresh-process restore from the middle of the explosion keeps its destruction claim, ejects one pilot and completes volley/assembly cleanup. |
| `flow-race1` | A fresh v3 race passes the full deadline, completion, normal bomber-run cleanup, extra flyer destruction and next-leg checks. |
| `restore-v1-race`, `restore-v2-race1` | Earlier v1/v2 saves restore exactly and pass their original full-flow checks on the new module. |

The two expected v2 failures are retained separately from these acceptance
runs. Existing media, navigation, ragdoll and later-map warnings remain visible.

## Remaining content and qualification limits

- The alpha's missing media and empty navigation datasets are not repaired by
  guessing replacement performances or shipping rebuilt proprietary maps.
  `m07_race1.aas128` and `.aas250`, and `m07_race2.aas96`, contain no useful
  navigation areas. A spline-bound cinematic actor and an independently moving
  combat AI must be assessed separately before choosing a fallback hull.
- The second map retains four missing tether references and a missing roof
  explosion target; the third retains seven obsolete removal-target references.
  The first map can also warn that a harvester lacks `range_attack`. These
  warnings remain visible in the reports. Removing a warning alone would not
  establish the intended positioning, animation or effect.
- The spline tether is disabled in the shipped speeder definition. Its script
  event stubs, abandoned gravity callbacks and commented-out chatter do not
  establish a requirement to turn those systems on.
- Callback probes qualify mission state, not collision-safe driving through
  every hazard, combat balance, the feel of steering, or cinematic audiovisual
  fidelity. An uninterrupted driven playthrough remains a Q4-04 acceptance gate.
- The transition also exposes unrelated M08 missing callbacks in this build:
  `VO_9_5_1`, `ValkaryneEscape`, `FirstLabCleared`, `FlashLightsOut` and
  `RemoveDoorIce3`. Arrival succeeds; the M07 test does not certify those later
  encounters or hide their warnings.

The speeder physics contract had a pre-existing stale assertion for
`ReadStaticObject`; it now expects the production `ReadPhysicsObject` restore
path while retaining the friction and restored-physics ordering checks.

## Workspace findings

The first review identified the clean, merged `claude/ui-right-stick` worktree
at `E:/Repositories/openQ4/.claude/worktrees/ui-roadmap-work-aaa011` as stale.
It was not removed by this task and was no longer registered at the second
review. No additional worktree was established to be disposable.

The third review found a possible stale checkout at
`E:/Repositories/openQ4-worktrees/e931/openQ4`: detached HEAD `85c61fe16`, no
uncommitted changes, and its commit is already contained in the main checkout's
branch. Consider removing it once its owner confirms it is no longer needed;
this task did not remove it. The other checked worktrees have unmerged commits
or active edits. The snapshot is `.tmp/awakening-m07-round3/worktrees.json`.

Automatic approval review rejected cleanup of obsolete test outputs, including
a narrowly scoped deletion after checking the absolute target and absence of
links, with the reason "blocked by policy". The second review's attempt to
remove disposable native fixture sources/binaries and a duplicate comparison
image received the same rejection. Disposable files therefore remain under
`.tmp/awakening-m07/` and `.tmp/awakening-m07-round2/` alongside retained evidence
and the reusable asset cache.
The third review separately verified the four disposable native fixture files
and their parent directories had no links, then attempted deletion using their
literal absolute paths. Automatic approval review again rejected the action
as "blocked by policy"; those files remain under `.tmp/awakening-m07-round3/`.
