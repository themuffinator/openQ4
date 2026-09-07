# Shadow Mapping and Transparency Shadowing Guide

This guide covers openQ4's user-facing shadow-map settings, including projected-light shadow maps, experimental point-light shadow maps, cascaded shadow maps (CSM), alpha-tested transparency shadows, and the current experimental translucent-shadow path.

## Quick Start

Recommended starting point:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapHashedAlpha 1
vid_restart
```

Optional experimental translucent-shadow overlay:

```cfg
seta r_shadowMapTranslucentMoments 1
seta r_shadowMapTranslucentDensity 1.0
seta r_shadowMapTranslucentMinAlpha 0.02
vid_restart
```

Notes:
- `r_shadows` must stay enabled for any shadow path to render.
- If a light cannot use a complete shadow map, openQ4 uses its stencil representation when available. Vulkan logs a missing-resource warning and preserves direct illumination if neither representation can be submitted.
- If only part of a receiver ownership can be represented in a shadow map, the classic renderer combines that exact map with the missing casters' stencil supplements. If those supplements are incomplete, the complete ownership falls back to stencil instead of publishing a partial shadow.
- Point lights shadow-map by default (`r_shadowMapPointLights 1`); they are the dominant light class in Quake 4 content. Set `r_shadowMapPointLights 0` to fall back to stencil shadows for point lights only.
- Lights touching animated, deformed, or packed character receivers can also fall back to the legacy stencil path so stock character lighting, mirrored seams, and eye materials retain retail-style interaction behavior.
- The player flashlight uses its authored single projected shadow map; disabling flashlight cascades does not select stencil shadows. `testFlashlight 1` enables the current SP weapon's flashlight for rendering tests without simulated player input (`testFlashlight 0` turns it off).
- Light-emitting panels that cast no stencil volume no longer force an entire point light into fallback. Panels with a real surface volume are admitted to the depth map, including the moving lift panel in Storage 2. A real missing caster still requires a complete map or stencil supplement.
- Update budgets and subview policy may select stencil only when it covers all admitted casters. Required map updates can exceed `r_shadowMapMaxUpdatesPerView`; use `0` for unrestricted updates. Cache slot exhaustion alone can use scratch maps and does not require stencil.
- Modern renderer diagnostics keep lighting visible when shadow-map receiver sampling is not ready, but full modern visible-frame replacement stays fail-closed so the legacy path continues to provide the actual shadowed frame.
- Most shadow cvars can be changed live, but `vid_restart` is the safest way to apply large changes such as map resolution, cascade layout, or switching the shadow pipeline on/off.

## What the System Does

openQ4 currently supports:
- Projected-light shadow maps for regular projected lights.
- Point-light cubemap shadow maps for omni/point lights (the dominant Quake 4 light class), on by default.
- Optional projected-light cascaded shadow maps (CSM).
- Alpha-tested transparency shadows for cutout materials such as fences, grates, and foliage cards.
- Optional experimental translucent-shadow accumulation for some blended materials.

High-level behavior:
- Opaque materials cast normal solid shadows.
- Alpha-tested or perforated materials cast cutout shadows in the shadow-map pass.
- Blended/translucent materials do not cast translucent shadowing unless `r_shadowMapTranslucentMoments 1` is enabled.
- The translucent-shadow path is experimental and intentionally conservative.

## Recommended Presets

### Balanced Quality

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapSize 1024
seta r_shadowMapFilterRadius 0.75
seta r_shadowMapFilterTaps 9
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 9
seta r_shadowMapCasterCulling 2
seta r_shadowMapHashedAlpha 1
seta r_shadowMapCascadeStabilize 1
vid_restart
```

### Higher Quality

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapSize 2048
seta r_shadowMapPointSize 1024
seta r_shadowMapCascadeCount 4
seta r_shadowMapFilterRadius 1.0
seta r_shadowMapFilterTaps 13
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 13
seta r_shadowMapCasterCulling 2
seta r_shadowMapHashedAlpha 1
seta r_shadowMapCascadeStabilize 1
vid_restart
```

### Optional Modern Filtering

```cfg
seta r_shadowMapFilterMode 1
seta r_shadowMapPointFilterMode 1
seta r_shadowMapStaticCache 1
vid_restart
```

For projected lights, `r_shadowMapFilterMode 2` enables the experimental PCSS-lite path. Hardware depth-compare sampling cannot run the blocker search needed for PCSS-lite, so selecting filter mode 2 automatically switches the renderer to the manual raw-depth path even while `r_shadowMapDepthCompare 1` is set.

### Performance-Focused

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 0
seta r_shadowMapSize 512
seta r_shadowMapPointSize 256
seta r_shadowMapFilterRadius 0.75
seta r_shadowMapFilterTaps 5
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 5
seta r_shadowMapHashedAlpha 1
vid_restart
```

