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
| **Which still casters a map held followed the camera too.** The shadow LOD admitted an entity's shadow within 768 units and dropped it beyond, in the static map as anywhere, and a model's first snapshot counted as the model moving for a quarter of a second. Each change rendered the light's maps again. | **Fixed, both backends.** A static caster of a still light is in the map at any distance, and a first snapshot is not motion. See "Static membership" below. |
| **A still light's maps were rendered again when two of its casters changed places in the list.** The signature chained every static caster into one running hash, in the order the casters reached the light. That order follows the view: entities are sorted by model and, within one model, by where the frame's memory happened to put them, so two door frames or two crates of one model change places as the camera moves. The casters and the depth they write are the same, and the signature was another. In the airdefense1 arena every map rendered fresh on two short walks was for this: 8 and 8 on Vulkan, 8 and 20 on OpenGL. | **Fixed, both backends.** Each caster is hashed by itself and the hashes are added up. See "Caster order and the live window" below. |
| **A caster that had just come to rest could count as moving again.** A caster is live while it has changed within the frames of a quarter second, and that number of frames comes from a frame time that drops at once on a fast frame. When the frame rate rose the window grew by several frames in one step, and a door that had left it a frame earlier was inside it again: out of its lights' static maps and back in, each a change of signature. | **Fixed, both backends.** The oldest frame that still counts as recent never moves back. See "Caster order and the live window" below. |
| **On Vulkan a light with a character in it kept two resident maps of the same static depth.** A cached map holds static depth, and a light's two receiver ownerships differ only in the `noSelfShadow` casters, which in the ordinary case are the characters and are live. OpenGL kept one cache entry for both; Vulkan kept one each, rendered both when the static depth changed, and rendered the LOCAL one again each time a character walked into the light or out of it. | **Fixed (Vulkan).** The GLOBAL pass composes over a copy of the LOCAL pass's resident map. Two or three cubes fewer where a point light's local casters are only characters, and half the resident tiles of hub2's and medlabs's spotlights. See "One static cube for both ownerships" and the section after it below. |
| **A light that moves built the stencil volumes of everything it touches, every frame, and drew none of them.** All of a moving light's interactions are built again each time it moves. A dynamic model's volumes were already left out under a mapped light; a static model's were not, on the reasoning that they are built once. Under a moving light they are built every frame: 27 a view in storage2's alarm room, a quarter of the time that view spent building interactions. | **Fixed, both backends.** Volumes under a light that has moved are left out with the dynamic models'. See "Static volumes under mapped lights" below. |
| Under a light that has never moved, the stencil volumes of static models are built although the light renders shadow maps: about 7 ms and 4 MiB for all of hub2, 5 to 10 parts in a hundred of the work of building interactions. | **By design.** They are built once and make the light's stencil fallback instant; without them every return to stencil would build the light's interactions again whole. See "Static volumes under mapped lights" below. |
| **GL timer queries ran in every shadow phase of every light**, whether or not anything read them. A view with more phases than the 64 query slots polled all 64 unfinished queries for each further phase. | **Fixed.** Queries are issued only while `r_shadowMapReport` or `r_shadowMapDebugOverlay` is on. This alone was about half of the OpenGL CPU frame in hub2. |
| **The point receiver's per-texel path was the common path.** Each of the nine taps compared four texels against their own point on the receiver plane, with a cube-face selection each: 36 comparisons that read a four-by-four block of texels. | **Fixed, both backends.** The default kernel (nine fixed taps, one texel radius, hardware comparison) is one pass over the sixteen texels with the same weights, or nine plain bilinear lookups where the bias covers the block. Other tap counts, radii and the rotated mode keep the tap-by-tap path. 1.6 to 2.6 ms of GPU in hub2 against the old path, in two launches. |
| **Receivers filtered shadows for fragments the light does not reach** (outside the falloff, turned away). | **Fixed, both backends**, in the stock-material path. The PBR variants still sample first. |
| **Entities the game updates once a tic flipped between live and static** whenever the display outran the tic rate, because "live" meant "changed in the last frame". Each flip changed the static caster signature and forced a static re-render. | **Fixed.** A caster is live for the frames of a quarter second (`R_ShadowMapDynamicCasterFrames`). |
| **Mapped lights still did the stencil front-end work.** Three causes. Prelit lights, nine in ten, never shed their volumes. Dynamic volumes were never left out, because the test asked the instantiated snapshot, which is always a static model. And a caster beyond its shadow LOD distance (768 units for every game entity) left the map while its volume stayed, which read as a caster the map cannot hold and put the light on its stencil supplement for the rest of the level; the main light of the airdefense1 arena was one. On Vulkan a fourth: the player's own body and head, never seen in first person, kept every light near the camera "unknown" until their turn in the entity walk. | **Fixed, both backends.** See "Stencil front-end work" below. |

### Agreement with stencil

| Finding | State |
|---|---|
| **A `noDynamicShadows` light lost every shadow** with shadow maps on: it has no per-surface volumes and no map casters, and the mapped-light rule also dropped its dmap prelight. 364 authored lights carry the flag, nearly all in multiplayer maps; the ones with a prelight (197 by a static count of the `.proc` files) shone through walls. | **Fixed** (`R_ShadowMapsNeedPerSurfaceStencilVolumes`). Verified by image on 23 November: waste's `light_8501` (`nodynamicshadows`, prelit), isolated and frozen in six views, shadows the same 0.7% of the one view its prelight reaches under shadow maps as under stencil, with no pixel shadowed by one and lit by the other, on both backends. |
| **Static world geometry with a `noSelfShadow` material stopped shadowing `noSelfShadow` receivers** under prelit lights. Retail links the combined prelight as a GLOBAL shadow; the mapped path routed those surfaces by material into the local chains, so a marine behind such a pillar or container was lit, and the light needed a second map. | **Fixed.** Static-world casters of a prelit light keep global ownership. Verified by image on 23 November: a stock marine crate (`noSelfShadow` material) spawned behind `noSelfShadow` world geometry under mcc_1's prelit `corey_light_3557` is as dark under shadow maps as under stencil on both backends. See "A `noSelfShadow` receiver behind `noSelfShadow` world geometry" below. |
| **Gibs, burning corpses and fading moveables cast whenever `r_useShadowMap` was on**, including under point lights left on stencil at Enhanced, which is exactly the volume retail suppressed. | **Fixed.** They cast only when point lights are mapped too. |
| **The five-tap kernel was one-sided**: the first four taps of the Poisson set all lie left of the centre (centroid 0.41 radius off). Enhanced uses five taps. | **Fixed, both backends.** A balanced four-tap ring. |
| **Packed point depth wrapped at the far envelope**: `fract(1.0)` stored the nearest depth for a caster at or beyond far. Manual (non-comparison) OpenGL path only. | **Fixed.** |
| **Shadow passes restored the viewport without the tiled-capture offset.** | **Fixed** (OpenGL). |
| **The static world was not cut to the light volume.** For a light whose source lies outside its own box (396 point lights and 9 parallel ones in the stock maps), geometry between source and box shadowed the box in a map and never in stencil: a map drew each world surface whole, and the dmap prelight volume, which is the stencil shadow of the static world, holds only the pieces inside the light (`ClipTriByLight` and the shadow frustums). | **Fixed, both backends**, for the 296 of those lights whose prelight volume carries front caps: the static world casts into the map as those caps (`r_shadowMapPrelightCasters`). Entities need nothing: stencil does not cut them either (their run-time volumes are infinite projections of the whole surface). **Open** for the 78 whose volume folds its caps away (a perforated surface casts into it) and the 31 with no prelight. See "What the stock volumes hold" below. |
| **mcc_landing's `light_9083` shadowed structures in its map that stencil leaves lit**: 25% to 49% of four views from the light's entity origin on both backends. A prelit fill light whose source lies 832 units outside its box. | **Fixed, both backends**: 48.9% to 0.01% of the worst view. The map held the terrain's triangles that face the light; stencil casts only from those that face away, and the light's entity origin lies 63 units under that terrain, among wall bases that only the light reaches. See "What the stock volumes hold" below. |
| **mcc_landing's `light_9081` left lit in its map 10.4% of one view that stencil shadows**, on both backends. | **Fixed, both backends**: 10.4% to 0.22%. Its source sits 8 units above its box, inside the ceiling, and the ceiling face lies in the box's top plane: no interaction, so no map caster, while the prelight volume keeps the piece of it under the source. See "What the stock volumes hold" below. |
| **Suns and fill lights from outside lost shadows.** The maps of parallel and distant point lights stored the triangles of open geometry that face the light, the reverse of the stencil rule. A one-sided surface lit from its front blocked the light, and geometry that only has faces turned away cast nothing: q4cmp13's three parallel suns left lit 12% to 31% of a view that stencil shadows, q4ctf2's ten distant lights 3% to 14%. | **Fixed for the static world, both backends**, by the same caster: 30.9% to 0.7% (q4cmp13, worst light, both backends) and 14.3% to 1.4% (q4ctf2, Vulkan). **Open** for the other casters of those lights. See "What the stock volumes hold" below. |
| **A soft shadow's grain followed its tile around.** The projected filter turns its kernel by a hash of the texel, and the texel was counted within the sampled image. The grain therefore changed whenever the light's tile took another place in the atlas, and a cached map differed from the same map drawn fresh: in hub2's start view one light alone changed 19,000 pixels between the two, by up to 147 levels, on both renderers. Reading cached maps where they live on Vulkan added a third place. | **Fixed, both backends.** The hash takes the texel within the tile. See "Walking" below. |
| **Light-facing one-sided geometry cast in maps and never in stencil.** A stencil volume is built from the triangles that face away from the light and from nothing else, and the levels are lit on that understanding: a lamp shines through the one-sided floor it hangs over. The automatic caster mode drew every open surface two-sided, so the same floor blocked its light. storage2's fourteen alarm lights lit almost nothing with shadow maps on: at eight beam angles the mapped picture differed from the stencil one in up to 32,000 pixels on Vulkan and 73,000 on OpenGL. Recorded in September as a contact-detail decision; it is a missing light. | **Fixed, both backends.** Open geometry stores the triangles that face away from the light; a sealed hull keeps its near shell. See "Which triangles cast" below. |
| **The near shell of a sealed hull was its far shell under every parallel and distant point light**, and under any projector whose right and up vectors are left-handed against its direction. Those projections show the light's view mirrored, which swaps the face the rasterizer reports for every triangle, and the culling took no account of it. A sealed hull has the same silhouette either way, so it showed only as the detached contact shadow the near shell was chosen to avoid. | **Fixed, both backends.** The face to discard follows the projection's handedness. See "Which triangles cast" below. |
| **OpenGL lit a surface harder under a shadow map than without one.** Its two shadow receivers normalized the stock normal-map sample, which the classic interaction and Vulkan use as stored. A filtered sample is shorter than a unit vector wherever the relief turns, so every raised edge was drawn brighter with shadow maps on than with stencil shadows or none: in one test view 44,000 pixels were brighter than the picture with no shadows at all. | **Fixed (OpenGL).** See "Receiver lighting on OpenGL" below. |
| On OpenGL a shadow-mapped stock light is about 0.4% brighter than the same light without a map: one level of 255 in up to four mid-bright pixels in ten. The unshadowed light is drawn by the game's own ARB program, which reads its light vector from an 8-bit normalization cube that decodes to 0.996 of unit length; the receivers normalize exactly, as Vulkan does on every path. | **By design.** Matching it would mean reading that cube in the receivers; Vulkan has no such split and is exact. See "Receiver lighting on OpenGL" below. |
| CSM lights are unshadowed past `r_shadowMapCascadeDistance`. Maximum only. | **Open.** |

