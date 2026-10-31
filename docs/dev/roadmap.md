# openQ4 development roadmap

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Updated 23 October 2026. Stage-one implementation and tested source:
`7674acef91c2653e8c6153ae11e7b158c05e52ff`. The
[baseline evidence record](baseline-evidence.md) binds current native tests,
windowed gameplay and original-content controls to their exact source, binaries
and assets. Historical findings retain their original evidence scope; source-only
risks and remaining device/performance qualification are identified separately.

This is the single active development plan for openQ4. It consolidates the
outstanding implementation, integration, compatibility and release qualification
work across the engine, games, renderers, platforms and tools. Work should extend
the current implementation and close its remaining gaps while preserving retail
Quake 4 behavior and the supported package layout.

The [planning archive](archive/2026-10-18/README.md) preserves the previous plans,
proposals and their evidence. Historical checkboxes and instructions there do not
set current priorities or establish present support. Technical contracts, visual
specifications, requirement registers, format policies and test procedures remain
in force where this roadmap incorporates them. They provide detail and evidence;
future work and decisions are coordinated here.

## How to use this roadmap

- Keep task IDs stable. Record implementation commits, exact tested package,
  evidence and remaining limitations against an item before closing it. Split an
  item if implementation is complete but its qualification remains open.
- **P0** establishes safety, truthful scope and the evidence needed by other work.
  **P1** closes functional or qualification gaps in the intended product.
  **P2** completes broader functionality and measured quality improvements.
  **P3** is a deferred design decision or optional expansion, not a release promise.
- **Implementation** means code is missing; **integration** means relevant code
  exists outside the active source or is not connected to the production path;
  **qualification** means a scoped implementation exists but needs the stated
  evidence; **decision** means scope or compatibility policy must be settled first.
- All unchecked items remain open. A capability described as implemented below
  should not be rebuilt merely because an older plan calls it missing.
- Priorities order work within the selected delivery scope; they are not a promise
  that every P1 item ships in the next release. Select an explicit release subset
  with its dependencies, exclusions and rollback policy before implementation.
  Optional platforms/features do not block unrelated supported configurations.
- Close implementation and qualification separately. Each selected item needs an
  accountable workstream, exact source entry points, a failing/reproduction case
  where applicable, regression checks and the evidence required for its exit.
  Existing source-token checks establish contracts, not behavior or timing.
- Keep completed release material in [release completion](release-completion.md)
  and polished player notes in `releases/`. Those are release records, not a
  second development backlog. Keep the [capability matrix](engine-capability-matrix.md)
  and [platform support matrix](platform-support.md) aligned with accepted evidence.
- GitHub issues retain reports, reproductions and contributor discussion; PRs
  retain proposed changes and review. Link them to the work items here. A partial
  fix or merged PR does not close a report whose remaining symptoms are unproven,
  and a request for reporter evidence is not grounds for automatic stale closure.

## Report and contribution traceability

Reviewed 31 October 2026. These links connect the reports and contributions
to the existing workstreams; they do not create separate schedules or promise a
release. GitHub remains the source for subsequent discussion and PR/check state.