### Experimental Blended Transparency

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapHashedAlpha 1
seta r_shadowMapTranslucentMoments 1
seta r_shadowMapTranslucentDensity 1.0
seta r_shadowMapTranslucentMinAlpha 0.02
vid_restart
```

## Core Shadow Settings

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadows` | `1` | `0..1` | Master shadow toggle. |
| `r_useShadowMap` | `0` | `0..1` | Enables the shadow-map pipeline for supported lights. |
| `r_shadowMapCSM` | `0` | `0..1` | Enables cascaded shadow maps for projected lights. |
| `r_shadowMapProjectedCSM` | `1` | `0..1` | Allows ordinary projected lights to use CSM when `r_shadowMapCSM` is enabled; parallel/global lights keep their dedicated large-coverage policy. |
| `r_shadowMapConservativeCasters` | `1` | `0..1` | Keeps shadow-map caster submission separate from visible receiver scissors, so off-screen blockers can still shadow visible receivers. |
| `r_shadowMapSkipStencilShadows` | `1` | `0..1` | Skips stencil shadow volume generation and linking for lights that will render shadow maps, removing the duplicate CPU shadow work. A light that fails a shadow-map pass automatically restores its stencil volumes on the next frame. |
| `r_shadowMapCasterCulling` | `2` | `0..2` | Caster face culling: `0` always renders two-sided, `1` forces the material-oriented light-facing near shell, and `2` automatically uses the near shell for sealed hulls outside the light while rendering open, uncertain, or light-enclosing geometry two-sided. Automatic mode prevents front-only/open props and enclosing shells from disappearing without storing a detached far shell. Authored `twoSided`/`backSided` orientation is honored whenever one-sided culling is active; the deliberately two-sided modes override it. |
| `r_shadowMapSize` | `1024` | `128..4096` | Base shadow-map resolution. Higher values cost more VRAM and GPU time. |
| `r_shadowMapAtlasSize` | `4096` | `2048..8192` | Edge size of the shared projected-light shadow atlas. Cached projected lights occupy `r_shadowMapSize`-sized cells inside it (CSM lights use a 2x2 cell block), so all cached lights stay resident in one texture. |
| `r_shadowMapFilterRadius` | `0.75` | `0..8` | Projected-light PCF radius in texels. Increase resolution for more detail; increasing this value deliberately softens and widens the edge. |
| `r_shadowMapFilterTaps` | `9` | `1..13` | Projected-light PCF tap budget. Values up to `1`, `5`, `9`, and `13` select progressively denser sample sets without changing the requested radius. |
| `r_shadowMapFilterMode` | `0` | `0..2` | Projected-light filter mode: fixed PCF, stable rotated Poisson, or experimental PCSS-lite with raw depth sampling. |
| `r_shadowMapDistantFilterScale` | `0.35` | `0..1` | Scales projected PCF and PCSS radii for parallel/global distant sources such as sky or sun lights. Their texels cover much more world space than local projectors, so a tighter kernel preserves caster silhouettes instead of over-blurring them. Set `1` to use the ordinary projected-light radii unchanged. |
| `r_shadowMapPCSSLightRadius` | `4.0` | `0..16` | Projected PCSS-lite blocker search radius in shadow texels. |
| `r_shadowMapPCSSMaxRadius` | `8.0` | `0..16` | Maximum projected PCSS-lite filter radius in shadow texels. |
| `r_shadowMapPointFilterRadius` | `1.0` | `0..8` | Point-light PCF radius in cubemap texels. Large-radius lights can cover substantial world distance per texel, so wide values can visibly erase contact detail. |
| `r_shadowMapPointFilterTaps` | `9` | `1..13` | Point-light PCF tap budget. Values up to `1`, `5`, `9`, and `13` select progressively denser sample sets without changing the requested radius. |
| `r_shadowMapPointFilterMode` | `0` | `0..1` | Point-light filter mode: fixed PCF or stable rotated Poisson. |
| `r_shadowMapDepthCompare` | `1` | `0..1` | Uses hardware comparison sampling (with hardware-filtered PCF taps) for projected depth maps. Selecting PCSS-lite (`r_shadowMapFilterMode 2`) automatically uses the manual raw-depth path instead. Set `0` if a driver has trouble with GLSL shadow samplers. |
| `r_shadowMapPointDepthCompare` | `1` | `0..1` | Uses hardware comparison sampling for point-light depth cubemaps when GLSL 1.30 support is available. |
| `r_shadowMapPointHighPrecision` | `0` | `0..1` | Stores point-light shadow depth in an fp16 color cubemap instead of packed RGBA8. The packed path quantizes finer at half the memory, and the default hardware-compare path samples the depth cubemap directly, so this only affects the manual fallback. |
| `r_shadowMapPointLights` | `1` | `0..1` | Shadow-maps point lights when `r_useShadowMap 1` is enabled. Point lights are the dominant light class in Quake 4 content, so disabling this makes shadow mapping nearly a no-op; set `0` to fall back to stencil shadows for point lights only. |
| `r_shadowMapPointSize` | `512` | `128..2048` | Point-light cube face resolution, separate from `r_shadowMapSize`: each cached point light stores six faces of color and depth, so cube resolution dominates shadow VRAM. |
| `r_shadowMapHashedAlpha` | `1` | `0..1` | Uses hashed alpha testing for perforated/alpha-tested casters when supported. |
| `r_shadowMapStableAlphaHash` | `1` | `0..1` | Seeds hashed alpha from world-space caster coordinates to reduce atlas/camera-space dither drift. |
| `r_shadowMapProjectionPad` | `0.15` | `0..1` | Padding around projected-light shadow coverage. |
| `r_shadowMapPointFarScale` | `1.25` | `1..4` | Padding multiplier for point-light shadow range. |
| `r_shadowMapCascadeCount` | `4` | `1..4` | Number of projected-light cascades when CSM is enabled. |
| `r_shadowMapCascadeDistance` | `1536` | `64..8192` | Camera-space distance covered by projected-light cascades. |
| `r_shadowMapCascadeLambda` | `0.75` | `0..1` | Mix between uniform and logarithmic cascade split placement. |
| `r_shadowMapCascadeBlend` | `0.15` | `0..0.5` | Crossfade width between projected-light cascades. |
| `r_shadowMapCascadeStabilize` | `1` | `0..1` | Snaps cascade bounds to texels to reduce shimmering. |