### Resources

| Finding | State |
|---|---|
| **Quality and Ultra give the projected atlas four cells** (2048 cells in 4096, 4096 in 8192) while Enhanced has sixteen. A light with local casters takes two. OpenGL re-renders the overflow every view; Vulkan marks the light for stencil permanently and can draw it unshadowed for a frame. convoy2's start view does this: up to four projected lights, five lights sent to stencil for good and one drawn unshadowed. | **Fixed, both backends.** A tile size per light, assigned per view. See "Atlas tile sizes" below. No preset changed. |
| Vulkan: cascades in an 8192 atlas always fail (one CSM block is the whole atlas). | **Open**; no preset uses it. |
| On Vulkan a crowded view's spotlights are cut down as if every map needed an atlas block, though maps read where they live need none. | **By design.** The fit cuts lights in one stock view of fifteen, to half size and never less, and at sixteen poses there the picture is the same as with every light at full size: the cut lamps cast no shadow in view. Following the cache instead would make the size, which is part of the cache key, depend on what is cached. See "The tile-size fit and maps read in place" below. |
| OpenGL's projected cache is its atlas, four full maps at Quality and Ultra, where Vulkan keeps eight resident beside its atlas. A view crowded with spotlights evicts on OpenGL what Vulkan keeps: turning in convoy2's start view renders 45 maps fresh against 19. | **By design.** The atlas fills in one stock view of ten, and there the difference is a hundredth of a millisecond a view; eight maps would take a second atlas, 64 MiB at Quality and 256 MiB at Ultra. See "OpenGL's projected cache and its atlas" below. |
| The stencil-fallback flag on a light was never cleared: one failed map pass cost the light its volume elision for the rest of the level. | **Fixed, both backends.** A light leaves the fallback after 240 views without another failure. See "Leaving the stencil fallback" below. |
| **A light with nothing to cast was sent to the stencil fallback.** A receiver pass that found no caster for its map was treated as a map that had failed. OpenGL marked the light, released it after 240 clean views and marked it again, for as long as it was in view; Vulkan marked it in every view and never released it. Such a light has nothing for stencil to draw either. Found by the openD3 session on Doom 3's hell1; no stock Quake 4 view tried shows it. | **Fixed, both backends.** A pass with nothing to cast holds its light only where stencil has a volume for it. See "A pass with nothing to cast" below. |
| OpenGL point cubes carry a colour cube the default receiver never samples: half their memory. The opt-in modern executor still reads it. | **Fixed.** Cubes are depth only while nothing samples the colour. See "Depth-only point cubes on OpenGL" below. At 512 the cache holds 64 cubes in 384 MiB, as on Vulkan; it held 42 in 504 MiB. |
| Vulkan hub2: one light (`lights/squarelight1`) logs "required shadow resource unavailable" once and falls back; OpenGL maps it. | **Open**, present before this round. |
| Vulkan's scratch cube pool only grows. A view takes a scratch cube for each uncached or composed point pass, up to 512 of them, and none is released until the cube size changes. A noclip walk out of the airdefense1 arena into the void, from where every area is visible, mapped 289 point lights in one view and left 2.0 GB resident at Quality's 512 faces. The cache has a memory bound; this pool had none. | **Fixed (Vulkan).** The pool has the cache's memory figure again as its ceiling, and gives back what recent views did not need. See "Scratch cube pool" below. |

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

### Static membership

Added on 10 November, for the membership that still followed the camera after
the walking change. Two rules decided, per view, whether a caster that never
moves was in its light's static map.

- **The shadow LOD dropped it by distance.** Every game entity has a shadow
  LOD distance of 768 units, beyond which its shadow is not cast, to save
  drawing it every frame. A caster first seen from farther away was admitted
  to the map when the player came within that range and left it again beyond
  it, and each crossing rendered the light's maps again. A static caster of a
  light that holds still is now a map caster at any distance: it is drawn
  into the map once and kept. A light that moves draws its map every frame
  whatever is in it, so its casters keep the LOD, and so do casters that
  move. The map still never holds less than the stencil volumes do.
- **A model's first snapshot counted as the model moving.** An animated model
  is given a snapshot of its pose when first looked at. That was read as a
  change of pose, so a still machine or a corpse stood outside the static map
  for a quarter of a second and rendered the map again on joining it. Only a
  snapshot that replaces an earlier one counts now
  (`dynamicModelRegenFrame`). The signature still follows every snapshot.

Maps rendered fresh, Vulkan, Quality, the rules switched in separate launches
of one build:

| Walk | Before | Distance rule | First-snapshot rule | Both |
|---|---:|---:|---:|---:|
| hub2, +x | 24 | 22 | 20 | 18 |
| hub2, -y | 79 | 63 | 79 | 63 |
| airdefense1 arena, -x | 10 | 8 | 10 | 8 |
| airdefense1 arena, -y | 11 | 7 | 11 | 7 |
| airdefense1 arena, +y | 8 | 8 | 8 | 8 |
| hub2, +y | 651 | 659 | 660 | 658 |

The last route is not about membership. Two lights there ride hub2's moving
data router: they move every frame, render every frame, and are 460 of the 545
maps of a run. That is right for a moving light. What is not needed is that
each frame still goes through the cache: the static casters are drawn into a
cache cube, copied, and the live ones drawn over the copy, where one direct
draw would do. Noted below, not changed.

A consequence players can see: a shadow-mapped light now casts the shadows of
still scenery beyond 768 units, where they used to be missing until approached.
The other side of it: an object beyond its LOD range that starts to move, and
is small on screen, loses its shadow while it moves and has it again a quarter
of a second after it stops.

Checked for this change, on the final build: the walks above; cached against
fresh pictures of hub2's start view after a walk, on both back ends, differing
only in the animated HUD icon; the fourteen scenarios and the continuous-motion
qualification on both back ends; the in-engine self-tests; and the static
tests that read the touched files. One run of the qualification's Vulkan image
case failed on 69 faint pixels inside a smoke plume at one pose; two reruns
passed. One static test fails for a reason outside this change
(`ui_number_clipboard.py` no longer compiles its extract of the key code). No
frame-time figure, for the reason given under "Walking".

### Scratch cube pool

Added on 10 November. A view takes a scratch cube for each point pass it does
not read from the resident cache: an uncached map, or the copy a cached one
composes its live casters over. The pool was created cube by cube as views
asked for more, up to 512, and nothing was released until the cube size
changed.

- **A ceiling.** The pool has `r_shadowMapPointCacheMegs` again, the figure
  that bounds the cache, with the cache's floor of twelve cubes: 85 cubes at
  Quality's 512 faces, twelve at Maximum's 2048. A pass beyond it fails as an
  allocation does: its light is shadowed by stencil in that view and tries
  its map again in the next.
- **A trim.** Between frames, cubes that no view has needed for a window of
  five seconds are released, keeping what the last two windows used and a
  quarter more. No command buffer in flight can still name a cube that went
  unused that long.

airdefense1 arena, Quality, with one view from outside the level:

| | Before | After |
|---|---:|---:|
| Scratch cubes after the outside view | 289 | 85 (the ceiling) |
| Resident shadow memory then | 2.0 GB | 1.1 GB |
| Scratch cubes ten seconds after coming back | 289 | 7 |
| Resident shadow memory then | 2.0 GB | 0.6 GB (the cache, at its own bound) |

A second excursion grows the pool again and the validation layers stay silent
through both. Ordinary views use three to a few dozen cubes and never meet
the ceiling at the shipped sizes. An overcrowded view at 2048 faces does: the
lights beyond it are shadowed by stencil while it lasts. (This section first
said they stayed on stencil for the level. They do not: a light in the
fallback still tries its map every view. What it kept for the level was the
cost of building its volumes, which "Leaving the stencil fallback" ends.)

Checked for this change, on the final build: the excursion above under the
validation layers; the fourteen scenarios on both back ends, all passing; the
continuous-motion qualification on Vulkan; the in-engine self-tests; and the
seventy-seven static tests that read the touched files.

### Leaving the stencil fallback

Added on 10 November. A light whose shadow map fails (no cube or tile to be
had, a pass the back end could not draw, a caster its map cannot hold) is
marked, and from then on builds its stencil volumes again so that it has a
fallback. It still tries its map in every view. The mark was never removed,
so one bad frame cost the light its volume elision for the rest of the level.

- **A marked light counts the views it goes through with nothing asking for
  the fallback**, and after 240 of them stops building volumes
  (`r_shadowMapStencilFallbackViews`; 0 keeps the old behaviour). A failed
  pass in either renderer, or a caster the map cannot hold, restarts the
  count, so a light that keeps failing is never released. A light out of
  view is not counted.
- **The release is decided once a view, before any of the light's
  interactions are looked at**, so a view never has some of its volumes and
  not others.
- **A render-thread snapshot does not undo it.** A snapshot carries the mark
  it was taken with, and was copied back when its frame retired; only a
  failure stamped on the snapshot after it was taken marks the light now.

airdefense1 arena on Vulkan, with the view taken outside the level twice: 150
lights are marked there and none is released while the view lasts; back
inside, the two of them that are in view are released after their 240 views;
the second excursion marks no light anew and draws no light unshadowed. The
validation layers are silent. A released light that fails again behaves as
any light does at its first failure.

Corrected on 11 November. On OpenGL a receiver that can only be shadowed by
stencil volumes (custom GLSL lighting, generated geometry) marked its light
only in a view where the volumes were missing. Once marked, the light had
them, so each such view counted as clean; after 240 it left the fallback,
drew those receivers unshadowed for one frame, and was marked again, for as
long as the receiver was in view. Every view that draws such a receiver now
renews the hold (`RB_ShadowMapDrawReceiverFallbacks`). The openPREY session
found it in Prey, where four lights cycled five times each in 900 frames; no
stock Quake 4 view tried here has such a receiver, so the correction is
checked by the code path and its contract, not by a reproduction. Vulkan
keeps the volumes through its receiver scan and was not affected.

Checked for this change, on the final build: that run; the fourteen scenarios
on both back ends; the in-engine self-tests; and the static tests that read
the touched files.

### Moving lights

Added on 10 November. The static cache keeps a light's still casters so that
a view only draws the moving ones over them. A light that itself moves has a
new map every view. Through the cache, each view published a map the next
could not use, copied it, drew the live casters over the copy, and took a
cache record to do it.

- **A moving light draws its maps directly** (`R_ShadowMapMovingLightSkipsCache`,
  asked first by all four cacheable rules, so OpenGL's planner agrees with
  what is drawn). `r_shadowMapCacheMovingLights 1` restores the old path.
- **Moving means the light's geometry changed** within the last two game
  tics: origin, axes, radius, centre, projection. That is stamped apart from
  the light's modification frame, which also follows a colour that animates,
  so a light that only changes colour keeps its cache. The distance rule for
  static casters now uses the same test.

Two first attempts at "moving" were wrong, and the scenarios caught both. The
engine's own test for a changed shape also fails when the prelight model
differs, and a light that has moved once has that model dropped, so it failed
on every later update and a flashlight held still counted as moving for good.
And the casters' quarter-second window kept a light that had just stopped out
of the cache that long, drawing its maps afresh every frame meanwhile; seventy
milliseconds bridge two tics and no more.

