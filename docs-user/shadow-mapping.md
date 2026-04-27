# Shadow Mapping and Transparency Shadowing Guide

This guide covers OpenQ4's user-facing shadow-map settings, including projected-light shadow maps, point-light shadow maps, cascaded shadow maps (CSM), alpha-tested transparency shadows, and the current experimental translucent-shadow path.

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
- If the shadow-map path is unavailable or fails for a light, OpenQ4 falls back to the legacy shadow path instead of leaving the light unshadowed.
- Most shadow cvars can be changed live, but `vid_restart` is the safest way to apply large changes such as map resolution, cascade layout, or switching the shadow pipeline on/off.

## What the System Does

OpenQ4 currently supports:
- Projected-light shadow maps for regular projected lights.
- Point-light shadow maps for omni/point lights.
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
seta r_shadowMapPointFilterRadius 2.5
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
seta r_shadowMapPointFilterRadius 3.0
seta r_shadowMapHashedAlpha 1
seta r_shadowMapCascadeStabilize 1
vid_restart
```

### Performance-Focused

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 0
seta r_shadowMapSize 512
seta r_shadowMapFilterRadius 1.5
seta r_shadowMapPointFilterRadius 2.0
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
| `r_shadowMapSize` | `1024` | `128..4096` | Base shadow-map resolution. Higher values cost more VRAM and GPU time. |
| `r_shadowMapFilterRadius` | `2.0` | `0..8` | Projected-light PCF filter radius in texels. |
| `r_shadowMapPointFilterRadius` | `2.5` | `0..8` | Point-light PCF filter radius in texels. |
| `r_shadowMapHashedAlpha` | `1` | `0..1` | Uses hashed alpha testing for perforated/alpha-tested casters when supported. |
| `r_shadowMapProjectionPad` | `0.15` | `0..1` | Padding around projected-light shadow coverage. |
| `r_shadowMapPointFarScale` | `1.25` | `1..4` | Padding multiplier for point-light shadow range. |
| `r_shadowMapCascadeCount` | `4` | `1..4` | Number of projected-light cascades when CSM is enabled. |
| `r_shadowMapCascadeDistance` | `1536` | `64..8192` | Camera-space distance covered by projected-light cascades. |
| `r_shadowMapCascadeLambda` | `0.75` | `0..1` | Mix between uniform and logarithmic cascade split placement. |
| `r_shadowMapCascadeBlend` | `0.15` | `0..0.5` | Crossfade width between projected-light cascades. |
| `r_shadowMapCascadeStabilize` | `1` | `0..1` | Snaps cascade bounds to texels to reduce shimmering. |

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
- If a perforated stage cannot use the hashed path, OpenQ4 falls back to its classic alpha-test caster behavior instead of dropping the shadow.

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
- When a translucent shell/tint stage is layered on top of a separate explicit-ST coverage stage, OpenQ4 now reuses that coverage stage, including its alpha-test threshold when present, so layered pickup-orb and similar materials can cast shaped transmitted shadows instead of only uniform blobs.
- Supported translucent casters now derive colored transmission from the material inputs available to that stage: texture alpha, sampled texture RGB, stage color, and applicable vertex color.
- View-dependent reflection cubemaps are treated as tinted transmissive shells instead of using the reflected sample directly, so pickup orbs can tint transmitted light without camera-dependent shadow color shifts.
- The current high-quality path stores separate translucent shadow moments for red, green, and blue, so each channel resolves blocker depth independently instead of sharing one grayscale depth distribution.
- GUI/subview materials are skipped.
- BSE/FX particles, unusual custom stage setups, and many effect-style materials are intentionally not forced into the translucent shadow pass.
- Colored transmission is still approximate rather than a full deep-shadow solution, but it is materially closer to real tinted transmission than the earlier scalar/grayscale model.
- This path adds extra GPU work because eligible lights render an additional translucent caster pass.
- The feature now expects enough hardware for 3 translucent MRT attachments and 3 extra texture samplers in the receiver path; if that is unavailable, OpenQ4 disables this experimental translucent-shadow feature.

Controls:

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapTranslucentMoments` | `0` | `0..1` | Enables the experimental blended/translucent shadow overlay. |
| `r_shadowMapTranslucentDensity` | `1.0` | `0..8` | Scales resolved translucent-shadow strength. |
| `r_shadowMapTranslucentMinAlpha` | `0.02` | `0..1` | Ignores very faint translucent stages below this alpha. |