## Residency and Update Budgeting

openQ4 can keep opaque static depth resident and reuse it across backend views. Regular projected lights can compose moving and cutout casters over that depth each frame. Point lights still require an entirely static, opaque caster set for reuse. Translucent caster passes continue to update normally, and view-fitted CSM/global cache reuse requires an explicit opt-in.

OpenGL preserves the world's cache while drawing 2D HUD, console, and post-processing views. Changing the actual render world or map still invalidates its resident maps.

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapStaticCache` | `1` | `0..1` | Reuses opaque static depth for projected lights, and complete static-only maps for point lights. |
| `r_shadowMapStaticHysteresisFrames` | `2` | `0..120` | Frames to wait after dynamic casters disappear before a point light becomes cacheable again. Projected lights stay cached while dynamic casters move: their static tiles persist in the atlas and the moving casters are composed on top each frame. |
| `r_shadowMapResidentFrames` | `120` | `1..3600` | Frames an unused static shadow map can stay resident before its slot may be expired. |
| `r_shadowMapProjectedCacheSize` | `8` | `0..16` | Static projected-light cache slots inside the shared atlas. |
| `r_shadowMapPointCacheSize` | `12` | `0..16` | Static point-light cubemap cache slots. |
| `r_shadowMapCacheCSM` | `0` | `0..1` | Allows static-cache reuse for CSM/global shadow-map passes when their fitted projection is unchanged. Both backends refresh the map when camera movement or coverage settings change the fit. Most useful for fixed-view or otherwise stable scenes. |
| `r_shadowMapSubviewPolicy` | `1` | `0..2` | Shadow maps in mirror/remote-camera subviews: `0` renders them like main views, `1` reuses cached maps or falls back to stencil, `2` prefers stencil in subviews. When the target has no usable stencil attachment or an ownership contains map-only casters, Vulkan still renders the required fresh map rather than dropping its shadow. |
| `r_shadowMapTranslucentReceivers` | `1` | `0..1` | Lets translucent surfaces sample the shadow map like opaque receivers, matching the stencil path's `r_stencilTranslucentShadows` behavior. |

`r_shadowMapMaxUpdatesPerView` limits discretionary fresh shadow-map work. Resident cache hits do not consume that budget. When it constrains a frame, admission considers screen coverage, staleness, and view proximity. Both renderers require exact cache hits, including current caster membership and projection. A cache miss uses complete current stencil coverage or renders a required map. A light needing separate LOCAL and GLOBAL maps can begin filling its cache with a budget of one; it is no longer denied indefinitely because both passes cannot fit at once.

On both renderers, an ownership containing map-only casters can exceed the nominal budget because no equivalent complete stencil result exists. Vulkan also admits those required maps before optional maps consume bounded resources. Cache signatures include caster materials, alpha/hash settings, caster offsets, point-light range/depth mode, and fitted CSM state. Changing those inputs invalidates ordinary exact reuse.

Resident entries are scoped to the loaded render world and are invalidated on map changes. Recreated atlas or cube storage invalidates its old metadata before cache occupancy and eviction are counted. Modern OpenGL also verifies the exact world, light, GLOBAL-pass signature, resolution, storage generation, and selected atlas slot or cube immediately before use; a resource belonging to another light, pass, storage generation, or previous map is treated as unavailable. Because that path exposes one shadow resource per light and cannot compose stencil supplements, it accepts only a complete GLOBAL ownership with no distinct local/no-self caster ownership and otherwise leaves the complete shadowed frame to the classic path. GLOBAL ownership is the union of opaque and enabled translucent receivers, so a translucent-only receiver can still request a complete map; if that map cannot be prepared, the affected translucent subset returns to filtered stencil shadowing at its normal depth phase instead of being drawn unshadowed.

In the classic interaction path, projected lights can retain their opaque static geometry in the shared atlas, copy it to a working map, and draw only moving and cutout casters on top. Perforated materials always remain in these live chains, even on stationary entities: stage conditions, image animation, and texture matrices can change their coverage without moving the model. Adding or removing live casters does not by itself invalidate the opaque static depth.

The copy still costs GPU time, especially with several large cascades; cache reuse is not free. `r_shadowMapCacheCSM` remains off by default. The experimental shared interaction path keeps conservative cache admission where live composition is unavailable.

Normal cache hits preserve the exact floating-point projection and caster transforms. Cascaded-map keys include the fitted clip planes and receiver parameters, so changes to cascade blend overlap, PCSS guards, or distant-light filter scaling cannot silently retain an obsolete fit. Stabilization reserves space for center snapping and moves edge crops back inside the projection without trimming their resolution. Budgets and subviews cannot bypass these cache checks.

### Doors and other movers

A resting door may enter static depth. When it moves, its old static membership must invalidate that depth before its current pose is composed into a projected map. Adding the moving leaf over an old closed-door map would leave a ghost blocker. Point maps with moving casters regenerate. Changed portal connectivity also changes the participating caster and receiver sets; returning after a door moved elsewhere must validate them again.

Freezing single-player simulation with `g_stopTime 1` also holds the door's rendered pose and material clock. This allows consistent comparisons of cached and freshly rendered shadows at the same point in its travel.

The Vulkan shadow-light table supports 256 lights, with space for both receiver ownerships and matching descriptor capacity. Cubemap images remain allocated on demand. This removes the earlier 64-light failure seen in a stock Air Defense 2 door view; it does not remove hardware, atlas, or memory limits. Dense scenes containing more lights than the resident cache can still require substantial fresh shadow work.

The [door regression report](../dev/shadowmapping-movers-2026-09-05.md) covers real paired doors, portal state, opening and reversal, point lights, the weapon flashlight, and return after off-screen movement. `testDoor <entity name> [open|close]` is a single-player cheat diagnostic: omit the action to report every leaf's physics state and portal blocking flags. It uses the normal door movement logic.

## Transparency Shadowing

### Cutout / Alpha-Tested Materials

These are materials with holes cut by alpha test, such as:
- Chain-link fences
- Grates
- Leaf cards
- Other perforated or masked surfaces

Behavior:
- They cast cutout shadows in both projected and point shadow-map paths.
- `r_shadowMapHashedAlpha 1` is the recommended mode and is enabled by default.
- Supported perforated stages with explicit texture coordinates can use hashed alpha or hard alpha-test shadowing. OpenGL can use conservative solid depth for unsupported coverage stages; Vulkan retains stencil fallback for unsupported cinematic, dynamic-image, or generated-coordinate coverage. These cases still prevent removing the stencil implementation entirely.
- Translucent shadow coverage stages preserve the same material alpha-test mode, so blended foliage/glass masks stay consistent with opaque cutouts.

Hashed alpha notes:
- Usually gives more stable distant foliage/fence shadows than hard binary alpha test.
- Can look slightly noisy on some surfaces.
- If you prefer harder but cleaner cutout edges, set `r_shadowMapHashedAlpha 0`.

### Blended / Translucent Materials

These are materials with real blending instead of alpha-test cutouts, such as:
- Some glass-like surfaces
- Certain layered blended materials
- Some effect-style soft transparency

Behavior:
- Off by default.
- Optional with `r_shadowMapTranslucentMoments 1`.
- Implemented as an additional experimental translucent shadow overlay on top of the main shadow map.

Current limits:
- Translucent moments are an OpenGL experiment; enabling the CVar on Vulkan does not establish equivalent translucent shadowing.
- Supported stages currently include old-style alpha and premultiplied-alpha stages with explicit ST texture coordinates, plus common additive `blend add` / `GL_ONE, GL_ONE` stages.
- When a translucent shell/tint stage is layered on top of a separate explicit-ST coverage stage, openQ4 now reuses that coverage stage, including its alpha-test threshold when present, so layered pickup-orb and similar materials can cast shaped transmitted shadows instead of only uniform blobs.
- Supported translucent casters now derive colored transmission from the material inputs available to that stage: texture alpha, sampled texture RGB, stage color, and applicable vertex color.
- View-dependent reflection cubemaps are treated as tinted transmissive shells instead of using the reflected sample directly, so pickup orbs can tint transmitted light without camera-dependent shadow color shifts.
- The current high-quality path stores separate translucent shadow moments for red, green, and blue, so each channel resolves blocker depth independently instead of sharing one grayscale depth distribution.
- GUI/subview materials are skipped.
- BSE/FX particles, unusual custom stage setups, and many effect-style materials are intentionally not forced into the translucent shadow pass.
- Colored transmission is still approximate rather than a full deep-shadow solution, but it is materially closer to real tinted transmission than the earlier scalar/grayscale model.
- This path adds extra GPU work because eligible lights render an additional translucent caster pass.
- The feature now expects enough hardware for 3 translucent MRT attachments and 3 extra texture samplers in the receiver path; if that is unavailable, openQ4 disables this experimental translucent-shadow feature.

Controls:

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapTranslucentMoments` | `0` | `0..1` | Enables the experimental blended/translucent shadow overlay. |
| `r_shadowMapTranslucentDensity` | `1.0` | `0..8` | Scales resolved translucent-shadow strength. |
| `r_shadowMapTranslucentMinAlpha` | `0.02` | `0..1` | Ignores very faint translucent stages below this alpha. |
| `r_shadowMapTranslucentFilterRadius` | `-1.0` | `-1..8` | Translucent moment filter radius in texels. `-1` inherits the opaque shadow filter radius. |
| `r_shadowMapTranslucentMinVariance` | `0.00001` | `0.000001..0.01` | Minimum variance used when resolving translucent shadow moments. |
| `r_shadowMapTranslucentBleedReduction` | `0.0` | `0..0.95` | Reduces light bleed in the translucent moment resolve. Higher values can make translucent shadows harsher. |

