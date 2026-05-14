# Renderer Validation Matrix

This matrix is the validation source of truth for the staged GL renderer work. It separates safe automated startup/self-test coverage from gameplay smoke coverage that must be run manually with the mode-specific SP/MP launch tasks.

## Build And Stage

Use the project wrapper:

```powershell
tools\build\meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback
tools\build\meson_setup.ps1 compile -C builddir
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

For incremental validation after an existing setup:

```powershell
tools\build\meson_setup.ps1 compile -C builddir -- -j1
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

## Automated Safe Matrix

The safe matrix starts the staged client, runs renderer self-tests or startup probes, prints `gfxInfo`, then quits. It does not launch maps.

```powershell
python tools\tests\renderer_validation_matrix.py
```

The runner writes a timestamped report under `.tmp/renderer-validation/` with per-case logs and a JSON copy for CI or release triage.

Automated coverage:

| Case | Coverage |
|---|---|
| `renderer-foundation-selftests` | context ladder, tier selector, upload manager, GPU timer, scene packet, render graph, render graph resource owner, material resource table, geometry/instance resource records, GL state cache, Shader Library V2 pass-family/permutation/reflection coverage, draw plan, submit plan, modern executor, and shadow planner self-tests |
| `renderer-visible-depth-selftest` | opt-in `r_rendererModernVisibleDepth` coverage for graph-backed scene depth, compatible shadow-depth resources, fallback accounting, depth-overlay readiness, and `gfxInfo` reporting |
| `renderer-gbuffer-selftest` | opt-in `r_rendererModernOpaque` coverage for graph-backed G-buffer resources, MRT setup, opaque/alpha-test draw classification, diffuse texture binding, packing assumptions, fallback accounting, bandwidth metrics, attachment debug-overlay readiness, and `gfxInfo` reporting |
| `renderer-cluster-grid-selftest` | opt-in modern clustered-light preparation coverage for point/projected/fog/ambient/special light classification, budgeted dynamic grid slicing, cluster reference packing, spill/overflow accounting, GL 3.3 UBO fallback readiness, GL 4.3+ SSBO upload readiness, cluster debug-overlay texture generation, and `gfxInfo` reporting |
| `renderer-shadow-planner-selftest` | modern shadow planner coverage for projected/point/CSM policy, mapped/stencil-fallback/skipped accounting, benchmark-budgeted shadow resolution/light/pixel caps, render-graph shadow resource reporting, clustered shadow descriptor integration, and `gfxInfo` reporting |
| `renderer-deferred-resolve-selftest` | opt-in `r_rendererModernDeferred` coverage for graph-backed deferred resolve output, G-buffer/depth/cluster buffer inputs, point/projected light accumulation, light-grid contribution, fallback accounting, deferred debug-overlay readiness, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-forward-plus-selftest` | opt-in `r_rendererForwardPlus` coverage for graph-backed scene-color/depth resources, clustered opaque/alpha-test/transparent programs, clustered-light UBO/SSBO reads, transparent sort preservation, fallback accounting, overdraw estimates, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-visible-selftest` | opt-in `r_rendererModernVisible` coverage for the guarded hybrid visible-frame bridge: graph-backed depth, G-buffer, deferred resolve, forward+ source output, graph-owned `hybridSceneColor` composition, HDR/post-process handoff before SSAO/bloom/authored post, depth-copy handoff accounting, shadow-ready handoff/fallback accounting, final GUI/present overlay, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-modern-compatibility-selftest` | Phase 14 modern-visible compatibility coverage for command-category ownership inventory, modern fullscreen GUI readiness, light-grid ownership, explicit post/copy/subview/render-demo/BSE fallback buckets, deterministic render-demo accounting, and `gfxInfo` reporting |
| `renderer-compatibility-gates-selftest` | Phase 15 fallback-gate coverage for missing UBO, broken MRT, missing timer query, missing buffer storage, rejected debug-context fallback, and synthetic driver-quirk downgrades |
| `renderer-default-promotion-selftest` | Phase 17 default-promotion coverage for `r_glTier auto`, explicit `r_renderer arb2` escape behavior, compatibility gates, modern-executor readiness, ARB2 rollback availability, and `r_rendererModernAutoPromote` sign-off control |
| `renderer-benchmark-selftest` | Phase 16 benchmark coverage for rolling P50/P95/P99 frame-time capture, CPU front-end/visibility/packet/graph/submit/present timings, GPU pass timing fields, upload/draw/light/cluster/fallback counters, benchmark presets, and performance-threshold reporting |
| `renderer-gpu-driven-selftest` | forced `r_glTier gl43` coverage for GL 4.3 SSBO submit records, compute scissor culling, clustered-bin validation, compacted indirect command generation, CPU/GPU readback comparison, masked multi-draw indirect execution, GPU timer coverage, and `gfxInfo` reporting |
| `renderer-low-overhead-selftest` | forced `r_glTier gl45` coverage for GL 4.5 DSA graph texture/FBO allocation, DSA sampler creation, named buffer/FBO updates, UBO/SSBO/texture/sampler multi-bind batches, submit-batch compaction, bindless experiment reporting, persistent upload defaults, fence diagnostics, and `gfxInfo` reporting |
| `tier-auto` | default compatibility-preserving startup and `gfxInfo` |
| `tier-legacy` | forced legacy compatibility startup and `gfxInfo` |
| `tier-gl33` | forced GL 3.3 startup and `gfxInfo` |
| `tier-gl41` | forced GL 4.1 startup and `gfxInfo` |
| `tier-gl43` | forced GL 4.3 GPU-driven tier startup and `gfxInfo` |
| `tier-gl45` | forced GL 4.5 low-overhead tier startup and `gfxInfo` |
| `tier-gl46` | forced GL 4.6 top tier startup and `gfxInfo` |
| `tier-gl33-debug-context` | debug-context request with non-debug fallback available |
| `present-vsync0-fps0` | unlocked presentation startup probe |
| `present-vsync1-fps240` | high-refresh capped presentation startup probe |
| `present-vsync1-fps30` | low-fps capped presentation startup probe |

The forced tier cases pass when startup succeeds and the selected tier is reported. If a machine cannot support the forced tier, the log must show the selected fallback tier.

The visible-depth, G-buffer, clustered-light, deferred-resolve, forward+, modern-visible, modern-compatibility, compatibility-gates, default-promotion, benchmark, GPU-driven, and low-overhead self-tests intentionally run as their own safe cases instead of being appended to the foundation self-test startup command, because the engine command parser has a fixed startup command list budget.

## Compatibility Gates

`rendererCompatibilityGatesSelfTest` is the Phase 15 fallback-gate test. It does not need a map load; it simulates the driver/capability cases that must never promote the wrong visible path:

| Gate | Expected behavior |
|---|---|
| missing UBO | GL 3.3+ modern baseline is rejected and startup falls back to the legacy compatibility tier when fixed-function compatibility exists |
| broken MRT | G-buffer/deferred ownership is blocked and the tier selector falls back below modern visible ownership |
| missing timer query | renderer GPU timers report unavailable without downgrading an otherwise valid modern tier |
| missing buffer storage | GL 4.5/4.6 low-overhead tier downgrades to the GL 4.3 GPU-driven tier while retaining SSBO/compute coverage |
| rejected debug context | the shared context ladder proves a non-debug fallback candidate exists after debug candidates |
| driver quirk table | known-bad or synthetic driver matches can mask unsafe features before tier selection so `gfxInfo` and renderer bootstrap agree |

`gfxInfo` prints both `Renderer driver quirks:` and `Renderer compatibility gates:`. The quirk line records matched rules and cap changes; the gate line records selected tier, UBO/MRT/timer/buffer-storage readiness, low-overhead readiness, debug fallback, and forced-tier support.

## Default Promotion Criteria

`r_rendererModernAutoPromote` is the Phase 17 sign-off switch for making the guarded modern visible path the automatic choice under `r_glTier auto`. Its default is `0`, so ARB2 remains the default visible renderer until the manual evidence below is complete. `gfxInfo` prints `Renderer default promotion:` with the current reason, and `rendererDefaultPromotionSelfTest` verifies the gate logic without loading a map.

| Criterion | Required evidence |
|---|---|
| tier | `r_glTier auto` selects a modern GL 3.3+ tier after driver quirks and compatibility gates are applied |
| renderer escape | `r_renderer best` leaves promotion available; explicit `r_renderer arb2` keeps the ARB2 bridge |
| compatibility gates | modern baseline features, UBOs, MRT, scene packets, render graph, and Shader Library V2 readiness are available |
| fallback escape | the ARB2 compatibility bridge remains selectable through `r_renderer arb2` and `r_glTier legacy` |
| manual sign-off | SP/MP gameplay, RenderDoc captures, and benchmark captures pass on target hardware before `r_rendererModernAutoPromote 1` is used as a default |

## Deterministic Capture Matrix

These image captures are the comparison set for scenes where deterministic output is practical. Capture paths should live under `.tmp/renderer-captures/<date>/`, and any checked-in references must be approved separately so the repo does not accumulate accidental binary churn.

| Case | Mode | Scene | Purpose |
|---|---|---|---|
| `capture-startup-mainmenu` | SP | main menu after logo skip | deterministic GUI composition, font/material atlas, and widescreen expansion |
| `capture-renderer-visible-selftest` | safe startup | `rendererModernVisibleSelfTest` | synthetic modern-visible depth/G-buffer/deferred/forward+/hybrid-scene/present composition with shadow-policy handoff |
| `capture-renderer-compatibility-selftest` | safe startup | `rendererModernCompatibilitySelfTest` | known fallback inventory for GUI/post/subview/render-demo/BSE categories |
| `capture-sp-airdefense1-static` | SP | `game/airdefense1` fixed spawn, no input for 3 seconds | outdoor lighting, terrain decals, BSE smoke, and stock material parity |

## RenderDoc Tier Checklist

Capture with `r_rendererMetrics 2`, `r_rendererGpuTimers 1` when available, and the matching forced tier. Every capture should show named debug scopes and object labels for graph resources, modern executor buffers/programs, and pass-owned FBOs.

| Forced tier | Capture focus |
|---|---|
| `r_glTier gl33` | VAO/VBO/UBO baseline, graph resources, visible-depth/G-buffer/forward+ passes |
| `r_glTier gl41` | macOS-class GLSL path and GL 4.1 context fallback behavior |
| `r_glTier gl43` | SSBO scene records, compute validation dispatch, indirect-command generation |
| `r_glTier gl45` | DSA texture/FBO updates, persistent upload defaults, and multi-bind groups |
| `r_glTier gl46` | top-tier selection plus GL SPIR-V/bindless availability reporting without default use |

## Long-Run Matrix

These are manual long-run sign-off loops. They are intentionally outside the safe runner until map startup is reliable enough to automate.

| Case | Mode | Purpose |
|---|---|---|
| `longrun-vid-restart-10x` | SP | repeat `vid_restart` ten times under `r_glTier auto`, `gl33`, and the highest supported forced tier; inspect logs after each cycle |
| `longrun-map-transition-sp` | SP | transition between `game/airdefense1`, `game/storage2`, and `game/medlabs` without restarting the process |
| `longrun-mp-listen-reconnect` | MP | `mp/q4dm1` listen server with local client connect, disconnect, reconnect, then map restart |

## Performance Regression Thresholds

`rendererBenchmarkCapture` prints a rolling benchmark line when `r_rendererMetrics` is enabled. The safe matrix records the threshold table in its Markdown and JSON reports so hardware-specific performance triage can compare the same budget shape across runs. Local threshold cvars override the preset defaults for target-machine experiments.

| Preset | P95 target | P99 target | Screen | Cluster grid | Material batch | Light batch | Shadow budget | Post budget |
|---|---:|---:|---:|---|---:|---:|---|---:|
| `low` | 33 ms | 50 ms | 75% | 4x3x8 | 32 | 16 | 512 px / every 2 frames | 0 |
| `baseline` | 20 ms | 28 ms | 100% | 6x4x12 | 64 | 32 | 1024 px / every frame | 1 |
| `modern` | 16 ms | 24 ms | 100% | 8x6x16 | 96 | 64 | 1024 px / every frame | 2 |
| `high-end` | 12 ms | 18 ms | 100% | 8x6x16 | 128 | 96 | 2048 px / every frame | 3 |

## Manual Gameplay Matrix

Gameplay validation remains mandatory before renderer release sign-off, but it is not run by the safe matrix because local map startup is currently freeze-prone. Use the SP launch task for single-player maps and the MP launch task or `tools\debug\start_listen_server_client.ps1` for multiplayer.

| Case | Mode | Map | Purpose |
|---|---|---|---|
| `sp-airdefense1` | SP | `game/airdefense1` | stock SP baseline, outdoor lighting, BSE smoke |
| `sp-airdefense2` | SP | `game/airdefense2` | flashlight, projected shadows, animated characters |
| `sp-storage2` | SP | `game/storage2` | indoor materials and post-process coverage |
| `sp-bse-heavy` | SP | `game/medlabs` | stress BSE effects without replacement content |
| `sp-cinematic-subview` | SP | `game/mcc_landing` | subviews, remote cameras, cinematic and GUI interaction |
| `mp-q4dm1-listen` | MP | `mp/q4dm1` | listen-server and local-client MP parity |

For each gameplay case, validate the matrix variants that the hardware supports:

| Dimension | Values |
|---|---|
| `r_glTier` | `auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, `gl46` |
| renderer escape | `r_renderer best`, `r_renderer arb2`, `r_glTier legacy` |
| `r_swapInterval` | `0`, `1` |
| `com_maxfps` | `30`, `240`, `0` |
| display mode | windowed, fullscreen |
| renderer diagnostics | `r_rendererMetrics 1`, `r_rendererMetrics 2`, `r_rendererModernAutoPromote 0`, and one signed `r_rendererModernAutoPromote 1` candidate run after the other rows are clean |

After each gameplay smoke, inspect the configured log file under `fs_savepath\<gameDir>\logs\openq4.log` or the case-specific log emitted by the launch tool. Fix errors and warnings, then repeat the loop until the case is clean.

## Acceptance

- Automated safe matrix passes after build and install.
- Manual gameplay matrix reaches in-game/map gameplay for every required SP/MP case on supported hardware.
- Logs are inspected after every run.
- No stock-asset compatibility overrides are added as a validation shortcut.
- RenderDoc validation remains limited to forced modern/core bring-up paths until the visible renderer no longer depends on ARB2 compatibility features.
- Benchmark captures report P50/P95/P99 frame pacing, active preset budgets, and threshold pass/fail status before any claim that the modern visible path matches or beats ARB2 on target scenes.
- `r_rendererModernAutoPromote 1` is used only after the default-promotion criteria pass; `r_renderer arb2` and `r_glTier legacy` remain documented rollback paths.
