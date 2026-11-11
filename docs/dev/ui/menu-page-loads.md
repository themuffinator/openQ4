# Menu page loads, and a third menu round

A third round on 11 November 2026 measured every retained menu, removed the
wait when a page is first opened, and refined what a walk of the remaining
pages turned up. It follows the
[stock interface menus](legacy-menu-layout.md) and
[key glyphs and focus](key-bind-glyphs.md) records of 10 November.

## What the menus cost

`openq4-retained-ui-benchmark --screen` on each of the 29 menu documents
(`builddir`, 1280x720, best of three loads; `.tmp/menu-round/measure.py`):

- **A frame costs next to nothing.** Every page draws in 1 ms or less at
  rest (GAME OPTIONS 1.04 ms, Join Game 0.70 ms, the title 0.36 ms, most
  under 0.1 ms). There was nothing to gain there.
- **A load costs about 50 ms a megabyte.** The largest are the multiplayer
  card (14.7 MB, 704 ms), the match summary (8.5 MB, 408 ms), Join Game
  (6.0 MB, 359 ms), CONTROLS (5.5 MB, 266 ms), Create Server (206 ms) and
  GAME OPTIONS (183 ms); all 29 together take 3.9 s.
- **Most of a load is the parse and validation.** `--load` splits CONTROLS
  into 76 ms of JSON parse, 134 ms of validation and 81 ms for the view;
  Join Game into 113, 151 and 56 ms.

The nine pages the title reaches load at startup, and their parses already
ran on the job workers while the stock menus loaded
(`PrefetchRetainedScreens`). The pages that load when first opened did not:

| Page | First open, before | Now |
| --- | ---: | ---: |
| SETTINGS | 38 ms | 11 ms |
| CONTROLS | 268 ms | 87 ms |
| GAME OPTIONS | 184 ms | 54 ms |
| DISPLAY | about 122 ms (benchmark) | 40 ms |
| AUDIO | 42 ms | 15 ms |
| Single Player | about 33 ms (benchmark) | 6 ms |
| Campaign | about 41 ms (benchmark) | 21 ms |
| Campaign difficulty | about 38 ms (benchmark) | 9 ms |
| CHAPTERS | about 80 ms (benchmark) | 14 ms |

Measured in the engine with the new developer line (below), except where
the before figure is the benchmark's load of the same document.

## The page that shows starts the next pages' parses

`idSessionLocal::PrefetchRetainedNextPages` runs each frame and acts when
the page on screen changes. It hands the pages that page leads to, and that
are not loaded, to `UI_RetainedPrefetchDocuments`, which parses and
validates each on a job worker. The load then takes the finished parse
(`UI_RetainedTakePrefetched`) and waits for its view alone.

| Page shown | Parses |
| --- | --- |
| The title | Single Player, SETTINGS |
| Either pause | SETTINGS |
| Single Player | Campaign |
| Campaign | Campaign difficulty, CHAPTERS |
| SETTINGS | CONTROLS, GAME OPTIONS, DISPLAY, AUDIO |

- Nothing parses for a page that is loaded, that the stock screen stands in
  for, or whose parse is under way.
- Nothing parses at startup for these pages: the title never waits for
  them, and a player who never opens the settings pays for none of it.
- A parse no page takes goes with the next level's unload
  (`UI_RetainedPrefetchClear`), as before.
- SETTINGS left the startup list. Its startup parse was dropped unused
  after the preload, and the page was then parsed again when first opened.

A page opened sooner than its parse finishes waits for the worker, which is
never longer than parsing it in place.

`developer 1` now prints one line per retained document load:
`retained GUI <path> loaded in <N> ms (parse prefetched | parsed here)`.

### Considered and left

- **Hoisting repeated vector art into `symbols` in every document.** The
  repeated paths are 7.8 MB of the documents' 50 MB of compact JSON (24 % of
  Join Game, 16 % of the multiplayer card), so a load would gain about
  5 to 10 %, and three tests read a node's `paths` directly. Not worth the
  change.
- **Loading fewer pages at startup.** The startup pages precache their
  pictures inside the load so none arrives late; deferring them would
  trade a shorter startup for late picture loads.

## Focus

Walks with Down, Right and Left in the engine (`openq4_retainedGui menu`),
on the pages the second round had not walked: the Multiplayer page and
Create Server, Player Setup, Single Player and Arena, the pause, Load Game
and the title. One fault:

- **Right from a menu plate reached EXIT.** On the pause and the title the
  plates author Up and Down only, so Right or Left went looking for the
  nearest control that way and found a link on the plinth under them. The
  plates of a menu over a plinth now name themselves for Left and Right
  (`wrap_menu( ..., edges=True )`), which the runtime treats as an edge.

Everything else stepped as the pages read. Noted and left: on Create
Server, Down from BACK wraps into the map list; on Player Setup, Down from
Model passes PREVIOUS and NEXT, which Left and Right on the Model row do.

## Section headings

On the settings sub-pages a column's heading (IMAGE QUALITY, RENDERING) and
a page's note (CONTROLS' hint) started at the column's edge, before the
strip's backing and the header band's cut, so their first letters ran over
the backing's leading end. They now start where their rows' labels do,
21 dp in (`HEADING_INDENT`, `tools/ui/retained_settings_rows.py`), on
DISPLAY, CONTROLS, GAME OPTIONS and AUDIO, in the two-column and the
single-column layouts alike. A heading's text room shrank by the same
21 dp, and every language's headings still fit (`check_settings_text`).

## Labels

More English labels in sentence case beside Title Case rows now match them:
Create Server's Use Password, Set Password, Remote Console Password, Allow
Server Side Mods, Weapon Stay and Map Cycle Script File; Player Setup's
Model Rotation; and CONTROLS' Touch Layout on Android. The multiplayer
card's action names (Vote yes, Set ready) are in sentence case throughout
and stay so.

## Evidence, 11 November 2026

Hidden window, private save path, console verbs only; the kit is in
`.tmp/menu-round/`, the runtime `.tmp/runtime/ui-menus`.

- The benchmark of every document: `measure-before.json`.
- First-open times: `t2/s-load-times-inline` (the settings pages opened
  from the stock screen, parsed in place), `t3/s-load-times` and
  `t3/s-load-times-sp` (with the parses started by the page shown).
- Fourteen pages captured for review: `t1`, with `sheet-a.png` to
  `sheet-c.png`.
- The headings and note at the label inset on the four settings pages:
  `t6/headings.png`; before, `t1/sheet-c.png`.
- Focus walks: `t4/s-walk-create`, `s-walk-setup`, `s-walk-sp`,
  `s-walk-load`; the pause and title before (`t4/s-walk-pause`) and after
  the edge (`t5/s-walk-pause`, `t5/s-walk-title`).

Tests: `ui_retained_adapter.py` (the harness the adapter's tests share),
`ui_retained_gate.py` and the native `openq4-ui-retained-*` tests. Every
document was regenerated by the full `build_retained_screens.py`; the title
and the two pauses changed with the edges, the four settings pages with the
heading inset.
