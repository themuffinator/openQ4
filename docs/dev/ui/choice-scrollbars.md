# Authored choice-popup scrollbars

Choice controls can own a local vertical scrollbar in their existing popup.
The bar scrolls option content without changing the accepted value or emitting
an application action. It uses editable vector parts and a stable gutter;
Rml's native scrollbar artwork remains disabled. SYSTEM's four choice popups
are composed by `tools/ui/update_system_choice_scrollbars.py`.

The optional `control.scrollbar` object contains `track`, `thumb`, `lineStep`
and `minimumThumb`. Lengths are authored in dp; line step and minimum thumb are
bounded to 1–4096. The viewport and track must be direct popup children and the
thumb a direct track child. The track is outside option clipping. Track/thumb
position and height, plus viewport width, are derived properties with explicit
typed authored bases. Bindings, aliases and timelines cannot also own them.
The thumb cannot have its own transform or minimum/maximum height constraint.
The track keeps its authored width, with an eight-dp gap before option content.

Interaction owns the popup lifetime, local logical offset, explicit highlight
reveal request and matched pointer gesture. A copied readback binds the current
popup and revision to fresh actual layout. It has no `ControlAction` and uses
no generic `ScrollCommand`; the generic scrollbar queue retains its separate
ownership. Readback refusal and stale input do not produce a setting proposal.

Track presses page, while thumb drag preserves the grabbed fraction through
resizing. Their matched releases do not select an option or close the popup.
Wheel input scrolls one local line per pulse and preserves choice focus and
highlight. Keyboard navigation still selects the highlighted candidate and
explicitly requests its reveal. Ordinary paints do not undo deliberate pointer
scrolling to reveal an unchanged highlight. Only accepting an actual option
follows the existing typed proposal/host-acknowledgement path.

ValueControlView measures actual row extents, viewport, track, and the thumb's
border and padding minimum. It projects pointer coordinates into the track's
actual transformed plane. Local offsets remain in dp through live density
changes and clamp after reflow. A hidden popup must first become measurable;
zero/unavailable geometry grants no scrolling authority. Invalid viewport,
closure, replacement, modal change or a changed authoritative choice source
cancels the old gesture. No gesture is serialized.

The shared semantic `OpenChoicePopup`, `CloseChoicePopup` and
`ScrollChoicePopup` methods support editor and diagnostic presentation without
creating device events. Opening requires an eligible focused choice, at least
one available option, and no existing popup or conflicting held/armed input.
Every choice, including the earlier choice format, exposes its nonzero opening
token. Closing names that exact token. Scrolling additionally checks that the
painted row quads, viewport and track still match actual layout; a row transform
changed between frames invalidates the request. Neither scrolling nor closing
accepts an option or drains the application action queue.

The engine's `openq4_retainedGui choice open|close <control>` and
`choice scroll <control> line-back|line-forward|page-back|page-forward|start|end`
diagnostics require the active interactive view and strictly validate arguments.
Use the existing semantic `focus <control>` operation first. The `widget`
diagnostic reports the opening/revision, logical offset and copied measured
geometry beside the unchanged accepted/pending value record. A fitting list has
an inert bar and refuses scrolling. These operations grant no native input
authority and do not exercise physical device routing.

When every option fits, the track and thumb are hidden without changing their
measured boxes or the reserved gutter. They return when scrolling is usable.
This prevents a focused choice from showing an apparently draggable orange
thumb on an inert list. Geometry readback still describes the fitting viewport;
scroll requests remain refused and options retain their normal selection state.

Documents with an authored generic bar or a bar-equipped choice use nested
widget table version 3 and retain a bounded nonnegative `scrollOffsetDp`.
Restoration leaves the popup closed; its first opening clamps the restored
offset against measured geometry without resurrecting a drag. Older choices
retain `firstVisible` and their version-1/2 compatibility. Number draft/history
restoration and mixed generic/choice scrollbar documents keep their existing
atomic validation.

The portable runner `tools/tests/ui_choice_scroll_interaction.py` executes the
real Interaction and ScrollGeometry methods, including refused ownership and
compiled behavioral mutations. `tools/tests/ui_choice_scroll_runtime.py` links
the actual canonical runtime and pinned RmlUi against existing compatible MSVC
debug libraries supplied by `--repository`; it does not build those libraries.
The runtime suite includes the existing ValueRuntime cases and the generated
SYSTEM candidate, plus geometry, transforms, density, framed thumbs, source
changes and persistence. Host fonts/materials/render submission are counted
doubles. These checks do not qualify native input adapters, GPU pixels, actual
font fit, touch, accessibility, or the complete Choice/product requirements.
Those still require their platform and gameplay evidence.
