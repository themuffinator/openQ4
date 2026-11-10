# Key glyphs on the Controls pages, and menu focus that steps as it reads

A second menu round on 10 November 2026 took the settings tree further in
both interfaces. The retained CONTROLS page draws each bound key as a glyph
where it printed key names. The stock page's key rows are legible. An action
keeps its controller buttons when a keyboard key is bound to it. The stock
menus step their focus in the order a page reads, scroll a settings page to
the row that takes the focus, and no longer hold the arrow keys on a key row,
a number field or a scroll bar. It follows the
[stock interface menus](legacy-menu-layout.md) record of the same day.

`docs/dev/ui/retained-screens.md` and `docs/dev/ui/document-format.md` are
bound by hash in the UI register and were not edited in this round. This
record is the current description of the CONTROLS page's rows and of the
document format's `symbols`, and supersedes the older text in both until the
register is next revised.

## The retained CONTROLS page

Each row named its action's keys as text, joined by a dot: `W · UPARROW`,
`SPACE · INS · JOY3 · JOY19`. A controller button read as its key code.

A row now draws its keys ([visual specification](../ui-visual-design.md)
sections 13.4 and 13.5):

- **Keyboard keys** are the prompt bar's keycap: the key's name in dark
  Lowpixel on a white plate with the 45-degree cut. The four arrow keys draw
  their arrow.
- **Mouse buttons and the wheel** draw the mouse with the bound button dark,
  the wheel with an arrow for a turn up or down, or a side button's number.
- **Controller buttons** draw the glyph of the controller in use, in the
  prompt bar's white with dark marks: a face button's letter or PlayStation
  symbol (by position when the family is unknown), bumper, trigger, d-pad
  arm, stick, menu, view, home, touchpad, paddle or another labelled button.
  `in_promptStyle` changes them as it changes the prompts.
- A row shows up to two keyboard or mouse keys, then up to two controller
  buttons a little apart. `+N` counts any further keys, and a dim dash
  stands for an action with none.
- While the page waits for a key the row's glyphs give way to
  `Press any key...` in the focus colour, and Accept leaves the prompt bar,
  since the next key is the answer.
- On a row, the prompt bar's Accept reads CHANGE (`#str_231766`, each
  language's word from the stock controls hint).
- A tab's keys start past its labels on one line where the keys keep their
  room (`keys_column`), so `Toggle Ready Status` no longer wraps in English.

A third legend, CLEAR, was tried in the prompt bar and left out: Accept would
start 36 dp before the bar in English, and German's CANCEL ends 3 dp past it.
The heading line still says how to clear and cancel.

### Slots

The adapter fills a slot per key (`idUserInterfaceRetained::SetKeyBindingNames`).
Beside a state with a `keyBinding`, a document may declare, all optional:

| State | Holds |
| --- | --- |
| `<state>.key0`, `.key1`, ... | The action's keyboard and mouse keys in key order, each a key state: with `<slot>.glyph` beside it, it receives the glyph's kind and its label, as a HUD prompt's key does (`PromptGlyph`), and nothing past the last key |
| `<state>.pad0`, `.pad1`, ... | Its controller buttons, the same way |
| `<slot>.arrow` | 1 to 4 for the keyboard's up, down, left and right arrows, else 0 |
| `<state>.keyCount`, `<state>.padCount` | How many keys of each family the action has |
| `<state>.more` | `+N` for the keys past the declared slots, else empty |

`tools/ui/retained_key_glyphs.py` builds the chips. A chip is sized in em of
its strip, whose font size is the chip's height, so a page that grows its
rows (a tall view, a larger text size) grows one length and every chip
follows.

### Shared symbols

A page repeats every glyph in each of its slots: 49 rows of four slots, 36
glyphs a row. Carried in every node, the art made the document 19 MB. The
document format now has shared vector art:

- a top-level `symbols` object maps an ID to a path array;
- a vector node may name one with `symbol` in place of its own `paths`;
- a node may not have both, and an unknown symbol refuses the document.

The parser copies the symbol's paths into the node, so nothing after the
load differs. `Document.symbols` in `tools/ui/build_retained_screens.py`
emits them. The page is 5.6 MB with 4,627 nodes and 5,247 bindings and loads
in 272 ms on the benchmark where GAME OPTIONS loads in 203 ms
(`openq4-retained-ui-benchmark --screen`, `builddir`); a frame at rest
measures the same as before.

## Two keys of each family

Both interfaces kept two keys on an action by counting every key together
(the stock `bindunbindtwo`). Jump ships with Space, Insert and two controller
buttons, so binding any key to it cleared all four. `idKeyInput::BindForMenu`
keeps two keyboard or mouse keys and two controller buttons: a third key of a
family replaces that family's two and leaves the other's. Both CONTROLS
pages use it. The console command `bindunbindtwo` is unchanged.

## The stock CONTROLS page

- **No word between keys.** The menu's key list joined its glyphs with the
  language's ` or `, which cost a row a glyph's width each time: Jump's
  fourth glyph was cut at the field's edge. A row now lists its glyphs with
  a space (`KeysFromBindingForMenu`); a HUD hint keeps the word between its
  alternatives.
- **Long key names are legible.** A keycap was capped at 2.2 heights, and
  `LEFT ALT`, `RIGHT ALT` and `LEFT SHIFT` were squeezed into it. In a
  settings row the cap takes the width its name needs, up to four heights
  (`idDeviceContext::SetKeyBindingWideLabels`, set by `idBindWindow::Draw`);
  an inline hint keeps the compact bound.

## Focus in the stock menus