Suggested use:
- Start with `r_shadowMapTranslucentMoments 1`, `r_shadowMapTranslucentDensity 1.0`, `r_shadowMapTranslucentMinAlpha 0.02`.
- Raise `r_shadowMapTranslucentDensity` if the effect looks too weak.
- Raise `r_shadowMapTranslucentMinAlpha` if very faint blended surfaces create more shadowing than you want.
- Use `r_shadowMapTranslucentFilterRadius` when translucent shadows need a different softness than opaque shadows.
- Increase `r_shadowMapTranslucentBleedReduction` only when moment light bleed is visibly worse than the added hardness.
- Turn the feature back off if you want the most predictable performance and compatibility.

## Bias and Artifact Tuning

Shadow artifacts are usually one of two classes:
- Shadow acne or speckling: bias is too low.
- Peter Panning or detached shadows: bias is too high.

Projected-light tuning:

Projected shadow maps store Quake 4's authored light falloff depth directly, so the projected defaults are intentionally small. Raise them only when you see acne or speckling.

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapBias` | `0.00016` | `0..0.05` | Constant receiver depth bias for projected lights. |
| `r_shadowMapNormalBias` | `0.00075` | `0..0.05` | Extra projected-light bias on sloped receivers. |
| `r_shadowMapTexelBiasScale` | `0.45` | `0..8` | Uses texel-aware receiver bias based on fitted cascade/light footprint. Constant bias acts as a compatibility floor. |
| `r_shadowMapNormalOffsetScale` | `1.0` | `0..8` | Normal-offset bias in shadow texels. It helps slope acne but can move contact edges when set too high, especially on lights whose texels cover a large world-space footprint. |
| `r_shadowMapReceiverPlaneBias` | `0` | `0..1` | Enables the experimental derivative receiver-plane approximation. Keep it disabled for ordinary play. |
| `r_shadowMapPolygonFactor` | `0.25` | `0..16` | Slope-scale caster depth offset applied by the caster shaders while rendering shadow maps (shadow casters write shader depth, which `glPolygonOffset` cannot bias). |
| `r_shadowMapPolygonOffset` | `0.5` | `0..64` | Constant caster depth offset in resolvable depth-buffer steps, applied by the caster shaders alongside the slope-scale term. |

Point-light tuning:

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapPointBias` | `0.00010` | `0..0.05` | Constant receiver depth bias for point lights. |
| `r_shadowMapPointNormalBias` | `0.0010` | `0..0.05` | Extra point-light bias on sloped receivers. |
| `r_shadowMapPointMaxWorldBias` | `4.0` | `0..64` | Conservative world-space ceiling for the combined point-light receiver depth and normal-offset bias. It prevents huge-radius lights from turning small normalized values into large contact gaps. `0` disables the ceiling for comparison. |

