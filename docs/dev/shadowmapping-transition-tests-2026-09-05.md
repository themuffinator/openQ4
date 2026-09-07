# Shadow-map transition tests — 2026-09-05

Second round following the [individual stock-map tests](shadowmapping-map-tests-2026-09-05.md).
This round exercises settings and scene changes within gameplay, using installed
retail assets, isolated save directories, bordered 960×540 windows, and the
engine `screenshot` command. No keyboard/mouse input is injected. Validation
uses Windows x64 Debug builds on the NVIDIA RTX 4060 Laptop GPU.

## Repair: disabling shadows latched fallback state

Q4DM9's parallel light 179 reported a missing Marine helmet caster immediately
after switching `r_shadows` to zero. The interaction's cached stencil eligibility
remained true while depth-map admission was deliberately disabled. The fallback
bookkeeping interpreted that empty map chain as failed admission and set
`shadowMapStencilFallbackSticky` on the light. This flag persists after shadows
are enabled again and prevents normal stencil-volume elision.

`idInteraction::AddActiveInteraction` now activates map admission and completeness
bookkeeping only when both `r_shadows` and `r_useShadowMap` are enabled. Intentional
shadows-off frames cannot poison a light's fallback state. The existing
compatibility regression checks this gate. The runtime transition profiles now
also fail on unexpected `SM missing caster` reports; ordinary stock-map tests
retain those reports separately because some authored surfaces legitimately
require stencil ownership.

The failing run is retained in `cascade-motion-visible/`; its diagnostic names
entity 240, `_MD5_Snapshot_`, `models/characters/marine/helmet_bright_mp`, with
`admitted=0 eligible=1 volume=0`. It occurs between the first fresh and
unshadowed captures on both APIs. This establishes the invalid state transition;
it does not imply that every subsequent receiver visibly rendered with stencil.

After rebuilding and staging both renderer modules and the dedicated target,
the same 44-capture courtyard sequence passed on OpenGL and Vulkan in
`cascade-motion-final/`. Neither run reports a missing caster or receiver/budget
fallback. The courtyard shadows remain visible through every off/on cycle.

