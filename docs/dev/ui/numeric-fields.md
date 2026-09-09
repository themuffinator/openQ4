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
Returning to the field starts a fresh session. Explicit cancellation discards
that field's draft. An external setting change preserves dirty text and marks
a conflict. Beginning the edit again does not discard the conflict:

- **Keep draft:** explicitly adopt the current accepted baseline while retaining
  local text and history. A separate valid commit is still required.
- **Reload:** replace the draft with the current accepted value and clear its
  history.

These are semantic operations. Their production controls, specific localized
validation and integration with settings Apply/exit remain required work.

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
motion gives a steady caret.

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
the shipped SYSTEM page still needs a completed entry field and ordinary input.

The Windows integration passes all 21 retained-UI suites. Windowed SP/OpenGL
at 125% density and MP/Vulkan at 200% each reach gameplay before exercising
the managed numeric fixture. These captures cover exact accepted readbacks,
incomplete drafts, inactive save restoration, undo, selection and composition
ink. The ordinary SYSTEM regression also preserves native-resolution UI pixels
across the recorded render-scale cases. These are bounded semantic and rendering
checks; they do not establish physical-device or native IME acceptance.

The adjacent event-journal repair bounds and validates owned event payloads
before allocation or command dispatch, ignores recorded process pointers,
releases payloads on error, and closes partial journal-file opens. It retains
the historical native journal layout and does not establish portable or
deterministic native text replay.
