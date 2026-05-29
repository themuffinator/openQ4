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
- If the shadow-map path is unavailable or fails for a light, openQ4 falls back to the legacy shadow path instead of leaving the light unshadowed.
- Point lights use the legacy stencil shadow path by default; enable `r_shadowMapPointLights 1` only when testing the experimental point-light cubemap path.
- Lights touching animated, deformed, or packed character receivers can also fall back to the legacy stencil path so stock character lighting, mirrored seams, and eye materials retain retail-style interaction behavior.
- Modern renderer diagnostics keep lighting visible when shadow-map receiver sampling is not ready, but full modern visible-frame replacement stays fail-closed so the legacy path continues to provide the actual shadowed frame.
- Most shadow cvars can be changed live, but `vid_restart` is the safest way to apply large changes such as map resolution, cascade layout, or switching the shadow pipeline on/off.

## What the System Does

openQ4 currently supports:
- Projected-light shadow maps for regular projected lights.
- Experimental point-light shadow maps for omni/point lights when `r_shadowMapPointLights 1` is enabled.
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
seta r_shadowMapFilterRadius 2.0
seta r_shadowMapFilterTaps 13
seta r_shadowMapPointFilterRadius 2.5
seta r_shadowMapPointFilterTaps 13
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
seta r_shadowMapCascadeCount 4
seta r_shadowMapFilterRadius 2.5
seta r_shadowMapFilterTaps 13
seta r_shadowMapPointFilterRadius 3.0
seta r_shadowMapPointFilterTaps 13
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

For projected lights, `r_shadowMapFilterMode 2` enables the experimental PCSS-lite path when raw depth sampling is active. Leave `r_shadowMapDepthCompare 0` for that mode; hardware depth-compare sampling cannot run the blocker search needed for PCSS-lite.

### Performance-Focused

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 0
seta r_shadowMapSize 512
seta r_shadowMapFilterRadius 1.5
seta r_shadowMapFilterTaps 5
seta r_shadowMapPointFilterRadius 2.0
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
| `r_shadowMapSize` | `1024` | `128..4096` | Base shadow-map resolution. Higher values cost more VRAM and GPU time. |
| `r_shadowMapFilterRadius` | `2.0` | `0..8` | Projected-light PCF filter radius in texels. |
| `r_shadowMapFilterTaps` | `13` | `1..13` | Projected-light PCF tap budget. Values up to `1`, `5`, `9`, and `13` select progressively wider sample sets. |
| `r_shadowMapFilterMode` | `0` | `0..2` | Projected-light filter mode: fixed PCF, stable rotated Poisson, or experimental PCSS-lite with raw depth sampling. |
| `r_shadowMapPCSSLightRadius` | `4.0` | `0..16` | Projected PCSS-lite blocker search radius in shadow texels. |
| `r_shadowMapPCSSMaxRadius` | `8.0` | `0..16` | Maximum projected PCSS-lite filter radius in shadow texels. |
| `r_shadowMapPointFilterRadius` | `2.5` | `0..8` | Point-light PCF filter radius in texels. |
| `r_shadowMapPointFilterTaps` | `13` | `1..13` | Point-light PCF tap budget. Values up to `1`, `5`, `9`, and `13` select progressively wider sample sets. |
| `r_shadowMapPointFilterMode` | `0` | `0..1` | Point-light filter mode: fixed PCF or stable rotated Poisson. |
| `r_shadowMapDepthCompare` | `0` | `0..1` | Uses hardware comparison sampling for projected depth maps. Leave off if a driver has trouble with GLSL shadow samplers. |
| `r_shadowMapPointDepthCompare` | `0` | `0..1` | Uses hardware comparison sampling for point-light depth cubemaps when GLSL 1.30 support is available. |
| `r_shadowMapPointHighPrecision` | `1` | `0..1` | Stores point-light shadow depth in a high-precision float cubemap instead of the older packed RGBA8 depth path. |
| `r_shadowMapPointLights` | `0` | `0..1` | Opts into experimental point-light shadow maps. When disabled, point lights use the legacy stencil shadow path even if `r_useShadowMap 1` is enabled. |
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

