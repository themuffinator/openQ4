# openQ4 development roadmap

> **Development status:** This document describes the current source tree and its evidence, not the contents of the latest available 0.15.0 package. The 0.14.0 release was withdrawn. See [release status](../user/release-status.md).

Updated 8 November 2026 with the frame-rate recovery category (FPS),
21 November 2026 with the controller experience category (PAD) and a staged
splitscreen plan (MP-25), and 22 November 2026 with a gap review of the
ray-tracing category (RT). The progress
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

| Progress measure, 15 November 2026 | Current result | Meaning |
| --- | --- | --- |
| Formally closed roadmap tasks | **46 / 167 (27.5%)** | **121 remain open**. Counts all stable top-level task checkboxes, including Q4-01 through Q4-09, FPS-01 through FPS-10 and PAD-01 through PAD-10, not partial substeps. |
| Estimated category progress | **21 categories: 3 at 100%, 16 partial, 2 at 0%** | Governance (GOV), engine loading (CORE) and developer map tools (TOOL) are complete; the detailed estimates follow. |
| UI active requirements | **0 / 312 verified; 117 partial, 195 pending** | Excludes 40 superseded rows from the 352-row register. |
| UI corpus and final gates | **0 / 358 migration variants accepted; 0 / 7 final gates accepted** | Working screens and passing checkpoint validators do not grant product acceptance. |
| Quality frame-time reference | **Gate still failing** | Recorded Vulkan reference: 8.76 ms mean, 13.80 ms p99, 24.08 ms maximum against **every frame <=6.67 ms**. Later prototype runs do not close it. |
| Ultra frame rate at 2560x1440 | **91-188 fps on GL, 96-188 fps on Vulkan, on the reference laptop; every tier meets its refresh class** | FPS-02 re-tiered the presets to refresh classes (Enhanced 144/90, Quality 100/60, Ultra 75/45 average/1% low) and all three meet them in every scene on both backends; the previous everything-on Ultra (19-58 fps on GL) is now the Maximum preset with no frame-rate target. Budgets are in [`performance-budgets.json`](performance-budgets.json). See the FPS category and the [performance report](performance-report.md). |
| Release acceptance | **0 / 4 REL tasks closed** | 0.15.0 (November 2026) is the available release. It shipped ahead of these gates, which stay open for final-package acceptance; later source progress is unreleased. Since 23 November one frozen package per candidate runs a [release qualification](release-qualification.md) matrix: the Windows x64 0.16.0 candidate C3 passes it, after C1 and C2 exposed lost player settings, missing renderer symbols and starving audio streams, all fixed; other platforms, the human reviews and promotion remain. |

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
| [Vulkan rendering](#vulkan-rendering) — VK | **60%** | 1 / 8 | Driver breadth, visible pacing, material/HDR/capture combinations and resource soaks; HDR display output (VK-08, not started). |
| [Vulkan ray tracing](#vulkan-ray-tracing) — RT | **0%** | 0 / 7 | Scope, budget and licence decisions, acceleration structures, hybrid effects, path tracing, denoising and qualification. |
| [Modern rendering engine](#modern-rendering-engine) — REN | **65%** | 5 / 13 | Aggregate lighting ownership, combined-effect/performance qualification and platform breadth; shadowed volumetrics (REN-09, partial). GTAO (REN-10) and parallax occlusion mapping (REN-11) closed 21 November 2026; temporal AA and dynamic resolution (REN-04) closed 23 November 2026; depth of field (REN-12, partial) 23 November, with scripted cinematic focus to follow (REN-13). |
| [Frame-rate recovery and performance budgets](#frame-rate-recovery-and-performance-budgets) — FPS | **40%** | 4 / 9 | Shadow-map cost, equal-settings regressions (the retained HUD's composition and most of the Vulkan depth capture removed 22 November, unmeasured in fullscreen), hitches, front-end cost and the release gate. |
| [Virtual reality](#virtual-reality) — VR | **75%** | 4 / 7 | Real-headset gameplay, comfort and frame budgets; standalone Quest needs a headset (AND-05); desktop stereoscopic 3D on physical stereo displays (VR-07). |
| [Android and mobile rendering](#android-and-mobile-rendering) — AND | **70%** | 1 / 5 | Device/lifecycle breadth, setup upgrades, mobile UI qualification and Quest on a headset. |
| [iOS, iPadOS and visionOS](#ios-ipados-and-visionos) — IOS | **5%** | 0 / 6 | Adoption decisions, patch integration, platform builds and hardware acceptance. |
| [New user interface](#new-user-interface) — UI | **30%** | 1 / 11 | Production toolkit/editor, full source-bound migration, input/accessibility and seven gates. |
| [Retail compatibility and campaigns](#retail-quake-4-compatibility-and-campaigns) — Q4 | **75%** | 1 / 9 | Full playthroughs, effect parity, broader restore graphs, platform/save evidence and campaign-demo breadth. |
| [Enhanced rigid-body and ragdoll physics](#rigid-body-and-ragdoll-physics) — PHYS | **90%** | 3 / 6 | CPU overruns on two of five workloads per platform (bounds proposed as exceptions, not accepted; `sp-ragdolls` on Linux now 65 %); the full Linux pass repeated on the build with exact math (cut short on 17 November); a windowed review in motion and the default decision. |
| [Multiplayer, bots, Arena and demos](#multiplayer-bots-arena-and-demos) — MP | **55%** | 3 / 26 | Human play, physical workflows and deferred peer qualification; the 15 November reference-parity items (casual match flow, pools, modifiers, HUD, spectating, identity, moderation, discovery, movement), the demo container, workflow, storage and robustness items, MP-25 splitscreen and MP-26 Horde; retain MP-02's accepted timing caveat. |
| [Audio and voice](#audio-and-voice) — AUD | **80%** | 0 / 7 | Listening/microphone/device checks, mobile memory and dialogue/subtitle coverage. |
| [Controller experience](#controller-experience) — PAD | **65%** | 0 / 10 | Built and qualified with in-process virtual controllers: the active controller chosen by use, stick aim in degrees per second with presets, aim assistance, gyro in its own units, context buttons, layouts and separate resets, trigger and light feedback. Open: the on-screen keyboard, the setup panel, stock-menu rows, multiplayer captures, the feel reviews and every physical run; PAD-10 optional. |
| [Desktop platforms](#desktop-platforms-and-packaging) — PLAT | **55%** | 0 / 5 | Deck/ARM64/macOS physical signoff and package/device lifecycle breadth. |
| [Packaging improvements](#pkg-completion-checkpoints) — PKG | **10%** | 0 / 6 | Update archives, pack split/trim, overlays and delta delivery. |
| [Steam integration](#steam-integration) — STEAM | **0%** | 0 / 6 | Identity/licence/distribution decisions before implementation. |
| [Engine loading, safety and compilers](#engine-loading-safety-and-developer-tools) — CORE | **100%** | 6 / 6 | None; macOS evidence stays with the platform gate. |
| [Developer map tools](#tool-completion-checkpoints) — TOOL | **100%** | 4 / 4 | None; macOS evidence stays with the platform gate. |
| [Release qualification](#release-qualification-and-completion-rules) — REL | **55%** | 0 / 4 | Packages and runs for the platforms other than Windows x64, human/device reviews, hardware lifecycle cases, signing and the promotion decision. |
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
| [#220: mouse and controller settings under Controls](https://github.com/themuffinator/openQ4/issues/220) | UI-01/PAD-06: the Mouse and Controller tabs, with Free Look, Toggle Zoom and Scope Sensitivity, moved to Settings > Controls in the development tree; not in 0.14.1. Close once a published build carries it. |
| [#244: rumble missing from weapon fire](https://github.com/themuffinator/openQ4/issues/244) | PAD-08: per-shot pulses, the lightning gun and gauntlet hum and damage thumps, mixed with sound-driven rumble, are in the development tree, as the issue's triage reply says. Await the reporter's retest on an Elite Series 2. |
| [#173: high-DPI menu cursor PR](https://github.com/themuffinator/openQ4/pull/173) | UI-02/PLAT-05: the widescreen clamp half landed on main on 16 November, credited to the contributor: a menu's cursor bounds no longer depend on the aspect mode of the GUI drawn last. The absolute-position routing was not taken: it conflicts with #207 and its zero-delta motion breaks the controller-to-mouse handoff. Remaining: requalify Retina sticking against current routing, keeping a non-zero motion signal and explicit console ownership. |
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
| [#217: hidef2k mod black helmets](https://github.com/themuffinator/openQ4/issues/217) | Q4-01: two causes. On the reporter's `J:` install, loose source textures make about 1,050 packed replacements look stale, so `airdefense1` loads 17.5 GB uncompressed against 3.5 GB from a copy of the same packs on `M:`; the reporter is finding them. On main since 17 November, BC7 files feed image programs (CPU decode, bit-exact against two reference decoders over 32,000 blocks) and `downsize(x, N)` takes `x`'s own DDS ahead of the stock `progimg/` result, so a pack's character, weapon and monster textures show; checked on GL and Vulkan with a BC7 test pack. Also on main since 17 November (render API 36): Quake 4's `progimg/` result is set aside when any input of the program resolves outside the shipped packs (`idFileSystem::GetFileOrigin`: retail PK4s by checksum, openQ4's own packs), and the rebuilt bump map is stored as DXT5 RXGB rather than RGBA8. A stock `airdefense1` load sets aside none of its 270 bump maps; with the test pack only the helmet's, whose cached DXT5 decodes to the pack's normal (X in alpha, Z in blue). Since 18 November, a reporter-side option for that 17.5 GB case: `image_useCompression 1` / `image_useNormalCompression 2` (Quake 4's names, default 0) block-compress textures the engine decodes (DXT1 when opaque, DXT5, RXGB bumps; cached under a `c1` name), and DX10-header BC1/BC3, BC4/BC5 and uncompressed DDS files load (BC4/BC5 decoded; BC5 rebuilds Z). Decodes are bit-exact against reference decoders in a GPU readback on GL; both options and the formats checked in `airdefense1` on GL and Vulkan. See [texture replacements](../user/texture-replacements.md#formats). |
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
| Modern rendering | Native GL/Vulkan PBR is default-on for authored PBR materials; stock materials stay classic. Shared classic rendering domains are qualified individually on Windows GL/Vulkan (REN-01) and remain default-off. Aggregate modern-visible lighting still has `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS=0`; these are different promotion decisions. |
| Temporal rendering | Automatic TAAU supports subnative bilinear/bicubic scaling and dynamic resolution on eligible desktop GL/Vulkan paths; crop/nearest modes are excluded, and VR takes only explicit per-eye TAA (`r_temporalAA 1`), without dynamic resolution. Rigid, skinned and first-person weapon motion vectors and reactive effect coverage exist. Dynamic resolution and GPU skinning remain opt-in. GLES has no temporal resolve. |
| VR | The active source implements PC OpenXR on OpenGL (GLX/WGL, and EGL on Wayland) and Vulkan, headset submission without desktop presentation, stereo, tracked input, room scale, standing/seated play spaces, crouch, two-handed aiming, world-GUI pointing, per-eye TAA, haptics, comfort controls and vehicle views, all qualified on the simulated runtime. A standalone Meta Quest flavor (AND-05) is integrated and checked on the emulator's simulated headset; real-headset qualification remains open ([checklist](vr-headset-qualification.md)). |
| Android | Experimental ARM64/API 24+ GLES3 client, SDLActivity host, in-tree game modules, signed APK/release verification and dependency licensing are implemented. Onboarding/import/export, touch, the per-orientation touch layout editor, the background suspension save and multiplayer resume are integrated with scoped S25/emulator evidence. A Meta Quest flavor (OpenXR on the GLES renderer) is integrated with emulator evidence only. Physical device/lifecycle breadth stays open under AND-01 through AND-05. |
| UI | Many retained screens and HUDs now run by default. The register preserves 352 rows, 312 active requirements and 40 superseded rows. UI-10 reconciled retail/base/Awakening mounts to 358 source/include-bound variants; all remain unaccepted for migration. Historical hashes and invalidated inventory acceptance are preserved. |
| Retail and Awakening | Canonical SP/MP sources and BSE are in-tree. Awakening is additive SP content under `q4xbase`, not a separate multiplayer module. Retail decl/BSE audits, stock transition chains and selected Awakening-state probes have scoped evidence; full playthroughs remain open. Saves now write portable v5 with Windows/Linux cross-load evidence while retaining legacy readers; other target qualification remains Q4-06/Q4-07. |
| Multiplayer | Dual-stack networking, pure asset checks and bounded rcon2 exist. Actual MP defaults are `net_mpLagCompensation 0` and `net_mpPredictMode 0`. Clients advertise bounded remote extrapolation, and server rewind subtracts it from the displayed pose age; the coupled implementation and remaining peer qualification are recorded under MP-07. Retail 2.85 interoperability is outside the current openQ4 2.42 protocol contract. |
| Platforms | Windows x64 and Linux x64/Steam Deck are first-class platform targets; macOS arm64 and Linux ARM64 are previews; Windows ARM64 and Android are experimental. iOS, iPadOS and visionOS run only in an external port pinned to withdrawn `v0.14.000`; integration is IOS-01 through IOS-06. Renderer tiers are separate from platform tiers. |
| Foundations and tools | The bounded job service, worker DDS prefetch, generated-cache experiments, native dmap/AAS commands, save-format compatibility readers and experimental map workspace already exist. Their remaining scope is defined below. |

## Delivery sequence and dependencies

| Stage | Work to conduct | Exit condition |
| --- | --- | --- |
| 1. Establish the current baseline — complete | GOV-01 through GOV-03, HF-01/HF-02, Q4-01, UI-10; scope/recovery review for VR-01/VR-06 and AND-01 | Current-source ownership, clocks, compatibility inventory, tests and evidence identities agree. [Closure evidence](baseline-evidence.md) retains reproduced failures and later-stage qualification limits. |
| 2. Close product gaps — in progress | UI-01/UI-02 plus the first UI-06 editor round trip, UI-07 measurements and UI-09 toolkit work, then UI-03/UI-04 at accepted scope; Q4-02 through Q4-05/Q4-07/Q4-08, MP-01 through MP-09, MP-12, MP-17 through MP-22, MP-24, AUD-01 through AUD-07, PAD-01 through PAD-03, PAD-05 and PAD-06; selected VR-01 and AND-01/AND-03 scope; preserve completed VR-06 | Required player flows work through the intended UI and runtime; remaining unsupported cases are explicit and fail safely. |
| 3. Improve motion and rendering — in progress | Preserve closed HF-03 through HF-06 and VK-03; finish HF-07/HF-08, remaining VK-01 through VK-07 scope, REN-01 through REN-08 and FPS-01 through FPS-10 | Visible gameplay meets declared correctness, latency and frame-time budgets with rollback paths intact. |
| 4. Complete tools and broader quality | Remaining UI-06 scope, CORE-01 through CORE-06, PHYS-01 through PHYS-06, STEAM-01 through STEAM-05, IOS-01 through IOS-04, remaining P2 items, including MP-10, MP-11, MP-13 through MP-16, MP-23, MP-25, MP-26, PAD-04, PAD-07, PAD-08, PAD-10, VK-08, REN-09, REN-10 and RT-01 through RT-06 (TOOL-01 through TOOL-04 closed 6 November 2026, REN-11 on 21 November 2026) | Authoring and runtime workloads meet their stated completion gates; optimizations are justified by measurements. |
| 5. Qualify packages and promote scope | PLAT-01 through PLAT-05, PKG-01 through PKG-05, STEAM-06, IOS-05/IOS-06, REL-01 through REL-04, UI-07/UI-08/UI-11, VR-02, RT-07, PAD-09, AND-02 and selected AND-05 scope | Exact release packages pass the selected platform, device, gameplay, visual and human acceptance matrix. |

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
| Quest VR integration | VR-01's PC Vulkan/EGL integration and VR-06 lifecycle are in place; AND-01's flavor-aware package checks are in place; the standalone flavor is integrated (AND-05, 12 November), and its headset qualification and distribution decision remain AND-05. PC headset qualification (VR-02) can proceed independently. |
| Android sustained operation | AND-02 includes AUD-03/CORE-03 aggregate memory and cancellation behavior, not only renderer FPS. |
| Modern-renderer promotion | Each REN-01 shared-domain gate is separate from REN-02 aggregate visible-lighting ownership. Keep their defaults and rollback decisions independent. |
| Preset re-tiering (FPS-02) | FPS-01's recorded budgets first; the FPS-03, FPS-04 and FPS-05 cost reductions decide what each tier can afford. HF-07's Quality floor and FPS-02's Ultra budget are measured by the same suite on the same reference machine; neither is qualified from a hidden or windowed capture. |
| Content pack split, trim and overlays (PKG-02 through PKG-04) | PKG-01's verifier first proves identical bulk-pack bytes across platforms and consecutive candidates. Client-only packs need their pure-server exemption before the dedicated server drops them. |
| Enhanced physics default (PHYS-06) | PHYS-01's recorded baseline and budgets first; PHYS-04's behavior envelope and PHYS-05's save/multiplayer/demo integration must pass before `g_enhancedPhysics` defaults on. `0` always runs the unchanged legacy path. |
| Steam integration (STEAM-02 through STEAM-06) | STEAM-01's app-identity, licence and distribution decision first. Joining and invites use MP-08's interoperability contract and the existing MP join flow. Steam absent or disabled always runs the unchanged path. |
| iOS and visionOS (IOS-02 through IOS-06) | IOS-01's patch inventory and build, layout and distribution decisions first. Shared fixes land through IOS-02, with regression evidence on the existing platforms, before the Apple targets build on them. visionOS stereo shares the VR stereo front end (VR-01/VR-06). Device qualification needs Apple hardware this project does not have. |
| Portable saves (Q4-06) | Q4-05's restore validation and Q4-07's historical readers stay authoritative for existing saves. Cross-platform qualification on a target follows that target's platform evidence (PLAT/AND), and the stages run in their listed order. |
| Casual match flow, moderation and ranking (MP-12, MP-17, MP-18) | MP-17's identity decision before bans keyed to identity, reconnect restoration and ratings; casual timeouts and votes reuse the managed `MatchTime()` pause and proposal pipeline rather than a second one. Address bans, flood control and the inactivity policy need no identity. |
| Demo container, workflow, storage and robustness (MP-21 through MP-24) | MP-21's container and registry before MP-22's client recording and MP-23's MVD compression; MP-24's fixtures accompany every format change. MP-05 stays closed at its selected scope, and every version a release wrote keeps its reader. |
| Multiplayer splitscreen (MP-25) | Multiplayer only: the single-player game and the Awakening campaign never gain local players. Each local player is an ordinary client on the wire, so MP-08's protocol, MP-17's identity, MP-18's per-client caps and MP-22's client recording apply per player without a second path. Per-viewport HUD and menus build on the retained interface (UI-03/UI-04/UI-09); per-viewport cost is measured against FPS-01's budgets before any preset enables effects in splitscreen. Seats build on PAD-01's device layer, and chat entry on PAD-05's keyboard. Stages 1 to 5 (listen server) need no protocol change. Stage 6's remote transport (decision D1) follows MP-08's packet budget and MP-21's registry. |
| Horde (MP-26) | Monster snapshot state is a protocol change: it follows MP-08's packet budget and MP-21's registry and keeps every older demo playable. Wildcard waves reuse MP-14's modifier layer, maps and presets go through MP-13 and MP-14, bots as fighters build on MP-09, and Horde stays unranked under MP-17. The stock Quake 4 roster only; Awakening content stays scoped to its campaign. |
| Vulkan ray tracing (RT-02 through RT-07) | RT-01's hardware, budget, licence and translation decisions first. Ray tracing builds on VK-03 admission, VK-06 resource counters and the existing TAAU, motion vectors and GPU skinning paths; RT-06 follows the VK-05/VK-07/VK-08 capture and HDR work and VR-01 stereo. `r_rayTracing 0` always runs the unchanged Vulkan path, and no other renderer depends on it. |
| Shadowed volumetrics, GTAO and parallax occlusion (REN-09 through REN-11) | REN-10 and REN-11 closed 21 November 2026. REN-09 samples the REN-03 shadow maps and replaces the REN-06 froxel leaf; REN-10 keeps SSAO and its REN-05 controls as the fallback and comparison. Each is default-off with exact rollback, and FPS-01's budgets price every tier before a preset enables it; a new preset setting carries the SYSTEM catalog ripple. |
| HDR display output (VK-08) | VK-05's exposure domain and tone curves first. The 6 November display-referred decision for stock content stands, so VK-08 changes the output transform and interface composition, not scene composition. Vulkan only: OpenGL, GLES and VR stay SDR and report it, and HDR output off always presents the unchanged SDR path. A new display setting carries the SYSTEM catalog ripple; qualification needs a physical HDR display. |
| Server discovery (MP-19) | A decision on a project-controlled master or list service before the Internet source can be qualified on a fresh install; Steam lobbies and matchmaking remain STEAM/OPT-09. |
| Controller experience (PAD-01 through PAD-10) | PAD-01's every-pad device layer is the one MP-25's seats use, and PAD-08's player LEDs follow those seats. PAD-02 and PAD-04 share one look transform with HF-03's late look. PAD-03's input-source flag and multiplayer setting follow MP-08's protocol and MP-21's demo registry, and stay off in the competitive rulesets. PAD-05's keyboard is the fallback STEAM-05 defers to Steam's keyboard where it reaches it. PAD-09 is the controller record UI-02, the PLAT signoffs, AND-02 and the MP human checks cite. Mouse and keyboard behavior never changes. |

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

**Estimate: 60%; 1/8 tasks closed.** Windows NVIDIA/Intel qualification and bounded
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
| VK-08 — Not started | Only an 8-bit sRGB swapchain; the RGBA16F scene, stock and PBR tone curves and automatic exposure exist (VK-05). | HDR10/scRGB swapchain and metadata, SDL3 display state with live switching, a peak-targeted output transform, paper-white interface composition, settings and calibration, SDR captures, and review on a physical HDR display. |

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
  Progress, 16 November: device loss under video-memory pressure (#217, #231:
  an uncompressed 16 GB texture pack on a 12 GB NVIDIA card) now degrades
  instead of wedging. Mipmapped material textures give up top levels as the
  device-local heap passes 80/90/97% of `r_vkTextureMemoryLimit` (default 90%
  of the `VK_EXT_memory_budget` budget), and retry one level smaller on an
  out-of-memory result. A blocked device takes no new images or staging,
  refused staging is freed at once, and a level load stops decoding until the
  queued restart. Verified on the RTX 4060 laptop with a 7.8 GB test pack under
  `r_vkTextureMemoryLimit 2500` and with the presentation-failure drill during
  a load, through to the next-launch OpenGL fallback. The NVIDIA desktop loss
  itself and an unlimited oversubscription run were not reproduced; reporter
  confirmation on the original hardware remains open.
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
  capabilities (HDR monitor output is VK-08). Share temporal/shadow acceptance with REN-03/REN-04 and HF-06.
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
- [ ] **VK-08 — P2, implementation: HDR display output.** Present on HDR10 and
  scRGB displays. Today the swapchain is always 8-bit sRGB
  (`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`) and both tone curves end at SDR white,
  so the RGBA16F scene of VK-05 never reaches the display as HDR.
  Swapchain: enable `VK_EXT_swapchain_colorspace` where present and choose HDR10
  (10-bit, `HDR10_ST2084`) or scRGB (FP16, `EXTENDED_SRGB_LINEAR`) from what the
  surface reports, with `VK_EXT_hdr_metadata` where the device offers it; every
  other surface keeps the SDR swapchain. Display state: read SDL3's HDR state,
  SDR white level and headroom for the window's display, and follow changes
  (the operating system's HDR switch, a move to another monitor) by recreating
  the swapchain, without `vid_restart` or an application restart.
  Output transform: retarget the stock and PBR filmic curves to the display's
  peak instead of 1.0, convert Rec.709 to Rec.2020 and PQ-encode for HDR10, and
  scale scRGB so paper white lands where the user set it. The 6 November
  decision stands: stock content stays display-referred, and its added range
  comes from the curve's shoulder, not from re-blending classic stages in linear
  light. Exposure, auto exposure, bloom and TAA keep their scene-domain
  behaviour. Composition: authored post effects, SMAA, the HUD, menus and the
  retained interface, subtitles, cinematics and loading screens stay SDR and are
  placed at a paper-white level in the output pass; no SDR element exceeds it.
  Settings: HDR output (automatic or off), peak brightness, paper white and a
  calibration screen, following the [UI visual design](ui-visual-design.md) with
  `#str_*` entries in every language table, and requested and effective state
  with the reason for any fallback in `gfxInfo` and `rendererVulkanHDRInfo`.
  Captures: screenshots, levelshots, save previews, `envshot` and light-grid and
  probe bakes stay SDR and match the SDR presentation; an HDR capture format is
  a separate decision. Boundaries: OpenGL, GLES and Apple GL stay SDR and say
  so; VR presents SDR through OpenXR; MoltenVK, Linux and Steam Deck OLED
  (gamescope) qualify only where their Vulkan surfaces report HDR colour
  spaces, and otherwise fall back. Acceptance: on a physical HDR10 monitor
  under Windows, and on the scRGB path, stock `airdefense1` and a PBR laboratory
  scene show highlights above paper white without clipping or hue shifts; the
  interface at paper white matches its SDR appearance; switching HDR in the
  operating system and moving between SDR and HDR monitors recover cleanly with
  no stale swapchain; HDR output off is byte-identical to the current SDR
  presentation; captures are unchanged; and the output pass is priced under
  FPS-01. Qualification needs an HDR display and the owner's review.

## Vulkan ray tracing

Sources: [Vulkan device selection](vulkan-device-selection.md),
[authored programs](vulkan-material-programs.md), [HDR](vulkan-hdr.md),
[GPU skinning](gpu-skinning-modernization.md), [capability matrix](engine-capability-matrix.md),
`src/renderer/Vulkan/` (`VulkanBringup.cpp`, `vk_Backend.cpp`, `vk_Interactions.cpp`,
`vk_ShadowMap.cpp`, `vk_PostProcess.cpp`) and the Quake II RTX (Q2RTX) renderer as
the external design reference.

Requested 15 November 2026. Quake II RTX showed that a classic id Tech game can be
rendered with real-time ray-traced lighting, shadows, reflections, refraction and
indirect light from its original assets. This work brings that class of rendering
to openQ4's Vulkan renderer: first ray-traced effects on top of the existing
rasterized frame, then a full path-traced mode. The current Vulkan baseline
(Vulkan 1.3 with dynamic rendering, synchronization2, timeline semaphores and
VMA) enables descriptor indexing and buffer device addresses at device creation
but uses neither yet; the
[gap review](#what-quake-ii-rtx-does-and-what-id-tech-4-changes) below lists what
ray tracing still needs. Every item honors six product rules:

1. **Gated behind a CVar.** The archived CVar `r_rayTracing` (`0` off, `1` hybrid
   effects, `2` path tracing; default `0`) is the only switch, with localized
   Settings rows in the retained and stock interfaces. Nothing turns it on
   implicitly: presets leave it at `0` unless RT-07 decides otherwise. Value `0` runs the
   current Vulkan renderer unchanged, with byte-identical captures. OpenGL, GLES,
   dedicated servers and devices without the required extensions are never
   affected; they show the rows as unavailable instead of failing.
2. **Stock assets only.** The ray tracer reads shipped Quake 4 materials, lights,
   models and maps as authored. Emitters, roughness and translucency are derived
   from existing material stages, light shaders and textures by engine rules, not
   from replacement or per-map content. Authored PBR materials keep their
   meaning. Any table of derived material properties ships with the executable.
3. **Presentation only.** Ray tracing never changes simulation, collision,
   network protocol, pure checks, saves or demos. Multiplayer clients may use it
   freely; a server never needs it, and render demos replay identically with it
   on or off apart from the image.
4. **Capability-gated and recoverable.** Ray tracing requires
   `VK_KHR_acceleration_structure` with `VK_KHR_ray_query` (hybrid) and, where
   chosen, `VK_KHR_ray_tracing_pipeline`. Missing extensions, allocation
   failure, device loss or a driver refusal fall back to the rasterized path for
   that session with one log line, following VK-03's bounded admission and
   cleanup rules. Platforms whose drivers lack these extensions (current
   MoltenVK, most Android GPUs) are out of scope until they gain them.
5. **Measured and licensed.** Each mode declares its reference hardware and
   frame-time budget before promotion, using the FPS suite. Q2RTX and any
   NVIDIA, AMD or Intel denoiser, light-sampling or upscaling SDK are reviewed
   file by file for GPLv3 compatibility before code is ported; incompatible code
   is reimplemented from published techniques, and proprietary SDKs stay
   optional decisions. Credits follow the attribution rules.
6. **Faithful to the authored lighting.** Required 22 November 2026. A level
   must read as it does in the classic renderer. Each light's authored color,
   intensity, projection, falloff and bounds stay the direct term, and what ray
   tracing adds (traced shadows, bounce light, emitters, soft penumbrae) is
   calibrated so that no area becomes drastically darker or brighter than the
   original. The engine derives that calibration from each level's own lights
   and materials when the map loads, never from hand-written per-map tables.
   Fidelity is measured as well as reviewed: at fixed viewpoints and the classic
   exposure, with automatic exposure off, the ray-traced frame's luminance is
   compared with the classic capture as a whole and by screen region, against
   the tolerances RT-01 records.

### What Quake II RTX does and what id Tech 4 changes

Reviewed 22 November 2026 against Q2RTX's `src/refresh/vkpt/` and openQ4's Vulkan
backend at `e2c580b29`. Q2RTX path-traces a game whose lighting was baked, so it
derives its emitters from textures and keeps the whole map on the GPU. Quake 4
lights every surface from authored dynamic lights through scripted materials, and
its renderer builds only what the view can see. Those differences, more than the
path tracer itself, are the work. This table feeds RT-01; it records gaps, not
decisions.

| Area | Quake II RTX | openQ4 today | Needed (task) |
| --- | --- | --- | --- |
| Look | Relights the game with new physically based textures, a physical sky and automatic exposure. Matching the original brightness was not a goal. | The authored lighting is the product, and stock views stay display-referred (6 November decision). | Rule 6: authored lights as the direct term, calibrated additions and a luminance comparison against classic (RT-01, RT-03, RT-04, RT-07). |
| Geometry on the device | The static world sits in one device buffer and a compute pass writes animated models into a per-frame buffer. Static and per-frame bottom-level structures feed one top-level structure a frame. | The Vulkan module keeps the vertex cache in CPU memory and copies each frame's drawn surfaces into per-frame rings (96 MiB of vertices, 8 MiB of indexes). No geometry persists on the device, no buffer has a device address and VMA is created without its device-address flag. | Persistent device geometry for static world and model surfaces; a per-frame buffer for skinned, deformed and generated surfaces with their previous positions; build-input usage on both (RT-02). |
| Scene extent | The whole map and every entity are in the structure, so geometry behind the camera casts shadows and shows in reflections. | The front end instantiates dynamic models and runs entity callbacks only for entities the view's portal walk, or a light visible in it, reaches. | A ray-tracing scene set that reaches beyond the view, bounded by area connectivity and distance, with its CPU cost priced (RT-02). |
| Textures and materials | Every image is in one global texture array (8,192 slots) with one material record per surface. Physically based properties come from replacement textures NVIDIA ships. | Descriptor sets are bound per draw; nothing is bindless. Materials are multi-stage scripts with per-entity expression registers, texture matrices, vertex color, alpha-test thresholds, authored programs and deforms. | A bindless image array and a per-frame material table holding evaluated registers (RT-02). Rules that derive roughness, metalness and emission from stock stages, since replacement textures are excluded (RT-01). |
| Lights | Emissive map polygons listed per visibility cluster, up to 32 dynamic sphere lights, a sun and a physical sky. The map's own light entities were baked and are not used. | Authored point and projected lights with projection and falloff images, animated light shaders and box-bounded falloff that is not inverse-square. Fog, blend and ambient lights. Glow stages accompany a light instead of replacing it, and fill lights imitate bounce light. | A device light table that samples the authored images, with light lists per portal area or cluster (RT-02, RT-05). Decisions on double counting, light size and falloff bounds (RT-01). |
| Shadow rules | Every opaque triangle occludes. | `noShadows` materials, lights and entities, `noSelfShadow`, shadow-only hull surfaces, and per-view suppression of the player's body and view weapon. | Instance masks and ray flags that reproduce those rules, including the view a ray belongs to (RT-01, RT-03). |
| Primary visibility | Primary rays write a visibility buffer and everything is shaded from hits. | Decals, model overlays, coplanar polygon-offset surfaces, deforms, the weapon depth hack and authored vertex programs depend on rasterization. | A decision between rasterized primary visibility feeding traced lighting, and traced primary rays with those cases rebuilt (RT-01, then RT-03 and RT-04). |
| Screens in the world | Security-camera monitors are traced. | Interactive GUIs, video maps, remote cameras and mirrors are surfaces redrawn every frame. | Render them to textures before tracing so rays see them as emitters; mirrors become reflection rays (RT-04, RT-06). |
| Effects | Particles, beams, sprites and explosions get their own structures and any-hit shaders. | BSE effects, particle models, beams and sprite deforms are built on the CPU facing one view. | Composite from the raster path first; separate masked instances only where reflections need them (RT-04). |
| Denoising and reconstruction | A-SVGF over separate diffuse and specular signals, temporal upscaling and FSR 1. | TAAU, rigid, skinned and view-weapon motion vectors and reactive coverage exist. No per-signal denoiser. | A denoiser fed by the existing vectors, with last frame's surface identity for disocclusion (RT-05). |
| Ground truth | A reference mode accumulates frames into a converged image. | Laboratory maps and deterministic capture exist, with nothing converged to compare against. | The same accumulation as the oracle for sampling and denoising, as REN-10's brute-force reference is for ambient occlusion (RT-04, RT-05). |
| Shaders | GLSL ray-generation, hit and miss stages compiled at build time; ray pipelines or ray queries chosen at startup. | SPIR-V ships in committed headers pinned by `vk_shader_header_pin.py`, so builds need no shader compiler. No ray-tracing stage exists. | Ray-tracing stages in the generator and its pin; RT-01 chooses ray queries, ray pipelines or both (RT-02). |
| Licence | In the reviewed checkout, 40 of 41 `vkpt` C sources and 69 of 70 shaders state GPL version 2 or any later version. FSR 1 is MIT. Bundled media has separate terms. | GPLv3 engine. | The headers permit porting in principle. RT-01 still reviews each file and embedded table before any is taken. |
| Hardware | Not applicable. | The development laptop's RTX 4060 (8 GB) is the only ray-tracing device available; its Intel Iris Xe lacks the extensions. | RT-01 sets budgets for an 8 GB card. RT-07's second vendor needs hardware this project does not have. |

### RT completion checkpoints

**Estimate: 0%; 0/7 tasks closed (22 November 2026).** Scope and a gap review
only; no ray-tracing code exists. The Vulkan 1.3 device baseline, motion vectors,
TAAU, HDR scene targets and GPU skinning compute paths are existing dependencies,
not credited RT progress.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| RT-01 — Decision pending | Product rules, mode CVar and capability gate proposed. Gap review against Q2RTX and the Vulkan backend recorded (22 November). | Reference hardware and budgets, Q2RTX/SDK licence review, material/light translation rules, the primary-visibility, double-counting and ray-interface decisions, rule 6's luminance measure, tolerances and load-time calibration, Settings contract and reference scenes. |
| RT-02 — Not started | Vulkan 1.3 baseline with VMA exists; descriptor indexing and buffer device addresses are enabled but unused. | Extension admission and fallback; persistent device geometry, a bindless image array, material and light tables and a scene set beyond the view; BLAS/TLAS for world, entities, skinned and deformed surfaces; memory budgets and lifetime across map change, restart and device loss. |
| RT-03 — Not started | Shadow maps, stencil shadows and SSAO provide the rasterized references. | Ray-traced shadows for interaction lights, ambient occlusion and reflections on the raster frame, each with its own fallback. |
| RT-04 — Not started | Specified only. | Path-traced mode: id Tech 4 lights and derived emitters, sky, indirect bounces, translucency, water and glass, in-world screens as textures, raster overlays for GUIs, particles and the view weapon, and a converged reference mode. |
| RT-05 — Not started | TAAU, motion vectors and reactive coverage exist. | Light sampling, temporal and spatial denoising checked against the converged reference, reconstruction with TAAU and dynamic resolution, and an open upscaler. |
| RT-06 — Not started | Specified only. | Captures, HDR, subviews and mirrors, VR stereo, render demos, BSE, MP and the Settings UI in all language tables. |
| RT-07 — Not started | Specified only. | Budgets on reference hardware from at least two vendors, the luminance comparison on every stock map, image review against classic, SP/Awakening/MP coverage, Linux, soaks and the availability decision. |

### RT task evidence and acceptance

- [ ] **RT-01 — P2, decision: scope, budgets and translation contract.** Choose
  the reference ray-tracing hardware (at least one NVIDIA RTX device and one AMD
  RDNA 2 or later or Intel Arc device), the reference SP and MP scenes and the
  frame-time budget for each mode at declared resolutions. Record the minimum
  extension and feature set. Review Q2RTX and every candidate SDK for licence
  compatibility and record what may be ported, what must be reimplemented and
  the credits owed. Define the rules that translate stock content: interaction
  stages (diffuse, bump, specular) to a physically based approximation, light
  shaders with projection and falloff images, fog and blend lights, `noShadows`
  and suppression flags, alpha-tested and translucent stages, additive and glow
  stages as emitters, and the sky. Decide what the
  [gap review](#what-quake-ii-rtx-does-and-what-id-tech-4-changes) leaves open:
  rasterized or traced primary visibility; ray queries, ray pipelines or both;
  how a glow surface and the light beside it, and fill and ambient lights, avoid
  double counting once bounce light is traced; whether lights gain a size for
  soft shadows while keeping their authored bounds; and how shadow flags and
  per-view suppression map to instance masks. Budgets assume the 8 GB reference
  card. Define rule 6's fidelity measure: the reference viewpoints for each map,
  the statistic (proposed: mean log luminance of the frame and of each cell of
  an 8 by 8 grid), the tolerances (proposed: the frame within a third of a stop
  and every cell within one stop of classic, each exception recorded with its
  cause) and the load-time calibration that keeps bounce light and emitters
  inside them. Define the CVar and Settings contract and
  where hybrid effects replace rather than add to raster ones. Close when the
  contract is recorded in a ray-tracing reference doc and the reference scenes
  have classic captures.
- [ ] **RT-02 — P2, implementation: device admission, ray-tracing scene and
  acceleration structures.** Admit the RT-01 extensions without changing device
  choice for `r_rayTracing 0`, and fall back cleanly when any step fails. Keep
  static world and model geometry in persistent device buffers with device
  addresses, and write skinned, deformed and generated surfaces, with their
  previous positions, to a per-frame buffer. Publish every loaded image through
  one bindless array, and each frame a material table of evaluated registers and
  a light table. Build a scene set that reaches beyond the view, bounded by area
  connectivity and distance, which instantiates off-screen dynamic models
  without changing game state. Ray-tracing shader stages join the committed
  SPIR-V headers and their pin. Build bottom-level
  structures for the world (per area or per model), static and moving entities,
  skinned meshes (reusing the GPU skinning compute output, or CPU-skinned
  vertices when it refuses) and deformed or animated surfaces, with refit or
  rebuild chosen per surface, and a top-level structure per view. Bound memory
  with compaction and budgets reported through VK-06's resource counters.
  Structures survive or rebuild correctly across map change, `vid_restart`,
  renderer reload and device loss. Gate: the stock maps build without failures
  or leaks across a five-cycle soak, the scene set's CPU time and device memory
  are reported per map, and `r_rayTracing 0` captures stay byte-identical.
- [ ] **RT-03 — P2, implementation: hybrid ray-traced effects.** In mode `1`,
  using ray queries from the rasterized frame: ray-traced shadows for
  interaction lights that honor light projection, falloff, shadow flags,
  per-view suppression of the player's body and view weapon, and alpha-tested
  casters; ray-traced ambient occlusion; and reflections on
  surfaces the RT-01 rules mark reflective, including authored PBR materials.
  Each effect has a developer CVar and falls back to its raster equivalent.
  Gate: the reference scenes show no missing, detached or light-leaking shadows
  against the classic reference, view-weapon and subview cases included, within
  the RT-01 budget, and every reference viewpoint stays inside the RT-01
  luminance tolerances.
- [ ] **RT-04 — P3, implementation: path-traced mode.** In mode `2`, replace
  rasterized lighting with a path tracer in the manner of Q2RTX: primary
  visibility, direct light from every id Tech 4 light type and from derived
  emissive surfaces, sky and environment light, a bounded number of indirect
  bounces, and refraction and reflection through glass, water and other
  translucent materials. Material expressions, scrolling and animated stages
  and remote-camera textures evaluate as in the raster path. Interactive GUIs,
  video maps and remote cameras render to textures before tracing, so they
  light the scene and show in reflections. Full-screen GUIs, BSE
  particles, decals, the view weapon's depth hack and post effects that the
  path tracer cannot own are composited from the raster path with correct
  depth. A reference mode accumulates frames into a converged image for
  comparison. Gate: every stock SP and MP map renders without missing surfaces,
  black materials or uncovered pixels on the reference hardware, the
  reference mode converges on the reference scenes, and every reference
  viewpoint stays inside the RT-01 luminance tolerances.
- [ ] **RT-05 — P2, implementation: sampling, denoising and reconstruction.**
  Importance sampling for many lights and emitters (for example a ReSTIR-style
  reservoir scheme), a temporal and spatial denoiser that separates direct,
  indirect and specular signals, and reconstruction through the existing TAAU,
  motion vectors and dynamic resolution. Provide at least one upscaler with a
  GPLv3-compatible licence; vendor upscalers remain optional decisions. Gate:
  the reference scenes meet the RT-01 budget without visible smearing, ghosting
  on moving actors and the view weapon, or fireflies, in windowed review at play
  speed, and the denoised image's difference from RT-04's converged reference is
  recorded for each scene.
- [ ] **RT-06 — P2, integration: the rest of the renderer and the product.**
  Screenshots, `envshot`, tiled and multisampled captures (VK-07), HDR output
  and exposure (VK-05, VK-08), mirrors and remote cameras, VR stereo with per-eye cost
  (VR-01), render demos and capture determinism, BSE effects, light-grid packs
  and multiplayer views work in each mode or fall back explicitly. Settings rows
  follow the [UI visual design](ui-visual-design.md), use `#str_*` entries in
  every language table and report why a mode is unavailable. The capability
  matrix and player documentation describe the modes and their requirements.
- [ ] **RT-07 — P2, qualification and decision: availability.** On the RT-01
  reference hardware, under Windows x64 and Linux x64, run the FPS suite, the
  reference scenes, stock SP progression samples including Awakening `q4xbase`,
  MP with bots, and a long-session soak in each mode. Run the rule 6 luminance
  comparison on every stock map and review images against classic at play
  speed. Decide whether each mode is offered as experimental or
  supported, and whether any preset enables mode `1`; `0` stays the default and
  a supported fallback. Update the capability matrix, platform support and
  release notes with the decision.

## Modern rendering engine

Sources: [modernization history](archive/2026-10-18/idtech5-modernization-roadmap.md),
[visible-light ownership history](archive/2026-10-18/plans/2026-08-16-modern-visible-lighting-ownership.md),
[capability matrix](engine-capability-matrix.md), [GPU skinning](gpu-skinning-modernization.md),
[PBR production evidence](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md) and
[shadow production audit](shadowmapping-final-audit-2026-09-05.md).

### REN completion checkpoints

**Estimate: 65%; 5/13 tasks closed.** PBR, temporal features and qualified shared
rendering domains exist (REN-01). Aggregate modern lighting still has
`MODERN_LIGHTING_PARITY_PROVEN_DOMAINS=0`; neither feature presence nor domain
diagnostics establish whole-frame ownership or production performance.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| REN-01 — Closed | All nine shared classic domains own stock (and, for subview forms stock content never authors, validation-room) views on a fresh GL/Vulkan package under the default preset, with 4x MSAA and with the post chain off: byte-identical to classic, visible content controls, named zero-commit rollback. They now draw into the scene target the classic walker uses. Defaults stay off. | Human appearance review of the retained captures and physical Linux/macOS/Android runs belong to platform qualification; default promotion is a separate owner decision. |
| REN-02 — Partial | In the laboratory the classic-lighting corridor owns classic stage shapes, cutouts, baked light and stencil-shadowed lights, each matched against native ARB2, and the aggregate executor composes with the shared domains behind `r_rendererOwnershipCompose`. No stock view is owned yet; the remaining decline reasons are inventoried. | Take the stock shapes the census names (skinned receivers, lit surfaces without a forward command, stage colour), shadow-mapped lights and the point-cube pool; move the corridor onto the sealed interaction stream; then measure benefit on owned stock views before parity promotion. |
| REN-03 — Partial | Point/projected/CSM and hybrid shadow paths, caches and Vulkan translucent moments exist. A continuous-motion stock tour passes at default, cascaded and constrained budgets on GL/Vulkan with bounded slots and retained light; receivers after motion match a fresh render, as do moving doors, cutout and translucent casters under camera motion; static/live composition and cutout-alpha audits done (GL now falls back to stencil like Vulkan). The opt-in modern OpenGL receivers filter projected shadows with the classic kernel and PCSS-lite blocker search since 17 November; a widened PCSS radius now softens the same pixels as the classic receivers (84% overlap), where before it changed none. The 9 November [stencil-parity audit](shadowmapping-audit-2026-11-09.md) fixed prelight-only (`noDynamicShadows`) lights losing their shadows, `noSelfShadow` world geometry no longer shadowing `noSelfShadow` receivers under prelit lights, the one-sided five-tap kernel and the packed-depth wrap at the far envelope. Since 23 November a light whose source lies outside its own box (an off-centre point light, a parallel sun) casts the static world into its map from the front caps of its dmap prelight volume, the triangles its stencil shadow is built from (`r_shadowMapPrelightCasters`): against stencil, mcc_landing's `light_9083` goes from 48.9% of a view wrongly shadowed to 0.01%, `light_9081` from 10.4% wrongly lit to 0.22%, and q4cmp13's suns from 30.9% wrongly lit to 0.7%, on both backends, with the existing motion, mover and caster-cycle gates passing; the `noSelfShadow` world-geometry rule has its image on both backends. | Physical platforms; the quiet-machine FPS-02/FPS-03 budgets; what the audit leaves open for lights whose source lies outside their box: the 78 stock lights whose prelight volume folds its caps away keep uncut per-surface casters, the casters other than the static world under distant and parallel lights store the faces turned towards the light, and the far-shell caster lets light through to a receiver closer behind it than the map's bias, tens of units under a coarse distant map (1.0% of one view of `light_9083`, 1.9% of one of Air Defense 1's `light_7910_cursed`, up from 0.15%); and mcc_landing's `outdoor_Main`, a cube light 280 units outside its box, differs from stencil more with the caster than without (9.6% map-only and 9.9% stencil-only of its worst view on OpenGL, 8.8% and 8.9% on Vulkan, against 0.6% and 1.0% with `r_shadowMapPrelightCasters 0`; cause not established, caster kept on by default). The cascade range end is closed (19 November, whole-light last cascade). |
| REN-04 — Closed | Exact vectors, TAAU and dynamic resolution; cumulative resolve counters and a GL/Vulkan runtime gate (cuts, capture, pause, restart, failed presents, mirrors, stale timing, scale recovery, SMAA rollback). Mirror/remote-view, withheld-present and capture-depth gaps fixed; the picture is still at rest (#216). A temporally upscaled scene samples sharper texture mips (16-18% more detail at 50% scale, stability unchanged), and Vulkan corrects the NVIDIA driver's softer anisotropic filter, so its frames hold OpenGL's detail. | Human review of ghosting and disocclusion on the reference display and physical Linux/macOS/Android belong to platform qualification. |
| REN-05 — Partial | Authored PBR, animated/MD5R models, box probes and indirect-only SSAO have scoped evidence. SSAO parity controls repaired after half-resolution SSAO, with a matched half-resolution control; the Intel residual is reproduced and explicitly bounded; skin substitutions and a mixed PBR/classic model on packed MD5R match CPU skinning. lavapipe's one-byte difference is traced to its texel decode and bounded to lavapipe. The showcase map qualifies skins, a mixed PBR/classic model, cutout coverage and shadows on both renderers (67/67 each, procedural stand-ins). | The Mesa SSAO controls that fail since 21 November (not investigated); real textures.com materials (the owner's downloads); physical Linux/Apple fallback (PLAT). |
| REN-06 — Partial | Probe admission decided (RGBA8/sRGB/generated RGBA16F; compressed, oversized and authored HDR excluded); cold misses filter on a background job and publish within the 4 ms budget on both backends; SSR and SSGI pass individually and together with byte-exact master-off rollback on GL/Vulkan in seven stock scenes; a map's own baked probe, a probe captured in a stock scene and authored decals pass on both renderers; a scratch re-bake of one bundled grid is measured. | Re-bake of the cropped bundled grids (owner decision, evidence recorded); quiet-machine cost of the leaves beyond the first scene; other GPUs/platforms and appearance review (PLAT). |
| REN-07 — Partial | Device-aware presets, measured culling/HUD improvements and the opt-in Vulkan render thread exist: stage 1 (tic overlap) and stage 2 (front-end overlap) with retained index and static vertex streams. 23 November, Windows x64 Vulkan: the per-frame join that had removed all overlap since 6 November is fixed; frozen Quality captures are bit-identical between modes 0, 1 and 2 on four single-player views and within the mode 0 floor in multiplayer; 100 resize/scale cycles and a restart per mode stay within the memory, handle, cache and Vulkan-allocation bounds with validation clean. Snapshot work per mode 2 frame then fell 46% in a same-binary A/B (17.0 M to 9.2 M thread cycles): front-end-only streams are no longer frozen, frame-temporary indexes no longer thrash the retained index cache (under 1,000 entries, not 32,768), one lease per version per frame, reused per-view tables; parity unchanged. | Matched benefit without tail regression: five quiet paired mode 0/1/2 runs and the every-frame Quality floor (shared-machine runs show mode 1 ahead of mode 0; mode 2 moved from above mode 0 to mostly below it); vertex streams validated twice per static surface (`verts` beside the ambient cache); physical display/input qualification; Wayland/macOS runtime coverage. |
| REN-08 — Closed | Motion blur coexists with TAA, owns exact skinned/weapon vectors, is frame-rate independent and reports requested/effective state; 41 GL/Vulkan combination cases pass. | Physical Linux/macOS/Android and appearance review belong to platform qualification. |
| REN-09 — Partial | Per-light shadowed scattering replaces the REN-06 leaf on GL and Vulkan: falloff/projection images, point, projected and cascaded-sun maps, stencil-light policy (none by default), composite before fog and translucents, status reporting, exact rollback; frozen outdoor/indoor captures pass under TAA and 4x MSAA. 23 November 2026: moving casters and a moving flashlight, temporal history, resolution scale and dynamic resolution, mirrors and remote cameras, a backend decline and VR stereo pass on GL and Vulkan; scratch targets are pooled by view size. Evidence: [volumetric lighting](volumetric-lighting.md). | Per-tier cost against FPS-01 on a quiet reference machine (a shared-machine ladder is recorded), the owner's visual review of the defaults (contact sheets captured, no default changed; 12 whites out the Tram 1 opening), and physical Linux/macOS/Android runs with the Apple GL 2.1 and GLES declines (PLAT). |
| REN-10 — Closed | HBAO (`r_ssaoMethod 1`), GTAO (`2`) and a brute-force reference (`3`) beside SSAO on GL and Vulkan, sharing SSAO's occlusion target, separable filter, resolve and indirect-only PBR field; native PBR's multi-bounce and specular occlusion; classic multi-bounce; temporal pattern turn under TAA. GTAO closer to the reference than SSAO with fewer halos, HBAO as close as SSAO; backend parity, exact rollback, cost at or near SSAO ([evidence](presentation-owners.md#ground-truth-ambient-occlusion-ren-10-21-november-2026)). | VR headset and GLES/mobile hardware runs; a preset decision. |
| REN-11 — Closed | Ray-marched parallax occlusion for the PBR `heightMap` keyword on GL and Vulkan: one shared kernel, refinement, distance fade, gradient-correct sampling, optional self-shadow; every lighting pass traces the same ray (per-light, shadow-mapped, ambient, environment, probes, grids, diagnostics). The retail `Parallaxbump` stage upgrades on Vulkan. Default off with byte-identical rollback; backend parity within the flat-surface noise; cost priced at 2560x1440 ([evidence](parallax-occlusion.md#qualification-21-november-2026)). Stock content has no parallax surface. | Linux (Mesa GL and Vulkan) and the Android emulator (GLES declines cleanly) pass. VR headset confirmation and physical Linux GPUs, macOS and Android devices belong to platform qualification; a preset decision waits for authored content. |
| REN-12 — Partial | Depth of field on GL and Vulkan from one lens model (`DepthOfFieldCore.h`): thin-lens CoC from the view's field of view on a 36 mm sensor, autofocus with an eased pull, tile-dilated scatter-as-gather bokeh (round or bladed, highlight weighting) at half or full resolution, before motion blur and bloom. The `r_dof*` promotional camera is cheat-only with no menu entry. The game asks through `SetViewDepthOfField` (render API 38, a `renderViewDepthOfField_t`): the weapon wheel's view-weapon focus, a focused in-world GUI (a thin lens on the cursor's point, view weapon kept sharp by `VIEW_DOF_SHARP_VIEW_WEAPON`), and map scripts (`sys.setDepthOfField`, `setDepthOfFieldFocus`, `setDepthOfFieldLens`, `clearDepthOfField`, declared by the compiler on first use, state saved in the worldspawn's spawn args). The player's **Depth of Field** setting (`r_depthOfField`, SYSTEM catalog V8) gates both and is on from Enhanced up. Windows captures on both backends; `DepthOfFieldCore` native test. | Cost not priced against FPS-01; Linux, macOS and Android (GLES declines) not run; capture-tool review of the bokeh on authored character shots; the campaign's cinematics (REN-13). |
| REN-13 — Not started | REN-12's scripted lens is in place for cinematics to use. | Dynamic depth of field across the campaign's cinematics: engine-driven focus for cinematic cameras on stock assets first, then per-scene direction where it needs it; see the task below. |

### REN task evidence and acceptance

- [x] **REN-01 — P1, qualification: promote shared classic domains individually.**
  Finish fresh-package GL/Vulkan acceptance for root GUI, in-world GUI, ambient,
  interaction, fog/blend, deform, subview, cinematic/post and special-frame owners.
  Cover stock fixed cameras, mapped/stencil/hybrid lights, dynamic/cutout casters,
  direct/dynamic mirrors, remote/x-ray/refraction views, nested color/depth/cubemap
  targets, `_currentDepth`, root post and render-demo/Raven effects. Require
  nonzero executed ownership, same-backend classic parity, visible effect deltas,
  complete zero-commit rollback and human/platform evidence for each domain.
  Completed 13 November 2026 on Windows x64 OpenGL and Vulkan.
  `renderer_shared_domain_qualification.py` runs each domain on a fixed stock
  pose (or, for subview forms no stock material authors, a validation room
  compiled into the run's save path) from a freshly built package, under the
  default preset, with 4x MSAA and with the post chain off. Each check requires
  owned views on the active backend and none on the other, a frame
  byte-identical to classic (alone and with every domain on), a visible
  content control and a named zero-commit rollback. The final run, from a
  fresh package built from the change set, passed 98 of 99 domain checks in 87
  runs (RTX 4060 Laptop GPU). The one failure was classic flicker: OpenGL
  stencil shadows under 4x MSAA flipped isolated pixels between two classic
  frames. The cause is NVIDIA's OpenGL driver, which shades overlapping draws
  into a 4x or 8x multisampled target slightly differently from frame to
  frame with an identical command stream; the harness now serializes classic
  OpenGL draws (`r_glSerializeDraws`, validation only) and the combination
  passes. OpenGL render-demo playback under
  the default preset, left out of that run, now passes: the renderer's scene
  target starts every frame cleared, so uncovered pixels no longer carry the
  last frame's bloom.
  Under the default presets nothing had ever been owned. Fixed: the adapters
  refused every render target, though the classic walker draws the view into
  the renderer's own scene target or the game's forward target (both
  backends); Vulkan moved a view to the swapchain whenever a domain was on,
  which changed the classic work beside it by up to 18 levels; dense views
  overflowed the 1024 per-frame geometry records; every stock in-world GUI was
  refused for its mirrored GUI transform; the ambient domain refused any lit
  view; the interaction domain refused every CPU-skinned character (it read
  `deformedSurface` as GPU posing, the misreading classic fixed in #73) and
  every distant point light's projected map (labelled a cube); Vulkan batched
  the HUD with the final scene composite; and Raven fallbacks were miscounted.
  Stock-content boundaries, by design: the retained HUD's backdrop blur,
  ambient views with decals/BSE/post surfaces, cubemap and depth subview
  targets (no producer) and wall-clock GUI video parity. The all-mapped scene
  ran with the static shadow cache off until 14 November, when its two gaps
  closed: classic Vulkan no longer regenerates composed maps under the
  interaction switch (the sealed stream composes and reads resident hits
  like classic), and OpenGL's sealed cache records carry classic's light
  class, so a distant point light's cached pass is owned. The
  defaults stay off: promotion is an owner decision on the
  [evidence](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01).
  Human appearance review and physical Linux/macOS/Android runs belong to
  platform qualification (PLAT), as for REN-08.
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
  Progress, 23 November: step 1's combinations are owned for one stage set.
  A per-draw classic stage (`uClassicStage`) lets the depth and forward
  passes test a cutout's alpha and add baked light through the stage's
  texture matrices, colour and vertex colour, as ARB2 does. Cutouts with a
  texture matrix, vertex colour or a conditioned stage match native ARB2 at
  mean 0.25 levels with 3 to 5 bounded hole-edge pixels, and byte for byte in
  display frames apart from those pixels (`production-fixed-cutout-*`).
  Baked-light receivers are composed frames inside the controls' limits
  (`production-fixed-grid-*`); see the inventory for what those controls do
  and do not prove. Several stage sets with baked light, and several
  alpha-tested stages, decline by name.
  Step 4 is done for classic receivers of stencil-shadowed lights: a light
  ARB2 shadows with stencil volumes by policy is drawn by the corridor with
  ARB2's own stencil pass (`production-fixed-stencil`: mean 0.25 levels; in a
  display frame the specimen is byte-identical and 3 pixels of the whole
  frame differ by 1 level). Shadow-mapped lights, a stencil light that also
  lights a PBR surface, and the point-cube pool remain.
  Step 5: `r_rendererOwnershipCompose` (default 0) lets the aggregate
  executor and the shared world domains be requested together with one owner
  per pass. The aggregate executor composes the frame it owns while the
  shared interaction stream is sealed and left undrawn, and a frame it
  declines is drawn by that stream, byte-identical to ARB2
  (`production-fixed-compose`, `production-fixed-stencil-compose-fallback`).
  The corridor still derives its own interactions; moving it onto the sealed
  stream remains.
  Named rollbacks: `r_rendererModernClassicShapes` (bit 1 stage state, bit 2
  stencil lights) and `r_rendererOwnershipCompose 0`. No parity mask,
  promotion token or default changed.
  Step 6 is not reached: no stock view is owned. Of eight
  `game/airdefense1` poses with the modern path requested and shadows off,
  the corridor declines four on skinned character geometry and four on a lit
  surface without a forward command, and the frames' first blockers are
  stage colour and stage conditions on shapes the corridor does not take
  ([census](modern-lit-frame-inventory.md#stock-views-step-6)). Requesting
  the path there costs a declined frame 8.4 ms against ARB2's 4.9 ms on
  average (shared-machine numbers, not budget evidence), so there is a cost
  to remove and no benefit to show yet.
  Remaining, in order: the stock shapes that census names, shadow-mapped
  lights and the point-cube pool, the corridor on the sealed interaction
  stream, then measured CPU/GPU benefit on owned stock views before any
  promotion.
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
  Weapon lights, 17 November (#235, #236): muzzle flashes, classic flashes and
  the light segments of weapon, monster and vehicle effects now follow
  `g_dynamicLightShadows` (on from Quality up), and explosion effects with no
  light of their own get a classic flash. The firer's body and gun, a
  projectile's own model and whatever an explosion starts inside are kept out
  of those shadows. A storage2 probe shows them mapped on both backends and
  drawn by the stencil path on Balanced, with no self-occlusion. See
  [weapon-light shadows](release-completion.md#weapon-fire-and-explosions-cast-shadows-235-236-17-november-2026).
  Character self-shadows, 17 November (#238): `r_shadowMapCharacterSelfShadow`
  (off by default) lets skinned models ignore noSelfShadow ownership under
  shadow-mapped lights; verified on both backends, stencil unchanged. See
  [character self-shadows](release-completion.md#characters-can-shadow-themselves-238-17-november-2026).
  Remaining: physical-platform breadth; combat-crowd cost of weapon-light
  shadows on Quality (FPS-03). The DISPLAY page's IMAGE QUALITY tab has a
  Character Self-Shadows row (17 November).
  Outdoor sunlight, 19 November (#239): Quality and Ultra cascade sunlight and
  distant sources (spotlights keep one map; Maximum still cascades them), and
  the last cascade holds the light's whole map so shadows continue past the
  reach, chosen per pixel where the last fitted crop ends. On ten frozen Air
  Defense 1 poses the share of stencil's shadow kept rises from 27.7% (single
  map) to 96.4% on both backends. See
  [cascaded sunlight](shadowmapping-outdoor-terrain.md#cascaded-sunlight-on-quality-and-ultra-19-november-2026-239).
  OpenGL keeps the whole-light slot's still scenery cached and draws only
  moving casters over it (296 caster draws a frame against 359 for the old
  four-cascade layout). Distant lights now size each receiver's normal offset
  by the texel at its depth (`r_shadowMapDistantNormalOffsetDepth`): 96.4% to
  97.0% with cascades, 27.6% to 29.0% with one map, and fewer falsely darkened
  pixels on both backends. See
  [depth-sized offset](shadowmapping-outdoor-terrain.md#normal-offset-sized-at-each-receivers-depth-for-distant-lights-19-november-2026).
  Remaining: re-measure the FPS-02 Quality/Ultra budgets with cascades on, on
  a quiet reference machine.
  Off-centre lights against stencil, 23 November:
  `renderer_shadow_stencil_parity_scan.py` isolates each shadow-casting point
  light whose source lies outside its own box (388 authored) and captures it
  with stencil shadows, shadow maps and none. On six such lights of
  mcc_landing the two backends agree, and the audit's unconfirmed item is
  settled: maps do cast whole surfaces where stencil holds only geometry in
  the light's volume, but the measured effect is 0.41% of a view at most. A
  trial that withheld and clipped those casters was withdrawn: both caster
  passes draw a model surface whole, and a caster that is admitted but not
  drawn sends the light to its stencil fallback (unshadowed on Vulkan for one
  light). The scan found two larger differences that are not that: the map of
  `light_9083`, a prelit fill light whose source lies outside the level,
  shadows structures stencil leaves lit (up to 49% of a view; light-facing
  casters reproduce stencil), and `light_9081` leaves lit 10% of a view that
  stencil shadows. The `noDynamicShadows` rule now has its image (waste's
  `light_8501`). See
  [off-centre lights against stencil](shadowmapping-audit-2026-11-09.md#off-centre-lights-against-stencil).
  The static world from the stencil volume, 23 November: both lights and the
  casters outside the volume are one finding. The dmap prelight volume, read
  from the retail `.proc`, holds exactly the world triangles that face away
  from the light, cut to it; a map drew each world surface whole, and under
  distant and parallel lights it stored the faces turned towards the light
  (the terrain over `light_9083`'s buried origin; a handedness verdict that
  measurement contradicts for the two synthesized projections). A light whose
  source lies outside its own box now casts the static world as one caster,
  the front caps of that volume, in both windings
  (`r_shadowMapPrelightCasters`, default 1, 0 as before): `light_9083`
  48.9% to 0.01% of a view shadowed where stencil lights, `light_9081` 10.4%
  to 0.22% lit where stencil shadows (the volume keeps a piece of a ceiling
  that lies in the light's boundary plane) and q4cmp13's parallel suns 30.9%
  to 0.7%, on both backends; q4ctf2's fill lights 14.3% to 1.4% on Vulkan.
  No light is sent to its stencil fallback. 296 of the 405 stock lights of that kind take it.
  The fixed `noSelfShadow` world-geometry rule has its image: a stock marine
  crate behind `noSelfShadow` world geometry under mcc_1's `corey_light_3557`
  is as dark under maps as under stencil on both backends. The scan tool
  rebuilds interactions for each picture and takes projectors, parallel and
  distant lights, exact views and spawned stock entities. See
  [what the stock volumes hold](shadowmapping-audit-2026-11-09.md#what-the-stock-volumes-hold).
  Remaining from the audit: the 78 lights whose volume folds its caps (the
  caps are recoverable; cutouts would cast solid), the other casters of
  distant and parallel lights on the reversed faces, and the far shell's
  leak under a coarse map: a receiver closer behind a cap than the map's
  bias is lit (Air Defense 1's `light_7910_cursed` 0.15% to 1.9% of one
  view, the one light measured that is worse for the change).
- [x] **REN-04 — P1, qualification: temporal AA and dynamic resolution.** Exercise
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
  [temporal qualification](temporal-presentation.md#validation).
  Progress, 11 November (#216): the resolved image moved by the whole jitter
  step every frame on both backends, because history was reprojected through
  each frame's jittered projection. History now lives on the unjittered grid
  (aligned previous camera, recentered upscaling input, the unjittered portal
  sky sampled in place), posed surfaces that were not re-posed keep exact
  motion, and `renderer_temporal_stability.py` measures the picture at rest.
  See [stability qualification](temporal-presentation.md#stability-qualification-11-november-2026-issue-216).
  Completed 23 November 2026 on Windows x64 OpenGL and Vulkan, with the two
  open findings closed. While temporal AA upscales, material textures are
  sampled log2( scale ) mips sharper (`r_temporalAALodBias`, default 1, in
  quarter-mip steps with hysteresis so dynamic resolution does not rebuild
  samplers every few frames; OpenGL ES has no such sampler state and declines).
  The settled history holds 5% more detail at 75% scale and 16-18% more at
  50% on both backends, inside the unchanged stability limits; the stability
  gate gained control cases and passes all 14 single-player runs.
  The softer Vulkan frames were NVIDIA's Vulkan driver: with identical sampler
  state, top mip and trilinear results, it loses detail once anisotropy
  passes 1, where OpenGL and Intel's Vulkan driver gain it. The Vulkan sampler
  cache now biases anisotropic samplers on that driver by the measured 0.15
  to 0.25 of a mip (`r_vkAnisotropicLodBias`, 0 off), and native-resolution
  detail reads 30.7 against OpenGL's 30.4 where it read 27.5 against 31.0.
  `image_lodbias` (the retail cvar) is restored as the user's control. See
  [texture detail](temporal-presentation.md#texture-detail-while-upscaling-and-under-vulkan-23-november-2026).
  Human review of ghosting and disocclusion on the reference display and
  physical Linux/macOS/Android runs belong to platform qualification (PLAT),
  as for REN-08. Native UI separation stays covered natively; capture frames
  cannot show it. Not rerun for this change: the multiplayer stability case.
  Found and left open: a 10240 x 5760 tiled Vulkan `screenshot` fills only
  part of the image.
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
  That was the position on 6 November; the 23 November entry below
  supersedes its remaining list.
  Progress, 21 November: SSAO's dither is gone. Both renderers draw the raw
  AO into an occlusion target (full or half resolution) with sixteen
  interleaved sample patterns and resolve it with a depth-plane-aware tent
  that cancels them; native PBR's field filters the same target, so its
  classic pixels still match a classic-only frame and it now follows
  `r_ssaoResolution`. See [filtered SSAO occlusion](presentation-owners.md#filtered-ssao-occlusion-21-november-2026).
  Progress, 23 November: the lavapipe run was made on a scratch disk on E:.
  The one-byte difference does not occur at `d30280aa2` (three runs, 41/41
  each, scalar, packed and separate forms identical; llvmpipe OpenGL 40/40).
  Its cause is lavapipe's texel decode, `float(k) * float(1/255)`, one float
  step from the rounded quotient for two of the laboratory's bytes; with
  exactly rounded scalars the October pixel returns. The ambient parity tool
  now allows at most four one-byte pixels between those forms on lavapipe
  only, read from the run's own logs. See
  [lavapipe](renderer-validation-matrix.md#lavapipe-material-layout-difference-23-november-2026-ren-05).
  The same run found the ten current SSAO controls failing on Mesa since the
  21 November change (`ssao-on` on llvmpipe and lavapipe, and OpenGL/Vulkan
  parity on `ssao-classic-half`); that is open.
  Authored content outside the laboratory: the showcase runner now spawns a
  skin swapping a PBR material for another and for a classic one, a skinned
  model carrying both (CPU skinning and packed MD5R), measures the cutout
  fence's coverage and the light passing it, and compares the two renderers.
  Both pass 67/67 on procedural stand-ins. It found that the showcase fence
  had never cast a shadow (`twoSided` implies `noShadows`, a stock rule the
  PBR guide now states) and that OpenGL's first PBR draw under a mapped
  projected light costs a two-minute, 9.3 GB cold shader compile on NVIDIA,
  which belongs to the shadow items. See
  [showcase proofs](renderer-validation-matrix.md#authoring-cutout-decal-and-probe-proofs-23-november-2026-ren-05-ren-06).
  Remaining: the Mesa SSAO controls (reproduce on current SSAO, then fix or
  bound as the Intel residual was); qualification on the real textures.com
  materials, which waits on the owner's downloads (the drop folder is empty);
  and physical Linux and Apple fallback, which belongs to platform
  qualification (PLAT).
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
  Progress, 23 November: broader authored scenes. The showcase map now
  carries a box probe captured in the engine and two authored decals; a
  further launch places a probe and a PBR specimen in `game/airdefense1`.
  On both renderers the probe changes every station, survives an image
  reload exactly, follows a second capture (new source generation, counted
  in `gfxInfo`) and returns with a third; the stock scene's pixels away from
  the specimen do not change at all with probes on; and the two renderers'
  captured faces agree. See
  [probe sources](vulkan-probe-sources.md#authored-scenes-beyond-the-laboratory-23-november-2026-ren-06),
  which also maps each admission case (incomplete sources, generations,
  reload, atlas exhaustion) to its control.
  SSR and SSGI now run on seven stock scenes (three `airdefense1` poses and
  the `medlabs`, `tram1`, `hangar1` and `building_b` starts): fourteen
  launches pass, master-off is byte-identical everywhere, and their
  screen-space limits are restated. The froxel leaf is REN-09's shadowed
  volumetric lighting now; the harness keeps it in the combination and the
  rollback and no longer judges its own effect. See
  [more stock scenes](renderer-validation-matrix.md#more-stock-scenes-23-november-2026-ren-06).
  For the owner's re-bake decision, `game/airdefense2` was baked in scratch
  with the bundled options (6.7 minutes): at 24 fixed views the bundled grid
  adds 0.00 levels and the re-bake 0.58, lighting a third to two thirds of
  the frame in four of six areas. Nothing in `pak1` changed. See
  [re-bake evidence](renderer-validation-matrix.md#light-grid-re-bake-evidence-23-november-2026-ren-06).
  Remaining: re-baking the bundled grids (owner decision); a quiet-machine
  measurement of the leaves' cost beyond the first scene (today's GPU times
  were taken on a shared machine); other GPUs and platforms and review of
  the leaves' appearance (PLAT).
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

      **Retained vertex streams, overlap repair and qualification,
      23 November.** Mode 2 now also retains the vertex-class streams of
      static geometry (world areas and static models drawn through their own
      triangle records): vertices, CPU vertex-cache bytes, shadow vertices and
      the per-vertex and per-face tables. Posed, deformed, frame-allocated and
      GPU-skinned geometry keeps its per-view copies. The classification only
      selects candidates: every acquisition compares the complete range, a
      range rewritten in place is refused for a doubling run of acquisitions,
      leases survive replacement and eviction and release at retirement, and
      the cache holds at most 128 MiB and 32,768 entries. `r_vkSnapshotRetain`
      is now 0, 1 (index streams) or 2 (both, the default); cached versions are
      dropped when the vertex caches are purged or a world is freed. Native
      tests cover replacement, lifetime, eviction, refusal and stale reuse with
      four new negative controls. Same-binary comparisons: copied bytes fall
      from 6.86 to 2.65 MB/frame (61.4%). Elapsed snapshot time does not fall
      (4.90 and 6.18 ms/frame on a shared machine): a stack sample attributes
      the largest share of it, about 40%, to the validating comparison, so no
      CPU gain is claimed.

      Qualifying the modes found that modes 1 and 2 had not overlapped at all
      since 6 November: the engine clears the VR frame on every frame, and
      that call joined the render thread unconditionally. The join is removed
      for an already clear frame and pinned by the contract test. With the
      overlap restored, on Windows x64 Vulkan with a hidden window and
      validation layers on:
      - **Parity.** Frozen Quality captures (HUD hidden, temporal AA on) are
        bit-identical between modes 0, 1 and 2 on four single-player views
        (`airdefense1` twice, `hub2`, `medlabs`), as is the worker-drawn scene
        readback. A multiplayer listen server cannot be frozen and stays
        within the mode 0 frame-to-frame floor. Every measured window ran
        entirely on the render thread with no early join; mode 2 started 24
        to 149 of 150 front ends while the backend was still busy.
      - **Bounds.** 100 window-resize and scene-scale cycles and a full
        `vid_restart` per mode, with mode 0 as the control: private bytes grew
        1.0% (mode 1) and 1.4% (mode 2) between the second and the last
        20-cycle period, both below the control's 1.7%; handles and Vulkan
        allocations stayed flat; the retained caches stayed under 6 MiB; no
        validation message. A first run failed and is kept: dead entries
        filled the vertex cache across restarts, which the purge-time drop
        fixes.
      - **Timing, shared machine (95% host CPU, five to six competing
        clients).** Mode 1 beat mode 0 in all six paired windows (median of
        window means 8.3 to 8.6 ms against 10.6 to 10.9 ms). Mode 2 was slower
        in all six (11.6 to 13.0 ms), its snapshot costing 4.6 to 7.5 ms a
        frame. Neither is budget evidence; `r_vkRenderThread` stays 0 and the
        Quality floor is not claimed.

      Snapshot cost beyond copying, same day. Every retained stream is still
      compared in full; the cut is in work the snapshot did not need:
      - Silhouette, face-plane, dominant-triangle, shadow-vertex and
        mirror/duplicate tables are not frozen. Only the front end and three
        null-checked debug views read them, and a snapshot taken while one of
        those views is on keeps them (about 2.0 MB a frame).
      - Frame-temporary index streams take the per-view copy instead of
        thrashing the retained index cache, which now holds under 1,000
        entries instead of sitting at its 32,768 cap. Index ranges also join
        the per-view lookup.
      - A frame holds one lease per retained version, not one per
        acquisition, and the four per-view hash containers are replaced by
        reused open-addressing tables.
      - `rendererFrameSnapshotStats` reports `threadWork`, work on the
        snapshotting thread that excludes preemption.

      A same-binary A/B (only the Vulkan DLL differs, nine mode 2 windows
      each, interleaved) cut thread work from a median 17.0 M to 9.2 M cycles
      per frame (-46%), and elapsed snapshot time from 7.7 to 4.0 ms. Parity
      is unchanged (bit-identical HUD-hidden captures on the four
      single-player views), and the 100-cycle bounds check passes, with the
      retained caches under 6 MiB. On the shared host (79-99% CPU), the candidate's
      mode 2 windows fell below the same run's mode 0 in six of nine; the
      baseline's never did. That is not budget evidence.

      See [vertex retention](presentation-owners.md#retained-vertex-streams-23-november-2026),
      [qualification evidence](presentation-owners.md#overlap-repair-and-mode-qualification-23-november-2026)
      and [snapshot cost](presentation-owners.md#snapshot-cost-beyond-copying-23-november-2026).

      **Still open:** quiet paired mode 0/1/2 measurements (five matched runs)
      and the every-frame Quality floor; static vertex bytes validated twice
      per view (`verts` beside the ambient cache, pending an audit of readers
      in the shared domain code); physical display/input qualification and
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
- [ ] **REN-09 — P2, implementation: shadowed volumetric lighting.** The REN-06
  froxel leaf is an unshadowed screen-space depth fog. Replace it with a
  view-frustum froxel volume that injects scattering from interaction lights,
  honours each light's falloff and projection images and samples the REN-03
  shadow maps (point, projected and the cascaded sunlight), so beams and shafts
  stop at their casters. Lights drawn with stencil shadows have no map to
  sample: decide per light whether they scatter unshadowed or not at all, and
  report the choice. Derive the medium from stock data and global settings:
  stock fog and blend lights keep their classic look, and no material, decl or
  map edits are required. Integrate front to back, reproject the volume with the
  TAA jitter and history resets, and composite before translucents so glass,
  particles and BSE effects sit correctly in the medium. Cover OpenGL and
  Vulkan, MSAA, dynamic resolution, mirrors and remote views, VR stereo, and the
  GLES/Apple GL 2.1 limits (decline cleanly where unsupported). Acceptance: a
  stock corridor, an outdoor sunlit map and a moving flashlight show shadowed
  shafts that follow their casters without temporal smear or light leaking;
  byte-identical rollback with the leaf off; requested and effective state
  reported; per-tier cost measured against the FPS-01 budgets before any preset
  enables it.
  Progress, 21 November 2026: implemented on both backends as per-light ray
  marching rather than a 3D froxel grid, because OpenGL keeps only the current
  light's shadow state (the march runs right after each light's GLOBAL receiver
  pass) and Vulkan's maps are resident only until the next view: each admitted
  light stage marches the view, jittered, through its world-space falloff and
  projection images and its own shadow map (point cube; projected atlas tile;
  cascaded sunlight, with the PCSS GLOBAL/LOCAL block pair on Vulkan) into a
  half-resolution RGBA16F scattering target, then a depth-aware 3x3 filter
  upsamples it and composites scene x transmittance + scattering before fog,
  blend lights and translucent surfaces. Fog, blend and ambient lights never
  scatter; stencil-shadowed lights add nothing unless
  `r_volumetricStencilLights 1`, and a shadow-casting light with neither a map
  nor volumes is skipped rather than leak. Sun-class lights (parallel, global,
  distant point) take `r_volumetricSunIntensity`. Temporal handling is the TAA
  history: the march offset turns per frame under a jittered view, and a
  volumetric setting change resets the history. `rendererVolumetricStatus` (and
  `gfxInfo`) reports requested and effective state per backend with each
  light's outcome; GLSL 1.20/Apple GL 2.1, the shared interaction stream, the
  HDR laboratory scene and GLES decline. `renderer_volumetric_lighting.py`
  (opt-in, hardware-bound) passes on GL and Vulkan for Air Defense 1 outdoors (5
  shadow-mapped lights), its swinging-light corridor (23 or 24, moving, so
  effect and status only) and Process 1 (8), with the Quality shadow
  configuration: frozen baselines identical, leaf-on master-off byte-identical
  to master-off, GL/Vulkan mean change within 0.6 of each other on the frozen
  scenes; it also passes with TAA and with 4x MSAA.
  Progress, 23 November 2026 ([evidence](volumetric-lighting.md)):
  `renderer_volumetric_motion.py` (opt-in, hardware-bound) passes its five cases
  on GL and Vulkan. With a stock door pair between a spawned shadow-casting
  light and a camera that pans 540 degrees carrying the flashlight, 17 lights
  scatter (11 shadow-mapped); the scattered light from motion-warmed shadow
  caches matches fresh maps (at most 0.02% of pixels differ), is nowhere
  brighter than the same lights unshadowed (`r_volumetricDebug 2`, new), moves
  by 39.8 levels where the doors shadow the medium and returns with them, the
  backends within 0.01 levels of each other. Frozen halfway through opening
  under TAA, the history clears as fast with the leaf as without (18 to 21% of
  the first frame's residual left after 16 frames, against 24 to 29%). The
  scattering target follows the scene at half resolution at 50% scale and
  through forced dynamic resolution, and the native frame afterwards is
  byte-identical. Mirrors, a reflection and a remote camera scatter (five views
  a frame in the REN-01 validation room), `r_volumetricSubviews 0` leaves them
  clear and reports them declined, and `r_skipPostProcess 1` declines with its
  reason and an unchanged frame. That case found each backend reallocating its
  one depth copy and scattering target for every view of a frame (204
  allocations in 34 frozen frames); both now keep four pairs keyed by view size
  and allocate nothing while the views hold still. The status counts scattering
  views, subviews and declined views alike on both backends.
  `openxr_vr_volumetric_smoke.py` passes on GL and Vulkan: both eyes scatter
  (two main views a frame, 12 shadow-mapped passes), keep their parallax and
  return byte for byte with the leaf off. `renderer_volumetric_tiers.py`
  records the tier ladder and the tuning candidates: on this shared machine, in
  a hidden window at 2538x1312, the default half-resolution tier cost roughly
  0.2 to 1.0 ms of GPU frame time and full resolution 1 to 4 ms, with the
  leaf-off frame itself drifting by up to 5 ms, so none of it is budget
  evidence. No default changed: the same `r_volumetricIntensity 12` is faint in
  the Process 1 view (+2.5 levels mean), lifts the busy Air Defense 2 corridor
  as a whole (+31 levels at the median) and whites out the Tram 1 opening view
  (+80), and the lower candidates pull those views apart, which is a visual
  decision. The frozen-view harness passes again on the changed engine (six
  of six, and outdoors with 4x MSAA).
  Open: the tier ladder and the performance suite on the quiet reference
  machine against the FPS-01 budgets; the owner's visual review of the defaults;
  the physical Linux, macOS and Android runs, including the Apple GL 2.1 and
  GLES declines, which no driver here can be made to report (PLAT). Outside the
  leaf: Vulkan shows no reflection in the validation rooms' direct mirror with
  the leaf off, so the leaf's picture inside it could not be compared there.
- [x] **REN-10 — P2, implementation: ground-truth ambient occlusion (GTAO).**
  Closed 21 November 2026. `r_ssaoMethod` selects the kernel of SSAO's
  occlusion pass: `0` SSAO (default), `1` HBAO (horizon rises above the
  tangent plane, evenly weighted per direction, strength from
  `r_ssaoIntensity`/`r_ssaoPower`; added at the owner's request), `2` GTAO
  (per-slice horizon search and the analytic cosine-weighted visible arc;
  slices turn evenly about the view direction), `3` a brute-force reference
  (256 cosine-weighted rays per pixel through the depth buffer, full
  resolution, unfiltered). The `r_ssao*` names stay: every method is
  screen-space ambient occlusion. All four share the
  occlusion target, separable filter, resolve and native PBR field, so the
  indirect-only application, resolution policy, MSAA, dynamic resolution,
  subviews, the view weapon and both backends are common. Native PBR folds
  GTAO's visibility through its coloured multi-bounce and specular occlusion;
  classic surfaces bounce with `r_ssaoMultiBounceAlbedo`. Under temporal AA
  HBAO's and GTAO's patterns turn a quarter pattern per frame. Evidence:
  [GTAO](presentation-owners.md#ground-truth-ambient-occlusion-ren-10-21-november-2026)
  and `tools/tests/renderer_ao_reference.py`: on three stock poses GTAO is
  15-50% closer to the reference than SSAO at Quality and Ultra, darkens
  silhouettes a third to a half as much and crawls no more under motion;
  OpenGL and Vulkan agree to 0.001-0.01 levels; SSAO after GTAO is
  byte-identical; the PBR laboratory's `hbao-*` and `gtao-*` controls pass
  20/20 on OpenGL, the native owner and Vulkan. GTAO costs the same as SSAO
  or up to a third less at each tier on both backends; HBAO is as accurate as
  SSAO and costs between the two (within 0.2 ms of SSAO at Quality on OpenGL). VR headsets, GLES/mobile hardware and
  the temporal turn (screenshot frames carry no jitter) were not observed;
  whether presets move to GTAO is the owner's decision.
  Original scope: add GTAO beside the current SSAO: horizon search over the depth buffer,
  cosine-weighted visibility, a multi-bounce colour approximation and a
  specular occlusion term for PBR surfaces. Run at half or full resolution with
  depth-aware upsampling, and accumulate over frames through the TAA jitter and
  history where TAA is on. Keep SSAO's indirect-only application (REN-05), so
  direct interaction lighting is never darkened, and keep SSAO available with its
  laboratory controls. Cover OpenGL and Vulkan, MSAA, dynamic resolution,
  subviews, the view weapon, VR stereo and the mobile tiers. Acceptance: on
  stock scenes GTAO is measurably closer than SSAO to a high-sample brute-force
  reference, with no haloing at depth discontinuities or crawl under motion;
  both backends agree within the REN-05 SSAO parity tolerances; rollback is
  exact; and its cost at each tier is priced against SSAO under FPS-01 before
  the owner decides whether presets move to it.
- [x] **REN-11 — P2, implementation: parallax occlusion mapping.** Closed 21
  November 2026. Stock content turned out to have no parallax surface: two
  pak002 test materials and nine Awakening-drop test materials name a
  `parallax_bump` guide that no shipped guide defines, and no pak ships
  `glsl/Parallaxbump.glsl`, so only Vulkan's native translation of that
  program ever drew it and OpenGL skips the stage. The production height
  source is therefore the new PBR `heightMap` keyword (`heightScale`,
  `heightReference`), carried in the ORM alpha (`packORMH`). One kernel,
  `src/renderer/ParallaxOcclusion.h`, serves both backends: a march whose
  sample count scales with view angle and distance, secant refinement,
  gradient-correct sampling of every map, a distance fade back to the flat
  surface and an optional self-shadow ray toward each light. The shared
  admission decides once per surface, so every pass that lights it (each
  interaction, shadow-mapped receivers, ambient lights, environment, probes,
  baked grids, diagnostics) traces the same ray; cutouts and emissive
  materials draw flat, depth is never changed. The retail stage upgrades on
  Vulkan with its own scale and bias. `r_parallaxOcclusion` (default 0),
  `r_parallaxSamples`, `r_parallaxDistance`, `r_parallaxShadows`; `gfxInfo`
  reports requested and effective state. Evidence:
  [parallax occlusion](parallax-occlusion.md#qualification-21-november-2026):
  the laboratory (`renderer_parallax_occlusion.py`) passes on GL and Vulkan,
  traced specimens change while their flat controls stay byte-identical,
  backends agree within the flat-surface noise, and the PBR laboratory is
  byte-identical to an unmodified build in 12/12 captures on both backends,
  with the feature off and on. Cost at 2560x1440 with a traced surface filling
  the frame: +1.3 ms (GL) and +1.5 ms (Vulkan) for the default ray, measured
  against FPS-01's Ultra budgets; no stock scenario or preset is affected.
  Linux (Mesa llvmpipe GL and lavapipe Vulkan, WSL) passes the same
  laboratory with the same figures; on the Android x86_64 emulator the GLES
  renderer, which has no native PBR owner, declines cleanly
  (`android_emulator_smoke.py --mode parallax`: identical frames off and on, no
  parallax draws, no warnings). VR stereo was not run on a headset, and physical
  Linux GPUs, macOS and Android devices belong to platform qualification
  (PLAT/AND); a preset decision waits for authored content.
- [ ] **REN-12 — P2, implementation: depth of field.** Partial, 23 November
  2026. A camera-quality depth of field for promotional captures and the
  weapon wheel ([player guide](../user/depth-of-field.md)).
  `src/renderer/DepthOfFieldCore.h` holds the lens model and the pass plan
  both backends draw from: the signed circle of confusion of a thin lens,
  K * (1 - S / d), with the focal length following the view's field of view on
  a 36 mm sensor unless `r_dofFocalLength` sets one; an in-focus band
  (`r_dofFocusRange`); separate near and far scales; blur sizes in pixels of
  a 1080-line view. Seven passes in one program per backend
  (`glprogs/dof.fs`, `shaders/post_dof.frag`): a 1x1 autofocus target eased
  toward the median depth of five taps around `r_dofFocusX/Y`; a prefilter to
  colour plus CoC at half resolution (full at `r_dofQuality 3`); 8x8 tile
  maxima of the near CoC, dilated by its reach; a golden-angle scatter-as-gather
  where a sample counts when its own disc covers the pixel, a farther sample
  may not spill over a sharper nearer one and foreground blur bleeds over the
  focus plane, shaped by `r_dofBlades`/`r_dofBladeRoundness` and weighted by
  `r_dofHighlights`; a tent; and a composite blending by the full-resolution
  CoC and the near coverage. It runs on the main scene view before motion blur
  and bloom, never on a headset's eye, and leaves the HUD sharp. Every
  `r_dof*` cvar is a cheat with no menu entry (reset on a server without
  cheats). The game asks through `idRenderSystem::SetViewDepthOfField`
  (render API 38): the weapon wheel keeps the view weapon (its depth range)
  and the world within 128 units sharp and blurs beyond (visual spec 1.50,
  register revision 30, `WID-039`), with the weapon kept raised under the
  wheel in single player as in multiplayer (spec 1.51); where the renderer
  declines, the view effects document's Gaussian takes over. Evidence: Windows x64 captures on
  OpenGL and Vulkan at 2560x1440 on airdefense2 (autofocus, telephoto with
  six blades, the CoC debug view, the open wheel), the
  `openq4-depth-of-field-core` native test, `retained_view_effects.py` and the
  retained-screens wheel checks. Remaining: price the cost against FPS-01's
  budgets, run Linux, macOS and the Android emulator (GLES declines by
  design), and review the bokeh on authored character shots.
  Progress, 23 November (second round): map scripts drive the same lens
  (`src/game/gamesys/ScriptedDepthOfField.h`): `sys.setDepthOfField( focus,
  fStop, blend )`, `sys.setDepthOfFieldFocus( entity, fStop, blend )` (follows
  an actor's eyes), `sys.setDepthOfFieldLens( focal, range, maxBlur )` and
  `sys.clearDepthOfField( blend )`, every change easing from the current focus
  (log-space focus pulls). The script compiler declares them on first use
  (`idCompiler::DeclareImplicitEvent`), so stock scripts compile to the same
  program and every existing save keeps its checksum; the state lives in the
  worldspawn's spawn args, so saves carry it without a format change. The
  request became `renderViewDepthOfField_t`; the weapon wheel still wins over
  the `r_dof` camera, which wins over a script. **Depth of Field** joins
  Settings > Display > Effects (retained page; SYSTEM catalog V8, journal
  fallback to V7) as `r_depthOfField`, a preset target on from Enhanced up
  (preset system version moves, so players are offered the system check);
  off, the wheel keeps its Gaussian and scripts draw nothing. Evidence: a
  `script` console call on airdefense2 draws a near focus and a 50 mm
  autofocus, the setting off and `clearDepthOfField` both return the frame
  to its reference. Third round (24 November): while the cursor is on an
  interactive in-world GUI the player view focuses a 50 mm f/2 lens on the
  cursor's point (eased in and out, the view weapon kept sharp), gated by
  the same setting, in both game modules; airdefense1 captures on Vulkan
  and OpenGL.
- [ ] **REN-13 — P2, implementation: dynamic depth of field in the
  campaign's cinematics.** Not started; the user's direction (23 November
  2026) once REN-12's scripted lens exists. Stock assets stay the baseline:
  1. Engine-driven focus while a cinematic camera owns the view
     (`gameLocal.GetCamera()`): focus on the actor the shot frames or the one
     speaking (lip-sync channel), easing between them on cuts, a lens from the
     camera's field of view, no content change.
  2. A per-scene direction table for the shots that need more (rack focus,
     a held subject, a lens), keyed by map and camera, applied by the game
     rather than by editing Quake 4's scripts.
  3. The Awakening's cinematics, and openQ4's own scripts (`scripts/openq4/`)
     calling the `sys.` events directly.
  Acceptance: every stock cinematic plays with focus that follows its subject
  on GL and Vulkan, with the Depth of Field setting off restoring the original
  image, no save or script-program change, and a reviewed capture per scene.

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

**Estimate: 40%; 4/10 tasks closed.** The suite, the two baselines, the per-feature
cost attribution and the declared per-scenario budgets exist on the reference machine
(the RTX 4060 Laptop). Enhanced, Quality and Ultra are re-tiered to refresh classes and
meet them on both backends; the previous Ultra is the Maximum preset. Structural work has
started: FPS-03 is partial, FPS-04 closed the post-processing chain with declared
per-scene budgets on both backends, and FPS-05 fixed one MSAA policy that draws the same
image on both backends. FPS-06 removed the retained HUD's offscreen composition
and most of the Vulkan depth capture's cost (22 November); frame times against 0.13.2
are not yet measured.

| Task / state | Completed points and evidence | Remaining subpoints / closure boundary |
| --- | --- | --- |
| FPS-01 — Closed | Suite (nine scripted stock-map scenarios, per-frame traces, cold/warm passes, toggle ladders, attribution, hitch events, transitions, idle-launch retry), 0.13.2 and current baselines on both backends, report with the settings/code split; the RTX 4060 Laptop named the reference machine; 42 budgets (7 scenarios x Ultra/Quality/Enhanced x OpenGL/Vulkan, warm mean and p99) declared from 126 launches of 94e0b14ec in `docs/dev/performance-budgets.json` and report section 10; `budgets` and `check` subcommands. | Budgets redeclared by FPS-02 (report section 11); the gate itself is FPS-09. |
| FPS-02 — Closed | Refresh classes declared at 2560x1440 on the reference machine (Enhanced 144/90, Quality 100/60, Ultra 75/45 fps average/1% low); candidates screened on both backends; the three recipes rebuilt (point cubes 512 or stencil, MSAA 0/2/4, no cascades or motion blur below Maximum); `maximum` added as the everything-on preset without a frame-rate claim; detected starts capped at the display refresh; the startup banner reports the bootstrap and the detected profile with corrected video memory; Vulkan point-policy admission fixed; 42 budgets redeclared from 126 launches, every tier inside its class in every scene. | Closed. |
| FPS-03 — Partial | Point-light cube maps identified as the dominant shadow cost; static caching and importance budgets already exist. FPS-02 sized the tiers around them: 512 cubes at Quality and Ultra, stencil point shadows at Enhanced, 2048 only at Maximum. The Vulkan light scan no longer keeps the stencil volumes of non-prelit mapped lights, including lights with an interaction left unbuilt in the view (identical images, CPU-neutral on the stock scenes). The point cube cache holds a whole view on both backends (64 slots within a memory bound, no eviction of cubes in use); OpenGL composes moving and cutout casters over cached static cubes; the default nine-tap point filter reads its sixteen texels once; receivers skip unlit fragments; the OpenGL timer queries run only for the report. In hub2 at Quality on OpenGL the shadow-mapped frame fell from 12.0 to 6.6 ms of GPU and 11.8 to 5.2 ms of CPU (hidden-window probe). Since then (10 November): Vulkan composes over cached cubes too, mapped lights shed their stencil front-end work, each projected light takes a tile size that lets a crowded view fit, the projected cache is bounded by memory, OpenGL cubes are depth only, cutouts that cannot change are cached, and Vulkan reads unchanged projected maps where they live. | Fullscreen tier budgets, including whether Enhanced can map point lights; one static cube for both ownerships of a composed Vulkan light; static-world volumes, still built once per mapped light. Per-face cube culling was measured and set aside. On OpenGL a cube is one layered render target, not six (FPS-08). |
| FPS-04 — Closed | OpenGL now draws SSAO at half resolution below 32 samples (Quality, Enhanced) with Vulkan's two passes, blends it into the multisampled scene instead of copying the scene, and reads the scene once for bloom and the tone map; at Ultra it resolves the scene twice a frame instead of four times, like Vulkan. Hidden-window GPU ladders in five scenes on both backends: the OpenGL Ultra frame 0.4-0.7 ms shorter and its chain 0.7-1.1 ms cheaper, OpenGL Quality SSAO 0.4-0.55 ms cheaper; per-scene Ultra chain budgets (1.5-5.0 ms) and per-pass shares of the 13.3 ms Ultra frame declared and met on both backends, where four of five old OpenGL chains were over. Pictures unchanged within one level; PBR laboratory SSAO controls 10/10 on both. [Report section 12](performance-report.md#12-post-processing-chain-fps-04). | Closed. Outside the gate: bloom's full-resolution first level and Ultra's SSAO resolution, a user decision. OpenGL's SSAO depth copies were cut afterwards ([report section 12.8](performance-report.md#128-opengls-ssao-depth-copies-follow-up): hub2 and storage2 0.6-1.05 ms faster on OpenGL, pictures unchanged). |
| FPS-05 — Closed | Sample counts 0/2/4/8/16, the LDR target and SMAA/TAA alternatives priced at Ultra on both backends in five scenes (hidden-window GPU ladders): 16x on OpenGL costs 6.0-10.9 ms over 4x and Vulkan never draws more than 8, 8x costs 0.75-2.5 ms over 4x, 4x + SMAA is within -0.7..+0.3 ms of SMAA alone on OpenGL and 0.6-1.6 ms on Vulkan. Policy: Ultra 4x + SMAA ultra (confirmed), Quality 2x, Enhanced none, Maximum 8x instead of a 16x request, so every preset draws one image on both renderers; no preset asks for more than 8 (self-test); the count does not change with resolution. `gfxInfo` names the Vulkan clamp and the resolves per frame, `r_multiSamplesEffective` holds the drawn count, and the settings menu's MSAA row names the trade-off and the clamp in twelve languages. [Report section 13](performance-report.md#13-msaa-and-the-hdr-scene-target-fps-05). | Closed. |
| FPS-06 — Partial | Equal-settings deltas attributed: in storage2 the retained HUD's layer composition (17 two-dimensional views and 34 render-target commands per frame against the classic HUD's 7 and 17; about 0.4 ms of main-thread time); on Vulkan the soft-particle depth capture (1.3 ms in airdefense1). The 2 October build equals 0.13.2 and the 15 October build carries the storage2 step, bracketing the 8 October retained-HUD commit (bisection); interpolation, light grid, reverb, portal cull and the five new Vulkan backend features cost nothing measurable. 22 November: the composition is gone. It was the Strogg gauges' static grain, a sheet of ten translated tiles under each fill mask, which took six offscreen targets a frame and kept the Strogg HUD from replaying; it is now one stationary quad with scrolling texture coordinates, as the EKG is. Storage2 carries 7 two-dimensional views and 18 render-target commands with the retained HUD (17 and 34 before; the classic HUD 7 and 17), every suite scene reads the same, and the HUD's own main-thread time fell from 0.4-0.9 to 0.2-0.3 ms a frame in a hidden-window profile; pictures within seven levels of 255, identical on OpenGL and Vulkan ([report section 18](performance-report.md#18-the-retained-huds-composition-fps-06-22-november-2026)). The Vulkan depth capture cost 0.64 ms each, two a frame outdoors (portal sky and main view), because the multisampled depth target was moved to the sampled layout and back around every capture; it now stays an attachment, and the resolve is 0.12 ms instead of 0.60 at 8 samples and 0.08 instead of 0.31 at 4, about 1 ms of GPU time a frame outdoors and 0.5 ms indoors at the 0.13.2 settings, with identical pictures and a clean validation layer ([report section 19](performance-report.md#19-the-vulkan-depth-capture-fps-06-22-november-2026)). 23 November, in the HUD documents alone: the static optic art of the air-defence cannon and the three zoom scopes is a cached layer, 218k vertices a frame to 20k for the cannon at 720p and 166-264k to 25-47k for the moving scopes at 1080p (railgun 4.1 to 0.7 ms), and the weapon wheels, quick chat and HUD artwork catalog are compact JSON, about a quarter of their size ([report section 20](performance-report.md#20-static-optic-art-and-hud-document-size-fps-06-23-november-2026)). | Fullscreen frame times of both changes against 0.13.2 (not measured: the machine was shared); the rest of the Vulkan difference (more draws in the airdefense1 scenes, the storage2 share); bisection pass 2 for the remainder; closure within 3% average and 10% p99 of 0.13.2 on every suite scene on both backends. |
| FPS-07 — Partial | Hitch classes recorded: 2-3 s cold draw stalls at Ultra, periodic 100 ms hub2 frames, a 393 ms storage2 frame, first-use events. Level loads and startup: one display-rate pacer, checkpoints in every long stretch and the stall sampler behind `com_loadingFrameTrace 2`; Air Defense 1 now loads at a 16.7 ms median, worst 54-60 ms against 1.6-2.3 s ([loading presentation](loading-presentation.md)). | Attribution per class, fixes or declared budgets, clean first-use catalog on both backends; for loads, retained document builds and the hand-off into the level (start-of-level autosave flush, first-frame program loads). |
| FPS-08 — Not started | The Awakening's outdoor battle ([report section 14](performance-report.md#14-the-awakenings-outdoor-battle-fps-08-evidence-17-november-2026)): game 22% and drawing 33% of the process; two exact clip-world fixes (contents gathered by mask, sector contents refreshed lazily) cut about 10% of game time, and one-pass light-interaction cull bits (bit-identical, natively tested) cut that renderer work by about 30%. HF-07 attribution: the Quality frame is CPU-bound on one thread; Vulkan render-thread stage 1 landed and gains nothing at the GPU-bound Ultra (within 0.4 ms, longer tails). Constant material registers restored on both renderers (they were never marked constant, so every surface evaluated its registers every view): 69-87% of register set-ups now take the parse-time block, saving 5-22 µs per frame in hub2, convoy2 and the airdefense1 arena, which whole-frame timings cannot resolve; see [Constant material registers](material-constant-registers.md). | Front-end and submission cost, stage 2 overlap, game-tic cost; shared with HF-07 and REN-07. |
| FPS-09 — Not started | Prevention plan written (report section 9). | Suite in the release checklist, budgets as gates, cvar-default diff and attribution checks in CI. |
| FPS-10 — Partial | Low drops stencil shadows and the light grid and holds 60 fps in six timed scenes on an Intel Iris Xe at 1080p (was 44-55 in combat and outdoors); Minimum and Low Power hold 30 ([performance presets](ui/performance-presets.md#low-end-measurements-24-november-2026)). | Auto-Detect's integrated-GPU tier (Balanced at 8 GB and up); Vulkan depth-capture blit and portal-sky cost on low-end GPUs; a decade-old CPU and an older iGPU (UHD 620 class) measured; OpenGL on the iGPU. |

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
  Follow-up, 17 November (#233): ARM64 Linux boards without a discrete or Apple
  GPU are capped at Performance, as the Steam Deck is, and Minimum and Low Power
  drop the light grid and, through their 30 Hz cap, automatic temporal AA; see
  [ARM boards and the low-power floor](ui/performance-presets.md#arm-boards-and-the-low-power-floor-17-november-2026).
  Model-suite coverage only: no ARM board ran it.
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
  after it. Remaining:
  the fullscreen budgets, including whether Enhanced can map point lights.
  (Static volumes under mapped lights were measured on 11 November: about 7 ms
  and 4 MiB for all of hub2, built once, and kept so the stencil fallback stays
  instant. Under a light that moves they were built every frame, 27 a view in
  storage2's alarm room, and are now left out with the dynamic models'.)
  (A receiver pass with nothing to cast no longer sends its light to the stencil
  fallback: OpenGL cycled such a light in and out every 240 views and Vulkan never
  released it. Found by the openD3 session on Doom 3's hell1; no stock Quake 4 view
  tried shows it, so it was reproduced with a temporary switch and is pinned by
  the shared rule's self-test.)
  (On Vulkan a light whose only `noSelfShadow` casters are live, a character
  standing in it, now keeps one resident map for both receiver ownerships, as
  OpenGL does: two or three point cubes fewer in medlabs and network1, half the
  resident spotlight tiles in hub2 and medlabs, and the LOCAL map is no longer
  rendered again each time a character walks in or out.)
  (A light that moves now draws its maps directly instead of publishing
  each one to the cache, copying it and composing over the copy: 560 passes on one
  hub2 route, `r_shadowMapCacheMovingLights`.) (A light whose map failed once now stops building its stencil volumes
  again after 240 views without another failure, `r_shadowMapStencilFallbackViews`;
  before, it built them for the rest of the level.) (Shadow maps now follow the
  stencil rule for which triangles cast: open geometry stores the ones that face
  away from the light, sealed hulls keep their near shell, and the face to discard
  follows the handedness of each light's projection, `r_shadowMapCasterCulling`.
  storage2's alarm lights had been blocked by a one-sided floor.) (OpenGL's
  shadow receivers no longer normalize the stock normal-map sample, which lit
  every raised edge harder under a shadow map than without one; OpenGL now
  agrees with stencil as closely as Vulkan does.) (A still light's maps are no
  longer rendered again when two of its casters change places in the view's
  entity order, or when a rising frame rate makes a caster at rest count as moving
  again: the static signature is a sum of per-caster hashes and the oldest frame
  that counts as recent never moves back. On two walks in the airdefense1 arena
  every fresh map was for the order, 8 and 8 on Vulkan and 8 and 20 on OpenGL,
  and none is left. The world's geometry joins a still light's casters only when
  a door connects an area, which the map needs.)
- [x] **FPS-04 — P1, implementation: post-processing chain cost.** Price and reduce the
  screen-space passes the current Ultra adds: SSAO (full resolution on OpenGL; the
  half-resolution path exists only on Vulkan and only below 32 samples), motion blur
  (16 samples), the HDR tone map (3 ms in airdefense1 at 16x MSAA), bloom (5 mips) and
  SMAA ultra: 10-14 ms per frame at 16 samples on OpenGL, 4-6 ms at 8 samples on Vulkan.
  Bring the half-resolution SSAO to OpenGL, merge passes that read the same
  scene target, make the resolves MSAA-aware so the tone map does not pay a second
  resolve, and set the sample counts per tier. Close when the post chain at the
  re-tiered Ultra costs less than a declared budget per scene on both backends and the
  feature ladders show each pass within its share.
  Closed 12 November 2026. FPS-02 had already taken motion blur and 16-sample MSAA out of
  Ultra, so the chain left to price was SSAO, bloom, the tone map and SMAA. Vulkan already
  did what this item asked of OpenGL; OpenGL now does the same. SSAO follows
  `r_ssaoResolution` on both renderers (automatic: full resolution at Ultra's 32 samples,
  half below, so Quality and Enhanced draw the two half-resolution passes of
  `VK_Post_DrawSSAOHalf`, `RB_DrawSSAOHalf` on OpenGL). The classic SSAO pass writes only
  its factor and the blend unit multiplies it into every sample of the multisampled scene,
  instead of resolving the scene into `_currentRender` first; native PBR's occlusion
  field, the full-resolution debug view and `r_glSSAOReferenceComposition 1` (a validation
  switch) keep the copy. The bloom/tone-map composite no longer resolves the finished scene
  a second time: every later reader in the view takes its own copy. At Ultra the OpenGL
  chain resolves the scene twice a frame instead of four times, as Vulkan does; these are
  the merged scene reads and MSAA-aware resolves. Sample counts per tier stay as FPS-02 set
  them (SSAO 32/20/12, bloom 5 levels, SMAA ultra/high/high, motion blur Maximum only); the
  price list below is their justification. Measured on the reference laptop with in-launch
  GPU ladders in hidden 2560x1440 windows (`rendererBenchmarkCapture` GPU time, toggles
  against neighbouring base windows), hub2, storage2, the Air Defense 1 arena, Air Defense
  1 and convoy1: the OpenGL Ultra frame is 0.4-0.7 ms of GPU shorter, its chain 0.7-1.1 ms
  cheaper (hub2 3.71 to 2.63 ms), and OpenGL Quality's SSAO 0.4-0.55 ms cheaper. Declared
  Ultra budgets for the whole chain, the worse backend plus 25%: hub2 3.3, storage2 5.0,
  arena 2.4 (combat and effects), Air Defense 1 1.7 and convoy1 1.5 ms. Per-pass shares
  of the 13.3 ms Ultra class frame: SSAO 22.5% (3.0 ms), tone map 7.5% (1.0), bloom 6%
  (0.8), SMAA 4.5% (0.6). Every scene and pass is inside them on both backends; before
  this change four of the five OpenGL chains, the OpenGL tone map (1.17 ms in storage2)
  and bloom (0.83 ms in convoy1) were over. The vehicle level was not priced. The
  fullscreen ladders of the same toggles lost the foreground in six of 30 launches on the
  shared machine; where they held they agree in order of cost, and the Vulkan hub2 chain
  was 3.66 ms of a 7.7 ms frame. Pictures: multiply composition matches the copy within
  one level on at most 439 pixels per pose; half resolution matches full within one level
  at the 99th percentile, and the PBR laboratory's matched control now holds OpenGL to
  Vulkan's limits (2.07 levels mean, +0.02 bias, 1.1% more darkened pixels; the SSAO
  controls pass 10/10 on both renderers). Outside the gate: bloom's full-resolution
  first level (a look change) and Ultra's SSAO resolution (the user's choice, about 1 ms).
  See [report section 12](performance-report.md#12-post-processing-chain-fps-04).
  Follow-up, 13 November 2026: OpenGL's SSAO world-depth snapshot is drawn into a depth
  target of its own and resolved where SSAO reads it, and the finished depth is copied
  once per view; hub2 and storage2 are 0.6-1.05 ms faster at Quality and Ultra, outdoor
  poses unchanged ([report section 12.8](performance-report.md#128-opengls-ssao-depth-copies-follow-up)).
- [x] **FPS-05 — P1, decision and implementation: MSAA and the HDR scene target.** The
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
  Closed 13 November 2026. FPS-02 had already moved Ultra to 4x; this item priced every
  count and fixed the policy. In-launch GPU ladders in hidden 2560x1440 windows at the five
  reference poses (`rendererBenchmarkCapture` GPU time against neighbouring Ultra base
  windows; both backends rebuild the scene target on an `r_multiSamples` change): OpenGL draws
  16 samples at 2.0-2.5 times the 4x frame (6.0-10.9 ms more) while the Vulkan renderer never
  draws more than 8 (`VK_Image_SelectSampleCount`), so the old Maximum was two images; 8x
  costs 0.75-1.7 ms over 4x on OpenGL and 1.8-2.5 ms on Vulkan; 4x + SMAA ultra is within
  -0.7 to +0.3 ms of SMAA alone on OpenGL (SMAA works harder without MSAA) and 0.6-1.6 ms on
  Vulkan; TAA alone costs what SMAA alone does; the LDR target saves 0.1-1.3 ms but clips
  the highlights the tone map and bloom need, so tone-mapping tiers keep RGBA16F. Policy:
  Ultra keeps 4x + SMAA ultra, Quality 2x + SMAA high, Enhanced SMAA high without MSAA, and
  Maximum asks for 8x instead of 16x, the most the Vulkan renderer draws, so every preset
  draws one image on both renderers; the presets self-test refuses a preset above 8. The
  count does not change with resolution (MSAA scales with the pixel count; render scale or
  a lower tier is the lever above 2560x1440). 16x stays a manual OpenGL choice. Visible:
  `gfxInfo` reports `reason=vulkan-clamp` and a line of the last frame's resolves (each
  copy out of a multisampled target), `r_multiSamplesEffective` (read-only) holds the count
  the last main scene drew with, and the settings menu's MSAA row carries a note naming the
  trade-off and, where the renderer draws fewer samples than applied, a line naming the
  count, in all twelve languages. Checked: captures of the page on Vulkan at a 16x request
  (the clamp line names 8x) and OpenGL (no line), the presets self-test on both, the new
  `msaa_status` case of `ui_settings_service.py`. See
  [report section 13](performance-report.md#13-msaa-and-the-hdr-scene-target-fps-05).
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
  Progress, 22 November: the storage2 composition was the Strogg gauges' static grain (the
  level follows the player's conversion, so its HUD is the Strogg one). Ten translated
  tiles of the installed `s_static` texture under each gauge's fill mask took the general
  masked path every frame, six offscreen targets in all, and the moving transform kept the
  Strogg HUD document from replaying. The grain is now one stationary quad with scrolling
  repeating texture coordinates, the EKG's own construction
  (`tools/ui/remastered_hud_status.py`; no engine change). Hidden-window counts at the
  0.13.2 settings: storage2 7 two-dimensional views and 18 render-target commands with the
  retained HUD against 17 and 34 before and the classic HUD's 7 and 17, and the same 7 and
  18 in all seven suite scenes. The retained HUD's own main-thread time fell from 0.37-0.94
  to 0.19-0.33 ms a frame (median of 30 frames, three launches each, on a shared machine).
  At rest the gauge differs from the tiled sheet in a quarter of its pixels, mostly by one
  level of 255 and by seven at most, identically on OpenGL and Vulkan; in motion it scrolls
  at the same rate. An engine path that cut the mask to each tile was tried first and set
  aside: it removed the targets but raised the HUD's own time. Still open: the fullscreen
  frame times of this change against 0.13.2, which need the machine and its display to
  themselves; bisection pass 2. See
  [report section 18](performance-report.md#18-the-retained-huds-composition-fps-06-22-november-2026).
  The Vulkan depth capture, the same day: two timing phases (`feedbackDepthResolve`,
  `feedbackDepthCopy`) put 0.60 ms of each capture in the multisample resolve and 0.04 ms
  in the copy, and the outdoor scenes take two captures a frame, one in the portal-sky
  view and one in the main view, both for soft particles and both needed. The resolve was
  not the cost: after it the multisampled depth target was moved to the sampled layout and
  straight back to the attachment layout, which on the reference GPU costs a pass over
  every sample. A depth capture now leaves the target an attachment
  (`r_vkDepthCaptureLayoutReference 1` restores the transitions). At the 0.13.2 settings
  the resolve is 0.12-0.15 ms where it was 0.58-0.64 ms in five scenes (0.18-0.21 from 0.85
  in storage2), a frame with a portal sky saves about 1 ms of GPU time and an indoor frame
  about 0.5 ms; at today's Ultra (4 samples, three captures a frame in airdefense1) it is
  0.08 ms from 0.31 ms. Pictures are identical with the game stopped in four scenes and
  the validation layer is silent in five. The capture is the same resolve into the same
  image, so this is not the prepass-depth read the original plan named; that read is no
  longer needed for the cost. Still open on Vulkan: the 7-15% more draws in the
  airdefense1 scenes and the remainder in storage2, and fullscreen frame times. See
  [report section 19](performance-report.md#19-the-vulkan-depth-capture-fps-06-22-november-2026).
- [ ] **FPS-07 — P1, implementation: hitch catalog.** Attribute and remove the stalls the
  matrices recorded: 2.0 s and 3.0 s draw stalls in a cold convoy1 pass at the current
  Ultra, periodic 100-124 ms draw frames in hub2 at the same sweep direction every turn,
  a 393 ms storage2 frame (134 ms of events, 252 ms of draw), and the first-use events of
  the `hitch_firstuse` scenario (weapons, first spawns, first explosion, scope, flashlight)
  and the level transition. Close when each class is attributed in the report, fixed or
  given a declared budget, and the hitch scenario shows no frame above a declared ceiling
  outside level loads on both backends.
  Progress, 17 November (multiplayer): a player or bot arriving with a model
  nobody had used yet had its declaration, head, skin and images read from
  disk mid-match, on the server and on every client that could see them, and
  the first fall of a match read its damage definition the same way. A
  six-bot match logged 53 such loads; it now logs none, with every player
  model loaded with the map (`g_precachePlayerModels`) at no measurable cost
  in load time ([measured](mp-audit-2026-11-17.md#measured)).
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): multiplayer
  `FindEntityDef` answers from a per-map cache; item timers collect their
  items once a send instead of three passes over every entity slot a
  recipient; an item whose model does not move keeps its PVS areas instead of
  dropping them every tic; the local HUD is published once a frame, not once
  a prediction pass; entities that are neither networked nor map entities
  skip the snapshot PVS test. None of these was measured.
  Level loads and startup, 21 November ([loading presentation](loading-presentation.md)):
  the startup and loading screens presented only when the work offered a
  frame, through two pacers that held them near 30 and 60 Hz, so an Air
  Defense 1 load showed 157 frames in 6.5 s (a 22 ms median between frames,
  p95 93 ms) with a freeze of up to 2.8 s while the opening cinematic was
  skipped, and startup a 25 ms median (p95 65 ms). One pacer
  (`LoadingFramePacer`) now presents both at the display's refresh or a whole
  divisor of it of at most 90 Hz, holding presentation under 40% of the time;
  `idCommon::LoadingCheckpoint` offers frames between the items of every long
  stretch the stall sampler named (declarations, map entities, the cinematic
  skip, script statements, stock menus, `.proc` and collision parsing, image
  headers, zip seeks); waits for worker parses keep presenting; paced frames
  skip the frame limiter's sleep; and the hit-material search, the script
  compiler's constant lookup and font atlas rasterisation that filled the gaps
  are indexed or parallel, with identical results. The same load now shows
  389 frames at a 16.7 ms median (p95 22-23 ms, worst 54-60 ms) in the same
  time, and startup a 17 ms median (p95 30-33 ms), on both renderers.
  `com_loadingFrameTrace 1` reports each period, `2` the call path behind each
  gap. The hand-off into a single-player level, 200-380 ms with the last
  loading frame on screen, now has no gap over about 50 ms: the Windows working-set
  trim after each load is gone (100-130 ms, and most of multiplayer's 720 ms),
  the start-of-level autosave writes on the checkpoint storage worker, and the
  level's first view is drawn once, unseen, behind the loading screen
  (`LOADING_HANDOFF` in the trace). A `+map` launch hands the startup view
  straight to the loading screen instead of fading to black and holding black
  (longest gap 23-43 ms, Cryofac included). Still open in this class: building a
  retained document (50-80 ms, not divisible), single large reads and
  animations (40-60 ms), and a multiplayer client's first frame (120-240 ms).
- [ ] **FPS-08 — P2, implementation: CPU-side renderer and game cost.** The HF-07
  attribution found the Quality frame CPU-bound on one thread (tic, scene front end and
  Vulkan recording in turn); the render-thread prototype's first stage gained 0.5 ms
  there and nothing at the GPU-bound Ultra (within 0.4 ms in the Vulkan ladders, with
  4 ms of the frame moved into the session phase and longer tails).
  Measure the front end, backend submission (OpenGL draw submission with shadow maps) and
  game tic per scene from the attribution runs, land the stage-2 overlap under REN-07,
  and reduce the front-end and tic work the attribution names. Close when the CPU-bound
  scenes meet their declared budgets with the GPU as the limit; shared with HF-07.
  Progress, 18 November ([report section 15](performance-report.md#15-cpu-profile-of-the-stock-combat-and-hub2-scenes-fps-08-evidence-18-november-2026)):
  a CPU profile of the stock combat and hub2 scenes ranks the main thread's
  work: BSE effects 10.6%, point-cube render-target switches 2.7%, depth copies
  2.2%, posed MD5 bounds 2.0%, per-frame static buffers 1.8%. The faded-decal
  pass now skips decals with nothing to remove (exact; 4.9% to 0.9% of the
  combat front end, `r_decalFadeEarlyOut`). A framebuffer per cube face was
  measured and reverted: the driver charges for the switch, not the attachment.
  OpenGL point cubes are now drawn into one layered target, the vertex shader
  naming each face (`r_shadowMapPointLayered`, bit-identical): non-drawing cube
  work fell from 0.80 to 0.49 of caster drawing in hub2 with the cache off and
  from 3.26 to 1.11 in the composing combat wave. Each cube still makes two
  switches, to it and back to the scene target.
  BSE particles ([report section 16](performance-report.md#16-the-bse-particle-runtime-fps-08-18-november-2026)):
  expiry stops at the first live particle of an end-time-ordered list and
  spawning appends at its tail (`bse_particleListEarlyOut`), and the next
  particle is fetched while one is drawn (`bse_particlePrefetch`); both exact.
  Effects' per-frame work fell to 0.77 of before in the combat wave (servicing
  0.43, the expiry walk 0.03) and to 0.47 in hub2. Fused surface bounds and
  two-ahead prefetching were measured slower and dropped.
  GPU particles, 21 November ([report section 17](performance-report.md#17-what-gpu-particles-could-save-fps-08-21-november-2026)):
  drawing effects costs 2-4 ms of the combat wave's CPU frame (`bse_render`
  A/B on both renderers). GPU evaluation would remove only the CPU particle
  geometry, 46% of that or about 1-1.3 ms; the soft-particle draws and depth
  copy stay. Physics particles must stay on the CPU for stock behavior.
- [ ] **FPS-09 — P1, qualification: regression gate.** Put the suite in the release
  checklist (`docs/dev/release-completion.md`, the manual release workflow): the core
  scenarios on the reference machine before every release and on every renderer,
  game-loop or asset-loading change, with the per-scenario budgets from FPS-01 as the
  gate (5% average, 10% p99; `performance_suite.py check` against
  `performance-budgets.json`), a cvar-default diff that fails when a default with a
  performance cost changes without a release note, the preset self-test, and the toggle
  ladder as the required before/after evidence in commits that touch post effects,
  shadows or presentation. Close when a release candidate has been gated by it once.
- [ ] **FPS-10 — P1, qualification: the low tiers hold their caps on low-end PCs.**
  Minimum and Low Power must hold 30 fps and Low 60 fps at native resolution on the
  low-end hardware of the past decade, without lowering the render scale (presets
  never change it). Measure each tier at its own frame cap with `performance_suite.py`
  (old and new recipes interleaved, GPU phase timestamps for attribution) on an
  integrated GPU at 1920x1080 on both backends, and on an older iGPU (Intel UHD 620
  class) and a decade-old quad-core CPU where available; decide which tier Auto-Detect
  gives integrated GPUs; reduce the engine costs that every tier pays on such GPUs
  (the Vulkan depth capture's blit, the portal-sky view). Close when every low tier
  holds its cap in the timed scenes on that hardware and Auto-Detect recommends a tier
  that does. Completed so far (24 November 2026): Low no longer draws stencil shadows
  or the light grid and holds 60 fps in all six timed scenes on the Intel Iris Xe at
  1080p on Vulkan, where it ran 44-55 fps in combat and outdoors; Minimum and Low Power
  hold 30 fps there ([performance presets](ui/performance-presets.md#low-end-measurements-24-november-2026)).

## Virtual reality

Sources: [OpenXR implementation history](archive/2026-10-18/plans/2026-10-02-openxr-vr.md),
[player guide](../user/vr.md) and current
`src/sys/openxr/OpenXRSystem.cpp`.

### VR completion checkpoints

**Estimate: 75%; 4/7 tasks closed.** Credit the OpenGL and Vulkan PC paths,
their fault handling, play spaces and per-eye TAA on the simulated runtime.
Synthetic stereo results do not qualify a headset, its comfort, its frame
budget or standalone Quest; those are the [headset checklist](vr-headset-qualification.md)
and AND-05.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| VR-01 — Closed (PC scope) | Vulkan (runtime-created device), desktop-independent submission, Wayland EGL and SP world-GUI pointing integrated at render API 32 with VR-06's guarantees; GL/Vulkan SP/MP/menu smokes, the Vulkan fault matrix and Linux Wayland/X11 bindings pass on the simulated runtime. | Quest standalone moved to AND-05 by decision; physical headsets are VR-02. |
| VR-02 — Partial | GL and Vulkan stereo, tracking/input, room scale, play spaces, aiming, haptics, world GUIs, per-eye TAA, comfort controls and (17 November) the player's own body with arms that reach the hands (`vr_body`) provide the testable implementation; the [headset checklist](vr-headset-qualification.md) is ready to run. | Run the checklist on each available PC runtime/headset: combat, refresh/latency, controls, collision, vehicles, HUD/world-GUI readability and recovery. |
| VR-03 — Closed | STAGE/LOCAL_FLOOR/LOCAL selection with warned fallback, floor-referenced standing height, recentering, calibration and live seated/standing changes pass natively and on the simulated runtime (GL/Vulkan). | Physical body alignment is checklist section 4 (VR-02). |
| VR-04 — Partial | `openxr_vr_stereo_cost.py` records flat/stereo/warm-stereo CPU, build, submit and GPU tails, now at a chosen preset (`--preset quality`); the simulated baselines on this GPU are recorded below. Uploads, dynamic geometry, remote cameras, cached static shadow maps and (8 November) world-GUI quads are shared between the eyes; culling, lights, draws, mirror/reflection/refraction/x-ray maps and dynamic shadow composition stay per eye. `openxr_vr_stereo_share_smoke.py` proves both by work count on OpenGL and Vulkan. | Headset tails against the refresh budgets (checklist 2.2), and sharing work where those tails show a view-independent cost. |
| VR-05 — Closed (approved scope) | Decision: per-eye TAA only. Each eye has its own view identity, history images, motion vectors and resets in the SP game chain and both backends' own temporal scenes; dynamic resolution stays off in VR. SP/MP GL/Vulkan pass the simulated-runtime smoke. | Headset comfort is checklist section 7. Hand presence arrived with the body (VR-02, 17 November); physical-object interaction needs its own design. |
| VR-06 — Closed | Active GL teardown/ownership repair passes native failure cases and simulated-runtime SP/MP gameplay. | Carry guarantees into VR-01 backends; physical controller/headset validation remains VR-02. |
| VR-07 — Partial | 24 November 2026: desktop stereoscopic 3D (`r_stereo3D`) on OpenGL and Vulkan: side by side (half and full width), top and bottom, red/cyan anaglyph (Dubois) and row interlaced, with depth, convergence and eye swap; SP, MP, menus, the paused menu's backdrop and window-sized screenshots captured in every layout; Settings rows on both interfaces in all 12 languages. | Physical stereo displays (a 3D TV or monitor, side-by-side glasses, a passive interlaced panel, anaglyph glasses); the zero-parallax crosshair; Apple GL and GLES, which decline it. |

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
  Quest (`65aa9bd88`) was left to AND-05 by decision and is integrated there
  (12 November). Physical headsets remain VR-02.
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
  snapped; and a mirror that ignored window resizes.
  11 November: six leads that audit verified are fixed
  ([release record](release-completion.md#vr-room-scale-steps-without-a-stutter-vehicle-saves-that-load-facing-forward-and-damage-numbers-on-their-targets-11-november-2026)):
  the interpolated eye carries the room-scale step the tracking space already
  took (`ShiftTrackingOrigin` reports whether it took it; up to a tic's walk
  of shimmer before, both game modules); a seat restored from a save faces the
  vehicle's eye once VR drives the usercmd again; multiplayer's vignette
  leaves a room-scale step's speed out; multiplayer damage numbers project
  through the HUD layer, not the right eye; a demo recorded in VR writes the
  left eye's views and none of the eyes' 2D; and a checkpoint preview reads
  the left eye, not the desktop mirror. The play-space and multiplayer smokes
  check each one, and each check fails on a build with its fix switched off.
  The audit's last lead is fixed the same day: an OpenGL frame whose
  virtual-screen copy failed, or that reached no target, stayed marked
  presented, so the engine submitted images it never wrote. It now submits no
  layer, as Vulkan; the test runtime's `imageFill:<n>:unbound` fault breaks
  the screen's images for `openxr_lifecycle_runtime.py`, which failed before
  the fix.
  17 November: the player's own body in the headset, and a third audit
  ([release record](release-completion.md#your-own-body-in-vr-and-a-headset-audit-17-november-2026)).
  `vr_body` (default 2) draws a second render entity of the player model in
  the player's own view only, posed on every draw by `src/framework/VRBody.h`
  in both game modules: the model's rest-pose trunk, scaled to the game's eye
  height, hangs from the neck under the tracked head and turns after the
  head's heading in the tracking space; the legs keep the animation's feet
  and bend to the pelvis; each arm meets the view weapon's own forearm at an
  elbow solved from shoulder to wrist, so the gun keeps Raven's hands, and the
  support hand follows its controller except in the two-handed grip and while
  the weapon is busy. The stock view models and The Awakening's two new ones
  share one arm rig (checked by reading every model's joints; the expansion's
  were not probed in game). The audit, three independent readings of the
  engine, game and renderer sides, led to these fixes: a held gun no longer
  depth-hacked over the world; facing resets and VR's takeover of an untracked
  view face the head, not the weapon hand; frozen views no longer re-anchor
  the world to the hand; turns pivot about the head; input released when the
  runtime stops a session; Valve Index grip bound to its force; recentre from
  the controller, on first focus and at each level, with no standing height
  learned below a metre; head-aimed shots and the water line taken at the
  tracked head; a comfort vignette fixed to the lens under zoom and open
  while paused; the fallback liquid wash drawn into the eyes; motion blur off
  in eye views; per-frame flashlight placement in the hand; and haptics for
  the lightning gun and gauntlet. `vr_bodyAhead` stands the body ahead of the
  player for inspection. Audit leads left open, none blocking the checklist:
  missing buttons on Vive wand, Index and WMR profiles (no flashlight or
  jump on some); the comfort vignette centred on each eye's own axis, which
  canted displays would not fuse; one 100 ms swapchain wait ending the
  session, and any `XR_ERROR_RUNTIME_FAILURE` from an optional call stopping
  VR until `vr_restart` (both pinned by the fault matrix); the third-person
  model's yaw spinning while the gun points near the vertical (mirrors and
  shadows only, now that the body in the eyes has its own heading); the
  projectile launch clearance assuming a muzzle inside the body's box; a
  room-scale catch-up walked in one tic after a pause; multiplayer facing
  resets, which the server sets from the hand's usercmd; no body in vehicle
  seats; and a left-handed hold that uses the model's simpler hands. The
  setting is **Your Body** on the Virtual Reality tab, in all twelve
  language tables, on the stock and the retained pages.
  The same day a second round took those leads
  ([release record](release-completion.md#vr-controllers-with-fewer-buttons-steadier-sessions-and-the-audits-other-leads-17-november-2026)):
  Index's left trackpad force is the flashlight, and Vive wands and Windows
  Mixed Reality controllers press their touchpads by region
  (`VR_TrackpadRegion`), proven per profile by
  `openxr_vr_controller_profiles_smoke.py` (the test runtime reports a chosen
  profile, `OPENQ4_XR_TEST_PROFILE`); a swapchain wait that times out is made
  again for up to a second; optional calls (haptics, the profile's name,
  refresh rate, performance levels, thread hints) go through `CheckOptional`,
  so a runtime failure there no longer stops VR; the comfort vignette centres
  on the head's forward in both eyes; the third-person model holds its heading
  past 55 degrees of aim pitch (`VR_ModelYawFollow`); a hand-held muzzle and a
  projectile's way are cleared from the hand; a single-player room-scale
  catch-up walks at no more than 5 m/s (`VR_RoomScaleCatchUp`); a multiplayer
  client turns its body onto a facing the server set from the hand
  (`idVRSystem::TurnBody`, game API 60), and the server holds a VR client's
  delta through frozen views as the client does; and an exposed seat shows the
  seat's own pose under the head (`vrBodySeated`). The model heading others
  see still comes from the aim, held near the vertical, not from the head,
  which the server never learns.
  Left-handed hold, 17 November 2026: the view model's own arms now hold a
  gun in the left hand, drawn by an entity of their own as the mirror image
  of the right-handed hold across the gun's upright barrel plane
  (`rvWeapon::GetVRArmsMirror`, `VRBody_MirrorPlacement`); the gun stays as
  modelled and sits in the left hand by the mirrored grip. The earlier
  attempt mirrored partner joints by name, which only the machinegun's rig
  allows: the other rigs hang the left wrist on the gun or number their
  fingers differently, and an offline skinning check stretched their arms up
  to 24 times. A reflected model winds the other way round on screen, which
  the legacy GL passes (`RB_SetSurfaceWinding`, at each surface-space change)
  and the Vulkan per-surface passes (`vkSurfaceWinding_t`) now honour, as the
  modern GL executor did by `negativeScale`; before, both drew such a model
  inside out. `openxr_vr_hold_closeup.py` frames every gun's hold close up,
  right- and left-handed, through the test runtime's new `lens` command, and
  sets each left-handed capture, flipped back, beside the right-handed one.
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
- [ ] **VR-07 — P3, implementation and qualification: stereoscopic 3D without a
  headset.** Present a stereo pair to the window for 3D televisions and
  monitors, side-by-side glasses, passive interlaced panels and anaglyph
  glasses, on both desktop renderers, with the HUD and menus readable in every
  layout. Implemented 24 November 2026
  ([player guide](../user/stereoscopic-3d.md)): `r_stereo3D` opens each frame
  as a stereo frame of the VR target machinery with no swapchains; the game
  renders its scene chain once per eye (`idPlayerView::StereoView`, eyes from
  `Stereo_BuildEyeView`: parallel axes and frusta shifted to meet at
  `r_stereoConvergence`, as ioquake3's `r_zProj`), the 2D lands on the
  premultiplied screen layer, and the swap composites the three
  (`RB_DesktopStereo_Composite`, `post_stereo.frag`). A capture of the screen
  layer reads the left eye, so the menus' blurred backdrop keeps the scene.
  Checked by window-sized screenshots in airdefense1, q4dm1, the title and the
  paused menu on both renderers, and by the retained and classic Settings
  captures (`capture_retained_screens.py` scenarios `game-stereo` and
  `game-stereo-stock`). Open: qualification on physical stereo displays, a
  crosshair at the target's depth, and the Apple GL 2.1 and GLES renderers,
  which keep a flat frame and say why.


## Android and mobile rendering

Sources: [Android build and packaging](android-build.md),
[port integration and credits](android-gles-integration.md),
[standalone host](../../mobile/android/README.md).

### AND completion checkpoints

**Estimate: 70%; 1/5 tasks closed.** Onboarding, touch and several lifecycle
repairs are already integrated; they are not all still archived. S25 and emulator
checks are bounded evidence, not the complete Adreno/Mali/API/lifecycle matrix.
The Quest flavor is integrated, with emulator and test-runtime evidence only.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| AND-01 — Closed | Phone/emulator scope, startup ownership, UI-thread binding, background handling, Back/cutouts and touch controls repaired; S25 save/touch/audio checks exist; host-owned orientation and the portrait thumb deck pass a mid-match emulator rotation; the per-orientation layout editor, suspension save, multiplayer resume/reconnect and Quest flavor package checks pass native/JVM and emulator probes. | None; S25 hardware checks pass (layout, portrait, suspension save, background kill/restore, multiplayer reconnect, editor by hand). Residuals moved to AND-02/AND-04. |
| AND-02 — Partial | The device harness and S25 (Adreno 830, API 36) campaign pass cold and warm: real level transitions, save/load, audio residency, storage, thermal and per-model budgets. | S25 sustain and multiplayer phases; audible and gamepad/mouse/keyboard checks; Mali and API-floor devices. |
| AND-03 — Partial | Verified package repair/import, localized setup, settings return, control/interface setup and save/log export implemented; scoped device round trips pass. | Release-signing continuity; cloud/removable providers; interrupted-process recovery; physical setup/accessibility and upgrade matrix. |
| AND-04 — Partial | Touch presets, UI/text sizing, an opt-in portrait orientation, retained safe areas, HUD safe-area and thumb-deck positioning and the per-orientation layout editor exist. | Device-based presets and GLES limits; keyboard/touch targets, human-finger portrait play, the weapon control over the portrait readouts, stock GUIs in portrait, cutouts/gesture bars/folds/multi-window qualification. |
| AND-05 — Partial | The archived Quest change (`65aa9bd88`) is integrated with refresh-rate, performance-level, thermal and frame-thread services; the `-Popenq4Quest` release APK with its generated headset manifest passes `verify_android_apk.py --variant quest`, and the emulator plays Air Defense 1 in stereo on the test runtime (12 November). | On a headset: installation/assets, stereo gameplay, controller/UI interaction, comfort, session recovery and sustained thermal/refresh budgets; distribution decision. |

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
  [Quit abort fixed](platform-recovery-review.md#android-quit-abort-13-november-2026)
  (13 November): quitting through `exit()` ran libhwui's static destructors,
  which destroyed the HWUI worker pool's mutex under its threads and aborted
  every quit. Android now leaves with `_exit()` once shutdown is done, and the
  emulator smoke fails on any native abort in the engine's process.
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
  Integrated 12 November ([evidence](platform-recovery-review.md#meta-quest-flavor-integration-12-november-2026)):
  the archived change's Android loader, ES session binding and GLES
  presentation sit on the current renderer, and its sRGB decode draw now
  invalidates the GLES program and texture caches. Where the runtime offers
  them, the engine also chooses the refresh rate (`vr_refreshRate`), holds the
  CPU/GPU level (`vr_performanceLevel`), reports thermal limits and names the
  frame thread. Gradle `-Popenq4Quest=true` packages `com.darkmatter.openq4.quest`
  with a manifest generated from the phone's, and its release APK passes
  `verify_android_apk.py --variant quest`. On the x86_64 AVD the test runtime,
  which only the emulator's debug APK may carry, runs Air Defense 1 in stereo
  with a head turn, the 90 Hz request, sustained-high levels and a thermal
  report (`android_emulator_smoke.py --mode vr`). This is emulator evidence
  only: every qualification point above still needs a headset, and the
  packaging policy a decision.

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
| UI-01 — Partial | Settings hub, GAME/CONTROLS/AUDIO, Load Defaults, DISPLAY (with presets applied there) and Player Setup exist; current scaling/tall layouts and effect/readback increments recorded. Settings tree audit (20 November 2026): tabs and sections grouped by what each row changes, GAME OPTIONS' VIEW WEAPON and ACCESSIBILITY tabs, DISPLAY's EFFECTS tab, and Frame Rate Cap, Anisotropic Filtering, Shadow Maps and Motion Blur rows ([retained screens](ui/retained-screens.md#the-settings-tree-audit-20-november-2026)). Dependent rows (23 November 2026): a row another setting turns off is dimmed and skipped on every retained settings page, each dependency checked against the code ([settings structure](settings-menu-structure.md#dependent-rows)). | Complete settings transactions/failure/restart/capability matrix; real-font/state coverage; production SYSTEM editor round trip and device/platform acceptance. |
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
  Progress, 11 November: the DISPLAY page starts
  [applying to the running renderer](ui/settings-live-apply.md). V-Sync is an
  immediate setting, a texture batch reloads images in place, and a
  Performance Preset restarts no device on Vulkan unless it changes MSAA.
  The same day a display change (window size, fullscreen, borderless,
  display, mode, refresh) became a transition on the running device
  (`TryDisplayTransition`, render API 34), with Keep/Revert, recovery and a
  fallback to the restart unchanged.
  On a visible desktop every mode change and undo was a transition on both
  back ends, and Vulkan kept fullscreen and the return to a window.
  A transition no longer advances the restart count, which had made the
  retained interface reload every document (2 to 5 s to the first frame, now
  under 0.3 s), and a window that leaves fullscreen is centred.
  The game now rebuilds its scene targets when `r_multiSamples` changes, on
  both back ends, and Vulkan applies an MSAA change as a transition, so no
  Performance Preset restarts Vulkan's device.
  Remaining, in order: a focused Keep under borderless and exclusive
  fullscreen, and any Keep on OpenGL (the test window had no focus); and an
  image reload spread across frames.
  OpenGL's window is single-sample by default now (`r_glWindowMultisample`,
  1 on Apple): 3D views drawn straight to it go through a multisampled
  stand-in, so an MSAA change is a transition there too and no preset
  restarts OpenGL. At 4x the view-capture sequence matches the multisampled
  window (eleven of twelve images exactly); Intel, AMD, a visible window and
  the classic `renderDef` pages were not run.
  OpenGL now resizes its upload stream on the running device, so a preset
  restarts OpenGL only when it changes MSAA.
  Stage 4, the same day: a changed `image_ignoreHighQuality` re-parses the
  materials that read the hint, in the session, and Restart Renderer works
  inside a single-player game, which is saved, reloaded around and resumed
  (both directions between OpenGL and Vulkan ran in Air Defense 1). A GPU
  change, the Awakening campaign and multiplayer were not run; multiplayer
  is not carried.
  Stage 7, 21 November: a texture setting reloads only the images it reaches
  (each load keeps the reduction key it was built under), through the level
  load's file prefetch, and leaves generated and persistent images alone. A
  change during a level load is no longer lost for the images the load kept.
  Stage 8, the same day: the reload runs a few milliseconds each frame
  (`image_reloadFrameMsec`, default 4) instead of in one pause, and finishes
  behind the loading screen when a level load interrupts it. A normal-map
  change in Air Defense 1 went from one 1.2 to 1.4 s frame to 145 frames of
  at most 33 ms.
  Stage 9, the same day: a texture that decodes from its source (precompressed
  textures off, image programs) is decoded, resampled and compressed on image
  workers (`image_reloadWorkers`, default 2) from file reads the main thread
  recorded, with byte-identical results. With precompressed textures off the
  main thread's share fell from 15.5 to 27 s to 9 to 14 s, cache lookups aside.
  Left: the file work itself (cache lookups, source reads, cache writes) stays
  on the main thread, because the file system cannot open files off it.
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
  Progress, 17 November: while The Awakening runs, the retained pause and its
  Load Game, Save Game and Restart Level pages take the expansion's own family
  (visual specification 1.40 to 1.45): horned, frosted bands that close in as
  the menu opens and glide between levels, blade-ended plates with the lens,
  snow and haze ([The Awakening's pause](ui/retained-screens.md#17-november-2026-the-awakenings-pause),
  `FLOW-071`), and so do its SETTINGS page and the CONTROLS, GAME OPTIONS,
  DISPLAY and AUDIO categories. Remaining: the objectives display's frames,
  tab and list scrolling, a page scrolling out on Back, the high-contrast and
  power-saving variants, and acceptance captures.
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
  Progress, 20 November: [menus in motion](ui/menu-frame-costs.md) were
  measured with their opening animations playing. The Awakening pages'
  drifting snow recompiled every frame (9.9-12.7 ms of CPU, about 2,100 draws a
  frame) and an open dialog tessellated its glow's turning mask and relaid out
  the page behind it every frame. A sliding vector now compiles at half-pixel
  phases its cache holds, many-path vectors draw as one or two geometries, each
  glow pulse turns under a GPU linear mask and a vector leaf's fade skips the
  layout pass: the Awakening pause fell to 0.24 ms and 167 draws. A shown
  toggle, slider or list no longer lays the page out every frame (GAME
  OPTIONS, DISPLAY, AUDIO at rest): its view repaints after a state change, so
  the settings pages, Awakening's included, take 0.05-0.3 ms a frame; number
  fields and scrollbars at rest do the same (Create Server 0.79 to 0.05 ms,
  Join Game 0.48 to 0.07 ms). Only an edited field or a held pointer lays out
  every frame. Open: the Awakening documents' 2.3 MB of snow and frost.
  Progress, 21 November: [event costs](ui/event-costs.md) were measured with
  a new `--event` benchmark mode. An event copied the document's whole state
  several times (Join Game's row focus 1.5 ms, a CONTROLS tab 2.5 ms, the
  Escape card's opening 4.2 ms on a quiet machine, up to twice that under
  load) and every interaction update searched the page's property tables for
  each input branch. State tables are now shared by copies until written,
  input eligibility is read once per state and motion revision and list rows
  compare RmlUi values: those events take 0.04, 0.11 and 0.34 ms and the
  HUD's events about 0.01 ms. A motion now holds only the values written
  over its authored ones, so an animated page no longer keeps a second copy
  of them (the Escape card 15 MB less). Open: the view build's spread-out cost.
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
  Progress, 13 November: [menu sounds](ui/menu-sounds.md) now come from one
  semantic cue stream and one router instead of 78 per-request session plays.
  A device event that moves the lit control or option ticks
  (`main_menu_mouseover`), and every completed press, value cycle, list or edit
  opened or closed, and Back selects (`main_menu_selection`). There is at most
  one sound per input event, played in the session's playing world. Value
  sliders stay silent, as retail's did. In a level the settings sub-pages
  now keep the menu world playing instead of the paused game's. Native
  interaction, runtime and adapter tests pass, and device-free OpenGL engine
  checks on the title, the `airdefense1` pause and settings pages and a DM
  Welcome card log one sound per step in the right world. The single-player
  loading screen's `load_screen_ready` and `load_screen_click`, silent under
  both interfaces, now play once each from the session's continue gate.
  `MOT-006` acceptance still needs physical-device sound counters and a
  listening review.
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
| Q4-02 — Partial | All 31 stock stage transitions with start-of-map save/load pass GL/Vulkan; fresh Turbo hangar lift progression passes with failing old-behavior control; 0.16.0 C2 sweep and chain warnings classified (23 kinds, none a defect); tram1b/mcc_1 AAS refusals proven retail content and pinned. | Ordered gameplay between triggers: AI, set pieces, bosses, vehicles, cinematics/terminals and checkpoints on supported paths; classify warnings raised during play. Already-stuck lift saves remain limited. |
| Q4-03 — Partial | Retail normal/tangent and initial-light questions settled; telemetry repaired; stock spark stop/restart/delete/save lifecycle passes both APIs. | Per-effect visual/audio parity, collision/child effects/trails, subviews, high refresh and full save/demo lifecycle. |
| Q4-04 — Partial | Thirteen-map integration; targeted freeze, speeder, boss, corpse-pin, isolation and occupied turret/walker save checks; bounded M01/M02 repairs recorded. | All thirteen ordered playthroughs; complete boss kills, walker repair/unlock/demolition, squad/extraction routes, sustained turret effects and actual Awakening-to-Arena switch. |
| Q4-05 — Partial | Recovery UI and object/handle/reference/animation/AF/event cleanup hardened; corruption/reload and historical-save regressions pass scoped tests. | Broader owning graphs and interruption boundaries across target platforms; maintain valid saves and explicit refusal without inventing header-only recovery. |
| Q4-06 — Partial; optional P3 | Raw inventory and field codecs restored; v5 portable stamp/encoding implemented; Windows/Linux bidirectional stock/Awakening cross-loads pass. | Confirm outstanding CI/Apple codec runs; macOS and another ARM64 runtime cross-load/resave directions; publish player migration guidance with accepted scope. |
| Q4-07 — Partial | Original-writer Windows corpus substantially qualified; v2 stamp reporting repaired; exact HiDef fixture investigated. | Resolve #123 against the reporter's actual GUI/content; other ABIs; recover/reproduce absent stage-two artifacts before claiming independently reviewable acceptance. |
| Q4-08 — Partial | Durable commit/recovery implemented; 56 process-interruption points, locked-file failure and NTFS/ext4/WSL 9p checks recorded. | APFS result, removable/network filesystems and actual power-cut behavior; retain measured CRC/sync costs and last-valid-save preservation. |
| Q4-09 — Partial | Render demo v11: one recording spans levels, save loads, death reloads and restarts as self-contained segments; gameplay-only frames; level loads, continuous timeline and level markers in playback; campaign-aware library and gate; whole retained HUD and in-world screens recorded. Render demo v12 predicts animated-entity joints exactly, a quarter smaller. Render demo v13 records every frame at the played frame rate instead of 30 Hz. Render demo v14 carries the stream in checked, packed blocks with keyframes: 40% of v13's size, files past 2 GiB, seeks in about a second, and playback of what is intact of a cut or damaged file. Windows checks pass with stock and Awakening content: GL recording, GL and Vulkan playback. | Vulkan recording, Linux and macOS, a session that is really hours long, a multiplayer recording in the block container, a listening pass, a smaller exact joint coding (joints are now most of a recording), the masked retained HUD groups that replay unmasked, and local sounds already playing at a segment start. |

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
  Progress, 23 November: the REL-01 sweep's `tram1b` report, AI that "cannot
  path" although `tram1b.aas32` was being loaded, is retail content, not an AI
  defect. The shipped [`tram1b.aas32` and `mcc_1.aas48`](retail-baseline-ledger.md#stock-aas-refusals-on-tram1b-and-mcc_1-23-november-2026)
  are compiles that kept no navigation areas, and retail 1.4.2 refuses both
  with the same check. The AI that request them are tram occupants or hidden,
  passive stand-ins that never path. The loader and AI warnings now name the
  cause and the file. `stock_aas_availability.py` pins both messages, and
  given the retail packs, also pins the two empty files and their placed
  users. The ledger also classifies the C2 sweep's 21 map-start warning kinds,
  the same on both renderers. It adds two more kinds from REL-01's C2 chain
  rerun on OpenGL, which again passes all 62 loads in order with no errors.
  None is an openQ4 defect. Gameplay between triggers remains unplayed.
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
  Progress, 18 November: the menu switch between Quake 4 and The Awakening
  remounts in place (`idCommonLocal::RemountCampaign`) instead of reloading the
  engine; decl fingerprints after a switch match fresh starts in both
  directions, and m01/airdefense1 play after a switch. A switch from a running
  level and MP entry from The Awakening keep the engine reload; Linux and
  macOS runs of the in-place switch, with and without a mod over the
  expansion, remain to qualify here.
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
  `turret_hud.gui`, Remastered `remastered_chaingun_turret.rui`: the gunner's
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
  Its pacing revision 2 (17 November) restores a steady siren rhythm after
  playtesting found the reminders too sparse and faint.
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
  Silent names, 14 November: content that names a dialogue key, radio sound or
  joint which does not exist fails without a warning, so the earlier reviews did
  not reach it. Seventeen scripted lines now play the recordings their scripts
  meant, the nine radio calls of the M06 cannon defence among them; the Tank and
  the Retch see from their heads, where they saw from their feet; the Tank's
  rockets flash and the opening dropships light both thrusters. The tables,
  evidence and limits are in the
  [mission audit](awakening-missions.md#silent-names--14-november-2026). Hidden
  input-free probes measure each correction against `g_awakeningContentRepairs 0`.
  Scene timing with the restored lines and the look of the two effects have not
  been reviewed by ear or eye, and the playthroughs remain.
  A second pass the same day sorted the 376 recorded lines the alpha never plays
  (most are earlier takes of lines that do play) and restored what has a
  provable place: Cryofac's three music cues, which named tracks under the wrong
  mission number, Navajas's six charge-setting lines there, whose sound
  declarations name a folder that does not exist, the radio marine's reply in
  M06's Renner scene, three more replies from M04's Fox marines, and three
  replies in Cryofac through script revision 4. The races' unused chatter has
  no recoverable placement and stays out. See [the second pass](awakening-missions.md#replies-the-scenes-stop-short-of--14-november-2026).
  Missing voice lines, 16 November: the Pain Lord's three tube marines play
  stand-in screams from installed recordings, and Darnell's undeclared m08 radio
  line no longer beeps (both m08 lines are recorded and still to be restored, as
  are m01's 23 "+use" replies a key slip turns into beeps). m01's 29 draft radio calls (words known, no audio,
  draft script runs nowhere) are placed at the shipped battle's scenes through
  `m01_radio.script`, gated on their content. Open: voicing them (25 anonymous
  squad lines, 3 Markswell), and the 100 draft lines whose scenes the shipped
  maps lack. See the [register](awakening-missing-voice-lines.md) and the
  [voice generation breakdown](voice-generation-breakdown.md).
  M09 frost, 21 November (hidden, input-free, Normal, OpenGL): all three
  freezing-fog slabs move on the map's schedule, hurt once they reach head height,
  ignore armour, and respond to their second-stage triggers; crouching clears the
  finale's resting slab. A descent made against it with player movement, the
  other skills and the other platforms remain. See
  [M09 frost hazard](awakening-missions.md#m09-frost-hazard--21-november-2026).
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
  Render demo v13 (11 November 2026) records at the frame rate the game is played
  at ([frame-rate recording](multiview-demos.md#frame-rate-recording-v13-11-november-2026)).
  Recording had held the live game to two tics a frame, 30 Hz, and multiplayer
  render demos, which recorded every frame, played back in slow motion. Every
  gameplay frame now carries its own time and playback paces every demo on frame
  times. Hidden-window GL checks: the game kept its 60 Hz (and a 45 Hz) cap while
  recording, both recordings and the v11 and v12 demos replayed every frame,
  recorded action played at real time at 1x, 2x and 0.5x, and a multiplayer
  recording that `main` played at 0.48x speed played at real time. A review
  of the change also found that playback stopped on a joint checksum when an
  animated entity took a freed handle with the same joint count (v12 too);
  playback now starts a fresh joint history at every raw record, and both
  reproductions play in full.
  Render demo v14 (14 November 2026) replaces the single LZW stream with a
  container of checked blocks
  ([block container](multiview-demos.md#block-container-keyframes-and-large-files-v14-14-november-2026)).
  Each block carries its own checksums and its place on the timeline and is
  packed in two stages, long-range matches against the last 2 MiB and then
  DEFLATE, on a worker thread. An 82-second, two-level recording at 60 frames a
  second is 56.9 MB where the same stream as LZW is 140.0 MB. A keyframe every
  ten seconds gives each level further ways in, which playback in order skips:
  seeks that took 1.7 to 5.1 s by replay take 0.6 to 1.1 s, and none loads a
  level it does not land in. A recording killed mid-flight plays every frame
  that reached the disk and is listed with its length; one flipped bit costs
  the frames up to the next keyframe (83 of 4,913 in the test) instead of the
  rest of the file; a recording ends itself cleanly before the disk fills or
  at a size limit; and a 2.35 GiB test file opens, lists and seeks beyond the
  2 GiB mark, where no demo could be opened before. Effects that playback
  learns of late are now shown in full flow rather than from their opening
  burst. A native test runs the container without the engine (2,264
  truncations, 400 bit flips, hostile payloads), and the v11 to v13 recordings
  still play every frame on OpenGL and Vulkan.
  Remaining: Vulkan recording, Linux and macOS, a session that is really hours
  long (the 2 GiB check used a synthetic file), a multiplayer recording in the
  block container, a listening pass (every run was silent), a smaller exact
  coding of animated entities' joints, which are now most of a recording, the
  retained HUD's masked groups, which replay without their masks, and local
  sounds already playing when a segment starts.

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

**Estimate: 90%; 3/6 tasks closed (17 November 2026).** PHYS-01, PHYS-04 and
PHYS-05 are closed. The enhanced solver is implemented behind
`g_enhancedPhysics` (default `0`) with its Settings row, telemetry, workloads
and harnesses, and legacy mode still replays the pre-solver binary's workload
digests exactly.

The third round (16 November) ended with the same build passing on Windows
and failing three checks on Linux. The fourth (17 November) found that
neither was the enhanced solver. **The Linux and macOS builds of idlib
compiled approximate square roots and trigonometry (`_FAST_MATH`) where
Windows compiles the exact ones.** A one-unit difference in the last place of
an animation quaternion moved the right knee joint by 5e-7, which flipped the
frame the ragdoll's thigh bone gets at load by a quarter turn, which pointed
the knee's limit sideways against every pose a map stores: placed ragdolls
blew apart at up to 1,600 units/s when a level started, in legacy mode too.
`_FAST_MATH` is now off everywhere and a CI test holds it off. The third
failure, a rocket splash on resting corpses, was the test's staging: each
mode had killed and settled its own corpses, and enhanced ones lay closer to
the blast. The envelope now hits both modes from one saved scene. With both
changes the three Linux checks pass, and the rigid-body stack workloads
produce the same state digests on Linux and Windows in both modes.

**What keeps PHYS-02 and PHYS-03 open is processor time.** With the map's own
ragdolls at rest enhanced costs more than legacy on two of the five
single-player workloads on each platform: 3 % and 24 % on Windows, 65 % and
34 % on Linux (2 to 54 ms over a workload), and less or the same on the
others (75 % less on the sloped stacks). Bounds that would cover the third
round's figures are written into the [physics reference's
budgets](physics-reference.md#budgets-and-tolerances) as proposed exceptions
for the project owner; they are not accepted, and the Linux ragdoll figure has
since grown past its proposed bound. **The default stays `0`.** The user asked
for the round to be wrapped up before the full Linux pass had been repeated on
the corrected build, so that repeat is open too. Each open point is in the
[physics reference](physics-reference.md#known-defects-and-what-was-tried);
the evidence is in [its Evidence section](physics-reference.md#evidence).

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PHYS-01 — Closed (16 November 2026) | Contract recorded: one archived option with a localized Settings row on both interfaces (12 languages), committed at map load, servers publish `si_enhancedPhysics`. Legacy CPU distributions on the reference workloads with a per-frame noise floor, the legacy impulse envelope per damage path, and the budgets and tolerances (pooled stagings, the joint-stretch rule) in the [physics reference](physics-reference.md). `physics_defects.py` stages all seven defect classes in stock maps in both modes: legacy shows resting jitter (1,767 sleeps and 1,757 wakes on the sloped stacks) and joint stretch (85 units, visible for 42 frames); twitching, sliding corpses, limbs stuck in walls and tunnelling up to 9,000 units/s did not reproduce as legacy defects and are recorded as such. | None. New repros belong to the item whose gate they test. |
| PHYS-02 — Partial | Semi-implicit rigid bodies with persistent warm-started contacts, sleeping after a ten-step quiet window and selective sweeps. Contact gaps come exactly from the collision model's contact features. Friction is calibrated against legacy on flat-floor shoves (three times the authored contact friction); impacts are frictionless and rebound by the body's bouncyness, on a contact the body already had only against fixed geometry. A body that has not moved since it was created rests by the legacy test, so placed pickups stay put (Network 1, Core 2 and Storage 2). Legacy replay identical. Native test: no energy gain, settled stacks, no tunnelling through a 2-unit wall, impacts slide and rebound; no tunnelling in 18 map launches. Blasted and shoved crates and barrels pass every class on the crater and on a flat floor (on Linux too, on the third round's build); shoves of 4,000 to 40,000 on a flat floor end 0.7x to 1.2x as far as legacy's. From 17 November the stack workloads give the same state digests and step counts on Linux and Windows in both modes. CPU with the map's own ragdolls at rest: 75 to 78 % below legacy on the sloped stacks on both platforms (1,667 wakes against 11). Stills of the stacks reviewed side by side. | CPU budget: `sp-flatstacks` costs 24 % (2.8 ms) more than legacy on Windows and 34 % (4.1 ms) on Linux, because legacy rests a placed stack on its first step (164 moving steps against 425 on both platforms); `sp-explosion` costs 3 % (2.2 ms) more on Windows with a first-step frame 0.9 ms worse, and the same as legacy on Linux. Either fix these or have the proposed exceptions accepted. Visible inside the gates: a body knocked loose on the crater's slope travels about half as far as legacy's, and which sloped stack survives a knock differs. A windowed review in motion. |
| PHYS-03 — Partial | Warm-started PGS on the legacy constraint matrix with an exact-LCP fallback (0.6 ms of a 58 ms workload), bounded recovery, quiet-figure sleeping, death-pose velocity inheritance, an energy guard that keeps the figure's momentum, frictionless impacts and no figure bounce (as legacy). Round two: joints pulled apart close by position only through the tree solve, the surface that stops a body joins its contacts, and position-only motion is solved against every contact and swept on its own. Round three: exact contact gaps, so an edge-on-edge contact acts at the ledge and not at the limb's end; bodies a map places inside static geometry are not pushed out of it; a slow body is swept along a two-unit chord every eight steps. All seven defect repros pass on Windows: joints 37 units apart for 22 frames after a Dark Matter splash against legacy's 85 for 42; no joint pierced in 18 corpses killed against a wall; the placed marine lying across a ledge edge sinks 1.0 unit with no joint pierced (7.9 units and two joints before). On Linux, on the build with exact math, the three repros that were repeated pass with Windows's numbers: the placed marine sinks 1.0 unit and stays on its ledge, joints are 33 units apart for 12 frames against legacy's 58 for 28, and the sloped stacks sleep 22 times and wake 11 against 1,768 and 1,757. Windows ragdoll CPU 15 % below legacy on the noise floor. Stills of ragdolls at play distance reviewed side by side. | CPU budget on Linux x64: `sp-ragdolls` costs 65 % (54 ms) more than legacy on the build with exact math. The three staged corpses take 534 moving steps to settle against legacy's 336 there, and 340 against 332 on Windows; why enhanced settles so differently on the two platforms when legacy no longer does is not found. The `mp-frags` bot round cost 24 % more than legacy on Linux before the math fix and was not repeated after it. The other four defect repros were not repeated on Linux after the fix. A windowed review in motion. |
| PHYS-04 — Closed (17 November 2026) | Impulse guard and telemetry. `physics_envelope.py`, both modes hit from one saved scene, passes momentum and all ten hit classes on Strogg marine corpses (rocket and grenade splash median 141 / 110 units, Dark Matter splash 211 / 183, largest 526 / 525) and, each mode from its own start and judged on the bodies the blast reached, on The Awakening's `m01_stranarus_trench2`; crates and barrels pass all nine classes on Air Defense 1's crater and on Hangar Perimeter's flat floor, and six flat-floor shoves; multiplayer player corpses pass ten of eleven classes at three repeats and the eleventh at six (a still-falling class decided by which mode draws a spawn point where the corpse tumbles down the map). `physics_coverage.py` passes all nine cases: Air Defense 1's six hanging lights, Putra's 21 and client debris. `physics_campaign.py`: the 31-stage stock campaign chain completes in both modes on the retail packs (62 of 62 loads, a save and a load on every map, no errors, 1,675 bodies, 11 pierced joints after settling in each mode) and six scripted set pieces match legacy. The three checks that failed on Linux on 16 November pass there on the build with exact math: the placed marine stays on its ledge (sinks 1.0 unit; legacy 0), the rocket splash on resting corpses reads 150 / 125 units at the median and the Dark Matter splash 222 / 293, and Putra's lights stretch 1.3 units against 0.8, as on Windows. | None open. Recorded limits: the debris sample is one piece; vehicles keep the legacy step by design and were exercised only by the chain; the multiplayer scene has to run on an otherwise idle machine and was last run on the third round's build; on Linux only the three failed checks have run since the math fix (PHYS-06). |
| PHYS-05 — Closed (16 November 2026) | `physics_integration.py` passes all seven checks on Windows x64 and Linux x64: saves load across modes both ways and in their own mode; clients follow a listen server's mode both ways; an MVD recorded on an enhanced server plays to its end in enhanced mode on a legacy client; a legacy client follows an enhanced dedicated server. A Linux x64 dedicated server and an AArch64 one under emulation run a seven-bot round in both modes without a renderer and without errors. The campaign chain saves and loads on all 31 stock maps in each mode. `presentation_look_runtime.py` at 240 Hz passes ragdoll, debris and death-camera interpolation in both modes (97-100 % of posed figure frames blended between steps in enhanced, 94-96 % in legacy, on the third round's build); both steps still run once per game tic. The first run found offline MVD playback failing in 0.14.1 (`spawn_entnum 4095 out of range`), fixed in `RegisterEntity`. | None. A physical 240/360 Hz display remains HF-07/HF-08's gate, not this item's. |
| PHYS-06 — Partial | Reference workloads repeated in both modes on Windows x64 (five runs, per-frame noise floor, third round's build) and Linux x64 (three runs, on the build with exact math), with the map's own ragdolls at rest, with CPU distributions, worst frames, step counts and failures recorded; the solver test passes on six x64 and AArch64 builds, and an AArch64 dedicated server ran the bot round in both modes under `qemu-aarch64` (third round). The envelope, defect, coverage, set-piece and campaign harnesses ran on Windows on current sources and the retail packs. The fourth round found and removed the cause of every Linux-only failure (approximate math in the Linux and macOS builds of idlib). Stills of combat, explosions and stacks were reviewed side by side. **Decision, 17 November 2026: `g_enhancedPhysics` stays `0`**, because the CPU budget is missed on two workloads per platform and no windowed review in motion has been made. `0` remains the supported fallback, an upgraded profile keeps its saved choice, and the [capability matrix](engine-capability-matrix.md), user guide and release notes record the decision. | Repeat the decision once PHYS-02 and PHYS-03 close, and accept or reject the proposed CPU exceptions. In-game, windowed visual checks of combat, explosions and set pieces in motion (every round so far reviewed stills). Repeat the full Linux pass on the build with exact math: only the rested CPU workloads and three targeted checks ran before the round was wrapped up. Re-measure the Windows CPU tables and the campaign walk's timings on current sources (the walk's timings of 17 November were taken on an oversubscribed machine and are not usable). macOS and Android after the math change. ARM64 beyond an emulated dedicated server. |

### PHYS task evidence and acceptance

- [x] **PHYS-01 — P2, investigation and decision: legacy baseline and enhanced
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
- [x] **PHYS-04 — P1 for promotion, qualification: behavior preservation and
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
- [x] **PHYS-05 — P1 for promotion, integration: saves, multiplayer, demos and
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

Reference comparison, 15 November: the multiplayer offering was compared with
Quake II Rerelease and Muff Mode, CPMA and Q4MAX
([record](competitive-match-reference-audit.md#reference-comparison-against-quake-ii-rerelease-muff-mode-cpma-and-q4max-15-november-2026)). MP-12 through MP-20 take the
gaps in features and play experience; MP-21 through MP-24 take the demo
format's standardization, backward compatibility, robustness and storage
efficiency. They add scope. They close nothing and reopen nothing that MP-01
through MP-11 recorded, and casual servers keep their current behaviour until
an option is turned on.

### MP completion checkpoints

**Estimate: 55%; 3/26 tasks closed.** The eleven original tasks stand where
they were (80% against their own scope); the 15 November comparison added
thirteen items that are mostly unstarted, MP-25 (splitscreen, added the
same day, with a staged plan since 21 November) and MP-26 (Horde, added the
same day) are not started. Most shipped systems work and have
substantial local evidence. MP-02's final long-match/catalog safety checks pass, with its timing
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
| MP-06 — Partial | Static-liquid compiler output, bot swimming, remote air/HUD/demo state, muffle and projectile crossings checked; player swimming, climbing out, drowning and the wade sound pass under `in_movementProbe` on OpenGL and Vulkan after the floor-slide and lava/slime haze fixes. | Real keys/controller, hearing the sounds and a by-eye pass at play distance ([checklist](mp-human-qualification.md#mp-06-swimming-and-liquid-effects-by-eye)); swimming as a remote client (prediction) is unexercised. Captured splash/bubble presentation passes after the slime/lava splash fix. Swimming monsters, new animations and moving volumes remain deferred. |
| MP-07 — Partial | Coupled pose-age/rewind, signed timing controller, impairment simulation and weapon geometry fixes tested; the firing player's own hit-scan shots are predicted on the fire frame and pass loopback, impaired and Vulkan captures; the weapon for a new life raises with the server's, so a burst held through a respawn stays on the server's frames, also when jitter drops the snapshot before or after the respawn; the server's aim stays with the client's after a client stall (one HUD artwork parse a level, HUD views built at spawn, a controller that recovers from hitches); long client frames no longer make commands late (a prediction margin from the client's frame times, whole bursts per packet); a snapshot read between tics is replayed before it is drawn and a resync starts from the target (14 November audit); defaults remain off. | By-eye pass with real input, teleporters and a moving gauntlet target ([checklist](mp-human-qualification.md#mp-07-weapon-visuals-under-prediction)); remote weapon play passes from spectator captures after the casual follow fix; real WAN and default promotion only when selected and qualified. |
| MP-08 — Partial | Local IPv4/IPv6 pure/discovery/reconnect/content checks, family fallback and fragment repair pass; Windows/Linux peer evidence exists. A 12-player match holds over a simulated 1280-byte path on IPv4 and IPv6, and over IPv6 even when the path drops IP fragments (connection replies fit one packet). The 14 November audit fixed the channel, server, client and replication defects that need no new network format, and protocol 2.42 (15 November) made the ones that did: keyed channel packets, a hit-confirm counter, 16-bit velocity and direction fields, one fragment size for both families (1,132 bytes) and a command-slack report; 2.41 demos still play. | Physical MTU/multi-record DNS, a real NAT64 or fragment-dropping IPv4 path, cross-machine load and the elevated retail-binary run ([checklist](mp-human-qualification.md#mp-08-real-network-conditions)); deferred WAN/other-architecture peers; the snapshot base still travels by reliable message. No interoperability claim. |
| MP-09 — Partial | Aim/turn/trigger fixes and five-level combat ladder measured over a round robin. | People verify skill 1 accessibility and skill 5 difficulty/fairness ([checklist](mp-human-qualification.md#mp-09-bot-difficulty-against-people)); retune from that evidence. |
| MP-10 — Partial | Buying offers Quake 4's or The Awakening's rules as one server option; the catalog, prices, awards, mods (networked and carried over a death) and the retained buy card pass contract, native and gate checks and OpenGL and Vulkan gameplay captures, including a remote client; CTF buying matches start again. | Play with real keys and a controller and judge the four mods in play by eye; UI register acceptance for FLOW-010 and a visual-spec record of the card. |
| MP-11 — Partial | Classic ruleset (`si_ruleset 1`): definition overlay, blaster start, tiered armour, Quake 2 health and mega health, Quake 2 weapon/ammo numbers, stair double jump, Threewave CTF scoring and the grappling hook, with Create Server and Controls rows; host, bot and predicted remote-client runs pass. Bots use the hook as a route shortcut; the hook rides movers and lets go on teleport. | Real-input play (hook feel, double jump on stairs, weapon balance on the stock maps), Classic pickup names. |
| MP-12 — Partial | `callvote`/`cv`/`vote` with eleven new vote words, `g_votePercent`, `g_voteLimit` and `si_spectatorVote`; casual team timeouts (`si_timeouts`, `timeout`/`timein`) and a console `pause`; `si_spectatorLock`, `g_inactivity` and `g_pauseOnDisconnect`; contract, electorate harness and a hidden listen-server smoke pass on OpenGL. 20 November: the in-match Vote page offers the casual votes (next map, random map, round limit, overtime, ruleset, Instagib, ready all, coin toss) and Create Server sets the timeouts, the spectator lock and the idle timeout. | Casual team locks, a console forfeit, Duel casual timeouts; Vulkan, dedicated (beyond the intermission), remote-client and real-input runs. The `mute`, `modifier`, `preset`, `arena` and `spectatorchat` votes and their Vote page rows landed 23 November, and the intermission's next-map announcement and awards reel (`g_intermissionAnnounce`, off by default) the same day. |
| MP-13 — Partial | One map database in the game module (`mapdb.txt` over discovered mapDefs): player bounds, modes, per-map settings applied at load and reverted after; the rotation, the player queue (`mymap`), the end-of-match pick (`mappick`), `si_nextMap`; votes, the Vote and Admin pages, the series pool, `PickMap` and the server card read it. Contract, core harness and a dedicated-server smoke with bots pass. | Create Server's catalogue and levelshot list (engine side, still its own mapDef filter) and its map-cycle field; a retained page for the queue and the pick (Create Server sets `g_mapRotation` and `g_mapQueue` since 20 November); a browser map filter; play by people; other platforms. |
| MP-14 — Partial | One modifier layer latched with the game type on every peer: Instagib, Quad Hog, Vampiric Damage, Weapons Frenzy, ten single-weapon arenas, a powerup switch, self-damage, knockback and fall-damage percentages and a weapon respawn time; eleven built-in presets and server-authored `presets/<name>.cfg` behind an allow list; Create Server's Modifiers tab, the match title, the loading card and the browser's rows and details card show them; contract and scripted bot and remote-client runs pass. | Match-rules fields so a managed match's digest records them (the `modifier`, `arena` and `preset` votes landed 23 November); Last Man Standing; a hog marker; Vulkan, dedicated-server and real-input passes for each modifier, and the percentages in play; the UI register's record of the Modifiers tab and its caption size. |
| MP-15 — Partial | Crosshair options, hit marker and damage numbers, brightskins/outlines/rimlights, forced models, lagometer, speed and input displays, four layouts, awards. Landed 15 November: chat tokens (`#h #a #w #l #n`) expanded on the sender, with place names taken from the map's own landmarks; hit sounds by damage (`hud_hitSound`, two tone sets made from Quake 4's own hit sounds, armour variant kept); a weapon preference order (`ui_weaponOrder`) for pickups and the empty-weapon fallback; ammunition beside the crosshair and a crosshair-name option; team info sent to team mates, so the Competitive team list is right across the map and names where each player is; a server item-timer policy (`si_itemTimers`: nobody, spectators, everyone) enforced where the list is sent, with the timer list and the time of day in all four layouts; `si_maxBrightskin` and `si_forcedModels`. The cores have a native test, the wiring is pinned, and a two-client smoke passes on OpenGL and Vulkan. Landed 17 November: smart weapon switching after WORR's (`ui_autoSwitch` 0 never, 1 always, 2 smart and the default, with a one-time move of profiles on the old default `1`; core `MPSmartSwitchAllows` in `mp/WeaponOrder.h`, natively tested), a three-way Auto Weapon Switch row in both interfaces, a Last Weapon Controls row (`_impulse51`, now live in single player too) and Quake Live-informed default keys (`G` last weapon, `Mouse4` grappling hook). | A person judging smart switching in a live match; A person judging hit tones and tokens in play; (menu rows landed 20 November: the HUD options on Player Setup's HUD tab and the in-match Settings page's HUD section, Weapon Order's dialog on both PLAYER pages, and Item Timers, Highlight Limit and Allow Forced Models on Create Server); UI-11 register rows and visual-spec text for crosshair ammunition and team locations (the timer list's, with symbols, places and progress, landed 23 November as `FLOW-074`); 4:3 captures; a team list outside the Competitive layout and in the stock interface; the empty-weapon fallback in a live match; team info and held-timer states in demo playback; cosmetic limits beyond the player's own game module; editor placement (UI-06). The frag line and match clock were already Quake 4's. |
| MP-16 — Partial | Follow by slot or name with a named list; live auto-follow in five modes (killer, action, leader, powerup, flag) with manual override; first-person, third-person and chase follow views and a death camera; a free-camera speed setting; a casual caster view (`g_casterAccess`, `caster on`) with both sides' vitals; a typed live event stream on servers and clients (`g_matchEventStream`) with a sample overlay; a notice when spectator chat is kept from players. Casual spectators are neutral, a broadcaster who joins the game loses the role, and follow requests are no longer lost between frames. Three contracts and a two-client live smoke pass on OpenGL and Vulkan. Landed 23 November: Competitive's scoreline for Duel, Tourney and the team modes (large scores beside the timer and each side's roster of up to four players with weapon, health and armour where the viewer may see them, both sides for a spectator, and a skull for the dead; deathmatch keeps the standing and timer top-leading), and item timers with symbols, places and respawn progress over a spectator's trailing list (visual spec 1.47, register revision 27: `FLOW-073` supersedes `FLOW-058`, `FLOW-074`, both pending); native checks, the client-options smoke's respawn-length check and Duel and Team DM spectator captures at 16:9 and 4:3. Revised the same day on review: tiles name players by initials with a weapon-colored ammunition bar (team info now carries ammunition, also to spectators on casual servers), the side team lists are gone, and kill feed lines slide in and glide up (visual spec 1.48, register revision 28: `FLOW-075`, `FLOW-076`); team mates' places list under their tiles (visual spec 1.49, register revision 29: `FLOW-077`). | Human judgement of the scoreline while casting, captures at 21:9 and 9:16, and the rosters in MVD playback; the named list as a menu page (UI-11; Spectator Speed and Follow View are menu rows since 20 November); the three new camera modes in MVD playback; play by people with real input; other platforms; multi-POV and delay stay OPT-04. |
| MP-17 — Partial | Decision (user, 23 November): an identity on by default, one Ed25519 key per server derived from a seed kept on the player's computer. A one-time challenge bound to the server's id, the slot and the connection; `status` shows it; identity bans beside address bans (`ban id:`), applied on proof; a casual leaver's team and score kept for `g_reconnectRestore` seconds and given back by identity or by `ghost <code>`; a local ranking table (`g_ranking`, Elo K 48 then 32); the player's own history (`history`); `identityReset`; Player Setup's Player Identity. RFC 8032 vectors, a native core test, a contract and a dedicated-server smoke pass on Windows. | The awards reel and a `stats` command with accuracy; casual exports in the managed schema-v2 JSON with a summary and a catalogue; Duel queue places and round inventories on return; Tourney ratings; a reviewer's pass over the key protocol; Steam identity (STEAM-02); other platforms; nothing uploaded. |
| MP-18 — Partial | rcon2, the Admin page's kick/ban/switch, PBKDF2 referee, voice mute, `si_motd`. 22 November: bans by address and range with an expiry and a reason (`ban`, `unban`, `banlist`, a text `banlist.txt` that takes over Quake 4's binary one; the Admin page's Ban now bans), a text mute that follows the address (`mute`, `unmute`), the chat flood window (`g_chatFlood*`), the name policy (`g_reservedNames`), `net_serverMaxClientsPerAddress`, `status` with addresses and the NDJSON admin log; then the `mute` vote (`callvote mute`, `g_voteMuteMinutes`), graceful shutdown (`serverShutdown match|now|time|cancel`) and the engine's own `kick` in the log; then the match log (`g_matchLog`, the live event stream's events appended to `logs/match.ndjson`) and an atomic write of `banlist.txt`; then the admin role (`net_serverAdminPassword`, `admin`: the referee's challenge and response with a second password, then kick, mute and capped bans of connected players from the admin's own console, `g_adminMaxBanMinutes`); then an audit that fixed what two independent reviews found (commands read their line as typed, an unreadable ban list is never written over, IPv6 `/64` for bans, mutes and the limit, engine-accurate name rules, the host's `admin list|revoke|off`). The core and the ban file reader have native tests, the commands and call sites are pinned, and a dedicated-server smoke passes on Windows over loopback ([record](mp-moderation.md)). | A menu for admins who log in with `admin`; Linux, macOS, IPv6 and a real remote address. A ban's length and reason on the Admin page and the Vote page's mute row landed 23 November. |
| MP-19 — Partial | Browser with favourites, filters, find player and tiles; LAN scan. 23 November: an openQ4 master program (`tools/master/openq4_master.py`: challenge-verified heartbeats, the legacy and the extended list, a JSON list for HTTPS) and the browser's HTTPS fallback (`net_serverListUrl`, merged with the master's list); the user chose to keep id's master as the default and the list address empty until the project's hosts are chosen. | A discovery service the project controls with a list fallback, recent servers, region sorting; a fresh install on each platform finds a server. |
| MP-20 — Not started | Stock Quake 4 physics; Classic double jump and hook; `pm_jumpheight`, `pm_slidevelocity` and `pm_powerslide` synced. | Retail 1.4.2 movement parity evidence on the stock maps at 30, 60 and 90 tics; an optional ProMode-style movement ruleset the bots use. |
| MP-21 — Partial | Render v14 block container with keyframes, 64-bit files and `compressDemo`; MVD 1.3 records with CRCs and readers for 1.0 to 1.2; the update policy. | One container for every stream, `demoConvert`, the format registry, per-version golden fixtures, byte-identical cross-platform reads in CI. |
| MP-22 — Partial | Server MVDs with markers, keyframes and auto-tracking; render demos record the local view; a deck with pause, speed, step, seek and follow. 23 November: a player records their own view on any server (`record`, `stopRecord`) as a client-view MVD and plays it back as that player; `demoExport` writes PNG pictures and a WAV sound track; retail Quake 4 render `.demo` files and Quake 4 1.4.2 `.ndmo85` network demos play. | Client-side recording on any server, autoaction, named list, reverse scrub, `timeDemo` for MVD, video and image export, thumbnails and metadata; the retail-demo decision. |
| MP-23 — Partial | The render container packs its stream 4.78:1; joints are predicted (v12). | MVD compression and field encodings, 2D/sound/effect encodings, `demoStats`, budgets and targets met byte-exact. |
| MP-24 — Partial | Native codec flips, truncations and hostile payloads; MVD cut points and recovery; damaged render files play on. | One fuzz corpus for every reader; MVD worker-thread, 64-bit and hours-long recordings; cheats-off, under-load, Vulkan/Linux/macOS/ARM64 recording and playback; cross-platform determinism; a listening check. |
| MP-25 — Not started; staged plan 21 November | Survey: the server already runs networkless local clients (bots through `InitLocalClient`), and `Draw( clientNum )` and `renderView_t`'s viewport rectangle exist. One human local client, one command generator, one open gamepad, one listener and one active GUI; `src/mpgame/` asks who is local on about 730 lines; whole-target post passes; temporal history slotted only for VR eyes. | Decisions D1-D6 (remote transport, seat identity, stock-interface policy, listener model, keyboard seat, automated seats); then the stages: seats and input, listen-server seats, split views, HUD and menus, audio, remote servers, spectating and demos, cost and qualification. |
| MP-26 — Not started | The multiplayer module compiles Quake 4's AI, AAS and monster classes; `idAI` and `idActor` write no snapshot state and no mode spawns monsters. | Monsters networked within MP-08's budget; a wave director with scaling, champions, themes and boss waves; spawn anchors and the map decision; crowd targeting and pursuit; lives, rallies and rewards; HUD, Create Server and localized text; demo playback; play by people together. |

Completion substeps:

- **Safety and skill:** retain MP-02's final safety evidence and accepted timing
  caveat; qualify human combat under MP-09.
- **Player workflows:** exercise real match/Arena/liquid controls and visual/audio
  outcomes, including interruption and slot reuse, rather than only command paths.
- **Peer breadth:** retain current local results; reopen deferred WAN/architecture
  qualification only within selected scope and preserve explicit unsupported cases.
- **Reference parity:** land the casual match flow, map pools, modifiers, HUD
  options, live spectating, identity, moderation and discovery items in their
  dependency order; each keeps casual defaults unchanged until enabled.
- **Demo format:** one container and its registry first, then client recording
  and compression, with the robustness fixtures carried through every change.

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
  Progress, 12 November: [swimming under player movement and the wade sound](liquid-qualification.md#swimming-wading-and-drowning-under-player-movement-12-november-2026)
  are qualified on OpenGL and Vulkan without input. The cheat-gated
  `in_movementProbe` holds movement buttons inside the usercmd generator, and
  `mp_liquid_swim_probe.py` takes a listen host into all five liquid-lab pools:
  it dives, moves in all four directions on the floor, surfaces, climbs out at
  the far edge, drowns and recovers its air in the deep pool, and wades with
  five logged `player_sounds_water_wade` footsteps. The run found two defects,
  both fixed. A swimmer resting on a pool floor with crouch held slid at about
  40 units a second, because the inherited underwater slope rule gave the
  downward push to any leftover drift; it now keeps speed only for wishes that
  run along the floor (60 units of drift in 1.5 s becomes 0 to 2.4). Under lava
  the underwater haze was teal and under slime grey-purple, because in-scattering
  put back the absorbed colours; both renderers now put back the liquid's own
  colour. A six-bot liquid-lab audit has no world deaths and no stuck reports.
  Remaining: the [human checklist](mp-human-qualification.md#mp-06-swimming-and-liquid-effects-by-eye)
  (real keys and controller, hearing the sounds, a by-eye pass at play distance)
  and swimming as a remote client: the harness cannot place one at a pool without
  input, so client prediction while swimming is unexercised.
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
  Progress, 12 November: [the firing player's own shots](mp-network-qualification.md#the-firing-players-own-shots-under-prediction-12-november-2026)
  are drawn on the predicted frame that fires them (`net_mpPredictHitscan`,
  default on). Before, the client drew its machinegun, shotgun and railgun
  shots from the server's copy, a round trip late and from where the server had
  the gun. Over loopback at a 220-degree-a-second turn that was 12 degrees off
  the aim (median, machinegun sweep), and 54 degrees with 80 ms ±20 latency.
  `rvWeapon::Attack` now runs `rvWeapon::Hitscan` on the newest predicted frame
  with no damage. `idGameLocal::HitScan` takes an effects mode, so the server's
  copy adds only impacts on actors. A one-second note ring pairs copies with
  predictions, and multiplayer spread is seeded by shooter and game time.
  `mp_firing_view_capture.py` passes on OpenGL loopback, impaired and Vulkan,
  with the old behaviour as baseline. Every server shot outside the respawn
  burst was predicted on its own frame, with the same seed and direction. No
  copy was drawn twice, and every prediction starts at the drawn gun. After a
  respawn nothing is drawn from the old position. The respawn burst ran a
  frame or more out of phase with the server, because the client's new weapon
  was caught up straight to ready while the server's raised (fixed 13
  November, below). `mp_predicted_hitscan_state.py` runs the note ring
  and seed natively with 14 failing controls. Remaining: teleporters, a moving
  gauntlet target and the by-eye pass with a real mouse, WAN peers, and the
  defaults decision for enhanced prediction and lag compensation.
  Progress, 13 November: [the weapon for a new life](mp-network-qualification.md#the-weapon-for-a-new-life-raises-with-the-servers-13-november-2026)
  raises with the server's. The client's respawn branch and its switch to
  spectating reset the snapshot sequence in `Init()`. The next snapshot read
  as a state hitch and caught the new weapon up to ready, which `UpdateWeapon`
  has honoured since the 28 October seek fix. Both now keep the sequence.
  A respawn, or new weapon entities, catch up only across a genuine hitch.
  Otherwise the new weapon raises from the server's raise frame. That frame is
  read from whether the snapshot carries the server's weapon and from the
  spawn's knockback clock. On loopback and at 80 ms ±20, every server shot in
  the respawn and rejoin bursts was predicted on its own frame. Each new
  life's raise matched the server's frame for frame, and nothing was predicted
  after a respawn that the server did not fire.
  A native fixture spliced from production adds 19 failing controls to
  `mp_predicted_hitscan_state.py`. `mp_firing_view_capture.py` gained a
  rejoin step. It holds the respawn and rejoin bursts to same-frame matching,
  and it fails a new life's raise that is caught up or off the server's frame.
  Later on 13 November: [the server's aim after a client stall](mp-network-qualification.md#the-servers-aim-after-a-client-stall-13-november-2026)
  no longer drifts. Two rejoin bursts under 80 ms ±20 had drifted because the
  client's frame stalled about 200 ms at its first hit on a bot. Every retained
  HUD, the first damage number's included, parsed the 8.4 MB HUD artwork again
  on its first draw. A client sends no commands while its frame stalls, so the
  server repeated its last one: its aim stood still, and after 25 repeats it
  dropped the attack. The artwork now parses once a level, on a worker. The
  local player builds its crosshair, hit marker and damage number views as it
  spawns. Deliberate stalls then exposed a second defect. Each hitch over
  100 ms under latency left the prediction controller's catch-up higher until
  its clamp held it. From then on every command reached the server late, and
  the server never fired again. The frame residual now joins the catch-up.
  `mp_firing_view_capture.py` holds the rejoin's directions under latency too,
  adds a hitch step before the respawn and fails a HUD view built mid-burst.
  `mp_client_prediction_state.py` adds a hitch loop and two failing controls,
  and `ui_hud_catalog_state.py` executes the catalog and the spawn precache
  with 20 failing controls. Every rejoin burst in four impaired runs and on
  loopback fell on the server's frames with its directions. Open: a respawn
  snapshot that follows a snapshot jitter dropped still catches the new
  weapon up, and a long client frame can still make a command late (both
  fixed later the same day, below), so the impaired harness did not pass
  dependably then.
  Later on 13 November: [commands stay on time through long client frames](mp-network-qualification.md#commands-on-time-through-long-client-frames-13-november-2026).
  The controller steps its target to the median of the server's
  `aheadOfServer` report, which covers a client frame of about two tics. A
  longer frame held its commands back until the server had run their frames,
  and a client below 30 fps did that on every frame. Measured in the fixture
  over 80 ms ±20, lateness starts at frames about 20 ms longer than a tic and
  reaches 0.7 late commands per long frame at 56 ms and 3.0 at 96 ms.
  Decision: a client-side margin from the client's own frame times
  (`src/framework/async/PredictionMargin.h`, `net_clientPredictionMargin`,
  default 1). The target gains the longest recent frame less the two tics the
  median covers. Each long frame holds its own length for 10 s and then decays
  at 4 ms a second, so one spike at a join does not hold a large margin while
  milder long frames continue. A frame cut by the 100 ms `UpdateTime` clamp is
  a hitch and is left out. The cost is extra server-side wait equal to the
  margin while it lasts, for example 24 ms after 56 ms frames and 44 ms after
  76 ms frames. Smooth clients pay nothing. Tracking the server's worst
  `aheadOfServer` dips was rejected. Those samples sit on a 16 ms lattice
  33 ms apart, so a floor that caught tails also added 13-16 ms to every clean
  connection in the model, and it needed windup protection. A second defect
  lost commands outright: a long frame's burst of one-command packets
  reorders on a jittery link, and the channel discards the older ones. Each
  packet of a burst now carries the whole burst. The server counts the frames
  it runs on a repeated command (`com_showAsyncStats`), and
  `mp_firing_view_capture.py` reports them (`late_commands`), the shooter's
  frame-time tail and the margin's rises. `mp_client_prediction_state.py` adds
  long-frame, slow-client, spike, random-tail and cost loops, a measurement
  table and six failing controls (12 in all). Six interleaved impaired runs,
  four with the margin and two without, all passed with every server shot on
  the client's frame and direction. Long frames left 7 late frames over 253
  frames of 36 ms or more with the margin. Without it they left 42 over 303,
  counting two margin-off runs on intermediate builds. The ones left are each
  first long frame of a new length and stalls past the clamp.
  Later on 13 November: [a respawn read across a dropped snapshot](mp-network-qualification.md#a-respawn-read-across-a-dropped-snapshot-13-november-2026)
  raises with the server's. Under 80 ms ±20 the shooter's channel loses a
  snapshot whenever one client frame reads two at once and the jitter swaps
  them: 6-16 single losses a minute on a quiet machine (0.3-0.9% of
  snapshots), nearly one in ten on a busy one. Across such a loss the
  spawn's knockback now places the server's raise, when the last snapshot
  read is at most 96 ms old and so the knockback must be the spawn's; a
  longer loss still catches up. The snapshot after a respawn names the
  weapon `Init()` cleared, and read across a loss that no longer counts as a
  missed weapon swap. `net_clientSnapshotDropProbe` (development) discards
  every Nth snapshot, and `mp_firing_view_capture.py --drop-respawn-snapshot`
  uses it to put every respawn behind one; every run now counts the
  channel's losses. In four probe runs and two natural runs at 80 ms ±20,
  five on a quiet machine kept every respawn and rejoin shot on the server's
  frames with its directions, each raise exact; the control with the old rule
  caught the weapon up (9 of 34 respawn shots on the frame). The sixth run,
  on a busy machine, raised exactly but lost shots to stale commands, the long
  client frame problem fixed in the same round (above). `mp_predicted_hitscan_state.py`
  fails 42 single-reversal controls (nine new).
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
  not a version-number change to openQ4 2.42 or a prerequisite for retail assets.
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
  Progress, 13 November: checklist row 8.1's match runs on one machine over a
  simulated 1280-byte path (`net_forceMTU`, `mp_path_mtu_soak.py`). With the
  path carrying IP fragments, a 12-player q4ctf1 CTF match on a pure dedicated
  server holds for five minutes on IPv4 and IPv6 with no overflow, drop or
  disconnect. Inside the match IPv6 stays within 1,232 bytes, but over a path
  that drops IP fragments neither family can join: the `infoResponse` asked
  for before the challenge and the `connectResponse` carry the whole
  server-info dictionary, about 1.8 KB, outside the fragmenting channel.
  A message that packing grew past a fragment now leaves in fragments instead
  of one oversized datagram
  ([evidence](mp-network-qualification.md#a-1280-byte-path-mtu-13-november-2026)).
  Decision and follow-through, same day: the user chose to shrink the replies.
  The info reply keeps the browser's keys and players and fills the rest to
  1,232 bytes; a client that names the new connect capability gets a connect
  reply with only the loading keys and a trimmed roster, and the full server
  info follows in the channel's game-init message, sent again if lost. Older
  builds on either end get the old replies. Over a fragment-dropping 1280 path
  the IPv6 match now joins and holds five minutes with nothing over 1,232
  bytes; IPv4 still cannot, because its channel fragments are 1,300 bytes
  ([evidence](mp-network-qualification.md#connection-replies-that-fit-one-packet-13-november-2026)).
  The physical-MTU row stays open.
  Progress, 14 November: [netcode audit](mp-network-qualification.md#netcode-audit-14-november-2026).
  A read of the channel, server, client, replication and prediction code,
  each finding re-read against the source before it was changed. Fixed with
  no change to the network format: a snapshot read between tics was drawn
  without being replayed; a stall's queued snapshots wound the catch-up up;
  unreadable server data left the map running with no connection; a client in
  the game could be begun twice; user info and reliable game messages had no
  budget; snapshots were paced on a millisecond boundary and fragments left one
  a frame; a full reliable queue wedged the channel; entity ids broke after
  524,288 spawns on one map load; broadcast sounds were aged against the
  predicted clock; one missed snapshot hid a death or a hit. The three MP-07
  branches of 13 November are merged with it. `network_channel_backlog_state.py`
  executes the channel with six failing controls, and `network_security.py`
  pins the rest. Measured on loopback: interpolated frames 50.2 to 60.0 a
  second on a 60 fps client behind 40 ms, snapshots from a 24-30 mix to 30 a
  second; the reconnect smoke, the 12-player fragmenting-path soak and the
  impaired firing-view capture pass.
  Progress, 15 November: the user chose one new network format for them, and
  [protocol 2.42](mp-network-qualification.md#network-protocol-242-15-november-2026)
  makes them: a keyed tag in every channel packet, a hit-confirm counter in
  place of the one-snapshot bit, 16-bit velocity for other players with the
  exact value for the owner, a 48-bit projectile launch direction, one-byte
  snapshot tags, a command-slack report in the snapshot header (logged, not
  steered by yet) and one fragment size for both address families. Older
  openQ4 builds are refused like retail; 2.41 multiview demos still play.
  Still open as decisions: predicting jump pads for the local player, a
  per-address connection limit, a private salt for the hit-scan spread, and
  carrying the snapshot base in the header.
  Progress, 17 November: [multiplayer audit](mp-audit-2026-11-17.md#engine-network-layer).
  Six fixes with no change to the network format: the 2.42 packet tag is
  written unsigned (every tag with its top bit set logged a value-overflow
  warning); an out-of-band bad-challenge notice or denial is obeyed only
  during the handshake that draws it (one forged packet, or a late duplicate
  `connect`, used to end an in-game session); `auth` and runt packets print
  nothing outside developer mode; an rcon lockout is carried across a map
  change instead of lasting the new map's length; a master lookup that failed
  is tried again at each forced heartbeat instead of being reported as a
  success; an oversize datagram no longer ends the frame's packet drain.
  The network contracts pass with the changed lines. Left open: the global
  challenge budget and IPv6 per-source keying, rcon limiter eviction, a
  staged join burst, dropping a client that sends malformed packets, and
  names containing byte 0xFF.
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): all six closed.
  By the owner's decision a `connect` that proves its challenge is no longer
  charged to the reply budget, and rate limits key an IPv6 source on its /64.
  An rcon entry holding failures is never evicted (a new source is refused
  while every entry holds some), and its windows survive a map change. A join
  sends the other clients' user info as the joiner's reliable queue frees
  room. Eight unreadable packets in ten seconds drop a client, silently
  unless `net_channelShowDrop` is set. Byte 255 is sent as `.`. Covered by
  `network_security.py` and the channel fixtures; not run against a hostile
  client or a full server.
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
- [ ] **MP-10 — P2, implementation and qualification: buying rules and the
  buy card.** A buying server sells by Quake 4's rules or The Awakening's as
  one option (Create Server, vote, admin), and the buy menu presents in the new
  interface's in-match card construction ([reference](mp-buying.md)). Gates:
  `mp_buying_rules_contract.py`, `openq4-ui-retained-buy`, the retained gate's
  buy-card case and `capture_buy_menu.py` on OpenGL and Vulkan, with a remote
  client building the modded weapon from the snapshot; then real keyboard and
  controller purchases and the mods judged in play.
  Progress, 13 November: the expansion's buy menu was a prototype (prices in
  its labels, two mods with no def, items that need its content). Its catalog
  now lives in `buyCatalog` with retail's items priced by the content, its
  prices and cash awards in code, four mods the base content defines, the Dark
  Matter Gun and FC Armor Regen. Mods travel in the player snapshot, survive a
  buying mode's death and never give a clipless weapon a reload. The match
  rules' buying mask now covers the four CTF modes, which had kept a buying
  CTF match in warmup. Remaining: hands-on play and the UI register's FLOW-010
  acceptance with a visual-spec record of the card's detail-column reasons.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#gameplay)): a buy
  from a bind or the buy card is no longer lost on a remote client (it went
  through the event path that drops outside a new frame); the Chain Lightning
  Mod chains in multiplayer, to opponents in play and never to its owner or
  team mates; FC Armor Regen no longer ends with a burst of lost armour; the
  Ammo Regen rune leaves a weapon arena's unlimited ammunition alone. None of
  these was run in play. Still open: the Bounce Shot and Burst Fire
  projectiles are single-player definitions with no physics sync, so a remote
  client draws them flying straight; and Classic armour bought at 100 points
  costs credits for nothing.
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): both closed.
  `projectile_hyperblaster_bounce1_mp` and `projectile_rocket_homing_mp` send
  their physics with the weapons' multiplayer tuning, and a Classic armour
  purchase that would add nothing is refused before it is charged (bots value
  Classic armour by what it adds, MP-11). A remote-client capture of each mod
  is still owed.
- [ ] **MP-11 — P2, implementation and qualification: Classic multiplayer ruleset.**
  A Quake 2 inspired ruleset over the stock maps and weapons
  ([player guide](../user/classic-ruleset.md)), selected by `si_ruleset` and
  latched at map load on server and clients. Weapon, ammo and item numbers are
  an overlay on the shipped entity definitions (`src/mpgame/mp/ClassicRules.cpp`),
  applied before anything spawns and reverted when the map unloads, so no
  replacement content ships and the stock ruleset is untouched. The armour
  tier, hook state and double-jump window ride the snapshot only on Classic
  maps; the hook's pull runs in the player physics so the owner predicts it.
  Gates: a person plays Classic DM and CTF with real input, judges the hook,
  the stair double jump and the weapon balance on the stock maps, and the
  findings are folded back into the overlay.
  Progress, 13 November: implemented and checked on Windows/Vulkan with a
  listen server, bots and a connected client: overlay applied on both ends,
  blaster spawns, Quake 2 armour conversion, mega health drain, hook pull on
  the host and a predicted remote client, Create Server Rules tab. Remaining:
  real-input play, bot hook use, Classic pickup names and an armour-tier HUD tint.
  Progress, 14 November: the multiplayer armour icon takes the tier's colour
  (green jacket, yellow combat, red body) on the Remastered and stock HUDs;
  `guis/hud.gui` keeps its earlier layout as save revision `hud_r4`, and The
  Awakening's generated HUD leaves the tint out. Remaining: real-input play,
  bot hook use and Classic pickup names.
  Progress, 14 November (hook): bots take hook shortcuts (`rvBot::UpdateHook`,
  `hookUse` trait, `bot_grapple`): a corner further along the route in sight,
  well short of the route in a straight line or a climb, with a world bite over
  it that the body can follow; two or three pulls a minute with four skill-5 bots
  on q4dm1, landing over the corner. The hook anchors to movers, lets go on
  teleport, flies by a function of the throw so every end draws it smoothly,
  and has stock throw/bite/reel/release sounds. The overlay now rebuilds the
  engine's entityDef dictionaries through the engine's own `FreeData`/`Parse`;
  editing them with game-module `idDict` code had corrupted the engine's string
  pool, and the client crashed on quit after every Classic map.
  `mp_classic_grapple_contract.py` pins all of it. Remaining: real-input play
  and Classic pickup names.
  Progress, 14 November (bot hook): one bite search for any landing (a ceiling
  over it, points above it, points just past it); fall rescues back over the
  last floor or a nearby corner when the navmesh proves a fall into nothing or
  128+ units down unsafe; a stalled route tries the hook on its nearest corners;
  carriers and withdrawing bots may turn from a fight to throw; a pull that
  arrived allows the next after 0.3 s; `hookUse` scaled by style; `bot_debugNav 2`
  draws the plan; `mp_bot_audit.py` counts hook events and `--min-hook-pulls`
  gates them. Six skill-4 bots, 120 s: q4dm1 (all modes) 8 and 6 pulls, no misses,
  no world deaths; q4ctf1 26 pulls (2 rescues, 1 unstick). q4ctf5's open
  walkways give the hook nothing to hold, so it planned two rescues and made
  no pulls there.
  Progress, 23 November (ruleset audit): every overlay row checked against the
  stock definitions and Quake 2's and Threewave CTF's values. Fixed: zoomed
  seeking nails kept a 25-point splash; the expansion napalm launcher
  respawned every 5 s; slower rockets died at 3,900 units (fuse 12); quad was
  Quake 4's 3x, now Quake 2's 4x; tiered armour ignored Scout; Regeneration's
  overheal never drained once Classic disabled the stock decay; flag defence
  followed the moving flag pointer instead of the base flag; the capturer got
  no assists; a capture left the "hurt the carrier" marks standing.
  `mp_classic_grapple_contract.py` pins them. Live: a Classic CTF bot match on
  q4ctf1 applied all 156 values with none missing and played without an MP
  module fault, but only in warmup, so the CTF awards were not exercised live.
  Kept as deliberate deviations: Quake 4's linear splash falloff (Quake 2's
  stays stronger further out), flag defence needs sight and 400 units (Threewave
  took either), and the zoomed machinegun keeps 10 damage at 5 shots a second.
  Remaining: real-input play, a CTF capture on a live server, and Classic
  pickup names.

- [ ] **MP-12 — P1, implementation: casual match flow and voting parity.** Bring
  to every server what exists today only inside a managed match, or not at all,
  measured against Muff Mode, CPMA and Q4MAX
  ([comparison](competitive-match-reference-audit.md#reference-comparison-against-quake-ii-rerelease-muff-mode-cpma-and-q4max-15-november-2026)).
  Team timeouts and a technical pause on casual Duel, Team DM, CTF and round
  servers through the managed `MatchTime()` primitive, with a per-side budget
  and a resume countdown; captain and operator team locks; a spectator lock
  (`g_spectatorLock`); pause on a player's disconnect (`g_pauseOnDisconnect`);
  an inactivity policy that warns, moves to spectators and then drops, and
  never fires in a paused match; an intermission that shows the final
  scoreboard, an awards reel and the next map before the cycle advances; and a
  forfeit from the console. One vote surface with the names Quake players type
  (`callvote`/`cv`): `map`, `nextmap`, `restart`, `gametype`, `ruleset`,
  `modifier`, `preset`, `timelimit`, `scorelimit`, `roundlimit`, `overtime`,
  `shuffle`, `balance`, `cointoss`, `random`, `kick`, `mute`, `readyall` and
  `timeout`, each a typed operation through the managed pipeline's validation,
  with a pass percentage, a per-map vote limit, a spectator-vote policy and the
  existing cooldown, each disableable from `si_voteFlags`. Casual behaviour is
  unchanged until a server turns an option on. Gates: asset-free contracts for
  every new operation and vote, a live smoke with three humans on OpenGL and
  Vulkan (timeout, pause on disconnect, inactivity, intermission, each vote),
  the Create Server and Match Control rows localized in every language, and the
  [competitive matches guide](../user/competitive-matches.md) and
  [server setup guide](../user/server-setup.md) updated.
  Progress, 15 November: the first implementation round. Votes: `callvote`,
  `cv` and `vote yes|no` reach the inherited packed vote, which gains
  `nextmap`, `shuffle`, `balance`, `scorelimit` (the limit the mode scores
  by), `roundlimit`, `ruleset q4|classic`, `instagib`, `overtime`,
  `cointoss`, `random` (a map that supports the mode, drawn when the vote
  passes) and `readyall` (only in a warmup that waits for ready players);
  the flag word is a long on all three vote messages. `g_votePercent` (51,
  the inherited majority) decides the pass, and a vote fails as soon as the
  undecided could not carry it; `g_voteLimit` caps a connection's calls per
  map; `si_spectatorVote` admits spectators, never bots. Timeouts: the
  rules core no longer confines a timeout budget to managed matches, the
  casual import takes `si_timeouts`/`si_timeoutLength` in team modes, an
  active human on a side may call and end its own timeout through the
  typed operation path (`timeout`, `timein`), the host, a referee or the
  dedicated console may `pause`, a casual server announces the pause and
  the resumption in the centre of the screen, and clients now move their
  HUD clock origin with a frozen frame, which managed pauses needed too.
  Policies, all off by default: `si_spectatorLock` (1 keeps spectators out
  of live play, 2 keeps players in), `g_inactivity` (warn at half time, then
  spectators, or a drop when the server has none; bots and paused play
  never count) and `g_pauseOnDisconnect` (a technical pause with an expiry
  when a playing human leaves a live team match; the abort check runs when
  it ends). `mp_casual_match_flow_contract.py` pins all of it, the vote
  security and electorate contracts and the player-targets fixture follow
  the wider flag word, and `mp_casual_flow_smoke.py` plays it on a hidden
  listen server: on 15 November (OpenGL, q4dm1, two paused bots) it passed all eleven checkpoints: a coin-toss vote, a team timeout that expired into the resume countdown and back to play, a second ended early by `timein`, a refused third with the server reason printed at the console, a `roundlimit 5` vote once the cooldown (game time, which the pauses hold) had passed, `si_spectatorLock 2` holding the host in play, and `g_inactivity` moving it to spectators, which ended the match as abandoned with no human left. Deferred to the next round: casual team locks
  (locks are lobby-only and casual servers have no captains), the console
  forfeit (it needs a side-consent transport), the intermission awards reel
  and next-map announcement, the `mute` vote's row on the Vote page (the vote
  itself landed with MP-18 on 22 November) and the
  `modifier`/`preset`/`arena` votes (MP-14's layer) and MP-16's
  `spectatorchat` vote (`si_voteFlags` bits from 20 are reserved for them), Duel
  casual timeouts, and Vulkan, dedicated, remote-client and real-input runs.
  Progress, 23 November: the intermission. With `g_intermissionAnnounce 1` (off
  by default) a casual server's final scoreboard names the next map in chat,
  the map the rotation will load (`ServerPredictNext`), and the change does
  not name it again; a pick, when offered, names its own maps instead. The
  awards reel then reads out the stat manager's end-of-match awards, one
  award a player a round, one line every two seconds, each localized by the
  client; managed matches and the Arena campaign are left alone.
  `mp_casual_match_flow_contract.py` and `mp_map_database_contract.py` pin it,
  and `mp_map_rotation_smoke.py` plays it on a pure dedicated server with two
  bots and a hidden client: quiet until turned on, the next map named once
  during the scoreboard and loaded, each award read out reaching the client.
  The scoreboard itself stays Quake 4's; a reel on the scoreboard GUI is open.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#match-flow-votes-and-pauses)):
  nine defects fixed. The idle drop no longer deletes a player inside its own
  think; a casual server keeps a limit changed by a vote, at the console or
  by hosting again (the rules import laid the first values back at every map
  start), and a joining client's own settings are left alone; a player who is
  eliminated or on the scoreboard can vote; `si_spectatorLock` holds someone
  who connects with `ui_autoJoin`; `g_pauseOnDisconnect` is one pause a side
  a match, ends when the player is back, lets them back in under the lock and
  ignores kicks and idle drops; the stock vote menu obeys `g_voteLimit` and
  the next-map switch. The idle drop, the kept limit and the client's
  settings were [run](mp-audit-2026-11-17.md#checked); the electorate, the
  lock and the disconnect pause are covered by their contracts only.
  Progress, 23 November: the votes MP-14 and MP-16 waited for. `callvote modifier <name> 0|1` (or the modifier's own name: `quadhog 1`), `arena <weapon|none>`, `preset <name>` and `spectatorchat 0|1` are bits 21 to 24 (`VOTEFLAG_MODIFIER`, `_SPECTATORCHAT`, `_PRESET`, `_ARENA`, `NUM_VOTES` 25), each with its payload after the MP-12 fields only when its flag is set, so a vote without them is laid out as before. The server checks each through the layer's own rules (Quad Hog only where the ballot leaves Deathmatch; a preset the server has whose game type a map plays, alone in its ballot) and refuses one that changes nothing; a passed modifier or arena restarts the match through `CheckModifierChange`, a preset as the `preset` command does. `g_spectatorChat` is in the server info so every Vote page knows its value. The retained Vote page offers Quad Hog (in Deathmatch), Vampiric Damage, Weapons Frenzy, a weapon arena, a built-in preset, spectator chat and **Mute Instead of Kicking** for the player chosen (the mute vote's row). `mp_vote_extensions_smoke.py` passes on a hidden OpenGL listen server: three refused calls start no vote, then Quad Hog, a rocket arena, spectator chat and the standard preset each pass and apply.
- [ ] **MP-13 — P2, implementation: map pools, rotation and the player queue.**
  Replace the `si_mapCycle` string and `mapcycle.scriptcfg` with one structured
  map database (Muff Mode `mapdb.json`, CPMA `map_cfgdir`): for each stock or
  installed map, the modes it supports, its player-count bounds and an optional
  per-map configuration applied at load and reverted at unload; maps discovered
  rather than listed. A rotation that respects the bounds and the current
  player count; a player queue (`mymap`, one entry a player, consumed at the
  next change); an end-of-match pick from three candidates or a next-map vote;
  and the series pool, the Create Server catalogue, the Vote and Admin pages,
  the browser filters and the bots' map checks read the same database. Gates:
  a contract that every menu, filter and vote enumerates the database and
  nothing else; a dedicated-server rotation smoke over bounds, queue and pick
  with bots; the [server setup guide](../user/server-setup.md) updated.
  Progress, 15 November: the database and everything in the game module that
  lists maps. `mpMapDatabase` (`src/mpgame/mp/MapDatabase.*`) is a bounded core
  with no engine dependency: entries discovered from mapDefs (none listed),
  gametype masks, player bounds (a mapDef's `minPlayers`/`maxPlayers`, else
  two to four for tourney arenas and four or more for maps with no
  free-for-all mode), rotation/vote/queue flags and up to sixteen settings a
  map. The server's `mapdb.txt` (`g_mapDatabase`) overrides them and may list
  the rotation; a file with an error is refused whole, by line. The rotation
  (`g_mapRotation`: stay, in order, shuffled, or the legacy `g_mapCycle`
  script) passes over maps outside their bounds and ignores the bounds rather
  than stall; `si_mapCycle` remains the rotation's list. The queue (`mymap`,
  one entry a player, `g_mapQueue`) is played first, then the end-of-match
  pick from three candidates (`mappick`, `g_mapPick`), then the rotation. The
  engine asks the game before it reads the server info for a map change
  (`idGame::ValidateServerSettings`, unused until now), so a map's settings
  are in force from its first frame and the previous map's are back; a map
  may not set the map, gametype, pool, purity, slots, name or a password. A
  casual session imports its rules from the settings once and mirrors them
  back at every map start, which overwrote a map's own frag limit in the
  first live run; the database now has that import repeated whenever it
  changed a setting (`InvalidateCasualRulesImport`), and managed matches take
  no per-map settings. The
  server publishes `si_nextMap` and `si_mapPick`. Map votes (both forms), the
  retained Vote and Admin pages, the stock map lists, the implicit series
  pool, `PickMap`, the server card's next map and rotation and
  `MultiplayerResolveMapDecl` read the database; nothing in `src/mpgame`
  enumerates map decls outside it except the developer levelshot helper.
  `GAME_RELIABLE_MESSAGE_MAPDB` carries the two player requests, flood-gated
  to one a second. `mp_map_database_contract.py` pins all of it and executes
  the core alone (tokens, bounds, rotation, shuffle, queue, pick, nineteen
  malformed override files, a full database); `mp_map_rotation_smoke.py`
  runs a pure dedicated server with two bots and one client through bounds,
  per-map settings, a refused vote, the queue and the pick (44 checks, pass
  on Windows x64)
  ([release record](release-completion.md#one-list-of-maps-a-rotation-that-fits-the-server-a-map-queue-and-a-pick-15-november-2026)).
  Bots have no per-map list to convert: the navmesh is built from collision
  on any map. Remaining: the Create Server catalogue, its levelshot list and
  its map-cycle field are engine code with their own mapDef filter
  (`Session_menu.cpp`), and the gametype table they would need lives in the
  game module; the queue and the pick are chat and console only, with no
  retained page and no row in the UI register; the server card's wording in
  the hash-bound menus specification still says `si_mapCycle`; the browser
  has no map filter to convert; shuffle and the legacy script mode are
  covered by the core harness and by reading, not by a live run; nobody has
  played it, and only Windows x64 has run the smoke.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#rulesets-the-map-database-presets-and-buying)):
  the rotation, the prediction and the pick use the game type set for the
  next map; a player who was a client and then hosts gets their `mapdb.txt`
  on the first map; map votes ask the database, so a `modes` override counts;
  the shuffle's recent window follows the rotation's size; a queued map is
  not taken twice by a slow frame; a quit puts a map's own settings back
  before the configuration is saved; a byte order mark no longer fails the
  file. Covered by the map database contract, not by a run. Open: the
  database's publish still does a full `rescanSI`, and `ServerAdvance` is not
  gated for managed matches.
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): both closed. The
  database publishes its two keys with the new engine command `publishSI`,
  and a managed match stays on its map at the end of its review instead of
  having the rotation rewrite `si_map`. `mp_map_rotation_smoke.py` passes on
  the build, the client reading the next map and the pick from the server info.
- [ ] **MP-14 — P2, decision and implementation: rulesets, modifiers and presets.**
  Muff Mode layers Instagib, Quad Hog, Vampiric Damage, Weapons Frenzy and
  NadeFest over any mode and names 58 presets; CPMA composes modes from
  `modes/*.cfg`. openQ4 has `si_instagib` over any mode, `si_ruleset` (stock or
  Classic) and eight managed profiles. Define one modifier layer in the rules
  core, each modifier a replicated flag with its own HUD and browser label:
  Quad Hog (one quad, the carrier scores), Vampiric Damage, Weapons Frenzy
  (fast respawns and regenerating ammo), a single-weapon arena family built
  from Quake 4's own weapons, a Last Man Standing round mode on the Clan Arena
  state, and self-damage, knockback and fall-damage scales with weapon-respawn
  and powerup switches. Named presets (`si_preset`: a compiled table plus
  `presets/<name>.cfg`) set gametype, ruleset, modifiers and limits atomically
  at a match boundary, votable and selectable in Create Server. OPT-05 kept
  factories and rulesets outside scope; MP-11 has since shipped a ruleset, so
  this item records the decision in its place. Overload, Harvester, Domination
  and Attack & Defend stay reserved under OPT-05. Gates: each modifier has a
  contract, a bot run and a predicted remote-client run on OpenGL and Vulkan;
  presets round-trip through vote, Create Server and `si_matchRules`; the
  browser and join card show the active modifiers; a person plays each
  modifier once with real input.
  Progress, 15 November: the layer, the scales and the presets are built
  ([reference](mp-modifiers.md), [player guide](../user/server-setup.md#modifiers-and-presets)).
  Decision recorded there: modifiers are rows of one table latched in
  `idMultiplayerGame::SetGameType` on every peer, never game types, and a
  preset is allow-listed typed data that is parsed and never executed.
  Instagib moved into the layer unchanged. New: Quad Hog (Deathmatch, one
  server-spawned quad, only frags by or on its carrier score), Vampiric
  Damage, Weapons Frenzy, ten weapon arenas, `si_powerups`, `si_selfDamage`,
  `si_knockback`, `si_fallDamage`, `si_weaponRespawn`, `si_preset` with eleven
  built-in presets and `presets/<name>.cfg`, the `modifier`, `weaponArena`,
  `preset` and `listPresets` commands, `si_modifiers` for the browser, and
  Create Server's Modifiers tab in twelve languages.
  Evidence (Windows, OpenGL, hidden listen servers on stock maps with skill-4
  bots, `mp_modifiers_smoke.py`): Quad Hog on `mp/q4dm1` spawned two quads, bots
  took the quad five times, once from where the hog fell, and a quad nobody
  reached was taken back after 20 s; of 23 frags the 12 by or on the hog scored,
  and the final scores add up to those 12. Vampiric Team Deathmatch: 130 hits
  healed their attacker for 975 health in all, none across a team, the highest
  health was 166 of the 200 cap, and the map had no health pickup.
  `preset rocketarena` restarted the match with all four players holding the
  rocket launcher and gauntlet with unlimited ammo, no weapon, ammo or powerup
  pickups and the stock health and armour; `si_instagib 1` set by hand cleared
  `si_preset`; `preset standard` brought the stock pickups back; under Weapons
  Frenzy an idle player's machinegun ammo went from 100 to 300; a preset file
  with a forbidden setting was refused whole with its line number, and
  `presets/friday.cfg` loaded as "Vampiric Team Deathmatch (Lightning Gun)".
  `preset instactf` on `mp/q4ctf1` kept both flags, left no pickups and held its
  capture limit of 8 through the restart. A second client joining a Vampiric
  rocket arena without powerups, and one joining Quad Hog, latched the same
  modifiers and title as the host, populated the same pickups, saw every
  player's loadout and saw the Quad Damage on the hog; neither log reported an
  entity disagreement. `mp_modifiers_contract.py` runs the shipped
  `Modifiers.cpp` and `Presets.cpp` against doubles (the resolve rules, token
  round trips over every combination, preset drift, preset files and the
  commands), and `server_browser_contract.py` reads a modified server's reply,
  hostile text included. In a capture of Create Server the Modifiers tab dimmed Quad Hog outside
  Deathmatch; choosing Rocket Arena set `si_preset`, the arena, `si_powerups 0`
  and both percentages and dimmed Weapons Frenzy, and Standard put them back;
  the native retained-screens test passes its multiplayer pages with the tab's
  checks. Not exercised in play: the three
  percentages and `si_weaponRespawn` (contract only), Vulkan, a dedicated
  server, the browser against a live modified server, and real input.
  Remaining: `callvote modifier` and `callvote preset` (MP-12 owns the vote
  surface and builds on this item's validators); match-rules fields for the
  ruleset, modifiers and scales, so presets round-trip through
  `si_matchRules`; Last Man Standing on the Clan Arena state; a scoreboard
  marker for the hog; the Modifiers tab's caption size and its UI register
  record; Vulkan and dedicated-server runs, the percentages in play, and a
  person playing each modifier with real input.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#rulesets-the-map-database-presets-and-buying)):
  changing `si_ruleset` on a running map (a passed ruleset vote, `preset
  classic`) hung the server in a restart loop; it now loads the map again
  once, which was run. A preset whose game type the map cannot play moves to
  a map that can, or is refused when none is installed.
  Progress, 23 November: votes through MP-12's `callvote`: `modifier <name> 0|1`
  (or the modifier's name), `arena <weapon|none>` and `preset <name>`, checked by
  this layer's validators and restarting the match as the console commands do,
  with Vote page rows for the three modifiers, the arena and the built-in
  presets (MP-12 has the round's record).
- [ ] **MP-15 — P2, implementation: competitive HUD and client options.** CPMA's
  SuperHUD, Q4MAX's HUD scripts and Muff Mode's client commands set the
  expectation. openQ4 already has crosshair size, colour, custom art, animation
  and hit flash, the hit marker and damage numbers under `g_hitFeedback`,
  brightskins with outlines and rimlights, forced models, the lagometer, the
  speed and input displays, four HUD layouts and the stock awards. Add: chat
  tokens expanded on the sender (`#h`, `#a`, `#w`, `#l`, `#n`: health, armour,
  weapon and ammo, location, nearby teammates); hit sounds by damage bucket
  from a selectable set, with the armour variant kept; a weapon preference
  order for auto-switch and empty-weapon fallback; the ammo count beside the
  crosshair; the target's name and status under the crosshair for teammates; a
  team overlay with location names and the weapon carried (the Competitive
  layout shows health and armour only); a server item-timer policy with a
  third state that lets players see the timers; a frag line and match/local
  clocks; and a cosmetic-parity policy for what a server may restrict
  (brightskin intensity, forced models), enforced where the snapshot is
  written. Element placement goes through the retained HUD and its editor
  (UI-06), not a second scripting format. Gates: every element in the
  Remastered, Minimal, Competitive and Classic layouts with register rows under
  UI-11, captures on OpenGL and Vulkan at 4:3 and 16:9, localization, and a
  person judging hit sounds and tokens in play.
  *State, 15 November 2026:* the game side of every item above is in, with
  the timer list, crosshair ammunition and time of day drawn in all four
  layouts and team locations in the Competitive list
  ([player guide](../user/multiplayer-client-options.md),
  [record](release-completion.md#chat-tokens-hit-tones-a-weapon-order-item-timers-and-team-locations-mp-15-15-november-2026)).
  Cosmetic limits are applied by each client's game module on the server's
  instruction: a snapshot carries nothing cosmetic to withhold, so the
  item-timer list and team info are what is enforced where the message is
  written. The frag line and the match clock were already stock. Still open:
  the human judgement, menu rows, register rows and spec text, 4:3 captures,
  a team list outside Competitive, and editor placement under UI-06.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#client-options-chat-and-spectating)):
  item timers clear with the restart in place at a match's start and at each
  round; `say_team` tokens are not expanded when the line will go to
  everyone; a long `say` is cut instead of ending the client; a player cannot
  be named `server`; the team list gives an eliminated player their own team
  only. Contracts only; not run.
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): item timers build
  their list once a send.
- [ ] **MP-16 — P2, implementation: live spectating and casting.** Give live
  spectators what MVD playback already has: follow by name from a named list,
  auto-follow of the killer, the leader, a powerup carrier or a flag carrier
  with manual override, first- and third-person follow views and a chase camera
  (also offered as the death camera), and a free-camera speed setting; a
  casual broadcaster role (both teams' vitals and timers) without a managed
  profile; a typed live event stream (the match journal's events as they
  happen) for overlay tools; and a votable spectator chat policy
  (`g_spectatorChat`, mute spectators). Multi-POV live views, delayed
  broadcast and relay transport remain OPT-04 decisions. Gates: recipient
  filtering holds for every new camera (byte-level checks as in MP-01), a live
  smoke on OpenGL and Vulkan, the event stream consumed by a sample overlay in
  `tools/`, and the [competitive matches guide](../user/competitive-matches.md)
  updated.
  Progress, 15 November: everything but the vote. *Follow:* `follow <name>`
  resolves a name to a slot on the client (the wire still carries slot and
  spawn id), `followlist` prints the list, and `follow auto <mode>` asks the
  server to move the camera. The server keeps one chooser per spectator
  (`mpDemoAutoTrack`, now shared with MVD playback and extended with leader,
  powerup and flag modes) and offers it only players `CanSpectatorFollow`
  allows that spectator that frame, so auto-follow reaches nobody a manual
  choice could not; a manual choice, attack or jump hands the camera back.
  *Views:* `g_spectatorFollowView` (first, third, chase), `g_chaseCameraRange`
  and `g_chaseCameraHeight`, and `g_deathCamera`, are presentation on the
  client: a following spectator's snapshot is built from the followed
  player's position whichever view draws it. `ui_spectatorSpeed` (25 to 300
  percent) travels in userinfo so the predicted free camera and the server's
  agree. *Caster:* with `g_casterAccess 1` outside a managed match a
  spectator takes the broadcaster's audience with `caster on`
  (`ServerRequestCasterRole`, the operator path's own target rule); it is
  withdrawn when the policy goes off, a managed match begins, the caster
  joins the game or the slot changes hands. It adds no disclosure rule: the
  grant is `MPMatchDisclosureBuildGrant`'s broadcaster branch, without item
  timing outside managed matches (casual spectators, casters included, get
  item timers from MP-15's `si_itemTimers`). The client shows the disclosed
  team-vital rows in the match
  panel. *Stream:* `g_matchEventStream 1` writes one JSON object a line to
  `match-events/live.ndjson` ([reference](mp-live-event-stream.md)); the match
  journal is managed-only and has no kill, flag or score events, so the
  stream is built from scoreboard state on whichever end writes it, and
  `tools/overlay/match_overlay.py` consumes it. *Chat:* a spectator whose
  all-chat is kept from players (`g_spectatorChat 0`, live play) is told so,
  once a minute at most. Three defects found on the way are fixed: a casual
  spectator carried the side of their last `ui_team`, so in team modes they
  could follow only that team; a broadcaster who joined a casual game stayed
  an inactive broadcaster in the session and kept both sides' vitals while
  playing; and a follow request made between frames was dropped whenever
  the last prediction pass was a re-run (`idEntity::ClientSendRequestEvent`).
  Evidence: `mp_match_spectator_follow_contract.py` (production follow,
  auto-follow and caster request paths against the real disclosure policy),
  `mp_spectator_casting_contract.py` (grant and every withdrawal against the
  real session, eight failing controls), `mp_live_event_stream_contract.py`
  (schema, overlay replay, ten failing controls), `mvd_navigation_contract.py`
  (the new modes), and `mp_spectator_casting_smoke.py`: a host, three bots
  and a remote spectator in a live Team DM match, read from both ends, on
  OpenGL and Vulkan. Recipient filtering is checked on the wire there: the
  remote client's accepted match view holds no vitals before the grant, four
  after it and none after withdrawal, and every camera it holds is one the
  server reports as allowed. Remaining: the `spectatorchat` vote, which
  waits for MP-12's vote set (bit 22 is reserved for it); a caster HUD panel
  and a menu page for the named list under UI-11 (the vitals reuse the
  managed context panel's rows); the three new modes in MVD playback's
  `demo_autoTrack`; real-input play; other platforms.
  Progress, 17 November ([audit](mp-audit-2026-11-17.md#client-options-chat-and-spectating)):
  an eliminated team member is no longer sent every opponent's position and
  vitals, and can follow their own team mates (they could follow nobody); a
  free camera with nobody to follow stays where it is instead of jumping to a
  spawn point; team chat reaches the whole team and spectator-only chat only
  those who chose to watch. Contracts only; the spectator smoke was not
  re-run. Open: the chase and death cameras' clip trace.
  Second round, 17 November ([second round](mp-audit-2026-11-17.md#second-round)): the chase and death
  cameras clip their raised focus before pulling back, so a low ceiling no
  longer holds the camera inside it. Not yet looked at by eye.
  Progress, 23 November: the `spectatorchat` vote, on bit 22 as reserved, with a
  Spectator Chat row on the Vote page; `g_spectatorChat` is now in the server
  info so clients know its value (MP-12 has the round's record).
- [ ] **MP-17 — P1, decision and implementation: player identity, persistence
  and ranking.** `com_guid` is read-only and never written, so GUID bans do
  nothing and no feature can recognise a returning player; the archived plan
  rejected counterfeit identity from names or addresses. Decide and build an
  opt-in identity: a client-generated key pair whose public key signs a server
  challenge, no account and no network service, optionally bound to a Steam
  identity under STEAM-02; the server stores a hash and a player can reset the
  key. On it: reconnect restoration on every server (team, score, seat, Duel
  queue place and, in round modes, the round's inventory; Muff Mode restores by
  social id and offers `ghost <code>` as the fallback); working bans (MP-18); a
  local player profile with match history; a per-gametype skill rating (Muff
  Mode uses Elo with K 48 for a player's first 20 rated matches, then 32); an
  awards reel and a `stats` command with accuracy at the end of any match;
  casual-server match exports in the managed schema-v2 JSON plus a readable
  summary and a catalogue; and an opt-in local ranking table on the server.
  Nothing is uploaded. Gates: the key protocol reviewed against replay and
  slot-reuse (the MP-01 identity guards), rating fixtures against a known
  sequence, reconnect smokes across every mode, privacy notes in the
  [server security guide](../user/server-security.md).
  Progress, 23 November: the decision and the first round. The user chose an
  identity that is on unless the player turns it off (Player Setup's Player
  Identity, `ui_identity`, which travels in userinfo), in place of this item's
  "opt-in": each player's game keeps a random seed in `identity.key` and
  derives a separate Ed25519 key for each server from it and the server's own
  id (`identity_server.txt`), so servers cannot link a player across them.
  Signing joins the updater's verifier in `src/idlib/Ed25519.*` (RFC 8032's
  vectors in `tools/tests/native/Ed25519SignTest.cpp`). The server sends a
  challenge (its id and a fresh nonce) only to a game whose userinfo offers an
  identity, so a 0.15.0 client never sees the message; the proof signs the
  domain, the server's id, the nonce, the slot and the connection's number,
  and one answer spends the challenge whatever it says. The fingerprint
  (sixteen hex digits) is in `status`. Bans by identity share `banlist.txt`
  (`ban identity <fingerprint> ...` lines, which an older build keeps
  untouched), `ban id:`/`unban id:` take them, and banning a connected player
  bans their range and their identity. A casual leaver's team and score are
  kept for `g_reconnectRestore` seconds (300) and given back by identity, or
  by the ghost code the server tells a player without one; Tourney keeps no
  places. `g_ranking 1` keeps `ranking.txt`: Elo by game type, team modes
  against the other team's average, free-for-all pairwise by score, K 48 for
  the first 20 rated matches then 32. Each player's game appends its matches
  to `profile/history.ndjson` (`history`). `mp_identity.py` runs both native
  tests and pins the wiring; `mp_identity_smoke.py` qualifies it on a pure
  dedicated server with three hidden clients. Open: the awards reel and
  `stats`, casual exports, Duel queue places and round inventories, Tourney
  ratings, an independent review of the protocol, Steam identity and other
  platforms.
- [ ] **MP-18 — P1, implementation: server administration and moderation.**
  openQ4 has rcon2, the Admin page's kick, ban and team switch, the PBKDF2
  referee, voice mute and `si_motd`; its bans are keyed to a GUID that is never
  sent, and there is no address ban, text mute, flood control, inactivity
  policy or admin log. Add: bans by identity (MP-17) and by address with prefix
  lengths for IPv4 and IPv6, with expiry and reasons, migrating `banlist.txt`;
  text-chat mute by slot and by vote; chat and command flood protection; the
  per-address connection cap left open by the netcode audit; a name policy
  (reserved names, length, control characters, a name already in use by a
  connected player); a kick/ban log and an NDJSON match log for casual
  servers; an admin role between referee and rcon for a listen host or a
  trusted admin (Muff Mode `admin <password>`), using the match pipeline's
  capabilities and never the file system; graceful shutdown (announce, finish
  the match, stop); and `status` output that shows identity and address.
  Gates: `network_security.py` pins the caps and policies, an asset-free
  contract per command, a dedicated smoke over ban, mute, flood and shutdown,
  and the [server security guide](../user/server-security.md) updated.
  Progress, 22 November 2026: address and range bans with expiry and
  reasons, the `banlist.txt` migration, the text mute by slot (kept by
  address across a reconnect), the chat flood window, the per-address
  connection limit (off by default, as the audit's decision stood), the name
  policy, the kick/ban log and `status` with addresses are implemented;
  `network_security.py` pins the caps, `mp_moderation.py` is the asset-free
  contract for each command, `mp_moderation_smoke.py` covers ban, mute, flood
  and the limit on a pure dedicated server (Windows, loopback), and the
  guide is updated ([record](mp-moderation.md)). The same day: the mute vote
  (the kick vote's target plus one flag, so the packed vote's layout is
  unchanged), graceful shutdown (`serverShutdown`: announce, finish the
  match or wait out a time, say goodbye, stop) with its smoke, and the
  engine's own `kick` in the log through `net_serverLastDrop`, without a new
  game-interface slot. Then the NDJSON match log (`g_matchLog`: the live
  event stream's typed events appended on a server, one format for both)
  and `banlist.txt` written through a staged file and one atomic replace.
  Then the admin role, on the user's decision of what an admin may do: a
  second password (`net_serverAdminPassword`) through the referee's challenge and
  response, answered by its own authentication service; the grant belongs to
  the connection and lasts one map; a logged-in admin's `gameKick`, `mute`,
  `unmute` and `ban` travel as one typed request for a connected player, the
  ban cut to `g_adminMaxBanMinutes`; lifting bans, addresses, ranges and
  shutdown stay with the console and rcon; an admin who is watching is a
  referee as well. `mp_moderation_smoke.py` covers it with a second loopback
  address (104 checks). The admin passwords and the referee's
  `g_refPassword` are private cvars declared in the engine, so none of them
  reaches the console history or the log whichever game module is loaded.
  An audit the same day (two independent reviews and a pass of the author's;
  [record](mp-moderation.md#audit-22-november-2026)) fixed what it found:
  the commands read their line as typed, because the console's lexer could
  ban a different IPv6 range than the one typed; a ban list the server
  cannot read is never written over; IPv6 players are banned, muted and
  counted by their `/64`; names are read with the engine's own escape rules;
  a mute holds the name; `serverShutdown match` covers every map load; an
  admin's login lasts for the connection, and the host can list, revoke and
  turn off admin logins. `mp_moderation_smoke.py` passes 133 checks.
  Still open: identity bans and identity in `status` (MP-17), command flood
  beyond the engine's existing reliable-message budget, the menu rows for a
  ban's reason and for the mute vote, a menu for admins, and other platforms
  (IPv6 included).
  Progress, 23 November: the Admin page's Ban has a **Ban Length** (10 minutes,
  an hour, a day, a week or permanent, the default) and a **Ban Reason** (none
  or one of five, recorded in the ban list and the admin log in plain words),
  and its ban list a details column with each ban's reason and time left. A
  remote rcon admin's Ban carries them to a server that advertises
  `si_uiTargetProtocol` 3 (`serverAdminTarget ban <slot> <identity> <seconds>
  <reason>`); an older server gets the old form. The Vote page's **Mute Instead
  of Kicking** is the mute vote's row (MP-12). Still open: a menu for admins who
  log in with `admin`, identity bans (MP-17) and other platforms.
  Progress, 23 November (MP-17): bans by identity and an identity in `status`.
- [ ] **MP-19 — P1, decision and implementation: server discovery.** `net_master0`
  defaults to id's `q4master.idsoftware.com:27650`; the name resolves today,
  but no openQ4 evidence shows it listing openQ4 servers and the project does
  not control it. Decide and provide a discovery path the project controls: an
  openQ4 master (heartbeat and query protocol documented, IPv4 and IPv6,
  several masters, operator-run instances) with an HTTPS server-list fallback
  the browser can read; the browser adds recent servers, region/ping sorting
  and persistence; friends and recent-player lists stay local. Steam lobbies
  remain STEAM/OPT-09. Gates: a fresh install on Windows, Linux and macOS lists
  a test server from the Internet source; a master outage falls back and says
  so; `server-setup.md` and `multiplayer-networking.md` updated.
  Progress, 23 November: the decision and the program. The user chose to
  keep `net_master0` at id's master and ship `net_serverListUrl` empty until
  the project's hosts are chosen. `tools/master/openq4_master.py` is a
  standard-library master: a heartbeat is answered by `getInfo` with a
  random challenge and listed only when the `infoResponse` echoes it;
  `getServers` gets six-byte IPv4 records and `getServersExt` dpmaster-style
  IPv4 and IPv6 records, each filtered by protocol and game; per-address
  rate limits; `--json` writes the list a web server publishes. The browser
  reads such a list over HTTPS on a worker thread at each Internet refresh
  (`src/framework/async/ServerListFallback.*`, the reader in
  `ServerListParse.h`) and merges it with the master's. `master_protocol.py`
  plays both sides of the protocol against the program and
  `mp_server_discovery.py` runs the reader's native test and pins the wiring.
  `NETScan` runs the browser's Internet search from the console, and
  `mp_master_smoke.py` qualifies the whole path on loopback on Windows: the
  program, a staged dedicated server's heartbeat and challenge answer, the
  JSON list, and a hidden client's `NETScan` listing the server once with its
  name and ping. Open: the hosts and their deployment, the HTTPS list against
  a real certificate, recent servers and region/ping sorting in the browser,
  the master's outage notice in the browser, and a fresh install on each
  platform finding a server from the Internet source.
- [ ] **MP-20 — P1, qualification and implementation: movement parity and a
  ProMode-style option.** Qualify openQ4's player movement against retail 1.4.2
  on the stock multiplayer maps (strafe jumps, crouch slides, ramp and jump-pad
  flights, stairs and the known trick jumps), frame-exact at `si_fps 30` and
  bounded at 60 and 90 with HF-01's clocks, through `in_movementProbe` scripts
  and the retail binary on the same inputs
  (the [retail baseline ledger](retail-baseline-ledger.md) records the retail runs). Then an optional
  movement ruleset in the MP-14 layer: air control, double jumps everywhere,
  ramp-jump boost, faster weapon switches and an armour-system choice (CPMA's
  CPM lineage), as `pm_` server cvars the bots read (MP-09). Gates: a recorded
  parity table per map and tick rate with any divergence named and fixed or
  accepted; the option's physics predicted on a remote client without
  corrections; a person plays both rulesets with real input.
- [ ] **MP-21 — P1, decision and implementation: one demo container and a
  compatibility registry.** Two streams, two containers today: MVD 1.3 writes
  CRC-checked records with no compression and its own index and keyframe
  directory; render demos v14 write the `OQ4B` block container (long-range
  matches and DEFLATE, a CRC per block, a timeline from headers alone, 64-bit
  files; [design](multiview-demos.md#block-container-keyframes-and-large-files-v14-14-november-2026)).
  Standardize on the block container for every stream (MVD, render and
  MP-22's client network demo) with one header, trailer, timeline and keyframe
  directory; one `demoConvert` that turns any readable historical file into the
  current container without changing its playback, replacing `compressDemo`;
  and one format registry document listing every version a release has
  written with its reader status. Backward compatibility: every version a
  shipped release wrote keeps its reader and golden fixtures (complete,
  interrupted, truncated, corrupt, unknown optional, unknown required, older
  and newer minor), forward compatibility by optional records, and the
  [update policy](multiview-demos.md#update-policy) as written; MVD 1.0 to 1.3
  stay readable. Cross-platform byte-identical reads of every fixture on
  Windows, Linux, macOS and ARM64 in CI. MP-05 stays closed at its selected
  scope. Gates: the registry, the fixtures in `tools/tests`, the converter
  round-tripping each fixture, and a play-through of a converted file of each
  kind on OpenGL and Vulkan.
- [ ] **MP-22 — P1, implementation: the player's demo workflow.** A player who
  is not hosting cannot record a match today; only the server writes an MVD,
  and a render demo keeps the local view at ten times the size. Add
  client-side recording on any server: a network demo (received snapshots and
  the player's own commands) in the MP-21 container, started by `record`, by
  autoaction (`ui_autoAction demo ss stats`, driven by the countdown-to-live
  edge, stopped at the result) or by a server's `si_autoAction`, named by
  `MPBuildMatchBasename`; playback from the recorded point of view with the
  same deck as MVD and follow of what the recording carried. Complete the
  deck: a named player list, reverse scrubbing from keyframes, frame-accurate
  stepping, `timeDemo` for MVD and network demos (benchmark and determinism
  checks the FPS items can use), video and image export (a PNG sequence with
  WAV, optional pipe to an external encoder) in place of the unfinished AVI
  path, generated thumbnails and library metadata (players, map, mode, result,
  markers; searchable), and a content-mismatch message that names the pak.
  Retail `.demo` and `.netdemo` files stay Legacy unless a reader is decided
  here. Gates: a client records on a cheats-off dedicated server and plays it
  back on OpenGL and Vulkan, the deck features each have a fixture, export
  verified frame-exact against captures, and the
  [demos guide](../user/multiview-demos.md) updated.
  Progress, 23 November: client-side recording. `record` (and `recordNetDemo`,
  `recordMVD`) on a server the player joined writes a client-view MVD: the
  header requires a new feature bit, the start state is the game module's
  client network info behind the engine's last snapshot, frame and commands in
  flight, and the stream holds every snapshot message, applied snapshot, game
  reliable and user-info change the client gave its game, with the player's
  own commands, on a monotonic real-time clock. Playback runs the game as the
  recorder's client and repeats its prediction with those commands; the start
  state is validated end to end first, follow and free camera are refused,
  seeking replays from the start, and the library and deck offer the controls
  that apply ([format](multiview-demos.md#client-view-recordings-mp-22-23-november-2026)).
  `mp_client_demo.py` pins the wiring and `mp_client_demo_smoke.py` records on
  a cheats-off pure dedicated server with bots and plays the file back.
  Progress, 23 November (second round): autoaction (`ui_autoAction`,
  `si_autoAction`: demo, ss, stats, named by `MPBuildMatchBasename`), keyframes
  and `mvdRecover` for client recordings, `timeDemo` for any MVD (one tic a
  frame, uncapped, `MVD_TIMEDEMO` report), and a content refusal that names
  the pack that differs from the recording's. The retail-demo decision (user,
  23 November): build readers for both retail `.demo` and `.netdemo`.
  `mp_autoaction.py` runs the basename's native test and pins the wiring;
  `mp_autoaction_smoke.py` qualifies both sides on a cheats-off dedicated
  server; `mp_client_demo_smoke.py` adds a keyframe seek, a timedemo, a
  recovery and a refusal.
  Progress, 23 November (third round): the deck's player list (**Players**,
  `demoPlayers`), frame steps back through keyframes (**Step back**,
  `demoStep -1`) with exact tics both ways, `demoExport` as a PNG sequence,
  markers in client recordings, a summary of names in every recording, the
  library's player and recorder details with `demoFind`, and generated
  thumbnails; both smokes check each.
  Progress, 23 November (fourth round): `demoExport` writes the sound track,
  `exports/<demo>/<demo>.wav`, from an `ALC_SOFT_loopback` mixer the sound
  system restarts onto for the export at the playback device's rate and
  layout. Each picture mixes exactly its step on a sound clock that follows the
  export, the steps add up to `pictures * 1000 / fps`, and the playback device
  opens again at the end ([sound in exports](multiview-demos.md#client-view-recordings-mp-22-23-november-2026)).
  `SoundCaptureTest` covers the arithmetic and the WAV header;
  `mp_client_demo_smoke.py` checks the file's format, length and level and
  the device's return. The unfinished render-demo `aviDemo` path writes no
  pictures, so it gets no track. No optional pipe to an external encoder: the
  [demos guide](../user/multiview-demos.md#export-a-demo-as-pictures) gives
  the `ffmpeg` line instead.
  Progress, 23 November (retail render demos): retail Quake 4 1.4.2 render
  `.demo` files play from the library (listed as **Retail Quake 4 render
  demo** with their level) and with `playDemo` and `timeDemo`, with pause,
  speed, step both ways and replay-based seek
  ([format and reader](multiview-demos.md#retail-quake-4-render-demos-mp-22-23-november-2026)).
  The layout was read from the retail executable's writers: byte tokens, GUI
  window trees and live effect state inside entity and effect records, bounds
  written maximum first. Every count, index and length is bounded; a damaged
  file stops with a warning. A step back now lands on the previous frame of any
  render demo on a frame timeline. `retail_render_demo_contract.py` builds,
  cuts and corrupts a retail-layout stream; `retail_render_demo_smoke.py`
  records a fixture with retail Quake 4 (console scripts only), plays it in all
  four compression schemes on OpenGL and Vulkan, matches openQ4's frames with
  retail's own `aviDemo` captures (nine cameras, mean luma difference 2-4 of
  255 against 11-16 to every other camera), and plays sixteen cut or corrupted
  copies without a crash or fatal error.
  Progress, 23 November (retail network demos): the reader. Quake 4
  1.4.2 writes `.ndmo85` (version 12, protocol 2.85; the layout was read from
  the 1.4.2 executable) and ships two, a client and a server recording, which
  serve as fixtures without running the retail game. Every outer length is
  checked before anything reads it; the 1.4.2 channel (fragments, zero-run
  coding, reliable queue) turns the packets into events, written in memory as
  client-view or server-view MVD records marked protocol 2.85, and the MVD
  player plays them with its deck (seek by replay, follow and free camera for
  a server recording), the library and `playNetDemo`/`playNetTimeDemo`/
  `timeDemo`. The game module reads 2.85 in the 1.4.2 layouts (the 2.41 layout
  plus gates on what openQ4 added), retail's delta-base reads, and the
  recording's decl numbering, rebuilt from Quake 4's own paks in parse order
  and identical to what the retail dedicated server lists for entity
  definitions, effects, sounds, materials and skins
  ([format](multiview-demos.md#retail-quake-4-142-network-demos-mp-22-23-november-2026)).
  `retail_netdemo_contract.py` reads both demos with an independent reader and
  pins the source; `mp_retail_netdemo_smoke.py` plays both to their ends,
  times one, refuses every damaged copy without a fatal error and compares the
  numbering with `Quake4Ded.exe`. Demos from releases before 1.4.2
  (`.netdemo`, other layouts and protocols) and recordings at another
  `si_fps` are refused with the reason.
  Open: `.netdemo` files from before 1.4.2 and recordings at another `si_fps`,
  network demos on Vulkan, Linux and macOS runs, export verified frame-exact
  against captures, and a person watching a recording and listening to an
  export.
- [ ] **MP-23 — P2, measured optimization: demo storage efficiency.** MVD is
  written uncompressed; the render container reaches 4.78:1 on its stream and
  the v12 joint predictor cut 24.5%. Budgets first: MVD bytes a minute for a
  12-player CTF match and a Duel, render demo MB/s at 60 frames a second on a
  busy and a quiet level, with a `demoStats` command reporting the split by
  record category. Then MVD through the block codec; field encodings in the
  snapshot (varint and zigzag deltas against the baseline as v12 does for
  joints, packed PVS words); keyframe cost held under 2% of the file; render
  demo 2D, sound and effect encodings (28%, 21% and 13% of the uncompressed
  stream); window and anchor tuning; ratios compared with xz as the ceiling;
  and the recording thread's CPU per block held so that recording adds no
  measurable frame time (HF-02's budget). Targets: MVD at least 3:1 against
  today's 1.3 bytes, render demos at most 0.5 MB/s on the busy level at 60
  frames a second, both byte-exact against uncompressed playback. Gates: the
  measurements recorded in [multiview demos](multiview-demos.md), the native
  codec test extended to each encoding, and `compressDemo`/`demoConvert`
  reproducing the ratios on the fixtures.
  Noted 17 November: MVD snapshots are paced in milliseconds against the
  16 ms game clock, which gives 15 a second where the default asks for 20.
  Matching the request makes recordings about a third larger, so it is a
  decision for this item ([audit](mp-audit-2026-11-17.md#engine-network-layer)).
  Decided 17 November: record the 20 a second asked for. Snapshots are paced
  on the frames' real times; recordings grow by about a third
  ([second round](mp-audit-2026-11-17.md#second-round)).
- [ ] **MP-24 — P1, qualification: demo robustness and platform breadth.** The
  render container has native coverage of 416 header flips, 2,264 truncations
  and hostile payloads; MVD has 2,032 cut points through `mvdRecover` and a
  CRC per record. Close the gaps with one fuzz and mutation harness for every
  reader (both containers, every record type, hostile lengths, duplicate and
  out-of-order records, directories that lie about offsets) under time and
  memory bounds: playback never crashes or fatals, always reports the offset,
  and keeps a hostile-file corpus under `tools/tests`. Give MVD what render
  demos have: worker-thread writing with at most half a second lost to a
  crash, 64-bit files past 2 GiB (revisit the 1,024 MB default), and a real
  hours-long recording of each kind. Qualify recording and playback on a
  cheats-off dedicated server, under CPU load with no frame-time hitch
  (measured), on Vulkan, Linux, macOS and ARM64 for both kinds, the same file
  played on two platforms giving identical state checksums (the float-text and
  FP-flag traps), and a person listening to a recorded demo once. Gates: the
  harness in CI, the recordings and checksums recorded in
  [multiview demos](multiview-demos.md), and the platform rows in the
  [capability matrix](engine-capability-matrix.md).
- [ ] **MP-25 — P2, implementation: multiplayer splitscreen for up to four
  local players.** Multiplayer only; the single-player game, the Awakening
  campaign and SP Arena gain no local players. Deliver a splitscreen that
  plays as well as a single view, on a listen server, against bots, and
  joining a remote or dedicated server.

  **Where the source stands (survey, 21 November 2026).** Each point names
  the single-view assumption a stage below removes.
  - **Server:** `idAsyncServer` already runs clients that have no network
    connection. Bots enter through `InitLocalClient( clientNum, true )` on an
    `NA_BOT` channel and take commands from `ServerSetBotUserCommand`; the one
    human local client (`localClientNum`) takes
    `usercmdGen->GetDirectUsercmd()` in `LocalClientInput`. A second human on
    a listen server is that bot path fed by a seat's own command source, so
    it needs no protocol change.
  - **Game module:** `idGame::Draw( clientNum )` and
    `idMultiplayerGame::Draw( clientNum )` already draw for a named client,
    but `src/mpgame/` asks who is local on nearly 580 lines through
    `GetLocalPlayer()` and on 156 through `localClientNum` (62 in
    `MultiplayerGame.cpp`, 28 in `Player.cpp`). Those lines cover HUD
    updates, private sounds, the view weapon, hit feedback, item timers, team
    info and prediction.
  - **Remote client:** `idAsyncClient` holds one `clientNum`, one channel and
    one prediction (`ClientPrediction( clientNum, ... )`). Each snapshot
    carries one player's private state, built from that player's PVS.
  - **Input:** one `idUsercmdGen`, one key-binding table and global
    `in_*`/`m_*`/`joy_*` settings. The SDL3 backend keeps one open gamepad,
    and PAD-01 opens every pad.
  - **Renderer:** `renderView_t` already carries an `x, y, width, height`
    viewport. Temporal history already has slots (`temporalHistorySlot`: 0,
    or 1 for a VR right eye, VR-05). The post chain runs whole-target passes
    (`RB_BeginFullscreenPostProcessPass`). Resolution scale, dynamic
    resolution, auto-exposure and HF-03's late look each keep one view's
    state.
  - **Sound and menus:** the game places one listener per frame, and its
    `listenerId` (entity number + 1) also gates `SCHANNEL_PRIVATE` sounds.
    The session holds one active GUI (`guiActive`).

  **Decisions, settled and recorded before stage 2 starts:**
  - **D1, remote transport.** Recommended: one connection carries every
    seat. The server sends one snapshot built from the union of the seats'
    PVS, with a private block per seat, and each client packet carries every
    seat's commands. This is a protocol bump with an MP-21 registry entry.
    The alternative is one connection per seat. It needs no protocol change,
    but the client would hold several snapshot streams of one world at
    different frames. Price four seats against MP-08's packet budget either
    way.
  - **D2, seat identity.** Each seat is a full server client slot with its
    own client number, user info, name, model, team and score. Seats count
    against the server's player limit, and per-address limits count them
    correctly. A server option refuses or caps guests. A guest's identity and
    stats follow MP-17.
  - **D3, interface.** Recommended: splitscreen runs on the retained
    interface only, and the stock interface refuses it with a localized
    notice. The alternative is a scaled stock HUD per viewport, accepted only
    if a measured capture shows it readable.
  - **D4, audio.** Recommended: for each emitter, the nearest seat's listener
    sets spatialization and gain, so shared world sounds never double. The
    alternative is summed listeners with gain limits.
  - **D5, keyboard and mouse.** Keyboard and mouse together form one seat.
    Steam Deck's built-in controls are one seat.
  - **D6, automated seats.** Automated runs drive extra seats with bots and
    cheat-gated, seat-addressed in-engine probes in the manner of
    `in_movementProbe`. They never synthesize device input.

  **Stages.** Each stage closes on its own exit evidence. Stages 1 to 5
  deliver listen-server splitscreen without a protocol change; stage 6 adds
  remote servers.
  1. **Seats and input.** A seat table maps each seat to its device, profile
     and client number. It uses PAD-01's every-pad layer (SDL3 instance ids),
     with a press-a-button-to-join lobby that binds a device to a seat.
     Hot-plug and reconnect keep the seat. Each seat has its own command
     source (one `idUsercmdGen` state per seat). Bindings, sensitivity,
     inversion, aim assist and rumble become per-seat profile namespaces over
     the global settings. Guest profiles are saved with the host's, and
     PAD-08's player LEDs show the seat. Exit: an asset-free contract for
     assignment, hot-plug, reconnect and profile namespaces, and two virtual
     pads in PAD-01's harness bound to two seats.
  2. **Listen-server seats.** Extra seats join as local clients through the
     bot path, and `LocalClientInput` fills each seat's command. Classify
     every `GetLocalPlayer()` and `localClientNum` use in `src/mpgame/` as
     per seat (view, HUD, private sounds, view weapon, hit feedback), any
     local seat, or host only. Route the per-seat uses through a current-seat
     context that `Draw` and the HUD set. Scoreboards, votes, chat and team
     balance treat each seat as a player. Exit: a game-module contract over
     the classified call sites, and two to four seats playing against bots
     on a listen server. A debug seat switch shows each seat full screen in
     turn.
  3. **Split views.** Layouts are viewport rectangles, each with its own safe
     area: full, two-way (horizontal or vertical, chosen by aspect and
     resolution), three-way and a four-way grid. Field of view is corrected
     for each viewport's aspect. Each seat has one render view with its own
     subviews (mirrors, remote cameras), view weapon, view effects, and
     damage and death views. Post passes stay inside the seat's rectangle on
     OpenGL and Vulkan. Temporal history, motion vectors, auto-exposure and
     dynamic resolution are kept per seat, by widening the history slot to
     seat times eye. Late look runs per seat or is off in splitscreen.
     Culling, shadow and light caches are shared across seats where it is
     safe. Exit: a native test of per-viewport views and confined post
     passes, captures of each layout at 16:9, 21:9 and 4:3 on both backends,
     and each seat's image matching its full-screen capture within a stated
     tolerance.
  4. **HUD, menus and text.** The retained HUD is laid out per viewport at a
     readable minimum scale: crosshair, ammunition, chat, centerprints, kill
     feed, hit markers, item timers, and the follow and spectator panels. A
     pause or settings menu belongs to the seat that opened it, while the
     other seats keep playing. Each seat enters chat through PAD-05's
     on-screen keyboard. The scoreboard and intermission are shared, and
     notices go to their seat. Every new label is localized, and D3's
     interface policy applies. Exit: UI register rows, captures for each
     layout, and passing retained-document validators.
  5. **Audio.** D4's listener model with per-seat private channels for the
     announcer, hit tones and pickups. Voice chat is routed per seat. Exit:
     a native test of listener choice and gain, and a listening check in the
     human checklist entry.
  6. **Remote and dedicated servers.** D1's transport with per-seat
     prediction, rewind (MP-07) and private snapshot state, plus D2's guest
     caps and identity. Exit: clients with two to four seats join a dedicated
     server under MP-08's simulated impairment, with bandwidth recorded
     against the budget. Single-seat clients and older demos are unaffected.
  7. **Spectating and demos.** A seat can spectate or follow while the others
     play. MVD and MP-22 client demos record each seat and play back one
     viewport or the whole layout, with MP-24 fixtures for any new record.
  8. **Cost and qualification.** Record per-layout frame-time budgets against
     FPS-01 on the reference machine. Choose per-layout resolution scale and
     effect presets so four seats hold the refresh class FPS-02 sets for the
     active tier, measured, not assumed. Run on Windows, Linux and Steam
     Deck. People play each layout with real controllers through a
     [human qualification checklist](mp-human-qualification.md) entry.
     Publish a splitscreen player guide in `docs/user/` and release notes.
  Close MP-25 when every stage exit holds on one build. That build must pass
  two-, three- and four-seat smokes against bots on a listen server and
  joining a dedicated server, on OpenGL and Vulkan.
- [ ] **MP-26 — P2, implementation: Horde, a co-operative wave mode against
  Quake 4's Strogg.** Reference: Muff Mode's Horde for the Quake II Rerelease
  (MMREX, `DarkMatter-Productions/MuffMode`: `mm_horde*`, the
  configuration reference's Horde sections, the level design guide's spawn
  anchors and `horde-nav-format-design.md`), adapted to Quake 4 rather than
  copied. Today the multiplayer module compiles Quake 4's AI, AAS and monster
  classes (`src/mpgame/ai/`, registered in `Class.cpp`), but `idAI` and
  `idActor` write no snapshot state, so a client never sees a monster, and no
  multiplayer mode spawns one. Deliver Horde as a new multiplayer mode in
  `game_mp`, not a single-player co-op path:
  - **Monsters on the wire:** decide and implement how a monster reaches a
    client (snapshot state for position, animation, health, attachments,
    effects and death; ragdolls and gibs as client-side presentation; monster
    projectiles through the existing projectile path), within MP-08's packet
    budget and a protocol bump that keeps older demos playable. Derive the
    live-monster cap from a measured snapshot cost instead of a guess (Muff
    Mode caps its alive count to keep client buffers from overflowing).
    Monsters need lag compensation for hit-scan (MP-07's rewind) and stay out
    of client prediction.
  - **Wave director:** a point budget per wave that grows, then tapers past a
    content peak; scaling by fighter count and map size; spawn pacing with
    bursts and rests; an alive cap; unlock waves for each Strogg type with
    featured first spawns; roster variety that widens over the run;
    champions (health, damage and speed multipliers with a visible marker);
    themed waves; scheduled boss waves from named profiles (Quake 4's large
    Strogg: Gladiator, Heavy Hover Tank, Harvester, Network Guardian and the
    like, chosen for what fits a multiplayer arena); optional wildcard waves
    that reuse MP-14's modifier layer where they overlap; finite runs set by
    the round limit and an endless run.
  - **Where monsters come from:** typed spawn anchors (`info_horde_spawn`,
    flying and boss variants) with wave range, weight, cooldown and an
    optional class key, a minimum distance from fighters, and fallbacks to
    player spawns; a decision, made here with a measured check, on whether
    single-player maps can host Horde (their monster placements reused as
    anchors, as Muff Mode does) or Horde stays on multiplayer maps and
    community maps. Every map the mode lists has the AAS sizes its roster
    needs; a map without them is refused or limited to the types it can
    path, never run with monsters stuck in place.
  - **AI for a crowd of players:** target choice spread across fighters
    (load, threat, finishability and reachability), periodic retargeting,
    pursuit that does not give up when sight is lost, stall recovery that
    retargets and then relocates a stranded monster, medics and other
    support roles kept, and per-frame budgets for path and reachability
    queries.
  - **Players, lives and rewards:** lives per wave, eliminated players
    spectating until a rally (kills earn a team mate back with brief
    protection) or the next wave; defeat when every fighter is down or the
    server empties; drops from kills with richer drops from champions and
    bosses; kill-streak momentum and a flawless-wave bonus; slower non-weapon
    item respawns; powerup timers paused between waves; bots (MP-09) as
    fighters that target monsters and survive waves.
  - **Presentation and hosting:** a wave and remaining-monster readout and
    end-of-wave notices in the retained HUD and the stock HUD, a marker on
    the last few monsters, a Horde scoreboard and intermission, the mode in
    Create Server with its options, localized strings for every new label,
    and announcer lines. Horde is unranked (no MP-17 rating, no awards reel)
    and gets its own MP-13 map-database entries and an MP-14 preset; the
    managed competitive pipeline does not offer it.
  - **Demos:** MVD and MP-22 client demos record and play back waves,
    monsters and bosses, with MP-24 fixtures for the new records.
  Gates: an asset-free contract for the director (budgets, unlocks, scaling,
  boss schedule, endless taper) with fixed seeds; a native test of monster
  snapshot encoding and the alive cap against MP-08's budget; a dedicated
  smoke where bots clear a finite run and lose an endless one on two stock
  maps, with a remote client seeing every monster, on OpenGL and Vulkan; a
  measured frame-time and bandwidth record at the alive cap; MVD playback of
  a boss wave; Windows and Linux runs; people playing a run together with
  real input through a [human qualification checklist](mp-human-qualification.md)
  entry; and a Horde section in a player guide and the server setup guide.

## Audio and voice

Sources: [audio implementation and remaining limits](audio-modernization.md),
[retail reverb](retail-audio-reverb.md), [networking guide](../user/multiplayer-networking.md).

### AUD completion checkpoints

**Estimate: 80%; 0/7 tasks closed.** Open checkboxes here predominantly reflect
qualification debt rather than missing core implementations. However, mobile
memory bounds are substantive outstanding scope, not automatically satisfied by
a listening pass. Subtitles now cover nearly every campaign line from installed data
(lip-sync transcripts, helmet-radio copies and shader script lines); caption
packs, which openQ4 supports but does not ship, correct or translate them, and
the non-verbal remainder is marked for players rather than counted as covered.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| AUD-01 — Partial | 54 emitter/OpenAL distance measurements validate current map-unit curves; no speculative unit conversion needed. A rendered retail/openQ4 comparison harness is implemented; openQ4's rendered half follows the authored curve within 0.6 dB and is silent at and beyond the maximum. | One focused run of the retail half (retail mutes itself while unfocused) passing the comparison. |
| AUD-02 — Partial | Simulated endpoint recovery, scene-clock save/load repair, Linux checks and repeated-map audio residency evidence exist. | Physical endpoint loss/hotplug, audible pause/cinematics, long sessions and target-OS breadth including macOS. |
| AUD-03 — Partial | Aggregate payload budgets/backpressure, bounded compressed streaming, recycled-source fix and exactly-once low-memory trim implemented; Windows/Linux measurements recorded. | Real mobile OS low-memory callbacks, device/provider process-memory budgets, thermal/loading/cancellation behavior and device breadth. |
| AUD-04 — Partial | Center routing, HRTF/stereo fallback, live layout changes and mod wet-send rolloff measured with loopback audio; real-device harness passes on real stereo endpoints, including a forced layout and a mid-line device switch. | Run `audio_surround_endpoint.py` on a real 5.1/7.1 endpoint and listen on speakers; preserve positional mix and stock zero-rolloff behavior. |
| AUD-05 — Partial | Consent/PTT/capture pipeline, codec/transport/moderation and bounded playback pass multi-process Windows/Linux and clock/loss tests. | Physical microphone/capture hotplug, independent-machine clocks and OS/device breadth. Existing 1:59:29 run is not a two-hour pass; cancelled rerun is not silently reinstated. |
| AUD-06 — Partial | Localized dialogue gain, complete installed-reference routing audit, extensive scripted-scene mute/save checks and a 42-map progression sweep with fights (1,052 kills, no mute leak) exist. | Ordered human play, including `walker` past the step where the probe player dies, and physical listening/device checks; retain effects/music separation. |
| AUD-07 — Partial | Implementation complete: playback-driven captions, fallback/mapping policy, authored caption-pack declarations, an optional marker for untranscribed speech, a reading hold after a line ends, portrait overlap, real save/load checks and the fight-gated scene corpus (1,546 distinct lines across retained reports, no untranscribed cue). | Qualification only: ordered human play and physical stereo-HUD readability. Installed text is English only. Lines without a lip-sync transcript show their shader's script line, and helmet-radio copies their dry line's transcript (#221); only non-verbal sounds and multiplayer announcers show the marker unless a caption pack supplies text. |

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
  November 13: the listening pass becomes a measured comparison of what each
  game renders. `sound_distance_retail_compare.py` runs retail 1.4.2 and openQ4
  on the same three shaders, nine distances and two passes, and writes each mix
  to a WAV through OpenAL Soft. Nothing plays aloud. openQ4 must follow retail and
  the authored curve within 1 dB beyond the minimum distance (2 dB inside it, where
  retail's OpenAL path keeps a point source), and both must be silent at and beyond
  the maximum. Retail's OpenAL output is a fair reference: 1.4.2 sets rolloff 0 and
  sends its shared channel volume as the gain. The listener sits in Hangar 1's
  portal-sky room, with the four map sounds that reach it removed. The openQ4 half
  passes (within 0.6 dB, silent at and beyond the maximum). Retail selects no sound
  world while it is not the active application, and faking focus would be
  synthetic input. The retail half therefore waits for a person to focus its window,
  and it has not run focused yet. Close this item when it passes.
  [Rendered comparison](audio-modernization.md#rendered-retail-comparison-13-november-2026).
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
  November 23: one block per job refilled one queue buffer per frame, so frames
  over a third of a second starved streams, and a stream restarted after an
  output change underran at once; the C2 qualification run, on a saturated
  machine, failed `audio_output_recovery_runtime.py` that way. A stream now
  decodes three blocks ahead and a frame refills every played-out buffer; the
  harness takes scene time at the probe, measures loudness over the audible span
  and fails runs whose frames lag (`frames_kept_pace`). 0.15.0 and this change
  pass every check on a quiet machine; at 2 fps only this change keeps streams
  playing.
  [Evidence](audio-voice-evidence.md#streams-across-long-frames-and-output-changes-23-november-2026).
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
  November 13: `audio_surround_endpoint.py` qualifies placement and transitions
  on real output devices: stock gameplay through WASAPI (or PulseAudio/PipeWire),
  measured from each device's own loopback in its own layout, with no wave
  writer. On two real stereo endpoints all 19 checks pass. The automatic layout
  matches the device, dialogue stays balanced and positional sound follows its
  side. A forced 5.1 that the stereo device refused leaves the line un-routed
  and audible. A switch between devices mid-line reopens in place, silences the
  old device and plays on the new one within 40 ms of the scene clock. The
  centre branch checks exclusive centre dialogue and a silent LFE, but no
  centre-capable device was available, so it ran only on synthetic data. The
  remaining gate is one run of that harness on a 5.1/7.1 endpoint, plus
  listening on speakers; real endpoint loss and hotplug stay with AUD-02.
  [Real-endpoint evidence](audio-voice-evidence.md#real-playback-endpoints-13-november-2026).
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
  November 13: implementation complete. The inventory confirms the installed
  assets have no more text to map: every stock lipSync transcript is in use, and
  Awakening's 45 unused ones belong to race samples no shader plays. The
  licensed authored mapping is therefore a caption-pack declaration, a lipSync
  named `subtitle/<sample path>` or, for a single-sample shader,
  `subtitle/<shader>`, checked before installed text and falling back to it.
  Packs carry their own localized text and leave lip sync alone; the repository
  ships none, and `audio_asset_inventory.py --pack-template` lists the 1,419
  stock and 72 Awakening gaps as empty entries. Missing coverage is explicit to
  players: `ui_subtitleUntranscribed` (default on, Settings > Audio in both UI
  modes, twelve languages) marks speech without a transcript in any slot the
  transcribed lines leave. A line that ends naturally holds its last words
  0.5 s, and a short line 1.5 s in all, closing the logical-clock tail the fight
  sweep found; stops and replacements still clear at once. Native selection
  tests and the stock, Awakening portrait Turkish and Classic runtime harnesses
  pass. Ordered human play and physical stereo-HUD placement remain the only
  open AUD-07 work.
  [Caption-pack evidence](audio-voice-evidence.md#caption-packs-untranscribed-speech-and-held-captions-13-november-2026).
  November 17 (#221): the 13 November inventory missed two installed sources.
  Every stock voice shader without lip-sync text carries its script line as its
  description, and every one of the 692 helmet-radio `_h` copies replays a dry
  recording that has a transcript. Lines without lip-sync text of their own now
  take the dry transcript, then the cleaned script line (`source=script`),
  after caption packs and installed text. 1,329 of the 1,340 campaign gaps,
  including all 249 untranscribed radio lines, now show text; the eleven left
  are screams, laughter and pain, and the 64 multiplayer announcers stay
  uncaptioned.
  [Script-line evidence](audio-voice-evidence.md#script-lines-and-helmet-radio-captions-17-november-2026).

## Controller experience

Sources: [input settings](../user/input-settings.md), [Steam Deck guide](../user/steam-deck.md),
[Deck QA](steam-deck-qa.md), [input key matrix](input-key-matrix.md),
[input routing](ui/input-routing.md), `src/sys/sdl3/sdl3_backend.cpp`,
`src/framework/UsercmdGen.cpp`, `src/sys/sys_local.cpp` (rumble mixing) and
`content/baseoq4/pak0/default.cfg`.

Reviewed 21 November 2026 from the point of view of a player who uses only a
controller. The SDL3 layer is broad: HIDAPI drivers for Xbox, PlayStation,
Switch, Steam and Deck controllers, a stable `JOY1`-`JOY32` button mapping,
hotplug, a radial dead zone with move and look curves, Southpaw, trigger
hysteresis, the retained menus' full controller navigation, per-family button
prompts with Confirm/Back swapping, the weapon and quick-chat wheels, motor
rumble with strength and low-battery caps, gyro and touchpad input, the Steam
Deck profile and a "Controller disconnected" notice in the menus. What it lacks
is mostly what makes a pad feel first-class in a modern shooter: the right
controller taking charge, stick aim that reaches full speed and turns quickly,
assistance, gyro that does not borrow the mouse settings, typing without a
keyboard, a complete default layout and setup tools, and evidence from real
controllers. Every item honors five rules:

1. **Mouse and keyboard unchanged.** Nothing here changes mouse or keyboard
   input, their settings or their bindings.
2. **Existing profiles keep their feel.** New defaults reach fresh profiles
   only. Archived values and binds are never rewritten except by an explicit
   preset, reset or a migration that recognizes untouched shipped values.
3. **One look path.** The usercmd and the HF-03 late-look presentation use one
   stick and gyro transform, so the drawn view never disagrees with the aim.
4. **Contained and localized.** SDL3 stays the platform layer and game modules
   see input only through usercmds and the engine interface. New text uses
   `#str_*` entries in every language table; new rows follow the
   [UI visual design](ui-visual-design.md) and the settings-row checklist, and
   appear in the stock menus wherever those already have the setting's page.
5. **Asset-free tests first.** SDL3 virtual joysticks (`SDL_AttachVirtualJoystick`,
   with sensors and touchpads) drive contracts and gameplay harnesses without
   hardware; physical acceptance is PAD-09 and is never inferred from them.

### PAD completion checkpoints

**Estimate: 75%; 0/10 tasks closed.** The 22 November 2026 increment built
PAD-01 through PAD-08 and qualified each with a native test and a
virtual-controller run in the client
([technical contract](controller-experience.md),
[release record](release-completion.md#controllers-the-one-in-your-hands-aim-in-degrees-gyro-layouts-and-haptics-22-november-2026)).
No task is closed: each still owes evidence only a person or a multiplayer
session can give, and PAD-10 is a decision.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PAD-01 — Partial | Every controller open with the active one chosen by use, release on handover, a saved device choice, pause on loss in single-player, the multiplayer HUD notice and the two battery notices; `ControllerRoster` policy test and the `handover`, `pause` and `battery` virtual-controller runs pass. | A multiplayer capture of the HUD notice; a symbol for the battery notices; the stock-menu device row; physical runs under PAD-09. |
| PAD-02 — Partial | Per-stick inner and outer dead zones with an axial option, speeds in degrees per second, turn boost, walk-independent turning, three presets with Custom, and one transform for the usercmd and the late look; existing profiles carried over; measured 157, 90 and 357 degrees per second in the `turnrate` run. | The feel review on two controller families, which decides the fresh-profile preset; stock-menu rows. |
| PAD-03 — Partial | [Decision recorded](controller-experience.md#pad-03-aim-assistance); slowdown and follow for sticks only in the campaign and Arena, world-screen steadying, the server cap `si_aimAssist` and no assistance under competitive rulesets; policy test and the `assist` run pass (allies ignored, 0.45 and 0.75 on a live enemy, nothing when Off). | Campaign and Arena feel runs, which decide the default (Off for now); the cap shown in the server details and browser; a multiplayer run of the cap; follow measured against a moving target. |
| PAD-04 — Partial | Gyro in its own units with no mouse setting touching it, Yaw, Roll and Player Space, hold-still and at-rest calibration stored per controller, activation with the ratchet, tightening and smoothing, the menu pointer and rows; math test and the `gyro` run pass (90 degrees for a quarter turn whatever the mouse settings). | The direction check and review on DualSense, Switch Pro and Deck; stock-menu rows. |
| PAD-05 — Partial | Retained text and number fields show openQ4's own on-screen keyboard (`guis/menu/keyboard.rui`) when their edit opens by controller or touch and SDL reports no system keyboard (`ui_onScreenKeyboard`): five layers covering every shipped language's letters, shift, caret arrows, DELETE, SPACE and DONE, each also on a controller button drawn on its key, operable by controller, pointer and touch; the panel stands clear of the field. Evidence: `openq4-on-screen-keyboard` (layouts), `openq4-ui-on-screen-keyboard` (document), `controller_runtime.py --scenario keyboard` (a save named with a capital, an accented and a Cyrillic letter, and saved by DONE). | The harness's other two cases (a player name, a password-protected local server); device buttons and touch on the keys; captures in each language; Deck game mode still showing Steam's keyboard; field defaults that save typing (recent server addresses); the stock chat line and console, which stay keyboard-only; UI register acceptance (WID-022 and INP-012 are partial). |
| PAD-06 — Partial | Context buttons for votes, Ready, Spectate, chat, team chat, the buy menu, voice chat and quick save and load, with prompts; four layouts with Custom; Reset Controller and Reset Keyboard and Mouse; rows for Toggle Crouch, Always Run and Move Curve; the View button follows its binding in play; the `layout` run passes. | Hints on the held scoreboard for its buttons; multiplayer harness runs of the vote and match buttons; the D-pad weapon direction decision (shared with VR); Toggle Run and hold-or-toggle zoom rows; stock-menu rows. |
| PAD-07 — Partial | Settings > Controls > Controller > Test Controller opens CONTROLLER TEST: both sticks with their dead zones and outer limits drawn, trigger travel against Trigger Press, the buttons lit in the active family's glyphs, the gyro's trace and calibration state, name, power and battery, a rumble test that runs each motor present in turn, and a Steam Input line. A family's first connection raises two notices naming Prompt Style and Swap Confirm and Back. Evidence: `openq4-ui-retained-controls`; `controller_runtime.py --scenario testpanel` (a virtual DualSense's stick, trigger and button reaching the panel, the four motors in order). | Captures of each family's glyph set and of each language; the Steam Input line with Steam running; UI register rows beyond the interface record; a physical controller. Back closes the panel, so it cannot be shown held. |
| PAD-08 — Partial | Trigger motors on the firing trigger, DualSense trigger feel by weapon, the light for team or health, player lights, side-biased hits, a switch for each and capability gating; the `feedback` run passes on virtual DualSense and Xbox pads. | The #244 reporter's retest; physical DualSense and Xbox review under PAD-09; stock-menu rows. |
| PAD-09 — Partial | The [controller qualification checklist](controller-qualification.md) is written; no physical controller run is recorded. | One checklist run per family and platform through menus, SP and MP play. |
| PAD-10 — Not started (optional) | A [recommendation](controller-experience.md#pad-10-recommendation-not-yet-decided) is recorded; nothing is decided or built. | Decide flick stick, co-pilot and HD haptics before any implementation. |

### PAD task evidence and acceptance

- [ ] **PAD-01 — P1, implementation: the controller in your hands is the one in
  charge.** Today `SDL3_OpenFirstController` opens the first gamepad, and a
  gamepad that connects while one is open is ignored until the first is
  removed; only a plain joystick yields to a new gamepad. A Steam Deck with an
  external pad, a wired and a Bluetooth pad, or a remapper (Steam Input,
  DS4Windows, reWASD) presenting a virtual pad beside the physical one can leave
  the wrong device active, so the player's presses do nothing or the prompts
  show the wrong buttons, and turning `in_joystick` off and on is the only way
  out. Deliver: open every gamepad and make the active one the last to press a
  button or move a stick past its dead zone; prompts, the glyph family,
  Confirm/Back, rumble, gyro, touchpad and battery follow it, and the outgoing
  device's held buttons and axes release on handover. Add an archived device
  choice (Automatic, or a saved device chosen from the connected list) as a
  Controller row, and list every device in `listControllers` with the active
  one marked. This device layer is the one MP-25's seats build on, not a
  second implementation. When the active controller goes away during
  single-player gameplay and no other controller takes over, open the pause
  menu with the existing notice; reconnecting never resumes play by itself.
  In multiplayer, show the notice on the HUD and do not pause. Show one notice
  when the active controller's battery reaches the low-battery threshold
  (SDL's battery events) and another when it starts charging. Gates: a virtual
  joystick contract with two pads (connect, use-to-switch, release on handover,
  loss of the inactive and the active pad, reconnect keeping the choice); an SP
  gameplay harness pausing on loss; an MP HUD notice capture; a battery notice
  from the virtual device's power state; physical runs under PAD-09.
- [ ] **PAD-02 — P1, implementation: stick aim that reaches full speed and
  turns quickly.** Today both sticks share one radial inner dead zone (`0.18`)
  with no outer dead zone, so a worn stick that never reports full deflection
  never reaches full speed. One look sensitivity covers both axes. Full-stick
  turning is `in_yawspeed` × Look Sensitivity × the run scale, about 158°/s with
  defaults and always-run, and clicking the stick to walk drops it to about
  105°/s because the run state (`in_anglespeedkey`) scales stick turning as it
  does the keyboard turn keys. Without turn acceleration a 180° turn takes more
  than a second. Deliver: inner and outer dead zones per stick (radial, with an
  axial option for movement); horizontal and vertical look speed shown in
  degrees per second; an optional turn boost that adds yaw after the stick
  holds near full deflection for a short ramp, off while zoomed; stick turning
  independent of walk and run (the keyboard turn keys keep retail scaling); and
  response presets (Classic, today's values; Precise; Dynamic) that set curve,
  dead zones and boost together and show Custom after any edit. Zoom scaling
  (`pm_zoomedSlow`) and the wheels' routing stay. `JoystickMove` and the late-look
  path in `UsercmdGen.cpp` repeat the same look arithmetic today; replace both
  with one function. Classic stays for existing profiles; whether fresh profiles
  get another preset is decided after a human feel review. Gates: asset-free
  transform tests (dead zones, outer clamp, boost ramp, walk independence,
  late look equal to the usercmd); a virtual-joystick turn-rate measurement in
  SP gameplay; a feel review on two controller families.
- [ ] **PAD-03 — P1, decision and implementation: aim assistance for
  controllers.** Raven's Xbox assistance (`pm_AimAssist*`: a target frustum,
  turn slowdown over an enemy and a muzzle bend) compiles only under
  `_XENON`/`_XBOX`, so PC controller players have none, while the archived
  `pm_AimAssist` (default `2`) does nothing. Decide first: reuse or replace that
  code; which forms (slowdown near a target, light pull while the stick moves,
  never a snap to the target); which inputs receive it (sticks; gyro off by
  default; never the mouse); how the input source reaches the game so saves,
  demos and the server see the same decision (a usercmd flag is a protocol and
  demo change under MP-08 and MP-21); and the multiplayer policy. Deliver in
  single-player and SP Arena: Off, Low, Standard and High on the Controller
  tab; applied to live enemies in range and in sight, never allies, corpses or
  scripted actors; and slowdown over interactive world-GUI controls while the
  crosshair is the cursor, for Quake 4's terminals and lift panels. In
  multiplayer: a server setting (off, or allowed up to a cap), off in the
  competitive rulesets, shown in the server details and the browser, and
  unaffected bots. Gates: the recorded decision; game-side contracts (no effect
  from mouse or gyro, strength bounds, nothing through walls); campaign and
  Arena runs with a controller harness; the server cap replicated and enforced;
  existing demos playing back unchanged.
- [ ] **PAD-04 — P2, implementation: gyro aiming on its own terms.** Today gyro
  motion becomes mouse counts and joins the mouse path, so Mouse Sensitivity,
  Mouse CPI, Smooth Mouse, View Filter and mouse acceleration all change gyro
  aim. It reads one sensor axis for turning (local yaw, which feels wrong when
  the controller is tilted), has no calibration for controllers SDL does not
  calibrate, and cannot be paused (ratchet) or limited to zooming. Inversion and
  axes are console-only, and gyro does not move the menu pointer. Deliver: gyro
  input into the shared look transform with its own sensitivity in real-world
  terms (degrees of view per degree of turn, optionally separate vertically),
  independent of every mouse setting; Yaw, Roll and Player Space turning; a
  hold-still calibration stored per controller, with slow bias correction at
  rest; activation choices (always; off while a bound button is held, or on
  only while held; while zoomed) and a `_gyro` binding; small-motion smoothing
  and tightening below a threshold; rows for all of it; and an optional gyro
  menu pointer. Migrate the Deck profile's values to the new units only where
  they are still the shipped ones. Gates: sensor traces replayed through SDL
  virtual sensors giving the expected view change while the mouse settings vary;
  calibration removing an injected bias; activation and ratchet tests; review on
  DualSense, Switch Pro and Deck.
- [ ] **PAD-05 — P1, implementation: typing with only a controller.** Text
  fields (save names, player name, chat, the server address, password and
  filter, Create Server's name, the console) take text only from a keyboard or
  a system keyboard where SDL shows one: Deck game mode through Steam, Android
  and touch tablets. A desktop player on a couch, or any non-Steam install,
  cannot name a save or join a password-protected server. STEAM-05 covers
  Steam's keyboard and waits on STEAM-01. Deliver: a retained on-screen keyboard
  (vector art to the UI specification, controller and touch operation, layouts
  covering every shipped language's characters, shift, accents and symbols,
  with face-button shortcuts for backspace and space), shown when a text field
  takes focus by controller and `SDL_HasScreenKeyboardSupport` reports no
  system keyboard; and field defaults that save typing (a prefilled save name,
  recent server addresses). STEAM-05 later prefers Steam's keyboard where it
  reaches it. The stock interface either uses the same keyboard or is
  documented as keyboard-only. Gates: UI register rows; a harness naming a
  save, setting a player name and joining a password-protected local server
  with controller events only; captures in each shipped language; Deck game
  mode still showing Steam's keyboard.
- [ ] **PAD-06 — P1, implementation: a complete layout and settings.** Today the
  default controller layout gives multiplayer Vote Yes and No, Ready, chat, team
  chat, voice chat, the buy menu, spectating and quick save and load no button
  (they sit on `F1`-`F9`, `T`, `Y` and `B`), so the vote notice and Ready prompt
  name keys a pad player cannot press. Scores and objectives take three inputs
  (View, D-pad up and the touchpad), and D-pad right selects the previous weapon.
  Toggle Crouch, Toggle Run, Always Run, Move Curve and the gyro inversion and
  axes are console settings only, and Load Defaults resets keyboard and
  controller bindings together. Deliver: context actions that put those
  commands on the controller without spending buttons permanently (for example
  the D-pad votes while a vote is open, Ready and Spectate on the held
  scoreboard, and voice chat on a free input in multiplayer), with prompts
  naming those buttons; a decision on D-pad weapon direction for fresh
  profiles; layout presets on the Controller tab (Classic, today's layout, and
  at least one bumper-jump and one one-handed accessibility layout), applied
  only when chosen; Reset Controller and Reset Keyboard and Mouse beside Load
  Defaults; and rows for the console-only settings above, with hold or toggle
  choices for zoom, crouch and run. Gates: fresh-profile binding contracts;
  existing configs left untouched; votes, Ready, buying, chat and quick
  save/load reached with controller events in SP and MP harnesses; localized
  rows in every shipped language.
- [ ] **PAD-07 — P2, implementation: controller setup and test panel.** Add a
  Test panel to Controls > Controller: both sticks with their dead zones and
  outer limits drawn, trigger travel against Trigger Press, buttons lit as the
  active family's glyphs, the gyro trace and calibration state, a rumble test
  for each motor present, battery and connection, matching the Android setup's
  live look-speed test. The first connection of a controller family shows a
  one-time notice naming Prompt Style and Swap Confirm and Back. When SDL
  reports a Steam Input virtual pad (`SDL_GetGamepadSteamHandle`) and the gyro
  or touchpad is not reaching the game, say so and point to the Steam Deck
  guide. Gates: UI register rows; virtual-joystick state reaching the panel;
  captures of each family's glyph set.
- [ ] **PAD-08 — P2, implementation: haptics and lights.** Motor rumble comes
  from screen-shaking sounds plus, in the development tree, per-shot pulses,
  the lightning gun and gauntlet hum and damage thumps (#244), mixed in
  `sys_local.cpp` under the Rumble, strength and low-battery settings. Add, where
  SDL reports the capability: Xbox trigger motors (`SDL_RumbleGamepadTriggers`)
  for firing; DualSense adaptive trigger resistance per weapon through
  `SDL_SendGamepadEffect`; lightbar or LED colour (team colour in multiplayer,
  a health or fixed colour in single-player, `SDL_SetGamepadLED`); player-number
  LEDs (`SDL_SetGamepadPlayerIndex`, shared with MP-25's seats); and left/right
  motor balance from the direction of damage. Each has its own switch under the
  existing Rumble master; menus, pause, suspend and loss stop everything.
  Gates: capability-gated contracts (devices without the feature unchanged); a
  virtual device receiving the expected calls; the #244 reporter's retest on an
  Elite Series 2; physical DualSense and Xbox review under PAD-09.
- [ ] **PAD-09 — P1, qualification: controller families on real hardware.**
  Write one controller checklist in `docs/dev/` that UI-02, the platform signoffs,
  AND-02 and the multiplayer human checks cite instead of repeating. Families:
  Xbox Series and One (USB, Bluetooth, wireless adapter) and Elite paddles,
  DualShock 4, DualSense and Edge, Switch Pro and a Joy-Con pair, a generic
  XInput pad, Steam Deck controls, a Steam Input virtual pad and one flight
  stick on the raw-joystick path. Platforms: Windows x64, Linux x64, Deck game
  and desktop modes, and the macOS preview. For each: detection, hotplug and
  handover, prompts and Confirm/Back, menus with `ui_retained 1` and `0`,
  rumble, triggers and lights, gyro and touchpad, and battery; then SP play
  through combat, a hovertank and a walker section, world terminals, the weapon
  wheel and naming a save, an MP match with voting, Ready, chat, buying and the
  quick-chat wheel, and SP Arena. Bind each record to its build per GOV-03.
  Close when each first-class platform has a recorded pass for the Xbox,
  PlayStation and Nintendo or Deck families and every failure links to an
  issue or task.
- [ ] **PAD-10 — P3, decision: advanced and assistive input.** Decide whether to
  offer flick stick (the right stick points the view's direction, then turns
  with rotation), co-pilot play (two controllers driving one player, for
  assisted play) and HD haptics on DualSense and Switch controllers, each with
  its dependencies on PAD-02, PAD-04 and PAD-08. No implementation is credited
  before the decision.

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

**Estimate: 92%; 4/6 tasks closed (16 November 2026).** PKG-01 to PKG-04 are
implemented and proven on local candidates; their release-workflow steps first
run with the next release. PKG-05 needs that release's measurements; PKG-06's
updater is implemented (#227), its key generated and the signing secret set;
it waits for an in-place update between two signed releases. Evidence:
[implementation status](content-pack-distribution.md#implementation-status-6-november-2026),
[updater](content-pack-distribution.md#implementation-16-november-2026).

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| PKG-01 — Complete | Rootless per-platform update archives, Windows `-symbols.zip`, `content-packs.json`, release-note pack status, localized missing/mismatched-pack report (12 languages), patch releases refuse bulk changes; `verify-update` and `verify-identity` proved on consecutive Windows candidates and wired into the release job. | None. |
| PKG-02 — Complete | `content_packs.json` drives engine, Meson, packagers, installer, verifiers and Android; pak0/pak1/pak2 split; packs built once per release and reused by source digest; `-text` checkout makes Windows and Linux packs identical; client-only packs `PURE_NEVER` and optional on servers; retired packs skipped and deleted. | None. |
| PKG-03 — Complete | Software DXT decode for renderers without S3TC; 51/51 packs accepted with every area resident on GL, GL software decode and Vulkan; atlases and `core2` metadata trimmed (pak1 267.5 → 96.4 MB); `waste` re-baked; loadscreen format decided: keep TGA + DDS. | None. |
| PKG-04 — Complete | Content generations, overlays pak3–pak9 rebuilt from changed files with the base kept byte-identical from the generation's blobs, size budget, generation bump folding; one-file fix shipped as a verified 153 KB overlay. | None. |
| PKG-05 — Implemented, measurement open | Installer downloads only missing or different bulk packs, SHA-256 and release URL compiled in; AppImages embed `gh-releases-zsync` information and publish `.zsync`; itch.io declined. | Measure update downloads for each channel on the next release (maintainer decision, 6 November). |
| PKG-06 — Implemented, first in-place update open | In-game updater under the recorded constraints (#227): Ed25519-signed `openq4-update.json` with the key compiled in, compiled release URL, WinHTTP / system libcurl, staged `.update/` swapped by `openQ4-updater` after exit with backup and rollback, per-user installs only; macOS, AppImage, Flatpak, Snap, Android and development builds get the release page. Native, helper and manifest-tool tests. | Key generated and `OPENQ4_UPDATE_SIGNING_KEY` set (16 November). Update the first signed release in place to the next one on Windows and Linux (only builds carrying the key trust it). |

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
  whole-APK Android). Bulk sources stay in Git with this decision. Implemented
  16 November 2026 for #227 ([design](content-pack-distribution.md#implementation-16-november-2026)):
  the Updates button and a startup check in release builds read the latest
  release's signed manifest; Windows and Linux installs download, verify, stage
  and hand over to `openQ4-updater`, which swaps the files after the game exits
  and restarts it. The key was generated and its workflow secret set on
  16 November. Remaining exit: updating the first signed release in place to
  the next one on Windows and Linux, since only builds carrying the key trust
  its signatures.

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
| OPT-03 | Sparse/virtual textures, broad GPU-driven/async-compute work, background shader compilation and raster world-space GI: require measured need, target content/hardware and classic rollback. Ray-traced shadows, reflections, indirect light and path tracing moved to RT-01 through RT-07 on 15 November 2026; shadowed volumetrics moved to REN-09, and HDR10/scRGB output to VK-08, on 21 November 2026. |
| OPT-04 | Live relay/delayed broadcast, multi-POV/multi-window audio and web/brackets: separate security, capacity, backpressure and reconnect designs. Forced client screenshots remain rejected. Demo compression and export moved to MP-23/MP-22 and persistent identity to MP-17 on 15 November 2026. |
| OPT-05 | Overload/Harvester/Domination/Attack and Defend: keep unavailable until authoritative objectives/scoring, networking, bots and UI exist. Race remains outside accepted scope; rulesets, modifiers and presets moved to MP-11/MP-14 on 15 November 2026. |
| OPT-06 | Awakening's unreachable grapple/concussion/tether/gravgun/compass content: explicit content-completion project, not a blocker for the integrated SP campaign. No expansion multiplayer module. |
| OPT-07 | HDR audio, ducking, geometric obstruction and reverb editing: optional authored-mix changes after retail audio qualification. |
| OPT-08 | Additional idTech titles, wholesale SP/MP source unification, broad third-party feature ports and extra cosmetic color syntax: require a product/maintenance decision, source/license review and concrete benefit; the old TODO is not an implementation mandate. |
| OPT-09 | Further Steam services beyond STEAM-02 through STEAM-05: lobbies and matchmaking, Steam relay/NAT traversal, Workshop mod distribution, Steam Cloud saves, achievements and stats, and Steam avatars in scoreboards (which needs a Steam ID exchange and its own privacy design). Each depends on the STEAM-01 app identity, and several need app-owner configuration. |

## Release qualification and completion rules

### REL completion checkpoints

**Estimate: 55%; 0/4 tasks closed.** Since 23 November 2026 one frozen package per
candidate runs the matrix in [release qualification](release-qualification.md):
`release_qualification.py` hashes every file, proves the runtime is exactly the
release archive's, binds it to its commit and re-checks it around every case,
keeping every attempt. Candidate C1 (`174cb31a4`) was rejected for losing
players' multiplayer settings; C2 (`8016bc922`) failed audio output recovery,
which exposed streams starving under slow frames (fixed in `5b8f32617`); C3
(`7fc884a87`) passes the Windows x64 matrix.
Tests on different revisions still do not combine into an accepted package, and
this update promotes no release or support tier.

| Task / state | Completed points and evidence | Remaining completion subpoints |
| --- | --- | --- |
| REL-01 — Partial | Release-configuration Windows x64 candidates built from a frozen commit, packaged and frozen. On C3 (and C2): package layout, dedicated server, dedicated and listen-server multiplayer on all 20 stock MP maps (GL and Vulkan), all 29 stock SP maps on both renderers (in each Vulkan sweep one renderer's `mcc_2` capture fell in the map's scripted opening fades), the 31-stage campaign chain, campaign saves, Quake 4/Awakening switching, SP save/load and demo, pack reports, localization, a clean install and an upgrade over a 0.15.0 profile and save. C1's upgrade run found three settings defects, also in 0.15.0, fixed in `8016bc922`. | The same matrix on Linux x64/Steam Deck, macOS, Linux and Windows ARM64 and Android packages; multiplayer demos (MVD) in the matrix. |
| REL-02 — Partial | Engine-capture and diagnostic infrastructure, scoped visual/audio evidence and human-review procedures exist. C2 and C3 each wrote 49 contact sheets of every stock map's opening view on Vulkan beside OpenGL at the same viewpoint; the runner lists each review and records the reviewer's verdict (`record-human`). | The reviews themselves: matched visual acceptance from those sheets, actual motion/latency, audible sound, accessibility and VR comfort on physical devices with required permissions. |
| REL-03 — Partial | On C3 (and C2): cold and warm starts, start after generated-cache damage, resize and renderer restarts, a Vulkan relaunch, pause, slow motion, map change and clean shutdown; GL and Vulkan soaks of 16 stops with a `vid_restart` (process memory within 1.14 times); network loss and reconnect, IPv6; Vulkan startup recovery; effects across map changes and restores; audio output interruption, layout changes and the low-memory trim (C2 failed it: streams decoded on job workers refilled one buffer a frame and underran under slow frames, since 0.14.1; fixed in `5b8f32617`, C3 passes all 34 checks). C3's first OpenGL soak was ended by two machine-wide GPU resets (Windows LiveKernelEvent 141) from another process: the OpenGL client has no context-reset handling and exits without a message. | OpenGL recovery from a GPU reset (no `ARB_robustness` handling); device and surface loss, an unplugged audio device, background/suspend and high-DPI on real hardware, and every other platform. |
| REL-04 — Partial | Withdrawal policy, release-status page, curated unreleased material and support/promotion validators exist. Package checks run per candidate: provenance, exact archive, layout, symbols (C1 found the renderer and updater PDBs missing from the Windows symbols archive, fixed in `be420c1ad`) and signing (every Windows image unsigned). Upgrade notes cover the settings fix. | A signed Windows package or a stated decision against one; the promotion decision for the proven subset, with the support and capability matrices updated with it. |

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
  Progress, 23 November: the [release qualification](release-qualification.md)
  runner and `release_profile_runtime.py` run this matrix on one frozen package.
  Windows x64 candidate C1 lost players' settings (multiplayer-only settings
  dropped by `seta`'s missing archive flag, the multiplayer menu's 21 settings
  declared after the configs, the Failed Transfer default model; fixed in
  `8016bc922`); C2 and C3 pass. Other platforms are not run.
- [ ] **REL-02 — P1, qualification: visual and human evidence.** Use engine
  `screenshot` render-target captures and matched viewpoints/time. Perform actual
  motion, input latency, audible sound, accessibility and VR comfort checks on
  appropriate devices. Default to windowed tests. Fullscreen and input control
  require the project's specific prior user permission; never substitute OS
  screen capture or silently infer permission from a historical test plan.
  Progress, 23 November: C2's and C3's sweeps wrote matched Vulkan/OpenGL
  contact sheets of every stock map; the human reviews are listed in the matrix
  and not done.
- [ ] **REL-03 — P1, qualification: lifecycle and failure matrix.** Cover cold/warm
  starts, assets/cache failures, long sessions, map/campaign/mode changes, network
  loss/reconnect, saves/demos, device/surface/audio loss, resize/high DPI, pause/
  slow motion, background/suspend, shutdown and renderer/module recovery. Verify
  bounded resources, fallback and user-visible error behavior without lowering
  gates to turn historical failures green.
  Progress, 23 November: C3 passes the software lifecycle, soak, network,
  audio and renderer-recovery cases on Windows x64 (C2's audio failure exposed
  stream starvation, fixed in `5b8f32617`). A machine-wide GPU reset ended the
  OpenGL client without a message: GL has no context-reset recovery.
  Real-hardware loss, suspend and high-DPI remain.
- [ ] **REL-04 — P1, documentation and promotion: close only proven scope.** Keep
  published downloads distinct from current-source features. Record withdrawals
  in the shared release-status policy, player status page and historical notes;
  exclude them from version/changelog baselines without deleting evidence.
  Reconciliation on 23 October 2026 restores 0.13.2 as latest and marks 0.14.0
  withdrawn; 0.14.1 replaced 0.13.2 as the latest release in November 2026, and 0.15.0 replaced
  0.14.1 later that month. Update
  this roadmap, UI acceptance registers, support/capability matrices and curated
  release notes together. State hardware/backend exclusions, upgrade/reinstall
  requirements and opt-in defaults. Use the existing renderer promotion token,
  platform evidence validators, signing/symbol/licensing/package checks. A scoped
  local implementation pass, synthetic device or archived branch is not product
  acceptance. Record a deliberate exclusion when a feature will remain deferred.
  Progress, 23 November: candidates carry provenance, exact-archive, layout,
  symbols and signing checks; the symbols gap is fixed (`be420c1ad`) and C3's
  seven PDBs match their images. Windows packages are unsigned. Promotion is the
  project owner's decision.

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