The hub2 route with the two lights that ride the data router, Vulkan, Quality,
504 views:

| | Through the cache | Drawn directly |
|---|---:|---:|
| Point passes published and composed | 588 | 24 |
| Point passes drawn in one go | 0 | 567 |
| Composed passes in all | 1,945 | 1,390 |
| Cubes rendered fresh | 651 | 641 |

Each of those passes loses a cube copy, six render scopes and a cache record.

The player's flashlight is the moving light most often on screen, and since
11 November it casts shadows on every preset that draws them. Air Defense 2,
Quality, flashlight on, the view turning, the switch alternated in one
process on a quiet machine, six pairs:

| Vulkan | Through the cache | Drawn directly |
|---|---:|---:|
| Shadow GPU phases | 0.69 ms | 0.34 ms |
| of which the cache phase (the publish) | 0.20 ms | none |
| of which the projected phase | 0.41 ms | 0.28 ms |
| GPU frame | 6.07 ms | 5.72 ms (0.34 ms less, spread 1.03 ms, five of six) |

CPU frame time is not resolved on Vulkan. On OpenGL nothing is resolved over
eight pairs (GPU median of the pair differences 0.2 ms less, CPU 0.7 ms more,
with single windows moving by several milliseconds either way).

Checked for this change, on the final build: the route and the flashlight
figures above; the fourteen scenarios on both back ends, all passing, and the
one that had failed three times more; the continuous-motion qualification on
both back ends; the in-engine self-tests; and the fifty-five static tests that
read the touched files. One scenario harness needed a repair of its own: it
waited thirty frames after placing a light before asking whether its map was
reused, which on a fast enough machine is less than the seventy milliseconds
a placed light counts as moving. It waits in wall time first now.

Checks that passed on the final build: `renderer_shadow_mapping_maps.py`
`caster-cycle-point`, `caster-cycle-cutout` and `door-point` on both backends
(caster counts, cache reuse, cached against fresh images byte-identical; run
again after the Vulkan port, where the point lights now report
`reuse+compose`), the in-engine
`rendererShadowProjectedDiagnosticSelfTest` (now with the cache and live-window
policies) and `performancePresetSelfTest`, and
`renderer_vulkan_shadow_compatibility.py`, which pins every contract above.

### Which triangles cast

Added on 11 November, from the last item on the open list: storage2 rendered
two or three maps fresh in every view.

**What that was.** storage2 has fourteen alarm lights (`spinnyLight_1` to
`_14`): projectors bound to a spinning mover, with shadows on. One to three
are in view, and a light that turns has a new map every frame. Since "Moving
lights" each is drawn directly, one pass, or two while the beam crosses a
`noSelfShadow` receiver, at 0.3 to 0.5 ms of GPU on Vulkan for the one in the
start view. That part needed nothing.

**What it uncovered.** With shadow maps on, the same light lit almost
nothing. The light was isolated and frozen at eight beam angles, and the
mapped picture set against the stencil one and against the picture with no
shadows:

| Pixels that differ from the stencil picture, worst of eight angles | Before | After |
|---|---:|---:|
| Vulkan | 32,070 | 67 |
| OpenGL | 72,831 | 819 |

What is left on OpenGL is the soft edge of shadows both pictures have.

At the worst angles stencil shadowed a few hundred pixels and the map tens of
thousands: the whole room the beam falls into. No receiver setting changed it
(filter, biases, depth comparison, padding, atlas size). Removing one world
surface from the light's casters did: a floor 184 units under the lamp, lit
from above, with the room in question beneath it and visible through glass.

- **A stencil volume is built from the triangles that face away from the
  light**, and from nothing else (`R_CreateShadowVolume` and the turbo path
  both skip a facing triangle). A one-sided surface lit from its front casts
  no stencil shadow, and the levels are lit on that understanding. The
  automatic caster mode drew every open surface two-sided, so in a map the
  floor blocked the lamp.
- **Open geometry now stores the triangles that face away from the light.**
  A sealed hull keeps its near shell: its silhouette is the same from either
  side, and the near shell holds a contact shadow against the bias. The rule
  is one function for the four places casters are drawn
  (`R_ShadowMapCasterFaces`); each renderer only turns its answer into a
  cull face.
- **The face to discard follows the projection.** The first version of the
  rule made the airdefense1 arena worse: its main light, a point light whose
  centre lies far outside its box, lost the shadow of a lamp hood and of a
  column. Such a light, and every parallel light, takes the two axes of its
  projection from `idVec3::NormalVectors`, and they are left-handed against
  the direction the light travels. The map then shows the light's view
  mirrored, and the rasterizer's front and back faces change places. An
  authored projector does the same if its right and up vectors are the other
  way round. The handedness is read from the planes the map is drawn with
  (`R_ShadowMapLightMirrorsCasterWinding`); a cube face is a camera at the
  light and is never mirrored. The same fault had been storing the far shell
  of sealed hulls under those lights since the near shell was introduced.

The arena's main light alone, Vulkan, pixels that differ from stencil: 3,153
two-sided as before, 5,576 with the rule and no handedness, 3,155 with both.

Cube maps were measured apart, with a test point light placed at the alarm
lamp, over the same floor: discarding the rasterizer's back face differs from
stencil in 15,429 pixels, two-sided and front-discarding both in 16,495, on
Vulkan; on OpenGL 83,339 against 83,598.

Whole views, every light, time frozen, the earlier automatic mode against the
new one (`r_shadowMapCasterCulling 4` and `2`), pixels that differ from the
stencil picture by more than 24 levels, the animated HUD strip left out:

| View | Vulkan, earlier | Vulkan, new | OpenGL, earlier | OpenGL, new | The two modes apart, Vulkan / OpenGL |
|---|---:|---:|---:|---:|---:|
| hub2 | 4,504 | 4,504 | 8,589 | 8,590 | 0 / 0 |
| convoy2 | 76 | 61 | 2,327 | 2,320 | 2 / 12 |
| airdefense1, outdoor pose | 11,161 | 11,161 | 11,429 | 11,433 | 0 / 0 |
| airdefense1, start | 19,919 | 19,919 | 24,235 | 24,235 | 0 / 0 |
| airdefense1, arena | 3,272 | 3,403 | 5,248 | 5,477 | 56 / 132 |

storage2's start view and medlabs show no shadow at the frozen moment and
are the same in every mode. The differences from stencil that remain are
filtered edges, the same in both modes.

So outside the lights the old rule blocked, the two modes draw the same
picture, to within 132 pixels of 2.6 million.

A map drawn under the new rule rasterizes about half of each open caster.
hub2, Vulkan, Quality, the static cache off so that every map is drawn in
every view, the two modes alternated in one process, six pairs: the shadow
GPU phases fall from 1.41 to 0.87 ms (projected 0.99 to 0.66, point 0.41 to
0.21). The GPU frame was 0.57 ms shorter in four of the six pairs, with a
spread of 2.8 ms: other sessions were running clients. With the cache on, a
view draws few maps and the difference is that much smaller.

`r_shadowMapCasterCulling` has two more values: `3` applies the stencil rule to sealed hulls as well, and
`4` is the earlier automatic mode, kept for comparison.

Not verified: no stock projector with left-handed vectors was found in a
test view, so that class rests on the same determinant as the two that were
measured; and nothing was run on another GPU.

Checked for this change, on the final build: the eight angles and the whole
views above on both renderers; the fourteen scenarios on both back ends; the
continuous-motion qualification; the in-engine self-tests; and the static
tests that read the touched files.

### Receiver lighting on OpenGL

Added on 11 November, from a figure in the section above: on OpenGL the test
point light's mapped picture differed from the stencil one in 83,000 pixels,
where Vulkan's differed in 16,000, in every caster mode.

It was not shadow. OpenGL's stencil and unshadowed pictures match Vulkan's
(4,300 and 5,200 pixels apart in the left half of the view); OpenGL's mapped
picture differed from Vulkan's mapped one in 50,400, in thin lines along the
relief of every panel. And where stencil casts no shadow, the mapped picture
was brighter than the picture with no shadows at all, in 44,000 pixels. A
shadow map can only take light away. No receiver setting moved it, and it was
the same with multisampling off.

- **The two OpenGL shadow receivers normalized the stock normal-map sample**
  (`shadow_interaction.fs`, `shadow_point_interaction.fs`). The stock
  interaction uses the sample as stored, and so do `material_interaction.fs`,
  which draws a light without a shadow map, and the three Vulkan interaction
  shaders. A filtered sample is shorter than a unit vector wherever the bump
  detail turns, and that shortfall dims the edge; normalized, the edge is lit
  at full strength. The classic shader was given the stock decode in July
  and the receivers were not.
- **They now use the sample as stored**, and carry the stock specular term's
  doubling inside the term as the classic shader does, so that cel banding
  clamps and steps the same value on both paths. Without cel banding that
  part changes nothing.

OpenGL, Quality, pixels that differ from the stencil picture by more than 24
levels, before and after, with Vulkan's figure for the same view:

| View | OpenGL before | OpenGL after | Vulkan |
|---|---:|---:|---:|
| Test point light, left half of the view | 57,819 | 11,952 | 11,939 |
| of which brighter than no shadows at all | 44,091 | 37 | 0 |
| storage2 alarm light, worst of eight angles | 819 | 35 | 67 |
| hub2 | 8,590 | 3,442 | 4,504 |
| convoy2 | 2,320 | 61 | 61 |
| airdefense1, outdoor pose | 11,433 | 11,403 | 11,161 |
| airdefense1, start | 24,235 | 19,967 | 19,919 |
| airdefense1, arena | 5,477 | 3,609 | 3,403 |

So the figures for OpenGL in "Which triangles cast" carried this fault; the
two renderers now agree with stencil equally.

A loose copy of the two shaders under the save path had no effect on the
picture; the change showed only once a build had packed them.

**What is left on the stock path.** After the change the mapped picture is
still brighter than the unshadowed one by exactly one level in 345,000 pixels
of that view, by two in 1,300, and by more than eight in 400. The share rises
with brightness, from 3% of dark channels to 41% around mid-grey, which is a
light about 0.4% too strong, not an offset. On OpenGL a stock light without a
map is drawn by the game's own ARB program (`interaction.vfp`), not by
`material_interaction.fs`, which serves cel banding and the enhanced
materials. That program reads the light vector from the normalization cube,
stored as `128 + 127 v` and read back as `2 c / 255 - 1`: 0.996 of unit
length. The receivers normalize exactly. The size fits; neither side was
changed to prove it. Vulkan draws both with one shader and shows no pixel
brighter under a map than without one.

**The PBR branch.** Checked in the PBR laboratory (24 authored stations, a
point light and a projector that cast, two fills that do not), built on this
session's binaries, with the stations captured unshadowed, under stencil and
under maps in one process. A PBR receiver, mapped or not, is lit by one
function that takes the shadow factor as its argument (`OpenQ4PBRDirect`),
and the pictures agree:

| Pixels brighter than the unshadowed capture, outside the translucent station | Stencil | Projector mapped | Point light mapped too |
|---|---:|---:|---:|
| Vulkan | 0 | 0 | 0 |
| OpenGL | 0 | 10,843, by one level | 88,061, by two at most |

