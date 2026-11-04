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

## Save and campaign boundaries

Fresh starts compile corrected copies of the installed scripts into private
`oq4_m05`, `oq4_m05_painlord`, `oq4_m06` and `oq4_m06b` namespaces, using
versioned virtual map-script names. Only callbacks in that fresh map are
redirected. The default program and legacy save statement layout stay intact.
The harvester's installed animation frame callback deliberately keeps its
original `m06b::TURRET_DEAD` flag; the corrected encounter waits for that flag.

Saved corrected programs recompile the same virtual version, regardless of the
fresh-start toggle. Keep v1 stable once released; use a new version for later
statement changes. Unknown source anchors, a changed world callback, or a mapper's
map sidecar leave the original mission in control. Missing source on a corrected
save fails its load instead of substituting a different suspended program.

Restart these maps to adopt script changes. `g_awakeningM05M06 0` selects original
scripts on a fresh start for comparison; it does not disable Pain Lord AI fixes.
Retail and multiplayer receive no mission-script substitution. The payload
format is unchanged.

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

## Remaining limits

The alpha still contains missing media and abandoned ancillary callbacks.
M06's unused Tarrant routine references deleted actors;
it is not on the active route and is not resurrected.
The map audit also found orphan targets/binds, including M05's unused upper
freight-lift call panel and a light bound to a deleted lift name. The actual
freight lift uses its scripted mover. Existing item-in-solid, missing animation,
duplicate sound and shadow-map fallback diagnostics remain in the runtime logs.

An exploratory early cannon resave logged the serializer's transient-reference
to-NULL diagnostic (`failure-restore/`); the verified phase-to-completion paths
do not establish clean saving at every combat instant. Generic mover damage
also ignores the console command's damage scale; the hull probes use three
explicit retail telefrag-damage events and verify actual hull health instead.

The focused probes do not qualify every escort path, optional conversation,
camera angle, teleporter encounter, or overall combat balance. They do not
provide Linux/macOS gameplay or full audiovisual parity evidence.
