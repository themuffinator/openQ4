# openQ4 release completion and evidence record

**Release status, 23 October 2026:** 0.13.2 is the latest available release.
0.14.0 (`v0.14.000`) was published and then withdrawn because it was broken.
Later implementation and validation entries below are unreleased development
work. Historical CI/publication success does not restore the withdrawn release.
See [player release status](../user/release-status.md).

## The flashlight casts shadows on every preset that draws them, 10 November 2026

- **Low, Performance, Balanced and Enhanced now give the flashlight its
  shadows.** As in the original game, it used to cast them only on the top
  quality setting (Quality, Ultra and Maximum). Every preset that draws shadows
  now includes the flashlight: Enhanced and above draw it as one shadow map,
  the presets with the original stencil shadows draw it the way the original
  game's top setting did. Minimum and Low Power still draw no shadows.
- **Projectile lights keep the original rule.** Rockets, plasma and other
  glowing projectiles still cast shadows only on Quality, Ultra and Maximum.
- **Saved games follow the new setting.** A game saved while the flashlight
  had no shadow gets it back when the save is loaded.
- **Nothing to do.** `g_dynamicLightShadows 0` still turns these shadows off,
  and `1` turns projectile-light shadows on everywhere. Multiplayer follows
  the same rule.
- Evidence: Air Defense 1 under Vulkan on Balanced (stencil) and Enhanced
  (one projected shadow map reported for the flashlight), each against
  `g_dynamicLightShadows 0`; no new warnings.

## Display settings open in the new interface, 10 November 2026

- **Settings > Display is now the new page.** In the new interface, Display
  opens with Graphics, Screen and Image Quality tabs, like the other settings
  pages. It used to hand over to the classic Display page.
- **Performance presets and Auto-Detect apply from it.** Choose a preset and
  press Apply Changes. The renderer restarts once and reloads its textures,
  and the sound device restarts if the speaker layout or reverb setting
  changed. A preset that changes anti-aliasing asks you to keep or revert, as
  any display change does. Other presets apply without a question.
- **Textures load once, not twice.** After a preset, after a `vid_restart`
  that follows a texture quality change, and at a start with reduced
  textures, the game used to load its textures twice in a row. It now loads
  them once.
- **Two details wait for the next start.** High-quality texture flags and the
  per-sound sample limit fully change when the game next starts, as they
  always have. The page tells you when your changes include one of them.
- **Your previous settings come back if applying fails.** This also holds if
  the game closes while it is applying or waiting for Keep: the next start
  restores them.
- **Nothing to do.** The classic interface (`ui_retained 0`) keeps the classic
  Display page.
- Evidence: [the combined executor](ui/settings-effect-execution.md#combined-executor)
  and [the gate](ui/system-resolution-rows.md#the-gate);
  `tools/tests/ui_settings_display_service.py`,
  `tools/tests/ui_settings_service.py`, `tools/tests/ui_retained_gate.py`.

## Materials that never change are worked out once, 10 November 2026

- **An engine repair with no visible change.** Most materials have settings
  that never change while you play. The engine was meant to work those out
  once, when the material loads; a missing line made it redo them for every
  surface in every frame, on OpenGL and on Vulkan. It now does them once, as
  the original engine did.
- **Do not expect a higher frame rate from it.** In the three scenes measured
  it saves 5 to 22 millionths of a second per frame, 0.1 to 0.5 percent of the
  frame's processor time, which whole-frame timings on the test machine cannot
  resolve. Pictures taken with the repair on and off are identical in every
  pixel on both renderers.
- **Nothing to change.** `r_useConstantMaterials 0` restores the earlier
  behaviour without a restart, should a material ever look wrong.
- Evidence: [Constant material registers](material-constant-registers.md);
  FPS-08 in the [roadmap](roadmap.md);
  `tools/tests/material_constant_registers_contract.py`; the
  `rendererPBRMaterialSelfTest` command.

## Key pictures on the Controls page, and menus that step the way they read, 10 November 2026

- **The new interface's Controls page shows keys as pictures.** Each action
  shows keycaps for its keyboard keys, a mouse with the bound button marked,
  and your controller's own buttons, where it used to print names such as
  `JOY3`. A row that is waiting for a key says so in orange.
- **Binding a key no longer unbinds your controller.** An action keeps up to
  two keyboard or mouse keys and up to two controller buttons. A third key
  replaces the two of its own kind only. Before, binding any key to an
  action such as Jump cleared its controller buttons as well. This applies
  to both interfaces.
- **The classic Controls page is easier to read.** Long key names such as
  LEFT SHIFT and RIGHT ALT get a keycap wide enough to read, and the word
  "or" between keys is gone, so every key of an action fits its row.
- **Keyboard and controller navigation in the classic settings.**
  - The arrow keys no longer get stuck on the first key row of Controls, on
    a number box such as Brightness, or on the scroll bar.
  - Focus moves down a page in the order it reads. On Display it used to
    jump between rows far apart.
  - The Display, Audio and Game Options pages scroll to the row you move
    to. Before, focus could move to rows that were off the screen.
- **Fixes in the new interface's settings.**
  - Pressing Right on the last column, or Left towards a hidden row, no
    longer jumps up to a tab.
  - A few English labels now use the same capitals as their neighbours, for
    example UI Scale, Rendering Backend and Enable Shadows.
- Not changed in this round: Display in the new interface still opened the
  classic Display page. It opens the new page since later the same day
  ([above](#display-settings-open-in-the-new-interface-10-november-2026)).
- Evidence: [key glyphs and focus](ui/key-bind-glyphs.md#evidence-10-november-2026);
  `tools/tests/key_bind_presentation.py`,
  `tools/tests/menu_controller_navigation.py`.

## Classic menus that match the new ones, and easier to drive from the keyboard, 10 November 2026

- **The classic Single Player, Campaign, Arena and Demos pages look like the
  rest of the menus.** With `ui_retained 0`, or whenever a new-interface
  screen cannot be shown, these pages now use the same frame as every other
  page: the title at the top left, plates down the left, the description
  beside them and BACK at the bottom right. They had their own centered
  headlines, bordered cards and a BACK button in the opposite corner. The
  Arena page is arranged as in the new interface, with tiers and matches as
  plates and the chosen match on the right.
- **Demos sits under Multiplayer in both interfaces.** The classic
  Multiplayer page now reads JOIN GAME, CREATE SERVER, DEMOS and PLAYER
  SETUP, as the new one does. It replaces FRIENDS LIST, which opened an empty
  page. Back from Demos returns to the Multiplayer page.
- **Arrow keys work in the classic menus.** Up, Down, Left and Right move
  between menu items and highlight the one they reach, and Enter chooses it.
  Before, only Tab moved, and nothing showed where it was. Lists, sliders,
  choices and text fields keep the arrow keys they use.
- **Fixes.**
  - Classic settings no longer show stray quote marks: MSAA reads Off, not
    'Off', and the sound device no longer starts with a quote.
  - In the classic Demos page only the filter you chose is marked. All five
    were marked at once.
  - In the new interface, the server browser no longer shows a bright orange
    bar under the list while no scan is running.
  - In the new interface's settings, the Interface heading is in capitals
    like the other headings, and Crosshair Size and Crosshair Color no longer
    end in a colon.
  - The classic pages no longer fade to black at the edges of a widescreen
    view, and their labels fit in all twelve languages.
- Evidence: [stock interface menus](ui/legacy-menu-layout.md#evidence-10-november-2026);
  `tools/tests/choice_gui_value_contract.py`,
  `tools/tests/menu_controller_navigation.py`.

## Animated crosshairs for the new interface, 10 November 2026

- **Each weapon's crosshair now answers the weapon.** A new option,
  `Settings -> Game Options -> HUD -> Animated Crosshair`
  (`hud_crosshairAnimated`, off by default), draws the original Quake 4
  crosshair designs live. Every design assembles as the weapon comes up, kicks
  with each shot, dims and shows the reload while you can't fire, and folds
  away for the use hand and the other context cursors.
- **Each design shows its own weapon's state.** The blaster's arcs wind shut
  into a ring as it charges, the hyperblaster's turn with its barrels, the
  machinegun's ticks walk out under sustained fire, the shotgun's ring is
  thrown wide and its reload counts the shells back in, the rocket launcher's
  halo sweeps round until the next rocket is ready, the railgun's bars come
  home as it recharges, the nailgun's brackets close with the drum and clamp
  on a seeker lock, the lightning gun's darts are drawn in by the beam, and
  the dark matter gun's quarters draw tight as its core charges.
- **Hit feedback sits inside the crosshair.** With the hit marker on, its marks
  dock into the clear space of the design in hand instead of being laid over
  it, with the same sizes, colors and timing; the lightning gun's darts are the
  marks. Every hit also squeezes and tints the design, and a kill sends a thin
  ring out from its edge. This works with the hit marker off too.
- **Vehicle guns are covered.** The gun of your seat gets the same treatment,
  a cannon shows its recharge as a measured sweep, and hits you land from a
  vehicle now show on its crosshair.
- **Requirements and limits.** New interface only (`ui_retained 1`, the
  default); the classic interface keeps the still crosshair. It follows the
  crosshair size and color settings, a chosen custom crosshair, Reduced
  Flashing and reduced motion. The Awakening's space cannon keeps its own
  lock-on reticule.
- **Validation.** Captured headless from the debug build at 1920x1080 on
  OpenGL: all twelve designs through deploy, shot, cycle, hit, kill, reload,
  empty, charge and lock; the ten campaign weapons fired through the game's
  own test input; the hover tank's cannon and machinegun on `game/convoy1`;
  the context-cursor handoff, the option toggled in play and from the retained
  menu, the scoped machinegun, sizes 16 and 48, a tinted color, reduced motion
  and the classic interface. `crosshair_cvar_refresh_contract.py` pins the
  wiring in both game trees. The multiplayer module, which shares the
  single-player implementation, was captured on a listen server with bots:
  the weapon in hand, hits, kills and held states. Weapons fired under a
  remote client's prediction were not exercised.

## Graphics presets that cover the whole range of hardware, 10 November 2026

- **A new Low preset for PCs at the minimum requirements.** Low keeps the
  original shadows and a 60 FPS cap at a reduced scene size and texture
  budget. Auto-Detect now chooses it for a PC with at most 4 GB of RAM or a
  1 GB graphics card, which used to get Low Power and its 30 FPS cap. The stock
  menu's Low quality uses it too.
- **Minimum and Low Power now draw no shadows.** They used to lower only the
  resolution and texture size; they now also skip shadow rendering, which took
  12 to 16 percent off each frame on the integrated GPU we measured. They are for
  software renderers, Raspberry Pi-class boards, low-memory phones and battery
  saving. If you already use one of them and want the change, select the
  preset again; your current settings are not altered until you do.
- **Auto-Detect now recognizes high-end graphics cards.** It used to stop at
  Enhanced. A 12 GB card now starts on Quality and a 16 GB card on Ultra at up
  to 1440p, stepping down one preset at 4K; a 24 GB card keeps Ultra at 4K.
  Maximum is still yours to choose.
- **Better starting points on laptops and ultrawide displays.** 1920x1200 and
  2560x1600 panels are treated like 1080p and 1440p, so integrated graphics on
  a 16:10 laptop start on Balanced at native resolution instead of a reduced
  scene.
- **Phones.** A phone with about 12 GB of RAM renders at 85% of its panel
  instead of half, and a detected frame cap no longer rises to a 120 Hz panel:
  phones, Steam Deck and Raspberry Pi keep their preset's cap to save power.
- **More GPUs are identified correctly under OpenGL**, including recent AMD
  and Intel integrated graphics whose driver names were not recognized.

Run **Auto-Detect** in `Settings -> System` to get the new recommendation; an
existing configuration is never changed on its own. Details are in the
[display settings guide](../user/display-settings.md#performance-presets) and
the [audit notes](ui/performance-presets.md#hardware-range-audit-10-november-2026).

## VR: shots that stay on the laser, a weapon wheel that answers your hand, and a pause when the headset comes off, 10 November 2026

- **Shots land on the laser dot after a save, a ride or a cutscene.** Loading
  a save, leaving a vehicle, sitting through a cutscene, teleporting or
  clicking the weapon-hand stick could leave every shot, and what you pointed
  at on world screens, a fixed angle above or below the laser. Nothing you did
  with your hand took it out again. The game's aim now always has the
  controller's own pitch. In multiplayer the server applies the same rule, so
  both the server and the VR player need this build.
- **Levels and loaded saves start facing the right way.** After snap turns,
  the next level started turned by as much as you had turned in the level
  before, and so did a save loaded after turning. Both now start facing the
  way the level, or the save, faces.
- **The weapon wheel answers your hand.** With the modern interface (the
  default), the wheel opened but turning your weapon hand selected nothing,
  so it could only keep the weapon you held. It follows your hand again, in
  single player and multiplayer, and so does multiplayer's quick-chat wheel.
- **Single player pauses when the headset comes off.** Taking the headset off,
  opening your runtime's own menu (the SteamVR dashboard, the Meta menu) or
  losing the runtime now opens the pause menu, so nothing happens to you while
  you can't see or steer. Resume from the menu when you're back.
  `vr_pauseOnFocusLoss 0` keeps the game running. Multiplayer can't pause.
- **Your headset's own recentre no longer jumps.** A runtime announces a
  recentre a moment before it applies it. openQ4 recentred at the
  announcement, measured against the old position, and the view jumped when
  the new one arrived. It now follows on the frame the headset applies it.
- **Zoom eases in and out.** A scoped weapon's zoom held the view still for
  the length of the zoom and then jumped to full magnification, and jumped
  back on release. It now eases, as on a monitor.
- **The Arena's entrance, intro and victory shots play on the floating
  screen.** They are authored camera shots; the headset showed your own locked
  view instead, turning with your weapon hand.
- **Smaller fixes.**
  - `vr_restart` looks for the runtime and the headset at once. After a failed
    start it could do nothing for up to nine seconds.
  - `vr_status` reports the headset's refresh rate.
  - A session the runtime would not begin is opened again after three seconds;
    it used to stay idle until `vr_restart`.
  - Resizing the desktop window in VR resizes its mirror. The mirror kept the
    old size until VR stopped, and on Vulkan the flat view after VR kept it too.
  - The Awakening: a jammed turret (the dropship's top gun in the third
    mission) no longer follows your head or controller while it is jammed.
  - With `vr_vehicleStereo 0`, aiming a vehicle's turret aside no longer makes
    the stick drive it sideways.
  - OpenGL: menu and HUD drawing after a development screenshot taken
    mid-frame keeps its transparency in the headset.
  - Vulkan: with the experimental render thread (`r_vkRenderThread`), VR frames
    render on the main thread. The headset's images were handed back to the
    runtime while the render thread could still be drawing into them.
- **Not yet checked on a headset.** These were found by reading the code and
  verified on the simulated OpenXR runtime; no headset was available. Known
  and left for later: room-scale walking can shimmer by up to a tic's step
  against the smoothed view, a save made in a vehicle seat can load facing
  away from the cockpit, multiplayer's comfort vignette also darkens on real
  steps, multiplayer damage numbers are placed from the right eye, a demo
  recorded in VR stores both eyes' overlays, and checkpoint thumbnails show
  the desktop mirror.
- Evidence: `openxr_vr_smoke.py` (weapon wheel, focus-loss pause),
  `openxr_vr_playspace_smoke.py` (runtime recentre, level and save start
  facing, aim pitch after centring the view and after a load),
  `openxr_vr_contract.py`, the native XR lifecycle cases;
  [VR roadmap](roadmap.md#vr-task-evidence-and-acceptance).

## HUD fixes, and a classic-interface HUD that matches the new one, 10 November 2026

- **HUD size and opacity now work in the classic interface.** With
  `ui_retained 0`, the HUD Size and HUD Opacity settings (`ui_hudScale`,
  `ui_hudOpacity`) apply as they do in the modern interface: each group of the
  HUD grows or shrinks from its own screen edge, and the whole HUD fades.
- **Spectators, votes and respawns get the new displays there too.** A card
  above the status bar names the player you follow, with their place and
  score. A vote shows as a card at the left edge with the Yes and No keys, a
  tally for each side and the time left. While you wait to respawn, a prompt
  names the control and counts down.
- **The classic-interface HUD sits where the new one does.** Text sizes and
  positions were measured against the modern HUD and corrected in the player,
  vehicle and multiplayer HUDs. Your standing row, your team's panel and kill
  feed lines about you carry the orange marker, a locked vehicle shows a
  padlock, and the damage direction has a mark at its peak, so it no longer
  depends on color alone.
- **Classic-interface fixes.**
  - Tourney bracket names sat low and the second row was cut off; long names
    now end in "..." in the bracket and the kill feed.
  - A key named in a notice ("Press F3 to ready") is written out; its keycap
    used to push the Tourney message bar's text under the bar and split the
    warm-up notice over two lines.
  - In a vehicle, the exit notice, the save notice and the missile warning no
    longer draw over the boss bar.
  - The Minimal layout now hides the centre notices, awards, aim text and the
    managed-match card, as the modern HUD's Minimal does, and the
    managed-match card no longer covers the kill feed.
  - A dead player's health reads 0, not -100.
  - A failed mission's fade to black covers the whole screen; on a wide
    screen it left lit strips at the sides.
- **The Awakening's HUD follows in the classic interface.** On foot in the
  expansion, the classic-interface HUD now sits where the modern one does,
  with all 13 weapons in the strip, and takes the HUD layout, Size and
  Opacity settings. On the dropship mission its status panel shows hull
  damage, objective markers and attack warnings while you are on foot; before,
  only the gun's display showed them. Saves made before this update load as
  usual.
- **The Awakening's vehicles follow too.** In the classic interface the
  dropship guns, the MCC gun and the speeder bike now use the same layout as
  the modern HUD.
  - The guns gain the radio display, the boss bar, notices and an Exit
    prompt.
  - The MCC gun's hull gauge fills with the hull and turns red at a quarter.
  - The speeder bike shows its hull and shield warnings and its weapon lines
    again, and the right weapon for each of its guns. Its prompt names the
    boost key and shows a hazard while you boost; the expansion's own boost
    warning never appeared. The race timer no longer shows placeholder
    digits before a race starts.
  - A vehicle the level has locked shows "Vehicle locked", not "Exit". The
    same now holds for the walker and the hover tank in Quake 4's own
    campaign.
- **The Awakening, modern interface.** The dropship panel keeps its damage and
  objective markers after you load a save or change the HUD layout; it used
  to come back empty.
- **Fixes in both interfaces.**
  - In the modern interface, the vote, statistics and managed-match cards
    have their dark plate behind the whole card; it drew as a thin band
    across the top.
  - Multiplayer obituaries in the console use each weapon's own line again,
    not "was killed by" for every death.
  - Dead Zone's score flash lasts as long at 240 Hz as at 60, is no longer
    cut short by the next update from a dedicated server, and no longer fires
    with its sound on the first frame of the next Dead Zone map.
  - The timer no longer turns to its last-minute colors during a long warm-up
    or after the match ends.
  - Weapon names with accented or Cyrillic letters are upper-cased correctly
    in the weapon wheel.
  - `pm_air 0` no longer crashes single player, and a vehicle without a HUD
    no longer crashes when it takes electrical damage or a missile locks on.
  - With a mod that brings its own multiplayer HUD, chat lines and the chat
    input no longer disappear in the modern interface. A mod that does not
    replace a HUD now gets the same HUD in both interfaces.
- **Less HUD work every frame.** The multiplayer HUD's state is published
  once per game tick, not once per drawn frame, and not at all in the Classic
  layout. Holding a powerup no longer re-evaluates the HUD every frame, nor
  does turning the view. Engine-owned layout values are written to each
  interface only when the view changes.
- **Older saves still load.** The player and vehicle HUDs changed again, so the
  previous definitions are kept for saves written before this build.
- Evidence: [stock interface HUD layout](ui/legacy-hud-layout.md#evidence-10-november-2026);
  `tools/tests/savegame_gui_layout_contract.py`.

## A console that stops scrolling when you let go, 10 November 2026

- **Page Up and Page Down stop when the key is released** (GitHub issue
  #214). Holding either for half a second on a long listing such as
  `listImages` left the log scrolling for many seconds afterwards. Two things
  combined: the game repeated the key itself once per drawn frame, on top of
  the system's key repeat, so a short hold asked for hundreds of lines; and
  the scrolling animation moved at a fixed 24 lines a second, so it took as
  long as that distance needed, up to half a minute for a full log. A held
  key now repeats at your system's key-repeat rate only, and the animation
  finishes within half a second whatever the distance. The same repeat also
  ran with the console closed, so holding a key bound to Page Up or Page Down
  in a game scrolled the hidden log away from its newest line; that is gone
  too.
- **Escape closes the console.** With a completion list showing it closes the
  list first. Before, Escape in a game closed the console and opened the menu
  in the same press, and over a menu it did nothing.
- **Command history keeps what you were typing.** Pressing Up and then Down
  returns to the unfinished line. Before, Down past the newest command showed
  the oldest one once 64 commands had been run, and Up on an empty history
  erased the line. Blank lines and a command run twice in a row no longer
  take a history slot each.
- **Home, End and the mouse wheel do what they do elsewhere.** Home and End
  move the caret even while the completion list is showing, where they used to
  jump through the list. The wheel scrolls the log unless the pointer is over
  the list. Ctrl+Home and Ctrl+End jump to the oldest and newest line of the
  log; the edit line used to take both keys first, so they never did.
- **Smaller fixes.** Running a command returns the log to its newest line, so
  the answer is in view. The caret stays visible while it is being moved
  instead of blinking through it. Pasting turns a tab or line break between
  two words into a space instead of joining them, and no longer stalls on a
  long line. A long command is no longer drawn through the version number.
  The scrollbar's hover animation takes the same time at any frame rate.

Nothing needs to be changed. `con_scrollSmoothSpeed` keeps its value and now
sets the slowest speed of the animation; see
[Using the Console](../user/input-settings.md#using-the-console) for every key.
Covered by the `openq4-console-input` native test and
`tools/tests/console_input_contract.py`.

Not changed, and still open: text copied from the log is in the console font's
8-bit codepage, so non-ASCII characters do not survive the clipboard; a
selection dragged past the top or bottom of the log does not scroll it; and
there is no double-click word selection.

## Servers that outlast 25 days, safer custom content and quicker starts from a third audit round, 10 November 2026

- **A server left running no longer stops after 25 days.** The game's clock
  counter wraps after 24.86 days of uptime, and from that moment no game tic
  ran for the next seven weeks: the server was up but frozen. It now carries
  on across the wrap. Nothing needs to be changed.
- **A failed join no longer hides your files.** Joining a pure server locks
  the game to the server's list of packs. If the join then ended early (a
  full server, a refusal, a cancelled download or backing out), the lock
  stayed on: loose files and any pack the server had not listed were missing
  from the menus, from single player and on the next server, until a later
  server was joined and left or the game was restarted. The lock is now
  released when the attempt ends.
- **Custom maps, models and textures are checked as they load.** These files
  could make the game write outside its own tables, so a download could
  crash it or worse. Each is now refused or read safely:
  - an animation or mesh declaring more frames, weights or joints than its
    tables were sized for;
  - a collision file, bot navigation file or map patch naming entries
    outside itself;
  - LightWave and MD3 models with out-of-range indices or offsets;
  - a JPEG that ends early, an image program whose inner image is missing,
    and a material table given an invalid index.
- **Multiplayer fixes.**
  - A passed vote to shuffle teams could end the match as a forfeit, because
    one side was briefly empty while players were being moved.
  - A passed vote for a game type few maps support could end the server's
    session with an error instead of picking one of those maps.
  - Dying to the map within a second of being knocked by a player who then
    disconnected read freed memory to award the kill.
  - A bot's grudge passed to whoever joined into the slot of the player it
    was against.
- **Clients stand up better to hostile servers.** A server could crash a
  client by following a disconnect with more messages in the same packet, or
  end it with an error by listing one pack twice.
- **Scripts and saves.** The script function `sys.strFind` misread memory on
  every call; `%` with a divisor between -1 and 1 crashed the game; an error
  message containing `%` printed garbage or crashed; four animation frame
  commands crashed when written without their argument; weapons naming a
  joint their model lacks corrupted memory. A game saved during a lightning
  death now restores that effect correctly, and a save naming an impossible
  local player is refused.
- **Console.** Completing `exec` now lists configuration files inside packs.
- **Faster.**
  - Starting the game reads its definition files in about a fifth less time
    (592 to 462 ms on this development machine, median of four hidden starts).
  - Freeing a dictionary string no longer walks every string in the game;
    clearing a level's data did that once for each string it freed.
  - A server with no bots no longer rebuilds the bots' view of the world
    every frame, and each shot no longer searches every weapon slot by name.
- **For developers.** `untrusted_file_bounds.py` pins the loader, script and
  save properties above, and `network_security.py` the client ones; both run
  in CI.
- Validation: the native suite passes 338 of 338. The tic harness now runs
  the clock across its wrap and fails without the fix. All 1,205 stock models
  load through the changed model readers. Hidden, input-free probes: four
  campaign maps on OpenGL and one on Vulkan, each with a demo recorded and
  replayed, with the same warnings as the build before this round; a
  multiplayer listen server with four bots and a remote client on both
  renderers; and the loopback reconnect smoke. Listing the root for `.cfg`
  files returns 45 names where the build before returned 3.
  The 252 Python contract tests that read the changed files pass.

## Shadow maps cost far less, and match the original shadows in more places, 9 November 2026

- **Busy rooms run faster with shadow maps on.** In Quake 4 nineteen in twenty
  shadow-casting lights are point lights, and a busy room shows forty at once;
  the game kept the shadow maps of only twelve and redrew the rest every
  frame. It now keeps a whole view's worth. In the densest room of the
  campaign (Nexus Hub 2, Quality preset, OpenGL) the frame's graphics-card
  time fell from 12.0 to 6.6 ms and its processor time from 11.8 to 5.2 ms.
- **A character walking under a light no longer makes that light redraw its
  whole shadow map**, on OpenGL and Vulkan: only the character is drawn over
  the stored map. Grates and fences no longer keep a light from storing its
  map either. On Vulkan this is a small gain, about a tenth of a millisecond
  of graphics-card time in a busy fight.
- **Shadow maps no longer pay for the original shadows as well.** With shadow
  maps on, the game still built the original shadow shapes for every animated
  character under most lights, every frame, and never drew them. It now
  builds none; in a fight with ten characters on screen that was over thirty
  a frame. The saving is about a tenth of a millisecond of processor time per
  frame there.
- **Distant scenery keeps a soft shadow.** Once a door frame or prop was far
  enough away to look small on screen, its shadow switched to the original
  hard-edged kind, and the light around it stayed on the slower mixed path for
  the rest of the level.
- **Rooms with several spotlights keep all of them on shadow maps.** At the
  Quality and Ultra presets only four spotlight shadow maps fitted at once. A
  view with more (the start of Convoy 2 has up to eight spotlights) dropped
  the rest to the original hard-edged shadows for the rest of the level on
  Vulkan, with a frame where one cast no shadow at all, and redrew them every
  frame on OpenGL. The smallest spotlights in such a view now use a half or
  quarter resolution map so that all of them fit. Views that fitted before
  look the same as before.
- **A room full of spotlights no longer redraws some of their shadows every
  frame.** The memory kept for spotlight shadows that do not change held eight
  maps whatever their size, so the Convoy 2 view above redrew three of its
  eleven each frame. It is now measured by size: the smaller maps of a crowded
  view take a quarter of the room each and all of them stay cached, in the
  same memory as before. The saving in that view is small, about a fifth of a
  millisecond of GPU time on Vulkan. `r_shadowMapProjectedCacheSize` keeps its
  default and now counts full-size maps' worth of memory.
- **Point-light shadows take half the video memory on OpenGL.** Each point
  light's shadow was stored twice: once as depth, and once as a colour copy
  that only older graphics paths and debug views read. The copy is no longer
  made where nothing reads it. In a busy room that is about 250 MiB less, the
  shadows of every light in view stay cached where 42 fitted before, and
  frames are 0.2 to 0.45 ms shorter on the graphics card. Nothing needs
  changing; `r_shadowMapPointDepthOnly 0` brings the copy back if a driver
  objects.
- **Lights behind grates, railings and wire caps no longer redraw their
  shadows every frame.** Any shadow caster with holes cut in it by its texture
  was treated as if the holes could move, so every light reaching one copied
  and redrew its shadow map each frame. Most such surfaces never change, and
  they are now drawn into the shadow map once and kept. At the start of
  Convoy 2, where every lamp sits in a wire cap, frames are about 1.75 ms
  shorter on the graphics card on OpenGL and 0.5 ms on Vulkan. Surfaces whose
  holes do animate are still redrawn each frame, and the picture is unchanged.
- **Spotlight shadows that have not changed cost almost nothing on Vulkan.**
  Vulkan kept each unchanged spotlight shadow in memory, then copied it into
  a shared texture every frame before using it. It now uses the stored shadow
  directly unless something is moving in that light. At the start of Convoy 2
  the shadow work each frame falls from 0.68 to 0.18 ms of graphics-card time,
  and the picture is identical. Nothing needs changing;
  `r_vkShadowSampleResident 0` brings the copy back for comparison.
- **Walking past lights no longer redraws their shadows.** A light's stored
  shadow was thrown away and drawn again each time a nearby object's old-style
  shadow came into or went out of view, which happens constantly on the move
  and changes nothing in the stored shadow. On a walk through the Nexus Hub
  about half of all shadow redraws were of this kind, on both renderers; they
  no longer happen, so far fewer frames spend time on shadows while you move.
- **Soft shadow edges no longer shimmer when nothing in them moves.** The fine
  grain that softens a spotlight's shadow edge was tied to where the shadow
  happened to be stored, so it changed pattern as you looked around or as
  other lights came and went. It is now tied to the shadow itself and stays
  put.
- **Distant scenery keeps its shadows.** With shadow maps on, the shadows of
  machines, crates and other still objects were missing beyond a certain
  distance and appeared as you walked up to them, and each one appearing
  redrew its light's shadows. Lights that do not move now cast them at any
  distance, drawn once and kept. Moving objects and moving lights behave as
  before.
- **Vulkan gives shadow memory back.** The working copies Vulkan keeps for
  point-light shadows were never released, so one view of a great many lights
  (2.0 GB in our test) stayed allocated for the rest of the session. They now
  share the limit set by `r_shadowMapPointCacheMegs` and are released a few
  seconds after they stop being needed.
- **Lights that only cast the map's built-in shadows cast them again.** With
  shadow maps on, these lights (common in the multiplayer maps) had no shadow
  at all and shone through walls.
- **Pillars and containers shadow characters again.** Some static scenery
  stopped shadowing characters and other scenery of its kind under most lights
  when shadow maps were on.
- **Enhanced preset: gibs and burning corpses no longer cast hard shadows**
  from the point lights that preset leaves on the original shadows, as in the
  original game. On Quality and above they still cast.
- **Enhanced preset: shadow edges are no longer nudged to one side** by the
  five-sample filter.
- **Upgrade note.** A profile that still holds the old 12-slot setting
  (`r_shadowMapPointCacheSize`) moves to 64 once; any other value is kept. The
  store is limited to 512 MiB of video memory (`r_shadowMapPointCacheMegs`).
  No preset changed.
- Evidence and what remains open: the
  [9 November shadow audit](shadowmapping-audit-2026-11-09.md). The figures
  are hidden-window probe timings on the reference laptop, not the fullscreen
  preset budgets, which were not re-measured.

## Accented chat no longer freezes the HUD, plus server hardening and less wasted work from a second audit round, 9 November 2026

- **Accented letters no longer freeze the Remastered HUD.** Typing a letter
  such as é in multiplayer chat, or playing under a name that has one, handed
  the HUD a value it refused, and it then stopped taking updates on every
  client that received the line: chat, the kill feed and the scoreboard names
  stalled. Text from the game's one-byte fields is now converted before it
  reaches the HUD or a menu, and the letters draw. Nothing needs to be changed.
- **Servers stand up better to hostile clients.**
  - An 8-byte packet that claimed a full-size message and carried none cost the
    server about 2 GB of memory clearing.
  - Data chosen to grow under the connection's compression could overflow a
    packet buffer and stop the server.
  - A client could add to its player info without limit until the server
    stopped the next time someone joined. It is now held to 128 entries and
    4 KB, and a client that sends more is dropped.
  - Emote spam could overflow other players' message queues and get them
    disconnected. Emotes are now checked and limited to two a second.
  - Messages that followed a disconnect in the same packet were still acted on.
  - Each reconnect, and each map change on a listen server, leaked 64 KB.
- **Clients stand up better to hostile servers, demos and files.**
  - Tourney bracket history, death messages and award messages from a server or
    a demo could index outside their tables.
  - A damaged demo could run the decoder off the end of its dictionary. This
    also happened quietly at the end of ordinary recordings, where the decoder
    read on into the information stored after the demo.
  - A crafted `.pk4` could free memory twice (the zlib 1.1.3 defect that 1.1.4
    fixed) or hang the loader with a stored file claiming more data than it has.
  - In definition files, `$evalfloat` of a very large product overflowed a
    128-byte buffer, a 1,024-character name wrote one byte past its buffer and a
    packed stub's length was trusted.
- **Crash and corruption fixes.**
  - Single player: picking up a weapon modification for the weapon in your
    hands while short of ammunition wrote a clip count outside the clip table.
  - A key pressed in a text field in the same frame its text was cleared, as
    Enter does to a chat line, could move memory by a negative length.
  - A long line made only of colour codes overran the line buffer the stock
    interface draws text from.
  - Icon-style items were drawn with an unset orientation for a frame when the
    player you were spectating left.
  - `g_stopTime 1` made the session read a command out of uninitialised memory.
  - A map or script naming an out-of-range `spawn_entnum`, an impact entity
    that is not a projectile, a boss stomp that finds more than 1,024 entities
    and a sound shader with a huge `shakeData` index no longer write out of
    bounds.
  - On Windows, a failure to unload a game module printed garbage instead of
    the reason.
- **Console.** Printing a line no longer copies the whole console history, which
  is 1.5 MB once full and was copied up to three times per line. Pasting non-ASCII
  text works on Linux and macOS, and typing over a selection in overstrike mode
  no longer brings the deleted text back.
- **Less work per frame.**
  - Stock GUI scripts searched a list of every script block in every loaded GUI
    before each statement they ran.
  - Menus with number fields (the multiplayer Escape menu, Save and SYSTEM)
    copied their whole state on every update to compare two values.
  - Temporal anti-aliasing and motion blur rebuilt their moving-entity lists
    from nothing every frame, on OpenGL and Vulkan.
  - Each animated surface took a statistics lock four times a frame, now twice.
  - Effects measured every particle surface twice per view, and looked
    settings up by name for each particle spawned and each segment culled.
  - Projectiles ask what liquid they are in twice a tick instead of three
    times, the server browser no longer copies each server's record to filter
    it, and Windows no longer looks a setting up by name for every packet sent.
- **Quieter logs.** Single player printed debug lines for any door named
  `func_door_63` or `func_door_64` and any trigger named `trigger_once_47`,
  left over from an old investigation. They are gone.
- **For developers.** A new native test, `openq4-compressor`, runs the
  connection and demo compressors from the shipped source (round trips, a
  source that runs dry, growth bounds, corrupt and trailing data), and
  `network_security.py` pins the new bounds. Four imported multiplayer tests
  now read the engine from their own checkout; from a worktree they read a
  different checkout or skipped. `renderer_image_recovery_loader` compiles
  again.
- Validation: the native suite passes 338 of 338, with the new compressor test
  and the three tests that failed before this round. Hidden, input-free probes
  on OpenGL and Vulkan: single player `game/airdefense1` with a demo recorded
  and replayed; a multiplayer listen server on `mp/q4dm1` with four bots and a
  remote client that chats with accented text, takes an accented name and
  emotes; and the loopback LAN reconnect smoke. The same multiplayer probe on
  the build from before this round logs `Invalid application state` for
  `oq4_chat0_text`, the kill feed and the scoreboard name on server and
  client; this build logs none. The 205 Python contract tests that read the
  changed files pass.

## The Awakening's chaingun turret gets heat, its own HUD and a fair machinegun, 9 November 2026

- **The machinegun hits as it does in Quake 4 again.** In The Awakening the
  player's machinegun hit for 40 a round (more than its zoomed 20) and lost
  its bleeding, flesh sounds and knockback, because the expansion's turret
  file reused the machinegun's damage for the turret. It is back to 15, 20
  zoomed; the turret keeps its 40.
- **The chaingun turret you man in Stranarus Trench and the prison heats up.**
  About five seconds of continuous fire overheats it; it vents steam and will
  not fire for about two seconds. Short bursts never overheat it, a cold gun
  is more accurate than before and spreads as it heats, and the zoom button
  now magnifies its view. Its rounds land under the crosshair (the alpha fired
  them parallel to it, well to the side), and its servos sound as it turns.
- **Its own HUD.** Instead of the on-foot HUD with an empty ammunition gauge,
  the turret shows your health and armor and its barrel heat, also as a bar
  under the crosshair with OVERHEATED while it vents, in the Remastered,
  Minimal and Classic layouts and the classic interface.
- **Upgrade note.** Saves made before this change keep the old turret until
  the level is restarted or reached again; the machinegun's damage applies at
  once.
- Evidence: hidden, input-free M01 and M04 runs (heat cycle, damage
  definitions, a hot-gun save and reload, the scripted prison gunner, HUD
  captures with `tools/ui/capture_remastered_hud.py --scenario chaingun`),
  the native Remastered HUD checks and `tools/tests/awakening_turret_skin_contract.py`.
  A sustained wave defence and audio listening remain; see Q4-04 in the
  [roadmap](roadmap.md).

## Vulkan drops unused shadow volumes like OpenGL (FPS-03), 9 November 2026

- **Vulkan now treats lights without baked shadows as OpenGL does.** This covers
  the weapon flashlight, moving lights and lights the game spawns. When such a light
  renders a shadow map, Vulkan no longer keeps its old-style stencil shadow volumes
  as well, even when the light also reaches objects that are out of view. Shadows
  look the same; nothing needs to be changed.
- **No measurable frame-rate change on the stock maps.** Most map lights have baked
  shadows and are unaffected, so this is a correctness step for FPS-03 rather than a
  speed-up.
- **What to expect if a shadow map fails.** As on OpenGL, such a light can draw
  unshadowed for one frame before it falls back to stencil shadows.
- Evidence: FPS-03 in the [roadmap](roadmap.md);
  `tools/tests/renderer_vulkan_shadow_compatibility.py`.

## Widescreen loading screens on pure servers, without a restart, 9 November 2026

- **No more restart on the first pure join in a widescreen window.** If you
  played or hosted a map in a window that is not 4:3 before joining a server
  with pure checking on (`si_pure 1`), the game restarted itself on the
  connect, which cost about 30 seconds. The cause was the widened loading
  screen openQ4 saved in your settings folder and read back. openQ4 now builds
  that picture in memory and saves nothing, so the game goes straight on to
  the match. Nothing needs to be changed.
- **Pure servers show the wide loading screen too.** Their loading screens
  used to fall back to the map's standard 4:3 picture in a frame. The widened
  picture is made only from game files the server allows, so it now fills the
  window there as everywhere else.
- **Pure checking is as strict as before.** Any loose file read from a game
  folder before connecting still restarts the game into pure mode. Older
  versions left saved loading screens in `guis/assets/generated/loadscreens/`
  in your settings folder; they are no longer used and you can delete them.
- Validation: a pure dedicated `mp/q4dm1` server and a 640x360 client that
  first hosted the same map locally, on OpenGL and Vulkan, with the retained
  and the stock interface. Engine captures of the pure map's loading screen
  show the wide picture; the client wrote nothing to its settings folder and
  reached the match without an engine reload. Details are in the
  [pure-server decision](mp-network-qualification.md#expanded-loading-backgrounds-on-pure-servers-9-november-2026).

## Pure servers also refuse the experimental level-load caches, 9 November 2026

- **Pure checking now covers the fast-loading caches too.** The experimental
  level-load option (`com_levelLoadModernization 1`, off by default) keeps its
  own copies of decoded models, maps, collision data and animations. A client
  on a pure server (`si_pure 1`) used to read them anyway. Now it never does,
  and anything it loaded from them before connecting is loaded again from the
  original game files when the server's map loads. An edited cache file cannot
  be told apart from a genuine one, so it cannot be trusted where pure mode
  applies, the same rule the image cache follows.
- **Nothing to change.** Only the affected models, map data and animations
  are rebuilt; the game does not restart for them. With the option off,
  nothing changes.
- Validation: a pure dedicated `mp/q4dm1` server and a client that first
  loaded the same map from warm caches, on OpenGL and Vulkan. After connecting,
  the client opened no model, map, collision or animation cache file, rebuilt
  91 models, the map and 222 animations instead, and reached the match without
  an engine reload. Details are in the
  [pure-server cache decision](mp-network-qualification.md#level-load-caches-on-pure-servers-9-november-2026).

## Every multiplayer mode gets the Remastered HUD in the classic interface, 9 November 2026

- **Team modes, Tourney and the kill feed join the new layout.** With
  `ui_retained 0`, the layout that Deathmatch and Duel received earlier today now
  covers the rest of multiplayer, keeping the original artwork:
  - Team DM, CTF, One Flag, Arena CTF, Dead Zone, Clan Arena, Freeze Tag and Red
    Rover show compact team panels with your team first and the score against
    the limit. The flags show who carries them, the round modes show who is
    still standing, and Dead Zone names the team holding the zone.
  - The match timer sits under the panels, with the round number and round
    clock in the round modes.
  - Tourney shows its bracket strip on plates, with your match highlighted and
    each finished match's winner ticked. The message bar names the round, and
    the round and arena sit beside the timer.
  - The kill feed sits in the top-right corner, each line on its own plate with
    the weapon icon, team colors and a TEAM tag on team kills. Lines that
    involve you stand out, and a long name no longer pushes the victim off the
    screen.
- **Minimal now hides the multiplayer extras too,** as it does in the modern HUD.
- Choose the Classic layout (`ui_hudLayout 0`) to keep the original positions.
- Evidence: [stock interface HUD layout](ui/legacy-hud-layout.md#multiplayer-groups).

## Joining a pure server no longer restarts the game, 9 November 2026

- **Much faster first join to a pure server.** Joining a server with pure
  checking on (`si_pure 1`) used to restart the whole game the first time in
  each session, even on a stock install. The client had read images from its
  own image cache, and pure mode treated that as possibly modified content.
  Now only those few dozen cached images are rebuilt from the original game
  files. On the test machine, a client reached the match about 7–10 seconds
  after connecting instead of 50–58. Nothing needs to be changed.
- **Pure checking is as strict as before.** On a pure server the client never
  reads its image cache, because an edited cache entry cannot be told apart
  from a genuine one without rebuilding it. A loose file read from a game
  folder before connecting, such as an extracted texture or map, still
  restarts the game into pure mode.
- Validation: a pure dedicated `mp/q4dm1` server and a connecting client on
  OpenGL and Vulkan, with empty and warm caches and with tampered cache files.
  The engine never reloaded, no cache entry was read after the lock, and the
  rebuilt images matched the untampered originals byte for byte. Details are in
  the [pure-server cache decision](mp-network-qualification.md#generated-image-cache-on-pure-servers-9-november-2026).

## The classic interface's HUD gets the Remastered layout, 9 November 2026

- **The modern HUD's layout without the modern UI.** With `ui_retained 0`, the
  HUD now follows the same layout as the modern interface, keeping its original
  artwork:
  - The status bar is centered on the bottom edge.
  - The weapon strip names the weapon above it.
  - Up to two pickup lines sit over the gauges and count repeats ("Armor Shard ×2").
  - Notices no longer overlap the boss bar.
  - Vehicles get a centered status bar and an Exit prompt naming your key in the
    bottom-right corner.
  - Deathmatch and Duel show the new standing rows and timer plate.
  - Chat sits at the bottom left above the status bar, each message on its own plate.
  - On ultrawide screens the edge items stop at 21:9, as in the modern HUD.
- **Your HUD settings now work there too.** HUD Layout, HUD width, safe area,
  pickup messages, the weapon strip and the damage direction indicator all apply.
  Minimal hides the same extras. HUD size and opacity stay modern-UI settings.
  Choose the Classic layout (`ui_hudLayout 0`) to keep the original positions.
- **Older saves still load.** Saves from earlier builds restore with the new
  layout, keeping what was on screen, such as a boss bar.
- Evidence: [stock interface HUD layout](ui/legacy-hud-layout.md);
  `tools/tests/savegame_gui_layout_contract.py`.

## Fixes and smoother first spawns from an audit round, 9 November 2026

- **No hitch on your first multiplayer spawn.** A multiplayer player spawns after
  the level has finished loading, so the first spawn on each map used to parse and
  load the player model, skins, crosshairs, weapons, HUD pieces and damage
  definitions mid-game: 86 late loads on `mp/q4dm1`, many with images. They now
  load with the level, on listen servers, dedicated servers and connecting
  clients alike, and only one small definition is left. Loading a map takes a
  little longer in exchange. Nothing needs to be changed.
- **Crash and corruption fixes.**
  - A mover that crushed a corpse, a loose moveable or a ragdoll could damage it
    after it had already been deleted.
  - Bursts of particle impacts or child effects could overrun a per-frame list
    and corrupt rendering memory.
  - A crafted server or demo stream could make a client write or read outside
    its powerup and player tables.
  - The Tourney announcer read a sound entry after freeing it.
  - Appending a fixed-length buffer to a string read one byte past it, for example
    on 4-byte UTF-8 text from a JSON file.
  - A model or camera definition naming an unknown frame event crashed instead of
    reporting the bad name.
  - On Vulkan with the render thread enabled, bloom and HDR setup used the shared
    text buffer that the game thread uses at the same time.
- **Better crash reports.** A stack overflow on Windows now still writes its crash
  log and dump; before, the report needed more stack than was left and the game
  vanished without one.
- **Smaller fixes.** `func_waitforbutton` now fires (an operator mix-up meant it
  never could), the `jumpnext`/`jumpprev` debug commands step through the points,
  `func_phantom_objects` no longer crashes without a target, and a GUI naming a
  missing font no longer grows the font list on every lookup or reports a font
  failure after every `vid_restart`.
- **Tab walks the settings tabs in reading order again.** On Display, Game
  Options and Controls, Tab from a tab's last setting went back up to the first
  tab of the strip once the tabs began answering the mouse. It now moves on to
  Apply Changes or Back, then to the strip, and never reaches a hidden tab's
  settings.
- **GUI expressions read colour components correctly.** An expression reading
  one component of a window colour, such as `forecolor[3]`, read unrelated
  memory instead, a fault inherited from the original GUI code. No stock GUI
  does this, so only custom GUIs see a difference.
- **Less per-frame overhead.** OpenGL no longer frees and reallocates its
  scaled-rendering bookkeeping every view, upload counters no longer take a lock
  while renderer metrics are off, and the light-grid area memo grows in large
  steps instead of recopying itself every 16 entries.
- **Faster local builds.** Editing game code or committing no longer forces a full
  Meson reconfigure on the next `meson_setup` compile; only adding or removing
  game source files does. The build's source snapshot is refreshed in place
  instead.
- Validation: SP `game/airdefense1` and MP `mp/q4dm1` probes on OpenGL and Vulkan
  with clean logs, a dedicated-server connect, 334 of 337 native tests (the other
  three fail on unrelated in-progress or older work), and the affected Python
  contract tests.


## Rebuilt maps keep their textures where patches meet (CORE-05, #209), 9 November 2026

- **No more smeared strips on rebuilt maps.** Where flat patches meet end to end
  and the texture starts again at the seam, such as the ramp road on `walker`,
  openQ4's `dmap` welded the seam and stretched the texture across a strip of
  triangles. Retail-compiled maps never had this. Recompile your maps to repair
  them; existing `.proc` files keep the streaks.
- **Dense maps compile faster and smaller.** The #209 reporter's 10×10-patch
  `airdefense1` and `core1` come out with 36-37% fewer triangles, and geometry
  takes about a fifth less time than with the previous build on the same machine.
- Evidence: [dmap compilation](dmap-compilation.md#texture-seams-between-consecutive-patches-9-november-2026);
  `tools/tests/dmap_patch_texture_groups.py`.

## Graphics presets rebuilt around frame rates (FPS-02), 9 November 2026

- **Ultra, Quality and Enhanced now hold a frame rate.** On the reference laptop
  (RTX 4060 Laptop GPU, 2560x1440) Enhanced averages at least 144 fps with 1% lows of
  90, Quality 100 and 60, and Ultra 75 and 45, in every scene of the performance suite
  on both OpenGL and Vulkan. Measured: Enhanced 166-298 fps, Quality 120-238, Ultra
  91-188, where the previous Ultra ran 19-58 fps on OpenGL.
- **New Maximum preset.** The previous Ultra, with every effect at its maximum
  (16x MSAA request, 2048 point-light shadows, shadow cascades, motion blur), is now
  **Maximum**. It makes no frame-rate promise and is only chosen by hand.
- **What each tier changed.** Ultra uses 4x MSAA, smaller point-light shadows and no
  cascades or motion blur. Quality uses 2x MSAA and smaller point-light shadows.
  Enhanced replaces MSAA with SMAA and gives point lights the original stencil shadows
  while keeping shadow maps for other lights. Every modern tier uses 16x anisotropic
  filtering. Enhanced's cap rises from 120 to 144 fps.
- **Fresh installs match your display.** The automatic recommendation caps the frame
  rate at your display's refresh rate, so a 240 Hz monitor is no longer held at 120.
  Choosing a preset by name keeps that preset's own cap.
- **Clearer startup report.** The console's `Quality profile:` line marks the
  first-launch placeholder as such and then reports the detected profile with the real
  video memory.
- **Upgrade note: select your preset again.** Saved settings keep their old values
  until you pick a preset. If you liked the old Ultra, choose **Maximum**; choose
  **Ultra**, **Quality** or **Enhanced** to get the new recipes. Setting
  `com_performancePreset` alone does not reapply a preset; use the menu or
  `applyPerformancePreset <name>`.
- Fix: on Vulkan with point-light shadow maps off, the console no longer reports
  false `shadow map pass failed` errors.
- Validation: the preset self-test (eight presets), `ui_performance_presets.py`,
  the SYSTEM catalog and preset tests, the in-game preset smoke on both backends,
  `renderer_vulkan_shadow_compatibility.py`, and the FPS-02 reference run with 42
  redeclared budgets in `docs/dev/performance-budgets.json`
  ([performance report section 11](performance-report.md#11-re-tiered-presets-fps-02)).

## Frame-rate budgets on the reference machine (FPS-01), 8 November 2026

- Developer tooling and evidence, no shipped default changes. The RTX 4060 Laptop
  that measured the performance report is the reference machine. The reference run
  measured the seven timed stock-map scenarios at Ultra, Quality and Enhanced on
  OpenGL and Vulkan (126 launches of `main` 94e0b14ec at 2560x1440).
  `docs/dev/performance-budgets.json` and
  [section 10 of the performance report](performance-report.md#10-declared-budgets-fps-01)
  record 42 warm-mean and p99 budgets.
- `tools/tests/performance_suite.py budgets` declares budgets from reference runs;
  `check --budgets docs/dev/performance-budgets.json --runs <runs>` gates a
  candidate (+5% mean, +10% p99; exit 1 on a regression). It also adds `own-quality`
  and `own-enhanced` settings profiles.
- **Release checklist:** before cutting a release, run the timed scenarios at the
  three tiers on both backends on the reference machine, then `report` and `check`.
  Keep the report's `summary.md`/`aggregate.json` and the `check` table with the
  release notes, and cite them in `docs/dev/releases/vX.Y.Z.md`. A p99-only failure is
  rerun before it counts (report section 10.4). If the release changes a preset on
  purpose, redeclare with `budgets` and commit the new file. The enforced gate
  remains FPS-09.
- Known caveat: the Vulkan convoy1 p99 budgets at Quality and Enhanced include a
  Vulkan presentation stall: slow presents while the frame work stays short, sometimes
  with a swapchain recreation (FPS-07). It is not a game-side hitch.

## Android device qualification (AND-02), 10 November 2026

- [x] Android setup keeps the screen on while it checks or unpacks the game
  data, so the phone no longer locks part-way through a long check.
- Developer: `tools/tests/android_device_qualification.py` plays stock single
  player, sustained play and a bot match on a connected debug phone, without
  touch input. It starts the game through setup's debuggable-only
  `diagnostic_launch` extra and compares the results with the per-model limits
  in `docs/dev/android-device-budgets.json`. The OpenAL log now records the
  output device's rate and source counts. The Galaxy S25 campaign passes cold
  and warm. Its sustain and multiplayer phases, Mali and API-floor devices, and
  audible and controller checks remain open. See the
  [recovery review](platform-recovery-review.md#android-device-qualification-s25-10-november-2026).

Candidate player wording: Android setup keeps the screen awake while it checks
your game files.

## Android touch layout editor, background saves and multiplayer resume (AND-01), 9 November 2026

- [x] A touch layout editor in Android setup moves, resizes (75-150%), fades
  and hides each touch control on an 8 dp grid, with separate landscape and
  portrait layouts, the three presets and resets. It opens from setup's
  Controls page or, during play, from **Settings > Controls > Touch layout**.
  Edits are drafts until **Apply layout**, and survive the app being closed.
- [x] Leaving single player saves it as **Suspended**, so if Android closes
  openQ4 in the background, **Continue** picks up where you left off
  (`com_suspendSave`).
- [x] Returning to a multiplayer match after 40 seconds or more away
  reconnects to the server at once instead of freezing until a time-out;
  after a shorter break the connection is checked and re-established if the
  server no longer answers (`net_resumeReconnect`, `net_resumeProbe`).
- [x] Sound comes back when you return to a multiplayer match on Android;
  before, it stayed silent until you left the match.
- Developer: `verify_android_apk.py --variant quest` and Meson's
  `android_headset` option give the Meta Quest flavor its own package checks
  ahead of AND-05. Checked on the Android emulator and on a Galaxy S25
  (layout, portrait, background save and restore, multiplayer reconnect, the
  editor by hand); details are in the
  [recovery review](platform-recovery-review.md#android-touch-layout-editor-suspension-save-network-resume-and-quest-flavor-checks-9-november-2026).

Candidate player wording: You can now rearrange the Android touch controls:
move, resize, fade or hide each button, separately for landscape and portrait.
Switching away from the game saves your single-player progress first, and a
multiplayer match reconnects by itself when you come back.

## Android portrait play and orientation ownership (AND-01, AND-04), 8 November 2026

- Built-in touch controls gain section 13.6's portrait thumb deck, and the
  Remastered HUD's bottom readouts rise above it while the overlay is active.
- The Interface setup page adds **Screen orientation** (Landscape, or
  Landscape and portrait). `OpenQ4Activity` applies it and overrides SDL's
  `setOrientationBis`, which requested `FULL_USER` for the resizable engine
  window regardless of the manifest's landscape lock.
- Validation: native/JVM host tests with negative controls, and
  `android_emulator_smoke.py --mode rotate` on the API 35 x86_64 AVD (a
  landscape-portrait-landscape turn mid-match with movement, fire and
  save/load). Physical phones remain unqualified. See the
  [recovery review](platform-recovery-review.md#android-portrait-thumb-deck-and-orientation-ownership-8-november-2026).

## Frame-time profiling command and performance report, 8 November 2026

- [x] `profileFrameWork <frames> [file.csv]` takes an optional CSV path below the
  save path, and `profileFrameWork 0` stops a running capture early and writes the
  frames recorded so far, so a frame-time log of a chosen duration can be taken from
  a config script without waiting for the frame count.
- Developer tooling, no shipped default changes: `tools/tests/performance_suite.py`
  measures scripted stock-map scenarios on both renderers with per-frame traces, the
  per-launch GPU load and feature toggle ladders, and
  [`docs/dev/performance-report.md`](performance-report.md) records the frame-rate
  drop since 0.13.2 on the RTX 4060 Laptop: the current Ultra preset's 16-sample scene
  target, shadow-mapped point lights and post chain on OpenGL, the Vulkan backend's
  clamp of that preset to 8 samples, and its equal-settings regression led by the
  soft-particle depth capture. The optimization plan is the roadmap's FPS category.
- Evidence: [performance report](performance-report.md), roadmap FPS-01 to FPS-09.

## Campaign demos (Q4-09), 7 November 2026

- [x] Record a whole single-player session as one demo. `recordDemo` now keeps
  recording through level transitions, save loads, deaths and reloads,
  difficulty restarts and chapter starts. Enter it at the title screen to record
  from the first frame of the next level. Menus, pauses and loading screens are
  left out, and quitting to the menu or the game finishes the file.
- [x] Campaign demos play back level by level: each level shows its loading
  screen, the timeline runs across levels, **Prev/Next Marker** jump between
  level starts, and seeking, speed, pause and stepping cross levels.
- [x] Demos now show the HUD as you saw it, values, icons, crosshair and panels
  included, and in-world computer screens, which earlier render demos left
  blank. Grouped HUD panels play back without their shaped edges.
- [x] Render demos no longer stop playing where the recording started music,
  narration or an interface sound.
- [x] The demo library lists a campaign recording with its first level, length
  and number of levels. It refuses one recorded in the other campaign, saying
  which, and still plays an interrupted recording up to its last frame.
- [x] Demos are about a quarter smaller on levels full of animated characters,
  with no change to what plays back: moving characters' skeletons are stored as
  exact changes from the previous frame instead of in full.
- Compatibility: new demos use render demo version 12. Earlier openQ4 builds
  cannot play them; this build still plays earlier openQ4 render demos,
  including version 11 campaign demos. Expect about 1 MB per second on busy
  levels.
- Evidence and remaining qualification:
  [campaign render demos](multiview-demos.md#campaign-render-demos-v11-7-november-2026),
  roadmap Q4-09.

## Level editor completion (TOOL-01 to TOOL-04), 6 November 2026

- [x] The experimental level editor (`editorExperimental`) now edits brushes
  and patches as well as entities, with lossless `brushDef3`/patch adapters,
  transforms with texture lock, one undo step per gesture, groups, layers,
  prefabs, reference-repairing rename and a bounded background asset index; a
  docked workspace with 3D and top/front/side views, per-map view state, entity
  models, a typed inspector and source-linked validation; and sandboxed
  builds whose outputs replace the playable files only after a whole
  successful compile, with an isolated play client. Two engine defects found on
  the way are fixed: a leak in a map whose folder did not exist beside the
  engine aborted `dmap` (its `.lin` trail used `fopen` there), and the
  dedicated server then hung in shutdown, which builds now detect.
  On Windows, OpenGL and Vulkan, `level_editor_authoring.py` qualifies the
  workspace, every label in 12 languages at three sizes, an editor-authored map
  built and played with AI on its rebuilt navigation, a leaking and a cancelled
  build that leave the previous outputs untouched, and a stock `airdefense1`
  round trip with tactical AI; the native suites pass all 543,372 retail
  primitives, also under Linux GCC and Clang sanitizers. Declared latency
  budgets are met. The build-and-play phase also passes on Linux; macOS is
  covered by its preview evidence gate.
  See [the editor reference](level-editor-modernization.md) and the
  [user guide](../user/level-editor.md).

## Vulkan depth precision and antialiased GUI models, 6 November 2026

- With MSAA on, Vulkan now antialiases 3D models shown inside menus and other
  GUIs, and the pictures on live camera and mirror screens, as OpenGL always
  has. Before, their edges were jagged on Vulkan only.
- On graphics cards that support it (NVIDIA and Intel here), Vulkan now
  handles depth the way OpenGL does. This removes a few single-pixel specks
  at the edges of stencil shadows, and makes a shader mod that moves its
  vertices place them at the same depth as on OpenGL.
- `r_vkWindowViewMultisample 0` and `r_vkDepthClipControl 0` restore the
  previous behaviour for comparison.
- The developer view `r_showShadowCount` now counts shadow volumes inside
  each light's scissor on both renderers, as Doom 3 did, and shows and prints
  the same counts on OpenGL and Vulkan. Before, the result depended on which
  post effects were on.
- Replace the engine and renderer modules together.
- Evidence: [depth control](vulkan-material-programs.md#clip-space-depth-control-6-november-2026-vk-04)
  and [window-view multisampling](vulkan-captures.md#window-view-multisampling-6-november-2026).

## Motion blur, temporal AA in mirrors, and Vulkan screenshots, 6 November 2026

- Motion blur now works while temporal AA is on, including automatic
  upscaling and dynamic resolution. Before, the sub-pixel jitter temporal AA
  adds every frame looked like a camera change, so motion blur silently
  stopped drawing on both renderers.
- Your weapon no longer smears when you turn: it blurs only with its own
  sway, recoil and reload animation. Characters and other skinned models blur
  with their animation as well as their movement.
- The blur length now follows how fast things move, not how many frames are
  drawn, so 144 Hz and uncapped play look like 60 Hz play. Set
  `r_motionBlurShutterMsec 0` to restore the previous behaviour, where blur
  got shorter as the frame rate rose.
- Cinematic camera cuts start a fresh blur instead of smearing across the cut,
  and screenshots keep the blur. Dynamic-resolution scale changes no longer
  interrupt it.
- On Vulkan, temporal AA no longer switches itself off whenever a mirror,
  security-camera screen or refraction effect is in view. Those views had
  copied the main view's jitter, and each one reset the main view's history.
  Mirrors are now also drawn without temporal AA's sub-pixel jitter on both
  renderers.
- On Vulkan, a display that stops accepting frames (switched off, or an
  occluded window) no longer stalls the game for about half a minute per
  frame; each frame now gives up once and tries again on the next.
- Vulkan screenshots now include froxel fog, screen-space reflections and
  screen-space GI exactly as displayed. Before, screenshots drew those effects
  without the scene's depth.
- Authored reflection probes (an experimental, default-off feature) no longer
  pause the game when a new probe becomes resident. The half-second stall
  first became about 85 ms with parallel filtering. Probes are now prepared
  in the background and appear 0.15-0.37 s later, with identical lighting.
  No gameplay frame spends more than about 3 ms on them on either renderer.
  Screenshots wait for them. `r_pbrProbeAsyncPrefilter 0` restores the
  previous behaviour.
- On OpenGL with shadow maps, a mod surface whose see-through holes come from
  a generated or missing alpha texture now falls back to the original stencil
  shadows, as Vulkan already did, instead of casting a solid shadow while
  reporting a mapped light. No stock material is affected.
- The SSAO quality checks for PBR materials work again after half-resolution
  SSAO; a handful of pixels that differ on Intel graphics are now an
  explicitly bounded exception.
- `rendererMotionBlurStatus` shows what motion blur is doing and why it
  skipped a frame. `rendererTemporalPresentationStatus` now also counts how
  every temporal AA frame resolved, with `reset` to clear both.
- Replace the engine, renderer modules and `pak0.pk4` together.
- Evidence: [motion blur](temporal-presentation.md#motion-blur),
  [temporal qualification](temporal-presentation.md#validation),
  [screen-space leaves](renderer-validation-matrix.md#frozen-same-process-leaf-and-rollback-captures-6-november-2026-ren-06),
  [SSAO parity](renderer-validation-matrix.md#ssao-parity-controls-6-november-2026-ren-05) and
  [probe cold misses](vulkan-probe-sources.md#cold-miss-cost-and-budget-6-november-2026-ren-06).

## Map compiler, collision and loading round (CORE), 6 November 2026

- CORE-06: the AAS compiler generates Quake 4's tactical features (cover, lean,
  look-over and corner positions) after optimization, and `runAASTactical`
  regenerates them on existing files. On the shipped geometry of 28 `aas32`
  files it reproduces 7,333 of 7,422 retail features (98.8%) with retail's
  flags and normals. A rebuilt `airdefense1` gives tactical AI the same cover
  choice as retail data. `Write()` now saves features and area spans; the loader
  rejects or drops malformed tactical data. Evidence:
  [AAS compiler](aas-compiler.md#tactical-features).
- Collision: shipped `.cm` files are decoded through Quake 4's shifted contents
  table, so vehicle, fly, shot and item clips load as authored. openQ4's
  compiler writes `CM "4"`; compile custom maps with those clips again.
  Evidence: [collision contents names](dmap-compilation.md#collision-contents-names-6-november-2026).
- CORE-05: dmap and the AAS compiler no longer scale quadratically with
  per-plane curve groups and dense vertex bins; stock maps compile to
  byte-identical output, faster (`airdefense1` AAS 689 s to 56 s).
  The supplied #209 maps compile in about 3 (`core1`) and 4-5
  (`airdefense1`) minutes instead of days, and the rebuilt `airdefense1` loads,
  renders and navigates in SP. Evidence:
  [dmap compilation](dmap-compilation.md#compile-time-on-dense-patches-6-november-2026).
- CORE-02/03: dedicated servers no longer read and discard every level image
  prefetch (media finish 8.5 s to 1.5 s over 24 stock MP loads). The opt-in
  level-load cache was re-measured and stays off.
- CORE-01/04: Windows map-cycle tours with jobs on and off plateau and shut down
  cleanly; fresh configures print no Meson warning; an MSVC AddressSanitizer
  build ran dedicated and SP gameplay, both compilers and the AAS fuzz cases
  with no report.
- CORE-01 closed on clean 0.14.1 release-style Windows x64 and Linux x64
  packages: dedicated and SP map tours, jobs on/off parity, dedicated exits and
  smokes, renderer budgets on both backends and the retail baseline. The Linux
  tours found SP memory rising about 60 MB per level change with jobs on; the
  engine now returns free heap memory after each load. Evidence:
  [final-package qualification](parallel-job-system.md#final-package-qualification-6-november-2026).
  macOS stays with its evidence gate.

## Vulkan screenshots and live camera screens, 6 November 2026

- On Vulkan, a `screenshot` or `envshot` larger than the game window now
  captures the whole picture. Before, it repeated the same window-sized corner
  in every part of the image. Ordinary screenshots are unchanged.
- A `screenshot` larger than the game window is now correct on OpenGL and
  Vulkan with anti-aliasing (SMAA) on, too. Before, most of the picture came
  out black or as repeated copies of one part, and on OpenGL one corner could
  repeat another even with anti-aliasing off. See
  [tiled-capture evidence](vulkan-captures.md#tiled-captures-6-november-2026).
- A new developer drill, `r_vkMaterialDescriptorPoolTest`, makes the graphics
  driver itself refuse shader resources. On NVIDIA and Intel, the affected
  authored shader stages skip without changing the image, and rendering
  recovers fully when the drill ends, after a shader reload and after a video
  restart. All GL/Vulkan comparisons stay within one display level. See
  [tiled-capture evidence](vulkan-captures.md#tiled-captures-6-november-2026) and
  [descriptor evidence](vulkan-material-programs.md#descriptor-refusal-on-real-drivers-6-november-2026).
- Live camera screens (a `remoteRenderMap` material that captures into an
  ordinary texture, as the stock camera monitors do with
  `textures/common/camera1`) showed black or old pixels on both renderers. The
  capture could not write that texture, because it is stored compressed and
  with mipmaps. Like the original game, the capture now replaces the texture
  with a plain image of the camera's view, on OpenGL and Vulkan alike. See
  [nested-view evidence](vulkan-captures.md#nested-cropped-and-offset-views-6-november-2026).
- With the legacy `r_screenFraction` crop (`r_resolutionScaleMode 0`), an
  `envshot` or other capture that is not window-sized now captures the full
  view on both renderers. Before, each face was drawn at half size in one
  corner.
- Still open: larger-than-window captures through the game's post-process
  path are wrong on both renderers, and OpenGL repeats one tile in the direct
  path.

## Smaller downloads and updates, 6 November 2026

- openQ4's own content now comes in three packs: `pak0.pk4` (menus, fonts,
  strings, shaders and the other core files, about 14 MB), `pak1.pk4` (baked
  light grids) and `pak2.pk4` (loadscreens). The light grids and loadscreens
  change only when they are re-baked or recaptured, so most updates leave them
  alone.
- Each Windows and Linux release adds an **update archive**
  (`openq4-<version>-windows-x64-update.zip`, `...-linux-x64-update.tar.xz`)
  holding everything except the light-grid and loadscreen packs. Extract it
  into your openQ4 folder, replacing files. The release notes now say, pack by
  pack, whether anything changed. A local Windows x64 candidate measured
  841.8 MB for the full package and 54.0 MB for its update archive.
- The light-grid pack no longer carries the loose atlas images of maps that
  have a packed light grid. Every renderer reads the packs, including ones
  without DXT texture support, which now decode them on the CPU. Waste
  Processing gains a light-grid pack of its own.
- The Windows installer downloads the light-grid and loadscreen packs only when
  your installation lacks them or holds different ones, from this release's
  page and checked against digests built into the installer.
- The Linux AppImage carries update information for AppImageUpdate and other
  zsync clients, which then download only the parts that changed.
- A dedicated server needs only `pak0.pk4`; it starts without the light grids
  and loadscreens, which clients keep using on pure servers.
- If a pack is missing or does not match, openQ4 now says which one, what it
  holds and where to get it, in your language.
- Windows debugging symbols moved out of the player package into
  `openq4-<version>-windows-<arch>-symbols.zip`.
- Upgrade note: this release is a full download. Remove any `baseoq4` folder
  left by an older openQ4 before extracting it; openQ4 ignores and names any
  leftover `pakN.pk4` it does not use.
- Evidence: [content pack distribution](content-pack-distribution.md#implementation-status-6-november-2026).

## Fewer hitches when music and long lines start, 5 November 2026

- Music, long dialogue and other long sounds no longer stall a frame when they
  start, restart after a device change, or resume after loading a save. Opening
  and decoding these tracks now happens on background worker threads. Before,
  each start could add a few milliseconds to one frame, enough to contribute
  to some of the 20 ms hitches seen at high frame rates.
- These sounds start a frame or two later than before (about 20–30 ms in
  testing), at the same position in the track. Nothing needs to be
  reinstalled or changed. `s_asyncStreamDecode 0` restores the previous
  behaviour for comparison.
- Evidence: [streamed sound starts off the main thread](presentation-owners.md#streamed-sound-starts-off-the-main-thread-5-november-2026).

## Multiplayer menus and an opaque menu backing, 5 November 2026

- The in-match menu's player lists now show who is dead (a skull, the row
  dimmed), which players are bots, and each player's connection quality as
  four bars, as the scoreboard does.
- Kick and Ban on the Admin page ask before acting and name the player. Yes
  removes exactly that player, even if the selection changed meanwhile.
- The Admin page now opens with the chosen player in view on a long list.
- The Match page lists a full 32-player server's roster, every substitute,
  every vote option (including the new side forfeit) and 32 maps, instead of
  sending you to the classic page part-way down.
- French and Italian players now see the "team has too many players" and
  "already on this team" messages in their own language.
- Game Options > HUD gains Opaque Menu Backing: Yes draws a solid dark
  backdrop behind menus and dialogs instead of the blurred view behind them.
- Install the matching engine, multiplayer module and menus together.

## Faster ambient occlusion on Quality, 5 November 2026

- On Vulkan, the Quality preset (and any setting below 32 SSAO samples) now
  computes ambient occlusion at half resolution and upsamples it along depth
  edges. At native 2560 x 1440 this cuts the effect's GPU cost by more than
  half, about 1 ms per frame on an RTX 4060 laptop GPU, with shading that matches
  the full-resolution result to within about one level in 255 on average.
- Ultra keeps full-resolution ambient occlusion. `r_ssaoResolution 1` forces
  full resolution on any preset and `2` forces half; the default `0` chooses
  automatically. OpenGL is unchanged.
- On Vulkan, maps with a skybox world beyond the level (such as the
  Air Defense levels) no longer render that backdrop behind walls and terrain
  that hide it. Ambient occlusion also no longer pauses the scene's depth pass
  to copy it. Together this saves around 1 ms per frame at 1440p Quality, with
  the same image. `r_vkPortalSkyMask 0` turns it off.
- With the experimental Vulkan front-end overlap (`r_vkRenderThread 2`), a
  periodic 15–20 ms stall is gone: its geometry cache now retires old entries
  gradually instead of emptying itself all at once.

## Multiplayer join crash near new players, 5 November 2026

- A multiplayer client could occasionally drop with
  `idCollisionModelManagerLocal::Contents model passed was nullptr` just after
  connecting. It happened when its movement or water check touched a player or
  bot that had just appeared and was not yet drawn. Collision checks now skip
  that placeholder shape, as single player already did. Clients only need the
  updated multiplayer module.
- `tools/tests/clip_empty_model_behavior.py` runs the shared single-player and
  multiplayer filter and fails if either tree drops it or they diverge.

## Team forfeit votes and failed series map loads, 5 November 2026

- Competitive teams can now vote to concede. Any active player can open
  **Forfeit (Side)** from Match Control during live play. Only that team sees
  and votes on it, a majority of its connected players must agree, and a passed
  vote forfeits exactly as the captain's Forfeit does. Referees and the other
  team cannot open one.
- A series map that passes its file check but then fails to load no longer
  leaves the server unable to run managed matches. The next session records it
  as an aborted attempt with no score, and **Advance map** replays it once the
  map is repaired. A server that restarts from such a checkpoint on another map
  is released the same way.
- Spectators can follow bots and late joiners in casual matches again. During
  live play, the server refused anyone who joined after warmup, including every
  bot added mid-match, as a camera target until the next warmup.
- Falling into slime or lava now throws a visible splash. The old effects were too
  small to see against the glowing surface.
- Step-by-step checklists for the remaining hands-on multiplayer checks
  (keyboard/controller workflows, Arena pause, swimming, weapon visuals, real
  networks and bot difficulty) are in
  [mp-human-qualification.md](mp-human-qualification.md).

## Android host integration, 4 November 2026

- SigmaTouch menu controls no longer start gameplay actions behind a menu, and
  releasing a control during a screen transition clears its held state. Custom
  buttons reach their assigned keys, and graphics options have a localized label.
- A touch or Back action can dismiss the host's fatal-error screen. Standalone
  Android keeps its existing storage checks and density-aware error display.
- This reconciles the older Android branch with the current Meson build and
  in-tree game modules. Device and external-host qualification remain open;
  see [integration decisions](android-gles-integration.md#branch-consolidation-4-november-2026).

## Shader allocation failure recovery, 4 November 2026

- A failed Vulkan shader descriptor allocation now returns the uniform memory
  reserved for that draw. Repeated failures no longer consume space needed by
  later rendering; the affected authored stage still skips with a diagnostic.
- Native capacity and recovery controls pass on Windows and Linux, including
  descriptor pools, uniform space and pipeline-cache reuse. NVIDIA and Intel
  also recover from real uniform exhaustion with unchanged images. Pipeline
  pressure exposed a separate reload failure: new shaders could remain missing
  until a video restart. The existing eviction policy now reclaims retired,
  stale programs when their pipelines fill the cache; current and in-flight
  programs remain protected. Both adapters recover exactly in the pipeline
  pressure sequence; all sixteen GL/Vulkan comparisons differ by at most one
  display level, with no validation messages. See
  [evidence and limits](vulkan-material-programs.md#draw-resource-failure-and-capacity-controls-4-november-2026).

## Renderer restart residency and debug views, 4 November 2026

- Video restarts keep textures from old maps unloaded, avoiding a reproduced
  Vulkan image-table overflow after touring several stock maps. Active textures
  and checked graphics-settings recovery retain their reload behavior.
- OpenGL depth and intensity views cover the whole scene at render scales above
  100%; their previous window-sized readback left part of a larger target
  unconverted. Stencil statistics now use the actual attachment dimensions.
- Native regression controls pass on Windows and Linux. All 102 paired GL/Vulkan
  debug captures pass from 50% through 200% scale at 0x/4x/8x MSAA, with exact
  reload/restart recovery. Both NVIDIA and Intel complete the corrected
  16-stop map tour with a peak of 2,372 resident images and no Vulkan validation
  messages. Stock-content warnings and memory growth remain recorded; see
  [resource evidence](vulkan-resource-profiling.md) and [capture evidence](vulkan-captures.md).

## Vulkan foreground qualification tooling, 4 November 2026

- Visible pacing tests now wait for an idle test environment and sustained focus
  on the actual game window before measuring. Foreground events catch brief
  interruptions between polls, preventing a disrupted run from passing.
- An opt-in, cheat-gated engine diagnostic requests focus once through SDL when
  the tester has permission. Normal startup and input behavior are unchanged.
  Native and parser controls pass; presentation results and remaining conditions
  are recorded in [the pacing evidence](vulkan-frame-pacing.md).
- An optional external PresentMon trace can measure displayed-frame pacing on
  drivers without Vulkan present-wait timing. Three foreground measurements per
  local adapter meet the unchanged p99 budget on AC power. The original UTF-16
  parser failure is preserved with its corrected reanalysis; physical display
  routes and other power states remain unqualified. The tool is not bundled
  with the engine, and normal rendering/input behavior is unchanged.

## Smoother retained weapon scopes, 4 November 2026

- The railgun aperture and nailgun hooks/ring now have smooth vector contours
  instead of serrated outlines. The ring's intentional notches remain, along
  with the scope performance improvements below.
- Turning and tracking targets with the railgun and nailgun scopes no longer
  rebuild their moving decorations every frame. The machinegun compass benefits
  too, and the nailgun caches its glow while lock strength changes. Motion, lock
  readouts and ammo indicators retain their behavior.
- Moving decorations use cached triangle edges; stationary aiming marks and
  lens edges retain antialiasing. Continuous-motion regression checks cover
  1080p, 4K and portrait layouts. See [measurements and validation](ui/runtime-performance.md#moving-weapon-scopes-4-november-2026).

## Safer legacy save restoration, 4 November 2026

- Older saves with queued collision events now restore their material references
  before gameplay or an immediate resave. Damaged event strings and trace flags
  return to Load Game instead of reaching callbacks with unsafe data.
- No save migration is needed. The oldest raw trace format uses the material's
  base surface type when it did not record a per-hit type; current saves retain
  their separately saved type. See [scope and evidence](savegame-reliability.md#legacy-event-strings-and-trace-resources).
- Both game libraries pass Windows/Linux regression controls and 60 focused
  Windows checks. Damaged records recover on OpenGL/Vulkan, and a healthy legacy
  trace can be resaved immediately and loaded again. All 11 existing historical
  save fixtures still restore into gameplay.

## Awakening Falcon and prison refinement, 4 November 2026

- The Falcon's two turrets now keep independent heat, weapon and missile
  displays, start without a false missile lock, and retain a usable aiming
  reticle. Ship damage and repair markers agree between both guns and the
  on-foot display, including after saving and loading.
- Missile volleys now match the one-to-five loaded rockets shown in the
  cockpit. Empty firing attempts cannot bypass reloading.
- Ship repairs restore real hull health and clear repaired damage sections.
  Restarting the engine completes and rewards the repair once.
- Prison reinforcements reach the correct cellblock landing, the maintenance
  bot uses its own workstations, and camera startup no longer calls an
  unsupported animation. The missing bridge ambush destination is restored.
  Reinforcements switch to combat before reaching occupied marine cover,
  avoiding a stalled approach during the cell evacuation.
- The prison's constant alarm is now a quieter, localized warning with long
  pauses. Nearby computer alarms also leave room for dialogue and combat cues.
  The security console still stops the warning, and the improvement applies
  to existing saves as well as new games.
- Start M03 and M04 afresh to receive their mission callback corrections.
  Older saves retain their original mission threads. See the
  [review and qualification limits](awakening-missions.md#m03-and-m04-review-4-november-2026).
- The prison security override completes once: repeated use cannot restart
  the alarm or lift the shutters farther. Destroying the turret-room camera
  after the ambush starts no longer starts an early or duplicate door hack.
  Cameras also fit their hanging models, allowing the blocked turret-room
  camera to appear, scan and take damage as intended.

## Awakening rescue and MCC defense, 4 November 2026

- The Pain Lord fight preserves its weak-point stages under heavy damage,
  removes destroyed captives consistently, and gives Strauss the full final
  processing window. Interrupting that attempt buys time to finish the rescue,
  which now has a localized objective that completes with the boss fight.
- The MCC arrival respects Mitchell's death. Its cannon defense has a complete
  opening fighter interval, repeating flight runs, and a reliable transition
  into the escape; later hull damage no longer fails a completed defense.
- Destroyed escape teleporters stop spawning enemies and animating immediately,
  and repeated triggers cannot restart them. Boss victory and cannon failure
  cancel pending encounter actions during normal vehicle/AI callbacks too.
  M05's service-lift lamp now travels with the platform, and its rescue teleport
  waits for the cinematic stand-in to leave the destination.
- Bullets left behind by departing fighters no longer retain deleted collision
  owners, fixing an active-cannon save warning and preventing later entities
  from inheriting an old bullet's collision exemption.
- Idle squad followers give their medic leader room to reach the player. The
  unused upper freight panel no longer offers a broken lift interaction, and
  several missing light, monitor and character/effect texture references use
  their installed counterparts. Fast command-room teleporter kills also prevent
  its delayed activation from running after destruction.
- Restart M05/M06 to use corrected mission scripts and entity assignments.
  Earlier saves keep their
  original script flow. See [intent, changes and validation](awakening-m05-m06.md).

## Damaged queued events recover safely, 4 November 2026

- Saves with unsupported queued callbacks or out-of-order event times now
  return to Load Game instead of silently dropping or delaying actions.
  Interrupted reads of legacy event arguments also release their temporary
  memory. Healthy saves retain their existing format and compatibility readers.
- Both game libraries pass Windows/Linux failure and regression controls; the
  focused Windows suite passes 60 checks. Seven damaged-save cases recover and
  allow a healthy reload on each of OpenGL and Vulkan; all 11 existing historical
  fixtures still restore into gameplay.
  See [scope and evidence](savegame-reliability.md#event-queues-and-interrupted-legacy-arguments).

## Damaged ragdoll saves recover safely, 4 November 2026

- Saves with inconsistent ragdoll owners, missing required definitions or
  mismatched bodies/joints now return to Load Game instead of risking an
  assertion or applying a pose to the wrong part. Legitimate inactive figures
  and the existing save format are preserved.
- Both game libraries pass Windows/Linux regression controls and the focused
  Windows suite passes 58 checks. Seven damaged-save cases recover on each of
  OpenGL and Vulkan, healthy ragdolls survive resave/reload, and all 11 existing
  0.9–0.13 fixtures still load. See [scope and evidence](savegame-reliability.md#articulated-figure-reconstruction-and-saved-structure).

## Awakening opening missions, 4 November 2026

- M01's dropship now builds from engine rumble into coordinated impacts,
  emergency light dips, hull strain, wind and local harness/rope/welding sounds.
  Environmental layers duck under dialogue, and Tak's improved repair lighting
  is preserved. Cinematic shake has an amount/off control; reduced flashing
  keeps stable lighting and non-flashing feedback. Restart M01 for the new
  sequence; older saves retain their original scene revision.
- The complete scene passes OpenGL/Vulkan playback, eight phase save/load and
  eight skip checks. The repair mix was auditioned and confirmed clear; see
  [tested scope and remaining device/performance limits](m01-interior-feedback-audit.md#implemented-revision-3-4-november-2026).
- Tak's face retains natural skin tone and detail while freeing the player
  from the dropship harness in M01. A local cabin fill light covers that repair
  sequence and is removed before gameplay; his model and textures are unchanged.
- Loading a save no longer adds another copy of each burning vent's flame
  effect. Existing duplicate effects are reconciled on load.
- M01's placed Strogg corpse uses the available articulated rig, allowing new
  saves to reload. Saves already written with the missing rig's invalid
  placeholder still require restarting the map. The stair route also stays
  open for squadmates trailing behind the Retch introduction.
- Walker saves retain their stop-animation name safely in both game libraries.
- Navajas now clears the opened courtyard door and reaches M01's final regroup.
  Earlier M01 saves retain their cinematic script; new starts use the lighting
  revision under a separate script identity. M01 saves from the interim,
  unversioned lighting development build may require restarting that map.
- M02's extraction now directs the Darnell spawned for that scene to the
  dropship. Restart M02 for this additional script correction; original and
  version 1 saves retain their previous script identities.
- Tak now waits clear of his squadmates at M02's lifting door, allowing the
  door and lift sequence to proceed. Existing saves stalled at this point
  recover when loaded.
- M02's Nexus console now activates once, preventing firing near it from
  restarting the Tank introduction. Earlier post-introduction saves are guarded
  too. Squad routes, walker defences, Tank victory and extraction into M03 have
  been exercised with checkpointed game-input runs.
- Freezing a fire vent now clears its invisible collision and fully extinguishes
  its light. Players and squadmates can cross active flames while receiving
  their normal damage; quenched vents also restore correctly from saves.
- Trianfac's repair bots use their own stations, and the generator sabotage
  waits for all six bots to be destroyed. The generator objective appears before
  the encounter and completes after the explosion. The Tank victory completes
  the Nexus objective, and the extraction dropship finishes landing before
  starting its idle animation.
- Restart m02 to adopt its script corrections. Earlier saves keep their original
  script program; corrected saves retain a separate source identity.
- [Mission audit and validation](awakening-missions.md) records the designer
  intent, confirmed fixes and remaining Q4-04 qualification. These focused
  encounter checks do not establish complete mission playthroughs.

## Awakening Cryofac and finale corrections, 4 November 2026

- Cryofac's freezer staging waits until the preceding thaw scene finishes,
  preventing overlapping squad commands and attempts to cross a still-locked
  door. Restart M08 to use revision 3; earlier saves keep their script revision.

- The finale's three blast-door controls now work. Closing each door behind you
  earns its intended escape-time bonus, and repeated activation cannot reopen
  it or stack rewards. Restart M09 to use revision 4; existing saves retain
  their selected script revision.
- Cryofac and the finale use surviving installed assets for missing tables,
  arm materials, objective imagery and effects. Unavailable small decorative
  surgical props no longer appear as error cubes.

- The squad clears the thawed entrance more reliably, and Navajas stays on the
  correct floor during the lower-console scene. The finale's cold-breath effects
  now attach to surviving actors without aborting setup on removed characters.
  Restart M08 to adopt its revision 3 corrections, and M09 for revision 4;
  earlier saves retain their original script behavior.
- M09 now completes Navajas's escort objective after the bomb is ready, activates
  the second shaft's intended Iron Maiden encounter, and keeps the cave stairs
  intact until the player reaches the far landing. The docked Valkaryne stays
  on its scripted perches instead of falling into the shaft.
- Melee attacks now apply AI defender modifiers only to AI targets, avoiding
  an invalid state read when monsters strike the player.
- Docked beam attacks commit their aim when the windup begins, giving the
  player time to move out of the blast area on the descent pipes. Standing in
  the aimed area still receives the original damage.

- Cryofac's cooling control accepts a single shutdown request, including an
  early press, and repeated presses cannot turn the cold damage back on.
  Navajas receives his intended movement commands, the flashlight trigger
  works, and Tak's final attack animation finishes before scene cleanup.
- The Valkaryne finale starts its protected sphere phase during the intro.
  Early damage cannot skip that phase, and destroying the sphere releases the
  boss without a later docking request. Repair bots, Navajas's scene handoff
  and the three light-taunt sequences follow their authored progression.
- The extraction gives the thirty seconds promised over the radio. Missing it
  reports mission failure instead of leaving a black screen; boarding after
  departure starts cannot launch a conflicting success sequence.
- Restart m08/m09 to adopt the script corrections. Older saves retain their
  original script program; corrected saves carry a separate versioned source.
- [Design inference, fixes and qualification limits](awakening-missions.md#m08-cryofac-and-m09-valkaryne--4-november-2026)
  distinguish the focused checks from full campaign completion.

## Vehicle health and shield meters, 4 November 2026

- Invulnerable turrets now show a full health meter, and vehicles without shields
  show an empty shield meter. This prevents invalid HUD geometry and warnings
  when saving from these vehicles. Damaged meters retain their normal fractions.
- The Awakening prison turret and trench walker pass occupied save/load/resave/
  reload checks on OpenGL and Vulkan, including firing after a load and respecting
  the walker's repair lock. This does not qualify the complete repair sequence or
  campaign. See [vehicle evidence and limits](retail-baseline-ledger.md#awakening-vehicle-saves-and-hud-fractions-4-november-2026).

## Damaged actor saves recover safely, 4 November 2026

- Loading a save with inconsistent actor animation or IK data now returns to
  Load Game instead of risking a crash or using another actor's animation state.
  A failed load also releases partially restored state callbacks.
- Healthy saves keep their existing format and compatibility readers. Both game
  libraries pass the new Windows/Linux failure tests; the focused Windows suite
  passes 54 checks. Eight damaged-save cases recover and allow a healthy reload
  on OpenGL and Vulkan, and all 11 existing 0.9–0.13 release fixtures still load
  into gameplay. See [scope and evidence](savegame-reliability.md#actor-animation-ownership-and-interrupted-state-calls).

## Multiplayer bot recovery follow-up, 4 November 2026

The latest safety build passes all four long-match and all 33 map-catalog
death/duration checks, with zero navigation falls after strict enemy-impulse
assessment. The targeted recovery and pad-traffic checks also pass. MP-02 is
closed at the project owner's direction with an accepted timing-contention
caveat: the six-bot CTF3 Windows x64/OpenGL run meets its numerical limits,
but separate SP tests overlapped it and no uncontended repeat was completed.
Controlled timing remains unqualified, and the failed report is preserved;
see the [closeout evidence](mp-bots.md#unpressed-ramp-flights-and-consumed-walking-routes-4-november-2026).

- Bots can recover when a ramp carries them into the air after they reach a
  walking waypoint or pass beside a jump pad.
- Bots check their remaining momentum before hopping up an ordinary ledge,
  preventing a pending climb attempt from jumping back off a recovered walkway.
  The reproduced CTF5 fall now survives.
- Low-health bots can adjust their airborne approach to a catch pad when
  a straight coast would end in a fatal landing. The reproduced descent now
  survives and resumes navigation.
- Recovery preserves a safe landing stopped by a wall and settles its final
  braking without drifting back off a ledge. The reproduced CTF5 fall is fixed.
- Bots returning to a route avoid unplanned jump pads, and airborne recovery
  accounts for nearby players that can deflect a safe landing. Reproduced falls
  pass isolated checks.

- Bots keep their place in a jump-pad queue while hopping up its ramp, preventing
  a following bot from launching into them. The reproduced CTF3 collision
  now survives.
- Bots react to small hits that spoil a ledge approach or make a jump lethal,
  and can change a stale movement command before the next physics step after
  a hit. See the
  [hit-to-movement correction](mp-bots.md#hit-to-movement-handoff-4-november-2026).
- Bots avoid close self-blasts during flight and account for downward momentum
  and glancing contacts when checking a recovery landing.
- Recovery continues steering after a ramp throws a bot back into the air,
  instead of treating that brief contact as a safe landing. The reproduced
  CTF5 rocket fall now survives.
- Recovery can preserve a coast into a working jump pad when its complete
  onward flight has a safe landing. The recorded catch now survives.
- Bots can steer a dangerous knockback slide before leaving the floor and
  brake after catching a narrow ledge. Recovery also accounts for movement
  boosts so its commands match the predicted trajectory.
- Recovery distinguishes a narrow ledge where braking can stop the bot from
  an edge it will slide off after touching down. The recorded DM8 rocket
  case now survives.
- Bots avoid the area swept by a lethal moving crusher and check whether their
  own splash weapon would throw them off supported ground before firing.
- Bots stopped beside a narrow walking rim can slowly move onto supported
  ground, resolving the reproduced CTF6 approach stall without restoring the
  old brake oscillation.
- Two additional recorded DM8/CTF5 poses now recover in isolated gameplay.
  Windows/Linux regression controls pass. See [evidence and limits](mp-bots.md#earlier-knockback-control-and-marginal-landings-4-november-2026).

## Multiplayer bot knockback recovery, 3 November 2026

- Bots better preserve a safe landing after a rocket or rail hit, including
  hits that first slide them across the ground. Edge braking also settles
  low-speed movement instead of repeatedly reversing it on a narrow rim.
- Two recorded DM8 failures now survive isolated gameplay replays. The wider
  traversal-safety gate was subsequently completed with the timing caveat
  recorded in the 4 November follow-up above; see the [original qualification and limits](mp-bots.md#knockback-slide-and-replay-corrections-3-november-2026).

## Kick votes keep their selected player, 4 November 2026

- A changing player list can no longer redirect a retained kick choice or a
  delayed kick-vote request to someone who reused the connection slot.
- If the selected player leaves, the Vote page asks you to select again.
  Other drafted changes wait too, so a mixed vote cannot silently lose its kick.
- Install matching engine, multiplayer module and menus. Remote kick votes
  require an updated server; ordinary non-kick votes keep their existing format.

## Retained interface text queries, 4 November 2026

- Interface authors can request localized key-binding labels and measure how
  much plain text fits a retained label. Measurements follow its font, text
  size and available width and preserve complete UTF-8 characters.
- CONTROLS uses the shared binding-name path for its existing capture, clear
  and rebind behavior. Install matching engine and menu content together.
- This is a toolkit increment; full IME, complex-script shaping and the visual
  editor remain in development. See [the text and binding contract](ui/bindings.md#public-key-binding-refresh-4-november-2026).

## Multiplayer menu player targets, 3 November 2026

- Mute and unmute requests and the Admin menu's player actions stay attached
  to the player selected, even if someone else takes that connection slot
  before the request arrives. Kick votes are cancelled if their target leaves
  before execution.
- Install the matching engine, multiplayer module and menu package together.
  On older servers, muting remains local and remote menu player administration
  requires an updated server; direct console administration is unchanged.
- The remaining vote-request and Match Control identity work stays open under
  UI-03. See [the implementation contract](ui/player-identity.md#mute-vote-and-admin-targets-3-november-2026).

## Immediate resaves and interrupted loads, 3 November 2026

- **Saving immediately after loading now produces a loadable save.** This also
  protects a checkpoint queued behind a load. Previously, saving before gameplay
  resumed could leave the new slot unable to load. Already-damaged slots are not
  repaired; load a healthy slot and save again.
- **Interrupted loads clean up more reliably.** Partially read inventory items,
  collision models and collision-cache records are released, and an unfinished
  list of collision models no longer exposes unset pointers during cleanup.
- The save format and existing compatibility readers are unchanged. The new
  failure-path tests pass for both game libraries on Windows and Linux, including
  deliberate regressions. All 50 focused Windows save/restore tests pass, and
  immediate resaves reload into gameplay on OpenGL and Vulkan. See
  [save reliability](savegame-reliability.md#saving-before-the-first-frame-after-a-load).

## Effect diagnostics, 3 November 2026

- Effect diagnostics now report particle activity and collision queries instead
  of constant zeros. A new stock-effect probe passes start, stop, restart, owner
  deletion and save restoration on OpenGL and Vulkan. This improves diagnostics
  and lifecycle evidence; it does not establish complete retail effect parity.
- The historical save matrix also reports v2 save metadata correctly, and all
  11 existing 0.9–0.13 release fixtures restore with their inputs unchanged.
  The engine's save readers have not changed.

## Vulkan shader editing and engine captures, 3 November 2026

- **Long shader-editing sessions keep working.** Vulkan now reclaims obsolete
  compiled sources when their storage fills, avoiding missing material stages
  after repeated edits to large shaders. Programs still used by the current
  frame remain protected.
- **HDR cubemap captures preserve the intended brightness.** In-frame engine
  captures now use the same floating-point scene target as an ordinary HDR
  view, with correct viewport size and main-view recovery. This extends the
  internal linear-HDR laboratory; it does not add HDR monitor output.
- **Failed captures report failure.** An incomplete screenshot write no longer
  reports success. Cubemap capture stops at the failed face, and incomplete
  reflection probes are not reloaded.
- Developer evidence: live shader-edit tests cover both the 256-version and
  32 MiB limits with exact image recovery. The 4x and 8x MSAA capture suites each
  pass 121 GL/Vulkan pairs, and the HDR post/cubemap suite passes 42, all within one
  display level. Native cache/compiler/device/report contracts pass. See
  [authored programs](vulkan-material-programs.md), [captures](vulkan-captures.md)
  and [HDR](vulkan-hdr.md) for scope and retained failing controls.
- Opt-in resource counters and a visible, input-disabled pacing harness now
  collect the evidence needed for later Vulkan optimization and presentation
  qualification. They do not promote another driver/platform or claim a
  performance improvement. Open gates remain in [the roadmap](roadmap.md).
  Current visible diagnostics retained a missed NVIDIA presentation budget,
  unavailable Intel presentation timing and unsuccessful foreground checks;
  these remain failures. The harness now pins one performance preset across
  GPUs and verifies both its saved settings and CPU/GPU evidence identity.

## Local microphone test isolation, 4 November 2026

- **Microphone-test audio stays local when returning to push-to-talk.** Switching
  modes discards buffered test audio and stops local playback, including when
  push-to-talk was already held or the input cannot reopen.
- **Voice capture drops stale fragments after a stall.** An incomplete frame
  expires after 120 ms instead of joining later speech and being sent as fresh
  audio. Capture permission and saved settings are unchanged.
- Physical microphone and hotplug qualification remains open under AUD-05.
  [Implementation and evidence](audio-voice-evidence.md#local-test-isolation-and-partial-capture-age-4-november-2026).

## Audio cancellation and memory pressure, 3 November 2026

- **Discarding a level no longer waits for unused music or speech to finish
  decoding.** Audio workers stop between codec blocks when cancelled; samples
  still needed by the scene resume from the same position.
- **Low-memory cleanup avoids allocating more sound buffers for samples it is
  about to discard.** Active sounds, looping sounds and samples required by a
  level load remain protected. No audio defaults or authored mix changed.
- The audio scene audit now records load times and memory budgets for repeatable
  loading comparisons. Real mobile low-memory notices, physical listening and
  device recovery remain qualification gates in AUD-01 through AUD-07; these
  changes do not close those gates.
- Evidence: [audio cancellation and loading measurements](audio-voice-evidence.md#decode-cancellation-and-loading-measurements-3-november-2026).

## Experimental Vulkan front-end overlap, 4 November 2026

- **Full-overlap mode can reuse unchanged index data across frames.** It keeps
  owned copies alive until drawing finishes and refreshes them when contents
  change. Performance qualification remains required before recommending mode 2.

- **The experimental full-overlap mode avoids repeated copies of shared
  geometry within a view.** Each submitted view keeps its own data, including
  when a later view rebuilds the same source memory. This reduces copy volume;
  a quiet performance comparison is still required before recommending mode 2.
- **Vulkan can prepare the next frame while drawing the previous one.**
  Developers and testers can opt in with `r_vkRenderThread 2`; mode `1` keeps
  the earlier game-tic overlap and mode `0` remains the default. Performance
  qualification is pending, and copying submitted geometry adds CPU and memory
  cost. This is not yet a recommendation to change players' settings.
- Submitted geometry, entities, lights and temporal state retain their own
  storage until completion. Renderer setting changes, resource reloads,
  screenshots and teardown wait for their current readers.
- **Upgrade note:** rebuild and install the engine and renderer modules
  together (renderer API **31**). Saves, network timestamps and the 60 Hz
  simulation remain unchanged.
- Validation covers concurrent frame arenas, geometry/cache ownership,
  shutdown, setting changes and worker errors, plus windowed Vulkan SP,
  MP host/client and OpenGL SP gameplay with GPU skinning, TAA, motion blur,
  scaling, shader reload and Vulkan surface recovery/restart. See
  [overlap evidence](presentation-owners.md#full-front-end-overlap-4-november-2026).
- Quiet mode 0/1/2 comparisons, the native-resolution Quality budget, physical
  display/input qualification and Linux/macOS runtime coverage remain open in
  HF-07/HF-08 and REN-07. These are unreleased source changes.
  The new repeatable [mode comparison and snapshot accounting](presentation-owners.md#mode-comparisons-and-snapshot-reuse-4-november-2026)
  records shared-machine evidence without claiming those qualification gates.

## High-refresh frame ownership, 4 November 2026

- **Blur and other controller-driven effects keep the settings of the frame
  being drawn.** The experimental Vulkan render thread no longer waits merely
  because the next game tic changes an effect setting. OpenGL consumes the same
  command snapshots, including forced-effect settings.
- **Rendering diagnostics and shadow caches keep the submitted frame's
  identity.** Advancing the front end cannot prematurely age Vulkan timing
  queries or invalidate a submitted classic deformation record.
- **Vulkan recording failures return to the main thread safely.** Fatal shadow
  invariants retain their severity after frame retirement instead of starting
  renderer teardown on the worker.
- Validation includes eleven focused native/Meson checks and production-method
  coverage for queued effect changes, submitted-frame query ages and failure
  severity. Windowed Vulkan SP/MP host/client and OpenGL SP gameplay passed,
  including effect toggles, eight classic-domain self-tests per role and engine
  screenshot review. See [submission evidence](presentation-owners.md#submitted-frame-ownership-4-november-2026).
- `r_vkRenderThread` and `in_presentationView` remain opt-in. Overlap performance,
  the Quality every-frame budget and physical display/latency qualification
  remain open under HF-07/HF-08 and REN-07. These are unreleased source changes.

## High-refresh render-thread reliability, 3 November 2026

- **The experimental Vulkan render thread retires every completed frame.**
  Fast frames can no longer skip cleanup, lose a backend error, or apply queued
  world changes in the wrong order. Debug drawing now waits before changing
  storage used by the worker. Map cleanup and renderer restart retire pending
  frames before clearing live world resources. Swapchain and lost-surface
  recovery run on the main thread, including window queries and surface creation.
- **Input-latency diagnostics measure the frame's successful swap.** They no
  longer mistake the asynchronous handoff for presentation. Hidden frames,
  readbacks and failed presents are excluded, and resetting the statistics
  excludes older frames still awaiting retirement.
- **High-refresh qualification reports retain failures.** The reference suite
  returns a failing status for failed or incomplete runs and records whether
  Vulkan used its render thread. Passing automation does not claim physical
  latency or visual qualification.
- **Upgrade note:** build/install the engine and renderer modules together
  (renderer API 30). Saves, network timestamps and the 60 Hz simulation are
  unchanged. `r_vkRenderThread` and `in_presentationView` remain off by default.
- Validation: executable production-function tests cover 1,006 worker handoffs,
  fast/slow completion, backend errors, shutdown/restart, swap-time receipts,
  reset boundaries and acquire/present recovery. Eight reference-suite tests
  cover failure aggregation. All nine focused Meson tests and windowed Vulkan
  SP/MP host/client plus OpenGL SP gameplay checks pass; every role records
  five successful flash receipts and no gameplay during 128 sampled draws.
  See [validation details](presentation-owners.md#threaded-lifetime-validation-3-november-2026).
  The Quality frame-time floor, stage-2 renderer
  overlap, physical latency and operator motion review remain open in HF-07,
  HF-08 and REN-07; this entry makes no performance improvement claim.

## Saves tested between Windows and Linux, 3 November 2026

- **New saves made on Windows restore on Linux, and the other way round.** The
  tests covered seven campaign levels and four levels of The Awakening, in both
  directions. Moving saves between platforms stays unsupported until macOS and
  an ARM64 system have been tested too.
- **A save no longer restarts its level when only a script's line endings or
  comments changed.** A new save now checks the level scripts' code only. Before
  this, a build whose script files had different line endings rejected the
  save's scripts and quietly restarted the level.
- **Fixed two cross-platform restore failures:** menu and HUD animations that
  were in progress, and the new menus' saved state, no longer stop a save
  loading on another platform.
- **Upgrade note:** saves made in the new format by development builds from
  earlier today restart their level when loaded. Earlier saves are unaffected.
- Evidence: `savegame_cross_load.py` (new; its self-test runs in CI) on a
  Windows x64 build and a Linux x64 build of the same commit. Every level
  restored in both directions, and each resave restored again where it began.
  Both platforms rebuilt identical object lists. `savegame_restore_matrix.py`
  still restores every older save it did before, `RetainedUiTest` covers the
  line-ending case, and the 51 meson save, restore and retained-UI tests pass.
  See [savegame reliability](savegame-reliability.md#cross-platform-qualification-q4-06-stage-4).

## Save format ready for other platforms, 3 November 2026

- **New saves use a platform-neutral format.** Saves now record every value
  with a fixed size and byte order, so the same file is valid on every 64-bit
  openQ4 build (Windows, Linux, macOS and Android, on x64 or ARM64). Moving
  saves between platforms is not supported yet: it will be announced once
  saves have been cross-loaded on each platform.
- **Existing saves still load, on the system that made them.** Saves from
  0.9 to the current development builds load as before. A save that another
  platform's build made is now refused with **This save was made on another
  platform and cannot be loaded on this one**, instead of the damaged-save
  message, and the game you are playing keeps running.
- **Older builds cannot load new saves.** Keep a copy of any slot you want to
  take back to an earlier build.
- Evidence (Windows x64): `savegame_restore_matrix.py` restores the 11 release
  fixtures and three stamped 0.12.0 saves, and refuses the three unstamped
  saves as before. It also restores v4 saves from builds 1769 to 1958 through
  the retained readers. Fresh saves round-trip on six stock maps (save, load,
  save, load). Saves restamped for another platform are refused before the
  level unloads. Static checks: `savegame_raw_inventory.py` (2 raw writes per
  game tree, both script storage), `savegame_v3_contract.py`,
  `savegame_corruption_contract.py`, `save_codec.py` and the 50 meson
  save/restore tests. Linux and macOS runtime round trips follow in roadmap
  Q4-06 stage 4. See
  [savegame compatibility](savegame-compatibility-policy.md#version-5).

## Section summaries, 3 November 2026

- Section 13.7 has a section that leads deeper summarize itself in the
  content column, with a levelshot where one fits. The Single Player page's
  CAMPAIGN and ARENA and the Multiplayer page's JOIN GAME, CREATE SERVER,
  DEMOS and PLAYER SETUP now show a sentence of their own there
  (`#str_231552` to `#str_231557`, in all twelve tables) while they are the
  subject, and BACK keeps the page's description.
- A control's feedback reaches only its own subtree, so the plates could not
  light the column. The document format's detail definition now takes an
  optional `card`: without one the runtime only writes the subject's detail
  to its state, and the column binds `summary.section`.
- CAMPAIGN stands over the newest save's levelshot, ARENA over the match the
  Arena page would open on (`idArenaCampaign::SelectedLevelshot`) and CREATE
  SERVER over `si_map`'s. The session publishes them as each page opens,
  reads them as precaches and takes them out of the idle-frame queue. No
  picture shows in a tall view or when text would reach it.
- `UiDocumentTest`, `ui_retained_gate.py`, the session harnesses, the 170 UI
  native tests and the `summaries` OpenGL capture (16:9 and 9:16) pass.
  Opening the Multiplayer page still late-loads Player Setup's preview model
  and crosshairs, as it did before this change.
- Campaign's NEW GAME and CHAPTERS and SETTINGS' CONTROLS, GAME OPTIONS,
  DISPLAY and AUDIO followed (`#str_231571` to `#str_231576`). NEW GAME
  stands over the active campaign's first level and CHAPTERS over the
  furthest it has reached (`CampaignSummaryShots`). The pictures show only
  while the campaign chosen on the page is the active one, since the other's
  levels are not mounted until choosing it reloads the engine. QUAKE 4, THE
  AWAKENING, LOAD DEFAULTS and BACK keep their page's description. The gate,
  the 170 UI native tests and the extended `summaries` capture pass.

## Multiplayer weapon and packet fixes, 3 November 2026

- With optional lag compensation enabled, gauntlet and lightning impacts now
  follow the target's current position. Lightning chain directions follow those
  same positions, and penetrating rail shots retain their original trace line.
  Prediction and lag compensation remain off by default.
- Delayed packets from an older network message no longer discard a newer
  message in progress. Fragments can also arrive in any order within one message,
  reducing failed snapshot delivery on jittery links. Oversized fragments are
  refused consistently over IPv4 and IPv6, without changing the protocol or the
  supported peer combinations. The final build passes three pure joins over each
  loopback family with simulated delay, jitter, loss and duplication.
- Executable production-code tests cover the weapon geometry, fragment ordering
  and competitive artifact writers' short-write/capacity/failure handling.
  See [network qualification](mp-network-qualification.md#weapon-hit-geometry-and-fragment-ordering-3-november-2026)
  and the [competitive audit](competitive-match-reference-audit.md#artifact-adapter-failure-boundaries-3-november-2026)
  for their evidence scope and remaining live/physical checks.

## Network interoperability checks, 3 November 2026

- **Retail Quake 4 peers are told why they cannot play.** An openQ4 client
  now learns a server's protocol before following it into another game
  folder. Connecting to a retail 1.4.2 server shows **Network protocol
  mismatch** with both versions instead of a message about a missing `q4mp`
  mod. A retail client's challenge to an openQ4 server is answered with
  `server uses protocol 2.41` instead of silence.
- **IPv6-only servers on Windows appear in LAN scans.** Windows delivers the
  IPv6 discovery group only to sockets that join it, and the server sockets
  now do.
- **An incomplete `mod.json` falls back to openQ4, not retail-only content**,
  so a server in that state still accepts players without the mod.
- Evidence: `mp_lan_reconnect_smoke.py` (scan, then three successive joins in
  one slot over IPv4 loopback, IPv6 loopback and scoped link-local IPv6),
  `mp_mod_admission_smoke.py` (eight content and module scenarios) and
  `mp_retail_protocol_smoke.py` (the installed retail `Quake4Ded.exe` on
  loopback and a reproduced 1.4.2 challenge), each with a failing control on
  `1e1f3e88a`; `network_security.py`, `network_ipv6_support.py` and
  `filesystem_mod_manifest.py` pin the fixes. See
  [network qualification](mp-network-qualification.md#discovery-reconnects-content-and-retail-refusal-3-november-2026)
  and roadmap MP-08. WAN, cross-machine and other-architecture peers remain
  open.

## Loading screen grid and top-right bracket, 3 November 2026

- The Air Defense Bunker intro art (`e3_load`) has the `+` grid painted in, so
  the stock `intro.gui` draws no grid over it. The retained loading screen
  drew its own vector grid there too, and the two pitches clashed. The
  vector grid now hides on the intro screen, as the brackets and identity
  already did.
- The top-trailing corner bracket is gone from the retained loading screen:
  the level name and its detail line stand over it, and it showed through.
- Checked on real Vulkan loads at 1600x900 (airdefense1 and airdefense2,
  captured at the continue prompt) and with `ui_retained_gate.py`.

## Spectate on Join Game, 3 November 2026

- Section 13.7's list pattern names Join, Spectate and Favorite. The prompt
  bar's raised section has 214 dp before BACK, but the three prompts need
  about 335 dp (English) to 390 dp (German, Spanish). So SPECTATE became a
  plate (`join_spectate`, stock `#str_200195`) between JOIN SERVER and the
  favorite toggle, with its Accept verb in the bar. `joinSpectate` connects
  as JOIN SERVER does with `ui_spectate` Spectate, and JOIN SERVER now sets
  Play.
- A press of the left or right stick runs a document's `onLeftStick` or
  `onRightStick` (`K_JOY13`, `K_JOY14`). Join Game spectates and toggles the
  favorite with them. `stick_glyph` draws each family's LS/RS or L3/R3 8 dp
  past the plates, for a controller only. In tall views SPECTATE stands
  over the favorite toggle.
- `ui_retained_adapter.py`, `ui_retained_gate.py`, the mp-pages native test
  and the `join-stick-glyphs` OpenGL capture pass. Connecting to a real
  server as a spectator was not exercised: no server was running for it.

## Campaign's NEW GAME and CHAPTERS, 3 November 2026

- Visual specification section 13.7 gives Campaign a campaign choice, then
  New Campaign and Chapters as fourth-level children built with the same
  helper. The retained Campaign page now holds QUAKE 4 and THE AWAKENING as a
  choice (the orange rail marks the chosen one, the campaign being played by
  default), then NEW GAME (`#str_200000`) and CHAPTERS, which go deeper
  through `child_transition` (`campaignNew`, `campaignChapters`). NEW GAME
  carries the chosen campaign's label into the title slot, and CHAPTERS its
  own.
- CHAPTERS is a document of its own (`chapters.q4ui`). It has the levels list,
  picture, START CHAPTER, text size and tall view the difficulty page held,
  and Back climbs onto CHAPTERS (`campaignChaptersBack`). A chapter started
  without its autosave takes the current difficulty. The difficulty page
  keeps its difficulty, Turbo and START GAME. Its tall view now takes any
  portrait view that holds it (0.906 at 200 % text), as section 13.7 asks of
  fixed-growth pages. An engine reload into The Awakening continues with
  `campaignSelect awakening chapters`. The classic campaign page is
  unchanged.
- `ui_retained_gate.py`, `campaign_chapters.py`, the native screens tests
  and the `campaignskill` OpenGL capture pass (see retained-screens.md).

## The remaining pages follow the text size, 3 November 2026

- Above 100 % text the pages the list increment left kept their 100 % rows:
  Player Setup, the Mods dialog, the Objectives page, Create Server's options,
  Join Game's other controls, Restart Level, Credits, the Arena page and the
  campaign's difficulty page. Each now meets section 2's contract
  (`LAY-004`): setting rows in a fixed column stack their value under their
  label (`retained_mp_menus.stack_row`) and scroll; descriptions, details,
  status lines and dialog contents grow in the room their frame leaves
  (a picture giving its room where needed, plates moving down or aside);
  Credits' screens grow as far as each one fits; furniture keeps its size.
  Player Setup gained a tall arrangement, and Restart Level and the
  campaign's difficulty take their tall column only where it holds them at
  the running text size (`TallLayout.fits(height, larger)`).
- At 100 % the documents evaluate as before in landscape and tall views
  (bindings diffed at view.text 1 over twelve aspects and every language),
  bar explicit property defaults, path art in fractions of the same box,
  Player Setup's new tall arrangement and sub-0.0001 dp rounding of newly
  fixed furniture in tall views. `fit_narrow` carries a binding on a part's
  `top` over to its centred offset, and the tall layout scales a zero
  length that a text binding moves.
- The native screens test gains `CheckTextScaleCase` for every changed page
  (100/150/200 % on 16:9, 4:3, 0.78 and 9:16; chains in order, parts in
  their containers, controls and texts on screen, furniture at one size,
  restore at 100 %) and a stand-in model renderer for Player Setup's
  preview; all screens groups, the safe-area and settings runs,
  `ui_retained_gate.py` and the register validator pass. OpenGL captures at
  200 %, 16:9 and 9:16: Restart Level, the campaign's difficulty and
  chapters, Credits, the Arena, Mods, the Objectives page, Create Server
  with its advanced dialog, and Player Setup; Join Game at 16:9 (its
  scenario's dedicated server could not take its port in the 9:16 runs).
  The Arena's ledger, which needs a whole match, is qualified by the native
  case only. The captures led to three fixes: the Arena's progress line
  wraps rather than cutting its count, Join Game's status runs on to CLEAR
  SORTING, and Objectives' text stays over the back bar.

## Fallback menu watchdog waits for a second frame, 3 November 2026

- On a fresh profile the first-launch preset restarts the renderer during
  initialization. The first frame afterwards draws the fallback loading screen
  while it restores the retained menus' fonts and eight documents, which took
  1.9 s on an idle machine and 5.8 s under load. The watchdog counted that
  frame toward its 3 s and, past it, opened the stock main menu before a
  startup `+devmap` could run. That loaded 65 decls late: the player models,
  the Player Setup preview's media and the crosshairs. It was intermittent:
  1 of 6 fresh probes on 10404e2c2. Reported by the general-fixes session.
- `idSessionLocal::Draw` now starts the watchdog when the first fallback
  frame ends, in both console-lock branches, so a pending command always gets
  a frame. Four fresh-profile probes after the fix logged 0 late loads, and
  `loading_wipe_capture_crop_contract.py`, `settings_menu_coverage.py` and
  `ui_retained_gate.py` pass.

## General fixes round 2, 3 November 2026

- Push Verification on dcb04ea9b failed on Linux x64, Linux ARM64 and macOS
  Intel. `openq4-ui-json-number` failed on Linux because JsonCpp 1.9.8
  imbues its number stream with the classic locale, so the test's global
  libc++/gdtoa `num_get` emulation no longer reaches the parser, and
  libstdc++'s own `num_get` decodes `1e-400` as zero. The emulation is gone.
  The patched subnormal branch is exercised natively on libc++ (the macOS
  jobs), and the test now states each platform's underflow result. It
  reproduced and passes under GCC 13 in WSL, and passes with MSVC.
- `openq4-ui-retained-screens-menus` timed out at 120 s on the macOS Intel
  runner (49-57 s there before). It ran 18 s alone on this machine, over the
  ~8 s a group should take, so its pause and Strogg pause checks are now the
  `pause` and `strogg-pause` groups (4.9, 3.1 and 2.3 s), with the same
  checks. JsonCpp 1.9.8 was ruled out: it parses `mp_escape.q4ui` as fast as
  1.9.6, with or without `from_chars`.
- The right-stick change (10404e2c2, unpushed) left five extracting
  harnesses unable to compile: `ui_number_clipboard`, `ui_retained_adapter`
  and `ui_text_owner` lacked `Runtime::ScrollDetail` in their shared stand-in,
  and `ui_session_test_gui` and `ui_system_session_route` lacked
  `PumpControllerDetailStick`. They now carry both.
- Chat: `idStr::GetLastColorCode` tested
  `escapeType == S_ESCAPE_COLORINDEX || S_ESCAPE_COLOR`, which is true for
  every escape. The chat window carries a wrapped line's last color code onto
  the next line, so a line whose last escape was an icon (`^i...`) or a
  command repeated it at the start of the next line. It now returns the last
  color code, and nothing after a `^r` reset.
  `tools/tests/idstr_last_color_code.py` runs the production function, and
  two single reversals fail (the old test, and no reset rule).
- `idDict::FindKeyIndex` returned `NULL` (index 0, the first pair) for an
  empty key instead of -1. No current caller passes one (`Set` refuses empty
  keys). A stale `static void GfxInfo_f( void )` declaration of an overload
  that never existed is gone (clang `-Wunused-function`).

## Damaged saves fail cleanly, 3 November 2026

- **Refusing a damaged save no longer risks closing the game.** When a load
  is refused part-way through, the half-loaded level is now cleared without
  running its scripts or vehicle exit sequences, and without the second error
  that used to end the game. Ragdolls, animations, vehicle seats, client-side
  models and several entity lists are now checked as they load instead of
  being trusted.
- **Normal saves load exactly as before.** Every saved-game fixture from 0.9
  to 0.13 still loads, and the whole stock campaign still saves and reloads on
  every level.
- Evidence: `savegame_corruption_contract.py`, `savegame_restore_bindings.py`,
  `savegame_restore_identity.py`, `savegame_restore_matrix.py` (11 release
  fixtures and three 0.12.0 saves), and the 31-stage campaign chain. See
  [savegame reliability](savegame-reliability.md#owner-lists-and-physics-links-during-failed-load-teardown)
  and roadmap Q4-05.

## Right stick in the menus, 3 November 2026

- Section 13.5 gives the right stick the detail area and model previews; the
  retained menus ignored it. The session now pumps the look stick each frame
  (`PumpControllerDetailStick`) to the retained view that takes the menu
  input, past a 16/127 rest zone, timed from the previous frame of the push
  and capped at 100 ms of it, with inverted look not inverting the menus.
- Up and down scroll the detail area at up to 720 dp a second
  (`Runtime::ScrollDetail`): the first shown, reachable scrolling container
  with content past its edge that holds no control. Today that is the
  pause's Objectives page (both families), the summary card's scoreboard,
  the Welcome card's roster and the DISPLAY page's confirmation and discard
  bodies; focus stays and nothing activates.
- Left and right turn Player Setup's preview half a turn a second through the
  session, which owns the Rotation row, so the slider follows the model.
- `openq4_retainedGui stick <x> <y> <msec>` drives both for harnesses without
  a device. `ui_controller_menu_pump.py` (38 checks), `ui_event_delivery.py`,
  `ui_retained_gate.py`, all 168 UI native tests (the focus-reveal test gains
  a detail-area fixture) and OpenGL 16:9 captures (`playersetup-stick`: 200 to
  290 to 110 degrees with the slider following; `pause-objectives-stick`: an
  Objectives page given four test rows scrolled down and back) pass. No
  physical stick was held; per-family device runs remain.

## Multi-view demo markers, keyframes and auto-tracking, 2 November 2026

- MVD recordings are format 1.3. The new records are optional, so builds that
  read 1.2 still play them without the extras. The game schema is unchanged;
  game API 58 adds the keyframe baseline calls and the marker entry point, so
  engine and both game modules must be replaced together.
- Structured markers: phase, round, timeout/pause, team score, kill, flag and
  powerup events, plus `mvdMark <text>` operator notes over the console or rcon.
  `mvdMarkers` lists them, `mvdMarker`/`demoMarker next|prev [kind]` and the
  new Prev/Next Marker buttons jump to them, two seconds early by default
  (`mvd_markerLeadMS`).
- Keyframes every `mvd_keyframeInterval` seconds (default 10) let a rewind or
  a long forward seek restore the nearest keyframe and replay at most ten
  seconds instead of the whole match; replay from the start remains the
  fallback. A keyframe carries map state, network info and the recorder's
  complete delta baseline, so the next snapshot decodes exactly as in
  continuous play.
- User-info changes are recorded, so players who join or rename during a
  recording keep their names and can be followed by name.
- Auto-tracking: `demoAutoTrack killer` follows whoever killed the player being
  watched; `action` follows flag carriers, powerups and recent kills, with a
  dwell so the camera does not flicker; `marine`/`strogg` limits it to a team.
  Choosing a camera by hand ends it. The playback deck has an Auto-Track button.
- Fixed: demos recorded on servers without cheats could not be paused, slowed or
  sped up, and the timeline did not follow the playback position. Playback
  restores the server's `net_allowCheats 0`, and the playback cvars counted as
  cheats.
- Evidence: listen and dedicated recordings on Windows/GL match full replay at
  seven keyframe-seek targets, walk markers, follow a late joiner by name and
  auto-track kills ([MVD 1.3 qualification](multiview-demos.md#mvd-13-qualification-2-november-2026)).
  `mvd_navigation_contract.py` (15 controls) and the extended
  `mvd_recovery_contract.py` cover the parsers, stepping, marker diff,
  auto-tracking and directory recovery. Not run: Vulkan, Linux and macOS
  playback of 1.3 recordings, and runtime phase/score/flag markers.
- Liquids (MP-06): `mp_liquid_air_smoke.py` now passes with the real HUD probe
  command and loopback binding, and a liquid-lab bot audit records no world
  deaths ([evidence](liquid-qualification.md#replicated-air-and-bot-swimming-2-november-2026)).

Candidate release wording: Multi-view demos now mark the moments that matter.
Jump between kills, flag captures, score changes, timeouts and your own notes,
let the camera follow the killer or the action, and rewind long matches without
waiting for a full replay. Players who join mid-match keep their names, and
demos from ordinary servers can be paused and slowed again.

## Android NDK r30 and OpenAL Soft 1.25.2, 2 November 2026

- The Android release workflow (`android-build.yml`) and the local build
  instructions move from NDK r27d (27.3.13750724) to NDK r30 (30.0.16248370),
  and Android's OpenAL Soft from 1.24.3 to 1.25.2, the release the desktop
  packages already use. OpenAL Soft 1.25 needs the newer NDK's libc++: r27d
  lacked the ranges support it uses. AGP 8.7.3, Gradle 8.12, SDK and target
  API 35 and build-tools 35.0.0 are unchanged.
- OpenAL Soft 1.25 bundles Microsoft GSL, with its notice in `gsl/LICENSE`.
  `prepare_android_deps.py` now ships it as `licenses/openal-soft/LICENSE-gsl`
  beside `COPYING`, `LICENSE-pffft` and `LICENSE-fmt`; without that the APK
  would have omitted it. `android_build.py` checks the notice.
- Validation: the steps of `build_android_release.sh` with the Linux NDK r30
  in WSL Ubuntu 24.04 (the CI runner's release), for arm64-v8a and x86_64:
  dependencies, cross file, Meson build and install, strip and
  `android_elf.py` (all seven libraries target API 24 with 16 KiB load
  alignment). Gradle assembled the arm64 release APK, which was aligned,
  signed with a key made for the run and accepted by `verify_android_apk.py`;
  the x86_64 debug APK passed `--variant emulator`. On the AOSP API 35 x86_64
  emulator (SwiftShader), started with cfg files and no input,
  game/airdefense1, game/hangar1 and an auto-joined mp/q4dm1 listen server
  reached gameplay, with OpenAL Soft 1.25.2 mixing 8 to 10 voices through
  Android's OpenSL output. The emulator had no host audio, so nothing was
  heard. All 19 Android host tests pass.
- Single-player view weapons are not drawn on the emulator, while
  multiplayer draws them. A same-source x86_64 build with NDK r27d and
  OpenAL Soft 1.24.3 gives the identical frame and log, so this predates the
  NDK move; it stays an open emulator finding.
- Not checked: a Windows-hosted NDK r30 build (the local recipe in
  `docs/dev/android-build.md`), a physical device and the signed CI release
  run.

## Save durability, restore safety and effect parity, 2 November 2026

- **Saves survive a crash or power cut at the moment of saving.** Each step
  that replaces a save slot now waits until the change is on disk, so a save
  the game reported as complete stays complete, and an interrupted save leaves
  the previous one intact. A typical save takes about 1 ms longer on Windows
  and 7 ms longer on Linux.
- **Quitting or crashing while a save is being written no longer damages the
  previous save.** Before this fix, interrupting the save at that point removed
  the old save's description file, so the slot lost its menu label.
- **A damaged or incompatible save fails cleanly instead of crashing.** Four
  load paths could leave half-read lists or links behind for the game to clean
  up when a load was refused; they now keep only complete data. Normal saves
  load exactly as before.
- **Saves made after a level change load again.** If a radio message had played
  on the previous level, a save made on the next level could be refused with
  "could not restore saved state" (first seen on the level after the second
  Air Defense map). Those saves, including ones already written, now load.
- **Client-side models and debris keep working after a load.** A save holding
  them restored them half-linked, which could stop client-side objects from
  updating and make the next save fail to load.
- **Model particles match the original game.** Effect particles drawn as
  models no longer rotate the model's surface normals, matching Quake 4 1.4.2.
  Lit surfaces rebuild their normals anyway, so only unlit surfaces that read
  them can look different.
- Evidence: `savegame_durability.py` (killed-process recovery on NTFS, Linux
  ext4 and the WSL 9p mount, plus timings up to 512 MiB), `durable_file.py`
  with three new mutations, the checkpoint commit test's staging cases,
  `savegame_corruption_contract.py` and `bse_retail_parity.py`. See
  [savegame reliability](savegame-reliability.md#durability-guarantee) and
  roadmap Q4-03, Q4-05, Q4-07 and Q4-08.

## List pages follow the text size, 2 November 2026

- Above 100 % text the list pages' rows kept their 20 u pitch while the
  runtime enlarged their text, cutting names off and running the selected
  item's details together. `retained_saves.grow_rows` now grows each row,
  its cells, lights and rule by the text size on Load and Save Game (both
  families), Demos, Join Game and Create Server, with an ellipsis for values
  past their column above 100 %; `grow_details` stacks Load's and Demos'
  detail lines over a picture that shrinks to make room; `grow_empty` grows
  the empty states' sentences. Headings, filters, plates, Save Game's name
  field and, on Join Game and Create Server, every other control keep their
  size (`fixed_text`). In tall views the rows keep growing and the details
  keep the tall size.
- At 100 % every property of the seven documents evaluates as before in
  landscape and tall views, and the Demos captures compare pixel-identical.
  The native pages, Strogg pages, mp-pages, mp-card, menus, pointer-reach,
  safe-area and settings groups (with a new `CheckListTextScale` for Load
  Game and Demos at 100-200 %), `ui_retained_gate.py` and OpenGL captures at
  200 % (16:9 and 9:16) pass. Player Setup, the Mods dialog, the Objectives
  page, Create Server's options, Join Game's other controls, Restart Level,
  Credits, Arena and the campaign skill page still keep their 100 % rows.

## Demos as a Multiplayer sub-page, 2 November 2026

- Section 9 of the visual specification makes the demo library a Multiplayer
  section opening a sub-page, and section 13.7 keeps the title's plinth to
  MODS, UPDATES, CREDITS and EXIT. The retained Multiplayer page now holds
  JOIN GAME, CREATE SERVER, DEMOS and PLAYER SETUP (description
  `#str_231550` in all twelve languages); DEMOS goes deeper through the shared
  change of level (`RETAINED_SUBPAGE_DEMOS`) to the Demos page, now built in
  the sub-page frame under the MULTIPLAYER crumb, and Back returns onto DEMOS.
  The title drops its DEMOS link and the session its `demos` home request.
  During playback, or where the sub-page cannot present, DEMOS opens the
  legacy demo browser over the Multiplayer page.
- Player Setup has edited in place on the Multiplayer page since 28 October;
  an engine capture now qualifies it (a typed name, the next model, a rail
  colour and a turned preview, each read back from its CVar, then CLOSE onto
  PLAYER SETUP), and the roadmap and screen record no longer list it as
  outstanding. Its text size and tall-view behaviour remain.
- `ui_retained_gate.py` (route, publication while leaving, Back, paging,
  legacy fallbacks, the session-verb allowlist), the native screens' pages,
  mp-pages, menus, pointer-reach and safe-area groups, and OpenGL 16:9
  captures (`demos`, `demosback`, `multiplayer`, `playersetup`) pass.

## Third-party library updates, 2 November 2026

- Moved to the newest stable upstream releases:
  - SDL 3.4.16 to 3.4.18 (archive hash checked against the release asset's
    published digest). openQ4's five SDL patches apply unchanged: the eight
    files they touch are identical upstream, and the include closure of
    `SDL_events.h` and `SDL_init.h` that the native-input tests whitelist is
    the same. 3.4.18 makes `SDL_SetWindowBordered` fail on video drivers
    without border control (Android, KMSDRM, offscreen, dummy) even when
    nothing would change, which would have failed every strict video restart
    on Android. `SDL3_SetWindowBorderedCompat` now asks SDL only for a real
    change, and `sdl3_strict_window` models such a driver.
  - glslang 15.1.0 to 16.6.0. The overlay's source list is unchanged (16.0
    dropped the SPIR-V remapper, which openQ4 never built), and
    `LICENSES/glslang.txt` follows upstream's shortened licence text.
  - JsonCpp 1.9.6 to 1.9.8. `fast-double.patch` is rebased (upstream removed
    the `sscanf` block above its anchor); `subnormal-numbers.patch` applies
    unchanged. 1.9.8 lowers the default nesting limit from 1000 to 256 and
    rejects raw control characters in strings. The retained document reader
    sets its own limits and rejects those characters before JsonCpp sees
    them, and every tracked JSON file still parses under strict rules.
  - OpenXR SDK 1.1.63 to 1.1.65. The loader's source list is unchanged;
    upstream removed its pre-C++17 filesystem fallback.
  - Vulkan headers and volk from Vulkan SDK 1.4.363.0, Vulkan Memory
    Allocator 3.4.0 to 3.5.0, and the Khronos OpenGL ES headers regenerated
    on 3 August 2026 (additions only).
  - CI: `actions/setup-java` v4 to v6 in the push, commit and macOS
    workflows, matching the Android workflow.
- Already current: RmlUi 6.3, GLEW 2.3.1, stb_vorbis 1.22, libtess2 (upstream
  head), OpenAL Soft 1.25.2 for the Windows source build, the bundled headers
  and macOS (the NuGet fallback's 1.23.1 is NuGet's newest), linuxdeploy
  1-alpha-20251107-1, appimagetool 1.9.1, the SMAA lookup textures and the
  other GitHub actions.
- Left for a project decision:
  - MoltenVK stays at v1.4.1: v1.4.2 needs macOS 12, and openQ4 still targets
    macOS 11.
  - Android keeps AGP 8.7.3, Gradle 8.12, SDK and target API 35 and
    build-tools 35.0.0. The newest are AGP 9.5, Gradle 9.8 and API 37; AGP 9
    is a breaking major release and a higher target API changes app
    behaviour. (NDK r30 and OpenAL Soft 1.25.2 followed the same day; see
    the Android section above.)
  - IJG jpeg-6: replacing id's modified release-6 decoder with libjpeg 9 or
    libjpeg-turbo would change the decoder used for stock textures.
- Validation on Windows: a debugoptimized `builddir` build staged to
  `.install`; all 307 native Meson tests; the push-verification script-smoke
  list (419 commands); the UI register validator after the rebind. The
  staged client, with no input, loaded game/airdefense1 and an auto-joined
  mp/q4dm1 listen server on OpenGL and on Vulkan (RTX 4060). Every run
  reached gameplay with no errors or warnings, and OpenAL Soft was playing
  14 to 15 voices in single player (ambience, gunfire, streamed Ogg dialogue)
  and 1 to 3 in multiplayer. Linux, macOS and Android were not built or run
  here.

## General fixes round, 2 November 2026

- Push Verification on main was red from the menus' safe-area change
  (3ec10bc92) on. Four extracting harnesses compile slices of production
  source that now call `SDL3_RefreshSafeArea` and read
  `engineWindowState.safeArea*`, `Viewport::safe*` and `ui_retainedSafeArea`,
  and `ui_system_number_fields` rendered SYSTEM without the generator's
  `safe_frame` pass. The harnesses now carry those stand-ins and extract the
  new function. `sdl3_strict_window` also checks that phone insets become
  framebuffer pixels rounded outward at a 1.5x2 density and that a failed
  query reports none. `ui_retained_resource_lifecycle` checks window insets
  against the UI viewport's own margin, test insets, and malformed or
  non-finite test values.
- Bots: `idClass::Cast<T>()` returned NULL for a null `this`, and the bot code
  called it on empty entity slots (`gameLocal.entities[i]->Cast<idPlayer>()`).
  A member call through a null pointer is undefined, and GCC and clang may
  fold the check to true (both warned), so optimized Linux and macOS game
  modules could dereference null there. The member is replaced by a null-safe
  `idCast<T>( object )`, used at all 23 call sites; a missed caller would now
  fail to compile instead of crashing.
- `idPolynomial::GetDerivative` and `GetAntiDerivative` fell off the end
  without returning their result (undefined behaviour); BFG's idlib returns it.
- `printEntityDef` now prints the def's key/value pairs. `idDeclEntityDef::Print`
  and `rvDeclPlayerModel::Print` were not `const`, so they never overrode
  `idDecl::Print() const` and the decl manager reached the empty base version.
- `idRenderWorldLocal::DebugBounds` hid Raven's five-argument interface
  method, so game debug draws went to the interface's fallback: it ignored
  `depthTest` (four vehicle debug draws pass it) and drew a garbage box for
  cleared bounds. It now overrides and passes `depthTest` to its lines. The
  beam and sprite models' `IsLoaded() const` likewise hid `IsLoaded()`; they
  now override it, with no behaviour change (`InitEmpty` models are never
  purged).
- CI build logs: clang reported about 50,000 lines of
  `-Winconsistent-missing-override` because three methods in
  `idRenderSystemLocal` were marked `override`; those markers are gone, so
  the class is consistent again. Also gone: about 9,300 `-Wparentheses-equality`
  lines from `idEntityPtr::GetEntity` (and `rvClientEntityPtr`), about 2,600
  GCC `-Wclass-memaccess` lines from `idInterpolate`'s constructor, the
  nameless `typedef enum` in `AASFile.h` and `BSE.h`, a `/*` inside a
  comment in `snd_local.h`, and a discarded comma expression in
  `idMatX::Eigen_Sort*`. `declEntityDef.h` and `DeclPlayerModel.h`, which
  already marked `Parse` `override`, now mark every override.

## Menu pointer reach, 2 November 2026

- The SDL3 backend drives a menu GUI's cursor with deltas between successive
  pointer positions. A GUI that ignored some (its view still loading, a
  retained hand-off, input paused) or clamped them (the pointer outside the
  UI viewport) kept the loss as an offset, so the drawn pointer's reach was a
  rectangle short of the screen until a clamp on the opposite side absorbed
  it. Commit fe6476036 recovered only a change of GUI. Now, once the event
  queue is empty (`Sys_HasQueuedEvents`), a cursor more than a step's
  rounding from the backend's tracking takes the next step from where it is,
  so it lands on the pointer.
- `tools/tests/ui_background_cursor.py` compiles the production routing
  functions and replays ignored steps, edge clamps, several motions per
  delivery, sub-unit motion and the console's own tracking; the same
  scenarios fail with the recovery removed. Not checked by hand in a running
  client: openQ4 tooling never synthesises mouse input.

## Mod manifest version spacing, 2 November 2026

- A mod whose `mod.json` has a stray space or tab around its
  `requiredopenQ4Version` value, such as `"0.13.2 "`, now loads instead of
  being skipped as invalid. A hand-edited manifest in #181 hit this. Other
  malformed values, and mods that need a newer openQ4, are still skipped.
  Checked on the staged client with two test mods: `"0.13.2 "` loaded and
  `"0.14.0"` was still skipped.

## Bot combat strength, 1 November 2026

- Multiplayer bots now aim ahead of a moving target instead of trailing it.
  Their aim used to follow a lag with no anticipation: a skill 5 bot trailed
  anyone running at 320 units a second by 26 units, which is off a 32-unit
  hull, and its turn trailed anything crossing the view by a fifth of a second
  more. Its 2 degree fire cone then held the trigger, so the top skill fired
  rarely and dealt about as much damage as skill 1. A new `aimAnticipation`
  trait (0.50 at skill 1 to 0.97 at skill 5) carries the motion the bot has
  registered forward; that motion catches up with a change of direction in
  about half a reaction time, so juking still beats it. The turn follows the
  aim point instead of being damped against standing still, and the hold-fire
  gate measures the view's slew relative to the target's.
- Skills 4 and 5 hold an automatic weapon's trigger for as long as they are on
  target (450-1200 ms bursts and 60 ms rests at skill 5, previously 140-300 ms
  and 80 ms). Skills 1-3 keep their short bursts.
- The five levels are now evenly spaced. A round robin with every level in each
  match (22 matches, 6,362 kills between levels, four maps) put the committed
  curve's steps at 99, 56, 57 and 60 rating points: skill 2 stood too far
  above skill 1. Skill 2's row now sits 60% of the way from skill 1's, and the
  steps are 83 ± 11, 72 ± 14, 57 ± 9 and 60 ± 10, consistent with even
  spacing. Skill 5 takes 59% of the kills between it and skill 4, and 83% of
  those between it and skill 1.
- `mp_bot_skill_ladder.py --skills 1,2,3,4,5` rates every level from one match
  or several maps at once, with a spread from resampling whole matches, and
  `botlist weapons` adds each bot's per-weapon hits, shots and kills.
- `botlist` now prints each bot's kills, deaths, hits, shots, accuracy and
  damage from the match statistics. `tools/tests/mp_bot_skill_ladder.py` runs
  a free-for-all between two skill levels on the plain skill curve and reports
  cross kills, accuracy and damage per group.
- Measured on q4dm1, three bots a side for 180 s: skill 5 against skill 1 went
  from 216 to 446 damage per bot-minute and from 51% to 68% accuracy, with
  cross kills from 7:3 to 17:5; skill 1 itself stayed near 200. A skill 5
  mirror match went from 169 to 487 damage per bot-minute and from 25 to 60
  kills. Skill 5 beat skill 3 by 20:9 and skill 3 beat skill 1 by 17:5
  (439/362 and 368/250 damage per bot-minute). An offline model of the aim
  puts skill 5 on a steady strafer at 600 units for 56% of frames, against 3%
  before, and on a fast juker for 32%.
  The asset-free bot contracts and both native bot fixture suites pass. Not
  done: qualification against human players. See
  [the aim model](mp-bots.md#the-aim-model) and
  [combat strength](mp-bots.md#combat-strength-1-november-2026).

Candidate release wording: Bots fight much harder at the top skill levels.
They now track and lead moving targets instead of trailing them, so skill 5 is
a genuine threat; skill 1 stays beginner-friendly, and each level in between
is an even step up from the one below.

## Silhouette edges on large surfaces, 1 November 2026

- The renderer's silhouette edge scratch now grows to fit each surface instead
  of stopping at 65,536 edges, and its hash grows with it. The modified `hub2`
  from [#181](https://github.com/themuffinator/openQ4/issues/181) has a pipe
  surface of 78,794 edges. With the previous renderer it loaded with 25,951
  `MAX_SIL_EDGES` warnings and an incomplete edge list. It now loads into
  single-player gameplay and saves with none. No stock map surface, static
  model or animated mesh reaches the old limit, so stock edge lists are
  unchanged.
- The turbo shadow builders move their side-wall buffer to the heap once it
  reaches 256 KiB, since the edge count no longer caps it. Generated world
  and static-model cache parser versions rise to 4 and `0x00010004`, so caches
  holding a cut-off edge list are rebuilt.
- `tools/tests/renderer_silhouette_edges.py` builds edges with the production
  code and the real `idHashIndex`, and compares them with a direct
  implementation of the matching rule on surfaces of up to 120,400 edges. It
  fails on the previous code. Windows Clang, MSVC, Clang sanitizers and Linux
  GCC with sanitizers pass, as do the renderer, cache and workflow contract
  checks. #181 now waits only on the reporter's retest. See
  [dmap compilation](dmap-compilation.md#silhouette-edges-on-large-surfaces-1-november-2026).

Candidate release wording: Very detailed surfaces in custom maps and models now
keep their whole shadow outline; the renderer used to drop every shadow edge
past 65,536 per surface.

## Map compiler slivers, 31 October 2026

- dmap's T-junction repair no longer splits zero-area slivers. Each split of a
  sliver left two halves spanning the same line, so every further vertex on it
  split both again. The modified `hub2` from
  [#181](https://github.com/themuffinator/openQ4/issues/181), whose pipes use
  explicit 10×10 subdivision, compiled to 12,601,692 triangles, 97.6% of them
  slivers. It now compiles in about two minutes to 298,438, and its largest
  surface falls from 36,635,340 to 153,000 indexes. It loads into single-player
  gameplay and saves. Stock `hub2` loses 57 triangles, 55 of them slivers.
- `tools/tests/dmap_tjunction_slivers.py` runs the production splitter on
  slivers and valid thin triangles; it fails on the previous code. Windows
  Clang, MSVC and Clang sanitizer runs pass, as do the dmap and workflow
  contract checks.
- Not resolved then: one surface of that map had more silhouette edges than
  the renderer's 65,536 limit; fixed the next day (above). See
  [dmap compilation](dmap-compilation.md#sliver-multiplication-in-t-junction-repair-31-october-2026).

Candidate release wording: Maps whose curved surfaces use fine explicit
subdivision no longer compile into millions of invisible triangles, which had
made them too large to load.

## Engine and game fixes, 31 October 2026

- Multiplayer map loads no longer print five "Couldn't load AAS file ... AI
  that uses it cannot navigate" warnings. The stock multiplayer maps ship no
  AAS files, and multiplayer bots navigate with the runtime navigation mesh,
  so nothing was lost; developer logs keep one informational line per size.
  Single-player still warns, because its monsters do need those files.
- Loading the Nexus Hub (`game/hub2`) or the Makron fight in `game/core1` no
  longer warns about unknown `recharge` and `teleport` scripts. Those are the
  Makron's own scripts, which it reads itself; retail Quake 4 printed the
  same false warning on every Makron spawn.
- Developer logs for `game/core1` and `game/process2` no longer report
  `idDict::FindKey: empty key`. An inventory item without a loading-screen
  icon was looked up under an empty name; the loading screen still shows each
  icon and item name once, as before.
- String literals are no longer converted to writable `char *` anywhere in
  the C++ sources. That removes about 1,400 `-Wwrite-strings` warnings from
  Linux and macOS builds, and MSVC builds now enforce the strict
  string-literal rules: the `/Zc:strictStrings-` compatibility flag is gone
  (CORE-04).

## Menus at a reduced resolution scale, 31 October 2026

- Menus and the HUD stay sharp and full-screen when **Resolution Scale** is
  below 100% with the legacy `r_resolutionScaleMode 0` (#169). That mode used
  to shrink the whole frame into the bottom-left corner, menu included; now
  only the 3D world is cropped. Multiplayer keeps the world-in-corner view for
  fill-rate testing, and the campaign stretches its scene to fill the screen.
  The default mode 1 was already unaffected.
- On Windows x64 at 25%, OpenGL and Vulkan, the retained and stock title menus
  (also after `vid_restart`), the campaign pause menu and the campaign and
  multiplayer HUDs match their 100% captures. A model drawn inside a menu is no
  longer treated as the world view, which on OpenGL ES could have stretched
  the corner of the screen over the whole menu; that ES change was not tested
  on a device.

## Community report and package triage, 31 October 2026

- Development test packages can save again (#205). Push payloads had come from
  single-commit checkouts, so each identified itself as build 1 and rejected the
  saves it wrote. #203 gives all three payload jobs complete history. The
  Windows x64 payload from push verification at `f0a86b0ec` reports build 1920.
  In SP on `game/airdefense1`, it committed a save stamped build 1920, format 4,
  and loaded that save back into gameplay in a fresh profile. The Linux, macOS
  and Windows ARM64 payloads were not loaded. The replacement package was linked
  from #168, #170, #183, #184 and #192, and the defective 26 October download
  comments now point to it.
- Merged #189 (medic approach watchdog) and #191 (parallel script-smoke checks)
  with #203. Script-smoke fell from 76.8 to 20.0 minutes. Closed #180, #193 and
  #198, whose changes had already landed in `2677fb87b`, and #201, superseded by
  `9fe6f4d89`. Every push-verification job passed at both `b9c663758` and
  `f0a86b0ec`.
- No new release is claimed: 0.13.2 remains the latest available release. The
  [roadmap](roadmap.md#report-and-contribution-traceability) records the
  remaining reporter qualifications for #168, #170, #184 and #192.

## Settings sub-pages, 31 October 2026

- [x] CONTROLS, GAME OPTIONS and AUDIO become sub-pages of the SETTINGS page
  in the main menu's construction, replacing the drafted full-screen panels:
  the SETTINGS crumb and the one-level descent and climb, sections as tabs in
  the Escape card's strip, at most two columns of setting rows
  ([settings sub-pages](ui/retained-screens.md#the-settings-sub-pages-31-october-2026),
  visual specification 1.29, register revision 21 appending `FLOW-068`).
- [x] Native page tests for the three pages and the SETTINGS page, the
  session gate (descent, Back and the stock fallback at the hand-over) and
  the SYSTEM route harness pass; engine captures of every tab at 16:9
  (OpenGL, English), 4:3 (German) and 9:16.
- [x] The opt-in SYSTEM page is the SETTINGS page's DISPLAY sub-page in the
  same construction, generated by `tools/ui/retained_system.py` instead of
  the hand-authored panel and its twelve patch scripts: GRAPHICS, SCREEN and
  IMAGE QUALITY tabs, APPLY CHANGES under the columns, the draft, Keep or
  Revert and unapplied-changes dialogs, presets, renderer and display lists
  and typed numbers unchanged
  ([SYSTEM sub-page](ui/retained-screens.md#the-system-page-as-a-settings-sub-page-31-october-2026),
  visual specification 1.30).
- [x] Portrait views of 3:4 and taller stand each settings tab in one
  scrolling column at about twice the size (the runtime's new `view.aspect`
  source), and GAME OPTIONS' fine sliders take a typed number whose message
  names its own range (`#str_231560` in every language, the number control's
  new `decimals`;
  [portrait and typed numbers](ui/retained-screens.md#portrait-views-and-typed-numbers-31-october-2026)).
- [x] The settings rows follow the text size: above 100 % a tab stands in one
  scrolling column whose rows grow with the text, the strip, prompt bar,
  titles and APPLY CHANGES keep their size, and DISPLAY takes the portrait
  column too
  ([text size](ui/retained-screens.md#settings-rows-follow-the-text-size-31-october-2026),
  visual specification 1.31).
- [x] The menu levels follow the text size: on the title, both pauses,
  SETTINGS, Single Player, Campaign and Multiplayer the plates, titles,
  plinth links and prompt bars keep their size, while descriptions, the
  message line, confirmation bodies and the level block's details grow in the
  room the frame leaves them
  ([menu levels](ui/retained-screens.md#menu-levels-follow-the-text-size-31-october-2026),
  visual specification 1.32).
- [x] The menu levels use a tall view's height: in views of 3:4 and taller
  the title, both pauses, SETTINGS, Single Player, Campaign and Multiplayer
  stand in one column scaled to the room between the bands, with their
  questions at the column's scale
  ([tall views](ui/retained-screens.md#menu-levels-in-tall-views-31-october-2026),
  visual specification 1.33).
- [x] The list pages use a tall view's height: in views of 3:4 and taller
  Load and Save Game, Demos, Create Server and Join Game stand in one column
  with two-line rows, grown plates and dialogs and a full-width backing, and a
  selected row draws one band in every view
  ([list pages](ui/retained-screens.md#list-pages-in-tall-views-31-october-2026),
  visual specification 1.34).
- [x] Restart Level, the campaign's difficulty, Arena pause, the restart
  screen and Credits stand in one column in views of 3:4 and taller
  ([more pages](ui/retained-screens.md#more-pages-in-tall-views-1-november-2026)).
- [x] The Arena page uses a tall view's height: the tier ladder with its
  briefing and progress, then the matches with the difficulty and START
  MATCH under them and the match's levelshot and details beside them, 1.4
  times the landscape size, with its ledger and question grown
  ([Arena page](ui/retained-screens.md#the-arena-page-in-tall-views-1-november-2026),
  visual specification 1.35).
- [x] Menu levels and the fixed-growth pages take their column in every
  portrait view tall enough for it, not only from 3:4, and Join Game's empty
  levelshot frame shows the generic art
  ([every portrait view](ui/retained-screens.md#every-portrait-view-and-other-aspect-ratios-1-november-2026),
  visual specification 1.36).
- [x] Touch in the menus: lists drag with a finger, a touch moved past 8 dp
  is no longer a tap, and a finger leaves no cursor or hover behind
  ([touch](ui/retained-screens.md#touch-in-the-menus-1-november-2026), INP-014
  partial). A press lights at once, a released list keeps going and lights
  its rim at either end, a swipe across a tab strip changes tab and a
  long-press opens Join Game's server details; the real menus now receive the
  finger as it moves. Haptics and device qualification remain.
- [x] The window's safe area: the retained menus keep their text and
  controls inside it while bands and backdrops run full-bleed
  ([safe area](ui/retained-screens.md#the-windows-safe-area-1-november-2026),
  LAY-016 partial); the stock GUIs, swipe edge margins and device
  qualification remain.
- [x] The Remastered HUD's edge readouts keep inside the window's safe area
  too, each edge taking the larger of the player's Safe Area Inset and the
  window's inset, while the root and full-screen effects stay full-bleed and
  desktop layouts are unchanged
  ([HUD safe area](ui/remastered-hud.md#the-windows-safe-area-2-november-2026)).
- [ ] Bound acceptance evidence remains.

## Windows stay see-through at any distance, 29 October 2026

- Windows and open doorways no longer turn opaque once you step back from
  them. On `game/mcc_1`, the medical bay window went solid as soon as you were
  more than 200 units from it.
- The cause was Quake 4's portal distance cull. The map files give some
  windows and doorways a fade distance, and retail stopped drawing whatever
  lay behind a portal past that distance. Retail hid the cut with a
  fade-to-black that openQ4 does not draw, so the room simply vanished
  behind the glass.
- `r_portalsDistanceCull` now defaults to `0`, so every portal stays open. The
  cull only saved fill rate on 2005 hardware. Set it to `1` for the retail
  behaviour; the fade-to-black is still not drawn, so expect the same pop.

## Source layout cleanup, 29 October 2026

- Packages now ship the glslang notice as `licenses/glslang.txt` (previously
  `licenses/glslang-LICENSE.txt`). The other licence files keep their names.
- Source checkouts on Windows and macOS no longer show phantom changes from
  nine files that were tracked under both `OpenQ4` and `openQ4` spellings.
- Developer-only: `src/sys/osx` is now `src/sys/macos` (`macosx_*` files are
  `macos_*`), `src/render_geo` is `src/rendergeo`, `src/MayaImport` is
  `src/mayaimport`, vendored libjpeg and SMAA moved under `src/external/`, and
  all licence notices live in `LICENSES/`. Unreferenced legacy build systems,
  installers and Xcode projects were removed. See
  [Source Layout and Naming](source-layout.md).

## Rendering backend and GPU menu, 28 October 2026

- The retained System page now groups rendering backend/device selection with
  presets and Auto-Detect, shows the active renderer separately from pending
  preferences, and offers a checked main-menu restart after Apply. Named GPUs
  select Vulkan and persist stable IDs. Automatic prefers compatible discrete
  hardware; fresh desktop configurations use Vulkan with OpenGL fallback while
  existing explicit backend choices persist and Android keeps GLES.
  Automatic preserves the implemented OpenGL binding when PC OpenXR is enabled.
- Windowed stock SP gameplay passes on NVIDIA and Intel after cold saved-device
  startup and actual menu-driven module switches in both directions. Missing
  saved hardware falls back to GL with an explicit notice. The menu restart
  supersedes current launch API/GPU overrides only, and waits until all old
  owners have retired before replacing the renderer module. Linux selector and
  desktop/Android adapter ASan/UBSan runs, historical-journal tests, 22 preset
  fault mutations and 12-locale RmlUi layout tests pass. See
  [selection policy and evidence](ui/performance-presets.md#rendering-backend-and-device-selection-28-october-2026)
  for retained logs/captures, failed-run history and unrelated findings.
- Engine/renderer ABI 29 requires replacing those binaries together. This
  policy change does not qualify physical Linux/macOS/Android hardware or
  frame-rate budgets, and does not bypass the complete-preset live Apply gate.
  Vulkan remains experimental on Linux/macOS. The historical OpenGL-default
  entries below describe their earlier source states and are superseded by
  this user-requested selection policy in the current development tree.

## Community report and package triage, 28 October 2026

- Closed #183 after the reporter confirmed the development scope fix on both
  OpenGL and Vulkan. The separate build-1 save failure is tracked in #205, with
  #203 proposed; no new release or verified replacement payload is claimed.
  Corrected the defective 26 October package recommendations on #168/#170/#183.
- Marked #204 as a report against MacSourcePorts' signed build of withdrawn
  0.14.000, with the music symptom still unconfirmed on available 0.13.2. The
  requested log/level/volume evidence and package/save compatibility guidance
  are on the issue. Corrected old 0.14.0 installation advice on #181.
- Reviewed #203's exact head on Windows: its build-history check passes and
  rejects each payload job changed back to a shallow checkout. Evidence:
  `.tmp/github-maintenance-20261028/pr203-validation.json`. Actual packaged
  save/load remains outstanding. Seven PRs remain blocked on CI; #173 retains
  its input-routing change request. #191's ARM64 native run fails only the
  cleanup cases covered by #201, while other jobs exhaust their 90-minute
  limits. The [roadmap](roadmap.md#report-and-contribution-traceability) retains
  the integration and qualification gates.

## Retained model foundation, 28 October 2026

- [x] Add a typed model scene and a native animated renderDef bridge with
  per-part skins, body/head/weapon attachments, bounded target ownership and
  renderer-internal coverage shaders. Preserve the frame's clock/primary world;
  release borrowed references before purges/module unloads. Dedicated links
  remain independent of the retained library.
- [x] Full matched build/stage, seven selected native UI suites and 36 actual
  Vulkan shader compiler cases pass. GL/Vulkan engine captures show the full
  model after SP gameplay; Vulkan survives language/video resets. Raw failed
  fixture views and 12 late asset precache warnings per final run remain in
  the [bounded evidence](ui/document-format.md#model-foundation-evidence-28-october-2026).
- [ ] Player Setup, both production previews, live-animation/native-skin changes,
  native turnover/composition/map/module/failure qualification and remaining
  device/platform gates are outstanding. Renderer-demo output is refused and
  the diagnostic preview still ignores the authored canvas header. No product
  requirement or migration variant is accepted.

## Retained SETTINGS and acceptance review, 28 October 2026

- [x] Recover the SETTINGS hub and Load Defaults dialog from the earlier UI
  worktree into the main checkout. Preserve retained page returns and stock
  fallback; repair SETTINGS from the Strogg pause selecting Load Game.
- [x] Build and stage the current runtime; six native UI suites and the session
  gate pass. OpenGL title/defaults/gameplay, Vulkan Marine/Strogg gameplay and
  German portrait/fallback captures are retained under
  `.tmp/retained-ui-completion/`. See the
  [bounded evidence and failed Vulkan cases](ui/retained-screens.md#main-checkout-verification-28-october-2026).
- [x] Harden the UI-11 transition after source review: require committed record
  bytes, reject logs/nonimages as artwork, recheck earlier binary/log/image
  hashes and reject control characters in evidence paths. All 24 acceptance,
  14 archive and seven inventory fixtures pass.
- [x] Repeat both Vulkan front-end cases in a coordinated runtime window: both
  pass with unchanged engine/renderer/SP/MP hashes. Preserve the four earlier
  unexpected exits as failed evidence; their cause is not established.
- [ ] Complete Player Setup, the SYSTEM/editor round trip, independent tooling review and
  the full UI platform/device/visual gates remain open. No requirement, resource
  or final gate is accepted by this checkpoint.

## Multiplayer roadmap qualification, 28 October 2026

- MP-05: fixed snapshot catch-up being discarded during weapon creation. The
  executable initialization regression rejects the old behavior; exact paused
  Red Rover seeks pass on GL/Vulkan and the inspected capture shows the weapon.
  [Evidence and remaining scope](multiview-demos.md#paused-seek-weapon-catch-up-28-october-2026).
- MP-05: the optional library quota bounds newly admitted recordings without
  pruning complete or partial files. The staged GL smoke passes refusal,
  bounded stop, explicit recovery and playback, preserving its occupancy file;
  [retention and sharing policy](multiview-demos.md#library-retention-and-quota-28-october-2026)
  documents single-recorder admission and explicit archival.
- MP-06: AAS area emission now supplies the liquid flag required by water-jump
  reachability. The production-area fixture and pre-fix control pass. The actual
  dedicated compiler emits 26 swim/16 water-jump links and nine correctly flagged
  water areas in the partitioned lab; [evidence](liquid-qualification.md).
  Liquid-consumer and broader presentation qualification remain open.
- MP-07: the server diagnostic applies the measured lead/RTT prerequisite before
  adding rewind bias. State/wiring checks pass; defaults remain off.
- MP-02 qualification correction (4 November): prior causal replays omitted
  armour and cannot qualify final safety exclusions. Version 2 logs and restores
  post-hit/pre-hit armour and rejects incomplete state. Both recorded DM8 pad
  catches and the CTF5 diagonal-pad recovery pass isolated runtime checks.
  Ground-slide prediction now restores friction on the physics timer's expiry
  frame, and the recorded rocket-slide recovery passes. Bots also check whether
  their momentum reaches a lower floor than their route's landing can afford.
  Both low-health drop poses pass isolated checks. Causal audits also preserve
  actual pre/post knockback timers, including refused resets; renewed final-build
  long/catalog/timing gates remain open; see [bot evidence](mp-bots.md#exact-armour-and-timer-qualification-4-november-2026).
- MP-02: bounded percentile profiling and explicit audit budgets are implemented.
  The five-minute DM8 audit retains a navigation death, a stuck event and a
  contested timing failure; it does not close the safety or performance gate.
  A descending unpressed WALK fall now uses the existing safe-landing recovery.
  Both diagonal DM8 probes that died before the fix land alive; four direct
  approach controls still survive. Native boundaries and a knock-only reversal
  control pass. The original match input sequence is not established, and the
  wider gate stays open ([details](mp-bots.md#unpressed-walk-falls-and-timing-evidence-28-october-2026)).
- A supporting staging fix excludes static-library object directories and linker
  artifacts from `builddir/baseoq4/`. Copying those build-only files could fail
  on a compiler lock. The packaging safety regression passes. This was discovered
  during the MP rebuild; compiler outputs remain in their original build paths.
- The imported regression run initially passed 73/74; its remaining teardown
  fixture lacked a stub for the checkout's save dictionary-cache cleanup. The
  corrected fixture and complete rerun pass 74/74
  (`.tmp/mp-completion-20261028/contracts-final/`). New MVD quota, seek-weapon,
  AAS-area and bot-profile checks pass separately. Affected final-runtime
  reruns remain part of this work.

## Graphics presets and airdefense performance, 27 October 2026

- **28 October holistic preset audit:** Enhanced bridges Balanced and Quality;
  lower tiers reset dormant effect costs and disable unused HDR scene targets.
  All twelve localized choice lists and both menu routes expose seven tiers.
  Active-device auto-detection replaces host-wide VRAM and the retired card-cap
  stub, preserves unknown observations, handles UMA without an ARM penalty and
  limits recommendations by drawable size and CPU concurrency. Auto never
  selects the unmeasured Quality/Ultra tiers. Renderer ABI 28 carries the active
  GPU facts; the settings effect footprint and journal schema are unchanged.
- **First-launch follow-up:** new installations bootstrap conservatively and
  apply the canonical active-device profile before entering gameplay, replacing
  the frequency-based initial classifier. Saved configs with a missing spec
  marker remain intact. Named profiles and launch overrides take precedence;
  Android has a Performance ceiling and verified <=512 MiB discrete GPUs stay
  at Minimum even when platform power hints are present. Seven tiers provide
  the required coverage; Quality/Ultra remain manual choices.
- **Classic-renderer and saved-config correction:** Apple GL 2.1's missing
  modern post shaders no longer force Minimum on capable hardware. Classic
  shader/texture capability and modern-effect eligibility are assessed
  separately, while shared-memory Balanced keeps its resource/resolution limits.
  SDL3 RAM-query failures stay unknown. Startup repairs an unset legacy machine
  classification without executing the old recipes over saved AA, scale,
  textures or VSync. New evidence is in `.tmp/preset-platform-followup/`.
  Its `audit.json` records the reproduced startup failure, passing all-seven
  GL/Vulkan gameplay runs, native Intel detection, three Meson suites and 18
  rejected mutations each on Windows and Linux ASan/UBSan. Cleanup remains
  blocked by automatic approval review; physical platform/FPS gates stay open.
- **Drawable and legacy-command follow-up:** automatic scene scale now respects
  conservative pixel budgets, including 50% at 4K on shared-memory graphics;
  unsupported post AA is left off. The resolved recommendation is shared by
  first launch, console and SYSTEM draft expansion. Stock quality choices use
  canonical recipes, and Recommended reapplies even when the old classification
  is unchanged, preserving display mode and VSync. Manual profiles and the
  seven-tier/55-field footprint are unchanged. Evidence is recorded under
  `.tmp/preset-resolution-audit/`; physical platform/FPS gates remain separate.
  Final Windows/Linux sanitizer suites reject 22 mutations each; three Meson
  suites and GL/Vulkan/Intel gameplay probes pass. Intel's 2400x1280 window uses
  a verified 1800x960 scene. A Windows-constrained 4K request is retained as
  nonqualifying evidence. Shared-stage Multiplayer/air-HUD document failures
  and a vector coverage warning are tracked separately in the audit.
- Follow-up validation: `.tmp/preset-review/audit.json` records passing Windows
  build/staging, six Meson preset/UI/journal suites, Windows and Linux sanitizer
  policy suites with 15 rejected mutations each, all seven presets in windowed
  OpenGL/Vulkan SP gameplay, and native Intel Vulkan detection. Fresh autoexec
  overrides (including equal-to-bootstrap values) and an existing config with
  a missing detection marker pass live gameplay checks. Logs and accepted engine
  captures remain; obsolete test binaries/caches and the old wiring helper are
  removed under standing cleanup permission. Physical platform/FPS limits remain.
- Preset audit validation passes: Windows build and policy/mutation suites;
  Linux ASan/UBSan policy/mutation suites; seven-choice, twelve-locale UI layouts;
  all seven profiles with device restarts in windowed SP gameplay on NVIDIA
  OpenGL and Vulkan; native Intel Vulkan auto-detection. Engine captures and
  immutable runtime identities are under `.tmp/preset-audit/`. This establishes
  no fullscreen frame-time gate or physical qualification on other platforms.

- **28 October scope update:** subsequent airdefense and checkpoint measurements
  use Quality, the preset immediately below Ultra, at native 2560 x 1440. The
  benchmark disables Quality's 144 FPS cap while preserving its graphics values.
  The strict 150 FPS floor and checkpoint-stall removal remain open. Ultra still
  selects the highest supported quality; earlier Ultra evidence remains historical.
- Ultra now requests maximum stable AA, shadow and post-processing quality;
  Quality uses intermediate settings. Reapplying a preset adopts the new values.
  The retained SYSTEM catalog/draft and versioned recovery definitions cover
  every added setting. Original V1/V2 effect catalogs remain immutable.
- Pure profiles/Common/host/rollback checks, the SYSTEM Runtime/RmlUi preset
  matrix and journal negative controls pass. The live engine preset self-test
  also passes. No new UI Apply capability is claimed by this revision.
- Renderer/UI optimizations cache BSE particle sort distances, preserve equal-
  depth ordering, and avoid repeated retained HUD frame reads and opaque raster
  vertex copies. Default fullscreen minimization remains unchanged; an explicit
  SDL opt-out is now respected by the platform fallback too.
- HF-07/REN-07/UI-07 remain open. The first complete native 1440p fullscreen
  rotation with all Ultra effects and four cascades averages 38.5 FPS on Vulkan
  (37.55 ms maximum), and 30.9 FPS on OpenGL (69.85 ms maximum);
  it fails the required 150 FPS on every frame. See the
  [measured checkpoint](ui/runtime-performance.md#airdefense-maximum-quality-reference-27-october-2026).
  Subsequent profiling found enabled motion blur was not drawing because portal
  skies reset main-camera history. Both backends now preserve it across subviews;
  the benchmark requires actual post-effect draw evidence as well as CVar values.
- The final preset mutation/adjacent suite passes with unchanged production
  inputs (`.tmp/performance-presets-wqnrsn7b/result.json`); versioned settings
  journal controls pass (`.tmp/settings-effect-journal-jot5612u/result.json`).
  The preset runner now returns failure if its input-integrity audit fails;
  previously that condition marked the JSON failed while the process exited 0.
- Vulkan post-processing avoids unused depth transitions and redundant MSAA
  capture blits. Paired native rotations save about 0.1–0.2 ms and 0.8–0.9 ms
  respectively, with identical frozen RGB captures. Render-target/HDR validation
  passes, including new fractional-coverage and cropped-capture GPU checks.
  An unrelated restart crash exposed by those checks is fixed: Vulkan now clears
  backend pointers to retired frame memory during device teardown. Temporal
  fixtures and probe-source lifecycle checks pass across restart with validation.
- Fresh opaque projected shadow maps share global-caster depth across receiver
  ownerships. Five paired native rotations save 0.38 ms on average; Vulkan
  validation and 100 bounded atlas/tile resize cycles pass. The latest strict
  qualification averages 43.4 FPS and peaks at 38.36 ms, so the 150 FPS goal
  remains unmet and the preceding qualification's worst-frame result is not improved.

- Fresh PCSS receivers now sample the exact minimum of separate ownership depths,
  avoiding the shared depth-block copy. Five paired native rotations save 2.53 ms
  on average and improve p99 in every pair. Four view pairs are RGB-identical;
  validation and 100 bounded atlas/tile resize cycles pass. Shader-header
  consistency and shadow contracts pass. The staged default-on qualification
  reaches 50.2 FPS mean and 26.35 ms p99, but its worst frame is 38.72 ms; the
  every-frame 150 FPS goal remains unmet.

- The optional GPU phase trace now distinguishes shadow-map production from
  receiver shading without adding waits. With explicit default cache settings,
  1,024 maximum-quality frames average 6.715 ms generating maps and 1.824 ms in
  the remaining lighting work. This is diagnostic evidence, not qualification.
  A repeated zero-light sampling experiment showed no gain and was removed.
  The scratch profiler now resets cache settings left by resize probes; fresh
  strict qualification profiles already used the correct defaults.

- Shadow passes now omit unused stencil attachments while preserving depth
  format and quality. Five matched rotations save 1.220 ms (6.16%) on average;
  four p99 results improve and one regresses by 0.10 ms. Three settled views
  match exactly, and one differs at 35 pixels by at most one intensity level.
  Vulkan validation, 100 bounded atlas/point-size cycles and video restart pass.
  The staged strict qualification records 52.55 FPS mean, 25.05 ms p99 and
  29.82 ms maximum: workload gates pass, every performance gate still fails.
  Point-cube ownership copying was slower in all five pairs and was removed.

- Expanded optional GPU attribution identifies 15.008 ms in the main view,
  2.269 ms in portal-sky rendering and only 0.243 ms in SMAA. Validation passes
  all 1,024 phase masks and interval relationships without Vulkan warnings or
  errors. Precomputed SSAO spiral offsets showed no useful gain in five matched
  pairs and were removed. These are diagnostic results; the strict qualification
  above remains the latest acceptance result.
  A subsequent conservative portal-discard prototype also passes four exact
  image comparisons and Vulkan validation, but its 0.21% mean difference over
  five pairs is below repeat variation; its larger worst-frame spike and absent
  useful gain do not justify retaining it. The experiment was removed.
  The re-staged build's latest native qualification records 53.35 FPS mean,
  26.40 ms p99 and 32.79 ms maximum, with workload gates passing and every
  performance gate failing. The result does not close the strict 150 FPS goal.
  Finer optional GPU timers attribute 4.400 ms to projected-map generation and
  0.914 ms to point cubes; 1,024 nested timing samples pass Vulkan validation.
  A bounded local-caster clear prototype was 0.204% slower across five pairs
  despite frozen-view/control parity, so all experimental rendering code was
  removed. The latest strict qualification remains unchanged.
  After rebuilding the shared dictionary layout for a disabled-by-default save
  prototype, the strict native fullscreen rerun records 53.01 FPS mean,
  24.90 ms p99 and 31.81 ms maximum. Workload checks pass and all performance
  gates fail; this is the latest default-build measurement, not a claimed gain.

- Optional projected-shadow pipeline counters identify 143.5 million fragment
  invocations per frame at maximum quality. Scoped captures and enabled/disabled
  video-restart checks pass Vulkan validation. An opaque-caster specialization
  preserves the tested view/control comparisons but regresses mean frame time
  by 0.606% across three pairs and is removed. Only default-off diagnostics are
  retained; this does not change the strict qualification or save-stall status.

## Checkpoint saving, 27 October 2026

Checkpoint optimization evidence for the current graphics work is recorded in
[runtime performance](ui/runtime-performance.md#checkpoint-save-stalls-27-october-2026).
Optional dictionary attribution (`com_saveGameProfile 3`) measures 2.82–2.99 ms
per warm Vulkan snapshot across roughly 3.94 MB of dictionaries. Native SP/MP
tests prove unchanged bytes and no diagnostic clock calls when disabled. The
windowed maximum-quality probe passes load/quit/reopen, damaged-save rejection
and obstructed-payload preservation. This identifies further work; the reported
timings include profiling overhead and do not close the stall-removal gate.
Two dictionary-cache prototypes that scanned content were slower and removed.
The replacement uses mutation invalidation, passes 4,614 live byte comparisons
and native mutation/lifetime/wire tests, and reduces mean warm serialization
from 14.80 to 12.85 ms over normal/reversed alternating probes. That immediate
population remained disabled: ten fresh-session saves showed a 16.94% mean
first-save regression, requiring population to leave the save frame. The candidate's largest
checkpoint window is 46.88 ms, so there is no worst-frame or stall-removal claim.
The shared dictionary layout requires matching game ABI 57 and renderer ABI 27.
That immediate-population prototype is now replaced by bounded population on
later game frames. Deferred reuse passes 4,774 live byte comparisons and ten
fresh-session save/reload checks without a measured first-save penalty. Normal
and reversed warm pairs improve combined means from 13.56 to 11.47 ms, retaining
every sample. Population batches peak at 0.566 ms in the oracle. The deferred
path is enabled by default; the staged Vulkan default passes lifecycle,
corrupt-save rejection and blocked-preview preservation. Native source-lifetime,
deadline/byte bounds and SP/MP wire tests pass. These gains do not close the
checkpoint stall or strict fullscreen frame-time gates.
The latest maximum-quality default qualification is 53.68 FPS mean with an
80.06 ms maximum frame; native extent, effects and HUD checks pass. That outlier
is retained without an established cause. This rotation contains no saves and
does not measure checkpoint improvements. An opt-in
worker-side snapshot-copy experiment passes native ownership/failure tests and
live lifecycle/corruption checks, but normal/reversed pairs regress total
checkpoint work (26.04 to 28.86 ms) despite cheaper handoff (1.94 to 0.19 ms).
It remains disabled by default. Transaction setup/commit and live snapshot
serialization remain unresolved; see the runtime evidence for all outliers.
Per-save legacy GUI reuse remains experimental and disabled: 430 GL/VK fresh-byte
comparisons and native SP/MP wire/failure tests pass, and ten paired comparisons
reduce serialization 13.66→9.92 ms, but total checkpoint time and window p99
regress. Detailed commit profiling attributes 1.31–4.96 ms to file operations;
transaction completion still needs work before promotion. A retained Motion
snapshot candidate was removed after finding the workload serialized only
legacy GUIs; its apparent speed difference is not valid evidence.
Background checkpoint publication is also experimental and disabled. It retains
payload-last commit, rollback/recovery and load/quit/menu joins. Native fault
and lifetime tests, live preview-rename denial with unchanged original slots,
and OpenGL disabled-job fallback pass. Normal/reversed samples regress total
work despite cheaper commits; a balanced 20-save follow-up improves total work
29.11→23.57 ms but has mixed p99 statistics. All results are retained, and no
consistent whole-frame benefit or stall-removal claim is made for this switch.
Combining it with GUI reuse improves total CPU work 32.16→29.25 ms but regresses
mean window p99 29.22→34.73 ms and the maximum window 46.90→65.33 ms. Both remain
disabled. The worst candidate window spends 32.69 ms in setup, now split into
finer optional phases for diagnosis.
Those phases expose a test observer cost: recovery checks emit unbuffered
developer messages. Recovery takes 1.85–4.30 ms in the debug launch and
0.08–0.17 ms with developer messages disabled on the same staged binary.
These remain separate logging configurations, with no engine speedup claimed.
The buffered developer-off comparison also leaves GUI reuse and worker
publication disabled: total work falls 22.08→20.24 ms, but the maximum window
rises 33.66→39.84 ms. A separate narrow fix removes duplicate developer tracing
from explicit reads, retaining the existing fs_debug trace. Recovery now takes
0.09–0.21 ms in the original debug launch; lifecycle, damaged-save rejection and
blocked-preview preservation pass. Windows still peak at 40.64/27.19/34.97 ms,
so this removes measured logging overhead without claiming stall removal.
Projected shadow caster sorting and compatible indirect batching did not
improve whole-frame means across five alternating pairs and were removed.
A receiver-bound diagnostic reports full tile coverage in all 76 cascade
samples, providing no basis for a simple rectangular shadow crop. These are
rejected approaches, with evidence retained in the runtime performance record.
A fixed maximum-sample SSAO sequence likewise regresses the five-pair mean
by 0.591% and is removed; all original quality controls remain intact.
Buffering and deferred capture/storage materially reduce the pause, with GL/VK
reload, immediate load/quit/reopen, disabled-job fallback and failed-overwrite
preservation passing. CRC wire parity and capture orientation tests pass.
Scalar/dictionary batching and cached immutable UI snapshot identity reduce repeated
serialization work. Checkpoint descriptions now persist with the background
payload; fresh-profile first-save and failed-description-write preservation pass.
The buffered-field rotating windowed probes peak at 29.9–34.9 ms on Vulkan and 45.6–81.2 ms
on OpenGL. Production scalar/dictionary-wire tests and durable-file fault tests pass.
The subsequent revision validates verified worker bytes in memory and processes
CRC in sixteen-byte slices. Ordinary validation falls to 3.6–3.8 ms, but the latest
Vulkan save windows still peak at 64.14/31.67/33.89 ms; the first-save spike remains.
Reload, immediate load/quit/reopen, preview checks, and failed payload/description
overwrite preservation pass. All six focused native suites pass.
Starting snapshots with a 16 MiB allocation reduces warm serialization to
16–17 ms in the latest Vulkan probe; lifecycle and failed-description preservation
pass. The first checkpoint still peaks at 69.02 ms, so this does not close the
stall-removal gate. OpenGL also passes load/quit/reopen with warm serialization
around 15 ms, but checkpoint-window maxima remain 43.95–72.65 ms.
The subsequent revision reuses the worker's CRC for its exact verified immutable
snapshot, reducing main-thread validation to about 0.2 ms. Loading and recovery
still hash the file independently. Vulkan lifecycle, damaged-save rejection and
failed-overwrite preservation pass, as does OpenGL with workers disabled.
The Vulkan checkpoint windows still peak at 62.05/32.98/28.37 ms.
Compiled-script checksums are now warmed after compilation and reused for saves,
with invalidation on code/definition changes and independent validation on load.
Five fresh-reference comparisons and repeated compile/reload probes pass; they
identify about 3 ms of avoidable hashing per warm save. The non-oracle run passes
load/quit/reopen and failed-payload preservation, with a 0.5 ms globals phase and
14–16 ms warm serialization. Its checkpoint windows still peak at
60.50/27.57/28.71 ms, so the stall-removal gate remains open.
The preview's durable write/readback now runs in a bounded worker after capture.
The current-shadow Vulkan comparison reduces main-thread preview completion
from 1.16–1.27 ms to 0.29–0.32 ms. Checkpoint windows still peak at
25.23/24.45/27.36 ms, with 12–14 ms warm serialization. Immediate load/quit/reopen,
damaged-save rejection and blocked-preview preservation pass; OpenGL lifecycle
and blocked-description preservation also pass. The disabled-worker Vulkan
fallback passes lifecycle and blocked-payload preservation. Native fault injection rejects
failed persistence/readback and malformed thumbnails. This removes a disk barrier
from the game thread, but does not close the stall-removal gate.
An inline scalar-write experiment showed unchanged warm serialization
(12.108 versus 12.134 ms) and was reverted. Both runtime variants pass lifecycle,
corrupt-save rejection and failed-payload preservation. Expanded native checks
for scalar bit patterns, buffer boundaries and failed flushes pass for the
restored SP/MP writers. No performance improvement is claimed for that experiment.
The staged build's subsequent native fullscreen rotation averages 50.0 FPS,
with 26.15 ms p99 and a 33.04 ms maximum. Workload gates pass, performance gates
fail; this run contains no checkpoint saves and does not measure the preview change.
Vulkan also avoids clearing unused projected-shadow atlas space: paired native
fullscreen rotations save roughly 2 ms per frame, with pixel-identical frozen
captures. This is recorded separately from checkpoint serialization gains.
The subsequent native-fullscreen Vulkan qualification, including the post-process
changes, reaches 44.3 FPS mean with a 36.84 ms maximum frame. Workload gates pass;
the strict 150 FPS gates still fail.
The complete stall-removal acceptance remains open; these changes do not qualify
the separate native-fullscreen 150 FPS floor.

## CI qualification follow-up, 26 October 2026

- GOV-02/CORE-04: Windows durable-file validation now has a 900-second Meson
  budget. Its existing per-command 600-second timeout still fails hung commands;
  POSIX retains 180 seconds. Main and #191 both previously killed the ARM64
  harness at 180 seconds, before it could finish or retain its result.
- The Windows ARM64 push job now uploads `builddir/game-regression-logs` and
  `builddir/meson-logs`, where the push profile actually writes them. The old
  upload was attached to the POSIX matrix and looked in the unused PR-style
  directory. The commit-validation job's separate PR paths remain unchanged.
- Validation: the unchanged native durable-file fixture passes on Windows x64;
  workflow structure and the Meson syntax are checked. ARM64 execution remains
  a CI gate, including the independent locked-executable cleanup in
  [#201](https://github.com/themuffinator/openQ4/pull/201).
- The [26 October main run](https://github.com/themuffinator/openQ4/actions/runs/40277373140)
  has a passing Windows x64 payload for #183 reporter testing. The overall run
  is not green: Windows ARM64 failed native checks and macOS Intel Metal timed
  out in renderer-foundation self-tests. These are development artifacts, not
  a new release or proof that the open gameplay reports are resolved.

## Release-status reconciliation, 23 October 2026

- [x] Verify GitHub's latest release and public release list identify 0.13.2;
  preserve the withdrawn 0.14.000 tag and evidence without advertising a download.
- [x] Correct README/player documentation and the mislabeled 0.13.2 notes;
  preserve later changes in [unreleased notes](releases/unreleased.md).
- [x] Exclude withdrawn numeric versions from build/release baselines and
  changelog ranges; reject explicit republication and skip the withdrawn number
  when selecting a future version. The Meson/source-archive floor is 0.13.2.

**Planning authority, 18 October 2026:** the [development roadmap](roadmap.md)
is the single active plan for outstanding work. This document retains candidate
release wording and implementation/validation history. Its older unchecked boxes
record findings when written; they are not an authoritative current backlog.
Likewise, a recorded pass or failure applies to its stated revision, package and
scope. Track new work and closure decisions against roadmap IDs, then record
completed release material here and publish curated notes in `releases/`.

## GUI and material memory fixes, 26 October 2026

- [x] CORE-01: three map-cycle defects a Codex AUD-03 round found on 23 October,
  ported from its uncommitted worktree. Map precache reuses the multiplayer menu
  GUI templates instead of keeping a never-freed copy per load; window expression
  storage grows on demand instead of reserving 4,096 entries per window; windows
  that were never drawn start with valid coordinates, so an autosave no longer
  aborts on them. Hit maps are freed by the engine allocator through the
  declaration manager (renderer API 25), and a second `materialImage` no longer
  leaks the first map.
- [x] `gui_precache_lifetime.py`, `gui_initial_save_state.py`,
  `gui_expression_storage.py` and `material_hit_map_lifetime.py` pass with their
  failing controls, and stock airdefense1 and hub1 save and load on the landed
  build. The map-memory tours were not rerun; their harnesses need refitting to
  main's audio interfaces. Candidate player wording:
  [core refinements](releases/core-refinements.md).

## High-refresh pacing precision and the by-eye review kit, 25 October 2026

- [x] HF-07 limiter: the frame cap sleeps on SDL's high-resolution delay
  (`SDL_DelayNS`: a high-resolution waitable timer on Windows, `nanosleep`
  elsewhere) and spins only the 95th percentile of its recent oversleep
  (0.25–2 ms) instead of a fixed eighth of the frame. `com_preciseFrameWait 0`
  restores the previous wait.
- [x] HF-07 Windows 11: `Sys_Init` opts out of Windows 11 ignoring
  `timeBeginPeriod` for occluded processes, so the async thread's 1 ms tick
  timer and the frame cap keep their resolution behind other windows.
- [x] HF-07 background policy: hidden and minimized windows present at most
  60 Hz on every platform, from SDL's window flags (Windows applied it only to
  hidden windows; Linux and macOS not at all). `windowMinimizeTest` (cheat) and
  the `minimized` runtime case check it.
- [x] HF-07 instrument: `com_showFramePacing` and `framePacingSnapshot` time
  frames from the limiter's high-resolution stamp in 50 µs bins (whole
  milliseconds read a steady 240 Hz cadence as p95 5 ms), report the display
  refresh, and name what holds the rate (`presentation-cap` at any cap,
  `vsync` against the refresh, `frame-work`). The overlay judges the 240 Hz
  budget. The limiter's late-wake count now includes only frames that waited
  (a work-bound frame used to count its overrun), and the snapshot adds the
  mean busy-wait per wait.
- [x] HF-08 engine latency: `framePacingSnapshot` reports, per frame, the time
  from the draw's first late-look sample and from a drawn latency flash's
  press to the return of the swap, so a click-to-photon reading separates into
  the engine's share and the GPU/compositor/display share.
- [x] HF-08 review kit: `presentation_look_runtime.py --scenario review` runs
  captioned by-eye sessions (turns under TAA, firing via the cheat-gated
  `in_presentationViewProbeAttack`, third person, ragdoll and debris, the death
  camera, airdefense1's opening cinematic, tram1's monitor) and writes the
  operator's `review-checklist.md`.
- [x] HF-07 presented-frame timing: with `r_vkPresentTiming 1` and
  `VK_KHR_present_wait`, Vulkan stamps when each frame reaches the display;
  `rendererPresentTiming` reports presented intervals and the queueing after
  `vkQueuePresentKHR`, and the harness holds them to the 240 Hz budget
  (`--present-timing`). The waiter is quiesced before every swapchain
  retirement and stopped before device shutdown.
- [x] HF-07/HF-08 reference suite: `presentation_reference_suite.py
  --reference` runs both renderers' cap matrices full screen, vsync, adaptive
  vsync, variable refresh under the reported refresh, the policy cases, the
  operator's sensor session (`--scenario sensor`) and the review, and writes
  one summary.
- [x] Reference-suite rehearsal (26 October): `--windowed` ran all 32 cases
  end to end on the development laptop. A capture that a slow window cannot
  fill now dumps what it has, each case gets 600 s for its fresh image
  regeneration, and the summary lists cases it could not judge.
- Evidence: `openq4-presentation-pacing`, `presentation_hf_contract.py
  --mutations` (8 rejected) and the runtime cases recorded in the
  [presentation owners contract](presentation-owners.md). Physical pacing on
  the reference desktop, sensor latency and the by-eye verdicts remain open
  under HF-07/HF-08.

## Codex bot navigation port, 26 October 2026

- [x] MP-02/MP-03: a Codex round's bot fixes from 23 October, left uncommitted in
  its worktree, landed: inward rim landings after knockback, unsafe dodges handed
  to air recovery, planned drops that override the edge brake only on a proven
  landing, pursuit progress on refreshed routes, the earlier-hit knockback
  classifier, `bot_probeDrop`, and `mp_bot_audit.py` no longer reporting the
  session-loop counter as MP game time.
- [x] On the landed build, `mp_bot_drop_replay.py` and `mp_bot_rim_replay.py`
  pass, the bot regression and trajectory fixtures pass with their mutation
  controls, and 120-second six-bot audits on q4ctf5 and q4dm8 record no world
  deaths, stuck events, goal give-ups or traversal timeouts
  ([results](mp-bots.md#rim-landings-dodges-drops-and-pursuit-progress-26-october-2026)).
  Candidate player wording: [bot refinements](releases/mp-bot-refinements.md).

## Multiplayer round three, 24 October 2026

- [x] MP-04: leaving the match or quitting the game during the Arena
  end-of-match ceremony keeps the decided result (it was lost). game_mp
  publishes `ui_arenaDecidedResult` while the handoff waits; a disconnect
  records and reports it, a quit or error drop records it.
  `arena_decided_result.py` carries nine failing controls.
- [x] MP-05: demo seeks no longer leave gibs and debris frozen where they
  spawned (the Red Rover seek/debris finding, reproduced on a new recording);
  `mvd_seeking` and `mvd_seek_debris_contract.py` with five controls.
  `listClientEntities` names real classes and positions.
- [x] MP-07/MP-08: simulated bottleneck and burst loss (`net_forceRate`,
  `net_forceQueue`, `net_forceDropBurst`) with 14 header and 8 wiring controls;
  congestion, rate-control and burst-loss qualification.
- Evidence: [Arena](arena-campaign-ceremony.md#leaving-during-the-ceremony-24-october-2026),
  [demos](multiview-demos.md#red-rover-seek-debris-24-october-2026),
  [network](mp-network-qualification.md#congestion-rate-control-and-burst-loss-24-october-2026).
  Candidate player wording: [Arena](releases/arena-refinements.md) and
  [multiplayer](releases/multiplayer-refinements.md) refinements.

## Arena Restart match in place, 24 October 2026

- [x] MP-04: Restart match resets the live Arena match on its own server
  through an authorized `serverMapRestart` instead of relaunching it. The bots
  keep their client slots, so nothing is removed and added again; the
  introductions replay within 0.3 s, with scores and clock at zero. A short
  roster, a missing server or a due result falls back to the relaunch.
  `arena_restart_in_place.py` carries eleven failing controls.
- Evidence: [in-place restart](arena-campaign-ceremony.md#in-place-restart-24-october-2026),
  six no-input runs. Candidate player wording:
  [Arena refinements](releases/arena-refinements.md).

## Lag-compensation bias, 26 October 2026

- [x] MP-07: a positive `net_mpLagCompBiasMS` no longer rewinds a bot, a listen
  host or an unmeasured slot by the bias alone. The state fixture's three new
  bias-20 cases pass. Candidate player wording:
  [multiplayer refinements](releases/multiplayer-refinements.md).

## Saved language over an installer autoexec, 24 October 2026

- [x] #199 (UI-01/PLAT-04): GOG installers write `q4base/autoexec.cfg` with the
  install-time `sys_lang`, and extraction tools that unpack every language
  variant leave the last one (French in the report). It ran after the saved
  config, so each launch replaced the language chosen in Settings. Once a saved
  config exists, a loose `autoexec.cfg` in the retail game directory no longer
  changes `sys_lang`; it still supplies the first launch's language and runs
  its other commands. A player's own `autoexec.cfg` elsewhere and `+set sys_lang`
  still win. `startup_language_override.py` pins the order; a hidden client
  probe on Steam data with a GOG-style autoexec covers first launch, a saved
  choice across relaunches, the player's own autoexec and the command line.

## Multiplayer network qualification, 24 October 2026

- [x] Reconciliation, 26 October: bandwidth simulation retains the clock-wrap
  overflow fix through both queue departure and packet delivery. The combined
  rate/latency/jitter case checks identical delivery across clock wrap.
- [x] MP-07/MP-08: simulated network conditions on Windows and POSIX
  (`src/sys/NetworkImpairment.h`): latency, jitter with the reordering it
  causes, loss and duplication, seeded for repeatable runs, reported by
  `netImpairment`. The old Windows-only lag queue dropped datagrams over 1,400
  bytes, so a client could not connect with `net_forceLatency` set. Native
  cases and a wiring contract carry eleven failing controls.
- [x] Loopback qualification with three bots in both prediction modes: no
  resyncs under 50 ms, jitter, 10% loss, 20% duplication or combined
  conditions; the lead follows the round trip and rewinds follow the lead; an
  impaired client connects on its first challenge. A 20-bot dedicated server
  holds its remote client steady under the same conditions. Three q4dm8 bot
  audits record no navigation deaths; the jump-down did not recur.
- Evidence: [network qualification](mp-network-qualification.md). Candidate
  player wording: [multiplayer refinements](releases/multiplayer-refinements.md).

## Menu button resumes from the pause's pages, 24 October 2026

- [x] The controller's Menu button runs a retained document's `onMenuButton`
  where declared (Back elsewhere); Save Game, Restart Level and in-game Load
  Game ask for `menuResume`, which returns the pause and resumes play.
- [Behavior and checks](ui/input-routing.md#menu-button-24-october-2026):
  adapter and session gate harnesses, native page checks and an in-game capture.
  Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).
- [x] Later the same day the Menu button applies pending changes on the opt-in
  SYSTEM page and the Escape card's Admin page, and is Back while a dialog or
  an open list holds the input: adapter harness, native SYSTEM and Escape card
  checks and in-game captures ([Apply](ui/input-routing.md#menu-button-24-october-2026)).

## Trigger glyphs on the section strips, 24 October 2026

- [x] The Escape card's Match and Admin section strips end with each
  controller family's trigger glyphs, a legend for a controller only.
- [Behavior and checks](ui/input-routing.md#trigger-glyphs-24-october-2026):
  generation width checks, native strip checks and a capture of every family.
  Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).

## SETTINGS page, 26 October 2026

- [x] SETTINGS on the title and the pause opens a retained page with the
  stock screen's buttons and Load Defaults' question, returning onto each
  button from its page and handing DISPLAY to the stock panel where the
  SYSTEM page is off ([SETTINGS page](ui/retained-screens.md#the-settings-page-26-october-2026):
  native page test, gate contracts, captures at 16:9 in English and German,
  9:16, the defaults and the pause).

## CONTROLS page, 25 October 2026

- [x] The settings screen's CONTROLS page in the retained interface: the 49
  stock bindings, a wait for the next key with the stock cancel and clear
  keys and two-key rule, the multiplayer route and the stock page as its
  fallback ([CONTROLS page](ui/retained-screens.md#the-controls-page-25-october-2026):
  native page test, gate contracts, captures at 16:9 and 9:16).
- [x] The settings screen arrives on no panel while the retained CONTROLS
  page is available, its buttons choosing the pages (gate contract,
  captures through CONTROLS and the stock DISPLAY panel in English and
  German).

## GAME OPTIONS page, 25 October 2026

- [x] The settings screen's GAME OPTIONS page in the retained interface: every
  stock row in eight sections, CVar rows through allowlisted settings, the
  computed rows through the stock choices, the stock page as its fallback
  ([GAME OPTIONS page](ui/retained-screens.md#the-game-options-page-25-october-2026):
  native page test, gate route and contracts, captures at 16:9 and 9:16).
- [x] The multiplayer menu's Game Options hand-off opens the retained page
  too, the stock page where it cannot (gate route checks, captures from the
  menu event, a listen server's in-game menu and a broken page).

## AUDIO page, 25 October 2026

- [x] The settings screen's AUDIO page in the retained interface, every stock
  row applied at once and read back, with the stock page as its fallback
  ([AUDIO page](ui/retained-screens.md#the-audio-page-25-october-2026):
  native page test, compiled session checks, captures at 16:9 and 9:16).

## Tall views, 25 October 2026

- [x] Load and Save Game, Demos and Join Game give a tall view's extra height
  to their list ([tall views](ui/retained-screens.md#tall-views-grow-the-lists-25-october-2026):
  native suites unchanged at 16:9, captures at 9:16, 5:4 and 16:9).

## Shared notices, 25 October 2026

- [x] The notices follow the player across the retained menus, and a lost
  controller shows one until it returns
  ([shared notices](ui/retained-screens.md#shared-notices-and-a-lost-controller-25-october-2026):
  session harness, native and capture checks).

## Full server names, 25 October 2026

- [x] Join Game's details card names its server in full over the address
  ([full server names](ui/retained-screens.md#full-server-names-25-october-2026):
  native, session and capture checks).

## Notices, 24 October 2026

- [x] Retained pages show section 13.13's notices; Join Game's favorites use
  them, a removal offering Undo
  ([notices](ui/retained-screens.md#notices-24-october-2026): native, session
  and capture checks).

## Empty states, 24 October 2026

- [x] Empty lists offer the action that fixes them: CLEAR FILTERS on Load and
  Save Game, Demos and Join Game while a filter hides everything, GET NEW
  LIST when Join Game found no server.
- [Behavior and checks](ui/retained-screens.md#empty-states-24-october-2026):
  native page and session checks, captures in English and Russian.
- [x] Join Game shows loading rows while a scan has found nothing, and its
  first servers fade in with `row.reveal`
  ([loading rows](ui/retained-screens.md#loading-rows-24-october-2026)).

## Narrow views, 24 October 2026

- [x] The title, pauses, Arena pause, Multiplayer page, death screen and the
  multiplayer cards fit views narrower than 4:3 (5:4, portrait) instead of
  clipping; wider views are unchanged.
- [x] The Load, Save, Restart Level, Demos, Credits, Create Server, Join Game
  and Arena pages fit too: the page centres and its prompt bar stays on the
  bottom band.
- [Behavior and checks](ui/retained-screens.md#narrow-views-24-october-2026):
  native suites unchanged at 16:9 and captures at 9:16, 5:4 and 16:9.

## Map catalogs, 24 October 2026

- [x] The Vote and Admin pages' map choices hold 128 maps a game type
  (`RETAINED_VOTE_MAPS`, read by the generators), up from 48.
- [Behavior and checks](ui/multiplayer-menus.md#map-catalogs-24-october-2026):
  native option-count check and the game module build.

## View button, 24 October 2026

- [x] The View button runs a retained document's `onViewButton` (Back
  elsewhere and in dialogs or open lists); Join Game's toggles its server
  details card. Its default bind in play is the scores or objectives while
  held (`_impulse19`).
- [Behavior and checks](ui/input-routing.md#view-button-24-october-2026):
  adapter harness, native Join Game check and an engine capture. Candidate
  player wording is in [baseline refinements](releases/baseline-refinements.md).

## List focus memory, 24 October 2026

- [x] Scrolling lists remember their last focused control; a directional
  move back into a list returns there (section 13.4).
- [Behavior and checks](ui/input-routing.md#wrapping-menus-24-october-2026):
  a native Load Game check and an engine capture.

## Wrapping menus, 24 October 2026

- [x] The title, both pauses, the Arena pause and the Single Player,
  Campaign and Multiplayer pages wrap Up and Down through their menu and
  plinth links (section 13.4); an authored move passes through an entry that
  cannot take focus.
- [Behavior and checks](ui/input-routing.md#wrapping-menus-24-october-2026):
  native wrap checks on the real title and pause and engine captures.
  Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).

## Menu button glyphs, 24 October 2026

- [x] Each controller family's Menu button glyph ends the Escape card's
  Admin Apply changes plate, which the button presses, for a controller only.
- [Behavior and checks](ui/input-routing.md#menu-glyphs-24-october-2026):
  native plate checks and a capture of every family.
- [x] Each family's View button glyph sits at the corner of Join Game's
  server details card, which the button hides, for a controller only
  ([View glyphs](ui/input-routing.md#view-glyphs-24-october-2026): native card
  checks and a capture of every family).

## Joined lines and Demos capitals, 24 October 2026

- [x] List separators, the time in the mission, a save's date and time and
  the load time's decimal mark and unit come from each language's table
  (`#str_231532`-`#str_231535`); the Demos page's headings and plates are
  capitals in every language (`#str_231530`, `#str_231531`, `#str_231536`).
- [Behavior and checks](ui/retained-screens.md#joined-lines-and-capitals-24-october-2026):
  gate harness fixtures, a format check over every table and captures in
  three languages. Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).

## Controller prompt settings and list paging, 24 October 2026

- [x] Game Options > Controller gains Prompt Style (`in_promptStyle`) and Swap
  Confirm and Back (`in_swapConfirmBack`), both console-only before, in all
  twelve language tables. Later rows, hovers, section anchors and the scroll
  extent move two rows; the settings registry and coverage test follow.
- [x] PageUp/PageDown and Home/End move focus a visible page through a retained
  list, or to its ends; the triggers send them where a document declares no
  section events, and step a focused slider coarsely.
- [x] Join Game's row focus follows its server through new orders, refreshes
  and filters onto its page (UI-03 stable refresh focus), checked by the
  session gate.
- [Behavior and checks](ui/input-routing.md#settings-rows-24-october-2026):
  settings coverage, GUI value/clipping/navigation contracts, the focus reveal
  and adapter harnesses, and in-game Settings and Load Game captures. Candidate
  player wording is in [baseline refinements](releases/baseline-refinements.md).

## Audio progression audit with fights, 6 November 2026

- [x] AUD-06/07: `openq4_killHostiles` and `audio_scene_audit.py --fight` let the
  progression audit win the fights scripts wait on; all 42 campaign maps ran
  (1,967 steps, 1,052 kills) with no mute leak or untranscribed cue, reaching 88
  dialogue lines no earlier report had.
- [x] A full client entity table no longer ends the level with "cannot load
  collision model" (both game libraries); found on `airdefense2`.
- [x] The audit now reports a dead player and a map that ends inside its own
  scene, matches its markers after unterminated script output, and removes
  failed-objective entities in its disposable session. The `teleport` cheat
  moves a driver's vehicle with it.
- Evidence and remaining gates (the probe player's death at `walker`'s step 11,
  ordered human play, physical devices):
  [fights in the progression audit](audio-voice-evidence.md#fights-in-the-progression-audit-6-november-2026).

## Audio progression-audit recovery, 26 October 2026

- [x] AUD-06/07: preserve the unfinished progression run's failures and distinguish
  current speech channels from effects that reuse the same sound files.
- [x] AUD-06/07: qualify progression with player-contact trigger filtering and
  level exits suppressed in disposable sessions; retain missing-entity counts
  and the distinction from an ordered playthrough.
- Evidence and remaining gates: [progression audit](audio-voice-evidence.md#progression-audit-recovery-26-october-2026).

## Audio save/restore on Linux, Arena on Linux and longer scene walks, 25 October 2026

- [x] AUD-02/06/07: a real save and load passes on Linux (subtitles and Dialogue Volume);
  the earlier failure came from the test tree's short Git history. The subtitle harness
  now proves the restore by the game sound world's clock being held through the load.
- [x] AUD-02: the Arena pause/resume sound harness passes on Linux.
- [x] AUD-06/07: a 90 s tail per map lets long conversations finish; the scene walk now
  starts 1,179 distinct dialogue lines (842 transcribed) with no mute leak or
  untranscribed cue.
- Evidence and limits: [saves on Linux](audio-voice-evidence.md#saves-on-linux),
  [Arena](audio-voice-evidence.md#arena-pause-on-linux) and
  [the longer walk](audio-voice-evidence.md#a-longer-tail-in-the-scene-walk).

## Audio split captions, low-memory notice and voice drift, 25 October 2026

- [x] AUD-07: two overlapping captions at the largest portrait text take one line each,
  the newer above the cinematic artwork and the older below it (visual specification
  1.27).
- [x] AUD-03: `testLowMemoryNotice` sends the operating system's low-memory event through
  SDL; each notice now trims audio once, not twice.
- [x] AUD-05: `s_voiceTestClockRate` drift between real peers; the receiver follows a
  sender 0.1% slow or fast and rejects nothing (Linux listen and dedicated servers).
- [x] Linux builds of main start again (the multiview demo reset a cvar during static
  initialization), and audio save/restore passes on Linux. A save failure first reported
  here came from the test tree's short Git history, not Linux.
- Evidence and limits: [two speakers, the low-memory notice and voice drift](audio-voice-evidence.md#two-speakers-at-the-largest-text-the-os-low-memory-notice-and-voice-clock-drift-25-october-2026).
  Candidate player wording is in [audio refinements](releases/audio-refinements.md).

## Audio scripted scenes, saved-game restore and process memory, 24 October 2026

- [x] AUD-02/06/07: loading a saved game resumes restored sounds where they were saved
  instead of as far into them as the load took. Pausing a sound world now refreshes
  the per-frame sound clock, and reading a saved sound state pauses the world until
  the map change completes. The subtitle and Awakening dialogue harnesses now make
  real saves (their earlier saves were refused inside opening cinematics).
- [x] AUD-06/07: `audio_scene_audit.py --walk` fires every campaign map's scripted
  speech scenes without input: 689 triggers, 1,326 dialogue starts, 1,050 distinct
  lines (727 transcribed), no mute leak, misroute or untranscribed cue.
- [x] AUD-07: at the largest text a lone portrait cinematic caption pages a line at a
  time below the artwork; speaker names never run under the words (visual
  specification 1.26).
- [x] AUD-03: `listSounds` reports process memory on every platform; audio's cost
  outside the managed payloads is bounded at about 60 MiB in six stock openings.
- [x] AUD-05: voice chat passes on Linux x64 against listen and dedicated servers.
- Evidence and limits: [scripted scenes, process memory and the largest captions](audio-voice-evidence.md#scripted-scenes-process-memory-and-the-largest-captions-24-october-2026).
  Candidate player wording is in [audio refinements](releases/audio-refinements.md).

## Audio on Linux, voice output loss and long sessions, 24 October 2026

- [x] AUD-02/03/04/06/07: the output-recovery, subtitle and scene harnesses pass on
  Linux x64 (Ubuntu 24.04, WSL2, Mesa llvmpipe, system OpenAL Soft 1.23.1). The
  Linux dedicated server links again: its OpenAL stub gains `alSourceRewind`.
- [x] AUD-05: voice playback drops frames while the output is gone and restarts with
  the next one after it returns; the simulated outage stops voice sources too.
- [x] AUD-02/03: `audio_scene_audit.py --repeat` long sessions; six maps' openings
  replayed three times keep flat residency, voices and stream reservations.
- Evidence and limits: [Linux, voice output loss and long sessions](audio-voice-evidence.md#linux-voice-output-loss-and-long-sessions-24-october-2026).
  Candidate player wording is in [audio refinements](releases/audio-refinements.md).

## Audio scenes, centred reverb and portrait captions, 23 October 2026

- [x] AUD-03: start a streamed track on a recycled voice reliably. A source that
  had played stayed `AL_STOPPED`, so OpenAL counted its queued blocks as already
  played and the track never started once the voice pool had cycled. The flush
  now rewinds the source. This is a regression in this cycle's unreleased
  streaming.
- [x] AUD-04: give a centre-routed positional sound a mod reverb's room rolloff on
  its sends, which OpenAL never applies to direct 5.1 channels.
- [x] AUD-06/AUD-07: `s_logSpeech` speech audit and `audio_scene_audit.py`, every
  campaign map's opening with cinematics, probing routing, muted Dialogue Volume,
  subtitle cues, streams and audio residency. Engine-made lipSync defaults no
  longer hide sample-path transcripts. Dialogue Volume holds through simulated
  output loss and in-place resets.
- [x] AUD-07: portrait cinematic captions hang below the artwork when two lines fit
  (visual specification 1.25, register increment re-binding).
- Evidence and limits: [authored scenes, centred reverb and portrait captions](audio-voice-evidence.md#authored-scenes-centred-reverb-and-portrait-captions-23-october-2026).
  Candidate player wording is in [audio refinements](releases/audio-refinements.md).

## Save details, 23 October 2026

- [x] CONTINUE and the Load Game page name a save's difficulty and the run's
  play time; the pause's level block adds the run's total time under the
  mission time and keeps the last save's age on its own line. Objectives
  completed on a map survive loading a save. The play time and difficulty
  travel in the persistent player info every save header already holds, so
  the save format is unchanged; saves from earlier builds show neither. Two
  play-time phrases are in all twelve language tables.
- [Behavior, checks and limits](ui/retained-screens.md#save-details): session
  gate, native page checks, the generator's fit checks, and an in-game
  capture run across two maps. Candidate player wording is in
  [baseline refinements](releases/baseline-refinements.md).

## High-refresh presentation owners, 23 October 2026

- [x] HF-03: add opt-in late look (`in_presentationView`) through an absolute-angle
  engine sampler and a frozen per-draw SP/MP correction of the drawn eye and what
  it holds. Game API 54 passes the usercmd generator to the game modules; engine
  and game modules must be replaced together.
- [x] HF-04: re-place the live view muzzle-flash light and flashlight from the drawn
  gun; draw unbound client debris, brass and gibs, their trails and active AF
  (ragdoll) poses between tick samples; record the remaining exclusions.
- [x] HF-05 (alternate views): sample cinematic and private cameras, third person
  and the death camera per tick and draw them on the scene clock with cut,
  identity, cadence, jump and carrier rules. Remote actors stay with MP-07.
- [x] HF-06/HF-07 definitions: cross-system rules, 240 Hz budgets and the
  measurement procedure in the [presentation owners contract](presentation-owners.md);
  physical qualification and live MP late look remain open under HF-07/HF-08.
- Evidence: `openq4-presentation-look`, `presentation_hf_contract.py --mutations`,
  the updated interpolation contract and live SP probes recorded in the
  contract. Candidate player wording is in
  [baseline refinements](releases/baseline-refinements.md).

## High-refresh presentation, second round, 24 October 2026

- [x] HF-05: decide remote actors. While the local client plays, remote bodies
  stay on the tick because MP-07 rewinds by the displayed pose age. A spectating
  client draws them, the followed player's third person and the death camera
  on its camera's clock. Vehicle cockpit eyes are bounded against their
  interpolated cockpit, and nested monitor views are recorded.
- [x] HF-06: a diagnostic guard counts event dispatch, script threads, entity
  physics and effect spawns that start inside a prepared draw;
  `g_presentationClockCapture` reports it per draw and the runtime harness
  fails any case that records one. Effects BSE starts while the renderer
  services it (particle impacts, nested segments) are counted apart.
- [x] HF-07: the runtime harness gains a `pacing` case for the cap matrix,
  judged against the 240 Hz budget; only operator-declared `--reference` runs
  in a visible foreground window qualify.
- [x] HF-08 (engine-side part): loopback listen-host and remote-client
  late-look cases consume input once and remove about 16 ms; spectator and
  walker-cockpit cases.
- [x] HF-08 zoom, lifts and loss: the cheat-gated `in_presentationViewProbeZoom`
  holds zoom without device input. Zoomed look, riding a lift and a remote
  client losing 5% of its packets all consume input once and draw the sampled
  input. With the network impairment rework, a client under 40 ms latency,
  10 ms jitter and 5% loss each way does too.
- [x] HF-06 demos: a session records a render demo under late look while the
  capture runs, then plays it back with `timeDemoQuit`; both stay clean.
- [x] HF-06/HF-07/HF-08 further cases: stereo frames from the OpenXR test
  runtime and a remote-camera monitor pass the gameplay guard. The capture
  reports each draw's preparation cost (0.35 ms of a 5 ms frame). Vsync and
  Vulkan pacing runs are recorded, and the declared frame-rate policy is
  contract-pinned. Rapid turns at 2200°/s consume input once. Late look stays
  excluded in vehicles by decision.
- [x] HF-08 physical latency instrument: `com_latencyFlash` draws a white bar on
  the first frame after a fire press for LDAT/OSLTT-style sensors; the `flash`
  runtime case checks one drawn frame per press.
- [x] HF-06 closed: one session with temporal AA, shadow maps, late look,
  retained UI and audio survives a save and load, `vid_restart` and an AVI
  capture, with every capture interpolated and no gameplay in any draw.
- [x] HF-07 catch-up and menus: the cheat-gated `com_presentationStallProbe`
  stalls a frame; through repeated 100 ms stalls the drawn view never steps
  backwards. Menus follow `com_maxfps`, and the harness has `--fullscreen` and
  variable-refresh reference cases.
- Evidence: `presentation_hf_contract.py --mutations`, the interpolation
  contract and live captures recorded in the
  [presentation owners contract](presentation-owners.md). Physical pacing,
  physical latency and by-eye review remain open under HF-07/HF-08.

## Audio output recovery, layout changes and memory, 23 October 2026

- [x] AUD-02: restart one-shot sounds that a lost or reopened output stopped at the
  scene clock, as loops and bounded queues already were; allocate no voices
  while the endpoint is gone; resume a line paused across the change in place.
  `s_testDeviceInterruption` and `listSoundVoices` support no-device checks.
- [x] AUD-04: apply speaker-layout, HRTF and limiter changes with an in-place
  device reset instead of a full sound restart, and re-route playing voices at
  their scene clock when the centre speaker comes or goes.
- [x] AUD-03: trim reclaimable audio on an operating-system low-memory notice
  (`s_trimIdleSeconds`, `s_trimAudioMemory`); stream Ogg files without usable
  length metadata through a small stb_vorbis seek extension; drop the one-hour
  streamed-track limit; build streamed envelopes on job workers during loads.
- [x] AUD-06/AUD-07: installed-asset dialogue routing audit (no unclassified
  speech in either campaign) and per-language transcript coverage.
- Stock-map gameplay, native, compiled-production and static checks pass; see
  the [evidence and limits](audio-voice-evidence.md#output-recovery-layout-changes-and-memory-pressure-23-october-2026).
  Physical endpoints, real low-memory notices and other operating systems remain
  open. Candidate player wording is in [audio refinements](releases/audio-refinements.md).

## Focus-following prompt verbs, 23 October 2026

- [x] The pages' prompt bars name what Accept does for the focused control:
  LOAD on LOAD GAME and the chosen save, DELETE, SAVE on SAVE GAME and the
  chosen save, PLAY and JOIN, and SELECT elsewhere. Accept leaves the bar
  while nothing focused can act. Buttons gain an optional `prompt`; five
  capitals verbs are in all twelve language tables.
- [Behavior, checks and limits](ui/retained-screens.md#page-prompt-bars):
  runtime, adapter and native page checks, and OpenGL (English) and Vulkan
  (French) captures. Contextual west/north prompts remain open (UI-02).
  Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).

## Strogg confirmations, 23 October 2026

- [x] Give the Strogg pause's QUIT TO MENU and EXIT questions, and the Strogg
  Save/Load pages' delete, overwrite and notice dialogs, the Strogg family:
  30-degree shoulders, #F59512 rails, R_Strogg text and Strogg action plates,
  with every label translating from runes as the dialog opens. Behavior,
  focus and timing stay the stock dialog's; Marine dialogs are unchanged.
- [The construction and its evidence](ui/retained-screens.md#strogg-confirmations)
  cover native checks and OpenGL/Vulkan captures in English, French and
  Russian. The art derives from section 13.7 and awaits review under UI-05.
  Candidate player wording is in [baseline refinements](releases/baseline-refinements.md).

## UI acceptance transition, 23 October 2026

- [x] Add the register's separate acceptance audit (UI-11). It accepts only
  named requirements and resources from committed, reviewed records that are
  current and prove each conjunct, constraint, dependency and per-resource
  behavior/artwork claim. Checkpoints withdraw stale acceptance explicitly.
- [x] Register three UI checks that earlier increments added without wiring them
  into CI and local validation.
- [The transition's rules, tests and limits](ui/acceptance-transition.md) are
  developer tooling with no player-visible change; nothing has been accepted
  with it yet.

## Multiplayer and bots round, 23 October 2026

- [x] MP-08: hostname connections try up to eight resolved addresses, the
  historical first choice then alternating families, three unanswered challenges
  each. Native ordering/failover cases pass, and a two-process probe joined an
  IPv6-only loopback server through `localhost` after three unanswered IPv4
  challenges. The guide documents the sequence.
- [x] MP-07: clients advertise their remote extrapolation
  (`net_mpRemoteExtrapolation`). The server rewinds by the displayed pose age and
  never lets a client claim lengthen it. `lagCompStatus` and the
  `net_mpPredictDebug` pose age expose both sides. An executable state fixture
  passes with five failing controls; the runtime probe shows the advertisement
  and rewinds following the client's mode. The guide no longer recommends
  enabling both corrections. Defaults are unchanged. The probe's rising
  reported lead was a client regression since 0.13.0 (42e765882): the
  widened prediction adjustment turned the unsigned catch-up step into full
  slowdown, leaving clients a frame behind with late commands. The steps are
  signed again; a closed-loop fixture with four failing controls covers it.
- [x] MP-01: passed ballots revalidate against the current session (adapter-owned
  once-only acknowledgement); veto turns use the series side; spectator follow
  latches spawn identity. Extended contracts fail against each reverted fix.
- [x] MP-05: `mvdRecover`, previous/slot/name follow, a named follow overlay and
  recording status in `mvdStatus`. MVDs with two or more players recorded since
  27 September failed playback validation because the start-state powerup mask
  grew to 18 bits; both readers now detect 16 or 18. Recovery (2,032 cut points)
  and start-state fixtures pass with failing controls; the runtime probe
  recovered a killed recording and played it to its clean end.
- [x] MP-02/MP-03: bots refuse splash shots whose predicted push would throw a
  team-mate into the void, and penetrating rails with a team-mate anywhere on
  the line; trench far rims are linked; a listen server clears debug lines
  each frame. MP physics has no ladders; lifts stay unmodelled. A headless
  build of all 33 catalog variants and seven bot audits pass.
- [x] MP-06: client air drains once per game frame, not per prediction replay;
  `s_soundWorlds` reports the underwater muffle; swimming bots dive for an
  underwater route corner while they have air to spare.
- Candidate player wording: [multiplayer refinements](releases/multiplayer-refinements.md)
  and [bot refinements](releases/mp-bot-refinements.md). Evidence and remaining
  scope are recorded under MP-01 to MP-08 in the [roadmap](roadmap.md).

## Vulkan roadmap round, 23 October 2026

- [x] VK-03: complete, fail-closed device and instance inventories; automatic
  starts resume on the next compatible GPU after a device, swapchain or resource
  failure, bounded to four adapters, while explicit `r_vkDevice` choices never
  substitute. 107 native fake-driver cases, the production startup route
  against adapter doubles, and NVIDIA/Dozen/Intel hardware drills pass.
- [x] VK-04: authored GLSL/ARB stages follow a specified per-stage fallback
  policy with once-per-reason reports and a fuller `reportShaderPrograms`. The
  version table evicts stale versions, a full ARB table no longer ends the
  session, and shader parameters with no Vulkan value follow OpenGL.
- [x] VK-05: per-domain exposure, temporal-history invalidation and settling of
  late linear-HDR failures; `rendererVulkanHDRInfo` reports admission counts.
- [x] VK-07: Vulkan captures fail closed instead of saving cleared, skewed or
  unread images, and stencil diagnostics carry frame and view identity.
- Evidence and limits: [device admission](vulkan-device-selection.md),
  [material programs](vulkan-material-programs.md), [HDR](vulkan-hdr.md) and
  [captures](vulkan-captures.md). AMD, physical Linux and MoltenVK hardware
  (VK-01), presentation pacing (VK-02) and the measured VK-06 work remain open.
  Candidate player wording is in [unreleased notes](releases/unreleased.md).

## Save browser controls, 23 October 2026

- [x] Add date/name/location sorting, manual/autosave filter tags and explicit
  refresh to the Marine and Strogg Save/Load pages. Preserve selection and row
  focus by filename and keep typed names intact. Freeze catalog operands and
  reject stale queued actions; keep deletion tied to its confirmation target.
- [Save browser evidence and limits](ui/save-browser.md) record native,
  adapter/session and real save/restore qualification. The broader UI-03 scope,
  physical input and other-platform acceptance remain open. Candidate player
  wording is in [baseline refinements](releases/baseline-refinements.md).

## Multiplayer player identity, 23 October 2026

- [x] Bind Players selections and local friend/mute marks to a connection's
  generation. Held or queued menu actions cannot follow a replacement occupant;
  ordinary score refresh retains valid work. The statistics return to the local
  player when the selected connection leaves. Package the matching engine and
  game modules together; the retained-menu cover protocol is now version 2.
- [x] Pass the Windows build/stage, 12 targeted native suites, the adapter's
  production case and 20 rejected mutations, the session gate, and production
  player-state checks across 32 slots with three rejected mutations. Review
  14 engine screenshots from live slot-replacement matches in OpenGL at
  1280×720 and Vulkan at 1920×1080. Both return to gameplay with no retained-UI
  warnings or errors; stock/bot asset warnings remain.
- [Player identity evidence and limits](ui/player-identity.md) preserve delayed
  remote voice-mute packets, kick/admin identity, device/platform qualification
  and full UI-03/UI-09 acceptance as outstanding. Candidate player wording is in
  [baseline refinements](releases/baseline-refinements.md).

## Multiplayer objective POI, 23 October 2026

- [x] Add restrained world-positioned flag-base, assault-point and DeadZone
  markers. Distance is the only text; pennant fill/position, zone cores and a
  small artifact padlock express status. Keep faction colours, off-screen
  bearings, crosshair fading, bounded overlap and HUD visibility rules.
- [x] Build and stage the Windows engine and matching SP/MP modules, pass six
  focused suites and the twelve-language source check, and qualify live CTF
  pickup/drop and DeadZone control in OpenGL/Vulkan engine captures. Verify a
  remote client, stock fallback and absence in DM. Preserve the existing stock
  asset/precache warnings as unrelated findings. [Evidence and remaining scope](ui/remastered-hud.md#multiplayer-objective-markers-23-october-2026)
  leave the broader UI-03/UI-04, real-headset, platform and performance gates open.
- [x] Extend markers to the OpenXR stereo HUD using tracked head bearings and
  physical HUD dimensions/offset. Keep ordinary rendering after runtime exit.
  Pass seven focused suites plus the VR ABI/source contract and simulated CTF/
  DeadZone gameplay, with 44 engine render-target/desktop captures. Compare
  Classic/retained marker footprints to catch missing strokes. Keep distance
  as the only text; real-headset readability and comfort remain unqualified.
- [x] Landed on main 26 October from the Codex worktree, with game API 55 and
  register revision 20 (visual specification 1.28, pending `FLOW-067`). On the
  landed build, GL and Vulkan captures (CTF 10, Dead Zone 5 each), simulated-OpenXR
  captures (CTF 19 and Dead Zone 22 stereo HUD frames), the native layout test
  and the register revision audit pass.
- Candidate player wording is included in [unreleased notes](releases/unreleased.md).

## Multiplayer roster reachability, 23 October 2026

- [x] Keep multiplayer player lists and summary scoreboards readable with fixed
  headings, 24 dp rows and scrolling through all 32 connection slots. Late
  players and spectators remain reachable; refresh and view restoration retain
  the scroll, while a smaller roster clears obsolete offsets. The kick chooser
  also includes every other connection. This leaves the configured server limit
  of 16 unchanged.
- [x] Pass the full Windows build/stage, six native suites, the session gate,
  four capture-oracle tests and generated-document checks. Review 30 engine
  screenshots from OpenGL at 1280×720 and Vulkan at 1920×1080, using live
  three-player gameplay plus explicit 32-slot production-document fixtures.
  This does not qualify a 32-client network session. Existing stock/bot precache
  warnings remain; there are no retained-UI warnings or errors in either run.
  [Evidence and remaining scope](ui/multiplayer-menus.md#full-roster-scrolling-23-october-2026)
  keep the broader UI-03, device, layout and performance gates open.
- Candidate player wording is included in [baseline refinements](releases/baseline-refinements.md).

## UI settings and control follow-through, 23 October 2026

- [x] Refresh an open, clean SYSTEM page after external settings and performance
  preset changes. Preserve dirty drafts and unfinished numeric text, report
  conflicts and prevent stale Apply. Queued value controls now check their
  starting values and current choice availability before dispatch, while valid
  ordered edits still work. Eleven native suites, 110 production-service
  scenarios and nine-stage SP/OpenGL/MP/Vulkan gameplay captures cover this
  bounded UI-01/UI-09 increment. [Evidence and remaining scope](ui/external-settings-refresh.md).
  No requirement, migration or final UI gate is accepted.
- Candidate player wording is included in [baseline refinements](releases/baseline-refinements.md).

## Outstanding-session follow-through, 22 October 2026

- [x] Repair airborne recovery's missing narrow headings and low-health fallback,
  using actual live pickup triggers. The recorded CTF5 controller and both
  recoverable fixed headings land alive when each trial starts with fresh map
  items. Native extracted-code fixtures and five failing mutation controls cover
  heading choice, retained steering, supported fallback and pickup eligibility.
- [x] Complete buffered bot removal before replacement; keep impulse replays in
  playable warmup and restore the map before each trial. Trials no longer forfeit
  the match, reuse an occupied bot slot or consume the next trial's health pickup.
- [x] Finish one complete 33-variant catalog on an unchanged staged build:
  33/33 pass, 215 combat kills, zero world deaths including warmup, zero traversal
  timeouts and no impulse exclusions. All maps exceed 40 simulated seconds.
  The [catalog evidence](mp-bots.md#single-build-complete-catalog-22-october-2026)
  retains route stalls/give-ups and timing peaks; wider MP-02/MP-03 gates remain
  open. Thirteen focused bot checks and the freshness regression pass.
- [x] Record five 1,000-frame Vulkan UI samples per title/System/SP-pause view and
  four GL/Vulkan stock/retained preset/scale capture runs. The intermittent menu
  failures were not reproduced in this bounded matrix; title/System CPU budgets
  still fail, and the open System page's external-preset binding remains stale.
  See [current UI evidence](ui/runtime-performance.md#current-build-measurements-and-presetscale-reproduction-22-october-2026).

## Unreleased corrections after the withdrawn 0.14.000 build

- [x] Integrate the selected archived VR work against the current renderer
  (VR-01): Vulkan stereo with the runtime creating the device through
  `XR_KHR_vulkan_enable2` (render API 32), headset submission with no desktop
  presentation, OpenGL binding through EGL on Wayland (`XR_MNDX_egl_enable`)
  and single-player world-GUI pointing from the VR weapon hand. VR-06's bounded
  image counts, idempotent teardown and deferred retirement now cover Vulkan.
  Also fixed: the settings tabs never received the mouse or VR pointer, because
  every page's full-size group sat above the tab strip. Standing and seated
  play spaces (VR-03): STAGE, LOCAL_FLOOR or LOCAL by `vr_playSpace`, standing
  height from the floor, `vr_calibrateHeight`, a localized **Play Space** row.
  Per-eye temporal AA (VR-05, `r_temporalAA 1`): eye-specific view identity and
  history in the game chain and both backends, with each VR target's temporal
  state carried to the backend; Vulkan's portal sky now finds its own eye's
  main view. Stereo frame cost is measured on the simulated runtime (VR-04).
  [Simulated-runtime evidence](platform-recovery-review.md#vulkan-egl-and-world-gui-xr-integration-6-november-2026);
  headset qualification is the [VR-02 checklist](vr-headset-qualification.md),
  and Quest waits on AND-05.

- [x] Mirrors and other reflective surfaces now show each eye its own
  reflection in VR; before, the right eye saw the left eye's (VR-04). Screens
  in the world are now drawn once per frame for both eyes instead of once per
  eye, saving CPU time in stereo without changing the picture
  (`r_vrStereoGuiShare 0` turns this off).
  [Evidence](platform-recovery-review.md#stereo-work-sharing-8-november-2026).

- [x] OpenGL: screens in the world, smoke and glass no longer vanish where the
  sky is behind them, which was most noticeable in the VR headset, and mirrors
  show a reflection instead of horizontal streaks. Vulkan was unaffected.
  [Details](platform-recovery-review.md#opengl-portal-sky-and-mirror-fixes-9-november-2026).

- [x] VR: nearby ground, wreckage and other large parts of a level no longer
  disappear from the headset view, leaving distant desert showing where they
  should be. Both renderers were affected; flat play was not.
  [Details](platform-recovery-review.md#vr-eye-culling-restored-9-november-2026).

- [x] VR: The Awakening's speeder-bike race no longer leaves you looking
  backwards over the bike when it seats you. Any vehicle a level turns as it
  seats you now faces where the level points it.
  [Details](platform-recovery-review.md#vr-seat-turn-restored-9-november-2026).

- [x] Recover safely when a damaged save fails partway through physics or
  vehicle data. Partial physics state no longer points cleanup at another
  entity or indexes unset contacts, and failed vehicle-part reads release their
  allocations. Invalid vehicle seat references fail with a load error. Existing
  save formats are unchanged. Windows/Linux fault-injection controls and the
  Windows save/restore suite pass; runtime evidence and remaining scope are in
  [save reliability](savegame-reliability.md#physics-and-vehicle-ownership-at-interrupted-field-boundaries).

Candidate save release wording: Loading a damaged save now recovers more
reliably when its physics or vehicle data is incomplete, so you can choose
another save without restarting the game. Valid saves keep their existing format.

- [x] Clarify UI defaults against the source and release tag: current
  `Session_menu.cpp` declares archived `ui_retained 1`; tag `v0.14.000`
  declared 0 before commit `1aa2332d9` restored 1. Align current player and
  developer guides, retain the release's historical default, and explain
  saved preferences and explicit launch overrides. Correct stale gate comments
  and the player guide's in-match multiplayer menu coverage.

- [x] Link Schnapple's [signed and notarized macOS builds at Mac Source Ports](https://www.macsourceports.com/game/quake4)
  from the README, macOS setup guide and 0.14.000 release notes, with contributor
  credit. Distinguish its Apple Silicon/Intel Universal 2 download from our
  unsigned Apple Silicon preview packages on GitHub.

- [x] Keep script-choreographed movers at their authored speed in Turbo mode
  (#184). Turbo doubled every mover, but map scripts time their own waits and
  actors against a `func_mover`: hangar1's Viper Squad lift arrived about 3.5 s
  early, `aiScriptedStop` released Tech Li before his scripted stand-and-turn
  finished, and that thread then left him in an endless scripted idle, blocking
  the level. Reproduced from a fresh map with the real lift trigger: stuck every
  time with `g_turboMode 1`, released with it off and with this change. Plain
  `func_mover`s now keep their timing; doors, platforms, elevators and decorative
  movers are still sped up. `turbo_scripted_movers.py` checks the exemption.
  On 4 November the reusable fresh-map lift probe passes boarding, ascent,
  release and the exit tether on OpenGL and Vulkan. A separate runtime with the
  exemption removed reproduces the stuck actor. Already-stuck saves still
  require a pre-lift save; see the [qualification](retail-baseline-ledger.md#hangar-perimeter-lift-4-november-2026).

Candidate gameplay release wording: In Turbo mode, Tech Li no longer freezes
after the Viper Squad lift in Hangar Perimeter; lifts and machinery that levels
choreograph keep their normal speed so scripted scenes stay in step.

- [ ] Keep saves loadable after long stretches with the blaster, gauntlet,
  grenade launcher, lightning gun or dark matter gun (#192). Those weapons have
  no view GUI, so the events every shot and weapon switch posted for it were
  never delivered and piled up in each save; once more than 256 were queued,
  the restore guard rejected the save ("invalid pending GUI event count 372")
  even on the build that wrote it. `rvWeapon::UpdateGUI` now drops events no
  GUI can take, and the restore bounds the count by the bytes left in the save
  instead of a fixed cap, so existing saves holding such a backlog load again.
  No save-format change. `weapon_gui_event_queue_contract.py` keeps SP and MP
  aligned.

Candidate save release wording: Saves made after firing or switching between
weapons without an ammo display many times no longer fail to load with an
"invalid pending GUI event count" error, and saves that already fail this way
load again.

- [ ] Keep HUD window state when an older save's HUD is read through its earlier
  definition. Copying only the GUI state dictionary lost values scripts set on
  windows: a save taken in a boss fight loaded without its boss bar, because
  `showBossBar` sets `BossBar::visible` and restoring the boss does not replay it.
  `idUserInterface::ReadFromSaveGameWithLayout` now carries every window value the
  game changed from the saved definition, with whether it still follows its
  expression, as well as the state, flags and cursor. A HUD a level script hid
  (as `game/mcc_1` does from its start) stays hidden after loading an older save.
- [ ] Add `savegame_gui_layout_contract.py`, which pins every serialized openQ4 GUI
  and the `guis/savecompat/` revisions, and `savegame_restore_matrix.py`, an opt-in
  runtime restore check whose `--self-test` runs in CI.

Candidate save release wording: Older saves taken during a boss fight now load
with the boss's health bar on screen.

- [ ] Stop medics and techs waiting forever for a player they cannot reach.
  Choosing the player as a patient makes the healer passive and blind to
  enemies, and the approach only ended beside a visible patient, so a player it
  could see but not reach held it in place indefinitely. (#184's frozen Tech Li
  turned out to be a Turbo-mode script race, fixed separately.)
  `ai_medicApproachTimeout` (15 s, 0 disables) now abandons an approach that
  makes no progress and holds back automatic heals for as long again; a heal the
  player asks for is unaffected. No save-format change.
  `ai_medic_approach_watchdog.py` keeps SP and MP aligned.

Candidate AI release wording: Medics and techs no longer freeze in place when
they cannot reach you to heal or repair you; after a short wait they carry on
with their orders.

- [x] Add one managed audio-payload budget across CPU/OpenAL data, decoder arenas,
  streams and envelopes. Bound ordinary PCM fallback queues, decode RoQ through
  fixed input scratch and keep pressure-refused samples retryable. Stock and
  Awakening pass at 192 MiB; forced exhaustion and two restarts retain active
  playback without reservation growth. Provider/process metadata, Android
  low-memory callbacks and broader performance/device qualification remain AUD-03.
  [Memory evidence](audio-voice-evidence.md#aggregate-payload-admission).

- [x] Add playback-driven dialogue subtitles using text in installed lip-sync
  declarations, with localized persistent size, background and speaker controls.
  Stock/Awakening runtime, missing-text, overlap, pause, time-scale, restart,
  save/restore and locale-fallback tests pass. The caption panel works in Classic
  and retained UI. Missing transcripts, portrait title-card collisions and full
  scene/headset coverage remain AUD-07/UI-02/UI-04 qualification work.
  [Subtitle evidence](audio-voice-evidence.md#dialogue-subtitles).

- [x] Route mono global/radio dialogue and explicitly centred sounds to the
  physical centre on supported surround outputs, with bounded buffers and
  unchanged stereo/HRTF fallback. Production queue tests, real OpenAL loopback
  PCM on 5.1/6.1/7.1 and quad layouts, and stock compressed/Awakening PCM
  gameplay pass; physical speaker and
  output-layout-transition qualification remains open under AUD-04.

- [x] Add independent archived dialogue volume with twelve localized Audio-menu
  labels, bound transient Vorbis input/PCM admission and restore authored
  minimum distance for mod room rolloff.
  Windows stock/Awakening gameplay and Turkish-to-English audio fallback checks
  cover gain isolation, mute, pause, time scale, restart and save/load. OpenAL
  loopback confirms the mod wet-gain correction without changing stock zero
  rolloff or dry gain. [Audio evidence and explicit limits](audio-voice-evidence.md)
  distinguish these repairs from open AUD-01–AUD-07 qualification. Curated
  candidate notes: [audio refinements](releases/audio-refinements.md).

- [x] Implement bounded multiplayer voice with separate opt-in microphone access,
  push-to-talk, local-only testing, receive volume, connection-scoped mute and
  server moderation. Windows listen and dedicated peer tests cover rendered
  audio, loss, reorder, latency, congestion, restart and slot reuse. Gameplay
  snapshots retain priority over voice. Admission follows slow sender clock
  drift within a bounded slew, verified by a simulated 24-hour drift/jitter/loss
  test. This closes the implementation slice, not AUD-05's physical-device,
  independent-machine clock-drift or target-OS qualification. The saved October
  22 local session reports 1:59:29 with 120 passing intervals; the old harness's
  one-interval duration tolerance prevents claiming a full two-hour pass.
  The endpoint check is corrected without repeating the long test.
  Engine/game API 53 requires matching modules. See
  [voice evidence](audio-voice-evidence.md#multiplayer-voice).

- [x] Record explicit two-way refusal between published 0.13.0 and the current
  candidate, with pure checking both enabled and disabled. This is rejection
  evidence, not a successful mixed-version voice session. The networking guide
  recommends matching releases and content; protocol-capability checks remain
  covered by regression tests. [Compatibility evidence](audio-voice-evidence.md#mixed-release-voice-check-22-october-2026).

- [x] Stream long Ogg dialogue/music and effects through bounded VFS input,
  decoder and playback buffers. Retain exact seek, leadin/loop, pause, pitch,
  restart and save/restore behavior; recover underruns at the channel clock.
  Windows stock/Awakening tests pass at a 16 MiB decode budget, reducing uploaded
  PCM in the measured scenes. The aggregate/non-Vorbis implementation is recorded
  above; performance and Android/device qualification remain AUD-03 work.
  [Streaming evidence](audio-voice-evidence.md#compressed-streaming).
  On 22 October the bounded queues were deepened from three 4,096-frame buffers
  to about one second within the same reserved bytes. Centre-routed dialogue
  uses a fixed pool of deep slots. A 450 ms hitch had been enough to interrupt
  them. [Queue depth evidence](audio-voice-evidence.md#queue-depth-across-long-frames-22-october-2026).

- [x] Add an Android setup Interface page with Desk, Couch, Handheld, Phone and
  Tablet viewing profiles, UI size (75–200%) and text size (100–200%, following
  the Android font size on phones and tablets). Each Apply sets the game's
  retained UI and text scale once, at the next launch or return, so later
  in-game changes are kept. The [interface setup check](platform-recovery-review.md#android-interface-setup-21-october-2026)
  passes JVM/JNI, native, mutation and emulator Apply-and-return checks.
  Physical S25 qualification remains open under AND-03.

- [x] Keep stock crosshair hit feedback for fresh SP/MP profiles by default.
  Saved marker opt-ins remain intact. Four
  [actual-map default/persistence/reset checks](ui/remastered-hud.md#hit-feedback-defaults-21-october-2026)
  and diagnostic engine captures pass. Optional marker artwork/timing and wider
  UI-05 qualification remain open.

- [x] Reconcile the UI register with the existing Awakening single-player scope.
  [Revision 19](ui/product-requirements.md#revision-19-awakening-single-player-scope)
  preserves historical rows and applicable SP/ordinary-MP obligations, without
  requiring an expansion MP module or accepting any GUI/artwork.

- [x] Stop multiplayer bots launching into each other from opposite jump pads,
  keep them from sliding onto pads their route did not choose, check chained
  pads against their health, and let a bot thrown by a rocket or rail fight
  its fall with air control. The [bot evidence](mp-bots.md#pad-corridors-unplanned-pads-and-knocked-falls-22-october-2026)
  covers native Windows/Linux regressions, isolated replays and full matches.
  World deaths after enemy impulses are now classified by replay under the
  MP-02 policy; the remaining navigation deaths keep MP-02 open.

- [x] Account for repeated force-brush contact when predicting jump-pad landing
  damage, and recheck low-health approaches before their final walking corner.
  [Original-map and native evidence](mp-bots.md#repeated-pad-contact-and-landing-health-21-october-2026)
  covers the underpredicted impact and late health check. Unintended entries,
  combat impulses and the failing strict map/catalog gate keep MP-02 open.

- [x] Reserve occupied jump-pad corridors before the final approach corner,
  giving bots time to brake across nearby walking corners. The
  [CTF5 diagnosis](mp-bots.md#early-pad-reservation-21-october-2026) and native
  Windows/Linux regression checks cover the late reservation. Full-duration
  gameplay retains separate landing/rocket-fall failures; MP-02 stays open.

- [x] Add Android touch setup with Default, Compact and Left-handed presets,
  button size, look sensitivity and a live drag test. Applied preferences reach
  the engine at startup/return; drafts and failed writes preserve the previous
  settings. The [bounded controls check](platform-recovery-review.md#android-controls-setup-21-october-2026)
  passes Windows/Linux native tests, JVM recovery and emulator returns. Physical
  setup/controller qualification and the full layout editor remain open.

- [x] Reopen Android setup from the game's Settings and return to the same
  running session. Return to game is also available on the language page.
  The [setup return check](platform-recovery-review.md#android-settings-return-21-october-2026)
  passes two emulator handoffs, a desktop visibility control and native/JVM
  recovery tests. Physical navigation and broader onboarding remain open.

- [x] Add genuine published-Windows-writer evidence for the historical readers.
  Twenty original committed slots from 0.6.95/0.8.0/0.10.0/0.11.0 and five
  completed v0.13.0 staged payloads pass current restore/resave/reload across
  five maps. The [corpus record](savegame-reliability.md#original-release-writers-21-october-2026)
  retains v0.13.0's self-rejected transactions, exact identities and failed
  harness assessments. Current #123 GUI/map compatibility and other-ABI
  qualification remain open; no save-format or support policy changed.

- [x] Reject voluntary bot hops that touch a platform but carry too much momentum
  to remain on it. The [q4xdm13 replay](mp-bots.md#dodge-landing-runout-21-october-2026)
  now slows before taking a supported hop. Windows/Linux fixtures preserve safe
  wider landings; negative controls and the isolated original-map replay pass.
  The renewed catalog's remaining failures and wider MP-02 gates stay open.

- [x] Scope Windows installation shutdown to the current checkout's staged
  runtime. Installing one worktree previously stopped matching games in other
  worktrees or installations. Six production-function cases and three failing
  regression controls cover process/path ownership and shutdown races; normal
  Windows staging validation executes them. The
  [interference evidence](mp-bots.md#installation-interference-21-october-2026)
  preserves the interrupted gameplay runs and their remaining safety failures.

- [x] Check both the predicted jump-pad touchdown and room to slow down, and
  brake unsafe momentum before an exhausted route's idle wait. The recorded
  [DM8 post-hit flight](mp-bots.md#jump-pad-impulse-recovery-21-october-2026)
  now lands and survives on the isolated replay. Native Windows/Linux cases and
  negative controls cover unsupported touchdown and insufficient runout. The
  intermediate failed fix and remaining MP-02 qualification are preserved.

- [x] Start live bot jump predictions at the actual collision origin. The old
  one-unit lift could miss a valid air step and approve an overshooting jump.
  The [isolated DM8 replay](mp-bots.md#live-jump-origin-correction-21-october-2026)
  now completes safely; Windows/Linux fixtures preserve broad landings and fail
  when the old offset is restored. Wider MP-02 safety qualification remains open.

- [x] Correct the support-box/full-player height mismatch that made bot edge
  braking reject valid slopes. Windows/Linux production-function fixtures cover
  box and cylindrical hulls, low ceilings and hazards; isolated regressions reject
  either old implementation. Original CTF6 surface checks and a 60-second bot run
  pass their scoped checks. The [bot evidence](mp-bots.md#sloped-floor-support-correction-21-october-2026)
  preserves remaining route stalls and failing DM8/CTF5 safety reruns. MP-02 and
  controlled performance qualification remain open.

- [x] Add read-only authored-distance/OpenAL diagnostics and measure three stock
  looping sounds through real Hangar 1 emitters. All 54 linear/quadratic samples
  pass on the recorded Windows build, preserving map-unit ranges, source radii
  and the existing mix. The [distance audit](audio-modernization.md#current-distance-audit-20-october-2026)
  retains exact evidence, the failed initial harness calibration and eight
  failure-detection tests. Human audibility remains pending under AUD-01;
  no audio defaults or support claims changed.

- [x] Recover a separate x86_64 Android diagnostic app with coordinated native
  metadata, exact package verification and disabled release variants. The ARM64
  phone packaging regression remains passing. A current 16 KiB Android emulator
  passes silent Hangar 1 touch/save/load and Q4DM1 movement/release checks,
  restores settings and exits normally. The
  [recovery record](platform-recovery-review.md#android-diagnostic-emulator-recovery-20-october-2026)
  retains exact identities, failed older-image attempts and the observed touch
  layout defects; software-rendered results do not promote physical-device support.

- [x] Recover the standalone Android gameplay controls against current input
  ownership. Touch movement/look bypass physical mouse/controller preferences,
  short taps survive game-tick boundaries, wheel steering preserves the view,
  and menu/focus/device changes release held actions. Fixed enlarged-control
  placement, physical target spacing and invalid-density handling.
  [Recovery evidence](platform-recovery-review.md#android-gameplay-touch-recovery-20-october-2026)
  records Windows/Linux behavior checks, both native builds, exact APK checks
  and same-debug-certificate S25 updates preserving all six original save files.
  Scripted movement, weapon selection and save round trips pass; the final
  engine capture confirms an unobscured weapon wheel. Physical move/look/fire,
  release, tap/hold selection and audible background/pause/resume pass on the
  recorded S25 build. Layout editing/portrait presentation, other devices and
  guided controls setup remain AND-01/AND-03 acceptance work.
  A later emulator-qualified increment keeps the visible button corners inside
  their hit targets, clears Pause from the MP rank display and lifts default
  Reload above the status readouts. Native Windows/Linux tests and fresh SP/MP
  captures pass; it has not replaced the physically checked S25 build.

- [x] Qualify the previously blocked original-save restore on the S25's recorded
  timer/GUI-repair APK. Storage 1 loads, creates another save, reloads it and
  exits cleanly, with both views captured by the engine and the original bytes
  preserved through the following update. The
  [device record](platform-recovery-review.md#s25-original-save-restore-round-trip-20-october-2026)
  limits this result to that opening sequence and package. The later export
  APK also passes the [exported-save round trip](platform-recovery-review.md#s25-exported-save-restore-20-october-2026);
  broader campaign and audio/lifecycle gates remain open.

- [x] Add Android saved-game backup and diagnostic-log export to setup. The
  system picker creates a new ZIP destination; bounded private snapshots keep
  saves, descriptions and previews intact and include an integrity inventory.
  Setup failures are recorded in the exported log folder. Cancellation/retry
  preserves the originals and warns that a failed destination may be partial.
  [Export evidence](platform-recovery-review.md#android-save-and-log-export-20-october-2026)
  records production-Java snapshot/provider checks on Windows/Linux, four
  localized resource sets and verified APK packaging. Actual S25 exports to
  Downloads pass independent ZIP/manifest checks, preserve the six original
  save files and supply a save that restores, saves and reloads on the same
  package. Other providers and release-upgrade acceptance remain open under
  AND-03.

- [x] Connect Android language/data setup before game startup. The system folder
  picker supplies read-only source access; verified imports, bundled-file
  preparation, cancellation and retry report progress and recovery guidance in
  English, German, Spanish and French. Play remains explicit after completion.
  Native storage ownership excludes setup during active or closing gameplay.
  [Setup evidence](platform-recovery-review.md#android-language-and-data-setup-19-october-2026)
  retains host pipeline/ownership checks, exact packaging and the separate SDL
  lint findings. Controls/interface steps, Settings re-entry and
  physical qualification remain open under AND-01/AND-03.

- [x] Prepare resumable Android retail import behind the setup flow. Verified
  temporary files replace destinations only after complete validation; failed
  or interrupted operations preserve installed data and private saves. A bounded
  document-provider adapter reads the selected tree and closes active resources
  on cancellation. [Import evidence](platform-recovery-review.md#android-document-import-backend-19-october-2026)
  records Windows/Linux recovery fixtures and SDK compilation. The launcher,
  persisted grants and localized setup still need integration; no player-facing
  import entry point is enabled by this checkpoint.

- [x] Prepare a read-only Android retail-data verifier for guided setup. It
  checks all required archives against the engine catalogue, verifies entry
  payloads and distinguishes optional/language warnings from blocking errors.
  The [backend evidence](platform-recovery-review.md#android-retail-verifier-backend-19-october-2026)
  covers native/Java checksum agreement, corruption, limits, cancellation/retry
  and the local retail set. Original checksum notices are included and required
  by package verification. This is implementation groundwork: the Activity does
  not invoke it yet, and AND-03's player-facing setup remains open.

- [x] Keep Android's game surface clear of camera cutouts and retain the correct
  remaining system/keyboard insets. Density and current SDL configuration changes
  keep the native Activity session. The
  [recovery evidence](platform-recovery-review.md#android-cutout-and-configuration-recovery-19-october-2026)
  records Windows/Linux production Activity tests, three failing regression
  controls and verified debug packaging. Real rotation, density, visual and
  input acceptance remains open; portrait play is still pending.

- [x] Restore three existing native-input SDK tests to Meson registration. An
  obsolete SDL 3.4.10 source path omitted them from builds using the current
  pinned dependency. Registration now follows the SDL wrap; the Windows setup
  lists 270 tests and all three restored tests pass with MSVC, separately from
  the preceding 267-test suite. No native-input feature is enabled by this fix.
  Evidence: `.tmp/stage-two/native-input-registration-evidence.json`, SHA-256
  `e7ffacfe85eef190d4c3fb7e82e8823eafef9901a4906f6552c4647fd64294d5`.

- [x] Route Android Back through the current menu/pause action and make the
  fatal-error console usable with density-scaled touch targets. Press ownership,
  release activation, cancellation and duplicate-event filtering prevent stale
  touches from activating buttons; fatal presentation leaves the keyboard shut.
  The [input evidence](platform-recovery-review.md#android-back-and-fatal-console-controls-19-october-2026)
  records production native/JVM checks on Windows/Linux, failing old-code
  controls, both platform builds and 267 passing native tests. Physical Android
  gesture/touch and visual qualification remains open.

- [x] Keep Android's early fatal reports beside its regular game log by sharing
  the standalone host's save directory with native startup. The
  [path evidence](platform-recovery-review.md#android-early-diagnostic-path-19-october-2026)
  records Windows/Linux production native and Java checks, preserved legacy
  fallbacks, both builds, five focused native regressions and debug-APK
  verification. Device diagnostics and release log export remain unqualified.

- [x] Reduce Android background timer wakes and bound catch-up after long process
  freezes, including accelerated time, while retaining foreground cadence and
  the desktop timing policy. The [timer evidence](platform-recovery-review.md#android-background-timer-recovery-19-october-2026)
  records Windows/Linux production scheduler/worker checks, failing old-code
  controls, both platform builds, 264 passing native tests and verified debug
  packaging. Physical power, suspension and network-resume checks remain open.

- [x] Suspend Android audio and release held input before SDL parks the game
  loop. Resume live single player behind the pause menu before audio/gameplay;
  preserve existing menu, load and network flows. The
  [lifecycle evidence](platform-recovery-review.md#android-background-audio-and-single-player-resume-19-october-2026)
  covers paired/fallback audio suspension, device retry, repeated events,
  foreign-thread deferral and inactive-key teardown in Windows/Linux production
  fixtures, both platform builds and 262 passing native tests. Physical Android
  suspend, input and listening qualification remains required.

- [x] Preserve optional menu fallback during a save load and reject missing
  serialized GUI definitions at their actual reader. The previous session-wide
  check misidentified an optional Android pause menu as saved state.
  [GUI boundary evidence](savegame-reliability.md#serialized-gui-admission-and-optional-menu-fallback)
  records Windows/Linux production checks, 259 passing native tests, both
  platform builds and GL/Vulkan fallback, rejection and healthy reload.
  Physical Android restore and broader Q4-05/Q4-07 gates remain open.

- [x] Bind retained views and GUI allocation queries to the engine thread at
  startup. Android loads native libraries on a different thread, which had
  prevented the optional UI from loading and interrupted save-load preparation.
  [Thread-binding evidence](platform-recovery-review.md#android-ui-engine-thread-binding-19-october-2026)
  includes the reproduced failure, Windows/Linux production-method checks,
  both platform builds and 257 passing Windows tests. Device restore/UI
  qualification remains pending.

- [x] Keep Android's selected game-data folder when retail files are missing,
  so startup diagnostics retain the correct copy destination. The
  [S25 and path-selection evidence](platform-recovery-review.md#s25-startup-data-path-and-upgrade-check-19-october-2026)
  records the reproduced bug, Windows/Linux production checks and 256 passing
  Windows tests. The separate device restore failure remains open.

- [x] Leave Android's single SDL window available for the game at startup and
  release a surviving game window before creating the fatal-error console.
  [Window ownership evidence](platform-recovery-review.md#android-window-ownership-19-october-2026)
  covers Windows/Linux production-method checks, the ARM64/API 24 cross-build
  and 254 passing Windows tests. Subsequent APK/S25 startup evidence is recorded
  separately; touch/back handling and broader AND-01 acceptance remain open.

- [x] Reject standalone weapon, physics and helper classes that cannot occur in
  a valid saved object table, before any object factory runs. View weapons and
  embedded physics retain their supported paths. The
  [class-admission evidence](savegame-reliability.md#standalone-object-class-admission)
  covers Windows/Linux native checks, 252 passing Windows tests, GL/Vulkan
  recovery and existing v2/v3/v4 save regression. Broader Q4-05/Q4-07 gates remain.

- [x] Reject invalid script-thread ownership during saved-state restoration and
  detach partial owners before failed-load cleanup. The save representation is
  unchanged. [Ownership and recovery evidence](savegame-reliability.md#script-thread-ownership-during-restore)
  covers Windows/Linux native failure controls, the 250-test Windows suite and
  six actual CRC-valid failure/reload cases on each of GL and Vulkan. Broader
  Q4-05 qualification remains open.

- [x] Validate saved door/platform activation chains and detach partial links
  before failed-load teardown. Standalone roots and forward references retain
  their behavior. [Mover-chain evidence](savegame-reliability.md#mover-activation-chains-during-restore)
  covers actual teardown methods and CRC-valid Storage1 door fixtures; broader
  Q4-05 ownership/factory qualification remains open.

- [x] Validate live saved spawn references against their declared entity/client
  class before publishing them. Interrupted reads preserve the previous pointer;
  expired weak references retain their existing behavior. The
  [template and runtime evidence](savegame-reliability.md#typed-spawn-references-during-restore)
  identifies the previous acceptance failure and the tested scope.

- [x] Reject invalid saved binding/team relationships and detach partial links
  before failed-load cleanup. Valid forward references retain their saved order.
  [Graph validation and teardown evidence](savegame-reliability.md#binding-graphs-during-failed-restores)
  preserves the previous staged cleanup failure and the focused qualification
  scope. Other owning graphs remain open under Q4-05.

- [x] Keep saved model/light indices separate from current renderer ownership
  during restore. Interrupted fields cannot leave an invalid or foreign handle
  for entity/effect cleanup to free. The
  [production-reader fault tests](savegame-reliability.md#renderer-ownership-during-interrupted-field-reads)
  cover nine paths in both game modules while retaining the save format.

- [x] Recover from semantic save-load errors through the Load Game page or classic
  dialog, after unwinding temporary restore objects and closing the failed map.
  Localized guidance lets players choose another save or start again. Client
  entity allocation also refuses a full table without reading past its end.
  [Recovery behavior and qualification limits](savegame-reliability.md#recovery-after-semantic-restore-failure)
  retain the initial UI-routing failure and distinguish native policy tests from
  actual CRC-valid corrupted-save runs. Broader Q4-05/Q4-07 work remains open.

- [x] Track temporary restore ownership through map teardown, release remaining
  objects while their dependencies are alive and avoid revisiting attachments
  deleted by another owner. [Lifetime and gameplay evidence](savegame-reliability.md#ownership-during-an-interrupted-restore)
  covers SP/MP native cycles and a current save's valid/rejected/valid sequence
  without modifying either file. The subsequent recovery UI is recorded above;
  deeper corrupted graphs remain open under Q4-05.

- [x] Preserve live entity identities when rejecting invalid or duplicate saved
  registrations, including interrupted master-table entries. Constructor-created
  client effects still restore correctly. [Production-reader and gameplay evidence](savegame-reliability.md#restore-identity-and-partial-registration)
  includes Windows/Linux corruption cases, the retained failed candidate and a
  corrected four-role stock gameplay baseline. Subsequent ownership and recovery
  changes are recorded above; broader Q4-05 qualification remains open.

- [x] Verify Android's extracted APK content on every launch and repair damaged
  copies before starting the engine. Only verified replacements are committed;
  failed repairs preserve saves and the previous package. The signed-APK verifier
  checks the new content manifest. [Production-Java and package evidence](platform-recovery-review.md#android-extracted-package-repair-18-october-2026)
  covers Windows/Linux fault cases and selective repair of the actual staged
  package. Onboarding UI and device upgrade qualification remain open.

- [x] Recover safely from OpenXR frame/session failures, retire controller state
  and stop using lost images. Session failures retry after a short desktop
  fallback; runtime loss waits for `vr_restart`. VR screenshots now include the
  world in both game modes. [Current OpenGL evidence](platform-recovery-review.md#opengl-xr-lifecycle-closure-18-october-2026)
  records failing controls, 19 Windows/Linux ownership cases, 28 simulated XR
  gameplay runs and 207 passing native tests. Physical headset qualification
  and integration of other backends remain separate.

- [x] Produce identical bundled content packages on Windows and Linux, preventing
  host ZIP metadata from invalidating an otherwise identical package. The real
  writer regression passes on both hosts and rejects the old behavior.
  [Windows/Linux IPv4 and IPv6 pure-peer gameplay](retail-baseline-ledger.md#cross-platform-pure-peers-18-october-2026)
  passes with matching packages; broader MP-08 qualification remains open.

- [x] Freeze Arena's game audio clock with its paused match while retaining menu
  audio, and resume only the pause it owns. Direct voice-over channels now pause
  with the world too. [Production-method and gameplay evidence](arena-campaign-ceremony.md#current-pause-and-result-evidence-18-october-2026)
  covers repeated GL/Vulkan pause/resume, victory/defeat/draw, restart persistence
  and preservation of a better result. A subsequent
  [headphone listening check](arena-campaign-ceremony.md#headphone-pause-and-resume-19-october-2026)
  confirms audible pause/resume twice. Physical controls, endpoint loss/return
  and the rest of MP-04 remain open.

- [x] Implement retained generated RGBA8 images with premultiplied blending,
  bounded shared storage and deferred release. Explicit 2D images keep their
  type even at 1:6 aspect ratios. The [runtime evidence](ui/runtime-spike.md#generated-image-integration-18-october-2026)
  records native ownership/failure/cycle tests and passing GL/Vulkan gameplay
  alpha/reopen/renderer-restart captures. UI-09 and broader product acceptance
  remain open.

- [x] Preserve queued commands and module-reload requests when command lists
  grow. This fixes a command-line multiplayer connection that silently stayed
  at the main menu. Production copy/list tests reject the previous ownership
  error, all 185 native tests pass, and the complete stock SP/demo/listen/pure-
  client baseline passes. [Evidence and limits](retail-baseline-ledger.md#stage-two-connection-regression)
  belong to the ongoing MP-08/Q4-02 work; Stage 2 remains in progress.

- [x] Complete the stage-one baseline implementation and audit: use a shared
  high-resolution presentation phase for each SP/MP draw and precise OpenXR
  smooth-turn deltas; restore imported game regression coverage; reconcile
  retail/base/Awakening UI source identities without granting migration
  acceptance; correct the networking/audio default documentation. Exact
  qualification limits and retained failures are in the
  [baseline evidence record](baseline-evidence.md).
  Implementation/tested source: `7674acef91c2653e8c6153ae11e7b158c05e52ff`;
  184/184 Meson tests, Linux native probes, source-governance rejection controls,
  twelve final map/clock runs and two original-content controls pass within their
  stated scope. The roadmap closes Stage 1 only; the reproduced connecting-client
  timeout and later gameplay/device/UI acceptance remain open. Curated candidate
  notes: [baseline refinements](releases/baseline-refinements.md).

Candidate release wording: Motion between game ticks now uses a more precise
clock, and camera, world and material presentation share one sample per draw.
Simulation and network timing are unchanged. The multiplayer guide now correctly
describes enhanced prediction and lag compensation as opt-in settings.

- [x] Add filters to the modern server browser. A row under the server list
  narrows it by game type and hides full servers, empty ones or those with
  a password, each a tag that lights while set and clears with a second
  press. The list filters every server itself, so the browser now asks the
  master server for every server and a changed filter never needs a new
  list. The gate test, the native page checks, the browser contract and a
  loopback capture pass.

Candidate UI release wording: The server browser can now show only one game
type and hide full, empty or password-protected servers, from a row of
filters under the list that apply at once.

- [x] Build the multiplayer Escape card's Admin page and make the card the
  default in-match menu. Admin asks for the server's remote console
  password in a masked field (a listen server's host skips it), then offers
  Players (kick, ban, force team switch and the ban list, each saying why
  it cannot act), Server (the rules drafted as the Vote page drafts a call,
  and Apply changes) and Console. The Settings pages' name and clan and the
  Match page's referee password are now the card's own text fields, so no
  page hands off to the classic menu and `ui_retained` alone brings the
  card. The multiplayer smokes that drive the stock in-match menu pin
  `ui_retained 0`. The gate test, the native card checks and listen-server
  and remote-client captures pass.

Candidate UI release wording: The modern multiplayer menu now covers every
in-match page, including server administration, so it replaces the classic
in-match menu whenever the modern interface is on. Set your name and clan,
sign in as referee and run the server's remote console without leaving it.

- [x] Give the Strogg pause its own Save Game, Load Game and Restart Level
  pages. After Kane's stroggification those three still opened the classic
  pages; they now open the modern pages in the Strogg family (shouldered
  plates, slanted markers, R_Strogg labels and the pause's orange), parsed
  on the job workers once the level's player is Strogg. A Strogg page that
  cannot load opens the classic page, never the Marine one. The gate test,
  the native page and pointer checks, and OpenGL and Vulkan captures from
  game/recomp pass.

Candidate UI release wording: After Kane's stroggification, saving, loading
and restarting from the pause menu stay in the modern Strogg menu instead of
dropping back to the classic pages.

- [x] Load saves from earlier releases again. Every older save was listed on the
  Load Game page, but most could not be loaded. Four causes, each fixed:
  releases up to 0.13.0 stamped their saves build 1, so 0.10.0–0.13.0 saves were
  refused by the build floor or misread (they are now recognised by their source
  snapshot and read as their release builds, and the floor drops to 640, where v3
  began); an idlib fix on 2026-10-14 (`f15a41f03`, build 1582) grew the raw joint
  modifier and GUI transition records, so 0.14.000 and every earlier save failed
  mid-restore after the map was gone (both readers now decode the earlier
  records); the 0.14.000 HUD edit changed `hud.gui`/`hud_strogg.gui`'s saved
  structure (their earlier revisions are kept under `guis/savecompat/` and old HUD
  records are read through them, carrying their state over); and saves before
  2026-09-01 could hold a non-finite GUI value that the reader rejected (it is now
  zeroed, as the writer does). Also: a stamped save from build 1262 no longer takes
  the retail 1.0 `idShaking` path. Verified on Windows x64 with genuine 0.14.000
  saves from the rebuilt release, the 0.12.0 release saves on hand, 0.10.0/0.11.0/0.13.0
  equivalents and 33 development saves, base game and The Awakening, all into
  gameplay. Version 2 saves (0.6.95–0.9.0) load again too: v3's liquid state is
  no longer read from them, and ten saves from a rebuilt 0.9.0 load. Unstamped
  saves (0.6.92 and earlier) are still refused before teardown: their script
  program and sound records changed in ways a release save cannot identify.
  Record:
  [compatibility policy](savegame-compatibility-policy.md#releases-that-stamped-build-1),
  [reliability evidence](savegame-reliability.md#validation-evidence-and-limits).

Candidate release wording: Saves from earlier openQ4 releases load again.
Saves made with 0.6.95 through 0.14.000 are listed on the Load Game page as
before and now load straight back into the level, instead of failing part way
through. A HUD restored from an older save redraws from scratch; everything else
picks up where you left off. Saves from 0.6.92 and earlier still cannot be
loaded.

- [x] Finish The Awakening's mannable Strogg chaingun turret (m01 part 1, m04).
  The alpha shipped its model untextured (grey ambient-occlusion bakes, a black
  specular map, a placeholder drum), so it rendered as clay, and it fired the
  hand-held machine gun's round. openQ4 now gives it its own Strogg skin
  (`materials/00_awakening_turret_openq4.mtr`, maps generated by
  `tools/assets/awakening_turret/`, keeping the expansion's normal maps; a
  second pass added lighter plates, worn edgings and distinct hoses and wiring) and,
  through a q4xbase-only `<name>_q4x_openq4` definition lookup, a hotter muzzle
  flash with a flash light, orange tracers, the walker's heavier impacts, ejected
  casings (the vehicle `EjectBrass` path never fired before) and smoke after a long
  burst; damage and rate of fire are unchanged. Verified headless on m04 and m01
  and on a stock map; `awakening_turret_skin_contract.py` pins it. Record:
  [Awakening plan, Phase 6](archive/2026-10-18/plans/q4x-awakening.md).

Candidate release wording: The Awakening's Strogg chaingun turret is finished: it
was never textured in the alpha, and now wears a battle-worn Strogg skin with
glowing vents, a sensor lens, worn steel edging and braided hoses, and fires with
a proper muzzle flash, tracers, ejected casings and barrel smoke.

- [x] Re-derive a texture that changed inside a pk4. The generated image cache
  checked a source's timestamp, and every pk4 member reports 0, so a texture
  openQ4 (or a mod) changed under the same name in a pak kept loading the old
  cached image until `generated/images` was deleted. The cache now compares a
  member's zip CRC-32 and length, read from the pak index without opening the
  member; loose files still use their modification time. Caches written by
  earlier builds miss once and are rewritten. Verified on a fresh save path on
  Air Defense 1 with a test pak: red, then green under the same name (a cache
  miss, re-derived, GPU readback green), then a hit. A cache stamped by an older build
  rewrote 91 entries, about 1.5 s once. Warm loads hit every pak image.
  `generated_image_pak_content_stamp.py` pins it. Record:
  [generated image source identity](loading-cache-modernization.md#generated-image-source-identity).

Candidate release wording: Updated textures now show up straight away. A
texture that changes inside an openQ4 or mod pak is rebuilt on the next load
instead of the old cached copy being reused, so you no longer need to clear
`generated/images` by hand. The first map load after updating rebuilds those
cached images once.

- [x] Clear the HUD from the single-player Arena's third-person shots. The
  introductions, the entrance, the spawn-in and the result ceremony now keep
  only their letterbox, their cards and the centre notice on both HUD
  layouts: the game publishes `arena_hudHidden`, the classic `mphud.gui`
  hides every other window under one `oq4_match_hud` container (its panels
  still reach into the wide-screen margins), the Remastered document gates
  every group but its Arena layer and the main notice, and the overlay HUD
  and the classic passive chat panel wait for the shot to end. OpenGL Arena
  runs on both layouts show the intro, spawn-in and victory shots bare and
  the live HUD back once control returns; the Arena campaign contract pins
  each part.

Candidate UI release wording: Arena introductions and victory shots are now
clean: the HUD, scores and chat stay hidden until the match is yours to
play, on both HUD layouts.

- [x] Give the classic menus the new menus' four Single Player levels:
  Single Player, CAMPAIGN (Quake 4 and The Awakening as cards, with why The
  Awakening is unavailable when it is) and a campaign page with its
  difficulty, Turbo Mode, START GAME and CHAPTERS. Back climbs one level at
  a time; the old route left CAMPAIGN's difficulty on the main menu's New
  Game page, whose Back skipped CAMPAIGN. Both campaign pages share one set
  of verbs (`Session_HandleCampaignPageRequest`). A no-input OpenGL walk of
  every level passes, as do the UI gate, the event delivery check and the
  campaign contracts.

- [x] Add chapter selection to both campaigns, in both menus. CHAPTERS lists
  every level in campaign order (31 for Quake 4, the storage and processing
  facilities once per visit; 13 for The Awakening) with its loading-screen
  name and levelshot. A level unlocks once reached: every level start
  records it in the campaign's archived `ui_chaptersQuake4` or
  `ui_chaptersAwakening`, and an existing start autosave counts, so earlier
  progress shows and deleting saves keeps it. A chapter starts from its
  level's start autosave; without one it starts fresh at the page's
  difficulty with the map's test arsenal and writes that autosave. Locked
  rows are disabled (retained) or gray and refused (classic).
  `campaignChapters` and `campaignChapter <n>` list and start them, and
  `openq4_guiSet gui::<key>` now sets GUI state for stock-list probes. The
  no-input walk covered a fresh start (hangar1 with its arsenal, its
  autosave written), the same chapter from that autosave, a locked pick, and
  the retained view's focus, pick and Back; the text fits every language.

Candidate UI release wording: Pick up either campaign at any level you have
reached: the new Chapters list on each campaign's page starts a level from
its own autosave, and unlocks stay even if you delete your saves. The
classic menus now have the same Single Player pages as the new ones, and
Back steps back one page at a time.

- [x] Load the modern interface's documents faster and lighter, and load a
  level's gameplay documents with the level. Each loaded document no longer
  keeps its parsed JSON tree (10 to 20 times the source: about 135 MB for
  the title's pages alone, 100 to 200 MB more per level); the validator
  builds diagnostic pointers only on failure, reserves its vectors and reads
  names in place; and a JsonCpp patch decodes numbers with `from_chars`.
  Document loads are 1.2 to 1.5 times faster. The Remastered HUD, objectives
  and vehicle HUDs, the weapon wheel and the quick chat now load inside the
  level load, from parses the job system's workers make as the map change
  begins, instead of on the first gameplay frame or their first opening.
  The frame after a single-player load fell from about 640 to under 100 ms,
  the weapon wheel's first opening from about 160 to 50 ms, and the quick
  chat's from about 160 to 40 ms. The native document, HUD and screen
  suites, the UI gate and engine probes on OpenGL pass. Record:
  `docs/dev/ui/runtime-performance.md`, "Document loads and level prefetch".

Candidate UI release wording: The modern interface loads its screens faster
and uses far less memory, and the HUD, weapon wheel and quick chat no longer
stall the first frame after a level load or their first opening.

- [x] Build the modern interface's views faster and lighter, and finish the
  validator's allocation work. The first presentation pass no longer parses
  every property a second time through RmlUi (the derived markup already
  carried it) nor keeps a map of 34 000 applied-CSS keys; the motion builds
  its authored values in key order and shares them with its live values
  until the first write; the state, the validator's own state and the
  control views reference the loaded document's model instead of copying its
  declarations, bindings, controls and property maps; and the validator reads
  each typed value and path command in one walk of its fields and skips the
  tree walk for non-finite numbers when the lexical scan shows none can
  exist, and indexes the timelines a control's five states name. A page's
  view builds 38 to 48 percent faster (the Escape card's from 201 to 112 ms,
  the MP HUD's from 76 to 40 ms) and retains about a third less memory;
  validation is 12 to 28 percent faster. Diagnostics and presentation are
  unchanged: the native document, screen, HUD and runtime suites and the UI
  gate pass. Record: `docs/dev/ui/runtime-performance.md`, "The view build
  and the remaining validation".

Candidate UI release wording: The modern interface's menus and HUD open
faster still and hold less memory.

- [x] Build the modern interface's views from the loaded document instead
  of markup. A canonical load no longer derives 800 KB of markup for RmlUi to
  parse, instance and style: the runtime creates each node's element from
  the model with typed inline properties that are exactly what RmlUi's
  parser produced from the value's CSS (one representative parse per
  property kind and keyword keeps the parser's own definition; numbers are
  the parser's `strtof` of the same digits, colours the same `rgba()`
  integers, transforms the same primitives), shows the document once instead
  of updating it hidden first, and the first presentation pass keys its
  records by the authored map's keys, skips formatting the values the load
  built and reads the bound properties in step. Every retained document in
  the tree (63) was compared element by element against the markup it
  replaces (attributes, each inline property's unit, bits, definition and
  parser, their order, layout boxes, computed values and the serialization)
  and matched exactly. A page's view builds 23 to 38 percent faster (the
  Escape card's from 108 to 68 ms, the MP HUD's from 42 to 29 ms, the Join
  Game page's from 32 to 21 ms) and retains 10 to 20 percent less memory;
  the document load is unchanged. Diagnostics and presentation are
  unchanged: the native document, screen, HUD and runtime suites and the UI
  gate pass. Record: `docs/dev/ui/runtime-performance.md`, "Elements built
  from the model".

Candidate UI release wording: The modern interface's menus and HUD open
faster again.

- [x] Show the Arena best results on the modern Arena page: an unlocked
  match's record across the foot of its levelshot (BEST RESULT over the
  record; generation checks both, and the widest record, in every
  language), NEW BEST on the report's score row, and RESET PROGRESS live
  whenever there is anything to clear. The native Arena and pointer checks
  and the Arena campaign contract pass.

Candidate UI release wording: The modern Arena page shows each match's
best result and marks a new one, as the classic page does.

- [x] Open the campaign difficulty page on the chosen difficulty. It arrived
  with nothing focused, so a controller's first press went to PRIVATE and
  Accept did nothing; like Restart Level's page it now focuses the chosen
  difficulty on each arrival. The native continuity check covers it.

Candidate UI release wording: The campaign difficulty page opens on the
difficulty already chosen, ready for a controller.

- [x] Mirror the Arena audit's match report in the modern Arena page. After
  a match the opponents left or the player abandoned, it offers RETRY and
  RETURN where the match can be played again and CONTINUE otherwise;
  CAMPAIGN COMPLETE replaces the awards only on the win that completed the
  campaign, so later replays keep theirs; award lines wrap; and the audit's
  new texts fit in every language. The report still opens on its safe
  action. Native result checks, the UI gate and OpenGL captures of the
  Arena browser, a start failure and an abandoned match pass.

Candidate UI release wording: The modern Arena page's match report matches
the classic one: a match that ended early can be retried, and a replayed
win shows its awards instead of the campaign-complete banner.

- [x] Give the campaign difficulty page its parents' prompt bar, so the bar
  stays where Single Player's and Campaign's is instead of jumping to the
  title's position on the last step. The native legend-bar check covers the
  page; all nine screens groups pass.

Candidate UI release wording: The campaign difficulty page keeps its button
prompts in the same place as the pages before it.

- [x] Use one prompt style for the HUD and the menus. The merged menus had
  their own `ui_promptStyle`, numbered differently from the HUD's
  `in_promptStyle` (3 was PlayStation in the menus and Nintendo on the HUD).
  The menus now take the family the HUD's prompts use, so `in_promptStyle`
  forces both, and forced Nintendo prompts also confirm on A, as a detected
  Nintendo pad does. `listControllers` shows the style in use, marked when
  forced, and the face layout behind the automatic swap. The UI gate's prompt
  checks and the input, SDL3 and Steam Deck contracts pass. See
  [input routing](ui/input-routing.md).

Candidate UI release wording: If Steam Input or another remapper hides your
controller, `in_promptStyle` now picks the button prompts for the menus and
the HUD alike, and choosing Nintendo prompts also makes A confirm in menus.

- [x] Merge the retained page line into main. The title's LOAD GAME, DEMOS,
  CREDITS, MODS and MULTIPLAYER (with its server browser and Create Server),
  Single Player's ARENA, the pause menu's SAVE GAME and RESTART LEVEL, and the
  screen after a death now open modern pages instead of handing off to the
  classic ones; controller prompts follow the device in use; Nintendo layouts
  confirm on A; and the menus' dead ends and stuck input from the 8 October
  audit are fixed. Main's initializing screen, weapon wheels and campaign
  difficulty page replace the line's own versions, and the title keeps its
  `q4text` wordmark. Large retained documents load in linear time on main's
  runtime too. The Windows x64 debugoptimized build, all 177 native
  tests (the screens test now runs as nine groups) and all 342 Python checks
  pass after fixture repairs, and the register increment validates (114
  partial, 232 pending, 1 verified). Windowed OpenGL captures of 35 menu,
  page and gameplay scenarios and Vulkan captures of 18 complete,
  including the airdefense1 and recomp pauses, saving, loading, restarting
  and the death screen. Evidence: `.tmp/ui-merge-20261014/validation.json`.
  See [retained screens](ui/retained-screens.md) and the
  [register increment](ui/product-requirements.md).

Candidate UI release wording: More of the menus use the modern interface:
Load Game and Save Game, Restart Level, Demos, Credits, the MODS list, the
Arena ladder, the screen after you die, and Multiplayer with a rebuilt server
browser and Create Server. Menu prompts show your controller's buttons, and
Nintendo controllers confirm with A (`in_swapConfirmBack` changes that). Menus
open on their main choice, the mouse only lights what it points at, and Back
no longer strands a blank menu. Unchanged menus and screens are kept across
map changes, so loading between maps is shorter. `ui_retained 0` still selects
the classic screens.

- [x] Keep unchanged stock GUI trees across map changes (from the merged page
  line): re-reference their parse-time media inside the load and reset
  authored window state; four interleaved SP/MP pairs cut stock GUI time
  81–92%. See [map changes](ui/runtime-performance.md#map-changes).

- [x] Keep a best result for every Arena Campaign match. The archived
  `ui_arenaBest` records each match's best outcome, score line and difficulty
  (win > draw > loss, then margin, then difficulty, then score). It is repaired
  with the progress mask: malformed, duplicate, locked or inconsistent entries
  are dropped. `FinishPendingCompletion` updates it from contested results only
  (aborted and abandoned matches never touch it). Reset Progress clears it and
  is offered while either record is non-empty. The browser shows the record
  across the foot of the levelshot ("No result yet" for an open match), and
  the match report tags a beaten record **NEW BEST** (`#str_42165`/`#str_42166`
  and a reworded reset prompt in all 12 tables). See
  [the ceremony notes](arena-campaign-ceremony.md#best-results) and
  [candidate player-facing notes](releases/arena-refinements.md).

- [x] Audit and refine the single-player Arena Campaign. Match start: the
  GAMEON edge no longer cancels the spawn-in, so the camera move, the "three,
  two, one" count and "Fight" with control are restored; the warmup
  introduction arms once, films each combatant face-on (the shared camera
  placed every authored angle on the wrong side), has its own HUD mode with a
  held letterbox, introduces teammates before opponents, and its card shows on
  the Remastered HUD. Presentation: no crosshair or status bar over cinematic
  shots; the campaign publishes its match card so Duels read as Duels on the
  entrance, scoreboard and loading card; the board is headed by the match; the
  Remastered champion card shows the victor, who is framed below the result
  lines; a draw is announced as a tie; the tableau orbit follows look input
  for a dead player; the stats beat has no chat entry, Escape line or cursor.
  Play: Arena matches pause behind their menus with every deadline rebased;
  leaving returns to the browser with a "Match abandoned" report and Retry; a
  failed launch's report names its match; Escape or fire skips the ceremony;
  emptied sides, forfeits and aborts end as "Match aborted" instead of a win,
  and the Admin actions are refused. Browser: coalesced timeline commands
  survive `arenaBlockInput`, overlays refuse the controls beneath them, tier
  briefings no longer name locked bosses (12 languages), IN PROGRESS tiers,
  NEXT to the next unwon match, awards after completion, wrapped award lists.
  New cheat `openq4_arenaCeremony` drives a match end to end without input;
  runtime evidence and remaining gaps are in
  [the ceremony notes](arena-campaign-ceremony.md). See
  [candidate player-facing notes](releases/arena-refinements.md).

- [x] Give the single-player Arena match its own pause menu. During a live
  Arena match Escape opens MATCH PAUSED (`guis/arena_pause.gui`,
  `currentMenu` 5) in place of the multiplayer menu, with Resume, Restart
  match, Settings and Leave match beside the match block (tier and role,
  levelshot, game type, map, paused clock, score). With `ui_retained` the
  session covers it with `guis/menu/arena_pause.q4ui` through the multiplayer
  cards' cover protocol. Restart (after a question) runs the new
  `arenaCampaignRestart`, which leaves the match and takes the abandoned
  report's own Retry for the same token: entrance, `PrepareServer`, a fresh
  server, nothing recorded. Leave (after a question) is `disconnect`, so the
  browser shows "Match abandoned" with Retry. Settings opens the main menu's
  settings page over the paused match, and backing out returns to the pause
  menu. The world pause (`ui_arenaMenuOpen`) holds throughout, and Escape
  still skips the ceremony once a result is pending. Strings `#str_42180`-`42186`
  in all 12 tables; spec 13.7 "Arena" (version 1.23). Runtime, staged build,
  hidden window, no device input (`.tmp/arena-audit` scenarios `pausemenu`
  and `restart`, classic and retained): the Escape path opens the menu in
  11-12 ms and the clock holds at 5:53 through the menu and the settings
  page; Settings returns to the pause menu; Leave lands on the "Match
  abandoned" report; Restart replays match 1 from its introduction to
  control. See [the ceremony notes](arena-campaign-ceremony.md#pause-menu).

- [x] Review and refine the multiplayer bots across combat safety, personality,
  tactics, navigation and presentation. Combat: a world or mover hit no longer
  switches off the splash cover tests, the speculative projectile sweep ignores
  water, splash maths carry the shooter's damage powerups and price a direct
  hit at its direct damage, an impact beside a team-mate is refused for the
  knockback even with team damage off, and napalm globs are hazards once at
  rest. Personality: `initiative` bends reaction by a quarter at most and keeps
  a quarter of the aim settle, `itemFocus` weighs pickups against enemies,
  `aimLead`/`aimLeadError` clamp at 1, `pursuit` no longer over-extrapolates a
  lost target, queued chat lines are not overwritten, skill variance reflects
  off the range ends. Tactics: orders that resolve to nothing leave the bot in
  role allocation; Dead Zone reads one holder and the rules clear both slots on
  a hand-over. Navigation: shortcuts refuse pits and pain floor, transport
  goals use the pad's brush, stale-edge validation sweeps recorded movers only,
  the cell hash mixes both coordinates, failed snaps retry after five seconds,
  partial step lifts are accepted, recovery hops are rate limited. Presentation:
  look-ahead view, idle sweep, personality-chosen carried weapon, combat
  footwork no longer silences stuck detection past a light strafe. The twelve
  in-tree bot suites pass. Live evidence: a same-day A/B of HEAD's module against
  the refined one, eight skill-4 bots for two minutes per map from an optimised
  candidate runtime, plus a five-map rerun (DM, DeadZone, Freeze Tag, CTF) of the
  final module; identical navmeshes, no regression in stuck, give-up or no-route
  counts, q4dm5 pickup refusals six to one, and the known q4ctf5 void and vertical
  pad limits unchanged. Numbers are tabled in the bots design record. See [implementation](mp-bots.md) and
  [candidate player-facing notes](releases/mp-bot-refinements.md).
- [x] Fix the two navigation stops that review measured but did not fix, in the
  navmesh: q4ctf1's string-pulled walk corners above a riser or on the side of
  its ramp (the shortcut sweep climbed with the line and cleared the riser's
  top; floor samples now may climb more than a step only across a slope or a
  stair, and a climbing leg is proven from either side of its corner), and
  q4dz1's vertical-pad bounce (the natural landing's node hung half a hull
  over the shaft; it is now the nearest supported node, proven from the pad's
  centre ridden or steered as the bot flies it). The stuck report names the
  leg and the navmesh's verdict on it. Twelve suites pass; the fixture runs the
  production shortcut proof. Live: 120 s, eight bots per map - both stops gone,
  q4ctf1 stuck reports 6 to 3, q4dz1 pad timeouts 2 to 0.
- [x] Fix the two patterns those audits left: rim walks (nodes whose hull
  hangs more than eight units past a platform's or catwalk's edge are rim
  nodes that routes pay a cell more to enter, and a shortcut needs the ground
  a support box's width to either side of its samples) and combat-displaced
  stops (a bot more than a corner's reach beside a walk leg, or a step off its
  height, re-routes as soon as the navmesh refuses the walk from where it
  stands to its corner, instead of waiting for the stuck detector). The
  `navmesh probe` command prints the nodes, links and floor heights around a
  point. Twelve suites pass; the fixture pins the rim line, the rim-node test
  and the stair and wall cases. Live numbers in the bots design record.

- [x] Complete the shared-runtime refinement pass: repair priority-queue order,
  aliased list insertion/growth and repeated bulk-copy work; correct cached and
  zero-duration motion samples; handle bit-message boundary cases and large
  binary deltas; and bound/reuse SP and MP tactical-search storage. Windows x64
  builds, 171 native tests and five Linux sanitizer suites pass. All 340 source
  checks have passing results after fixture/CI repairs and targeted reruns.
  OpenGL/Vulkan SP gameplay, an SP save/load round trip and two-client MP match
  controls pass. Initial timeouts and validation failures remain in the evidence.
  See [implementation and coverage](core-runtime-refinements.md)
  and [candidate player-facing notes](releases/core-refinements.md).

- [x] Qualify the `hub2` map compiler crash fix from
  [#181](https://github.com/themuffinator/openQ4/issues/181). Allocate shared
  vertices by their actual count, retain matching/order behavior, reject failed
  allocations safely, and avoid repeated triangle-list rescans during T-junction
  repair and output assembly. Windows MSVC and Clang native regression tests and
  Linux sanitizer checks pass. The supplied map completes geometry, collision
  and navigation compilation, with a 503-second geometry pass and normal exit.
  Gameplay remains blocked by its oversized surface exceeding the renderer's
  existing index limit; keep #181 open for that follow-up.
  See [compiler storage and regression coverage](dmap-compilation.md).

Candidate release wording: Large, heavily split maps such as `hub2` compile
without crashing on an oversized temporary vertex allocation. Surface sharing
and T-junction list assembly also do less repeated work. Rebuild affected maps
with the updated engine. The supplied `hub2` map still exceeds a separate runtime
surface limit and is not yet qualified for gameplay.

- [x] Re-enable the optimized modern UI by default (`ui_retained 1`). Preserve
  saved player preferences and the `ui_retained 0` classic-interface option.
  The UI gate suite passes. An optimized staged OpenGL gameplay smoke on
  `game/airdefense1` confirms default 1 with a fresh profile and saved 0 after
  restart, without a launch override. Evidence: `.tmp/ui-default-20261013/validation.json`.

- [x] Reduce repeated hidden HUD animation checks without changing animation
  phase, appearance or reveal behavior. Eight optimized native UI suites pass.
  Matched 720p gameplay comparisons in both condition orders add 2.60-4.32%
  frame time in MP/OpenGL and 1.79-2.93% in SP/Vulkan. Fresh engine captures
  qualify animation and Reduced Motion for all three bitmap graph families on
  both renderers. This establishes near-legacy performance in the tested scenes,
  not zero overhead or other-platform qualification. Evidence:
  `.tmp/ui-near-legacy-20261013/REPORT.md`.
  Complementary SP/OpenGL and MP/Vulkan checks also show near parity: first-order
  differences are +2.31% and +1.66%; reversing order gives -4.63% and +0.13% with
  substantial run-to-run variation. All four profiles avoid layout rebuilds.
  No additional runtime change was warranted by these profiles. Evidence:
  `.tmp/ui-cross-api-20261013/REPORT.md`.

Candidate UI release wording: The modern UI is enabled by default for new
profiles; existing saved choices are preserved. It does less work on hidden HUD
animations, improving gameplay performance while preserving its appearance and
animation. ECG and radio/voice graphs retain their legacy bitmap animation.

- [ ] Finish title and pause-menu performance parity alongside gameplay on both
  renderers. The current optimization pass reuses stationary artwork, isolated
  plates and the paused single-player scene, trims the bounded coverage atlas,
  replays settled menu commands and interaction geometry, evaluates moving
  convex masks in a shader, and reduces soft-focus and repeated HUD state work.
  Eleven UI/material suites, five production adapters and OpenGL/Vulkan
  ordinary-pause/restart/resume captures pass. Refreshed matched menu windows
  give comparable 144/240 Hz pacing. Moving-mask and fitted-image fade replay
  further reduce title work; current uncapped title overhead is 1.1% on Vulkan
  and 10–17% on OpenGL across condition orders. Stable pause comparisons remain
  about 40% on Vulkan and 49% on OpenGL; the parity gate stays open. All slow
  windows are retained, including an outlying legacy Vulkan pause window.
  See [measurements and scope](ui/runtime-performance.md#moving-mask-and-crossfade-replay-14-october-2026).

Candidate release wording: The modern title and pause menus reuse more of their
unchanged artwork and paused scene, reducing rendering work while keeping HUD,
title crossfades and menu animation running. Install the complete updated package so the engine,
game modules and UI resources stay together.

- [x] Restore the legacy machinegun scope's scene sampling on OpenGL, feather
  its stepped blur edge on both renderers, and extend the stock message-box
  dimming over wide and tall displays. Preserve retained UI batching and
  soft focus; qualify gameplay captures and live legacy/retained switching.
  OpenGL and Vulkan scope/backdrop captures pass at 1280x720 and 720x1280;
  retained dialogs retain their text and soft focus. The extra capture boundary
  is OpenGL-only: a controlled Vulkan build exposed a font-upload sensitivity
  when its existing ordering changed. Evidence: `.tmp/legacy-scope/validation.json`.

Candidate release wording: Legacy machinegun scopes show the scene correctly
on OpenGL and have a smoother blur boundary. Legacy pop-ups dim the full view
on wide and tall displays. The optional new UI keeps its scope and dialog effects.

- [x] Document how macOS players can obtain retail data from a supported GOG
  offline installer or an existing Windows installation, and select the folder
  containing `q4base`. The guide keeps retail files outside the signed app and
  explains extraction failures without claiming every installer is qualified.
  Addresses the documentation gap in [#185](https://github.com/themuffinator/openQ4/issues/185);
  the reporter's installation confirmation remains open under PLAT-04/REL-01.

## 0.14.000 release — withdrawn

The completed steps below record the original attempt. The release was later
withdrawn because it was broken; 0.13.2 remains the latest available download.

This section records the tagged release, whose default was `ui_retained 0`.
Later source builds restored `ui_retained 1` in commit `1aa2332d9`; see the
[current interface guide](../user/client-settings.md#modern-and-classic-screens).

- [x] Restore the shipping `ui_retained 0` default because retained gameplay
  overhead is still not negligible. Preserve archived player preferences and
  document `seta ui_retained 0` for existing profiles that opted in.
- [x] Include the pending workspace changes in the release, with curated
  [player-facing notes](releases/v0.14.000.md) that explicitly state the UI
  performance limitation and save/module upgrade requirements.
- [x] Add a manual release workflow prerelease selector. Keep the stable latest
  release unchanged when publishing a prerelease, retain asset verification
  before publication, and verify the resulting draft/prerelease state.
- [x] Add an experimental Android ARM64 APK job with persistent release signing,
  matching pinned dependencies, corresponding source downloads and signature,
  manifest, native-library and overlay verification. Keep Android's device
  qualification limits explicit even when the overall release is stable.
- [x] Originally publish 0.14.000 through [Manual Releases](https://github.com/themuffinator/openQ4/actions/runs/38297252865).
  All seven platform jobs and publication passed at the time, and the 16 assets
  were verified. The broken release was subsequently removed. Its APK, legacy
  UI default and other changes are not claims about the available 0.13.2 package.

## Unreleased candidate work previously grouped under 0.13.2

This section accumulated later development changes under an old candidate
heading. It is not the contents of the 14 September 2026 release. See the
[corrected 0.13.2 notes](releases/v0.13.2.md) for that package and
[unreleased notes](releases/unreleased.md) for the later candidate material.

- [x] Restore the actual legacy HUD audiograph and reduce repeated UI work. Radio and
  voice use the installed legacy waveform with its complete height table;
  editable SVG-derived icon meshes remain cached. Pending state deltas,
  presentation-only layout, deferred mask snapshots, cached vector panels,
  direct additive mask drawing and reduced target rebinding lower repeated
  work. Native runtime, screen, all HUD families and generated-source checks
  pass, including hidden-branch reveal and animation continuity. Staged OpenGL
  and Vulkan captures qualify bitmap animation and Reduced Motion. Native
  Marine/Strogg/MP-overlay frame work falls to 0.147/0.258/0.064 ms. Shared cache
  budget and cross-view eviction checks also pass. Evidence:
  `.tmp/ui-legacy-fidelity-perf/REPORT.md`.

- [x] Reuse settled HUD drawing while preserving live bitmap animation and
  editable vector sources. Bounded coverage atlases, presentation replay,
  direct disjoint-glyph opacity and deferred hidden properties reduce repeated
  work. Allocation recovery and immediate legacy fallback are qualified;
  fourteen native suites and the compiled adapter with 17 rejected mutations
  pass. The build wrapper also accepts numeric PowerShell arguments. Follow-up
  captures preserve gauge pixels outside the live ECG and qualify all three
  families' bitmap animation/Reduced Motion on OpenGL and Vulkan. Named image
  uploads preserve samplers; overlapping coverage retains original GPU blending.
  Fading leaf text/images reuse live opacity; overlapping text keeps a bounded
  composed target after an earlier-frame warm-up, avoiding a Vulkan first-draw
  font-upload gap. Six opacity levels match ordinary composition exactly on
  OpenGL and Vulkan, and HUD state forwarding uses case-insensitive hash lookup.
  Detailed retained stopwatches now run only on a profiler request; eight native
  suites pass with geometry parity when timing is disabled.
  Live leaf opacity also avoids redundant CSS formatting. Seven sequential
  opacity samples match ordinary composition on both APIs, including returning
  to an earlier value and crossing fully hidden/visible states (28 captures).
  Gameplay evidence: `.tmp/ui-negligible-overhead/REPORT.md`.

- [ ] Reach negligible new-UI overhead against legacy. The initial isolated 720p
  gameplay comparisons still add 0.60 ms in SP/Vulkan and 1.25 ms in MP/OpenGL;
  the coordinated visible-window retry removes the earlier 500 ms presentation
  stalls and measures smoothed-snapshot differences of +0.343/+0.570 ms.
  These were initially mislabelled as whole-window means; a dedicated arithmetic
  window mean now includes every interval since reset for acceptance runs.
  After background jobs were paused, default-settings arithmetic-window testing
  adds 0.049 ms SP and 0.358 ms MP. The latest build's +0.011 ms MP aggregate
  hides opposing block differences (+0.364/-0.605 ms); a longer-settled repeat
  still adds 0.218 ms (+4.93%). These do not establish negligible overhead.
  Work pauses after this validation checkpoint at the user's request. Follow-up crosshair
  replay, opacity reuse, conditional readbacks and legacy script-only evaluation
  are visually and natively qualified; performance acceptance remains open.
  The improvement above does not complete this requirement. Other platforms,
  the full density matrix and 4K GPU qualification also remain open. See
  [HUD submission measurements](ui/runtime-performance.md#legacy-instruments-and-hud-submission-11-october-2026).

- [x] Qualify the refined death and Air Defense crash screen effects on Windows
  x64 OpenGL/Vulkan. Death
  has one settling focus swell and a delayed inner vignette; `cin_opening`
  follows the original three wake fades and two blackouts with warm focus
  recovery and soft eyelid shading. Both respect reduced motion/flashing and
  retain camera, gameplay and fade deadlines. The focused native suite passes
  956 checks; production timing/cleanup checks pass. Engine captures cover
  SP/MP death, normal/reduced settings, landscape/portrait and the full crash
  sequence through gameplay handoff. The wider UI debug suite's 240-second
  timeout and other-platform qualification remain separate. Validation and captures:
  `.tmp/view-effects-refinement/`; implementation record:
  [retained screens](ui/retained-screens.md).

- [x] Keep initialization animation updating between startup stages with
  bounded drawing cost. Cached rotating artwork, a cooperative 30 Hz ceiling
  and cost-based backoff reduce warmed native frame cost from 13.53 to 0.53 ms
  at 720p. OpenGL/SP and Vulkan/MP gameplay entry, animated engine captures,
  Reduced Motion, native clipping/scale/pacing and loading-reentry checks pass.
  Long indivisible startup operations can still pause presentation. See
  [measurements and limits](ui/runtime-performance.md#initialization-animation-11-october-2026)
  and `.tmp/initialization-animation/` for evidence.

- [x] Qualify centered HUD gauge size, internal placement and background colour
  parity with legacy. The vector fills now include the stock texture luminance,
  soft bevel and translucent armor cells. Native checks pass for all four
  on-foot documents; staged engine captures cover Marine/SP and MP on OpenGL,
  Strogg and portrait/SP on Vulkan, low health, zero armor and 125% scaling.
  A flat Marine fill sample matches legacy red/green exactly and differs by
  one blue-channel level out of 255. Evidence: `.tmp/hud-status-parity/`.

- [x] Reduce Remastered HUD instrument cost and restore visible sound graphs
  ([performance evidence](ui/runtime-performance.md#gameplay-instrument-performance)).
  The approved installed ECG bitmaps preserve scrolling, masks and health
  response; radio and talker displays initially used preset activity traces
  after live audio columns proved costly (superseded by the actual legacy bitmap
  in the follow-up above). Four matched gameplay repeats improve
  UI-on FPS by 33.6–150.1% locally, with substantial host variability and some
  remaining multiplayer UI overhead. Three native suites, generated-document
  checks, decoder/playback checks and OpenGL/Vulkan engine captures pass.
  Captures confine existing-HUD differences to the approved ECG area and show
  visible graphs with Reduced Motion respected. Logs, hashes, timing windows
  and exclusions are in `.tmp/ui-perf-fix-20261011/REPORT.md`. Microphone/network
  voice and other platforms remain outside this qualification.

- [ ] Qualify status-bar TTF parity and original weapon bitmap backdrops in the
  optimized staged client. Four on-foot retained document suites cover the
  legacy layout dispatch, uncapped readouts, gains and bitmap bindings. Engine
  screenshots and HUD timing evidence belong under `.tmp/hud-ttf-parity/`.

- [x] Packaged qualification of the second Awakening m01 dropship pass.
  The across-aisle rescue camera now updates its view axis as well as its map
  target, preserves headroom and holds Darnell's reaching performance. Atkins'
  warning finishes before the fall; Markswell's ship harness follows his sit.
  Tak's repair runs at 0.85 animation rate with two cutting-sound windows while
  the squad exits. Dekard's final camera starts only after the main column clears.
  Scene mix cues bypass room occlusion, fade out at descent and stop at handoff;
  the helper's sound/effect manifest is loaded during map setup. All media still
  comes from the user's installed content. Saves made with a different revision
  of the map script can require restarting m01 (scripts are recompiled on load).
  The input-free `tools/tests/awakening_m01_cinematic.py` supports candidate
  overlays, direct OpenAL Soft wave-file recording, task-owned cache reuse and
  Reduced Flashing. Full original hangar-to-gameplay playback passed at 283.008
  game seconds; final focused framing/audio playback and same-build save/load
  passed, with handoff at 133.552 seconds. Full-map automatic cinematic skip with
  Reduced Flashing also passed. Logs, engine render-target captures, recordings,
  tested binary/content identities and the source freeze are under
  `.tmp/m01-cinematic/pass2-*`. The combined debugoptimized stage passed focused
  playback, audio output and save/load with all eight runtime hashes unchanged;
  packed scene files equal source and their preload warnings are gone.
  Further content work: hand-contact/harness animation, brief passing-body
  occlusion during the early repair shot, and subjective mix/listening review.
  The focused run also records an unexplained opening-bed StopSound after about
  23 seconds; reproduce that in full-path playback before attributing its cause.

- [ ] Qualify Awakening freeze persistence, damage gates, animation speed,
  Retch ownership, cockpit target validity and Goob/Napalm ammo status.
  The implementation uses save format 4 with a bounded v3 decoder; the native
  behavioral checks and save contracts pass. Staged gameplay and old/new save
  validation are recorded in [Awakening status and targeting](awakening-status-targeting.md).

- [x] Improve multiplayer bot navigation and decision-making
  ([Multiplayer bots](mp-bots.md)). Navigation: hazard volumes, long drops,
  proven gap jumps, sealed jump-pad and teleporter volumes, exact reachability,
  ALT landmarks, body-free agent sweeps, off-route recovery and drop
  push-through; path failures under 2% and stuck reports down from 29 to 3-4
  per two minutes on q4dm5. Intelligence: hearing, item timing, patrols,
  fight-or-flight, arming and buying, damage-model splash safety, route-aware
  role allocation and human team orders in team chat with in-character
  acknowledgments. Cost: landmark-ordered goal pricing instead of a field per
  decision, link compaction and node-major landmark rows. Verified with the
  in-tree contract and native fixture suites and `tools/tests/mp_bot_audit.py`
  runs on q4dm5 (DM and Team DM with a follow order).

- [ ] Extend bot landing qualification across shipped maps. The continuation
  now checks actual player hulls, supported touchdown and a walkable return
  to the destination for gap and raised-platform jumps. Airborne steps retain
  their momentum, movement speed remains consistent while looking aside,
  and bots check whether their health and armor can survive a pad or drop
  landing before entering it. Voluntary hops retain their checked movement
  direction through flight; off-mesh recovery checks edges and permits slow
  inward recovery only with continuous hull support. Every fresh routed jump
  checks the actual launch position, momentum and command; objective holds
  wait for landing, and active pads without safe routes still block ordinary
  walking through their launch volume. Twelve native/contract
  suites pass, including reproduced unsafe-platform and narrow-gap cases,
  with broad-platform routes and supported rim recovery retained. The
  combined optimized build completed a four-map diagnostic and all 32 shipped
  MP maps without an engine failure. The 32-map run recorded three measured
  world deaths, no warmup deaths and 306 completed traversals; strict safety
  qualification remains failed. Two deaths followed enemy knockback; one
  followed an unforced WALK fall after a successful jump landing. The separate
  four-map run exposed a favorable-health self-blast that pushed its shooter
  off a ledge. Bounded support/impulse corrections and their final targeted
  qualification are in progress. Persistent approaches on CTF6/7 remain open
  navigation issues. Build, pack and gameplay evidence is retained in
  `.tmp/claude-continuation/bots/` without replacing the earlier failed runs.

- [x] Validate the continued HUD reconstruction in bounded Windows OpenGL
  gameplay ([HUD](ui/remastered-hud.md), [weapon wheels](ui/weapon-wheels.md)).
  Native fixtures cover gauge geometry, isolated ammo gains, notice placement,
  wheel motion/input/fit, compact match context, respawn rules and wrapped
  Tourney instructions. Engine captures qualify both wheel families, Couch
  scale and safe inset, Classic damage/reduced flashing, cold MP medal loading,
  controller vote glyphs, normal respawn holds, full-round Clan Arena clocks,
  actual Tourney sudden-death transitions and continuous Strogg grain.
  Capture reports retain their tested binary/pack identities under
  `.tmp/claude-continuation/hud/`. Physical-device, full-platform and broader
  UI product acceptance remain open in the product register; this checkpoint
  does not complete the voice, touch, subtitle or full HUD corpus work.

- [x] Stop hashing the openQ4 runtime packs at every launch
  ([Runtime pack verification](runtime-pack-verification.md)). Startup
  MD5-hashed `baseoq4/pak1.pk4` (about 800 MB) and `pak0.pk4` each time, about
  1.3 s with a warm disk cache in an optimized build, and again at every
  filesystem restart. A pack that passes is now recorded in
  `generated/packs/verified.txt` under `fs_cachepath` with its size, write and
  change times, volume, file id and zip-directory CRC digest, and an unchanged
  pack is not read again. Anything that edits, replaces, touches or re-stages a
  pack sends it back to hashing, so a missing or modified pack still stops
  startup with the same message; `fs_cacheVerifiedPaks 0` hashes at every
  startup. Interleaved debug-build A/B (5 ABBA pairs, hidden 1280x720 GL,
  warm cache): the first title frame came a median 3.5 s sooner (15.0 s to
  11.9 s, every pair faster), the pack checks fell from 1.86 s to 4 ms, and
  decl and renderer init ran about 0.5 s faster each without the 800 MB read.
  The optimized MD5 of pak1 costs 1.34 s. A pak0 edited in place with its size
  and write time kept, and a missing pak1, are still refused; the dedicated
  server records and reuses the same way.

- [x] Lay out the gameplay HUD in visual specification section 14's Remastered
  layout: the on-foot, multiplayer, walker and hover tank, air-defence cannon,
  Awakening space cannon (with its ship-status panel), MCC cannon (with the
  MCC's hull readout), speeder bike (with the race display, the boost in the
  HUD prompt and the speed streak) and GEV (its status cluster drawn from the
  design atlas, since its art never shipped), flatbed and tram,
  objectives and scoreboard (Tourney included) HUDs answer the stock GUIs' state
  and events from generated retained documents, with the fields the stock GUIs
  lack published by the game, and multiplayer adds the respawn prompt, the
  free camera's name tags and Previous on the follow card. The
  Minimal preset (`ui_hudLayout 2`) keeps the readouts, the crosshair and what
  asks for the player's action; the Competitive preset (`ui_hudLayout 3`) gives
  multiplayer the match bar with network quality, the team status lists and a
  15% larger status bar. Message mode opens the Remastered chat input under up
  to eight lines of history, and the quick-chat wheel (`_quickChat`, D-pad
  Down and V for new configurations) sends one of eight localized phrases to
  the team or everyone ([quick chat](ui/weapon-wheels.md#quick-chat)).
  Settings > Game Options > HUD offers the layout,
  size, width and safe-area inset, the HUD's opacity (`ui_hudOpacity`), the
  damage direction, pickup messages and weapon strip toggles
  (`ui_hudDamageDirection`, `ui_hudPickups`, `ui_hudWeaponStrip`), and Toggle
  Objectives (`g_objectivesToggle`), which opens and closes the objectives
  display with presses, with a Close prompt. Reduced Flashing
  (`ui_reducedFlashing`, section 13.10) caps the damage direction indicator at
  0.40 in every layout (Classic plays a 0.40 copy of the stock indicator), caps
  the vehicle hit glow at 0.40, starts the game's white teleport and spawn
  flashes at 0.40 and drops the death afterimage. HUD prompts draw the controller
  button's glyph in the family of the controller in use (`in_promptStyle`
  forces one), and Remastered Tourney sets its message bar, timer and ROUND
  and ARENA plate under the bracket strip. The HUD's alarms follow section
  14.10: the carried flag beats as the stock one does, the one flag swings to
  its carrier's team color, and under reduced motion every loop rests on its
  static cue (a taken flag at 34 u, the exit chevron still, Dead Zone's team
  color held a second per tick). Medical Labs' implant activation
  (`guis/vehicles/medchange.gui`, section 14.19) is a Remastered document: the
  stock hex sequence on the centered screen continued through every band, the
  corner pieces at the view's corners, readouts at the screen edges, the hurt
  pulse and the save notice, with steady cells under reduced motion.
  The held scoreboard is the multiplayer menus' card (section 14.18) as tall
  as its lists, with Tourney's bracket chart of open-plate arenas, and scrolls
  past its height by whole rows on the weapon-cycle controls and the right
  stick, kept on the client, over a count of the rows below. With the retained
  menus the match summary is the same card (SUMMARY with the result, the
  standings with best weapons and a typed chat field; STATISTICS with match
  awards; SCOREBOARD), covering `guis/summary.gui` as the Escape card covers
  the menu ([the summary card](ui/multiplayer-menus.md#the-summary-card)).
  The classic summary keeps its stock layout over vector reconstructions of
  the stock menu furniture, marks your row with the corner marker on its
  SUMMARY and SCOREBOARD tabs, regains the retail list's top-three tints and
  greyed rows (`SetStateVec4` reached no stock GUI), names the active device's
  key to return, reads DRAW for a level team match and fills its tabs in every
  game type. [Remastered HUD](ui/remastered-hud.md).

- [x] Route the screen-space gameplay HUD families through the retained UI,
  including objectives, crosshairs, multiplayer score/chat/summary panels,
  vehicles, cinematics and Awakening overlays, plus game-owned combat/input
  indicators. Preserve stock GUI state and atomic fallback.
  [Coverage, validation and remaining design scope](ui/hud-coverage.md).

- [x] Connect the new vector ammo/health/armor status bar to normal SP/MP
  gameplay, with Marine/Strogg variants, independent scale and safe inset,
  and same-frame fallback to stock gauges. [Implementation and validation](ui/status-bar.md).

- [x] Restore the intended camera-shake strength and range after audio modernization.
  Air Defense's cannon uses the retail camera envelope and map-unit distance; simultaneous
  channels on one emitter no longer multiply the shake. Light flicker and controller rumble
  keep their separate audio behavior. Regression coverage and retail binary evidence:
  [screen-shake-regression.md](screen-shake-regression.md).

- [x] Drive the incoming-transmission graph from decoded radio playback and
  give the atlas separate audio-file playback for radio and voice graphs.
  [Playback graph evidence and remaining voice integration](ui/audio-graphs.md).
- [ ] Restore the multiplayer microphone capture/codec/playback backend before
  qualifying live voice graphs; the inherited backend remains stubbed.
- [x] Replace the legacy initialization presentation, add shared fourth-level
  campaign setup transitions, feather Gaussian focus outside every scope,
  preserve the MCC treatment sequence while refining its recovery, add a
  restrained death grade and complete the black fade before loading.
  Windowed GL/Vulkan gameplay, native layout/geometry and gate evidence are
  recorded in `docs/dev/ui/retained-screens.md`; wider UI migration stays open.
- [x] Make the initializing screen smooth: repaint it up to 60 times a second
  whenever startup reads a file, within a bounded share of startup time, and
  open it with a 1.3 s entrance in which the mechanism zooms down to its seat
  as its gears wind in and the clamps close; step lines rise into place over a
  cascading rule (visual specification 1.37, `FLOW-069`). Engine traces and
  captures are recorded in the roadmap's UI-07 progress note.
- [x] Resolve title pointer lag, wide/tall loading anchors, retained weapon
  scopes and monster flesh impacts. The title keeps scene parallax while its
  vector framing stays cached; loading furniture uses opposite virtual edges;
  SP/MP and vehicle scopes select the retained documents with stock/mod fallback;
  model hitscan requests the posed collision snapshot carrying retail flesh and
  armour maps. Native screen/vector/routing tests and windowed OpenGL/Vulkan
  gameplay captures are recorded in `docs/dev/ui/retained-screens.md`.
- [x] Add the Medical Labs implant activation to visual specification 1.15 and
  the interactive atlas, preserving stock timing and specifying wide/tall edge
  anchors. This completes the requested design/atlas coverage; its full retained
  vehicle-HUD migration stays explicit as `FLOW-062`.

- [x] Make temporal AA production quality and the default upscaler (idTech 6
  roadmap: complete motion ownership). The velocity pass now owns every moving
  surface per pixel on OpenGL and Vulkan: exact vectors for rigid entities,
  for skinned MD5 surfaces (each pose's predecessor is captured before
  `UpdateSurface` overwrites it and uploaded as a frame-temp `idVec3` stream)
  and for the weapon through its own depth-hack projection and depth range;
  reactive coverage in the velocity target's blue channel for BSE, GUIs,
  subviews, post and translucent moving surfaces, replacing the two scissor
  rectangles that let one effect reject history over the whole view. The
  resolve clips a Catmull-Rom history to a YCoCg variance box with
  nearest-depth velocity, no longer treats colour change as reactive, and
  presents through CAS (`r_temporalAASharpness` 0.5). `r_temporalAA` is now
  0/1/2 with default 2 (automatic below native resolution, never on ES or
  without GLSL 1.30), migrated once from an archived 0. `airdefense1` on GL and
  validation-clean Vulkan reports complete ownership (53 rigid, 11 posed incl.
  2 weapon, ~75 reactive, 0 missed). `renderer_temporal_presentation.py`, the
  Vulkan header pins and the vid_restart harness cover it.

- [x] Cut the search-path cost of a level load (idTech 5 track, Air Defense 1
  register item 1). A null-buffer `ReadFile` for a pk4 member is answered from
  the central-directory index (the uncompressed size is kept per entry) instead
  of reopening the pak, and is no longer recorded in the learned manifest.
  During a level load each loose search directory on the way to a file is
  checked once and a lookup under a missing directory skips its `fopen`
  (`fs_cacheLooseDirectories`, Windows/case-insensitive only; `CreateOSPath`
  drops negative answers). One `game/airdefense1` load skipped 60,854 failed
  opens for 176 directory checks; same-binary median load 14,255 -> 12,978 ms,
  image probe phase 415 -> 132 ms. `filesystem_length_probe_contract.py`
  compiles the real search, probe, memo and `CreateOSPath` bodies.

- [x] Hash loaded content with the processor's SHA instructions (idTech 5
  track, load-time register). `idCrypto::SHA256` compresses whole block runs
  through the x86 SHA extensions after a one-time CPUID check, or the Armv8
  SHA-256 instructions where the compilation baseline guarantees them (Apple
  Silicon), about 2 GB/s against 252 MB/s; the portable path scrubs its
  schedule once per call (339 MB/s). The image policy hashes about 510 MiB per
  Air Defense 1 load. Interleaved warm loads with only the renderer module
  swapped: OpenGL median 12.4 -> 10.6 s, Vulkan 2.9-3.7 s faster per pair. The
  native core-safety test cross-checks both paths at every length to 1,100
  bytes and four alignments; the Armv8 path passed under qemu-aarch64.

- [x] Read level textures on worker threads (idTech 5 track, Air Defense 1
  register item 2). `LoadLevelImages` predicts each image's precompressed DDS
  and up to four workers read and fingerprint the next ones while the main
  thread uploads; the main thread still opens every file and owns every
  buffer (32 files / 64 MiB window), and `R_LoadPrecompressedDDS` takes a
  finished buffer through `R_SetDDSPrefetchSource` (never for recovery reads).
  1,657 of 1,657 predictions taken; DDS phase 1.1-1.7 s -> 0.1 s; warm load
  OpenGL median 11.1 -> 9.1 s, Vulkan 1.6-2.3 s faster per pair; static
  screenshot region pixel-identical. `renderer_image_content.py` compiles the
  real loader with a counted prefetch hand-off and pins the worker invariants.

- [x] Stop sending every OpenGL texture to the driver twice. Compressed
  textures take immutable `glTexStorage2D` storage when available (new
  `hasTextureStorage` capability) instead of a 2011 BFG workaround that
  uploaded an uninitialized buffer per level, and a loaded image queries GL
  errors once instead of once per mip (still before its consumed load
  completes). Warm Air Defense 1, OpenGL module swapped, idle machine: median
  10.3 -> 9.6 s, every pair faster; fixed-tic screenshots matched outside
  animated effects.

- [x] Stop hidden Vulkan windows stalling up to a second per frame. With the
  display off (Modern Standby on 2026-10-06, 10:06-17:49), a hidden window's
  FIFO presentation engine handed swapchain images back only every quarter to
  whole second, so `vkAcquireNextImageKHR` often waited out its 1 s timeout and
  the frame was skipped: retained-UI captures alternated ~40 ms and ~1 s frames
  and ran up to four times as long as on OpenGL. A hidden window
  (`r_hiddenWindow`, or a strict restart's hidden request) now renders each
  frame into its slot's offscreen image and never acquires or presents;
  screenshots and back-buffer copies read that image, and the completed frame
  counts as presented. The swapchain is still created, so present-mode reports
  are unchanged, and visible windows keep the 1 s timeout.
  `r_vkPresentationFailureTest 3` withholds every swapchain image to reproduce
  the condition with the display on (presenting: 113 timed-out acquisitions in
  a 6 s wait; offscreen: 60 fps and none), and `r_vkHiddenWindowPresent 1`
  presents hidden frames again for presentation studies. The uncalled
  `VK_Device_PresentClearFrame`, an unbounded acquire, is removed. Offscreen
  and presented captures at one frozen pose are pixel-identical.
  `renderer_display_presentation.py` runs the real frame methods on both paths
  and rejects unbounded acquisition.

- [x] Keep Vulkan's compiled pipelines across crashes and measure them. The
  pipeline cache is written atomically before every level load as well as at
  shutdown, and every `vkCreate*Pipelines` call is timed (`gfxInfo`, per-map
  log line). A whole Air Defense 1 session with a full sweep creates 33
  pipelines: 49 ms total and one 11 ms creation cold, 4 ms total warm, so
  background or prewarmed compilation is not needed for stock content.

- [x] Stop GPU skinning from paying for the CPU work it replaces, and record
  why it stays opt-in. Vulkan accepts admitted surfaces as deferred (no CPU
  tangent derivation) and completes refused ones on the CPU before any draw;
  immutable four-weight sidecars are validated once at load instead of on
  every use; `deform expand` derives the basis of a GPU-skinned pose. The
  2026-10-06 strict pairs measured only about 1,500 skinned vertices (about
  15 microseconds of CPU) per frame on stock maps, so `r_gpuSkinning` stays
  off; the evidence verifier follows the benchmark's report schema again.

- [x] Play retail Quake 4's per-area reverb. openQ4 read neither reverb file
  and no sound reached its single Generic reverb, because the send was gated by
  a shader wet level that defaults to 0. It now loads `efxs/<map>.efx` or
  `efxs/default.efx` and `maps/<map>.reverb` exactly as Quake4.exe 1.4.2 does,
  drives four EFX reverb slots that follow the listener's area and the three
  nearest areas through its portals (pinned, panned toward each doorway, the
  listener's slot primary), and routes every mono voice to its own area's slot
  and the primary slot, with retail's -10000 mB radio and -500 mB voice-over
  room levels and -1500 mB of EAX occlusion per blocking portal. The EAX to
  EFX arithmetic is OpenAL Soft's EAX emulation and reproduces `efx-presets.h`.
  Adds the retail `s_useEAXOcclusion` and `s_muteEAXReverb` cvars, `s_showReverb`,
  `listReverbs`, the SDK's reverb editor interface, and
  `EndLevelLoad( mapName )`. Verified against the 1.4.2 binary; record and
  addresses in [retail-audio-reverb.md](retail-audio-reverb.md).
  `openq4-sound-reverb-core`, `sound_settings.py` (a new mutant) and
  `sound_retail_reverb_contract.py` pin it.

- [x] Audit the audio cvar defaults against Quake4.exe and Quake4Ded.exe 1.4.2
  (their cvar objects hold name, default and description together) and modern
  output devices. `s_numberOfSpeakers` defaulted to 6 and the three top
  presets forced it, so OpenAL Soft opened 5.1 on headphones, Windows downmixed
  it and automatic HRTF never engaged (seen on a Realtek headphone endpoint).
  It now defaults to `0`, the device's own layout: no output-mode request, any
  realized layout accepted by the checked settings and recovery paths (new
  `device-layout` recovery mode), every preset sets 0, and the Audio menu row
  reads `Auto;Off;On`. `s_radioChatterFraction` 0.5 was a guess; retail is
  0.9. `s_quadraticFalloff` 1 was Doom 3's; retail is 0 and unarchived.
  `s_openALEfxDebugMode` and `s_showLevelMeter` are no longer archived. A
  one-time migration (`s_audioDefaultsMigrated`) moves the archived 6, 0.5 and
  1. Every other audio default already matched retail or is openQ4-only and
  sound. `sound_settings.py` and `sound_recovery.py` cover the device layout.

- [x] Audio modernization pass against the id Tech 5 (BFG) and id Tech 6
  (Wwise-class) generation; record in
  [audio-modernization.md](audio-modernization.md). Spatial: `AL_SOURCE_RADIUS`
  from each sound's `minDistance` (`s_sourceRadiusScale`, BFG's unused inner
  radius), full spread for sounds at the listener, direct channels for stereo
  (`s_directStereo`; HRTF had narrowed and dimmed the menu music by ~5 dB).
  Device: one attribute list for create, reopen and the settings reset, which
  fixes `s_openALHRTF 1` being ignored and hot-plug dropping the layout; the
  output limiter (`s_outputLimiter`), an 11th order sinc resampler
  (`s_resampler`, `listResamplers`), `AL_SPEED_OF_SOUND` in game units and 16x
  `AL_MAX_GAIN` for `s_unclamped` speakers. Mixing: recycled channels keep no
  fade (silent sounds after -80 dB script fades), the channel clock follows the
  slow-motion pitch (weapon wheel cut dialogue short; music keeps its tempo),
  silent voice-over updates at zero gain (it kept playing while unfocused),
  path-inferred VO no longer sets retail's `SSF_IS_VO`, `no_dups` remembers
  finished variants, frame-rate independent cushion fade, global channels past
  positional range, filters committed once a frame, air absorption from the
  room preset (`s_airAbsorption`), and static voices restart after a device
  reopen. Shakes: the game's shake query reaches the world and follows authored
  `shakeData` with the corrected retail camera scale and map-unit falloff; the measured
  envelope remains for rumble and lights. Memory: Doom 3 level purge restored
  (airdefense1 then airdefense2: 233 MB to 121 MB of PCM), exact-size Ogg decode,
  and loaders that fail without uploading the beep or ending the session.
  Level loads decode Ogg samples on job workers (`s_asyncSampleDecode`): the
  main thread peeks the page headers for length and format, the job opens and
  checks the stream; airdefense1's sound precache went from ~970 to ~270 ms of
  main-thread time. Every performance preset keeps room reverb (3b13c1dd).

- [x] PBR production readiness, Stage A: complete roughness and AO shading on
  both backends. Multiple-scattering energy compensation on every specular
  lobe, specular and multi-bounce occlusion, horizon occlusion, AO on authored
  ambient lights and a roughness-aware atlas-less GL fallback, all from the
  numerically tested kernel in `PBRMath.h`. The audit also found that Vulkan
  had sampled the PBR environment atlas's mirror level for every roughness
  since c850233d; the atlas now keeps its mip chain, and GL/Vulkan IBL parity
  is exact. Evidence and the remaining stages:
  [production-readiness plan](archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md).
- [x] PBR production readiness, Stage C: calibration and display-domain
  composition on both backends. PBR irradiance is pi times the decoded classic
  light term (`PBRClassicLightIrradiance`), so a white rough PBR surface
  matches classic at normal incidence (a grey sphere showed 25/255 against
  classic 112/255). Every PBR light, light stage, environment term and emission
  is encoded into the display-referred framebuffer on its own, exactly as
  classic lights add; GL's clustered path encodes inside its light loop and
  splits ambient lights per stage. The linear scene and Vulkan's float preview
  are the laboratory mode `r_pbrLinearScene` (default 0); production shares
  the classic HDR tone map, so PBR never changes stock presentation.
- [x] PBR production readiness, Stage B: native per-surface PBR in the classic
  OpenGL light loop (`draw_pbr.cpp`, `r_glPBR`). Admission is the Vulkan
  material contract (`PBRNativeContract`); every light keeps its classic
  stencil or shadow-map shadowing through PBR variants of the shipped receiver
  programs; the ambient walk owns the environment term, typed emission,
  perforated coverage and, as on Vulkan, ordered source-alpha transparency.
  The paired comparison also found that every direct-light term blacked out
  normal-mapped silhouettes; all three now clamp N.V (`PBRShadingNoV`). The
  OpenGL owner matches Vulkan on every paired direct, ambient, environment,
  probe, diagnostic and geometry control under the documented classic and
  edge exclusions.
- [x] PBR production readiness, Stage E: environment lighting in real maps.
  Authored ambient lights are uniform environments for PBR (Fresnel-weighted
  diffuse and the split-sum specular, with a fitted bias term,
  `PBRSpecularBias`), so metals reflect them and AO occludes both. The
  analytic studio environment is the laboratory mode `r_pbrAnalyticEnvironment`
  (default 0). The OpenGL owner composes baked light grids as Vulkan and
  modern GL do, and modern GL no longer encodes baked PBR diffuse twice.
  Separate metallic, roughness and AO maps pack at load (`packORM`). The
  ambient oracles evaluate an independent NumPy reference
  (`tools/tests/pbr_reference.py`) pixel by pixel over the specimen sphere.
- [x] Vulkan's display-referred frame composes baked light grids for PBR.
  Vulkan had composed PBR-weighted grid diffuse only inside the laboratory's
  linear scene, so as players run it a PBR receiver in a grid lost its
  environment pass and took the classic grid pass through its legacy diffuse
  stage, metals included. The environment pass now composes the grid per
  surface like the OpenGL owner, the classic grid pass skips native PBR
  receivers, and PBR always decodes the atlas. `--production` runs the Vulkan
  laboratory suites with the linear scene off, paired with the OpenGL owner;
  every pairing passes.
- [x] PBR production readiness, Stage D: SSAO occludes native PBR indirect
  light only, on both backends. A view with native PBR world surfaces
  snapshots the classic and the complete world depth and draws an occlusion
  field right after its depth prepass; native PBR folds it into the AO of
  ambient, environment, probe and baked light as the lesser of the two, and
  the post pass applies only the field's classic factor. Classic pixels are
  unchanged and PBR direct light is never darkened, as paired laboratory
  controls prove on the OpenGL owner and on Vulkan.
- [x] PBR production readiness, Stage F: `r_pbrMaterials` defaults to 1, with
  a one-time migration of profiles saved under the old default
  (`r_pbrMaterialsDefaultMigrated`), and the renderer's default-safety
  inventory expects it on. Frames that draw no PBR-authored material skip the
  per-frame scene packets, material table and probe preparation on both
  backends (`R_ScenePackets_CommandStreamHasPBR`), so stock content pays
  nothing. Authored reflection probes stay opt-in: they start the modern
  scene-packet pipeline every frame. README, the user guide, the capability
  matrix and the roadmap describe PBR materials as a supported default.
- [x] PBR promotion cross-module audit: the opt-in shared world-interaction
  domain no longer rejects every view while `r_pbrMaterials` is on (it
  rejects PBR-authored receivers only), and the OpenGL ES module no longer
  loses the native owner's hooks or the baked-grid packing, which the ES
  module does not build (`tr_backend.cpp` guard; `RB_LightGridBakedParams`
  moved to `ModernGLExecutor.cpp`). A Linux GCC build and an ES symbol
  closure confirm it; `renderer_pbr_materials.py` pins the module guards.
- [x] PBR on animated and converted models: `r_gpuSkinning 1` surfaces were
  already PBR on OpenGL (compute-posed vertex caches), now proven by the
  skinned laboratory controls; a PBR-authored mesh of an MD5R-converted model
  keeps classic CPU-skinned geometry on OpenGL as on Vulkan, and the GPU-posed
  test reads only primitive batches. New `skin-md5r-*` controls pass on the
  OpenGL owner and Vulkan production.
- [x] The OpenGL ES module builds again on main: the Android review branch's
  fix for desktop-only GL in shared renderer code (3ce8167b0) is carried
  unchanged, so the branch merges cleanly later. A Linux build with the ES
  module enabled links every target, and no renderer module has an
  undefined symbol.
- [x] Box-projected parallax reflection probes: `boxParallax` makes a probe's
  light volume the room box its cubemap shows, with per-axis `light_radius`.
  The shared kernel (`PBRBoxParallaxDistance`, `PBRBoxInfluence`) runs on
  Vulkan, the OpenGL owner and the modern GL path; the probe record grew to
  seven vec4s. Seven probe controls prove on all four owners that reflections
  follow the box (sign tests), per-axis extents, face-based blending and an
  exact analytic frame outside the box.
- [x] In-engine probe capture: `bakeReflectionProbes` captures every
  native-cube probe from its light and reloads its image. `envshot` and the
  capture now size their views in virtual-screen units; window pixels had
  cropped every capture at window sizes other than 640x480. The `baked`
  control checks GL's cube convention in the written faces and in the
  reflection on all four owners. The light-grid bake shared the old sizing
  and is fixed separately.
- [x] SSAO on the modern visible GL path: a frame that requests SSAO and draws
  a PBR material is declined to the light loop (`pbr-ssao-indirect-only`),
  whose native owner applies SSAO to indirect light only; a modern GL run of
  the SSAO suite proves the decline and the owner's result.
- [x] OpenGL PBR needs GLSL 1.30: on a context without it (macOS's GL 2.1) PBR
  materials keep their classic stages, with a one-time console note, instead
  of being admitted with no environment program. Under an emulated Apple GL
  2.1 context the old path crashed the client in Mesa's shader compiler; now
  its PBR controls render like the PBR-off ones.
- [x] PBR qualification beyond Windows/NVIDIA: Vulkan on Intel Iris Xe passes
  every suite but one SSAO control (6 of 833,291 classic pixels move,
  deterministically), and on Linux Mesa (llvmpipe and lavapipe) the direct,
  probe, SSAO, environment and ambient suites pass on both backends with
  OpenGL/Vulkan parity, apart from one lavapipe pixel off by one byte in the
  ambient material-layout check. Mac hardware review remains open.
- [x] Fix the OpenGL PBR-laboratory regressions reported on 2026-10-04,
  bisected over lean lab runtimes of the 09-24, 09-25, 10-01, 10-02 and current
  builds:
  - `shadows` ("shadow acne on an unoccluded planar receiver", a dark wedge
    over the left wall and ceiling) started with a48624df. Its PCSS-lite
    default (`r_shadowMapFilterMode 2`) reached the modern GL clustered-forward
    receivers, which have no blocker search and filter with one fixed kernel:
    `ModernShadowPlanner` gave them `ceil( effectiveFilterRadius )`, the PCSS
    search and penumbra bound of 8 texels, since July. Grazing walls inside the
    lab projector's frustum self-shadowed. Not the player body
    (`static-shadows` had the same error) and no station: the projector alone
    reproduces it, and modes 0 and 1 at radius 2.0 are clean. Modern receivers
    now use the base contact radius; modes 0 and 1 are unchanged and the classic
    GLSL receiver keeps its own PCSS.
  - `production-fixed-shadow-fallback/-native` differed by one byte in two
    channels, and the first capture always took the extra byte whichever case
    it was. A projected light whose cache signature changed was re-rendered
    into the next free atlas cell, and the 2.0-texel default radius reaches
    receivers that flip between cells. A miss now re-renders into that light's
    own idle entry (`RB_ShadowMapRecycleProjectedCacheEntry`), in the legacy and
    sealed schedulers alike.
  - `production-fixed`, `-bump`, `-colored` and `-grid` ("classic interaction
    differs from the native lighting reference") were the laboratory's oracle,
    not the renderer. c850233d moved the stock display shoulder in `bloom.fs`
    from 0.98 to 0.5, so the native reference's highlight (0.782) displays as
    0.686, byte 175. With `r_hdrToneMap 0` the native image still matches the
    modern radiance at 0.251 mean and 0.56 maximum byte error, the 09-21
    figures. The comparison now passes the modern radiance through the same
    stock transfer (`stock_display_transfer`).
  Evidence (`+g8a4a7aa9` plus these changes, GL 4.5, `--gl-debug`): shadows
  8/8 with all four unoccluded patches exactly 0, classic 19/19 (specular cases
  0.251-0.252 mean, at most 0.62), classic grid 5/5, admission 12/12, multi 4/4,
  local/global 2/2, skinning 12/12, post 12/12 and isolated 6/6, and the four
  Air Defense 2 door scenarios (`renderer_shadow_mapping_maps.py --scenario
  door-*`) pass with the cache change. `pbr_laboratory_fixture.py` covers the
  shoulder with a negative control, and
  `renderer_vulkan_shadow_compatibility.py` pins the contact radius and the
  cache-cell reuse.

- [x] Remove the grazing-wall shadow acne of the 2.0-texel projected filter
  default on all three projected receivers: the classic OpenGL
  `shadow_interaction.fs`, the modern GL clustered receiver and Vulkan's
  `interaction_shadow.frag`. Each compared every PCF tap and PCSS blocker probe
  with the centre's receiver bias. On a receiver at an angle to the light that
  bias, with the one-texel normal offset, covers about 1.7 texels of the
  surface's own depth slope. The 0.75 radius kept the Poisson taps inside it;
  at 2.0 they reach 1.96 texels and the 4-texel blocker search 3.9, so taps on
  the side nearer the light found the receiver itself, and PCSS also took it
  for a blocker. Every off-centre tap and probe now adds
  `R_ShadowMapTexelDepthStep` (world texel over falloff depth extent; the
  texel bias is `r_shadowMapTexelBiasScale` times it) times `min(tan, 4)` per
  texel of its distance from the centre. The centre sample keeps the plain
  bias. Vulkan's shadow block carries the step in place of the scaled bias,
  with the scale in `texelSize.z`, and stays 512 bytes. The modern GL receiver
  derives the step from its shadow matrix's depth row. A signed receiver-plane
  bias from screen-space derivatives also cleared the wall, but drew a dark
  line along concave corners. A one-sided version kept the corner line that
  radius 2.0 already had; the distance-scaled bias also removes most of it.
  Evidence (`+g632d66b1` built with and without this change, lab
  projector-only captures, unoccluded left-wall patch, mean/max max-channel
  error): OpenGL classic 3.09/25 to 0.53/1 at the shipped defaults and
  3.86/13 to 0.53/1 at mode 0, the old 0.75 defaults' 0.53/1; Vulkan 2.74/25
  and 3.92/13 to 0/0. Pixels darkened by more than two levels on the deeper
  wall fell from 34,660 to 39 (OpenGL), 34,685 to 46 (Vulkan) and 5,005 to 17
  (modern GL); the old 0.75 filter also loses its 857 (OpenGL) and 1,153
  (Vulkan). A sphere moved to touch the grazing wall keeps its contact shadow
  (retained occlusion against the stencil reference in the contact band 0.988
  before, 0.987 after, on OpenGL and Vulkan), and the packed-ORM station is
  pixel-identical on both PBR receivers. Air Defense 1's terrain keeps 0.854
  (OpenGL) and 0.856 (Vulkan) of the stencil shadow contrast, against 0.866
  and 0.869 before: about 300 pixels along one stair-stepped shadow edge on
  sloped ground are about 4 levels lighter. The one-sided plane variant kept
  0.863 there but kept the corner line. Vulkan's lab `shadows` case now
  passes; `shadows-manual-point`, `multi-budget` and `local-global` fail
  identically with and without the change. OpenGL laboratory groups 80/80
  with `--gl-debug`, `vulkan-direct` 68/68 at 0x and 4x, the Air Defense 1
  and q4dm2 terrain checks on OpenGL and Vulkan (three runs each, both
  builds) and the four Air Defense 2 door scenarios pass.
  `renderer_vulkan_shadow_compatibility.py` pins the footprint bias on all
  three receivers and both upload paths.

- [x] Make the multiplayer Admin page's password check work. The page sends
  `rcon verifyRconPass` and waits for the game to hear the answer; the server
  ran it as an unknown command and the client never passed an answer on, so
  the page never opened. The server now answers the check with Quake 4's
  "rcon verified" string without running anything, and the client hands that
  answer, or the bad-password reply, to the game when it comes from the
  server asked, and answers false itself when it refuses the check before
  sending (no password, or one shorter than rcon2's 12 bytes) or after 10
  seconds without a reply. The client also hands the server's remote-console
  output to the game, as Quake 4 did, so the Admin page's console shows it.
  Evidence: `async_rcon_verify_contract.py` compiles the client and server
  code and catches nine reverted rules; two-process probes, on OpenGL with the
  classic menu in English and on Vulkan through the multiplayer card's
  hand-off in German, show a wrong password returning to Join Team, a short
  one refused, the right one (18 and 27 characters) opening the admin
  controls, and `rcon si_name` reaching the Admin console.

- [x] Make `r_resolutionScaleMode 2` and `3` reach single player. The SP game
  renders its below-native scene into its own targets and presented them with
  a full-screen material, a bilinear stretch, so the modes never applied. It
  now hands that final upscale to the renderer through the appended
  `idRenderSystem::PresentScaledScene` slot (render API 22, game API 51,
  `RC_PRESENT_SCALED_SCENE`), on the fast no-post path and the MSAA/SMAA path;
  blur and CAS run at the scene extent first. OpenGL presents with
  `resolutionscale.fs` or a nearest filter it restores afterwards (desktop GL had
  no mode 3 before, its own scene target included); Vulkan wraps
  `scene_scale.frag` and falls back to the bilinear spatial present. Modes 0 and
  1, native and supersampled scenes, ES, tiled captures and the temporal paths
  never call it. Evidence: hidden Air Defense 1 probes at 50% on OpenGL and
  Vulkan, fast and MSAA+SMAA paths, captured with `screenshot` (the back buffer
  or swapchain image the frame presents): mode 3 duplicates every scene texel
  into an exact 2x2 block, mode 2 raises edge contrast about 12% over mode 1,
  mode 1 is pixel-identical before and after cycling the modes, and mode 2 at
  sharpness 0 matches mode 1 within 1 LSB. `renderer_scaled_scene_presentation.py`
  and `renderer_native_ui_output.py` pin the routing, refusals and filter
  restore.

- [x] Promote the Vulkan renderer to a stable, supported opt-in renderer on
  Windows x64 (user sign-off 2026-10-04). It stays experimental on Linux and macOS, OpenGL stays
  the default and `best` still resolves to `gl`. README, the user guides,
  `TECHNICAL.md`, the platform-support tiers, the capability matrix, the
  `r_renderApi` help text and the release notes now say so.

- [x] Qualify the experimental Vulkan renderer further on Windows: 20 stock
  multiplayer maps host bot matches on Vulkan/NVIDIA, Vulkan/Intel and OpenGL
  (60 runs, zero validation errors), and a 25-minute soak of 16 map loads with
  a `vid_restart` stays validation-clean with memory that plateaus like
  OpenGL's. `gfxInfo` and the new `rendererVulkanMemoryInfo` report Vulkan
  heap usage, budget and live allocations. See the
  [gap-closure ledger](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).

- [x] Bring translucent moment shadows (`r_shadowMapTranslucentMoments`) to
  the experimental Vulkan renderer: a frame-boundary RGBA16F moment atlas
  whose blocks mirror the depth-atlas blocks, the GL caster stage analysis and
  resolve, and both receivers. In a laboratory scene with a cyan translucent
  pane under a shadowing light the moment contribution matches OpenGL (95% of
  399k pixels within one display level) on NVIDIA and Intel. Running Vulkan on
  Intel Iris Xe graphics also fixed pipeline sample locations used without
  `variableSampleLocations`; all 11 local Vulkan matrix cases then pass there,
  and all 29 stock SP maps load validation-clean on Vulkan/NVIDIA,
  Vulkan/Intel and OpenGL. See the
  [gap-closure ledger](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).

- [x] Clear four Vulkan PBR-laboratory failures found on 2026-10-04. Composed
  shadow-cache hits now bind the solid alpha image for their moving casters;
  a view of composed hits alone used to draw them with the scene's last set-0
  image, a multisampled one under 4x MSAA (`VUID-RuntimeSpirv-samples-08725`).
  Views that request a shared classic adapter (`r_rendererShared*`) and need
  nothing the native scene target provides stay on the swapchain again, so
  those adapters can own them; since the 2026-09-25 checkpoint none could. Two
  laboratory oracles that predated the linear HDR scene and the PBR preview now
  follow the reported display domain: extreme emission expects the filmic,
  once-encoded red channel (178.1, not 77.4) when the linear scene owns the
  view, and the capacity proof expects 65 float-composed alpha layers to reach
  255 rather than RGBA8's 254. On Vulkan the `vulkan-direct` suite passes
  68/68 at 0x and 4x, classic colors 90/90 and pixel-identical to OpenGL, and
  the capacity proof at 0x and 4x. See
  [shadow movers](shadowmapping-movers-2026-09-05.md#composed-vulkan-hits-drew-moving-casters-without-their-alpha-image)
  and the [world ambient domain](classic-world-ambient-domain-modernization.md#backend-execution).

- [x] Apply `r_brightness` and `r_gamma` once on Vulkan, as OpenGL does. The
  `RC_SWAP_BUFFERS` back-buffer pass added on 2026-09-18 mapped every frame,
  then the executor's display mapping (2026-09-09) mapped it again at
  presentation and screenshot readback, so any non-neutral value was applied
  twice. The display mapping is now the only pass: it runs after CRT, covers
  native and shared GUI draws and skips neutral settings. At two frozen Air
  Defense 1 poses, `r_gamma 2` gave Vulkan a mean luma of 127.1 and 149.2
  against OpenGL's 76.7 and 94.8, and Vulkan's pixels matched two applications
  of the curve to its own neutral frame. After the fix Vulkan gives 77.4 and
  95.3 against OpenGL's 77.0 and 95.4, every `r_gamma 2`, `r_brightness 1.5`
  and CRT-plus-gamma capture on both renderers matches a single application,
  and Vulkan's `r_gamma 2` pixels sit within 2 levels of it everywhere.
  `vk_display_color_mapping.py` now fails if any other Vulkan code reads
  either cvar, and the unused post colour-mapping shader is gone.

- [x] Make the experimental Vulkan renderer survive what used to stop it:
  a GPU or driver reset during play now restarts the renderer automatically
  and carries on (three times per session, `r_vkPresentationRecoveries`)
  instead of leaving a frozen window; a lost window surface is rebuilt in
  place; a display change that removes a typed vsync mode keeps presenting;
  minimizing no longer spins the device; live window resizes keep the render
  size in step; and changing shadow quality mid-game can no longer invalidate
  the frame being recorded. Vulkan also runs mods' ARB assembly material
  programs (translated to GLSL and matched against OpenGL), samples rendered
  textures the right way up in authored programs, and gains `r_showShadows`,
  `r_enhancedMaterials`, the renderer-owned sharpened and nearest
  `r_resolutionScaleMode` upscales, and a `benchmark` command that times GPU
  work. See [material programs](vulkan-material-programs.md) and the
  [gap-closure ledger](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).

- [ ] Add an experimental OpenXR VR mode for the OpenGL client on Windows and
  Linux: a statically built Khronos loader, off-axis per-eye rendering through
  a redirected default framebuffer (render API 21, game API 50), the HUD as a
  head-locked overlay, menus and cinematics on a world-locked screen with a
  controller pointer, controller buttons through the gamepad bindings, a
  stereo-correct laser dot (optional beam) where controller-aimed shots land,
  controller haptics on each shot and hit, room scale (the body walks after
  the head: directly with collision in single player, through the usercmd in
  multiplayer), physical crouch, weapons held at full size by each
  model's own grip with the barrel along the aim (measured from its idle
  pose; every stock weapon checked; earlier builds' archived offsets reset
  once), two-handed aim with the off hand on the foregrip, a comfort
  vignette during stick movement and smooth turning, scope zoom that
  magnifies each eye, single player vehicles and turrets in stereo (the eye
  on the turret's axis, the turret following the head, the seat keeping its
  own facing through the vehicle bind; the tram gun, the medical table, the
  bed and both drop pod rides probed start to finish), a weapon wheel that no longer turns
  the world, and a Virtual
  Reality section in
  Settings > Game Options (twelve rows, twelve languages). Building that section
  fixed the Game Options and display section selectors, which ignored clicks.
  The VR math core passes natively and under ASan/UBSan on Linux; the source
  contract, `openxr_vr_smoke.py` and `openxr_vr_menu_smoke.py` pass against
  the in-tree OpenXR test runtime on Air Defense 1 (stereo parallax, head
  tracking, HUD alpha, snap turn, the aim dot fusing on the shot's path and
  following the left controller when left-handed, the stick walking where
  the head faces or the off hand points, a shot
  pulsing the weapon hand, a room-scale step walking the body, trigger
  binding, a click on Resume with the window unfocused, a walker's cockpit
  turning 45 degrees after the head about a fixed eye, changing Laser Sight
  in the Virtual Reality section by pointing, localised action names, ending
  VR from the runtime); the Linux sources and loader compile under GCC. A run
  on real headset hardware is still open. See the
  [plan](archive/2026-10-18/plans/2026-10-02-openxr-vr.md) and [VR guide](../user/vr.md).

- [x] Suppress startup splash windows for hidden-window, renderer-disabled and dedicated launches across Windows, Linux and macOS.

- [x] Create Server's Server Type now takes effect: LAN keeps a hosted game off
  the Internet master servers and admits only players on your local network, as
  in retail Quake 4.

- [x] Create Server's Dedicated choice now hosts a dedicated server, as retail
  Quake 4 did: the game window closes and the console window runs the server,
  and closing it stops the server. It works from the single-player,
  multiplayer and Awakening menus, and openQ4 can start again on the same
  computer to join the server. Dedicated servers, `openQ4-ded` included, now
  keep sound off for every map they load instead of decoding each map's sounds,
  and keep the sound data that effects still read.

- [x] openQ4 starts as a dedicated server with `+set net_serverDedicated 1`, as
  retail Quake 4 did: no game window opens and the console window runs the
  server. Either way it became one, a dedicated server keeps serving through an
  engine reload (`reloadEngine`, an add-on map, `net_serverReloadEngine`)
  without opening its game window again, and closing its console still stops it.

- [x] A server you host keeps the network address and port set with `net_ip`,
  `net_ip6` and `net_port`, as in retail Quake 4. Starting a match without
  cheats reset them, so the next server you hosted, or a server reloading for
  an addon map, listened on every interface and the default port.

- [x] Consolidate SDK game sources and Awakening SP into the engine checkout;
  qualify campaign discovery, content/save isolation, stock/expansion save
  compatibility, multiplayer and Arena before archiving the historical repositories.
  See [implementation/evidence](archive/2026-10-18/plans/game-source-consolidation.md).

- [x] Restore CI build coverage after game-source consolidation by checking
  Linux ARM64 cross-build guidance for the canonical in-tree SP/MP sources
  instead of requiring retired companion-repository setup.

- [x] Fix the macOS dedicated-server build, which had stopped linking: it
  shares the client's source list, which had dropped the dedicated server's
  stand-in for the client-only native input driver.

- [x] Stop macOS OpenGL logging a `GL_INVALID_ENUM` error and backtrace at
  every startup. The render graph asked Apple's GL 2.1 legacy context for
  GL 3.2 multisample limits: GLEW's experimental flags only say an entry point
  resolved, and macOS resolves 3.2 entry points for that context too. It now
  asks the context, as texture allocation already did, so rendering is
  unchanged. The error failed four renderer self-tests on both macOS CI legs;
  an Apple-shaped Mesa 2.1 context reproduces it, and all four pass there with
  the fix.

- [x] Make `gfxInfo` report the MSAA macOS OpenGL actually delivers. On Apple's
  GL 2.1 legacy context it printed `MSAA requested=8 effective=8 reason=active`
  while the same log said every multisample texture had been refused: it read
  GLEW's experimental flags, which macOS sets for that context too. It now asks
  the context, as texture allocation does. Offscreen targets cannot multisample
  there, so the scene gets MSAA only when it draws straight into the window, and
  `gfxInfo` names where it drew: `reason=default-framebuffer` with the window's
  sample count, or `effective=0 reason=texture-msaa-unavailable`.
  `GL_MAX_SAMPLES` is read whenever the context has framebuffer multisampling.
  Rendering is unchanged. On an Apple-shaped Mesa 2.1 context the renderer
  self-tests now report `effective=0 reason=texture-msaa-unavailable` instead
  of `effective=4 reason=gl-max-clamp`, and Air Defense 1 reports the window
  route with Post AA off and the offscreen route with it on, where the old
  report claimed 4x both times; the GL 4.5 report is unchanged. See
  [display settings](../user/display-settings.md).

- [x] Keep MSAA on macOS OpenGL's 2.1 legacy context when Post AA is on.
  SMAA needs GLSL 1.30, which that context lacks, so it never ran there, but
  asking for it still sent the scene through the game's offscreen targets,
  which cannot multisample on that context. The `balanced`, `quality` and
  `ultra` presets ask for it, so they got neither SMAA nor MSAA. Only SMAA that
  will run now takes the scene offscreen; contexts that run SMAA or have
  multisample render targets keep their route. The offscreen route there also
  raised `GL_INVALID_FRAMEBUFFER_OPERATION` on every frame with a soft particle
  in view: the copy of scene depth that soft particles fade against blitted
  into a scratch depth-only framebuffer whose read buffer still named a colour
  attachment it never has. OpenGL before 4.1 calls that incomplete unless
  `ARB_ES2_compatibility` is present, and Apple's 2.1 context lacks it. That
  framebuffer now reads from no colour buffer, so the copy works, including
  from a multisampled window, which takes the same blit. On an Apple-shaped
  Mesa 2.1 context Air Defense 1 with Post AA on now draws to the window
  (`gfxInfo`: `default-framebuffer-single-sample`, as Xvfb's window has no
  samples) with no GL errors, where it reported `texture-msaa-unavailable` and
  71 to 96 errors a run; the depth-copy fix alone also clears the offscreen
  route. GL 4.5 still runs SMAA, with and without MSAA. A Mac with a
  multisampled window was not available. See
  [display settings](../user/display-settings.md).

- [x] Make `gfxInfo` report the MSAA the 3D scene actually rendered with on
  every OpenGL context. Once a map has drawn, the effective count comes from the
  target the scene drew into (the game's forward target, the renderer's own
  scene target or the window) instead of `r_multiSamples` clamped to
  `GL_MAX_SAMPLES`. That prediction went wrong in three cases: with
  supersampling (`r_screenFraction` above 100) it claimed
  `effective=0 reason=supersampling` while the scene kept its MSAA; after an
  `r_multiSamples` change it showed the new count before `vid_restart` or a map
  load applied it; and with a window that has no samples, as under Xvfb, a
  request raised after the map loaded claimed 4x for a single-sample scene. A
  target whose count differs from the request now reports
  `reason=scene-target` or `reason=scene-target-single-sample`, and a scene
  drawn straight into the window reports `default-framebuffer`. Screenshots and
  levelshots keep the gameplay frame's report, and the startup self-tests,
  which draw no scene, still show the predicted count. Rendering is unchanged.
  On Windows GL, Air Defense 1 at 4x now reports 4x at 150% and after an
  unapplied `r_multiSamples 8`, and ten PBR laboratory suites covering
  captures, scaling, supersampling and restarts report the same counts as
  before. Under Mesa llvmpipe on Xvfb, Air Defense 1 loaded at 0x reports
  `effective=0` after `r_multiSamples 8` where it claimed
  `effective=4 reason=gl-max-clamp`, and the foundation self-tests are
  unchanged. See [display settings](../user/display-settings.md).

- [x] Make `gfxInfo` report the MSAA the 3D scene actually rendered with on
  OpenGL ES too. The ES renderer module, which Android uses, draws views
  through its own backend, and that backend never recorded where the scene
  drew, so its report kept predicting from `r_multiSamples`. Render targets are
  single-sample there, so it always printed
  `effective=0 reason=texture-msaa-unavailable`, even when the scene drew
  straight into a multisampled window, which is the route with Post AA and the
  post effects off, and `reason=supersampling` above 100% scale. It now
  records the scene the way desktop OpenGL does: a scene drawn into the window
  reports the window's samples (`default-framebuffer`, or
  `default-framebuffer-single-sample` for a window without any), one drawn into
  the game's targets reports `texture-msaa-unavailable`, and screenshots keep
  the gameplay frame's report. Rendering is unchanged. Under Mesa llvmpipe's
  OpenGL ES 3.2 on Xvfb, which gives the window 4x MSAA, Air Defense 1 at 8x
  now reports `effective=4 reason=default-framebuffer` on the window route and
  `texture-msaa-unavailable` at 150%, where it reported `effective=0` with
  `texture-msaa-unavailable` and `supersampling`. A report taken right after an
  `envshot` keeps the gameplay frame's value. The ES module does not compile on
  main at the moment, because shared renderer code calls desktop-only GL. These
  runs merged in the fix from the pending Android review round. An Android
  device was not tested.

- [x] Draw the OpenGL `r_showShadows` stencil shadow volumes in colour again.
  Modes 1 and 3 drew black lines and the additive mode 2 drew nothing, because
  the stock shadow vertex programs (`shadow.vp` and the `md5rshadow` family)
  never write a colour, so the per-volume `glColor` never reached the screen.
  The debug pass now takes its colours from a fragment program compiled into
  the renderer, and ordinary stencil shadows are untouched. On Air Defense 1
  all three modes match the Vulkan view and the `r_useShadowVertexProgram 0`
  path, including after `vid_restart`, and normal frames before and after the
  debug view are pixel-identical. Packed MD5R volumes take the same program:
  since the MD5R conversion fix, Air Defense 1 with `r_convertMD5toMD5R 1`
  colours the walker's and the weapon's volumes as the unconverted frame does.

- [x] Make skinned models converted by the opt-in `r_convertMD5toMD5R` draw
  like their MD5 sources. On OpenGL, converted characters and the first-person
  weapon drew as black shapes and cast no shadows. The md5r vertex programs
  posed them correctly, but the CPU copy every other consumer reads (bounds,
  culling, light triangles, shadow volumes, the classic draw paths) stayed in
  the bind pose. The converter also left out the normals, colours, shadow
  stream and 25-joint batch split the packed paths need, and the packed shadow
  draw extruded skinned streams with the unskinned `md5rshadow.vp`. The
  converter now packs meshes the way retail Quake 4 did, so OpenGL lights,
  shadows and depth-tests converted models through `md5rinteraction1/4.vp`,
  `md5rshadow1/4.vp` and `md5rsimple1/4.vp`. Vulkan's CPU-skinned path now
  skins the authored basis like the MD5 path does instead of re-deriving it.
  Old conversions in the generated cache are rebuilt on first load. On Air
  Defense 1 at a fixed game tick, converted and unconverted frames match on
  OpenGL and Vulkan, including the walker's and the weapon's `r_showShadows`
  volumes: the weapon is pixel-identical and the walker's only differences are
  sky particles. `renderer_md5r_conversion_contract.py` pins the converter,
  the skinned sil-trace copy and the packed shadow program choice.

- [x] Make static models converted by the opt-in `r_convertStaticToMD5R` load
  and draw like the originals. The report that Air Defense 1 never finished
  loading with it was not a slow converter. Its 127 static models convert in
  about 0.3 s in a debug build, plus about 1 s of collision extraction that
  the unconverted load pays later anyway. The 2026-10-03 run that hit its
  240 s limit was four to six times slow even in phases with no MD5R work.
  The first frame did crash the OpenGL driver, though, and in a hidden-window
  run the crash dialog leaves the client waiting. A packed stage left buffer
  0 bound, so the next stage's classic colour array took its ambient-cache
  offset as a CPU address, and the packed stage draw never gave the md5r
  stage programs their vertex colours. Each stage now rebinds the ambient
  cache, and the packed stage feeds its own colours. Converted models also
  drew their two-sided lit surfaces twice: the converter packs the back-side
  copies the source model made, and surface generation then made more.
  Surfaces now stay 1:1 with the packed meshes, as in retail. On Air Defense 1
  at a fixed game tick, converted and unconverted frames match on OpenGL and
  Vulkan apart from particles, and `r_showLightCount` over the pod-crash roof
  matches where it doubled before. In three alternating cold-load pairs on a
  busy machine (debug build), whole loads swung between 67 and 124 s in
  either mode, while the model and collision precache, where conversion
  happens, stayed within a second of the unconverted run.
  `renderer_md5r_conversion_contract.py` pins both fixes.

- [x] Turn shadow maps on in the `quality` and `ultra` performance presets and
  default projected-light filtering to PCSS-lite (`r_shadowMapFilterMode 2`)
  with a 2-texel minimum radius (`r_shadowMapFilterRadius 2.0`). Lower presets
  keep stencil shadows. A profile still holding the old 0.75/fixed-PCF pair moves
  to the new pair once; any other choice is kept. The engine preset self-test,
  native preset tests and fresh/legacy/customized profile runs pass, and the
  Air Defense 1 and q4dm2 terrain/occlusion checks pass on OpenGL and Vulkan
  with the new filter default. See [display settings](../user/display-settings.md)
  and [shadow mapping](../user/shadow-mapping.md).

- [x] Stop map loads paying for generated caches that are switched off, and
  make the opt-in ones cheaper. With `com_levelLoadModernization 0`, the
  default, every static, MD5 and MD5R model load still serialized a cache
  payload, the collision loader serialized the map's collision data, and each
  cache call built a content key from every search path, all of it then
  discarded. Owners now ask the appended `idFileSystem` queries
  `GeneratedCacheReadsEnabled` and `GeneratedCacheWritesEnabled` (render API
  23) first. On a debug Air Defense 1 load with `r_convertStaticToMD5R 1` that
  removes about 134 ms of model serialization and 55 ms of key building, and
  collision data loaded in 345 ms instead of 662 ms. With the cache on, the
  MD5R codec streams vertex lists, index buffers, silhouette edges and joint
  matrices in bulk like the static codec, halving its warm decode (35 ms to
  18 ms for 127 payloads in debug), and writes byte-identical caches, so
  existing caches stay valid. That decode was a small part of the 1.8 s once
  blamed on it; most of that was SHA-256. A warm read no longer hashes each
  payload a third time, which saved about 1.35 s across Air Defense 1's 82.6
  MB of caches in a debug build. See
  [generated-cache cost](loading-cache-modernization.md#generated-cache-cost-on-air-defense-1).

- [x] Make the generated world and static-model caches reproducible. Proc
  model surfaces from `ParseModel` and `ParseShadowModel`, and the back sides
  `FinishSurfaces` adds, left `modelSurface_t::id` and `mOriginalSurfaceName`
  uninitialised, and both caches stored the ids. Two cold Air Defense 1 loads
  with `com_levelLoadModernization 1` wrote world caches that differed in all
  2,025 surface ids, and five static-model caches that differed in one
  back-side id each. That id had looked like a near-zero float, but it was
  uninitialised stack data. A surface's id is now its index, the way the ASE,
  LWO and MA converters number surfaces, because `GetSurfaceMask` sets
  `suppressSurfaceMask` bits by index and the ambient pass tests them against
  the id; with zero ids, hiding the first material of a brush model would hide
  all of it. The other renderer and BSE surfaces that started uninitialised are
  zeroed too. World parser version 3 and static parser version `0x00010003`
  make older caches miss, so they are rebuilt once. Two cold loads now write
  byte-identical world and model caches with `r_convertStaticToMD5R` 0 and 1,
  a warm load with the default 0 hits all 198 of its caches, and
  `level_load_cache.py` fails any `modelSurface_t` in `src/renderer` or
  `src/bse` that starts uninitialised.

- [x] Correct outdoor shadow-map terrain sampling on OpenGL and Vulkan.
  Point filters compare against the receiver plane at each sampled texel, while
  distant off-centre point sources use fitted perspective maps and cascades.
  Stock `mp/q4dm2` and `game/airdefense1` comparisons check both lit terrain and
  retained shadow contrast against stencil/shadows-off controls. See the
  [outdoor regression report](shadowmapping-outdoor-terrain.md).
  Expanded Air Defense 1 coverage also fixes static casters whose initial
  distant LOD rejection survived after approaching them: admission refreshes
  per renderer view and the staggered hold uses the renderer's frame clock.
  Eleven outdoor stops on both backends pass terrain/occlusion and full-frame
  cache comparisons, including entrance railings on the first approach and
  return. Sandstorm terrain and the Air Defense 2 indoor controls also pass.

- [x] Validate the local Windows `idtech5-ui` integration with the current renderer
  and game code. Preserve Vulkan startup recovery, PBR image handling, generated
  font atlases, language reloads and single-player cvar ownership. The modern
  SYSTEM page remains opt-in. Paired renderer/game interfaces reject stale
  modules; CI pins the matching companion revision. German covers the new UI
  labels, and Android touch labels have their own IDs alongside the level editor.
  The 285 portable Python check entries, an additional Vulkan policy variant,
  109 Meson tests and 17 companion checks pass; six expensive layout tests use
  an optimized build with assertions enabled. Eight stock-gameplay/SYSTEM-page
  cases cover SP/MP on OpenGL/Vulkan, and both startup-recovery gameplay cases
  and the Windows dedicated smoke pass. Existing version-10 baked lighting
  remains readable while obsolete generated image caches are regenerated.
  Full debug SYSTEM scripts reached edit/apply/resource reload but exceeded
  their six-minute limits. Complete UI workflow/performance, interactive input
  and other-platform qualification remain open; this is integration evidence,
  not full UI product signoff. Stock MP AAS/precache diagnostics remain.

- [x] Harden the preliminary Vulkan device probe: complete device and device
  extension inventories replace fixed caps, empty queues are rejected, forced
  choices cannot substitute another GPU, and verbose ranking considers only
  suitable candidates. All 74 native renderer/probe cases, four source
  contracts, eight startup/GPU/recovery cases and six local probe controls
  pass. Two stock SP/MP 0x MSAA lifecycle runs retain 16 engine captures with
  unchanged runtime hashes and no tracked renderer warnings. Earlier
  instance queries, later creation-failure retries and broader
  qualification remain open; see [device admission](vulkan-device-selection.md).
- [x] Check required Vulkan device and surface capabilities before selecting
  an adapter, allowing automatic selection to continue past an incompatible
  GPU. Explicit selections fail without substitution. The shared production
  selector passes 43 native cases, and all eight staged startup/GPU/recovery
  cases pass. Four SP/MP 0x/4x lifecycle runs and two gameplay recovery runs
  retain 34 engine captures without tracked renderer warnings. Earlier
  instance queries and broader hardware qualification remain
  separate audit items; see [device admission](vulkan-device-selection.md).
- [x] Separate Storage1's scripted first entry from its second-entry gameplay
  benchmark. Both record the actual map filter and endpoint camera poses; the
  second entry also checks its settled viewpoint. Report replay binds the
  scene identity and generated commands. This improves evidence quality;
  it does not close timing, continuous-view, visual-parity or platform gates.
- [ ] Resolve the recurring Vulkan frame-slot wait stalls and complete the
  performance matrix. Timing traces now separate fence waits from image
  acquisition, resource retirement and presentation. Shorter waits and visible
  background-window testing do not remove the delay. OpenGL also reproduces a
  long resource-retirement wait. GPU queue traces narrow the observed delay to
  NVIDIA-to-Intel presentation: both APIs have Intel wait packets pending for
  roughly half a second after unwait events. Direct Intel passes a diagnostic
  control, while returning to NVIDIA reproduces the stall. GPU selection is
  not a renderer fix; the cause and foreground/platform qualification remain
  open. See
  [frame-pacing evidence](vulkan-frame-pacing.md).
- [ ] Complete point-shadow parity and qualification. World-axis radial
  arithmetic and canonical native-resolution scene rendering now bring the
  default nine-tap mapped fixture within the existing two-display-level gate.
  All 26 independent lighting controls and twenty ordinary material comparisons
  pass. Three stencil boundary pixels and small single-lookup/disabled-bias
  differences remain. Stock timing and platform qualification are separate
  requirements; see [point-shadow evidence](vulkan-point-shadow-parity.md).
- [ ] Complete authored Vulkan material-program execution. The initial GLSL
  ambient draw path now compiles actual source pairs, binds material parameters
  and textures, and passes local GL image/restart comparisons. Stock-name
  overrides, edited-source reload and invalid-source repair now pass sixteen
  paired image controls. Per-light GLSL execution, light/texture/matrix bindings
  and receiver-specific stencil fallback now work in the controlled fixture;
  full-scene shadow parity still fails. Flipped render textures,
  broader language/state/geometry coverage, exhaustion and authored ARB remain
  open. These local controls do not close the complete requirement. See
  [material programs](vulkan-material-programs.md).
- [ ] Complete native cutout filtering/coverage parity. The v49 shared-filter
  prototypes improve aggregate sampling accuracy but retain numerical
  differences and individual quality regressions; the refined version can
  require 31 texture fetches. Neither is adopted. The native sampling
  discrepancy diagnosed in v48 remains open, without reducing
  filtering quality or comparison thresholds. See the
  [shared-filter assessment](vulkan-pbr-cutout.md#v49-shared-filter-prototypes).
- [x] Preserve fully opaque Vulkan PBR texels at an inclusive alpha cutoff of
  1.0 by reading constant stage alpha directly in the fragment shader. The
  verified 4x regression loses coverage in 4,436 pixels with the old module;
  the fix matches opaque geometry exactly through image reload and partial/full
  video restart. All 56 cutoff/minification controls pass on GL/Vulkan at 0x/4x,
  with all sixteen minification-suite pairs exact. Measured 1x fragment alpha
  and derivatives now match GL. The final staged renderer passes target/HDR/motion
  GPU contracts without tracked renderer warnings and passes shader/header pins.
  Separate anisotropic sampling differences remain open; see the
  [fragment investigation](vulkan-pbr-cutout.md#v46-constant-alpha-correction-and-fragment-evidence).
- [x] Use typed PBR albedo for admitted Vulkan cutout depth coverage, preventing
  strongly minified masks from disappearing because the classic texture copy
  has different alpha mip rounding. All 32 independent GL/native controls pass
  at 0x/4x with exact opaque/minified coverage and image/video recovery. The old
  module fails the same oracle, losing all 7,278 minified specimen pixels.
  The subsequent framebuffer-orientation fix makes all sixteen ordinary,
  minified and recovery pairs exact at 0x/4x. Smoothed 4x coverage with 16x
  anisotropy improves from 444 differing pixels to four; those residuals and
  the isolated baked-lighting comparison still fail the unchanged limit. See
  [cutout coverage](vulkan-pbr-cutout.md).
- [x] Resolve the 125%/8x PBR preview comparison by carrying GL framebuffer
  coordinates through native offscreen geometry, depth and presentation. The
  formerly maximum-49 case now matches exactly, and all 40 preview pairs pass
  within one display value against a fresh GL reference. All 80 independent
  GL/native preview controls, 38 HDR post controls, three GPU contracts and
  staged SP/MP runs pass. The GPU motion contract covers both stored depth
  origins in 36 fixtures before/after restart. No prototype switch, image
  reflection or tolerance change remains. See [image orientation](vulkan-image-origin.md)
  and `.tmp/vulkan-gap-closure/framebuffer-orientation/checkpoint-v44-image-origin.json`.
- [x] Prevent native RGBA8 resolve rounding from producing different Vulkan
  color edges with combined 8x MSAA and SMAA. The affected source matches GL
  exactly; its display maximum drops from 59 to one byte. All thirteen raw
  and display pairs pass, with all 26 capture/recovery controls and 124 SMAA
  controls passing independently. Sixty of 62 SMAA image pairs pass within
  one byte; the two native HUD filtering comparisons remain failed.
  In that v40 snapshot, all eighty 8x PBR controls passed independently and
  39/40 paired images passed; v44 subsequently closes the separate 125%
  coverage failure as recorded above. GPU checks pass at 2x/4x/8x
  for fractional coverage and partial/repeated copies, alongside existing
  target/MRT/depth and HDR contracts. Both staged stock SP/MP runs pass with
  sixteen captures and no tracked renderer warnings. Broader Vulkan
  qualification stays open. Evidence:
  `.tmp/vulkan-gap-closure/ldr-resolve/checkpoint-v40-ldr-resolve.json`.
- [x] Prevent native-resolve darkening of bright supersampled Vulkan PBR
  preview edges by explicitly averaging stored samples before display clamping.
  The 150%/8x capture matches OpenGL exactly before SMAA and within one byte
  after SMAA. All 160 independent preview/recovery controls and 26 capture
  controls pass, as do the Vulkan HDR GPU contract, shader/header checks and
  both staged stock SP/MP runs with sixteen captures. The strict image gate
  passed 40/40 pairs at 4x and 39/40 at 8x in the v39 snapshot. The later v40
  and v44 changes above close its classic 8x SMAA and 125% coverage failures.
  Native HUD anisotropy and broader renderer qualification remain open.
  Evidence: `.tmp/vulkan-gap-closure/msaa-input-parity/checkpoint-v39-preview-average.json`.
- [x] Let renderer developers inspect individual RGBA8 render textures with
  `screenshot image` on OpenGL and Vulkan. All 26 independent capture controls
  pass through resizing, reloads, video restarts and HDR switches, including
  exact frame preservation and invalid-request rejection. All 38 existing HDR
  capture controls and four GPU contracts pass; shared HDR images remain exact.
  All four staged stock SP/MP profiles pass with 32 gameplay captures and no
  tracked rendering warnings. Existing stock-map warnings remain separate.
  The investigation retains the classic 8x color-edge SMAA failure and exposes
  a separate 150% HDR-off PBR preview difference under the unchanged gate.
  Broader rendering parity remains open. See the
  [capture contract](renderer-image-capture.md) and
  `.tmp/vulkan-gap-closure/smaa-color-parity/`.
- [x] Keep requested MSAA in supersampled OpenGL PBR scenes and recycle unused
  render targets instead of falling back after repeated resolution changes.
  Both GL allocation paths pass 39 lifecycle controls, with 326 retirements
  each, bounded cache size and exact recovery. All 124 independent 4x/8x SMAA
  controls pass; all five scale comparisons remain within one display byte.
  The native HUD and one classic 8x color-edge case still fail the unchanged
  full-frame comparison gate. All seventeen shared linear HDR pairs and
  eighteen display pairs are exact; four GPU contracts and 32 staged stock
  SP/MP captures pass with no tracked rendering warnings. Broader renderer
  qualification remains open. See [target lifetime](vulkan-hdr.md#supersampling-and-render-target-lifetime)
  and `.tmp/vulkan-gap-closure/presentation-parity/`.
- [x] Apply authored fog and blend lights once in eligible OpenGL PBR previews
  with HDR tone mapping off, including diagnostic colors and transparency.
  All 111 OpenGL/Vulkan image pairs at 0x/4x/8x pass the original full-frame
  two-byte gate with the default lighting-parity setting. Fourteen HDR controls
  retain correct authored radiance and transparent composition. Twenty-eight
  whole-scene GL 3.3/4.5 preview, fallback and restoration controls also pass.
  Both staged stock GL SP/MP profiles pass with sixteen 8x captures and no
  tracked rendering warnings. This closes the bounded fog/blend mismatch
  recorded by the earlier preview qualification;
  broader material admission remains open, with presentation work recorded above.
  See [preview details](vulkan-hdr.md#pbr-previews-with-tone-mapping-off) and
  `.tmp/vulkan-gap-closure/preview-fog/`.
- [x] Preserve bright Vulkan PBR preview edges with HDR tone mapping off.
  PBR radiance resolves in floating point while classic color limits remain
  intact; prepared transparency uses pipelines matching the temporary target.
  All 37 native preview/recovery controls pass at 0x/4x/8x, as do their GL
  counterparts, 24 GPU clamp/resolve fixtures before/after restart, 44 resource
  controls and sixteen staged stock SP/MP captures. Twenty paired preview
  images per tier passed the original gate at that checkpoint; seventeen
  fog/blend failures per tier are preserved and addressed by the later entry
  above. The SMAA comparison improves to
  27/29, closing all five HDR-off PBR edge failures; HUD and 125% MSAA-policy
  differences remain. This closes the bright-edge defect,
  not complete HDR-off PBR visual parity. See
  [preview details](vulkan-hdr.md#pbr-previews-with-tone-mapping-off) and
  `.tmp/vulkan-gap-closure/pbr-preview/`.
- [x] Align Vulkan multisample coverage with the measured classic OpenGL
  convention where programmable sample locations are supported, preserving
  depth through transitions and resolves and retaining native MSAA elsewhere.
  All 144 classic 4x/8x pairs pass (4x exact, 8x within one byte), as do two 2x
  controls and all 21 linear HDR 4x float/display comparisons. Native fallback
  controls, GPU validation and sixteen staged SP/MP 8x captures including
  reload/restart pass. Both 29-case SMAA/recovery sequences pass
  independently; at that checkpoint seven cross-API images were outside the unchanged gate:
  five HDR-off PBR previews, the HUD, and the differing 125% MSAA policy.
  See `.tmp/vulkan-gap-closure/msaa-samples/` and the
  [sample-location notes](vulkan-hdr.md#multisample-locations). Broader Vulkan
  material/platform qualification remains open.
- [x] Classic Vulkan colors and cutout opacity now match the fixed-function
  reference while preserving HDR vertex tints and PBR emission. RGB-only ASE
  colors have defined alpha, and shared ambient preparation handles modern-only
  lighting records without unnecessary fallback. All 324 analytic controls
  pass; ninety 0x full-frame pairs are exact. All ten previously failing classic
  SMAA scene pairs now pass. Bulk texture reloads also retain enough descriptors
  for a full image-cache generation, with fence-safe cleanup and exact GPU
  readback across 1,024 generation draws. Both validation runners reject the
  formerly missed skipped-draw warnings; final SP/MP reload/restart checks pass
  with sixteen 8x captures and clean Vulkan diagnostics. Historical failures,
  source/binary identities and the original failed HUD/MSAA comparisons are bound
  in `.tmp/vulkan-gap-closure/classic-colors/checkpoint-v33-classic-colors.json`.
- [x] Vulkan SMAA preserves world orientation with classic and PBR materials,
  including HDR, scene scaling, image reloads and video restarts. Graphics
  diagnostics recognize the native programs. All 174 GL/Vulkan laboratory
  views at 0x/4x/8x pass orientation/recovery checks, along with four stock
  SP/MP profiles at 8x. Eighteen 0x PBR scene pairs differ by at most one byte.
  The subsequent v33 work closes classic emission and supersedes the native
  stock reload results after detecting warnings missed by the original gate.
  HUD color and strict multisample cross-API comparisons remain open; this does
  not promote the renderer. Original evidence is retained in
  `.tmp/vulkan-gap-closure/scene-orientation/checkpoint-v32-smaa-orientation.json`.
- [x] Scalable HUD, extended-character and console fonts retain their pixels
  across image reloads and video restarts. Generated image callbacks restore
  the glyph coverage; repeated reload and partial/full restart controls on GL
  and Vulkan at 0x/4x reproduce the original HUD and atlas images exactly.
  Linear scene captures remain unchanged with the HUD enabled. The original
  missing-glyph/Vulkan validation failure is retained as negative evidence in
  `.tmp/vulkan-gap-closure/linear-capture/`.
- [x] Start level-editor modernization with a portable, source-preserving
  document and separate experimental workspace. `editorExperimental` opens entity/map/class
  search, an inspector, source-geometry preview, point-entity operations,
  undo/redo, protected saves and recovery. Pane widths persist; unchanged
  geometry is reused across property edits. Windows build/staging, 2,938 native
  assertions and Linux ASan/UBSan pass. The original document milestone also
  qualified 79 stock-source round trips. OpenGL and Vulkan workspace workflows
  pass with stock SP gameplay, in-game reopening, 1280x720/1600x900 captures,
  renderer restart, picking, rejected replacements and bounded pane resizing.
  Visual qualification remains partial: full video restart can leave labels
  missing on either renderer; reactivation is not a reliable OpenGL workaround.
  The known missing retail `burn_misc_sm` image remains separately recorded.
  Legacy Radiant source and its `editor` command remain unchanged; Meson still
  does not build that legacy tool. Experimental settings and recovery are separate.
  Native input, high DPI, docking and geometry/build tools remain open; this
  does not declare the idStudio target complete. Further development is paused
  at the user's request. Evidence: `.tmp/editor-workspace/closeout-gl/`,
  `closeout-vk/` and `closeout-document/`. See [the editor roadmap](level-editor-modernization.md).
- [x] Qualify native Vulkan linear PFM screenshots for completed admitted HDR
  scenes, preserving pre-post radiance and resuming the frame without presenting.
  Independent composition, lifecycle, public HUD/reload and GPU controls pass;
  all 21 final 0x GL/native float pairs pass. Four stock SP/MP 0x/4x profiles
  have passing evidence, including reruns of two cases interrupted by concurrent
  staging. The original failed batch remains recorded. The strict 4x cross-API
  silhouette comparison remains open; this closes the bounded export contract,
  not all HDR/PBR parity. Evidence and retained failures are bound in
  `.tmp/vulkan-gap-closure/linear-capture/checkpoint-v31-linear-capture.json`.
- [x] Qualify bloom and automatic exposure for admitted native linear Vulkan
  PBR HDR scenes. Prepared post resources, exposure-domain reset and orientation
  checks pass, and v30 closes all 33 GL/native post pairs without changing the
  two-byte gate. Sixty more composition pairs at 50/75/125% differ by at most
  one byte/channel. The game now preserves GL HDR highlights through its
  forward/resolve targets. Independent radiance, actual 4x MSAA, scaled lighting,
  CPU/GPU animation and restart checks pass, as do four Vulkan SP/MP 0x/4x and
  two GL stock gameplay profiles. The retained failures and source/module
  archive are bound in `.tmp/vulkan-gap-closure/scene-scale/checkpoint-v30-scene-scale.json`.
  This closes the bounded scene/post result; broader material, baked-lighting,
  portal/capture, post-effect and release qualification remain open.
- [x] Authored ambient lights now use the native PBR diffuse response and obey
  ordered transparent alpha. The 82 lighting captures pass 110 independent
  numerical/material/recovery checks; all 36 capacity and 88 resource controls
  also pass at 0x/4x, along with both 68-case direct-light regressions. The
  previous native binary fails the ambient lighting checks. Direct testing also
  corrected a background-dependent alpha predicate, retaining its old-binary
  reproduction and exact authored alpha in the isolated captures.
  The mixed-material GL reference declines modern ownership, so this does not
  claim paired scene or HDR parity. See [ambient lights](vulkan-pbr-ambient.md).
- [x] Make Vulkan PBR material diagnostics visible without direct or environment
  lighting. Both 62-capture suites pass channel-value, light-independence and
  data-layout checks at 0x/4x, and all seven original unlit views match GL.
  The 44 transparent resource/restart controls also pass at each sample count.
  Strict mapped-filter/cutout differences and broader diagnostic integration
  remain recorded in [material diagnostics](vulkan-pbr-diagnostics.md).
- [x] Eligible native Vulkan PBR scenes now use linear HDR composition with
  manual exposure. Classic and PBR lighting accumulate separately, then combine
  per MSAA sample before fog and ordered transparency. The complete output is
  prepared before ownership and finishes before authored post/HUD. Forty
  composition and 86 ambient/recovery captures pass at 0x/4x, including
  independent radiance checks and exact transparent failure/reload restoration.
  All twenty 0x GL/native full-frame pairs pass with at most one byte per
  channel difference. Twelve additional captures qualify actual FP16 resize,
  scaling and full restart at both sample counts; restored images are exact.
  The same staged module passes all five GPU cases and all four stock SP/MP
  0x/4x compatibility profiles, including the unchanged SP 4x temporal interval.
- [x] Implement bounded native Vulkan baked HDR lighting with complete receiver
  preparation and rollback. Baked diffuse respects PBR metallic/occlusion while
  retaining reflections; classic flat-normal receivers keep their own color
  domain. The final runtime passes 79 independent GL/native controls, both GPU
  resource/HDR contracts and staged SP/MP runs with sixteen engine captures.
  Reload/restart and late resource failure restore exact images. Full-frame
  comparisons still fail on emissive, reflection and cutout/coverage differences;
  HDR-off baked previews and broader grid/material combinations remain outside
  this scope. See [native baked HDR](vulkan-pbr-baked.md).
- [ ] Complete native PBR HDR scene ownership across the remaining material,
  baked-grid, portal/feedback and post-effect combinations. Those combinations
  still decline the new whole-view owner. Shared-setting toggles do not prove
  that the shared fog consumer executed. The v27 output GPU fixtures remain
  qualified, with 48 new per-sample boundary executions added in v28. Broader
  hardware/performance requirements remain open; v27's initial SP 4x
  resized-history failure is retained alongside the successful v28 repeat. See
  [Vulkan HDR](vulkan-hdr.md#remaining-acceptance).

- [x] Keep native PBR environment lighting active under the default light-grid
  setting on maps without baked grids. Analytic lighting, authored probes,
  metal, cutouts and transparency pass 60 GL/Vulkan captures and 40 exact
  toggle/restoration comparisons at 0x/4x; the prior binary fails four opaque
  controls. Bounded baked HDR support is now implemented separately;
  [complete baked parity remains open](vulkan-pbr-baked.md). See
  [native environment lighting](vulkan-pbr-environment.md).

- [x] Locally qualify native Vulkan authored LDR reflection probes. The shared
  selection and native atlas pass 46 Vulkan and 40 GL controls at each of 0x/4x,
  with all 80 paired comparisons passing. Capacity, source/resource failures,
  transformed receivers and image/video recovery are covered. HDR probe input,
  final integration regression and broader platforms remain separate gates.
  See [native probes](vulkan-pbr-probes.md).

- [x] Native Vulkan PBR transparency now applies opacity once over the complete mesh and replays each light with its original receiver coverage. Separate entities using the same model keep independent lighting. All 25 dedicated partial-light, instance and reload/restart controls pass at both 0x and 4x, with 12 additional room/environment controls and retained-reference comparisons passing. See [native environment lighting](vulkan-pbr-environment.md).

- [x] Native Vulkan transparency now checks its complete record bound before drawing, retaining consistent classic lighting and alpha when capacity is exceeded. All 18 capacity controls pass at 0x and 4x, including exact classic fallback, active native lighting at the limit and exact image/video restoration; the same image predicate rejects the old renderer. Shadowing transparent ownership remains open.

- [x] Native transparent PBR admission now survives the fog pass's drawing-state reset. Seven independent background/alpha, replay and restoration controls pass at both 0x and 4x. All 50 existing partial-light regression images remain byte-identical to their preceding references.

- [x] Native transparent PBR prepares coverage, direct lighting and environment resources before taking ownership. Failure restores speculative geometry/uniform allocations, descriptor ownership and retirement state, retaining a complete classic view. All 44 resource controls pass at both 0x and 4x, including exact fallback/recovery, late failure after earlier lights, actual descriptor rollback, reload/restart and environment-only views. See [native environment lighting](vulkan-pbr-environment.md). Broader resource-failure and platform qualification remain separate.

- [x] The Vulkan authored-probe source bridge reads complete immutable cubemaps with exact generation checks and sRGB/HDR decoding. GPU patterns, rejection controls and padded compressed-mip uploads pass around partial/full restart. Invalid image upload regions are rejected before GPU recording. Native atlas/blending qualification is tracked above; this resource work does not promote PBR or Vulkan. See [probe sources](vulkan-probe-sources.md).

- [x] Native Vulkan PBR adds filtered analytic environment reflections and diffuse irradiance once per eligible surface, including ordered transparency without direct lights. Local 0x/4x qualification passes 27 Vulkan and 26 OpenGL controls each, plus both 26-control comparisons. Image/video restoration is exact; independent cutout coverage and radiance checks reject the retained pre-fix shader. The investigation also fixes image reload of adopted MSAA targets, verified through public image creation and raw GPU readback. Authored LDR probes are qualified separately above; baked PBR diffuse and full scene color parity remain open. See [native environment lighting](vulkan-pbr-environment.md).

- [ ] Vulkan TAA now draws and consumes eligible rigid-object velocity with independent transform history, resolved-depth rejection and conservative fallback for incomplete geometry. Eighteen numerical GPU fixtures pass before/after full restart. All four SP/MP HDR/MSAA lifecycle cases pass the interval-based gate with 39-60 newly completed rigid-vector views per interval, allowing legitimate history resets. Moving-scene visual comparisons and broader temporal/platform qualification remain required; this does not promote the renderer.

- [x] HDR preserves bright texture detail and the composed portal sky. The affected stock scene is image-identical across PBR toggles. A smoother stock curve and Vulkan portal-view/depth ownership pass 224 production tone-map and 20 sky-mask GPU executions around restart, four stock SP/MP 0x/4x lifecycle cases, and frozen-scene regressions on both backends with unchanged runtime packages and no tracked warnings. GL's linear PBR tone curve is unchanged; broader HDR/PBR color parity remains open.

- [ ] The [PBR completeness audit](archive/2026-10-18/plans/2026-09-20-pbr-rendering-audit.md) now has independent BRDF/environment numerical tests and an original 24-station map. GL material channels, authored texture sampling, filtered environment lighting, HDR float/display references, moving/multiple shadows, MSAA/cutout coverage, reload/restart and resource-limit fallback controls pass locally. The two-joint animated specimen has byte-identical CPU/GPU pose, normal and shadow captures, including paused video restarts after cache-lifetime and bounds fixes. GL 3.3 HDR/MSAA qualification also passes after target-format and cluster-budget fixes. Scoped production admission now works without a forced parity override; HDR exposure, stock HUD, native fog/blend fallback and exact PBR restoration controls pass. Eligible linear HDR scenes now preserve PBR under authored fog/blend lights, with per-pixel radiance and transparency-order controls; GL 3.3/4.5 and 4x MSAA regression passes. Baked PBR diffuse/IBL separation, normal encodings, fog/blend/transparency composition, GL 3.3/4.5, MSAA and actual shader/image/video reloads now pass 81 map controls plus 15 renderer self-test markers on the v47 runtime. Broader mixed materials and portal/view-weapon grids, final stock gameplay/performance regression and native Vulkan PBR parity remain open; this is not a renderer promotion.
- [x] Repeated SP/MP console script commands bypass the include-file cache. The PBR moving/restored-caster proof executes multiple script commands and restores the original rendered image.
- [x] A narrow fixed classic bump/diffuse/specular path now preserves native lighting inside full-viewport, single-sample HDR PBR scenes without shadows or ambient lights. Four unclipped material references, baked-light composition, GL 3.3/4.5 reload/restart/resize and complete shadow/MSAA fallback controls pass. Video restart no longer leaves stale native shadow-caster arrays, and MSAA telemetry follows the executed owner. The [authoring guide](../user/pbr-materials.md) describes the qualified settings and limits; broader classic parity remains unpromoted.
- [x] Experimental Vulkan gains cubemap face attachments and depth-only render targets, selected-face restoration after post effects, complete-layer image resolves, and safe attachment-view retirement on resize. Retirement overflow keeps resources alive until their recording frame completes. The on-device test passes 66 exact color/depth captures, real depth-only draws, 4x MSAA depth resolve, invalid-face rejection, and a retirement-capacity stress case under Vulkan validation. Validation requires a positive layer/debug-messenger marker; missing required modules and empty selections fail. The runtime-directory override selects its own executable and keeps generated output outside the tested package.
- [x] Vulkan render targets now accept up to five color buffers within the device limit. Pipeline formats, clears, barriers, blend masks and resolves cover every attachment. GPU tests pass mixed RGBA8/RGBA16F outputs, unclamped float values, resumed scopes, cube faces, 4x MSAA color/depth resolves, resize, and invalid attachment/alias rejection without validation warnings. The startup gate requires this evidence; rectangular cube-target resizing is rejected before changing its images.
- [x] Native Vulkan PBR supports scalar, packed/separate material data, optional XYZ/RG/AGB normals, matching alpha-tested coverage and single additive emission. The v69 map suite passes 68 controls each at 0x and 4x MSAA for decoding, visible effects, ownership, specular footprint filtering, cutouts, independent emission references, exact fallback, restarts and point/projected shadows. Raw FP16 tests preserve faint channels and bound one emission write at both sample counts. Later v70 evidence closes native source-alpha ownership, and the analytic environment increment is tracked above. Authored probes, baked PBR diffuse, quantified temporal stability, arbitrary additive overflow and full scene color parity remain open; see the [PBR audit](archive/2026-10-18/plans/2026-09-20-pbr-rendering-audit.md).
- [x] Vulkan screenshot/partial-restart lifetime and hidden-window resize fixes pass stock SP/MP at 0x/4x on v63. Backend-owned MP scenes now render and resolve real MSAA, including after HDR/TAA are disabled, and diagnostics report the actual sample count. GPU attachment/MRT and 48 HDR fixture executions around full restart pass with active validation. The later highlight/portal-sky repair is qualified above; broader visual parity and Vulkan qualification remain open. See [Vulkan HDR](vulkan-hdr.md).
- [x] Vulkan window, surface, swapchain and mandatory-resource startup failures now recover to OpenGL in the same process, after renderer owners and callbacks are retired safely. The requested renderer preference and original failure log survive. All four injected failures, ordinary Vulkan startup and both earlier fallback drills pass locally; resource-failure SP/MP gameplay produces engine captures and exits cleanly. Renderer ABI v14 requires matching engine and both renderer modules. Later full `vid_restart` device failures retain next-launch recovery; hosted/platform qualification remains open.
- [ ] Linux x64/ARM64 push and ARM64 pull-request jobs now select mandatory lavapipe Vulkan startup/render-target and fallback checks. Hosted results, full Vulkan feature parity, physical GPU/platform qualification, repeated comparisons and promotion sign-off remain pending. See [Vulkan gap closure](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).
- [x] Bot-only effect/feedback queues no longer fill in busy multiplayer combat, and bot recipients skip network visibility work. Native delivery checks preserve human spectators, demo/repeater recording and the documented unreliable broadcast destination. This resolves queue-overflow warnings found during the bot audit.
- [x] Multiplayer bots retain valid routes when a replacement is blocked, release stalled and no-longer-useful goals, and stop using unseen enemy positions for combat movement/weapon range. Explosion-aware threat filtering catches nearby resting explosives. Visibility and pickup shortlists avoid redundant collision queries; the native fixture reduces 80-item snap queries from 81 to 11 and checks 240 perception cases. Windows x64 build/staging, eight existing bot contracts, native regressions and live stock DM/CTF gameplay passed. CTF exercised fetch, defend, escort and capture goals. Details and reproduction: [Multiplayer bots](mp-bots.md).
- [x] German is selectable as **Deutsch** in Game Options, with complete bundled menu, objective, terminal and gameplay-message translations. Retail German campaign dialogue remains supported; English-only installations retain English dialogue. Coverage, format arguments, choice ordering and German glyphs are checked alongside the existing localization contracts. Language changes force a deferred GUI reload so cached labels use the selected language. Player notes: [unreleased](releases/unreleased.md).
- [x] Windows x64 build/staging and German SP/OpenGL and MP/Vulkan gameplay checks passed. The runtime regression checks live language switching, visible menus and the translated Match Control view using engine screenshots. German retail voice playback still requires qualification with the complete retail pack; the available English dialogue fallback was exercised.
- [x] The TrueType faces were audited glyph by glyph against the retail atlases they are traced from, and repaired. The 17 Latin-1 symbols every face lacked and Chain's `!` are built from each face's own glyphs, and the bullet is no longer a missing-glyph box. Accented letters compose on the face's own letters (breve and ogonek from the weight-matched donor) instead of arriving whole from Noto, stacked accents no longer overlap, and ProFont's and the console sheet's squeezed capital accents are recomposed. The console face no longer draws `ß` as `á` (a retail cell slip), maps no C1 codes, keeps every glyph inside the cell the console clips to, and traces without stepped bevels. `tools/assets/fonts/audit_fonts.py` scores a build against the atlases, and `tools/tests/ttf_font_integrity.py` guards the shipped files in CI. Details: [TrueType font system](ttf-font-system.md).
- [x] Brazilian Portuguese, Czech, Hungarian, Turkish and Ukrainian are selectable as **Português (Brasil)**, **Čeština**, **Magyar**, **Türkçe** and **Українська**, with complete bundled code, menu, map, map-pack and openQ4 tables; dialogue stays English. Turkish uses Windows-1254, Hungarian Windows-1250 and Ukrainian Windows-1251 for the console and legacy paths, first launch follows the OS language, and Czech, Hungarian, Turkish and Ukrainian always use the scalable fonts. Each translated table is checked against English for keys, order, format arguments, escapes, choice lists, endonyms and drawable signature letters; the retained gate fits every loading tip and server card, and the SYSTEM page native fit tests pass for all five, run locally with their 1.4x glyph-width margin (CI samples seven other locales). Player notes: [unreleased](releases/unreleased.md).
- [x] Windows x64 build/staging passed. Each new language passed the SP/OpenGL localization smoke on airdefense1 (gameplay, live switching to English and back, Settings and the language chooser) and the MP smoke on q4dm1 (translated Match Control; Turkish on Vulkan), checked with engine screenshots.
- [x] Restaging openQ4-game sources is incremental. The stage is now synced in place, so
  staged files whose bytes are unchanged keep their timestamps and ninja no longer
  rebuilds every game object after a restage. On Windows x64, editing and then reverting
  one multiplayer game source recompiled only that game object and relinked both `game-mp`
  modules; the other 3,378 staged files kept their timestamps. Deleted sources leave the
  stage and its manifest, and a link inside the stage is removed rather than written
  through. Edits to save-relevant sources no longer rebuild every game object: the
  savegame source stamp now stays out of the game precompiled headers, so adding a comment
  to `src/mpgame/Item.cpp` recompiled only that file, both `SaveGame.cpp` files and the
  engine's `Session.cpp` before relinking.
- [x] A Meson reconfigure, which every restage triggers, no longer re-packs the unchanged
  openQ4 packs. Meson follows each reconfigure with `ninja -t restat`, and a pack left
  unwritten because its bytes had not changed kept a timestamp older than its pack
  scripts, so ninja rebuilt the 800 MB `pak1.pk4` after every restage (two to five minutes
  on Windows x64). An unchanged pack now takes the time its build step started, and
  identical staged copies take that timestamp too, so the multiplayer smokes still see a
  fast-staged `.install` as current. Across two reconfigures on Windows x64 neither pack
  was rebuilt and `FileSystem.cpp` did not recompile; only the pack checksum header was
  checked again, in about 1.5 seconds.
- [x] GUI `transition` scripts now read a `$window::var` operand when the transition
  starts, as retail Quake 4 does, instead of copying it when the GUI loads. Two stock
  multiplayer HUD effects now play as they did in retail: the aimed player's name fades
  out over half a second toward the last target's team colour instead of vanishing at
  once, and the One-Flag CTF flag flashes toward the carrier's team colour instead of
  white. A `$window::rect` operand is still copied at load, which the flag pulses need.
  The demo menu's filter buttons now fade back on mouse exit as written. An audit of
  every `$` operand in the 264 stock, 33 openQ4 and 60 Awakening GUIs found no other
  change in behaviour, and the saved-game layout is unchanged: a checkpoint saved by the
  25 September build still loads. The rule was confirmed in the 1.4.2 `Quake4.exe`;
  `gui_transition_operand_contract.py` pins it, and a hidden client showed the stock aim
  fade and flag colour with no input.
- [x] Arena One Flag CTF now shows the One Flag CTF HUD: both team panels with their
  scores and the neutral flag, instead of two empty deathmatch rows and no team score
  anywhere. Retail Quake 4 never let the mode be selected, so its HUD had no layout for
  it. In both One Flag modes the neutral flag now reports on the HUD: "!" and a flash
  toward the carrier's team colour when it is taken, "?" when it is dropped, and neutral
  again when it returns to base or is captured. The game had never sent the HUD those
  events for the neutral flag, so the colour flash above could not appear in a match
  until now. A player who spawns while the flag is carried or dropped sees its real
  state. In CTF and Arena CTF, a Marine's HUD no longer keeps pulsing a dropped Strogg
  flag; the retail HUD stopped a window that does not exist. Duel keeps the deathmatch
  leader and own-score rows on purpose. A hidden client with no input checked Arena One
  Flag CTF and One Flag CTF on stock `q4ctf1` (a Marine and a Strogg carrier, a drop,
  the 30-second return, a respawn while the flag lay dropped, and a capture by each
  team) and a CTF drop, through HUD event logs and screenshots, with no new warnings.
  The game-side half is openQ4-game `de5ec71`.
- [x] The Game Options crosshair preview now picks the custom crosshair, as in retail
  Quake 4: click it for the next of the twenty designs, right-click it for the previous
  one, wrapping at both ends. The row did nothing before: the engine never answered its
  `chooseCrosshair` command, never showed the current crosshair in it, and never ran a
  menu's right-click action (`onBackAction`). A crosshair colour or custom crosshair
  change now shows in game at once on every platform. Before, it waited for the next
  weapon change or respawn, because stock re-applied it on the Xbox 360 only. A hidden
  client with no input checked both on `airdefense1` with the SP launch profile. The
  preview stepped forward and back and wrapped at both ends. Colour and custom
  crosshair changes showed on the next frame with no weapon change. The right-click
  dispatch is pinned by `crosshair_picker_contract.py` and matches the 1.4.2
  executable; it was not run, since that needs a real click. The new
  `openq4_guiAction` command runs a menu window's click or right-click action for such
  checks. The game-side half is openQ4-game `4d87dbd`.
- [x] The VS Code Run and Debug list is grouped, with a separator between main menus,
  menu variants, the Quake 4 and Awakening campaigns, campaign test variants,
  multiplayer by game mode, the Community Map Pack and the Awakening's multiplayer
  maps. Every launch now runs the retained (RmlUi) interface whatever `.home` has
  saved; new Stock UI entries compare against the stock screens. Launches no longer
  name a Quake 4 install path that warned on every start where it did not exist; the
  engine's Steam/GOG discovery finds the game. `launch.json` is generated by
  `tools/debug/generate_vscode_launch.py`, and `vscode_fast_build.py` fails when the
  two differ.
- [x] The Codex app's actions run their entries again. They picked VS Code tasks and
  launches by position, so every launch action had drifted onto another entry; they
  now name the task or launch, and `vscode_fast_build.py` fails when one no longer
  exists. Their runner also failed on every entry before running anything, the
  worktree setup script included, and would have stopped a dependency chain after
  its first step. The actions cover every task, plus the main menus and their
  variants and the first level or map of each campaign and stock multiplayer mode,
  on both renderers; a launch action lasts until the game exits.
- [x] Bring BSE effects into line with the retail 1.4.2 executable after an audit of
  spawn domains, segment timing, particle simulation and rendering, envelopes,
  lights, sounds and effect ordering against its disassembly. Electric arcs regain
  their forked look, looping map effects start in full flow, long-lived envelopes
  keep their speed, and missing tables run at full strength again. Looping effect
  sounds no longer stack after each savegame load: the client effect owns its
  emitter again, as in SDK 1.4.2. `bseStats` and `bseLog` are back. Retail quirks
  kept on purpose, divergences kept and the evidence are in the
  [BSE audit](archive/2026-10-18/quake4-bse-research-and-implementation-plan.md#retail-parity-audit-2026-10-02);
  `bse_retail_parity.py` pins the corrections. Evidence:
  - fixed-pose captures, before and after;
  - a save/load voice count (1, 2, 3 before; 1, 1, 1 after);
  - a six-bot multiplayer match;
  - the related contract tests.
- [x] Effect sounds pick a random variation again, as in retail 1.4.2, so bullet
  impacts, ricochets and rocket and grenade explosions no longer repeat their first
  sample. Emile Belanger found the fixed diversity first
  ([emileb/openQ4@71af0c11](https://github.com/emileb/openQ4/commit/71af0c115f4e99fd1a9b5d49bd74d5eba6eb9413)),
  and `bse_retail_parity.py` now pins both sound starts.
- [x] Match Control's team join and the player's own team agree again. Joining a
  side from Match Control (the stock page and the retained card send the same
  `team_join_*` token) committed the session's side. `idPlayer::UserInfoChanged`
  then ran the legacy `VerifyTeamSwitch`, whose `si_autobalance` rule (on by
  default) kept the player on their team. A casual session copied that team
  back, so the result read Committed while nothing moved. The team-change
  continuation now applies the same rule after the team core admits the join and
  before anything commits. It refuses an unbalancing join with the appended
  protocol reason `MP_MATCH_PROTOCOL_REASON_TEAM_BALANCE` ("Auto Balance Teams:
  that team has too many players.", `#str_42391` in all twelve languages,
  short enough for the retained card's reason column); a roster seat exempts
  its holder. A side the match layer has
  committed is published without being balanced again, so roster acceptance,
  substitution and the session's mirrors land too. Evidence, from hidden listen
  servers with bots in a Team DM warm-up and console verbs only:
  - the reported case (Player and a bot Marine, two bots Strogg): the stock Join
    Strogg button is refused with that reason, and the player's team, the
    session's side and its revision are unchanged; with `si_autobalance 0`
    the same button moves the player and the session to Strogg;
  - a remote client is refused with the reason in its own result line, then
    moves on the server and in its accepted view once a Marine bot leaves;
  - under `competitive_tdm` bots sit out, and the join applies;
  - `mp_match_control_team_join_smoke.py` passes, and fails on a pre-fix
    runtime (revision 19 -> 21, "Committed" while the player stays Marine);
    `competitive_match_layer.py` pins the order, and eight mutants fail it.
- [x] Map loads no longer drop just as they finish with "idRenderSystemLocal::UnCrop:
  currentRenderCrop < 1". ExecuteMapChange ends with StartWipe, which pushes a
  render crop, draws the loading screen and copies it into `_scratch` outside
  UpdateScreen while every print still offers PacifierUpdate a loading-screen
  redraw. A print inside that capture (fs_debug's "Can't find _scratch" from the
  level-load cache's image lookup, a developer DPrintf, a non-precached decl
  warning) ran the redraw once the pacing interval had passed. Its BeginFrame
  reset the crop stack, the capture copied the whole window, and StartWipe's
  UnCrop dropped the finished map. `sessionCaptureDrawGuard_t` now holds
  `insideUpdateScreen` across the wipe capture and the save preview's crop, so
  neither PacifierUpdate nor refresh-on-print can begin a frame inside a
  capture. The other crop pairs never meet such a redraw: the game's objective
  camshot fires 250 ms after spawn but a load runs only 160 ms of settle frames
  (and no stock map uses it), and the renderer's subview, demo and
  resolution-scale crops run inside a guarded draw or outside a map change.
  `loading_wipe_capture_crop_contract.py` runs the production StartWipe,
  PacifierUpdate and UpdateScreen against a renderer with a real crop stack and
  checks that an unguarded StartWipe still drops. Evidence, hidden-window SP
  loads of game/airdefense1 with fs_debug 1 and developer 1, one at a time:
  the previous build dropped 5 of 5, the fixed build 0 of 11, and every run
  printed "Can't find _scratch" inside the capture.
- [x] `bakeLightGrids` captures whole cube faces at any window size. Its
  capture view took the window's pixel size as its rectangle, but render-view
  rectangles are virtual-screen units: `RenderViewToViewport` scales them by
  the capture crop (`tr.tiledViewport`) over 640x480. Outside a 640x480 window
  each face drew into a larger viewport (128x107 for a `size64` face at
  1280x800) and the readback kept its top-left corner, stretched over the
  face. The synchronous and async PBO paths share the view, which now spans
  `SCREEN_WIDTH` x `SCREEN_HEIGHT` as levelshots, `envshot` and
  `bakeReflectionProbes` do; `renderer_screenshot_readback.py` pins all three
  and models the viewport. On mp/q4dm8 with the shipped settings (`size128
  samples128 bounce3`), fixed bakes at 1280x720, 1280x800 (async and
  synchronous readback) and 2538x1312 match the 640x480 bake to run-to-run
  noise (mean difference at most 0.009 levels, 99% of texels identical), where
  the unfixed build was off by 3.3-3.6 levels (r 0.62-0.77). Whole faces draw
  more of the map, so a bake takes about twice as long (q4xtourney2: 1.6 to
  3.2 minutes).
  - The bundled light grids carry cropped faces. 37 multiplayer grids match
    bakes logged on 2026-06-19 in a 2560x1334 window, and the unfixed build
    reproduces the shipped atlases at those windows: q4xtourney2 at 2538x1312
    (r 0.9996, mean difference 0.8 levels, against 9.4 for a correct bake) and
    q4dm8, from the unlogged 2026-06-18 batch, at the default 1280x720 window
    (r 0.997). airdefense2 shares that batch. They stay as shipped for now
    (decided 2026-10-05); the next entry fixes the two faults that kept a
    re-bake of them from being correct.
- [x] A light-grid bake releases every capture and builds on its own output,
  so maps that ship light grids can be re-baked correctly and large maps
  finish. Captures ran BeginFrame and their back end inline but never reached
  EndFrame, so frame memory grew about 300 KB a face (`r_showMemory 1`
  counted 3.5 GB after q4dm8's 11,340 faces, enough to wrap its 32-bit total;
  airdefense2 ran out of memory), frame-temp vertex data stayed live and the
  upload stream never fenced a buffer before a later capture reused it. Each
  capture now ends with EndFrame's release calls, without presenting
  (`LightGrid_FinishCapture`). openQ4 searches `fs_savepath` last, so a bake
  that read its atlases back by name got the copies `pak1` ships: re-baking
  q4dm8 lit bounces 2+ with the shipped grid and packed the shipped
  irradiance byte for byte. The bake now reads its atlases and
  `separateAreas` metadata from the save path (`LightGrid_ReadBakedTGA`,
  `LoadLightGridFile( path, true )`), uploads them in the format a by-name
  load derives, gives areas it wrote nothing for no images, and warns when
  another copy of the pack will load first. `lightgrid_bake_contract.py` pins
  both and round-trips the TGA reader in a compiled harness; mutants of each
  fault fail it. Evidence, with the new renderer module:
  - game/core2, 6,551 probes and about 118,000 faces: private memory held at
    3.1 GB from the map load to the end, and frame memory never passed one
    frame's 3.6 MB;
  - game/core2 at a coarser grid: during its 1.5-minute bake the previous
    module grew from 3.0 to 9.9 GB while the new one stayed at 3.2 GB, and
    with no shipped copy to shadow the atlases the two bakes match (99.97% of
    texels identical, largest difference 4 levels);
  - game/airdefense2 at the shipped settings, which ran out of memory before:
    all 14,041 probes in 23.5 minutes at a steady 3.1 GB. Its pack holds this
    bake's irradiance (1.4 levels from the baked atlases, the DXT1 error,
    against 9.6 from the shipped pack), and the bake warned that the shipped
    pack still loads first. The shipped grid is dark by comparison: about
    9,600 of its 14,041 valid probes are black, against about 150 in the new
    bake.

## Unreleased — Quake 4: The Awakening (`q4xbase`)

- [x] The three speeder races keep their clocks, carried time and objectives
  in step with progress. Completed legs clear the timer and fill the progress
  display; late callbacks cannot restart, fail or reduce the progress of a
  finished leg. A missed gate disables driving and firing during the failure
  screen, and late checkpoints cannot complete a failed race. The first
  lockdown retains its radio warning before starting the clock.
  Bomber attacks clean up the correct aircraft, and two previously broken
  flyer destruction sequences now work. Shooting down a bomber interrupts its
  volley cleanly and ejects one pilot, including when saving during the
  explosion; late callbacks no longer leave orphaned attack entities.
  Restart the current race leg to use these script corrections; existing saves
  retain their original mission scripts. Boosting also stops when the rider
  leaves or the bike is disabled, without resetting its recharge time. See the
  [M07 audit](awakening-m07.md) for validation and remaining playthrough limits.

- [x] The unreleased expansion's campaign loads on openQ4 from its own
  content and openQ4's binaries, never its leaked game DLL. Its game code is
  rebuilt as the new `openQ4-game-awakening` game-library layer: every class
  its maps and weapons need (fire volumes, the Walker, Retch, Tank, Pain Lord
  and Valkaryne, the objective beacon, the goob, freeze and spike guns with
  burning damage over time, the speeder bike and its boost, and the turret
  cockpit's pulse cannon, missile launcher and homing rockets). All 13
  campaign maps load, spawn every entity and shut down cleanly in headless
  runs; a burning Strogg takes its damage once a second until it dies. The
  freeze gun slows and freezes enemies solid before they shatter, and the
  expansion's turrets track, tighten their aim and sweep as its maps expect.
  The spike gun's spikes stick where they land, stay in the enemies they kill
  and pin the corpses to the wall behind them, and the scripted fights keep to
  their intended targets. Games saved on every campaign map load.
  See the [support plan](archive/2026-10-18/plans/q4x-awakening.md).
- [x] The expansion's multiplayer: its buy menu works (`sq_buy`, bound to `b`),
  with its own prices and its new items: the core cannon (spike gun), fire
  cannon (goob gun), freeze gun, the nine weapon mods (each for the weapon you
  carry) and FC Armor Regen, a team special that builds armor past its maximum.
  Its DeadZone tokens and team powerups keep working under its renumbered
  powerup list, and Adrenaline lends up to 400 health that drains back over
  its 40 seconds. Stock `baseoq4` buying, prices and powerups are unchanged.
- [x] Engine support the expansion's content needed: mods on the retail base
  keep openQ4's runtime content, a mod's same-named declaration files no
  longer hide the retail definitions beside them (`decl_layerModFiles`),
  version 2 collision models, DXT3 textures, `.jpg` textures shipped as
  `.tga`, `sound/music/` as music, the menu's cvar value commands, team maps
  marked `TeamDM` in the create-server list, the CTF maps' levelshots in the
  create-server and server-browser previews, and `_altattack` as the
  zoom/alternate-fire button. The Meson option `awakening` builds the layer
  when its repository sits next to the openQ4 checkout
  (`../openQ4-game-awakening`) or `OPENQ4_AWAKENING_REPO` names it.
- [x] Release packages include the expansion's game modules, so it runs from a
  normal openQ4 download. Windows and Linux packages, the Windows installer and
  the Linux AppImage carry a `q4xbase` folder beside `baseoq4`; the macOS app
  carries it inside the bundle. The expansion's content is not included: with
  its `q4xbase` folder beside `q4base` in your Quake 4 installation, start
  openQ4 with `+set fs_game q4xbase`. openQ4 never loads the expansion's own
  `gamex86.dll`. Crash reports from the new modules symbolicate: Windows
  packages carry their `.pdb` files, and the Linux debug-symbol and macOS dSYM
  archives cover them.
- [x] A mod's own build of a map that shares a stock map's name no longer picks
  up the lighting data openQ4 ships for the stock map. The expansion carries its
  own builds of seven post-release multiplayer maps: `q4xdm13` and `q4xdm14`
  failed to load and returned to the menu, and `q4xctf6`, `q4xdm10`, `q4xdm11`,
  `q4xdm15` and `q4xtourney1` were lit with lighting made for the retail
  geometry. They now load with the game's standard lighting, and the stock maps
  keep theirs, under the expansion too.
- [x] The speeder bike drives the way the expansion built it. A held turn
  settles at about 85 degrees a second and stops within a second of letting go;
  it spun up past 500 degrees a second and kept turning two full circles,
  because Quake 4's friction ceiling of 1 dropped the bike's angular friction
  of 5 (the expansion's ceiling is 10; openQ4 raises it for the bike alone, and
  older saves pick it up on load). The bike also reaches race speed (about 580
  units a second after three seconds of thrust on open track, where it crawled
  about 25 units): its grip turned the velocity by twice the bike's heading
  every tic.
  Its pads honour `traceRelativeDirection`, so the antisuspensor holds it off
  ceilings and the bumpers off walls instead of all probing the ground. Stock
  vehicles are unchanged: convoy1's GEV moves identically under a scripted
  drive and turn. `awakening_speeder_physics_contract.py` pins the three fixes.
- [x] Every unique Awakening monster, weapon and vehicle plays as its content
  was built to, checked in headless runs against the expansion's code (the
  [plan's Phase 5](archive/2026-10-18/plans/q4x-awakening.md)). The m09 Valkaryne docks behind
  the Makron sphere through her `requestDocking` script, which nothing ran, and
  undocks to fight on foot when the sphere dies. `ignoreplayer` holds on every
  enemy path, so the m01 death-ray turret never takes the player. Pain Lord
  headshots reach his head zone, which his legs zone swallowed. Retches walk
  on `aas48` where a map lacks a usable `aas96`. The elites' gravity well,
  spread fire and grenades, the space flyers' `canturn`, `noFaceEnemy` and
  master pitch and roll, the Tank's damaged-launcher accuracy and break
  effects, the goob gun's burst, burn stacking and alternate flash, the spike's
  impact force, the cannons' `useGodMode`, `resetOnExit` and jam lock, m03's
  script FOV and m07's `velscale` slow-down volumes now do what their keys say.
  Stock content is unchanged: the new keys appear only in Awakening defs and
  maps, and the jam lock defaults on only under `q4xbase`.
  `awakening_gameplay_contract.py` pins the fixes.
- [x] A vehicle passes script `guiEvent` and `setGuiParm` calls to its HUD as
  well as its world screens, as the expansion's `rvVehicle` did, so m07's race
  clock counts down on the speeder bike's HUD and m06's MCC health shows on the
  MCC cannon's; retail's vehicle HUDs handle none of what retail scripts send.
  Vehicles also write `vehicle_speed` to their own screens (the speeder bike's
  speedometer) and `playerYaw` to their HUD, as the player HUD now does too (the
  m03 ship panels' view cones), and the cursor gets `npc_talkstate` (the
  "wait" talk crosshair). Awakening spline movers that face along their spline
  bank into its turns (`g_splineRollLookahead` 500, `g_splineRollMultiplier`
  2.0, the expansion's defaults), so m03's and m06's fighters bank; retail's
  movers never did and do not. The goob gun's alternate fire holds the player's
  `fire_alt` pose, and m09's non-quad flares warn once per material instead of
  about 340 times a second. Audits of the expansion's frame commands, actions,
  script events, states and single-player GUI state find nothing else missing.
  Player-facing summary: [The Awakening on openQ4](../user/awakening.md).
- [x] The pulse cannon's barrels move as the expansion's code moved them: a
  shot kicks the firing barrel back and it slides home, and both fold out and
  down while the missiles are selected. Unselected vehicle weapons now get a
  per-frame call, `rvVehiclePart::RunInactivePostPhysics` (empty by default, so
  retail vehicles are unchanged), through which the cannon cools and the
  missile launcher reloads while the other weapon is selected, as in the
  expansion. Retail's cinematic harvester, which m07's races use as a
  background monster, no longer tries to turn in place without turn
  animations: it logged a warning every 250 ms (158 in one race) and skipped
  that think's attack checks; retail's combat harvesters, which have the
  animations, turn as before. A probe of m07_race1 to the end of m09 found the
  races' timed lockdown gates fail the mission by design, and only content
  holes past them (recorded in the support plan).

## Unreleased — `idtech5-ui` development

- LOAD GAME on the title screen and the pause menu opens a rebuilt Load Game
  page over the menu you came from: your saves, newest first, with their
  level, and each save's screenshot with when it was made. Press a save
  again, or LOAD GAME, to load it; DELETE GAME asks first and then selects
  the next save. A save that cannot be loaded now says why instead of doing
  nothing, and with no saves the page offers NEW GAME. The title's CONTINUE
  line shows the save's name, level and time cleanly. Set `ui_retained 0` for
  the classic page.

- SAVE GAME on the pause menu opens a rebuilt Save Game page in the same
  style: your saves, newest first, and a name field ready for typing. Type a
  name and press Enter, or SAVE GAME, to save; pick a save to put its name in
  the field and save over it after a confirmation that names it. Saving over
  an autosave, or saving when the game cannot be saved, now says why on the
  page. Set `ui_retained 0` for the classic page.

- Number fields on the modern SYSTEM page, such as the window and custom
  size fields, now take typed digits as well as pasted values.

- RESTART LEVEL on the pause menu opens a rebuilt page with the five
  difficulties and their descriptions, the level's own lit. Pick one and
  press RESTART LEVEL; Back keeps the difficulty you were playing. Set
  `ui_retained 0` for the classic page.

- DEMOS on the title opens a rebuilt demo library: your recordings newest
  first with their map, date and length, filter tags for each kind, and
  PLAY, DELETE and REFRESH. Set `ui_retained 0` for the classic browser.

- MODS on the title opens a rebuilt dialog listing your installed mods with
  their version, author, release date and folder; LOAD MOD switches to the
  selected one.

- Dying in single player now brings up a rebuilt restart screen: RESTART,
  LOAD GAME, MAIN MENU and EXIT GAME, each explaining itself as you point
  at it.

- CREDITS on the title plays the original credits, rebuilt: the Raven logo,
  then every screen translating from Strogg runes under its scan bars.

- ARENA on the single-player page opens a rebuilt Arena page: the five tiers
  as a ladder beside the chosen tier's matches, the boss on its own larger
  plate, the match's levelshot and rules, the difficulty and START MATCH,
  with the original entrance as a match starts and the match report when
  you come back. The bumpers switch tier.

- MULTIPLAYER on the title opens a rebuilt Multiplayer page: JOIN GAME,
  CREATE SERVER and PLAYER SETUP, each opening its page and coming back to
  the Multiplayer page, and BACK to the title.

- Create Server now starts Duel, Clan Arena, Freeze Tag, Red Rover, One Flag
  CTF and Arena One Flag CTF when you pick them from the main menu. Before,
  those modes quietly fell back and the server started as Deathmatch.

- Create Server is rebuilt in the modern interface: CREATE SERVER on the
  Multiplayer page slides one level down to it, with the maps the game type
  plays beside the chosen map's picture, every server option, and the
  advanced options, including the server and remote console passwords
  (shown as asterisks, never copied) and your server's ban list, where you
  can lift a ban.

- The classic Create Server page's ban list shows your server's bans again
  and REMOVE BAN lifts them, and its Min Players and Max Players stay in
  order, as in the original game.

- A favorite server running on your own machine, such as a local dedicated
  server, now appears in the server browser's Internet list. Before, the
  browser saved it as "localhost" and never contacted it. Favorites saved
  that way are picked up again automatically.

- The server browser is rebuilt in the modern interface: JOIN GAME on the
  Multiplayer page slides one level down to it. It lists Internet or LAN
  servers in sortable columns, marks favorites, passworded and dedicated
  servers and Q4TV repeaters, filters by mod, shows the chosen server's
  levelshot, adds or removes favorites, joins by IP address (showing your
  own) and finds a player's servers.

- FIND PLAYER/CLAN in the classic server browser works again: it now shows
  only the listed servers with a player whose name contains what you typed.
  The original searched through id's master server, which no longer answers.

- JOIN IP ADDRESS shows your IP address again, as the original did.

- Both server browsers show the scan's progress again: the bar under the list
  fills as servers answer, with the current step (getting the list, scanning
  server n of N, then how many were scanned). The classic browser's bar had
  always stayed empty in openQ4.

- The classic server browser's server card works again: pointing at a server
  shows its address, its players and its settings (map, time limit, the
  match's frag, capture or round limit, team damage, team autobalance, pure
  and spectators). openQ4 had kept the card hidden.

- The rebuilt server browser shows the same server card: point at a server,
  or move to it with the keyboard or a controller, to see its address,
  players and settings beside it.

- The rebuilt menus draw their vector art from less geometry, so each frame
  costs less.

- The rebuilt server browser shows each server's connection quality as bars
  beside its ping, from four green bars on a fast server down to one red
  bar.

- With a controller, the rebuilt server browser refreshes the list with the
  left face button and sorts it by the next column with the top one.

- On a Nintendo-layout controller, menus confirm with A and go back with B,
  as Switch games do, and the prompts show it. `in_swapConfirmBack` chooses
  either way for any controller (see the input guide).

- Menu prompts show your controller's buttons. After you use a controller,
  the title, pause and loading screens and the multiplayer menu show its
  buttons instead of keyboard keys: A and B on Xbox pads, the cross and
  circle on PlayStation, Switch labels, or the Steam Deck's own. The
  multiplayer menu's tab keys show the shoulder buttons. The keyboard or
  mouse switches them back.
  If Steam Input or another remapper hides your pad, `in_promptStyle`
  chooses the style for the menus and the HUD (see the input guide).

- The server browser's MOD filter label reads correctly in French and
  Italian; it showed a raw text key before.

- The rebuilt menus no longer leave you stranded: Back on Single Player
  returns to the title instead of an empty menu; Back on the death screen no
  longer drops you out of the level; the death screen's LOAD GAME opens the
  new Load Game page; a multiplayer join that fails or is cancelled brings
  you back to the server list instead of the title; and CONTINUE with a save
  that cannot be loaded opens the Load Game page and says why instead of
  doing nothing.

- Dropdowns change with left and right (arrow keys, D-pad or stick), as the
  original menus' options did. On the multiplayer Welcome card the stick no
  longer keeps moving the focus after you let go.

- Create Server's ADVANCED OPTIONS button sits under the server options
  again instead of over the page title, UPDATES checks for a new version
  without leaving the title, and menus keep their sound after you switch
  windows or the video restarts.

- Menu text fits in every language: the title's links, the MODS dialog and
  the quit dialog's title no longer run off their frames, the SELECT prompt
  is translated correctly in Spanish, Italian and French, and the pause and
  loading screens name the Warlord difficulty and openQ4's own multiplayer
  modes in your language.

- The rebuilt multiplayer menu cards and server browser use far less CPU
  while you are on them.

- Controllers and keyboards start on the right item: the title, the pause
  screen, Single Player, Campaign and Multiplayer open with their main
  choice selected, so the first press of A or Enter does something, and
  Back returns you to the item you came from. With the mouse, only the item
  under the pointer lights up.

- Every rebuilt menu page now shows your device's buttons. Load Game, Save
  Game, Restart Level, Demos, Credits, Create Server, Join Game
  and the pause menu's Objectives page carry the same prompt bar as the title
  and pause screens: Select and Back as keyboard keys, or as your
  controller's buttons once you use one. Back sits where the old BACK button
  was, and you can still click it. On wide screens the Single Player,
  Campaign and Multiplayer prompts no longer run past the bottom frame.

- The mouse now works on every button of the rebuilt Load Game, Save Game,
  Restart Level and Demos pages. LOAD GAME, DELETE GAME, SAVE GAME, the
  difficulties, PLAY, DELETE and REFRESH
  ignored the pointer before (the keyboard and controllers always reached
  them), and pointing at the lower edge of a difficulty no longer lights the
  one below it.

- When a selected button is switched off or a server row disappears, the
  next move picks up from where the selection was instead of jumping to the
  top of the page, and Back on a number field you have not changed leaves
  the page on the first press.

- Long server and map names in the server browser end in an ellipsis
  instead of being cut mid-letter. Spanish shows the same word for Back
  everywhere, and Polish and Russian translate the save names (Quicksave,
  Checkpoint, Map Start), save warnings and dialog buttons that still
  appeared in English.

- The title screen appears sooner after launch. Each rebuilt menu is now
  read once instead of twice, partly on spare processor cores; the pause
  menus and the pages only a level reaches load with your first level; and
  the multiplayer and Arena map pictures load while the title is up.

- Changing levels and multiplayer maps is quicker with the modern menus on:
  their pages now keep what they have loaded through a level change instead
  of being rebuilt every time.

- Multiplayer maps load faster and the in-match menu is smooth again: the
  modern Escape menu, which loads with every multiplayer map, now loads about
  fifteen times faster and draws its frames in a fraction of the time, and
  the Welcome card, title and pause menus are lighter too.

- The modern interface is now on by default for the title screen, the
  single-player pause menu and the loading screens; the other menus keep the
  classic look for now. Prefer the classic screens throughout? Enter
  `ui_retained 0` in the console; the choice is saved. The modern SYSTEM page
  stays opt-in (`ui_retainedSystem 1`) until it offers every setting of the
  classic one, so no setting goes missing from the menu. When a modern
  screen's content is missing or can't load, or a mod brings its own main
  menu or loading screen, that screen shows the classic one or the mod's
  own instead, so a click never leads to
  a blank or unresponsive screen. The new screens follow the visual
  specification's framing bands, lighting and motion, and hand off to the
  stock pages for anything they do not replace yet. Continue now shows the
  picture of your latest quick or manual save as well as autosaves, and the
  pause menu's level card shows the same picture as the level's loading
  screen instead of the generic art. The card now lists the objectives you
  hold and how long you have been in the mission, and since your last save.
  OBJECTIVES on the pause menu opens a page with every objective you hold,
  its picture and briefing, and the ones you completed on this level; the
  level card marks those with a check. Once Kane has been
  stroggified, the pause menu turns Strogg: an orange frame with angular
  shoulders and circuit traces, Strogg lettering, and labels that decode from
  Strogg runes as the menu opens (shown at once with reduced motion).
  Single Player's Campaign now opens one level down: the frame steps one
  notch along, SINGLE PLAYER moves up into the frame as the path, and Back
  climbs out again, or reverses the move if pressed on the way in.
  Choosing a page carries its name up into the page title as the frame docks,
  and the title screen's backdrop leans slightly away from the pointer or the
  right stick (off with reduced motion). Exiting or quitting asks in a dialog
  built like the stock one, lit around its edges, with a crisp outlined title,
  and it fades in and out as the stock dialog does. Instead of blacking out
  the screen, the dialog softly blurs and slightly desaturates what is behind
  it on OpenGL and Vulkan; `ui_retainedOpaqueBacking 1` restores the stock
  dark backing, which the GLES renderer always uses. The pause menu sits over
  a softly blurred view of the game instead of a darkened one, and on
  RESUME the menu closes at once while the game eases back into focus over
  a quarter of a second.
  While a level loads, its picture drifts slowly closer, the bar shows what is
  loading and how far along it is, the finished load reports how long it took,
  and the continue prompt shows your controller's button once you use one.
  Single-player loads now show a tip in the loading band, changing every six
  seconds, in all seven languages.
  Hosting a multiplayer match, the loading screen fills in the map's arsenal:
  each kind of weapon, ammunition and armor appears in its color as it
  spawns.
  The modern title screen now uses well under half as much CPU per frame, with
  no visible change, and holds its frame rate better, most of all on Vulkan.
  Multiplayer loads show the server's name, address, mode and limits on a card
  and read JOINING when done. The card also lists who is playing, by team in
  team modes, with spectators and players still connecting, whether you host
  the match or join one, and shows the server's message: server admins set it
  with the new `si_motd` server variable (up to 256 bytes, wrapped to three
  lines). The multiplayer menus remain in development.

- On the modern Single Player and Campaign pages and the opt-in SYSTEM page,
  the controller's stick now moves the selection one step per push, and
  steadily while held, and stops when the stick is released. The selection
  could keep moving on its own after a push, as it never saw the stick
  released.

- Menu development: with `ui_retainedMultiplayer 1`, the in-match
  multiplayer menu becomes a card over the softened match instead of a
  full-screen page, with the map, mode, clock and score in its header, eight
  tabs that Q and E (or the shoulder buttons) switch, and Resume, Main Menu
  and Disconnect, which asks first. Its Team page switches team, spectates or
  readies up, and says why when it cannot; its Players page lists the teams
  beside the chosen player's kills, deaths, score, accuracy per weapon and
  awards, and mutes or befriends them; its Vote page shows the running vote
  (who called it, what it changes, the time left and the tally) with Vote Yes
  and Vote No beside their keys, and lets you call a vote from rows for the
  map, game type, limits, team options and a player to kick, sending only
  what you changed (the classic page also sends the current map, which makes
  the server refuse the vote); its Server page shows the server's message,
  rules and map rotation; its Settings page sets your model, rail color and
  handicap and how opponents and teammates look (forced models, outlines,
  rim light, brightskin and their colors), with Controls, Game Options and
  System a step away; its Voice page sets voice chat's switches and volumes;
  and its Match page shows Match Control's state and its readiness, pause,
  forfeit and abort actions, its teams and rosters with the team, queue and
  roster actions and the role an invitation gives, its proposals with their
  ballots, its rules with their profiles and a value to stage, its series
  with the map pool, the veto and the history, and its evidence, each action
  saying why it is unavailable and asking first where the classic page does,
  with the triggers paging Match Control's sections. Its Admin page and the
  referee sign-in still open the classic pages for now.
  The prompt bars' Select now reads correctly in Spanish and Italian.

- Joining a server without automatic joining now opens the new Welcome card
  over the softened match instead of the classic join panel. The loading
  screen stays up, reading JOINING, until the card is ready, then fades into
  it, so the join no longer flashes the bare match first. The card offers team
  cards that join each team (and say why when the balance rule refuses one),
  Auto join naming the team it picks, Spectate, the server's rules, the
  players, and your name, model, rail color and crosshair. Esc spectates for
  now, and the menu key brings it back until you join. Set `ui_retained 0` to
  keep the classic menus.

- The classic multiplayer Players page's Add Friend button now marks the
  player as your friend in the lists and on the scoreboard. It did nothing on
  PC, which has no friends service.

- Menu development: `ui_retainedMultiplayer 1` and `ui_retainedSystem 1` now
  stay on when you join or host a multiplayer game, and you can turn them on
  or off during a match. Joining or hosting a game without cheats used to
  switch both back off, so the multiplayer cards never appeared after a
  normal join. Neither setting is saved, so each still lasts until you quit.

- The opt-in SYSTEM page adds **Preload Light Grids** under Irradiance Volumes.
  Apply saves it without a Keep/Revert question, since it takes effect when the
  next map loads, and a line under it says whether the current map preloads or
  streams its light grids, has none, or keeps the previous setting until the
  next map. If the game closes before the change is saved, the next launch
  finishes or undoes it.

- With the opt-in SYSTEM page enabled, choosing System from the multiplayer
  menu's settings now opens it too, instead of the stock page.

- The opt-in SYSTEM page adds **Display Resolution** and **Refresh Rate**
  to its Display Sizing column. They list only the sizes and rates your
  display offers for exclusive fullscreen, so they are dimmed until
  Fullscreen Policy is set to Exclusive (you can set them while still in a
  window). Choosing a resolution or display that cannot do the selected rate
  sets Refresh Rate back to Auto, and choosing a display that lacks the
  selected resolution switches it to Desktop Native, instead of leaving Apply
  unavailable. A saved resolution or rate your display no longer offers is
  named in its row, and Apply asks you to choose a supported one instead of
  failing. Reverting from an exclusive resolution smaller than your desktop
  now finds your monitor again. The page stays opt-in until presets can be
  applied from it and its size fields take typed digits.

- Upgrade note for players who opted into the modern SYSTEM page: if an
  earlier build closed while applying an exclusive fullscreen change with
  Refresh Rate on Auto, this version can stop at startup with "Settings
  startup recovery failed" until you delete `ui-settings-recovery.dat` from
  your `baseoq4` save folder. Your saved settings are kept.

- **Refresh Rate: Auto** now means the same rate everywhere: your desktop's
  refresh rate, or the nearest rate the chosen resolution offers. The modern
  SYSTEM page used to apply the display's highest rate for Auto, while the
  next start, `vid_restart` or Alt+Enter went back to the desktop rate. On
  macOS, exclusive fullscreen on Auto now uses the desktop rate rather than
  the highest one.

- The opt-in SYSTEM page adds **Display Device** and **Expand Across
  Displays** at the top of its display column when you have more than one
  display. Pick Auto or a display by name; a display that is no longer
  connected is still named, so you can switch away from it. Expand Across
  Displays answers Yes for a borderless window or desktop fullscreen when your
  displays can be spanned, and says why when they cannot. Choices you cannot
  pick are dimmed. On a single display both rows stay hidden, as before,
  unless one holds a display that is gone, a span, or a change you have not
  applied yet. Dropdowns now wrap an option too wide for the screen, such as a
  long monitor name at a large text size, instead of refusing to open, as long
  as one wrapped row fits.

- The opt-in SYSTEM page adds **Renderer Fallback** at the end of its render
  options: Auto, or ARB2 as the explicit rollback. Apply restarts the renderer
  in place without a Keep/Revert question, and if the game closes before the
  change is saved, the next launch finishes or undoes it. If your
  configuration names a renderer openQ4 no longer ships, a note under the
  choice says the game is using a supported one; pick Auto to clear it. The
  choice works on OpenGL; Vulkan has a single renderer, so there it is dimmed.

- The experimental SYSTEM page adds window and custom fullscreen width/height
  fields with localized whole-pixel validation. Custom dimensions select Custom
  resolution automatically. Unsupported display combinations remain editable
  drafts and cannot Apply; window resizing uses Keep/Revert confirmation.
  Oversized numeric fields keep their editable value visible when translated
  labels or validation text exceed the available scroll area. Typed digits do
  not reach these fields yet; paste a value instead.

- The opt-in modern SYSTEM page adds fullscreen, borderless, fullscreen policy
  and MSAA controls to its Apply/Keep/Revert flow. On Vulkan, MSAA now changes
  through this page too, and display changes no longer fail and revert while
  MSAA is on. Vulkan applies MSAA to the 3D scene rather than the window, at
  up to 8x.
  Blocking video reload time no longer consumes the subsequent 20-second
  presentation wait. The separate 15-second Keep/Revert countdown is unchanged.

- SYSTEM brightness and ambient-light sliders now grow with enlarged text.
  Their numeric values stay fully visible when focusing either control, and
  the paired field frames remain aligned.

- Experimental modern UI text now rasterizes at its displayed size, improving
  enlarged text and fractional UI scaling. Shared glyph atlases stay stable
  through view changes and rebuild after language or video changes. Replace
  the client and renderer modules together; older modules are rejected.
  Full font, language and interface qualification remains in development.

- The experimental modern UI supports independently enlarged text using
  `ui_retainedTextScale` (`1`–`2`, default `1`). SYSTEM fields and dialogs grow
  with the text, long messages scroll while actions stay visible, and translated
  dropdowns remeasure their rows after widening. Changing status messages keep
  the active control visible while preserving deliberate scrolling.
  SYSTEM now offers UI scale and text size controls plus Reset UI sizes through
  its normal Apply/Discard flow. Saved sizes survive restart; very large sizes
  fit the window so reset and essential actions remain reachable. Complete
  interface qualification remains in development.

- Modern SYSTEM settings again show the correct localized Ready, unsaved-change,
  confirmation and error messages after the language-table merge. Page generation
  and validation use the same current string IDs.

- The experimental modern UI respects vector-mask holes when targeting controls
  and scrolling. Empty or fully transparent masked panels no longer retain
  keyboard focus. Partly transparent controls and active drags remain usable.
  Full UI replacement and editor qualification remain in development.

- SYSTEM dropdowns now fit inside the settings panel with cut-corner vector
  framing. Lists open above a control when needed, preserve readable row sizes,
  and scroll within the available space without covering the action footer.
  The replacement UI remains in development.

- UI authoring can prepare a document and its layout before publishing the live
  canvas and undo history together. Failed preparation preserves the current
  document. Native editor panels and complete save/recovery workflows remain
  in development.

- Editor save development now has a checked way to create complete new files
  without replacing an existing document, including when another process creates
  that name at the same time. The editor Save As and recovery workflows remain
  in development.

- Full renderer shutdown now releases retained texture recovery data, including
  after an invalidated quality-change attempt. Interrupted cleanup preserves the
  recovery data until a later complete shutdown. Complete preset Apply remains
  in development.

- UI recovery development now preserves both original and requested supported
  texture data across failed quality changes. Native input retirement also checks
  actual menu and gameplay event disposal before releasing ownership. Complete
  preset Apply and native text entry remain in development and are not enabled.

- UI authoring gains validated source edits and undo/redo that preserve existing
  formatting, comments and extension data. The visual editor and file-save
  workflow remain in development; this is their document-editing foundation.

- Native text integration now invalidates stale menu and window ownership before
  replacement or teardown. Live native text entry remains in development and is
  not enabled by this checkpoint.

- SYSTEM dropdowns gain editable vector scrollbars for overflowing option lists.
  Scrolling preserves the current selection and does not change a setting;
  fitting lists hide the inert scrollbar. This remains development work on the
  opt-in replacement UI, with complete screen and platform acceptance pending.
- SYSTEM gains an editable vector scrollbar with a generous target, a narrow
  trough and a cut-corner thumb. Its range follows the actual settings content;
  deliberate scroll position survives UI-scale changes and window resizing.

- Texture loading now handles incomplete mip chains and failed image allocations
  without publishing partially assembled images. Large decoded textures retain
  their requested dimensions, and cube faces stay consistent when a reduction
  fails. Generated caches rebuild as needed for the corrected image processing.

- Renderer upload buffers now verify their actual allocated size and clean up
  partial failures. If the dynamic stream cannot be created or reused, rendering
  falls back to legacy uploads instead of using unverified storage.

- Image-quality settings gain checked renderer recovery foundations that reject
  stale texture and material state after a failed restart. Complete preset Apply
  remains in development; this does not enable unfinished mixed settings changes.
  Recovery development now retains the quality policy actually used by supported
  resources and distinguishes requested audio output from observed output.

- SYSTEM uses a compact preset selector and a wrapping status/action footer to
  keep more settings visible at large UI scales. Essential controls retain
  their full-size targets and text can grow with translated labels.

- Closing SYSTEM during preparation of a settings commit now cancels the
  operation before persistence begins. The shared settings controller also gains
  automatic completion for future resource-only changes, retaining recovery on
  uncertain saves. Complete preset Apply remains in development.

- The opt-in SYSTEM page adds the six performance presets and Auto-Detect to
  its settings draft. Custom brightness edits are preserved, and canceling a
  preset list keeps the previous selection. Labels wrap across the existing
  translations and larger UI scales. Applying complete presets remains in
  development while their renderer, image and audio effects gain checked
  execution and recovery.

- Refresh rate, sky rendering, emitter limits and generated-image cache
  preferences now participate in normal saved settings. Lower performance
  presets can edit their sound budget from a fresh start without requiring
  a prior console preset command.

- Retained menu focus keeps the complete control border inset from scroll-area
  edges at high UI scales. Authored scrollbars preserve deliberate scrolling
  through resizing and UI-scale changes; fresh navigation reveals its target.
  Full-page qualification
  remains in development.

- SYSTEM precision work removes stray decimal digits from generated slider
  values and preserves small numeric settings through Apply. Custom values keep
  their precision, including through rollback and recovery records. Native text
  composition and complete platform qualification remain in development.

- The opt-in SYSTEM page pairs Brightness and Ambient Brightness sliders with
  precise numeric fields. Unfinished edits are preserved when leaving a field;
  Apply and exit require an explicit commit or discard. Native character entry
  and complete field qualification remain in development.

- Numeric fields add Copy, Cut and Paste shortcuts with localized failure
  messages. A failed Cut preserves the selection, and rejected clipboard text
  cannot replace a draft. Complete native input and platform validation remain
  required before the retained UI is ready for general use.

- Text-entry development now checks field ownership across backend replacement
  and input-queue resets. A Windows text-store foundation keeps application
  edits and native text-service revisions synchronized; live native text and
  complete IME behavior remain in development.

- Active numeric fields support caret movement, selection, deletion, undo/redo
  and explicit Enter confirmation. Held editing keys cannot switch to another
  field, and SDL shortcuts preserve the modifiers captured with each key event.
  [Native text delivery](ui/text-input-routing.md) and production settings-field
  completion remain in development.

- Numeric-field development preserves unfinished edits and undo/redo through
  focus changes and resource restoration, with explicit recovery when a setting
  changes elsewhere. The [integration scope](ui/numeric-fields.md) separates
  the rendered editor and semantic diagnostics from the ordinary input and
  native IME work still needed before players can use the field.

- Event journals reject malformed input records before allocating or dispatching
  their payloads and release owned data on failures. Recording still uses its
  existing platform-dependent format.

- Repeated map loads release discarded input data, avoiding an event-queue
  memory leak. Windows clipboard handling also checks transfer failures and
  converts legacy text through the operating system's Unicode path. The
  [text-entry foundation](ui/text-entry-foundation.md) records the scope;
  editable retained fields and native IME integration are still in development.

- OpenGL menus retain sharper text and vector edges at reduced resolution scale:
  UI-only frames no longer receive the extra filter that softened the completed
  image. World scaling and supersampling remain in place; the explicit legacy
  crop mode is unchanged. See the [bounded qualification](ui/native-output.md).

- The opt-in SYSTEM page adds bloom, ambient occlusion, tone mapping, CRT,
  irradiance, UI aspect, resolution scale and VSync controls. Back now lets you
  keep editing, discard and return, or apply and return after successful display
  confirmation. Failed changes remain available for recovery. The
  [implementation scope](ui/system-exit.md) records the remaining work before
  enabling the replacement by default.

- Menu development: retained settings now support toggles, stepped sliders and
  selectable lists with editable vector parts. Changes stay in the settings
  draft until accepted, and choice lists follow animated controls and page
  fades. An opt-in SYSTEM page uses normal menu ownership and protects
  unresolved changes on Back. The [implementation scope](ui/value-controls.md)
  records remaining controls, artwork and editor work; the replacement remains
  disabled by default.

- Settings files now use checked serialization and durable replacement. A failed
  save retains pending changes instead of clearing their dirty flag, and shared
  recovery locks keep a second engine process from overwriting an active display
  transaction. Retained confirmation views add Apply, Keep, Revert and a distinct
  Retry action, with crash recovery and a countdown armed by the owning view's
  presentation. The [integration scope](ui/display-confirmation.md) keeps full
  production GUI/editor/platform acceptance separate from these implemented paths.

- Display-settings development: the renderer can attempt an exact display
  configuration, report failure and restore a captured window/device state.
  Presentation results distinguish a newly rendered frame from accepted
  settings values. The [confirmation integration](ui/display-confirmation.md)
  now connects Keep/Revert and crash recovery; complete production settings
  coverage remains in development.
- Vulkan stops further presentation after unsafe submission or synchronization
  failures until the device is restarted, avoiding reuse of synchronization
  objects whose state is uncertain. Engine and renderer modules must be updated
  together for the new private interface.

- Settings development: retained menus can keep edits in a private draft,
  discard changes, restore SYSTEM defaults into the draft, and apply immediate
  settings with conflict checks. Drafts survive renderer/language recreation;
  closing an editor releases its ownership. The
  [SYSTEM contract](ui/system-settings-contract.md) records the supported scope.
  Audio/resource application and the complete production screen remain in
  development; display confirmation is implemented for eligible retained views.

- Vulkan display initialization and partial display changes now report failed
  window or swapchain setup instead of publishing a successful configuration.
  Failed initialization cleans up its attempted device before releasing its window.

- Behavior development: retained menus can initialize data, branch, run ordered
  transitions and submit typed settings actions through named events. Pending
  game data reaches handlers immediately, completed actions survive input
  suspension, and outgoing menus finish their lifecycle safely. The
  [event-program scope](ui/event-programs.md) records the current subset;
  production GUI translation, full controls and visual editing remain open.

- Presentation development: retained menus now support named values shared by
  game callers, typed layout/text/color updates, and expression overrides that
  survive GUI saves and renderer/language reloads. Hidden or input-blocked
  panels cancel pending button activations. The
  [alias checkpoint](ui/presentation-aliases.md) records the supported contract;
  full GUI translation, controls and visual editing remain in development.

- Vulkan now applies brightness and gamma to the completed image, including
  menus and engine screenshots. Neutral settings bypass the correction pass.

- Legacy list selection rejects invalid negative/out-of-range rows and displays
  percent characters in row labels literally.

- Application integration development: retained menu documents can use normal
  GUI loading and submit brightness/shadow changes through typed settings operations.
  Multiple views share resource recovery, and restored GUI state discards old
  clicks. [Integration scope](ui/managed-application.md) records the current
  limits; the full settings screen, GUI translation and visual editor remain
  in development.

- Instance persistence development: retained previews preserve focus, dialogs,
  control availability and transition progress through renderer/language
  reloads. Restoring an instance discards pending clicks and actions. GUI
  ownership also handles direct editor deletion and allocations made before a
  resource is loaded. The [checkpoint](ui/instance-persistence.md) records the
  supported subset; complete game integration, translation and visual editing
  remain in development.

- Renderer recovery: full video restarts now recreate the selected renderer,
  allowing Vulkan menus to survive display reinitialization. The restart path
  reloads images once before rebuilding fonts.

- Settings recovery development now checks the exact bytes of supported DDS
  and previously loaded image-cache files before CPU reconstruction. Native
  input development also accounts for queued event ownership through terminal
  disposal. These are foundations for reliable settings and text entry;
  additional settings effects, native activation and complete recovery remain
  in development. See [image recovery](ui/image-content-recovery.md) and
  [input ownership](ui/native-input-publication.md).

- Instance development: retained documents can now keep separate data, focus,
  display scale and transitions while sharing engine services. Closing one view
  preserves its peers, and render targets cannot be resized underneath another
  view's queued drawing. The [instance checkpoint](ui/instances.md) records
  validation scope. GUI-manager/world integration, full translation and the
  visual editor remain in development.

- Integration development: menu and game callers now query GUI values without
  stopping their expressions. Explicit overrides and focused
  text-field queries prepare the replacement interface boundary. Development
  packages must replace engine and both game modules together (game API 48;
  renderer API 14). See the [presentation boundary](ui/presentation-bridge.md).
  Complete GUI migration and the visual editor remain in development.

- Live data development: retained interfaces now update text, gauges, layout
  and control availability from typed state and read-only CVars without
  rebuilding the screen. Invalid updates preserve the last valid snapshot;
  renderer restarts retain application values. Native and SP/OpenGL/MP/Vulkan
  checks pass; [binding evidence](ui/bindings.md) records current limits. Full
  game integration, GUI migration and the visual editor remain in development.

- Translation development: the current GUI set can now be imported through the
  engine preprocessor into ordered windows, events, expressions and dependency
  records. All 271 source hashes match in SP/OpenGL and MP/Vulkan; two unusual
  monitor expressions remain flagged for review. The
  [import checkpoint](ui/legacy-import.md) records the evidence. GUI replacements,
  vector reconstruction, game bindings and the visual editor remain in progress.

- Input development: the retained menu host now routes keyboard, mouse and
  controller navigation, preserves console access and requires held controls
  to be released before returning them to gameplay. Disconnecting a controller
  cancels a pending button press. Navigation repeats without
  depending on OS key repeat, and pointer positions retain their display-density
  precision. [Input ownership](ui/input-routing.md) records implementation and
  qualification: native checks pass and reviewed OpenGL/Vulkan gameplay captures
  confirm SP pause/resume and continued MP simulation. The game action bridge,
  full GUI migration and editor remain incomplete; existing MP warnings are
  unchanged.

- Button development: the retained runtime now owns focus, disabled navigation,
  paired activation and modal input scopes. Editable timelines supply hover,
  focus, pressed and disabled feedback. Native tests and reviewed SP/OpenGL
  125% and MP/Vulkan 200% gameplay captures pass, including an OpenGL video
  restart. The [interaction checkpoint](ui/interaction.md) records the evidence;
  live platform input, game action dispatch, full GUI translation and the visual
  editor remain open. The existing 93 MP content warnings are unchanged.

- Mask development: editable paths now clip complete panels and their contents,
  with smooth chamfers, curved holes and soft gradient reveals. Nested masks
  and fades share the runtime's vector renderer. The [mask checkpoint](ui/masks.md)
  records coverage checks and current limits; full GUI migration and the visual
  editor remain in development.

- Composition development: panels and their overlapping contents now fade as
  one group, including nested controls and text edges. The
  [composition checkpoint](ui/composition.md) records native overlap tests and
  engine pixel comparisons. Development packages must replace engine, renderer
  modules and both game modules together (renderer API 13, game API 47).
  Broader effects and complete UI/editor qualification remain open.

- Transition development: fades and whole-pixel movement now reuse compiled
  vector paths, with measured reductions in CPU work. A precision fix keeps thin
  rails stable during fractional movement. The new
  [runtime profiler and benchmarks](ui/runtime-performance.md) expose remaining
  cold/fractional-motion costs; optimized-build and GPU qualification remain open.

- Vector development: editable native paths now support responsive chamfers,
  curves, holes, strokes and fading rails through the retained engine renderer.
  [Geometry checks](ui/vector-paths.md) and SP/OpenGL and MP/Vulkan gameplay
  captures pass, including fractional scaling and video restart. Coverage
  antialiasing now has an [analytic implementation](ui/coverage-antialiasing.md)
  with independent pixel-area tests and reviewed SP/OpenGL 125% and MP/Vulkan
  200% captures. Dense vector batches now split before exceeding the renderer's
  surface capacity. Broader renderer qualification, the full
  component/artwork corpus and editor interactions remain incomplete; existing
  MP content warnings remain open.

- Document/editor foundation: typed canonical sources retain comments and
  extension data through validated value edits. Presentation tracks now support
  continuous timing, interruption, pause, cancellation and reduced motion. The
  [document checkpoint](ui/document-format.md) records the implemented subset;
  native checks and SP gameplay captures pass in OpenGL and Vulkan, including
  video restart and 100%/200% density. The visual editor and complete GUI
  translation remain in progress; existing non-UI Vulkan warnings remain open.

- Retained runtime development: RmlUi is pinned and integrated with the engine's
  renderer, density model and current scalable fonts. Native layout, clipping
  and motion-sampling checks are in place. The [integration checkpoint](ui/runtime-spike.md)
  records remaining renderer, text, timing, input, editor and migration work;
  this preview is not a completed player-facing replacement.

- [x] Disabled mouse input and hidden rendering windows no longer route menu
  cursor synchronization; unfocused windows cannot warp the pointer on menu
  changes. Production-function tests cover Windows and POSIX paths; the staged
  Windows client completed hidden-window SP/OpenGL and MP/Vulkan gameplay
  captures. Broader platform and interactive qualification remains pending.

- [ ] Replace every current GUI with a high-definition vector interface that
  preserves Quake 4's identity, expands across aspect ratios and responds to
  display density and player scale settings.
- [ ] Deliver continuous transitions and the complete visual editor, including
  editable vector paths, layout, states, bindings, timelines and reliable saving.
- [ ] Qualify the complete SP/MP corpus, saves, world terminals, scopes, vehicles,
  languages, supported platforms and release-style packages before promotion.
- Specification and staged evidence: [UI replacement plan](archive/2026-10-18/plans/idtech5-ui.md).
  This is development work; no completed player feature or upgrade requirement
  is claimed yet. Curated release notes will use the actual selected release
  version once the replacement is qualified.

## 0.13.1 — published 8 September 2026

- [x] #159: release compilation uses complete engine history; stable version generation refuses shallow or missing history before producing a savegame build number. Regression coverage uses real full/shallow Git repositories.
- [x] #160: full archive decompression/CRC validation runs before upload; GitHub asset sizes and SHA-256 digests must match local files before a new draft is published. The reported 0.13.0 Metal download was truncated (563,930,525 bytes); its intact original Actions artifact is 671,512,342 bytes and passes full gzip/tar validation.
- [x] The wider 0.13.0 asset audit also identified truncated Linux x64 tar.xz, Linux x64 AppImage and Windows x64 installer uploads. All three were restored from their original successful Actions artifacts and their new GitHub sizes/digests match. Together with the Metal repair, all 14 published 0.13.0 downloads now match the original artifact sizes; the old release page and #160 carry the re-download notice.
- [x] #158 render geometry: `.proc` now records the source geometry CRC instead of a fixed placeholder. AAS rebuilding is explicitly reported as unavailable; the compiler implementation is absent, so navigation remains open.
- [x] Staged Windows qualification: `airdefense1` manual save, quicksave, load and save-after-load pass. A freshly compiled `mp/liquid_lab` produces `.proc`/`.cm`, reports the AAS limitation, enters multiplayer gameplay with explicit auto-join and exits cleanly; its new `.proc` is not reported out of date. Release/version, savegame, dmap, macOS packaging and documentation contracts pass.
- [x] #157 investigation: fresh `game/hub1` passes saves during and after the terminal animation, restoration, and another save. The final fixture records its GUI activation marker as 1 in the manual saves and restored state; it supersedes an initial console-quoting failure. A further packaged-candidate check activates the stock `func_savegame_1` entity and verifies that `Checkpoint0` also retains the marker. That covers the checkpoint-save command without claiming the full spider/EMP encounter. A temporary named-event fixture executes the unchanged stock button script and its three target relays without injecting mouse/keyboard input; its console source is retained for restore. The supplied 0.12.0 save carries build 1 and is outside the current restore policy, so the exact historical campaign state and later EMP autosave still need reporter confirmation.
- [x] [0.13.1 is published](https://github.com/themuffinator/openQ4/releases/tag/v0.13.1) from engine `e74ae236920ee274cdfaeb8ad09686c6db16b749` and companion `089a328096faa8da82fad5c23306ec8c4ccee290`. All six [release build/package jobs](https://github.com/themuffinator/openQ4/actions/runs/34196053568), the complete [push-verification matrix](https://github.com/themuffinator/openQ4/actions/runs/34196042716) and [Linux ARM64 cross-build](https://github.com/themuffinator/openQ4/actions/runs/34196042790) passed. All 14 draft downloads passed full archive checks where applicable and size/SHA-256 comparison with the uploaded bytes before publication. The exact Windows x64 ZIP passed windowed `airdefense1` manual save, quicksave, quickload, manual restore and save-after-restore; all three payloads record version 3/build 858. That tested ZIP is 721,278,568 bytes, SHA-256 `8c2acd37a10ba615c7cf0a713655e91090e18f5238ab33a83652f3876379c3c5`.
- [ ] #89 remains awaiting native CachyOS/RTX 3070 frame-pacing evidence; Windows or software-rendered Linux results cannot close that hardware-specific report.

Curated player notes: [0.13.1](releases/v0.13.1.md). Work was committed and released from `main` in isolated engine/companion worktrees while both primary checkouts remained on `android-gles`. No companion source changes were needed.

Remaining qualification limits: the fresh Nexus fixture logs two missing `$strauss` script-entity warnings because it starts outside the original campaign state. The dmap multiplayer smoke retains missing AAS/content and non-precached-declaration warnings. The exact packaged `airdefense1` save/load run has no error or warning diagnostics. #158 navigation, #157 campaign/EMP confirmation and #89 native-hardware performance remain open.

- [x] Release CI exposed existing Apple GL 2.1 startup errors from unsupported texture swizzles, multisample textures, eager SMAA shader validation and GPU timestamp queries. Resource creation and direct texture binding now check the context's advertised capabilities, SMAA checks its backend before material compilation, and GPU timing distinguishes EXT elapsed queries from ARB/core timestamp support. The production timestamp capability regression passes on Windows and Linux; hosted macOS now has zero GL/shader/framebuffer errors and passes the default-renderer safety test.
- [x] Modern-renderer diagnostics exposed oversized simultaneous draw/submit/PBR fixtures on the macOS stack and unsupported-feature assumptions. The large fixtures now use heap storage, while unavailable PBR and clustered-light GPU paths produce explicit skip markers and executor CPU contracts run without requiring modern GPU uploads. Hosted macOS ARM64 OpenGL and Metal matrices pass with zero tracked error diagnostics. Local modern-renderer checks pass with both the normal and an 8 MiB stack, and forced-legacy foundation/PBR/clustered checks pass.

- [x] [0.13.0 is published](https://github.com/themuffinator/openQ4/releases/tag/v0.13.0) from engine `b639d265321e550b876ae44d6a49c1012df0bf6c` and companion `089a328096faa8da82fad5c23306ec8c4ccee290`. All six [manual release build/package jobs](https://github.com/themuffinator/openQ4/actions/runs/34164437153) passed; all 14 public downloads returned HTTP 200 with nonzero content lengths. The draft was promoted after validation, and the [Discord announcement](https://github.com/themuffinator/openQ4/actions/runs/34165971044) succeeded. Engine platform verification is recorded in [the release-source CI run](https://github.com/themuffinator/openQ4/actions/runs/34164437311); the [companion CI run](https://github.com/themuffinator/openQ4-game/actions/runs/34160166472) also passed.

- [x] The 0.13.0 integration pass wires the new source contracts into local and CI validation, includes syntax checks for gameplay drivers, synchronizes the shared memory-file header, and pins both repositories' CI inputs. Windows build/staging, 12 native tests, all 54 competitive contracts and OpenGL/Vulkan chat gameplay checks pass. Stock `q4dm1` still logs missing AAS sizes, the optional `mp_buying_givecash` sound and non-precached declarations during these no-bot chat checks. Assetless hosted probes also retain missing-stock-content and menu-parsing warnings; those content diagnostics remain outside this release-tooling change.

- [x] Manual release publication resolves both drafts and published releases through the draft-aware GitHub CLI lookup and sends typed boolean publication flags. Draft promotion no longer depends on a published tag existing before publication; the Discord job still waits for successful publication.

- [x] Roster authority and proposal reuse now survive the named live failure/reconnect cases. Captain demotion or benching revokes invitations, review disables unsupported roster mutations, and unsuccessful terminal proposals release their scope while preserving cooldowns. Missing series map files are checked before handoff and retain a retryable selection; loose-file checks close their handles. Full Windows build/staging and all 54 competitive contracts pass, with targeted Linux native/sanitizer coverage. Three-client OpenGL and Vulkan runs each pass 32 accepted-state checkpoints, including captain/referee slot reuse with no inherited authority or private views; all ten client lifetimes exit cleanly. A focused Vulkan run also retains visible captain/spectator Status captures. Other roster, proposal, physical-navigation and post-preflight loading edges remain in `docs/dev/competitive-match-reference-audit.md`.

- [x] MVD cameras preserve the selected player across round restarts during continuous playback and seeking, and immediately restore the recorded stance's eye height while paused. Saved runtimes reproduce the regressions; the matched staged package passes a newly recorded eight-round Red Rover replay with no rejected match views. An explicit Free Camera action still takes precedence. All 54 competitive contracts, Windows/Linux native camera checks, full Windows build, staging and demo/API checks pass. Manual MVDs survive map review and series sealing; recording-start failure and size/duration limits leave match results intact. Production writer/finalization fault tests pass on Windows and Linux ASan/UBSan. See [MVD qualification (2026-09-07)](mvd-qualification-2026-09-07.md).

- [x] Persistent multiplayer results and spectator controls pass scoped OpenGL/Vulkan gameplay qualification. Results freeze the winner, forfeit, draw or abort independently of optional evidence, survive review, warmup and winner-slot reuse, and clear at the next countdown. Match Control offers Last Result plus Previous, Next and Free Camera; both renderers pass 17 camera permission/revocation checks and retain 30 role/menu captures. Maximum-length Duel winner names remain readable at 800x600 and 960x540. Scrolled summary chat no longer submits inverted glyphs above its viewport; 2,723 native glyph cases pass on Windows and Linux ASan/UBSan, and both renderers retain clean engine captures. Root-level script includes pass map-cycle loading and native Windows/Linux regressions. All 53 competitive contracts, full Windows build and staging pass for this runtime. MatchView schema 4 requires matching updated clients and servers. Physical navigation, roster/proposal failure paths and other specific release edges remain open in `docs/dev/competitive-match-reference-audit.md`.

- [x] Repeated competitive pauses retain remote player and weapon animation when a snapshot skips predicted frames. The production clock regression fails before the fix and passes on Windows, Linux and Linux ASan/UBSan. Two real clients pass 28-second and 3.5-second freezes, both resumptions, stock projectile/mover motion, armor respawn and powerup expiration. Clan Arena and Tourney pass match and round/arena countdown pauses, preserve their remaining time, resume gameplay and abort from a further live pause. Separate BO3 runs pass blocked first-map JSON and MVD publication without changing the winner or later artifacts; the MVD case also preserves its failed `.part` status through an interrupted recovery.

- [x] Two-human BO1, BO3 and full-distance BO5 qualification completes veto, selected-map handoffs, gameplay, forfeits, review and final publication. The five-map team series restores at 1-1 and finishes 3-2; the Duel series restores at 1-0, accepts fresh explicit contestant bindings and finishes 2-0. Both accepted views agree with the saved series and map JSON, with committed MVDs and no pending files. Linked BO1 playback passes live seeking, both player cameras, free flight, rewind, stepping and clean completion. Fixes cover map-shutdown ordering, MVD path lifetime, terminal report finalization, recovery continuation in the server and menu, delayed-admission reliable traffic, recursive loading updates and clients predicting ahead during world reset. Windows/Linux native contracts and targeted Linux ASan/UBSan checks pass. Artifact failure isolation and repeated world/countdown pauses are covered by the later qualification above; physical interaction remains open. See `docs/dev/competitive-match-reference-audit.md`.

- [x] Multiplayer usability refinement: restored server-browser discovery, details, sorting, filters, favorites and reliable joining; repaired managed Duel FIFO turnover, leading-player forfeits and listen-host view refresh; clarified readiness, focused feedback and managed summaries. Managed mode survives server-info refreshes. Windows build/staging and all 52 competitive contracts pass, with selected Linux sanitizer checks. Both renderers pass browser joining, three-client Duel passes with verified persisted winner/scores, and 4:3/OpenGL plus 16:9/Vulkan role runs retain 60 engine captures. Physical input and the broader competitive qualification matrix remain open. See `docs/dev/competitive-match-reference-audit.md`.
- [x] Native multiplayer chat: Q2REX-inspired retained conversation, stable scrollback, per-channel drafts and sent-message recall, saved layout settings, safe literal-text submission, and stock-GUI integration. Windows client/dedicated/SP/MP builds, history-model and networking contracts, and windowed OpenGL/Vulkan gameplay checks passed, including stock GUIs and compact layouts. See `docs/dev/chat-design-port.md`.

- [x] Manual macOS release validation now recognizes the package's exact `@rpath/libopenal.1.dylib` dependency only when the pinned runtime exists inside `openQ4.app/Contents/Frameworks`. The redundant workflow audit also verifies that runtime's architecture, system-only dependencies, code signature, and install name before accepting either experimental macOS package.
- [x] Controller menu navigation now clears stale mouse highlights, hides the idle pointer, and traverses visible controls in both directions. Moving or clicking the mouse restores pointer control.
- [x] Players can reduce light-grid upload stutter through Settings > Display > Post FX > Preload Light Grids. The option applies on the next map load and explains its loading-time and video-memory costs; the default remains off.

Use this file as the source list for release changelog entries.

Process:
1. Add completed work under "Ready For Changelog".
2. When cutting a release with curated notes, save them in `docs/dev/releases/vX.Y.Z.md`; the manual release workflow will use that tracked file instead of the auto-generated history summary.
3. If no tracked release file exists, the workflow falls back to the generated release notes from commit history.
4. Move shipped items into a historical release section here (optional), and keep remaining work under stable IDs in `docs/dev/roadmap.md`.
5. For macOS support claims, complete the "macOS Evidence Gate" below and keep `docs/dev/macos-signoff-evidence.md` current before promoting anything beyond preview wording.

## macOS Support Claim Guard

Use this checklist before editing curated release notes or front-door package docs that mention macOS.

- [ ] Release notes that mention macOS say "preview Apple Silicon/arm64 macOS" and repeat the preview limitations from `docs/dev/macos-support-matrix-policy.md` (unsigned packages, and only current macOS played on real hardware) unless a completed evidence entry supports stronger language.
- [ ] Any first-class, stable, or fully supported macOS claim cites the current release entry in `docs/dev/macos-signoff-evidence.md` and has the "macOS Evidence Gate" completed.
- [ ] Intel Mac, universal2, and Rosetta appear only as unsupported, not-published, or future-policy items until a separate matrix expansion plan is accepted.
- [ ] `macos_graphics_bridge=metal` is described as a Metal bridge around the OpenGL renderer, not native Metal or OpenGL-free rendering.
- [ ] `platform_backend=native` on macOS is described as comparison-only diagnostic infrastructure, not a release backend.
- [ ] Curated release notes describe macOS as a preview. GitHub issue #98's reporter closed it on 0.12.0, and the #122 reporter confirmed stock gameplay and audio on 0.13.1; like closing the original issue #73 startup crash, these community reports support the preview but do not satisfy the macOS Evidence Gate.

## Windows ARM64 Support Claim Guard

Added 2026-09-18. Windows ARM64 assets carry no tier suffix, so the wording is the only label they get.

- [ ] Release notes, front-door docs, and the packaged README call Windows ARM64 experimental until `docs/dev/platform-support.md` records an automated launch on an Arm64 Windows runner and a real-hardware report.
- [ ] Windows ARM64 is never listed as supported alongside Windows x64 without that qualifier.

## macOS Vulkan Renderer Wording Guard

Added 2026-07-25. Use this alongside the checklist above whenever release notes,
package docs, or store text mention the macOS Vulkan option. The decision plan
is `docs/dev/archive/2026-10-18/macos-moltenvk-decision.md`.

- [ ] macOS Vulkan is described as running through MoltenVK, a Vulkan-on-Metal translation layer, and the first user-facing mention includes the words "translation layer".
- [ ] The translation layer is never called native Metal, a Metal renderer, a Metal backend, a native Vulkan driver, or an OpenGL-free renderer, and no wording implies openQ4 renders through Metal directly.
- [ ] Player documentation distinguishes fresh desktop Automatic (Vulkan with OpenGL fallback) from preserved explicit backend choices, and retains experimental Linux/macOS qualification and Android GLES defaults. The 28 October GPU-selection request supersedes the earlier OpenGL-default policy.
- [ ] The Vulkan renderer is described as a runtime choice inside the two existing `OpenGL` and `Metal bridge` packages, never as a third download, a new package variant, or a new `macos_graphics_bridge` value.
- [ ] Any macOS Vulkan claim states that it is experimental and may not work, because no accepted real-Apple-hardware evidence exists; performance claims and comparisons for macOS Vulkan are not made at all.
- [ ] Failed-start guidance covers module/translation-layer/GPU, window, surface, swapchain and mandatory-resource startup failures falling back to OpenGL; post-start presentation/device recovery remains distinct.
- [ ] The bundled MoltenVK version matches the pin recorded in `docs/dev/macos-moltenvk-provider-policy.md`, and its Apache-2.0 attribution is present in the package license collateral.

## Ready For Changelog

- [x] SP turbo mode bypasses every weapon reload state, including queued/restored reloads, the rocket launcher's separate loading thread, and the shotgun's post-shot pump. New-game difficulty/turbo choices survive the configuration replay when switching into the SP module. The controller wheel has correct horizontal direction and an archived Wheel Sensitivity setting (default 4.0, range 0.25–16.0), independent of aim/zoom/run modifiers. Production-code regression coverage: `tools/tests/sp_turbo_weapon_wheel.py`.
- [x] The console no longer crashes when, after going from single player to multiplayer, `g_renderFastNoPost`, `g_renderFastNoPostDirect`, `g_renderCaptureCurrentRender`, `g_renderCasUpscale`, `bot_showstate` or `bot_pathdebug` is typed followed by a space. Starting or joining a multiplayer game unloads the single-player game module and loads the multiplayer one. The single-player module declares those six console variables and the multiplayer one does not, and unloading it left their value completion pointing at its freed code, which the completion popup asks for as soon as a name and a space are typed. openQ4 now records the completions before it loads a game module and puts them back before it unloads it, as it already did for renderer modules, so a game variable declared the same way later cannot bring this back. The collision debugging variables, such as `cm_drawMask`, also keep their value completion now; switching game modules used to clear it for the rest of the session.
- [x] The console no longer crashes after `rendererVkProbe`, or on macOS after a Vulkan start falls back to OpenGL, when the name of a renderer variable such as `r_fullscreen`, or `developer`, is typed followed by a space. Loading the Vulkan module registered its console variables over the ones openQ4 and the active renderer already had, which handed their value completion to the module's code, and the completion popup asks for it as soon as a name and a space are typed. Unloading the module left those completions pointing at freed code: after `rendererVkProbe` passed on Windows, 311 variables did. The loader now records the completions before it loads a renderer module and puts them back before it unloads it.
- [x] When the Vulkan renderer cannot find a working device, openQ4 now renders with OpenGL instead of stopping. Before switching renderers it loads the Vulkan module and runs its device check. With no Vulkan driver, no GPU that meets the renderer's Vulkan 1.3 requirements, a device that cannot be created, or on macOS a missing MoltenVK, it logs the reason and renders with OpenGL, and `gfxInfo` shows why on its `Renderer API fallback reason` line. Until now all of these stopped with "Vulkan renderer device initialization failed" (#96), and because `r_renderApi` is saved, every later launch stopped the same way until openQ4 was started with `+set r_renderApi gl`. A failure the check cannot see, in window, surface, or swapchain creation, still stops that launch, but first sets `r_renderApi` back to `gl` in the saved config, so the next launch uses OpenGL. On the development laptop the check adds about 0.5 s to a Vulkan start, most of it spent in Microsoft's Vulkan-on-D3D12 compatibility driver; with that driver hidden it adds about 0.15 s. `rendererVkProbe` also finds the MoltenVK bundled inside the app now, as the renderer does; before, it only searched where a system Vulkan loader would be.
- [x] The Vulkan renderer documentation now lists the OpenGL features Vulkan does not have yet: brightness and gamma, baked light grids, soft particles, MSAA alpha-to-coverage, the classic post-processing chain, multiplayer player outlines, cel shading, debug views, and custom material programs. Vulkan stays experimental; the reasons are in the Vulkan row of `docs/dev/engine-capability-matrix.md`.
- [x] macOS on Apple Silicon is now a preview rather than experimental, and Windows ARM64 is labeled experimental. On macOS 26, one player on an M4 Max reported multiplayer working with sound on 0.12.0 (#98) and another on an M2 played the campaign with sound on 0.13.1 (#122), on top of CI that builds, packages, and starts both macOS variants on every push. The macOS packages are still unsigned, and nothing older than current macOS has been run. Windows ARM64 packages had shipped with no label at all, although nothing starts them in CI and no hardware report exists; the README, setup guide, packaged README, issue template, and release notes now say experimental. Linux ARM64 stays a preview. The tiers, and what each one rests on, are recorded in `docs/dev/platform-support.md`.
- [x] Windows ARM64 builds are now started by CI. Every push and pull request builds Windows ARM64 natively on an Arm64 Windows runner and starts the dedicated server without game data: it checks that the server, its multiplayer module, and OpenAL are arm64 builds, then requires the multiplayer game to initialize, a server frame to run, and a clean shutdown. Windows x64 runs the same check. The game client has not been started by automation and no real-hardware report exists yet, so Windows ARM64 packages stay experimental.
- [x] Baked light grids stream in without stalling when new areas come into view. A map's `.lightgridpack` is stored deflated in `pak1.pk4`, and reading an area's atlas seeked inside that entry, which inflates the pack from its first byte; with three atlases per area, an area deep in `game/airdefense2`'s pack cost up to about 200 ms, and a step that revealed several areas held a frame for half a second. The pack is now read into memory once while the map loads (68 ms for airdefense2's 18.6 MiB, 27 ms for `mp/q4dm1`) and each area streams from that copy in a few milliseconds. In a teleport tour through every area (Windows x64, OpenGL, three interleaved runs each), the worst frame fell from 491 ms to 23 ms on airdefense2 and from 434 ms to 21 ms on the q4dm1 listen server, close to the 11–21 ms of fully preloaded play. Preloading gains the same way: `r_lightGridPreload 1` now costs 84 ms instead of 3.9 s on airdefense2 and 42 ms instead of 2.3 s on q4dm1. The in-memory copy is released once every atlas is resident, unless `r_lightGridResidencyFrames` may still purge atlases; the Vulkan backend, which has no light-grid pass, reads the pack only when preloading, and dedicated servers never read it. `r_lightGridPreload` still defaults to 0.
- [x] Console argument completion now narrows to what has been typed. Typing a value such as `com_loadingContinueAutoAdvance 30000` or `com_maxfps 120` lists only the values that start with it, and Enter runs the command; before, the popup listed the first 64 values of the range whatever was typed, and Enter replaced the typed value with one of them. Enter also runs the line when the highlighted entry already is what was typed, as with `com_maxfps 60` while 600-609 are still listed. The popup keeps up to 1024 matches, so even the largest name family is listed in full (typing `r` matches 573 command and CVar names, `r_` 473). Typo suggestions stay limited to the best 64 and are no longer offered for numeric values, where a near miss such as 500 for 5000 is a different setting. Covered by the `openq4-console-completion` native test and `tools/tests/console_completion_contract.py`.
- [x] Heat haze and the other `_currentRender` effects survive antialiasing on the Vulkan backend. With `r_multiSamples` above 0 the scene renders into a multisampled target, `vkCmdBlitImage` cannot read one, and the `_currentRender` capture failed outright; every surface that consumes it — the fire distortion in `effects/fire/*.fx`, the `warp_mask` stages, heat haze — was then skipped for the whole frame, so multisampling silently deleted those effects. The capture now resolves a multisampled colour source into single-sample scratch of the source's own format and blits from that, using the same `vkCmdResolveImage` path the render-target resolve already uses. Depth capture is unchanged; a multisampled depth source is still refused. The added resolve costs roughly 1 ms at 2560x1440 with 4x multisampling on Apple M-class and A19-class GPUs.
- [x] The view weapon kicks back when it fires in multiplayer, as it always has in single player, and a new **Settings > Game Options > View Weapon > Weapon Kick** option (`g_weaponMuzzleKick`, on by default) turns it off in both. The engine code was never the difference: `idGameLocal::FindEntityDef` redirects every multiplayer lookup from `X` to `X_mp`, and every stock `weapon_*_mp` def zeroes the four `muzzle_kick_*` keys, so `rvWeapon::MuzzleRise` returned at its `muzzle_kick_maxtime <= 0` guard. Each multiplayer weapon now recovers its own single-player kick from the def that redirect replaced. This is presentation only: the kick feeds the view model transform, and no stock weapon sets `launchFromBarrel`, so aim and shot origin cannot follow it. The separate `recoilAngles` view punch those defs also zero is deliberately left off.
- [x] #158 navigation: openQ4 now compiles Quake 4 AAS in the engine. `dmap` rebuilds every AAS type declared by `aas_types` after writing `.proc`/`.cm`, `runAAS`, `runAASDir` and `runReach` register alongside `dmap` instead of inside `ID_ALLOW_TOOLS`, and unused AAS types get the placeholder file the runtime recognises as a dummy. Compiled `game/core2` reproduces the retail file's geometry CRC (3455855974) and navigation bounds exactly, with 1168 areas against retail's 1091 and 8435 valid reachabilities against 7566; forcing `-playerFlood` reproduces the shipped `aas48` bounds as well. The map loads all five compiled files with no AAS warnings, and a full `dmap mp/liquid_lab` produces `.proc`, `.cm` and five AAS files. Two defects found on the way: `idAASFileLocal::Optimize()` wrote uninitialised allocator memory as the dummy edge/face of every optimized AAS file, and `idVectorSet::FindVector()` never compiled because its base-class `Append` was unqualified. Raven's tactical feature lump is still not generated, so recompiled maps keep full pathing but lose navigation-mesh cover and lean positions. See [AAS compiler](aas-compiler.md).
- [x] Compiled map output now reports the absolute path of each file it writes. `dmap` writes `.proc`/`.aas*` under `fs_cdpath` and `.cm` under `fs_devpath` (unset by default, so it falls back to `fs_savepath`), which are different directories from each other and from the `.map` that was read; the reporter on #158 concluded no `.proc` had been produced when it had.

- [x] Experimental Android, GLES 3.0 and optional SigmaTouch integration based on **[Emile Belanger (emileb)](https://github.com/emileb)'s original contribution**, adapted to current official engine/game interfaces. Includes shader variants, ETC2/EAC textures, separate regenerable caches and a standalone SDLActivity APK host. Windows OpenGL/Vulkan and Linux GLES gameplay checks, native tests and Android cross-build/APK checks are recorded in [android-gles-integration.md](android-gles-integration.md); physical Android/SigmaTouch device qualification remains open. Candidate player notes: [Android/GLES](releases/android-gles.md).
- [x] Android/GLES pre-publication review repairs alpha-test boundaries, required vertex-buffer setup, compressed mip uploads and small texture/cube cache generation; keeps uploaded intro/loop audio playable without reloading its source; fixes overlapping SigmaTouch controls and deferred localization; and checks native APK metadata, dependency notices and safe cleanup of obsolete extracted packages. Existing image caches regenerate once. The integration record tracks focused behavior, shader, native-build and packaging validation.
- [x] Managed remote clients now retain their accepted initial join choice. Startup waits for the client's userinfo, and accepted game-initiated changes are published back to the owner. Two real clients pass automatic admission, roster configuration, captain assignment, force-ready, technical pause, remote captain timeout, resume and abort. Final Match Control layouts are verified at 960x540 and 800x600. The broader observer-role and camera matrix remains in progress.
- [x] Managed Duel retains accepted contestants while its legacy contender list initializes. Each contestant can request and resume their own timeout without gaining captain privileges. The two-client gameplay test verifies admission, independent A/B budget presentation, operator pause, remote timeout and abort. All 48 competitive contracts pass; membership, operation, menu-model and camera regressions also pass Linux ASan/UBSan. Third-player Duel queue turnover still needs runtime qualification.
- [x] Remote referee sign-in now completes its challenge response because the network engine services both client frame hooks, including frames without prediction. The production frame loop passes 200 cases on Windows and Linux. Three-client gameplay runs at 960x540 and 800x600 verify coach/broadcaster/referee disclosure, server camera permissions, role revocation, referee and operator technical pauses, a captain timeout and abort. Long readiness reasons still have a 4:3 clipping edge, and the wider competition matrix remains in progress.
- [x] The combined eight-round MVD replay test passes repeated paused camera cycling, free flight, forward seek, rewind, frame stepping and playback to the clean end record. Saved cameras survive round resets without advancing the paused clock. The later capture exposes a separate body/debris rendering issue retained in `docs/dev/competitive-match-reference-audit.md`.

- [x] Casual rules now import disabled ready-up and zero ready percentages consistently, preventing invalid-rule warmup stalls. Bots cannot call, cast, or inflate inherited option votes. Round turnover uses a dedicated world-reset message so remote clients retain the live match state. A sentinel bounds reliable message IDs without rejecting new round resets or existing announcer cues. The two-client Clan Arena test and one-human/two-bot option-vote test pass. Clients and servers must be updated together. Runtime evidence is tracked in `docs/dev/competitive-match-reference-audit.md`.
- [x] One Flag variants now retain explicit objective filters and use a stock neutral flag at a central unteamed pickup position when an original CTF map lacks one. Authored neutral objectives take precedence; both peers prepare the same map-entity list. The neutral entity cache is bounded across all three flags. All four flag variants completed two captures and returned to warmup on stock `q4ctf1`; a two-client One Flag capture test also passes. Map-pool placement review remains a competition qualification step.
- [x] Freeze Tag retains frozen bodies after overkills and traces rescue visibility from the rescuer's eyes, ignoring their own collision box. The stock-map test rescues a teammate, awards the rescuer a point and finishes two rounds. DeadZone excludes ineligible controllers, handles token loss and contested entry order, supports token-free authored zones and scores only during live play. The updated stock control test passes. All 46 competitive contracts pass, including Linux ASan/UBSan checks for the new rescue and round-team regressions.

- [x] Lit decals and other translucent materials authored with `noShadows` or `DECAL_MACRO` no longer cast unwanted solid shadows when translucent shadow options are enabled. Eligible translucent casters still work, and `forceShadows` retains precedence. The engine and companion material headers remain synchronized. Both renderers pass the bright Recomp decal fixture, the original Air Defense 1 comparison camera, four seeded random stock maps, and settled cutout-cache transitions; final qualification retains 15 runs and 69 engine captures. Evidence and remaining limits are recorded in `docs/dev/shadowmapping-final-audit-2026-09-05.md`.

- [x] Shadow-map budget and subview misses no longer sample obsolete OpenGL caster depth after doors move. Both renderers allow a two-pass light to make progress with a budget of one. Vulkan's coordinated light/cubemap/descriptor capacity now handles stock door views exceeding its former 64-light limit, and frame-scoped diagnostics no longer read recycled light records after leaving a light's area. Single-player stop-time holds a stable mover presentation pose and clock. Real door-team, portal, reversal, off-screen return, point-light, flashlight, and budget fixtures are recorded in `docs/dev/shadowmapping-movers-2026-09-05.md`.

- [x] Multiplayer warmup now identifies bots correctly, separates player and human population requirements, excludes bots from casual ready votes, and permits readiness withdrawal during countdown. Duel queue spectators no longer affect the minimum, team presence follows the configured minimum, and cancelling or restarting a match clears pending timers.
- [x] Multiplayer ranks now assign competition places and detect every tied group correctly, retain roster storage across refreshes, guard invalid client/team slots, and refresh the ranking before recording an individual result. Duel replicates contender membership and defers loser rotation until review finishes, keeping queue players out of the standings and both finalists on the final board. The production ranking/container/Duel regression and all 38 competitive contracts pass; the new regression also passes Linux ASan/UBSan. Duel clients and servers must be updated together; older development Duel demos have a different state layout.
- [x] DM/Duel resolve leaders from current scores; TDM/CTF announce the higher-scoring winner when both teams cross the limit. Score-limit ties keep their finishing delay before sudden death, and abandoned bot matches end when their last active human leaves. Rules schema 2 requires matching updated client/server packages and finishing active managed series before upgrading. See `docs/dev/competitive-match-reference-audit.md` for the MuffMode comparison and validation boundaries.

- [x] OpenGL HUD/post-processing views no longer discard the world shadow cache every frame. Shadow maps now include real light-emitting panel blockers that previously forced stencil fallback. Projected lights can retain opaque static depth while updating cutout coverage, and conservative bounds tests reduce point-caster submissions on OpenGL and Vulkan. Vulkan reports completed shadow work, and new lift/cutout transition profiles check real caster admission and cache use. Evidence and remaining replacement gaps are recorded in `docs/dev/shadowmapping-replacement-progress-2026-09-05.md`.

- [x] Turning shadows off no longer latches missing-caster fallback on otherwise supported lights. The transition test runner now exercises flashlight cycling, resource resizing, stacked caster insertion/removal, and visible cascade motion, and rejects unexpected missing-caster reports. Evidence is recorded in `docs/dev/shadowmapping-transition-tests-2026-09-05.md`.

- [x] Windows staging retries now honor `OPENQ4_INSTALL_CLOSE_RUNNING=0`, so an install failure cannot bypass the caller's request to leave active game/test processes running. Retry branches were exercised with stubbed install/process calls.

- [x] Memory-backed file writes now use amortized buffer growth, preserve file length when overwriting, and reject overflowing sizes before allocation. Reads and seeks avoid out-of-range pointer arithmetic, writes may safely use their own buffer as input, and clearing/replacing data respects caller-owned storage. The 8 MiB small-write regression drops from 513 allocations to 16; native and Linux sanitizer coverage is documented in `docs/dev/memory-file-robustness.md`.

- [x] Individual stock-map shadow tests identified and repaired false Vulkan point-light occlusion at the light plane and an OpenGL moment-sampler cleanup overflow. Vulkan cutout caster derivatives now run before discard. Storage map reruns and Medlabs with translucent moments passed without texture-state errors; the retained SP/MP map runner and clipping evidence are documented in `docs/dev/shadowmapping-map-tests-2026-09-05.md`.

- [x] Shadow coverage survives unnecessary emitter-panel fallback and optional update limits: empty emitter stencil probes no longer invalidate point lights; OpenGL preserves required map-only casters under budget/subview pressure, rejects partial multi-caster maps, and culls scaled point casters conservatively. Stock SP flashlight ownership and a 36-visible-light/890-caster-submission stress scene were exercised on windowed OpenGL/Vulkan with engine screenshots. The former Vulkan Air Defense 2 missing-shadow warning is resolved. Repeatable fixtures and remaining validation limits are documented in `docs/dev/shadowmapping-audit-2026-09-05.md`.

- [x] Optional shadow maps now refresh reliably when small light or caster changes alter their projection. OpenGL and Vulkan also validate the current cascade fit before ordinary cache reuse, including changes to camera coverage, cascade blending, and filter guards. Stabilized cascades retain their resolution at projection edges, modern OpenGL shadow bias no longer depends on undefined derivatives inside per-light branches, and Vulkan's receiver fallback honors lights authored with shadows disabled. Missing-resource diagnostics now identify the affected light. Existing settings remain compatible.
- [x] Traces against animated two-sided models now resolve joint weights through each generated surface's stable ID. Generated backfaces and reordered or omitted snapshot surfaces therefore map to their authored MD5/MD5R mesh instead of terminating the level with `NearestJoint: surfaceNum > meshes.Num()`. This resolves GitHub issue #151.
- [x] Machinegun scope markings now follow the interpolated camera that is actually displayed, rather than the weapon's last 60 Hz simulation pose. Turning while zoomed therefore stays aligned at high refresh rates in both single-player and multiplayer, and the same presentation-state route is available to other weapon zoom GUIs.
- [x] Asset parsing and GUI state handling now fail safely at several legacy edge cases. Default parser tokens initialize all metadata, lexer file loads reject impossible lengths, allocation failures, and short reads before touching the buffer, and GUI cursor/save values reject NaN or infinity. Relative input, absolute input, demos, and save restores now share one finite, aspect-aware cursor-bounds path instead of allowing invalid or off-canvas state to persist.
- [x] Corrupt auxiliary data now stops at a safe boundary instead of leaking state into later rendering or restore work. Engine and GameLib Base64 paths agree on bounded allocation, null handling, whitespace, and invalid characters; GUI timelines reject malformed or overflowing timestamps and clean up failed scripts; GUI window saves validate writes, structure, identity, and finite layout values; and malformed GUI/decal demo payloads or undefined decal projections are rejected before they can produce invalid geometry.
- [x] Post-map validation configs now enter the engine command queue instead of executing synchronously from the active game draw. This removes a reentrant screen-update path that could leave SP screenshots and save previews filled with untouched allocator memory; the strict windowed retail-PK4 baseline now passes SP capture, save/restore, demo playback, MP listen/client capture, image-integrity checks, and all map-specific CPU/GPU budgets.
- [x] Opening the in-game menu in single-player no longer waits for save-game, mod, key-binding, audio-device, display-mode, or multiplayer-model enumeration. The pause screen now activates immediately and refreshes each potentially expensive catalog only when the player opens the page that needs it; its background, fallback levelshot, and music are also re-referenced during normal level precaching instead of on the first Escape press.
- [x] Bitmap GUI text now selects its 12-, 24-, or 48-point source atlas using the final viewport enlargement as well as the authored 640x480-era text scale. Menus and HUD text therefore stay sharp at 1080p, 1440p, and 4K without the community workaround of forcing both font-limit CVars to zero, while text layout and low-resolution behavior remain unchanged.
- [x] Developer and nightly builds now regenerate their Git-derived version header on every build invocation while avoiding downstream recompilation when the value is unchanged. The console banner, executable metadata, engine, and both game modules therefore report the current commit without requiring a fresh Meson setup.
- [x] The built-in `dmap` compiler now resolves Quake 4 `func_group` entities before compiling geometry, just like the retail compiler and runtime map loader. This fixes false world leaks and missing grouped world geometry when compiling editable retail or custom `.map` sources, including the `hangar1` failure reported in GitHub discussion #124.
- [x] Linux save and configuration directories now work when an explicit home/save path contains an ordinary ellipsis (`...`) or another repeated-dot name. Directory creation still rejects a complete `..` parent component, preserving traversal protection without mistaking legitimate absolute paths for traversal attempts. This fixes the persistent-settings failure reported in GitHub issue #127.
- [x] Players can now choose smooth, sharp-mip, or deliberately pixelated material-texture sampling with the restored archived `image_filter` setting. All six retail Quake 4 filter names work on OpenGL and experimental Vulkan, changes update loaded textures immediately, and nearest-texel modes avoid anisotropic blur without overriding explicitly filtered HUD, font, shadow, or internal texture sampling. This resolves GitHub issue #121.
- [x] Milestone F now has a locally validated, default-off implementation for explicitly authored PBR materials, bounded OpenGL specular probes, and atomic OpenGL clustered decals. The 2026-08-23 current-source debug x64 evidence passed the static PBR/advanced-lighting checks, the final native 10/10 run, the final focused engine 2/2 rerun after an earlier 8/8 safe set, all four retail-PK4 compatibility roles, forced GL 3.3/4.1/4.3/4.5 PBR execution, the narrow validation-enabled Vulkan route, and exact `r_rendererModernQuality 0` rollback against leaf-disabled images. The stock baseline used all 40 retail PK4s with zero loose retail files; the generated PBR fixture remained temporary under `.tmp/`. PBR, probe, decal, and modern-visible controls stay default-off, `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS` stays `0`, and this local implementation exit is not final committed-package/platform/driver promotion or broad authored probe/decal visual qualification.
- [x] Milestone F's remaining quality leaves are implemented independently and default off on OpenGL and Vulkan. Bounded view-aligned froxel integration, depth-normal SSR, and fixed eight-tap depth-derived SSGI share the native scene-colour/depth presentation tail without allocating temporal history for effect-only use. The stable eight-float contract, final 11/11 native suite, static backend checks, Vulkan shader pin, combined GL/Vulkan `game/airdefense1` gameplay, individual OpenGL leaves, visible enabled delta, and exact master-off zero packet pass locally with engine screenshots and clean API/error counters. `r_rendererModernQuality 0` remains the one-setting rollback. This is a scoped screen-space implementation, not shadowed light-injected volumetrics, roughness-aware G-buffer reflections, world-space GI, or whole-frame modern-lighting promotion.
- [x] The post-roadmap performance pass removes two default-path regressions without enabling the guarded level-load cache experiment. Cinematic fast-forward no longer samples non-presented poses for every spawned entity on every 60 Hz tick, visible-frame interpolation samples active movers plus their bounded physics-team members and cleanup members, and maps without baked light-grid assets no longer construct a 22,024-point bake/debug layout during ordinary loading. On `game/airdefense1`, the controlled 186-second scripted skip fell from 7,113 to 5,770 ms at the exact same game-time endpoint and missing-grid setup fell from 357 to 1 ms; the isolated end-to-end run improved from 33,121 to 25,091 ms and steady pacing from 110.8 to 120.4 Hz, with the documented cache/scene-variance qualification. The final staged build also passed default `game/airdefense2`, pure auto-joined `mp/q4dm1`, and a four-role compatibility run against 40 retail PK4s with zero loose retail files on the tested Windows system.
- [x] Players and testers can opt into native-history temporal anti-aliasing/upscaling with `r_temporalAA 1`, and can add delayed GPU-time dynamic resolution with `r_rendererDynamicResolution 1`. The OpenGL and Vulkan paths keep HUD, menus, console, screenshots, and save previews out of accumulated low-resolution history; reject stale depth, camera cuts, capture frames, particles, unsupported deformation, subviews, in-world GUI, and view-model motion conservatively; and fall back to the current frame or established SMAA path whenever the exact history contract is unavailable. Both features remain default-off while clean-package and broader platform/driver qualification continues.
- [x] openQ4 now has a portable job service for bounded engine work without changing stock assets or compatibility formats. It bounds admitted lists and per-list work, supports dependency ordering and starvation-safe low/normal/high priorities, exposes cooperative cancellation and diagnostics, and uses sleepable waits instead of spinning. Dedicated servers default to deterministic inline execution, and `jobs_enable 0` still executes every accepted job instead of dropping it. The learned level-load read/PK4-inflate and framing/integrity pipeline is its first production consumer: workers read independently opened, identity-checked sources and publish sealed immutable DTOs while established owners retain asset-specific parse/decode, adoption, and upload authority. The foundation and consumer are integrated; clean final-package recapture and retained new-consumer parity/cancellation evidence remain promotion work.
- [x] Repeat map loads can now reuse only work learned from the exact installed content and settings. Successful loads record a per-map manifest with full SHA-256 mode/entity-filter keys, bound to the ordered VFS search, PK4/pure state, individual source identities, and load-affecting settings; a later exact match performs bounded cancellable reads, PK4 inflation, supported source framing, and whole-buffer integrity validation, then substitutes an immutable generation/type/source-identity DTO only after the ordinary authoritative VFS lookup succeeds again. Versioned private model, classic-world, and collision caches decode transactionally. Animation cache v3 follows the exact compiled-or-text parser source, adds a loose-source CRC and whole-record length/CRC trailer, validates private state all-or-nothing, and publishes atomically in both SP and MP. Corrupt or mismatched data is removed or ignored before source fallback, while packed MD5RProc worlds deliberately remain on their source path. `com_levelLoadModernization` defaults off and gates cache reads, writes, animation-cache work, and preload replay together; diagnostics also default off. The subordinate controls remain available for focused experiments after the master is enabled, and `com_levelLoadModernization 0` is the full classic source-path rollback. No retail assets or pure-PK4 behavior are changed. The local implementation gate is complete; final committed-package runtime/platform timings and Milestone B release-promotion evidence remain pending in `docs/dev/loading-cache-modernization.md`.
- [x] Renderer performance capture now uses one backend-neutral whole-frame GPU timing contract on OpenGL and Vulkan. Both paths resolve delayed timestamps without adding a current-frame wait, invalidate old generations across map/session and renderer/device discontinuities, and expose validity plus availability/drop/reset diagnostics through renderer ABI v11. `rendererBenchmarkCapture` pairs unique resolved GPU frames with high-resolution whole-renderer CPU samples in the versioned budget marker; target-hardware GL/Vulkan evidence is still required before performance promotion claims.
- [x] The renderer now shares ordered material/pass semantics, explicit GL/Vulkan clip and viewport conversion, semantic legacy/skinned vertex layouts, and typed generational buffer slices. The default-off `r_gpuSkinning` corridor admits only exact four-weight MD5/MD5R data, preserves signed residual weights and diffuse vertex colors, and emits the ordinary draw-vertex stream through bounded OpenGL/Vulkan compute paths. CPU positions remain authoritative for collision, hits, decals, overlays, and software consumers; stencil volumes and every invalid, unsupported, stale, exhausted, or unavailable case retain a named whole-surface CPU fallback. Dependency-light and renderer-module self-tests cover the shared contract. Clean-package SP/MP images, collision/hit digests, repeated animation-heavy CPU reduction, and platform/driver evidence remain required for promotion under `docs/dev/gpu-skinning-modernization.md`.
- [x] Internet-server administration and package negotiation now fail closed at their remaining legacy edges. `rcon2` keeps remote-console passwords out of packets, applies bounded challenge and reply budgets, and redacts private settings from console output, history, journals, configuration serialization, command expansion, and completion previews; plaintext rcon is disabled unless both relevant sides explicitly opt into it. Server-originated userinfo and synchronized-CVar dictionaries decode transactionally and can update only the CVar class owned by that wire message, so they cannot borrow authority over private or unrelated settings. Server-provided redirects and PK4 entries accept only bounded HTTP or HTTPS URLs with a syntactically valid DNS, IPv4, or bracketed IPv6 host. Standard Meson packages keep in-process direct PK4 transfer disabled while preserving validated web-redirect prompts; a separately integrated curl-enabled build additionally rechecks the URL, refuses redirects, applies connect/stall/total-duration limits, and retains package path, size, and checksum validation. The obsolete network-driven executable updater no longer downloads or runs code and ignores server-controlled release text and links.
- [x] Pure multiplayer can remain enabled without giving a server control over executable code. `si_pure 1` is no longer silently disabled; the ordered asset-PK4 list is still enforced, while the protocol 2.41 game-code field uses the stock 1.4.2 `game300.pk4` checksum only as a platform-independent compatibility token for an already loaded module from trusted local openQ4 package/module roots. Missing or unexpected tokens, modules, asset lists, and platform IDs fail closed, code-bearing server mods require the explicit `net_serverAllowServerMod 1` opt-in, and neither pure negotiation nor package download can install or restart into game code. The token is not a cryptographic measurement of the loaded module, a module-equality proof, or an anti-cheat guarantee.
- [x] Truncated and malformed multiplayer state now fails at an explicit safety boundary. Bit-message underflow is tracked across normal and delta reads, queued messages and delta user commands are length-checked, and audited SP/MP leaf readers stage decoded fields until their payload is valid while validating referenced slots, types, ranges, and remaining data. Invalid covered traffic is dropped; a late malformed top-level snapshot tears down the affected session before another game or presentation frame. The legacy entity lifecycle is not a whole-snapshot transaction, and this targeted hardening does not claim formal verification of every parser.
- [x] Multiplayer validation now reaches gameplay without menu-input automation. Supported MP test profiles explicitly launch with the archived `ui_autoJoin 1` setting and the retail-PK4 compatibility baseline proves that the local client is active, not spectating, outside the session menu, and drawing its HUD on both sides of the screenshot. Only tests specifically covering the join menu or initial spectator/join flow may explicitly set `ui_autoJoin 0`.
- [x] Multiplayer function keys once again use Quake 4's canonical impulse actions: F1/F2 vote, F3 ready, F6 team, and F7 spectate bindings execute real key actions, while the controls menu edits the same vote/ready actions. Ready presses retain their throttle but reach the server through the reliable ready message, so the warmup prompt names a real key; its stock two-line non-tourney layout uses the inline keycap size so the instruction is not clipped out of the authored 40-pixel HUD window. Existing configs migrate only the exact historical openQ4 defaults (`voteyes`, `voteno`, `ready`, `toggleteam`, and `spectate`) on those five keys; custom and compound bindings are preserved, and the idempotent `ready` console command remains available. Casual no-time-limit matches also normalize a retained positive `si_overtime` value to sudden death with a zero timed period, avoiding a rejected competitive-rules import at stock deathmatch defaults.
- [x] Retail-asset compatibility reports now describe what actually ran: unchanged approved retail PK4 bytes beneath packaged openQ4 overlays. Verification rejects dry runs, stale or missing expected-asset bindings, mismatched source provenance, recorded failure arrays, and altered role launches; reports also count every packaged-overlay virtual path that supersedes retail content instead of presenting an overlay-colliding run as stock-only evidence.
- [x] Successful Windows full installs and VS Code fast stages now apply one shared, explicit cleanup manifest so extensionless POSIX client/dedicated binaries and obsolete renderer `.dll.mainbak` files cannot survive a platform switch. The old `baseoq4/skins` directory is removed only when empty and populated local content is preserved. Stage-root or directory links, junctions, and wrong entry types fail closed; a known stale leaf symlink is unlinked without following its target.
- [x] Manual save-game previews now render one coherent full-size frame before a bounded CPU center crop and resize produces the intended 320x240 thumbnail, eliminating repeated or nested screen-effect copies at widescreen resolutions. Renderer readback also keeps row-alignment padding out of captures whose widths are not multiples of four.
- [x] Liquids now behave like complete combat volumes: projectiles and hitscan shots pass through while producing reliable crossing splashes and sounds, submerged projectile smoke becomes bubbles, and hitscan wakes cover only the underwater ray segment. Quake 4-height probes and a gravity-aware launch arc make water jumps reliable from clear pool ledges; the wading basin is also normally jumpable. Clear water remains readable, liquid acoustics suppress hall-like wet tails and duplicate jump splashes, and drowning, slime, and lava report distinct localized obituaries with graphical icons. The well-lit `mp/liquid_lab` developer map provides recessed deep, shallow, and wading water plus vivid cellular slime and bright lava; its sky ceiling, shadow-casting beams, illuminated deep-pool floor, lava heat haze, steam, bubbling surfaces, submerged ambience, and full-arsenal respawns make every behavior immediately testable.
- [x] The marine hovertank once again plays its engine and hover-pad loops throughout the vehicle sequence. Single-player now creates and updates the moving sound emitters just like the retail game and openQ4's multiplayer module; the later null-safe sound-world teardown keeps those emitters safe during map and engine shutdown. This addresses the missing vehicle audio reported in GitHub issue #114 without requiring different OpenAL, EAX, or speaker settings.
- [x] The built-in `dmap` compiler and other engine-side geometry tools now initialize their own triangle-surface allocator copy before use. This prevents light-volume generation from dereferencing an uninitialized allocator after the renderer was split into a runtime module, fixing the Windows crash reported while compiling `game/airdefense1` in GitHub issue #108.
- [x] Windows now resolves openQ4's per-user save root through the system Known Folder service, so redirected Local AppData and Saved Games locations are respected. Environment and working-directory fallbacks remain available for stripped-down sessions but reject malformed relative roots, while the displayed `fs_savepath` consistently uses native Windows separators.
- [x] Streaming and avatar-overlay workloads no longer send the Windows AMD OpenGL driver through openQ4's persistent-mapped upload/fence path. AMD systems retain the modern renderer but automatically use the safer orphaned map-range stream without persistent-path fence waits, avoiding the driver-timeout and follow-on access-violation corridor reported with OBS and PNG-tuber software in 0.10.0. Windows crash logs now also identify the module, load address, and relative offset that raised an exception, so driver or capture-hook faults are distinguishable from openQ4 faults without first symbolizing the minidump.
- [x] Story checkpoints no longer abort or crash when a stock map retains a transient area-location reference outside the saved object graph. The serializer once again writes that reference as null, matching retail Quake 4 behavior, while failed saves no longer retry object serialization from a destructor after map shutdown has released those objects. This fixes the timing-sensitive automatic checkpoint crash reported just after the first detonation-pack sequence in Interior Hangar (`game/hangar2`).
- [x] Every retail Quake 4 and Raven map-pack mapDef with a `loadimage` now has a complete center/left/right/top/bottom TGA+DDS levelshot set. The stock engine capture workflow supplied the 24 missing sets, while a source-indexed validation test keeps all 56 base and 7 map-pack loadimages classified, structurally valid, and complete. This resolves GitHub issue #11.
- [x] Those 24 levelshot sets now recreate the retail loading screens instead of showing unrelated views. Each retail camera was recovered by matching the retail image against engine renders (depth readback and PnP), and each scene was staged as the retail shot shows it: capture-the-flag bases with their flags, Dead Zone artifacts in a live match with their spin timed to the retail angle, sound-driven lights lit, a monitor the retail shot shows but the shipped map hides, and the lighting state and moment of each single-player scene, down to the Storage 1 drop-pod intro seen from behind the player's own lit pod. Storage 1's second visit also lost the Dark Matter Gun energy ball an older capture showed and has its core-room bridge extended as retail does. Tiles are 1024x1024, rendered from 2731x2048 sources. Where a retail shot came from a pre-release build (a machine in `game/medlabs`, a ceiling in `mp/q4ctf1`, floor lighting in `game/storage2`), the shipped content wins; `docs/dev/levelshot-capture.md` lists each set's staging and remaining differences. `levelshot` also accepts an explicit camera (`levelshot <size> <x y z pitch yaw roll> [fovX]`) and `r_levelshotSupersample`, sources larger than the window render as seamless sub-frustum regions instead of the broken viewport tiling, and the cheat-gated `levelshotProbe` command renders camera probes with optional depth, queued captures, and time-gated requests for headless capture tools.
- [x] Blended decals no longer draw as opaque squares in the world view behind the multiplayer join screen. The join screen's background blur captures depth with `GL_BLEND` and face culling switched off behind the renderer's state cache, and because "no blending" is expressed through blend factors with `GL_BLEND` left enabled, nothing turned blending back on for the rest of that view. The capture now restores both.
- [x] Lit surfaces no longer lose colour on the OpenGL renderer while SSAO, bloom, tone mapping, motion blur, temporal AA or resolution scaling is on. Those effects make openQ4 draw the view into a floating-point target, and the stock interaction programs never clamp their lighting term: a light behind a surface writes negative colour, which retail's 8-bit framebuffer turned into zero but the float target subtracted from the pixel. `noShadows` materials light their back faces on purpose, so the green cryo liquid in the tubes of `mp/q4xctf6` came out magenta, the bright background with the green taken away. openQ4 now floors those programs' colour at zero as it loads them, which matches retail and keeps HDR highlights above 1. Vulkan and the GLSL interaction paths already clamped.
- [x] A partial Quake 4 asset copy can no longer hide its missing dialogue media behind a successful core-PK4 check: client startup now warns when no unsuffixed `zpak_<language>.pk4` base archive is mounted and explains where to install it or point openQ4 at the retail files. Numbered patch archives no longer suppress this warning, matching the reporter-confirmed partial-install pattern where `zpak_english_01.pk4` through `_04` were present but `zpak_english.pk4` was absent. The check recognizes the supported base filename without claiming archive completeness, stays non-fatal for dedicated servers, and remains disabled with the existing assetless-validation override. Controlled `game/airdefense2` validation reproduced both linked symptoms with that incomplete media set: all dialogue was silent and the conversation thread remained before Bidwell's move, leaving him at `(5372, 2625, 244)` through 75 seconds. Restoring the unsuffixed archive moved him to the stock target at `(5437, 2575, 244)` by 60 seconds with no navigation failure. This root-causes and resolves GitHub issues #87 and #88 as an incomplete retail installation while keeping the startup diagnostic and player guidance explicit.
- [x] The original Apple Silicon launch crash tracked by GitHub issue #73 is now closed after two reporters confirmed it fixed in v0.8.0. Later FBO and single-player module-switch failures received their own fixes, and the remaining washed-out rendering report is tracked separately by issue #98; macOS support remains experimental pending current Apple-hardware signoff.
- [x] Dynamically expanded widescreen and ultrawide loading screens are now published atomically. A short or interrupted write can leave only a disposable staging file; it can no longer replace the live TGA with a truncated image that aborts map loading with `LoadTGA: incomplete file`. If publication fails, openQ4 uses the original levelshot and continues loading. This resolves GitHub issue #102.
- [x] Experimental macOS Vulkan can now find bundled MoltenVK in both supported loose-package layouts: beside the executable or under a `Frameworks/` child directory. SDL surface creation and the Vulkan module probe the same ordered paths, preventing a flat `.install` development run from missing the staged library or binding two different loader images. This resolves GitHub issue #95.
- [x] Large single-player levels on macOS no longer return to the menu when Apple's legacy OpenAL implementation runs out of buffer objects. Release and validation packages now build and bundle checksum-pinned OpenAL Soft 1.25.1 with package-relative loading, inside-out signing, architecture and deployment-floor checks, its LGPL notice, and the exact corresponding source archive. OpenAL allocation or upload failure also leaves the map running and falls back to streamed playback where resources permit instead of raising a fatal engine error. This resolves GitHub issue #122.
- [x] macOS OpenAL Soft builds fail clearly during configuration when the requested system package is not visible, instead of silently falling back to Apple's framework and then failing to compile against `AL/...` headers. The lookup is pkg-config-only; it began as the migration corridor delivered for GitHub issue #94 and is now supplied by the pinned package recipe used for releases.
- [x] Managed competitive multiplayer now has one cohesive, server-authoritative match system instead of scattered ready, vote, pause, team, demo and admin paths. An explicit competitive profile enables validated/frozen rules, readiness blockers, connection-scoped player/captain/coach/broadcaster/referee roles, durable rosters and substitutes, self-only coach/substitute withdrawal, Duel queues, team locks, tactical and technical pauses, typed proposals, recipient-filtered spectator follow/vitals, and BO1/BO3/BO5 map-pool/veto workflows; casual servers retain their existing behaviour. The localized Match Control surface exposes Status, Teams, Proposals, Rules, Series and Evidence from bounded recipient views with truthful denial reasons and confirmations. Series progress and its mutable report draft use one digest-protected schema-4 atomic checkpoint across maps without persisting names, addresses or client-slot authority; team-side mappings recover directly, while a trusted operator explicitly rebinds current Duel connections with `matchSeriesBind`. Managed matches also link series identity and an automatically owned server MVD into bounded schema-v2 per-map JSON evidence and an immutable schema-1 final series report promoted beneath `fs_savepath`, with recording/write failures isolated from the result. Managed activation cancels any inherited vote lifecycle, and legacy settings/kick/team/shuffle/restart/next-map adapters fail closed instead of bypassing typed authority. Local listen-server operators can grant a strictly observational broadcaster role through Match Control, and dedicated operators have the equivalent typed `matchBroadcaster` adapter. Supported placed, respawning major items are tracked authoritatively on the pause-safe match clock and projected only to explicit broadcaster/referee recipients; players, coaches and ordinary spectators receive no timers, and raw item tokens are never rendered. Captain invitations for neutral spectators remain fail-closed. The broadcaster role and recorded server MVD are supported, but no reachable engine Q4TV/repeater transport, delayed broadcast or live multi-POV path is shipped; the pure public-only repeater policy remains a fail-closed future integration boundary.
- [x] Bordered window placement now accounts for the complete native frame on Windows, X11, and macOS instead of treating the drawable client rectangle as the whole window. Saved and restored positions track the outer-frame origin, usable-area constraints reserve the actual title-bar and resize-border extents, oversized client requests shrink enough for all chrome to remain reachable, display/DPI moves persist the updated frame position, and startup repeats placement after the window server finishes decorating the window. Native Wayland continues to delegate absolute positioning to the compositor by design.
- [x] Malformed legacy console, configuration, and numeric input now fails safely instead of risking an invalid memory access or overflowing an impulse command. Command argument collection is bounded by both slot and storage capacity, escaped argument reconstruction grows safely, lexer lookahead preserves the next token, and extreme string growth is checked before allocator rounding or legacy 32-bit narrowing. Relative file mutations reject traversal, rooted paths, platform-specific filename aliases, and other non-portable targets before touching the save directory. In separately integrated curl-enabled builds, server-offered package transfers are confined to new PK4 destinations beneath that directory, including Windows device-name protection, reject invalid or overflowing advertised sizes and response types, enforce accepted sizes against the received HTTP body, and release temporary files on every completion path; standard Meson packages leave that direct-transfer path disabled. Multiplayer negotiation reports a deterministic empty game-package slot when no game-code package is requested.
- [x] Standalone game-module builds now refresh whenever either gameplay code or their shared engine support sources change, verify the complete staged snapshot by content, and compile the legacy inline-assembly SIMD implementations only for the 32-bit x86 target that supports them. This prevents stale modules and restores reliable x64 and modern-platform builds without dropping the existing x86 path.
- [x] Windows x64 engine and standalone game-module builds are warning-clean again. Legacy native-width conversions now use checked boundaries or native-width accounting, intentional allocator alignment is explicit and layout-guarded, dead SDK remnants are removed, and the cleaned warning classes fail the build if they return. SDL configuration no longer reports a false missing-libdecor-version warning on platforms that do not use libdecor, and the Windows validation wrapper now forwards an explicitly selected companion repository correctly instead of treating its parameter name as a path.
- [x] Bound controls are now easier to recognize at a glance: Settings key-bind rows show a bounded summary across keyboard, mouse, and controller inputs, while supported in-game prompts follow the device family the player used most recently. Keyboard bindings use clean labeled keycaps with square arrow symbols, upright mouse silhouettes fill the bound physical button without ambiguous numbers, and controller bindings use neutral positional graphics plus a localized Back/View label rather than assuming an Xbox, PlayStation, or Nintendo legend scheme. Inline icons now sit at nearly the full text-line height, important centered multiplayer prompts use a larger 150% treatment, and higher-sample rounded edges keep the procedural button art smooth at modern resolutions; the spectator HUD uses the same presentation for its follow-cycle and exit controls. Bind capture is safer too: Escape, Start, or Back cancels without erasing the action, while Backspace or Delete explicitly clears it.
- [x] Mods and server packages can now change a stock map's runtime entities without editing or redistributing the original map: a matching `.ent` supplies a complete entity-string replacement, while `.entx` safely appends point entities after that replacement or the stock list. Both formats are bounded, validate atomically, reject geometry and unsafe or ambiguous keys/names, preserve original collision loading, participate in map reload detection, and remain excluded from editor/export writes.
- [x] Single-player Arena matches now advance from their entrance ceremony into the countdown and live scoring phase. Disabling ready-up also transactionally zeroes the ready threshold required by the match-rule validator, preventing the clean-profile default from leaving combat in an infinite, non-scoring warmup with both scores stuck at zero. Each card now freshly imports its authored limits through the casual match profile while preserving the player's archived profile, so consecutive deathmatch-backed cards cannot reuse the previous card's time and frag limits. This fixes GitHub issue #110.
- [x] Single-player now includes an Arena Campaign beside the original Mission path: five escalating tiers combine Duel, Deathmatch, Team Deathmatch, Red Rover, and Clan Arena across twenty stock-map matches, with hand-picked character bots, boss fights, adjustable difficulty, persistent wins, and clear unlock progress. Tier briefings, effective-skill previews, a visual progress bar, distinct retry/replay/next actions, recommended keyboard and controller focus, hidden boss identities, explicit draw results, and reset confirmation keep the ladder readable and protect earned progress. The first three wins in each tier open its boss match, and defeating that boss opens the next tier; the final Makron duel now uses the distinct Stroggenomenon tournament arena. Each match now has an Arena-only ceremony: its challenge card expands into a cinematic no-ready entrance, the terminal score freezes the field for a collision-aware depth-of-field orbit around the victor, and the return passes through a staged report before visibly recording the win and pulsing new boss or tier gates on the ladder. Exact authored rosters, pre-countdown Duel contender assignment, authoritative score reporting, a campaign-owned result screen, and transactional restoration of temporary local-server settings make launches, completion, replay, disconnect, and quit paths deterministic. The ladder uses only required retail Quake 4 maps and openQ4's game modules, so no community map pack or replacement assets are needed.
- [x] The Demo Library, Single Player selector, and Arena browser now share Quake 4's stock rails, grid, panels, controls, and popup silhouette. Tiled chrome, full-viewport shades, and edge-anchored playback treatment fill additional horizontal and vertical display space without stretching the readable cards. Deliberate, input-safe transitions carry players from the main menu into Mission or Arena, between Arena views, and back again; campaign reset, progress, and result dialogs now expand on both axes while keeping their modern staged feedback within the original menu family. A final readability pass adds persistent Demo filter feedback, bounded browser columns with a selected-demo identity line, compact localized Arena card labels, roomier translated briefings, and modal exits that cover their nested content before closing.
- [x] Multiplayer bots now complete deliberate obstacle traversals instead of skipping their landing corners and jumping in place below elevated goals. Jump, drop, jump-pad, and teleporter routes retain their entry and exit; jumps are confirmed by leaving the ground and landing on the destination side, combat steering cannot disrupt them mid-action, and stuck detection measures real horizontal progress. Goal selection also respects one-way navigation, preventing bots from gathering below ledges they cannot climb. This fixes the two-jump route to the yellow armour crates on q4dm1 without map-specific content.
- [x] Multiplayer bots now play the match with substantially stronger general intelligence: they run, return and escort flags in Standard CTF, One Flag, Arena CTF and Arena One Flag, rescue frozen teammates, contest DeadZone artifacts and control zones, keep combat intent separate from movement objectives, and spread across inventory-aware, route-scored pickups instead of blindly choosing the nearest item. Exposed-point vision, real attacker memory, incoming-projectile prediction, gravity-aware projectile lead, complete stock-and-expansion weapon scoring, teammate/splash fire safety, goal hysteresis, path-progress deadlines, locally validated combat steering, transport-safe optimal routing, and reservation-aware team joins make them more purposeful, dangerous and reliable without map-specific bot files.
- [x] Multiplayer bots now adapt those foundations to live play instead of following a static nearest-target script. Navigation revalidates and repairs stale route edges around movers, crosses ordinary walk-activated doors while respecting locked or scripted barriers, weighs jumps and drops against walking, keeps A* optimal across cheap transports with a scaled lower-bound heuristic, and retries alternate roaming destinations. Combat uses lateral partial-cover visibility, real projectile hulls and gravity over segmented trajectories, projected teammate motion, future splash exposure and cover, and incoming-explosive world-impact or resting-fuse danger; decision-making adds weapon-aware spacing, fading cached pursuit, extra priority for threats to a friendly objective carrier, safe opposite-side dodge recovery, nav-projected escort formations and a defended capture perimeter. Distance-aware team roles split supported CTF variants between returns, interceptions, escorts, attacks and defence, carriers follow ordered per-instance assault chains, Freeze Tag uniquely matches rescuers to bodies, and DeadZone varies collectors, escorts, interceptors and zone contests with ownership and deadlock state. Attack & Defend, Overload, Harvester and Domination still lack complete authoritative runtime state and scoring rules, so their objective layer intentionally fails closed to general combat, pickup and roaming behavior rather than claiming false support.
- [x] Hits you land now register on the crosshair itself: four short angled marks bloom outwards and fade within a fraction of a second, in single-player and multiplayer alike. The marker tells you what the hit was worth - a graze stays small and quiet, a rail or rocket reads wider and brighter, a killing blow flashes white hot, armour lifts it towards white, and team or self damage shows amber or grey-blue instead of red. Sustained fire re-arms one marker rather than piling them up. It is on by default at `Settings -> Game Options -> Crosshair -> Hit Marker` and replaces stock Quake 4's crosshair recolour; turning the marker off brings that recolour back, and `hud_crosshairHitFlash 1` keeps both. `hud_hitMarkerScale` resizes it on top of your crosshair size.
- [x] Multiplayer pickups can now keep their original models while using icon-matched flat diffuse colours, with an optional soft upward lightness sweep that preserves authored transparency, normal maps, highlights, glow, and other effect layers. First-person weapons remain unchanged; opponent-held weapons can independently opt into flat colour without the sweep. Localized Game Options expose all modes while the stock presentation remains the default.
- [x] Multiplayer bots now both speak and fight like distinct characters instead of interchangeable arena personas. All sixteen character-owned voice files were rewritten around the retail cast's dialogue and backstory, with restrained extensions for the Strogg adaptations and openQ4-original characters. Their 1,792 event lines are joined by 558 character-specific replies to whole-word conversational cues from players and other bots—2,350 authored lines in total—with name-aware answers, sensible team routing, length-aware typing delays, the stock overhead typing icon, flood throttles, and hard reply-loop prevention. Dialogue lives separately from each bot's mechanics, making voices easier to extend without disturbing play tuning. Their movement, engagement range, survival instinct, threat selection, weapon choices, and preferred difficulty bands follow those same profiles; nine additional tactical axes now vary trigger initiative, target loyalty, opportunism, grudges, suppressive fire, strafe rhythm, weapon-switch timing, and aim height. Low-skill reactions and firing gates are less passive while their aim, tracking, lead, and mistakes remain intentionally weak. The normal skill curve still determines competence, match context is respected, and no player action is required.

- [x] The new **Demos** library brings recordings into one clear browser and one capability-aware player: native MVD and openQ4 render demos can pause, change speed, step, rewind, and fast-forward, while only full-world MVDs expose free flight and player following. MVD 1.2 now captures every active multiplayer instance and preserves instance-only reliable and positional events; its required feature bits, independently versioned game schema, and version-2 routed records leave the version-1 decoder available for MVD 1.0/1.1 recordings. The optional checked sparse timeline index, including valid no-index/zero-snapshot captures, retains bounded replay-from-start seeking. Render demos use a cooperative, sound-safe replay fallback with deferred restart and explicit handling for indivisible map/renderer work, and malformed legacy renderer or sound commands now stop playback with a warning instead of terminating the engine. Retail Quake 4 `.demo`/`.netdemo`, version-sensitive developer `.cdemo`, and incomplete `.part`/`.ucmd` files are identified honestly instead of being given unsupported controls; command demos pass a bounded nonfatal structural preflight before Play is enabled and are checked again around map load. Server-side MVD recording uses required-feature negotiation, bounded length-delimited records, per-record checksums, structural payload checks, a network-synchronized cvar allowlist, game-schema/simulation/content/protocol gates, per-frame and initialization work caps, recording limits, and staged `.part` finalization. Existing `recordNetDemo`, `playNetDemo`, and `stopNetDemo` aliases continue to operate on openQ4 `.mvd` files rather than claiming retail `.netdemo` compatibility.
- [x] 64-bit builds are now safer and more reliable across Windows, Linux, and macOS: engine/game allocation paths, media parsing, renderer uploads, operating-system handles, and editor callbacks no longer truncate native-width values. Oversized or malformed inputs fail cleanly, newly written event saves use a portable typed format while legacy saves remain readable, and strict Win64 plus LP64 build and contract checks prevent regressions. No configuration or save reset is required.
- [x] The experimental native Vulkan renderer now preserves stock Quake 4 material effects instead of dropping specialized stages or requiring replacement content. Its compatibility paths cover the environment, monochrome, and heat-haze ARB families; displacement, depth/blur, ghost, sniper, multiply, MedLabs, and AL GLSL families; and guide-driven parallax, custom lighting, water, and refractive glass. The material parser also accepts the retail `fp20Program` and `dsdt` syntax, while guide programs whose original shader source is not shipped are reconstructed natively from their authored interfaces. Windows x64 single-player gameplay validation has exercised representative map, weapon-blur, displacement, and special-effects-controller paths with Vulkan validation layers clean, including effective 4x MSAA with SMAA. OpenGL remains the default and recommended release renderer while Vulkan's broader parity, multiplayer, cross-platform, and promotion evidence continues under `docs/dev/archive/2026-10-18/plans/2026-07-16-vulkan-renderer.md`; Vulkan initialization failures still fall back safely to OpenGL.
- [x] Stock materials no longer appear excessively glossy under Vulkan: unpacked bump maps now expose their authored red X-normal through alpha, matching the legacy interaction-shader convention and the OpenGL backend without replacement textures or material overrides.
- [x] Level loading screens now keep their background art, logos, and other authored imagery visible throughout Vulkan map loads. Vulkan restores purged GUI images on first use just like OpenGL instead of leaving most of the screen black until the end-of-level media reload; no content overrides or configuration changes are required.
- [x] Vulkan world lighting now preserves the full stock interaction contract: conditioned light and material stages, bump/diffuse/specular decomposition, Raven custom lighting and parallax, vertex colors, texture matrices, additive blending, culling, polygon offset, opaque/translucent depth rules, retail shadow ownership, mixed map-plus-stencil caster composition, subview-safe scissors, and optional stencil depth bounds all follow the OpenGL reference. No replacement materials or configuration changes are required.
- [x] The native Vulkan renderer now preserves stock projected and map-authored decals: bullet holes, scorch marks, stains, and lit decal surfaces retain per-stage lifetime/depth fading, vertex color, multiply and classic blend modes, alpha tests, culling, material and stage polygon offsets, pre-fog ordering, and `r_skipDecals` behavior. A Windows x64 `game/airdefense1` machinegun-impact run exercised the stock `textures/decals/bullethole` GLSL path in gameplay with Vulkan validation layers clean; no replacement content or player action is required.
- [x] The experimental native Vulkan renderer now renders the complete stock shadow path instead of dropping shadows outside its original point-light prototype. Retail stencil volumes and mapped shadows cover point, projected, parallel, and global lights; preserve LOCAL/GLOBAL interaction ownership; support static, animated, packed MD5R, two-sided, and perforated/hashed-alpha casters; and retain translucent receiver behavior. Mapped shadows add up to four stabilized cascades, fixed or rotated PCF, PCSS-lite, hardware or manual depth comparison, exact static-light reuse, and per-view update budgets. Lights containing both map-only and stencil-only casters combine the partial map with ownership-specific supplement volumes that contain only map-missing casters, avoiding duplicate hard silhouettes, and the lazily allocated point-cube pool covers both receiver ownerships of every admitted light instead of cutting dense stock scenes off at sixteen maps. The update budget defers a map only when that receiver ownership has a complete stencil result; ownerships containing map-only casters may exceed the nominal budget to preserve correct shadows. Correctness-required ownership maps are admitted before discretionary maps, and render-texture subviews without stencil render the required maps instead of selecting an unusable stencil fallback. Genuine resource or submission failures reject incomplete maps and use complete same-frame stencil coverage when available. If neither shadow path can cover an affected receiver, Vulkan preserves its direct illumination and reports the missing shadow resource. No replacement content or player action is required, and OpenGL remains the default renderer while Vulkan is experimental.
- [x] Stock Quake 4 `progimg/` DDS results now load directly instead of rebuilding hundreds of image programs from source, including nested program names and RXGB normal maps. A clean-cache Windows x64 Air Defense 1 A/B with DDS replacements enabled reduced the measured image phase from 18.2 seconds to 4.9 seconds, estimated loaded texture storage from 756.6 MiB to 213.5 MiB, and total map load from 29.2 seconds to 15.7 seconds; unsupported compression paths retain the decoded/source fallback.
- [x] Maps with baked light grids now begin play without uploading every area atlas during map setup. Visible areas and portal neighbors stream through the existing runtime path, while `r_lightGridPreload 1` remains available for players who prefer fully preloaded traversal. `r_lightGridResidencyFrames` can now reclaim packed as well as loose atlas VRAM after areas leave use.
- [x] Level image loading now reads each generated binary image or packed chunk into one validated backing allocation and orders pending image work by path locality, reducing filesystem calls and per-mip heap churn across Windows, Linux, and macOS. SP/MP map-media deduplication also uses stack-built keys, a map-sized hash table, and releases its transient key set before gameplay.
- [x] Large SP and MP maps now spend less time repeating entity setup work: inherited media stays cached from entityDef parsing, each map instance scans only its authored overrides, and prepared spawn dictionaries avoid a second declaration lookup/default merge. Air Defense 1 issued about half as many map-media requests in validation, while `q4dm2` map population fell from 776 ms to 338 ms in the comparable Windows x64 run.

- [x] macOS packages now provide a self-contained, drag-installable `openQ4.app`: openQ4 PK4 data and splash resources live under `Contents/Resources`, signed SP/MP Mach-O modules live flat under `Contents/Frameworks` in accordance with Apple's nested-code layout, and `OpenQ4RuntimeLayout=self-contained-v1` keeps damaged-app diagnostics unambiguous. Finder launches bind `fs_cdpath` to the embedded resource root, and loose diagnostic/dedicated binaries discover the sibling app runtime without duplicating the large payload. Packaging, archive, architecture, dependency, deployment-floor, install-name, signing, symbol, support-intake, and release-smoke validation follow the embedded paths while older complete adjacent packages retain a runtime fallback.
- [x] macOS platform-sensitive debugging now has a repeatable manual ASan+UBSan workflow for both OpenGL and Metal bridge builds: PCH is disabled, the client/dedicated/SP/MP Mach-O outputs are checked for arm64 sanitizer instrumentation, an assetless fail-fast runtime probe is available by default, and toolchain, build, staging, and runtime diagnostics are retained without treating hosted sanitizer output as hardware signoff.
- [x] macOS dedicated-server builds now receive a hosted startup gate for both release bridge configurations: each native arm64 commit and push job uses test-only assetless media, loads the staged `game-mp` module, reaches a live dedicated server frame, and requires clean shutdown while retaining a machine-readable report and logs. Retail-map and physical-hardware dedicated-server signoff remain pending.
- [x] Experimental Intel macOS CI is now configured without widening release claims: matching `macos-15-intel` engine and `openQ4-game` jobs build thin x86_64 outputs, enforce the macOS 11 floor and package-relative module install names, validate staged client/dedicated/SP/MP architecture, and run both bridge renderer safety plus dedicated MP-module lifecycle smokes. Intel and universal2 downloads remain unpublished until hosted results and real Intel gameplay/package/signing evidence are accepted.
- [x] macOS universal2 runtime groundwork now preserves thin-package module preference while allowing both slices of a future merged client or dedicated server to fall back to one trusted `game-sp_universal2.dylib` / `game-mp_universal2.dylib` pair. Self-contained app integrity checks accept that pair only when the running slice's thin pair is unavailable, and the documented merge contract requires all code items, symbols, install names, inside-out signatures, notarization, and dual-architecture evidence before publication.
- [x] The hosted universal2 assembly corridor now accepts only clean, matched ARM64/x64 thin provenance, rejects unequal shared payloads and stale modules, validates both host-generated MoltenVK signatures before requiring the signature-stripped library bytes to match exactly, merges all five code items, requires exactly `arm64` plus `x86_64` after package signing, records both dSYM UUID slices, and runs a native assetless dedicated-server module-load smoke for each renderer bridge. It remains unpublished until final Apple Silicon and Intel signoff is accepted.
- [x] Linux releases now include directly executable AppImages for x86_64 and aarch64 alongside the loose archives. The images are built from the already-verified package directory with pinned, SHA-256-checked AppImage tooling; retain the complete offline docs and unified `baseoq4` payload; bundle the native dependency closure without changing detached-symbol build IDs; reject unsafe metadata, symlinks, runpaths, or unresolved libraries; and launch the final upload candidates assetlessly through both Wayland/Weston and X11/Xvfb before publication. ARM64 AppImage naming follows the selected preview or first-class release tier.
- [x] Linux and macOS system-console windows now render in logical window coordinates on HiDPI/Retina displays, so the console fills the window with readable text instead of occupying only the upper-left quarter; button and input hit testing follows the same scaled coordinate space after resize or display-scale changes.
- [x] Linux ARM64 first-class publication can no longer be unlocked by generic signoff prose: an opt-in, non-publishing candidate run builds only native ARM64 from a pushed prospective-release branch and emits the exact first-class-named archive plus pending TOML. Accepted evidence lives on an immutable separate ref to avoid a self-referential source commit, the tested candidate must reach `main` by unchanged fast-forward, and the first-class ARM64 job requires exact source/version/package identity plus matching SHA-256 values for the archive and all four staged/packaged ELFs before upload. Accepted review fields reject placeholders and require a valid ISO review date plus meaningful field-specific content. The publishing job permits only its exact expected asset-name set and rehashes the downloaded ARM64 archive against the accepted record before upload. `SOURCE_DATE_EPOCH` stabilizes compiler date macros, while any remaining rebuild or toolchain drift fails closed and requires a fresh physical-hardware signoff; ordinary preview releases are unchanged by default.
- [x] Linux release downloads now prove both supported desktop paths before packaging: release builds explicitly use the bundled SDL with Wayland and X11 enabled, then the exact post-strip x64 and ARM64 staged clients run assetlessly through bounded renderer/window lifecycle probes under headless Weston and Xvfb. Driver/backend and clean teardown markers are required, and failure diagnostics are retained without changing the ARM64 preview support claim.
- [x] Modern Linux diagnostics now harden several legacy edge paths shared by x64 and ARM64: long SP/MP session commands and admin-ban GUIDs are always NUL-terminated, empty or quoted text and high-byte filenames are handled without invalid indexing or overlapping copies, empty silhouettes and malformed legacy LWO subchunk ordering fail safely, and AI obstacle path refinement compares the intended AAS area numbers instead of boolean-negated integers.
- [x] Direct Meson build-directory launches now stage the required `baseoq4/mod.json` beside generated PK4s and game modules, so development and sanitizer runs use the same validated runtime identity as installed packages. If early startup still fails, the POSIX fatal console no longer reads cvars after engine shutdown, preserving the original diagnostic instead of replacing it with a use-after-free.
- [x] Renderer frame allocations now carry an explicit 16-byte payload alignment on 64-bit systems and obtain their backing blocks through the matching aligned allocator instead of relying on padding sized for 32-bit pointers or incidental heap alignment, preventing misaligned command/data access across Linux x64 and ARM64 allocator configurations.
- [x] Legacy OpenGL vertex-cache draws now form attribute addresses as integer byte offsets, so a valid VBO at offset zero is neither dereferenced as a null object nor rejected as missing. This removes sanitizer-only undefined behavior across ambient, shadow, material, visibility, and renderer-debug passes while preserving CPU-backed cache pointers.
- [x] Empty and duplicate-free triangle surfaces now skip zero-length copies, clears, moves, and sorts whose allocator-backed pointers may legitimately be null, allowing stock map geometry cleanup to proceed under ASan/UBSan without changing non-empty surface processing.
- [x] Default vector-set and vector-subset hash grids now initialize their box size before deriving the hash table size, removing an uninitialized signed-overflow path encountered while loading stock LWO models.
- [x] SP and MP entity and client-entity spawn IDs now use defined unsigned bit packing before returning their legacy 32-bit representation, preserving sentinel IDs during stock entity construction without a signed-left-shift failure on Linux x64 or ARM64.
- [x] Renderer and GameLib joint handles now model their real 32-bit integer-index contract instead of a single-value enum, so stock animated-model definitions can use nonnegative joint indices under strict Linux runtimes while retaining the `-1` invalid sentinel.
- [x] SP/MP player mute and friend masks now validate client indices and use unsigned 32-bit shifts, including the stat-window no-selection path, while debug-HUD masks use bounded unsigned indices and a ranged cvar; invalid UI/console values therefore disable or no-op safely instead of producing negative or sign-bit shifts.
- [x] SP/MP state-thread results now use their intended integer protocol, preserving embedded stage and millisecond-delay payloads from stock movers without loading out-of-range enum values under strict Linux runtimes; savegame restore reads the integer representation directly.
- [x] Empty animation metadata and other zero-length ranges now pass safely through the shared SIMD copy/set/zero boundary, allowing null-backed empty containers without invoking undefined C library behavior during stock SP/MP map loading under strict Linux runtimes.
- [x] The legacy SP/MP random generator now performs its intended 32-bit state wraparound with unsigned arithmetic while preserving the signed seed representation used by saves and network messages, keeping stock AI animation selection deterministic without signed-overflow failures on Linux x64 or ARM64.
- [x] SP/MP inventory restore and weapon-mod grants now validate current, explicit, and name-resolved weapon slots before indexing mod state, so the normal `-1` no-current-weapon sentinel safely forces a reload instead of reading before the inventory array during stock player spawn.
- [x] SP/MP weapons now initialize their render-light state, handles, owner pointer, and saved flashlight flag at construction, so stock player spawn can choose its first-person shadow-suppression light safely before full light setup and partially initialized weapons can be destroyed cleanly under strict Linux runtimes.
- [x] Multiplayer weapon lights now share SP's nonzero per-slot light-ID scheme, so client 0 no longer maps its muzzle flash to the renderer's reserved zero sentinel and first-person player/weapon shadows remain suppressed under that light.
- [x] SP/MP players now initialize their focus-friendly state before the first live-frame focus trace, preventing strict Linux runtimes from loading an indeterminate Boolean while preserving the normal neutral-target default.
- [x] Multiplayer async-network startup, shutdown, and viewer-capacity changes now skip viewer-array clears/copies until repeater storage has actually been allocated, allowing dedicated servers with zero viewers and repeater grow/shrink transitions to avoid passing null-backed empty ranges to memory routines under ASan/UBSan.
- [x] The renderer now grows its per-view draw-surface list without calling the C library on a null zero-length source during the first active frame, allowing valid empty initial lists under strict Linux runtimes before normal surface submission begins.
- [x] SP/MP repair bots now begin with defined arm joints, repair timers, effect distance, and repair-state Booleans before their first unconditional effect update, while nailgun guide-lock state and timers are initialized before first-frame zoom targeting; both stock paths therefore behave deterministically across Linux x64 and ARM64 allocator layouts.
- [x] The map compiler now builds stale `.reg`/`.lin` cleanup paths with dynamic strings instead of appending into a fixed 1 KiB buffer, and ARB program parsing initializes its section cursor before target dispatch.
- [x] Distant sky/sun shadows retain visible caster silhouettes instead of inheriting the much softer local-projector kernel: parallel and global projected sources now scale both PCF and PCSS radii with `r_shadowMapDistantFilterScale` (default `0.35`), the modern descriptor reports the same effective kernel, verbose shadow diagnostics expose the selected source policy, and cascade fitting reserves the shader's actual maximum filter guard.
- [x] Experimental Apple Silicon macOS gameplay no longer has to choose between a startup crash and flat ambient-only rendering: the default Apple OpenGL 2.1 policy now lights eligible stock surfaces through the existing neutral GLSL 1.10 interaction shader, uses a hardened CPU-memory/simple-ARB fallback per unsupported surface, and retains the old no-interaction path only as explicit emergency mode `r_appleARB2Interactions 3`. Client-memory draws now unbind stale array/index buffers, and generated-character fallbacks avoid the packed full-interaction programs.
- [x] Offscreen antialiasing failures now degrade safely instead of aborting after game-module startup: render-target creation reports the symbolic framebuffer status, GL/context details, and every attachment; returns a nonfatal failure; retries the forward RGBA8 + depth/stencil target through lower MSAA steps to `0`; records requested/effective samples; and uses the existing direct-render path if no FBO can be created. Resize and resolve paths reject null/incomplete targets instead of dereferencing them.
- [x] macOS app launches now resolve staged content independently of Finder's process working directory: the app validates embedded `Contents/Resources` and uses it as `fs_cdpath` even when Steam/GOG retail discovery supplies `fs_basepath`. Hosted push jobs require assetless launches for both bridge variants, release jobs launch the packaged app executable from an unrelated directory and verify `fs_cdpath`, and the Bash Meson wrapper propagates the macOS 11 deployment target to dependencies when unset.
- [x] macOS script/module troubleshooting is less noisy and more actionable: already-relative `baseoq4`, `q4base`, `fs_game`, and `fs_game_base` include paths no longer enter the OS-path converter, qpaths beginning with a game directory normalize correctly, selected filesystem/module paths are logged, and support summaries retain render-target/MSAA, interaction-fallback, path, and module lines.
- [x] Experimental macOS ARM64 builds now link the AudioToolbox framework required by SDL3's CoreAudio backend, so both OpenGL and Metal packages can link their client executable instead of failing on unresolved AudioQueue symbols.
- [x] OpenGL renderer robustness is stronger across driver tiers and repeated video restarts: render-target names are tied to their owning context generation, framebuffer validation is removed from the bind hot path and cached per cubemap face, texture-unit binding has core DSA, EXT DSA, and compatibility fallbacks, VAO element-buffer state is tracked correctly, multi-bind fallback is target-aware, upload rings try idle slots before bounded fence waits, debug contexts queue structured driver messages safely, GPU-driven shaders reject invalid buffer indices, and renderer validation fails on hidden GL errors or failed benchmark thresholds.
- [x] The single-player weapon wheel now uses a smoother circular depth-of-field gather with a per-pixel blur radius and depth-edge rejection, reducing star-shaped smears and foreground/background color bleed while keeping the nearby view weapon legible.
- [x] Performance presets now change only real, registered runtime controls: their 1x-to-16x anisotropy ladder drives the renderer instead of being ignored in favor of the hardware maximum, texture downsizing uses typed/range-checked cvars without a duplicate case-only alias, obsolete no-op image settings no longer pollute configs, every applied value is verified, and an unexpected normalization failure rolls the entire preset back instead of leaving a partial profile active.
- [x] Shadow-map correctness and cost rework (phase 0 of the 2026-07-09 shadow plan): the projected caster path now applies a real slope-scale depth offset in the caster shader (the previous `glPolygonOffset` state was silently inert against shader-written depth, which is why no bias cvar combination could resolve acne vs. detachment), point-light casters no longer leave fragment depth undefined on the default path and store slope-biased radial depth in both variants, hardware depth-compare sampling with linearly filtered (hardware PCF) taps is now the default for projected and point paths with PCSS-lite automatically selecting the raw-depth path, caster passes bind their program once per pass instead of per surface, receiver-plane bias derivatives are computed in uniform control flow, and the PCSS filter-radius guard uses the correct bound. `r_shadowMapSkipStencilShadows` stops generating/linking proven-replaceable stencil volumes; a failed map restores sticky stencil ownership, and Vulkan preserves direct illumination unshadowed in any failure frame where neither map nor stencil ownership can be submitted instead of visibly dropping the light.
- [x] SDL3 Windows startup now keeps the splash screen stable through renderer startup: openQ4 sets DPI awareness before creating the Win32 splash window and removes the splash at the render-window handoff, avoiding the brief scale/position jump when the SDL3 window initializes.
- [x] Objective popups now preserve retail simple-window clipping behavior, so long stock objective titles such as "Regroup with Rhino Squad" render fully instead of losing their final characters.
- [x] Failed or incompatible save restores no longer crash a second time while the game tears down a partially restored actor attachment. Cleanup now safely releases an attachment whose physics state was never installed, while preserving the normal visual and collision teardown path for fully restored entities. This fixes the save-load crash reported in GitHub issue #107.
- [x] Savegame handling is stricter and more recoverable: new saves carry a generated engine/GameLibs source snapshot stamp plus payload sync markers, an end footer, an exact wire-ABI stamp, and whole-file CRC protection; the session save header/description and GameLibs payload writers detect short writes before committing staged files; and staged saves must validate their persistent-player dictionaries, payload version/ABI, CRC, and footer before replacing an existing slot. Generated description sidecars and preview TGA headers are validated before promotion, the load path commits persistent-player state only after the full save header is valid and preflights gameplay compatibility before tearing down the current map, the save/load menu filters malformed save headers and unsafe slot names, description sidecars are read through a bounded parser and must still map back to their save slot, outbound dict/winding/object data is bounded, AI movement path caches are initialized and serialized with bounded live entries, and source stamps are stable across Windows and Unix line endings. Build/source drift is diagnostic for schema-compatible version 3 saves; version 2 requires one of six exact Windows x64 snapshots, and unstamped legacy saves require the current build plus the Windows/MSVC x64 `raw1` ABI. Unsupported or corrupted saves now fail at preflight or the nearest later restore boundary with a clear ABI, checksum, marker, or footer diagnostic instead of surfacing later as invalid AI path lengths or other drift errors.
- [x] Released v0.10 Windows x64 saves now restore consistently across optimized and debug GameLib builds: the exact affected version-3 source tuple defaults its two absent player liquid-state fields, the reader recognizes the empty physics frame that MSVC identical-code folding caused optimized writers to omit, and current writers derive frame ownership from source declarations instead of linker-foldable function addresses. The reporter's exact issue #123 fixture passes its original physics and `ReadTrace` boundaries; a fresh stock `airdefense2` save/reload completed in active gameplay with a visually reviewed engine screenshot, while final HiDef-mod confirmation remains with the reporter because the supplied archive omitted that mod's GUI/assets.
- [x] Savegame restore now also bounds SP/MP script interpreter state before rebuilding scripts from disk: call-stack depth, local-stack usage, function indices, statement pointers, stack bases, pop parameters, and multi-frame event names are validated so malformed saves fail at restore with a clear script error instead of indexing fixed interpreter arrays.
- [x] Savegame restore now rejects malformed SP/MP scalar state before it can steer later arrays or script cleanup: script-program watermarks, actor animation channels, client entity numbers, crawl-effect joint indices, crawl directions, and simple animated-entity counters are range-checked during restore.
- [x] Savegame sound and render restore is stricter: sound-world payloads now use checked primitive/string reads and writes with practical emitter/channel caps and sound-class validation, restored sound emitters retain their serialized indices instead of aliasing reusable empty emitters, trailing empty emitters are omitted from new saves, and SP/MP render entities now reject malformed saved bounds in release builds instead of relying only on debug asserts.
- [x] Save construction now hashes SP/MP game-object pointer lookups while preserving serialized object indices, avoiding repeated full-list scans as large maps discover and write object references.
- [x] Savegame UI restore is stricter: GUI window, simple-window, winvar, script, and arcade-minigame payloads now use checked block reads for saved scalar/vector/color state, clear stale child pointers before resolving saved window ids, and reject malformed Bust Out powerup enum values instead of silently continuing with partially restored UI data.
- [x] Savegame physics and mover restore is stricter: SP/MP articulated-figure payloads now reject body/constraint count mismatches in release builds, and elevator restores validate saved state plus floor-table counts before appending restored floor records.
- [x] Single-player save slots are now harder to lose or load incorrectly: version 3 checksums the complete `.save` through its footer, rejects invalid or over-512-MiB files before scanning them, validates format, ABI, and map availability before tearing down the running map, and commits synced description, preview, and payload staging through recoverable same-directory backups. Explicit no-preview markers prevent stale thumbnails from returning after recovery, while bounded state and typed object/GUI references reject corrupt restores close to the offending field. Cross-OS or cross-architecture transfer remains blocked by `raw1`, and save/load remains unavailable in multiplayer and on dedicated servers.
- [x] Load Game preview frames now read the selected material's dimensions and resize as a centered fit inside the stock preview area, preserving 4:3, widescreen, ultrawide, and portrait images without stretching. The fit also compensates for the optional retail-style stretched UI mode, and invalid material dimensions safely fall back to the classic 4:3 ratio.
- [x] Experimental macOS release packaging now fails cleanly when required release collateral is malformed: a placeholder or corrupted `collect_macos_support_info.sh` is reported as a one-line package error instead of escaping as a Python traceback before docs, binaries, signing, or archives are generated.
- [x] Experimental macOS signoff evidence recording now reuses the signoff archive validator's bounded text reader when parsing accepted reports and updates the evidence index through a same-directory temporary file plus replace, so oversized/swapped report members and interrupted recordings cannot leave unchecked or partially written release evidence.
- [x] Experimental macOS staged-package validation now rejects symlinked `.install` roots, symlinked `baseoq4/` game roots, and symlinked payload entries before parsing support-collector content, matching the release/archive policy that macOS payloads must be real files and directories rather than hand-staged links.
- [x] Experimental macOS support collection now tolerates sparse launch environments without `HOME`: home-scoped logs and DiagnosticReports are skipped with archive notes while package-local logs are still inspected, and staged/archive validation now requires those guards in `collect_macos_support_info.sh`.
- [x] Experimental macOS Apple-host guest scripts now resolve `~` through a validated `GUEST_HOME` from `HOME` or `OPENQ4_GUEST_HOME`, the host workflow exposes that override as `-MacHome /Users/<name>`, child tools receive the resolved `HOME`, malformed/nonexistent guest-home paths are rejected, wrong-host Darwin errors stay first, and Meson shims, workspace defaults, and Desktop launcher creation use the resolved home.
- [x] Experimental macOS signoff archive validation now caps tar member path and path-segment lengths before report/log parsing, so malformed copy-back archives with oversized path metadata fail with clear validation errors instead of producing unwieldy evidence diagnostics.
- [x] Experimental macOS package validation now rejects stale root engine binaries before publication: local `.install` validation, package roots, unsigned/runtime archives, manual release staging, and Apple-host signoff staging fail if extra `openQ4-client_*` or `openQ4-ded_*` files for another architecture are present, and manual release staging now also rejects extra `.dylib` game modules instead of only wrong-platform `.dll`/`.so` modules.
- [x] Experimental macOS staged-package validation now treats the support collector as a required runtime file: Darwin Meson installs stage `collect_macos_support_info.sh` beside the app and loose binaries, local staged validation checks that it is executable, UTF-8/LF-only, size-bounded, still contains the privacy/no-launch markers and bounded truncation fallback, and does not contain known environment-dump, launch-redirection, or xattr-value dump patterns; signed/notarized DMG source-tree preflight and macOS runtime archive validation reject placeholder, malformed, or privacy-unsafe collectors; and the Apple-host/manual-release workflows check the same support-collector markers before accepting a package.
- [x] Experimental macOS Apple GL 2.1 first-frame hardening now covers the July 5 issue #73 follow-up report for 0.6.91: after the degraded `ARB2 interaction driver bypass` path runs, openQ4 restores client-array, texture-unit, ARB program, stencil, and state-cache baseline before later classic frame passes, then records one-shot post-bypass breadcrumbs (`ARB2 interaction bypass state restored`, `ARB2 interaction bypass light scale`, `ARB2 interaction bypass ambient rescue`, and `ARB2 interaction bypass frame tail`) so any remaining crash identifies the next operation reached.
- [x] Experimental macOS Apple GL 2.1 first-frame hardening now covers the July 6 issue #73 follow-up report for 0.6.92, where the bypass reached `ARB2 interaction bypass light scale` before `SIGSEGV`: the degraded path now forces neutral `backEnd.lightScale`/`overBright`, skips the post-interaction `RB_STD_LightScale()` fullscreen pass entirely, and records `ARB2 interaction bypass light scale skipped` before continuing to the ambient rescue and frame-tail breadcrumbs.
- [x] Experimental macOS app-bundle detection recognizes renamed `.app` bundles that still launch from a standard `*.app/Contents/MacOS` layout, selecting their embedded runtime or the legacy adjacent-package diagnostic instead of falling through to generic base-path probing. Release packaging remains pinned to `openQ4.app`.
- [x] The original adjacent-layout macOS startup preflight was corrected to expect SP/MP game modules under packaged `baseoq4/` rather than `q4base/`; that location now remains only as a compatibility fallback for complete older experimental packages, while current self-contained apps use `Contents/Frameworks`.
- [x] Experimental macOS support collection is safer for public issue reports: generated support archives now use private permissions, are written through a temporary tarball before rename, refuse to overwrite an existing archive target, and bound copied package/log text plus DiagnosticReports text without falling back to full-file copies if truncation fails, so unusually large local files cannot create oversized support bundles.
- [x] Experimental macOS support collection now creates its output tarball through an output-directory `mktemp` path and rejects package-root or output-directory paths containing control characters before collection starts, closing a predictable-temp-name edge and preventing malformed archive paths in user reports.
- [x] Experimental macOS support collection now publishes the final support archive with a same-directory hard link from the private temporary tarball, so the collector fails instead of replacing a target that appears during collection.
- [x] Experimental macOS support collection now self-validates the generated support tarball before publishing it: the collector rejects empty or oversized compressed archives and runs a `tar -tzf` listing check before exposing the final support file to users.
- [x] Experimental macOS support collection now bounds direct diagnostic command output too: `sw_vers`, `system_profiler`, `xcodebuild`, `xcrun`, and similar command captures are stream-limited before redaction, preventing unusually noisy tools from inflating public support archives or temporary collector files.
- [x] Experimental macOS support collection now bounds and sanitizes generated report sections as well: package listings, path-resolution summaries, binary architecture/dependency reports, renderer/OpenAL summaries, signing checks, and quarantine reports are stream-limited before redaction, and copied public text replaces embedded NUL/control characters before entering the support archive.
- [x] Experimental macOS Apple-host workflow inputs now reject asset basepaths inside reserved workflow workspace children (`openQ4/`, `openQ4-game/`, `incoming-quake4/`, and `results/`) on both the host preflight and guest scripts, preventing `rsync --delete` asset installation from overwriting synced source trees, scratch extraction data, or collected signoff evidence.
- [x] Experimental macOS direct guest scripts now reject control characters in `OPENQ4_GUEST_HOME`/`HOME`, expanded workspace and asset-root paths, and the incoming Quake 4 asset tar path before creating directories, logs, launchers, or extracting archives; the asset installer also keeps its case-insensitive reserved-path guard import-complete for real execution, not only syntax checks.
- [x] Experimental macOS host workflow remote archive expansion and result collection now reject a control-character remote `HOME` fallback before expanding `~` or creating copy-back archives, so omitted `-MacHome` runs follow the same path hygiene as explicit guest-home overrides.
- [x] Experimental macOS host workflow reserved-directory preflight now compares `-MacWorkspace` and `-MacBasePath` through the `-MacHome`-resolved `~/` form with normalized case-insensitive keys, and source sync trims trailing workspace slashes before appending `openQ4/` or `openQ4-game/`, preventing mixed `~/...`/`/Users/...` inputs or trailing slash workspaces from slipping past host checks or producing double-slash extraction targets.
- [x] Experimental macOS signoff result copy-back now downloads remote archives into a same-directory temporary file, rejects empty transfers, rechecks the final path, and publishes without overwriting an existing archive for the same run ID, reducing the chance of stale or half-written local evidence.
- [x] Experimental macOS Apple-host result archiving now writes the remote copy-back tarball to a same-directory temporary file, rejects empty or unlistable tar output, and hard-links the validated archive into the final remote name without replacing an existing target before the host downloads it.
- [x] Experimental macOS guest signoff/build result directories now fail closed if the selected `<run-id>-<action>-<bridge>` directory already contains files before logging starts, so reruns cannot silently blend stale renderer evidence, logs, or reports into a new archive; operators should use a fresh `-MacOSRunId` or remove the old directory.
- [x] Experimental macOS archive and evidence intake now rejects more mismatched handoffs: package archive validation opens `.tar.gz` and `.tar.xz` artifacts with exact compression modes instead of auto-detecting mislabeled tarballs, and host/guest/signoff tooling caps result run IDs and action tokens before they become long paths, archive names, or evidence records.
- [x] Experimental macOS zip package validation now reports corrupt member payloads as clean package-validation errors, so a damaged `.zip` that opens successfully but fails while reading `Info.plist`, manifests, localized strings, or support metadata no longer escapes as a low-level Python `BadZipFile` traceback.
- [x] Experimental macOS package archive output and permission validation tightened again: release archives and DMGs refuse output paths inside the package tree they are archiving, zip runtime archive validation now checks every member's unsafe mode bits instead of only the final member, and dSYM symbol archive validation opens `.tar.xz` artifacts as xz-compressed tar data so mislabeled debug archives fail before publication.
- [x] Experimental macOS signoff result collection now preflights multiplayer smoke evidence on the Apple host before creating the copy-back archive, so incomplete `renderer-mp-smoke` output is caught before a result bundle reaches the local validator.
- [x] Experimental macOS signoff evidence recording now validates package artifact names before updating the evidence index: recorded artifacts must be safe exact-variant filenames for `openQ4` macOS arm64 `opengl`/`metal` packages, must avoid case-insensitive duplicates, must include artifacts for every requested graphics bridge, and must not include unrequested bridge artifacts so incomplete, stale, typoed, or path-like artifact entries cannot become release evidence.
- [x] Experimental macOS signoff evidence recording now rejects symlinked archive inputs before validation and symlinked evidence-index inputs before update, so completed Apple-hardware signoff records cannot be accepted or written through a symlinked handoff.
- [x] Experimental macOS signoff evidence recording now rejects control characters in free-form release metadata and in validated signoff report/log text, and reports malformed signoff tarballs as clean recorder errors, preventing broken Markdown records or Python tracebacks during release evidence intake.
- [x] Experimental macOS artifact identity checks are stricter: runtime package validation now requires the embedded `SYMBOLS.txt` `runtime_archive=` field to match the archive being validated, symbol manifests reject path-like runtime or symbol archive names, and completed signoff evidence archives must keep the canonical `openq4-macos-results-<run-id>.tar.gz` filename so renamed or stale copy-back bundles cannot pass as the current Apple-hardware run.
- [x] Experimental macOS `SYMBOLS.txt` validation is stricter: dSYM symbol archives now parse manifest headers and binary rows, reject duplicate or unknown manifest keys, require the runtime archive name, symbol archive name, and package suffix to match the artifact being validated, and verify the expected app/client/dedicated/game-module entries with valid SHA-256s, positive sizes, matching Mach-O architecture UUID lines, and exact dSYM mappings so mixed OpenGL/Metal, renamed, stale-architecture, or incomplete symbol bundles fail before publication.
- [x] Experimental macOS archive validation now caps total expanded payload size as well as individual entries: runtime packages, dSYM symbol archives, and collected signoff evidence archives reject archives whose declared uncompressed contents exceed conservative publication/evidence limits before metadata or evidence files are read.
- [x] Experimental macOS symbol packaging now fails closed on unsafe staging/output paths: stale symbol staging roots must be real directories before cleanup, dSYM outputs reject stale symlinks or files instead of unlinking through them, and symbol tarballs refuse symlinked input roots or archive outputs placed inside the symbol staging tree.
- [x] Experimental macOS archive validation now has explicit resource and permission guardrails: runtime package archives and dSYM symbol archives reject excessive member counts, symbol archives reject Finder/AppleDouble sidecars and apply the bounded metadata-size check before reading `SYMBOLS.txt`, and collected macOS signoff evidence archives reject special-mode or group/world-writable members before evidence is accepted.
- [x] Experimental macOS workflow archive intake is stricter before Apple-host sync or asset installation: remote source/GameLibs transfer archives and Quake 4 asset tarballs now get structured `tarfile` member preflight before extraction, reject duplicate members, case-insensitive member collisions, macOS metadata/debug sidecars, malformed/control-character paths, symlinks, hardlinks, and special files, and use `COPYFILE_DISABLE=1` on remote tar listing/extraction paths to avoid materializing AppleDouble sidecars during workflow transfers.
- [x] Experimental macOS robustness improved again: app-bundle startup failures now report the expected runtime architecture, per-file reasons such as missing/non-executable entries, and any obvious stale x64/x86 or wrong-platform game-module files found beside the app; the support collector no longer depends on GNU-style `dirname --`, suppresses AppleDouble sidecars when creating support archives, records the detected runtime architecture token in package path reports, rejects symlinked/non-directory package roots, refuses unsafe support archive output targets, no longer follows symlinked package/log/crash-report evidence inputs, and skips DiagnosticReports names that are not archive-safe; macOS package archive validation now rejects missing/symlinked inputs, unknown archive formats, and control-character member names in tar/zip artifacts, macOS dSYM symbol archives reject symlinked inputs plus duplicate or case-conflicting members before publication, and the macOS asset installer preflights tar members with structured metadata before extraction; `macos-static` now syntax-checks the Apple-host guest bootstrap, asset-install, and build/signoff scripts; and the host/guest Apple workflow keeps Meson build output constrained to `builddir*` or `.tmp/` paths.
- [x] Experimental macOS support archives now collect richer package/startup diagnostics without launching the game: `system/rosetta.txt` records collector-process architecture plus `sysctl.proc_translated` state for unsupported Rosetta reports, `logs/renderer-summary.txt` preserves renderer startup, driver-quirk, ARB2, and fatal-signal breadcrumbs from existing logs, `package/binary-architecture.txt` captures read-only `file`/`lipo` architecture output, `package/dylib-dependencies.txt` captures `otool` dependency and game-module install-name output, `package/signing.txt` captures read-only `codesign`, `spctl --assess --type execute --verbose=4`, and `xcrun stapler validate` output for the app, loose binaries, and game modules, and `package/quarantine.txt` reports extended-attribute names plus `com.apple.quarantine` presence without copying xattr values. The collector also inspects any loose `x64` or `x86` executable names that are present so accidental extra-architecture package contents are visible without expanding the supported release matrix.
- [x] Experimental macOS crash intake now includes dedicated-server reports too: the issue template and support-data guide distinguish `openQ4.app`, loose client, and loose `openQ4-ded_arm64` launches, while the support collector copies recent `openQ4-ded*.ips`/`.crash` DiagnosticReports alongside app/client crash reports.
- [x] Apple OpenGL 2.1 compatibility now bypasses ARB2 light interactions on the fragile M4/M5 startup path reported in GitHub issue #73 comment 4874832470; the fallback records `ARB2 interaction driver bypass`, keeps the CPU-backed vertex cache and `SimpleInteraction.vfp` shader selection, and applies the ambient rescue floor so affected experimental macOS launches degrade visually instead of walking into the first-frame `SIGSEGV`.
- [x] OpenAL audio has been restored to the end-of-day June 23 sound implementation; later speaker-detection, voice, streaming, diagnostic, and source-lifecycle experiments were removed from the shipped audio runtime.
- [x] OpenAL playback is steadier in dense or long-running audio scenes: queued streams now refill and drain their available buffers, trim stream chunks to each sample's playable range, convert sound times to sample offsets with bounded 64-bit math, align restart offsets to the retail/OpenAL block boundary, keep leadin-to-loop handoff inside playable ranges, restore retail-style mixer ranking based on audible volume plus protected voice/dialogue channels, skip inaudible channels before they can compete for dense-scene voice slots, restore retail diversity mapping for sample choice and deterministic loop starts, apply looping random-start diversity to leadin-then-loop sounds too, make `no_dups` sample selection see the outgoing channel before channel replacement, release stopped channels and their hardware voices immediately, accumulate and save/restore retail-style shifted playback time for live `frequencyShift` changes so resumed voices, completion, leadin volume, and shake/amplitude sampling stay on the same clock, remove the non-retail one-shot/loop/music priority boosts that could reorder busy combat scenes, stop evicting existing emitter channels during sound admission, preserve a real extra mixer candidate while keeping the full configured voice budget, apply music volume/mute handling to the retail musical sound class as well as flagged music samples, reset reused OpenAL sources to a neutral silent mix state before new playback, release failed starts or backend queue/update failures immediately, keep paused or queued voices busy until playable buffers are drained, and fail closed on unplayable payloads instead of holding silent sources.
- [x] The macOS no-runtime validation track is now a named local profile: `macos-static` runs Python static/policy tests, synthetic Apple GL 2.1 coverage, synthetic macOS package/archive fixtures, shell syntax checks, and push/PR validation dry-runs without building or running openQ4 on macOS. Windows/Linux maintainers can invoke it through `validate_macos_static.ps1` or `validate_macos_static.sh`, and optional renderer self-test binaries are guarded so they run only on available non-macOS hosts.
- [x] Experimental macOS GameLibs alignment is now enforced across both repositories: `openQ4` keeps SDK/game-library Darwin and Clang support in the companion `openQ4-game` repo instead of a copied `src/game` mirror, static checks guard arm64 `.dylib` names and `@loader_path` install names, macOS build logs report concrete architecture strings such as `macos-arm64`, package/support metadata records both openQ4 and `openQ4-game` commits, and `openQ4-game` CI now checks ARM64-sensitive allocation, savegame serialization, pointer-width, and alignment-sensitive allocation assumptions.
- [x] Experimental macOS audio-provider policy is explicit: the original Apple-framework decision established mandatory OpenAL Soft library-location, install-name, codesigning, license, and notarization gates; issue #122 supplied the evidence to change providers, and the pinned OpenAL Soft package now satisfies those gates. Crash/support intake continues to request vendor, renderer, device, and EFX lines through `logs/openal-summary.txt` collected from existing logs.
- [x] Experimental macOS package robustness is clearer without requiring Finder testing: self-contained app launches fail with a localized macOS startup error when embedded data or signed modules are missing, older adjacent packages retain their localized compatibility diagnostic, package validation requires those resources, and the support collector writes `package/path-resolution.txt` with app resource/module roots plus copied `fs_basepath`/`fs_cdpath`/`fs_savepath` log lines.
- [x] Experimental macOS missing-module reports now name the trusted roots and exact paths checked by the loader, and the privacy-filtered support archive preserves that diagnostic so damaged or older package layouts can be distinguished without suggesting unsafe module copies into retail data folders.
- [x] Experimental macOS found-but-unloadable module reports now identify the selected module and preserve the platform loader's `dlopen` error in support archives, making signing and dependent-library failures diagnosable alongside missing-module layouts.
- [x] The localized startup message for an old adjacent macOS package now distinguishes that legacy layout from current self-contained apps, so it directs users to reinstall rather than incorrectly suggesting that dragging a current `openQ4.app` to `/Applications` is unsupported.
- [x] Hosted macOS Debug artifacts now declare their requested and actual bridge scope, exact `openQ4` and `openQ4-game` provenance, clean-tree state, and optional runtime-probe setting, explicitly separating hosted build/package coverage from a completed player-operated Apple-hardware signoff and preventing one selected bridge artifact from being read as both-bridge evidence.
- [x] Experimental macOS app-bundle detection now recognizes renamed `.app` bundles that still launch from a standard `*.app/Contents/MacOS` layout, so renamed Finder bundles still select the embedded runtime or legacy package diagnostic instead of falling through to generic base-path probing. Release packaging remains pinned to `openQ4.app`.
- [x] Experimental macOS app-bundle package-root checks now reject symlinked adjacent runtime entries at launch, so hand-assembled or damaged packages fail with a clear `symlink` diagnostic instead of silently following `baseoq4/`, client, dedicated, or game-module symlinks.
- [x] Experimental macOS releases now publish symbolication data without putting debug payloads in runtime packages: the macOS packager writes `SYMBOLS.txt` with binary SHA-256s, Mach-O UUIDs, and the matching dSYM archive name, creates separate `openq4-<version>-macos-arm64-<bridge>-symbols.tar.xz` artifacts for the app executable, loose client/dedicated binaries, and SP/MP game dylibs, and the manual release workflow verifies/uploads those symbol archives beside the runtime DMG or unsigned tarball.
- [x] A manual non-publishing universal2 release-candidate workflow now assembles exact ARM64/Intel builds for both macOS bridge variants, preserves source provenance and symbols, and can perform post-merge Developer ID signing, notarization, stapling, Gatekeeper, and per-slice install-name checks. Its artifacts are retained as evidence only; user-facing macOS downloads remain experimental and arm64-only pending hosted and real-hardware signoff.
- [x] Experimental macOS Apple GL 2.1/ARB2 startup hardening is stricter for issue #73-style reports: renderer driver quirks now match Apple OpenGL 2.1 using normalized vendor/version and selected context facts, synthetic M4/M5/macOS 15/macOS 16/Tahoe/unknown-renderer cases force CPU-backed vertex cache plus `SimpleInteraction.vfp`, bypass ARB2 light interaction draws on the unstable compatibility path, quirk logs show VBO/PBO/simple-interaction/ARB2 impacts, and the fallback now fails closed with `Unsupported Apple OpenGL 2.1 compatibility path` if the required simple interaction shader pair cannot load.
- [x] Experimental macOS renderer crash diagnostics now record the last renderer startup phase for issue #73-style failures: normal logs show ordered breadcrumbs through `R_InitOpenGL`, ARB2 reload, upload setup, backend selection, `first ARB2 interaction handoff`, and the Apple GL 2.1 `ARB2 interaction driver bypass`; POSIX fatal-signal output prints `last renderer startup phase`; and static validation keeps those crash breadcrumbs, docs, and release notes aligned without requiring macOS runtime testing.
- [x] Experimental macOS crash intake is now more actionable for issue #73-style reports: GitHub has a dedicated macOS crash template, packages include a safe `collect_macos_support_info.sh` collector, the user guide explains which terminal/log/crash artifacts to attach, and a privacy-filtered `logs/renderer-config.txt` captures only renderer/performance cvars for graphics-setting regressions. macOS release notes point to that guide, and static validation keeps the template, docs, packaging contract, and no-macOS-runtime boundary aligned.
- [x] Experimental macOS support-claim guardrails now cover curated release notes and front-door package docs: the README, packaged HTML README, platform support page, and 0.6.5 curated notes all say Apple Silicon/arm64 where current macOS packages are described; the release-completion checklist now blocks first-class/stable wording without `docs/dev/macos-signoff-evidence.md`; and GitHub issue #73 stays in the macOS release-note limitation boilerplate until it is closed or explicitly carried forward with matching evidence.
- [x] Experimental macOS renderer/backend support now has an explicit policy: `docs/dev/macos-renderer-backend-policy.md` records the SDL3/OpenGL release path, keeps the `-metal` package labeled as a Metal bridge rather than native Metal, classifies the legacy native Cocoa/OpenGL backend as comparison-only diagnostic infrastructure, and lists the design gates required before any native Metal or native-backend release claim can be made. Meson now warns when macOS `platform_backend=native` is selected so local comparison builds do not look like release support coverage.
- [x] Experimental macOS native backend containment now has a dedicated policy and static guard: `docs/dev/macos-native-backend-containment-policy.md` keeps Carbon and NSOpenGL isolated to the native comparison backend, keeps SDL3 Darwin sources and SDL3 framework linkage Carbon-free, keeps native Cocoa/OpenGL support wording out of curated release notes, records that the native backend should get a removal plan after SDL3 coverage is mature instead of being kept indefinitely by default, and keeps native Metal implementation work blocked until a separate renderer design plan exists.
- [x] Experimental macOS support now has an explicit matrix policy: `docs/dev/macos-support-matrix-policy.md` records the current Apple Silicon/arm64-only release scope, keeps Intel Mac, universal2, and Rosetta outside published support, and requires signoff evidence for both the macOS 11 floor and the latest public macOS before any first-class macOS claim can ship. macOS signoff reports and evidence records now include architecture policy, CPU architecture, OS matrix role, Xcode version, and macOS SDK version so release notes can match the actual Apple toolchain and OS coverage.
- [x] Experimental macOS package UX is release-gated: the macOS Package Layout And Release Policy records the self-contained app contract and requires mounted-DMG, independently dragged-app, whole-package loose-tool, terminal, embedded-path, path-resolution, and Gatekeeper signoff evidence before macOS support claims can grow beyond the current experimental tier. The manual release workflow has an explicit first-class macOS support tier that fails instead of publishing unsigned fallback packages when signing/notarization credentials are missing.
- [x] Experimental macOS runtime signoff is now evidence-gated: `docs/dev/macos-signoff-evidence.md` records the active run ID, archive checksum, hardware/OS details, OpenGL/Metal bridge coverage, OpenAL provider, package artifacts, openQ4 and `openQ4-game` commits, SP/MP evidence, and release-note limitations; `tools/macos/record_signoff_evidence.py` turns a completed-checklist archive into that record, while collected signoff archives now require a dedicated `renderer-mp-smoke` evidence folder before validation accepts the hardware signoff bundle.
- [x] Multiplayer server entry is now more deliberate and Quake Live-like: new clients start as spectators at the map intermission camera when available, see a Q4-styled join screen with server details and gametype-aware join options, and can set `ui_autoJoin 1` to keep the old immediate-join behavior. That screen is now a compact centred panel rather than the full in-game menu: it is authored inside the plain 640x480 GUI canvas so it keeps a 4:3 shape on a widescreen display, it carries only the match summary and the join choices the gametype allows, and the live view behind it is softened through the shared special-effect blur controller. The interactive VS Code multiplayer launch configurations pass `ui_autoJoin 0` so manual runs exercise it; automated multiplayer profiles still pass `ui_autoJoin 1`. The panel is built from shipped Quake 4 interface art and carries settings and leave-match symbols, and `docs/dev/ui-visual-design.md` records the visual rules it follows. The OpenGL half of the special-effect blur was aligned with the native Vulkan pass at the same time, so the softened backdrop is identical on the classic OpenGL path, the OpenGL renderer module and Vulkan.
- [x] Pull-down console mouse selection and clicks now line up with the visible cursor hotspot, so the cursor no longer appears a row below the point that receives input.
- [x] Documentation now labels macOS support as experimental wherever current support or release packages are described, including the README, getting-started guide, build/platform docs, input guidance, packaged HTML docs, VM workflow, and curated release notes.
- [x] Experimental macOS packaging and signoff checks now reject case-insensitive path collisions, Finder/AppleDouble metadata inside result archives, and renderer evidence that contains only placeholder directories before artifacts are accepted.
- [x] Experimental macOS VM asset installation and package validation fail closed on more host filesystem hazards: Quake 4 asset archives and installed asset trees now reject macOS metadata/debug artifacts plus case-insensitive path collisions, and macOS ZIP packages reject directory entries even when external mode bits are missing.
- [x] Experimental macOS VM automation now validates host SSH targets, user names, identity files, transfer roots, transfer archive outputs, result directories, source archive roots, and guest workspace/asset paths before syncing or running `rsync --delete`, and refuses Finder/AppleDouble/debug sidecars before creating source or asset transfer archives.
- [x] Experimental macOS VM workspace and asset roots now must be absolute POSIX paths or `~/` paths: the host rejects relative, dot-segment, empty-segment, and backslash inputs, and the guest scripts recheck the expanded paths before source sync, asset install, build, smoke, signoff, or result collection work.
- [x] Experimental macOS VM sync and result collection now revalidate remote archive files, extraction targets, single-root transfer archives, result workspaces, and result archive output paths on the Mac side before unpacking or copying evidence back to the host.
- [x] Experimental macOS release packaging now enforces an explicit `openQ4.app` bundle allowlist, rejecting unexpected nested app files such as stale helpers, frameworks, or debug payloads both in the package directory and in generated archives.
- [x] Experimental macOS app bundle validation now also enforces the expected bundle directory layout, including the required `Contents/Frameworks/` code location, so stale empty or unexpected directories fail before signing, notarization, or archive publication.
- [x] Experimental macOS signed app bundles now treat `Contents/_CodeSignature/CodeResources` as a first-class, tightly validated bundle manifest, allowing legitimate ad-hoc/Developer ID signatures while still rejecting unexpected signature-side payloads.
- [x] Experimental macOS DMG creation and notarization upload now re-preflight the source tree at the final image boundary, catching symlinks, special files, unsafe modes, Finder/AppleDouble/debug metadata, case-insensitive collisions, and quarantine attributes before `hdiutil` or notary archive creation.
- [x] Experimental macOS archive output handling now rejects stale symlink, directory, non-regular, or symlink-parent paths consistently for release archives, direct DMG creation, and temporary notarization ZIPs before external Apple packaging tools run.
- [x] Experimental macOS signoff result collection now preflights result directories before creating the host-return archive, rejecting symlinks, special files, Finder/AppleDouble metadata, case-insensitive path collisions, empty report/log files, and renderer evidence folders with no files while suppressing macOS copyfile sidecars during tar creation.
- [x] Experimental macOS VM scripts now reject symlinked asset archives and guest helper scripts, suppress macOS copyfile sidecars while extracting asset tarballs, validate host copy-back targets before `scp`, and publish Desktop launchers plus signoff reports through temporary files instead of writing final paths directly.
- [x] Experimental macOS signoff archive validation now rejects symlinked archive inputs, control characters in tar member names or required UTF-8 report/log contents, excessive member counts, reports that do not record a staged client path, and reports/logs that do not reference the expected result and renderer evidence directories.
- [x] Experimental macOS guest signoff runs now validate result directories before opening workflow logs, refuse symlinked staged client launch targets, and recheck existing Quake 4 asset basepaths for symlinks, special files, Finder/AppleDouble metadata, and case-insensitive path collisions before gameplay smoke tests.
- [x] Experimental macOS guest scripts now refuse stale symlink, directory, or special-file workflow log, launcher, and signoff report paths before writing, so repeated Apple-host signoff runs cannot accidentally append evidence to the wrong filesystem target.
- [x] Fullscreen startup is smoother on the shared SDL3 backend: when `r_fullscreen 1` is saved, the render window now stays hidden until its configured fullscreen state is applied, so launch transitions from the splash screen to fullscreen without flashing a windowed game window.
- [x] macOS validation is stricter before signoff evidence is accepted: staged Mac payload checks now reject stale or wrong-platform game modules, missing icon/splash support files, symlinks, and special files, while collected signoff archives must include binary-architecture evidence plus macOS display, audio, USB, and Bluetooth inventory.
- [x] Multiplayer bright-skin character materials now preserve their authored glow overlays, so models such as Pro Marine keep the intended head color instead of receiving a flattened, desaturated overlay from the modern material path.
- [x] Cinematic framing now preserves retail Quake 4 composition across display shapes: top, bottom, left, and right black bars update with the viewport so the visible cinematic area stays 16:9 through letterboxing or pillarboxing as needed.
- [x] The temporary OpenAL voice experiment and its compatibility controls were removed; the live audio code now matches the June 23 baseline directly instead of carrying a runtime switch.
- [x] Multiplayer players can opt into clearer enemy and teammate readability: client-side outline and rimlight cvars add colored visibility effects to player bodies, heads, and world weapons without changing server snapshots, hit detection, or stock asset requirements.
- [x] Multiplayer appearance settings are easier to tune from the in-game menu: the Settings page now separates Self, Enemy, and Teammate tabs, uses dropdown model selection, exposes force-model, outline, rimlight, and brightskin controls, and previews the selected model with the active visibility colors.
- [x] Multiplayer score displays are refreshed from current rank data when the full scoreboard or mini score HUD draws, and unknown ammo snapshot values no longer spam `idBitMsg::WriteBits` overflow warnings during listen-server play.
- [x] Listen-server multiplayer clients now receive short-lived hit, impact, and explosion presentation events more reliably when PVS data is not available at the exact emit frame, and first-person view-weapon visuals update on the final client prediction step to reduce loopback weapon jitter.
- [x] Content-only mods are easier to run: when a selected mod does not ship its own SP/MP game module, openQ4 now falls back to the matching `baseoq4` module instead of requiring copied dynamic libraries in every mod folder.
- [x] Release packages now avoid debug-build performance costs on every platform: Windows release jobs moved from Meson `debug` to the optimized diagnostics profile already used by Linux and experimental macOS, release builds disable runtime asserts with `b_ndebug=true`, and symbol artifacts remain available through Windows PDBs plus Linux debug-symbol archives.
- [x] Linux packages no longer carry obsolete staged icon copies whose names differ only by case, allowing the archives to extract on Windows and other case-insensitive filesystems; staged validation now rejects case-insensitive path collisions on every platform before CI uploads an artifact.
- [x] Follow-up Strogg flyer/fighter destruction fix: map-authored `func_fx` explosions now service newly submitted BSE effects immediately like retail Quake 4, so one-shot explosion, smoke, sound, and debris bursts are no longer skipped before their first visible frame.
- [x] Single-player tram combat is more stable in debug and local validation builds: long straight AI projectile aim probes are split into collision-safe segments, preventing the `game/tram1b` tram-rail fight from tripping the debug `Clip.cpp` huge-translation assertion while keeping the collision guard active for invalid oversized sweeps.
- [x] Validation and release tooling now rejects more unsafe source inputs before build/package work starts: source roots, GameLibs roots, Meson-discovered source files, Windows runtime staging paths, OpenAL overrides, staged GameLibs manifests, macOS signing files, and documentation links/sources fail closed on symlinks, missing files, or root escapes; legacy Linux shell helpers also pass broad `bash -n` syntax checks.
- [x] Build, release, and legacy PK4 tooling now fail closed on more malformed inputs: legacy PK4 builders, installer generation, staged-content pruning, release-doc generation, and default GameLibs discovery reject symlinked roots before resolving paths; release archives strip host ownership/mtime metadata; package text manifests replace output symlinks with LF-normalized files; packaged asset changes are no longer treated as docs-only; Discord release announcements validate webhook/GitHub URLs; legacy Linux PK4 helpers run under Python 3 and clean up temporary update work safely.
- [x] Save-game handling is more robust across platforms: save, load, and delete now share the same scrubbed slot filenames, malformed empty names fail before touching disk, failed description sidecar writes clean up partial saves, and SP/MP GameLib bounds/angles serialization uses endian-aware float/vector writes.
- [x] Starting a new single-player game or loading a save after a game-module switch now resumes the requested map/load action after reload instead of falling back to the main menu, and reload logs continue across the module restart for support diagnostics.
- [x] Packaged single-player loading screens are more complete: Core 1, Core 2, Network 1, Network 2, Process 1, Process 2, Storage 1, and Tram 1 now include the missing DDS/TGA loadscreen image sets in `baseoq4/pak1`.
- [x] Packaged map loading screens now include the missing source loadscreen files found in the local `.home` runtime tree, including camera sidecars plus the MCC 2, Putra, Recomp, and Waste DDS/TGA image sets, so packaged builds no longer depend on local-only loadscreen captures.
- [x] Packaged single-player light-grid coverage is more complete: Air Defense 1, Core 2, and Waste now include the missing baked light-grid source assets in `baseoq4/pak1`, avoiding local `.home`-only coverage gaps.
- [x] OpenAL has been restored to the end-of-day June 23 sound implementation for default reliability, and the disabled revalidation slice has been removed from the shipped runtime.
- [x] Multiplayer client joins are more reliable after map load: valid compressed in-band net messages are no longer rejected as truncated when the run-length decoder reports its final bitstream cursor past the logical output size, so loopback clients can stay connected through MP validation while malformed short decodes still fail closed.
- [x] Release and staging automation avoids another class of symlink and malformed-output mistakes: generated changelogs refuse symlinked outputs and symlinked curated notes, Windows installers reject symlinked payload/template/output paths, stale staged-content cleanup unlinks known stale symlinks without following their targets, validation rejects symlinked build directories, generated headers/manifests replace destination symlinks safely, legacy PK4 staging uses the shared safe copy helper, and GameLibs staging rejects raw symlink stage roots before resolving them.
- [x] Build and release tooling now closes more raw-path gaps before packaging or validation work begins: shared copy helpers reject symlinked sources, icon sync refuses symlinked inputs and generated outputs, PK4 builders/checksum headers validate raw source/manifest/archive paths, package and fast-stage roots fail closed on symlinks, source-listing helpers reject symlinked roots, archive outputs report invalid directories clearly, and generated documentation link checks use isolated temp workspaces for concurrent runs.
- [x] Validation runs now fail earlier on unsafe inputs and malformed staged packages: the push/PR runner rejects invalid job counts, non-positive runtime timeouts, bad source roots, unsafe build directories, staged symlinks, nested linker artifacts, mismatched runtime architectures, wrong-platform game modules, and stale or missing Windows PDBs before a package can be trusted.
- [x] Release automation is harder to mis-publish: generated changelogs now honor their selected source checkout, validate release metadata before turning it into GitHub links, escape untrusted Markdown text, choose the correct prior tag, avoid nightly iteration collisions, reject release-version downgrades, guard packaged docs output paths, and verify Windows installers include the runtime DLLs they actually need.
- [x] Release packaging now rejects more malformed or unsafe inputs before archives are produced: generated docs refuse symlinked outputs, symlinked Markdown sources, symlinked copied assets, and unsafe URI schemes; changelog workflow URLs are checked for Markdown-breaking characters; package versions and package roots are validated before replacement; Windows runtime staging rejects symlinked binaries/content; staged game-library manifests require the expected schema; and CI smoke tests track the central validation list.
- [x] Local validation and release packaging guard more edge cases before publication: the push/PR Python suite now includes the platform, renderer, and settings tests that were previously CI-only; Windows runtime staging fails on malformed or mixed-architecture payloads and missing OpenAL runtimes; fast install staging rejects build/install directory overlap; installer script generation validates file-safe release tokens and escaped paths; macOS archives reject malformed path segments; and Discord release announcements include signed DMG plus unsigned macOS archive links.
- [x] The graphics settings menu no longer advertises renderer backends that openQ4 does not ship: the old Renderer Backend picker is now an advanced Renderer Fallback control with only Auto and explicit ARB2 rollback choices, while legacy config values continue to fall back safely through the engine warning path.
- [x] The System settings post-effects rows no longer overlap in compact display layouts: Renderer Fallback, irradiance, bloom, SSAO, HDR Tonemap, and CRT now keep separate rows so each setting can be read and changed from the menu.
- [x] Build and release packaging safety is tighter: PK4 source paths, accepted archive entries, fast `.install` staging roots, package version tags, copied package inputs, and staged GameLibs manifests now fail closed on path escapes, symlinks, duplicate metadata, or non-regular files, with regression coverage in the local validation suite.
- [x] Linux ARM64 save-game loading no longer crashes while restoring fresh saves: SP/MP game objects now keep 16-byte aligned allocation headers when savegame restore recreates entities, avoiding ARM64-only misaligned object pointers.
- [x] Linux ARM64 save-game restore is more reliable after the allocation fix: SP/MP script objects now read their saved payload sizes through the 32-bit save format instead of aliasing into pointer-width storage, preventing valid saves from hanging or failing during script-object restore on ARM64 hosts.
- [x] Save-game writes are now staged through temporary files and promoted with backups after header validation, while malformed save headers, persistent player info, minigame restores, and SP/MP game-object restore counts use domain-specific caps with SP/MP parity checks so corrupt files fail closed before overwriting good slots or driving runaway allocations. Arcade minigame integer payloads now use versioned little-endian save sections with legacy raw-format fallback, and SSD minigame restores validate level, weapon, pool, and entity-reference counts before indexing fixed arrays.
- [x] Save-game restore now fails closed more consistently in multiplayer game modules too: MP object creation, raw reads, strings, windings, dictionaries, and event payloads use the same bounds and short-read checks as the single-player restore path.
- [x] DXT5/RXGB normal-map DDS replacements now decode through the normal-map path when loaded as bump maps, so mod texture packs can replace `.tga`/`.jpg` bump sources with RXGB-style DDS files without losing the X/alpha normal channel.
- [x] Texture replacements can now drop a valid `.dds` beside an existing `.tga` or `.jpg` reference and have openQ4 prefer the DDS source automatically: supported DXT and BC7 mip chains upload directly when the renderer exposes the required compression format, with a decoded DXT fallback on older renderers.
- [x] Explicit BC7 DDS textures can now load directly as precompressed BPTC images on OpenGL 4.2/BPTC-capable renderers, including standard DX10 BC7 headers and common BC7 FourCC variants; unsupported drivers fail closed with a clear warning instead of mis-decoding the image.
- [x] BC7 texture handling is more robust and now matches dhewm3's replacement workflow end to end: `image_usePrecompressedTextures 2` follows the BC7-only policy, canonical `dds/` names are derived from complete image programs before extension-normalized and same-folder compatibility paths, header-only probes validate complete declared mip layouts without rereading full texture payloads, direct DXT/BC7 uploads honor downsize and unmipped policies, stale or malformed automatic replacements fall back to the original source, source-selection changes are detected by `reloadImages`, BC7 bump maps preserve authored alpha/RXGB data, `image_showPrecompressedTextures` reports exact selections, `imageDDSSelfTest` covers naming and malformed DX10 metadata, and `gfxInfo` reports DXT and BC7/BPTC availability.
- [x] Manual releases keep experimental macOS downloads visible when Apple signing credentials are absent: the workflow now always builds the experimental macOS ARM64 OpenGL and Metal packages, publishes signed/notarized DMGs when Developer ID/notary secrets are configured, and otherwise publishes clearly labeled `-unsigned.tar.gz` archives that are ad-hoc signed only for bundle validity and expected to show normal Gatekeeper warnings.
- [x] Manual experimental macOS ARM64 releases package successfully again: the app bundle executable and adjacent `openQ4-client_arm64` binary now keep separate valid macOS signatures, so OpenGL and Metal archives verify both launch paths without copying a bundle-scoped signature onto the standalone client.
- [x] Manual experimental macOS ARM64 release packages now keep SP/MP game module install names package-relative after Meson staging, so unsigned and signed OpenGL/Metal archives no longer point back at the CI workspace `.install` directory.
- [x] Manual releases no longer fail late when Apple signing credentials are absent: the workflow now builds a credential-aware release matrix, keeps Windows/Linux packages mandatory, includes signed/notarized experimental macOS DMGs only when all Apple Developer ID and notary secrets are configured, and verifies the expected artifact list against the matrix that actually ran.
- [x] Runtime PK4 packaging now tracks `content/baseoq4/pak0/` and `content/baseoq4/pak1/` through generated source manifests, so removing generated content such as obsolete Air Defense 1 light-grid chunks no longer leaves stale Ninja dependencies that break the fast local build.
- [x] Runtime logs now begin with the openQ4 product name and version before the log-open timestamp, making copied crash and support logs immediately identifiable.
- [x] Experimental macOS push verification builds are unblocked again: process handoff path and environment filtering now use openQ4's guarded string-compare API instead of raw C string calls rejected by the shared idStr safety wrappers.
- [x] Renderer helper and diagnostic loops do less repeated count work: portal export/find/show paths, MSAA resolves, light and subview winding projection, debug polygon drawing, material register printing, shadow-planner descriptor checks, and benchmark preset scans now cache stable loop counts before iterating.
- [x] Renderer geometry helper overhead is lower: GUI clipping, decal projection and decal winding emission, overlay creation, portal screen/fade checks, GUI/model tracing, and polytope surface construction now cache stable winding point and model surface counts while building or tracing geometry.
- [x] Renderer classic material passes do less repeated CPU work: soft particles, RV depth capture, depth fill, ambient shader passes, blend lights, lightgrid submission, shadow-map casters, point-shadow casters, custom GLSL interactions, and subview generation now cache material stage counts before scanning stages.
- [x] Renderer loading and legacy surface-scan overhead is lower: binary image uploads, lightgrid packed-image materialization, world-model export checks, decal projection, interaction creation, and GUI trace diagnostics now cache stable image/entity/surface counts before iterating.
- [x] Baked light-grid playback no longer streams atlas textures through the per-frame draw path: packed map grids are materialized during map setup and kept resident for the loaded level, removing the repeated pack reads, decode work, GL uploads, and purge/reload churn that caused traversal hitches.
- [x] Renderer CPU overhead is lower in frame preparation, diagnostics, and modern pass ownership: renderer loops now cache submit-command, graph-pass, packet-record, material-record, scene, and resource counts before iteration instead of repeatedly querying the same frame objects.
- [x] Renderer CPU overhead is lower in another command-submission pass: modern submit commands now cache material texture handles, texture-table indices, loaded semantic masks, and alpha-test metadata, while executor texture binding and material flag setup reuse those cached values and several legacy stage loops avoid repeated stage-count lookups.
- [x] Renderer CPU overhead is lower in material and clustered-light hot paths: material records cache semantic texture indices and loaded/color masks, modern draw and executor checks reuse those cached masks instead of scanning bindings, texture binding avoids redundant active-unit switches, and clustered light stage loops reuse cached stage counts.
- [x] Renderer CPU overhead is lower in another set of modern-path hot spots: render graph pass/resource lookups use cached masks and pointer fast paths, submit sorting avoids unnecessary range work, forward+ decal classification is cached per command, texture binds skip redundant active-unit changes, and material stage scans reuse cached stage counts.
- [x] Renderer CPU overhead is lower in modern-path preparation and submission: per-frame plan resets avoid clearing unused draw slots, modern draw planning reuses pass/shader decisions across draws, submit commands carry cached material and cull metadata, visibility bounds use cheaper clip transforms, and GL multi-bind calls now update only the changed binding range.
- [x] Core 2 AI navigation and combat recovery are more reliable on x64 builds: the AAS loader now matches retail Quake 4's tolerant AAS handling, initializes parsed area/reachability runtime fields deterministically, and lets AI resume normal combat after scripted idles while tactical route checks continue, preventing Makron arena enemies from standing idle instead of engaging.
- [x] World sound attenuation and volume now track retail Quake 4 more closely: master volume is applied in channel gain, sound fades use the retail linear scale, distance falloff honors `s_quadraticFalloff`, and radio/speaker world sounds receive the recovered Quake 4 attenuation pass.
- [x] OpenAL world-audio occlusion now sounds more physical: blocked portals and enviro-suit muffling are tracked separately, with stronger high-frequency loss on the direct path and a gentler filtered EFX send so walls, doors, and suit effects read as muffled instead of merely quieter.
- [x] Sound diagnostics and reload handling are closer to retail Quake 4: `listSounds`, `listSoundDecoders`, and `reloadSounds` are available again, `listSamples` remains as an OpenQ4 alias, and sound restarts now reload sample buffers after the OpenAL context is rebuilt.
- [x] Sound shader pitch behavior is closer to retail Quake 4: `s_frequencyShift` now gates frequency-shift playback, pitch shifts are clamped to the recovered retail range, live shifted playback time is accumulated and restored across pitch changes, delayed hardware starts advance by shifted playback time, non-looping shifted sounds use adjusted completion timing, amplitude/shake sampling follows the shifted playback position, and `s_skipStartSound` is available for retail-style sound start suppression diagnostics.
- [x] Voice and radio sounds now keep their mixer priority under the retail-style attenuation path, preventing dialogue and radio chatter from being stolen by lower-priority ambient channels when the sound world is busy.
- [x] Quake 4 sound parameter compatibility is restored between the engine and game modules: sound override parms now keep the retail `attenuatedVolume` layout field, Doppler and no-random-start flags use the retail bits again, and shader `frequencyshift` metadata no longer changes playback pitch by itself.
- [x] Skipping the Air Defense 1 intro cinematic no longer lets skipped intro voice or scripted audio resume after gameplay starts; cinematic skip now advances sound time with the fast-forwarded game frames so muted cinematic-only sounds age out during the skip.
- [x] Air Defense 1 cinematic effect sounds now clean up like retail: BSE reference sound emitters are freed once through the render-effect teardown path, skip-intro cleanup no longer double-frees/reuses the same emitter handle, late skip-shutdown string-pool cleanup no longer asserts or crashes, effect sound channels no longer collide with voice-only muting, and `listPlayingSounds` reports the actual non-zero-gain OpenAL sample files still active for debugging.
- [x] Native Wayland input validation now includes repeated mouse-capture stress: `sdl3MouseCaptureDiagnostics` accepts a bounded repeat count, CI runs a four-cycle relative mouse activate/deactivate case under Weston, and the log checks confirm capture state returns cleanly after repeated toggles.
- [x] Native Wayland window validation now has a stress case beside the basic lifecycle smoke: the Weston CI matrix repeatedly switches between windowed and fullscreen states with changing window sizes, keeping compositor-negotiated `vid_restart` behavior, pixel-size reporting, and fullscreen/windowed state logs covered separately from the shorter startup smoke.
- [x] Linux package metadata validation is harder to fool with malformed inputs: staged validation and release packaging share the same desktop-entry parser, reject duplicate `Exec=` keys, malformed shell quoting, NUL data, or field-code-only commands, and a deterministic fuzz smoke exercises those parser paths plus staged-source manifest hash/path invariants.
- [x] Linux packager guidance is clearer: `BUILDING.md` now names the SDL3 floor, Ubuntu 24.04-class userspace target, Wayland/X11/libdecor troubleshooting environment variables, `openQ4-game` staging manifest expectations, required validation commands, and the detached Linux debug-symbol archive that should stay paired with optimized release packages.
- [x] Linux sanitizer coverage now catches native Wayland runtime regressions as well as compile-time failures: PR validation builds the x64 engine plus staged SP/MP game modules with ASan+UBSan and precompiled headers disabled, stages the instrumented client separately from release dependency validation, runs the existing assetless SDL3 window/fullscreen lifecycle case under headless Weston with software rendering, and retains the runtime report alongside build logs and the openQ4-game staging manifest. Only UBSan's cross-DSO `vptr` check is excluded from engine/game translation units because their interfaces own RTTI on opposite sides of the runtime-loaded module boundary; ASan and every other UBSan check remain active, BSE retains `vptr` coverage, and the modules keep their fail-closed `-z defs` link contract.
- [x] In-tree BSE now compiles when precompiled headers are disabled: every BSE translation unit includes the canonical engine declarations explicitly, keeping sanitizer and Linux ARM64 cross builds from depending on incidental PCH state.
- [x] Disabling compiler PCH generation now keeps the legacy common-header contract intact by force-including `precompiled.h` as an ordinary header for engine and staged game translation units, so sanitizer and ARM64 cross builds test real code instead of failing on missing declarations.
- [x] SP and MP script-register strings are now copied through a literal format, so a value containing `%` is returned verbatim instead of being interpreted as a missing `printf` argument during editor/script introspection.
- [x] Legacy game-code edge cases are safe on modern Linux compilers: voice routing updates the selected cache slot without writing past the array, full AI vehicle-eject input stays within the signed command range, tactical-sensor cleanup reaches its derived destructor, and invalid animation channels return safely after the fatal diagnostic.
- [x] Engine diagnostic/output writers no longer consume missing or data-controlled format arguments: generated AAS area records emit exactly the fields the parser reads, and cvar flag listings print percent characters as data.
- [x] Linux ARM64 has an independent x64-hosted cross-build gate in addition to native CI: the checked-in Ubuntu multiarch toolchain builds all four runtime ELF artifacts in Wayland-only mode, rejects host dependency leakage, and verifies AArch64 machine headers plus game-module exports.
- [x] Linux dedicated-server linking is now genuinely headless on x64 and ARM64: a dedicated-only platform/stub source set removes direct OpenAL, OpenGL/GLX, SDL, PipeWire, Wayland, and X11/Xext runtime dependencies while retaining integrated BSE declaration support, and staged plus extracted-package ELF validation fail closed unless both the server and loaded MP module use only architecture-appropriate core runtimes.
- [x] Bundled SDL Linux clients now load PipeWire by its stable runtime SONAME instead of recording it as a direct ELF dependency, so systems without PipeWire can still start while openQ4's OpenAL game-audio path remains unchanged; staged validation rejects any return of the hard dependency.
- [x] Linux ARM64 release claims now match the available evidence: manual releases default to a visibly labeled preview tier, first-class publication fails unless the physical-hardware signoff record contains explicit accepted markers, and player/package docs call out the desktop-OpenGL requirement and remaining native-Wayland gameplay, dedicated-server, audio, input, and display gate.
- [x] Linux dedicated-server packages now get a real startup gate: dedicated builds skip client-only menu GUI preloading and no longer tear down an OpenGL vertex cache that was never initialized, then native x64 and ARM64 CI create a test-only minimal base pack, load staged `game_mp` without proprietary assets, wait through full common initialization and a live server frame, and require error-free game shutdown before passing.
- [x] The companion openQ4-GameLibs repository now supports standalone Linux x64 and ARM64 builds with GCC or Clang: native CI builds distinct `game-sp_<arch>.so` and `game-mp_<arch>.so` modules, verifies their ELF architecture, RELRO/NOW/non-executable-stack hardening, and required `GetGameAPI` export, and runs focused LP64/alignment/checksum probes. openQ4's staged companion-source build remains the integrated runtime, packaging, and gameplay-validation path.
- [x] Linux builds and Wayland validation are harder to regress: Meson now applies stack/FORTIFY plus RELRO/NOW/no-exec-stack hardening to Linux builds, staged Linux validation verifies PIE/RELRO/NOW/stack state with `readelf`, native Wayland PR CI runs the assetless renderer smoke under headless Weston with libdecor enabled and disabled, native Wayland VRAM detection avoids optional X11 probing, and `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` is available for compositor/libdecor troubleshooting.
- [x] Native Wayland and Linux ARM64 now meet in the same CI lanes: x64 and native ARM64 Weston jobs exercise assetless window lifecycle, repeated `vid_restart`, mouse capture, and display reporting; the ARM64 Wayland-only/libdecor-opt-out build uses Clang while the libdecor and dedicated-server builds retain GCC coverage; preferred libdecor is verified through dynamic-loader evidence, the opt-out is verified not to load it, and `-Dlinux_x11=disabled` rejects direct X11/GLX dependencies. Real ARM64 Wayland SP/MP gameplay, audio, and input remain a hardware signoff requirement.
- [x] SDL3 desktop changes now stay coherent after startup on Wayland and X11: monitor hotplug, dock/undock, orientation, mode, usable-bounds, and content-scale events refresh the selected desktop and framebuffer state even when SDL emits no window-specific event, while compositor hide/show transitions release latched input and only reacquire it after the render window is visibly focused.
- [x] The bundled SDL Linux feature contract now matches the runtime controls exposed by openQ4: Wayland, EGL, xkbcommon, and optional libdecor are configured with real versions and runtime SONAME loading; native IBus/Fcitx UI owns composition/candidates and openQ4 supplies its active text-input area, while commits outside the stock single-byte font range are rejected without corrupt narrowing; `wayland-scanner` and Python generators are build-machine tools for cross builds; and fake libdecor version defines are gone.
- [x] Linux ABI/package validation now checks the bytes, not just filenames: staged client, dedicated server, SP, and MP ELF files must match their x64/ARM64 machine label, carry the required PIE/RELRO/NOW/non-executable-stack hardening, and export `GetGameAPI` from both game modules. A real Ubuntu x64-to-AArch64 build completed all four artifacts and passed those architecture, hardening, export, dependency, and distinct-module checks.
- [x] Linux game-module linking now enforces the same closed ABI that packaging validates: both integrated and standalone companion builds use `-Wl,-z,defs` so unresolved definitions fail at link time, and a linker version script exports `GetGameAPI` while localizing every other symbol. Native Clang x64 and integrated GCC AArch64 builds link cleanly under that policy, with x64 `ldd -r` and the staged validator confirming no unresolved runtime definitions or leaked compiler helpers.
- [x] Fresh Linux x64 native-Wayland stock-asset gameplay now reaches well beyond menu startup: single-player entered `game/airdefense1`, produced an active-frame screenshot, and shut down cleanly; the `game_mp` loopback loaded stock `mp/q4dm1`, matched the server/client `0x3599935c` declaration checksum, connected and spawned both players, reached active draw on both processes, captured both views, and exited cleanly. The packaged dedicated server also loaded stock `mp/q4dm1` without initializing SDL video or OpenGL, advertised readiness only after the map finished spawning, accepted a separately packaged native-Wayland client with the same checksum, spawned its player, retained an active-gameplay capture, and shut down both processes cleanly.
- [x] Stock single-player Wayland evidence is now repeatable instead of one-off: the opt-in `linux_wayland_stock_sp_smoke.py` runner requires native host-matching ELF artifacts and a real Wayland socket, initializes software audio without a device-name assumption, enters `game/airdefense1`, auto-skips its cinematic, saves and reloads an isolated slot, proves a second active gameplay lifecycle, validates the save files and post-restore TGA, and records hashes plus ordered logs while keeping physical-hardware and human audible-playback attestations explicit.
- [x] Physical Linux ARM64 evidence is harder to mislabel: both stock-media harnesses record `systemd-detect-virt`, hypervisor, DMI/device-tree, kernel, CPU, and container signals; `--physical-hardware` rejects a run when any known VM/emulator signal is present; and the dedicated/client runner now requires a real Unix Wayland socket and carries the same explicit physical-host evidence fields as the SP report.
- [x] First-class Linux ARM64 acceptance now binds the exact stock-SP and dedicated/client JSON reports from the immutable evidence commit: schema 2 records both SHA-256s, strict bounded parsing rejects duplicate/unknown/missing or non-finite JSON, and release validation cross-checks generated report identity, exact marker and screenshot/save structures, physical native-ARM64 Wayland pass state, coherent virtualization probes with disclosed container use, canonical declaration checksums, ELF metadata, candidate client/server/SP/MP hashes, and matching retail `pak001.pk4` bytes. Human audible playback, input/hotplug/display checks, and two-client listen-server MP remain explicit review evidence rather than automated claims.
- [x] Cross-platform GameLibs staging now preserves the SP/MP split: `game-sp` compiles canonical companion `src/game`, `game-mp` compiles canonical `src/mpgame` with its own export map and precompiled-header route, savegame source stamps cover both trees, and staged-payload validation rejects byte-identical SP/MP modules.
- [x] Linux LP64 checksum and compiled-token semantics are now fixed-width and regression-tested in both engine and companion sources: MD5 and MD4 digest folding, CRC-32, and Honeyman checksum state use 32-bit words; compiled binary tokens retain 32-bit integer storage and explicit signed-byte encoding; and known-vector/native probes guard against x64/ARM64 `unsigned long` width changing pure-pack, declaration, or token results.
- [x] Native Wayland CI now exercises a real SDL3 window lifecycle under Weston, including fullscreen and windowed `vid_restart` transitions, compositor-accepted window state logging, and the optional `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1` path.
- [x] Native Wayland input validation is deeper: the SDL3 backend now has a command-driven mouse-capture diagnostic that toggles relative mouse mode, reports grab/capture state, and runs in the Weston CI matrix beside the window lifecycle smoke.
- [x] Native Wayland display reporting is now covered by runtime CI: the Weston matrix runs the SDL3 display diagnostics command and checks compositor-reported displays, content scale, orientation, mode metadata, and selected-display resolution.
- [x] Linux fallback display validation is broader: the X11/Xvfb ARM64 runtime smoke now runs SDL3 display diagnostics too, and driver-specific renderer cases are filtered out of default local matrix runs unless the matching SDL video driver is selected.
- [x] The project-specific XWayland escape hatch is now runtime-validated: CI launches the staged client with `OPENQ4_FORCE_X11=1` and checks that SDL selects the X11 driver while display diagnostics still report correctly.
- [x] Game-library source staging is now auditable: openQ4 treats openQ4-game as the canonical source-input repository, stages only regular source files into `.tmp/gamelibs_stage/`, writes a hash/git-state manifest, requires that manifest at Meson configure time, and validates the staging script in local and CI smoke tests.
- [x] Manual release source provenance is now reproducible across the full matrix: dispatches must originate from `main`, the requested `openq4_game_ref` is resolved once to a full immutable SHA in metadata, every job checks out the triggering openQ4 SHA plus that exact companion SHA, checked-out and staged manifest commits/clean state must agree, and release creation or reruns reject tags that do not target the triggering candidate commit.
- [x] Linux release artifacts are now revalidated after every packaging mutation: the stripped client, dedicated server, and distinct SP/MP modules must retain their architecture, hardening, closed ABI, resolvable dependency, and build-ID contracts; each `.gnu_debuglink` must match its detached debug file; and the exact runtime tarball is bounded-extracted into an isolated temporary tree and checked again before upload.
- [x] The multiplayer settings menu shows the selected player model preview again when the menu opens or the model choice changes.
- [x] Cinematic letterboxing now updates when the window or display size changes, and ultrawide/tall layouts add the needed pillarbox or letterbox bars so the visible cinematic area remains 16:9.
- [x] Single-player now offers a harsher Warlord difficulty above General, with beefier enemies and wider-range AI pressure, plus a Turbo Mode toggle that raises player run speed to a Quake 2-like pace, doubles vehicle top speed, doubles doors, lifts, conveyors, and other mover speeds, and removes weapon clip reload interruptions without changing weapon damage.
- [x] Starting a new single-player game from the menu now routes through a deferred session map-start command instead of executing a raw map load inside the GUI click handler, preventing the menu-window access violation seen after pressing Start Game while still entering gameplay directly.
- [x] The System settings page now has a Performance Preset dropdown and Auto-Detect button above the display controls, giving low-power systems such as Raspberry Pi a one-click profile that lowers resolution scale, AA, texture pressure, sound-shader sample admission, and surround/EAX audio cost without using debug visual skips. Auto-Detect now ignores missing or implausible memory telemetry, and Windows uses DXGI video-memory detection instead of feeding a hard-coded 100 TB VRAM value into the preset picker. The System-menu bridge now forwards preset command arguments through the same engine validation path used by the console, and settings-menu coverage keeps the GUI dropdown, localized choice labels, registry, user docs, and engine command list synchronized. The matching console commands are validated by `performancePresetSelfTest`, now recover from invalid stored preset cvars by returning to Balanced, reject explicit unknown preset names, extra preset arguments, and stray auto-detect arguments without mutating settings, require every preset write to be declared for backup/restore coverage, restore touched cvar values/flags and the global modified-state mask after validation, preflight target cvars before applying a profile, enforce coherent preset progression, keep optional shadow maps/post effects opt-in, call out `vid_restart` plus `s_restart` for full application, and still restore the higher audio targets when switching back to Balanced/Quality/Ultra instead of leaving low-power speaker/EAX/channel limits behind.
- [x] Startup installation failures now show a Quake 3-style system-console status strip with the fatal message pinned above the log, and asset/package errors clearly separate openQ4 runtime files in `baseoq4` from retail Quake 4 media PK4s in `q4base`.
- [x] Tournament spectator pre-game HUD text is centered on the main 640-wide HUD canvas again, while its black header bar now stretches across the full widescreen/ultrawide display.
- [x] Strogg flyer and fighter destruction now keeps the retail death effect visible after the aircraft is removed and restores the BSE debris handoff for the authored explosion chunks.
- [x] Mod discovery now fails clearly on bad manifests: `mod.json` parsing reports malformed manifests, tolerates unknown valid JSON fields, rejects trailing junk, and skips invalid mods with visible warnings instead of falling back to legacy directory probing.
- [x] Packaged openQ4 runtime content now has a stable pure-server identity: the build compiles openQ4 runtime PK4s before the engine, embeds their MD5 checksums for startup validation, always treats them as pure runtime packs, and release packaging reuses the staged packs while rejecting game-code/addon marker files that would change that role.
- [x] Level-related openQ4 content now ships as its own pure runtime pack: source content is split into `content/baseoq4/pak0/` for core runtime overrides and `content/baseoq4/pak1/` for light grids, map scripts, and level loadscreens; startup validates both `baseoq4/pak0.pk4` and `baseoq4/pak1.pk4`, and the default VS Code build now uses a fast compile + incremental stage path so untouched code and PK4s are skipped during everyday local/agent builds.
- [x] Experimental macOS SDL3 renderer shutdown and error paths are safer: the default SDL3 backend now checks for a live, current OpenGL window/context before screen changes, swap, teardown, or `glFinish`, rejects empty OpenGL extension lookups before calling SDL, uses overflow-safe selected-display viewport math, clamps malformed mouse movement, wheel, controller, and rumble values before integer conversion or event queuing, normalizes unusual app-entry argument state, guards POSIX clipboard/console allocation copies plus null fatal-error formats, falls back cleanly when Cocoa display enumeration is partial, and has validation coverage to keep those detached-context and null-pointer crash guards in place.
- [x] macOS fallback support is harder to crash during bring-up: the native Cocoa/OpenGL path now records a valid CGL context, validates pixel-format/context/window setup before use, releases a created context if the final make-current step fails, checks Cocoa screen and display-mode array bounds before `objectAtIndex:` calls, fails cleanly on fullscreen setup errors, unwinds partial display captures, treats missing or invalid-display VRAM telemetry as non-fatal, guards nil event helper entry points plus mouse capture, scroll-wheel overflow, and display/gamma-table lifetime paths, verifies the OpenGL context is current before swap/pause/resume/activation work, avoids stack allocation during extension lookup, returns clipboard text through the engine allocator, owns localized string fallbacks, displays fatal Cocoa alerts through a safe format wrapper, keeps obsolete Carbon/Xcode-era macOS sources out of the Meson build manifest, rejects raw C string builders from live macOS backend sources, and has validation coverage for the old undefined-return, transient-pointer, allocator-mismatch, allocation, overflow, stale-context, and null-pointer startup/shutdown hazards.
- [x] Experimental macOS POSIX support paths now reject more bad runtime state before it can crash: thread and trigger-event helpers guard invalid indexes in release builds, pthread setup avoids uninitialized attribute use, requires error-checking mutex attributes before enabling critical sections, and tracks mutex/condition initialization before later lock/wait/signal calls, thread creation validates null function/tracking arguments and negative tracking counts before registering handles, terminal and desktop-console input cursors are clamped before pointer arithmetic, console scroll-wheel deltas clamp and saturate before mutating scroll state, console command and input events check allocation and oversized input before copying, null debug/print format strings are ignored or normalized, signal-number reporting avoids signed-overflow edge cases, and process handoff survives an unexpected null environment pointer from the macOS runtime.
- [x] macOS local handoff paths are tighter: URL opening allows only bounded HTTP or HTTPS web links with a syntactically valid host and rejects local-file URLs, process handoff requires an absolute executable file path instead of searching `PATH`, strips dynamic-loader injection variables such as `DYLD_*`/`LD_PRELOAD` before launching the child process, and native fatal-error dialogs use the modern `NSAlert` API.
- [x] macOS static policy validation now guards the remaining platform-risk boundaries: validation rejects PATH-based process spawning, shell process helpers, broad AppKit URL opening, deprecated `NSRunAlertPanel`, Carbon leaking into the SDL3 path, unconditional Apple OpenAL linkage, and release workflows missing Developer ID/notarization/stapling while documenting the native fallback's legacy `NSOpenGL`/Carbon exceptions.
- [x] Experimental macOS hardware signoff now has a structured Apple-host workflow action: `Invoke-openQ4MacOSWorkflow.ps1 -Action Signoff -MacOSGraphicsBridge both` builds the OpenGL and Metal bridge variants in separate Meson build directories, runs the real-asset smoke profile and macOS renderer matrix for each, installs the Desktop launcher, records the selected OpenAL provider, captures host/display/audio/USB/Bluetooth inventory, writes bridge-specific `macos-runtime-signoff.md` reports with the remaining manual input, audio, display-mode, and in-game renderer checks, and copies the matching result directories back to `.tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz` for review on the host.
- [x] Collected experimental macOS hardware signoff archives now validate on the host immediately after copy-back: `tools/macos/validate_signoff_archive.py` rejects unsafe tar entries, requires both OpenGL and Metal bridge reports, checks workflow logs plus renderer smoke/matrix output, `-Action CollectResults -MacOSRunId <id>` can re-collect completed reports without rebuilding, and `-RequireCompletedSignoffChecklist` can fail final evidence if any manual hardware checklist item remains unchecked.
- [x] Experimental macOS release packaging now has a Developer ID/notarization lane for official artifacts: manual release jobs import the Developer ID Application certificate from secrets, sign with the Hardened Runtime, submit the package payload through `notarytool`, staple the app ticket, and verify the result with Developer ID authority, `spctl`, and `xcrun stapler validate` checks while keeping ad-hoc signing available for local package bring-up.
- [x] Experimental macOS release entitlement handling is now explicit and fail-fast: official releases use Hardened Runtime without custom entitlements by default, optional entitlement files must parse as plist dictionaries, and App Sandbox or `get-task-allow` entitlements are rejected until a reviewed sandbox/file-access design exists for user-selected assets, saves, logs, and staged overlays. Nested game dylibs are signed inside-out without app entitlements before the outer app receives its main-executable entitlements.
- [x] Credentialed experimental macOS release artifacts now use compressed DMG images for the OpenGL and Metal bridge packages: the package helper creates the DMG with `hdiutil`, official signed release jobs notarize and staple the final DMG after nested modules and the app bundle are signed, the workflow verifies the image before upload, and player docs explain the drag-installable self-contained app flow.
- [x] Experimental macOS OpenAL selection has an explicit build switch: release and validation workflows select `-Dmacos_openal_provider=system` with the pinned bundled OpenAL Soft dependency, while `apple_framework` remains available in a compile-only compatibility corridor.
- [x] The companion openQ4-GameLibs repository now has an experimental standalone macOS Meson path and CI coverage: Darwin/Clang builds use macOS defines, PIC game idlib linkage, `.dylib` game modules, and package-relative `@loader_path` install names, while the new macOS workflow builds the modules and verifies install names beside a Windows/MSVC `.dll` build leg.
- [x] The Apple Silicon/arm64-only experimental macOS support policy is now explicit in build, platform, release-package, and player setup docs: Intel Mac and universal2 packages are not advertised until a dedicated runner or universal release lane exists with architecture-specific validation.
- [x] Experimental macOS SDL3 release builds now avoid linking the legacy Carbon framework: the Meson Darwin framework list keeps Carbon only for the native Cocoa/OpenGL fallback, documentation describes the split, and the macOS bridge validation test guards against Carbon leaking back into the default SDL3 package path.
- [x] Experimental macOS debugging now has a compliant test path outside the normal CI matrix: agents are directed to an Apple-hardware/SSH workflow for persistent VM or hosted Mac testing, `E:\ISO\macos\` is reserved for macOS media inventory, and a manual **macOS Debug** GitHub Actions workflow can build OpenGL/Metal bridge variants and upload logs/artifacts while a dedicated Mac host is being provisioned.
- [x] Text asset loading and console/server string handling are more crash-resistant: ASE and Maya ASCII model import now reject oversized tokens, unsafe counts, and out-of-range mesh indices, while redirects, key binds, map/cinematic paths, checksum replies, and loading GUI paths avoid fixed-buffer overflows.
- [x] Diagnostics, clipboard, config, and UDP/SOCKS paths are harder to crash with oversized input: file float-string output, JPEG messages, Windows fatal errors and clipboard text, CVar value tables, GUI debug text, stub command lines, and UDP/SOCKS packet handling now use bounded writes or explicit size rejection.
- [x] More content/tooling string paths now handle unusual input safely: command arguments, parser and lexer diagnostics, exception text, ARB program loading, cube-image combining, download titles, and precache command writers avoid fixed-buffer writes or stack-sized file copies.
- [x] Additional UI and renderer utility paths now reject or contain malformed input: Base64 assignment reuses storage safely, rectangle/debug strings are bounded, download progress avoids zero-byte ETA division, view-note commands escape quoted user text, screenshot/envshot filenames are built dynamically, ambient-map output allocations are size-checked, and Hell Bust-Out board images must match the expected dimensions before copying.
- [x] Shared formatting and diagnostic paths are more resilient to oversized input: `idStr::snPrintf` and `va` no longer format through unbounded fixed buffers, timing reports avoid zero-frame division and fixed report buffers, model precache commands build dynamically, AF vector text clamps precision and long joint names safely, GLSL info logs are capped and heap-backed, and image/material diagnostics no longer size stack allocations from runtime counts.
- [x] Windows x64 game-module loading and builds are safer: SP/MP modules now build as `game-sp_x64` and `game-mp_x64`, allocator/event/save paths avoid pointer truncation, game DLL lookup is limited to staged local runtime directories instead of PK4/search-path extraction, and Windows symbol diagnostics keep full pointer-width addresses.
- [x] Additional x64 runtime paths now keep full pointer-width addresses: Raven heap page accounting no longer truncates allocation addresses, GameLibs report modern Linux/macOS CPU targets with pointer-width stack alignment, and legacy Linux OSS/ALSA writes keep their mix-buffer pointers intact.
- [x] More x64 migration hazards are removed: shared block allocators use explicit field offsets instead of int-truncated null-pointer math, queue helpers carry field offsets with pointer-width types, legacy renderdemo playback no longer writes archived 32-bit pointer values into live renderer structures, and GameLibs keep dynamically loaded tool-DLL handles pointer-width.
- [x] Experimental macOS release archives now get stricter integrity checks before publication: packaging rejects duplicate or unsafe archive paths, symlink entries, group/other-writable and special-mode files, stale mismatched game modules, broken root/app version manifests, and incomplete localized `InfoPlist.strings` resources.
- [x] Experimental macOS release packages now validate the app bundle more like a real Apple application: generated bundles include `PkgInfo`, display-name and game-category metadata, release validation rejects symlink payloads, verifies Mach-O architecture with `lipo`, applies and verifies ad-hoc code signatures, and checks package-relative game-module install names in both packaging and manual release CI.
- [x] Experimental macOS packages and staged builds are harder to ship in a broken or unsafe state: builds add stack/fortify/linker hardening, game modules use package-relative install names, dylib loads use canonical local-scope paths, release archives preserve executable bits for loose game modules, and validation rejects quarantine flags, debug bundles, stale non-runtime artifacts, unsafe archive metadata, and missing `.dylib` payloads before publication.
- [x] macOS host/guest testing is safer and more deterministic: SSH sync now uses per-run remote temp directories, source and asset archives reject unsafe paths, links, and special files, personal config/key files stay out of asset transfers, guest builds cannot wipe outside the repo, launcher paths are shell-quoted, wrong-architecture client launches require an explicit opt-in, and package PK4/archive creation rejects link or non-regular payloads.
- [x] Experimental macOS package and hardware-signoff evidence is stricter before release: staged payloads and unsigned archives now reject Finder/AppleDouble metadata such as `.DS_Store`, `._*`, and `__MACOSX`, signoff result archives reject duplicate or malformed path entries, host/guest tar extraction rejects hardlinks before unpacking, and custom macOS asset installs keep Quake 4 config, log, and key files excluded.
- [x] Experimental macOS hardware signoff now validates the staged runtime before smoke tests: the guest workflow fails early on missing or non-executable client/dedicated/game-module payloads, stale Windows/Linux modules, Finder/debug metadata, or architecture mismatches, records `lipo -archs` evidence in the report, rejects oversized signoff text members on the host, and verifies copied Quake 4 assets contain q4base PK4s before running gameplay validation.
- [x] macOS signoff collection is stricter and less prone to stale evidence: manually supplied asset roots are checked for q4base PK4s before smoke runs, staged installs reject additional Apple metadata folders such as `.Spotlight-V100` and `.Trashes`, collected result archives include only the expected `signoff` bridge directories, and the host validator rejects unexpected top-level result directories.
- [x] macOS guest signoff runs fail earlier on malformed workflow inputs: action, graphics bridge, OpenAL provider, build type, and platform backend tokens are validated before result directories or Meson setup arguments are created, keeping bad environment values from producing misleading signoff folders or logs.
- [x] macOS host workflow cleanup is more reliable: the SSH runner now removes its per-run `/tmp/openq4-macos-<guid>` transfer directory from the Apple host in a `finally` block, while refusing to remove any path that does not match the expected temp-root pattern.
- [x] macOS host/guest workflow paths and limits are more robust: documented `~/openq4-work` defaults now expand to the Apple user's home directory inside remote transfer, asset, build, and result-collection scripts; host parameters reject invalid ports, build types, empty build directories, and non-positive smoke limits; guest Meson setup refuses source/runtime/tool directories as build outputs; and signoff archives reject oversized non-text payloads or malformed action tokens.
- [x] macOS runtime handoff paths are more defensive: `Sys_OpenURL` rejects overlong, control-character, non-HTTP(S), local-file, and malformed-host URLs before handoff, and `Sys_StartProcess` fails clearly if an executable bit cannot be applied before `execve`/`posix_spawn`; the macOS bootstrap also checks for `xcrun`, `plutil`, `lipo`, `otool`, and `codesign` before build/signoff work starts.
- [x] Experimental macOS package metadata validation is stricter before artifacts are published: app bundle validation rejects empty icon/version/plist payloads and non-dictionary `Info.plist` roots, bundle creation fails immediately when no staged icon is available, localized `InfoPlist.strings` files are parsed for malformed or duplicate keys instead of substring-matched, and archive metadata members are capped so malformed packages cannot hide oversized app metadata.
- [x] Sound-reactive world lights now match stock Quake 4 behavior more closely: hum-driven map lights such as Air Defense 1's corridor lighting use the authored sound-shader flicker envelope instead of dropping to black.
- [x] Single-player enemy-hit feedback now matches retail Quake 4 again: the stock cursor GUI's per-component color aliases resolve correctly, so damaging enemies flashes the crosshair red without requiring custom HUD content or a new setting.
- [x] Display resolution control is now display-aware in the Settings menu: the System page lists Desktop Native, only SDL3-reported fullscreen preset resolutions for the selected monitor, then Custom; exact detected modes remain selected instead of relabeling as Custom, refresh choices follow the selected resolution, the legacy `r_mode` catalog now covers common display resolutions from older 4:3 panels through ultrawide/HiDPI/8K modes while preserving existing mode IDs, and high-refresh requests above 200 Hz are accepted.
- [x] Language selection in the settings menu now applies immediately and keeps non-English menus readable: the engine handles the menu's language reload commands, refreshes the current language index, parses translated choice labels without punctuation warnings, and falls back through English strings when a translated pack is missing an openQ4 entry.
- [x] Settings menu brightness and gamma changes now visibly apply on SDL3 and legacy non-native gamma backends through a final framebuffer color-mapping pass, while native Linux and experimental macOS gamma-ramp backends keep using their OS display ramp path.
- [x] Startup and module-switch loading screens keep their Quake 4 background artwork visible again by emitting the dark fill and splash art as separate fullscreen GUI batches, so stock material sorting can no longer cover the image.
- [x] Switching between single-player and multiplayer now keeps the game window open and shows the existing initialization-style loading screen while the matching game module is reloaded, avoiding the old console flash and window recreation during SP/MP map transitions.
- [x] Headless renderer jobs can now run with `r_hiddenWindow 1`, keeping the GPU-backed OpenGL context needed for light-grid baking while hiding the game window; the all-MP light-grid bake task now opts into that mode, starts directly in the multiplayer game module, and enables the cheat-gated bake path.
- [x] Baked light-grid coverage now includes the remaining stock multiplayer map families outside `mp/q4dm*`, so CTF, DeadZone, Tourney, q4cmp, and expansion arenas ship with generated irradiance-volume data under `baseoq4`.
- [x] Baked light grids now produce a map-local `.lightgridpack` runtime artifact that keeps probe metadata and per-area binary image chunks together, reducing loose-file lookup overhead while preserving current-area/neighbor residency and keeping `.lightgrid` plus TGA outputs as fallback/debug files.
- [x] Packed light-grid residency now keeps every current-frame receiver-referenced grid resident before aging unused loose fallback areas, and materialization timing only prints through `r_lightGridReport`; this stops Air Defense 1's repeated area materialization console spam without forcing packed grids back through runtime streaming.
- [x] Baked light grids now visibly affect world geometry: the indirect pass runs after opaque ambient/material drawing, submits receiver geometry through the stable baked-irradiance path instead of depending on fragile material albedo stages, and exposes gated receiver diagnostics through `r_lightGridReport`.
- [x] Renderer diagnostics now include `r_skipEntities`, which keeps BSP world area models while suppressing non-world render entities for clean world-only light-grid A/B captures.
- [x] Baked light-grid composition now hands modern visible color/depth back before legacy overlay passes, uses a tolerant overlay depth test against copied modern depth, and decodes AG-packed bump maps like the main material path so indirect irradiance samples the expected world normals.
- [x] Baked light-grid contribution is now color-managed before scene blending: 8-bit probe atlases are gamma-decoded, receiver color comes from representative material albedo instead of a white overlay, the default pass uses exact depth with no additive polygon-offset overdraw, and `r_lightGridIntensity`, `r_lightGridVisibilityFloor`, `r_lightGridIrradianceGamma`, and `r_lightGridMaxContribution` keep the result tunable for SDR, HDR, and bloom.
- [x] Machinegun scope lighting now stays consistent when irradiance volumes are enabled: first-person weapon scope/glass surfaces with authored color-mask stages are left to their normal material and GUI light instead of being washed out by the additive light-grid pass.
- [x] Optimized Linux builds keep baked light-grid receivers stable: world surfaces now use only their own resolved portal-area light grid instead of borrowing the camera area's grid, and GCC/Clang builds avoid strict-aliasing assumptions in the legacy renderer/game code.
- [x] The machinegun zoom scope now restores the retail blur effect while keeping widescreen and tallscreen projection correct: the scope samples the captured scene by active viewport coordinates instead of stretching the 640x480 GUI canvas, and aspect-expanded gutters use matching blur masks.
- [x] The nailgun zoom scope now fills widescreen and tallscreen gutters with matching expanded scope artwork, preserving the stock lock-on reticle while avoiding exposed safe-area edges.
- [x] GitHub releases now broadcast curated openQ4 announcements to Discord from both the manual release workflow and manually published GitHub releases, using release-note highlights plus platform download links while keeping the webhook URL in a repository secret.
- [x] Steam Deck and SteamOS SDL support is closer to Windows SDL parity: SDL lifecycle events are watched as soon as SDL receives them so suspend/resume, foreground/background, and low-memory notifications cannot be missed between normal event-pump frames, direct-launch Deck/SteamOS profile detection accepts more host signal aliases, and the launcher regression test now uses the real case-sensitive Linux template path.
- [x] SDL3 input support is aligned across Windows, Linux, and macOS: keyboard/text input, relative mouse capture, high-resolution wheel handling, gamepad/joystick hotplug, stable controller binds, rumble, battery diagnostics, gyro, touchpad, touchscreen routing, and `listControllers` diagnostics now share the same backend contract on SDL3 platforms, with macOS still tracked as experimental support.
- [x] Single-player loading screens now consume the button, key, or controller input used to continue before gameplay resumes, so pressing fire or another bound action to leave the prompt no longer triggers that action in the first in-game frame.
- [x] Linux and macOS memory handling now matches the Windows contract more closely: protected allocator blocks use real page-lock and unlock calls instead of no-op success stubs, and reported system RAM totals use the same rounded, overflow-safe megabyte reporting across Linux and experimental macOS builds.
- [x] macOS renderer startup now validates the exact callable OpenGL entry points used after SDL3 creates Apple's fallback compatibility context, including multitexture calls, VBO subdata uploads, optional PBO readbacks, ARB program attribute/local-parameter hooks, and GLSL uniform/program hooks; this addresses the earlier crashes reported in GitHub issue #73 comments 4730727130, 4749280134, and 4749378065 by downgrading optional upload/readback paths or failing cleanly when required ARB2 hooks are missing.
- [x] Apple OpenGL 2.1 compatibility launches now disable the legacy VBO vertex cache and use the CPU-backed vertex cache instead, avoiding the renderer-upload/ARB2 crash still reported on Apple Silicon in GitHub issue #73 after the initial entry-point guards.
- [x] Optional renderer buffer-object cleanup and PBO readbacks now follow the same macOS-safe capability gates: disabled upload bridges report zero live buffers, stale buffer names are dropped when delete entry points are unavailable, and HDR/light-grid async readbacks fall back instead of bypassing the validated PBO path.
- [x] macOS ARB2 interaction draws now pass VBO byte offsets through explicit `idDrawVert` layout constants instead of forming member addresses from the vertex-cache offset token, avoiding the Apple Silicon compatibility-context crash that could still occur after the entry-point guards in GitHub issue #73.
- [x] Apple OpenGL 2.1 compatibility now skips the full ARB interaction shader upload and automatically uses the simpler interaction shader pair, avoiding the Metal bridge startup crash reported immediately after interaction color-mode detection while preserving normal ARB2 interaction rendering on other drivers.
- [x] Apple OpenGL 2.1 compatibility now bypasses ARB2 light interactions after the July 3 issue #73 follow-up report (comment 4874832470) showed the M4 Max path still crashed after renderer startup completed; affected macOS launches log `ARB2 interaction driver bypass` and use the existing ambient rescue floor as a degraded-lighting fallback instead of entering the fragile first-frame interaction draw.
- [x] Experimental macOS package staging now pins game-module outputs to the `.dylib` names that the app bundle, release archives, and runtime validators require, so OpenGL and Metal bridge packages cannot silently drift to non-macOS module suffixes.
- [x] Platform macro guards were hardened so legacy `MACOS_X`, `__MACH__`, and `__MWERKS__` checks use defined-style preprocessor tests, shared feature switches provide guarded 0/1 defaults, and push validation now blocks those brittle guard forms from returning.
- [x] Linux x64 and ARM64 CI builds are unblocked again: SDL3 DRM VRAM detection now uses openQ4's portable string comparison path, keeping sysfs VRAM probing intact without tripping the guarded C string wrappers on GCC/Clang builds.
- [x] Multi-display and window behavior is now aligned across Windows, Linux, and experimental macOS SDL3 builds: selected-monitor launches, desktop/exclusive fullscreen, borderless windows, selected-display spanned UI, windowed placement, high-DPI drawable refresh, and display-change recovery use the shared SDL3 path, while macOS now honors the selected display for compatibility calls and falls back through current mode/display bounds when desktop mode is unavailable.
- [x] Strogg health stations are usable again after the player becomes Strogg in the single-player campaign: animated in-world GUI models now participate in renderer GUI tracing, so the station panel can receive the stock `heal begin` / `heal end` commands.
- [x] Strogg health-station panels now mask their animated cell strip like retail Quake 4: authored GUI background materials keep the GUI emission order instead of sorting over the station faceplate.
- [x] Experimental macOS package launches are more robust from Finder and Terminal: SDL3 startup now enters the engine through `SDL_RunApp()` so platform-specific launch setup runs before early engine initialization, macOS base-path discovery resolves the actual executable path and validates `baseoq4/` before selecting a default, and the generated `.app` embeds the packaged client as its native app executable.
- [x] macOS app base-path discovery no longer depends only on Cocoa bundle metadata: when the executable path is inside `*.app/Contents/MacOS`, the engine derives the bundle's embedded resource/module roots directly, while complete older packages can still use their extracted adjacent root.
- [x] macOS first-launch save/config handling is more robust: the default `~/Library/Application Support/openQ4` save root is normalized, created recursively with private permissions when missing, verified as writable/searchable before use, and falls back through the current working directory with clear diagnostics instead of silently selecting an unusable support-log path.
- [x] Native macOS OpenGL context deactivation is safer during renderer skip/restart diagnostics: deactivation now tolerates missing contexts and only clears openQ4's own Cocoa context, avoiding accidental state disruption when Cocoa has no active game context.
- [x] macOS URL and process handoff diagnostics are safer for support logs: rejected URLs are no longer echoed before the shared HTTP(S)-only policy, UTF-8 parsing, and Foundation validation pass, failed AppKit URL handoffs use a generic diagnostic, and malformed process command lines are rejected without copying the rejected command into logs.
- [x] Experimental macOS support is covered earlier in CI: pull-request and push validation now build/install both macOS ARM64 SDL3 OpenGL and Metal bridge variants, staged macOS payloads verify their icon and splash assets, and release packaging validates the generated `.app` executable and `Info.plist` before archiving.
- [x] Experimental macOS packages now advertise the same macOS 11.0 minimum version that Meson builds target, avoiding an accidental macOS 12 metadata floor for users on supported Big Sur systems.
- [x] macOS app launch diagnostics are clearer: the generated `.app` now embeds the packaged client binary directly, release CI checks that embedded executable against the adjacent package client plus key plist values, and executable-path discovery handles long `_NSGetExecutablePath` buffers without silently deriving paths from truncated strings.
- [x] Experimental macOS release packages now reject unbundled non-system runtime dependencies: app, client, dedicated-server, and game-module binaries are checked with `otool -L` during release packaging so accidental Homebrew/local/`@rpath` links fail before users hit a launch-time loader crash.
- [x] Final experimental macOS package validation now checks the packaged runtime source for required app bundle entries, executable bits, localized plist resources, safe package-relative paths, and matching `Info.plist` identity/version values before the release container is published.
- [x] Experimental macOS build and validation entrypoints no longer depend on GNU-style `dirname --`, so the Bash wrappers used by macOS CI and release packaging bootstrap cleanly on the stock BSD userland.
- [x] Legacy macOS app metadata is aligned with the generated release bundle: the in-tree `Info.plist` now uses `openQ4.icns`, declares `CFBundleName`, carries a short version, and is parsed by the macOS regression test against the same identity contract as packaged apps.
- [x] macOS staged-payload validation is now architecture-aware: release and local validators require matching no-extension client/dedicated binaries, matching `game-sp`/`game-mp` `.dylib` modules, and reject stale `.dll`/`.so` game modules before packaging.
- [x] Experimental macOS release instructions now call out the supported `openQ4.app` launch path and how to clear Gatekeeper quarantine for unsigned archive builds, making OS-level launch blocking easier to distinguish from engine startup failures.
- [x] Renderer performance promotion evidence is harder to overclaim: a new `renderer_performance_summary.py` tool compares candidate benchmark runs against explicit ARB2 across the agreed gameplay scenes, records P50/P95/P99/max frame time and pacing evidence, and keeps `perf=arb2-or-better` failed until every scene clears the review threshold.
- [x] Renderer candidate selection is harder to overclaim: a new `renderer_candidate_summary.py` tool records the measured modern path, candidate alternatives, acceptance thresholds, required promotion gates, and the current disqualification reason before any renderer path can be treated as a default-promotion candidate.
- [x] Renderer portability evidence is harder to overclaim: a new `renderer_platform_summary.py` tool records Windows x64, Linux x64, and macOS GL 4.1 report paths plus reviewed hardware, driver, GL, tier, timer, upload, DSA, multibind, and GPU-driven metadata before a platform can be treated as complete.
- [x] Approved visual-reference evidence is easier to review without adding screenshot binaries to the repo: a new `renderer_visual_reference_summary.py` tool defines the long-term visual matrix, writes a lightweight manifest template, checks reference-backed gameplay reports and RMS/max thresholds, and records the required human-review checklist before `visual=pass` can be considered.
- [x] Modern shader-library maintenance is easier to audit without changing packages: a new `renderer_shader_inventory.py` tool checks the internal shader-source inventory, GLSL tier coverage, program-slot capacity, draw-record safety guard, validation cases, and reload gate, while keeping runtime shader sources compiled into the engine instead of adding loose shader files.
- [x] RenderDoc/API renderer evidence is easier to collect consistently: a new `renderer_capture_summary.py` tool defines the long-term capture matrix, expected `.rdc` and API-trace file names, required platform/GL metadata, cvar observations, and review checks before any RenderDoc, bind-count, or graph-invalidation claim can be treated as complete.
- [x] Renderer promotion evidence is easier to audit without changing defaults: a new `renderer_promotion_evidence.py` tool gathers safe validation, SP/MP gameplay, visual, capture, performance, presentation, rollback, default-safety, and platform reports into one blocked-or-ready summary before any `r_rendererModernAutoPromote` sign-off can be considered.
- [x] Dedicated server renderer startup is safer to validate: the safe matrix now has a dedicated-server case that proves the disabled BSE runtime manager path still uses the real effect-decl allocator, loads the MP game module, and shuts down renderer scaffolding without the previous vertex-cache NULL-free error.
- [x] Low-overhead modern-submit validation is easier to reproduce: a new `low-overhead-submit` gameplay profile runs GL 4.5 diagnostic modern submit under real maps, reports submitted masked draws, DSA and multibind activity, restore handoff counters, and zero submit fallback or missing-buffer counters, while keeping renderer defaults unchanged until API captures prove a broader benefit.
- [x] Static renderer residency checks are easier to control: a default-off `r_vertexBufferBudget` path now lets validation runs enforce an LRU budget for static vertex/index buffers, reports fixed, protected, purgable, and over-budget bytes in `gfxInfo`, and keeps normal renderer defaults unchanged.
- [x] Render-graph invalidation diagnostics are clearer during gameplay: reports now distinguish submitted discard calls from armed candidates whose owning modern pass did not submit, and a new `graph-invalidation-modern` profile validates the modern side-path cvars, GPU-timer capture, and default-vs-armed accounting without changing renderer defaults.
- [x] Renderer shutdown and restart leak checks are easier to audit: a default-off `r_rendererShutdownAudit` path now reports renderer-owned GL object counts around full `vid_restart` and final shutdown, and the safe validation matrix verifies repeated restarts return those counts to zero before release sign-off.
- [x] SP/MP mode-switch renderer checks are easier to audit: the gameplay benchmark harness now has a `mode-transition-resource-count` profile that runs single-player, multiplayer, and single-player again in one process, preserves reload logs across module switches, and reports stable renderer graph, FBO, upload-buffer, fence, and shader counts while keeping static residency visible as tracked context.
- [x] Repeated map-transition renderer checks are easier to audit: the gameplay benchmark harness now has a `map-transition-resource-count` profile that alternates storage and storage2 maps in one SP process, compares post-warm repeated-map renderer object counts, and reports static upload residency separately so content-cache warming stays visible without hiding real graph, FBO, fence, or shader leaks.
- [x] Renderer restart/residency validation is easier to audit: the safe validation matrix now compares repeated post-restart render-graph, upload/static-buffer/fence, and shader-program counts, surfacing the stable values directly in Markdown and JSON reports.
- [x] Low-overhead state-cache fallback coverage is easier to audit: the safe validation matrix now has a focused no-multibind fallback case that proves mixed 2D/cube-map texture groups use the target-aware fallback path and report fallback batches without changing renderer defaults.
- [x] Long upload-pressure checks are easier to run and audit: the benchmark harness now has an `upload-pressure-long` profile with 30-second samples and reports whether fence waits, fence timeouts, fence fallbacks, ring high-water, overflow, and frame-stall counters were captured for every role before any upload-default decision is considered.
- [x] Render-graph invalidation is safer to test: the default-off path now has pass-owned GL discard submission, self-test coverage, and diagnostics for submitted calls plus disabled, unsupported-tier, missing-owner, and missing-FBO skips, while renderer defaults stay unchanged until gameplay and capture evidence prove a benefit.
- [x] Renderer gameplay sign-off is current for the first mid-term Windows x64 tranche: the required storage, air-defense, storage2, medlabs, MCC Landing, and q4dm1 loopback SP/MP coverage now passes after the renderer profiling and reporting updates.
- [x] Renderer visual screenshot evidence is current for the first mid-term Windows x64 tranche: post-processing, BSE-heavy, GUI/subview, dense-light, and MP listen/client scenes now have preserved gameplay captures with no black-frame, post-process, GUI/subview, or obvious shadow/stencil fallback failures found in the inspected bundle; approved reference-image comparisons remain a separate promotion gate.
- [x] Renderer presentation evidence is current for the first mid-term Windows x64 tranche: storage, air-defense, storage2, medlabs, and q4dm1 loopback now have capped, uncapped, and VSync gameplay pacing captures, with no renderer failure signatures found and no default pacing policy change made from the sampled data.
- [x] Renderer performance comparison evidence is current for the first mid-term Windows x64 tranche: storage, air-defense, storage2, and medlabs now have matched default, explicit ARB2, and modern-executor-prep captures, and the mixed results keep renderer defaults unchanged until a candidate is consistently ARB2-or-better.
- [x] Renderer GPU-timer diagnostics are current for the first mid-term Windows x64 tranche: the same performance scenes now have opt-in GL timer-query captures for default, explicit ARB2, and modern-executor-prep variants, improving bottleneck evidence while keeping timer-query runs out of default pacing policy decisions.
- [x] Renderer low-overhead evidence is current for the first mid-term Windows x64 tranche: storage, air-defense, and medlabs gameplay captures now compare GL 3.3 fallback behavior against GL 4.5 low-overhead readiness, confirming the DSA graph-resource path while keeping further state-filtering changes out of defaults until draw-submission and API-capture evidence are stronger.
- [x] Render-graph invalidation evidence is current for the first mid-term Windows x64 tranche: post-processing and BSE-heavy scenes now have default-vs-armed GPU-timer captures with clean logs, while transient invalidation remains default-off until pass-owned submission counters and stronger bandwidth evidence exist.
- [x] Renderer benchmark runs are more reliable on Windows: long descriptive gameplay case names now use compact stable filesystem IDs for save, log, stdout, and stderr paths, preventing stress profiles from failing before the game launches because the log path is too long.
- [x] Renderer upload-pressure evidence is current for the first mid-term Windows x64 tranche: storage, air-defense, and medlabs scenes now have baseline and stress-profile captures for default, persistent, reduced-ring, minimum-frame-buffer, and map-range/fallback upload paths, with no default upload-policy change required from the sampled data.
- [x] Renderer safe-start validation is current for the mid-term renderer work: the broad default validation matrix now passes all 36 startup, self-test, tier, promotion-readiness, shader-tier, and presentation-safe cases after the latest renderer diagnostics and reporting updates.
- [x] Renderer platform validation is easier to audit: validation reports now summarize the actual startup platform, GL context, selected tier, debug-context state, GPU timer availability, upload path, and low-overhead DSA/multibind readiness for completed safe cases, while keeping Linux and experimental macOS promotion requirements explicit until those runs exist.
- [x] Modern renderer promotion readiness is easier to audit: the validation matrix now includes assetless startup probes that prove auto-promotion stays blocked with missing or incomplete evidence, explicit `r_renderer arb2` remains a rollback escape even with complete evidence, and default startups keep debug, validation, bindless, shader-reload, and modern-visible side paths off.
- [x] Modern renderer rollback evidence is current for the first mid-term Windows x64 tranche: a storage gameplay proof now exercises opt-in modern-visible side paths, verifies they fail closed, then returns to explicit ARB2/legacy settings with experimental renderer paths disabled.
- [x] Renderer presentation checks are easier to reproduce: the gameplay benchmark harness now has a `presentation-comparison` profile for capped, uncapped, and VSync pacing across the first mid-term SP/MP scenes, and reports now include avg/P50/P95/P99/max frame pacing in one table.
- [x] Renderer benchmark reports are easier to reproduce and debug: generated reports now record the executable, asset base path, staged dev path, game directory, save paths, cvar overrides, autoexec files, elapsed/timeout status, and exact launch commands for each benchmark role, with failed and planned runs summarized without digging through logs first.
- [x] Mid-term renderer visual checks are easier to reproduce: the gameplay benchmark harness now has a `visual-comparison` screenshot profile for post-processing, BSE-heavy, GUI/subview, dense local-light, and MP listen/client scenes, and reports now show the active reference bundle and image-diff thresholds used for optional TGA comparisons.
- [x] Renderer performance comparisons are easier to reproduce: the gameplay benchmark harness now has a `performance-comparison` profile for default, explicit ARB2, and modern-executor-prep runs across the first mid-term target scenes, and reports now surface renderer CPU phase timing, GPU timing text, draw counts, pacing, uploads, and state-cache counters in one table.
- [x] Render-graph invalidation evidence is easier to collect: the gameplay benchmark harness now has an opt-in `graph-invalidation` A/B profile that compares default and `r_rendererGraphInvalidate`-armed runs across post-processing and BSE-heavy scenes, with report tables for tagged, candidate, armed, submitted, and skipped invalidation counts.
- [x] Render-graph transient attachment optimization is easier to validate safely: resource diagnostics now classify invalidate tags into last-use candidates and skip reasons, `rendererMetrics` and benchmark reports expose those counts, and the default-off `r_rendererGraphInvalidate` path is armed for future pass-timed discard submission without changing the visible renderer.
- [x] Low-overhead renderer profiling is easier to compare: the gameplay benchmark harness now has an opt-in GL 3.3 versus GL 4.5 state/bind profile with detailed metrics for DSA updates, multibind batches, classic texture binds, graph allocation paths, upload fences, and state-cache counters.
- [x] Renderer optimization profiling is easier to reproduce: the gameplay benchmark harness now includes an opt-in upload-pressure matrix for storage, air-defense, and medlabs scenes, with reports that surface upload KB, ring high-water/overflow, stalls, fence waits/timeouts/fallbacks, upload path, and state-cache counters without manual log parsing.
- [x] Linux AI wake/sleep behavior is more reliable: SP/MP PVS visibility merging now uses fixed 32-bit words on every platform and the connected-area flood no longer reads past its queue, preventing monsters from incorrectly going dormant when they should remain connected to the player.
- [x] Multiplayer light-grid coverage now includes three-bounce bakes for every shipped `q4dm*` map, covering Q4DM1 through Q4DM11 plus Q4DM11V1, and single-player coverage now includes three-bounce Air Defense 1 (`game/airdefense1`) and Air Defense 2 (`game/airdefense2`) bakes; the light-grid receiver now replays terrain material blend layers instead of lighting only one diffuse stage, and the baker completes light-grid metadata files reliably after atlas generation.
- [x] Generated light-grid outputs for `game/airdefense1` are no longer shipped, removing its map-specific metadata and per-area atlas payload from the runtime content package while leaving the engine’s light-grid support available for other maps.
- [x] Save-game loading is more reliable: restored trace-model caches now accept stock-compatible saved collision data without an over-strict plane check, and failed or interrupted restores clean up safely instead of crashing during map shutdown.
- [x] macOS manual releases now publish both OpenGL and Metal ARM64 packages reliably: the release workflow passes dash-prefixed package suffixes in an argument-safe form, so `-opengl` and `-metal` variants no longer fail before packaging starts.
- [x] Enemy bullet impacts now keep their resolved flesh, armor, or monster-metal hit material through replicated damage effects, so sounds, wounds, and splats follow the same hit-material result as retail Quake 4 instead of falling back to a model's base material.
- [x] Blood marks and splats now project onto visible animated models again: the renderer uses the retail dynamic-model overlay path for wounds and handles Quake 4's packed character meshes when baking overlay surfaces, so enemy flesh hits and weapon blood splats are no longer lost on collision-only geometry.
- [x] The `levelshot` capture command now writes tiles using the active mapDef `loadimage` name, so shared shots like `game/airdefense2` save as `airdefense` and split-map visits such as `first`/`second` save under their matching loadscreen names.
- [x] The `gotolevelshot` debug command now reads that matching levelshot `.txt` pose, supports explicit `first`/`second` split-map lookups, enables noclip only after a valid pose file is found, and moves the player back to the captured view.
- [x] Experimental macOS builds now retain the OpenGL package while adding a separate Metal bridge package without a native renderer rewrite: releases build both `-Dmacos_graphics_bridge=opengl` and `-Dmacos_graphics_bridge=metal` SDL3 variants, the Metal build links the Metal/QuartzCore bridge frameworks, startup logs the active graphics bridge, and the visible renderer stays on openQ4's stock-compatible OpenGL path.
- [x] The macOS Metal bridge is more robust during startup and diagnostics: the bridge define is applied consistently across macOS source languages, failed SDL Metal hint setup is logged, SDL splash/system-console support windows now require valid SDL window IDs before renderer creation, try the Metal/default renderer first while falling back to software, and log splash event preservation failures so fatal-error and startup UI remain diagnosable if the Metal renderer or SDL event queue cannot initialize cleanly.
- [x] Shadow-map contact now stays tighter on level-start lighting and grazing surfaces: receiver slope bias is bounded, projected and point defaults use lower bias, caster polygon offset is less aggressive, and the modern clustered path follows the same receiver-plane-bias policy as ARB2.
- [x] Level loads now start real map work sooner and are easier to diagnose: the loading screen no longer waits a full extra second before blocking initialization, repeated missing collision-cache probes and duplicate map media precache requests are skipped, cold-cache image generation avoids redundant source timestamp probes when no generated file exists yet, runtime image loads write generated binary-image cache files only for sources that actually need CPU decode/compression (anything served straight from a precompressed DDS bypasses the generated file), so the cache stays small on retail data while still sparing every later load the recompression cost — `image_writeGeneratedImages` defaults on and can be turned off to trade load time for disk, loading-screen presents temporarily bypass VSync so `r_swapInterval` cannot throttle blocking map loads, missing image fallbacks now correctly mark themselves defaulted so optional generated light-grid atlases stop retrying every frame, generated binary-image cache writes and optional light-grid misses no longer spam normal logs, and load logs now break startup time into render-world, game, collision, entity-spawn, renderer media, sound, decl, BSE, UI, and settle phases.
- [x] Level transitions now release collision geometry with balanced cache ownership: gameplay clip models acquire and release render-model collision references, renderer extraction and trace export stay reference-neutral, SP and MP use the same `.cm`-only precache path, and precaching avoids duplicate canonicalization/cache lookup passes. A clean Air Defense 2 load and an Air Defense 1-to-2 transition both settled at 7,714 KB of live collision geometry, confirming that the earlier map was not retained.
- [x] Animation-heavy level loads now build validated, endian-stable generated MD5 animation caches under `fs_savepath`: SP and MP share portable version 3, cache identity follows the exact compiled-or-text source selected by the parser, loose sources add a content CRC, and a bounded whole-record length/CRC check runs before private all-or-nothing field parsing. Corrupt or stale entries fail safely back to the selected source, atomic writes cannot publish a partial record, and animation memory accounting includes base-frame storage. An earlier Windows x64 Air Defense 1 predecessor-format profile showed the feature's potential (820 entries, 26.9 MiB, model-definition phase reduced from 2.7–2.8 seconds to 0.98–0.99 seconds after the one-time build); current v3 release measurements remain part of the pending Milestone B promotion record.
- [x] Starting a campaign map partway in no-save/dev-start flows now treats the authored loadout as already owned equipment: startup armor, ammo, inventory items, and weapons no longer flash pickup notifications, and the selected weapon is settled into its ready state instead of playing a pickup-style raise after the map appears.
- [x] Steam Deck setup is easier to diagnose and friendlier on battery: Steam asset discovery now accepts explicit Quake 4, Steam root, Steam library, Steam compatibility, and XDG override paths with startup logging, while the Deck profile can apply a display-refresh-aware default frame cap and low-battery rumble cap without rewriting user-tuned settings.
- [x] Steam Deck support now has a dedicated `listControllers` diagnostics command plus a shipping QA checklist, making it easier to capture SDL controller, gyro, touchpad, touchscreen, battery, Steam Input pass-through, suspend/resume, display, asset-discovery, and performance evidence before release.
- [x] Steam Deck controls are easier to tune in-game: Settings -> Game Options -> Controller now exposes gyro, gyro sensitivity, touchpad mode and sensitivity, touchscreen routing, and low-battery rumble caps with localized labels and settings-registry coverage.
- [x] Steam Deck support is now guarded by automated validation: launcher/profile activation, Steam asset discovery overrides, native SDL3 gyro/touchpad/touchscreen routing, suspend/resume recovery, frame-cap defaults, controller diagnostics, menu rows, and user/QA docs are checked in local validation and CI smoke jobs.
- [x] Steam Deck package guidance is clearer: the packaged README and getting-started guide now explain the Deck launcher, direct-launch auto-profile fallback, native Wayland default, `OPENQ4_FORCE_X11=1` fallback, in-game Deck controls, and `listControllers` diagnostics, while staged and nightly package validation check the generated Deck launcher before release.
- [x] The screen-space camera-flare post effect has been removed from renderer scheduling, settings, validation, metrics, and shader-library coverage while preserving authored material flare deform support used by stock content.
- [x] Post anti-aliasing is easier to trust: `r_postAA` now resolves through an explicit Off/SMAA 1x schedule with startup/runtime logs for requested and effective mode, source/output targets, and fallback reason, while SMAA lookup textures fail closed when dimensions, format, filter, or clamp mode do not match the embedded tables.
- [x] SMAA post anti-aliasing now samples with the actual post-process source size instead of the desktop video mode, improving correctness across render-scale and resized targets while logging the active target dimensions and falling back safely if source/output sizes drift.
- [x] SMAA post anti-aliasing now has an explicit render-target safety contract: source, edge/blend, and weights/final targets are named, checked for read/write aliasing before each pass, cleared before use, and labelled for GL debug captures when supported.
- [x] SMAA edge detection now declares its source colour-space contract explicitly, preserving the current legacy perceptual-LDR look while logging the active contract and preventing future HDR or tonemap changes from silently shifting edge thresholds.
- [x] SMAA post anti-aliasing is less dependent on legacy OpenGL state: the SMAA shaders now use explicit GLSL inputs and outputs, fullscreen passes draw through explicit geometry, and a state-poison gameplay profile guards against texture-unit and client-array leakage that could otherwise crash the driver.
- [x] SMAA post anti-aliasing now has opt-in quality modes with an explicit shader contract: the existing medium path remains `r_postAA 1`, `r_postAA 2` and `3` increase the luma search quality, `r_postAA 4` enables a colour-edge prototype for comparison captures, and the runtime log records the active edge mode, threshold, search steps, and local-contrast scale.
- [x] SMAA post anti-aliasing no longer blacks out gameplay when `r_postAA` is set to a nonzero mode; medium, high, ultra, and colour-edge prototype modes now keep the world visible through the explicit fullscreen pass.
- [x] The graphics settings menu now lists every Post AA mode by name, so players can choose Off, SMAA Medium, SMAA High, SMAA Ultra, or Color Edge without using the console, and the settings registry/docs now track the same full `0..4` range.
- [x] MSAA settings now stay on the supported quality ladder: `r_multiSamples` is bounded to `0..16`, console completion lists only Off/2x/4x/8x/16x values, unusual config values are normalized before video startup, SDL3 builds retry lower sample counts when a window or GL context rejects the requested MSAA level, and `gfxInfo` now summarizes requested/effective MSAA, GL sample caps, alpha-to-coverage, post AA mode, screen fraction, and supersampling state.
- [x] Steam Deck support is more complete: the engine can auto-select the Deck profile on SteamOS/Deck hosts, the Deck launcher no longer forces XWayland unless `OPENQ4_FORCE_X11=1` is set, SDL3 now handles native gyro, gamepad touchpad, and touchscreen input paths, and suspend/resume events release input, stop rumble, write config, and reacquire controllers on wake.
- [x] Projected shadow-map classification is now shared between the ARB2 renderer and modern shadow planner, so ordinary projected lights only use cascades when `r_shadowMapProjectedCSM` allows it while point and parallel lights keep their expected shadow-map behavior.
- [x] Projected shadow-map planning now uses the same padded clip planes, cascade fitting, atlas rectangles, split depths, and single-cascade fallback rules as the ARB2 renderer, reducing flashlight/projected-light drift between diagnostics and the visible renderer path.
- [x] Shadow-map diagnostics now validate planner descriptors and ARB2 projected-light state before use, surfacing invariant failures for cascade counts, atlas bounds, split depths, and light classification instead of letting malformed shadow data fail silently.
- [x] Shadow-map planner self-tests now compare modern descriptors against ARB2 reference state for projected, point, and cascaded lights, including the point-light six-face layout and projected clip/atlas/split metadata, catching contract drift before gameplay.
- [x] Shadow-map planning now budgets single projected, cascaded, and point lights through separate light/tile quotas, so cheap projected lights, expensive cascades, and six-face point shadows report their own capacity and throttling instead of competing in one opaque flat cap.
- [x] Dense shadow-map scenes now degrade more visibly when planner atlas budget is exhausted: budget-missed lights with drawable legacy shadow inputs become stencil fallback/cache-reuse candidates instead of silent skips, and diagnostics report fallback, stencil, cache-candidate, and true-skip counts separately.
- [x] Shadow-map planning now ages budget-throttled lights across frames, giving previously starved lights a bounded priority boost so dense scenes rotate mapped coverage instead of always selecting the same highest-priority lights.
- [x] Shadow-map diagnostics now retain per-light budget-throttling history, including the light index, reason mask, miss streak, recovered lights, and class/global budget reason counts, making dense-scene starvation visible instead of only reporting aggregate misses.
- [x] Shadow-map planning now recognizes ARB2 resident static-cache reuse separately from fresh atlas work, so diagnostics can show cacheable, cache-hit, cache-miss, fresh-update, and isolated ARB2-owned passes instead of treating all planner budget pressure as new shadow-map work.
- [x] Shadow-map diagnostics now include a reproducible synthetic projected-light scene that logs the same flashlight-style light through planner classification, ARB2 cascade/atlas/clip state, split depths, and receiver shader inputs for stable regression checks.
- [x] Shadow-map reports now expose the `r_shadowMapProjectedCSM` gate separately from global CSM counts, so planner, ARB2 summary, per-light, pass, and projected-state diagnostics agree on when ordinary projected lights cascade or stay single-tile.
- [x] Projected-light shadow transforms now carry the padded clip-plane contract through shared state, planner descriptors, ARB2 validation, and flashlight diagnostics, reducing the chance that flashlight shadows regress to raw, unpadded projection math.
- [x] Projected-light cascade fitting now records W-sign buckets, skipped samples, invalid projected samples, and named single-cascade fallback reasons, with synthetic flashlight coverage for mixed-W fallback behavior.
- [x] Flashlight projection and shadows now stay on the stock single-projection path for view-scoped weapon lights and the stock `gfx/lights/flashlight` projector, so projected shadow maps no longer camera-fit the flashlight into cascades while regular world projected lights can still use cascades when requested.
- [x] MCC Landing's post-bigdoor lift sequence now hands off smoothly to the broken replacement lift when it fails near the top of its upward ride, avoiding player trapping, fall-through, and snap correction.
- [x] Flashlight shadow parity restored: automatic quality detection now uses retail-style Quake 4 machine-spec thresholds again and repairs stale `com_machineSpec -1` configs, so capable systems classify as Ultra and the SDK flashlight shadow gate casts world shadows like retail.
- [x] Shadow-map diagnostics now match the active renderer path: modern-sidecar budget misses and ARB2 receiver-only local passes no longer report visible stencil fallback when the backend is actually rendering supported lights with shadow maps.
- [x] Shadow-map receiver fallback is easier to trust on characters and viewmodels: ARB2 pass and summary logs now split generated/skinned, custom-GLSL, and invalid receiver fallback counts, verbose logs name the stencil-filtered fallback path, and planner cache estimates account for mixed mapped-plus-fallback receiver passes.
- [x] Compatible custom GLSL-lit materials can now keep mapped shadow-map receiver lighting when they also expose ordinary stock interaction stages, while custom-only shader materials still use the stencil fallback; diagnostics report wrapped custom GLSL receivers separately and `r_shadowMapCustomGLSLReceiverWrapper` can disable the compatibility path for comparison.
- [x] Shadow-map caster admission is now documented and self-tested for perforated, translucent, GUI, subview, and dedicated collision-only materials: alpha-tested casters stay on the mapped depth path, translucent casters remain capability-gated through moment maps, view-producing GUI/subview surfaces stay excluded, and diagnostics now name dedicated collision hull rejections explicitly.
- [x] Shadow-map LOD behavior is easier to diagnose in dense scenes: mapped-caster LOD tests and rejections now flow from interaction building into ARB2 reports, modern planner descriptors, renderer metrics, and self-tests, including counters that flag when LOD-thinned lights also become budget-throttled or planner-unmapped.
- [x] Modern deferred/forward+ shadow ownership now has real projected, cascaded, point, and translucent moment lookups backed by ARB2-owned atlas bindings, while modern-visible handoff remains fail-closed whenever fallback receivers or required shadow atlas resources are not ready.
- [x] Shadow-map diagnostics now emit joined per-light rows that line up the planner decision, budget reason, caster and receiver counts, atlas layout, ARB2 pass result, receiver fallback breakdown, and modern clustered-light packed policy under the same light index.
- [x] Shadow-map diagnostics now split timing by map rendering, cache reuse, receiver mask work, receiver fallback, and full stencil fallback, with CPU, optional sync-GPU, and nonblocking timer-query buckets that make budget and fallback costs visible in gameplay logs.
- [x] Shadow-map timing no longer asserts on level start: the timed-phase helper now preserves `idTimer` construction state instead of raw-clearing timer objects before `Start()`, fixing debug-breaks in mapped-shadow level loads while leaving `r_postAA 0` untouched.
- [x] Shadow-map visual debugging now includes compare-depth delta, receiver eligibility, and fallback-reason modes for both projected and point-light shadows, making alignment problems and receiver fallback choices easier to capture through the gameplay benchmark presets.
- [x] The removed camera-flare overlay no longer contributes visibility sampling, ghost quads, or staged-overlay color paths; authored material flare cards remain on the legacy material path so their diffuse and additive glow colors stay separate.
- [x] Core engine math now uses SSE2 SIMD on x86-64 instead of scalar fallback code: character skinning, shadow-volume facing/projection, model bounds, and joint-matrix work run through a new vectorized processor (validated bit-compatible by `testSIMD`), reducing CPU cost per animated character and per shadow-casting light; `com_forceGenericSIMD 1` restores the previous scalar path.
- [x] The renderer submits work to the graphics driver with much less redundant chatter: repeated program parameters, vertex-array toggles, and vertex/index buffer rebinds are now filtered out across consecutive draws (`r_useRedundantStateFiltering`, default on), and the per-view surface sort uses a radix sort instead of the slower general-purpose C library sort.
- [x] With shadow maps disabled (the default stencil-shadow look), the renderer no longer evaluates per-surface shadow-map caster policy checks every frame, trimming front-end cost in light-dense scenes.
- [x] Remaining mixed-case project branding now consistently uses `openQ4` across UI strings, documentation, packaging templates, launch helpers, macOS resources, staged content, and the mod manifest field `requiredopenQ4Version`.
- [x] The Settings menu is clearer and more keyboard/controller friendly: the deprecated Aspect Ratio selector was removed, Screen Size now follows the active mode automatically, Resolution Scale exposes lower performance presets plus supersampling presets, Ambient Brightness accepts the full `0.0..1.0` range, and Crosshair Color is now a standard localized picker instead of mouse-only swatches.
- [x] More documented settings are now reachable from the menus: System exposes UI aspect correction, refresh-rate presets, windowed sizing, and custom exclusive-fullscreen sizing, while Game Options exposes auto-skip cinematics plus first-person weapon FOV/aspect/offset tuning; multiplayer now honors the same client weapon-offset cvars as single-player.
- [x] System quality toggles now behave like positive feature controls: Specular, Bump Mapping, and Sky show normal No/Yes choices while writing the underlying `r_skip*` cvars explicitly, and settings-page scroll bounds now match the expanded panes for mouse, keyboard, and controller paging.
- [x] Settings menu maintenance is less fragile: the Controls, System, Audio, and shared Settings popup widgets now live in their own GUI includes, creating clearer Settings boundaries while preserving the existing key-binding, video/display, defaults/auto-detect, and backend/device/volume controls.
- [x] Settings menu coverage is now metadata-driven for the refactored surface: Controls bind rows, Audio, active System rows, and all active Game Options value widgets have declarative registry records checked for widget presence, localization, bind targets/default keys, values/ranges, defaults, restart requirements, accessibility notes, duplicate direct cvar bindings, and Game Options registry completeness; the pass also fills missing French/Italian labels for active System, Controls, and Game Options rows.
- [x] Numeric settings fields are easier to type precisely: master volume and brightness accept decimal values, master volume now matches the engine's `0..1` range, Music Volume has a paired exact-entry field, and positive numeric settings filter stray text while signed adjustment fields continue to allow negative values.
- [x] Settings menu cleanup reduced legacy clutter: unreachable hidden System audio controls were removed now that Audio owns those settings directly, and coverage now guards the remaining dormant advanced-menu placeholders from being accidentally exposed.
- [x] Scripted Game Options settings are less fragile: Corpse Time, Gun Position, and Force Model now route their presets through typed menu adapters instead of duplicating cvar write chains in the GUI script, and turning Force Model off now clears its force-model cvars instead of leaving a stale `null` value.
- [x] Settings-page scrolling now behaves consistently with modern input: mouse wheels keep high-resolution fractional movement, Page Up/Page Down and controller shoulders move by page-sized jumps, Home/End jump to the start/end, modal popups no longer scroll the page behind them, and GUI scroll sliders clamp their values safely.
- [x] Long Settings panes are easier to navigate without changing any settings behavior: localized section pickers now jump Game Options directly to General, Mouse, Controller, Crosshair, Gameplay, or View Weapon and Display directly to Video, Window, Rendering, Quality, Post FX, or Sizing; both pickers follow normal scroll position changes. Audio now separates level, device, and effects rows into clearer groups while keeping the same controls.
- [x] Settings scroll panes now keep their new section controls out of the row viewport: Game Options no longer overlaps its title, section picker, and first rows, dragging a Settings scrollbar applies the scroll position immediately instead of only moving the thumb, and wheel/page scrolling no longer stalls behind the transient menu-animation active flag.
- [x] Settings page scrolling now has a single engine-owned layout path: Game Options, System, and Audio route scrollbar drags, wheel/page keys, controller paging, and section jumps through `applySettingsScroll`, which clamps the cvar, syncs the section picker, controls thumb visibility, and writes the visible row-canvas rect directly instead of replaying stale GUI transition tables.
- [x] Console and menu mouse routing no longer clamps cursor coordinates to the virtual screen or console bounds, preventing cursor snapping/sticking when using the console or moving across letterboxed, high-DPI, or edge-of-window regions.
- [x] Linux SDL3 menu cursor routing now follows the real host pointer when menu input activates, matching the already-working console cursor path and restoring reliable menu hovering and clicking on modern Linux desktops.
- [x] Linux SDL3 mouse capture now recovers reliably after opening or closing the pull-down console: POSIX frame event generation applies SDL input-frame state like Windows, and console routing is treated as the topmost mouse target before returning control to gameplay or menus.
- [x] Linux and experimental macOS SDL3 single-player loading screens now pump SDL window events while the displayed "press any key to continue" prompt is active and honor normal keyboard and printable character input, matching the expected Windows behavior after a level finishes loading. Mouse clicks now also dismiss the prompt on all SDL3 platforms: the continue gate raises a loading-input flag so the backend queues mouse-button key events even though the mouse is neither captured nor menu-routed during loading, and `tools/tests/loading_continue_input.py` locks the whole path in CI.
- [x] Linux VSync support is more consistent: SDL3 now applies and reports the requested swap interval during startup, display-mode changes, and live `r_swapInterval` edits, while the native GLX fallback now honors the same cvar through EXT/MESA/SGI swap-control extensions when available.
- [x] Native Wayland now restores SDL's OpenGL context before every swap-interval update, so initial window setup can no longer leave `r_swapInterval` unapplied with a `No OpenGL context has been made current` error.
- [x] Linux SDL3 native Wayland startup is more robust: bundled GLEW now initializes through SDL's current OpenGL proc-address path and skips GLX-only extension probing on the SDL3 backend, preventing the post-context `Init Glew...` crash reported on CachyOS while preserving the native GLX comparison backend.
- [x] Startup language selection is more predictable: after `default.cfg`, user config, and `autoexec.cfg` run, command-line `+set sys_lang ...` is restored before the final language dictionary reload, so launch scripts can override stale archived language settings.
- [x] First-run language selection now respects installed retail language packs: openQ4 discovers available `q4base/zpak_<language>.pk4` packs, follows the OS language preference when multiple packs are present, uses the only available pack when appropriate, and repairs stale `sys_lang` values that point at missing localization media.
- [x] Linux graphics auto-detect no longer falls back to the legacy 64 MB VRAM path when modern drivers or VMs do not expose a queryable VRAM value, preventing fresh Linux configs from enabling the low-quality 256px texture downsize preset; existing affected Linux configs migrate out of that stale texture cap, GUI/font presentation atlases are protected from downsizing, and `sys_videoRam` can now be forced above 512 MB for manual validation.
- [x] SDL3 Linux VRAM autodetection is less X11-dependent: the SDL3 backend now enumerates DRM card and render-node sysfs VRAM totals before falling back to known nodes and legacy `/proc/dri`, improving native Wayland and minimal Linux sessions where optional XNVCtrl helpers are unavailable.
- [x] Linux Wayland startup guidance is clearer before SDL video initialization: mixed and pure Wayland sessions now report openQ4's `OPENQ4_FORCE_X11`, `OPENQ4_WAYLAND_PREFER_LIBDECOR`, and `OPENQ4_WAYLAND_SYNC_WINDOW_OPS` fallback/diagnostic switches instead of only pointing users at raw SDL driver variables.
- [x] SDL3 Linux desktop-resolution detection is more robust on unusual compositors: if SDL cannot report a desktop display mode, openQ4 now falls back to the current display mode and then display bounds before failing the query.
- [x] Multiplayer weapon-wheel handling no longer tries to restore the cheat-protected `timescale` cvar every frame, removing the repeated "timescale cannot be changed in multiplayer" warning while preserving normal MP weapon selection.
- [x] The single-player weapon wheel no longer owns the public `timescale` cvar: a dedicated transient engine/GameLib time-scale channel now drives smooth simulation and audio slowdown and releases cleanly on wheel close or Escape. The Quake 4 SDK walk/run condition is restored as well, so console changes to `timescale` remain intact and `pm_walkspeed` once again governs walking instead of being bypassed by forced running.
- [x] Startup and SP map validation logs are cleaner: stock intro loading screens now select their cinematic loading background from engine code without a mapDef override, openQ4 napalm gameplay decl replacements apply without duplicate-decl noise, stale staged intro-video material overrides no longer shadow stock assets, and optional packed-MD5R shader probes skip missing variants quietly.
- [x] Stock mappack material loading is quieter: known Quake 4 addon redeclarations between `mapobjects_mp2`, `mappack1`, and `stroyent_mp` no longer flood developer logs while unrelated duplicate material warnings remain visible.
- [x] Developer map-launch logs are less noisy during stock content validation: duplicate/tripled silhouette-edge topology diagnostics are now opt-in through `r_reportSilhouetteEdgeWarnings` instead of flooding normal `developer 1` runs.
- [x] Stock screen-wipe materials are now loaded during renderer startup, removing late `gfx/wipes/fade` and `gfx/wipes/fade_blend` decl warnings from SP/MP map-transition validation logs.
- [x] GUI text now measures the same guarded glyph extents it draws, and tight stock radio labels keep the guarded final glyph visible without expanding the small marine font atlas sampling, including the normal/vehicle HUD `incoming` and `transmission` labels, while long loading-screen map titles remain correctly aligned.
- [x] Stock GUI popup animations now keep their retail-style button labels and layered panels, fixing the exit-confirmation screen regression where the Yes/No text disappeared and the button backgrounds rendered as faint translucent strips.
- [x] Linux packaging and launch validation are stricter: staged and packaged engine binaries must remain executable, desktop entries now point at the real case-sensitive launcher names, the Steam Deck desktop entry launches `openQ4-steamdeck` correctly, Linux saves honor valid absolute `XDG_DATA_HOME`, and executable-path discovery fails clearly instead of using a truncated path.
- [x] Linux desktop setup is easier: a helper now creates an executable openQ4 launcher on the user's desktop with absolute paths to the staged runtime, icon, and optional Quake 4 asset root, and the Linux Mint VM workflow can install it through a dedicated `Launcher` action.
- [x] Linux Mint VM validation now builds and launches cleanly with the SDL3 fallback: the VM bootstrap installs a current Meson for SDL3, the Linux build wrapper honors an explicit Meson override, first-install cleanup no longer fails when `.install/` does not exist yet, and the Steam Deck launcher template path matches Linux case-sensitive filesystems.
- [x] SDL3 Linux builds no longer require X11/Xext development packages just to configure: the NVIDIA XNVCtrl VRAM probe is now an optional helper compiled only when X11 is available, while the native GLX backend keeps its explicit X11/VidMode requirements.
- [x] POSIX networking now uses modern address resolution instead of obsolete IPv4-only lookups: Linux and experimental macOS builds accept IPv6 literals and AAAA records, bind IPv4 and IPv6 UDP sockets side by side for the default local interface, and keep legacy IPv4 LAN broadcast behavior intact.
- [x] IPv4 hosting and direct connections are more dependable across supported platforms: strict endpoint handling accepts the full unsigned port range, server-list results preserve ports `32768` through `65535`, malformed list tails and unresolved remote-console targets fail safely, and an invalid explicit `net_ip` now fails binding instead of silently opening every interface. Windows resolver, UDP, and nonblocking TCP paths follow the hardened network contract, while the local `netIPv4SelfTest` command provides a focused parser and UDP-loopback developer diagnostic without claiming public network reachability.
- [x] IPv6 multiplayer is complete and symmetric across Windows, Linux, and experimental macOS: one `idPort` binds both families on the same port and routes each datagram to the socket of its own family, `net_ip6`/`net_enableIPv4`/`net_enableIPv6` select interfaces and families including IPv6-only hosting, a resolved IPv4 bind failure still fails closed so a port scan cannot settle on an endpoint IPv4 peers cannot reach, and the two receive sockets alternate so a flood on one family cannot starve the other. Addresses use one shared canonical RFC 5952 formatter on every platform, so the server browser's address-keyed dictionary and the game module's ban list can no longer collide two hosts onto one entry; IPv4 peers arriving through `::ffff:0:0/96` are normalized to a single IPv4 identity; LAN discovery reaches IPv6 servers through per-link `ff02::1` multicast in place of the broadcast IPv6 lacks; message fragments respect the 1280-byte IPv6 minimum path MTU that routers may not fragment; and the local `netIPv6SelfTest` command provides a parser, canonical-text, and IPv6-plus-dual-stack UDP-loopback developer diagnostic without claiming public network reachability.
- [x] POSIX thread shutdown is now cooperative instead of cancellation-driven: engine threads can request stop through shared thread state, async/background/light-grid workers observe that request in their normal loops, and Unix shutdown no longer depends on `pthread_cancel()` reaching a cancellation point.
- [x] POSIX elapsed timing now uses a monotonic clock instead of wall-clock time, so Linux and experimental macOS frame/event deltas keep advancing steadily across system time changes.
- [x] Linux and experimental macOS SDL3 builds now have a desktop system console window matching the Windows log console path: `sys_viewlog`/`win_viewlog` can show it, dedicated or skip-renderer sessions auto-show it when SDL video is available, and the window supports scrollback, command entry, history, tab completion, copy, clear, and quit controls while keeping terminal input/output available.
- [x] Linux and experimental macOS SDL3 startup now shows and tears down the same splash bitmap used by Windows before the renderer is ready, includes that splash asset in staged non-Windows packages, falls back to a built-in startup panel if the bitmap cannot be found, gives splash, system-console, and renderer users independent matched SDL video references instead of reinitializing the subsystem under X11/XWayland, and recreates the SDL OpenGL window between context-tier retries so a failed GLX make-current attempt cannot poison every fallback.
- [x] Linux ARM64 is now covered by normal CI before release packaging: push and PR validation build/stage on native ARM64 runners, run an assetless renderer startup smoke under a virtual X display, and keep that smoke path bootable even when stock PK4 material or AAS declarations are intentionally absent. This hosted smoke does not replace real ARM64 Wayland SP/MP gameplay, audio, and input signoff.
- [x] The native Linux X11/GLX backend now closes its X display connection cleanly during shutdown, restoring display mode/window resources before `XCloseDisplay()` and closing the display before unloading GLX.
- [x] Loose-file lookups on case-sensitive Linux and macOS filesystems now resolve existing mixed-case directory segments instead of assuming lowercase paths, while exact-case probes use a direct type-checked lookup before directory enumeration. This preserves mixed-case asset/mod discovery and removes repeated parent-directory scans that made stock PK4 map loads disproportionately slow on high-latency filesystems.
- [x] Linux ARM64 release packages build again: the platform timing code now keeps x86-only CPU-frequency fallback parsing out of ARM64 compiles while continuing to use the monotonic nanosecond clock rate on non-x86 hosts.
- [x] Linux and experimental macOS runtime support is safer under normal desktop use: URL/update handoff no longer routes through a shell, Linux freedesktop URL openers work without a custom `openurl.sh`, macOS URL opens are validated before reaching `NSWorkspace`, macOS delayed process handoff now runs instead of hitting a stub, macOS GL startup fails cleanly on mode/context setup errors, long POSIX/macOS paths avoid old 256/1024-byte truncation points, free-space and memory checks use the real platform APIs, sleeps use millisecond timing without busy-looping, and POSIX networking handles DNS, strict-alignment IPv4 parsing, invalid packet sizes, socket cleanup, and nonblocking TCP edge cases more robustly.
- [x] Release packaging is better tuned per platform: stable packages now use optimized `debugoptimized` binaries with `b_ndebug=true` on Windows, Linux, and experimental macOS; Linux publishes separate detached debug-symbol archives for native crash analysis; Windows packages keep matching PDB diagnostic symbols; and Windows crashes write `crashes/openq4_crash_*.log` plus `.dmp` minidumps beside the executable for easier issue reports.
- [x] Manual release version selection is clearer: the workflow now offers `auto`, `major (x..)`, `minor (.x.)`, and `patch (..x)` choices, with patch replacing the old serial wording for third-component release bumps.
- [x] CRT Emulation no longer enables a heavy chromatic channel offset by default, fixing the red/green edge artifact that could affect gameplay, HUD, and menu output when 0.3.200 users enabled the setting; legacy archived chromatic values are capped to a subtle range.
- [x] Modern visible rendering now preserves scene-referred highlight energy in its FP16 lighting targets, restoring visible bloom extraction and HDR tonemapping/color controls when the hybrid renderer is enabled.
- [x] SSAO now darkens the final scene again under the hybrid renderer by sampling the composed scene with a world-only depth source, restoring contact darkening around wall junctions and inset details while keeping viewmodels and other foreground depth-hacked surfaces out of the effect.
- [x] Skyboxes and portal skies now remain visible when SSAO or other root-scene post-processing paths are enabled, so sky-portal scenes such as `game/airdefense1` no longer lose their sky during the final scene-target present.
- [x] SSAO no longer leaves stale material or texture state after its world-depth capture, preventing blue tinting or zebra-like striping on world models when enabled.
- [x] Supersampling in the video menu now works as a real above-native scene render instead of being clamped away: `r_screenFraction` supports safe `125%`, `150%`, and `200%` presets, clamps against GL texture limits, keeps legacy below-native scaling intact, avoids stacking MSAA onto the oversized scene target, and resolves the supersampled scene before native-resolution GUI presentation.
- [x] The `com_showFPS` overlay now uses explicit modes: `1` draws during active gameplay only, while `2` keeps the legacy always-on behavior for menus, loading, and fallback screens.
- [x] Top-right diagnostic overlays such as `com_showFPS`, frame pacing, memory, async networking, and PROC/AAS notices now draw at half size, making the readouts less intrusive during gameplay and renderer validation.
- [x] Weapon impact hit marks now appear on world surfaces again, with stock BSE decal effects honoring `g_decals` and projecting bullet-hole decals into renderer geometry correctly.
- [x] The multiplayer HUD's sheared ammo, health, and armor panels now remain visible under the modern Forward+ renderer instead of disappearing after the renderer rework.
- [x] First-person weapon model GUI readouts now remain visible under the hybrid modern-visible renderer, restoring stock on-weapon ammo/display text such as the machinegun panel without replacement assets.
- [x] Bloom bright-pass extraction now preserves saturated HDR highlights without making ordinary saturated LDR textures glow, clamps negative scratch-chain energy out of the bloom pyramid, and skips the bloom pass entirely when the intensity slider is effectively zero.
- [x] HDR tonemapping and auto exposure now sanitize scene-referred FP input before display mapping or luminance metering, preventing negative scratch-target energy from causing color inversions or exposure pumping; the tone curve uses a monotonic shoulder while preserving midrange SDR contrast, and auto exposure no longer forces the HDR scene-target path when tonemapping is disabled.
- [x] The renderer can reuse model-space dynamic snapshots for transform-only entity-definition updates, avoiding redundant CPU skinning when a caller submits repeated-state presentation transforms while keeping shadows and lighting correct (`r_useRepeatedStateReuse`, default on; reuse hits show as `snapshotsReused` under `r_showUpdates 1`). This is renderer groundwork and does not claim that generic game entities are currently interpolated.
- [x] HDR auto exposure no longer stalls the GPU pipeline every frame: the luminance sample is read back asynchronously with one frame of latency (`r_hdrAutoExposureAsync`, default on), removing a per-frame synchronization point that could cause frame-pacing hitches when `r_hdrAutoExposure` is enabled.
- [x] Damage overlays (wound marks) on animated models now appear on the next rendered frame instead of waiting for the model's next animation-driven refresh.
- [x] The opt-in modern renderer path is more robust: draw-plan overflow/not-ready conditions report to the console instead of silently dropping modern draws to the legacy path, shader-library debug reloads can no longer leave stale program state in the draw/submit plans, GPU validation readbacks refuse to run without GL sync objects instead of guessing readiness, material-record lookups are O(1), and per-draw matrix work moved to plan-build time.
- [x] Shader Library V2 validation now focuses on the remaining internal shader families across forced GL 3.3, 4.1, 4.3, 4.5, and 4.6 tiers, with generic shader-library cases replacing the removed camera-flare-specific probes.
- [x] Bloom, HDR, SSAO, and tonemapping now run only on the root world view instead of nested sky/mirror/camera subviews, so scene post-processing is applied once to the final frame and no longer appears attached only to subview content.
- [x] Linux startup and shutdown are more fault-tolerant: early executable-path and `/proc/cpuinfo` parsing no longer risk startup memory corruption, POSIX quit signals now exit through the main thread with the correct signal-style exit code, crash signals avoid unsafe recursive engine shutdown from the signal handler, TCP writes no longer install a temporary SIGPIPE handler, dedicated binaries no longer print client display-session guidance, openQ4's native GLX VidMode dependency is limited to the native backend, and the native GLX fallback tolerates partial window/context initialization without null-state cleanup crashes.
- [x] Linux startup now handles Quake 4 addon metadata and PK4 checksums safely, fixing the launch-time segmentation fault seen when `q4cmp_pak001.pk4` or other addon PK4s trigger lexer punctuation-table setup on GCC/Clang builds and restoring deterministic official-media validation on 64-bit Unix platforms.
- [x] Linux and experimental macOS collision/render math now uses fixed 32-bit float and sign-bit operations instead of platform-sized `long`, fixing the 64-bit Unix crash seen during single-player collision initialization on maps such as `game/airdefense1` and preventing corrupted normalization/tangent math from stretching tinted world geometry across the scene.
- [x] Linux BSE particle effects now use the same 32-bit Raven random-number sequence as Windows, fixing the oversized translucent effect sheets that could wash multiplayer scenes yellow while preserving the authored effect timing and shape.
- [x] Character heads now bind to a fully current body pose and skip invalid copied joint samples, fixing intermittent head/body misalignment seen on Linux while making SP, MP, and client-side head attachments deterministic across platforms.
- [x] Linux 64-bit game-event dispatch now preserves script, animation frame-command, entity, vector, and float arguments through the SP/MP game libraries, preventing enemy AI or animation callbacks from freezing after pointer or ABI-sensitive event data is corrupted.
- [x] Saving immediately after a stock map starts now records deterministic tactical-AI sensor state, preventing strict Linux runtimes from rejecting uninitialized SP/MP search data before an AI has performed its first tactical search.
- [x] Particle-physics objects now start with a defined unbound orientation state, allowing immediate stock-map saves to serialize items consistently on strict Linux runtimes.
- [x] Rigid-body physics now starts with its drop-to-floor solid-test state disabled explicitly, preventing immediate stock-map saves from reading an indeterminate flag on strict Linux runtimes.
- [x] Immediate saves now preserve deterministic actor death-push, Strogg Marine attack, and Harvester turn/sweep state, while the Makron's bolt-sweep completion state starts disabled, avoiding platform-dependent save data before those combat paths first run.
- [x] Vehicle positions now initialize their previous command, view-angle, and impulse-sequence history, making first-frame input comparisons and immediate save data deterministic across Linux architectures.
- [x] Vehicle weapons now begin with deterministic animation and muzzle-flash state and honor their authored flash duration from first spawn, so immediate saves and first shots behave consistently across Linux architectures while partially constructed weapons remain safe to destroy.
- [x] Single-player map transitions now release every PVS array with its matching array deallocator, allowing stock save/reload and map shutdown paths to complete under strict Linux allocators instead of aborting after a successful save.
- [x] Harvester whip projectiles and gauntlet/dark-matter client effects now restore through aligned object-pointer temporaries with type checks before updating their compact spawn-ID wrappers, preserving the existing save format without overwriting adjacent state on 64-bit Linux and ARM64.
- [x] SP/MP players now construct with the transient emote state explicitly disabled, so a restored player resumes idle gameplay deterministically instead of reading allocator residue that was intentionally absent from the save format.
- [x] Empty multiplayer rankings and terminal tram, spline, and vehicle paths now skip zero-item sorts, avoiding strict Linux runtime failures during server startup/disconnect and on stock transport routes with no next target.
- [x] Linux packages now stage the same openQ4 HUD GUI overrides as Windows, restoring the widescreen/tallscreen HUD alignment metadata for health, armor, ammo, and multiplayer overlays instead of falling back to stock 4:3 GUI scripts.
- [x] Startup asset handling now gives openQ4-style single-digit `pakN.pk4` archives priority over wider retail-style `pakNN.pk4`/`pakNNN.pk4` names in the same directory, and startup now reports a targeted error when official Quake 4 media PK4s are found under `baseoq4` instead of `q4base`.
- [x] Startup asset validation now accepts physical/DVD-style Quake 4 installs that only include the core retail media set (`pak001.pk4` through `pak022.pk4`), while still recognizing optional official patch/menu packs such as `pak023.pk4` through `pak025.pk4` when present.
- [x] Release packaging now generates `baseoq4/mod.json` from the effective engine release version, treats `requiredopenQ4Version` as a minimum compatible engine version, and rejects stable packages whose manifest version drifts, fixing the `v0.2.000` package startup failure where the splash screen appeared and the client exited before normal logging.
- [x] SMAA post anti-aliasing now keeps the resolved scene in a dedicated post-process source target, fixes the black-screen regression from copy-based scene preservation, restores stock LDR lighting parity in the post-AA render path, and marks game-level scene render targets as safe current-render feedback sources so stock heat-haze effects around fires distort the scene instead of drawing black masks.
- [x] BSE fire and smoke effects now preserve their authored tint when effect owners arrive with legacy zeroed color parameters while keeping explicit material-color effects opt-in.
- [x] Mouse input now handles modern high-DPI mice without camera stutter: the legacy hard cutoff that zeroed large view deltas is disabled by default, `m_maxMouseDelta` can restore an opt-in legacy clamp for genuinely noisy hardware, and the in-game mouse-sensitivity control now reaches sub-1 values for modern DPI tuning.
- [x] Advanced mouse tuning now covers the QuakeLive-style configuration surface while keeping openQ4's direct raw-input feel by default: the in-game Mouse Input section exposes CPI-normalized sensitivity, view filtering, signed acceleration, acceleration offset/curve, and sensitivity cap controls, while `cl_mouseAccelDebug` can write `logs/mouse.log` samples for calibration from the console.
- [x] Scoped aiming now has a dedicated Scope Sensitivity setting for mouse, gyro/touchpad mouse-look, and controller stick look; fresh configs default to 50% look speed while scoped, and the control appears in Game Options -> General beside Toggle Zoom.
- [x] Linux mouse input now maps bindable `MOUSE6` through `MOUSE8` on both SDL3 and the native X11 fallback; the SDL3 path also preserves fractional relative-motion deltas, requests unscaled relative movement where SDL supports it, and shares the same implementation source as Windows through platform wrapper files.
- [x] Linux SDL3 mouse feel is steadier on scaled high-DPI desktops: fractional relative motion now waits for whole engine counts instead of rounding early, and menu/console mouse coordinates refresh when SDL reports a display or DPI-scale change.
- [x] Cross-platform input compatibility improved beyond the SDL3 mouse path: native Linux now filters unmapped X11 keys before they enter the poll buffer, the shared POSIX poll layer ignores invalid key `0`, and native macOS keyboard/mouse input now filters unknown virtual keys, synthesizes edit-field control characters, tracks Command/Caps modifier changes, maps `MOUSE6` through `MOUSE8`, and accumulates fractional scroll-wheel deltas.
- [x] The shared SDL3 input backend now has an experimental macOS source-selection path instead of silently falling back to native Cocoa sources, so keyboard, mouse, gamepad/joystick, hotplug, and rumble handling are implemented through the same SDL3 backend on Windows, Linux, and experimental macOS. macOS hardware validation is still required before calling that path first-class.
- [x] Experimental macOS SDL3 builds now link the POSIX process, shutdown, sleep, error, and clipboard services they need instead of mixing in the legacy Cocoa app bootstrap, and SDL3 Linux/experimental macOS paste operations now use the engine allocator expected by console and edit-field callers.
- [x] Input polling is more defensive across backends: SDL3, native POSIX, dedicated stubs, and legacy Win32 now reject invalid key/mouse events, skip no-op mouse deltas, clear failed poll-return outputs, and reset controller tracking cleanly around hotplug, disable, and shutdown paths.
- [x] Controller input tuning is now available directly from the in-game settings menu, with localized controls for enabling controllers, changing stick layout, adjusting radial dead zone, look sensitivity, look curve, invert-look, trigger press threshold, and toggling or scaling rumble.
- [x] Analog controller handling now uses radial stick shaping, response-curve cvars, configurable southpaw/inverted look behavior, and immediate recalc when controller tuning changes in the menu.
- [x] Controller looking now applies sensitivity to normalized stick deflection instead of the raw `-127..127` axis value, fixing ultra-fast Xbox-style right-stick turning even when Look Sensitivity is set to the minimum.
- [x] Generic SDL joystick handling now keeps documented auto axis defaults for movement, dedicated look, and vertical/throttle input, exposes console cvars for unusual raw-axis layouts, logs joystick axis/button/hat counts on open, and adds trigger-button hysteresis so analog trigger binds do not chatter around the press threshold.
- [x] Controller look defaults now use lower sensitivity and a smoother response curve, so fresh configs and reset-defaults are less likely to feel twitchy or over-fast on analog sticks.
- [x] Controller menu navigation now feels native: focused menu buttons activate from A/Enter, D-pad and left-stick menu movement support hold-repeat, shoulder buttons page/scroll longer lists, controller users can open keybind capture without needing a mouse, and SDL3 gamepads/joysticks now receive gameplay rumble from Quake 4 sound shake/rumble metadata with `in_joystickRumble` controls that stop cleanly and avoid redundant motor refreshes.
- [x] Default controller binds now cover SDL3's menu, Guide, touchpad, trigger, stick-click, D-pad, and paddle button surface explicitly, while leaving ambiguous extra device buttons bindable but unassigned for user customization.
- [x] Bundled SDL3 is updated to 3.4.10, pulling in the latest cross-platform gamepad, joystick, Steam Controller/Triton, touchpad, sensor, and rumble fixes for the shared Windows/Linux/experimental macOS SDL3 backend.
- [x] Native Wayland compatibility improved for the SDL3 Linux backend: startup logs the selected video driver, Wayland/X11 environment, active Wayland hints, display content scale, orientation, current/desktop display modes, exact refresh details when SDL reports them, and compositor-accepted window state after screen changes; direct launches honor `OPENQ4_FORCE_X11=1` as an XWayland fallback unless an explicit SDL video-driver override is set; `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` can opt into SDL's libdecor preference for compositor decoration issues; `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1` can opt into SDL's blocking window-operation synchronization for diagnosis; Wayland sessions avoid persisting compositor-owned window positions; relative mouse-look can continue when pointer confinement is unavailable; compositor-negotiated size/fullscreen changes are synchronized before renderer placement is refreshed; multi-display spanning gracefully falls back to the selected display; and `r_glTier auto` tries SDL's unversioned compatibility context before explicit profile/version contexts.
- [x] Shadow-map quality and pacing controls advanced: static projected lights can now reuse resident shadow maps with hysteresis instead of rerendering every view, update budgets account for cached reuse separately from fresh work, optional stable Poisson and projected PCSS-lite filters improve soft-shadow tuning, the experimental point-light cubemap path remains available behind `r_shadowMapPointLights 1`, and translucent shadow moments gained separate softness and light-bleed controls.
- [x] Shadow-map diagnostics and stability improved: projected and authored parallel lights now report richer caster/timing/atlas metrics with semantic light class separated from backing resource shape, optional GL timer-query diagnostics, rejected-caster reason breakdowns, off-screen shadow-map casters submitted independently from visible receiver scissors, projected CSM bias reports per-cascade near/far ranges, receiver bias uses texel-aware terms with scalar bias as a compatibility floor, hashed alpha uses stable world-space seeds, new debug/tuning cvars expose compare sampling, filter tap budgets, update budgeting, and a bias heatmap, and multiplayer maps such as `mp/q4dm1` keep their authored point lighting/shadows by using the legacy stencil path for point lights unless the experimental point-shadow-map path is explicitly enabled.
- [x] Projected shadow maps now store Quake 4 light-falloff depth directly instead of comparing projectively divided falloff depth, fixing detached or unraveled shadows from angled projected lights and letting the default projected bias and caster offset values drop to contact-friendly levels; GLSL cutout shadow casters also preserve material alphaFunc comparison modes instead of assuming all alpha-tested stages use greater-than thresholds.
- [x] Shadow-map static-cache signatures now cover caster materials, alpha/hash settings, caster offset, point-light range/depth mode, and view-fitted CSM inputs, while point/translucent caster shaders reject out-of-range depth and translucent coverage masks preserve material alphaFunc comparison modes.
- [x] Static models with authored collision helper surfaces now keep those surfaces available for traces without letting them cast duplicate stencil/prelight shadows, fixing stock crate-style props whose hidden collision hulls overlap the visible mesh.
- [x] GL renderer modernization groundwork is in place without replacing the shipping ARB2 path: SDL3 now negotiates explicit OpenGL tiers, `gfxInfo` reports selected tier/caps, `r_glTier`, `r_glDebugContext`, and `r_rendererMetrics` expose bring-up controls, extension parsing is token-safe, and the first render-graph, scene-packet, upload-manager, and metrics scaffolds wrap the legacy renderer for future GL 3.3/4.x work.
- [x] The GL renderer upload bridge now owns frame-temporary vertex-cache streaming behind the existing ARB2 renderer: dynamic uploads use persistent mapped buffers on capable GL 4.4+ drivers, map-range/subdata fallbacks below that, multiple frame buffers with sync when available, `r_rendererUploadMegs`/`r_rendererUploadPersistent` controls, `gfxInfo` reporting, and upload high-water/overflow metrics while preserving the old temp-buffer fallback.
- [x] The renderer upload layer now owns long-lived vertex-cache buffer uploads instead of only frame-temp streaming: `idVertexCache::Alloc` routes static vertex/index VBO creation and data upload through `UploadManager`/`BufferAllocator`, frees those buffers through the same owner, retains the old direct GL/virtual-memory fallback, expands `rendererUploadSelfTest`, and reports static upload/live-buffer counts in `gfxInfo` and `r_rendererMetrics`.
- [x] Renderer metrics now include nonblocking GL timer-query sampling behind `r_rendererGpuTimers`: the existing ARB2 backend reports resolved GPU timing for 3D views, 2D/GUI views, render-target work, copy-render work, special effects, and buffer switches when supported, `gfxInfo` exposes timer availability, and `rendererGpuTimerSelfTest` verifies the live query path without changing rendering output.
- [x] The renderer scene-packet contract now converts legacy draw data into `ScenePacket`, `PassPacket`, and `DrawPacket` records for validation and metrics: `r_rendererMetrics` reports scene/pass/draw packet counts, `gfxInfo` exposes the packet limits, and `rendererScenePacketSelfTest` verifies the packet contract while the legacy renderer still executes the original commands.
- [x] The render graph now consumes scene packets as its first packet-driven data source: legacy pass nodes are built from packet categories, graph pass/pass-packet/draw coverage appears in `r_rendererMetrics`, `gfxInfo` reports graph bridge limits, and `rendererRenderGraphSelfTest` validates world and GUI graph construction while ARB2 output remains unchanged.
- [x] Draw packets now carry legacy material/resource and sort metadata for future modern GL executors: packet records include material-resource entries, first bump/diffuse/specular stage images where available, permutation hints, legacy sort keys, geometry/scissor/cache availability, `r_rendererMetrics` reports material/resource/geometry packet coverage, and `gfxInfo` reports the material-record bridge limit without changing ARB2 output.
- [x] The first modern GL executor entry point is available behind `r_rendererModernExecutor`: capable GL 3.3+ tiers allocate a starter VAO and frame-constants UBO, consume scene packets/render-graph data each backend frame, report preparation coverage in renderer metrics and `gfxInfo`, and deliberately fall back to the ARB2 bridge for all drawing until modern draw submission lands.
- [x] The modern GL executor now validates a pass-oriented internal shader-library contract without changing visible rendering: capable GL 3.3+ tiers compile and reflect built-in depth, shadow-depth, flat-material, light-grid, fog/blend, GUI, and post-copy GLSL 330/410/430/450 variants as supported by the selected context, attach pass/material/permutation metadata plus uniform/sampler reflection, expose shader readiness in `gfxInfo` and renderer metrics, and keep ARB2 as the visible draw path.
- [x] The modern GL executor now builds its first prepare-only draw plan from scene packets and the packet render graph: capable tiers classify depth, shadow-depth, flat-material, light-grid, and fog/blend packet categories, select validated internal GLSL programs with permutation/reflection metadata, report plan draw/state-batch/fallback coverage in `gfxInfo` and renderer metrics, and still leave all visible drawing on the ARB2 compatibility path.
- [x] The modern GL executor now derives its first non-rendering submit-readiness plan from the draw plan: capable tiers identify which planned draws already have VBO-backed ambient/index cache state for a future GL 3.3 submitter, report missing-buffer fallback reasons plus program/vertex/index/scissor/material batch counts in `gfxInfo` and renderer metrics, and still leave all visible drawing on the ARB2 compatibility path.
- [x] The modern GL executor now has its first real opt-in GL 3.3 submission path behind `r_rendererModernSubmit`: ready draw-plan commands bind the internal GLSL programs, frame UBO, per-draw MVP, ambient VBO, and VBO-backed or uploaded CPU index data, execute masked diagnostic `glDraw*` calls for RenderDoc/metrics validation, report submitted/fallback/upload counts in `gfxInfo` and `r_rendererMetrics`, and still leave ARB2 responsible for the visible frame.
- [x] The modern GL executor now routes diagnostic submission through a shared `GLStateCache`: redundant program, VAO, buffer, framebuffer, viewport/scissor, blend/depth/stencil/raster, texture, sampler, and color-mask changes are suppressed and counted, ARB2 handoff invalidation is explicit, KHR_debug scopes/object labels are used where available, and `rendererGLStateCacheSelfTest`, `gfxInfo`, and detailed renderer metrics report the cache behavior without changing visible output.
- [x] GL 4.3+ and GL 4.5+ renderer tiers now execute real tier-gated work instead of only reporting capability labels: `GpuDrivenGL43` allocates and updates SSBO scene records, validation SSBOs, draw-indirect command buffers, and compute validation dispatches from the modern draw/submit plans, while `LowOverheadGL45` uses DSA buffer updates and multi-bind UBO/SSBO binding; `gfxInfo`, detailed renderer metrics, and the modern executor self-test report the active resource path while ARB2 remains the visible renderer.
- [x] The GL 4.5 low-overhead renderer path now covers the resource, binding, and upload primitives needed for CPU-cost reduction: graph textures/FBOs allocate through DSA immutable storage and named framebuffers, the modern executor creates a DSA sampler and uses buffer/texture/sampler multi-bind groups, submit-plan program/material batch compaction is reported, persistent upload streams default through the selected low-overhead tier with fence retire/wait diagnostics, and `rendererLowOverheadSelfTest` plus the safe validation matrix cover the path while bindless remains a default-off diagnostic experiment.
- [x] The GL context ladder is now shared across platform backends instead of being a Windows/SDL-local array: Windows SDL3, Linux SDL3, native Linux/GLX, and native macOS/NSOpenGL consume the same tier candidate table, forced modern tiers start at their requested version and walk downward through core and compatibility fallbacks, debug-context requests fall back to non-debug candidates, macOS caps requests at 4.1 core, and `rendererContextLadderSelfTest` plus `gfxInfo` cover the selected request.
- [x] The renderer validation matrix now has an automated safe runner and documented manual sign-off coverage: `tools/tests/renderer_validation_matrix.py` starts the staged client without map loads, runs all renderer foundation self-tests, probes forced `r_glTier` values, debug context fallback, and presentation cvars, emits Markdown/JSON reports under `.tmp/renderer-validation/`, and `docs/dev/renderer-validation-matrix.md` tracks the required SP/MP gameplay matrix separately until map startup is safe to automate.
- [x] The renderer now has an opt-in gameplay benchmark harness for target-hardware evidence: `tools/tests/renderer_gameplay_benchmark.py` launches from `.install`, enters the required SP maps or an MP listen server plus loopback client, waits for active gameplay through a one-shot SP/MP `g_autoExecAfterMapLoad` hook, captures screenshots, prints `rendererBenchmarkCapture`, `framePacingSnapshot`, and `gfxInfo`, optionally compares deterministic TGA references, and fails cases on renderer overflow/idStr/shader/link/fatal log markers.
- [x] Renderer recovery validation now targets high-FPS gameplay instead of low-cap smoke tests: `com_maxfps` defaults to `240`, presentation coverage uses `120/240/uncapped` instead of `30 FPS`, `game/storage1` is the primary smoke scene, pacing-only runs avoid diagnostic overhead, wall-clock sampling measures real three-second gameplay windows, and parsed frame-pacing thresholds can enforce a `120+ FPS` acceptance window.
- [x] Stock Quake 4 lighting and default renderer performance are restored in dense indoor scenes: authored detail lights now remain enabled by default, portal-sky setup once again uses retail-style PVS/skybox checks, unused shadow-map caster work is skipped when shadow maps are off, expensive shadow-surface scissor tightening is opt-in on the default stencil-shadow path, and BSE effects keep conservative default servicing while the more aggressive loop-service/frustum culls remain opt-in diagnostics.
- [x] BSE particles now have an opt-in soft-particle path: `r_softParticles 1` depth-fades compatible blended and additive sprite/oriented effect quads against the opaque scene depth, with `r_softParticleFadeDistance` controlling the fade width, while decals, beams, electricity, trails, model debris, custom GLSL stages, post-process materials, and non-effect translucent materials remain on their normal renderer path.
- [x] Soft particles now survive the modern visible renderer handoff safely: when `r_softParticles 1` is active, only BSE particle quads with active compatible material stages are replayed through the legacy soft-particle shader after modern scene/depth composition, avoiding the broken-effect failures caused by routing every BSE surface through the particle shader.
- [x] Stock character lighting parity is protected while the new GLSL interaction paths mature: enhanced material interactions and shadow-map receiver sampling now only claim eligible static stock geometry, while MD5/MD5R skinned, deformed, packed, or dynamic character surfaces retain the proven ARB2/stencil interaction path to avoid mirrored body seams and broken eye shading.
- [x] Shadow-map receiver lighting now preserves stock material decoding unless enhanced materials are explicitly enabled: the projected and point shadow interaction shaders use the legacy normal/specular path by default and only apply the newer normal-scale/specular-boost/fresnel math under `r_enhancedMaterials`, restoring character eye and body lighting parity while retaining the opt-in enhanced material path.
- [x] Modern material lighting now uses the same Quake 4 bump-map channel contract as the classic interaction path and carries `r_enhancedMaterials` normal scale, specular boost, and fresnel controls through both uniform and draw-record submission, preventing the modern G-buffer, deferred, and forward+ paths from mis-decoding stock materials when enhanced materials are enabled.
- [x] Modern GL submit readiness now recognizes frame-temporary/generated index-cache blocks as real element-buffer sources: deformed surfaces, packed MD5R cache materialization, packed light interactions, and GLSL-prepared interaction paths now tag temp index allocations correctly, preserving ARB2 behavior while reducing false submit fallbacks and unnecessary CPU-index upload pressure.
- [x] Scene packets now originate from front-end view producers instead of being rebuilt from the backend command stream on normal frames: completed `RenderWorld` views emit packets after culling/subview generation/sorting, GUI fullscreen views emit their own packets, `r_rendererMetrics` reports whether packets came from `frontend` or the backend fallback, and `rendererScenePacketSelfTest` verifies both paths while ARB2 remains the visible renderer.
- [x] Renderer scene packets now describe real pass work instead of multiplying every visible surface across every pass: depth, ambient/material, GUI, post, light-grid, interaction, fog/blend, stencil-shadow, and shadow-map packets are filtered from the same inputs the legacy renderer actually consumes, reducing modern renderer preparation work and keeping lighting/shadow ownership data aligned with ARB2 fallback behavior.
- [x] Modern renderer pass ownership now fails closed on incomplete visual parity: material/geometry fallbacks, interaction lighting, fog/blend, light-grid contribution, and shadow receiver gaps block modern visible replacement before expensive passes are planned, ARB2 lighting/shadows stay visible instead of being dropped, and renderer metrics report the first ownership blocker plus duplicate/drop accounting.
- [x] The renderer graph now models resources instead of only counting passes: ordered packet-pass nodes carry virtual read/write/clear/resolve/present edges for scene color/depth, post targets, imported light-grid data, and the back buffer; resource lifetimes plus transient/imported/aliasable counts appear in `r_rendererMetrics`, `gfxInfo` reports the graph limits, and `rendererRenderGraphSelfTest` verifies world and GUI resource graphs while ARB2 still owns visible rendering.
- [x] The render graph now owns real GL resource handles without changing visible output: `RenderGraphResources` imports the back buffer, legacy scene color/depth, and light-grid data, allocates transient scene/post textures with complete FBOs, tracks lifetimes/alias groups/physical allocation ids, adds invalidate metadata to graph metrics, and exposes `rendererRenderGraphResourceSelfTest` plus `rendererRenderGraphResourceDump` while ARB2 remains the visible renderer.
- [x] The modern renderer now has a stable material resource table without changing visible output: packet material records are expanded into per-frame material ids with material class, blend/sort/alpha-test metadata, texture/sampler descriptors, shader-register ranges, GL 4.3+ descriptor placeholders, disabled bindless fields, explicit fallback reasons, `materialTable(...)` metrics, `rendererMaterialResourceTableSelfTest`, and `rendererMaterialResourceTableDump` while ARB2 remains the visible renderer.
- [x] The modern renderer now carries backend-neutral geometry and instance packet records without changing visible output: scene packets copy vertex/index cache handles, offsets, formats, bounds, skinning/deform/upload classifications, matrices, entity color, shader-register ranges, visibility categories, overflow causes, and sort-key validation into records that modern draw/submit plans can consume while ARB2 remains the visible renderer.
- [x] The modern renderer now has a Shader Library V2 foundation for visible pass bring-up without changing output: capable GL tiers compile controlled depth, shadow-depth, G-buffer, deferred resolve, clustered forward, transparent, GUI, post, and debug GLSL families with compact permutation keys, reflected UBO/SSBO/sampler/image/attribute records, pass/material/tier compile diagnostics, `gfxInfo` coverage, `rendererShaderLibrarySelfTest`, and a fail-closed debug reload gate behind `r_rendererShaderReload` while ARB2 remains the visible renderer.
- [x] The modern renderer now has its first opt-in visible-depth bridge for renderer bring-up: `r_rendererModernVisibleDepth` writes graph-backed scene depth and compatible shadow-depth resources while ARB2 still draws the final color frame, `r_rendererModernDepthDebug` can overlay those depth resources for inspection, unsupported materials and stencil-shadow cases fall back cleanly, and `rendererVisiblePathSelfTest` plus the safe validation matrix cover the path.
- [x] The modern renderer now has an opt-in opaque G-buffer bridge without replacing the visible ARB2 frame: `r_rendererModernOpaque` writes graph-backed albedo, normal, material/specular surrogate, and emissive/light-grid attachments through a GL 3.3 MRT path, `r_rendererModernGBufferDebug` can overlay each attachment for inspection, unsupported materials/geometry fall back cleanly, and `rendererGBufferSelfTest` plus the safe validation matrix cover resource, shader, packing, fallback, and bandwidth reporting.
- [x] The modern renderer now prepares conservative clustered-light data for the hybrid lighting path without changing the visible ARB2 frame: packet-scene `viewLight_t` lists are classified into point, projected, fog, ambient, and special light records, binned into 8x6x16 cluster grids with overflow accounting, uploaded through GL 3.3 UBO fallback buffers, reported through `gfxInfo`/renderer metrics, visualized with `r_rendererClusterDebug`, and covered by `rendererClusterGridSelfTest` plus the safe validation matrix.
- [x] The modern renderer now has its first opt-in deferred-lite resolve bridge: `r_rendererModernDeferred` consumes graph-backed G-buffer/depth resources and clustered-light UBOs, writes a graph-owned `deferredLight` target, accumulates baseline point/projected and light-grid contribution, reports fallback pressure for unsupported fog/special/overflow cases, exposes `r_rendererModernDeferredDebug` overlays, and is covered by `rendererDeferredResolveSelfTest` plus the safe validation matrix while ARB2 remains the visible renderer.
- [x] The modern renderer now has an opt-in clustered forward+ bridge for material classes outside the deferred subset: `r_rendererForwardPlus` routes eligible opaque, alpha-tested, and transparent/fog-blend draws through graph-backed `sceneColor`/`sceneDepth` resources and clustered-light UBOs, preserves transparent sort ordering, reports forward draw, fallback, overdraw, light-read, and GPU-timer metrics through `gfxInfo`/`r_rendererMetrics`, and is covered by `rendererForwardPlusSelfTest` plus the safe validation matrix while ARB2 remains the visible renderer.
- [x] The modern renderer now has its first guarded visible-frame bridge behind `r_rendererModernVisible`: the option requests modern depth, G-buffer, deferred-lite, forward+, and present ownership together, composes `deferredLight` plus `sceneColor` to the back buffer when the frame has no legacy-owned GUI/post/special/subview islands, records owner/resource fallback reasons otherwise, and is covered by `rendererModernVisibleSelfTest` plus the safe validation matrix while ARB2 remains the default visible renderer.
- [x] The modern visible bridge now has Phase 14 compatibility ownership instead of unknown command islands: every render-pass category is assigned a modern owner or explicit compatibility fallback, fullscreen GUI packets get a dedicated modern draw pipeline and overlay replay after composition, light-grid passes are modern-owned by the hybrid path, post/copy/subview/render-demo/BSE categories report graph and fallback buckets, render-demo fallback remains deterministic, and `rendererModernCompatibilitySelfTest` plus the safe validation matrix cover the inventory.
- [x] Modern visible rendering now preserves retail feedback and subview effects without forcing a full-frame fallback: heat-haze/current-render materials draw through a narrow legacy feedback lane over the composed modern scene, `_currentRender` is refreshed at the right moment for fire and distortion effects, and generated subview/render-demo views keep their legacy sidecar passes while the main scene stays on the modern handoff.
- [x] Modern renderer shadow-map gating now keeps deferred-lite and forward+ diagnostic lighting alive when shadow-map receiver sampling is unavailable, records those blocked receivers separately from real stencil fallbacks, ignores intentional no-shadow/no-receiver skips for ownership, and keeps full visible replacement fail-closed so legacy shadows remain visible until modern receiver sampling reaches parity.
- [x] Optimized prelight shadows now sanitize legacy invalid prelight-model sentinels before view shadow submission, preventing airdefense-style access violations while falling back to dynamic/static shadow handling when no real precomputed model exists.
- [x] The modern renderer now has Phase 15 compatibility gates before any default-promotion decision: `gfxInfo` reports driver quirks and fallback gates, startup masks known-bad driver features before tier selection, `rendererCompatibilityGatesSelfTest` covers missing UBO, broken MRT, missing timer query, missing buffer storage, rejected debug context, and synthetic quirk downgrades, and the validation matrix now includes deterministic capture, RenderDoc tier, long-run, SP, and MP sign-off checklists.
- [x] The modern renderer now has a Phase 16 benchmark and scalability contract: renderer metrics feed `rendererBenchmarkCapture` with CPU front-end/visibility/packet/graph/submit/present timings, GPU pass samples, upload/draw/light/cluster/fallback counts, rolling P50/P95/P99 frame-time percentiles, low/baseline/modern/high-end budget presets, threshold override cvars, and an opt-in adaptive cluster-grid experiment, with `rendererBenchmarkSelfTest` plus validation-report thresholds covering the capture format before performance claims are made.
- [x] The modern renderer now has a Phase 17 default-promotion gate instead of an unconditional renderer flip: `r_rendererModernAutoPromote` remains off by default, `gfxInfo` reports the promotion reason, `rendererDefaultPromotionSelfTest` covers unsigned eligibility, signed promotion, explicit `r_renderer arb2` escape, forced `r_glTier legacy`, compatibility-gate blocks, and missing ARB2 rollback, and the validation docs now require SP/MP gameplay, RenderDoc, benchmark, and rollback sign-off before auto promotion is used.
- [x] The renderer recovery plan now has a conservative-default safety gate for Phase 13: `gfxInfo` reports `Renderer default safety`, `rendererDefaultSafetySelfTest` verifies that ARB2 remains the visible clean-start or explicit rollback path with modern executor/submit/visible/debug/validation/bindless/shader-reload side paths off by default, and the README plus renderer docs list the rollback and quarantine cvars for users testing modern renderer bring-up.
- [x] The OpenGL tier ladder now reports an explicit workload contract for every selected tier: `gfxInfo` shows selected/requested readiness, forced-tier degradation, fail-closed fallback state, ARB2 rollback availability, GL3.3 CPU workload readiness, GL4.3 GPU-driven readiness, GL4.5 low-overhead readiness, GL4.6 top-tier readiness, and concise missing reasons, with `rendererTierContractSelfTest` and the safe validation matrix covering the contract.
- [x] The GL 4.3 GPU-driven path now performs real compute work instead of only validating resource creation: submit records are uploaded to SSBOs, compute culls visible/scissor-rejected commands, validates clustered-bin counts against a CPU reference, generates compact draw-indirect commands for a compatible indexed batch, can execute the range through masked multi-draw indirect under `r_rendererGpuValidation`, and reports dispatch/readback/mismatch/indirect-submit metrics through `gfxInfo`, `r_rendererMetrics`, and `rendererGpuDrivenSelfTest`.
- [x] The modern renderer recovery path now keeps blocked visible-frame requests cheap: compatibility ownership is analyzed before expensive modern passes run, blocked frames stay analyze-only unless an explicit sidecar/debug request is active, shadow-map diagnostics use the same producer gate, `modernPassGate` explains skipped work, and renderer debug/status strings no longer flood validation logs with `idStr::snPrintf` overflow warnings.
- [x] Modern clustered lighting now carries real point/projected light data instead of placeholder vectors: per-light descriptors include origin, radius, evaluated color, falloff/projection planes, image/sampler handles, depth/scissor policy, and shadow fallback state; deferred and forward+ lookup clusters in view-depth space, use real attenuation/specular math, upload all per-view grids through UBO/SSBO paths with DSA/multi-bind support, and fail closed when cluster spill or capacity limits would otherwise drop light contribution.
- [x] The modern renderer now has a fail-closed pass-ownership handoff for opt-in visible replacement: modern-owned depth, lighting, light-grid, ambient/material, fog, GUI, and shadow/stencil responsibilities can suppress their exact ARB2 counterparts only after modern resources and fallbacks pass validation, while mixed diagnostics keep ARB2 active and `rendererPassOwnershipSelfTest` verifies duplicate-ownership prevention.
- [x] Modern renderer geometry correctness advanced: promoted modern draws now bind the full `idDrawVert` layout, carry per-draw model-view matrices into G-buffer/forward+ shaders, reconstruct view-space normals and tangent handedness for mirrored UVs/transforms, validate the layout through renderer self-tests, and keep deformed/GPU-palette-skinned surfaces on the legacy path until their contracts are safe.
- [x] Modern renderer material correctness advanced: promoted G-buffer and forward+ draws now use the material table's stage contract, legacy alpha-test thresholds, normal/specular/emissive textures, and conservative visible/shadow fallback gates instead of placeholder normals and first-texture-only sampling.
- [x] Modern renderer material/geometry promotion is now stricter and closer to retail behavior: unsupported material features and geometry producers fall into stable fallback buckets, material cull/two-sided state, mirrored or negative transforms, material color registers, transparent blend families, and weapon/model depth hacks are carried into modern submissions, and the opaque G-buffer path preserves reusable scene depth instead of clearing away a valid prepass.
- [x] Modern clustered lighting now scales beyond the first tiny prototype: cluster grids are budgeted from viewport/light pressure, GL 3.3 keeps a UBO-safe CPU-binned path, GL 4.3+ can upload larger SSBO-backed light/index lists, and dense clusters report spill pressure instead of turning the fifth light into a hard overflow.
- [x] Modern shadow mapping is now part of the shared renderer plan instead of a separate side path: the renderer builds a per-frame shadow plan from visible lights, records mapped/stencil-fallback/skipped decisions, budgets shadow candidates against the active renderer preset, names projected/point/cascade/translucent shadow resources in the render graph, and feeds shadow descriptor state into clustered light records for deferred and forward+ follow-up while preserving the existing `r_shadows`/`r_useShadowMap` fallback contract.
- [x] Shadow-map validation and modern receiver safety advanced again: mapped shadow descriptors now carry atlas tile, matrix, cascade, bias/filter, scissor, and readiness metadata, clustered deferred/forward+ fail closed to stencil fallback when modern receiver sampling is unavailable, and new debug modes can hard-disable receiver bias, PCF, caster polygon offset, or receiver-plane/normal bias during gameplay.
- [x] Shadow-map regression coverage now has a compact gameplay profile for projected, point, cascaded, character, and alpha-tested captures, with screenshot hashes or optional TGA reference comparisons recorded in reports and safe planner self-tests checking the same coverage tokens before release.
- [x] Modern deferred/forward+ shadow handoff now uploads real per-light shadow descriptor buffers with map type, compare/bias model, atlas tile rectangles, cascade split/bias data, and fallback/readiness flags; diagnostics and self-tests report descriptor upload readiness while visible modern shadow ownership stays fail-closed whenever required sampler resources are missing.
- [x] Modern deferred/forward+ shadow setup now binds the actual ARB2-owned projected and point shadow atlases plus translucent moment textures on fixed modern sampler units, with diagnostics separating binding-contract readiness from real texture availability for the modern lookup shaders.
- [x] Shadow-map lighting no longer throws away an entire mapped light when one receiver surface is incompatible with the GLSL receiver path: eligible static receivers keep mapped shadows while skinned, generated, custom, or dynamic receivers use the stencil fallback for that light.
- [x] Gameplay-authored flashlights, weapon muzzle flashes, vehicle lights, and Strogg hover headlights now validate their projected-light vectors before registration, preserving valid stock cones while safely demoting malformed projector definitions to point lights instead of feeding broken frustums into shadow mapping.
- [x] The modern hybrid visible path now has a graph-owned composition handoff: alpha-tested depth and shadow-depth draws can use material alpha thresholds safely, CPU-skinned depth is accepted while unsafe skinning/deforms remain legacy-owned, deferred and forward+ shaders consume the shared shadow descriptor policy, unresolved stencil/shadow fallbacks block visible replacement fail-closed, and `hybridSceneColor` receives the combined deferred/forward output before the final back-buffer copy.
- [x] The modern hybrid visible path now feeds the existing HDR and post stack instead of bypassing it: supported modern tiers allocate FP16 scene/post composition targets, the combined deferred/forward scene is copied into the active HDR scene target before SSAO, motion blur, bloom, tone mapping, and authored `_currentRender` passes, graph depth is handed to depth-aware post effects, auto exposure is gated to completed modern handoff frames, and GUI replay remains a final LDR overlay.
- [x] The modern renderer now has a stronger conservative visibility and occlusion foundation for detailed-scene performance: submit commands cache projected bounds/depth/safety visibility packets, `r_rendererOcclusion` and `r_rendererHiZ` gate scissor/frustum/screen rejection and Hi-Z candidate accounting, graph-owned Hi-Z pyramids build through an explicit max-depth reduction path without in-frame GL state queries, and shadow-caster safety metrics preserve Quake 4 area/portal/PVS culling as the first tier.
- [x] The modern renderer now selects only the passes a frame actually needs: G-buffer, deferred, and forward+ work are derived from ready submit commands and explicit debug requests, empty passes are skipped, single-source modern visible composition remains valid, repeated G-buffer/forward+ FBO layouts are cached, and `gfxInfo` reports pipeline stability, batching, validation-readback policy, and GL 3.3/4.3/4.5 fit.
- [x] Modern renderer draw submission now avoids rebuilding the fixed `idDrawVert` vertex-input layout for every draw: GL 4.3+ tiers keep the attribute format on the modern VAO and switch only the vertex source binding, while the GL 3.3 path caches repeated VBO/stride/base-offset layouts; `gfxInfo` and renderer self-tests report the vertex-input cache path.
- [x] Modern renderer submit plans now bucket reorder-safe world opaque/depth work before execution: same-view depth, G-buffer, and forward+ opaque commands are stably sorted by pipeline/program/material/geometry/scissor state while transparent, GUI, post, viewmodel, subview, and shadow work keeps source ordering; `gfxInfo`, renderer metrics, and self-tests report eligible draws, moved draws, bucket counts, and saved state batches.
- [x] The GL 4.3 GPU-driven submit path now feeds multi-draw indirect from a real per-draw record stream instead of one first-seen signature: draw records carry MVP/model-view matrices, material ids, local parameters, alpha refs, and material flags in an SSBO, an instanced record-index attribute selects the right record for each indirect command, and the executor emits one `glMultiDrawElementsIndirect` call per compatible bucket while `gfxInfo`, renderer metrics, and GPU-driven self-tests report draw-record bytes, bucket counts, bucketed commands, and indirect fallbacks.
- [x] The GL 4.3 GPU-driven submit path now consumes the graph-owned Hi-Z pyramid for conservative indirect culling: modern-submit frames build a depth prepass when Hi-Z culling is enabled, compute tests eligible opaque/material draw bounds against the max-depth pyramid, compacts visible indirect commands per bucket with zero-count padding for GL 4.3-safe MDI submission, and reports bucket-buffer readiness, compacted commands, Hi-Z candidates, and validation-only rejection counts in `gfxInfo` and renderer metrics.
- [x] Conservative BSE visibility culling now avoids cached-bounds pop-ins and queued-surface flicker by default: offscreen loop service deferral stays limited to portal/PVS area culling, stale portal-culled loops get budgeted state/bounds refreshes through `bse_culledLoopServiceMaxMsec` and `bse_culledLoopServiceRefreshBudget`, frustum rejection runs after the effect model is rebuilt from current geometry, queued BSE surfaces submit frame-local vertex/index snapshots so later view rebuilds cannot clear their caches, and the older cached-bounds frustum path remains available behind `bse_useCachedFrustumCull` for opt-in profiling.
- [x] Modern clustered lighting now uses compact CSR-style cluster lists instead of fixed per-cluster slots: each per-view grid uploads `(offset,count)` headers plus a flat packed light-index stream, dense clusters stay lossless within the shared budget on GL 3.3, and GL 4.3+ can rebuild the same headers/index stream with compute count/prefix/fill passes while `gfxInfo`, renderer metrics, and `rendererClusterGridSelfTest` report CSR and compute-binning readiness.
- [x] GL 4.3+ modern material draws can now collapse repeated classic texture binds through a conservative per-frame sampler table: the material resource table assigns table indices for loaded textures and fallback images, shaders keep the GL 3.3 classic sampler path but can select table entries with per-draw texture indices, and `gfxInfo`, renderer metrics, and low-overhead self-tests report table size, draw usage, uniform updates, and fallback pressure while bindless remains opt-in only.
- [x] Modern renderer pass handoff now avoids full GL teardown between modern passes: depth, Hi-Z, G-buffer, deferred, forward+, GUI, and diagnostic submits use a soft intra-modern restore that keeps the state cache warm, then perform one full restore before the ARB2 legacy handoff, with `gfxInfo`, renderer metrics, and visible-path self-tests reporting soft/full restore coverage.
- [x] The idTech 5 modernization path now has its first complete shared classic-frame domain: eligible fixed-function root 2D GUI views preserve repeated ordered material stages, evaluate conditions/colors/matrices/state once per draw, resolve generation-checked image resources, and run from the same sealed records on OpenGL and Vulkan. `r_rendererSharedGui` remains default-off, and any unsupported view, material, resource, or backend condition executes the untouched whole-view classic path with explicit coverage and fallback diagnostics.
- [x] The idTech 5 modernization path now has a second complete shared classic-frame implementation for eligible ambient-only 3D world views: packet-derived fixed-function surface passes preserve repeated evaluated state, opaque/perforated surfaces require matching established depth work, and both backends preflight the whole pre-fog/post-fog plan before committing. `r_rendererSharedWorldAmbient` remains default-off; any interaction, shadow, fog/blend, special-surface, material, resource, or backend blocker leaves the complete ambient view on its untouched classic walker. Local stock `maps/tools/mv2` GL/Vulkan option-off/on engine captures match exactly with one owned pre-fog draw, and a stock deform override proves named zero-draw whole-view fallback on both backends; clean-package/platform promotion evidence remains open.
- [x] The idTech 5 modernization path established its third complete shared classic-frame implementation with eligible unshadowed fixed-classic interaction views: scene packets identify the exact source light and local/global/translucent receiver, one bounded transaction seals the complete light-stage x bump/diffuse/specular stream, and both backends preflight every program/resource/geometry/transient requirement before ownership. `r_rendererSharedWorldInteraction` remains default-off; custom, deform/skinned, special-view, resource, overflow, or backend blockers perform zero shared draws and leave the complete interaction view on its untouched classic walker. Native/static coverage and controlled stock GL/Vulkan unshadowed qualification pass with nonempty ownership and exact option-off/on engine images; clean-package/platform promotion evidence remains open.
- [x] The third complete shared classic-frame domain now includes shadow-coupled interaction views. The same default-off interaction transaction seals classic stencil volumes, projected single-map and CSM/parallel shadows, point cubes, mixed mapped/stencil lights, same-light hybrid supplements, dynamic mapped casters, and perforated alpha stages for both OpenGL and Vulkan. Each backend retains the complete cache/update/scratch and receiver plan before visible ownership, reconciles physical volume/map/hybrid work exactly, and commits no shared main-target draw on an admission/resource/backend failure. Translucent moment-map casters remain an intentional shadow-specific whole-view fallback. Native/static contracts pass, and the controlled five-case engine-screenshot profile passed 5/5 on both GL and Vulkan with same-backend classic parity, material shadow deltas, and atomic fallback. Stock fixed-camera/reference, clean final-package, and platform/driver qualification remain open.
- [x] The idTech 5 modernization path now has a fourth complete shared classic-frame implementation for eligible fog/blend phases. `r_rendererSharedWorldFogBlend` remains default-off; one bounded transaction seals every fog/blend light, GLOBAL-to-LOCAL receiver, ordered active/inactive blend stage, fog receiver and frustum cap, evaluated texgen/compositing state, image resource, no-op, and coverage identity for both OpenGL and Vulkan. Each backend preflights the whole phase before the first shared main-target draw, and any unsupported source, view, geometry, material, resource, target, capacity, or backend condition keeps the complete phase on its untouched classic walker. Native/static validation passes, and the controlled stock-declaration GL/Vulkan profile now has exact shared/classic engine-image parity, nonempty reconciled ownership, material fog/blend deltas, and named zero-commit atomic fallback. Authored-stock fog and clean-package/platform promotion remain open.
- [x] The Milestone D material-deform dependency is implemented behind default-off `r_rendererSharedDeform`: the authoritative CPU material-deform result now carries sealed source/result geometry, evaluated input, frame/cache provenance, consumer role, explicit completed/intentional-empty/not-applicable/skipped/failed/unsupported outcome, and a stable semantic hash. Root GUI, world ambient, mapped interaction casters, and fog/interaction receivers validate the correct classic role before either backend owns their complete transaction. Particle/particle2 and any skipped, failed, stale, or mismatched record remain named atomic fallback; OpenGL and Vulkan consume only the ordinary final vertex/index stream.
- [x] The special-subview Milestone D transaction is implemented behind default-off `r_rendererSharedSubview`: direct mirrors seal camera/clip/scissor semantics, while eligible remote-camera, mirror, reflection, refraction, and x-ray child scenes seal their parent surface and exact 2D/cubemap color/depth capture. Nested chains retain exact parent/root/depth and depth-first order. Eligible authored-post/video/current-render/depth work inside a child must complete before root publication; either domain's rejection rolls the whole tree back, and later members bypass duplicate fallback reporting. Native/static validation and the narrow nested cinematic-plus-`_currentRender` mirror acceptance pass on Windows GL/Vulkan; direct, multi-level, other capture kind/aspect/face, `_currentDepth`, clean-package, retained-review, and platform/driver qualification remain open.
- [x] In-world GUI ownership now has its own default-off `r_rendererSharedInWorldGui` corridor: only quads emitted by `R_RenderGuiSurf` are provenance-tagged, packetized as a world-category GUI subset, and evaluated through the existing ordered material records with the classic 3D depth contract. OpenGL and Vulkan preflight every tagged surface before submitting at the pre-ambient boundary; any source, packet, material, resource, target, capacity, or backend rejection keeps the entire tagged subset on the untouched classic walker. Root 2D GUI remains independently controlled by `r_rendererSharedGui`; runtime screenshot, clean-package, and platform qualification remain open.
- [x] A default-off shared cinematic/authored-post transaction now seals an eligible complete root `videoMap`/`soundMap` view or ordered post-process tail behind `r_rendererSharedCinematicPost`. It reconciles source/packet order, cinematic clock, and `_currentRender`/`_currentDepth` use before OpenGL and Vulkan dispatch their mature dynamic-stage executors. Nested tails bind to the exact special-view root/depth, remain unpublished until that tree completes, and propagate fallback atomically in either direction. Native/static validation and the narrow nested cinematic-plus-`_currentRender` acceptance pass on Windows GL/Vulkan; root views, ordinary-root post, `_currentDepth`, clean-package, retained-review, and platform/driver qualification remain open.
- [x] Final controlled Milestone D nested acceptance is retained at `.tmp/renderer-milestone-d/acceptance-20260823-r6-all/renderer_milestone_d_acceptance_report.json` (SHA-256 `b49031c48267fbfa86c295082707525a390d6090c24f600d00644237a67b252a`), bound to `.tmp/renderer-milestone-d/qualification-20260823-r6/fixture_manifest.json` (SHA-256 `24a2a4926f4b1263e311ad762bf40d391b33de225732d12154ebd7625705282d`) and `.tmp/stock-runtime/milestone-d-qualification-20260823-r6` (manifest SHA-256 `dbece44597ec5e0686eed215f6ae99c5d6ba4cdd959c1efa7c4acaf1bc2ca673`). The unchanged 3,884-file / 7,281,560,043-byte stock dependency seal is `13abad18f70eb8b4bf6ea0e9697b317718bb065ee76f86070787005b045dda7a`; the unchanged eight-file / 25,330,583-byte fixture evidence inventory seal is `0572fb8d9c132f7e97060345464f97750e59e69819871fce90eed13d1348d8ae`. The separate passing R6 foundation JSON has SHA-256 `0659f1f2ee5108a422cd6c41bdc78809731f25ad7eda4b5ee84328a9163ce7ac`, with Markdown SHA-256 `a619169ee97f2d6e8c7dfab991adbab5b5b282fd2e1197e2d59eb3609c87cddf`. All 12 sequential GL/Vulkan cases passed using the fixed input-disabled capture script, actual windowed mode `-1` at 1280x720, and 1280x720 engine TGAs. All four exact comparisons per API had RMS `0`, maximum delta `0`, and zero differing channels; active normal cinematic owned/fallback/mismatch/duplicate was `1/0/0/0` and subview owned/fallback was `1/0`, skip was `0/1/0/0` and `0/1`, inactive-backend counters were zero, and no runtime, fixture, or stock verification failed. Normal versus skip changed 46,359 channels at RMS `2.0073` / maximum `72` on GL and 3,329 channels at RMS `0.2506` / maximum `18` on Vulkan. This closes only the controlled nested cinematic-plus-`_currentRender` mirror gate.
- [x] A default-off shared render-demo/Raven special-frame transaction now seals an eligible complete active-session render-demo view or exact `RC_DRAW_SPECIAL_EFFECTS` blur/AL mask behind `r_rendererSharedSpecialFrame`. Session/render-world provenance avoids confusing portal-sky negative view IDs with demo playback; OpenGL and Vulkan retain their mature full-view, resource, resolve, blur, and projected-light executors, reporting ownership only after all sealed coverage completes. Incomplete source, session, effect, resolve, or backend state retains the complete classic item; native/static validation passes while engine-screenshot, clean-package, and platform qualification remain open.
- [x] Milestone D scoped implementation is complete: every named classic-frame corridor has a default-off sealed GL/Vulkan owner, explicit classic rollback, diagnostics, static/native evidence, and a documented release-promotion boundary. This does not promote the aggregate modern-visible renderer or close clean-package, retained-review, authored-stock, target-platform, or driver qualification.
- [x] Modern renderer transient uploads now use the shared upload-manager stream for frame constants and GL 4.3 GPU-driven scene, indirect, draw-record, bucket, and validation buffers when available; fixed-buffer fallbacks remain intact, upload ring rotation is configurable through `r_rendererUploadFrameBuffers`, opt-in GPU validation readbacks are deferred behind nonblocking sync polling, and launch-time validation now waits for real scene draw packets instead of doing GPU-driven work on loading/GUI frames.
- [x] Forced modern GL tiers now keep stock-world lighting on the visible ARB2 bridge by preferring versioned compatibility contexts before core-profile diagnostics; ARB2 initialization also rejects core-profile contexts explicitly instead of advertising a bridge that can only produce a black frame.
- [x] Modern renderer auto-promotion now requires explicit Phase 8 validation evidence, not just a boolean sign-off: `r_rendererPromotionEvidence` must carry the complete zero-warning visual/gameplay/RenderDoc/performance/presentation/rollback/debug-off token before `r_rendererModernAutoPromote 1` can activate, and `gfxInfo`, `rendererDefaultPromotionSelfTest`, `rendererDefaultSafetySelfTest`, and the validation matrix all report the same evidence gate.
- [x] Filter-style decal mips now fold coverage alpha into the neutral multiply color per generated level, so stock terrain stains such as `game/airdefense1` no longer reveal their decal bounds when the flashlight crosses them.
- [x] Decal surfaces retain their established sorted pre-fog material position after direct interactions and before baked light-grid and fog work. When a sealed clustered-decal transaction owns a surface, its modern forward+ draw substitutes one-for-one for that exact classic draw occurrence; transaction rejection leaves the classic draw unchanged, and `r_skipDecals` suppresses both ordinary and clustered-decal submissions.
- [x] Runtime logs now open under the selected game directory as soon as filesystem startup can write files, so `+set fs_game baseoq4 +set logFileName logs/openq4.log` writes to `fs_savepath/baseoq4/logs/` instead of being captured early under `q4base/logs/`.
- [x] Retail BSE trail-material parity is restored for shared bullet-impact effects: particle templates now retain the authored/default trail-material name, motion trails resolve that material at finish time instead of relying on an early static pointer, and the stock machinegun/shotgun impact spark trails once again use Quake 4's `gfx/effects/particles_shapes/motionblur` trail instead of falling back to the engine default texture.
- [x] Ragdoll startup now follows Quake 4's retail articulated-figure path again: stock corpses no longer pick up openQ4-only AF handoff velocity/depenetration heuristics or extra per-body angular-velocity injection during pose transfer, and the AF contact solver is back on retail's tighter contact budget and per-pair planar-contact cap for SP/MP ragdolls.
- [x] Animated MD5 reference bounds now use exact posed mesh extents when available, eliminating the fixed-screen-square scissor clipping that could chop up player heads/bodies and attached weapons in stock scenes such as the normal `game/airdefense2` launch path.
- [x] VS Code launch-task runtime parity is fixed on Windows: the client and dedicated entry path now preserves native Windows argv quoting, startup `+set` handling now preserves the full multi-word value tail for debugger/task launches that fail to quote space-containing arguments cleanly, and projected shadow-map defaults now carry enough receiver bias to stop stock flashlight scenes such as `game/airdefense2` from collapsing into near-black self-shadowing.
- [x] Decl reload progress now uses a shared retail-style tool print path: `Common.*` centralizes tool-routed print behavior, `reloadDecls` progress lines now use that helper instead of printing straight to the runtime console, and the decl browser can surface reload progress in its status bar while normal runtime sessions still fall back to the usual console/log output.
- [x] Decl late-load warning suppression now uses one shared retail-like any-tool helper: `Common.*` centralizes the "is any tool active?" check alongside the existing masked tool helpers, and decl late-load warnings now read through that path instead of open-coding focus and editor-bit state inside the decl manager.
- [x] Decl tool-state checks now share one retail-style helper: masked tool-flag queries live in `Common.*`, entityDef media caching uses that shared helper instead of a raw editor-bit test, and decl-validation state checks now read through the same path so tool-sensitive decl behavior stays centralized.
- [x] Articulated-figure `fastEval` text now matches retail style: the parser still accepts explicit boolean forms for tools, bare `fastEval` behaves like retail anywhere in the settings block, rebuilt AF text only emits `fastEval` when true, and default AF definitions stop writing `fastEval 0`.
- [x] Packed `.decls` writer policy is now explicit: openQ4 keeps its extended game-section export coverage by default, exact-retail game-type coverage is selectable through `com_singleDeclFileWriteMode` or `writeDeclFile retail`, and export logs now report which policy produced the file.
- [x] Effect decl allocation is now a hard integrated-BSE invariant: startup probes the `effect` allocator before registration, allocator failures fatal instead of producing plain `idDecl` stubs, and dedicated servers keep the disabled BSE runtime while still using the real BSE effect-decl parser.
- [x] Playback decl finalization now fires the retail-style playback-finished tool bridge after resampling recorded paths, giving editor/tool workflows a completion signal while remaining a no-op in normal runtime builds.
- [x] Articulated figure decl content masks now preserve Quake 4's `vehicleclip`, `flyclip`, and `itemclip` collision bits when parsing or resaving AF files, restoring retail collision-mask round-tripping for stock and SDK-derived articulated figures.
- [x] Light-grid baking now writes versioned v5 metadata with a deterministic bake-settings/layout hash, records probe and atlas layout stats, prints per-phase timing/counter summaries including async readback stalls, and rebuilds stale baked outputs automatically when the map layout or bake-affecting settings change.
- [x] Light-grid interpolation is now visibility-aware: bakes emit companion `area*_lightgrid_vis.tga` atlases with directional distance moments, and runtime sampling uses those moments with a self-shadow bias to reduce wall/corner light leaks.
- [x] Light-grid probe placement now detects solid/near-solid positions, relocates probes within a bounded search when possible, records richer validity states and relocated origins in v7 metadata, emits compact `area*_lightgrid_pos.tga` probe-position atlases for runtime interpolation, and reports relocated/near-solid probe counts in bake diagnostics.
- [x] First-person weapon/viewmodel surfaces now receive baked light-grid indirect diffuse through a dedicated weapon pass that samples the active view area's probes while keeping depth-hacked weapon geometry out of the main world receiver pass.
- [x] Light-grid area transitions now blend across visible, unblocked portal neighbors near doorway/window boundaries, using `r_lightGridPortalBlend` to smooth indirect-light seams without changing baked asset formats.
- [x] Light-grid atlas residency now keeps visible areas and unblocked portal neighbors hot while loose fallback atlases can still use an age-based window controlled by `r_lightGridResidencyFrames`, reducing reload churn and traversal hitches when moving through connected areas.
- [x] Light-grid indirect rendering now uses a material-compiled representative diffuse stage instead of redrawing every diffuse stage, reducing extra geometry submissions for multi-diffuse materials while retaining a fallback when the preferred stage is conditionally disabled.
- [x] Startup asset validation ignores retail Quake 4 game-binary PK4 archives (`game000.pk4` through `game300.pk4`, plus `gamex*.pk4` variants), so openQ4 only requires and verifies the stock media PK4s it actually uses. Pure multiplayer retains the official `game300.pk4` checksum solely as the protocol 2.41 compatibility token for trusted local openQ4 modules; the archive itself remains optional and is never loaded as game code.
- [x] Retail decl loading parity improved: Quake 4 decl folders now honor retail recursion/unique-file behavior, no-cache decl lookups reach entityDef inheritance/media caching, legacy Doom 3 PDA/email/video/audio console commands are no longer exposed as supported decls, and playback/table/lip-sync/material-type decl parsing plus playback runtime sampling/record hooks now handle more shipped Raven asset data safely.
- [x] Packed decl-manager parity advanced: the retail single-decl-file API is enabled again across engine/game headers, packed `.decls` sections can be read and written through controlled cvars/commands, stored decl indices are preserved during load, validation/allocation/tool hooks are available, packed stub decls expand lazily from their original source text, PDA/email/video/audio decl families are included in the framework packed section, and the startup/session/network asset-log wiring now selects and refreshes per-map packed decl files when the retail cvars are enabled.
- [x] Packed decl asset-log wiring now follows the retail `assetlogs/<map>` naming contract, including entity-filter suffixes, while the decl manager preserves the real `maps/...` path when selecting per-map `.decls` files.
- [x] Regenerated collision caches now keep the first map entity named as the world model even after openQ4's trace-model slots are initialized, preventing MP map loads from null-dereferencing the clip world when a stale stock `.cm` has to be rebuilt.
- [x] Material decl parity advanced: per-level material use counts now resolve onto image diagnostics like retail, Quake 4 material tokens for `alphaFunc`, `fragmentParm`, `sky`, and `needCurrentRender` now preserve their authored behavior instead of being ignored or defaulting to Doom 3-only semantics, `sort gui` and retail sort constants now resolve consistently, and unknown `materialType` references now warn like the shipped engine.
- [x] Sound shader decl parity improved: descriptions, min/max `frequencyshift`, `frequentlyUsed`, `no_shakes`, `minSamples`, and +10 dB `volumeDb` clamping now survive parsing, sound shader memory/list diagnostics include their retained decl state, level-load reference marking now propagates from sound decls to their loaded samples, retail entry-only default/duration queries are restored, and the retail `rvSoundShaderEdit` bridge now exposes sample/shake/purge/load helpers for tools.
- [x] Effect/player-model/skin decl parity advanced: skin and player-model decl diagnostics now report retained allocations and useful player-model fields, BSE effect decls now expose retail-style segment lookup/copy/editor-original helpers with safe bounds handling, and the integrated BSE path now wires the retail `rvDeclEffectEdit` bridge instead of leaving it null.
- [x] Decl editor/tool wiring advanced: AF, playback, and lip-sync decl edit adapters are now available engine-side, lip-sync decls can rebuild retail-style text, Raven decl parsers now expose the no-caching signatures expected by the shared SDK headers, player-model parsing performs the retail media-cache pass, and table/material-type parsing now follows recovered retail edge-case behavior.
- [x] Retail decl validation parity advanced: table, skin, entityDef, AF, material-type, lip-sync, playback, and player-model validation now performs the retail temporary-decl parse/default-state check instead of accepting every text block, and the base `idDecl` no-caching parse shim now preserves the caller's cache-suppression request.
- [x] Added a default-off `r_motionBlur` graphics option for subtle camera motion blur, including depth-aware sampling, safe history resets on map/view changes, and a debug vector view for tuning.
- [x] Added optional rigid-object motion vectors behind `r_motionBlurObjectVectors`, improving blur on moving doors, platforms, and other non-skinned opaque objects while preserving the camera-only fallback for unsupported surfaces.
- [x] Packed MD5R light interactions now advance along the retail ARB2 path without regressing the stable hybrid renderer: packed interaction passes switch to the stock `md5rinteraction*` vertex-program family, upload the retail MD5R interaction parameter block in `program.env[75..91]`, interaction `lightTris` now emit the retail per-prim-batch headered index stream instead of staying on the older flat filtered list when the packed builder succeeds, packed interaction rendering consumes that headered batch stream directly while continuing to fall back to the classic interaction path if a packed program/buffer bind or retail packed light-tri build is unavailable, retail-style packed stage texturing now picks the correct MD5R program variant and submits full ambient/material stage batches directly from the packed draw buffers when the stage path is fully prepared, ARB ambient material-program stages with an authored `md5r*.vp` alias now use the same prepared packed draw/runtime path instead of forcing the classic ambient-cache vertex layout, packed late blend-light passes now explicitly refresh the frame-temp ambient/index view they need instead of depending on earlier light submission to have populated it, the deferred light-grid indirect pass now does the same so packed surfaces stop assuming a prior ambient submission already materialized classic cache-backed draw data, the shared depth / ambient shader passes now run through the same packed cache-prep helper before they touch classic `ambientCache` state, the generic `RB_RenderTriangleSurface` / `RB_T_RenderTriangleSurface` path now gives packed surfaces the same frame-temp classic draw view instead of dropping them straight to immediate-mode fallback, the last-resort `RB_DrawElementsImmediate` path can now materialize packed prim-batch triangles on demand instead of assuming classic `verts/indexes` residency, the GLSL material/projected-shadow/point-shadow interaction creators now recover packed ambient-cache state from the source ambient surface instead of assuming earlier interaction submission already populated classic draw-vertex data, the draw core now recognizes a second prepared packed direct-draw context so the MD5R depth and fog passes can route their per-prim-batch submission through `RB_DrawElementsWithCounters()` instead of each carrying a separate private loop, those ARB2 packed depth/fog entry points now attempt direct MD5R submission before falling back to the older classic-cache requirement, and renderer diagnostics such as `r_showLightCount`, `r_showNormals`, `r_showEdges`, and the other raw-geometry overlays now rematerialize packed interaction/ambient data instead of silently skipping MD5R-backed surfaces.
- [x] Fixed the active light-parity fog regression in `game/storage1` and other stock scenes: fog passes now force the fog light's current color instead of inheriting stale per-vertex color arrays, the generated `_fog` / `_fogEnter` lookup textures once again stay full RGBA instead of collapsing to alpha-only classification, and packed MD5R fog again routes through the retail `md5rbasicfog*` variant/palette path for skinned packed surfaces.
- [x] Storage 1's main shaft beam now follows retail fog/material ordering: opaque, decal, and `sort far` materials draw before fog while medium/near translucent BSE beam layers draw afterward, restoring the bright white core and blue additive shaft instead of letting fog wash over the blended beam. HDR tonemapping now preserves stock LDR-authored highlights until the near-white shoulder, keeping additive beam/fx colors vivid while still compressing true scene-referred HDR energy. DXT-compressed GUI/image strips with heights smaller than one 4x4 block also decode through a block-aligned scratch buffer, preventing debug-build startup crashes while loading stock DDS replacements.
- [x] Collision-model parity improved for generated/runtime CM data: retail Quake 4 primitive tracking and polygon texture-basis data are now preserved during CM generation/serialization, render-model collision no longer over-merges across distinct source surfaces, and `.proc` CRC validation now matches retail CM load behavior more closely.
- [x] Collision-model load parity improved for stock maps and SP entities: CM parsing now reuses existing model slots instead of exhausting submodel capacity, world/map submodel naming matches retail expectations more closely, retail-style SP clipmodel fallback no longer probes entity names, and stock `game/medlabs` map init no longer emits `contains different model`/`no free slots` CM failures.
- [x] Collision-model lifetime parity now more closely matches retail Quake 4: map teardown drops CM reference counts instead of clearing the entire manager, purges free model geometry in place for slot reuse, and SP/MP map shutdown now purges unreferenced CM data before the next level load, fixing slot exhaustion during SP transitions such as `game/mcc_1` to `game/convoy1`.
- [x] MCC med-bed intro overlay now keeps its retail 128x128 ambient data/static layers tiled at native scale across the expanded GUI canvas, and edge-hugging detail widgets anchor to the appropriate wide/tall screen sides.
- [x] Stock split-map transitions now preserve Raven entity filters such as `game/storage1 first` / `second` across map commands, autosaves, savegame restore, and loading screens, and bare launches such as `map game/storage1` or `map game/process1` now default to the first filtered visit when the retail mapDefs only provide `first`/`second` variants. This restores the filtered map state, `mapDef` title, levelshot, autosave name, and load-size metadata instead of falling back to the raw map path.
- [x] Split-map transition validation now has a named campaign profile that triggers the real end-level targets from `game/mcc_2` through `game/storage1 first`, `game/storage2`, `game/storage1 second`, and into `game/tram1`, asserting the active map/filter after each load so first/second state regressions fail loudly.
- [x] Save-game command handling now matches retail Quake 4 more closely: map-start autosaves use stable map/entity-filter slot names, quicksaves rotate through tracked quick slots, `saveGame checkPoint` creates protected `Checkpoint0`/`Checkpoint1` slots instead of a manual `checkPoint` save, restart prefers the latest checkpoint slot, and saves are blocked after a failed objective like retail.
- [x] Save-game loading is more stable: openQ4 now restores the game-state alert fields in the same order they are saved, validates save, entity, GUI, script, animation, physics, event, and collision-cache payloads before rebuilding runtime state, and reports truncated or malformed save files cleanly instead of continuing into a crash-prone misaligned stream.
- [x] Main menu background art now cycles through five fixed single-player campaign level images from menu-owned assets instead of randomly scanning loadscreen shots; the images are precached for faster menu opening, keep a subtle slow pan/zoom, and use a static edge-fade mask so separate expansion side/top/bottom images are no longer required.
- [x] Startup/loadscreen placeholder now hands off to the main menu automatically after 3 seconds, main-menu entry transitions use a short black fade-in stretched to native screen extents, and startup logo videos can be skipped with default-on `com_skipLogoVideos 1`.
- [x] Removed the depth-aware camera-flare graphics option and its post-process runtime path; stock-authored material flare surfaces remain supported through the existing material deform path.
- [x] Material handling fixes completed; engine startup no longer depends on custom material script overrides in repo `q4base/`.
- [x] Startup material/icon sizing now tolerates stock decls without drawable stages, preventing an application-load crash during UI icon registration.
- [x] Decl parse dispatch now follows the retail SDK no-caching path through `idDeclBase` again while material and sound shaders expose explicit three-argument parser overrides, preserving stock material stage/image parsing and preventing fully black map rendering after level load.
- [x] Decl-manager folder registration and packed-decl writing now mirror more retail bookkeeping: startup logs per-type load time/text memory again, and packed `.decls` writes store the decl's current linear-list slot with the retail index fallback.
- [x] Decl late-load diagnostics now match retail more closely by warning when uncached decls are parsed after startup/level precaching outside active editor sessions.
- [x] Decl editor/helper safety improved: `CreateNewDecl` now mirrors retail null-tolerant filename/default-definition handling, and `DeclByIndex` range errors report the decl type and requested/max indices.
- [x] Legacy PDA/email/video/audio decl families are registered engine-side again with retail list/print command wiring and parser stubs, restoring Quake 4's full decl type surface for game and tool lookups without adding content overrides.
- [x] Retail light material parsing parity improved: `lights/*` material stages and `lightFalloffImage` entries now load through the renderer's light image bucket instead of generic material image classification, restoring Quake 4's intended projection/falloff handling for stock map lights.
- [x] Retail programmable material decl parsing now recognizes Quake 4's symbolic `fragmentMap` bindings for `lightfalloffImage`, `lightImage`, `ambientNormalMap`, `normalCubeMap`, and `specularTableImage`, keeping light-driven slots tokenized instead of misloading them as ordinary images.
- [x] Retail guide-template decl wiring restored: `rvDeclGuide` now has its retail parse/evaluate/remove-bracing implementation, guide reloads clear the previous table before reparsing, duplicate guide names are rejected like retail, and `guide`/`inlineGuide` material expansion now shares the same parameter substitution path.
- [x] Decl validation/tool parity improved: the base `idDecl::Validate` wrapper now delegates through the decl base like the Quake 4 SDK, the local decl fallback validates by parsing a temporary decl instead of accepting every block, validation runs now assert the retail `DoingDeclValidation()` editor bit so parser-side hard errors can downgrade to warnings, entityDef media caching observes the recovered retail editor-mask bits, material/sound decls expose the retail validation slot, sound shader decls can rebuild text for `resaveDecl` tooling, and the engine-side `idCommon`/UI manager surface again exposes Raven's decl/tool hooks used by SDK validation, `editDecls`, model-view, debugger, and always-think GUI paths.
- [x] Game-library decl wiring tightened: SP `model` and `camera` decl validation now performs temporary-decl parsing instead of accepting every text block, SP validation-only model/animation failures downgrade through the same `DoingDeclValidation()` warning path as MP/the SDK, SP `def`/`af` folder registration uses the same Raven wrapper path as MP/the SDK, and camera validation now allocates the dedicated `DECL_CAMERADEF` slot rather than the SDK's stale model-def allocation.
- [x] Packed decl writer/export support improved: generated game-section `.decls` files can include the registered `model` and `playerModel` decl slots alongside entity/camera/AF/export data in openQ4 mode, while the interactive `writeDeclFile` command is exposed from the same loose-decl startup branch as retail.
- [x] Decl SDK/header wiring tightened: restored PDA/email/video/audio decl declarations now expose both retail parse entry points to the companion game-library headers, and optional decl-folder scans tolerate absent file lists while continuing recursive discovery.
- [x] Player-model decl parsing now follows the recovered retail rules more closely: malformed unquoted keys warn/default instead of being accepted, only canonical `def_head` / `def_head_ui` fields populate head data, and the fallback definition matches Quake 4's minimal marine/Kane-head pair.
- [x] Playback decl runtime sampling now follows retail timing more closely: recorded paths are resampled on finish, offset/angle queries return the playback expiration state, and button/impulse callbacks preserve their event offsets for game playback drivers.
- [x] BSE effect decl parsing now follows retail keyword handling more closely: the effect decl exposes the retail no-caching parser slot used by the decl manager, effect/segment/particle tokens are case-insensitive, `doubleVision`/shake/tunnel segment IDs use the shipped ordering, malformed effect bodies report failure at the same closing-brace boundary, trail segment lookups are case-insensitive, effect duration bounds rebuild on every finish, invalid particle-template entries warn/recover like retail, empty particle models fall back to `_default`, and overfull spawned-effect lists warn instead of terminating the client.
- [x] Game-library decl validation now shares the engine's temporary-decl validation/free helpers so SP/MP model and camera validation reject defaulted parses and clean allocated decl/base pairs consistently, and the companion `playerModel` header exposes the same retail parse overload surface as the engine.
- [x] Sound-shader decl parsing now reads the retail `s_maxSoundsPerShader` cvar, so stock configs and quality presets control per-shader sample admission instead of being ignored by the old openQ4-only `s_maxSamples` name.
- [x] Decl command tooling now follows retail default-creation semantics for `resaveDecl <type> <name>` and `touch <type> <name>`, sharing the same valid-type listing and materializing/defaulting named decls through `FindType` instead of treating absent loose declarations as hard misses.
- [x] Guide-template decl expansion now follows retail consumption semantics: found `guide` and `inlineGuide` calls are consumed even when their argument list does not match, inline-guide replacement runs as a single pass over the original definition, and missing inline-guide definitions return the retail failure state instead of partially succeeding.
- [x] Decl-file guide dispatch now follows the retail loader path in loose, packed, and lazy packed-stub parses: `guide` decls run the guide expansion attempt and the inline-guide pass without treating a failed expansion return as a skip, and duplicate loose decl definitions warn unconditionally like the shipped engine.
- [x] Decl source replacement now follows retail editor/save wiring: invalid source files report an empty filename, external-modification checks close their read handle before returning, and `resaveDecl`/decl-editor writes target `fs_devpath` instead of the launch CD path.
- [x] Retail guide parsing parity advanced: `guides/*.guide` now loads through a recovered guide-table path, guide creation shares the retail `GetNewGuide` helper, and unrelated top-level guide-file tokens are tolerated like the shipped engine instead of aborting startup parsing.
- [x] Packed decl serialization and intake now track retail more closely: loose/packed decl loaders let `ParseBracedSectionExact` consume non-guide bodies directly, inline-guide expansion runs from the same post-body step, and generated packed decl entries always write the retail `type name` header before the stored body or stub.
- [x] Guide-template substitution now follows retail's evolving-definition replacement loop, preserving correct behavior when guide arguments overlap with parameter names or later replacements shift the body, and the obsolete pre-retail guide preprocessor has been removed.
- [x] Loose decl reloads now honor retail's renderer-side `image_writeProgramImages` parse trigger, allowing the program-image generation path to force newly loaded decls through `ParseLocal()` once the renderer is running even when they have not already been referenced this level.
- [x] The `checkDecls` command now follows retail's reparse gate for every loaded decl that is not already `DS_PARSED`, so defaulted decls are checked again instead of being skipped as if they were clean.
- [x] Packed decl loading now initializes placeholder decl records completely before indexed replacement, preventing the retail `com_SingleDeclFile`/`checkDecls` path from dereferencing debug-fill memory when sparse packed indexes are replayed.
- [x] Decl reload/tooling diagnostics now follow the retail path more closely: `reloadDecls` reports per-file progress, and decl text-memory totals walk `GetNumDecls`/`DeclByIndex(false)` so packed sparse indexes and editor summaries share the normal decl access surface.
- [x] Guide-template edge cases now match retail parser recovery more closely: malformed parameter comma counts warn under the same rule, inline-guide body unbracing follows the shipped two-step trim, and missing guide names no longer skip ahead past the lexer position retail leaves for subsequent parsing.
- [x] The packed-decl cvar surface now matches retail wiring more closely: `com_SingleDeclFile` is exported through the shared engine headers instead of staying private to the decl manager, so renderer/session/game code can bind to the same cvar symbol as the SDK headers expect.
- [x] Packed-decl startup naming now follows retail's default asset-log handoff: the filesystem seeds `assetlogs/default` during init, and the decl manager derives `default.decls` from the active asset-log basename instead of falling back to openQ4's older `decls.decls` name.
- [x] Asset-log ownership now follows retail's decl-packaging handoff more closely: `SetAssetLogName` renames the active asset log without implicitly clearing collected entries, leaving log reset/write ownership to the explicit asset-log paths.
- [x] Retail programmable material-decl parsing now recognizes Raven GLSL `shaderParm` and `shaderTexture` built-ins from guide-expanded stages, including light origins/projection/falloff, color modulation/addition, diffuse/specular colors, matrix rows, shared lookup images, and dynamic light image slots.
- [x] Retail custom-lighting material stages now stay out of the ambient pass and run through the per-light interaction path, with material-authored GLSL programs receiving Quake 4 interaction uniforms, dynamic light textures, stable tangent-space attribute bindings, and the same packed MD5R ambient-cache recovery used by the other GLSL interaction paths while shadow-map lighting falls back to the stencil path for those custom material programs.
- [x] Retail lighting parity restored for distance-cull portal fades, Raven special-effect pass ordering, ARB2 specular scaling, and `noSelfShadow` stencil-shadow routing.
- [x] Retail light/interaction parity improved again: fog/blend lights now honor Quake 4's `noFog` interaction contract, sound-driven light/material shader expressions once again receive `referenceSoundHandle` amplitude inputs, entity ambient submission now honors retail `r_lod_entities` scissor-LOD gating for tiny on-screen coverage, retail `r_useEntityScissors` behavior is once again enabled by default so entity scissor tightening matches stock Quake 4 out of the box, view entities now follow retail's sort-by-model submission order before ambient/light interaction work, authored light detail levels once again gate view-light admission through retail-style `r_lightDetailLevel`, light/shadow submissions now honor retail `r_limitBatchSize` tiny-batch dropping before drawsurf allocation, interaction shadows now respect retail shadow-LOD / `noDynamicShadows` gating instead of keeping distant or explicitly disabled dynamic shadows alive, and light/shadow routing once again keys off the original interaction material instead of letting shader overrides change `noSelfShadow` ownership.
- [x] Retail shadow admission parity now carries through openQ4's dedicated shadow-map caster path: opaque and experimental translucent shadow-map casters honor Quake 4's cached shadow-LOD decision instead of resurrecting distant dynamic shadows, the randomized retail shadow-LOD frame hold now uses Quake 4's recovered 15-bit `1/32768 * 1000` range consistently across supported platforms, shadow-map-only ambient casters keep their interaction alive without requiring a stencil silhouette volume, cvar-gated shadow-map features can request the cached LOD decision lazily without treating "not needed yet" as "rejected", inert non-silhouette surfaces no longer refresh the retail shadow-LOD hold unless they can feed a dedicated shadow-map caster, shadow-map local/global ownership now follows the same original-material `noSelfShadow` split as the stencil path, point-light shadow maps keep legacy `globalShadows`/`localShadows` in mixed dedicated/fallback caster scenes, mapped lights now fall back to the retail stencil path when every opaque caster in a legacy/prelight chain is skipped instead of sampling an empty shadow map, receiverless lights now discard dedicated shadow-map caster chains alongside retail stencil shadow chains before scene-packet/planner work, and static/prelit geometry, stencil fallback, and the modern shadow planner keep their existing behavior.
- [x] Retail packed MD5R light-submission parity advanced again in the current hybrid renderer: packed ambient surfaces, packed-backed light interactions, and packed shadow-map casters now refresh frame-temp draw/index data per visible frame instead of forcing long-lived classic cache ownership, interaction `lightTris` retain retail packed ownership metadata in the front end, and packed temp-cache submissions no longer trip `idVertexCache::Touch()`.
- [x] The final tracked renderer light-parity gap is now closed in code: packed MD5R depth, fog, ambient, material-stage, interaction, and stencil-shadow submission now bind retail-style MD5R vertex programs and draw packed buffers directly instead of falling back through transient classic draw-vertex uploads, while material parsing also resolves the retail `md5r*.vp` stage-program aliases needed by those packed paths.
- [x] Added a live renderer light-report tool (`r_showViewLights`) that prints detailed per-light diagnostics for lights affecting the current view origin/player position.
- [x] Added a persistent visual overlay for `r_showViewLights` (`r_showViewLightsVisuals`) that draws each last-reported light's origin marker, color, and radius volume in-world until the next report refresh.
- [x] Retail `deform rectsprite` material support restored so shipped Quake 4 multiplayer flag-display shaders follow the renderer's rectangular autosprite path.
- [x] Menu rendering issues fixed.
- [x] GUI font rendering now follows retail Quake 4 font metrics more closely: stock `.fontdat` files load with their native glyph bearings, advances, point-size scaling, no extra draw-time vertical glyph bias, guarded glyph atlas edge sampling, no-picmip atlas materials, retail-style DXT1 green-alpha font atlas upload/mips, `.fontdat` atlas-token canonicalization, embedded icon sizing/UV handling, shared Q4 color/repeat/icon escape parsing, RGB escape alpha preservation, retail-style icon measurement/escape-spanning cursor accounting, chat-window wrapping and parse-flag behavior, field edit spacing behavior, bind/choice/list/edit caller spacing parity, list tab baseline alignment, wrapped edit line-height/scrollbar wrapping behavior, end-of-text cursor handling, single-pass authored/choice/simple-window foreground text rendering, retail simple-window eligibility for static labels, transformed glyph and GUI-quad renderer clipping bounds, deterministic GUI quad vertex payload, font language/cache lifecycle cleanup, text spacing/style save-load persistence, and MP HUD wrap-index support while preserving openQ4's wide/tall aspect-correct UI expansion. The new `uiFontParitySelfTest` locks down retail glyph scaling, embedded icon sizing modes, RGB escape alpha behavior, baseline/alignment, expanded wide/tall canvas mapping, clipped UV trimming, font atlas upload options, and the tight stock HUD radio `fonts/marine` sizing that keeps the `incoming transmission` label intact instead of vertically clipping it against its own text boxes.
- [x] Settings menus now present cleaner Display/Game pages with compact single-display spacing, smooth clipped scrolling, corrected scroll extents, and Proper Case openQ4 setting labels while keeping section headings uppercase.
- [x] Settings-page scrolling now responds to mouse wheel, PageUp/PageDown, and controller shoulder paging on the active settings tab, keeps thumb and paging state in sync, and exposes the full Game settings scroll range.
- [x] SDL3 backend integrated as the default platform path (legacy Win32 backend remains transitional).
- [x] Meson + Ninja build system introduced as canonical build path.
- [x] External dependencies moved to Meson subprojects/wraps (`sdl3`, `glew`, `stb_vorbis`, `openal-soft-prebuilt`).
- [x] Vendored GLEW updated to 2.3.1 (both `subprojects/glew` and `src/external/glew`) while preserving local static-link/include-path compatibility tweaks.
- [x] Vendored OpenAL Soft updated to 1.25.1 headers/defs/import libs (both `subprojects/openal-soft-prebuilt` and `src/external/openal-soft`), with Win64 runtime DLL refreshed under `src/external/openal-soft/bin/win64/OpenAL32.dll`.
- [x] Dependency refresh validated with clean Meson reconfigure (`setup --wipe`) and successful rebuild in `builddir`.
- [x] Ogg Vorbis (`.ogg`) playback support integrated (decoded via `stb_vorbis`).
- [x] C++23-targeting baseline enabled on MSVC (`cpp_std=vc++latest`).
- [x] MSVC 2026 toolchain direction documented and implemented as an optional enforceable baseline (`-Denforce_msvc_2026=true`).
- [x] Meson setup wrapper improved to auto-recover missing/stale build directories and avoid VS tool discovery null-crash.
- [x] Windows SDL3 key/input parity improvements: backspace fix, control-char synthesis, locale-aware RightAlt behavior.
- [x] Manual key matrix audit completed and documented for console, GUI edit fields, chat, binds, numpad, and modifiers.
- [x] GUI scaling behavior updated to preserve uniform/aspect-correct rendering on window resize.
- [x] Engine-side console/UI relayout now handles wide and narrow/tall aspect ratios, with live updates on screen size/aspect changes.
- [x] Console mouse handling now uses native console-space cursor routing/drawing, and adds archived `con_height` control for the console open height (`0.1` to `1.0`, default `0.5`).
- [x] Platform/architecture roadmap documentation added for Windows/Linux/macOS direction with x64 baseline.
- [x] Legacy/redundant build-system artifacts reduced (CMake path retired from active source tree).
- [x] BSE manager/renderer lifecycle contract restored: effect completion now propagates correctly through `ServiceEffect` -> `UpdateEffectDef`, preventing immediate client-effect teardown and enabling proper expiry reporting.
- [x] BSE sound capability path restored at render-world boundary: `EffectDefHasSound` now delegates to decl metadata and manager checks instead of unconditional false.
- [x] BSE segment runtime no longer compiles as pure no-op stubs; core segment timing/attenuation/check/update/count plumbing has been re-enabled as the phase-4 runtime foundation.
- [x] BSE game-lib network/event parity restored: `EVENT_PLAYEFFECT_JOINT` no longer hits an assert placeholder, receive paths now validate decl/filter/rate-limit consistently, and networked effects restore gravity assignment parity.
- [x] BSE save/restore stability improved for active effects by clearing stale `referenceSoundHandle` on restore so emitters are safely reallocated.
- [x] Temporary BSE visibility fallback added (`bse_fallbackSprite`, default `1`): active client effects now emit a sprite-backed render entity until full particle-surface rendering is fully restored.
- [x] BSE segment activation timing fixed: `rvBSE::Service` no longer applies a large negative per-segment spawn offset that delayed segment starts by multiple seconds.
- [x] BSE segment spawn lifecycle restored for `SEG_EMITTER`, `SEG_TRAIL`, and `SEG_SPAWNER`, including attenuation-aware interval/count spawning and loop-time progression tracking.
- [x] BSE trail/child spawn placement improved: non-zero spawn offsets now propagate into particle init positions, and trail segments spawn using interpolated local movement offsets.
- [x] BSE moving-emitter transform parity improved: non-locked particle position/velocity evaluation now converts from spawn-space into current effect-space using stored spawn origin/axis.
- [x] BSE owner wind propagation restored by transforming `renderEffect_t::windVector` into local effect wind each update.
- [x] BSE decal segment path restored from no-op to active world projection (`SEG_DECAL` now builds/projections decal winding via `ProjectDecalOntoWorld` using sampled spawn size/rotation).
- [x] BSE spawn ordering parity improved: runtime particle insertion is no longer unconditional LIFO; linked segments preserve stable chronological/end-time list order while complex segments retain front-insert behavior.
- [x] BSE segment draw-order refinements applied for linked-strip rendering: depth-sort is disabled for linked strip topologies and initial strip index budgeting is handled explicitly.
- [x] BSE runtime sort utility implemented (`rvSegment::Sort`) with stable depth ordering for deterministic per-segment ordering when used.
- [x] BSE network/entity receive rate-limiting corrected by removing duplicate cost consumption (`Filtered` already applies category rate checks; explicit second `CanPlayRateLimited` checks removed).
- [x] BSE particle spawn-parameter sanitization restored (`rvParticleTemplate::FixupParms` no longer compiles as a no-op stub).
- [x] BSE segment attenuation scaling parity restored: emitter interval/count attenuation now uses `bse_scale` interpolation semantics.
- [x] BSE particle cap parity restored: `bse_maxParticles` now drives runtime segment allocation/count clamps instead of compile-time-only `MAX_PARTICLES` paths.
- [x] BSE owner-state parity improved: `mLightningAxis` is now derived from current origin/end-origin direction with stable basis fallback.
- [x] Temporary BSE trace spam removed from hot runtime paths (spawn/expire/remove and segment render skip/state traces), and renderer counter cvar default restored to off (`bse_frameCounters=0`).
- [x] Full clean rebuild validation completed in `builddir/` with both `openQ4.exe` and `openQ4-ded.exe` linking successfully after BSE parity updates.
- [x] BSE particle gravity parity restored: template `gravity` ranges now contribute spawn acceleration in `rvParticle::FinishSpawn`.
- [x] BSE debris parity advanced: debris particles now spawn client moveable entities (`entityDef`) through `game->SpawnClientMoveable` and stop CPU-side particle rendering.
- [x] BSE shader-parm safety defaults hardened: client effects now initialize RGBA/brightness/timeoffset in `rvClientEffect::Init`, with zeroed-parm fallback in `rvBSE::UpdateFromOwner`.
- [x] BSE template runtime contract completed by implementing missing declared helpers (`rvParticleTemplate::Compare`, `GetTraceModel`, `GetTrailCount`, `ShutdownStatic`).
- [x] BSE unlocked-bounce matrix reprojection parity improved: post-impact velocity/position persistence now uses inverse `current->init` axis mapping with origin-delta compensation, avoiding frame-space double transforms in moving/rotating emitter paths.
- [x] BSE owner-kinematics parity improved: `rvBSE::UpdateFromOwner` now preserves last valid owner velocity across same-timestamp updates instead of zeroing `mCurrentVelocity` on tiny frame deltas.
- [x] BSE oriented-particle matrix parity improved: oriented quad corner ordering/signs now match decompiled transform basis usage, removing mirrored orientation in rotated oriented-particle paths.
- [x] BSE sprite and oriented particle rendering now follows retail Quake 4's cross-quad sizing and center-normal vertex payload, restoring walker shield-hit ripples/sparks and related stock impact effects to their intended scale and orientation.
- [x] BSE effect culling now avoids stale portal-visibility rejects by requiring cached rendered geometry before pre-service/pre-render area or frustum culls can skip work, then rechecking with freshly rebuilt effect bounds; visible map-authored effects such as `game/airdefense1`'s `func_fx_24` no longer pop in late when turning toward them.
- [x] Startup command parsing hardening completed: oversized `+wait` stress launches no longer crash during early `StartupVariable` cvar processing (command-line overflow/drop handling and `idCmdArgs::AppendArg` bounds checks tightened).
- [x] Multiplayer server-side hitscan lag compensation added with configurable rewind controls (`net_mpLagCompensation`, `net_mpLagCompMaxMS`, `net_mpLagCompBiasMS`) and server diagnostics (`net_mpLagCompDebug`).
- [x] Multiplayer non-local prediction mode is now runtime-selectable through `net_mpPredictMode` (`0` legacy limited behavior, `1` enhanced per-frame prediction).
- [x] Scope/zoom handling documentation updated to reflect multiplayer zoom stability and scope yaw alignment behavior.
- [x] High-refresh presentation blends transient previous/current camera origin, axis, and FOV plus the full first-person viewmodel pose only during `Draw()`, without rerunning gameplay or adding fields to save data. Eligible moving-world entities, projectiles, lights, bound client effects, and actively animating skeletons share that presentation clock. The MP path snaps during active prediction-error smoothing, and the non-extrapolating blend accepts at most one `60 Hz` tic (about `16.7 ms`) of visual latency.
- [x] Skeletal presentation evaluates the same rounded previous-to-current time as the root and camera into isolated animator-owned scratch joints. Renderer callbacks are suppressed only on the copied draw entity, bounds and joint-bound attachments follow the scratch pose, and authoritative animator caches, AF/angular joint state, demos, saves, networking, and gameplay queries remain unchanged.
- [x] Remote-client presentation no longer advances a projectile twice during prediction replay, overwrites viewweapon interpolation endpoints on a replayed frame, or silently drops the impact/fizzle visual when the first received state is already terminal. Multiplayer actors remain outside the generic pass: remote bodies stay aligned with server rewind, and the local body/world-weapon stencil shadows remain an explicitly deferred simulation-clock visual.
- [x] Current issue #99 validation includes the expanded SP/MP presentation contract, standalone and integrated SP/MP builds, the full push profile, a scripted campaign transition ending on `game/tram1` at `189.4 Hz`, an animated-character/stencil SP pass at `202.0 Hz`, and a clean MP host/client pass at `148.0/116.1 Hz`; simulation remained `60.0 Hz` and every staged role wrote an engine-side screenshot. Human-motion, Apple, and dedicated high-refresh hardware qualification remain open.
- [x] Ragdoll activation quality improved without changing the fixed 60 Hz simulation cadence: startup now keeps owner/world motion, handles initial penetrations more cleanly, and preserves slightly richer contact support for grounded corpses.
- [x] Rigid-body physics timing-safe quality pass applied without changing the fixed simulation cadence: angular velocity handoff now respects world inertia, water is handled as drag instead of a one-time collision-state hack, rigid-body contacts keep richer deduplicated support points, and impacts now preserve time-of-impact momentum while consuming a small bounded amount of leftover fixed-step time.
- [x] Script compiler x64 pointer-temp parity fix ported from OpenD3: right-associative indirect-expression retagging now guards 4-byte object-ref temp vs 8-byte pointer temp storage mismatch by allocating pointer-sized result defs when needed, preventing trigger/door script chain corruption.
- [x] Retail and rebuilt AAS compatibility is restored: stock dummy `.aas` files load/discard like retail instead of warning-spamming and failing stock map init, AAS tactical data is cleared correctly between loads, and the AAS 1.08 settings reader now accepts the `generateAllFaces` key emitted by its own writer. The face/tactical builder flags are initialized and round-trip their selected values instead of being hardcoded on output. Issue #137's supplied `airdefense1` archive (`CED6E9D92E67D8FF71E1F2E93B68D7F6DD7C961D7E759E90E3AA243408AED187`) exposed the mismatch consistently in `.aas32`, `.aas48`, `.aas96`, `.aas128`, and `.aas250`. The focused source/CI contract passes, and a current 1280x720 windowed OpenGL run loaded the archive's exact `.map`, `.proc`, `.cm`, and all five AAS files from the isolated loose path, initialized gameplay, wrote the engine capture (`E62BD7DE995E16D31890CE9E44625FB829B20541DD7B7CB31729251214548F38`), and exited without an invalid-token, stale-AAS, navigation-load, engine error, or fatal diagnostic; the retained log is `74E5C6775C75CEA391E9E6D9E16443A026EFFAFDAC9291072F2966F50A73B196` under `.tmp/issue137-exact-map/`.
- [x] Bloom stability and quality improved: live `r_bloom`/`r_bloom*` changes now use the offscreen scene-target path immediately, scratch render targets rebuild their FBO attachments safely after runtime reallocations, map handoffs no longer rely on the fragile back-buffer bloom path, and bright-pass extraction now keeps only highlight energy so broad lights stop producing solid white bloom discs.
- [x] Manual release packaging builds are healthy again: the engine restored the shared `idCommon` timing/error/tool accessors expected by the SP/MP game libs, fixing the cross-platform release-build break that stopped the `v0.1.011` workflow before artifacts were produced.
- [x] Retail trigger compatibility improved for `game/process1 second`: the intro lift now uses retail-style precise clipping for hurt triggers while keeping the x64 bounds fallback for non-damaging player triggers, preventing a stock shaft kill volume from falsely killing the player during the downward ride.
- [x] Lift panels in `game/process2` are more reliable: in-world GUI focus now uses the actual traced panel hit, retail-style unique GUI reuse keeps panels such as `func_elevator_1_gui` interactive, and ignored no-op/disabled floor commands refresh the display instead of leaving it stuck on an in-transit state.
- [x] Renderer effect lifetime replay regression fixed: expired one-shot BSE impact effects now stay finished until their client-effect owner frees the handle, preventing bullet marks, scorch decals, and hit sounds from being recreated every frame after a hit.
- [x] Renderer-side BSE servicing now runs once per rendered frame instead of repeating for each view. The current high-refresh game-library slice does not claim broader client-effect owner-time or client-entity interpolation changes.
- [x] Collision-model retail parity improved again: `.proc` loading now matches Quake 4's looser version-token behavior, CM precache requests stay on the `.cm`-only path instead of generating render-model collision data, and extension-mismatched authored collision caches (for example `.lwo`/`.ase` names requested through runtime render aliases) now reuse the existing loaded CM without renaming or duplicating cache entries.
- [x] Collision-model loader parity improved again: CM `.proc` and `.cm` parsing now use Quake 4's binary-aware lexer path instead of bypassing it, restoring compiled-companion fallback behavior (`.procc` / `.cmc`), normal search-path precedence, retail-style CM loader diagnostics, retail proc-clip slot reservation during map builds, retail-style `.cm` serialization that no longer forces synthetic proc-clip models into saved map collision caches, retail-style binary companion generation (`.cmc`) when `com_BinaryWrite` is enabled, stock load pacifier updates around collision-data generation, retail-style fatal map-load handling when the required `.proc` build data is missing, explicit map-entity CM exports back on `fs_devpath`, and the retail PROC/AAS out-of-date overlays during content bring-up.
- [x] Collision-model trace/debug parity improved again: rotation and contents paths now restore retail-style `materialType` propagation, SP/MP share the retail two-argument CM debug interface with player-view-oriented replay hooks, retained translation/rotation failure replay now mirrors retail debugging more closely, and stock `game/airdefense1` no longer crashes at player spawn/level load when retail-style CM material-type lookups run against parsed `.cm` data.
- [x] Level-load filesystem robustness improved: empty-path `ReadFile` probes now resolve as normal missing-file checks instead of raising a fatal dialog, which unblocks stock map startup paths such as `game/airdefense1` and keeps the runtime log alive for any follow-up diagnostics.
- [x] Collision-model diagnostics parity improved again: local CM `ModelInfo()` reporting is no longer a stub, single-player restores the retail `collisionModelInfo` debug command alongside multiplayer, `Contacts()` once again follows retail's translation-only contact sweep semantics, and SP validation on `game/airdefense1` plus `collisionModelInfo all` now completes cleanly with the updated CM debug/reporting paths.
- [x] Collision-model cache-write parity improved again: runtime-generated `.cm` caches now follow retail Quake 4's `fs_devpath` destination instead of being diverted to `fs_savepath`, keeping generated CM outputs on the normal authored/dev override path alongside the existing map-entity export behavior.
- [x] Collision-model render-source parity improved again: `LoadRenderModel()` now matches retail Quake 4's authored-CM source-model whitelist by admitting only `.ase`, `.lwo`, and `.obj` when generating collision data from render models, dropping the extra `.mdr` / `.dae` CM-generation path that was broader than retail.
- [x] Single-player clip-query parity improved again: `idGameLocal::ClipModelsTouchingBounds()` no longer falls back from non-zero clip worlds into world `0` for trigger queries, so the SP bounds-query path now matches multiplayer and retail behavior instead of keeping the temporary openQ4 multi-instance trigger override.
- [x] MD5R parity improved again: SoA `Pos3Swizzled` vertex buffers now decode into usable packed geometry instead of falling back to metadata-only loads, `MD5RProc` companions now preserve and round-trip the real retail map CRC token instead of a stub value, and retained MD5R vertex exports now keep specular-color / point-size fields instead of discarding them.
- [x] Decal parity improved again: packed MD5R surfaces now prefer a retail-style native sil-trace decal projector over the transient draw-vertex materialization fallback, while still falling back safely when packed mesh state is incomplete; the fallback now reuses the current live draw geometry when available so dynamic skinned recipients do not drop back to bind-pose decal projection, the native packed path now only runs on true packed sil-trace buffers instead of misusing draw-order `silTraceVerts` mirrors, decal fade cleanup no longer needlessly invalidates cached tangent/plane data every frame when the decal mesh itself did not change, and decal draw-surfs now snapshot vertices plus per-stage color data into one retail-style transient upload block instead of splitting them across separate temp cache allocations.
- [x] SDL3 keyboard layout parity improved: printable key events now use SDL3's active-layout translation instead of the fixed US scancode map, non-English console-toggle lookup now follows the translated grave/console scancode again, and SDL text commits are decoded as complete codepoints before the stock single-byte font range is enforced rather than being split or narrowed into corrupt bytes.
- [x] POSIX startup now enforces the same single-instance guard as Windows by holding a per-user advisory launch lock, while `sys_allowMultipleInstances 1` keeps multi-instance testing available when explicitly needed.
- [x] Retail flare/material deform parity improved again: ambient submission no longer clips `deform flare` / sprite-style materials to tiny authored entity scissors, so stock map-authored effects such as `game/storage1`'s moving `beamBlastFlare` render as expanded flare geometry instead of a boxed white square; the flare deform path also restores retail prim-batch handling and cleared-temp-vertex setup.
- [x] Retail decl folder registration parity improved: recursive decl folder scans now register each child folder as its own folder/default-type pair before loading files, and `DECL_MODELEXPORT` ownership is back on the game-library `model`/`export`/`camera` registration path instead of the framework pre-claiming the parser-only export slot.
- [x] Direct no-post scene rendering now clears the backbuffer before drawing the world, preventing full-screen GUI/material overlays such as stock vehicle screen dirt from persisting after the GUI that drew them has stopped rendering.
- [x] Renderer restart and game-mode reloads are more robust: `vid_restart` and SP/MP engine reloads now release scene, post-process, shadow-map, BSE, and background filesystem state before rebuilding OpenGL, reducing black-scene and initialization failures after changing renderer settings or switching game modules.
- [x] Outdoor BSE-heavy scenes such as `game/airdefense1` now reject offscreen effect geometry from maintained effect bounds before rebuilding render surfaces, reducing frame pacing stutter while preserving authored smoke and effect rendering.
- [x] Windowed play is smoother in BSE-heavy outdoor scenes: new installs default windowed mode to borderless presentation and legacy Windows configs migrate once from the old bordered-window default, avoiding Windows/OpenGL swap stalls seen in `game/airdefense1`; BSE debris entity definitions are also precached when effects parse so first-impact debris media no longer loads during gameplay.
- [x] The `game/core2` Makron fight is more reliable on x64 builds: scripted flying turns now rotate toward their target without getting stuck, and the retail teleport phase can recover if its script handoff fails to finish so the boss no longer remains hidden and effectively undamageable.
- [x] Linux optimized builds now keep light-fixture glow colors stable while panning the camera, fixing the primary-color flicker seen on stock map fixtures such as `game/airdefense1`.
- [x] SDL3 desktop focus transitions now release fullscreen relative-mouse capture, clear keyboard/controller/touch state exactly once, reject queued user input while the game window is inactive, and apply `s_muteUnfocused` consistently on Linux and experimental macOS as well as Windows.
- [x] SDL3 Linux and experimental macOS system-console input now decodes committed and pasted UTF-8 before narrowing to the stock single-byte font range, and asynchronous OpenAL device hotplug notifications use an atomic handoff instead of racing the audio update thread.
- [x] Vulkan now renders both authored MD5 meshes and converted or prebuilt MD5R assets consistently in single-player and multiplayer. MD5R models materialize the classic geometry contract used by every Vulkan consumer—including ambient and interaction lighting, stencil and mapped-shadow submission, fog/blend lights, sky, decals, overlays, and collision—while retaining animated tangents and legacy MD5 skin-scale behavior. Native ASCII `.md5r` / `.MD5RProc` and compiled `.md5rc` / `.MD5RProcc` export-and-reload paths are covered, including retail packed vertex formats and rebuild-on-load payloads; no replacement game content is required.
- [x] Accented characters now display correctly in Spanish, French, and Italian menus and subtitles. The shipped string tables are UTF-8 while the stock Quake 4 fonts are a fixed 256-glyph set, so text such as `MENÚS` previously rendered as `MENA S`; openQ4 now converts a UTF-8 string table to the font's character set when it loads one, and substitutes readable ASCII for typographic characters the stock fonts do not draw. English apostrophes in subtitles are also fixed, community translations authored in UTF-8 now work, and retail localized packs are untouched.
- [x] Startup failures no longer exit silently. A second error raised while the engine was already shutting down used to end the process with a success code and no message, which left users with a log that simply stopped mid-shutdown and no way to find out what went wrong. Both messages are now reported and the process exits with a failure code. Content checks that abort startup additionally list the resolved `fs_basepath`/`fs_savepath`/`fs_cdpath` and every PK4 actually found in `q4base` and `baseoq4`, which identifies the most common Linux cause: a case-mismatched folder or PK4 name copied from a Windows install.
- [x] Custom DDS texture packs, including BC7, are now selected reliably. Texture-compression capabilities were only ever published to one of the binaries that need them, so depending on the active renderer every DDS replacement could be rejected without explanation; user replacements are now also probed ahead of the stock precompressed data instead of behind it, and the highest performance preset no longer switches replacements off and discards a player's setting. `image_showPrecompressedTextures 1` now lists every path tried and why each was rejected, so a pack author can see exactly what a file must be named. BC7 normal maps use the ordinary XYZ-in-RGB layout — packs previously encoded with `bc7enc -r2a` for openQ4 need re-encoding without it.
- [x] Missing sound samples are no longer silent failures: the engine reports the sample it could not find along with every path it tried. Voice-over now falls back to English when a language ships localized text but no localized voice track, matching the original game, and the `s_showVoices` diagnostic prints its voice list again instead of producing no output at all.
- [x] Navigation data problems are now reported instead of being developer-only, and a scripted character that cannot reach its destination can no longer stall a map script indefinitely — the move is abandoned after `ai_scriptedMoveTimeout` seconds without progress (30 by default, 0 to disable) with a log line naming the character, its target, and why it was stuck. Flying-enemy movement and tether goals are also corrected: area ceiling heights were never computed, so every navigation area reported a ceiling of zero.
- [x] Loading a savegame without first quitting to the menu now starts from the same clean state as loading after a restart; looping sounds and render objects from the previous session are released before the new one is built.
- [x] Release builds are more robust on 64-bit ARM: the binary declaration reader now fails cleanly on an unexpected value instead of silently losing sync with the rest of the stream when assertions are compiled out, an unexpected BC7 texture degrades to uncompressed instead of ending the session mid-load, and floating-point contraction is pinned off so ARM64 and x86-64 evaluate physics and navigation math identically.
- [x] Menu and HUD text is now drawn from scalable fonts instead of the original fixed-size bitmap ones. The shipped fonts were rebuilt as TrueType faces traced from the game's own artwork, so the letterforms are unchanged, and the engine rasterises them at whatever resolution the display is actually running at rather than magnifying a 640x480-era atlas — text stays sharp at 1440p and 4K instead of softening. This is on by default; `r_useTrueTypeFonts 0` returns to the original bitmap fonts, and the game falls back to them automatically if the font files are missing. `r_ttfFontResolution` trades atlas memory for extra sharpness. The developer console and loading screen are covered as well: they draw from a separate fixed-size character sheet, which is now rebuilt at display resolution too. Note that the original `chain` font shipped with a zero-width space, which ran words together in that face; the rebuilt font gives it a normal space, so some text is spaced differently — correctly — than it used to be.
- [x] Accessibility: text can be given a solid black backing so it stays readable over the world and over busy panel artwork. `gui_textBackground` sets how opaque the backing is (0 is off, 1 is fully opaque; around 0.75 keeps the artwork visible while making text easy to read) and `gui_textBackgroundPadding` controls how far it extends past the text. It covers menu, HUD and in-game GUI text, applies immediately with no restart, and works with either the scalable or the original bitmap fonts. The backing is drawn once per line, so it never shows through the gaps between characters, and every line of a given font gets the same height whatever characters it contains.
- [x] A texture quality control in the style of Quake 3's `r_picmip` is available for players who need to trade sharpness for memory and bandwidth. `image_picmip` drops whole mip levels, so every step halves a texture no matter how large it started, but unlike the original it only reduces the **diffuse layer** of a material — normal maps, specular maps, lighting, skies, decals, fonts, and the whole HUD and menu keep their authored resolution, so surfaces stay correctly lit and the interface stays sharp. `image_picmipFilter` restricts it to world surfaces (the default), models, or both, `image_picmipMinSize` stops small textures from turning to mush, and a material that declares `nopicmip` opts out. Textures reload as soon as one of these changes, with no `vid_restart` needed, and the generated texture cache is keyed by the active reduction so switching settings never leaves a stale size behind.

- [x] Renderer performance evidence now binds exact stock-map, OpenGL/Vulkan backend, benchmark profile, and bordered-window 1280x720 launch state to a versioned CPU/GPU percentile contract. Gameplay and retail-baseline reports retain the selected thresholds, measured samples, runtime/artifact hashes, and source provenance, then replay them fail-closed; unavailable GPU timing, archived display drift, fullscreen/noncanonical budget runs, or changed evidence cannot pass. The initial 20/28 ms rows are target ceilings rather than universal measured claims.
- [x] Milestone A's current-build integration gate has exercised identical jobs-on/off storage output, clean OpenGL and Vulkan repeated-map job shutdown, five deterministic dedicated exits, delayed whole-frame timing on both backends, a replay-valid schema-10 four-role retail baseline with local human image review, and replay-verified 8/8 OpenGL plus 8/8 Vulkan required profiles from the final immutable development runtime. This validates the development build, not a release package or universal performance level.
- [x] OpenGL depth-bounds submission now orders and clamps both values to the legal range before calling the driver, eliminating the two `GL_INVALID_VALUE` records that previously made `game/medlabs` the lone required-profile failure; its debug-context rerun is error-free.
- [x] Shadow-map caster coverage and contact quality are corrected across the classic OpenGL, direct Vulkan, and shared interaction paths. Automatic culling now stores the near shell for sealed geometry and renders open/uncertain stock geometry two-sided; large point-light bias is world-bounded, balanced filter footprints are tighter and cheaper, PCSS blocker search is bias-consistent, map/alpha/dynamic cache ownership fails closed, projected and point resources carry exact world/pass/storage provenance through bind time, partial mapped ownerships receive exact stencil supplements or return to full stencil, Modern OpenGL refuses resources it cannot consume completely, atlas placement follows physical limits, and nested OpenGL elapsed queries no longer generate validation errors. Final staged, windowed OpenGL `game/airdefense1` stencil/mapped qualification passed 2/2 with retained engine captures under `.tmp/shadowmap-review/final-ab-runtime-6/`; stencil measured 96.8 Hz at p95 14 ms and mapped measured 74.8 Hz at p95 16 ms, while the mapped capture keeps the start-area buggy, crates, arches, and rails grounded without the former light-corner gap. A matching direct-Vulkan mapped smoke passed at 83.7 Hz and p95 15 ms with a clean capture and no Vulkan validation/call failure under `.tmp/shadowmap-review/final-vk-runtime-5/`. The low-frequency telemetry rerun under `.tmp/shadowmap-review/final-telemetry-6/` recorded 22 lights with six supported point lights, four local and four global mapped ownerships, 212 admitted casters, two successful translucent receiver chains, three complete fail-closed stencil fallbacks, 48 rendered point faces, zero unshadowed/render/mask/receiver failures, and 43 completed plus 21 still-pending asynchronous elapsed-time queries with zero synchronous waits, slot exhaustion, drops, or query failures.
- [x] Known stock startup warnings are resolved without replacement content: the three brown-fluid images and large water splash omitted from the retail PK4s use fail-after-primary shipped-media fallbacks, generated TrueType atlas materials bind their already-uploaded intrinsic images on first parse and survive renderer restart, wide loading backgrounds retain atomic publication under long Windows save paths through a root-relative 128-bit CSPRNG staging identity, generated image caches fall back from the legacy qpath to a versioned SHA-256/128 compact identity without bypassing VFS or pure-server policy, and rigid-body states that are immediately clamped or forced to rest remain developer diagnostics while unrecovered physics faults keep warning severity. Every binary-image header, mip record, and payload write now rejects short output, and the renderer evidence tools fail closed on either cache-write or expanded-loadscreen-publication warnings. The final staged, windowed OpenGL `game/airdefense1` cold run and same-savepath warm reuse run passed with engine captures and no warning, error, fatal, or asset-load-failure records under `.tmp/unrelated-warning-fixes/airdefense1-compact-cache-cold-longpath-final-2/`: the fixture held the valid expanded TGA at 240 characters, reduced its formerly failing 263-character staging path to 222, forced nine overlong legacy cache identities onto compact paths, left no staging files behind, and preserved byte hashes plus modification times for all eight persistent stock/image-program compact caches on warm load. The generated loadscreen derivative alone refreshed as expected when its source TGA was atomically regenerated.
- [x] Muzzle flashes, bright projectiles, and explosions now light the world the way Quake II and Quake III did, through the new default-on `g_classicDynamicLights`. Firing raises a short, wide world flash at the barrel on top of the weapon's authored one; rockets, plasma bolts, dark matter spheres, and napalm carry their own light; and detonations flash the surrounding geometry, which no shipped Quake 4 explosion effect did at all. Colors are taken from the shipped assets rather than invented - muzzle flashes use each weapon def's own `flashColor`, and projectile and explosion colors come from the light segments Raven authored in `effects/weapons` and `effects/monsters` - so a classic light reads as part of the effect it belongs to. Projectiles whose def authors a light, and the single-player rocket and grenade whose shipped trail effects already carry a light segment, are left untouched; bullets, nails, debris, and glass are deliberately unlit. At most 24 one-shot flashes are alive at once, all unshadowed, distance-culled, and recycled oldest-first. `g_classicDynamicLightScale` scales their radius between 0.25 and 4, content can override or opt out per entity def with `classic_light`, `classic_light_color`, and `classic_light_radius`, and the cheat command `testClassicLight` raises them without firing a shot.
- [ ] Finish Milestone A release qualification by repeating and retaining the full job, renderer-budget, retail-baseline, and human-review evidence from clean committed source and a freshly staged final package, then recording the required release platform/driver coverage.

## macOS Evidence Gate

Complete this section before release notes claim macOS support beyond the current preview Apple Silicon/arm64 status.

- [ ] `docs/dev/macos-signoff-evidence.md` has a current release entry for the candidate version.
- [ ] The evidence entry records run ID, archive path or artifact URL, archive SHA-256, validator command, validator result, openQ4 commit, `openQ4-game` commit, package artifact names, matching dSYM symbol artifact names, `SYMBOLS.txt` manifest status, signing/notarization status, architecture policy, CPU architecture, OS matrix role, macOS version, kernel, Xcode version, macOS SDK version, hardware model, graphics bridges, OpenAL provider, and known exceptions.
- [ ] The current matrix policy in `docs/dev/macos-support-matrix-policy.md` matches the release notes, `BUILDING.md`, `docs/dev/platform-support.md`, `docs/user/getting-started.md`, and packaged README wording.
- [ ] macOS floor-version evidence is recorded for the documented `macOS 11` floor, or the release stays a preview and notes that floor evidence is still pending.
- [ ] Latest-public-macOS evidence is recorded for the release candidate, or the release stays a preview and notes that latest-version evidence is still pending.
- [ ] The accepted archive was collected with `-RequireCompletedSignoffChecklist`.
- [ ] `python tools/macos/validate_signoff_archive.py <archive> --require-completed-checklist` passed for both OpenGL and Metal bridge result directories.
- [ ] `python tools/macos/record_signoff_evidence.py <archive> --version vX.Y.Z --update-index` updated `docs/dev/macos-signoff-evidence.md`.
- [ ] Each bridge report includes `renderer-smoke`, `renderer-mp-smoke`, and `renderer-matrix` evidence.
- [ ] The release follows `docs/dev/macos-renderer-backend-policy.md` and `docs/dev/macos-native-backend-containment-policy.md`: OpenGL and Metal bridge evidence are kept separate, the Metal package is not described as native Metal, and native Cocoa/OpenGL backend results are not counted as release support evidence.
- [ ] SP entered an in-game map on real Apple hardware.
- [ ] MP loaded `game-mp`, started `mp/q4dm1`, connected a local client, and exited cleanly.
- [ ] Dedicated server startup was covered or explicitly documented as unsupported for this release.
- [ ] Finder or Desktop launcher startup and terminal startup were both checked.
- [ ] The package layout contract in `docs/dev/macos-package-layout-and-release-policy.md` was followed: app data was under `Contents/Resources/baseoq4`, signed SP/MP modules were under `Contents/Frameworks`, and no adjacent `baseoq4` duplicate shipped.
- [ ] Runtime macOS packages contain `SYMBOLS.txt`, do not contain `.dSYM` bundles, and have matching `openq4-<version>-macos-arm64-<bridge>-symbols.tar.xz` artifacts recorded for crash symbolication.
- [ ] Launch from a mounted signed/notarized DMG was checked, or unsigned archive behavior was recorded as an unsigned-package exception.
- [ ] Dragging only `openQ4.app` to `/Applications` or another user-writable location was checked.
- [ ] Whole-package copied launch was checked for loose client, dedicated-server, and support-tool sibling-runtime discovery.
- [ ] `fs_basepath`, `fs_cdpath`, and `fs_savepath` were confirmed in logs for Finder/copied package and terminal launches.
- [ ] Gatekeeper assessment passed for signed/notarized DMGs, or unsigned/unnotarized approval friction was recorded for development archives.
- [ ] First-class macOS release jobs used signed/notarized DMGs; unsigned `-unsigned.tar.gz` artifacts were published only as preview/development fallback output.
- [ ] Keyboard, mouse, controller if available, audio output/device switching, windowed/fullscreen, selected display, and HiDPI/Retina checks were completed or recorded as exceptions.
- [ ] Curated release notes in `docs/dev/releases/vX.Y.Z.md` mention arm64-only support, unsigned/unnotarized package behavior if applicable, and any renderer, audio, input, package-layout, SP, MP, or dedicated-server limitation found during signoff.
- [ ] Curated release notes in `docs/dev/releases/vX.Y.Z.md` mention that the macOS Metal package is a bridge around the OpenGL renderer and do not imply native Metal or native Cocoa/OpenGL backend support.

## Carry-forward disposition

Current work is tracked in the [development roadmap](roadmap.md): shared rendering
and advanced effects under REN-01/REN-06, competitive workflows and recordings
under MP-01/MP-05, high-refresh scope under HF-01 through HF-07, macOS signoff
under PLAT-04, and strict-string compliance under CORE-04. Final-package evidence
is governed by REL-01 through REL-04.

<details>
<summary>Historical carry-forward wording, superseded on 18 October 2026</summary>

These entries preserve their original evidence and limitations. Their unchecked
state and older implementation boundaries do not override the current roadmap.

- [ ] Complete Milestone F release promotion from clean committed source and a freshly staged final package, then retain target platform/driver and release-review evidence. The local Windows current-source implementation exit already has passing PBR/probe/decal contracts and execution plus the 2026-08-24 independent froxel-volumetric, SSR, and SSGI native/static/shader-pin gate, individual/combined windowed OpenGL gameplay, combined validation-clean Vulkan gameplay, visible effect delta, and exact master-off packet rollback. Promotion still needs final-package recapture and any retained RenderDoc, broader authored probe/decal scene, advanced screen-space visual review, and target platform/driver evidence selected for release. All leaves remain default-off, and `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS` remains `0`.
- [ ] Repeat and retain the `r_rendererSharedGui 0/1` root-menu/loading/HUD release-acceptance set from clean committed source and a freshly staged final package on OpenGL and Vulkan, with exact engine-TGA parity, complete ownership/fallback diagnostics, human review, and target-platform/driver coverage.
- [ ] Run and retain the `r_rendererSharedInWorldGui 0/1` fixed-camera tagged-surface release-acceptance set from clean committed source and a freshly staged final package on OpenGL and Vulkan. Require exact same-settings parity, nonzero tagged-subset ownership, named zero-commit rollback, and no duplicate classic/shared draws.
- [ ] Repeat the locally passing `r_rendererSharedWorldAmbient 0/1` eligible-view and deform-fallback evidence from clean committed source and a freshly staged final package, then retain target-platform/driver coverage before promotion. The development-worktree GL/Vulkan engine captures, exact hashes, named rollback, and safe-matrix results are recorded in `docs/dev/classic-world-ambient-domain-modernization.md`.
- [ ] Run and retain the complete `r_rendererSharedWorldInteraction 0/1` release-acceptance set from clean committed source and a freshly staged final package on OpenGL and Vulkan: controlled unshadowed, stencil, projected+point mapped, mixed map/stencil, and constrained map-admission fallback; stock projected, point, CSM/parallel, dynamic, perforated, same-light hybrid, and translucent-moment fallback. Require exact per-map/backend reconciliation, explicit dynamic/alpha feature evidence, a retained final six-component camera pose, same-settings classic/shared engine-TGA parity, a material shadows-on/off engine-TGA delta, named fallbacks with zero committed ownership counters and no shared main-target draw, and retained target-platform/driver coverage before promotion. The controlled development-worktree suite already passed 5/5 on GL and 5/5 on Vulkan; every stock-profile case still needs fixed-camera and reference qualification. The controlled test light is not a multi-cascade case. The contract and local evidence are recorded in `docs/dev/classic-interaction-domain-modernization.md`.
- [ ] Repeat and retain the complete `r_rendererSharedWorldFogBlend 0/1` release-acceptance set on OpenGL and Vulkan from clean committed source and a freshly staged final package, then add fixed-camera authored-stock fog, retained human review, and target-platform/driver coverage. The controlled development-worktree stock-declaration suite already closes exact same-backend classic/shared engine-TGA parity, nonzero light/receiver/stage/fog-cap/draw/no-op/hash/backend reconciliation, material fog/blend deltas, named zero-commit complete-phase fallback, and clean Vulkan validation on both backends. The contract, hashes, deltas, and local report directories are recorded in `docs/dev/classic-fog-blend-domain-modernization.md`.
- [ ] Repeat and retain the `r_rendererSharedDeform 0/1` release-acceptance set from clean committed source and a freshly staged final package on OpenGL and Vulkan. Require exact same-backend classic/shared engine-TGA parity with nonzero completed output, an otherwise identical `r_skipDeforms 1` effect delta, named unsupported/skipped/failure rollback with zero committed shared work, clean restart/frame-reset diagnostics, human review, and target-platform/driver coverage. The controlled development-worktree suite already passes exact parity, nonzero completed ownership, material effect delta, skipped zero-commit rollback, native/static checks, and clean backend diagnostics on both backends; its hashes, deltas, and report directories are recorded in `docs/dev/classic-deform-domain-modernization.md`.
- [ ] Repeat and retain the broader `r_rendererSharedSubview 0/1` release-acceptance set from clean committed source and a freshly staged final package on OpenGL and Vulkan. R6 already closes one capture-backed color-2D mirror coupled to a cinematic-plus-`_currentRender` tail. Still require fixed direct-mirror, dynamic mirror/reflection, x-ray, remote-camera, refraction, multi-level nested-special-view parity, qualifying 2D/cubemap color/depth ownership, named malformed semantic/target/face/descendant zero-commit fallback, a coupled `_currentDepth` case, retained coordinates, human review, and platform/driver coverage.
- [ ] Repeat and retain the `r_rendererSharedCinematicPost 0/1` root cinematic and ordinary-root authored-post release set from clean committed source and a freshly staged final package on both backends, including frozen decoder time, exact engine-TGA parity, visible enabled/skip delta, complete ownership/fallback accounting, and `_currentDepth`. The R6 independent-CVar and nested cinematic-plus-`_currentRender` cases are controlled development-runtime evidence, not substitutes for these root/final-package cases.
- [ ] Run and retain the `r_rendererSharedSpecialFrame 0/1` stock render-demo plus Raven blur/AL controller release set from a fresh final package on OpenGL and Vulkan, proving exact session/effect-mask ownership, parity, named fallback, no partial effect execution, and retained human/platform review.
- [ ] Complete competitive-match release qualification: run the staged warning-clean MP build and full automated contracts, then exercise the compact HUD/scoreboard and Match Control through windowed 16:9/4:3 keyboard/controller workflows for every supported live role, encoded recipient-redaction and camera-follow checks, supported placed-major-item pickup/respawn timing across repeated world pauses, supported-mode lifecycle edges, BO1/BO3/BO5 map/recovery paths, JSON interruption handling and playback of the linked generated MVD. Captain spectator invitations remain fail-closed. Verify the pure future-repeater policy separately for a public-only, no-item-timing result without treating it as a shipped live Q4TV/repeater transport; delayed broadcast and live multi-POV remain deferred.
- [ ] Complete issue #99 high-refresh qualification: repeat human SP/MP feel, projectile/viewweapon, moving-platform, and cut-heavy camera checks on the final package; validate on Apple and dedicated high-refresh hardware; and decide the release scope for multiplayer actor/shadow presentation, bespoke FX/BSE owners, alternate cameras, and raw-input compensation. Remote multiplayer actors and the local body/world-weapon stencil shadows remain explicitly outside the current generic presentation pass.
- [ ] Run macOS `Signoff -MacOSGraphicsBridge both`, complete the manual hardware checklist, then run `CollectResults -MacOSRunId <id> -MacOSGraphicsBridge both -RequireCompletedSignoffChecklist` on real Apple hardware and attach the auto-collected, auto-validated `.tmp/macos-vm/results/openq4-macos-results-<run-id>.tar.gz` evidence for input devices, audio devices, display modes, SP gameplay, MP `renderer-mp-smoke`, and in-game OpenGL/Metal renderer coverage beyond hosted CI compile/link/package checks; record the accepted archive in `docs/dev/macos-signoff-evidence.md`.
- [x] Replace temporary MSVC compatibility flag (`/Zc:strictStrings-`) with full strict-strings codebase compliance (31 October 2026).

</details>

### Native weapon wheels and horizontal placement (7 October 2026)

- Replaced the `ui_retained` wheel bypass with native vector Marine/Strogg documents, stock/expansion rosters, live ammo, selection and transitions; added MP support through the existing reliable impulse path.
- Added archived `ui_weaponWheelOffset` (default 27; 0 centered; −40..40) and a localized Game settings control; safety fitting preserves the preference on narrow views. Classic remains selectable.
- Wheel scene focus uses an eased 8 dp Gaussian pass before the HUD in both SP and MP, covering wide/tall views and respecting the opaque-backing option.
- Evidence and boundaries: [weapon wheels](ui/weapon-wheels.md). Full wheel/quick-chat product acceptance remains open.
