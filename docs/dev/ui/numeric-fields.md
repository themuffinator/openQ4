# Numeric field implementation

Status: partial implementation. This component does not yet have a native text
input route or full visual/platform acceptance. It contributes to the mandatory
[UI replacement plan](../plans/idtech5-ui.md); it does not narrow that plan.

## Behavior and ownership

The canonical `number` role complements a slider with explicit precise entry.
Its finite decimal proposal is independent of slider steps. Empty text, a sign,
an unfinished exponent, invalid text and an out-of-range value remain local
drafts. None changes the accepted setting. Composition also stays local until
its committed text has been received and an explicit valid field commit occurs.

Each editing session and revision has a process-local identity. Selection,
replacement, composition, undo, redo and commit check that identity and current
focused eligibility. A typed proposal also carries the editor identity, modal
identity and proposal token through application dispatch. Earlier commands,
focus changes or document replacement can invalidate a queued proposal.

The application publishes authoritative readback before acknowledging success.
A successful callback alone cannot manufacture an accepted number. Host-backed
readbacks are refreshed before local edits and successful acknowledgements,
including actions completed between presentation frames.

Leaving a field, changing modal scope or losing usable input retires the live
session and composition while preserving the draft and its undo/redo history.
Returning to the field starts a fresh session. For a bound native editor,
cancellation first abandons native presentation and preserves the stable draft;
a later explicit local cancellation discards it. An external setting change preserves dirty text and marks
a conflict. Beginning the edit again does not discard the conflict:

- **Keep draft:** explicitly adopt the current accepted baseline while retaining
  local text and history. A separate valid commit is still required.
- **Reload:** replace the draft with the current accepted value and clear its
  history.

These are semantic operations. The opt-in SYSTEM page now pairs Brightness and
Ambient Brightness sliders with numeric fields, localized validation and guarded
Apply/exit behavior. Complete conflict controls, native character delivery and
the remaining production field families still require implementation.

## Editing commands

An active numeric editor now handles Left/Right, Home/End, Shift selection,
Backspace/Delete, Ctrl+A, Ctrl+Z, Ctrl+Shift+Z/Ctrl+Y, and Enter/keypad Enter or
controller Accept. Enter proposes the current valid number once per press.
Space belongs to text delivery and cannot also activate a menu control.
Tab and the menu navigation actions retain their focus-navigation role; leaving
the editor preserves its draft. Native character delivery remains unfinished.

Commands measure the current local buffer with the current resolved typography,
even when an earlier edit in the same event batch has not been painted. They use
the shared scalar text run, stage range replacements atomically, and preserve the
original directional selection for undo. Valid no-op commands retain the editor
revision. Current composition, conflicts, pending proposals or stale ownership
refuse mutation. Word movement has no boundary provider yet; Ctrl+Backspace and
Ctrl+Delete remain reserved until word deletion is implemented.

Ctrl+C/X/V, Ctrl+Insert, Shift+Insert and Shift+Delete request Copy, Cut or Paste.
The request carries its operation and original editor through the ordered
application queue. After earlier actions drain, the manager copies the current
buffer. Managed allocation, backend, document, modal and edit identity
are checked again before and after native clipboard access. Cut removes selected
text only after a successful write. Empty Paste does not delete selection, and
malformed, excessive or multiline text is rejected atomically. AltGr combinations
cannot become clipboard shortcuts. Localized notices distinguish read failure,
write failure and rejected text without changing the text or its undo history.

## SYSTEM draft handling

Brightness accepts finite values from 0.5 to 2; Ambient Brightness accepts 0 to 1.
Explicit field commits retain precision independently of the paired slider's
step. The responsive row keeps the label above the slider and field; validation
wraps below the field within its editable vector framing.

An unfinished local edit blocks Apply, Apply and Exit, Defaults and a conflicting
edit from a sibling control, including when the field is inactive. Unrelated
settings remain editable. Back considers both service changes and local drafts.
Keep Editing closes the dialog and returns to the first blocking draft without
committing or rebasing it. Discard clears local text only after the settings
service has completed cancellation. A queued display rollback, failed readback
or changed editor inventory preserves the text. A later explicit Discard can
clear a local-only draft even when the settings transaction is already closed.

