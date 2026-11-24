# Outdoor terrain shadow mapping

The stock Sandstorm (`mp/q4dm2`) terrain showed repeating dark filter lines.
Air Defense 1 (`game/airdefense1`) also lost terrain brightness and useful debris
shadows in its opening outdoor view. These scenes use authored point sources;
fixing only parallel sunlight would not address them.

## Changes

Point shadow filtering previously compared neighbouring cubemap texels against
one radial receiver depth. On sloped terrain their correct depths differ, often
by more than the four-world-unit bias budget. The receiver now derives a triangle
plane before divergent fragment control flow and compares each sample at the
ray/plane intersection. Hardware PCF is reconstructed with its bilinear weights
where that correction is needed. Samples crossing a cube seam use the adjacent
face's actual texel centre. Manual raw-depth sampling uses the same geometric
reference. The native lookup remains when the existing bias safely covers the
tap displacement and texel footprint; bias debug modes retain their bypasses.

Air Defense 1's main light is far outside its authored radius box. Its 512-pixel
cube faces cannot resolve nearby curved terrain and thin debris well enough,
even with corrected planar sampling. Such sources now use a perspective map
covering the complete oriented radius box, provided every corner lies in front
of the source. Ordinary enclosing point sources keep their cubemaps. The new
projection preserves the source position and ray directions, uses linear depth
along its axis, and includes blockers between the source and light volume.
It shares the existing cascade fitting, overlap, stabilization, caching and
distant-source filtering policies. Authored illumination and light attenuation
are unchanged. Point-shadow disabling still selects stencil for these lights.

Keep `r_shadowMapCSM 1` for outdoor quality; disabling cascades uses the complete
single projection at lower receiver resolution. No map-specific rules or stock
asset replacements are involved. Vulkan's ordinary and HDR shader variants are
regenerated from the same receiver source.

The expanded Air Defense 1 route exposed another omission at the entrance:
railings first rejected by shadow LOD could remain rejected after the camera
approached them. The surface cache and randomized hold used the host's
`idLib::frameNumber`, which does not advance in the renderer modules' separate
idLib state. Shadow admission now refreshes for each renderer view, while the
hold uses the renderer's frame counter. Both backends share this correction.
It preserves the authored distance/coverage thresholds and staggered hold.

## Cascaded sunlight on Quality and Ultra, 19 November 2026 (#239)

Players reported blocky or missing character shadows outdoors on Quality.
Air Defense 1's sunlight is `light_146`, a point light whose `light_center`
puts its source about 19,000 units from a 14,784 x 13,888 x 6,368 volume. Its
single 2048 map measures 25 world units per texel at the player (renderer
estimate), so a character is two texels wide and the texel-scaled bias and
normal offset remove most nearby shadows. Cascade 0 measures 0.73.

