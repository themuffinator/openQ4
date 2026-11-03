# Retained menu ownership and platform input

8 September 2026. Retained documents can now acquire application input through
`ui_retainedOpen "path.q4ui"`. This advances the [full replacement plan](../archive/2026-10-18/plans/idtech5-ui.md)
from semantic button fixtures to an engine menu host. The host closes the console,
receives SDL input, owns the session's GUI state and inhibits gameplay controls.
`ui_retainedPreview` remains a passive renderer. Neither command accepts a stock
GUI as a finished replacement; the game action bridge, other widgets, complete
corpus and native editor remain required.

## Input and ordering

SDL supplies absolute window-coordinate pointer positions from motion and button
events. The retained runtime converts window density and viewport origin once,
then uses the presented document's transformed hit geometry. Mouse movement does
not pass through the legacy 640×480 cursor conversion. An initial click supplies
its own position even if no motion preceded it. Pointer leave clears hover.
The menu uses the native cursor during this integration phase; it does not warp
or poll the cursor to synchronize it with a legacy GUI position.

Keyboard, pointer and controller input enter the existing ordered system-event
queue as `SE_RETAINED_UI`, with a fixed engine-owned payload and document
generation. The session consumes this event before legacy GUI/game bindings.
The enum is appended in the engine and companion headers; existing event values,
`sysEvent_t` layout, game API 47 and renderer API 13 are unchanged. Payloads are
copied into queue-owned memory and freed by the event loop. Malformed payloads
and stale generations cannot target a replacement document. Queued key releases
still update physical key tracking after their original document closes.
Broad event-journal/replay qualification remains open.

Shared-header qualification also repaired the companion copy of
`engineWindowState_t`, which lacked the engine's three density fields. All CI
and candidate/release workflow defaults now pin the matching published companion
revision; the parity test checks the pinned files as well as the working tree.

