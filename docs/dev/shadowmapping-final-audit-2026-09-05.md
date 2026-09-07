# Shadow-mapping final audit — 2026-09-05

This closes the implementation review after the [door and mover tests](shadowmapping-movers-2026-09-05.md),
with additional randomly selected stock maps and the retained Air Defense 1
mapped/stencil comparison. Validation uses Windows x64 Debug, the NVIDIA RTX
4060 Laptop GPU, staged binaries, installed retail assets, isolated settings,
and bordered 960×540 windows. All captures use the engine `screenshot` command.
No operating-system input or screen capture is used.

## Additional repair: authored translucent shadow opt-outs

A temporary point light in Recomp exposed a large black wall region in stencil
mode that did not match mapped shadows. The stock `textures/decals/slime`
material includes both `DECAL_MACRO` and `noShadows`, but also has a specular
lighting stage. `r_stencilTranslucentShadows` admitted it solely because it was
translucent and received lighting, overriding the authored exclusion. Decal
geometry then produced an unwanted solid stencil volume over the wall.

The same fixture with translucent stencil casting disabled removed the black
region. Disabling all translucent shadows is too broad a remedy: eligible
glass and other translucent casters should retain their behavior.

Material parsing now distinguishes an authored `noShadows`/`DECAL_MACRO` from the
no-shadow flag inferred automatically for translucent coverage. Stencil casting,
binary mapped translucent casting, and experimental translucent moments honor
the authored exclusion. `forceShadows` retains its established precedence in
either directive order. No material files or other retail assets are changed.

The in-engine caster self-test parses five intrinsic-image materials: an implicit
translucent default, explicit `noShadows`, `DECAL_MACRO`, and both orderings of
`forceShadows` with `noShadows`. It checks the actual parser and admission helpers,
including that the option still admits eligible translucent casters. Source
contracts additionally check both stencil/map consumers and moment admission.
The engine and companion `Material.h` declarations are synchronized; the new
flag uses an unused bit and the accessor adds no object-layout or virtual-table
change. The shared-material compatibility check passes.

| Recomp diagnostic light | Before: mapped/stencil MAE | After: mapped/stencil MAE |
| --- | ---: | ---: |
| OpenGL | 13.746 | 2.612 |
| Vulkan | 12.611 | 1.356 |

MAE is mean absolute RGB error in 8-bit color units across the full image. The
captures visibly confirm removal of the unwanted wall shadow. Remaining error
includes filtering, cutout coverage, and other shadow-boundary differences;
these numbers do not establish pixel parity or a performance improvement.

Reproduce the bright diagnostic fixture without adding content:

```powershell
python tools/tests/renderer_shadow_mapping_scenes.py recomp-decal .tmp/recomp-decal.cfg
python tools/tests/renderer_shadow_mapping_maps.py --maps recomp --extra-cfg .tmp/recomp-decal.cfg --output .tmp/recomp-decal-review
```

## Earlier Air Defense 1 reference

The retained reference is dated **2026-08-24/25** and lives under
`.tmp/shadowmap-review/`, especially `final-ab-runtime-6`. It compares separate
mapped and stencil processes in active `game/airdefense1` gameplay. Its camera
command is `setviewpos 10200 -6800 40 0 170 0`; an immediately following `viewpos`
still printed the preceding presentation pose. Trusting that stale report
would compare a different direction.

The current map runner now includes the original camera command and waits for
presentation to update before freezing. It validates all six resulting camera
components with a one-unit tolerance; the retained final eye is
`(10200, -6800, 40.5)`, yaw 170. Both renderers capture mapped, stencil, and
shadows-disabled controls in the pipe/bunker view. The earlier benchmark's
frame-rate samples are historical only; current diagnostic runs are not timed
performance comparisons.

