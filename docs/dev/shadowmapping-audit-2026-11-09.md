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
| **Point lights with any moving or alpha-tested caster re-rendered six faces every view** and dropped their cached cube. Alpha-tested casters are always classed live, so a static light touching one grate never cached. | **Fixed, both backends.** The cached cube keeps static depth; each view copies it to a scratch cube and draws the live chains over the copy (`r_shadowMapPointCompose`). OpenGL blits face by face and shares one static entry between the LOCAL and GLOBAL ownerships when the light has no static local caster; 0.15 ms of GPU per composed pass in hub2. Vulkan copies the six faces in one transfer and keeps a static cube per ownership, because a resident entry is reserved by one pass per view. It does not compose under the opt-in shared interaction stream, which draws every pass's whole caster plan. See "Vulkan point composition" below. |
| **GL timer queries ran in every shadow phase of every light**, whether or not anything read them. A view with more phases than the 64 query slots polled all 64 unfinished queries for each further phase. | **Fixed.** Queries are issued only while `r_shadowMapReport` or `r_shadowMapDebugOverlay` is on. This alone was about half of the OpenGL CPU frame in hub2. |
| **The point receiver's per-texel path was the common path.** Each of the nine taps compared four texels against their own point on the receiver plane, with a cube-face selection each: 36 comparisons that read a four-by-four block of texels. | **Fixed, both backends.** The default kernel (nine fixed taps, one texel radius, hardware comparison) is one pass over the sixteen texels with the same weights, or nine plain bilinear lookups where the bias covers the block. Other tap counts, radii and the rotated mode keep the tap-by-tap path. 1.6 to 2.6 ms of GPU in hub2 against the old path, in two launches. |
| **Receivers filtered shadows for fragments the light does not reach** (outside the falloff, turned away). | **Fixed, both backends**, in the stock-material path. The PBR variants still sample first. |
| **Entities the game updates once a tic flipped between live and static** whenever the display outran the tic rate, because "live" meant "changed in the last frame". Each flip changed the static caster signature and forced a static re-render. | **Fixed.** A caster is live for the frames of a quarter second (`R_ShadowMapDynamicCasterFrames`). |
| **Mapped lights still did the stencil front-end work.** Three causes. Prelit lights, nine in ten, never shed their volumes. Dynamic volumes were never left out, because the test asked the instantiated snapshot, which is always a static model. And a caster beyond its shadow LOD distance (768 units for every game entity) left the map while its volume stayed, which read as a caster the map cannot hold and put the light on its stencil supplement for the rest of the level; the main light of the airdefense1 arena was one. On Vulkan a fourth: the player's own body and head, never seen in first person, kept every light near the camera "unknown" until their turn in the entity walk. | **Fixed, both backends.** See "Stencil front-end work" below. |

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
| **Quality and Ultra give the projected atlas four cells** (2048 cells in 4096, 4096 in 8192) while Enhanced has sixteen. A light with local casters takes two. OpenGL re-renders the overflow every view; Vulkan marks the light for stencil permanently and can draw it unshadowed for a frame. convoy2's start view does this: up to four projected lights, five lights sent to stencil for good and one drawn unshadowed. | **Fixed, both backends.** A tile size per light, assigned per view. See "Atlas tile sizes" below. No preset changed. |
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
lights with a live caster, and with point composition to none.

### Vulkan point composition

Added after the table above was measured. GPU time of the shadow-map phases
from `r_vkGpuPhaseProfile` over 240 frames a window, composition on against
`r_shadowMapPointCompose 0` in the same launch, two windows each:

| Scene, preset | Point cubes, ms | All shadow maps, ms | Fresh cubes a frame |
|---|---|---|---|
| airdefense1 arena, Quality | 0.13 from 0.28 | 0.40 from 0.55 | 0 from 2 to 5 |
| airdefense1 arena, Ultra | 0.11 from 0.20 | 1.22 from 1.31 | not counted |
| hub2, Quality | 0.15 from 0.24 | 0.69 from 0.75 | 0 to 2 from 0 to 7 |

