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
Single-map results vary between launches at some poses (opening 39% or 96%)
with identical interactions, casters and cache reports; the cause is open and
cascades are unaffected.

**Cached whole-light slot (OpenGL, `r_shadowMapCascadeFarCache`, default 1).**
The whole-light cascade does not move with the camera, so its still scenery is
kept in an atlas cell of its own (a projected cache entry in a separate pass-kind
namespace, keyed by the pass signature with the fitted projection replaced by
the whole-light cascade's planes, texel and depth contract). Each frame the cell
is copied into the cascade's tile and only moving casters are drawn over it.
A changed caster set, a moving light or a disabled static cache draws it fresh.
Cached and freshly drawn frames match outside the animated HUD (3 pixels by one
level over nine poses). Vulkan redraws the slot with the other cascades; its
whole cascade set costs a fraction of a millisecond.

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
