# Menu frame costs, and a fourth menu round

A fourth round on 20 November 2026 measured what every retained menu costs
a frame while its animations play, not only at rest, and removed the costs
it found. It follows the [navigation hitches](menu-navigation-hitches.md)
round of 12 November and the [page loads](menu-page-loads.md) round of 11
November, whose benchmark measured each page settled and found every one
under a millisecond.

## What the menus cost while they move

`openq4-retained-ui-benchmark --screen <document> 240 1280 720 <event>`
runs each document's opening event (`open`, `onActivate` or `returnHome`)
and then 240 frames (`.tmp/menu-round-4/events.py`). The benchmark host has
no GPU, coverage atlas or linear masks, so its times are the runtime's CPU
work alone; the engine's own `ui_retainedProfile` measured the rest (below).

- **The Awakening pages recompiled their snow every frame.** The pause, the
  SETTINGS hub and its four pages draw two layers of snow (1,262 flakes, each
  a path) drifting along the artwork's 13-degree line. A vector moving by
  translation alone compiles its coverage at quarter-pixel phases and keeps
  eight of them, but a diagonal drift visits all sixteen, so nearly every
  frame missed and compiled the layer again: 7.5 to 8.6 ms of a 9.9 to
  12.7 ms frame, against 0.04 ms for the Quake 4 pause.
- **They drew a path at a time.** Each flake and each frost ribbon was its
  own draw: about 2,130 draws a frame at rest on those pages, against 41 to
  162 elsewhere.
- **An open dialog compiled its glow every frame.** The two pulses that run
  round a dialog's outline turn under a mask of two wedges, which the runtime
  tessellated again on every frame (2 to 2.25 paths a frame in the engine).
- **An open dialog relaid its page out every frame.** The glow's halo and its
  falling wave pulse their opacity. The paint-only opacity update took only
  text and image leaves, so every other frame restyled and laid out the whole
  page behind the dialog (`layout_updates_mean` 1.00; 0.3 to 0.5 ms on the
  title and the pause, more on larger pages).

Nothing else moved at a cost: every other page's opening and resting frames
stay under about 1 ms, as in the third round.

## What changed

