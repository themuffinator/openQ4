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

The metrics layer records front-end time, submit time, back-end time, view/entity/light counts, draw/surface/vertex/index counts, upload bytes, buffer stalls, upload-stream high-water/overflow data, scene-packet counts, packet material/resource/geometry coverage, packet-driven render-graph counts, modern-executor preparation coverage, modern shader-library readiness, modern draw-plan coverage, modern submit-plan readiness, and selected renderer tier.

`r_rendererGpuTimers 1` samples GL timer queries when `r_rendererMetrics` is enabled and the driver exposes timer-query support. Samples are resolved on a delayed, nonblocking path; unavailable results are reported as `not-sampled` or dropped instead of stalling the CPU. Detail mode reports resolved GPU timing for the current compatibility backend command categories:

- 3D views
- 2D/GUI views
- render-target operations
- copy-render operations
- special effects
- buffer switches

Use `rendererGpuTimerSelfTest` to verify live timer-query support. `gfxInfo` reports whether renderer GPU timers are available and whether the cvar is enabled.

Metrics now also include the front-end scene-packet stream, resource-backed render graph, and modern GL executor path. Completed `RenderWorld` views emit `ScenePacket`, `PassPacket`, and `DrawPacket` records after portal/area/scissor culling, surface extraction, special-effect surface submission, subview generation, and draw-surface sorting. Full-screen GUI views emit the same packet contract from the GUI model builder. Draw packets carry legacy sort keys, material records, first bump/diffuse/specular stage images where available, geometry counts, scissor data, shader-register availability, and cache availability. The old ARB2 command-stream translator remains as a backend fallback for direct legacy command flushes, but normal frames report `packets=frontend` in `r_rendererMetrics`. The render graph now preserves ordered packet-pass nodes and attaches explicit virtual resources such as `sceneColor`, `sceneDepth`, `postA`, `backBuffer`, and imported light-grid data. It records per-pass read/write/clear/resolve/present edges, transient/imported resource counts, first/last resource lifetimes, and aliasable transient groups while ARB2 still owns the actual FBO/texture execution. When `r_rendererModernExecutor 1` is enabled on a GL 3.3+ capable tier, the modern executor consumes that packet/graph data, keeps a starter VAO plus frame-constants UBO alive, validates the internal shader-library variants, builds a draw plan with program selections and state-batch counts, derives a submit plan from vertex/index cache state, updates the UBO once per backend frame through its first owned state binding cache, and reports prepared pass/draw/plan/submit coverage. When `r_rendererModernSubmit 1` is also enabled, the executor issues diagnostic GL 3.3 draw calls before the legacy backend runs while masking color/depth writes so ARB2 remains the visible renderer.

## Modern GL Executor Bring-Up

`r_rendererModernExecutor 1` enables the first modern-executor entry point. `r_rendererModernSubmit 1` additionally enables the first real GL 3.3 submission path:

- Requires the selected renderer tier to expose the GL 3.3 baseline feature set, including VAOs, UBOs, and buffer objects.
- Allocates a starter VAO and frame-constants UBO during OpenGL initialization.
- Compiles and reflects internal pass-specific shader variants for the supported GLSL tier matrix: 330, 410, 430, and 450 where the current context supports them.
- Consumes front-end `ScenePacket` and `RenderGraph` data every backend frame, with backend command-stream packetization retained only as a fallback.
- Builds a modern draw plan for depth, shadow-depth, flat-material, light-grid, and fog/blend packet categories, including shader-program selections, permutation/reflection metadata, fallback counts, and state-batch transition counts.
- Builds a modern submit plan from draw-plan entries with VBO-backed ambient vertex data and either VBO-backed indices, frame-temp/generated index-cache blocks, or CPU indices that can be uploaded through `RendererUpload`.
- On `GpuDrivenGL43` and higher tiers, allocates executor-owned SSBO scene records, a validation SSBO, and a draw-indirect command buffer. The buffers are filled from the modern draw/submit plans every backend frame, and a tiny compute validation dispatch checks the scene-record stream without changing visible output.
- On `LowOverheadGL45` and higher tiers, uses direct-state-access buffer creation/subdata updates and multi-bind UBO/SSBO binding where the selected context exposes those entry points.
- When `r_rendererModernSubmit 1` is active, binds the selected GLSL program, frame UBO, MVP uniform, scissor, ambient VBO, and index source, then issues `glDrawElements` or `glDrawArrays` for ready commands.
- Masks color and depth writes during diagnostic submission, restores GL state afterward, and then lets ARB2 render the visible frame.
- Reports prepared pass/draw coverage, shader-library readiness, draw-plan coverage, submit-plan readiness, cache-backed versus CPU-uploaded index counts, submitted draw/fallback counts, GL 4.3 GPU-driven resource readiness, and GL 4.5 DSA/multi-bind usage through `gfxInfo` and the `r_rendererMetrics 2` executor detail line.

This gives the GL 3.3/4.1 executor a live object lifetime, frame-constant upload, packet-consumption, shader-library reflection, draw-plan generation, diagnostic GL submission, fallback accounting, and metrics contract without changing stock Quake 4 visible rendering behavior. The GL 4.3 and GL 4.5 tiers are now active executor paths rather than capability labels: forced `r_glTier gl43` exercises SSBO/compute/indirect resource ownership, while forced `r_glTier gl45` additionally exercises DSA and multi-bind. Use `rendererModernGLExecutorSelfTest` to verify the analysis path, live GL object state, GPU-driven buffers, compute validation dispatch, and low-overhead binding path. Use `rendererModernGLShaderLibrarySelfTest` to verify the internal GLSL variants and reflected frame-constant bindings. Use `rendererModernGLDrawPlanSelfTest` to verify packet-to-plan classification against the current resource graph. Use `rendererModernGLSubmitPlanSelfTest` to verify submit-readiness classification, persistent/temp index-cache classification, CPU-index upload classification, and cache-fallback accounting.