| Report or contribution | Owning work and remaining acceptance |
| --- | --- |
| [#168: disappearing menu text after preset changes](https://github.com/themuffinator/openQ4/issues/168) | UI-01/UI-07, REN-04: the earlier glyph reload fix is partial evidence; qualify current GL/Vulkan preset changes and seek confirmation of the intermittent symptom. The verified 31 October replacement package (`f0a86b0ec`, #205) was offered for the retest. |
| [#169: resolution scaling affects menus](https://github.com/themuffinator/openQ4/issues/169) | REN-04/UI-07: the contributor's 22 October Linux/OpenGL retest preserves menu sharpness in mode 1. Since 31 October legacy mode 0 crops only the full-display world render instead of the whole frame. At 25% on Windows x64, GL and Vulkan, the retained and stock title menus, the SP pause menu and the SP/MP HUD now match their native-resolution captures, including after `vid_restart`; MP keeps the world-in-corner fill-rate view. Await the reporter's confirmation; Linux, macOS and window resizing were not rechecked. |
| [#170: weapon effects, sound and reload animation](https://github.com/themuffinator/openQ4/issues/170) | Q4-03/AUD-01/HF-04: preserve the merged BSE sound-diversity fix; identify and qualify each remaining weapon/effect/animation symptom separately. The 26 October package could not save; the verified 31 October replacement (`f0a86b0ec`, #205) was offered for the retest. |
| [#181: rebuilt hub2 cannot load](https://github.com/themuffinator/openQ4/issues/181) | CORE-05: the compiler allocation crash was fixed by #182; oversized runtime surfaces and silhouette warnings remain. The 31 October compile of the reporter's `hub2.zip` traced them to zero-area slivers, not real detail: 12,302,653 of its 12,601,692 triangles (97.6%) are collinear, and the largest surface's 12.2 million triangles hold only 23,671 with any area. The trigger is the map's switch of 345 pipe patches to explicit 10×10 subdivision. With stock subdivision the same edits compile in about a minute to 118,497 triangles. T-junction fixing then keeps splitting the collinear fragments, because each split of a flat sliver overlaps the last. Stock `hub2` built with current dmap also contains 58 such slivers (retail's `.proc` has 43). Next: stop T-junction fixing from splitting or emitting zero-area triangles, then recheck the reporter's map against the renderer limits; partitioning remains the plan only for surfaces that are genuinely too large. |
| [#183: machinegun transparency](https://github.com/themuffinator/openQ4/issues/183) | Q4-01/VK-04/REN-08: closed on 28 October after the reporter confirmed the `2126a232` scope corrections on both GL/Vulkan, in separate and normal installations. This is a development fix, not a new release. The test package's separate save failure (#205) was fixed by #203; the replacement package and instructions were posted here on 31 October. |
| [#184: Tech Li at the hangar1 lift](https://github.com/themuffinator/openQ4/issues/184) | Q4-02: the supplied save identifies the Turbo/scripted-mover timing race; #198's fix landed on main in `2677fb87b` (29 October). #189 is separate unreachable-patient recovery. The verified 31 October package (`f0a86b0ec`) was offered to the reporter; qualify the fresh lift sequence in-game. The fix does not repair already-stuck saves, which still need a pre-lift save. |
| [#185: GOG offline installation on macOS](https://github.com/themuffinator/openQ4/issues/185) | PLAT-04/REL-01: provide extraction/path guidance and qualify the reporter's installer/package combination; do not imply that openQ4 imports Windows installers. |
| [#186: subtitles](https://github.com/themuffinator/openQ4/issues/186) | AUD-07/UI-02/UI-04: define supported dialogue text, timing, localization and accessible display coverage. |
| [#187: dialogue volume](https://github.com/themuffinator/openQ4/issues/187) | AUD-06/UI-01: independent spoken-dialogue gain with real mixer classification and persistent settings. |
| [#173: high-DPI menu cursor PR](https://github.com/themuffinator/openQ4/pull/173) | UI-02/PLAT-05: preserve controller-to-mouse handoff and console ownership; requalify against current routing. |
| [#180: GUI save migration PR](https://github.com/themuffinator/openQ4/pull/180) | Q4-05/Q4-07/UI-09: landed on main in `2677fb87b` (29 October) and closed on 31 October; main's push verification at `b9c663758` passed on every platform. Contributor Linux restore/update evidence is retained; Windows, macOS and ARM64 historical-save qualification remains a distinct gate. |
| [#189: medic approach watchdog PR](https://github.com/themuffinator/openQ4/pull/189) | Q4-02: bounded stall recovery, SP/MP parity, restore behavior and normal heal/progression regression; does not by itself prove #184 resolved. Merged on 31 October in `fcaaeb198`, with the `release-completion.md` conflict resolved by keeping both entries. The SP and MP objects compile cleanly on Windows, the watchdog contract passes, and main's push verification at `f0a86b0ec` passed all eight jobs. |
| [#190: numeric clipboard harness PR](https://github.com/themuffinator/openQ4/pull/190) | CORE-04/GOV-02: mock-interface fix merged; the subsequent atlas math-include fix passes locally. Complete fresh CI without suppressing checks; neither fixture fix alone qualifies the full product. |
| [#191: parallel validation PR](https://github.com/themuffinator/openQ4/pull/191) | GOV-02/CORE-04: source review approved; 16 of 18 checks passed at `a721db93`. Main has since fixed the three locked-harness cleanup failures (`9fe6f4d89`) and moved the Python checks out of most build jobs (`91aaab963`). Merged on 31 October in `0d29323fb`. Both workflows' script-smoke jobs keep main's exact 429-command list, now fed through `run_parallel_commands.py`, and `validation_hardening.py` passes. At `f0a86b0ec`, script-smoke passed in 20.0 minutes (428 parallel checks in 1,111 s), against 76.8 minutes at `b9c663758`. |
| [#192: weapon GUI events prevent save loading](https://github.com/themuffinator/openQ4/issues/192) / [#193: backlog recovery PR](https://github.com/themuffinator/openQ4/pull/193) | Q4-05/Q4-07: #193 landed on main in `2677fb87b` (29 October) and was closed on 31 October. Contributor Linux synthetic-save evidence demonstrates the queue defect and recovery. The reporter's affected save has been requested, and the verified 31 October package (`f0a86b0ec`) was offered; qualify their failure before closing #192. |
| [#198: Turbo scripted-mover timing PR](https://github.com/themuffinator/openQ4/pull/198) | Q4-02: landed on main in `2677fb87b` (29 October) and closed on 31 October. Plain scripted movers keep their authored timing while doors and platforms stay fast; the #184 save-recovery limitation remains explicit. |
| [#201: Windows ARM64 harness cleanup PR](https://github.com/themuffinator/openQ4/pull/201) | GOV-02/CORE-04: closed on 31 October as superseded by `9fe6f4d89`, which retries short-lived Windows executable locks during fixture cleanup. Main's Windows ARM64 push verification at `b9c663758` passed `openq4-imported-game-regressions` and all 305 native tests. |
| [#203: complete push-payload history](https://github.com/themuffinator/openQ4/pull/203) / [#205: packaged saves rejected as build 1](https://github.com/themuffinator/openQ4/issues/205) | REL-01/Q4-05/GOV-02: source review approved at `e178a61b`; the exact build-history regression and all three shallow-checkout negative controls pass on Windows. Main's payloads at `b9c663758` passed CI but still used a single-commit checkout, so they also report build 1 (the real count is 1905). #203 merged on 31 October in `18117f896`. #205 closed on 31 October: the Windows x64 payload from push verification at `f0a86b0ec` (artifact 13033914500) reports build 1920. In SP on `game/airdefense1`, its save committed with header build 1920, format 4, and loaded back into gameplay in a fresh profile. The replacement was linked from #168, #170, #183, #184 and #192. The Linux, macOS and Windows ARM64 payloads were not loaded. |
| [#204: missing music in signed macOS 0.14.000](https://github.com/themuffinator/openQ4/issues/204) | PLAT-04/AUD-01: the report uses MacSourcePorts' signed build of withdrawn 0.14.0. The reporter recalls music and console ambience also missing in 0.13.2; their log has been requested. Their Hannibal opaque-window report matches the portal fade-distance fix `d361e62ea`, and the briefing-hologram decal report is to be filed separately. Signing does not establish a later engine revision. Preserve saves and package separation in any comparison; 0.13.2 cannot read format-4 saves. |
| [26 October main verification](https://github.com/themuffinator/openQ4/actions/runs/40277373140) | GOV-02/CORE-04/PLAT-04: Windows x64, Linux x64/ARM64 and three macOS jobs passed. Windows ARM64 has the failures above; macOS Intel Metal timed out in renderer-foundation self-tests. The Windows payload subsequently proved unable to save because its shallow checkout produced build 1 (#205). Preserve those failures; passing CI did not qualify packaged save/load or the failed targets. |
| [29 October main verification](https://github.com/themuffinator/openQ4/actions/runs/40951670759) | GOV-02/CORE-04/PLAT-04: all eight push-verification jobs passed at `b9c663758`, including Windows ARM64 (305/305 native tests) and the Intel macOS OpenGL route. Its payloads still report build 1 (#205), so this run does not qualify packaged save/load. |
| [31 October main verification](https://github.com/themuffinator/openQ4/actions/runs/41175931810) | GOV-02/CORE-04/PLAT-04: all eight push-verification jobs passed at `f0a86b0ec`, the first run with #203, #189 and #191. Its Windows x64 payload reports build 1920 and passed the packaged SP save/load check (#205). |

## Current baseline to preserve

| Area | Current implementation and boundary |
| --- | --- |
| High refresh | Rendering is decoupled from exact 60 Hz scheduling. Camera, view weapon, eligible world transforms and skeletons interpolate; local multiplayer body presentation, ordinary joint modifiers, clock-phase correction and weapon GUI, muzzle-flash and flashlight lights are aligned. Alternate views, client debris and active ragdoll poses share the clock, and opt-in late look turns the drawn view toward unapplied input. Remote actors and the recorded exclusions in the [presentation owners contract](presentation-owners.md) remain separate work. |
| Game time | Ordinary game code advances in 16 ms steps. Network game timestamps deliberately use that same clock while scheduling uses the exact 60 Hz ladder. Do not apply a global 16-to-16.667 ms conversion as a presentation fix. |
| Vulkan | Stable supported on Windows x64 following the October NVIDIA/Intel qualification; experimental on Linux and macOS. The user-requested 28 October GPU policy makes fresh desktop `best` prefer compatible high-performance Vulkan hardware with OpenGL fallback; saved explicit choices persist. Android keeps GLES. This does not expand hardware qualification. Historical failed runs remain evidence for their specific builds and conditions. |
| Modern rendering | Native GL/Vulkan PBR is default-on for authored PBR materials; stock materials stay classic. Shared classic rendering domains exist but remain default-off. Aggregate modern-visible lighting still has `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS=0`; these are different promotion decisions. |
| Temporal rendering | Automatic TAAU supports subnative bilinear/bicubic scaling and dynamic resolution on eligible desktop GL/Vulkan paths; crop/nearest modes and VR are excluded. Rigid, skinned and first-person weapon motion vectors and reactive effect coverage exist. Dynamic resolution and GPU skinning remain opt-in. GLES has no temporal resolve. |
| VR | The active source implements OpenGL PC OpenXR, stereo, tracked input, room scale, crouch, two-handed aiming, haptics, comfort controls and vehicle views. Vulkan/EGL/Quest work is recoverable from archived history, not active implementation. Real-headset qualification remains open. |
| Android | Experimental ARM64/API 24+ GLES3 client, SDLActivity host, in-tree game modules, signed APK/release verification and dependency licensing are implemented. Device qualification and integration of archived onboarding/touch/lifecycle work remain open. |
| UI | Many retained screens and HUDs now run by default. The register preserves 350 rows, 311 active requirements and 39 superseded rows. UI-10 reconciled retail/base/Awakening mounts to 358 source/include-bound variants; all remain unaccepted for migration. Historical hashes and invalidated inventory acceptance are preserved. |
| Retail and Awakening | Canonical SP/MP sources and BSE are in-tree. Awakening is additive SP content under `q4xbase`, not a separate multiplayer module. Retail decl and BSE audits have already resolved extensive earlier gaps. |
| Multiplayer | Dual-stack networking, pure asset checks and bounded rcon2 exist. Actual MP defaults are `net_mpLagCompensation 0` and `net_mpPredictMode 0`. Clients advertise bounded remote extrapolation, and server rewind subtracts it from the displayed pose age; the coupled implementation and remaining peer qualification are recorded under MP-07. Retail 2.85 interoperability is outside the current openQ4 2.41 protocol contract. |
| Platforms | Windows x64 and Linux x64/Steam Deck are first-class platform targets; macOS arm64 and Linux ARM64 are previews; Windows ARM64 and Android are experimental. Renderer tiers are separate from platform tiers. |
| Foundations and tools | The bounded job service, worker DDS prefetch, generated-cache experiments, native dmap/AAS commands, save-format compatibility readers and experimental map workspace already exist. Their remaining scope is defined below. |

## Delivery sequence and dependencies

| Stage | Work to conduct | Exit condition |
| --- | --- | --- |
| 1. Establish the current baseline — complete | GOV-01 through GOV-03, HF-01/HF-02, Q4-01, UI-10; scope/recovery review for VR-01/VR-06 and AND-01 | Current-source ownership, clocks, compatibility inventory, tests and evidence identities agree. [Closure evidence](baseline-evidence.md) retains reproduced failures and later-stage qualification limits. |
| 2. Close product gaps — in progress | UI-01/UI-02 plus the first UI-06 editor round trip, UI-07 measurements and UI-09 toolkit work, then UI-03/UI-04 at accepted scope; Q4-02 through Q4-05/Q4-07, MP-01 through MP-08, AUD-01; selected VR-01/VR-06 and AND-01/AND-03 scope | Required player flows work through the intended UI and runtime; remaining unsupported cases are explicit and fail safely. |
| 3. Improve motion and rendering | HF-03 through HF-08, VK-01 through VK-07, REN-01 through REN-08 | Visible gameplay meets declared correctness, latency and frame-time budgets with rollback paths intact. |
| 4. Complete tools and broader quality | Remaining UI-06 scope, CORE-01 through CORE-06, remaining P2 items; TOOL-01 through TOOL-04 only when the paused map-editor work resumes | Authoring and runtime workloads meet their stated completion gates; optimizations are justified by measurements. |
| 5. Qualify packages and promote scope | PLAT-01 through PLAT-05, REL-01 through REL-04, UI-07/UI-08/UI-11, VR-02, AND-02 and selected AND-05 scope | Exact release packages pass the selected platform, device, gameplay, visual and human acceptance matrix. |

Independent workstreams may proceed together. GPU/renderer optimization depends on
the current timing and workload baseline; UI cutover depends on its complete
requirements and migration evidence; support promotion depends on physical device
and final-package evidence. Optional P3 work never silently becomes a prerequisite
for shipping an already supported configuration.

These stages apply to each delivery increment, rather than deferring all testing
to a final project-wide phase. Important dependencies are:

| Work | Required predecessor or boundary |
| --- | --- |
| Full UI migration/cutover | UI-10 refreshed inventory and resolved scope; a real SYSTEM editor round trip and RmlUi decision at `GATE-003`; shared toolkit/editor qualification at `GATE-004`; UI-11 evidence-controlled acceptance. Working screens may continue to improve before full-product acceptance. |
| Remote-player smoothing or lag-compensation promotion | HF-01 and MP-07 must define the displayed remote pose, extrapolation and server rewind together. Do not enable both corrections independently. |
| Vulkan/Quest VR integration | VR-06 fault-safe lifecycle and current renderer/module contracts; Android variants also need AND-01 packaging/verifier work. Existing GL headset qualification can proceed independently. |
| Android sustained operation | AND-02 includes AUD-03/CORE-03 aggregate memory and cancellation behavior, not only renderer FPS. |
| Modern-renderer promotion | Each REN-01 shared-domain gate is separate from REN-02 aggregate visible-lighting ownership. Keep their defaults and rollback decisions independent. |

## Governance and baseline evidence

- [x] **GOV-01 — P0, qualification: reconcile current scope and defaults.** Audit
  current source, capability/support claims and remaining historical findings.
  Classify each as reproduced, resolved, intentionally unsupported, qualification
  pending or optional. Preserve old failure records without presenting them as
  current failures. Correct stale reference/player documentation against the
  actual shipped defaults, beginning with multiplayer prediction/lag compensation
  and audio distance units; a documentation claim must not drive an unqualified
  default change. Close when every supported configuration and promoted default
  has an identifiable current evidence owner. Completed: the
  [configuration/ownership table](baseline-evidence.md#current-defaults-and-accountable-workstreams),
  capability/platform matrices, multiplayer guide and audio contract now agree
  with source; no unqualified default or support tier was promoted.
- [x] **GOV-02 — P0, implementation and qualification: recover missing regression
  coverage.** Review the imported game-history tests, beginning with
  `presentation_interpolation_contract.py`, now restored in this checkout.
  Restore/adapt relevant contracts to in-tree SP/MP and add executable clock/state
  tests where source-token checks are insufficient. Preserve licensing/provenance.
  Map the missing historical tests to current coverage first; restore behaviors
  that are untested, not obsolete filenames or duplicate token assertions. Verify
  test discovery, native suite registration and CI actually execute the checks.
  Close when validation runs those checks without either archived companion repo.
  Completed: [83 historical entries mapped](game-test-coverage.md), 74 relevant
  Python contracts/compiled fixtures recovered, production native clock and
  platform probes registered with Meson and push/PR CI. The current Windows
  Meson run passes all 184 tests; Linux portability/checksum probes pass in WSL.
- [x] **GOV-03 — P0, qualification: bind results to the actual workload.** Record
  source revision, staged binary hashes, backend/device/driver, map/entity filter,
  camera, settings, capture mode and sample interval. Keep hidden offscreen,
  visible-window, software-rendered and physical-device results distinct. Retain
  failed reports alongside reruns. Use current benchmark and support-evidence
  machinery instead of creating incompatible parallel acceptance formats.
  For timing comparisons declare warmup, sample size, run ordering, measurement
  overhead and acceptable noise/tolerance before testing; retain distributions
  and failures rather than promoting a single favorable average. Completed:
  [baseline workload/timing policy and report hashes](baseline-evidence.md)
  retain clean-source runtime identities, original media, overlays, cameras,
  capture modes and failures using the existing harness formats. The current
  visual controls make no before/after performance claim.

## High refresh presentation and input

Sources: [historical high-refresh scope](archive/2026-10-18/high-framerate-rendering-plan.md),
[temporal presentation](temporal-presentation.md), and current
`src/game/Game_local.cpp`, `src/framework/async/AsyncServer.cpp` and
`src/framework/UsercmdGen.cpp`.

- [x] **HF-01 — P0, decision and qualification: specify clock ownership.** Preserve
  exact scheduling versus the current 16 ms game/snapshot clock unless a separate
  gameplay-timing project is accepted. Specify wall, usercmd, game, prediction,
  pose, material/effect, UI, audio and capture time; cover slow motion, competitive
  pause, ordinary SP pause and Arena freeze. Test long-run timestamp agreement,
  event ordering and unchanged gameplay across presentation caps. Completed:
  [clock ownership contract](presentation-clock.md), one-million-tick production
  scheduler/SP/MP/network agreement and deterministic event/state fixtures across
  presentation caps. Full game replay and physical pacing remain HF-06/HF-07.
- [x] **HF-02 — P1, implementation: improve fractional presentation precision.**
  Replace or justify millisecond-quantized draw-phase sampling using one
  high-resolution monotonic tick-phase source and one frozen scene sample. Keep
  established integer timestamp APIs at their boundaries. Test 16/17 ms scheduling,
  non-divisor refresh rates, jitter, catch-up, missed ticks, resets and clock wrap;
  verify no accumulated phase error or authoritative state mutation.
  Include VR smooth-turn/frame deltas and distinguish the host clock from XR
  predicted display time. Completed: shared monotonic fractional SP/MP policy,
  one frozen phase per scene, unchanged integer API boundaries and high-resolution
  XR host deltas. Native/pause tests and 256 live samples each in SP and MP pass;
  see the [clock contract](presentation-clock.md) and [evidence](baseline-evidence.md).
- [x] **HF-03 — P1, implementation: finish low-latency look presentation.** The
  optional `GetPresentationViewDelta()` sampler had no game-side consumer.
  Integrate a defined presentation-only path for mouse and applicable controller/
  gyro input, respecting sensitivity, zoom, filtering, recoil and UI ownership.
  Define displayed aim versus authoritative firing, prevent double consumption and
  keep collision/network commands authoritative. Measure input-to-display latency
  as well as smoothness; previous-to-current interpolation has a deliberate delay.
  The existing helper deliberately omits filtering; choose and test a filtered-
  versus-late-look policy. Define same-fire-frame muzzle/tracer/impact alignment
  for stock and `launchFromBarrel` weapons before enablement. Cosmetic correction
  must not alter authoritative traces, damage, projectile spawns, network events
  or RNG. Test rapid turns, zoom, movers and prediction recovery with identical
  authoritative command/event replay. Completed, opt-in (`in_presentationView 0`
  default): an absolute-angle engine sampler (game API 54) covering mouse, gyro,
  look keys and sticks without consuming input, and one frozen per-draw SP/MP
  correction that turns only the drawn eye and what it holds. Filtered input keeps
  the authoritative view; aim, firing alignment and exclusions are defined in the
  [presentation owners contract](presentation-owners.md). The native pipeline
  test, mutation-checked contract and live SP probe show input consumed once
  (turn rate within 0.03% in both modes), the drawn view equal to the sampled input
  within 0.007° and about 16 ms of delay removed. Live MP, zoom, mover and
  prediction-recovery runs and physical latency moved to HF-08.
- [x] **HF-04 — P1, implementation and qualification: close remaining visual
  ownership gaps.** Inventory muzzle-flash lights, independent weapon effects,
  unbound client models, world BSE, trails, debris, fractures, active articulated
  figures and stateful joint modifiers. Reuse completed GUI-light, bound-effect and
  projectile fixes. Implement bounded draw-only owners with spawn/age/sound events
  executed once, or record a precise release exclusion. Test moving/rotating
  carriers, attachments, terminal projectile states and restore/reconciliation.
  Completed: draw-only owners for the view muzzle-flash light and flashlight,
  unbound client debris, brass and gibs with their trails, and active AF
  (ragdoll) poses blended between their two authoritative samples; every other
  category has a recorded disposition in the
  [presentation owners contract](presentation-owners.md). Live SP counters show
  99% of ragdoll draws blended and thrown barrel debris interpolated, with no
  errors; all new state is transient. A by-eye review moved to HF-08.
- [x] **HF-05 — P2, decision and implementation: alternate views and remote
  actors.** Define cinematic, private/security, third-person, spectator and nested
  camera smoothing with cut/discontinuity rules. Keep remote-player interpolation
  separate from the generic entity pass until snapshot smoothing, server rewind
  and visible aim agree. Local-body admission is already implemented and needs
  regression coverage, not reintroduction. Qualify vehicles and joint-bound cameras.
  Depend on MP-07's prediction/rewind contract before changing remote poses.
  Completed: cinematic and private cameras, third person (on foot and in
  vehicles) and the death camera are sampled once per tick and drawn on the scene
  clock. They restart on authored cuts, camera or kind changes, skipped ticks,
  jumps and non-interpolable carriers. Decision: while the local client plays,
  remote bodies stay on the tick, because MP-07 rewinds by the displayed pose age
  and a draw-phase delay would need a protocol change. A spectating client,
  which fires nothing, draws remote bodies, the followed player's third person
  and the death camera on its camera's clock. Both rules are contract-pinned
  beside the local-body admission. Vehicle cockpit eyes are bounded to under
  0.05 units against their interpolated cockpit, and nested monitor views keep
  their static cameras; see the
  [presentation owners contract](presentation-owners.md). By-eye review of
  cut-heavy cinematics and cockpits is HF-08.
- [x] **HF-06 — P1, qualification: preserve cross-system presentation.** Exercise
  TAA history/motion vectors, shadow caches, BSE multi-view servicing, retained UI,
  demo/AVI clocks, audio, VR and save/map resets together. Verify repeated draws and
  stereo/subviews do not rerun Think, prediction, scripts or effect events and that
  presentation buffers remain valid for their actual renderer lifetime.
  Account for intentional current bypasses: `PreparePlayerSceneForRender` skips
  demo/timedemo playback, altered tic rates prevent fractional animation, and
  noninterpolated cameras return the scene to authoritative time. Define game/
  network-demo, render-demo and timedemo behavior separately; preserve deterministic
  playback/benchmark meaning before extending smooth presentation to those paths.
  Progress: per-system behavior, including the separate game/network-demo,
  render-demo, command-demo and timedemo rules, is defined in the
  [presentation owners contract](presentation-owners.md); new owners are transient
  and contract-tested against Think, physics, sound and effect creation. A
  diagnostic guard now counts event dispatch, script threads, entity physics and
  effect spawns that start inside a prepared draw, and the runtime harness fails
  on any. Effects BSE starts while the renderer services it (particle impacts,
  nested segments; once per particle or effect step) are counted apart. Every
  SP and MP case on 24 October counted none. A session that records a render
  demo under late look, then plays it back with `timeDemoQuit`, captured and
  played back cleanly. Stereo draws from the in-tree OpenXR test runtime (2,340
  frames) and a remote-camera monitor drawn in every frame also counted none.
  Completed: one session ran temporal AA, shadow maps, late look, retained UI
  and audio together, through a save and load, a `vid_restart` and an AVI
  capture. All four captures recorded 256 interpolated late-look draws, with no
  gameplay in any draw and no errors, so presentation state reseeds across map
  resets and renderer restarts. Frames taken mid-turn under TAA and late look
  show no ghosting. Viewing in motion on the reference display and a physical
  headset run move to HF-08 and the VR track.
- [ ] **HF-07 — P1, qualification: establish supported frame-rate behavior.** Declare
  target hardware/scenes/settings and latency/p95/p99 budgets for 240 Hz (4.17 ms
  frame interval); retain 360 Hz (2.78 ms) as experimental until qualified. Include
  30/60/120/144/165/240/360 and uncapped, VSync/VRR where available, low-FPS catch-up,
  menu/loading/background policy and preset/Steam Deck overrides. Verify physical
  motion and pacing; a cap, average FPS or hidden capture alone cannot close it.
  Added reference, 27 October: native 2560 x 1440 fullscreen `game/airdefense1`
  starting-position rotations with the Remastered HUD and revised Ultra settings
  must keep **every frame <=6.67 ms**. The first complete maximum-quality Vulkan
  run with all effects and four cascades averages 38.5 FPS with a 37.55 ms
  maximum on Vulkan (OpenGL: 30.9 FPS / 69.85 ms); qualification remains open.
  Earlier MSAA-off measurements do not qualify this reference. See the
  [airdefense checkpoint](ui/runtime-performance.md#airdefense-maximum-quality-reference-27-october-2026).
  Progress: targets, budgets and the measurement procedure are declared in the
  [presentation owners contract](presentation-owners.md), with
  `presentation_look_runtime.py` for engine-side latency and smoothness; its
  `pacing` case runs the cap matrix and judges 240 Hz against the budget, but
  only operator-declared `--reference` runs qualify. An occluded window on a
  loaded machine presented 57 Hz (GL) and 15 Hz (Vulkan) at a 240 cap. A later
  engine-side matrix held 30/60/120/144/165 and reached 189–204 Hz at 240 (p95
  7.2–8.5 ms). On that laptop (hybrid graphics, 240 Hz panel) preparing a draw
  costs 0.35 ms of a 5 ms frame, and vsync settles at 138 Hz. The declared
  frame-rate policy (hidden-window, loading and preset caps) is contract-pinned,
  and a hidden window capped at 240 measured 60.0 Hz. Through 100 ms stalls
  (`com_presentationStallProbe`) the drawn view never stepped backwards and late
  look still matched its sampled input. The main menu follows the cap. Variable
  refresh and adaptive vsync are reference-run cases. On 25 October the frame
  cap began sleeping on SDL's high-resolution delay and busy-waiting only its
  measured oversleep (`com_preciseFrameWait 0` keeps the old wait): at 144 Hz
  the old wait woke about 1 ms late at p95 and the new one was not late, and at
  60 Hz the busy-wait halved. Windows 11 now keeps the 1 ms timer behind other
  windows. Hidden and minimized windows present at most 60 Hz on every platform
  (a minimized client held 59.5 Hz). Frame pacing statistics moved from whole
  milliseconds, which read a steady 240 Hz cadence as p95 5 ms, to 50 µs bins.
  The report gained the display refresh and names what holds the rate, and the
  overlay judges the 240 Hz budget. The limiter's late-wake count no longer
  includes frames that overran. Vulkan can now time when frames reach the
  display (`r_vkPresentTiming`, `VK_KHR_present_wait`): at 144 Hz here, frames
  drawn at p99 13.5 ms were presented at p99 22.1 ms, a gap draw-side numbers
  cannot show. `presentation_reference_suite.py --reference` runs the whole
  reference procedure (both renderers' cap matrices full screen, vsync,
  adaptive vsync, variable refresh under the reported refresh, the policy cases,
  the sensor session and the review) and writes one summary. A windowed
  rehearsal of the whole suite on 26 October (32 cases) found and fixed three
  faults in the procedure: slow windows reported nothing, image regeneration
  outran the per-case timeout, and the summary dropped cases it could not judge.
  Physical runs on the desktop reference remain.
- [ ] **HF-08 — P1, qualification: late look and alternate views in play.**
  Run late look in live MP (listen host and remote client, prediction recovery),
  zoomed, on lifts and trams and through rapid turns, and measure physical
  input-to-display latency on the HF-07 reference before any change to the
  `in_presentationView 0` default. Review cinematic, security-camera, third-person
  and death-camera smoothing and cuts, ragdolls, debris, the view lights and
  temporal AA under late look by eye. Engine-side evidence alone cannot close it.
  Progress, 24 October: loopback listen-host and remote-client runs consumed
  input once with late look off and on (−220.00 ±0.01°/s) and drew the input
  sampled for each draw (p95 0.0001°), removing about 16 ms in both. The
  frame-to-frame residual fell from 0.26° (host) and 1.17° (client) to 0.08°.
  Spectating and a walker cockpit entered by script were also exercised (the
  cockpit is linear within each tick to 0.0001°); see the
  [presentation owners contract](presentation-owners.md). Zoomed (railgun,
  50% sensitivity), riding a lift and a remote client losing 5% of its packets
  showed the same: input consumed once, the drawn view equal to the input
  sampled for the draw, and about 16 ms removed. So did a remote client under
  40 ms latency, 10 ms jitter and 5% loss each way, confirmed by `netImpairment`
  after the join. Rapid turns at 2200°/s consumed input once and cut the
  frame-to-frame residual from 4.67° to 0.09°. Physical latency now has its instrument:
  `com_latencyFlash` paints the first frame after a fire press for click-to-photon
  sensors (procedure in the owners contract). Late look stays excluded in vehicles by decision, because
  vehicles turn under their own rate limits (a walker turned 196°/s for a
  220°/s input). Q4's trams are vehicles (the ridden `player_tram` is a
  marine-driven tramcar), so the same decision covers them. Physical latency and
  the by-eye review remain. Both now have engine-side support (25 October).
  `framePacingSnapshot` times each frame from the draw's late-look input sample
  and from a flash press to the swap, so a sensor reading separates into the
  engine's share and the display's. `presentation_look_runtime.py --scenario
  review` runs captioned review sessions (turns under TAA, firing through the
  cheat-gated `in_presentationViewProbeAttack`, third person, ragdoll and
  debris, the death camera, the opening cinematic and a monitor) and writes the
  operator's checklist. Its `sensor` case is the click-to-photon session: the
  operator presses fire with a sensor on the flashed edge, and the snapshot
  gives the engine's press-to-swap time for the same presses.

## Vulkan rendering

Sources: [Vulkan gap history](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md),
[frame pacing](vulkan-frame-pacing.md), [device selection](vulkan-device-selection.md),
[authored programs](vulkan-material-programs.md), [HDR](vulkan-hdr.md), and
[point-shadow evidence](vulkan-point-shadow-parity.md).

- [ ] **VK-01 — P1, qualification: extend the supported driver/platform matrix.**
  Preserve Windows x64 NVIDIA/Intel support and qualify AMD, physical Linux GPUs
  and MoltenVK on real Apple hardware separately. Cover stock SP/MP, restarts,
  device/surface loss, resize, captures, long sessions and final-package budgets.
  Software Vulkan CI remains useful regression evidence but cannot promote a
  physical platform or change `best` by itself.
- [ ] **VK-02 — P1, investigation and qualification: visible presentation pacing.**
  Reproduce the hybrid-adapter/power-state conditions using current foreground
  windowed workloads and unchanged budgets. Separate host scheduling, fence waits,
  GPU execution and display presentation. Hidden Vulkan now renders offscreen;
  use actual presentation evidence for queue/latency claims and retain the older
  failure conditions. Close with repeated direct/hybrid-adapter results meeting
  the declared budget, or a reviewed support-scope limitation tied to retained
  reproduction evidence. Merely recording an exception does not qualify a failing
  configuration as supported.
- [x] **VK-03 — P1, implementation: finish bounded device admission and recovery.**
  Audit fixed-size instance/extension enumeration, optional extension truncation,
  the explicit-device index limit and retry after later resource/device creation
  failure. Preserve explicit adapter selection. Exercise incomplete/error/capacity
  driver responses, automatic fallback, repeated restart and resource cleanup.
  Completed, 23 October: [complete inventories and adapter retry](vulkan-device-selection.md#complete-inventories-and-adapter-retry-23-october-2026-vk-03)
  replace every fixed-size or truncating list (probe layers/extensions, renderer
  instance and window extensions, the optional device-extension lookup, present
  modes, an eight-image swapchain cap) with complete, fail-closed inventories.
  Automatic starts resume on the next compatible adapter after a logical-device,
  swapchain or resource failure (bounded to four); explicit `r_vkDevice` choices
  never substitute and accept indices to 255. 107 native fake-driver cases, the
  production startup route against adapter doubles and hardware drills
  (`r_vkStartupFailureDevice`) on NVIDIA/Dozen/Intel cover it; three repeated
  `vid_restart`s in airdefense1 held Vulkan memory at the same blocks and
  allocations. Physical AMD, Linux and MoltenVK qualification stays under VK-01.
- [ ] **VK-04 — P1, qualification and targeted implementation: authored material
  parity.** Expand executable GLSL/ARB and VFS-override coverage across mixed and
  custom lighting, alpha/translucent receivers, animated models, texgen, decals,
  `_currentRender`/`_currentDepth`, offscreen views and reloads. Implement reproduced
  missing semantics; do not recreate the existing program translator/executor.
  Require visible paired GL/Vulkan results, bounded resources and safe rejection.
  Make rejection visible and intentional: current failed authored GLSL/ARB stages
  are skipped, while failed diagnostic translation of a stock pair retains its
  native path. Specify the supported subset and missing-stage/material/frame
  fallback policy; test appearance, diagnostics and recovery after reload. Existing
  per-program compile/allocation diagnostics should be retained, not recreated.
  Progress, 23 October: the [rejection, capacity and fallback policy](vulkan-material-programs.md#rejection-capacity-and-fallback-policy-23-october-2026-vk-04)
  is specified and implemented. Skips are per stage and reported once per program
  and reason; `reportShaderPrograms` lists state, diagnostics, draws, skips and
  stale versions. The 256-version table evicts frame-safe stale versions instead
  of filling for good, a full ARB table no longer ends the session, the
  per-frame draw and program-pipeline limits are raised, and capacity warnings no
  longer repeat every frame. Shader parameters with no Vulkan value follow GL
  (registers or default) instead of skipping the stage, and authored pipelines
  honour alpha-to-coverage. The [paired laboratories](vulkan-material-programs.md#laboratory-results-on-the-committed-build-23-october-2026)
  pass on a clean build of that commit: authored GLSL (20 controls), live reload
  and repair (16) and translated ARB (15), each within one display level of
  OpenGL with exact recovery images and no validation messages. Capacity and
  eviction limits, the authored-lighting laboratory and mod content remain open.
- [ ] **VK-05 — P1, implementation and qualification: internal HDR and complex
  view composition.** Extend complete-scene admission for remaining baked/material,
  portal, capture, nested and authored-post combinations. Verify radiance, alpha,
  image orientation, exposure, reset and whole-view fallback. Current production
  PBR is display-referred; internal linear HDR and HDR monitor output are separate
  capabilities. Share temporal/shadow acceptance with REN-03/REN-04 and HF-06.
  Progress, 23 October: [domain changes and late failures](vulkan-hdr.md#domain-changes-settling-and-admission-counts-23-october-2026-vk-05)
  now keep per-domain exposure, invalidate temporal history on a flip, settle a
  linear view that missed its output, and release the accumulation after a failed
  combine. `rendererVulkanHDRInfo` reports the main view's outcome and per-map
  admission counts. The GPU self-tests pass under validation. No declined
  combination was admitted; classic ambient blends were shown unsafe to admit.
- [ ] **VK-06 — P2, measured optimization: submission and resource lifetime.**
  Profile descriptor/pipeline churn, barriers, command recording, uploads and
  attachment reuse before changing them. Preserve GUI/transparency/post order,
  bounded caches and restart generations. Require matching output, resource
  plateaus and improved frame-time tails on a declared workload before promotion.
  Record decisions for indirect drawing/Hi-Z culling, parallel command recording,
  transfer/upload overlap and pipeline warm-up. Production currently uses one
  graphics queue; the timeline-semaphore bring-up test does not prove asynchronous
  production overlap. A measured decision to defer a mechanism is valid; adding
  it without a workload and correctness/benefit gate is not required modernization.
  Decision, 23 October (pipeline warm-up): deferred. The 6 October Intel Xe
  measurement of airdefense1 plus a full camera sweep created 33 pipelines in
  49 ms total (slowest 11 ms) from an empty cache and 4 ms (slowest 0.5 ms) from
  the persisted cache, which is written at shutdown and before each level load
  (`VulkanDevice.cpp`). Revisit only with a stock or mod workload whose
  first-encounter hitch exceeds that. The cache keeps one blob for the last
  device; switching adapters (including the new automatic retry) discards it.
  Indirect/Hi-Z, parallel recording and upload overlap still need their profile.
- [ ] **VK-07 — P2, qualification: capture and developer diagnostics.** Compare
  Vulkan debug views and engine captures with their GL contracts for color/depth/
  stencil interpretation, cubemap faces, nested targets, clipping/depth hacks,
  MSAA resolves and resize/restart. Preserve capability-limited line/point sizes
  and intentional asynchronous diagnostic readbacks. Require labelled frame
  identity, bounded deferred resources and no accidental current-frame wait or
  stale result presented as current. Use engine-generated captures throughout.
  Progress, 23 October: the [capture inventory](vulkan-captures.md) records every
  readback's timing and identity. `glReadPixels` no longer returns a cleared
  frame or a skewed, resized rectangle as success, failed captures are not saved
  as screenshots, hidden-window frames are readable whatever the surface allows,
  and stencil readbacks print their frame, age, view and target and are dropped
  after a failed submission. GL-contract comparisons for cubemaps, nested
  targets, MSAA and capture-time resize/restart remain open.

## Modern rendering engine

Sources: [modernization history](archive/2026-10-18/idtech5-modernization-roadmap.md),
[visible-light ownership history](archive/2026-10-18/plans/2026-08-16-modern-visible-lighting-ownership.md),
[capability matrix](engine-capability-matrix.md), [GPU skinning](gpu-skinning-modernization.md),
[PBR production evidence](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md) and
[shadow production audit](shadowmapping-final-audit-2026-09-05.md).

- [ ] **REN-01 — P1, qualification: promote shared classic domains individually.**
  Finish fresh-package GL/Vulkan acceptance for root GUI, in-world GUI, ambient,
  interaction, fog/blend, deform, subview, cinematic/post and special-frame owners.
  Cover stock fixed cameras, mapped/stencil/hybrid lights, dynamic/cutout casters,
  direct/dynamic mirrors, remote/x-ray/refraction views, nested color/depth/cubemap
  targets, `_currentDepth`, root post and render-demo/Raven effects. Require
  nonzero executed ownership, same-backend classic parity, visible effect deltas,
  complete zero-commit rollback and human/platform evidence for each domain.
- [ ] **REN-02 — P2, implementation and qualification: finish aggregate modern
  lit-frame ownership.** Inventory the remaining material/light semantics against
  implemented shared corridors; finish coherent whole-frame lighting and
  composition before changing parity masks, auto-promotion or core-profile defaults.
  Require meaningful visible lighting, complete stock fallback and measured CPU/GPU
  benefit. Diagnostic readiness is not proof that the new executor owns gameplay.
- [ ] **REN-03 — P1, qualification and implementation: shadow production scope.**
  Qualify point/projected/CSM, stencil/map hybrids, moments, movers, near emitters,
  dynamic/cutout/custom receivers and constrained budgets through full scenes and
  continuous motion. Audit point static/live composition and mapped dynamic-image/
  non-explicit-texcoord alpha cases; retain stencil fallback when coverage cannot
  be reproduced. Translucent moments already exist on Vulkan. Prove cache freshness,
  bounded memory, no missing/doubled receivers and retained light on fallback.
- [ ] **REN-04 — P1, qualification: temporal AA and dynamic resolution.** Exercise
  existing rigid/skinned/weapon exact vectors and reactive surfaces through
  interpolation, disocclusion, pause, cuts, failed presents, capture, multiple
  views and retained UI. Test delayed/stale GPU timing and resolution recovery.
  Require stable quality, separate native UI, valid previous-frame identity and
  immediate current-frame/SMAA fallback. Per-eye VR temporal work remains VR-05.
- [ ] **REN-05 — P1, qualification: remaining PBR quality and portability.**
  Reproduce strict residual Intel SSAO and lavapipe scalar/texture differences and
  recorded cutout/normal diagnostic mismatches; fix or explicitly bound exceptions.
  Qualify authored mixed-material mods, skin substitutions on packed MD5R, physical
  Linux and Apple fallback. Preserve stock appearance. Box probes, probe capture,
  animated/MD5R PBR and indirect-only SSAO are implemented and need regression
  evidence rather than another implementation phase.
- [ ] **REN-06 — P2, qualification: probes, baked data and advanced effects.**
  Qualify probe capture orientation, generated-grid provenance and whether existing
  baked grids need regeneration after capture fixes. Broaden authored probe/decal
  scenes and resource captures. Validate default-off froxel, SSR and SSGI leaves
  individually and together with exact master-off rollback and bounded work.
  Keep their screen-space approximation limits explicit; classic decal routing
  does not require an invented Vulkan clustered-decal port.
  Qualify actual probe-source admission: the Vulkan loader requires immutable,
  nondefault cubemaps within the current 256-face-size ceiling, with supported
  authored RGBA8/generated RGBA16F storage. Test incomplete sources, storage/upload
  generations, reload and atlas exhaustion. Measure the synchronous upload-flush/
  readback path on source-cache misses and set cold/reload latency and memory
  budgets. Decide explicit support or exclusion for compressed, authored-HDR and
  oversized inputs; an HDR generator does not establish a general HDR loader.
- [ ] **REN-07 — P2, measured optimization: remove demonstrated CPU/GPU stalls.**
  The 28 October [preset audit](ui/performance-presets.md#preset-and-detection-audit-28-october-2026)
  adds Enhanced between Balanced and Quality and replaces host-wide memory
  ranking with active-device, shared-memory, resolution and CPU-aware detection.
  The follow-up applies the same policy on first launch, preserves existing
  configs/launch overrides, caps Android conservatively and intersects platform
  hints with verified memory limits. The seven-level ladder is sufficient.
  Further review separates Apple GL 2.1's optional post-effect limit from classic
  material capability, keeps failed SDL RAM queries unknown, and prevents repair
  of an unset legacy classification from overwriting saved graphics settings.
  The drawable follow-up also bounds automatic scene scale on high-resolution
  UMA/mobile displays, disables unavailable post AA, and routes stock quality
  commands through the canonical profiles. See the same audit for policy and
  validation limits; manual quality levels remain unchanged.
  Preset/runtime checks are separate from the still-open frame-time gate.
  The [backend/device follow-up](ui/performance-presets.md#rendering-backend-and-device-selection-28-october-2026)
  groups renderer and stable GPU selection with the presets, prefers compatible
  discrete hardware, and checks active-device detection after a menu restart.
  Windows NVIDIA/Intel and simulated cross-platform selection coverage do not
  close the physical Linux/macOS/Android or frame-time gates.
  As requested on 28 October, subsequent airdefense qualification uses Quality
  (one preset below Ultra), native 2560 x 1440 fullscreen and the Remastered HUD.
  Disable the preset's 144 FPS cap for measurement; retain the strict per-frame
  150 FPS floor and checkpoint-stall requirement. Ultra remains maximum quality;
  the maximum-quality measurements below are historical, not the current gate.
  Current airdefense work adds strict full-window pacing evidence, cached-depth
  particle sorting and retained HUD vertex/frame-query reductions. Maximum
  quality currently remains GPU-bound; no frame-rate gate is closed.
  Bounding Vulkan shadow clears to occupied fresh blocks saves roughly 2 ms
  in paired native fullscreen rotations, with pixel-identical frozen captures.
  Avoiding unused post-process depth transitions and redundant MSAA capture
  blits saves a further roughly 0.1–0.2 ms and 0.8–0.9 ms in paired probes;
  render-target/HDR checks pass. A stale backend-view pointer found by temporal
  restart testing is fixed, and temporal/probe-source restart checks pass.
  Fresh opaque shadow ownership depth reuse saves another 0.38 ms on average
  across five matched rotations; 100 atlas/tile resize cycles remain bounded.
  Sampling the exact PCSS ownership depth union then removes the copy, saving
  2.53 ms across five matched rotations. Four view pairs are RGB-identical;
  Vulkan validation and another 100 atlas/tile resize cycles pass. Individual
  frame spikes still exceed the target, so this does not close the strict gate.
  Default-cache GPU diagnostics now isolate map production at 6.715 ms mean
  versus 1.824 ms for remaining lighting; 1,024 nested timing samples pass Vulkan
  validation. Zero-light receiver sampling and save-scalar inlining showed no
  useful gain and were removed; retain their evidence rather than repeating them.
  Omitting unused shadow stencil attachments saves 1.220 ms across five matched
  rotations, with validation and 100 bounded atlas/point-size cycles plus restart
  passing. Native maximum-quality qualification reaches 52.55 FPS mean with a
  29.82 ms worst frame; the strict floor remains unmet. Point-cube depth copying
  was slower in every pair and is removed.
  Expanded GPU attribution measures 15.008 ms in the main view and 2.269 ms in
  portal-sky rendering; SMAA is only 0.243 ms. All 1,024 frame masks and nested
  intervals pass Vulkan validation. Precomputed SSAO spiral offsets showed no
  useful speed gain across five pairs and were removed. These diagnostics do
  not supersede the strict qualification or close the performance gate.
  Conservative portal-sky discard matches four frozen views exactly, but five
  paired rotations improve the mean by only 0.21%, below repeat variation, and
  worsen the largest spike. The prototype and optional extension were removed.
  The restored-build native qualification records 53.35 FPS mean, 26.40 ms p99
  and 32.79 ms maximum; workload gates pass and all performance gates still fail.
  Finer GPU attribution measures 4.400 ms in projected-map production versus
  0.914 ms in point cubes, with 1,024 validated nested intervals. A conservative
  local-caster bounded-clear experiment preserved frozen-view/control parity
  but was 0.204% slower across five pairs and is removed. Prioritize projected
  map generation without reducing the quality contract.
  Caster depth ordering and compatible indirect batches also fail five-pair
  whole-frame comparisons (0.034% and 0.100% slower); both are removed.
  Optional pipeline counters now measure 143.5 million fragment invocations
  per frame in fresh projected maps. Stationary per-cascade captures locate
  most of that work in LOCAL cascades of perspective light 0; counter-enabled
  and disabled video-restart tests pass. An opaque-only shader specialization
  is 0.606% slower over three pairs and worsens p99/max in each pair, so it is
  removed. The counters remain default-off; no performance gate is closed.
  Receiver-bound diagnostics cover 100% of every cascade in all 76 samples,
  so they do not justify a simple receiver-driven rectangular crop.
  A fixed 32-call SSAO sequence also regresses the five-pair mean by 0.591%
  and is removed. No quality setting is reduced by these investigations.
  Read-only hardware sampling confirms sustained 100% GPU utilization at
  2550 MHz with no active clock-event flags in 55 busy samples; the repeated
  1,024-frame GPU profile is 18.378 ms. Investigate larger GPU-work reductions;
  these results do not support a device-setting change or close any gate.
  Checkpoint work now buffers the live snapshot, defers GL/Vulkan thumbnail
  readback, batches scalar/dictionary writes and writes bounded payloads and
  descriptions through the existing job/durable-file
  service. Reusing the joined worker's verified CRC reduces completion validation to
  about 0.2 ms. A save-only compiled-script checksum cache removes roughly 3 ms
  of repeated hashing; fresh-reference and compile/reload invalidation probes pass.
  Preview persistence/readback now also runs on a bounded worker, reducing its
  main-thread completion phase to about 0.3 ms; lifecycle, blocked-preview and
  native persistence-failure checks pass.
  Optional dictionary profiling now attributes 2.82–2.99 ms of warm snapshots
  to about 3.94 MB of dictionary data. Both production writers preserve byte
  parity with profiling enabled/disabled; Vulkan load/quit/reopen, corruption
  rejection and failed-payload preservation pass. Two scanning-based cache
  prototypes regressed and were removed. Immediate mutation-observer population
  also regressed first saves, so population now runs in bounded later game frames.
  Deferred reuse is enabled after 4,774 live byte comparisons, ten fresh-session
  saves without a measured cold penalty, and normal/reversed warm pairs showing
  a 15.42% mean reduction (13.56→11.47 ms). The largest observed population batch
  is 0.566 ms. Native mutation/lifetime/deadline/byte bounds and SP/MP wire tests,
  plus default-path lifecycle/corruption/failed-preview checks, pass. Checkpoint
  windows still exceed the target; worst-frame and stall-removal gates stay open.
  Moving the final snapshot copy to a worker cuts handoff means from 1.94 to
  0.19 ms, but normal/reversed pairs regress total work and window p99; that
  experiment remains disabled. Native ownership/rejection/storage-failure and
  live load/quit/corruption/fallback checks pass. Transaction setup/commit and
  live-state serialization remain targets; a commit outlier reaches 19.6 ms.
  Legacy GUI attribution finds 43 duplicate writes per snapshot. A bounded
  per-save cache passes 430 GL/VK fresh-byte comparisons and SP/MP wire/failure tests,
  reducing combined normal/reversed mean serialization 13.66→9.92 ms. Total
  checkpoint work and window p99 regress, so it remains disabled. Commit detail
  separates 1.31–4.96 ms in file operations from 0.34–0.45 ms in notification;
  experimental background publication now preserves payload-last commit and
  interrupted rollback. Native fault/recovery/lifetime tests, live rename-denial
  rollback/reload and GL disabled-job fallback pass. Ten normal/reversed samples
  per mode regress total work despite cheaper commits. A further balanced
  20-save run reduces total work 29.11→23.57 ms with nearly equal serialization,
  but window p99 medians regress; the switch remains disabled pending a
  consistent whole-frame gain. Combining publication with GUI reuse also fails:
  mean window p99 rises 29.22→34.73 ms despite lower total CPU work. A 32.69 ms
  setup outlier motivates finer preparation timing. Neither experiment closes
  the stall-removal gate. Fine timing identifies 1.85–4.30 ms of recovery work
  with developer/unbuffered logging, versus 0.08–0.17 ms with developer messages
  disabled. This is observer-cost evidence, not a product optimization; compare
  buffered-logging gameplay separately before promotion. That comparison still
  regresses mean p99 and the largest window, so both experiments remain off.
  Duplicate explicit-read developer traces are removed under the existing
  fs_debug policy: recovery now takes 0.09–0.21 ms in developer launches, with
  lifecycle/corruption/blocked-preview checks passing. Checkpoint windows still
  peak at 40.64/27.19/34.97 ms; this removes logging cost, not the entire stall.
  A retained Motion-validation candidate did not exercise the serialized GUI
  path and was removed; its apparent timing gain is explicitly invalidated.
  The latest staged default remains below the strict target at 53.68 FPS mean,
  27.65 ms p99 and 80.06 ms maximum, with native fullscreen/max-quality checks
  passing (`qualification-checkpoint-commit-vk`). The outlier remains in the
  result without an established cause; this rotation contains no saves.
  Damaged saves still fail independent load-time integrity checks;
  reload, immediate load/quit and failed-overwrite checks pass. Snapshot CPU
  time and first-use rendering still cause measurable hitches;
  keep the user's stall-removal gate open. See the
  [checkpoint measurements](ui/runtime-performance.md#checkpoint-save-stalls-27-october-2026).
  Audit hot copy-helper GL state queries, cluster allocation/binning, per-frame
  pose work, shadow invalidation, render-target generations and upload lifetimes.
  Reuse existing job/measurement infrastructure. Shadow uniform caching already
  exists. Require at least five matched performance runs, unchanged quality/order,
  no p99 regression beyond the predeclared measurement tolerance in GOV-03 and
  bounded resources through at least 100 resize/scale cycles.
- [ ] **REN-08 — P1, qualification and targeted implementation: complete post-
  processing combinations.** Qualify motion blur independently of TAA: it has its
  own history, limited rigid-opaque object vectors and subview/time-gap resets;
  existing gameplay benchmarks disable it. Cover skinned/weapon motion, mirrors/
  portals/remote views, frame caps, pause/slow frames and TAA/HDR/bloom/SSAO/scaling
  combinations on GL/Vulkan. Require no unrelated-subview history flicker, coherent
  effect strength and accurate requested/effective settings. Fix reproduced gaps
  or publish precise supported combinations, including Apple GL 2.1 offscreen/MSAA
  restrictions and Vulkan's effective 8x MSAA ceiling. Preserve unchanged gameplay.

## Virtual reality

Sources: [OpenXR implementation history](archive/2026-10-18/plans/2026-10-02-openxr-vr.md),
[player guide](../user/vr.md) and current
`src/sys/openxr/OpenXRSystem.cpp`.

- [ ] **VR-01 — P1, integration: review recoverable Vulkan/EGL/Quest changes.**
  Review `archive/worktree-wtq-quest-vr`, especially `0d83c14c4` (Vulkan),
  `f98a1d247` (headset rendering without desktop presentation), `4b5e0c4f9`
  (Wayland/EGL), `65aa9bd88` (Quest) and `9dcd121da` (world-GUI pointing).
  Reconcile selected changes with current renderer APIs, in-tree game code, UI and
  Android packaging rather than replaying the branch wholesale. Gate integration
  on GL/Vulkan test-runtime SP/MP/menu coverage, runtime GPU/device ownership,
  restart/session-loss recovery, validation-clean Vulkan and VR-off parity. Require
  continued headset submission when desktop presentation is unavailable; qualify
  native-Wayland Vulkan/EGL and the appropriate GLX fallback independently.
  Carry VR-06's lifecycle guarantees into each recovered backend/host change.
  Stage-one scope/recovery review is complete in the
  [XR/Android recovery review](platform-recovery-review.md); integration remains open.
- [ ] **VR-02 — P1, qualification: real headset gameplay and comfort.** Qualify
  available PC runtimes/headsets for refresh pacing, tracking latency, aim/grip,
  handedness, two-hand weapons, haptics, room-scale collision/MP prediction, crouch,
  seated vehicles, zoom, menus/HUD/world GUI and loss/reconnect. Exercise full combat
  and camera transitions. Synthetic stereo/test-runtime success cannot establish
  physical tracking, readable UI or comfort.
- [ ] **VR-03 — P2, implementation: standing space and floor calibration.** Define
  LOCAL/STAGE/floor behavior, recentering, seated/standing transitions and safe
  fallback when a runtime lacks stage space. Validate crouch/body alignment and
  room-scale bounds without double-applying physical movement.
- [ ] **VR-04 — P2, measured optimization: stereo frame cost.** Profile two-eye
  front-end work, shadows, particles, UI and loading on actual hardware. Share
  view-independent work while preserving eye-specific culling/pose/output and
  exactly-once effect servicing. Establish headset-specific CPU/GPU tail budgets.
- [ ] **VR-05 — P3, decision and implementation: per-eye temporal features and
  further interaction.** TAA/dynamic resolution require independent eye histories,
  motion vectors, resets and scale control before enablement. Hand presence and
  physical-object interaction require separate gameplay/comfort designs. Quest
  distribution and device qualification also depend on AND-01 through AND-05.
- [x] **VR-06 — P1, implementation and qualification: fault-safe XR ownership.**
  Reproduce the current `Check` → `DestroySession` → `ReleaseImages` teardown
  re-entry risk in `OpenXRSystem.cpp`: session loss can be reported before
  `frameOpen`/acquired flags are cleared. Make destruction idempotent and stop
  frame/input work immediately when its owning session is invalidated. Extend the
  existing test runtime with injected session/instance loss, partial acquisition,
  wait/release failures, malformed image counts/indices, failed swapchain
  creation/resize and tracking invalidation. Require bounded cleanup, no recursion
  or stale-handle calls, balanced image/frame ownership and a recoverable desktop
  path. Ordinary call-order tests and headset happy paths do not close this item.
  Complete for the active desktop OpenGL scope: deferred retirement, bounded
  image validation, idempotent teardown and input cancellation pass 19 native
  production-method cases on Windows/Linux and 28 simulated-runtime SP/MP
  gameplay cases. Original failures, exact package identities, visual captures
  and the 207-test native suite are bound in the
  [recovery evidence](platform-recovery-review.md#opengl-xr-lifecycle-closure-18-october-2026).
  VR-01 carries these guarantees into other backends; VR-02 retains physical
  headset/controller qualification.

## Android and mobile rendering

Sources: [Android build and packaging](android-build.md),
[port integration and credits](android-gles-integration.md),
[standalone host](../../mobile/android/README.md).

- [ ] **AND-01 — P1, integration: recover onboarding, controls and lifecycle work.**
  Review archived Android changes for touch controls (`486ecf735`), diagnostics/
  lifecycle (`3ce244cda`), background tic sleep (`819846f5d`), cutouts (`20766e8d1`),
  launcher/localized data preflight (`ba6d3af2e`, `c5845ca2d`, `2f1d2d681`) and
  emulator support (`023f0eeee`, `1484195c4`). Preserve newer release signing,
  verification, notices, existing usable assets during failed extraction and the
  retained UI. Accept each integrated feature independently with recovery tests.
  Select an explicit ABI/API/device/flavor matrix before integration. Current
  Meson/Gradle admit ARM64 phones and a separate debug-only x86_64 emulator
  package. Both use exact library/asset allowlists and coordinated native
  metadata; Quest still needs its own staging and variant checks.
  The [diagnostic emulator recovery](platform-recovery-review.md#android-diagnostic-emulator-recovery-20-october-2026)
  passes native/package builds, phone regression checks and silent SP save/load
  and MP movement/release on a current GLES3, 16 KiB x86_64 image. The API 24
  image tested exposes only GLES2 and is rejected before installation. Engine
  captures exposed touch/HUD overlap and corner-hit issues; the recorded follow-up
  APK fixes them for the default landscape layout and passes fresh SP/MP probes.
  Full editable/portrait layouts retain their separate acceptance.
  Preserve matching SDL/native versions, 16 KiB
  alignment, certificate identity and licensing. Reject test-runtime/debug payloads
  from published APKs; toolchain/API bumps require their own qualification.
  Stage-one archived-change review and explicit phone/emulator/Quest scope matrix
  are complete in the [recovery review](platform-recovery-review.md). Feature
  integration, flavor verification and physical-device qualification remain open.
  The [single-window startup/fatal handoff](platform-recovery-review.md#android-window-ownership-19-october-2026)
  is repaired and passes Windows/Linux production-method checks and the matching
  ARM64/API 24 cross-build. The [S25 check](platform-recovery-review.md#s25-startup-data-path-and-upgrade-check-19-october-2026)
  reaches Storage 1, captures the opening scene and writes a save; restoration
  exposes a retained-UI thread-binding failure. The
  [engine-thread binding repair](platform-recovery-review.md#android-ui-engine-thread-binding-19-october-2026)
  passes native Windows/Linux checks. The subsequent
  [S25 restore round trip](platform-recovery-review.md#s25-original-save-restore-round-trip-20-october-2026)
  loads the original Storage 1 save, writes/reloads another save and exits
  cleanly on the recorded timer/GUI-repair APK. The later export APK also
  [restores a copy from its backup ZIP](platform-recovery-review.md#s25-exported-save-restore-20-october-2026),
  saves/reloads again and exits normally. Broader campaign/lifecycle behavior
  retains its own device gates.
  [Background audio/input and SP resume](platform-recovery-review.md#android-background-audio-and-single-player-resume-19-october-2026)
  now run before SDL blocks and return live SP behind the pause menu. Native
  Windows/Linux recovery controls and both builds pass. The S25 touch check
  below also passes audible background/resume and paused SP return; sustained
  suspension, process death and resumed network sessions remain unqualified.
  [Background timer recovery](platform-recovery-review.md#android-background-timer-recovery-19-october-2026)
  adds 100 ms background waits and bounded catch-up at every Android timescale;
  production Windows/Linux cadence, freeze and shutdown checks pass. Physical
  power/suspension and resumed network sessions remain unqualified.
  [Back and fatal-console controls](platform-recovery-review.md#android-back-and-fatal-console-controls-19-october-2026)
  now route Android Back through menu/pause navigation, accept a single touch
  owner with release/cancellation rules and use density-scaled error controls.
  Native/JVM recovery checks and both platform builds pass; physical touch,
  gesture, readability and cutout qualification remain required.
  [Cutout/configuration recovery](platform-recovery-review.md#android-cutout-and-configuration-recovery-19-october-2026)
  now keeps the actual SDL surface clear of camera cutouts and retains the
  remaining child system/IME insets. Windows/Linux host checks and exact debug
  package verification pass; real rotation/density/input and portrait work remain.
  [Gameplay touch recovery](platform-recovery-review.md#android-gameplay-touch-recovery-20-october-2026)
  now integrates the standalone vector controls with independent usercmd motion,
  preserved quick taps, touch-driven weapon selection and contact cancellation.
  Windows/Linux behavioral checks, both native builds and exact APK checks pass;
  the debug S25 updates preserve all six existing save files. Scripted
  gameplay/save round trips and physical move/look/fire, tap/hold weapon
  selection and background/resume audio pass on the recorded S25 builds.
  The full editable layout/portrait contract and setup integration remain open.
  Broader device startup and the remaining host/input/lifecycle recovery are
  still open.
- [ ] **AND-02 — P1, qualification: real Adreno/Mali gameplay.** Cover the API floor
  and current Android, ES3 capability differences, stock SP/MP, saves, map/campaign
  transitions, materials/shadows/ETC2/EAC, cold/warm caches, audio and touch/gamepad/
  mouse/keyboard. Exercise focus, suspend, low memory, process death, rotation,
  density and cutouts. Retain thermal, memory, storage and sustained frame budgets.
  Include compressed input, pending decode, retained CPU PCM and uploaded OpenAL
  residency together. Qualify Android's own OpenAL provider/capabilities rather
  than inheriting desktop provider assumptions; depend on AUD-03/CORE-03 limits.
- [ ] **AND-03 — P1, implementation and qualification: player setup and upgrades.**
  Finish scoped-storage onboarding, retail-pak validation, localized insufficient
  space/extraction/retry messages, accessible logs and save backup/export. Validate
  interrupted onboarding, APK upgrades and signing-certificate continuity. Keep
  proprietary retail data outside the APK. Optional SigmaTouch/host dependencies
  require a compatible distribution licence, not merely an available licence.
  The [extracted-package repair](platform-recovery-review.md#android-extracted-package-repair-18-october-2026)
  now verifies a bounded per-file manifest on every launch and atomically repairs
  missing/truncated/mismatched files behind `.ready`. Windows/Linux production-
  Java tests cover interruption, replacement failure, low-space preflight and
  save preservation; a real 810 MB package passes selective repair. Full engine
  PK4 checks remain enabled. Broader provider acceptance and release-signed
  device-upgrade evidence keep AND-03 open. Android now preserves the host's
  data directory when retail
  packs are absent, with production Android/desktop selection tests. An S25
  debug APK update preserves save bytes, and the original Storage 1 save now
  passes a device restore/resave/reload round trip on the recorded timer/GUI
  package. The current export APK also passes an exported-save restore/resave/
  reload check; release certificate-continuity qualification remains required.
  [Early native diagnostics](platform-recovery-review.md#android-early-diagnostic-path-19-october-2026)
  now share the host's save/log directory, with production native/JVM recovery
  checks on Windows/Linux and both platform builds. The S25 now records a
  successful installed-data check and exports its earlier native logs through
  the system picker; broader device diagnostics remain open.
  The [retail verifier backend](platform-recovery-review.md#android-retail-verifier-backend-19-october-2026)
  now checks the complete required set, engine-compatible checksums and every
  entry's payload, with optional/language findings, bounded progress and
  cancellation. The local retail set passes; the setup entry point below now
  invokes it for imports and installed-data checks.
  The [document import backend](platform-recovery-review.md#android-document-import-backend-19-october-2026)
  now copies granted provider streams through verified temporary files, preserves
  existing destinations on failure and resumes from completed archives. Host
  recovery and provider-adapter checks pass. The setup entry point below connects
  picker/grant ownership and localized recovery; physical provider acceptance
  remains open.
  The [localized setup entry point](platform-recovery-review.md#android-language-and-data-setup-19-october-2026)
  now connects language selection, the system folder picker, read-only persisted
  grants, verified import/check, bundled-file preparation and explicit SP/MP
  launch. Progress/cancellation and retry preserve completed archives; native
  storage ownership blocks setup until the game actually exits. Four resource
  sets and Windows/Linux pipeline checks cover this increment. Release signing
  continuity and
  physical provider/lifecycle/accessibility/visual acceptance remain open.
  [Settings re-entry](platform-recovery-review.md#android-settings-return-21-october-2026)
  now opens setup without ending the native game and returns to the same session.
  Two emulator handoffs preserve map/save state; desktop visibility and
  native/JVM recovery controls pass. Physical navigation on the new APK remains
  unqualified, and storage operations still require the game to finish closing.
  [Touch controls setup](platform-recovery-review.md#android-controls-setup-21-october-2026)
  adds Default/Compact/Left-handed presets, size/sensitivity controls, a live
  look-speed test and detected-controller information. Drafts stay separate
  from applied settings, which reach the engine at startup/foreground entry.
  Native/JVM, negative controls and two emulator handoffs pass. Physical setup
  interaction, controller behavior and full editor/portrait acceptance remain.
  [Interface setup](platform-recovery-review.md#android-interface-setup-21-october-2026)
  previews the section 13.2 viewing profiles with independent UI and text size.
  Each Apply reaches `ui_retainedScale`/`ui_retainedTextScale` once at
  foreground entry, so later in-game size changes survive a return. JVM/JNI,
  native parser and mutation checks pass, and the emulator drives the page from
  Apply to the engine. Physical S25 interaction and the in-game Interface
  settings page remain open.
  The [save and log export flow](platform-recovery-review.md#android-save-and-log-export-20-october-2026)
  now prepares bounded ZIPs with per-file integrity records and copies them to
  new documents chosen through the system picker. It preserves original saves,
  includes release setup diagnostics and holds native storage ownership until
  provider cleanup completes. Localized cancellation/retry, Windows/Linux
  production-Java tests and exact APK packaging cover this increment. S25
  picker exports to Downloads pass independent ZIP/manifest checks with all six
  original save files unchanged; a copy from the ZIP restores on that same
  package. Cloud/removable providers, interrupted-process recovery and release
  signing continuity remain open.
- [ ] **AND-04 — P2, implementation and qualification: mobile UI and defaults.**
  Qualify touch targets, keyboard entry, safe areas, HUD/menu scaling, bindings and
  overlay configuration. Select mobile presets from device results; explicitly
  handle GLES feature limits rather than assuming desktop temporal/post parity.
- [ ] **AND-05 — P2, integration and qualification: standalone Quest.** After
  VR-01/AND-01, qualify real installation, assets, stereo gameplay, controller/UI
  interaction, comfort, session recovery and sustained refresh/thermal budgets.
  Decide sideload/store packaging policy. Keep emulator/test-runtime results
  separate from headset support and x86_64 emulator builds separate from publication.
  Require AND-01's flavor-aware package verification and VR-06 lifecycle tests.

## New user interface

The [visual specification](ui-visual-design.md),
[product register](ui/product-requirements.md),
[machine-readable requirements](ui/product-requirements.json) and
[migration manifest](ui/migration-manifest.json) retain the full product contract.
This roadmap adopts all **311 active requirements, 358 current source/include-bound
migration variants and seven final gates**. UI-10 reconciled retail/base/Awakening
mounts while preserving the historical 271-entry manifest. The denominator follows
effective source changes; it is not a fixed completion target. Scope contradictions
remain explicitly paired with UI-05 and cannot grant migration acceptance.
Archiving their original plans does not remove requirements,
renumber IDs or accept a GUI. Stable `product:`/`original:` references retain their
archived provenance; new scheduling belongs here.

- [ ] **UI-01 — P1, implementation: complete production settings and essential
  pages.** Finish GAME, CONTROLS, AUDIO, Load Defaults and Player Setup; complete
  preset and mixed-effect transactions before SYSTEM leaves its current opt-in
  scope. Demonstrate real effect/readback, persistence, cancel/revert, failed Apply,
  interrupted writes, restart, device/capability changes and stale-owner rejection
  in SP/MP. Localize every display string and preserve current working pages.
  Progress, 23 October: [external settings refresh](ui/external-settings-refresh.md)
  closes the stale selection recorded on 22 October after console
  `applyPerformancePreset`. Clean pages adopt current values; dirty drafts and
  unfinished numeric text remain protected, with explicit conflict feedback.
  Native and windowed SP/OpenGL/MP/Vulkan gameplay checks cover this subset.
  The remaining settings/effect/editor and platform qualification stays open.
  Progress, 25 October: the [AUDIO page](ui/retained-screens.md#the-audio-page-25-october-2026)
  is reconstructed in the retained interface with every stock row; each change
  applies at once and every value reads back from the engine. Later that day
  the [GAME OPTIONS page](ui/retained-screens.md#the-game-options-page-25-october-2026)
  followed with its 79 rows in eight sections, then the
  [CONTROLS page](ui/retained-screens.md#the-controls-page-25-october-2026)
  with its 49 bindings and key capture; the stock settings screen now arrives
  on no panel, so its buttons choose the retained pages. Progress, 26 October:
  the [SETTINGS page](ui/retained-screens.md#the-settings-page-26-october-2026)
  replaces the settings screen's level from the title and the pause, with
  Load Defaults' question; DISPLAY still opens the stock panel unless the
  SYSTEM page is on. Player Setup remains.
  Progress, 28 October: the SETTINGS hub and Load Defaults work stranded in
  the earlier UI worktree is integrated in the main checkout. SETTINGS from
  the Strogg pause now opens the hub rather than its Load Game document;
  returning preserves the correct pause family. Native navigation/fallback
  coverage and current windowed engine captures qualify this bounded change;
  Player Setup, the SYSTEM/editor round trip and broader acceptance remain open.
  Later that day, the [model-scene foundation](ui/document-format.md#model-scenes)
  adds the real animated renderDef host needed by Player Setup: typed root/head/
  weapon scenes, per-part skins and bounded native targets. Native suites and
  GL/Vulkan gameplay pose captures pass, including Vulkan resource resets;
  late fixture precache warnings and broader lifecycle/visual gates remain.
  Player Setup itself is still outstanding.
  Progress, 31 October: CONTROLS, GAME OPTIONS and AUDIO are now
  [sub-pages of the SETTINGS page](ui/retained-screens.md#the-settings-sub-pages-31-october-2026)
  in the main menu's construction (visual specification 1.29, `FLOW-068`),
  replacing the drafted full-screen panels: sections are tabs and no tab has
  more than two columns. The opt-in SYSTEM page followed the same day as the
  [DISPLAY sub-page](ui/retained-screens.md#the-system-page-as-a-settings-sub-page-31-october-2026)
  (GRAPHICS, SCREEN and IMAGE QUALITY tabs, generated by
  `tools/ui/retained_system.py`; visual specification 1.30).
  Portrait views of 3:4 and taller now stand each tab in one larger,
  scrolling column, and GAME OPTIONS' fine sliders take typed values
  ([portrait and typed numbers](ui/retained-screens.md#portrait-views-and-typed-numbers-31-october-2026)).
- [ ] **UI-02 — P1, implementation and qualification: text, input and accessibility.**
  Finish in-field IME/composition, shaping/kerning/fallback/styles, grapheme editing,
  caret/candidate placement, binding capture, accessibility exposure and contextual
  prompts. Qualify shipped languages, long labels/breadcrumbs, portrait/ultrawide,
  text scale/density, keyboard/controller/touch and hotplug. Keep physical input
  acceptance distinct from handler/ownership unit tests.
  Connect `NativeInputDriver`/`WindowsTextSession` to production translators,
  bootstrap, activation and disposal; they currently have tests but no normal
  construction path. Require native-message/SDL routing, focus/loss/recreation,
  held-source handling and a working fallback before claiming live IME support.
  Progress, 23 October: [focus-following prompts](ui/retained-screens.md#page-prompt-bars)
  give the pages' prompt bars Accept verbs that follow focus in the same frame:
  LOAD, DELETE, SAVE, PLAY and JOIN, from a button's new `prompt`, in every
  shipped language. Accept leaves the bar while no focused control can act.
  Contextual west/north prompts and the home screens' bars remain open.
  Progress, 24 October: [Prompt Style and Swap Confirm and Back](ui/input-routing.md#settings-rows-24-october-2026)
  are rows in Game Options > Controller, bound to `in_promptStyle` and
  `in_swapConfirmBack` in every shipped language (`INP-011`). The triggers now
  [page lists](ui/input-routing.md#list-paging-24-october-2026) a visible page at
  a time (PageUp/PageDown, Home/End) where a document declares no section events,
  and step a focused slider coarsely. Prompt glyphs for the triggers and remaining
  buttons, the right stick's detail scrolling and per-family controller runs remain.
  Progress, 24 October: the [Menu button](ui/input-routing.md#menu-button-24-october-2026)
  closes the menus and resumes play from the pause's Save, Load and Restart Level
  pages. Later that day it [applies pending changes](ui/input-routing.md#menu-button-24-october-2026)
  on the SYSTEM page and the Escape card's Admin page, and stays Back while a
  dialog or an open list holds the input. The Escape card's Match and Admin
  section strips end with [trigger glyphs](ui/input-routing.md#trigger-glyphs-24-october-2026)
  for each controller family. The [joined lines](ui/retained-screens.md#joined-lines-and-capitals-24-october-2026)
  that C++ assembled (list separators, the time in the mission, a save's date
  and time, the load time's decimal mark and unit) and the Demos page's
  capitals come from every language's table. The Admin page's Apply changes
  plate ends with the [Menu button's glyph](ui/input-routing.md#menu-glyphs-24-october-2026),
  and Join Game's details card shows the [View button's](ui/input-routing.md#view-glyphs-24-october-2026).
  The home screens and menu pages [wrap](ui/input-routing.md#wrapping-menus-24-october-2026):
  Up and Down loop through each menu and its plinth links, skipping hidden entries.
  Confirmations and Restart Level wrap too, and scrolling lists remember their
  focus when a move comes back into them. The [View button](ui/input-routing.md#view-button-24-october-2026)
  hides or shows Join Game's server details and, by default, shows the scores
  while held in play. The home screens, menu pages and multiplayer cards
  [fit views narrower than 4:3](ui/retained-screens.md#narrow-views-24-october-2026)
  (5:4, portrait), and the list pages [grow their lists](ui/retained-screens.md#tall-views-grow-the-lists-25-october-2026)
  into a tall view's extra height.
- [ ] **UI-03 — P1, implementation: complete player and multiplayer workflows.**
  Finish tourney queue/arena/watch controls, model previews, full Match Control,
  map catalogs beyond 128 entries, stable refresh focus, contextual disabled
  reasons and role-specific vote operations. Finish identity protection for
  delayed remote mute packets and kick/admin targets across slot reuse.
  Complete save chapter and
  objective/play-time details, Strogg confirmations, loading handoffs, remote-load
  arsenal and MVD rosters. Qualify real task completion and failure/cancellation.
  Progress, 23 October: [full roster scrolling](ui/multiplayer-menus.md#full-roster-scrolling-23-october-2026)
  removes the 16-row publication/document limit from Welcome, Players and the
  summary's Statistics/Scoreboard pages. Rows keep a 24 dp pitch beneath fixed
  headings; kick choices cover the engine's connection table. This bounded
  implementation does not change server capacity or accept the full UI-03 scope.
  Progress, 23 October: [player identity guards](ui/player-identity.md) bind
  selections, local friend/mute marks and retained player requests to connection
  generations. Held and queued controls cannot follow a replacement occupant;
  stale selections return to the local player. Remote voice transport and other
  player-targeted flows remain open.
  Progress, 23 October: [save browser controls](ui/save-browser.md) add date/name/
  location sorting, manual/autosave filters and explicit refresh to both page
  families. Selection and row focus follow filenames, typed names survive refresh,
  and catalog generations reject stale actions. The remaining save details and
  wider device/platform acceptance stay open.
  Progress, 23 October: [Strogg confirmations](ui/retained-screens.md#strogg-confirmations)
  give the Strogg pause and its Save/Load pages dialogs in the Strogg family,
  translating from runes as they open. Ids, focus, soft focus and timing stay
  the stock dialog's. The construction derives from section 13.7's family rules
  and awaits art review under UI-05.
  Progress, 24 October: the Vote and Admin pages' [map catalogs](ui/multiplayer-menus.md#map-catalogs-24-october-2026)
  hold 128 maps a game type instead of 48.
  Progress, 24 October: Join Game's [row focus follows its server](ui/retained-screens.md#join-game-sub-page)
  through new orders, refreshes and filters, onto its page, as the choice
  already did; a server that leaves the list leaves the focus in place.
  Progress, 24 October: the lists' [empty states](ui/retained-screens.md#empty-states-24-october-2026)
  offer the action that fixes them: CLEAR FILTERS on Load and Save Game,
  Demos and Join Game while a filter hides everything, and GET NEW LIST when
  Join Game's scan found no server, under a heading in every language.
  While a scan has found nothing yet, Join Game shows [loading rows](ui/retained-screens.md#loading-rows-24-october-2026)
  at rest, and its first servers replace them with `row.reveal`. Retained pages
  gain section 13.13's [notices](ui/retained-screens.md#notices-24-october-2026);
  Join Game's favorites use them, and a removal offers Undo for 8 s. The
  [notices are shared](ui/retained-screens.md#shared-notices-and-a-lost-controller-25-october-2026)
  across the retained menus and say when a controller disconnects. Join
  Game's details card names its server [in full](ui/retained-screens.md#full-server-names-25-october-2026),
  which the list cuts.
  Progress, 23 October: [save details](ui/retained-screens.md#save-details)
  record each run's play time and difficulty in the persistent player info
  that every save header carries, without a save format change. CONTINUE and
  the Load Game page name them, the pause adds the run's total time, and
  objectives completed on a map survive loading its saves. Saves from earlier
  builds show neither. Chapter numbers on saves are not shown: the list names
  each save's mission.
  Progress, 23 October (landed 26 October): multiplayer
  [objective markers](ui/remastered-hud.md#multiplayer-objective-markers-23-october-2026)
  now cover permanent flag bases, Arena CTF assault points and DeadZone zones,
  with icon-based status, distance only, bounded overlap and off-screen directions.
  Preserve their gameplay/state and projection coverage while completing the
  broader UI-03/UI-04 workflows. VR now projects bearings from the tracked head
  onto the stereo HUD plane; preserve simulated-runtime and geometric coverage.
  Real-headset comfort/readability and the full device matrix remain open.
  Visual specification 1.28 and register revision 20 add the scope as pending
  `FLOW-067`; no evidence record is bound yet.
- [ ] **UI-04 — P1, implementation and qualification: full GUI corpus migration.**
  Finish all HUD, wrist/comms, scope, vehicle, cinematic, terminal/world and scripted
  surfaces with source-bound retained lowering. Preserve legacy grouping/remainder
  and established conditional-alpha behavior; finish include, script and timeline
  provenance instead of reopening resolved native alpha semantics. Preserve
  per-entity instances, projected density
  and input, saves/checkpoints and exactly-once commands. Require accepted behavior
  and artwork for every resource; conversion counts do not close migration.
  Progress, 24 October: the Medical Labs implant overlay has
  [engine captures](ui/remastered-hud.md#medical-labs-implant-captures-24-october-2026)
  through the level's own script at 4:3, 16:9, 21:9, 32:9 and 9:16 (`FLOW-062`).
- [ ] **UI-05 — P1, decision and qualification: resolve visual policy conflicts.**
  Reconcile SP/MP hit-marker defaults with visual specification section 14.7 and
  other recorded deviations before acceptance. Preserve editable vector framing,
  controls, symbols, 45-degree geometry and continuous motion; use bitmaps only for
  approved complex imagery. Keep stock references without adding proprietary art.
  Progress, 21 October: [register revision 19](ui/product-requirements.md#revision-19-awakening-single-player-scope)
  preserves `FLOW-054`/`WID-034` as history and replaces their active scope with
  `FLOW-066`/`WID-038`. Awakening single-player and ordinary Quake 4 multiplayer
  obligations remain; an expansion multiplayer module is explicitly excluded.
  This resolves the stale scope clauses without accepting a GUI. The
  [hit-feedback default correction](ui/remastered-hud.md#hit-feedback-defaults-21-october-2026)
  restores stock feedback for fresh SP/MP profiles and preserves archived opt-ins;
  four actual-map default/write/restore/reset runs pass. Optional marker artwork,
  timing and the remaining visual-policy qualification stay open.
- [ ] **UI-06 — P1, implementation: finish the visual authoring editor.** Build on
  DocumentEdit/history/prepared-view foundations: shell, hierarchy, property/layout/
  anchor tools, reusable components, pen/path/gradient/mask editing, timelines/state
  graphs/bindings, localization/assets, undo, conflicts and recovery. Use the same
  runtime renderer. Gate on editing a production screen and component/transition,
  undo/redo, interrupted save/recovery, reopen/package and SP/MP execution across
  multiple documents/viewports.
  Deliver the first real SYSTEM source round trip alongside UI-01, as required by
  `GATE-003`; judge the RmlUi architecture with that result and optimized cost.
  Complete the shared editor/toolkit exit at `GATE-004` before bulk corpus cutover.
- [ ] **UI-07 — P1, measured optimization and qualification: full UI performance.**
  Close retained title/pause parity and remaining fractional motion, cold compile,
  long-document, vertex-copy, mask/composition and GPU/memory costs. Ratify per-UI
  CPU/GPU budgets against HF-07 and VR-04; the inherited proposal is p95 at most
  10% and p99 at most 20% of the target frame interval per budget. Use optimized
  builds, at least 1,000 warm frames, cold/restart/resize results and 1,000-cycle
  bounded-resource checks. Keep whole-frame and isolated UI measurements separate.
  Begin with the SYSTEM/editor vertical slice; do not defer architecture/cost
  decisions until all screens have been converted.
  Progress, 22 October: [current-build measurements](ui/runtime-performance.md#current-build-measurements-and-presetscale-reproduction-22-october-2026)
  record five 1,000-frame Vulkan samples each for title, System and SP pause.
  Title/System exceed the proposed 60 Hz CPU budget; GPU and resource-cycle
  qualification remain open. Four GL/Vulkan stock/retained title/pause runs
  retain readable text through scene scaling and preset/restart cycles, without
  reproducing #168/#169. The System route is retained in all four; its stale
  selection after an external console preset change was subsequently repaired
  under UI-01; that repair does not change these performance measurements.
  Progress, 28 October: the [renderer/device controls](ui/performance-presets.md#rendering-backend-and-device-selection-28-october-2026)
  pass the actual SYSTEM layout matrix in twelve locales and windowed engine
  selection/restart checks on NVIDIA/Intel. This is functional and layout
  evidence only; the existing product, performance and platform gates remain.
- [ ] **UI-08 — P1, qualification and decision: final cutover.** Complete all seven
  final gates with current source-bound behavior, visual/editor/performance and
  Windows/Linux/macOS/Android-GLES evidence, including dedicated packaging. Decide
  external-mod legacy loading policy before removing temporary stock-product
  routes. Update acceptance registers only for the exact scope actually proven.
  UI-10 and UI-11 are prerequisites; currently passing checkpoint validators cannot
  record product completion.
- [ ] **UI-09 — P1, implementation and qualification: production toolkit and
  engine integration.** Close the adopted RUN/DOC/WID/RES/SUR/SAVE requirements,
  not just individual pages: general controls/components, public GUI adapter
  methods, material/movie/model/generated-image operations, world-surface output
  and input, gameplay timers, independent instances and save/restart continuity.
  Current code has six canonical control roles, five node kinds,
  partial material-image operations, empty binding-name
  updates/text-wrap queries and an explicit world-output rejection. Map each
  limitation to required semantics and extend existing working facilities rather
  than rebuilding them. Test actual SP/MP engine callers, simultaneous HUD/menu/
  world/editor contexts, failed recreation, timer/presentation-clock separation,
  restored state and exactly-once actions before bulk UI-03/UI-04 acceptance.
  Progress, 23 October: optional [button contexts](ui/player-identity.md#contract)
  cancel held and queued actions when their subject changes, including changes
  back to the old subject. Existing buttons retain their behavior. This bounded
  facility does not close the toolkit or device/platform qualification.
  Progress, 18 October: generated RGBA8 upload/release now uses bounded shared
  storage and frame-safe retirement. Actual Rml callback/engine-adapter tests,
  1,000-cycle checks and GL/Vulkan gameplay alpha/reopen/restart captures pass;
  [implementation and evidence](ui/runtime-spike.md#generated-image-integration-18-october-2026)
  do not close the wider toolkit or resource requirements.
  Progress, 23 October: [queued value ownership](ui/external-settings-refresh.md)
  now checks current starting values, mixed state and choice availability before
  dispatch. Ordered pending steps still work; external changes and removed or
  disabled options reject stale proposals. Native control/page regressions pass;
  complete toolkit and exactly-once corpus qualification remain open.
  Progress, 28 October: the typed model node and native colour/depth coverage
  resolve provide actual model output rather than a decorative substitute.
  [Bounded evidence and limitations](ui/document-format.md#model-foundation-evidence-28-october-2026)
  retain the failed fixture views, renderer-demo refusal, diagnostic-preview
  canvas issue and unqualified native lifecycle/composition cases. This does
  not accept `INV-013` or either production model preview.
- [x] **UI-10 — P0, investigation and implementation: refresh effective scope and
  source inventory.** Completed [retail/base/Awakening inventory](ui/inventory-report.md):
  264/276/324 effective paths, 358 distinct source/include-bound variants, 18
  changed loose overlays and five added base paths. Historical hashes/evidence
  remain; stale inventory acceptance was invalidated and no GUI was accepted.
  The governed inventory-revision path checks added/removed/changed sources,
  include closure and exact denominator while retaining existing validator
  restrictions. Positive and negative controls pass. UI-05 owns explicit artwork/
  exception contradictions; UI-11 still owns product acceptance tooling.
- [ ] **UI-11 — P1, implementation: evidence-controlled acceptance tooling.**
  `validate_product_requirements.py` deliberately freezes migration records and
  forbids new verified requirements/gates in checkpoint, revision and archive
  modes. Add a separately reviewed acceptance transition for exact requirements
  and resources, with current source/evidence bindings, complete dependencies and
  per-resource behavior/artwork proof. Retain existing modes' restrictions. Test
  stale evidence, partial conjuncts, changed sources, missing dependencies and
  unqualified resources as failures; a broad success flag must not accept a corpus.
  Milestone gates may accept their explicitly bounded exit scope while broader
  shared requirements remain partial, as specified by `GATE-003`/`GATE-004`.
  Final product acceptance still requires complete applicable requirements; check
  dependency evidence against the particular milestone being accepted.
  Progress, 23 October: the [acceptance transition](ui/acceptance-transition.md)
  (`--acceptance`) accepts only the requirements and resources it names. Each
  one needs committed, reviewed records that prove every conjunct, constraint
  and dependency, a gate's bounded milestone scope, and per-resource behavior and
  artwork, with all of it current against the present files. Checkpoints keep
  earlier acceptance only while it is current, or withdraw it explicitly. Its 19
  fixture tests cover the failure cases above. The transition awaits its
  separate review, and nothing has been accepted with it.
  Progress, 28 October: a follow-up source review found that records did not
  have to be committed, artwork could cite only a log, and earlier acceptance
  did not recheck binary/capture artifacts. The transition now enforces those
  checks and rejects control characters in evidence paths. Twenty-four fixture
  tests pass, with archive and inventory modes retaining their restrictions.
  [Review details](ui/acceptance-transition.md). The changed tooling still needs
  separate review before product acceptance; no requirement or resource is accepted.

Detailed current evidence: [menu audit](ui/menu-audit.md),
[retained screens](ui/retained-screens.md), [multiplayer menus](ui/multiplayer-menus.md),
[runtime performance](ui/runtime-performance.md), [document editing](ui/document-editing.md),
and [settings transactions](ui/settings-effect-execution.md).

## Retail Quake 4 compatibility and campaigns

Sources: [retail decl audit](retail-q4-decl-parity-audit.md),
[BSE retail audit](archive/2026-10-18/quake4-bse-research-and-implementation-plan.md#retail-parity-audit-2026-10-02),
[Awakening history](archive/2026-10-18/plans/q4x-awakening.md), [save reliability](savegame-reliability.md),
[save compatibility policy](savegame-compatibility-policy.md),
[retail asset baseline](stock-asset-baseline.md).

- [x] **Q4-01 — P0, investigation and qualification: current retail gap ledger.**
  Inventory and reproduce compatibility reports against current binaries and
  installed base PK4s. Distinguish engine defects, absent media, authored-content
  issues and deliberate supported differences. Recheck historical Network2/Recomp/
  Storage1/Q4DM9 lighting findings before calling them current. Keep resolved decl
  and BSE reconstruction work closed. Audit remaining overrides file-by-file and
  replace bootstrap workarounds with engine/game fixes where possible. Separate
  compatibility workarounds from intentional new UI/campaign content; retain the
  original-asset-only baseline and record each overlay used by a qualification.
  Completed: [current ledger and 165-path overlay audit](retail-baseline-ledger.md),
  both-API four-map controls plus lit Recomp, original-content SP/MP gameplay,
  and retained complete-baseline failures with source/package identities. The
  connecting pure-client timeout belongs to MP-08/Q4-02; unsynchronized lighting
  differences and reports awaiting precise reproductions remain explicitly open.
- [ ] **Q4-02 — P1, qualification: complete stock campaign and mode transitions.**
  Cover progression, scripted triggers, set pieces, AI, vehicles, bosses,
  cinematics/lip-sync, world terminals, checkpoints and saves through full campaigns
  on supported render paths. Test SP/MP and campaign switches with original assets;
  classify expected warnings individually. Map initialization or a single capture
  is insufficient evidence of playable progression.
- [ ] **Q4-03 — P1, investigation and qualification: remaining BSE/effect parity.**
  Resolve initial-light current-versus-spawn transform and rotated model-particle
  normal/tangent questions from the October audit. Qualify light/sound/decal,
  trails, child effects, collision, stops/deletes, owner changes, subviews,
  high-refresh and demo/save lifecycle. Preserve intentional safety and
  presentation differences; BSE stays in-tree and linked into the client.
- [ ] **Q4-04 — P1, qualification: Awakening campaign closure.** Play all thirteen
  maps and restore saves during unique weapon/status/vehicle/boss states; qualify
  freeze persistence, damage gates, animation speed, and turret effects. Recheck
  transient corpse pin loss on restore as a bounded limitation. Verify base/Arena/
  MP isolation after restart. Keep the completed classes, gameplay and turret work;
  no expansion MP module or resurrection of obsolete companion-layer architecture.
- [ ] **Q4-05 — P1, decision and implementation: save failure recovery.** Preserve
  approved v2/v3/v4 readers and current corruption checks. Design safe recovery for
  object-restore failure after map teardown; evaluate staged-world rollback versus
  explicit recovery behavior. For unrecognized unstamped/build-1 payloads, assess
  restart at saved level with header loadout rather than guessing the payload;
  recognized build-1 releases already have compatibility readers. Require
  interruption/corruption fixtures and no destruction of valid existing saves.
  Current partial repair preserves live entity identities when rejecting invalid
  saved numbers, duplicate master-table entries and interrupted registrations;
  see [restore identity evidence](savegame-reliability.md#restore-identity-and-partial-registration).
  [Factory-object lifetime tracking](savegame-reliability.md#ownership-during-an-interrupted-restore)
  now releases remaining objects after map cleanup and retires attached objects
  already deleted by another owner. Native lifecycle/failure controls and a real
  script-incompatibility/reload probe pass. The subsequent
  [semantic-failure recovery flow](savegame-reliability.md#recovery-after-semantic-restore-failure)
  unwinds before session teardown, opens the retained Load Game page or classic
  dialog and leaves save files intact. Unknown unstamped formats still fail
  preflight; automatic header-only restart is intentionally excluded.
  [Saved render handles](savegame-reliability.md#renderer-ownership-during-interrupted-field-reads)
  also stay temporary until the renderer returns a current definition, preventing
  an interrupted read from giving cleanup a foreign handle.
  [Binding/team validation and failed-load teardown](savegame-reliability.md#binding-graphs-during-failed-restores)
  reject cyclic links, check complete team ordering and discard partial links
  before cleanup. [Typed spawn references](savegame-reliability.md#typed-spawn-references-during-restore)
  also validate live referenced classes before publishing a saved ID, while
  preserving expired-reference behavior.
  [Mover activation chains](savegame-reliability.md#mover-activation-chains-during-restore)
  receive equivalent cycle/membership checks and failed-load detachment without
  changing their saved layout. [Throwing factories](savegame-reliability.md#throwing-object-factories)
  also recover through the staged GL/Vulkan load page and healthy reload.
  [Script-thread ownership](savegame-reliability.md#script-thread-ownership-during-restore)
  rejects duplicate owners, interpreter mismatches and owned threads that would
  delete themselves; failed-load cleanup detaches temporary owners. Native and
  GL/Vulkan recovery fixtures pass. The
  [weapon-owner investigation and class admission](savegame-reliability.md#standalone-object-class-admission)
  now reject standalone weapon/physics/helper classes outside the writer's three
  object families before construction. Current GL/Vulkan recovery, 252 native
  checks and v2/v3/v4 historical-reader controls pass. The
  [serialized-GUI boundary](savegame-reliability.md#serialized-gui-admission-and-optional-menu-fallback)
  now preserves optional menu fallback during loading while rejecting a missing
  GUI required by the save. Windows/Linux native, 259-test Windows suite and
  GL/Vulkan recovery/reload checks pass. Other owning-graph and
  partial-constructor qualification remains open.
- [ ] **Q4-06 — P3, decision: portable saves.** `raw1` native aggregates are not
  cross-platform saves. Inventory and normalize game plus GUI/interpolator raw
  blocks before introducing a versioned portable format and ABI/architecture
  fixture matrix. Keep existing decoders and explicit unsupported-format handling.
  Retail proprietary game-DLL/save interchangeability is not a project target.
- [ ] **Q4-07 — P1, qualification: genuine historical save corpus.** Complete the
  historic-binary corpus for 0.6.95, 0.8, 0.10, 0.11 and 0.13 where current evidence
  uses equivalent/restamped fixtures. Qualify the exact HiDef #123 fixture and
  other target ABIs, identifying source executable, architecture and save layout.
  Keep synthetic corruption/layout tests separately labelled; they cannot prove
  compatibility with saves generated by the original binaries.
  Progress, 21 October: the [published-writer corpus](savegame-reliability.md#original-release-writers-21-october-2026)
  passes current restore/resave/reload for five maps from each requested release
  on Windows x64. Four releases commit their original saves; v0.13.0 rejects its
  own build stamp, so its five completed staged payloads were captured without
  changing the writer or bytes. That original transaction failure remains explicit.
  The exact [#123 retest](savegame-reliability.md#issue-123) still fails safely:
  the supplied rebuilt map lacks a saved prelight model, and the retail-map
  control reaches a GUI child-identity mismatch. Its writer is fingerprinted
  0.10.0; historical reporter confirmation and corrections to prior assumptions
  are preserved. This current failure and the other target ABIs keep Q4-07 open.
- [ ] **Q4-08 — P2, investigation and qualification: save cost and durability.**
  Measure large-save CRC/sync time and fault behavior across target filesystems.
  Directory metadata is not synced and full power-loss atomicity is not established.
  Define any stronger durability guarantee before implementing it; qualify partial
  writes, replace failures, process interruption and preservation of the last valid
  save independently from object-restore recovery in Q4-05.

## Multiplayer bots Arena and demos

Sources: [competitive closure audit](competitive-match-reference-audit.md),
[bot implementation](mp-bots.md), [Arena ceremony](arena-campaign-ceremony.md),
[demo architecture](multiview-demos.md), [MVD qualification](mvd-qualification-2026-09-07.md),
[liquid implementation history](archive/2026-10-18/plans/2026-08-07-liquid-support.md).

Current delivery scope, 28 October: the user deferred real WAN and
cross-architecture peer qualification for now. MP-07/MP-08 retain those gates for
future support qualification; local implementation, simulation and current-host
checks continue. This deferral does not promote prediction/rewind defaults or
claim peer combinations that have not been tested.

- [ ] **MP-01 — P1, qualification: competitive framework release closure.** Exercise
  roster invitations/substitution/bench/disconnect, replacement identity and slot
  reuse, ballots/stale operations, pool/veto/load-failure rollback, countdown/ready/
  round edges and duplicate timeout charging. Qualify role-filtered item timers,
  invalidated follow targets, journal/MVD interruption/capacity and all live roles'
  localized keyboard/controller workflows. Keep captain spectator invitations
  fail-closed and record remaining exact runtime scenarios from the current audit.
  Progress, 18 October: current staged GL role/follow and roster/reconnect runs
  pass, including replacement identity isolation. The [audit evidence](competitive-match-reference-audit.md#stage-two-current-runtime-rerun-2026-10-18)
  retains their precise scope; the remaining release checks are still open.
  Progress, 23 October: a review fixed three pipeline defects. Passed
  ballots kept the session revision their proposer saw, so any ready change
  or connection during the vote voided them; execution now revalidates
  against the current session, with once-only acknowledgement owned by the
  adapter. Veto turns are held to the series side the view shows, and a
  spectator's follow latches the target's spawn identity. Extended asset-free
  contracts fail against each reverted fix; all 52 asset-free `mp_match_*`
  contracts pass. [Details](competitive-match-reference-audit.md#proposal-revalidation-veto-side-and-follow-identity-2026-10-23).
  The live runtime scenarios above remain open.
- [ ] **MP-02 — P1, qualification and targeted fixes: bot traversal safety.** Rerun
  staged q4dm8/q4ctf3/q4ctf5/q4dz3 after the exact-state jump/unroutable-pad fixes,
  then the complete declared-map sweep including warmup (currently 33 variants).
  Separate enemy impulses from
  navigation/control failures and retain strict world-death gates. Qualify route
  generation/think budgets, objectives and real combat; source tests do not close
  the previously failing runtime safety sweep.
  Progress, 18 October: [four-map rerun](mp-bots.md#current-four-map-rerun-18-october-2026)
  passes CTF3/DZ3 but retains DM8/CTF5 world-death failures following logged combat
  impulses. The subsequent [33-map sweep](mp-bots.md#current-complete-catalog-sweep-18-october-2026)
  passes 32 maps but retains a DM8 world death following an enemy impulse. The
  gate stays open, including recovery steering and controlled timing evidence.
  Progress, 21 October: a [sloped-floor support correction](mp-bots.md#sloped-floor-support-correction-21-october-2026)
  fixes false edge braking caused by the support box/full-player height difference.
  Windows/Linux fixtures, isolated failing controls and original CTF6 geometry
  checks pass. A 60-second CTF6 bot run has no world deaths or traversal timeouts;
  remaining stalls and the renewed four-map sweep's DM8/CTF5 world deaths keep
  the wider gate open. A subsequent [live-origin correction](mp-bots.md#live-jump-origin-correction-21-october-2026)
  resolves the isolated DM8 air-step false approval: the proof had started one
  unit above the actual player and incorrectly removed horizontal momentum.
  Native regression controls and the original-map replay pass; the broader DM8
  rerun still has a world death, so its remaining cause and the full gate stay open.
  The ensuing [pad-flight investigation](mp-bots.md#jump-pad-impulse-recovery-21-october-2026)
  reproduces that separate post-hit overshoot. Supported touchdown/runout checks
  and immediate braking at an exhausted route now pass the isolated replay and
  Windows/Linux regression controls. The wider map/catalog and timing gates remain
  open; the failed intermediate touchdown-only fix is retained as evidence.
  The [renewed 33-variant sweep](mp-bots.md#renewed-catalog-sweep-21-october-2026)
  passes 31 variants and retains CTF5/q4xdm13 failures. The isolated
  [q4xdm13 dodge repair](mp-bots.md#dodge-landing-runout-21-october-2026)
  now reserves supported ground for touchdown momentum; Windows/Linux native
  controls and the original-map replay pass. CTF5 impact diagnosis, current
  match/catalog reruns and the broader acceptance gates remain open.
  The [CTF5 pad reservation correction](mp-bots.md#early-pad-reservation-21-october-2026)
  brakes before multiple nearby walking corners can carry a bot into a busy
  pad. Windows/Linux regression controls pass. A full-duration repeat retains
  two separate world deaths (low-health landing and post-rocket fall), so the
  strict map/catalog safety gate remains open.
  The [repeated-contact correction](mp-bots.md#repeated-pad-contact-and-landing-health-21-october-2026)
  models successive force-brush pushes and rechecks low-health approaches before
  the final walking corner. Native controls and the isolated landing-damage
  replay pass. The next full-duration match retains four deaths after unintended
  pad entries or combat impulses; recovery and strict catalog gates stay open.
  Progress, 22 October: the [pad corridor, unplanned-pad and knocked-fall work](mp-bots.md#pad-corridors-unplanned-pads-and-knocked-falls-22-october-2026)
  reserves shared pad corridors between bots, keeps momentum off pads a route
  did not choose, checks whole pad chains against landing health and brakes or
  steers falls a strong knock caused. The user's impulse-exclusion policy is now
  `mp_bot_audit.py --classify-impulses`: a death counts unless an enemy impulse
  precedes it, its replay falls under every fixed air input, and the moment
  without the impulse lands; 22 asset-free controls pin the rule. Windows/Linux
  native regressions and 21 single-reversal controls pass. The final build's
  q4ctf3 and q4dz3 and four catalog reruns have no deaths, but q4ctf5 keeps one
  and q4dm8 three navigation deaths (a recoverable rail fall the air choice
  missed, a tap on an already-falling bot, a fail-closed replay, an unexplained
  jump-down), and the 33-variant catalog passed 29 variants on the previous
  build with peers saturating the CPU. Remaining: those navigation causes,
  team-mate splash knockback, replay-bot replacement, one uncontended
  33-variant sweep on one build, and controlled think/route budgets.
  Follow-through: [air recovery and replay isolation](mp-bots.md#air-recovery-and-replay-isolation-22-october-2026)
  repair the missed CTF5 heading and bot replacement. The controller and both
  known recoverable headings land alive with fresh pickups per trial; all 21
  DM8 Sledge trials arm, and its surviving controller keeps that death classified
  as navigation. Independent DM8 jump-down and team-knockback causes remain open.
  The subsequent [single-build 33-variant sweep](mp-bots.md#single-build-complete-catalog-22-october-2026)
  passes every map with 215 combat kills, zero world deaths including warmup,
  zero traversal timeouts and no exclusions. Every map exceeds 40 simulated
  seconds. This closes the outstanding catalog rerun; 17 stuck events, 12 goal
  give-ups, earlier long-match failures and unratified timing tails keep the
  wider MP-02/MP-03 gates open.
  Progress, 23 October: the team-knockback cause is addressed. A bot now
  refuses a splash shot whose predicted push (RadiusDamage falloff, the
  multiplayer push direction, `knockback_team` and `g_knockback`) would
  throw a team-mate onto ground that does not hold them, with team damage on
  or off; source pins tie the prediction to the game's own formulas.
  Seven 90-second six-bot audits on this build (q4ctf5 to q4ctf8, q4dz3,
  q4xtourney1 and the liquid lab) pass the world-death gate; q4ctf5's one
  death was an enemy rocket push, which the audit classifies and excludes. The
  audits also caught a railgun hit on a team-mate standing behind an enemy:
  the multiplayer railgun penetrates players, so bots now trace its whole line
  and hold fire when a team-mate is on it ([audit results](mp-bots.md#team-knock-guard-and-trench-rims-23-october-2026)).
  The DM8 jump-down cause, controlled timing and the strict long-match gate
  remain open.
  Progress, 24 October: three 140-second q4dm8 audits on this build
  (`bot_debug 3`) record 83 kills and eight world deaths, every one an enemy
  push the replay classifies and excludes, with no navigation deaths. The
  jump-down did not recur in 419 simulated seconds, so its cause stays
  unestablished ([audit results](mp-bots.md#q4dm8-recurrence-audits-24-october-2026)).
  Progress, 26 October: four bot navigation fixes a Codex round made on 23
  October landed: inward rim landings after knockback, unsafe dodges handed to
  air recovery, planned drops that override the edge brake only on a proven
  landing, and pursuit progress on refreshed routes, plus a knockback classifier
  that looks past the last hit. Their CTF5 drop and rim replays pass, and
  120-second six-bot audits on q4ctf5 and q4dm8 record no world deaths, stuck
  events, give-ups or traversal timeouts
  ([rim landings, dodges, drops and pursuit](mp-bots.md#rim-landings-dodges-drops-and-pursuit-progress-26-october-2026)).
  The strict long-match gate and controlled timing remain open.
  Progress, 28 October: a bounded quarter-millisecond histogram adds conservative
  p50/p95/p99 ceilings to `bot_profile`, with native boundary/reset/overflow
  coverage. The audit can enforce explicit mean, p95 and generation budgets and
  reject runs overlapping other runtimes or compilers. A five-minute six-bot DM8
  run records 284.928 simulated seconds, 56 combat kills, one navigation world
  death and one stuck event. Its 2.192 ms mean passes the selected 4 ms reference
  budget; a 12 ms worst-window p95 exceeds 8 ms, and competing processes invalidate
  controlled timing. No failure is waived. The unhit fall near `(601, 685, 314)`
  is being isolated from the retained pre-departure states; the gate stays open.
  The [unpressed-WALK investigation](mp-bots.md#unpressed-walk-falls-and-timing-evidence-28-october-2026)
  reproduces a diagonal rim fall from one retained pose. Air recovery now admits
  descending unpressed WALK falls with a proven-unsafe coast, preserving planned
  departures and safe arcs. Native boundary/reversal tests pass; both failing
  diagonal cases now land alive and all four direct-approach controls survive.
  This isolates and repairs a defect without claiming the original match's exact
  cause. Current long-match/catalog and controlled timing gates remain open.
- [ ] **MP-03 — P2, implementation and qualification: broader bot navigation.**
  Audit custom-map ladders/lifts, shallow trenches, route-cost approximations,
  pad/player collisions, graph limits, optional caching and debug growth. Extend
  only demonstrated gaps; bots and personality/chat/roster systems already exist.
  Progress, 23 October: [audit results](mp-bots.md#known-limits). Multiplayer
  player physics has no ladders (retail behaviour), so no ladder link is
  needed; lifts remain unmodelled and no stock map routes through one. The
  gap-jump scan now also links a trench's far rim (`GapJumpClear` still
  proves the arc). All 33 catalog variants still build; 24 gain 271
  trench-rim jumps (q4ctf7 75), and audits on the three most affected maps
  record no world deaths. The listen-server `bot_debugNav` line pool
  no longer fills, because the server frame clears debug lines as single
  player does. Graph limits are unchanged and documented; caching remains a
  non-goal while generation stays lazy.
- [ ] **MP-04 — P1, implementation and qualification: Arena completion.** Resolve
  world-pause versus continuing sound, restart/map-navigation cost and physical
  pause navigation. Capture victory/defeat/draw ledgers, progression and ceremony
  through save/reload and failure paths; an aborted-launch ledger is not completion
  evidence. Coordinate with UI-03, HF-01 and AUD-02.
  Progress, 18 October: [Arena sound ownership and result evidence](arena-campaign-ceremony.md#current-pause-and-result-evidence-18-october-2026)
  closes the continuing game-audio clock gap. Native ownership tests and final
  GL/Vulkan gameplay runs pass pause/resume, victory/defeat/draw and persisted
  best-result ordering. The [19 October headphone check](arena-campaign-ceremony.md#headphone-pause-and-resume-19-october-2026)
  also passes two audible pause/resume cycles. Restart/navigation cost, physical
  controls, endpoint loss/return and remaining failure-path coverage keep MP-04 open.
  Progress, 24 October: Restart match now resets the live match in place
  through an authorized `serverMapRestart`. The bots keep their client slots,
  so nothing is removed and added again, and the introductions start within
  0.3 s instead of after a map and navigation reload. A short roster, a missing
  server or a due result falls back to the fresh-server relaunch. Six no-input
  runs cover the card and the stock menu, warmup, Team DM, Clan Arena, the
  fallback and a recorded result after a restart
  ([in-place restart](arena-campaign-ceremony.md#in-place-restart-24-october-2026)).
  Physical controls, endpoint loss/return and remaining failure-path coverage
  keep MP-04 open.
  Later on 24 October: a failure path lost decided results. Leaving the match
  or quitting the game during the end-of-match ceremony recorded nothing, since
  the result is handed over only after the ceremony. game_mp now publishes the
  decided result while the handoff waits, and a disconnect, quit or error drop
  records it; a disconnect also reports it. Two no-input runs keep a win's
  progress and best result through a disconnect and through a quit
  ([evidence](arena-campaign-ceremony.md#leaving-during-the-ceremony-24-october-2026)).
  Physical controls and endpoint loss/return remain.
- [ ] **MP-05 — P1, investigation and implementation: demo reliability and
  operator workflow.** Reproduce the recorded Red Rover seek/debris distortion
  using the original recording. Define retention/quota/download/partial recovery
  and structured markers; complete named/previous follow, autotracking, independent
  keyframes and desired reverse-scrub/timedemo/AVI/thumbnail scope. Preserve approved
  formats and explicit retail-stream rejection. Test partial writes, repeated
  seeking, pause/restart/round changes and transient geometry; reuse competition's
  existing auto-record lifecycle rather than duplicating it.
  Scope decision, 28 October: the user selected the core recording/playback
  workflow, structured markers, autotracking and independent keyframes. Reverse
  scrubbing, timedemo, AVI export and generated thumbnails are deferred and do
  not block this selected delivery scope.
  Progress, 23 October: `mvdRecover` salvages an interrupted `.mvd.part`
  into a new demo, never modifying the source. An executable fixture runs the
  production reader, writer and recovery at all 2,032 cut points of a
  synthetic recording, plus corrupt-record, stale-index and damaged-header
  cases, with failing controls. Demo follow gains previous, slot and name
  targets, the overlay names the followed player, and `mvdStatus` reports an
  active recording and the last result. The runtime probe also found that
  every MVD recorded with two or more players since 27 September was refused
  at playback: The Awakening's powerups widened the start-state powerup mask
  to 18 bits while the engine validator assumed 16. Both readers now detect
  the recorded width (16 or 18). A start-state fixture covers one to six
  players at both widths with failing controls. A runtime probe killed a
  three-bot recording mid-write; `mvdRecover` kept 201 snapshots, stopping at
  a truncated record. The recovered demo played to its clean end, and next,
  previous, name and free follow all selected the expected players
  (`.tmp/mp-round/runtime/mvd/`). Retention,
  markers, keyframes, reverse scrub, AVI, thumbnails and the original Red Rover
  reproduction remain open.
  Progress, 24 October: the original Red Rover recording behind the seek/debris
  finding (`rr-managed-isolated/host/baseoq4/demos/q4dm1.mvd`) no longer exists,
  so that reproduction needs a new recording. Retention and markers need design
  decisions first: automatic recordings are linked from competitive series
  evidence, so pruning them would break those links, and structured markers need
  a new MVD record type and a game-to-engine event interface.
  Later on 24 October: a new Red Rover recording reproduces the seek/debris
  finding. A seek replays records without running frames, so gibs and debris
  spawned during it stayed frozen where they spawned, often at the death camera.
  Seeks now drop them (the engine publishes `mvd_seeking`). On a fixed grid of
  14 seeks, six left 9 to 18 frozen chunks before the fix and none after
  ([evidence](multiview-demos.md#red-rover-seek-debris-24-october-2026)). The
  missing first-person weapon after a paused seek, keyframes, reverse scrub,
  AVI, thumbnails, retention and markers remain open.
  Progress, 28 October: snapshot weapon initialization now honours its pending
  catch-up before clearing it. Previously a paused seek recreated a weapon at
  the start of its raise animation, below the view, although a model definition
  existed. The executable regression rejects the old initialization and preserves
  ordinary spawns, absent model snapshots and once-only catch-up. Seven exact-time
  forward/backward paused seeks pass on GL and Vulkan with the retained Red Rover
  recording; inspected engine captures show the weapon again. See the
  [dated qualification](multiview-demos.md#paused-seek-weapon-catch-up-28-october-2026).
  Broader operator, keyframe and export scope remains open.
  The same day's [retention/quota policy](multiview-demos.md#library-retention-and-quota-28-october-2026)
  preserves recordings and journal links until explicit archival/deletion. The
  optional `mvd_libraryQuotaMB` counts nested complete/partial recordings and
  freezes the remaining budget at admission; it never prunes. Native failure
  tests and staged GL refusal/cap/recovery/playback pass, with existing files
  unchanged. Downloads remain explicitly outside standard builds. Structured
  markers, autotracking, independent keyframes and export scope remain open.
- [ ] **MP-06 — P2, implementation and qualification: liquid extensions.** Verify
  splash/bubble/wade effects, underwater view/muffle, projectiles, buoyancy and bot
  behavior. The current AAS compiler already emits water/swim/waterjump data;
  validate its end-to-end consumers before proposing new generation. AI swimming,
  authored swim animation, moving volumes and air-HUD replication need explicit
  content/gameplay scope. Preserve mapper guidance against duplicate hurt damage.
  Scope decision, 28 October: the user selected completion of static liquids,
  player/bot swimming and replicated air readouts. Swimming monsters, new swim
  animations and moving liquid volumes are deferred; they do not block this
  selected delivery scope.
  Progress, 23 October: a client drained its local air reservoir once per
  prediction replay pass, N+1 times the server's rate under an N-frame lead;
  it now steps once per game frame. `s_soundWorlds` reports the underwater
  muffle. The audit found the AAS compiler never emits waterjump data
  (`AREA_LIQUID` is never set) and no consumer routes water. A six-bot audit
  of the liquid lab had no deaths but 20 stuck reports: bots surfaced whenever
  their eyes were under water, so a route onto a pool floor left them treading
  water above it. Swimming bots now dive for an underwater route corner while
  they have more than half their air; the rerun records no stuck reports and
  28 completed traversals instead of 8 ([details](mp-bots.md#team-knock-guard-and-trench-rims-23-october-2026)).
  Bot swimming, ragdoll buoyancy and an air HUD remain scope decisions.
  Progress, 28 October: the AAS compiler now sets `AREA_LIQUID` from the emitted
  water contents. Swim links already used contents, while the water-jump builder
  required the missing flag. An executable production-area fixture covers wet/dry,
  floor/no-floor, reused and empty nodes and rejects the old compiler. Actual
  compiled-water-map/consumer qualification and broader liquid presentation remain
  open; this does not add swimming AI, animations or moving volumes.
  Follow-through: the [actual dedicated compiler](liquid-qualification.md)
  emits 26 swim and 16 water-jump links in the partitioned liquid-lab fixture;
  all nine serialized water areas carry `AREA_LIQUID`. The independently copied
  fixed compiler runtime preserves its hashes and writes outside `.install/`.
  This closes the compiler-output check; routing/movement consumers and wider
  liquid presentation remain open, and stock nonswimming AAS defaults stay intact.
- [ ] **MP-07 — P1, implementation and qualification: coupled prediction and
  rewind.** In `src/mpgame/gamesys/SysCvar.cpp` both enhanced prediction and lag
  compensation default off. Preserve the coupled displayed-pose-age and rewind
  contract implemented below with HF-01/HF-05; bound untrusted client claims
  and prevent double correction.
  Keep promotion separate from implementation. Qualify actual remote peers under
  delay, jitter, loss, reordering and duplicate commands, including teleport,
  respawn, slot reuse, pause and spectator changes. Cover hitscan, lightning and
  melee plus same-fire-frame presentation; restore present-world state before
  damage/effects and preserve authoritative replay. Extend existing lag-compensation
  contracts with executable state/peer tests and correct the networking guide.
  Progress, 23 October: the coupled contract is implemented. Each client
  advertises its remote extrapolation in user info
  (`net_mpRemoteExtrapolation`, read-only and derived every frame, so a
  server echo cannot change what the client simulates). The server rewinds by
  the displayed pose age: min(lead + one frame, round trip + slack), less
  min(lead, the advertised tics). Client claims can only shorten a rewind.
  `lagCompStatus` and the client's `net_mpPredictDebug` pose age expose both
  sides. An executable fixture covers the estimate, advertisement, history
  across teleport/respawn/slot reuse/map reset/pause and the rewind/restore
  bracket, with five failing controls. The networking guide now replaces the
  presets that enabled both corrections. A two-process loopback probe with
  `net_forceLatency 60` (`.tmp/mp-round/runtime/lagcomp/`) shows the
  advertisement switching 0, 16, 0 and 4 as the client changed modes. Rewinds
  were 0 enhanced, 198 legacy (the round-trip ceiling) and 134 with a 4-tic
  cap, and client pose ages were 0, 48 and 0 ms. The same probe found the
  client's reported prediction target rising about 1 ms per snapshot to its
  1000 ms cap, on plain loopback too, while its realized replay chain stayed
  at 0-3 tics. `net_verbose 2` now logs the controller's state per snapshot,
  and GL and Vulkan reruns traced it to a client regression from 19 August
  (42e765882): the catch-up step `( 1 - ( INTSIGNBITSET( delta ) << 1 ) )` is
  unsigned, and once the sum was widened to 64 bits a client behind its target
  was slammed to full slowdown instead of speeding up. It then ran one frame
  behind every snapshot, resyncing each time, and the server duplicated its
  late commands (45 ms late on loopback, 109 ms with 60 ms of forced latency).
  The steps are now signed. A fixture runs the production snapshot-timing
  block in a closed client/server loop at 0, 30 and 75 ms one way: it settles
  with no resyncs or late commands, and four controls, including the
  regression, fail. The GL rerun with the fix (`.tmp/mp-round/runtime/gl3/`)
  has no resyncs. On plain loopback the client leads by 48 ms, its commands
  arrive about 15 ms early and the target holds at 41 ms. With 60 ms of forced
  latency it settles near 170 ms (round trip 150), and duplicated commands
  appear only while it adapts to the sudden change. The server's estimates
  now follow the real lead: legacy rewinds 56 and 182-185 ms, enhanced 16 ms,
  and 120-124 ms with a 4-tic cap. Defaults stay off; real peers under
  jitter, loss and reordering remain open.
  Progress, 24 October: [simulated network conditions](mp-network-qualification.md)
  now cover delay, jitter (with the reordering it causes), loss and duplication on
  Windows and POSIX, seeded for repeatable runs (`net_forceJitter`,
  `net_forceDuplicate`, `net_forceSeed`, `netImpairment`). They replace a
  Windows-only lag queue that dropped datagrams over 1,400 bytes, so a client
  could not connect with `net_forceLatency` set. Native cases and a wiring
  contract carry eleven failing controls. Loopback probes with three bots in
  both prediction modes step through clean, 50 ms, 50 ms with 0-40 ms jitter,
  10% loss, 20% duplication and combined conditions: no resyncs in any phase,
  the client's lead follows the round trip, the server duplicates a command in
  at most six snapshots a phase, and rewinds follow the real lead (legacy up to
  the 200 ms cap, enhanced 0-16 ms). A client connecting with 80 ms, jitter,
  10% loss and 5% duplication already set joins on its first challenge. Real
  WAN peers and the defaults decision remain open.
  Later on 24 October: the simulation adds a bottleneck (`net_forceRate`,
  `net_forceQueue`) and burst loss (`net_forceDropBurst`), and `netImpairment`
  reports the data rate. Bursts of about five losses at 10% held with no
  resyncs, at a 32 ms higher lead than single losses
  ([congestion evidence](mp-network-qualification.md#congestion-rate-control-and-burst-loss-24-october-2026)).
  Progress, 26 October: a positive `net_mpLagCompBiasMS` no longer rewinds a
  bot, a listen host or an unmeasured slot by the bias alone; the rewind now
  returns before the bias when the lead or the measured round trip is missing.
  The state fixture adds three bias-20 cases. A Codex round found this on
  23 October in its own copy of the code.
  Progress, 28 October: `lagCompStatus` now applies the same measured-connection
  prerequisite as the rewind path, so positive bias cannot report a fictitious
  rewind for a bot, listen host or unmeasured slot. The state and wiring checks
  pass; enhanced prediction and compensation remain off by default.
- [ ] **MP-08 — P1, qualification and decision: network and content interoperability.**
  Qualify real cross-platform/architecture peers over IPv4 and IPv6, fragmentation/
  MTU/rate limits, congestion, high player counts, discovery and reconnect. Preserve
  the bounded multi-address/family failover sequence implemented
  below; qualify real multi-record DNS separately. Exercise ordered pure assets,
  content-only mods and explicit trusted
  local code-module admission. Keep the same-platform pure compatibility token
  distinct from cryptographic module verification or anti-cheat; standard builds
  do not enable direct asset downloading. Document clear rejection of unsupported
  protocols: retail 2.85 interoperability is a separate future scope decision,
  not a version-number change to openQ4 2.41 or a prerequisite for retail assets.
  Progress, 18 October: fixed command-argument ownership during list growth and
  reload replay, which dropped the baseline client's `connect` command. The
  [retail ledger](retail-baseline-ledger.md#stage-two-connection-regression)
  records a passing four-role stock baseline, 185 native checks and a failing
  shallow-copy control. The broader peer/architecture/interoperability gate
  remains open. The player [networking guide](../user/multiplayer-networking.md#ipv6-connections)
  now explicitly documents single-address DNS selection and the numeric-address
  workaround when the chosen endpoint is unreachable.
  October 22: published 0.13.0 and the voice-enabled candidate explicitly refuse
  each other in both directions: missing packages with pure checking, declaration
  checksum mismatch without it. The four scenarios produce no unknown voice
  messages, but no mixed-release peer joins or transmits; they do not qualify
  mixed-release voice. Capability-gating regression checks pass separately.
  [Exact compatibility scope](audio-voice-evidence.md#mixed-release-voice-check-22-october-2026).
  The [cross-platform peer evidence](retail-baseline-ledger.md#cross-platform-pure-peers-18-october-2026)
  adds passing Windows-client/Linux-server IPv4 and scoped IPv6 pure joins and
  successive reconnects. Canonical ZIP metadata fixes otherwise identical
  host-built PK4s failing package identity. WAN impairment, concurrent load,
  discovery, other architectures and the remaining content cases stay open.
  October 23: hostname connections now try up to eight resolved addresses:
  the historical first choice, then alternating families, with three
  unanswered challenges each. A runtime probe joined an IPv6-only loopback
  server through `localhost`: after three unanswered challenges to
  127.0.0.1 the client moved to `[::1]` and entered the game. Native ordering
  and failover tests cover the shared helper. The networking guide documents
  the sequence. Real multi-record DNS, WAN impairment and concurrent load
  remain open.
  Progress, 24 October: simulated WAN conditions now exist on every platform
  (see MP-07). With 20 bots on a dedicated server, one remote client holds
  16-17 ms frames and a steady 140-148 ms prediction target under 50 ms
  latency, 10% loss and 20% duplication, with no deferred snapshots or
  overflowing messages. Earlier resync bursts traced to CPU starvation on the
  shared machine; a client that cannot hold 60 Hz still falls behind toward the
  1 s cap ([evidence](mp-network-qualification.md#high-player-count-24-october-2026)).
  Real WAN peers, load across machines and discovery remain open.
  Later on 24 October: congestion and rate limits on a simulated bottleneck. A
  three-bot match needs about 6 KB/s downstream; links with spare capacity add
  only queueing. A 6 KB/s link congests and feeds itself: the server offers
  1.7 times the demand, snapshots fall 28% and the lead reaches 224 ms. A
  client connecting with `net_clientMaxRate 5000` removes the congestion (no
  drops, 160 ms lead) at the cost of snapshot rate
  ([evidence](mp-network-qualification.md#congestion-rate-control-and-burst-loss-24-october-2026)).
  Whether the server should adapt its rate on its own is a design decision;
  the networking guide tells players to lower `net_clientMaxRate`.

## Audio and voice

Sources: [audio implementation and remaining limits](audio-modernization.md),
[retail reverb](retail-audio-reverb.md), [networking guide](../user/multiplayer-networking.md).

- [ ] **AUD-01 — P1, investigation and qualification: retail distance semantics.**
  Audit listener/source radius and authored range units against retail audible
  fixtures and distance curves before changing attenuation defaults. Both
  `DOOM_TO_METERS` and `METERS_TO_DOOM` in `src/sound/sound.h` are currently 1.0;
  the older audio report's unit-mismatch explanation is not an established current
  defect. Reconcile that report with measured source/listener behavior rather than
  applying a speculative 39.37 conversion. Preserve the authored mix and already
  implemented spatial/HRTF/EFX/reverb behavior.
  Progress, 20 October: the [current distance audit](audio-modernization.md#current-distance-audit-20-october-2026)
  passes 54 actual emitter/OpenAL measurements across three original shaders,
  with default linear and optional quadratic curves. Source/listener coordinates
  and radii remain in map units; OpenAL rolloff is zero and does not double the
  engine curve. The failed initial harness calibration is preserved. Defaults
  remain unchanged; physical audibility is still pending.
  October 21: the mod wet-path reference-distance defect is isolated and measured
  under AUD-04; no change was made to dry attenuation or either unit macro.
  October 22 audit: the remaining gate is human listening on real speakers or
  headphones against retail. The measured distance-curve audit is complete;
  keep this item open until that listening pass is recorded.
- [ ] **AUD-02 — P1, qualification: physical device and timing lifecycle.** Test
  default/selected endpoint loss and return, hotplug, pause/resume/slow motion,
  cinematics, map/restart and long sessions on target OSes. Verify audible output,
  not just backend initialization; include physical Arena pause/resume after its
  game-audio clock ownership correction.
  October 22 audit: the remaining gates require physical devices and target-OS
  audible-output checks. Counted API recovery tests and the long local voice
  session support those checks but do not replace them.
  October 23: a lost or reopened output no longer ends the one-shot sounds it
  stopped. Dialogue and effects restart at the scene clock with loops and queues,
  no voices are allocated while the endpoint is gone, and a line paused across
  the change resumes in place instead of from its first sample. A simulated
  outage (`s_testDeviceInterruption`) qualifies this path in stock gameplay;
  physical endpoint loss, hotplug and OS breadth remain the gate.
  [Output recovery evidence](audio-voice-evidence.md#output-recovery-layout-changes-and-memory-pressure-23-october-2026).
  October 24: the output-recovery, subtitle and scene harnesses pass on Linux x64.
  That is Ubuntu 24.04 under WSL2 with Mesa llvmpipe and the system OpenAL Soft 1.23.1,
  covering output loss, paused resume, live layout and HRTF resets and both stream
  fixtures. A long session replays six maps' openings three times in one client
  with flat audio residency, voices and stream reservations. Physical devices,
  hotplug, macOS and other distributions remain the gate.
  [Linux and long-session evidence](audio-voice-evidence.md#linux-voice-output-loss-and-long-sessions-24-october-2026).
  October 24, second round: loading a saved game no longer moves every restored sound
  forward by the load time. The sound clock advances once per mixed frame and none is
  mixed during a load, so the session's pause and unpause read the same stale time;
  pausing now refreshes it, and reading a saved sound state pauses the world until the
  map change completes. Restored captions now resume 0.4–1.0 s after their saved point
  across a 500 ms wait, where they had resumed 1.4–3.6 s later. The subtitle and
  Awakening dialogue harnesses had saved inside opening cinematics, which refuse saves,
  so their earlier save/restore passes were vacuous; both now check a real save and
  load. No Apple host is available for macOS. Physical devices, hotplug, macOS and
  other distributions remain the gate.
  [Save and load evidence](audio-voice-evidence.md#real-saves-in-the-subtitle-and-dialogue-harnesses).
  October 25: Linux builds of main crashed before `main` (the multiview demo's
  constructor reset a cvar not yet constructed under GCC's link order); fixed. A real
  save and load then passes on Linux with Dialogue Volume and subtitles intact, once the
  test build carries main's build number: a short exported history had made it misread
  its own saves, which an earlier note here took for a Linux defect. The Arena
  pause/resume harness also passes on Linux (won and lost matches; the game world's clock
  holds through each pause). Hearing it on Linux remains a listening check.
  [Linux evidence](audio-voice-evidence.md#saves-on-linux).
- [ ] **AUD-03 — P2, implementation and qualification: bounded decode and compressed
  streaming.** Current decode admission bounds job count, while each job can
  allocate a complete PCM sample. Measure compressed input, pending decoded PCM,
  retained CPU data and uploaded OpenAL buffers together; set byte budgets and
  backpressure where needed, including Android low-memory/map-cancel cases.
  Add bounded streaming for music/VO with correct leadin/loop, seek/save restore,
  pause/time scale and underrun behavior. Existing queued PCM and decode jobs do
  not by themselves establish compressed streaming. Android's required memory
  bound can ship before optional streaming completion; keep those exits separate.
  October 21: compressed input, fixed decoder arenas and pending Vorbis PCM
  have a 64 MiB admission budget with backpressure and cancellation accounting.
  Ogg tracks above 512 KiB decoded size now stream from the VFS/PK4 through
  fixed input/decoder buffers and bounded PCM queues, with a separate
  32 MiB active-stream reservation limit. PCM/seek/overlap/loop tests and stock/
  Awakening gameplay at a 16 MiB decode budget pass pause, time scale, restart,
  save/load and gain checks without rejected tracks. `listSounds` reports sample,
  envelope, pending decoder and stream costs. A joint 256 MiB managed-payload
  budget now admits CPU/OpenAL data, envelopes, decoder arenas and conservative
  queues together, including WAV/ADPCM and bounded two-pass RoQ loading.
  Stock/Awakening tests pass at 192 MiB; deliberate exhaustion retains active
  audio, refuses new data without caching beeps, retries after budget recovery
  and survives repeated restart without reservation growth. Metadata/provider
  process memory, lengthless streaming, loading-performance, Android low-memory
  callbacks and device breadth remain open.
  [Implementation and evidence](audio-voice-evidence.md#aggregate-payload-admission).
  The October 22 queue correction retains about one second of audio within the
  same reserved bytes. This item still includes automated and implementation
  work as well as physical/mobile qualification; it is not a listening-only gate.
  October 23: an operating-system low-memory notice (`SDL_EVENT_LOW_MEMORY`)
  now frees earlier levels' samples, CPU copies OpenAL also holds and this
  level's samples idle for `s_trimIdleSeconds`, never anything a channel uses;
  freed samples reload when played. Ogg files without usable length metadata
  now stream, counted by their envelope pass and seeking sample-exactly through
  a small stb_vorbis extension; the one-hour streamed-track limit is gone, and
  the envelope pass runs on job workers during level loads. A notice from a real
  Android/iOS device, provider/process memory and device breadth remain open.
  [Memory and streaming evidence](audio-voice-evidence.md#output-recovery-layout-changes-and-memory-pressure-23-october-2026).
  October 23, second round: a streamed track started on a recycled voice never
  played, because OpenAL counts buffers queued on a stopped source as already
  processed. Once the voice pool had cycled, music, long lines and late ambience
  loops started and stopped every frame. Flushed sources now rewind, and a
  production queue test covers the case. A sweep of every stock and Awakening map's
  opening finds streams playing on recycled voices and records each level's
  residency: at most 193.8 and 242.1 MiB after an opening, with a managed-payload
  peak of 255.7 MiB under the 256 MiB budget and no refusals. Device and mobile budgets
  remain open.
  [Scene and residency evidence](audio-voice-evidence.md#authored-scenes-centred-reverb-and-portrait-captions-23-october-2026).
  October 24: replaying six maps' openings three times in one client leaves each
  map's residency flat, within 83 KiB of its first pass, with voices and stream
  reservations unchanged. On Linux the trim, both stream fixtures and recycled-voice
  streams pass as on Windows.
  October 24, second round: `listSounds` reports the process's private and resident
  memory on Windows, Linux/Android and macOS, and `audio_scene_audit.py
  --silent-baseline` compares each opening with a run under `s_noSound 1`. In two pairs
  of runs over six stock openings on Windows, audio adds 116–207 MiB of private memory,
  and at most 60 MiB of it lies outside the managed payloads (provider mixer, effects
  and device buffers, metadata); the rest of the process varied by up to 76 MiB between
  runs, so that overhead is bounded rather than measured exactly. Real Android/iOS
  low-memory notices, device and mobile budgets and provider memory on those devices
  remain open.
  [Process memory evidence](audio-voice-evidence.md#process-memory).
  October 25: `testLowMemoryNotice` pushes the operating system's low-memory event
  through SDL's queue and event watchers, and the output-recovery harness now trims
  through it on Windows and Linux. That found every notice trimmed twice (the watcher's
  pending pass and then the queued event); one notice now trims once. A notice from a
  real Android or iOS device remains open: the Android emulator set-up used for the port
  was removed with its worktree, and restoring it needs a download approved by the user.
  [Low-memory notice evidence](audio-voice-evidence.md#the-operating-systems-low-memory-notice).
- [ ] **AUD-04 — P2, implementation and qualification: surround and mod reverb.**
  Resolve `SSF_CENTER`/VO placement on 5.1 with stereo/HRTF fallback and nonzero mod
  room-rolloff unit handling. Keep stock zero-rolloff behavior intact.
  October 21: mod rolloff is repaired and measured using OpenAL loopback audio;
  stock zero rolloff and the dry mix are unchanged. Mono global/radio dialogue
  and explicit `SSF_CENTER` sounds now use bounded direct 5.1 buffers on a
  centre-capable output. Positional NPC speech retains its direction. Loopback
  tests (26 checks) measure exclusive centre placement, the configured VO
  fraction and silent LFE on 5.1, 6.1 and 7.1 outputs, plus identical stereo,
  quad and HRTF fallback PCM; stock compressed and Awakening PCM
  gameplay pass pause, time scale, restart and restore. Physical surround
  devices, output-layout transitions and positional mod wet sends remain open.
  [Centre evidence](audio-voice-evidence.md#centre-channel-placement).
  October 23: speaker-layout, HRTF and limiter changes reset the open device in
  place instead of reloading every sample, and playing voices re-route at their
  scene clock when the output gains or loses a centre speaker, including after
  a reopen onto another endpoint. Stock gameplay qualifies live 5.1, stereo and
  HRTF transitions on rendered output. Physical surround devices and positional
  mod wet sends remain open; stock content has no centred positional sound.
  October 23, second round: OpenAL applies no distance attenuation to the direct
  5.1 channels of a centre-routed voice, so a mod reverb's room rolloff never
  reached its sends. The engine now applies it. On OpenAL loopback, those sends
  now fall off exactly as a mono source's do (half at twice the minimum
  distance, a quarter at four times) with standard and EAX reverb, and stock
  presets are unchanged. Physical surround devices remain open.
- [ ] **AUD-05 — P1, implementation: functional multiplayer voice.** Capture,
  codec, transport and playback are implemented with explicit microphone consent,
  push-to-talk, local-only testing, connection-scoped mute, server moderation and
  existing recipient policy. The versioned 8 kHz mu-law path uses independent
  unreliable 20 ms frames, 80 ms capture/playback rings, late/duplicate/rate
  rejection and a server mailbox flushed after snapshots without retry backlog.
  October 21: Windows listen and dedicated servers with three real client
  processes pass bidirectional rendered-tone playback, volume, real-time pitch,
  mute/receive/moderation, restart, reconnect/slot reuse, loss/reorder, delayed
  frame rejection and congestion recovery. At 2,000 B/s voice yields while
  snapshots continue. Capture fault/lifecycle tests execute production code
  without a microphone. The real settings adapter and SDL dummy callback path
  also pass local-only testing, tab-exit closure and consent revocation, with
  inspected retained and Classic engine screenshots. Voice admission now tracks
  sender clock drift with a bounded slew (at most 2 ms per second of accepted
  traffic), and a simulated 24-hour drift/jitter/loss test passes. Physical
  capture, endpoint hotplug, independent-machine clock drift and OS/device
  breadth remain open; retain these qualification gates.
  October 22: the saved dedicated-server/two-client session reports 7,168.9 s
  (1:59:29), with all 120 intervals passing, continued admission after reconnect,
  bounded queues and rejected delayed backlog. Its old duration check allowed
  one missing interval, so this is long-session evidence, not a full two-hour
  pass. The harness now measures through the requested endpoint and excludes
  shutdown time. The requested two-hour rerun was cancelled at the user's
  direction; it is not required to record the useful existing evidence honestly.
  [Session evidence and limits](audio-voice-evidence.md#long-session-voice-evidence-22-october-2026).
  [Implementation and evidence](audio-voice-evidence.md#multiplayer-voice).
  October 24: voice playback follows an output loss like every other source.
  Frames are dropped while the endpoint is gone instead of being queued late, and
  playback starts again with the next frame after it returns. A simulated outage
  during the local test qualifies this with the real settings adapter. Physical
  capture and playback endpoints remain open.
  [Voice output-loss evidence](audio-voice-evidence.md#voice-chat-across-output-loss).
  October 24, second round: voice chat passes on Linux x64 (Ubuntu 24.04 under WSL2,
  OpenAL Soft 1.23.1): all 20 checks against a listen server and against the
  dedicated server, each with three real client processes, covering both directions,
  mute, volume, pitch, moderation, restart, loss and reorder, congestion and
  reconnect. Software rendering could not keep the clients above the 50 Hz voice
  cadence, so the runs use WSL's GPU paravirtualization. Physical capture and playback
  devices, independent machines and their clock drift, and other systems remain open.
  [Linux voice evidence](audio-voice-evidence.md#linux-voice-chat).
  October 25: `s_voiceTestClockRate` (test only) runs a sender's voice clock fast or slow,
  and `voice_chat_runtime.py` drives the peer 0.1% slow and then 0.1% fast through 10 s
  of speech each: the host's admission baseline follows it up and down and rejects no
  frame. On Linux x64 all 22 checks pass through a listen server and the dedicated
  server; the baseline rose 8–9 ms and fell 7–10 ms over about 10 s each, rejecting
  nothing. Separate machines with their own clocks remain open.
  [Drift evidence](audio-voice-evidence.md#voice-clock-drift-between-real-peers).
- [ ] **AUD-06 — P2, implementation and qualification: independent dialogue volume.**
  Address [#187](https://github.com/themuffinator/openQ4/issues/187) with a localized,
  persistent spoken-dialogue control in UI-01. Inventory actual dialogue routing
  across NPC speech, radio/comms, cinematics, scripted/BSE sources and Awakening;
  do not assume one channel or the multiplayer voice-chat CVar covers them all.
  Keep master/effects/music controls and authored distance/reverb behavior coherent.
  Qualify overlapping speech, mute, pause, save/restore, locale fallback and device
  changes, including proving that the control does not mute unrelated weapon audio.
  October 21: the localized archived control and central playback routing are
  implemented. Stock/Awakening gameplay tests cover overlapping speech/effect
  fixtures, mute, pause, time scale, restart, save/load and Turkish text with
  English audio fallback. Complete authored NPC/script/BSE/cinematic scene and
  physical device coverage remains open. [Evidence](audio-voice-evidence.md).
  October 23: an installed-asset routing audit classifies every reference to a
  speech shader with the production classifier. All 3,672 stock and 5,153
  Awakening-layer speech shaders are dialogue by sample path. Their 3,495 and
  4,912 references in NPC lip-sync keys, definitions, maps and scripts all
  follow the control, and no effect or GUI references one. Audible scene and
  device coverage remains open.
  [Routing audit](audio-voice-evidence.md#dialogue-routing-audit).
  October 23, second round: `s_logSpeech` and `audio_scene_audit.py` check speech
  as it plays. The first 30 s of all 29 stock and 13 Awakening maps, cinematics
  included, start 148 dialogue lines. No declared or flagged speech misses the
  control, and muting it silenced every dialogue voice in 378 probes while effects
  continued. Dialogue Volume also holds through a simulated output loss and in-place
  HRTF resets. Scenes past each opening, which need player input, and physical
  devices remain open.
  October 24, second round: `audio_scene_audit.py --walk` reaches the scripted scenes
  past each opening without input. It fires every entity whose script makes a
  character speak, and every radio chatter, in authored order, leaving out anything
  that ends the level. In all 29 stock and 13 Awakening maps, 689 triggers start 1,326
  dialogue lines, 1,050 of them distinct; no declared speech missed the control and no
  dialogue played while muted in 941 probes, with effects continuing. Dialogue
  Volume's 0.6 now also holds across a real Awakening save and load. Scenes play out
  of their gameplay order, so this is routing coverage, not a playthrough; physical
  devices remain open.
  [Scripted-scene evidence](audio-voice-evidence.md#scripted-scenes).
  October 25: with 90 s for each map's last conversations, the walk starts 1,766
  dialogue lines across both campaigns and silences every dialogue voice in 1,949 muted
  probes while effects continue; Dialogue Volume's 0.6 holds across a real save and load
  on Windows and Linux. Lines that wait on the player moving or fighting, and physical
  devices, remain open.
  October 26: the progression audit now distinguishes dialogue from effects that
  reuse the same sample by joining each audible source to its current channel.
  Candidate contact triggers exclude non-player/activation-only cases, and level-end
  targets are suppressed in disposable sessions so nearby or delayed exits cannot
  invalidate the scene chain. The previous run's three failed chains are retained
  as failures. Attempts and missing entities remain explicit; these probes do not
  replace an ordered playthrough or physical listening.
  [Progression-audit evidence](audio-voice-evidence.md#progression-audit-recovery-26-october-2026).
- [ ] **AUD-07 — P1, decision, implementation and qualification: dialogue subtitles.**
  Address [#186](https://github.com/themuffinator/openQ4/issues/186) with UI-02/UI-04.
  Inventory dialogue-to-text/localization data available from installed assets and
  define licensed authored mappings for gaps; do not add extracted proprietary
  transcripts to the repository. Drive cues from actual voice playback, covering
  interruption, overlapping/radio speech, pause/time scale, save/restore and locale
  fallback. Provide readable scaling, contrast/background, speaker identification
  where available and persistent controls. Qualify SP/Awakening scenes and stereo
  HUD placement; distinguish subtitles from optional full sound-effect captions.
  Missing transcript coverage must remain explicit rather than counted as complete.
  October 21: installed-asset inventories identify 2,268 same-name lip-sync/text
  mappings among 3,672 stock speech candidates, and 1,512 among 1,584 Awakening
  candidates. Reports retain identifiers and localization keys, not transcripts.
  The playback-driven overlay now uses verified single-variant shader or full
  sample-path lip-sync text. Localized, archived enable/size/background/speaker
  controls work in both UI modes. Stock and Awakening runtime tests pass muted
  audio, interruption/overlap, pause/time scale, restart, save/restore and English
  text fallback under Turkish. Pagination and native layout checks pass; missing
  transcripts are skipped. Portrait Awakening title-card collisions, the full
  scene/locale corpus and physical stereo-HUD qualification remain open.
  [Subtitle evidence and mapping policy](audio-voice-evidence.md#dialogue-subtitles).
  October 23: the inventory reports per-language transcript coverage. The Steam
  install's 2,268 mapped lines exist only in English tables; its French,
  Italian and Spanish tables carry no lip-sync text. Awakening's 1,512 lines are
  literal English declarations. Other menu languages therefore show English
  captions, which the player guide now states.
  October 23, second round: on screens narrower than 4:3, cinematic captions now
  hang below the cinematic artwork when two lines fit there, clear of Awakening's
  title text (visual specification 1.25; captures inspected). A lipSync default the
  game makes up for an undeclared name no longer hides a sample-path transcript.
  In the map-opening sweep 65 of 148 dialogue starts have a transcript. Every cue
  shown had one, and no transcribed line lacked a cue while a slot was free. The
  full scene/locale corpus and physical stereo-HUD qualification remain open.
  October 24: the stock subtitle runtime harness (16 checks) and a four-map speech
  sweep also pass on Linux x64.
  October 24, second round: the scripted-scene walk shows 1,050 distinct dialogue
  lines in both campaigns, 727 with a transcript; every cue shown had one, and no
  transcribed line lacked a cue while a slot was free. At the largest text in a
  portrait cinematic only one line fits below the artwork, so a lone caption now pages
  a line at a time there instead of covering the title text, and a speaker name no
  longer runs under the words: it is left out when wider than half the line (visual
  specification 1.26). Subtitle save/restore is now exercised by a real save and
  load, and captions resume where they were saved. The rest of the scene corpus,
  overlapping captions below a portrait picture at the largest text and physical
  stereo-HUD qualification remain open.
  [Caption evidence](audio-voice-evidence.md#the-largest-captions).
  October 25: two overlapping captions at the largest portrait text now take one line
  each, the newer above the cinematic artwork and the older below it, both clear of the
  picture (visual specification 1.27). Subtitles pass a real save and load on Linux too,
  resuming where they were saved. With 90 s for each map's last conversations to finish,
  the scene walk starts 1,179 distinct dialogue lines (842 transcribed), every cue shown
  with a transcript and no dialogue heard while muted. Lines that wait on the player
  moving or fighting and physical stereo-HUD qualification remain open; the installed
  game has transcripts in English only, so other languages show English captions.
  [Two-speaker evidence](audio-voice-evidence.md#two-speakers-at-the-largest-text).
  October 26: progression probes pair current-channel dialogue attribution with
  subtitle identifiers and transcript status, without recording proprietary text.
  Interrupted map chains are not accepted as full scene coverage. Complete
  gameplay-order coverage and physical stereo-HUD placement remain open.
  [Progression-audit evidence](audio-voice-evidence.md#progression-audit-recovery-26-october-2026).

## Desktop platforms and packaging

Sources: [platform support](platform-support.md), [Deck QA](steam-deck-qa.md),
[Linux ARM64 evidence](linux-arm64-signoff-evidence.md),
[macOS policy](macos-support-matrix-policy.md), [macOS evidence](macos-signoff-evidence.md).

- [ ] **PLAT-01 — P1, qualification: Linux x64 and Steam Deck hardware breadth.**
  Broaden physical GNOME/KWin/wlroots/SteamOS, Wayland/XWayland, audio/input hotplug,
  suspend/resume, HiDPI/display and package coverage. Execute the Deck hardware
  checklist across available LCD/OLED/game/desktop modes and Steam Input routes.
  Its engineering implementation is already present; hosted compositor/WSLg tests
  cannot substitute for its remaining physical-device checks. Retain the specific
  CachyOS/RTX 3070 pacing report until matching evidence closes it.
- [ ] **PLAT-02 — P1, qualification: Linux ARM64 promotion.** Use physical
  desktop-GL-capable AArch64 hardware for native-Wayland SP/save-load, MP peers,
  packaged dedicated/client, audible audio, input and display recovery. Bind the
  accepted reports to exact candidate archive/runtime hashes under the existing
  evidence policy before changing the preview release tier.
- [ ] **PLAT-03 — P1, implementation and qualification: Windows ARM64 client.**
  Add automated client startup/renderer/audio evidence, then real-hardware SP/MP,
  save/load, controllers and package checks using available GL/Vulkan drivers.
  Native build/stage and assetless dedicated startup already exist and do not
  establish client support.
- [ ] **PLAT-04 — P1, qualification: macOS arm64 release evidence.** Use real Apple
  hardware/compliant Apple-hosted workflow at the documented macOS 11 floor and
  latest supported public macOS. Qualify both published package variants, SP/MP,
  dedicated scope, Retina/displays, input/audio, Finder/terminal/DMG/copied-app
  discovery, signing/notarization/Gatekeeper and matching symbols. Run the existing
  full Signoff/CollectResults validators before first-class claims. Legacy evidence
  schema companion-SHA fields must describe the in-tree source identity.
- [ ] **PLAT-05 — P2, investigation and qualification: Apple renderer and
  packaging decisions.** Recheck historical points/pixels/letterbox, failed
  context/MSAA preferences, VBO workaround cost and module transitions on current
  hardware. Qualify MoltenVK separately; retain its translation-layer description.
  Intel/universal2/Rosetta publication, removing the Metal-bridge package, native
  Metal and retiring comparison backends require explicit policy and evidence
  decisions; an old plan is not authorization to change the current release matrix.

## Engine loading safety and developer tools

Sources: [job service](parallel-job-system.md), [loading caches](loading-cache-modernization.md),
[runtime refinements](core-runtime-refinements.md), [map workspace](level-editor-modernization.md),
[AAS compiler](aas-compiler.md), [dmap](dmap-compilation.md).

The broader map-editor implementation remains **paused** as recorded in its
reference. TOOL-01 through TOOL-04 preserve the outstanding scope; this planning
consolidation does not resume it. Compiler/runtime compatibility fixes and the UI
visual editor have their own active items.

- [ ] **CORE-01 — P1, qualification: foundation final-package evidence.** Repeat
  job on/off, renderer-budget, retail baseline, map-cycle/cancellation and
  deterministic dedicated shutdown checks on clean committed final packages and
  required platforms. Keep installed content read-only and dedicated BSE disabled.
  Progress, 26 October: three map-cycle defects a Codex AUD-03 round found on
  23 October are fixed. Map precache instantiated the multiplayer menu GUIs
  afresh on every load and never freed them (about 1.1 GiB over its community
  map tour); precache now reuses the GUI template. Every interface window
  reserved 4,096 expression entries up front (the bulk of The Awakening's
  Trian Factory growth); the storage now grows on demand. Windows never drawn
  kept unset coordinates that abort an autosave. Hit maps are freed by the
  engine's own allocator through the declaration manager (renderer API 25).
  Static fixtures with failing controls cover each; the map tours themselves
  were not rerun on main, and the tour harnesses remain to be refitted to
  main's audio interfaces.
- [ ] **CORE-02 — P2, optimization: cache and loading promotion.** Investigate cold
  load regressions, per-file Sync, cache identity and rewrite churn before enabling
  learned/generated-cache experiments. Default worker DDS reading already exists.
  Require cold/warm baselines, corrupt-cache fallback, bounded memory and cancel/
  restart/disconnect coverage; generated data remains disposable under savepath.
- [ ] **CORE-03 — P2, implementation: justified background asset work.** Extend
  detached decode/transcode/upload scheduling and portal-aware prioritization only
  after profiling, with immutable ownership handoffs, backpressure, cancellation
  and main-thread adoption. Do not move game callbacks, GL calls or live parsers
  onto workers without a proven lifetime and thread-safety contract.
- [ ] **CORE-04 — P2, implementation and qualification: compiler and safety debt.**
  Strict-string compliance is done (31 October): the sources are const-correct
  and `/Zc:strictStrings-` is removed, so MSVC enforces it. Broaden sanitizer
  gameplay and focused fuzzing for remaining parsers, event normalization and
  geometry. Preserve existing bounds, malformed-network rejection, archive/path
  safety and source provenance. Track reproduced issues individually rather than
  treating prior hardening as proof of every parser or full restore transaction.
  Reproduce historical RmlUi wrap/revision and four native input fixtures' SDL
  subproject-header warnings on a fresh current configure; repair remaining
  dependency/header mismatches without updating dependencies solely to satisfy
  an obsolete warning. Recheck lazy pre-cache/missing-retail-image diagnostics
  separately.
- [ ] **CORE-05 — P1, implementation and qualification: safe compiled geometry.**
  Resolve `hub2`'s generated 36,635,340-index runtime surface against the current
  16,777,216-index limit through safe partitioning or representation changes; do
  not simply raise safety bounds. Address `MAX_SIL_EDGES` and remaining compiler
  diagnostics individually. Gate on successful compile, runtime load, rendering,
  collision and AI behavior with bounded memory. Reproduce the historical `hub2`
  failure with the current compiler before selecting a representation change.
- [ ] **CORE-06 — P2, implementation and qualification: AAS tactical generation.**
  Complete cover/lean/corner/pinch/vantage generation or retain an explicit
  rebuilt-map limitation; path/swim compilation already exists. Test stock/custom
  rebuilt data and actual AI consumers, including malformed or absent tactical
  data. These compiler gaps remain independent of the map-editor pause.
- [ ] **TOOL-01 — P2, implementation: complete the map workspace.** Build on the
  existing transactional document service and source preview. Add docking,
  orthographic views/camera persistence, richer entity/model previews, typed
  properties and linked diagnostics. Qualify localized responsive layouts, native
  input/high DPI and desktop backends. This is not restored MFC Radiant.
- [ ] **TOOL-02 — P2, implementation: geometry and asset authoring.** Add lossless
  brush/patch adapters, grid/snapping/transforms, texture locking, one undo action
  per gesture, layers/groups/prefabs and safe rename/reference repair. Make asset
  indexing asynchronous and bounded. Test cancellation, mixed selections, large
  maps and preservation of untouched source.
- [ ] **TOOL-03 — P2, implementation: build and play workflow.** Integrate dmap/AAS
  progress, cancellation and source-linked diagnostics with an isolated play
  sandbox and editor-state restoration. Preserve previous playable output on
  failure. Consume CORE-05/CORE-06 compiler capabilities and qualify stock/custom map
  round trips and AI use of rebuilt data.
- [ ] **TOOL-04 — P2, qualification: authoring reliability.** Cover forced I/O
  failure, external edits, atomic saves, recovery, repeated undo/redo, renderer
  switches and Windows/Linux/macOS workflows. Declare startup/edit/selection/view
  latency budgets. Keep the known unbuilt legacy Radiant defects distinct from
  the supported experimental workspace and the separate UI visual editor.
  Reproduce and fix workspace labels disappearing after full video restart on
  GL/Vulkan; Vulkan reactivation helping one run does not close both backends.

## Deferred design decisions

These preserve uncompleted ambitions from old plans without presenting them as
promised features or retail compatibility defects. Promote a decision into a
workstream with scope, dependencies and acceptance before implementation.

| ID | Deferred scope and prerequisites |
| --- | --- |
| OPT-01 | Higher authoritative simulation rates: separate physics/script/prediction/demo/content design after HF-01 through HF-07; do not change tick rate to claim higher FPS. |
| OPT-02 | GPU skinning promotion for animation-heavy content: batch GL dispatches/persist source buffers and prove benefit. Current stock workloads did not justify default promotion. |
| OPT-03 | True HDR10/scRGB output, sparse/virtual textures, broad GPU-driven/async-compute work, background shader compilation, shadowed light-injected volumetrics and world-space GI: require measured need, target content/hardware and classic rollback. |
| OPT-04 | Live relay/delayed broadcast, multi-POV/multi-window audio, compression/export, persistent competitive identity/web/brackets: separate security, capacity, backpressure and reconnect designs. Forced client screenshots remain rejected. |
| OPT-05 | Overload/Harvester/Domination/Attack and Defend: keep unavailable until authoritative objectives/scoring, networking, bots and UI exist. Race/factories/rulesets remain outside accepted current scope. |
| OPT-06 | Awakening's unreachable grapple/concussion/tether/gravgun/compass content: explicit content-completion project, not a blocker for the integrated SP campaign. No expansion multiplayer module. |
| OPT-07 | HDR audio, ducking, geometric obstruction and reverb editing: optional authored-mix changes after retail audio qualification. |
| OPT-08 | Additional idTech titles, wholesale SP/MP source unification, broad third-party feature ports and extra cosmetic color syntax: require a product/maintenance decision, source/license review and concrete benefit; the old TODO is not an implementation mandate. |

## Release qualification and completion rules

- [ ] **REL-01 — P1, qualification: exact final-package matrix.** Build with
  project Meson wrappers/presets and restage current engine/renderer/game modules
  into `.install/`. Run SP, MP listen/client and dedicated roles against installed
  original assets, including campaign/mode transitions and save/demo cases.
  Record both passed and failed configurations, source/build identities and logs;
  no stale staged package or menu-only run qualifies gameplay.
  Use complete Git history for release version/save-layout generation; the current
  shallow-history rejection protects against erroneous legacy build-1 stamps.
  Test both clean installs and upgrades with existing archived CVars/configs,
  bindings, saves and mod selections. Qualify any default/config migration without
  erasing player choices merely to make upgraded installs match fresh defaults.
- [ ] **REL-02 — P1, qualification: visual and human evidence.** Use engine
  `screenshot` render-target captures and matched viewpoints/time. Perform actual
  motion, input latency, audible sound, accessibility and VR comfort checks on
  appropriate devices. Default to windowed tests. Fullscreen and input control
  require the project's specific prior user permission; never substitute OS
  screen capture or silently infer permission from a historical test plan.
- [ ] **REL-03 — P1, qualification: lifecycle and failure matrix.** Cover cold/warm
  starts, assets/cache failures, long sessions, map/campaign/mode changes, network
  loss/reconnect, saves/demos, device/surface/audio loss, resize/high DPI, pause/
  slow motion, background/suspend, shutdown and renderer/module recovery. Verify
  bounded resources, fallback and user-visible error behavior without lowering
  gates to turn historical failures green.
- [ ] **REL-04 — P1, documentation and promotion: close only proven scope.** Keep
  published downloads distinct from current-source features. Record withdrawals
  in the shared release-status policy, player status page and historical notes;
  exclude them from version/changelog baselines without deleting evidence.
  Reconciliation on 23 October 2026 restores 0.13.2 as latest and marks 0.14.0
  withdrawn. Update
  this roadmap, UI acceptance registers, support/capability matrices and curated
  release notes together. State hardware/backend exclusions, upgrade/reinstall
  requirements and opt-in defaults. Use the existing renderer promotion token,
  platform evidence validators, signing/symbol/licensing/package checks. A scoped
  local implementation pass, synthetic device or archived branch is not product
  acceptance. Record a deliberate exclusion when a feature will remain deferred.

## Plan archive and traceability

The [archive index](archive/2026-10-18/README.md) and
[relocation manifest](archive/2026-10-18/manifest.json) map every superseded planning
file to its retained location and the owning roadmap sections. The manifest binds
original Git content and migrated archive bytes. Historical design constraints
used by tests and UI requirement sources remain traceable; their paths and hashes
are updated without accepting or deleting requirements.

Keep new work in this roadmap. Put implementation contracts, investigations and
validation evidence in the relevant technical reference, linked from the task.
Add newly discovered gaps here with a stable ID and acceptance condition instead
of starting another competing plan.
