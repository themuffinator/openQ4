# Shadow-map replacement progress — 2026-09-05

Third round following the [transition tests](shadowmapping-transition-tests-2026-09-05.md).
This round repairs unnecessary fallback and cache invalidation, reduces point-light
draw submissions, and checks actual cache use alongside image correctness. Shadow
mapping remains optional: the remaining compatibility cases below still need the
stencil implementation.

## Repairs and optimisation

### OpenGL retained no world cache across 2D views

`RB_ShadowMapPrepareCacheView` treated a view without a render world as a world
change. HUD, console, and post-processing views could therefore invalidate the
preceding world's projected and point maps every frame. The cache-enabled image
could be correct while silently paying for fresh rendering.

A temporary trace in Air Defense 2 recorded the owner alternating between the
map's render world and null on consecutive views in each frame. Non-world views
now leave cache ownership alone. A real render-world, map CRC, or map-name change
still invalidates the cache. The diagnostic trace was removed after confirmation.

The cutout transition fixture now requires actual flashlight cache reuse during
its twelve- and six-caster phases. It exposed this defect after the initial visual
comparisons passed. Earlier GL captures with caching enabled are not, by themselves,
evidence of cache hits. The GL scenarios were repeated after this repair.

### Live cutouts no longer discard the opaque projected cache

Previously, any perforated caster made the entire light uncacheable. Perforated
surfaces now enter the live dynamic chains even when their entity and transform
are static. This is necessary because image animation, alpha-stage conditions,
and texture matrices can change coverage without updating an entity.

Both classic backends retain opaque static projected depth and compose current
cutout/moving casters over it. Static cache keys no longer include total, alpha,
or dynamic caster counts, so adding or removing live casters does not invalidate
unchanged opaque depth. Static caster membership, transforms, geometry, materials,
projection, and resource identity remain part of cache validation.

Point cubemaps still regenerate when they contain dynamic or perforated casters;
they do not yet implement static/live composition. Translucent moments and the
experimental shared interaction path retain their existing conservative gates.

### Real emitter blockers stay in depth maps

The near-emitter heuristic could reject a panel that actually produced a stencil
volume, forcing missing-caster fallback. Surface-volume provenance now overrides
that heuristic and admits the real blocker to the depth map. Empty emitter probes
remain excluded, preserving the earlier protection against false wedge occlusion.

The retained pre-fix Storage 2 witness names light 289, entity 160,
`func_mover_11942`, and `textures/common_lights/small_light4`, with
`admitted=0 eligible=1 volume=1`. The 90-second lift sequence now reaches that
light on both APIs without reporting a missing caster. This closes that specific
fallback case; it does not disable completeness checks for other missing casters.

### Tighter point-face bounds reduce unnecessary submissions

OpenGL and Vulkan share `R_ShadowMapCasterOutsidePointFace`. It transforms caster
bounds conservatively under rotation, scale, reflection, and shear, then tests
the four side planes of the selected cube face. Invalid or non-affine inputs stay
visible. A margin protects boundary cases.

This replaces GL's looser sphere test and adds rejection before Vulkan's legacy
walker binds/uploads caster geometry. The bounds support calculation needs no
square root. Vulkan's experimental sealed walker is outside this draw reduction.

The engine diagnostic self-test checks 960 affine cases, independently requiring
all eight transformed corners to lie outside one face plane for every rejection.
Both APIs report 650 conservative rejections and pass the existing cascade-fit
self-test. This mathematical check complements the retail-map images.

### Diagnostics describe completed work

Vulkan cache reporting now runs after the view's shadow and interaction work,
including when the visual overlay is disabled. Previously, reporting during
preparation hid rendered tile/face counts, composition work, and late failures.
`casterFaces=C/T culled/tested` records candidate caster-face submissions skipped
before geometry binding.

The runner now takes its summary before the mapped capture, rather than from the
final intentional shadows-off frame. It also rejects failed engine self-tests.
The opaque and cutout caster fixtures check actual flashlight caster counts. The
cutout fixture additionally checks cache reuse, so enabling a CVar is insufficient
to pass an optimisation test.

