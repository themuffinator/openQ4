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

## Panel bounds and vector popup framing

A choice may name a proper ancestor with `placementBounds`. SYSTEM's four
dropdowns name the settings body so their lists stay inside the panel and clear
the action footer. The bound uses the ancestor's actual projected convex border
quad intersected with the drawable viewport, with a 4 dp clearance. Rotated,
skewed and perspective ancestors keep their real edge planes; an axis-aligned
bounding box is not a substitute. Omitted bounds preserve the earlier placement
behavior. The strict document validator rejects a missing or unrelated ancestor
and an independently transformed popup root.

Placement chooses the space below or above the control, with anchor overlap as
a last resort. It preserves font and row size, reserves popup padding and the
scrollbar gutter, and requires at least one complete row. Intrinsic text width
uses Rml's actual text-line processing and font metrics, including whitespace
handling. Unsupported canonical properties remain rejected; robustness checks
against Rml text transforms do not add canonical `text-transform` support.

After layout, the measured popup, viewport and scrollbar must fit the intended
plate. An opening is retired only when it can no longer be presented at all:
its plate or anchor became unavailable, or a settled paint could not place it.
Retiring preserves the release quarantine for held input and leaves the accepted
setting unchanged.

Everything else is presentation that is not measured yet rather than
presentation that is wrong, and an unmeasured opening stays open and accepts no
option. This covers an opening that has not completed its first layout and one
whose panel moved since the last paint. Both are ordinary: the engine pumps
pointer motion and paired key releases between paints, and revealing a freshly
focused control keeps scrolling its panel, so an opening routinely meets input
before it has ever been painted. Retiring there would close every bounded list
before the player could see it. The next paint re-places the opening and retires
it only if it genuinely cannot be placed.

Losing measurement also drops any gesture the opening owns, because the press
named rows whose geometry no longer describes the screen. The matching release
selects nothing, the list stays open, and the following painted frame makes it
usable again. This is what prevents a rapid second acceptance from committing an
option using old row geometry.

SYSTEM now authors a dark six-dp cut-corner plate with olive outer rails, a
subdued inner rail, a matching vector mask and eight-dp interior spacing. These
are editable canonical vectors. The control IDs, localized options and settings
actions retain their existing associations.

The regular Meson suite runs both the pure placement solver and the actual
Runtime/Rml test against the repository SYSTEM document and all six language
tables. The pure runner also supports Clang, MSVC, GCC sanitizers and compiled
behavioral mutations. The MSVC Runtime runner binds its exact source, archives,
document and language inputs; it never silently substitutes a scratch document.
The Runtime runner drives each production dropdown the way the engine does,
opening it while the reveal scroll is still settling and moving the pointer
before the first paint. `tools/ui/capture_system_page.py` then checks the same
thing in the real client: every choice reports its popup geometry beside its
widget record, and an open bounded popup must name a measured opening.
These checks cover the bounded placement increment, not the complete migrated
GUI corpus or physical-device acceptance.
