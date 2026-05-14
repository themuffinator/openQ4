# GL Renderer Modernization

OpenQ4 now has the first GL-only foundation for the high-performance renderer redesign. This milestone does not replace the ARB2 renderer yet; it adds the capability, selection, telemetry, compatibility bridge, and opt-in modern-executor preparation path that later GL 3.3/4.x draw execution will use.

## Runtime Tiers

`r_glTier` selects the requested OpenGL tier:

- `auto` keeps the compatibility bridge safe by default while probing the full driver capability set.
- `legacy` forces the GL2-style compatibility survival path.
- `gl33`, `gl41`, `gl43`, `gl45`, and `gl46` request modern tier selection. On the SDL3 backend these forced modern values use the core-profile context ladder first.

The internal tier names are:

- `LegacyGL2Compat`
- `ModernGL33`
- `ModernGL41`
- `GpuDrivenGL43`
- `LowOverheadGL45`
- `TopGL46`
- `NullRenderer`

Metal and Vulkan are intentionally out of scope for this track.

## Cross-Platform Context Ladder

OpenQ4 now builds context candidates from one shared renderer-side ladder instead of keeping per-platform arrays. Windows SDL3, Linux SDL3, native Linux/GLX, and native macOS/NSOpenGL all record the selected request in `glConfig.contextRequest`, and `gfxInfo` prints both requested and actual debug-context state.

Forced modern tiers try the requested core-profile tier first, then lower modern tiers, then compatibility-profile fallbacks:

1. `gl46`: `4.6 core`, `4.5 core`, `4.3 core`, `4.1 core`, `3.3 core`, compatibility fallbacks
2. `gl45`: `4.5 core`, `4.3 core`, `4.1 core`, `3.3 core`, compatibility fallbacks
3. `gl43`: `4.3 core`, `4.1 core`, `3.3 core`, compatibility fallbacks
4. `gl41`: `4.1 core`, `3.3 core`, compatibility fallbacks
5. `gl33`: `3.3 core`, compatibility fallback

The default `auto` path still uses a versioned compatibility-profile ladder, because the current visible shipping executor is still the ARB2 compatibility bridge. This avoids regressing stock rendering while the modern executor is being promoted pass by pass. The shared ladder also has a modern-auto mode covered by self-test for the point where the visible renderer no longer needs the compatibility bridge.

`r_glDebugContext 1` now expands the ladder rather than making startup all-or-nothing: each requested debug candidate is followed by the same non-debug candidate if the platform or driver rejects debug contexts. macOS skips debug candidates explicitly because NSOpenGL does not expose debug-context creation. macOS also skips 4.3+ core requests and continues at the highest supported OpenGL profile, 4.1 core.

Use `rendererContextLadderSelfTest` to validate the table-driven ladder contract without entering gameplay.

## Capability Probe

The new capability probe records exact version/profile data and feature flags for:

- UBOs, VAOs, instancing, texture arrays, MRT/FBO support
- timer queries, sync objects, map-buffer-range
- buffer storage, direct state access, multi-bind
- compute shaders, SSBOs, draw indirect, multi-draw indirect
- texture views, GL SPIR-V, bindless texture availability

Extension parsing is token-safe and uses `glGetStringi` when available, with legacy string parsing only as fallback.

## Upload Bridge

Vertex-cache uploads now route through the renderer upload manager before falling back to the original legacy paths. This keeps the public `idVertexCache` API unchanged while moving both long-lived static vertex/index buffers and dynamic GUI, deform, packed-surface, and interaction-prep uploads onto renderer-owned allocation/upload code.

The upload layer is split into three pieces:

- `BufferAllocator` owns static VBO creation, data upload, deletion, and live static-buffer accounting for `idVertexCache::Alloc`.
- `RingBuffer` owns per-frame offsets, alignment, high-water, and overflow tracking for dynamic uploads.
- `UploadManager` selects the live GL upload path, owns the multi-buffered frame stream, retires sync fences, records static and dynamic upload metrics, and keeps `LegacyStreamBuffer` as the compatibility fallback/accounting path.