The Runtime supplies a complete, bounded inventory of local editors with
non-reused lifetime/revision stamps. The adapter alone publishes
`ui.numberDraftsPending` and `ui.numberDraftMessage`; authored programs and
external GUI state cannot write those authorities. Every destructive decision
checks the current full inventory. Service recovery/confirmation messages take
priority over local draft guidance. Completion of asynchronous cancellation
never silently replays an earlier local discard request.

The input adapter binds each held command key to its original editing session.
Repeats cannot transfer to another field, reactivate an old session, or emit a
menu Accept/release. Cancellation quarantines claims until release. SDL keyboard
events carry optional fixed-byte modifier/repeat metadata, so Ctrl/Alt shortcuts
and Shift+Tab use the state captured for that event. Older payload-free keyboard,
mouse and controller records remain supported. Journal decoding validates the
metadata before publication and retains the historical outer event layout.

The semantic diagnostic `openq4_retainedGui number command <control> <command>`
accepts `left`, `right`, `home`, `end`, `word-left`, `word-right`, `select-all`,
`backspace` and `delete`; an optional final `extend` requests selection movement.
It invokes the same Runtime command API without injecting a device event.
See [native text delivery](text-input-routing.md) for the remaining input route.

## Authoring and rendering

The control requires `value`, a typed numeric `action`, finite `minimum` and
`maximum`, optional `exponent` and `maxBytes`, and these named `parts`:

| Part | Required relationship and purpose |
| --- | --- |
| `viewport` | Positioned, clipped group inside the control |
| `text` | Literal single-line text, directly inside the viewport |
| `selection` | Independent geometry directly inside the viewport |
| `caret` | Independent geometry directly inside the viewport; opacity controls blink |
| `composition` | Independent underline geometry directly inside the viewport |
| `validation` | Localized text inside the control and outside the clipped viewport |

The viewport may share a transform with its contents. Individual transforms on
the text and editing geometry are forbidden. Their derived position, dimensions,
visibility and text properties are reserved from bindings and animation.
Viewport clipping, literal text and left alignment are enforced by the runtime.
Author artwork and surrounding layout according to the
[visual specification](../ui-visual-design.md).

Font measurement, glyph submission and numeric caret queries share an immutable
run. Fractional glyph advances remain intact. Placement accounts separately for
the renderer's snapped text translation, actual line baseline, margins, padding,
clipping and horizontal scroll. Invalid or unsettled geometry cannot supply a
native candidate anchor or keep stale editing ink visible. Inactive drafts keep
their text and validation but have no editing ink or live hit geometry. Reduced
motion gives a steady caret. Native presentation supplies the full displayed
text, directional selection and every composition range. Bounded copies of the
authored vector underline share the viewport clip, carry no canonical IDs and
are removed when the document or editor retires. Empty ranges retain native
ownership without drawing false underline extent. Geometry caches include exact
native presentation and unsettled state; matching local edit IDs alone are
insufficient to reuse a caret or candidate anchor.

These runs currently describe unshaped scalar text. They are not evidence of
grapheme, bidirectional, fallback-font or general international text completion.
The shared font service preserves its existing streaming behavior for legacy
markup that exceeds the editing limit or cannot supply a strict editable run.

## Persistence

The existing outer instance schema remains version 3 or 4 according to modal
support. Nested `widgets.version` is 2; version 1 remains readable. A Number
widget can additionally contain `number` with:

```json
{
  "state": {"text": "1.25", "anchor": 4, "caret": 0},
  "undo": [{"text": "1", "anchor": 0, "caret": 1}],
  "redo": [],
  "baselineValue": 1,
  "baselineText": "1",
  "conflict": false
}
```

Snapshots never contain composition, native origins, editor identities or queued
proposals. Restore validates the exact document/source identity and fresh host
readbacks, then restores drafts inactive. A changed authoritative baseline
rebases a clean field or preserves a dirty field as a conflict.

Save and restore reject more than 256 drafts or 16 MiB of combined draft,
baseline and history text. Each field retains its own text limit and shared
64-entry/1 MiB undo/redo budget. Invalid text, offsets, baseline formatting,
unknown fields or excessive history reject the complete restore atomically.
Limits reject rather than truncate user text.

## Validation boundary

