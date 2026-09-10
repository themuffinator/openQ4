# UI product requirement register

9 September 2026. The [machine-readable register](product-requirements.json)
contains **227 mandatory requirements** for the complete UI product. It expands
the [completion plan](../plans/ui-product-completion.md), the
[original delivery scope](../plans/idtech5-ui.md) and the normative
[visual specification](../ui-visual-design.md) into implementation ownership,
dependencies, concrete evidence and explicit unfinished work.

An earlier recorded increment starts from engine `17b106daa1827d6e50c786f99ea5970b1e8b91cf` and companion
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

The historical [typed value-control increment](value-controls.md), based on engine
`b3bf279b4347329cd8a45a4533536adc0510e860`, added canonical toggles,
stepped sliders and constrained choices with authoritative typed readback,
immutable invocation-local proposals and authored vector parts. The packaged
`guis/menu/settings/system.q4ui` declares all 53 draft/baseline fields and
presents eight initial immediate image controls. Its default-off,
non-archived `ui_retainedSystem` option uses normal Session ownership and
preserves the exact parent on a clean return. Dirty Back opens a local prompt;
at that checkpoint, discard reset the draft and stayed on the page. The document also
provides the existing eligible Keep/Revert/Retry confirmation structure.

The final Windows build/package stage and 13 native UI suites passed. The
production Session boundary passed 173 checks; the capture runner passed
15 tests and rejected 444 altered-log cases. Two hidden windowed captures after
active gameplay, SP/OpenGL at 125% and MP/Vulkan at 200%, each passed 18 ordered
normal-route stages at 1280x720. All 14 engine render-target images were
reviewed, including the two-column/scrolling-column layouts, popup placement,
dirty dialog, distinct disabled Apply, resource resets, parent return and
reopen. SP had no warnings/errors; MP had 94 baseline warnings and no new warning
messages or errors. This evidence covers semantic integration and the recorded
static views; it does not qualify physical input, complete motion or all
display/platform/language cases.

The immutable record is `.tmp/ui/value-controls-review/capture-evidence.json`,
SHA-256 `2c5c14d4d0a9d0fb8cc8d45dab9af39fdc9e6c4ab017f93a1f6a3e49c3cf1a40`.
It binds 8,778 final build inputs, seven runtime binaries, packaged source and
supporting logs, images and tests. The strict register audit is
`.tmp/ui/value-controls-review/register-validation-final.json`.

That checkpoint did not establish full control, screen or artwork
acceptance. Precise numeric entry and IME, the full
setting inventory and dependent popups, remaining effects and presets,
Defaults/Cancel/discard-and-exit, accessibility, physical-device and display
qualification, component authoring and the native editor round trip remain
required. Historical display-confirmation captures cannot qualify these new
controls or normal entry/return behavior.

The subsequent [SYSTEM controls and transactional exit increment](system-exit.md)
adds seven immediate settings and VSync to the packaged page, bringing it to
16 value controls while preserving all 53 draft/baseline fields. Dirty Back now
offers Apply changes, Discard changes and continue editing. The ordinary Apply
button stays on the page. Apply-and-exit uses a typed service operation and a
one-use owner/request-bound receipt; asynchronous return requires successful
Keep and completed persistence. Revert, timeout, failure and recovery cancel
exit intent, and GUI dictionary values cannot authorize return. Repeated wheel
events no longer replay an unchanged pointer move; actual movement and button
input retain pointer-navigation reclamation. Authored modal scopes establish
safe default focus before drawing, restore prior focus and invalidate stale
queued input across scope replacement and instance reconstruction.