## Retained scenarios

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --scenario flashlight-cycle --output .tmp/shadow-flashlight-cycle
python tools/tests/renderer_shadow_mapping_maps.py --scenario resource-cycle --output .tmp/shadow-resource-cycle
python tools/tests/renderer_shadow_mapping_maps.py --scenario caster-cycle --output .tmp/shadow-caster-cycle
python tools/tests/renderer_shadow_mapping_maps.py --scenario caster-cycle-point --output .tmp/shadow-point-cycle
python tools/tests/renderer_shadow_mapping_maps.py --scenario cascade-motion --output .tmp/shadow-cascade-motion
```

Each command runs OpenGL and Vulkan sequentially. `--apis gl` or `--apis vulkan`
selects one renderer. Use a new output directory. The runner selects the correct
SP/MP launch configuration, enters gameplay, and preserves exact launch commands,
console scripts, logs, captures, and `results.json`. `--dry-run` only writes the
configuration. No custom maps or replacement materials are installed.

| Scenario | Map | Exercise | Captures per API |
| --- | --- | --- | ---: |
| `flashlight-cycle` | Air Defense 2 | Actual weapon flashlight off/on, cache disabled with an update budget of one, engine camera turn/return, then another off/on cycle | 10 |
| `resource-cycle` | Air Defense 2 | Point sizes 128/1024/512, projected sizes 256/1024, manual/hardware comparison, translucent moments on/off, cache off/on | 12 |
| `caster-cycle` | Air Defense 2 | Insert 12 stacked stock crates, remove the upper six, then the remaining six; all scene lights and the actual flashlight remain active | 15 |
| `caster-cycle-point` | Air Defense 2 | Same geometry transitions with steady point light 168 isolated; repeated uncached captures control for scene animation | 15 |
| `cascade-motion` | Q4DM9 | Isolated parallel light 179, four cascades, sub-unit translation, larger translation, turn, revisit, coverage/blend changes, and shadows-off controls | 22 |

Counts include the concluding mapped/stencil/unshadowed reference triplet.
The caster profiles capture each phase with cache enabled, disabled, and still
disabled. The isolated point light can contain dynamic casters and does not
guarantee a resident static-cache hit. Actual cache reuse is checked in the
full-scene flashlight and cascade diagnostics.

The initial Q4DM9 view reports static cascade reuse with dynamic composition.
Courtyard views can introduce cutout casters that correctly require fresh
scratch maps even with caching enabled; the motion sequence does not guarantee
a cache hit at every pose.

The cascade profile uses visible receiving floors around (1000, -1024, 450),
then restores the standard multiplayer comparison view. Its initial placement
near (1528, -384, 452.25) was too dark with this light isolated and was rejected
as visual coverage despite clean allocation/cache logs. Shadows-off courtyard
captures confirm substantial visible occlusion in the retained view. Multiplayer
explicitly enables `ui_autoJoin 1` and `net_allowCheats 1`.

## Diagnosis and interpretation

- The flashlight's caster set grows from 30 to 54 surfaces when the 12 crates
  are added, falls to 42 after removing the upper layer, and returns to 30 after
  removing all crates. Cached and regenerated captures show the same geometry
  and shadow coverage; no residual crate shadows were observed.
- Full-scene cached/uncached images are not byte-identical. The corridor contains
  sound-reactive and animated lighting, effects, and an animated GUI. Holding
  sound amplitude constant alone did not remove every difference. Repeated
  uncached controls reproduce the variation: for example, Vulkan's initial
  full-scene comparison and its uncached repeat both have RMS 2.132 and 5,059
  pixels differing by more than 12 RGB levels. GL's six-crate comparisons both
  have RMS 1.214 and 2,037 such pixels. These differences are not evidence of
  stale cache content.
- The isolated point-light comparisons reduce variation to at most 74 pixels
  over that threshold, with RMS below 0.4. Repeated uncached captures show the
  same small variation. The animated GUI remains visible in this isolation.
- Q4DM9 keeps simulating in multiplayer; pickups and effects move between
  captures. Compare static receiver shadow boundaries rather than treating
  whole-frame pixel identity as the acceptance criterion. These are discrete
  engine camera changes, not a continuous-motion shimmer or performance test.
- Resource transitions exercised the previous point-depth and GL moment-sampler
  repairs without texture-state, allocation, or Vulkan validation errors. Setting
  the moments CVar on Vulkan does not establish parity with GL's experimental
  translucent-moment implementation.

## Evidence and limits

The selected evidence comprises **10 successful scenario/API runs and 148 engine
captures**, with zero unexpected missing-caster, resource, texture-state, or
Vulkan validation failures. The final cascade pair was rerun after the fallback
repair; the other eight runs supplied the preceding transition investigation.
`final-evidence.json` rechecks all selected runs with the stricter missing-caster
gate, including every scenario capture rather than only the final triplet.

MSVC compilation and release-style staging passed. Vulkan shadow compatibility,
world-interaction compatibility, classic interaction-domain, macOS shadow-policy,
Python compilation, and whitespace checks passed. The Mac policy check is a
source regression, not Apple-hardware rendering validation.

Local artifacts are under `.tmp/shadow-round2-20260905/`, including contact sheets,
image-comparison JSON, per-run results, and logs. Images were visually reviewed;
successful allocation alone is not treated as correct rendering.

An early MP script incorrectly used SP-only post-processing CVars. The runner
rejected both runs; those commands are now omitted from the MP profile. One GL
sound-amplitude isolation run ended with exit code -1 before completing its
captures, without an engine error or Windows application-crash event. Its
complete retry passed; the incomplete run is retained and excluded from passing
evidence. No cause for that termination was established.

Unrelated stock path-case warnings and multiplayer missing AAS/sound and
non-precache warnings remain in the logs. Vulkan's legacy vertex-array warning
also remains. The earlier Storage 1 green corpse-effect discrepancy was outside
these two maps and has not been resolved by this work. Linux/macOS driver
behavior, long playthroughs, and performance remain unvalidated by this round.

The [next replacement review](shadowmapping-replacement-progress-2026-09-05.md)
adds lift and static-cutout fixtures, repairs GL cache eviction by 2D views, and
allows live cutouts over cached opaque projected depth. It supersedes this round's
cutout scratch-map limitation and separately checks actual cache reuse.
