# Unreleased openQ4 development changes

These changes belong to the development tree and have not shipped in an
available release. **0.13.2 is the latest available release; 0.14.0 was
withdrawn.** This material was previously accumulated in the 0.13.2 notes,
which incorrectly attributed later work to that package. It is retained here
for the next release's curation, with no version or release date promised.
See [release status](../../user/release-status.md) and the
[development roadmap](../roadmap.md) for current scope.

## Highlights

- **Build levels in the experimental editor.** `editorExperimental` now edits
  brushes and patches as well as entities: move, turn and scale them in new
  top, front and side views or in 3D, on a grid and with texture lock, draw new
  brushes, group, layer and duplicate them, save and insert prefabs, and rename
  entities without breaking their links. Panels dock where you want them, each
  map remembers its cameras, and the inspector knows each property's type.
  **Build** compiles the saved map in the background, links every compiler
  warning to the brush or entity it names, and replaces your playable files
  only when the whole compile succeeds; **Play** opens the result in its own
  window with a private profile. See the
  [level editor guide](../../user/level-editor.md).

- **Vehicle, fly, shot and item clips work as Quake 4 intended.** openQ4 read
  the names in Quake 4's collision files literally, but Quake 4 wrote several of
  them one slot off. The Convoy and Walker levels' vehicle clips, and the shot
  clips in campaign maps and in multiplayer maps such as `q4dm4`, `q4dm6`,
  `q4ctf5` and `q4ctf7`, now block what they were built to block.

- **Recompiled maps keep their AI tactics.** `dmap` and `runAAS` now place the
  cover, lean and corner positions Quake 4's enemies use to take cover and
  fire around walls, matching 98.8% of the positions in the shipped maps.
  `runAASTactical` adds them to an existing map's navigation. Compiling a map
  with heavily subdivided curves is also far faster.

- **Find the moments that matter in multi-view demos.** Recordings now mark
  kills, flag captures, score and round changes, timeouts and powerups, and
  server operators can add notes with `mvdMark`. Jump between them with the
  new **Prev Marker** and **Next Marker** buttons or `demoMarker`, let
  **Auto-Track** follow the killer or the action, and rewind long matches
  quickly: a keyframe every ten seconds means a seek replays at most ten
  seconds instead of the whole match. Players who join mid-recording keep their
  names. See the [demos guide](../../user/multiview-demos.md).

- **Choose your renderer and GPU in System settings.** The retained menu groups
  backend and device selection with quality presets and Auto-Detect, shows the
  active renderer, and offers a checked restart after applying a change. Choosing
  a GPU selects Vulkan automatically. Fresh desktop configurations prefer a
  compatible high-performance GPU through Vulkan, with OpenGL fallback; existing
  saved backend choices are preserved, Android keeps OpenGL ES, and Automatic
  keeps OpenGL for PC OpenXR. GPU choices
  survive changes in driver enumeration order. Replace engine and renderer
  modules together. Vulkan remains experimental on Linux and macOS.

- **More useful graphics presets and hardware detection.** Enhanced fills the
  gap between Balanced and Quality with lighter modern shadows and effects.
  Auto-Detect uses the active GPU, distinguishes shared memory, and accounts for
  resolution and CPU capacity. Unknown hardware gets a conservative starting
  point. High-end cards start on Quality (12 GB) or Ultra (16 GB) at up to
  1440p and one preset lower at 4K; Maximum remains a manual choice. Reapply a
  preset to adopt the revised values, and replace engine and renderer modules
  together.
  A new **Low** preset suits PCs at the minimum requirements: the original
  shadows at 60 FPS with a reduced scene size. **Minimum** and **Low Power**
  now draw no shadows, for software renderers, small boards, low-memory phones
  and battery saving.
  Fresh installations now use that same detector after the GPU initializes;
  existing saved settings and launch overrides are preserved. Android gets a
  conservative mobile limit, and platform hints no longer override a smaller
  verified memory budget. Phones, Steam Deck and Raspberry Pi keep their
  preset's frame cap on a faster display, and 16:10 laptop panels are treated
  like their 16:9 siblings.
  Capable Macs using Apple's compatibility renderer are no longer forced to
  Minimum because modern post effects are unavailable. An unset legacy quality
  marker no longer resets saved graphics settings at startup.
  On high-resolution displays, Auto-Detect also reduces scene render scale to
  suit the hardware, keeping the interface sharp, and leaves unsupported post
  AA off. Stock quality controls now use the same presets and preserve your
  display mode and VSync preference.

- **Less wasted Vulkan rendering work.** High-resolution shadow maps avoid clearing
  unused atlas space and repeating shared opaque caster draws. PCSS shadows
  also avoid copying large shared depth blocks, and post-processing avoids
  unnecessary image transitions and copies while preserving graphics quality.
  Shadow passes also avoid clearing and storing unused stencil data.
- Vulkan also clears stale rendering state during video restart, preventing a
  crash when diagnostic rendering runs before the next normal view.

- **Shorter checkpoint pauses.** Game-state buffering, reuse of unchanged script checksums
  and dictionary data prepared between saves,
  deferred thumbnails and background storage, including thumbnail persistence,
  reduce the saving interruption while preserving existing
  save files and recovery checks. Loading or quitting waits for a pending save
  to finish, and the saved message appears only after it commits. A short pause
  can still occur while game state is captured. Developer launches also avoid
  unnecessary logging during recovery checks. Replace the engine, renderer
  and game modules together.

- **Ultra now selects maximum graphics quality.** It requests the highest
  supported MSAA, SMAA Ultra, maximum shadow resolution and filtering, and
  higher-quality ambient occlusion and motion blur, with bloom and tone mapping
  enabled. Quality provides an intermediate step. Reapply your preset to adopt
  the new values; native resolution and DDS replacement packs remain supported.
  Four stabilized cascades use an expanded shadow atlas to retain maximum
  resolution without dropping a required shadow pass. Motion blur also works
  in areas with portal skies, which previously cleared its camera history.

- **Motion blur that works with temporal AA and looks the same at any frame
  rate.** Motion blur used to switch itself off silently whenever temporal AA
  was active, including automatic upscaling and dynamic resolution; it now
  blurs on both renderers. Your weapon no longer smears when you turn, and
  characters blur with their animation as well as their movement. The blur
  length now follows how fast things move, so 144 Hz and uncapped play look
  like 60 Hz play; `r_motionBlurShutterMsec 0` restores the old frame-rate-
  dependent length. Cinematic cuts start a fresh blur, and screenshots keep
  it. Replace the engine, renderer modules and `pak0.pk4` together. See
  [display settings](../../user/display-settings.md#motion-blur).

- **Find multiplayer objectives at a glance.** Flag bases, Arena CTF assault
  points and DeadZone zones now have compact HUD symbols with distance and
  directions when they are outside your view. The icons show flag availability,
  zone control and artifact requirements without additional text. Markers dim
  near your crosshair and stay clear of scoreboards. Use
  `seta hud_objectiveMarkers 0` to turn them off. Update the engine and game
  modules together. In VR, markers follow your head on the stereo HUD and
  respect its size and position. VR remains experimental; real-headset
  readability and comfort have not yet been qualified.

- **Faster gameplay HUD.** The ECG uses its original installed artwork with
  the same scrolling and health colours, reducing the new HUD's rendering
  cost. Radio and voice-chat displays use the original bitmap waveform and
  animation, and stop under Reduced Motion. Cached artwork, fewer mask passes
  and less repeated state/layout work further reduce HUD overhead. Crosshairs
  reuse unchanged drawing, and fading text reuses its drawing without changing
  glyph overlap or colour. HUD fades also avoid repeated styling and unnecessary
  layout updates. If a new
  HUD frame cannot render, the stock HUD takes over immediately.

- **Familiar status readouts.** Ammo, health and armor in the new HUD regain
  the legacy TTF positioning, spacing and scale. The centered gauges also
  match the original background tones, saturation, soft shading and armor
  dividers. Ammo panels use the original
  side-on weapon images. Legacy HUD scripts continue running while the new
  HUD supplies the layout and drawing.

- **A clearer opening dropship scene in The Awakening.** A wider rescue shot
  shows the reach and fall, the squad reacts to incoming fire, and Tak works
  on the jammed harnesses while the others evacuate. The escape camera waits
  for the aisle to clear. Engine trouble, exposed wind, cutting sounds and a
  clearer lighting change support the action. Restart m01 after updating;
  saves made with an earlier revision of this scene script may need a fresh
  map start. The campaign remains unfinished alpha content.

- **The Remastered HUD.** The gameplay HUD now follows your screen: the status
  bar centers on the bottom edge, the radio keeps to the top-right corner, and
  multiplayer shows a compact kill feed, standing, timer and chat. Vehicles get
  a centered status bar with an Exit prompt naming your jump key, the
  air-defence cannon keeps its amber lens and adds the radio display, The
  Awakening's dropship guns show the ship's status panel, its MCC cannon the
  MCC's hull and its speeder bike the race clock with a boost prompt, the
  flatbed and tram show their own gauges, objectives list every task with the completed
  ones below and each notice closes on its own, and the scoreboard is a card
  like the new multiplayer menus, with your place, ping bars and a Tourney
  bracket. The end-of-match summary is the same card: the result, the
  standings with everyone's best weapons, the chat with a message field you
  can type into, and its Statistics and Scoreboard tabs. After a multiplayer death, a Respawn
  prompt names your attack key once you can return. With a controller, every
  HUD prompt shows its button the way your controller labels it: A or ✕, LB or
  L1, the d-pad's arm. With Reduced Motion on, HUD warnings hold steady instead
  of pulsing. Spectators see name tags
  over the players in view of the free camera and step back through the
  players they follow with the weapon-cycle keys. The new **HUD** section of
  Settings > Game Options switches between Remastered, a Minimal HUD of health,
  armor, ammunition and the crosshair, a Competitive multiplayer HUD and the
  original Classic positions, and sets the HUD's size, width, safe area and
  opacity, and whether damage direction, pickup messages and the weapon strip
  appear; Toggle Objectives opens and closes the objectives with one press
  instead of holding the scores button. Reduced Flashing, in every layout, keeps
  the damage indicator and the vehicle hit glow at 40% at most and softens the
  white flash of teleporters and spawns. Competitive puts both sides' scores, the timer,
  the flags and your connection quality in one bar at the top, lists your
  team's health, armor and weapons at the left edge (both teams while you
  spectate) and enlarges the status bar. Typing a chat message now opens a
  matching entry field under the recent messages, with the channel, a cursor
  that keeps your text in view, the characters you have left near the limit
  and the Send, Channel and Cancel keys.

