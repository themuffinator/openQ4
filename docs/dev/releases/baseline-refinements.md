# Baseline refinements (unreleased)

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- **Smoother cutscenes, cameras and third person on high-refresh screens.**
  Cutscene and security cameras, third person and the death camera now move
  between the game's 60 Hz ticks together with the world they show. Authored
  camera cuts still cut cleanly.
- **Ragdolls and flying debris stay smooth.** Falling bodies, shell casings,
  barrel fragments and their smoke trails move smoothly above 60 FPS, and your
  weapon's muzzle-flash light and flashlight stay on the gun while you turn.
- **Smoother spectating.** While you spectate a multiplayer match, the players
  you watch move smoothly above 60 FPS, including in third person and the death
  camera. While you play, other players keep the timing that hit detection uses.
- **Optional low-latency look.** Set `in_presentationView 1` to turn the view
  with your mouse, gyro, look keys or stick on every drawn frame instead of up
  to a tick later. Aiming, hit detection and the network are unchanged.
- **A steadier frame cap that burns less CPU.** `com_maxfps` now waits on a
  high-resolution timer and busy-waits only as long as that timer needs,
  instead of a fixed slice of every frame. On Windows 11 the game keeps its
  precise timers while another window covers it, so a listen server behind
  other windows keeps an even tick.
- **Minimized games rest.** A minimized or hidden openQ4 window now draws at
  most 60 frames a second on Windows, Linux and macOS, instead of running at
  its full cap while you cannot see it. The game itself keeps running at full
  speed.

- Command-line multiplayer connections now reach the server reliably when a
  long launch command requires the game to switch from single player.

- More precise motion between game ticks: camera, world and material presentation
  share a high-resolution sample for each draw. Pauses, hitches and clock resets
  keep a bounded presentation phase.
- OpenXR smooth turning now measures elapsed host time without rounding each
  frame to whole milliseconds.
- `com_latencyFlash 1` draws a white bar on the first frame after you press
  fire, so latency testers such as NVIDIA LDAT or OSLTT can measure
  click-to-photon delay. `framePacingSnapshot` now also reports how long the
  engine itself took from your input to handing the frame to the display, so
  the rest of a tester's reading is the GPU, compositor and screen.
