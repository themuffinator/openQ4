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
through controlled doors, watch the other fireteam die in the freezing chamber,
shut down the leaking cryogenic system, then face the Tank guarding the descent.
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

Fresh M08 starts use `maps/game/m08_cryofac.openq4-v1.script`. Fresh M09 starts
compile `maps/game/m09_valkaryne.openq4-v1.script` into `oq4_m09_v1`, redirecting
the map's existing callbacks and docking request. The original `map_m09`
program loaded by `main.script` remains untouched. Old saves retain original
script statement indices; new saves recompile the virtual sources. M09's
custom map sidecars take precedence. All source anchors must match their
expected counts, otherwise fresh starts retain installed behavior. Do not edit
these v1 transformations after publishing saves; introduce a new version.

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

- M08's unused `RemoveDoorIce3` relay, undefined `ValkaryneEscape` callback,
  deleted steam targets and scientist `FirstLabCleared` callbacks have no
  unambiguous complete replacement sequence. The two-maiden death counter is
  intact despite one maiden's extra dangling target; it starts the cold scene.
- M09 still references removed cold-breath hosts, a missing forcefield,
  abandoned `spawnTactical02`/`spawnTrashCan01` callbacks and deleted decorative
  targets. The absent forcefield is now optional to its script; cold-breath
  setup still stops at the missing hosts/joints. Missing media and navigation
  warnings are not suppressed.
- Complete physical traversal, normal weapon balance, the three-minute cold
  deadline, escape timing, extraction framing/audio, and cross-platform runtime
  qualification remain Q4-04 gates. Focused callbacks and forced damage cannot
  establish those results.
- The Vulkan boss restore reported an unrelated renderer fallback for light
  498 (`lights/round`): a required shadow resource was unavailable and its
  receivers rendered without that shadow. This audit does not qualify Vulkan
  shadow parity or change the renderer.
