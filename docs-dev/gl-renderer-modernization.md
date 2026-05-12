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

## Metrics

`r_rendererMetrics` controls renderer telemetry:

- `0`: off
- `1`: periodic frame summary
- `2`: per-frame command and legacy graph detail

The first metrics layer records front-end time, submit time, back-end time, view/entity/light counts, draw/surface/vertex/index counts, upload bytes, buffer stalls, and selected renderer tier. GPU pass timings are reserved for the next timer-query milestone and currently report as `not-sampled`.

## Compatibility Bridge

The active renderer remains ARB2 unless future work adds a modern executor. The new scaffolding deliberately wraps existing behavior:

- `RendererBootstrap` owns selected tier/features and marks the ARB2 bridge.
- `RenderGraph` currently wraps the existing command stream as legacy graph nodes.
- `ScenePacket`, `DrawPacket`, `PassPacket`, and `MaterialResourceRecord` define the backend-neutral packet contract for future extraction.
- `RendererUpload` records legacy vertex-cache uploads and exposes the allocator/ring/upload-manager skeleton for persistent mapped rings later.

Use `gfxInfo` to inspect the selected tier, context profile, feature flags, and capability summary. Use `rendererTierSelfTest` to run the table-driven tier-selector tests in a live build.
