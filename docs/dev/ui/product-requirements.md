# UI product requirement register

9 September 2026. The [machine-readable register](product-requirements.json)
contains **227 mandatory requirements** for the complete UI product. It expands
the [completion plan](../plans/ui-product-completion.md), the
[original delivery scope](../plans/idtech5-ui.md) and the normative
[visual specification](../ui-visual-design.md) into implementation ownership,
dependencies, concrete evidence and explicit unfinished work.

The baseline is engine `a9919d1be2dd9ce43e19ec16bb0392e947ce8576` and companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. The register also identifies the current
[manager/snapshot foundation](instance-persistence.md) as partial
evidence with native and SP/OpenGL/MP/Vulkan validation. It does not treat that work as a
finished adapter, complete widget persistence or game-save compatibility.

There are **53 partial, 173 pending and one verified requirement**. The verified
requirement, `INV-001`, covers the fresh effective-source inventory only. All
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
| `GOV-001`–`008` | Complete scope, source authority, requirement maintenance, canonical ownership, dependencies and publication decisions | M0–M6 |
| `INV-001`–`013` | Fresh effective corpus, native provenance, expression/name/time semantics, full lowering, assets and special widgets | M0, M1, M5 |
| `DOC-001`–`008` | Canonical versioned model, components/variants, structural transactions, behavior and binding ownership | M1–M3 |
| `RUN-001`–`008` | Public retained adapter, neutral manager, source routing, independent views, application frame order and restart | M1, M6 |
| `BEH-001`–`007` | Typed operations, settings transactions, aliases/events, exactly-once actions/sounds, pause and MP authority | M1, M2, M4 |
| `SUR-001`–`006` | Explicit output surfaces, GPU lifetime, world density/rays, HUD/aim projection and output-resolution UI | M1, M5, M6 |
| `SAVE-001`–`004` | Versioned instance/game-save policy, full durable state and live/level restoration | M1, M5 |
| `LAY-001`–`012` | Exact scale/spacing/target-size values, aspect expansion, compact recovery, HUD safe areas and contrast | M1–M5 |
| `TXT-001`–`009` | Scalable typography, shaped runs, Unicode, localization, caret/selection/clipboard/IME and platform accessibility | M2, M3 |
| `INP-001`–`008` | Input transforms, paired activation, repeat/handoff, wheel/touch/text, modal focus and authored mask-aware hits | M1, M3 |
| `WID-001`–`016` | Every named functional widget: actions, checkbox, radio, slider, choice, text, binding, tabs, lists, scrolling, progress, tooltip, modal, status, tree and path/color controls | M2, M3 |
| `REN-001`–`013` | Editable paths/paints/strokes/SVG, coverage/color, isolated opacity/masks/clips, composition and recovery | M3, M6 |
| `RES-001`–`005` | Full material/movie/model operations, generated images and actual renderer resource accounting | M3, M4 |
| `ART-001`–`017` | Measured component/icon libraries, exact visual tokens, all eight distinct families and complex-image exceptions | M2–M5 |
| `MOT-001`–`008` | Continuous clocks, normative timing/easing, reversal/ownership, page/modal orchestration, sound and reduced motion | M1–M3, M6 |
| `FLOW-001`–`018` | Every menu/MP flow, HUD, communications, scope/vehicle, cinematic and scripted/world GUI | M2, M4, M5 |
| `ED-001`–`030` | Native workspace, canvas/constraints, components, vectors, motion/behavior, persistence/recovery, diagnostics and complete SP/MP delivery | M2, M3 |
| `PERF-001`–`008` | Named optimized baselines, separate CPU/GPU telemetry, warm/cold runs, budgets, long-run plateau and measured optimization | M3, M6 |
| `QUAL-001`–`009` | Display/text/input/refresh/language/world extremes, independent visual review, actual platforms and evidence provenance | M6 |
| `SHIP-001`–`006` | Complete retained routing, external-mod policy, retail-asset staging, artifact hygiene, dedicated builds and documentation | M1, M6 |
| `GATE-001`–`007` | The seven milestone exit conditions | M0–M6 |
| `FINAL-001`–`007` | Each numbered final product audit item | M6 |

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
this task did not run a game or remeasure them.
