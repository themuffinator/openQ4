# Retained menu sounds

13 November 2026. Retained menus now sound their own input. A single semantic
cue stream feeds one sound router, which plays the stock vocabulary from
[visual section 8](../ui-visual-design.md#8-motion-transitions-and-sound).
This implements the routing that `MOT-006` (stock semantic sound routing) and
the sound clause of `BEH-005` ask for. The register is not updated and nothing
is accepted: acceptance still needs the sound counters across devices and
modal hand-offs that `MOT-006` names, and a listening review.

## What was wrong

The retained runtime played nothing itself. Activation sounds came from 78
`play main_menu_selection` calls spread through the session's per-page request
handlers. So:

- Moving the focus, with keys, a controller or the mouse, never made the stock
  hover tick (`main_menu_mouseover`).
- A press sounded only when its page's request handler happened to play one.
  Buttons that only run a document program were silent: the title's EXIT and
  its confirmation's NO, the mods list's rows, and Settings' LOAD DEFAULTS.
  Opening, choosing in and closing a dropdown were silent, as was Back closing
  a dialog. On the in-game multiplayer card, vote, appearance and server
  setting choices changed with no sound.
- A sound depended on which request was sent, not on the press. The in-game
  Match page's refresh, which the page sends as it opens, needed a special
  case to stay silent.
- In a level, the session played the paused game world behind the settings
  sub-pages (Game, Controls, Audio), and behind the Arena, Create Server, Join
  Game and Demos pages. They are main-menu pages, but only their parent pages
  were listed, so their sounds started at a frozen sound time and the menu
  music was not heard. `RetainedChildPageActive` now keeps the menu world playing
  there, as it does for their parents.

## Stock vocabulary

Counted in the shipped `guis/mainmenu.gui` and `guis/mpmain.gui`:

| Stock event | Plays | Sound |
| --- | --- | --- |
| `onMouseEnter` on a button or list | 156 + 64 | `main_menu_mouseover` |
| `onAction` of a button, bind field, choice, edit field or list; `onActivate` of a page | 260 + 79 | `main_menu_selection` |
| A value slider's change | 4 + 2 authored, none heard | silent |
| hover exit, Escape, server list refresh | none | silent |

Raven wrote `onAction` plays on the gamma, volume and ambient sliders, but
retail's `idSliderWindow` never runs a slider's action as its value changes
(`Quake4Decompiled-main/src/ui/sliderwindow.cpp`, `SliderWindow_NotifyChange`),
and openQ4's legacy window runs it only for a scrollbar. Stock sliders were
silent. The two shaders are `sound/guis.sndshd` entries at -6 dB with no
occlusion.

## Cues

`Interaction` and `Runtime` produce one `ControlCue` per semantic event
(`src/ui/retained/Interaction.h`, `Runtime::TakeCues`):

| Cue | When | Never |
| --- | --- | --- |
| **Focus** | The *lit* control changes because of a device event. The lit control is the one under the pointer while the pointer leads, otherwise the focus, and in an open list its highlighted option. The events are a navigation press (Tab, arrows, D-pad, stick steps, Home/End, page keys), a wheel step through an open list, or a mouse moving onto a control. | Programmatic focus (page open, modal restore, programs), frames, layout or animation sliding content under a resting pointer, a finger, a held button, a pointer entering a scrollbar, a navigation press with nowhere to go, or a pointer finding the control the keys already lit. |
| **Activate** | A completed press (paired release), a toggle, a choice cycled with Left/Right, a list opened, an option chosen, a list closed by Back or by a press elsewhere, an edit opened with Accept or committed, Back cancelling an edit, and every Back request. | A press dragged off its control, held repeats, a value slider's steps or drag, typing, scrolling, a refused (overflowing) action. |

Keys and a pointer resting on the same control never sound twice, because the
rule compares what is lit, not which device moved.

The adapter (`idUserInterfaceRetained`) adds two cues the runtime cannot see:
a controller shortcut that runs a document event (`onTabPrevious`/`onTabNext`,
`onSecondary`, `onTertiary`, `onMenuButton`, `onViewButton`, the stick presses)
or a swipe across a tab strip is an Activate. The triggers' `onSectionPrevious`
and `onSectionNext` only move between lists, so they are a Focus.

## Router

`Impl::PlayCues` runs once at the end of each `HandleEvent` and each
device-free `openq4_retainedGui` verb:

- At most one sound per input event. An Activate outranks a Focus, so a tap
  that both lands and presses, or a key that lights a control while cycling it,
  sounds its press alone. A mouse move and its click are separate events and
  sound a tick and then a press, as the stock menus did.
- It plays in the world the session has playing, on channel 1, as the
  session's `play` command does. A selection cuts short the tick before it,
  menu music on channel 2 is untouched, and the in-game multiplayer menus sound
  over the match, as stock `mpmain.gui` did through the game world.
- With `ui_retainedTrace 1` each sound logs
  `RETAINED_GUI_SOUND path=... cue=focus|activate node=... shader=... world=menu|game|other|none cues=N`.

The session's 77 duplicate plays are gone, including the
`play main_menu_selection ;` prefixes of in-game multiplayer commands. Two
remain, because the session takes those keys before the page sees them: the
key that completes a CONTROLS binding capture, and Delete or Backspace clearing
a binding row. The controller's clear reaches the page as `onSecondary` and
sounds there.

## Qualification

- `openq4-ui-value-interaction` (`ActivationCues`) covers buttons, toggles,
  silent slider steps and drags, choice cycling, list open, choose and close
  (Back and press elsewhere), drag-off, Back, queue overflow and document
  replacement.
- `openq4-ui-value-runtime` (`SoundCues`) drives the real Runtime and RmlUi
  layout. It covers mouse entry, re-entry and movement within a control, empty
  space, held drags, programmatic focus, keys after a resting pointer,
  navigation edges, silent slider steps, key repeats, click and touch tap,
  list navigation by key and wheel, Back, and a timeline sliding a choice under
  a resting pointer.
- `ui_retained_adapter.py` (`CheckSounds`) compiles the real adapter against a
  recording runtime and sound system. It checks one sound per event with the
  press outranking a tick, channel 1, silence without a playing world, and the
  sounds of shortcuts and swipes.
- `ui_retained_gate.py` and `arena_campaign.py` now require the session's
  request handlers, and the in-game multiplayer card's game commands, to play
  nothing themselves. `arena_sound_pause.py` and `ui_system_session_route.py`
  require a settings sub-page over a level to play the menu world.
- Engine checks on Windows x64 with OpenGL drove the screens through
  `openq4_retainedGui` with no device input, with `ui_retainedTrace 1` and
  `s_showStartSound 1`. Each check below logged exactly one sound per step,
  and no step that should be silent started one:
  - The title and Settings: keys and the pointer tick; EXIT, its dialog's Back,
    SETTINGS and Back select; programmatic focus is silent; the session's
    requests add nothing.
  - `airdefense1`, pause → Settings → Game → Back to the game: a choice cycled
    both ways, its list opened, moved and closed, a toggle pressed twice, and
    silent slider steps. All played in the menu world with an advancing sound
    clock. Before the world fix, the Game page's sounds started at a frozen
    time in the paused game world.
  - A DM listen server on `mp/q4dm1`: the Welcome card's moves tick and its
    Back selects in the game world, once, with no second play from the
    `mpClose` command.

## Loading screen

The single-player loading screen sounds again, under both interfaces. Stock
`generic.gui` and `intro.gui` play `load_screen_ready` from their
`FinishedLoading` event and `load_screen_click` from the continue button's
`onAction`, as `play` commands. The continue gate holds inside the map change
and gives its events to no GUI, so neither command ran. The retained
`loading.rui` that replaces those screens has no scripts. Before this fix, an
`airdefense1` load logged neither sound under `ui_retained 1` or `0`.

The session now plays each sound once per gate (`ExecuteMapChange`) for those
two stock screens and the retained screen. A map's own loading GUI is left
alone.

- When the continue prompt appears, `load_screen_ready` plays in the menu
  world, the world the session plays under a loading screen. The load had muted
  sound, and `Frame` never runs inside the map change. So the gate makes the
  menu world current, unmutes and mixes sound itself. Its first mix brings the
  stopped sound clock up to date, because a sound started at the stale clock
  would count as already played out. The level's own sounds stay out of the
  mix until the press.
- The press plays `load_screen_click` in the game world after that world takes
  over and sound is back on, as the stock `close ; play load_screen_click` did.
  `com_loadingContinueAutoAdvance` stands in for the press and sounds as one. A
  skipped gate (`com_skipLoadingContinue`, multiplayer, loading a save) plays
  neither.
- `loading_continue_input.py` (`validate_loading_screen_sounds`) pins the stock
  scripts and one session play of each shader. It also pins the order of world,
  unmute, clock and stinger, the held gate's mixing, and the click after the
  hand-over.
- Engine checks ran on Windows x64 with OpenGL, `com_loadingContinueAutoAdvance
  3000`, `s_showStartSound 1` and no device input. `airdefense1` under
  `ui_retained 1` and `0`, and `hangar1` under `ui_retained 0`, each started
  `load_screen_ready` once (`world=menu muted=0`) and `load_screen_click` once
  (`world=game muted=0`). With `s_showVoices 1`, the stinger was the gate's only
  voice, at -6 dB, for 45 mixes from the first mix after it started, and the
  click was in the game world's first mix. With `com_skipLoadingContinue 1`,
  neither started.

## Limits

- Documents cannot name their own sounds. World terminals (`guisound_beep2`)
  wait for retained world-surface output, which does not exist yet.
- The weapon wheel (silent by design) and the HUD documents have no controls
  and make no cues.
- Stock pages that still run under `ui_retained 1` keep their own scripts.
