# Shadow mapping

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

Shadow maps give Quake 4's lights soft, stable shadows: sharp where an object touches the floor, softer further from it, with holes where light passes through grates, fences and foliage. They replace Quake 4's original hard-edged shadows (called stencil shadows) for the lights they cover. The Quality, Ultra and Maximum presets turn them on for you. This guide explains what each preset does, how to tune shadows yourself, and what to try when something looks wrong.

## Turning shadow maps on

Choose a preset on **Settings → Display → Graphics → Performance Preset** (the System page in the classic menus). The preset decides which shadows you get:

| Preset | Shadows |
|---|---|
| Minimum, Low Power | None, to save work on weak or battery-powered hardware. |
| Low, Performance, Balanced | Quake 4's original stencil shadows. |
| Enhanced | Shadow maps for spotlights and sunlight. Point lights, which are most of Quake 4's lights, keep the original shadows, because their shadow maps cost the most. |
| Quality | Shadow maps for every light, with soft edges that harden where objects meet surfaces. |
| Ultra | As Quality, with sharper spotlight and sunlight shadows and smoother edges. |
| Maximum | Every shadow setting at its highest, including sharper point-light shadows and extra detail for distant light. |

The change applies at once; you don't need to restart the renderer. **Settings → Display → Image Quality → Enable Shadows** turns all shadows off or on, whichever kind the preset uses.

To use shadow maps with any other preset, open the console and enter:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
```

Choosing a preset again replaces these with the preset's own values. Shadow settings ("console variables", or cvars) take effect immediately, without `vid_restart`.

## What you see

- **Soft, grounded edges.** Quality and above use contact-hardening shadows (known as PCSS): crisp where an object meets the surface it stands on, softening with distance. Enhanced uses a simple fixed soft edge.
- **See-through shadows.** Grates, chain-link fences, railings and leaf cards cast shadows with their holes in them.
- **Your flashlight casts shadows** on every preset that draws shadows. Projectile lights, such as a rocket's glow, cast shadows only on Quality, Ultra and Maximum, as on the original game's top setting.
- **Stable while you move.** openQ4 draws the shadows of still scenery under still lights once and keeps them, so they cost little and don't shimmer as you walk. Only moving objects and moving lights are drawn again each frame.
- **Distance.** As in the original game, moving objects far away and small on screen stop casting shadows. Still scenery keeps its shadow at any distance.
- **No half-drawn shadows.** If a light's shadow map can't be finished in a frame, that light uses its original stencil shadow instead. Some lights on animated characters also keep the original shadows so that character lighting looks as Quake 4 intended.
- **Mirrors and security cameras** reuse stored shadow maps or fall back to the original shadows, to keep their cost down.

## Tuning quality and speed

These are the settings that matter most. Each is described fully in the [settings reference](#settings-reference).

- **Sharpness.** `r_shadowMapSize` sets the resolution of spotlight and sunlight shadows (presets use 1024 to 4096). `r_shadowMapPointSize` does the same for point lights (512, or 2048 on Maximum). Higher values are sharper but use more video memory; point-light size costs the most, because each point light stores six images.
- **Point lights.** `r_shadowMapPointLights 0` keeps the original shadows for point lights only. It is the biggest single saving, and it is what Enhanced does.
- **Soft edges.** `r_shadowMapFilterMode` picks the edge style: `2` contact-hardening (the default), `1` an even soft edge, `0` a simple fixed blur. `r_shadowMapFilterTaps` (5, 9 or 13) sets how many samples smooth each edge: more is smoother and costs more.
- **Large outdoor areas.** `r_shadowMapCSM 1` turns on cascades: a distant light, such as sunlight, gets several shadow maps, with the sharpest one closest to you. Only Maximum turns cascades on, because they cost more.
- **Crowded views.** All spotlight and sunlight shadows share one large texture, the shadow atlas (`r_shadowMapAtlasSize`). When a view has more lights than fit, openQ4 draws the lights that cover the least of the world at half or quarter size, rather than dropping any of them.

### Example settings

Lighter, for modest graphics cards:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 0
seta r_shadowMapSize 512
seta r_shadowMapPointSize 256
seta r_shadowMapFilterMode 0
seta r_shadowMapFilterRadius 0.75
seta r_shadowMapFilterTaps 5
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 5
seta r_shadowMapHashedAlpha 1
```

