# UI product requirement register

9 September 2026. The [machine-readable register](product-requirements.json)
contains **227 mandatory requirements** for the complete UI product. It expands
the [completion plan](../plans/ui-product-completion.md), the
[original delivery scope](../plans/idtech5-ui.md) and the normative
[visual specification](../ui-visual-design.md) into implementation ownership,
dependencies, concrete evidence and explicit unfinished work.

The current increment starts from engine `1f6bfa556941f85b245aa80d7aa54e481fa71d3c` and companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. The register also identifies the current
[manager/snapshot foundation](instance-persistence.md) as partial
evidence with native and SP/OpenGL/MP/Vulkan validation. It does not treat that work as a
finished adapter, complete widget persistence or game-save compatibility.
The subsequent [application integration checkpoint](managed-application.md)
adds normal retained loading, two typed settings operations and coordinated
engine views. These remain partial evidence for the full application contract.
The [presentation alias checkpoint](presentation-aliases.md) adds explicit shared
metadata and rendered-property aliases, transient/explicit expression ownership,
snapshot version 2 and immediate ancestor visibility/input eligibility. The
checkpoint records native checks, five alias gameplay captures and a sixth
typed-settings regression across Windows SP/OpenGL and MP/Vulkan. These qualify
the authored subset and retain their stated display/platform limitations.
The [event-program checkpoint](event-programs.md) adds canonical ordered
state/presentation/motion programs, conditional and nested calls, immutable typed
action arguments, atomic pending-dictionary/host-state entry and normal GUI
lifecycle delivery. The Session pump delivers completed programs independently
of physical input; queued controls recheck document and input eligibility.
Five native suites, production-method checks and two source/binary-bound Windows
gameplay captures cover this subset: SP/OpenGL at 125% and MP/Vulkan at 200% passed
their event, ownership and outgoing-lifecycle checks and render-target visual
review. The captures include non-replaying save restore, language/video recovery,
independent peers and SP/MP pause behavior. That historical checkpoint's catalog
contains brightness, shadows and dismissal. Full legacy broadcast/timer/native-CVar
semantics, production GUI flows, complete widgets, world/game-save/demo contracts,
physical-device/platform/performance qualification and the native editor remain
open. The immutable local evidence is
`.tmp/ui/events-review/capture-evidence.json`; the separate register audit is
`.tmp/ui/events-review/register-validation.json`.

The [SYSTEM settings increment](system-settings-contract.md) adds the actual
source/control contract, complete 53-field host catalog, bounded transaction core
and normal typed service operations. Owned draft edits, immediate Apply/Cancel/
Defaults, conflict-safe rollback, read-only service state, lifecycle abandonment
and config-write protection have native and production-body test evidence.
That checkpoint's service test passes 15 scenarios; the separate host test covers
the full catalog. At that checkpoint, any changed non-immediate field rejected
the entire Apply batch before writes. Profile expansion, complete production
controls and the native editor remain open. Its gameplay evidence is
recorded at `.tmp/ui/settings-review/capture-evidence.json`:
hidden windowed Windows SP/OpenGL at density 125% and MP/Vulkan at density 200% each pass 168
ordered readbacks, 19 service results and two language/video recoveries. The
source-bound probes and reviewed render-target images qualify this subset only;
they do not perform real device changes or accept the production page. The
structural register audit is `.tmp/ui/settings-review/register-validation.json`.

The [display-device foundation](display-device-contract.md) supplies strict actual
window state, recoverable typed renderer requests, video-lifetime identity
preservation and backend presentation observations. Its diagnostic resizing and
restore probes preserve the open draft through resource reconstruction. The
Windows SP/OpenGL and MP/Vulkan captures each pass eight device observations,
32 ordered settings readbacks and four reviewed render-target images, including
gameplay after failure and restoration. Actual interval and GL sample changes
remain independent of stored preferences; an additional SP/OpenGL run verifies
gameplay with a four-sample default framebuffer and its restoration. Seven native UI suites, five production
method harnesses and the 23-test capture oracle qualify this subset. The immutable
record is `.tmp/ui/display-review/capture-policy-evidence.json`; its structural register
audit is `.tmp/ui/display-review/register-validation.json`. The
full production settings page, visible geometry behavior and platform
qualification remained open at that checkpoint.

The subsequent [display confirmation implementation](display-confirmation.md)
adds queued Apply/Keep/Revert/Retry, durable Pending/Confirmed recovery records,
checked atomic configuration writes, native settings/window-placement leases and
strict first-device startup recovery. Confirmation requires an eligible owning
view and successful presentation; request identities prevent stale actions from
affecting a later draft. The new native and gameplay qualification is recorded
separately from the immutable display-device checkpoint. This work remains
partial evidence for `BEH-002` and `FLOW-002`; it does not accept the production
SYSTEM screen, audio/resource effects, preset expansion, the native editor or any
of the 271 GUI migrations.