Native model suites cover parsing, readback/proposal separation, stale ownership,
inactive drafts, conflict decisions, bounded history and atomic snapshots. The
Runtime suite uses actual RmlUi layout and drawing with a counted Host, including
fractional density, transforms, scrolling, composition and resource restoration.
These tests do not operate a native device or qualify native IME behavior.

`tools/ui/fixtures/number-edit-smoke.q4ui` supplies first-party vector artwork for
the managed adapter. Its companion configuration uses `openq4_retainedGui number`
semantic operations and engine `screenshot` commands. It is a test fixture;
at that checkpoint the shipped SYSTEM page still needed an entry field and
ordinary input.

The command integration passes all 27 Windows retained-UI suites. Windowed
SP/OpenGL at 125% density and MP/Vulkan at 200% each reach gameplay before
exercising `number-command-smoke.cfg`. Each run records 16 successful semantic
operations and five engine screenshots. Selection of the final digit of `1.25`,
deletion to `1.2`, undo restoring `1.25` and its directional selection, same-batch
commands before repaint, and explicit acceptance of `1.375` are checked.
Counted adapter tests additionally exercise ordinary key routing, held-key
ownership and captured modifiers; these are not physical-device tests.

The earlier numeric-field checkpoint at `b09d8ee` passed 21 suites and recorded
incomplete drafts, inactive save restoration and composition ink. Its ordinary
SYSTEM regression preserved native-resolution UI pixels across the recorded
render-scale cases. Those historical captures retain their own source/binary
bindings. Neither checkpoint establishes native IME or complete product acceptance.

The adjacent event-journal repair bounds and validates owned event payloads
before allocation or command dispatch, ignores recorded process pointers,
releases payloads on error, and closes partial journal-file opens. It retains
the historical native journal layout and does not establish portable or
deterministic native text replay.

The precise SYSTEM-field checkpoint passes 32 Windows UI suites, including
same-dispatch modal focus restoration and growing-validation scroll regressions.
Windowed SP/OpenGL at 125% density and MP/Vulkan at 200% each reach gameplay,
then pass 37 semantic operations with 17 engine screenshots. The reviewed
sequence preserves a local `1.375` draft, displays all three clipboard notices,
resumes editing through the safe modal default, commits the field before Apply,
discards invalid `1e`, reopens the accepted value and restores brightness to `1`.
New validation text scrolls into view when the focused field grows; unchanged
frames preserve deliberate user scrolling. At 200%, the field's external label
can scroll above the viewport, so this does not qualify the entire page layout.
The immutable local record is
`.tmp/ui/native-editing-integration/validation-evidence.json`. Native OS clipboard
and character input, IME/shaping, other languages, full-page and product acceptance
remain separate requirements.

The follow-up precision audit found generated slider ticks with floating-point
tails and small ambient values such as `1e-7` lost by legacy CVar exponent
normalization. Generated ticks now use the authored minimum/step decimal scale;
custom readbacks and typed drafts retain their exact value. SYSTEM writes use
fixed decimal text and reject new values that narrow to a nonfinite or zero
float. This does not increase the renderer's float precision. Exact recovery of
observed originals under FTZ/DAZ is being extended through the transaction and
persistent recovery journal.

The native field binding regression now drives actual Runtime/Rml at 125% and
200% density through complete native text, reverse selection, concurrent and
empty composition ranges, stable-only snapshots, host conflicts, checked
restoration and one-group undo/redo. Native synchronization precedes prepared
local settlement. The combined Windows build passes 36 UI suites; standalone
checks additionally cover the actual Windows SDK store and CVar normalization.
Windowed SP/OpenGL at 125% and MP/Vulkan at 200% each reach gameplay and pass
38 semantic operations with 12 reviewed engine screenshots. The sequence checks
clean `0.075` slider text, exact accepted readbacks for `1e-7` and custom `0.1375`,
the next authored slider tick `0.15`, Apply, restoration and return to the parent.
SP has no warnings; MP retains the preceding checkpoint's same 93 warnings.
At 200%, the focused control still sits against the body's scroll boundary;
focus reveal spacing and full-page layout remain unfinished. These captures
do not operate native character or IME input. The immutable local record is
`.tmp/ui/native-field-binding-integration/validation-evidence.json`;
live native provider integration and final product acceptance remain open.