The dynamic stream is feature-gated:

- GL 4.4+/`ARB_buffer_storage` uses persistent mapped buffers when `r_rendererUploadPersistent 1`.
- GL 3.x+/`ARB_map_buffer_range` uses map-range streaming.
- Older VBO-capable paths use orphaned `glBufferSubData` streaming.
- If the upload stream is unavailable or overflows, the old vertex-cache fallback remains active for compatibility.

`r_rendererUploadMegs` controls the per-frame upload-stream size. The stream keeps multiple frame buffers and uses GL sync objects when available before reusing one. `gfxInfo` reports whether the frame stream and static-buffer allocator are enabled, which path was selected, the ring size, live static-buffer ownership, and whether persistent mapping is active. `r_rendererMetrics` reports static upload bytes/allocations, live static buffers, frame-ring high-water/overflow data, and persistent/map-range/subdata dynamic writes.

Use `rendererUploadSelfTest` to run the ring, allocator, and static-buffer tests in a live build.

## Metrics

`r_rendererMetrics` controls renderer telemetry:

- `0`: off
- `1`: periodic frame summary
- `2`: per-frame command, packet, and graph detail

The metrics layer records front-end time, submit time, back-end time, view/entity/light counts, draw/surface/vertex/index counts, upload bytes, buffer stalls, upload-stream high-water/overflow data, scene-packet counts, packet material/resource/geometry/instance coverage, packet category and sort-key validation counters, packet-driven render-graph counts, modern-executor preparation coverage, modern shader-library readiness, modern draw-plan coverage, modern submit-plan readiness, modern clustered-light readiness, modern deferred-resolve readiness, modern forward+ readiness, and selected renderer tier.

`r_rendererGpuTimers 1` samples GL timer queries when `r_rendererMetrics` is enabled and the driver exposes timer-query support. Samples are resolved on a delayed, nonblocking path; unavailable results are reported as `not-sampled` or dropped instead of stalling the CPU. Detail mode reports resolved GPU timing for the current compatibility backend command categories:

- 3D views
- 2D/GUI views
- render-target operations
- copy-render operations
- special effects
- buffer switches

Use `rendererGpuTimerSelfTest` to verify live timer-query support. `gfxInfo` reports whether renderer GPU timers are available and whether the cvar is enabled.

Metrics now also include the front-end scene-packet stream, resource-backed render graph, graph resource owner, modern GL executor path, modern clustered-light data model, deferred-lite resolve bridge, clustered forward+ bridge, and modern GL state-cache counters. Completed `RenderWorld` views emit `ScenePacket`, `PassPacket`, and `DrawPacket` records after portal/area/scissor culling, surface extraction, special-effect surface submission, subview generation, and draw-surface sorting. Full-screen GUI views emit the same packet contract from the GUI model builder. Draw packets carry legacy sort keys, material records, first bump/diffuse/specular stage images where available, geometry counts, scissor data, shader-register availability, and cache availability. The old ARB2 command-stream translator remains as a backend fallback for direct legacy command flushes, but normal frames report `packets=frontend` in `r_rendererMetrics`. The render graph now preserves ordered packet-pass nodes and attaches explicit virtual resources such as `sceneColor`, `sceneDepth`, G-buffer attachments, `deferredLight`, `postA`, `backBuffer`, and imported light-grid/cluster data. It records per-pass read/write/clear/resolve/invalidate/present edges, transient/imported resource counts, first/last resource lifetimes, and aliasable transient groups while ARB2 still owns visible pass execution. When `r_rendererModernExecutor 1` is enabled on a GL 3.3+ capable tier, the modern executor consumes that packet/graph data, keeps a starter VAO plus frame-constants UBO alive, validates the internal shader-library variants, builds a draw plan with program selections and state-batch counts, derives a submit plan from vertex/index cache state, builds CPU clustered-light records and UBOs for the hybrid-lighting milestones, can resolve the G-buffer subset into graph-owned deferred lighting, can submit graph-backed clustered forward+ opaque/alpha/transparent side-path draws, updates the frame UBO once per backend frame through the shared `GLStateCache`, and reports prepared pass/draw/plan/submit/cluster/deferred/forward+ coverage. When `r_rendererModernSubmit 1` is also enabled, the executor issues diagnostic GL 3.3 draw calls before the legacy backend runs while masking color/depth writes so ARB2 remains the visible renderer.