openQ4 can keep static-only shadow maps resident and reuse them across backend views. This is enabled by default for regular projected and point lights. Dynamic casters, translucent caster passes, and view-fitted CSM/global passes are conservative by default and continue to update normally unless explicitly opted in.

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapStaticCache` | `1` | `0..1` | Reuses resident shadow maps for static-only projected and point lights. |
| `r_shadowMapStaticHysteresisFrames` | `2` | `0..120` | Frames to wait after dynamic casters disappear before a light becomes cacheable again. |
| `r_shadowMapResidentFrames` | `120` | `1..3600` | Frames an unused static shadow map can stay resident before its slot may be expired. |
| `r_shadowMapProjectedCacheSize` | `4` | `0..8` | Static projected-light cache slots. |
| `r_shadowMapPointCacheSize` | `4` | `0..8` | Static point-light cubemap cache slots. |
| `r_shadowMapCacheCSM` | `0` | `0..1` | Allows static-cache reuse for CSM/global shadow-map passes. Leave off unless you are testing a fixed-view or otherwise stable scene. |

`r_shadowMapMaxUpdatesPerView` still limits dynamic shadow-map work. Resident cache hits do not consume that update budget, so static light reuse can reduce update pressure without starving moving lights.

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
- If a perforated stage cannot use the hashed path, openQ4 falls back to its classic alpha-test caster behavior instead of dropping the shadow.

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

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapBias` | `0.010` | `0..0.05` | Constant receiver depth bias for projected lights. |
| `r_shadowMapNormalBias` | `0.020` | `0..0.05` | Extra projected-light bias on sloped receivers. |
| `r_shadowMapTexelBiasScale` | `1.0` | `0..8` | Uses texel-aware receiver bias based on fitted cascade/light footprint. Constant bias acts as a compatibility floor. |
| `r_shadowMapReceiverPlaneBias` | `0` | `0..1` | Allows derivative receiver-plane bias for wider projected-light filters. |
| `r_shadowMapPolygonFactor` | `2.0` | `0..16` | Slope-scale caster polygon offset used while rendering projected shadow maps. |
| `r_shadowMapPolygonOffset` | `4.0` | `0..64` | Constant caster polygon offset used while rendering projected shadow maps. |

