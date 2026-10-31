# Independent retained text scale

17 September 2026. Implementation increment on `idtech5-ui`, starting from
engine `85c011754b800409e5e471b454ac4d658b6f3d00` and companion
`a4946f8b28c53bb32ec323bddcf0e2276bc8492c`.

The retained runtime supports independent 100–200% text size through the archived
`ui_retainedTextScale` CVar (default `1`). This is partial implementation of
`LAY-004` in the [product register](product-requirements.md). Normal settings
controls and a reachable reset were subsequent work at this checkpoint; they
are now implemented in the [interface preference increment](interface-size-preferences.md).
Complete screen/editor integration and product acceptance remain required.

## Runtime and authoring contract

`Viewport::textScale` multiplies absolute `font-size`, `line-height` and
`letter-spacing` at presentation. Both `dp` and explicitly authored physical
`px` typography participate. Display/UI density still affects `dp` independently.
The inherited document default participates once. Font-relative `em` values use
the resulting font metrics and do not receive another multiplier.

Canonical values, bindings, animation samples, presentation aliases and saved
snapshots retain their authored units. Changing text size preserves motion
progress. Fixed `dp` furniture, vector paths and window-to-document pointer
conversion retain their existing scale. Invalid scale values recover to 100%;
finite values clamp to the supported 100–200% range. Strict prepared-document
validation rejects nonfinite/nonpositive scale input.