Suggested use:
- Start with `r_shadowMapTranslucentMoments 1`, `r_shadowMapTranslucentDensity 1.0`, `r_shadowMapTranslucentMinAlpha 0.02`.
- Raise `r_shadowMapTranslucentDensity` if the effect looks too weak.
- Raise `r_shadowMapTranslucentMinAlpha` if very faint blended surfaces create more shadowing than you want.
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
| `r_shadowMapPolygonFactor` | `2.0` | `0..16` | Slope-scale caster polygon offset used while rendering projected shadow maps. |
| `r_shadowMapPolygonOffset` | `4.0` | `0..64` | Constant caster polygon offset used while rendering projected shadow maps. |

Point-light tuning:

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapPointBias` | `0.00020` | `0..0.05` | Constant receiver depth bias for point lights. |
| `r_shadowMapPointNormalBias` | `0.0020` | `0..0.05` | Extra point-light bias on sloped receivers. |

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
| `r_shadowMapReport` | `0` | Shadow-map diagnostics: `0` off, `1` summary, `2` per-light decisions. |
| `r_shadowMapReportInterval` | `30` | Frames between report prints when `r_shadowMapReport` is enabled. |
| `reportShaderPrograms` | n/a | Prints current ARB/GLSL shader validity, including shadow programs. |

`r_shadowMapDebugMode` values:
- `0`: Off
- `1`: Projected shadow atlas/depth
- `2`: Cascade index
- `3`: Projected UV
- `4`: Projected depth
- `5`: Projected W
- `6`: Invalid mask

Useful workflow:
1. Enable `r_useShadowMap 1`.
2. Set `r_shadowMapDebugMode 1` to inspect projected atlas/depth content.
3. Set `r_shadowMapDebugOverlay 1` to keep a live mini-map in the top-left corner while you play.
4. Use `r_singleLight` to lock the overlay to one light; otherwise it follows the last successfully rendered mapped light that frame.
5. The overlay stats are:
   `POINT` / `PROJ` = light type, `L` / `G` = local/global interaction pass, `F` / `C` = point faces or projected cascades, `MAP` / `FB` = whether the selected pass stayed on shadow maps or fell back.
6. Point-light overlay tiles are face indices `0..5` in a `3x2` layout:
   `0 = +X`, `1 = -X`, `2 = +Y`, `3 = -Y`, `4 = +Z`, `5 = -Z`.
7. Use `reportShaderPrograms` if the scene looks unlit or obviously wrong.
8. Use `r_shadowMapReport 1` or `2` for live diagnostic logging.

## Troubleshooting

- If shadows do not appear at all, check `r_shadows 1` and `r_useShadowMap 1`, then run `vid_restart`.
- If projected-light shadows shimmer while moving, keep `r_shadowMapCSM 1` and `r_shadowMapCascadeStabilize 1`.
- If cutout materials cast solid-looking shadows, make sure `r_shadowMapHashedAlpha 1` is enabled and the material is actually alpha-tested rather than blended.
- If translucent shadows are too strong or too noisy, lower `r_shadowMapTranslucentDensity` or disable `r_shadowMapTranslucentMoments`.
- If blended materials still do not cast translucent shadows, that material may be outside the currently supported stage set. Common additive pickup orbs are supported, but many particle/effect materials still are not.
- If point-light shadows look too detached, reduce `r_shadowMapPointBias` or `r_shadowMapPointNormalBias`.
- If projected-light shadows look detached, reduce `r_shadowMapBias`, `r_shadowMapNormalBias`, `r_shadowMapPolygonFactor`, or `r_shadowMapPolygonOffset` in small steps.
- If performance drops sharply after enabling translucent shadowing, turn off `r_shadowMapTranslucentMoments` first; it adds an extra pass for eligible lights.

## Summary

Recommended default user setup:
- `r_shadows 1`
- `r_useShadowMap 1`
- `r_shadowMapCSM 1`
- `r_shadowMapHashedAlpha 1`
- `r_shadowMapCascadeStabilize 1`

Only enable `r_shadowMapTranslucentMoments 1` if you specifically want experimental blended transparency shadows and accept the extra cost and current material-support limits.