The first round's arrow keys were verified by a compiled test only. Walking
the pages in the engine (below) found four faults, all fixed:

| Fault | Now |
| --- | --- |
| The arrows never left a key row: the first round kept every arrow from a focused `bindDef` | A key row steps like any control. While it waits for its key the GUI gives the row the next key before the desktop sees it, so nothing reaches the step |
| The arrows never left a one-line number field (Brightness's, the window sizes') | Up and Down step from a one-line field; a field of several lines keeps them for its scroll |
| Focus stepped in file order: on the stock DISPLAY page V-Sync followed Ambient brightness and Borderless Window followed Resolution Scale | Children step column by column from the leading edge, each column in reading order (`SortFocusOrder`): windows whose horizontal extents overlap form a column, so a settings row's value and its button stay together while Arena's tiers, matches and details are three columns |
| A settings page scrolled only by the wheel, the bar or the section list, so focus stepped onto rows outside the view; on the scroll bar itself Up and Down moved the page and never left | A focus that moves to a row outside the view scrolls the page just far enough to show it (`ScrollMainMenuSettingsFocusIntoView`). A page's scroll bar is no step. Only a change of focus scrolls, so the wheel and the bar can still move the page away from a focused row |

`openq4_guiFocus [next|previous]` steps a classic GUI's focus as the arrow
keys do and reports the focused window, its rectangle and whether it is
shown. It raises the same event inside the engine and touches no device.

## Focus on the retained settings pages

`.tmp/menu-round/navcheck.py` reads every retained menu document's authored
navigation: no map names a missing control. Walking each settings page with
Down, Right and Left in the engine found one fault:

- **Right from the trailing column reached a tab.** A row with no value to
  change (a number field, a button) has no target past its column, so the
  runtime looked for the nearest control in that direction and found the
  next tab's header. A settings page's columns now hold the focus at their
  outer edges: a row names itself there, and the runtime treats a control
  that names itself in a direction as an authored edge
  (`Interaction::Navigate`).
- **Left towards a hidden row reached a tab too.** On the SCREEN tab Window
  Width stands beside the display list, which a machine with one display
  hides. A step across to a hidden row now lands on the row its column shows
  in its place, the next one below it or else above
  (`Interaction::Authored`): Left from Window Width reaches Fullscreen.

## Labels

English setting labels that were in sentence case beside Title Case rows now
match them, on both DISPLAY pages and GAME OPTIONS: Rendering Backend,
Rendering Device, Restart Renderer, UI Scale, Text Size, Reset UI Sizes,
Enable Shadows, Enable Specular, Enable Bump Maps, Enable Detailed Sky,
Force Ambient Light, Ambient Brightness, Cinematic Shake and Wheel Offset.
Other languages follow their own conventions and are unchanged.

## Found, not fixed

- **DISPLAY in the retained interface still opened the stock page.** Fixed
  later the same day: a Performance Preset now applies on the retained
  DISPLAY page through the
  [combined executor](settings-effect-execution.md#combined-executor), so the
  page joined the `ui_retained` gate and SETTINGS > DISPLAY opens it
  ([the gate](system-resolution-rows.md#the-gate)).
- **The stock DISPLAY page's Section list** names its sections Video, Window,
  Rendering, Quality, Post FX and Sizing, which are not the page's headings
  (GRAPHICS, SCREEN, IMAGE QUALITY, RENDERING, ADVANCED RENDERING, DISPLAY
  SIZING), and its jumps land between headings. With one display the page
  scrolls in 19.5 u steps under rows on a 22 u pitch, so a row is often cut
  at the view's top. Both follow from one table serving two layouts (one
  display, several) and need the several-display layout to verify.
- **A third keyboard key replaces both.** As in the stock menu, the new key
  does not push out only the older of the two; the menu does not know which
  that is.
- Hungarian still names the Arena's SKILL and DIFFICULTY rows alike.

## Evidence, 10 November 2026

Captures use a hidden window, a private save path and console verbs only
(`openq4_guiAction`, `openq4_guiFocus`, `openq4_retainedGui`,
`testControlsKey`, `screenshot`), with no operating-system input. The kit is
in `.tmp/menu-round/`, the runtime `.tmp/runtime/ui-menus`, built from
`builddir`.

- Retained CONTROLS, every tab, a wait for a key, a side mouse button bound,
  a third keyboard key on Jump (Space and Insert give way to F, the two
  controller buttons stay) and a row cleared: `s8/s-controls`. Before:
  `r3-retained/controls`.
- Stock CONTROLS, every tab: `s6/l-controls`; before, `final-legacy/l-settings`.
- Stock focus walks (`legacy.py`, `walklog.py`): title, Single Player and
  Arena, Multiplayer, CONTROLS, DISPLAY and GAME OPTIONS (`s6`, `s5`). The
  DISPLAY walk reaches Custom FS Height with the page scrolled to 28; GAME
  OPTIONS reaches its 61st step. Before the fixes: `s2/l-walk-controls`
  (stuck on Move Forward), `s3/l-walk-display` (stuck on Brightness's
  number, then on the scroll bar).
- Retained focus walks: the title and SETTINGS (`s7/s-walk-hub`), DISPLAY's
  three tabs (`s10/s-walk-display`), GAME OPTIONS' five and AUDIO (`s9`).
- Both CONTROLS pages in German, from the packed build: `s11-de`.

Tests: `key_bind_presentation.py`, `menu_controller_navigation.py` (now
covers a key row, a one-line field, the focus order and the scroll bar),
`ui_retained_gate.py`, `settings_menu_coverage.py`,
`lang_table_encoding.py`, and the full `build_retained_screens.py --check`.
