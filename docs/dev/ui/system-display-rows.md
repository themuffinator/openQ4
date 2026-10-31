# SYSTEM display rows

The retained SYSTEM page now offers **Display Device** and **Expand Across
Displays**. Both rows lead the display column and read the
[SYSTEM display lists](system-display-catalog.md) the settings service
publishes to the page owner. Display Resolution and Refresh Rate followed in
their own increment ([SYSTEM resolution rows](system-resolution-rows.md)).

## Display Device

- **Options.** Auto (`#str_229914`) comes first, then one option per device
  list slot: up to eight, labelled from `settings.display.N.label`. The list's
  `settings.display.optionCount` bounds the shown options, so the popup
  lists Auto and the published devices only. Eight rows show before the
  popup scrolls, as the visual specification's dropdowns do, where the scroll
  body has room; a shorter body shows what fits (about seven rows at
  1280×720). A monitor name wider than the popup's safe area wraps onto more
  lines, breaking inside a word when it must, so the list opens wherever one
  wrapped row fits ([wrapped options](text-scale.md)).
- **Picks.** A pick sends `settings.system.display` with its slot and the
  list token, `{index, catalog}`, and never edits `r_screen` directly; the
  page's direct `edit.r_screen` action is gone. The service checks the token,
  writes `r_screen` as an ordinary draft edit and records the monitor. The
  token is read when the pick is dispatched. A list that changes its labels
  or length closes an open popup, and the service refuses a pick against a
  list rebuilt since its last publication.
- **Displays that are not connected.** A slot past the connected displays
  names a stale choice, "Display N (not connected)". It shows so the closed
  control can name it, but it cannot be picked: its label reads at the
  visual specification's disabled-text strength, 40%, and Auto or a
  connected display replaces it.
- **Limit.** The list offers the first eight displays. A draft `r_screen` of
  8 or more has no slot, so the closed control shows the setting's number,
  which counts from 0 where the labels count from 1: the ninth display reads
  "8". The stock page lists every display.

## Expand Across Displays

- **Options.** No or Yes (`#str_200059`), as V-Sync answers, editing
  `r_multiScreen` 0 or 1 through the ordinary draft edit. **A deliberate
  departure:** the stock labels, "Primary Display Only" and "Span All
  Displays", wrap past the scroll body's height at the largest text size in
  several languages, so their list could not open there.
- **When a span is offered.** Yes is enabled only while the
  lists allow a span (several displays, absolute window placement and a
  combined size within 16384 pixels) and the draft asks for a borderless
  window or desktop fullscreen. An ordinary window ignores a span and
  exclusive fullscreen refuses one, so neither offers it. A drafted span that
  can no longer apply keeps its name, and No stays pickable.
- **Why Yes is unavailable.** While it is, Yes reads at the disabled-text
  strength and a line under the value says why: "Spanning needs a borderless
  window or desktop fullscreen." (`#str_230088`) when the lists allow a span,
  or "These displays cannot be spanned." (`#str_230089`) when they do not, as
  with one display or a desktop without absolute window placement.

## Visibility and availability

- On one display both rows are hidden, as on the stock page. Each still shows
  while the draft names a display the lists lack (`r_screen` at or past the
  display count) or a span (`r_multiScreen` other than 0), or holds a change
  not yet applied, so the player can see and undo it. A pick therefore keeps
  its row, and its focus, until Apply; and a display picked on two displays
  stays reachable when its monitor goes, while Apply needs that monitor until
  the slot is chosen again.
- Undoing a pending change, back to an applied value for which the row is
  hidden, hides the row again. Focus then leaves the row, and the next
  navigation input starts from the top of the page. That needs a change made
  while the row showed for another reason, such as a monitor unplugged after
  the pick.
- Without lists, as before the first publication or when the topology capture
  fails, both rows stay hidden: neither could work.
- Both rows follow the page's shared editing rule (open, editing, not busy, no
  confirmation, discard dialog or unconfirmed numeric draft) and need the
  lists to be available.
- Hidden rows take no focus, and keyboard order passes over them.
- An option that can become unavailable owns its label colour at run time:
  `ChoiceOption::Conditional()` reserves it, so a binding or timeline cannot
  also drive it.
- `RETAINED_SYSTEM_MISSING_SETTINGS` no longer lists `r_screen` or
  `r_multiScreen`.

## Authoring

`tools/ui/retained_system.py` builds both rows (originally cloned from the
post-AA choice and V-Sync by a patch script, since retired), with their
`draftDisplayDevice`, `baselineDisplayDevice`, `draftMultiScreen` and
`baselineMultiScreen` aliases, bindings and feedback timelines. The span row's
reason line, `settings_multiscreen-status`, is a note under the row. On the
SCREEN tab the DISPLAY column reads `[device, span, span reason, fullscreen,
borderless, policy, MSAA, V-Sync]`.

## Known limitations

- Like every retained dropdown, the rows step their value with left and
  right, as the visual specification asks of cycling values, and Accept
  opens the list, so a list that cannot fit one wrapped row still leaves a
  way to change the value. The engine lays the page out in
  at least 640×480 dp, where the stale "not connected" labels fit at every
  text size; a long connected monitor name that wraps to four or more lines at
  200% text can still refuse at that floor.
- Displays past the eighth cannot be picked here (see **Limit** above).

## Qualification

- `tools/tests/native/UiSystemDisplayCatalogTest.cpp` runs the production page,
  runtime and transaction with lists published as the service publishes them,
  in seven languages at two glyph widths. It covers:
  - one display hiding both rows, with keyboard order passing over them, and
    each reason a row still shows there: a pending pick, an applied display
    at the display count, an applied span and turning it off, and a pick whose
    monitor changed;
  - a stale applied display and span on one display: the closed control's
    "not connected" name, the stale slots refused and dimmed, the pick's slot
    and token, the rows kept until Apply and hidden once applied;
  - several displays: Auto's localized label, slot labels, the pick's slot and
    token, a declined pick, a new token on the next pick, and a changed list
    closing the open popup without a pick;
  - all sixteen fullscreen, desktop, borderless and span-availability
    combinations for the span, with Yes's strength and the reason line's
    presence and text in each;
  - hidden rows without lists, and the shared editing guards;
  - cards and popups fitting with all eight displays at 1920×1080, 1280×720
    (100% and 125%), 640×480 (100% and 200% text) and 3440×1440 (200%), with
    a 48-code-point name at the large sizes, and eight rows visible before the
    list scrolls at 1920×1080; the span card, taller than the scroll body at
    640×480 with 200% text once its reason line shows, is revealed from its
    top, and the test fails if no card outgrows the body;
  - document recreation replaying no pick.
- Every SYSTEM page test fails when its locale argument names no language it
  covers, rather than passing with nothing run.
- The text-scale, choice-scroll and popup-placement tests add both rows to
  their choice lists, and `tools/tests/ui_retained_gate.py` holds the shorter
  missing list. The popup-placement test also gives the second display a long
  multi-word name, which the list wraps where it must and keeps on one line
  where it fits, opens a name with no spaces by breaking inside it, settles a
  rotated scroll body, and opens the stale list through a sequence of sizes
  and list lengths in one runtime ([wrapped options](text-scale.md)).
- The value runtime and schema tests cover the disabled-text strength of an
  unavailable option and the reserved label colour.

Evidence: `.tmp/ui/system-display-rows/validation-evidence.json`, SHA-256
`13a53dceb2a89b149ee7965bdfa1e43626f9049b8e78850cf2a2a0377bcaf0b4`.

Display Resolution and Refresh Rate followed in
[SYSTEM resolution rows](system-resolution-rows.md).