Point-light constant and texel-aware receiver bias use the larger value rather than stacking both terms. The slope-amplified texel-aware term is bounded internally, and the complete receiver-depth plus normal-offset budget is scaled together when it would exceed `r_shadowMapPointMaxWorldBias`.

Practical advice:
- Raise bias values slowly in very small steps.
- If shadows detach from contact points, lower the relevant bias before changing many other settings.
- Existing configs may keep older archived bias values. If detached shadows persist after updating, compare your local cvars against the defaults above.
- If CSM shimmers while moving the camera, keep `r_shadowMapCascadeStabilize 1`.
- Wider filter radii usually need more careful bias tuning.

## Debugging and Diagnostics

| Setting / Command | Default | What it does |
|---|---:|---|
| `r_shadowMapDebugMode` | `0` | Projected-light shadow debug mode. |
| `r_shadowMapDebugOverlay` | `0` | Draws a top-left mini-map of the selected shadow map plus frame counters. |
| `r_shadowMapReport` | `0` | Shadow-map diagnostics: `0` off, `1` summary, `2` per-light decisions, `3` verbose receiver-submit decisions. |
| `r_shadowMapReportInterval` | `30` | Frames between report prints when `r_shadowMapReport` is enabled. |
| `r_shadowMapMaxUpdatesPerView` | `0` | Optional per-view update budget; `0` means unlimited. Both renderers require exact cache hits. Budget misses use stencil only when it is complete, and may exceed the limit for map-only casters. Two-pass lights can fill their cache over successive views. |
| `r_shadowMapGpuTimerQueries` | `1` | Uses non-blocking GL timer queries for shadow-map GPU timing when the driver supports them. |
| `r_shadowMapGpuSyncTimings` | `0` | Diagnostic-only GPU-synchronized pass timing using `glFinish`; leave off during normal play. |
| `reportShaderPrograms` | n/a | Prints current ARB/GLSL shader validity, including shadow programs. |