The change follows id Tech 6 (DOOM 2016, SIGGRAPH 2016 "The Devil is in the
Details", slides 17-18): cascades for directional lights only, one PCF path for
every light type, and static shadows cached separately from moving casters.

- **Presets.** Quality and Ultra now set `r_shadowMapCSM 1` with
  `r_shadowMapProjectedCSM 0`: sunlight, parallel and distant point sources are
  cascaded and ordinary spotlights keep one map. Maximum keeps cascading
  spotlights. The preset system version moves to `1.46eb190c`, so existing
  players see the system check once.
- **Whole-light last cascade** (`r_shadowMapCascadeFarMap`, default 1). The
  fitted cascades ended at `r_shadowMapCascadeDistance` and receivers beyond
  their crops came out unshadowed (the cascade range end in REN-03). The last
  of the requested cascades now holds the light's base projection at the
  cascade tile size, with the single map's bias contract
  (`R_ShadowMapSetWholeLightCascade`). The other cascades split the reach.
- **Per-pixel selection past the reach.** Choosing the whole-light map by
  view depth alone cost elevated views most of their distant shadows (ridge
  91% -> 60%): Air Defense 1's buildings stand past the reach but inside the
  last fitted crop, at 6.8 units a texel against the whole map's 50. A
  per-view "hold depth" (deepest cross-section inside that crop) did not help,
  because the full frustum cross-section leaves the crop long before the
  buildings do. Every receiver path now tests the last fitted crop per pixel
  (`CascadeHoldMargin`: GL `shadow_interaction.fs`, Vulkan
  `interaction_shadow.frag`, the modern clustered library) and fades to the
  whole-light map over the crop's outer 5%. Inside the reach a receiver never
  blends toward the whole-light map. Vulkan carries the index in
  `texelSize.w`, GL in `uShadowWholeLightCascade`, the modern record in
  `counts.w` (index + 1, 0 = none).

Measured on ten frozen poses (the nine stops below plus the player standing
at `info_player_start` looking at the ground), 2560 x 1440, Quality. Score:
the share of pixels that stencil shadows by more than 15 levels where the mode
keeps at least half of that occlusion.

| Mode | OpenGL | Vulkan |
| --- | ---: | ---: |
| Single map (0.14.1 Quality) | 27.7% | 27.6% |
| Four fitted cascades (0.14.1 Maximum layout) | 96.0% | 96.0% |
| Three fitted + whole-light (new Quality) | 96.4% | 96.4% |

| Pose | Single map | Four fitted | New |
| --- | ---: | ---: | ---: |
| Standing at the start, own surroundings | 0.6% | 90.5% | 90.6% |
| Wreckage | 5.6% | 100% | 100% |
| Opening | 37.4% | 94.7% | 94.5% |
| Ridge (elevated) | 68.3% | 90.2% | 90.1% |
| Vista | 81.1% | 81.7% | 89.3% |
| Distant | 92.9% | 84.1% | 89.7% |

(OpenGL; Vulkan agrees within one point everywhere.) Splitting the reach
three ways instead of four costs up to a point at tank, reverse and opening.
The single-map column is the map as the preset draws it and repeats in every
launch. Launches that seemed to score 96% at the opening were a fault in the
capture script (see "Single map: why two launches scored higher" below).

**Cached whole-light slot (OpenGL, `r_shadowMapCascadeFarCache`, default 1).**
The whole-light cascade does not move with the camera, so its still scenery is
kept in an atlas cell of its own (a projected cache entry in a separate pass-kind
namespace, keyed by the pass signature with the fitted projection replaced by
the whole-light cascade's planes, texel and depth contract). Each frame the cell
is copied into the cascade's tile and only moving casters are drawn over it.
A changed caster set, a moving light or a disabled static cache draws it fresh.
Cached and freshly drawn frames match outside the animated HUD (3 pixels by one
level over nine poses). Vulkan redraws the slot with the other cascades. The
same cache was built for Vulkan on 24 November and left out: it removed 20 to
176 caster draws a view, but copying the tile back cost the GPU as much as
drawing it at Quality and more at Ultra
([measurements](vulkan-resource-profiling.md#shadow-map-savings-from-opengl-24-november-2026)).

Cost, Air Defense 1 opening, view turning, per frame. Caster draws come from the
`casterDraws=` field added to the `SM metrics` report and do not depend on load:

| Layout | Caster draws | Caster indexes |
| --- | ---: | ---: |
| Single map (0.14.1 Quality) | 17 | 27k |
| Four fitted cascades (0.14.1 Maximum layout) | 359 | 333k |
| Three fitted + whole-light, slot redrawn | 447 | 379k |
| Three fitted + whole-light, slot cached (new default) | 296 | 279k |

Vulkan's projected map rendering measured 0.09 ms for the single map and 0.23 ms
for the new layout (three interleaved repeats each). OpenGL timer queries were
not repeatable on the shared machine on 19 November (two other projects'
clients held the GPU); the quietest earlier run put four fitted cascades at
+0.6 to +1.4 ms of shadow GPU time over the single map at 2560 x 1440, and the
cached layout issues 18% fewer caster draws than that layout. The FPS-02
Quality/Ultra budgets still need re-measuring on the reference machine.

`rendererShadowProjectedDiagnosticSelfTest` runs
`R_ShadowMapCascadeWholeLightSelfTest` on light_146's parameters: layouts of
four fitted, three plus whole-light and one plus whole-light; fitted crops
holding receivers inside the reach; the last cascade equal to the base
projection; every receiver past the reach inside the light covered; and a
distinct cache identity. The planner fixtures pin fitted-only cascades.

## Single map: why two launches scored higher (19 November 2026)

During the #239 measurements, two of seven launches scored the single map far
higher: 96% at the opening, 99.9-100% at the wreckage. The interaction, caster
and cache reports matched the low launches. The renderer was not at fault.
Before each mode, the capture script reset a fixed list of cvars, and the
receiver and caster bias cvars were not on it. A mode that zeroed one, such as
`r_shadowMapNormalOffsetScale`, left it zeroed for every later mode and pose
in that launch. The two high launches were the two that compared bias
settings. Their single-map captures after the first pose had no bias, so the
reports still matched: bias moves no casters and no interactions. The only
field that differed, `expanded=`, counts casters whose stencil volume lies
outside this view. It follows the camera and animated entities, not the map.

Evidence, Quality with `r_shadowMapCSM 0`, 2560 x 1440. Each column is one
mode in a single launch, captured at the same frozen moment:

| Pose | Preset | Preset again | Normal offset 0 | Every bias 0 | Old script, after a bias mode |
| --- | ---: | ---: | ---: | ---: | ---: |
| Opening | 37.8% | 37.9% | 43.9% | 93.0% | 93.1% |
| Wreckage | 5.6% | 5.6% | 99.9% | 99.9% | 99.9% |
| Standing at the start | 0.9% | 0.9% | 0.9% | 13.6% | 13.5% |

(OpenGL. Vulkan gives 39.0%, 39.1%, 43.9% and 93.0% at the opening and 5.8%,
5.8%, 100% and 100% at the wreckage. "Every bias 0" also zeroes
`r_shadowMapTexelBiasScale`, `r_shadowMapBias`, `r_shadowMapNormalBias` and
`r_shadowMapPolygonOffset`. In the old-script launch, the first single-map
capture, taken before any bias mode, scored 0.7% standing at the start.)

The single map is deterministic. With every cvar reset, six launches (five
OpenGL, one Vulkan) scored 37.7-41.4% at the opening, 5.5-5.8% at the
wreckage and 0.7-0.9% standing. Repeat captures within a launch matched.
Small differences between launches come from the storm and cannon-flash
lights.

The low scores are the single map's bias contract working as designed. The
normal offset pushes each receiver about one map texel along its normal:
25 world units, from the light's far-plane width (19.3 at the player's depth).
That detaches contact shadows within about a texel of their caster. The
wreckage's shadows are nearly all such contacts. Zeroing the offset alone
restores them. In another launch whose poses sat 12 units higher, offset
scales of 1, 0.5, 0.25 and 0.1 kept 23%, 69%, 79% and 80% at the wreckage.
The opening needs every term removed at once: no single term moves it by more
than about six points. Standing at the start stays near 1% with any bias, because
at this resolution the player is two texels wide.

Removing the bias is not a fix. It is the acne guard, and without it the
opening shows more than three times as many falsely darkened pixels
(653 to 2,174). Cascades are the fix, and they are the default on Quality and
Ultra: cascade 0's texel is 0.73 units, so its offset is too. Sizing the
offset by each receiver's depth, the next section, recovers part of the rest.

To repeat these comparisons, reset every cvar any mode sets before each mode,
and compare launches only when their pose lists match. A ground pose run first
changes the eye height of every pose after it by 12 units.

## Normal offset sized at each receiver's depth for distant lights (19 November 2026)

A distant point light's map (light_146, and any source far outside its
radius box) is a perspective view from its source, so one texel covers more
ground the farther a receiver is from that source. The normal offset was one
distance per cascade, `r_shadowMapNormalOffsetScale` texels measured where
the volume is widest, at its far corner. Air Defense 1's single map pushed
every receiver 25 units, where the texel at the player is 19.3 units; each
fitted cascade overshot by the same ratio, the player's depth against the far
corner's (18,850 against 24,530 units from the source).

Each receiver's offset now follows its own texel. The projected state records
`texelPerDepth` per cascade: the world texel per unit of light depth `w`,
`(2 / tileSize) / |S across the depth row|`, taking the coarser of S and T.
A cascade crop scales S and T and adds multiples of the depth row, so the one
formula holds for the single map, every fitted crop and the whole-light slot.
At the volume's far corner it equals the old size; nearer the source it is
smaller, so no receiver's offset grows. Spotlights, parallel lights and
ordinary point lights are unchanged; point cube maps already scale their
offset by radial distance.

The receivers read it from the existing offset value, which can never be
negative, so the sign carries it: a value below zero is the offset per unit
of `w`, and the shader multiplies it by the receiver's own `w` (its shadow
row 3). GL `shadow_interaction.vs`, Vulkan `interaction_shadow.vert` and the
three upload sites (`R_ShadowMapProjectedNormalOffsetWorld`) share it. The
Vulkan shadow block is full at 32 vec4s, so it gains no field. The classic
interaction domain snapshots the switch with the pass's other receiver
settings and hashes it into the pass identity. The opt-in modern visible path
(`r_rendererModernVisible`) keeps the far-corner size: its descriptor record
is full and its texel also feeds the footprint bias and the ARB2 parity
checks.

`r_shadowMapDistantNormalOffsetDepth 1` (default) turns it on; `0` restores
the far-corner size.

Measured at one frozen moment per pose, nine Air Defense 1 poses, 2560 x 1440,
Quality, both settings in the same launch:

| Pose | Single map, far-corner offset | Single map, depth offset | Cascades, far-corner offset | Cascades, depth offset |
| --- | ---: | ---: | ---: | ---: |
| Standing at the start | 0.5% | 0.5% | 90.7% | 91.3% |
| Opening | 40.7% | 41.3% | 95.1% | 95.3% |
| Wreckage | 5.9% | 6.5% | 100% | 100% |
| Tank | 80.0% | 83.0% | 94.7% | 96.3% |
| Ridge | 69.9% | 73.0% | 90.6% | 92.4% |
| Crater | 71.0% | 77.5% | 88.1% | 91.1% |
| Reverse | 80.5% | 83.2% | 93.8% | 95.0% |
| Vista | 80.4% | 84.9% | 89.0% | 91.0% |
| Distant | 93.0% | 94.0% | 89.7% | 90.6% |
| **All poses** | **27.6%** | **29.0%** | **96.4%** | **97.0%** |

(OpenGL. Vulkan: single map 27.9% to 29.3%, cascades 96.5% to 97.1%.) No pose
loses. The falsely darkened pixels fall too, because an offset that is too
large also moves the lookup into a neighbour's shadow: on OpenGL, 17,002 to
13,841 for the single map and 19,026 to 18,007 for the cascades; on Vulkan,
14,710 to 12,927 and 19,484 to 18,114. The wreckage stays dark for the single
map: its contact shadows sit within the 19-unit texel itself.

`rendererShadowProjectedDiagnosticSelfTest` runs
`R_ShadowMapDistantNormalOffsetSelfTest` on light_146: 55 receiver texels
(the volume's corners and centre, the player start and the wreckage, through
the single map, three fitted cascades and the whole-light slot) agree within
0.5% with the texel measured by stepping across the depth row; the depth
offset never exceeds the far-corner size; switched off, the offset keeps that
size; and a parallel light keeps one offset.

With the new default, the terrain check and the eleven-stop Air Defense 1
route below pass on OpenGL and Vulkan against a packs-only asset view (the
Steam folder's `baseoq4` and loose recompiled maps are refused). No lit terrain
pixel in Sandstorm or Air Defense 1 darkens past the gate, the route's lit
areas move by at most 0.22 display levels, its shadow contrast stays at
82-99.5% of stencil, the entrance railing keeps 99.0% (OpenGL) and 99.1%
(Vulkan) support, and cached and fresh frames differ by at most 0.01 RMS.

## Reproduction

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --terrain-check --hidden `
  --basepath 'E:\SteamLibrary\steamapps\common\Quake 4' `
  --output .tmp/outdoor-shadow-review
```

The runner uses the repository's mode-specific SP/MP launches, staged binaries,
isolated saves, disabled mouse grabbing, explicit multiplayer auto-join and an
engine-owned window. Captures come from the registered `screenshot` command.
`--basepath` overrides a stale machine-specific asset path in the launch catalog.

The image check identifies lit terrain from agreement between each run's stencil
and shadows-off images. It rejects mean darkening beyond two display levels or
more than 10% of that region darkened by over six levels. A separate stencil
shadow region must retain at least 80% of the reference shadow contrast, so an
unshadowed image cannot pass. These gates reject the original captures: Sandstorm
darkened 24% of its lit terrain region; Air Defense 1 darkened 99% and retained
only 67% of the selected debris shadow contrast.

`rendererShadowProjectedDiagnosticSelfTest` also exercises the production distant
projection on 32 receiver/blocker rays under rotation and translation. It checks
volume coverage, equal projected coordinates along each ray, monotonic stored
depth, and retention of ordinary point/parallel classifications.

For the wider Air Defense 1 comparison:

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --scenario airdefense1-outdoor `
  --hidden --basepath 'E:\SteamLibrary\steamapps\common\Quake 4' `
  --output .tmp/airdefense1-outdoor-review
```

This route covers nine distinct outdoor poses: the opening, wreckage, tank,
ridge, crater, reverse view, entrance, elevated vista and distant terrain.
It approaches the entrance before any shadow-mode switches can recreate
interactions, revisits it later, and returns to the opening. Each of the eleven
stops captures mapped, cache-bypassed, stencil and unshadowed rendering with
simulation frozen. Logged camera poses must match, including equivalent wrapped
angles. The lower-screen terrain and caster regions use the same brightness
and contrast gates as above. Cached/fresh images must agree within one RMS
display level. A separate entrance check allows two pixels of filter spread
but requires at least 90% of the stencil railing-shadow footprint to retain
nearby mapped occlusion; the missing-rail capture fails at 41%.

`rendererShadowPlannerSelfTest` also checks the production LOD cache with a
far-to-near view change within one renderer frame, retention during the hold,
and rejection after the hold expires. This catches the stale-clock problem
without depending on one map or backend.

## Local validation

The Windows x64 debug build passes the native renderer contracts, Vulkan shadow
compatibility checks and generated SPIR-V header verification. The staged client,
renderer modules, game modules and shader package match their build outputs by
SHA-256. Both outdoor maps pass the image gates on OpenGL and Vulkan:

| Scene / renderer | Lit mean difference (0–255) | Excessively dark lit pixels | Retained shadow contrast |
| --- | ---: | ---: | ---: |
| Sandstorm / OpenGL | +0.83 | 1.12% | 94.3% |
| Sandstorm / Vulkan | −0.21 | 1.27% | 94.3% |
| Air Defense 1 / OpenGL | +1.38 | 0.10% | 89.1% |
| Air Defense 1 / Vulkan | −0.01 | 0.08% | 89.2% |

An additional six-run matrix covers those maps and Air Defense 2's indoor opening
on both renderers. Engine captures were reviewed with raw-depth comparison,
cache bypass, a single projection, rotated filtering and point-shadow disabling.
Air Defense 1 and the indoor control reproduce the stencil image exactly when
point shadow mapping is disabled. Air Defense 1's single projection remains
usable, with reduced terrain and shadow detail compared with cascades. The MP
scene continues to animate, so its sequential whole-image controls are not
expected to match byte for byte. No renderer or shadow-resource failures were
reported.

### Expanded Air Defense 1 validation

The final Windows x64 staged build passes all eleven outdoor stops on both
OpenGL and Vulkan (88 scenario captures). The entrance railing check rises
from 40.8% support in the failing capture to 97.8% on OpenGL and 97.9% on Vulkan,
both on the initial approach and the return. All cached/fresh full-frame pairs
are identical. Visual review confirms terrain, structure, debris and railing
occlusion with the expected filtered edges relative to stencil.
Stock mover, helmet and door exclusions can still emit `SM missing caster`
diagnostics with retained stencil volumes. The existing compatibility path
keeps those volumes available as supplements to mapped ownership.

| View | GL lit delta (0–255) | Vulkan lit delta (0–255) | GL shadow contrast | Vulkan shadow contrast |
| --- | ---: | ---: | ---: | ---: |
| Entrance, first approach | +0.59 | −0.23 | 82.4% | 83.6% |
| Opening | +1.15 | −0.02 | 89.9% | 90.5% |
| Wreckage | +1.23 | 0.00 | 99.4% | 99.5% |
| Tank | +0.79 | −0.03 | 96.1% | 96.2% |
| Ridge | +0.74 | −0.01 | 87.4% | 92.3% |
| Crater | +0.76 | −0.01 | 83.1% | 90.3% |
| Reverse | +0.86 | −0.02 | 92.5% | 96.4% |
| Entrance, return | +0.52 | −0.23 | 81.8% | 82.7% |
| Elevated vista | +0.80 | 0.00 | 87.5% | 88.3% |
| Distant terrain | +2.13 | 0.00 | 89.8% | 90.5% |
| Opening, return | +1.15 | −0.02 | 90.2% | 90.8% |

At most 1.2% of the lit reference pixels exceed the six-level darkening
threshold at any stop. Shadow contrast is relative to each run's frozen
stencil/shadows-off pair; actor and effect state can differ between processes.
The first ridge angle faced predominantly lit ground and could contain too few
reference shadow pixels to qualify. Full-detail and lower-camera controls
confirmed this, so the final ridge view looks toward structure shadows on the
slope instead of weakening the reference-mask gate.

The LOD self-test passes in both renderer modules, including Vulkan without
the optional scene-packet planner. Sandstorm's terrain comparison and Air
Defense 2's indoor opening also pass on both backends after the LOD change.
Air Defense 1 reports no warnings. No renderer-resource or Vulkan validation
failures occur in these six final runs.

Expanded evidence is retained under `.tmp/airdefense1-outdoor-views/`: `final/`
contains the complete route, `sandstorm/` and `indoor/` contain the control
runs, and `final_image_validation.json` records the image checks and LOD test.
`entrance_before_after.png` shows the repaired railing shadows;
`final_gl_1.png` through `final_gl_3.png` and their Vulkan counterparts compare
all nine views with stencil and unshadowed rendering. `runtime_identity.json`
records the six matching build/stage hashes. Diagnostic probes and logs remain
alongside this evidence; automatic approval review blocked removal of their
verified disposable caches.

Unrelated stock-content diagnostics remain: Sandstorm reports missing AI AAS,
sound and precache warnings; Air Defense 2 reports uppercase asset paths. The
local launch catalog also retains a C-drive Steam asset path; the test command
above supplies the installed E-drive location explicitly.

Local Windows x64 evidence is retained under `.tmp/outdoor-shadows/`, including
the stock before/after images, launch/configuration files, logs and runtime
hashes. Other-platform qualification and the separate general Vulkan parity and
frame-pacing investigations remain open.