## Modern Shader Library

The `ShaderLibrary` remains internal-only and does not replace ARB assembly or Raven-authored GLSL material programs. Capable modern tiers now compile a pass-oriented built-in shader family instead of a pair of debug-only programs:

- depth and shadow-depth variants
- flat material, light-grid, and fog/blend variants used by the current draw-plan categories
- texture-capable GUI and post-copy variants for later modern pass ownership
- shared `ModernFrameConstants` UBO reflection, per-draw MVP reflection, debug/local-parameter reflection, sampler reflection, fixed position/texcoord attribute metadata, and permutation metadata
- GLSL 330 baseline, plus 410/430/450 variants when the selected GL context supports them

Each linked program records its pass category, material class, `rendererPermutationKey_t`, reflected uniform/sampler locations, and validated GLSL version. `gfxInfo` reports program/kind/permutation counts, reflected uniforms/samplers, texture-program coverage, and readiness for every shader kind under `Modern GL shader library`. Later milestones can promote these validated variants into visible depth, material, GUI, and post passes without introducing external shader assets or changing stock Quake 4 material behavior.

## Modern Draw Plan

The first modern draw-plan milestone is also internal-only. It consumes the packet frame and render graph after the shader library is ready, then emits executor-owned plan entries for packet categories that can be represented by the starter shader set:

- depth packets select the internal depth pipeline
- stencil-shadow and shadow-map packets select the internal shadow-depth pipeline
- ARB2 interaction and ambient packets select the internal flat-material pipeline
- light-grid and fog/blend packets select their own internal shader variants
- GUI, special effects, authored post-process, render-target, copy, and present packets remain explicit fallback categories

Each plan entry records the source draw packet, pass category, shader kind, program handle, shader permutation, reflected MVP/debug/local/sampler bindings, material record index, geometry counts, index/vertex-only mode, and GLSL variant. Metrics report planned draws, depth draws, material-family draws, fallback draws, state batches, program switches, material switches, and overflow status. The plan is deliberately prepare-first: it validates the modern submission shape while ARB2 continues to render the visible frame.

## Modern Submit Plan

The modern submit plan consumes the draw plan and derives the subset that is ready for a GL 3.3-style VAO/VBO indexed submission path:

- planned draws with VBO-backed ambient vertex caches and VBO-backed index buffers become submit-ready commands
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
- `ScenePacket`, `DrawPacket`, `PassPacket`, and `MaterialResourceRecord` define the backend-neutral packet contract. `RenderWorld` and GUI producers emit draw-view packets before the legacy command queue reaches the backend; command-only passes such as render-target, copy, special-effect, and present operations are tracked alongside them, and the backend command-stream translator is kept as a survival fallback. ARB2 still executes the original commands for visible output.
- `ModernGLExecutor` owns the opt-in GL 3.3+ executor shell, validates packet/graph coverage, keeps the modern shader library alive, updates a frame-constants UBO, optionally performs masked diagnostic GL submission, promotes GL 4.3+ tiers into live SSBO/compute/indirect resource updates, promotes GL 4.5+ tiers into DSA/multi-bind updates, and then leaves visible execution on the ARB2 bridge.
- `ModernGLDrawPlan` translates eligible packet/graph categories into executor-owned depth, shadow-depth, flat-material, light-grid, and fog/blend draw-plan entries with shader selections, permutation metadata, reflected bindings, and state-batch metrics, while explicit fallback categories stay on the legacy path.
- `ModernGLSubmitPlan` translates draw-plan entries into submit commands when the legacy vertex cache exposes VBO-backed ambient buffers and either VBO-backed, frame-temp, generated, or CPU-uploadable index data, carries shader-kind/reflection metadata forward, and records missing-buffer fallback reasons for the GL3 executor.
- `RendererUpload` owns static vertex/index buffer uploads, the feature-gated dynamic frame-temp stream, and the old vertex-cache path as a fallback.

Use `gfxInfo` to inspect the selected tier, context profile, feature flags, capability summary, GPU-timer availability, scene-packet limits/source reporting, resource-graph limits, modern-executor availability, shader-library status, draw-plan coverage, submit-plan readiness, GL 4.3/4.5 executor resource readiness, and upload stream. Use `rendererContextLadderSelfTest`, `rendererTierSelfTest`, `rendererUploadSelfTest`, `rendererGpuTimerSelfTest`, `rendererScenePacketSelfTest`, `rendererRenderGraphSelfTest`, `rendererModernGLExecutorSelfTest`, `rendererModernGLShaderLibrarySelfTest`, `rendererModernGLDrawPlanSelfTest`, and `rendererModernGLSubmitPlanSelfTest` to run the live renderer foundation tests.

The automated safe validation matrix lives in [renderer-validation-matrix.md](renderer-validation-matrix.md) and can be run with:

```powershell
python tools\tests\renderer_validation_matrix.py
```

The runner covers startup, `gfxInfo`, forced `r_glTier` probes, debug-context fallback, presentation cvar probes, and all renderer foundation self-tests without entering gameplay. The manual section of that document remains the release sign-off matrix for SP/MP map smoke tests once map startup is safe to run.