If Vulkan cannot submit either a required shadow map or the matching stencil
ownership for one frame, it keeps that receiver's direct light unshadowed and
logs the degradation. This avoids a complete light pop-out; the sticky fallback
requests stencil ownership for later frames.

`r_shadowMapDebugMode` values:
- `0`: Off
- `1`: Projected shadow atlas/depth
- `2`: Cascade index
- `3`: Projected UV
- `4`: Projected depth
- `5`: Projected W
- `6`: Invalid mask
- `7`: Bias heatmap
- `8`: Bias off
- `9`: PCF off
- `10`: Caster polygon offset off
- `11`: Receiver-plane/normal bias off
- `12`: Compare-depth delta
- `13`: Receiver eligibility
- `14`: Receiver fallback reason

Useful workflow:
1. Enable `r_useShadowMap 1`.
2. Set `r_shadowMapDebugMode 1` to inspect projected atlas/depth content.
3. Set `r_shadowMapDebugOverlay 1` to keep a live mini-map in the top-left corner while you play.
4. Use `r_singleLight` to lock the overlay to one light; otherwise OpenGL follows the last successfully rendered mapped light that frame, and Vulkan shows the first light in its prepared table (stable while the view is).
5. The overlay stats are:
   `POINT` / `PROJ` = light type, `L` / `G` = local/global interaction pass, `F` / `C` = point faces or projected cascades, `MAP` / `FB` = whether the selected pass stayed on shadow maps or fell back. On OpenGL the extra caster row reports alpha, translucent, rejected, and expanded off-screen caster counts. Vulkan reports its own accounting instead: the selected pass's cache decision (`SCRATCH`, `REUSE`, `PUB`, or either with `+DYN` when this view's dynamic casters composed over cached static depth) and its tile size and caster split, then `HIT` projected/point cache hits, `NEW` projected/point fresh updates, `CMP` composed passes, `TILE` atlas tiles rendered against allocated, `FACE` cube faces rendered, and `FB` budget/admission/subview fallbacks.
