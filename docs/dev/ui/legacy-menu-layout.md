# Stock interface menus in the new layout

The stock interface (`ui_retained 0`) shows the stock menu GUIs, and so does
the retained interface whenever one of its screens cannot present. Since
10 November 2026 the menu pages openQ4 added to that interface are laid out as
their retained counterparts, in the stock pages' own frame and bitmap
vocabulary ([visual specification](../ui-visual-design.md) sections 6, 9 and
13.7). The stock Quake 4 pages (the title, the pause, Load and Save Game,
Settings, Create Server, the server browser and Player Setup) already shared
that frame, since the retained pages were reconstructed from them. This
record covers what changed, what stays different on purpose, and how it was
measured. It is the menus' counterpart to the
[stock interface HUD layout](legacy-hud-layout.md).

## The page frame

Every page below the home screen stands in one frame, in both interfaces:

- the framing bands docked (the top band at -77,-63 u and the bottom band at
  -26,386 u, the stock sub-page rectangles), with the stock tiles carrying
  them across wide views;
- the page's title in the top band's slot at 39,19 u (`fonts/marine`, 0.25,
  white at 0.5), with the path to it over the title at 39,7 u (0.17, white at
  0.35) on pages below the first level;
- sections on the stock navigation plates (`b1_dark`, the corner marker and a
  `fonts/marine` 0.31 label) from 181 u on the stock 30 u pitch, with the
  stock hover (plate to 1, label and marker to orange, 300 ms back);
- the subject's summary in the content column at 395,186 u (`fonts/lowpixel`
  0.27): the page's own description, or the hovered or focused section's;
- actions on the stock `b6_light` plate with its corner marker;
- BACK on the stock `b4_light` plate in the bottom band's raised section
  (532,441 u), where every stock page has it;
- the retained pages' backdrop: black, the stock `screen` light band across
  the middle at 0.64 and the loading grid at 0.16, the same at every width.

Before this round the Single Player, Campaign, Arena and Demos pages kept the
home position's bands, a centered headline under an orange rule, bordered
cards and boxes, BACK at the bottom-leading corner without its marker, and a
backdrop that faded to black at the edges of a wide view.

## Pages

| Page | GUI | Now |
| --- | --- | --- |
| Single Player | `guis/arena_menu.gui` (`arena_view 0`) | CAMPAIGN and ARENA on section plates, with the retained page's description and summaries (`#str_230044`, `#str_231552`, `#str_231553`) |
| Campaign | `guis/campaign_menu.gui` (`campaign_view 0`) | QUAKE 4 and THE AWAKENING on section plates. The Awakening's plate is dim while it cannot start, and its summary says why |
| A campaign's page | `guis/campaign_menu.gui` (`campaign_view 1`) | The path over the campaign's name, the stock difficulty list, START GAME, TURBO MODE and CHAPTERS, and the chapter list |
| Arena | `guis/arena_menu.gui` (`arena_view 1`) | The retained Arena page's arrangement: tiers and the tier's matches as plates (a larger one for the boss), the progress and RESET PROGRESS under the tiers, the tier briefing under the matches, and the chosen match's levelshot, details, difficulty and START MATCH in the trailing column |
| Demos | `guis/demo_menu.gui` (browser) | MULTIPLAYER over DEMOS, the filters as small cut tags over the list, the selected demo's details and PLAY, DELETE and REFRESH in the leading column, the list in the trailing one |
| Multiplayer | `guis/mainmenu.gui` | JOIN GAME, CREATE SERVER, DEMOS and PLAYER SETUP, the retained page's sections in its order |
| Title | `guis/mainmenu.gui` | SINGLE PLAYER, LOAD GAME, MULTIPLAYER and SETTINGS; DEMOS moved to the Multiplayer page |

Window names, visibility conditions and commands are unchanged, so the
session's verbs, the focus scripts and the page tests still address the same
windows. The Arena's launch, result, progress and reset presentations are
untouched.

- **Multiplayer page.** The stock FRIENDS LIST led to a page no service
  fills. Its plate (`mp_b_friendslist`, which keeps the name the menu's
  scripts use) now reads DEMOS and opens the demo library over the page; Back
  there returns to the Multiplayer page. CREATE SERVER and DEMOS swap plates
  so the order matches the retained page, and the two windows swap places in
  the file, because focus steps through windows in file order.
- **Demos list.** Its rows still carry six columns. The status column stands
  past the list's trailing edge, since the selected demo's details show it.
- **Sizes.** Label boxes were sized for the longest translation in the twelve
  shipped languages (`.tmp/menu-round/lengths.py`): the Arena's progress line
  at 0.16 on one line, its path on two, and the Demos tags at 0.17 across the
  list's width.

## Engine changes

- **Quoted choices.** The stock tables quote their choice lists
  (`'Off';'2x';'4x'`), and the engine quotes the names it lists itself
  (`"Default Device (...)"`), as the retail lexer read them. openQ4's own
  splitter kept the quotes, so the stock SYSTEM page showed MSAA as `'Off'`
  and the AUDIO page's device with a leading quote. `SplitChoiceList`
  (`src/ui/ChoiceWindow.cpp`) now shows a choice that is wholly inside one
  pair of quotes without them, keeps a separator inside the quotes, and
  leaves an apostrophe that begins a plain word alone.