OpenGL and Vulkan differ from each other in 5 to 14 pixels of these captures.
The one or two levels on OpenGL do not follow the filter (the same with one
tap) or the translucent receiver setting.

**Corrected on 17 November: none of those pixels is PBR.** The counts above
cover the whole frame outside the translucent station, and the laboratory
room around the stations is a classic material. Captured again with an
ownership mask (`r_pbrDebug 7`, one pixel eroded at the edges), the 175,000
pixels of the PBR stations are never brighter mapped than unshadowed, with
the projector mapped or the point light too; every brighter pixel is on the
room. That is the stock-path difference recorded above (the ARB program's
normalization cube, 0.996 of unit length), by design. The same room drawn
with `r_pbrMaterials 0` shows it on the stations as well, and the modern
OpenGL path, which draws the room with exact vectors, shows no pixel brighter
anywhere (see "Modern OpenGL receivers and PCSS-lite" below).

One station is lighter whenever shadows are on, on both renderers and under
stencil as much as under maps: the translucent one. That is the ownership
rule, not the receivers. A view in which a shadow-casting light reaches a
translucent receiver hands every translucent surface back to the classic
path, because a light replayed at the blend stage cannot carry its shadow
(`RB_GLPBR_TranslucentViewAdmits`, `VK_PBRTransparentViewAdmits`).

Checked for this change, on the final build: the figures above; the fourteen
scenarios on both back ends; the continuous-motion qualification; the
in-engine self-tests on both renderers; and the 74 static tests that read the
touched files. The enhanced-material branch of the receivers reads the same
as the classic shader's; the PBR branch was not compared with its unshadowed
counterpart.

### Modern OpenGL receivers and PCSS-lite

Added on 17 November. The modern OpenGL path (the opt-in modern visible frame,
`ModernGLShaderLibrary.cpp`) filtered projected shadows with a fixed five-tap
box at the contact radius: no blocker search, no rotation, and a kernel unlike
the classic shader's and Vulkan's. Since the October fix for grazing-wall acne
it had used the contact radius because the PCSS-lite search bound, as a fixed
kernel, shadowed walls nothing occludes.

- **The shadow descriptor carries the projected filter.** One `vec4` appended
  to the GPU record (`filterParams`: contact radius, taps + 32 x mode, PCSS
  search and maximum radii, both zero unless the mode is 2), filled from
  `R_ShadowMapProjectedFilterSettings` like the other two receivers. 64 UBO
  records are now exactly 32 KiB.
- **The modern receiver runs the classic kernel.** Rotation by a hash of the
  texel within the tile, the 1/5/9/13-tap sets with the balanced five-tap
  ring, the per-tap footprint bias, and under PCSS-lite the blocker search
  (`ModernClusterProjectedPCSSRadius`, the classic `ProjectedPCSSRadius` with
  its footprint bias per probe). The atlas guard and the receiver-plane bias
  use the effective radius, as the classic shader does. `pcfKernel` stays the
  contact radius.

PBR laboratory, overview camera, OpenGL, every light mapped. Widening the PCSS
radii to 16 texels against the defaults (4 and 8) is the control: a receiver
without a blocker search cannot respond to it.

| Pixels changed by more than two levels | Modern, before | Modern, after | Classic receivers |
|---|---:|---:|---:|
| PCSS radii widened to 16 | 0 | 9,601 | 9,813 |
| PCF radius widened to 8 (mode 1) | 17,192 | 29,315 | 22,129 |

The widened penumbrae fall on the same pixels on both paths (84% overlap) and
change them in the same direction in 99.7% of them. At the default radii the
modern picture with PCSS-lite and with plain PCF differ in 52 pixels; the
kernel itself moved 7,700 pixels against the old box. Four room patches no
station can shadow are identical to the unshadowed capture on the modern path
before and after (mean and maximum difference 0).

**A laboratory gap found on the way.** Under the laboratory's settings the
modern path did not own any shadowed frame before or after this change ("PBR
did not own the visible frame"): the first-run preset leaves point lights on
stencil, and a view with a stencil-shadow pass falls back to the classic path
(blocker `pass=stencilShadow`, reason `missingVertexBuffer`). The figures above
set `r_shadowMapPointLights 1`. The laboratory's modern shadow controls need
the same setting to test the modern receivers at all.
Vulkan was not rebuilt or captured for this change; its receivers are
untouched.

### Caster order and the live window

Added on 11 November, for the open item "the world's own geometry joins and
leaves a light's casters area by area as the player moves", which had been
seen on hub2's two moving lights and not measured on still ones. A temporary
front-end diagnostic compared, for every light, the static casters of one
main view with those of the next, and wherever the signature of a light that
holds still had changed it named the casters gained, lost or altered and the
rule that had turned a lost one away.

The walks themselves gave the first answer before the diagnostic had named
anything: lights whose signature changed with no caster gained or lost.

- **The same casters in another order.** The signature was one running hash:
  each static caster was hashed on top of the result of the one before it.
  Casters reach a light in the order the view's entities are walked, and
  `R_SortViewEntities` orders them by model and, within a model, by the
  address the frame's allocator gave each view entity. Two entities that
  share a model therefore change places as the camera moves and others enter
  the view. Nothing in a map depends on the order its casters are drawn in.
  Each caster is now hashed from the same start, its bits are spread, and the
  results are added (`R_RecordShadowMapCaster`); a sum is the same in any
  order. The cache keys carry the static caster count beside the signature,
  so nothing reads a zero sum as "no caster".

With the sum in place the diagnostic reported sixteen changes of signature on
still lights over hub2's -y walk, 300 steps. They were of three kinds, and
only the last is the item as it was written.

- **A caster at rest counted as moving again.** `R_ShadowMapCasterIsDynamic`
  asked whether the caster had changed since "this frame less the window",
  and the window is a quarter second's worth of frames at the smoothed frame
  time, which follows a fast frame at once and a slow one gradually. One
  fast frame lengthened the window by several frames, so the limit moved
  back: a door static at frame 953 was live at 955 and static at 956, in
  the maps of three lights. Six of the sixteen changes. The limit is now
  kept from frame to frame and only advances
  (`R_ShadowMapLiveCasterSinceFrame`); a window that shrinks is followed at
  once, and a frame counter that starts again starts the limit again.
- **Doors that move.** A door is live from the frame it starts to open until
  a quarter second after it stops, and the static map is rendered without it
  and then with it. Nine of the sixteen: the two leaves of one lift door,
  under three lights. That is the composition rule doing its work: the
  door's shadow is drawn over the stored map each frame while it moves.
- **An area joining.** Once, a still light gained fifteen casters in a single
  view when that door opened: surfaces of the world model of the area behind
  it, and two light fixtures there. A caster in an area that no chain of
  open portals connects
  to the view is left out of the map (`R_ShadowMapEntityTouchesConnectedArea`):
  nothing in that area can be seen, and a light by a door would otherwise
  draw the room behind it into every map. When the door opens the room can
  be seen and its casters are needed. Apart from that door, no still light
  on the walk gained or lost a surface of the world.

So the world does not join a still light's casters area by area as the player
walks. It joins when a door connects an area, the map needs it then, and it
costs one render of that light's maps at that moment. Not changed.

Maps rendered fresh, Quality, the old signature against the sum in separate
launches of one build, with the live window as it was:

| Walk | Vulkan | OpenGL | Views that rendered any, Vulkan | OpenGL |
|---|---:|---:|---:|---:|
| hub2, +x, 300 steps | 18 to 18 | 15 to 15 | 2 to 2 | 5 to 5 |
| hub2, -y, 300 steps | 63 to 57 | 37 to 31 | 20 to 19 | 15 to 14 |
| airdefense1 arena, -x, 120 steps | 8 to 0 | 8 to 0 | 2 to 0 | 2 to 0 |
| airdefense1 arena, +y, 120 steps | 8 to 0 | 20 to 0 | 2 to 0 | 5 to 0 |

The kept limit took the diagnostic's count on hub2's -y walk from sixteen
changes to ten. It does not show in fresh maps on these walks. Vulkan's
counts did not move, most likely because its cache still held the map of the
signature a light came back to. OpenGL's hub2 +x walk gave 15, 15, 14 and 16
in four launches (the old signature, the sum, the sum with the limit, and
the final build), which is the spread of that walk and not a difference:
OpenGL's walks are not the same views from launch to launch. The change is
kept for what it removes, a caster at rest classed as moving, and not for a
figure. What is left on the two hub2 walks is first renders of lights
entering the view, the doors, and the one area.

The final build, one launch of each walk: Vulkan 18, 57, 0 and 0; OpenGL 16,
31, 0 and 0.

Nothing here changes a picture. A map is rendered again less often; what it
holds is what it held.

Checked for this change, on the final build with the diagnostic and its
switches removed: the walks above; the fourteen scenarios on both back ends;
the continuous-motion qualification, eight cases; the in-engine self-tests on
both renderers, which now exercise the kept limit; and the static tests that
read the touched files. The qualification's OpenGL image case failed once, at
a pose where two captures of the same frozen frame differed from each other,
while other tests were compiling on the machine; run alone it passed. Of 72
static tests 64 pass, four need arguments and two were stopped at four
minutes. Two fail for reasons outside this change: `validation_hardening.py`
(a test added on 11 November is not wired into the push workflow) and
`ui_choice_scroll_runtime.py` (its native extract links against libraries of
another build type). No frame-time figure, for the reason given under
"Walking".

### Static volumes under mapped lights

Added on 11 November, for the open item "static-world volumes are still built
once for every mapped light and kept for the fallback". A temporary counter
timed every stencil volume built for a static model under a light that was
going to render shadow maps, and a switch left those volumes out the way a
dynamic model's are.

Quality, the volumes of static models built under mapped lights:

| | Vulkan | OpenGL |
|---|---:|---:|
| hub2, loading and settling | 924 to 1,244 volumes, 6.3 to 9.2 ms | 1,226 volumes, 5.6 ms |
| hub2, a 1,200 unit walk | 165 and 602 more, 0.9 to 1.5 ms | 165 more, 0.6 ms |
| hub2, the worst single view | 0.7 ms | 0.4 ms |
| hub2, memory they hold | 3.3 to 4.2 MiB | 4.0 MiB |
| airdefense1 arena, loading and settling | 160 volumes, 2.6 ms | 159 volumes, 4.7 ms |
| airdefense1 arena, a 480 unit walk | none | none |

Building the interactions these volumes belong to took 76 to 126 ms in the
same hub2 launches and 43 to 49 ms in the arena, so the volumes are 5 to 10
parts in a hundred of work that is itself done once, when a light and a
surface first meet. What they hold is index lists: the vertex program shares
the surface's own vertices. On the walks 155 of 165 and 331 of 602 were world
surfaces under a prelit light, which retail never builds, because the baked
prelight stands for them, and which a mapped light needs surface by surface.

With the switch on nothing else moved: the -y walk rendered the same 57 fresh
maps in two launches each way. (A fifth launch, switch on, rendered 589: two
projected lights drew a fresh map in every view of it and some 27 static
surfaces were built again each view. That is the picture of two lights that
move, the case of the second point below, and it did not come back in four
further launches.)

- **Under a light that has never moved the volumes stay.** Leaving them out
  would save about 7 ms and 4 MiB a level. It would cost, each time a light
  returned to stencil (a map that failed, an atlas that cannot hold the view,
  which sends every atlas light of that view back at once, an update budget,
  a receiver that needs stencil), building every interaction of that light
  again whole, light surfaces as well as volumes: the work of which the
  volumes are a twentieth to a tenth. And a subview, which under the default
  policy draws a light whose cached map it cannot reuse from the stored
  volumes, would have to render that light's map instead.
- **Under a light that moves they are no longer built.** Every interaction
  of such a light is built again each time it moves, static geometry
  included, so its volumes were built every frame, and under a map none was
  drawn. storage2's alarm room built 27 a view for its spinning lights, 54 of
  the 214 microseconds a view spent building interactions. They are left out
  now with the dynamic models' (`volumeIsRegenerated` in
  `idInteraction::CreateInteraction`), by the engine's own mark for a light
  that has moved (`lightHasMoved`, which also drops its prelight) and not by
  the moving window, so that a light which settles keeps what it was given.
  If such a light returns to stencil its surfaces are built again with their
  volumes, by the rule that already did this for dynamic models. A subview
  that sees a moving light needs that light's map now, where static geometry
  alone could be drawn from volumes before.