6. Point-light overlay tiles are face indices `0..5` in a `3x2` layout:
   `0 = +X`, `1 = -X`, `2 = +Y`, `3 = -Y`, `4 = +Z`, `5 = -Z`.
7. Use `reportShaderPrograms` if the scene looks unlit or obviously wrong.
8. Use `r_shadowMapReport 1`, `2`, or `3` for live diagnostic logging. Summary lines include point depth-compare usage as `pointCmp`, timer-query totals as `gpuQuery`, and the `SM cache:` line reports cache hits, misses, resident reuse, budget reuse, evictions, active projected/point cache slots, and resident shadow VRAM. The `SM metrics:` line reports per-frame updates, composed static/dynamic layer passes (`composed`), subview reuse/fallback activity, translucent receiver chains drawn with shadow sampling, and importance-budget denials. The `SM modern:` line summarizes the modern path's consumption of the persistent atlas (live atlas slots, mapped/cache-reuse lights, and per-light blocking counts). Verbose projected CSM bias lines include per-cascade near/far range, fitted depth range, world texel size, and clip-space depth extent. Level `3` adds receiver-submit diagnostics for mapped-light cases that had visible receivers but no GLSL interaction submissions.
   Per-light lines separate semantic light class from backing shadow-map resource shape. For example, a stock authored parallel light may report `class=parallel type=point` when it uses the point-resource path.
   Vulkan reports cache work after rendering the view, so `tiles`, `pointFaces`, and `composed` describe completed work. `casterFaces=C/T culled/tested` counts candidate point-caster submissions rejected before geometry binding by conservative cube-face bounds tests. It is not an FPS or whole-frame performance measurement.
9. When rejected casters are present, the `SM caster-reject:` line breaks them down by reason, such as view-only entities, depth-hack models, disconnected portal areas, GUI/subview surfaces, non-shadowing materials, or disabled/unsupported translucent casters.

## Troubleshooting

- If shadows do not appear at all, check `r_shadows 1` and `r_useShadowMap 1`, then run `vid_restart`.
- If projected-light shadows shimmer while moving, keep `r_shadowMapCSM 1` and `r_shadowMapCascadeStabilize 1`.
- Parallel/global sky shadows automatically use the tighter `r_shadowMapDistantFilterScale 0.35` policy. If a particular outdoor scene still looks too soft, lower it toward `0`; if you want the old shared projected-light softness, set it to `1`.
- If cutout materials cast solid-looking shadows, check the material's alpha-test and explicit texture coordinates. Unsupported coverage can use conservative solid depth on OpenGL or stencil fallback on Vulkan. Changing `r_shadowMapHashedAlpha` cannot add support for an unsupported stage type.
- If translucent shadows are too strong or too noisy, lower `r_shadowMapTranslucentDensity` or disable `r_shadowMapTranslucentMoments`.
- If blended materials still do not cast translucent shadows, that material may be outside the currently supported stage set. Common additive pickup orbs are supported, but many particle/effect materials still are not.
- If point-light shadows look too detached, first confirm `r_shadowMapCasterCulling 2` and `r_shadowMapPointMaxWorldBias 4`; then reduce `r_shadowMapPointFilterRadius`, `r_shadowMapPointBias`, or `r_shadowMapPointNormalBias` in small steps.
- If projected-light shadows look detached, reduce `r_shadowMapBias`, `r_shadowMapNormalBias`, `r_shadowMapTexelBiasScale`, `r_shadowMapPolygonFactor`, or `r_shadowMapPolygonOffset` in small steps.
- If projected-light acne appears only with a large filter radius, prefer a higher shadow-map resolution or a narrower radius before increasing bias. The experimental receiver-plane approximation remains off by default.
- If PCSS-lite seems unchanged, confirm `r_shadowMapFilterMode 2` is active; the renderer selects the manual raw-depth path for that mode automatically.
- If point-light depth compare causes shader trouble on a driver, leave `r_shadowMapPointDepthCompare 0`; the renderer falls back to the high-precision color-depth cubemap path.
- If static-cache reuse hides expected updates while testing unusual content, set `r_shadowMapStaticCache 0` or reduce `r_shadowMapResidentFrames`.
- If performance drops sharply after enabling translucent shadowing, turn off `r_shadowMapTranslucentMoments` first; it adds an extra pass for eligible lights.