## Random-map coverage

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --random-maps 4 --seed 20260905 --output .tmp/shadow-random-review
```

The runner samples without replacement from the sorted stock SP launch catalog,
records the complete pool and chosen maps in `selection.json`, and uses the
repository's mode-specific launch configurations. It also validates the requested
renderer module and completion of the mapped/stencil/unshadowed capture sequence.
`--maps` accepts stock SP catalog names plus the existing fixed-view Q4DM1/Q4DM9
profiles. Reusing an output directory is rejected.

Seed `20260905` selected these four maps from 29 candidates:

| Map | Exercise |
| --- | --- |
| Network 2 | Interior pipes, point lights, cutout surfaces, and characters |
| Dispersal | Outdoor passage, a point light, and a parallel light with four cascades |
| Building B | Interior door/character view, point lights, and projected cascades |
| Recomp | Dim opening with point and cutout casters, followed by the separate steady-light decal fixture |

The Recomp opening alone is limited shadow-quality evidence because much of it
is dark. A camera/flashlight exploration remained dark and was not accepted as
flashlight coverage. The diagnostic point-light fixture supplies a clear view of
its receiving walls, floor grates, and pipes. It is identified separately from
the map's authored lighting.

## Validation and evidence

The current-source validation record is retained under
`.tmp/shadow-final-20260905/`, including launch commands, scripts, logs, engine
captures, image metrics, and review sheets. Before/after Recomp captures and the
translucent-stencil-off control remain separate from the accepted fixed runs.

The prior 14-run transition/cutout/lift matrix and eight-run door matrix were
revalidated against the stricter capture inspector: **442 retained engine
captures pass**. These remain historical evidence for those repairs; final
material-policy qualification is recorded separately. Source
checks that required signature-agnostic cache reuse and obsolete fail-closed
wording were corrected to enforce exact caster signatures and the documented
fallback behavior.

The final material-policy set passes **15 runs / 69 engine captures**:

| Retained directory under `.tmp/shadow-final-20260905/` | Runs | Captures | Result |
| --- | ---: | ---: | --- |
| `random-after` | 8 | 24 | Four seeded stock SP maps, both APIs |
| `airdefense-after` | 2 | 6 | Original Air Defense 1 camera, both APIs |
| `recomp-lit-after` | 2 | 6 | Explicit decal exclusion, both APIs |
| `cutout-settled` | 2 | 30 | Cutout insertion/removal and projected cache reuse, both APIs |
| `moments-after` | 1 | 3 | OpenGL Medlabs with translucent moments enabled |

Every accepted case exits successfully, confirms its renderer and windowed mode,
completes its capture sequence, and passes strict missing-caster/resource/API
failure checks. All 69 images decode at 960×540 and were visually reviewed.
`final-evidence.py` re-inspects these runs, records image comparisons and engine
self-test output in `final-evidence.json`, and verifies matching SHA-256 hashes
for the six built/staged engine, renderer, and game-module binaries.

The initial cutout rerun retained four transient opening casters on Vulkan and
failed the opening count gate. Its later phases already had the exact required
30 opaque plus 24, 12, then zero alpha casters. The fixture now advances three
seconds of simulation before freezing its empty reference; the count gate was
not relaxed. Both settled reruns pass. Cached/fresh/repeated-fresh RGB images are
byte-identical in the twelve-, six-, and removed-caster phases on both APIs;
the OpenGL empty pair differs by one color unit in one channel of one pixel.
The retained non-casting cutout material is still rejected as a negative control.

Medlabs reports the translucent-moment option enabled with no eligible moment
casters in this view. This run verifies enabled-option resource cleanup and the
parser/admission self-tests, not the appearance of transmitted colored shadows.
The darker opening also limits its visual shadow-quality coverage.

The full Windows x64 Debug compile and staging completed successfully. A final
incremental compile/stage and `openq4-renderer-contracts` run pass. Source checks
pass for Vulkan shadows, Vulkan world interactions, the classic interaction
domain, macOS shadow policy, shared PBR/material declarations, and companion
presentation interpolation. Native mathematical tests and source policy checks
do not substitute for platform-specific driver qualification.

## Remaining limits and unrelated findings

- Mapping remains optional. Unsupported material/receiver paths and real
  resource limits retain stencil fallback; this review does not remove it.
- Point maps with live casters still lack static/live composition. Translucent
  moments remain experimental on OpenGL. Windows runs do not qualify Linux,
  Apple GL, or MoltenVK drivers, long campaign playthroughs, or continuous-motion
  shimmer. Dense-view frame-rate budgets remain a separate qualification task.
- Network 2 and Recomp still show lighting differences between OpenGL and Vulkan
  with shadows disabled. Earlier Storage 1 corpse-effect and Q4DM9 ambient/wall
  differences also remain outside the shadow fixes. The stock-map captures are
  not pixel-parity gates: material shading and animated lighting/effects still
  contribute to mapped/stencil image differences, especially on OpenGL.
- Stock warnings about embedded rigid bodies/items, unhidden AI, speaker/AAS
  data, and non-pre-cached assets remain. Vulkan's legacy vertex-array warning
  remains. No retail assets were changed to suppress these diagnostics.
