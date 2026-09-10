# Authored vector scrollbars

The retained document model supports a `scrollbar` control attached to a named
scroll viewport. Its track and thumb are ordinary editable vector nodes. The
runtime measures the actual Rml scroll range and track, derives thumb geometry,
and clips content inside its viewport. Scroll offset is local view state and
never becomes a settings value or an application action.
The engine adapter accepts that local role without demanding an application
action, while rejecting any scrollbar action, event or value output. Other
controls retain the typed application validation.

The SYSTEM composition reserves a 36 dp target and an 8 dp gutter beside the
settings body. A narrow dark trough and olive thumb preserve the Quake 4
45-degree motif; orange ink marks hover, focus and press. The header, action
footer and frame remain outside the scrolling body. The source generator is
`tools/ui/update_system_scrollbar.py`; repeated generation must be idempotent.
The body wrapper has a zero initial height and grows into the remaining panel
space, keeping percentage-height children from pushing the footer out of view.
The preset band reserves room for the gutter while its labels and actions retain
their minimum targets and can wrap with translated text.

## Interaction and restoration

Thumb dragging retains the original grab fraction through layout changes.
Track presses page with one line of overlap, and wheel, keyboard and gamepad
navigation share bounded scroll steps. A wheel over content prefers a usable
vertical bar, with horizontal fallback; a wheel directly over a bar uses that
bar's axis. Scrolling with a pointer preserves the focused content control.
Explicit navigation can focus a bar for line, page, start and end operations.

Every queued operation carries the owning source, modal and geometry identities.
Hidden, fitting or unavailable scroll regions cannot accept stale operations.
A zero-sized viewport invalidates layout and quarantines held navigation until
release; restoring a valid size does not revive a canceled gesture. The actual
viewport and projected track supply local coordinates, so density is applied once.

Documents with authored bars save logical scroll offsets in version-3 widget
snapshots. Restore clamps them to the current measured range. A live density or
viewport change preserves deliberate scrolling; a fresh focus request or a
validation-driven field extent change can reveal its control. Legacy documents
retain their previous focus-reveal and snapshot behavior. In mixed layouts,
preservation applies only to the authored viewport's declared axis; unrelated,
nested and perpendicular legacy scroll axes can still reveal focus. Restored
focus is checked after the corresponding fresh layout bounds are available.

## Qualification scope

The pure interaction and actual Runtime/RmlUi tests cover geometry, ownership,
modal cancellation, snapshots, live density changes and invalid viewport recovery.
Thumb border and padding participate in the measured minimum; a frame larger than
its track is inert. Adjacent slider, choice and numeric-field size conversion
also includes both border and padding, preserving the logical painted extent for
content-box parts. Actual Rml geometry tests cover both box-sizing modes.
The integrated engine captures and final suite results are recorded in
`.tmp/ui/production-retirement-integration/validation-evidence.json` when complete.
The first integrated Vulkan run exposed the adapter's unconditional action
requirement; its failed capture is preserved beside the corrected qualification.
These tests do not establish physical input, touch gestures, all transformed
layouts, every screen migration or full platform and accessibility acceptance.
The product requirement `WID-010` remains incomplete until that work is qualified.