Its immutable local checkpoint is
`.tmp/ui/confirmation-review/capture-evidence.json`, SHA-256
`1f3e66f316a689ccaae2bf0c1e08c322deb296a0539852047a8a0c9ef0b00521`.
Two inspected display probes and four two-process recovery cases passed on
hidden windowed Windows SP/OpenGL and MP/Vulkan. The approved recovery cases
explicitly inject a Confirmed marker into a real Pending journal; they do not
qualify an actual interrupted Keep or power cut. The checkpoint binds the
tested source/binaries, native checks, configurations, journal bytes, logs and
engine screenshots. Its register audit is
`.tmp/ui/confirmation-review/register-validation.json`. Requirement statuses,
acceptance criteria and migration entries remain unchanged.

There are **60 partial, 166 pending and one verified requirement**. `BEH-002`
is now partial for the implemented settings transaction/service boundary.
`BEH-005` remains partial for committed action delivery and cancellation; stock
sounds and the complete device/modal/widget behavior still require implementation
and evidence. `FLOW-002` remains pending for the complete production SYSTEM page.
The verified requirement, `INV-001`, covers the fresh effective-source inventory only. All
seven final product gates and all **271 migration entries remain unaccepted**.
These counts describe evidence state, not percentage of implementation effort
or product quality.

## Using and maintaining the register

Each requirement has a permanent `GROUP-NNN` ID, normative section references,
an accountable milestone, a responsible implementation workstream, dependencies,
acceptance evidence needed, current evidence and remaining work. Workstream names
assign engineering responsibility; they do not invent individual assignees.

Keep IDs stable. Append requirements when new source behavior or an omitted
normative detail is discovered. If a requirement is split or superseded, preserve
its ID and explicitly link its successors. The specification remains normative;
an omission in this register cannot authorize reducing scope.

Dependencies identify capabilities needed at the stated scope. For example,
the first settings screen needs the applicable controls and source references
before the entire widget or family library is finished. Its narrow evidence may
pass the intermediate screen gate; it cannot mark the broader shared requirement
verified. **Final product acceptance requires every register requirement**, not
only the dependencies reachable from a selected gate.

`current_evidence` can describe limited implementation, historical checks or a
source audit. It is not automatically acceptance. `acceptance_evidence` is empty
until direct, current evidence proves every part of the individual requirement.
An evidence record names scope, revisions, source/asset hashes, exact operations,
builds/binaries where applicable, host/backend, logs/images, outcome, review and
limitations. Relevant changes require revalidation; a stale hash or unrelated
green test cannot close a requirement.

The [migration manifest](migration-manifest.json) remains the per-resource source
of replacement, behavior and visual acceptance. This register covers shared
capabilities and product gates; it neither duplicates nor overwrites acceptance
for an individual GUI/include. Includes may become components, but still need
complete source-bound mappings and evidence.

## Requirement index

| IDs | Required area | Main accountable milestones |
| --- | --- | --- |
| `GOV-001`â€“`008` | Complete scope, source authority, requirement maintenance, canonical ownership, dependencies and publication decisions | M0â€“M6 |
| `INV-001`â€“`013` | Fresh effective corpus, native provenance, expression/name/time semantics, full lowering, assets and special widgets | M0, M1, M5 |
| `DOC-001`â€“`008` | Canonical versioned model, components/variants, structural transactions, behavior and binding ownership | M1â€“M3 |
| `RUN-001`â€“`008` | Public retained adapter, neutral manager, source routing, independent views, application frame order and restart | M1, M6 |
| `BEH-001`â€“`007` | Typed operations, settings transactions, aliases/events, exactly-once actions/sounds, pause and MP authority | M1, M2, M4 |
| `SUR-001`â€“`006` | Explicit output surfaces, GPU lifetime, world density/rays, HUD/aim projection and output-resolution UI | M1, M5, M6 |
| `SAVE-001`â€“`004` | Versioned instance/game-save policy, full durable state and live/level restoration | M1, M5 |
| `LAY-001`â€“`012` | Exact scale/spacing/target-size values, aspect expansion, compact recovery, HUD safe areas and contrast | M1â€“M5 |
| `TXT-001`â€“`009` | Scalable typography, shaped runs, Unicode, localization, caret/selection/clipboard/IME and platform accessibility | M2, M3 |
| `INP-001`â€“`008` | Input transforms, paired activation, repeat/handoff, wheel/touch/text, modal focus and authored mask-aware hits | M1, M3 |
| `WID-001`â€“`016` | Every named functional widget: actions, checkbox, radio, slider, choice, text, binding, tabs, lists, scrolling, progress, tooltip, modal, status, tree and path/color controls | M2, M3 |
| `REN-001`â€“`013` | Editable paths/paints/strokes/SVG, coverage/color, isolated opacity/masks/clips, composition and recovery | M3, M6 |
| `RES-001`â€“`005` | Full material/movie/model operations, generated images and actual renderer resource accounting | M3, M4 |
| `ART-001`â€“`017` | Measured component/icon libraries, exact visual tokens, all eight distinct families and complex-image exceptions | M2â€“M5 |
| `MOT-001`â€“`008` | Continuous clocks, normative timing/easing, reversal/ownership, page/modal orchestration, sound and reduced motion | M1â€“M3, M6 |
| `FLOW-001`â€“`018` | Every menu/MP flow, HUD, communications, scope/vehicle, cinematic and scripted/world GUI | M2, M4, M5 |
| `ED-001`â€“`030` | Native workspace, canvas/constraints, components, vectors, motion/behavior, persistence/recovery, diagnostics and complete SP/MP delivery | M2, M3 |
| `PERF-001`â€“`008` | Named optimized baselines, separate CPU/GPU telemetry, warm/cold runs, budgets, long-run plateau and measured optimization | M3, M6 |
| `QUAL-001`â€“`009` | Display/text/input/refresh/language/world extremes, independent visual review, actual platforms and evidence provenance | M6 |
| `SHIP-001`â€“`006` | Complete retained routing, external-mod policy, retail-asset staging, artifact hygiene, dedicated builds and documentation | M1, M6 |
| `GATE-001`â€“`007` | The seven milestone exit conditions | M0â€“M6 |
| `FINAL-001`â€“`007` | Each numbered final product audit item | M6 |