## Retained gameplay cases

Tests use the mode-specific SP/MP launch configurations, staged binaries, installed
retail PK4s, isolated save directories, and bordered 960×540 windows. All images
come from the engine's registered `screenshot` command. No operating-system input
or screen capture is used. MP explicitly joins gameplay with `ui_autoJoin 1` and
enables its cheat commands with `net_allowCheats 1`.

| Scenario | Stock map | Exercise | Captures per API |
| --- | --- | --- | ---: |
| `flashlight-cycle` | Air Defense 2 | Actual weapon light off/on, turn/return, cache-off update budget of one | 10 |
| `caster-cycle` | Air Defense 2 | Twelve overlapping stock crates, then six, then none; flashlight and scene lights active | 15 |
| `caster-cycle-point` | Air Defense 2 | Same insertion/removal under isolated steady point light 168 | 15 |
| `caster-cycle-cutout` | Air Defense 2 | Same static models with stock `textures/terminal/square_hole` coverage | 15 |
| `emitter-lift` | Storage 2 | Mapped/stencil/off triplets through four frozen phases, advancing gameplay 30 seconds between phases | 15 |
| `cascade-motion` | Q4DM9 | Four cascades, sub-unit translation, turn, return, coverage/blend changes, and shadows-off controls | 22 |
| `resource-cycle` | Air Defense 2 | Live projected/cube resolution, comparison mode, moments, and cache changes | 12 |

