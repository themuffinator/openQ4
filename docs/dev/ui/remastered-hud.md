# Remastered HUD layout

## Multiplayer objective markers, 23 October 2026

`rvObjectiveMarkers` supplies a bounded game-owned overlay through
`native/objective-markers`. It discovers permanent CTF/One Flag bases, Arena CTF
assault points and DeadZone triggers from the active map, caches spawn-safe
references for one second, filters instances and disabled zones, and clears on
map/module reset. At most eight objectives draw; bases have priority, followed
by nearby points. Overlapping symbols collapse; distance placement considers
every selected symbol before submitting any text.
The camera-space projection follows the current rendered view and compensates
the 640x480 submission space for the actual output aspect. No network messages,
carrier tracking, new materials or proprietary artwork are introduced.

CTF status comes from replicated `rvCTFGameState`; the marker remains at the
permanent base when its flag travels. DeadZone uses the existing per-zone
`entityAffect` visual state, with the replicated aggregate state as a fallback
only for a single zone. A custom multi-zone map without that visual contract
gets location/eligibility markers rather than a fabricated per-zone owner.
The artifact requirement respects `requiresDeadZonePowerup` and the viewed
player's actual inventory. Following spectators inherit that player's team;
free-camera spectators, dead players, zoom, menus and scoreboards suppress it.
Warm-up/countdown permit orientation; review does not.

Stereo VR uses the same corrected tracked head pose as `PlayerView::VRView`,
including lean limits and physical crouch, then intersects objective bearings
with the OpenXR compositor's physical HUD quad. `vr_hudDistance`, `vr_hudWidth`,
the actual virtual-screen aspect and the hand-aiming `vr_hudHeightOffset` all
participate. Head aiming centres that plane. Eye FOV and controller aim do not
drive marker placement. Symbols converge at HUD depth in both eyes; they are
HUD bearings, not geometry placed at the distant objective. Forward-gaze fading
stays relative to the head, even on a lowered HUD, and directly-behind arrows
remain deterministic. The actual stereo view gates this path, so a disconnected
runtime with `vr_enable 1` and flat spectator views retain ordinary projection.

Symbols are first-party geometry with a stock fallback. Distance is the only
text: pennant fill/position and directional cues show home/carried/dropped;
zone cores show empty/controlled/contested and a padlock shows the artifact
requirement. Faction colours remain consistent, with quiet outlines and no
flashing or alert text. Localized distances use
the UI's existing Lowpixel font, language decoding and glyph capture, exposed
through the append-only `idUserInterfaceManager::BuildHudText` slot. Game API
55 rejects older modules: stage the engine and both games together. All twelve
language tables carry the distance format. Distances fit and centre by their
shaped ink bounds and cannot wrap out of their clipping box.

The native `openq4-objective-marker-layout` test exercises 12,705 aspect/bearing/
pitch combinations, behind-camera and near-plane cases, invalid input,
crosshair emphasis and collision bounds. Another 47,628 VR bearings cover HUD
dimensions and offsets, including 7,044 in-frame intersections independently
checked against `VR_RayQuadIntersect` from the head and both eyes.
`tools/ui/capture_objective_markers.py`
uses stock maps, staged binaries, isolated profiles, hidden windowed clients,
disabled device input and engine `screenshot` captures. Its report records the
source, dirty tree, binary hashes, launch arguments, images and logs. Gameplay
state assertions distinguish taken/dropped flags and a controlled zone from
warm-up. The death/respawn step uses the server's semantic `kill 0` command and
automatic respawn, without injected device input.

Windows validation on 23 October 2026 passed the complete debugoptimized
build/stage and six focused suites: `openq4-objective-marker-layout`,
`openq4-ui-hud`, `openq4-ui-remastered-hud`,
`openq4-ui-remastered-hud-generated`, `openq4-ui-hud-coverage` and
`openq4-ui-retained-screens-loading-hud`. The source language-table check also
passes. Local evidence is retained under `.tmp/hud-poi/`:

- `icons-gl/report.json`: OpenGL 1280×720, five passing live scenarios and
  sixteen engine captures covering CTF, DeadZone, One Flag CTF, a dedicated
  server's remote CTF client and DM with no markers. Includes stock fallback,
  behind-camera directions, HUD size/safe inset, the opt-out and HUD suppression.
- `icons-vulkan-verified/report.json`: Vulkan 1920×1080, two passing scenarios
  and fifteen engine captures. Actual flag pickup/drop and DeadZone ownership
  assertions pass; death, scoreboard and hidden-HUD captures suppress the POI.
- `icons-ultrawide/report.json`: a passing Russian One Flag CTF run, including
  all three bases and Cyrillic distance units. The requested 2560×1080 window
  produced a 2538×1080 engine capture on this host; markers use that actual
  viewport aspect.
- `build-icons.log`, `stage-icons.log`, `tests-icons.log`: final build/stage and
  focused checks. Reports record executable/module hashes and launch commands.

The fresh worktree lacked an OpenAL runtime DLL; staging used an independent
temporary copy of the existing development checkout's runtime. Stock AAS,
missing-image and non-precache warnings remain unrelated to the overlay.
The passing captures contain no retained-UI warnings or engine errors.

VR validation uses `tools/tests/openxr_objective_markers.py` with the existing
non-installed OpenXR test runtime. Set its process-local `XR_RUNTIME_JSON`;
no system runtime registration or headset is needed. The harness keeps every
controller action inactive (`OPENQ4_XR_TEST_NO_INPUT=1`), disables host mouse/
joystick input, and uses scripted tracking poses with room scale and physical
crouching off. Windowed CTF and DeadZone matches exercise head yaw/pitch/roll,
lean, altered HUD width/distance/height, head aiming, behind-head bearings,
flag pickup, artifact pickup/zone ownership, and runtime-requested exit with
`vr_enable 1`. Eye and HUD images use the engine's `screenshot image` path;
paired enabled/disabled HUD captures prove marker presence. Classic/retained
right-edge footprints are compared independently of other HUD animation:
strokes use clockwise canvas winding so the Classic renderer cannot cull them.

The Windows build/stage, the six suites above plus `openq4-vr-math-core`, and
`tools/tests/openxr_vr_contract.py` pass. The contract follows game API 55's
appended HUD text slot. VR evidence is under `.tmp/hud-poi-vr/`:

- `accepted/report.json`: two passing gameplay scenarios and 44 engine images,
  with 1200×1320 eye targets and a 1920×1080 HUD. Runtime layer metadata verifies
  stereo projection plus the HUD quad at each tested physical size/offset;
  on/off pairs and Classic/retained stroke comparisons pass. The report records
  source/dirty-tree identity, binary hashes, process environment and commands.
- `desktop-vulkan/report.json`: two passing CTF/DeadZone scenarios and fifteen
  engine captures at 1280×720, using the same executable/module hashes as the
  VR run. Classic strokes, flag pickup/drop and zone ownership are preserved.
- `build.log`, `stage.log`, `tests.log`, `vr-contract.log`: build and focused
  verification evidence. Existing stock asset/AAS/precache warnings remain;
  these VR runs have no engine errors, retained-UI warnings or XR layer errors.

Reproduce without hardware after building/staging and building the test runtime:

```powershell
python tools/tests/openxr_objective_markers.py --assets "E:/SteamLibrary/steamapps/common/Quake 4" --output .tmp/hud-poi-vr/new-run
```

This checks geometry, layer submission and gameplay rendering. It does not
qualify real-headset optics, readability, latency or comfort.

This bounded addition extends UI-03/UI-04; it does not accept the complete HUD
migration, performance or device matrix.

## HUD layout and presentation

With `ui_retained 1` and `ui_hudLayout 1` (the default), the stock HUD GUIs
draw the Remastered layout of [visual specification section 14](../ui-visual-design.md)
instead of their stock rectangles. Each covered GUI is paired with a retained
document that receives the same state keys and named events the game sends
the stock GUI, and lays its groups out on the section 14.4 anchors: the 4:3
center screen, the side screens capped by `ui_hudWidth`, the safe inset
`ui_hudSafeInset` and the HUD scale `ui_hudScale`. `ui_hudLayout 0` keeps the
stock layout, drawn through the [HUD presentation bridge](hud-coverage.md).
Settings > Game Options > HUD sets all four (HUD Layout, HUD Size, HUD Width
and Safe Area Inset); see the [settings structure](../settings-menu-structure.md).

### The window's safe area (2 November 2026)