## Platform Support Matrix

| Platform | Shadow maps | Notes |
|---|---|---|
| Windows (GL3.3+) | Supported | Primary development and validation platform; the bit-exact regression net runs here. |
| Linux (GL3.3+) | Supported | Same GL feature path as Windows. |
| Steam Deck | Supported | Recommended: `r_shadowMapSize 1024`, `r_shadowMapPointSize 512`, `r_shadowMapMaxUpdatesPerView 2`, `r_shadowMapCSM 1` with `r_shadowMapCascadeCount 3`. The importance-ordered update budget keeps per-frame shadow cost bounded. |
| macOS (Apple legacy GL2.1 tier) | Stencil only | The Apple compatibility corridor lacks the GLSL/FBO feature set the shadow-map receiver and caster programs require. Lights fall back to the retail stencil path automatically; this is expected and documented behavior, not an error. |
| macOS (opt-in Vulkan through MoltenVK) | Implemented, untested on Apple hardware | Requires `r_renderApi vulkan` and a full engine restart. MoltenVK is a Vulkan-on-Metal translation layer bundled in both macOS packages, not a Metal renderer. This path uses the same Vulkan shadow implementation as Windows and Linux — projected, point, parallel, and global lights, cascades, PCF/PCSS-lite, and static caching — but no real-Mac evidence exists yet, so treat it as experimental and expect problems. The default macOS renderer is still OpenGL, which uses the row above. |

The shadow-map pipeline rejects incomplete maps and uses complete stencil coverage when available. If Vulkan temporarily has neither usable representation, it preserves direct illumination and logs the missing resource while requesting stencil ownership for later frames. This recovery case and unsupported material/platform paths still prevent treating shadow mapping as a universal replacement.

## Summary

Recommended default user setup:
- `r_shadows 1`
- `r_useShadowMap 1`
- `r_shadowMapCSM 1`
- `r_shadowMapHashedAlpha 1`
- `r_shadowMapCascadeStabilize 1`
- `r_shadowMapStaticCache 1`

Only enable `r_shadowMapTranslucentMoments 1` if you specifically want experimental blended transparency shadows and accept the extra cost and current material-support limits.

Authored `noShadows` and `DECAL_MACRO` materials remain non-casting under both
the translucent stencil option and translucent moment maps. `forceShadows`
retains precedence when a material explicitly requests it. This prevents lit
slime and other decals from becoming solid shadow blockers.

For repeatable stock-map comparisons, see the [individual map test report](../dev/shadowmapping-map-tests-2026-09-05.md) and `tools/tests/renderer_shadow_mapping_maps.py`. It captures mapped, stencil, and disabled shadows in isolated windowed SP/MP runs; images still require visual inspection.

The runner also accepts `--scenario flashlight-cycle`, `caster-cycle`,
`caster-cycle-point`, `caster-cycle-cutout`, `emitter-lift`, `resource-cycle`, or `cascade-motion` to exercise changes
during gameplay. See the [transition test report](../dev/shadowmapping-transition-tests-2026-09-05.md)
for commands, coverage, and interpretation of animated-scene comparisons.

The [replacement progress report](../dev/shadowmapping-replacement-progress-2026-09-05.md)
records the next round's cutout-cache, emitter, and point-face improvements,
retained map tests, measured shadow-pass costs, and remaining stencil dependencies.

Use `--random-maps 4 --seed 20260905` to sample the stock SP launch catalog
reproducibly. The runner saves the candidate pool and chosen maps, verifies the
requested renderer module, and checks the Air Defense 1 reference camera after
its presentation pose has updated. See the [final audit and random-map report](../dev/shadowmapping-final-audit-2026-09-05.md)
for the decal repair, comparison evidence, and remaining limitations.
