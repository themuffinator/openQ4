# Shadow-mapping audit, 9 November 2026

A review of the shadow-map implementation as the replacement for stencil
shadows: what the world it lights looks like, what it cost, where it disagreed
with stencil, what this round changed and what is still open. It follows the
[September audit](shadowmapping-final-audit-2026-09-05.md) and feeds FPS-03 and
REN-03 in the [roadmap](roadmap.md).

All timings are from the reference machine (RTX 4060 Laptop) in a hidden
2560x1440 window, which the engine caps at 60 Hz: they are GPU timer and CPU
frame-work milliseconds from `rendererBenchmarkCapture` over a 360 degree view
sweep, read against interleaved base windows in the same launch. They compare
configurations; they are not the fullscreen frame rates of the
[performance report](performance-report.md), and no preset budget is redeclared
from them.

## What the stock world asks of it

Counted from the entity lists of the 79 stock maps:

| Class | Lights | Casting shadows |
|---|---:|---:|
| Point | 20,655 | 10,477 |
| Projected | 599 | 518 |
| Parallel | 20 | 12 |

- Nineteen in twenty shadow-casting lights are point lights. 73% of those have
  a radius of 256 units or less and 92% of 512 or less.
- 85% of projected lights reach 512 units or less.
- A dense view (Nexus Hub 2's processing hall) shows 40 lights at once, 33 of
  them shadow-mapped point lights; LOCAL and GLOBAL ownerships bring that to 41
  cube passes.

The implementation was sized the other way round: a twelve-slot point cube
cache (sixteen at most) against forty cubes in view.

## Findings and what was done

### Cost

| Finding | State |
|---|---|
| **Point cache far smaller than a view.** In the hub2 sweep 26 of 39 cacheable cube passes missed every frame: 29 cubes (174 faces) re-rendered per frame, 4.9 ms of CPU. OpenGL also evicted the first slot in a chain, one miss evicting an entry a later light of the same view would have hit. | **Fixed, both backends.** 64 slots (96 records), bounded by `r_shadowMapPointCacheMegs` (512 MiB, never fewer than the old twelve). A cube used this frame or the last is not evicted; the light renders to scratch until one leaves the view. OpenGL recycles a light's own stale cube. Archived profiles holding the old 12 move to 64 once. |
| **Point lights with any moving or alpha-tested caster re-rendered six faces every view** and dropped their cached cube. Alpha-tested casters are always classed live, so a static light touching one grate never cached. | **Fixed on OpenGL.** The cached cube keeps static depth; each view blits it to the scratch cube and draws the live chains over it (`r_shadowMapPointCompose`). LOCAL and GLOBAL ownerships share one static entry when the light has no static local caster. 0.15 ms of GPU per composed pass in hub2. **Open on Vulkan**, which still re-renders such cubes. |
| **GL timer queries ran in every shadow phase of every light**, whether or not anything read them. A view with more phases than the 64 query slots polled all 64 unfinished queries for each further phase. | **Fixed.** Queries are issued only while `r_shadowMapReport` or `r_shadowMapDebugOverlay` is on. This alone was about half of the OpenGL CPU frame in hub2. |
| **The point receiver's per-texel path was the common path.** Each of the nine taps compared four texels against their own point on the receiver plane, with a cube-face selection each: 36 comparisons that read a four-by-four block of texels. | **Fixed, both backends.** The default kernel (nine fixed taps, one texel radius, hardware comparison) is one pass over the sixteen texels with the same weights, or nine plain bilinear lookups where the bias covers the block. Other tap counts, radii and the rotated mode keep the tap-by-tap path. 1.6 to 2.6 ms of GPU in hub2 against the old path, in two launches. |
| **Receivers filtered shadows for fragments the light does not reach** (outside the falloff, turned away). | **Fixed, both backends**, in the stock-material path. The PBR variants still sample first. |
| **Entities the game updates once a tic flipped between live and static** whenever the display outran the tic rate, because "live" meant "changed in the last frame". Each flip changed the static caster signature and forced a static re-render. | **Fixed.** A caster is live for the frames of a quarter second (`R_ShadowMapDynamicCasterFrames`). |
| Mapped lights still do the stencil front-end work: dynamic volumes are generated for mapped lights (the suppression tests the instantiated model, which is always static), and prelit lights, nine in ten, never elide volume linking. | **Open.** See "Still open". |

### Agreement with stencil

| Finding | State |
|---|---|
| **A `noDynamicShadows` light lost every shadow** with shadow maps on: it has no per-surface volumes and no map casters, and the mapped-light rule also dropped its dmap prelight. 364 authored lights carry the flag, nearly all in multiplayer maps; the ones with a prelight (197 by a static count of the `.proc` files) shone through walls. | **Fixed** (`R_ShadowMapsNeedPerSurfaceStencilVolumes`). Verified by code path, not yet by image. |
| **Static world geometry with a `noSelfShadow` material stopped shadowing `noSelfShadow` receivers** under prelit lights. Retail links the combined prelight as a GLOBAL shadow; the mapped path routed those surfaces by material into the local chains, so a marine behind such a pillar or container was lit, and the light needed a second map. | **Fixed.** Static-world casters of a prelit light keep global ownership. |
| **Gibs, burning corpses and fading moveables cast whenever `r_useShadowMap` was on**, including under point lights left on stencil at Enhanced, which is exactly the volume retail suppressed. | **Fixed.** They cast only when point lights are mapped too. |
| **The five-tap kernel was one-sided**: the first four taps of the Poisson set all lie left of the centre (centroid 0.41 radius off). Enhanced uses five taps. | **Fixed, both backends.** A balanced four-tap ring. |
| **Packed point depth wrapped at the far envelope**: `fract(1.0)` stored the nearest depth for a caster at or beyond far. Manual (non-comparison) OpenGL path only. | **Fixed.** |
| **Shadow passes restored the viewport without the tiled-capture offset.** | **Fixed** (OpenGL). |
| Casters are not clipped to the light volume. For a point light whose origin lies outside its box (388 authored), geometry between origin and box can shadow the box in a map and never in stencil. | **Open, unconfirmed.** convoy2's `light_9020` captured mapped and stencilled shows no such difference. mcc_landing's `light_9081` view does differ, but those captures were not controlled for the player's own shadow or the scene's animation. A fragment clip in the cube caster is the fix if a case is confirmed; Vulkan's caster push block has no room for the box. |
| Light-facing one-sided geometry casts in maps and never in stencil. | **By design** (the September contact-detail decision). Recorded, not changed. |
| CSM lights are unshadowed past `r_shadowMapCascadeDistance`. Maximum only. | **Open.** |

### Resources

| Finding | State |
|---|---|
| **Quality and Ultra give the projected atlas four cells** (2048 cells in 4096, 4096 in 8192) while Enhanced has sixteen. A light with local casters takes two. OpenGL re-renders the overflow every view; Vulkan marks the light for stencil permanently and can draw it unshadowed for a frame. | **Open.** The prelight ownership fix removes many second cells. The structural fix is a tile size per light; a preset change needs the budgets redeclared. |
| Vulkan: cascades in an 8192 atlas always fail (one CSM block is the whole atlas). | **Open**; no preset uses it. |
| The stencil-fallback flag on a light is never cleared. | **Open.** Costs the light its volume elision only. |
| OpenGL point cubes carry a colour cube the default receiver never samples: half their memory. The opt-in modern executor still reads it. | **Open.** At 512 the cache can now reach 504 MiB on OpenGL (42 cubes) against 384 MiB on Vulkan (64). |
| Vulkan hub2: one light (`lights/squarelight1`) logs "required shadow resource unavailable" once and falls back; OpenGL maps it. | **Open**, present before this round. |

## Measured

Nexus Hub 2, Quality preset, view sweep. "Before" is the 767f39de runtime.

| OpenGL | GPU ms | CPU ms |
|---|---:|---:|
| Shadow maps, before | 11.97 | 11.8 |
| Shadow maps, after | 6.55 | 5.2 |
| Point lights on stencil, before | 6.29 | 7.7 |
| Point lights on stencil, after | 5.46 | 4.4 |
| Every light on stencil, after | 5.13 | 3.9 |

The premium of mapping point lights over stencilling them fell from 5.7 ms of
GPU and 4.1 ms of CPU to 1.1 and 0.8. Per frame at the densest pose the cube
work went from 29 updates (174 faces) to none, with five composed passes.

| Vulkan, after | GPU ms | CPU ms |
|---|---:|---:|
| Shadow maps, hub2 | 6.16 | 3.5 |
| Point lights on stencil, hub2 | 6.20 | 3.2 |
| Shadow maps, storage2 | 5.83 | 4.2 |
| Point lights on stencil, storage2 | 5.78 | 3.6 |

On Vulkan the dense pose went from 18 fresh cubes a frame (108 faces) to 3, the
lights with a live caster.

Checks that passed on the final build: `renderer_shadow_mapping_maps.py`
`caster-cycle-point` and `caster-cycle-cutout` on both backends (caster counts,
cache reuse, cached against fresh images byte-identical), the in-engine
`rendererShadowProjectedDiagnosticSelfTest` (now with the cache and live-window
policies) and `performancePresetSelfTest`, and
`renderer_vulkan_shadow_compatibility.py`, which pins every contract above.

## Presets

Unchanged. Enhanced keeps point lights on stencil: mapping them still costs
about 1 ms of GPU and of CPU in the densest scene on OpenGL, and that tier's
OpenGL combat margin is under 1 ms. Quality, Ultra and Maximum gain the cache
and filter work at their existing settings, and their budgets were not
re-measured in fullscreen; that is the next step for FPS-03, together with
whether Enhanced can now map point lights on Vulkan.

## Still open, in order of value

1. Vulkan point composition (the OpenGL change mirrored: a cube copy, a LOAD
   scope for the live chains, a pass that names both its static source and its
   composed target).
2. Front-end elision for prelit lights and suppression of dynamic volumes for
   mapped lights, with a rebuild when a light returns to stencil.
3. A tile size per projected light, so the atlas holds a view at every tier.
4. Depth-only point cubes on OpenGL while nothing samples the colour cube.
5. Cube faces the view cannot see; fullscreen budgets for the three tiers.
