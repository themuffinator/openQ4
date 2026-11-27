# Retained menu tooltips

This is the contract for the retained interface's tooltips: the document
schema, the runtime's timing, dismissal and placement, and the settings
pages that carry them. The visual design (section 7's small popover, the
section 8 `tooltip` token and WID-012) is in
[ui-visual-design.md](../ui-visual-design.md); the detail card it shares
its subject rules with is in [interaction.md](interaction.md#detail-cards).
The classic GUI (`ui_retained 0`) has no part in it.

## Schema and runtime

A document may define one tooltip (section 7's small popover, WID-012), and
any control except a scrollbar may name the text it explains itself with:

```json
"tooltip": {"state": "tooltip.text", "card": "tooltip", "enter": "tooltip.enter"}
```

A control's `"tooltip": "#str_233090"` is a localization key, or a string
expression whose result is one; an empty result names none. The subject is
chosen as the detail card's is, with three differences that keep the
tooltip out of the way: focus counts only once a menu input has moved it
(a page opening on its first control shows none to a player at the mouse),
a finger never has a subject, and an open choice list or a field being
typed in shows none.

The runtime waits 300 ms with the pointer on the subject, or 500 ms with
focus on it, then writes the control's text to `state`, which must be a
declared string, and plays `enter` when it names a timeline. While one
shows, or within 300 ms of one showing, the pointer reaching the next
control replaces it at once, without the entrance. Any press, menu input,
wheel step or input cancel hides it, and it stays hidden until the subject
changes. With no tooltip to show the state is empty, and bindings hide the
card.

The runtime places `card` by its measured box: 12 dp after and 15 dp below
the pointer, on the pointer's leading side before it would cross the
view's trailing edge and 8 dp over it before it would cross the foot; or
4 dp under the focused control, 21 dp in from its leading edge, and over
it near the foot. It always stays 16 dp inside the safe area, and it
follows the pointer while shown. The card must be a node below the root
holding no controls, not the detail card, and bindings and timelines
cannot own its `left` or `top`; the state cannot be the detail state. It
never takes the focus and should not take the pointer (`pointer-events:
none`). The host never resets the state. `UiDocumentTest` holds the schema
(`CheckTooltipSchema`) and `RetainedUiTest` the timing, dismissal and
placement (`CheckTooltip`).

The settings pages (GAME OPTIONS, SYSTEM, AUDIO, CONTROLS and their
Awakening copies) carry one, built by `tools/ui/retained_tooltips.py`:
every value row names a tooltip, generation fails on a row without one,
and a document whose card binds `ui_tooltips` hides it when the player
turns them off. Only those documents read it, so the Menu Tooltips row
(Game Options, Accessibility) is on the retained page alone: the stock
GAME OPTIONS page, which has no tooltips, owes it no row.
`tools/tests/ui_retained_gate.py` holds that exemption only while no engine
code and no stock GUI names `ui_tooltips`.

## Content

`tools/ui/retained_tooltips.py` holds the table of control ids and their
language keys (`#str_233001` to `#str_233186`, with `#str_233000` the Menu
Tooltips row's label) and builds the card: the section 7 small popover, a
black 0.94 body inside one `marine.rail.card` rail (`awakening.rail.card`
in The Awakening's copies, by its palette remap) with 8 dp cuts at the
top-trailing and bottom-leading corners and a second rail inside the top
cut, and the text in the settings rows' Lowpixel 14 dp face, wrapping at
300 dp. Its entrance is the `tooltip` token, 100 ms of opacity, with a
4 dp drift into place; reduced motion shows it at once.

Each tooltip says, in a sentence or two, what the option does and what it
costs or needs (a restart, FFmpeg, a DualSense). Key bindings carry none:
their labels already name their action. A new settings row needs a
tooltip in every shipped language, in the `*_openq4.lang` tables, or the
generator stops.

## Deviations from the visual design

- Keyboard and controller focus shows a settings row's tooltip, under the
  row, 500 ms after a menu input moved focus there. Section 7 hands
  controller players the detail area instead, but the settings rows have
  no per-row detail area; the longer delay and the keyed-focus rule keep
  it from appearing while a player steps through rows.
- Touch shows none. Section 13's long-press or information symbol for
  touch tooltips is not built yet.
