# Awakening M01 and M02 mission audit

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
also appears stale: clean, detached at the merged 3 November commit `908b2c012`
(no branch). Removing it is recommended with the same approval. Other worktrees
had unmerged work or active changes and were preserved.
