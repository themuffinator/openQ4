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

Fresh starts use `maps/game/m07_race.openq4-v1.script`,
`maps/game/m07_race1.openq4-v1.script` or
`maps/game/m07_race2.openq4-v1.script`. Saves record that virtual filename and
restore the same transformation. Older saves retain their original installed
script names and statement layouts. Restart the current race leg to adopt the
mission-script corrections. Boost fixes apply independently of that choice.
Keep these v1 transformations stable; future changes require a new identity.

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
each run's `plan.json` record those identities. The final native fixture passed
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

`E:/Repositories/openQ4/.claude/worktrees/ui-roadmap-work-aaa011`, branch
`claude/ui-right-stick`, is clean and fully merged into the main checkout's
branch; its last commit is dated 3 November 2026. It appears stale and removal
is recommended after the user's required worktree approval. It was left intact.

Automatic approval review rejected cleanup of obsolete test outputs, including
a narrowly scoped deletion after checking the absolute target and absence of
links, with the reason "blocked by policy". Disposable preliminary profiles
therefore remain under `.tmp/awakening-m07/` alongside the retained evidence and
reusable asset cache.