Balanced:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapSize 1024
seta r_shadowMapFilterMode 2
seta r_shadowMapFilterRadius 2.0
seta r_shadowMapFilterTaps 9
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 9
seta r_shadowMapCasterCulling 2
seta r_shadowMapHashedAlpha 1
seta r_shadowMapCascadeStabilize 1
```

Sharper:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapCSM 1
seta r_shadowMapSize 2048
seta r_shadowMapPointSize 1024
seta r_shadowMapCascadeCount 4
seta r_shadowMapFilterMode 2
seta r_shadowMapFilterRadius 2.0
seta r_shadowMapFilterTaps 13
seta r_shadowMapPointFilterRadius 1.0
seta r_shadowMapPointFilterTaps 13
seta r_shadowMapCasterCulling 2
seta r_shadowMapHashedAlpha 1
seta r_shadowMapCascadeStabilize 1
```

For an even soft edge instead of contact-hardening, add:

```cfg
seta r_shadowMapFilterMode 1
seta r_shadowMapPointFilterMode 1
```

## Shadows through see-through surfaces

### Grates, fences and foliage

Surfaces with holes cut in them cast shadows with the same holes, on every light that uses shadow maps. `r_shadowMapHashedAlpha 1` (the default) gives steadier shadows from distant fences and foliage but can look slightly grainy; set it to `0` for harder, cleaner edges. A few materials whose holes come from a video or a generated texture can't be traced this way: they cast a solid shadow on OpenGL, or use the original shadow on Vulkan.

### Glass and other blended surfaces (experimental)

Glass, pickup orbs and similar blended surfaces cast no shadow by default. `r_shadowMapTranslucentMoments 1` lets them cast partial, tinted shadows, coloured by the surface's texture and tint. It is experimental:

- It adds a shadow pass for every light that reaches such a surface, so it costs noticeably more.
- It covers common blended and additive materials. GUI screens, particles and many effect materials are left out.
- Colours are approximate.
- OpenGL and Vulkan both draw it. With many such lights in one view, Vulkan lowers their resolution to fit, and it skips surfaces whose texture is a video or a generated image.
- It needs a graphics card with three extra render targets and texture samplers; without them openQ4 turns it off.

Start with these values, then adjust:

```cfg
seta r_shadowMapTranslucentMoments 1
seta r_shadowMapTranslucentDensity 1.0
seta r_shadowMapTranslucentMinAlpha 0.02
```

- Raise `r_shadowMapTranslucentDensity` if the effect is too weak, or lower it if it's too strong.
- Raise `r_shadowMapTranslucentMinAlpha` if very faint surfaces cast more shadow than you want.
- Use `r_shadowMapTranslucentFilterRadius` to give these shadows a different softness from solid ones.
- Raise `r_shadowMapTranslucentBleedReduction` only if light visibly bleeds through; higher values make the shadows harsher.
- Turn the feature off for the most predictable performance.

## For mappers and modders

- Materials marked `noShadows`, and decals (`DECAL_MACRO`), never cast shadows, even with see-through shadows on. `forceShadows` makes a material cast one.
- Off-centre point lights whose whole light volume lies in front of them, such as distant outdoor lights, are shadowed like spotlights: they use `r_shadowMapSize` and the cascade settings instead of `r_shadowMapPointSize`.
- A door at rest can join a light's stored shadow; once it moves, the stored shadow is redrawn, so it never leaves a ghost of the closed door.
- Freezing single-player time with `g_stopTime 1` also holds doors and animated materials, which helps when comparing shadows at one moment.

## Platform support

