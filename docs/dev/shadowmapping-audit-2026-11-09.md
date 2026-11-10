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
| **Every cutout caster was live.** A caster with holes cut by an alpha test was treated as if the holes could move, so every light that reaches a grate, a railing or a wire cap copied its cached map and redrew the cutout every view, for the same result. In convoy2's start view 25 passes were composed each frame, and 20 of them had no other live caster: every lamp there sits in a wire cap. | **Fixed, both backends.** A cutout is live only while its coverage can change. See "Static cutout casters" below. |
| **Vulkan copied every reused projected map into the view's atlas**, each view, only to sample it there. The copy serves a view that has moving casters to draw over the map and nothing else. With static cutouts it was the largest shadow phase left in convoy2's start view: 0.54 ms for eleven maps, ten of which had nothing live. | **Fixed (Vulkan).** A reused map with no moving caster is read where it lives. See "Resident projected tiles on Vulkan" below. |
| **A light's cached maps were rendered again whenever a static caster's stencil volume entered or left the view.** The signature that names the cached depth hashed, for each static caster, whether the view also saw that caster's volume. It flips as the player walks past, and the depth does not change. On a walk through hub2 one light with 77 static casters re-rendered both of its cubes nine times in five seconds for it. | **Fixed, both backends.** The signature no longer carries that bit; about half of the maps rendered fresh on a walk were for it. See "Walking" below. |
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
| **A soft shadow's grain followed its tile around.** The projected filter turns its kernel by a hash of the texel, and the texel was counted within the sampled image. The grain therefore changed whenever the light's tile took another place in the atlas, and a cached map differed from the same map drawn fresh: in hub2's start view one light alone changed 19,000 pixels between the two, by up to 147 levels, on both renderers. Reading cached maps where they live on Vulkan added a third place. | **Fixed, both backends.** The hash takes the texel within the tile. See "Walking" below. |
| Light-facing one-sided geometry casts in maps and never in stencil. | **By design** (the September contact-detail decision). Recorded, not changed. |
| CSM lights are unshadowed past `r_shadowMapCascadeDistance`. Maximum only. | **Open.** |

### Resources

| Finding | State |
|---|---|
| **Quality and Ultra give the projected atlas four cells** (2048 cells in 4096, 4096 in 8192) while Enhanced has sixteen. A light with local casters takes two. OpenGL re-renders the overflow every view; Vulkan marks the light for stencil permanently and can draw it unshadowed for a frame. convoy2's start view does this: up to four projected lights, five lights sent to stencil for good and one drawn unshadowed. | **Fixed, both backends.** A tile size per light, assigned per view. See "Atlas tile sizes" below. No preset changed. |
| Vulkan: cascades in an 8192 atlas always fail (one CSM block is the whole atlas). | **Open**; no preset uses it. |
| The stencil-fallback flag on a light is never cleared. | **Open.** Costs the light its volume elision only. |
| OpenGL point cubes carry a colour cube the default receiver never samples: half their memory. The opt-in modern executor still reads it. | **Fixed.** Cubes are depth only while nothing samples the colour. See "Depth-only point cubes on OpenGL" below. At 512 the cache holds 64 cubes in 384 MiB, as on Vulkan; it held 42 in 504 MiB. |
| Vulkan hub2: one light (`lights/squarelight1`) logs "required shadow resource unavailable" once and falls back; OpenGL maps it. | **Open**, present before this round. |
| Vulkan's scratch cube pool only grows. A view takes a scratch cube for each uncached or composed point pass, up to 512 of them, and none is released until the cube size changes. A noclip walk out of the airdefense1 arena into the void, from where every area is visible, mapped 289 point lights in one view and left 2.0 GB resident at Quality's 512 faces. The cache has a memory bound; this pool has none. | **Open.** Ordinary views use a handful. One crowded view pays for the rest of the level. |

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

### Depth-only point cubes on OpenGL

Added on 10 November. An OpenGL point cube was two cubemaps: depth, and a
colour cube holding the same radial depth packed into RGBA for receivers that
compare by hand. With hardware compare, the default, the receiver reads the
depth cube through a shadow sampler. The colour cube was still written by
every caster pass, kept in the cache, and copied face by face by every
composed pass, for nothing.

- **A cube has a colour image only while something samples it**
  (`RB_PointShadowMapColorCubeNeeded`): the classic receiver without hardware
  compare (GL 2.1, a driver whose compare shader fails,
  `r_shadowMapPointDepthCompare 0`, the compare-delta debug mode), the opt-in
  modern renderers, the shared interaction stream, and the debug overlay.
  Otherwise the cube's target has no colour attachment.