- **A static model that moves under still lights is left as it is.** A lift
  in hub2 builds 7 or 8 volumes a view while it travels, 40 to 45
  microseconds. An entity carries no mark of having moved, only of having
  been touched recently, which every entity has when a level starts.

storage2's alarm room after the change, 340 frames on each renderer: no
stencil volume built in any frame, against 21 a frame with
`r_shadowMapSkipStencilShadows 0`. With the lights frozen at nine moments and
the room then switched from shadow maps to stencil, which is the rebuild, the
two pictures differ in 0 to 77 pixels on Vulkan and 3 to 7 on OpenGL, and the
stencil picture differs from the unshadowed one in up to 37,000: the volumes
are there when they are asked for.

Checked for this change, with the counter and its switch removed: the figures
of the last paragraph; the fourteen scenarios on both back ends; the
continuous-motion qualification, eight cases; the in-engine self-tests on
both renderers; and the static tests that read the touched files. One
scenario failed once: `door-portal` on Vulkan published the flashlight's map
in the very view of its first cached capture, where the check wants a reuse,
while the other scenarios were running one after another on a busy machine;
run alone it passed twice. Of 68 static tests 61 pass, four need arguments
and one was stopped at four minutes. The two that fail are the two of the
last round, for the same reasons outside this change
(`validation_hardening.py`, `ui_choice_scroll_runtime.py`). No frame-time
figure: the counts are exact, and 54 microseconds a view is below what a
frame time on this machine can show.

### A pass with nothing to cast

Added on 11 November, from a report by the openD3 session, which runs this
renderer on Doom 3. On hell1 three point lights have receivers and no caster
the map accepts, and each of them was sent to the stencil fallback, left it
after 240 views and was sent back, for as long as it was in view.

A light renders one map for each kind of receiver it has. Both back ends
treated a receiver pass that found no caster as a map that had failed:

- **OpenGL** marked the light whenever the front end had expected it to map
  (`RB_ShadowMapRunPass`). Marked, the light builds its stencil volumes
  again; nothing asks for them, so after the clean views it is released, the
  front end expects a map again, and the next view marks it.
- **Vulkan** marked it in every view, whatever the front end expected
  (three places in `VK_ShadowMap_PrepareViewLights`). Each mark restarts the
  count of clean views, so the light never left the fallback.

Restoring volume generation helps only if there is a volume to restore. By
the time a back end runs, the front end has already marked the light for
every caster that stencil could draw and the map does not hold, in the same
view. What arrives with an empty pass is a light that stencil has nothing
for either: no caster at all; a point light whose only caster is its own
fixture; casters that neither representation draws, by distance or behind a
closed door; or, as on hell1, casters and receivers that are all
`noSelfShadow`, whose volumes shadow other receivers and never each other.

- **The front end now records what it left out.** For each receiver
  ownership, whether a stencil volume would have shadowed it in this view
  and does not because the light is going to map: a volume that exists and
  was not linked, or one that was not built
  (`viewLight_t::shadowMapElidedStencilMask`, set in
  `idInteraction::AddActiveInteraction`). A `noSelfShadow` caster counts for
  global receivers only.
- **An empty pass holds its light only where stencil has a volume for that
  ownership**, linked in this view or left out
  (`R_ShadowMapEmptyPassHasStencilVolumes`, one rule for both back ends).
  The pass is drawn unshadowed, as before, and the light stays a mapped
  light. A light that has volumes and no map caster is a stencil light, as
  before, and is held for as long as that lasts.

What the wrong mark cost is small and never showed in a picture: a marked
light builds and links stencil volumes for whatever moving thing enters it,
which a mapped light does not, and it wrote a line to the developer log at
each turn of the cycle.

No stock Quake 4 view tried shows the case: no light is marked in the start
views of ten levels on OpenGL, in four of them on Vulkan, or on six walks on
either renderer. It was reproduced with a temporary switch in the front end
that gave every light of hub2's start view nothing to cast, the camera
still, 750 views:

| | OpenGL | Vulkan |
|---|---:|---:|
| Before: lights marked | 13, 57 times | 13, once each |
| Before: lights released | 11, 44 times, and marked again | none |
| After: lights marked | none | none |

The other direction was checked with a second switch that hid the casters
from OpenGL's back end only, so that the front end had left volumes out:
twelve lights were marked once and held, and thirteen passes were drawn with
stencil shadows. A view from outside the airdefense1 arena, where Vulkan runs
out of shadow resources, marks 162 lights before and after.

Checked for this change, with both switches removed: no light marked in four
start views on each renderer, and the 162 from outside the arena; the
fourteen scenarios on both back ends, all passing; the continuous-motion
qualification, eight cases; the in-engine self-tests on both renderers,
which now exercise the rule; and the static tests that read the touched
files. The qualification's Vulkan image case failed once, at a pose where
two captures of the same frozen frame differed from each other, while
another session's test client was running; run alone it passed. Of 51 static
tests 45 pass, four need arguments, one was stopped at four minutes and one
fails for a reason outside this change (`ui_choice_scroll_runtime.py`). Not
checked here: Doom 3's hell1, which is the openD3 session's to confirm.

### OpenGL's projected cache and its atlas

Added on 11 November, for the open item "the OpenGL projected cache is the
atlas, four full tiles at Quality and Ultra, so it holds one crowded view and
nothing of the view just left". On OpenGL a cached projected map lives in the
atlas and is sampled there, and a map that is not cached is drawn into one
scratch tile; Vulkan keeps eight maps' worth resident beside its atlas.

Ten start views, Quality, the view turning through full circles for twelve
seconds, point lights left on stencil so that every pass counted is an atlas
pass:

| Level | Most atlas passes in a view | OpenGL: maps rendered fresh | OpenGL: evictions | OpenGL: atlas cells used, of 64 | Vulkan: maps rendered fresh |
|---|---:|---:|---:|---:|---:|
| convoy2 | 10 | 45 | 12 | 64 | 19 |
| hub2 | 4 | 2 | 0 | 32 | 3 |
| medlabs | 4 | 0 | 0 | 32 | 1 |
| convoy1 | 2 | 1 | 0 | 16 | 2 |
| building_b | 2 | 2 | 0 | 32 | 2 |
| network1 | 2 | 2 | 0 | 32 | 2 |
| storage1, putra, waste, mcc_landing | 0 | 0 | 0 | 0 | 0 |

The atlas fills in one view of the ten, the one that led to the tile sizes:
convoy2's start, with up to ten atlas passes. There OpenGL evicts twelve
times and renders 45 maps fresh in 38 of its 746 views, where Vulkan renders
19. In the other nine it is half full at most, nothing is evicted, and the
two renderers render the same handful of first maps.

What the 26 extra maps cost: a convoy2 view that renders a fresh projected
map spends 0.39 ms of processor time and 0.14 ms of graphics-card time on
map rendering, against 0.13 and 0.02 ms in a view that renders none. That is
about a quarter of a millisecond of processor time and a tenth of
graphics-card time a map, some 7 ms and 3 ms in all over twelve seconds of
turning, or a hundredth of a millisecond a view.

Holding eight maps on OpenGL would take a second atlas of the same size, 64
MiB at Quality and 256 MiB at Ultra, with every cached tile carrying which
of the two it is in: the cache, the receivers' binding, the prepared pass of
the shared interaction stream and the debug overlay all name the one atlas
today. That is not worth a hundredth of a millisecond in one view. Left as
it is. A level that did crowd its spotlights can be given sixteen maps'
worth today with `r_shadowMapAtlasSize 8192`, which also lets the front end
keep more lights at full size.

Checked: the counts above, one launch a level on each renderer. No code
changed.

### The tile-size fit and maps read in place

Added on 11 November, for the open item "on Vulkan a projected light's tile
size is fitted as if every map of the view needed an atlas block". The front
end sizes a crowded view's atlas lights so that one block for each of their
passes fits the atlas (`R_ShadowMapAssignAtlasTiles`). Vulkan reads a cached
map with nothing live in it where it lives and takes no block for it, so in
a settled view most of the atlas the fit made room in stays empty, and the
lights could have kept their size.

Where the fit cuts anything, Vulkan, Quality, fifteen start views with the
view turning for twelve seconds:

| Level | Most atlas passes in a view | Atlas lights cut below 2048 |
|---|---:|---|
| convoy2 | 10 | in 29 of 75 reports: 163 light-reports at 1024, 38 at 2048, none smaller |
| hub2, medlabs, storage2 | 4 | never |
| convoy1, building_b, network1 | 2 | never |
| hub1 | 1 | never |
| airdefense1 (two views), airdefense2, waste, putra, storage1, mcc_landing | 0 | never |

One view of fifteen, the one the tile sizes were made for. Its passes are
served as the item says: of 273 passes in those reports 242 were read in
place, 27 composed over a copy and 4 rendered.

What the cut costs there was looked for in pictures: time frozen, point
lights left on stencil so that only atlas lights are mapped, the fit's
picture against the same view with `r_shadowMapAtlasSize 8192`, which keeps
every light at 2048.

| convoy2, from the start position | Pixels that differ at all |
|---|---:|
| Eight directions at eye level, fit against every light at 2048 | 0 in all eight |
| Four directions looking 25 and 50 degrees down, the same | 0 in seven, 1 pixel by one level in one |
| The same sixteen poses, fit against every light at 256 | 0 in fifteen, 1 pixel in one |
| OpenGL, one pose, fit against 256 | 0 |
| hub2's start view for comparison, 2048 against 1024 | 56,381, of them 5,196 by more than 24 levels |

The lamps the fit cuts cast nothing that can be seen from there. Isolated
one at a time on OpenGL, five of six shadow no pixel at all and the sixth 34
pixels by less than 8 levels: they are small spotlights in wire caps whose
light volumes reach the view and whose shadows do not. hub2 shows what a
halved map costs a light whose shadows are in view, and it is the order of
the fit, smallest lights first, that keeps such a light at full size.

Letting Vulkan's lights keep their size would also need more than a
different count. A map that is not cached yet is rendered into an atlas
block and copied to its resident image, so the first view of a crowded place
needs a block for every pass whatever the cache holds, and the size is part
of the cache key: a fit that followed the cache would render at one size,
find the maps cached, choose another size and miss. The resident images
would have to become render targets (they are created as copy targets), or
first renders be spread over several views with lights unshadowed
meanwhile. Not worth doing for a difference no stock view shows. Left as it
is.