| Platform | Shadow maps | Notes |
|---|---|---|
| Windows | Supported | OpenGL 3.3 or newer, and Vulkan. |
| Linux | Supported | OpenGL 3.3 or newer. The Vulkan renderer is experimental on Linux. |
| Steam Deck | Supported, off by default | Auto-Detect picks Performance on Deck, which uses the original shadows. To try shadow maps, use `r_shadowMapSize 1024`, `r_shadowMapPointSize 512`, `r_shadowMapMaxUpdatesPerView 2`, and `r_shadowMapCSM 1` with `r_shadowMapCascadeCount 3`. The update limit keeps shadow cost bounded in busy scenes. See the [Steam Deck guide](steam-deck.md). |
| macOS, OpenGL (Apple legacy GL2.1 tier) | Stencil only | Apple's OpenGL lacks the shader features shadow maps need. Lights fall back to the original stencil shadows automatically; this is expected, not an error. |
| macOS, Vulkan (through MoltenVK) | Experimental, untested on Apple hardware | Used when Automatic or `r_renderApi vulkan` starts Vulkan. MoltenVK, included in the macOS packages, runs Vulkan on Apple's Metal. It uses the same shadow code as Windows and Linux, but nobody has checked its shadows on a real Mac yet, so expect problems. If macOS uses OpenGL instead, the row above applies. |
| Android (OpenGL ES) | Stencil only | The OpenGL ES renderer has no shadow maps; lights use the original stencil shadows. |

On every platform, openQ4 never draws a half-finished shadow: it rejects incomplete maps and uses complete stencil coverage when available, so the light shows its original shadow instead. If Vulkan briefly has neither, it keeps the light on without a shadow for that frame and notes it in the log, rather than letting the light vanish.

## Troubleshooting

- **No shadows at all.** Check **Settings → Display → Image Quality → Enable Shadows** (`r_shadows 1`), then `r_useShadowMap 1`, or pick Quality or higher.
- **Shadows shimmer as you move.** Keep `r_shadowMapCascadeStabilize 1` and `r_shadowMapStaticCache 1`.
- **Shadows float away from the objects casting them.** For point lights, first confirm `r_shadowMapCasterCulling 2` and `r_shadowMapPointMaxWorldBias 4`, then lower `r_shadowMapPointFilterRadius`, `r_shadowMapPointBias` or `r_shadowMapPointNormalBias` in small steps. For spotlights, lower `r_shadowMapBias`, `r_shadowMapNormalBias`, `r_shadowMapTexelBiasScale`, `r_shadowMapPolygonFactor` or `r_shadowMapPolygonOffset` in small steps.
- **Speckles or stripes on surfaces (shadow acne).** Raise the matching bias a very little. If it only happens with a wide soft edge, raise the shadow resolution or narrow `r_shadowMapFilterRadius` first.
- **Outdoor sunlight shadows look too soft or too sharp.** Distant lights use a tighter edge (`r_shadowMapDistantFilterScale 0.35`). Lower it toward `0` for sharper shadows, or set `1` for the same softness as other lights.
- **Soft edges don't change with distance.** Check `r_shadowMapFilterMode 2`.
- **A fence or grate casts a solid shadow.** Its material's holes come from something shadow maps can't trace (a video, a generated texture or unusual texture coordinates). `r_shadowMapHashedAlpha` can't change that.
- **Glass casts no shadow, or too much.** Glass shadows need `r_shadowMapTranslucentMoments 1`, and only common blended materials are supported. Lower `r_shadowMapTranslucentDensity` if they're too strong.
- **Frame rate drops with glass shadows on.** Turn `r_shadowMapTranslucentMoments` off first.
- **Shader errors with point-light shadows on one driver.** Set `r_shadowMapPointDepthCompare 0`.
- **A shadow doesn't update on unusual content.** Set `r_shadowMapStaticCache 0` or lower `r_shadowMapResidentFrames`.
- **After upgrading from an older openQ4.** openQ4 moves a few outdated shadow values to their new defaults once (the soft-edge radius and mode, and the point-light cache size); anything else you saved is kept. If shadows look detached after upgrading, compare your bias settings with the defaults below.

## Settings reference