- **A change of that answer gives the old cubes back at once**
  (`RB_PointShadowMapSyncColorCubes`) and they are made and rendered again.
  It changes with a cvar or a debug view, never in ordinary play.
- **A cube changes shape whole**: both images and the target
  (`RB_PointShadowMapReleaseCube`). The first version kept the depth image
  and wrapped a new target around it, at whatever size it had been left.
  `door-point` lowers the cube size to 256 and later toggles the debug
  overlay; its light's cached cube came back at 512 beside a 256 scratch
  cube, could not be composed, failed its pass and went to stencil. A new
  target is now checked for size like an old one.
- **The memory bound counts four bytes a texel** for such a cube, so
  `r_shadowMapPointCacheMegs 512` holds all 64 cubes at 512 where it held 42.
- **It fails closed.** If a target without a colour attachment is incomplete,
  the colour cube is kept for that video generation.
  `r_shadowMapPointDepthOnly 0` keeps it always.

Quality, OpenGL. The two states were alternated in one process with
`r_shadowMapPointDepthOnly`, six pairs of five-second windows, the view held
still unless noted:

| | With colour cube | Depth only | |
|---|---:|---:|---|
| Combat view: resident shadow memory | 212 MiB | 152 MiB | ten cached cubes |
| Combat view: GPU frame | 6.88 ms | 6.43 ms | 0.45 ms less, spread 0.11 ms, six of six |
| Combat view: CPU frame | 11.02 ms | 10.07 ms | 0.95 ms less, spread 0.53 ms, six of six |
| hub2: resident shadow memory | 212 MiB | 158 MiB | |
| hub2: GPU frame | 5.90 ms | 5.70 ms | 0.21 ms less, spread 0.10 ms, six of six |
| hub2: CPU frame | 7.59 ms | 7.66 ms | not resolved (spread 0.49 ms) |
| hub2, view turning: cubes cached | 42 of 42 | 55 to 58 of 64 | one run, not paired |
| hub2, view turning: resident shadow memory | 686 MiB | 428 to 440 MiB | one run, not paired |

The GPU time saved is the colour plane of the face copies that composed
passes make and the colour writes of the caster pass. The images agree: the
combat view captured with and without the colour cube differs by 0.039 on
average in 8-bit colour units, which is what two captures of the same state
differ by (0.038 and 0.039, from animated surfaces), against 2.81 with shadows
off.

The frame times were taken on a quiet machine, before the correction to the
release path described above, which a steady frame does not run. The memory
figures are from the final build. A repeat of the timing on the final build
ran beside another session's client and tests, with frames half as long
again, and added nothing: 0.54 ms less GPU in the combat view with a spread
of 0.69 ms, five of six pairs in favour, and nothing resolved in hub2.

Checked for this change, on the final build: the fourteen scenarios on both
back ends, twenty-seven of twenty-eight cases passing. The other is
`door-portal` on OpenGL, whose cached capture of one phase differed from the
fresh one by 0.262 against a tolerance of 0.25; three reruns passed, and that
scenario's captures vary by up to 0.26 between any two on both back ends,
Vulkan included, which this change does not touch. Every way the answer
changes was switched in one process without a failed pass: the cvar off and
on at cube sizes 512 and 256, the debug overlay, no hardware compare, the
shared interaction stream, the modern forward+ path, and a `vid_restart`. The
in-engine self-tests and the sixty-two static tests that read the touched
files pass. Not run: macOS and Linux drivers, where a depth-only cube target
is expected to be complete and falls back to the colour cube if it is not.

### Static cutout casters

Added on 10 November, from the measurement meant to price skipping hidden cube
faces. That measurement turned up a view whose point-cube phase cost six times
what it did anywhere else: convoy2's start, 0.81 ms of Vulkan GPU against 0.13
in hub2 and 0.16 in the airdefense1 arena. The cause was not faces. Of the 25
passes composed there each frame (14 point, 11 projected), 20 had no moving
caster at all, only a cutout: the wire cap around each lamp
(`textures/common_misc/c_wirecap01`).

- **A cutout is live only while its coverage can change**
  (`R_ShadowMapCutoutCoverageAnimates`). It can when a register the coverage
  is read from follows time, an entity parm, a global or a table (the stage
  condition, the alpha scale, the alpha reference, the texture matrix), when
  an alpha stage samples a video or a render target or an image that is not
  loaded yet, or when the material has a deform. Otherwise the caster is as
  static as its entity and is drawn into the cached depth with the rest.