Point-light tuning:

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapPointBias` | `0.00020` | `0..0.05` | Constant receiver depth bias for point lights. |
| `r_shadowMapPointNormalBias` | `0.0020` | `0..0.05` | Extra point-light bias on sloped receivers. |

Point-light constant and texel-aware receiver bias also use the larger value rather than stacking both terms, so tuning one path does not automatically over-bias the other.

Practical advice:
- Raise bias values slowly in very small steps.
- If shadows detach from contact points, lower the relevant bias before changing many other settings.
- If CSM shimmers while moving the camera, keep `r_shadowMapCascadeStabilize 1`.
- Wider filter radii usually need more careful bias tuning.

## Debugging and Diagnostics

| Setting / Command | Default | What it does |
|---|---:|---|
| `r_shadowMapDebugMode` | `0` | Projected-light shadow debug mode. |
| `r_shadowMapDebugOverlay` | `0` | Draws a top-left mini-map of the selected shadow map plus frame counters. |
| `r_shadowMapReport` | `0` | Shadow-map diagnostics: `0` off, `1` summary, `2` per-light decisions, `3` verbose receiver-submit decisions. |
| `r_shadowMapReportInterval` | `30` | Frames between report prints when `r_shadowMapReport` is enabled. |
| `r_shadowMapMaxUpdatesPerView` | `0` | Optional per-view shadow-map update budget. `0` means unlimited. |
| `r_shadowMapGpuTimerQueries` | `1` | Uses non-blocking GL timer queries for shadow-map GPU timing when the driver supports them. |
| `r_shadowMapGpuSyncTimings` | `0` | Diagnostic-only GPU-synchronized pass timing using `glFinish`; leave off during normal play. |
| `reportShaderPrograms` | n/a | Prints current ARB/GLSL shader validity, including shadow programs. |

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

Useful workflow:
1. Enable `r_useShadowMap 1`.
2. Set `r_shadowMapDebugMode 1` to inspect projected atlas/depth content.
3. Set `r_shadowMapDebugOverlay 1` to keep a live mini-map in the top-left corner while you play.
4. Use `r_singleLight` to lock the overlay to one light; otherwise it follows the last successfully rendered mapped light that frame.
5. The overlay stats are:
   `POINT` / `PROJ` = light type, `L` / `G` = local/global interaction pass, `F` / `C` = point faces or projected cascades, `MAP` / `FB` = whether the selected pass stayed on shadow maps or fell back. The extra caster row reports alpha, translucent, rejected, and expanded off-screen caster counts.
6. Point-light overlay tiles are face indices `0..5` in a `3x2` layout:
   `0 = +X`, `1 = -X`, `2 = +Y`, `3 = -Y`, `4 = +Z`, `5 = -Z`.
7. Use `reportShaderPrograms` if the scene looks unlit or obviously wrong.
8. Use `r_shadowMapReport 1`, `2`, or `3` for live diagnostic logging. Summary lines include point depth-compare usage as `pointCmp`, timer-query totals as `gpuQuery`, and the `SM cache:` line reports cache hits, misses, resident reuse, budget reuse, evictions, and active projected/point cache slots. Verbose projected CSM bias lines include per-cascade near/far range, fitted depth range, world texel size, and clip-space depth extent. Level `3` adds receiver-submit diagnostics for mapped-light cases that had visible receivers but no GLSL interaction submissions.
   Per-light lines separate semantic light class from backing shadow-map resource shape. For example, a stock authored parallel light may report `class=parallel type=point` when it uses the point-resource path.
9. When rejected casters are present, the `SM caster-reject:` line breaks them down by reason, such as view-only entities, depth-hack models, disconnected portal areas, GUI/subview surfaces, non-shadowing materials, or disabled/unsupported translucent casters.

## Troubleshooting

- If shadows do not appear at all, check `r_shadows 1` and `r_useShadowMap 1`, then run `vid_restart`.
- If projected-light shadows shimmer while moving, keep `r_shadowMapCSM 1` and `r_shadowMapCascadeStabilize 1`.
- If cutout materials cast solid-looking shadows, make sure `r_shadowMapHashedAlpha 1` is enabled and the material is actually alpha-tested rather than blended.
- If translucent shadows are too strong or too noisy, lower `r_shadowMapTranslucentDensity` or disable `r_shadowMapTranslucentMoments`.
- If blended materials still do not cast translucent shadows, that material may be outside the currently supported stage set. Common additive pickup orbs are supported, but many particle/effect materials still are not.
- If point-light shadows look too detached, reduce `r_shadowMapPointBias` or `r_shadowMapPointNormalBias`.
- If projected-light shadows look detached, reduce `r_shadowMapBias`, `r_shadowMapNormalBias`, `r_shadowMapTexelBiasScale`, `r_shadowMapPolygonFactor`, or `r_shadowMapPolygonOffset` in small steps.
- If projected-light acne appears only with large filter radii, try `r_shadowMapReceiverPlaneBias 1` before greatly increasing constant bias.
- If PCSS-lite seems unchanged, confirm `r_shadowMapFilterMode 2` is active and `r_shadowMapDepthCompare 0` is still selected.
- If point-light depth compare causes shader trouble on a driver, leave `r_shadowMapPointDepthCompare 0`; the renderer falls back to the high-precision color-depth cubemap path.
- If static-cache reuse hides expected updates while testing unusual content, set `r_shadowMapStaticCache 0` or reduce `r_shadowMapResidentFrames`.
- If performance drops sharply after enabling translucent shadowing, turn off `r_shadowMapTranslucentMoments` first; it adds an extra pass for eligible lights.

## Summary

Recommended default user setup:
- `r_shadows 1`
- `r_useShadowMap 1`
- `r_shadowMapCSM 1`
- `r_shadowMapHashedAlpha 1`
- `r_shadowMapCascadeStabilize 1`
- `r_shadowMapStaticCache 1`

Only enable `r_shadowMapTranslucentMoments 1` if you specifically want experimental blended transparency shadows and accept the extra cost and current material-support limits.
