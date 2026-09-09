# SYSTEM controls and transactional exit

This increment starts from engine
`6c0defc8158c674c0e117a0a3754882a51bcef0d` and companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. It advances the normal SYSTEM flow in
M2 of the [product completion plan](../plans/ui-product-completion.md). The
[SYSTEM contract](system-settings-contract.md), all 227 product requirements,
all 271 GUI migrations and the seven final gates remain required.

## Settings and authored controls

The packaged `guis/menu/settings/system.q4ui` now presents 16 value controls.
Bloom, SSAO, tone mapping, CRT, irradiance, UI aspect behavior and resolution
scale join the eight initial image controls. VSync uses the existing display
transaction and confirmation path. Resolution-scale option labels are localized
through all six language tables. The page retains the complete 53-field
draft/baseline schema and its existing responsive column layout.

Each new control uses the same typed proposal/readback contract and editable
vector parts as the [value-control implementation](value-controls.md). Disabled
appearance follows input eligibility. These additions do not expose Defaults or
performance presets before their complete effect application is available.

## Returning with an unresolved draft

Back on a dirty page presents three choices. Continue editing preserves the
draft. Discard cancels the owned transaction and returns to the exact parent
without writing the draft to live settings. Apply changes submits the new typed,
no-argument operation `settings.system.applyExit`. The ordinary page Apply button
continues to apply while staying on the page.

An immediate successful Apply-and-exit closes the actual transaction before
publishing an exit receipt. A display change remains on its owning page while
the device applies and the user confirms it. Exit requires successful Keep and
completion of persistence. Failed application, Revert, timeout, recovery or a
failed save cancels the exit intent and keeps the unresolved flow available.
Retry can finish recovery or saving; it cannot revive canceled exit intent.

The service binds pending exit intent to the live owner and display request.
A private, one-use receipt transfers a successful result to the GUI adapter;
document state cannot authorize return. The adapter's read-only pending-work
query observes this receipt so asynchronous completion reaches Session without
another input event. Dispatch consumes it only after preparing the owning view
and draining ordered actions. New transactions, owner closure and release
invalidate old receipts. A failed resource reconstruction leaves a valid
receipt pending until the view can be prepared.

Session still checks the actual settings owner before restoring its saved
parent. The service therefore cannot convert a clean-looking GUI dictionary or
a stale callback into permission to close an unrelated transaction.

## Authored modal ownership

Group nodes can declare `modal` metadata with a descendant `initialFocus` control
and an optional `back` event. Effective canonical display through the ancestor chain determines
modal ownership; opacity animation does not expose background input. The shared
runtime preserves the prior focus before changing eligibility, contains input in
the active scope and restores the prior eligible control when that scope closes.
Nested scopes retain their own return focus. Safe initial focus is established
after fresh layout and before the first visible frame.

The dirty dialog declares Keep editing as its initial focus and its Back action.
Its visible label and semantic control label share a dedicated lookup in all
six language tables. Twelve CPU layout cases at 125% and 200% use the authored
TrueType metrics and confirm that this label fits without overlap or missing
glyphs. Surrounding retail labels use the installed English fallback plus locale
overrides, so this check does not qualify entire translated screens.
The display dialog declares Revert; its Back event requests restoration only
while the service permits it. An authored modal without a Back event consumes
Back. The adapter validates the scope's activation identity before dispatching a
queued Back, click or value proposal, preventing a replaced or reopened dialog
from receiving old input. Already-committed event-program actions retain their
separate ordered delivery contract.

Documents declaring authored modals use instance snapshot version 4, recording
scope ownership and pending initial/return focus. Activation tokens remain
transient and are regenerated on restoration. Documents without this metadata
retain their existing snapshot formats. Simultaneously visible modal branches
must form a nested chain; invalid direct updates are rejected atomically, and
the runtime also guards against ambiguous scope ownership. Canonical display
properties remain non-animatable. Invalid viewport geometry suspends focus
resolution while preserving the selected control for the next valid layout.

## Repeated pointer-wheel input

The engine adapter previously replayed a pointer move before every wheel event.
That could return a choice popup to pointer navigation and reset the tentative
highlight between consecutive wheel steps. Wheel routing now refreshes pointer
coordinates only when they or the viewport mapping change. Explicit pointer
movement and button input still reclaim pointer navigation. Input quarantine
and resource reconstruction invalidate the cached mapping.

## Qualification and remaining work

The final Windows build and staged package passed the following bounded normal
Session checks after active map gameplay. Every run used a hidden 1280x720 window,
disabled host input and engine render-target screenshots.

| Run | Density | Semantic stages | Reviewed images | Engine warnings |
| --- | --- | --- | --- | --- |
| SP/OpenGL exit decisions and display confirmation | 125% | 21 | 8 | 0 |
| MP/Vulkan exit decisions and display confirmation | 200% | 21 | 8 | 96 |
| SP/OpenGL page, added controls and reconstruction | 125% | 25 | 9 | 0 |
| MP/Vulkan page, added controls and reconstruction | 200% | 25 | 9 | 94 |

All four runs completed without engine errors. MP retains 86 previously reviewed
warning signatures: stock asset/AAS/map warnings and a vertex-cache warning.
The exit run has four successful renderer initializations versus two in the page
baseline; its two additional cache warnings each occur immediately after the
corresponding initialization. Every other warning signature and multiplicity
matches the baseline exactly. Raw differences remain recorded.

Fifteen native UI suites pass, including 302 modal interaction checks, 134 real
Runtime/RmlUi modal checks and 10,179 value-runtime checks. Production-boundary
validation includes 79 settings-service scenarios, 173 Session checks and the
retained adapter suite. Twenty-nine capture-tool tests reject 2,294 negative
trace mutations plus source-contract and warning-placement forgeries. The final
page passes 5,747 Document/State/Behavior checks across 512 guard combinations.

The immutable local record
`.tmp/ui/system-exit-review/capture-evidence.json` has SHA-256
`ddde52034403036ca1821dbb75aed11961404b311032149d1d2ff1c35f213b74`.
It binds 8,839 source/build inputs, seven staged runtime binaries, two PK4s,
the test/tool evidence, logs and all 34 visually reviewed images. Lossless PNG
review copies are verified against the engine TGA pixels. Preparation and failed
checker records remain separate; they do not substitute for the four final runs.

This evidence qualifies the recorded semantic paths and static views. It does
not qualify physical devices, complete motion, full localized screens or the
entire platform/display matrix. CI wiring is present; cross-platform CI execution
is not inferred from these Windows results.

Render-order review also found required follow-up work. OpenGL's
[swap-tail resolution filter](../../../src/renderer/draw_common.cpp) can filter
the completed UI in a UI-only frame without an eligible world-scene resolve.
Ordinary world-backed GL and Vulkan paths resolve before retained UI; the
legacy whole-frame crop mode remains a separate exception. OpenGL applies CRT
to the completed framebuffer, while the inspected Vulkan route has no matching
CRT implementation. The combined CRT/85% capture does not isolate resolution
scaling or establish effect parity. Repair and independently qualify these
paths before accepting output-resolution UI or the complete settings effects.

The replacement remains a non-archived, default-off development option. The
complete 31-option SYSTEM contract still requires capability-backed display and
resolution catalogs, precise numeric text entry, preset and Auto-Detect draft
expansion, all effect executors, Defaults, full modal and scrollbar behavior,
source-derived artwork and transitions, editor round trips and the full
platform, language, display and physical-input matrix. Sixteen controls and
bounded semantic captures cannot accept M2, a production migration or the full
UI product.