- **The registers are asked one by one**
  (`idMaterial::GetConstantRegisterValue`: a parse-time literal that no
  expression writes). So an animated glow stage beside a fixed alpha stage
  does not keep a cutout live.
- **The static signature carries what the baked coverage depends on**: the
  storage of the alpha images and the two hashed-alpha settings.
- **Only the front end changed.** Both back ends already drew a cutout through
  its alpha stages in whichever chain it was linked.
  `r_shadowMapStaticCutouts 0` restores the old rule.

convoy2 start view, Quality. The rule was switched in one process, six pairs,
each timed window after a settling one:

| | All cutouts live | Static cutouts | |
|---|---:|---:|---|
| Passes composed each view | 25 | 3 | both renderers |
| Vulkan: shadow GPU phases | 1.36 ms | 0.71 ms | the point phase 0.83 to 0.13 ms |
| Vulkan: GPU frame | 6.02 ms | 5.54 ms | 0.48 ms less, spread 0.56 ms, five of six |
| OpenGL: shadow-map render time (timer queries) | 3.86 ms | 0.51 ms | 26 passes to 5 |
| OpenGL: GPU frame | 9.33 ms | 7.58 ms | 1.75 ms less, spread 0.38 ms, six of six |
| OpenGL: CPU frame | 17.18 ms | 16.04 ms | 1.14 ms less, spread 1.08 ms, five of six |

Other sessions were building and running clients throughout, which inflates
the frame times and widens the spreads; the phase times and the pass counts do
not depend on that. The Vulkan cache phase (0.5 ms) is unchanged: it is the
copy of each reused projected tile into the view's atlas, item 2 below.

The rule itself was tested with four crates under a stock light, on both
renderers: a plain shipped cutout and a cutout with an animated glow stage
became static casters, and cutouts whose alpha reference or texture matrix
follows time stayed live.

The picture does not change. With the rule on against off, and cached against
uncached with either, the convoy2 captures differ by what two captures of one
state differ by, in two views and on both renderers (0.010 mean in 8-bit
units, 0.05% of pixels over 8 levels; 19.1 and 70% with shadows off).

Two things found on the way:

- **Sound-reactive lights make paused captures differ.** The `*_snd` light
  shaders follow the audio, which keeps playing under `g_stopTime`. Captures
  taken at a fixed cadence sampled a periodic sound in step with the variants,
  and one such light looked like a shadow difference between the rule on and
  off until it was isolated. That is also what made the scenarios'
  cached-against-fresh errors wander by up to 0.26. The scenario harness now
  holds those shaders at a constant level (`s_constantAmplitude 1`), and
  `door-portal` compares at exactly 0 in every phase on both renderers.
- **`idMaterial::ConstantRegisters()` is NULL for every material.** Stock
  idTech 4 starts each material with its registers marked constant "until
  shown otherwise"; that line is missing here and in the upstream base, so no
  material is ever constant and every surface evaluates its registers every
  view, on both renderers. Not a shadow matter and not changed here; restored
  afterwards, see [Constant material registers](material-constant-registers.md).

Hidden cube faces, the item this started from, were measured after the change
and set aside. All per-view point-cube work on Vulkan is then 0.13 ms of GPU
in convoy2 and hub2, 0.16 in the arena and 0.07 in storage2. Skipping the
faces a view cannot see would save part of that, and needs a margin at every
face seam for the filter taps that cross it.

Checked for this change: the rule and the pictures as above; the fourteen
scenarios on both back ends from a private runtime, all passing; the
in-engine self-tests; and the sixty-two static tests that read the touched
files.

### Resident projected tiles on Vulkan

Added on 10 November. A projected light's cached depth is an image of its own
on Vulkan, and every view that reused it copied that image into a block of the
view's atlas to sample it there. The copy serves a view that has moving
casters to draw over the map. For any other view it bought nothing, and after
static cutouts it was the largest shadow phase left in convoy2's start view.

- **An exact hit with no moving caster is read where it lives**
  (`VK_ShadowMap_ProjectedEntryResidentSet`). The receivers bind the cache
  image itself. The pass claims no atlas block and records no copy; its
  cascade rects, its texel size and its translucent moment mapping address
  that image instead of the atlas.
- **A hit that composes keeps the copy**, and so does every hit under the
  shared interaction stream and the debug overlay, which address the atlas.
  `r_vkShadowSampleResident 0` restores the copy for all of them.
- **A view of resident maps records no shadow work at all.** Once each map
  rests in the layout it is read in, the shadow pass returns before it breaks
  the scene's render scope, as it already did for a view of cached point
  cubes. That return now collects the GPU timings of earlier views first:
  nothing else reads them, and a long run of quiet views repeated the last
  report and let its queries age out as dropped. An independent read of the
  change found this, and no rendering fault.