- **A long slide compiles at half-pixel phases.** After eight consecutive
  renders that moved by translation alone, a vector compiles at half-pixel
  phases: four, which its cache of eight holds, so a steady drift compiles
  again only as it crosses into another cache region. A short move (a step,
  a settle, a band's first frames) keeps quarter-pixel phases, and a vector
  at rest compiles exactly (`SlidingHalfPhaseRenders`,
  `src/ui/retained/VectorElement.cpp`).
- **A vector of many paths is a draw or two.** A vector of 16 paths or more
  uploads its consecutive meshes that draw alike as one geometry (normal and
  additive paths alike; multiply paths apart), up to RmlUi's 262,144 vertices
  a geometry. Paths still paint in source order: a draw's triangles blend in
  order. A smaller vector keeps a geometry per path, because a layer reads
  "more than one draw" as a compound plate whose isolated alpha it may cache.
- **Each glow pulse turns under one wedge.** The dialog's glow
  (`modal_energy`, `tools/ui/build_retained_screens.py`) draws its outline
  twice, each copy under a mask of one convex wedge with its middle stop on
  a whole 8-bit level (128/255 for 0.5), which the renderer draws as a GPU
  linear mask instead of tessellating it. The pulses look as before.
- **A vector leaf fades without a layout pass.** A vector node with paths and
  no children, mask, control, text or image now takes the same paint-only
  opacity update as a text or image leaf: its isolated layer reads the live
  opacity when it composites, so its overlapping paths still take the alpha
  once. The native test `CheckVectorFadeReplay` (`RetainedUiTest.cpp`) fades
  two overlapping paths beside a hovered button with the layer cache off and
  on: no layout pass, the alpha applied once, and a replayed frame equal to a
  full one.

| Page and event (benchmark) | Frame p50, before | After | Draws before | After |
| --- | ---: | ---: | ---: | ---: |
| Awakening pause, `open` | 10.28 ms | 0.24 ms | 2,669 | 167 |
| Awakening SETTINGS, `onActivate` | 10.25 ms | 0.25 ms | 2,661 | 159 |
| Awakening CONTROLS, `onActivate` | 9.89 ms | 0.27 ms | 2,684 | 182 |
| Awakening AUDIO, `onActivate` | 11.94 ms | 1.05 ms | 2,759 | 217 |
| Awakening GAME OPTIONS, `onActivate` | 12.19 ms | 2.60 ms | 2,701 | 199 |
| Awakening DISPLAY, `onActivate` | 12.66 ms | 2.29 ms | 2,681 | 179 |
| Create Server, at rest | 0.79 ms | 0.76-0.90 ms | 170 | 110 |
| AUDIO, at rest | 0.17 ms | 0.29 ms | 162 | 122 |

The machine was shared and loaded (other sessions building and capturing),
so single runs vary by up to half again: Create Server's resting page read
0.76 to 0.90 ms across runs of one build. The paths compiled and the draws
do not vary. In the benchmark the message box and the preset check still
compile two paths a frame, because its host has no linear masks; the engine
draws them on the GPU (below).

In the engine (`ui_retainedProfile 240`, hidden window, OpenGL, 1280x720),
before the round (`.install`) and after it:

| View | Paths compiled a frame | Layout passes a frame | Retained CPU p50 |
| --- | --- | --- | --- |
| Title, EXIT dialog open | 2.25 -> 0.25 | 1.00 -> 0.00 | 2.55 -> 0.75 ms |
| Pause, QUIT dialog open | 2.00 -> 0.00 | 1.00 -> 0.00 | 2.13 -> 0.76 ms |
| SETTINGS hub over the title | 0 -> 0 | 0.23 -> 0.00 | 2.46 -> 2.13 ms |
| Title at rest | 0.30 -> 0.31 | 0.17 -> 0.00 | 0.41 -> 0.57 ms |

The resting title and pause did the same work in both builds (the same
draws, atlas tiles and replayed views); their times moved with the machine's
load, which slowed even the phases this round does not touch.

## Refinements

- **A long dropdown value ends in an ellipsis.** A sound device's name ran
  under its list's chevron, cut mid-letter against it ("Default Device
  (OpenAL Soft on Hea"). A list's value now ends 4 dp short of the chevron, in
  an ellipsis (`value_row`, `tools/ui/retained_mp_menus.py`), on every
  settings page and multiplayer page that builds its rows there.
- **A list's name or map ends in an ellipsis at every text size.** The Demos
  list cut AIR DEFENSE BUNKER to "AIR DEFENSE BUN" against the date, as the
  Load and Save lists, CHAPTERS, Create Server's map list and the mods list
  would a long name; above 100 % text they already ended in an ellipsis. They
  now do at every size, as Join Game's rows and the stock lists do
  (`grow_rows`, `tools/ui/retained_saves.py`).
- **The `settings-subpages` capture scenario reaches VIRTUAL REALITY again.**
  The settings tree audit of the same day gave GAME OPTIONS five tabs, so
  the scenario's two tab steps stopped on VIEW WEAPON, its list never opened
  and every later step ran one page out (`tools/ui/capture_retained_screens.py`).

## Settings pages at rest, later the same day

The first pass left about 1 ms a frame on the Awakening GAME OPTIONS and
DISPLAY pages, which looked like the snow's drift taking the style path. The
benchmark now reports layout passes a frame (`layout_updates_mean`), and it
showed something wider: **GAME OPTIONS, DISPLAY, AUDIO and Create Server laid
out the whole page on every frame, at rest, in either campaign.** A frame
skipped the style and layout pass only when no control other than a button
showed, because a toggle's, slider's or list's view was repainted in the
settle that follows a layout pass. The Awakening copies paid more because
the pass also walks their snow and frost.

- **A toggle, slider or list at rest needs no layout pass.** Their values
  change only with the state, so after a state change the value view paints
  what changed, and only then does the frame lay out
  (`valueWidgets`, `src/ui/retained/Runtime.cpp`). A shown number field
  (its caret) or scrollbar keeps a pass a frame, as before.
- **Not on every presentation pass.** The first version painted the value
  widgets every frame; a closed list repaints whenever the presentation pass
  changes, which the drifting snow does every frame, so every list on the
  page repainted (1.4 ms). The check now runs only after a state change.
- **Focusing a control and opening, closing or scrolling a list bring the
  page's styles up to date first** (`RefreshStylesForDecision`): a style
  changed outside the runtime since the last layout (a test that hides a
  list's element directly) is applied before the decision. Two native tests
  (`openq4-ui-choice-scroll-runtime`, `openq4-ui-focus-reveal`) caught that
  case.
- **The dropdown ellipsis keeps 4 dp before the chevron, not 8.** 8 dp left
  Hungarian's GPU choice, "Automatikus (nagy teljesítmény)", 1.5 dp short of
  the room every language's options must have (`openq4-ui-system-presets`).

Best of three benchmark runs (`layout_updates_mean` 0.04 after an opening
event is its first frame):

| Page | Layout passes a frame, before | After | Frame p50, before | After |
| --- | ---: | ---: | ---: | ---: |
| GAME OPTIONS, at rest | 1.00 | 0.00 | 0.55 ms | 0.05 ms |
| DISPLAY, at rest | 1.00 | 0.00 | 0.51 ms | 0.05 ms |
| AUDIO, at rest | 1.00 | 0.00 | 0.17 ms | 0.06 ms |
| Awakening GAME OPTIONS, `onActivate` | 1.00 | 0.04 | 2.60 ms | 0.16 ms |
| Awakening DISPLAY, `onActivate` | 1.00 | 0.04 | 2.29 ms | 0.20 ms |
| Awakening AUDIO, `onActivate` | 1.00 | 0.04 | 1.05 ms | 0.23 ms |
| Awakening CONTROLS, `onActivate` | 0.04 | 0.04 | 0.27 ms | 0.23 ms |

The before column for the Awakening pages is after the first pass above;
for the Quake 4 pages it is the build before the round. The native tests
run 120 of 120, the compiler-backed ones in the MSVC environment.

## Lists and fields at rest, later still

Create Server and Join Game still laid out every frame: a shown scrollbar
(their map and server lists) or number field kept the pass, as it did on
Demos, Load and Save.

- **Only a field being edited or a held pointer keeps a pass a frame.** A
  number or text field at rest shows its value, which changes only with the
  state, so its view now paints with the value widgets after a state change.
  While a field is edited (its caret, its typing) or the pointer is held (a
  drag), the frame still lays out every time.
- **A scrollbar follows its list without one.** Its thumb follows input,
  focus and layout, which already settle; RmlUi's own motion (a smooth
  scroll) asks for its next update (`GetNextUpdateDelay`), and the frame lays
  out when that is due.
- **`Runtime::InvalidateLayout`** tells the runtime that something outside it
  changed a document's elements, so the next frame lays out. In the product
  only the runtime changes a menu's elements (`Runtime::SetProperty` refuses
  a canonical document); `openq4-ui-scrollbar-runtime` resizes and hides its
  content directly and now says so.

| Page at rest (best of three) | Frame p50, before the round | Now | Layout passes a frame |
| --- | ---: | ---: | --- |
| Create Server | 0.79 ms | 0.05 ms | 1 -> 0 |
| Join Game | 0.48 ms | 0.07 ms | 1 -> 0 |
| Demos | 0.22 ms | 0.03 ms | 1 -> 0 |
| Load Game | 0.23 ms | 0.02 ms | 1 -> 0 |

In the engine, Create Server's tabs, game types, password entry and ban list
and Join Game's sorting, filters and player search behaved as before
(`v6`; no refused operation).

## Found, not fixed
- **Every Awakening page still carries 2.3 MB of snow and frost** and loads
  about 100 ms slower than its Quake 4 twin. Merging the flakes into a few
  paths cut the pause's load by a third, but made each recompile slower
  (coverage then works across the whole layer's bounds), so the flakes stay
  separate paths.
- Two-line labels ("Opponent Weapon Style", "Character Self-Shadows") are the
  settings pages' design: the value column leaves every language's label two
  lines. Create Server's map list ends half a row down, as the stock list's
  210 u of 20 u rows does.

## Evidence, 20 November 2026

The kit is in `.tmp/menu-round-4/`, the runtime `.tmp/runtime/ui-menus`
(rebuilt from `builddir`), with a hidden window, a private save path and
console verbs only.

- Document sizes and settled benchmarks: `measure-before.json`.
- Every document after its opening events: `events-before.json`,
  `events-after.json` (the first two changes), `events-final.json`
  (`events.py`).
- Engine profiles of the title at rest, its EXIT dialog, the SETTINGS hub,
  the pause and its quit dialog (`ui_retainedProfile 240`): `p-before`
  (`.install`, the build before this round), `p-after` and `p-final`
  (`retained.py`, scenarios `p-title` and `p-pause` in `extra.py`).
- Pages captured for review: `v1` (seventeen scenarios), `v2` after the
  refinements; `v4` and `v5` after the settings pages stopped laying out at
  rest (GAME OPTIONS' custom crosshair, its next crosshair, Gun Position and
  German applied and repainted; AUDIO; the sub-page and tree walks). The
  `game` scenario's Gun Position step now opens VIEW WEAPON first, where the
  settings tree audit moved the row.

Tests: all 120 native UI tests, the compiler-backed ones in the MSVC
environment, and the DISPLAY page's 28 per-language tests. Two image
recovery tests (`openq4-ui-image-recovery-owner` and `-lifecycle`) exit 3
there; they build renderer sources this round does not touch. Earlier: the native `openq4-retained-ui` (with `CheckVectorFadeReplay`),
`openq4-ui-retained-*`, HUD and vector tests; `ui_retained_gate.py`'s static
checks (its compiled half fails on main, on session symbols this round did
not touch).
