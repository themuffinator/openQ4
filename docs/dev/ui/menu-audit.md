# Retained menu audit

8 October 2026. A review of the retained (idtech5-ui) menus as they ship with
`ui_retained 1`: the title, the Marine and Strogg pause screens, Single Player
and its Campaign, New Campaign and Arena sub-pages, the Load, Save, Restart
Level, Demos and Credits pages, the MODS dialog, the death screen, the
Multiplayer page with Create Server and Join Game, the SYSTEM page and the
multiplayer Welcome and Escape cards. The HUD, scopes and weapon wheels were
out of scope. It extends the [retained screens](retained-screens.md),
[multiplayer menus](multiplayer-menus.md) and
[runtime performance](runtime-performance.md) records.

## Method

- **Read-only code review** of the session glue (`Session_menu.cpp`,
  `Session.cpp`), the runtime's input and focus (`Interaction.cpp`,
  `Runtime.cpp`, the adapters), the generators and the shipped string tables,
  and a map of every front-end page against its stock route.
- **Engine captures** with `tools/ui/capture_retained_screens.py` of every
  menu scenario at 1280x720, 4:3 and 21:9, in German, French, Hungarian and
  Polish, plus probe scenarios for routes no capture covered: Back on the
  Single Player page reached from the title, Back and LOAD GAME on the death
  screen, and UPDATES. No OS input was sent; every step is a console verb.
  The harness keeps those routes as scenarios: `singleplayer-back`,
  `death` (now with Back), `death-load`, `updates` and
  `createserver-stepping`.
- **CPU measurements** with `openq4-retained-ui-benchmark --screen` and
  main-thread stack samples taken by attaching cdb to it.
- **A second round** the same day took the remaining list's tractable items:
  focus and pointer presentation, the failed-view fallback, Join Game's names,
  the strings and the Escape card's paint. The probe scenarios `probe-focus`,
  `probe-focus-pause` and `probe-ellipsis` (the audit's kit, beside the
  harness) show them in the engine.
- **A third pass** gave the pages section 13.4's prompt bar. Its design was
  chosen from two sketches drawn over engine captures: the title's band
  prompts, or the stock BACK plate with a glyph in place of its marker.
  Engine captures show every page's bar in English at 16:9, with Xbox
  glyphs, in Spanish (Marine and Strogg, the tightest fit), in German at 4:3
  and in Russian at 21:9; the kit's `probe-page-prompts` switches one page
  through every glyph style and Swap Confirm and Back.