The final Windows build, 15 native suites and four staged normal Session runs
passed. All 34 engine images from 92 semantic stages were reviewed. The local
record `.tmp/ui/system-exit-review/capture-evidence.json` binds final sources,
packages, binaries, tools, logs and images. The
[qualification and limits](system-exit.md#qualification-and-remaining-work)
include localized-label CPU layout checks and outstanding native-resolution
composition/effect-parity findings. Earlier captures retain their original scope.
The complete settings inventory and dependent flows, numeric text entry/IME,
remaining effects and presets, Defaults, full modal/scrollbar behavior,
source-derived artwork/transitions, editor round trips and the complete
platform/language/display/physical-input matrix remain required. No requirement,
milestone, production screen or migration is accepted by this increment.

The subsequent [native-output OpenGL repair](native-output.md) removes the
UI-only swap-tail resolution filter while retaining existing world sizing and
resolves and the explicit legacy mode-0 crop. Its final integrated production-method
test passes 1,220 checks and rejects seven compiled mutations. Separately bound
Windows SP/OpenGL before/after engine images establish zero RGB differences
from the 100% UI reference across eight ordinary-scale cases after the repair.
The preserved baseline demonstrates the former filtering;
`24` engine images were reviewed. `SUR-006`
moves only from pending to partial, with acceptance evidence still empty.
Dynamic resolution, all UI surfaces and input/cinematic/subtitle geometry,
CRT parity, the existing GL mode-3 filtering mismatch and M6 remain open.
Historical evidence retains its original scope and bindings.

The subsequent [text-entry foundation](text-entry-foundation.md) integrates
validated Unicode commits/preedit, bounded local editing, exact numeric parsing
and checked Windows clipboard primitives. Pending event payloads are released
on queue clear/overflow. Native and compiled-method tests and a Windows
SP/OpenGL gameplay/menu smoke qualify this bounded integration. Live fields,
ordered native ownership, IME, shared shaped caret/grapheme/bidi editing and
the complete authoring application remain required. The final local record is
`.tmp/ui/text-input-foundation/validation-evidence.json`; the strict structural
audit is `.tmp/ui/text-input-foundation/register-validation-final.json`.

The [numeric-field increment](numeric-fields.md) adds a canonical exact-entry
model, authored selection/caret/preedit rendering, shared scalar font runs and
bounded inactive draft/history restoration. Fresh host values guard edits,
queued numeric dispatch and accepted acknowledgements. The final Windows build,
21 retained-UI suites, SP/OpenGL at 125% and MP/Vulkan at 200% managed fixture
captures after gameplay, and an ordinary SYSTEM rendering regression qualify
this subset. The journal repair bounds historical-layout payloads and lifetime
cleanup. Its local record is
`.tmp/ui/number-field-integration/validation-evidence.json`; its structural
audit is `.tmp/ui/number-field-integration/register-validation-final.json`.
Native keyboard/clipboard/IME routes, complete shaped/grapheme/bidi editing,
production precise-entry settings, editor round trips and full product/platform
qualification remain required. No requirement status, milestone, gate or GUI
migration is accepted by this increment.
No status, migration entry or final gate is accepted by this increment.

The native owner/store checkpoint adds exact managed editor ownership, engine
queue-continuity checks, a portable shadow document and an actual Windows SDK
text store. Application text/selection notifications allow reads and defer
writes until the full batch completes. The Windows build, 29 UI suites and the
updated text-store suite pass; counted SDK tests cover 729 checks and reject
18 compiled fault variants. Source-bound windowed SP/OpenGL at 125% and
MP/Vulkan at 200% captures pass 32 semantic operations with ten reviewed engine
images. The immutable evidence is
`.tmp/ui/native-owner-integration/validation-evidence.json`. The first SDL fence
remains disabled and does not prove earlier-event consumption; checked queue
delivery, live native editing, complete IME/shaping, production fields, the
editor and all migrations/gates remain open. No acceptance state changes.

The precise SYSTEM-field increment adds the two paired brightness fields,
localized validation, checked clipboard commands and explicit handling of
unfinished local edits during Apply/exit. A completed service cancellation and
an unchanged full editor inventory are required before local Discard. Keep
Editing synchronizes projected layout before resuming the field in the same
dispatch. Growing validation scrolls into view without overriding later user
scrolling. The Windows build and 32 UI suites pass. SP/OpenGL at 125% and
MP/Vulkan at 200% each pass 37 semantic operations with 17 reviewed engine
images after gameplay, recorded in
`.tmp/ui/native-editing-integration/validation-evidence.json`.
Checked queue consumption,
owned SDL event collections and a portable composition reconciler extend the
native-input foundation; field activation and complete IME delivery remain
unfinished. The new evidence supplements the existing scope and accepts no
requirement, GUI migration or final gate.

The subsequent [native field binding and precision checkpoint](numeric-fields.md)
starts from engine `7a8036ac87dde9d29eb279a9c6ecc31cf187df55`. It adds clean authored
slider ticks, fixed CVar writes for small and custom numbers, complete native
Number presentation, checked collection scopes and prepared stable settlement.
The Windows build and 36 UI suites pass. SP/OpenGL at 125% and MP/Vulkan at 200%
each pass 38 semantic operations and 12 reviewed engine images after gameplay.
The immutable evidence is `.tmp/ui/native-field-binding-integration/validation-evidence.json`.
At that checkpoint, 200% focused controls still met the scroll boundary; page layout
qualification remained open. Live managed/provider/Session integration, exact persistent
recovery, native IME, the complete editor and all production migrations remained unfinished.
No requirement status or acceptance gate changes in this increment.

The native managed-bridge increment, based on engine
`59dbfa9429056a85ead40fff5e8f4f5b38be4225`, extends the [numeric field contract](numeric-fields.md)
and [native text boundary](text-input-routing.md). Exact typed settings comparisons and FTZ/DAZ-independent journal number serialization extend through transaction, display validation and startup recovery. Managed native-owner endpoints re-resolve the registered allocation/backend/document after resource preparation; copied owner barriers and exact retirement preserve stable drafts. The Windows hook-to-store bridge binds one checked provider generation and native/editor lease, preserves the immutable closed receipt through FIFO acknowledgements and synchronization, and closes its own callback scope before fence publication. Explicit lifecycle reconciliation and a checked idle barrier query supplement the ordinary Pump path. Shared focus reveal uses exact projected border corners, a density-aware 4dp inset where authored scroll ranges permit it, nested transformed scroll planes and the owning view clock without repeated pointer-scroll takeover.

The Windows engine build and all 40 UI suites pass. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% each reach gameplay, pass 38 semantic operations and produce 12 reviewed engine screenshots. The focused field retains the checked 4 dp body inset; at 200% its bottom is pixel 464 inside a body ending at 472. Raw screenshots and PNG previews have identical RGB pixels. SP has no warnings; MP retains the previous 93 warnings. Final captures use the verified E: installation, whose engine package checksums match the earlier capture.

The coordinator passes MSVC debug checks with 18 rejected mutations, Clang with 19 (including allocation failure), and Linux sanitizer checks. SDK bridge, managed-owner and exact recovery tests retain their own frozen source bindings and explicit limits. The immutable combined record is `.tmp/ui/native-managed-bridge-integration/validation-evidence.json`. Earlier timeout, harness-path and asset-discovery runs remain preserved; they are not substituted for the final qualification.

Native activation, the production Session route/probe, ordinary native character association, candidate geometry, complete composition/shaping, full page/editor/platform qualification, all 271 migrations and seven final gates remain open. Counted SDK callbacks, semantic controls and bounded focus geometry do not establish installed IME behavior, physical-input qualification or real interrupted-device/power-loss recovery. No requirement status or product acceptance changes.

The [preset and Windows text-session increment](performance-presets.md), based on engine `b542315efd4b724c458bc9cd0f6310c76afd71e8`, adds the six shared performance profiles and Auto-Detect to the opt-in SYSTEM draft. The generated 32-field change preserves all 21 unrelated settings, including custom brightness, and publishes only after validation and successful result construction. New controls reuse the existing editable vector artwork; labels wrap using all six existing language tables. Popup highlighting stays transient, and authoritative service readback precedes acknowledgement. Unfinished Number edits guard bulk actions and exit.

The [Windows text-session controller](text-input-routing.md) owns one exact native/editor/window/provider lease through activation and cleanup, including ordinary event disposition and a fresh ownership check before the final ACK. SDK tests use counted providers; production native activation and installed IME behavior remain unqualified.

The full engine build and all 44 UI suites passed, including 28,802 production Runtime assertions across 48 locale/density/wider-glyph/resize cases. The first gameplay capture exposed a missing local archive declaration: the protected emitter limit rejected lower presets. That failed evidence is preserved. Four preferences now declare their archive policy; actual production methods passed 22 checks on each of MSVC and Clang, followed by a fresh engine build and staging. Corrected windowed gameplay runs covered SP/OpenGL at 125%, MP/Vulkan at 200% and Russian SP/OpenGL at 200%, with 95 preset operations and 15 reviewed render-target images each, all 53 live settings unchanged. Separate SP/OpenGL at 125% and MP/Vulkan at 200% numeric regressions each passed 38 operations with 12 reviewed images, preserving exact values, Apply/reset and full focused borders. In total 361 operations and 69 images qualify only these states. SP runs had no warnings; each MP run retained 93 existing warnings. The source-bound record `.tmp/ui/preset-session-integration/validation-evidence.json` has SHA-256 `69ccd3f6872a47cf9fa66f632c969c426e9e95f61098ee7b93f9f7c317e526b8`.

Full effect execution and recovery follow the [unified settings direction](settings-effect-execution.md). Complete settings, native input/shaping, the editor, all 271 GUI migrations and every final gate remain required. No requirement status or acceptance changes.

The [settings effect foundations](settings-effect-execution.md) add automatic completion, checked audio finalization and a strict schema2 recovery envelope. Production startup and live settings still use schema1; complete mixed effects and portable reconstruction remain required. The [native disposition increment](text-input-routing.md) accounts for both fixed Session/poll schedules and preserves ownership through platform, pushed and scalar polled storage. Live native activation remains disconnected.

The [compact SYSTEM layout](performance-presets.md) keeps the real Marine-font Apply label on one line and the complete first brightness control visible at 200%. Full engine build, staging and all 47 UI suites passed, including 41,623 Runtime checks over 48 locale/wider-glyph/density/resize cases. Four engine render-target images were reviewed after windowed gameplay: English SP/OpenGL at 125% and MP/Vulkan at 200%. No input was injected or controlled and no mixed-effect Apply was performed. SP logged no warnings; MP retained 93 existing warnings. A visible authored scrollbar, full page interaction and all real-font/platform states remain required.

The source-bound checkpoint starts from engine `efcfdb0ffe6eaffa11dbf2ced026495539156478`; local evidence `.tmp/ui/native-effects-integration/validation-evidence.json` has SHA-256 `909df76b2aea07f6641eece8550cd90bd1d3d354e84acf1ffe5ad06c0dbd6457`. Earlier failed layout captures and the corrected pack-only staging failure remain recorded. No requirement, GUI migration or final gate is accepted by this checkpoint.

The [checked image/material restart](settings-effect-execution.md) preserves original recovery inventories and rejects stale instance or metadata ownership. The full engine and renderer modules build; all 49 UI suites pass. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% return to SYSTEM after ordinary video restart, with four reviewed engine render-target images and no injected or controlled input. This qualifies ordinary compatibility; the checked image-policy Apply operation still needs live qualification and portable consumed-policy/content recovery. SP logs no warnings; MP logs the previous 93 plus a vertex-cache virtual-memory warning during restart.

The checkpoint begins at engine `d6abeabc7e2e348ea11ce50a7671b830e77a15b1`. Its local record `.tmp/ui/renderer-effects-integration/validation-evidence.json` has SHA-256 `38ec300cd73f59baca9761b21351f3daa8612d6dbc73b98b0f420f4089975ad2`. Full settings, native input, editor, every GUI migration and all final gates remain required.

There are **65 partial, 161 pending and one verified requirement**. `BEH-002`
remains partial for the implemented settings transaction/service boundary.
`BEH-005` remains partial for committed action delivery and cancellation; stock
sounds and the complete device/modal/widget behavior still require implementation
and evidence. `WID-002`, `WID-004`, `WID-005` and `FLOW-002` moved from pending to
partial at the historical value-control checkpoint; every remaining conjunct and acceptance
criterion is preserved.
The SYSTEM exit increment adds bounded qualification evidence for `BEH-001`,
`BEH-002`, `SAVE-003`, `INP-004`, `INP-005`, `INP-006` and `FLOW-002` without
changing any status; `INP-005` remains
pending for the complete wheel, touch, text, composition and device contract.
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

The reusable [register validator](https://github.com/themuffinator/openQ4/blob/e3e65887480368191df154a9b52cce69d8e72140/tools/ui/validate_product_requirements.py)
compares an increment with its immutable Git baseline. It preserves requirement
scope, constraints, owners, dependencies and acceptance fields, checks explicit
pending-to-partial transitions, historical evidence, source hashes and unchanged
migration records. It never interprets a linked test or image as product
acceptance. Run the final audit after evidence documents and hashes are frozen:

```powershell
python tools/ui/validate_product_requirements.py `
  --baseline a8bad0f5bf69b08493714ad4faaeae7585d5abf2 `
  --partial SUR-006 `
  --output .tmp/ui/native-output-review/register-validation-final.json
```

Use a new output path for each audit. The optional `--defer-hashes` preparation
mode explicitly reports incomplete source bindings and cannot serve as the
final audit. The native-output strict structural/source audit is recorded at
`.tmp/ui/native-output-review/register-validation-final.json`. The historical
SYSTEM-exit strict structural/source audit is recorded at
`.tmp/ui/system-exit-review/register-validation-final.json`. The preparation
audit remains at
`.tmp/ui/system-exit-review/register-preparation-validation-final.json`. Historical
preparation, negative-case checks and the previous strict audit remain under
`.tmp/ui/value-controls-review/`. Three pre-existing historical summary
pointers have no recorded capture hash; the audit reports them as unbound and
does not turn their currently observed hashes into historical qualification.

The JSON `audit_findings` map explicitly connects all twelve implementation
findings (`UI-01` through `UI-12`) to required resolutions. `FINAL-001` through
`FINAL-007` preserve the complete corpus/behavior, visual product, editor,
display/accessibility/lifetime, performance/platform, clean installation and
documentation/publication gates respectively. None is satisfied by a prototype
or conversion count.

## Historical effective inventory refresh

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
