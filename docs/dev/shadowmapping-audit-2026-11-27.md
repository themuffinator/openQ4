# Shadow-mapping audit, 27 November 2026

A review of the shadow-map code on OpenGL and Vulkan for correctness, cost,
practice and picture quality: the shared planning that both renderers read
(cascade fits, atlas tiles, cache signatures), each renderer's map rendering,
and the receivers that sample the maps. It follows the
[9 November audit](shadowmapping-audit-2026-11-09.md) and feeds REN-03 and
FPS-03 in the [roadmap](roadmap.md).

Five independent read-throughs produced about thirty candidates. Each was
checked against the code before anything changed. Fourteen were fixed here, a
fifteenth (the cascade collapse behind a light's source plane) the same day, one
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
| **OpenGL drew the stencil supplement of a hybrid light and then ignored it.** A light whose map cannot hold every caster stencils the volumes of the missing ones and tests its mapped receivers against them. The receiver setup reset the stencil test to always-pass before every draw, so those casters shadowed nothing and their volumes were rasterised for no result. | **Fixed (OpenGL).** The receivers keep the test the supplement was stencilled for, PBR receivers included, and leave it open afterwards as before. Verified by reading the three call sites; no stock view was found that takes this path, so it has not been seen on screen. |
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
  reaching behind the source. The planner's diagnostic fixture, whose every
  slice straddles the plane, now expects three cascades where it expected the
  fallback.
- **Found on the way:** the opt-in modern planner refused every far-shell
  light. Its invariant required a positive cascade bias scale, and a map of
  far faces scales its receiver bias to zero by design (24 November). It logged
  `projected bias scale is invalid` and put the light on its stencil fallback.
  Zero is accepted where the light's receiver bias scale is zero.

## Measured and left

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

- **Cascade scale steps with camera yaw.** Each slice's light-space box is
  quantised to a 1.25 ladder per axis, so the crop size changes fifteen to
  twenty-four times in a full turn at the Air Defense 1 fixture. A
  rotation-invariant extent (the slice's bounding sphere) removes the steps at
  the cost of the resolution of the worst orientation. A judgement call that
  wants a moving capture, not frozen poses.
- **Off-screen animated casters are drawn into every map they touch.** A
  live caster whose shadow misses the view could be skipped. It cannot be done
  by the interaction's frustum test alone: a skipped caster's model is not
  regenerated, so after a quarter second it reads as static, joins the static
  map, is regenerated, reads as live and leaves again, re-rendering the map
  each time. It needs a liveness signal that does not depend on having drawn
  the model.
- **PCSS-lite on parallel lights** divides the caster-to-receiver gap by the
  blocker's depth into the light box, so softness follows height in the box
  and not the gap. A sun's penumbra should be `gap * tan(source angle)` in
  world units. It needs a per-cascade uniform on three receivers and a
  constant chosen by eye. Twelve stock parallel lights cast shadows.
- **Vulkan's default projected taps are binary.** PCSS-lite turns hardware
  comparison off for the whole receiver, so each of nine taps is a nearest
  compare and penumbrae have ten grey levels. Vulkan binds a compare sampler
  on the same image and could filter each tap bilinearly; OpenGL 2.1 cannot
  have both on one texture. Changing it breaks picture parity between the
  renderers and the PCSS ownership union.
- **Reflected model transforms and caster culling** disagree between the
  renderers: OpenGL turns the front face for a mirrored entity where the
  shared face verdict has already swapped it (mode 2), and Vulkan does neither
  in mode 1. A scan of the stock `.map` sources found no reflected entity;
  a mirrored model in a mod or the VR left hand would show it.
- **PBR receivers sample shadows before knowing the light reaches the pixel**
  (both renderers), and the modern OpenGL cluster loop does the same. Stock
  content has no PBR owners and the modern path is opt-in.
- **Vulkan keeps shadow memory after its consumer is switched off** (point
  caches after Quality to Enhanced, the atlas after `r_useShadowMap 0`) until
  a size cvar changes. OpenGL's idle purge and the slot release above cover
  its side.
- **Smaller items:** Vulkan takes a full-atlas layout round trip in views
  where every projected map is read in place; it builds the projected state
  two or three times per light per view; its shadow GPU timing spans can read
  a slot's previous result (diagnostics only); the point filter's rotated mode
  hashes a fixed direction grid, seven to twenty-eight texels wide, where the
  projected one hashes the texel; OpenGL's translucent moment passes do not
  check their framebuffer; the receiver-plane bias (off by default) mixes
  screen pixels with shadow texels; OpenGL asks for an unsized depth format
  where its bias assumes 24 bits; the debug overlay draws its tile grid in
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
