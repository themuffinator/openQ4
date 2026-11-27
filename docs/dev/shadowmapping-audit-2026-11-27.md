# Shadow-mapping audit, 27 November 2026

A review of the shadow-map code on OpenGL and Vulkan for correctness, cost,
practice and picture quality: the shared planning that both renderers read
(cascade fits, atlas tiles, cache signatures), each renderer's map rendering,
and the receivers that sample the maps. It follows the
[9 November audit](shadowmapping-audit-2026-11-09.md) and feeds REN-03 and
FPS-03 in the [roadmap](roadmap.md).

Five independent read-throughs produced about thirty candidates. Each was
checked against the code before anything changed. Fourteen were fixed here, a
fifteenth (the cascade collapse behind a light's source plane), five more
leads, bilinear PCSS-lite taps on Vulkan, the point light left without a cube
or volumes, the top level's point cache and turn-steady cascades (as an
option) the same day, one
was measured and left, and the rest are recorded below as leads with the reason
each was not taken.

## Fixed

### Picture quality

| Finding | What changed |
|---|---|
| **The nearest cascades of a cascaded light drew coarser than they had to.** A cascade's crop keeps a guard around the view slice, so that the filter's taps and the normal offset never leave the map. The receivers count that guard in texels of the cascade they sample; the fit counted it in texels of the light's whole map. A cascade that covers a hundredth of the light then reserved a hundred times the guard it needed: around Air Defense 1's sun the guard alone was as wide as the nearest slice. | **Fixed, both backends** (`R_ShadowMapCascadeGuardKeep`, `R_ShadowMapProjectedCascadeGuardTexels`). The guard is a share of the crop: the filter's radius plus 0.75 texels, plus two texels of normal offset (more for an authored projector near its lamp), each side. At Quality on Air Defense 1 the nearest cascade goes from 0.91 to 0.58 units a texel (1.14 to 0.73 at the wider poses), the second is one ladder step finer at seven of nine poses (2.23 to 1.78, or 1.78 to 1.43); the third and the whole-light map report the same texel. |
| **Pixels at the player's feet took the wrong cascade.** Both receivers clamped the eye depth to zero at each vertex. Eye depth is linear across a triangle, and a clamped vertex bends it: a floor triangle reaching from behind the player to 400 units ahead read 228 where the pixel stands at 100, selected the next cascade there, and showed a resolution seam along triangle edges. | **Fixed, both backends.** The depth is interpolated unclamped (`shadow_interaction.vs`, `interaction_shadow.vert`). |
| **A spotlight beside a cascaded sun kept half its resolution for nothing.** When a view's lights do not fit the atlas each is halved in turn, smallest first. A cascaded sun's block is four tiles on a side, so its one halving frees three quarters of a Quality atlas, after the spotlight before it had already given up its own tile. | **Fixed, both backends** (`R_ShadowMapFitAtlasTiles`). Room a round frees beyond its need is handed back, largest light first. |
| **Cel-shaded highlights changed with the shadow method on Vulkan.** The two mapped receivers banded the stock specular term before its doubling; the unmapped Vulkan receiver and all three OpenGL ones band it after. With `r_celShading 1` a highlight peaked twice as bright under a shadow-mapped light and its bands moved. | **Fixed (Vulkan).** |
| **A parallel light's texel was sized from its box's largest radius**, plus the length of its `light_center`, which for a parallel light is a direction of whatever length the mapper typed. The map spans the box's footprint across the rays: wider than the largest radius for a sun slanting across its box, narrower for one shining down the box's long axis. | **Fixed, both backends.** The size is read from the projection's own rows. Parallel lights store far faces and scale their texel bias to zero by default, so this shows only with `r_shadowMapDistantFarShell 0` or a non-zero `r_shadowMapDistantFarShellBias`. |
| **The opt-in modern OpenGL receivers ignored the far-shell bias scale** for the normal offset and the off-centre taps' bias: a far-shell sun kept a whole-light texel of each, the leak behind the shell that the scale was added to remove. | **Fixed (modern OpenGL).** The descriptor carries the scale. |

### Correctness

