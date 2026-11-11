# Modern UI audit: masked input and settings messages

17 September 2026. Reviewed engine baseline `85c01175` on `idtech5-ui`, paired
with companion `a4946f8b28c53bb32ec323bddcf0e2276bc8492c`. The companion is unchanged
by this work. This review applies the [complete product plan](../archive/2026-10-18/plans/ui-product-completion.md)
and [visual specification](../ui-visual-design.md); it does not accept a milestone.

## Current gaps and delivery priorities

The register still contains 227 requirements: 66 partial, 160 pending and one
verified. All 271 migration records and all seven final gates remain unaccepted.
The source tree contains one production retained (`.rui`) document, SYSTEM, reached through
the default-off `ui_retainedSystem` route. Historical foundation reports describe
bounded increments; they are not evidence of complete replacement.

| Area | Current assessment | Required next delivery |
| --- | --- | --- |
| M1 application and instances | Manager-owned retained documents, semantic events, presentation aliases and snapshots exist. Complete legacy behavior lowering, projected world views and surrounding save/demo compatibility remain open. | Exercise full application and world-surface lifetimes through normal game callers. |
| M2 SYSTEM | Drafts, confirmation, numeric fields, presets, scrolling and constrained choices are implemented in part. Complete mixed-effect Apply/recovery and supported native text activation remain unfinished. | Finish the complete settings flow and source-derived responsive artwork before accepting the page. |
| M2/M3 typography and controls | Six roles exist: button, toggle, slider, choice, number and scrollbar. Independent retained text scale, complete shaping/composition, radio groups, bindings, tables, tabs, tooltips and other required controls remain open. | Add missing behavior with language, input, narrow-layout and large-text evidence. |
| M2/M3 targeting | Rendered alpha masks did not participate in pointer targeting; fully masked controls could retain focus. This increment repairs that gap below. | Complete arbitrary clip/mask intersection for focus, exact transformed overflow rendering/input and authored hit-region overrides. |
| M2/M3 editor | Source-preserving edits, history, canvas publication and file services exist; the extensive visible `.rui` authoring workspace does not. | Deliver the required edit/path/timeline/localize/bind/save/reopen/package round trip in the native editor. |
| M4/M5 corpus and art | No legacy resource has behavioral and visual replacement acceptance. One SYSTEM page cannot establish menu, HUD, terminal, scope or vehicle parity. | Translate each family using the existing manifest, installed assets and measured vector artwork. |
| M6 quality and performance | Debug tests and bounded Windows captures cannot establish release CPU/GPU budgets or the full platform/display/input matrix. | Measure optimized builds, renderer memory/reclamation, high refresh, extreme scale/aspect combinations and supported platforms before cutover. |

## Defect and repair

`Runtime::HitControl` previously used RmlUi's rectangular hit result without
consulting canonical masks. Text and value-control parts had the same omission.
A transparent hole could activate the obscured foreground control; empty masks
could leave a fully invisible subtree in keyboard navigation. Wheel routing
could reach a masked-out scrolling body.

The runtime now filters candidates in the existing RmlUi stacking traversal,
checking live visibility/pointer eligibility and every actual ancestor's vector
mask. Owning controls retain their semantic eligibility checks at dispatch.
Rejected elements no longer occlude eligible elements behind them. Existing modal
ownership still prevents background activation through a modal hole. Buttons,
compound widget parts and body wheel routing use this same query.

Mask coverage comes from the existing path compiler: fill rules, holes, strokes,
gradient alpha and responsive coordinates retain their canonical meaning. Queries
use the projected border box and the same 0.025 output-pixel curve tolerance as
mask rendering. Cached geometric meshes exclude antialiasing fringes. Their keys
include dimensions and the affine transform; translation is applied at query
time. Source/artwork replacement invalidates the cache. Empty, degenerate or
fully transparent masks exclude the entire subtree from focus and input.

The explicit interaction policy is positive authored geometric alpha: partial
transparency stays interactive; zero alpha and holes do not. Mask RGB is irrelevant.
The control's existing ergonomic border box remains the target, constrained by
masks; decorative ink does not enlarge it. A matched drag started in a visible
slider/scrollbar region retains capture when moving outside the mask.

The small [RmlUi patch](../../../subprojects/packagefiles/rmlui/masked-hit-test.patch)
adds an optional candidate predicate to its existing hit traversal. Ordinary RML
calls preserve their behavior. The dependency stays pinned at 6.3, under its
compatible MIT license, with existing attribution and patch provenance retained.

## Verification