- **Arrow keys.** In a menu GUI the arrow keys step the focus as the D-pad
  does and light the item they reach (`KeyboardMenuFocusDirection`,
  `src/ui/Window.cpp`). The stock menus answered only Tab, and Tab showed
  nothing. A key the focused control uses itself stays with it: a list's
  rows, a slider's or choice's value and an edit field's caret. Only a
  menu's desktop window turns arrows into steps, so in-world GUIs and HUDs
  are unaffected. A [second round](key-bind-glyphs.md#focus-in-the-stock-menus)
  the same day walked the pages in the engine and fixed four faults this
  left: the arrows held on a key row and on a number field, focus stepped in
  file order, and a settings page did not follow the focus.
- **Demo filters.** Every filter looked chosen, because the GUI compared the
  filter's name and a GUI expression compares numbers only. The session
  publishes `demo_filterIndex` (0 All to 4 Incomplete) and the tags' rails
  read it.

## Retained fix found in the same pass

- **Join Game.** With no scan running, the scan's fill drew at full width
  over its track, twice as bright as the stock track. The page carries a
  transform, so its boxes clip through a clip mask rather than a scissor,
  and a box with no width has no mask geometry: at no progress the fill's
  clipping group showed the whole tube. The fill leaves the page until the
  scan reports progress. The loading screen's bar is built the same way
  but stands under no transform, and measured the same at no progress with
  and without the change (`x3-before`, `x3-after`), so it is unchanged.

## Deliberate differences

- **Pause.** The stock pause keeps the stock order (SAVE GAME, LOAD GAME,
  QUIT CURRENT GAME, SETTINGS, DIFFICULTY, RETURN TO GAME) and the title's
  wordmark. The retained pause leads with RESUME and shows the level block.
  Reordering the stock plates would also need the focus order, which follows
  the file, to move with them.
- **Settings.** The stock screen keeps its categories beside the open
  category; the retained one opens on the list of categories and gives each
  its own sub-page.
- **Campaign.** The stock interface chooses the campaign on one page and the
  difficulty, Turbo Mode and CHAPTERS on the campaign's own page. The retained
  one offers NEW GAME and CHAPTERS beside the campaign choice.
- **Prompt bars and the detail card** are retained constructions. The stock
  pages keep BACK alone in the raised section.
- **Art.** The stock pages use the stock bitmaps (`b1_dark`, `b6_light`,
  `icon_locked`), so plates are lighter and the lock keeps its dark tile.
- **Band tiles.** The stock band tiles show faint seam lines at the leading
  edge of a wide view on every stock page, the stock ones included.

## Follow-up, 10 November 2026

Three items the pass first left open, fixed the same day:

- **The retained gate.** `tools/tests/ui_retained_gate.py` failed on main
  since `d81433266`: the retained GAME OPTIONS page gained
  `hud_crosshairAnimated`, and the gate expects every retained row on the
  stock page. The animated crosshair draws only under the retained interface
  (`rvAnimatedCrosshair::Enabled`), so the stock page owes it no row. The
  gate now admits a setting listed as retained-only, provided the game gates
  it on `ui_retained` and the stock page has no row for it; every other
  setting still needs its stock row.
- **The DISPLAY page's Interface heading** was mixed case beside upper-case
  headings. `#str_230018` is upper case in all twelve tables, as every other
  section heading on the settings pages is.
- **Crosshair Size and Crosshair Color** ended in the colon of the stock
  rows' labels, which no other retained row has. The retained rows read
  `#str_231764` and `#str_231765`, each language's stock label without the
  colon; the stock page keeps its own.

Captures: `y1/settings-system` and `y1/settings-subpages`.

## Found, not fixed

See also the [second round's list](key-bind-glyphs.md#found-not-fixed).

- Hungarian names both the Arena's SKILL and DIFFICULTY rows NEHÉZSÉG.
- The retained Join Game's JOIN SERVER, SPECTATE and favorite plates end at
  one trailing edge, so their leading edges differ. That arrangement was
  chosen with the stick prompts and is unchanged.

## Evidence, 10 November 2026

Captures use a hidden window, a private save path and console verbs only
(`openq4_guiAction`, `GuiEvent`, `openq4_retainedGui`, `screenshot`), with no
operating-system input. The kit is in `.tmp/menu-round/`:

- `legacy.py` walks the stock menus (`ui_retained 0`) and `retained.py` runs
  `tools/ui/capture_retained_screens.py` scenarios with longer waits for a
  loaded machine. Both use the fixed runtime `.tmp/runtime/ui-menus`, built
  from `builddir`, with the edited GUIs synced in as loose files (`sync.py`).
- Pairs at 1280x720: `r1-legacy`, `r1-legacy-sp` (before) and `final-legacy`
  (after) against `r2-retained` and `r3-retained`. The rebuilt pages were
  also captured at 1024x768 and 1720x720 (`t7-43`, `t7-219`), in German
  (`final-de`, `final-de2`) and in Hungarian (`final-hu2`).
- The stock SYSTEM page reads `MSAA Off` and the AUDIO page
  `Default Device (OpenAL ...` (`t6/l-settings`); the Demos filters show one
  rail, which follows `demoFilter render` (`t6/l-demos`); Back from Demos
  returns to the Multiplayer page.
- Join Game's idle track measures (76,45,5) at its brightest row against the
  stock track's (76,42,6); before the fix it measured (146,76,3)
  (`x1`, `x2`).

Tests: `arena_campaign.py`, `campaign_chapters.py`, `demo_playback.py`,
`mvd_navigation_contract.py`, `choice_gui_value_contract.py` (now compiles
the production splitter) and `menu_controller_navigation.py` (now covers the
arrow keys).