`RenderGraphResources` turns the virtual graph into real GL-owned resource handles while ARB2 remains visible. It imports the back buffer, legacy scene color/depth, and light-grid data; allocates transient texture/FBO pairs for scene depth, scene color, and post targets; validates FBO completeness; tracks first/last pass lifetimes, alias groups, and physical allocation ids; and exposes `rendererRenderGraphResourceSelfTest` plus `rendererRenderGraphResourceDump`. Detailed metrics report graph invalidates and `graphGL(...)` prepared/handle/physical/FBO status.

`MaterialResourceTable` turns packet material records into stable backend-facing material ids. Each entry records the source material id/name/class, blend and sort group, alpha-test state, shader-register ranges, first bump/diffuse/specular/emissive/GUI/post images, GL 3.3 classic texture slots, GL 4.3+ array/view descriptor candidates, disabled bindless fields, and an explicit fallback reason. Detailed metrics report `materialTable(...)` record, texture, class, missing-image, unsupported-feature, and fallback-reason counters, and `rendererMaterialResourceTableSelfTest` covers opaque, perforated, translucent, GUI, post, and missing-texture records.

Scene Packet V2 adds copied geometry and instance records to the packet stream. `GeometryResourceRecord` captures ambient/index buffer ids, cache offsets and byte ranges, vertex/index formats and counts, bounds, skinning/deform/upload classifications, and CPU index fallback provenance. `InstanceRecord` captures model and model-view matrices, previous matrix, entity color, shader-register ranges, palette offsets, and visibility flags for world, GUI, viewmodel, subview, remote-camera, render-demo, and bridge cases. Detailed metrics report geometry/instance record counts, record references, packet category counts, sort-key validation failures, and overflow causes, and `rendererGeometryResourceSelfTest` validates record copy, category classification, and overflow behavior.

`GLStateCache` is the central modern-state ownership point for program, VAO, buffer, texture, sampler, framebuffer, blend, depth, stencil, raster, viewport, scissor, and color-mask changes. It is invalidated explicitly when the backend hands control back to the legacy ARB2 renderer, so the modern diagnostic path never trusts state that ARB2 may have changed. `r_rendererMetrics 2` reports state-cache hits, misses, per-category misses, forced invalidations, and legacy handoff resets; `gfxInfo` reports debug-group/object-label availability and the latest cache state. Use `rendererGLStateCacheSelfTest` to exercise redundant state changes and legacy invalidation.

## Modern GL Executor Bring-Up

`r_rendererModernExecutor 1` enables the first modern-executor entry point. `r_rendererModernSubmit 1` additionally enables the first real GL 3.3 submission path:

- Requires the selected renderer tier to expose the GL 3.3 baseline feature set, including VAOs, UBOs, and buffer objects.
- Allocates a starter VAO and frame-constants UBO during OpenGL initialization.
- Compiles and reflects internal pass-specific shader variants for the supported GLSL tier matrix: 330, 410, 430, and 450 where the current context supports them.
- Consumes front-end `ScenePacket` and `RenderGraph` data every backend frame, with backend command-stream packetization retained only as a fallback.
- Builds a modern draw plan for depth, shadow-depth, flat-material, light-grid, and fog/blend packet categories, including shader-program selections, permutation/reflection metadata, fallback counts, and state-batch transition counts.
- Builds a modern submit plan from draw-plan entries with packet geometry/instance records, VBO-backed ambient vertex data, and either VBO-backed indices, frame-temp/generated index-cache blocks, or CPU indices that can be uploaded through `RendererUpload`.
- On `GpuDrivenGL43` and higher tiers, allocates executor-owned SSBO scene records, a validation SSBO, and a draw-indirect command buffer. The buffers are filled from the modern draw/submit plans every backend frame, and a tiny compute validation dispatch checks the scene-record stream without changing visible output.
- On `LowOverheadGL45` and higher tiers, uses direct-state-access buffer creation/subdata updates and multi-bind UBO/SSBO binding where the selected context exposes those entry points.
- Labels executor-owned buffers, programs, and the starter VAO when `KHR_debug` object labels are available, and wraps diagnostic submit/compute validation work in debug groups when supported.
- When `r_rendererModernSubmit 1` is active, binds the selected GLSL program, frame UBO, MVP uniform, scissor, ambient VBO, and index source through `GLStateCache`, then issues `glDrawElements` or `glDrawArrays` for ready commands.
- Masks color and depth writes during diagnostic submission, restores GL state afterward, and then lets ARB2 render the visible frame.
- When `r_rendererModernVisibleDepth 1` is active, clears graph-backed `sceneDepth` and `shadowMap` resources, submits conservative modern depth and compatible shadow-depth draws into those resources, and keeps ARB2 in charge of final color output.
- When `r_rendererModernOpaque 1` is active, allocates graph-backed `gbufferAlbedo`, `gbufferNormal`, `gbufferMaterial`, and `gbufferEmissive` resources, binds them as a four-target MRT FBO with graph-owned `sceneDepth`, and submits conservative opaque/perforated/alpha-test G-buffer draws before ARB2 renders the final color frame.
- When `r_rendererModernDeferred 1` is active, resolves the graph-backed G-buffer subset into `deferredLight` by sampling albedo, normal, material, emissive/light-grid, scene depth, and the clustered-light UBOs while ARB2 keeps visible color ownership.
- When `r_rendererForwardPlus 1` is active, writes graph-backed `sceneColor` through clustered forward+ opaque, alpha-test, and transparent/fog-blend pipelines that sample the clustered-light UBOs and preserve transparent sort order while ARB2 keeps final frame ownership.
- When the modern executor, opaque G-buffer bridge, deferred resolve, forward+ bridge, or clustered debug overlay is active, builds conservative per-scene 8x6x16 clustered-light grids from `viewLight_t` lists, classifies point, projected, fog, ambient, and special lights, applies portal/area plus scissor coarse culling, and uploads grid params, light records, and packed cluster references through GL 3.3 UBO fallback buffers.
- `r_rendererModernDepthDebug 1` overlays the graph-backed scene depth resource before swap; mode `2` overlays the shadow-depth resource when that pass executed.
- `r_rendererModernGBufferDebug` overlays the selected G-buffer attachment before swap for albedo, normal, material, or emissive/light-grid inspection.
- `r_rendererModernDeferredDebug` overlays deferred light contribution, cluster id, light-count heat, or fallback/overflow pressure before swap.
- `r_rendererClusterDebug` overlays clustered-light occupancy, light-count heatmap, or overflow pressure before swap.
- Keeps stencil shadows, deform surfaces, unsupported material classes, GPU-palette skinning, unsupported geometry, unavailable graph resources, unavailable MRT state, unsupported forward+ blend modes, GUI/subview/post/special-effect packets, and alpha-test surfaces without a diffuse texture on the legacy path with explicit fallback counters.
- Reports prepared pass/draw coverage, shader-library readiness, draw-plan coverage, submit-plan readiness, cache-backed versus CPU-uploaded index counts, submitted draw/fallback counts, visible depth/shadow draw and fallback counts, G-buffer draw/fallback/bandwidth/MRT/overlay state, deferred resolve pixel/light/cluster-read/fallback/GPU-timer/overlay state, forward+ opaque/alpha/transparent/sort/fallback/overdraw/cluster-read/GPU-timer state, clustered-light grid/light/reference/overflow/build/UBO/debug-overlay state, resource readiness, debug-overlay readiness, GL 4.3 GPU-driven resource readiness, GL 4.5 DSA/multi-bind usage, and GL state-cache hit/miss behavior through `gfxInfo` and `r_rendererMetrics 2`.