- **The new UI reaches the rest of the gameplay HUD.** Crosshairs, objectives,
  radio, powerups, boss and vehicle displays, multiplayer scores, chat and
  match summaries now use the new renderer and vector artwork. Awakening's
  vehicle overlays are covered too, and the Strogg implant activation in
  Medical Labs now lights its gold hexagon field across wide and portrait
  screens instead of a 4:3 box. Hit markers, damage numbers and the
  optional movement display also use the new renderer. Stock UI remains
  available with `ui_retained 0`.

- **Crosshairs that come alive.** Switch on `Settings -> Game Options -> HUD ->
  Animated Crosshair` and each weapon's original Quake 4 crosshair answers the
  weapon instead of sitting still. It assembles as the weapon comes up and
  kicks with every shot; the blaster's arcs wind shut into a ring as it
  charges, the railgun's bars come home as it recharges, the rocket launcher's
  halo sweeps round until the next rocket is ready, and the shotgun counts its
  shells back in. Hits land inside the design instead of over it, and vehicle
  guns get the same treatment. It is off by default and needs the new
  interface. See [Animated Crosshair](../../user/gameplay-settings.md#animated-crosshair).

- **A sharper gameplay status bar.** The new interface now centers vector ammo,
  health and armor gauges in campaign and multiplayer, with a Strogg variant
  after Kane's transformation. Values stay aligned as armor comes and goes,
  and the bar fits wide and portrait displays. `ui_hudScale` adjusts its size;
  `ui_hudSafeInset` moves it inward. Stock UI remains available with `ui_retained 0`.

- **Modern weapon wheels in campaign and multiplayer.** The new UI uses vector Marine/Strogg wheels with live ammo, fixed weapon positions and smooth scene soft focus that keeps the wheel and HUD crisp. The wheel keeps its familiar right-side placement; change **Wheel offset** beside wheel sensitivity in Game settings, or use `ui_weaponWheelOffset 0` to center it. Multiplayer keeps running at normal speed.
- Classic HUD gauges regain their beveled fills, armor cells and clipped Marine/Strogg traces, and stay on the centered 4:3 canvas. Its directional damage glow and missile chevron have their own shapes. Boss and timed notices use separate rows. Ammo gains highlight only the value that increased.
- Weapon wheels now use distinct Marine transmission and Strogg translation effects, reserve bands and accurate Awakening symbols. Controller selection follows the stick directly; Escape or the controller's B/Circle button cancels without also opening a menu or firing a bound action.
- Tourney keeps its arena instructions visible, names the current round, shows infinity during arena sudden death, and uses translucent banners that respect Reduced Motion. Clan Arena shows its timer throughout the round. Respawn prompts show the remaining delay, vote prompts use your controller's buttons, and live match details fold into a compact card. Warm-up shows a compact ready count, and wrapped chat stays within eight lines even when you are not typing.
- Strogg gauges keep their moving grain across the full fill, with a steady texture under Reduced Motion.
- **Quick chat in multiplayer.** Hold **D-pad Down** on a controller, or **V** on the keyboard, for a wheel of eight phrases such as *Attack!*, *Need backup!* and *Good game!*, then release over one to send it. In team games it goes to your team; press zoom while the wheel is open to send to everyone instead. Bind or rebind it under Settings > Controls > Weapons > **Quick Chat**. See [Quick chat](../../user/multiplayer-chat.md#quick-chat).

- **Radio graphics follow the dialogue.** Incoming transmission graphs now show
  the playing line's audio, including pauses between words. Classic retains
  its original animation.
- **A more consistent new interface.** Startup now uses the animated vector
  initialization screen. It opens with a quick zoom as its gears wind into
  place, then keeps turning smoothly while the game loads, and each loading
  step slides in instead of flickering. Slower systems still give most of
  the time to loading. Reduced Motion keeps it still.
  Campaign difficulty and Turbo options open on a
  fourth menu level with matching breadcrumbs and smooth Back transitions.
  The Settings hub includes a confirmation before restoring defaults and
  returns to the correct Marine or Strogg pause screen.
- **Settings pages that match the menus.** In the new interface, Controls,
  Game Options and Audio open as pages of the Settings menu, with the same
  frame, breadcrumbs and transitions as Multiplayer's pages, instead of
  full-screen panels. Long pages are split into tabs (switch with Q and E,
  the shoulder buttons or a click), and no page uses more than two columns.
  Game Options' tabs replace its Section list. Display is a page of the new
  interface too: Graphics, Screen and Image Quality tabs, with Apply Changes
  and the Keep or Revert countdown. A performance preset or Auto-Detect
  chosen there applies with Apply Changes. At larger text sizes the settings rows now grow with the text
  (switching to one scrolling column), and the button prompts at the bottom
  of every menu stay on screen.
- **Larger text across the menus.** With a larger text size (up to 200 %),
  the main menu, the pause menu and the Settings, Single Player, Campaign and
  Multiplayer menus in the new interface now keep everything readable and on
  screen: descriptions, the main menu's hint line, the pause menu's mission
  details and confirmation messages grow and make room for themselves, while
  menu buttons, titles and the button prompts keep their size so the layout
  stays intact. The lists of saves, demos, servers and maps now grow their
  rows with the text too, so names stay whole in their rows (a name too long
  for its column ends in "…"), and the selected save's or demo's details
  stand one under another above a smaller picture. The remaining pages
  follow too: in Create Server, Player Setup and the server search each
  setting's value moves under its label so nothing is cut off, and the
  difficulty pages, chapters, Mods list, objectives, server browser
  details, Arena and Credits grow their text while their buttons stay put.
- **Player Setup on tall screens.** On a portrait screen the player setup
  fields now span the screen at a readable size with your character shown
  large underneath, instead of everything squeezed into the top third.
- **Menus on tall screens.** On a portrait or rotated screen, the main menu,
  the pause menu and the Settings, Single Player, Campaign and Multiplayer
  menus in the new interface now fill the height: their buttons are larger
  and the description, hint line or mission details stand underneath instead
  of the whole menu shrinking into a strip.
- **Saves, demos and servers on tall screens.** On a portrait or rotated
  screen, Load Game, Save Game, Demos, Create Server and Join Game in the new
  interface now stack into one column at a readable size: the selected save,
  demo or map with its picture and buttons on top, then a long scrolling
  list whose entries take two lines, and larger buttons and dialogs. Restart
  Level, the campaign difficulty choice, the Arena pause menu, the restart
  screen and the credits fill the height the same way. The Arena campaign's
  page shows the tiers with their briefing and progress, then the matches
  with the difficulty and START MATCH, the chosen match's picture and details
  beside them, all larger. The main and pause menus, Settings, Single Player,
  Campaign and Multiplayer take this layout on any portrait screen that has
  room for it, so a nearly square window or an unfolded foldable no longer
  shows the menu in a strip between empty bands.
- **Join Game picture.** Before you choose a server, Join Game's picture
  frame shows the Quake 4 art instead of an empty black box.
- **Demos under Multiplayer.** In the new interface, DEMOS is now a section of
  the Multiplayer menu: it opens your recordings one level deeper, with the
  same breadcrumb and transitions as Join Game and Create Server, and Back
  returns to it. The title screen's bottom links are now Mods, Updates,
  Credits and Exit. Player Setup, also on the Multiplayer menu, edits your
  name, clan tag, model and rail colour in place beside a turning preview of
  your player, and each change applies at once.
- **Spectate from the server list.** In the new interface, Join Game has a
  **Spectate** button under Join Server that joins the selected server as a
  spectator. With a controller, press the left stick to spectate and the
  right stick to add or remove the server from your favorites; the stick
  symbols show beside the two buttons.
- **Menus explain their sections.** In the new interface, the Single Player,
  Campaign, Multiplayer and Settings menus describe the section you point at
  or select beside the list, with a picture where one fits: the level of your
  latest save for Campaign, the next Arena match, the campaign's first level
  for New Game, the furthest level you have reached for Chapters, and the map
  Create Server will host.
- **Campaign: New Game and Chapters.** In the new interface, Single Player →
  Campaign now has you choose Quake 4 or The Awakening (the one you are
  playing is already chosen), then **New Game** for the difficulty and Turbo
  Mode, or **Chapters** to restart from any level you have reached, each on a
  page of its own with Back returning to where you were. The classic menus
  keep everything on the campaign page.
- **Right stick in the menus.** With a controller in the new interface, the
  right stick scrolls the pause menu's objectives, the multiplayer scoreboard
  and player list, and long confirmation messages without moving your
  selection, and in Player Setup it turns the preview of your player, with
  the Model rotation slider following.
- **Menus on a touchscreen.** On a touchscreen (a phone, a tablet or the
  Steam Deck's screen), you can drag the lists of saves, demos, servers,
  chapters and settings with your finger, and a flicked list keeps going and
  glows at its top or bottom when you reach the end; sliding your finger no
  longer presses the button where you let go; a swipe across the settings or
  multiplayer tabs changes tab; holding your finger on a server in Join Game
  shows its details; and the menus no longer show a mouse cursor or leave a
  button lit where you touched.
- **Menus and HUD clear of the notch and system bars.** On a phone or tablet
  whose screen has a camera cutout, rounded corners or status and gesture
  bars, the new menus keep their titles, buttons, lists and prompts clear of
  them, while their backgrounds still fill the whole screen. The Remastered
  and Minimal HUDs keep their readouts clear of them too, while scopes and
  other full-screen effects still cover the whole screen; your Safe Area
  Inset still applies where it reaches further.
- **Clearer list selection.** The selected save, demo, server or map in the
  new interface's lists no longer doubles its orange highlight under the
  pointer, so its name stays readable.
- **Settings on tall screens, and exact values.** On a portrait or rotated
  screen, Controls, Game Options and Audio in the new interface now use the
  height: their rows stand in one larger, scrolling column instead of
  shrinking. Mouse sensitivity, CPI, acceleration, gyro and touchpad
  sensitivity and the weapon's field of view and position also take a typed
  value, and a value outside the allowed range says what that range is.
- **A sharp menu pointer, with a text cursor.** The new interface's pointer is
  a smooth redraw of Quake 4's shaded arrow that stays crisp at any
  resolution, and over a text or number field it becomes a matching I-beam.
  It stays on screen whenever you use the mouse or keyboard in a menu and
  hides only while a controller is driving the menus. The console uses the
  same pointer, with the I-beam over its command line.
- Player previews apply separate head skins and keep attachments aligned when
  their position changes.
- **Clearer focus and cinematic transitions.** All three scopes keep the aim
  area sharp while softly defocusing the surroundings. The MCC treatment
  opening regains focus gradually. Kane's crash awakening in Air Defense
  Trenches now has soft eyelid shading and a hesitant return to focus, following
  the original wake-ups and blackouts. Death briefly loses focus, drains colour
  and darkens gradually at the edges while keeping the HUD readable. Reduced
  Motion and Reduced Flashing suppress the transient accents. Level changes
  fade briefly to black before loading.
- **Smoother menus and wider loading screens.** Moving the pointer across the
  new main menu keeps its scenery motion without stuttering the vector frame.
  Loading objectives and progress now use the left and right screen edges,
  including ultrawide and portrait windows.
- **Vector weapon scopes.** The new interface includes machinegun, railgun and
  nailgun scopes with live compass, ammo and lock feedback. Optics keep their
  shape while their surrounding effects cover wide and tall screens.
- **Flesh sounds like flesh.** Bullets now use monsters' original flesh and
  armour maps when selecting impact effects and sounds, correcting metal hits
  on exposed flesh while keeping metallic impacts on armour.

- **Choose your story campaign.** Single Player → Campaign offers Quake 4 and enables The Awakening when its single-player content is installed. The campaigns keep separate saves and settings; returning to Quake 4, Arena or multiplayer removes expansion overrides.
- **One source checkout.** Engine and SDK game sources now build together in openQ4. Developers no longer need the two companion repositories. The engine retains its GPL terms; game sources retain the SDK EULA.

- **Vulkan renderer supported on Windows.** The Vulkan renderer is now a
  stable alternative to OpenGL on Windows instead of an experiment: it draws
  the stock campaign and multiplayer with OpenGL's effects, recovers on its own from graphics driver
  resets, and has been tested on NVIDIA and Intel graphics across every stock
  single-player map and 20 multiplayer maps. Saved OpenGL choices stay, and
  new desktop installs start on it through Automatic, with OpenGL as the
  fallback. Vulkan stays experimental on Linux and macOS. See
  [Renderer Backend](../../user/display-settings.md#renderer-backend-automatic-selection-vulkan-stable-on-windows).
- **Cleaner outdoor shadows.** Shadow mapping removes repeating terrain lines in Sandstorm and restores terrain brightness and debris shadows in Air Defense 1's opening, on OpenGL and Vulkan. Keep cascaded shadow maps enabled for distant outdoor lighting.
- Railings and other objects first seen at a distance now regain their mapped shadows as you approach, fixing missing entrance shadows in Air Defense 1.
- **Shadow maps come on with the top presets.** Choosing **Quality** or **Ultra** in Settings > System now turns shadow maps on. Shadows from projected lights, such as the flashlight, stay sharp where an object touches a surface and soften with distance. Lower presets keep Quake 4's original shadows.
- **Sharper, smoother image at lower render scales.** When the 3D scene renders below your screen's resolution, as the **Performance**, **Low Power** and **Minimum** presets do, temporal anti-aliasing now rebuilds it at full resolution instead of stretching it. Edges stay smooth and fine detail returns. It also tracks animated characters and your weapon exactly while they move, so they no longer shimmer or lose their anti-aliasing. At native resolution nothing changes unless you turn it on with `r_temporalAA 1`. See [Temporal AA](../../user/temporal-presentation.md).
- **Physically based materials for mods.** Maps and mods can now ship PBR materials that render by default on OpenGL and Vulkan, beside the stock surfaces. Their metalness, roughness and ambient occlusion shape how every light (shadows included), the map's ambient light and its baked light grids fall on them. Glow maps shine on their own, and SSAO darkens only their bounced light. Reflection probes can follow a room's walls and be captured in the game. Stock content has no PBR materials and looks exactly as before. See the [PBR materials guide](../../user/pbr-materials.md).
- **Smarter multiplayer bots you can give orders to.** Bots plan safer routes around jump pads, ledges and hazardous floors, recover when knocked off their route, and make running jumps across gaps. They hear gunfire and footsteps, time armour and weapon respawns, back off from fights they can't win and go hunting while they have quad damage. In team games you can give the bots on your side orders in team chat, such as `kane defend`, `everyone follow me` or `sorg hold here`, and they answer in character. Navigation and decision-making also use less processor time. See [Giving bots orders](../../user/server-setup.md#giving-bots-orders).
- Bots account for their health and armour before taking drops or jump pads, and check the ground beyond a touchdown before continuing. Running jumps check their current position and momentum against the player's collision shape, and bots avoid walking through jump pads that have no safe landing route.
- **German menus and gameplay text are now available.** Choose **Deutsch** in Settings > Game Options > Language for translated settings, objectives, terminals, Arena Campaign, demos and multiplayer controls.
- **Five more languages: Brazilian Portuguese, Czech, Hungarian, Turkish and Ukrainian.** Choose **Português (Brasil)**, **Čeština**, **Magyar**, **Türkçe** or **Українська** in Settings > Game Options > Language. Each translates the same menus, settings, objectives, terminals, Arena Campaign, demos and multiplayer messages as German. On its first launch openQ4 picks your system language. See the [languages guide](../../user/languages.md).
- **Play in VR (experimental).** With a PC VR headset and SteamVR, Meta Quest Link or Monado, on the OpenGL or Vulkan renderer, enter `vr_enable 1` and `vr_restart` to play the campaign or multiplayer in stereo at your headset's resolution. Look around with your head, aim and shoot with a motion controller while a laser dot shows where your shots land and the controllers vibrate as you fire and take hits, and point at menus to click them; each gun sits full size in your hand, gripped where its own model holds it and pointing along the laser, and you can steady it by holding the foregrip with your other hand; the HUD floats in view, and menus, loading screens and cinematics appear on a floating screen. You can walk around your room and your character steps with you, in multiplayer too, and crouching in your room crouches your character; play standing or seated, with your height measured from the floor or from where you recentered. Snap turning, a comfort vignette that darkens the edges of your view while the stick moves or turns you, and other comfort settings are on by default. See the [VR guide](../../user/vr.md).
- **Cleaner, more complete text.** The scalable fonts now draw symbols Quake 4's fonts never had, such as ½, ², º, ± and the non-breaking space, instead of question marks. Accents no longer run into capital letters in terminal and console text, and every accented letter keeps Quake 4's own lettering. Console text has clean edges instead of stepped ones, and no longer shows the German **ß** as **á**.
- **Effects closer to the original.** Particle effects now spawn, move, fade and draw as they do in the original Quake 4. Electrical arcs regain their bright, forked look. Steam and smoke placed in maps are already flowing when you first see them. Long-running effects no longer slow down or fade early.
- **Rooms sound the way Quake 4's level designers set them up.** Every map's rooms, corridors and outdoor areas get their own reverb again, as the original game did with EAX sound hardware: a tiled washroom rings, a hangar booms, and a firefight in the hall next door sounds like that hall, coming from its doorway. Closed doors muffle the sounds behind them, radio chatter stays dry, and voice-over sits back in the mix. Until now openQ4 played no reverb at all. It is on by default (`s_useEAXReverb`) wherever OpenAL Soft is in use; see [Room Reverb](../../user/gameplay-settings.md#room-reverb).

## Upgrade Notes

- If you compiled a map with an earlier openQ4 `dmap` and it uses vehicle, fly,
  shot or item clip brushes, compile it again so its collision file records
  them correctly.
- Multi-view demos recorded by this build use MVD 1.3. Earlier openQ4 builds
  that play 1.2 demos still play them, without markers, keyframes or late
  joiners' names. Replace the engine and both game modules together (game API
  58).
- New campaign saves use format 4 to preserve frozen enemies, Retch buffs and
  animation speeds. Supported format 3 saves still load on the same platform;
  an enemy saved as solid ice gets a fresh four-second shatter countdown.
  Keep your old slots if you plan to return to an earlier openQ4 version:
  those builds cannot read new format 4 saves.

- Install matching engine and `baseoq4` SP/MP modules together. Awakening uses the same SP module; older `q4xbase` DLLs are ignored. Supply expansion assets separately, following the [campaign guide](../../user/campaigns.md). Awakening remains unfinished alpha content, and its multiplayer changes are excluded.
- The repository and package include a component licensing notice and the original SDK EULA. Source consolidation does not relicense SDK-derived code or establish new distribution permission.

- The preview macOS packages target Apple Silicon/arm64. Intel Mac packages are not published, and Rosetta is not supported. macOS uses a Metal bridge around the OpenGL renderer, not a native Metal renderer. The legacy native backend (`platform_backend=native`) remains comparison-only diagnostic infrastructure, not a release backend. Qualification remains tracked in [docs/dev/macos-signoff-evidence.md](../macos-signoff-evidence.md).

- Replace the engine, renderer modules and both SP/MP game modules together.
- Quick chat's default controls, D-pad Down and V, apply to new configurations, where D-pad Down no longer centers the view (clicking the right stick still does). Existing configurations keep their bindings: bind **Quick Chat** under Settings > Controls > Weapons.
  The integrated UI and renderer interfaces require matching modules;
  older modules are rejected.

- The retained title, pause and loading screens are enabled by default
  (`ui_retained 1`). Set `ui_retained 0` to use the stock screens; your saved
  preference is kept. The experimental SYSTEM page and multiplayer menu card
  remain opt-in. The full interface and visual editor replacement is unfinished.

- Presets apply only when you choose one, so an existing **Quality** or **Ultra**
  setup gains shadow maps after you select the preset again. Shadow filtering
  you never changed moves to the new softer default once; a filter you chose
  yourself is kept.

- Temporal anti-aliasing now defaults to automatic (`r_temporalAA 2`), which
  only acts below native resolution. A profile saved by an earlier version moves
  to it once; set `r_temporalAA 0` afterwards to keep the plain upscale, and
  that choice is kept.

- PBR materials are now on by default, and a profile saved by an earlier
  version switches them on once. To keep a mod's classic look, set
  `r_pbrMaterials 0` after the first launch; that choice is kept.

- A profile saved by an earlier version that asked for 5.1 surround moves to
  the new **Auto** speaker layout once, which still gives a 5.1 system its 5.1.
  A saved stereo choice is kept. Radio chatter and distance falloff move to the
  original game's values once.

- Room reverb now stays on in every performance preset. A profile on the
  Minimum, Low Power or Performance preset with reverb off has it switched back
  on once.

- Update the complete openQ4 package, including its bundled data. German text also works with English retail assets.
- For German campaign voices and subtitles, install the complete retail German language pack and restart the game. Numbered language patches alone do not contain the complete dialogue; English dialogue remains the fallback when German assets are absent.
- Brazilian Portuguese, Czech, Hungarian, Turkish and Ukrainian are text-only. Quake 4 never had voice packs for them, so characters speak English.
- VR works on the OpenGL or the Vulkan renderer with an OpenXR runtime that supports it, such as SteamVR, Meta Quest Link or Monado. Windows Mixed Reality headsets work through SteamVR. On Vulkan, turning VR on the first time restarts the renderer once so the runtime can pick your headset's GPU. On a Linux Wayland desktop, use Vulkan, or OpenGL with a runtime that supports it there (Monado does; with SteamVR start openQ4 with `OPENQ4_FORCE_X11=1`). VR is new and has not yet been confirmed on headset hardware, so please report how it runs on yours. It is off unless you turn it on.
- Czech, Hungarian, Turkish and Ukrainian always use openQ4's scalable fonts. They ignore `r_useTrueTypeFonts 0`, because Quake 4's original bitmap fonts lack their letters. Chat and the console cannot yet type Cyrillic letters, or the Czech, Hungarian and Turkish letters outside the Western European set.

## Change Log

- A level can no longer end with "cannot load collision model" during heavy
  effects. When a burst of effects filled the game's table of client-side
  effects and debris, a piece of debris that did not fit stopped the level
  with a fatal error; the second trench battle in Air Defense Trenches could
  trigger it. The piece is now skipped. This affects single-player and
  multiplayer game modules; replace both.
- On Linux, a long single-player session no longer grows the game's memory use
  with every level change. Memory the game had already freed stayed reserved,
  about 60 MB per level, so a session that visited many levels could use
  gigabytes more than it needed. It is now returned after each level loads,
  which takes a few hundredths of a second.
- Dedicated servers change maps faster: they no longer read every texture of
  the next map in the background only to discard it, about a quarter of each
  map load on stock multiplayer maps.
- Compiling a map with heavily subdivided patches, such as a reworked
  Air Defense 1 that previously took days, now finishes in minutes. Stock maps
  compile faster too, to identical output.
- A navigation file with damaged tactical data loads without those positions
  and a warning, instead of letting AI read past the end of the data.
- On Vulkan, temporal AA keeps working while a mirror, security-camera screen
  or refraction effect is in view; it used to switch off for those frames.
  Mirrors are also drawn without temporal AA's sub-pixel jitter.
- On Vulkan, a display that stops accepting frames no longer stalls the game
  for about half a minute per frame.
- Vulkan screenshots now show froxel fog, screen-space reflections and
  screen-space GI as displayed.
- Authored reflection probes (experimental, off by default) no longer hitch
  the game when they first come into view. Each probe is prepared in the
  background and appears a fraction of a second later; screenshots wait for
  it. `r_pbrProbeAsyncPrefilter 0` restores the old behaviour.
- A long multiplayer chat message with an icon in it no longer repeats the
  icon at the start of the next line when it wraps.
- On a first launch that starts a map from the command line or a shortcut
  (for example `+devmap`), the game goes straight to the map instead of
  sometimes opening the main menu first. The menu used to win on a slow first
  frame and loaded player models and crosshairs late.
- Demos recorded on servers without cheats can be paused, slowed and sped up
  again, and the playback timeline follows the position. Playback took over
  the server's cheat setting, which locked those controls.
- The bundled libraries are updated to their latest releases. SDL 3.4.18
  adds support for the Flydigi Apex 6 controller, stops the Keychron Q6
  System Control from appearing as a gamepad, and fixes a startup crash on
  PCs with a broken EZFRD64.DLL installed. The OpenXR loader 1.1.65 copes
  better with broken API-layer registry entries on Windows. The Vulkan
  headers and memory allocator, the shader compiler and the JSON parser
  are current too. Replace the whole package as usual.
- The experimental Android build now uses OpenAL Soft 1.25.2, the same audio
  library release as the desktop packages, built with the current Android
  NDK (r30).
- Experimental Android: you can now play holding the phone upright. Choose
  **Landscape and portrait** under **Screen orientation** on the Interface
  setup page; the game then turns with the device (unless Android's rotation
  lock is on) and keeps your match running. Upright, the touch controls gather
  in the band below the view, with movement on the left and fire, weapon,
  jump, crouch, zoom and reload on the right, and the Remastered HUD's
  readouts sit just above them. Landscape stays the default and is now held
  reliably: the engine's window could previously ask Android for every
  orientation.
- Multiplayer bots on Linux and macOS no longer risk a crash when a player
  slot is empty. The bot code checked for a missing player in a way those
  systems' compilers are allowed to skip.
- For modders: the `printEntityDef` console command now prints the entity
  definition's keys and values, not just where it was defined.
- For modders and server hosts: a mod with an incomplete `mod.json` is now
  ignored cleanly. The game runs normal openQ4 instead of falling back to
  retail-only content with missing text and menus. A server in that state
  also accepts players who don't have the mod.
- Connecting to a retail Quake 4 1.4.2 server now says why it cannot work:
  **Network protocol mismatch**, with both protocol versions. openQ4 used to
  stop with a misleading message about a missing `q4mp` mod. Retail players
  who try an openQ4 server are now told `server uses protocol 2.41` instead of
  waiting for an answer that never came.
- The LAN server list finds IPv6-only servers hosted on Windows. Those servers
  never heard the IPv6 scan, so only IPv4 or a typed address could reach them.

- The menu pointer reaches every edge of the screen again. Moving the mouse
  while a menu screen was still opening, or out past the edge of the window,
  could leave the pointer stopping short of the far edges until you swept it
  hard against the opposite side; it now catches up with the mouse on its
  next move.

- The Awakening's freeze gun now works on one-health enemies and respects boss
  invulnerability. Saving preserves frozen enemies and their shatter timer;
  thawing keeps the Pain Lord's enraged speed. Retch buffs survive chilling
  without duplicating or erasing ice effects.
- Cockpit missiles lock onto visible enemies within range, and distant targets
  no longer block closer valid ones. Rockets and markers use the intended aim
  points on large enemies. The Goob gun and Napalm Gun report their ammo-ready
  state correctly.

- openQ4 starts more than a second faster. It used to read the whole of its
  800 MB content pack at every launch to check that the pack was intact. It
  now remembers a pack that passed the check and reads it again only after the
  file changes, so a missing or modified pack is still refused. The first
  launch after installing or updating checks the packs as before. To check
  them at every launch, start openQ4 with `+set fs_cacheVerifiedPaks 0`.

- The multiplayer menu's Admin page now accepts the server's remote-console
  password: Accept checks it with the server and opens the admin controls
  instead of waiting with no answer. A wrong password returns to Join Team,
  as in Quake 4, and the Admin console now shows the server's replies. See
  [Server and Remote-Console Security](../../user/server-security.md#the-in-match-admin-page).

- Create Server's **Server Type** and **Dedicated** settings now take effect,
  as in the original game. **LAN** keeps your server off the Internet master
  servers and admits only players on your network. **Dedicated** hosts without
  you playing: the game window closes and the console window runs the server
  until you close it, and you can start openQ4 again to join it. See the
  [Server Setup Guide](../../user/server-setup.md#hosting-from-the-game).

- You can also start openQ4 as a dedicated server without opening the game,
  as you could the original: add `+set net_serverDedicated 1` to its command
  line, with `+spawnServer` to start serving at once. The console window runs
  the server, as with **Dedicated**.

- A server you host keeps the network address and port you set with
  `net_ip`, `net_ip6` and `net_port`. Starting a match reset them, so the next
  server you hosted, or a server reloading for an addon map, listened on every
  network interface and the default port.

- The classic end-of-match summary (with the retained menus off) marks your
  row with the orange corner marker, on its Scoreboard tab too, and tints the
  top three deathmatch players blue, red and yellow again, as the original game
  did. Its prompt names the key that takes you back on the device you last
  used, and Arena One Flag CTF, Clan Arena, Freeze Tag and Red Rover matches now
  fill its Scoreboard and Statistics tabs. The classic scoreboard greys out
  eliminated Tourney players again.

- On a full server the scoreboard no longer runs past its footer: hold it and
  scroll with the mouse wheel, your weapon-change buttons or the right stick,
  and a count shows how many players are further down. Weapon changes while
  you hold it work as before when everyone fits.

- VR now runs on the Vulkan renderer too. The OpenXR runtime creates the
  renderer's device on the GPU your headset uses, and the headset keeps
  rendering while the desktop window is minimized or covered. On a Linux
  Wayland desktop, OpenGL VR binds through EGL where the runtime supports it.

- Standing and seated VR play spaces (**Play Space** in the Virtual Reality
  tab, `vr_playSpace`). Standing play measures your height from the floor, so
  recentering while you crouch or sit no longer shifts your standing height;
  `vr_calibrateHeight` measures it again. Seated play measures from where you
  recentered, as before. Automatic picks standing when your headset knows the
  floor.

- In single player, door panels, lift calls and other screens in the world
  answer the VR weapon hand where it points, with their cursor where the laser
  meets them.

- Temporal anti-aliasing in VR: set `r_temporalAA 1` and each eye smooths its
  edges from its own previous frames, in single player and multiplayer, on
  OpenGL and Vulkan. A fast head turn starts afresh instead of smearing. The
  automatic setting still leaves it off in VR.

- On Vulkan, the sky and distant scenery behind a level (its portal sky) no
  longer go missing from the left eye in VR.

- The tabs at the top of the settings pages respond to the mouse and the VR
  pointer again; clicking one used to do nothing. In VR, the weapon-hand and
  off-hand grips also step between tabs.

- VR settings have their own section in Settings > Game Options, so you can
  switch VR on and set aiming, turning, movement, the laser sight,
  vibration, the two-handed grip, the comfort vignette and physical
  crouching from the menu,
  including by pointing at it in the headset.

- The Section selector at the top of Game Options and the display settings
  now works with the mouse: clicking it used to snap back without moving.

- New experimental OpenXR VR mode for Windows and Linux. Each eye renders with
  the headset's own lens projection, and controllers use the gamepad bindings,
  so rebinding works as usual. A laser dot (`vr_aimLaser`, with an optional
  beam) marks where controller-aimed shots land, at the target's real depth,
  and the controllers vibrate on each shot and hit (`vr_hapticStrength`).
  Each weapon is held by its own grip at full size with its barrel along the
  controller's aim; `vr_weaponOffsetX/Y/Z` and `vr_weaponPitch` now default to
  0 and only fine-tune that hold. Squeezing the off-hand grip on the gun's
  foregrip holds it in both hands (`vr_twoHanded`); anywhere else that grip
  still opens the weapon wheel. A comfort vignette (`vr_comfortVignette`)
  darkens the edges of each eye during stick movement and smooth turning,
  never for real steps about the room. Zooming a scoped weapon magnifies the
  view by the scope's own amount, with the laser dot as the crosshair.
  Crouching in your room crouches your character (`vr_physicalCrouch`)
  without lowering your view twice. In single player the hovertank, the
  walker and the mounted guns surround you in stereo too
  (`vr_vehicleStereo`): you sit where the turret turns, it turns where you
  look, and the trigger fires. Scripted rides, from the tram's rear gun to
  the drop pods, carry you in stereo as well, with the horizon kept level.
  In menus the weapon-hand controller points and clicks. Temporal anti-aliasing and dynamic resolution switch off while VR
  presents. `vr_status`, `vr_recenter` and `vr_restart` help with setup, and the
  desktop window mirrors the left eye by default.

- Headless and hidden-window launches no longer display the startup splash screen.

- Hidden-window Vulkan runs (`r_hiddenWindow 1`), such as automated
  screenshot captures, no longer slow to about a frame a second while the
  display is off or the PC is in standby. They now render offscreen at an even
  frame rate. Visible windows are unchanged.

- Temporal anti-aliasing was reworked. Animated characters and your weapon are
  now tracked exactly, like doors and lifts already were, so they keep their
  anti-aliasing while they move. Smoke, sparks, screens and mirrors only affect the pixels
  they cover, where before one visible effect could switch history off for the
  whole view. Edges are steadier, old frames are resampled more sharply, and
  `r_temporalAASharpness` (0.5) sharpens the final image. `r_temporalAA 2`, the
  new default, uses it only to upscale scenes rendered below native resolution.

- The experimental GPU skinning (`r_gpuSkinning 1`) now saves the CPU work it
  replaces on Vulkan, instead of also deriving every animated model's normals on
  the CPU. Effects that inflate a GPU-skinned model, such as power-up shells, now
  expand along its real surface. It stays off by default: Quake 4's own models
  are light enough that the CPU skins them in a few microseconds per frame.

- On Vulkan, the driver's compiled pipelines are now saved before every map
  load, not only when you quit, so a crash no longer discards what earlier maps
  compiled. `gfxInfo` reports how many pipelines were created and how long they
  took.

- Maps load faster on Windows: about 9% faster for Air Defense 1 on the test
  machine (14.3 s to 13.0 s). Looking up a file no longer tries to open it in
  every empty mod and install folder first, which happened about 60,000 times
  per load, and checking whether an image is up to date no longer opens it
  inside its pk4.

- Maps load faster again, on every platform: about 15% faster for Air Defense
  1 with OpenGL on the test machine (12.4 s to 10.6 s), and about 3 s faster
  with Vulkan. openQ4 fingerprints every texture it loads, and now does it with
  the processor's SHA instructions, which every AMD Ryzen, Intel Core
  processors from the 11th generation on and every Apple Silicon Mac have.
  Other processors get a faster fallback too.

- With OpenGL, maps load about 7% faster still: each compressed texture is now
  set up on the graphics card once instead of being sent to the driver twice.

- Maps load about 2 seconds faster again on the test machine (Air Defense 1),
  with OpenGL and Vulkan: while one texture uploads, openQ4 now unpacks and
  checks the next ones on other processor cores. `image_prefetchLevelImages 0`
  switches this off.

- A map no longer fails just as it finishes loading, sending you back to the
  menu with "UnCrop: currentRenderCrop < 1". It happened only now and then,
  mostly with `developer 1` or `fs_debug 1` set, such as while baking light grids.

- Local builds correctly refresh engine/game interfaces after in-tree source
  updates, preventing incompatible modules from surviving an incremental build.

- Existing baked map lighting remains compatible with the updated image loader.
  Older generated texture caches are rebuilt automatically when needed.

- Light grids baked with `bakeLightGrids` now come out right at any window
  size. Before, only a 640x480 window baked correctly: in any other window each
  probe recorded a magnified corner of its surroundings, so its baked indirect
  light could come from the wrong direction. Re-bake maps you baked yourself to
  pick up the fix.

- Baking light grids no longer runs out of memory on large maps such as Air
  Defense Trenches: memory used to grow with every probe face the bake
  captured. Re-baking a map that already ships a light grid now builds every
  bounce, and the pack it writes, from the new bake instead of the bundled
  grid, and the bake says when the bundled copy will still load first.

- Maps whose curved surfaces use fine explicit subdivision no longer compile
  into millions of invisible triangles. The map compiler kept splitting
  zero-width slivers, which made a modified Hub 2 too large to load
  ([#181](https://github.com/themuffinator/openQ4/issues/181)); it now builds
  in about two minutes and plays. Recompile affected maps.

- Very detailed surfaces in custom maps and models now keep their whole
  shadow outline. The renderer used to stop at 65,536 shadow edges per surface
  and drop the rest, so such surfaces could cast broken stencil shadows. No
  stock map or model came near that limit.

- With the experimental `com_levelLoadModernization 1`, map and model caches
  now come out the same on every load and match a load from the original files.
  Caches from earlier versions are rebuilt the first time each map loads.

- The experimental SYSTEM page adds precise window and custom fullscreen sizes.
  Invalid values stay editable, and unsupported display combinations cannot be
  applied. Enlarged labels and validation messages keep the focused value
  visible. Window resizing uses Keep/Revert confirmation.

- The experimental modern SYSTEM page adds fullscreen, borderless, fullscreen
  policy and MSAA controls with Apply/Keep/Revert confirmation. MSAA changes
  through this page currently require an OpenGL backend.
  Slow video reloads now receive their full presentation wait before
  confirmation or recovery can time out.

- Enlarged SYSTEM brightness and ambient-light fields keep their full numeric
  values visible when focused. Slider tracks stay centered beside the values.

- Text in the experimental modern UI now rasterizes at its displayed size,
  improving enlarged text and fractional UI scaling. Cached glyphs stay stable
  across views and rebuild after language or video changes.

- The experimental modern UI supports larger text independently of its framing.
  Set `ui_retainedTextScale` from `1` to `2`; the default is `1`. SYSTEM fields
  and dialogs grow with text, long messages scroll while actions stay visible,
  and translated dropdowns remeasure after widening. Changing status messages
  keep the active control visible while preserving deliberate scrolling.
  SYSTEM offers UI scale (75–200%), text size (100–200%) and Reset UI sizes.
  Apply saves both preferences; Discard keeps their previous values. Large sizes
  fit the window to keep reset and essential actions reachable.

- Modern SYSTEM settings show the correct localized status and error messages,
  including Ready, unsaved changes and pending confirmation.

- The experimental modern UI now respects transparent cutouts when selecting
  controls and scrolling panels. Fully masked panels no longer retain keyboard
  focus; partly transparent controls and drags started in visible regions remain
  usable.

- Improved Vulkan startup on systems with multiple GPUs: automatic
  selection now skips adapters missing required rendering or presentation
  capabilities. The preliminary startup check handles changing device lists
  and preserves explicit GPU choices when the chosen adapter is unavailable.
- Vulkan now starts on your next compatible GPU when the first one fails to
  create its device or resources, instead of dropping back to OpenGL. A GPU you
  chose with `r_vkDevice` is still never swapped for another one.
- Vulkan screenshots and environment captures no longer save a blank, cleared
  or garbled image when a capture fails or the window changes size mid-capture;
  the game reports the failure and writes nothing.
- Mods with many ARB material programs no longer stop the game on Vulkan
  when the program table fills; the extra effects are skipped with a warning.
  Custom shader effects that cannot run are now reported once with the reason,
  and live shader editing no longer runs out of room after many edits.
- Vulkan timing traces now help distinguish rendering work from waits when
  diagnosing stutters. Performance qualification remains in progress.
- Storage1 performance tests now distinguish the opening sequence from a
  settled gameplay view, helping avoid misleading renderer comparisons.
- Improved point-light shadow-edge consistency in Vulkan at
  native rendering resolution. Broader shadow compatibility remains in testing.
- Vulkan can now render an initial subset of custom GLSL material
  effects, including texture sampling, material colors, vertex movement and
  per-light effects with stencil shadows. Supported effects can also override
  stock shader names. Broader shader compatibility and complete rendering
  qualification remain in development.
- Material authors can reload supported GLSL effects with `reloadGLSLprograms`
  on OpenGL and Vulkan, including after repairing a broken shader. The existing
  `reloadARBprograms` command now reloads GLSL material effects too.
- The Vulkan renderer now carries on when the graphics driver resets during
  play: the renderer restarts itself (up to three times per session) instead of
  leaving a frozen window. A lost window surface is rebuilt in place, a display
  change that removes your chosen vsync mode no longer stops presentation, and
  changing shadow quality mid-game is safe.
- The Vulkan renderer runs mods' ARB assembly material effects, matching
  OpenGL, and custom GLSL effects that sample rendered textures now draw them
  the right way up. Vulkan also gains `r_showShadows`, `r_enhancedMaterials`,
  and the sharpened and nearest-neighbour `r_resolutionScaleMode` upscales, and
  its `benchmark` timings now include the GPU's work, as OpenGL's do.
- The Vulkan renderer now draws OpenGL's colored translucent shadows
  (`r_shadowMapTranslucentMoments 1`), so glass-like and other blended
  surfaces tint the light that passes through them.
- Fixed Vulkan MSAA on GPUs that cannot move their sample
  positions, such as Intel integrated graphics: they now keep their own
  standard pattern instead of relying on a feature they lack.
- Fixed Vulkan shadow maps with MSAA reading the wrong image when an object
  moves through a light whose shadow is cached. Graphics-driver validation
  reported this as an error.
- On Vulkan, Brightness (`r_brightness`) and `r_gamma` now change the picture
  by the same amount as on OpenGL, across gameplay, the HUD, menus and
  screenshots.
- `r_resolutionScaleMode 2` (sharpened) and `3` (nearest-neighbour, a crisp
  pixelated look) now change the picture in the single-player campaign whenever
  Resolution Scale is below 100%, on OpenGL and Vulkan, with or without MSAA and
  SMAA and while the weapon wheel blurs the scene. Before, the campaign always
  used mode `1`'s smooth upscale whatever the setting. OpenGL multiplayer gains
  mode `3` as well.
- Fixed missing surface normals and tangents in ordinary OpenGL custom GLSL
  stages, which could make direction-dependent effects appear flat.
- Characters and weapons converted by the opt-in `r_convertMD5toMD5R` now
  animate, light and cast shadows like the original models on OpenGL, instead
  of drawing as black shapes with no shadows. Earlier conversions are rebuilt
  automatically the first time they load.
- Turning on the opt-in `r_convertStaticToMD5R` no longer crashes OpenGL as
  soon as the map appears, and converted props no longer light their
  two-sided surfaces twice. Converted static models now look like the
  originals on OpenGL and Vulkan.
- Vulkan now keeps explicitly selected linear/nearest texture filtering stable
  across image reloads. Custom lighting also retains solid surface coverage
  when it uses the standard vertex transform.
- Fixed an OpenGL texture-binding conflict between material rendering and
  projected shadow maps.
- Fixed large, wrong shadows on walls and ceilings lit by projected lights,
  such as the flashlight, in the experimental OpenGL PBR preview with shadow
  maps on. Since the softer shadow filter became the default, the preview
  spread these shadows so widely that surfaces nothing blocked turned dark.
- On OpenGL, shadows from projected lights no longer shift slightly when a
  light's shadow map is redrawn, and the shadow cache keeps room for more
  lights.
- Walls and floors lit at a shallow angle by a projected light, such as the
  flashlight, no longer show speckled shadow "acne" with shadow maps on (the
  **Quality** and **Ultra** presets), on OpenGL and Vulkan. Shadows still meet
  the objects that cast them.
- Added an experimental level editor. Run `editorExperimental` to edit Quake 4
  maps: brushes, patches and entities in 3D and top, front and side views,
  with docked panels, typed properties, validation, layers, groups, prefabs,
  sandboxed builds and play tests. Brushes and patches keep their exact source
  text until you change them. The legacy Radiant editor and its command are
  retained for legacy tool builds. Update the bundled data along with the
  engine to receive the editor interface and its translations.
- Compiling a map that leaks no longer aborts with "Couldn't open ... .lin"
  when the map's folder does not exist beside the engine; the leak trail is
  written beside the compiled map.
- Fixed vertically flipped Vulkan scenes when presenting a reduced-resolution
  render texture through an ordinary material, including PBR with bloom and HDR.
- Fixed upside-down Vulkan gameplay with SMAA enabled, including PBR scenes.
  Graphics diagnostics also recognize Vulkan's native SMAA support correctly.
- Fixed color-edge artifacts from multisample rounding when Vulkan combines
  8x MSAA with SMAA.
- Fixed over-bright classic Vulkan materials and cutouts that became opaque
  with large color values. HDR vertex-color brightness and PBR emission retain
  their intended range.
- Imported ASE models with vertex colors now have consistent opacity instead
  of depending on an unset alpha value, on both OpenGL and Vulkan.
- Fixed skipped Vulkan rendering immediately after reloading large texture sets.
- Vulkan multisample edges now follow OpenGL's standard sampling convention on
  supported GPUs, including HDR scenes. Other GPUs retain their native MSAA.
- Fixed dark multisample edges around bright materials in eligible Vulkan PBR
  previews with HDR tone mapping off, while preserving classic material colors.
- Fixed darkening during multisample resolve in supersampled Vulkan PBR previews, including
  bright materials with 8x MSAA and SMAA.
- OpenGL PBR previews now preserve authored fog and blend lights with HDR tone
  mapping off, including material diagnostic views and transparent surfaces.
- Supersampled OpenGL PBR scenes now retain the requested MSAA. Repeated
  resolution changes no longer exhaust the render-target cache and make PBR
  fall back to classic rendering.
- Reduced-resolution and supersampled OpenGL PBR scenes now use consistent
  lighting and post-processing sizes, matching the tested Vulkan HDR scenes.
- OpenGL HDR now preserves bright emission and faint transparent colors through
  the game's scene buffers, including with MSAA. Update the bundled game modules
  together with the renderer to receive this fix.

- Improved the experimental OpenGL PBR preview with filtered reflections, diffuse environment lighting, stable material colors and highlights, HDR tone mapping, current moving shadows and working MSAA cutout coverage.
- Rough PBR metals now keep their full brightness instead of losing up to two
  thirds of their reflected light. PBR ambient occlusion now also darkens
  authored ambient lights. Smooth surfaces seen head-on keep their
  reflections, bright occluded colors no longer turn muddy, and reflections a
  normal map would bend below the surface fade out. This applies on OpenGL and
  Vulkan.
- PBR materials now match the brightness of the map's own lighting. A PBR
  surface under a classic light shows the brightness the level designer
  balanced, where it previously showed about a fifth of it, and several lights
  add up exactly as they do on classic surfaces. With HDR tone mapping on, PBR
  surfaces now share the classic tone curve, so turning PBR on never changes
  how stock surfaces look. The former linear PBR scene, which presented the
  whole view through the PBR filmic curve, is now the developer setting
  `r_pbrLinearScene`. This applies on OpenGL and Vulkan.
- PBR materials now render on the default OpenGL renderer in ordinary
  gameplay. Each PBR surface is drawn by the regular light loop with every
  light's own shadows, where previously the whole frame had to qualify for an
  experimental path that any weapon, stock material, ambient light or stencil
  shadow ruled out. Translucent PBR materials blend through their alpha as on
  Vulkan. `r_glPBR 0` returns OpenGL to the classic stages.
- Normal-mapped PBR surfaces no longer show black pixels along their edges,
  on OpenGL and Vulkan.
- PBR surfaces now take their indirect light from the map. A level's ambient
  lights reach PBR reflections as well as diffuse light, so metals reflect a
  room's ambient light instead of turning black, and PBR surfaces no longer
  glow in dark interiors from a built-in studio sky (now the developer setting
  `r_pbrAnalyticEnvironment`). Maps with a baked light grid now light PBR
  surfaces with it on both OpenGL and Vulkan, so metals no longer pick up the
  grid's diffuse glow. Material AO darkens all of this indirect light.
- PBR materials are now on by default, so maps and mods that ship them render
  them without any setup. Stock content has no PBR materials and looks exactly
  as before; a frame without one does no PBR work at all. A profile saved with
  the old default switches PBR on once; set `r_pbrMaterials 0` to keep a
  mod's classic look. Authored reflection probes stay opt-in
  (`r_rendererReflectionProbes 1`).
- With SSAO on, PBR surfaces now darken only their bounced light in corners
  and crevices: direct light and highlights stay bright, and SSAO no longer
  doubles the darkening a material's own AO already describes. Classic
  surfaces look exactly as before. This applies on OpenGL and Vulkan.
- On OpenGL, PBR materials on models converted with the opt-in
  `r_convertMD5toMD5R` now render as PBR, as they already did on Vulkan,
  instead of falling back to their classic stages.
- Reflection probes can now be boxes. Add `boxParallax` to a probe's
  `openQ4SpecularProbe` block, and its reflections line up with the room's
  walls as you move, instead of sliding along with you. A box probe can be
  longer along one axis than another.
- Mappers can capture reflection probes in the game: `bakeReflectionProbes`
  renders every probe of the loaded map from where it stands, saves the
  faces its `cubeMap` names to your save folder and shows the result at
  once. Ship the saved faces with your map. See the
  [PBR materials guide](../../user/pbr-materials.md).
- `envshot` now captures the whole view at every window size; outside
  640x480 it used to save a cropped corner.
- With the experimental modern visible renderer, SSAO now also darkens only
  the bounced light of PBR surfaces.
- On a Mac's OpenGL renderer, which cannot run the PBR shaders, PBR
  materials now keep their classic look instead of rendering without their
  bounced light and printing shader errors.
- PBR materials have now been checked on Intel graphics and with Linux's Mesa
  software renderers as well as on NVIDIA, on both OpenGL and Vulkan.
- Repeated console `script` commands now execute correctly instead of silently skipping commands after the first.
- GPU-skinned models now remain visible when animation is paused and retain correct culling bounds after deformation, preventing disappearing or clipped meshes.
- The stock HUD now remains visible over experimental PBR scenes and keeps its colors when scene exposure changes. Environment lighting also stays active when the last direct light is switched off.
- Fog and blend lights now compose with eligible HDR PBR scenes, preserving reflections, emissive materials and transparency. Unsupported scenes keep the compatibility renderer.
- Eligible HDR PBR materials now receive baked area lighting with correct metallic and occlusion response while retaining their reflections. Existing light-grid assets remain compatible.
- Simple classic bump, diffuse and specular materials can now share eligible HDR PBR scenes while preserving their authored lighting. Unsupported combinations retain the compatibility renderer.
- Fixed a crash when returning to shadowed classic rendering after a video restart and window resize. Renderer diagnostics now report native MSAA correctly when a modern frame falls back.
- Switching shadows off now restores experimental PBR lighting consistently, including after a video restart.
- Experimental Vulkan rendering now survives a screenshot immediately followed by a window resize/restart, and multiplayer scenes honor 4x MSAA through HDR and temporal-AA toggles.
- `screenshot linear` now exports eligible Vulkan HDR scenes (developer setting `r_pbrLinearScene 1`) for material debugging, preserving bright lighting before exposure and tone mapping. Unavailable scenes are rejected instead of producing an old capture.
- Modders can inspect individual RGBA8 render textures with `screenshot image`
  on OpenGL and Vulkan, helping locate material and post-processing problems
  without changing the displayed scene.
- Scalable HUD, menu and console fonts now retain their glyphs when images are reloaded, including across video restarts on Vulkan and OpenGL.
- Experimental Vulkan temporal AA now tracks eligible moving rigid objects using their previous transforms. Unsupported geometry continues to reject temporal history conservatively.
- Optional HDR tone mapping now retains detail in bright textures. Vulkan also preserves the sky's colors and cloud detail when exposure changes, matching OpenGL's treatment of the backdrop.
- Experimental Vulkan PBR direct lighting now supports scalar and separate-map materials and all supported normal-map encodings, with tested point/projected shadows and safe classic fallback for unsupported materials.
- Matching Vulkan PBR cutouts preserve their holes and multisample coverage. Emissive materials now glow consistently without direct lights, retain faint channels at extreme brightness, and avoid being added again for every light. Experimental Vulkan also draws see-through PBR materials itself now, blending them in the right order with the lighting they receive; a shadow-casting light over a see-through surface keeps the compatibility path.
- Fixed missing coverage on strongly minified Vulkan PBR cutout textures by using the material's own albedo for its depth mask. Broader multisample edge differences remain under investigation.
- Fixed unintended holes in fully opaque Vulkan PBR texels when a material's
  alpha cutoff is exactly 1.0. Image reloads and video restarts preserve the fix.
- Reduced Vulkan PBR cutout edge differences by correcting offscreen framebuffer
  orientation through depth sampling and presentation. Some filtered edges and
  baked-lighting comparisons still differ.
- Fixed a bright edge artifact in Vulkan PBR previews at 125% resolution with
  8x MSAA, without changing the scene's intended brightness.
- Experimental Vulkan PBR now supports filtered environment reflections and diffuse lighting, including on see-through materials when direct lights are off. Authored reflection probes have passed local LDR checks with and without MSAA, and the default light-grid setting now preserves environment lighting on maps without baked grids. Broader material and hardware qualification remain in progress. Vulkan remains opt-in.
- Eligible Vulkan HDR scenes now receive baked area lighting with metallic and occlusion response while keeping their reflections. Image reloads and video restarts restore that lighting.
- See-through Vulkan PBR materials now keep consistent opacity when a light covers only part of them. Objects using the same model also keep their own lighting instead of borrowing another object's highlights.
- Crowded see-through Vulkan PBR scenes now fall back consistently when their temporary draw capacity is exceeded, avoiding mixed lighting and transparency paths.
- See-through Vulkan PBR materials now retain their lighting when fog is present.
- See-through Vulkan PBR scenes now recover consistently when rendering resources cannot be prepared, preserving classic lighting and opacity until native rendering is ready again.
- Ambient lights now respect Vulkan PBR metallic materials and transparent opacity, preventing the extra diffuse glow caused by classic lighting being added to those surfaces.
- Eligible Vulkan PBR scenes now retain correct HDR brightness through fog and transparency, including with 4x MSAA. Bloom and automatic exposure use the same linear lighting, and exposure history resets when a scene changes rendering paths. Broader material, baked-lighting and post-effect combinations remain experimental.
- Vulkan PBR material views now show albedo, normals, metallic, roughness, occlusion and ownership without requiring a direct light. Emissive materials keep their glow view, and transparent diagnostics retain their opacity. Broader diagnostic qualification continues.
- Improved Vulkan texture-upload reliability: malformed regions are rejected safely, and the smallest compressed cubemap mips upload at their correct size.
- Reloading images now preserves render-target dimensions and formats, fixing a black scene after image reload with MSAA enabled. Vulkan PBR cutout edges also use footprint-filtered alpha coverage.
- Native Vulkan PBR now filters narrow specular highlights using surface curvature and normal-map variation. Constant shading normals and fully rough materials keep their original appearance.
- Improved bot recovery from stalled routes and stopped bots pursuing pickups they no longer need. Collecting their own item no longer triggers a stolen-item reaction.
- Bots no longer walk across jump pads or into teleporters by accident, stop at the edge of a drop, or keep heading for a corner on a floor they have been knocked off. They know every place a jump pad can land them and steer in the air like a player to get there, including the ledge beside a launch shaft. They also leave alone the pads that would only bounce them in place, and hop onto a ledge they cannot step up instead of pushing against it. Route failures fell from about one search in ten to about one in fifty on q4dm5, and getting stuck from about fifteen times a minute to two.
- Bots plan each move on a navigation map that knows about kill volumes, lava, crushers and trapdoors, running jumps across gaps, and long drops with their fall damage. They choose goals by the real route length, so a bot directly above an item no longer counts it as close.
- Bots check the complete flight and touchdown before choosing a jump, rejecting narrow lower ledges that their running momentum would carry them past.
- Bots hear weapons, footsteps and pickups, learn when the items they see taken come back, break off fights they are losing, pick up a real weapon first when they only have the machinegun, buy gear in buying modes, and dodge rockets fired from any direction. A bot in a firefight no longer abandons the flag it was heading for.
- In team games, team chat gives your bots orders: follow, defend, hold here, attack, or dismissed. Ordered bots answer in team chat, and their team-mates share the remaining roles around them.
- Bot decisions cost far less: goal choices search a few hundred places instead of a third of the map, and bots no longer count other players as walls when they plan.
- Stopped bot-only network message queues filling during combat, eliminating repeated overflow warnings and unnecessary visibility work. Spectators following bots and demo recordings still receive combat feedback; unreliable broadcasts now reach their intended human recipients.
- Bots now use their last sighting when choosing a weapon for a hidden enemy, and stop adjusting combat movement to that enemy's unseen position. Projectile threat detection includes explosion reach, including resting grenades.
- Joining a team from the multiplayer menu's Match Control now moves you to that team. When Auto Balance Teams refuses because that team has too many players, Match Control says so, instead of reporting the change as made while you stayed on your old team.
- Added complete German translations for bundled menu and gameplay text, with umlauts and sharp s supported by the existing fonts.
- Changing language now refreshes cached menu labels immediately and keeps the menu visible.
- The language you choose in Settings is kept when your game data comes from a GOG installer. Extracted GOG data can include a `q4base/autoexec.cfg` that switched the game back to French at every launch; it now sets only the first launch's language. Thanks to tomkidd for finding the cause.
- Improved Vulkan render-target compatibility: cube faces, depth-only targets and multiple color buffers now render and capture correctly, including resizing and multisample resolves. Heavy render-target resizing also avoids releasing images before the GPU has finished with them.
- If Vulkan cannot create its window, surface, swapchain or startup resources, openQ4 now continues with OpenGL in the same launch. Your Vulkan preference is kept for the next launch, and the log retains the recovery reason.
- Looping effect sounds, such as crackling electrical arcs, no longer play an extra copy each time you load a saved game.
- Bullet impacts, ricochets and explosions play their full range of sounds again, as in the original game, instead of repeating the same one every time. Thanks to Emile Belanger for finding this.
- Headphones and stereo speakers no longer get a 5.1 mix folded down to two channels. openQ4 now follows your audio device's own speaker layout, so headphones get stereo with OpenAL Soft's 3D headphone audio where the system reports headphones, and 5.1 or 7.1 systems get all their channels. **Surround Speakers** in the Audio settings now offers **Auto**, **Off** and **On**.
- Radio chatter plays at the original game's level instead of noticeably quieter, and world sounds fade with distance as they did in the original game instead of dropping away sooner.
- Sounds right next to you surround you instead of snapping from ear to ear as you turn, and your own footsteps, weapons and the announcer play evenly instead of from straight ahead.
- Music and other stereo sounds keep their full width and level with 3D headphone audio on, instead of being narrowed and turned down by it.
- Turning **Headphone HRTF** off now works. Unplugging a headset or switching audio devices keeps music and ambience playing, with your speaker and HRTF settings intact.
- Cleaner high frequencies, because most of Quake 4's sounds are recorded at 22 kHz and are now upsampled with a sinc filter. Loud firefights compress instead of clipping, and distant sounds lose a little treble, as the original game's room presets specify.
- Explosions and heavy machinery shake the camera at their intended strength and distance, fixing excessive shaking in Air Defense. Controller rumble follows each sound's loudness.
- Sounds that maps set louder than normal play at their intended volume again, and voice lines from map speakers sit at the original game's level instead of 3.7 dB louder.
- Fixed sounds occasionally playing silently after a scripted fade elsewhere in the level, dialogue being cut off while the weapon wheel slows time (music now keeps its tempo there), dialogue carrying on aloud while the game is in the background, sounds that play everywhere cutting out far from their source, and footsteps repeating the same step twice in a row.
- Levels load faster: sounds now decode on background threads while the rest of the level loads, cutting the sound share of loading Air Defense from about a second to about a quarter of that.
- Sound memory no longer grows with every level you play: sounds a new level does not use are freed, so two levels in a row hold about 120 MB instead of 230 MB, and sounds load without an extra copy.
- A damaged sound file is reported in the log instead of playing a beep, ending the game or playing as noise.
- The Awakening's speeder bike now handles as the expansion intended. It reaches
  racing speed instead of crawling, a held turn stays steady instead of spinning
  faster and faster, the bike stops turning as soon as you let go, and it holds
  itself off walls and tunnel ceilings. Games saved on the bike pick up the fix
  when you load them.
- The Awakening's final battle plays as designed. The Valkaryne docks behind the
  Makron sphere's shields and fires death-ray volleys you can take cover from,
  then fights you on foot once you destroy the sphere.
- The Awakening's enemies fight as designed. The beach's death-ray turret no
  longer shoots at you, Pain Lord headshots count, elite grunts pull you in with
  a gravity well, elite gunners spray their fire, tactical elites throw grenades,
  and the Retches on two levels that never moved now walk.
- The Awakening's goob gun throws its burst of globs and shows its flamethrower
  flash, and its burns stack only up to a limit. The dropship and MCC cannons
  protect their gunner, freeze during the scripted ammo jam and recentre when you
  leave, and the race track's rough edges slow your bike.
- The Awakening's race clock now counts down on your speeder bike's display, the
  bike's dashboard shows your speed, and the MCC's health shows on the MCC
  cannon's. Strogg fighters bank into their turns, the cannons' pulse barrels
  kick back with every shot and fold away while you use the missiles (which keep
  reloading meanwhile), and the crosshair shows when a marine is too busy to
  talk. See [The Awakening on openQ4](../../user/awakening.md) for everything
  openQ4 changes in the campaign.
- Effects and materials that name a table the game never shipped now run at full strength, as in the original game. This restores full-size blood bursts.
- Overlapping see-through effects now draw farthest first, as in the original game.
- Napalm impacts now match the original game, which places part of the flame column far from the impact.
- Modders can again use `bseStats` and `bseLog` to list effect statistics and how often each effect has played.
