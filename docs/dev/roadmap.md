# openQ4 development roadmap

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.13.2 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Updated 8 November 2026 with the frame-rate recovery category (FPS). The progress
review below otherwise dates from 4 November and uses the integrated source and
recorded evidence through `c09500893cea22bd7becd7a1b6ece5439c408cde`. The MP-02
closeout and task counts additionally include its later integrated safety
qualification and the accepted timing-contention caveat of 4 November.
Uncommitted changes and work on other branches are not credited. This documentation
review did not rerun gameplay or qualify a new package. Each evidence entry retains
its own tested source, binaries, assets and limitations. The earlier stage-one
baseline is `7674acef91c2653e8c6153ae11e7b158c05e52ff`, recorded in
[baseline evidence](baseline-evidence.md); it is not the revision of every later test.

## Progress overview

**Implementation is substantially ahead of formal acceptance.** High refresh,
multiplayer and audio are furthest along among the unfinished workstreams. Full
campaign progression, physical-device qualification, rendering performance and the
UI toolkit/editor/corpus remain major completion gates.

| Progress measure, 8 November 2026 | Current result | Meaning |
| --- | --- | --- |
| Formally closed roadmap tasks | **36 / 125 (28.8%)** | **89 remain open**. Counts all stable top-level task checkboxes, including Q4-01 through Q4-09 and FPS-01 through FPS-09, not partial substeps. |
| Estimated category progress | **19 categories: 3 at 100%, 14 partial, 2 at 0%** | Governance (GOV), engine loading (CORE) and developer map tools (TOOL) are complete; the detailed estimates follow. |
| UI active requirements | **0 / 312 verified; 117 partial, 195 pending** | Excludes 40 superseded rows from the 352-row register. |
| UI corpus and final gates | **0 / 358 migration variants accepted; 0 / 7 final gates accepted** | Working screens and passing checkpoint validators do not grant product acceptance. |
| Quality frame-time reference | **Gate still failing** | Recorded Vulkan reference: 8.76 ms mean, 13.80 ms p99, 24.08 ms maximum against **every frame <=6.67 ms**. Later prototype runs do not close it. |
| Ultra frame rate at 2560x1440 | **91-188 fps on GL, 96-188 fps on Vulkan, on the reference laptop; every tier meets its refresh class** | FPS-02 re-tiered the presets to refresh classes (Enhanced 144/90, Quality 100/60, Ultra 75/45 average/1% low) and all three meet them in every scene on both backends; the previous everything-on Ultra (19-58 fps on GL) is now the Maximum preset with no frame-rate target. Budgets are in [`performance-budgets.json`](performance-budgets.json). See the FPS category and the [performance report](performance-report.md). |
| Release acceptance | **0 / 4 REL tasks closed** | Current-source progress is unreleased; 0.13.2 remains the available release under the recorded release-status policy. |

### Category completion estimates

These are engineering judgments of progress toward each category's stated scope,
including implementation, integration and qualification. They are **not measured
fractions of effort, support certifications or release-readiness percentages**.
Allow roughly **±10 percentage points** for partial categories. A 0% means no
implementation/baseline acceptance is credited beyond the proposed scope; 100%
means the bounded category gates are closed, not that maintenance ends.

Credit working production paths and scoped behavioral evidence; retain substantial
remaining work for missing semantics, end-to-end play, physical devices, performance
and final-package acceptance. Do not average these percentages into a project total:
categories differ greatly in size, overlap through dependencies and include optional
work. Optional P3 scope remains labelled in its task; the nine OPT decisions have no
accepted implementation denominator and are excluded. The task closure column is
exact; the estimate column is deliberately approximate.