- **The image is sampled and copied, never rendered into**, so it is not an
  attachment. It rests in a transfer layout after a copy and in a read-only
  one after being read in place, and each use moves it from where the last
  one left it (`VK_ShadowMap_TransitionProjectedEntry`).
- **The descriptor sets belong to the cache record**, one for each frame in
  flight, and outlive its images. A record's image is replaced when its slot
  is reused at another size: the image and its view retire behind the frame
  fence, and a frame slot's set is rewritten at its first use after that. It
  is never rewritten once the frame being recorded has bound it; such a pass
  copies for that view.

Quality, the switch alternated in one process, each timed window after a
settling one (six pairs in convoy2, four elsewhere):

| | Hits read in place | Cache phase | All shadow phases | GPU frame |
|---|---:|---:|---:|---|
| convoy2 start | 10 of 11 | 0.54 to 0.04 ms | 0.68 to 0.18 ms | 5.44 to 4.88 ms: 0.56 ms less, spread 0.91 ms, six of six |
| airdefense1 arena | 1 of 2 | 0.26 to 0.11 ms | 0.44 to 0.28 ms | 5.90 to 5.40 ms: 0.50 ms less, spread 1.17 ms, four of four |
| hub2 | 2 of 4 | 0.58 to 0.30 ms | 0.82 to 0.55 ms | not resolved (0.10 ms, spread 0.59 ms) |
| storage2 | no cached projected map | unchanged | unchanged | not resolved |

The phase times are the result. Other sessions were building and running
clients, and the frame figures move more than the phases can explain where
the spread is wide. CPU frame time is not resolved in any of them. The hits
that are not read in place are the ones with a moving caster.

The picture is the same to the byte. In convoy2's start view, paused, two
captures each of the maps read in place, copied, and drawn again without the
cache agree in every pixel above the HUD; with shadows off 158,701 of 2.66
million pixels differ. That held for convoy2's lights, which filter with a
fixed kernel. A light whose kernel turns per texel did not agree until the
turn was taken from the texel within the tile: see "Walking" below.

The validation layers stayed silent in convoy2 and hub2 while the view turned
and, in one process, the switch, the map size (1024 and 512), the cache
budget (8 and 2), the debug overlay, the static cache, the shared interaction
stream and a `vid_restart` were changed.

Checked for this change: the figures, pictures and validation runs above; the
fourteen scenarios on both back ends from a private runtime, all passing, with
in-place reads in every Vulkan case that caches a projected map; the
continuous-motion qualification on Vulkan, which fails on a dropped timing
query; a view held on the no-work return for several hundred frames and then
made to copy again, with no query dropped; the in-engine self-tests; and the
seventy-one static tests that read the touched files. Not run: any graphics
card but the reference one, and Linux or macOS.

### Walking

Added on 10 November, from the measurement meant to price one static cube for
both ownerships of a composed Vulkan light. Every cache figure above was taken
with the camera turning on the spot. Here the player is moved through the
level four units a frame (teleported, no input), which enters and leaves
lights as walking does, and the shadow maps rendered fresh are counted.

On the first walk, 1,200 units through hub2, one light with 77 static casters
rendered both of its cubes again nine times, with the same casters each time.

- **The static signature followed the camera.** It hashed, for each static
  caster, whether the view also saw that caster's stencil volume. A caster the
  map needs but whose volume is off screen is kept for the map alone, and that
  state flips as the player walks past: here, the small light fixtures along
  the route. The depth a caster writes is the same either way, so the bit is
  no longer hashed (`R_RecordShadowMapCaster`). Both renderers share the
  front end.

Quality, 300 steps unless noted, the old signature against the new in separate
launches of one build:

| Walk | Maps rendered fresh | Views that rendered any |
|---|---:|---:|
| hub2, +x, Vulkan | 48 to 24 | 19 to 4 of 411 |
| hub2, +x, OpenGL | 41 to 18 | 21 to 7 of 310 |
| hub2, +y, Vulkan | 716 to 296 | 204 to 84 |
| hub2, -y, Vulkan | 137 to 79 | 62 to 28 |
| airdefense1 arena, -x, 120 steps | 20 to 10 | 8 to 3 |
| airdefense1 arena, +y, 120 steps | 39 to 8 | 18 to 2 |
| airdefense1 arena, -y, 120 steps | 28 to 11 | 14 to 4 |
| airdefense1 arena, +x, 120 steps | 6 to 5 | 3 to 3 |
| storage2, three directions, 150 steps | 478 to 475, 155 to 151, 458 to 454 | every view |