Checked: the counts and pictures above. No code changed.

### One static cube for both ownerships

Added on 12 November, for the open item "one static cube for both ownerships
of a composed Vulkan light, as on OpenGL". A light keeps a map for each kind
of receiver: the LOCAL map, for receivers that do not shadow themselves,
holds the light's global casters, and the GLOBAL map holds the local
(`noSelfShadow`) casters as well. The cache keeps static depth only; live
casters are drawn over a copy of it in each view. So the two cached maps are
the same whenever no local caster is static, which is the ordinary case of a
character standing in a light: its skin is `noSelfShadow`, and it moves.

OpenGL has kept one cache entry for both in that state all along. Vulkan did
so only for a light with no local caster at all. With a character in it a
point light kept two resident cubes holding the same static depth, rendered
both when that depth changed, and gave the LOCAL cube another name each time
the character walked in or out, so that it was rendered again.

- **One rule for both back ends** says when the two ownerships hold the same
  resident depth (`R_ShadowMapOwnershipsShareResidentDepth`): no static and
  no translucent local caster, and any live local casters composed.
  OpenGL's `RB_ShadowMapCachePassKind` is that rule now, unchanged in what
  it decides.
- **On Vulkan the GLOBAL pass of such a point light owns no cube.** It names
  the resident cube the LOCAL pass has restored, or is publishing in the
  same view, takes a scratch cube and composes its live casters over a copy
  (`VK_ShadowMap_SharePointStaticCube`, `vkShadowPassState_t::sharesStatic`).
  The LOCAL pass runs first within the light, so the cube is rendered and
  back in its sampled layout before the copy. If the LOCAL pass fails the
  sharing pass goes with it; if the sharing pass fails the cube stays.
- **Projected lights were not included at first.** See "One resident tile
  for both ownerships of a projected light" below.

Vulkan, Quality, the view turning for twelve seconds, reports every tenth
view. A paired light is one whose two passes are both served from the cache
without one aliasing the other:

| Level | Paired point lights in a view, most | Of those light-reports, sharing now | Resident point cubes, most: before, after | Point maps rendered fresh: before, after |
|---|---:|---:|---:|---:|
| medlabs | 4 | 49 of 263 | 15, 13 | 112, 103 |
| network1 | 9 | 115 of 236 | 28, 25 | 3, 1 |
| hub2 | 8 | 0 of 147 | 47, 47 | 8, 8 |
| airdefense1 arena | 5 | 0 of 184 | 12, 12 | 1, 0 |

The lights that do not share have a static `noSelfShadow` caster as well as
the live ones: scenery with that material is common in hub2 and the arena,
and there the two maps really differ. Where the local casters are only
characters, two or three cubes go, 6 MiB each at Quality's 512 faces. A
second launch with the change gave 104 fresh maps in medlabs, 3 in network1
and 2 in the arena, so only medlabs's figure is outside a launch's spread.
The cube counts are the most a run reached: network1 gave 25, 25 and 24 in
three more launches with the change, and medlabs 13 and, with the validation
layers on, 15.

The picture cannot be compared with time frozen in the usual way, because a
frozen caster stops being live within a quarter second and the light leaves
the state. In network1 time was frozen and both forms captured inside that
quarter second, two lights sharing in one capture and none in the other: no
pixel differs, against 160,000 between shadows and none. With time frozen for
longer, hub2, the arena, medlabs and network1 are identical too. The
validation layers reported nothing over 1,873 sharing passes in medlabs and
network1, five of them composed over a cube published in the same view.

Checked for this change, on the final build with the comparison switch
removed: the validation layers again, silent over 156 sharing passes; the
fourteen scenarios on both back ends, all passing; the continuous-motion
qualification, eight cases, all passing; the in-engine self-tests on both
renderers, which now exercise the shared rule; and the static tests that
read the touched files. Of 67 static tests 63 pass, two need arguments and
one is the scenario runner itself, stopped after four minutes. The one that
fails does so for a reason outside this change (`validation_hardening.py`:
a test added on 12 November is not wired into local validation). OpenGL's
rule is unchanged in what it decides and was not measured again.

### One resident tile for both ownerships of a projected light

Added later on 12 November: what the last section did for point cubes, for
the tiles of projected, parallel and distant point lights on Vulkan. The
state is the same, a light whose `noSelfShadow` casters are all live, and so
is the rule (`R_ShadowMapOwnershipsShareResidentDepth`). Such a light kept
two resident tiles of the same static depth; it keeps one.

- **The GLOBAL pass names the LOCAL pass's tile** and takes an atlas block,
  into which the tile is copied and over which its live casters are drawn
  (`VK_ShadowMap_ShareProjectedStaticTile`). In a view where the LOCAL pass
  publishes the tile, the copy follows the publication, as it does for any
  light restored in the view another light published in.
- **The LOCAL pass often reads its tile where it lives**, which the copy must
  not disturb: a tile is a copy source in one layout and sampled in another.
  The restore phase therefore makes every copy first and only then gives the
  tiles that are read in place the layout their receivers sample
  (`VK_ShadowMap_RenderAtlas`). Before, the two were done light by light,
  which was right while no tile had two users.
- **The front end's fit is unchanged.** The sharing pass still takes its
  atlas block, so the view asks for what it asked for.

Vulkan, Quality, the view turning for twelve seconds, a tile for each
ownership against one:

| Level | Paired atlas lights, light-reports | Of those, sharing | Resident tiles, most: before, after | Resident maps' worth, most: before, after |
|---|---:|---:|---:|---:|
| hub2 | 71 | 71 | 4, 2 | 6.00, 3.00 |
| medlabs | 79 | 79 | 4, 2 | 4.00, 2.00 |
| convoy1 | 1 | 1 | 2, 1 | 4.00, 2.00 |
| convoy2 | 72 | 24 | 15, 14 | 8.00, 8.00 |
| building_b | 29 | 0 | 2, 2 | 4.00, 4.00 |
| network1 | 24 | 0 | 2, 2 | 4.00, 3.00 |