- `com_showFramePacing` now reports frame times to a hundredth of a
  millisecond, shows your display's refresh rate, names what is limiting the
  frame rate (the frame cap, vsync, or the frame's own work) and, with a
  240 FPS cap, whether pacing meets openQ4's 240 Hz target.
  `com_preciseFrameWait 0` restores the previous frame-cap wait if you need to
  compare.
- With the Vulkan renderer, `r_vkPresentTiming 1` (then `vid_restart`) measures
  when each frame actually reaches your display, not just when it was drawn;
  `rendererPresentTiming` reports it. It needs a driver with
  `VK_KHR_present_wait` and is off by default.
- The multiplayer guide correctly identifies enhanced prediction and hitscan
  lag compensation as opt-in settings.

## Upgrade notes

- The engine and the SP/MP game modules must come from the same build (game
  API 54); a mismatched module refuses to load.
- Low-latency look is off by default. It steps aside, leaving the view exactly as
  before, while mouse smoothing or filtering is on, in menus and wheels,
  vehicles, cutscenes, third person, VR and demo playback.
- Replace the complete package, including the engine, matching SP/MP modules,
  renderers and bundled openQ4 data. Save formats, network timestamps,
  authoritative gameplay timing and existing controls are unchanged.
- This increment does not promote a new refresh-rate target, headset/backend,
  Android device or UI migration to supported status. Current known compatibility
  gaps remain tracked in the development roadmap.

## Change log

- Settings > Game Options > Controller now offers **Prompt Style**, to pick the
  button symbols menus and the HUD show when a remapper hides your controller,
  and **Swap Confirm and Back**, to choose which face button confirms in menus.
  Both were console-only before; existing choices are kept.
- The controller triggers, and Page Up, Page Down, Home and End on the keyboard,
  now move through long menu lists such as Load Game a page at a time, and the
  triggers adjust a selected slider in larger steps.
- In Join Game, the server you have highlighted stays highlighted while the list
  re-sorts or refreshes, instead of the highlight landing on another server.
- The controller's Menu button now returns straight to the game from the pause
  menu's Save, Load and Restart Level pages.
- On a multiplayer server's Admin page, the Menu button applies your server
  changes; with a dialog or list open, it closes that first.
- With a controller, the Match and Admin pages of the multiplayer menu show
  your triggers beside their sections, which the triggers step through.
- The loading screen's load time uses your language's decimal mark and unit,
  and the Demos page's headings and buttons are in capitals like the other
  pages'.
- The main menu, pause menus and Single Player and Multiplayer pages wrap:
  pressing up at the top moves to the links at the bottom, and down from
  them returns to the top.
- Moving back into a list, such as the saves on the Load Game page, returns
  you to the entry you left instead of the one nearest the button.
- Settings > Audio and Settings > Game Options now use the new interface,
  with every setting of the original pages. Changes apply as you make them,
  and the pages always show the values the game is using. Game Options
  groups its settings under headings, and its Section list jumps straight
  to one. It opens the same way from the multiplayer menu.
- Settings > Controls uses the new interface too: select an action, then
  press its new key. Escape cancels, and Delete clears an action's keys.
  Settings now opens on its list of pages rather than straight into the
  controls.
- Settings itself is now a page in the new interface, from the main menu and
  from the pause menu: choose Controls, Game Options, Display or Audio, or
  Load Defaults, which asks first. Back from a page returns you to the button
  you chose.
- In portrait windows and on 5:4 screens, the Load, Save, Demos and Join Game
  lists now grow to fill the extra height and show more entries.
- If your controller disconnects while you are in the menus, a notice at the
  top right says so until it is back. Notices now stay with you as you move
  between menus.
- Join Game's server details now show the server's full name, which the list
  cuts short when it is long.
- Adding or removing a favorite server in Join Game now shows a short notice
  at the top right, and removing one offers Undo for a few seconds.
- While Join Game is still looking for servers, its list shows empty rows
  instead of a blank space, and the servers it finds fade in.
- When a filter hides every save, demo or server, the empty list now offers
  CLEAR FILTERS right there, and Join Game offers GET NEW LIST when it found
  no servers. One CLEAR FILTERS in Join Game clears the tags, game type, mod
  and player search together.
- On 5:4 screens and in portrait windows, the main menu, pause menus, the
  Load, Save, Restart Level, Demos, Credits and Arena pages, the multiplayer
  pages, the death screen and the in-match menu now fit the screen instead of
  being cut off at the sides.
- A server with a long map rotation now offers up to 128 maps per game type
  in the multiplayer menu's Vote and Admin pages, up from 48.
- The controller's View button now shows the scores (or your objectives)
  while held, instead of opening the menu; Start still opens it. In Join Game
  it hides or shows the server details, whose card shows the button's symbol.
  If you rebound the button yourself, your binding is kept.

- Continue on the title screen and the Load Game page now show each save's
  difficulty and total play time, and the pause menu adds your total play time
  beside the time in the current mission. Objectives you completed on a map
  stay listed after loading a save. Saves made with earlier versions show no
  difficulty or play time; play time starts counting with a new game.

- Menu prompt bars now say what Enter or the confirm button will do: LOAD on
  the save you have chosen, DELETE, SAVE, PLAY or JOIN, in every supported
  language. The prompt disappears when nothing you have selected can be used.

- After Kane's stroggification, the pause menu's quit and exit questions and
  the Save and Load pages' delete and overwrite questions match the Strogg
  menus around them, their text translating from Strogg runes as they open.

- Save and Load Game now offer date, name and location sorting, manual-save and
  autosave filters, and Refresh List. Your selection follows the same save as the
  list changes, and refreshing preserves a name you are typing.

- Multiplayer friend and mute marks now clear when another player takes over
  a vacated slot. The Players menu returns to your statistics if the selected
  player leaves, and pending menu actions cannot follow their replacement.

- Multiplayer player lists now scroll at a readable row height, with column
  headings kept in view. Crowded rosters remain reachable in Welcome, Players
  and the match summary, and kick-vote choices no longer stop after 16 entries.

- The opt-in SYSTEM page now follows performance presets and settings changed
  from the console while it is open. Unfinished edits are preserved with a
  conflict message, and queued controls cannot overwrite newly changed values
  or select an option that has become unavailable.

- Fresh profiles keep Quake 4's stock crosshair hit feedback. Optional hit
  markers remain available in Game settings, and existing saved choices are kept.

- Installing a Windows development build no longer closes openQ4 games running
  from other worktrees or installations.

- Bots can correct jump-pad flights that would overshoot the platform after a
  hit, and brake dangerous momentum when their route ends.
- Bots allow room to slow down after a dodge jump, avoiding hops that land at
  a platform's edge and immediately slide off.
- Bots brake earlier when another player is already using a jump-pad corridor.
- Bots account for repeated jump-pad pushes when checking landing damage and
  reconsider dangerous routes earlier after losing health.
- Bots check their actual takeoff position before jumping onto raised ledges,
  preventing a mistaken wall-slide prediction from approving an overshoot.
- Bots can traverse valid slopes without mistaking the lower support probe for
  an obstructed player position, including under low ceilings. Their checks for
  unsupported edges and hazardous landings remain in place.
- Android touch buttons now respond across their full visible plates. Default
  Pause and Reload placement leaves the multiplayer rank and status readouts clear.
- Improved damaged-save handling: invalid or duplicate entity registrations no
  longer corrupt the identities needed to clean up a failed load.
- Failed save loads now release remaining restored objects and avoid deleting
  attachments a second time during cleanup.
- A damaged save that fails during restoration now returns to the Load Game page
  with guidance, or a message dialog when using the classic menu. The original
  save remains untouched, and another save can be loaded afterward.
- An unavailable optional menu now falls back to the stock screen during save
  loading. Missing GUI definitions required by the save still stop the load
  safely and identify the missing file.
- Creating client effects when their entity table is full now fails safely.
- Interrupted save restores no longer release models or lights using stale
  handles from the saved world.
- Damaged saves with invalid attachment or physics-team links are rejected, and
  partial links are discarded before failed-load cleanup.
- Saved actor and client-effect references now reject incompatible entity types
  before they can be used during restoration or play.
- Invalid saved door and platform activation chains now return to save-load
  recovery, including errors encountered midway through restoring a group.
- Damaged saves containing invalid standalone object classes are rejected before
  construction and return to the load menu. Supported older saves retain their
  existing readers.
- Damaged saves with conflicting script-thread ownership now return safely to
  the load screen instead of leaving cleanup or subsequent play with invalid
  thread references.

- The experimental Android app now checks its extracted bundled content and
  repairs missing or damaged files before starting. Interrupted repairs keep
  saves and the previous package intact. Allow free storage for a replacement
  copy of the damaged file; retail game files remain separate.
- The experimental Android app no longer blocks game-window creation behind
  its startup splash. Fatal errors can replace a still-open game window.
- Missing retail files no longer send Android startup diagnostics to the wrong
  folder; the app keeps its selected game-data location.
- Android's optional new interface now initializes on the game's thread,
  fixing rejected menu creation during startup and save-load preparation.
- The experimental Android app silences audio and releases held controls when
  backgrounded. Returning to a live single-player game opens its pause menu
  before combat resumes, while existing menus and loading screens keep their
  current flow. Android device qualification remains in progress.
- Developers can build a separate x86_64 Android emulator app for diagnosis.
  It keeps its own saves and data and cannot replace the ARM64 phone app.
  This diagnostic build is not offered as a release APK.
- Android's timer wakes less often in the background and avoids prolonged
  catch-up after the system freezes the app, including with accelerated time.
- Android native error reports now stay beside the normal game log, making
  startup failures easier to diagnose.
- Android Back now follows menu and pause navigation. Error screens have larger
  touch controls and can be dismissed with Back, without opening the on-screen
  keyboard over the report. Dragging away from a button cancels the tap.
- On Android 11 and later, the game renders beside camera cutouts so they do
  not cover HUD controls. The surrounding strip stays black.
- The experimental Android app now opens a language and game-data setup screen.
  Choose your own Quake 4 folder with the system picker, follow copy/check
  progress and retry interrupted work without losing completed archives or
  saves. Setup includes English, German, Spanish and French recovery messages;
  select Single player or Multiplayer once the files are ready. Physical device
  testing is still in progress.
- Android setup can now back up saved games, including descriptions and previews,
  or export diagnostic logs to a ZIP through the system file picker. Setup
  failures are included in the logs. Cancelled or failed exports keep the
  original files; retry uses a new destination. See the
  [Android setup guide](../../user/android-setup.md) for backup limits and
  upgrade guidance. Physical device/provider qualification remains in progress.
- Open **Android setup** from the game's Settings and use **Return to game**
  to keep playing the same session. Quit the game before importing files or
  exporting saves and logs.
- Android setup now offers Default, Compact and Left-handed touch layouts,
  button size and look sensitivity with a drag test. Apply your choices before
  launching or returning to the game. Unapplied drafts leave your active
  controls unchanged.
- Android setup now has an **Interface** page. Choose a viewing profile such as
  Phone, Tablet or Couch, or set the menu and text size yourself, with a live
  preview. Applied sizes reach the game at its next launch or return from
  setup; size changes you make later in the game are kept. See the
  [Android setup guide](../../user/android-setup.md#adjust-menu-and-text-size).
- The standalone Android app now provides experimental touch gameplay controls:
  move, look and fire together, tap to cycle weapons or hold for the selector,
  and pause without a controller. Reload and zoom appear for supported weapons.
  Layout editing and portrait presentation are still in development; see the
  [touch controls guide](../../user/android-setup.md#play-with-touch-controls).

- Experimental OpenXR mode now returns safely to desktop play after frame or
  session failures and clears held VR controls. A lost session retries after a
  short delay; use `vr_restart` after the runtime itself stops. Screenshots taken
  during VR gameplay now include the world.

- Windows and Linux builds now produce matching bundled content packages, so
  an identical package built on the other platform passes package validation.
- Pausing an Arena match also pauses its game audio clock, while menu sounds stay
  active. Resuming continues from the paused position.
- Retained interfaces can upload generated images with correct transparency and
  bounded resource use, including tall images and renderer restarts.
- Restored game regression coverage and added executable clock/pause checks.
- Refreshed the retail, openQ4 and Awakening GUI inventories, preserving historical
  evidence and identifying changed resources for further validation.
