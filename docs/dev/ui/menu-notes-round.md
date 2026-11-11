# Menu round from annotated screens, 11 November 2026

The fourth menu round worked from thirteen screenshots of the new interface
that carried written notes. It follows the
[page loads](menu-page-loads.md) round. This record covers the faults and
the shared pop-up, frame and title design, and the pages the notes ask to
restructure: the campaign pages, CHAPTERS, Player Setup, Create Server's
advanced options, and Join Game's top section and list views.

## Faults

### A framing band lost its end over a level

Load Game and Save Game opened from the pause showed the level through the
leading 484 dp of the top band and 560 dp of the bottom one. The bands
belong to the pause behind the page and dock toward the trailing edge when a
page opens; each draws 1400 dp past its own box so a dock never shows its
end.

`VectorGeometry::Render` (`src/ui/retained/VectorElement.cpp`) bounded a
vector that slides by translation to its own box, so each phase's
compilation is kept while it moves. That cut everything the band draws
outside its box, and the last sliding frame's mesh then stood for the docked
band. The title's black backdrop hid it; a level did not. A sliding vector
now keeps the view's region as well as its box.

### Level pictures

A stock levelshot is a square texture of a 4:3 picture.

- The pause, the Strogg pause and the Arena pause covered a frame wider than
  4:3 with it, cutting its top and bottom. The picture now stands at 4:3 in
  the middle of the same frame (`level_picture`), and keeps that shape as
  the frame shrinks under a larger text size.
- CHAPTERS, Create Server, Join Game, Demos and the Single Player pages'
  summaries contained it in a 4:3 frame, where it stood square between two
  bars. Those frames now fill (`picture_frame( ..., fit="fill" )`), which is
  as right for a 4:3 save picture as for a levelshot.
- A save's preview is captured at 320x240 and stays contained, so it keeps
  its shape whatever a save holds.

### Create Server's header band

The stock header band stood from 37 u, under the stepped top band and over
the page's own title in its notch. It starts at 46 u, where the settings
pages' strip does, and keeps its foot; MAP SELECTION and SERVER OPTIONS are
centred in what is left.

### CHAPTERS' plates on the list's scrollbar

START CHAPTER and BACK began 8 u before the picture, on the list's
scrollbar. The list is 4 u narrower and the plates begin 5 u past its
scrollbar.

### Slider lines

A slider drew 20 lines whatever its range. It now draws one for each step
the thumb takes, so every line is a place the thumb stops and one press of
Left or Right moves it a line. A slider of more than 40 steps draws a line
for every second, fourth, fifth or tenth step (`slider_ticks`), whichever
first divides its steps evenly and fits.

## The pop-up

The stock confirmation (`confirmation`, `modal_silhouette`) keeps its
measures, its opaque black frame over the glow, and its motion.

- **Frame.** The stock rectangle's plain trailing side and foot take the
  family's 45-degree steps: the raised section's trailing corner is cut
  13 dp, a lug stands 4 dp out of the trailing edge under it, and the foot
  has a notch 6 dp deep toward its trailing end, through which the glow
  shows. Every step lies inside the dialog's lit margin. A dialog whose
  actions stand too near its foot goes without the notch. The Strogg
  family's dialog keeps its own silhouette.
- **Title tab.** The tab started at the slot and left the frame's tooth a
  lighter strip beside it, and its foot ended a step above the slot floor's
  outline. It now starts at the frame's leading edge and ends on the floor's
  outline. The Marine tab carries the family's corner marker before the
  title, and the title stands in the middle of the tab's height, 3 dp lower.
- **Energy.** Under the frame and over the glow (`modal_energy`): a faint
  light along the whole outline that swells and settles; two pulses that
  run around the outline, each a bright head with a tail a tenth of a turn
  long, one passing a point every 3.6 s; and a wave of the glow's own light
  that falls through the dialog once a turn. The pulses are the outline
  drawn hot under a mask of two wedges that turns about the dialog's centre
  while the outline inside it turns back. The energy shows with the
  dialog's contents, never under reduced motion, and not above 100 % text
  on a dialog that grows with the text; its timeline is cancelled as the
  dialog hides.

### The session's message box

UPDATES' answer, a failed connection, a password prompt and every other box
the session raises (`idSessionLocal::MessageBox`) drew the stock
`guis/msg.gui` over the retained menus. They now present
`guis/menu/msgbox.q4ui` (`tools/ui/retained_msgbox.py`): the pop-up 560 dp
wide and 176 u tall, with six lines of message, a prompt's field, and up to
two buttons.