| Category / task family | Estimated completion | Tasks closed | Principal remaining work |
| --- | ---: | ---: | --- |
| [Governance and baseline evidence](#governance-and-baseline-evidence) — GOV | **100%** | 3 / 3 | Maintain evidence/default alignment as later changes land. |
| [High refresh presentation and input](#high-refresh-presentation-and-input) — HF | **80%** | 6 / 8 | Reference pacing, physical latency and visual motion review. |
| [Vulkan rendering](#vulkan-rendering) — VK | **70%** | 1 / 7 | Driver breadth, visible pacing, material/HDR/capture combinations and resource soaks. |
| [Modern rendering engine](#modern-rendering-engine) — REN | **50%** | 0 / 8 | Domain promotion, aggregate lighting ownership and combined-effect/performance qualification. |
| [Frame-rate recovery and performance budgets](#frame-rate-recovery-and-performance-budgets) — FPS | **30%** | 2 / 9 | Preset re-tiering, shadow-map/post/MSAA cost, equal-settings regressions, hitches and the release gate. |
| [Virtual reality](#virtual-reality) — VR | **75%** | 4 / 6 | Real-headset gameplay, comfort and frame budgets; Quest waits on AND-05. |
| [Android and mobile rendering](#android-and-mobile-rendering) — AND | **66%** | 1 / 5 | Device/lifecycle breadth, setup upgrades, mobile UI qualification and Quest. |
| [iOS, iPadOS and visionOS](#ios-ipados-and-visionos) — IOS | **5%** | 0 / 6 | Adoption decisions, patch integration, platform builds and hardware acceptance. |
| [New user interface](#new-user-interface) — UI | **30%** | 1 / 11 | Production toolkit/editor, full source-bound migration, input/accessibility and seven gates. |
| [Retail compatibility and campaigns](#retail-quake-4-compatibility-and-campaigns) — Q4 | **75%** | 1 / 9 | Full playthroughs, effect parity, broader restore graphs, platform/save evidence and campaign-demo breadth. |
| [Enhanced rigid-body and ragdoll physics](#rigid-body-and-ragdoll-physics) — PHYS | **0%** | 0 / 6 | Measured baseline, enhanced solvers, behavior preservation and integration. |
| [Multiplayer, bots, Arena and demos](#multiplayer-bots-arena-and-demos) — MP | **80%** | 3 / 9 | Human play, physical workflows and deferred peer qualification; retain MP-02's accepted timing caveat. |
| [Audio and voice](#audio-and-voice) — AUD | **80%** | 0 / 7 | Listening/microphone/device checks, mobile memory and dialogue/subtitle coverage. |
| [Desktop platforms](#desktop-platforms-and-packaging) — PLAT | **55%** | 0 / 5 | Deck/ARM64/macOS physical signoff and package/device lifecycle breadth. |
| [Packaging improvements](#pkg-completion-checkpoints) — PKG | **10%** | 0 / 6 | Update archives, pack split/trim, overlays and delta delivery. |
| [Steam integration](#steam-integration) — STEAM | **0%** | 0 / 6 | Identity/licence/distribution decisions before implementation. |
| [Engine loading, safety and compilers](#engine-loading-safety-and-developer-tools) — CORE | **100%** | 6 / 6 | None; macOS evidence stays with the platform gate. |
| [Developer map tools](#tool-completion-checkpoints) — TOOL | **100%** | 4 / 4 | None; macOS evidence stays with the platform gate. |
| [Release qualification](#release-qualification-and-completion-rules) — REL | **35%** | 0 / 4 | Exact package matrix, human/device evidence, lifecycle and promotion records. |
| [Deferred design decisions](#deferred-design-decisions) — OPT | **Not scored** | 9 decisions | Select scope and prerequisites before creating an implementation denominator. |

### Reading and maintaining completion checkpoints

Every category below has a current breakdown by its existing task IDs. **Closed**
means that task's selected acceptance scope is closed; **partial** means useful
implementation or evidence exists but its full gate remains open; **not started**,
**decision pending** and **paused** distinguish the other states. A row's
remaining column supplies the next completion substeps, not a new parallel backlog.
The detailed task retains its full conditions and evidence links.

- Update a checkpoint, its estimate, the dashboard counts and relevant evidence
  together. Do not close a task merely to align it with its estimated percentage.
- Dated progress paragraphs are historical checkpoints. A later result supersedes
  an earlier remaining-work statement only for the same behavior and configuration;
  failed runs, unsupported cases and untested platforms remain evidence.
- A test harness, synthetic device or map-start probe earns only its demonstrated
  scope. It cannot substitute for physical interaction, sustained performance or
  an ordered playthrough. A code fix still needs its applicable acceptance tests.
- Preserve deliberate scope boundaries: MP-05's selected demo scope, VR-06's active
  GL lifecycle scope, optional Q4-06/VR-05/PKG-06/IOS-06 work, the level editor's
  experimental label and the deferred real-WAN/cross-architecture MP peer gates.

### Status reconciliation in this review

- **Player Setup:** implemented in the Multiplayer page, with 2 November effect/
  readback evidence and 3 November scale/tall-layout work. Earlier statements that
  the page is missing are historical; full UI/editor/device acceptance remains.
- **Liquids:** the AAS water-jump flag defect is fixed and generated swim/water-jump
  links are checked. Static liquids, player/bot swimming and replicated air are
  selected scope; swimming monsters, new animations and moving volumes are deferred.
- **Compiled geometry:** CORE-05 is closed. Besides the repaired `hub2` sliver
  explosion and silhouette scratch, the #209 maps that took days now compile in
  minutes to unchanged output, collision clips decode as Quake 4 wrote them, and
  CORE-06 adds the AI's tactical features to rebuilt navigation.
- **Saves:** v5 portable encoding and Windows/Linux cross-loads exist; legacy readers,
  Apple/ARM64 qualification and historical-mod failures keep their separate gates.
- **Evidence limits:** the UI register remains wholly unaccepted at product level;
  Q4-07 records missing historical `.tmp/stage-two/` artifacts. Their recorded hashes
  preserve provenance but do not make absent artifacts independently reviewable.

## Planning authority

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
- Keep the progress overview and category checkpoints synchronized with accepted
  evidence. Historical test counts and dated results describe their original build,
  not an assertion that the same suite or package has just been rerun.
- Keep completed release material in [release completion](release-completion.md)
  and polished player notes in `releases/`. Those are release records, not a
  second development backlog. Keep the [capability matrix](engine-capability-matrix.md)
  and [platform support matrix](platform-support.md) aligned with accepted evidence.
- GitHub issues retain reports, reproductions and contributor discussion; PRs
  retain proposed changes and review. Link them to the work items here. A partial
  fix or merged PR does not close a report whose remaining symptoms are unproven,
  and a request for reporter evidence is not grounds for automatic stale closure.

## Report and contribution traceability

Locally reconciled 4 November 2026 against the evidence recorded below; this is not
a fresh GitHub status check. These links connect the reports and contributions
to the existing workstreams; they do not create separate schedules or promise a
release. GitHub remains the source for subsequent discussion and PR/check state.
The #209 entry was separately refreshed from GitHub and its supplied maps on
5 November; the other entries retain their individual evidence dates.

| Report or contribution | Owning work and remaining acceptance |
| --- | --- |
| [#168: disappearing menu text after preset changes](https://github.com/themuffinator/openQ4/issues/168) | UI-01/UI-07, REN-04: the earlier glyph reload fix is partial evidence; qualify current GL/Vulkan preset changes and seek confirmation of the intermittent symptom. The verified 31 October replacement package (`f0a86b0ec`, #205) was offered for the retest. |
| [#169: resolution scaling affects menus](https://github.com/themuffinator/openQ4/issues/169) | REN-04/UI-07: the contributor's 22 October Linux/OpenGL retest preserves menu sharpness in mode 1. Since 31 October legacy mode 0 crops only the full-display world render instead of the whole frame. At 25% on Windows x64, GL and Vulkan, the retained and stock title menus, the SP pause menu and the SP/MP HUD now match their native-resolution captures, including after `vid_restart`; MP keeps the world-in-corner fill-rate view. Await the reporter's confirmation; Linux, macOS and window resizing were not rechecked. |
| [#170: weapon effects, sound and reload animation](https://github.com/themuffinator/openQ4/issues/170) | Q4-03/AUD-01/HF-04: preserve the merged BSE sound-diversity fix; identify and qualify each remaining weapon/effect/animation symptom separately. The 26 October package could not save; the verified 31 October replacement (`f0a86b0ec`, #205) was offered for the retest. |
| [#181: rebuilt hub2 cannot load](https://github.com/themuffinator/openQ4/issues/181) | CORE-05: the compiler allocation crash was fixed by #182. The oversized surface came from zero-area slivers: T-junction repair kept splitting them after the map's 345 pipe patches moved to explicit 10×10 subdivision, leaving 12,302,653 of 12,601,692 triangles (97.6%) collinear. Since 31 October repair leaves slivers unsplit. The reporter's map compiles in about two minutes to 298,438 triangles (largest surface 153,000 indexes), loads into single-player gameplay and saves; stock `hub2` loses 55 slivers and two near-slivers. Since 1 November the renderer sizes its silhouette edge scratch to each surface instead of stopping at 65,536 edges, so the map's 78,794-edge pipe surface keeps every edge and loads without `MAX_SIL_EDGES` warnings (previously 25,951); no stock content reached the limit. The 4 November traceability update for #209 records the #181 reporter's confirmation that the rebuild succeeds; broader compiler acceptance remains CORE-05. See [dmap compilation](dmap-compilation.md#sliver-multiplication-in-t-junction-repair-31-october-2026) and [silhouette edges](dmap-compilation.md#silhouette-edges-on-large-surfaces-1-november-2026). |
| [#183: machinegun transparency](https://github.com/themuffinator/openQ4/issues/183) | Q4-01/VK-04/REN-08: closed on 28 October after the reporter confirmed the `2126a232` scope corrections on both GL/Vulkan, in separate and normal installations. This is a development fix, not a new release. The test package's separate save failure (#205) was fixed by #203; the replacement package and instructions were posted here on 31 October. |
| [#184: Tech Li at the hangar1 lift](https://github.com/themuffinator/openQ4/issues/184) | Q4-02: the supplied save identifies the Turbo/scripted-mover timing race; #198's fix landed on main in `2677fb87b` (29 October). The fresh stock boarding/lift/exit sequence passes on OpenGL and Vulkan on 4 November, and removing the mover exemption reproduces the stall. #189 is separate unreachable-patient recovery. The fix does not repair already-stuck saves, which still need a pre-lift save. |
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
| [#209: airdefense1/core1 excessive compile time](https://github.com/themuffinator/openQ4/issues/209) | CORE-05: the compile-time fix is on `main` since 6 November but in no Windows test build yet. Profiling the supplied maps against the retail packages found passes that scanned per-plane curve groups, crowded vertex bins or growing triangle lists once per triangle. With indexes and tails, the supplied `airdefense1` compiles in 4-5 minutes and `core1` in about 3, and six stock maps give byte-identical `.proc` and AAS output. The rebuilt `airdefense1` loads, renders and navigates in SP. The reporter's later logs predate that fix (their `walker` 4,046 s and `putra` 12,376 s of dmap; current source 83 s and 370 s). Their `walker` road streaks, reproduced on stock `walker`, were seams between consecutive patches welded by optimize grouping; fixed locally on 9 November, not yet published. A black sky (likely an `hidef2kbc72` cube map) and a slow `putra` load await reporter details. Keep open for confirmation on a published build. Separate from #181. See [compile time on dense patches](dmap-compilation.md#compile-time-on-dense-patches-6-november-2026) and [texture seams](dmap-compilation.md#texture-seams-between-consecutive-patches-9-november-2026). |
| [rebelancap/openQ4-ios: iPhone, iPad and Apple Vision Pro port](https://github.com/rebelancap/openQ4-ios) | IOS-01 through IOS-06: a tested external port (v1.1.0), pinned to withdrawn `v0.14.000` as patches over a pristine upstream; porting from it is preferred. Integrate shared fixes on main first, then the Apple targets, crediting its author. Its user reports do not qualify the integrated build. |
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
| Temporal rendering | Automatic TAAU supports subnative bilinear/bicubic scaling and dynamic resolution on eligible desktop GL/Vulkan paths; crop/nearest modes are excluded, and VR takes only explicit per-eye TAA (`r_temporalAA 1`), without dynamic resolution. Rigid, skinned and first-person weapon motion vectors and reactive effect coverage exist. Dynamic resolution and GPU skinning remain opt-in. GLES has no temporal resolve. |
| VR | The active source implements PC OpenXR on OpenGL (GLX/WGL, and EGL on Wayland) and Vulkan, headset submission without desktop presentation, stereo, tracked input, room scale, standing/seated play spaces, crouch, two-handed aiming, world-GUI pointing, per-eye TAA, haptics, comfort controls and vehicle views, all qualified on the simulated runtime. Quest standalone waits on AND-05; real-headset qualification remains open ([checklist](vr-headset-qualification.md)). |
| Android | Experimental ARM64/API 24+ GLES3 client, SDLActivity host, in-tree game modules, signed APK/release verification and dependency licensing are implemented. Onboarding/import/export, touch, the per-orientation touch layout editor, the background suspension save and multiplayer resume are integrated with scoped S25/emulator evidence. Physical device/lifecycle breadth stays open under AND-01 through AND-05. |
| UI | Many retained screens and HUDs now run by default. The register preserves 352 rows, 312 active requirements and 40 superseded rows. UI-10 reconciled retail/base/Awakening mounts to 358 source/include-bound variants; all remain unaccepted for migration. Historical hashes and invalidated inventory acceptance are preserved. |
| Retail and Awakening | Canonical SP/MP sources and BSE are in-tree. Awakening is additive SP content under `q4xbase`, not a separate multiplayer module. Retail decl/BSE audits, stock transition chains and selected Awakening-state probes have scoped evidence; full playthroughs remain open. Saves now write portable v5 with Windows/Linux cross-load evidence while retaining legacy readers; other target qualification remains Q4-06/Q4-07. |
| Multiplayer | Dual-stack networking, pure asset checks and bounded rcon2 exist. Actual MP defaults are `net_mpLagCompensation 0` and `net_mpPredictMode 0`. Clients advertise bounded remote extrapolation, and server rewind subtracts it from the displayed pose age; the coupled implementation and remaining peer qualification are recorded under MP-07. Retail 2.85 interoperability is outside the current openQ4 2.41 protocol contract. |
| Platforms | Windows x64 and Linux x64/Steam Deck are first-class platform targets; macOS arm64 and Linux ARM64 are previews; Windows ARM64 and Android are experimental. iOS, iPadOS and visionOS run only in an external port pinned to withdrawn `v0.14.000`; integration is IOS-01 through IOS-06. Renderer tiers are separate from platform tiers. |
| Foundations and tools | The bounded job service, worker DDS prefetch, generated-cache experiments, native dmap/AAS commands, save-format compatibility readers and experimental map workspace already exist. Their remaining scope is defined below. |

## Delivery sequence and dependencies

| Stage | Work to conduct | Exit condition |
| --- | --- | --- |
| 1. Establish the current baseline — complete | GOV-01 through GOV-03, HF-01/HF-02, Q4-01, UI-10; scope/recovery review for VR-01/VR-06 and AND-01 | Current-source ownership, clocks, compatibility inventory, tests and evidence identities agree. [Closure evidence](baseline-evidence.md) retains reproduced failures and later-stage qualification limits. |
| 2. Close product gaps — in progress | UI-01/UI-02 plus the first UI-06 editor round trip, UI-07 measurements and UI-09 toolkit work, then UI-03/UI-04 at accepted scope; Q4-02 through Q4-05/Q4-07/Q4-08, MP-01 through MP-09 and AUD-01 through AUD-07; selected VR-01 and AND-01/AND-03 scope; preserve completed VR-06 | Required player flows work through the intended UI and runtime; remaining unsupported cases are explicit and fail safely. |
| 3. Improve motion and rendering — in progress | Preserve closed HF-03 through HF-06 and VK-03; finish HF-07/HF-08, remaining VK-01 through VK-07 scope, REN-01 through REN-08 and FPS-01 through FPS-09 | Visible gameplay meets declared correctness, latency and frame-time budgets with rollback paths intact. |
| 4. Complete tools and broader quality | Remaining UI-06 scope, CORE-01 through CORE-06, PHYS-01 through PHYS-06, STEAM-01 through STEAM-05, IOS-01 through IOS-04, remaining P2 items (TOOL-01 through TOOL-04 closed 6 November 2026) | Authoring and runtime workloads meet their stated completion gates; optimizations are justified by measurements. |
| 5. Qualify packages and promote scope | PLAT-01 through PLAT-05, PKG-01 through PKG-05, STEAM-06, IOS-05/IOS-06, REL-01 through REL-04, UI-07/UI-08/UI-11, VR-02, AND-02 and selected AND-05 scope | Exact release packages pass the selected platform, device, gameplay, visual and human acceptance matrix. |

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
| Quest VR integration | VR-01's PC Vulkan/EGL integration and VR-06 lifecycle are in place; AND-01's flavor-aware package checks are in place; the standalone flavor's Gradle side, installation and distribution decision are AND-05. PC headset qualification (VR-02) can proceed independently. |
| Android sustained operation | AND-02 includes AUD-03/CORE-03 aggregate memory and cancellation behavior, not only renderer FPS. |
| Modern-renderer promotion | Each REN-01 shared-domain gate is separate from REN-02 aggregate visible-lighting ownership. Keep their defaults and rollback decisions independent. |
| Preset re-tiering (FPS-02) | FPS-01's recorded budgets first; the FPS-03, FPS-04 and FPS-05 cost reductions decide what each tier can afford. HF-07's Quality floor and FPS-02's Ultra budget are measured by the same suite on the same reference machine; neither is qualified from a hidden or windowed capture. |
| Content pack split, trim and overlays (PKG-02 through PKG-04) | PKG-01's verifier first proves identical bulk-pack bytes across platforms and consecutive candidates. Client-only packs need their pure-server exemption before the dedicated server drops them. |
| Enhanced physics default (PHYS-06) | PHYS-01's recorded baseline and budgets first; PHYS-04's behavior envelope and PHYS-05's save/multiplayer/demo integration must pass before `g_enhancedPhysics` defaults on. `0` always runs the unchanged legacy path. |
| Steam integration (STEAM-02 through STEAM-06) | STEAM-01's app-identity, licence and distribution decision first. Joining and invites use MP-08's interoperability contract and the existing MP join flow. Steam absent or disabled always runs the unchanged path. |
| iOS and visionOS (IOS-02 through IOS-06) | IOS-01's patch inventory and build, layout and distribution decisions first. Shared fixes land through IOS-02, with regression evidence on the existing platforms, before the Apple targets build on them. visionOS stereo shares the VR stereo front end (VR-01/VR-06). Device qualification needs Apple hardware this project does not have. |
| Portable saves (Q4-06) | Q4-05's restore validation and Q4-07's historical readers stay authoritative for existing saves. Cross-platform qualification on a target follows that target's platform evidence (PLAT/AND), and the stages run in their listed order. |

## Governance and baseline evidence

### GOV completion checkpoints

**Estimate: 100%; 3/3 tasks closed.** This closes the defined baseline exercise.
Later changes still need their own evidence; it does not certify all current
packages or turn historical suite counts into current results.

| Task / state | Completed points and evidence | Remaining subpoints / maintenance boundary |
| --- | --- | --- |
| GOV-01 — Closed | Source/default ownership reconciled; capability, platform and audio/network claims aligned at the baseline. | Reconcile new defaults and scope changes before promotion; retain unsupported cases explicitly. |
| GOV-02 — Closed | 83 historical entries mapped; 74 relevant contracts/fixtures recovered; native clock/platform probes registered in Meson and CI. | Keep discovery and CI execution intact as tests evolve; later full-suite runs retain their own build identity. |
| GOV-03 — Closed | Workload identity, binary/asset hashes, timing policy, failures and original-content controls recorded. | Bind every new performance/support claim to its actual workload; restore missing artifacts or rerun before reusing evidence. |

### GOV task evidence and acceptance

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

### HF completion checkpoints

**Estimate: 80%; 6/8 tasks closed.** Presentation implementation is mature within
its declared exclusions. The remaining work includes a failing performance target,
not merely paperwork or a final screenshot.

| Task / state | Completed points and evidence | Remaining subpoints / closure boundary |
| --- | --- | --- |
| HF-01 — Closed | Clock ownership specified; million-tick scheduling and deterministic state/event fixtures pass. | Keep simulation and presentation clocks separate; physical pacing belongs to HF-07. |
| HF-02 — Closed | Shared high-resolution fractional phase; frozen scene samples; SP/MP runtime and reset coverage. | Preserve integer API boundaries and XR host/display clock distinction. |
| HF-03 — Closed | Opt-in late look consumes input once; production/native and live SP evidence establishes the scoped implementation. | Default stays off; remote-play and physical checks are tracked by HF-08. |
| HF-04 — Closed | View lights, debris/trails and active ragdoll draw ownership implemented; other categories have explicit dispositions. | Keep authoritative events unchanged; human motion review remains HF-08. |
| HF-05 — Closed | Alternate/spectator cameras interpolate with cut/reset rules; vehicle and remote-body policies documented. | Playing clients' remote bodies remain on the tick; do not silently broaden this closure. |
| HF-06 — Closed | Cross-system probes cover exactly-once gameplay, TAA, shadows, save/load, restart, demos, AVI and simulated stereo. | Physical motion/headset review remains HF-08/VR-02. |
| HF-07 — Partial | Cap/policy matrix, precise wait, pacing histograms, present timing, reference runner and CPU attribution exist. | Meet the Quality every-frame floor and declared 240 Hz budgets; run foreground reference, VSync/VRR and physical display checks. 360 Hz remains experimental. |
| HF-08 — Partial | Loopback host/client, zoom, lift, loss/jitter, rapid-turn and cockpit probes pass; sensor/review tooling exists. | Measure click-to-photon latency; review cuts, weapons/TAA, debris and ragdolls in motion; retain vehicle late-look exclusion. |

### HF task evidence and acceptance

- [x] **HF-01 — P0, decision and qualification: specify clock ownership.** Preserve
  exact scheduling versus the current 16 ms game/snapshot clock unless a separate
  gameplay-timing project is accepted. Specify wall, usercmd, game, prediction,
  pose, material/effect, UI, audio and capture time; cover slow motion, competitive
  pause, ordinary SP pause and Arena freeze. Test long-run timestamp agreement,
  event ordering and unchanged gameplay across presentation caps. Completed:
  [clock ownership contract](presentation-clock.md), one-million-tick production
  scheduler/SP/MP/network agreement and deterministic event/state fixtures across
  presentation caps. Cross-system qualification subsequently closed under HF-06;
  reference pacing and physical review remain HF-07/HF-08.
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
  Added reference, 27 October, with the workload moved to Quality (the preset
  below Ultra, its 144 FPS cap lifted) on 28 October: native 2560 x 1440
  fullscreen `game/airdefense1` starting-position rotations with the Remastered
  HUD must keep **every frame <=6.67 ms**. The first Quality run on Vulkan
  averages 114.1 FPS (8.76 ms) with a 13.80 ms p99 and a 24.08 ms maximum;
  qualification remains open. The earlier maximum-quality runs are historical.
  See the [airdefense checkpoint](ui/runtime-performance.md#airdefense-maximum-quality-reference-27-october-2026)
  and the [Quality workload](ui/runtime-performance.md#quality-workload-and-deferred-shadow-scrolling-28-october-2026).
  On 3 November a [frame attribution](ui/runtime-performance.md#quality-frame-attribution-and-effect-culling-3-november-2026)
  found this workload CPU-bound. One thread runs the game tic, the scene front
  end and Vulkan command recording in turn, and a frame that takes a tic adds
  about 3 ms to it, so GPU work alone cannot meet the floor. The front end
  (36% of the main thread) and recording (26%) lead; the game tic takes 19%.
  Effects now take the frustum test before the portal-area walk, and the HUD's
  powerup clear runs once instead of every tic. Both are exact and cut about 4%
  of main-thread work. `tools/debug/cpu_sampler.py` samples main-thread stacks
  without an elevated shell. Meeting the floor still needs recording moved off
  the main thread, a cheaper tic and front end, and less GPU post-processing.
  A render-thread prototype (`r_vkRenderThread`, default 0) moves Vulkan
  recording, submit and present off the main thread; its design, hazards and
  stages are under REN-07. Its first stage overlaps the backend only with the
  next game tic. In paired rotations the median frame fell about 0.5 ms (9.21
  to 8.69 ms), p95 and p99 did not improve, and a GPU-bound run showed no
  gain. Overlapping the front end as well (stage 2) is what can lift the
  steady floor.
  Further implementation, 3 November: frame ownership now lasts through
  main-thread retirement, including when the worker finishes first. This fixes
  skipped cleanup, lost worker errors and out-of-order queued world changes.
  Map cleanup and restart drain the worker before erasing world resources.
  Debug primitives synchronize only when their storage changes, and surface/
  swapchain recovery is deferred to the main thread. Input-to-swap timing uses
  the completed frame's successful swap receipt rather than the asynchronous
  `EndFrame` return. The reference suite now fails on failed/missing reports
  and missing refresh coverage, and records `--render-thread 0|1|2`. Executable
  tests exercise fast/slow completion, errors, restart, delayed timing receipts
  and acquire/present recovery. Windowed Vulkan SP/MP host/client and OpenGL SP
  checks pass with five successful flash receipts per role and no gameplay in
  sampled draws; see [lifetime evidence](presentation-owners.md#threaded-lifetime-validation-3-november-2026).
  Progress, 4 November: special-effect commands now snapshot their enable mask
  and parameters, including forced effects, for both GL and Vulkan. Changing
  controller values during the next game tic no longer waits for the worker;
  resource creation and teardown still synchronize. Backend preflight latches
  the command's frame number before preparing resources. Vulkan shadow-cache
  stamps, GPU/query/readback ages, classic deformation validation and clustered
  generations use that submitted frame, independently of the front-end counter.
  The four Vulkan interaction/shadow failure paths now unwind recording and
  deliver recoverable/fatal receipts after main-thread retirement. Executable
  tests cover queued views across edits/reset, an advanced front-end counter,
  current-frame queries that must not retire, and fatality preservation. Test
  registration and the earlier swap-wrapper test fixtures are repaired too.
  See [submission evidence](presentation-owners.md#submitted-frame-ownership-4-november-2026).
  Full front-end overlap is now available experimentally with
  `r_vkRenderThread 2`; REN-07 records the ownership implementation and remaining
  qualification. The every-frame Quality floor and physical reference
  qualification remain open. See [overlap evidence](presentation-owners.md#full-front-end-overlap-4-november-2026).
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
  Quiet baseline, 5 November: in the first measurement window without peer
  load, the Quality rotation is GPU-bound on its laptop reference. Render-thread
  modes 0, 1 and 2 each average about 9.5–9.9 ms. GPU work averages 10.0 ms per
  frame, with a 14.9 ms maximum, and 2,014 of 2,016 frames exceed 6.67 ms on the
  GPU alone. SSAO is the largest lever (about 1.9 ms), then bloom (about 0.5 ms).
  With SSAO off, the median is 6.1–6.35 ms but p99 stays about 11 ms, and every
  configuration still has isolated 20–38 ms frames. Exact optimization cannot
  close the floor. It needs a decision on the Quality workload plus removal of
  those long frames; see the [quiet baseline](presentation-owners.md#quiet-quality-floor-baseline-5-november-2026).
  The user chose half-resolution SSAO for Quality, with Ultra kept at full
  resolution. On Vulkan, `r_ssaoResolution 0` (automatic) now halves it below
  32 samples. The SSAO draw is 55–57% cheaper (about 1.0–1.2 ms), and AO-only
  captures match full resolution to a mean of 0.001–0.004. The world-depth
  snapshot SSAO takes mid-prepass costs another measured 0.75 ms and is the
  next GPU lever. Long-frame attribution (`cpu_sampler.py frames` with
  `profileFrameWork` start times) found and fixed whole-cache clears of
  mode 2's retained index streams. It also traced streamed-sound starts to the
  main thread, since moved to job workers under AUD-03
  ([stream starts](presentation-owners.md#streamed-sound-starts-off-the-main-thread-5-november-2026)),
  and window-message bursts to desktop activity; see
  [half-resolution SSAO](presentation-owners.md#half-resolution-ssao-for-quality-5-november-2026)
  and [long-frame attribution](presentation-owners.md#long-frame-attribution-5-november-2026).
  The every-frame floor stays open: about 7.7 ms of GPU work remains per frame,
  and main-thread tic frames exceed 6.67 ms.
  **Floor decision, 5 November:** the user replaced the every-frame rule with a
  percentile budget. The Quality floor is now a **99th-percentile frame
  interval at or under 6.67 ms**; the maximum is reported but not judged.
  `airdefense_performance.py` gates on p99 and records the Vulkan worker
  mode (`--render-thread`). In quiet windows the half-resolution Quality GPU
  frame averages 6.9–7.4 ms, so the p99 floor still needs about 2.5 ms of
  mean frame time removed, as well as the heavy-view tail. Clipping the portal
  sky to its visible region is not exact in the current pass order: the
  compiled map has no sky faces, so where the backdrop shows is only known
  from the main view's depth, after the backdrop is drawn. See
  [where the floor stands](presentation-owners.md#half-resolution-ssao-for-quality-5-november-2026).
  A portal-sky mask now gets that depth early. At the backdrop's depth clear,
  Vulkan draws the main view's world depth single-sample and keeps the backdrop
  to the pixels it leaves far, dilated one pixel. It is exact against
  full-frame captures, and the same capture replaces SSAO's prepass snapshot.
  Normalized to bloom under heavy GPU contention, the GPU frame fell 16% (about
  1.1 ms); the CPU cost of the early world draw still needs a quiet run. See
  the [portal-sky mask](presentation-owners.md#portal-sky-mask-5-november-2026).
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
  Threaded-backend timing correction, 3 November: input clocks travel with the
  drawn frame and are recorded on the main thread after its backend completes,
  using the timestamp of its successful swap. Failed/withheld and hidden frames
  do not count as latency samples; resetting statistics excludes older frames
  that retire later. This corrects the engine-side instrument, not the still
  pending click-to-photon measurement or by-eye review.

## Vulkan rendering

Sources: [Vulkan gap history](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md),
[frame pacing](vulkan-frame-pacing.md), [device selection](vulkan-device-selection.md),
[authored programs](vulkan-material-programs.md), [HDR](vulkan-hdr.md), and
[point-shadow evidence](vulkan-point-shadow-parity.md).

### VK completion checkpoints

**Estimate: 70%; 1/7 tasks closed.** Windows NVIDIA/Intel qualification and bounded
laboratory results earn implementation credit. Failed strict image/pacing checks
and untested drivers remain open, with no tolerance or support-tier change.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| VK-01 — Partial | Windows x64 NVIDIA/Intel support; qualification runner records real presentation versus hidden/benchmark scope. | Physical AMD, Linux and MoltenVK SP/MP; device/surface loss, restart, long sessions beyond the Windows NVIDIA/Intel five-cycle soak, and exact-package budgets. |
| VK-02 — Partial | Six controlled foreground samples pass the declared 28 ms presentation p99 budget on NVIDIA/Intel; failed diagnostic repeats remain retained. | Physical direct/hybrid routing, other power conditions and the broader exact-package pacing matrix. |
| VK-03 — Closed | Complete driver inventories, bounded automatic adapter retry, explicit-device preservation and failure/restart controls pass. | Preserve cleanup invariants; broader physical adapters remain VK-01. |
| VK-04 — Partial | Authored GLSL/ARB, reload repair, source eviction and native resource limits pass; real uniform, pipeline and driver descriptor-refusal pressure recover on NVIDIA/Intel; OpenGL clip-space depth through `VK_EXT_depth_clip_control` removes the three stencil pixels. | The strict lighting image gate (one point-map and one projected-map shadow-filter pixel on NVIDIA, more on Intel) and broader authored-lighting/mod content. |
| VK-05 — Partial | Exposure-domain changes, late-failure cleanup and in-frame HDR cubemap fixes pass scoped GPU controls; classic stock stages stay display-referred by decision, with a byte-identical, qualified fallback. | Tiled/out-of-frame captures and authored-post combinations of admitted (PBR) views. |
| VK-06 — Partial | Resource counters and measured deferral decisions exist; image reload residency is fixed; five-cycle NVIDIA/Intel/GL soaks pass, with residual image growth attributed to bounded front-end pools. | Improved frame-time tails from a promoted optimization on a declared workload; preserve ordering and restart lifetimes before promotion. |
| VK-07 — Partial | Failed-write handling, MSAA resize/restart/cubemap comparisons, 50–200% depth/intensity views, nested/legacy-cropped remote views, cropped `envshot` faces and multisampled window views (GUI `renderDef`, remote captures), stencil shadow/light-count views and prints, and the weapon depth hack pass on NVIDIA. Tiled (larger-than-window) captures, direct and through the game-owned post target, pass on OpenGL and on Vulkan with NVIDIA and Intel. | Multisampled lit-geometry edges (4x) and Intel/other-vendor MSAA edge parity, other clipping combinations and bounded deferred resources across the broader matrix. |

### VK task evidence and acceptance

- [ ] **VK-01 — P1, qualification: extend the supported driver/platform matrix.**
  Preserve Windows x64 NVIDIA/Intel support and qualify AMD, physical Linux GPUs
  and MoltenVK on real Apple hardware separately. Cover stock SP/MP, restarts,
  device/surface loss, resize, captures, long sessions and final-package budgets.
  Software Vulkan CI remains useful regression evidence but cannot promote a
  physical platform or change `best` by itself.
  Progress, 3 November: the [qualification runner contract](vulkan-support-evidence.md)
  now records hidden/offscreen versus synchronous-benchmark scope explicitly and
  refuses configuration overrides of its window/input/filesystem invariants.
  Physical AMD, Linux and Apple hardware results are still required.
  Progress, 10 November: Windows x64 support was rechecked on a clean
  debugoptimized build of `8735ad1b`, the first since the 4 October sign-off,
  with OpenGL as the warning reference on the RTX 4060 (591.74) and Iris Xe
  (101.5590). All 29 SP maps passed (87 runs), as did the two-cycle, eight-map
  soak with a video restart: process growth at most 1.09x on NVIDIA, 1.15x on
  Intel and 1.11x on OpenGL. The MP sweep passed 59 of 60 runs. The NVIDIA
  q4dm6 run logged three bot-dependent decl and ragdoll warnings that OpenGL
  did not, and a full rerun of that map passed on all three. The validation
  layers logged nothing in any of the 102 Vulkan runs. This does not cover
  later Vulkan back-end commits on main, AMD, VK-02 pacing or the VK-04 image
  gates.
- [ ] **VK-02 — P1, investigation and qualification: visible presentation pacing.**
  Reproduce the hybrid-adapter/power-state conditions using current foreground
  windowed workloads and unchanged budgets. Separate host scheduling, fence waits,
  GPU execution and display presentation. Hidden Vulkan now renders offscreen;
  use actual presentation evidence for queue/latency claims and retain the older
  failure conditions. Close with repeated direct/hybrid-adapter results meeting
  the declared budget, or a reviewed support-scope limitation tied to retained
  reproduction evidence. Merely recording an exception does not qualify a failing
  configuration as supported.
  Progress, 3 November: the [visible-window runner](vulkan-frame-pacing.md#visible-window-diagnostics-3-november-2026)
  retains presentation/CPU/GPU phases, focus, competing clients and power state.
  Six diagnostic repeats remain failed: foreground ownership was not observed,
  one NVIDIA repeat exceeded the budget, and Intel lacks the timing extension.
  A GPU-dependent first-run preset was also found and is now pinned. These
  results do not replace the required foreground/direct/hybrid/power controls.
  Progress, 4 November: readiness now requires an idle environment and sustained
  focus on the identified game window; foreground events also reject brief
  interruptions between polls. The native and observer controls pass. A
  [coordinated quiet window](vulkan-frame-pacing.md#coordinated-foreground-repeats-4-november-2026)
  after a completed MP batch now supplies three foreground measurements per
  adapter: NVIDIA ETW p99 11.28–11.85 ms and Intel 20.80–20.86 ms, all within
  the unchanged 28 ms budget with zero trace loss and no observed contention.
  The first NVIDIA probe is qualified by a separate immutable-input reanalysis
  after fixing UTF-16 status-log decoding; its failed original is retained.
  Intel's GDI stream has no available swapchain address. Physical display
  routing, alternate power states and the broader matrix remain open.
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
  eviction limits, the authored-lighting laboratory and mod content remained open
  at that checkpoint. Progress, 3 November: [compiler-storage recovery](vulkan-material-programs.md#compiler-storage-recovery-3-november-2026-vk-04)
  fixes rejection of large-source edits before the version table fills. Validated
  GPU runs now cover 271 small-source versions and 181 padded-source versions,
  safe eviction at both limits and exact reload/restart recovery. Descriptor,
  uniform and pipeline exhaustion, broader authored lighting and mod content
  remain open. The integrated-build lighting repeat passes all 26 independent
  controls per backend, but its strict full-frame comparison still fails five
  shadow-boundary pixels across three cases; the image gate remains unchanged.
  Progress, 4 November: [native draw-resource controls](vulkan-material-programs.md#draw-resource-failure-and-capacity-controls-4-november-2026)
  exercise the four-pool descriptor quota, four-MiB uniform ring, 512-entry
  pipeline cache and frame-slot recovery on Windows and Linux. A reproduced
  descriptor-allocation failure no longer consumes unused uniform reservations
  and starves later draws. Real uniform-pressure controls now pass on NVIDIA
  and Intel, including load reduction, reload and restart, with all sixteen
  GL/Vulkan image pairs within one display level. Pipeline pressure reproduced
  new programs remaining absent after reload; the existing eviction policy now
  reclaims safely retired stale versions when their pipelines fill the cache.
  Both adapters pass the corrected pipeline-pressure sequence with exact
  recovery and all sixteen GL/Vulkan pairs within one display level.
  Descriptor-driver failure on GPU, broader lighting and mod-content
  qualification remain open.
  Progress, 6 November: the [real-driver descriptor drill](vulkan-material-programs.md#descriptor-refusal-on-real-drivers-6-november-2026)
  (`r_vkMaterialDescriptorPoolTest`) passes on NVIDIA and Intel. Both drivers
  refuse over-quota authored draws, skipped redundant stages leave images
  unchanged, and skips stop once both frame slots return to production pools.
  Reload and restart with and without the drill recover exactly; all sixteen
  GL/Vulkan pairs are within one display level. The descriptor-driver subpoint
  is closed. Broader lighting/mod content and the strict lighting gate remain.
  Progress, 6 November (depth): [clip-space depth control](vulkan-material-programs.md#clip-space-depth-control-6-november-2026-vk-04)
  makes Vulkan clip and round depth as OpenGL does where `VK_EXT_depth_clip_control`
  exists (NVIDIA and Intel here). The three point-stencil pixels of the strict
  lighting comparison are gone on NVIDIA. One point-map and one projected-map
  shadow-filter pixel remain, unchanged by the convention, so the gate stays
  failed. The change exposed and fixed a second depth remap in displaced
  authored vertex shaders; all three authored laboratories pass again.
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
  Progress, 3 November: [in-frame HDR cubemap capture](vulkan-hdr.md#hdr-cubemap-capture-3-november-2026-vk-05vk-07)
  now uses the existing float scene target without stretching the capture's
  viewport. This fixes a reproduced 81-level cube-face difference; all 42
  paired post/cubemap controls pass within one level, with exact main-view
  recovery. Complete-view material admission remains required. Tiled/out-of-frame
  captures and arbitrary portal/nested/material combinations remain open.
  Decision, 6 November: classic stock stages (non-replace/add blends, dynamic,
  texgen and cinematic stages, late sorts and the subviews that draw them) [stay
  display-referred](vulkan-hdr.md#stock-content-stays-display-referred-6-november-2026-vk-05),
  matching OpenGL and retail, instead of being re-blended in linear light.
  Their admission scope is closed as declined by design. On Vulkan, requesting
  the linear scene on stock `airdefense1` and `process1` leaves every capture
  byte-identical, declines every main view and admits nothing. Tiled and
  out-of-frame captures and authored post effects of admitted PBR views remain.
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
  Progress, 3 November: [opt-in executor counters](vulkan-resource-profiling.md)
  expose descriptor reuse/churn, mapped-ring uploads, target changes, rendering
  scopes, barriers and submissions alongside the existing memory/pipeline report.
  They introduce no GPU wait/query and do not establish production overlap.
  Short diagnostic intervals kept heap/pipeline counts stable with negligible
  descriptor churn. The linked profile records measured reasons to defer new
  cache/warm-up, indirect/Hi-Z, parallel-recording and transfer mechanisms;
  it does not promote an optimization or close the broader timing/soak gates.
  Progress, 4 November: a longer stock-map tour reproduced table exhaustion at
  a video restart because old maps' purged images were reloaded. The
  [residency correction](vulkan-resource-profiling.md#restart-residency-failure-4-november-2026)
  keeps those handles lazy, preserves checked recovery and reports live image
  slots. Native controls pass on Windows and Linux. Corrected two-cycle tours
  complete all 16 map stops on both NVIDIA and Intel, peak at 2,372 of 4,096
  image slots, and report no Vulkan/validation warnings. Same-map allocation
  and process-memory growth remains recorded; these bounded tours do not
  establish long-session plateaus or promote an optimization.
  Progress, 6 November: the [five-cycle soak](vulkan-resource-profiling.md#five-cycle-soak-6-november-2026)
  ran 40 stops on the clean `1243eb6a` package on NVIDIA, Intel and GL, all
  passing, with no validation messages. After the first cycle, Vulkan process
  growth (at most 94 MiB on NVIDIA and 60 MiB on Intel over three cycles) is no
  larger than GL's 97 MiB. Heap allocations stay 12 or 13 above resident images
  throughout. The remaining one slot per cycle was traced, by listing every
  image, to the retained interface's layer pool (capped at 48 slots and
  256 MiB, with reuse and eviction) and a second loaded copy of one core1
  objective image. Both are bounded front-end pools, not Vulkan lifetime
  defects. This closes the long-session plateau evidence. What remains is
  promoting an optimization with improved frame-time tails, which the
  measured deferral decisions above do not yet justify.
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
  after a failed submission. Progress, 3 November: the [paired capture sequence](vulkan-captures.md#cubemap-and-write-failure-qualification-3-november-2026)
  passes 121 RGBA/cubemap comparisons at each of 4x and 8x MSAA within one display level,
  with exact per-backend resize/restart recovery. Failed filesystem writes now
  fail screenshots, stop incomplete cubemaps and prevent incomplete probe reloads.
  Arbitrary nested targets and the remaining debug/readback combinations stay
  open; this fixture does not qualify all of VK-07.
  Progress, 4 November: [debug-view comparisons](vulkan-captures.md#debug-view-qualification-fixture-4-november-2026)
  pass 25 pairs at each of 0x/4x/8x MSAA for 50–100% game-owned scene targets.
  Larger-target probes exposed GL debug readbacks using window dimensions;
  active-viewport/attachment sizing now corrects that path. Native extent,
  MSAA and state-restoration controls pass on Windows and Linux. The corrected
  34-case sequence passes at each of 0x/4x/8x MSAA from 50% through 200% scale,
  within the unchanged two-level full-frame limit and with exact reload/restart
  recovery. Actual cropped, offset and nested GPU comparisons remain open.
  Progress, 6 November: [tiled captures](vulkan-captures.md#tiled-captures-6-november-2026)
  larger than the window repeated one tile on Vulkan, which ignored the tile
  viewport offset. Each view of a tiled frame now moves by the offset, and
  view scissors are cut at the attachment edge. On NVIDIA, Vulkan places every
  tile correctly (box-filtered correlation 0.999). 26 of 27 GL/Vulkan tile
  comparisons are within one display level; the remaining one is an OpenGL
  tile that repeats another. Tiling through the game-owned post-process target
  is wrong on both backends, identically.
  Progress, 6 November (tiled, closed): both backends offset views drawn
  *into* a render texture as well as views drawn to the window. Now only window
  draws take the tile offset; a view drawn into a target sized to the capture
  stays at the target's origin, and its composite moves instead. OpenGL and
  Vulkan (NVIDIA and Intel) now pass the gated `direct`, `post` and `restored`
  cases (box-filtered correlation 0.999 direct, 0.9998 post), and all 27
  GL/Vulkan tile comparisons on each device are within one display level.
  Progress, 6 November (views): the [nested/cropped/offset fixture](vulkan-captures.md#nested-cropped-and-offset-views-6-november-2026)
  found that remote-camera captures into ordinary file textures (the stock
  `textures/common/camera1` idiom) failed on both backends. Those textures
  are block-compressed with mips, so Vulkan blitted into BC1 and OpenGL bound
  an incomplete framebuffer. Both now re-create such a destination as one RGBA8
  level of the capture size, as Quake 4's `glCopyTexImage2D` did. Nested,
  legacy-cropped nested and restarted nested views then pass on each backend
  and match within one display level. An `envshot` under the legacy crop
  drew each face at half size in a corner on both backends, because
  `BeginFrame` sizes `glConfig` to the capture. The window size is now kept,
  and non-window-sized captures skip that crop. Still open: Vulkan does not
  multisample views drawn straight to the window, as OpenGL's 4x MSAA window
  does: GUI `renderDef` edges (112 pixels, at most 11 levels) and a remote
  capture inside an `envshot` face (113 edge pixels).
  Progress, 6 November (MSAA): [window-view multisampling](vulkan-captures.md#window-view-multisampling-6-november-2026)
  renders those views into a seeded, window-sized multisampled target in the
  swapchain's row order and copies their rectangle back. On NVIDIA the whole
  view sequence now passes, both differences included, and they return with
  `r_vkWindowViewMultisample 0`. Capture views stay single-sample, as on
  OpenGL. Intel's multisampled edges still differ from NVIDIA's OpenGL
  reference.
  Progress, 6 November (stencil): the [stencil debug fixture](vulkan-captures.md#stencil-debug-views-and-the-weapon-depth-hack-6-november-2026)
  found that the debug tools started with an undefined scissor test, so
  `r_showShadowCount` counted different areas on each backend (overdraw 1.8 on
  OpenGL, 3.9 on Vulkan). The tools now enable it themselves. Shadow- and
  light-count images and their printed overdraw then match exactly at 0x and
  4x, and the weapon depth hack's depth view is within one level. At 4x, lit
  geometry edges still differ in up to 20 pixels, by up to 8 levels.

## Modern rendering engine

Sources: [modernization history](archive/2026-10-18/idtech5-modernization-roadmap.md),
[visible-light ownership history](archive/2026-10-18/plans/2026-08-16-modern-visible-lighting-ownership.md),
[capability matrix](engine-capability-matrix.md), [GPU skinning](gpu-skinning-modernization.md),
[PBR production evidence](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md) and
[shadow production audit](shadowmapping-final-audit-2026-09-05.md).

### REN completion checkpoints

**Estimate: 55%; 1/8 tasks closed.** PBR, temporal features and shared rendering
domains already exist. Aggregate modern lighting still has
`MODERN_LIGHTING_PARITY_PROVEN_DOMAINS=0`; neither feature presence nor domain
diagnostics establish whole-frame ownership or production performance.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| REN-01 — Partial | Shared classic GUI/world/interaction/fog/deform/subview/post domains have scoped implementations and contracts. | Qualify each on fresh GL/Vulkan packages: executed ownership, matching appearance, zero-commit rollback, human review and platform breadth. |
| REN-02 — Partial | Shared material/light contracts and corridors provide foundations; the remaining decline reasons are inventoried. | Inventory remaining semantics; finish coherent lit-frame composition, visible ownership, full fallback and measured benefit before parity promotion. |
| REN-03 — Partial | Point/projected/CSM and hybrid shadow paths, caches and Vulkan translucent moments exist. A continuous-motion stock tour passes at default, cascaded and constrained budgets on GL/Vulkan with bounded slots and retained light; receivers after motion match a fresh render, as do moving doors, cutout and translucent casters under camera motion; static/live composition and cutout-alpha audits done (GL now falls back to stencil like Vulkan). The 9 November [stencil-parity audit](shadowmapping-audit-2026-11-09.md) fixed prelight-only (`noDynamicShadows`) lights losing their shadows, `noSelfShadow` world geometry no longer shadowing `noSelfShadow` receivers under prelit lights, the one-sided five-tap kernel and the packed-depth wrap at the far envelope. | Physical platforms; the audit's open parity items (casters outside an off-centre light's box, cascade range end, images for the two fixed rules). |
| REN-04 — Partial | Exact vectors, TAAU and dynamic resolution; cumulative resolve counters and a GL/Vulkan runtime gate (cuts, capture, pause, restart, failed presents, mirrors, stale timing, scale recovery, SMAA rollback). Mirror/remote-view, withheld-present and capture-depth gaps fixed. | Human review of visual stability; physical Linux/macOS/Android. |
| REN-05 — Partial | Authored PBR, animated/MD5R models, box probes and indirect-only SSAO have scoped evidence. SSAO parity controls repaired after half-resolution SSAO, with a matched half-resolution control; the Intel residual is reproduced and explicitly bounded; skin substitutions and a mixed PBR/classic model on packed MD5R match CPU skinning. | lavapipe residual, mods authored outside the laboratory, physical Linux/Apple fallback. |
| REN-06 — Partial | Probe admission decided (RGBA8/sRGB/generated RGBA16F; compressed, oversized and authored HDR excluded); cold misses filter on a background job and publish within the 4 ms budget on both backends; froxel/SSR/SSGI pass individually and together with byte-exact master-off rollback on GL/Vulkan. | Re-bake of the cropped bundled grids (owner decision); broader authored scenes. |
| REN-07 — Partial | Device-aware presets, measured culling/HUD improvements and opt-in Vulkan render-thread stage 1 exist. | Stage-2 ownership work; matched benefit without tail regression; quality parity, 100 resize/scale cycles and sustained resource bounds. |
| REN-08 — Closed | Motion blur coexists with TAA, owns exact skinned/weapon vectors, is frame-rate independent and reports requested/effective state; 41 GL/Vulkan combination cases pass. | Physical Linux/macOS/Android and appearance review belong to platform qualification. |

### REN task evidence and acceptance

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
  Progress, 6 November: the inventory is written
  ([modern lit-frame inventory](modern-lit-frame-inventory.md)). No lighting
  domain is proven by default, so every lit stock frame declines to ARB2,
  usually as `lighting-parity-unproven`. The only per-material proof covers
  PBR and two narrow classic classes, and every modern-path corridor runs only
  in a laboratory mode (`r_pbrLinearScene`). The aggregate executor and the
  REN-01 shared domains are still mutually exclusive.
  Step 1 has begun: classic cutouts now join the proven classes and match
  native ARB2 in the laboratory: flat-bump diffuse cutouts on the forward
  path (`production-fixed-cutout`, mean 0.28 levels) and bumped, specular
  cutouts through the classic-lighting corridor (`production-fixed-cutout-bump`,
  mean 0.25), each with 5 bounded hole-edge pixels.
  Texture-matrix and vertex-coloured materials follow as corridor-only
  fallbacks (`production-fixed-texmatrix` and `production-fixed-vertex`,
  both mean 0.25 levels; an interaction takes its vertex colour from the last
  diffuse or specular stage, which the fixture now honours).
  Stage expressions (conditions, colours, matrices) and multi-set materials
  follow (`production-fixed-condition`, `-multistage`, mean 0.25 levels).
  Step 1 now covers the dominant classic interaction shapes inside the
  corridor; what remains of it is cutouts and light grids combined with
  those fallbacks.
  Step 3 is done: the classic-lighting and baked-grid corridors also run in
  display-referred modern frames (`r_pbrLinearScene 0`), and the fog/blend
  corridor already did. Every class above, the light grid and a fog light
  match native ARB2 there (`production-fixed*-display`, byte-identical except
  the grid's 1-level maximum).
  Step 2 is done for corridor-owned classic surfaces: ambient lights draw
  with ARB2's ambient normal cube there and match native ARB2
  (`production-fixed-ambient`, byte-identical in display frames).
  Remaining, in order: finish step 1's cutout/light-grid combinations,
  represent stencil-fallback and multiple point shadows, reconcile the two
  ownership models, then measure benefit before any promotion.
- [ ] **REN-03 — P1, qualification and implementation: shadow production scope.**
  Qualify point/projected/CSM, stencil/map hybrids, moments, movers, near emitters,
  dynamic/cutout/custom receivers and constrained budgets through full scenes and
  continuous motion. Audit point static/live composition and mapped dynamic-image/
  non-explicit-texcoord alpha cases; retain stencil fallback when coverage cannot
  be reproduced. Translucent moments already exist on Vulkan. Prove cache freshness,
  bounded memory, no missing/doubled receivers and retained light on fallback.
  Progress, 6 November: `renderer_shadow_motion_qualification.py` tours eight
  stock outdoor poses while panning continuously, at the default budget, with
  cascades and with one update per view. OpenGL renders fresh maps throughout
  with no receiver, budget or stencil fallback and no unshadowed light; the
  constrained budget takes 994 budget fallbacks with every light still
  shadowed. Vulkan reports no fallback or dropped query and stays inside its
  slot capacity. See the [shadow tour](renderer-validation-matrix.md#continuous-motion-shadow-tour-6-november-2026-ren-03).
  The dynamic-image/non-explicit-texcoord alpha audit found OpenGL drawing
  unreproducible cutouts as solid depth; it now takes Vulkan's sticky stencil
  fallback (no stock material reaches the case).
  The point static/live composition audit found no defect: point lights never
  compose (dynamic casters defeat the cube cache on both backends), projected
  lights compose static depth with live casters. (Point lights compose on both
  backends since 9 November 2026, under FPS-03.)
  Image-level receiver check after motion (`--cases receivers`): at every
  tour pose the frame from the motion-warmed cache matches a fresh render on
  both backends (worst 0.002% of bytes), with a shadows-off control proving the
  poses show shadows. See [receiver images](renderer-validation-matrix.md#continuous-motion-shadow-tour-6-november-2026-ren-03).
  Moving doors (`door-sweep`) and perforated cutout casters
  (`caster-cycle-cutout-sweep`) now run with the camera and its flashlight
  panning through every change, and cached shadows match fresh renders on
  both backends; the caster cycles gained image checks. These fixtures had
  stopped exercising the flashlight once `g_dynamicLightShadows` followed
  machine spec; the runner now pins it. See
  [movers and cutouts](renderer-validation-matrix.md#continuous-motion-shadow-tour-6-november-2026-ren-03).
  Translucent casters under motion now have a fixture:
  `caster-cycle-translucent(-sweep)` spawns stock forcefield-glass crates in
  the flashlight with moments on; OpenGL admits exactly those casters, Vulkan
  draws its moment pass, and cached images match fresh ones while the camera
  turns. The gameplay benchmark's `shadow-translucent-medlabs` candidate
  remains unusable (it falls back for shadow geometry before translucent
  moments, stationary too). See [movers, cutouts and translucents](renderer-validation-matrix.md#continuous-motion-shadow-tour-6-november-2026-ren-03).
  Remaining: physical-platform breadth.
- [ ] **REN-04 — P1, qualification: temporal AA and dynamic resolution.** Exercise
  existing rigid/skinned/weapon exact vectors and reactive surfaces through
  interpolation, disocclusion, pause, cuts, failed presents, capture, multiple
  views and retained UI. Test delayed/stale GPU timing and resolution recovery.
  Require stable quality, separate native UI, valid previous-frame identity and
  immediate current-frame/SMAA fallback. Per-eye VR temporal AA is VR-05's (closed).
  Progress, 6 November: both backends now count how every resolve ended and why
  history was rejected (`rendererTemporalPresentationStatus`, `reset`), and
  `renderer_temporal_qualification.py` exercises native, TAAU and dynamic
  resolution, scale recovery, stale timing, pause, capture, cuts, `vid_restart`,
  SMAA rollback, MSAA, `r_vkRenderThread 2`, withheld images, surface and
  device loss and the mirror fixture, requiring history before and after each
  event with complete ownership. It reproduced and fixed three Vulkan gaps:
  subviews inherited the parent's temporal preparation (mirrors, remote and
  refraction views were jittered and retired the main history every frame),
  withheld presents retried a 1 s acquire per command (~27 s per frame), and
  capture frames presented screen-space effects without depth. See
  [temporal qualification](temporal-presentation.md#validation). Remaining:
  human review of ghosting/shimmer/disocclusion on the reference display, and
  physical Linux/macOS/Android. Native UI separation stays covered natively;
  capture frames cannot show it.
- [ ] **REN-05 — P1, qualification: remaining PBR quality and portability.**
  Reproduce strict residual Intel SSAO and lavapipe scalar/texture differences and
  recorded cutout/normal diagnostic mismatches; fix or explicitly bound exceptions.
  Qualify authored mixed-material mods, skin substitutions on packed MD5R, physical
  Linux and Apple fallback. Preserve stock appearance. Box probes, probe capture,
  animated/MD5R PBR and indirect-only SSAO are implemented and need regression
  evidence rather than another implementation phase.
  Progress, 6 November: the SSAO laboratory controls had failed on every GPU
  since automatic half-resolution SSAO (they compared the full-resolution field
  pass with a half-resolution classic pass); they now pin full resolution and
  pass exactly on NVIDIA. The Intel residual (4 classic, 6 debug pixels) is
  reproduced, unaffected by precise arithmetic, and bounded to Intel Vulkan
  only. See [SSAO parity](renderer-validation-matrix.md#ssao-parity-controls-6-november-2026-ren-05).
  A matched half-resolution control now compares forced half-resolution SSAO
  on classic-only frames with full resolution: OpenGL is exact (one
  resolution), and Vulkan stays within 2.1 (NVIDIA) and 2.9 (Intel) levels
  on average with no bias. See [half resolution](renderer-validation-matrix.md#ssao-parity-controls-6-november-2026-ren-05).
  Skin substitutions on packed MD5R are qualified: laboratory skins swap the
  skinned specimen's PBR metal for a PBR dielectric or a classic material,
  and MD5R draws both exactly like CPU skinning on both backends, with PBR
  ownership kept or released as authored. See [skin substitutions](renderer-validation-matrix.md#skin-substitutions-on-packed-md5r-6-november-2026-ren-05).
  A mixed-material model (one skinned mesh, PBR above and classic below) keeps
  each part's owner on both backends, on CPU skinning and packed MD5R; OpenGL
  declines such frames to its light loop, whose native owner still draws the
  PBR part. See [skin substitutions](renderer-validation-matrix.md#skin-substitutions-on-packed-md5r-6-november-2026-ren-05).
  Remaining: lavapipe's one-byte difference (a WSL lavapipe run, blocked by
  free space on C:, where the WSL disk lives), mods authored outside the
  laboratory, and physical Linux/Apple.
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
  Progress, 6 November: admission is decided - authored RGBA8/sRGB and
  generated RGBA16F supported; compressed, oversized and authored HDR excluded
  with cleared output - and `gfxInfo` reports source-read and prefilter time per
  miss. The 56-control probe suite passes; a miss costs 3-6 ms to read (21 ms
  after reload) but 0.49-0.58 s to prefilter on the CPU, against the new 4 ms
  per-frame budget (memory: the fixed 44.7 MB atlas plus one transient source).
  Parallel tiles (bit-identical, both backends) cut the prefilter to 84-93 ms.
  See [probe sources](vulkan-probe-sources.md#cold-miss-cost-and-budget-6-november-2026-ren-06).
  `renderer_screen_space_effects_qualification.py` captures each leaf, all three
  and master-off at a frozen pose: every leaf changes the frame, master-off is
  byte-identical to the baseline on both backends, and the combination costs at
  most 1.4 ms. It found Vulkan screenshots presenting the leaves without depth,
  now fixed. See [leaf captures](renderer-validation-matrix.md#frozen-same-process-leaf-and-rollback-captures-6-november-2026-ren-06).
  The prefilter is now off the frame (`r_pbrProbeAsyncPrefilter`, default on):
  both backends filter on a background job, Vulkan also defers its copy and
  decode, and finished RGBA16F tiles upload within 2 ms a frame while the set
  keeps its analytic fallback. Probe work in any gameplay frame now peaks at
  2.1 ms (Vulkan) and 3.3 ms (OpenGL), inside the 4 ms budget; probes appear
  0.15-0.37 s after they are needed, and screenshots wait for them. See
  [background prefilter](vulkan-probe-sources.md#background-prefilter-6-november-2026-ren-06).
  Probe capture orientation is qualified: the `probes-baked` control bakes two
  probes in the game and requires GL's cube convention in the written -Y face
  and in the reflection, and passed on both backends on 6 November. Grid
  provenance is established: every bundled light grid is a cropped June
  bake (37 multiplayer grids match logged 2560x1334 bakes, the June 18 batch
  1280x720), reproduced by the unfixed build; the bake is now correct, so the
  grids do need regenerating. Keeping them was the owner's decision on
  5 October; re-baking (about 24 minutes a large map, replacing pak1 data)
  waits on that decision being reversed. See
  [release notes](release-completion.md).
  Remaining: re-baking the bundled grids (owner decision), broader authored
  scenes.
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
  The 3 November [frame attribution](ui/runtime-performance.md#quality-frame-attribution-and-effect-culling-3-november-2026)
  finds the Quality rotation CPU-bound on its main thread (see HF-07). Exact
  effect-culling and HUD changes remove about 4% of that work. Per-frame ring
  copies of static geometry (3.6%), fallback shadow volumes for prelit lights
  (2.6%) and exact presentation-pose bounds (2.4%) remain measured candidates.
  Audit hot copy-helper GL state queries, cluster allocation/binning, per-frame
  pose work, shadow invalidation, render-target generations and upload lifetimes.
  Reuse existing job/measurement infrastructure. Shadow uniform caching already
  exists. Require at least five matched performance runs, unchanged quality/order,
  no p99 regression beyond the predeclared measurement tolerance in GOV-03 and
  bounded resources through at least 100 resize/scale cycles.
  - **Render thread: ownership and stages (3 November).** HF-07's attribution
    found one thread running the game tic (19% of it), the front end (36%) and
    Vulkan recording, submit and present (26%) in turn. The boundary for a
    render thread is `RB_ExecuteBackEndCommands`. Everything it reads must
    either belong to the frame it was handed or stay frozen until that frame
    completes.
    - **Ownership.** The main thread owns the game, the render world (entity,
      light and effect defs, decals, overlays), the front end, and the image
      and decl managers. `EndFrame` hands frame N to the render thread. Until N
      completes, the render thread owns:
      - frame N's command chain, frame memory and vertex-cache temps;
      - the Vulkan queue, command pool, upload batch, swapchain and deferred
        destroys;
      - backend-only state: executor rings and memos, the shadow atlas and its
        caches, the classic domains and the metrics accumulators.

      `R_RenderThread_Sync` runs on the main thread. It waits for the frame and
      then completes it: render-texture deletes, metrics, `R_ToggleSmpFrame`
      (frame memory and the deferred tri, light, entity and effect-model frees),
      `vertexCache.EndFrame`, and finally the queued render-world changes. The
      render thread never waits on the main thread. Its prints and warnings
      queue for the main thread, and `common->Error` unwinds only the render
      thread; the sync re-raises it on the main thread.
    - **Stage 1, prototyped: `r_vkRenderThread 1` (default 0, Vulkan only).**
      The next `BeginFrame` syncs, so only events, console commands, sound and
      the game tic overlap the backend. Frame memory, the command chain, scene
      packets, temporal state and vertex-cache temps keep one owner at a time,
      so nothing needs double buffering. While a frame is in flight:
      - Updates and frees of existing entity and light defs, and decals and
        overlays on them, queue and apply in call order at the sync.
        `GetRenderEntity` and `GetRenderLight` return the queued copy.
        `ModelTrace`, `GuiTrace` and `Trace` sync first when they would see a
        queued change.
      - Effect snapshots defer their delete to the sync.
      - Every other main-thread entry that touches backend-owned state syncs
        first: frame memory, the vertex cache, dynamic-model rebuilds, image
        creation, upload and purge, the upload batch and deferred destroys,
        render textures, readbacks, screenshots and save previews,
        special-effect changes, material cinematics, `vid_restart` and screen
        parameters, and world load and free.
      - Window-service polling and presentation recovery moved out of
        `GLimp_SwapBuffers` to the main thread, at hand-off and completion:
        they call SDL, set cvars and queue `vid_restart`.
      - Captures, tiled screenshots, loading screens, the Alpha Labs overlay
        (it walks the live lightDefs) and checked image reloads still run the
        backend inline.

      `renderThreadStats` counts threaded and inline frames, the main thread's
      waits, and every early sync by reason.
    - **Stage 1 lifetime repairs, 3 November.** A completed worker keeps the
      frame pending until the main thread collects it; clearing ownership on
      the worker could skip retirement and its errors entirely. Queued world
      operations now keep call order even when the backend completes between
      two updates. The previous-frame timing read precedes handoff, avoiding a
      race with the worker's counters. Debug-line, polygon and text mutations
      synchronize; empty-list expiry does not serialize ordinary game tics.
      Swapchain and lost-surface recovery queue from the worker and run on the
      main thread after retirement, including window-extent queries and surface
      creation. Native tests execute the actual handoff and recovery functions
      against controlled workers/drivers. `renderThreadStats` drains the frame
      before reporting/resetting, reports `completed`, and counts waits only
      when the backend was still busy. The prototype remains opt-in; physical
      Wayland/macOS testing and stage-2 performance remain unqualified.
    - **Remaining stage 1 limits.** Lazy image loads on first bind run on the
      render thread and invalidate the consumed-policy observation epoch, which costs cache
      receipts, not correctness.
    - **Stage 1 evidence (3 November, native 2560 x 1440 fullscreen Quality
      `game/airdefense1` rotations on a shared machine).**
      - **Stability.** Six launches ran more than 20,000 threaded frames,
        including a console `screenshot` (run inline) and a `vid_restart` with
        the thread live. The logs show no warnings or errors, and no early sync:
        during play only the `BeginFrame` wait fired.
      - **Quietest launch** (five in-launch pairs, renderer metrics on). The
        threaded median frame was 8.69 ms against 9.21 ms inline, better in four
        of five pairs. The mean was 9.00 ms against 9.27 ms. p95 rose from 13.5
        to 14.9 ms and p99 from 18.9 to 19.5 ms.
      - **Loaded launch** (five pairs, after discarding a load spike). The
        threaded mean was 12.85 ms against 13.23 ms, again better in four of
        five pairs, but p95 rose from 19.7 to 21.3 ms. On non-tic frames the
        threaded path was slower: 11.7 ms against 10.1 ms.
      - **GPU-bound under peer load.** Peer clients on the same GPU kept GPU
        time near 12.1 ms per frame in both modes, and the inline backend
        already waited 2.0 ms per frame on the frame fence (17 µs on 3
        November). The threaded backend's fence wait grew to 2.8 ms.
      - **Stack samples** (`tools/debug/cpu_sampler.py`). Inline, the main
        thread used 0.80 CPU-seconds per second, and the backend accounted for
        46% of its samples, 21% of them driver waits. Threaded, the main thread
        used 0.52 CPU-s/s and waited in `R_RenderThread_Sync` for 35% of its
        samples. The render thread used 0.25 CPU-s/s, spent 38% of its samples
        in driver waits and 27% idle.

      Verdict: stage 1 is a working opt-in prototype, but it cannot meet the
      floor. Only the tic overlaps the backend, the next front end still waits
      for it every frame, and the tic's render-world updates now apply serially
      at the sync. The tails did not improve; wake-up latency and the moved
      updates are candidates, not yet shown. Repeat the pairs on a quiet machine
      or the reference desktop before relying on the median gain. Stage 2 is the
      lever for the floor. `r_vkRenderThread` stays 0.
    - **Stage 2 prototype implemented, 4 November: `r_vkRenderThread 2`.**
      The next front end builds while Vulkan records/presents the previous
      frame. A single consumer remains in flight; the normal join moves to the
      next `EndFrame`, after GUI/VR command assembly. Default remains **0**.
      This implements overlap, not the HF-07 every-frame Quality budget.
      The 3 November ownership audit is addressed as follows:
      1. **Frame memory and commands.** Two arenas retain separate command
         chains. A thread-local submitted-frame selector keeps defensive backend
         allocations out of the producer arena. Large/aligned allocations are
         checked; deferred ambient/interaction triangles retire in a combined
         reference-count prepass, including shutdown.
      2. **Vertex cache.** Two temp-header and deferred-free lists follow the
         frame slots. Geometry caches and complete CPU skinning fallback are
         prepared before submission. Defensive backend allocations use only
         the submitted arena, without touching producer cache lists.
      3. **Dynamic models.** Submitted geometry, cache headers and CPU bytes,
         palettes, MD5R transforms and previous positions are copied. The
         initial implementation also copies static streams; reducing that cost
         needs measured, explicitly retained immutable storage.
      4. **Render world.** Submitted entity/light definitions are snapshots.
         Main-thread updates apply immediately in mode 2; mode 1 keeps ordered
         queued updates. Sticky shadow failures return through checked light
         receipts at retirement. World/model teardown still joins first.
      5. **Frame identity.** Special-effect commands, primary view, screenshot
         state and Vulkan display configuration belong to the submitted frame.
         Backend history, query ages, shadow caches and deformation hashes use
         that frame/view. A mode transition with an unsnapshotted queued view
         runs inline.
      6. **Scene packets.** Two packet frames and per-thread lookup/history
         tables prevent next-frame clearing from changing submitted packets.
         Front-end history seals at handoff, once per frame.
      7. **Temporal presentation.** Frame state and reset receipts are per-slot.
         Backend invalidations return to main-thread history at retirement.
      8. **Caches and counters.** Native material-family/diagnostic caches and
         skinning statistics are synchronized; metrics retain separate frame
         accumulators and actual worker-finish timestamps. Shadow readiness is
         published atomically. Renderer CVar changes join before mutation via
         renderer API 31; unchanged assignments do not wait.
      9. **Resources.** Submitted materials' static images/programs and usable
         light-grid areas prepare at handoff. Name-only image lookups lock the
         registry; sampler mutation, material reload/teardown and device changes
         retain barriers.
         Backend scratch targets and cinematic work retain exclusive access
         through those barriers; their first-use cost is still a profiling
         target. Captures, loading, checked image policy, locked surfaces and
         the Alpha Labs live-light path retain synchronous ownership.
      10. **Errors.** Recoverable/fatal worker failures retire exactly once on
          the main thread before the original severity is raised.

      `rendererFrameOwnershipSelfTest` mutates source streams/definitions and
      checks submitted copies, aliases and poisoned non-finalized records.
      Native tests exercise concurrent arenas, cache-slot retirement, shared
      triangle retirement, two negative controls and 1,011 worker handoffs.
      `renderThreadStats` reports both front ends started while busy and front
      ends completed before the backend. Windowed SP and MP host/client runs
      show both forms of overlap; OpenGL remains an inline control. See
      [ownership and validation evidence](presentation-owners.md#full-front-end-overlap-4-november-2026).

      **Snapshot-cost iteration, 4 November.** The in-launch comparison runner
      exercises modes 0/1/2 in balanced order at windowed 1280 x 720 Quality,
      with uncapped presentation and VSync off. It retains per-window timing,
      optional snapshot accounting, runtime identities and peer-load evidence.
      Initial measurements identified about 13 MB of copying per mode-2 frame.
      Exact source ranges now share an owned copy within one view. All geometry
      preparation precedes copying, and a bounded arena-backed lookup avoids
      per-stream heap nodes. Later views still take new copies; borrowed game
      data and cross-view caching remain prohibited without a lifetime contract.
      Native tests cover aliasing, distinct ranges, preparation order, table
      growth/capacity fallback and later-view isolation. The final nine-window
      comparison passes and avoids 34.54% of requested snapshot copy bytes
      (4.64 MB/frame reused, 8.79 MB/frame copied). All host samples contained
      competing games/build tools; elapsed snapshot time was 3.98 ms/frame
      versus 3.49 ms before reuse, so no timing gain is established. All 15
      focused tests pass. See
      [comparison evidence](presentation-owners.md#mode-comparisons-and-snapshot-reuse-4-november-2026).

      **Retained index streams, 4 November.** Mode 2 now retains immutable,
      owned index/silhouette-index and index-cache payloads across views and
      frames. Each acquisition compares the complete source range; address
      reuse or content/length changes create a new immutable version. Submitted
      frame leases survive replacement/eviction and release at retirement.
      The cache bounds payloads at 64 MiB and entries at 32,768; oversized
      ranges use ordinary snapshots. `r_vkSnapshotRetain 0` selects the earlier
      copy path for comparisons. Vertex/skinning streams still copy per view.
      Native tests cover replacement, lifetime, eviction and stale reuse.
      Same-binary mode comparisons pass all nine windows per run: copied bytes
      fall from 8.53 to 7.57 MB/frame (11.32%); elapsed snapshot time rises from
      2.90 to 5.23 ms amid competing workloads, so no timing gain is claimed.
      See [retention evidence](presentation-owners.md#retained-index-streams-4-november-2026).

      **Still open:** quiet paired mode 0/1/2 measurements, further snapshot cost
      reduction (including retained immutable vertex streams), the
      every-frame Quality floor, physical display/input qualification and
      Wayland/macOS runtime coverage. Do not promote the default from
      shared-machine measurements.
- [x] **REN-08 — P1, qualification and targeted implementation: complete post-
  processing combinations.** Qualify motion blur independently of TAA: it has its
  own history, limited rigid-opaque object vectors and subview/time-gap resets;
  existing gameplay benchmarks disable it. Cover skinned/weapon motion, mirrors/
  portals/remote views, frame caps, pause/slow frames and TAA/HDR/bloom/SSAO/scaling
  combinations on GL/Vulkan. Require no unrelated-subview history flicker, coherent
  effect strength and accurate requested/effective settings. Fix reproduced gaps
  or publish precise supported combinations, including Apple GL 2.1 offscreen/MSAA
  restrictions and Vulkan's effective 8x MSAA ceiling. Preserve unchanged gameplay.
  Completed 6 November 2026 on Windows x64 OpenGL and Vulkan. Fixed: temporal AA
  jitter read as a projection change, so motion blur never drew with TAA;
  rigid-only vectors smeared the view weapon and ignored skinning; blur length
  shrank with frame rate (now `r_motionBlurShutterMsec`, 0 restores it); equal
  presentation times, screenshots and dynamic-resolution scale steps reset the
  history; authored cuts did not. Both backends share `MotionBlurCore.h` (native
  test) and report `rendererMotionBlurStatus`. `renderer_motion_blur_qualification.py`
  passes all 41 cases and the coherence, rollback and weapon checks; supported
  combinations, Apple GL 2.1 and the Vulkan 8x ceiling are published in the
  [player guide](../user/display-settings.md#motion-blur). Evidence:
  [motion blur qualification](temporal-presentation.md#motion-blur-qualification-6-november-2026).
  Physical Linux/macOS/Android runs and appearance review remain with platform
  qualification (PLAT).

## Frame-rate recovery and performance budgets

Sources: [performance report](performance-report.md) (methodology, build matrices, feature
ladders, hitch catalog, bisection, headroom), `tools/tests/performance_suite.py`,
`src/framework/PerformancePreset.cpp` and the startup detection in `src/framework/Common.cpp`,
the shadow-map scheduling in `src/renderer/draw_arb2.cpp` and
`src/renderer/Vulkan/vk_ShadowMap.cpp`, the MSAA and SSAO policy in
`src/renderer/RenderSystem_init.cpp`, and the HF-07 attribution in
[runtime performance](ui/runtime-performance.md).

The 7-8 November 2026 measurement on the RTX 4060 Laptop (2560x1440, frame cap and vsync
off, nine scripted stock-map scenarios, three interleaved launches per build) found no
general code regression since 0.13.2. At the 0.13.2 Ultra settings the current build is
within noise of 0.13.2 on convoy1, hub2, the combat wave and the walker and 7-12% slower on
storage2, airdefense1 and the effects wave. The frame-rate drop players report is the
current **Ultra preset**, which costs 12-49 ms per frame on that GPU (19-58 fps where
0.13.2 Ultra runs 113-336 fps): 16x MSAA into the RGBA16F scene target (this GPU honours
the 16 samples; MSAA 4 alone doubles the airdefense1 frame rate), shadow-mapped point
lights with 13-tap PCSS, cascades and a 16384 atlas (30 of hub2's 54 ms), full-resolution
32-sample SSAO, 16-sample motion blur, bloom and the HDR tone map. A fresh install
auto-detects Enhanced with a 120 fps cap. This category restores a measured, budgeted
preset ladder, removes the structural costs behind it, closes the equal-settings
differences and installs a release gate, so that "Ultra" again means the best image the
reference hardware can hold at its declared frame rate rather than every maximum at once.

### FPS completion checkpoints

**Estimate: 30%; 2/9 tasks closed.** The suite, the two baselines, the per-feature
cost attribution and the declared per-scenario budgets exist on the reference machine
(the RTX 4060 Laptop). Enhanced, Quality and Ultra are re-tiered to refresh classes and
meet them on both backends; the previous Ultra is the Maximum preset. No structural
optimization has landed.

| Task / state | Completed points and evidence | Remaining subpoints / closure boundary |
| --- | --- | --- |
| FPS-01 — Closed | Suite (nine scripted stock-map scenarios, per-frame traces, cold/warm passes, toggle ladders, attribution, hitch events, transitions, idle-launch retry), 0.13.2 and current baselines on both backends, report with the settings/code split; the RTX 4060 Laptop named the reference machine; 42 budgets (7 scenarios x Ultra/Quality/Enhanced x OpenGL/Vulkan, warm mean and p99) declared from 126 launches of 94e0b14ec in `docs/dev/performance-budgets.json` and report section 10; `budgets` and `check` subcommands. | Budgets redeclared by FPS-02 (report section 11); the gate itself is FPS-09. |
| FPS-02 — Closed | Refresh classes declared at 2560x1440 on the reference machine (Enhanced 144/90, Quality 100/60, Ultra 75/45 fps average/1% low); candidates screened on both backends; the three recipes rebuilt (point cubes 512 or stencil, MSAA 0/2/4, no cascades or motion blur below Maximum); `maximum` added as the everything-on preset without a frame-rate claim; detected starts capped at the display refresh; the startup banner reports the bootstrap and the detected profile with corrected video memory; Vulkan point-policy admission fixed; 42 budgets redeclared from 126 launches, every tier inside its class in every scene. | Closed. |
| FPS-03 — Partial | Point-light cube maps identified as the dominant shadow cost; static caching and importance budgets already exist. FPS-02 sized the tiers around them: 512 cubes at Quality and Ultra, stencil point shadows at Enhanced, 2048 only at Maximum. The Vulkan light scan no longer keeps the stencil volumes of non-prelit mapped lights, including lights with an interaction left unbuilt in the view (identical images, CPU-neutral on the stock scenes). The point cube cache holds a whole view on both backends (64 slots within a memory bound, no eviction of cubes in use); OpenGL composes moving and cutout casters over cached static cubes; the default nine-tap point filter reads its sixteen texels once; receivers skip unlit fragments; the OpenGL timer queries run only for the report. In hub2 at Quality on OpenGL the shadow-mapped frame fell from 12.0 to 6.6 ms of GPU and 11.8 to 5.2 ms of CPU (hidden-window probe). Since then (10 November): Vulkan composes over cached cubes too, mapped lights shed their stencil front-end work, each projected light takes a tile size that lets a crowded view fit, the projected cache is bounded by memory, OpenGL cubes are depth only, cutouts that cannot change are cached, and Vulkan reads unchanged projected maps where they live. | Fullscreen tier budgets, including whether Enhanced can map point lights; one static cube for both ownerships of a composed Vulkan light; static-world volumes, still built once per mapped light. Per-face cube culling was measured and set aside. |
| FPS-04 — Not started | SSAO, motion blur, HDR tone map, bloom and SMAA priced per scene on both backends (the chain is 10-14 ms per frame at 16 samples on OpenGL, 4-6 ms at 8 samples on Vulkan). | Half-resolution SSAO on OpenGL, pass merging, MSAA-aware resolves, tone-map cost. |
| FPS-05 — Not started | 16x MSAA measured as the largest single Ultra cost on OpenGL; the Vulkan backend found to clamp the preset's 16 samples to the device's 8, so the two backends render different Ultra images. FPS-02's Ultra uses 4x MSAA with SMAA ultra, which both backends honour; only Maximum still requests 16. | Sample-count policy per tier, resolve cost, one sample count on both backends, MSAA 4 + SMAA as the Ultra candidate. |
| FPS-06 — Not started | Equal-settings deltas attributed: in storage2 the retained HUD's layer composition (17 two-dimensional views and 34 render-target commands per frame against the classic HUD's 7 and 17; about 0.4 ms of main-thread time); on Vulkan the soft-particle depth capture (1.3 ms in airdefense1). The 2 October build equals 0.13.2 and the 15 October build carries the storage2 step, bracketing the 8 October retained-HUD commit (bisection); interpolation, light grid, reverb, portal cull and the five new Vulkan backend features cost nothing measurable. | Bisection pass 2 for the remainder, the retained HUD composed in one pass, the Vulkan soft-particle capture made cheap, closure within 3% average and 10% p99 of 0.13.2 on every suite scene on both backends. |
| FPS-07 — Not started | Hitch classes recorded: 2-3 s cold draw stalls at Ultra, periodic 100 ms hub2 frames, a 393 ms storage2 frame, first-use events. | Attribution per class, fixes or declared budgets, clean first-use catalog on both backends. |
| FPS-08 — Not started | HF-07 attribution: the Quality frame is CPU-bound on one thread; Vulkan render-thread stage 1 landed and gains nothing at the GPU-bound Ultra (within 0.4 ms, longer tails). Constant material registers restored on both renderers (they were never marked constant, so every surface evaluated its registers every view): 69-87% of register set-ups now take the parse-time block, saving 5-22 µs per frame in hub2, convoy2 and the airdefense1 arena, which whole-frame timings cannot resolve; see [Constant material registers](material-constant-registers.md). | Front-end and submission cost, stage 2 overlap, game-tic cost; shared with HF-07 and REN-07. |
| FPS-09 — Not started | Prevention plan written (report section 9). | Suite in the release checklist, budgets as gates, cvar-default diff and attribution checks in CI. |

### FPS task evidence and acceptance

- [x] **FPS-01 — P0, qualification: performance suite, baselines and budgets.** Keep
  `tools/tests/performance_suite.py` the single way frame rates are measured for this
  project: scripted stock-map scenarios driven by `g_autoExecAfterMapLoad` and
  `benchmarkViewSweep`, never synthesized input; per-frame traces from
  `profileFrameWork`; cold and warm passes; interleaved builds; the per-launch CPU/GPU
  load record. Run the OpenGL and Vulkan matrices on the project's reference machine,
  then declare a per-scenario budget (warm mean frame time and p99) for each supported
  tier at 2560x1440 and record it in the [performance report](performance-report.md)
  beside the release notes. Close when the budgets exist for both backends on the
  reference machine and the suite's `report` output is the artifact the release
  workflow cites. Completed so far: the suite, the OpenGL and Vulkan matrices of 0.13.2,
  the current build and the current build at the 0.13.2 settings on the RTX 4060 Laptop,
  the settings/code split, and the feature ladders and restart variants on both backends.
  Closed 8 November 2026. The reference machine is the RTX 4060 Laptop (MSI Vector GP77
  13VF, i7-13700H, 2560x1440 fullscreen) that measured the report. The reference run
  (`.tmp/perf-suite/ref-gl`, `ref-vk`, runtime `.tmp/stock-runtime/perf-ref`, `main`
  94e0b14ec, client SHA-256 `44064cda...`) measured the seven timed scenarios at
  `own-ultra`, `own-quality` and `own-enhanced` on OpenGL and Vulkan, three rounds of
  two passes each, 126 launches (one idle launch repeated). `performance_suite.py
  budgets` wrote the 42 warm-mean and p99 budgets with their gate limits (+5% mean,
  +10% p99) to [`performance-budgets.json`](performance-budgets.json), and section 10 of
  the report tabulates them. `performance_suite.py check --budgets
  docs/dev/performance-budgets.json --runs <candidate runs>` gates a candidate run
  (exit 1 on a regression). The release checklist in
  [release-completion.md](release-completion.md) cites the `check` table and the
  `report` output (`summary.md`, `aggregate.json`) as the release artifact. The Vulkan
  convoy1 p99 budgets at Quality and Enhanced contain a Vulkan presentation stall
  (slow presents with short frame work, sometimes a swapchain recreation; FPS-07), not
  a game-side hitch.
- [x] **FPS-02 — P0, decision and implementation: re-tier the presets to declared
  budgets.** Ultra currently requests every maximum at once and no tier is tied to a
  frame rate. Declare, per tier, the resolution and reference hardware it must hold and
  the frame-time budget it must meet (the user's Ultra target is 220 fps average and
  180 fps 1% low at 2560x1440 on the reference machine). Rebuild the Ultra, Quality and
  Enhanced recipes in `src/framework/PerformancePreset.cpp` from the measured feature
  costs: MSAA sample count (FPS-05), shadow-map tier (FPS-03), SSAO resolution and
  samples, motion-blur samples, bloom mips and SMAA quality (FPS-04). Keep an explicit
  "everything on" choice for players who want it, labelled without a frame-rate claim.
  Decide the fresh-install behaviour from the `own-default` measurements: the
  auto-detected tier, its frame cap against the display refresh rate, and the startup
  banner that today prints the bootstrap `minimum` profile with 64 MB of video memory
  before the device exists (`Quality profile:` in `src/framework/Common.cpp`; the
  detected tier is applied later and `com_videoRam` is never corrected). Update the
  preset self-test, the SYSTEM catalog and the player documentation together. Close when
  every re-tiered preset meets its declared budget on the reference machine on both
  backends (rerun `performance_suite.py budgets` for the new recipes and commit the
  updated `performance-budgets.json`), the self-test passes, and the release notes describe the change and what a
  player must re-select.
  Closed 9 November 2026. The targets are refresh classes at 2560x1440 on the reference
  machine, on both backends and in every timed scenario: Enhanced 144 fps average and
  90 fps 1% low, Quality 100/60, Ultra 75/45; 220/180 stays a goal for the lighter
  scenes, not a tier gate. Screening on both backends (report section 11.2) priced the
  levers: point-light cube size dominates (2048 cubes double the hub2 p99), cascades
  cost about 4 ms on OpenGL, MSAA about 1 ms per halving once cubes are small. The new
  recipes (section 11.3): Enhanced drops MSAA for SMAA high, leaves point lights on
  stencil shadows (`r_shadowMapPointLights` is now a per-preset value) and caps at
  144; Quality uses 2x MSAA and 512 cubes; Ultra uses 4x MSAA, 512 cubes, an 8192
  atlas and no cascades or motion blur; all use 16x anisotropy. The previous Ultra
  recipe is the new `maximum` preset (**Maximum**, localized in the 12 language
  tables), which carries no frame-rate claim and which Auto-Detect never selects. An
  automatic recommendation caps `com_maxfps` at the display's whole-hertz refresh
  rate; the `Quality profile:` banner names the first-launch bootstrap and reprints the
  detected profile with `com_videoRam` corrected. The Vulkan shadow-map admission now
  applies the point-light policy before the caster and capacity checks, which had
  logged sticky `shadow map pass failed` errors with point maps off. The FPS-02 run
  (`.tmp/perf-suite/fps02-gl`, `fps02-vk`, 126 launches) meets every class: Ultra
  91-188 fps, Quality 120-238, Enhanced 166-298, worst p99 15.40, 11.70 and 10.39 ms;
  `performance_suite.py budgets` redeclared the 42 budgets, and the runs pass `check
  --require-all`. The preset self-test (`presets=8`), `ui_performance_presets.py`, the
  SYSTEM catalog tests and the in-game preset smoke pass on both backends. Player
  guidance and re-selection notes are in [display settings](../user/display-settings.md)
  and [release-completion.md](release-completion.md).
  Follow-up, 10 November: the [hardware-range audit](ui/performance-presets.md#hardware-range-audit-10-november-2026)
  extends the ladder to nine presets without changing these three recipes or
  their budgets. A new **Low** preset serves the documented minimum PC, Minimum
  and Low Power draw no shadows, and Auto-Detect places 12, 16 and 24 GiB cards
  on Quality and Ultra by memory and resolution class (Maximum stays manual).
  The reference laptop still detects Enhanced. The larger classes are policy
  derived from this item's measurements, not measured on such cards.
- [ ] **FPS-03 — P1, implementation: shadow-map cost.** In hub2 the shadow maps are 33 of
  the current Ultra's 54 ms per frame and 30 ms of that is point lights: a point light
  with any moving caster in range re-renders all six cube faces every frame, because
  only projected lights compose dynamic casters over cached static depth
  (`RB_ShadowMapStaticCacheable`, `src/renderer/draw_arb2.cpp`). Implement the
  dynamic-caster composition for point lights, cull cube faces that cannot reach the
  view, make the importance-ordered update budget (`r_shadowMapMaxUpdatesPerView`) part
  of every tier instead of unlimited at Ultra, size the atlas and the per-light maps by
  tier and available video memory (the 16384 atlas is 1 GiB of the 8 GB card), lower
  the PCSS tap count with distance, and remove the per-surface quadratic light scan
  on Vulkan (`R_ShadowMapLightWillUseShadowMaps`). Close when hub2 and storage2 at the
  re-tiered Ultra spend less than a declared share of the frame on shadow maps on both
  backends, with image comparisons showing no shadow regression at the stock maps'
  reference poses.
  Completed so far: the Vulkan light scan (9 November 2026). On Vulkan,
  `R_ShadowMapLightWillUseShadowMaps` treated any NULL surface slot of a light's
  interactions as unknown. A finished interaction leaves every surface with no geometry
  or outside the light NULL, so Vulkan kept and linked the stencil volumes of every
  non-prelit shadow-mapped light that OpenGL drops. The scan now counts only the filled
  slots of a finished interaction, answers for the whole model while one is being built,
  and still treats an unbuilt one as unknown. It is memoized on the view light,
  invalidated by every interaction state change and by a GLSL program reload, which
  replaces the per-surface rescan; `renderer_vulkan_shadow_compatibility.py` pins the
  contract. At the Quality preset on Vulkan, hub2 now drops the volumes of 3.4-3.8
  non-prelit mapped lights per view (none before) and storage2 of 0.3; Air Defense 1 has
  no such light. Eight frozen poses on hub2, storage2 and Air Defense 1 render
  byte-identical images with the old and the new scan (a ninth did not freeze), and
  the shadow-map counters match. **The CPU result is neutral within measurement.** An
  ABBA frozen-pose A/B in hub2 (hidden 2560x1440, `profileFrameWork` draw time) differs
  by -0.10 ms (sd 0.26) and +0.07 ms (sd 0.65). `tools/debug/cpu_sampler.py` puts
  `R_RenderView` at 7.9% against 8.3% of main-thread samples (-0.07 ms per frame) and
  the scan itself at no samples. The fullscreen `performance_suite.py` ladder at
  `own-quality` stayed within -0.08/+0.15 ms in the one hub2 round that held the
  foreground; those frames are GPU-bound. The saving is small for two reasons. Prelit
  lights, 12-19 of hub2's shadowing lights per view, keep their volumes by design before
  the scan, so the separate 2.6% prelit fallback-volume share is untouched. Dynamic
  volumes are still built while another entity's interaction on the same light is
  unbuilt (`createShadowVolumes` per frame did not fall), and a light with an
  interaction that is never built in the view keeps its volumes. With its volumes
  dropped, a light whose map pass fails draws unshadowed for one frame before the sticky
  fallback restores them, as on OpenGL. This was seen once, for the weapon flashlight,
  in about twenty launches, and did not recur in four targeted runs.
  Unbuilt interactions (9 November 2026, follow-up). An unbuilt interaction no longer
  holds a light's volumes once it cannot gain receivers in the view: either its entity
  is not a view entity, or `AddActiveInteraction`, the only builder, which runs once
  per interaction per view, returned leaving it unbuilt (`unbuiltVisitView`). At the
  end of each view every non-prelit mapped light can now drop its volumes. Airdefense2
  with the flashlight has 2,723 elided and 0 kept light-views against 1,811 and 912;
  hub2 has 8,121 and 0 against 7,901 and 183. Ten frozen poses on hub2, storage2, Air
  Defense 1 and airdefense2 render byte-identical images with the rule on and off (an
  eleventh did not freeze). An interaction still waiting for its entity's turn in
  `R_AddModelSurfaces` stays unknown, so entities processed before it keep their
  volumes; that costs work but never a shadow. On the stock scenes no dynamic volume is
  built under a non-prelit mapped light (`createShadowVolumes` did not change), so this
  ordering costs nothing measurable there. The CPU change could not be measured above
  the noise of a machine at 76% load. `cpu_sampler.py` puts the shadow-volume linking it
  removes at 0.03% against 0.11% of airdefense2's main-thread samples. A light whose
  map is not ready in the frame `r_shadows` or `r_useShadowMap` is switched back on now
  draws unshadowed for that one frame, as on OpenGL.
  Audit round (9 November 2026). The [audit](shadowmapping-audit-2026-11-09.md) counted
  the stock lights (10,477 shadow-casting point lights against 518 projected, forty lit
  at once in hub2) and found the point cube cache held twelve. In the hub2 sweep 29
  cubes were re-rendered every frame. Done on both backends: a 64-slot cache inside
  `r_shadowMapPointCacheMegs` that never evicts a cube in use, the nine-tap point filter
  as one pass over its sixteen texels, no shadow filtering for unlit fragments, and a
  quarter-second live-caster window so per-tic entities stop flipping into the static
  set at high refresh. Also on both: live casters composed over a copy of the cached
  static cube (`r_shadowMapPointCompose`), which on Vulkan brought the point phase to
  half to two thirds of its GPU time where lights have live casters (0.06 to 0.15 ms
  a frame in hub2 and the airdefense1 arena, no measurable CPU change). Done on
  OpenGL: one static entry for both ownerships, and
  timer queries only while the report is on, which were about half of that scene's
  CPU frame. Hub2 at Quality on OpenGL: 12.0 to 6.6 ms of GPU and 11.8 to 5.2 ms of
  CPU, the premium over stencil point shadows down from 5.7/4.1 ms to 1.1/0.8 ms;
  on Vulkan the mapped frame is within 0.1 ms of GPU of the stencil one in hub2 and
  storage2. These are hidden-window probe figures, not fullscreen budgets.
  Stencil front-end work (10 November 2026). A mapped light no longer builds or
  submits the volumes it does not draw: prelit lights shed them like any other, an
  animated model gets none (the old test asked the instantiated snapshot, always
  static) and has them rebuilt when its light returns to stencil, and a caster beyond
  its shadow LOD distance stays in the map with its volume instead of putting the
  light on the stencil supplement for good. Atlas lights shed volumes only in a view
  whose atlas is certain to hold them all, and Vulkan's receiver scan no longer waits
  for entities the view cannot see. In the airdefense1 arena shadow volumes built per
  frame went from over thirty to none on both backends; the CPU frame is 0.14 ms
  shorter on OpenGL and 0.1 ms on Vulkan against the switch.
  Atlas tile sizes (10 November 2026). Quality and Ultra cut four projected atlas
  tiles and a light with both receiver ownerships takes two; convoy2's start view
  has up to eight such lights, and on Vulkan five went to stencil for the level and
  one was drawn unshadowed. The front end now sizes each atlas light's tile once the
  view is finished: full size while the view fits, else halved from the lights that
  cover the least world up, to a quarter at most. Vulkan cuts the atlas by buddy
  allocation, OpenGL's grid cell is the smallest tile, and neither evicts a projected
  tile in use. That view now maps eleven atlas passes with none on stencil; no preset
  changed and a view that fitted before renders as before.
  Projected cache memory (10 November 2026). The projected cache counted eight
  entries whatever their size, so that view drew three of its eleven atlas passes
  again every view. `r_shadowMapProjectedCacheSize` now counts full-size maps' worth
  of tile texels (default eight, the same memory ceiling) and a map cut down takes
  its share; OpenGL's cache is bounded by its atlas instead. All eleven passes are
  reused on Vulkan and OpenGL misses none. The frame-time gain
  there is small, 0.24 ms of Vulkan GPU across six alternating pairs and nothing
  resolved on OpenGL.
  Depth-only point cubes on OpenGL (10 November 2026). An OpenGL point cube also
  kept a colour cube of packed depth that only the receivers without hardware
  compare, the opt-in modern renderers and the debug overlay read. It is now made
  only while one of those needs it (`r_shadowMapPointDepthOnly`, on by default):
  resident shadow memory in the combat view 212 to 152 MiB, 64 cubes in the 512 MiB
  bound where 42 fitted, and GPU frames 0.45 ms shorter there and 0.21 ms in hub2
  over six alternating pairs.
  Static cutout casters (10 November 2026). Every cutout (alpha-tested) caster was
  classed live, so a light that reaches a grate or a wire cap copied its cached map
  and redrew the cutout every view: 25 passes a frame in convoy2's start view, 20 of
  them with no other live caster. A cutout is now live only while its coverage can
  change (`r_shadowMapStaticCutouts`, on by default): 3 passes composed there, the
  Vulkan shadow phases 1.36 to 0.71 ms, and the OpenGL frame 1.75 ms shorter on the
  GPU over six alternating pairs. Skipping the cube faces a view cannot see was
  measured afterwards and set aside: all per-view cube work is then 0.07 to 0.16 ms
  of Vulkan GPU in four scenes.
  Resident projected tiles on Vulkan (10 November 2026). Vulkan copied every reused
  projected map into the view's atlas, each view, to sample it there. A reused map
  with no moving caster is now read where it lives (`r_vkShadowSampleResident`, on
  by default): in convoy2's start view the cache phase fell from 0.54 to 0.04 ms and
  all shadow phases from 0.68 to 0.18 ms, with captures identical to the byte and
  the validation layers silent.
  Walking (10 November 2026). The cache had only been measured with the camera
  turning on the spot. Moving the player through the level showed lights rendering
  their maps again with unchanged casters: the static signature hashed whether the
  view also saw each caster's stencil volume, which flips as the player walks past.
  Without that bit a 1,200 unit walk through hub2 renders 24 fresh maps where it
  rendered 48 on Vulkan and 18 where it rendered 41 on OpenGL, and the views that
  render any fall from about 20 to 4 to 7. The picture check for it found the
  projected filter's grain following a light's tile around the atlas, so a cached
  map and the same map drawn fresh differed (19,000 pixels in hub2's start view,
  both renderers); the kernel now turns by the texel within the tile and they agree
  to within 1 pixel on Vulkan and 77 on OpenGL. One static cube for both ownerships
  of a composed Vulkan light was measured at 4 of 24 fresh maps on that walk and
  not built.
  Static membership (10 November 2026). Which still casters a map held also
  followed the camera: the shadow LOD admitted an entity's shadow within 768 units
  and dropped it beyond, and a model's first snapshot counted as the model moving
  for a quarter of a second. A static caster of a still light is now in the map at
  any distance and a first snapshot is not motion; fresh maps on four walks fall
  from 24 to 18, 79 to 63, 10 to 8 and 11 to 7, and shadow-mapped lights cast the
  shadows of still scenery beyond 768 units.
  Scratch cube pool (10 November 2026). Vulkan's pool of scratch point cubes only
  grew, to 512 cubes: one view from outside a level left 2.0 GB resident. It now has
  the cache's memory figure again as its ceiling (85 cubes at Quality) and releases
  what no view has needed for five seconds: 1.1 GB at that view, 0.6 GB ten seconds
  after it. Remaining, in order:
  the fullscreen budgets, including whether Enhanced can map point lights; and
  static-world volumes, which are still built once per mapped light for the
  fallback. (A light that moves now draws its maps directly instead of publishing
  each one to the cache, copying it and composing over the copy: 560 passes on one
  hub2 route, `r_shadowMapCacheMovingLights`.) (A light whose map failed once now stops building its stencil volumes
  again after 240 views without another failure, `r_shadowMapStencilFallbackViews`;
  before, it built them for the rest of the level.)
- [ ] **FPS-04 — P1, implementation: post-processing chain cost.** Price and reduce the
  screen-space passes the current Ultra adds: SSAO (full resolution on OpenGL; the
  half-resolution path exists only on Vulkan and only below 32 samples), motion blur
  (16 samples), the HDR tone map (3 ms in airdefense1 at 16x MSAA), bloom (5 mips) and
  SMAA ultra: 10-14 ms per frame at 16 samples on OpenGL, 4-6 ms at 8 samples on Vulkan.
  Bring the half-resolution SSAO to OpenGL, merge passes that read the same
  scene target, make the resolves MSAA-aware so the tone map does not pay a second
  resolve, and set the sample counts per tier. Close when the post chain at the
  re-tiered Ultra costs less than a declared budget per scene on both backends and the
  feature ladders show each pass within its share.
- [ ] **FPS-05 — P1, decision and implementation: MSAA and the HDR scene target.** The
  Ultra recipe requests 16 samples; OpenGL renders them on this GPU (`GL_MAX_SAMPLES` 32)
  into an RGBA16F target at 2560x1440, where MSAA 4 alone doubles the airdefense1 frame
  rate, while the Vulkan backend clamps the request to the device's 8 colour samples
  (`Forward render target MSAA: requested 16, effective 8`), so the two backends render
  different Ultra images and the Vulkan Ultra is 1.7-2.7x faster for that reason alone.
  Measure 16/8/4/2/0 with and without the HDR target and with SMAA/TAA alternatives on
  both backends (16/4/0 and 8/4/0 are in the report), declare the sample-count policy
  per tier and resolution so that a preset means the same sample count on both backends,
  and make the resolve and the sample count visible in `gfxInfo` and the settings menu.
  Close when the chosen Ultra sample count is justified by the measurements in the
  report, both backends honour it or report the clamp, and the settings menu names the
  trade-off.
- [ ] **FPS-06 — P1, investigation and implementation: equal-settings regressions.** At
  the 0.13.2 settings the current build is slower than 0.13.2 on OpenGL by a constant
  1.0-1.2 ms per frame in storage2 (0.9-1.0 ms of it in the draw phase, in every window
  of the drift launches; airdefense1 is within 0.05 ms) and spends more spike frames in
  the effects wave (183 against 17 over three warm launches); the 2 October build
  (834564945) is within 3% of 0.13.2, so the storage2 regression entered after it. On
  Vulkan the current build is slower in every scene, +0.8 to +2.3 ms per frame (+24-50%),
  85-90% of it in the draw phase, where 0.13.2's Vulkan backend was 4-25% faster than its
  OpenGL one; 1.3 ms of the airdefense1 share is the soft-particle depth capture, a
  feature the 0.13.2 backend did not draw, which leaves the forward pass and resolves the
  8-sample depth per view (four times OpenGL's cost for the same effect), and the current
  build issues 7-15% more draws in the airdefense1 scenes. In storage2 the difference is
  the retained HUD: its layer composition binds an offscreen target per layer and mask,
  every bind flushes the pending 2D batch as a full-screen view, and the frame carries 17
  two-dimensional views and 34 render-target commands against the classic HUD's 7 and 17
  (`r_rendererMetrics 2`), about 0.4 ms of main-thread time on OpenGL and render-pass
  boundaries with attachment store and load on Vulkan, where that scene is GPU-bound.
  Presentation interpolation, the light grid, reverb, the portal cull and the five Vulkan
  backend features that stay on at the 0.13.2 settings (snapshot retention, window-view
  multisampling, the portal-sky mask, ARB program translation, the precise frame wait)
  cost nothing measurable. Compose the retained HUD's layers in one offscreen pass, or
  with scissor and stencil instead of per-layer targets, so the HUD adds one 2D view like
  the classic one; make the Vulkan soft-particle capture read the prepass depth without a
  render-pass break; the bisection (one build per week at the 0.13.2 settings against the
  0.13.2 rebuild) places the storage2 step between the 2 October and 15 October builds,
  around the 8 October commit that made the retained HUD the gameplay HUD, so the HUD
  composition is the fix to verify; then fix or document each remaining component. Close when the current build at the 0.13.2
  settings is within 3% average and 10% p99 of 0.13.2 on every suite scene on both
  backends.
- [ ] **FPS-07 — P1, implementation: hitch catalog.** Attribute and remove the stalls the
  matrices recorded: 2.0 s and 3.0 s draw stalls in a cold convoy1 pass at the current
  Ultra, periodic 100-124 ms draw frames in hub2 at the same sweep direction every turn,
  a 393 ms storage2 frame (134 ms of events, 252 ms of draw), and the first-use events of
  the `hitch_firstuse` scenario (weapons, first spawns, first explosion, scope, flashlight)
  and the level transition. Close when each class is attributed in the report, fixed or
  given a declared budget, and the hitch scenario shows no frame above a declared ceiling
  outside level loads on both backends.
- [ ] **FPS-08 — P2, implementation: CPU-side renderer and game cost.** The HF-07
  attribution found the Quality frame CPU-bound on one thread (tic, scene front end and
  Vulkan recording in turn); the render-thread prototype's first stage gained 0.5 ms
  there and nothing at the GPU-bound Ultra (within 0.4 ms in the Vulkan ladders, with
  4 ms of the frame moved into the session phase and longer tails).
  Measure the front end, backend submission (OpenGL draw submission with shadow maps) and
  game tic per scene from the attribution runs, land the stage-2 overlap under REN-07,
  and reduce the front-end and tic work the attribution names. Close when the CPU-bound
  scenes meet their declared budgets with the GPU as the limit; shared with HF-07.
- [ ] **FPS-09 — P1, qualification: regression gate.** Put the suite in the release
  checklist (`docs/dev/release-completion.md`, the manual release workflow): the core
  scenarios on the reference machine before every release and on every renderer,
  game-loop or asset-loading change, with the per-scenario budgets from FPS-01 as the
  gate (5% average, 10% p99; `performance_suite.py check` against
  `performance-budgets.json`), a cvar-default diff that fails when a default with a
  performance cost changes without a release note, the preset self-test, and the toggle
  ladder as the required before/after evidence in commits that touch post effects,
  shadows or presentation. Close when a release candidate has been gated by it once.

## Virtual reality

Sources: [OpenXR implementation history](archive/2026-10-18/plans/2026-10-02-openxr-vr.md),
[player guide](../user/vr.md) and current
`src/sys/openxr/OpenXRSystem.cpp`.

### VR completion checkpoints

**Estimate: 75%; 4/6 tasks closed.** Credit the OpenGL and Vulkan PC paths,
their fault handling, play spaces and per-eye TAA on the simulated runtime.
Synthetic stereo results do not qualify a headset, its comfort, its frame
budget or standalone Quest; those are the [headset checklist](vr-headset-qualification.md)
and AND-05.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| VR-01 — Closed (PC scope) | Vulkan (runtime-created device), desktop-independent submission, Wayland EGL and SP world-GUI pointing integrated at render API 32 with VR-06's guarantees; GL/Vulkan SP/MP/menu smokes, the Vulkan fault matrix and Linux Wayland/X11 bindings pass on the simulated runtime. | Quest standalone moved to AND-05 by decision; physical headsets are VR-02. |
| VR-02 — Partial | GL and Vulkan stereo, tracking/input, room scale, play spaces, aiming, haptics, world GUIs, per-eye TAA and comfort controls provide the testable implementation; the [headset checklist](vr-headset-qualification.md) is ready to run. | Run the checklist on each available PC runtime/headset: combat, refresh/latency, controls, collision, vehicles, HUD/world-GUI readability and recovery. |
| VR-03 — Closed | STAGE/LOCAL_FLOOR/LOCAL selection with warned fallback, floor-referenced standing height, recentering, calibration and live seated/standing changes pass natively and on the simulated runtime (GL/Vulkan). | Physical body alignment is checklist section 4 (VR-02). |
| VR-04 — Partial | `openxr_vr_stereo_cost.py` records flat/stereo/warm-stereo CPU, build, submit and GPU tails, now at a chosen preset (`--preset quality`); the simulated baselines on this GPU are recorded below. Uploads, dynamic geometry, remote cameras, cached static shadow maps and (8 November) world-GUI quads are shared between the eyes; culling, lights, draws, mirror/reflection/refraction/x-ray maps and dynamic shadow composition stay per eye. `openxr_vr_stereo_share_smoke.py` proves both by work count on OpenGL and Vulkan. | Headset tails against the refresh budgets (checklist 2.2), and sharing work where those tails show a view-independent cost. |
| VR-05 — Closed (approved scope) | Decision: per-eye TAA only. Each eye has its own view identity, history images, motion vectors and resets in the SP game chain and both backends' own temporal scenes; dynamic resolution stays off in VR. SP/MP GL/Vulkan pass the simulated-runtime smoke. | Headset comfort is checklist section 7; hand presence and physical-object interaction need their own designs. |
| VR-06 — Closed | Active GL teardown/ownership repair passes native failure cases and simulated-runtime SP/MP gameplay. | Carry guarantees into VR-01 backends; physical controller/headset validation remains VR-02. |

### VR task evidence and acceptance

- [x] **VR-01 — P1, integration: review recoverable Vulkan/EGL/Quest changes.**
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
  [XR/Android recovery review](platform-recovery-review.md).
  Complete for the PC scope, 6 November (`f6034967f`, `244cb2457` and the VR-05
  commit): the runtime creates Vulkan's instance and device through
  `XR_KHR_vulkan_enable2` (render API 32), eye images are the executor's output
  per VR target, a frame opens without a desktop swapchain, OpenGL binds through
  EGL on Wayland and GLX on X11, and single-player focus starts at the VR aim.
  VR-06's bounded counts, index checks and deferred retirement cover Vulkan.
  GL and Vulkan (validation on) SP, MP and menu smokes, the play-space and
  per-eye TAA smokes, the Vulkan fault matrix (windowless included) and Linux
  native-Wayland Vulkan/EGL and X11 Vulkan/GLX runs pass on the simulated
  runtime; see the [integration evidence](platform-recovery-review.md#vulkan-egl-and-world-gui-xr-integration-6-november-2026).
  Quest (`65aa9bd88`) is not integrated: by decision it waits on AND-05's
  distribution decision. Physical headsets remain VR-02.
  9 November: the review missed two fixes on that branch. The off-axis culling
  planes fix (`ea85bdd7b`) is restored in `3a40f705c`; without it the eyes lost
  near terrain and wreckage on both renderers
  ([evidence](platform-recovery-review.md#vr-eye-culling-restored-9-november-2026)).
  The seat-turn fix (`383eebfc5`: when a script turns a vehicle in the same
  frame it seats the player, the seat missed that turn, leaving The
  Awakening's m07 speeder bike facing backwards) is restored in `00c2addb7`
  ([evidence](platform-recovery-review.md#vr-seat-turn-restored-9-november-2026)).
- [ ] **VR-02 — P1, qualification: real headset gameplay and comfort.** Qualify
  available PC runtimes/headsets for refresh pacing, tracking latency, aim/grip,
  handedness, two-hand weapons, haptics, room-scale collision/MP prediction, crouch,
  seated vehicles, zoom, menus/HUD/world GUI and loss/reconnect. Exercise full combat
  and camera transitions. Synthetic stereo/test-runtime success cannot establish
  physical tracking, readable UI or comfort.
  The [headset checklist](vr-headset-qualification.md) covers every point above
  on both renderers, plus the hardware halves of VR-01 and VR-03 through VR-05.
  10 November: a code audit on the simulated runtime, with no headset, fixed
  defects a headset run would have met at once
  ([release record](release-completion.md#vr-shots-that-stay-on-the-laser-a-weapon-wheel-that-answers-your-hand-and-a-pause-when-the-headset-comes-off-10-november-2026)):
  a pitch offset between shots and the laser after a load, vehicle exit,
  cutscene or stick click (`BUTTON_VRAIM`, both game modules); levels and
  saves starting turned by earlier snap turns; a weapon wheel the hand could
  not steer under the retained interface; the runtime's recentre applied
  before its change time; no pause when the headset loses focus; zoom that
  snapped; and a mirror that ignored window resizes. Verified leads left for
  the headset run or a later round: room-scale steps against the interpolated
  eye (up to one tic's walk of shimmer), a save made in a vehicle seat loading
  with an arbitrary seat facing, the multiplayer comfort vignette firing on
  real room-scale steps, multiplayer damage numbers projected from the right
  eye, render demos recorded in VR storing both eyes' 2D, and checkpoint
  thumbnails capturing the desktop mirror.
- [x] **VR-03 — P2, implementation: standing space and floor calibration.** Define
  LOCAL/STAGE/floor behavior, recentering, seated/standing transitions and safe
  fallback when a runtime lacks stage space. Validate crouch/body alignment and
  room-scale bounds without double-applying physical movement.
  Complete in `b8995be28`: `vr_playSpace` chooses STAGE, then LOCAL_FLOOR, then
  LOCAL (warning when standing play falls back); standing play measures height
  from the floor so a crouched or seated recenter keeps it, `vr_calibrateHeight`
  measures it again, and a change applies between frames. The choice is pure
  math with native tests, and `openxr_vr_playspace_smoke.py` passes on GL and
  Vulkan with the runtime offering every space combination. Physical alignment
  is checklist section 4.
- [ ] **VR-04 — P2, measured optimization: stereo frame cost.** Profile two-eye
  front-end work, shadows, particles, UI and loading on actual hardware. Share
  view-independent work while preserving eye-specific culling/pose/output and
  exactly-once effect servicing. Establish headset-specific CPU/GPU tail budgets.
  Simulated baseline, 6 November (`openxr_vr_stereo_cost.py`, airdefense1, two
  1440x1600 eyes against a 1920x1080 window, unpaced runtime): see the
  [measurement](platform-recovery-review.md#stereo-frame-cost-vr-04). The
  budgets are the headset's frame interval: 11.1 ms at 90 Hz, 12.5 ms at 80 Hz,
  13.9 ms at 72 Hz, for both the CPU and the GPU p95. This shared desktop is too
  noisy to attribute a sharing change; headset tails (checklist 2.2) decide it.
  8 November, by deterministic work count instead of time
  ([stereo sharing](platform-recovery-review.md#stereo-work-sharing-8-november-2026)):
  the second eye emits the world-GUI quads the first eye drew instead of
  running each GUI again (`r_vrStereoGuiShare`, default on; half the GUI
  redraws). Mirror, reflection, refraction and x-ray maps now render for each
  eye: they follow the viewer, and the second eye had been showing the first
  eye's capture. Remote cameras still render once per frame. At the Quality
  preset static shadow maps are already reused by both eyes (no fresh
  renders); each eye composes its own dynamic casters, one composition per eye
  in airdefense1, too small to justify sharing it across both backends yet.
  Found on the way and fixed 9 November, OpenGL only and older than this work
  ([fixes](platform-recovery-review.md#opengl-portal-sky-and-mirror-fixes-9-november-2026)):
  a world GUI vanished wherever the portal sky sat behind it, most of all in
  the eye views, and a mirror render map drew streaks in every view.
- [x] **VR-05 — P3, decision and implementation: per-eye temporal features and
  further interaction.** TAA/dynamic resolution require independent eye histories,
  motion vectors, resets and scale control before enablement. Hand presence and
  physical-object interaction require separate gameplay/comfort designs. Quest
  distribution and device qualification also depend on AND-01 through AND-05.
  Decided 6 November: per-eye TAA only; dynamic resolution stays off in VR, and
  hand presence/physical interaction are not in scope. `r_temporalAA 1` resolves
  each eye from its own history: the eye is mixed into its view identity, and the
  game's history ping-pong, OpenGL's and Vulkan's backend history images,
  velocity/entity history and Vulkan motion vectors keep a slot per eye. Each VR
  target command carries its temporal state to the backend, and Vulkan pairs a
  portal sky with its own eye's main view. Automatic TAA stays off in VR.
  `openxr_vr_temporal_smoke.py` passes SP and MP on GL and Vulkan: every resolve
  keeps history, no per-view resets or fallbacks, parallax and convergence hold,
  and a head turn does not smear. Headset comfort is checklist section 7.
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

### AND completion checkpoints

**Estimate: 66%; 1/5 tasks closed.** Onboarding, touch and several lifecycle
repairs are already integrated; they are not all still archived. S25 and emulator
checks are bounded evidence, not the complete Adreno/Mali/API/lifecycle matrix.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| AND-01 — Closed | Phone/emulator scope, startup ownership, UI-thread binding, background handling, Back/cutouts and touch controls repaired; S25 save/touch/audio checks exist; host-owned orientation and the portrait thumb deck pass a mid-match emulator rotation; the per-orientation layout editor, suspension save, multiplayer resume/reconnect and Quest flavor package checks pass native/JVM and emulator probes. | None; S25 hardware checks pass (layout, portrait, suspension save, background kill/restore, multiplayer reconnect, editor by hand). Residuals moved to AND-02/AND-04. |
| AND-02 — Partial | The device harness and S25 (Adreno 830, API 36) campaign pass cold and warm: real level transitions, save/load, audio residency, storage, thermal and per-model budgets. | S25 sustain and multiplayer phases; audible and gamepad/mouse/keyboard checks; Mali and API-floor devices. |
| AND-03 — Partial | Verified package repair/import, localized setup, settings return, control/interface setup and save/log export implemented; scoped device round trips pass. | Release-signing continuity; cloud/removable providers; interrupted-process recovery; physical setup/accessibility and upgrade matrix. |
| AND-04 — Partial | Touch presets, UI/text sizing, an opt-in portrait orientation, retained safe areas, HUD safe-area and thumb-deck positioning and the per-orientation layout editor exist. | Device-based presets and GLES limits; keyboard/touch targets, human-finger portrait play, the weapon control over the portrait readouts, stock GUIs in portrait, cutouts/gesture bars/folds/multi-window qualification. |
| AND-05 — Not integrated | Quest recovery sources are identified under VR-01/AND-01; the archived Quest change (`65aa9bd88`) moved here from VR-01 on 6 November, on top of the integrated PC Vulkan/EGL XR path. | Dedicated verified flavor, installation/assets, stereo/controller UI, comfort/session recovery and sustained thermal/refresh budgets; distribution decision. |

### AND task evidence and acceptance

- [x] **AND-01 — P1, integration: recover onboarding, controls and lifecycle work.**
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
  [Portrait and orientation ownership](platform-recovery-review.md#android-portrait-thumb-deck-and-orientation-ownership-8-november-2026)
  add section 13.6's thumb deck and stop SDL's window creation from replacing
  the landscape lock with `FULL_USER`; portrait is an Interface setup choice.
  Native/JVM checks and a silent emulator probe (`--mode rotate`) pass a
  landscape-portrait-landscape turn mid-match with movement, fire and save/load.
  Physical portrait play and the per-orientation layout editor remain.
  The [touch layout editor, suspension save, network resume and Quest flavor checks](platform-recovery-review.md#android-touch-layout-editor-suspension-save-network-resume-and-quest-flavor-checks-9-november-2026)
  close the remaining implementation. Section 13.6's editor moves, resizes,
  fades and hides each control per orientation, reachable from setup and the
  game's Settings > Controls; single player is saved as `Suspended` when it
  leaves the foreground, so CONTINUE survives process death; a multiplayer
  client reconnects after a stay the server did not survive; and Android
  audio now resumes in multiplayer, where the session's resume never ran.
  `verify_android_apk.py --variant quest` and `-Dandroid_headset=true` give
  AND-05 its flavor-aware package checks. Native, JVM, C++/Java placement and
  retained-UI checks pass, and the emulator probes (`--mode lifecycle`,
  `--mode resume`) pass the layout record, background save, `am kill` and
  restore, and reconnect/keep-alive against a dedicated server.
  [On the S25](platform-recovery-review.md#s25-physical-checks-9-november-2026)
  the same checks pass on hardware: the edited landscape layout, portrait play
  with the thumb deck, the `Suspended` save, Android's background kill and an
  exact restore, a 57-second multiplayer stay that reconnects past the dropped
  channel's disconnects, and the editor driven by hand (which found and fixed
  taps pinning controls). Closed 9 November 2026. Human-finger portrait play
  and the raised weapon control over the armour readout move to AND-04;
  audible resume and Adreno/Mali breadth stay with AND-02; Quest device work
  is AND-05.
- [ ] **AND-02 — P1, qualification: real Adreno/Mali gameplay.** Cover the API floor
  and current Android, ES3 capability differences, stock SP/MP, saves, map/campaign
  transitions, materials/shadows/ETC2/EAC, cold/warm caches, audio and touch/gamepad/
  mouse/keyboard. Exercise focus, suspend, low memory, process death, rotation,
  density and cutouts. Retain thermal, memory, storage and sustained frame budgets.
  Include compressed input, pending decode, retained CPU PCM and uploaded OpenAL
  residency together. Qualify Android's own OpenAL provider/capabilities rather
  than inheriting desktop provider assumptions; depend on AUD-03/CORE-03 limits.
  [Device harness and S25 campaign](platform-recovery-review.md#android-device-qualification-s25-10-november-2026):
  `android_device_qualification.py` drives stock play through setup's
  debuggable-only `diagnostic_launch`, with no OS input. On the S25 the campaign
  passes cold and warm. It plays `hangar1` to `mcc_landing` through the levels'
  own end triggers, loads `airdefense1`, `convoy1` and `process1` fresh, and
  restores a `process1` save exactly. Five stages hold about 60 fps;
  `process1` is render-bound at 27 fps (AND-04 presets). Thermal status stays
  at 0 and PSS peaks at 2.83 GB. Android's OpenAL Soft through OpenSL keeps
  decoded PCM in OpenAL only, at most 253 MiB of sample data on `mcc_landing`.
  [`android-device-budgets.json`](android-device-budgets.json) holds the
  per-model limits. Still open: the phone's sustain and multiplayer phases
  (interrupted), audible and gamepad/mouse/keyboard checks, and Mali and
  API-floor hardware.
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
  The retained menus now keep their text and controls inside the window's
  [safe area](ui/retained-screens.md#the-windows-safe-area-1-november-2026)
  while their bands and backdrops run full-bleed (`LAY-016` partial), and
  the Remastered HUD's edge readouts follow the same
  [safe area](ui/remastered-hud.md#the-windows-safe-area-2-november-2026);
  cutout, gesture-bar, fold and multi-window qualification on devices
  remains. In portrait the Remastered HUD's bottom readouts now rise above the
  touch thumb deck while the overlay is active, and **Landscape and portrait**
  on the Interface page opts into turning the device; stock GUIs in portrait,
  the Touch HUD preset as a whole and readouts behind a raised weapon control
  remain; the S25 (19.5:9) shows the weapon control over the armour readout
  too, and human-finger portrait play moved here from AND-01.
  Mobile defaults, 10 November: the [hardware-range audit](ui/performance-presets.md#hardware-range-audit-10-november-2026)
  uses the S25's AND-02 results. A phone in the 12 GB class keeps the
  Performance ceiling with a 1080p-class scene (85% on the S25 instead of 50%),
  smaller phones a 720p-class scene, phones with at most 4 GiB the shadowless
  Low Power, and a detected cap stays at the preset's 60 on a 120 Hz panel.
  This is policy from one device: a Mali phone, a mid-range phone and a fresh
  first launch on the S25 still have to confirm it.
- [ ] **AND-05 — P2, integration and qualification: standalone Quest.** After
  VR-01/AND-01, qualify real installation, assets, stereo gameplay, controller/UI
  interaction, comfort, session recovery and sustained refresh/thermal budgets.
  Decide sideload/store packaging policy. Keep emulator/test-runtime results
  separate from headset support and x86_64 emulator builds separate from publication.
  Require AND-01's flavor-aware package verification and VR-06 lifecycle tests.

## iOS, iPadOS and visionOS

Sources: [rebelancap/openQ4-ios](https://github.com/rebelancap/openQ4-ios)
(GPL-3.0; release v1.1.0, 13 October 2026), [platform support](platform-support.md),
[macOS VM workflow](macos-vm-testing-workflow.md).

Requested 3 November 2026. An external port already runs openQ4 on iPhone and
iPad (iOS 16+) and on Apple Vision Pro (visionOS 2.5+), and it is published and
tested. It plays the story campaign with saves, the Arena Campaign and openQ4
multiplayer, with touch, gyro and controller input. On Vision Pro it adds a
window mode and a stereoscopic, world-locked 3D screen. It uses the Vulkan
renderer through a static MoltenVK, SDL3 and OpenAL Soft. Porting from it is
preferred to a fresh implementation. It is not part of this repository.

Its structure determines the work:
- It pins openQ4 `v0.14.000` (`1621aa21`), the withdrawn release, in
  `UPSTREAM.pin`, and keeps upstream pristine.
- It applies 29 engine patches, two game patches and four SDL patches with
  `patch --fuzz=0`. The largest are the Vulkan backend's live swapchain extent,
  timers and stereo present (0019, 213 KB) and the SDL3 backend's input, text,
  mouse, safe-area and screen changes (0005, 84 KB).
- An Objective-C/Swift app shell (`ios/shell`, `ios/shell-visionos`) adds a
  checksum-verified `q4base` import that fixes file-name case, settings, data-only
  mods, Siri intents, audio sessions, diagnostics and four localized languages.
- It builds with Xcode, xcodegen and CMake scripts. iOS loads no code outside
  the signed app, so the renderer and both game modules link statically.

Every item follows the project rules: Meson stays the primary build, SDL3 stays
the platform layer, game-library changes land in `src/game` and `src/mpgame`,
displayed text goes through the language tables, and the port's author is
credited in `AGENTS.md`, `README.md` and the release notes when code lands.

### IOS completion checkpoints

**Estimate: 5%; 0/6 tasks closed.** This credits the identified external port and
initial integration analysis only. No external build or user report is accepted
as evidence for an integrated openQ4 Apple-mobile package.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| IOS-01 — Partial decision work | Upstream pin, patch counts, shell/build structure and static-module constraints identified. | Consult the author; classify every patch/file; settle Meson/Xcode split, layout, static modules and distribution. |
| IOS-02 — Not started | Shared-fix candidates identified in the external port. | Rebase/integrate individually; test Windows/Linux/macOS/Android regressions; reconcile touch and saves with canonical implementations. |
| IOS-03 — Not started | External iPhone/iPad implementation available for reference. | Static device/simulator builds, shell/data import, lifecycle/audio, retained UI, twelve-language localization and measured presets. |
| IOS-04 — Not started | External visionOS window/stereo implementation available for reference. | Integrate shared stereo rendering, ornament/controller controls and declared interaction limits. |
| IOS-05 — Not started | No integrated-build physical-device evidence. | iPhone/iPad/Vision Pro campaigns, saves/portable cross-loads, MP, input/audio, thermal/memory and comfort; exact contributor build identities. |
| IOS-06 — Not started; optional P3 | No integrated release package. | Distribution decision, signing/IPA workflow, licensing, installation guides and evidence-earned support tier. |

### IOS task evidence and acceptance

- [ ] **IOS-01 — P2, decision and integration plan: adopt the port.** Contact the
  port's author and agree how they take part. Inventory every overlay patch,
  shell file and build script, and class each one:
  - a platform-neutral fix that belongs on main for every platform (for
    example zip seek caching, OpenAL error latching, live swapchain extent and
    pipeline-cache checkpoints, missing BC texture support, `vid_restart` under
    a renderer module, in-world GUI touch, rumble);
  - an Apple platform layer;
  - port-only behaviour to drop or redesign.

  Map each patch to the current main, which has moved well past `v0.14.000`:
  portable saves (Q4-06), retained UI, the safe area and the renderer modules.
  Record the decisions:
  - **Build.** Use Meson cross files for the engine, renderer and game
    libraries, with a generated Xcode app target for the shell, or
    justify another split.
  - **Repository layout.** Place the shell and Apple assets under
    `mobile/apple/` (or as `docs/dev/source-layout.md` decides) and the SDL
    patches under `subprojects/`.
  - **Static modules.** Decide how static linking coexists with the dynamic
    game-module and renderer-module contract.
  - **Distribution.** Choose sideloaded IPAs or a store submission. The game
    modules stay under the Quake 4 SDK EULA, so distribution is free and
    non-commercial.

  Gate: the inventory and decisions are documented, and the author has been
  consulted.
- [ ] **IOS-02 — P2, integration: shared engine fixes first.** Land the
  platform-neutral patches from IOS-01 one by one, rebased onto main. Each needs
  its own test or reproduction, and each must show that Windows, Linux, macOS
  and Android are unchanged or improved. In-world GUI touch and the touch-look
  model should serve Android too (AND-01, AND-04), not become a second
  implementation. Reconcile the port's save handling (its payload-v3 GUI shapes,
  stamp and refusal patches) with Q4-05, Q4-06 and Q4-07 instead of carrying a
  separate reader.
- [ ] **IOS-03 — P2, implementation: iOS and iPadOS client.**
  - Build the engine, the Vulkan renderer and both game modules statically for
    `arm64` device and simulator lanes, against MoltenVK, SDL3 and OpenAL Soft
    through Meson subprojects.
  - Port the app shell: display-link main loop, onboarding and `q4base`
    import with the official 1.4.2 checksum table, settings, data-only mods,
    audio-session interruptions, lifecycle and backgrounding.
  - Bring over the touch controls, gyro aim and controller defaults.
  - Use the retained UI and its safe area rather than port-specific menus.
  - Localize every shell string through the project's language tables, in all
    twelve languages, not only the port's four.
  - Make the quality tier a mobile preset chosen from device results.

  The macOS CI runner builds both lanes unsigned, and the simulator boots to
  gameplay with retail assets where licensing allows.
- [ ] **IOS-04 — P2, implementation: visionOS window and 3D mode.** Port the
  window mode, ornament controls and the stereoscopic world-locked screen. Its
  per-eye rendering, foveation, comfort and depth settings and controller input
  carry over. Share the stereo eye in the render front end and the stereo
  present path with the VR work (VR-01 and VR-06) rather than adding a second
  stereo path. Declare what the 3D mode does not support, such as menu input.
- [ ] **IOS-05 — P2, qualification: Apple devices.** Use real iPhone,
  iPad (60 Hz and ProMotion) and Apple Vision Pro hardware. Cover:
  - the SP campaign, Awakening and the Arena;
  - save and load, including cross-loading portable saves with desktop and
    Android (Q4-06; both targets are 64-bit little-endian);
  - multiplayer with desktop peers;
  - touch, gyro, controllers and rumble;
  - audio interruptions, backgrounding, memory and thermal budgets, storage,
    and the 3D mode's comfort and frame pacing.

  Bind each report to the exact build hashes under the existing evidence
  policy. This project has no Apple hardware, so accept contributor evidence
  only on those terms. The port's existing user reports count as background,
  not as qualification of the integrated build.
- [ ] **IOS-06 — P3, release: packages and player documentation.** Add signed
  IPA artifacts for iOS and visionOS to the manual release workflow under
  IOS-01's distribution decision. Cover licences and notices, player
  installation and data-import guides, and release notes. Then list the
  platforms in [platform support](platform-support.md) at the tier their
  evidence earns, and add a `README.md` bullet once players can install the
  integrated build.

## New user interface

The [visual specification](ui-visual-design.md),
[product register](ui/product-requirements.md),
[machine-readable requirements](ui/product-requirements.json) and
[migration manifest](ui/migration-manifest.json) retain the full product contract.
This roadmap adopts all **312 active requirements, 358 current source/include-bound
migration variants and seven final gates**. UI-10 reconciled retail/base/Awakening
mounts while preserving the historical 271-entry manifest. The denominator follows
effective source changes; it is not a fixed completion target. Scope contradictions
remain explicitly paired with UI-05 and cannot grant migration acceptance.
Archiving their original plans does not remove requirements,
renumber IDs or accept a GUI. Stable `product:`/`original:` references retain their
archived provenance; new scheduling belongs here.

### UI completion checkpoints

**Estimate: 30%; 1/11 tasks closed.** The full product denominator includes
runtime/toolkit, editor, artwork, source-bound migration and platform acceptance,
not just the visible menu pages. The current [register](ui/product-requirements.json)
has **117 partial and 195 pending active requirements**, with none verified;
the [manifest](ui/migration-manifest.json) has **358 pending variants** and all
seven final gates remain pending.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| UI-01 — Partial | Settings hub, GAME/CONTROLS/AUDIO, Load Defaults, DISPLAY (with presets applied there) and Player Setup exist; current scaling/tall layouts and effect/readback increments recorded. | Complete settings transactions/failure/restart/capability matrix; real-font/state coverage; production SYSTEM editor round trip and device/platform acceptance. |
| UI-02 — Partial | Binding capture, contextual prompts, controller navigation, touch scrolling and right-stick detail/preview handling exist. | Production native IME wiring, shaping/fallback/graphemes, accessibility exposure; physical controller families, composition/focus/hotplug and locale/layout matrix. |
| UI-03 — Partial | Roster scrolling/identity guards, save-browser/details, Strogg confirmations, server focus/empty states and objective markers have scoped evidence. | Tourney/watch and Match Control flows, catalogs beyond 128, remaining target-identity/role operations, handoffs and actual task/failure completion. |
| UI-04 — Partial | Retained menus/HUDs and specific implant/vehicle/cinematic surfaces have implementation or captures. | Accept every required source/include variant's behavior and artwork; world terminals, script/timeline provenance, per-instance state and exactly-once commands. |
| UI-05 — Partial | Awakening scope clauses reconciled; fresh-profile hit-feedback defaults repaired without overwriting opt-ins. | Remaining artwork/default-policy exceptions, vector fidelity, motion and visual review; approve exceptions explicitly. |
| UI-06 — Partial | DocumentEdit/history/prepared-view foundations exist. | Production editor shell, hierarchy/properties/layout, vectors/masks, components, timelines/bindings, localization and recovery; real SYSTEM round trip for GATE-003/004. |
| UI-07 — Partial | Scoped runtime measurements, preset/device layout checks and loading repaint/mesh optimizations exist. | Title/System CPU budget failures, GPU cost, idle-machine/GLES checks, cold/long-document cases and 1,000-cycle resource qualification. |
| UI-08 — Partial prerequisites only | Working routes and refreshed inventory provide cutover inputs. | All seven gates; accepted behavior/artwork/editor/performance across required platforms; external-mod legacy policy and dedicated packaging. |
| UI-09 — Partial | Retained adapter/control foundations, generated images, stale-action guards and typed animated model scenes exist. | Complete controls/components and public adapter semantics, text queries/bindings, world output/input, timers/instances and save/restart integration. |
| UI-10 — Closed | Effective retail/base/Awakening inventory refreshed to 358 bound variants; stale acceptance invalidated with positive/negative controls. | Maintain the denominator after source/include changes; inventory closure does not accept a GUI. |
| UI-11 — Partial | Separate acceptance transition implemented and hardened; 24 fixtures cover artifact/review/path failures. | Separate review of changed tooling; commit/review current per-requirement and per-resource behavior/artwork evidence before accepting anything. |

Completion must advance through the existing dependencies:

1. **Finish the production vertical slice (UI-01/UI-06/UI-07/UI-09).**
   - Edit, undo, save, recover and reopen SYSTEM and a component/transition.
   - Run the packaged result in SP/MP and judge architecture and measured cost.
2. **Complete and accept the wider product (UI-02 through UI-05/UI-09).**
   - Prove toolkit semantics and input/accessibility before bulk corpus acceptance.
   - Bind each migrated resource to current sources and behavior/artwork evidence.
3. **Approve acceptance and cutover (UI-11/UI-08).**
   - Review the transition tooling separately; reject stale or incomplete evidence.
   - Close the seven gates and explicit legacy-mod/platform policies together.

### UI task evidence and acceptance

- [ ] **UI-01 — P1, implementation: complete production settings and essential
  pages.** GAME, CONTROLS, AUDIO, Load Defaults and Player Setup are implemented
  in the retained interface, with the bounded evidence below. Finish their full
  transaction and qualification scope. Preset and mixed-effect transactions
  apply since 10 November, when SYSTEM left its opt-in scope (below). Demonstrate real
  effect/readback, persistence, cancel/revert, failed Apply,
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
  SYSTEM page is on. Player Setup remained at this checkpoint; its later
  implementation/readback and layout evidence is recorded below.
  Progress, 28 October: the SETTINGS hub and Load Defaults work stranded in
  the earlier UI worktree is integrated in the main checkout. SETTINGS from
  the Strogg pause now opens the hub rather than its Load Game document;
  returning preserves the correct pause family. Native navigation/fallback
  coverage and current windowed engine captures qualify this bounded change;
  The SYSTEM/editor round trip and broader acceptance remain open; Player Setup's
  later implementation and bounded verification are recorded below.
  Later that day, the [model-scene foundation](ui/document-format.md#model-scenes)
  adds the real animated renderDef host needed by Player Setup: typed root/head/
  weapon scenes, per-part skins and bounded native targets. Native suites and
  GL/Vulkan gameplay pose captures pass, including Vulkan resource resets;
  late fixture precache warnings and broader lifecycle/visual gates remain.
  This foundation did not itself deliver Player Setup; the later page and its
  bounded verification are recorded below.
  Progress, 31 October: CONTROLS, GAME OPTIONS and AUDIO are now
  [sub-pages of the SETTINGS page](ui/retained-screens.md#the-settings-sub-pages-31-october-2026)
  in the main menu's construction (visual specification 1.29, `FLOW-068`),
  replacing the drafted full-screen panels: sections are tabs and no tab has
  more than two columns. The opt-in SYSTEM page followed the same day as the
  [DISPLAY sub-page](ui/retained-screens.md#the-system-page-as-a-settings-sub-page-31-october-2026)
  (GRAPHICS, SCREEN and IMAGE QUALITY tabs, generated by
  `tools/ui/retained_system.py`; visual specification 1.30), and the
  settings rows then learned to
  [follow the text size](ui/retained-screens.md#settings-rows-follow-the-text-size-31-october-2026)
  (visual specification 1.31).
  Portrait views of 3:4 and taller now stand each tab in one larger,
  scrolling column, and GAME OPTIONS' fine sliders take typed values
  ([portrait and typed numbers](ui/retained-screens.md#portrait-views-and-typed-numbers-31-october-2026)).
  The menu levels (title, pauses, SETTINGS, Single Player, Campaign and
  Multiplayer) then met the
  [text-scale contract](ui/retained-screens.md#menu-levels-follow-the-text-size-31-october-2026)
  too (visual specification 1.32, `LAY-004`); at that checkpoint Player Setup,
  the Mods dialog, Objectives and the list/form pages still kept their 100 %
  rows. The 2-3 November increments below extend their scaling.
  In views of 3:4 and taller the same menu levels now
  [stand in one column](ui/retained-screens.md#menu-levels-in-tall-views-31-october-2026)
  that uses the height (visual specification 1.33, `LAY-021`), and so do
  [the list pages](ui/retained-screens.md#list-pages-in-tall-views-31-october-2026)
  (Load and Save Game, Demos, Create Server and Join Game; visual
  specification 1.34) and
  [the remaining pages](ui/retained-screens.md#more-pages-in-tall-views-1-november-2026),
  the Arena page in
  [two blocks of its own](ui/retained-screens.md#the-arena-page-in-tall-views-1-november-2026)
  (visual specification 1.35). Menu levels and the fixed-growth pages then
  took their column in
  [every portrait view that holds it](ui/retained-screens.md#every-portrait-view-and-other-aspect-ratios-1-november-2026)
  (visual specification 1.36). On a touchscreen the menus' lists now
  [drag with the finger](ui/retained-screens.md#touch-in-the-menus-1-november-2026),
  a moved touch is no longer a tap and a finger leaves no cursor or hover
  behind; a press lights at touch-down, a released list keeps going as
  inertia and lights its rim at either end, a swipe across a tab strip
  changes tab and a 500 ms long-press opens a button's detail (`INP-014`
  partial; haptics and device qualification remain).
  Progress, 2 November: Player Setup has
  [edited in place](ui/retained-screens.md#player-setup-edits-in-place-2-november-2026)
  on the Multiplayer page since 28 October; an engine capture now confirms
  each change applies and reads back. Text size and tall views remained at that
  checkpoint and gained implementation in the 3 November increment below.
  The Multiplayer page also gained DEMOS, which opens the
  [Demos page as a sub-page](ui/retained-screens.md#demos-as-a-multiplayer-sub-page-2-november-2026)
  (`FLOW-050`); the title's plinth keeps MODS, UPDATES, CREDITS and EXIT.
  The same day the list pages' rows began to
  [follow the text size](ui/retained-screens.md#list-pages-follow-the-text-size-2-november-2026)
  (Load and Save Game, Demos, Join Game and Create Server's maps), with Load
  Game's and Demos' details stacked over a shrinking picture and their
  furniture at its size (`LAY-004` partial).
  Progress, 3 November: the
  [remaining pages follow the text size](ui/retained-screens.md#the-remaining-pages-follow-the-text-size-3-november-2026)
  too: Create Server's options and advanced dialog, Player Setup and Join
  Game's search stack each setting's value under its label in a scrolling
  column; Restart Level, the campaign's difficulty and chapters, the Mods
  dialog, the Objectives page, Join Game's status, empty state, details card
  and entry dialogs, the Arena (with its ledger, reset question and
  entrance) and Credits grow their content while furniture keeps its size
  (`LAY-004` partial). Player Setup gained its tall arrangement. Engine
  qualification of real fonts across every state and the native editor
  remain.
  The same day Campaign became the campaign choice with
  [NEW GAME and CHAPTERS as its fourth-level children](ui/retained-screens.md#chapters-as-a-level-of-its-own-3-november-2026)
  (`FLOW-050`): CHAPTERS is a page of its own through the same helper, and
  the difficulty page keeps only the difficulty, Turbo and START GAME. The
  classic campaign page is unchanged. Join Game gained
  [SPECTATE](ui/retained-screens.md#spectate-on-join-game-3-november-2026)
  beside JOIN SERVER and the favorite toggle; a press of the left stick
  spectates and of the right one toggles the favorite, their glyphs on the
  plates since the prompt bar has no room for them (`FLOW-050`, `INP-011`).
  The menu levels' sections (Single Player, Campaign, Multiplayer and
  SETTINGS) now
  [summarize themselves](ui/retained-screens.md#section-summaries-3-november-2026)
  in the content column while they are the subject, CAMPAIGN, ARENA, NEW
  GAME, CHAPTERS and CREATE SERVER over a levelshot, through a cardless detail
  definition (`FLOW-050`). Join Game's docked detail column for expanded
  layouts and the section layout remain.
  Progress, 5 November: Game Options > HUD gains
  [Opaque Menu Backing](ui/retained-screens.md#opaque-menu-backing-5-november-2026),
  the accessibility alternative to the softened backdrop, on the retained and
  stock pages in every shipped language.
  Progress, 10 November: the retained DISPLAY page is the default under
  `ui_retained`. A Performance Preset or Auto-Detect chosen there applies
  through the [combined executor](ui/settings-effect-execution.md#combined-executor):
  one journaled attempt with a device restart, a sound restart for a layout or
  EFX change and cold recovery in both directions. That emptied
  `RETAINED_SYSTEM_INCOMPLETE_SETTINGS`
  ([the gate](ui/system-resolution-rows.md#the-gate)). The checked image and
  audio results, a visible exclusive-fullscreen Apply, the editor round trip
  and platform acceptance remain open.
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
  Progress, 3 November: the [right stick](ui/input-routing.md#right-stick-3-november-2026)
  scrolls a retained menu's detail area (a scrolling area that holds no
  control: the pause's Objectives page, the scoreboard and roster cards and
  long dialog bodies) and turns Player Setup's preview, with its Rotation
  slider following (`INP-011` partial). Physical per-family stick runs, View's
  details beyond Join Game and the sticks' glyphs remain.
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
  Progress, 3 November: [player-target guards](ui/player-identity.md#mute-vote-and-admin-targets-3-november-2026)
  bind remote mute/unmute requests and retained Admin selections/actions to
  the selected connection, including authenticated rcon delivery. Classic
  menu player choices also retain their original identity. A kick vote
  aborts when its target disappears or is replaced, including during the
  passed-vote delay; a mixed vote applies none of its other changes.
  Vote requests before server admission, retained kick-choice drafts,
  Match Control rows and the wider device/platform matrix remain open.
  Progress, 4 November: [kick choice, draft and request identity](ui/player-identity.md#kick-choice-draft-and-request-identity-4-november-2026)
  binds the retained choice to its ordered connection catalog and freezes that
  catalog with queued selections. A departed target blocks the entire draft
  until explicitly selected again or cleared. Both kick-request transports
  carry the selected identity and reject stale requests before server admission.
  Match Control rows and broader remote/device/platform qualification remain.
  Progress, 5 November: the multiplayer cards' Players lists gained the held
  scoreboard's [marks](ui/multiplayer-menus.md#players-list-marks-and-team-refusals-5-november-2026):
  a skull with the row at half while dead or out of the Tourney, a BOT tag,
  and network-quality bars stepped by the game. French and Italian players now
  read the stock team-refusal messages in their language. Native card checks
  cover each mark, and a live OpenGL deathmatch with bots shows them in play;
  Vulkan and device qualification remain.
  Progress, 5 November: Kick and Ban on the Escape card's Admin page now
  [ask first](ui/multiplayer-menus.md#kick-and-ban-confirmations-5-november-2026)
  in the stock confirmation, naming the player; YES acts on exactly that
  player's captured connection. Native checks and a Vulkan listen-server
  capture that kicks a bot cover the flow.
  The Match page's lists now hold a
  [full server's rows](ui/multiplayer-menus.md#match-list-capacities-5-november-2026):
  64 team rows, 32 replacements, every proposal template and 32 pool maps,
  checked against Match Control's model at compile time. The model's 128
  team rows and history steps would put the Escape card past the runtime's
  16 MiB document limit, so those still hand off past a full server's.
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
  Progress, 9 November: the stock interface's player, multiplayer and vehicle HUD
  GUIs follow the Remastered layout outside Classic, with matching settings, the
  pickup merge, chat and standing, and save-compat revisions
  ([stock interface HUD layout](ui/legacy-hud-layout.md)). This does not migrate
  or accept a GUI. The next inventory refresh (UI-10) picks up the edited
  GUIs and their revisions. The same day the stock multiplayer HUD gained the
  team modes' panels and flags, Tourney's strip and message bar, and the kill
  feed ([multiplayer groups](ui/legacy-hud-layout.md#multiplayer-groups)).
  Progress, 10 November: the stock HUDs take the HUD scale and opacity, the
  multiplayer one gained the follow card, vote card and respawn prompt, and
  sizes and offsets were measured against the retained HUD in paired captures
  ([evidence](ui/legacy-hud-layout.md#evidence-10-november-2026)). The
  Awakening's on-foot HUD follows the layout too, from a definition generated
  from `hud.gui` that the engine reads in place of the expansion's script
  ([evidence](ui/legacy-hud-layout.md#the-awakening-10-november-2026)), and so
  do its dropship gun, MCC gun and speeder bike HUDs, generated from
  `vehicles/hud.gui`
  ([evidence](ui/legacy-hud-layout.md#the-awakenings-vehicles-10-november-2026)).
  Its GEV HUD, which no map uses, keeps the expansion's script.
  The stock interface's Single Player, Campaign, Arena and Demos pages stand in
  the stock page frame the retained pages share, its Multiplayer page lists the
  retained page's sections, and its menus answer the arrow keys
  ([stock interface menus](ui/legacy-menu-layout.md)). Both CONTROLS pages
  draw keys as glyphs and keep an action's controller buttons when a key is
  bound, and the stock menus step their focus as a page reads and scroll a
  settings page to it ([key glyphs and focus](ui/key-bind-glyphs.md)). This
  does not migrate or accept a GUI either.
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
  Progress, 4 November: [moving weapon scopes](ui/runtime-performance.md#moving-weapon-scopes-4-november-2026)
  retain local meshes for their faint animated furniture. At 1080p the isolated
  rail/nail scope CPU medians fall from 6.99/8.80 ms to 0.018/0.037 ms; continuous
  yaw, sweep, lock/unlock and resize tests bound compilation through 4K and
  portrait. Exact pixel-edge antialiasing remains on stationary reticles and
  focus masks. Whole-product GPU, idle-machine and resource-cycle gates remain.
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
  Progress, 3 November: the initializing screen repainted only about eight
  times a second with pauses of up to a second: a 30 Hz cap, a 10% repaint
  share that counted single slow frames in full, and loading without
  presentation hooks (the 1 s script compile). Every main-thread file read now
  offers a time-paced repaint, at up to 60 Hz within half of the time during
  the new 1.3 s entrance and a fifth after it (`FLOW-069`). Local vector meshes
  in a zoom keep their compilation while it is at most a sqrt(2) step finer
  than needed, the machined bands became solid annuli (101k to a few thousand
  vertices), the lettering holds still (a fade or group layer recompiled or
  recolored its 50k glow vertices) and the screen adds no atlas rasters, whose
  CPU raster stalled the frame. Windowed GL traces on a heavily loaded machine:
  12-22 frames, a 100-330 ms first frame and gaps up to 1.35 s before; 57-88
  frames, a 13-23 ms first frame, entrance frames of 3-19 ms and loading gaps
  of 50-150 ms after, apart from machine-wide stalls that reduced motion shows
  too. Repaints cost 0.4-0.7 s of a 2.5-3.5 s start; an idle-machine
  measurement and the GLES fallback remain.
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
  partial material-image operations, bounded scalar text-fit queries and an
  explicit world-output rejection. Map each
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
  Progress, 4 November: [public binding names and text fitting](ui/bindings.md#public-key-binding-refresh-4-november-2026)
  replace the two empty GUI adapter methods. CONTROLS declares its 49 binding
  lookups and uses the public localized refresh, preserving capture overrides.
  Text queries use the current content width and shared font runs, return whole
  UTF-8 scalars and isolate each view's metrics. Native/adapter tests and windowed
  SP/OpenGL English and MP/Vulkan French gameplay checks pass, with all 14 final
  engine captures reviewed. Existing late-precache warnings remain. This does not complete shaping,
  legacy text escapes, live IME, world output, the editor or toolkit acceptance.
  Progress, 4 November: [choice catalog contexts](ui/player-identity.md#kick-choice-draft-and-request-identity-4-november-2026)
  extend the subject lifetime to choices independently of their selected value.
  Same-label catalog changes close the popup and cancel held or queued proposals,
  including ABA changes and restoration. The production kick choice freezes that
  catalog through dispatch. Native and GL/Vulkan gameplay checks pass; full
  toolkit and device/platform acceptance remain open.
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

### Q4 completion checkpoints

**Estimate: 75%; 1/9 tasks closed.** Campaign entry/transition and targeted-state
probes are useful but do not establish full playthroughs. Portable v5 saves now
exist; their optional scope and platform gates remain separate from restoring
historical saves and recovering safely from damage.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| Q4-01 — Closed | Retail gap ledger, 165-path overlay audit and original-content SP/MP/render controls recorded. | Keep new reports classified; broader progression/effect/network acceptance belongs to its owning tasks. |
| Q4-02 — Partial | All 31 stock stage transitions with start-of-map save/load pass GL/Vulkan; fresh Turbo hangar lift progression passes with failing old-behavior control. | Ordered gameplay between triggers: AI, set pieces, bosses, vehicles, cinematics/terminals and checkpoints on supported paths. Already-stuck lift saves remain limited. |
| Q4-03 — Partial | Retail normal/tangent and initial-light questions settled; telemetry repaired; stock spark stop/restart/delete/save lifecycle passes both APIs. | Per-effect visual/audio parity, collision/child effects/trails, subviews, high refresh and full save/demo lifecycle. |
| Q4-04 — Partial | Thirteen-map integration; targeted freeze, speeder, boss, corpse-pin, isolation and occupied turret/walker save checks; bounded M01/M02 repairs recorded. | All thirteen ordered playthroughs; complete boss kills, walker repair/unlock/demolition, squad/extraction routes, sustained turret effects and actual Awakening-to-Arena switch. |
| Q4-05 — Partial | Recovery UI and object/handle/reference/animation/AF/event cleanup hardened; corruption/reload and historical-save regressions pass scoped tests. | Broader owning graphs and interruption boundaries across target platforms; maintain valid saves and explicit refusal without inventing header-only recovery. |
| Q4-06 — Partial; optional P3 | Raw inventory and field codecs restored; v5 portable stamp/encoding implemented; Windows/Linux bidirectional stock/Awakening cross-loads pass. | Confirm outstanding CI/Apple codec runs; macOS and another ARM64 runtime cross-load/resave directions; publish player migration guidance with accepted scope. |
| Q4-07 — Partial | Original-writer Windows corpus substantially qualified; v2 stamp reporting repaired; exact HiDef fixture investigated. | Resolve #123 against the reporter's actual GUI/content; other ABIs; recover/reproduce absent stage-two artifacts before claiming independently reviewable acceptance. |
| Q4-08 — Partial | Durable commit/recovery implemented; 56 process-interruption points, locked-file failure and NTFS/ext4/WSL 9p checks recorded. | APFS result, removable/network filesystems and actual power-cut behavior; retain measured CRC/sync costs and last-valid-save preservation. |
| Q4-09 — Partial | Render demo v11: one recording spans levels, save loads, death reloads and restarts as self-contained segments; gameplay-only frames; level loads, continuous timeline and level markers in playback; campaign-aware library and gate; whole retained HUD and in-world screens recorded. Render demo v12 predicts animated-entity joints exactly, a quarter smaller. Windows checks pass with stock and Awakening content: GL recording, GL and Vulkan playback. | Vulkan recording, Linux and macOS, a long multi-level session with seek timing and file size, the 2D, sound and effect records that now dominate the size, the masked retained HUD groups that replay unmasked, and local sounds already playing at a segment start. |

Completion substeps must keep three forms of evidence distinct:

1. **Campaign behavior:** ordered progression and actual combat, then targeted
   saves in unique states and campaign/mode isolation. Map loading is only entry.
2. **Save correctness:** original writers and corruption/recovery graphs, then
   portable cross-platform round trips; restamped fixtures are not original writers.
3. **Parity and durability:** matched render-target/audio comparisons and physical
   filesystem/device tests. A successful scripted trigger or process kill does not
   prove a full scene or power-loss outcome.

### Q4 task evidence and acceptance

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
  Progress, 2 November: a hidden [campaign transition chain](retail-baseline-ledger.md#campaign-transition-chain-2-november-2026)
  saves, restores and fires each map's own end-level trigger across all 31
  stock stages, airdefense1 to core2's end of game, on OpenGL and Vulkan with
  retail assets.
  It passes after fixing a save that could not be loaded after a level change
  (radio-chatter reference, see Q4-05), and its 96 warning kinds are classified.
  This covers level changes, persistent state and save/restore at map start, not
  the gameplay between triggers; full playthroughs on each render path remain.
  Progress, 4 November: the [fresh Hangar Perimeter lift sequence](retail-baseline-ledger.md#hangar-perimeter-lift-4-november-2026)
  passes in Turbo mode on OpenGL and Vulkan: all three marines board, the lift
  rises fully and Tech Li leaves scripted control and reaches his exit tether.
  An isolated module with the old Turbo mover behaviour reproduces his idle
  stall at the lift's top. This closes the fresh-sequence check for #184/#198;
  it does not repair already-stuck saves or qualify the full map/campaign.
- [ ] **Q4-03 — P1, investigation and qualification: remaining BSE/effect parity.**
  Resolve initial-light current-versus-spawn transform and rotated model-particle
  normal/tangent questions from the October audit. Qualify light/sound/decal,
  trails, child effects, collision, stops/deletes, owner changes, subviews,
  high-refresh and demo/save lifecycle. Preserve intentional safety and
  presentation differences; BSE stays in-tree and linked into the client.
  Progress, 2 November: both audit questions are settled from retail
  `Quake4.exe`. Retail `rvModelParticle::Render` (0x101c8d90) and
  `rvPrimBatch::TransformVerts` transform only vertex positions and keep model
  normals and tangents. openQ4 rotated them, a leftover of the ETQW-derived
  source, and now matches retail, pinned by `bse_retail_parity.py`. Lit stages
  rebuild normals and tangents anyway, so only unlit stages that read normals
  can change. Retail `rvLightParticle::InitLight` (0x101bf190) places a new
  light at the effect's original origin and axis plus its current-frame offset,
  but `rvBSE::Service` (0x101b9f80) runs `PresentLight` with the current
  transform in the same call, so the first placement never renders. openQ4's
  current-transform placement matches what retail renders and is kept. The
  runtime qualification list above remains open.
  Progress, 3 November: a hidden effect lifecycle probe on `game/airdefense1`
  (OpenGL) samples `bse_speeds` through save, restore and demo playback. The
  map's ambient effects come back after a load: about 103 active before the
  save, 82 on the first restored frame and 101 six seconds later. The game
  saved and restored right after firing, with no errors. A 16 MB
  render demo recorded over the same scene plays to its end. This is lifecycle
  evidence only. The particle and trace counters stay at 0, firing did not raise
  the active count measurably, and BSE prints no counters during render-demo
  playback. Per-effect behaviour and retail comparison remain open.
  Later on 3 November: the zero counters were traced to missing instrumentation.
  Particle units, collision queries and serviced segments now update
  `bse_speeds`; unmeasured texel coverage is no longer printed as zero.
  The [stock spark lifecycle probe](retail-baseline-ledger.md#effect-lifecycle-telemetry-3-november-2026)
  passes start, stop, restart, owner deletion and saved-loop restoration on both
  OpenGL and Vulkan, including nonzero particle/trace activity and quiet frames
  after stop/delete. Immediate resaves contain no unresolved references. This is
  scoped lifecycle evidence, not visual/audio parity or the whole list above.
- [ ] **Q4-04 — P1, qualification: Awakening campaign closure.** Play all thirteen
  maps and restore saves during unique weapon/status/vehicle/boss states; qualify
  freeze persistence, damage gates, animation speed, and turret effects. Recheck
  transient corpse pin loss on restore as a bounded limitation. Verify base/Arena/
  MP isolation after restart. Keep the completed classes, gameplay and turret work;
  no expansion MP module or resurrection of obsolete companion-layer architecture.
  M05/M06 encounter work, 4 November: the Pain Lord's shield phases, final
  processing window and cleanup, MCC arrival continuity, and cannon/fighter
  lifecycle are reviewed in the [mission audit](awakening-m05-m06.md). A second
  pass fixes normal callback cancellation, escape-teleporter shutdown/re-entry
  and the service-lift lamp, with versioned scripts preserving v1 saves. A third
  pass addresses the blocked medic approach, unused freight panel and incomplete
  media references, plus a delayed teleporter activation race in invasion v3;
  v1/v2 script statements remain unchanged.
  Focused encounter and restore evidence belongs there; full traversal remains
  part of this gate.
  Progress, 2 November: freeze persistence passes one save/restore case. On
  `game/m01_stranarus_trench2` a spawned Strogg marine hit by the freeze gun
  reports "frozen solid" at 5888 ms, the game saves and restores, the marine
  chooses no action afterwards, and it shatters at exactly 9888 ms, the 4000 ms
  shatter delay measured from its saved freeze time (hidden, input-free,
  `fs_awakeningpath` pointing at the Awakening content). Vehicle and boss
  probes need the maps' own entry sequences: the developer `script` command
  cannot be used before saving, because restore recompiles its "console"
  source name and fails, as retail's restore would. The other unique states,
  corpse-pin loss and base/Arena/MP isolation remain to qualify.
  Progress, 3 November (hidden, input-free; saves reloaded in a fresh launch):
  - **Speeder.** `game/m07_race` starts the player on the speeder. A save made
    while driving reloads still on the bike at the saved spot, and driving goes
    on. The race locks the exit.
  - **Valkaryne.** Saved while she is docked and firing her death ray. After
    the reload, destroying the Makron sphere makes her undock, and she attacks
    with railgun and rockets.
  - **Pain Lord.** Saved once the fight is live. After the reload, head hits
    put him in pain and then stun, and both dude zones take damage. The kill
    was not confirmed.
  - **Pinned corpse.** A spike pins a killed Strogg marine on
    `game/m01_stranarus_trench2`, and the save with the pinned corpse loads
    without errors, unpinned as the bounded limitation describes.
  - **Isolation.** In an Awakening run, `q4xbase` is on the search path and 13
    `monster_q4x_*` definitions are loaded. After `campaignSelect quake4 start`
    in the same process, and in fresh base and multiplayer launches, there is
    no `q4xbase` path and no expansion definition.

  Progress, 4 November: the authored prison chaingun and trench walker now
  have repeatable entry through their stock activation path, without input or
  injected scripts. Both pass occupied save/load/resave/reload and a real
  diagnostic shot before and after restore on OpenGL and Vulkan. Seat ownership,
  pose, lock and HUD values survive each fresh process. The turret exits normally;
  the walker keeps the repair sequence's authored exit lock. The probe exposed
  division by zero in invulnerable/unshielded vehicle HUDs, now fixed in both
  game libraries. See [vehicle evidence and limits](retail-baseline-ledger.md#awakening-vehicle-saves-and-hud-fractions-4-november-2026).
  Walker repair completion, unlock and traversal, sustained turret bursts and
  visual/audio parity remain open; this checks one shot and its engine capture.
  Progress, 9 November (chaingun turret): the expansion's turret file redefined
  `damage_machinegun`, so the player's machinegun hit for 40 without
  `damage_bullet`'s bleeding, sounds or knockback; it is retail's again and the
  turret keeps 40 through its own damage. The turret gained barrel heat
  (`idVehicleTurretGun`: player gunners only, saved), crosshair convergence, a
  zoom, its parts' servo loop and openQ4's turret HUD (Classic
  `turret_hud.gui`, Remastered `remastered_chaingun_turret.q4ui`: the gunner's
  gauges and the heat). Hidden, input-free M01 and M04 runs measured the heat
  cycle (overheat after 5.3 s of fire, 2.0 s vent), the 40/15/20 damage, one
  hot-gun save/reload and both HUD layouts; a sustained wave defence and
  audio listening remain.
  Arena was covered only as `game_mp` on `baseoq4`, not as a switch from the
  Awakening menu. Full playthroughs remain.
  M01/M02 scrutiny, 4 November: fire volumes now use projectile/touch collision
  while burning and clear it when quenched, including on restore; final freeze
  hits fully dim their lights. M02's repair-bot attachment, generator defence
  gate, objective timing and animation sequencing are corrected under a separate
  saved script identity. Original M02 saves retain the installed program.
  See [mission intent, findings and bounded runtime evidence](awakening-missions.md).
  Follow-up checkpointed game-input runs cover M01/M02 squad routing, both
  walker demolition defences and courtyard traversal, generator/download
  defence, normal Tank victory, extraction framing and M03's autosave. They
  exposed and verified repairs for restored flame duplication, corpse and
  walker saves, Navajas's stair/courtyard route, Tak's lifting-door approach,
  the repeated boss console and the extraction actor. God mode and ammunition
  refills exclude balance qualification; uninterrupted and other-platform
  campaign playthroughs remain part of this gate.

  **M01 interior dramatic-feedback pass (implemented, 4 November).** Revision 3
  preserves Tak's repair face fill, exact dialogue sequence and 132.912-second
  interior timing. Fresh starts add ducked cabin/wind/rattle layers, positional
  foley, synchronized impact tiers, bounded camera response, damage lighting,
  joint-following tool effects and a completed descent sound transition.
  Original/v2 scripts remain immutable for saves. The localized shake control,
  reduced-flashing treatment and VR motion suppression are implemented.
  The [dropship audit and beat acceptance](m01-interior-feedback-audit.md#implemented-revision-3-4-november-2026)
  records the ordered plan's delivered scope and limits:

  1. **Audio continuity — implemented and checked.** Full production stereo/HRTF
     recordings trace ownership, ducking and natural score expiry. The user
     auditioned the repair passage and confirmed clear dialogue.
  2. **Coupled impact response — implemented and checked.** Three finite tiers
     compose once after camera sampling; no translation or accumulated offset.
     Shake-off and the VR setting yield zero added motion.
  3. **Damage and close action — implemented and reviewed.** Damage carries across
     sets; tool audio/glow/emission follow both repair intervals. Face lighting
     and squad sightlines remain readable; sparks stay small and mostly below
     the close-up crop.
  4. **Scene qualification — passed within documented scope.** Full GL/Vulkan
     playbacks reach gameplay with identical beat times. Eight phase saves and
     eight fresh-process skip cases pass, as do old-revision restore/resave,
     normal-time repair at a 144 FPS cap, reduced flashing, pause-clock tests
     and the settings control. Concurrent games prevent isolated performance
     qualification; physical speaker/headphone comparisons and headset comfort
     remain wider campaign qualification. Candidate release notes are updated.


  Progress, 4 November: [M03/M04 review](awakening-missions.md#m03-and-m04-review-4-november-2026)
  repairs the Falcon's shared cockpit screens, false initial lock, missing
  reticle coverage, ship-state routing, hull repair accounting and duplicate
  engine completion. Prison cellblock routing, maintenance-bot references,
  camera initialization and the missing bridge destination are corrected.
  A second pass ties missile volleys to the displayed magazine, exercises
  positive lock/restore transitions and separates reinforcement arrival from
  occupied marine cover. Script checks now report failures in release builds.
  A third pass replaces M04's constant global siren and computer alarm loops
  with quieter, localized reminders, preserving console shutoff and old saves.
  A fourth pass makes the security override one-time and prevents late camera
  destruction from starting an early or duplicate turret-room door hack.
  Fitted camera bounds remove the upper cell door overlap that blocked startup.
  Focused runtime and production-method probes cover these states; complete
  mission playthroughs and the other campaign acceptance gates remain open.

  Progress, 4 November (M07): the three race scripts now use versioned mission
  corrections for late clock callbacks, single stage transitions, time
  carryover, objective ordering, zero-clamped expiry and a completed HUD. Boost
  cancellation respects seat exit, movement disablement and destruction while
  preserving recharge. See the [M07 audit and qualification limits](awakening-m07.md).
  Bomber cleanup now targets each attack's own assembly; the two missing
  destruction callbacks use the existing authored sequence.
  A second review added v2 terminal-state guards: repeatable progress volumes
  cannot reduce a completed HUD, and failed gates disable vehicle controls and
  reject late completion callbacks. Original and v1 saved script layouts remain
  supported. Pipe machinery repeatability and next-leg HUD reset were checked
  without requiring changes.
  A third review adds v3 bomber ownership and interruption handling: one pilot
  per destroyed flyer, safe paired-shot cancellation, assembly and temporary
  aim cleanup, and completion of mover-waiting attack threads. Existing v1/v2 script
  programs remain unchanged for restore compatibility.
  Full driven traversal and race balance remain open under this item.

  M08/M09 scrutiny, 4 November: versioned mission corrections address the
  cooling-button race/re-entry, squad and cinematic handoffs, repair loops,
  light-taunt ranges and the finale's intro/docking boundary. Revision 2 also
  clears the first-thaw squad bottleneck, corrects Navajas's lower-console
  destination, completes cold-breath setup and prunes exact abandoned callbacks.
  Physical traversal now completes M08 and preserves its inventory through the
  natural M09 handoff; the one-time developer loadout at M08 entry still limits
  campaign balance claims. Second-shaft descent and the following cave tank
  now have physical-route evidence. M09 v3 repairs escort completion, the
  second-shaft Maiden targets and premature stair collapse; docked Valkaryne
  physics preserves its perches. The fresh v3 descent exposed a late beam aim
  lock; committing aim at windup start preserves damage while making the pipe
  crossing evasive. Moving and restored-windup controls survive; a standing
  control dies. Both lower-shaft enemies are now defeated, and natural recovery
  yields 77 HP/50 armour. Fresh/restored stair clearance passes. The physical
  route now clears the factory, both boss phases and timed extraction, boarding
  with 16 HP and reaching the ending after restoring that checkpoint. The shared
  melee path also guards AI-only defender state when attacking the player.
  These checkpointed runs retain the M08 developer-loadout limitation; they do
  not qualify uninterrupted campaign balance. The subsequent focused follow-up
  restores the three optional blast-door controls in M09 v4, serializes M08 v3
  freezer staging after the thaw handoff, and repairs the identified missing media
  using surviving installed assets. Earlier script
  revisions remain reproducible. Full traversal was not repeated; the focused
  follow-up evidence remains in the late-mission audit.
  Original script
  programs remain available to older saves. See the
  [late-mission audit and runtime limits](awakening-missions.md#m08-cryofac-and-m09-valkaryne--4-november-2026).
  Normal combat, complete traversal and timed extraction remain qualification
  requirements; focused script callbacks do not close this work item.
- [ ] **Q4-05 — P1, decision and implementation: save failure recovery.** Preserve
  approved v2/v3/v4 compatibility readers, the v5 portable reader and corruption
  checks. Explicit recovery after semantic object-restore failure is implemented
  below; broader graph/platform qualification remains. Unknown unstamped/build-1
  payloads fail preflight rather than being guessed; automatic header-only restart
  is intentionally excluded, while recognized build-1 releases retain their
  compatibility readers. Require interruption/corruption fixtures and no destruction
  of valid existing saves. Current partial repair preserves live entity identities
  when rejecting invalid
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
  Progress, 2 November: a failed-load teardown audit fixed
  [several restore paths](savegame-reliability.md#owner-lists-and-physics-links-during-failed-load-teardown)
  in both libraries without changing save bytes. Signal lists are bounded to the
  16 entries `Signal()` copies and hold only resolved functions. Vehicle parts
  and weapons, animator joint modifiers and brittle-fracture decals are appended
  as they are read instead of sized over unset pointers. Restored physics is
  attached to its own entity rather than a saved owner pointer. Restored client
  models and moveables now have list-node owners; without them the client-entity
  think loop could stop after a load and a later save could be misaligned. The audit's other
  reported hazards (unwinding errors, articulated-figure and animator owners,
  vehicle occupants, client-entity binds, raw entity numbers) are listed there
  and remain open. The Q4-02 chain also found that the typed spawn-reference
  check refused saves made after a level change: the static
  [radio-chatter reference](savegame-reliability.md#typed-spawn-references-during-restore)
  carried the previous map's spawn id into the next. It is now cleared with its
  chatter and restored only when it names one, so those saves load again.
  Progress, 3 November: the audit's [remaining hazards](savegame-reliability.md#owner-lists-and-physics-links-during-failed-load-teardown)
  are closed without changing save bytes. A refused load no longer runs partly
  restored script or vehicle exit code during teardown, and a full event queue
  no longer raises a second error there. Articulated figures and animators keep
  their restoring entity as owner. Constraint box indexes, vehicle positions,
  client-entity bindings, influence-target entity numbers, burning-joint counts
  and unregistered objects' numbers are checked. Valid saves are unaffected:
  all 11 release fixtures (0.9 to 0.13) restore, and the campaign chain passes
  on OpenGL. Later on 3 November, the identified
  [restore allocation leaks](savegame-reliability.md#allocations-during-interrupted-restores)
  and the [immediate-resave defect](savegame-reliability.md#saving-before-the-first-frame-after-a-load)
  were fixed in both libraries without changing save bytes. Embedded physics
  fixups complete inside the restore transaction instead of putting unsaveable
  targets on the event queue. Dictionaries and trace records have scoped owners,
  clip-model replacement is transactional, and partially read clip-model lists
  are safe to destroy. Native read-failure and mutation controls pass on Windows
  and Linux; all 50 Windows save/restore tests pass. On OpenGL and Vulkan the
  immediate resave now reloads into gameplay with zero unresolved references.
  Broader owning-graph/partial-constructor and platform qualification remains open.
  Progress, 4 November: [physics ownership now precedes every embedded field
  read](savegame-reliability.md#physics-and-vehicle-ownership-at-interrupted-field-boundaries),
  closing the interruption window before the earlier re-attachment. Partial
  contact lists cannot expose unset entity indexes to destruction. Vehicle
  part/weapon allocations remain owned during failed reads, and missing parents
  or invalid seat indexes are refused. Both game libraries pass Windows/Linux
  native fixtures and 24 negative controls; all 52 Windows save/restore tests
  pass. CRC-valid OpenGL/Vulkan interruption probes return to recovery and reload
  the healthy save, and all 11 existing 0.9–0.13 fixtures restore with their input
  files unchanged. Broader owning graphs and other platforms remain open.
  Further progress, 4 November: [actor animation and IK ownership, plus
  interrupted state-call allocations](savegame-reliability.md#actor-animation-ownership-and-interrupted-state-calls)
  now reject inconsistent owners and missing active resources without publishing
  unsafe state. Inactive IK and saved layouts are preserved. Both libraries pass
  Windows/Linux field-failure fixtures and 18 negative controls per platform;
  the focused Windows suite passes 54/54. Eight CRC-valid corruption cases per
  renderer recover to Load Game on OpenGL/Vulkan and reload a healthy save.
  All 11 existing 0.9–0.13 release fixtures also restore and run gameplay on
  OpenGL. Every input save remains unchanged.
  Broader owning graphs and other platforms remain open.
  Articulated-figure follow-up, 4 November: [required reconstruction and saved
  structure](savegame-reliability.md#articulated-figure-reconstruction-and-saved-structure)
  now validate figure ownership, loaded/active state, body/joint names and joint
  types in both libraries. Missing required AF resources enter recovery;
  optional inactive setup failures remain valid. Windows/Linux fixtures reject
  16 deliberate regressions per platform; the focused Windows suite passes 58/58.
  Seven CRC-valid damaged-ragdoll cases per renderer recover and permit a healthy
  reload on OpenGL/Vulkan. Healthy body poses survive frozen restore, the resave
  reloads, and all 11 existing 0.9–0.13 fixtures still restore into gameplay.
  This closes the identified AF boundaries, not the broader graph/platform gate.
  Event-queue follow-up, 4 November: [queued callbacks and legacy argument
  ownership](savegame-reliability.md#event-queues-and-interrupted-legacy-arguments)
  now reject unsupported target callbacks and descending event times, and free
  temporary legacy buffers after interrupted reads. Both game libraries pass
  Windows/Linux read/allocation-failure fixtures and eight negative controls per
  platform; the focused Windows suite passes 60/60. Save layouts are unchanged.
  Seven CRC-valid damaged-event cases per renderer recover to Load Game and
  reload the healthy save on OpenGL/Vulkan, with all input files unchanged.
  All 11 existing 0.9–0.13 fixtures still restore into gameplay. Broader owning
  graphs and other target platforms remain open.
  Legacy-argument follow-up, 4 November: [bounded strings and restored trace
  resources](savegame-reliability.md#legacy-event-strings-and-trace-resources)
  now prevent unterminated legacy strings and obsolete material/type addresses
  from reaching callbacks or an immediate resave. Both libraries pass native
  Windows/Linux field-failure and lookup-failure fixtures, including 18 negative
  controls per platform. The old raw format falls back to the material's base
  surface type where no per-hit type was recorded; current typed traces retain
  their saved type. Save layouts are unchanged. The focused Windows suite passes
  60/60; three damaged-record cases recover and reload healthy gameplay on each
  of OpenGL/Vulkan. A healthy legacy trace survives immediate resave on OpenGL
  and reload on Vulkan with valid typed material references.
  All 11 existing 0.9–0.13 fixtures still restore into gameplay. The broader
  owning-graph and platform gate remains open.
- [ ] **Q4-06 — P3, design and implementation: portable saves.** Decision,
  3 November 2026: build a portable save format, so that a save written on one
  supported platform loads on the others.
  - **Scope.** Every supported, preview and experimental target is 64-bit
    little-endian: Windows x64 and ARM64, Linux x64 and ARM64, macOS arm64, and
    Android arm64 including Quest. The portable revision is defined for that
    family only; 32-bit and big-endian builds keep refusing it.
  - **Existing saves.** `raw1` saves keep loading, through the current decoders,
    only on the ABI that wrote them.
  - **Out of scope.** Interchange with the proprietary retail game DLLs and their
    saves.

  The [technical contract](savegame-compatibility-policy.md#portable-revision)
  and the [block inventory](savegame-reliability.md#remaining-raw-serialization-raw1)
  are the supporting references. The stages run in order, and each closes on its
  own gate:
  1. **Inventory gate.** Restore an in-tree test that finds every native-layout
     read and write in both game trees, the engine GUI save code and script
     storage, and fails on any unclassified addition. The inventory that
     `savegame-reliability.md` described as locked has had no in-tree test since
     the source consolidation. The stage also confirms that the script's
     `intptr_t`-sized slots never carry a live value across a save point.
     Gate: the test runs in CI and assigns every block a class.
     Done, 3 November: `savegame_raw_inventory.py` is in the validation runner
     and both CI workflows; it has not run in CI yet. It finds 23 direct raw
     writes per game tree, not the 22 the old table claimed, and classifies
     every raw save call in both trees and the GUI. It fails on any unlisted,
     missing or recounted call, and on any change to the script type widths.
     Disassembled stock and Awakening scripts hold pointer slots only as
     interpreter-stack address temporaries, none of them live across a
     suspending call; see the [inventory](savegame-reliability.md#remaining-raw-serialization-raw1).
  2. **Field codecs.**
     - Write each block whose layout is up to the compiler as explicit
       fixed-width little-endian fields.
     - Flag bitfields are written one bit per flag in declaration order, packed
       as before. Trace models, joint modifiers, vehicle path targets and GUI
       interpolators are written field by field.
     - The path targets' entity pointers restore through the typed reference
       check.
     - Padding and stale script pointer slots are written as zero, so the
       encoder is deterministic.

     Gate: golden-byte native tests for each codec pass in CI on Windows MSVC,
     Linux GCC and Clang, macOS arm64 and Linux ARM64 (qemu), with mutation
     controls.
     First slice, 3 November: `src/idlib/SaveCodec.h` fixes the portable field
     order for these:
     - trace models;
     - joint modifiers;
     - articulated-figure pose modifiers;
     - joint quaternions and matrices;
     - interpolators, rebuilt through `Init` so no derived cache is saved;
     - vehicle path targets, with the entity reference left to the caller;
     - flag bitfields;
     - plain int and float arrays.

     Both game modules instantiate the codecs with the production types, so a
     renamed or retyped field breaks the build. `save_codec.py` compares
     golden bytes and requires eight source mutations to fail. It passes
     locally with MSVC, Clang on Windows and Linux, and GCC, and the meson
     test runs it on each CI platform.
     Second slice, 3 November: the blocks whose bytes need not change now go
     through explicit fields, with no version change and existing saves
     restoring unchanged:
     - the seven flag bitfields;
     - the joint, articulated-figure pose and pose-frame arrays;
     - the vehicle joint maps;
     - the hover-dampening angles;
     - the GUI window layout fields.

     All seven flag structs are one-bit `bool` fields. MSVC, GCC and Clang
     pack those identically on little-endian targets, so the plan's 32-bit
     masks gave way to the packed bits saves already hold. Each list must
     follow its declaration (`savegame_raw_inventory.py`), and the codec is
     checked against each compiler's raw layout. Direct raw writes fell from
     23 to 7 per game tree.
     Third slice, 3 November: the PDA minigame fields go through the typed GUI
     encoders, again with unchanged bytes. The script stack is now written with
     its address temporaries zeroed (`idProgram::ZeroPointerLocals`): those
     slots held stale raw pointers that nothing reads after a load. A store
     through a restored slot is now skipped rather than written through
     another process's address. `save_codec.py` also passes, with every
     mutation caught, as aarch64 Linux under qemu. That covers stage 2's
     locally reachable gate. The macOS arm64 run and the CI runs follow once
     the change is pushed.

     Trace models, joint modifiers, path targets and the GUI interpolator
     change bytes, so they move to stage 3 with payload version 5.
  3. **Wire revision.**
     - Payload version 5 carries a portable wire ABI stamp (`le64-portable1`),
       accepted from any family member.
     - The v2 to v4 readers keep their exact `raw1` matching.
     - Any other stamp is refused before map teardown with a localized player
       message.

     Gate: a v5 round trip on each CI platform. All historical fixtures still
     restore (`savegame_restore_matrix.py`). The schema-bump rules, contracts and
     compatibility policy are updated in the same change.
     Implemented, 3 November. Version 5 writes trace models, joint modifiers,
     vehicle path targets and GUI transition interpolators through
     `SaveCodec.h`. Path targets now restore their entity through the typed
     reference check. Saves are stamped `le64-portable1`, which every 64-bit
     little-endian build accepts; a build outside that family would stamp its
     own wire ABI. The v2 to v4 readers keep their raw branches and exact `raw1`
     match. A refused stamp shows a new notice, "made on another platform", in
     all twelve language tables. Direct raw writes fell from 7 to 2 per game
     tree, both script storage. Contracts, inventory, policy and reliability
     documents changed with it.
     Evidence on Windows x64:
     - the historical matrix again restores 14 of 17 saves, refusing the three
       unstamped ones, and six v4 saves from builds 1769 to 1958 restore
       through the retained readers (a seventh holds a script compiled at the
       console, which no build can restore);
     - fresh v5 saves round-trip on six stock maps and with a spawned convoy
       hover;
     - saves restamped `be64-portable1`, set to v4 with the portable stamp, or
       stamped for Windows x86 are refused before teardown, the running map
       carries on, and the unaltered save loads;
     - the restore cleanup and recovery runtime probes pass on a v5 save.

     The changed sources also compile with GCC and Clang. Still open for the
     gate: CI has no game assets, so on the CI platforms only the codec golden
     tests run, and only after a push. The runtime round trip on Linux and
     macOS moves into stage 4, which sets up those runtimes anyway.
  4. **Cross-platform qualification.** Save on one platform, then restore,
     resave and reload on another, on stock and Awakening maps, in both
     directions. Start with Windows x64 and Linux x64 (WSL), then macOS arm64
     through the VM workflow and one ARM64 Linux or Android target.
     Gate: every direction restores into gameplay without errors, its resave
     restores again on the source platform, and both platforms report the same
     object table.
     Windows x64 and Linux x64 qualified, 3 November. `savegame_cross_load.py`
     runs one platform's leg, at fixed tics. Its frozen save records the restore
     before any time passes, and its resave, after 180 tics, travels back. Its
     `--self-test` runs in CI. The Linux side is a WSL Ubuntu release build of
     the same commit, using software GL under Xvfb.

     The first runs found three platform dependences in typed fields, all fixed:
     - The script program checksum covered source lines. Those number
       differently in CRLF and LF checkouts of openQ4's script overrides, so
       every cross-platform load quietly restarted the level. From v5 it covers
       code only.
     - GUI transitions named their target by a byte offset in the window
       class. v5 names it by a field index.
     - Retained GUI snapshots named their document by its source text, line
       endings included. That comparison now ignores CRLF against LF.

     Evidence:
     - Seven stock maps (`airdefense1`, `convoy1`, `convoy2b`, `hangar1`,
       `process1`, `storage1`, `walker`) and four Awakening maps
       (`m01_stranarus_trench1`, `m03_airassault`, `m05_bio`, `m08_cryofac`)
       pass in both directions, without errors.
     - Each resave restores on the source platform.
     - Both platforms' frozen saves of one input list the same object table,
       568 to 3114 objects.
     - The historical matrix still restores all 14 release saves and six v4
       saves from builds 1769 to 1958.

     Still open: macOS arm64 needs an Apple host, and this project has none.
     ARM64 Linux or Android needs hardware, or an emulated GL stack that WSL
     does not have. The stage stays open until one of each has run.
  5. **Release.** Player documentation on moving saves between platforms,
     release notes and the revised platform table. Gate: published with the
     evidence above.

  Q4-06 stays optional (P3) and never blocks shipping a supported configuration.
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
  Progress, 2 November: [no engine path omits](savegame-reliability.md#issue-123)
  the missing `mon4.gui` leaf. The retail file has no includes or conditionals,
  and both the 0.10.0 and current parsers keep every window. The reporter's log
  searches a loose HiDef folder ahead of the retail paks, so the fixture now
  waits on their copy of `mon4.gui` or a window-by-window comparison of the
  attachment. The cited `.tmp/stage-two/` evidence directories are gone from the
  checkout; their recorded hashes remain.
  Later on 3 November: the historical-matrix stamp parser was corrected for v2,
  which has no wire-ABI string. It had reported object-table bytes as an ABI.
  Bounded v2–v5 parser fixtures now run with its existing CI self-test; this is a
  reporting correction, not a change to historical save admission.
- [ ] **Q4-08 — P2, investigation and qualification: save cost and durability.**
  Measure large-save CRC/sync time and fault behavior across target filesystems.
  Directory synchronization is implemented as recorded below; actual power-cut
  behavior and the remaining filesystems still need qualification. Qualify partial
  writes, replace failures, process interruption and preservation of the last valid
  save independently from object-restore recovery in Q4-05.
  Progress, 2 November: the [guarantee](savegame-reliability.md#durability-guarantee)
  is defined and implemented. Every slot rename is a no-replace,
  write-through (Windows) or directory-synced (Linux, macOS) step, so a reported
  save survives an OS crash or power loss as far as the device acknowledges
  writes, and the commit order holds on disk. Qualification found and fixed a
  recovery defect: a crash while the payload was staged deleted the previous
  save's description. `savegame_durability.py` runs the production commit,
  recovery and CRC against real files, kills 56 child processes across every
  commit step and staging point, and fails a locked old payload on Windows.
  Every case recovers to one whole save on NTFS, Linux ext4 and the WSL 9p
  mount. Measured costs up to the 512 MiB cap are recorded; the durable commit
  adds about 1 ms on NTFS and 7 ms on ext4. Remaining: macOS APFS (the new
  native test runs there in CI), removable and network filesystems, and any
  power-cut test.
- [ ] **Q4-09 — P2, implementation and qualification: campaign demo recording and
  playback.** A render demo recorded in single-player must survive everything a
  campaign does between its levels and replay as the player saw it. Acceptance:
  one recording across level transitions, save loads, death reloads, difficulty
  restarts and quit-to-menu; recording armed at the title so a campaign records
  from its first frame; no menu, pause or loading-screen frames; playback that
  loads each level (with its loading screen), purges the previous level's media,
  keeps one timeline and offers each level start as a marker; a library entry that
  names the campaign, first level, level count and duration, refuses a demo from
  the other campaign before anything is torn down, and marks an interrupted
  recording as playable to its last complete frame; the HUD and in-world screens
  as the player saw them. Qualify on OpenGL and Vulkan, Windows and Linux, with
  stock and Awakening content and a session long enough to measure seek time and
  file size.
  Progress, 7 November 2026: render demo v11 implements the format and both ends
  ([architecture](multiview-demos.md#campaign-render-demos-v11-7-november-2026)).
  On the staged Windows GL client with stock content, a recording armed at the
  title spanned airdefense1, a change to airdefense2 and a save load back (3
  segments, 745 frames) and replayed all 745 frames; a death through the restart
  menu suspended the recording until the reload; `disconnect` finalized it; a killed
  recording played to its last complete frame; level markers, cross-level seeks,
  the library entry and the cross-campaign refusal behaved as specified; and
  playback frames matched live captures for the HUD values, icons, crosshair and an
  in-world terminal. Vulkan played the same recordings identically (all 745 frames
  of the three-level demo; the HUD capture matched). `campaign_demo_contract.py`
  pins the format and both ends.
  An Awakening recording across a trench1-to-trench2 transition (406 frames) found
  that render demos could not replay a local sound (music, narration, interface)
  at all; the sound reader now resolves that emitter, and the demo played in full
  and was refused in base Quake 4.
  Render demo v12 (7 November 2026) predicts animated entities' joints from the
  joints last recorded for the same handle and stores only the moved joints'
  residuals, exact on every platform and checksummed
  ([joint prediction](multiview-demos.md#joint-prediction-v12-7-november-2026)).
  Joints had been half of the compressed file. The same scripted airdefense1 to
  airdefense2 clip (500 frames) shrank from 20.4 MB to 15.4 MB (-24.5%, about
  0.92 MB per second); it replayed all 500 frames on GL and Vulkan with every
  record verified, the three-level v11 demo still played in full, and paused
  frames of the two recordings matched in view and pose.
  Remaining: Vulkan recording, Linux and macOS, a long session with measured seek
  times (rewinds replay from the start) and size, the 2D model, sound and effect
  records that now make up most of a recording, the retained HUD's masked
  groups, which replay without their masks, and local sounds already playing when
  a segment starts.

## Rigid-body and ragdoll physics

Sources: `src/game/physics/` and `src/mpgame/physics/` (`Physics_RigidBody`,
`Physics_AF`, `Clip`), `src/game/AF.cpp`, `src/game/AFEntity.cpp`, the `af_*` and
`rb_*` CVars in `gamesys/SysCvar.cpp`.

Requested 3 November 2026. The legacy simulation integrates rigid bodies with a
plain Euler step and solves articulated figures with an LCP per frame, without
contact persistence, warm starting, island sleeping or continuous collision. The
enhanced mode replaces that with a more stable and more realistic simulation that
is also cheaper to run. Every item honors three product rules:

1. **One option.** A single archived CVar, `g_enhancedPhysics` (default `0`
   until PHYS-06 promotes it), with one localized Settings row, turns the whole
   enhancement on. There are no separate player-facing sub-options; any tuning
   CVars are cheat-protected developer controls. `0` runs the legacy code path
   unchanged: the same code, results and save bytes as before this work, which
   a deterministic replay against the pre-change binary proves.
2. **Highly optimized.** On the declared reference workloads, enhanced mode costs
   no more aggregate physics CPU time than legacy, and its worst frame is no
   worse. Any exception is declared with its bound before promotion, never
   discovered afterwards.
3. **No behavior breakage.** Damage, splash and death pushes deliver the same
   momentum as legacy. Ragdolls must never fly across the map from a light hit,
   explode from interpenetration, jitter at rest or gain energy during contact
   resolution. Scripted set pieces, progression and authored AF/entity defs keep
   working unmodified with stock assets.

### PHYS completion checkpoints

**Estimate: 0%; 0/6 tasks closed.** The legacy physics engine, recent AF restore
hardening and presentation interpolation are not implementation of the proposed
enhanced solver. The product rules below are requirements, not current features.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PHYS-01 — Not started | Proposed workloads and preservation rules specified. | Record legacy CPU distributions, defect reproductions and impulse envelope; set budgets/tolerances and the CVar/localized UI contract. |
| PHYS-02 — Not started | Legacy rigid-body code available as baseline. | Semi-implicit integration, persistent contacts/warm starting, friction, island sleeping and selective swept collision; prove budgets and stable energy. |
| PHYS-03 — Not started | Legacy AF code/stock definitions available as baseline. | Warm-started constraint solver, stable limits/penetration recovery, authored self-collision/sleeping and animation-velocity handoff. |
| PHYS-04 — Not started | No accepted enhanced-mode behavior envelope or runtime evidence. | Executable impulse guards and telemetry; light/heavy hit tests; stock/Awakening actors, moveables and scripted progression without behavior breakage. |
| PHYS-05 — Not started | Existing save/network/demo/presentation contracts constrain integration. | Cross-mode saves, server-authoritative mode, demo parity, transient/versioned state and dedicated-safe interpolation. |
| PHYS-06 — Not started | Default remains legacy; no promotion evidence. | Windows/Linux and one ARM64 matched workloads, worst-frame/visual acceptance, explicit default decision and upgrade-preserved fallback. |

### PHYS task evidence and acceptance

- [ ] **PHYS-01 — P2, investigation and decision: legacy baseline and enhanced
  contract.** Measure legacy rigid-body and AF costs (`af_showTimings`,
  `rb_showTimings`, the CPU sampler) on fixed workloads: a combat-heavy SP scene
  with several ragdolls, an explosion clearing moveables and debris, an MP
  frag-heavy round with bots, and a scripted AF set piece. Inventory reproduced
  legacy defects (resting jitter, limbs stuck in or through walls, tunnelling of
  small fast bodies, joint stretching, sliding corpses, endless twitching) with
  saves or repros. Record the momentum each damage path applies today (direct
  hit, splash, death push, melee, vehicle impact, `g_corpseSink`) as the
  behavior envelope PHYS-04 must keep. Declare the per-workload CPU budgets,
  replay tolerance and the CVar/UI/localization contract. Close when the
  baseline and budgets are recorded in a physics reference doc.
- [ ] **PHYS-02 — P2, implementation: enhanced rigid bodies.** Behind
  `g_enhancedPhysics 1`: a semi-implicit integrator, persistent contact
  manifolds with warm-started impulse solving, rolling/spinning friction,
  stable resting stacks, island-based sleeping with a correct wake on any
  touch, push or script move, and swept collision only for small fast bodies
  that would otherwise tunnel. Share one implementation between the SP and MP
  game trees. Gate on the PHYS-01 rigid-body workloads meeting their CPU
  budgets, no tunnelling in the defect repros and no energy gain in resting or
  stacked cases.
- [ ] **PHYS-03 — P2, implementation: enhanced ragdolls and articulated
  figures.** Behind the same switch: a warm-started iterative constraint solver
  for AFs, stable cone/twist joint limits that cannot explode when violated,
  bounded penetration recovery that removes overlap without adding velocity,
  self-collision where the AF def asks for it, and settling to sleep. At death,
  the ragdoll inherits the animated pose's actual joint velocities instead of
  starting from rest plus an impulse, so bodies fall naturally from their
  motion. Keep reading stock AF defs as authored: masses, limits and contact
  friction keep their meaning, with no replacement assets. Gate on the PHYS-01
  ragdoll workloads meeting their CPU budgets and the defect repros resolved.
- [ ] **PHYS-04 — P1 for promotion, qualification: behavior preservation and
  the impulse guard.** Prove enhanced mode keeps the PHYS-01 envelope. Every
  external impulse delivers the same linear momentum as legacy, distributed
  over the bodies it hits by their masses. A per-hit velocity change is
  bounded relative to the struck body's mass and the legacy result. The
  solver may never raise a body's kinetic energy except through a declared
  external force. An executable test drives light hits (machinegun, melee,
  small splash, weak vehicle bumps) into resting and falling corpses and
  fails if any body leaves the legacy displacement envelope. Heavy hits
  (rockets, grenades, Dark Matter Gun, gibbing thresholds) stay within the
  declared tolerance. A runtime telemetry counter records clamps and peak
  body speeds. Cover AI ragdolls, player corpses in MP, hanging and
  constrained bodies, moveables, debris, and Awakening `q4xbase` content.
  Scripted AF set pieces, vehicles and trigger-driven moveables must complete
  their progression in the Q4-02 campaign chain with the switch on.
- [ ] **PHYS-05 — P1 for promotion, integration: saves, multiplayer, demos and
  presentation.** Saves load in either mode and survive switching between
  them. Enhanced-only state is either rebuilt on load or written in a
  versioned block that legacy reads ignore, coordinated with Q4-05/Q4-06. In
  multiplayer the server's value governs both networked physics and
  client-local corpses (published in serverinfo), so all peers and MVD
  playback simulate the same mode. A client's own setting never changes
  authoritative results, and changing it takes effect at map load. Active
  ragdoll and debris interpolation (HF) keeps working with the new solver's
  sample cadence. Dedicated servers run the same mode without effect or
  renderer dependencies.
- [ ] **PHYS-06 — P2, qualification and decision: promotion.** Repeat the
  PHYS-01 workloads with the switch on and off on Windows x64 and Linux x64,
  plus one ARM64 preview target, and record CPU distributions, worst frames and
  failures. Run the PHYS-04 envelope tests and in-game, windowed visual checks of
  combat, explosions and set pieces. Decide whether `g_enhancedPhysics`
  defaults to `1` for fresh profiles. Upgraded profiles keep a saved explicit
  choice, and `0` remains a supported fallback. Update the capability matrix
  and release notes with the decision.

## Multiplayer bots Arena and demos

Sources: [competitive closure audit](competitive-match-reference-audit.md),
[bot implementation](mp-bots.md), [Arena ceremony](arena-campaign-ceremony.md),
[demo architecture](multiview-demos.md), [MVD qualification](mvd-qualification-2026-09-07.md),
[liquid implementation history](archive/2026-10-18/plans/2026-08-07-liquid-support.md),
[human qualification checklists](mp-human-qualification.md).

Current delivery scope, 28 October: the user deferred real WAN and
cross-architecture peer qualification for now. MP-07/MP-08 retain those gates for
future support qualification; local implementation, simulation and current-host
checks continue. This deferral does not promote prediction/rewind defaults or
claim peer combinations that have not been tested.

Human gates, 5 November: the owner chose to run, in person, the gates that need
a person or physical hardware. These are keyboard/controller workflows, Arena pause
and audio endpoints, real swimming and effects by eye, weapon visuals, physical
networks and bot difficulty. They follow the
[human qualification checklists](mp-human-qualification.md), because automated
fixtures never synthesize input. Each such gate stays open until its checklist
result is recorded.

### MP completion checkpoints

**Estimate: 80%; 3/9 tasks closed.** Most systems work and have substantial local
evidence. MP-02's final long-match/catalog safety checks pass, with its timing
contention caveat accepted for closeout; controlled timing remains unqualified.
Simulated networking does not close physical peers. The 28 October
WAN/cross-architecture deferral remains in effect and does not promote
prediction/rewind defaults.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| MP-01 — Partial | Roster/identity/vote fixes; eight live edge scenarios, failed-map retry, writer fault controls, live team-forfeit side ballots and post-commit map-load release pass scoped checks; side ballots pass on OpenGL and Vulkan. | Physical keyboard/controller flows ([checklist](mp-human-qualification.md#mp-01-competitive-match-control-by-hand)) and other platforms. |
| MP-02 — Closed with accepted timing caveat | Final `f6aa481e6` four-map long matches and all 33 catalog variants pass death/duration checks with zero navigation deaths after strict policy-2 assessment; targeted recovery, traffic and Windows/Linux controls pass. | CTF3 timing meets numerical limits but SP contention invalidates controlled qualification; no uncontended repeat was completed. Owner accepted this caveat for closeout; failed report retained, other maps/platforms unqualified. |
| MP-03 — Closed | Trench jumps and debug-pool defects fixed; route-cost, collision, graph/cache and ladder/lift subjects have recorded dispositions. | Custom lifts remain unsupported; final safety evidence and the accepted timing-contention caveat are recorded under completed MP-02. |
| MP-04 — Partial | Arena audio pause/result persistence, in-place restart and ceremony disconnect/quit preservation checked. | Physical navigation and endpoint loss/return ([checklist](mp-human-qualification.md#mp-04-arena-by-hand)); retain failure-path and UI/audio dependencies. |
| MP-05 — Closed at selected scope | Seek repairs, quota/retention policy and MVD 1.3 markers/keyframes/late-join identity/autotracking pass selected runtime checks. | Reverse scrub, timedemo, AVI, thumbnails and named list remain deferred; 1.3 Vulkan/Linux/macOS playback breadth belongs to release qualification. |
| MP-06 — Partial | Static-liquid compiler output, bot swimming, remote air/HUD/demo state, muffle and projectile crossings checked. | Real player swimming, wade sound and a by-eye pass at play distance ([checklist](mp-human-qualification.md#mp-06-swimming-and-liquid-effects-by-eye)); captured splash/bubble presentation passes after the slime/lava splash fix. Swimming monsters, new animations and moving volumes remain deferred. |
| MP-07 — Partial | Coupled pose-age/rewind, signed timing controller, impairment simulation and weapon geometry fixes tested; defaults remain off. | Firing player's same-fire-frame visuals and remaining peer transitions ([checklist](mp-human-qualification.md#mp-07-weapon-visuals-under-prediction)); remote weapon play passes from spectator captures after the casual follow fix; real WAN and default promotion only when selected and qualified. |
| MP-08 — Partial | Local IPv4/IPv6 pure/discovery/reconnect/content checks, family fallback and fragment repair pass; Windows/Linux peer evidence exists. | Physical MTU/multi-record DNS, cross-machine load and the elevated retail-binary run ([checklist](mp-human-qualification.md#mp-08-real-network-conditions)); deferred WAN/other-architecture peers. No interoperability claim. |
| MP-09 — Partial | Aim/turn/trigger fixes and five-level combat ladder measured over a round robin. | People verify skill 1 accessibility and skill 5 difficulty/fairness ([checklist](mp-human-qualification.md#mp-09-bot-difficulty-against-people)); retune from that evidence. |

Completion substeps:

- **Safety and skill:** retain MP-02's final safety evidence and accepted timing
  caveat; qualify human combat under MP-09.
- **Player workflows:** exercise real match/Arena/liquid controls and visual/audio
  outcomes, including interruption and slot reuse, rather than only command paths.
- **Peer breadth:** retain current local results; reopen deferred WAN/architecture
  qualification only within selected scope and preserve explicit unsupported cases.

### MP task evidence and acceptance

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
  Progress, 3 November: the eight live scenarios from that review now pass on
  one staged OpenGL build of `1e1f3e88a`, with reports bound to its binary
  hashes. The new `mp_match_edges_smoke.py` covers captain substitution both
  ways and timed invitation expiry. It also covers countdown cancellation by an
  unready player and by a captain's disconnect, once-only charging of duplicate
  and simultaneous timeouts, and a passed global abort ballot that executes
  after a mid-vote session change. Follow targets are released on disconnect
  and not inherited on slot reuse, and an invited spectator is refused cameras.
  `mp_series_smoke.py --missing-map-retry` rolls a failed series map load back
  to READY twice, then plays the retry. No product defect surfaced. Side
  proposals cannot be qualified: every Match Control creation template is
  global by contract, so no shipped surface creates a side ballot
  ([evidence and limits](competitive-match-reference-audit.md#live-closure-scenarios-2026-11-03)).
  Physical keyboard/controller workflows, side ballots, the post-commit
  map-load limit and the rest of the release order remain open.
  Follow-through, 3 November: the [artifact adapter fault-injection fixture](competitive-match-reference-audit.md#artifact-adapter-failure-boundaries-3-november-2026)
  executes all three production writers at every short-write failure boundary
  of a 257-byte payload, their capacity edges, failed sync/open/promotion/cleanup
  and invalid input. Twelve reversal controls fail. This qualifies adapter
  handling, not physical disk exhaustion, power loss or process interruption;
  the live and physical-workflow gates remain open.
  Progress, 5 November: side ballots and the post-commit map-load limit are
  closed ([evidence](competitive-match-reference-audit.md#side-ballots-and-post-commit-map-load-release-2026-11-05)).
  The owner chose a targeted template: **Forfeit | Side** is a team forfeit
  vote. Its scope and target are always the recipient's own side. The server
  accepts it only from an active member of that side while forfeit is legal,
  and a passed ballot runs the ordinary forfeit. `mp_match_side_ballot_smoke.py`
  passes live with three humans. Only Strogg sees the Strogg ballot, the
  captain's NO fails it, and a later YES ends the match as a Strogg forfeit won
  by Marine. A committed series map that never starts a session, after a load
  failure past the preflight or on recovery onto another map, is now released
  as an aborted, artifact-free attempt that Advance replays. Before this, every
  later session refused to bind. `mp_series_smoke.py --broken-map-retry` passes
  the whole path live. Five native reversal controls fail, and all 53 asset-free
  `mp_match_*` contracts pass. Remaining: the physical keyboard/controller
  workflows in the [human checklist](mp-human-qualification.md#mp-01-competitive-match-control-by-hand),
  and other renderers and platforms.
  Follow-through, 5 November: the side-ballot smoke also passes on Vulkan. Both
  ballots and the Strogg forfeit won by Marine match the OpenGL run, with clean
  exits ([evidence](competitive-match-reference-audit.md#side-ballots-and-post-commit-map-load-release-2026-11-05)).
  Other platforms and the physical workflows remain.
- [x] **MP-02 — P1, qualification and targeted fixes: bot traversal safety.** Rerun
  staged q4dm8/q4ctf3/q4ctf5/q4dz3 after the exact-state jump/unroutable-pad fixes,
  then the complete declared-map sweep including warmup (currently 33 variants).
  Separate enemy impulses from
  navigation/control failures and retain strict world-death gates. Qualify route
  generation/think budgets, objectives and real combat; source tests do not close
  the previously failing runtime safety sweep.
  **Closed, 4 November 2026, with the timing-contention caveat accepted by the
  project owner.** The archived `f6aa481e6` runtime passes all four long-match
  and all 33 catalog death/duration gates: 710 kills, 1,103 traversals, eight
  strict policy-2 impulse exclusions and zero navigation world deaths. All 24
  targeted hit cases, ten recovery cases, two-bot pad traffic and thirty native
  reversal controls on Windows/Linux pass. The fix is landed in main, with its
  build, staging and four CTF3 recovery checks also passing.
  The six-bot CTF3 timing run on Windows x64/OpenGL meets the selected numerical
  limits: 0.791 ms mean / 4.25 ms worst-window p95 / 744 ms navigation generation
  against 4 / 8 / 1,000 ms. **Controlled timing remains unqualified:** two SP
  processes overlapped the run, and no uncontended repeat was completed. The
  owner accepted this limitation for MP-02 closeout; the original failed report
  and contention monitor remain unchanged. These measurements do not qualify
  other maps or platforms. See the [closeout evidence](mp-bots.md#unpressed-ramp-flights-and-consumed-walking-routes-4-november-2026).
  **Historical investigation and qualification record:** the open-gate statements
  below describe earlier stages and are superseded by this closeout.
  Qualification correction: [exact armour and timer audit](mp-bots.md#exact-armour-and-timer-qualification-4-november-2026)
  finds that earlier causal replays omitted armour. Their exclusions below are
  historical, not final safety evidence. Policy version 2 restores post-hit and
  pre-hit armour and rejects incomplete logs. Both DM8 pad poses and the CTF5
  pad catch now pass isolated recovery; the ground-slide timer is corrected.
  The rocket-slide controller now passes its isolated runtime check. CTF7's
  low-health drop reproduces a lethal overshoot onto a lower neighbouring floor;
  the edge guard now prices that actual floor before permitting departure.
  Both CTF7 poses now pass isolated runtime checks. Audit logging and replay
  also preserve actual pre/post knockback timers, including refused timer
  resets. The `9a70f7b68` four-map long sweep has 15 raw deaths, 14 exact-state
  impulse exclusions and one recoverable DM8 rocket landing; DM8 also misses
  the minimum simulated duration. Recovery now distinguishes a hull-edge
  landing with a surviving brake from a momentary touch followed by a fatal
  slide. The `189c4b4a9` repeat meets all four duration gates but exposes two
  further CTF3 deaths. One reproduces a pad collision when a climb hop loses
  its approach reservation; the reservation now survives that hop. The other
  departure, renewed final-build long/catalog runs and controlled timing remain
  open. The `04e637ae0` repeat qualifies CTF3 directly and all six DM8 deaths
  under the exact-state impulse policy, but retains two CTF5 navigation deaths.
  One reproduces a ramp contact falsely accepted as a landing; recovery now
  requires actual grounding after the clipped collision frame, and its runtime
  replay survives. Off-route movement now guards its final command against
  unplanned transports; two fatal CTF5 reconstructions and four nearby approach
  checks survive. Live recovery also forecasts current player hulls, fixing a
  reproducible DM8 body deflection. The original moving-player sequences and
  older CTF3 missed-pad departure are not exact reproductions. CTF5/DeadZone
  repeats clear their duration shortfalls without waiving the original failed
  reports. The archived `04e637ae0` stage now qualifies all four duration-selected
  long runs and all 33 catalog variants under strict causal assessment. The
  subsequent ramp, off-route and player-contact corrections still need renewed
  long-match, complete-catalog and controlled-timing qualification.
  The `5bd8dff11` repeat meets all four long-run duration gates and leaves two
  unresolved deaths after exact-state replay; its 33-variant catalog clears
  death/duration assessment. [Wall-arrested recovery and settled braking](mp-bots.md#wall-arrested-landings-and-settled-braking-4-november-2026)
  fix the CTF5 controller's rejected safe coast and a delayed ground-brake
  oscillation exposed by the replay. That brake artifact requires fresh
  final-stage qualification rather than carrying old exclusions forward.
  The DM8 enemy-rocket/self-splash sequence and controlled timing remain open.
  The `51517896f` repeat passes all 33 catalog death/duration gates and all
  eighteen hit, ten recovery and two-bot traffic checks. Its long runs meet
  every duration gate but retain one recoverable CTF5 low-health descent.
  The next correction forecasts a steered approach to a known catch pad
  followed by the normal pad coast; renewed runtime qualification is required.
  Verified fixes through that stage are landed in the main checkout at
  `79df4b6bc`; MP-02 remains open.
  The `26f2bcd64` pad-approach correction passes nineteen hit cases, ten
  recovery cases, pad traffic and all 33 catalog death/duration gates (463
  kills, 627 traversals, one strict impulse exclusion). Its four long runs
  meet every duration gate with 285 kills and 520 traversals, but retain one
  CTF5 climbing-hop failure after seven strict exclusions. The bot first
  recovers from the rail hit, then jumps against its remaining momentum.
  Ordinary climbs now use the momentum-aware dodge proof and held input;
  the reproduced pending-hop fall survives. Renewed full-runtime and controlled
  timing qualification remain required.
  The correction is landed in the main checkout at `1417cda59`, whose ten
  CTF5 positive cases also pass.
  The `f57364857` stage passes all four long-match death/duration gates with
  six strict impulse exclusions, all 21 positive hit cases, ten recovery cases
  and pad traffic. Its complete catalog retains one CTF3 ramp departure after
  two exclusions. [Unpressed ramp-flight recovery](mp-bots.md#unpressed-ramp-flights-and-consumed-walking-routes-4-november-2026)
  addresses ascent, a consumed walking route and an intended pad that has not
  launched the bot; thirty native reversal controls pass on Windows/Linux.
  Full runtime qualification of that correction and controlled timing remain
  required. The climbing-hop fix is landed in main at `4768b7d61`.
  The `f6aa481e6` ramp correction now passes all four long-match and all 33
  catalog death/duration gates: 710 kills, 1,103 completed traversals, eight
  strict policy-2 impulse exclusions and zero navigation world deaths. All
  24 targeted hit cases, ten established recovery cases, pad traffic and thirty
  Windows/Linux native reversal controls pass. Two positive ramp observations
  extend to eighteen seconds; their original incomplete terminal results remain
  preserved, and causal exclusion criteria are unchanged. The fix is landed
  in main at `26460c802`, whose build, stage and four CTF3 recovery cases pass.
  Controlled timing remains separate; source/runtime evidence is recorded in
  the [ramp-flight qualification](mp-bots.md#unpressed-ramp-flights-and-consumed-walking-routes-4-november-2026).
  The renewed CTF3 timing attempt has zero world deaths and meets the selected
  4 ms mean / 8 ms p95 / 1,000 ms generation limits (0.791 / 4.25 / 744 ms),
  but mandatory contention detection fails because two separate SP processes
  overlap it. This initially left MP-02 open for a quiet-host timing repeat.
  The closeout above accepts that outstanding qualification caveat; the
  original failed report is preserved.
  The identical-stage `9a70f7b68` catalog's two CTF5 deaths pass
  exact-state causal exclusion, leaving zero navigation deaths and four
  duration failures; those duration failures are not waived.
  Follow-through, 4 November: the [hit-to-movement correction](mp-bots.md#hit-to-movement-handoff-4-november-2026)
  fixes all three previously recorded long-match hit states. The renewed
  `84dde9c6d` long runs pass CTF3/DZ3 directly and CTF5 after strict causal
  exclusion, but expose two further DM8 failures. Airborne self-blast admission
  and glancing-contact prediction are corrected next; renewed final-build
  long matches, the full catalog and controlled timing remain required.
  The `c65041464` stage passes all four targeted hit cases and ten established
  recovery cases. CTF3/DZ3 again pass directly, and all three CTF5 deaths pass
  strict causal exclusion. DM8 retains two recoverable failures: a coast that
  could safely catch a second pad, and a rocket slide requiring earlier input.
  The bounded pad-catch proof and the slide investigation continue under the
  same acceptance gates; the new catalog sweep is not final-build qualification.
  Follow-through, 4 November: the [terminal-state audit](mp-bots.md#terminal-state-long-match-audit-4-november-2026)
  completes the earlier impulse requalification: DM8 retains one navigation
  death and CTF5 two, with three and two proven enemy-impulse exclusions.
  [Earlier steering and marginal-contact recovery](mp-bots.md#earlier-knockback-control-and-marginal-landings-4-november-2026)
  repair two additional isolated poses. Sixteen Windows/Linux native reversal
  controls pass. Ten real-physics recovery outcomes pass their explicit positive
  criteria, including a settled CTF5 recovery and a DM8 recovery followed by two
  completed traversals. Their strict original report remains preserved; neither
  positive-only criterion weakens death exclusions. The second CTF5 departure,
  current long-match/catalog safety and controlled timing still need closure.
  The subsequent [four-map audit](mp-bots.md#moving-crusher-and-self-blast-safety-4-november-2026)
  passes the death gate on CTF3/DZ3 and retains six DM8/four CTF5 world deaths.
  It exposes two further gaps: the graph approved the future floor of CTF5's
  moving crusher, and self-splash health trades omitted the shooter's trajectory.
  The crusher's full travel envelope is now refused, with native reversal tests
  and before/after stock-map probes; self-splash checks the knockback slide/flight.
  The [renewed catalog and four-map assessment](mp-bots.md#complete-catalog-and-renewed-long-matches-4-november-2026)
  on `ee6207834` qualifies all 33 catalog variants' death/duration gates with one
  strictly proven impulse exclusion. Its 156 stalls include 80 on CTF6, which
  prompted a targeted walking-rim correction: stopped bots can creep onto
  continuously supported ground. Its original pose and ten recovery cases pass;
  a two-minute CTF6 repeat has 11 kills, 52 traversals and no world deaths, with
  26 stalls still recorded. The four earlier long matches retain
  one unexcluded DM8 death and one unexcluded CTF5 death after complete causal
  replays; their full later departures remain unisolated. Shared-host runs do
  not qualify controlled timing. A separate [single-host timing reference](mp-bots.md#single-host-timing-reference-4-november-2026)
  on the final main stage passes selected CTF3 limits (0.986 ms mean think,
  5 ms worst-window p95, 784 ms navigation generation), with no competing
  runtimes/compilers. Its independent safety report retains one unexcluded world
  death: three fixed-input replays and the controller survive, so the full
  match's later departure remains unisolated.
  Other maps/platforms, remaining departures and the full sweep after the
  final rim correction keep MP-02 open.
  Follow-through, 3 November: [knockback slide and replay corrections](mp-bots.md#knockback-slide-and-replay-corrections-3-november-2026)
  fix two isolated DM8 failures: route steering spoiled a survivable ground
  slide, and full reverse commands oscillated on a landing rim. Exact-origin
  prediction, a bounded slide proof, retained coasting and scaled edge braking
  pass six real-physics replay outcomes and Windows/Linux native controls.
  The replay checker now requires alive/grounded terminal state and exact trial
  markers; old first-touch impulse exclusions need requalification. The strict
  long-match, catalog and controlled-timing gates remain open.
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
- [x] **MP-03 — P2, implementation and qualification: broader bot navigation.**
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
  Completed, 3 November: the last two audit subjects are recorded limits, not
  defects. Role allocation compares landmark route-cost lower bounds, and a pad
  landing is flown without other players in the way; off-route recovery
  re-routes from wherever a bot lands ([known limits](mp-bots.md#known-limits)).
  Every audited subject now has a recorded outcome, and the only demonstrated
  gaps (trench rims, the debug-line pool) are fixed. Lifts in custom maps stay
  unmodelled; long-match qualification and the accepted timing-contention
  caveat are recorded under the completed MP-02 item.
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
- [x] **MP-05 — P1, investigation and implementation: demo reliability and
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
  Completed, 2 November: [MVD 1.3](multiview-demos.md#mvd-13-qualification-2-november-2026)
  adds only optional records, so 1.2 readers still play it. The server records
  structured markers (phase, round, pause, team score, kill, flag, powerup and
  `mvdMark` operator notes) and every user-info broadcast, so players who join
  mid-recording keep their names. Keyframes every `mvd_keyframeInterval` seconds
  hold map state, network info and the recorder's full delta baseline;
  backward and long forward seeks restore the nearest one and replay at most ten
  seconds, with replay from the start as the fallback. `mvdMarker`/`demoMarker`
  and the playback deck step through markers; killer and action auto-tracking
  (`demoAutoTrack`, optional team filter) end when a camera is chosen by hand.
  Runtime checks on listen and dedicated recordings match full replay at seven
  keyframe-seek targets (state, players, entities, living poses), walk the
  markers, follow a late joiner by name and auto-track kills. The dedicated run
  found that a demo from a cheat-free server could never be paused or slowed
  (`mvd_paused`, `mvd_scale` and `demo_uiSeek` counted as cheats); those
  viewer-side cvars are now `CVAR_NOCHEAT`. Native fixtures carry 15 and 6
  single-reversal controls. This closes the selected scope; reverse scrubbing,
  timedemo, AVI export, thumbnails and a named player list stay deferred, and
  Vulkan, Linux and macOS playback of 1.3 recordings were not run.
- [ ] **MP-06 — P2, implementation and qualification: liquid extensions.** Verify
  splash/bubble/wade effects, underwater view/muffle, projectiles, buoyancy and bot
  behavior within the selected static-liquid scope. The AAS liquid flag was repaired
  on 28 October and actual compiler output now includes checked swim/water-jump
  links; the earlier assumption that water-jump generation already worked was
  disproved. Player/bot swimming and replicated air are selected; swimming monsters,
  authored swim animations and moving volumes are deferred as recorded below.
  Preserve mapper guidance against duplicate hurt damage.
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
  At that checkpoint bot swimming, ragdoll buoyancy and an air HUD remained scope
  questions. The later selection admits player/bot swimming and replicated air;
  this older note does not reopen those decisions or establish new buoyancy scope.
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
  Progress, 2 November: [replicated air and bot swimming](liquid-qualification.md#replicated-air-and-bot-swimming-2-november-2026)
  are qualified on Windows/GL. `mp_liquid_air_smoke.py` passes: a remote
  observer receives the host's air as it drains and recovers, the underwater
  muffle switches, and paused demo seeks restore air on the followed HUD (100,
  56, 100, 56). The harness called a nonexistent HUD probe and bound every
  interface; it now uses `ui_hudTest` and loopback. A 90-second six-bot
  liquid-lab audit simulates 82 s with 53 kills, no world deaths and one stuck
  event. Multiplayer bots navigate by their runtime mesh and swimming monsters
  are deferred, so no AAS swim consumer is in the selected scope. Remaining:
  player-controlled swimming (real input only), projectiles crossing liquids,
  and splash, bubble and wade presentation checked by eye.
  Progress, 3 November: projectiles crossing liquids are qualified on the
  server. `g_debugLiquid` now logs each crossing, and a 120-second six-bot
  liquid-lab audit recorded 58 hyperblaster and rocket crossings of water,
  lava and slime. Shots fired under water leave the pool, shots fired into it
  carry on to impacts below the surface, and every crossing plays the splash
  at the boundary ([evidence](liquid-qualification.md#projectile-liquid-crossings-3-november-2026)).
  Remaining: player-controlled swimming (real input only), and splash, bubble
  and wade presentation checked by eye.
  Follow-through, 3 November: the [current GL regression](liquid-qualification.md#current-mp-regression-run-3-november-2026)
  passes remote air replication and four paused MVD seeks with matching air/HUD
  values of 100, 56, 100 and 56. The engine capture shows the underwater view
  and AIR gauge. This does not close physical swimming or the remaining effects
  presentation checks.
  Progress, 5 November: [splash and bubble presentation](liquid-qualification.md#splash-and-bubble-presentation-by-eye-5-november-2026)
  is checked from engine captures, using `mp_liquid_effects_capture.py` with
  effect logging. The water splash and head bubble trail play and the splash
  shows. The slime and lava splashes played but could not be seen: the shared
  definition used a flat surface quad and a drip-sized effect. They now use
  player-scale stock effects (`pipesplash`, `wastesplash`), and both show
  in the rebuilt package. Wading has no visual effect, only its moving sound.
  Remaining: swimming under player control, wade sound and a by-eye pass at
  play distance ([checklist](mp-human-qualification.md#mp-06-swimming-and-liquid-effects-by-eye)).
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
  Progress, 3 November: [weapon geometry fixtures](mp-network-qualification.md#weapon-hit-geometry-and-fragment-ordering-3-november-2026)
  found and fixed stale gauntlet/lightning effect endpoints and a penetrating
  rail ray shifted by impact translation. Contacts and chain directions now
  follow present targets, while rail continuation retains the historical ray.
  The executable state fixture includes the production weapon resolution blocks
  and rejects nine reversals. Remote weapon-play and same-fire-frame visual
  qualification remain open, and defaults are unchanged.
  Progress, 5 November: [remote weapon play](mp-network-qualification.md#remote-weapon-play-and-live-spectator-follows-5-november-2026)
  is checked from a loopback spectator's captures with `mp_remote_weapon_capture.py`.
  A followed bot's lightning beam runs from the drawn gun to its target, and the
  impact, damage numbers and hit marker sit on the target. The run found and
  fixed a casual-match defect: during live play the server refused every bot as
  a follow target, because participation was synchronized only in warmup and
  countdown. Casual sessions now mirror participation in every phase, and the
  follow contract pins this with a failing reversal. Remaining: the firing
  player's same-fire-frame view, impaired and WAN peers
  ([checklist](mp-human-qualification.md#mp-07-weapon-visuals-under-prediction)).
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
  Progress, 3 November: the checks one machine can qualify pass, and three
  defects are fixed
  ([evidence](mp-network-qualification.md#discovery-reconnects-content-and-retail-refusal-3-november-2026)).
  A LAN scan lists a pure dedicated server over IPv4 loopback and over this
  host's own scoped link-local IPv6 address. The client then joins it three
  times in a row, through the scan, by endpoint and by `reconnect`, in the same
  slot. IPv6 loopback has no link to scan and joins by endpoint. Windows
  servers never heard IPv6 scans: Windows delivers `ff02::1` only to sockets
  that joined it, and they now do. Ordered pure content, content-only mods and
  trusted local modules pass eight scenarios. A matching client joins, a
  missing or altered package is refused, modules in the save path or a PK4 are
  ignored, and a mod's module loads only with `net_serverAllowServerMod 1`. An
  incomplete `mod.json` now falls back to the openQ4 game, not retail-only
  content. The retail `Quake4Ded.exe` (2.85) is left out of the scan and
  refused for its protocol before openQ4 follows it into `q4mp`; it used to be
  refused as a missing mod. A 1.4.2 client's challenge, reproduced from
  `Quake4.exe`, is now answered with `server uses protocol 2.41` instead of
  silence. The pure token stays a same-platform compatibility check, not
  cryptographic module verification or anti-cheat. Real WAN, cross-machine and
  other-architecture peers, real multi-record DNS and the retail client binary
  itself remain open; the user deferred the peer gates on 28 October.
  Follow-through, 3 November: [fragment assembly qualification](mp-network-qualification.md#weapon-hit-geometry-and-fragment-ordering-3-november-2026)
  fixes delayed older packets discarding a newer message prefix, refuses
  fragments larger than the family's limit, and assembles aligned fragments in
  any order with bounded storage. The impaired IPv6 run exposed a reliable-queue
  overflow when same-message reordering discarded fragments; its failed report
  is retained. Production send/reassembly fixtures
  cover both families through 16 KiB, interleaving, missing/out-of-order packets,
  duplicates, exact-multiple terminators and malformed headers, with eight failing
  reversal controls. This does not qualify physical path MTUs or deferred peers.
  The final staged Windows build passes three pure joins each on IPv4 and IPv6
  loopback under seeded delay/jitter/loss/duplication (221 and 275 received client
  fragments), with unchanged runtime hashes and clean exits. The 59 broader
  match/network contracts pass. Real WAN, physical MTU and architecture gates
  retain their previous scope and deferrals.
  Progress, 5 November: the retail-client check now runs the real binary on
  request. `mp_retail_protocol_smoke.py --retail-client-binary` starts the
  installed `Quake4.exe` windowed and loopback-bound, connects it to the openQ4
  server and requires the protocol refusal in its own log. The Steam 1.4.2
  executable demands elevation (WinError 740) and the harness never elevates,
  so the check is step 8.4 of the [human checklist](mp-human-qualification.md#mp-08-real-network-conditions),
  run from an elevated prompt. The default run passes again on the current
  stage. The retail 2.85 server is refused for its protocol and never joined,
  and the openQ4 server answers a 1.4.2 challenge with `server uses protocol 2.41`.
  Decision, 9 November: openQ4's generated caches are never trusted on a pure
  server, because a cache entry's source stamp is public pak metadata and its
  pixels are bound to nothing. Reading the image cache before connecting no
  longer restarts the engine, though; the client reloads those images once the
  lock is in place (render API 33). A first pure join on `mp/q4dm1` now reaches
  gameplay in 7–10 s instead of 50–58 s
  ([decision and evidence](mp-network-qualification.md#generated-image-cache-on-pure-servers-9-november-2026)).
  The level-load caches of the experimental `com_levelLoadModernization 1`
  follow the same rule: none is read while pure, and models, worlds, collision
  and animations decoded from them before the lock are derived again at the
  next level load
  ([evidence](mp-network-qualification.md#level-load-caches-on-pure-servers-9-november-2026)).
  The session's expanded loading background for windows that are not 4:3 is
  now composed in memory from VFS reads the lock admits and never saved, so
  pure clients show it too and a widescreen client that loaded a map first no
  longer restarts on its pure connect; files older builds saved are still
  purged at the lock
  ([evidence](mp-network-qualification.md#expanded-loading-backgrounds-on-pure-servers-9-november-2026)).
- [ ] **MP-09 — P1, implementation and qualification: bot combat strength.**
  Each skill level fights as [its description](mp-bots.md#what-each-skill-level-feels-like)
  says, and skill 5 threatens a strong player. Gates: on the plain skill curve,
  `mp_bot_skill_ladder.py` shows every level beating the one below it on cross
  kills and damage, with skill 1 still beatable. Then play against people
  confirms skill 5 is hard but beatable by movement.
  Progress, 1 November: skill 5 was easy because the aim had no anticipation.
  It trailed a steady runner by 26 units, off the hull, and the turn trailed
  anything crossing the view by 0.22 s. The tight cone then held the trigger,
  so skill 5 dealt about skill 1's damage. A new `aimAnticipation` trait, a
  slew that pursues the aim point, a relative hold-fire gate and long bursts at
  skills 4-5 fix it. Skill 5 against skill 1 went from 216 to 446 damage per
  bot-minute and from 7:3 to 17:5 cross kills; 5 beats 3 (20:9) and 3 beats 1
  (17:5) ([evidence](mp-bots.md#combat-strength-1-november-2026)).
  Later on 1 November: a five-level round robin (22 matches, 6,362 kills)
  found skill 2 too far above skill 1 (steps 99, 56, 57, 60). Skill 2's row
  now sits 60% of the way from skill 1's, giving 83 ± 11, 72 ± 14, 57 ± 9 and
  60 ± 10, consistent with even spacing ([evidence](mp-bots.md#skill-levels-1-november-2026)).
  Remaining: play against people, and retuning the curve from what that shows.

## Audio and voice

Sources: [audio implementation and remaining limits](audio-modernization.md),
[retail reverb](retail-audio-reverb.md), [networking guide](../user/multiplayer-networking.md).

### AUD completion checkpoints

**Estimate: 80%; 0/7 tasks closed.** Open checkboxes here predominantly reflect
qualification debt rather than missing core implementations. However, mobile
memory bounds and full subtitle coverage are substantive outstanding scope, not
automatically satisfied by a listening pass.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| AUD-01 — Partial | 54 emitter/OpenAL distance measurements validate current map-unit curves; no speculative unit conversion needed. | Human retail comparison on real speakers/headphones. |
| AUD-02 — Partial | Simulated endpoint recovery, scene-clock save/load repair, Linux checks and repeated-map audio residency evidence exist. | Physical endpoint loss/hotplug, audible pause/cinematics, long sessions and target-OS breadth including macOS. |
| AUD-03 — Partial | Aggregate payload budgets/backpressure, bounded compressed streaming, recycled-source fix and exactly-once low-memory trim implemented; Windows/Linux measurements recorded. | Real mobile OS low-memory callbacks, device/provider process-memory budgets, thermal/loading/cancellation behavior and device breadth. |
| AUD-04 — Partial | Center routing, HRTF/stereo fallback, live layout changes and mod wet-send rolloff measured with loopback audio. | Physical surround outputs and real endpoint/layout transitions; preserve positional mix and stock zero-rolloff behavior. |
| AUD-05 — Partial | Consent/PTT/capture pipeline, codec/transport/moderation and bounded playback pass multi-process Windows/Linux and clock/loss tests. | Physical microphone/capture hotplug, independent-machine clocks and OS/device breadth. Existing 1:59:29 run is not a two-hour pass; cancelled rerun is not silently reinstated. |
| AUD-06 — Partial | Localized dialogue gain, complete installed-reference routing audit, extensive scripted-scene mute/save checks and a 42-map progression sweep with fights (1,052 kills, no mute leak) exist. | Ordered human play, including `walker` past the step where the probe player dies, and physical listening/device checks; retain effects/music separation. |
| AUD-07 — Partial | Playback-driven captions, fallback/mapping policy, portrait overlap, real save/load checks and the fight-gated scene corpus (1,546 distinct lines across retained reports, no untranscribed cue) implemented. | Missing transcripts remain explicit; ordered human play and physical stereo-HUD readability. Installed transcripts are English only. Available mappings do not mean every line is captioned. |

### AUD task evidence and acceptance

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
  streaming.** Byte-budget admission, backpressure and bounded compressed streaming
  are implemented as recorded below, replacing the earlier job-count-only bound.
  Qualify compressed input, pending/retained PCM, uploaded OpenAL buffers and
  provider/process overhead together on remaining devices, including Android
  low-memory/map-cancel cases. Preserve leadin/loop, seek/save restore, pause/time
  scale, underrun and cancellation behavior. Desktop/synthetic notice tests do
  not qualify mobile memory pressure. Android's required memory bound keeps its
  own exit rather than depending on unrelated optional streaming expansion.
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
  November 3: running PCM and stream-envelope jobs now observe cancellation between
  codec blocks instead of decoding the entire track before releasing it. A retained
  cancelled job resumes at its cursor; discarded jobs keep reservations until joined.
  Low-memory trims no longer finish/upload every pending sample, and level-end and
  admission reclamation discard stale jobs before joining retained work. Real-worker
  cancellation and production trim/reclaim/load-finalization tests cover both paths.
  The scene audit now records map-load phases, sound finalization, decode mode and
  byte budgets, with signed sound-on/off load-time comparisons. This supplies the
  loading-performance measurement path; target-device budgets and real mobile
  low-memory notices remain open.
  [Cancellation and loading evidence](audio-voice-evidence.md#decode-cancellation-and-loading-measurements-3-november-2026).
  November 5: HF-07's long-frame attribution found streamed voices opening,
  seeking and decoding on the main thread: 7.4 ms per start on average over
  four stock openings, and up to 105 ms. Each stream block now decodes on a job
  worker (`s_asyncStreamDecode`, default 1). The main thread keeps admission,
  the VFS open and OpenAL. A pending block keeps its voice alive. A voice
  stopped meanwhile cancels and parks its stream without waiting, and
  `s_restart` and shutdown join what is parked. Native tests run the
  production stream and voice queue against a deferring job system. With
  workers on, airdefense1 rotations show no Vorbis work on the main thread;
  with them off, the control does. Scene audits of four stock openings play
  the same streams in both modes. A start is now heard one to two frames late
  at the same position. Device and mobile qualification remain open.
  [Stream start evidence](presentation-owners.md#streamed-sound-starts-off-the-main-thread-5-november-2026).
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
  November 4: switching between the local microphone test and push-to-talk now
  closes the previous capture stream before reopening, discarding its resampler,
  queued audio and partial frame. Buffered test audio can no longer become network
  speech when push-to-talk is already held on test exit. Incomplete frames also
  expire after 120 ms instead of gaining a fresh timestamp after a callback stall.
  Production regressions cover both reproduced failures, reopening failure and
  clock wrap; the peer harness adds an optional SDL dummy-input local-test pass.
  Packet-clock diagnostics separate the injected sender drift from network jitter
  while retaining delivery, rejection and added-latency checks.
  Windows listen and dedicated peers each pass all 31 checks with dummy capture;
  the native production capture regressions pass on Windows and Linux.
  Physical microphones, hotplug, independent-machine clocks and target-OS breadth
  remain open. [Capture evidence](audio-voice-evidence.md#local-test-isolation-and-partial-capture-age-4-november-2026).
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
  November 6: the progression audit now fights. `openq4_killHostiles` kills the AI
  fighting the player through ordinary damage halfway through each step, so the
  conversations scripts hold until a fight is won run too. All 42 maps ran
  (1,967 steps, 1,052 kills): 1,418 dialogue starts, no mute leak in 2,681 muted
  probes, effects continuing, and 88 lines no earlier report reached. The sweep
  found a level-ending defect inherited from the SDK, now fixed: a full client
  entity table turned a refused debris piece into a fatal "cannot load collision
  model" error (`airdefense2`'s second trench). The `teleport` cheat now moves a
  driver's vehicle with it, which completes `m07_race`; the probe player still
  dies at `walker`'s step 11 (on foot, cause not identified), so its later steps
  remain uncovered.
  This is still not an ordered playthrough; physical devices remain open.
  [Fight evidence](audio-voice-evidence.md#fights-in-the-progression-audit-6-november-2026).
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
  November 6: with fights, the 42-map progression sweep starts 993 distinct
  dialogue lines (679 transcribed); every cue shown had a transcript. Two
  transcribed lines were heard without a cue, both at their very end: one voice
  already stopped, one 4.06 s into a 4.32 s line whose cue had ended with its
  logical playback. Across every retained
  report the corpus is now 1,546 distinct lines (954 stock, 592 Awakening). The
  installed game has English transcripts only, which the player guide states;
  ordered human play and physical stereo-HUD placement remain open.
  [Fight evidence](audio-voice-evidence.md#fights-in-the-progression-audit-6-november-2026).

## Desktop platforms and packaging

Sources: [platform support](platform-support.md), [Deck QA](steam-deck-qa.md),
[Linux ARM64 evidence](linux-arm64-signoff-evidence.md),
[macOS policy](macos-support-matrix-policy.md), [macOS evidence](macos-signoff-evidence.md),
[content pack distribution](content-pack-distribution.md).

### PLAT completion checkpoints

**Estimate: 55%; 0/5 tasks closed.** Existing build/package work and supported
desktop paths earn credit. Hosted CI, software graphics and community reports
remain narrower than final-package physical-device signoff.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PLAT-01 — Partial | Linux x64/Deck platform implementation, hosted compositor paths and scoped stock/runtime evidence exist. | Physical GNOME/KWin/wlroots, Deck LCD/OLED and game/desktop modes; input/audio hotplug, suspend, displays and reported pacing. |
| PLAT-02 — Partial | Linux ARM64 builds, packages and assetless client/dedicated CI exist. | Physical desktop-GL hardware SP/save/MP, audible sound, input/display recovery and accepted archive-bound signoff. |
| PLAT-03 — Partial | Windows ARM64 native build/stage and assetless dedicated startup exist. | Automated client/renderer/audio startup first; then hardware SP/MP/save/controllers and package checks. |
| PLAT-04 — Partial | macOS arm64 bundle/build/provider work, hosted startup and community gameplay reports exist. | Floor/current OS, both package variants, Finder/DMG/copied app, signing/Gatekeeper, symbols and physical display/input/audio/gameplay matrix. |
| PLAT-05 — Partial | Apple renderer/backend policies and compatibility repairs documented. | Current hardware reproduction of rendering/context/MSAA/VBO/module behavior; MoltenVK separately; explicit package/architecture/backend decisions. |

### PLAT task evidence and acceptance

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

### PKG completion checkpoints

**Estimate: 85%; 4/6 tasks closed (6 November 2026).** PKG-01 to PKG-04 are
implemented and proven on local candidates; their release-workflow steps first
run with the next release. PKG-05 needs that release's measurements; PKG-06 is
decided as a planned updater not yet started. Evidence:
[implementation status](content-pack-distribution.md#implementation-status-6-november-2026).

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PKG-01 — Complete | Rootless per-platform update archives, Windows `-symbols.zip`, `content-packs.json`, release-note pack status, localized missing/mismatched-pack report (12 languages), patch releases refuse bulk changes; `verify-update` and `verify-identity` proved on consecutive Windows candidates and wired into the release job. | None. |
| PKG-02 — Complete | `content_packs.json` drives engine, Meson, packagers, installer, verifiers and Android; pak0/pak1/pak2 split; packs built once per release and reused by source digest; `-text` checkout makes Windows and Linux packs identical; client-only packs `PURE_NEVER` and optional on servers; retired packs skipped and deleted. | None. |
| PKG-03 — Complete | Software DXT decode for renderers without S3TC; 51/51 packs accepted with every area resident on GL, GL software decode and Vulkan; atlases and `core2` metadata trimmed (pak1 267.5 → 96.4 MB); `waste` re-baked; loadscreen format decided: keep TGA + DDS. | None. |
| PKG-04 — Complete | Content generations, overlays pak3–pak9 rebuilt from changed files with the base kept byte-identical from the generation's blobs, size budget, generation bump folding; one-file fix shipped as a verified 153 KB overlay. | None. |
| PKG-05 — Implemented, measurement open | Installer downloads only missing or different bulk packs, SHA-256 and release URL compiled in; AppImages embed `gh-releases-zsync` information and publish `.zsync`; itch.io declined. | Measure update downloads for each channel on the next release (maintainer decision, 6 November). |
| PKG-06 — Decided: plan an updater; optional P3 | Constraints recorded: signed manifest, compiled release URL, platform HTTPS client, helper-process replacement, per-user installs, Sparkle for macOS, whole-APK Android. | Implement the updater under those constraints. |

### PKG task evidence and acceptance

- [x] **PKG-01 — P1, implementation: update archives and content-pack status.**
  Every Windows and Linux package has a rootless `-update` archive without the
  bulk packs, and the Windows PDBs moved to `-symbols.zip`. `content-packs.json`
  is published with each release, the notes gain a Content Packs section, and
  the startup report for a missing or mismatched client-only pack is localized.
  A release with the previous release's major.minor refuses a changed bulk pack.
  Local Windows x64 candidates: full 841.8 MB, update 54.0 MB, symbols 88.3 MB;
  `verify-update` reproduced all 394 files of candidate B from A plus B's update
  archive (395 of C with C's changed standalone `pak1.pk4`), and
  `verify-identity` matched five archives to the manifest. The release job runs
  both before publishing.
- [x] **PKG-02 — P1, implementation: split pak1 by change rate and audience.** Done
  in `849a65032` and later. pak0 gained `liquid_lab` and the script overrides,
  pak1 holds light grids and pak2 loadscreens; the generated table drives every
  consumer. Packs are built once per release and reused by source digest; WSL
  and Windows build identical pak0. `content_pack_split_smoke.py`: a dedicated
  server with only pak0 hosts `mp/q4dm1`, and a joining client keeps pak1 and
  pak2 off the pure list and lights the map from its packed grid. The engine
  skips and names unlisted `pakN.pk4`; the installer deletes them.
- [x] **PKG-03 — P1, decision and implementation: ship only what the runtime reads.**
  Renderers without S3TC decode pack atlases on the CPU, so no renderer needs
  the loose TGAs, which pak1 no longer carries (96.4 MB).
  `lightgrid_pack_load_test.py` accepts all 51 packs with every area resident on
  OpenGL, OpenGL with `r_lightGridSoftwareDecode 1` and Vulkan. `waste` was
  re-baked with the shipped settings; its v4 loose grid had lit nothing. The
  maintainer kept TGA + DDS loadscreens (6 November): DDS-only would have saved
  455 MB but needed an engine change, because retail TGAs share 69 of the 70
  names, and would compose wide-screen backgrounds from DXT1.
  `loadscreen_format_audit.py` showed every reader on GL and Vulkan at normal and
  reduced texture quality from the release packs.
- [x] **PKG-04 — P2, implementation: overlay packs.** pak1 and pak2 record content
  generation 1. `overlay-plan` declares an overlay for files changed since the
  generation, within `overlay_budget_bytes` (25 MB), and proposes a new
  generation past it or for removals; `generation-bump` folds overlays. A
  one-file loadscreen fix shipped as a 153 KB `pak3.pk4`, verified at startup
  and read in place of pak2's copy, with pak2 rebuilt to its published MD5.
- [ ] **PKG-05 — P2, implementation: delta-aware channels.** Implemented: AppImages
  carry `gh-releases-zsync` update information and publish `.zsync` files; the
  Windows installer keeps a current pack, uses one beside `setup.exe`, or
  downloads it from the release URL with the SHA-256 compiled in. The itch.io
  channel was declined. Remaining exit: measured update downloads for one
  release on each channel, taken on the next release by the maintainer's
  choice; the installer's download path is first compiled and run there.
- [ ] **PKG-06 — P3, decision: in-game updater and content storage.** Decided
  6 November 2026: plan an in-game updater, built under the recorded
  constraints (signed manifest and compiled release URL, platform HTTPS client,
  helper-process replacement, per-user installs, Sparkle deltas on macOS,
  whole-APK Android). Bulk sources stay in Git with this decision. Remaining:
  the updater implementation.

## Steam integration

Sources: [platform support](platform-support.md), [Deck QA](steam-deck-qa.md),
`src/framework/FileSystem.cpp` (Steam library discovery), the MP join screen and
server browser under the retained UI.

Requested 3 November 2026. Many players own Quake 4 on Steam and run openQ4 with
the Steam client open, including on Steam Deck. Today openQ4 uses Steam only to
find the installed game files; it has no Steamworks code. This work makes Steam's
social features work for openQ4 players: friends see what they are playing and
can join them in one click, invites arrive in the game, and the Deck's keyboard,
overlay, screenshots and recordings behave as they do for native Steam games.
Every item honors five product rules:

1. **Seamless when detected, invisible when not.** The integration activates
   only when a running Steam client and the Steam API are found at startup. When
   either is missing, offline or fails to initialize, openQ4 behaves exactly as
   it does today: no error dialog, no missing-library failure, no Steam-only UI
   rows, and a single informational log line. GOG, retail-disc, Android and
   assetless dedicated installs never depend on it.
2. **One option.** An archived CVar (proposed `com_steamIntegration`, default
   `1`, beside the existing `com_steamDeck*` CVars) with one localized Settings
   row turns it all off. Presence privacy is the only sub-option a player sees.
   Dedicated servers never load the Steam API.
3. **No gameplay coupling.** Steam never changes simulation, network protocol,
   pure checks, saves or demos. Callbacks run on the main thread at a fixed point
   in the frame, and a missing or hung Steam client cannot stall a frame.
4. **Privacy first.** Presence never publishes server passwords, rcon details or
   private LAN addresses to anyone except a friend the player explicitly invites.
5. **Contained and portable.** SDL3 stays the platform layer: where SDL3 already
   covers a feature (gamepads, Steam Input, text input), use it. Steam calls live
   only in the platform layer under `src/sys/`; shared engine code and the SP/MP
   game modules see a backend-neutral interface, never Steam types. The build
   uses a Meson feature option that stays off until STEAM-01 allows it, and any
   third-party code follows the vendoring and licence-notice rules in
   [source layout](source-layout.md). All new text uses `#str_*` entries in every
   language table, and new menus and rows follow the
   [UI visual design](ui-visual-design.md) in the retained interface, while the
   stock interface keeps working without them.

### STEAM completion checkpoints

**Estimate: 0%; 0/6 tasks closed.** Steam install discovery and the Deck launcher
already exist but are not a Steamworks integration. No bridge implementation is
credited before STEAM-01's decisions.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| STEAM-01 — Decision pending | Identity/licence/distribution questions and feature dependencies specified. | Record permitted app identity, proprietary API boundary and redistribution/platform policy before code lands. |
| STEAM-02 — Not started | Backend-neutral/no-op design requirement specified. | Optional runtime service, initialization/callback/overlay lifecycle, identity and no-Steam parity on supported renderers. |
| STEAM-03 — Not started | Presence states and privacy boundary specified. | Localized state-driven presence, safe joinable-server metadata and friends-list verification on required systems. |
| STEAM-04 — Not started | Existing MP join/rejection flow is the integration target. | Cold/running joins, invites, save confirmation and friends UI; distinct-network success/failure cases and private-password handling. |
| STEAM-05 — Not started | Keyboard, engine-screenshot and recording-marker scope specified. | Connect supported SDL/Steam routes, all text fields, screenshot library tagging and bounded timeline markers; Deck game/desktop verification. |
| STEAM-06 — Not started | Lifecycle/platform matrix specified. | Steam absent/offline/restart, launch origins, renderer/mode/save/demo transitions and long sessions; exact packages, documentation and support promotion. |

### STEAM task evidence and acceptance

- [ ] **STEAM-01 — P2, decision: identity, licensing and distribution.** Settle
  three questions before any code lands. (a) **App identity:** what Steam sees
  openQ4 as: the player's own Quake 4 app (2210, and what that means for launch
  options, overlay attachment and a friend's "Join Game" launching the retail
  executable rather than openQ4), a non-Steam shortcut (overlay only, no API), or
  a dedicated openQ4 app ID. Rich-presence display strings, achievements and
  Steam Cloud need configuration only an app's owner can upload, so the answer
  bounds which later items are possible; record each feature's dependency on it.
  (b) **Licence:** the engine is GPLv3 with the Doom 3 additional terms and
  Steamworks is proprietary. Review whether and how openQ4 may call the Steam API
  (for example a dynamically loaded, separately licensed bridge module behind a
  small openQ4-defined interface, like the renderer modules) and what may be
  redistributed. No Steamworks headers or binaries enter Git or packages until
  this review is recorded. (c) **Distribution:** which packages carry the bridge
  and Steam API library (Windows x64, Linux x64/Deck, macOS arm64; none for
  Android or ARM64 targets without a Steam client), and how the Windows
  installer, AppImage and Flatpak/Steam Runtime cases find it. Exit: a recorded
  decision with the supported feature set per identity option.
- [ ] **STEAM-02 — P2, implementation: optional platform-services layer.** Add an
  engine-side platform-services interface whose default is a no-op, plus the
  Steam implementation chosen under STEAM-01, loaded at runtime only when Steam
  is detected. It owns init/shutdown ordering with SDL3 and the renderers,
  per-frame callback pumping, the local player's Steam identity and persona name
  (offered as the default player name on first run, never overwriting a chosen
  one), and overlay activation: opening the overlay pauses single-player like
  the pause menu and releases mouse capture, and closing it restores both. Game
  modules report presence state (campaign, map, mode, score) through the
  engine's game interface; neither game library calls Steam. Keep
  the interface backend-neutral so another platform (for example Discord
  presence) could implement it later without touching game code. Exit: with
  Steam absent, startup, frame time and logs are unchanged against the pre-change
  binary; with Steam present, the overlay opens over GL and Vulkan in windowed
  and fullscreen modes without input or capture glitches.
- [ ] **STEAM-03 — P2, implementation: rich presence.** Publish what the player
  is doing, updated on state changes rather than every frame: main menu;
  single-player campaign, chapter and map, with Quake 4 and The Awakening named
  separately; SP Arena; multiplayer gametype, map, player count and team or
  score summary; spectating; demo playback; loading. Use the game's localized
  map and mode names. Set a `connect` value only while the player is on a
  joinable, non-password, internet-reachable server, and group teammates with
  Steam's player-group keys. A privacy setting offers full presence, presence
  without join, or none. Exit: each state shows correctly in a friend's Steam
  friends list on Windows, Linux and Deck, and no state publishes a password or
  private address.
- [ ] **STEAM-04 — P2, implementation: friends, invites and joining.** Accept
  joins from every Steam path: "Join Game" in the friends list or overlay while
  openQ4 is running, and launch-time connect arguments when it is not. Route each
  through the existing MP join flow so version, protocol, pure-content, password
  and full-server outcomes show the same localized messages as a manual join. If
  the player is in single-player, confirm first and offer to save. Add **Invite
  friends** to the multiplayer pause menu and scoreboard through Steam's invite
  dialog; a direct invite to a password-protected server carries the password
  only to that friend. Show friends who are playing openQ4, and their servers, in
  the server browser and join screen. Keep openQ4's own UDP networking; Steam
  relay or NAT traversal is OPT-09 scope. Exit: two machines on different
  networks join each other by invite and by friends-list join, both while running
  and from a cold launch, and each failure case reaches its correct message.
- [ ] **STEAM-05 — P2, implementation: Deck and client conveniences.** Show
  Steam's on-screen keyboard (floating where supported) for every text field when
  the player is using a controller, through SDL3's screen-keyboard path where it
  already reaches Steam's keyboard and the Steam API only where it does not:
  player name, chat, console, server password, save names and the server
  browser filter. Add F12 and engine `screenshot`
  captures to the player's Steam screenshot library, tagged with the map name.
  Add Steam game-recording timeline markers for meaningful moments (match start
  and end, frags, deaths, flag captures, chapter and boss starts, Arena rounds)
  using only built-in marker icons, with bounded rates so busy matches do not
  flood the timeline. Exit: Deck game mode and desktop each show the keyboard on
  focus and return the typed text, screenshots appear in the library with their
  tag, and a recorded match shows its markers in order.
- [ ] **STEAM-06 — P2, qualification: detection and lifecycle matrix.** On
  Windows x64, Linux x64 (native and Steam Runtime), Steam Deck game and desktop
  modes, and macOS arm64 where the bridge ships: Steam absent; Steam installed
  but not running; Steam offline mode; Steam exiting or restarting mid-session;
  launched from Steam versus directly; `vid_restart`, renderer switch, map and
  mode changes, SP save/load, demo playback and long sessions. Test SP and MP
  gameplay, not only the main menu, from the restaged `.install/` package with
  the mode-specific launches. Every case starts, plays and shuts down cleanly,
  presence never shows a stale state, and with the integration off or Steam
  absent the build is indistinguishable from one without it. Close with the
  [platform support](platform-support.md) and
  [capability](engine-capability-matrix.md) matrices updated, player release
  notes in `releases/`, and a README feature bullet once players can use it.

## Engine loading safety and developer tools

Sources: [job service](parallel-job-system.md), [loading caches](loading-cache-modernization.md),
[runtime refinements](core-runtime-refinements.md), [map workspace](level-editor-modernization.md),
[AAS compiler](aas-compiler.md), [dmap](dmap-compilation.md).

The map-editor work resumed on 6 November 2026 and TOOL-01 through TOOL-04 are
closed; the editor stays labelled experimental. Compiler/runtime compatibility
fixes and the UI visual editor have their own items.

### CORE completion checkpoints

**Estimate: 100%; 6/6 tasks closed.** CORE-01, CORE-02 (not promoted), CORE-03,
CORE-04, CORE-05 and CORE-06 are closed. macOS, a preview platform, is qualified
through its own evidence gate rather than CORE-01.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| CORE-01 — Closed | GUI precache, expression-storage, coordinates and hit-map fixes; clean 0.14.1 Windows x64 and Linux x64 packages pass dedicated and SP tours with jobs on and off, jobs parity, dedicated exits, renderer budgets on both backends and the retail baseline; Linux jobs-on level-change memory growth fixed. | — |
| CORE-02 — Closed: not promoted | Current-build cold/warm tours, corrupt-cache fallback, bounded staging and map-change generations measured; the opt-in cache is slower cold (40%) and warm (7-14%), so it stays off. | — |
| CORE-03 — Closed | SP load profiled: remaining main-thread image decode is under 5% of the load, not worth moving; dedicated servers no longer read and discard every level image prefetch (media finish 8.5 s to 1.5 s over 24 loads). | — |
| CORE-04 — Closed | Strict strings; configure warnings fixed (RmlUi's not reproduced); diagnostics reviewed; collision contents decoding fixed; MSVC ASan gameplay (dedicated tour, SP combat, compilers, malformed and fuzzed AAS data) with no report; T-junction ASan/UBSan fixture. | — |
| CORE-05 — Closed | `hub2` sliver and silhouette repairs; #209's maps profiled and compiling in minutes with byte-identical output; collision contents decoded as retail; rebuilt maps load, render and navigate (AI take cover) in SP with compile memory recorded. | — |
| CORE-06 — Closed | Tactical features generated as Quake 4 does (98.8% of 7,422 retail features reproduced exactly); `runAASTactical`; rebuilt `airdefense1` AI takes cover as on retail data; malformed data dropped or rejected, fuzzed. | — |

### CORE task evidence and acceptance

- [x] **CORE-01 — P1, qualification: foundation final-package evidence.** Repeat
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
  Progress, 6 November: [map-cycle tours](parallel-job-system.md#map-cycle-and-shutdown-evidence-6-november-2026)
  on the Windows x64 development build. The dedicated server loaded `q4dm1`-
  `q4dm8` three times with jobs threaded and with `jobs_enable 0`: 24/24 loads
  each, lap peaks 391/427/427 MiB, exit 0 within 0.42 s and `jobsShutdown PASS`.
  The SP client toured four campaign maps three times, then `airdefense1`/
  `hangar2` six times; both settle within a fixed band (the last four
  `airdefense1` visits within 27 MiB) and shut down cleanly.
  Closed 6 November on [clean final packages](parallel-job-system.md#final-package-qualification-6-november-2026):
  0.14.1 Windows x64 and Linux x64 packages built from `00f4b4003` as the
  release workflow builds them, recording `openq4_dirty=false`. On both, the
  dedicated 24-load tours with jobs on and off, five starts and quits each with
  one synchronous self-test, the SP campaign with jobs on and off and the
  `airdefense1`/`hangar2` tours plateau and end in `jobsShutdown PASS`, and the
  dedicated server never starts its effects runtime. Four `storage1` runs give
  identical game state with jobs on and off. All nine required renderer-budget
  cases pass on OpenGL and Vulkan, and the retail baseline passes its SP
  capture, demo playback, save and load and multiplayer server and client. The Linux tours
  found SP memory rising about 60 MiB per level change with jobs on, because
  glibc kept the worker heap arenas; the engine now returns free heap memory
  after each load, and the eight-lap tour stays level with jobs off. A fast
  package also exposed a frame-counted lift settle in `sp-storage1-second`,
  which now waits in real time as well. macOS, a preview platform, is qualified
  through the macOS evidence gate.
- [x] **CORE-02 — P2, optimization: cache and loading promotion.** Investigate cold
  load regressions, per-file Sync, cache identity and rewrite churn before enabling
  learned/generated-cache experiments. Default worker DDS reading already exists.
  Require cold/warm baselines, corrupt-cache fallback, bounded memory and cancel/
  restart/disconnect coverage; generated data remains disposable under savepath.
  Closed 6 November, [not promoted](loading-cache-modernization.md#current-build-measurement-and-promotion-decision-6-november-2026):
  on three dedicated laps of eight stock maps, `com_levelLoadModernization 1`
  took 8.5 s cold and 7.7-7.9 s warm against 6.1-7.2 s for the classic path,
  although every warm load replayed its manifest without rejection and staging
  stayed under 1 MiB. All 45 deliberately corrupted payloads were rebuilt from
  source with no partial file left, and map changes closed every generation.
  The experiment stays default-off.
- [x] **CORE-03 — P2, implementation: justified background asset work.** Extend
  detached decode/transcode/upload scheduling and portal-aware prioritization only
  after profiling, with immutable ownership handoffs, backpressure, cancellation
  and main-thread adoption. Do not move game callbacks, GL calls or live parsers
  onto workers without a proven lifetime and thread-safety contract.
  Closed 6 November after [profiling](parallel-job-system.md#background-asset-work-profile-6-november-2026):
  of a 15.9 s SP `airdefense1` load, image decode left on the main thread is
  0.72 s and GL upload 0.31 s, with the DDS reads already on workers; moving the
  decode would save under 5% and is not justified. The profile showed dedicated
  servers reading and discarding every prefetched level image; only a running
  renderer now prefetches, which cut media finish from 8.5 s to 1.5 s over 24
  dedicated loads.
- [x] **CORE-04 — P2, implementation and qualification: compiler and safety debt.**
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
  Progress, 6 November ([details](core-runtime-refinements.md#follow-up-configure-warnings-and-diagnostics-6-november-2026)):
  a fresh configure reproduced the SDL include-sandbox warning, now in three
  engine libraries as well as the fixtures, plus a content-pack newline warning;
  both are fixed, and a fresh configure with native tests prints no Meson
  warning. The RmlUi wrap warning does not reproduce on a fresh configure. A
  dedicated `q4dm1` load logs no lazy pre-cache warning; `burn_misc_sm` and the
  `explosion_deform3` stage are genuine retail data gaps and keep their warnings;
  dedicated program-path lines now end. Shipped `.cm` contents were decoded by
  name although Quake 4 wrote them through a shifted table, so vehicle, fly, shot
  and item clips loaded as the wrong contents; see
  [collision contents names](dmap-compilation.md#collision-contents-names-6-november-2026).
  New coverage: the T-junction fixture checks the indexed hash against the
  original bin scans under Clang ASan+UBSan and MSVC, and 30 seeded mutations of
  AAS tactical sections load safely. Closed with
  [AddressSanitizer gameplay](core-runtime-refinements.md#addresssanitizer-gameplay-6-november-2026):
  with `/INFERASANLIBS:NO` and explicit `/MD` runtime libraries an MSVC ASan
  build links every module. A dedicated 16-load tour, SP combat on the rebuilt
  #209 map, `dmap` of `hub2` and the #209 map, tactical generation, and all
  malformed and fuzzed AAS inputs ran with no report. Linux ASan gameplay was
  not run (system-drive space).
- [x] **CORE-05 — P1, implementation and qualification: safe compiled geometry.**
  The historical `hub2` 36,635,340-index failure is repaired, not an outstanding
  request to raise the 16,777,216-index safety bound or redesign the surface format.
  [T-junction sliver repair](dmap-compilation.md#sliver-multiplication-in-t-junction-repair-31-october-2026)
  reduced the supplied map to 298,438 triangles with a 153,000-index largest
  surface on the recorded Windows build. The compile produces collision and
  navigation data, loads into SP gameplay and saves. Optimizer zero-area rejection
  has separate Windows/Linux evidence. The
  [silhouette scratch repair](dmap-compilation.md#silhouette-edges-on-large-surfaces-1-november-2026)
  preserves all 78,794 edges in the large pipe surface, removes the previous
  `MAX_SIL_EDGES` warnings and retains small-surface behavior. Production fixtures
  and runtime stencil views are recorded; the traceability table records the
  reporter's successful rebuild confirmation.
  Remaining: complete bounded-memory rendering, collision and actual AI behavior
  qualification, and investigate other diagnostics individually. Successful AAS
  generation and map entry alone do not prove navigation or whole-map parity.
  Track #209's separate excessive compile time on the supplied `airdefense1` and
  `core1` maps. The reporter confirms eventual completion, with two days reported
  for `airdefense1`; the maps' increased explicit subdivision is a profiling lead.
  Confirm the completed-run command/build and `hidef2kbc72` asset identity, then
  measure stage timings and memory against stock assets and profile the slow pass.
  The earlier log ending in global T-junction repair does not establish that the
  whole delay occurs there or that the earlier `hub2` fixes regressed. See the
  [5 November evidence](dmap-compilation.md#airdefense1-and-core1-slow-compile-report-5-november-2026).
  Closed 6 November. Profiling the supplied #209 maps against the retail
  packages found passes that grew with the square of a curve's triangles. dmap
  scanned per-plane groups, crowded vertex bins and growing triangle lists once
  per triangle, and the AAS leaf merge tested every portal plane against every
  portal point. Indexes, list tails and a bounds test fix each pass
  ([evidence](dmap-compilation.md#compile-time-on-dense-patches-6-november-2026)).
  The supplied `airdefense1` now compiles in 4-5 minutes (138-198 s geometry,
  64 s AAS, 1.18 GB peak) and `core1` in about 3 (1.75 GB). Six stock maps give
  byte-identical `.proc` files and four identical AAS files, and stock
  `airdefense1`'s AAS fell from 692 s to 58 s. Its rebuilt map and the supplied
  one load in SP with valid navigation, render, and give tactical AI cover.
  Shipped collision files now decode their clip contents as Quake 4 wrote them
  ([collision contents names](dmap-compilation.md#collision-contents-names-6-november-2026)).
  Compiler diagnostics such as `BuildOptTriangles: couldn't locate opposite`
  are kept unchanged. #209 itself stays open for the reporter to confirm on a
  published build.
- [x] **CORE-06 — P2, implementation and qualification: AAS tactical generation.**
  Complete cover/lean/corner/pinch/vantage generation or retain an explicit
  rebuilt-map limitation; path/swim compilation already exists. Test stock/custom
  rebuilt data and actual AI consumers, including malformed or absent tactical
  data. These compiler gaps remain independent of the map-editor pause.
  Closed 6 November ([evidence](aas-compiler.md#tactical-features)): the AAS
  compiler places cover, lean, look-over and corner features after optimization,
  reproducing the rules of Quake 4's own generator, and `runAASTactical`
  regenerates them on existing files. On the retail geometry of all 28 shipped
  `aas32` files it reproduced 7,333 of 7,422 retail features (98.8%), each with
  retail's flags and normal; `core2`, `convoy1`, `convoy2`, `network2` and
  `storage2` match exactly. Each area now lists only its own features, unlike
  retail's modulo-1024 bucket quirk. A fully rebuilt `airdefense1` (242
  features) loads in SP, and spawned tactical AI take cover at the same spot as
  on retail data. The loader rejects truncated or negative sections and drops
  inconsistent spans or indexes with a warning; 30 seeded mutations all load
  safely. Pinch and vantage come only from `info_tactical_feature`, as in Quake 4.

### TOOL completion checkpoints

**Estimate: 100%; 4/4 tasks closed.** Work resumed on 6 November 2026 and the
[level editor](level-editor-modernization.md) now authors, builds and plays
Quake 4 maps. It stays labelled experimental; the legacy Radiant source remains
unbuilt and separate, and the UI visual editor is a separate UI-06 obligation.
macOS, a preview platform, is qualified through its own evidence gate.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| TOOL-01 — Closed | Docked, tabbed and resizable panels with persisted layout; 3D plus top/front/side wireframe views and quad; per-map camera and view state; static entity models; typed inspector from entityDef `editor_*` declarations; source-linked diagnostics; all labels fit in 12 languages at 720p, 1080p and 2160p; GL and Vulkan. | — |
| TOOL-02 — Closed | Lossless brushDef3/patch adapters; grid, snap, move, turn, scale and texture lock; one undo step per gesture or tool; groups, layers, prefabs, duplicate, reference-repairing rename; bounded background asset index; all 543,372 retail primitives round-trip. | — |
| TOOL-03 — Closed | Sandboxed child-process builds with progress, cancel and linked diagnostics; outputs published only after a whole successful build; isolated play client; editor state kept; authored and stock maps built and played with AI on the rebuilt navigation. | — |
| TOOL-04 — Closed | Fault-injected storage and builds, external-edit detection, atomic saves, recovery, randomized undo/redo, renderer switch and resized restart, Windows and Linux runs, declared latency budgets met; the old restart label defect no longer reproduces. | — |

### TOOL task evidence and acceptance

- [x] **TOOL-01 — P2, implementation: complete the map workspace.** Build on the
  existing transactional document service and source preview. Add docking,
  orthographic views/camera persistence, richer entity/model previews, typed
  properties and linked diagnostics. Qualify localized responsive layouts, native
  input/high DPI and desktop backends. This is not restored MFC Radiant.
  Closed 6 November ([workspace](level-editor-modernization.md#milestone-4-the-docked-workspace-tool-01),
  [evidence](level-editor-modernization.md#qualification-evidence-6-november-2026)):
  Panels dock left, right or (Problems) under the views, two to a
  dock as tabs, resize and persist in `editorExperimental_layout`. Top, front
  and side wireframes drawn through the GUI device context join the 3D view,
  alone or as a quad; each map's cameras, view mode, grid, texture lock, layers
  and selection persist. Point entities show their static models, loaded
  within a per-frame budget; the inspector types properties from entityDef
  `editor_*` declarations and refuses ill-typed values, and validation links
  each problem to its entity or primitive. Every label fits in all 12
  languages at 1280x720, 1920x1080 and 2538x1312 in two layouts (the first
  runs found wrapping in nine languages). OpenGL and Vulkan pass the workspace
  phase, including a renderer switch and a resized restart; input is driven
  through engine events into the workspace GUI, and a physical pointer and
  keyboard pass remains the owner's hands-on check.
- [x] **TOOL-02 — P2, implementation: geometry and asset authoring.** Add lossless
  brush/patch adapters, grid/snapping/transforms, texture locking, one undo action
  per gesture, layers/groups/prefabs and safe rename/reference repair. Make asset
  indexing asynchronous and bounded. Test cancellation, mixed selections, large
  maps and preservation of untouched source.
  Closed 6 November ([geometry](level-editor-modernization.md#milestone-3-geometry-and-asset-authoring-tool-02)):
  Lossless `brushDef3`/patch adapters rewrite only changed numbers;
  all 543,372 primitives of the 79 retail maps serialize byte for byte and every
  valid one survives locked transforms (five degenerate retail brushes are
  reported). Move, turn, scale and snap act on mixed selections with per-face
  texture lock; grouped transactions make each gesture or tool one undo step,
  and a cancelled gesture restores the exact revision. `func_group` groups,
  `editor_layer` layers, prefabs that rename collisions and keep their links,
  duplicates whose links follow the copies, and renames that repair `target`,
  `bind` and entityDef-declared references. Asset names are indexed a bounded
  slice per frame, with cancellation and size limits. On a 20,000-brush map,
  turning 5,000 brushes and 1,000 entities is one 146-227 ms step.
- [x] **TOOL-03 — P2, implementation: build and play workflow.** Integrate dmap/AAS
  progress, cancellation and source-linked diagnostics with an isolated play
  sandbox and editor-state restoration. Preserve previous playable output on
  failure. Consume CORE-05/CORE-06 compiler capabilities and qualify stock/custom map
  round trips and AI use of rebuilt data.
  Closed 6 November ([build and play](level-editor-modernization.md#milestone-5-build-and-play-tool-03)):
  Builds run the dedicated server as a child process (SDL3, or
  `posix_spawn` where the Linux dedicated server has no SDL3) against a sandbox
  save path; `com_compilerOutput` keeps `.proc`/`.aas` there. Progress, cancel
  and diagnostics are mapped through `idMapFile::Resolve`'s renumbering to the
  source brush. Outputs replace the previous set only after a whole successful
  build, with rollback; leaks, errors, timeouts, cancels and hung compilers
  leave it untouched. Play starts an isolated client with the sandbox as its
  profile while the editor keeps its state. A room authored only with editor
  commands built (navigation with 5 areas and 8 reachabilities) and its
  marines walked about 200 and 150 units around a pillar; stock `airdefense1`
  was edited, rebuilt (1,170 areas, 242 tactical features) and played with
  tactical AI taking cover. Two engine defects found on the way are fixed: a
  leak in a map whose folder was not beside the engine aborted `dmap`, and the
  dedicated server then hung in shutdown.
- [x] **TOOL-04 — P2, qualification: authoring reliability.** Cover forced I/O
  failure, external edits, atomic saves, recovery, repeated undo/redo, renderer
  switches and Windows/Linux/macOS workflows. Declare startup/edit/selection/view
  latency budgets. Keep the known unbuilt legacy Radiant defects distinct from
  the supported experimental workspace and the separate UI visual editor.
  Reproduce and fix workspace labels disappearing after full video restart on
  GL/Vulkan; Vulkan reactivation helping one run does not close both backends.
  Closed 6 November ([qualification](level-editor-modernization.md#milestone-6-qualification-tool-04)):
  Fault-injected storage covers short writes, sync, backup and
  promotion failures and conflicts (2,938 document checks), and build staging
  and promotion failures roll back; external edits are detected while a map
  is open; 2,000 randomized edits and gestures undo to the original bytes.
  The workspace phase covers a renderer switch, a resized restart, view state
  across a client restart and the declared budgets on `airdefense1`: startup
  6 s, edit 100 ms, selection 16 ms, view drawing 16 ms at the 95th percentile
  and validation 250 ms, all met on both backends. Windows is qualified end to
  end; Linux passes the native suites under GCC and Clang sanitizers and the
  build-and-play phase on a GCC build under Xvfb; macOS is qualified through the platform evidence gate.
  The restart label defect no longer reproduces with the old or the new
  editor on either backend. Legacy Radiant defects stay recorded as unbuilt
  source, apart from this editor and the UI visual editor.

## Deferred design decisions

These preserve uncompleted ambitions from old plans without presenting them as
promised features or retail compatibility defects. Promote a decision into a
workstream with scope, dependencies and acceptance before implementation.

### OPT decision checkpoints

**Not scored: nine deferred decisions.** Existing experiments or related systems
do not establish an accepted delivery scope for these ambitions. To advance one:

1. **Establish the need and boundaries.**
   - OPT-01/OPT-02/OPT-03 need authoritative-clock or measured rendering workloads,
     target hardware and rollback constraints; more FPS alone does not justify a
     simulation change or GPU feature.
   - OPT-04/OPT-09 need capacity, privacy, security and service-identity decisions
     before broadcast, competitive services or further Steam services are promised.
2. **Define the complete product work.**
   - OPT-05 needs objectives/scoring, networking, bots and UI together; exposing a
     gametype name is not implementation.
   - OPT-06 needs a separate Awakening content project; unreachable weapons/content
     are not a defect in the accepted integrated campaign scope.
   - OPT-07 follows retail audio qualification and requires an authored-mix policy.
   - OPT-08 needs a concrete benefit, maintenance decision and source/licence review.
3. **Select and track an increment.** Record dependencies, exclusions, implementation
   and acceptance under a stable work item in this roadmap before assigning a
   completion estimate. Until then, no OPT item blocks an unrelated supported release.

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
| OPT-09 | Further Steam services beyond STEAM-02 through STEAM-05: lobbies and matchmaking, Steam relay/NAT traversal, Workshop mod distribution, Steam Cloud saves, achievements and stats, and Steam avatars in scoreboards (which needs a Steam ID exchange and its own privacy design). Each depends on the STEAM-01 app identity, and several need app-owner configuration. |

## Release qualification and completion rules

### REL completion checkpoints

**Estimate: 35%; 0/4 tasks closed.** Existing validators, release-status policy and
scoped staged-runtime tests are foundations. Tests on different revisions do not
combine into an accepted final package, and this documentation update promotes no
release or support tier.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| REL-01 — Partial | Meson/staging, package validators and scoped SP/MP/dedicated tests exist; complete-history build stamping repaired. | One exact candidate matrix across selected platforms/roles; stock assets, transitions, save/demo and clean-install/upgrade/config preservation. |
| REL-02 — Partial | Engine-capture and diagnostic infrastructure, scoped visual/audio evidence and human-review procedures exist. | Matched visual acceptance, actual motion/latency, audible sound, accessibility and VR comfort on physical devices with required permissions. |
| REL-03 — Partial | Many targeted lifecycle/failure fixtures and runtime fixes exist. | Coherent final-candidate cold/warm/long-session, asset/cache/network/save/device/surface/suspend/shutdown matrix with bounded resources and safe recovery. |
| REL-04 — Partial | Withdrawal policy, release-status page, curated unreleased material and support/promotion validators exist. | Reconcile the accepted candidate's roadmap/registers/matrices/notes; exclusions, migration guidance, signing/symbol/licence/package checks and promotion decision. |

Completion order for the selected release:

1. Freeze the intended supported subset, exclusions, source and package identities.
2. Run functional and lifecycle checks on those packages, retaining failures and
   rerunning affected configurations after fixes/restaging.
3. Obtain the physical/human evidence required by that subset; do not infer it from
   synthetic tests, hidden runs or unrelated historical packages.
4. Update acceptance/support records and player release notes together; promote only
   the proven subset. Optional/deferred features do not block unrelated support.

### REL task evidence and acceptance

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
