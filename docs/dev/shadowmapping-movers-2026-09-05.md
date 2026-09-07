# Shadow mapping with doors and movers — 2026-09-05

Fourth round following the [replacement progress audit](shadowmapping-replacement-progress-2026-09-05.md).

## Fixes

### A budget miss could retain a door's old position

OpenGL's scheduler first checked exact cache signatures, then performed a
signature-agnostic lookup when update admission or subview policy denied fresh
work. A stationary door can belong to cached static depth; when it starts moving,
its removal from the static caster set changes the signature. Reusing the old
depth and composing its current pose would retain the old blocker too. A change
in portal connectivity can similarly introduce or remove casters.

Scheduling now accepts only exact hits, matching Vulkan. A denied miss uses
complete current stencil coverage. If that coverage is incomplete, the existing
required-map rule overrides the budget. Compatible point-allocation history is
used only to score update priority. The unused projected stale-sampling lookup
was removed.

This defect was established by inspecting the scheduling and caster-membership
paths. The initial full-scene budget probe did **not** reproduce a stale image:
required maps already overrode its budget. Its cached/fresh and fresh/repeat
images matched exactly. This distinction matters when interpreting the evidence.

### Two-pass lights could starve under a budget of one

Both backends scored lights before drawing, but rejected any candidate whose
complete cost exceeded the remaining budget. A light needing distinct LOCAL and
GLOBAL maps costs two updates and could therefore never be admitted with a limit
of one. The highest-priority candidate can now make partial progress. Actual
per-pass scheduling retains the limit, and a retained first map lets the second
ownership update on a subsequent view. Lower-priority candidates still need to
fit the remaining reservation.

### A stock door view exceeded Vulkan's light table

The first full-scene Vulkan door run emitted a required-resource failure for
light 126, `lights/round`: it had complete map coverage but incomplete stencil
coverage and could not enter the 64-light table. Sticky stencil recovery could
only help a later view, leaving the affected first view unshadowed.

The table now supports 256 shadowing lights. Its two-ownership scratch cubemap
pool, descriptor reservation, and update-admission bound stay coordinated.
Images are allocated lazily; this change increases metadata/descriptor capacity,
not every map's up-front cubemap image allocation. The bounded overflow path now
describes its same-view limitations accurately. Hardware and memory exhaustion
are still real limits, and the projected atlas retains its existing bound.

### Paused movers still changed their rendered pose

The focused opening test failed on both renderers after the cache fixes. Door
physics stayed fixed, but the cached/fresh image errors were 0.9392 (GL) and
0.5411 (Vulkan), while repeated fresh captures matched exactly. The difference
included the door leaves themselves, not only their projected shadows.

The presentation clock re-anchored after four real-time tics without a simulation
update, restarting interpolation between the previous and current mover samples.
With `g_stopTime 1`, this repeatedly moved a frozen door backward by up to one
tic of travel and churned its shadow cache. Single-player stop-time now holds
the authoritative interpolation fraction and material clock. The shared SP/MP
implementation retains its parity contract; the guard leaves multiplayer
presentation unchanged. The fixture requires 24 paused draw samples at one game
time with fraction 1, in addition to its image and physics checks.

### Leaving a light's area could crash Vulkan shadow diagnostics

The isolated point-light run crashed after the camera moved away from the door.
The dump traced the invalid read through `VK_ShadowMap_ReportViewLights` into
light classification. Shadow preparation had been skipped for the empty light
view, but the frame allocator reused the previous view's address. A pointer-only
identity check therefore exposed the previous frame's recycled light records to
the report/overlay hook.

Prepared Vulkan shadow metadata now carries its frame number. Light lookup,
reporting, and overlay selection reject expired records even when the view
address matches. This guards transient view data without discarding resident
shadow maps. The point-light fixture now enables the overlay in the away view
and captures it before returning, covering the previously crashing path.

## Test method

`renderer_shadow_mapping_maps.py` uses the repository's SP launch configuration,
retail PK4s, isolated settings, and a bordered 960×540 window. The new profiles in
`renderer_shadow_mapping_movers.py` drive actual `idDoor` teams using `testDoor`
in the companion GameLib repository. The command is SP-only and cheat-gated. It
calls normal Open/Close logic and reports each leaf's state, position, velocity,
portal handle, and portal blocking flags. No OS input or screen capture is used.
Canonical game changes are in `openQ4-game`: `src/game/Mover.h`,
`src/game/gamesys/SysCmds.cpp`, and the matching SP/MP `Game_local.cpp`
presentation-clock methods.

| Scenario | Coverage |
| --- | --- |
| `door-portal` | Retail `func_door_63` / `func_door_64`, portal 26, full surrounding light set |
| `door-projected` | Paired stock door models under the actual weapon flashlight |
| `door-point` | The same team under isolated stock point light 168, plus an empty-light view with the debug overlay enabled |
| `door-budget` | The paired team with a discretionary update budget of one |