A fresh map is six faces, or one tile, of every static caster of its light.
There is no frame-time figure: other sessions were building, and the walks'
frame means moved by more than the maps cost. storage2 renders two or three
maps in every view for another reason, not looked into.

What is left on these walks is membership that really changes, and that also
follows the camera: a caster joins a light's map when the player comes within
its shadow LOD distance, and an animated model that stands still joins a
quarter of a second after it is first instanced. Each renders the light's
maps again.

**One static cube for both ownerships was not built.** On the first walk it
would have saved 2 of the 48 fresh maps, 4 of the 24 left after this change,
and Vulkan held up to 25 cubes where OpenGL held 15. The light that rendered
most often cannot share at all: it has static casters of its own in the
second ownership.

The picture check for this change turned up a second fault. After a walk, and
then without one, the cached and the fresh pictures of hub2's start view
differed in 19,000 pixels, on both renderers, and all of it was one projected
light with a soft filter.

- **The filter's grain followed the tile.** The projected filter turns its
  kernel by a hash of the texel so that neighbouring pixels sample
  differently, and it counted the texel within the sampled image. The same
  map in another place therefore filtered with another pattern: a cached cell
  against the scratch target on OpenGL; one atlas block against another, or
  against the cached map read in place, on Vulkan. The shadow's shape was
  right in each. The hash now takes the texel within the tile, in both
  receiver shaders, so the grain stays put when the tile moves.

hub2 start view, paused, pixels above the HUD that differ between the cached
and the fresh picture, of 2.66 million:

| | Before | After |
|---|---:|---:|
| Vulkan, cached against fresh | 19,400, by up to 147 levels | 1, by one level |
| Vulkan, read in place against copied | 19,200 | 1 |
| OpenGL, cached against fresh | 20,000 | 77, by up to 24 levels |
| Two captures of one state | 0 to 1,800 (an animated HUD icon) | 0 |

Found on the way and not changed: Vulkan's scratch cube pool only grows (see
the resources table).

Checked for these two changes, on the final build: the walks and pictures
above; the fourteen scenarios on both back ends, all passing; the
continuous-motion qualification on both back ends, whose image case compares
the cache a moving camera leaves behind with a fresh render at eight poses;
the validation layers silent on a Vulkan walk; the in-engine self-tests; the
shader header pin; and the eighty static tests that read the touched files.
The motion qualification needed two repairs of its own first. It took the
host's preset, which keeps point lights on stencil on OpenGL, so it mapped
nothing there; and it froze game time in the frame it placed the camera, so
its first capture could show the view still turning or the exposure still
settling. It now sets `r_shadowMapPointLights 1` and lets the view settle for
a second before freezing. Not run: any graphics card but the reference one,
and Linux or macOS.

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

1. Fullscreen budgets for the three tiers, on the reference machine with
   nothing else running. Every change of this round moved what they cost.
2. Static caster membership still follows the camera: the shadow LOD
   distance admits a caster as the player approaches, and a model first
   instanced counts as moving for a quarter of a second. Each change renders
   the light's maps again, and these are most of what is left on a walk (296
   maps in 472 views on one hub2 route).
3. Vulkan's scratch cube pool only grows: up to 512 cubes, 2.0 GB seen after
   one view from outside the level, against a cache bounded at 512 MB.
4. Static-world volumes are still built once for every mapped light and kept
   for the fallback. Leaving them out too would need the same rebuild on
   return, for every surface the light touches.
5. The OpenGL projected cache is the atlas, four full tiles at Quality and
   Ultra, so it holds one crowded view and nothing of the view just left.
   Vulkan holds eight maps' worth.
6. On Vulkan a projected light's tile size is fitted as if every map of the
   view needed an atlas block. Maps read where they live need none, so a
   crowded view could keep more of its lights at full size. The fit decides
   the cache key, so it must not follow what happens to be cached.
7. One static cube for both ownerships of a composed Vulkan light, as on
   OpenGL. Measured on a walk: 4 of 24 fresh maps and up to ten resident
   cubes. It needs a resident entry two passes of a view can share.
8. storage2 renders two or three maps fresh in every view. Not looked into.

Set aside after measurement: cube faces the view cannot see (see "Static
cutout casters").

Outside the shadow system, found in passing: `idMaterial::ConstantRegisters()`
is NULL for every material, so every surface evaluates its registers every
view (since restored: [Constant material registers](material-constant-registers.md)).