The fresh cubes left in hub2 are lights the sweep brings into view for the
first time. The point phase costs half to two thirds of what it did where lights
have live casters, which is 0.06 to 0.15 ms of a 6 to 8 ms GPU frame in these
scenes. Whole-frame GPU
means moved by less than their window-to-window spread at Quality (about 0.2 ms)
and by 0.09 ms at Ultra, and CPU frame time did not change measurably: a
composed pass trades its static draws for a copy, five layout transitions and
six more render scopes. The validation layers reported nothing in hub2 or the
arena with composition on, or with the shared interaction stream on.

### Stencil front-end work

Added on 10 November. What changed:

- **Prelit lights shed their volumes like any other.** A mapped light casts
  from per-surface volumes whether or not dmap baked a prelight, so the
  exclusion protected nothing. Point lights always qualify. An atlas light
  (projected, parallel, distant point) does when every atlas light of the view
  is certain of a tile, counted at two blocks a light: Vulkan gives a light
  that finds the atlas full nothing but its volumes in that frame, and Quality
  and Ultra cut four tiles. This also covers lights without a prelight, which
  used to shed their volumes whether or not the atlas could hold the view.
  (With the tile sizes below the count is made at each light's smallest tile.)
- **Dynamic models get no volumes under a mapped light**, by the entity's own
  model. An entity that stops animating keeps those surfaces, so they are
  built again, with volumes, when the light returns to stencil (sticky
  fallback, update budget, a receiver that needs stencil). A subview policy
  alone does not rebuild: there the missing volumes already make the map
  required.
- **The map and the stencil volume agree on shadow LOD.** A surface that got a
  volume when its interaction was built stays a map caster, as it stays a
  stencil caster. A surface built without one still joins the map when a view
  admits it. Static casters therefore no longer drop out of cached maps as the
  camera moves away, which also spares the cache a re-render at each crossing.
- **Vulkan's receiver scan answers for entities it has not built yet.** An
  entity the view does not see links no receivers and is skipped; a static
  model answers for itself; an animating entity is answered by the verdict of
  the model it last drew, while it still names the same model, skin and custom
  shader. A surface an entity gains without changing any of those (an
  overlay, a suppression mask) is seen one view late, which for a material
  with authored GLSL lighting means one frame without the volumes of entities
  walked before it.

airdefense1 arena, Quality, about ten animated characters in view:

| | OpenGL | Vulkan |
|---|---:|---:|
| Shadow volumes built a frame, before | 31 to 36 | 31 to 36 |
| Shadow volumes built a frame, after | 0 | 0 |
| The same with `r_shadowMapSkipStencilShadows 0` | 34 | 33 |
| CPU frame, ms, switch off against on | 5.23 to 5.09 | 3.70 to 3.55 |

The CPU row is the median frame at a fixed view, alternating windows in one
launch, after the first pair. Against the build before the change the sampler
(`cpu_sampler.py`, OpenGL, view sweep, one launch each) puts the front end at
1.33 ms a frame from 2.06, of which linking and building interactions is 0.28
from 0.72 and shadow-volume generation nothing from 0.20; with the switch off
in the new build it reads 1.75 ms. The two methods do not agree on the size,
0.14 ms against 0.4, so take the smaller as measured and the larger as an
upper bound. Scenes with fewer animated casters gain less.

Checked for this change: all fourteen `renderer_shadow_mapping_maps.py`
scenarios on both backends (`door-projected` failed once on OpenGL, for a
missed cache reuse, while other tests loaded the machine, and passed twice run
alone). Under an update budget of one the volumes are built again (27 to 29 a
frame) and the frame is stencil-shadowed; back at zero they stop. The
validation layers reported nothing through that cycle. No stock view with a
mirror or camera screen was exercised, so the subview rule rests on the code:
missing volumes mark the stencil fallback incomplete, which makes the map
required there on both backends.

### Atlas tile sizes

Added on 10 November. Every projected, parallel and distant point light took a
tile of `r_shadowMapSize`, one per receiver ownership, and Quality and Ultra cut
four. A survey of fifteen stock views found most ask for four passes or fewer,
and one that does not: convoy2's start, with up to eight such lights in view.

- **The front end sizes each atlas light's tile once the view is finished**
  (`R_ShadowMapAssignAtlasTiles`, `viewLight_t::shadowMapTileSize`). A view
  that fits keeps every light at `r_shadowMapSize`, so nothing changes where
  nothing overflowed. Otherwise lights halve, one step each in turn, those
  covering the least world first (the width of a projector's far face;
  sunlight and other fitted sources last), down to a quarter and never under
  128. Both back ends, the sealed stream and the cache signature read the
  same size (`R_ShadowMapProjectedTileSize`).
- **Vulkan cuts the atlas by buddy allocation** instead of a row scan, so any
  set of power-of-two blocks whose areas fit is placed, in any order. Its
  projected cache now holds tiles of different sizes; it used to discard every
  resident tile when one light's size differed.
- **OpenGL's atlas grid cell is the smallest tile**, a full-size light spanning
  a block of sixteen cells, and its scratch target keeps the largest size
  asked for instead of being reallocated for each light.
- **Neither back end evicts a projected tile a light on screen is using**, the
  rule the point cube cache already had.
- **The stencil-shedding rule follows**: an atlas light sheds its volumes when
  the view's atlas lights would fit at their smallest tiles, two blocks each.
  That is thirty-two lights at Quality and Ultra where it was two.

convoy2 start view, Quality, view sweep:

| Vulkan | Before | After |
|---|---:|---:|
| Atlas passes mapped in one view, most | 4 | 11 |
| Lights sent to stencil for the level | 5 | 0 |
| Lights drawn unshadowed (the warning prints once) | at least 1 | 0 |

The small projectors there drop to 1024 while the view is crowded and the
distant source keeps 2048. On OpenGL the same view maps its seven projected
lights with the eight cache slots in use and no evictions. The validation
layers reported nothing.

What this costs: a light changes resolution when the view becomes crowded or
clears, which re-renders its map once at the new size and can be seen as a
change in shadow sharpness on the smallest lights. There is no hysteresis.

Checked for this change: the fourteen scenarios on both backends, the crowded
convoy2 view captured mapped, stencilled and unshadowed on both (six lights at
1024, two at 2048, soft shadows where the stencil frame has hard ones), hub2
unchanged at 2048 with four passes, and the in-engine self-test, which now
pins the fit. `door-projected` again failed once on OpenGL for a missed cache
reuse. That was the scenario, not the renderer: it waited six frames before
its cached capture, and since the live-caster window became a quarter second
of real time the capture could land on the frame the map is re-rendered. It
now waits that window out (`renderer_shadow_mapping_movers.py`) and passed
seven runs in a row.

### Projected cache memory

Added on 10 November, after the tile sizes. The projected cache counted
entries: eight slots (`r_shadowMapProjectedCacheSize`), whatever the size of
the tile in each. That bounded memory while every tile was `r_shadowMapSize`.
Once a crowded view's tiles are cut to a half or a quarter it is the wrong
unit: convoy2's start view has eleven atlas passes, ten of them at 1024, and
three were drawn again every view while the eight resident tiles filled a
fraction of the memory eight slots were meant to allow.

- **The bound is tile texels** (`R_ShadowMapProjectedCacheTexelBudget`):
  `r_shadowMapProjectedCacheSize` full-size maps' worth, eight by default as
  before. A map takes the texels of its tile, so one cut to half size takes a
  quarter of a full one. The entry records no longer limit anything (64 on
  both back ends).
- **A cascaded block counts as one map**, by its tile, as its one slot did.
  The first version counted it as the four tiles of memory it is, and
  `door-portal`, which turns cascade caching on, failed on OpenGL: two cached
  blocks filled the cache and the flashlight's map was drawn again every
  view. Vulkan passed the scenario but with its cache full at five entries
  and four cacheable passes drawn again each view. So the ceiling stays what
  eight slots allowed, eight
  blocks of the largest kind at full size, and no set of maps that was
  cached before is refused now.
- **Vulkan** counts every resident image, including those idle records keep
  for the next map of their size, and makes room in that order: idle images
  first, then the maps used longest ago that no light on screen is using.
  Nothing is given up for a map that would not fit even then. A lowered cvar
  or `r_shadowMapSize` gives the memory back at the next view; images used
  to stay until a `vid_restart`.
- **OpenGL's bound is the atlas.** Its cached tiles live there and the atlas
  is allocated whole, so holding cells back would save nothing: the cvar only
  turns the cache on or off. That is sixty-four cells at Quality and Ultra,
  four full tiles.
- **Memory is unchanged**: at most 128 MiB at Quality and 512 MiB at Ultra on
  Vulkan with 32-bit depth, four times that if every cached map is a
  cascaded block (`r_shadowMapCacheCSM 1`, off by default).

convoy2 start view, Quality, the view held still:

| | Before | After |
|---|---:|---:|
| Vulkan: atlas passes reused from the cache, of 11 | 8 | 11 |
| Vulkan: atlas passes drawn again each view | 3 | 0 |
| Vulkan: cache in use | 8 of 8 slots | 3.5 of 8 maps' worth, 11 entries (4.75 with idle images) |
| OpenGL: cacheable passes missed each view | 1 to 2 | 0 |
| OpenGL: cache in use | 8 of 8 slots | 52 of 64 cells, 10 entries |

The gain in frame time is small. The old count and the new were alternated in
one process, six pairs of five-second windows, the old one reproduced with a
lower cvar: `2` on Vulkan caches the same eight of eleven. The OpenGL rows
come from the first version of this change, where the cvar still limited the
atlas and `3` left one pass uncached:

| | Old count | New | Difference between pairs |
|---|---:|---:|---|
| Vulkan GPU | 5.97 ms | 5.74 ms | 0.24 ms less, spread 0.26 ms, five of six in favour |
| Vulkan CPU | 4.18 ms | 4.03 ms | not resolved (spread 0.72 ms) |
| OpenGL GPU | 9.20 ms | 9.14 ms | not resolved (spread 0.43 ms) |
| OpenGL CPU | 12.87 ms | 12.94 ms | not resolved (spread 0.56 ms) |

Another session was building and running a client on the machine during these
runs, which widens the spread. Caching is cheap rather than free here: a
reused pass on Vulkan copies its cached depth into the view's atlas before the
live casters are drawn over it, and with the cache off altogether the same
view costs 5.92 ms of GPU against 5.62 ms with all eleven cached. The change
matters more the more crowded a view is, since the old count redrew every
pass past the eighth.

The cvar is live in both directions on Vulkan: set to `0` the resident shadow
memory fell from 560 MiB to 484 MiB at the next view and the report showed
`projectedTiles=0.00/0`; `2` held exactly `2.00/2` with eight entries; and
`8` restored the full set each time. With the view turning, the cache fills
to `8.00/8` with fourteen entries, 128 MiB, which is the ceiling eight slots
had at Quality.

Checked for this change, on the final build: the fourteen scenarios on both
back ends, all passing; the convoy2 counts above; the Vulkan validation
layers, silent while the view turned and the cvar went 8, 1, 0, 2, 8 and
`r_shadowMapSize` to 1024 and back; the same on OpenGL without the layers
(off at `0`, unchanged at `3`, no failed pass); the in-engine self-tests on
both, which now pin the count; and the sixty-two static tests that read the
touched files. Two scenario runs cannot share `.install` at once: the second
one's clients exit at once without a log, which reads as every capture
missing.

Checks that passed on the final build: `renderer_shadow_mapping_maps.py`
`caster-cycle-point`, `caster-cycle-cutout` and `door-point` on both backends
(caster counts, cache reuse, cached against fresh images byte-identical; run
again after the Vulkan port, where the point lights now report
`reuse+compose`), the in-engine
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

1. Depth-only point cubes on OpenGL while nothing samples the colour cube.
2. Cube faces the view cannot see; fullscreen budgets for the three tiers.
3. One static cube for both ownerships of a composed Vulkan light, as on
   OpenGL. It needs a resident entry two passes of a view can share.
4. Static-world volumes are still built once for every mapped light and kept
   for the fallback. Leaving them out too would need the same rebuild on
   return, for every surface the light touches.
5. A reused projected pass on Vulkan copies its resident tile into the view's
   atlas even when it has no live casters to draw over it. Sampling the
   resident tile in place would save the copy; convoy2's eleven cached passes
   together save only 0.3 ms of GPU against drawing them fresh.
6. The OpenGL projected cache is the atlas, four full tiles at Quality and
   Ultra, so it holds one crowded view and nothing of the view just left.
   Vulkan holds eight maps' worth.