### Main switches

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadows` | `1` | `0..1` | Draws shadows at all. Must be on for shadow maps. |
| `r_useShadowMap` | `0` | `0..1` | Uses shadow maps for the lights that support them. Enhanced, Quality, Ultra and Maximum set it to `1`. |
| `r_shadowMapPointLights` | `1` | `0..1` | Uses shadow maps for point lights too. Most of Quake 4's lights are point lights (about 19 in 20 shadow-casting ones), so `0` keeps nearly every light on the original shadows. Enhanced sets `0`. |
| `r_shadowMapCSM` | `0` | `0..1` | Cascades for spotlights and sunlight: several shadow maps per light, sharpest nearest you. Maximum sets `1`. |
| `r_shadowMapProjectedCSM` | `1` | `0..1` | Lets ordinary spotlights use cascades when `r_shadowMapCSM` is on. Sunlight and other parallel lights keep their own wide coverage either way. |
| `r_shadowMapHashedAlpha` | `1` | `0..1` | Steadier shadows from grates, fences and foliage. `0` gives harder, cleaner cutout edges. |
| `r_shadowMapStableAlphaHash` | `1` | `0..1` | Keeps that cutout pattern fixed to the world, so it doesn't crawl as the camera moves. |
| `r_shadowMapTranslucentReceivers` | `1` | `0..1` | Lets see-through surfaces receive shadows like solid ones, as `r_stencilTranslucentShadows` does for the original shadows. |
| `r_shadowMapCasterCulling` | `2` | `0..4` | Which side of an object's surfaces the shadow records. `2` (automatic) matches the original shadows: a lamp behind a one-sided panel shines through it, and solid props keep a grounded contact shadow. `0` records both sides, `1` the side facing the light, `3` the side facing away for every object, and `4` is the earlier automatic mode, kept for comparison. |

### Sharpness and memory

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapSize` | `1024` | `128..4096` | Resolution of each spotlight, sunlight or distant point light shadow. Higher is sharper and uses more video memory. In a crowded view, the lights covering the least of the world drop to half, then a quarter, of this size (never below 128); sunlight is reduced last. |
| `r_shadowMapPointSize` | `512` | `128..2048` | Resolution of each face of a point light's shadow. Each point light stores six faces, so this setting dominates shadow memory. |
| `r_shadowMapAtlasSize` | `4096` | `2048..16384` | Size of the shared texture that holds every spotlight and sunlight shadow in view. Quality fits four full-size shadows in 4096, Ultra four in 8192, Enhanced sixteen. |
| `r_shadowMapProjectionPad` | `0.15` | `0..1` | Extra margin around a spotlight's shadow coverage. |
| `r_shadowMapPointFarScale` | `1.25` | `1..4` | Extra range for point-light shadows. |
| `r_shadowMapPointDepthCompare` | `1` | `0..1` | Uses the graphics card's built-in comparison for point-light shadows where available. Set `0` if a driver has trouble. |
| `r_shadowMapPointDepthOnly` | `1` | `0..1` | OpenGL: stores point-light shadows as depth only, which halves their memory. openQ4 adds the extra colour copy by itself when something needs it (no hardware comparison, the experimental renderer paths, or the debug overlay). `0` always keeps it. Vulkan always stores depth only. |
| `r_shadowMapPointHighPrecision` | `0` | `0..1` | Stores that colour copy at higher precision. It only matters when the colour copy is in use. |
| `r_shadowMapDepthCompare` | `1` | `0..1` | Uses the graphics card's built-in comparison for spotlight shadows. Contact-hardening (`r_shadowMapFilterMode 2`) reads the shadow map directly instead, whatever this is set to. Set `0` if a driver has trouble. |

### Soft edges

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapFilterMode` | `2` | `0..2` | Spotlight edge style: `0` fixed blur, `1` even soft edge, `2` contact-hardening (sharp at contact, softer with distance). |
| `r_shadowMapFilterRadius` | `2.0` | `0..8` | Spotlight edge width in shadow-map pixels: the blur width in modes `0` and `1`, and the narrowest edge in mode `2`. Raising it softens and widens edges; for more detail, raise the resolution instead. |
| `r_shadowMapFilterTaps` | `9` | `1..13` | Samples per spotlight edge (1, 5, 9 or 13). More is smoother and costs more. |
| `r_shadowMapPCSSLightRadius` | `4.0` | `0..16` | How far contact-hardening searches for the object casting a shadow, in shadow-map pixels. |
| `r_shadowMapPCSSMaxRadius` | `8.0` | `0..16` | The widest edge contact-hardening can draw, in shadow-map pixels. |
| `r_shadowMapDistantFilterScale` | `0.35` | `0..1` | Narrows soft edges for sunlight and other distant lights, whose shadow pixels cover far more ground, so their outlines stay crisp. `1` uses the ordinary spotlight width. |
| `r_shadowMapPointFilterMode` | `0` | `0..1` | Point-light edge style: `0` fixed blur, `1` even soft edge. Point lights don't use contact-hardening. |
| `r_shadowMapPointFilterRadius` | `1.0` | `0..8` | Point-light edge width. Large lights cover a lot of ground per pixel, so wide values can wash out contact detail. |
| `r_shadowMapPointFilterTaps` | `9` | `1..13` | Samples per point-light edge (1, 5, 9 or 13). |

### Cascades

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapCascadeCount` | `4` | `1..4` | Number of cascades when `r_shadowMapCSM` is on. |
| `r_shadowMapCascadeDistance` | `1536` | `64..8192` | How far from you the cascades reach, in game units. |
| `r_shadowMapCascadeLambda` | `0.75` | `0..1` | How the cascades divide that distance: lower spaces them evenly, higher packs more detail close to you. |
| `r_shadowMapCascadeBlend` | `0.15` | `0..0.5` | Width of the crossfade between neighbouring cascades. |
| `r_shadowMapCascadeStabilize` | `1` | `0..1` | Holds cascades steady to stop shimmering as you move. |