A full-size map is 16 MiB at Quality and 64 MiB at Ultra, so hub2 holds 32
to 48 MiB less (a second launch with the change reached 4.00 maps' worth)
and medlabs 32. The count of maps' worth includes images idle records keep,
which is why it is not twice the tiles and why network1's moved with nothing
shared. convoy2's cache is full either way; there the
tile that is no longer doubled leaves room for another light's. The lights
that do not share have static `noSelfShadow` casters, as with the cubes.

The picture, with time frozen and both forms captured inside the quarter
second in which the casters are still live:

| View | Shared against a tile each | Shared against shared, just published |
|---|---:|---:|
| medlabs, one light sharing | 0 pixels | 0 pixels |
| convoy2, one light sharing | 0 outside the player's weapon | 0 outside the player's weapon |

In convoy2 the player's own weapon was still coming to rest after the freeze
in two launches of three, in either form: 21,000 pixels in a box at the
bottom of the view, and nothing outside it. hub2, where both spotlights
share, could not be used: objects in view keep moving for several frames
after the freeze, and two captures of the same form differ there.

The validation layers reported nothing over 492 sharing passes in hub2,
medlabs and convoy2, six of them restored from a tile published in the same
view, through two flushes of the cache and a change of form in each of two
of the launches.

Checked for this change, on the final build with the comparison switch
removed: the validation layers again, silent over 151 sharing tile passes in
hub2 and medlabs and 107 sharing cube passes in network1; the fourteen
scenarios on both back ends, all passing; the continuous-motion
qualification, eight cases, all passing; the in-engine self-tests on both
renderers; and the 54 static tests that read the touched files, all passing.
OpenGL is not touched.

### Off-centre lights against stencil

Added on 23 November, from the open row "Casters are not clipped to the light
volume" (REN-03).

**The scan.** `tools/tests/renderer_shadow_stencil_parity_scan.py` reads a
stock map's lights from the installed packs, selects shadow-casting point
lights whose source (origin plus `light_center`) lies outside their own
radius box, isolates each with `r_singleLight` (`listRenderLightDefs` now
prints every render light's origin), freezes game time with the player's
shadow, HUD and weapon off, and captures six views from the light's entity
origin with stencil shadows, shadow maps and no shadows. "Map only" is the
share of pixels the map shadows and stencil lights in the worst view;
"stencil only" the reverse. Windows x64, GeForce RTX 4060 Laptop GPU,
1280 x 720, the Quality shadow configuration; mcc_landing:

| Light | Source outside the box by | Map only, OpenGL | Map only, Vulkan | Stencil only, OpenGL | Stencil only, Vulkan |
|---|---:|---:|---:|---:|---:|
| `light_9081` | 8 units | 0.41% | 0.42% | 10.4% | 10.4% |
| `light_9004` | 48 | 0.00% | 0.00% | 0.11% | 0.12% |
| `light_9058` | 128 | 0.10% | 0.10% | 0.18% | 0.18% |
| `outdoor_Main` | 280 | 6.4% | 6.7% | 6.6% | 6.7% |
| `light_9083` | 832 | 48.9% | 48.9% | 0.22% | 0.22% |
| `light_8995` | 920 | 0.02% | 0.02% | 0.24% | 0.24% |

`outdoor_Main` differs both ways by the same amount: the soft and the hard
edge of one large shadow. The two backends agree throughout, so what follows
is in the shared front end.

**Casters outside the volume: confirmed.** A map draws each caster surface
whole (`sint->ambientTris`), and an interaction exists as soon as a surface's
bounds touch the light. For the static world stencil holds less: dmap cuts a
prelight's casters to the light (`ClipTriByLight`, then the shadow frustums).
With the source inside the volume that cannot matter; with it outside,
geometry between source and box shadows the box in the map only. In
`light_9083`'s interactions, 18 of one trim surface's 2,025 indexes reach the
volume.

A first trial withheld static surfaces with no triangle in the volume from
such a light's casters, and then clipped the rest. It was withdrawn: both
caster passes draw a model surface whole (OpenGL's
`RB_ShadowMapResolveCasterDrawData` and Vulkan's
`VK_ShadowMap_ResolveCasterGeo` resolve an index-subset surface back to its
ambient surface), so the clipped lists were never drawn as such, and an
admitted caster that is not linked counts as a map that failed
(`mapMissingCasterNeedsStencil`, `prelightMapMissingCasterNeedsStencil`): on
Vulkan `light_9081` fell back unshadowed.

Two conclusions drawn from that trial the same morning were wrong, and are
corrected in "What the stock volumes hold" below. "Clipping changes nothing
for `light_9083`" measured nothing, because nothing was clipped. And
"`r_skipEntities 1` changes nothing, so the blockers are world surfaces" was
right by accident: that setting keeps entities out of the view, not out of a
light's casters.

**`nodynamicshadows`.** The same scan with `--select nodynamic` gives the
image the fixed rule lacked: waste's `light_8501` shadows 0.7% of one view
under maps and under stencil alike, on both backends.

### What the stock volumes hold

Added later on 23 November, from the two lights the scan left open.

**The volume.** `_prelight_light_9083`, read from the retail
`mcc_landing.proc`, has 2,166 vertexes in pairs (a point on the casting
geometry, then its projection onto the light's far planes) and 6,864 indexes:
silhouette quads, then 568 rear caps on the projected vertexes, then 568
front caps on the others. Set against the world model of the same file, the
front caps cover the world triangles in the light's box that face away from
the source, and no others: by material, the covered area equals the
away-facing area (the terrain 184,361 square units against 184,359, the
largest wall material 830,022 against 830,022). There is no optimizer
exclusion and no other rule:
the stock volume follows the stencil rule of "Which triangles cast", cut to
the light. The map compiler says the same (`BuildLightShadows` skips a group
whose plane has the light in front, then `ClipTriByLight`).

Run-time volumes are not cut. Every per-surface volume is built by the turbo
path, an infinite projection of all the surface's away-facing triangles
(`idInteraction::CreateInteraction` asks for the clipped builder only for a
model wider than 3,000 units). So stencil cuts the static world of a prelit
light, through the dmap volume, and nothing else.

**`light_9083`: the terrain.** Its blocker is one world surface, the terrain
(`textures/rock/smoothrock01_to_sharprock01_brown`, area 2): withheld from
the map alone, the worst view's 48.9% goes to 0.0%. The light's entity origin
lies 63 units under that terrain, so the scan's views look at the bases of
walls set into the ground. The source is above, and sees the terrain's
front. Stencil does not cast from a triangle that faces the light, so the
buried walls are lit; the map held exactly those triangles. Predicted per
pixel by casting rays through the `.proc` geometry (cells of an 80 x 45
grid over the view, shadowed out of those predicted; three views for the
first row, two of them for the second):

| Between source and receiver | Stencil | Map before | Map with facing reversed |
|---|---:|---:|---:|
| only triangles that face the light | 0/1,843, 6/1,241, 4/87 | 1,832/1,843, 1,227/1,241, 82/87 | 0/1,843, 5/1,241, 0/87 |
| triangles that face away as well | 194/198, 186/189 | 195/198, 185/189 | 193/198, 163/189 |

So the map of a distant point light stores, for open geometry, the faces
turned towards the light: the reverse of what `R_ShadowMapCasterFaces`
intends. The same was measured on Air Defense 1's sun (`light_146`) at
receivers the `.proc` predicts to be lit by the stencil rule and blocked by a
light-facing surface (stencil and the reversed map lit, the map black in 61%
and 75% of two views), on q4ctf2's ten distant lights and on q4cmp13's three
parallel suns. `R_ShadowMapLightMirrorsCasterWinding` calls both synthesized
projections mirrored, by the determinant of their planes, and the face to
discard is swapped on that verdict. Measured, the swap is wrong for them. It
is right for authored projectors (storage2's alarm lights; hub2's
`light_7984`, where reversing it shadows 9.5% of a view that stencil
lights). Why the determinant misjudges the synthesized projections is not
established: the planes do have the opposite handedness from an authored
projector's.

The 11 November round measured the arena under that sun at 3,153 pixels
differing from stencil with two-sided casters, 5,576 with the rule and no
swap, and 3,155 with the swap, and took the swap. The swap stores what a
two-sided map stores wherever the nearest surface faces the light, which is
nearly everywhere; the rule without it is the stencil rule, with the light
leaks of a far shell under a coarse map. That reading was not available then.

**`light_9081`: the ceiling.** A cube light whose source is 8 units above its
box, inside the ceiling. The ceiling face (a 256-unit square of
`textures/common_misc/elevator_floor`) lies in the box's top plane. Its
bounds only touch the light, so it has no interaction and was no map caster.
dmap keeps of it what the shadow frustum of the box's bottom face cuts out: a
window of 8.2 by 7.2 units under the source, which shadows everything inside
that frustum down to the floor, and nothing towards the walls. That is the
10.4%.

**The caster.** For a light whose source lies outside its own box, the
static world now casts into the map as one caster: the front caps of the
prelight volume (`R_ShadowMapPrelightCaster`, built once per light and kept
with it; `R_ShadowMapLinkPrelightCaster` links it into the GLOBAL caster
chain, as the volume is a GLOBAL shadow). The world surfaces of that light
stand aside in `idInteraction::AddActiveInteraction`, and count as held by
the map, so no light is sent to its stencil fallback. The caps are the
triangles stencil casts from, where stencil casts from them, so one mechanism
gives the facing rule, the cut to the volume and the ceiling window. Each cap
is stored in both windings, so the result does not depend on the caster
culling mode or on the handedness verdict above. Both caster passes draw it
as an ordinary surface with its own vertexes; neither backend changed.

`r_shadowMapPrelightCasters` (archived, default 1): 0 draws each world
surface whole as before, 1 takes the caps for a light whose source lies
outside its box (an off-centre point light, a parallel light), 2 for every
light with such a volume.

It found one defect on the way: the view's world space had an identity model
matrix without its last row (`R_SetViewMatrix`), which nothing had read as a
whole matrix before. The caster passes do, and the first version of the
caster came out with w = 0 and wrote no depth. The row is set now.

Worst of six views from each light's entity origin, the share of pixels the
map shadows and stencil lights / the reverse, before (the setting at 0) and
after, one build:

| Light | OpenGL before | OpenGL after | Vulkan before | Vulkan after |
|---|---:|---:|---:|---:|
| mcc_landing `light_9083` (distant) | 48.93% / 0.22% | 0.01% / 1.01% | 48.94% / 0.22% | 0.01% / 1.01% |
| mcc_landing `light_9081` (cube, 8 units) | 0.41% / 10.4% | 0.42% / 0.22% | 0.42% / 10.4% | 0.42% / 0.22% |
| mcc_landing `light_9058` (cube, 128) | 0.10% / 0.18% | 0.08% / 0.17% | 0.10% / 0.18% | 0.09% / 0.17% |
| q4cmp13 `light_4895` (parallel sun) | 0.14% / 30.87% | 0.21% / 0.65% | 0.14% / 30.89% | 0.21% / 0.65% |
| q4cmp13 `light_4894` (parallel sun) | 0.09% / 27.75% | 0.10% / 0.82% | 0.09% / 27.75% | 0.10% / 0.82% |
| q4cmp13 `light_2` (parallel sun) | 0.28% / 11.92% | 0.38% / 0.69% | 0.28% / 11.93% | 0.39% / 0.68% |
| q4ctf2 `light_5` (distant) | not run | not run | 0.47% / 14.28% | 0.49% / 1.39% |
| Air Defense 1 `light_146` (the sun) | not run | not run | 0.03% / 0.03% | 0.01% / 0.04% |
| Air Defense 1 `light_7910_cursed` (distant, 48) | not run | not run | 0.06% / 0.15% | 0.05% / 1.90% |

Over 20 distant lights of q4ctf2 and hub2 (Vulkan, worst views summed): 4.5
points map-only and 73.7 stencil-only before, 4.6 and 23.0 after. hub2's
`light_6456` holds 13.1 of what is left: its volume folds its caps. One light
measured is worse for it: Air Defense 1's `light_7910_cursed`, by the leak
described first below. The four other off-centre lights of that level that
were scanned fold their caps or changed by under 0.1 points.

Checked for this change, on the final build: the scan above on both
backends, with no stencil-fallback mark and no missing-caster report in any
log; the continuous-motion qualification's receiver case on both; the five
moving-door scenarios and the six caster-cycle scenarios on both, all
passing; storage2's alarm lights, which are projectors and do not take the
caster, by render light index from the start view with the setting at 1 and
at 0 (map-only 0.16% at most, stencil-only 0.18%, identical between the
settings on Vulkan); the in-engine self-tests on both renderers, including
the new one for the caster; and the static tests that read the touched
files.

What remains after it, in the order it shows:

- **`outdoor_Main` moves away from stencil with the caster.** mcc_landing's
  main outdoor light, a cube light whose source lies 280 units outside its
  box, was measured again when REN-02 and REN-03 were merged (23 November,
  same scan and machine, under load). With the caster its worst view differs
  from stencil by 9.57% map-only and 9.86% stencil-only on OpenGL (8.79% and
  8.87% on Vulkan, both in view 0 or 1); with `r_shadowMapPrelightCasters 0`
  its worst view is 0.60% and 1.00% (OpenGL, view 4). In the worst view the
  map leaves lit a stretch of terrain behind the rocks that stencil shadows,
  and darkens ground that stencil leaves lit. The caster was kept on by
  default because the lights above improve by far more; the cause for this
  light is not established. (Settled on 24 November: the light has no
  prelight volume, so the caster never reached it, and the reading does not
  repeat; see "The rest of the off-centre lights".)
- **A far shell leaks behind itself.** The caps are the faces turned away
  from the light, and a receiver that stands closer behind one than the
  map's bias is lit: `light_9083`'s stencil-only share rises from 0.22% to
  1.01% in the view of an arch whose inner wall meets its roof. Stencil has
  no such band. The bias follows the texel, and a wide distant map is coarse
  (22 units a texel for `light_9083`'s whole-light map). Measured on Air
  Defense 1's `light_170` with every caster on its away-facing triangles
  (cells of one view that the stencil rule shadows, by how far in front of
  the receiver the first such triangle stands):

  | Distance | Cells | Stencil shadows | Away-facing map | Map before (near shell) |
  |---|---:|---:|---:|---:|
  | under 16 units | 50 | 36 | 0 | 29 |
  | 16 to 32 | 82 | 79 | 14 | 72 |
  | 32 to 64 | 191 | 191 | 138 | 174 |
  | 64 to 128 | 293 | 284 | 235 | 235 |
  | over 128 | 201 | 201 | 181 | 184 |

  Beyond 64 units the two maps agree; closer in, the near shell of the same
  solid, further towards the light, is what kept the shadow. That is the
  price of casting from what stencil casts from, and it is paid wherever the
  caster is used.
- **78 stock lights fold their caps.** dmap writes a volume with a perforated
  caster in it with no cap ranges (the caps cannot be left out when a hole
  shows through them). Those lights keep their per-surface casters, uncut and
  on the reversed faces if the light is distant or parallel. The caps are
  still in the index list: a front cap is a triangle on three even vertexes
  (verified on `light_9083`, where that selects exactly the 568). Taking them
  would cast the cutout solid, as stencil does, where the map now casts its
  holes; that choice has not been made.
- **The other casters of distant and parallel lights** (entities; the world
  under the 78 lights above and the 31 without a prelight) keep the reversed
  faces: open geometry its light-facing triangles, sealed hulls their far
  shell. Reversing the verdict alone, before the caster existed, summed over
  57 distant lights of six maps: map-only 36 to 47 points, stencil-only 98 to
  34. With the caster in place it changes nothing in q4ctf2 and trades
  hub2's `light_6456` from 13.1% stencil-only to 11.4% map-only (its uncut
  casters outside the volume). On Air Defense 1's six distant lights it made
  the worst views worse (stencil-only 3.1 to 9.7 points summed), by the leak
  above. It also takes the contact shadow of a character standing in
  sunlight from the near shell to the far one. Left as it is until the
  folded volumes are decided.
- **A mapped light's stencil fallback is not the stock shadow** for a
  prelit light: its world surfaces carry per-surface turbo volumes, uncut,
  and the fallback draws those. Found in passing; the fallback is rare and
  brief.
- dispersal's parallel `light_4722` leaves lit 24.5% of a view that stencil
  shadows, with the faces either way. Not examined.

The scan tool changed with it. An interaction keeps the stencil volumes it
was built with, so each picture is now taken after `regenerateWorld`, from
interactions built under that picture's settings (the numbers did not move).
`--select` takes `distant`, `projected` and `parallel`, `--position` and
`--view` place the camera, and `--spawn` puts stock entities in the level and
reports the pixels they changed.

### A `noSelfShadow` receiver behind `noSelfShadow` world geometry

The image the fixed rule lacked. Stock maps inline many models into the
world, and most of their materials are `noSelfShadow` (18,482 such world
triangles in mcc_1). Under mcc_1's prelit `corey_light_3557` a stock marine
crate (`models/mapobjects/marine/equipment/crates/crate1.lwo`, a
`noSelfShadow` material) was spawned at `437 -1575 98`, where the `.proc`
puts only `noSelfShadow` world triangles between it and the source, and
viewed from `538 -1656 152`:

```
renderer_shadow_stencil_parity_scan.py --map game/mcc_1 --names corey_light_3557
  --view 538,-1656,152,14,141
  --spawn 'func_static name probe model models/mapobjects/marine/equipment/crates/crate1.lwo origin "437 -1575 98"'
```

| Mean brightness of the crate's pixels (33,800) | OpenGL | Vulkan |
|---|---:|---:|
| No shadows | 5.14 | 5.24 |
| Stencil | 3.36 | 3.44 |
| Shadow maps | 3.56 | 3.64 |

0.2% of those pixels are shadowed by the map and lit by stencil, 0.8% the
reverse, on both. With the world's model-material casters withheld from the
map by a temporary switch, the crate went from 2.11 to 3.25 on a narrower
mask where the unshadowed picture reads 3.76: it is that geometry that
shadows it.

### The rest of the off-centre lights

Added on 24 November, from the four items "What the stock volumes hold" left
open. Same scan, machine and settings (Windows x64, GeForce RTX 4060 Laptop
GPU, 1280 x 720, the Quality shadow configuration), worst of six views from
each light's entity origin, map only / stencil only. "Before" is the build of
the morning (45c7c4fc7), kept as a separate runtime, "after" this round's;
both in the same session. The before column repeats the 23 November figures.

| Light | OpenGL before | OpenGL after | Vulkan before | Vulkan after |
|---|---:|---:|---:|---:|
| mcc_landing `light_9083` (distant) | 0.01% / 1.01% | 0.07% / 0.18% | 0.01% / 1.01% | 0.07% / 0.18% |
| mcc_landing `outdoor_Main` (wide source) | 0.15% / 2.00% | 0.41% / 0.88% | 0.15% / 1.99% | 0.42% / 0.88% |
| mcc_landing `light_9081` (cube, 8 units) | 0.54% / 0.12% | 0.42% / 0.26% | 0.42% / 0.23% | 0.49% / 0.17% |
| Air Defense 1 `light_146` (the sun) | 0.01% / 0.04% | 0.01% / 0.01% | 0.01% / 0.04% | 0.00% / 0.01% |
| Air Defense 1 `light_170` (folded) | 0.29% / 2.22% | 0.36% / 1.04% | 0.23% / 2.22% | 0.38% / 1.04% |
| Air Defense 1 `light_7910_cursed` | 0.05% / 1.89% | 0.02% / 0.03% | 0.05% / 1.90% | 0.02% / 0.03% |
| hub2 `light_6456` (folded) | 0.73% / 13.07% | 0.11% / 0.43% | 0.74% / 13.07% | 0.12% / 0.43% |
| network1 `light_6365` (folded) | 6.77% / 2.15% | 7.60% / 0.73% | 6.85% / 2.14% | 7.84% / 0.73% |
| convoy1 `light_7744` (folded) | 0.15% / 4.32% | 0.38% / 1.43% | 0.15% / 4.34% | 0.42% / 1.51% |
| q4cmp13 `light_4895` (parallel sun) | 0.21% / 0.65% | 0.20% / 0.20% | 0.21% / 0.65% | 0.20% / 0.19% |
| q4cmp13 `light_4894` (parallel sun) | 0.10% / 0.82% | 0.18% / 0.13% | 0.10% / 0.82% | 0.18% / 0.13% |
| q4cmp13 `light_2` (parallel sun) | 0.38% / 0.69% | 0.36% / 0.12% | 0.39% / 0.68% | 0.37% / 0.12% |
| Sum | 9.39 / 28.98 | 10.12 / 5.44 | 9.31 / 29.09 | 10.51 / 5.42 |

Every light's two shares together fall; map only rises by a few tenths on
some, the price of the bias below. In the order of what they moved:

**1. The far shell's leak was the receiver bias.** A map that holds only the
faces turned away from the light holds no receiver that faces it, so the bias
that keeps such a receiver off its own depth has nothing to do there, and
every unit of it lights a receiver that stands that close behind a caster.
On the frozen build, with the bias terms set by hand (Vulkan): the texel bias
alone took `light_9083`'s stencil-only share from 1.01% to 0.62% and
`light_7910_cursed`'s from 1.90% to 1.17%; the normal offset alone moved them
by a tenth. With every bias term and the caster offset at zero and every
caster on its far faces, 13 lights went from 2.7 / 25.9 points summed to
3.1 / 5.5 (network1 aside, see below); keeping a quarter of the bias gave
2.6 / 9.3. Distant
point and parallel lights now store every caster by the stencil rule and keep
`r_shadowMapDistantFarShellBias` (default 0) of the receiver bias: the
constant and normal bias through the cascade bias scale, the texel bias and
the filter taps' bias through the texel steps, and the normal offset, all
applied once in `R_BuildShadowMapProjectedLightState`, so both backends and
the sealed interaction packets take it without a change of their own. The
caster offset is left as it was. `r_shadowMapDistantFarShell 0` restores the
earlier faces, verdict and bias.

**2. The 78 folded volumes keep their caps.** A front cap is a triangle on
three even vertexes, wherever dmap put it in the index list
(`R_ShadowMapBuildPrelightCaster`). Alone, before the bias change, over 11 of
those lights (Vulkan): 50.5 points summed to 41.0; hub2's `light_6456` 13.8 to
6.1, Air Defense 1's `light_7160` 4.4 to 0.6, convoy1's `light_9015` 3.2 to
0.0. The stencil-only share rose on others (Air Defense 1's `light_170` 2.2%
to 5.6%, network1 2.1% to 7.1%): the same far-shell leak, which the bias
change then removed. Cutouts are the choice left open: a perforated world
surface that lies inside the light's box keeps a caster of its own and
casts through its holes, and its caps are left out of the caster (a cap is a
piece of a casting triangle, so its centre lies in that triangle);
one that reaches outside the box casts solid from the caps, as stencil does,
since drawn whole it would shadow from where the volume holds nothing.
`r_shadowMapPrelightCutouts 0` casts every cutout solid. The 31 lights with
no prelight volume keep their surface casters.

**3. The face verdict of the synthesized projections was wrong, as measured.**
With the world drawn surface by surface (`r_shadowMapPrelightCasters 0`),
`light_9083` shadows 48.9% of a view that stencil lights with the verdict as
it was, with every caster two-sided, and with the "far faces for every caster"
mode alike: what the caster passes stored was the faces turned towards the
light. Turned for the parallel and distant point projections, the same
setting gives 0.01% / 1.01%, exactly what the prelight caps give. The
determinant does call both mirrored (their axes come from `NormalVectors`, a
left-handed pair), and nothing in the cascade crop or the matrix upload was
found to turn the picture back; why the raster disagrees is still not
established. Authored projectors keep the determinant (storage2's alarm
lights: 0.16% / 0.18% at most, as before). Under the default caster the
static world does not depend on the verdict, so on its own it moved the scan
little (summed over 14 lights, 2.9 / 31.0 to 9.2 / 28.3, the rise in map
only being network1's); it matters for
entities, the 31 lights without a volume (`outdoor_Main` among them) and,
above all, for the bias: a near shell at zero bias would shade itself.
Sealed hulls under these lights take the far shell too, the stencil rule.

**4. `outdoor_Main` was never the caster.** The retail `mcc_landing.proc`
holds no prelight volume for it, so `r_shadowMapPrelightCasters` cannot reach
it: on this build its worst view is 0.15% / 1.99% with the setting at 1 and
0.12-0.15% / 1.99-2.00% at 0, and with `r_shadowMapWideSources 0` (the cube
it was on 23 November) 0.61% / 1.00% both ways, the figure recorded then for
the setting at 0. Three repeats and two earlier autoexec delays (0 and 200
ms, the capture then falls at another moment of the level's opening) gave
0.61% / 1.00% each time. The 9.6% / 9.9% of 23 November came from one run on
the REN-02/REN-03 merge under load, and the 6.4% / 6.6% of that morning from
a build that had no caster at all: an equal share both ways is one large
shadow edge drawn differently, and it came and went with the run, not the
setting. Since 24 November it is cascaded (`r_shadowMapWideSources`), and
with the far shell its stencil-only share falls from 2.0% to 0.9%.

**Bounded: network1's tube lights.** network1's ten folded lights (sources 32
units outside a 256-unit box, a projection that spans 162 degrees) darken
about 7.8% of the view up the tube that stencil leaves lit: a grate ring
round the tube, from that grate's own prelight caps. It was 6.8% before the
round (the whole grate drawn), 0.07% (with 7.1% stencil only) while the grate
kept its own caster under the earlier verdict, and 0.14% / 0.44% with its caps and
caster withheld altogether; casting the grate solid or through its holes
gives the same 7.8%. Stencil lights those caps' own receiver, the map does
not, and why is not established. Their sum still falls (8.99 points to 8.57).

Checked on the final build: the scan above on both backends; the five
moving-door and six caster-cycle scenarios, Air Defense 1's outdoor scenario
and the cascade-motion scenario on both, all passing; the continuous-motion
receiver case on both (OpenGL on a second run: the first found two identical
frozen captures 3.5 MB apart at one pose, an unfrozen scene); storage2's alarm lights; the in-engine self-tests on
both renderers (folded caps, cutouts, the far-shell faces and the bias
scale).

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
2. The PBR laboratory's modern OpenGL shadow controls leave point lights on
   stencil, so the modern path never owns their frames (see "Modern OpenGL
   receivers and PCSS-lite"). The earlier item here, PBR receivers one or two
   levels brighter under a map on OpenGL, was the classic room around the
   stations; the PBR pixels are exact (see "The PBR branch").
3. A static model that moves under still lights (a lift, a door) builds its
   volumes again each frame it moves, 40 to 45 microseconds a view in hub2.
   Leaving them out needs a mark on the entity for having moved.
4. Stencil parity for lights whose source lies outside their box: the folded
   volumes, the faces and the far shell's leak were done on 24 November (see
   "The rest of the off-centre lights"). What remains: network1's tube lights,
   where a grate's own caps darken what stencil leaves lit; why the
   synthesized projections raster unmirrored against their determinant; and
   the 31 lights with no prelight volume, which keep their surface casters.

Set aside after measurement: cube faces the view cannot see (see "Static
cutout casters"); the stencil volumes of static models under lights that have
never moved (see "Static volumes under mapped lights"); a projected cache for
OpenGL beyond its atlas (see "OpenGL's projected cache and its atlas"); a
tile-size fit on Vulkan that leaves out maps read in place (see "The
tile-size fit and maps read in place").

Outside the shadow system, found in passing: `idMaterial::ConstantRegisters()`
is NULL for every material, so every surface evaluates its registers every
view (since restored: [Constant material registers](material-constant-registers.md)).