This gives the GL 3.3/4.1 executor a live object lifetime, frame-constant upload, packet-consumption, shader-library reflection, draw-plan generation, diagnostic GL submission, graph-backed depth and G-buffer resource execution, clustered-light preparation, deferred-lite resolve execution, clustered forward+ side-path execution, fallback accounting, state-cache handoff safety, and metrics contract without replacing the stock Quake 4 color path. The GL 4.3 and GL 4.5 tiers are now active executor paths rather than capability labels: forced `r_glTier gl43` exercises SSBO/compute/indirect resource ownership, while forced `r_glTier gl45` additionally exercises DSA and multi-bind. Use `rendererModernGLExecutorSelfTest` to verify the analysis path, live GL object state, GPU-driven buffers, compute validation dispatch, and low-overhead binding path. Use `rendererVisiblePathSelfTest` to verify the visible-depth bridge, graph-backed scene/shadow resources, depth-overlay program, and conservative fallback contract. Use `rendererGBufferSelfTest` to verify graph-backed G-buffer resources, MRT setup, diffuse texture binding, packing assumptions, attachment bandwidth metrics, and G-buffer overlay readiness. Use `rendererClusterGridSelfTest` to verify light classification, cluster slicing, overflow accounting, UBO upload readiness, and debug-overlay texture generation. Use `rendererDeferredResolveSelfTest` to verify graph-backed deferred output, G-buffer/depth/cluster inputs, point/projected light accumulation, light-grid contribution, fallback counters, and deferred overlay readiness. Use `rendererForwardPlusSelfTest` to verify graph-backed scene-color/depth resources, clustered forward opaque/alpha/transparent programs, transparent sort preservation, clustered-light reads, and forward+ fallback counters. Use `rendererGLStateCacheSelfTest` to verify redundant-state suppression and legacy invalidation. Use `rendererShaderLibrarySelfTest` to verify the internal GLSL families, compact permutation keys, and reflected frame/resource bindings. Use `rendererModernGLDrawPlanSelfTest` to verify packet-to-plan classification against the current resource graph. Use `rendererModernGLSubmitPlanSelfTest` to verify submit-readiness classification, persistent/temp index-cache classification, CPU-index upload classification, and cache-fallback accounting.

## Modern Shader Library

The `ShaderLibrary` remains internal-only and does not replace ARB assembly or Raven-authored GLSL material programs. Shader Library V2 now compiles a controlled pass-oriented program family for every enabled GLSL tier:

- depth and shadow-depth variants for the first visible depth milestones
- transitional flat material, light-grid, fog/blend, GUI, and post-copy variants used by the current analysis path
- G-buffer opaque and alpha-tested variants for opt-in opaque material ownership
- deferred light resolve, clustered forward opaque, clustered forward alpha-tested, and transparent forward variants for hybrid lighting paths
- debug visualization variants for future resource and lighting inspection
- GLSL 330 baseline, plus 410/430/450 variants when the selected GL context supports them

Each linked program records its pass category, material class, compact `rendererPermutationKey_t`, reflected uniform locations, UBOs, SSBOs, samplers, images, attributes, and validated GLSL version. Shader compile/link diagnostics include the program name, pass, material class, GLSL tier, and permutation key. `gfxInfo` reports program/kind/permutation counts, per-version coverage, reload count, reflection totals, texture-program coverage, and readiness for every shader kind under `Modern GL shader library`.

Runtime shader reload is intentionally debug-only. `rendererShaderLibraryReload` rebuilds the internal programs only when `r_rendererShaderReload 1` is set; otherwise it logs a skipped reload and leaves the existing library intact. Use `rendererShaderLibrarySelfTest` as the canonical self-test alias. `rendererModernGLShaderLibrarySelfTest` remains available for older scripts. Later milestones can promote these validated variants into visible depth, material, GUI, and post passes without introducing external shader assets or changing stock Quake 4 material behavior.

## Modern Draw Plan