The concluding mapped/stencil/unshadowed triplet is included in each count. Use:

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --scenario caster-cycle-cutout --output .tmp/shadow-cutouts
python tools/tests/renderer_shadow_mapping_maps.py --scenario emitter-lift --output .tmp/shadow-lift
```

The runner executes GL and Vulkan sequentially; `--apis gl` or `--apis vulkan`
selects one. Use a new output directory. `--dry-run` writes configurations without
launching. Fixtures create temporary entities with existing models and materials;
they install no custom assets.

In the cutout fixture, the flashlight keeps 30 opaque static surfaces while 24,
then 12, perforated surfaces remain live. Removing the last six entities returns
the light to opaque depth alone. The opening scene can briefly include one extra
transient caster; the added and removed fixture sets are checked exactly.
Cached/fresh/repeated-fresh images are compared because animated corridor lights,
effects, and the world GUI still vary between captures. Q4DM9's pickups also keep
animating in MP; static receiver boundaries are the useful comparison.

With six cutout crates and after removing them all, cached and fresh RGB images
are identical on both APIs. With twelve crates, GL's cached/fresh RMS is 0.757 and
Vulkan's is 2.766; the repeated fresh control reproduces each value and changed
pixel count. These controls support the visual finding of consistent shadow
coverage despite scene animation. The small opening differences are not used to
claim exact cache parity.

An initial cutout fixture used a stock two-sided material that implicitly disabled
shadow casting. Its successful processes did not exercise the intended path. Those
runs are excluded, and the strengthened admission check rejects both as negative
controls. Early lift tests stopped before the emitter was reached; the retained
sequence advances simulation with shadows enabled for the full 90 seconds.

## Measured work

On Windows x64 Debug with an NVIDIA RTX 4060 Laptop GPU, the sampled Vulkan reports
reject these candidate caster-face submissions before binding geometry:

| Scenario | Culled / tested across sampled reports | Reduction |
| --- | ---: | ---: |
| Q4DM9 cascade sequence | 5,121 / 10,434 | 49.1% |
| Air Defense 2 full crate sequence | 17,720 / 37,008 | 47.9% |
| Air Defense 2 isolated point sequence | 9,034 / 17,958 | 50.3% |
| Storage 2 lift sequence | 29,418 / 64,824 | 45.4% |

These are sampled candidate submission counts, including load/view warmup where
applicable. They are not whole-frame draw-count or FPS improvements.
The table uses the first round-three `cascade-after`, `caster-cycle-after`,
`caster-cycle-point-after`, and `emitter-lift-after` Vulkan logs; later package
qualification captures do not replace those measurement samples.

A separate Vulkan GPU timestamp experiment isolates Q4DM9 light 179 at
`(1000, -1024, 450)`, yaw 180, and alternates cache-off and cache-on blocks. Each
block has 180 warmup frames followed by 240 measured frames, with reports every
five frames and no intervening screenshots. Median reported costs are:

| Block | Samples | Shadow render | Static-depth copy | Total |
| --- | ---: | ---: | ---: | ---: |
| Fresh A | 47 | 1.74 ms | 0.00 ms | 1.74 ms |
| Cached A | 48 | 0.05 ms | 1.08 ms | 1.13 ms |
| Fresh B | 48 | 1.81 ms | 0.00 ms | 1.81 ms |
| Cached B | 48 | 0.05 ms | 0.60 ms | 0.65 ms |

Static geometry redraw falls substantially, but copying large cascade depth still
has a material, variable cost. This is a view-specific shadow-pass measurement,
not a full-frame benchmark. It does not establish equivalent results on other
drivers. `r_shadowMapCacheCSM` remains opt-in.

## Evidence and replacement limits

The accepted matrix comprises **14 successful scenario/API runs and 208 engine
captures**. Every selected capture was checked for dimensions and visually reviewed;
the strict log checks report no unexpected missing casters, failed shadow resources,
texture-state errors, or Vulkan validation failures. The two non-casting material
runs are rejected by the admission gate. The initial GL cutout result also exposed
an overly rigid opening-caster assertion; the validator now permits the observed
single transient opening caster while keeping the spawned sets and cutout coverage
exact, and revalidation passes.

The full MSVC build, engine/renderer builds, and release-style staging pass.
Vulkan shadow compatibility, world-interaction compatibility, classic interaction
domain, macOS shadow-policy, Python compilation, and whitespace checks pass. The
macOS check validates source policy, not Apple-hardware rendering.
The final selected cascade pair was also repeated after staging concurrent MP
changes. A transient full-build failure during those unrelated MP edits was
followed by a passing full build; the shadow renderer targets built successfully.

Local evidence is retained under `.tmp/shadow-round3-20260905/`: per-case launch
commands, scripts, logs, engine images, contact sheets, cache traces, negative
controls, and GPU timing summaries. `final-evidence.py` rechecks every selected
capture, strict caster/failure diagnostics, and the rejected material fixture;
`final-evidence.json` records the accepted selection. The separate timing run's
three final reference captures are additional to the scenario matrix.

The changes retain stencil compatibility where mapped coverage is not complete:

- Vulkan cinematic, dynamic-image, and non-explicit-coordinate alpha stages
  remain unsupported mapped coverage. GL can use conservative solid coverage for
  some unsupported stages, which is not exact material parity.
- Custom/animated receiver paths and experimental shared/modern ownership have
  remaining conservative fallback rules. A classic interaction walker can still
  render shadow maps; selecting that walker does not itself mean stencil shadows.
- Point lights with live casters lack static/live depth composition. Translucent
  moments remain an OpenGL experiment, with no general Vulkan parity claim.
- Required resource failures, hardware limits, and configured subview policies
  still need complete stencil coverage. Near-emitter provenance also still uses
  stencil-volume probes, even when the final shadow is mapped.
- The Apple legacy GL tier remains stencil-only; MoltenVK rendering lacks accepted
  Apple-hardware evidence. These Windows tests do not qualify Linux/macOS drivers,
  long campaign playthroughs, continuous-motion shimmer, or campaign-wide budgets.

Unrelated stock asset path-case, missing AAS/sound, non-precache, and Vulkan legacy
vertex-array warnings remain. Storage 1's earlier green corpse-effect discrepancy
is outside these maps and remains unresolved.
The isolated Q4DM9 view also has darker wall/ambient rendering on Vulkan than GL,
including with shadows disabled. The within-backend shadow comparisons do not
establish complete cross-backend image parity or resolve that difference.
