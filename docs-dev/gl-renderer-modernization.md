# GL Renderer Modernization

OpenQ4 now has the first GL-only foundation for the high-performance renderer redesign. This milestone does not replace the ARB2 renderer yet; it adds the capability, selection, telemetry, and compatibility bridge that later GL 3.3/4.x executors will use.

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

## SDL3 Context Ladder

The SDL3 platform backend now creates GL contexts through an explicit ladder. Forced modern tiers try:

1. `4.6 core`
2. `4.5 core`
3. `4.3 core`
4. `4.1 core`
5. `3.3 core`
6. compatibility fallback

The default `auto` path uses the same version ladder with compatibility profiles, because the current shipping executor is still the ARB2 compatibility bridge. This avoids regressing stock rendering while the modern executor is being built.

`r_glDebugContext 1` requests a debug context on platforms/drivers that support it.

## Capability Probe

The new capability probe records exact version/profile data and feature flags for:

- UBOs, VAOs, instancing, texture arrays, MRT/FBO support
- timer queries, sync objects, map-buffer-range
- buffer storage, direct state access, multi-bind
- compute shaders, SSBOs, draw indirect, multi-draw indirect
- texture views, GL SPIR-V, bindless texture availability

Extension parsing is token-safe and uses `glGetStringi` when available, with legacy string parsing only as fallback.

## Upload Bridge

Frame-temporary vertex-cache uploads now route through the renderer upload stream before falling back to the original legacy temp buffers. This keeps the public `idVertexCache` API unchanged while moving dynamic GUI, deform, packed-surface, and interaction-prep uploads onto the new renderer-owned path.

The stream is feature-gated:

- GL 4.4+/`ARB_buffer_storage` uses persistent mapped buffers when `r_rendererUploadPersistent 1`.
- GL 3.x+/`ARB_map_buffer_range` uses map-range streaming.
- Older VBO-capable paths use orphaned `glBufferSubData` streaming.
- If the upload stream is unavailable or overflows, the old vertex-cache fallback remains active for compatibility.

`r_rendererUploadMegs` controls the per-frame upload-stream size. The stream keeps multiple frame buffers and uses GL sync objects when available before reusing one. `gfxInfo` reports whether the dynamic upload bridge is enabled, which path it selected, the ring size, and whether persistent mapping is active.

Use `rendererUploadSelfTest` to run the pure ring/allocation tests in a live build.

## Metrics

`r_rendererMetrics` controls renderer telemetry:

- `0`: off
- `1`: periodic frame summary
- `2`: per-frame command and legacy graph detail

The metrics layer records front-end time, submit time, back-end time, view/entity/light counts, draw/surface/vertex/index counts, upload bytes, buffer stalls, upload-stream high-water/overflow data, and selected renderer tier.

`r_rendererGpuTimers 1` samples GL timer queries when `r_rendererMetrics` is enabled and the driver exposes timer-query support. Samples are resolved on a delayed, nonblocking path; unavailable results are reported as `not-sampled` or dropped instead of stalling the CPU. Detail mode reports resolved GPU timing for the current compatibility backend command categories:

- 3D views
- 2D/GUI views
- render-target operations
- copy-render operations
- special effects
- buffer switches

Use `rendererGpuTimerSelfTest` to verify live timer-query support. `gfxInfo` reports whether renderer GPU timers are available and whether the cvar is enabled.

## Compatibility Bridge

The active renderer remains ARB2 unless future work adds a modern executor. The new scaffolding deliberately wraps existing behavior:

- `RendererBootstrap` owns selected tier/features and marks the ARB2 bridge.
- `RenderGraph` currently wraps the existing command stream as legacy graph nodes.
- `ScenePacket`, `DrawPacket`, `PassPacket`, and `MaterialResourceRecord` define the backend-neutral packet contract for future extraction.
- `RendererUpload` owns the feature-gated dynamic frame-temp stream and keeps the old vertex-cache path as a fallback.

Use `gfxInfo` to inspect the selected tier, context profile, feature flags, capability summary, GPU-timer availability, and upload stream. Use `rendererTierSelfTest`, `rendererUploadSelfTest`, and `rendererGpuTimerSelfTest` to run the live renderer foundation tests.