- The document takes the stock box's state under the same names (`title`,
  `message`, `visible_left`, `left`, `visible_mid`, `mid`, `visible_entry`,
  `str_entry` and the rest), so the session fills either box alike.
- Its buttons ask the session verbs `msgLeft`, `msgRight` and `msgMid`,
  which `HandleRetainedSessionRequest` maps to the stock box's `left`,
  `right` and `mid`. Escape answers as the trailing button of two, or the
  lone one.
- `SelectMessageBox` chooses the box before each message: the retained one
  where the retained screens present and the document loads and draws, else
  the stock one. The download progress box keeps the stock screen.

## A preview picture's frame

`picture_frame` (Load and Save Game, CHAPTERS, Create Server, Join Game,
Demos, Arena, the Single Player summaries) drew a plain 1 u border. Its
upper-leading and lower-trailing corners are now cut at 45 degrees (8 and
12 u; the Strogg family's 30-degree shoulders), the picture cut with them
by a mask, and each cut corner carries a rail that runs along the two edges
it joins and fades out by their middle, as a plate's rail does. The
family's corner marker stands in the upper trailing corner. Every point is
a fraction of the frame, so the frame still follows whatever sizes it. The
pause's level card keeps its own frame.

## The title

- **A hot plate's label.** A plate's label grows 3 % while it has hover or
  focus. The scale is about the label box's middle, so the first letter ran
  1.5 % of the box's width back over the plate's corner marker (7 dp on a
  home plate). The label now grows from its leading edge (`pop`), on the
  home, section, action and multiplayer plates.
- **The emblem.** The glint that ran around the rim every 9 s gives way to
  a charge: every 6.4 s a lit band with a hard front climbs the rim from the
  spike's tip up both arms of the ring, and the whole rim flares as it fills
  the ring. A reticle indexes about the ring's centre: three arcs with end
  ticks step 30 degrees one way and a gauge's ticks step 10 the other, each
  holding between steps as a mechanism does. Each ring is one path and
  compiles only while it moves, so a settled title still tessellates
  nothing. Reduced motion hides the charge and the flare and stops the
  reticle.

## The campaign pages

The Campaign page and its difficulty page each listed what they choose and
what they do as one column of plates: QUAKE 4, THE AWAKENING, NEW GAME,
CHAPTERS and BACK, then PRIVATE to WARLORD, TURBO MODE, START GAME and BACK,
eight plates. A page's choices now stand as tiles
(`tools/ui/retained_tiles.py`, `choice_tile`), the chosen one lit with an
orange wash, outline and corner marker, and its plates are its actions
alone. The pages, their verbs and the state the session publishes are the
same.

- **Campaign.** QUAKE 4 and THE AWAKENING are two tiles side by side over
  the plates, each under the emblem (THE AWAKENING's over a rising arc).
  Left and Right step between them; Up from NEW GAME and Down from BACK
  reach them. NEW GAME, CHAPTERS and BACK are the plates.
- **Difficulty.** Under DIFFICULTY, a grid of six tiles: the five ranks,
  each under one to five chevrons, and TURBO MODE as a switch in the sixth
  place, its knob at the trailing end while it is on. START GAME and BACK
  are the plates. The chosen rank's description keeps its column. A tall
  view stands the grid over the plates at the column's scale.

### CHAPTERS' rows

A row said its status in words (Locked, Autosave, Fresh start). It shows a
padlock on a level not yet reached and a save mark on one with a start
autosave, at its trailing end under STATUS; a level that starts fresh has
neither. The chosen level's panel still says it in full, and a tall view's
two-line rows keep the words on their second line. The session publishes
`chapter.row<N>.saved` for the mark.

## Player Setup's tabs

Player Setup held the player's own name, clan tag, model and rail color. It
now has three tabs (`tools/ui/retained_player_setup.py`), each row showing
on its tab alone:

- **PLAYER.** The rows it had, and Handicap.
- **OPPONENTS.** Force Model, then Outline, Rim Light, Effect Color,
  Brightskin and Bright Color for opponents, and Outline Width, which both
  sides share.
- **TEAMMATES.** The same five appearance rows for teammates, and the line
  that says they apply in team modes.

The appearance rows are the in-game card's Settings page's
(`retained_mp_menus.settings_page`): the same settings, lists and labels,
set through `settings.player.set` and read back from each CVar. Force Model
forces a model on every other player outside team modes (`g_forceModel`):
Off, then the models of the page's catalog, asked through the page's own
proposal (`player.setup.apply`, the new field `force_model`), which the
session answers with the row it stands on (`setup.force_row`). The two
teams' forced models depend on the team the player is on and stay on the
in-game page.

## Create Server's advanced options

ADVANCED SERVER OPTIONS listed sixteen rows in one column, taller than the
view, so its title and first row stood off the top. Its rows now stand in
three tabs (`ADVANCED_TABS`), and the dialog is as tall as its longest:

- **SERVER.** Use Password, Set Password, Remote Console Password, Pure
  Server, Allow Server Side Mods, Server Upstream Rate, Map Cycle Script
  File and the ban list.
- **MATCH.** Ready Mode, Do Warmup, Allow Spectators, Allow Voice Chat,
  Shuffle Teams, Fill With Bots and Bot Skill.
- **RULES.** Weapon Stay, Allow Buying, Overtime (seconds), Mercy Lead and
  Round Limit.

Shuffle Teams, Overtime, Mercy Lead and Round Limit are new rows for
openQ4's match rules (`si_shuffle`, `si_overtime`, `si_mercyLimit`,
`si_roundLimit`), under the Match Control page's labels; the session's
`RETAINED_CREATE_SETTINGS` holds their verbs and ranges. Each opening
starts on SERVER.

## Join Game's top section

The stock header band (`mp_browse_header`, from 54 u) drew its top rail
through the MOD and SOURCE row, which stands from 46 u, and past its own
fill. Two flat strips as wide as the list's rows take its place
(`browser_header`): a toolbar behind MOD and SOURCE, its upper-leading
corner cut with an orange line that fades out along the top, and the
columns' headings under it down to the list. Every edge ends with the
strips.

### The list's views

The list shows its servers as rows, the stock list, or as tiles. The
toolbar's LAYOUT button, between the MOD spinner and SOURCE and drawn as the
view it shows, changes the view; `ui_browserView` (0 rows, 1 tiles,
archived) keeps it, set through `settings.player.set` and read back as
`join.view`.

A tile is a server's row 40 u tall (`tile_view`, `TILE_CELLS`): its map's
levelshot at 4:3, its name over its game type and map, the ping and players
at the trailing end of the first line and the symbols under them. The tiles
are the same rows and controls, so choosing, the details card, the sorts and
the filters work as they do in rows; each property a tile moves keeps
whatever else binds it as the value it has while the tiles are off. A tall
view and a text size above 100 % keep rows, which have their own layouts
there.

The session publishes each row's levelshot (`join.row<N>.shot`) only while
the tiles show, and publishes the rows again when the view changes
(`UpdateRetainedJoinGame`).

## Evidence, 11 November 2026

Hidden window, private save path, console verbs only; the kit is in
`.tmp/menu-round/`, the runtime `.tmp/runtime/ui-menus`. The annotated
screens are in `.tmp/menu-round/screens-2026-11-11/new/`.

- The faults after their fixes at 2240x1260: `n2` (the pause, the Strogg
  pause, CHAPTERS, Create Server, Save Game in a level).
- The pop-up, the title's plate and the emblem: `n2/title`. The session's
  message box as the pop-up (UPDATES' answer): `n3/updates`.
- The campaign's tiles, the difficulty grid and CHAPTERS' marks:
  `n3/campaignskill`.
- Player Setup's tabs, with Outline set to Low and a model forced on
  OPPONENTS (`cl_player_outline_enemy` 0.35, `g_forceModel` read back):
  `n4/n-setup-tabs`, `n5/n-setup-tabs`.
- Create Server's tabs, with Shuffle Teams set (`si_shuffle` 1):
  `n4/n-create-tabs`.
- Join Game's top section and both list views over a loopback server, the
  view changed with the LAYOUT button and back (`ui_browserView` 1, then
  0): `n5/n-join-views`.
- The preview picture's frame on Load Game: `n5/loadgame`.

Tests: the native `openq4-ui-retained-*`, `openq4-ui-document*`,
`openq4-ui-vector`, `openq4-ui-player-setup-request` and
`openq4-ui-popup-placement*` tests (32), `ui_retained_gate.py`, and every
`tools/tests` script that names a changed source. Every document was
regenerated by the full `build_retained_screens.py`. The expression budget
of a document rose from 65,536 to 131,072 nodes (`Document.cpp`): Join
Game's rows, whose cells each bind for the text size, a tall view and the
tiles, hold about 75,000.