- **A fourth pass** shortened the wait for the title. Timestamped startups
  with temporary laps and the screen benchmark found where it went;
  interleaved startups of the builds before and after, and engine probes
  over the hand-offs, measured the change
  ([startup preload](runtime-performance.md#startup-preload)).
- **A fifth pass** shortened map changes. Timestamped chains of single-player
  and multiplayer map changes, interleaved between the builds before and
  after, and an engine probe over the pause's hand-offs after a second map
  change measured the change ([map changes](runtime-performance.md#map-changes)).

## Fixed

| Severity | Finding | Fix |
| --- | --- | --- |
| High | Back on Single Player, reached from the title's SINGLE PLAYER, left a blank stock menu: the legacy menu stayed off its home after the hand-off, so the retained title never came back and the stock page had no buttons. | Back runs the legacy menu's own return from Single Player unseen and rests it at home, and the title returns with its bands from the page dock (`ReturnRetainedSinglePlayerHome`). |
| High | Back (Escape, B, Start) on the death screen closed it: the map was gone, so the player saw a loading screen and then the title. | The death screen's Back keeps it and returns focus to RESTART, and the session ignores close requests from it. |
| High | A deflected stick on the multiplayer Welcome card, which presents by default, sent presses with no release, so the card repeated the move until another key ended it. | The menu pump pairs the stick's steps on the card, as it does on every other retained screen. |
| Medium | Left and right could not change a dropdown: they moved focus, unlike a stock choiceDef and against section 13.5. | Left and right step a closed choice through its eligible options and wrap (`Interaction::ChoiceKey`). |
| Medium | Create Server's ADVANCED OPTIONS button sat in the header over the page title: a second `ADVANCED_TOP` (the dialog's top) shadowed the button's. | The button has its own constant and sits under the server options. |
| Medium | UPDATES faded the title's content for a pop-up that never opens, and its answer, a stock message box, showed the stock title behind it. Any message box at the title did. | UPDATES runs in place; a message box keeps the retained home behind it and gives the screen back as it was. |
| Medium | The death screen's LOAD GAME opened the stock Load Game page. | It opens the retained Load Game page over the title. |
| Medium | On Single Player, Campaign, New Campaign, Arena, Create Server and Join Game a focus change or video restart switched to the game's sound world, silencing the menu. | Every retained front-end screen without a level plays the menu's sounds. |
| Medium | Back from Create Server or Join Game did nothing when the Multiplayer page could not present. | Back finds the stock Multiplayer page. |
| Medium | A join from Join Game that failed or was aborted came back to the title, not to the server list: the legacy menu never left its home on that route. | The session remembers the join's origin, and the menu's next return home presents Join Game once. |
| Medium | The Escape card spent about 14 ms of CPU on every settled frame (debugoptimized), two thirds of it re-walking its 34,395 authored values. | `ApplyMotion` returns at once while nothing it reads has changed: 5.9 ms on the Escape card, 0.5 ms on Join Game ([settled frames](runtime-performance.md#settled-frames)). |
| Low | CONTINUE did nothing when the newest save could not be loaded. | It opens the Load Game page on that save, saying why. |
| Low | The pause and loading screens named difficulty 4 NIGHTMARE, the Arena's name, instead of WARLORD. | They name it WARLORD. |
| Low | The loading screen showed Duel, Clan Arena, Freeze Tag and Red Rover untranslated. | They take their localized names. |
| Low | The prompt bars' SELECT read "modo" in Spanish and "selezionata" in Italian (`#str_200747` is a monitor label). | The prompt uses the localized `#str_231042`. |
| Low | The title's link bar ran past its plinth in German, Turkish, Hungarian, Polish, Czech, Brazilian, Spanish and Russian. | The plinth runs on for each language that needs it. |
| Low | The pause's QUIT CURRENT GAME title was clipped in French. | It runs over the raised trailing section, and the generator fails any dialog title that does not fit in some language. |
| Low | The MODS dialog clipped LOAD MOD in nine languages and its detail labels in five. | The dialog and its plates are as wide as the longest language, and the values start after the longest label. |
| Low | The Create Server, Join Game and Multiplayer crumbs never shrank: their fit read only openQ4's own string tables. | The fit reads the stock tables too, so they shrink like the others, down to the 13 dp floor. |
| Medium | The title, both pause screens, Single Player, Campaign and Multiplayer opened with nothing focused, so a controller's first Accept did nothing (section 13.4 puts initial focus on the primary action). | Each opens on its primary action: CONTINUE or SINGLE PLAYER, RESUME, CAMPAIGN, the campaign being played, JOIN GAME. Back returns onto the item that led away, PLAYER SETUP's stock page included. |
| Medium | A focused item stayed lit while the mouse hovered another: two items lit, and on the title two message lines drawn over each other. | The most recent device sets the presentation: while the mouse leads, only the item under the pointer is lit (`Interaction::SetPointerPresentation`). |
| Medium | A focused control that turned hidden or disabled (SYSTEM's Apply, a Join Game row a refresh removed) lost the focus, and the next move started again at the top of the page. | The next move resumes on it, or moves on from where it stood. |
| Medium | The Escape card still spent about 2.1 ms on a settled frame (debugoptimized) reasserting every closed list's options. | A closed list whose readback has not changed since its last paint is left as painted: 0.7 ms. |
| Low | Back on a number field whose editor held nothing to cancel was swallowed once. | It reaches the page. |
| Low | Single Player, Campaign and New Campaign kept presenting a view that stopped drawing after a renderer or language change. | They hand over to the stock screen, as the other pages do. |
| Medium | The opt-in SYSTEM page opened with nothing focused. | Its activation focuses the performance preset after beginning the draft (`tools/ui/retained_system.py`); the row is enabled once the draft opens, and the runtime's retry after the next frame's layout lands the focus there (the `system-optin` capture reports `focus=settings_preset`). |
| Low | Join Game cut long server names and maps mid-letter. | They end in an ellipsis (`text-overflow`, new to the document format). |
| Low | Spanish read ATRÁS for Back in the prompts and the Single Player, Campaign and Multiplayer pages, VOLVER on the other pages. | Both read VOLVER, the stock word. |
| Low | Polish and Russian showed the save names, the save refusals, OK, Cancel and the keycaps in English: they ship no code string table. | Their openQ4 tables carry the sixteen code strings the menus use. |
| Low | Each launch's menu preload read every community map's levelshot out of its package and wrote it into the save path again (`FindMapScreenshot`). | A levelshot extracted by an earlier launch is reused. |
| Medium | Load Game, Save Game, Restart Level, New Campaign, Demos, Credits, Create Server, Join Game and the pause's Objectives page kept the stock BACK plate in the docked bottom band, so controller and keyboard players saw device glyphs only on the title, pause, Single Player, Campaign and Multiplayer screens. | Each page's bottom band carries section 13.4's prompt bar: SELECT and BACK in the active device's glyphs, Back a button where the stock plate stood, which the pointer lights ([page prompt bars](retained-screens.md#page-prompt-bars)). |
| Low | At 21:9 the Single Player, Campaign and Multiplayer prompt bars hung past the bottom band's raised section, and their place differed from the pages'. | They stand where the pages' bars do, on the raised section at every aspect. |
| Medium | The title waited while every retained page loaded, each parsed twice, and every multiplayer map's and Arena match's levelshot precached: its first frame came 13.6 s after launch on a debugoptimized build, 6.8 s of it the session's initialization. | Each page is parsed once, on the job system's workers where it can be, and validates without building unused diagnostics; the pause screens, Save Game, Restart Level and the death screen load with the first level; the levelshots precache on idle front-end frames, and Create Server and the Arena page read any still waiting. The title comes at 8.8 s, after 2.1 s of session initialization ([startup preload](runtime-performance.md#startup-preload)). |
| High | The mouse could not reach Load Game's LOAD GAME and DELETE GAME, Save Game's SAVE GAME, Restart Level's and New Campaign's difficulty plates, New Campaign's START GAME and TURBO MODE, or Demos' PLAY, DELETE and REFRESH. Hit testing takes the topmost box under the pointer, and a full-size group after them in the document, the page's `*-panel` or New Campaign's canvas-size `back-stage`, took their hits even at opacity 0. The keyboard and controllers reached them, and engine captures, which send no input, never showed it. A section plate's box was also its whole 208x52 u art, so on the 30 u difficulty pitch the next plate took the foot of each band. | The plates follow the panel in their page, and New Campaign's prompt bar spans the view on its own. A section plate's target is its 30 u pitch around its band (section 3), from its leading rail to where the plate fades out or its widest label ends, and its art takes no hits, so a plate never covers the next one or the save and demo lists beside it. A native probe moves the pointer to the centre of every control each retained menu screen shows, its tabs and dialogs included, and expects it lit (`CheckMenusPointerReach`); it found nothing else. |
| Medium | Every map change re-initialized every retained page loaded, the startup's document work again on the main thread: 1.5 to 2.1 s of each map change on a debugoptimized build, for 8 to 16 pages. | A page whose document is unchanged keeps its view through the load: the pictures it names resolve again inside it, and its pending input and requests are dropped, as a new view's would be. Kept, they take 47 to 85 ms, and a map change is 1.5 to 2.2 s shorter in single player and multiplayer, its hand-offs as smooth ([map changes](runtime-performance.md#map-changes)). |
| Medium | Stock menus and previously loaded in-world GUIs still reparsed at every map change, spending 173–356 ms on GUI work after the level-load purge. | Unchanged source/include bytes keep the parsed window tree; captured declaration references and live window resources are refreshed inside the load. Four paired SP and MP runs reduce that share to 14–63 ms (81–92%), with only 0–2 ms paired purge differences and matching precache warnings ([stock GUI reuse](runtime-performance.md#stock-gui-reuse)). |

The [retained screens](retained-screens.md#known-limitations) and
[multiplayer menus](multiplayer-menus.md#known-limitations) known limitations
dropped four stale entries: the Escape card's Match page, the Save Game page,
Join Game's controller sorting and Match Control's team join are all built or
fixed.

## Remaining

Highest impact first.

- **Settings.** GAME, CONTROLS and AUDIO, the Load Defaults dialog and Player
  Setup are the stock pages, and the retained SYSTEM page stays opt-in until
  presets can be applied ([effect execution](settings-effect-execution.md)).
  Going to them from the retained title swaps the whole backdrop.
- **Map changes.** Unchanged stock and retained GUIs now keep their parsed
  trees and refresh media during the load
  ([map changes](runtime-performance.md#stock-gui-reuse)). Custom legacy
  widgets without a refresh path and restored GUI trees still reparse.
  Opening Create Server or the Arena page in the title's first two seconds,
  before its idle frames have read every levelshot, still reads the rest
  in one long frame ([startup preload](runtime-performance.md#startup-preload)).
- **Strings.** The Italian and Polish crumbs of long titles are cut at the
  13 dp floor. The separators and formats once joined in C++ and the Demos
  page's mixed-case headings and plates now come from each table
  ([joined lines and capitals](retained-screens.md#joined-lines-and-capitals-24-october-2026)).
- **Join Game** republishes all 64 rows on every change of the scan.
- **Prompt bar verbs.** The pages' Accept now follows focus with short
  verbs in every language (LOAD, DELETE, SAVE, PLAY and JOIN; [page prompt
  bars](retained-screens.md#page-prompt-bars)), and leaves the bar while no
  focused control can act. The bars still list no contextual action: the
  west and north buttons of Join Game and the Save and Load pages refresh
  and sort with no prompt, and the title's and pauses' bars keep SELECT.

## Stock interface round, 10 November 2026

A paired review of the stock (`ui_retained 0`) and retained menus: every
front-end page of each at 1280x720, the rebuilt stock pages at 4:3 and 21:9
and in German and Hungarian. The method, measurements and the differences
kept on purpose are in the [stock interface menus](legacy-menu-layout.md)
record.

| Severity | Interface | Finding | Fix |
| --- | --- | --- | --- |
| High | Stock | The Single Player, Campaign, Arena and Demos pages did not follow the stock page frame the other pages and the retained ones share: home-position bands, centered headlines, bordered cards and boxes, BACK at the bottom-leading corner without its marker. | Each stands in the page frame: docked bands, the title (and the path over it) in the top band's slot, stock plates, the summary in the content column and BACK in the bottom band's raised section. The Arena page takes the retained page's arrangement. |
| Medium | Stock | FRIENDS LIST on the Multiplayer page opened a page nothing fills, and DEMOS stood on the title where the retained interface has it under Multiplayer. | The Multiplayer page reads JOIN GAME, CREATE SERVER, DEMOS and PLAYER SETUP; the title keeps four sections. |
| Medium | Stock | Every Demos filter looked chosen: the GUI compared the filter's name, and GUI expressions compare numbers. | The session publishes `demo_filterIndex`; one tag carries the rail. |
| Medium | Stock | The arrow keys did nothing in the menus; Tab moved the focus without showing it. | The arrow keys step a menu's focus as the D-pad does and light the item reached; a control that uses a key keeps it. |
| Medium | Retained | Join Game showed a full, bright scan bar while no scan ran: under the page's transform a clipping box with no width has no clip mask, so the fill drew whole over its track. | The fill leaves the page until there is progress; the idle track matches the stock one. |
| Low | Stock | Choice rows showed their quotes: MSAA read `'Off'` and the sound device began with a quote. | Quoted choices show without their quotes, as retail read them. |
| Low | Stock | The four pages' backdrop faded to black at the edges of a wide view. | They stand on the retained pages' backdrop, the same at every width. |
| Low | Stock | Arena's progress line and path, and the Demos filters, were cut in longer languages once laid out. | Sized for the longest of the twelve languages. |

A [follow-up](legacy-menu-layout.md#follow-up-10-november-2026) the same
day gave the retained DISPLAY page's Interface heading its upper case and
the HUD tab's Crosshair Size and Crosshair Color rows labels without the
stock colon, and let the retained gate admit a setting only the retained
interface honors.

A [second round](key-bind-glyphs.md) that day took the settings tree:

| Severity | Interface | Was | Now |
| --- | --- | --- | --- |
| High | Stock | The arrow keys never left a key row on CONTROLS, nor a number field on DISPLAY. | Both step on. |
| High | Stock | Focus stepped onto settings rows outside the view; on the scroll bar it stayed. | The page scrolls to the focused row; the bar is no step. |
| Medium | Stock | Focus stepped in file order: V-Sync after Ambient brightness. | Columns from the leading edge, each in reading order. |
| Medium | Both | Binding a keyboard key to an action with two keys cleared its controller buttons too. | An action keeps two keys of each family. |
| Medium | Retained | CONTROLS named keys as text, a controller button by its key code (`JOY3`). | Keycaps, the mouse and the controller's own glyphs. |
| Medium | Retained | Right from a settings page's trailing column, or Left to a hidden row, reached a tab. | The columns' outer edges hold the focus; a hidden row gives way to the one in its place. |
| Low | Stock | CONTROLS joined its key glyphs with "or" and squeezed long key names; Jump's fourth glyph was cut. | A space between glyphs; a keycap as wide as its name. |
| Low | Both | Some English setting labels were in sentence case beside Title Case rows. | Title Case. |

Left open there: DISPLAY in the retained interface still opens the stock
page until a Performance Preset can be applied on the retained one, and the
stock DISPLAY page's Section list does not name the page's headings.

Left open: the stock pause's order, the stock Settings screen's
arrangement and the campaign levels, which stay as the stock composition
([deliberate differences](legacy-menu-layout.md#deliberate-differences)), and
the items under [found, not fixed](legacy-menu-layout.md#found-not-fixed).