The first modern draw-plan milestone is also internal-only. It consumes the packet frame and render graph after the shader library is ready, then emits executor-owned plan entries for packet categories that can be represented by the starter shader set:

- depth packets select the internal depth pipeline
- stencil-shadow and shadow-map packets select the internal shadow-depth pipeline
- ARB2 interaction and ambient packets select the internal flat-material pipeline, the G-buffer pipeline when `r_rendererModernOpaque`/G-buffer debug mode is active and the material is in the conservative opaque or alpha-test subset, or clustered forward+ pipelines when `r_rendererForwardPlus` is active for eligible non-deferred surfaces
- light-grid and fog/blend packets select their own internal shader variants, with eligible fog/blend packets promoted to the transparent forward+ pipeline when `r_rendererForwardPlus` is active
- GUI, special effects, authored post-process, render-target, copy, and present packets remain explicit fallback categories

Each plan entry records the source draw packet, pass category, shader kind, program handle, shader permutation, reflected MVP/debug/local/sampler bindings, material record index, geometry record index, instance record index, geometry counts, index/vertex-only mode, and GLSL variant. Metrics report planned draws, depth draws, material-family draws, fallback draws, missing material/geometry/instance records, state batches, program switches, material switches, and overflow status. The plan is deliberately prepare-first: it validates the modern submission shape while ARB2 continues to render the visible frame.

## Modern Submit Plan

The modern submit plan consumes the draw plan and derives the subset that is ready for a GL 3.3-style VAO/VBO indexed submission path:

- planned draws with copied geometry/instance records, VBO-backed ambient vertex caches, and VBO-backed index buffers become submit-ready commands
- generated or frame-temporary index caches now retain their element-buffer identity, so deformed, packed MD5R, packed light-interaction, and GLSL-prepared interaction paths can become cache-backed submit-ready commands instead of falling back to CPU-index upload
- planned draws with VBO-backed ambient vertex caches and CPU index arrays use `RendererUpload` for a frame-temp index buffer
- draws missing modern-safe vertex data or any usable index source remain explicit fallback draws
- the plan records program, vertex-buffer, index-buffer, scissor, and material batch transitions
- the plan records per-draw uniform-update pressure, the single frame-UBO bind, CPU-index upload pressure, and submitted/fallback counts

With `r_rendererModernSubmit 0`, this still answers “how much of this frame could a modern GL submitter draw today?” With `r_rendererModernSubmit 1`, it also executes those ready commands before ARB2 while color/depth writes are disabled. ARB2 still renders the frame.

## Compatibility Bridge

The active visible renderer remains ARB2. The modern executor is still opt-in; when `r_rendererModernSubmit 1` is enabled it can issue masked diagnostic GL draws before ARB2, but it does not own visible pass output yet. The new scaffolding deliberately wraps existing behavior:

- `RendererBootstrap` owns selected tier/features and marks the ARB2 bridge.
- `RenderGraph` builds ordered packet-pass nodes from the front-end scene-packet frame and attaches virtual resource edges for current legacy depth, color, post-process, GUI, and present work.
- `RenderGraphResources` turns graph resource records into stable imported or transient GL handles, owns transient texture/FBO allocation and validation, tracks lifetimes and conservative aliasing, and reports graph resource state without changing visible output.
- `MaterialResourceTable` turns packet material records into stable table records with texture/sampler descriptors and explicit fallback reasons, so modern draw plans can carry a material table id forward instead of treating the legacy material pointer as submit-time state.
- `ScenePacket`, `DrawPacket`, `PassPacket`, `MaterialResourceRecord`, `GeometryResourceRecord`, and `InstanceRecord` define the backend-neutral packet contract. `RenderWorld` and GUI producers emit draw-view packets before the legacy command queue reaches the backend; command-only passes such as render-target, copy, special-effect, and present operations are tracked alongside them, and the backend command-stream translator is kept as a survival fallback. ARB2 still executes the original commands for visible output.
- `ModernGLExecutor` owns the opt-in GL 3.3+ executor shell, validates packet/graph coverage, keeps Shader Library V2 alive, updates a frame-constants UBO, optionally performs masked diagnostic GL submission, optionally writes graph-backed scene/shadow depth resources through `r_rendererModernVisibleDepth`, optionally writes graph-backed opaque G-buffer resources through `r_rendererModernOpaque`, optionally resolves that G-buffer into graph-owned `deferredLight` through `r_rendererModernDeferred`, optionally writes clustered forward+ opaque/alpha/transparent side-path output through `r_rendererForwardPlus`, promotes GL 4.3+ tiers into live SSBO/compute/indirect resource updates, promotes GL 4.5+ tiers into DSA/multi-bind updates, and then leaves visible color execution on the ARB2 bridge.
- `ModernClusteredLighting` builds conservative CPU light-cluster grids from packet-scene `viewLight_t` lists, uploads grid/light/index records through GL 3.3 UBOs for later hybrid-lighting shaders, and exposes `r_rendererClusterDebug` occupancy, count, and overflow overlays without changing final lighting.
- `GLStateCache` owns modern-side GL state binding suppression and records explicit invalidations before ARB2 resumes control.
- `ModernGLDrawPlan` translates eligible packet/graph categories into executor-owned depth, shadow-depth, flat-material, G-buffer, clustered forward+, light-grid, and fog/blend draw-plan entries with Shader Library V2 selections, compact permutation metadata, reflected bindings, material/geometry/instance record ids, and state-batch metrics, while explicit fallback categories stay on the legacy path.
- `ModernGLSubmitPlan` translates draw-plan entries into submit commands from copied geometry and instance records when the legacy vertex cache exposes VBO-backed ambient buffers and either VBO-backed, frame-temp, generated, or CPU-uploadable index data, carries shader-kind/reflection metadata forward, and records missing-buffer fallback reasons for the GL3 executor.
- `RendererUpload` owns static vertex/index buffer uploads, the feature-gated dynamic frame-temp stream, and the old vertex-cache path as a fallback.

Use `gfxInfo` to inspect the selected tier, context profile, feature flags, capability summary, GPU-timer availability, scene-packet limits/source reporting, geometry/instance record limits, resource-graph limits, render-graph resource ownership, material resource-table readiness, modern-executor availability, visible-depth resource/debug-overlay state, G-buffer resource/MRT/debug-overlay state, deferred resolve resource/debug-overlay state, forward+ resource/program/cluster/draw/fallback state, clustered-light UBO/debug-overlay state, GL state-cache/debug-label status, Shader Library V2 status, draw-plan coverage, submit-plan readiness, GL 4.3/4.5 executor resource readiness, and upload stream. Use `rendererContextLadderSelfTest`, `rendererTierSelfTest`, `rendererUploadSelfTest`, `rendererGpuTimerSelfTest`, `rendererScenePacketSelfTest`, `rendererRenderGraphSelfTest`, `rendererRenderGraphResourceSelfTest`, `rendererMaterialResourceTableSelfTest`, `rendererGeometryResourceSelfTest`, `rendererGLStateCacheSelfTest`, `rendererModernGLExecutorSelfTest`, `rendererVisiblePathSelfTest`, `rendererGBufferSelfTest`, `rendererClusterGridSelfTest`, `rendererDeferredResolveSelfTest`, `rendererForwardPlusSelfTest`, `rendererShaderLibrarySelfTest`, `rendererModernGLDrawPlanSelfTest`, and `rendererModernGLSubmitPlanSelfTest` to run the live renderer foundation tests.

The automated safe validation matrix lives in [renderer-validation-matrix.md](renderer-validation-matrix.md) and can be run with:

```powershell
python tools\tests\renderer_validation_matrix.py
```

The runner covers startup, `gfxInfo`, forced `r_glTier` probes, debug-context fallback, presentation cvar probes, clustered-light diagnostics, deferred-resolve diagnostics, forward+ diagnostics, and all renderer foundation self-tests without entering gameplay. The manual section of that document remains the release sign-off matrix for SP/MP map smoke tests once map startup is safe to run.