### Stored shadows and update limits

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapStaticCache` | `1` | `0..1` | Stores the shadows of still scenery and reuses them every frame. Moving objects are drawn on top. |
| `r_shadowMapStaticCutouts` | `1` | `0..1` | Stores grates, railings and fences with the still scenery when their holes can't change, so a light reaching one isn't redrawn every frame. Cutouts that animate are still drawn each frame. `0` redraws every cutout each frame. |
| `r_shadowMapResidentFrames` | `120` | `1..3600` | Frames an unused stored shadow is kept before its space can be reused. |
| `r_shadowMapStaticHysteresisFrames` | `2` | `0..120` | Frames to wait after moving objects leave before a point light that can't layer moving objects over its stored shadow is stored again. |
| `r_shadowMapProjectedCacheSize` | `8` | `0..16` | How many full-size spotlight shadows to store (half-size ones count as a quarter each). `0` turns storing off. On Vulkan this caps the memory used, at most 128 MiB on Quality and 512 MiB on Ultra; on OpenGL the shadow atlas is the limit. Shadows in use on screen are never dropped. |
| `r_shadowMapPointCacheSize` | `64` | `0..96` | How many point-light shadows to store. |
| `r_shadowMapPointCacheMegs` | `512` | `64..8192` | Video memory the stored point-light shadows may use, in MiB. The default holds all 64 at the default size. A light without a slot is still shadowed; it is just drawn afresh each frame. |
| `r_shadowMapCacheCSM` | `0` | `0..1` | Also stores cascade shadows while their fit is unchanged. Most useful for fixed views. |
| `r_shadowMapMaxUpdatesPerView` | `0` | `0..1024` | Limits how many shadows are drawn afresh per frame; `0` means no limit, which every preset uses. A low limit keeps cost bounded on weak hardware but can leave moving shadows slightly behind. A light whose shadow would otherwise go missing is drawn anyway. |
| `r_shadowMapSubviewPolicy` | `1` | `0..2` | Shadows in mirrors and security cameras: `0` draws them like the main view, `1` reuses stored shadows or falls back to the original shadows, `2` prefers the original shadows. A shadow that would otherwise go missing is still drawn. |
| `r_shadowMapStencilFallbackViews` | `240` | `0..100000` | After a light's shadow map fails once, it keeps its original shadow ready as a backup. This is how many views must pass without another failure before it stops. `0` keeps the backup for the rest of the level. |

### Shadow acne and detached shadows

Shadow artifacts usually come in two kinds: speckles or stripes on a lit surface (bias too low), or shadows that float away from the object casting them (bias too high). Change bias in very small steps. Soft spotlight edges already allow for surfaces lit at a shallow angle, so they need no extra bias for that.

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapBias` | `0.00016` | `0..0.05` | Basic spotlight bias. |
| `r_shadowMapNormalBias` | `0.00075` | `0..0.05` | Extra spotlight bias on sloped surfaces. |
| `r_shadowMapTexelBiasScale` | `0.45` | `0..8` | Bias that scales with how much ground each shadow pixel covers. `r_shadowMapBias` acts as its minimum. |
| `r_shadowMapNormalOffsetScale` | `1.0` | `0..8` | Nudges the shadow lookup off sloped surfaces. Helps with stripes, but too high moves contact edges, especially on large lights. |
| `r_shadowMapReceiverPlaneBias` | `0` | `0..1` | An experimental alternative bias. Leave it off for normal play. |
| `r_shadowMapPolygonFactor` | `0.25` | `0..16` | Pushes shadow-casting surfaces back in proportion to their slope while the shadow is drawn. |
| `r_shadowMapPolygonOffset` | `0.5` | `0..64` | Pushes shadow-casting surfaces back by a fixed amount while the shadow is drawn. |
| `r_shadowMapPointBias` | `0.00010` | `0..0.05` | Basic point-light bias. |
| `r_shadowMapPointNormalBias` | `0.0010` | `0..0.05` | Extra point-light bias on sloped surfaces. |
| `r_shadowMapPointMaxWorldBias` | `4.0` | `0..64` | Caps the total point-light bias in game units, so very large lights don't open gaps at contact points. `0` removes the cap. |