Each profile captures closed, opening, reversed motion, reclosed, fully open, and
closed-after-return poses. Every pose has cached, fresh, repeated-fresh, and
shadows-disabled engine screenshots. Two additional captures occur during actual
simulation before freezing. The normal mapped/stencil/unshadowed triplet finishes
each run: 29 screenshots per API/profile, or 30 for the point-light profile's
additional away-view overlay capture.

Fixed simulation tics make motion phases repeatable. Diagnostics must confirm
both moving leaves, actual displacement, endpoint restoration, and—for the
retail door—portal blocking 7 when closed and 0 while opening/open/closing.
Image checks compare cached/fresh error against the repeated-fresh control plus
0.25 in 8-bit colour units, and require a visible difference when shadows are
disabled. Each comparison needs intervening light and leaf diagnostics; an older
mapped result cannot qualify a later capture. The stationary flashlight must
also demonstrate resident cache reuse. These are regression gates, supplemented
by visual inspection.

The first GL full-scene run exceeded the old 180-second harness timeout before
finishing all phases. Its incomplete captures are retained, not counted as a
pass. The runner now exposes `--timeout`; the full-scene rerun permits 360 seconds
and uses fewer redundant settling frames. The first Vulkan run completed all
captures but correctly failed on the resource warning above.

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --scenario door-portal --timeout 360
python tools/tests/renderer_shadow_mapping_maps.py --scenario door-projected
python tools/tests/renderer_shadow_mapping_maps.py --scenario door-point
python tools/tests/renderer_shadow_mapping_maps.py --scenario door-budget
python tools/tests/renderer_vulkan_shadow_compatibility.py
```

## Validation results

**8/8 final runs passed, with 234 engine screenshots.** Each run entered
`game/airdefense2` using the staged client and canonical SP GameLib. All six
motion phases, both live-motion captures, portal flags, and 24 paused
presentation samples passed their checks. There were no missing-caster,
required-resource, map-render, mask, or Vulkan validation failures.

The table gives the largest cached/fresh mean absolute error across the six
poses, in 8-bit colour units. Counts include both APIs.

| Scenario | Captures | OpenGL maximum error | Vulkan maximum error | Result |
| --- | ---: | ---: | ---: | --- |
| `door-portal` | 58 | 0.221176 | 0.211412 | Pass |
| `door-projected` | 58 | 0.000003 | 0 | Pass |
| `door-point` | 60 | 0 | 0 | Pass |
| `door-budget` | 58 | 0.000003 | 0 | Pass |

The full portal scene retains small image variation, including between repeated
fresh captures (maximum 0.234829 on GL and 0.071800 on Vulkan). Its results pass
the stated per-pose control-plus-tolerance gate; they are not pixel-identical
comparisons. The focused fixtures have zero repeated-fresh error. Every pose
also differs visibly from its shadows-disabled control. All eight contact
sheets and both away-view overlay images were visually reviewed. Magnified GL
door crops keep the leaf silhouettes aligned in the cached/fresh comparison.

Vulkan prepared **68 shadowing lights in a single reported view**, beyond the
former 64-light capacity. The previously crashing empty-light view completes
with a `NO MAP` overlay and then returns to correct door shadowing. Dense views
still perform substantial fresh work; this is correctness coverage, not an FPS
benchmark. The budget fixture protects required shadow coverage under a limit
of one; the separate two-pass admission-starvation defect was established by
source inspection and source contracts, not a before/after runtime witness.

Validation also passed:

- Full Windows compile and release-style staging via `meson_setup.ps1`.
- Native `openq4-renderer-contracts` (1/1).
- `renderer_vulkan_shadow_compatibility.py`, including exact-hit scheduling,
  coordinated light capacity, and frame-arena lifetime checks.
- Companion `presentation_interpolation_contract.py`, including SP/MP parity.
- In-engine shadow-planner and projected-diagnostic self-tests on both APIs.
  GL also ran its caster, receiver-fallback, point-face (960 cases), and cascade
  stability checks.
- `git diff --check` in both repositories.

Evidence is retained under `.tmp/shadow-round4-20260905/`. `final-evidence.json`
revalidates the selected logs/captures and records their metrics. The selected
results are `accepted/door-portal`, `accepted/door-budget`, GL from
`accepted/door-projected`, Vulkan from `accepted-vk/door-projected`, GL from
`accepted-point-overlay/door-point`, and Vulkan from `accepted-vk/door-point`.
Each selected profile has an API-labelled `*-review.jpg` contact sheet.
`away-overlay-review.png` and `portal-gl-difference-review.png` retain the
additional visual checks. `crash-analysis.txt` preserves the failing Vulkan
stack. Build, stage, native-test logs, and `staged-artifacts.json` record the
final verification. Earlier failed runs remain separate and are not counted.

## Limits and unrelated findings

These tests exercise Windows OpenGL and Vulkan, stock sliding door teams, and
their portal lifecycle. They do not qualify all campaign movers, bound rotating
assemblies, save/load transitions, or other platform drivers. Dense portal views
can exceed the small resident cache and require expensive fresh point maps;
there is no general performance or full stencil-removal claim.

Existing stock path-case and Vulkan legacy vertex-array warnings remain. Earlier
Storage 1 corpse-effect and Q4DM9 ambient/wall differences are outside this round.
