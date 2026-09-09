# Typed value controls and the normal SYSTEM route

This implementation increment starts from engine
`b3bf279b4347329cd8a45a4533536adc0510e860` and companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. It advances M2 of the
[product completion plan](../plans/ui-product-completion.md). The complete
[SYSTEM contract](system-settings-contract.md), all 227 product requirements,
all 271 GUI migrations and the seven final gates remain required.

## Application contract

Canonical document version 1 now supports `toggle`, stepped `slider` and
constrained `choice` roles alongside existing buttons. Each value control binds
a typed readback expression and names an action with an explicit `input` type.
An action argument may use `{"input":"value"}`; this operand exists only for
that immutable invocation. Events and buttons cannot invoke input-bearing
actions without an operand. No temporary application-state key or CVar carries
the gesture proposal.

State evaluation publishes properties, enabled state, values, mixed state and
choice-option eligibility atomically. Custom off-grid slider values and
unmatched choice values remain the actual readback until the user changes them.
The settings service validates edits, publishes the accepted draft and
acknowledges a process-unique proposal token. A late acknowledgement cannot
clear a newer pending edit. Failed edits retain rejection context without
changing the displayed accepted value. Captured slider drags alone display a
temporary preview; release submits one proposal.

The initial SYSTEM page binds each control's disabled appearance to the same
eligibility expression used for input, with 40% opacity from the visual
specification. Apply therefore visibly disables when there is no applicable
draft; its label and frame cannot continue to advertise an available action.

Toggle marks, slider tracks/fills/thumbs and choice popup/row/selection parts
are authored nodes with stable IDs. The compiler validates ancestry, part
types, positioning and property ownership. Widget-owned properties cannot be
written through aliases, bindings or motion tracks. The canonical source retains
the authored hierarchy for future editor round trips; runtime popup placement
changes only the derived DOM.

## Interaction and rendering

Sliders project pointer coordinates through the actual track transform and
thumb travel. Capture continues outside the track and over other controls;
loss of a usable projection, focus, eligibility or input ownership cancels the
gesture. Keyboard changes use the authored step grid, with Home/End and
Page Up/Down support. A choice popup owns its tentative highlight and consumes
outside dismissal without activating a control underneath. Disabled options
cannot be selected. Selection and keyboard/pointer highlight have separate
editable vector parts.

Choice popups escape clipped ancestors, retain inherited typography and clamp
or flip inside the drawable viewport. Their scroll position uses measured row
geometry. Canonical popup and ancestor opacity multiply after reparenting, so
page fades also affect the popup. Positioning and clipping invariants are
reasserted after layout and restore. A small
[RmlUi extension](https://github.com/themuffinator/openQ4/blob/e3e65887480368191df154a9b52cce69d8e72140/subprojects/packagefiles/rmlui/projection-geometry.patch)
updates stacking geometry and transforms after layout and before popup
placement, without an additional rendering pass. This makes placement use the
same animated transform as the frame being rendered. RmlUi remains pinned to
6.3 under its [MIT notice](../../licenses/RmlUi.txt).

Keyboard focus reveals controls inside scrollable bodies without resetting
unrelated scroll positions. Pointer-wheel events scroll the nearest actual
scrollable container when a choice popup does not own input. Wheel events are
bounded rather than replayed as a backlog after a stalled frame.

A second [RmlUi extension](https://github.com/themuffinator/openQ4/blob/e3e65887480368191df154a9b52cce69d8e72140/subprojects/packagefiles/rmlui/positioned-overflow.patch)
includes absolutely positioned descendants in scroll extents before a containing
block propagates its visible overflow. Fixed elements do not extend a scrolling
ancestor, and clipped descendants stop at their container. Canonical documents
suppress unstyled native scrollbar geometry; wheel and focus scrolling remain
available. This correction does not rerun layout to accommodate late native
scrollbars with nonzero dimensions. Editable vector scrollbar parts and their
complete pointer, keyboard, persistence and editor contracts remain required.

Snapshots containing value controls use instance version 3, with versioned
role and choice-scroll records. Restore validates the complete widget set,
roles and scroll ranges before committing any state. Open popups, drag
previews, pending proposals, input arms and queued actions are transient.
Button-only instances continue to write version 2; existing supported version
1/2 readers remain available for their original document contracts.

## Session ownership

`ui_retainedSystem` is a non-archived, default-off development option. The normal
SYSTEM tab submits `openRetainedSystem`; the narrow
`openq4_system open/report/back` diagnostics call the same Session operation.
The packaged resource is `guis/menu/settings/system.q4ui`, with canonical ID
`openq4.system`. The source and settings contract must validate before the
parent is deactivated.

Session owns the child and saves the exact parent and command handler. A clean
return restores that parent without replaying title-menu startup or resetting
its settings selections. Dirty, busy and recovery state is read directly from
the service owner, so pending dictionary writes cannot bypass the return
guard. Authored Back behavior handles unresolved changes. Forced replacement
or map teardown deactivates, drains and frees the child once, with references
cleared before callbacks. The normal SP/MP input inhibition and menu sound
ownership apply to the child.

## Remaining product work

This is an implementation increment, not acceptance of a finished SYSTEM
screen or widget library. The production page still requires the complete
31-option contract, display capability catalogs and coupled resolution
choices, precise numeric editing, complete effect application, preset
expansion, full modal/scrollbar accessibility, reusable component authoring
and the required editor round trip. Popup inheritance of canonical ancestor
vector masks and complete scroll-container persistence remain unqualified.
Its artwork, localization expansion,
motion, controller interaction, display matrix and all supported platforms
must meet the existing visual specification before release cutover. The
default-off route must not be enabled globally on the strength of a small
set of controls or diagnostic captures.

## Qualification of this increment

The Windows build and normal package stage passed. Thirteen native UI suites
passed, including the real Runtime/RmlUi geometry and widget suite, schema,
interaction, snapshot, presentation and settings tests. The Session route's
production-body boundary test passed 173 checks; the capture runner passed
15 Python tests and rejected 444 altered-log cases. The adapter test exercises
production adapter code with engine/service doubles and is reported separately
from the real engine runs.

Two normal Session captures passed after active map gameplay: SP/OpenGL at
125% retained density and MP/Vulkan at 200%, both at 1280x720. Each run covers
18 ordered stages, actual draft/readback and Apply, dirty Back, keep editing,
discard, tentative and committed choice selection, language and renderer
resource resets with an unapplied draft, clean parent return and a fresh owner
on reopen. All seven engine render-target screenshots from each run were
inspected through lossless PNG conversion. The 125% page uses two columns;
the 200% page reflows to a scrollable column and flips its popup above the
anchor. Disabled Apply is visibly distinct after the final presentation fix.

SP produced no warnings or errors. MP produced 94 pre-existing asset, AAS,
map-placement and vertex-array warnings, with no new warning messages and no
errors. These are bounded semantic integration and static visual checks, not
qualification of physical input devices, full motion quality or the complete
display/platform/language matrix.

The immutable local record is
`.tmp/ui/value-controls-review/capture-evidence.json`, SHA-256
`2c5c14d4d0a9d0fb8cc8d45dab9af39fdc9e6c4ab017f93a1f6a3e49c3cf1a40`.
It binds the final 8,778 build inputs, seven staged runtime binaries, packaged
source, logs, screenshots and supporting tests. Historical display-confirmation
and recovery records remain unchanged. No complete product requirement,
milestone or GUI migration is accepted by this increment.