### Glass and blended surfaces

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `r_shadowMapTranslucentMoments` | `0` | `0..1` | Turns on the experimental shadows from glass and other blended surfaces. |
| `r_shadowMapTranslucentDensity` | `1.0` | `0..8` | Strength of those shadows. |
| `r_shadowMapTranslucentMinAlpha` | `0.02` | `0..1` | Ignores surfaces fainter than this. |
| `r_shadowMapTranslucentFilterRadius` | `-1.0` | `-1..8` | Edge softness of those shadows. `-1` uses the solid-shadow setting. |
| `r_shadowMapTranslucentMinVariance` | `0.00001` | `0.000001..0.01` | A precision floor for those shadows. Rarely needs changing. |
| `r_shadowMapTranslucentBleedReduction` | `0.0` | `0..0.95` | Reduces light bleeding through them. Higher values make them harsher. |

### Light shadows from gameplay

| Setting | Default | Range | What it does |
|---|---:|---:|---|
| `g_dynamicLightShadows` | `-1` | `-1..1` | Shadows from your weapon's flashlight and from projectile lights. `-1` follows the preset (flashlight wherever shadows are drawn, projectile lights on Quality and up), `0` never, `1` always. A weapon picks up the change when it is next drawn or a save is loaded. |

### Troubleshooting and diagnostics

These help when reporting a shadow problem. Leave the last five settings at their defaults for play; the comparison switches exist to compare against the older way of drawing.

| Setting or command | Default | What it does |
|---|---:|---|
| `r_shadowMapReport` | `0` | Prints shadow statistics to the console: `1` a summary, `2` each light's decisions, `3` more detail. |
| `r_shadowMapReportInterval` | `30` | Frames between report prints. |
| `r_shadowMapDebugOverlay` | `0` | Shows a small view of one light's shadow map in the top-left corner, with counters. Use `r_singleLight` to pick the light. |
| `r_shadowMapDebugMode` | `0` | Colours the scene to show one part of the shadow calculation: `1` the shadow atlas, `2` which cascade each pixel uses, `7` a bias heatmap; the other values up to `14` isolate individual stages. |
| `reportShaderPrograms` | | Lists shader programs and whether they compiled. Try it if the scene looks unlit or plainly wrong. |
| `testFlashlight 1` | | Cheat: switches on the current single-player weapon's flashlight without input, for testing. `testFlashlight 0` turns it off. |
| `testDoor <name> [open\|close]` | | Cheat: reports or moves a single-player door, for checking its shadow. |
| `r_shadowMapSkipStencilShadows` | `1` | Skips building original shadows for lights drawn with shadow maps, which saves processor time. A light whose map fails gets them back the next frame. |
| `r_shadowMapConservativeCasters` | `1` | Lets objects off screen cast shadows onto what you can see. |
| `r_shadowMapPointCompose` | `1` | Comparison switch: draws a point light's moving objects over its stored shadow instead of redrawing all six faces. |
| `r_shadowMapCacheMovingLights` | `0` | Comparison switch: a moving light gets a new shadow every frame, so it is drawn directly rather than stored. `1` stores it anyway. A light that only changes colour is not moving. |
| `r_vkShadowSampleResident` | `1` | Comparison switch, Vulkan: reads a stored spotlight shadow in place instead of copying it each frame. The picture is identical. |