| Finding | What changed |
|---|---|
| **OpenGL drew the stencil supplement of a hybrid light and then ignored it.** A light whose map cannot hold every caster stencils the volumes of the missing ones and tests its mapped receivers against them. The receiver setup reset the stencil test to always-pass before every draw, so those casters shadowed nothing and their volumes were rasterised for no result. | **Fixed (OpenGL).** The receivers keep the test the supplement was stencilled for, PBR receivers included, and leave it open afterwards as before. Verified by reading the three call sites, and later on screen with a forced case: see [the stencil supplement on screen](#the-stencil-supplement-on-screen). |
| **A resting body kept the wrong cached map after `r_shadowMapCharacterSelfShadow` was switched.** The static-caster signature did not record which of a light's two maps holds a caster. The setting moves a still `noSelfShadow` caster between them with nothing else changed, and the LOCAL map cached before the switch was reused. | **Fixed, both backends.** The signature carries the ownership. Every signature changes once, so each cached map renders once more after the update. |
| **Vulkan kept cached maps across `r_shadowMapDebugMode 10`.** Its signature hashed the caster offsets as set, while the mode draws with none; OpenGL hashes the offsets it draws with. | **Fixed (Vulkan).** |
| **The point receivers passed NaN through their range test.** `depth <= 0.0 || depth >= 1.0` is false for NaN, so a degenerate vertex went on to the cube lookups. | **Fixed, both backends.** The test is written to reject it, as the projected receivers already do. |

### Cost

| Finding | What changed |
|---|---|
| **A map that left the view was thrown away sooner the faster the game ran.** `r_shadowMapResidentFrames` (120) counted frames as drawn: two seconds at 60 Hz, 0.83 seconds under Enhanced's 144 Hz cap, a quarter of a second at 480 Hz. Nothing is freed when a map expires, so a glance away from a room only re-rendered its maps on the glance back. | **Fixed, both backends** (`R_ShadowMapResidentFrames`). The setting is counted at 60 frames a second and scaled to the display rate: two seconds at the default whatever the frame rate. Below 60 it keeps the count it had. |
| **OpenGL kept point cubes it could no longer use.** The slot limit follows the cube size and two live cvars. Raising `r_shadowMapPointSize` from 512 to 1024 takes it from 64 slots to 21, and the 43 old cubes stayed allocated beside the new ones: about 760 MiB under a 512 MiB bound, until the next video restart. | **Fixed (OpenGL).** A slot past the limit gives its cube back at the next view. |
| **A shadow program that failed to load was loaded again for every light of every view.** Each loader's cache test needs a program object to pass and a failed load leaves none: two file reads, a compile and a warning per light per view. Rendering fell back to stencil correctly; the frame rate and the log did not survive it. | **Fixed (OpenGL).** A failed load is remembered for its variant until the video restarts. |
| **The projected filter read its centre texel twice.** The PCSS search's first probe is the texel the filter's centre tap compares, against the same biased depth. | **Fixed, both backends.** The filter takes the search's verdict: one fetch in ten fewer on a fully lit pixel, one in nineteen elsewhere (two under Vulkan's ownership union). Bit-identical. |

## A slice behind the source plane

Done as a follow-up the same day.

**A cascaded light fell to one map as soon as a view slice reached behind its
source plane.** The fit projects sample points of each slice of the view into
the light's map. A point behind the plane the light shines from has a negative
divide (`w`), and one such sample collapsed every cascade of the light into a
single map of the whole light (`mixed-w-signs`). mcc_landing's `outdoor_Main`
stands 280 units above its box: look up from the ground and the far slices rise
past the source, and the three fitted cascades at 0.96, 2.9 and 53 units a
texel became one map at 66. The same happened beside any cascaded spotlight at
Maximum.

