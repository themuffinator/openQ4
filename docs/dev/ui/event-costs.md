# Event costs, and a fifth retained UI round

A fifth round on 21 November 2026 measured what a retained document costs
when something happens on it: a player's input raising an event (a focus
move, a tab, a dialog), and the game changing a readout. It follows the
[menu frame costs](menu-frame-costs.md) round of 20 November, which measured
frames at rest and while the menus animate.

## What an event cost

`openq4-retained-ui-benchmark --event <document> <event> [repeats]` settles a
document, then runs the event repeatedly with a frame after each, and prints
the event's own time (`event_p50_ms`) and the frame's (`frame_p50_ms`).
`--screen` now also prints `event_ms` for the event it runs, and
`--state <document> <key> <a> <b> [frames] [event]` changes a state value on
every frame, as the game changes a HUD's clock or health, and prints what
those frames cost.

- **An event's cost grew with its document.** Moving the focus down a row
  of Join Game took 1.5 to 2.9 ms, a CONTROLS tab 2.5 to 5.0 ms and the
  multiplayer card's (the Escape card's) opening 4 to 9 ms, depending on
  the machine's load, before the frame that shows it. Every arrow press on
  a list paid it.
- **Most of it copied the state.** An event evaluates a copy of the
  document's state, so that a rejected event leaves the page as it was.
  The copy duplicated every bound property, state value and control
  readback, several times an event: once on entry, once more for the
  program, and once for every presentation write it made, and then freed
  the copies it did not keep. On the Escape card that was about two
  fifths of an event.
- **The rest searched the page for what can take input.** Each interaction
  update (an event's, and up to five in a frame that settles focus or
  layout) asked every branch of the page that holds a control for its
  `display` and `pointer-events`, building both property names and
  searching the bound and animated tables, which on the Escape card hold
  tens of thousands of entries under long shared prefixes.
- **Lists repainted through formatted strings.** After a state change each
  list's rows (selected mark, highlight, availability) check that the
  element still holds what was written. The check formatted the element's
  property as text and built the table key's strings, for every row of
  every list.
- **A HUD readout costs little.** A health or clock change on the
  Remastered HUDs takes 0.1 to 0.4 ms including its frame, so the HUD was
  left as it is.

## What changed

- **A state's copies share its tables until one writes.** The bound
  properties, state values, control states, readbacks and presentation
  cells are each held once and shared by every copy (`SharedOnCopy` in
  `State.h`), as the motion's values already were. A copy costs a few
  references; the first write in a copy clones only the table it changes,
  and a binding whose value did not change writes nothing. A page's initial
  state is shared with its document the same way, and the transactional
  copies a frame makes when a CVar it reads changes are as cheap.
- **Input eligibility is read once per state and motion.** Each branch keeps
  its two property keys from the load, reads the presented values in place,
  and remembers whether they let input through; an update at the same state
  and motion revisions reuses that and reads only the masks, whose area
  follows layout. The table it fills is overwritten in place instead of
  rebuilt.
- **List rows compare RmlUi values.** A row's last write keeps the
  element's resulting property and compares it as an RmlUi property; the
  table of writes is searched without building key strings.

Behaviour is unchanged: the same event commits or is rejected, with the
same state, focus and presentation.

## Results

Best of three interleaved runs of each build (`.tmp/rui-round/ab_events.py`,
`builddir-perf`, debugoptimized, a shared machine), 40 repeats each:

| Page, event | Event, before | Now | Its frame, before | Now |
| --- | ---: | ---: | ---: | ---: |
| Escape card, `onActivate` | 4.24 ms | 0.34 ms | 2.20 ms | 0.45 ms |
| Escape card, ban dialog `adminBanModalShow` | 4.71 ms | 2.33 ms | 7.82 ms | 4.75 ms |
| Join Game, a row's focus `joinFocus3` | 1.54 ms | 0.04 ms | 0.39 ms | 0.13 ms |
| CONTROLS, `tab_mouse` | 2.47 ms | 0.11 ms | 1.08 ms | 0.35 ms |
| GAME OPTIONS, `tab_hud` | 0.64 ms | 0.10 ms | 0.48 ms | 0.25 ms |
| DISPLAY, `onTabNext` | 1.04 ms | 0.78 ms | 3.47 ms | 2.91 ms |
| Create Server, a ban row `banRow3` | 0.90 ms | 0.08 ms | 0.54 ms | 0.25 ms |
| Match summary, `statistics_a1` | 1.40 ms | 0.05 ms | 0.25 ms | 0.06 ms |
| Save Game, a row's focus `browserFocus3` | 0.40 ms | 0.02 ms | 0.12 ms | 0.05 ms |
| Pause, `onActivate` | 0.15 ms | 0.01 ms | 0.08 ms | 0.06 ms |