| Input | Menu action |
| --- | --- |
| Tab / Shift+Tab | Next / previous eligible control |
| Arrow keys / D-pad | Spatial navigation |
| Enter, keypad Enter, Space / gamepad South (East while swapped) | Accept on paired release |
| Escape / gamepad East (South while swapped), Start, Back | Pop the current modal scope, then close the root menu |
| Gamepad Menu (Start) | The document's `onMenuButton` where it declares it ([Menu button](#menu-button-24-october-2026)), once per press; otherwise, and while a dialog or an open list holds the input, as Back |
| Gamepad View (Back) | The document's `onViewButton` where it declares it ([View button](#view-button-24-october-2026)), once per press; otherwise, and while a dialog or an open list holds the input, as Back |
| Primary pointer | Focus, arm and release the hit button; a finger that moves past 8 dp is no longer a tap, in a scrolling list drags its content with inertia and a rim light at its ends, and across a tab strip swipes to the previous or next tab; a finger resting 500 ms on a button that names a detail opens it ([touch](#touch-in-the-menus-1-november-2026)) |
| Movement stick | Dominant-axis spatial navigation after neutral |
| Q / E, gamepad shoulders | The document's previous / next tab (`onTabPrevious`, `onTabNext`) where it declares them; not while a field is being edited, nor with Ctrl or Alt; a finger's swipe across a tab strip runs them too |
| Gamepad triggers | The document's previous / next section (`onSectionPrevious`, `onSectionNext`), on the same terms; where it declares neither, PageUp / PageDown ([list paging](#list-paging-24-october-2026)) |
| Gamepad West / North | The document's secondary / tertiary action (`onSecondary`, `onTertiary`), on the same terms |

SDL's gamepad South/East buttons map to engine `K_JOY3`/`K_JOY4` and West/North
to `K_JOY6`/`K_JOY5`; the shoulders are distinct.

### Touch in the menus (1 November 2026)

A touchscreen (`in_touchscreen`) routes one finger as the primary pointer;
SDL's touch-to-mouse synthesis stays off, so every mouse event is a mouse.
The SDL layer names each pointer input's source (`idKeyInput::NotePointerSource`):
a finger, or the mouse once it moves or presses. While a finger leads, the
menus draw no cursor, the retained one or the stock GUIs' (section 13.6:
nothing depends on hover), and a lifted finger queues a pointer leave after
its release, so the control it touched does not stay lit as a hovered one.
Both retained adapters, the menus' (`idUserInterfaceRetained`) and the
`ui_retainedOpen` preview's, tell the runtime before each pointer input
(`Runtime::SetTouchPointer`), and the menus' adapter moves the pointer out
after a lifted finger's release. A routed finger moves the menu cursor
directly, so the SDL layer also queues a zero-delta pointer event for each
finger motion: a retained menu follows the finger as it moves, not only
where it pressed and lifted. A mouse keeps the desktop rules: its release on
the button it pressed activates it, and it never drags, swipes or
long-presses. A touch press records where and when it landed, and becomes
(INP-014):

- **A tap** while it stays within 8 dp. Touch-down presents the pressed
  state in the same input, with no tap delay: the control takes focus and
  arms at once, so its pressed treatment as the document authors it (the
  focus look with section 8's 1 dp `press` inset) shows while the finger
  rests. Past 8 dp in any direction
  it is no longer a tap: the arm is cancelled and the release activates
  nothing.
- **A drag** when it moves more up or down than across in a scrolling
  container: the content follows the finger, and the finger hovers nothing
  while it drags. Pulled past either end, the content stays and that end's
  rim light brightens with the pull, fully at 64 dp. Released faster than
  50 dp a second (measured over its last 100 ms, at most 8000), the content
  keeps that velocity and slows exponentially over 325 ms until under 20 dp a
  second; a finger that rests before lifting leaves none. A new press, a
  focus move (navigation, a program's focus, `FocusControl`), a modal, the
  wheel or a cancellation stops it. A fling reaching an end stops there and
  lights that end's rim by the speed it arrives with, fully at 2000 dp a
  second. Let go, a rim light fades over 250 ms. The rim light is
  `marine.rim`, `#626934` falling linearly to nothing across 19 u from the
  list's edge, at twice the framing bands' 0.37 when fully lit; the runtime
  draws it in an element of its own over the document, which takes no input.
- **A swipe** when it is pressed on a tab strip and moves more across than up
  or down. A tab strip is a button that runs a `tab_` program, or the
  strip's previous or next control, in a document that declares
  `onTabPrevious` or `onTabNext`. Released 48 dp or more across, it asks for
  `onTabNext` (moving toward the leading edge) or `onTabPrevious`; the
  adapter takes them from `Runtime::TakeGestureEvents` and runs them as it
  runs Q and E. Shorter, it asks for nothing and is no tap.
- **A long-press** when it rests 500 ms within 8 dp on a button that names a
  detail (section 13.4's detail area; DocumentModel's `detail`). The press is
  cancelled, so its release activates nothing, and the detail card stays
  beside the button, placed as for focus, after the finger lifts, until the
  next press, navigation, focus request or mouse movement. A lift 500 ms
  after the press long-presses even with no frame between. Once long-pressed,
  the finger drags and swipes nothing. A button without a detail keeps its
  tap however long the press.

A press that captured a value control (a slider, a scrollbar's thumb) or
lands while a choice popup is open keeps that control's own gesture. A light
haptic tick on press remains open: it must follow the system's
touch-feedback setting, which Android applies to the activity's
`performHapticFeedback` and SDL's haptic API does not read.

**Swap Confirm and Back.** Nintendo layouts confirm on the east button,
where their A sits, and go back on the south button (section 13.5). The
archived `in_swapConfirmBack` decides:

| Value | Menus confirm on |
| --- | --- |
| 0 (default) | East for a Nintendo layout (`in_promptStyle` 3, or `in_gamepadFamily` nintendo unless `in_promptStyle` forces another family), south otherwise |
| 1 | South |
| 2 | East |

`idKeyInput::MenuControllerKey` trades the south and east buttons while
confirm is swapped. Every menu input path translates through it: the stock
windows' controller translation, the retained adapter's menu keys and a text
field's commit, the retained runtime's `MapMenuKey`, and the sub-page change's
Back. The physical button stays the input's source, so a release ends what its
press began even if the swap changes in between. In-game bindings never swap.

Evidence for Swap Confirm and Back: `.tmp/ui/retained-confirm-swap-evidence/validation-evidence.json`,
SHA-256 `3f3d402b39d24c8fe5926b1bfd16fe7ab791975460ef3493bbc8a35c60342b67`.

**Prompt glyphs.** Prompts show the active device's glyphs (section 13.5).
The engine already tracks the last device's family for key-binding hints
(`idKeyInput::LastInputWasController`). A controller key or a deflected
axis makes it a controller. A keyboard key, a mouse button or mouse
movement makes it the desktop. A controller's glyphs are the family the
HUD's prompts use, `Sys_SDL_GamepadGlyphFamily`: the one the archived
`in_promptStyle` forces (1 Xbox, 2 PlayStation, 3 Nintendo, 4 Steam Deck,
5 generic) when a remapping layer hides the real device, otherwise Steam
Deck under the `steamdeck` platform profile or the family of the gamepad
type SDL reports, generic for any other type or a joystick SDL cannot map.

The SDL3 backend also reports the open controller's face layout, for the
automatic swap above, in the read-only `in_gamepadFamily`, from the label
SDL reports printed on its south button:

- A is Xbox, or Steam Deck under the `steamdeck` platform profile.
- The cross is PlayStation.
- B is Nintendo.
- Any other label is generic, as is a joystick SDL cannot map.
- `none` means no controller is open.

`in_gamepadFamily` is `CVAR_NOCHEAT`, so joining a server without cheats
cannot reset it while the controller stays open. `listControllers` prints
the glyph family as `prompt glyphs:`, marked when `in_promptStyle` forces
it, followed by the face layout.

`RetainedUI_PromptGlyphs` combines the device and the family. The adapter publishes the
result as `prompt.glyphs` to every document that declares it, before the
frame that shows the change draws. The value is 0 for the keyboard's keycaps
and 1–5 for the Xbox, PlayStation, Nintendo, Steam Deck and generic face,
shoulder and trigger buttons. The adapter also publishes `prompt.swapped`
(`RetainedUI_PromptSwapped`, the swap above) to documents that declare it. Keyboard scancodes, controller buttons and pointer buttons have
separate source IDs. Multiple sources mapped to Accept hold one logical press;
releasing the first source cannot activate while another remains down. Key-up
releases the action captured at key-down, including when Shift changes first.

Evidence for prompt glyphs: `.tmp/ui/retained-prompt-glyphs-evidence/validation-evidence.json`,
SHA-256 `8246e1584cd199da491094441ac48f320d14fe796122ecb7a7e2991214ff004c`.

### List paging (24 October 2026)

Section 13.5 pages lists with the triggers. PageUp and PageDown, from the
keyboard or a trigger, move focus a visible page through the scrolling list
that holds the focused button or toggle (`Runtime::Impl::PageList`): to the
farthest row that fits on the list's client height with the focused one, or
the next row when even that one does not fit. Home and End move to the list's
first and last row. Rows in the focused row's column are preferred, a
disabled or hidden row is skipped, focus is revealed as any focus change is,
and nothing activates. Focus outside a scrolling list, a list with nowhere to
go and a focused slider, scrollbar or choice keep their existing meaning.

The adapter sends the triggers as PageUp (left) and PageDown (right) to a
document that declares no `onSectionPrevious`/`onSectionNext` of its own; on
a focused slider the right trigger raises the value, as Right does, by the
interaction's ten-step coarse step. Documents with section events keep them.
`tools/tests/native/UiFocusRevealTest.cpp` covers the paging at 1x and 2x
density, and the `list-paging` capture pages the Load Game list.

### Menu button (24 October 2026)

Section 13.5's Menu button closes the menus and resumes play. The adapter
runs a document's `onMenuButton`, once per press, where the document declares
it, and treats the button as Back elsewhere, as before. The pause's pages
declare it: Save Game, Restart Level and, during a game, Load Game ask the
session for `menuResume`, which returns the pause and runs its RESUME, so the
softened view releases as it does from the pause. At the title, Load Game's
Menu goes back as Back does. The pause itself already resumes on Back.
`menuResume` acts only while a single-player game runs; a field being edited
keeps the button.

Evidence for the Menu button:
`.tmp/ui/menu-button-resume/validation-evidence.json`, SHA-256
`7d791a2d806b2f192709d210e04b984d5ba59b3a49860c5564c28448e1af3782`.

**Apply.** On a screen with pending changes the Menu button applies them.
While a dialog (an authored modal scope) or the focused control's open list
holds the input, the adapter keeps the button Back, which closes them
(`Runtime::ModalOpen`, the widget's `popupOpen`), so Menu never acts behind
a dialog. The two screens with an Apply declare it:

- The opt-in SYSTEM page (`tools/ui/retained_system.py`) runs its
  own programs. With pending changes Menu runs `apply`, whose condition
  still decides whether Apply can run; with a typed number not yet
  committed it moves the focus to that number, as Continue Editing does,
  since Apply waits for it; on a clean page it runs `onBack`, which closes
  the page. In the display confirmation and the discard dialog it does what
  Back does there (`confirmationBack`, `continueEditing`), so Menu never
  keeps a display change unseen; the adapter already routes those dialogs
  to Back.
- The Escape card's [Admin page](multiplayer-menus.md#the-admin-page)
  presses Apply changes while its Server section has something to apply
  (`mp.admin.apply.available`); otherwise Menu closes the card, as Back
  does. The Welcome and summary cards keep the button Back. The Apply
  changes plate ends with the Menu button's glyph for a controller
  ([Menu glyphs](#menu-glyphs-24-october-2026)).

Match Control's staged rules commit through a confirmation and keep Menu
as Back.

Evidence for Apply: `.tmp/ui/menu-button-apply/validation-evidence.json`,
SHA-256 `f8fea2394b50fc49215af334f825208e7b17f9fe35970f45b7adb3e9945665c6`.

### Trigger glyphs (24 October 2026)

The triggers step the Escape card's Match and Admin sections, and their inner
strips now end with the triggers' glyphs (section 13.5), so the strip shows
which buttons step it. The glyphs are a legend for a controller only: no key
steps the sections, so the keyboard shows none, and the slot stays empty.
Each family's trigger is taller than its bumper, an 18 dp plate whose top
corners round by 7 dp, labelled as SDL reports it: LT and RT (Xbox), L2 and
R2 (PlayStation and Steam Deck, the latter outlined), ZL and ZR (Nintendo),
and for a generic controller an outlined plate with a chevron pointing to its
side. Generation fails if a strip's sections in any shipped language would
reach the glyphs. The Menu and View buttons have glyphs since
([Menu glyphs](#menu-glyphs-24-october-2026), [View glyphs](#view-glyphs-24-october-2026));
the sticks' remain.

Evidence for trigger glyphs: `.tmp/ui/trigger-glyphs/validation-evidence.json`,
SHA-256 `1de24fafd097c2ba6f764a6b37406e5972111dc88c79fa6b55a74db617779d59`.

### Wrapping menus (24 October 2026)

Section 13.4's menus of up to seven items wrap; scrolling lists do not. The
title, both pauses, the Arena pause and the Single Player, Campaign and
Multiplayer pages author their wrap (`wrap_menu`): Up and Down step through
the column and on to the plinth's links, and from the links back to the
top, so the column and the plinth form one loop that a controller can go
round either way; Left and Right wrap along the plinth. A confirmation's
two actions wrap too (Left from YES reaches NO, Right from NO reaches YES),
and Restart Level's five difficulties loop through RESTART LEVEL. At the title, Up
from SINGLE PLAYER (or CONTINUE) reaches MODS, Left from MODS reaches EXIT,
and Down from any link returns to the top. On the pauses Up from RESUME
reaches EXIT and Down from EXIT returns to RESUME.

Each move is an authored navigation target. The runtime now passes a move
on through a target that cannot take focus along that target's own
authored move in the same direction (`Interaction::Authored`), so CONTINUE
without a save, or a disabled entry such as an Awakening campaign that is
not installed, is skipped rather than stopping the move; a chain that ends
or loops falls back to the spatial or tab-order move as before. Tab strips
already wrap (the cards' shoulders), screens already restore the focus of
the item that led to a page (`returnHome`), and closing a dialog returns
focus to the control that opened it. The campaign difficulty page holds nine
items (five difficulties, Turbo, Start, Chapters and Back), beyond the seven
section 13.4 wraps, and does not wrap.

**List focus memory.** Each scrolling list (the viewport a scrollbar
scrolls) remembers the control last focused in it. A directional move that
enters the list from outside it, from a plate, a heading button or the
scrollbar, returns to that control while it can still take focus, rather
than to the row nearest where the move started (`Interaction::Container`,
`Interaction::Remembered`); an authored move still wins. On the Load Game
page, Right from LOAD GAME returns to the save last focused, not the bottom
row beside the plate. The memory is per document and transient: a document
load clears it, and a remembered row that a list's refresh hides or
disables is passed over for the nearest row.

Evidence for list focus memory: `.tmp/ui/list-memory/validation-evidence.json`,
SHA-256 `9c98d8f5eb69eefe7cff9d0178b72b6d6793ee74ac3e62fc9688e289ab72b090`.

Evidence for wrapping menus: `.tmp/ui/menu-wrap/validation-evidence.json`,
SHA-256 `c6e0cce45e07d6b112980a2399eb413fc22ad7d596deacc620a976f7b545739f`.
Evidence for the dialog and Restart Level wraps:
`.tmp/ui/menu-wrap-dialogs/validation-evidence.json`, SHA-256
`950ee588fc31835335cb205174b5607db8e11dca821498d2d75cd8c66dfc91ff`.

### View button (24 October 2026)

Section 13.5 gives the View button the scoreboard in play and, in menus,
showing or hiding a page's details. The adapter runs a document's
`onViewButton` the way it runs `onMenuButton`: once per press where the
document declares it, Back elsewhere and while a dialog or an open list
holds the input. Join Game declares it: View hides the server details card
under the focused row and shows it again (`join.detail.hidden`), without
changing which row the card describes. In play the default bind of `JOY8`
is now `_impulse19`, the scores or objectives while held, as D-pad Up's is;
it opened the menu, as Start still does. Players who kept the old bind in
their configuration keep it. Join Game's card is the only details panel a
retained page has yet.

Evidence for the View button: `.tmp/ui/view-button/validation-evidence.json`,
SHA-256 `d34fdad0c627ab5312d76888a780f35eee9f61cf8e8e0d39a52e03f19d7f1e7c`.

### Menu glyphs (24 October 2026)

Section 13.5's glyph families gain the Menu button, drawn in the family text
color: Xbox's three lines on a disc, PlayStation's Options button as three
lines on an upright pill, Nintendo's plus on a disc, Steam Deck's three
lines in a ring and a generic controller's three lines in an outlined pill
(`menu_glyph`, the `menu` position of `device_glyphs`). The card's prompt
bar has no room for a Menu prompt in every language (German and Spanish run
past the card with one), so the glyph sits on the control the button
presses: the end of the Admin page's Apply changes plate, after its label
and lock, for a controller only. The keyboard has no key for it and shows
none. The sticks' glyphs remain.

Evidence for Menu glyphs: `.tmp/ui/menu-glyphs/validation-evidence.json`,
SHA-256 `c2bc17f9841b34b99304af21faa4c2f65104a3c8dad2af5a484488d67fa2a4cf`.

### View glyphs (24 October 2026)

The glyph families gain the View button (`view_glyph`, the `view` position of
`device_glyphs`), in the family text color: Xbox's two overlapping windows on
a disc, PlayStation's Create button as three rays on an upright pill,
Nintendo's minus on a disc, Steam Deck's two windows in a ring and a generic
controller's two windows in an outlined pill. As the Menu glyph does, it sits
on what the button acts on: the top trailing corner of Join Game's server
details card, 18 dp beside the address and above the rule, for a controller
only. View hides the card; the keyboard has no key for it and shows none.
The mp-pages native test checks each family's glyph, its place on the card
and the keyboard's absence, and the `join-view-glyphs` capture forces each
family through Prompt Style. The family shapes await art review under UI-05.

Evidence for View glyphs: `.tmp/ui/view-glyphs/validation-evidence.json`,
SHA-256 `d3870701f6e9d2e67bf53894eb59a5cc5ff0e8477369a2ac1e9f1ca658195d18`.

### Right stick (3 November 2026)

Section 13.5 gives the right stick the detail area and model previews. Each
frame the session's pump (`PumpControllerDetailStick`, beside the left stick's
navigation pump) reads the look stick, the left one when Southpaw swaps them,
and hands its deflection to the retained view that takes the menu input: the
home screen over the legacy menu, the multiplayer card over the game's menu or
an active retained page (`RetainedStickTarget`). Nothing reaches a legacy GUI,
a page while a change between menu levels holds the input, or a home screen
whose hand-off holds it. Past a rest zone of 16 of 127 the rate follows the
deflection, timed from the previous frame of the same push and never more
than 100 ms of it, so a stalled frame cannot jump. Inverted look turns the
view in play; the menus keep the stick's own sense.

- **Up and down scroll the detail area**, 720 dp a second at full deflection
  (`Runtime::ScrollDetail`). The detail area is the first scrolling container
  in document order that shows, that input may reach (a dialog's scope
  included) and that has content past its edge, and that holds no control: a
  list of rows already scrolls with its focus, the left stick and the
  triggers. Today those are the pause's OBJECTIVES page in both families, the
  summary card's scoreboard, the Welcome card's roster and the bodies of the
  DISPLAY page's confirmation and discard dialogs. Focus stays where it is
  and nothing activates. An open choice list holds the stick, as it holds
  the wheel.
- **Left and right turn Player Setup's preview**, half a turn a second at
  full deflection. The session owns the angle, as it owns the Rotation row
  (`TurnRetainedPlayerSetup`), so the row's slider follows the model and the
  angle wraps within the row's 0-360 degrees.

`openq4_retainedGui stick <x> <y> <msec>` holds the stick for a harness
without a device: the session turns the preview and the view scrolls its
detail area. The `ui_controller_menu_pump.py` harness compiles the pump and
its target against counted doubles (rest zone, timing, stalls, inverted look,
each target and refusal), the focus-reveal native test scrolls a fixture's
detail area past a hidden area, a fitting one and a list of buttons, and the
`playersetup-stick` and `pause-objectives-stick` captures turn the preview
from 200 to 290 and 110 degrees with the slider following, and scroll an
Objectives page made to overflow down and back to the top. Holding a
physical stick on each controller family remains part of the device runs.

### Settings rows (24 October 2026)

Settings > Game Options > Controller offers both choices as rows, after Low
Battery Rumble Cap (section 13.5, `INP-011`):

| Row | Cvar | Choices (values) |
| --- | --- | --- |
| Prompt Style | `in_promptStyle` | Automatic (0), Xbox (1), PlayStation (2), Nintendo (3), Steam Deck (4), Generic (5) |
| Swap Confirm and Back | `in_swapConfirmBack` | Automatic (0), Off (1, south confirms), On (2, east confirms) |

They are ordinary stock `choiceDef` rows bound to the archived cvars, so a
change applies at once: the next frame's prompts take the family, and the next
menu key takes the swap. Brand names stay as printed; the other words are in
all twelve tables (`#str_231526`-`#str_231529`). Every later Game Options row,
its hover and the section anchors moved two rows (48 px) down; the page's
scroll extent, its last step (75) and the section thresholds in
`MainMenuSettingsSectionChoiceForScroll` follow, and
`tools/tests/settings_menu_coverage.py` pins them with the registry entries
`game.controller.prompt_style` and `game.controller.swap_confirm`.

The `settings-controller` capture (with the retained screens off) scrolls
Game Options to the rows, sets both cvars from the console and reads them back
in the rows, then shows the page's last step.

Evidence for the settings rows, list paging and Join Game's row focus
([retained screens](retained-screens.md#join-game-sub-page)):
`.tmp/ui/controller-settings-paging/validation-evidence.json`, SHA-256
`4ee88d83dbf0f98347f95fa70529aeb463fc20bb7725aaad1f905b6a5a82831c`.

The device-independent `Input` adapter owns navigation repeat: immediate first
navigation, 320 ms initial delay, then 110 ms between repeats. The newest held
navigation source owns repeat. OS repeated downs cannot duplicate it. Accept,
Back and pointer activation never repeat. A presentation stall produces at most
one navigation step per subsequent frame. Stick navigation uses 50/38 press/
release thresholds in the existing normalized -127..127 range and requires
neutral after opening or resuming input.

Documents the session routes (the home screen, the SYSTEM, Single Player and
Campaign pages) do not read the stick themselves. The session's controller
pump turns a deflected stick, like a held D-pad direction or shoulder button,
into menu key presses at its own cadence: a stick step at once, a held button
after 320 ms, then one step every 110 ms. The stick never sends a release, and the
adapter repeats a held key until its release, so on any retained input target
the pump sends each step's release straight after its press
(`idSessionLocal::RetainedInputTarget`, checked by
`tools/tests/ui_controller_menu_pump.py`). Legacy GUIs still receive the
presses alone.

Evidence: `.tmp/ui/controller-menu-pump/validation-evidence.json`, SHA-256
`90860f636950012e402d58132f0f8c0c2d4834633005a15d59bdca33bcb0c94c`.

Cancellation reaches the runtime before logical releases. Document replacement
keeps held sources blocked until release; focus loss forgets physical sources
whose releases may happen outside the application and rejects orphan OS repeats.
Logical arms supplied by another adapter or a developer command are released too.
Controller disconnect, disable and replacement queue cancellation before their
artificial button/hat releases, so removing a device cannot activate a held
Accept button. The adapter releases its virtual stick source during cancellation
and requires physical neutral before rearming it; replacing a document while
the stick is held cannot leave navigation permanently blocked.
Focus loss, console ownership and invalid output suspend interaction. Video/font
recreation cancels input, advances the generation, resamples window focus and
keeps the presentation clock monotonic. A failed recreation closes the host.

The existing console toggle and console editing paths retain ownership. Other
menu keys cannot execute gameplay bindings. Platform hotkeys remain in their
existing SDL path. When a legacy GUI explicitly takes ownership or a map unloads,
the retained host closes. Temporary legacy coexistence remains part of migration.

## Gameplay handoff

`RetainedUI_IsOpen()` is atomic for the async usercmd reader. Opening/closing
changes ownership under the usercmd critical section, records held keys/direct
buttons/cached axes, clears their accumulated gameplay actions and mouse filter,
and discards queued poll samples belonging to the old owner. Other subsystems'
inhibit bits are preserved. During ownership the usercmd generator emits neutral
gameplay input. After closing, held buttons need release and axes need neutral
before they can affect gameplay. A key release already observed in the ordered
event path clears an obsolete poll-path block during the handoff, preserving
the next fresh press. SDL also suppresses old-owner key repeats so
they cannot execute a bound console command after closing the menu.

The session includes the retained host in its active-GUI query, input inhibition
and SP pause branch. UI repeat and motion run on presentation time while SP is
paused. The asynchronous multiplayer simulation continues with local controls
inhibited. Legacy controller repeat and GUI frame events do not run behind an
owned retained menu.

Back is handled by the host's modal/root lifetime. Other button activations are
bounded, owned document/node/action requests available through
`ui_retainedEvents`; they are not executed as command strings. Application/game
dispatch and source bindings are still pending. Opening a fixture containing
`menu.controls` therefore demonstrates the request and its feedback, not a
completed settings page.

## Qualification

The native retained suite runs the real layout/runtime, fixture and `Input`
adapter. It checks source aggregation, release pairing, repeat cadence and stall
behavior, modifier changes, focus/replacement cancellation, orphan repeats,
pointer sources and logical arms inherited from another adapter.

`python tools/tests/ui_retained_input.py` compiles and executes the production
key mapping, SDL queue helpers, engine transport/clock dispatcher and usercmd
release gates with counted in-memory services. It checks South/East mapping,
console routing, exact fractional coordinates, stick thresholds/neutral,
payload/generation rejection, focus loss/recovery, actual SDL device-release
ordering through the dispatcher, stick recovery after cancellation, close-time key releases,
held movement/buttons/impulses/direct actions and axis recovery. The runtime sink
is counted in that boundary test; the native retained suite separately uses the
real renderer and controls.

`python tools/tests/ui_background_cursor.py` runs the production cursor routing
functions for Windows and POSIX branches across 64 state combinations. Retained
menus do not warp or poll the cursor; disabled input, hidden windows and lost
focus remain guarded. `python tools/tests/sdl3_input_parity.py` also passes.
These tests never control or inject an OS device.
The input dispatcher and cursor regression scripts now run in the CI script
smoke jobs. Shared-header/pinned-revision and release-tooling checks also pass.
Generated offline documentation includes the linked UI fixtures, verification
scripts, migration manifest and dependency notices; its link check passes.

The capture harness's `--retained-open` mode keeps the game hidden/windowed with
mouse/controller input disabled, runs named semantic operations, and records
`ui_retainedOwnership` snapshots around the open interval and after close.
`gameEdit->GetGameTime()` supplies a read-only game clock. The verifier requires
SP time to remain constant while open, MP time to advance, and time to advance
after closing, alongside exact action/state traces and rendered pixel checks.
It captures through the registered engine screenshot command.

Reproduce from a staged package and use fresh output directories for each run:

```powershell
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode sp --renderer gl --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --retained-open --density 1.25 --video-restart --profile-frames 60 --output .tmp/ui/input/sp-gl-125-r8
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode mp --renderer vulkan --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --retained-open --density 2 --width 1920 --height 1080 --profile-frames 60 --output .tmp/ui/input/mp-vulkan-200-r8
python tools/ui/verify_interaction_capture.py .tmp/ui/input/sp-gl-125-r8
python tools/ui/verify_interaction_capture.py .tmp/ui/input/mp-vulkan-200-r8
```

The final Windows package passes both capture verifiers:

| Capture | Output / scale | Game time while open (ms) | After close (ms) | Semantic / pixels |
| --- | --- | --- | --- | --- |
| SP `game/airdefense1`, OpenGL, video restart | 1280×720 / 125% | 190224 → 190224, including restart | 190336 | Pass |
| MP `mp/q4dm1`, Vulkan | 1920×1080 / 200% | 3120 → 4336 | 4432 | Pass |

The host reports active session GUI ownership while open and releases it on
close. Input is suspended in these hidden runs, as intended; the script invokes
semantic controls directly. Each script repetition yields `[0,1,0]` action
batches. SP has two activations/ten state observations across restart; MP has one
activation/five observations. The disabled-plate error is 0.55 of 255 across
186 SP pixels and 600 MP pixels. Pressed orange rail coverage is 1,835 and 3,070
pixels respectively. Both final TGAs are byte-identical to the preceding
visually reviewed interaction captures; their matching vector rails, disabled
plate/label fade and panel chamfers were reviewed again. Static orange Controls
plates are fixture art, not implemented selection state or an accepted screen.

The complete Windows engine/renderer/dedicated and canonical SP/MP build succeeds.
All three native retained/document/vector suites pass; the final production
input, 64-state cursor and SDL parity checks pass. The companion revision is
`300aedd9c56e20666ca7eacc0d71db70502660b9`. Final client SHA-256:
`80a0f7ea94e5b59d19dc2d55f13e301c3d6d2f101ffbfa906f49268da0917aa4`.
Engine screenshot SHA-256 values are
`fd953b5b29811af1f1b46578a5faf0ef5c7453980531e6137df38441a6d1f99b`
(SP/OpenGL) and
`15439fc3c3dc21362b6efd0e3d002b2d9665d3954ff795897148ce6970d50a28`
(MP/Vulkan). Full source/module/log hashes, traces and verifier results are
retained in `.tmp/ui/input/summary.json` and each final `capture.json`.

The 60-frame MSVC Debug draw/submission CPU samples measure 2.580/3.447 ms p50/p95
on OpenGL, 2.408/3.188 ms after restart, and 3.525/4.220 ms on Vulkan. These samples
start after warm-up and are not optimized-build, GPU or total input-processing
measurements. Shared GUI ownership was disabled. SP has zero warnings/errors;
MP has zero errors and the same 93 stock warnings (86 unique messages with
identical multiplicities) as the preceding interaction checkpoint. There are
no retained UI diagnostics. Those existing MP issues remain open.

## Remaining completion work

Interactive device testing and supported-platform qualification, touch/scroll/
text editing and IME, gamepad touchpad behavior, text input/caret ownership,
accessibility exposure, sounds, game action dispatch, bindings, other widgets,
the full editor and all GUI translations remain open. Non-SDL pointer routing
has not been qualified. The separate transformed-overflow clip-mask gap remains
recorded in [semantic controls](interaction.md). This work does not promote the
replacement as the default player interface.