A point behind the plane is outside the light and needs no place in its map, so
the slice is now cut where the light begins (`R_ShadowMapLightNearW`: the
smallest `w` of the light's own box or frustum). What is left of it is convex
and in front of the plane, so its corners there, and the points where its
twelve edges cross the plane, bound it exactly
(`R_ShadowMapBuildCascadeBounds`). The fit still records `mixedWSigns`; no
state reports the `mixed-w-signs` fallback any more. Samples between the source
plane and the start of the light's volume, which used to stretch a crop
towards the whole map, are cut the same way.

A slice wholly behind the plane still falls back to one map
(`insufficient-valid-samples`): it holds nothing the light reaches. In play
that needs the camera outside the light, or more than a cascade's depth behind
a spotlight's lamp.

- **Before and after in one launch**, mcc_landing at Quality, twelve views from
  the middle of the light looking up 35, 50 and 70 degrees at four headings,
  1920x1080, scored against the stencil picture of the same frozen moment, with
  a temporary switch restoring the collapse (removed before the commit). Every
  view took the collapse before. OpenGL: 68.0% of stencil's shadow kept before,
  87.1% after (82.7% to 95.3% allowing two pixels of offset), and 10,114
  pixels darkened where stencil leaves them lit against 4,956. Vulkan: 68.7%
  to 87.1%, and 10,565 to 5,198.
- **What it does not improve.** The far slice of such a view reaches the top
  of the light's box, where the light's fan is narrowest, so its crop has to
  span nearly the whole map: 53 units a texel against the single map's 66. A
  shadow one or two texels wide there is luck in either picture; in one view a
  wedge that stencil draws is smeared by the old map and absent in the new.
  The debug views show that region in the third cascade with no invalid
  lookup: resolution, not coverage.
- **Self-test.** `R_ShadowMapCascadeSourcePlaneSelfTest` builds that light and
  45 views of it, from level to 85 degrees up. Every view keeps four cascades;
  16,134 points spread through the slices and inside the light's box all land
  inside their slice's crop; 24 slices are cut at the plane, 18 of them
  reaching behind the source. With the edge crossings switched off in a
  scratch build the test fails, naming a point of the third cascade at
  (0.92, -1.54) of its crop. The planner's diagnostic fixture, whose every
  slice straddles the plane, now expects three cascades where it expected the
  fallback.
- **Found on the way:** the opt-in modern planner refused every far-shell
  light. Its invariant required a positive cascade bias scale, and a map of
  far faces scales its receiver bias to zero by design (24 November). It logged
  `projected bias scale is invalid` and put the light on its stencil fallback.
  Zero is accepted where the light's receiver bias scale is zero.

## Five more leads

Taken the same day from the list below.

**An animating caster whose shadow cannot reach the view is left out of the
map.** A caster that is changing is in no cached map: it is drawn into its
lights' maps afresh every view, and skinned first if nothing else needed its
model. Where the volume its shadow can fall in misses the view, that is work
for nothing. The first audit set this aside because a skipped caster's model
is not rebuilt, so it stops reading as changing; the worry was that it would
then join the static map, be rebuilt, leave again, and re-render the map each
time. It does not: such a caster is let through after the quarter-second live
window, and the rebuild that follows marks it changing before its surfaces are
sorted into chains, so it is drawn as a live caster for that one view and
skipped again from the next. An off-screen animation costs one skin and one
draw a quarter second instead of one a frame and never enters a static map; a
caster that has really come to rest joins the static map as before. A static
caster is never skipped, and suns and other distant sources keep every caster,
because their filter reaches tens of units past a shadow's edge
(`R_ShadowMapOffscreenLiveCasterSkips`, `r_shadowMapCullOffscreenLiveCasters`,
default 1).

- Hub 2 at Quality on Vulkan, eight marines spawned behind a fixed view, the
  setting switched within one launch, two windows each way: 49 caster draws a
  view without it, 5.5 with; the shadow pass's CPU time 0.20 ms to 0.05; the
  CPU frame 3.42 and 3.61 ms to 2.96 and 3.00; front-end time 1.47 and 1.61 ms
  to 1.21 and 1.20. GPU time does not move. All thirty point maps are exact
  cache hits in every window either way: nothing re-renders.
- On OpenGL with four marines the skip engages the same way (ten light and
  caster pairs a view) and the time difference is inside the noise: its back
  end already drew little for them.
- Not checked by picture: a soft edge within a filter radius of the screen
  edge can appear a frame late for a caster that was just outside. The
  frustum is built from the entity's reference bounds, which are generous for
  animated models.

**Vulkan gives back shadow memory nothing has used for half a minute.** Only
a size change or a video restart released it before. A hidden run at Quality
in Hub 2 under the validation layers: 260 MiB resident; 128 thirty seconds
after `r_shadowMapPointLights 0` (the point cubes); 0 thirty seconds after
`r_useShadowMap 0` (the atlas and resident tiles); 162 a few seconds after both
are switched back on. No validation message
(`VK_ShadowMap_ReleaseIdleResources`).

**One front face for a caster pass, on both renderers.** The shared verdict on
which face a caster discards already answers for a reflected model. OpenGL's
two caster chains also turned the front face for such a model, as the lit
passes do, which undid the answer: a mirrored open caster stored the triangles
that face the light. They now keep one front face, as Vulkan and the shared
stream do. The forced near shell (`r_shadowMapCasterCulling 1`) took no account
of a reflected model at all, which only OpenGL's per-surface front face hid; it
swaps like every other mode. Three self-test cases pin it. No stock map has a
reflected entity, so this was not seen on screen.

**OpenGL's translucent moment passes prove their framebuffer** before changing
state, as the opaque passes do; and **Vulkan's shadow timing spans are read
only after their frame slot has retired**, so a span cannot be credited with
the previous result of the queries it reuses (diagnostics only).

## Bilinear PCSS-lite taps on Vulkan

Asked for after the leads above, knowing it ends picture parity between the
renderers for soft projected shadows.

PCSS-lite reads raw depths for its blocker search, so it cannot use the
comparison sampler, and it compared every filter tap against the one nearest
texel: nine taps, ten grey levels, with the kernel turned per texel, so a soft
edge was stipple. OpenGL 2.1 can do no better, since a texture there has one
compare mode. On Vulkan each tap now weighs the four texels around it as a
bilinear lookup would (`BilinearShadowCompare`, one `textureGather`). The
comparison sampler itself is not used, because under the ownership union a
texel's depth is the nearer of two tiles and that minimum has to be taken
before the compare; the shader takes it across both gathers, so the union
stays exact. `r_vkShadowBilinearPCSS 0` restores the nearest compare, which is
the picture OpenGL draws.

- **Grain.** Air Defense 1 at Quality, six frozen poses at 2560x1440, both
  modes at the same moment. In the penumbra (pixels that keep between 10% and
  90% of stencil's occlusion) the mean difference between a pixel's shadow
  term and the mean of its four neighbours falls from 0.048 to 0.022.
  Coverage does not change: 99.2% of stencil's shadow kept either way.
- **Cost.** One more fetch on a lit pixel (the centre tap, which the nearest
  mode takes from the search) and four compares a tap in place of one. Three
  other projects' clients held the test machine's GPU at 66% during the
  measurement, so the figure is rough: in the three quiet pairs of an
  alternating run in the Air Defense 1 arena at 1440p the lit pass took
  0.04 to 0.2 ms longer (about 6%). It wants confirming on a quiet machine.
- **Validation.** Both modes under the validation layers at Quality on Air
  Defense 1 (the union path) and at Maximum in Hub 2: no message.
- The Maximum run logs the "required shadow resource unavailable" warning of
  the 9 November audit's open list for one light. The 0.15.0 release build
  logs it in the same view, with nineteen failed passes against one now. The
  next section ends it.

## A point light with no cube and no volumes

**On Vulkan a point light could be drawn for a frame with no shadow at all.**
Vulkan renders a view's maps before it draws the view, so every point pass
that is not read straight from the resident cache needs a scratch cube, and
the scratch pool is bounded by `r_shadowMapPointCacheMegs` like the cache. At
the top shadow level, whose 1024 cubes are 24 MiB each, that is 21 cached and
21 scratch cubes; Nexus Hub 2's processing hall asks for 41 passes and a few
composed copies. A pass that finds the pool empty falls back to its light's
stencil volumes, by design. But the front end had decided, before the pass
was tried, that a mapped point light needs no volumes, so there were none:
the light's receivers were drawn unshadowed for that frame, the light was
held on the stencil fallback, left it after a few clean views, and flashed
again the next time. The 9 November audit recorded it as open ("required
shadow resource unavailable").

Projected lights never had this, because the front end counts the view's atlas
lights before it sheds their volumes (`R_ShadowMapAtlasHoldsView`). What a
point pass needs depends on what the cache holds, which cannot be counted
there, so the back end reports it instead: a view that leaves less than a
quarter of the scratch pool free (and never fewer than four cubes) marks the
pool as under pressure, and for two seconds after the last such view the
front end keeps point lights' stencil volumes
(`RB_ShadowMapPointPoolHasRoom`, `VK_ShadowMap_PointPoolHasRoom`). A pass that
overflows then falls back to stencil shadows and nothing flashes. OpenGL draws
a light's receivers as soon as its cube is rendered, needs one scratch cube
for a whole view, and always answers that there is room.

- Hub 2 at the Maximum preset on Vulkan, the 360 degree sweep, hidden 1440p:
  the warning appeared once in each of four runs before and in none of three
  after, the last under the validation layers with no message. The pool still
  overflows there (one allocation failure a run) and the light draws its
  stencil shadows.
- The cost is the volumes themselves, built as every preset built them before
  10 November, and only while the pool is that full: at Quality the same view
  uses one scratch cube of 85. It was not timed.
- Not covered: a jump from an empty pool to a full one inside one frame (a
  teleport into a crowded hall) can still overflow before the pressure is
  known. Level starts are covered, since volumes are kept until the back end
  has proven its resources.

## The top level's point cache

The pool ran out at the top shadow level because its budget was sized for
another cube. `r_shadowMapPointCacheMegs` (512) holds all 64 cache slots of 512
cubes at 6 MiB each. The top level draws 1024 cubes, four times the texels:
the same figure held 21, about half of the passes of Nexus Hub 2's hall, and
the other half were rendered again every frame into a scratch pool with the
same 21.

The owner chose 1536 MiB for the top level. It is a rule in the renderer and
not a preset value: cubes above 512 texels a side get three times the
configured figure, for the cache and for Vulkan's scratch pool
(`R_ShadowMapPointCacheBudgetMegsFor`). The top level is the only one with
such cubes, a player who sets 1024 cubes by hand gets the same, an archived
512 needs no migration, and the presets' closed footprint, with its settings
catalogue version and System Check, does not grow.

Hub 2 at the Maximum preset on Vulkan, the 360 degree sweep, default settings,
two windows each:

| | Before | After |
|---|---:|---:|
| Cache slots available | 21 | 64 |
| Slots in use | 20 to 21 | 38 to 41 |
| Point cubes rendered a view | 5.2 and 5.3 | 0.2 and 0.3 |
| Cube faces rendered a view | 31 and 32 | 0.9 and 1.6 |
| Caster draws a view | 698 and 688 | 243 and 258 |
| Scratch cubes in use | 11 to 17 | 1 |
| Pool overflows a run | 1 | 0 |
| Resident shadow memory | 1.7 to 2.8 GiB | 1.9 to 2.4 GiB |

Resident memory did not rise as a larger budget suggests: the scratch pool the
uncached lights kept busy is nearly empty now. GPU time was not compared,
because other projects' clients were loading the GPU. On OpenGL the same view
holds 54 of 64 cube slots at Maximum. Quality is unchanged on both: 64 slots,
350 to 450 MiB resident on Vulkan.

## Cascades that keep their scale while the view turns

Built and measured, then put to the owner, because it trades resolution for
steadiness and only eyes can price that. **The owner chose the nearest two
cascades (27 November 2026)**, which is the default.

The tight fit bounds each view slice's corners in light space. That box
changes shape as the view turns, its two sides are quantised to the 1.25
ladder separately, and each time one crosses a step the cascade's texel size
changes and every shadow edge in it is resampled. A sphere around the slice
looks the same from the light whichever way the view faces, so a crop sized by
it keeps its scale through a turn (`R_ShadowMapSliceStableBox`).
`r_shadowMapCascadeStableExtent` is how many of a light's nearest fitted
cascades are sized that way: 0 none (as before), 2 the default, 4 all.

Two things had to be got right for a perspective source such as Air Defense
1's sun, whose picture is a fan 46 degrees to a side:

- The sphere's size in the picture depends on where its centre is, and the
  centre swings about the eye as the view turns. Sized at the centre itself
  the far cascade still crossed a ladder step four times in a turn. The size
  is taken over everywhere a turn can put the centre, so it follows only where
  the eye stands.
- With no breathing left to absorb, these extents use a finer ladder (eight
  steps to a doubling), which gives back most of the quarter of the resolution
  the coarse one can round away.

A view that stands close under a lamp, where the bound would reach the plane
the light begins at, keeps the tight fit for that cascade.

Air Defense 1 at Quality. The sweep is one 360 degree turn at one pose on
OpenGL; the scores are nine frozen poses at 2560x1440 against stencil.

| Setting | Scale changes in the turn, per cascade | Texel, units | Shadow kept (GL / VK) | Falsely dark pixels (GL / VK) |
|---|---|---|---|---|
| 0, tight box | 8 / 8 / 8 | 0.47-0.73 / 1.43-1.78 / 5.44-6.80 | 99.1% / 99.1% | 7,583 / 8,586 |
| 2, nearest two | 0 / 0 / 8 | 0.72 / 2.03 / 5.44-6.80 | 99.2% / 99.2% | 9,138 / 9,996 |
| 4, all | 0 / 0 / 0 | 0.72 / 2.03 / 8.87 | 99.1% / 99.1% | 9,864 / 10,807 |

The self-test's fixture (the same sun from the level's opening, a degree at a
time) counts 24, 26 and 26 changes for the tight box and none for the sphere
(`R_ShadowMapCascadeTurnSelfTest`), with every in-light point of every slice
inside its crop all the way round.

Reading it: the two near cascades give up their best directions and keep the
tight fit's worst (0.72 against 0.47 to 0.73, 2.03 against 1.43 to 1.78), for
no resampling at all as the view turns. The far cascade pays more, 8.87
against 5.44 to 6.80, because a slice 1500 units deep reaches a fifth of the
way to that sun. The texel also grows and shrinks with the eye's distance from
the source (0.72 to 0.93 across the nine poses). Not measured: how the
resampling looks in motion, which is the reason for doing it.

With the default in place, the same turn counts 0 / 0 / 8 at texels of 0.72,
2.03 and 5.44 to 6.80; the nine poses keep 99.1% of stencil's shadow on OpenGL
and 99.2% on Vulkan; and Hub 2 at the Maximum preset, where spotlights are
cascaded too, runs under the Vulkan validation layers with no message.

## The stencil supplement on screen

The first batch fixed OpenGL ignoring the stencil of casters a light's map
cannot hold, and could only check it by reading: no stock view takes that
path. That is now measured rather than assumed. The shadow report's metrics
line gained `hybrid=`, the number of receiver passes drawn against such a
stencil, and four stock scenes at Quality (Hub 2, Storage 2, Air Defense 1,
Convoy 1, each over a full turn of the view) report `hybrid=0` and no
`SM missing caster` line. Stock content at the shipped settings does not
reach it; it is the safety net for a caster the map refuses.

So the case was forced. A temporary switch kept every animated model out of
the maps, which leaves its shadow to the stencil supplement, and a second one
restored the old always-pass test. In Hub 2 with a marine spawned in front of
the player and the game clock stopped, at 1920x1080 on OpenGL, four pictures
of the same moment:

| Picture | Pixels more than 12 levels from the stencil picture |
| --- | --- |
| shadow maps, nothing refused | 0.19% |
| animated models refused, old stencil test | 3.25%, all of it brighter: the shadow across the far wall is missing |
| animated models refused, fixed | 0.05% |

`hybrid=4` receiver passes a frame in both refused runs. Vulkan, which has its
own implementation of the same idea and was not changed, scores 0.05% in the
same forced case. Blended receivers of such a light go through the same
function and needed no change. Both switches were removed before the commit;
the `hybrid=` field stays.

## PBR receivers and the light's reach

The classic receivers skip the shadow filter where the light does not reach
the pixel. The PBR branch of the same four shaders ran the whole filter first
and multiplied the result into a radiance that was already zero. The PBR
evaluation builds its radiance from the light's falloff and projection
samples (`OpenQ4PBRDirect` on OpenGL, `EvaluatePBRDirect` on Vulkan), so
where their product is black the result is black whatever the shadow says;
the branch now takes those two samples first and filters only where they are
not black. The debug views still filter every pixel.

- **Exact.** The PBR showcase map's 24 captures (12 cameras, lit and
  ownership) are identical to the last bit before and after, on both
  renderers, and its 26 checks pass on both.
- **Cost.** OpenGL, the showcase overview at 2560x1440, Quality, three
  interleaved pairs of launches with the old shaders put back as loose files:
  6.58, 6.58 and 6.64 ms of GPU a frame with the change against 6.68, 7.13 and
  7.56 without. Never slower; the size of the gain is not pinned down, because
  the old shaders' three runs spread over 0.9 ms. That room is lit nearly
  wall to wall, so it is the unfavourable case. Vulkan was not timed: the old
  shader would have needed a second build.
- **Not covered.** A surface turned away from the light still filters: the
  PBR normal is resolved inside the evaluation. The modern OpenGL cluster
  loop, which is opt-in, is unchanged.

Stock content has no PBR owners, so nothing changes for the stock game; this
is for materials a mod or texture pack adds.

## Measured and left

**Building a light's cascade layout more than once a view.** Vulkan builds
the projected state two or three times per light per view. Timed in the turn
self-test (Air Defense 1's sun, four cascades, 7,200 builds): 1.3 microseconds
each with the tight fit, 1.5 with the sphere fit. A view with ten cascaded
lights spends under 0.05 ms on the repeats. Not worth a cache and its
invalidation. The timing code was removed.


**Skipping the blocker search under distant suns.** A distant point light's
stored depth is distance from a source thousands of units away, so the
point-source penumbra rule `separation / blockerDepth` can hardly widen the
filter there, and the nine-probe search looked like wasted work. Measured on
Vulkan in the Air Defense 1 arena at Quality with `r_shadowMapPCSSLightRadius 0`
against the default, interleaved in one launch: 6.14 and 6.09 ms of GPU without
the search, 5.75, 6.01 and 6.10 with it. No gain above the noise, and the
search also returns "fully lit" early, which the plain filter does not. Left as
it is.

## Open leads

Not done in this round. Each is real; the note says why it waited.

- **PCSS-lite on parallel lights** divides the caster-to-receiver gap by the
  blocker's depth into the light box, so softness follows height in the box
  and not the gap. A sun's penumbra should be `gap * tan(source angle)` in
  world units. It needs a per-cascade uniform on three receivers and a
  constant chosen by eye. Twelve stock parallel lights cast shadows.
- **The modern OpenGL cluster loop samples shadows before knowing the light
  reaches the pixel.** The path is opt-in. (The PBR receivers of the shipped
  paths were done: see above.)
- **Smaller items:** Vulkan takes a full-atlas layout round trip in views
  where every projected map is read in place; the point filter's rotated mode (off by
  default)
  hashes a fixed direction grid, seven to twenty-eight texels wide, where the
  projected one hashes the texel; the receiver-plane bias (off by default) mixes
  screen pixels with shadow texels; OpenGL asks for an unsized depth format
  where its bias assumes 24 bits (the format is shared by every depth image
  in the renderer, so sizing it is wider than shadows and wants the other
  platforms' drivers checked); the debug overlay draws its tile grid in
  atlas coordinates.

## Evidence

- **Cascade guard, before and after in one launch.** A temporary switch
  restored the old guard so both fits could be captured at the same frozen
  moment: nine Air Defense 1 poses at 2560x1440, Quality, scored against the
  stencil picture of the same moment (pixels stencil shadows that the map
  keeps at least half as dark). OpenGL: 98.8% kept before, 99.1% after;
  the standing own-shadow pose 97.0% to 98.8%. Vulkan: 98.8% to 99.1%, and
  7,476 to 7,109 pixels darkened where stencil leaves them lit. One pose
  (crater) scores a fifth of a percent lower on OpenGL: a thin distant shadow
  at the right edge of the view is softer with the new fit. The third cascade
  reports the same texel in both, so the likely cause is its crop's finer axis
  crossing a ladder step and that strip, past the cascade reach, falling to
  the whole-light map; this was not confirmed. The other eight poses show no
  pixel at a screen edge that the new fit lights and the old one shadows. The
  switch was removed before the commit.
- **Self-tests.** `rendererShadowPlannerSelfTest` and
  `rendererShadowProjectedDiagnosticSelfTest` pass on both renderers. The
  cascade stability test now checks that the slice keeps its guard in the
  crop's own texels and that the crop scales with the slice; the cache policy
  test pins the atlas hand-back and the rate-independent residency window.
- **Vulkan validation.** A Hub 2 sweep at Quality with
  `r_vkValidation 1` logs no validation message.
- **Cost.** Hub 2 at Quality, hidden 1440p sweep, interleaved windows: OpenGL
  4.2 and 4.0 ms of GPU against 3.6 with point lights on stencil; Vulkan 6.5
  and 5.6 against 5.4 with shadow maps off. In line with the 9 November
  figures on a shared machine; nothing here was expected to move them.
- **Source contracts.** `renderer_vulkan_shadow_compatibility.py` carries the
  changed contracts (unclamped view depth on both renderers, the shared centre
  tap, the residency window, the atlas hand-back); the twenty-four static
  tests that read the changed files pass.
