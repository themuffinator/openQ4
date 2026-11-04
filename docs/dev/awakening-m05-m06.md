# Awakening M05 and M06 mission audit

Supporting evidence for **Q4-04** in the [roadmap](roadmap.md), reviewed on
4 November 2026. This is an encounter audit, not a separate development plan
or a claim that the complete campaign has been played through.

## Intent and evidence

| Map | Intent inferred from installed scripts and entities |
| --- | --- |
| `m05_bio` | Descend into the processing facility, lose the escort, reach Strauss before the processing line kills him, defeat the Pain Lord, then escort the rescue party to extraction. The boss combines body-health stun thresholds, two captive weak points, processing requests and a timed rescue. |
| `m06_mcc` | Give the rescue consequences: Strauss deciphers the stolen data, the command briefing explains the next offensive, and the player walks through a functioning MCC to Sci-Tech. This quiet interval establishes the spaces that the invasion revisits. |
| `m06_mcc_invasion` | Interrupt the briefing with an attack, defend the MCC from its cannon, escalate from fighters to harvesters and a convoy departure, then retreat through the damaged interior to the speeder launch. Protecting the MCC is a bounded combat objective, not a hidden timer that follows the player into the hangar. |

The references are the user's installed `m05_bio.map`, `m06_mcc.map`,
`m06_mcc_invasion.map`, their `scripts/maps/` files, the Pain Lord and harvester
definitions, and the current vehicle/spawner implementations. Design credit
remains with Raven Software and Ritual Entertainment; Justin Marshall recovered
the alpha. See [upstream credits](../../AGENTS.md#upstream-credits) and
[licensing](../../LICENSING.md). No recovered script, map or artwork is added.

## Corrections

- **Pain Lord phases:** a single heavy hit could cross both thresholds, launch
  two stun callbacks, or kill the boss while entering stun. Damage now stops just
  below the next threshold and fires one callback. A previous weak-point break
  cannot immediately cancel a later stun. Undying actors do not emit stuns for
  damage they rejected.
- **Captive weak points:** an invisible detached captive's collision joints
  could continue draining the surviving captive's shared health, then act as
  invisible body hitboxes after enrage. Those hits now do no damage. A hit that exhausts the whole pool removes both models, once.
  Attachment uses the existing saved surface mask; no save fields are added.
  The last captive's removal uses the authored `stunned_end_rage` recovery.
- **Final processing attempt:** the original third cycle killed Strauss as soon
  as the boss arrived at the console, omitting the first two cycles' wait for the
  work to finish. The final cycle now waits. Interrupting it buys another
  40-second interval before a new attempt, using the line's existing cadence.
  This retry is a deliberate fairness refinement: interruption must not be
  treated as successful processing.
- **Boss encounter cleanup:** light/spawner toggles no longer race across a
  one-second script yield. Death cancels the station approach and processing
  requests, and pending light callbacks cannot restart the combat spawner or
  leave the arena in its warning state.
- **Rescue objective:** the alpha's commented objective calls referred to
  deleted entities. A localized rescue objective now starts with the boss
  fight and completes with victory, before the existing escort objective.
  It uses the existing objective UI and installed Strauss image.
- **MCC continuity:** the landing no longer brings Mitchell back after his
  explicit M05 death. The remaining squad keeps its authored paths and cameras.
- **Cannon encounter:** the release-intended exit lock is active, entry is
  guarded against repetition, and hull monitoring ends with the defense.
  Hull destruction fails immediately instead of queueing a failure that can
  race victory. It no longer tries to seat the player again on failure.
  Pending wave toggles end before their spawners are removed. The hangar-route
  autosave happens after seat exit and teleport, rather than during transfer.
- **Fighter runs:** the first flyer phase gets 20 seconds instead of the
  explicit two-second unfinished placeholder, allowing its three spawners to
  participate before the harvester escalation. This is a pacing choice, not a
  claim to reproduce a finished alpha encounter. Shared spawn-return variables
  are protected during the short vehicle/driver handshake. Drivers are removed
  when their run finishes, freeing their `max_active=1` spawners for later runs.
  Missing spawn results cannot bind another flight lane's vehicle.
- **Normal callback cancellation (second review):** vehicle and AI callbacks
  start unnamed threads, unlike the diagnostic function-call command used in
  the first encounter probes. The station approach and cannon sequence now
  explicitly name their threads so death/failure can cancel them. The station
  approach also rejects duplicate and post-death calls and checks death after
  each movement wait. This closes a gap in the first cleanup correction.
- **Escape teleporters (second review):** each of the three core monitors claims
  its encounter once. Destroying a core marks it destroyed, stops its rotation
  waiter and removes its enemy spawner before deleting the core or yielding for
  break effects. Repeated callbacks cannot reset a destroyed teleporter or start
  duplicate rotation/health threads.
- **Service-lift lamp (second review):** M05's lamp follows `elevator2`; its
  original binding named a deleted lift and left the light downstairs. The
  correction requires the original lamp name, class and binding.
- **Rescue teleport (second review):** script removal is deferred. The outro
  teleported the player into Dekard's cinematic stand-in in the same frame,
  causing a telefrag before removal. A one-frame wait under the existing black
  fade lets removal finish before the teleport.
- **Flyby projectile ownership (second review):** a surviving fighter bullet
  retained a raw collision-owner pointer after its aircraft was removed. An
  early cannon resave reproduced the missing-object diagnostic. Clip owners now
  track the entity's spawn generation, so removal or reuse of its slot/address
  cannot keep an obsolete collision exemption. SP and MP share this fix; saves
  still write the original object-reference field, and restore reconstructs the
  generation from the already loaded entity table.

### Third review: escort support and incomplete references

The upper freight panel targeted a deleted `func_elevator_1`; the actual
`elevA` is a script-controlled mover. Its gates, squad boarding waits and
50-second ascent establish a one-way escape. The unused panel now matches the
other noninteractive freight panels and has no obsolete target. No return trip
or replacement elevator controller is introduced.

Tak's post-rescue armor-repair stall reproduces from a v2 save. The player stays
at `(1016, 2304, -511.75)`, while Tak turns around `(1149, 2360, -511.75)` and
hits Strauss. Removing Strauss in a disposable diagnostic permits the approach
and armor repair. Idle followers can now use the existing move-aside tactic
when their medic leader physically encounters them. The request preserves
scripted actions, existing movement, combat, tethers and disabled movement.
The move-aside search also lacked its target-distance check: it reported success
with Strauss's current origin as its destination. Candidate goals now require
the requested separation from the avoided entity as well as a valid destination.
The 15-second unreachable-patient watchdog remains in place. SP and MP share
the AI correction; no actor is teleported, made nonsolid or healed remotely.

Four M05 lights now select the installed `lights/p_light1xb` material when the
old misspelling has no declaration. Four small MCC monitors select the existing
Awakening ambient display when their old GUI is absent. Map media precaching
uses the same corrections as spawning. Installation-provided replacements win.
The tactical trooper's misspelled `a4x_tactical` specular texture uses its existing
`q4x_tactical` counterpart; `downsize()` now reports a missing child correctly.
The missing particle-fire image uses the corresponding retail `fire2a` image.
Both image fallbacks retain separate generated cache identities and prefer an
authored replacement. The machinegun Strogg skin uses the matching retail arm
variant when its expansion-only material is absent.

The command-room crash exposes its teleporter before a three-second activation
delay. A fast core kill removed the spawner while the crash thread still planned
to trigger it, terminating that thread with a missing-entity warning. Invasion
v3 checks the destruction flag, surviving core/health and spawner before the late
activation. The regression now treats missing script-event entities as failures.

## Save and campaign boundaries

Fresh starts compile corrected copies of the installed scripts into private
`oq4_m05`, `oq4_m05_painlord`, `oq4_m06` and `oq4_m06b` namespaces, using
versioned virtual map-script names. Only callbacks in that fresh map are
redirected. The default program and legacy save statement layout stay intact.
The harvester's installed animation frame callback deliberately keeps its
original `m06b::TURRET_DEAD` flag; the corrected encounter waits for that flag.

Saved corrected programs recompile the same virtual version, regardless of the
fresh-start toggle. Fresh starts use v2 for M05 and MCC arrival, and v3 for MCC invasion. The
v1/v2 generated script statements remain unchanged for their existing saves. Unknown
source anchors, a changed world callback, or a mapper's
map sidecar leave the original mission in control. Missing source on a corrected
save fails its load instead of substituting a different suspended program.

Restart these maps to adopt script changes. `g_awakeningM05M06 0` selects original
scripts on a fresh start for comparison; it does not disable Pain Lord AI fixes.
Retail and multiplayer receive no mission-script substitution. The payload
format is unchanged. The third review leaves both v1 and v2 script statements
unchanged. AI improvements apply to existing saves; restart a map to adopt its
corrected entity panels, lights and monitor assignments. Restart MCC invasion
to adopt the v3 delayed-activation guard.

## Validation

`tools/tests/awakening_status_behavior.py` executes extracted production methods:
136 checks cover the new thresholds, both hit orders, removed weak points,
overkill, processing completion/interruption and normal/rage recovery, alongside
the existing status and targeting controls.

`tools/tests/awakening_m05_m06_runtime.py` uses the staged SP client, hidden and
windowed, with input disabled and isolated `.tmp/awakening-m05-m06/` profiles.
It starts existing mission functions, applies engine damage commands, reads
entity/vehicle state, saves and restores in fresh processes, and captures only
through the engine screenshot command. Reports retain binary identity and exact
launch arguments. Windows x64 `debugoptimized` builds and staging pass using
`tools/build/meson_setup.ps1`; the gameplay/CI contracts and repository path
portability checks pass. All twelve shipped language tables contain the rescue
title and hint. The nine audited installed input files retain their hashes.

Evidence is under `.tmp/awakening-m05-m06/` in the task checkout:

| Probe | Evidence and observed outcome |
| --- | --- |
| Fresh M05 boss | `boss-final/report.json`: first stun at 5333 HP, second at 2666, rescue objective present, boss dead and Strauss alive/visible, then escort objective; processing thread removed. |
| Restored M05 boss | `boss-qualified/report.json`: resume a stun in a fresh process, destroy both captives, verify hits on their removed joints leave body health unchanged, finish the rescue and resave. |
| Uninterrupted processing | `processing/verification.json`: all three console animations complete before Strauss dies; the failure returns to the menu and no new save is written. The original report expected the failed map to remain loaded; the completed log was re-evaluated with the correct menu expectation. |
| MCC arrival | `arrival/report.json`: introduction completes and Mitchell's stale arrival actor is absent. |
| Cannon defense | `defense/report.json`: seated weapon fires, combat exit is refused, both harvester stages finish, player exits at the interior teleport and the hull-monitor thread ends. |
| Restored defense, Vulkan | `defense-vulkan/report.json`: the OpenGL cannon save resumes, completes, and survives actual hull destruction (`health=-999`) after victory; final save succeeds. |
| Hull failure | `failure/report.json`: restore the cannon phase, destroy the hull, and verify the mission refuses a new save. |
| Speeder handoff | `speeder/report.json`: board the unlocked bike, save aboard it, lower the hangar platform and enter `m07_race`; engine capture and save complete there. |
| Original-script save | `legacy/report.json` and `legacy-restore/report.json`: create a checkpoint with fresh-start corrections disabled, then restore/resave with the default enabled. |

Engine captures of the rescue, arrival, cannon, interior exit and M07 handoff
were inspected. These are functional encounter checks with scripted damage and
god mode, not difficulty or aiming evaluations. Native checks additionally cover
both weak-point hit orders and processing interruption/recovery. The final
processing retry has source and native-state coverage; a player-driven last-second
interruption is not qualified by the unattended processing probe.

### Second review

Evidence is under `.tmp/awakening-m05-m06-round2/`. The collision-owner
regression executes the same extracted production methods from both SP and MP:
`tools/tests/clip_owner_behavior.py` passes 12 checks covering deletion, copied
clips, slot/address reuse, explicit reassignment, restore ordering and high-bit
spawn generations. Both ordinary CI workflows run it.
The SP and MP restore-resource executables also pass their immediate-fixup and
interrupted-allocation checks.

| Probe | Evidence and observed outcome |
| --- | --- |
| Early cannon save | `cannon-owner-diagnostic/` identifies the old warning as the collision owner of `projectile_q4x_strogg_fighter_bullet`. After the fix, `cannon-resave/report.json` restores the same v1 checkpoint and resaves at 13.6 seconds with the cannon occupied and locked, without a missing-object diagnostic. |
| Three escape teleporters | `teleporters-final/report.json`: all cores and enemy spawners are removed, broken models show, repeated monitor calls are harmless and no rotation/health threads survive. |
| Active-core restore | `teleporters-restored/report.json`: a fresh process resumes all three active cores, repeats the callbacks and destruction, then resaves cleanly. |
| Destruction-boundary restore | `teleporters-breaking/report.json`: resume the save made one frame after core damage; shutdown completes, callbacks cannot restart it, and another save succeeds. |
| Fresh M05 rescue | `boss-final/report.json`: the station approach starts before the kill but never reaches its console after victory; Strauss survives, the rescue objective completes and the lamp reports `bind=elevator2`. The outro no longer telefrags Dekard's cinematic stand-in. The engine rescue capture was inspected. |
| Cannon failure through vehicle entry | `cannon-failure-final/report.json`: activate `dummyTurret`, verify the named encounter thread is waiting, destroy the hull, verify the encounter/wave threads are cancelled and saving is refused. |
| Cannon victory through vehicle entry | `cannon-defense/report.json`: enter through the vehicle callback, fire, verify the combat exit lock, finish both harvester stages, exit inside the MCC, destroy the hull after victory and resave without failing the escape. |
| First-review M05 save | `boss-v1-restored/report.json`: restore the original v1 stun checkpoint, finish both weak-point stages, rescue Strauss and resave with the new collision-owner code. |
| Second-review M05 save | `boss-v2-restored/report.json`: restore the final v2 stun checkpoint in a fresh process, finish the rescue without late processing or a cinematic telefrag, and resave. |
| Fresh MCC arrival | `arrival/report.json`: v2 compiles, the landing finishes without Mitchell's stale double, and the checkpoint saves. |

The earlier exploratory teleporter run exposed suspended mover waiters even
after `killthread`. The final version calls `stopRotating` first, waking the
waiter so cancellation can retire it. Temporary serializer diagnostics used for
the cannon investigation are removed from the final code.
The initial failure probe incorrectly treated registered threads as runnable.
`listThreads` now reports waiting/ready/cancelled state in both modules: a
cancelled timed thread can remain registered until its queued wake-up, especially
while the failed-save modal pauses gameplay. The test checks cancellation state.

### Third review

Evidence is under `.tmp/awakening-m05-m06-round3/`. The native regression
`tools/tests/medic_yield_behavior.py` executes the matching production methods
from SP and MP: 21 checks cover the follower eligibility guards, existing move
preservation, target separation, boundary distance and destination rejection.
Both ordinary CI workflows include the regression. Windows x64 build/staging,
SP/MP restore-resource executables, gameplay/CI contracts, media-fallback
contracts and repository path checks pass.

| Probe | Evidence and observed outcome |
| --- | --- |
| Blocked medic approach | `medic-final/report.json`: restore the post-rescue v2 checkpoint with zero armor; Strauss yields, Tak reaches the player and repairs armor to 100 without a navigation watchdog warning. Strauss, Navajas and Darnell remain alive and visible. |
| Mid-approach restore | `medic-restored/report.json`: restore the save made during the corrected approach in a fresh process; the same repair completes. |
| Fresh M05 rescue/media | `boss-final/report.json`: the full boss/rescue probe passes; all freight panels are noninteractive with no obsolete target, all four lamps name the intended material, and repaired image references no longer default. |
| Freight boarding/ascent | `freight-boarded-final/report.json`: resume the squad's completed boarding checkpoint, enter the platform, and reach the upper floor with the player and all four squad members. The panel remains noninteractive. |
| Mid-ascent restore | `freight-restored-final/report.json`: resume the correctly boarded mid-ride checkpoint in a fresh process; the platform, player and all four squad members reach the upper floor and resave. |
| Fresh MCC invasion, Vulkan | `mcc-v3-final/report.json`: all four monitors load the installed ambient GUI, repaired image references no longer default, and killing all three cores during the command-room activation delay finishes without a missing-entity event. Core/spawner removal, broken models and monitor/rotation shutdown pass. |
| Delayed activation restore, OpenGL | `mcc-v3-restored/report.json`: restore the Vulkan checkpoint before delayed activation, destroy the cores, and resave after clean shutdown with no script-event error. |
| Earlier MCC invasion save | `mcc-v2-restored/report.json`: the pre-existing v2 cannon checkpoint restores with its occupied, locked vehicle and writes another save using the final build. |

The door bypass and all four boarding waits complete in the `freight` and
`freight-final` exploratory logs. Those early probes exposed command-syntax and
view-height mistakes in the harness, not a failed squad route. The final boarding
probe uses an unobstructed player position and checks the player's origin as well
as the platform and squad. The earlier embedded-player restore is excluded from
qualification. The first `mcc-media` run also exposed a late-activation error
missed by the older assertions; it is superseded by `mcc-v3-final`. The native yield search diagnostic likewise showed why requesting
movement alone was insufficient: the old callback accepted its current position.

Engine captures of the repaired medic approach and freight ride were inspected.
`script-version-compat.log` compares the old and current production generators:
all six v1/v2 virtual scripts are byte-for-byte identical, while invasion v3 adds
the late-activation guard. `source-integrity.json` also confirms that all nine
audited installed inputs remain unchanged. The probes use scripted entry
points and checkpoints; they do not replace a complete player-driven traversal.

## Remaining limits

The alpha still contains missing media and abandoned ancillary callbacks.
M06's unused Tarrant routine references deleted actors;
it is not on the active route and is not resurrected.
Unused teleporter binds still name a deleted static parent; their current
world placements and the tested core-destruction sequence work without it.
Existing item-in-solid, missing animation, duplicate sound and shadow-map
fallback diagnostics remain in the runtime logs. Two remaining missing-image
references originate in retail files: `burn_misc_sm` in the failed-transfer eye
material and `hurt03` in the GEV machinegun flesh-impact effect. Neither named
image exists in the audited retail archives; this pass does not invent artwork
or silently substitute a different decal.

Generic mover damage ignores the console command's damage scale; the hull probes
use three explicit retail telefrag-damage events and verify actual hull health.

The focused probes do not qualify every escort path, optional conversation,
camera angle, the full walking route between encounters, or combat balance. They do not
provide Linux/macOS gameplay or full audiovisual parity evidence.