The machine was quieter for these runs than for the first measurements
above, which put the same events at up to twice these before-times. The
ban dialog's event and frame repeat its opening on every repeat (its
timelines restart), and DISPLAY's frame lays out the tab it shows.

The HUD's own events fell the same way: the multiplayer HUD's transmit
event from 0.34 to 0.014 ms, the Marine HUD's health update from 0.12 to
0.013 ms, the scoreboard's join from 0.32 to 0.006 ms.

In the engine (`ui_retainedProfile 240`, hidden window, OpenGL, 1280x720)
frames are as the fourth round left them: the title's document 0.61 ms at
rest and 0.76 ms with its EXIT dialog, the pause 0.40 ms and 0.74 ms with
its quit dialog. The pause, SETTINGS sub-page, Join Game, Create Server,
Escape card and admin scenarios completed with no refused operation, no
retained warning and no stock fallback.

## The first animated frame

The motion kept its live values as one map: the authored values until the
first sample wrote one, then a private copy of all of them. Every page that
animated (its opening timeline, a focus glow) therefore held two copies of
its authored values from its first frame on, and that frame paid for the
copy.

- **The live values are the writes over the authored values.** A motion
  holds only the entries its samples and writes replaced; `Motion::Values()`
  returns a view (`MotionValues`) that finds a written entry before the
  authored one and walks every authored key once, in order, with its live
  value, so ApplyMotion's complete pass still reads the authored values in
  step. A write can only replace an authored value, as before, so both maps
  keep the same key order. Transactional copies share the written entries
  and clone only those.
- **Results.** What a shown page holds after its frames
  (`--screen` now prints `held_kb`), best of three interleaved runs:

| Page | Held before | Now |
| --- | ---: | ---: |
| Escape card | 269 MB | 254 MB |
| Match summary | 147 MB | 137 MB |
| CONTROLS | 164 MB | 157 MB |
| Join Game | 92 MB | 89 MB |
| Awakening pause | 119 MB | 118 MB |

  The first frame's time changed within run-to-run noise (the copy was a
  few milliseconds at most), and frames at rest and the engine's pause
  profile are unchanged. `openq4-ui-document` now checks the view's order
  and lookups.

## Found, not fixed

- **The view build is still the main thread's part of a load** (the Escape
  card about 300 ms, CONTROLS about 200 ms in the benchmark); page warming
  runs it while the previous page rests. Its samples are spread across
  element creation, inline property templates and hashing, with no single
  cost to remove.
- **A state change re-evaluates every control readback** on pages with
  value controls (`EvaluateDerived`), which keeps DISPLAY's tab change near
  a millisecond.

## Evidence, 21 November 2026

The kit is in `.tmp/rui-round/`: `loads.py` (every document's load split),
`ab_events.py` (the interleaved event A/B), `ab_cold.py` (first frame and
held memory), `baseline.sh` (builds the before benchmark from HEAD's copies
of the changed sources), `subtree.py` (ranks cdb stack samples under a
function), the samples (`samples-*.txt`), and the engine runs (`v1`, `v2`,
and `v3` after the motion change; `retained.py` with the previous round's
`extra.py` scenarios) on the runtime `.tmp/runtime/ui-menus`.

Tests: 165 of the 174 native UI and HUD tests in the MSVC environment,
including the transaction canaries (`openq4-ui-presentation-runtime`,
`openq4-ui-modal-runtime`, `openq4-ui-choice-scroll-runtime`,
`openq4-ui-focus-reveal`, `openq4-ui-scrollbar-runtime`) and every
retained screen group. `openq4-ui-state` now checks that a copy's write
never reaches the original, nor the original's the copy. The other nine
are renderer image and upload script tests whose stubs fail to compile at
this HEAD (`idBinaryImage::fileDepth`, from the volume and array image
change); they build no retained UI source.