The review also found an incomplete localization merge: eight settings status
entries moved from `229982`–`229989` to `230007`–`230014`, but the production
service, number-field generator and fixtures still used the old IDs. The old
Ready ID had become Weapon Kick; other status IDs had no translation. The service,
generators and validation fixtures now use the existing translated entries.
An integration check verifies that every service status key resolves exactly once
in every shipped locale and that the page and service agree on Ready. No new
translations or runtime content overrides are introduced.

`openq4-ui-mask-input` runs actual Runtime/RmlUi layout and interaction with
authored controls. It checks overlapping controls and text, nested masks,
90-degree rotation, translation, 100/125/150/200% density, viewport origins and
different window pixel densities, empty/transparent masks, modal ownership,
gradient and stroke coverage, slider capture, popup selection, body scrolling
and reuse of unchanged hit geometry. It does not query or manipulate a device.

The final Windows x64 MSVC debug build and staging completed. All 83 registered
UI suites passed (78 in `final-tests.log` and five in `targeted-tests.log`),
including 3,638 masked-input checks. The settings service's 90 compiled-body
scenarios passed. SYSTEM source checks passed 14 tests; the capture oracle
passed 18 tests and rejected 1,224 negative mutations.

The final staged binary ran the normal SYSTEM route after actual SP
`game/airdefense1` gameplay on OpenGL at 125% density and MP `mp/q4dm1` gameplay
on Vulkan at 200% density. Both 1280x720 runs passed the state/readback oracle,
including Apply, Discard, popup selection, full renderer restart, return and
reopen. All 18 engine screenshots were visually reviewed: localized status,
focus, modal actions, popup clipping and scrolling remained readable at these
two sampled layouts. SP had no warnings or errors; MP had 94 warnings, discussed
below, and no errors. Runs were hidden/windowed, with host input disabled.

Earlier captures in this same increment separately exercised the existing mask
renderer after gameplay and a renderer restart (SP/OpenGL 1280x720 at 125%,
MP/Vulkan 1920x1080 at 200%). The independent pixel oracle checked 558,552 pixels,
including 65,752 fully hidden pixels, with zero hidden-pixel leaks and no channel
error above five. These captures predate the final popup-visibility and settings
message repairs; their binary hashes remain distinct from the final SYSTEM
runs. They qualify the unchanged mask rendering, not final input behavior.

Exact commands, source/binary hashes, logs, screenshots and review outcomes are
retained under `.tmp/ui/masked-input-review/`, summarized in
`validation-evidence.json`. RmlUi's patch was also applied to pristine pinned
source and compared with the compiled source. These bounded results do not
establish optimized performance, physical-device behavior or full UI acceptance.

The requirement-register structural/source audit also passed with all 227 IDs,
statuses, constraints, migration records and gates preserved. Three historical
capture-summary references still lack recorded hash bindings (instance
persistence, managed applications and presentation aliases). They remain
explicitly unbound; this review does not retroactively qualify those captures.

The SYSTEM capture test data now records measured choice-scroll geometry and
`offsetDp`, matching the current capture oracle. Native ownership/driver tests
have been reviewed against the pinned SDL 3.4.16 headers: their 31-header include
closure is unchanged from 3.4.10. The changed headers contain an ARM64EC pause
guard, audio documentation, an enum comma fix and the new pen proximity state
field. The test version guards remain strict; the review does not enable native
input or claim physical pen/IME qualification.

## Remaining limits and unrelated findings

`INP-007` remains partial. A nonempty mask can still contain an individual control
entirely inside a hole without removing that control from directional focus;
solving this requires geometric intersection and a deliberate scroll-to-reveal
policy. Arbitrary authored hit shapes, transformed overflow clip rendering,
perspective world surfaces, tiny quantized-alpha edge equivalence and physical
device/IME qualification remain open. The native test's host does not implement
GPU mask composition; existing engine mask captures are a separate visual check.

The preexisting build tree required reconfiguration after changed GameLib inputs
and an SDL update. Meson reported existing subproject include-layout warnings and
MSVC ignored `/release` in configuration probes. The configured nightly iteration
also predates this audit; it was left unchanged. These are build/release follow-ups,
not evidence of UI completion.

MP reports existing missing AAS/navigation data and `mp_buying_givecash`,
non-pre-cached declarations, an `ammo_shotgun_3` entity embedded in solid geometry
and the vertex-array virtual-memory warning. None is a retained-UI error. The
retail PK4s used here are installed under `E:\SteamLibrary\steamapps\common\Quake 4`;
the older C-drive reference in the agent guide is not the usable asset location
on this host.

Automatic approval review rejected cleanup of the temporary pristine patch copy
and test binaries with `blocked by policy`. They remain under
`.tmp/ui/masked-input-review/`; the two initial build logs remain at
`.tmp/ui-mask-input-build.log` and `.tmp/ui-mask-full-build.log`.