Typed lengths now accept `em`; transforms still accept only `dp`/`px` translation.
Font-relative dimensions allow text fields and columns to grow with typography.
The numeric view resolves these dimensions using the same computed font as its
text, caret and selection. `word-break` accepts the pinned RmlUi normal,
break-all and break-word policies, and `text-overflow` its clip and ellipsis
policies: a label in a box that hides overflow ends in an ellipsis instead of
being cut (the Remastered kill feed's names).

The production SYSTEM source uses flowing choice readbacks, font-relative
numeric fields and columns, and modal widths/action wrapping that grow with text.
Focus reveals enlarged fields after a text-size change. If the focused control
was visible before its layout or an ancestor viewport changed, reflow reveals
it again. Explicit focus records that geometry immediately, including when a
settings edit arrives before the next frame. Deliberate scrolling can still move
focus out of view, and a later reflow preserves that choice. The comparison
excludes scroll offsets; restored authored scroll positions retain their policy.
Auto-Detect grows with the font and wraps its label at word boundaries.
The header reserves room
for a whole title line, the main body reserves room for a complete numeric field,
and footer actions remain outside both scroll areas. Long status messages have
an authored scrollbar; dialog headings and instructions can also scroll while
their decisions stay visible. At combined extreme density/text settings, reading
the complete message or heading can require scrolling. The header rail follows
layout instead of crossing enlarged text at a fixed vertical coordinate.
Fixed padding, frame chamfers and scrollbar targets keep their design-pixel
dimensions.

Constrained dropdowns now resolve width and the scrollbar gutter before refusing
an opening because a row is too tall. Previously a translation wrapped at the
old width could falsely fail the full-row height check, even when it fit at the
new width. The intermediate layout remains unmeasured and cannot accept an
option. Final projected containment, minimum readable row size and stale-gesture
guards remain enforced.

A dropdown whose widest option is wider than the safe area now wraps it instead
of refusing to open. The list prefers each option's natural width and narrows
toward the widest run that the label's own `white-space` and `word-break` rules
cannot break, the same runs RmlUi wraps at. Under `break-word`, which every
SYSTEM label inherits from the page, a word may break anywhere, so that run is
the widest single character; it is measured per character, in linear time,
rather than by RmlUi's zero-width layout, whose cost grows with the cube of a
word's length. Narrowed rows are measured again after they wrap, before the
opening can accept an option. That adds one settling pass, so a frame now
allows four layout passes and still stops at the first pass that changes
nothing.

Within one opening the width only narrows. A narrower width wraps rows taller,
so growing back would undo the step that made them fit; where a projected
(rotated or skewed) safe area cannot hold one row at any width, the list is
refused instead of alternating between widths frame after frame. A new opening
or a changed placement context starts again from the natural width. Rows a
pass has just shown or changed, and every row of a popup shown in that pass,
still hold the boxes of their last layout, perhaps at another size or for
another list. They may place the popup, but a refusal waits for the next
pass's layout: refusing on them would hide the rows again before they were
laid out, so the same opening would be refused every time. A list that cannot
fit one wrapped row still refuses to open, and so does a label with a single
unbreakable run wider than the safe area. Reducing the readable font or
clipping an option is still never a fit.

The popup-placement runtime test opens the SYSTEM device list with a long
multi-word monitor name in seven languages at two glyph widths (1280x720 at
125% and 200% density, and 800x600 in English): the name wraps where the safe
area is too narrow, keeps one line where it is not, and every wrapped line
stays inside its row. It also opens a 48-character name with no spaces at
800x600 with 200% text, which only breaking inside the word can fit, and a
rotated scroll body at two angles, which must settle opened and measured or
refused within a few frames and then keep its width. Finally one runtime
opens a stale device list through ten alternating sizes in English and
German, two slots then eight at each, so rows hidden at one size show again
at the next still holding the boxes of the last; every opening must open, as a
fresh view does.
Before the stale-box rule one of those openings was refused.

Author the page through
[`retained_system.py`](../../../tools/ui/retained_system.py), which
`build_retained_screens.py --check` verifies without writing staged output. `capture_system_page.py --text-scale`
records the explicit text multiplier and language alongside density and exact
source/binary bindings. Its optional `--timeout` records a longer process budget
for slow debug builds without changing the semantic acceptance checks.

## Qualification

The Windows x64 MSVC debug build and staging passed. All 84 regression suites
passed, including 3,486 checks in the independent runtime suite, 3,616 in focus
reveal and 60,817 in the popup suite. The separate expanded SYSTEM matrix passed
211,245 checks, completing all 85 registered suites. The source checks passed
14 tests, the production viewport/resource harness passed, and the capture
oracle passed 18 tests with 1,224 rejected mutations. Evidence is retained under
`.tmp/ui/text-scale/`.

The new runtime suite checks independent density/text matrices, inheritance,
`dp`/`px`/`em`, tracking, fixed furniture, pointer conversion, animation and
snapshot recreation. The production SYSTEM suite uses real RmlUi layout and
the actual transaction source with counted host callbacks. It checks seven
viewport/density/text combinations for all six repository locales, each with
ordinary metrics and 40% glyph-width expansion: 84 combinations, including
640×480 at 100/150/200% text. Checks cover every service status string, footer
visibility, numeric focus/caret containment, four dropdowns, dirty-discard choices
and confirmation layout. Scrolling long statuses must reveal their final line
without generating a settings operation. The confirmation state is seeded for
layout testing; this does not qualify actual display Apply/Keep/Revert execution.
Both repository language tables are loaded; missing stock keys still use the
harness fallback. Counted font metrics are not real glyph rasterization, shaping
or completed localization evidence.

The staged engine then passed the normal SYSTEM Session route after active
SP `game/airdefense1` and MP `mp/q4dm1` gameplay. Three 1280×720 runs each passed
25 semantic stages: English SP/OpenGL at 125% density, English MP/Vulkan at
200%, and French SP/OpenGL at 200%, all with 200% text. All 27 engine screenshots
were reviewed, including focused edits, Apply, Discard, choice navigation,
language/renderer restoration, return and reopen. A separate French run added
three full-size reviewed images of Auto-Detect and the ends of the header and
discard-dialog text. Its scrollbar readbacks reached their measured ends.
All runs were hidden and windowed with host input disabled; screenshots came
from the engine render target. PNG previews preserve the raw screenshots' RGB
pixels exactly. These sampled states do not establish complete page acceptance.

The final source/binary/command/log/image bindings and review outcomes are in
`.tmp/ui/text-scale/validation-evidence.json`. The companion repository is
unchanged. `LAY-004` is partial; the register retains 227 requirements, 271
unaccepted GUI migrations and seven open final gates.

Intermediate failures are retained rather than accepted: footer/status wrapping
could consume the control viewport, one French real-font dropdown could not
open, action labels could split into tiny fragments, and a fixed header rail
crossed enlarged text. Real-font review also found that changing status could
clip an already-focused slider without changing focus. A subsequent capture
exposed focus and editing arriving together before the next frame. The shared
reveal path and production-page tests cover both cases and preservation of user
scroll, including a later layout change after focus was deliberately hidden.
The production source and the status/locale tests were
corrected before the final qualification.

## Remaining work

Add normal transactional UI/text preference controls, Defaults/reset and actual
archived-setting readback/restart coverage. Qualify real fonts across every
state/modal, independently combined UI and display scales, high contrast and
safe insets. Extend the same behavior to every production GUI and the native
editor. Complete font fallback/style/weight caches, largest animated transform
density, shaping, installed IME and native device/platform/performance evidence.
`TXT-002` remains pending: the engine font adapter still selects `fontInfoLarge`
and scales glyph geometry. This increment passes the independent requested size
through measurement/drawing; it does not add arbitrary-size glyph rasterization.
No production GUI, milestone or final gate is accepted by this increment.

## Unrelated findings

The English SP run is warning-free. MP retains the same 94 diagnostics as the
preceding audit: missing navigation/sound data, non-pre-cached declarations,
an ammunition entity in solid geometry and the vertex-array virtual-memory
warning. Both French runs retain six non-pre-cached Marine font-material
warnings seen in the preceding French capture. None of these runs reports an
engine error.

The build wrapper also repacks the unchanged 641,672,780-byte `pak1.pk4` after
UI source changes trigger GameLib staging reconfiguration. That adds minutes
to the debug iteration cycle; incremental package invalidation is a separate
build-system follow-up.