Numeric `constraints` retain the specification's UI/text scales, dimensions,
spacing, typography, palette/alpha, panel/button/icon geometry, input repeat,
motion tokens and proposed performance limits. Family/source exceptions require
documented rationale and review; numerical tokens do not replace composition
or visual fidelity review.

The JSON `audit_findings` map explicitly connects all twelve implementation
findings (`UI-01` through `UI-12`) to required resolutions. `FINAL-001` through
`FINAL-007` preserve the complete corpus/behavior, visual product, editor,
display/accessibility/lifetime, performance/platform, clean installation and
documentation/publication gates respectively. None is satisfied by a prototype
or conversion count.

## Effective inventory refresh

The read-only refresh used the same explicit low-to-high retail/openQ4 mounts as
the [inventory method](inventory-report.md), reading installed PK4s in place.
It did not launch the engine, extract retail artwork or modify migration status.

| Comparison against the tracked manifest | Result |
| --- | ---: |
| Effective GUI roots/includes | 271 |
| Added / removed resources | 0 / 0 |
| Changed source content hashes | 0 |
| Changed selected source locations | 0 |
| Mount order | Identical |
| Pending migration entries | 271 |
| Lexical window / event declarations | 14,517 / 8,037 |
| Missing/cyclic include errors | 0 |

Reproduce with a fresh output location:

```powershell
python tools/ui/legacy_inventory.py `
  --mount 'retail=C:\Program Files (x86)\Steam\steamapps\common\Quake 4\q4base' `
  --mount 'openq4-pak0=content/baseoq4/pak0' `
  --mount 'openq4-pak1=content/baseoq4/pak1' `
  --output .tmp/ui/requirements/legacy-inventory-2026-09-09.json `
  --export-requests .tmp/ui/requirements/legacy-export-requests-2026-09-09.json
```

Local evidence includes the full inventory, source/hash export requests,
`inventory-refresh-summary.json` with comparison results and tool/manifest hashes,
and `register-validation.json` under `.tmp/ui/requirements/`. The inventory SHA-256
is `d48708c473d52e140c11143083d8bbf9d2de8b30086c8cbe1221d9a4622caae0`;
the unchanged migration-manifest SHA-256 is
`82120ffe3e07964828b5a05e538c797c99f4de6b4e43f9d58c6121377d45f04a`.

The alias checkpoint's refreshed structural/source-hash check is recorded in
`.tmp/ui/aliases-review/register-validation.json`. It preserves the original
inventory evidence and all requirement acceptance states.

Register checks confirmed unique IDs, valid milestone/source/evidence references,
resolved dependencies with no cycles, all twelve audit-finding mappings and all
seven final audit items. This checks traceability structure, not completeness of
runtime behavior, visual quality or the product. Native preprocessing was not
rerun; its existing [source-bound checkpoint](legacy-import.md) remains historical
evidence with its stated limits.

The same 17 unresolved lexical artwork references and one lexical brace
diagnostic remain. The latter source is consumed by the native importer, so it
is not newly established as a gameplay failure. The two previously recorded
bare-backslash alpha expressions remain explicit semantic work in `INV-010`.
No new unrelated defect was established by this read-only inventory/register
work. Historical MP content warnings remain a separate qualification backlog;
that inventory-only refresh did not run a game. The later renderer probes above
record their own gameplay and warning comparisons.