On a phone, or any window that draws under a camera cutout, rounded corners,
a status bar or a gesture bar, the Remastered and Minimal readouts keep clear
of them (visual specification section 13.3, `LAY-016`). The platform reports
the clear area as the window's safe area
([retained screens](retained-screens.md#the-windows-safe-area-1-november-2026));
each edge anchor takes the larger of its own distance (the side screen beyond
the HUD Width cap plus the player's Safe Area Inset) and the window's inset on
that edge. A cutout inside an empty side screen therefore moves nothing, and a
deeper Safe Area Inset keeps its place. The HUD documents' 720 dp canvas fits
the safe area, as the menus' does, and the engine peer measures its anchors
with that same ratio (`HudLayoutFor` in `src/ui/retained/Hud.h`). The root
still fills the window, so the crosshair and the other center-anchor groups
stay on the true projection center and every layer that fills the root stays
full-bleed. The view effects and weapon scopes are documents of their own that
fill the window and take no inset. The status bar's narrow-view fit
measures the span between the deeper side anchor and its mirror, so a
one-sided cutout cannot push the centered bar under it.

Desktop windows report no inset, so every layout presents as before.
`ui_retainedSafeArea "left top right bottom"` (pixels, unarchived) sets test
insets on a desktop. Classic keeps the stock layout and reads no inset, and
multiplayer objective markers keep their own Safe Area Inset margin.
The center-left and center-right anchors center on the safe area's height
rather than the view's: unequal top and bottom anchors (a status bar without
a gesture bar, or Android's touch controls lowering the top) move them by
half the difference, so their canvas never reaches into the deeper inset. On
desktop the two anchors are equal and nothing moves. The center anchor never
moves.

`openq4-ui-hud` checks the anchors at every view, scale, inset and width with
and without window insets. `openq4-ui-remastered-hud` lays out every Remastered
document at 1080x2400 (insets 0, 96, 0 and 48 px) and 2400x1080 (96, 0, 24
and 40 px) with every event that raises something run at once. Each edge
anchor's anchored edges stand inside the safe area and the edge midpoints
center on its height. Everything each text, vector and picture on an edge
anchor draws stands inside it too; a draw belongs to the readout
whose box holds it whole, since text and vector boxes run wider than their
words and art. Art that a twin without insets already draws off the screen
(the objectives bar, the vehicles' electric warning below the stock canvas)
may run under that edge's inset. The root and the layers filling it fill the
view, and a view that loses its insets lays every readout out as the twin.

Classic suppresses legacy edge expansion for the stock HUD families and keeps
their centered 4:3 canvas. Its vector catalog has separate beveled Marine and
Strogg fills, armor cells, measured traces clipped to the plate, and an inner
low-health glow. External mods retain their own layout behavior.
The Classic damage direction uses a soft elliptical glow with transparent
canvas margins; the missile warning has its own small chevron. Restarting
either hit-flash timeline restores its initial color before fading, including
when Reduced Flashing changes between hits. Multiplayer award medals retain
their approved stock bitmap artwork and are registered during level precache.
Remastered Strogg gauges tile the installed `s_static` grain across the full
moving area under each fill mask, with the stock additive `#161616` tint.
The two-second translation loops without exposing an empty part of the mask;
reduced motion holds it still. This installed-art exception is recorded in the
[implementation inventory](inventory-report.md); it adds no extracted asset.

The on-foot ammo, health and armor numbers use the legacy TTF renderer for
layout. Original text rectangles, per-window Marine shear, proportional
advances, two-unit inset, atlas selection and gain textscale are preserved
inside the centered status-bar group. Strogg reserve text retains its 0.26
scale. The ammo backdrop uses the selected weapon's original installed bitmap
material, including expansion weapons; no extracted pixels ship in the repo.
The unseen legacy windows still advance scripts and presentation aliases,
but discard geometry before allocating primitive records.

The 11 October status-bar parity pass keeps the centered group and the legacy
125x59 Marine / 129x67 Strogg rectangles, vertical positions and gauge spacing.
Marine health and armor icons use their original 98-unit inset. Fill artwork
multiplies the original tint by the measured texture luminance (148/255 Marine,
171/255 Strogg, 175/255 armor), with a soft bevel and translucent armor dividers.
Disjoint vector contour bands preserve material alpha; a single silhouette
mask antialiases the exterior without introducing dark seams between bands.
The original black 0.50 backing is retained. The same construction serves SP,
MP and both Awakening variants. Validation evidence is under
`.tmp/hud-status-parity/`, using native layout checks and engine screenshots.

The ECG uses the installed legacy `ekg`, `s_ekg` and `s_ekg2` bitmaps under
the existing vector fill mask. Repeating UVs cover each complete scroll period;
health colours, glow, speed and essential motion are retained. This approved
bitmap exception removes the repeated waveform/glow tessellation and vertex
submission without adding extracted assets to the repository.

Radio and voice-chat displays use the installed legacy `waveform.tga`, as
requested in the 11 October review. The complete recorded trace scrolls at
0.6 texture widths per second and uses the stock 21-sample `wavTable` for
centred vertical UV scaling, including its repeated endpoint and wrap interval.
The bitmap stays in the same vector mask and colours; talker events, names and team
colours still control the display. It is an activity indicator, not an audio
level meter. Reduced Motion freezes the graph. Documents without peak fields
skip audio-history reads and unused state writes in the HUD peer. See
[measurements](runtime-performance.md#gameplay-instrument-performance).

HUD icons remain editable SVG-derived paths with cached tessellation and
engine meshes. Completed vector gauge layers reuse their rendered pixels;
additive instrument images can draw through disjoint mask coverage directly.
Composition uses artwork bounds and avoids rebinding an unchanged target, so
small icons and panels can share the engine's existing GUI batches.

Ammo gain events compare complete successive readouts for the same weapon.
Reloading animates the clip, a reserve pickup animates the reserve, and a
clipless or multiplayer weapon animates its total; firing, switching weapons,
unlimited ammunition and unrelated inventory pulses do not count as gains.
Each readout scales about its own trailing baseline. Boss, exit, save and
missile notices reserve separate rows when they coexist. The status bar limits
its effective scale to the safe width on narrow views without changing the
player's saved preference.

Tourney publishes the viewed arena's sudden-death state, preserves both arena
instruction lines, and uses translucent banners with nonessential motion.
Its message bar uses the banner's localized round name; the countdown appears
only in the timer. Arena sudden death also sends the state and clock-warning
events that stop the expired timer's pulse. Round modes show their round clock
throughout a timed live round, while Classic keeps its last-30-second flag.
The timer shares Competitive's localized readiness text and omits an empty
eligible-human count. Chat is bounded to eight wrapped lines at rest as well
as during entry; server notices use their information style.

## Minimal

`ui_hudLayout 2` is section 13.9's Minimal preset: the same documents, with
everything but health, armor, ammunition and the crosshair family left out.
What answers or asks for the player's action stays: the vehicle's Exit or
locked prompt, the save notice, chat, voice and votes, the respawn prompt,
the follow card, an objective failure and the held displays (objectives,
scoreboard, statistics). A vehicle keeps its own display: the cannon's optic,
the ship panel, the MCC's hull readout and the speeder's race display. It
leaves out the weapon strip, pickups,
transmissions, the boss bar, damage direction, missile warnings, objective
notices, powerups, the vehicle's hit flash, electrical warnings and the
speeder's speed streak, and in
multiplayer the standings, team panels, timer, center notices, kill feed, aim
text, the Tourney strip, the match card and the lag notice. Each document reads
the preference as `hud.layout` and wraps the groups Minimal drops in a
full-anchor container (`beyond_minimal`), so the remaining groups keep their
places.

## Competitive

`ui_hudLayout 3` is section 13.9's Competitive preset, built by
`tools/ui/remastered_hud_competitive.py` into the same documents. In single
player it is the Remastered layout. In multiplayer:

- **Match bar.** The standing or team panels, the timer and the Tourney strip
  (`tl-rem`, `tc-strip`) give way to one bar at the top center: three sheared
  plates from y 6, 26 u tall and 2 u apart, a 60 u timer at 0.72 between 122 u
  sides at 0.55. In team modes each side's outer 25 u block is the team color
  at 0.85 with its faction mark, then the team name; your team (or the followed
  player's) leads. In deathmatch and Tourney the block is the header band with
  the rank ordinal (the leader's in #FFBE23) and the name, cut at 52 u, with the
  marker on your side. The scores sit at the inner ends in Chain 21 u.
- **Timer.** The Remastered timer's states in Chain 15.5 u, with WARM-UP, the
  countdown and STARTS IN, SUDDEN DEATH and the infinity mark, and under it the
  limit (TO 30), the round, the arena or the warm-up's ready count.
- **Under the bar.** Two-flag modes hang each side's flag 6 u in from its inner
  end (AT BASE, TAKEN with the carrier, DROPPED); round modes show five alive
  pips; Dead Zone puts the holder, CONTESTED or EMPTY under the timer.
- **Network quality** follows the bar's trailing end at all times: the ping
  beside four bars (four to 60 ms, three to 120 ms, two to 200 ms, one above),
  each 2% of this client's incoming packet loss removing one, down to one.
- **Team status.** Team modes list your team's players (up to eight, best score
  first) at the leading edge from y 150: the weapon, the name, health in Chain
  (#E46D56 at 25 and below, #FFBE23 above 100) and armor; your row on the band
  plate with the marker; the dead at 0.40 under a cross. Spectators see the
  Marines leading and the Strogg trailing, the followed row lit at 0.22 with
  the marker; held statistics take the trailing list's place.
- **Readouts.** The status bar, the weapon strip and the pickups grow by 15%
  about the bottom edge (`status_scale`, still limited to the span on narrow
  views). The chat box keeps its 8 u clearance above the bar and never rises
  toward the team status: short of the 180 u it needs beside the bar, it is
  200 u wide, or as wide as the space beside the widest enlarged strip allows,
  down to 120 u at 4:3. The vote moves to the top-leading corner (36 u down,
  clear of the team status) and the free camera's name tags add a health line.
- **Narrow views.** Below about 16:9 at the HUD scale in use
  (`under_match_bar`), the kill feed, the managed-match card and the lag
  notice step 38 u down, so the feed's longest line clears network quality.

The game publishes the bar's sides (`oq4_mb0_*`, `oq4_mb1_*`), the line under
the timer (`oq4_mb_sub`, `oq4_mb_ready`), the team status rows
(`oq4_ts{team}_count`, `oq4_ts{team}_{row}_*`), the tags' health and
`oq4_loss` from `idMultiplayerGame::PublishRemasteredCompetitive` and
`PublishRemasteredHud`.

## Covered GUIs

| Stock GUI | Document | Contents |
| --- | --- | --- |
| `guis/hud.gui`, `guis/q4xhud.gui` | `remastered_marine.q4ui`, `remastered_marine_awakening.q4ui` | Status bar, weapon strip, pickups, transmission display, boss bar, notices, interactive brackets, damage direction, missile warning; in multiplayer the powerup slots and developer readouts |
| `guis/hud_strogg.gui`, `guis/q4xhud_strogg.gui` | `remastered_strogg.q4ui`, `remastered_strogg_awakening.q4ui` | The Strogg family of the same groups |
| `guis/mphud.gui` | `remastered_mp.q4ui` | Kill feed, standing, team panels with flags and zone, timer, center column notices, aim text, vote, voice, chat lines and, while message mode is open (`guis/mpmsgmode.gui`), the chat input, follow card (Next, Previous, Free camera), the free camera's name tags, respawn prompt, Tourney strip, statistics card, Arena ceremony, lag notice; in Competitive the match bar, network quality and team status |
| `guis/hud.gui` while riding the flatbed, `guis/hud_strogg.gui` on the tram | (the on-foot documents) | The ride's silhouette gauge replaces the three gauges (`enterFlatbed`/`enterTram`), traced from the stock truck and tram art |
| `guis/vehicles/airassault/turretplayerhud.gui` | `remastered_turret.q4ui` | The Awakening's space cannon (m03's dropship guns): the transmission display, boss bar, notices, damage direction, brackets and missile warning without gauges, strip or powerups; the ship-status panel under the transmission display (damage sections, spinning objective markers, attack darts, and the view cone relative to the ship's nose); the HUD prompt's Exit |
| `guis/vehicles/mcc/cannonplayerhud.gui` | `remastered_mcc.q4ui` | The Awakening's MCC cannon (m06): the gun takes no damage, so the readout is the MCC's hull, U.S.S. Hannibal over the stock gauge plate with the hull number (the MCC's health divided by 10), stacked under the transmission display the side gun's HUD lacked, its track filled in proportion and warning at 25%; the HUD prompt's Exit |
| `guis/vehicles/turret_hud.gui` | `remastered_chaingun_turret.q4ui` | openQ4's HUD for The Awakening's Strogg chaingun turret (m01's trench, m04's prison), which the expansion gave the on-foot HUD with an empty ammunition gauge: the gunner's Marine health and armor gauges in their on-foot places and the barrel heat (`vehicle_heat`, `vehicle_overheat`, `vehicle_heatwarn` from `idVehicleTurretGun`) on the gauge where ammunition shows, its figures and paint #FF3300 and pulsing from the warning level; a heat bar under the crosshair with OVERHEATED while the gun vents (Minimal keeps both); the transmission display, damage direction, notices and the HUD prompt's Exit. Classic is the GUI itself, built on the stock vehicle HUD |
| `guis/vehicles/speederplayerhud.gui` | `remastered_speeder.q4ui` | The Awakening's speeder bike (m07's races): the vehicle status bar with the bike's own glyphs (its machinegun first, then the cannon with its recharge bar), the hull and shield warnings the expansion removed and the entry lines its HUD never played; the race display 18 u lower (the plate, ten checkpoint pips, the countdown with two-digit hundredths and its seconds #FF3333 in the last 10 s, shown only while the race runs it); the HUD prompt (Boost on the crouch control while a boost is ready, the hazard and Boosting while it pushes, then Exit or Vehicle locked); the speed streak while boosting; transmission display, boss bar, notices |
| `guis/vehicles/gev_hud.gui` | `remastered_gev.q4ui` | The Awakening's GEV (`vehicle_speeder`, which no map places), whose HUD names art that never shipped: the atlas's status cluster centered on the bottom edge (two plates at black 0.60; the #FF9E1A health bar draining from the top, where the stock divided the normalized health by 1000 and read empty; the hull from above turning with the view under the fixed turret; three shield stages as arcs that fade out in turn and the inner arcs pulsing #FF591A to #80591A once the shields fail; the weapon's glyph turned 60 degrees with the infinity mark or the rocket count over the charge bar), the transmission display, notices and the HUD prompt's Exit or Vehicle locked |
| `guis/vehicles/hud.gui` | `remastered_vehicle.q4ui` | The walker's and hover tank's status bar (ARMOR and SHIELDS gauges, weapon panel, rocket count, charge bar), entry lines, hit flash, electrical warning, HUD prompt (Exit or Vehicle locked), transmission display, boss bar, notices |
| `guis/vehicles/airdef_cannon.gui` | `remastered_airdef.q4ui` | The air-defence cannon: the retained nailgun lens held still (Gaussian focus beyond the aperture) with its two crescent load bars and amber reticle, the transmission display the stock lacks, the HUD prompt (Exit or Vehicle locked) in place of the centered exit line, and the save notice |
| `guis/vehicles/medchange.gui` | `remastered_medchange.q4ui` | The Medical Labs implant activation (section 14.19), hidden until `activate`: the gold hex field on the centered 640x480 u screen (the 38 stock cells, lit one every 50 ms from 500 ms in the stock order and fading over 500 ms, twice over), continued through the side, top and bottom bands with phases from the same sequence; the four corner pieces at the view's corners; the processor readouts, segmented bars and lower rune field at the leading screen's bottom, the X/Y/Z readouts and upper rune field at the trailing screen's top, cycling the stock data; the falling rune data, the pink and color static and the orange edge glow over the view. Corner pieces and readouts fade from 4150 ms and the overlay hides at 4650 ms; each `activate` restarts it. `hurt` pulses a red edge glow from 0.6 over 1 s and `saveMessage` shows the save notice, on their own. Under reduced motion the cells hold at 0.10, the readouts still, without the falling data. The hex and corner rims, the edge glow's falloff and the static streaks were measured from the stock textures |
| `guis/wristcomm.gui`, `guis/wristcomm_strogg.gui` | `remastered_objectives.q4ui`, `remastered_objectives_strogg.q4ui` | Objectives display (every objective, three at a time, a COMPLETED section), new-objective and objective-complete notices, OBJECTIVE FAILED |
| `guis/scoreboard.gui` | `remastered_scoreboard.q4ui` | The held scoreboard as the multiplayer menus' card (section 14.18), as tall as its lists: the header with the server's name, your place and the clock, the match line with the mode, map and limits, the headings once, your team's panel, the other team's and the spectators' in the Players page's bands and rows (your marker, the speaker, friends, the dead, flag carriers and the ready check, BOT tags, DEATHS, ping bars), and the address under them, scrolling by whole rows past the card's height over the count of the rows below; Tourney's wider card sets the bracket chart under its match line (with the tournament count), above the players and spectators lists: open-plate arenas converging on a wider final in the centre, round titles, links that light as arenas are decided, your arena in aqua, decided arenas in blue with the winner's score in the value color and the champion's marker, EMPTY arenas |

All documents live in `content/baseoq4/pak0/guis/hud/` and are generated by
`tools/ui/build_remastered_hud.py` from `tools/ui/remastered_hud_*.py`; never
edit the generated JSON. `--check` fails when a document is stale.

## Engine peer

`idRemasteredHud` (`src/ui/RetainedHud.cpp`) pairs a stock GUI with its
document by source name. `idUserInterfaceLocal` forwards every state write,
deletion and named event to the peer; the stock windows keep evaluating unseen
so their scripts, timers and presentation aliases stay where the stock layout
expects them. On its first frame the peer joins the stock GUI's dictionary.
Named events sent before that frame (a vehicle's boarding events arrive before
its HUD is first drawn) are queued, up to 32, and replayed after the join.
State that only events carry survives a peer rebuilt mid-game (the layout
switched to Classic and back): a document's `extensions.openq4.joinEvents`
pairs a stock window variable with the event that set it (`exit_msg::visible`
with Exit, `p_radio::visible` with the transmission display, `desktop::hidden`
with `hideHud`), and the peer replays the event at its join when the variable
holds (`!` inverts the test). The generator refuses an event the document does
not answer.
Strings cross over with color escapes removed and `^ik` key escapes kept,
and the retained state resolves those (`UserInterfaceRetained.cpp`): to the
key's name, or, for a key state a document declares a `<key>.glyph` for, to
the prompt glyph of section 13.5. A mod that ships its own stock-named GUI
keeps it.

**Prompt glyphs.** Every control a HUD prompt names (Exit, Boost, Close,
Respawn, vote Yes/No, the follow card's keys and the quick-chat wheel's channel switch) is
a `glyph_chip` (`tools/ui/remastered_hud_mp.py`). The engine resolves the key
the binding names for the device used last (`KeysFromBinding`) into a kind in
`<key>.glyph` and a label in the key state (`PromptGlyph`): a keyboard key is
a keycap with its name; a controller button draws its family's vector glyph in
the HUD's white, the family being the controller SDL reports (Xbox,
PlayStation, Nintendo, otherwise generic), Steam Deck under the `steamdeck`
platform profile, or the one `in_promptStyle` forces (0 the controller's own,
1 Xbox, 2 PlayStation, 3 Nintendo, 4 Steam Deck, 5 generic). Face buttons
show the label SDL reports for each position (A, B, X, Y, or the cross,
circle, square and triangle), or the lit position in a generic family;
bumpers, triggers and stick clicks are named LB/RB, LT/RT, LS/RS (Xbox),
L1/R1, L2/R2, L3/R3 (PlayStation, Steam Deck, generic) or L/R, ZL/ZR, LS/RS
(Nintendo); the d-pad lights its arm; menu and view draw their symbols
(OPTIONS/CREATE and +/− on PlayStation and Nintendo); paddles are P1-P4, or
R4/L4/R5/L5 on a Steam Deck; the mouse lights its bound button or the wheel's
direction. `testPromptDevice controller|desktop` (cheat protected) sets the
device the prompts name until the next real input, for captures.

A document can ask the peer to read its layout back into its state for the
next frame (`extensions.openq4.readbacks`), where bindings alone cannot know a
laid-out size. None of these readbacks feeds the layout it measures:

- `[state, node, view]`: whether the node lays out wider than the view (the
  chat entry's mark of text scrolled out of view).
- `[state, node, "@height"]`: the node's laid-out height in canvas dp while it
  has one (the chat prompts, one row or wrapped).
- `[state, view, "@firstInside", prefix, count]`: the first of the shown
  nodes `prefix0` to `prefix<count-1>` whose top lies inside the view (the
  first whole message of the chat history; the lines above it fade).

The retained GUI answers `node::@width`, `@height`, `@x` and `@y` presentation
reads with the node's laid-out box.

Diagnostics: `ui_hudProfile N` prints per-document forward, event, state,
draw and unseen-evaluation timings averaged over N frames, and
`ui_hudTest <gui> state|event|get ...` writes, sends or reads a key without
input.

## Game fields

Where the stock GUIs lack what the Remastered layout shows, the game publishes
extra fields beside the stock ones; the stock GUIs ignore them.

- Multiplayer (`idMultiplayerGame::PublishRemasteredHud`): the kill feed and chat
  as structured lines (`oq4_feed*`, `oq4_chat*`, `rvHudLines`), ordinals, the
  lead gap, limits, the clock and its warnings, flags, zone, alive counts, the
  vote tally, the followed player and the control keys. `oq4_flags`,
  `oq4_clockWarn` and `oq4_state` are announced as events, so looping motion
  starts only when its state does; so is the player HUD's carried flag
  (`oq4_carriedFlag`, when it comes or goes, from `idPlayer::UpdateHudPowerUps`).
  Each Dead Zone control tick sends `oq4_zonePulse0` or `1` for the scoring team.
- Chat input (`PublishRemasteredChat`): while message mode is open,
  `oq4_chatting`, up to eight lines of untimed history in the chat slots
  (`oq4_chat_count`), and the entry from the message-mode GUI's edit control
  split at its caret (`oq4_chatin_before`, `_after`), the characters left of
  the stock 128 (`oq4_chatin_left`), the channel (`oq4_chatin_team`), whether
  team chat exists (`oq4_chatin_channels`) and the overstrike mode. The
  control (`idChatWindow`) publishes its caret and overstrike mode
  (`oq4_chat_caret`, `oq4_chat_overstrike`) and, in the Remastered layout,
  stops drawing its own panel; it keeps the typing, recall and channel
  switching. Opening the input, typing and moving the caret send
  `oq4_chatType`, which restarts the caret's blink. `chatInput <text>` and
  `chatCaret <n>` fill the open entry and move its caret without device
  input.
- Name tags (`PublishRemasteredNameTags`): for a free-flying spectator,
  `oq4_tags` players in view, nearest first and at most eight, each with
  `oq4_tag{i}_x`/`_y` (the point above its head as fractions of the view),
  `_name`, `_team` (-1 outside team modes) and `_health`. The game projects each
  player through the spectator's render view and traces it, so no tag shows
  through a wall. `oq4_key_prev` names the control a following spectator steps
  back with: the weapon-cycle controls now step through the followed players
  outside Tourney, as Tourney's do.
- Managed context (`ProjectClientManagedMatchContext`): the full localized
  `match_context_*` record stays available to the scoreboard. During live
  play, `match_context_compact` limits the HUD to its phase and
  `match_context_summary`: an active proposal, otherwise the series, otherwise
  the remaining timeout budgets. Warm-up and pauses expand the record again.
- Respawn (`PublishRemasteredHud`): `oq4_respawn` is 1 while you lie dead
  and the attack control would bring you back, 2 during any server delay,
  including the normal three-second fall, with `oq4_respawn_in` seconds left,
  else 0. The control glyph and Respawn label remain beside the countdown on
  the atlas's y 318 u plate; the `oq4_respawn` event
  fades the prompt in as it appears. It follows `CheckRespawns`: round modes
  keep the eliminated out until the next round, sudden death in deathmatch
  brings back only the leaders, and Tourney only its arena's two players. A
  client mirrors the server's hold (the 3 s fall, longer in overtime) when its
  snapshot reports the death.
- Scoreboard (`idMultiplayerGame::PublishRemasteredScoreboard`): per panel
  `oq4_sb{p}_kind`, `_score` and `_count`, per row `oq4_sb{p}_{r}_name`, `_clan`,
  `_score`, `_kills`, `_deaths`, `_time`, `_ping` and `_flags` (you, dead, bot,
  eliminated, ready, muted, flag carrier, friend), the row pitch `oq4_sb_pitch`
  (16 u, the card's 24 dp rows), `oq4_sb_kills` and your place `oq4_sb_rank`.
  The header, match line and address read the stock keys. The document sizes
  the card from the panels' kinds and row counts. `testScoreboard <msec>` holds
  the board open without input.
- Scoreboard scrolling (section 14.14): when the panels outgrow the tallest
  card (496 dp of lists, 316 dp under Tourney's tree), the game publishes the
  window of whole rows the scroll shows, each panel heading the rows it shows,
  with the rows below as `oq4_sb_below` and their count's text
  `oq4_sb_below_text`. `PublishRemasteredScoreboard` lays the window out on
  `remastered_hud_scoreboard.py`'s numbers (`LIST_VIEW`, `TOURNEY_LIST_VIEW`,
  the 28 dp a panel opens with). While the held
  board scrolls in the Remastered layouts, the game sets the engine's
  `ui_scoreboardScrolls`, and the engine turns the weapon-cycle controls (the
  wheel) and the right stick (a row per push, then every 120 ms) into the
  game's `scoreboardScroll <rows>`: the scroll stays on the client and never
  reaches the server as a weapon change. Each hold opens at the top.
- Vehicles (`rvVehicle::UpdateHUD`): `oq4_key_jump`, the control that leaves,
  and `oq4_shipYaw`, the heading of whatever carries the vehicle (m03's
  dropship), against which the ship panel's cone shows the gun's heading.
- The speeder bike's boost (`riVehiclePartBoost`): `oq4_boost` (0 ready, 1
  pushing, 2 recharging), `oq4_boost_envelope` (the push's strength, 0 to 1,
  which the speed streak follows) and `oq4_key_crouch`, the control that fires
  it. The race's timer, checkpoints and countdown come from m07's scripts
  through the vehicle's script GUI events.
- The Awakening's on-foot documents carry the ship panel too, shown once m03's
  script sends the player's HUD a marker, damage or warning.
- Objectives (`idPlayer`): `oq4_new_title`/`oq4_new_shot` and `oq4_done_title`
  latched per notice, `oq4_obj_count` with `oq4_obj{i}_title/_text/_shot`
  newest first, `oq4_done_count` with `oq4_done{i}`, `oq4_obj_scroll`, and
  `oq4_failed_title`. Each notice closes on its own timer
  (`oq4_closeNewObjective`, `oq4_closeDoneObjective`), and the completion click
  plays once. While the display is open and holds more than three items, the
  weapon-cycle controls scroll it. `oq4_obj_toggle` reports Toggle Objectives
  (`g_objectivesToggle`, Settings > Game Options > HUD), under which each press
  of the scores button opens or closes the display, as the Xbox 360 edition's
  did; the display then shows a HUD prompt naming `oq4_key_scores` and Close.

## Match summary

With the retained menus the match summary is a multiplayer card, which covers
`guis/summary.gui` as the Escape card covers the game's menu
([the summary card](multiplayer-menus.md#the-summary-card)). With `ui_retained
0` the summary (`guis/summary.gui`, section 14.14) keeps the stock layout the
specification adopts, drawn through the presentation bridge. The bridge
draws the stock menu furniture as vector reconstructions authored in each
texture's own texels (`artwork.q4ui`, from `tools/ui/build_retained_hud.py`),
so each stretches and flips as the stock art does: the soft backing, the three
tab strips, the list bands (line, hover and greyed), the separators, the header
band, the light plate, the chat box, the card frame, the backdrop gradient and
the corner marker. Rails are filled one-texel geometry, never strokes, which
would thicken unevenly in a stretched tile. openQ4's
`content/baseoq4/pak0/guis/summary.gui` adds to the stock:

- **Draw.** A team match that ends level reads DRAW over neutral team panels
  (the managed result's `summary_result_*` presentation), where the stock named
  the Strogg the winners.
- **Exit prompt.** The engine publishes `oq4_summary_exit` (`#str_231257`)
  with the key of the device last used: Escape, or a controller's east button,
  which menus take as Escape.
- **Your row.** The corner marker in #E38900 stands 1 u before your row's band
  on the SUMMARY and SCOREBOARD tabs, beside the brighter band or a top-three
  tint. The game publishes each list's row as `<list>_you`, counted from 1 (0
  when you are not listed), apart from the selection that a click on the tab
  moves; each marker follows its list's `itemheight`. The top of your team,
  which the stock left unmarked, now gets the brighter band too.
- **Layouts.** Arena One Flag CTF (7) takes the flag modes' SCOREBOARD and
  STATISTICS layout, and Clan Arena, Freeze Tag and Red Rover (10–12) Team
  DM's, as `scoreboard.gui` does. They fell through to the Tourney layout,
  whose lists the team update never fills.

The engine's list window draws a row's `<list>_item_<i>_highlight` fill and
`<list>_item_<i>_greyed` band as Quake 4 1.4.2 does, in its order: the
selection, a greyed row, the hover band, the highlight, then the line band.
The summary's top three deathmatch rows take their blue, red and yellow tints
from it, and the classic scoreboard greys eliminated Tourney players.

## Runtime cost

HUD documents are large (about 7,200 authored values in the Marine document)
and animate continuously, which the retained runtime now handles without
walking every value:

- A frame applies only the motion values written since the last complete
  pass, plus the properties state bindings own when the state changed
  (`Motion::TakeChanged`). Input-time refreshes of a constrained menu popup
  stay complete passes.
- Motion values are copy-on-write, and an event samples the live motion in
  place (`Motion::AdvanceUndoable`), rolling the sample back if the event is
  rejected; an event no longer copies the document's values.
- Vector geometry caches quarter-pixel phases for elements moving by
  translation; the vehicle, ride and MCC gauges drain through transformed clip
  windows, so a regenerating shield never lays the document out again. RmlUi
  clips only content that overflows a window toward its trailing or bottom
  side, so each window's content box reaches past its trailing edge; the
  native checks measure the drawn fill, not only the window's transform.

- The implant overlay keeps edge coverage only on its bright cell and corner
  rims; its washes, wide bands, glows, static and runes set `"antialias": false`
  (vector-paths.md). With the sliding cache region for its falling data, that
  took its frames in the optimized native host from about 13 ms (353K
  vertices) to about 5 ms (163K) while the cells light, and its warm frames to
  no recompiles; the first activated frame compiles everything once.

`openq4-ui-retained-screens-test --hud-bench <document>` reports event, state
and frame costs for a document.

## Alarms and reduced motion

Every looping alarm follows section 14.10. Reduced motion
(`ui_retainedReducedMotion`) jumps a moving track to its last key and fades an
opacity track there within 80 ms, so each loop ends on its static cue: the
low-health rim, the boss shield, missile and vehicle hull and shield warnings
at full strength, the timer's last ten seconds steady #E46D56, the exit
chevron still at 0.50 and the electric warning at 0.70 (the last two snap back
to rest on their final millisecond, as their stock loops reset). Where the cue
is a loop's peak, a wrapper holds the peak under reduced motion instead: the
carried flag at 28 u (the stock beat, 22 to 28 u over 150 ms and back over
500 ms), a taken flag at 34 u, and the one flag in its carrier's team color,
which it otherwise swings to with each beat (there over 250 ms, back over
750 ms). Dead Zone's score follows the game's relaxing pulse; under reduced
motion each control tick holds the team's color for a second. A finished
Tourney arena's blue settles from 1.00 to 0.40 over 500 ms. The hit indicator
is essential and keeps its 500 ms fade.

Reduced flashing (`ui_reducedFlashing`, Settings > Game Options > HUD,
sections 13.10 and 14.7) caps the damage direction indicator at 0.40 in every
layout: the Remastered dome and chevron scale their leaves by 0.40/0.70, and
the Classic `hud.gui` and `hud_strogg.gui` play a 0.40 copy of the stock
indicator, which the game selects through the `reducedFlashing` GUI state it
sets with each hit (The Awakening's own Classic HUD keeps its stock
indicator). The vehicle hit glow peaks at 0.40, the game's full-screen white
flashes (teleporters and multiplayer spawns) start at 0.40 instead of white,
and the red death afterimage is dropped, as under reduced motion.

## Validation

- `openq4-ui-remastered-hud` loads every document through the real compiler and
  runtime and checks each group's anchors at several aspects, its states and
  its events. `openq4-ui-remastered-hud-generated` checks the generated
  documents are current.
- `tools/tests/remastered_hud_behavior.py --output .tmp/<dir>` executes the
  production live-context and respawn projections without devices or a game
  window. It checks summary priority, phase/pause expansion, preservation of
  the full record, ordinary and extended delay boundaries, and eliminated,
  spectator and Tourney eligibility. Both CI verification workflows run it.
- The October 2026 continuation records its source and staged identities in
  `.tmp/claude-continuation/hud/mp-followup-validation.json`. The production
  projection fixture passes 74 cases; MP/Competitive and both Strogg variants
  pass 13,440,569 native assertions. Fifteen engine screenshots verify normal
  respawn hold/readiness, active prompt glyphs, the live Clan Arena clock,
  real Tourney round-to-sudden-death events, and grain coverage with reduced
  motion. Managed-context compaction is covered by the production fixture
  and native layout checks; it is not claimed as a gameplay capture.
- A separate final stage widens Tourney's instructions to the available
  center-screen width and wraps localized text in vertical flow. Its native
  MP/Competitive run passes 861,569 assertions, including long instructions
  and an absent first row; a countdown engine capture confirms both stock
  instructions remain fully readable. The evidence preserves earlier stage
  identities and the failed capture-harness attempt. These scoped checks do
  not complete the broader device, editor or platform requirements.
- `tools/ui/capture_remastered_hud.py --assets <Quake 4> --output .tmp/<dir>
  --scenario <name>` captures windowed gameplay without host input: `marine`,
  `strogg`, `objectives`, `tank` (convoy1, boarding through the actor's
  `enterVehicle` script event), `airdef` (mcc_landing's cannon, boarded the
  same way), `turret` (m03's top gun, with `--awakening <content root holding
  q4xbase>`, after the level's own hand-off from its AI gunner), `mcc` (m06's
  side gun, the same way), `chaingun` (m01's trench turret, boarded through
  `enterVehicle`; the gun's own fire heats it past the warning level and its
  limit, and `setHealth` gives the gunner low health), `objectives-toggle` (Toggle Objectives opening,
  keeping and closing the display with presses), `speeder` (m07_race1, whose
  script seats the player
  on the bike and runs the countdown; the race's checkpoint event, the
  countdown's expiry and the crouch control drive the rest), `gev`
  (m09_valkaryne: the hover tank gives up its spot to the GEV, which no map
  places; the `damage` command drains the shield and, with god mode off, the
  hull, and `openq4_testInput` selects the machinegun and fires the cannon
  last. The GEV's model lacks every joint its def names (`eye`, `turret`,
  `gun_pivot`, `muzzle`): without an eye joint the game applies the hull's
  yaw twice, so `vehicle_rotate` reads the vehicle's own heading, and the
  cannon's kickback flings it and ejects the driver), `medchange` (medlabs,
  which starts with Kane on the medbed whose HUD is the implant overlay: the
  test command sends `activate`, `hurt` and `saveMessage` as the map script
  does, then under reduced motion and in Classic), `mp` (with the
  scoreboard and the respawn
  prompt after `kill 0`, then the chat input through `clientMessageMode`,
  `chatInput` and `chatCaret`), `mp-spectator` (in warm-up, so the match is not
  abandoned when the only human spectates: the follow card through
  `follownext`, then the free camera's name tags through `followfree`),
  `mp-ctf` (with the scoreboard), the Competitive `mp-competitive` (deathmatch),
  `mp-competitive-ctf`, `mp-competitive-spec` (both teams' lists while
  following and flying free) and `mp-competitive-matrix` (eight players a
  team, a long name and the chat input with a long entry; run it at each
  `--size`), `mp-summary`, `mp-summary-team` and `mp-summary-ca` (the match
  summary once bots reach a frag limit of one, or a Clan Arena duel's round
  ends, its SCOREBOARD and STATISTICS tabs opened through `openq4_guiAction`),
  `mp-scoreboard-scroll` and `mp-scoreboard-scroll-team` (22 and 20 players,
  the held board at its top, three rows down and at its end), `mp-tourney`
  (the bracket strip, message bar, timer and ROUND and ARENA plate),
  `mp-quickchat` and `mp-quickchat-team` (the quick-chat wheel through
  `testQuickChat`, a phrase sent on each channel), `mp-glyphs` (the prompts
  under each controller family through `testPromptDevice` and
  `in_promptStyle`), and the `mp-pace`/`sp-pace` profiles. `--gate 0` runs a scenario with the stock GUIs
  for comparison.

## Medical Labs implant captures, 24 October 2026

`capture_remastered_hud.py --scenario medchange-script` drives the implant
overlay (section 14.19) through the Medical Labs script itself rather than
the HUD test command: with Kane on the medbed at the level's start,
`script map_medlabs::playersTurn = 1` and `script map_medlabs::nanocyteFade()`
run the level's own function, whose white flash and `sendNamedEvent`
activate the overlay as the stroggification does; `determineScream()` sends
`hurt` with the player's scream, and a real save sends the game's
`saveMessage`. The level's own threads clear `playersTurn` as its sequence
moves on, so each call sets it first. The scenario captures the flash at
550 ms, the field at 1600 ms, the fade at 4250 ms, the hide by about
4850 ms (hidden at 4650 ms), the hurt pulse while hidden, the save notice,
a second activation 2 s into a first restarting from the beginning, and
reduced motion's steady cells. Captured at 4:3 (1024x768), 16:9 (1280x720),
21:9 (1260x540), 32:9 (1920x540) and 9:16 (540x960), and in German at 16:9:
the lattice continues through the side, top and bottom bands, the corner
pieces stand at the view's corners, the readouts keep their trailing-top and
leading-bottom anchors, and the notice reads "Spiel gespeichert..." from the
table. The harness gained `--language`, and `capture_retained_screens.run`
no longer requires `--set` from the harnesses that reuse it (the HUD, scope,
status bar, wheel, continuity and legacy harnesses had failed before
starting).

Evidence: `.tmp/ui/implant-overlay/validation-evidence.json`, SHA-256
`1c1e411486a484bbee29a4fde2c2d27ee482cc6bd75aef5b0db8d5800a1a9980`.

## Hit-feedback defaults, 21 October 2026

Fresh SP and MP profiles now default `hud_hitMarker` to zero, preserving the
stock crosshair feedback required by visual section 14.7. The setting remains
archived: an existing explicit opt-in is retained, and Reset returns to zero.
The Game settings description now states the same default. No saved preference
migration or hit-feedback transport changed.

Four silent, windowed OpenGL runs enter SP Hangar 1 and MP q4dm1. In each mode,
the fresh value is zero, an explicit opt-in is written, a separate launch loads
that saved value over a disabled startup configuration, and Reset returns to
zero. Engine captures show no diagnostic marker when disabled and the existing
marker when enabled. They do not qualify real-hit timing, the retained settings
workflow or the full artwork specification. The first harness attempt used an
unquoted configuration filename, failed its explicit-write check and is retained.

Evidence: `.tmp/stage-two/hit-feedback-default-runtime-r2/report.json`, SHA-256
`f31e99427af160c78a9e44ae3fb106ad1ce35c3e122241833cdf9cb4d1b3e0b7`.
Source/runtime identities and all four engine logs are bound in that report;
`hit-marker-review.png` contains the inspected engine-capture crops. The optional
marker's color, duration and geometry still differ from section 14.7 and remain
under UI-05; this default correction does not accept `FLOW-043`.

## Open work

- The chat input's prompts name the keyboard's keys; controller and touch
  players' platform or on-screen keyboard (section 13.5) is not implemented.
- The touch control that toggles the objectives display is not implemented.
- Voice plates: the engine's voice capture and playback are stubbed out
  (`idMultiplayerGame` never reports a talker or a transmission), so the plates
  stay dormant, and section 14.9's one plate per talker needs the voice
  backend's talker list.
- Hit markers: section 14.7 specifies white ticks for a hit and
  `status.error` for a kill over 150 ms; the shipped markers
  (`src/mpgame/HitMarker.cpp`) are tiered, red for a hit and white for a kill,
  over 180-420 ms. One of the two needs a decision.
- Subtitles (section 14.8) do not exist yet.
- The implant overlay (`FLOW-062`) has engine captures through the level's
  own script functions at each aspect
  ([captures](#medical-labs-implant-captures-24-october-2026)); a run of the
  whole stroggification from the level start, other renderers and platforms
  and art review remain.
- Competitive flag warnings, the connection-warning plate and the Dead
  Zone holding sub-label still need the reviewed visual treatment. The
  current chat and kill-feed fades advance in 50 ms steps.
- Match-summary weapon symbols, controller prompts, and score/team order
  during review remain outside the wheel and on-foot correction checkpoint.
  The written specification governs the scoreboard card; its atlas drawing
  has not yet caught up.
